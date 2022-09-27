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
*  \file   ni_device_api_logan.h
*
*  \brief  Main NETINT device API header file
*           provides the ability to communicate with NI T-408 type hardware
*           transcoder devices
*
*******************************************************************************/

#pragma once


#ifdef __cplusplus
extern "C"
{
#endif

#include "ni_defs_logan.h"

#define NI_LOGAN_DATA_FORMAT_VIDEO_PACKET 0
#define NI_LOGAN_DATA_FORMAT_YUV_FRAME    1
#define NI_LOGAN_DATA_FORMAT_Y_FRAME      2
#define NI_LOGAN_DATA_FORMAT_CB_FRAME     3
#define NI_LOGAN_DATA_FORMAT_CR_FRAME     4

#define NI_LOGAN_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

// the following are the default values from FFmpeg
#define LOGAN_AV_CODEC_DEFAULT_BITRATE 200 * 1000

#define NI_LOGAN_MAX_GOP_NUM 8

#define NI_LOGAN_MAX_VUI_SIZE 32

#define LOGAN_FRAME_CHUNK_INDEX_SIZE  4096
#define NI_LOGAN_SIGNATURE_SIZE       256

#define NI_LOGAN_MAX_RESOLUTION_WIDTH 8192
#define NI_LOGAN_MAX_RESOLUTION_HEIGHT 8192
#define NI_LOGAN_MAX_RESOLUTION_AREA 8192*5120

#define NI_LOGAN_FRAME_LITTLE_ENDIAN 0
#define NI_LOGAN_FRAME_BIG_ENDIAN 1

#define NI_LOGAN_INVALID_SESSION_ID   (-1)
#define NI_LOGAN_INVALID_HW_FRAME_IDX (-3) // fw default invalid value
#define NI_LOGAN_INVALID_HW_META_IDX  (-5) // fw hw meta index

#define NI_LOGAN_MAX_BITRATE 700000000
#define NI_LOGAN_MIN_BITRATE 64000

#define NI_LOGAN_MAX_INTRA_PERIOD 1024
#define NI_LOGAN_MIN_INTRA_PERIOD 0

/*Values below used for timeout checking*/
#define NI_LOGAN_MAX_SESSION_OPEN_RETRIES 20
#define NI_LOGAN_SESSION_OPEN_RETRY_INTERVAL_US 200

#define NI_LOGAN_MAX_ENC_SESSION_OPEN_QUERY_RETRIES       3000
#define NI_LOGAN_ENC_SESSION_OPEN_RETRY_INTERVAL_US       1000

#define NI_LOGAN_MAX_ENC_SESSION_WRITE_QUERY_RETRIES      2000
#define NI_LOGAN_MAX_ENC_SESSION_READ_QUERY_RETRIES       3000

#define NI_LOGAN_MAX_DEC_SESSION_WRITE_QUERY_RETRIES      100
#define NI_LOGAN_MAX_DEC_SESSION_READ_QUERY_RETRIES       3000
#define NI_LOGAN_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES   15000

#define NI_LOGAN_MAX_SESSION_CLOSE_RETRIES        10
#define NI_LOGAN_SESSION_CLOSE_RETRY_INTERVAL_US  500000

#define NI_LOGAN_RETRY_INTERVAL_100US  100
#define NI_LOGAN_RETRY_INTERVAL_200US  200

// Number of pixels for main stream resolutions
#define NI_LOGAN_NUM_OF_PIXELS_720P          (1280*720)

/*Values below used for VPU resolution range checking*/
#define NI_LOGAN_MAX_WIDTH 8192
#define NI_LOGAN_MIN_WIDTH 256
#define NI_LOGAN_MAX_HEIGHT 8192
#define NI_LOGAN_MIN_HEIGHT 128

/*Values below used for parameter resolution range checking*/
#define NI_LOGAN_PARAM_MAX_WIDTH 8192
#define NI_LOGAN_PARAM_MIN_WIDTH 32
#define NI_LOGAN_PARAM_MAX_HEIGHT 8192
#define NI_LOGAN_PARAM_MIN_HEIGHT 32

#define NI_LOGAN_MAX_GOP_SIZE 8
#define NI_LOGAN_MIN_GOP_SIZE 1
#define NI_LOGAN_MAX_GOP_PRESET_IDX 9
#define NI_LOGAN_MIN_GOP_PRESET_IDX 0
#define NI_LOGAN_MAX_DECODING_REFRESH_TYPE 2
#define NI_LOGAN_MIN_DECODING_REFRESH_TYPE 0
#define NI_LOGAN_DEFAULT_CU_SIZE_MODE 7
#define NI_LOGAN_MAX_DYNAMIC_MERGE 1
#define NI_LOGAN_MIN_DYNAMIC_MERGE 0
#define NI_LOGAN_MAX_USE_RECOMMENDED_ENC_PARAMS 3
#define NI_LOGAN_MIN_USE_RECOMMENDED_ENC_PARAMS 0
#define NI_LOGAN_MAX_MAX_NUM_MERGE 3
#define NI_LOGAN_MIN_MAX_NUM_MERGE 0
#define NI_LOGAN_MAX_INTRA_QP 51
#define NI_LOGAN_MIN_INTRA_QP 0
#define NI_LOGAN_DEFAULT_INTRA_QP 22
#define NI_LOGAN_INTRA_QP_RANGE 25
#define NI_LOGAN_QP_MID_POINT 26
#define NI_LOGAN_MAX_MAX_QP 51
#define NI_LOGAN_MIN_MAX_QP 0
#define NI_LOGAN_MAX_MIN_QP 51
#define NI_LOGAN_MIN_MIN_QP 0
#define NI_LOGAN_DEFAULT_MAX_QP 51
#define NI_LOGAN_DEFAULT_MIN_QP 8
#define NI_LOGAN_MAX_MAX_DELTA_QP 51
#define NI_LOGAN_MIN_MAX_DELTA_QP 0
#define NI_LOGAN_DEFAULT_MAX_DELTA_QP 10
#define NI_LOGAN_MAX_BIN 1
#define NI_LOGAN_MIN_BIN 0
#define NI_LOGAN_MAX_NUM_SESSIONS 32
#define NI_LOGAN_MAX_CRF 51
#define NI_LOGAN_MIN_CRF  0
#define NI_LOGAN_MIN_INTRA_REFRESH_MIN_PERIOD    0
#define NI_LOGAN_MAX_INTRA_REFRESH_MIN_PERIOD 8191
#define NI_LOGAN_MAX_KEEP_ALIVE_TIMEOUT          100
#define NI_LOGAN_MIN_KEEP_ALIVE_TIMEOUT          1
#define NI_LOGAN_DEFAULT_KEEP_ALIVE_TIMEOUT      3
#define NI_LOGAN_MIN_CUSTOM_SEI_PASSTHRU        (-1)
#define NI_LOGAN_MAX_CUSTOM_SEI_PASSTHRU         1000
#define NI_LOGAN_MIN_PRIORITY                    0
#define NI_LOGAN_MAX_PRIORITY                    1
#define NI_LOGAN_DISABLE_USR_DATA_SEI_PASSTHRU       0
#define NI_LOGAN_ENABLE_USR_DATA_SEI_PASSTHRU       1
#define NI_LOGAN_DISABLE_CHECK_PACKET                0
#define NI_LOGAN_ENABLE_CHECK_PACKET                1

#define RC_SUCCESS           true
#define RC_ERROR             false

#define BEST_DEVICE_INST_STR  "bestinst"
#define BEST_MODEL_LOAD_STR   "bestload"
#define LIST_DEVICES_STR  "list"
#define LOGAN_MAX_CHAR_IN_DEVICE_NAME 32

#define LOGAN_MAX_NUM_FRAMEPOOL_HWAVFRAME 64

// Picked from the xcoder firmware, commit e3b882e7
#define NI_LOGAN_VPU_CEIL(_data, _align)     (((_data)+(_align-1))&~(_align-1))
#define NI_LOGAN_VPU_ALIGN4(_x)              (((_x)+0x03)&~0x03)
#define NI_LOGAN_VPU_ALIGN8(_x)              (((_x)+0x07)&~0x07)
#define NI_LOGAN_VPU_ALIGN16(_x)             (((_x)+0x0f)&~0x0f)
#define NI_LOGAN_VPU_ALIGN32(_x)             (((_x)+0x1f)&~0x1f)
#define NI_LOGAN_VPU_ALIGN64(_x)             (((_x)+0x3f)&~0x3f)
#define NI_LOGAN_VPU_ALIGN128(_x)            (((_x)+0x7f)&~0x7f)
#define NI_LOGAN_VPU_ALIGN256(_x)            (((_x)+0xff)&~0xff)
#define NI_LOGAN_VPU_ALIGN512(_x)            (((_x)+0x1ff)&~0x1ff)
#define NI_LOGAN_VPU_ALIGN2048(_x)           (((_x)+0x7ff)&~0x7ff)
#define NI_LOGAN_VPU_ALIGN4096(_x)           (((_x)+0xfff)&~0xfff)
#define NI_LOGAN_VPU_ALIGN16384(_x)          (((_x)+0x3fff)&~0x3fff)

typedef struct _ni_logan_sei_user_data_entry
{
  uint32_t offset;
  uint32_t size;
} ni_logan_sei_user_data_entry_t;

typedef enum
{
  NI_LOGAN_H265_USERDATA_FLAG_RESERVED_0 = 0,
  NI_LOGAN_H265_USERDATA_FLAG_RESERVED_1 = 1,
  NI_LOGAN_H265_USERDATA_FLAG_VUI = 2,
  NI_LOGAN_H265_USERDATA_FLAG_ALTERNATIVE_TRANSFER_CHARACTERISTICS = 3,
  NI_LOGAN_H265_USERDATA_FLAG_PIC_TIMING = 4,
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE = 5, /* SEI Prefix: user_data_registered_itu_t_t35 */
  NI_LOGAN_H265_USERDATA_FLAG_UNREGISTERED_PRE = 6, /* SEI Prefix: user_data_unregistered */
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF = 7, /* SEI Suffix: user_data_registered_itu_t_t35 */
  NI_LOGAN_H265_USERDATA_FLAG_UNREGISTERED_SUF = 8, /* SEI Suffix: user_data_unregistered */
  NI_LOGAN_H265_USERDATA_FLAG_RESERVED_9 = 9, /* SEI RESERVED */
  NI_LOGAN_H265_USERDATA_FLAG_MASTERING_COLOR_VOL = 10, /* SEI Prefix: mastering_display_color_volume */
  NI_LOGAN_H265_USERDATA_FLAG_CHROMA_RESAMPLING_FILTER_HINT = 11, /* SEI Prefix: chroma_resampling_filter_hint */
  NI_LOGAN_H265_USERDATA_FLAG_KNEE_FUNCTION_INFO = 12, /* SEI Prefix: knee_function_info */
  NI_LOGAN_H265_USERDATA_FLAG_TONE_MAPPING_INFO  = 13,  /* SEI Prefix: tone_mapping_info */
  NI_LOGAN_H265_USER_DATA_FLAG_FILM_GRAIN_CHARACTERISTICS_INFO = 14,  /* SEI Prefix: film_grain_characteristics_info */
  NI_LOGAN_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO  = 15,  /* SEI Prefix: content_light_level_info */
  NI_LOGAN_H265_USER_DATA_FLAG_COLOUR_REMAPPING_INFO  = 16,  /* SEI Prefix: content_light_level_info */
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE_1 = 28, /* SEI Prefix: additional user_data_registered_itu_t_t35 */
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE_2 = 29, /* SEI Prefix: additional user_data_registered_itu_t_t35 */
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF_1 = 30, /* SEI Suffix: additional user_data_registered_itu_t_t35 */
  NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF_2 = 31, /* SEI Suffix: additional user_data_registered_itu_t_t35 */
} ni_logan_h265_sei_user_data_type_t;

typedef enum
{
  LOGAN_PIC_TYPE_I = 0,                    /*!*< I picture */
  LOGAN_PIC_TYPE_P = 1,                    /*!*< P picture */
  LOGAN_PIC_TYPE_B = 2,                    /*!*< B picture (except VC1) */
  LOGAN_PIC_TYPE_FORCE_IDR = 3,           /*!*< force IDR frame when encoding */

  LOGAN_PIC_TYPE_IDR = 5,                  /*!*< H.264/H.265 IDR picture */
  LOGAN_PIC_TYPE_CRA = 6,                  /*!*< H.265 CRA picture */
  LOGAN_PIC_TYPE_MAX                       /*!*< No Meaning */
} ni_logan_pic_type_t;

typedef enum
{
  NI_LOGAN_PIX_FMT_YUV420P     = 0,   /* 8-bit YUV420 planar       */
  NI_LOGAN_PIX_FMT_YUV420P10LE = 1,   /* 10-bit YUV420 planar      */
} ni_logan_pix_fmt_t;

// Maximum SEI sizes for supported types for encoder and decoder
#define NI_LOGAN_ENC_MAX_SEI_BUF_SIZE        NI_LOGAN_VPU_ALIGN16(1024) //1024
#define NI_LOGAN_MAX_SEI_ENTRIES      32
// 32 user_data_entry_t records + various SEI messages sizes
#define NI_LOGAN_MAX_SEI_DATA     NI_LOGAN_VPU_ALIGN8(NI_LOGAN_MAX_SEI_ENTRIES * sizeof(ni_logan_sei_user_data_entry_t) + 1024) // 1280

#define NI_LOGAN_DEC_MAX_CC_BUF_SIZE  93      // max 31 CC entries of 3 bytes each

#ifndef QUADRA

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
typedef enum _ni_logan_frame_aux_data_type
{
  NI_FRAME_AUX_DATA_NONE   = 0,

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
  // ni_logan_frame_aux_data.size / sizeof(ni_region_of_interest_t)
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

  // NETINT: long term reference frame support, which is a struct of
  // ni_long_term_ref_t that specifies a frame's support of long term
  // reference frame.
  NI_FRAME_AUX_DATA_LONG_TERM_REF,

} ni_aux_data_type_t;

// rational number (pair of numerator and denominator)
typedef struct _ni_logan_rational
{
  int num; // numerator
  int den; // denominator
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
  return a.num / (double) a.den;
}

// struct to hold auxiliary data for ni_logan_frame_t
typedef struct _ni_logan_aux_data
{
  ni_aux_data_type_t type;
  uint8_t *data;
  int      size;
} ni_aux_data_t;

// struct describing a Region Of Interest (ROI)
typedef struct _ni_logan_region_of_interest
{
  // self size: must be set to: sizeof(ni_region_of_interest_t)
  uint32_t self_size;

  // ROI rectangle: pixels from the frame's top edge to the top and bottom edges
  // of the rectangle, from the frame's left edge to the left and right edges
  // of the rectangle.
  int top;
  int bottom;
  int left;
  int right;

  // quantisation offset: [-1, +1], 0 means no quality change; < 0 value asks
  // for better quality (less quantisation), > 0 value asks for worse quality
  // (greater quantisation).
  ni_rational_t qoffset;
} ni_region_of_interest_t;

// struct describing long term reference frame support.
typedef struct _ni_logan_long_term_ref
{
  // A flag for the current picture to be used as a long term reference
  // picture later at other pictures' encoding
  uint8_t use_cur_src_as_long_term_pic;

  // A flag to use a long term reference picture in DPB when encoding the
  // current picture
  uint8_t use_long_term_ref;
} ni_long_term_ref_t;

#endif // #ifndef QUADRA

/*!*
* \brief  encoder HEVC ROI custom map (1 CTU = 64bits)
*/
typedef union _ni_logan_enc_hevc_roi_custom_map
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
} ni_logan_enc_hevc_roi_custom_map_t;

