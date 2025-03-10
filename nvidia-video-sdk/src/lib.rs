use std::{
    ffi::{c_char, c_int, c_schar, c_short, c_uchar, c_uint, c_ulong, c_ulonglong, c_ushort, c_void, CStr},
    fmt::{self, Debug},
    future::Future,
    marker::PhantomData,
    mem,
    ops::Deref as _,
    pin::Pin,
    ptr::{self, null_mut},
    slice,
    sync::Arc,
    task::{self, Waker},
};

use tokio::sync::{Mutex, MutexGuard};

#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
#[allow(unused)]
#[allow(clippy::all)]
mod sys {
    include!(concat!(env!("OUT_DIR"), "/video_sdk_bindings.rs"));
}

/// Must be called prior to using any other API in this crate.
pub fn cu_init() -> Result<CudaInitToken, CudaError> {
    from_cuda_error(unsafe { sys::cuInit(0) })?;
    Ok(CudaInitToken { _private: () })
}

/// This structure contains no data and serves exclusively as a claim ticket that CUDA has in fact been initialized.
#[derive(Clone, Copy)]
pub struct CudaInitToken {
    /// This field exists to prevent initialization outside this crate
    _private: (),
}

impl Debug for CudaInitToken {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("CudaInitToken").finish_non_exhaustive()
    }
}
/// Returns the total count of CUDA ready devices. These devices are later referred to by ordinal.
/// i.e. if there are 3 devices, then there are devices 0, 1, and 2.
pub fn cu_device_get_count(_: CudaInitToken) -> Result<c_int, CudaError> {
    let mut count = 0;
    from_cuda_error(unsafe { sys::cuDeviceGetCount(&mut count) })?;
    Ok(count)
}

/// Initializes a device from the given ordinal. Use `cu_device_get_count` to check how many devices are available.
pub fn cu_device_get(_: CudaInitToken, ordinal: c_int) -> Result<CuDevice, CudaError> {
    let mut device = 0;
    from_cuda_error(unsafe { sys::cuDeviceGet(&mut device, ordinal) })?;
    Ok(CuDevice(device))
}

#[derive(Debug, Copy, Clone)]
pub struct CuDevice(c_int);

impl CuDevice {
    pub fn raw(&self) -> c_int {
        self.0
    }
}

pub struct CuContext {
    inner: sys::CUcontext,
}

// This is explicitly dependent on the idea that we manage the current thread's cuCtx stack correctly.
// Push the context before issuing global calls, pop it when done doing so.
unsafe impl Send for CuContext {}
unsafe impl Sync for CuContext {}

impl CuContext {
    pub fn new(device: &CuDevice, flags: CuContextFlags) -> Result<Self, CudaError> {
        let mut inner: sys::CUcontext = ptr::null_mut();
        from_cuda_error(unsafe { sys::cuCtxCreate_v3(&mut inner, null_mut(), 0, flags.to_raw(), device.raw()) })?;
        // Immediately pop this off the current context stack. We intend to use `cuCtxPushCurrent` before every relevant call, so we don't need to leave it on
        // the stack.
        from_cuda_error(unsafe { sys::cuCtxPopCurrent_v2(&mut inner) })?;
        Ok(Self { inner })
    }

    pub fn get_api_version(&self) -> Result<c_uint, CudaError> {
        let mut version = 0;
        from_cuda_error(unsafe { sys::cuCtxGetApiVersion(self.inner, &mut version) })?;
        Ok(version)
    }

    /// Blocks until the device has completed all preceding requested tasks. This returns an error if one of the preceding tasks failed.
    /// If the context was created with [`CuContextFlagSched::BlockingSync`], the CPU thread will block until the GPU context has finished its work.
    pub fn synchronize(&self) -> Result<(), CudaError> {
        self.exec(|| from_cuda_error(unsafe { sys::cuCtxSynchronize() }))
    }

    /// Pushes this context onto the current thread's context stack, performs an operation, and then pops the context from the stack.
    /// This context juggling enables us to make the `CuContext` implement `Send` and `Sync`. Otherwise it would be bound to the thread
    /// that it was created on.
    pub fn exec<R>(&self, f: impl FnOnce() -> Result<R, CudaError>) -> Result<R, CudaError> {
        from_cuda_error(unsafe { sys::cuCtxPushCurrent_v2(self.inner) })?;
        let ret = f();
        let mut ctx = ptr::null_mut();
        from_cuda_error(unsafe { sys::cuCtxPopCurrent_v2(&mut ctx) })?;
        ret
    }
}

impl Drop for CuContext {
    fn drop(&mut self) {
        unsafe { sys::cuCtxDestroy_v2(self.inner) };
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CuContextFlags {
    /// Controls how the CPU and GPU interact.
    pub sched: CuContextFlagSched,
    /// Instruct CUDA to support mapped pinned allocations. This flag must be set in order to allocate pinned host memory that is accessible to the GPU.
    pub map_host: bool,
    /// Instruct CUDA to not reduce local memory after resizing local memory for a kernel. This can prevent thrashing by local memory allocations when
    /// launching many kernels with high local memory usage at the cost of potentially increased memory usage.
    pub lmem_resize_to_max: bool,
}

impl CuContextFlags {
    pub fn to_raw(&self) -> c_uint {
        let mut ret: c_uint = match self.sched {
            CuContextFlagSched::Auto => sys::CUctx_flags::CU_CTX_SCHED_AUTO,
            CuContextFlagSched::Spin => sys::CUctx_flags::CU_CTX_SCHED_SPIN,
            CuContextFlagSched::Yield => sys::CUctx_flags::CU_CTX_SCHED_YIELD,
            CuContextFlagSched::BlockingSync => sys::CUctx_flags::CU_CTX_SCHED_BLOCKING_SYNC,
        } as c_uint;
        if self.map_host {
            ret |= sys::CUctx_flags::CU_CTX_MAP_HOST as c_uint;
        }
        if self.lmem_resize_to_max {
            ret |= sys::CUctx_flags::CU_CTX_LMEM_RESIZE_TO_MAX as c_uint;
        }
        ret
    }
}

#[derive(Debug, Copy, Clone)]
pub enum CuContextFlagSched {
    /// Uses a heuristic based on the number of active CUDA contexts in the process C and the number of logical processors in the system P. If C > P then CUDA
    /// will yield to other OS threads when waiting for the GPU, otherwise CUDA will not yield while waiting for results and actively spin on the processor.
    Auto,
    /// Spin while waiting for results. This improves latency but may lower the performance of CPU threads if they are performing work in parallel with the GPU.
    Spin,
    /// Yield while waiting for results. This can increase latency when waiting for the GPU but can increase the performance of CPU threads performing work in parallel with the GPU.
    Yield,
    /// Block on a synchronization primitive while waiting for the GPU to finish work.
    BlockingSync,
}

impl Default for CuContextFlagSched {
    fn default() -> Self {
        Self::Auto
    }
}

/// Can be cloned freely, only contains handles to other structs. Internally reference counted.
#[derive(Clone)]
pub struct CuVideoDecoder<'a> {
    inner: sys::CUvideodecoder,
    codec: sys::cudaVideoCodec,
    chroma_format: sys::cudaVideoChromaFormat,
    height: c_ulong,
    arc: Option<Arc<()>>,
    ctx: &'a CuContext,
}

impl<'a> CuVideoDecoder<'a> {
    pub fn new(ctx: &'a CuContext, dci: &CuvidDecodeCreateInfo) -> Result<Self, CudaError> {
        ctx.exec(|| {
            let mut inner: sys::CUvideodecoder = ptr::null_mut();
            // In theory the API could put an output here. I'll worry about that if a future version of CUDA adds any outputs to this struct.
            let mut dci = sys::CUVIDDECODECREATEINFO::from(dci);
            from_cuda_error(unsafe { sys::cuvidCreateDecoder(&mut inner, &mut dci) })?;
            Ok(CuVideoDecoder {
                inner,
                codec: dci.CodecType,
                chroma_format: dci.ChromaFormat,
                height: dci.ulTargetHeight,
                arc: Some(Arc::new(())),
                ctx,
            })
        })
    }

