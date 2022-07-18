/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_api.h
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

#include "ni_defs.h"

#ifdef _WIN32
#ifdef MSVC_BUILD
#include "w32pthreads.h"
#else
#include <pthread.h>
#endif
#elif __linux__
#include <pthread.h>
#include <semaphore.h>
#endif

#define NI_DATA_FORMAT_VIDEO_PACKET 0
#define NI_DATA_FORMAT_YUV_FRAME    1
#define NI_DATA_FORMAT_Y_FRAME      2
#define NI_DATA_FORMAT_CB_FRAME     3
#define NI_DATA_FORMAT_CR_FRAME     4

#define NI_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

// the following are the default values from FFmpeg
#define AV_CODEC_DEFAULT_BITRATE 200 * 1000

#define NI_MAX_GOP_NUM 8

#define NI_MAX_VUI_SIZE 32

#define FRAME_CHUNK_INDEX_SIZE  4096
#define NI_SIGNATURE_SIZE       256

#define NI_MAX_RESOLUTION_WIDTH 8192
#define NI_MAX_RESOLUTION_HEIGHT 8192
#define NI_MAX_RESOLUTION_AREA 8192*5120

#define NI_FRAME_LITTLE_ENDIAN 0
#define NI_FRAME_BIG_ENDIAN 1

#define NI_INVALID_SESSION_ID 0xFFFFFFFF

#define NI_MAX_BITRATE 700000000
#define NI_MIN_BITRATE 64000

#define NI_MAX_INTRA_PERIOD 1024
#define NI_MIN_INTRA_PERIOD 0

/*Values below used for timeout checking*/
#define NI_MAX_SESSION_OPEN_RETRIES 20
#define NI_SESSION_OPEN_RETRY_INTERVAL_US 200

#define NI_MAX_ENC_SESSION_OPEN_QUERY_RETRIES       3000
#define NI_ENC_SESSION_OPEN_RETRY_INTERVAL_US       1000

#define NI_MAX_ENC_SESSION_WRITE_QUERY_RETRIES      2000
#define NI_MAX_ENC_SESSION_READ_QUERY_RETRIES       3000

#define NI_MAX_DEC_SESSION_WRITE_QUERY_RETRIES      100
#define NI_MAX_DEC_SESSION_READ_QUERY_RETRIES       3000
#define NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES   15000

#define NI_MAX_SESSION_CLOSE_RETRIES        10
#define NI_SESSION_CLOSE_RETRY_INTERVAL_US  500000

#define NI_RETRY_INTERVAL_100US  100
#define NI_RETRY_INTERVAL_200US  200

/*Values below used for VPU resolution range checking*/
#define NI_MAX_WIDTH 8192
#define NI_MIN_WIDTH 256
#define NI_MAX_HEIGHT 8192
#define NI_MIN_HEIGHT 128

/*Values below used for parameter resolution range checking*/
#define NI_PARAM_MAX_WIDTH 8192
#define NI_PARAM_MIN_WIDTH 32
#define NI_PARAM_MAX_HEIGHT 8192
#define NI_PARAM_MIN_HEIGHT 32

#define NI_MAX_GOP_SIZE 8
#define NI_MIN_GOP_SIZE 1
#define NI_MAX_GOP_PRESET_IDX 9
#define NI_MIN_GOP_PRESET_IDX 0
#define NI_MAX_DECODING_REFRESH_TYPE 2
#define NI_MIN_DECODING_REFRESH_TYPE 0
#define NI_DEFAULT_CU_SIZE_MODE 7
#define NI_MAX_DYNAMIC_MERGE 1
#define NI_MIN_DYNAMIC_MERGE 0
#define NI_MAX_USE_RECOMMENDED_ENC_PARAMS 3
#define NI_MIN_USE_RECOMMENDED_ENC_PARAMS 0
#define NI_MAX_MAX_NUM_MERGE 3
#define NI_MIN_MAX_NUM_MERGE 0
#define NI_MAX_INTRA_QP 51
#define NI_MIN_INTRA_QP 0
#define NI_DEFAULT_INTRA_QP 22
#define NI_INTRA_QP_RANGE 25
#define NI_MAX_MAX_QP 51
#define NI_MIN_MAX_QP 0
#define NI_MAX_MIN_QP 51
#define NI_MIN_MIN_QP 0
#define NI_DEFAULT_MAX_QP 51
#define NI_DEFAULT_MIN_QP 8
#define NI_MAX_MAX_DELTA_QP 51
#define NI_MIN_MAX_DELTA_QP 0
#define NI_DEFAULT_MAX_DELTA_QP 10
#define NI_MAX_BIN 1
#define NI_MIN_BIN 0
#define NI_MAX_NUM_SESSIONS 32
#define NI_MAX_CRF 51
#define NI_MIN_CRF  0
#define NI_MIN_INTRA_REFRESH_MIN_PERIOD    0
#define NI_MAX_INTRA_REFRESH_MIN_PERIOD 8191
#define NI_MAX_KEEP_ALIVE_TIMEOUT          100
#define NI_MIN_KEEP_ALIVE_TIMEOUT          1
#define NI_DEFAULT_KEEP_ALIVE_TIMEOUT      3

