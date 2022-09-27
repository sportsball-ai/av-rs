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
 *
 *   \file          ni_device_api_priv_logan.h
 *
 *   @date          April 1, 2018
 *
 *   \brief         Private definitions used by main ni_device_api_logan file
 *
 *   @author
 *
 ******************************************************************************/

#pragma once


#ifdef __cplusplus
extern "C"
{
#endif

#include "ni_defs_logan.h"

typedef enum
{
  INST_READ_CONFIG = 0,
  INST_WRITE_CONFIG = 1,
} ni_logan_inst_config_rw_type_t;

typedef enum
{
  INST_BUF_INFO_RW_READ  = 0,
  INST_BUF_INFO_RW_WRITE = 1,
  INST_BUF_INFO_RW_UPLOAD = 2
} ni_logan_instance_buf_info_rw_type_t;

// mapping of a subset of NI_LOGAN_RETCODE_NVME_SC_* of ni_logan_retcode_t, returned by
// fw in regular i/o environment.
typedef enum _ni_logan_xcoder_mgr_retcode
{
  ni_logan_xcoder_request_success          = 0,      // NVME SC
  ni_logan_xcoder_request_pending          = 1,      // NVME SC 0x305
  ni_logan_xcoder_resource_recovery        = 0xFFFD, // NVME SC 0x3FD
  ni_logan_xcoder_resource_insufficient    = 0xFFFE, // NVME SC 0x3FE
  ni_logan_xcoder_general_error            = 0xFFFF, // NVME SC 0x3FF
} ni_logan_xcoder_mgr_retcode_t;

typedef struct _ni_logan_inst_config_rw
{
  uint8_t ui8Enable;
  uint8_t ui8HWAccess;
  union
  {
    uint16_t ui16ReadFrameId;
    uint16_t ui16WriteFrameId;
  } uHWAccessField;
} ni_logan_inst_config_rw_t;

typedef struct _ni_logan_instance_mgr_general_status
{
  uint8_t active_sub_instances_cnt; // Number of active sub-instance in that instance
  uint8_t process_load_percent;  // Processing load in percentage
  uint8_t error_count;
  uint8_t fatal_error;
  uint32_t fw_model_load;
  uint8_t cmd_queue_count;
  uint8_t fw_video_mem_usage;
  uint8_t reserved;
} ni_logan_instance_mgr_general_status_t;

typedef struct _ni_logan_instance_debugInfo{
  uint8_t ui8VpuCoreError;
  uint8_t ui8VpuInstId;
  uint8_t ui8DataPktSeqNum;
  uint8_t ui8DataPktSeqTotal;
  uint32_t ui32DataPktSize;
  uint32_t ui32VpuInstError;
  uint32_t ui32BufferAddr;
  uint32_t ui32BufferWrPt;
  uint32_t ui32BufferRdPt;
  uint32_t ui32BufferSize;
  uint32_t ui32Reserved;
} ni_logan_instance_debugInfo_t; // 32 bytes (Has to be 8 byte aligned)

typedef struct _ni_logan_instance_mgr_stream_info
{
  uint16_t picture_width;  // Picture Width
  uint16_t picture_height; // Picture Height
  uint16_t transfer_frame_stride; // Transfer Frame Stride
  uint16_t transfer_frame_height; // Transfer Frame Height(Not VPU Frame Height)
  uint16_t frame_rate; // Sequence Frame Rate
  uint16_t is_flushed; // Is decoder/encoder flushed or still has data to process
  uint16_t reserved[2];
} ni_logan_instance_mgr_stream_info_t;

typedef struct _ni_logan_instance_mgr_stream_complete
{
  uint16_t is_flushed;     // Is decoder/encoder flushed or still has data to process
  uint16_t data_bytes_available; // How much data is available to read/write
} ni_logan_instance_mgr_stream_complete_t;

typedef struct _ni_logan_get_session_id
{
  uint32_t session_id;          /*! Session ID */
} ni_logan_get_session_id_t;

typedef struct _ni_logan_encoder_session_open_info
{
  uint32_t codec_format;        /*! Device Type, Either NI_LOGAN_CODEC_FORMAT_H264 or NI_LOGAN_CODEC_FORMAT_H265 */
  int32_t i32picWidth;          /*!*< The width of a picture to be encoded in unit of sample. */
  int32_t i32picHeight;         /*!*< The height of a picture to be encoded in unit of sample. */
  uint32_t model_load;
  uint32_t EncoderReadSyncQuery; // In low latency mode, encoder read packet will just send query command one time
  uint32_t hw_desc_mode;         // hw action mode
  uint32_t set_high_priority;    //set high priory to FW
} ni_logan_encoder_session_open_info_t;

typedef struct _ni_logan_decoder_session_open_info
{
  uint32_t codec_format;        /*! Device Type, Either NI_LOGAN_CODEC_FORMAT_H264 or NI_LOGAN_CODEC_FORMAT_H265 */
  uint32_t model_load;
  uint32_t low_delay_mode;
  uint32_t hw_desc_mode;        // hw action mode
  uint32_t set_high_priority;    //set high priory to FW
  uint32_t reserved[1];
} ni_logan_decoder_session_open_info_t;

typedef struct _ni_logan_session_closed_status
{
  uint32_t session_closed;
} ni_logan_session_closed_status_t;

typedef struct _ni_logan_instance_dec_packet_info
{
  uint32_t packet_size;   // encoder packet actual size
} ni_logan_instance_dec_packet_info_t;

typedef struct _ni_logan_instance_upload_ret_hwdesc
{
  int16_t inst_id;     // hardware instance ID
  int16_t frame_index; // hardware frame index,
                       // -3: default invalid value
                       // -5: meta data index
} ni_logan_instance_upload_ret_hwdesc_t;

typedef struct _ni_logan_instance_buf_info
{
  union
  {
    uint32_t buf_avail_size;  // available size of space/data for write/read
    ni_logan_instance_upload_ret_hwdesc_t hw_inst_ind;
  };
} ni_logan_instance_buf_info_t;

typedef struct _ni_logan_encoder_frame_params
{
  uint16_t force_picture_type; //flag to force the pic type
  uint16_t data_format;   // In Env write this is usually set to NI_LOGAN_DATA_FORMAT_YUV_FRAME
  uint16_t picture_type;  // This is set to either XCODER_PIC_TYPE_I or XCODER_PIC_TYPE_CRA or XCODER_PIC_TYPE_IDR
  uint16_t video_width;
  uint16_t video_height;
  uint32_t timestamp;
} ni_logan_encoder_frame_params_t;

// 24 bytes
typedef struct _ni_logan_metadata_common
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
  uint16_t bit_depth;
} ni_logan_metadata_common_t;

