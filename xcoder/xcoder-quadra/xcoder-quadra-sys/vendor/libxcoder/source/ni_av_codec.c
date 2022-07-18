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
*  \file   ni_av_codec.c
*
*  \brief  NETINT audio/video related utility functions
*
*******************************************************************************/
#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#pragma comment(lib, "wsock32.lib")
#elif __linux__
#include <arpa/inet.h>
#endif
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "ni_util.h"
#include "ni_nvme.h"
#include "ni_bitstream.h"
#include "ni_av_codec.h"

typedef enum
{
    SLICE_TYPE_B = 0,
    SLICE_TYPE_P = 1,
    SLICE_TYPE_I = 2,
    SLICE_TYPE_MP = 3
} slice_type_t;

typedef enum
{
    GOP_PRESET_CUSTOM = 0,
    GOP_PRESET_I_1 = 1,
    GOP_PRESET_P_1 = 2,
    GOP_PRESET_B_1 = 3,
    GOP_PRESET_BP_2 = 4,
    GOP_PRESET_BBBP_3 = 5,
    GOP_PRESET_LP_4 = 6,
    GOP_PRESET_LD_4 = 7,
    GOP_PRESET_RA_8 = 8,
    // single_ref
    GOP_PRESET_SP_1 = 9,
    GOP_PRESET_BSP_2 = 10,
    GOP_PRESET_BBBSP_3 = 11,
    GOP_PRESET_LSP_4 = 12,

    // newly added
    GOP_PRESET_BBP_3 = 13,
    GOP_PRESET_BBSP_3 = 14,
    GOP_PRESET_BBBBBBBP_8 = 15,
    GOP_PRESET_BBBBBBBSP_8 = 16,
    NUM_GOP_PRESET_NUM = 17,
} gop_preset_t;

static const int32_t GOP_SIZE[NUM_GOP_PRESET_NUM] = {0, 1, 1, 1, 2, 4, 4,
                                                     4, 8, 1, 2, 4, 4};

static const int32_t LT_GOP_PRESET_I_1[6] = {SLICE_TYPE_I, 1, 0, 0, 0, 0};
static const int32_t LT_GOP_PRESET_P_1[6] = {SLICE_TYPE_MP, 1, 1, 0, 0, -1};
static const int32_t LT_GOP_PRESET_B_1[6] = {SLICE_TYPE_B, 1, 1, 0, 0, -1};
// gop_size = 2
static const int32_t LT_GOP_PRESET_BP_2[12] = {
    SLICE_TYPE_MP, 2, 1, 0, 0, -2, SLICE_TYPE_B, 1, 3, 0, 0, 2,
};
// gop_size = 4
static const int32_t LT_GOP_PRESET_BBBP_4[24] = {
    SLICE_TYPE_MP, 4, 1, 0, 0, -4, SLICE_TYPE_B, 2, 3, 0, 0, 4,
    SLICE_TYPE_B,  1, 5, 0, 0, 2,  SLICE_TYPE_B, 3, 5, 0, 2, 4,
};

static const int32_t LT_GOP_PRESET_LP_4[24] = {
    SLICE_TYPE_MP, 1, 5, 0, 0, -4, SLICE_TYPE_MP, 2, 3, 0, 1, 0,
    SLICE_TYPE_MP, 3, 5, 0, 2, 0,  SLICE_TYPE_MP, 4, 1, 0, 3, 0,
};
static const int32_t LT_GOP_PRESET_LD_4[24] = {
    SLICE_TYPE_B, 1, 5, 0, 0, -4, SLICE_TYPE_B, 2, 3, 0, 1, 0,
    SLICE_TYPE_B, 3, 5, 0, 2, 0,  SLICE_TYPE_B, 4, 1, 0, 3, 0,
};

// gop_size = 8
static const int32_t LT_GOP_PRESET_RA_8[48] = {
    SLICE_TYPE_B, 8, 1, 0, 0, -8, SLICE_TYPE_B, 4, 3, 0, 0, 8,
    SLICE_TYPE_B, 2, 5, 0, 0, 4,  SLICE_TYPE_B, 1, 8, 0, 0, 2,
    SLICE_TYPE_B, 3, 8, 0, 2, 4,  SLICE_TYPE_B, 6, 5, 0, 4, 8,
    SLICE_TYPE_B, 5, 8, 0, 4, 6,  SLICE_TYPE_B, 7, 8, 0, 6, 8,
};
// single-ref-P
static const int32_t LT_GOP_PRESET_SP_1[6] = {SLICE_TYPE_P, 1, 1, 0, 0, -1};

static const int32_t LT_GOP_PRESET_BSP_2[12] = {
    SLICE_TYPE_P, 2, 1, 0, 0, -2, SLICE_TYPE_B, 1, 3, 0, 0, 2,
};
static const int32_t LT_GOP_PRESET_BBBSP_4[24] = {
    SLICE_TYPE_P, 4, 1, 0, 0, -4, SLICE_TYPE_B, 2, 3, 0, 0, 4,
    SLICE_TYPE_B, 1, 5, 0, 0, 2,  SLICE_TYPE_B, 3, 5, 0, 2, 4,
};
static const int32_t LT_GOP_PRESET_LSP_4[24] = {
    SLICE_TYPE_P, 1, 5, 0, 0, -4, SLICE_TYPE_P, 2, 3, 0, 1, 0,
    SLICE_TYPE_P, 3, 5, 0, 2, 0,  SLICE_TYPE_P, 4, 1, 0, 3, 0,
};

static const int32_t LT_GOP_PRESET_BBP_3[18] = {
    SLICE_TYPE_MP, 3, 1, 0, 0, -3, SLICE_TYPE_B, 1, 3, 0, 0, 3,
    SLICE_TYPE_B,  2, 6, 0, 1, 3,
};

static const int32_t LT_GOP_PRESET_BBSP_3[18] = {
    SLICE_TYPE_P, 3, 1, 0, 0, 0, SLICE_TYPE_B, 1, 3, 0, 0, 3,
    SLICE_TYPE_B, 2, 6, 0, 1, 3,
};

static const int32_t LT_GOP_PRESET_BBBBBBBP_8[48] = {
    SLICE_TYPE_MP, 8, 1, 0, 0, -8, SLICE_TYPE_B, 4, 3, 0, 0, 8,
    SLICE_TYPE_B,  2, 5, 0, 0, 4,  SLICE_TYPE_B, 1, 8, 0, 0, 2,
    SLICE_TYPE_B,  3, 8, 0, 2, 4,  SLICE_TYPE_B, 6, 5, 0, 4, 8,
    SLICE_TYPE_B,  5, 8, 0, 4, 6,  SLICE_TYPE_B, 7, 8, 0, 6, 8,
};

static const int32_t LT_GOP_PRESET_BBBBBBBSP_8[48] = {
    SLICE_TYPE_P, 8, 1, 0, 0, 0, SLICE_TYPE_B, 4, 3, 0, 0, 8,
    SLICE_TYPE_B, 2, 5, 0, 0, 4, SLICE_TYPE_B, 1, 8, 0, 0, 2,
    SLICE_TYPE_B, 3, 8, 0, 2, 4, SLICE_TYPE_B, 6, 5, 0, 4, 8,
    SLICE_TYPE_B, 5, 8, 0, 4, 6, SLICE_TYPE_B, 7, 8, 0, 6, 8,
};

static const int32_t *GOP_PRESET[NUM_GOP_PRESET_NUM] = {
    NULL,
    LT_GOP_PRESET_I_1,
    LT_GOP_PRESET_P_1,
    LT_GOP_PRESET_B_1,
    LT_GOP_PRESET_BP_2,
    LT_GOP_PRESET_BBBP_4,
    LT_GOP_PRESET_LP_4,
    LT_GOP_PRESET_LD_4,
    LT_GOP_PRESET_RA_8,

    LT_GOP_PRESET_SP_1,
    LT_GOP_PRESET_BSP_2,
    LT_GOP_PRESET_BBBSP_4,
    LT_GOP_PRESET_LSP_4,

    LT_GOP_PRESET_BBP_3,
    LT_GOP_PRESET_BBSP_3,
    LT_GOP_PRESET_BBBBBBBP_8,
    LT_GOP_PRESET_BBBBBBBSP_8,
};

#define BR_SHIFT 6
#define CPB_SHIFT 4

#define SAMPLE_SPS_MAX_SUB_LAYERS_MINUS1 0
#define MAX_VPS_MAX_SUB_LAYERS 16
#define MAX_CPB_COUNT 16
#define MAX_DURATION 0.5

/*!*****************************************************************************
 *  \brief  Whether SEI (HDR) should be sent together with this frame to encoder
 *
 *  \param[in]  p_enc_ctx encoder session context
 *  \param[in]  pic_type frame type
 *  \param[in]  p_param encoder parameters
 *
 *  \return 1 if yes, 0 otherwise
 ******************************************************************************/
int ni_should_send_sei_with_frame(ni_session_context_t *p_enc_ctx,
                                  ni_pic_type_t pic_type,
                                  ni_xcoder_params_t *p_param)
{
    // repeatHeaders = 0. send on first frame only (IDR)
    // repeatHeaders = 1. send on every I-frame including I-frames generated
    // on the intraPeriod interval as well as I-frames that are forced.
    if (0 == p_enc_ctx->frame_num || PIC_TYPE_IDR == pic_type ||
        (p_param->cfg_enc_params.forced_header_enable &&
         p_param->cfg_enc_params.intra_period &&
         0 == (p_enc_ctx->frame_num % p_param->cfg_enc_params.intra_period)))
    {
        return 1;
    }
    return 0;
}

// create a ni_rational_t
static ni_rational_t ni_make_rational(int num, int den)
{
    ni_rational_t r = {num, den};
    return r;
}

/*!*****************************************************************************
 *  \brief  Retrieve auxiliary data (close caption, various SEI) associated with
 *          this frame that is returned by decoder, convert them to appropriate
 *          format and save them in the frame's auxiliary data storage for
 *          future use by encoding. Usually they would be sent together with
 *          this frame to encoder at encoding.
 *
 *  \param[in/out]  frame that is returned by decoder
 *
 *  \return NONE
 ******************************************************************************/
