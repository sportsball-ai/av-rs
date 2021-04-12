#import <AVFoundation/AVFoundation.h>

#if !__has_feature(objc_arc)
#error "ARC is off"
#endif

extern "C" void avrs_foundation_release_object(const void* obj) {
    @autoreleasepool {
        id m = (__bridge_transfer id)obj;
        m = nil;
    }
}
