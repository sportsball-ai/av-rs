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
 *  \file   ni_device_api_priv.h
 *
 *  \brief  Private definitions used by ni_device_api.c for video processing
 *          tasks
 ******************************************************************************/

#pragma once


#ifdef __cplusplus
extern "C"
{
#endif

#include "ni_defs.h"
#include "ni_rsrc_api.h"

typedef enum
{
  SESSION_READ_CONFIG = 0,
  SESSION_WRITE_CONFIG = 1,
} ni_session_config_rw_type_t;

typedef enum
{
    INST_BUF_INFO_RW_READ = 0,
    INST_BUF_INFO_RW_WRITE = 1,
    INST_BUF_INFO_RW_UPLOAD = 2,
    INST_BUF_INFO_RW_READ_BUSY = 3,
    INST_BUF_INFO_RW_WRITE_BUSY = 4,
    INST_BUF_INFO_R_ACQUIRE = 5,
    INST_BUF_INFO_RW_WRITE_BY_EP = 6,
    INST_BUF_INFO_RW_READ_BY_AI = 7,
} ni_instance_buf_info_rw_type_t;
// mapping of a subset of NI_RETCODE_NVME_SC_* of ni_retcode_t, returned by
// fw in regular i/o environment.
typedef enum _ni_xcoder_mgr_retcode
{
  ni_xcoder_request_success          = 0,      // NVME SC
  ni_xcoder_request_pending          = 1,      // NVME SC 0x305
  ni_xcoder_resource_recovery        = 0xFFFD, // NVME SC 0x3FD
  ni_xcoder_resource_insufficient    = 0xFFFE, // NVME SC 0x3FE
  ni_xcoder_general_error            = 0xFFFF, // NVME SC 0x3FF
} ni_xcoder_mgr_retcode_t;

typedef struct _ni_session_config_rw
{
	uint8_t ui8Enable;
	uint8_t ui8HWAccess;
	union
	{
		uint16_t ui16ReadFrameId;
		uint16_t ui16WriteFrameId;
	} uHWAccessField;
} ni_session_config_rw_t;

typedef struct _ni_instance_mgr_general_status
{
  uint8_t active_sub_instances_cnt; // Number of active sub-instance in that instance
  uint8_t process_load_percent;  // Processing load in percentage
  union{
      uint8_t error_count;
      uint8_t pcie_load;
  };
  union{
      uint8_t fatal_error;
      uint8_t pcie_throughput;
  };
  uint32_t fw_model_load;
  uint8_t cmd_queue_count;
  uint8_t fw_video_mem_usage;
  uint8_t fw_share_mem_usage;
  uint8_t fw_p2p_mem_usage;
  uint8_t active_hwupload_sub_inst_cnt; // number of hwuploader instances
  uint8_t fw_load;
  uint8_t fw_video_shared_mem_usage;
  uint8_t process_load_percent_upper;   // Processing load upper 8 bits
  uint8_t process_load_percent_overall; // Processing load among all namespaces
  uint8_t active_sub_instances_cnt_overall ; // Number of active sub-instance among all namespaces
  uint32_t fw_model_load_overall;       // Model load among all namespaces
  uint8_t admin_nsid; // declares if there is info from admin namespace
  uint8_t tp_fw_load; //uint8_t rsrv;
} ni_instance_mgr_general_status_t;

#define NI_VERSION_CHARACTER_COUNT 8

typedef struct _ni_log_fl_fw_versions
{
  uint8_t last_ran_fl_version[NI_VERSION_CHARACTER_COUNT];
  uint8_t nor_flash_fl_version[NI_VERSION_CHARACTER_COUNT];
  uint8_t current_fw_revision[NI_VERSION_CHARACTER_COUNT];
  uint8_t nor_flash_fw_revision[NI_VERSION_CHARACTER_COUNT];
} ni_log_fl_fw_versions_t;

typedef struct _ni_instance_mgr_stream_info
{
  uint16_t picture_width;  // Picture Width
  uint16_t picture_height; // Picture Height
  uint16_t transfer_frame_stride; // Transfer Frame Stride
  uint16_t transfer_frame_height; // Transfer Frame Height(Not VPU Frame Height)
  uint16_t frame_rate; // Sequence Frame Rate
  uint16_t is_flushed; // Is decoder/encoder flushed or still has data to process
  uint8_t  pix_format;  // Pixel format from decoder Vendor
  uint8_t  reserve;
  uint16_t reserved[1]; 
} ni_instance_mgr_stream_info_t;

typedef struct _ni_session_stats
{

  uint16_t ui16SessionId;
  uint16_t ui16ErrorCount;
  uint32_t ui32LastTransactionId;
  uint32_t ui32LastTransactionCompletionStatus;
  uint32_t ui32LastErrorTransactionId;
  uint32_t ui32LastErrorStatus;
  uint32_t ui32Session_timestamp_high;  // session start timestamp high
  uint32_t ui32Session_timestamp_low;   // session start timestamp low
  uint32_t reserved[1];
} ni_session_stats_t; // 32 bytes (has to be 8 byte aligned)

typedef struct _ni_instance_mgr_allocation_info
{
  uint16_t picture_width;
  uint16_t picture_height;
  uint16_t picture_format;
  uint16_t options;
  uint16_t rectangle_width;
  uint16_t rectangle_height;
  int16_t  rectangle_x;
  int16_t  rectangle_y;
  uint32_t rgba_color;
  uint16_t frame_index;
  uint16_t session_id;
  uint8_t output_index;
  uint8_t input_index;
  uint8_t orientation;
  uint8_t reserved0;
  uint32_t reserved;
} ni_instance_mgr_allocation_info_t;

typedef struct _ni_instance_mgr_stream_complete
{
  uint16_t is_flushed;     // Is decoder/encoder flushed or still has data to process
  uint16_t data_bytes_available; // How much data is available to read/write
} ni_instance_mgr_stream_complete_t;

typedef struct _ni_instance_upload_ret_hwdesc
{
    int16_t buffer_avail;   // 1 ==  avail, else not avail
    int16_t frame_index;    // memory bin ID
} ni_instance_upload_ret_hwdesc_t;

typedef struct _ni_instance_buf_info
{
  union
  {
    uint32_t buf_avail_size;  // available size of space/data for write/read
    ni_instance_upload_ret_hwdesc_t hw_inst_ind;
  };
} ni_instance_buf_info_t;

typedef struct _ni_encoder_frame_params
{
  uint16_t force_picture_type; //flag to force the pic type
  uint16_t data_format;   // In Env write this is usually set to NI_DATA_FORMAT_YUV_FRAME
  uint16_t picture_type;  // This is set to either XCODER_PIC_TYPE_I or XCODER_PIC_TYPE_CRA or XCODER_PIC_TYPE_IDR
  uint16_t video_width;
  uint16_t video_height;
  uint32_t timestamp;
} ni_encoder_frame_params_t;

// 24 bytes
typedef struct _ni_metadata_common
{
  uint16_t crop_left;
  uint16_t crop_right;
  uint16_t crop_top;
  uint16_t crop_bottom;
  union
  {
    uint64_t frame_offset;
    uint64_t frame_tstamp;
  } ui64_data;
  uint16_t frame_width;
  uint16_t frame_height;
  uint16_t frame_type;
  uint8_t has_b_frame;
  uint8_t pkt_delay_cnt;
} ni_metadata_common_t;

// 48 bytes
typedef struct _ni_metadata_dec_frame
{
  ni_metadata_common_t metadata_common;
  union {
    uint32_t             sei_header;
    ni_sei_header_t      first_sei_header;
  };
  uint16_t             sei_number;
  uint16_t             sei_size;
  niFrameSurface1_t    hwdesc;
} ni_metadata_dec_frame_t;

// 56 bytes
typedef struct _ni_metadata_enc_frame
{
  ni_metadata_common_t  metadata_common;
  uint32_t              frame_roi_avg_qp;
  uint32_t              frame_roi_map_size;
  uint32_t              frame_sei_data_size;
  uint32_t              enc_reconfig_data_size;
  uint16_t              frame_force_type_enable;
  uint16_t              frame_force_type;
  uint16_t              force_pic_qp_enable;
  uint16_t              force_pic_qp_i;
  uint16_t              force_pic_qp_p;
  uint16_t              force_pic_qp_b;
  uint16_t force_headers;
  uint8_t use_cur_src_as_long_term_pic;
  uint8_t use_long_term_ref;
  //uint32_t              reserved;
  uint16_t              start_len[3];
  uint8_t                inconsecutive_transfer;
  uint8_t                reserved;
} ni_metadata_enc_frame_t;

typedef struct _ni_metadata_enc_bstream_rev61
{
    uint32_t      bs_frame_size;
    uint32_t      frame_type;
    uint64_t      frame_tstamp;
    uint32_t      frame_cycle;
    uint32_t      avg_frame_qp;
    uint32_t      recycle_index;
    uint32_t av1_show_frame;
} ni_metadata_enc_bstream_rev61_t; // Revision 61 or lower

typedef struct _ni_metadata_enc_bstream
{
    uint32_t      metadata_size;
    uint32_t      frame_type;
    uint64_t      frame_tstamp;
    uint32_t      frame_cycle;
    uint32_t      avg_frame_qp;
    uint32_t      recycle_index;
    uint32_t      av1_show_frame;
    //Added for Revision 61
    uint32_t      ssimY;
    uint32_t      ssimU;
    uint32_t      ssimV;
    //Added for Revision 6p
    int16_t       max_mv_x[2]; // 1/4 pixel unit
    int16_t       min_mv_x[2]; // 1/4 pixel unit
    int16_t       max_mv_y[2]; // 1/4 pixel unit
    int16_t       min_mv_y[2]; // 1/4 pixel unit
    uint16_t      frame_size;
    uint16_t      inter_total_count; // the inter includ the skip mode
    uint16_t      intra_total_count;
    uint8_t        gop_size;
    uint8_t        reserved_bytes[3];
    uint32_t      reserved[4];
} ni_metadata_enc_bstream_t;

/*!****** encoder paramters *********************************************/

#define TUNE_BFRAME_VISUAL_MEDIUM     1
#define TUNE_BFRAME_VISUAL_HIGH     2

typedef enum _ni_gop_preset_idx
{
    GOP_PRESET_IDX_DEFAULT =
        -1, /*!*< Default, gop decided by gopSize. E.g gopSize=0 is Adpative gop, gopsize adjusted dynamically */
    GOP_PRESET_IDX_CUSTOM = 0,
    GOP_PRESET_IDX_ALL_I = 1, /*!*< All Intra, gopsize = 1 */
    GOP_PRESET_IDX_IPP = 2,   /*!*< Consecutive P, cyclic gopsize = 1  */
    GOP_PRESET_IDX_IBBB = 3,  /*!*< Consecutive B, cyclic gopsize = 1  */
    GOP_PRESET_IDX_IBPBP = 4, /*!*< gopsize = 2  */
    GOP_PRESET_IDX_IBBBP = 5, /*!*< gopsize = 4  */
    GOP_PRESET_IDX_IPPPP = 6, /*!*< Consecutive P, cyclic gopsize = 4 */
    GOP_PRESET_IDX_IBBBB = 7, /*!*< Consecutive B, cyclic gopsize = 4 */
    GOP_PRESET_IDX_RA_IB = 8, /*!*< Random Access, cyclic gopsize = 8 */
    GOP_PRESET_IDX_SP = 9, /*!*< Consecutive P, gopsize=1, similar to 2 but */
                           /* uses 1 instead of 2 reference frames */
    GOP_PRESET_IDX_HIERARCHICAL_IPPPP = 10,   /*!*< Hierarchical P, cyclic gopsize = 4  */
} ni_gop_preset_idx_t;

/*!*
* \brief
@verbatim
This is an enumeration for declaring codec standard type variables. Currently,
VPU supports many different video standards such as H.265/HEVC, MPEG4 SP/ASP, H.263 Profile 3, H.264/AVC
BP/MP/HP, VC1 SP/MP/AP, Divx3, MPEG1, MPEG2, RealVideo 8/9/10, AVS Jizhun/Guangdian profile, AVS2,
 Theora, VP3, VP8/VP9 and SVAC.

NOTE: MPEG-1 decoder operation is handled as a special case of MPEG2 decoder.
STD_THO must be always 9.
@endverbatim
*/
typedef enum
{
    STD_AVC,
    STD_VC1,
    STD_MPEG2,
    STD_MPEG4,
    STD_H263,
    STD_DIV3,
    STD_RV,
    STD_AVS,
    STD_THO = 9,
    STD_VP3,
    STD_VP8,
    STD_HEVC,
    STD_VP9,
    STD_AVS2,
    STD_SVAC,
    STD_AV1,
    STD_JPEG,
    STD_MAX
} ni_bitstream_format_t;

typedef struct _ni_t408_config_t
{
  int32_t profile;
  int32_t level;
  int32_t tier;
  int32_t internalBitDepth;
  int32_t losslessEnable;     /*!*< It enables lossless coding. */
  int32_t constIntraPredFlag; /*!*< It enables constrained intra prediction. */
  int32_t gop_preset_index;
  int32_t decoding_refresh_type;
  int32_t intra_qp;     /*!*< A quantization parameter of intra picture */
  int32_t intra_period; /*!*< A period of intra picture in GOP size */
  int32_t conf_win_top;   /*!*< A top offset of conformance window */
  int32_t conf_win_bottom;   /*!*< A bottom offset of conformance window */
  int32_t conf_win_left;  /*!*< A left offset of conformance window */
  int32_t conf_win_right; /*!*< A right offset of conformance window */
  int32_t independSliceMode;
  int32_t independSliceModeArg; /*!*< The number of CTU for a slice when independSliceMode is set with 1  */
  int32_t dependSliceMode;
  int32_t dependSliceModeArg; /*!*< The number of CTU or bytes for a slice when dependSliceMode is set with 1 or 2  */
  int32_t intraRefreshMode;
  int32_t intraRefreshArg;
  int32_t use_recommend_enc_params;
  int32_t scalingListEnable; /*!*< It enables a scaling list. */
  int32_t cu_size_mode;
  int32_t tmvpEnable;                 /*!*< It enables temporal motion vector prediction. */
  int32_t wppEnable;                  /*!*< It enables WPP (T408-front Parallel Processing). WPP is unsupported in ring buffer mode of bitstream buffer. */
  int32_t max_num_merge;                /*!*< It specifies the number of merge candidates in RDO (1 or 2). 2 of max_num_merge (default) offers better quality of encoded picture, while 1 of max_num_merge improves encoding performance.  */
  int32_t disableDeblk;               /*!*< It disables in-loop deblocking filtering. */
  int32_t lfCrossSliceBoundaryEnable; /*!*< It enables filtering across slice boundaries for in-loop deblocking. */
  int32_t betaOffsetDiv2;             /*!*< It sets BetaOffsetDiv2 for deblocking filter. */
  int32_t tcOffsetDiv2;               /*!*< It sets TcOffsetDiv3 for deblocking filter. */
  int32_t skipIntraTrans;             /*!*< It enables transform skip for an intra CU. */
  int32_t saoEnable;                  /*!*< It enables SAO (Sample Adaptive Offset). */
  int32_t intraNxNEnable;             /*!*< It enables intra NxN PUs. */
  int32_t bitAllocMode;
  int32_t fixedBitRatio[NI_MAX_GOP_NUM];
  int32_t enable_cu_level_rate_control; /*!*< It enable CU level rate control. */
  int32_t enable_hvs_qp; /*!*< It enable CU QP adjustment for subjective quality enhancement. */
  int32_t hvs_qp_scale; /*!*< A QP scaling factor for CU QP adjustment when enable_hvs_qp_scale is 1 */
  int32_t max_delta_qp; /*!*< A maximum delta QP for rate control */

  // CUSTOM_GOP
  ni_custom_gop_params_t custom_gop_params; /*!*< <<vpuapi_h_CustomGopParam>> */
  int32_t roiEnable;           /*!*< It enables ROI map. NOTE: It is valid when rate control is on. */

  uint32_t numUnitsInTick;     /*!*< It specifies the number of time units of a clock operating at the frequency time_scale Hz. This is used to to calculate frame_rate syntax.  */
  uint32_t timeScale;          /*!*< It specifies the number of time units that pass in one second. This is used to to calculate frame_rate syntax.  */
  uint32_t numTicksPocDiffOne; /*!*< It specifies the number of clock ticks corresponding to a difference of picture order count values equal to 1. This is used to calculate frame_rate syntax. */

  int32_t chromaCbQpOffset; /*!*< The value of chroma(Cb) QP offset */
  int32_t chromaCrQpOffset; /*!*< The value of chroma(Cr) QP offset */

  int32_t initialRcQp; /*!*< The value of initial QP by HOST application. This value is meaningless if INITIAL_RC_QP is 63.*/

  uint32_t nrYEnable;  /*!*< It enables noise reduction algorithm to Y component.  */
  uint32_t nrCbEnable; /*!*< It enables noise reduction algorithm to Cb component. */
  uint32_t nrCrEnable; /*!*< It enables noise reduction algorithm to Cr component. */

  // ENC_NR_WEIGHT
  uint32_t nrIntraWeightY;  /*!*< A weight to Y noise level for intra picture (0 ~ 31). nrIntraWeight/4 is multiplied to the noise level that has been estimated. This weight is put for intra frame to be filtered more strongly or more weakly than just with the estimated noise level. */
  uint32_t nrIntraWeightCb; /*!*< A weight to Cb noise level for intra picture (0 ~ 31) */
  uint32_t nrIntraWeightCr; /*!*< A weight to Cr noise level for intra picture (0 ~ 31) */
  uint32_t nrInterWeightY;  /*!*< A weight to Y noise level for inter picture (0 ~ 31). nrInterWeight/4 is multiplied to the noise level that has been estimated. This weight is put for inter frame to be filtered more strongly or more weakly than just with the estimated noise level. */
  uint32_t nrInterWeightCb; /*!*< A weight to Cb noise level for inter picture (0 ~ 31) */
  uint32_t nrInterWeightCr; /*!*< A weight to Cr noise level for inter picture (0 ~ 31) */

  uint32_t nrNoiseEstEnable; /*!*< It enables noise estimation for noise reduction. When this is disabled, host carries out noise estimation with nrNoiseSigmaY/Cb/Cr. */
  uint32_t nrNoiseSigmaY;    /*!*< It specifies Y noise standard deviation when nrNoiseEstEnable is 0.  */
  uint32_t nrNoiseSigmaCb;   /*!*< It specifies Cb noise standard deviation when nrNoiseEstEnable is 0. */
  uint32_t nrNoiseSigmaCr;   /*!*< It specifies Cr noise standard deviation when nrNoiseEstEnable is 0. */

  uint32_t useLongTerm; /*!*< It enables long-term reference function. */

  // newly added for T408
  uint32_t monochromeEnable;        /*!*< It enables monochrome encoding mode. */
  uint32_t strongIntraSmoothEnable; /*!*< It enables strong intra smoothing. */

  uint32_t weightPredEnable; /*!*< It enables to use weighted prediction.*/
  uint32_t bgDetectEnable;   /*!*< It enables background detection. */
  uint32_t bgThrDiff;        /*!*< It specifies the threshold of max difference that is used in s2me block. It is valid when background detection is on. */
  uint32_t bgThrMeanDiff;    /*!*< It specifies the threshold of mean difference that is used in s2me block. It is valid  when background detection is on. */
  uint32_t bgLambdaQp;       /*!*< It specifies the minimum lambda QP value to be used in the background area. */
  int32_t bgDeltaQp;             /*!*< It specifies the difference between the lambda QP value of background and the lambda QP value of foreground. */

  uint32_t customLambdaEnable;  /*!*< It enables custom lambda table. */
  uint32_t customMDEnable;      /*!*< It enables custom mode decision. */
  int32_t pu04DeltaRate;            /*!*< A value which is added to the total cost of 4x4 blocks */
  int32_t pu08DeltaRate;            /*!*< A value which is added to the total cost of 8x8 blocks */
  int32_t pu16DeltaRate;            /*!*< A value which is added to the total cost of 16x16 blocks */
  int32_t pu32DeltaRate;            /*!*< A value which is added to the total cost of 32x32 blocks */
  int32_t pu04IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 4x4 Planar intra prediction mode. */
  int32_t pu04IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost (=distortion + rate) in 4x4 DC intra prediction mode. */
  int32_t pu04IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost (=distortion + rate) in 4x4 Angular intra prediction mode.  */
  int32_t pu08IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 8x8 Planar intra prediction mode.*/
  int32_t pu08IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 8x8 DC intra prediction mode.*/
  int32_t pu08IntraAngleDeltaRate;  /*!*< A value which is added to  rate when calculating cost(=distortion + rate) in 8x8 Angular intra prediction mode. */
  int32_t pu16IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 16x16 Planar intra prediction mode. */
  int32_t pu16IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 16x16 DC intra prediction mode */
  int32_t pu16IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 16x16 Angular intra prediction mode */
  int32_t pu32IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 32x32 Planar intra prediction mode */
  int32_t pu32IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 32x32 DC intra prediction mode */
  int32_t pu32IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost(=distortion + rate) in 32x32 Angular intra prediction mode */
  int32_t cu08IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU8x8 */
  int32_t cu08InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU8x8 */
  int32_t cu08MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU8x8 */
  int32_t cu16IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU16x16 */
  int32_t cu16InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU16x16 */
  int32_t cu16MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU16x16 */
  int32_t cu32IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU32x32 */
  int32_t cu32InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU32x32 */
  int32_t cu32MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU32x32 */
  int32_t coefClearDisable;         /*!*< It disables the transform coefficient clearing algorithm for P or B picture. If this is 1, all-zero coefficient block is not evaluated in RDO. */
  int32_t minQpI;                   /*!*< A minimum QP of I picture for rate control */
  int32_t maxQpI;                   /*!*< A maximum QP of I picture for rate control */
  int32_t minQpP;                   /*!*< A minimum QP of P picture for rate control */
  int32_t maxQpP;                   /*!*< A maximum QP of P picture for rate control */
  int32_t minQpB;                   /*!*< A minimum QP of B picture for rate control */
  int32_t maxQpB;                   /*!*< A maximum QP of B picture for rate control */

  // for H.264 on T408
  int32_t avcIdrPeriod;        /*!*< A period of IDR picture (0 ~ 1024) 0 - implies an infinite period */
  int32_t rdoSkip;             /*!*< It skips RDO(rate distortion optimization). */
  int32_t lambdaScalingEnable; /*!*< It enables lambda scaling using custom GOP. */
  int32_t enable_transform_8x8;  /*!*< It enables 8x8 intra prediction and 8x8 transform. */
  int32_t slice_mode;
  int32_t slice_arg; /*!*< The number of MB for a slice when slice_mode is set with 1  */
  int32_t intra_mb_refresh_mode;
  int32_t intra_mb_refresh_arg;
  int32_t enable_mb_level_rc; /*!*< It enables MB-level rate control. */
  int32_t entropy_coding_mode;

    /**< It enables every I-frames to include VPS/SPS/PPS. */
    // forcedHeaderEnable = 2:Every IRAP frame includes headers(VPS, SPS, PPS)
    // forcedHeaderEnable = 1:Every IDR frame includes headers (VPS,SPS, PPS).
    // forcedHeaderEnable = 0:First IDR frame includes headers (VPS,SPS, PPS).
    uint32_t forcedHeaderEnable;
} ni_t408_config_t;

typedef struct _ni_encoder_config_t
{
  uint8_t ui8bitstreamFormat; /*!*< The standard type of bitstream in encoder operation. It is one of STD_AVC and STD_HEVC, use enums from ni_bitstream_format_t. */
  int32_t i32picWidth;        /*!*< The width of a picture to be encoded in unit of sample. */
  int32_t i32picHeight;       /*!*< The height of a picture to be encoded in unit of sample. */
  int32_t i32meBlkMode;       // (AVC ONLY)
  uint8_t ui8sliceMode;       /*!*< <<vpuapi_h_EncSliceMode>> */
  int32_t i32frameRateInfo;
  int32_t i32vbvBufferSize;
  int32_t i32userQpMax;
  // AVC only
  int32_t i32maxIntraSize;   /*!*< The maximum bit size for intra frame. (H.264/AVC only) */
  int32_t i32userMaxDeltaQp; /*!*< The maximum delta QP for encoding process. (H.264/AVC only) */
  int32_t i32userMinDeltaQp; /*!*< The minimum delta QP for encoding process. (H.264/AVC only) */
  int32_t i32userQpMin;      /*!*< The minimum quantized step parameter for encoding process. (H.264/AVC only) */
  int32_t i32bitRate;
  int32_t i32bitRateBL;
  uint8_t ui8rcEnable;
  int32_t i32srcBitDepth;    /*!*< A bit-depth of source image */
  uint8_t ui8enablePTS;      /*!*< An enable flag to report PTS(Presentation Timestamp) */
  uint8_t ui8lowLatencyMode; /*!*< 2bits low latency mode setting. bit[1]: low latency interrupt enable, bit[0]: fast bitstream-packing enable (only for T408_5) */

  ni_t408_config_t niParamT408; /*!*< <<vpuapi_h_EncT408Param>> */

  /*!*< endianess of 10 bit source YUV. 0: little (default) 1: big */
  uint32_t ui32sourceEndian;

  /**> 0=no HDR in VUI, 1=add HDR info to VUI **/
  uint32_t hdrEnableVUI; /* Ignored by Quadra. Kept for backward and forward compatibility. */
  uint32_t ui32VuiDataSizeBits;       /**< size of VUI RBSP in bits **/
  uint32_t ui32VuiDataSizeBytes;     /**< size of VUI RBSP in bytes up to NI_MAX_VUI_SIZE **/
  int32_t  i32hwframes;
  uint8_t  ui8EnableAUD;             /**< Enables Access Unit Delimiter if set to 1 **/
  uint8_t  ui8LookAheadDepth;        /**Number of frames to look ahead while encoding**/
  uint8_t  ui8rdoLevel;              /*Number of candidates to use for rate distortion optimization*/
  int8_t   i8crf;                    /*constant rate factor mode*/
  uint16_t ui16HDR10MaxLight;        /*Max content light level*/
  uint16_t ui16HDR10AveLight;        /*Max picture Average content light level*/
  uint8_t ui8HDR10CLLEnable;         /*Max picture Average content light level*/
  uint8_t  ui8EnableRdoQuant;        /*Enables RDO quant*/
  uint8_t  ui8repeatHeaders;         /*Repeat the headers every Iframe*/
  uint8_t  ui8ctbRcMode;             /*CTB QP adjustment mode for Rate Control and Subjective Quality*/
  uint8_t  ui8gopSize;               /*Specifies GOP size, 0 is adaptive*/  
  uint8_t  ui8useLowDelayPocType;    /*picture_order_count_type in the H.264 SPS */
  uint8_t  ui8gopLowdelay;               /*Use low delay GOP configuration*/
  uint16_t ui16gdrDuration;          /*intra Refresh period*/
  uint8_t  ui8hrdEnable;             /*Enables hypothetical Reference Decoder compliance*/
  uint8_t  ui8colorDescPresent; 
  uint8_t  ui8colorPrimaries;       
  uint8_t  ui8colorTrc;
  uint8_t  ui8colorSpace;
  uint16_t ui16aspectRatioWidth;
  uint16_t ui16aspectRatioHeight;
  uint16_t ui16rootBufId;            /*Non zero values override frame buffer allocation to match this buffer*/
  uint8_t  ui8planarFormat;
  uint8_t  ui8PixelFormat;           /* pixel format */
  int32_t i32frameRateDenominator;
  int8_t i8intraQpDelta;
  uint8_t  ui8fillerEnable;
  uint8_t  ui8picSkipEnable;
  uint32_t ui32ltrRefInterval;
  int32_t i32ltrRefQpOffset;
  uint32_t ui32ltrFirstGap;
  uint32_t ui32ltrNextInterval;
  uint8_t ui8multicoreJointMode;
  uint8_t ui8videoFullRange;
  uint32_t ui32setLongTermInterval;     /* sets long-term reference interval */
  uint32_t ui32QLevel;                  /* JPEG Quantization scale (0-9) */
  int8_t i8chromaQpOffset;
  int32_t i32tolCtbRcInter;
  int32_t i32tolCtbRcIntra;
  int16_t i16bitrateWindow;
  // experimental parameters, not to be set by user --->
  uint8_t ui8inLoopDSRatio;
  uint8_t ui8blockRCSize;
  // <--- experimental parameters, not to be set by user
  uint8_t ui8rcQpDeltaRange;
  uint8_t ui8LowDelay;
  uint8_t ui8setLongTermCount; /* sets long-term reference frames count */
  uint16_t ui16maxFrameSize;
  uint8_t ui8enableSSIM;
  uint8_t  ui8hdr10_enable;
  uint16_t ui16hdr10_dx0;
  uint16_t ui16hdr10_dy0;
  uint16_t ui16hdr10_dx1;
  uint16_t ui16hdr10_dy1;
  uint16_t ui16hdr10_dx2;
  uint16_t ui16hdr10_dy2;
  uint16_t ui16hdr10_wx;
  uint16_t ui16hdr10_wy;
  uint32_t ui32hdr10_maxluma;
  uint32_t ui32hdr10_minluma;
  int8_t i8skipFrameEnable;
  int8_t i8maxConsecutiveSkipFrameNum;
  int8_t i8enableipRatio;
  uint8_t u8skipFrameInterval;
  uint16_t ui16iFrameSizeRatio;
  uint8_t rsvd2;
  uint8_t ui8fixedframerate;
  uint8_t ui8av1ErrResilientMode;
  uint8_t ui8intraResetRefresh;              /**< reset intra refresh on force IDR frame. */
  int16_t i16ctbRowQpStep;
  uint8_t ui8NewRCEnable;  // <--- experimental parameters, not to be set by user
  uint8_t ui8temporalLayersEnable;
  uint8_t ui8AiEnhanceMode;
  uint8_t ui8enable2PassGopPatern;
  uint32_t ui32lumaLinesize;
  uint32_t ui32chromaLinesize;
  uint32_t ui32cropWidth;
  uint32_t ui32cropHeight;
  uint32_t ui32horOffset;
  uint32_t ui32verOffset;
  uint8_t ui8AiEnhanceLevel;
  int8_t   i8crfMax;  // <--- experimental parameters, not to be set by user                  /* prevent quadra from reducing the ratefactor below the given value */
  int32_t i32qcomp;
  // experimental parameters, not to be set by user --->
  uint8_t ui8noMbtree;
  uint8_t ui8noHWMultiPassSupport;
  int8_t   i8cuTreeFactor;
  // <--- experimental parameters, not to be set by user
  int32_t  i32ipRatio;
  int32_t  i32pbRatio;
  int32_t  i32cplxDecay;
  uint32_t ui32vbvMaxRate;
  int8_t i8ppsInitQp;
  uint8_t ui8bitrateMode;
  int8_t i8pass1Qp; // <--- experimental parameters, not to be set by user
  int8_t i8crfDecimal;
  int8_t    i8hvsBaseMbComplexity;
  int8_t i8statisticOutputLevel;
  uint8_t ui8crfMaxIframeEnable;
  uint32_t ui32vbvMinRate;
  uint8_t ui8disableBframeRDOQ;
  int32_t i32forceBframeQpFactor;
  uint8_t ui8tuneBframeVisual;
  uint8_t ui8EnableAcqLimit;
} ni_encoder_config_t;

typedef struct _ni_uploader_config_t
{
    uint16_t ui16picWidth;
    uint16_t ui16picHeight;
    uint8_t ui8poolSize;
    uint8_t ui8PixelFormat;
    uint8_t ui8Pool;
    uint8_t ui8rsvd[1];
    uint32_t   ui32lumaLinesize;
    uint32_t   ui32chromaLinesize;
} ni_uploader_config_t;

// struct describing resolution change.
typedef struct _ni_resolution
{
    // width
    int32_t width;

    // height
    int32_t height;

    // bit depth factor
    int32_t bit_depth_factor;

    int32_t luma_linesize;
    int32_t chroma_linesize;
} ni_resolution_t;

#define NI_MINIMUM_CROPPED_LENGTH 48
typedef enum {
  CROP_DISABLED = 0,
  CROP_AUTO,
  CROP_MANUAL
}ni_decoder_crop_mode;

typedef struct {
  uint16_t                    ui16X;
  uint16_t                    ui16Y;
  uint16_t                    ui16W;
  uint16_t                    ui16H;
}ni_decode_cropping_rectangle;

typedef struct {
  uint16_t                    ui16Height;
  uint16_t                    ui16Width;
}ni_decoder_output_picture_size;

typedef struct {
  uint8_t                           ui8Enabled;
  uint8_t                           ui8Force8Bit;
  uint8_t                           ui8CropMode;
  uint8_t                           ui8ScaleEnabled;
  uint8_t                           ui8SemiPlanarEnabled;
  // ui8EnablePpuScaleAdapt 0: disable 1: enable long-edge adapt 2: enable short-edge adapt
  // 3: enable long-edge adapt 4: enable short-edge adapt
  // 1 and 2 is that default rounding down to 2, and don't check change in area before and after
  // 3 and 4 is that default rounding up to 2, and will check change in area before and after
  uint8_t                           ui8EnablePpuScaleAdapt;
  uint8_t                           ui8EnablePpuScaleLimit;
  uint8_t                           ui8ScaleResCeil;  // even is up, odd is down (odd+1)
  ni_decode_cropping_rectangle      sCroppingRectable;
  ni_decoder_output_picture_size    sOutputPictureSize;
}ni_decoder_output_config_t;

typedef struct _ni_decoder_config_t
{
  uint8_t                     ui8HWFrame;
  uint8_t                     ui8UduSeiEnabled; // ui8OutputFormat;
  uint16_t                    ui16MaxSeiDataSize;
  uint32_t fps_number;
  uint32_t fps_denominator;
  uint8_t ui8MCMode;
  uint8_t ui8DisablePictureReordering;
  uint8_t ui8EnablelowDelayCheck;
  uint8_t ui8MaxExtraHwFrameCnt;
  uint32_t ui32MaxPktSize;
  ni_decoder_output_config_t  asOutputConfig[NI_MAX_NUM_OF_DECODER_OUTPUTS];
  uint8_t ui8EcPolicy;
  uint8_t ui8EnableAdvancedEc;
  uint8_t ui8DisableAdaptiveBuffers;
  uint8_t ui8reserved;
  uint32_t ui32SourceWidth;
  uint32_t ui32SourceHeight;
} ni_decoder_config_t;

typedef struct _ni_ai_config_t
{
    uint32_t ui32NetworkBinarySize;
    uint8_t ui8Sha256[32];
} ni_ai_config_t;

typedef struct _ni_network_buffer
{
    uint16_t ui16FrameScale;
    uint16_t ui16Width;
    uint16_t ui16Height;
    uint16_t ui16Option;
    uint8_t ui8PoolSize;
    uint8_t ui8MultiIn;
    uint16_t ui16FrameIdx[4];
} ni_network_buffer_t;

typedef struct _ni_scaler_config
{
    uint8_t filterblit;
    uint8_t numInputs;
    uint8_t ui8Reserved[2];
    uint32_t ui32Reserved[3];
} ni_scaler_config_t;

typedef struct _ni_ddr_priority_config
{
    uint8_t  ddr_mode;
    uint8_t  ui8Reserved[7];
} ni_ddr_priority_config_t;

// the following enum and struct are copied from firmware/nvme/vpuapi/vpuapi.h
/*!*
* \brief
@verbatim
This is an enumeration for declaring SET_PARAM command options. (_T400_ENC encoder only)
Depending on this, SET_PARAM command parameter registers from 0x15C have different meanings.

@endverbatim
*/
typedef enum
{
  OPT_COMMON = 0,
  OPT_CUSTOM_GOP = 1,
  OPT_SEI = 2,
  OPT_VUI = 3
} ni_set_param_option_t;

#define FRAME_CHUNK_INDEX_SIZE  4096
#define NI_SESSION_CLOSE_RETRY_MAX 10
#define NI_SESSION_CLOSE_RETRY_INTERVAL_US 500000

/* This value must agree with the membin size in Quadra firmware */
#define NI_HWDESC_UNIFIED_MEMBIN_SIZE 0x187000

#define NI_QUADRA_MEMORY_CONFIG_DR   0
#define NI_QUADRA_MEMORY_CONFIG_SR    1
#define NI_QUADRA_MEMORY_CONFIG_SR2_REMOVE_P2P    2
#define NI_QUADRA_MEMORY_CONFIG_SR_4G   3


#define NI_MAX_AI_NETWORK_BINARY_BUFFER_QUERY_RETRIES 300000
#define NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES     15000
#define NI_RETRY_INTERVAL_200US                       200
#define NI_RETRY_INTERVAL_100US                       100

// size of meta data sent together with bitstream: from f/w encoder to app for FW/SW before rev 6.1
#define NI_FW_ENC_BITSTREAM_META_DATA_SIZE 32
// size of meta data sent together with bitstream: from f/w encoder to app for FW/SW before rev 6.o
#define NI_FW_ENC_BITSTREAM_META_DATA_SIZE_UNDER_MAJOR_6_MINOR_o 48

int ni_create_frame(ni_frame_t* p_frame, uint32_t read_length, uint64_t* frame_offset, bool is_hw_frame);

void ni_set_custom_template(ni_session_context_t *p_ctx,
                            ni_encoder_config_t *p_cfg,
                            ni_xcoder_params_t *p_src);
void ni_set_custom_dec_template(ni_session_context_t *p_ctx,
                                ni_decoder_config_t *p_cfg,
                                ni_xcoder_params_t *p_src,
                                uint32_t max_pkt_size);
void ni_set_default_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_config);
ni_retcode_t ni_validate_custom_template(ni_session_context_t *p_ctx,
                                         ni_encoder_config_t *p_cfg,
                                         ni_xcoder_params_t *p_src,
                                         char *p_param_err,
                                         uint32_t max_err_len);