// 64 bytes
typedef struct _ni_logan_metadata_dec_frame
{
  ni_logan_metadata_common_t metadata_common;
  uint32_t             sei_header;
  uint16_t             sei_number;
  uint16_t             sei_size;
  uint32_t             frame_cycle;
  uint32_t             reserved;
  ni_logan_hwframe_surface_t hwdesc;
} ni_logan_metadata_dec_frame_t;

// 64 bytes
typedef struct _ni_logan_metadata_enc_frame
{
  ni_logan_metadata_common_t  metadata_common;
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
  uint16_t              force_headers;
  uint8_t               use_cur_src_as_long_term_pic;
  uint8_t               use_long_term_ref;
  uint8_t               ui8Reserved[8]; //8
} ni_logan_metadata_enc_frame_t;

// 40 bytes
typedef struct _ni_logan_metadata_enc_bstream
{
  uint32_t      bs_frame_size;
  uint32_t      frame_type;
  uint64_t      frame_tstamp;
  uint32_t      recycle_index;
  uint32_t      frame_cycle;
  uint32_t      avg_frame_qp;
  uint8_t       end_of_packet;
  uint8_t       reserved[11];
} ni_logan_metadata_enc_bstream_t;

/*!****** encoder paramters *********************************************/

typedef enum _ni_logan_gop_preset_idx
{
  GOP_PRESET_IDX_CUSTOM = 0,
  GOP_PRESET_IDX_ALL_I = 1, /*!*< All Intra, gopsize = 1 */
  GOP_PRESET_IDX_IPP = 2,   /*!*< Consecutive P, cyclic gopsize = 1  */
  GOP_PRESET_IDX_IBBB = 3,  /*!*< Consecutive B, cyclic gopsize = 1  */
  GOP_PRESET_IDX_IBPBP = 4, /*!*< gopsize = 2  */
  GOP_PRESET_IDX_IBBBP = 5, /*!*< gopsize = 4  */
  GOP_PRESET_IDX_IPPPP = 6, /*!*< Consecutive P, cyclic gopsize = 4 */
  GOP_PRESET_IDX_IBBBB = 7, /*!*< Consecutive B, cyclic gopsize = 4 */
  GOP_PRESET_IDX_RA_IB = 8, /*!*< Random Access, cyclic gopsize = 8 */
  GOP_PRESET_IDX_SP = 9,    /*!*< Consecutive P, gopsize=1, similar to 2 but */
                            /* uses 1 instead of 2 reference frames */

  GOP_PRESET_IDX_17 = 17,   /*!*< GOP_PRESET_IDX_ALL_I, poc_type=2 */
  GOP_PRESET_IDX_18 = 18,   /*!*< GOP_PRESET_IDX_IPP, poc_type=2 */
  GOP_PRESET_IDX_19 = 19,   /*!*< GOP_PRESET_IDX_IBBB, poc_type=2 */
  GOP_PRESET_IDX_20 = 20,   /*!*< GOP_PRESET_IDX_IPPPP, poc_type=2 */
  GOP_PRESET_IDX_21 = 21,   /*!*< GOP_PRESET_IDX_IBBBB, poc_type=2 */
  GOP_PRESET_IDX_22 = 22,   /*!*< GOP_PRESET_IDX_SP, poc_type=2 */
} ni_logan_gop_preset_idx_t;

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
  STD_MAX
} ni_logan_bitstream_format_t;

