use super::{new_srt_error, sys, Result, Socket};
use libc::c_int as int;
use std::{
    collections::HashMap,
    io::Write,
    net,
    os::unix::{io::IntoRawFd, net::UnixStream},
    sync::{Arc, Mutex},
    task::Waker,
    thread,
};

#[derive(Default)]
struct Wakers {
    read_waker: Option<Waker>,
    write_waker: Option<Waker>,
}

pub(crate) struct EpollReactor {
    eid: int,
    join_handle: Option<thread::JoinHandle<()>>,
    pipe: UnixStream,
    wakers: Arc<Mutex<HashMap<sys::SRTSOCKET, Wakers>>>,
}

const READ_EVENTS: int = sys::SRT_EPOLL_OPT::SRT_EPOLL_ERR as int | sys::SRT_EPOLL_OPT::SRT_EPOLL_IN as int;
const WRITE_EVENTS: int = sys::SRT_EPOLL_OPT::SRT_EPOLL_ERR as int | sys::SRT_EPOLL_OPT::SRT_EPOLL_OUT as int;
const READ_WRITE_EVENTS: int = READ_EVENTS | WRITE_EVENTS;

impl EpollReactor {
    pub fn new() -> Result<Self> {
        let eid = match unsafe { sys::srt_epoll_create() } {
            -1 => return Err(new_srt_error("srt_epoll_create")),
            id => id,
        };
        let (pipe_a, pipe_b) = UnixStream::pair()?;
        let wakers = Arc::new(Mutex::new(HashMap::new()));
        Ok(Self {
            eid,
            pipe: pipe_a,
            wakers: wakers.clone(),
            join_handle: Some(thread::spawn(move || Self::run(eid, wakers, pipe_b))),
        })
    }

    pub fn wake_when_read_ready(&self, s: &Socket, waker: Waker) {
        let s = s.raw();
        let mut wakers = self.wakers.lock().expect("the lock should not be poisoned");
        match wakers.get_mut(&s) {
            Some(prev) => {
                let events = if prev.write_waker.is_some() { READ_WRITE_EVENTS } else { READ_EVENTS };
                if prev.read_waker.is_none() {
                    unsafe { sys::srt_epoll_update_usock(self.eid, s, &events as _) };
                }
                prev.read_waker = Some(waker);
            }
            None => {
                wakers.insert(
                    s,
                    Wakers {
                        read_waker: Some(waker),
                        ..Default::default()
                    },
                );
                unsafe { sys::srt_epoll_add_usock(self.eid, s, &READ_EVENTS) };
            }
        }
    }

    pub fn wake_when_write_ready(&self, s: &Socket, waker: Waker) {
        let s = s.raw();
        let mut wakers = self.wakers.lock().expect("the lock should not be poisoned");
        match wakers.get_mut(&s) {
            Some(prev) => {
                let events = if prev.read_waker.is_some() { READ_WRITE_EVENTS } else { WRITE_EVENTS };
                if prev.write_waker.is_none() {
                    unsafe { sys::srt_epoll_update_usock(self.eid, s, &events as _) };
                }
                prev.write_waker = Some(waker);
            }
            None => {
                wakers.insert(
                    s,
                    Wakers {
                        write_waker: Some(waker),
                        ..Default::default()
                    },
                );
                unsafe { sys::srt_epoll_add_usock(self.eid, s, &WRITE_EVENTS) };
            }
        }
    }

    pub fn remove_socket(&self, s: &Socket) {
        let s = s.raw();
        let mut wakers = self.wakers.lock().expect("the lock should not be poisoned");
        if wakers.remove(&s).is_some() {
            unsafe { sys::srt_epoll_remove_usock(self.eid, s) };
        }
    }

    fn run(eid: int, wakers: Arc<Mutex<HashMap<sys::SRTSOCKET, Wakers>>>, pipe: UnixStream) {
        unsafe { sys::srt_epoll_add_ssock(eid, pipe.into_raw_fd(), &READ_EVENTS) };

        let mut readfds = [0; 10];
        let mut writefds = [0; 10];
        let mut sys_readfds = [0; 1];
        let mut sys_writefds = [0; 1];
        loop {
            let mut rnum = readfds.len() as int;
            let mut wnum = writefds.len() as int;
            let mut lrnum = sys_readfds.len() as int;
            let mut lwnum = sys_writefds.len() as int;
            unsafe {
                sys::srt_epoll_wait(
                    eid,
                    readfds.as_mut_ptr(),
                    &mut rnum as _,
                    writefds.as_mut_ptr(),
                    &mut wnum as _,
                    -1,
                    sys_readfds.as_mut_ptr(),
                    &mut lrnum as _,
                    sys_writefds.as_mut_ptr(),
                    &mut lwnum as _,
                )
            };

            if lrnum > 0 {
                return;
            }

            if rnum > 0 || wnum > 0 {
                let mut wakers = wakers.lock().expect("the lock should not be poisoned");
                for &fd in &readfds[..readfds.len().min(rnum as _)] {
                    match wakers.get_mut(&fd) {
                        Some(fd_wakers) => {
                            if let Some(waker) = fd_wakers.read_waker.take() {
                                waker.wake();
                            }
                            if fd_wakers.write_waker.is_some() {
                                unsafe { sys::srt_epoll_update_usock(eid, fd, &WRITE_EVENTS) };
                            } else {
                                wakers.remove(&fd);
                                unsafe { sys::srt_epoll_remove_usock(eid, fd) };
                            }
                        }
                        None => unsafe {
                            sys::srt_epoll_remove_usock(eid, fd);
                        },
                    }
                }
                for &fd in &writefds[..writefds.len().min(wnum as _)] {
                    match wakers.get_mut(&fd) {
                        Some(fd_wakers) => {
                            if let Some(waker) = fd_wakers.write_waker.take() {
                                waker.wake();
                            }
                            if fd_wakers.read_waker.is_some() {
                                unsafe { sys::srt_epoll_update_usock(eid, fd, &READ_EVENTS) };
                            } else {
                                wakers.remove(&fd);
                                unsafe { sys::srt_epoll_remove_usock(eid, fd) };
                            }
                        }
                        None => unsafe {
                            sys::srt_epoll_remove_usock(eid, fd);
                        },
                    }
                }
            }
        }
    }
}

impl Drop for EpollReactor {
    fn drop(&mut self) {
        self.pipe.write(b"x").expect("we should be able to write to the epoll thread pipe");
        self.join_handle
            .take()
            .expect("there should be a join handle")
            .join()
            .expect("we should be able to join the epoll reactor thread");
        self.pipe.shutdown(net::Shutdown::Both).expect("we should be able to shut down the pipe");
        unsafe { sys::srt_epoll_release(self.eid) };
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_epoll_reactor() {
        let reactor = EpollReactor::new();
        std::mem::drop(reactor);
    }
}
