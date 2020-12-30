use super::{
    check_code, listener_callback, sockaddr_from_storage, sys, to_sockaddr, ConnectOptions, Error, ListenerCallback, ListenerOption, Message, Result, Socket,
};
use std::{
    future::Future,
    io,
    mem::{self, MaybeUninit},
    net::{SocketAddr, ToSocketAddrs},
    pin::Pin,
    task::{Context, Poll},
};
use tokio::{
    prelude::{AsyncRead, AsyncWrite},
    task::{spawn_blocking, JoinError, JoinHandle},
};

impl From<JoinError> for Error {
    fn from(e: JoinError) -> Error {
        Error::JoinError(e)
    }
}

pub struct AsyncListener<'c> {
    socket: Socket,
    _callback: Option<Pin<Box<Box<dyn ListenerCallback + 'c>>>>,
}

impl AsyncListener<'static> {
    pub fn bind<A: ToSocketAddrs>(addr: A) -> Result<Self> {
        Self::bind_with_options(addr, vec![])
    }

    pub fn bind_with_options<A: ToSocketAddrs, O: IntoIterator<Item = ListenerOption>>(addr: A, opts: O) -> Result<Self> {
        let socket = Socket::new()?;
        for opt in opts.into_iter() {
            opt.set(&socket)?;
        }
        for addr in addr.to_socket_addrs()? {
            let (addr, len) = to_sockaddr(&addr);
            unsafe {
                check_code(sys::srt_bind(socket.raw(), addr, len as _))?;
            }
        }
        unsafe {
            check_code(sys::srt_listen(socket.raw(), 10))?;
        }
        Ok(Self { socket, _callback: None })
    }
}

impl<'c> AsyncListener<'c> {
    pub fn with_callback<F: ListenerCallback + 'c>(self, f: F) -> Result<AsyncListener<'c>> {
        let mut cb: Box<Box<dyn ListenerCallback>> = Box::new(Box::new(f));
        let ptr = &mut *cb as *mut Box<dyn ListenerCallback>;
        let pb = unsafe { Pin::new_unchecked(cb) };
        check_code(unsafe { sys::srt_listen_callback(self.socket.raw(), listener_callback, ptr as *mut _) })?;
        Ok(AsyncListener {
            _callback: Some(pb),
            socket: self.socket,
        })
    }

    pub async fn accept(&self) -> Result<(AsyncStream, SocketAddr)> {
        Accept {
            listener: self,
            state: State::Idle,
        }
        .await
    }
}

enum State<T> {
    Idle,
    Busy(JoinHandle<Result<T>>),
}

struct Accept<'a, 'c> {
    listener: &'a AsyncListener<'c>,
    state: State<(AsyncStream, SocketAddr)>,
}

impl<'a, 'c> Future for Accept<'a, 'c> {
    type Output = Result<(AsyncStream, SocketAddr)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
        match &mut self.state {
            State::Idle => {
                let sock = self.listener.socket.raw();
                let api = self.listener.socket.api.clone();
                let mut handle = spawn_blocking(move || {
                    let mut storage: sys::sockaddr_storage = unsafe { mem::zeroed() };
                    let mut len = mem::size_of_val(&storage) as sys::socklen_t;
                    let sock = unsafe { sys::srt_accept(sock, &mut storage as *mut _ as *mut _, &mut len as *mut _ as *mut _) };
                    let socket = Socket { api, sock };
                    let addr = sockaddr_from_storage(&storage, len)?;
                    Ok((AsyncStream::new(socket.get(sys::SRT_SOCKOPT::STREAMID)?, socket), addr))
                });
                let ret = Pin::new(&mut handle).poll(cx);
                self.state = State::Busy(handle);
                ret
            }
            State::Busy(ref mut handle) => Pin::new(handle).poll(cx),
        }
        .map(|r| match r {
            Ok(r) => r,
            Err(e) => Err(e.into()),
        })
    }
}

enum IOState {
    Idle(Option<Vec<u8>>),
    Reading(JoinHandle<(Vec<u8>, io::Result<(usize, sys::SRT_MSGCTRL)>)>),
    Writing(JoinHandle<(Vec<u8>, io::Result<usize>)>),
}

pub struct AsyncStream {
    socket: Socket,
    read_state: IOState,
    write_state: IOState,
    id: Option<String>,
}

impl AsyncStream {
    fn new(id: Option<String>, socket: Socket) -> Self {
        Self {
            id,
            socket,
            read_state: IOState::Idle(None),
            write_state: IOState::Idle(None),
        }
    }