typedef struct _ni_logan_t408_config_t
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
  int32_t wppEnable;                  /*!*< It enables WPP (T408-front Parallel Processing).
                                        WPP is unsupported in ring buffer mode of bitstream buffer. */
  int32_t max_num_merge;              /*!*< It specifies the number of merge candidates in RDO (1 or 2).
                                          2 of max_num_merge (default) offers better quality of encoded picture,
                                          while 1 of max_num_merge improves encoding performance.  */
  int32_t disableDeblk;               /*!*< It disables in-loop deblocking filtering. */
  int32_t lfCrossSliceBoundaryEnable; /*!*< It enables filtering across slice boundaries for in-loop deblocking. */
  int32_t betaOffsetDiv2;             /*!*< It sets BetaOffsetDiv2 for deblocking filter. */
  int32_t tcOffsetDiv2;               /*!*< It sets TcOffsetDiv3 for deblocking filter. */
  int32_t skipIntraTrans;             /*!*< It enables transform skip for an intra CU. */
  int32_t saoEnable;                  /*!*< It enables SAO (Sample Adaptive Offset). */
  int32_t intraNxNEnable;             /*!*< It enables intra NxN PUs. */
  int32_t bitAllocMode;
  int32_t fixedBitRatio[NI_LOGAN_MAX_GOP_NUM];
  int32_t enable_cu_level_rate_control; /*!*< It enable CU level rate control. */
  int32_t enable_hvs_qp; /*!*< It enable CU QP adjustment for subjective quality enhancement. */
  int32_t hvs_qp_scale; /*!*< A QP scaling factor for CU QP adjustment when enable_hvs_qp_scale is 1 */
  int32_t max_delta_qp; /*!*< A maximum delta QP for rate control */

  // CUSTOM_GOP
  ni_logan_custom_gop_params_t custom_gop_params; /*!*< <<vpuapi_h_CustomGopParam>> */
  int32_t roiEnable;           /*!*< It enables ROI map. NOTE: It is valid when rate control is on. */

  uint32_t numUnitsInTick;     /*!*< It specifies the number of time units of a clock operating at the frequency time_scale Hz.
                                 This is used to to calculate frame_rate syntax.  */
  uint32_t timeScale;          /*!*< It specifies the number of time units that pass in one second.
                                 This is used to to calculate frame_rate syntax.  */
  uint32_t numTicksPocDiffOne; /*!*< It specifies the number of clock ticks corresponding to
                                 a difference of picture order count values equal to 1.
                                 This is used to calculate frame_rate syntax. */

  int32_t chromaCbQpOffset; /*!*< The value of chroma(Cb) QP offset */
  int32_t chromaCrQpOffset; /*!*< The value of chroma(Cr) QP offset */

  int32_t initialRcQp; /*!*< The value of initial QP by HOST application. This value is meaningless if INITIAL_RC_QP is 63.*/

  uint32_t nrYEnable;  /*!*< It enables noise reduction algorithm to Y component.  */
  uint32_t nrCbEnable; /*!*< It enables noise reduction algorithm to Cb component. */
  uint32_t nrCrEnable; /*!*< It enables noise reduction algorithm to Cr component. */

  // ENC_NR_WEIGHT
  uint32_t nrIntraWeightY;  /*!*< A weight to Y noise level for intra picture (0 ~ 31).
                             nrIntraWeight/4 is multiplied to the noise level that has been estimated.
                             This weight is put for intra frame to be filtered more strongly or
                             more weakly than just with the estimated noise level. */
  uint32_t nrIntraWeightCb; /*!*< A weight to Cb noise level for intra picture (0 ~ 31) */
  uint32_t nrIntraWeightCr; /*!*< A weight to Cr noise level for intra picture (0 ~ 31) */
  uint32_t nrInterWeightY;  /*!*< A weight to Y noise level for inter picture (0 ~ 31).
                              nrInterWeight/4 is multiplied to the noise level that has been estimated.
                              This weight is put for inter frame to be filtered more strongly or
                              more weakly than just with the estimated noise level. */
  uint32_t nrInterWeightCb; /*!*< A weight to Cb noise level for inter picture (0 ~ 31) */
  uint32_t nrInterWeightCr; /*!*< A weight to Cr noise level for inter picture (0 ~ 31) */

  uint32_t nrNoiseEstEnable; /*!*< It enables noise estimation for noise reduction.
                               When this is disabled, host carries out noise estimation with nrNoiseSigmaY/Cb/Cr. */
  uint32_t nrNoiseSigmaY;    /*!*< It specifies Y noise standard deviation when nrNoiseEstEnable is 0.  */
  uint32_t nrNoiseSigmaCb;   /*!*< It specifies Cb noise standard deviation when nrNoiseEstEnable is 0. */
  uint32_t nrNoiseSigmaCr;   /*!*< It specifies Cr noise standard deviation when nrNoiseEstEnable is 0. */

  uint32_t useLongTerm; /*!*< It enables long-term reference function. */

  // newly added for T408_520
  uint32_t monochromeEnable;        /*!*< It enables monochrome encoding mode. */
  uint32_t strongIntraSmoothEnable; /*!*< It enables strong intra smoothing. */

  uint32_t weightPredEnable; /*!*< It enables to use weighted prediction.*/
  uint32_t bgDetectEnable;   /*!*< It enables background detection. */
  uint32_t bgThrDiff;        /*!*< It specifies the threshold of max difference that is used in s2me block.
                               It is valid when background detection is on. */
  uint32_t bgThrMeanDiff;    /*!*< It specifies the threshold of mean difference that is used in s2me block.
                               It is valid  when background detection is on. */
  uint32_t bgLambdaQp;       /*!*< It specifies the minimum lambda QP value to be used in the background area. */
  int32_t bgDeltaQp;         /*!*< It specifies the difference between the lambda QP value of background
                               and the lambda QP value of foreground. */

  uint32_t customLambdaEnable;  /*!*< It enables custom lambda table. */
  uint32_t customMDEnable;      /*!*< It enables custom mode decision. */
  int32_t pu04DeltaRate;            /*!*< A value which is added to the total cost of 4x4 blocks */
  int32_t pu08DeltaRate;            /*!*< A value which is added to the total cost of 8x8 blocks */
  int32_t pu16DeltaRate;            /*!*< A value which is added to the total cost of 16x16 blocks */
  int32_t pu32DeltaRate;            /*!*< A value which is added to the total cost of 32x32 blocks */
  int32_t pu04IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 4x4 Planar intra prediction mode. */
  int32_t pu04IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost (=distortion + rate)
                                      in 4x4 DC intra prediction mode. */
  int32_t pu04IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost (=distortion + rate)
                                      in 4x4 Angular intra prediction mode.  */
  int32_t pu08IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 8x8 Planar intra prediction mode.*/
  int32_t pu08IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 8x8 DC intra prediction mode.*/
  int32_t pu08IntraAngleDeltaRate;  /*!*< A value which is added to  rate when calculating cost(=distortion + rate)
                                      in 8x8 Angular intra prediction mode. */
  int32_t pu16IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 16x16 Planar intra prediction mode. */
  int32_t pu16IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 16x16 DC intra prediction mode */
  int32_t pu16IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 16x16 Angular intra prediction mode */
  int32_t pu32IntraPlanarDeltaRate; /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 32x32 Planar intra prediction mode */
  int32_t pu32IntraDcDeltaRate;     /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 32x32 DC intra prediction mode */
  int32_t pu32IntraAngleDeltaRate;  /*!*< A value which is added to rate when calculating cost(=distortion + rate)
                                      in 32x32 Angular intra prediction mode */
  int32_t cu08IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU8x8 */
  int32_t cu08InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU8x8 */
  int32_t cu08MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU8x8 */
  int32_t cu16IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU16x16 */
  int32_t cu16InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU16x16 */
  int32_t cu16MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU16x16 */
  int32_t cu32IntraDeltaRate;       /*!*< A value which is added to rate when calculating cost for intra CU32x32 */
  int32_t cu32InterDeltaRate;       /*!*< A value which is added to rate when calculating cost for inter CU32x32 */
  int32_t cu32MergeDeltaRate;       /*!*< A value which is added to rate when calculating cost for merge CU32x32 */
  int32_t coefClearDisable;         /*!*< It disables the transform coefficient clearing algorithm for P or B picture.
                                      If this is 1, all-zero coefficient block is not evaluated in RDO. */
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
  int32_t avc_slice_mode;
  int32_t avc_slice_arg; /*!*< The number of MB for a slice when avc_slice_mode is set with 1  */
  int32_t intra_mb_refresh_mode;
  int32_t intra_mb_refresh_arg;
  int32_t enable_mb_level_rc; /*!*< It enables MB-level rate control. */
  int32_t entropy_coding_mode;

    /**< It enables every I-frames to include VPS/SPS/PPS. */
    // forcedHeaderEnable = 2:Every IRAP frame includes headers(VPS, SPS, PPS)
    // forcedHeaderEnable = 1:Every IDR frame includes headers (VPS,SPS, PPS).
    // forcedHeaderEnable = 0:First IDR frame includes headers (VPS,SPS, PPS).
    uint32_t forcedHeaderEnable;
} ni_logan_t408_config_t;