/*!*
* \brief  encoder AVC ROI custom map (1 MB = 8bits)
*/
typedef union _ni_logan_enc_avc_roi_custom_map
{
  struct
  {
    uint8_t  mb_force_mode  :  2; // [ 1: 0]
    uint8_t  mb_qp          :  6; // [ 7: 2]
  } field;
} ni_logan_enc_avc_roi_custom_map_t;

/*!*
* \brief This is an enumeration for supported codec formats.
*/
typedef enum _ni_logan_codec_format
{
  NI_LOGAN_CODEC_FORMAT_H264 = 0,
  NI_LOGAN_CODEC_FORMAT_H265 = 1

} ni_logan_codec_format_t;

/*!*
* \brief This is an enumeration for hw actions
*/
typedef enum _ni_logan_codec_hw_actions
{
  NI_LOGAN_CODEC_HW_NONE = 0,
  NI_LOGAN_CODEC_HW_ENABLE = (1 << 0),
  NI_LOGAN_CODEC_HW_DOWNLOAD = (1 << 1),
  NI_LOGAN_CODEC_HW_UPLOAD = (1 << 2),
  NI_LOGAN_CODEC_HW_RSVD = (1 << 3)
} ni_logan_codec_hw_actions_t;

/*!*
* \brief This is an enumeration for encoder parameter change.
*/
typedef enum _ni_logan_param_change_flags
{
  // COMMON parameters which can be changed frame by frame.
  NI_LOGAN_SET_CHANGE_PARAM_PPS                 = (1 << 0),
  NI_LOGAN_SET_CHANGE_PARAM_INTRA_PARAM         = (1 << 1),
  NI_LOGAN_SET_CHANGE_PARAM_RC_TARGET_RATE      = (1 << 8),
  NI_LOGAN_SET_CHANGE_PARAM_RC                  = (1 << 9),
  NI_LOGAN_SET_CHANGE_PARAM_RC_MIN_MAX_QP       = (1 << 10),
  NI_LOGAN_SET_CHANGE_PARAM_RC_BIT_RATIO_LAYER  = (1 << 11),
  NI_LOGAN_SET_CHANGE_PARAM_INDEPEND_SLICE      = (1 << 16),
  NI_LOGAN_SET_CHANGE_PARAM_DEPEND_SLICE        = (1 << 17),
  NI_LOGAN_SET_CHANGE_PARAM_RDO                 = (1 << 18),
  NI_LOGAN_SET_CHANGE_PARAM_NR                  = (1 << 19),
  NI_LOGAN_SET_CHANGE_PARAM_BG                  = (1 << 20),
  NI_LOGAN_SET_CHANGE_PARAM_CUSTOM_MD           = (1 << 21),
  NI_LOGAN_SET_CHANGE_PARAM_CUSTOM_LAMBDA       = (1 << 22),
  NI_LOGAN_SET_CHANGE_PARAM_RC2                 = (1 << 23),
  NI_LOGAN_SET_CHANGE_PARAM_VUI_HRD_PARAM       = (1 << 24),

} ni_logan_param_change_flags_t;