#define    RC_SUCCESS           true
#define    RC_ERROR             false
#define    MAX_NUM_OF_ITEM_IN_QUEUE    256
#define    MAX_NUM_OF_THREADS_PER_FRAME  4
#define    WORKER_QUEUE_DEPTH          MAX_NUM_OF_ITEM_IN_QUEUE
#define    MAX_NUM_OF_THREADS          MAX_NUM_OF_THREADS_PER_FRAME

#define BEST_DEVICE_INST_STR  "bestinst"
#define BEST_MODEL_LOAD_STR   "bestload"
#define LIST_DEVICES_STR  "list"
#define MAX_CHAR_IN_DEVICE_NAME 32

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

typedef struct _ni_sei_user_data_entry
{
  uint32_t offset;
  uint32_t size;
} ni_sei_user_data_entry_t;

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

#define NI_ENC_MAX_SEI_BUF_SIZE        NI_VPU_ALIGN16(1024) //1024

#define NI_MAX_SEI_ENTRIES      32
// 32 user_data_entry_t records + various SEI messages sizes
#define NI_MAX_SEI_DATA     NI_VPU_ALIGN8(NI_MAX_SEI_ENTRIES * sizeof(ni_sei_user_data_entry_t) + 1024) // 1280

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
    uint8_t  mb_force_mode  :  2; // [ 1: 0]
    uint8_t  mb_qp          :  6; // [ 7: 2]
  } field;
} ni_enc_avc_roi_custom_map_t;

/*!*
* \brief This is an enumeration for supported codec formats.
*/
typedef enum _ni_codec_format
{
  NI_CODEC_FORMAT_H264 = 0,
  NI_CODEC_FORMAT_H265 = 1

} ni_codec_format_t;
    
/*!*
* \brief This is an enumeration for encoder parameter change.
*/
typedef enum _ni_param_change_flags
{
  // COMMON parameters which can be changed frame by frame.
  NI_SET_CHANGE_PARAM_PPS                 = (1 << 0),
  NI_SET_CHANGE_PARAM_INTRA_PARAM         = (1 << 1),
  NI_SET_CHANGE_PARAM_RC_TARGET_RATE      = (1 << 8),
  NI_SET_CHANGE_PARAM_RC                  = (1 << 9),
  NI_SET_CHANGE_PARAM_RC_MIN_MAX_QP       = (1 << 10),
  NI_SET_CHANGE_PARAM_RC_BIT_RATIO_LAYER  = (1 << 11),
  NI_SET_CHANGE_PARAM_INDEPEND_SLICE      = (1 << 16),
  NI_SET_CHANGE_PARAM_DEPEND_SLICE        = (1 << 17),
  NI_SET_CHANGE_PARAM_RDO                 = (1 << 18),
  NI_SET_CHANGE_PARAM_NR                  = (1 << 19),
  NI_SET_CHANGE_PARAM_BG                  = (1 << 20),
  NI_SET_CHANGE_PARAM_CUSTOM_MD           = (1 << 21),
  NI_SET_CHANGE_PARAM_CUSTOM_LAMBDA       = (1 << 22),
  NI_SET_CHANGE_PARAM_RC2                 = (1 << 23),
  NI_SET_CHANGE_PARAM_VUI_HRD_PARAM       = (1 << 24),

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
  int32_t fillerEnable;       /**< enables filler data for strict rate control*/

  // NI_SET_CHANGE_PARAM_RC_MIN_MAX_QP
  int32_t minQpI;                         /**< A minimum QP of I picture for rate control */
  int32_t maxQpI;                         /**< A maximum QP of I picture for rate control */
  int32_t maxDeltaQp;                     /**< A maximum delta QP for rate control */


  int32_t minQpP;                         /**< A minimum QP of P picture for rate control */
  int32_t minQpB;                         /**< A minimum QP of B picture for rate control */
  int32_t maxQpP;                         /**< A maximum QP of P picture for rate control */
  int32_t maxQpB;                         /**< A maximum QP of B picture for rate control */

  // NI_SET_CHANGE_PARAM_INTRA_PARAM
  int32_t intraQP;                        /**< A quantization parameter of intra picture */
  int32_t intraPeriod;                    /**< A period of intra picture in GOP size */
  int32_t repeatHeaders; /**< When enabled, encoder repeats the VPS/SPS/PPS headers on I-frames */

  // NI_SET_CHANGE_PARAM_VUI_HRD_PARAM
  uint32_t encodeVuiRbsp;        /**< A flag to encode the VUI syntax in rbsp */
  uint32_t vuiDataSizeBits;       /**< The bit size of the VUI rbsp data */
  uint32_t vuiDataSizeBytes;      /**< The byte size of the VUI rbsp data */
  uint8_t  vuiRbsp[NI_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/

  int32_t reserved[16];          // reserved bytes to make struct size 8-align
} ni_encoder_change_params_t; // 176 bytes (has to be 8 byte aligned)

/*!*
* \brief decoded payload format of HDR SEI mastering display colour volume
*
*/
typedef struct _ni_dec_mastering_display_colour_volume
{
  uint32_t   display_primaries_x[3];
  uint32_t   display_primaries_y[3];
  uint32_t   white_point_x                   : 16;
  uint32_t   white_point_y                   : 16;
  uint32_t   max_display_mastering_luminance : 32;
  uint32_t   min_display_mastering_luminance : 32;
} ni_dec_mastering_display_colour_volume_t;

/*!*
* \brief payload format of HDR SEI content light level info
*
*/
typedef struct _ni_content_light_level_info
{
  uint16_t max_content_light_level;
  uint16_t max_pic_average_light_level;
} ni_content_light_level_info_t;

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
* \brief hardware capability type
*/
typedef struct _ni_hw_capability
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
} ni_hw_capability_t;

