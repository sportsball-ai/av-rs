#import <AVFoundation/AVFoundation.h>

#if !__has_feature(objc_arc)
#error "ARC is off"
#endif

extern "C" const void* avrs_new_av_capture_video_data_output() {
    @autoreleasepool {
        AVCaptureVideoDataOutput* output = [AVCaptureVideoDataOutput new];
        return (__bridge_retained void*)output;
    }
}

@interface RustAVCaptureVideoDataOutputSampleBufferDelegate: NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>
    @property const void* impl;
@end

extern "C" void avrs_av_capture_video_data_output_sample_buffer_delegate_did_output(const void*, CMSampleBufferRef);

@implementation RustAVCaptureVideoDataOutputSampleBufferDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    avrs_av_capture_video_data_output_sample_buffer_delegate_did_output(self.impl, sampleBuffer);
}
@end

extern "C" const void* avrs_new_av_capture_video_data_output_sample_buffer_delegate(const void* delegateIn) {
    @autoreleasepool {
        RustAVCaptureVideoDataOutputSampleBufferDelegate* delegate = [RustAVCaptureVideoDataOutputSampleBufferDelegate new];
        delegate.impl = delegateIn;
        return (__bridge_retained void*)delegate;
    }
}

extern "C" void avrs_new_av_capture_video_data_output_set_sample_buffer_delegate(const void* outputIn, const void* delegateIn) {
    @autoreleasepool {
        AVCaptureVideoDataOutput* output = ((__bridge AVCaptureVideoDataOutput*)outputIn);
        RustAVCaptureVideoDataOutputSampleBufferDelegate* delegate = ((__bridge RustAVCaptureVideoDataOutputSampleBufferDelegate*)delegateIn);
        [output setSampleBufferDelegate:delegate queue:dispatch_queue_create("avrs.video-data-output", NULL)];
    }
}

extern "C" void avrs_new_av_capture_video_data_output_set_video_settings(const void* outputIn, uint32_t pixelFormatType) {
    @autoreleasepool {
        AVCaptureVideoDataOutput* output = ((__bridge AVCaptureVideoDataOutput*)outputIn);
        if (pixelFormatType) {
            output.videoSettings = @{(NSString*)kCVPixelBufferPixelFormatTypeKey: [NSNumber numberWithUnsignedInt:pixelFormatType]};
        } else {
            output.videoSettings = @{};
        }
    }
}