typedef struct _ni_logan_encoder_config_t
{
  uint8_t ui8bitstreamFormat; /*!*< The standard type of bitstream in encoder operation.
                                It is one of STD_AVC and STD_HEVC, use enums from ni_logan_bitstream_format_t. */
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
  uint8_t ui8lowLatencyMode; /*!*< 2bits low latency mode setting. bit[1]: low latency interrupt enable,
                               bit[0]: fast bitstream-packing enable (only for T408_5) */

  ni_logan_t408_config_t niParamT408; /*!*< <<vpuapi_h_EncT408Param>> */

  /*!*< endianess of 10 bit source YUV. 0: little (default) 1: big */
  uint32_t ui32sourceEndian;

  /**> 0=no HDR in VUI, 1=add HDR info to VUI **/
  uint32_t hdrEnableVUI; //TODO: to be deprecated
  uint32_t ui32VuiDataSizeBits;       /**< size of VUI RBSP in bits **/
  uint32_t ui32VuiDataSizeBytes;     /**< size of VUI RBSP in bytes up to NI_LOGAN_MAX_VUI_SIZE **/
  uint8_t  ui8EnableAUD;    /**< Enables Access Unit Delimiter if set to 1 **/
  int8_t   ui8hwframes;     /**< Init encoder with yuv bypass mode **/
  uint8_t  ui8explicitRefListEnable;  /**< Enable explicit reference list if set to 1 **/
  uint8_t  ui8Reserved[5]; /**< reserved bytes **/
  uint32_t ui32flushGop; /**< When enabled forces IDR at the intraPeriod/avcIdrPeriod - results in Gop being flushed**/
  uint32_t ui32minIntraRefreshCycle;    /**< Sets minimum number of intra refresh cycles for intraRefresh feature**/
  uint32_t ui32fillerEnable;        /**< It enables filler data for strict rate control*/
  uint8_t  ui8VuiRbsp[NI_LOGAN_MAX_VUI_SIZE]; /**< VUI raw byte sequence **/
} ni_logan_encoder_config_t;