/*!*
* \brief  device capability type
*/
typedef struct _ni_device_capability
{
  uint8_t device_is_xcoder;
  uint8_t hw_elements_cnt;
  uint8_t h264_decoders_cnt;
  uint8_t h264_encoders_cnt;
  uint8_t h265_decoders_cnt;
  uint8_t h265_encoders_cnt;
  // firmware revision, a space right filled ASCII array, not a string
  uint8_t fw_rev[8];
  ni_hw_capability_t xcoder_devices[NI_MAX_DEVICES_PER_HW_INSTANCE];
  uint8_t fw_commit_hash[41];
  uint8_t fw_commit_time[26];
  uint8_t fw_branch_name[256];
} ni_device_capability_t;

/*!*
* \brief Session running state type.
*/
typedef enum _ni_session_run_state
{
  SESSION_RUN_STATE_NORMAL = 0,
  SESSION_RUN_STATE_SEQ_CHANGE_DRAINING = 1,
  SESSION_RUN_STATE_RESETTING = 2,
} ni_session_run_state_t;


typedef struct _ni_context_query
{
  uint32_t context_id : 8;     //07:00  SW Instance ID (0 to Max number of instances)
  uint32_t context_status : 8; //15:08  Instance Status (0-Idle, 1-Active)
  uint32_t codec_format : 8;   //23:16  Codec Format (0-H264, 1-H265)
  uint32_t video_width : 16;   //39:24  Video Width (0 to Max Width)
  uint32_t video_height : 16;  //55:40  Video Height (0 to Max Height)
  uint32_t fps : 8;            //63:56  FPS (0 to 255)
  uint32_t reserved : 8;       //Alignment
} ni_context_query_t;

typedef struct _ni_load_query
{
  uint32_t current_load;
  uint32_t fw_model_load;
  uint32_t fw_video_mem_usage;
  uint32_t total_contexts;
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
  void *p_buffer;                         // only be used when macro XCODER_IO_RW_ENABLED is defined.
  uint32_t keep_alive_timeout;            // keep alive timeout setting
}ni_thread_arg_struct_t;


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
  pthread_mutex_t mutex;
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


typedef struct worker_queue_item
{
  ni_nvme_opcode_t opcode;
  ni_device_handle_t handle;
  ni_nvme_command_t *p_ni_nvme_cmd;
  uint32_t data_len;
  void *p_data;
  uint32_t *p_result;
  uint32_t frame_chunk_index; // now change to the offset divide by 4k
  uint32_t frame_chunk_size;
  uint32_t dw14_yuv_offset;
  uint32_t dw15_len;
  uint32_t session_id;
} worker_queue_item;

typedef struct queue_info
{
  int headidx;
  int tailidx;
  worker_queue_item *worker_queue_head;
} queue_info;