ni_retcode_t ni_validate_custom_dec_template(ni_xcoder_params_t *p_src,
                                             ni_session_context_t *p_ctx,
                                             ni_decoder_config_t *p_cfg,
                                             char *p_param_err,
                                             uint32_t max_err_len);

ni_retcode_t ni_check_common_params(ni_t408_config_t *p_param,
                                    ni_xcoder_params_t *p_src, char *param_err,
                                    uint32_t max_err_len);
ni_retcode_t ni_check_ratecontrol_params(ni_encoder_config_t* p_cfg, char* param_err, uint32_t max_err_len);

void ni_params_print(ni_xcoder_params_t *const p_encoder_params);

int32_t ni_get_frame_index(uint32_t* value);
void ni_populate_device_capability_struct(ni_device_capability_t* p_cap, void * p_data);

int ni_xcoder_session_query(ni_session_context_t *p_ctx,
                            ni_device_type_t device_type);
int ni_xcoder_session_query_detail(ni_session_context_t *p_ctx,
                            ni_device_type_t device_type, void *detail_data, int ver);

ni_retcode_t ni_config_instance_set_decoder_params(ni_session_context_t* p_ctx, uint32_t max_pkt_size);
ni_retcode_t ni_decoder_session_open(ni_session_context_t *p_ctx);
ni_retcode_t ni_decoder_session_close(ni_session_context_t *p_ctx, int eos_recieved);
ni_retcode_t ni_decoder_session_send_eos(ni_session_context_t *p_ctx);
ni_retcode_t ni_decoder_session_flush(ni_session_context_t *p_ctx);

