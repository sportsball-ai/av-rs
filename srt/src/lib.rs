#[macro_use]
extern crate lazy_static;

use std::{
    ffi::CStr,
    fmt,
    io::{self, Read, Write},
    mem,
    net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6, ToSocketAddrs},
    pin::Pin,
    str,
    sync::{Arc, Mutex, Weak},
};

#[cfg(feature = "async")]
mod async_lib;
#[cfg(feature = "async")]
pub use async_lib::*;

mod sys;

#[derive(Debug)]
pub enum Error {
    InvalidAddress,
    UnsupportedFamily(sys::int),
    IOError(io::Error),
    Utf8Error(str::Utf8Error),
    SRTError(sys::int, sys::int),
    #[cfg(feature = "async")]
    JoinError(tokio::task::JoinError),
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Self::IOError(e)
    }
}

impl From<str::Utf8Error> for Error {
    fn from(e: str::Utf8Error) -> Self {
        Self::Utf8Error(e)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidAddress => write!(f, "invalid address"),
            Self::UnsupportedFamily(family) => write!(f, "unsupported family: {}", family),
            Self::IOError(e) => write!(f, "io error: {}", e),
            Self::Utf8Error(e) => write!(f, "utf8 error: {}", e),
            Self::SRTError(code, errno) => write!(f, "srt error: {} (errno = {})", code, errno),
            #[cfg(feature = "async")]
            Self::JoinError(e) => write!(f, "join error: {}", e),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

pub(crate) fn new_io_error(fn_name: &'static str) -> io::Error {
    let mut errno = 0;
    let code = unsafe { sys::srt_getlasterror(&mut errno as _) };
    io::Error::new(io::ErrorKind::Other, format!("{} error: {} (errno = {})", fn_name, code, errno))
}

fn check_code(code: sys::int) -> Result<()> {
    match code {
        0 => Ok(()),
        _ => {
            let mut errno = 0;
            let code = unsafe { sys::srt_getlasterror(&mut errno as _) };
            Err(Error::SRTError(code, errno))
        }
    }
}

struct API;

lazy_static! {
    static ref GLOBAL_API: Mutex<Option<Weak<API>>> = Mutex::new(None);
}

impl API {
    fn get() -> Result<Arc<API>> {
        let mut api = GLOBAL_API.lock().unwrap();
        let existing = (*api).as_ref().and_then(|api| api.upgrade());
        match existing {
            Some(api) => Ok(api),
            None => {
                unsafe {
                    check_code(sys::srt_startup())?;
                }
                let new_api = Arc::new(Self);
                *api = Some(Arc::downgrade(&new_api));
                Ok(new_api)
            }
        }
    }
}

impl Drop for API {
    fn drop(&mut self) {
        unsafe {
            sys::srt_cleanup();
        }
    }
}

struct Socket {
    api: Arc<API>,
    sock: sys::SRTSOCKET,
}

trait ToOption {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()>;
}

impl ToOption for &String {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code(unsafe { sys::srt_setsockopt(sock, 0, opt, self.as_ptr() as *const _, self.len() as _) })
    }
}

impl ToOption for bool {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code(unsafe { sys::srt_setsockopt(sock, 0, opt, self as *const bool as *const _, std::mem::size_of::<bool>() as _) })
    }
}

impl ToOption for i32 {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code(unsafe { sys::srt_setsockopt(sock, 0, opt, self as *const i32 as *const _, std::mem::size_of::<i32>() as _) })
    }
}

trait FromOption: Sized {
    fn get(sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<Self>;
}

impl FromOption for Option<String> {
    fn get(sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<Self> {
        let mut buf = [0u8; 512];
        let mut len = buf.len();
        check_code(unsafe { sys::srt_getsockopt(sock, 0, opt, buf.as_mut_ptr() as *mut _, &mut len as *mut _ as *mut _) })?;
        Ok(match len {
            0 => None,
            len => Some(str::from_utf8(&buf[..len])?.to_string()),
        })
    }
}

impl Socket {
    fn new() -> Result<Socket> {
        Ok(Socket {
            api: API::get()?,
            sock: unsafe { sys::srt_create_socket() },
        })
    }

    fn raw(&self) -> sys::SRTSOCKET {
        self.sock
    }

    fn set_connect_options(&self, options: &ConnectOptions) -> Result<()> {
        if let Some(v) = &options.passphrase {
            self.set(sys::SRT_SOCKOPT::PASSPHRASE, v)?;
        }
        if let Some(v) = &options.stream_id {
            self.set(sys::SRT_SOCKOPT::STREAMID, v)?;
        }
        if let Some(v) = &options.too_late_packet_drop {
            self.set(sys::SRT_SOCKOPT::TLPKTDROP, *v)?;
        }
        if let Some(v) = &options.timestamp_based_packet_delivery_mode {
            self.set(sys::SRT_SOCKOPT::TSBPDMODE, *v)?;
        }
        if let Some(v) = &options.receive_buffer_size {
            self.set(sys::SRT_SOCKOPT::RCVBUF, *v)?;
        }
        Ok(())
    }

