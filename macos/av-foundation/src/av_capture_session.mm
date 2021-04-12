#import <AVFoundation/AVFoundation.h>

#if !__has_feature(objc_arc)
#error "ARC is off"
#endif

extern "C" const void* avrs_new_av_capture_session() {
    @autoreleasepool {
        AVCaptureSession* sess = [AVCaptureSession new];
        return (__bridge_retained void*)sess;
    }
}

extern "C" BOOL avrs_av_capture_session_can_add_input(const void* sessIn, const void* inputIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        sess.sessionPreset = AVCaptureSessionPresetMedium;
        AVCaptureDeviceInput* input = ((__bridge AVCaptureDeviceInput*)inputIn);
        return [sess canAddInput:input];
    }
}

extern "C" void avrs_av_capture_session_add_input(const void* sessIn, const void* inputIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        AVCaptureInput* input = ((__bridge AVCaptureInput*)inputIn);
        [sess addInput:input];
    }
}

extern "C" BOOL avrs_av_capture_session_can_add_output(const void* sessIn, const void* outputIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        AVCaptureOutput* output = ((__bridge AVCaptureOutput*)outputIn);
        return [sess canAddOutput:output];
    }
}

extern "C" void avrs_av_capture_session_add_output(const void* sessIn, const void* outputIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        AVCaptureOutput* output = ((__bridge AVCaptureOutput*)outputIn);
        [sess addOutput:output];
    }
}

extern "C" void avrs_av_capture_session_start_running(const void* sessIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        [sess startRunning];
    }
}

extern "C" void avrs_av_capture_session_stop_running(const void* sessIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        [sess stopRunning];
    }
}

extern "C" void avrs_av_capture_session_begin_configuration(const void* sessIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        [sess beginConfiguration];
    }
}

extern "C" void avrs_av_capture_session_commit_configuration(const void* sessIn) {
    @autoreleasepool {
        AVCaptureSession* sess = ((__bridge AVCaptureSession*)sessIn);
        [sess commitConfiguration];
    }
}