int ni_decoder_session_write(ni_session_context_t *p_ctx, ni_packet_t *p_packet);
int ni_decoder_session_read(ni_session_context_t *p_ctx, ni_frame_t *p_frame);

ni_retcode_t ni_encoder_session_open(ni_session_context_t *p_ctx);
ni_retcode_t ni_encoder_session_close(ni_session_context_t *p_ctx, int eos_recieved);
ni_retcode_t ni_encoder_session_send_eos(ni_session_context_t *p_ctx);
int ni_encoder_session_write(ni_session_context_t *p_ctx, ni_frame_t *p_frame);
int ni_encoder_session_read(ni_session_context_t *p_ctx, ni_packet_t *p_packet);
ni_retcode_t ni_encoder_session_sequence_change(ni_session_context_t *p_ctx, ni_resolution_t *p_resoluion);
//int ni_encoder_session_reconfig(ni_session_context_t *p_ctx, ni_session_config_t *p_config, ni_param_change_flags_t change_flags);

ni_retcode_t ni_query_general_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_general_status_t* p_gen_status);
ni_retcode_t ni_query_detail_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, void* p_detail_status, int ver);

ni_retcode_t ni_query_stream_info(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_info_t* p_stream_info);
ni_retcode_t ni_query_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_complete_t* p_stream_complete);