/*!****** hwuploader initialization paramters *********************************/
typedef struct _ni_logan_init_frames_params_t
{
  uint16_t width;
  uint16_t height;
  uint16_t bit_depth_factor; // for YUV buffer allocation
  uint16_t pool_size;     // hardware YUV buffer pool size
}ni_logan_init_frames_params_t;

/*!****** paramters of clearing hardware frames *********************************/
typedef struct _ni_logan_recycle_buffer_t
{
  int8_t i8FrameIdx;
  int8_t i8InstID;
  int8_t resv[2];
}ni_logan_recycle_buffer_t;


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
} ni_logan_set_param_option_t;

/*!******************************************************************************
 *  \brief  Get info from received p_frame
 *
 *  \param ni_logan_frame_t* p_frame - ni_logan_frame_t
 *  \param uint32_t read_length - buffer length in p_frame
 *  \param uint64_t* frame_offset - frame offset
 *  \param bool is_hw_frame - 0: software frame, 1: hardware frame
 *
 *  \return frame size
 *******************************************************************************/
int ni_logan_create_frame(ni_logan_frame_t* p_frame, uint32_t read_length, uint64_t* frame_offset, bool is_hw_frame);

/*!******************************************************************************
 *  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_logan_set_custom_template(ni_logan_session_context_t* p_ctx,
                            ni_logan_encoder_config_t* p_cfg,
                            ni_logan_encoder_params_t* p_src);

/*!******************************************************************************
 *  \brief  Setup and initialize all xcoder configuration to default (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_logan_set_default_template(ni_logan_session_context_t* p_ctx,
                             ni_logan_encoder_config_t* p_config);

/*!******************************************************************************
 *  \brief  Perform validation on custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_logan_retcode_t ni_logan_validate_custom_template(ni_logan_session_context_t* p_ctx,
                                         ni_logan_encoder_config_t* p_cfg,
                                         ni_logan_encoder_params_t* p_src,
                                         char* p_param_err,
                                         uint32_t max_err_len);

/*!******************************************************************************
 *  \brief  check the range of common parameters
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_logan_retcode_t ni_logan_check_common_params(ni_logan_t408_config_t* p_param,
                                    ni_logan_encoder_params_t* p_src,
                                    char* param_err,
                                    uint32_t max_err_len);

/*!******************************************************************************
 *  \brief  check the range of rate control parameters
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_logan_retcode_t ni_logan_check_ratecontrol_params(ni_logan_encoder_config_t* p_cfg,
                                         char* param_err,
                                         uint32_t max_err_len);

/*!******************************************************************************
 *  \brief  Print xcoder user configurations
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_params_print(ni_logan_encoder_params_t * const p_encoder_params, ni_logan_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Get info from received xcoder capability
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_populate_device_capability_struct(ni_logan_device_capability_t* p_cap, void * p_data);

/*!******************************************************************************
 *  \brief  Open a xcoder decoder instance
 *
 *  \param
 *
 *  \return
*******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_open(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Close a xcoder decoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_close(ni_logan_session_context_t *p_ctx, int eos_recieved);

/*!******************************************************************************
 *  \brief  Flush decoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_flush(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Send a video p_packet to decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_decoder_session_write(ni_logan_session_context_t *p_ctx, ni_logan_packet_t *p_packet);

/*!******************************************************************************
 *  \brief  Retrieve a YUV p_frame from decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_decoder_session_read(ni_logan_session_context_t *p_ctx, ni_logan_frame_t *p_frame);

/*!******************************************************************************
 *  \brief  Query current decoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_decoder_session_query(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Open a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_open(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Close a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_close(ni_logan_session_context_t *p_ctx, int eos_recieved);

/*!******************************************************************************
 *  \brief  Flush encoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_flush(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
 *  \brief  Send a YUV p_frame to encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_encoder_session_write(ni_logan_session_context_t *p_ctx, ni_logan_frame_t *p_frame);

/*!******************************************************************************
 *  \brief  Retrieve an encoded packet from encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_encoder_session_read(ni_logan_session_context_t *p_ctx, ni_logan_packet_t *p_packet);

/*!******************************************************************************
 *  \brief  Query current encoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_encoder_session_query(ni_logan_session_context_t *p_ctx);
//int ni_logan_encoder_session_reconfig(ni_logan_session_context_t *p_ctx, ni_logan_session_config_t *p_config,
//                                ni_logan_param_change_flags_t change_flags);

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get GeneralStatus data
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_logan_instance_mgr_general_status_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_query_general_status(ni_logan_session_context_t* p_ctx,
                                     ni_logan_device_type_t device_type,
                                     ni_logan_instance_mgr_general_status_t* p_gen_status);

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get Stream Info data
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_logan_instance_mgr_stream_info_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION, NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *  or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_query_stream_info(ni_logan_session_context_t* p_ctx,
                                  ni_logan_device_type_t device_type,
                                  ni_logan_instance_mgr_stream_info_t* p_stream_info,
                                  bool is_hw);

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get status Info data
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_logan_instance_status_info_t *out - Struct preallocated from the
 *           caller where the resulting data will be placed
 *  \param   int rc - rc returned by the last call
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *            on failure
 ******************************************************************************/