void ni_dec_retrieve_aux_data(ni_frame_t *frame)
{
    uint8_t *sei_buf = NULL;
    ni_aux_data_t *aux_data = NULL;
    int start_offset = 0;
    if (frame->data_len[3] != 0)
    {
        //HW frame
        start_offset = 3;
    }
    // User Data Unregistered SEI if available
    if (frame->sei_user_data_unreg_len && frame->sei_user_data_unreg_offset)
    {
        sei_buf = (uint8_t *)frame->p_data[start_offset] +
            frame->sei_user_data_unreg_offset;
        if (!ni_frame_new_aux_data_from_raw_data(
                frame, NI_FRAME_AUX_DATA_UDU_SEI, sei_buf,
                (int)frame->sei_user_data_unreg_len))
        {
            ni_log(NI_LOG_ERROR, "ni_dec_retrieve_aux_data error retrieve User data "
                           "unregisted SEI!\n");
        }
    }

    // close caption data if available
    if (frame->sei_cc_len && frame->sei_cc_offset)
    {
        sei_buf = (uint8_t *)frame->p_data[start_offset] + frame->sei_cc_offset;
        if (!ni_frame_new_aux_data_from_raw_data(
                frame, NI_FRAME_AUX_DATA_A53_CC, sei_buf,
                (int)frame->sei_cc_len))
        {
            ni_log(NI_LOG_ERROR, "ni_dec_retrieve_aux_data error retrieve close "
                           "caption SEI!\n");
        }
    }

    // hdr10 sei data if available

    // mastering display metadata
    if (frame->sei_hdr_mastering_display_color_vol_len &&
        frame->sei_hdr_mastering_display_color_vol_offset)
    {
        aux_data = ni_frame_new_aux_data(
            frame, NI_FRAME_AUX_DATA_MASTERING_DISPLAY_METADATA,
            sizeof(ni_mastering_display_metadata_t));

        if (!aux_data)
        {
            ni_log(NI_LOG_ERROR, "ni_dec_retrieve_aux_data error retrieve HDR10 "
                           "mastering display color SEI!\n");
        } else
        {
            ni_mastering_display_metadata_t *mdm =
                (ni_mastering_display_metadata_t *)aux_data->data;
            const int chroma_den = 50000;
            const int luma_den = 10000;
            ni_dec_mastering_display_colour_volume_bytes_t *pColourVolume =
                (ni_dec_mastering_display_colour_volume_bytes_t
                     *)((uint8_t *)frame->p_data[start_offset] +
                        frame->sei_hdr_mastering_display_color_vol_offset);

            // HEVC uses a g,b,r ordering, which we convert to a more natural r,
            // g,b,this is so we are compatible with FFmpeg default soft decoder
            mdm->display_primaries[0][0].num =
                ntohs(pColourVolume->display_primaries[2][0]);
            mdm->display_primaries[0][0].den = chroma_den;
            mdm->display_primaries[0][1].num =
                ntohs(pColourVolume->display_primaries[2][1]);
            mdm->display_primaries[0][1].den = chroma_den;
            mdm->display_primaries[1][0].num =
                ntohs(pColourVolume->display_primaries[0][0]);
            mdm->display_primaries[1][0].den = chroma_den;
            mdm->display_primaries[1][1].num =
                ntohs(pColourVolume->display_primaries[0][1]);
            mdm->display_primaries[1][1].den = chroma_den;
            mdm->display_primaries[2][0].num =
                ntohs(pColourVolume->display_primaries[1][0]);
            mdm->display_primaries[2][0].den = chroma_den;
            mdm->display_primaries[2][1].num =
                ntohs(pColourVolume->display_primaries[1][1]);
            mdm->display_primaries[2][1].den = chroma_den;
            mdm->white_point[0].num = ntohs(pColourVolume->white_point_x);
            mdm->white_point[0].den = chroma_den;
            mdm->white_point[1].num = ntohs(pColourVolume->white_point_y);
            mdm->white_point[1].den = chroma_den;

            mdm->min_luminance.num =
                ntohl(pColourVolume->min_display_mastering_luminance);
            mdm->min_luminance.den = luma_den;
            mdm->max_luminance.num =
                ntohl(pColourVolume->max_display_mastering_luminance);
            mdm->max_luminance.den = luma_den;

            mdm->has_luminance = mdm->has_primaries = 1;
        }
    }

    // hdr10 content light level
    if (frame->sei_hdr_content_light_level_info_len &&
        frame->sei_hdr_content_light_level_info_offset)
    {
        aux_data =
            ni_frame_new_aux_data(frame, NI_FRAME_AUX_DATA_CONTENT_LIGHT_LEVEL,
                                  sizeof(ni_content_light_level_t));

        if (!aux_data)
        {
            ni_log(NI_LOG_ERROR, "ni_dec_retrieve_aux_data error retrieve HDR10 "
                           "content light level SEI !\n");
        } else
        {
            ni_content_light_level_t *clm =
                (ni_content_light_level_t *)aux_data->data;
            ni_content_light_level_info_bytes_t *pLightLevel =
                (ni_content_light_level_info_bytes_t
                     *)((uint8_t *)frame->p_data[start_offset] +
                        frame->sei_hdr_content_light_level_info_offset);

            clm->max_cll = ntohs(pLightLevel->max_content_light_level);
            clm->max_fall = ntohs(pLightLevel->max_pic_average_light_level);
        }
    }

    // hdr10+ sei data if available
    if (frame->sei_hdr_plus_len && frame->sei_hdr_plus_offset)
    {
        aux_data = ni_frame_new_aux_data(frame, NI_FRAME_AUX_DATA_HDR_PLUS,
                                         sizeof(ni_dynamic_hdr_plus_t));

        if (!aux_data)
        {
            ni_log(NI_LOG_ERROR, "ni_dec_retrieve_aux_data error retrieve HDR10+ SEI "
                           "!\n");
        } else
        {
            int w, i, j, i_limit, j_limit;
            ni_dynamic_hdr_plus_t *hdrp =
                (ni_dynamic_hdr_plus_t *)aux_data->data;
            ni_bitstream_reader_t br;

            sei_buf = (uint8_t *)frame->p_data[start_offset] +
                frame->sei_hdr_plus_offset;
            ni_bitstream_reader_init(&br, sei_buf,
                                     8 * (int)frame->sei_hdr_plus_len);
            hdrp->itu_t_t35_country_code = 0xB5;
            hdrp->application_version = 0;
            // first 7 bytes of t35 SEI data header already matched HDR10+, and:
            ni_bs_reader_skip_bits(&br, 7 * 8);

            // Num_windows u(2)
            hdrp->num_windows = ni_bs_reader_get_bits(&br, 2);
            ni_log(NI_LOG_DEBUG, "hdr10+ num_windows %u\n", hdrp->num_windows);
            if (!(1 == hdrp->num_windows || 2 == hdrp->num_windows ||
                  3 == hdrp->num_windows))
            {
                // wrong format and skip this HDR10+ SEI
            } else
            {
                // the following block will be skipped for hdrp->num_windows ==
                // 1
                for (w = 1; w < hdrp->num_windows; w++)
                {
                    hdrp->params[w - 1].window_upper_left_corner_x =
                        ni_make_q(ni_bs_reader_get_bits(&br, 16), 1);
                    hdrp->params[w - 1].window_upper_left_corner_y =
                        ni_make_q(ni_bs_reader_get_bits(&br, 16), 1);
                    hdrp->params[w - 1].window_lower_right_corner_x =
                        ni_make_q(ni_bs_reader_get_bits(&br, 16), 1);
                    hdrp->params[w - 1].window_lower_right_corner_y =
                        ni_make_q(ni_bs_reader_get_bits(&br, 16), 1);
                    hdrp->params[w - 1].center_of_ellipse_x =
                        ni_bs_reader_get_bits(&br, 16);
                    hdrp->params[w - 1].center_of_ellipse_y =
                        ni_bs_reader_get_bits(&br, 16);
                    hdrp->params[w - 1].rotation_angle =
                        ni_bs_reader_get_bits(&br, 8);
                    hdrp->params[w - 1].semimajor_axis_internal_ellipse =
                        ni_bs_reader_get_bits(&br, 16);
                    hdrp->params[w - 1].semimajor_axis_external_ellipse =
                        ni_bs_reader_get_bits(&br, 16);
                    hdrp->params[w - 1].semiminor_axis_external_ellipse =
                        ni_bs_reader_get_bits(&br, 16);
                    hdrp->params[w - 1].overlap_process_option =
                        (ni_hdr_plus_overlap_process_option_t)
                            ni_bs_reader_get_bits(&br, 1);
                }

                // values are scaled down according to standard spec
                hdrp->targeted_system_display_maximum_luminance.num =
                    ni_bs_reader_get_bits(&br, 27);
                hdrp->targeted_system_display_maximum_luminance.den = 10000;

                hdrp->targeted_system_display_actual_peak_luminance_flag =
                    ni_bs_reader_get_bits(&br, 1);

                ni_log(NI_LOG_DEBUG, 
                    "hdr10+ targeted_system_display_maximum_luminance "
                    "%d\n",
                    hdrp->targeted_system_display_maximum_luminance.num);
                ni_log(NI_LOG_DEBUG, 
                    "hdr10+ targeted_system_display_actual_peak_lumi"
                    "nance_flag %u\n",
                    hdrp->targeted_system_display_actual_peak_luminance_flag);

                if (hdrp->targeted_system_display_actual_peak_luminance_flag)
                {
                    i_limit =
                        hdrp->num_rows_targeted_system_display_actual_peak_luminance =
                            ni_bs_reader_get_bits(&br, 5);

                    j_limit =
                        hdrp->num_cols_targeted_system_display_actual_peak_luminance =
                            ni_bs_reader_get_bits(&br, 5);

                    ni_log(NI_LOG_DEBUG, 
                        "hdr10+ num_rows_targeted_system_display_actual"
                        "_peak_luminance x "
                        "num_cols_targeted_system_display_actual_"
                        "peak_luminance %d x %d\n",
                        i_limit, j_limit);

                    i_limit = i_limit > 25 ? 25 : i_limit;
                    j_limit = j_limit > 25 ? 25 : j_limit;
                    for (i = 0; i < i_limit; i++)
                        for (j = 0; j < j_limit; j++)
                        {
                            hdrp->targeted_system_display_actual_peak_luminance
                                [i][j]
                                    .num = ni_bs_reader_get_bits(&br, 4);
                            hdrp->targeted_system_display_actual_peak_luminance
                                [i][j]
                                    .den = 15;
                            ni_log(NI_LOG_DEBUG, 
                                "hdr10+ targeted_system_display_actual_peak"
                                "_luminance[%d][%d] %d\n",
                                i, j,
                                hdrp->targeted_system_display_actual_peak_luminance
                                    [i][j]
                                        .num);
                        }
                }

                for (w = 0; w < hdrp->num_windows; w++)
                {
                    for (i = 0; i < 3; i++)
                    {
                        hdrp->params[w].maxscl[i].num =
                            ni_bs_reader_get_bits(&br, 17);
                        hdrp->params[w].maxscl[i].den = 100000;
                        ni_log(NI_LOG_DEBUG, "hdr10+ maxscl[%d][%d] %d\n", w, i,
                                       hdrp->params[w].maxscl[i].num);
                    }
                    hdrp->params[w].average_maxrgb.num =
                        ni_bs_reader_get_bits(&br, 17);
                    hdrp->params[w].average_maxrgb.den = 100000;
                    ni_log(NI_LOG_DEBUG, "hdr10+ average_maxrgb[%d] %d\n", w,
                                   hdrp->params[w].average_maxrgb.num);

                    i_limit =
                        hdrp->params[w].num_distribution_maxrgb_percentiles =
                            ni_bs_reader_get_bits(&br, 4);
                    ni_log(NI_LOG_DEBUG, 
                        "hdr10+ num_distribution_maxrgb_percentiles[%d] %d\n",
                        w, hdrp->params[w].num_distribution_maxrgb_percentiles);

                    i_limit = i_limit > 15 ? 15 : i_limit;
                    for (i = 0; i < i_limit; i++)
                    {
                        hdrp->params[w].distribution_maxrgb[i].percentage =
                            ni_bs_reader_get_bits(&br, 7);
                        hdrp->params[w].distribution_maxrgb[i].percentile.num =
                            ni_bs_reader_get_bits(&br, 17);
                        hdrp->params[w].distribution_maxrgb[i].percentile.den =
                            100000;
                        ni_log(NI_LOG_DEBUG, 
                            "hdr10+ distribution_maxrgb_percentage[%d][%d] "
                            "%u\n",
                            w, i,
                            hdrp->params[w].distribution_maxrgb[i].percentage);
                        ni_log(NI_LOG_DEBUG, 
                            "hdr10+ distribution_maxrgb_percentile[%d][%d] "
                            "%d\n",
                            w, i,
                            hdrp->params[w]
                                .distribution_maxrgb[i]
                                .percentile.num);
                    }

                    hdrp->params[w].fraction_bright_pixels.num =
                        ni_bs_reader_get_bits(&br, 10);
                    hdrp->params[w].fraction_bright_pixels.den = 1000;
                    ni_log(NI_LOG_DEBUG, "hdr10+ fraction_bright_pixels[%d] %d\n", w,
                                   hdrp->params[w].fraction_bright_pixels.num);
                }

                hdrp->mastering_display_actual_peak_luminance_flag =
                    ni_bs_reader_get_bits(&br, 1);
                ni_log(NI_LOG_DEBUG, 
                    "hdr10+ mastering_display_actual_peak_luminance_flag %u\n",
                    hdrp->mastering_display_actual_peak_luminance_flag);
                if (hdrp->mastering_display_actual_peak_luminance_flag)
                {
                    i_limit =
                        hdrp->num_rows_mastering_display_actual_peak_luminance =
                            ni_bs_reader_get_bits(&br, 5);
                    j_limit =
                        hdrp->num_cols_mastering_display_actual_peak_luminance =
                            ni_bs_reader_get_bits(&br, 5);
                    ni_log(NI_LOG_DEBUG, 
                        "hdr10+ num_rows_mastering_display_actual_peak_"
                        "luminance x "
                        "num_cols_mastering_display_actual_peak_luminance "
                        "%d x %d\n",
                        i_limit, j_limit);

                    i_limit = i_limit > 25 ? 25 : i_limit;
                    j_limit = j_limit > 25 ? 25 : j_limit;
                    for (i = 0; i < i_limit; i++)
                        for (j = 0; j < j_limit; j++)
                        {
                            hdrp->mastering_display_actual_peak_luminance[i][j]
                                .num = ni_bs_reader_get_bits(&br, 4);
                            hdrp->mastering_display_actual_peak_luminance[i][j]
                                .den = 15;
                            ni_log(NI_LOG_DEBUG, 
                                "hdr10+ mastering_display_actual_peak_lumi"
                                "nance[%d][%d] %d\n",
                                i, j,
                                hdrp
                                    ->mastering_display_actual_peak_luminance[i]
                                                                             [j]
                                    .num);
                        }
                }

                for (w = 0; w < hdrp->num_windows; w++)
                {
                    hdrp->params[w].tone_mapping_flag =
                        ni_bs_reader_get_bits(&br, 1);
                    ni_log(NI_LOG_DEBUG, "hdr10+ tone_mapping_flag[%d] %u\n", w,
                                   hdrp->params[w].tone_mapping_flag);

                    if (hdrp->params[w].tone_mapping_flag)
                    {
                        hdrp->params[w].knee_point_x.num =
                            ni_bs_reader_get_bits(&br, 12);
                        hdrp->params[w].knee_point_x.den = 4095;
                        hdrp->params[w].knee_point_y.num =
                            ni_bs_reader_get_bits(&br, 12);
                        hdrp->params[w].knee_point_y.den = 4095;
                        ni_log(NI_LOG_DEBUG, "hdr10+ knee_point_x[%d] %d\n", w,
                                       hdrp->params[w].knee_point_x.num);
                        ni_log(NI_LOG_DEBUG, "hdr10+ knee_point_y[%d] %d\n", w,
                                       hdrp->params[w].knee_point_y.num);

                        hdrp->params[w].num_bezier_curve_anchors =
                            ni_bs_reader_get_bits(&br, 4);
                        ni_log(NI_LOG_DEBUG, 
                            "hdr10+ num_bezier_curve_anchors[%d] %u\n", w,
                            hdrp->params[w].num_bezier_curve_anchors);
                        for (i = 0;
                             i < hdrp->params[w].num_bezier_curve_anchors; i++)
                        {
                            hdrp->params[w].bezier_curve_anchors[i].num =
                                ni_bs_reader_get_bits(&br, 10);
                            hdrp->params[w].bezier_curve_anchors[i].den = 1023;
                            ni_log(NI_LOG_DEBUG, 
                                "hdr10+ bezier_curve_anchors[%d][%d] %d\n", w,
                                i, hdrp->params[w].bezier_curve_anchors[i].num);
                        }
                    }

                    hdrp->params[w].color_saturation_mapping_flag =
                        ni_bs_reader_get_bits(&br, 1);
                    ni_log(NI_LOG_DEBUG, 
                        "hdr10+ color_saturation_mapping_flag[%d] %u\n", w,
                        hdrp->params[w].color_saturation_mapping_flag);
                    if (hdrp->params[w].color_saturation_mapping_flag)
                    {
                        hdrp->params[w].color_saturation_weight.num =
                            ni_bs_reader_get_bits(&br, 6);
                        hdrp->params[w].color_saturation_weight.den = 8;
                        ni_log(NI_LOG_DEBUG, 
                            "hdr10+ color_saturation_weight[%d] %d\n", w,
                            hdrp->params[w].color_saturation_weight.num);
                    }
                }   // num_windows

            }   // right number of windows
        }       // alloc memory
    }           // HDR10+ SEI

    // init source stream info to default values (unspecified)
    frame->color_primaries = NI_COL_PRI_UNSPECIFIED;
    frame->color_trc = NI_COL_TRC_UNSPECIFIED;
    frame->color_space = NI_COL_SPC_UNSPECIFIED;
    frame->video_full_range_flag = 0;
    frame->aspect_ratio_idc = 0;
    frame->sar_width = 0;
    frame->sar_height = 0;
    frame->vui_num_units_in_tick = 0;
    frame->vui_time_scale = 0;

    // VUI if retrieved
    if (frame->vui_offset || frame->vui_len)
    {
        sei_buf = (uint8_t *)frame->p_data[start_offset] + frame->vui_offset;

        if (NI_CODEC_FORMAT_H265 == frame->src_codec)
        {
            if (sizeof(ni_dec_h265_vui_param_t) == frame->vui_len)
            {
                ni_dec_h265_vui_param_t *vui =
                    (ni_dec_h265_vui_param_t *)sei_buf;
                if (vui->colour_description_present_flag)
                {
                    frame->color_primaries = vui->colour_primaries;
                    frame->color_trc = vui->transfer_characteristics;
                    frame->color_space = vui->matrix_coefficients;
                }
                frame->video_full_range_flag = vui->video_full_range_flag;

                if (vui->aspect_ratio_info_present_flag)
                {
                    frame->aspect_ratio_idc = vui->aspect_ratio_idc;
                    if (255 == frame->aspect_ratio_idc)
                    {
                        frame->sar_width = vui->sar_width;
                        frame->sar_height = vui->sar_height;
                    }
                }

                if (vui->vui_timing_info_present_flag)
                {
                    frame->vui_num_units_in_tick = vui->vui_num_units_in_tick;
                    frame->vui_time_scale = vui->vui_time_scale;
                }

                ni_log(NI_LOG_DEBUG, 
                    "ni_dec_retrieve_aux_data H.265 VUI "
                    "aspect_ratio_info_present_flag %u aspect_ratio_idc %u "
                    "sar_width %u sar_height %u "
                    "video_signal_type_present_flag %u video_format %d "
                    "video_full_range_flag %d colour_description_present_flag "
                    "%u "
                    "color-pri %u color-trc %u color-space %u "
                    "vui_timing_info_present_flag %u vui_num_units_in_tick %u "
                    "vui_time_scale %u\n",
                    vui->aspect_ratio_info_present_flag,
                    frame->aspect_ratio_idc, frame->sar_width,
                    frame->sar_height, vui->video_signal_type_present_flag,
                    vui->video_format, frame->video_full_range_flag,
                    vui->colour_description_present_flag,
                    frame->color_primaries, frame->color_trc,
                    frame->color_space, vui->vui_timing_info_present_flag,
                    frame->vui_num_units_in_tick, frame->vui_time_scale);
            } else
            {
                ni_log(NI_LOG_DEBUG, 
                    "ni_dec_retrieve_aux_data VUI, expecting H.265 VUI "
                    "struct size %d, got %u, dropped!\n",
                    (int)sizeof(ni_dec_h265_vui_param_t), frame->vui_len);
            }
        } else if (NI_CODEC_FORMAT_H264 == frame->src_codec)
        {
            if (sizeof(ni_dec_h264_vui_param_t) == frame->vui_len)
            {
                ni_dec_h264_vui_param_t *vui =
                    (ni_dec_h264_vui_param_t *)sei_buf;
                if (vui->colour_description_present_flag)
                {
                    frame->color_primaries = vui->colour_primaries;
                    frame->color_trc = vui->transfer_characteristics;
                    frame->color_space = vui->matrix_coefficients;
                }
                frame->video_full_range_flag = vui->video_full_range_flag;

                if (vui->aspect_ratio_info_present_flag)
                {
                    frame->aspect_ratio_idc = vui->aspect_ratio_idc;
                    if (255 == frame->aspect_ratio_idc)
                    {
                        frame->sar_width = vui->sar_width;
                        frame->sar_height = vui->sar_height;
                    }
                }

                if (vui->vui_timing_info_present_flag)
                {
                    frame->vui_num_units_in_tick = vui->vui_num_units_in_tick;
                    frame->vui_time_scale = vui->vui_time_scale;
                }

                ni_log(NI_LOG_DEBUG, 
                    "ni_dec_retrieve_aux_data H.264 VUI "
                    "aspect_ratio_info_present_flag %u aspect_ratio_idc %u "
                    "sar_width %u sar_height %u "
                    "video_signal_type_present_flag %u video_format %d "
                    "video_full_range_flag %d colour_description_present_flag "
                    "%u "
                    "color-pri %u color-trc %u color-space %u "
                    "vui_timing_info_present_flag %u vui_num_units_in_tick %u "
                    "vui_time_scale %u pic_struct_present_flag %u\n",
                    vui->aspect_ratio_info_present_flag,
                    frame->aspect_ratio_idc, frame->sar_width,
                    frame->sar_height, vui->video_signal_type_present_flag,
                    vui->video_format, frame->video_full_range_flag,
                    vui->colour_description_present_flag,
                    frame->color_primaries, frame->color_trc,
                    frame->color_space, vui->vui_timing_info_present_flag,
                    frame->vui_num_units_in_tick, frame->vui_time_scale,
                    vui->pic_struct_present_flag);
            } else
            {
                ni_log(NI_LOG_DEBUG, 
                    "ni_dec_retrieve_aux_data VUI, expecting H.264 VUI "
                    "struct size %d, got %u, dropped!\n",
                    (int)sizeof(ni_dec_h264_vui_param_t), frame->vui_len);
            }
        } else
        {
            ni_log(NI_LOG_DEBUG, 
                "ni_dec_retrieve_aux_data VUI, unsupported codec: %d, "
                "dropped\n",
                frame->src_codec);
        }
    }

    // alternative transfer characteristics SEI if available
    if (frame->sei_alt_transfer_characteristics_len &&
        frame->sei_alt_transfer_characteristics_offset)
    {
        sei_buf = (uint8_t *)frame->p_data[start_offset] +
            frame->sei_alt_transfer_characteristics_offset;

        // and overwrite the color-trc in the VUI
        ni_log(NI_LOG_DEBUG, "ni_dec_retrieve_aux_data alt trc SEI value %u over-"
                       "writting VUI color-trc value %u\n",
                       *sei_buf, frame->color_trc);
        frame->color_trc = *sei_buf;
    }
}

