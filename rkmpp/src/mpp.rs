use rkmpp_sys as sys;
use snafu::Snafu;
use std::{
    ffi::{c_void, CStr, CString},
    marker::PhantomData,
    mem::MaybeUninit,
    ops::Deref,
    ptr,
    rc::Rc,
    sync::Arc,
};

#[derive(Debug, Snafu)]
#[snafu(display("error code: {code} (/var/log/syslog may contain more information)"))]
pub struct Error {
    code: sys::MPP_RET,
}

type Result<T> = core::result::Result<T, Error>;

#[derive(Clone)]
pub struct Lib {
    pub sys: Arc<sys::rockchip_mpp>,
}

impl Lib {
    /// Attempts to load the library. Returns `None` if the library could not be loaded.
    pub fn new() -> Option<Self> {
        let sys = Arc::new(unsafe { sys::rockchip_mpp::new("librockchip_mpp.so").ok()? });
        Some(Self { sys })
    }
}

#[derive(Clone)]
struct BufferGroupInner {
    lib: Lib,
    group: sys::MppBufferGroup,
}

impl Drop for BufferGroupInner {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_buffer_group_put(self.group);
        }
    }
}

pub struct BufferGroup {
    inner: Rc<BufferGroupInner>,
}

const MODULE_TAG: &CStr = c"rkmpp_rs";
const MPP_BUFFER_FLAGS_CACHABLE: u32 = 0x00020000;

impl Lib {
    pub fn new_buffer_group(&self) -> Result<BufferGroup> {
        let mut group = MaybeUninit::uninit();
        unsafe {
            match self.sys.mpp_buffer_group_get(
                group.as_mut_ptr(),
                sys::MppBufferType_MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE,
                sys::MppBufferMode_MPP_BUFFER_INTERNAL,
                MODULE_TAG.as_ptr(),
                c"new_buffer_group".as_ptr(),
            ) {
                sys::MPP_RET_MPP_OK => Ok(BufferGroup {
                    inner: Rc::new(BufferGroupInner {
                        lib: self.clone(),
                        group: group.assume_init(),
                    }),
                }),
                code => Err(Error { code }),
            }
        }
    }
}

impl BufferGroup {
    pub fn get_buffer(&mut self, size: usize) -> Result<Buffer> {
        let mut buf = ptr::null_mut();
        unsafe {
            match self
                .inner
                .lib
                .sys
                .mpp_buffer_get_with_tag(self.inner.group, &mut buf, size, MODULE_TAG.as_ptr(), c"get_buffer".as_ptr())
            {
                sys::MPP_RET_MPP_OK => Ok(Buffer {
                    lib: self.inner.lib.clone(),
                    _group: self.inner.clone(),
                    buf,
                }),
                code => Err(Error { code }),
            }
        }
    }
}

pub struct Buffer {
    lib: Lib,
    _group: Rc<BufferGroupInner>,
    buf: sys::MppBuffer,
}

pub struct BufferSync<'a> {
    buf: &'a mut Buffer,
}

impl<'a> BufferSync<'a> {
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe {
            let ptr = self
                .buf
                .lib
                .sys
                .mpp_buffer_get_ptr_with_caller(self.buf.buf, c"buffer_sync_as_mut_slice".as_ptr());
            let size = self
                .buf
                .lib
                .sys
                .mpp_buffer_get_size_with_caller(self.buf.buf, c"buffer_sync_as_mut_slice".as_ptr());
            core::slice::from_raw_parts_mut(ptr as *mut u8, size)
        }
    }
}

impl<'a> Drop for BufferSync<'a> {
    fn drop(&mut self) {
        unsafe {
            self.buf.lib.sys.mpp_buffer_sync_end_f(self.buf.buf, 0, c"buffer_sync_drop".as_ptr());
        }
    }
}

impl Buffer {
    pub fn sync(&mut self) -> BufferSync {
        unsafe {
            self.lib.sys.mpp_buffer_sync_begin_f(self.buf, 0, c"buffer_sync".as_ptr());
        }
        BufferSync { buf: self }
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_buffer_put_with_caller(self.buf, c"buffer_drop".as_ptr());
        }
    }
}

pub struct Frame {
    lib: Lib,
    frame: sys::MppFrame,
}

impl Lib {
    pub fn new_frame(&self) -> Result<Frame> {
        let mut frame = MaybeUninit::uninit();
        unsafe {
            match self.sys.mpp_frame_init(frame.as_mut_ptr()) {
                sys::MPP_RET_MPP_OK => Ok(Frame {
                    lib: self.clone(),
                    frame: frame.assume_init(),
                }),
                code => Err(Error { code }),
            }
        }
    }
}

impl Frame {
    pub fn set_buffer(&mut self, buffer: &Buffer) {
        unsafe { self.lib.sys.mpp_frame_set_buffer(self.frame, buffer.buf) }
    }
}

impl Deref for Frame {
    type Target = sys::MppFrame;

    fn deref(&self) -> &Self::Target {
        &self.frame
    }
}

impl Drop for Frame {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_frame_deinit(&mut self.frame);
        }
    }
}

pub struct Context {
    lib: Lib,
    ctx: sys::MppCtx,
    api: *mut sys::MppApi,
}