ni_logan_retcode_t ni_logan_query_status_info(ni_logan_session_context_t* p_ctx,
                                  ni_logan_device_type_t device_type,
                                  ni_logan_instance_status_info_t* p_status_info,
                                  int rc,
                                  int opcode);

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get buffer/data Info data
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_instance_buf_info_rw_type_t rw_type
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_logan_instance_buf_info_t *out - Struct preallocated from the caller
 *           where the resulting data will be placed
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_logan_retcode_t ni_logan_query_instance_buf_info(ni_logan_session_context_t* p_ctx,
                                        ni_logan_instance_buf_info_rw_type_t rw_type,
                                        ni_logan_device_type_t device_type,
                                        ni_logan_instance_buf_info_t *p_inst_buf_info,
					bool is_hw);

/*!******************************************************************************
 *  \brief  Send a p_config command for Start Of Stream
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION.
 *            NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_sos(ni_logan_session_context_t* p_ctx,
                                    ni_logan_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Send a p_config command for End Of Stream
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_eos(ni_logan_session_context_t* p_ctx,
                                    ni_logan_device_type_t device_type);

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding parameters.
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_set_encoder_params(ni_logan_session_context_t* p_ctx);

/*!******************************************************************************
 *  \brief  updates encoder parameters based on change flags structure
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_update_encoder_params(ni_logan_session_context_t* p_ctx,
                                                      ni_logan_param_change_flags_t change_flags);

/*!******************************************************************************
 *  \brief  decoder keep alive thread function triggers every 1 second
 *
 *  \param void thread args
 *
 *  \return void
 *******************************************************************************/