// internal function to convert struct of ROIs to NetInt ROI map and store
// them inside the encoder context passed in.
// return 0 if successful, -1 otherwise
static int set_roi_map(ni_session_context_t *p_enc_ctx,
                       ni_codec_format_t codec_format,
                       const ni_aux_data_t *aux_data, int nb_roi, int width,
                       int height, int intra_qp)
{
    int r;
    uint32_t i, j, k, m;
    const ni_region_of_interest_t *roi =
        (const ni_region_of_interest_t *)aux_data->data;
    uint32_t self_size = roi->self_size;
    int32_t set_qp = 0;
    uint32_t sumQp = 0;

    uint32_t max_cu_size = (codec_format == NI_CODEC_FORMAT_H264) ? 16 : 64;

    // for H.264, select ROI Map Block Unit Size: 16x16
    // for H.265, select ROI Map Block Unit Size: 64x64
    uint32_t roiMapBlockUnitSize =
        (codec_format == NI_CODEC_FORMAT_H264) ? 16 : 64;
    uint32_t mbWidth = ((width + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    uint32_t mbHeight = ((height + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    uint32_t numMbs = mbWidth * mbHeight;
    uint32_t subMbWidth = roiMapBlockUnitSize / 8;
    uint32_t subMbHeight = subMbWidth;
    uint32_t subNumMbs = subMbWidth * subMbHeight;

    // (ROI map version >= 1) each QP info takes 8-bit, represent 8x8 pixel
    // block
    uint32_t block_size = ((width + max_cu_size - 1) & (~(max_cu_size - 1))) *
        ((height + max_cu_size - 1) & (~(max_cu_size - 1))) / (8 * 8);

    // need to align to 64 bytes
    uint32_t customMapSize = ((block_size + 63) & (~63));
    if (!p_enc_ctx->roi_map)
    {
        p_enc_ctx->roi_map =
            (ni_enc_quad_roi_custom_map *)calloc(1, customMapSize);
        if (!p_enc_ctx->roi_map)
        {
            return -1;
        }
    }

    // init ipcm_flag to 0, roiAbsQp_falg to 0 (qp delta), and qp_info to 0
    memset(p_enc_ctx->roi_map, 0, customMapSize);

    // iterate ROI list from the last as regions are defined in order of
    // decreasing importance.
    for (r = nb_roi - 1; r >= 0; r--)
    {
        roi = (const ni_region_of_interest_t *)(aux_data->data + self_size * r);
        if (!roi->qoffset.den)
        {
            ni_log(NI_LOG_DEBUG, "ni_region_of_interest_t.qoffset.den must not be "
                           "zero.\n");
            continue;
        }

        set_qp = (int32_t)((float)roi->qoffset.num * 1.0f /
                           (float)roi->qoffset.den * NI_INTRA_QP_RANGE);
        set_qp = clip3(NI_MIN_QP_DELTA, NI_MAX_QP_DELTA, set_qp);
        // Adjust qp delta range (-25 to 25) to (0 to 63): 0 to 0, -1 to 1, -2 to
        // 2 ... 1 to 63, 2 to 62 ...
        // Theoretically the possible qp delta range is (-32 to 31)
        set_qp = (NI_MAX_QP_INFO + 1 - set_qp) % (NI_MAX_QP_INFO + 1);

        ni_log(NI_LOG_DEBUG, 
            "set_roi_map: left %d right %d top %d bottom %d num %d den"
            " %d set_qp %d\n",
            roi->left, roi->right, roi->top, roi->bottom, roi->qoffset.num,
            roi->qoffset.den, set_qp);

        // copy ROI MBs QPs into custom map
        for (j = 0; j < mbHeight; j++)
        {
            for (i = 0; i < mbWidth; i++)
            {
                k = j * (int)mbWidth + i;

                for (m = 0; m < subNumMbs; m++)
                {
                    if (((int)(i % mbWidth) >=
                         (int)((roi->left + roiMapBlockUnitSize - 1) /
                               roiMapBlockUnitSize) -
                             1) &&
                        ((int)(i % mbWidth) <=
                         (int)((roi->right + roiMapBlockUnitSize - 1) /
                               roiMapBlockUnitSize) -
                             1) &&
                        ((int)(j % mbHeight) >=
                         (int)((roi->top + roiMapBlockUnitSize - 1) /
                               roiMapBlockUnitSize) -
                             1) &&
                        ((int)(j % mbHeight) <=
                         (int)((roi->bottom + roiMapBlockUnitSize - 1) /
                               roiMapBlockUnitSize) -
                             1))
                    {
                        p_enc_ctx->roi_map[k * subNumMbs + m].field.ipcm_flag =
                            0;   // don't force skip mode
                        p_enc_ctx->roi_map[k * subNumMbs + m]
                            .field.roiAbsQp_flag = 0;   // delta QP
                        p_enc_ctx->roi_map[k * subNumMbs + m].field.qp_info =
                            set_qp;
                        // ni_log(NI_LOG_DEBUG, "## x %d y %d index %d\n", i,
                        // j, k*subNumMbs+m);
                    }
                }
                sumQp += p_enc_ctx->roi_map[k * subNumMbs].field.qp_info;
            }
        }
    }

    p_enc_ctx->roi_len = customMapSize;
    p_enc_ctx->roi_avg_qp =
        (sumQp + (numMbs >> 1)) / numMbs + NI_DEFAULT_INTRA_QP;   // round off

    return 0;
}

/*!*****************************************************************************
 *  \brief  Prepare auxiliary data that should be sent together with this frame
 *          to encoder based on the auxiliary data of the decoded frame.
 *
 * Note: Some of the SEI (e.g. HDR) will be updated and stored in encoder
 *       context whenever received through decoded frame; they will be sent
 *       out with the encoded frame to encoder only when appropriate, i.e.
 *       should_send_sei_with_frame is true. When a type of aux data is to be
 *       sent, its associated length will be set in the encoder frame.
 *
 *  \param[in/out]  p_enc_ctx encoder session contextwhose various SEI type
 *                  header can be updated as the result of this function
 *  \param[out]  p_enc_frame frame to be sent to encoder
 *  \param[in]  p_dec_frame frame that is returned by decoder
 *  \param[in]  codec_format H.264 or H.265
 *  \param[in]  should_send_sei_with_frame if need to send a certain type of
 *              SEI with this frame
 *  \param[out]  mdcv_data SEI for HDR mastering display color volume info
 *  \param[out]  cll_data SEI for HDR content light level info
 *  \param[out]  cc_data SEI for close caption
 *  \param[out]  udu_data SEI for User data unregistered
 *  \param[out]  hdrp_data SEI for HDR10+
 *
 *  \return NONE
 ******************************************************************************/
void ni_enc_prep_aux_data(ni_session_context_t *p_enc_ctx,
                          ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame,
                          ni_codec_format_t codec_format,
                          int should_send_sei_with_frame, uint8_t *mdcv_data,
                          uint8_t *cll_data, uint8_t *cc_data,
                          uint8_t *udu_data, uint8_t *hdrp_data)
{
    uint8_t *dst = NULL;
    ni_aux_data_t *aux_data = NULL;
    ni_xcoder_params_t *api_params =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;

    // reset all auxiliary data flag and length of encode frame
    p_enc_frame->preferred_characteristics_data_len =
        p_enc_frame->use_cur_src_as_long_term_pic =
            p_enc_frame->use_long_term_ref = 0;

    p_enc_frame->sei_total_len = p_enc_frame->sei_cc_offset =
        p_enc_frame->sei_cc_len =
            p_enc_frame->sei_hdr_mastering_display_color_vol_offset =
                p_enc_frame->sei_hdr_mastering_display_color_vol_len =
                    p_enc_frame->sei_hdr_content_light_level_info_offset =
                        p_enc_frame->sei_hdr_content_light_level_info_len =
                            p_enc_frame->sei_user_data_unreg_offset =
                                p_enc_frame->sei_user_data_unreg_len =
                                    p_enc_frame->sei_hdr_plus_offset =
                                        p_enc_frame->sei_hdr_plus_len = 0;

    // prep SEI for HDR (mastering display color volume)
    aux_data = ni_frame_get_aux_data(
        p_dec_frame, NI_FRAME_AUX_DATA_MASTERING_DISPLAY_METADATA);
    if (aux_data)
    {
        p_enc_ctx->mdcv_max_min_lum_data_len = 8;
        p_enc_ctx->sei_hdr_mastering_display_color_vol_len =
            8 + 6 * 2 + 2 * 2 + 2 * 4 + 1;
        if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            p_enc_ctx->sei_hdr_mastering_display_color_vol_len--;
        }

        ni_mastering_display_metadata_t *p_src =
            (ni_mastering_display_metadata_t *)aux_data->data;

        // save a copy
        if (!p_enc_ctx->p_master_display_meta_data)
        {
            p_enc_ctx->p_master_display_meta_data =
                malloc(sizeof(ni_mastering_display_metadata_t));
        }
        if (!p_enc_ctx->p_master_display_meta_data)
        {
            ni_log(NI_LOG_ERROR, "Error mem alloc for mastering display color vol\n");
        } else
        {
            memcpy(p_enc_ctx->p_master_display_meta_data, p_src,
                   sizeof(ni_mastering_display_metadata_t));

            const int luma_den = 10000;

            uint32_t uint32_t_tmp = htonl(
                (uint32_t)(lrint(luma_den * ni_q2d(p_src->max_luminance))));
            memcpy(p_enc_ctx->ui8_mdcv_max_min_lum_data, &uint32_t_tmp,
                   sizeof(uint32_t));
            uint32_t_tmp = htonl(
                (uint32_t)(lrint(luma_den * ni_q2d(p_src->min_luminance))));
            memcpy(p_enc_ctx->ui8_mdcv_max_min_lum_data + 4, &uint32_t_tmp,
                   sizeof(uint32_t));

            // emulation prevention checking of luminance data
            int emu_bytes_inserted = ni_insert_emulation_prevent_bytes(
                p_enc_ctx->ui8_mdcv_max_min_lum_data, 2 * 4);

            p_enc_ctx->mdcv_max_min_lum_data_len += emu_bytes_inserted;
            p_enc_ctx->sei_hdr_mastering_display_color_vol_len +=
                emu_bytes_inserted;
        }
    }

    if (p_enc_ctx->sei_hdr_mastering_display_color_vol_len &&
        p_enc_ctx->p_master_display_meta_data && should_send_sei_with_frame)
    {
        dst = mdcv_data;
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 1;

        if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            dst[4] = 0x6;
            dst[5] = 0x89;   // payload type=137
            dst[6] = 0x18;   // payload size=24
            dst += 7;
        } else
        {
            dst[4] = 0x4e;
            dst[5] = 1;
            dst[6] = 0x89;   // payload type=137
            dst[7] = 0x18;   // payload size=24
            dst += 8;
        }

        ni_enc_mastering_display_colour_volume_t *p_mdcv =
            (ni_enc_mastering_display_colour_volume_t *)dst;
        ni_mastering_display_metadata_t *p_src =
            (ni_mastering_display_metadata_t *)
                p_enc_ctx->p_master_display_meta_data;

        const int chroma_den = 50000;
        const int luma_den = 10000;

        uint16_t dp00 = 0, dp01 = 0, dp10 = 0, dp11 = 0, dp20 = 0, dp21 = 0,
                 wpx = 0, wpy = 0;
        // assuming p_src->has_primaries is always true
        // this is stored in r,g,b order and needs to be in g.b,r order
        // when sent to encoder
        dp00 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[1][0]));
        p_mdcv->display_primaries[0][0] = htons(dp00);
        dp01 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[1][1]));
        p_mdcv->display_primaries[0][1] = htons(dp01);
        dp10 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[2][0]));
        p_mdcv->display_primaries[1][0] = htons(dp10);
        dp11 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[2][1]));
        p_mdcv->display_primaries[1][1] = htons(dp11);
        dp20 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[0][0]));
        p_mdcv->display_primaries[2][0] = htons(dp20);
        dp21 = (uint16_t)lrint(chroma_den *
                               ni_q2d(p_src->display_primaries[0][1]));
        p_mdcv->display_primaries[2][1] = htons(dp21);

        wpx = (uint16_t)lrint(chroma_den * ni_q2d(p_src->white_point[0]));
        p_mdcv->white_point_x = htons(wpx);
        wpy = (uint16_t)lrint(chroma_den * ni_q2d(p_src->white_point[1]));
        p_mdcv->white_point_y = htons(wpy);

        ni_log(NI_LOG_DEBUG, 
            "mastering display color volume, primaries "
            "%u/%u/%u/%u/%u/%u white_point_x/y %u/%u max/min_lumi %u/%u\n",
            (uint16_t)dp00, (uint16_t)dp01, (uint16_t)dp10, (uint16_t)dp11,
            (uint16_t)dp20, (uint16_t)dp21, (uint16_t)wpx, (uint16_t)wpy,
            (uint32_t)(luma_den * ni_q2d(p_src->max_luminance)),
            (uint32_t)(luma_den * ni_q2d(p_src->min_luminance)));

        dst += 6 * 2 + 2 * 2;
        memcpy(dst, p_enc_ctx->ui8_mdcv_max_min_lum_data,
               p_enc_ctx->mdcv_max_min_lum_data_len);

        dst += p_enc_ctx->mdcv_max_min_lum_data_len;
        *dst = 0x80;

        p_enc_frame->sei_hdr_mastering_display_color_vol_len =
            p_enc_ctx->sei_hdr_mastering_display_color_vol_len;

        p_enc_frame->sei_total_len +=
            p_enc_frame->sei_hdr_mastering_display_color_vol_len;
    }

    // prep SEI for HDR (content light level info)
    aux_data = ni_frame_get_aux_data(p_dec_frame,
                                     NI_FRAME_AUX_DATA_CONTENT_LIGHT_LEVEL);
    if (aux_data)
    {
        // size of: start code + NAL unit header + payload type byte +
        //          payload size byte + payload + rbsp trailing bits, default HEVC
        p_enc_ctx->light_level_data_len = 4;
        p_enc_ctx->sei_hdr_content_light_level_info_len = 8 + 2 * 2 + 1;
        if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            p_enc_ctx->sei_hdr_content_light_level_info_len--;
        }

        uint16_t max_content_light_level =
            htons(((ni_content_light_level_t *)aux_data->data)->max_cll);
        uint16_t max_pic_average_light_level =
            htons(((ni_content_light_level_t *)aux_data->data)->max_fall);

        ni_log(NI_LOG_DEBUG, "content light level info, MaxCLL %u MaxFALL %u\n",
                       ((ni_content_light_level_t *)aux_data->data)->max_cll,
                       ((ni_content_light_level_t *)aux_data->data)->max_fall);

        memcpy(p_enc_ctx->ui8_light_level_data, &max_content_light_level,
               sizeof(uint16_t));
        memcpy(&(p_enc_ctx->ui8_light_level_data[2]),
               &max_pic_average_light_level, sizeof(uint16_t));

        // emulation prevention checking
        int emu_bytes_inserted = ni_insert_emulation_prevent_bytes(
            p_enc_ctx->ui8_light_level_data, p_enc_ctx->light_level_data_len);

        p_enc_ctx->light_level_data_len += emu_bytes_inserted;
        p_enc_ctx->sei_hdr_content_light_level_info_len += emu_bytes_inserted;
    }

    if (p_enc_ctx->sei_hdr_content_light_level_info_len &&
        should_send_sei_with_frame)
    {
        dst = cll_data;
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 1;

        if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            dst[4] = 0x6;
            dst[5] = 0x90;   // payload type=144
            dst[6] = 4;      // payload size=4
            dst += 7;
        } else
        {
            dst[4] = 0x4e;
            dst[5] = 1;
            dst[6] = 0x90;   // payload type=144
            dst[7] = 4;      // payload size=4
            dst += 8;
        }

        memcpy(dst, p_enc_ctx->ui8_light_level_data,
               p_enc_ctx->light_level_data_len);
        dst += p_enc_ctx->light_level_data_len;
        *dst = 0x80;

        p_enc_frame->sei_hdr_content_light_level_info_len =
            p_enc_ctx->sei_hdr_content_light_level_info_len;

        p_enc_frame->sei_total_len +=
            p_enc_frame->sei_hdr_content_light_level_info_len;
    }

    // prep SEI for close caption
    aux_data = ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_A53_CC);
    if (aux_data)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_prep_aux_data sei_cc_len %d\n", aux_data->size);

        uint8_t cc_data_emu_prevent[NI_MAX_SEI_DATA];
        int cc_size = aux_data->size;
        if (cc_size > NI_MAX_SEI_DATA)
        {
            ni_log(NI_LOG_DEBUG, "ni_enc_prep_aux_data sei_cc_len %d > MAX %d !\n",
                           aux_data->size, (int)NI_MAX_SEI_DATA);
            cc_size = NI_MAX_SEI_DATA;
        }
        memcpy(cc_data_emu_prevent, aux_data->data, cc_size);
        int cc_size_emu_prevent = cc_size +
            ni_insert_emulation_prevent_bytes(cc_data_emu_prevent, cc_size);
        if (cc_size_emu_prevent != cc_size)
        {
            ni_log(NI_LOG_DEBUG, "ni_enc_prep_aux_data: close caption "
                           "emulation prevention bytes added: %d\n",
                           cc_size_emu_prevent - cc_size);
        }

        dst = cc_data;
        // set header info fields and extra size based on codec
        if (NI_CODEC_FORMAT_H265 == codec_format)
        {
            p_enc_frame->sei_cc_len = NI_CC_SEI_HDR_HEVC_LEN +
                cc_size_emu_prevent + NI_CC_SEI_TRAILER_LEN;
            p_enc_frame->sei_total_len += p_enc_frame->sei_cc_len;

            p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc[7] = cc_size + 11;
            p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc[16] = (cc_size / 3) | 0xc0;

            memcpy(dst, p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc,
                   NI_CC_SEI_HDR_HEVC_LEN);
            dst += NI_CC_SEI_HDR_HEVC_LEN;
            memcpy(dst, cc_data_emu_prevent, cc_size_emu_prevent);
            dst += cc_size_emu_prevent;
            memcpy(dst, p_enc_ctx->sei_trailer, NI_CC_SEI_TRAILER_LEN);
        } else   // H.264
        {
            p_enc_frame->sei_cc_len = NI_CC_SEI_HDR_H264_LEN +
                cc_size_emu_prevent + NI_CC_SEI_TRAILER_LEN;
            p_enc_frame->sei_total_len += p_enc_frame->sei_cc_len;

            p_enc_ctx->itu_t_t35_cc_sei_hdr_h264[6] = cc_size + 11;
            p_enc_ctx->itu_t_t35_cc_sei_hdr_h264[15] = (cc_size / 3) | 0xc0;

            memcpy(dst, p_enc_ctx->itu_t_t35_cc_sei_hdr_h264,
                   NI_CC_SEI_HDR_H264_LEN);
            dst += NI_CC_SEI_HDR_H264_LEN;
            memcpy(dst, cc_data_emu_prevent, cc_size_emu_prevent);
            dst += cc_size_emu_prevent;
            memcpy(dst, p_enc_ctx->sei_trailer, NI_CC_SEI_TRAILER_LEN);
        }
    }

    // prep SEI for HDR+
    aux_data = ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_HDR_PLUS);
    if (aux_data)
    {
        ni_dynamic_hdr_plus_t *hdrp = (ni_dynamic_hdr_plus_t *)aux_data->data;
        int w, i, j;
        ni_bitstream_writer_t pb;
        uint32_t ui_tmp;

        ni_bitstream_writer_init(&pb);

        // HDR10+ SEI header bytes

        // itu_t_t35_provider_code and itu_t_t35_provider_oriented_code are
        // contained in the first 4 bytes of payload; pb has all the data until
        // start of trailer
        ni_bs_writer_put(&pb, 0, 8);
        ni_bs_writer_put(&pb, 0x3c,
                         8);   // u16 itu_t_t35_provider_code = 0x003c
        ni_bs_writer_put(&pb, 0, 8);
        // u16 itu_t_t35_provider_oriented_code = 0x0001
        ni_bs_writer_put(&pb, 0x01, 8);
        ni_bs_writer_put(&pb, 4, 8);   // u8 application_identifier = 0x04
        ni_bs_writer_put(&pb, 0, 8);   // u8 application version = 0x00

        ni_bs_writer_put(&pb, hdrp->num_windows, 2);
        ni_log(NI_LOG_DEBUG, "hdr10+ num_windows %u\n", hdrp->num_windows);
        for (w = 1; w < hdrp->num_windows; w++)
        {
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].window_upper_left_corner_x.num, 16);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].window_upper_left_corner_y.num, 16);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].window_lower_right_corner_x.num, 16);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].window_lower_right_corner_y.num, 16);
            ni_bs_writer_put(&pb, hdrp->params[w - 1].center_of_ellipse_x, 16);
            ni_bs_writer_put(&pb, hdrp->params[w - 1].center_of_ellipse_y, 16);
            ni_bs_writer_put(&pb, hdrp->params[w - 1].rotation_angle, 8);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].semimajor_axis_internal_ellipse, 16);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].semimajor_axis_external_ellipse, 16);
            ni_bs_writer_put(
                &pb, hdrp->params[w - 1].semiminor_axis_external_ellipse, 16);
            ni_bs_writer_put(&pb, hdrp->params[w - 1].overlap_process_option,
                             1);
        }

        // values are scaled up according to standard spec
        ui_tmp = lrint(10000 *
                       ni_q2d(hdrp->targeted_system_display_maximum_luminance));
        ni_bs_writer_put(&pb, ui_tmp, 27);
        ni_bs_writer_put(
            &pb, hdrp->targeted_system_display_actual_peak_luminance_flag, 1);
        ni_log(NI_LOG_DEBUG, "hdr10+ targeted_system_display_maximum_luminance "
                       "%u\n",
                       ui_tmp);
        ni_log(NI_LOG_DEBUG, 
            "hdr10+ targeted_system_display_actual_peak_luminance_"
            "flag %u\n",
            hdrp->targeted_system_display_actual_peak_luminance_flag);

        if (hdrp->targeted_system_display_actual_peak_luminance_flag)
        {
            ni_bs_writer_put(
                &pb,
                hdrp->num_rows_targeted_system_display_actual_peak_luminance,
                5);
            ni_bs_writer_put(
                &pb,
                hdrp->num_cols_targeted_system_display_actual_peak_luminance,
                5);
            ni_log(NI_LOG_DEBUG, 
                "hdr10+ num_rows_targeted_system_display_actual_peak_luminance "
                "x num_cols_targeted_system_display_actual_peak_luminance %u x "
                "%u\n",
                hdrp->num_rows_targeted_system_display_actual_peak_luminance,
                hdrp->num_cols_targeted_system_display_actual_peak_luminance);

            for (i = 0; i <
                 hdrp->num_rows_targeted_system_display_actual_peak_luminance;
                 i++)
                for (
                    j = 0; j <
                    hdrp->num_cols_targeted_system_display_actual_peak_luminance;
                    j++)
                {
                    ui_tmp = lrint(
                        15 *
                        ni_q2d(
                            hdrp->targeted_system_display_actual_peak_luminance
                                [i][j]));
                    ni_bs_writer_put(&pb, ui_tmp, 4);
                    ni_log(NI_LOG_DEBUG, "hdr10+ targeted_system_display_actual_peak_"
                                   "luminance[%d][%d] %u\n",
                                   i, j, ui_tmp);
                }
        }

        for (w = 0; w < hdrp->num_windows; w++)
        {
            for (i = 0; i < 3; i++)
            {
                ui_tmp = lrint(100000 * ni_q2d(hdrp->params[w].maxscl[i]));
                ni_bs_writer_put(&pb, ui_tmp, 17);
                ni_log(NI_LOG_DEBUG, "hdr10+ maxscl[%d][%d] %u\n", w, i, ui_tmp);
            }
            ui_tmp = lrint(100000 * ni_q2d(hdrp->params[w].average_maxrgb));
            ni_bs_writer_put(&pb, ui_tmp, 17);
            ni_log(NI_LOG_DEBUG, "hdr10+ average_maxrgb[%d] %u\n", w, ui_tmp);

            ni_bs_writer_put(
                &pb, hdrp->params[w].num_distribution_maxrgb_percentiles, 4);
            ni_log(NI_LOG_DEBUG, 
                "hdr10+ num_distribution_maxrgb_percentiles[%d] %d\n", w,
                hdrp->params[w].num_distribution_maxrgb_percentiles);

            for (i = 0; i < hdrp->params[w].num_distribution_maxrgb_percentiles;
                 i++)
            {
                ni_bs_writer_put(
                    &pb, hdrp->params[w].distribution_maxrgb[i].percentage, 7);
                ui_tmp = lrint(
                    100000 *
                    ni_q2d(hdrp->params[w].distribution_maxrgb[i].percentile));
                ni_bs_writer_put(&pb, ui_tmp, 17);
                ni_log(NI_LOG_DEBUG, 
                    "hdr10+ distribution_maxrgb_percentage[%d][%d] %u\n", w, i,
                    hdrp->params[w].distribution_maxrgb[i].percentage);
                ni_log(NI_LOG_DEBUG, 
                    "hdr10+ distribution_maxrgb_percentile[%d][%d] %u\n", w, i,
                    ui_tmp);
            }

            ui_tmp =
                lrint(1000 * ni_q2d(hdrp->params[w].fraction_bright_pixels));
            ni_bs_writer_put(&pb, ui_tmp, 10);
            ni_log(NI_LOG_DEBUG, "hdr10+ fraction_bright_pixels[%d] %u\n", w, ui_tmp);
        }

        ni_bs_writer_put(&pb,
                         hdrp->mastering_display_actual_peak_luminance_flag, 1);
        ni_log(NI_LOG_DEBUG, 
            "hdr10+ mastering_display_actual_peak_luminance_flag %u\n",
            hdrp->mastering_display_actual_peak_luminance_flag);
        if (hdrp->mastering_display_actual_peak_luminance_flag)
        {
            ni_bs_writer_put(
                &pb, hdrp->num_rows_mastering_display_actual_peak_luminance, 5);
            ni_bs_writer_put(
                &pb, hdrp->num_cols_mastering_display_actual_peak_luminance, 5);
            ni_log(NI_LOG_DEBUG, 
                "hdr10+ num_rows_mastering_display_actual_peak_luminance x "
                "num_cols_mastering_display_actual_peak_luminance %u x %u\n",
                hdrp->num_rows_mastering_display_actual_peak_luminance,
                hdrp->num_cols_mastering_display_actual_peak_luminance);

            for (i = 0;
                 i < hdrp->num_rows_mastering_display_actual_peak_luminance;
                 i++)
                for (j = 0;
                     j < hdrp->num_cols_mastering_display_actual_peak_luminance;
                     j++)
                {
                    ui_tmp = lrint(
                        15 *
                        ni_q2d(
                            hdrp->mastering_display_actual_peak_luminance[i]
                                                                         [j]));
                    ni_bs_writer_put(&pb, ui_tmp, 4);
                    ni_log(NI_LOG_DEBUG, 
                        "hdr10+ "
                        "mastering_display_actual_peak_luminance[%d][%d] %u\n",
                        i, j, ui_tmp);
                }
        }

        for (w = 0; w < hdrp->num_windows; w++)
        {
            ni_bs_writer_put(&pb, hdrp->params[w].tone_mapping_flag, 1);
            ni_log(NI_LOG_DEBUG, "hdr10+ tone_mapping_flag[%d] %u\n", w,
                           hdrp->params[w].tone_mapping_flag);

            if (hdrp->params[w].tone_mapping_flag)
            {
                ui_tmp = lrint(4095 * ni_q2d(hdrp->params[w].knee_point_x));
                ni_bs_writer_put(&pb, ui_tmp, 12);
                ni_log(NI_LOG_DEBUG, "hdr10+ knee_point_x[%d] %u\n", w, ui_tmp);

                ui_tmp = lrint(4095 * ni_q2d(hdrp->params[w].knee_point_y));
                ni_bs_writer_put(&pb, ui_tmp, 12);
                ni_log(NI_LOG_DEBUG, "hdr10+ knee_point_y[%d] %u\n", w, ui_tmp);

                ni_bs_writer_put(&pb, hdrp->params[w].num_bezier_curve_anchors,
                                 4);
                ni_log(NI_LOG_DEBUG, "hdr10+ num_bezier_curve_anchors[%d] %u\n", w,
                               hdrp->params[w].num_bezier_curve_anchors);
                for (i = 0; i < hdrp->params[w].num_bezier_curve_anchors; i++)
                {
                    ui_tmp = lrint(
                        1023 * ni_q2d(hdrp->params[w].bezier_curve_anchors[i]));
                    ni_bs_writer_put(&pb, ui_tmp, 10);
                    ni_log(NI_LOG_DEBUG, "hdr10+ bezier_curve_anchors[%d][%d] %u\n",
                                   w, i, ui_tmp);
                }
            }

            ni_bs_writer_put(&pb, hdrp->params[w].color_saturation_mapping_flag,
                             1);
            ni_log(NI_LOG_DEBUG, "hdr10+ color_saturation_mapping_flag[%d] %u\n", w,
                           hdrp->params[w].color_saturation_mapping_flag);
            if (hdrp->params[w].color_saturation_mapping_flag)
            {
                ui_tmp =
                    lrint(8 * ni_q2d(hdrp->params[w].color_saturation_weight));
                ni_bs_writer_put(&pb, 6, ui_tmp);
                ni_log(NI_LOG_DEBUG, "hdr10+ color_saturation_weight[%d] %u\n", w,
                               ui_tmp);
            }
        }   // num_windows

        uint64_t hdr10p_num_bytes = (ni_bs_writer_tell(&pb) + 7) / 8;
        ni_log(NI_LOG_DEBUG, "hdr10+ total bits: %d -> bytes %" PRIu64 "\n",
                       (int)ni_bs_writer_tell(&pb), hdr10p_num_bytes);
        ni_bs_writer_align_zero(&pb);

        dst = hdrp_data;

        // emulation prevention checking of payload
        int emu_bytes_inserted;

        // set header info fields and extra size based on codec
        if (NI_CODEC_FORMAT_H265 == codec_format)
        {
            p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_hevc[7] =
                (uint8_t)hdr10p_num_bytes + NI_RBSP_TRAILING_BITS_LEN;

            memcpy(dst, p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_hevc,
                   NI_HDR10P_SEI_HDR_HEVC_LEN);
            dst += NI_HDR10P_SEI_HDR_HEVC_LEN;
            ni_bs_writer_copy(dst, &pb);

            emu_bytes_inserted =
                ni_insert_emulation_prevent_bytes(dst, (int)hdr10p_num_bytes);
            dst += hdr10p_num_bytes + emu_bytes_inserted;
            *dst = p_enc_ctx->sei_trailer[1];
            //dst += NI_RBSP_TRAILING_BITS_LEN;

            p_enc_frame->sei_hdr_plus_len = NI_HDR10P_SEI_HDR_HEVC_LEN +
                (uint32_t)hdr10p_num_bytes + emu_bytes_inserted +
                NI_RBSP_TRAILING_BITS_LEN;
            p_enc_frame->sei_total_len += p_enc_frame->sei_hdr_plus_len;
        } else if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_h264[6] =
                (uint8_t)hdr10p_num_bytes + NI_RBSP_TRAILING_BITS_LEN;

            memcpy(dst, p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_h264,
                   NI_HDR10P_SEI_HDR_H264_LEN);
            dst += NI_HDR10P_SEI_HDR_H264_LEN;
            ni_bs_writer_copy(dst, &pb);

            emu_bytes_inserted =
                ni_insert_emulation_prevent_bytes(dst, (int)hdr10p_num_bytes);
            dst += hdr10p_num_bytes + emu_bytes_inserted;
            *dst = p_enc_ctx->sei_trailer[1];
            //dst += NI_RBSP_TRAILING_BITS_LEN;

            p_enc_frame->sei_hdr_plus_len = NI_HDR10P_SEI_HDR_H264_LEN +
                (uint8_t)hdr10p_num_bytes + emu_bytes_inserted +
                NI_RBSP_TRAILING_BITS_LEN;
            p_enc_frame->sei_total_len += p_enc_frame->sei_hdr_plus_len;
        } else
        {
            ni_log(NI_LOG_ERROR, 
                "ni_enc_prep_aux_data: codec %d not supported for HDR10+ "
                "SEI !\n",
                codec_format);
            p_enc_frame->sei_hdr_plus_len = 0;
        }

        ni_bs_writer_clear(&pb);
    }   // hdr10+

    // prep SEI for User Data Unregistered
    aux_data = ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_UDU_SEI);
    if (aux_data)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_prep_aux_data sei_user_data_unreg_len %d\n",
                       aux_data->size);

        // emulation prevention checking: a working buffer of size in worst case
        // that each two bytes comes with 1B emulation prevention byte
        int udu_sei_size = aux_data->size;
        int ext_udu_sei_size, sei_len;

        uint8_t *sei_data = malloc(udu_sei_size * 3 / 2);
        if (sei_data)
        {
            memcpy(sei_data, (uint8_t *)aux_data->data, udu_sei_size);
            int emu_bytes_inserted =
                ni_insert_emulation_prevent_bytes(sei_data, udu_sei_size);

            ext_udu_sei_size = udu_sei_size + emu_bytes_inserted;

            if (NI_CODEC_FORMAT_H264 == codec_format)
            {
                /* 4B long start code + 1B nal header + 1B SEI type + Bytes of
                   payload length + Bytes of SEI payload + 1B trailing */
                sei_len =
                    6 + ((udu_sei_size + 0xFE) / 0xFF) + ext_udu_sei_size + 1;
            } else
            {
                /* 4B long start code + 2B nal header + 1B SEI type + Bytes of
                   payload length + Bytes of SEI payload + 1B trailing */
                sei_len =
                    7 + ((udu_sei_size + 0xFE) / 0xFF) + ext_udu_sei_size + 1;
            }

            // discard this UDU SEI if the total SEI size exceeds the max size
            if (p_enc_frame->sei_total_len + sei_len > NI_ENC_MAX_SEI_BUF_SIZE)
            {
                ni_log(NI_LOG_ERROR, 
                    "ni_enc_prep_aux_data sei total length %u + sei_len %d "
                    "exceeds maximum sei size %u, discarding it !\n",
                    p_enc_frame->sei_total_len, sei_len,
                    NI_ENC_MAX_SEI_BUF_SIZE);
            } else
            {
                int payload_size = udu_sei_size;

                dst = udu_data;
                *dst++ = 0x00;   // long start code
                *dst++ = 0x00;
                *dst++ = 0x00;
                *dst++ = 0x01;

                if (NI_CODEC_FORMAT_H264 == codec_format)
                {
                    *dst++ = 0x06;   // nal type: SEI
                } else
                {
                    *dst++ = 0x4e;   // nal type: SEI
                    *dst++ = 0x01;
                }
                *dst++ = 0x05;   // SEI type: user data unregistered

                // original payload size
                while (payload_size > 0)
                {
                    *dst++ =
                        (payload_size > 0xFF ? 0xFF : (uint8_t)payload_size);
                    payload_size -= 0xFF;
                }

                // payload data after emulation prevention checking
                memcpy(dst, sei_data, ext_udu_sei_size);
                dst += ext_udu_sei_size;

                // trailing byte
                *dst = 0x80;
                dst++;

                // save UDU data length
                p_enc_frame->sei_user_data_unreg_len = sei_len;
                p_enc_frame->sei_total_len += sei_len;
            }

            free(sei_data);
            sei_data = NULL;
        }
    }

    // supply QP map if ROI enabled and if ROIs passed in as aux data
    aux_data = ni_frame_get_aux_data(p_dec_frame,
                                     NI_FRAME_AUX_DATA_REGIONS_OF_INTEREST);
    if (api_params->cfg_enc_params.roi_enable && aux_data)
    {
        int is_new_rois = 1;
        const ni_region_of_interest_t *roi = NULL;
        uint32_t self_size = 0;

        roi = (const ni_region_of_interest_t *)aux_data->data;
        self_size = roi->self_size;
        if (!self_size || aux_data->size % self_size)
        {
            ni_log(NI_LOG_ERROR, "Invalid ni_region_of_interest_t.self_size, "
                           "aux_data size %d self_size %u\n",
                           aux_data->size, self_size);
        } else
        {
            int nb_roi = aux_data->size / (int)self_size;

            // update ROI(s) if new/different from last one
            if (0 == p_enc_ctx->nb_rois || 0 == p_enc_ctx->roi_side_data_size ||
                !p_enc_ctx->av_rois || p_enc_ctx->nb_rois != nb_roi ||
                p_enc_ctx->roi_side_data_size != aux_data->size ||
                memcmp(p_enc_ctx->av_rois, aux_data->data, aux_data->size) != 0)
            {
                p_enc_ctx->roi_side_data_size = aux_data->size;
                p_enc_ctx->nb_rois = nb_roi;

                free(p_enc_ctx->av_rois);
                p_enc_ctx->av_rois = malloc(aux_data->size);
                if (!p_enc_ctx->av_rois)
                {
                    ni_log(NI_LOG_ERROR, "malloc ROI aux_data failed.\n");
                    is_new_rois = 0;
                } else
                {
                    memcpy(p_enc_ctx->av_rois, aux_data->data, aux_data->size);
                }
            } else
            {
                is_new_rois = 0;
            }

            if (is_new_rois)
            {
                if (set_roi_map(p_enc_ctx, codec_format, aux_data, nb_roi,
                                api_params->source_width,
                                api_params->source_height,
                                api_params->cfg_enc_params.rc.intra_qp))
                {
                    ni_log(NI_LOG_ERROR, "set_roi_map failed\n");
                }
            }
        }

        // ROI data in the frame
        p_enc_frame->roi_len = p_enc_ctx->roi_len;
    }

    // when ROI is enabled, a QP map is always supplied with each frame, and
    // we use frame->roi_len value to: when 0, supply a zeroed map; when non-0,
    // supply the map stored in p_enc_ctx->roi_map.
    // - if ROI aux data is present, use it, otherwise:
    // - if cacheRoi, use map if exists, use 0 map otherwise
    // - if !cacheRoi, use 0 map
    // Note: the above excludes the demo modes which is handled separately in
    //       application (e.g. nienc, xcoder)
    if (api_params->cfg_enc_params.roi_enable && !api_params->roi_demo_mode)
    {
        if (aux_data && p_enc_ctx->roi_map)
        {
            p_enc_frame->roi_len = p_enc_ctx->roi_len;
        } else
        {
            if (api_params->cacheRoi)
            {
                p_enc_frame->roi_len =
                    (p_enc_ctx->roi_map ? p_enc_ctx->roi_len : 0);
            } else
            {
                p_enc_frame->roi_len = 0;
            }
        }

        p_enc_frame->extra_data_len += p_enc_ctx->roi_len;

        ni_log(NI_LOG_DEBUG, "ni_enc_prep_aux_data: supply QP map, cacheRoi %d "
                       "aux_data %d ctx->roi_map %d frame->roi_len %u\n",
                       api_params->cacheRoi, aux_data != NULL,
                       p_enc_ctx->roi_map != NULL, p_enc_frame->roi_len);
    }

    // prep for NetInt long term reference frame support setting: when this has
    // been requested by both frame and API, API takes priority
    ni_long_term_ref_t ltr = {0};
    aux_data =
        ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_LONG_TERM_REF);
    if (aux_data)
    {
        ltr = *((ni_long_term_ref_t *)aux_data->data);
        ni_log(NI_LOG_DEBUG,
               "%s(): frame aux data LTR use_cur_src_as_ltr %u "
               "use_ltr %u\n",
               __func__, ltr.use_cur_src_as_long_term_pic,
               ltr.use_long_term_ref);
    }

    if (p_enc_ctx->ltr_to_set.use_cur_src_as_long_term_pic > 0)
    {
        ltr = p_enc_ctx->ltr_to_set;
        p_enc_ctx->ltr_to_set.use_cur_src_as_long_term_pic =
            p_enc_ctx->ltr_to_set.use_long_term_ref = 0;

        ni_log(NI_LOG_DEBUG,
               "%s(): frame API set LTR use_cur_src_as_ltr %u "
               "use_ltr %u\n",
               __func__, ltr.use_cur_src_as_long_term_pic,
               ltr.use_long_term_ref);
    }

    if (ltr.use_cur_src_as_long_term_pic > 0)
    {
        p_enc_frame->use_cur_src_as_long_term_pic =
            ltr.use_cur_src_as_long_term_pic;
        p_enc_frame->use_long_term_ref = ltr.use_long_term_ref;
    }

    // prep for NetInt target bitrate reconfiguration support: when bitrate
    // setting has been requested by both frame and API, API takes priority
    int32_t bitrate = -1;
    aux_data = ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_BITRATE);
    if (aux_data)
    {
        bitrate = *((int32_t *)aux_data->data);
        ni_log(NI_LOG_DEBUG, "%s(): frame aux data bitrate %d\n", __func__,
               bitrate);
    }

    if (p_enc_ctx->target_bitrate > 0)
    {
        bitrate = p_enc_ctx->target_bitrate;
        p_enc_ctx->target_bitrate = -1;
        ni_log(NI_LOG_DEBUG, "%s(): API set bitrate %d\n", __func__, bitrate);
    }

    if (bitrate > 0)
    {
        p_enc_ctx->enc_change_params->enable_option |=
            NI_SET_CHANGE_PARAM_RC_TARGET_RATE;

        p_enc_ctx->enc_change_params->bitRate = bitrate;
        p_enc_frame->reconf_len = sizeof(ni_encoder_change_params_t);
    }

    // prep for NetInt API force frame type
    if (p_enc_ctx->force_idr_frame)
    {
        p_enc_frame->force_key_frame = 1;
        p_enc_frame->ni_pict_type = PIC_TYPE_IDR;

        p_enc_ctx->force_idr_frame = 0;
        ni_log(NI_LOG_DEBUG, "%s(): API force IDR frame\n", __func__);
    }

    // prep for NetInt long term reference interval reconfiguration support:
    // when LTR has been requested by both frame and API, API takes priority
    int32_t ltr_interval = -1;
    aux_data =
        ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_LTR_INTERVAL);
    if (aux_data)
    {
        ltr_interval = *((int32_t *)aux_data->data);
        ni_log(NI_LOG_DEBUG, "%s(): frame aux data LTR interval %d\n", __func__,
               ltr_interval);
    }

    if (p_enc_ctx->ltr_interval > 0)
    {
        ltr_interval = p_enc_ctx->ltr_interval;
        p_enc_ctx->ltr_interval = -1;
        ni_log(NI_LOG_DEBUG, "%s(): API set LTR interval %d\n", __func__,
               ltr_interval);
    }

    if (ltr_interval > 0)
    {
        p_enc_ctx->enc_change_params->enable_option |=
            NI_SET_CHANGE_PARAM_LTR_INTERVAL;

        p_enc_ctx->enc_change_params->ltrInterval = ltr_interval;
        p_enc_frame->reconf_len = sizeof(ni_encoder_change_params_t);
    }

    // prep for NetInt target framerate reconfiguration support: when framerate
    // setting has been requested by both frame aux data and API, API takes priority
    ni_framerate_t framerate = {0};
    aux_data = ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_FRAMERATE);
    if (aux_data)
    {
        ni_framerate_t *aux_framerate_ptr = (ni_framerate_t *)aux_data->data;
        int32_t framerate_num = aux_framerate_ptr->framerate_num;
        int32_t framerate_denom = aux_framerate_ptr->framerate_denom;
        if ((framerate_num <= 0) || (framerate_denom <= 0))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): invalid framerate in aux data (%d/%d)\n",
                   __func__, framerate_num, framerate_denom);
        } else
        {
            if ((framerate_num % framerate_denom) != 0)
            {
                uint32_t numUnitsInTick = 1000;
                framerate_num = framerate_num / framerate_denom;
                framerate_denom = numUnitsInTick + 1;
                framerate_num += 1;
                framerate_num *= numUnitsInTick;
            } else
            {
                framerate_num = framerate_num / framerate_denom;
                framerate_denom = 1;
            }
            if (((framerate_num + framerate_denom - 1) / framerate_denom) >
                NI_MAX_FRAMERATE)
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR: %s(): invalid framerate in aux data (%d/%d)\n",
                       __func__, aux_framerate_ptr->framerate_num,
                       aux_framerate_ptr->framerate_denom);
            } else
            {
                framerate.framerate_num = framerate_num;
                framerate.framerate_denom = framerate_denom;
                ni_log(NI_LOG_DEBUG, "%s(): frame aux data framerate (%d/%d)\n",
                       __func__, framerate_num, framerate_denom);
            }
        }
    }

    if (p_enc_ctx->framerate.framerate_num > 0)
    {
        framerate = p_enc_ctx->framerate;
        p_enc_ctx->framerate.framerate_num =
            p_enc_ctx->framerate.framerate_denom = 0;
        ni_log(NI_LOG_DEBUG, "%s(): API set framerate (%d/%d)\n", __func__,
               framerate.framerate_num, framerate.framerate_denom);
    }

    if (framerate.framerate_num > 0)
    {
        p_enc_ctx->enc_change_params->enable_option |=
            NI_SET_CHANGE_PARAM_RC_FRAMERATE;

        p_enc_ctx->enc_change_params->frameRateNum = framerate.framerate_num;
        p_enc_ctx->enc_change_params->frameRateDenom =
            framerate.framerate_denom;
        p_enc_frame->reconf_len = sizeof(ni_encoder_change_params_t);
    }

    // prep for NetInt frame reference invalidation support: when this setting
    // has been requested by both frame and API, API takes priority
    int32_t frame_num = -1;
    aux_data =
        ni_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_INVALID_REF_FRAME);
    if (aux_data)
    {
        frame_num = *((int32_t *)aux_data->data);
        ni_log(NI_LOG_ERROR, "%s(): frame aux data frame ref invalid %d\n",
               __func__, frame_num);
    }

    if (p_enc_ctx->ltr_frame_ref_invalid > 0)
    {
        frame_num = p_enc_ctx->ltr_frame_ref_invalid;
        p_enc_ctx->ltr_frame_ref_invalid = -1;
        ni_log(NI_LOG_ERROR, "%s(): API set frame ref invalid %d\n", __func__,
               frame_num);
    }

    if (frame_num >= 0)
    {
        p_enc_ctx->enc_change_params->enable_option |=
            NI_SET_CHANGE_PARAM_INVALID_REF_FRAME;

        p_enc_ctx->enc_change_params->invalidFrameNum = frame_num;
        p_enc_frame->reconf_len = sizeof(ni_encoder_change_params_t);
    }

    // prep for alternative preferred transfer characteristics SEI
    if (api_params->cfg_enc_params.preferred_transfer_characteristics >= 0 &&
        should_send_sei_with_frame)
    {
        if (NI_CODEC_FORMAT_H264 == codec_format)
        {
            p_enc_frame->preferred_characteristics_data_len = 9;
        } else
        {
            p_enc_frame->preferred_characteristics_data_len = 10;
        }

        p_enc_ctx->preferred_characteristics_data =
            (uint8_t)
                api_params->cfg_enc_params.preferred_transfer_characteristics;
        p_enc_frame->sei_total_len +=
            p_enc_frame->preferred_characteristics_data_len;
    }
}