ni_retcode_t ni_query_instance_buf_info(ni_session_context_t* p_ctx, ni_instance_buf_info_rw_type_t rw_type, ni_device_type_t device_type, ni_instance_buf_info_t *p_inst_buf_info);
ni_retcode_t ni_query_session_stats(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_session_stats_t* p_session_stats, int rc, int opcode);
ni_retcode_t
ni_query_session_statistic_info(ni_session_context_t *p_ctx,
                                ni_device_type_t device_type,
                                ni_session_statistic_t *p_session_statistic);

ni_retcode_t ni_config_session_rw(ni_session_context_t* p_ctx, ni_session_config_rw_type_t rw_type, uint8_t enable, uint8_t hw_action, uint16_t frame_id);
ni_retcode_t ni_config_instance_sos(ni_session_context_t* p_ctx, ni_device_type_t device_type);
ni_retcode_t ni_config_instance_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type);
ni_retcode_t ni_config_instance_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type);
ni_retcode_t ni_config_instance_set_encoder_params(ni_session_context_t* p_ctx);
ni_retcode_t ni_config_instance_update_encoder_params(ni_session_context_t* p_ctx, ni_param_change_flags_t change_flags);
ni_retcode_t ni_config_instance_set_encoder_frame_params(ni_session_context_t* p_ctx, ni_encoder_frame_params_t* p_params);
ni_retcode_t ni_config_instance_set_write_len(ni_session_context_t* p_ctx, ni_device_type_t device_type, uint32_t len);
ni_retcode_t ni_config_instance_set_sequence_change(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_resolution_t *p_resolution);
void ni_encoder_set_vui(uint8_t* vui, ni_encoder_config_t *p_cfg);
void *ni_session_keep_alive_thread(void *arguments);
ni_retcode_t ni_send_session_keep_alive(uint32_t session_id, ni_device_handle_t device_handle, ni_event_handle_t event_handle, void *p_data);
void ni_fix_VUI(uint8_t *vui, int pos, int value);