/**
* @brief This is a data structure for encoding parameters that have changed.
*/
typedef struct _ni_logan_encoder_change_params_t
{
  uint32_t enable_option;

  // NI_LOGAN_SET_CHANGE_PARAM_RC_TARGET_RATE
  int32_t bitRate;                        /**< A target bitrate when separateBitrateEnable is 0 */


  // NI_LOGAN_SET_CHANGE_PARAM_RC
// (rcEnable, cuLevelRc, bitAllocMode, RoiEnable, RcInitQp can't be changed while encoding)
  int32_t hvsQPEnable;                    /**< It enables CU QP adjustment for subjective quality enhancement. */
  int32_t hvsQpScale;                     /**< QP scaling factor for CU QP adjustment when hvcQpenable is 1. */
  int32_t vbvBufferSize;                  /**< Specifies the size of the VBV buffer in msec (10 ~ 3000).
                                               For example, 3000 should be set for 3 seconds.
                                               This value is valid when rcEnable is 1.
                                               VBV buffer size in bits is EncBitrate * VbvBufferSize / 1000. */
  int32_t mbLevelRcEnable;                /**< (for H.264 encoder) */
  int32_t fillerEnable;       /**< enables filler data for strict rate control*/

  // NI_LOGAN_SET_CHANGE_PARAM_RC_MIN_MAX_QP
  int32_t minQpI;                         /**< A minimum QP of I picture for rate control */
  int32_t maxQpI;                         /**< A maximum QP of I picture for rate control */
  int32_t maxDeltaQp;                     /**< A maximum delta QP for rate control */


  int32_t minQpP;                         /**< A minimum QP of P picture for rate control */
  int32_t minQpB;                         /**< A minimum QP of B picture for rate control */
  int32_t maxQpP;                         /**< A maximum QP of P picture for rate control */
  int32_t maxQpB;                         /**< A maximum QP of B picture for rate control */

  // NI_LOGAN_SET_CHANGE_PARAM_INTRA_PARAM
  int32_t intraQP;                        /**< A quantization parameter of intra picture */
  int32_t intraPeriod;                    /**< A period of intra picture in GOP size */
  int32_t repeatHeaders; /**< When enabled, encoder repeats the VPS/SPS/PPS headers on I-frames */

  // NI_LOGAN_SET_CHANGE_PARAM_VUI_HRD_PARAM
  uint32_t encodeVuiRbsp;        /**< A flag to encode the VUI syntax in rbsp */
  uint32_t vuiDataSizeBits;       /**< The bit size of the VUI rbsp data */
  uint32_t vuiDataSizeBytes;      /**< The byte size of the VUI rbsp data */
  uint8_t  vuiRbsp[NI_LOGAN_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/

  int32_t reserved[16];          // reserved bytes to make struct size 8-align
} ni_logan_encoder_change_params_t; // 176 bytes (has to be 8 byte aligned)

/*!*
* \brief decoded payload format of HDR SEI mastering display colour volume
*
*/
typedef struct _ni_logan_dec_mastering_display_colour_volume
{
  uint32_t   display_primaries_x[3];
  uint32_t   display_primaries_y[3];
  uint32_t   white_point_x                   : 16;
  uint32_t   white_point_y                   : 16;
  uint32_t   max_display_mastering_luminance : 32;
  uint32_t   min_display_mastering_luminance : 32;
} ni_logan_dec_mastering_display_colour_volume_t;

typedef struct _ni_logan_dec_win
{
  int16_t left;
  int16_t right;
  int16_t top;
  int16_t bottom;
} ni_logan_dec_win_t;

/*!*
* \brief decoded payload format of H.265 VUI
*
*/
typedef struct _ni_logan_dec_h265_vui_param
{
  uint8_t    aspect_ratio_info_present_flag;
  uint8_t    aspect_ratio_idc;
  uint8_t    overscan_info_present_flag;
  uint8_t    overscan_appropriate_flag;

  uint8_t    video_signal_type_present_flag;
  int8_t     video_format;

  uint8_t    video_full_range_flag;
  uint8_t    colour_description_present_flag;

  uint16_t   sar_width;
  uint16_t   sar_height;

  uint8_t    colour_primaries;
  uint8_t    transfer_characteristics;
  uint8_t    matrix_coefficients;

  uint8_t    chroma_loc_info_present_flag;
  int8_t     chroma_sample_loc_type_top_field;
  int8_t     chroma_sample_loc_type_bottom_field;

  uint8_t    neutral_chroma_indication_flag;

  uint8_t    field_seq_flag;

  uint8_t    frame_field_info_present_flag;
  uint8_t    default_display_window_flag;
  uint8_t    vui_timing_info_present_flag;
  uint8_t    vui_poc_proportional_to_timing_flag;

  uint32_t   vui_num_units_in_tick;
  uint32_t   vui_time_scale;

  uint8_t    vui_hrd_parameters_present_flag;
  uint8_t    bitstream_restriction_flag;

  uint8_t    tiles_fixed_structure_flag;
  uint8_t    motion_vectors_over_pic_boundaries_flag;
  uint8_t    restricted_ref_pic_lists_flag;
  int8_t     min_spatial_segmentation_idc;
  int8_t     max_bytes_per_pic_denom;
  int8_t     max_bits_per_mincu_denom;

  int16_t    vui_num_ticks_poc_diff_one_minus1;
  int8_t     log2_max_mv_length_horizontal;
  int8_t     log2_max_mv_length_vertical;

  ni_logan_dec_win_t    def_disp_win;

} ni_logan_dec_h265_vui_param_t;

/*!*
* \brief decoded payload format of H.264 VUI
*
*/
typedef struct _ni_logan_dec_h264_vui_param
{
  uint8_t  aspect_ratio_info_present_flag;
  uint8_t  aspect_ratio_idc;
  uint8_t  overscan_info_present_flag;
  uint8_t  overscan_appropriate_flag;

  uint8_t  video_signal_type_present_flag;
  int8_t   video_format;
  uint8_t  video_full_range_flag;
  uint8_t  colour_description_present_flag;

  uint16_t sar_width;
  uint16_t sar_height;

  uint8_t  colour_primaries;
  uint8_t  transfer_characteristics;
  uint8_t  matrix_coefficients;
  uint8_t  chroma_loc_info_present_flag;

  int8_t   chroma_sample_loc_type_top_field;
  int8_t   chroma_sample_loc_type_bottom_field;

  uint8_t  vui_timing_info_present_flag;
  uint8_t  fixed_frame_rate_flag;

  uint32_t vui_num_units_in_tick;
  uint32_t vui_time_scale;

  uint8_t  nal_hrd_parameters_present_flag;
  uint8_t  vcl_hrd_parameters_present_flag;
  uint8_t  low_delay_hrd_flag;
  uint8_t  pic_struct_present_flag;

  uint8_t  bitstream_restriction_flag;
  uint8_t  motion_vectors_over_pic_boundaries_flag;
  int8_t   max_bytes_per_pic_denom;
  int8_t   max_bits_per_mincu_denom;

  int8_t   log2_max_mv_length_horizontal;
  int8_t   log2_max_mv_length_vertical;
  int8_t   max_num_reorder_frames;
  int8_t   max_dec_frame_buffering;
} ni_logan_dec_h264_vui_param_t;

/*!*
* \brief payload format of HDR SEI content light level info
*
*/
typedef struct _ni_logan_content_light_level_info
{
  uint16_t max_content_light_level;
  uint16_t max_pic_average_light_level;
} ni_logan_content_light_level_info_t;

/*!*
* \brief encoded payload format of HDR SEI mastering display colour volume
*
*/
typedef struct _ni_logan_enc_mastering_display_colour_volume
{
  uint16_t   display_primaries[3][2];
  uint16_t   white_point_x;
  uint16_t   white_point_y;
  uint32_t   max_display_mastering_luminance;
  uint32_t   min_display_mastering_luminance;
} ni_logan_enc_mastering_display_colour_volume_t;

/*!*
* \brief This is an enumeration for illustrating the custom SEI locations.
*/
typedef enum _ni_logan_custom_sei_location
{
  NI_LOGAN_CUSTOM_SEI_LOC_BEFORE_VCL = 0,
  NI_LOGAN_CUSTOM_SEI_LOC_AFTER_VCL = 1
} ni_logan_custom_sei_location_t;

/*!*
* \brief custom sei payload passthrough
*
*/
typedef struct _ni_logan_custom_sei
{
  uint8_t custom_sei_type;
  ni_logan_custom_sei_location_t custom_sei_loc;
  uint32_t custom_sei_size;
  uint8_t custom_sei_data[NI_LOGAN_MAX_CUSTOM_SEI_SZ];
}ni_logan_custom_sei_t;

typedef struct _ni_logan_all_custom_sei
{
  uint8_t custom_sei_cnt;
  ni_logan_custom_sei_t ni_custom_sei[NI_LOGAN_MAX_CUSTOM_SEI_CNT];
}ni_logan_all_custom_sei_t;

/*!*
* \brief hardware capability type
*/
typedef struct _ni_logan_hw_capability
{
  uint8_t reserved; //alignment
  uint8_t hw_id;
  uint8_t max_number_of_contexts;
  uint8_t max_1080p_fps;
  uint8_t codec_format;
  uint8_t codec_type;
  uint16_t max_video_width;
  uint16_t max_video_height;
  uint16_t min_video_width;
  uint16_t min_video_height;
  uint8_t video_profile;
  uint8_t video_level;
} ni_logan_hw_capability_t;

/*!*
* \brief  device capability type
*/
typedef struct _ni_logan_device_capability
{
  uint8_t device_is_xcoder;
  uint8_t hw_elements_cnt;
  uint8_t h264_decoders_cnt;
  uint8_t h264_encoders_cnt;
  uint8_t h265_decoders_cnt;
  uint8_t h265_encoders_cnt;
  // firmware revision, a space right filled ASCII array, not a string
  uint8_t fw_rev[8];
  ni_logan_hw_capability_t xcoder_devices[NI_LOGAN_MAX_DEVICES_PER_HW_INSTANCE];
  uint8_t fw_commit_hash[41];
  uint8_t fw_commit_time[26];
  uint8_t fw_branch_name[256];
} ni_logan_device_capability_t;

/*!*
* \brief Session running state type.
*/
typedef enum _ni_logan_session_run_state
{
  LOGAN_SESSION_RUN_STATE_NORMAL = 0,
  LOGAN_SESSION_RUN_STATE_SEQ_CHANGE_DRAINING = 1,
  LOGAN_SESSION_RUN_STATE_RESETTING = 2,
  LOGAN_SESSION_RUN_STATE_QUEUED_FRAME_DRAINING = 3,
} ni_logan_session_run_state_t;


typedef struct _ni_logan_context_query
{
  uint32_t context_id : 8;     //07:00  SW Instance ID (0 to Max number of instances)
  uint32_t context_status : 8; //15:08  Instance Status (0-Idle, 1-Active)
  uint32_t codec_format : 8;   //23:16  Codec Format (0-H264, 1-H265)
  uint32_t video_width : 16;   //39:24  Video Width (0 to Max Width)
  uint32_t video_height : 16;  //55:40  Video Height (0 to Max Height)
  uint32_t fps : 8;            //63:56  FPS (0 to 255)
  uint32_t reserved : 8;       //Alignment
} ni_logan_context_query_t;

typedef struct _ni_logan_load_query
{
  uint32_t current_load;
  uint32_t fw_model_load;
  uint32_t fw_video_mem_usage;
  uint32_t total_contexts;
  ni_logan_context_query_t context_status[NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE];
} ni_logan_load_query_t;

typedef struct _ni_logan_thread_arg_struct_t
{
  int hw_id;                              // Codec ID
  uint32_t session_id;                    // session id
  uint64_t session_timestamp;             // Session Start Timestamp
  bool close_thread;                      // a flag that the keep alive thread is closed or need to be closed
  uint32_t device_type;                   // Device Type, Either NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
  ni_device_handle_t device_handle;       // block device handler
  ni_event_handle_t thread_event_handle;  // only for Windows asynchronous read and write
  void *p_buffer;                         // only be used when regular i/o
  uint32_t keep_alive_timeout;            // keep alive timeout setting
} ni_logan_thread_arg_struct_t;


typedef struct _ni_logan_buf_t
{
  void* buf;
  struct _ni_logan_buf_pool_t* pool;
  struct  _ni_logan_buf_t *p_prev;
  struct  _ni_logan_buf_t *p_next;
  struct  _ni_logan_buf_t *p_previous_buffer;
  struct  _ni_logan_buf_t *p_next_buffer;
} ni_logan_buf_t;

typedef struct _ni_logan_buf_pool_t
{
  ni_pthread_mutex_t mutex;
  uint32_t number_of_buffers;
  uint32_t buf_size;
  ni_logan_buf_t *p_free_head;
  ni_logan_buf_t *p_free_tail;
  ni_logan_buf_t *p_used_head;
  ni_logan_buf_t *p_used_tail;
} ni_logan_buf_pool_t;

typedef struct _ni_logan_serial_num_t
{
  uint8_t ai8Sn[20];
} ni_logan_serial_num_t;

typedef struct _ni_logan_queue_node_t
{
  uint64_t timestamp;
  uint64_t frame_info;
  time_t   checkout_timestamp;
  struct _ni_logan_queue_node_t *p_prev;
  struct _ni_logan_queue_node_t *p_next;
  struct _ni_logan_queue_node_t *p_previous_buffer;
  struct _ni_logan_queue_node_t *p_next_buffer;
} ni_logan_queue_node_t;

typedef struct _ni_logan_queue_buffer_pool_t
{
  uint32_t number_of_buffers; // total number of buffers
  ni_logan_queue_node_t *p_free_head;
  ni_logan_queue_node_t *p_free_tail;
  ni_logan_queue_node_t *p_used_head;
  ni_logan_queue_node_t *p_used_tail;
} ni_logan_queue_buffer_pool_t;

typedef struct _ni_logan_session_context
{
  /*! LATENCY MEASUREMENT queue */
  ni_logan_lat_meas_q_t *frame_time_q;
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
  uint8_t ui8_light_level_data[NI_LOGAN_LIGHT_LEVEL_DATA_SZ];
  int sei_hdr_mastering_display_color_vol_len;
  int mdcv_max_min_lum_data_len;
  uint8_t ui8_mdcv_max_min_lum_data[NI_LOGAN_MDCV_LUM_DATA_SZ];
  void *p_master_display_meta_data;
  uint8_t preferred_characteristics_data;

  /*! frame pts calculation: for decoder */
  int is_sequence_change;
  int64_t last_pts;
  int64_t last_dts;
  int64_t last_dts_interval;
  int64_t enc_pts_list[NI_LOGAN_FIFO_SZ];
  int64_t enc_pts_r_idx;
  int64_t enc_pts_w_idx;
  int pts_correction_num_faulty_dts;
  int64_t pts_correction_last_dts;
  int pts_correction_num_faulty_pts;
  int64_t pts_correction_last_pts;
  /* store pts values to create an accurate pts offset */
  int pic_reorder_delay;
  int flags_array[NI_LOGAN_FIFO_SZ];
  int64_t pts_offsets[NI_LOGAN_FIFO_SZ];
  uint64_t pkt_index;
  uint64_t pkt_offsets_index_min[NI_LOGAN_FIFO_SZ];
  uint64_t pkt_offsets_index[NI_LOGAN_FIFO_SZ];
  ni_logan_all_custom_sei_t *pkt_custom_sei[NI_LOGAN_FIFO_SZ];

  /*! if session is on a decoder handling incoming pkt 512-aligned */
  int is_dec_pkt_512_aligned;

  /*! Device Card ID */
  ni_device_handle_t device_handle;
  /*! block device fd */
  ni_device_handle_t blk_io_handle;

  /*! Sender information for YUV bypass mode*/
  // sender_handle is device handle of uploader or decoder.
  ni_device_handle_t sender_handle;
  // default is invalid, if need download back yuv, equals to sender_handle
  ni_device_handle_t auto_dl_handle;

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
  /*! Device Type, Either NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER */
  uint32_t device_type;
  /*! Device Type, Either NI_LOGAN_CODEC_FORMAT_H264 or NI_LOGAN_CODEC_FORMAT_H265 */
  uint32_t codec_format;
  /*! From the user command, which device allocation method we use */
  char dev_xcoder[LOGAN_MAX_CHAR_IN_DEVICE_NAME];
  /*! the device name that opened*/
  char dev_xcoder_name[LOGAN_MAX_CHAR_IN_DEVICE_NAME];
  /*! the block name that opened */
  char blk_xcoder_name[LOGAN_MAX_CHAR_IN_DEVICE_NAME];

  ni_logan_serial_num_t d_serial_number; /*Serial number of card (dec or uploader) in use*/
  ni_logan_serial_num_t e_serial_number; /*Serial number of card (enc) in use*/

  int src_bit_depth;         // 8 or 10 bits/pixel formats, default 8
  int src_endian;            // encoding 0: little endian (default) 1: big
  int bit_depth_factor;      // for YUV buffer allocation
  // for encoder roi metadata
  uint32_t roi_len;
  uint32_t roi_avg_qp;

  /*! Context Query */
  ni_logan_load_query_t load_query;
  /*! session metrics including frame statistics */
  ni_logan_instance_status_info_t session_stats;

  /*! Leftover Buffer */
  void *p_leftover;
  int prev_size;
  uint32_t sent_size;

  /*! for decoder: buffer for stream header */
  uint8_t *p_hdr_buf;
  uint8_t hdr_buf_size;

  /*! for decoder: buffer for lone SEI as a single packet, to be sent along
    with next frame */
  uint8_t buf_lone_sei[NI_LOGAN_MAX_SEI_DATA];
  int lone_sei_size;

  /*! PTS Table */
  void *pts_table;

  /*! DTS Queue */
  void *dts_queue;

  /*! keep alive timeout */
  uint32_t keep_alive_timeout;
  uint32_t set_high_priority;
  /*! Other */
  int status;
  int key_frame_type;

  void *p_dump[2];
  char param_err_msg[512];

  int keyframe_factor;
  uint64_t frame_num;
  uint64_t pkt_num;
  int rc_error_count;

  // write packet/frame required buf size
  uint32_t required_buf_size;

  // frame forcing: for encoding
  int force_frame_type;

  uint32_t ready_to_close; //flag to indicate we are ready to close session

  // session running state
  ni_logan_session_run_state_t session_run_state;
  uint32_t active_video_width;  //Current video width. this is used to do sequence change
  uint32_t active_video_height; //Current video height ,this is used to do sequence change
  uint32_t active_bit_depth; //Current bit depth, this is used to do sequence change
  ni_pthread_t keep_alive_thread;
  ni_logan_thread_arg_struct_t *keep_alive_thread_args;
  ni_logan_queue_buffer_pool_t *buffer_pool;
  ni_logan_buf_pool_t *dec_fme_buf_pool;
  uint32_t needs_dealoc; //This is set to indicate that the context was dynamically allocated and needs to be freed

  uint64_t codec_total_ticks;
  uint64_t codec_start_time;

  // only be used when regular i/o
  void *p_all_zero_buf; //This is for sos, eos, flush and keep alive request
  void *p_dec_packet_inf_buf;

  // these two event handle are only for Windows asynchronous read and write now
  ni_event_handle_t event_handle;
  ni_event_handle_t thread_event_handle;

  // decoder lowDelay mode for All I packets or IPPP packets
  int decoder_low_delay;
  // decoder lowDelay mode drop frame number
  uint64_t decoder_drop_frame_num;

  void *session_info;

  // ROI data
  int roi_side_data_size;
  ni_region_of_interest_t *av_rois;  // last passed in ni_region_of_interest_t
  int nb_rois;
  ni_logan_enc_avc_roi_custom_map_t *avc_roi_map; // actual AVC/HEVC map(s)
  uint8_t *hevc_sub_ctu_roi_buf;
  ni_logan_enc_hevc_roi_custom_map_t *hevc_roi_map;

  // encoder reconfig parameters
  ni_logan_encoder_change_params_t *enc_change_params;

  // path of decoder input pkt saving folder
  char stream_dir_name[128];
  // hw yuvbypass
  int hw_action;
  int pixel_format;

  ///Params used in VFR mode Start///
  /*! Numerator and denominator of frame rate,
  * used framerate change for VFR mode*/
  uint32_t prev_fps;
  /*! The last setting bitrate in the VFR mode*/
  int      prev_bitrate;
  int      init_bitrate;
  uint64_t prev_pts;
  uint32_t last_change_framenum;
  uint32_t fps_change_detect_count;
  uint32_t count_frame_num_in_sec;
  uint32_t passed_time_in_timebase_unit;

  uint32_t ui32timing_scale;  // only used for the vfr, initialize the vui
  uint32_t ui32num_unit_in_tick;// only used for the vfr, initialize the vui
  ///Params used in VFR mode Done///

  // for decoder: store currently returned decoded frame's pkt offset
  uint64_t frame_pkt_offset;

  // device session open/close/read/write session mutex
  ni_pthread_mutex_t mutex;
} ni_logan_session_context_t;

/*!*
* \brief This is an enumeration for encoder reconfiguration test settings
*/
typedef enum _ni_logan_reconfig
{
  LOGAN_XCODER_TEST_RECONF_OFF = 0,
  LOGAN_XCODER_TEST_RECONF_BR = 1,
  LOGAN_XCODER_TEST_RECONF_INTRAPRD = 2,
  LOGAN_XCODER_TEST_RECONF_VUI_HRD = 3,
  LOGAN_XCODER_TEST_RECONF_LONG_TERM_REF = 4,
  LOGAN_XCODER_TEST_RECONF_RC            = 5,
  LOGAN_XCODER_TEST_RECONF_RC_MIN_MAX_QP = 6,
} ni_logan_reconfig_t;

#define NI_LOGAN_ENC_GOP_PARAMS_G0_PIC_TYPE       "g0picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_POC_OFFSET     "g0pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_PIC_QP         "g0picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_PIC_L0 "g0numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_POC_L0 "g0refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_POC_L1 "g0refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G0_TEMPORAL_ID    "g0temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G1_PIC_TYPE       "g1picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_POC_OFFSET     "g1pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_PIC_QP         "g1picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_PIC_L0 "g1numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_POC_L0 "g1refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_POC_L1 "g1refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G1_TEMPORAL_ID    "g1temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G2_PIC_TYPE       "g2picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_POC_OFFSET     "g2pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_PIC_QP         "g2picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_PIC_L0 "g2numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_POC_L0 "g2refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_POC_L1 "g2refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G2_TEMPORAL_ID    "g2temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G3_PIC_TYPE       "g3picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_POC_OFFSET     "g3pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_PIC_QP         "g3picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_PIC_L0 "g3numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_POC_L0 "g3refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_POC_L1 "g3refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G3_TEMPORAL_ID    "g3temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G4_PIC_TYPE       "g4picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_POC_OFFSET     "g4pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_PIC_QP         "g4picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_PIC_L0 "g4numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_POC_L0 "g4refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_POC_L1 "g4refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G4_TEMPORAL_ID    "g4temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G5_PIC_TYPE       "g5picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_POC_OFFSET     "g5pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_PIC_QP         "g5picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_PIC_L0 "g5numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_POC_L0 "g5refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_POC_L1 "g5refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G5_TEMPORAL_ID    "g5temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G6_PIC_TYPE       "g6picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_POC_OFFSET     "g6pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_PIC_QP         "g6picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_PIC_L0 "g6numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_POC_L0 "g6refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_POC_L1 "g6refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G6_TEMPORAL_ID    "g6temporalId"

#define NI_LOGAN_ENC_GOP_PARAMS_G7_PIC_TYPE       "g7picType"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_POC_OFFSET     "g7pocOffset"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_PIC_QP         "g7picQp"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_PIC_L0 "g7numRefPicL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_POC_L0 "g7refPocL0"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_POC_L1 "g7refPocL1"
#define NI_LOGAN_ENC_GOP_PARAMS_G7_TEMPORAL_ID    "g7temporalId"

typedef struct _ni_logan_gop_params
{
  int pic_type;     /*!*< A picture type of Nth picture in the custom GOP */
  int poc_offset;   /*!*< A POC of Nth picture in the custom GOP */
  int pic_qp;       /*!*< A quantization parameter of Nth picture in the custom GOP */
  int num_ref_pic_L0; /*!*< The number of reference L0 of Nth picture in the custom GOP */
  int ref_poc_L0;    /*!*< A POC of reference L0 of Nth picture in the custom GOP */
  int ref_poc_L1;    /*!*< A POC of reference L1 of Nth picture in the custom GOP */
  int temporal_id;  /*!*< A temporal ID of Nth picture in the custom GOP */
} ni_logan_gop_params_t;

#define NI_LOGAN_ENC_GOP_PARAMS_CUSTOM_GOP_SIZE "customGopSize"

typedef struct _ni_logan_custom_gop_params
{
  int custom_gop_size;                              /*!*< The size of custom GOP (0~8) */
  ni_logan_gop_params_t pic_param[NI_LOGAN_MAX_GOP_NUM]; /*!*< Picture parameters of Nth picture in custom GOP */
} ni_logan_custom_gop_params_t;

#define NI_LOGAN_ENC_REPEAT_HEADERS_FIRST_IDR    0
#define NI_LOGAN_ENC_REPEAT_HEADERS_ALL_KEY_FRAMES 1 // repeat headers for all I frames and intra refreshed key frames
#define NI_LOGAN_ENC_REPEAT_HEADERS_ALL_I_FRAMES 2 // repeat headers for all I frames

//ni_logan_h265_encoder_params defs
#define NI_LOGAN_ENC_PARAM_BITRATE                        "bitrate"
#define NI_LOGAN_ENC_PARAM_RECONF_DEMO_MODE               "ReconfDemoMode"
#define NI_LOGAN_ENC_PARAM_RECONF_FILE                    "ReconfFile"
#define NI_LOGAN_ENC_PARAM_ROI_DEMO_MODE                  "RoiDemoMode"
#define NI_LOGAN_ENC_PARAM_CACHE_ROI                      "cacheRoi"
#define NI_LOGAN_ENC_PARAM_FORCE_PIC_QP_DEMO_MODE         "ForcePicQpDemoMode"
#define NI_LOGAN_ENC_PARAM_GEN_HDRS                       "GenHdrs"
#define NI_LOGAN_ENC_PARAM_PADDING                        "padding"
#define NI_LOGAN_ENC_PARAM_FORCE_FRAME_TYPE               "forceFrameType"
#define NI_LOGAN_ENC_PARAM_PROFILE                        "profile"
#define NI_LOGAN_ENC_PARAM_LEVEL                          "level"
#define NI_LOGAN_ENC_PARAM_HIGH_TIER                      "high-tier"
#define NI_LOGAN_ENC_PARAM_LOG_LEVEL                      "log-level"
#define NI_LOGAN_ENC_PARAM_LOG                            "log"
#define NI_LOGAN_ENC_PARAM_GOP_PRESET_IDX                 "gopPresetIdx"
#define NI_LOGAN_ENC_PARAM_LOW_DELAY                      "lowDelay"
#define NI_LOGAN_ENC_PARAM_USE_RECOMMENDED_ENC_PARAMS     "useRecommendEncParam"
#define NI_LOGAN_ENC_PARAM_USE_LOW_DELAY_POC_TYPE         "useLowDelayPocType"
#define NI_LOGAN_ENC_PARAM_ENABLE_RATE_CONTROL            "RcEnable"
#define NI_LOGAN_ENC_PARAM_ENABLE_CU_LEVEL_RATE_CONTROL   "cuLevelRCEnable"
#define NI_LOGAN_ENC_PARAM_ENABLE_HVS_QP                  "hvsQPEnable"
#define NI_LOGAN_ENC_PARAM_ENABLE_HVS_QP_SCALE            "hvsQpScaleEnable"
#define NI_LOGAN_ENC_PARAM_HVS_QP_SCALE                   "hvsQpScale"
#define NI_LOGAN_ENC_PARAM_MIN_QP                         "minQp"
#define NI_LOGAN_ENC_PARAM_MAX_QP                         "maxQp"
#define NI_LOGAN_ENC_PARAM_MAX_DELTA_QP                   "maxDeltaQp"
#define NI_LOGAN_ENC_PARAM_RC_INIT_DELAY                  "RcInitDelay"
#define NI_LOGAN_ENC_PARAM_FORCED_HEADER_ENABLE           "repeatHeaders"
#define NI_LOGAN_ENC_PARAM_ROI_ENABLE                     "roiEnable"
#define NI_LOGAN_ENC_PARAM_CONF_WIN_TOP                   "confWinTop"
#define NI_LOGAN_ENC_PARAM_CONF_WIN_BOTTOM                "confWinBot"
#define NI_LOGAN_ENC_PARAM_CONF_WIN_LEFT                  "confWinLeft"
#define NI_LOGAN_ENC_PARAM_CONF_WIN_RIGHT                 "confWinRight"
#define NI_LOGAN_ENC_PARAM_INTRA_PERIOD                   "intraPeriod"
#define NI_LOGAN_ENC_PARAM_TRANS_RATE                     "transRate"
#define NI_LOGAN_ENC_PARAM_FRAME_RATE                     "frameRate"
#define NI_LOGAN_ENC_PARAM_FRAME_RATE_DENOM               "frameRateDenom"
#define NI_LOGAN_ENC_PARAM_INTRA_QP                       "intraQP"
#define NI_LOGAN_ENC_PARAM_DECODING_REFRESH_TYPE          "decodingRefreshType"
// Rev. B: H.264 only parameters.
#define NI_LOGAN_ENC_PARAM_ENABLE_8X8_TRANSFORM           "transform8x8Enable"
#define NI_LOGAN_ENC_PARAM_AVC_SLICE_MODE                 "avcSliceMode"
#define NI_LOGAN_ENC_PARAM_AVC_SLICE_ARG                  "avcSliceArg"
#define NI_LOGAN_ENC_PARAM_ENTROPY_CODING_MODE            "entropyCodingMode"
#define NI_LOGAN_ENC_PARAM_INTRA_MB_REFRESH_MODE          "intraMbRefreshMode"
#define NI_LOGAN_ENC_PARAM_INTRA_MB_REFRESH_ARG           "intraMbRefreshArg"
// Rev. B: shared between HEVC and H.264
#define NI_LOGAN_ENC_PARAM_SLICE_MODE                 "sliceMode"
#define NI_LOGAN_ENC_PARAM_SLICE_ARG                  "sliceArg"
#define NI_LOGAN_ENC_PARAM_INTRA_REFRESH_MODE             "intraRefreshMode"
#define NI_LOGAN_ENC_PARAM_INTRA_REFRESH_ARG              "intraRefreshArg"
// TBD Rev. B: could be shared for HEVC and H.264
#define NI_LOGAN_ENC_PARAM_ENABLE_MB_LEVEL_RC             "mbLevelRcEnable"
#define NI_LOGAN_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS "prefTRC"
// HRD and AUD features related
#define NI_LOGAN_ENC_PARAM_DOLBY_VISION_PROFILE           "dolbyVisionProfile"
#define NI_LOGAN_ENC_PARAM_HRD_ENABLE                     "hrdEnable"
#define NI_LOGAN_ENC_PARAM_ENABLE_AUD                     "enableAUD"
#define NI_LOGAN_ENC_PARAM_CRF                            "crf"
#define NI_LOGAN_ENC_PARAM_CBR                            "cbr"
#define NI_LOGAN_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD       "intraRefreshMinPeriod"
#define NI_LOGAN_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE     "longTermReferenceEnable"
// stricter timeout detection enable
#define NI_LOGAN_ENC_PARAM_STRICT_TIMEOUT_MODE            "strictTimeout"
#define NI_LOGAN_ENC_PARAM_LOSSLESS_ENABLE                "losslessEnable"
#define NI_LOGAN_ENC_PARAM_FLUSH_GOP                      "flushGop"
// stream color info
#define NI_LOGAN_ENC_PARAM_COLOR_PRIMARY                  "colorPri"
#define NI_LOGAN_ENC_PARAM_COLOR_TRANSFER_CHARACTERISTIC  "colorTrc"
#define NI_LOGAN_ENC_PARAM_COLOR_SPACE                    "colorSpc"
// sample aspect ratio specified in numerator/denominator
#define NI_LOGAN_ENC_PARAM_SAR_NUM                        "sarNum"
#define NI_LOGAN_ENC_PARAM_SAR_DENOM                      "sarDenom"
// video_full_range_flag
#define NI_LOGAN_ENC_PARAM_VIDEO_FULL_RANGE_FLAG          "videoFullRangeFlag"
// VFR related
#define NI_LOGAN_ENC_PARAM_ENABLE_VFR                     "enableVFR"
// HEVC explicit reference picture list
#define NI_ENC_PARAM_ENABLE_EXPLICIT_RPL                  "enableExplicitRPL"

/* Decoder parameters */
#define NI_LOGAN_DEC_PARAM_USR_DATA_SEI_PASSTHRU          "enableUserDataSeiPassthru"
#define NI_LOGAN_DEC_PARAM_CHECK_PACKET                   "checkPacket"
#define NI_LOGAN_DEC_PARAM_CUSTOM_SEI_PASSTHRU            "customSeiPassthru"
#define NI_LOGAN_DEC_PARAM_LOW_DELAY                      "lowDelay"
// data exchange save options
#define NI_LOGAN_DEC_PARAM_SAVE_PKT                       "savePkt"

/* General parameters */
#define NI_LOGAN_SET_HIGH_PRIORITY                        "setHighPriority"
#define NI_LOGAN_KEEP_ALIVE_TIMEOUT                       "keepAliveTimeout"

typedef struct _ni_logan_h265_encoder_params
{
  int profile;
  int level_idc;
  int high_tier;
  int frame_rate;
  int set_high_priority;
  int keep_alive_timeout;
  //GOP Pattern
  int gop_preset_index; /*!*< A GOP structure preset option (IPP, IBP, IBBP, IbBbP, use Custom GOP, etc)
                              0-custom 1-I-only 2-IPPP 3-IBBB 4-IBP ..... */

  // CUSTOM_GOP
  ni_logan_custom_gop_params_t custom_gop_params;

  //Preset Mode
  int use_recommend_enc_params; /*!*< 0: Custom, 1: Slow speed and best quality,
                                      2: Normal Speed and quality, 3: Fast Speed and Low Quality */

  //Encode Options

  struct
  {
    //Rate control parameters
    int enable_rate_control;              /*!*< It enable rate control */
    int enable_cu_level_rate_control;     /*!*< It enable CU level rate control */
    int enable_hvs_qp;                    /*!*< It enable CU QP adjustment for subjective quality enhancement */
    int enable_hvs_qp_scale;              /*!*< It enable QP scaling factor for CU QP adjustment when enable_hvs_qp = 1 */
    int hvs_qp_scale;                     /*!*< A QP scaling factor for CU QP adjustment when hvcQpenable = 1 */
    int min_qp;                           /*!*< A minimum QP for rate control */            //8
    int max_qp;                           /*!*< A maximum QP for rate control */            //51
    int max_delta_qp;                     /*!*< A maximum delta QP for rate control */ //10
    int trans_rate;                       /*!*< trans_rate */
    int rc_init_delay;
    int intra_qp;

    // TBD Rev. B: could be shared for HEVC and H.264 ?
    int enable_mb_level_rc;
  } rc;

  int roi_enable;

  int forced_header_enable;

  int long_term_ref_enable;

  int lossless_enable;

  //ConformanceWindowOffsets
  int conf_win_top;   /*!*< A conformance window size of TOP */
  int conf_win_bottom;   /*!*< A conformance window size of BOTTOM */
  int conf_win_left;  /*!*< A conformance window size of LEFT */
  int conf_win_right; /*!*< A conformance window size of RIGHT */

  int intra_period; /*! Key Frame Interval */
  int decoding_refresh_type;

  // Rev. B: H.264 only parameters, in ni_logan_t408_config_t
  // - for H.264 on T408:
  int enable_transform_8x8;
  int avc_slice_mode;
  int avc_slice_arg;
  int entropy_coding_mode;

  // - shared between HEVC and H.264
  int slice_mode;
  int slice_arg;
  int intra_mb_refresh_mode;
  int intra_mb_refresh_arg;
  // HLG preferred transfer characteristics
  int preferred_transfer_characteristics;

} ni_logan_h265_encoder_params_t;

//ni_logan_decoder_input_params defs
#define NI_LOGAN_DEC_PARAM_OUT                          "out"

typedef struct _ni_logan_decoder_input_params
{
  int hwframes;
  int set_high_priority;
  int keep_alive_timeout;
  int enable_user_data_sei_passthru;
  int check_packet;
  int custom_sei_passthru;
  int lowdelay;
} ni_logan_decoder_input_params_t;

typedef struct _ni_logan_encoder_params
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
  int roi_demo_mode;    // demo ROI support - for internal testing
  int reconf_demo_mode; // NETINT_INTERNAL - currently only for internal testing
  int force_pic_qp_demo_mode; // demo force pic qp mode - for internal testing
  int low_delay_mode;   // encoder low latency mode
  int padding;          // encoder input padding setting
  int generate_enc_hdrs; // generate encoder headers in advance of encoding
  int use_low_delay_poc_type; // specifies the encoder to set
                              // picture_order_count_type=2 in the H.264 SPS
  int strict_timeout_mode; // encoder stricter timeout detection mode

  // HRD and AUD features related
  int dolby_vision_profile;
  int hrd_enable;
  int enable_aud;

  // 1: force on every frame with same input type; 0: no (except for I-frame)
  int force_frame_type;

  /**> 0=no HDR in VUI, 1=add HDR info to VUI **/
  int hdrEnableVUI;

  int crf;  /*!*< Constant Rate Factor setting */
  int cbr;  //It enables filler data for strict rate control
  int cacheRoi; // enables caching of ROIs applied to subsequent frames
  int enable_vfr; // enable the vfr
  int enable_explicit_rpl; // enable HEVC reference picture list

  uint32_t ui32flushGop; /**< force IDR at the intraPeriod/avcIdrPeriod thus flush Gop **/
  uint32_t ui32minIntraRefreshCycle; /**< min number of intra refresh cycles for intraRefresh feature**/

  uint32_t ui32VuiDataSizeBits;       /**< size of VUI RBSP in bits **/
  uint32_t ui32VuiDataSizeBytes;     /**< size of VUI RBSP in bytes up to MAX_VUI_SIZE **/
  uint8_t  ui8VuiRbsp[NI_LOGAN_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/
  uint32_t pos_num_units_in_tick;
  uint32_t pos_time_scale;

  int color_primaries;
  int color_transfer_characteristic;
  int color_space;
  int sar_num;
  int sar_denom;
  int video_full_range_flag;

  ni_logan_h265_encoder_params_t hevc_enc_params;
  ni_logan_decoder_input_params_t dec_input_params;

  // NETINT_INTERNAL - currently only for internal testing of reconfig, saving
  // key:val1,val2,val3,...val9 (max 9 values) in the demo reconfig data file
  // this supports max 100 lines in reconfig file, max 10 key/values per line
  int reconf_hash[100][10];

  // NETINT INTERNAL - save exchanged data between host and fw; the data is
  // saved in a circular buffer by packets in a folder uniquely identified
  // by process id and session id; 0 is to not save.
  unsigned int nb_save_pkt;    // number of decoder-in packets to save

  uint8_t hwframes;
} ni_logan_encoder_params_t;

typedef struct _ni_logan_frame
{
  // codec of the source from which this frame is decoded
  ni_logan_codec_format_t src_codec;
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
  ni_logan_pic_type_t ni_logan_pict_type;
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

  void * p_data[NI_LOGAN_MAX_NUM_DATA_POINTERS];
  uint32_t data_len[NI_LOGAN_MAX_NUM_DATA_POINTERS];

  void* p_buffer;
  uint32_t buffer_size;

  ni_logan_buf_t *dec_buf; // buffer pool entry (has memory pointed to by p_buffer)
  uint8_t preferred_characteristics_data_len;

  uint8_t *p_custom_sei;
  uint16_t bit_depth;
  int flags;

  // frame auxiliary data
  ni_aux_data_t *aux_data[NI_MAX_NUM_AUX_DATA_PER_FRAME];
  int            nb_aux_data;

  // the following info is of the source stream that is returned by decoder:
  // color info, sample aspect ratio, timing etc that are useful at encoding.
  uint8_t  color_primaries;
  uint8_t  color_trc;
  uint8_t  color_space;
  int      video_full_range_flag;
  uint8_t  aspect_ratio_idc;
  uint16_t sar_width;
  uint16_t sar_height;
  uint32_t vui_num_units_in_tick;
  uint32_t vui_time_scale;
} ni_logan_frame_t;

typedef struct _ni_logan_packet
{

  long long dts;
  long long pts;
  long long pos;
  uint32_t end_of_stream;
  uint32_t start_of_stream;
  uint32_t video_width;
  uint32_t video_height;
  uint32_t frame_type; // encoding result only 0=I, 1=P, 2=B, 5=IDR, 6=CRA
  int recycle_index;

  void* p_data;
  uint32_t data_len;
  int sent_size;

  void* p_buffer;
  uint32_t buffer_size;
  uint32_t avg_frame_qp; // average frame QP reported by VPU

  ni_logan_all_custom_sei_t *p_all_custom_sei;
  //The length of SEI data which is located after slice payload in the same packet.
  //Skip these SEI data when send the packet to the FW
  int len_of_sei_after_vcl;
  int flags;
} ni_logan_packet_t;

// 24 bytes
typedef struct _ni_logan_hwframe_surface
{
  int32_t device_handle;

  int8_t i8FrameIdx;
  int8_t i8InstID;
  uint16_t ui16SessionID;

  uint16_t ui16width;
  uint16_t ui16height;

  int8_t bit_depth; //1 ==8bit per pixel, 2 ==10
  int8_t encoding_type; //h264/265
  int8_t seq_change;
  int8_t rsvd;
  int32_t device_handle_ext; // high 32-bit device handle
  int8_t rsv[4];
} ni_logan_hwframe_surface_t;

typedef struct _ni_logan_session_data_io
{
  union
  {
    ni_logan_frame_t  frame;
    ni_logan_packet_t packet;
  }data;

} ni_logan_session_data_io_t;



#define NI_LOGAN_XCODER_PRESET_NAMES_ARRAY_LEN  3
#define NI_LOGAN_XCODER_LOG_NAMES_ARRAY_LEN     7

#define NI_LOGAN_XCODER_PRESET_NAME_DEFAULT     "default"
#define NI_LOGAN_XCODER_PRESET_NAME_CUSTOM      "custom"

#define NI_LOGAN_XCODER_LOG_NAME_NONE           "none"
#define NI_LOGAN_XCODER_LOG_NAME_ERROR          "error"
#define NI_LOGAN_XCODER_LOG_NAME_WARN           "warning"
#define NI_LOGAN_XCODER_LOG_NAME_INFO           "info"
#define NI_LOGAN_XCODER_LOG_NAME_DEBUG          "debug"
#define NI_LOGAN_XCODER_LOG_NAME_FULL           "full"

extern LIB_API const char* const g_logan_xcoder_preset_names[NI_LOGAN_XCODER_PRESET_NAMES_ARRAY_LEN];
extern LIB_API const char* const g_logan_xcoder_log_names[NI_LOGAN_XCODER_LOG_NAMES_ARRAY_LEN];

/*!*****************************************************************************
 *  \brief  Allocate and initialize a new ni_logan_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 ******************************************************************************/
 LIB_API ni_logan_session_context_t * ni_logan_device_session_context_alloc_init(void);

/*!*****************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 *
 ******************************************************************************/
 LIB_API void ni_logan_device_session_context_init(ni_logan_session_context_t *p_ctx);

 /*!****************************************************************************
 *  \brief  Frees previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 ******************************************************************************/
 LIB_API void ni_logan_device_session_context_free(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Clear already allocated session context to all zeros buffer
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 *
 *******************************************************************************/
 LIB_API void ni_logan_device_session_context_clear(ni_logan_session_context_t *p_ctx);
 
 /*!****************************************************************************
  *  \brief  Create event and returnes event handle if successful
  *
  *  \return On success returns a event handle
  *          On failure returns NI_INVALID_EVENT_HANDLE
  *****************************************************************************/
 LIB_API ni_event_handle_t ni_logan_create_event();

 /*!****************************************************************************
 *  \brief  Closes event and releases resources
 *
 *  \return NONE
 *
 ******************************************************************************/
LIB_API void ni_logan_close_event(ni_event_handle_t event_handle);

/*!*****************************************************************************
 *  \brief  Opens device and returnes device device_handle if successful
 *
 *  \param[in]  p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_max_io_size_out Maximum IO Transfer size supported
 *
 *  \return On success returns a device device_handle
 *          On failure returns NI_INVALID_DEVICE_HANDLE
 ******************************************************************************/
LIB_API ni_device_handle_t ni_logan_device_open(const char* dev, uint32_t * p_max_io_size_out);

/*!*****************************************************************************
 *  \brief  Closes device and releases resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_logan_device_open()
 *
 *  \return NONE
 *
 ******************************************************************************/
LIB_API void ni_logan_device_close(ni_device_handle_t dev);

/*!*****************************************************************************
 *  \brief  Queries device and returns device capability structure
 *
 *  \param[in] device_handle Device handle obtained by calling ni_logan_device_open()
 *  \param[in] p_cap  Pointer to a caller allocated ni_logan_device_capability_t struct
 *  \return On success
 *                     NI_LOGAN_RETCODE_SUCCESS
 *          On failure
                       NI_LOGAN_RETCODE_INVALID_PARAM
                       NI_LOGAN_RETCODE_ERROR_MEM_ALOC
                       NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_capability_query(ni_device_handle_t device_handle,
                                                            ni_logan_device_capability_t *p_cap);

/*!*****************************************************************************
 *  \brief  Opens a new device session depending on the device_type parameter
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER opens encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *                                               ni_logan_session_config_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_session_open(ni_logan_session_context_t *p_ctx,
                                                        ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Closes device session that was previously opened by calling
 *          ni_logan_device_session_open()
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER closes encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] eos_recieved Flag indicating if End Of Stream indicator was recieved
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_session_close(ni_logan_session_context_t *p_ctx,
                                                         int eos_recieved,
                                                         ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Sends a flush command to the device
 *          ni_logan_device_session_open()
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER sends flush command to decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER sends flush command to decoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_session_flush(ni_logan_session_context_t *p_ctx,
                                                         ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Save a stream's headers in a decoder session that can be used later
 *          for continuous decoding from the same source.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] hdr_data     Pointer to header data
 *  \param[in] hdr_size     Size of header data in bytes
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_dec_session_save_hdrs(
  ni_logan_session_context_t *p_ctx, uint8_t *hdr_data, uint8_t hdr_size);

/*!*****************************************************************************
 *  \brief  Flush a decoder session to get ready to continue decoding.
 *  Note: this is different from ni_logan_device_session_flush in that it closes the
 *        current decode session and opens a new one for continuous decoding.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_dec_session_flush(ni_logan_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Sends data the device
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER sends data packet to decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER sends data frame to encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_logan_session_data_io_t struct which contains either a
 *                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *                          If NI_LOGAN_DEVICE_TYPE_DECODER is specified, it is expected
 *                          that the ni_logan_packet_t struct inside the p_data pointer
 *                          contains data to send.
 *                          If NI_LOGAN_DEVICE_TYPE_ENCODER is specified, it is expected
 *                          that the ni_logan_frame_t struct inside the p_data pointer
 *                          contains data to send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API int ni_logan_device_session_write(ni_logan_session_context_t* p_ctx,
                                          ni_logan_session_data_io_t* p_data,
                                          ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Reads data the device
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER reads data packet from decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER reads data frame from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_logan_session_data_io_t struct which contains either a
 *                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *                          If NI_LOGAN_DEVICE_TYPE_DECODER is specified, data that was
 *                          read will be placed into ni_logan_frame_t struct inside the p_data pointer
 *                          If NI_LOGAN_DEVICE_TYPE_ENCODER is specified,  data that was
 *                          read will be placed into ni_logan_packet_t struct inside the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API int ni_logan_device_session_read(ni_logan_session_context_t* p_ctx,
                                         ni_logan_session_data_io_t* p_data,
                                         ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Query session data from the device - Currently not implemented
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER query session data
 *          from decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER query session data
 *          from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_session_query(ni_logan_session_context_t* p_ctx,
                                                         ni_logan_device_type_t device_type);

/*!*****************************************************************************
 *  \brief  Allocate preliminary memory for the frame buffer for encoding
 *          based on provided parameters. Applicable to YUV420 Planar pixel
 *          format only, 8 or 10 bit/pixel.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                                               ni_logan_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_frame_buffer_alloc(ni_logan_frame_t *pframe,
                                                       int video_width,
                                                       int video_height,
                                                       int alignment,
                                                       int metadata_flag,
                                                       int factor,
                                                       int hw_frame_count);

/*!*****************************************************************************
 *  \brief  Allocate memory for decoder frame buffer based on provided
 *          parameters; the memory is retrieved from a buffer pool and will be
 *          returned to the same buffer pool by ni_logan_decoder_frame_buffer_free.
 *  Note:   all attributes of ni_logan_frame_t will be set up except for memory and
 *          buffer, which rely on the pool being allocated; the pool will be
 *          allocated only after the frame resolution is known.
 *
 *  \param[in] p_pool        Buffer pool to get the memory from
 *  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
 *  \param[in] alloc_mem     Whether to get memory from buffer pool
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_decoder_frame_buffer_alloc(ni_logan_buf_pool_t* p_pool,
                                                               ni_logan_frame_t *pframe,
                                                               int alloc_mem,
                                                               int video_width,
                                                               int video_height,
                                                               int alignment,
                                                               int factor);

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data. 
  *          Applicable to YUV420p AVFrame only. 8 or 10 bit/pixel.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data)
  *
  *  \return On success
  *                          NI_LOGAN_RETCODE_SUCCESS
  *          On failure
  *                          NI_LOGAN_RETCODE_INVALID_PARAM
  *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_encoder_frame_buffer_alloc(ni_logan_frame_t *pframe,
                                                   int video_width,
                                                   int video_height,
                                                   int linesize[],
                                                   int alignment,
                                                   int extra_len,
                                                   int factor);

/*!*****************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_logan_frame_buffer_alloc or ni_logan_encoder_frame_buffer_alloc
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_logan_frame_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_frame_buffer_free(ni_logan_frame_t *pframe);

/*!*****************************************************************************
 *  \brief  Free decoder frame buffer that was previously allocated with
 *          ni_logan_decoder_frame_buffer_alloc, returning memory to a buffer pool.
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_logan_frame_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_decoder_frame_buffer_free(ni_logan_frame_t *pframe);

/*!*****************************************************************************
 *  \brief  Return a memory buffer to memory buffer pool, for a decoder frame.
 *
 *  \param[in] buf              Buffer to be returned.
 *  \param[in] p_buffer_pool    Buffer pool to return buffer to.
 *
 *  \return None
 ******************************************************************************/
LIB_API void ni_logan_decoder_frame_buffer_pool_return_buf(ni_logan_buf_t *buf,
                                                     ni_logan_buf_pool_t *p_buffer_pool);

/*!*****************************************************************************
 *  \brief  Allocate memory for the packet buffer based on provided packet size
 *
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_logan_packet_t struct
 *  \param[in] packet_size   Required allocation size
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_packet_buffer_alloc(ni_logan_packet_t *ppacket,
                                            int packet_size);

/*!*****************************************************************************
 *  \brief  Free packet buffer that was previously allocated with either
 *          ni_logan_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_logan_packet_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_packet_buffer_free(ni_logan_packet_t *ppacket);

/*!*****************************************************************************
 *  \brief  Copy video packet accounting for allighment
 *
 *  \param[in] p_destination  Destination to where to copy to
 *  \param[in] p_source       Source from where to copy from
 *  \param[in] cur_size       current size
 *  \param[out] p_leftover    Pointer to the data that was left over
 *  \param[out] p_prev_size   Size of the data leftover ??
 *
 *  \return On success        Total number of bytes that were copied
 *          On failure        NI_LOGAN_RETCODE_FAILURE
 ******************************************************************************/
LIB_API int ni_logan_packet_copy(void* p_destination,
                                 const void* const p_source,
                                 int cur_size,
                                 void* p_leftover,
                                 int* p_prev_size);

/*!*****************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_encoder_init_default_params(ni_logan_encoder_params_t *p_param,
                                                                int fps_num,
                                                                int fps_denom,
                                                                long bit_rate,
                                                                int width,
                                                                int height);


/*!*****************************************************************************
 *  \brief  Initialize default decoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_decoder_init_default_params(ni_logan_encoder_params_t *p_param,
                                                                int fps_num,
                                                                int fps_denom,
                                                                long bit_rate,
                                                                int width,
                                                                int height);

/*!******************************************************************************
*  \brief  Set value referenced by name in decoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_logan_decoder_params_t
*                                    to find and set a particular parameter
*  \param[in] name       String represented parameter name to search
*  \param[in] value      Parameter value to set
*
*  \return On success
*                        NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                        NI_LOGAN_RETCODE_FAILURE
*                        NI_LOGAN_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_decoder_params_set_value(ni_logan_encoder_params_t* p_params,
                                                             const char* name,
                                                             char* value);

/*!******************************************************************************
*  \brief  Set value referenced by name in encoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
*                                    to find and set a particular parameter
*  \param[in] name       String represented parameter name to search
*  \param[in] value      Parameter value to set
*
*  \return On success
*                        NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                        NI_LOGAN_RETCODE_FAILURE
*                        NI_LOGAN_RETCODE_INVALID_PARAM
******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_encoder_params_set_value(ni_logan_encoder_params_t * p_params,
                                                             const char *name,
                                                             const char *value,
                                                             ni_logan_session_context_t *ctx);

/*!*****************************************************************************
*  \brief  Validate relationship of some params in encoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
*  \param[in] codec      encoding codec
*
*  \return On success
*                        NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                        NI_LOGAN_RETCODE_FAILURE
*                        NI_LOGAN_RETCODE_PARAM_ERROR_OOR and other error rc
*******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_encoder_params_check(ni_logan_encoder_params_t* p_params,
                                                         ni_logan_codec_format_t codec);


/*!*****************************************************************************
 *  \brief  Set gop parameter value referenced by name in encoder parameters
 *          structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_encoder_gop_params_set_value(ni_logan_encoder_params_t * p_params,
                                                                 const char *name,
                                                                 const char *value);

/*!*****************************************************************************
 *  \brief  Get GOP's max number of reorder frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *
 *  \return max number of reorder frames of the GOP
 ******************************************************************************/
LIB_API int ni_logan_get_num_reorder_of_gop_structure(ni_logan_encoder_params_t * p_params);

/*!*****************************************************************************
 *  \brief  Get GOP's number of reference frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *
 *  \return number of reference frames of the GOP
 ******************************************************************************/
LIB_API int ni_logan_get_num_ref_frame_of_gop_structure(ni_logan_encoder_params_t * p_params);

/*!*****************************************************************************
 *  \brief  Add a new auxiliary data to a frame
 *
 *  \param[in/out] frame  a frame to which the auxiliary data should be added
 *  \param[in]     type   type of the added auxiliary data
 *  \param[in]     data_size size of the added auxiliary data
 *
 *  \return a pointer to the newly added aux data on success, NULL otherwise
 ******************************************************************************/
LIB_API ni_aux_data_t *ni_logan_frame_new_aux_data(ni_logan_frame_t *frame,
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
LIB_API ni_aux_data_t *ni_logan_frame_new_aux_data_from_raw_data(
  ni_logan_frame_t *frame,
  ni_aux_data_type_t type,
  const uint8_t* raw_data,
  int data_size);

/*!*****************************************************************************
 *  \brief  Retrieve from the frame auxiliary data of a given type if exists
 *
 *  \param[in] frame  a frame from which the auxiliary data should be retrieved
 *  \param[in] type   type of the auxiliary data to be retrieved
 *
 *  \return a pointer to the aux data of a given type on success, NULL otherwise
 ******************************************************************************/
LIB_API ni_aux_data_t *ni_logan_frame_get_aux_data(const ni_logan_frame_t *frame,
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
LIB_API void ni_logan_frame_free_aux_data(ni_logan_frame_t *frame,
                                          ni_aux_data_type_t type);

/*!*****************************************************************************
 *  \brief  Free and remove all auxiliary data from the frame.
 *
 *  \param[in/out] frame a frame from which the auxiliary data should be removed
 *
 *  \return None
 ******************************************************************************/
LIB_API void ni_logan_frame_wipe_aux_data(ni_logan_frame_t *frame);

/*!******************************************************************************
*  \brief  Queries device Serial number
*
*  \param[in] device_handle Device handle used to backtrace serial Num
*  \param[in] p_serial_num  Pointer to a caller allocated ni_logan_serial_num_t struct
*  \return On success
*                     NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                     NI_LOGAN_RETCODE_INVALID_PARAM
*                     NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*                     NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_handle_map_SN(ni_device_handle_t device_handle,
                                                         ni_logan_serial_num_t *p_serial_num);

/*!******************************************************************************
*  \brief  Copies existing decoding session params for hw frame usage
*
*  \param[in] src_p_ctx    Pointer to a caller allocated source
*                                               ni_logan_session_context_t struct
*  \param[in] dst_p_ctx    Pointer to a caller allocated destination
*                                               ni_logan_session_context_t struct
*  \return On success
*                          NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_session_copy(ni_logan_session_context_t *src_p_ctx,
                                                        ni_logan_session_context_t *dst_p_ctx);

/*!******************************************************************************
*  \brief  Sends frame pool setup info to device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_logan_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \return On success      Return code
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_logan_device_session_init_framepool(ni_logan_session_context_t *p_ctx,
                                                   uint32_t pool_size);

/*!******************************************************************************
*  \brief  Reads data from the device
*          device_type should be NI_LOGAN_DEVICE_TYPE_DECODER,
*          and reads data hwdesc from decoder when hw transcoding
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_logan_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_logan_device_session_read_hwdesc(ni_logan_session_context_t *p_ctx,
                                                ni_logan_session_data_io_t *p_data);

/*!******************************************************************************
*  \brief  Reads data from hw descriptor from decoder output buffer
*
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_logan_device_session_hwdl(ni_logan_session_context_t* p_ctx,
                                         ni_logan_session_data_io_t *p_data,
                                         ni_logan_hwframe_surface_t* hwdesc);

/*!******************************************************************************
*  \brief  Writes data and reads back hw descriptor from decoder output buffer
*
*  \param[in] p_src_data   Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
LIB_API int ni_logan_device_session_hwup(ni_logan_session_context_t* p_ctx,
                                         ni_logan_session_data_io_t *p_src_data,
                                         ni_logan_hwframe_surface_t* hwdesc);

/*!*****************************************************************************
*  \brief  Allocate memory for the frame buffer based on provided parameters
*          taking into account pic line size and extra data.
*          Applicable to YUV420p AVFrame only. Cb/Cr size matches that of Y.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] linesize      Picture line size
*  \param[in] alignment     Allignment requirement
*  \param[in] extra_len     Extra data size (incl. meta data)
*
*  \return On success
*                          NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_frame_buffer_alloc_hwenc(ni_logan_frame_t *pframe,
                                                             int video_width,
                                                             int video_height,
                                                             int extra_len);

/*!******************************************************************************
*  \brief  POPULATE ME LATER
*
*  \param[in] p_packet      Pointer to a previously allocated ni_logan_packet_t struct
*  \param[in] device_handle device handle
*  \param[in] event_handle  event handle
*
*  \return On success    NI_LOGAN_RETCODE_SUCCESS
*          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
*******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_decode_buffer_free(ni_logan_hwframe_surface_t* surface,
                                                       ni_device_handle_t device_handle,
                                                       ni_event_handle_t event_handle);

/*!******************************************************************************
 *  \brief  Allocate a frame on the device based on provided parameters
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  width       width, in pixels
 *  \param[in]  height      height, in pixels
 *  \param[in]  format      pixel format
 *  \param[in]  options     options flags
 *  \param[in]  rectangle_width     clipping rectangle width
 *  \param[in]  rectangle_height    clipping rectangle height
 *  \param[in]  rectangle_x         horizontal position of clipping rectangle
 *  \param[in]  rectangle_y         vertical position of clipping rectangle
 *  \param[in]  rgba_color          RGBA fill colour (for padding only)
 *  \param[in]  frame_index         frame index (only applicable for hw frame)
 *  \param[in]  device_type         only NI_LOGAN_DEVICE_TYPE_SCALER supported now
 *
 *  \return         NI_LOGAN_RETCODE_INVALID_PARAM
 *                  NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *                  NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_alloc_frame(ni_logan_session_context_t* p_ctx,
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
                                                       ni_logan_device_type_t device_type);

/*!******************************************************************************
 *  \brief   Allocate memory for the frame buffer based on provided parameters
 *           taking into account width, height, format, stride, alignment, and
 *           extra data
 *
 *  \param[in]  p_frame         Pointer to caller allocated ni_logan_frame_t
 *  \param[in]  pixel_format    pixel format
 *  \param[in]  video_width     width, in pixels
 *  \param[in]  video_height    height, in pixels
 *  \param[in]  linesize        horizontal stride
 *  \param[in]  alignment       apply a 16 pixel height alignment (T408 only)
 *  \param[in]  extra_len       meta data size
 *
 *  \return     NI_LOGAN_RETCODE_SUCCESS
 *              NI_LOGAN_RETCODE_INVALID_PARAM
 *              NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *
 *******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_frame_buffer_alloc_v4(ni_logan_frame_t *pframe,
                                                          int pixel_format,
                                                          int video_width,
                                                          int video_height,
                                                          int linesize[],
                                                          int alignment,
                                                          int extra_len);

#ifdef __cplusplus
}
#endif
