/******************************************************************************
 *
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************
 * Originally developed and contributed by Ittiam Systems Pvt. Ltd, Bangalore
*/
#ifndef _IH264D_STRUCTS_H_
#define _IH264D_STRUCTS_H_

#include "ifwh264d_vui.h"
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_device_test.h"

struct _DecStruct;

typedef struct
{
    UWORD8 u1_seq_parameter_set_id; /** id for the seq par set 0-31 */
    UWORD8 u1_is_valid;             /** is Seq Param set valid */

    UWORD16 u2_frm_wd_in_mbs; /** Frame width expressed in MB units */
    UWORD16 u2_frm_ht_in_mbs; /** Frame height expressed in MB units */

    /* Following are derived from the above two */
    UWORD16 u2_fld_ht_in_mbs; /** Field height expressed in MB units */
    UWORD16
    u2_max_mb_addr; /** Total number of macroblocks in a coded picture */
    UWORD16
    u2_total_num_of_mbs;   /** Total number of macroblocks in a coded picture */
    UWORD32 u4_fld_ht;     /** field height */
    UWORD32 u4_cwidth;     /** chroma width */
    UWORD32 u4_chr_frm_ht; /** chroma height */
    UWORD32 u4_chr_fld_ht; /** chroma field height */
    UWORD8 u1_mb_aff_flag; /** 0 - no mb_aff; 1 - uses mb_aff */

    UWORD8 u1_profile_idc; /** profile value */
    UWORD8 u1_level_idc;   /** level value */

    /* high profile related syntax elements   */
    WORD32 i4_chroma_format_idc;
    WORD32 i4_bit_depth_luma_minus8;
    WORD32 i4_bit_depth_chroma_minus8;
    WORD32 i4_qpprime_y_zero_transform_bypass_flag;
    WORD32 i4_seq_scaling_matrix_present_flag;
    UWORD8 u1_seq_scaling_list_present_flag[8];
    UWORD8 u1_use_default_scaling_matrix_flag[8];
    WORD16 i2_scalinglist4x4[6][16];
    WORD16 i2_scalinglist8x8[2][64];
    UWORD8 u1_more_than_one_slice_group_allowed_flag;
    UWORD8 u1_arbitrary_slice_order_allowed_flag;
    UWORD8 u1_redundant_slices_allowed_flag;
    UWORD8 u1_bits_in_frm_num;        /** Number of bits in frame num */
    UWORD16 u2_u4_max_pic_num_minus1; /** Maximum frame num minus 1 */
    UWORD8
    u1_pic_order_cnt_type; /** 0 - 2 indicates the method to code picture order count */
    UWORD8 u1_log2_max_pic_order_cnt_lsb_minus;
    WORD32 i4_max_pic_order_cntLsb;
    UWORD8 u1_num_ref_frames_in_pic_order_cnt_cycle;
    UWORD8 u1_delta_pic_order_always_zero_flag;
    WORD32 i4_ofst_for_non_ref_pic;
    WORD32 i4_ofst_for_top_to_bottom_field;
    WORD32 i4_ofst_for_ref_frame[MAX_NUM_REF_FRAMES_OFFSET];
    UWORD8 u1_num_ref_frames;
    UWORD8 u1_gaps_in_frame_num_value_allowed_flag;
    UWORD8 u1_frame_mbs_only_flag; /** 1 - frame only; 0 - field/frame pic */
    UWORD8 u1_direct_8x8_inference_flag;
    UWORD8 u1_vui_parameters_present_flag;
    vui_t s_vui;
} dec_seq_params_t;

typedef struct _XcodecDecStruct
{
    int sos_flag, edFlag, bytes_sent, bytes_recv;
    unsigned long total_bytes_sent;
    unsigned long long total_bytes_recieved;
    unsigned long long xcodeRecvTotal;

    ni_session_context_t dec_ctx;
    tx_data_t sdPara;
    rx_data_t rcPara;

    int input_video_width;
    int input_video_height;
    int output_video_width;
    int output_video_height;

    ni_xcoder_params_t api_param_dec;

    ni_session_data_io_t in_pkt;
    ni_session_data_io_t out_frame;

} XcodecDecStruct;

/** Aggregating structure that is globally available */
typedef struct _DecStruct
{
    /* Output format sent by the application */
    dec_seq_params_t *ps_cur_sps;

    UWORD8 u1_chroma_format;
    UWORD8 u1_pic_decode_done;
    UWORD8 u1_slice_header_done;
    WORD32 init_done;

    UWORD32 u4_ts;
    UWORD8 u1_flushfrm;

    UWORD8 u1_resetfrm;

    UWORD16 u2_pic_wd; /** Width of the picture being decoded */
    UWORD16 u2_pic_ht; /** Height of the picture being decoded */

    /* Variables required for cropping */
    UWORD16 u2_disp_width;
    UWORD16 u2_disp_height;
    UWORD16 u2_crop_offset_y;
    UWORD16 u2_crop_offset_uv;

    WORD32 i4_frametype;
    UWORD32 u4_output_present;

    WORD32 i4_pic_type;
    WORD32 i4_content_type;
    WORD32 i4_decode_header;
    WORD32 i4_header_decoded;
    UWORD32 u4_total_frames_decoded;

    UWORD32 u4_app_disp_width;
    UWORD32 u4_app_disp_height;
    WORD32 i4_error_code;

    ivd_out_bufdesc_t *ps_out_buffer;
    ivd_get_display_frame_op_t s_disp_op;

    WORD32 i4_vui_frame_rate;

    UWORD8 u1_res_changed;

    UWORD8 u1_frame_decoded_flag;
    UWORD32 u4_num_cores;
    UWORD8 u1_separate_parse;

    WORD32 i4_app_skip_mode;

    IVD_DISPLAY_FRAME_OUT_MODE_T e_frm_out_mode;

    XcodecDecStruct *xcodec_dec;

    void *(*pf_aligned_alloc)(void *pv_mem_ctxt, WORD32 alignment, WORD32 size);
    void (*pf_aligned_free)(void *pv_mem_ctxt, void *pv_buf);
    void *pv_mem_ctxt;

} dec_struct_t;

#endif /* _H264_DEC_STRUCTS_H */
