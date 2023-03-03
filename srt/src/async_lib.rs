use super::{
    check_code,
    epoll_reactor::{EpollReactor, READ_EVENTS, WRITE_EVENTS},
    listener_callback, new_io_error, sockaddr_from_storage, sys, to_sockaddr, ConnectOptions, Error, ListenerCallback, ListenerOption, Result, Socket,
};
use std::{
    future::Future,
    io, mem,
    net::{SocketAddr, ToSocketAddrs},
    pin::Pin,
    sync::Arc,
    task::{Context, Poll},
};
use tokio::{
    io::{AsyncRead, AsyncWrite, ReadBuf},
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
                check_code("srt_bind", sys::srt_bind(socket.raw(), &addr as *const _ as _, len as _))?;
            }
        }
        unsafe {
            check_code("srt_listen", sys::srt_listen(socket.raw(), 10))?;
        }
        Ok(Self { socket, _callback: None })
    }
}

impl<'c> AsyncListener<'c> {
    pub fn with_callback<F: ListenerCallback + 'c>(self, f: F) -> Result<AsyncListener<'c>> {
        let mut cb: Box<Box<dyn ListenerCallback>> = Box::new(Box::new(f));
        let ptr = &mut *cb as *mut Box<dyn ListenerCallback>;
        let pb = unsafe { Pin::new_unchecked(cb) };
        check_code("srt_listen_callback", unsafe {
            sys::srt_listen_callback(self.socket.raw(), Some(listener_callback), ptr as *mut _)
        })?;
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
                // TODO: use epoll instead of spawn_blocking
                let mut handle = spawn_blocking(move || {
                    let mut storage: sys::sockaddr_storage = unsafe { mem::zeroed() };
                    let mut len = mem::size_of_val(&storage) as sys::socklen_t;
                    let sock = unsafe { sys::srt_accept(sock, &mut storage as *mut _ as *mut _, &mut len as *mut _ as *mut _) };
                    let socket = Socket { api, sock };
                    let addr = sockaddr_from_storage(&storage, len)?;
                    Ok((AsyncStream::new(socket.get(sys::SRT_SOCKOPT_SRTO_STREAMID)?, socket)?, addr))
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

pub struct AsyncStream {
    epoll_reactor: Arc<EpollReactor>, // must be dropped before socket
    socket: Socket,
    id: Option<String>,
    payload_size: usize, // maximum payload size can be sent in one UDP packet
}

impl AsyncStream {
    fn new(id: Option<String>, socket: Socket) -> Result<Self> {
        socket.set(sys::SRT_SOCKOPT_SRTO_SNDSYN, false)?;
        socket.set(sys::SRT_SOCKOPT_SRTO_RCVSYN, false)?;
        let payload_size = socket.get(sys::SRT_SOCKOPT_SRTO_PAYLOADSIZE).unwrap_or(1316) as _;
        Ok(Self {
            epoll_reactor: socket.api.get_epoll_reactor()?,
            socket,
            id,
            payload_size,
        })
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

    /// Returns the underlying stats for the socket.
    ///
    /// Refer to https://github.com/Haivision/srt/blob/v1.4.4/docs/API/statistics.md to learn more about them.
    pub fn raw_stats(&mut self, clear: bool, instantaneous: bool) -> Result<sys::SRT_TRACEBSTATS> {
        self.socket.raw_stats(clear, instantaneous)
    }

    pub fn payload_size(&self) -> usize {
       self.payload_size
    }
}

struct Connect {
    addr: SocketAddr,
    options: ConnectOptions,
    state: State<AsyncStream>,
}

impl Future for Connect {
    type Output = Result<AsyncStream>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Self::Output> {
        match &mut self.state {
            State::Idle => {
                let addr = self.addr;
                let options = self.options.clone();
                // TODO: use epoll instead of spawn_blocking
                let mut handle = spawn_blocking(move || {
                    let (addr, len) = to_sockaddr(&addr);
                    let socket = Socket::new()?;
                    socket.set_connect_options(&options)?;
                    unsafe {
                        check_code("srt_connect", sys::srt_connect(socket.raw(), &addr as *const _ as _, len as _))?;
                    }
                    AsyncStream::new(options.stream_id, socket)
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
    fn poll_read(self: Pin<&mut Self>, cx: &mut Context, buf: &mut ReadBuf) -> Poll<io::Result<()>> {
        if let Ok(events) = self.socket.get::<i32>(sys::SRT_SOCKOPT_SRTO_EVENT) {
            if events & READ_EVENTS == 0 {
                self.epoll_reactor.wake_when_read_ready(&self.socket, cx.waker().clone());
                return Poll::Pending;
            }
        }
        let sock = self.socket.raw();
        match unsafe { sys::srt_recv(sock, buf.unfilled_mut().as_mut_ptr() as *mut sys::char, buf.remaining() as _) } {
            len if len >= 0 => {
                unsafe { buf.assume_init(len as _) };
                buf.advance(len as _);
                Poll::Ready(Ok(()))
            }
            _ => Poll::Ready(Err(new_io_error("srt_recv"))),
        }
    }
}

impl AsyncWrite for AsyncStream {
    fn poll_write(self: Pin<&mut Self>, cx: &mut Context<'_>, src: &[u8]) -> Poll<io::Result<usize>> {
        if let Ok(events) = self.socket.get::<i32>(sys::SRT_SOCKOPT_SRTO_EVENT) {
            if events & WRITE_EVENTS == 0 {
                self.epoll_reactor.wake_when_write_ready(&self.socket, cx.waker().clone());
                return Poll::Pending;
            }
        }
        let sock = self.socket.raw();
        let mut sent = 0;
        while sent < src.len() {
            let data = &src[sent..];
            let len = self.payload_size.min(data.len());
            sent += match unsafe { sys::srt_send(sock, data.as_ptr() as *const sys::char, len as _) } {
                sent  if sent >= 0 => sent as usize,
                _ => return Poll::Ready(Err(new_io_error("srt_send"))),
            };
        }

        Poll::Ready(Ok(sent))
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_shutdown(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

impl Drop for AsyncStream {
    fn drop(&mut self) {
        self.epoll_reactor.remove_socket(&self.socket);
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
        let listener = AsyncListener::bind_with_options(
            "127.0.0.1:1235",
            [
                ListenerOption::TimestampBasedPacketDeliveryMode(false),
                ListenerOption::TooLatePacketDrop(false),
                ListenerOption::ReceiveBufferSize(36400000),
            ]
            .iter()
            .cloned(),
        )
        .unwrap();

        let options = ConnectOptions {
            timestamp_based_packet_delivery_mode: Some(false),
            too_late_packet_drop: Some(false),
            ..Default::default()
        };
        let (accept_result, connect_result) = join!(listener.accept(), AsyncStream::connect("127.0.0.1:1235", &options));
        let mut server_conn = accept_result.unwrap().0;
        let mut client_conn = connect_result.unwrap();
        assert_eq!(client_conn.id(), None);
        assert_eq!(client_conn.payload_size(), 1316);

        let mut buf = [0; 1316];
        for i in 0..5 {
            let payload = [i as u8; 1316];

            for _ in 0..10 {
                assert_eq!(client_conn.write(&payload).await.unwrap(), 1316);
            }

            for _ in 0..10 {
                assert_eq!(server_conn.read(&mut buf).await.unwrap(), 1316);
                assert_eq!(&buf, &payload);
            }
        }

        assert!(server_conn.raw_stats(false, false).unwrap().pktRecvTotal > 0);
    }

    #[tokio::test]
    async fn test_async_client_server_disconnect() {
        let listener = AsyncListener::bind("127.0.0.1:1238").unwrap();

        let options = Default::default();
        let (accept_result, connect_result) = join!(listener.accept(), AsyncStream::connect("127.0.0.1:1238", &options));
        let mut server_conn = accept_result.unwrap().0;
        let client_conn = connect_result.unwrap();
        assert_eq!(client_conn.id(), None);

        // Drop the client.
        mem::drop(client_conn);

        // This read should return an error immediately.
        let mut buf = [0; 1316];
        assert!(server_conn.read(&mut buf).await.is_err());
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

        let mut options = ConnectOptions {
            passphrase: Some("notthepassphrase".to_string()),
            stream_id: Some("mystreamid".to_string()),
            ..Default::default()
        };

        assert!(AsyncStream::connect("127.0.0.1:1237", &options).await.is_err());

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