void *ni_logan_session_keep_alive_thread(void *arguments);

/*!******************************************************************************
 *  \brief  send a keep alive message to firmware
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_send_session_keep_alive(uint32_t session_id,
                                        ni_device_handle_t device_handle,
                                        ni_event_handle_t event_handle,
                                        void *p_data);

/*!******************************************************************************
 *  \brief  insert the 32 bits of integer value at bit position pos
 *
 *  \param int pos, int value
 *
 *  \return void
 ******************************************************************************/
void ni_logan_fix_VUI(uint8_t *vui, int pos, int value);

/*!******************************************************************************
*  \brief  Copy a xcoder decoder card info and create worker thread
*
*  \param  ni_logan_session_context_t p_ctx - source xcoder Context
*  \param  ni_logan_session_context_t p_ctx - destination xcoder Context
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          NI_LOGAN_RETCODE_INVALID_PARAM on failure
*******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_copy_internal(ni_logan_session_context_t *src_p_ctx,
                                              ni_logan_session_context_t *dst_p_ctx);

/*!******************************************************************************
*  \brief   Retrieve a hw desc p_frame from decoder
*           When yuvbypass enabled, this is used for decoder
*           to read hardware frame index, extra data and meta data
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  ni_logan_frame_t* p_frame - xcoder frame
*  \param  ni_logan_hwframe_surface_t* hwdesc - xcoder hardware descriptor
*
*  \return rx_size on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
int ni_logan_decoder_session_read_desc(ni_logan_session_context_t* p_ctx,
                                 ni_logan_frame_t* p_frame);

/*!******************************************************************************
*  \brief  Get Card Serial Number from received Nvme Indentify info
*
*  \param
*
*  \return
*******************************************************************************/
void ni_logan_populate_serial_number(ni_logan_serial_num_t* p_serial_num, void* p_data);

