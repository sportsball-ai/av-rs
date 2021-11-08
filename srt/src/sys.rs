// our naming mimicks the srt library
#![allow(clippy::upper_case_acronyms)]

pub use libc::{c_char as char, c_int as int, c_void as void, sockaddr, sockaddr_storage, socklen_t};

pub type SRTSOCKET = int;

#[cfg(feature = "async")]
#[repr(C)]
#[allow(non_camel_case_types)]
pub enum SRT_EPOLL_OPT {
    SRT_EPOLL_IN = 1,
    SRT_EPOLL_OUT = 4,
    SRT_EPOLL_ERR = 8,
}

#[cfg(feature = "async")]
pub type SYSSOCKET = int;

#[repr(C)]
#[allow(non_camel_case_types)]
pub enum SRT_SOCKOPT {
    #[cfg(feature = "async")]
    SNDSYN = 1,
    #[cfg(feature = "async")]
    RCVSYN = 2,
    RCVBUF = 6,
    #[cfg(feature = "async")]
    EVENT = 18,
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

#[cfg(feature = "async")]
#[link(name = "srt", kind = "static")]
extern "C" {
    pub fn srt_epoll_create() -> int;
    pub fn srt_epoll_release(eid: int) -> int;
    pub fn srt_epoll_add_ssock(eid: int, s: SYSSOCKET, events: *const int) -> int;
    pub fn srt_epoll_add_usock(eid: int, s: SRTSOCKET, events: *const int) -> int;
    pub fn srt_epoll_update_usock(eid: int, s: SRTSOCKET, events: *const int) -> int;
    pub fn srt_epoll_remove_usock(eid: int, s: SRTSOCKET) -> int;
    pub fn srt_epoll_wait(
        eid: int,
        readfds: *mut SRTSOCKET,
        rnum: *mut int,
        writefds: *mut SRTSOCKET,
        wnum: *mut int,
        ms_timeout: i64,
        lrfds: *mut SYSSOCKET,
        lrnum: *mut int,
        lwfds: *mut SYSSOCKET,
        lwnum: *mut int,
    ) -> int;
}
