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

#define ENC_CONF_STRUCT_SIZE 						0x100

typedef struct _device_state
{
    int dec_eos_sent;
    int dec_eos_received;
    int enc_eos_sent;
    int enc_eos_received;
} device_state_t;

typedef struct _tx_data
{
    char fileName[FILE_NAME_LEN];
    uint32_t DataSizeLimit;

    int device_handle;
    int mode;
    ni_session_context_t *p_dec_ctx;
    ni_session_context_t *p_enc_ctx;
    ni_session_context_t *p_upl_ctx;
    ni_device_context_t *p_dec_rsrc_ctx;
    ni_device_context_t *p_enc_rsrc_ctx;
    ni_device_context_t *p_upl_rsrc_ctx;
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
    ni_device_context_t *p_dec_rsrc_ctx;
    ni_device_context_t *p_enc_rsrc_ctx;
    ni_device_context_t *p_upl_rsrc_ctx;

    int arg_width;
    int arg_height;
} rx_data_t;

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

int decoder_send_data(ni_session_context_t * p_dec_ctx,
                      ni_session_data_io_t * p_in_data, int stFlag,
                      int input_video_width, int input_video_height, int pfs,
                      unsigned int fileSize, unsigned long *sentTotal,
                      int printT, device_state_t *xState, ni_h264_sps_t *sps);

int decoder_receive_data(ni_session_context_t * p_dec_ctx,
                         ni_session_data_io_t * p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *pfr, unsigned long long *recvTotal, int printT,
                         int writeToFile, device_state_t *xState);

int encoder_send_data(
    ni_session_context_t * p_enc_ctx, ni_session_data_io_t * p_in_data,
    int stFlag, int input_video_width, int input_video_height, int pfs,
    unsigned int fileSize, unsigned long *sentSize, device_state_t *xState);

int encoder_send_data2(
    ni_session_context_t * p_enc_ctx, uint32_t dec_codec_format,
    ni_session_data_io_t * p_dec_out_data, ni_session_data_io_t * p_enc_in_data,
    int stFlag, int input_video_width, int input_video_height, int pfs,
    unsigned int fileSize, unsigned long *sentSize, device_state_t *xState);

#ifdef __cplusplus
}
#endif