/*!******************************************************************************
*  \brief  Open a xcoder uploader instance
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*  \return
*******************************************************************************/
ni_logan_retcode_t ni_logan_uploader_session_open(ni_logan_session_context_t *p_ctx);

/*!******************************************************************************
*  \brief  Setup framepool for hwupload. Uses decoder framepool
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  uint32_t pool_size - buffer pool in HW
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
int ni_logan_hwupload_init_framepool(ni_logan_session_context_t* p_ctx,
                               uint32_t pool_size);

/*!******************************************************************************
*  \brief  Send a YUV to hardware, hardware will store it.
*
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  ni_logan_frame_t* p_frame - xcoder frame
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
int ni_logan_hwupload_session_write(ni_logan_session_context_t* p_ctx,
                              ni_logan_frame_t* p_frame);

/*!******************************************************************************
*  \brief  Retrieve a HW descriptor of uploaded frame
*          The HW descriptor will contain the YUV frame index,
*          which stored in HW through ni_logan_hwupload_session_write().
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  ni_logan_hwframe_surface_t* hwdesc - xcoder hardware descriptor
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
int ni_logan_hwupload_session_read_hwdesc(ni_logan_session_context_t* p_ctx,
                                    ni_logan_hwframe_surface_t* hwdesc);

/*!******************************************************************************
*  \brief  Retrieve a YUV through HW descriptor from decoder
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  ni_logan_frame_t* p_frame - xcoder frame
*  \param  ni_logan_hwframe_surface_t* hwdesc - xcoder hardware descriptor
*
*  \return rx_size on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
int ni_logan_hwdownload_session_read(ni_logan_session_context_t* p_ctx,
                               ni_logan_frame_t* p_frame,
                               ni_logan_hwframe_surface_t* hwdesc);

/*!*****************************************************************************
*  \brief  clear a particular xcoder instance buffer/data
*
*  \param   ni_logan_frame_surface1_t* surface - target hardware descriptor
*  \param   ni_device_handle_t device_handle - device handle
*  \param   ni_event_handle_t event_handle - event handle
*
*  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
*            or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on
*            failure
******************************************************************************/
ni_logan_retcode_t ni_logan_clear_instance_buf(ni_logan_hwframe_surface_t* surface,
                                   ni_device_handle_t device_handle,
                                   ni_event_handle_t event_handle);

/*!******************************************************************************
 *  \brief  Set up schedule priority. First try to run with RR mode. If fails,
 *          try to set nice value. If fails either, ignore it and run with default
 *          priority.
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_logan_change_priority(void);
#ifdef __cplusplus
}
#endif