/*!******************************************************************************
*  \brief  Copy a xcoder decoder worker thread info and card info
*
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_copy_internal(ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx);
/*!******************************************************************************
*  \brief  Copy a xcoder decoder worker thread info and card info
*
*  \param
*
*  \return
*******************************************************************************/
int ni_decoder_session_read_desc(ni_session_context_t* p_ctx, ni_frame_t* p_frame);
/*!******************************************************************************
*  \brief  Open a xcoder uploader instance
*
*  \param   p_ctx   - pointer to caller allocated uploader session context
*
*  \return
*           On success
*               NI_RETCODE_SUCCESS
*
*           On failure
*               NI_RETCODE_INVALID_PARAM
*               NI_RETCODE_ERROR_MEM_ALOC
*               NI_RETCODE_ERROR_INVALID_SESSION
*               NI_RETCODE_FAILURE
*******************************************************************************/
ni_retcode_t ni_uploader_session_open(ni_session_context_t *p_ctx);

/*!******************************************************************************
*  \brief  Close an xcoder upload instance
*
*  \param   p_ctx     pointer to uploader session context
*
*  \return  NI_RETCODE_SUCCESS
*******************************************************************************/
ni_retcode_t ni_uploader_session_close(ni_session_context_t *p_ctx);

/*!******************************************************************************
*  \brief  Send a YUV p_frame to upload session
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwupload_session_write(ni_session_context_t *p_ctx, ni_frame_t *p_frame,
                              niFrameSurface1_t *hwdesc);

/*!******************************************************************************
*  \brief  Retrieve a HW descriptor of uploaded frame
*
*  \param   p_ctx           pointer to uploader session context
*           hwdesc          pointer to hw descriptor
*
*  \return
*           On success
*               NI_RETCODE_SUCCESS
*           On failure
*               NI_RETCODE_INVALID_PARAM
*               NI_RETCODE_ERROR_INVALID_SESSION
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_hwupload_session_read_hwdesc(ni_session_context_t *p_ctx,
                                    niFrameSurface1_t *hwdesc);

/*!******************************************************************************
*  \brief  Retrieve a YUV p_frame from decoder
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwdownload_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame, niFrameSurface1_t* hwdesc);

/*!******************************************************************************
*  \brief  Copy a src hw frame to a dst hw frame
*
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_hwframe_clone(ni_session_context_t *p_ctx,
                              ni_frameclone_desc_t *p_frameclone_desc);

/*!*****************************************************************************
*  \brief  clear a particular xcoder instance buffer/data
*
*  \param   niFrameSurface1_t* surface - target hardware descriptor
*
*  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
*            or NI_RETCODE_ERROR_NVME_CMD_FAILED on
*            failure
******************************************************************************/
ni_retcode_t ni_clear_instance_buf(niFrameSurface1_t *surface);