    pub fn decode_picture(&self, pic_params: &CuvidPicParams) -> Result<(), CudaError> {
        assert_eq!(self.codec, pic_params.codec_specific.codec());
        self.ctx.exec(|| {
            let mut pic_params = pic_params.into();
            from_cuda_error(unsafe { sys::cuvidDecodePicture(self.inner, &mut pic_params) })?;
            // If Nvidia ever adds an out param here then make sure to convert back and assign to the original argument.
            Ok(())
        })
    }

    /// For best throughput it's recommended to perform mapping and `decode_picture` calls on different threads.
    pub fn map_video_frame(&self, pic_idx: i32, proc_params: &CuvidProcParams) -> Result<MapVideoFrameOutput, CudaError> {
        self.ctx.exec(|| {
            let mut dev_ptr = 0;
            let mut pitch = 0;
            let mut histogram_dptr = 0;
            let mut params = proc_params.to_sys(&mut histogram_dptr);
            from_cuda_error(unsafe { sys::cuvidMapVideoFrame64(self.inner, pic_idx as c_int, &mut dev_ptr, &mut pitch, &mut params) })?;
            // Formula taken from NVIDIA documentation
            let memory_len = match self.chroma_format {
                sys::cudaVideoChromaFormat::cudaVideoChromaFormat_444 => pitch as c_ulong * (3 * self.height),
                _ => pitch as c_ulong * (self.height + (self.height + 1) / 2),
            };
            Ok(MapVideoFrameOutput {
                dev_mem: DecoderMemory {
                    dev_ptr,
                    len: memory_len as usize,
                    decoder: self,
                },
                pitch,
                histogram_dptr,
            })
        })
    }

    pub fn unmap_video_frame(&self, dev_ptr: c_ulonglong) -> Result<(), CudaError> {
        self.ctx.exec(|| from_cuda_error(unsafe { sys::cuvidUnmapVideoFrame64(self.inner, dev_ptr) }))
    }
}

impl Drop for CuVideoDecoder<'_> {
    fn drop(&mut self) {
        if Arc::into_inner(self.arc.take().expect("Arc never removed prior to drop")).is_some() {
            unsafe { sys::cuvidDestroyDecoder(self.inner) };
        }
    }
}

pub struct CuvidProcParams {
    // Docs copy pasted from code gen
    #[doc = "< IN: Input is progressive (deinterlace_mode will be ignored)"]
    pub progressive_frame: c_int,
    #[doc = "< IN: Output the second field (ignored if deinterlace mode is Weave)"]
    pub second_field: c_int,
    #[doc = "< IN: Input frame is top field first (1st field is top, 2nd field is bottom)"]
    pub top_field_first: c_int,
    #[doc = "< IN: Input only contains one field (2nd field is invalid)"]
    pub unpaired_field: c_int,
    #[doc = "< Reserved for future use (set to zero)"]
    pub reserved_flags: c_uint,
    #[doc = "< Reserved (set to zero)"]
    pub reserved_zero: c_uint,
    #[doc = "< IN: Input CUdeviceptr for raw YUV extensions"]
    pub raw_input_dptr: c_ulonglong,
    #[doc = "< IN: pitch in bytes of raw YUV input (should be aligned appropriately)"]
    pub raw_input_pitch: c_uint,
    #[doc = "< IN: Input YUV format (cudaVideoCodec_enum)"]
    pub raw_input_format: c_uint,
    #[doc = "< IN: Output CUdeviceptr for raw YUV extensions"]
    pub raw_output_dptr: c_ulonglong,
    #[doc = "< IN: pitch in bytes of raw YUV output (should be aligned appropriately)"]
    pub raw_output_pitch: c_uint,
    #[doc = "< IN: stream object used by cuvidMapVideoFrame, or NULL for default stream"]
    pub output_stream: sys::CUstream,
}

impl CuvidProcParams {
    fn to_sys(&self, histogram_dptr: &mut c_ulonglong) -> sys::CUVIDPROCPARAMS {
        let Self {
            progressive_frame,
            second_field,
            top_field_first,
            unpaired_field,
            reserved_flags,
            reserved_zero,
            raw_input_dptr,
            raw_input_pitch,
            raw_input_format,
            raw_output_dptr,
            raw_output_pitch,
            output_stream,
        } = *self;
        sys::CUVIDPROCPARAMS {
            progressive_frame,
            second_field,
            top_field_first,
            unpaired_field,
            reserved_flags,
            reserved_zero,
            raw_input_dptr,
            raw_input_pitch,
            raw_input_format,
            raw_output_dptr,
            raw_output_pitch,
            output_stream,
            histogram_dptr,
            Reserved: unsafe { mem::MaybeUninit::zeroed().assume_init() },
            Reserved1: unsafe { mem::MaybeUninit::zeroed().assume_init() },
            Reserved2: unsafe { mem::MaybeUninit::zeroed().assume_init() },
        }
    }
}

