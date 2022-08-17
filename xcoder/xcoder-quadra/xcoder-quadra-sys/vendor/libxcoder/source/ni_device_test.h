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
*  \file   ni_device_test.h
*
*  \brief  Example code on how to programmatically work with NI Quadra using
*          libxcoder API
*
*******************************************************************************/

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "ni_av_codec.h"

#ifdef __cplusplus
 "C" {
#endif

#ifdef _WIN32
#define open  _open
#define close _close
#define read  _read
#define write _write
#define lseek _lseek
#endif

#if defined(LRETURN)
#undef LRETURN
#define LRETURN goto end;
#undef END
#define END                                     \
  end:
#else
#define LRETURN goto end;
#define END                                     \
  end:
#endif

#define NVME_CMD_SEM_PROTECT 			1


#define FILE_NAME_LEN 	256

#define XCODER_APP_TRANSCODE 0
#define XCODER_APP_DECODE 1
#define XCODER_APP_ENCODE 2
#define XCODER_APP_HWUP_ENCODE 3
#define XCODER_APP_FILTER 4

#define ENC_CONF_STRUCT_SIZE 						0x100

#define MAX_INPUT_FILES 	3

#define NI_TEST_RETCODE_FAILURE -1
#define NI_TEST_RETCODE_SUCCESS 0
#define NI_TEST_RETCODE_END_OF_STREAM 1
#define NI_TEST_RETCODE_EAGAIN 2
#define NI_TEST_RETCODE_NEXT_INPUT 3
#define NI_TEST_RETCODE_SEQ_CHANGE_DONE 4

#define NI_ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

typedef struct _device_state
{
    int dec_eos_sent;
    int dec_eos_received;
    int enc_eos_sent;
    int enc_eos_received;
    int enc_seq_change;
} device_state_t;

typedef struct _tx_data
{
    char fileName[MAX_INPUT_FILES][FILE_NAME_LEN];
    uint32_t DataSizeLimit;

    int device_handle;
    int mode;
    ni_session_context_t *p_dec_ctx;
    ni_session_context_t *p_enc_ctx;
    ni_session_context_t *p_upl_ctx;
    ni_session_context_t *p_sca_ctx;
    ni_session_context_t *p_crop_ctx;
    ni_session_context_t *p_pad_ctx;
    ni_session_context_t *p_ovly_ctx;
    ni_session_context_t *p_fmt_ctx;
    ni_device_context_t *p_dec_rsrc_ctx;
    ni_device_context_t *p_enc_rsrc_ctx;
    ni_device_context_t *p_upl_rsrc_ctx;
    ni_device_context_t *p_sca_rsrc_ctx;
    ni_device_context_t *p_crop_rsrc_ctx;
    ni_device_context_t *p_pad_rsrc_ctx;
    ni_device_context_t *p_ovly_rsrc_ctx;
    ni_device_context_t *p_fmt_rsrc_ctx;
    int arg_width;
    int arg_height;
} tx_data_t;

typedef struct RecvDataStruct_
{
    char fileName[FILE_NAME_LEN];
    uint32_t DataSizeLimit;

    int device_handle;
    int mode;
    ni_session_context_t *p_dec_ctx;
    ni_session_context_t *p_enc_ctx;
    ni_session_context_t *p_upl_ctx;
    ni_session_context_t *p_sca_ctx;
    ni_session_context_t *p_crop_ctx;
    ni_session_context_t *p_pad_ctx;
    ni_session_context_t *p_ovly_ctx;
    ni_session_context_t *p_fmt_ctx;
    ni_device_context_t *p_dec_rsrc_ctx;
    ni_device_context_t *p_enc_rsrc_ctx;
    ni_device_context_t *p_upl_rsrc_ctx;
    ni_device_context_t *p_sca_rsrc_ctx;
    ni_device_context_t *p_crop_rsrc_ctx;
    ni_device_context_t *p_pad_rsrc_ctx;
    ni_device_context_t *p_ovly_rsrc_ctx;
    ni_device_context_t *p_fmt_rsrc_ctx;

    int arg_width;
    int arg_height;
} rx_data_t;

typedef struct _ni_drawbox_params
{
    int box_w;
    int box_h;
    int box_x;
    int box_y;
} box_params_t;

/**
 * Sequence parameter set
 */
typedef struct _ni_h264_sps_t
{
    int width;
    int height;

    unsigned int sps_id;
    int profile_idc;
    int level_idc;
    int chroma_format_idc;
    int transform_bypass;     ///< qpprime_y_zero_transform_bypass_flag
    int log2_max_frame_num;   ///< log2_max_frame_num_minus4 + 4
    int poc_type;             ///< pic_order_cnt_type
    int log2_max_poc_lsb;     ///< log2_max_pic_order_cnt_lsb_minus4
    int delta_pic_order_always_zero_flag;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;   ///< num_ref_frames_in_pic_order_cnt_cycle
    int ref_frame_count;    ///< num_ref_frames
    int gaps_in_frame_num_allowed_flag;
    int mb_width;   ///< pic_width_in_mbs_minus1 + 1
    ///< (pic_height_in_map_units_minus1 + 1) * (2 - frame_mbs_only_flag)
    int mb_height;
    int frame_mbs_only_flag;
    int mb_aff;   ///< mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int crop;   ///< frame_cropping_flag

    unsigned int crop_left;     ///< frame_cropping_rect_left_offset
    unsigned int crop_right;    ///< frame_cropping_rect_right_offset
    unsigned int crop_top;      ///< frame_cropping_rect_top_offset
    unsigned int crop_bottom;   ///< frame_cropping_rect_bottom_offset
    int vui_parameters_present_flag;
    ni_rational_t sar;
    int video_signal_type_present_flag;
    int full_range;
    int colour_description_present_flag;
    ni_color_primaries_t color_primaries;
    ni_color_transfer_characteristic_t color_trc;
    ni_color_space_t colorspace;
    int timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    int fixed_frame_rate_flag;
    short offset_for_ref_frame[256];
    int bitstream_restriction_flag;
    int num_reorder_frames;
    unsigned int max_dec_frame_buffering;
    int scaling_matrix_present;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[6][64];
    int nal_hrd_parameters_present_flag;
    int vcl_hrd_parameters_present_flag;
    int pic_struct_present_flag;
    int time_offset_length;
    int cpb_cnt;                            ///< See H.264 E.1.2
    int initial_cpb_removal_delay_length;   ///< initial_cpb_removal_delay_length_minus1 + 1
    int cpb_removal_delay_length;   ///< cpb_removal_delay_length_minus1 + 1
    int dpb_output_delay_length;    ///< dpb_output_delay_length_minus1 + 1
    int bit_depth_luma;             ///< bit_depth_luma_minus8 + 8
    int bit_depth_chroma;           ///< bit_depth_chroma_minus8 + 8
    int residual_color_transform_flag;   ///< residual_colour_transform_flag
    int constraint_set_flags;            ///< constraint_set[0-3]_flag
    uint8_t data[4096];
    size_t data_size;
} ni_h264_sps_t;

#define HEVC_MAX_SUB_LAYERS 7
#define HEVC_MAX_SHORT_TERM_REF_PIC_SETS 64
#define HEVC_MAX_LONG_TERM_REF_PICS 32
#define HEVC_MAX_SPS_COUNT 16
#define HEVC_MAX_REFS 16
#define HEVC_MAX_LOG2_CTB_SIZE 6

typedef struct _ni_h265_window_t
{
    unsigned int left_offset;
    unsigned int right_offset;
    unsigned int top_offset;
    unsigned int bottom_offset;
} ni_h265_window_t;

typedef struct VUI
{
    ni_rational_t sar;

    int overscan_info_present_flag;
    int overscan_appropriate_flag;

    int video_signal_type_present_flag;
    int video_format;
    int video_full_range_flag;
    int colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristic;
    uint8_t matrix_coeffs;

    int chroma_loc_info_present_flag;
    int chroma_sample_loc_type_top_field;
    int chroma_sample_loc_type_bottom_field;
    int neutra_chroma_indication_flag;

    int field_seq_flag;
    int frame_field_info_present_flag;

    int default_display_window_flag;
    ni_h265_window_t def_disp_win;

    int vui_timing_info_present_flag;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;
    int vui_poc_proportional_to_timing_flag;
    int vui_num_ticks_poc_diff_one_minus1;
    int vui_hrd_parameters_present_flag;

    int bitstream_restriction_flag;
    int tiles_fixed_structure_flag;
    int motion_vectors_over_pic_boundaries_flag;
    int restricted_ref_pic_lists_flag;
    int min_spatial_segmentation_idc;
    int max_bytes_per_pic_denom;
    int max_bits_per_min_cu_denom;
    int log2_max_mv_length_horizontal;
    int log2_max_mv_length_vertical;
} VUI;

typedef struct PTLCommon
{
    uint8_t profile_space;
    uint8_t tier_flag;
    uint8_t profile_idc;
    uint8_t profile_compatibility_flag[32];
    uint8_t progressive_source_flag;
    uint8_t interlaced_source_flag;
    uint8_t non_packed_constraint_flag;
    uint8_t frame_only_constraint_flag;
    uint8_t max_12bit_constraint_flag;
    uint8_t max_10bit_constraint_flag;
    uint8_t max_8bit_constraint_flag;
    uint8_t max_422chroma_constraint_flag;
    uint8_t max_420chroma_constraint_flag;
    uint8_t max_monochrome_constraint_flag;
    uint8_t intra_constraint_flag;
    uint8_t one_picture_only_constraint_flag;
    uint8_t lower_bit_rate_constraint_flag;
    uint8_t max_14bit_constraint_flag;
    uint8_t inbld_flag;
    uint8_t level_idc;
} PTLCommon;

typedef struct PTL
{
    PTLCommon general_ptl;
    PTLCommon sub_layer_ptl[HEVC_MAX_SUB_LAYERS];

    uint8_t sub_layer_profile_present_flag[HEVC_MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[HEVC_MAX_SUB_LAYERS];
} PTL;

typedef struct ScalingList
{
    /* This is a little wasteful, since sizeID 0 only needs 8 coeffs,
     * and size ID 3 only has 2 arrays, not 6. */
    uint8_t sl[4][6][64];
    uint8_t sl_dc[2][6];
} ScalingList;

typedef struct ShortTermRPS
{
    unsigned int num_negative_pics;
    int num_delta_pocs;
    int rps_idx_num_delta_pocs;
    int32_t delta_poc[32];
    uint8_t used[32];
} ShortTermRPS;

/**
 * HEVC Sequence parameter set
 */
typedef struct _ni_h265_sps_t
{
    unsigned vps_id;
    int chroma_format_idc;
    uint8_t separate_colour_plane_flag;

    ni_h265_window_t output_window;
    ni_h265_window_t pic_conf_win;

    int bit_depth;
    int bit_depth_chroma;
    int pixel_shift;
    int pix_fmt;

    unsigned int log2_max_poc_lsb;
    int pcm_enabled_flag;

    int max_sub_layers;
    struct
    {
        int max_dec_pic_buffering;
        int num_reorder_pics;
        int max_latency_increase;
    } temporal_layer[HEVC_MAX_SUB_LAYERS];
    uint8_t temporal_id_nesting_flag;

    VUI vui;
    PTL ptl;

    uint8_t scaling_list_enable_flag;
    ScalingList scaling_list;

    unsigned int nb_st_rps;
    ShortTermRPS st_rps[HEVC_MAX_SHORT_TERM_REF_PIC_SETS];

    uint8_t amp_enabled_flag;
    uint8_t sao_enabled;

    uint8_t long_term_ref_pics_present_flag;
    uint16_t lt_ref_pic_poc_lsb_sps[HEVC_MAX_LONG_TERM_REF_PICS];
    uint8_t used_by_curr_pic_lt_sps_flag[HEVC_MAX_LONG_TERM_REF_PICS];
    uint8_t num_long_term_ref_pics_sps;

    struct
    {
        uint8_t bit_depth;
        uint8_t bit_depth_chroma;
        unsigned int log2_min_pcm_cb_size;
        unsigned int log2_max_pcm_cb_size;
        uint8_t loop_filter_disable_flag;
    } pcm;
    uint8_t sps_temporal_mvp_enabled_flag;
    uint8_t sps_strong_intra_smoothing_enable_flag;

    unsigned int log2_min_cb_size;
    unsigned int log2_diff_max_min_coding_block_size;
    unsigned int log2_min_tb_size;
    unsigned int log2_max_trafo_size;
    unsigned int log2_ctb_size;
    unsigned int log2_min_pu_size;

    int max_transform_hierarchy_depth_inter;
    int max_transform_hierarchy_depth_intra;

    int sps_range_extension_flag;
    int transform_skip_rotation_enabled_flag;
    int transform_skip_context_enabled_flag;
    int implicit_rdpcm_enabled_flag;
    int explicit_rdpcm_enabled_flag;
    int extended_precision_processing_flag;
    int intra_smoothing_disabled_flag;
    int high_precision_offsets_enabled_flag;
    int persistent_rice_adaptation_enabled_flag;
    int cabac_bypass_alignment_enabled_flag;

    ///< coded frame dimension in various units
    int width;
    int height;
    int ctb_width;
    int ctb_height;
    int ctb_size;
    int min_cb_width;
    int min_cb_height;
    int min_tb_width;
    int min_tb_height;
    int min_pu_width;
    int min_pu_height;
    int tb_mask;

    int hshift[3];
    int vshift[3];

    int qp_bd_offset;

    uint8_t data[4096];
    int data_size;
} ni_h265_sps_t;

typedef struct _ni_vp9_header_info
{
    int profile;
    uint16_t header_length;
    uint16_t width;
    uint16_t height;
    struct
    {
        uint32_t den;
        uint32_t num;
    } timebase;
    uint32_t total_frames;
} ni_vp9_header_info_t;

int decoder_send_data(ni_session_context_t * p_dec_ctx,
                      ni_session_data_io_t * p_in_data, int stFlag,
                      int input_video_width, int input_video_height, int pfs,
                      unsigned int fileSize, unsigned long *sentTotal,
                      int printT, device_state_t *xState, void *stream_info);

int decoder_receive_data(ni_session_context_t * p_dec_ctx,
                         ni_session_data_io_t * p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *pfr, unsigned long long *recvTotal, int printT,
                         int writeToFile, device_state_t *xState);

int encoder_send_data(
    ni_session_context_t * p_enc_ctx, ni_session_data_io_t * p_in_data,
    int stFlag, int input_video_width, int input_video_height, int pfs,
    unsigned int fileSize, unsigned long *sentSize, device_state_t *xState,
    int bit_depth, int is_last_input);

int encoder_send_data2(
    ni_session_context_t * p_enc_ctx, uint32_t dec_codec_format,
    ni_session_data_io_t * p_dec_out_data, ni_session_data_io_t * p_enc_in_data,
    int stFlag, int input_video_width, int input_video_height, int pfs,
    unsigned int fileSize, unsigned long *sentSize, device_state_t *xState);

int encoder_close_session(ni_session_context_t *p_enc_ctx,
    ni_session_data_io_t *p_in_data,
    ni_session_data_io_t *p_out_data);

int encoder_init_session(ni_session_context_t *p_enc_ctx,
    ni_session_data_io_t *p_in_data,    
    ni_session_data_io_t *p_out_data,
    int arg_width,
    int arg_height,
    int bit_depth);

int encoder_sequence_change(ni_session_context_t *p_enc_ctx,
    ni_session_data_io_t *p_in_data,    
    ni_session_data_io_t *p_out_data,
    int width,
    int height,
    int bit_depth_factor);

int scale_filter(ni_session_context_t * p_ctx, ni_frame_t * p_frame_in,
                 ni_session_data_io_t * p_data_out, int scale_width,
                 int scale_height, int out_format);

int drawbox_filter(
    ni_session_context_t * p_crop_ctx, ni_session_context_t * p_pad_ctx,
    ni_session_context_t * p_overlay_ctx, ni_session_context_t * p_fmt_ctx,
    ni_frame_t * p_frame_in, ni_session_data_io_t * p_data_out,
    box_params_t * p_box_params, int output_format);

#ifdef __cplusplus
}
#endif