/*!******************************************************************************
 *  \brief  condif a scaler instance
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_params            pointer to scaler parameters
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_scaler_params(ni_session_context_t *p_ctx,
                                                  ni_scaler_params_t *p_params);

/*!******************************************************************************
 *  \brief  allocate a frame in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  width               width in pixels
 *  \param[in]  height              height in pixels
 *  \param[in]  format              pixel format
 *  \param[in]  options             option flags
 *  \param[in]  rectangle_width     clipping rectangle width in pixels
 *  \param[in]  rectangle_height    clipping rectangle height in pixels
 *  \param[in]  rectangle_x         clipping rectangle x position
 *  \param[in]  rectangle_y         clipping rectangle y position
 *  \param[in]  rgba_color          background colour (only used by pad filter)
 *  \param[in]  frame_index         frame index (only for hardware frames)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_alloc_frame(ni_session_context_t* p_ctx, int width,
                                   int height, int format, int options,
                                   int rectangle_width,int rectangle_height,
                                   int rectangle_x, int rectangle_y,
                                   int rgba_color, int frame_index);

/*!******************************************************************************
 *  \brief  config frame in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_cfg               pointer to frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_config_frame(ni_session_context_t *p_ctx,
                                    ni_frame_config_t *p_cfg);

/*!******************************************************************************
 *  \brief  config multi frames in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_cfg_in            pointer to input frame config array
 *  \param[in]  numInCfgs           number of input frame configs in p_cfg_in array
 *  \param[in]  p_cfg_out           pointer to output frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_multi_config_frame(ni_session_context_t *p_ctx,
                                          ni_frame_config_t p_cfg_in[],
                                          int numInCfgs,
                                          ni_frame_config_t *p_cfg_out);

/*!******************************************************************************
 *  \brief  Open a xcoder scaler instance
 *
 *  \param[in]  p_ctx   pointer to session context
 *
 *  \return     NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_NVME_CMD_FAILED
 *              NI_RETCODE_ERROR_INVALID_SESSION
 *              NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
int ni_scaler_session_open(ni_session_context_t* p_ctx);

/*!******************************************************************************
 *  \brief  close a scaler session
 *
 *  \param[in]  p_ctx           pointer to session context
 *  \param[in]  eos_received    (not used)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *******************************************************************************/