    fn set<T: ToOption>(&self, opt: sys::SRT_SOCKOPT, value: T) -> Result<()> {
        value.set(self.sock, opt)
    }

    fn get<T: FromOption>(&self, opt: sys::SRT_SOCKOPT) -> Result<T> {
        T::get(self.sock, opt)
    }
}

impl Drop for Socket {
    fn drop(&mut self) {
        unsafe {
            sys::srt_close(self.sock);
        }
    }
}

fn sockaddr_from_storage(storage: &sys::sockaddr_storage, len: sys::socklen_t) -> Result<SocketAddr> {
    // from: https://github.com/rust-lang/rust/blob/7c78a5f97de07a185eebae5a5de436c80d8ba9d4/src/libstd/sys_common/net.rs#L95
    use libc::{c_int, sockaddr_in, sockaddr_in6, AF_INET, AF_INET6};
    match storage.ss_family as c_int {
        AF_INET => {
            assert!(len as usize >= mem::size_of::<sockaddr_in>());
            Ok(SocketAddr::V4(SocketAddrV4::new(
                Ipv4Addr::from(unsafe { u32::from_be((*(storage as *const _ as *const sockaddr_in)).sin_addr.s_addr as u32) }),
                unsafe { u16::from_be((*(storage as *const _ as *const sockaddr_in)).sin_port) },
            )))
        }
        AF_INET6 => {
            assert!(len as usize >= mem::size_of::<sockaddr_in6>());
            Ok(SocketAddr::V6(SocketAddrV6::new(
                Ipv6Addr::from(unsafe { (*(storage as *const _ as *const sockaddr_in6)).sin6_addr.s6_addr }),
                unsafe { u16::from_be((*(storage as *const _ as *const sockaddr_in6)).sin6_port) },
                unsafe { u32::from_be((*(storage as *const _ as *const sockaddr_in6)).sin6_flowinfo) },
                unsafe { u32::from_be((*(storage as *const _ as *const sockaddr_in6)).sin6_scope_id) },
            )))
        }
        f => Err(Error::UnsupportedFamily(f)),
    }
}

fn to_sockaddr(addr: &SocketAddr) -> (*const sys::sockaddr, sys::socklen_t) {
    match addr {
        SocketAddr::V4(ref a) => (a as *const _ as *const _, mem::size_of_val(a) as _),
        SocketAddr::V6(ref a) => (a as *const _ as *const _, mem::size_of_val(a) as _),
    }
}

pub trait ListenerCallback {
    fn callback(&self, stream_id: Option<&str>) -> ListenerCallbackAction;
}

impl<T: Fn(Option<&str>) -> ListenerCallbackAction> ListenerCallback for T {
    fn callback(&self, stream_id: Option<&str>) -> ListenerCallbackAction {
        (*self)(stream_id)
    }
}

pub enum ListenerCallbackAction {
    Deny,
    Allow { passphrase: Option<String> },
}

pub struct Listener<'c> {
    socket: Socket,
    _callback: Option<Pin<Box<Box<dyn ListenerCallback + 'c>>>>,
}

extern "C" fn listener_callback(
    opaq: *mut sys::void,
    ns: sys::SRTSOCKET,
    _hs_version: sys::int,
    _peer: *const sys::sockaddr,
    stream_id: *const sys::char,
) -> sys::int {
    unsafe {
        let f = opaq as *mut Box<dyn ListenerCallback>;
        let stream_id = if stream_id.is_null() { None } else { CStr::from_ptr(stream_id).to_str().ok() };
        match (*f).callback(stream_id) {
            ListenerCallbackAction::Deny => -1,
            ListenerCallbackAction::Allow { passphrase } => {
                if let Some(v) = &passphrase {
                    if v.set(ns, sys::SRT_SOCKOPT::PASSPHRASE).is_err() {
                        return -1;
                    }
                }
                0
            }
        }
    }
}

#[derive(Clone, Debug)]
pub enum ListenerOption {
    TimestampBasedPacketDeliveryMode(bool),
    TooLatePacketDrop(bool),
    ReceiveBufferSize(i32),
}

impl ListenerOption {
    pub(crate) fn set(&self, sock: &Socket) -> Result<()> {
        match self {
            ListenerOption::TimestampBasedPacketDeliveryMode(v) => sock.set(sys::SRT_SOCKOPT::TSBPDMODE, *v),
            ListenerOption::TooLatePacketDrop(v) => sock.set(sys::SRT_SOCKOPT::TLPKTDROP, *v),
            ListenerOption::ReceiveBufferSize(v) => sock.set(sys::SRT_SOCKOPT::RCVBUF, *v),
        }
    }
}

impl Listener<'static> {
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

impl<'c> Listener<'c> {
    pub fn with_callback<F: ListenerCallback + 'c>(self, f: F) -> Result<Listener<'c>> {
        let mut cb: Box<Box<dyn ListenerCallback>> = Box::new(Box::new(f));
        let ptr = &mut *cb as *mut Box<dyn ListenerCallback>;
        let pb = unsafe { Pin::new_unchecked(cb) };
        check_code(unsafe { sys::srt_listen_callback(self.socket.raw(), listener_callback, ptr as *mut _) })?;
        Ok(Listener {
            _callback: Some(pb),
            socket: self.socket,
        })
    }

