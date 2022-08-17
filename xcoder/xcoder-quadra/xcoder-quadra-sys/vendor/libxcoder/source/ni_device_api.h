/*******************************************************************************
 *
 * Copyright (C) 2022 NETINT Technologies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

/*!*****************************************************************************
*   \file   ni_device_api.h
*
*  \brief  Main NETINT device API header file
*           provides the ability to communicate with NI Quadra type hardware
*           transcoder devices
*
*******************************************************************************/

#pragma once


#ifdef __cplusplus
extern "C"
{
#endif

#include "ni_defs.h"

#define NI_DATA_FORMAT_VIDEO_PACKET 0
#define NI_DATA_FORMAT_YUV_FRAME    1
#define NI_DATA_FORMAT_Y_FRAME      2
#define NI_DATA_FORMAT_CB_FRAME     3
#define NI_DATA_FORMAT_CR_FRAME     4

#define NI_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

// the following are the default values from FFmpeg
#define AV_CODEC_DEFAULT_BITRATE 200 * 1000

#define NI_MAX_GOP_NUM 8

#define NI_MAX_REF_PIC 4

#define NI_MAX_VUI_SIZE 32

#define NI_MAX_TX_RETRIES 1000

#define NI_MAX_ENCODER_QUERY_RETRIES 5000

// Number of pixels for main stream resolutions
#define NI_NUM_OF_PIXELS_360P          (640*360)
#define NI_NUM_OF_PIXELS_720P          (1280*720)
#define NI_NUM_OF_PIXELS_1080P         (1920*1080)
#define NI_NUM_OF_PIXELS_1440P         (2560*1440)
#define NI_NUM_OF_PIXELS_4K            (3840*2160)
#define NI_NUM_OF_PIXELS_4K_2          (4096*2160)
#define NI_NUM_OF_PIXELS_8K            (7680*4320)

#define NI_MIN_RESOLUTION_WIDTH_JPEG 48
#define NI_MIN_RESOLUTION_HEIGHT_JPEG 48

#define NI_MIN_RESOLUTION_WIDTH 144
#define NI_MIN_RESOLUTION_HEIGHT 144

#define NI_MAX_RESOLUTION_WIDTH 8192
#define NI_MAX_RESOLUTION_HEIGHT 8192
#define NI_MAX_RESOLUTION_AREA 8192*8192

#define NI_MAX_RESOLUTION_LINESIZE (NI_MAX_RESOLUTION_WIDTH*2)

#define NI_FRAME_LITTLE_ENDIAN 0
#define NI_FRAME_BIG_ENDIAN 1

#define NI_INVALID_SESSION_ID 0xFFFF

#define NI_MAX_BITRATE 1000000000
#define NI_MIN_BITRATE 10000

#define NI_MAX_FRAMERATE 65535
#define NI_MAX_ASPECTRATIO 65535

/*Values below used for VPU resolution range checking*/
#define NI_MAX_WIDTH 8192
#define NI_MIN_WIDTH 144
#define NI_MAX_HEIGHT 8192
#define NI_MIN_HEIGHT 128

#define NI_2PASS_ENCODE_MIN_WIDTH 272
#define NI_2PASS_ENCODE_MIN_HEIGHT 256

#define NI_MULTICORE_ENCODE_MIN_WIDTH 256
#define NI_MULTICORE_ENCODE_MIN_HEIGHT 256

/*Values below used for parameter resolution range checking*/
#define NI_PARAM_MAX_WIDTH 8192
#define NI_PARAM_MIN_WIDTH 32
#define NI_PARAM_MAX_HEIGHT 8192
#define NI_PARAM_MIN_HEIGHT 32

#define NI_PARAM_JPEG_MIN_WIDTH 48
#define NI_PARAM_JPEG_MIN_HEIGHT 48

#define NI_PARAM_AV1_MIN_WIDTH 144
#define NI_PARAM_AV1_MIN_HEIGHT 128
#define NI_PARAM_AV1_MAX_WIDTH 4096
#define NI_PARAM_AV1_MAX_HEIGHT 4352
#define NI_PARAM_AV1_MAX_AREA (4096 * 2304)
#define NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT 8

#define NI_MAX_GOP_SIZE 8
#define NI_MIN_GOP_SIZE 1
#define NI_MAX_GOP_PRESET_IDX 9
#define NI_MIN_GOP_PRESET_IDX -1
#define NI_MAX_DECODING_REFRESH_TYPE 2
#define NI_MIN_DECODING_REFRESH_TYPE 0
#define NI_MAX_CU_SIZE_MODE 2
#define NI_MIN_CU_SIZE_MODE 0
#define NI_DEFAULT_CU_SIZE_MODE 7
#define NI_MAX_DYNAMIC_MERGE 1
#define NI_MIN_DYNAMIC_MERGE 0
#define NI_MAX_USE_RECOMMENDED_ENC_PARAMS 3
#define NI_MIN_USE_RECOMMENDED_ENC_PARAMS 0
#define NI_MAX_MAX_NUM_MERGE 3
#define NI_MIN_MAX_NUM_MERGE 0
#define NI_MAX_INTRA_QP 51
#define NI_MIN_INTRA_QP -1
#define NI_MAX_INTRA_QP_DELTA 51
#define NI_MIN_INTRA_QP_DELTA -51
#define NI_DEFAULT_INTRA_QP 22
#define NI_INTRA_QP_RANGE 25
#define NI_MIN_QP_DELTA (-25)
#define NI_MAX_QP_DELTA 25
#define NI_MAX_QP_INFO 63
#define NI_MAX_BIN 1
#define NI_MIN_BIN 0
#define NI_MAX_NUM_SESSIONS 32
#define NI_MIN_FRAME_SIZE 0
#define NI_MAX_FRAME_SIZE (7680*4320*3)

#define    RC_SUCCESS           true
#define    RC_ERROR             false

#define MAX_CHAR_IN_DEVICE_NAME 32

#define MAX_NUM_FRAMEPOOL_HWAVFRAME 128

/* These constants are the values used by the GC620 2D engine */
#define GC620_NV12          0x104
#define GC620_NV21          0x105
#define GC620_I420          0x103
#define GC620_P010_MSB      0x108
#define GC620_I010          0x10A
#define GC620_YUYV 0x100
#define GC620_UYVY 0x101
#define GC620_NV16 0x106
#define GC620_RGBA8888      0
#define GC620_BGRA8888      4
#define GC620_BGRX8888 5
#define GC620_ABGR8888      12
#define GC620_ARGB8888      15
#define GC620_RGB565        3
#define GC620_BGR565        11
#define GC620_B5G5R5X1      8
#define GC620_RGB888_PLANAR 0x10C

#define NI_ENABLE_AUD_FOR_GLOBAL_HEADER 2

// XCODER STATE
typedef enum
{
    NI_XCODER_IDLE_STATE = 0x00,         // Xcoder idle state
    NI_XCODER_OPEN_STATE = 0x01 << 1,    // Xcoder open state
    NI_XCODER_WRITE_STATE = 0x01 << 2,   // Xcoder write state
    NI_XCODER_READ_STATE = 0x01 << 3,    // Xcoder read state
    NI_XCODER_CLOSE_STATE = 0x01 << 4,   // Xcoder close state
    // Xcoder flush state, flush at the end of stream
    NI_XCODER_FLUSH_STATE = 0x01 << 5,
    // Xcoder inter flush state, flush during transcoding stream
    NI_XCODER_INTER_FLUSH_STATE = 0x01 << 6,
    NI_XCODER_READ_DESC_STATE = 0x01 << 7,   // Xcoder Read Desc state
    NI_XCODER_HWUP_STATE = 0x01 << 8,        // Xcoder HW upload state
    NI_XCODER_HWDL_STATE = 0x01 << 9,        // Xcoder HW download state
    // Other states, like init, alloc, etc.
    NI_XCODER_GENERAL_STATE = 0x01 << 10,
} ni_xcoder_state_t;

typedef enum
{
    NI_PIX_FMT_YUV420P = 0,     /* 8-bit YUV420 planar       */
    NI_PIX_FMT_YUV420P10LE = 1, /* 10-bit YUV420 planar      */
    NI_PIX_FMT_NV12 = 2,        /* 8-bit YUV420 semi-planar  */
    NI_PIX_FMT_P010LE = 3,      /* 10-bit YUV420 semi-planar */
    NI_PIX_FMT_RGBA = 4,        /* 32-bit RGBA packed        */
    NI_PIX_FMT_BGRA = 5,        /* 32-bit BGRA packed        */
    NI_PIX_FMT_ARGB = 6,        /* 32-bit ARGB packed        */
    NI_PIX_FMT_ABGR = 7,        /* 32-bit ABGR packed        */
    NI_PIX_FMT_BGR0 = 8,        /* 32-bit RGB packed         */
    NI_PIX_FMT_BGRP = 9,        /* 24bit RGB packed          */
    NI_PIX_FMT_NV16 = 10,       /* 8-bit YUV422 semi-planar  */
    NI_PIX_FMT_YUYV422 = 11,    /* 8-bit YUV422              */
    NI_PIX_FMT_UYVY422 = 12,    /* 8-bit YUV422              */
    NI_PIX_FMT_NONE = 13,       /* invalid format            */
} ni_pix_fmt_t;

#define NI_SCALER_FLAG_IO   0x0001  /* 0 = source frame, 1 = destination frame */
#define NI_SCALER_FLAG_PC   0x0002  /* 0 = single allocation, 1 = create pool */
#define NI_SCALER_FLAG_PA   0x0004  /* 0 = straight alpha, 1 = premultiplied alpha */
#define NI_SCALER_FLAG_P2 0x0008 /* 0 = normal allocation, 1 = P2P allocation */
#define NI_SCALER_FLAG_FCE  0x0010  /* 0 = no fill color, 1 = fill color enabled */

#define NI_MAX_KEEP_ALIVE_TIMEOUT 100
#define NI_MIN_KEEP_ALIVE_TIMEOUT 1
#define NI_DEFAULT_KEEP_ALIVE_TIMEOUT 3

// Picked from the xcoder firmware, commit e3b882e7
#define NI_VPU_CEIL(_data, _align)     (((_data)+(_align-1))&~(_align-1))
#define NI_VPU_ALIGN4(_x)              (((_x)+0x03)&~0x03)
#define NI_VPU_ALIGN8(_x)              (((_x)+0x07)&~0x07)
#define NI_VPU_ALIGN16(_x)             (((_x)+0x0f)&~0x0f)
#define NI_VPU_ALIGN32(_x)             (((_x)+0x1f)&~0x1f)
#define NI_VPU_ALIGN64(_x)             (((_x)+0x3f)&~0x3f)
#define NI_VPU_ALIGN128(_x)            (((_x)+0x7f)&~0x7f)
#define NI_VPU_ALIGN256(_x)            (((_x)+0xff)&~0xff)
#define NI_VPU_ALIGN512(_x)            (((_x)+0x1ff)&~0x1ff)
#define NI_VPU_ALIGN2048(_x)           (((_x)+0x7ff)&~0x7ff)
#define NI_VPU_ALIGN4096(_x)           (((_x)+0xfff)&~0xfff)
#define NI_VPU_ALIGN16384(_x)          (((_x)+0x3fff)&~0x3fff)

#if 1 // QUADRA_SEI_FMT
#pragma pack(1)
typedef struct _ni_sei_header {
  uint8_t status;
  uint16_t size;
  uint8_t type;
} ni_sei_header_t;
#pragma pack()
#else // QUADRA_SEI_FMT
typedef struct _ni_sei_user_data_entry
{
  uint32_t offset;
  uint32_t size;
} ni_sei_user_data_entry_t;
#endif // QUADRA_SEI_FMT

typedef enum
{
  NI_H265_USERDATA_FLAG_RESERVED_0 = 0,
  NI_H265_USERDATA_FLAG_RESERVED_1 = 1,
  NI_H265_USERDATA_FLAG_VUI = 2,
  NI_H265_USERDATA_FLAG_RESERVED_3 = 3,
  NI_H265_USERDATA_FLAG_PIC_TIMING = 4,
  NI_H265_USERDATA_FLAG_ITU_T_T35_PRE = 5, /* SEI Prefix: user_data_registered_itu_t_t35 */
  NI_H265_USERDATA_FLAG_UNREGISTERED_PRE = 6, /* SEI Prefix: user_data_unregistered */
  NI_H265_USERDATA_FLAG_ITU_T_T35_SUF = 7, /* SEI Suffix: user_data_registered_itu_t_t35 */
  NI_H265_USERDATA_FLAG_UNREGISTERED_SUF = 8, /* SEI Suffix: user_data_unregistered */
  NI_H265_USERDATA_FLAG_RESERVED_9 = 9, /* SEI RESERVED */
  NI_H265_USERDATA_FLAG_MASTERING_COLOR_VOL = 10, /* SEI Prefix: mastering_display_color_volume */
  NI_H265_USERDATA_FLAG_CHROMA_RESAMPLING_FILTER_HINT = 11, /* SEI Prefix: chroma_resampling_filter_hint */
  NI_H265_USERDATA_FLAG_KNEE_FUNCTION_INFO = 12, /* SEI Prefix: knee_function_info */
  NI_H265_USERDATA_FLAG_TONE_MAPPING_INFO  = 13,  /* SEI Prefix: tone_mapping_info */
  NI_H265_USER_DATA_FLAG_FILM_GRAIN_CHARACTERISTICS_INFO = 14,  /* SEI Prefix: film_grain_characteristics_info */
  NI_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO  = 15,  /* SEI Prefix: content_light_level_info */
  NI_H265_USER_DATA_FLAG_COLOUR_REMAPPING_INFO  = 16,  /* SEI Prefix: content_light_level_info */
  NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_1 = 28, /* SEI Prefix: additional user_data_registered_itu_t_t35 */
  NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_2 = 29, /* SEI Prefix: additional user_data_registered_itu_t_t35 */
  NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_1 = 30, /* SEI Suffix: additional user_data_registered_itu_t_t35 */
  NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_2 = 31, /* SEI Suffix: additional user_data_registered_itu_t_t35 */
} ni_h265_sei_user_data_type_t;

typedef enum
{
  PIC_TYPE_I = 0,                    /*!*< I picture */
  PIC_TYPE_P = 1,                    /*!*< P picture */
  PIC_TYPE_B = 2,                    /*!*< B picture (except VC1) */

  PIC_TYPE_CRA = 4,
  PIC_TYPE_IDR = 3,                  /*!*< Encoder IDR pic type */
  DECODER_PIC_TYPE_IDR = 5,          /*!*< Decoder-returned IDR pic type */
  PIC_TYPE_NIDR = 5,                  /*!*< H.264/H.265 IDR picture */
  PIC_TYPE_MAX                       /*!*< No Meaning */
} ni_pic_type_t;

#if 1 // QUADRA_SEI_FMT
// user data unreg and custom SEIs have variable length;
// non-custom SEIs will be dropped if buffer overflow;
// custom SEIs passthru is via SW and host memory constrained
#define NI_ENC_MAX_SEI_BUF_SIZE        NI_VPU_ALIGN64(1024) // sync with encoder

// PIC_TIMING and BUFFERING_PERIOD apply to encoder only, not needed from decoder
// For now, only the following will be returned from decoder:
// - T35 has 3 possible data types: CLOSE_CAPTION, HDR10PLUS, AFD
// - MASTERING_DISPLAY_COLOUR
// - CONTENT_LIGHT_LEVEL
// - USER_DATA_UNREGISTERED
// - CUSTOM_SEI
// Note: USER_DATA_UNREGISTERED may have arbitrary size but SEI buffer
// size is limited. The SEI buffer on decoder has the following format:
//       [byte0: #entries][entry1]..[entryN]
// up to (1 + sizeof(ni_sei_header_t) + NI_MAX_SEI_DATA) bytes maximum;
// where [entryX] := [ni_sei_header][payload]
//       [payload] of ni_sei_header.size: present only if ni_sei_header.status == 1
//         X in [1..N] with N <= #entries
//         N == #entries only if NI_MAX_SEI_DATA is large enough to store all #entries
// ex:
//     entryX header fits in the buffer during SEI extration, but payload can't fit in;
//            so only entryX header is stored but payload is drop with status set 0.
//     entryX header can't fit in the buffer (SEI buffer full); so extryX is not stored
//            in the SEI buffer (neither will any further SEIs)
//
// To maintain alignment, current implementation retrieves SEI data buffer from
// entry 1's payload onwards (skipping byte0, and entry 1's header),
// while #entries (same value as SEI buffer byte0) and entry 1's header are notified
// separately via metadata.
#define NI_MAX_SEI_DATA     (NI_ENC_MAX_SEI_BUF_SIZE) // sync with decoder_manager

#else // QUADRA_SEI_FMT
#define NI_MAX_SEI_ENTRIES      32
// 32 user_data_entry_t records + various SEI messages sizes
#define NI_MAX_SEI_DATA                                                        \
    NI_MAX_SEI_ENTRIES * sizeof(ni_sei_user_data_entry_t) +                    \
        NI_MAX_T35_CLOSE_CAPTION_SIZE +                                        \
        NI_MASTERING_DISPLAY_COLOUR_VOLUME_SIZE +                              \
        NI_CONTENT_LIGHT_LEVEL_INFO_SIZE + NI_MAX_T35_HDR10PLUS_SIZE +         \
        NI_MAX_T35_AFD_SIZE
#endif // QUADRA_SEI_FMT

#define NI_DEC_MAX_CC_BUF_SIZE  93      // max 31 CC entries of 3 bytes each

#define NI_CC_SEI_BYTE0    0xB5   // itu_t_t35_country_code =181 (North America)
#define NI_CC_SEI_BYTE1         0x00
#define NI_CC_SEI_BYTE2         0x31    // itu_t_t35_provider_code = 49
#define NI_CC_SEI_BYTE3         0x47    // ATSC_user_identifier = "GA94"
#define NI_CC_SEI_BYTE4         0x41
#define NI_CC_SEI_BYTE5         0x39
#define NI_CC_SEI_BYTE6         0x34

#define NI_HDR10P_SEI_BYTE0  0xB5 // itu_t_t35_country_code =181 (North America
#define NI_HDR10P_SEI_BYTE1     0x00
#define NI_HDR10P_SEI_BYTE2     0x3c    // itu_t_t35_provider_code = 0x003c
#define NI_HDR10P_SEI_BYTE3     0x00
#define NI_HDR10P_SEI_BYTE4     0x01    // u16 itu_t_t35_provider_oriented_code = 0x0001
#define NI_HDR10P_SEI_BYTE5     0x04 // u8 application_identifier = 0x04
#define NI_HDR10P_SEI_BYTE6     0x00 // u8 application version = 0x00

#define NI_CC_SEI_HDR_HEVC_LEN 18
#define NI_HDR10P_SEI_HDR_HEVC_LEN 9
#define NI_HDR10P_SEI_HDR_H264_LEN 8
#define NI_CC_SEI_HDR_H264_LEN 17
#define NI_CC_SEI_TRAILER_LEN  2
#define NI_RBSP_TRAILING_BITS_LEN 1

#define NI_MAX_NUM_AUX_DATA_PER_FRAME 16

// frame auxiliary data; mostly used for SEI data associated with frame
typedef enum _ni_frame_aux_data_type
{
    NI_FRAME_AUX_DATA_NONE = 0,

    // ATSC A53 Part 4 Closed Captions
    NI_FRAME_AUX_DATA_A53_CC,

    // HDR10 mastering display metadata associated with a video frame
    NI_FRAME_AUX_DATA_MASTERING_DISPLAY_METADATA,

    // HDR10 content light level (based on CTA-861.3). This payload contains
    // data in the form of ni_content_light_level_t struct.
    NI_FRAME_AUX_DATA_CONTENT_LIGHT_LEVEL,

    // HDR10+ dynamic metadata associated with a video frame. The payload is
    // a ni_dynamic_hdr_plus_t struct that contains information for color volume
    // transform - application 4 of SMPTE 2094-40:2016 standard.
    NI_FRAME_AUX_DATA_HDR_PLUS,

    // Regions of Interest, the payload is an array of ni_region_of_interest_t,
    // the number of array element is implied by:
    // ni_frame_aux_data.size / sizeof(ni_region_of_interest_t)
    NI_FRAME_AUX_DATA_REGIONS_OF_INTEREST,

    // NETINT: user data unregistered SEI data, which takes SEI payload type
    // USER_DATA_UNREGISTERED.
    // There will be no byte reordering.
    // Usually this payload would be: 16B UUID + other payload Bytes.
    NI_FRAME_AUX_DATA_UDU_SEI,

    // NETINT: custom SEI data, which takes SEI payload custom types.
    // There will be no byte reordering.
    // Usually this payload would be: 1B Custom SEI type + 16B UUID + other
    // payload Bytes.
    NI_FRAME_AUX_DATA_CUSTOM_SEI,

    // NETINT: custom bitrate adjustment, which takes int32_t type data as
    // payload that indicates the new target bitrate value.
    NI_FRAME_AUX_DATA_BITRATE,

    // NETINT: custom VUI adjustment, which is a struct of
    // ni_long_term_ref_t that specifies a frame's support of long term
    // reference frame.
    NI_FRAME_AUX_DATA_VUI,

    // NETINT: long term reference frame support, which is a struct of
    // ni_long_term_ref_t that specifies a frame's support of long term
    // reference frame.
    NI_FRAME_AUX_DATA_LONG_TERM_REF,

    // NETINT: long term reference interval adjustment, which takes int32_t
    // type data as payload that indicates the new long term reference interval
    // value.
    NI_FRAME_AUX_DATA_LTR_INTERVAL,

    // NETINT: frame reference invalidation, which takes int32_t type data
    // as payload that indicates the frame number after which all references
    // shall be invalidated.
    NI_FRAME_AUX_DATA_INVALID_REF_FRAME,

    // NETINT: custom framerate adjustment, which takes int32_t type data as
    // payload that indicates the new target framerate numerator and denominator values.
    NI_FRAME_AUX_DATA_FRAMERATE,
} ni_aux_data_type_t;

// rational number (pair of numerator and denominator)
typedef struct _ni_rational
{
    int num;   // numerator
    int den;   // denominator
} ni_rational_t;

// create an ni_rational_t
static inline ni_rational_t ni_make_q(int num, int den)
{
    ni_rational_t ret = {num, den};
    return ret;
}

// convert an ni_rational_t to a double
static inline double ni_q2d(ni_rational_t a)
{
    return a.num / (double)a.den;
}

// struct to hold auxiliary data for ni_frame_t
typedef struct _ni_aux_data
{
    ni_aux_data_type_t type;
    uint8_t *data;
    int size;
} ni_aux_data_t;

// struct describing a Region Of Interest (ROI)
typedef struct _ni_region_of_interest
{
    // self size: must be set to: sizeof(ni_region_of_interest_t)
    uint32_t self_size;

    // ROI rectangle: pixels from the frame's top edge to the top and bottom
    // edges of the rectangle, from the frame's left edge to the left and right
    // edges of the rectangle.
    int top;
    int bottom;
    int left;
    int right;

    // quantisation offset: [-1, +1], 0 means no quality change; < 0 value asks
    // for better quality (less quantisation), > 0 value asks for worse quality
    // (greater quantisation).
    ni_rational_t qoffset;
} ni_region_of_interest_t;

// struct describing VUI HRD support.
typedef struct _ni_vui_hrd
{
    // Indicates the presence of color info such as primaries, trc etc.
    int32_t colorDescPresent;

    // Indicates the chromaticity of RGB and white components of the
    // displayed image (See Table E.3 of H.265 spec)
    int32_t colorPrimaries;

    // The opto-electronic transfer characteristic of the source picture
    // (See Table E.4 of H.265 spec)
    int32_t colorTrc;

    // Method to represent brightness, luminance or luma and colour (e.g. RGB)
    int32_t colorSpace;

    // Luma sample aspect ratio width. With aspectRatioHeight, translates
    // into specific display ratios such as 16:9, 4:3, etc.
    int32_t aspectRatioWidth;

    // Luma sample aspect ratio height
    int32_t aspectRatioHeight;

    // Input video signal sample range [0,1].
    // 0 = Y range in [16..235] Cb,Cr in [16..240]. 1 = Y,Cb,Cr in [0..255]
    int32_t videoFullRange;
} ni_vui_hrd_t;

// struct describing long term reference frame support.
typedef struct _ni_long_term_ref
{
    // A flag for the current picture to be used as a long term reference
    // picture later at other pictures' encoding
    uint8_t use_cur_src_as_long_term_pic;

    // A flag to use a long term reference picture in DPB when encoding the
    // current picture
    uint8_t use_long_term_ref;
} ni_long_term_ref_t;

// struct describing framerate.
typedef struct _ni_framerate
{
    // framerate numerator
    int32_t framerate_num;

    // framerate denominator
    int32_t framerate_denom;
} ni_framerate_t;

typedef struct _ni_dec_win
{
    int16_t left;
    int16_t right;
    int16_t top;
    int16_t bottom;
} ni_dec_win_t;

/*!*
* \brief decoded payload format of H.265 VUI
*
*/
typedef struct _ni_dec_h265_vui_param
{
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;

    uint8_t video_signal_type_present_flag;
    int8_t video_format;

    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;

    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;

    uint8_t chroma_loc_info_present_flag;
    int8_t chroma_sample_loc_type_top_field;
    int8_t chroma_sample_loc_type_bottom_field;

    uint8_t neutral_chroma_indication_flag;

    uint8_t field_seq_flag;

    uint8_t frame_field_info_present_flag;
    uint8_t default_display_window_flag;
    uint8_t vui_timing_info_present_flag;
    uint8_t vui_poc_proportional_to_timing_flag;

    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;

    uint8_t vui_hrd_parameters_present_flag;
    uint8_t bitstream_restriction_flag;

    uint8_t tiles_fixed_structure_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint8_t restricted_ref_pic_lists_flag;
    int8_t min_spatial_segmentation_idc;
    int8_t max_bytes_per_pic_denom;
    int8_t max_bits_per_mincu_denom;

    int16_t vui_num_ticks_poc_diff_one_minus1;
    int8_t log2_max_mv_length_horizontal;
    int8_t log2_max_mv_length_vertical;

    ni_dec_win_t def_disp_win;

} ni_dec_h265_vui_param_t;

/*!*
* \brief decoded payload format of H.264 VUI
*
*/
typedef struct _ni_dec_h264_vui_param
{
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;

    uint8_t video_signal_type_present_flag;
    int8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;

    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t chroma_loc_info_present_flag;

    int8_t chroma_sample_loc_type_top_field;
    int8_t chroma_sample_loc_type_bottom_field;

    uint8_t vui_timing_info_present_flag;
    uint8_t fixed_frame_rate_flag;

    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;

    uint8_t nal_hrd_parameters_present_flag;
    uint8_t vcl_hrd_parameters_present_flag;
    uint8_t low_delay_hrd_flag;
    uint8_t pic_struct_present_flag;

    uint8_t bitstream_restriction_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    int8_t max_bytes_per_pic_denom;
    int8_t max_bits_per_mincu_denom;

    int8_t log2_max_mv_length_horizontal;
    int8_t log2_max_mv_length_vertical;
    int8_t max_num_reorder_frames;
    int8_t max_dec_frame_buffering;
} ni_dec_h264_vui_param_t;

/*!*
* \brief  encoder HEVC ROI custom map (1 CTU = 64bits)
*/
typedef union _ni_enc_hevc_roi_custom_map
{
  struct
  {
    uint32_t  ctu_force_mode  :  2; //[ 1: 0]
    uint32_t  ctu_coeff_drop  :  1; //[    2]
    uint32_t  reserved        :  5; //[ 7: 3]
    uint32_t  sub_ctu_qp_0    :  6; //[13: 8]
    uint32_t  sub_ctu_qp_1    :  6; //[19:14]
    uint32_t  sub_ctu_qp_2    :  6; //[25:20]
    uint32_t  sub_ctu_qp_3    :  6; //[31:26]

    uint32_t  lambda_sad_0    :  8; //[39:32]
    uint32_t  lambda_sad_1    :  8; //[47:40]
    uint32_t  lambda_sad_2    :  8; //[55:48]
    uint32_t  lambda_sad_3    :  8; //[63:56]
  } field;
} ni_enc_hevc_roi_custom_map_t;

/*!*
* \brief  encoder AVC ROI custom map (1 MB = 8bits)
*/
typedef union _ni_enc_avc_roi_custom_map
{
    struct
    {
        uint8_t mb_force_mode : 2;   // [ 1: 0]
        uint8_t mb_qp : 6;           // [ 7: 2]
    } field;
} ni_enc_avc_roi_custom_map_t;

/*!*
* \brief  encoder AVC ROI custom map (1 MB = 8bits)
*/
// QP/CU Control Information Format 1
typedef union _ni_enc_quad_roi_custom_map
{
    struct
    {
        uint8_t roiAbsQp_flag : 1;   // [ 0] (0: QP_delta, 1: abs_QP)
        uint8_t
            qp_info : 6;   // [ 6: 1] (QP_delta: -32 <= qp_info <= 31, QP_info =- QP_delta, abs_QP: 0 <= Qp_info <= 51, Qp_info = abs_QP)
        uint8_t
            ipcm_flag : 1;   // [ 7] (0: do not force IPCM mode, 1: force IPCM mode)
    } field;
} ni_enc_quad_roi_custom_map;
// QP/CU Control Information Format 2
/*
typedef union _ni_enc_quad_roi_custom_map
{
  struct
  {
    uint8_t  qp_info  :  6; // [ 5: 0] (QP_delta: -32 <= qp_info <= 31, QP_info =- QP_delta, abs_QP: 0 <= Qp_info <= 51, Qp_info = abs_QP)
    uint8_t  roiAbsQp_flag          :  1; // [ 6] (0: QP_delta, 1: abs_QP)
    uint8_t  skip_flag          :  1; // [ 7] (0: do not force skip mode, 1: force skip)
  } field;
} ni_enc_quad_roi_custom_map;
*/
/*!*
* \brief This is an enumeration for supported codec formats.
*/
typedef enum _ni_codec_format
{
    NI_CODEC_FORMAT_H264 = 0,
    NI_CODEC_FORMAT_H265 = 1,
    NI_CODEC_FORMAT_VP9 = 2,
    NI_CODEC_FORMAT_JPEG = 3,
    NI_CODEC_FORMAT_AV1 = 4
} ni_codec_format_t;

typedef enum _ni_pixel_planar_format
{
  NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR = 0,
  NI_PIXEL_PLANAR_FORMAT_PLANAR = 1
} ni_pixel_planar_format;

typedef enum _ni_dec_crop_mode
{
  NI_DEC_CROP_MODE_DISABLE = 0,
  NI_DEC_CROP_MODE_AUTO = 1,
  NI_DEC_CROP_MODE_MANUAL = 2
} ni_dec_crop_mode;


/*!*
* \brief This is an enumeration for hw actions
*/
typedef enum ni_codec_hw_actions
{
  NI_CODEC_HW_NONE = 0,
  NI_CODEC_HW_ENABLE = (1 << 0),
  NI_CODEC_HW_DOWNLOAD = (1 << 1),
  NI_CODEC_HW_UPLOAD = (1 << 2),
  NI_CODEC_HW_RSVD = (1 << 3),
  NI_CODEC_HW_PAYLOAD_OFFSET = 4
} ni_codec_hw_actions_t;

/*!*
* \brief This is an enumeration for encoder parameter change.
*/
typedef enum _ni_param_change_flags
{
    // COMMON parameters which can be changed frame by frame.
    NI_SET_CHANGE_PARAM_PPS = (1 << 0),
    //NI_SET_CHANGE_PARAM_INTRA_PARAM = (1 << 1), // not required by customer
    NI_SET_CHANGE_PARAM_RC_TARGET_RATE = (1 << 8),
    //NI_SET_CHANGE_PARAM_RC = (1 << 9), // not required by customer
    //NI_SET_CHANGE_PARAM_RC_MIN_MAX_QP = (1 << 10), // not required by customer
    NI_SET_CHANGE_PARAM_RC_BIT_RATIO_LAYER = (1 << 11),
    NI_SET_CHANGE_PARAM_INDEPEND_SLICE = (1 << 16),
    NI_SET_CHANGE_PARAM_DEPEND_SLICE = (1 << 17),
    NI_SET_CHANGE_PARAM_RDO = (1 << 18),
    NI_SET_CHANGE_PARAM_NR = (1 << 19),
    NI_SET_CHANGE_PARAM_BG = (1 << 20),
    NI_SET_CHANGE_PARAM_CUSTOM_MD = (1 << 21),
    NI_SET_CHANGE_PARAM_CUSTOM_LAMBDA = (1 << 22),
    NI_SET_CHANGE_PARAM_RC2 = (1 << 23),
    NI_SET_CHANGE_PARAM_VUI_HRD_PARAM = (1 << 24),
    NI_SET_CHANGE_PARAM_INVALID_REF_FRAME = (1 << 25),
    NI_SET_CHANGE_PARAM_LTR_INTERVAL = (1 << 26),
    NI_SET_CHANGE_PARAM_RC_FRAMERATE = (1 << 27),

} ni_param_change_flags_t;

/**
* @brief This is a data structure for encoding parameters that have changed.
*/
typedef struct _ni_encoder_change_params_t
{
  uint32_t enable_option;

  // NI_SET_CHANGE_PARAM_RC_TARGET_RATE
  int32_t bitRate;                        /**< A target bitrate when separateBitrateEnable is 0 */


  // NI_SET_CHANGE_PARAM_RC
// (rcEnable, cuLevelRc, bitAllocMode, RoiEnable, RcInitQp can't be changed while encoding)
  int32_t hvsQPEnable;                    /**< It enables CU QP adjustment for subjective quality enhancement. */
  int32_t hvsQpScale;                     /**< QP scaling factor for CU QP adjustment when hvcQpenable is 1. */
  int32_t vbvBufferSize;                  /**< Specifies the size of the VBV buffer in msec (10 ~ 3000). For example, 3000 should be set for 3 seconds. This value is valid when rcEnable is 1. VBV buffer size in bits is EncBitrate * VbvBufferSize / 1000. */
  int32_t mbLevelRcEnable;                /**< (for H.264 encoder) */

  // NI_SET_CHANGE_PARAM_RC_MIN_MAX_QP
  int32_t minQpI;                         /**< A minimum QP of I picture for rate control */
  int32_t maxQpI;                         /**< A maximum QP of I picture for rate control */

  int32_t maxDeltaQp;                     /**< A maximum delta QP for rate control */
#ifdef QUADRA
  int32_t minQpPB;                         /**< A minimum QP of P/B picture for rate control */
  int32_t maxQpPB;                         /**< A maximum QP of P/B picture for rate control */
#else
  int32_t minQpP;                         /**< A minimum QP of P picture for rate control */
  int32_t minQpB;                         /**< A minimum QP of B picture for rate control */
  int32_t maxQpP;                         /**< A maximum QP of P picture for rate control */
  int32_t maxQpB;                         /**< A maximum QP of B picture for rate control */
#endif

  // NI_SET_CHANGE_PARAM_INTRA_PARAM
  int32_t intraQP;                        /**< A quantization parameter of intra picture */
  int32_t intraPeriod;                    /**< A period of intra picture in GOP size */
  int32_t repeatHeaders; /**< When enabled, encoder repeats the VPS/SPS/PPS headers on I-frames */

#ifdef QUADRA
  // NI_SET_CHANGE_PARAM_VUI_HRD_PARAM
  uint8_t colorDescPresent;               /**< Indicates the presence of color info such as primaries, trc etc. in VUI */
  uint8_t colorPrimaries;                 /**< Indicates the chromaticity of RGB and white components of the displayed image (See Table E.3 of H.265 spec) */
  uint8_t colorTrc;                       /**< The opto-electronic transfer characteristic of the source picture (See Table E.4 of H.265 spec) */
  uint8_t colorSpace;                     /**< Method to represent brightness, luminance or luma and colour (e.g. RGB) */
  uint16_t aspectRatioWidth;              /**< Luma sample aspect ratio width. With aspectRatioHeight, translates into specific display ratios such as 16:9, 4:3, etc. */
  uint16_t aspectRatioHeight;             /**< Luma sample aspect ratio height */
  uint8_t videoFullRange;                 /**< Input video signal sample range [0,1]. 0 = Y range in [16..235] Cb,Cr in [16..240]. 1 = Y,Cb,Cr in [0..255] */

  // RESERVED FOR FUTURE USE
  uint8_t reserved[15];

  // NI_SET_CHANGE_PARAM_INVALID_REF_FRAME
  int32_t invalidFrameNum;

  // NI_SET_CHANGE_PARAM_LTR_INTERVAL
  int32_t ltrInterval;

  // NI_SET_CHANGE_PARAM_RC_FRAMERATE
  int32_t frameRateNum;
  int32_t frameRateDenom;
#else
  int32_t reserved[8];
#endif
} ni_encoder_change_params_t;

/*!*
* \brief decoded payload format of HDR SEI mastering display colour volume
*
*/
typedef struct _ni_dec_mastering_display_colour_volume_bytes
{
  uint16_t   display_primaries[3][2];
  uint16_t   white_point_x;
  uint16_t   white_point_y;
  uint32_t   max_display_mastering_luminance;
  uint32_t   min_display_mastering_luminance;
} ni_dec_mastering_display_colour_volume_bytes_t;

/*!*
* \brief payload format of HDR SEI content light level info
*
*/
typedef struct _ni_content_light_level_info_bytes
{
  uint16_t max_content_light_level;
  uint16_t max_pic_average_light_level;
} ni_content_light_level_info_bytes_t;

/*!*
* \brief encoded payload format of HDR SEI mastering display colour volume
*
*/
typedef struct _ni_enc_mastering_display_colour_volume
{
  uint16_t   display_primaries[3][2];
  uint16_t   white_point_x;
  uint16_t   white_point_y;
  uint32_t   max_display_mastering_luminance;
  uint32_t   min_display_mastering_luminance;
} ni_enc_mastering_display_colour_volume_t;

/*!*
 * \brief This is an enumeration for illustrating the custom SEI locations.
 */
typedef enum _ni_custom_sei_location
{
  NI_CUSTOM_SEI_LOC_BEFORE_VCL = 0,
  NI_CUSTOM_SEI_LOC_AFTER_VCL = 1
} ni_custom_sei_location_t;

/*!*
 * \brief custom sei payload passthrough
 */
typedef struct _ni_custom_sei
{
  uint8_t type;
  ni_custom_sei_location_t location;
  uint32_t size;
  uint8_t data[NI_MAX_SEI_DATA];
} ni_custom_sei_t;

typedef struct _ni_custom_sei_set
{
  ni_custom_sei_t custom_sei[NI_MAX_CUSTOM_SEI_CNT];
  int count;
} ni_custom_sei_set_t;

/*!*
* \brief hardware capability type
*/
typedef struct _ni_hw_capability
{
  uint8_t hw_id;
  uint8_t max_number_of_contexts;
  uint8_t max_4k_fps;
  uint8_t codec_format;
  uint8_t codec_type;
  uint16_t max_video_width;
  uint16_t max_video_height;
  uint16_t min_video_width;
  uint16_t min_video_height;
  uint8_t video_profile;
  uint8_t video_level;
  uint8_t reserved; // 16B alignment. Unnecessary?
} ni_hw_capability_t;

/*!*
* \brief  device capability type
*/
typedef struct _ni_device_capability
{
  uint8_t device_is_xcoder;
  uint8_t hw_elements_cnt;
  uint8_t xcoder_devices_cnt;
  uint8_t xcoder_cnt[NI_DEVICE_TYPE_XCODER_MAX];
  ni_hw_capability_t xcoder_devices[NI_MAX_DEVICES_PER_HW_INSTANCE];

  uint8_t serial_number[20];
  uint8_t model_number[40];

  uint8_t fw_rev[8]; // space right filled ASCII array, not a string
  uint8_t fw_branch_name[256];
  uint8_t fw_commit_time[26];
  uint8_t fw_commit_hash[41];
  uint8_t fw_build_time[26];
  uint8_t fw_build_id[256];
} ni_device_capability_t;

/*!*
* \brief Session running state type.
*/
typedef enum _ni_session_run_state
{
  SESSION_RUN_STATE_NORMAL = 0,
  SESSION_RUN_STATE_SEQ_CHANGE_DRAINING = 1,
  SESSION_RUN_STATE_SEQ_CHANGE_OPENING = 2,
  SESSION_RUN_STATE_RESETTING = 3,
} ni_session_run_state_t;

typedef struct _ni_context_query
{
  uint32_t context_id : 8;     //07:00  SW Instance ID (0 to Max number of instances)
  uint32_t context_status : 8; //15:08  Instance Status (0-Idle, 1-Active)
  uint32_t
      codec_format : 8;   //23:16  Codec Format (0-H264, 1-H265, 2-VP9, 3-JPEG, 4-AV1)
  uint32_t video_width : 16;   //39:24  Video Width (0 to Max Width)
  uint32_t video_height : 16;  //55:40  Video Height (0 to Max Height)
  uint32_t fps : 8;            //63:56  FPS (0 to 255)
  uint32_t reserved : 8;       //Alignment
} ni_context_query_t;

typedef struct _ni_load_query
{
  uint32_t current_load;
  uint32_t fw_model_load;
  uint32_t total_contexts;
  uint32_t fw_video_mem_usage;
  uint32_t fw_share_mem_usage;
  uint32_t fw_p2p_mem_usage;
  uint32_t active_hwuploaders;
  ni_context_query_t context_status[NI_MAX_CONTEXTS_PER_HW_INSTANCE];
} ni_load_query_t;

typedef struct _ni_thread_arg_struct_t
{
  int hw_id;                              // Codec ID
  uint32_t session_id;                    // session id
  uint64_t session_timestamp;             // Session Start Timestamp
  bool close_thread;                      // a flag that the keep alive thread is closed or need to be closed
  uint32_t device_type;                   // Device Type, Either NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
  ni_device_handle_t device_handle;       // block device handler
  ni_event_handle_t thread_event_handle;  // only for Windows asynchronous read and write
  void *p_buffer;                         // only be used when regular-io.
  uint32_t keep_alive_timeout;            // keep alive timeout setting
} ni_thread_arg_struct_t;

typedef struct _ni_buf_t
{
  void* buf;
  struct _ni_buf_pool_t* pool;
  struct  _ni_buf_t *p_prev;
  struct  _ni_buf_t *p_next;
  struct  _ni_buf_t *p_previous_buffer;
  struct  _ni_buf_t *p_next_buffer;
} ni_buf_t;

typedef struct _ni_buf_pool_t
{
    ni_pthread_mutex_t mutex;
    uint32_t number_of_buffers;
    uint32_t buf_size;
    ni_buf_t *p_free_head;
    ni_buf_t *p_free_tail;
    ni_buf_t *p_used_head;
    ni_buf_t *p_used_tail;
} ni_buf_pool_t;

typedef struct _ni_queue_node_t
{
    uint64_t timestamp;
    uint64_t frame_info;
    time_t   checkout_timestamp;
    struct _ni_queue_node_t *p_prev;
    struct _ni_queue_node_t *p_next;
    struct _ni_queue_node_t *p_previous_buffer;
    struct _ni_queue_node_t *p_next_buffer;
} ni_queue_node_t;

typedef struct _ni_queue_buffer_pool_t
{
    uint32_t number_of_buffers; // total number of buffers
    ni_queue_node_t *p_free_head;
    ni_queue_node_t *p_free_tail;
    ni_queue_node_t *p_used_head;
    ni_queue_node_t *p_used_tail;
} ni_queue_buffer_pool_t;

typedef struct _ni_queue_t
{
    char name[32];
    uint32_t count;
    ni_queue_node_t *p_first;
    ni_queue_node_t *p_last;
} ni_queue_t;

typedef struct _ni_timestamp_table_t
{
    ni_queue_t list;
} ni_timestamp_table_t;

typedef struct _ni_session_context
{
    /*! MEASURE_LATENCY queue */
    /* frame_time_q is pointer to ni_lat_meas_q_t but reserved as void pointer
       here as ni_lat_meas_q_t is part of private API */
    void *frame_time_q;
    uint64_t prev_read_frame_time;

    /*! close-caption/HDR10+ header and trailer template, used for encoder */
    uint8_t itu_t_t35_cc_sei_hdr_hevc[NI_CC_SEI_HDR_HEVC_LEN];
    uint8_t itu_t_t35_cc_sei_hdr_h264[NI_CC_SEI_HDR_H264_LEN];
    uint8_t itu_t_t35_hdr10p_sei_hdr_hevc[NI_HDR10P_SEI_HDR_HEVC_LEN];
    uint8_t itu_t_t35_hdr10p_sei_hdr_h264[NI_HDR10P_SEI_HDR_H264_LEN];

    uint8_t sei_trailer[NI_CC_SEI_TRAILER_LEN];

    /*! storage of HDR SEI, updated when received from decoder, to be applied
    to I frame at encoding */
    int sei_hdr_content_light_level_info_len;
    int light_level_data_len;
    uint8_t ui8_light_level_data[5];
    int sei_hdr_mastering_display_color_vol_len;
    int mdcv_max_min_lum_data_len;
    uint8_t ui8_mdcv_max_min_lum_data[9];
    void *p_master_display_meta_data;
    uint8_t preferred_characteristics_data;

    /*! frame pts calculation: for decoder */
    int is_first_frame;
    int64_t last_pts;
    int64_t last_dts;
    int64_t enc_pts_list[NI_FIFO_SZ];
    int64_t enc_pts_r_idx;
    int64_t enc_pts_w_idx;
    int pts_correction_num_faulty_dts;
    int64_t pts_correction_last_dts;
    int pts_correction_num_faulty_pts;
    int64_t pts_correction_last_pts;
    NI_DEPRECATED int64_t start_dts_offset;

    /* store pts values to create an accurate pts offset */
    int64_t pts_offsets[NI_FIFO_SZ];
    int pkt_index;
    uint64_t pkt_offsets_index[NI_FIFO_SZ];
    uint64_t pkt_offsets_index_min[NI_FIFO_SZ];
    ni_custom_sei_set_t *pkt_custom_sei_set[NI_FIFO_SZ];

    /*! if session is on a decoder handling incoming pkt 512-aligned */
    int is_dec_pkt_512_aligned;

    /*! Device Card ID */
    ni_device_handle_t device_handle;

    /*! block device fd */
    ni_device_handle_t blk_io_handle;

    /*! Sender information*/
    ni_device_handle_t sender_handle;
    ni_device_handle_t auto_dl_handle;
    uint8_t is_auto_dl;

    uint32_t template_config_id;
    void *p_session_config;

    /*! Max Linux NVME IO Size */
    uint32_t max_nvme_io_size;
    /*! Codec ID */
    int hw_id;
    /*! Session ID */
    uint32_t session_id;
    /*! Session Start Timestamp */
    uint64_t session_timestamp;
    /*! Device Type, Either NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER */
    uint32_t device_type;
    /*! Device Type, Either NI_CODEC_FORMAT_H264 or NI_CODEC_FORMAT_H265 */
    uint32_t codec_format;
    /*! the device name that opened*/
    char dev_xcoder_name[MAX_CHAR_IN_DEVICE_NAME];
    /*! the block name that opened */
    char blk_xcoder_name[MAX_CHAR_IN_DEVICE_NAME];

    int src_bit_depth;      // 8 or 10 bits/pixel formats, default 8
    int src_endian;         // encoding 0: little endian (default) 1: big
    int bit_depth_factor;   // for YUV buffer allocation
    // for encoder roi metadata
    uint32_t roi_len;
    uint32_t roi_avg_qp;

    /*! Context Query */
    ni_load_query_t load_query;

    /*! Leftover Buffer */
    uint8_t *p_leftover;
    int prev_size;
    uint32_t sent_size;

    /*! for decoder: buffer for stream header */
    uint8_t *p_hdr_buf;
    uint8_t hdr_buf_size;

    /*! PTS Table */
    ni_timestamp_table_t *pts_table;

    /*! DTS Queue */
    ni_timestamp_table_t *dts_queue;

    /*! keep alive timeout */
    uint32_t keep_alive_timeout;

    /*! Other */
    int status;
    int key_frame_type;

    void *p_dump[2];
    char param_err_msg[512];

    int keyframe_factor;
    uint64_t frame_num;
    uint64_t pkt_num;
    int rc_error_count;

    uint32_t hwd_Frame_Idx;
    uint32_t hwd_src_cpu;
    uint32_t hwd_minor_offset;
    uint32_t hwd_chunk_offset;
    uint32_t hwd_Inst_ID;

    // frame forcing: for encoding
    int force_frame_type;

    uint32_t ready_to_close;   //flag to indicate we are ready to close session

    // session running state
    ni_session_run_state_t session_run_state;
    //Current video width. this is used to do sequence change
    uint32_t active_video_width;
    //Current video height ,this is used to do sequence change
    uint32_t active_video_height;
    //Actual video width (without stride + cropped)
    uint32_t actual_video_width;
    // Used to track sequence changes that require bigger bitstream buffers
    uint32_t biggest_bitstream_buffer_allocated;
    ni_pthread_t keep_alive_thread;
    ni_thread_arg_struct_t *keep_alive_thread_args;
    ni_queue_buffer_pool_t *buffer_pool;
    ni_buf_pool_t *dec_fme_buf_pool;

    // original resolution this stream started with, this is used by encoder sequence change
    int ori_width, ori_height, ori_bit_depth_factor, ori_pix_fmt;

    // a muxter for Xcoder API, to keep the thread-safety.
    ni_pthread_mutex_t *xcoder_mutex;

    // Xcoder running state
    uint32_t xcoder_state;

    // only be used when regular-io
    void *p_all_zero_buf;   //This is for sos, eos, flush and keep alive request

    // these two event handle are only for Windows asynchronous read and write now
    ni_event_handle_t event_handle;
    ni_event_handle_t thread_event_handle;

    // ROI data
    int roi_side_data_size;
    // last passed in ni_region_of_interest_t
    ni_region_of_interest_t *av_rois;
    int nb_rois;
    ni_enc_quad_roi_custom_map *roi_map;   // actual AVC/HEVC QP map

    // encoder reconfig parameters
    ni_encoder_change_params_t *enc_change_params;
    // decoder lowDelay mode for All I packets or IPPP packets
    int decoder_low_delay;

    // wrapper API request data, to be sent out once with the next input frame
    // to encoder
    int32_t target_bitrate;   // A target bitrate to reconfig, -1 if inactive
    int force_idr_frame;      // force IDR frame type
    ni_long_term_ref_t ltr_to_set;   // LTR to be set
    int32_t ltr_interval;            // LTR interval to set
    int32_t ltr_frame_ref_invalid;   // frame after which all ref to be invalid
    ni_framerate_t framerate;        // framerate to reconfig, 0 if inactive
    ni_vui_hrd_t vui;                // vui to reconfig

    // path of decoder input pkt saving folder
    char stream_dir_name[256];
    int hw_action;
    uint32_t scaler_operation;

    // some parameters for decoder session
    // int hw_frame_extra_buffer; e.g. 3
    int enable_user_data_sei_passthru;
    int burst_control;
    int pixel_format;
    int32_t isP2P;
    int netint_fd;
    unsigned short domain;
    unsigned short bus;
    unsigned short dev;
    unsigned short fn;

    // the FW API version of device that opened
    uint8_t fw_rev[8];
    uint8_t ddr_config;

    ///Params used in VFR mode Start///
    /*! Numerator and denominator of frame rate,
     * used framerate change for VFR mode*/
    uint32_t prev_fps;
    /*! The last setting bitrate in the VFR mode*/
    uint64_t prev_pts;
    uint32_t last_change_framenum;
    uint32_t fps_change_detect_count;
    ///Params used in VFR mode Done///

    uint32_t meta_size;
    int64_t last_dts_interval;
    int pic_reorder_delay;

    // flags_array to save packet flags
    int flags_array[NI_FIFO_SZ];

    // for decoder: store currently returned decoded frame's pkt offset
    uint64_t frame_pkt_offset;
} ni_session_context_t;

typedef struct _ni_split_context_t
{
    int enabled;
    int w[3];
    int h[3];
    int f[3];     //planar format
    int f8b[3];   //forced 8bit
                  //int crop_meta_data_rltb[3][4]; //crop rectangle
} ni_split_context_t;

/*!*
* \brief This is an enumeration for encoder reconfiguration test settings
*/
typedef enum _ni_reconfig
{
    XCODER_TEST_RECONF_OFF = 0,
    XCODER_TEST_RECONF_BR = 1,
    //XCODER_TEST_RECONF_INTRAPRD = 2, // not required by customer
    XCODER_TEST_RECONF_VUI_HRD = 3,
    XCODER_TEST_RECONF_LONG_TERM_REF = 4,
//XCODER_TEST_RECONF_RC = 5,   // // not required by customer
//XCODER_TEST_RECONF_RC_MIN_MAX_QP = 6, // // not required by customer
#ifdef QUADRA
    XCODER_TEST_RECONF_LTR_INTERVAL = 7,
    XCODER_TEST_INVALID_REF_FRAME = 8,
    XCODER_TEST_RECONF_FRAMERATE = 9,
    XCODER_TEST_FORCE_IDR_FRAME = 100,   // force IDR through libxcoder API
    XCODER_TEST_RECONF_BR_API = 101,     // reconfig BR through libxcoder API
    XCODER_TEST_RECONF_VUI_HRD_API = 103,    // reconfig VUI through libxcoder API
    XCODER_TEST_RECONF_LTR_API = 104,    // reconfig LTR through libxcoder API
    XCODER_TEST_RECONF_LTR_INTERVAL_API = 107,   // reconf LTR interval thru API
    XCODER_TEST_INVALID_REF_FRAME_API = 108,   // invalidate ref frame thru API
    XCODER_TEST_RECONF_FRAMERATE_API =
        109,   // reconfig framerate through libxcoder API
    XCODER_TEST_RECONF_END = 110,
#endif
} ni_reconfig_t;

typedef enum _ni_ai_buffer_format_e
{
    /* A float type of buffer data */
    NI_AI_BUFFER_FORMAT_FP32 = 0,
    /* A half float type of buffer data */
    NI_AI_BUFFER_FORMAT_FP16 = 1,
    /* A 8 bit unsigned integer type of buffer data */
    NI_AI_BUFFER_FORMAT_UINT8 = 2,
    /* A 8 bit signed integer type of buffer data */
    NI_AI_BUFFER_FORMAT_INT8 = 3,
    /* A 16 bit unsigned integer type of buffer data */
    NI_AI_BUFFER_FORMAT_UINT16 = 4,
    /* A 16 signed integer type of buffer data */
    NI_AI_BUFFER_FORMAT_INT16 = 5,
    /* A char type of data */
    NI_AI_BUFFER_FORMAT_CHAR = 6,
    /* A bfloat 16 type of data */
    NI_AI_BUFFER_FORMAT_BFP16 = 7,
    /* A 32 bit integer type of data */
    NI_AI_BUFFER_FORMAT_INT32 = 8,
    /* A 32 bit unsigned signed integer type of buffer */
    NI_AI_BUFFER_FORMAT_UINT32 = 9,
    /* A 64 bit signed integer type of data */
    NI_AI_BUFFER_FORMAT_INT64 = 10,
    /* A 64 bit unsigned integer type of data */
    NI_AI_BUFFER_FORMAT_UINT64 = 11,
    /* A 64 bit float type of buffer data */
    NI_AI_BUFFER_FORMAT_FP64 = 12,
} ni_ai_buffer_format_e;

typedef enum _ni_ai_buffer_quantize_format_e
{
    /* Not quantized format */
    NI_AI_BUFFER_QUANTIZE_NONE = 0,
    /* A quantization data type which specifies the fixed point position for whole tensor. */
    NI_AI_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT = 1,
    /* A quantization data type which has scale value and zero point to match with TF and
      Android NN API for whole tensor. */
    NI_AI_BUFFER_QUANTIZE_TF_ASYMM = 2,
    /* A max vaule support quantize format */
    NI_AI_BUFFER_QUANTIZE_MAX,
} ni_ai_buffer_quantize_format_e;

#define NI_MAX_NETWORK_INPUT_NUM 4
#define NI_MAX_NETWORK_OUTPUT_NUM 4

typedef struct _ni_network_layer_params_t
{
    uint32_t num_of_dims; /* The number of dimensions specified in *sizes */
    uint32_t sizes[6];    /* The pointer to an array of dimension */
    int32_t
        data_format; /* Data format for the tensor, see ni_ai_buffer_format_e */
    int32_t
        quant_format; /* Quantized format see ni_ai_buffer_quantize_format_e */
    union
    {
        struct
        {
            int32_t
                fixed_point_pos; /* Specifies the fixed point position when the input element type is int16, if 0 calculations are performed in integer math */
        } dfp;

        struct
        {
            float scale;       /* Scale value for the quantized value */
            int32_t zeroPoint; /* A 32 bit integer, in range [0, 255] */
        } affine;
    } quant_data; /* The union of quantization information */
    /* The type of this buffer memory. */
    uint32_t memory_type;
} ni_network_layer_params_t;

typedef struct _ni_network_layer_info
{
    ni_network_layer_params_t in_param[NI_MAX_NETWORK_INPUT_NUM];
    ni_network_layer_params_t out_param[NI_MAX_NETWORK_OUTPUT_NUM];
} ni_network_layer_info_t;

typedef struct _ni_network_data
{
    uint32_t input_num;
    uint32_t output_num;
    ni_network_layer_info_t linfo;
    struct
    {
        int32_t
            offset; /* point to each input layer start offset from p_frame */
    } inset[NI_MAX_NETWORK_INPUT_NUM];
    struct
    {
        int32_t
            offset; /* point to each output layer start offset from p_packet */
    } outset[NI_MAX_NETWORK_OUTPUT_NUM];
} ni_network_data_t;

#ifdef QUADRA
#define NI_ENC_GOP_PARAMS_G0_POC_OFFSET     "g0pocOffset"
#define NI_ENC_GOP_PARAMS_G0_QP_OFFSET         "g0QpOffset"
//#define NI_ENC_GOP_PARAMS_G0_QP_FACTOR         "g0QpFactor"
#define NI_ENC_GOP_PARAMS_G0_TEMPORAL_ID    "g0temporalId"
#define NI_ENC_GOP_PARAMS_G0_PIC_TYPE       "g0picType"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PICS   "g0numRefPics"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0 "g0refPic0"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0_USED "g0refPic0Used"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1 "g0refPic1"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1_USED "g0refPic1Used"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2 "g0refPic2"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2_USED "g0refPic2Used"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3 "g0refPic3"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3_USED "g0refPic3Used"

#define NI_ENC_GOP_PARAMS_G1_POC_OFFSET     "g1pocOffset"
#define NI_ENC_GOP_PARAMS_G1_QP_OFFSET         "g1QpOffset"
//#define NI_ENC_GOP_PARAMS_G1_QP_FACTOR         "g1QpFactor"
#define NI_ENC_GOP_PARAMS_G1_TEMPORAL_ID    "g1temporalId"
#define NI_ENC_GOP_PARAMS_G1_PIC_TYPE       "g1picType"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PICS   "g1numRefPics"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0 "g1refPic0"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0_USED "g1refPic0Used"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1 "g1refPic1"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1_USED "g1refPic1Used"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2 "g1refPic2"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2_USED "g1refPic2Used"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3 "g1refPic3"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3_USED "g1refPic3Used"

#define NI_ENC_GOP_PARAMS_G2_POC_OFFSET     "g2pocOffset"
#define NI_ENC_GOP_PARAMS_G2_QP_OFFSET         "g2QpOffset"
//#define NI_ENC_GOP_PARAMS_G2_QP_FACTOR         "g2QpFactor"
#define NI_ENC_GOP_PARAMS_G2_TEMPORAL_ID    "g2temporalId"
#define NI_ENC_GOP_PARAMS_G2_PIC_TYPE       "g2picType"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PICS   "g2numRefPics"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0 "g2refPic0"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0_USED "g2refPic0Used"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1 "g2refPic1"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1_USED "g2refPic1Used"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2 "g2refPic2"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2_USED "g2refPic2Used"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3 "g2refPic3"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3_USED "g2refPic3Used"

#define NI_ENC_GOP_PARAMS_G3_POC_OFFSET     "g3pocOffset"
#define NI_ENC_GOP_PARAMS_G3_QP_OFFSET         "g3QpOffset"
//#define NI_ENC_GOP_PARAMS_G3_QP_FACTOR         "g3QpFactor"
#define NI_ENC_GOP_PARAMS_G3_TEMPORAL_ID    "g3temporalId"
#define NI_ENC_GOP_PARAMS_G3_PIC_TYPE       "g3picType"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PICS   "g3numRefPics"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0 "g3refPic0"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0_USED "g3refPic0Used"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1 "g3refPic1"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1_USED "g3refPic1Used"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2 "g3refPic2"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2_USED "g3refPic2Used"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3 "g3refPic3"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3_USED "g3refPic3Used"

#define NI_ENC_GOP_PARAMS_G4_POC_OFFSET     "g4pocOffset"
#define NI_ENC_GOP_PARAMS_G4_QP_OFFSET         "g4QpOffset"
//#define NI_ENC_GOP_PARAMS_G4_QP_FACTOR         "g4QpFactor"
#define NI_ENC_GOP_PARAMS_G4_TEMPORAL_ID    "g4temporalId"
#define NI_ENC_GOP_PARAMS_G4_PIC_TYPE       "g4picType"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PICS   "g4numRefPics"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0 "g4refPic0"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0_USED "g4refPic0Used"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1 "g4refPic1"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1_USED "g4refPic1Used"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2 "g4refPic2"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2_USED "g4refPic2Used"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3 "g4refPic3"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3_USED "g4refPic3Used"

#define NI_ENC_GOP_PARAMS_G5_POC_OFFSET     "g5pocOffset"
#define NI_ENC_GOP_PARAMS_G5_QP_OFFSET         "g5QpOffset"
//#define NI_ENC_GOP_PARAMS_G5_QP_FACTOR         "g5QpFactor"
#define NI_ENC_GOP_PARAMS_G5_TEMPORAL_ID    "g5temporalId"
#define NI_ENC_GOP_PARAMS_G5_PIC_TYPE       "g5picType"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PICS   "g5numRefPics"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0 "g5refPic0"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0_USED "g5refPic0Used"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1 "g5refPic1"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1_USED "g5refPic1Used"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2 "g5refPic2"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2_USED "g5refPic2Used"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3 "g5refPic3"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3_USED "g5refPic3Used"

#define NI_ENC_GOP_PARAMS_G6_POC_OFFSET     "g6pocOffset"
#define NI_ENC_GOP_PARAMS_G6_QP_OFFSET         "g6QpOffset"
//#define NI_ENC_GOP_PARAMS_G6_QP_FACTOR         "g6QpFactor"
#define NI_ENC_GOP_PARAMS_G6_TEMPORAL_ID    "g6temporalId"
#define NI_ENC_GOP_PARAMS_G6_PIC_TYPE       "g6picType"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PICS   "g6numRefPics"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0 "g6refPic0"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0_USED "g6refPic0Used"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1 "g6refPic1"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1_USED "g6refPic1Used"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2 "g6refPic2"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2_USED "g6refPic2Used"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3 "g6refPic3"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3_USED "g6refPic3Used"

#define NI_ENC_GOP_PARAMS_G7_POC_OFFSET     "g7pocOffset"
#define NI_ENC_GOP_PARAMS_G7_QP_OFFSET         "g7QpOffset"
//#define NI_ENC_GOP_PARAMS_G7_QP_FACTOR         "g7QpFactor"
#define NI_ENC_GOP_PARAMS_G7_TEMPORAL_ID    "g7temporalId"
#define NI_ENC_GOP_PARAMS_G7_PIC_TYPE       "g7picType"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PICS   "g7numRefPics"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0 "g7refPic0"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0_USED "g7refPic0Used"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1 "g7refPic1"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1_USED "g7refPic1Used"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2 "g7refPic2"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2_USED "g7refPic2Used"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3 "g7refPic3"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3_USED "g7refPic3Used"

typedef struct _ni_gop_rps
{
  int ref_pic;  /*!*< delta_poc of this short reference picture relative to the poc of current picture or index of LTR */
  int ref_pic_used; /*!*< whether this reference picture used by current picture */
} ni_gop_rps_t;

typedef struct _ni_gop_params
{
  int poc_offset;   /*!*< A POC of Nth picture in the custom GOP */
  int qp_offset;       /*!*< QP offset of Nth picture in the custom GOP */
  float qp_factor;  /*!*< QP factor of Nth picture in the custom GOP */
  int temporal_id;  /*!*< A temporal ID of Nth picture in the custom GOP */
  int pic_type;     /*!*< A picture type of Nth picture in the custom GOP */
  int num_ref_pics; /*!*< the number of reference pictures kept for this picture, the value should be within [0, 4] */
  ni_gop_rps_t rps[NI_MAX_REF_PIC];
} ni_gop_params_t;
#else
#define NI_ENC_GOP_PARAMS_G0_PIC_TYPE       "g0picType"
#define NI_ENC_GOP_PARAMS_G0_POC_OFFSET     "g0pocOffset"
#define NI_ENC_GOP_PARAMS_G0_PIC_QP         "g0picQp"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC_L0 "g0numRefPicL0"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_POC_L0 "g0refPocL0"
#define NI_ENC_GOP_PARAMS_G0_NUM_REF_POC_L1 "g0refPocL1"
#define NI_ENC_GOP_PARAMS_G0_TEMPORAL_ID    "g0temporalId"

#define NI_ENC_GOP_PARAMS_G1_PIC_TYPE       "g1picType"
#define NI_ENC_GOP_PARAMS_G1_POC_OFFSET     "g1pocOffset"
#define NI_ENC_GOP_PARAMS_G1_PIC_QP         "g1picQp"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC_L0 "g1numRefPicL0"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_POC_L0 "g1refPocL0"
#define NI_ENC_GOP_PARAMS_G1_NUM_REF_POC_L1 "g1refPocL1"
#define NI_ENC_GOP_PARAMS_G1_TEMPORAL_ID    "g1temporalId"

#define NI_ENC_GOP_PARAMS_G2_PIC_TYPE       "g2picType"
#define NI_ENC_GOP_PARAMS_G2_POC_OFFSET     "g2pocOffset"
#define NI_ENC_GOP_PARAMS_G2_PIC_QP         "g2picQp"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC_L0 "g2numRefPicL0"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_POC_L0 "g2refPocL0"
#define NI_ENC_GOP_PARAMS_G2_NUM_REF_POC_L1 "g2refPocL1"
#define NI_ENC_GOP_PARAMS_G2_TEMPORAL_ID    "g2temporalId"

#define NI_ENC_GOP_PARAMS_G3_PIC_TYPE       "g3picType"
#define NI_ENC_GOP_PARAMS_G3_POC_OFFSET     "g3pocOffset"
#define NI_ENC_GOP_PARAMS_G3_PIC_QP         "g3picQp"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC_L0 "g3numRefPicL0"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_POC_L0 "g3refPocL0"
#define NI_ENC_GOP_PARAMS_G3_NUM_REF_POC_L1 "g3refPocL1"
#define NI_ENC_GOP_PARAMS_G3_TEMPORAL_ID    "g3temporalId"

#define NI_ENC_GOP_PARAMS_G4_PIC_TYPE       "g4picType"
#define NI_ENC_GOP_PARAMS_G4_POC_OFFSET     "g4pocOffset"
#define NI_ENC_GOP_PARAMS_G4_PIC_QP         "g4picQp"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC_L0 "g4numRefPicL0"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_POC_L0 "g4refPocL0"
#define NI_ENC_GOP_PARAMS_G4_NUM_REF_POC_L1 "g4refPocL1"
#define NI_ENC_GOP_PARAMS_G4_TEMPORAL_ID    "g4temporalId"

#define NI_ENC_GOP_PARAMS_G5_PIC_TYPE       "g5picType"
#define NI_ENC_GOP_PARAMS_G5_POC_OFFSET     "g5pocOffset"
#define NI_ENC_GOP_PARAMS_G5_PIC_QP         "g5picQp"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC_L0 "g5numRefPicL0"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_POC_L0 "g5refPocL0"
#define NI_ENC_GOP_PARAMS_G5_NUM_REF_POC_L1 "g5refPocL1"
#define NI_ENC_GOP_PARAMS_G5_TEMPORAL_ID    "g5temporalId"

#define NI_ENC_GOP_PARAMS_G6_PIC_TYPE       "g6picType"
#define NI_ENC_GOP_PARAMS_G6_POC_OFFSET     "g6pocOffset"
#define NI_ENC_GOP_PARAMS_G6_PIC_QP         "g6picQp"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC_L0 "g6numRefPicL0"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_POC_L0 "g6refPocL0"
#define NI_ENC_GOP_PARAMS_G6_NUM_REF_POC_L1 "g6refPocL1"
#define NI_ENC_GOP_PARAMS_G6_TEMPORAL_ID    "g6temporalId"

#define NI_ENC_GOP_PARAMS_G7_PIC_TYPE       "g7picType"
#define NI_ENC_GOP_PARAMS_G7_POC_OFFSET     "g7pocOffset"
#define NI_ENC_GOP_PARAMS_G7_PIC_QP         "g7picQp"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC_L0 "g7numRefPicL0"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_POC_L0 "g7refPocL0"
#define NI_ENC_GOP_PARAMS_G7_NUM_REF_POC_L1 "g7refPocL1"
#define NI_ENC_GOP_PARAMS_G7_TEMPORAL_ID    "g7temporalId"

typedef struct _ni_gop_params
{
  int pic_type;     /*!*< A picture type of Nth picture in the custom GOP */
  int poc_offset;   /*!*< A POC of Nth picture in the custom GOP */
  int pic_qp;       /*!*< A quantization parameter of Nth picture in the custom GOP */
  int num_ref_pic_L0; /*!*< The number of reference L0 of Nth picture in the custom GOP */
  int ref_poc_L0;    /*!*< A POC of reference L0 of Nth picture in the custom GOP */
  int ref_poc_L1;    /*!*< A POC of reference L1 of Nth picture in the custom GOP */
  int temporal_id;  /*!*< A temporal ID of Nth picture in the custom GOP */
} ni_gop_params_t;
#endif



#define NI_ENC_GOP_PARAMS_CUSTOM_GOP_SIZE "customGopSize"

typedef struct _ni_custom_gop_params
{
  int custom_gop_size;                              /*!*< The size of custom GOP (0~8) */
  ni_gop_params_t pic_param[NI_MAX_GOP_NUM]; /*!*< Picture parameters of Nth picture in custom GOP */
} ni_custom_gop_params_t;

#define NI_ENC_REPEAT_HEADERS_FIRST_IDR    0
#define NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES 1
#define NI_KEEP_ALIVE_TIMEOUT "keepAliveTimeout"

typedef struct _ni_encoder_cfg_params
{
#define NI_ENC_PARAM_BITRATE "bitrate"
#define NI_ENC_PARAM_RECONF_DEMO_MODE "ReconfDemoMode"
#define NI_ENC_PARAM_RECONF_FILE "ReconfFile"
#define NI_ENC_PARAM_ROI_DEMO_MODE "RoiDemoMode"
#define NI_ENC_PARAM_CACHE_ROI "cacheRoi"
#define NI_ENC_PARAM_FORCE_PIC_QP_DEMO_MODE "ForcePicQpDemoMode"
#define NI_ENC_PARAM_GEN_HDRS "GenHdrs"
#define NI_ENC_PARAM_PADDING "padding"
#define NI_ENC_PARAM_FORCE_FRAME_TYPE "forceFrameType"
#define NI_ENC_PARAM_PROFILE "profile"
#define NI_ENC_PARAM_LEVEL "level"
#define NI_ENC_PARAM_HIGH_TIER "high-tier"
#define NI_ENC_PARAM_LOG_LEVEL "log-level"
#define NI_ENC_PARAM_LOG "log"
#define NI_ENC_PARAM_GOP_PRESET_IDX "gopPresetIdx"
#define NI_ENC_PARAM_LOW_DELAY "lowDelay"
#define NI_ENC_PARAM_USE_RECOMMENDED_ENC_PARAMS "useRecommendEncParam"
#define NI_ENC_PARAM_USE_LOW_DELAY_POC_TYPE "useLowDelayPocType"
#define NI_ENC_PARAM_CU_SIZE_MODE "cuSizeMode"
#define NI_ENC_PARAM_MAX_NUM_MERGE "maxNumMerge"
#define NI_ENC_PARAM_ENABLE_DYNAMIC_8X8_MERGE "dynamicMerge8x8Enable"
#define NI_ENC_PARAM_ENABLE_DYNAMIC_16X16_MERGE "dynamicMerge16x16Enable"
#define NI_ENC_PARAM_ENABLE_DYNAMIC_32X32_MERGE "dynamicMerge32x32Enable"
#define NI_ENC_PARAM_ENABLE_RATE_CONTROL "RcEnable"
#define NI_ENC_PARAM_ENABLE_CU_LEVEL_RATE_CONTROL "cuLevelRCEnable"
#define NI_ENC_PARAM_ENABLE_HVS_QP "hvsQPEnable"
#define NI_ENC_PARAM_ENABLE_HVS_QP_SCALE "hvsQpScaleEnable"
#define NI_ENC_PARAM_HVS_QP_SCALE "hvsQpScale"
#define NI_ENC_PARAM_MIN_QP "minQp"
#define NI_ENC_PARAM_MAX_QP "maxQp"
#define NI_ENC_PARAM_MAX_DELTA_QP "maxDeltaQp"
#define NI_ENC_PARAM_FORCED_HEADER_ENABLE "repeatHeaders"
#define NI_ENC_PARAM_ROI_ENABLE "roiEnable"
#define NI_ENC_PARAM_CONF_WIN_TOP "confWinTop"
#define NI_ENC_PARAM_CONF_WIN_BOTTOM "confWinBot"
#define NI_ENC_PARAM_CONF_WIN_LEFT "confWinLeft"
#define NI_ENC_PARAM_CONF_WIN_RIGHT "confWinRight"
#define NI_ENC_PARAM_INTRA_PERIOD "intraPeriod"
#define NI_ENC_PARAM_TRANS_RATE "transRate"
#define NI_ENC_PARAM_FRAME_RATE "frameRate"
#define NI_ENC_PARAM_FRAME_RATE_DENOM "frameRateDenom"
#define NI_ENC_PARAM_INTRA_QP "intraQP"
#define NI_ENC_PARAM_DECODING_REFRESH_TYPE "decodingRefreshType"
// Rev. B: H.264 only parameters.
#define NI_ENC_PARAM_ENABLE_8X8_TRANSFORM "transform8x8Enable"
#define NI_ENC_PARAM_AVC_SLICE_MODE "avcSliceMode"
#define NI_ENC_PARAM_AVC_SLICE_ARG "avcSliceArg"
#define NI_ENC_PARAM_ENTROPY_CODING_MODE "entropyCodingMode"
// Rev. B: shared between HEVC and H.264
#define NI_ENC_PARAM_INTRA_MB_REFRESH_MODE "intraMbRefreshMode"
#define NI_ENC_PARAM_INTRA_MB_REFRESH_ARG "intraMbRefreshArg"
#define NI_ENC_PARAM_INTRA_REFRESH_MODE "intraRefreshMode"
#define NI_ENC_PARAM_INTRA_REFRESH_ARG "intraRefreshArg"
// TBD Rev. B: could be shared for HEVC and H.264
#define NI_ENC_PARAM_ENABLE_MB_LEVEL_RC "mbLevelRcEnable"
#define NI_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS "prefTRC"

// To be deprecated: RcInitDelay -> vbvBufferSize, cbr -> fillerEnable
#define NI_ENC_PARAM_RC_INIT_DELAY "RcInitDelay"
#define NI_ENC_PARAM_CBR "cbr"
#define NI_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD "intraRefreshMinPeriod"

//QUADRA
#define NI_ENC_PARAM_CONSTANT_RATE_FACTOR "crf"
#define NI_ENC_PARAM_RDO_LEVEL "rdoLevel"
#define NI_ENC_PARAM_RDO_QUANT "EnableRdoQuant"
#define NI_ENC_PARAM_MAX_CLL "maxCLL"
#define NI_ENC_PARAM_LOOK_AHEAD_DEPTH "lookAheadDepth"
#define NI_ENC_PARAM_ENABLE_AUD "enableAUD"
#define NI_ENC_PARAM_CTB_RC_MODE "ctbRcMode"
#define NI_ENC_PARAM_GOP_SIZE "gopSize"
#define NI_ENC_PARAM_GOP_LOW_DELAY "gopLowdelay"
#define NI_ENC_PARAM_GDR_DURATION "intraRefreshDuration"
#define NI_ENC_PARAM_HRD_ENABLE "hrdEnable"
#define NI_ENC_PARAM_DOLBY_VISION_PROFILE "dolbyVisionProfile"
#define NI_ENC_PARAM_VBV_BUFFER_SIZE "vbvBufferSize"
#define NI_ENC_PARAM_ENABLE_FILLER "fillerEnable"
#define NI_ENC_PARAM_ENABLE_PIC_SKIP "picSkip"
#define NI_ENC_PARAM_MAX_FRAME_SIZE_LOW_DELAY "maxFrameSize"
#define NI_ENC_PARAM_LTR_REF_INTERVAL "ltrRefInterval"
#define NI_ENC_PARAM_LTR_REF_QPOFFSET "ltrRefQpOffset"
#define NI_ENC_PARAM_LTR_FIRST_GAP "ltrFirstGap"
#define NI_ENC_PARAM_LTR_NEXT_INTERVAL "ltrNextInterval"
#define NI_ENC_PARAM_MULTICORE_JOINT_MODE "multicoreJointMode"
#define NI_ENC_PARAM_JPEG_QLEVEL "qlevel"
#define NI_ENC_PARAM_CHROMA_QP_OFFSET "chromaQpOffset"
#define NI_ENC_PARAM_TOL_RC_INTER "tolCtbRcInter"
#define NI_ENC_PARAM_TOL_RC_INTRA "tolCtbRcIntra"
#define NI_ENC_PARAM_BITRATE_WINDOW "bitrateWindow"
#define NI_ENC_INLOOP_DS_RATIO "inLoopDSRatio"
#define NI_ENC_BLOCK_RC_SIZE "blockRCSize"
#define NI_ENC_RC_QP_DELTA_RANGE "rcQpDeltaRange"
#define NI_ENC_PARAM_INTRA_QP_DELTA "intraQpDelta"
#define NI_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE "longTermReferenceEnable"
#define NI_ENC_PARAM_LONG_TERM_REFERENCE_COUNT "longTermReferenceCount"
#define NI_ENC_PARAM_LONG_TERM_REFERENCE_INTERVAL "longTermReferenceInterval"
// stream color info
#define NI_ENC_PARAM_COLOR_PRIMARY "colorPri"
#define NI_ENC_PARAM_COLOR_TRANSFER_CHARACTERISTIC "colorTrc"
#define NI_ENC_PARAM_COLOR_SPACE "colorSpc"
// sample aspect ratio specified in numerator/denominator
#define NI_ENC_PARAM_SAR_NUM "sarNum"
#define NI_ENC_PARAM_SAR_DENOM "sarDenom"
// video_full_range_flag
#define NI_ENC_PARAM_VIDEO_FULL_RANGE_FLAG "videoFullRangeFlag"
// VFR related
#define NI_ENC_PARAM_ENABLE_VFR "enableVFR"
#define NI_ENC_ENABLE_SSIM "enableSSIM"

    //----- Start supported by all codecs -----
    int frame_rate;
    int aspectRatioWidth;
    int aspectRatioHeight;
    int planar;
    int maxFrameSize;
    //----- End supported by all codecs -----

    //----- Start supported by AV1, AVC, HEVC only -----
    int profile;
    int level_idc;
    //GOP Pattern
    int gop_preset_index;
    /*!*< A GOP structure preset option (IPP, IBP, IBBP, IbBbP, use Custom GOP, etc) */   // 0-custom 1-I-only 2-IPPP 3-IBBB 4-IBP .....

    // CUSTOM_GOP
    ni_custom_gop_params_t custom_gop_params;

    int roi_enable;
    int forced_header_enable;
    int long_term_ref_enable;
    int intra_period; /*! Key Frame Interval */
    int intra_mb_refresh_mode;
    int intra_mb_refresh_arg;

    // HLG preferred transfer characteristics
    int preferred_transfer_characteristics;

    int lookAheadDepth;
    int rdoLevel;
    int crf;
    int HDR10MaxLight;
    int HDR10AveLight;
    int HDR10CLLEnable;
    int gdrDuration;
    int ltrRefInterval;
    int ltrRefQpOffset;
    int ltrFirstGap;
    int ltrNextInterval;
    int multicoreJointMode;
    int videoFullRange;
    int long_term_ref_interval;
    int long_term_ref_count;
    //----- End supported by AV1, AVC, HEVC only -----

    //----- Start supported by AVC, HEVC only -----
    //ConformanceWindowOffsets
    int conf_win_top;    /*!*< A conformance window size of TOP */
    int conf_win_bottom; /*!*< A conformance window size of BOTTOM */
    int conf_win_left;   /*!*< A conformance window size of LEFT */
    int conf_win_right;  /*!*< A conformance window size of RIGHT */
    int EnableAUD;
    int EnableRdoQuant;
    int hrdEnable;
    //----- End supported by AVC, HEVC only -----

    //----- Start supported by AVC only -----
    int entropy_coding_mode;
    //----- End supported by AVC only -----

    //----- Start supported by HEVC only -----
    int colorDescPresent;
    int colorPrimaries;
    int colorTrc;
    int colorSpace;
    //----- End supported by HEVC only -----

    //----- Start supported by JPEG only -----
    int qlevel;
    //----- End supported by JPEG only -----

    //----- Start internal use only -----
    int ctbRcMode;
    int gopSize;
    int gopLowdelay;
    int chromaQpOffset;
    float tolCtbRcInter;
    float tolCtbRcIntra;
    int bitrateWindow;
    int inLoopDSRatio;
    int blockRCSize;
    int rcQpDeltaRange;
    //----- End internal use only -----

    //----- start DEPRECATED or for T408 -----
    int high_tier;
    //Preset Mode
    int use_recommend_enc_params; /*!*< 0: Custom, 1: Slow speed and best quality, 2: Normal Speed and quality, 3: Fast Speed and Low Quality */
    //Encode Options
    int cu_size_mode; /*!*< bit 0: enable 8x8 CU, bit 1: enable 16x16 CU, bit 2: enable 32x32 CU */
    int max_num_merge; /*!*< Maximum number of merge candidates (0~2) */
    int enable_dynamic_8x8_merge; /*!*< It enables dynamic merge 8x8 candidates. */
    int enable_dynamic_16x16_merge; /*!*< It enables dynamic merge 16x16 candidates. */
    int enable_dynamic_32x32_merge; /*!*< It enables dynamic merge 32x32 candidates. */

    // Rev. B: H.264 only parameters, in ni_t408_config_t
    // - for H.264 on T408:
    int enable_transform_8x8;
    int avc_slice_mode;
    int avc_slice_arg;

    int decoding_refresh_type;
    //----- end DEPRECATED or for T408 -----

    struct   //Rate control parameters
    {
        int enable_rate_control; /*!*< It enable rate control */
        int min_qp; /*!*< A minimum QP for rate control */   //8
        int max_qp; /*!*< A maximum QP for rate control */   //51
        int intra_qp; /*!< is not used when rate control is enabled */
        int intra_qp_delta;
        int enable_pic_skip;

        //no JPEG
        int enable_cu_level_rate_control; /*!*< It enable CU level rate control */
        int enable_hvs_qp; /*!*< It enable CU QP adjustment for subjective quality enhancement */
        int hvs_qp_scale; /*!*< A QP scaling factor for CU QP adjustment when hvcQpenable = 1 */
        int enable_filler;
        int vbv_buffer_size;

        //deprecated
        int enable_hvs_qp_scale; /*!*< It enable QP scaling factor for CU QP adjustment when enable_hvs_qp = 1 */
        int max_delta_qp; /*!*< A maximum delta QP for rate control */   //10
        int trans_rate;
        int enable_mb_level_rc;
    } rc;
    int keep_alive_timeout; /* keep alive timeout setting */
    int enable_ssim;
} ni_encoder_cfg_params_t;

typedef struct _ni_decoder_input_params_t
{
#define NI_DEC_PARAM_OUT                          "out"
#define NI_DEC_PARAM_ENABLE_OUT_1                 "enableOut1"
#define NI_DEC_PARAM_ENABLE_OUT_2                 "enableOut2"
#define NI_DEC_PARAM_FORCE_8BIT_0                 "force8Bit0"
#define NI_DEC_PARAM_FORCE_8BIT_1                 "force8Bit1"
#define NI_DEC_PARAM_FORCE_8BIT_2                 "force8Bit2"
#define NI_DEC_PARAM_SEMI_PLANAR_0                "semiplanar0"
#define NI_DEC_PARAM_SEMI_PLANAR_1                "semiplanar1"
#define NI_DEC_PARAM_SEMI_PLANAR_2                "semiplanar2"
#define NI_DEC_PARAM_CROP_MODE_0                  "cropMode0"
#define NI_DEC_PARAM_CROP_MODE_1                  "cropMode1"
#define NI_DEC_PARAM_CROP_MODE_2                  "cropMode2"
#define NI_DEC_PARAM_CROP_PARAM_0                 "crop0"
#define NI_DEC_PARAM_CROP_PARAM_1                 "crop1"
#define NI_DEC_PARAM_CROP_PARAM_2                 "crop2"
#define NI_DEC_PARAM_SCALE_0                      "scale0"
#define NI_DEC_PARAM_SCALE_1                      "scale1"
#define NI_DEC_PARAM_SCALE_2                      "scale2"
#define NI_DEC_PARAM_MULTICORE_JOINT_MODE "multicoreJointMode"
#define NI_DEC_PARAM_SAVE_PKT "savePkt"
#define NI_DEC_PARAM_LOW_DELAY "lowDelay"

    int hwframes;
    int enable_out1;
    int enable_out2;
    int mcmode;
    int nb_save_pkt;   // number of decoder input packets to be saved
    int force_8_bit[NI_MAX_NUM_OF_DECODER_OUTPUTS];
    int semi_planar[NI_MAX_NUM_OF_DECODER_OUTPUTS];
    int crop_mode[NI_MAX_NUM_OF_DECODER_OUTPUTS];
    int crop_whxy[NI_MAX_NUM_OF_DECODER_OUTPUTS][4];
    char cr_expr[NI_MAX_NUM_OF_DECODER_OUTPUTS][4]
                [NI_MAX_PPU_PARAM_EXPR_CHAR +
                 1]; /*cut-out of expression to be parsed*/
    int scale_wh[NI_MAX_NUM_OF_DECODER_OUTPUTS][2];
    char sc_expr[NI_MAX_NUM_OF_DECODER_OUTPUTS][2]
                [NI_MAX_PPU_PARAM_EXPR_CHAR +
                 1]; /*cut-out of expression to be parsed*/
    int keep_alive_timeout; /* keep alive timeout setting */
    // decoder lowDelay mode for All I packets or IPPP packets
    int decoder_low_delay;
} ni_decoder_input_params_t;

typedef struct _ni_scaler_input_params_t
{
    int input_format;
    int32_t input_width;
    int32_t input_height;
    int32_t in_rec_width;
    int32_t in_rec_height;
    int32_t in_rec_x;
    int32_t in_rec_y;

    int output_format;
    int32_t output_width;
    int32_t output_height;
    int32_t out_rec_width;
    int32_t out_rec_height;
    int32_t out_rec_x;
    int32_t out_rec_y;

    uint32_t rgba_color;
    ni_scaler_opcode_t op;
} ni_scaler_input_params_t;

typedef struct _ni_scaler_params_t
{
    int filterblit;
    int nb_inputs;
} ni_scaler_params_t;

typedef struct _ni_frame
{
    // codec of the source from which this frame is decoded
    ni_codec_format_t src_codec;

    long long dts;
    long long pts;
    uint32_t end_of_stream;
    uint32_t start_of_stream;
    uint32_t video_width;
    uint32_t video_height;

    uint32_t crop_top;
    uint32_t crop_bottom;
    uint32_t crop_left;
    uint32_t crop_right;

    // for encoder: force headers on this frame
    uint16_t force_headers;
    // for encoder: control long term reference picture feature
    uint8_t use_cur_src_as_long_term_pic;
    uint8_t use_long_term_ref;

    int force_key_frame;
    // for encoding: frame picture type sent to encoder
    // for decoding: frame picture type returned from decoder
    ni_pic_type_t ni_pict_type;
    // total SEI size: used by encoder
    unsigned int sei_total_len;

    // SEI info of closed caption: returned by decoder or set by encoder
    unsigned int sei_cc_offset;
    unsigned int sei_cc_len;
    // SEI info of HDR: returned by decoder
    unsigned int sei_hdr_mastering_display_color_vol_offset;
    unsigned int sei_hdr_mastering_display_color_vol_len;
    unsigned int sei_hdr_content_light_level_info_offset;
    unsigned int sei_hdr_content_light_level_info_len;
    // SEI info of HDR10+: returned by decoder
    unsigned int sei_hdr_plus_offset;
    unsigned int sei_hdr_plus_len;
    // SEI info of User Data Unregistered SEI: returned by decoder
    unsigned int sei_user_data_unreg_offset;
    unsigned int sei_user_data_unreg_len;
    // SEI info of alternative transfer characteristics: returned by decoder
    unsigned int sei_alt_transfer_characteristics_offset;
    unsigned int sei_alt_transfer_characteristics_len;
    // VUI info: returned by decoder
    unsigned int vui_offset;
    unsigned int vui_len;

    // ROI data length: for encoder
    unsigned int roi_len;
    // reconfig data length: for encoder
    unsigned int reconf_len;
    // total extra data data length: for encoder
    unsigned int extra_data_len;
    // force pic qp value
    uint16_t force_pic_qp;
    // frame chunk index
    uint32_t frame_chunk_idx;

    uint8_t *p_data[NI_MAX_NUM_DATA_POINTERS];
    uint32_t data_len[NI_MAX_NUM_DATA_POINTERS];

    uint8_t *p_buffer;
    uint32_t buffer_size;

    ni_buf_t
        *dec_buf;   // buffer pool entry (has memory pointed to by p_buffer)
    uint8_t preferred_characteristics_data_len;
    int pixel_format;

    ni_custom_sei_set_t *p_custom_sei_set;

    // frame auxiliary data
    ni_aux_data_t *aux_data[NI_MAX_NUM_AUX_DATA_PER_FRAME];
    int nb_aux_data;

    // the following info is of the source stream that is returned by decoder:
    // color info, sample aspect ratio, timing etc that are useful at encoding.
    uint8_t color_primaries;
    uint8_t color_trc;
    uint8_t color_space;
    int video_full_range_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;

    int flags;   // flags of demuxed packet
} ni_frame_t;

typedef struct _ni_xcoder_params
{
    int log;
    int preset;
    /*! Numerator and denominator of frame rate */
    uint32_t fps_number;
    uint32_t fps_denominator;
    /*! Width (in pixels) of the source pictures. If this width is not an even
 * multiple of 4, the encoder will pad the pictures internally to meet this
 * minimum requirement. All valid HEVC widths are supported */
    int source_width;

    /*! Height (in pixels) of the source pictures. If this height is not an even
 * multiple of 4, the encoder will pad the pictures internally to meet this
 * minimum requirement. All valid HEVC heights are supported */
    int source_height;
    int bitrate;
    int roi_demo_mode;   // demo ROI support - for internal testing
    int reconf_demo_mode;   // NETINT_INTERNAL - currently only for internal testing
    int force_pic_qp_demo_mode;   // demo force pic qp mode - for internal testing
    int low_delay_mode;           // encoder low latency mode
    int padding;                 // encoder input padding setting
    int generate_enc_hdrs;   // generate encoder headers in advance of encoding
    int use_low_delay_poc_type;   // specifies the encoder to set
                                  // picture_order_count_type=2 in the H.264 SPS

    int dolby_vision_profile;

    // 1: force on every frame with same input type; 0: no (except for I-frame)
    int force_frame_type;

    /**> 0=no HDR in VUI, 1=add HDR info to VUI **/
    int hdrEnableVUI;

    int cacheRoi;   // enables caching of ROIs applied to subsequent frames

    uint32_t ui32VuiDataSizeBits; /**< size of VUI RBSP in bits **/
    uint32_t
        ui32VuiDataSizeBytes; /**< size of VUI RBSP in bytes up to MAX_VUI_SIZE **/
    uint8_t ui8VuiRbsp[NI_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/
    uint32_t pos_num_units_in_tick;
    uint32_t pos_time_scale;

    int color_primaries;
    int color_transfer_characteristic;
    int color_space;
    int sar_num;
    int sar_denom;
    int video_full_range_flag;

    union
    {
        ni_encoder_cfg_params_t cfg_enc_params;
        ni_decoder_input_params_t dec_input_params;
    };

    // NETINT_INTERNAL - currently only for internal testing of reconfig, saving
    // key:val1,val2,val3,...val9 (max 9 values) in the demo reconfig data file
    // this supports max 100 lines in reconfig file, max 10 key/values per line
    int reconf_hash[100][10];
    int hwframes;
    int rootBufId;
    ni_frame_t *p_first_frame;

    int enable_vfr;   //enable the vfr
} ni_xcoder_params_t;

typedef struct _niFrameSurface1
{
    uint16_t ui16FrameIdx;      //frame location on device
    uint16_t ui16session_ID;    //for instance tracking
    uint16_t ui16width;         // width on device
    uint16_t ui16height;        // height on device
    uint32_t ui32nodeAddress;   //currently not in use, formerly offset
    int32_t device_handle;      //handle to access device
    int8_t bit_depth;           //1 == 8bit per pixel, 2 == 10bit
    int8_t encoding_type;       //0 = semiplanar, 1 = semiplanar
    int8_t output_idx;          // 0-2 for decoder output index
    int8_t src_cpu;             // frame origin location
    int32_t dma_buf_fd;         // P2P dma buffer file descriptor
} niFrameSurface1_t;

typedef struct _ni_frame_config
{
    uint16_t picture_width;
    uint16_t picture_height;
    uint16_t picture_format;
    uint16_t options;
    uint16_t rectangle_width;
    uint16_t rectangle_height;
    int16_t rectangle_x;
    int16_t rectangle_y;
    uint32_t rgba_color;
    uint16_t frame_index;
    uint16_t session_id;
    uint8_t output_index;
} ni_frame_config_t;

typedef struct _ni_packet
{
  long long dts;
  long long pts;
  long long pos;
  uint32_t end_of_stream;
  uint32_t start_of_stream;
  uint32_t video_width;
  uint32_t video_height;
  uint32_t frame_type; // encoding only 0=I, 1=P, 2=B
  int recycle_index;
  void* p_data;
  uint32_t data_len;
  int sent_size;

  void* p_buffer;
  uint32_t buffer_size;
  uint32_t avg_frame_qp; // average frame QP reported by VPU
  uint8_t *av1_p_buffer[MAX_AV1_ENCODER_GOP_NUM];
  uint8_t *av1_p_data[MAX_AV1_ENCODER_GOP_NUM];
  uint32_t av1_buffer_size[MAX_AV1_ENCODER_GOP_NUM];
  uint32_t av1_data_len[MAX_AV1_ENCODER_GOP_NUM];
  int av1_buffer_index;
  int av1_show_frame;

  int flags;   // flags of demuxed packet

  ni_custom_sei_set_t *p_custom_sei_set;
} ni_packet_t;

typedef struct _ni_session_data_io
{
  union
  {
    ni_frame_t  frame;
    ni_packet_t packet;
  }data;

} ni_session_data_io_t;

#define NI_XCODER_PRESET_NAMES_ARRAY_LEN  3
#define NI_XCODER_LOG_NAMES_ARRAY_LEN     7

#define NI_XCODER_PRESET_NAME_DEFAULT     "default"
#define NI_XCODER_PRESET_NAME_CUSTOM      "custom"

#define NI_XCODER_LOG_NAME_NONE           "none"
#define NI_XCODER_LOG_NAME_ERROR          "error"
#define NI_XCODER_LOG_NAME_WARN           "warning"
#define NI_XCODER_LOG_NAME_INFO           "info"
#define NI_XCODER_LOG_NAME_DEBUG          "debug"
#define NI_XCODER_LOG_NAME_FULL           "full"

extern LIB_API const char* const g_xcoder_preset_names[NI_XCODER_PRESET_NAMES_ARRAY_LEN];
extern LIB_API const char* const g_xcoder_log_names[NI_XCODER_LOG_NAMES_ARRAY_LEN];

/*!*****************************************************************************
 *  \brief  Allocate and initialize a new ni_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 ******************************************************************************/
LIB_API ni_session_context_t *ni_device_session_context_alloc_init(void);

/*!*****************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *              struct
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_FAILURE
 ******************************************************************************/
LIB_API ni_retcode_t
ni_device_session_context_init(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Clear already allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *
 *
 ******************************************************************************/
LIB_API void ni_device_session_context_clear(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Free previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *              struct
 *
 ******************************************************************************/
LIB_API void ni_device_session_context_free(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Create event and return event handle if successful (Windows only)
 *
 *  \return On success returns a event handle
 *          On failure returns NI_INVALID_EVENT_HANDLE
 ******************************************************************************/
LIB_API ni_event_handle_t ni_create_event();

/*!*****************************************************************************
 *  \brief  Close event and release resources (Windows only)
 *
 *  \return NONE
 *
 ******************************************************************************/
LIB_API void ni_close_event(ni_event_handle_t event_handle);

/*!*****************************************************************************
 *  \brief  Open device and return device device_handle if successful
 *
 *  \param[in]  p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_max_io_size_out Maximum IO Transfer size supported
 *
 *  \return On success returns a device device_handle
 *          On failure returns NI_INVALID_DEVICE_HANDLE
 ******************************************************************************/
LIB_API ni_device_handle_t ni_device_open(const char *dev,
                                          uint32_t *p_max_io_size_out);

/*!*****************************************************************************
 *  \brief  Close device and release resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *
 *  \return NONE
 *
 ******************************************************************************/
LIB_API void ni_device_close(ni_device_handle_t dev);

/*!*****************************************************************************
 *  \brief  Query device and return device capability structure
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] p_cap  Pointer to a caller allocated ni_device_capability_t
 *                    struct
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_ERROR_MEM_ALOC
 *                     NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_capability_query(
    ni_device_handle_t device_handle, ni_device_capability_t *p_cap);

/*!*****************************************************************************
 *  \brief  Open a new device session depending on the device_type parameter
 *          If device_type is NI_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_DEVICE_TYPE_ENCODER opens encoding session
 *          If device_type is NI_DEVICE_TYPE_SCALER opens scaling session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER,
 *                          or NI_DEVICE_TYPE_SCALER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_session_open(ni_session_context_t *p_ctx,
                                            ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Close device session that was previously opened by calling
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_DEVICE_TYPE_ENCODER closes encoding session
 *          If device_type is NI_DEVICE_TYPE_SCALER closes scaling session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] eos_received Flag indicating if End Of Stream indicator was
 *                          received
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER,
 *                          or NI_DEVICE_TYPE_SCALER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_session_close(ni_session_context_t *p_ctx,
                                             int eos_received,
                                             ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Send a flush command to the device
 *          If device_type is NI_DEVICE_TYPE_DECODER sends flush command to
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER sends flush command to
 *          encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_session_flush(ni_session_context_t *p_ctx,
                                             ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Save a stream's headers in a decoder session that can be used later
 *          for continuous decoding from the same source.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] hdr_data     Pointer to header data
 *  \param[in] hdr_size     Size of header data in bytes
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_dec_session_save_hdrs(
    ni_session_context_t *p_ctx, uint8_t *hdr_data, uint8_t hdr_size);

/*!*****************************************************************************
 *  \brief  Flush a decoder session to get ready to continue decoding.
 *  Note: this is different from ni_device_session_flush in that it closes the
 *        current decode session and opens a new one for continuous decoding.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_dec_session_flush(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Send data to the device
 *          If device_type is NI_DEVICE_TYPE_DECODER sends data packet to
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER sends data frame to encoder
 *          If device_type is NI_DEVICE_TYPE_AI sends data frame to AI engine
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a
 *                          ni_frame_t data frame or ni_packet_t data packet to
 *                          send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER or
 *                          NI_DEVICE_TYPE_AI
 *                          If NI_DEVICE_TYPE_DECODER is specified, it is
 *                          expected that the ni_packet_t struct inside the
 *                          p_data pointer contains data to send.
 *                          If NI_DEVICE_TYPE_ENCODER or NI_DEVICE_TYPE_AI is
 *                          specified, it is expected that the ni_frame_t
 *                          struct inside the p_data pointer contains data to
 *                          send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API int ni_device_session_write(ni_session_context_t *p_ctx,
                                    ni_session_data_io_t *p_data,
                                    ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Read data from the device
 *          If device_type is NI_DEVICE_TYPE_DECODER reads data packet from
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER reads data frame from
 *          encoder
 *          If device_type is NI_DEVICE_TYPE_AI reads data frame from AI engine
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated ni_session_data_io_t
 *                          struct which contains either a ni_frame_t data frame
 *                          or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER, or
 *                          NI_DEVICE_TYPE_SCALER
 *                          If NI_DEVICE_TYPE_DECODER is specified, data that
 *                          was read will be placed into ni_frame_t struct
 *                          inside the p_data pointer
 *                          If NI_DEVICE_TYPE_ENCODER is specified, data that
 *                          was read will be placed into ni_packet_t struct
 *                          inside the p_data pointer
 *                          If NI_DEVICE_TYPE_AI is specified, data that was
 *                          read will be placed into ni_frame_t struct inside
 *                          the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API int ni_device_session_read(ni_session_context_t *p_ctx,
                                   ni_session_data_io_t *p_data,
                                   ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Query session data from the device -
 *          If device_type is valid, will query session data
 *          from specified device type
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or
 *                          NI_DEVICE_TYPE_ENCODER or
 *                          NI_DEVICE_TYPE_SCALER or
 *                          NI_DEVICE_TYPE_AI or
 *                          NI_DEVICE_TYPE_UPLOADER
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_session_query(ni_session_context_t *p_ctx,
                                             ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Allocate preliminary memory for the frame buffer based on provided
 *          parameters. Applicable to YUV420 Planar pixel (8 or 10 bit/pixel)
 *          format or 32-bit RGBA.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                           ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                           should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel,
 *                           4 for 32 bits/pixel (RGBA)
 *  \param[in] hw_frame_count Number of hw descriptors stored in lieu of raw YUV
 *  \param[in] is_planar     0 if semiplanar else planar
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc(ni_frame_t *p_frame, int video_width,
                                           int video_height, int alignment,
                                           int metadata_flag, int factor,
                                           int hw_frame_count, int is_planar);

LIB_API ni_retcode_t ni_frame_buffer_alloc_dl(ni_frame_t *p_frame,
                                                 int video_width, int video_height,
                                                 int pixel_format);

/*!*****************************************************************************
 *  \brief  Allocate memory for decoder frame buffer based on provided
 *          parameters; the memory is retrieved from a buffer pool and will be
 *          returned to the same buffer pool by ni_decoder_frame_buffer_free.
 *  Note:   all attributes of ni_frame_t will be set up except for memory and
 *          buffer, which rely on the pool being allocated; the pool will be
 *          allocated only after the frame resolution is known.
 *
 *  \param[in] p_pool        Buffer pool to get the memory from
 *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
 *  \param[in] alloc_mem     Whether to get memory from buffer pool
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Alignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *  \param[in] is_planar     0 if semiplanar else planar
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_decoder_frame_buffer_alloc(
    ni_buf_pool_t *p_pool, ni_frame_t *pframe, int alloc_mem, int video_width,
    int video_height, int alignment, int factor, int is_planar);

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data.
  *          Applicable to YUV420p AVFrame only. 8 or 10 bit/pixel.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not 
  *                           to allocate any buffer (zero-copy from existing)
  *  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
LIB_API ni_retcode_t ni_encoder_frame_buffer_alloc(
    ni_frame_t *pframe, int video_width, int video_height, int linesize[],
    int alignment, int extra_len, bool alignment_2pass_wa);

LIB_API ni_retcode_t ni_scaler_dest_frame_alloc(
    ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params,
    niFrameSurface1_t *p_surface);

LIB_API ni_retcode_t ni_scaler_input_frame_alloc(
    ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params,
    niFrameSurface1_t *p_src_surface);

LIB_API ni_retcode_t ni_scaler_frame_pool_alloc(
    ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params);

/*!*****************************************************************************
*  \brief  Allocate memory for the frame buffer based on provided parameters
*          taking into account pic line size and extra data.
*          Applicable to nv12 AVFrame only. Cb/Cr size matches that of Y.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] linesize      Picture line size
*  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not
*                           to allocate any buffer (zero-copy from existing)
*  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc_nv(ni_frame_t *p_frame,
                                              int video_width, int video_height,
                                              int linesize[], int extra_len,
                                              bool alignment_2pass_wa);

/*!*****************************************************************************
  *  \brief  This API is a wrapper for ni_encoder_frame_buffer_alloc(), used
  *          for planar pixel formats, and ni_frame_buffer_alloc_nv(), used for
  *          semi-planar pixel formats. This API is meant to combine the
  *          functionality for both formats.
  *          Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data.
  *          Applicable to YUV420p(8 or 10 bit/pixel) or nv12 AVFrame.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] planar        true: if planar:
  *                           pixel_format == (NI_PIX_FMT_YUV420P ||
  *                               NI_PIX_FMT_YUV420P10LE ||NI_PIX_FMT_RGBA).
  *                           false: semi-planar:
  *                           pixel_format == (NI_PIX_FMT_NV12 ||
  *                                NI_PIX_FMT_P010LE).
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement. Only used for planar format.
  *  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not 
  *                           to allocate any buffer (zero-copy from existing)
  *  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
LIB_API ni_retcode_t ni_encoder_sw_frame_buffer_alloc(bool planar, ni_frame_t *p_frame,
                                                      int video_width, int video_height,
                                                      int linesize[], int alignment,
                                                      int extra_len,
                                                      bool alignment_2pass_wa);

/*!*****************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_frame_buffer_alloc or ni_encoder_frame_buffer_alloc or
 *          ni_frame_buffer_alloc_nv
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_free(ni_frame_t *pframe);

/*!*****************************************************************************
 *  \brief  Free decoder frame buffer that was previously allocated with
 *          ni_decoder_frame_buffer_alloc, returning memory to a buffer pool.
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_decoder_frame_buffer_free(ni_frame_t *pframe);

/*!*****************************************************************************
 *  \brief  Return a memory buffer to memory buffer pool, for a decoder frame.
 *
 *  \param[in] buf              Buffer to be returned.
 *  \param[in] p_buffer_pool    Buffer pool to return buffer to.
 *
 *  \return None
 ******************************************************************************/
LIB_API void
ni_decoder_frame_buffer_pool_return_buf(ni_buf_t *buf,
                                        ni_buf_pool_t *p_buffer_pool);

/*!*****************************************************************************
 *  \brief  Allocate memory for the packet buffer based on provided packet size
 *
 *  \param[in] p_packet      Pointer to a caller allocated
 *                           ni_packet_t struct
 *  \param[in] packet_size   Required allocation size
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_packet_buffer_alloc(ni_packet_t *ppacket,
                                            int packet_size);

/*!*****************************************************************************
 *  \brief  Allocate packet buffer using a user provided pointer, the memory
 *   is expected to have already been allocated.
 *
 *   For ideal performance memory should be 4k aligned. If it is not 4K aligned
 *   then a temporary 4k aligned memory will be used to copy data to and from
 *   when writing and reading. This will negatively impact performance.
 *
 *   This API will overwrite p_packet->buffer_size, p_packet->p_buffer and
 *   p_packet->p_data fields in p_packet.
 *
 *   This API will not free any memory associated with p_packet->p_buffer and
 *   p_packet->p_data fields in p_packet.
 *   Common use case could be,
 *       1. Allocate memory to pointer
 *       2. Call ni_custom_packet_buffer_alloc() with allocated pointer.
 *       3. Use p_packet as required.
 *       4. Call ni_packet_buffer_free() to free up the memory.
 *
 *  \param[in] p_buffer      User provided pointer to be used for buffer
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_packet_t struct
 *  \param[in] buffer_size   Buffer size
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_custom_packet_buffer_alloc(void *p_buffer,
                                           ni_packet_t *p_packet,
                                           int buffer_size);

/*!*****************************************************************************
 *  \brief  Free packet buffer that was previously allocated with
 *          ni_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_packet_buffer_free(ni_packet_t *ppacket);

/*!*****************************************************************************
 *  \brief  Free packet buffer that was previously allocated with
 *          ni_packet_buffer_alloc for AV1 packets merge
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_packet_buffer_free_av1(ni_packet_t *ppacket);

/*!*****************************************************************************
 *  \brief  Copy video packet accounting for alignment
 *
 *  \param[in] p_destination  Destination to where to copy to
 *  \param[in] p_source       Source from where to copy from
 *  \param[in] cur_size       current size
 *  \param[out] p_leftover    Pointer to the data that was left over
 *  \param[out] p_prev_size   Size of the data leftover
 *
 *  \return On success        Total number of bytes that were copied
 *          On failure        NI_RETCODE_FAILURE
 ******************************************************************************/
LIB_API int ni_packet_copy(void *p_destination, const void *const p_source,
                           int cur_size, void *p_leftover, int *p_prev_size);

/*!*****************************************************************************
 *  \brief  Add a new auxiliary data to a frame
 *
 *  \param[in/out] frame  a frame to which the auxiliary data should be added
 *  \param[in]     type   type of the added auxiliary data
 *  \param[in]     data_size size of the added auxiliary data
 *
 *  \return a pointer to the newly added aux data on success, NULL otherwise
 ******************************************************************************/
LIB_API ni_aux_data_t *ni_frame_new_aux_data(ni_frame_t *frame,
                                             ni_aux_data_type_t type,
                                             int data_size);

/*!*****************************************************************************
 *  \brief  Add a new auxiliary data to a frame and copy in the raw data
 *
 *  \param[in/out] frame  a frame to which the auxiliary data should be added
 *  \param[in]     type   type of the added auxiliary data
 *  \param[in]     raw_data  the raw data of the aux data
 *  \param[in]     data_size size of the added auxiliary data
 *
 *  \return a pointer to the newly added aux data on success, NULL otherwise
 ******************************************************************************/
LIB_API ni_aux_data_t *
ni_frame_new_aux_data_from_raw_data(ni_frame_t *frame, ni_aux_data_type_t type,
                                    const uint8_t *raw_data, int data_size);

/*!*****************************************************************************
 *  \brief  Retrieve from the frame auxiliary data of a given type if exists
 *
 *  \param[in] frame  a frame from which the auxiliary data should be retrieved
 *  \param[in] type   type of the auxiliary data to be retrieved
 *
 *  \return a pointer to the aux data of a given type on success, NULL otherwise
 ******************************************************************************/
LIB_API ni_aux_data_t *ni_frame_get_aux_data(const ni_frame_t *frame,
                                             ni_aux_data_type_t type);

/*!*****************************************************************************
 *  \brief  If auxiliary data of the given type exists in the frame, free it
 *          and remove it from the frame.
 *
 *  \param[in/out] frame a frame from which the auxiliary data should be removed
 *  \param[in] type   type of the auxiliary data to be removed
 *
 *  \return None
 ******************************************************************************/
LIB_API void ni_frame_free_aux_data(ni_frame_t *frame, ni_aux_data_type_t type);

/*!*****************************************************************************
 *  \brief  Free and remove all auxiliary data from the frame.
 *
 *  \param[in/out] frame a frame from which the auxiliary data should be removed
 *
 *  \return None
 ******************************************************************************/
LIB_API void ni_frame_wipe_aux_data(ni_frame_t *frame);

/*!*****************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param        Pointer to a user allocated ni_xcoder_params_t
 *                           to initialize to default parameters
 *  \param[in] fps_num       Frames per second
 *  \param[in] fps_denom     FPS denomination
 *  \param[in] bit_rate      bit rate
 *  \param[in] width         frame width
 *  \param[in] height        frame height
 *  \param[in] codec_format  codec from ni_codec_format_t
 *
 *  \return On success
 *                           NI_RETCODE_SUCCESS
 *          On failure
 *                           NI_RETCODE_FAILURE
 *                           NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_encoder_init_default_params(
    ni_xcoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate,
    int width, int height, ni_codec_format_t codec_format);

/*!*****************************************************************************
 *  \brief  Initialize default decoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_xcoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      frame width
 *  \param[in] height     frame height
 *
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_decoder_init_default_params(ni_xcoder_params_t *p_param,
                                                    int fps_num, int fps_denom,
                                                    long bit_rate, int width,
                                                    int height);

/*!*****************************************************************************
 *  \brief  Set value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t
 *                        to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_encoder_params_set_value(ni_xcoder_params_t *p_params,
                                                 const char *name,
                                                 const char *value);

/*!*****************************************************************************
*  \brief  Set value referenced by name in decoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t (used
*                        for decoder too for now ) to find and set a particular
*                        parameter
*  \param[in] name       String represented parameter name to search
*  \param[in] value      Parameter value to set
*
*  \return On success
*                        NI_RETCODE_SUCCESS
*          On failure
*                        NI_RETCODE_FAILURE
*                        NI_RETCODE_INVALID_PARAM
******************************************************************************/
LIB_API ni_retcode_t ni_decoder_params_set_value(ni_xcoder_params_t *p_params,
                                                 const char *name, char *value);

/*!*****************************************************************************
 *  \brief  Set GOP parameter value referenced by name in encoder parameters
 *          structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t
 *                        to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_encoder_gop_params_set_value(
    ni_xcoder_params_t *p_params, const char *name, const char *value);

/*!*****************************************************************************
*  \brief  Copy existing decoding session params for hw frame usage
*
*  \param[in] src_p_ctx    Pointer to a caller allocated source session context
*  \param[in] dst_p_ctx    Pointer to a caller allocated destination session
*                          context
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
******************************************************************************/
LIB_API ni_retcode_t ni_device_session_copy(ni_session_context_t *src_p_ctx,
                                            ni_session_context_t *dst_p_ctx);

/*!*****************************************************************************
*  \brief  Send frame pool setup info to device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] pool_size    Upload session initial allocated frames count
*                          must be > 0,
*  \param[in] pool         0 use the normal pool
*                          1 use a dedicated P2P pool
*
*  \return On success      Return code
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*                          NI_RETCODE_ERROR_MEM_ALOC
******************************************************************************/
LIB_API int ni_device_session_init_framepool(ni_session_context_t *p_ctx,
                                             uint32_t pool_size, uint32_t pool);

/*!*****************************************************************************
*  \brief  Read data from the device
*          If device_type is NI_DEVICE_TYPE_DECODER reads data hwdesc from
*          decoder
*          If device_type is NI_DEVICE_TYPE_SCALER reads data hwdesc from
*          scaler
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains either a
*                          ni_frame_t data frame or ni_packet_t data packet to
*                          send
*  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_SCALER
*                          If NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_SCALER is specified,
*                          hw descriptor info will be stored in p_data ni_frame
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
******************************************************************************/
LIB_API int ni_device_session_read_hwdesc(ni_session_context_t *p_ctx,
                                          ni_session_data_io_t *p_data,
                                          ni_device_type_t device_type);

/*!*****************************************************************************
*  \brief  Read YUV data from hw descriptor stored location on device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains either a
*                          ni_frame_t data frame or ni_packet_t data packet to
*                          send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
******************************************************************************/
LIB_API int ni_device_session_hwdl(ni_session_context_t *p_ctx,
                                   ni_session_data_io_t *p_data,
                                   niFrameSurface1_t *hwdesc);

/*!*****************************************************************************
*  \brief  Send raw YUV input to uploader instance and retrieve a HW descriptor
*          to represent it
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] p_src_data   Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains a
*                          ni_frame_t data frame to send to uploader
*  \param[out] hwdesc      HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_device_session_hwup(ni_session_context_t* p_ctx, ni_session_data_io_t *p_src_data, niFrameSurface1_t* hwdesc);

/*!*****************************************************************************
*  \brief  Allocate memory for the hwDescriptor buffer based on provided
*          parameters taking into account pic size and extra data.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] extra_len     Extra data size (incl. meta data)
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc_hwenc(ni_frame_t *pframe,
                                                 int video_width,
                                                 int video_height,
                                                 int extra_len);

/*!*****************************************************************************
*  \brief  Recycle a frame buffer on card
*
*  \param[in] surface   Struct containing device and frame location to clear out
*  \param[in] device_handle  handle to access device memory buffer is stored in
*
*  \return On success    NI_RETCODE_SUCCESS
*          On failure    NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_hwframe_buffer_recycle(niFrameSurface1_t *surface,
                                               int32_t device_handle);

/*!*****************************************************************************
 *  \brief  Set parameters on the device for the 2D engine
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  p_params    pointer to the scaler parameters
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED

 ******************************************************************************/
LIB_API ni_retcode_t ni_scaler_set_params(ni_session_context_t *p_ctx,
                                          ni_scaler_params_t *p_params);

/*!*****************************************************************************
 *  \brief  Allocate a frame on the device for 2D engine or AI engine
 *          to work on based on provided parameters
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  width       width, in pixels
 *  \param[in]  height      height, in pixels
 *  \param[in]  format      pixel format
 *  \param[in]  options     options bitmap flags, bit 0 (NI_SCALER_FLAG_IO) is
 *              0=input frame or 1=output frame. Bit 1 (NI_SCALER_FLAG_PC) is
 *              0=single allocation, 1=create pool. Bit 2 (NI_SCALER_FLAG_PA) is
 *              0=straight alpha, 1=premultiplied alpha
 *  \param[in]  rectangle_width     clipping rectangle width
 *  \param[in]  rectangle_height    clipping rectangle height
 *  \param[in]  rectangle_x         horizontal position of clipping rectangle
 *  \param[in]  rectangle_y         vertical position of clipping rectangle
 *  \param[in]  rgba_color          RGBA fill colour (for padding only)
 *  \param[in]  frame_index         input hwdesc index
 *  \param[in]  device_type         only NI_DEVICE_TYPE_SCALER
 *              and NI_DEVICE_TYPE_AI (only needs p_ctx and frame_index)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_alloc_frame(ni_session_context_t* p_ctx,
                                   int width,
                                   int height,
                                   int format,
                                   int options,
                                   int rectangle_width,
                                   int rectangle_height,
                                   int rectangle_x,
                                   int rectangle_y,
                                   int rgba_color,
                                   int frame_index,
                                   ni_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Config a frame on the device for 2D engined
 *          to work on based on provided parameters
 *
 *  \param[in]  p_ctx        pointer to session context
 *  \param[in]  p_cfg        pointer to frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_config_frame(ni_session_context_t *p_ctx,
                                            ni_frame_config_t *p_cfg);

/*!*****************************************************************************
 *  \brief  Config multiple frame on the device for 2D engined
 *          to work on based on provided parameters
 *
 *  \param[in]  p_ctx        pointer to session context
 *  \param[in]  p_cfg_in     input frame config array
 *  \param[in]  numInCfgs    number of frame config entries in the p_cfg_in array
 *  \param[in]  p_cfg_out    output frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_multi_config_frame(ni_session_context_t *p_ctx,
                                                  ni_frame_config_t p_cfg_in[],
                                                  int numInCfgs,
                                                  ni_frame_config_t *p_cfg_out);

/*!*****************************************************************************
 *  \brief   Allocate memory for the frame buffer based on provided parameters
 *           taking into account the pixel format, width, height, stride,
 *           alignment, and extra data
 *  \param[in]  p_frame         Pointer to caller allocated ni_frame_t
 *  \param[in]  pixel_format    a pixel format in ni_pix_fmt_t enum
 *  \param[in]  video_width     width, in pixels
 *  \param[in]  video_height    height, in pixels
 *  \param[in]  linesize        horizontal stride
 *  \param[in]  alignment       apply a 16 pixel height alignment (T408 only)
 *  \param[in]  extra_len       meta data size
 *
 *  \return     NI_RETCODE_SUCCESS
 *              NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_MEM_ALOC
 *
 ******************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc_pixfmt(
    ni_frame_t *pframe, int pixel_format, int video_width, int video_height,
    int linesize[], int alignment, int extra_len);

/*!*****************************************************************************
 *  \brief   configure a network context based with the network binary
 *
 *  \param[in]  p_ctx           Pointer to caller allocated ni_session_context_t
 *  \param[in]  file            Pointer to caller network binary file path
 *
 *  \return     NI_RETCODE_SUCCESS
 *              NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_MEM_ALOC
 *              NI_RETCODE_ERROR_INVALID_SESSION
 *              NI_RETCODE_ERROR_NVME_CMD_FAILED
 *              NI_RETCODE_FAILURE
 *
 ******************************************************************************/
LIB_API ni_retcode_t ni_ai_config_network_binary(ni_session_context_t *p_ctx,
                                                 ni_network_data_t *p_network,
                                                 const char *file);

/*!*****************************************************************************
 *  \brief   Allocate input layers memory for AI frame buffer based on provided parameters
 *           taking into account width, height, format defined by network.
 *
 *  \param[out] p_frame         Pointer to caller allocated ni_frame_t
 *  \param[in]  p_network       Pointer to caller allocated ni_network_data_t
 *
 *  \return     NI_RETCODE_SUCCESS
 *              NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_MEM_ALOC
 *
 ******************************************************************************/
LIB_API ni_retcode_t ni_ai_frame_buffer_alloc(ni_frame_t *p_frame,
                                              ni_network_data_t *p_network);

/*!*****************************************************************************
 *  \brief  Allocate output layers memory for the packet buffer based on provided network
 *
 *  \param[out] p_packet     Pointer to a caller allocated
 *                                               ni_packet_t struct
 *  \param[in] p_network     Pointer to a caller allocated
 *                                               ni_network_data_t struct
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_ai_packet_buffer_alloc(ni_packet_t *p_packet,
                                               ni_network_data_t *p_network);

// wrapper API request, dynamic encode configuration setting to be sent to the
// encoder with the next frame

/*!*****************************************************************************
 *  \brief  Reconfigure bitrate dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] bitrate    Target bitrate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_reconfig_bitrate(ni_session_context_t *p_ctx,
                                         int32_t bitrate);

/*!*****************************************************************************
 *  \brief  Reconfigure VUI dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] bitrate    Target bitrate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_reconfig_vui(ni_session_context_t *p_ctx,
                                     ni_vui_hrd_t *vui);

/*!*****************************************************************************
 *  \brief  Force next frame to be IDR frame during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *
 *  \return On success    NI_RETCODE_SUCCESS
 ******************************************************************************/
LIB_API ni_retcode_t ni_force_idr_frame_type(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Set a frame's support of Long Term Reference frame during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] ltr        Pointer to struct specifying LTR support
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_set_ltr(ni_session_context_t *p_ctx,
                                ni_long_term_ref_t *ltr);

/*!*****************************************************************************
 *  \brief  Set Long Term Reference interval
 *
 *  \param[in] p_ctx         Pointer to caller allocated ni_session_context_t
 *  \param[in] ltr_interval  the new long term reference inteval value
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_set_ltr_interval(ni_session_context_t *p_ctx,
                                         int32_t ltr_interval);

/*!*****************************************************************************
 *  \brief  Set frame reference invalidation
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] frame_num  frame number after which all references shall be
 *                        invalidated
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_set_frame_ref_invalid(ni_session_context_t *p_ctx,
                                              int32_t frame_num);

/*!*****************************************************************************
 *  \brief  Reconfigure framerate dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] framerate        Pointer to struct specifying framerate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_retcode_t ni_reconfig_framerate(ni_session_context_t *p_ctx,
                                           ni_framerate_t *framerate);

#ifndef _WIN32
/*!*****************************************************************************
*  \brief  Acquire a P2P frame buffer from the hwupload session
*
*  \param[in] p_upl_ctx    Pointer to a caller allocated
*                                              ni_session_context_t struct
*  \param[out] p_frame     Pointer to a caller allocated hw frame
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_device_session_acquire(ni_session_context_t *p_upl_ctx,
                                      ni_frame_t *p_frame);

/*!*****************************************************************************
 *  \brief  Lock a hardware P2P frame prior to encoding
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated upload context
 *        [in] p_frame      pointer to caller allocated hardware P2P frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure      NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_uploader_frame_buffer_lock(
    ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);

/*!*****************************************************************************
 *  \brief  Unlock a hardware P2P frame after encoding
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated upload context
 *        [in] p_frame      pointer to caller allocated hardware P2P frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure      NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_uploader_frame_buffer_unlock(
    ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);

/*!*****************************************************************************
 *  \brief  Special P2P test API call. Copies YUV data from the software
 *          frame to the hardware P2P frame on the Quadra device
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated uploader session
 *                          context
 *        [in] p_swframe    pointer to a caller allocated software frame
 *        [in] p_hwframe    pointer to a caller allocated hardware frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_uploader_p2p_test_send(ni_session_context_t *p_upl_ctx,
                                               uint8_t *p_data, uint32_t len,
                                               ni_frame_t *p_hwframe);

/*!*****************************************************************************
 *  \brief  Set the incoming frame format for the encoder
 *
 *  \param[in] p_enc_ctx    pointer to encoder context
 *        [in] p_enc_params pointer to encoder parameters
 *        [in] width        input width
 *        [in] height       input height
 *        [in] bit_depth    8 for 8-bit YUV, 10 for 10-bit YUV
 *        [in] src_endian   NI_FRAME_LITTLE_ENDIAN or NI_FRAME_BIG_ENDIAN
 *        [in] planar       0 for semi-planar YUV, 1 for planar YUV
 *
 *  \return on success
 *          NI_RETCODE_SUCCESS
 *
 *          on failure
 *          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_encoder_set_input_frame_format(
    ni_session_context_t *p_enc_ctx, ni_xcoder_params_t *p_enc_params,
    int width, int height, int bit_depth, int src_endian, int planar);

/*!*****************************************************************************
 *  \brief  Set the frame format for the uploader
 *
 *  \param[in]  p_upl_ctx       pointer to uploader context
 *        [in]  width           width
 *        [in]  height          height
 *        [in]  pixel_format    pixel format
 *        [in]  isP2P           0 = normal, 1 = P2P
 *
 *  \return on success
 *          NI_RETCODE_SUCCESS
 *
 *          on failure
 *          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t
ni_uploader_set_frame_format(ni_session_context_t *p_upl_ctx, int width,
                             int height, ni_pix_fmt_t pixel_format, int isP2P);

LIB_API ni_retcode_t ni_scaler_p2p_frame_acquire(ni_session_context_t *p_ctx,
                                                 niFrameSurface1_t *p_surface,
                                                 int data_len);

/*!*****************************************************************************
 *  \brief  Recycle hw P2P frames
 *
 *  \param [in] p_frame     pointer to an acquired P2P hw frame
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *
 *          on failure
 *              NI_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_retcode_t ni_hwframe_p2p_buffer_recycle(ni_frame_t *p_frame);
#endif

/*!*****************************************************************************
 *  \brief  Read encoder stream header from the device
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct from encoder
 *  \param[in] p_data       Pointer to a caller allocated ni_session_data_io_t
 *                          struct which contains a ni_packet_t data packet to
 *                          receive
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_encoder_session_read_stream_header(ni_session_context_t *p_ctx,
                                                  ni_session_data_io_t *p_data);


/*!*****************************************************************************
 *  \brief  Get the DMA buffer file descriptor from the P2P frame
 *
 *  \param[in]  p_frame     pointer to a P2P frame
 *
 *  \return     On success
 *                          DMA buffer file descriptor
 *              On failure
 *                          NI_RETCODE_FAILURE
*******************************************************************************/
LIB_API int32_t ni_get_dma_buf_file_descriptor(const ni_frame_t* p_frame);


/*!*****************************************************************************
 *  \brief  Send sequence change information to device
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] width        input width
 *  \param[in] height       input height
 *  \param[in] bit_depth_factor    1 for 8-bit YUV, 2 for 10-bit YUV
 *  \param[in] device_type  device type (must be encoder)
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_retcode_t ni_device_session_sequence_change(ni_session_context_t *p_ctx,
                                            int width, int height, int bit_depth_factor, ni_device_type_t device_type);


#ifdef __cplusplus
}
#endif
