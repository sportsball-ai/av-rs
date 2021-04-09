#import "av_media_type.h"

#if !__has_feature(objc_arc)
#error "ARC is off"
#endif

extern "C" const void* avrs_default_av_capture_device(int mediaType) {
    @autoreleasepool {
        AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:avrs_av_media_type_from_int(mediaType)];
        if (!device) {
            return nil;
        }
        return (__bridge_retained void*)device;
    }
}

extern "C" bool avrs_avf_request_av_capture_device_access(int mediaTypeIn) {
    AVMediaType mediaType = avrs_av_media_type_from_int(mediaTypeIn);

    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:mediaType];
    if (status == AVAuthorizationStatusAuthorized) {
        return true;
    }

    dispatch_group_t group = dispatch_group_create();
    __block bool accessGranted = false;
    dispatch_group_enter(group);
    [AVCaptureDevice requestAccessForMediaType:mediaType completionHandler:^(BOOL granted) {
        accessGranted = granted;
        dispatch_group_leave(group);
    }];
    dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(60.0 * NSEC_PER_SEC)));

    return accessGranted;
}

extern "C" bool avrs_avf_av_capture_device_configure(const void* deviceIn, Float64 fps) {
    @autoreleasepool {
        AVCaptureDevice* device = ((__bridge AVCaptureDevice*)deviceIn);
        AVCaptureDeviceFormat* best = nil;
        int32_t bestHeight = 0;
        AVFrameRateRange* bestFrameRateRange = nil;
        for (AVCaptureDeviceFormat* format in device.formats) {
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
            if (dims.height < bestHeight) {
                continue;
            }

            AVFrameRateRange* frameRateRange = nil;
            if (fps) {
                for (AVFrameRateRange* r in format.videoSupportedFrameRateRanges) {
                    if (r.minFrameRate - 0.0001 <= fps && r.maxFrameRate + 0.0001 >= fps) {
                        frameRateRange = r;
                    }
                }
            }

            if (!fps || frameRateRange) {
                best = format;
                bestHeight = dims.height;
                bestFrameRateRange = frameRateRange;
            }
        }

        if (!best || ![device lockForConfiguration:nil]) {
            return false;
        }
        @try {
            device.activeFormat = best;
            if (bestFrameRateRange) {
                device.activeVideoMinFrameDuration = bestFrameRateRange.minFrameDuration;
                device.activeVideoMaxFrameDuration = bestFrameRateRange.minFrameDuration;
            }
        } @catch(id anException) {
            NSLog(@"%@", anException);
            [device unlockForConfiguration];
            return false;
        }
        [device unlockForConfiguration];
        return true;
    }
}

extern "C" const void* avrs_avf_av_capture_device_input_from_device(const void* deviceIn, const void** errorOut) {
    @autoreleasepool {
        AVCaptureDevice* device = ((__bridge AVCaptureDevice*)deviceIn);
        NSError* error = nil;
        AVCaptureDeviceInput* deviceInput = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
        if (error) {
            *errorOut = (__bridge_retained void*)error;
            return nil;
        }
        return (__bridge_retained void*)deviceInput;
    }
}