pub struct MapVideoFrameOutput<'decoder, 'context> {
    /// Use this with CUDA memory calls to get data out
    pub dev_mem: DecoderMemory<'decoder, 'context>,
    pub pitch: c_uint,
    /// Use this with CUDA memory calls to get data out
    pub histogram_dptr: c_ulonglong,
}

/// Memory owned by CUDA, specifically the decoder hardware.
pub struct DecoderMemory<'decoder, 'context> {
    dev_ptr: c_ulonglong,
    len: usize,
    decoder: &'decoder CuVideoDecoder<'context>,
}

impl<'context> DecoderMemory<'_, 'context> {
    /// Copies `byte_count` bytes from the device memory into the host memory.
    /// The host memory and the device memory must be at least as large as `byte_count`.
    /// Leave `byte_count` as `None` to copy the entire `DecoderMemory` block.
    /// If `byte_count` is larger than the entire `DecoderMemory` block then it will be treated as `None`.
    pub async fn memcpy_d_to_h(&self, dst: &CudaHostMemory<'context>, byte_count: Option<usize>) -> Result<(), CudaError> {
        let byte_count = if let Some(byte_count) = byte_count {
            byte_count.min(self.len)
        } else {
            self.len
        }
        .min(dst.len);
        self.decoder
            .ctx
            .exec(|| {
                let mut stream = ptr::null_mut();
                from_cuda_error(unsafe { sys::cuStreamCreate(&mut stream, sys::CUstream_flags::CU_STREAM_NON_BLOCKING as u32) })?;
                from_cuda_error(unsafe { sys::cuMemcpyDtoHAsync_v2(dst.ptr, self.dev_ptr, byte_count, stream) })?;
                CuStreamFuture::new(self.decoder.ctx, stream)
            })?
            .await;
        Ok(())
    }

    #[allow(clippy::len_without_is_empty)] // This isn't actually a container, so is_empty would be nonsense.
    pub fn len(&self) -> usize {
        self.len
    }
}

impl Drop for DecoderMemory<'_, '_> {
    fn drop(&mut self) {
        let _ = self
            .decoder
            .ctx
            .exec(|| from_cuda_error(unsafe { sys::cuvidUnmapVideoFrame64(self.decoder.inner, self.dev_ptr) }));
    }
}

pub struct CuStreamFuture<'context> {
    // If we're trying asynchronously to get a lock on the Mutex, store the future for that work here.
    // Must be a `Box` because the future is an opaque type. This data is not actually 'static,
    // it's tied to the lifetime of the `waker_storage`. We manage this lifetime internally via `unsafe`.
    // Do not ever expose this as part of the public API.
    mutex_lock_future: Option<Pin<Box<dyn Future<Output = MutexGuard<'static, WakerState>>>>>,
    waker_storage: Arc<Mutex<WakerState>>,
    _phantom: PhantomData<&'context CuContext>,
}

enum WakerState {
    Initial,
    HasWaker(Waker),
    WorkComplete,
}

impl<'context> CuStreamFuture<'context> {
    // Not intended for outside usage, this function is a bit unwieldy.
    // Call this with a stream after some work has been queued to create a future
    // that will resolve once that work is finished.
    fn new(ctx: &'context CuContext, stream: sys::CUstream) -> Result<Self, CudaError> {
        ctx.exec(|| {
            unsafe {
                let waker_storage = Arc::new(Mutex::new(WakerState::Initial));
                // Leaking an Arc on purpose here. This must be reclaimed inside the callback.
                let user_data_ptr: *const Mutex<WakerState> = Arc::into_raw(Arc::clone(&waker_storage));
                let ret = from_cuda_error(sys::cuLaunchHostFunc(stream, Some(Self::wake_future), user_data_ptr as *mut c_void));
                if ret.is_err() {
                    // Cleanup leaked Arc here if error occurs.
                    Arc::<Mutex<WakerState>>::from_raw(user_data_ptr);
                }
                ret?;
                Ok(Self {
                    mutex_lock_future: None,
                    waker_storage,
                    _phantom: PhantomData,
                })
            }
        })
    }

    unsafe extern "C" fn wake_future(user_data: *mut c_void) {
        let user_data = Arc::<Mutex<WakerState>>::from_raw(user_data as *mut Mutex<WakerState>);
        let mut state = WakerState::WorkComplete;
        {
            let mut guard = user_data.blocking_lock();
            // In all cases we move to work complete, so swap that in.
            mem::swap(&mut state, &mut *guard);
        }
        // As a minor optimization we drop our lock before waking the future to reduce the chance of lock contention occuring.
        if let WakerState::HasWaker(waker) = state {
            waker.wake();
        }
    }
}

impl Future for CuStreamFuture<'_> {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> task::Poll<Self::Output> {
        let mut mutex_lock_future = self.mutex_lock_future.take().unwrap_or_else(|| {
            // SAFETY: We're going to transmute the lifetime on this a bit. Instead of the lifetime of the Mutex reference,
            // we're going to force it to lifetime 'static. This is a convenient lie. The data is not actually 'static.
            // We manage its actual lifetime internally. Do not ever expose this lifetime publicly.
            unsafe {
                // Types fully specified to prevent mistakes. Please don't remove these types.
                mem::transmute::<Pin<Box<dyn Future<Output = MutexGuard<'_, WakerState>>>>, Pin<Box<dyn Future<Output = MutexGuard<'static, WakerState>>>>>(
                    Box::pin(self.waker_storage.lock()),
                )
            }
        });
        let mutex_poll = Future::poll(mutex_lock_future.as_mut(), cx);
        let mut mutex_guard = match mutex_poll {
            task::Poll::Ready(guard) => guard,
            task::Poll::Pending => {
                // Still waiting for a lock, put the future in storage and then return `Pending`.
                self.mutex_lock_future = Some(mutex_lock_future);
                return task::Poll::Pending;
            }
        };
        match mutex_guard.deref() {
            WakerState::Initial | WakerState::HasWaker(_) => {
                // We got polled before the CUDA work finished, store the waker and return Pending.
                *mutex_guard = WakerState::HasWaker(cx.waker().clone());
                task::Poll::Pending
            }
            WakerState::WorkComplete => {
                // Cuda work finished, ready to complete.
                task::Poll::Ready(())
            }
        }
    }
}