    pub async fn connect<A: ToSocketAddrs>(addr: A, options: &ConnectOptions) -> Result<Self> {
        let mut last_err = Error::InvalidAddress;
        for addr in addr.to_socket_addrs()? {
            let f = Connect {
                addr,
                options: options.clone(),
                state: State::Idle,
            };
            match f.await {
                Err(e) => last_err = e,
                Ok(s) => return Ok(s),
            }
        }
        Err(last_err)
    }

    pub fn id(&self) -> Option<&String> {
        self.id.as_ref()
    }

    pub fn receive<'a>(&'a mut self, buf: &'a mut [u8]) -> Receive<'a> {
        Receive { stream: self, buf: Some(buf) }
    }

    fn poll_read_impl(&mut self, cx: &mut Context, len: usize) -> Poll<std::result::Result<(Vec<u8>, io::Result<(usize, sys::SRT_MSGCTRL)>), JoinError>> {
        match &mut self.read_state {
            IOState::Idle(ref mut recv_buf) => {
                let mut recv_buf = match recv_buf.take() {
                    Some(b) => b,
                    None => Vec::new(),
                };
                recv_buf.resize(len, 0);
                let sock = self.socket.raw();
                let mut handle = spawn_blocking(move || {
                    let mut mc = sys::SRT_MSGCTRL::default();
                    let r = match unsafe { sys::srt_recvmsg2(sock, recv_buf.as_mut_ptr() as *mut sys::char, recv_buf.len() as _, &mut mc as _) } {
                        len if len >= 0 => Ok((len as usize, mc)),
                        _ => Err(io::Error::new(io::ErrorKind::Other, "srt_recvmsg2 error")),
                    };
                    (recv_buf, r)
                });
                let poll = Pin::new(&mut handle).poll(cx);
                self.read_state = IOState::Reading(handle);
                poll
            }
            IOState::Reading(handle) => Pin::new(handle).poll(cx),
            IOState::Writing(_) => panic!("read polled during pending write"),
        }
    }
}

struct Connect {
    addr: SocketAddr,
    options: ConnectOptions,
    state: State<AsyncStream>,
}

impl<'a> Future for Connect {
    type Output = Result<AsyncStream>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
        match &mut self.state {
            State::Idle => {
                let addr = self.addr;
                let options = self.options.clone();
                let mut handle = spawn_blocking(move || {
                    let (addr, len) = to_sockaddr(&addr);
                    let socket = Socket::new()?;
                    socket.set_connect_options(&options)?;
                    unsafe {
                        check_code(sys::srt_connect(socket.raw(), addr, len as _))?;
                    }
                    Ok(AsyncStream::new(options.stream_id, socket))
                });
                let ret = Pin::new(&mut handle).poll(cx);
                self.state = State::Busy(handle);
                ret
            }
            State::Busy(ref mut handle) => Pin::new(handle).poll(cx),
        }
        .map(|r| match r {
            Ok(r) => r,
            Err(e) => Err(e.into()),
        })
    }
}

impl AsyncRead for AsyncStream {
    unsafe fn prepare_uninitialized_buffer(&self, _: &mut [MaybeUninit<u8>]) -> bool {
        false
    }

    fn poll_read(mut self: Pin<&mut Self>, cx: &mut Context, buf: &mut [u8]) -> Poll<io::Result<usize>> {
        match self.poll_read_impl(cx, buf.len()) {
            Poll::Ready(Ok((recv_buf, result))) => {
                if let Ok((n, _)) = result {
                    let n = n.min(buf.len());
                    buf[..n].copy_from_slice(&recv_buf[..n]);
                }
                self.read_state = IOState::Idle(Some(recv_buf));
                Poll::Ready(result.map(|r| r.0))
            }
            Poll::Ready(Err(join_error)) => {
                self.read_state = IOState::Idle(None);
                Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, format!("join error: {}", join_error))))
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

pub struct Receive<'a> {
    stream: &'a mut AsyncStream,
    buf: Option<&'a mut [u8]>,
}

