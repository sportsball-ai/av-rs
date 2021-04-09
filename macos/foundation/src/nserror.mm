#import <Foundation/Foundation.h>

extern "C" const void* avrs_nserror_localized_description(const void* errIn) {
    @autoreleasepool {
        NSError* err = ((__bridge NSError*)errIn);
        NSString* desc = [err localizedDescription];
        return (__bridge_retained void*)desc;
    }
}
