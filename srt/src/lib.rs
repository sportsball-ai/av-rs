#[macro_use]
extern crate lazy_static;

pub use srt_sys as sys;
use std::{
    ffi::CStr,
    fmt,
    io::{self, Read, Write},
    mem,
    net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6, ToSocketAddrs},
    pin::Pin,
    str,
    sync::{atomic::AtomicUsize, Arc, Mutex},
};

#[cfg(feature = "async")]
mod async_lib;
#[cfg(feature = "async")]
mod epoll_reactor;
#[cfg(feature = "async")]
pub use async_lib::*;

const DEFAULT_SEND_PAYLOAD_SIZE: usize = 1316;

#[derive(Debug)]
pub enum Error {
    InvalidAddress,
    UnsupportedFamily(sys::int),
    IOError(io::Error),
    Utf8Error(str::Utf8Error),
    SRTError {
        fn_name: &'static str,
        code: sys::int,
        errno: sys::int,
    },
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
            Self::SRTError { fn_name, code, errno } => write!(f, "{} error: {} (errno = {})", fn_name, code, errno),
            #[cfg(feature = "async")]
            Self::JoinError(e) => write!(f, "join error: {}", e),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

pub(crate) fn new_io_error(fn_name: &'static str) -> io::Error {
    let mut errno = 0;
    let code = unsafe { sys::srt_getlasterror(&mut errno as _) };
    io::Error::new(io::ErrorKind::Other, Error::SRTError { fn_name, code, errno })
}

pub(crate) fn new_srt_error(fn_name: &'static str) -> Error {
    let mut errno = 0;
    let code = unsafe { sys::srt_getlasterror(&mut errno as _) };
    Error::SRTError { fn_name, code, errno }
}

fn check_code(fn_name: &'static str, code: sys::int) -> Result<()> {
    match code {
        0 => Ok(()),
        _ => Err(new_srt_error(fn_name)),
    }
}

#[derive(Default)]
struct Api {
    #[cfg(feature = "async")]
    state: Arc<ApiState>,
}

#[derive(Default)]
struct ApiState {
    #[cfg(feature = "async")]
    epoll_reactor: Mutex<Option<Arc<epoll_reactor::EpollReactor>>>,
    ref_count: AtomicUsize,
}

lazy_static! {
    static ref GLOBAL_API_STATE: Mutex<Arc<ApiState>> = Mutex::new(Arc::new(ApiState::default()));
}

impl Api {
    #[cfg(feature = "async")]
    fn get_epoll_reactor(&self) -> Result<Arc<epoll_reactor::EpollReactor>> {
        let mut api_reactor = self.state.epoll_reactor.lock().expect("the lock should not be poisoned");
        match &*api_reactor {
            Some(api_reactor) => Ok(api_reactor.clone()),
            None => {
                let r = Arc::new(epoll_reactor::EpollReactor::new()?);
                *api_reactor = Some(r.clone());
                Ok(r)
            }
        }
    }

    fn get() -> Result<Arc<Api>> {
        let api_state = GLOBAL_API_STATE.lock().unwrap();
        if api_state.ref_count.fetch_add(1, std::sync::atomic::Ordering::SeqCst) == 0 {
            unsafe {
                check_code("srt_startup", sys::srt_startup())?;
            }
        }
        Ok(Arc::new(Api {
            #[cfg(feature = "async")]
            state: api_state.clone(),
        }))
    }
}

impl Drop for Api {
    fn drop(&mut self) {
        let api_state = GLOBAL_API_STATE.lock().unwrap();
        if api_state.ref_count.fetch_sub(1, std::sync::atomic::Ordering::SeqCst) == 1 {
            #[cfg(feature = "async")]
            if let Some(reactor) = api_state.epoll_reactor.lock().expect("the lock should not be poisoned").take() {
                if Arc::try_unwrap(reactor).is_err() {
                    panic!("the api must have the last strong reference to the reactor");
                }
            }

            unsafe {
                sys::srt_cleanup();
            }
        }
    }
}

struct Socket {
    pub(crate) api: Arc<Api>,
    sock: sys::SRTSOCKET,
}

trait ToOption {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()>;
}

impl ToOption for &String {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code("srt_setsockopt", unsafe {
            sys::srt_setsockopt(sock, 0, opt, self.as_ptr() as *const _, self.len() as _)
        })
    }
}

