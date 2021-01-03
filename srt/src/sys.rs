pub use libc::{c_char as char, c_int as int, c_void as void, sockaddr, sockaddr_storage, socklen_t};

pub type SRTSOCKET = int;

#[repr(C)]
#[allow(non_camel_case_types)]
pub enum SRT_SOCKOPT {
    RCVBUF = 6,
    TSBPDMODE = 22,
    PASSPHRASE = 26,
    TLPKTDROP = 31,
    STREAMID = 46,
}

#[cfg(target_os = "macos")]
#[link(name = "crypto", kind = "static")]
extern "C" {}

#[link(name = "srt", kind = "static")]
extern "C" {
    pub fn srt_startup() -> int;
    pub fn srt_cleanup() -> int;

    pub fn srt_getlasterror(errno_loc: *mut int) -> int;

    pub fn srt_create_socket() -> SRTSOCKET;
    pub fn srt_close(u: SRTSOCKET) -> int;

    pub fn srt_bind(u: SRTSOCKET, name: *const sockaddr, namelen: int) -> int;
    pub fn srt_listen(u: SRTSOCKET, backlog: int) -> int;
    pub fn srt_accept(u: SRTSOCKET, addr: *mut sockaddr, addrlen: *mut int) -> SRTSOCKET;
    pub fn srt_connect(u: SRTSOCKET, name: *const sockaddr, namelen: int) -> int;

    pub fn srt_recv(u: SRTSOCKET, buf: *mut char, len: int) -> int;
    pub fn srt_send(u: SRTSOCKET, buf: *const char, len: int) -> int;

    pub fn srt_setsockopt(u: SRTSOCKET, level: int, optname: SRT_SOCKOPT, optval: *const void, optlen: int) -> int;
    pub fn srt_getsockopt(u: SRTSOCKET, level: int, optname: SRT_SOCKOPT, optval: *mut void, optlen: *mut int) -> int;

    pub fn srt_listen_callback(
        lsn: SRTSOCKET,
        hook_fn: extern "C" fn(*mut void, SRTSOCKET, int, *const sockaddr, *const char) -> int,
        hook_opaque: *mut void,
    ) -> int;
}