impl Lib {
    pub fn new_context(&self, coding: sys::MppCodingType) -> Result<Context> {
        let mut ctx = MaybeUninit::uninit();
        let mut api = ptr::null_mut();
        unsafe {
            match self.sys.mpp_create(ctx.as_mut_ptr(), &mut api) {
                sys::MPP_RET_MPP_OK => {}
                code => return Err(Error { code }),
            }
            let ctx = ctx.assume_init();
            match self.sys.mpp_init(ctx, sys::MppCtxType_MPP_CTX_ENC, coding) {
                sys::MPP_RET_MPP_OK => {}
                code => return Err(Error { code }),
            }
            Ok(Context { lib: self.clone(), ctx, api })
        }
    }
}

impl Context {
    pub fn get_config(&self, cfg: &mut Config) -> Result<()> {
        unsafe { self.control(sys::MpiCmd_MPP_ENC_GET_CFG, cfg.cfg) }
    }

    pub fn set_config(&mut self, cfg: &Config) -> Result<()> {
        unsafe { self.control(sys::MpiCmd_MPP_ENC_SET_CFG, cfg.cfg) }
    }

    pub fn force_keyframe(&mut self) -> Result<()> {
        unsafe { self.control(sys::MpiCmd_MPP_ENC_SET_IDR_FRAME, ptr::null_mut()) }
    }

    pub fn get_encoder_header_sync_packet(&mut self, packet: &mut Packet) -> Result<()> {
        unsafe { self.control(sys::MpiCmd_MPP_ENC_GET_HDR_SYNC, packet.packet) }
    }

    unsafe fn control(&self, cmd: sys::MpiCmd, v: *mut c_void) -> Result<()> {
        unsafe {
            match (*self.api).control.unwrap()(self.ctx, cmd, v) {
                sys::MPP_RET_MPP_OK => Ok(()),
                code => Err(Error { code }),
            }
        }
    }

    pub fn encode_put_frame(&mut self, frame: &Frame) -> Result<()> {
        unsafe {
            match (*self.api).encode_put_frame.unwrap()(self.ctx, frame.frame) {
                sys::MPP_RET_MPP_OK => Ok(()),
                code => Err(Error { code }),
            }
        }
    }

    pub fn encode_get_packet(&mut self) -> Result<Option<Packet>> {
        let mut packet = ptr::null_mut();
        unsafe {
            match (*self.api).encode_get_packet.unwrap()(self.ctx, &mut packet) {
                sys::MPP_RET_MPP_OK => Ok(if packet.is_null() {
                    None
                } else {
                    Some(Packet { lib: self.lib.clone(), packet })
                }),
                code => Err(Error { code }),
            }
        }
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_destroy(self.ctx);
        }
    }
}

pub struct Meta<'a> {
    lib: Lib,
    meta: sys::MppMeta,
    _phantom: PhantomData<&'a u8>,
}

impl<'a> Meta<'a> {
    pub fn get_s32(&self, key: sys::MppMetaKey_e) -> Result<i32> {
        let mut value = 0;
        unsafe {
            match self.lib.sys.mpp_meta_get_s32(self.meta, key, &mut value) {
                sys::MPP_RET_MPP_OK => Ok(value),
                code => Err(Error { code }),
            }
        }
    }
}

pub struct Packet {
    lib: Lib,
    packet: sys::MppPacket,
}

impl Packet {
    pub fn with_buffer(buffer: &mut Buffer) -> Result<Packet> {
        let mut packet = ptr::null_mut();
        unsafe {
            match buffer.lib.sys.mpp_packet_init_with_buffer(&mut packet, buffer.buf) {
                sys::MPP_RET_MPP_OK => Ok(Packet {
                    lib: buffer.lib.clone(),
                    packet,
                }),
                code => Err(Error { code }),
            }
        }
    }

    pub fn meta(&self) -> Meta {
        Meta {
            lib: self.lib.clone(),
            meta: unsafe { self.lib.sys.mpp_packet_get_meta(self.packet) },
            _phantom: PhantomData,
        }
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe {
            let ptr = self.lib.sys.mpp_packet_get_pos(self.packet);
            let size = self.lib.sys.mpp_packet_get_length(self.packet);
            core::slice::from_raw_parts(ptr as *const u8, size)
        }
    }
}

impl Drop for Packet {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_packet_deinit(&mut self.packet);
        }
    }
}

pub struct Config {
    lib: Lib,
    cfg: sys::MppEncCfg,
}

impl Lib {
    pub fn new_config(&self) -> Result<Config> {
        let mut cfg = MaybeUninit::uninit();
        unsafe {
            match self.sys.mpp_enc_cfg_init(cfg.as_mut_ptr()) {
                sys::MPP_RET_MPP_OK => Ok(Config {
                    lib: self.clone(),
                    cfg: cfg.assume_init(),
                }),
                code => Err(Error { code }),
            }
        }
    }
}

impl Config {
    pub fn set_s32(&mut self, name: &str, value: i32) -> Result<()> {
        let name = CString::new(name).unwrap();
        unsafe {
            match self.lib.sys.mpp_enc_cfg_set_s32(self.cfg, name.as_ptr(), value) {
                sys::MPP_RET_MPP_OK => Ok(()),
                code => Err(Error { code }),
            }
        }
    }
}

impl Drop for Config {
    fn drop(&mut self) {
        unsafe {
            self.lib.sys.mpp_enc_cfg_deinit(self.cfg);
        }
    }
}