/*!*****************************************************************************
 *  \brief  Copy auxiliary data that should be sent together with this frame
 *          to encoder.
 *
 *  \param[in]  p_enc_ctx encoder session context
 *  \param[out]  p_enc_frame frame to be sent to encoder
 *  \param[in]  p_dec_frame frame returned by decoder
 *  \param[in]  mdcv_data SEI for HDR mastering display color volume info
 *  \param[in]  cll_data SEI for HDR content light level info
 *  \param[in]  cc_data SEI for close caption
 *  \param[in]  udu_data SEI for User data unregistered
 *  \param[in]  hdrp_data SEI for HDR10+
 *  \param[in]  is_hwframe, must be 0 (sw frame) or 1 (hw frame)
 *  \param[in]  is_nv12frame, must be 1 (nv12 frame) or 0 (not)
 *
 *  \return NONE
 ******************************************************************************/
void ni_enc_copy_aux_data(ni_session_context_t *p_enc_ctx,
                          ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame,
                          ni_codec_format_t codec_format,
                          const uint8_t *mdcv_data, const uint8_t *cll_data,
                          const uint8_t *cc_data, const uint8_t *udu_data,
                          const uint8_t *hdrp_data, int is_hwframe,
                          int is_nv12frame)
{
    ni_xcoder_params_t *api_params =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;

    // fill in extra data  (skipping meta data header)
    if (is_hwframe != 0 && is_hwframe != 1 && is_nv12frame != 0 &&
        is_nv12frame != 1)
    {
        ni_log(NI_LOG_ERROR, 
            "ni_enc_copy_aux_data: error, illegal hwframe or nv12frame\n");
        return;
    }
    uint8_t *dst = (uint8_t *)p_enc_frame->p_data[2 + is_hwframe] +
        p_enc_frame->data_len[2 + is_hwframe] + NI_APP_ENC_FRAME_META_DATA_SIZE;

    if (!is_hwframe)
    {
        dst = (uint8_t *)p_enc_frame->p_data[2 - is_nv12frame] +
            p_enc_frame->data_len[2 - is_nv12frame] +
            NI_APP_ENC_FRAME_META_DATA_SIZE;
    }

    // fill in reconfig data if enabled; even if it's disabled, keep the space
    // for it if SEI or ROI is present;
    if (p_enc_frame->reconf_len || api_params->cfg_enc_params.roi_enable ||
        p_enc_frame->sei_total_len)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: keep reconfig space: %"
               PRId64 "\n", sizeof(ni_encoder_change_params_t));

        memset(dst, 0, sizeof(ni_encoder_change_params_t));

        if (p_enc_frame->reconf_len && p_enc_ctx->enc_change_params)
        {
            memcpy(dst, p_enc_ctx->enc_change_params, p_enc_frame->reconf_len);
        }

        dst += sizeof(ni_encoder_change_params_t);
    }

    // fill in ROI map, if ROI is enabled; the ROI map could be:
    // - a zeroed one, e.g. in the case of no any ROI specified yet at session
    //   start, or
    // - one generated by ROI auxiliary data coming with the frame, or
    // - a hardcoded one created in nienc/xcoder by ROI demo modes
    if (api_params->cfg_enc_params.roi_enable)
    {
        if (p_enc_frame->roi_len && p_enc_ctx->roi_map)
        {
            memcpy(dst, p_enc_ctx->roi_map, p_enc_frame->roi_len);
            ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: ROI size: %u\n",
                           p_enc_frame->roi_len);

        } else
        {
            memset(dst, 0, p_enc_ctx->roi_len);
            ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: zeroed ROI size: %u\n",
                           p_enc_ctx->roi_len);
        }

        dst += p_enc_ctx->roi_len;
        // reset frame ROI len to the actual map size
        p_enc_frame->roi_len = p_enc_ctx->roi_len;
    }

    // HDR SEI: mastering display color volume
    if (p_enc_frame->sei_hdr_mastering_display_color_vol_len)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: HDR SEI mdcv size: %u\n",
                       p_enc_frame->sei_hdr_mastering_display_color_vol_len);
        memcpy(dst, mdcv_data,
               p_enc_frame->sei_hdr_mastering_display_color_vol_len);
        dst += p_enc_frame->sei_hdr_mastering_display_color_vol_len;
    }

    // HDR SEI: content light level info
    if (p_enc_frame->sei_hdr_content_light_level_info_len)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: HDR SEI cll size: %u\n",
                       p_enc_frame->sei_hdr_content_light_level_info_len);

        memcpy(dst, cll_data,
               p_enc_frame->sei_hdr_content_light_level_info_len);
        dst += p_enc_frame->sei_hdr_content_light_level_info_len;
    }

    // HLG SEI: preferred characteristics
    if (p_enc_frame->preferred_characteristics_data_len)
    {
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 1;
        if (NI_CODEC_FORMAT_H265 == codec_format)
        {
            dst[4] = 0x4e;
            dst[5] = 1;
            dst[6] = 0x93;   // payload type=147
            dst[7] = 1;      // payload size=1
            dst += 8;
        } else
        {
            dst[4] = 0x6;
            dst[5] = 0x93;   // payload type=147
            dst[6] = 1;      // payload size=1
            dst += 7;
        }
        *dst = p_enc_ctx->preferred_characteristics_data;
        dst++;
        *dst = 0x80;
        dst++;
    }

    // close caption
    if (p_enc_frame->sei_cc_len)
    {
        ni_log(NI_LOG_DEBUG, "ni_enc_copy_aux_data: close caption size: %u\n",
                       p_enc_frame->sei_cc_len);

        memcpy(dst, cc_data, p_enc_frame->sei_cc_len);
        dst += p_enc_frame->sei_cc_len;
    }

    // HDR10+
    if (p_enc_frame->sei_hdr_plus_len)
    {
        memcpy(dst, hdrp_data, p_enc_frame->sei_hdr_plus_len);
        dst += p_enc_frame->sei_hdr_plus_len;
    }

    // User data unregistered SEI
    if (p_enc_frame->sei_user_data_unreg_len)
    {
        memcpy(dst, udu_data, p_enc_frame->sei_user_data_unreg_len);
        //dst += p_enc_frame->sei_user_data_unreg_len;
    }
}