ni_retcode_t ni_scaler_session_close(ni_session_context_t* p_ctx, int eos_received);

/*!******************************************************************************
*  \brief  Send a p_config command to configure uploading parameters.
*
*  \param     ni_session_context_t p_ctx - xcoder Context
*  \param[in] pool_size                    pool size to create
*  \param[in] pool                         0 = normal pool, 1 = P2P pool
*
*  \return - NI_RETCODE_SUCCESS on success,
*            NI_RETCODE_ERROR_INVALID_SESSION
*            NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_config_instance_set_uploader_params(ni_session_context_t *p_ctx,
                                                    uint32_t pool_size,
                                                    uint32_t pool);

/*!******************************************************************************
 *  \brief  read a hardware descriptor from a scaler session
 *
 *  \param[in]      p_ctx           pointer to session context
 *  \param[out]     p_frame         pointer to frame to write hw descriptor
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_scaler_session_read_hwdesc(
  ni_session_context_t *p_ctx,
  ni_frame_t *p_frame);

/*!******************************************************************************
*  \brief  Grab bitdepth factor from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         1 or 2
*******************************************************************************/
int ni_get_bitdepth_factor_from_pixfmt(int pix_fmt);

/*!******************************************************************************
*  \brief  Grab planar info from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         0 or 1 for success, -1 for error
*******************************************************************************/
int ni_get_planar_from_pixfmt(int pix_fmt);