typedef struct _ni_session_context
{
  /*! LATENCY MEASUREMENT queue */
  ni_lat_meas_q_t *frame_time_q;
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
  uint8_t ui8_light_level_data[NI_LIGHT_LEVEL_DATA_SZ];
  int sei_hdr_mastering_display_color_vol_len;
  int mdcv_max_min_lum_data_len;
  uint8_t ui8_mdcv_max_min_lum_data[NI_MDCV_LUM_DATA_SZ];
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
  /* store pts values to create an accurate pts offset */
  int64_t pts_offsets[NI_FIFO_SZ];
  uint64_t pkt_index;
  uint64_t pkt_offsets_index_min[NI_FIFO_SZ];
  uint64_t pkt_offsets_index[NI_FIFO_SZ];
  uint8_t *pkt_custom_sei[NI_FIFO_SZ];
  uint32_t pkt_custom_sei_len[NI_FIFO_SZ];
  uint8_t *last_pkt_custom_sei;
  uint32_t last_pkt_custom_sei_len;

  /*! if session is on a decoder handling incoming pkt 512-aligned */
  int is_dec_pkt_512_aligned;

  /*! Device Card ID */
  ni_device_handle_t device_handle;
  /*! block device fd */
  ni_device_handle_t blk_io_handle;
  
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
  /*! From the user command, which device allocation method we use */
  char dev_xcoder[MAX_CHAR_IN_DEVICE_NAME];
  /*! the device name that opened*/
  char dev_xcoder_name[MAX_CHAR_IN_DEVICE_NAME];
  /*! the block name that opened */
  char blk_xcoder_name[MAX_CHAR_IN_DEVICE_NAME];

  int src_bit_depth;         // 8 or 10 bits/pixel formats, default 8
  int src_endian;            // encoding 0: little endian (default) 1: big
  int bit_depth_factor;      // for YUV buffer allocation
  // for encoder roi metadata
  uint32_t roi_len;
  uint32_t roi_avg_qp;

  /*! Context Query */
  ni_load_query_t load_query;
  /*! session metrics including frame statistics */
  ni_instance_status_info_t session_stats;

  /*! Leftover Buffer */
  void *p_leftover;
  int prev_size;
  uint32_t sent_size;

  /*! for decoder: buffer for lone SEI as a single packet, to be sent along
    with next frame */
  uint8_t buf_lone_sei[NI_MAX_SEI_DATA];
  int lone_sei_size;

  /*! PTS Table */
  void *pts_table;

  /*! DTS Queue */
  void *dts_queue;

  // Now the code has multithread on the frame level so a mutex is needed for dts queue operations
  pthread_mutex_t *dts_queue_mutex;

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

  // write packet/frame required buf size
  uint32_t required_buf_size;

  // frame forcing: for encoding
  int force_frame_type;

  uint32_t ready_to_close; //flag to indicate we are ready to close session

  // session running state
  ni_session_run_state_t session_run_state;
  uint32_t active_video_width;  //Current video width. this is used to do sequence change
  uint32_t active_video_height; //Current video height ,this is used to do sequence change
  pthread_t keep_alive_thread;
  ni_thread_arg_struct_t *keep_alive_thread_args;
  ni_queue_buffer_pool_t *buffer_pool;
  ni_buf_pool_t *dec_fme_buf_pool;
  uint32_t needs_dealoc; //This is set to indicate that the context was dynamically allocated and needs to be freed

  uint32_t encoder_read_total_num_of_chunks;
  uint32_t encoder_write_total_num_of_chunks;
  uint32_t decoder_read_total_num_of_chunks;

  uint32_t encoder_read_total_num_of_bytes;
  uint32_t encoder_write_total_num_of_bytes;
  uint32_t decoder_read_total_num_of_bytes;

  uint32_t encoder_read_processed_data_len;
  uint32_t encoder_write_processed_data_len;
  uint32_t decoder_read_processed_data_len;

  pthread_t ThreadID_encoder_write[MAX_NUM_OF_THREADS];
  pthread_t ThreadID_encoder_read[MAX_NUM_OF_THREADS];
  pthread_t ThreadID_decoder_read[MAX_NUM_OF_THREADS];
  
  worker_queue_item args_encoder_write[WORKER_QUEUE_DEPTH];
  worker_queue_item args_encoder_read[WORKER_QUEUE_DEPTH];
  worker_queue_item args_decoder_read[WORKER_QUEUE_DEPTH];

  worker_queue_item worker_queue_encoder_write[WORKER_QUEUE_DEPTH];
  worker_queue_item worker_queue_encoder_read[WORKER_QUEUE_DEPTH];
  worker_queue_item worker_queue_decoder_read[WORKER_QUEUE_DEPTH];

  bool close_encoder_write_thread;
  bool close_encoder_read_thread;
  bool close_decoder_read_thread;

  queue_info encoder_write_workerqueue;
  queue_info encoder_read_workerqueue;
  queue_info decoder_read_workerqueue;

  pthread_mutex_t *encoder_read_mutex;
  pthread_mutex_t *encoder_read_mutex_len;
  sem_t *encoder_read_sem;
  pthread_cond_t *encoder_read_cond;

  pthread_mutex_t *encoder_write_mutex;
  pthread_mutex_t *encoder_write_mutex_len;
  sem_t *encoder_write_semaphore;
  pthread_cond_t *encoder_write_cond;

  pthread_mutex_t *decoder_read_mutex;
  pthread_mutex_t *decoder_read_mutex_len;
  sem_t *decoder_read_semaphore;
  pthread_cond_t *decoder_read_cond;

  uint64_t codec_total_ticks;
  uint64_t codec_start_time;

  // only be used when macro XCODER_IO_RW_ENABLED is defined.
  void *p_all_zero_buf; //This is for sos, eos, flush and keep alive request
  void *p_dec_packet_inf_buf;

  // these two event handle are only for Windows asynchronous read and write now
  ni_event_handle_t event_handle;
  ni_event_handle_t thread_event_handle;

  // decoder lowDelay mode for All I packets or IPPP packets
  int decoder_low_delay;
#if NI_DEBUG_LATENCY
  //per context session timestamps for encode latency measure
  long long microseconds_w;
  long long microseconds_w_prev;
  long long microseconds_r;
#endif

  void *session_info;
} ni_session_context_t;

