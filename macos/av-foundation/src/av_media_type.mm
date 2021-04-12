#import <AVFoundation/AVFoundation.h>

#if !__has_feature(objc_arc)
#error "ARC is off"
#endif

AVMediaType avrs_av_media_type_from_int(int id) {
    if (id == 0) {
        return AVMediaTypeAudio;
    } else {
        return AVMediaTypeVideo;
    }
}