impl ToOption for bool {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code("srt_setsockopt", unsafe {
            sys::srt_setsockopt(sock, 0, opt, self as *const bool as *const _, std::mem::size_of::<bool>() as _)
        })
    }
}

impl ToOption for i32 {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code("srt_setsockopt", unsafe {
            sys::srt_setsockopt(sock, 0, opt, self as *const i32 as *const _, std::mem::size_of::<i32>() as _)
        })
    }
}

impl ToOption for i64 {
    fn set(&self, sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<()> {
        check_code("srt_setsockopt", unsafe {
            sys::srt_setsockopt(sock, 0, opt, self as *const i64 as *const _, std::mem::size_of::<i64>() as _)
        })
    }
}

trait FromOption: Sized {
    fn get(sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<Self>;
}

impl FromOption for Option<String> {
    fn get(sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<Self> {
        let mut buf = [0u8; 512];
        let mut len = buf.len();
        check_code("srt_getsockopt", unsafe {
            sys::srt_getsockopt(sock, 0, opt, buf.as_mut_ptr() as *mut _, &mut len as *mut _ as *mut _)
        })?;
        Ok(match len {
            0 => None,
            len => Some(str::from_utf8(&buf[..len])?.to_string()),
        })
    }
}

impl FromOption for i32 {
    fn get(sock: sys::SRTSOCKET, opt: sys::SRT_SOCKOPT) -> Result<Self> {
        let mut n = 0i32;
        let mut n_len = std::mem::size_of::<i32>() as sys::int;
        check_code("srt_getsockopt", unsafe {
            sys::srt_getsockopt(sock, 0, opt, &mut n as *mut i32 as _, &mut n_len as *mut _)
        })?;
        Ok(n)
    }
}

impl Socket {
    fn new() -> Result<Socket> {
        Ok(Socket {
            api: Api::get()?,
            sock: unsafe { sys::srt_create_socket() },
        })
    }

    fn raw(&self) -> sys::SRTSOCKET {
        self.sock
    }

    fn raw_stats(&mut self, clear: bool, instantaneous: bool) -> Result<sys::SRT_TRACEBSTATS> {
        unsafe {
            let mut perf: sys::SRT_TRACEBSTATS = mem::zeroed();
            check_code(
                "srt_bistats",
                sys::srt_bistats(self.raw(), &mut perf, if clear { 1 } else { 0 }, if instantaneous { 1 } else { 0 }),
            )?;
            Ok(perf)
        }
    }