/*!*
* \brief This is an enumeration for encoder reconfiguration test settings
*/
typedef enum _ni_reconfig
{
  XCODER_TEST_RECONF_OFF = 0,
  XCODER_TEST_RECONF_BR = 1,
  XCODER_TEST_RECONF_INTRAPRD = 2,
  XCODER_TEST_RECONF_VUI_HRD = 3,
  XCODER_TEST_RECONF_LONG_TERM_REF = 4,
  XCODER_TEST_RECONF_RC            = 5,
  XCODER_TEST_RECONF_RC_MIN_MAX_QP = 6,
} ni_reconfig_t;
 
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

#define NI_ENC_GOP_PARAMS_CUSTOM_GOP_SIZE "customGopSize"

typedef struct _ni_custom_gop_params
{
  int custom_gop_size;                              /*!*< The size of custom GOP (0~8) */
  ni_gop_params_t pic_param[NI_MAX_GOP_NUM]; /*!*< Picture parameters of Nth picture in custom GOP */
} ni_custom_gop_params_t;

#define NI_ENC_REPEAT_HEADERS_FIRST_IDR    0
#define NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES 1

//ni_h265_encoder_params defs
#define NI_ENC_PARAM_BITRATE                        "bitrate"
#define NI_ENC_PARAM_RECONF_DEMO_MODE               "ReconfDemoMode"
#define NI_ENC_PARAM_RECONF_FILE                    "ReconfFile"
#define NI_ENC_PARAM_ROI_DEMO_MODE                  "RoiDemoMode"
#define NI_ENC_PARAM_FORCE_PIC_QP_DEMO_MODE         "ForcePicQpDemoMode"
#define NI_ENC_PARAM_GEN_HDRS                       "GenHdrs"
#define NI_ENC_PARAM_PADDING                        "padding"
#define NI_ENC_PARAM_FORCE_FRAME_TYPE               "forceFrameType"
#define NI_ENC_PARAM_PROFILE                        "profile"
#define NI_ENC_PARAM_LEVEL_IDC                      "level-idc"
#define NI_ENC_PARAM_LEVEL                          "level"
#define NI_ENC_PARAM_HIGH_TIER                      "high-tier"
#define NI_ENC_PARAM_LOG_LEVEL                      "log-level"
#define NI_ENC_PARAM_LOG                            "log"
#define NI_ENC_PARAM_GOP_PRESET_IDX                 "gopPresetIdx"
#define NI_ENC_PARAM_LOW_DELAY                      "lowDelay"
#define NI_ENC_PARAM_USE_RECOMMENDED_ENC_PARAMS     "useRecommendEncParam"
#define NI_ENC_PARAM_USE_LOW_DELAY_POC_TYPE         "useLowDelayPocType"
#define NI_ENC_PARAM_ENABLE_RATE_CONTROL            "RcEnable"
#define NI_ENC_PARAM_ENABLE_CU_LEVEL_RATE_CONTROL   "cuLevelRCEnable"
#define NI_ENC_PARAM_ENABLE_HVS_QP                  "hvsQPEnable"
#define NI_ENC_PARAM_ENABLE_HVS_QP_SCALE            "hvsQpScaleEnable"
#define NI_ENC_PARAM_HVS_QP_SCALE                   "hvsQpScale"
#define NI_ENC_PARAM_MIN_QP                         "minQp"
#define NI_ENC_PARAM_MAX_QP                         "maxQp"
#define NI_ENC_PARAM_MAX_DELTA_QP                   "maxDeltaQp"
#define NI_ENC_PARAM_RC_INIT_DELAY                  "RcInitDelay"
#define NI_ENC_PARAM_FORCED_HEADER_ENABLE           "repeatHeaders"
#define NI_ENC_PARAM_ROI_ENABLE                     "roiEnable"
#define NI_ENC_PARAM_CONF_WIN_TOP                   "confWinTop"
#define NI_ENC_PARAM_CONF_WIN_BOTTOM                "confWinBot"
#define NI_ENC_PARAM_CONF_WIN_LEFT                  "confWinLeft"
#define NI_ENC_PARAM_CONF_WIN_RIGHT                 "confWinRight"
#define NI_ENC_PARAM_INTRA_PERIOD                   "intraPeriod"
#define NI_ENC_PARAM_TRANS_RATE                     "transRate"
#define NI_ENC_PARAM_FRAME_RATE                     "frameRate"
#define NI_ENC_PARAM_FRAME_RATE_DENOM               "frameRateDenom"
#define NI_ENC_PARAM_INTRA_QP                       "intraQP"
#define NI_ENC_PARAM_DECODING_REFRESH_TYPE          "decodingRefreshType"
// Rev. B: H.264 only parameters.
#define NI_ENC_PARAM_ENABLE_8X8_TRANSFORM           "transform8x8Enable"
#define NI_ENC_PARAM_AVC_SLICE_MODE                 "avcSliceMode"
#define NI_ENC_PARAM_AVC_SLICE_ARG                  "avcSliceArg"
#define NI_ENC_PARAM_ENTROPY_CODING_MODE            "entropyCodingMode"
// Rev. B: shared between HEVC and H.264
#define NI_ENC_PARAM_INTRA_MB_REFRESH_MODE          "intraMbRefreshMode"
#define NI_ENC_PARAM_INTRA_MB_REFRESH_ARG           "intraMbRefreshArg"
#define NI_ENC_PARAM_INTRA_REFRESH_MODE             "intraRefreshMode"
#define NI_ENC_PARAM_INTRA_REFRESH_ARG              "intraRefreshArg"
// TBD Rev. B: could be shared for HEVC and H.264
#define NI_ENC_PARAM_ENABLE_MB_LEVEL_RC             "mbLevelRcEnable"
#define NI_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS "prefTRC"
// HRD and AUD features related
#define NI_ENC_PARAM_DOLBY_VISION_PROFILE           "dolbyVisionProfile"
#define NI_ENC_PARAM_HRD_ENABLE                     "hrdEnable"
#define NI_ENC_PARAM_ENABLE_AUD                     "enableAUD"
#define NI_ENC_PARAM_CRF                            "crf"
#define NI_ENC_PARAM_CBR                            "cbr"
#define NI_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD       "intraRefreshMinPeriod"
#define NI_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE     "longTermReferenceEnable"
// stricter timeout detection enable
#define NI_ENC_PARAM_STRICT_TIMEOUT_MODE            "strictTimeout"
#define NI_ENC_PARAM_LOSSLESS_ENABLE                "losslessEnable"
#define NI_ENC_PARAM_FLUSH_GOP                      "flushGop"

