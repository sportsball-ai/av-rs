#include "lib.h"

x264_t* x264_encoder_open_wrapper(x264_param_t* param) {
    // x264_encoder_open is actually a macro
    return x264_encoder_open(param);
}