    pub fn accept(&self) -> Result<(Stream, SocketAddr)> {
        let mut storage: sys::sockaddr_storage = unsafe { mem::zeroed() };
        let mut len = mem::size_of_val(&storage) as sys::socklen_t;
        let sock = unsafe { sys::srt_accept(self.socket.raw(), &mut storage as *mut _ as *mut _, &mut len as *mut _ as *mut _) };
        let socket = Socket {
            api: self.socket.api.clone(),
            sock,
        };
        let addr = sockaddr_from_storage(&storage, len)?;
        Ok((
            Stream {
                id: socket.get(sys::SRT_SOCKOPT::STREAMID)?,
                socket,
            },
            addr,
        ))
    }
}

pub struct Stream {
    socket: Socket,
    id: Option<String>,
}

#[derive(Clone, Debug, Default)]
pub struct ConnectOptions {
    pub passphrase: Option<String>,
    pub stream_id: Option<String>,
    pub timestamp_based_packet_delivery_mode: Option<bool>,
    pub too_late_packet_drop: Option<bool>,
    pub receive_buffer_size: Option<i32>,
}

impl Stream {
    pub fn connect<A: ToSocketAddrs>(addr: A, options: &ConnectOptions) -> Result<Self> {
        let mut last_err = Error::InvalidAddress;
        for addr in addr.to_socket_addrs()? {
            let (addr, len) = to_sockaddr(&addr);
            let socket = Socket::new()?;
            socket.set_connect_options(&options)?;
            unsafe {
                match check_code(sys::srt_connect(socket.raw(), addr, len as _)) {
                    Err(e) => last_err = e,
                    Ok(_) => {
                        return Ok(Self {
                            socket,
                            id: options.stream_id.clone(),
                        })
                    }
                }
            }
        }
        Err(last_err)
    }

    pub fn id(&self) -> Option<&String> {
        self.id.as_ref()
    }
}

impl Read for Stream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match unsafe { sys::srt_recv(self.socket.raw(), buf.as_mut_ptr() as *mut sys::char, buf.len() as _) } {
            len if len >= 0 => Ok(len as usize),
            _ => Err(new_io_error("srt_recv")),
        }
    }
}

impl Write for Stream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        match unsafe { sys::srt_send(self.socket.raw(), buf.as_ptr() as *const sys::char, buf.len() as _) } {
            len if len >= 0 => Ok(len as usize),
            _ => Err(new_io_error("srt_send")),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::thread;

    #[test]
    fn test_client_server() {
        let server_thread = thread::spawn(|| {
            let listener = Listener::bind("127.0.0.1:1234").unwrap();
            let (mut conn, _) = listener.accept().unwrap();
            let mut buf = [0; 1316];
            assert_eq!(conn.read(&mut buf).unwrap(), 3);
            assert_eq!(&buf[0..3], b"foo");
        });

        let mut conn = Stream::connect("127.0.0.1:1234", &ConnectOptions::default()).unwrap();
        assert_eq!(conn.write(b"foo").unwrap(), 3);
        assert_eq!(conn.id(), None);

        server_thread.join().unwrap();
    }

    #[test]
    fn test_passphrase() {
        let server_thread = thread::spawn(|| {
            let listener = Listener::bind_with_options("127.0.0.1:1236", [ListenerOption::TooLatePacketDrop(false)].iter().cloned())
                .unwrap()
                .with_callback(|stream_id: Option<&_>| {
                    assert_eq!(stream_id, Some("mystreamid"));
                    ListenerCallbackAction::Allow {
                        passphrase: Some("thepassphrase".to_string()),
                    }
                })
                .unwrap();
            let (mut conn, _) = listener.accept().unwrap();
            let mut buf = [0; 1316];
            assert_eq!(conn.read(&mut buf).unwrap(), 3);
            assert_eq!(&buf[0..3], b"foo");
        });

        let mut options = ConnectOptions::default();
        options.stream_id = Some("mystreamid".to_string());

        options.passphrase = Some("notthepassphrase".to_string());
        assert_eq!(Stream::connect("127.0.0.1:1236", &options).is_err(), true);

        options.passphrase = Some("thepassphrase".to_string());
        let mut conn = Stream::connect("127.0.0.1:1236", &options).unwrap();
        assert_eq!(conn.write(b"foo").unwrap(), 3);
        assert_eq!(conn.id(), options.stream_id.as_ref());

        server_thread.join().unwrap();
    }
}