impl Drop for CuStreamFuture<'_> {
    fn drop(&mut self) {
        // Uphold our prior promises by dropping the `MutexGuard` before anything else if it exists.
        self.mutex_lock_future.take();
    }
}

/// Memory owned by the CUDA driver, and stored on the host
pub struct CudaHostMemory<'context> {
    // Specifically choosing against a slice here because it is undefined
    // behavior to carry a slice that points to invalid data, even momentarily.
    // This could come up with our `Drop` implementation.
    // That might be fine, but why take the risk?
    ptr: *mut c_void,
    len: usize,
    _phantom: PhantomData<&'context CuContext>,
}

impl CudaHostMemory<'_> {
    pub fn as_slice(&self) -> &[u8] {
        // Safe so long as return lifetime is constrained by input lifetime.
        unsafe { slice::from_raw_parts(self.ptr as *const c_void as *const u8, self.len) }
    }

    pub fn as_slice_mut(&self) -> &[u8] {
        // Safe so long as return lifetime is constrained by input lifetime.
        unsafe { slice::from_raw_parts_mut(self.ptr as *mut u8, self.len) }
    }
}

impl Drop for CudaHostMemory<'_> {
    fn drop(&mut self) {
        unsafe { sys::cuMemFreeHost(self.ptr) };
    }
}

pub struct CuvidDecodeCreateInfo {
    pub ul_width: u32,
    pub ul_height: u32,
    pub ul_num_decode_surfaces: u32,
    pub codec_type: sys::cudaVideoCodec,
    pub chroma_format: sys::cudaVideoChromaFormat,
    pub ul_creation_flags: u32,
    pub bit_depth_minus_8: u32,
    /// < IN: Set 1 only if video has all intra frames (default value is 0).
    /// This will optimize video memory for Intra frames only decoding.
    /// The support is limited to specific codecs - H264, HEVC, VP9, the flag will be ignored for codecs which are not supported.
    /// However decoding might fail if the flag is enabled in case of supported codecs for regular bit streams having P and/or B frames.
    pub ul_intra_decode_only: bool,
    pub ul_max_width: u32,
    pub ul_max_height: u32,
    pub display_area: Rect,
    pub output_format: sys::cudaVideoSurfaceFormat,
    pub deinterlace_mode: sys::cudaVideoDeinterlaceMode,
    pub ul_target_width: u32,
    pub ul_target_height: u32,
    pub ul_num_output_surfaces: u32,
    // Omitting vidLock on purpose for now.
    pub target_rect: Rect,
    pub enable_histogram: u32,
}