#ifndef _WIN32
/*!*****************************************************************************
 *  \brief  Get an address offset from a hw descriptor
 *
 *  \param[in]  p_ctx     ni_session_context_t to be referenced
 *  \param[in]  hwdesc    Pointer to caller allocated niFrameSurface1_t
 *  \param[out] p_offset  Value of offset
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_get_memory_offset(ni_session_context_t * p_ctx, const niFrameSurface1_t *hwdesc,
                                  uint32_t *p_offset);

#endif

/*!*****************************************************************************
 *  \brief  Get DDR configuration of Quadra device
 *
 *  \param[in/out] p_ctx  pointer to a session context with valid file handle
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *                        NI_RETCODE_ERROR_MEM_ALOC
 *                        NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_get_ddr_configuration(ni_session_context_t *p_ctx);

/*!*****************************************************************************
 *  \brief  Set DDR configuration of Quadra device
 *
 *  \param[in] p_ctx  pointer to a session context with valid file handle
 *  \param[in] ddr_priority_mode  ddr priority mode
 * 
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *                        NI_RETCODE_ERROR_MEM_ALOC
 *                        NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_set_ddr_configuration(ni_session_context_t *p_ctx,
                                             uint8_t ddr_priority_mode);


#define NI_AI_HW_ALIGN_SIZE 64

ni_retcode_t ni_ai_session_open(ni_session_context_t *p_ctx);
ni_retcode_t ni_ai_session_close(ni_session_context_t *p_ctx, int eos_received);
ni_retcode_t ni_config_instance_network_binary(ni_session_context_t *p_ctx,
                                               void *nb_data, uint32_t nb_size);
ni_retcode_t ni_ai_query_network_ready(ni_session_context_t *p_ctx);
ni_retcode_t ni_ai_session_write(ni_session_context_t *p_ctx,
                                 ni_frame_t *p_frame);
ni_retcode_t ni_ai_session_read(ni_session_context_t *p_ctx,
                                ni_packet_t *p_packet);
ni_retcode_t ni_config_read_inout_layers(ni_session_context_t *p_ctx,
                                         ni_network_data_t *p_network);
ni_retcode_t ni_ai_alloc_hwframe(ni_session_context_t *p_ctx, int width,
                                 int height, int options, int pool_size,
                                 int frame_index);
ni_retcode_t ni_ai_alloc_dst_frame(ni_session_context_t *p_ctx,
                                   niFrameSurface1_t *p_out_surface);
ni_retcode_t ni_ai_multi_config_frame(ni_session_context_t *p_ctx,
                                          ni_frame_config_t p_cfg_in[],
                                          int numInCfgs,
                                          ni_frame_config_t *p_cfg_out);

ni_retcode_t ni_ai_session_read_hwdesc(ni_session_context_t *p_ctx,
                                       ni_frame_t *p_frame);
/*!*****************************************************************************
  *  \brief  Allocate memory for the metadata header and auxillary data for
  *          encoder input data.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] extra_len     Length header and auxillary data
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_metadata_buffer_alloc(ni_frame_t *p_frame,
                                              int extra_len);

ni_retcode_t ni_encoder_start_buffer_alloc(ni_frame_t *p_frame);

ni_retcode_t ni_ai_session_query_metrics(ni_session_context_t *p_ctx,
                                         ni_network_perf_metrics_t *p_metrics);


/*!*****************************************************************************
 *  \brief  Send namespace num / Opmode and SRIOv index/value to the device with 
 *          specified logic block address.
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] Key            Represents either namespace num or opmode
 *  \param[in] Value          Represents either SRIOv index or opmode value
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_config_ns_qos(ni_device_handle_t device_handle,
                                     uint32_t key, uint32_t value);

ni_retcode_t ni_dump_log_single_core(ni_session_context_t *p_ctx, void* p_data, uint32_t core_id, bool gen_log_file);
ni_retcode_t ni_dump_log_all_cores(ni_session_context_t *p_ctx, void* p_data, bool gen_log_file);
ni_retcode_t ni_send_to_target(niFrameSurface1_t *source, uint64_t ui64DestAddr, uint32_t ui32FrameSize);

#ifdef __cplusplus
}
#endif
