#import <Foundation/Foundation.h>

extern "C" const char* avrs_nsstring_utf8_string(const void* strIn) {
    @autoreleasepool {
        NSString* str = ((__bridge NSString*)strIn);
        return [str UTF8String];
    }
}