impl From<&CuvidDecodeCreateInfo> for sys::CUVIDDECODECREATEINFO {
    fn from(value: &CuvidDecodeCreateInfo) -> Self {
        sys::CUVIDDECODECREATEINFO {
            ulWidth: value.ul_width as c_ulong,
            ulHeight: value.ul_height as c_ulong,
            ulNumDecodeSurfaces: value.ul_num_decode_surfaces as c_ulong,
            CodecType: value.codec_type,
            ChromaFormat: value.chroma_format,
            ulCreationFlags: value.ul_creation_flags as c_ulong,
            bitDepthMinus8: value.bit_depth_minus_8 as c_ulong,
            ulIntraDecodeOnly: value.ul_intra_decode_only as c_ulong,
            ulMaxWidth: value.ul_max_width as c_ulong,
            ulMaxHeight: value.ul_max_height as c_ulong,
            Reserved1: 0,
            display_area: sys::_CUVIDDECODECREATEINFO__bindgen_ty_1 {
                left: value.display_area.left as c_short,
                top: value.display_area.top as c_short,
                right: value.display_area.right as c_short,
                bottom: value.display_area.bottom as c_short,
            },
            OutputFormat: value.output_format,
            DeinterlaceMode: value.deinterlace_mode,
            ulTargetWidth: value.ul_target_width as c_ulong,
            ulTargetHeight: value.ul_target_height as c_ulong,
            ulNumOutputSurfaces: value.ul_num_output_surfaces as c_ulong,
            vidLock: null_mut(),
            target_rect: sys::_CUVIDDECODECREATEINFO__bindgen_ty_2 {
                left: value.target_rect.left as c_short,
                top: value.target_rect.top as c_short,
                right: value.target_rect.right as c_short,
                bottom: value.target_rect.bottom as c_short,
            },
            enableHistogram: value.enable_histogram as c_ulong,
            Reserved2: [0; 4],
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct Rect {
    pub left: i16,
    pub top: i16,
    pub right: i16,
    pub bottom: i16,
}

pub struct CuvidPicParams<'a, 'b> {
    /// Coded frame size in macroblocks
    pub pic_width_in_mbs: i32,
    pub frame_height_in_mbs: i32,
    pub curr_pic_idx: i32,
    pub field_pic_flag: i32,
    pub bottom_field_flag: i32,
    pub second_field: i32,
    pub bitstream_data: &'a [u8],
    pub slice_data_offsets: &'b [u32],
    pub ref_pic_flag: i32,
    pub intra_pic_flag: i32,
    pub codec_specific: CuvidPicParamsCodecSpecific,
}

#[derive(Debug)]
// Not enough instances of this enum to justify heap allocations to shrink it
// Additionally, enum is accessed often. Better to keep it in stack.
#[allow(clippy::large_enum_variant)]
pub enum CuvidPicParamsCodecSpecific {
    Mpeg2 {},
    H264 {
        log2_max_frame_num_minus4: c_int,
        pic_order_cnt_type: c_int,
        log2_max_pic_order_cnt_lsb_minus4: c_int,
        delta_pic_order_always_zero_flag: c_int,
        frame_mbs_only_flag: c_int,
        direct_8x8_inference_flag: c_int,
        num_ref_frames: c_int,
        residual_colour_transform_flag: c_uchar,
        bit_depth_luma_minus8: c_uchar,
        bit_depth_chroma_minus8: c_uchar,
        qpprime_y_zero_transform_bypass_flag: c_uchar,
        entropy_coding_mode_flag: c_int,
        pic_order_present_flag: c_int,
        num_ref_idx_l0_active_minus1: c_int,
        num_ref_idx_l1_active_minus1: c_int,
        weighted_pred_flag: c_int,
        weighted_bipred_idc: c_int,
        pic_init_qp_minus26: c_int,
        deblocking_filter_control_present_flag: c_int,
        redundant_pic_cnt_present_flag: c_int,
        transform_8x8_mode_flag: c_int,
        mbaff_frame_flag: c_int,
        constrained_intra_pred_flag: c_int,
        chroma_qp_index_offset: c_int,
        second_chroma_qp_index_offset: c_int,
        ref_pic_flag: c_int,
        frame_num: c_int,
        curr_field_order_cnt: [c_int; 2],
        dpb: [sys::_CUVIDH264DPBENTRY; 16],
        weight_scale_4x4: [[c_uchar; 16]; 6],
        weight_scale_8x8: [[c_uchar; 64]; 2],
        fmo_aso_enable: c_uchar,
        num_slice_groups_minus1: c_uchar,
        slice_group_map_type: c_uchar,
        pic_init_qs_minus26: c_schar,
        slice_group_change_rate_minus1: c_uint,
        fmo: SliceGroupMap,
    },
    H264Svc {},
    H264Mvc {},
    VC1 {},
    Mpeg4 {},
    Jpeg {},
    Hevc {
        pic_width_in_luma_samples: c_int,
        pic_height_in_luma_samples: c_int,
        log2_min_luma_coding_block_size_minus3: c_uchar,
        log2_diff_max_min_luma_coding_block_size: c_uchar,
        log2_min_transform_block_size_minus2: c_uchar,
        log2_diff_max_min_transform_block_size: c_uchar,
        pcm_enabled_flag: c_uchar,
        log2_min_pcm_luma_coding_block_size_minus3: c_uchar,
        log2_diff_max_min_pcm_luma_coding_block_size: c_uchar,
        pcm_sample_bit_depth_luma_minus1: c_uchar,
        pcm_sample_bit_depth_chroma_minus1: c_uchar,
        pcm_loop_filter_disabled_flag: c_uchar,
        strong_intra_smoothing_enabled_flag: c_uchar,
        max_transform_hierarchy_depth_intra: c_uchar,
        max_transform_hierarchy_depth_inter: c_uchar,
        amp_enabled_flag: c_uchar,
        separate_colour_plane_flag: c_uchar,
        log2_max_pic_order_cnt_lsb_minus4: c_uchar,
        num_short_term_ref_pic_sets: c_uchar,
        long_term_ref_pics_present_flag: c_uchar,
        num_long_term_ref_pics_sps: c_uchar,
        sps_temporal_mvp_enabled_flag: c_uchar,
        sample_adaptive_offset_enabled_flag: c_uchar,
        scaling_list_enable_flag: c_uchar,
        irap_pic_flag: c_uchar,
        idr_pic_flag: c_uchar,
        bit_depth_luma_minus8: c_uchar,
        bit_depth_chroma_minus8: c_uchar,
        log2_max_transform_skip_block_size_minus2: c_uchar,
        log2_sao_offset_scale_luma: c_uchar,
        log2_sao_offset_scale_chroma: c_uchar,
        high_precision_offsets_enabled_flag: c_uchar,
        dependent_slice_segments_enabled_flag: c_uchar,
        slice_segment_header_extension_present_flag: c_uchar,
        sign_data_hiding_enabled_flag: c_uchar,
        cu_qp_delta_enabled_flag: c_uchar,
        diff_cu_qp_delta_depth: c_uchar,
        init_qp_minus26: c_schar,
        pps_cb_qp_offset: c_schar,
        pps_cr_qp_offset: c_schar,
        constrained_intra_pred_flag: c_uchar,
        weighted_pred_flag: c_uchar,
        weighted_bipred_flag: c_uchar,
        transform_skip_enabled_flag: c_uchar,
        transquant_bypass_enabled_flag: c_uchar,
        entropy_coding_sync_enabled_flag: c_uchar,
        log2_parallel_merge_level_minus2: c_uchar,
        num_extra_slice_header_bits: c_uchar,
        loop_filter_across_tiles_enabled_flag: c_uchar,
        loop_filter_across_slices_enabled_flag: c_uchar,
        output_flag_present_flag: c_uchar,
        num_ref_idx_l0_default_active_minus1: c_uchar,
        num_ref_idx_l1_default_active_minus1: c_uchar,
        lists_modification_present_flag: c_uchar,
        cabac_init_present_flag: c_uchar,
        pps_slice_chroma_qp_offsets_present_flag: c_uchar,
        deblocking_filter_override_enabled_flag: c_uchar,
        pps_deblocking_filter_disabled_flag: c_uchar,
        pps_beta_offset_div2: c_schar,
        pps_tc_offset_div2: c_schar,
        tiles_enabled_flag: c_uchar,
        uniform_spacing_flag: c_uchar,
        num_tile_columns_minus1: c_uchar,
        num_tile_rows_minus1: c_uchar,
        column_width_minus1: [c_ushort; 21usize],
        row_height_minus1: [c_ushort; 21usize],
        sps_range_extension_flag: c_uchar,
        transform_skip_rotation_enabled_flag: c_uchar,
        transform_skip_context_enabled_flag: c_uchar,
        implicit_rdpcm_enabled_flag: c_uchar,
        explicit_rdpcm_enabled_flag: c_uchar,
        extended_precision_processing_flag: c_uchar,
        intra_smoothing_disabled_flag: c_uchar,
        persistent_rice_adaptation_enabled_flag: c_uchar,
        cabac_bypass_alignment_enabled_flag: c_uchar,
        pps_range_extension_flag: c_uchar,
        cross_component_prediction_enabled_flag: c_uchar,
        chroma_qp_offset_list_enabled_flag: c_uchar,
        diff_cu_chroma_qp_offset_depth: c_uchar,
        chroma_qp_offset_list_len_minus1: c_uchar,
        cb_qp_offset_list: [c_schar; 6usize],
        cr_qp_offset_list: [c_schar; 6usize],
        num_bits_for_short_term_rpsin_slice: c_int,
        num_delta_pocs_of_ref_rps_idx: c_int,
        num_poc_total_curr: c_int,
        num_poc_st_curr_before: c_int,
        num_poc_st_curr_after: c_int,
        num_poc_lt_curr: c_int,
        curr_pic_order_cnt_val: c_int,
        ref_pic_idx: [c_int; 16usize],
        pic_order_cnt_val: [c_int; 16usize],
        is_long_term: [c_uchar; 16usize],
        ref_pic_set_st_curr_before: [c_uchar; 8usize],
        ref_pic_set_st_curr_after: [c_uchar; 8usize],
        ref_pic_set_lt_curr: [c_uchar; 8usize],
        ref_pic_set_inter_layer0: [c_uchar; 8usize],
        ref_pic_set_inter_layer1: [c_uchar; 8usize],
        scaling_list4x4: [[c_uchar; 16usize]; 6usize],
        scaling_list8x8: [[c_uchar; 64usize]; 6usize],
        scaling_list16x16: [[c_uchar; 64usize]; 6usize],
        scaling_list32x32: [[c_uchar; 64usize]; 2usize],
        scaling_list_dccoeff16x16: [c_uchar; 6usize],
        scaling_list_dccoeff32x32: [c_uchar; 2usize],
    },
    Vp8 {},
    Vp9 {},
    Av1 {},
}

impl CuvidPicParamsCodecSpecific {
    pub fn codec(&self) -> sys::cudaVideoCodec {
        use sys::cudaVideoCodec::*;
        match self {
            CuvidPicParamsCodecSpecific::Mpeg2 { .. } => cudaVideoCodec_MPEG2,
            CuvidPicParamsCodecSpecific::H264 { .. } => cudaVideoCodec_H264,
            CuvidPicParamsCodecSpecific::VC1 { .. } => cudaVideoCodec_VC1,
            CuvidPicParamsCodecSpecific::Mpeg4 { .. } => cudaVideoCodec_MPEG4,
            CuvidPicParamsCodecSpecific::Jpeg { .. } => cudaVideoCodec_JPEG,
            CuvidPicParamsCodecSpecific::Hevc { .. } => cudaVideoCodec_HEVC,
            CuvidPicParamsCodecSpecific::Vp8 { .. } => cudaVideoCodec_VP8,
            CuvidPicParamsCodecSpecific::Vp9 { .. } => cudaVideoCodec_VP9,
            CuvidPicParamsCodecSpecific::Av1 { .. } => cudaVideoCodec_AV1,
            CuvidPicParamsCodecSpecific::H264Svc { .. } => cudaVideoCodec_H264_SVC,
            CuvidPicParamsCodecSpecific::H264Mvc { .. } => cudaVideoCodec_H264_MVC,
        }
    }
}

impl From<&CuvidPicParamsCodecSpecific> for sys::_CUVIDPICPARAMS__bindgen_ty_1 {
    fn from(value: &CuvidPicParamsCodecSpecific) -> Self {
        match *value {
            CuvidPicParamsCodecSpecific::Mpeg2 {} => todo!(),
            CuvidPicParamsCodecSpecific::H264 {
                log2_max_frame_num_minus4,
                pic_order_cnt_type,
                log2_max_pic_order_cnt_lsb_minus4,
                delta_pic_order_always_zero_flag,
                frame_mbs_only_flag,
                direct_8x8_inference_flag,
                num_ref_frames,
                residual_colour_transform_flag,
                bit_depth_luma_minus8,
                bit_depth_chroma_minus8,
                qpprime_y_zero_transform_bypass_flag,
                entropy_coding_mode_flag,
                pic_order_present_flag,
                num_ref_idx_l0_active_minus1,
                num_ref_idx_l1_active_minus1,
                weighted_pred_flag,
                weighted_bipred_idc,
                pic_init_qp_minus26,
                deblocking_filter_control_present_flag,
                redundant_pic_cnt_present_flag,
                transform_8x8_mode_flag,
                mbaff_frame_flag,
                constrained_intra_pred_flag,
                chroma_qp_index_offset,
                second_chroma_qp_index_offset,
                ref_pic_flag,
                frame_num,
                curr_field_order_cnt,
                dpb,
                weight_scale_4x4,
                weight_scale_8x8,
                fmo_aso_enable,
                num_slice_groups_minus1,
                slice_group_map_type,
                pic_init_qs_minus26,
                slice_group_change_rate_minus1,
                fmo,
            } => sys::_CUVIDPICPARAMS__bindgen_ty_1 {
                h264: sys::_CUVIDH264PICPARAMS {
                    log2_max_frame_num_minus4,
                    pic_order_cnt_type,
                    log2_max_pic_order_cnt_lsb_minus4,
                    delta_pic_order_always_zero_flag,
                    frame_mbs_only_flag,
                    direct_8x8_inference_flag,
                    num_ref_frames,
                    residual_colour_transform_flag,
                    bit_depth_luma_minus8,
                    bit_depth_chroma_minus8,
                    qpprime_y_zero_transform_bypass_flag,
                    entropy_coding_mode_flag,
                    pic_order_present_flag,
                    num_ref_idx_l0_active_minus1,
                    num_ref_idx_l1_active_minus1,
                    weighted_pred_flag,
                    weighted_bipred_idc,
                    pic_init_qp_minus26,
                    deblocking_filter_control_present_flag,
                    redundant_pic_cnt_present_flag,
                    transform_8x8_mode_flag,
                    MbaffFrameFlag: mbaff_frame_flag,
                    constrained_intra_pred_flag,
                    chroma_qp_index_offset,
                    ref_pic_flag,
                    frame_num,
                    CurrFieldOrderCnt: curr_field_order_cnt,
                    dpb,
                    WeightScale4x4: weight_scale_4x4,
                    WeightScale8x8: weight_scale_8x8,
                    fmo_aso_enable,
                    num_slice_groups_minus1,
                    slice_group_map_type,
                    pic_init_qs_minus26,
                    slice_group_change_rate_minus1,
                    fmo: match fmo {
                        SliceGroupMap::Addr(a) => sys::_CUVIDH264PICPARAMS__bindgen_ty_1 { slice_group_map_addr: a },
                        SliceGroupMap::Mb2(p) => sys::_CUVIDH264PICPARAMS__bindgen_ty_1 { pMb2SliceGroupMap: p },
                    },
                    second_chroma_qp_index_offset,
                    _bitfield_align_1: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    _bitfield_1: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    Reserved: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    __bindgen_anon_1: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                },
            },
            CuvidPicParamsCodecSpecific::H264Svc {} => todo!(),
            CuvidPicParamsCodecSpecific::H264Mvc {} => todo!(),
            CuvidPicParamsCodecSpecific::VC1 {} => todo!(),
            CuvidPicParamsCodecSpecific::Mpeg4 {} => todo!(),
            CuvidPicParamsCodecSpecific::Jpeg {} => todo!(),
            CuvidPicParamsCodecSpecific::Hevc {
                pic_width_in_luma_samples,
                pic_height_in_luma_samples,
                log2_min_luma_coding_block_size_minus3,
                log2_diff_max_min_luma_coding_block_size,
                log2_min_transform_block_size_minus2,
                log2_diff_max_min_transform_block_size,
                pcm_enabled_flag,
                log2_min_pcm_luma_coding_block_size_minus3,
                log2_diff_max_min_pcm_luma_coding_block_size,
                pcm_sample_bit_depth_luma_minus1,
                pcm_sample_bit_depth_chroma_minus1,
                pcm_loop_filter_disabled_flag,
                strong_intra_smoothing_enabled_flag,
                max_transform_hierarchy_depth_intra,
                max_transform_hierarchy_depth_inter,
                amp_enabled_flag,
                separate_colour_plane_flag,
                log2_max_pic_order_cnt_lsb_minus4,
                num_short_term_ref_pic_sets,
                long_term_ref_pics_present_flag,
                num_long_term_ref_pics_sps,
                sps_temporal_mvp_enabled_flag,
                sample_adaptive_offset_enabled_flag,
                scaling_list_enable_flag,
                irap_pic_flag,
                idr_pic_flag,
                bit_depth_luma_minus8,
                bit_depth_chroma_minus8,
                log2_max_transform_skip_block_size_minus2,
                log2_sao_offset_scale_luma,
                log2_sao_offset_scale_chroma,
                high_precision_offsets_enabled_flag,
                dependent_slice_segments_enabled_flag,
                slice_segment_header_extension_present_flag,
                sign_data_hiding_enabled_flag,
                cu_qp_delta_enabled_flag,
                diff_cu_qp_delta_depth,
                init_qp_minus26,
                pps_cb_qp_offset,
                pps_cr_qp_offset,
                constrained_intra_pred_flag,
                weighted_pred_flag,
                weighted_bipred_flag,
                transform_skip_enabled_flag,
                transquant_bypass_enabled_flag,
                entropy_coding_sync_enabled_flag,
                log2_parallel_merge_level_minus2,
                num_extra_slice_header_bits,
                loop_filter_across_tiles_enabled_flag,
                loop_filter_across_slices_enabled_flag,
                output_flag_present_flag,
                num_ref_idx_l0_default_active_minus1,
                num_ref_idx_l1_default_active_minus1,
                lists_modification_present_flag,
                cabac_init_present_flag,
                pps_slice_chroma_qp_offsets_present_flag,
                deblocking_filter_override_enabled_flag,
                pps_deblocking_filter_disabled_flag,
                pps_beta_offset_div2,
                pps_tc_offset_div2,
                tiles_enabled_flag,
                uniform_spacing_flag,
                num_tile_columns_minus1,
                num_tile_rows_minus1,
                column_width_minus1,
                row_height_minus1,
                sps_range_extension_flag,
                transform_skip_rotation_enabled_flag,
                transform_skip_context_enabled_flag,
                implicit_rdpcm_enabled_flag,
                explicit_rdpcm_enabled_flag,
                extended_precision_processing_flag,
                intra_smoothing_disabled_flag,
                persistent_rice_adaptation_enabled_flag,
                cabac_bypass_alignment_enabled_flag,
                pps_range_extension_flag,
                cross_component_prediction_enabled_flag,
                chroma_qp_offset_list_enabled_flag,
                diff_cu_chroma_qp_offset_depth,
                chroma_qp_offset_list_len_minus1,
                cb_qp_offset_list,
                cr_qp_offset_list,
                num_bits_for_short_term_rpsin_slice,
                num_delta_pocs_of_ref_rps_idx,
                num_poc_total_curr,
                num_poc_st_curr_before,
                num_poc_st_curr_after,
                num_poc_lt_curr,
                curr_pic_order_cnt_val,
                ref_pic_idx,
                pic_order_cnt_val,
                is_long_term,
                ref_pic_set_st_curr_before,
                ref_pic_set_st_curr_after,
                ref_pic_set_lt_curr,
                ref_pic_set_inter_layer0,
                ref_pic_set_inter_layer1,
                scaling_list4x4,
                scaling_list8x8,
                scaling_list16x16,
                scaling_list32x32,
                scaling_list_dccoeff16x16,
                scaling_list_dccoeff32x32,
            } => sys::_CUVIDPICPARAMS__bindgen_ty_1 {
                hevc: sys::CUVIDHEVCPICPARAMS {
                    pic_width_in_luma_samples,
                    pic_height_in_luma_samples,
                    log2_min_luma_coding_block_size_minus3,
                    log2_diff_max_min_luma_coding_block_size,
                    log2_min_transform_block_size_minus2,
                    log2_diff_max_min_transform_block_size,
                    pcm_enabled_flag,
                    log2_min_pcm_luma_coding_block_size_minus3,
                    log2_diff_max_min_pcm_luma_coding_block_size,
                    pcm_sample_bit_depth_luma_minus1,
                    pcm_sample_bit_depth_chroma_minus1,
                    pcm_loop_filter_disabled_flag,
                    strong_intra_smoothing_enabled_flag,
                    max_transform_hierarchy_depth_intra,
                    max_transform_hierarchy_depth_inter,
                    amp_enabled_flag,
                    separate_colour_plane_flag,
                    log2_max_pic_order_cnt_lsb_minus4,
                    num_short_term_ref_pic_sets,
                    long_term_ref_pics_present_flag,
                    num_long_term_ref_pics_sps,
                    sps_temporal_mvp_enabled_flag,
                    sample_adaptive_offset_enabled_flag,
                    scaling_list_enable_flag,
                    IrapPicFlag: irap_pic_flag,
                    IdrPicFlag: idr_pic_flag,
                    bit_depth_luma_minus8,
                    bit_depth_chroma_minus8,
                    log2_max_transform_skip_block_size_minus2,
                    log2_sao_offset_scale_luma,
                    log2_sao_offset_scale_chroma,
                    high_precision_offsets_enabled_flag,
                    dependent_slice_segments_enabled_flag,
                    slice_segment_header_extension_present_flag,
                    sign_data_hiding_enabled_flag,
                    cu_qp_delta_enabled_flag,
                    diff_cu_qp_delta_depth,
                    init_qp_minus26,
                    pps_cb_qp_offset,
                    pps_cr_qp_offset,
                    constrained_intra_pred_flag,
                    weighted_pred_flag,
                    weighted_bipred_flag,
                    transform_skip_enabled_flag,
                    transquant_bypass_enabled_flag,
                    entropy_coding_sync_enabled_flag,
                    log2_parallel_merge_level_minus2,
                    num_extra_slice_header_bits,
                    loop_filter_across_tiles_enabled_flag,
                    loop_filter_across_slices_enabled_flag,
                    output_flag_present_flag,
                    num_ref_idx_l0_default_active_minus1,
                    num_ref_idx_l1_default_active_minus1,
                    lists_modification_present_flag,
                    cabac_init_present_flag,
                    pps_slice_chroma_qp_offsets_present_flag,
                    deblocking_filter_override_enabled_flag,
                    pps_deblocking_filter_disabled_flag,
                    pps_beta_offset_div2,
                    pps_tc_offset_div2,
                    tiles_enabled_flag,
                    uniform_spacing_flag,
                    num_tile_columns_minus1,
                    num_tile_rows_minus1,
                    column_width_minus1,
                    row_height_minus1,
                    sps_range_extension_flag,
                    transform_skip_rotation_enabled_flag,
                    transform_skip_context_enabled_flag,
                    implicit_rdpcm_enabled_flag,
                    explicit_rdpcm_enabled_flag,
                    extended_precision_processing_flag,
                    intra_smoothing_disabled_flag,
                    persistent_rice_adaptation_enabled_flag,
                    cabac_bypass_alignment_enabled_flag,
                    pps_range_extension_flag,
                    cross_component_prediction_enabled_flag,
                    chroma_qp_offset_list_enabled_flag,
                    diff_cu_chroma_qp_offset_depth,
                    chroma_qp_offset_list_len_minus1,
                    cb_qp_offset_list,
                    cr_qp_offset_list,
                    NumBitsForShortTermRPSInSlice: num_bits_for_short_term_rpsin_slice,
                    NumDeltaPocsOfRefRpsIdx: num_delta_pocs_of_ref_rps_idx,
                    NumPocTotalCurr: num_poc_total_curr,
                    NumPocStCurrBefore: num_poc_st_curr_before,
                    NumPocStCurrAfter: num_poc_st_curr_after,
                    NumPocLtCurr: num_poc_lt_curr,
                    CurrPicOrderCntVal: curr_pic_order_cnt_val,
                    RefPicIdx: ref_pic_idx,
                    PicOrderCntVal: pic_order_cnt_val,
                    IsLongTerm: is_long_term,
                    RefPicSetStCurrBefore: ref_pic_set_st_curr_before,
                    RefPicSetStCurrAfter: ref_pic_set_st_curr_after,
                    RefPicSetLtCurr: ref_pic_set_lt_curr,
                    RefPicSetInterLayer0: ref_pic_set_inter_layer0,
                    RefPicSetInterLayer1: ref_pic_set_inter_layer1,
                    ScalingList4x4: scaling_list4x4,
                    ScalingList8x8: scaling_list8x8,
                    ScalingList16x16: scaling_list16x16,
                    ScalingList32x32: scaling_list32x32,
                    ScalingListDCCoeff16x16: scaling_list_dccoeff16x16,
                    ScalingListDCCoeff32x32: scaling_list_dccoeff32x32,
                    reserved1: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    reserved2: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    reserved3: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                    reserved4: unsafe { mem::MaybeUninit::zeroed().assume_init() },
                },
            },
            CuvidPicParamsCodecSpecific::Vp8 {} => todo!(),
            CuvidPicParamsCodecSpecific::Vp9 {} => todo!(),
            CuvidPicParamsCodecSpecific::Av1 {} => todo!(),
        }
    }
}

impl<'a, 'b> From<&CuvidPicParams<'a, 'b>> for sys::CUVIDPICPARAMS {
    fn from(value: &CuvidPicParams<'a, 'b>) -> Self {
        sys::CUVIDPICPARAMS {
            PicWidthInMbs: value.pic_width_in_mbs,
            FrameHeightInMbs: value.frame_height_in_mbs,
            CurrPicIdx: value.curr_pic_idx,
            field_pic_flag: value.field_pic_flag,
            bottom_field_flag: value.bottom_field_flag,
            second_field: value.second_field,
            nBitstreamDataLen: value.bitstream_data.len() as u32,
            pBitstreamData: value.bitstream_data.as_ptr(),
            nNumSlices: value.slice_data_offsets.len() as u32,
            pSliceDataOffsets: value.slice_data_offsets.as_ptr(),
            ref_pic_flag: value.ref_pic_flag,
            intra_pic_flag: value.intra_pic_flag,
            Reserved: unsafe { mem::MaybeUninit::zeroed().assume_init() },
            CodecSpecific: (&value.codec_specific).into(),
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub enum SliceGroupMap {
    Addr(c_ulonglong),
    Mb2(*const c_uchar),
}

#[derive(Copy, Clone)]
pub struct CudaError(pub sys::cudaError_enum);

impl Debug for CudaError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut error_name: *const c_char = ptr::null();
        let mut error_string: *const c_char = ptr::null();
        if unsafe { sys::cuGetErrorName(self.0, &mut error_name) } == sys::cudaError_enum::CUDA_SUCCESS {
            // If the first succeeds then this one is guaranteed to as well. So we don't check the error.
            unsafe {
                sys::cuGetErrorString(self.0, &mut error_string);
            }
        } else {
            return write!(f, "internal error code invalid, no pretty print available");
        }
        let error_name = unsafe {
            if !error_name.is_null() {
                CStr::from_ptr(error_name).to_string_lossy()
            } else {
                "".into()
            }
        };
        let error_string = unsafe {
            if !error_string.is_null() {
                CStr::from_ptr(error_string).to_string_lossy()
            } else {
                "".into()
            }
        };
        write!(f, "{error_name}: {error_string}")
    }
}

impl From<sys::cudaError_enum> for CudaError {
    fn from(value: sys::cudaError_enum) -> Self {
        CudaError(value)
    }
}

pub fn from_cuda_error(maybe_success: sys::cudaError_enum) -> Result<(), CudaError> {
    if maybe_success == sys::cudaError_enum::CUDA_SUCCESS {
        Ok(())
    } else {
        Err(maybe_success.into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
}