typedef struct _ni_h265_encoder_params
{
  int profile;
  int level_idc;
  int high_tier;
  int frame_rate;

  //GOP Pattern
  int gop_preset_index; /*!*< A GOP structure preset option (IPP, IBP, IBBP, IbBbP, use Custom GOP, etc) */ // 0-custom 1-I-only 2-IPPP 3-IBBB 4-IBP .....

  // CUSTOM_GOP
  ni_custom_gop_params_t custom_gop_params;

  //Preset Mode
  int use_recommend_enc_params; /*!*< 0: Custom, 1: Slow speed and best quality, 2: Normal Speed and quality, 3: Fast Speed and Low Quality */

  //Encode Options

  struct
  {
    //Rate control parameters
    int enable_rate_control;                                               /*!*< It enable rate control */
    int enable_cu_level_rate_control;                                        /*!*< It enable CU level rate control */
    int enable_hvs_qp;                                            /*!*< It enable CU QP adjustment for subjective quality enhancement */
    int enable_hvs_qp_scale;                                       /*!*< It enable QP scaling factor for CU QP adjustment when enable_hvs_qp = 1 */
    int hvs_qp_scale;                                             /*!*< A QP scaling factor for CU QP adjustment when hvcQpenable = 1 */
    int min_qp; /*!*< A minimum QP for rate control */            //8
    int max_qp; /*!*< A maximum QP for rate control */            //51
    int max_delta_qp; /*!*< A maximum delta QP for rate control */ //10
    int trans_rate;                                              /*!*< trans_rate */
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

  // Rev. B: H.264 only parameters, in ni_t408_config_t
  // - for H.264 on T408:
  int enable_transform_8x8;
  int avc_slice_mode;
  int avc_slice_arg;
  int entropy_coding_mode;

  // - shared between HEVC and H.264
  int intra_mb_refresh_mode;
  int intra_mb_refresh_arg;
  // HLG preferred transfer characteristics
  int preferred_transfer_characteristics;

} ni_h265_encoder_params_t;

typedef struct _ni_encoder_params
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

  uint32_t ui32flushGop; /**< force IDR at the intraPeriod/avcIdrPeriod thus flush Gop **/
  uint32_t ui32minIntraRefreshCycle; /**< min number of intra refresh cycles for intraRefresh feature**/

  uint32_t ui32VuiDataSizeBits;       /**< size of VUI RBSP in bits **/
  uint32_t ui32VuiDataSizeBytes;     /**< size of VUI RBSP in bytes up to MAX_VUI_SIZE **/
  uint8_t  ui8VuiRbsp[NI_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/
  int pos_num_units_in_tick;
  int pos_time_scale;


  ni_h265_encoder_params_t hevc_enc_params;
  // NETINT_INTERNAL - currently only for internal testing of reconfig, saving
  // key:val1,val2,val3,...val9 (max 9 values) in the demo reconfig data file
  // this supports max 100 lines in reconfig file, max 10 key/values per line
  int reconf_hash[100][10];
} ni_encoder_params_t;

typedef struct _ni_frame
{
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

  void * p_data[NI_MAX_NUM_DATA_POINTERS];
  uint32_t data_len[NI_MAX_NUM_DATA_POINTERS];
  
  // This variable is ued for Zero copy ONLY, It a round down size of Y and round up size if U and V
  uint32_t data_len_page_aligned[NI_MAX_NUM_DATA_POINTERS];  

  void* p_buffer;
  uint32_t buffer_size;

  ni_buf_t *dec_buf; // buffer pool entry (has memory pointed to by p_buffer)
  uint8_t preferred_characteristics_data_len;

  uint8_t *p_custom_sei;
  uint32_t custom_sei_len;
} ni_frame_t;

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

  void* p_data;
  uint32_t data_len;
  int sent_size;

  void* p_buffer;
  uint32_t buffer_size;
  uint32_t avg_frame_qp; // average frame QP reported by VPU

  uint8_t *p_custom_sei;
  uint32_t custom_sei_len;
  //this packet contains user data unregistered SEI payload but slice payload if set.
  int no_slice;
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

extern const char* const g_xcoder_preset_names[NI_XCODER_PRESET_NAMES_ARRAY_LEN];
extern const char* const g_xcoder_log_names[NI_XCODER_LOG_NAMES_ARRAY_LEN];

/*!******************************************************************************
 *  \brief  Allocate and initialize a new ni_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 *******************************************************************************/
 LIB_API ni_session_context_t * ni_device_session_context_alloc_init(void);

/*!******************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t struct
 *
 *
 *******************************************************************************/
 LIB_API void ni_device_session_context_init(ni_session_context_t *p_ctx);
 
 /*!******************************************************************************
 *  \brief  Frees previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t struct
 *
 *******************************************************************************/
 LIB_API void ni_device_session_context_free(ni_session_context_t *p_ctx);
 
 /*!******************************************************************************
  *  \brief  Create event and returnes event handle if successful
  *
  *  \return On success returns a event handle
  *          On failure returns NI_INVALID_EVENT_HANDLE
  *******************************************************************************/
 LIB_API ni_event_handle_t ni_create_event(); 
 
 /*!******************************************************************************
 *  \brief  Closes event and releases resources
 *
 *  \return NONE
 *          
 *******************************************************************************/
LIB_API void ni_close_event(ni_event_handle_t event_handle);
 
/*!******************************************************************************
 *  \brief  Opens device and returnes device device_handle if successful
 *
 *  \param[in]  p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_max_io_size_out Maximum IO Transfer size supported
 *
 *  \return On success returns a device device_handle
 *          On failure returns NI_INVALID_DEVICE_HANDLE
 *******************************************************************************/
LIB_API ni_device_handle_t ni_device_open(const char* dev, uint32_t * p_max_io_size_out);

/*!******************************************************************************
 *  \brief  Closes device and releases resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *
 *  \return NONE
 *
 *******************************************************************************/
LIB_API void ni_device_close(ni_device_handle_t dev);

/*!******************************************************************************
 *  \brief  Queries device and returns device capability structure
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *  \param[in] p_cap  Pointer to a caller allocated ni_device_capability_t struct
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
                       NI_RETCODE_INVALID_PARAM
                       NI_RETCODE_ERROR_MEM_ALOC
                       NI_RETCODE_ERROR_NVME_CMD_FAILED
 *******************************************************************************/
LIB_API ni_retcode_t ni_device_capability_query(ni_device_handle_t device_handle, ni_device_capability_t *p_cap);

/*!******************************************************************************
 *  \brief  Opens a new device session depending on the device_type parameter
 *          If device_type is NI_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_DEVICE_TYPE_EECODER opens encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *                                               ni_session_config_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API ni_retcode_t ni_device_session_open(ni_session_context_t *p_ctx, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Closes device session that was previously opened by calling
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_DEVICE_TYPE_EECODER closes encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] eos_recieved Flag indicating if End Of Stream indicator was recieved
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API ni_retcode_t ni_device_session_close(ni_session_context_t *p_ctx, int eos_recieved, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Sends a flush command to the device
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER sends flush command to decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER sends flush command to decoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API ni_retcode_t ni_device_session_flush(ni_session_context_t *p_ctx, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Sends data the device
 *          If device_type is NI_DEVICE_TYPE_DECODER sends data packet to decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER sends data frame to encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a
 *                          ni_frame_t data frame or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *                          If NI_DEVICE_TYPE_DECODER is specified, it is expected
 *                          that the ni_packet_t struct inside the p_data pointer
 *                          contains data to send.
 *                          If NI_DEVICE_TYPE_ENCODER is specified, it is expected
 *                          that the ni_frame_t struct inside the p_data pointer
 *                          contains data to send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API int ni_device_session_write(ni_session_context_t* p_ctx, ni_session_data_io_t* p_data, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Reads data the device
 *          If device_type is NI_DEVICE_TYPE_DECODER reads data packet from decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER reads data frame from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a
 *                          ni_frame_t data frame or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *                          If NI_DEVICE_TYPE_DECODER is specified, data that was
 *                          read will be placed into ni_frame_t struct inside the p_data pointer
 *                          If NI_DEVICE_TYPE_ENCODER is specified,  data that was
 *                          read will be placed into ni_packet_t struct inside the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API int ni_device_session_read(ni_session_context_t* p_ctx, ni_session_data_io_t* p_data, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Query session data from the device - Currently not implemented
 *          If device_type is NI_DEVICE_TYPE_DECODER query session data
 *          from decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER query session data
 *          from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
LIB_API ni_retcode_t ni_device_session_query(ni_session_context_t* p_ctx, ni_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Allocate memory for the frame buffer based on provided parameters
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                                               ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc(ni_frame_t *pframe, int video_width, int video_height, int alignment, int metadata_flag, int factor);

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
 *  \param[in] alignment     Allignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
LIB_API ni_retcode_t ni_decoder_frame_buffer_alloc(ni_buf_pool_t* p_pool, ni_frame_t *pframe, int alloc_mem, int video_width, int video_height, int alignment, int factor);

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer based on provided parameters
  *          taking into account pic line size and extra data. 
  *          Applicable to YUV420p AVFrame only. Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data)
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
LIB_API ni_retcode_t ni_frame_buffer_alloc_v3(ni_frame_t *pframe, int video_width, int video_height, int linesize[], int alignment, int extra_len);

/*!******************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_frame_buffer_alloc or ni_frame_buffer_alloc_v3
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
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
LIB_API void ni_decoder_frame_buffer_pool_return_buf(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Allocate memory for the packet buffer based on provided packet size
 *
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_packet_t struct
 *  \param[in] packet_size   Required allocation size
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
LIB_API ni_retcode_t ni_packet_buffer_alloc(ni_packet_t *ppacket, int packet_size);

/*!******************************************************************************
 *  \brief  Free packet buffer that was previously allocated with either
 *          ni_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
LIB_API ni_retcode_t ni_packet_buffer_free(ni_packet_t *ppacket);

/*!******************************************************************************
 *  \brief  Copy video packet accounting for allighment
 *
 *  \param[in] p_destination  Destination to where to copy to
 *  \param[in] p_source       Source from where to copy from
 *  \param[in] cur_size       current size
 *  \param[out] p_leftover    Pointer to the data that was left over
 *  \param[out] p_prev_size   Size of the data leftover ??
 *
 *  \return On success        Total number of bytes that were copied
 *          On failure        NI_RETCODE_FAILURE
 *******************************************************************************/
LIB_API int ni_packet_copy(void* p_destination, const void* const p_source, int cur_size, void* p_leftover, int* p_prev_size);

/*!******************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
LIB_API ni_retcode_t ni_encoder_init_default_params(ni_encoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height);


/*!******************************************************************************
 *  \brief  Initialize default decoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
LIB_API ni_retcode_t ni_decoder_init_default_params(ni_encoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height);

/*!******************************************************************************
 *  \brief  Set value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
LIB_API ni_retcode_t ni_encoder_params_set_value(ni_encoder_params_t * p_params, const char *name, const char *value);

/*!******************************************************************************
 *  \brief  Set gop parameter value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
LIB_API ni_retcode_t ni_encoder_gop_params_set_value(ni_encoder_params_t * p_params, const char *name, const char *value);

/*!*****************************************************************************
 *  \brief  Get GOP's max number of reorder frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *
 *  \return max number of reorder frames of the GOP
 ******************************************************************************/
LIB_API int ni_get_num_reorder_of_gop_structure(ni_encoder_params_t * p_params);

/*!*****************************************************************************
 *  \brief  Get GOP's number of reference frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *
 *  \return number of reference frames of the GOP
 ******************************************************************************/
LIB_API int ni_get_num_ref_frame_of_gop_structure(ni_encoder_params_t * p_params);

#ifdef __cplusplus
}
#endif