    fn set_connect_options(&self, options: &ConnectOptions) -> Result<()> {
        if let Some(v) = &options.passphrase {
            self.set(sys::SRT_SOCKOPT_SRTO_PASSPHRASE, v)?;
        }
        if let Some(v) = &options.stream_id {
            self.set(sys::SRT_SOCKOPT_SRTO_STREAMID, v)?;
        }
        if let Some(v) = &options.too_late_packet_drop {
            self.set(sys::SRT_SOCKOPT_SRTO_TLPKTDROP, *v)?;
        }
        if let Some(v) = &options.timestamp_based_packet_delivery_mode {
            self.set(sys::SRT_SOCKOPT_SRTO_TSBPDMODE, *v)?;
        }
        if let Some(v) = &options.receive_buffer_size {
            self.set(sys::SRT_SOCKOPT_SRTO_RCVBUF, *v)?;
        }
        if let Some(v) = &options.send_buffer_size {
            self.set(sys::SRT_SOCKOPT_SRTO_SNDBUF, *v)?;
        }
        if let Some(v) = &options.max_bandwidth {
            match v {
                MaxBandwidth::Infinite => self.set(sys::SRT_SOCKOPT_SRTO_MAXBW, -1i64)?,
                MaxBandwidth::Relative {
                    input_bandwidth,
                    overhead_bandwidth_percentage,
                } => {
                    self.set(sys::SRT_SOCKOPT_SRTO_MAXBW, 0i64)?;
                    self.set(sys::SRT_SOCKOPT_SRTO_INPUTBW, *input_bandwidth as i64)?;
                    self.set(sys::SRT_SOCKOPT_SRTO_OHEADBW, *overhead_bandwidth_percentage as i32)?;
                }
                MaxBandwidth::Absolute(n) => self.set(sys::SRT_SOCKOPT_SRTO_MAXBW, *n as i64)?,
            }
        }
        if let Some(v) = &options.max_send_payload_size {
            self.set(sys::SRT_SOCKOPT_SRTO_PAYLOADSIZE, *v)?;
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

fn to_sockaddr(addr: &SocketAddr) -> (sys::sockaddr_storage, sys::socklen_t) {
    use libc::{sockaddr_in, sockaddr_in6, AF_INET, AF_INET6};
    let mut storage: sys::sockaddr_storage = unsafe { mem::zeroed() };
    let socklen = match addr {
        SocketAddr::V4(ref a) => {
            let mut storage = unsafe { &mut *(&mut storage as *mut _ as *mut sockaddr_in) };
            storage.sin_family = AF_INET as _;
            storage.sin_port = u16::to_be(a.port());
            storage.sin_addr.s_addr = u32::from_ne_bytes(a.ip().octets());
            mem::size_of::<sockaddr_in>()
        }
        SocketAddr::V6(ref a) => {
            let mut storage = unsafe { &mut *(&mut storage as *mut _ as *mut sockaddr_in6) };
            storage.sin6_family = AF_INET6 as _;
            storage.sin6_port = u16::to_be(a.port());
            storage.sin6_addr.s6_addr.copy_from_slice(&a.ip().octets());
            mem::size_of::<sockaddr_in6>()
        }
    };
    (storage, socklen as _)
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

    // we pass a pointer to `Box<dyn ListenerCallback + 'c>` to C land, and the double boxing is
    // necessary here to keep that pointer valid
    #[allow(clippy::redundant_allocation)]
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
                    if v.set(ns, sys::SRT_SOCKOPT_SRTO_PASSPHRASE).is_err() {
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
    SendBufferSize(i32),
}

impl ListenerOption {
    pub(crate) fn set(&self, sock: &Socket) -> Result<()> {
        match self {
            ListenerOption::TimestampBasedPacketDeliveryMode(v) => sock.set(sys::SRT_SOCKOPT_SRTO_TSBPDMODE, *v),
            ListenerOption::TooLatePacketDrop(v) => sock.set(sys::SRT_SOCKOPT_SRTO_TLPKTDROP, *v),
            ListenerOption::ReceiveBufferSize(v) => sock.set(sys::SRT_SOCKOPT_SRTO_RCVBUF, *v),
            ListenerOption::SendBufferSize(v) => sock.set(sys::SRT_SOCKOPT_SRTO_SNDBUF, *v),
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
                check_code("srt_bind", sys::srt_bind(socket.raw(), &addr as *const _ as _, len as _))?;
            }
        }
        unsafe {
            check_code("srt_listen", sys::srt_listen(socket.raw(), 10))?;
        }
        Ok(Self { socket, _callback: None })
    }
}

impl<'c> Listener<'c> {
    pub fn with_callback<F: ListenerCallback + 'c>(self, f: F) -> Result<Listener<'c>> {
        let mut cb: Box<Box<dyn ListenerCallback>> = Box::new(Box::new(f));
        let ptr = &mut *cb as *mut Box<dyn ListenerCallback>;
        let pb = unsafe { Pin::new_unchecked(cb) };
        check_code("srt_listen_callback", unsafe {
            sys::srt_listen_callback(self.socket.raw(), Some(listener_callback), ptr as *mut _)
        })?;
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
        let max_send_payload_size = socket.get::<i32>(sys::SRT_SOCKOPT_SRTO_PAYLOADSIZE).unwrap_or(DEFAULT_SEND_PAYLOAD_SIZE as _) as _;
        Ok((
            Stream {
                id: socket.get(sys::SRT_SOCKOPT_SRTO_STREAMID)?,
                socket,
                max_send_payload_size,
            },
            addr,
        ))
    }
}

pub struct Stream {
    socket: Socket,
    id: Option<String>,
    max_send_payload_size: usize,
}

#[derive(Clone, Debug)]
pub enum MaxBandwidth {
    Infinite,
    Relative { input_bandwidth: u64, overhead_bandwidth_percentage: u32 },
    Absolute(u64),
}

#[derive(Clone, Debug, Default)]
pub struct ConnectOptions {
    pub passphrase: Option<String>,
    pub stream_id: Option<String>,
    pub timestamp_based_packet_delivery_mode: Option<bool>,
    pub too_late_packet_drop: Option<bool>,
    pub receive_buffer_size: Option<i32>,
    pub send_buffer_size: Option<i32>,
    pub max_bandwidth: Option<MaxBandwidth>,
    pub max_send_payload_size: Option<i32>,
}

impl Stream {
    pub fn connect<A: ToSocketAddrs>(addr: A, options: &ConnectOptions) -> Result<Self> {
        let mut last_err = Error::InvalidAddress;
        for addr in addr.to_socket_addrs()? {
            let (addr, len) = to_sockaddr(&addr);
            let socket = Socket::new()?;
            socket.set_connect_options(options)?;
            let max_send_payload_size = options.max_send_payload_size.unwrap_or(DEFAULT_SEND_PAYLOAD_SIZE as _) as _;
            unsafe {
                match check_code("srt_connect", sys::srt_connect(socket.raw(), &addr as *const _ as _, len as _)) {
                    Err(e) => last_err = e,
                    Ok(_) => {
                        return Ok(Self {
                            socket,
                            id: options.stream_id.clone(),
                            max_send_payload_size,
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

    /// Returns the underlying stats for the socket.
    ///
    /// Refer to https://github.com/Haivision/srt/blob/v1.4.4/docs/API/statistics.md to learn more about them.
    pub fn raw_stats(&mut self, clear: bool, instantaneous: bool) -> Result<sys::SRT_TRACEBSTATS> {
        self.socket.raw_stats(clear, instantaneous)
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
        let data_size = self.max_send_payload_size.min(buf.len());
        match unsafe { sys::srt_send(self.socket.raw(), buf.as_ptr() as *const sys::char, data_size as _) } {
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

            assert!(conn.raw_stats(false, false).unwrap().pktRecvTotal > 0);
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

        let mut options = ConnectOptions {
            stream_id: Some("mystreamid".to_string()),
            passphrase: Some("notthepassphrase".to_string()),
            max_send_payload_size: Some(1400),
            ..Default::default()
        };

        assert!(Stream::connect("127.0.0.1:1236", &options).is_err());

        options.passphrase = Some("thepassphrase".to_string());
        let mut conn = Stream::connect("127.0.0.1:1236", &options).unwrap();
        assert_eq!(conn.write(b"foo").unwrap(), 3);
        let buf = [0; 2000];
        assert_eq!(conn.write(&buf[..]).unwrap(), 1400);
        assert_eq!(conn.id(), options.stream_id.as_ref());

        server_thread.join().unwrap();
    }

    #[test]
    fn test_to_sockaddr() {
        let addr: SocketAddr = "127.0.0.1:8080".parse().unwrap();
        let (sockaddr, socklen) = to_sockaddr(&addr);
        let round_tripped = sockaddr_from_storage(&sockaddr, socklen).unwrap();
        assert_eq!(addr, round_tripped);
    }
}