impl<'a> Future for Receive<'a> {
    type Output = io::Result<Message<'a>>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
        let recv_len = match &self.buf {
            Some(buf) => buf.len(),
            None => panic!("poll called after completion"),
        };
        match self.stream.poll_read_impl(cx, recv_len) {
            Poll::Ready(Ok((recv_buf, result))) => {
                let result = result.map(|(n, mc)| {
                    let buf = self.buf.take().expect("we ensured that we have buf above");
                    buf[..n].copy_from_slice(&recv_buf[..n]);
                    Message {
                        data: &buf[..],
                        source_time_usec: match mc.srctime {
                            0 => None,
                            usec @ _ => Some(usec),
                        },
                    }
                });
                self.stream.read_state = IOState::Idle(Some(recv_buf));
                Poll::Ready(result)
            }
            Poll::Ready(Err(join_error)) => {
                self.stream.read_state = IOState::Idle(None);
                Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, format!("join error: {}", join_error))))
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

impl AsyncWrite for AsyncStream {
    fn poll_write(mut self: Pin<&mut Self>, cx: &mut Context<'_>, src: &[u8]) -> Poll<io::Result<usize>> {
        let poll = match &mut self.write_state {
            IOState::Idle(ref mut send_buf) => {
                let mut send_buf = match send_buf.take() {
                    Some(b) => b,
                    None => Vec::new(),
                };
                send_buf.resize(src.len(), 0);
                send_buf.copy_from_slice(src);
                let sock = self.socket.raw();
                let mut handle = spawn_blocking(move || {
                    let r = match unsafe { sys::srt_send(sock, send_buf.as_ptr() as *const sys::char, send_buf.len() as _) } {
                        len if len >= 0 => Ok(len as usize),
                        _ => Err(io::Error::new(io::ErrorKind::Other, "srt_send error")),
                    };
                    (send_buf, r)
                });
                let poll = Pin::new(&mut handle).poll(cx);
                self.write_state = IOState::Writing(handle);
                poll
            }
            IOState::Writing(handle) => Pin::new(handle).poll(cx),
            IOState::Reading(_) => panic!("write polled during pending read"),
        };

        match poll {
            Poll::Ready(Ok((send_buf, result))) => {
                self.write_state = IOState::Idle(Some(send_buf));
                Poll::Ready(result)
            }
            Poll::Ready(Err(join_error)) => {
                self.write_state = IOState::Idle(None);
                Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, format!("join error: {}", join_error))))
            }
            Poll::Pending => Poll::Pending,
        }
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_shutdown(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

#[cfg(test)]
mod test {
    use super::{
        super::{ListenerCallbackAction, ListenerOption},
        *,
    };
    use tokio::{
        io::{AsyncReadExt, AsyncWriteExt},
        join,
    };

    #[tokio::test]
    async fn test_async_client_server() {
        let listener = AsyncListener::bind("127.0.0.1:1235").unwrap();

        let options = &ConnectOptions::default();
        let (accept_result, connect_result) = join!(listener.accept(), AsyncStream::connect("127.0.0.1:1235", options));
        let mut server_conn = accept_result.unwrap().0;
        let mut client_conn = connect_result.unwrap();
        assert_eq!(client_conn.id(), None);

        assert_eq!(client_conn.write(b"foo").await.unwrap(), 3);

        let mut buf = [0; 1316];
        assert_eq!(server_conn.read(&mut buf).await.unwrap(), 3);
        assert_eq!(&buf[0..3], b"foo");
    }

    #[tokio::test]
    async fn test_async_passphrase() {
        let listener = AsyncListener::bind_with_options("127.0.0.1:1237", [ListenerOption::TooLatePacketDrop(false)].iter().cloned())
            .unwrap()
            .with_callback(|stream_id: Option<&_>| {
                assert_eq!(stream_id, Some("mystreamid"));
                ListenerCallbackAction::Allow {
                    passphrase: Some("thepassphrase".to_string()),
                }
            })
            .unwrap();

        let mut options = ConnectOptions::default();
        options.stream_id = Some("mystreamid".to_string());

        options.passphrase = Some("notthepassphrase".to_string());
        assert_eq!(AsyncStream::connect("127.0.0.1:1237", &options).await.is_err(), true);

        options.passphrase = Some("thepassphrase".to_string());
        let (accept_result, connect_result) = join!(listener.accept(), AsyncStream::connect("127.0.0.1:1237", &options));
        let mut server_conn = accept_result.unwrap().0;
        let mut client_conn = connect_result.unwrap();
        assert_eq!(client_conn.id(), options.stream_id.as_ref());

        assert_eq!(client_conn.write(b"foo").await.unwrap(), 3);

        let mut buf = [0; 1316];
        assert_eq!(server_conn.read(&mut buf).await.unwrap(), 3);
        assert_eq!(&buf[0..3], b"foo");
    }
}
