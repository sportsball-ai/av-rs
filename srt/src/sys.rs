pub use libc::{c_char as char, c_int as int, c_void as void, size_t, sockaddr, sockaddr_storage, socklen_t};
use std::sync::atomic::AtomicPtr;

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

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct SRT_MSGCTRL {
    pub flags: int,
    pub msgttl: int,
    pub inorder: int,
    pub boundary: int,
    pub srctime: i64,
    pub pktseq: i32,
    pub msgno: i32,
    pub grpdata: AtomicPtr<void>,
    pub grpdata_size: size_t,
}

static SRT_MSGTTL_INF: i32 = -1;
static PB_SUBSEQUENT: i32 = 0;
static SRT_SEQNO_NONE: i32 = -1;
static SRT_MSGNO_NONE: i32 = -1;

impl Default for SRT_MSGCTRL {
    fn default() -> Self {
        Self {
            flags: 0,
            msgttl: SRT_MSGTTL_INF,
            inorder: 0,
            boundary: PB_SUBSEQUENT,
            srctime: 0,
            pktseq: SRT_SEQNO_NONE,
            msgno: SRT_MSGNO_NONE,
            grpdata: AtomicPtr::new(std::ptr::null_mut()),
            grpdata_size: 0,
        }
    }
}

#[cfg(target_os = "macos")]
#[link(name = "crypto", kind = "static")]
extern "C" {}

#[link(name = "srt", kind = "static")]
extern "C" {
    pub fn srt_startup() -> int;
    pub fn srt_cleanup() -> int;

    pub fn srt_create_socket() -> SRTSOCKET;
    pub fn srt_close(u: SRTSOCKET) -> int;

    pub fn srt_bind(u: SRTSOCKET, name: *const sockaddr, namelen: int) -> int;
    pub fn srt_listen(u: SRTSOCKET, backlog: int) -> int;
    pub fn srt_accept(u: SRTSOCKET, addr: *mut sockaddr, addrlen: *mut int) -> SRTSOCKET;
    pub fn srt_connect(u: SRTSOCKET, name: *const sockaddr, namelen: int) -> int;

    pub fn srt_recvmsg2(u: SRTSOCKET, buf: *mut char, len: int, mc: *mut SRT_MSGCTRL) -> int;
    pub fn srt_send(u: SRTSOCKET, buf: *const char, len: int) -> int;

    pub fn srt_setsockopt(u: SRTSOCKET, level: int, optname: SRT_SOCKOPT, optval: *const void, optlen: int) -> int;
    pub fn srt_getsockopt(u: SRTSOCKET, level: int, optname: SRT_SOCKOPT, optval: *mut void, optlen: *mut int) -> int;

    pub fn srt_listen_callback(
        lsn: SRTSOCKET,
        hook_fn: extern "C" fn(*mut void, SRTSOCKET, int, *const sockaddr, *const char) -> int,
        hook_opaque: *mut void,
    ) -> int;
}
