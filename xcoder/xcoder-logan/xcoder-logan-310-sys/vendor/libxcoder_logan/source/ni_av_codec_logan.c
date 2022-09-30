/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_av_codec_logan.c
*
*  \brief  NETINT audio/video related utility functions
*
*******************************************************************************/

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#include <winsock.h>
#elif __linux__
#include <arpa/inet.h>
#endif
#include "ni_util_logan.h"
#include "ni_nvme_logan.h"
#include "ni_bitstream_logan.h"
#include "ni_av_codec_logan.h"


typedef enum
{
  SLICE_TYPE_B = 0,
  SLICE_TYPE_P = 1,
  SLICE_TYPE_I = 2,
  SLICE_TYPE_MP = 3
} slice_type_t;

typedef enum
{
  GOP_PRESET_CUSTOM        = 0,
  GOP_PRESET_I_1           = 1,
  GOP_PRESET_P_1           = 2,
  GOP_PRESET_B_1           = 3,
  GOP_PRESET_BP_2          = 4,
  GOP_PRESET_BBBP_3        = 5,
  GOP_PRESET_LP_4          = 6,
  GOP_PRESET_LD_4          = 7,
  GOP_PRESET_RA_8          = 8,
  // single_ref
  GOP_PRESET_SP_1          = 9,
  GOP_PRESET_BSP_2         = 10,
  GOP_PRESET_BBBSP_3       = 11,
  GOP_PRESET_LSP_4         = 12,

  // newly added
  GOP_PRESET_BBP_3         = 13,
  GOP_PRESET_BBSP_3        = 14,
  GOP_PRESET_BBBBBBBP_8    = 15,
  GOP_PRESET_BBBBBBBSP_8   = 16,
  NUM_GOP_PRESET_NUM       = 17,
} gop_preset_t;


static const int32_t GOP_SIZE[NUM_GOP_PRESET_NUM] =
{0, 1, 1, 1, 2, 4, 4, 4, 8, 1, 2, 4, 4};

static const int32_t LT_GOP_PRESET_I_1[6] = {SLICE_TYPE_I,  1, 0, 0, 0, 0};
static const int32_t LT_GOP_PRESET_P_1[6] = {SLICE_TYPE_MP, 1, 1, 0, 0, -1};
static const int32_t LT_GOP_PRESET_B_1[6] = {SLICE_TYPE_B,  1, 1, 0, 0, -1};
// gop_size = 2
static const int32_t LT_GOP_PRESET_BP_2[12] =
{
  SLICE_TYPE_MP, 2, 1, 0, 0, -2,
  SLICE_TYPE_B,  1, 3, 0, 0, 2,
};
// gop_size = 4
static const int32_t LT_GOP_PRESET_BBBP_4[24] =
{
  SLICE_TYPE_MP, 4, 1, 0, 0, -4,
  SLICE_TYPE_B,  2, 3, 0, 0, 4,
  SLICE_TYPE_B,  1, 5, 0, 0, 2,
  SLICE_TYPE_B,  3, 5, 0, 2, 4,
};

static const int32_t LT_GOP_PRESET_LP_4[24] =
{
  SLICE_TYPE_MP, 1, 5, 0, 0, -4,
  SLICE_TYPE_MP, 2, 3, 0, 1, 0,
  SLICE_TYPE_MP, 3, 5, 0, 2, 0,
  SLICE_TYPE_MP, 4, 1, 0, 3, 0,
};
static const int32_t LT_GOP_PRESET_LD_4[24] =
{
  SLICE_TYPE_B, 1, 5, 0, 0, -4,
  SLICE_TYPE_B, 2, 3, 0, 1, 0,
  SLICE_TYPE_B, 3, 5, 0, 2, 0,
  SLICE_TYPE_B, 4, 1, 0, 3, 0,
};

// gop_size = 8
static const int32_t LT_GOP_PRESET_RA_8[48] =
{
  SLICE_TYPE_B, 8, 1, 0, 0, -8,
  SLICE_TYPE_B, 4, 3, 0, 0, 8,
  SLICE_TYPE_B, 2, 5, 0, 0, 4,
  SLICE_TYPE_B, 1, 8, 0, 0, 2,
  SLICE_TYPE_B, 3, 8, 0, 2, 4,
  SLICE_TYPE_B, 6, 5, 0, 4, 8,
  SLICE_TYPE_B, 5, 8, 0, 4, 6,
  SLICE_TYPE_B, 7, 8, 0, 6, 8,
};
// single-ref-P
static const int32_t LT_GOP_PRESET_SP_1[6] = {SLICE_TYPE_P, 1, 1, 0, 0, -1};

static const int32_t LT_GOP_PRESET_BSP_2[12] =
{
  SLICE_TYPE_P, 2, 1, 0, 0, -2,
  SLICE_TYPE_B, 1, 3, 0, 0, 2,
};
static const int32_t LT_GOP_PRESET_BBBSP_4[24] =
{
  SLICE_TYPE_P, 4, 1, 0, 0, -4,
  SLICE_TYPE_B, 2, 3, 0, 0, 4,
  SLICE_TYPE_B, 1, 5, 0, 0, 2,
  SLICE_TYPE_B, 3, 5, 0, 2, 4,
};
static const int32_t LT_GOP_PRESET_LSP_4[24] =
{
  SLICE_TYPE_P, 1, 5, 0, 0, -4,
  SLICE_TYPE_P, 2, 3, 0, 1, 0,
  SLICE_TYPE_P, 3, 5, 0, 2, 0,
  SLICE_TYPE_P, 4, 1, 0, 3, 0,
};

static const int32_t LT_GOP_PRESET_BBP_3[18] =
{
  SLICE_TYPE_MP, 3, 1, 0, 0, -3,
  SLICE_TYPE_B, 1, 3, 0, 0, 3,
  SLICE_TYPE_B, 2, 6, 0, 1, 3,
};

static const int32_t LT_GOP_PRESET_BBSP_3[18] =
{
  SLICE_TYPE_P, 3, 1, 0, 0, 0,
  SLICE_TYPE_B, 1, 3, 0, 0, 3,
  SLICE_TYPE_B, 2, 6, 0, 1, 3,
};

static const int32_t LT_GOP_PRESET_BBBBBBBP_8[48] =
{
  SLICE_TYPE_MP, 8, 1, 0, 0, -8,
  SLICE_TYPE_B, 4, 3, 0, 0, 8,
  SLICE_TYPE_B, 2, 5, 0, 0, 4,
  SLICE_TYPE_B, 1, 8, 0, 0, 2,
  SLICE_TYPE_B, 3, 8, 0, 2, 4,
  SLICE_TYPE_B, 6, 5, 0, 4, 8,
  SLICE_TYPE_B, 5, 8, 0, 4, 6,
  SLICE_TYPE_B, 7, 8, 0, 6, 8,
};

static const int32_t LT_GOP_PRESET_BBBBBBBSP_8[48] =
{
  SLICE_TYPE_P, 8, 1, 0, 0, 0,
  SLICE_TYPE_B, 4, 3, 0, 0, 8,
  SLICE_TYPE_B, 2, 5, 0, 0, 4,
  SLICE_TYPE_B, 1, 8, 0, 0, 2,
  SLICE_TYPE_B, 3, 8, 0, 2, 4,
  SLICE_TYPE_B, 6, 5, 0, 4, 8,
  SLICE_TYPE_B, 5, 8, 0, 4, 6,
  SLICE_TYPE_B, 7, 8, 0, 6, 8,
};
static const int32_t* GOP_PRESET[NUM_GOP_PRESET_NUM] =
{
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

  LT_GOP_PRESET_BBP_3    ,
  LT_GOP_PRESET_BBSP_3   ,
  LT_GOP_PRESET_BBBBBBBP_8 ,
  LT_GOP_PRESET_BBBBBBBSP_8,
};

static void init_gop_param(ni_logan_custom_gop_params_t *gopParam,
                           ni_logan_encoder_params_t *param)
{
  int i;
  int j;
  int gopSize;
  int gopPreset = param->hevc_enc_params.gop_preset_index;

  // GOP_PRESET_IDX_CUSTOM
  if (gopPreset == 0)
  {
    memcpy(gopParam, &param->hevc_enc_params.custom_gop_params,
           sizeof(ni_logan_custom_gop_params_t));
  }
  else
  {
    const int32_t*  src_gop = GOP_PRESET[gopPreset];
    gopSize = GOP_SIZE[gopPreset];
    gopParam->custom_gop_size = gopSize;
    for(i = 0, j = 0; i < gopSize; i++)
    {
      gopParam->pic_param[i].pic_type      = src_gop[j++];
      gopParam->pic_param[i].poc_offset    = src_gop[j++];
      gopParam->pic_param[i].pic_qp        = src_gop[j++] + param->hevc_enc_params.rc.intra_qp;
      gopParam->pic_param[i].temporal_id   = src_gop[j++];
      gopParam->pic_param[i].ref_poc_L0    = src_gop[j++];
      gopParam->pic_param[i].ref_poc_L1    = src_gop[j++];
    }
  }
}

static int check_low_delay_flag(ni_logan_encoder_params_t *param,
                                ni_logan_custom_gop_params_t *gopParam)
{
  int i;
  int minVal = 0;
  int low_delay = 0;
  int gopPreset = param->hevc_enc_params.gop_preset_index;

  if (gopPreset == 0) // // GOP_PRESET_IDX_CUSTOM
  {
    if (gopParam->custom_gop_size > 1)
    {
      minVal = gopParam->pic_param[0].poc_offset;
      low_delay = 1;
      for (i = 1; i < gopParam->custom_gop_size; i++)
      {
        if (minVal > gopParam->pic_param[i].poc_offset)
        {
          low_delay = 0;
          break;
        }
        else
        {
          minVal = gopParam->pic_param[i].poc_offset;
        }
      }
    }
  }
  else if (gopPreset == 1 || gopPreset == 2 || gopPreset == 3 ||
           gopPreset == 6 || gopPreset == 7 || gopPreset == 9)
  {
    low_delay = 1;
  }

  return low_delay;
}

static int get_num_reorder_of_gop_structure(ni_logan_encoder_params_t *param)
{
  int i;
  int j;
  int ret_num_reorder = 0;
  ni_logan_custom_gop_params_t gopParam;

  init_gop_param(&gopParam, param);
  for(i = 0; i < gopParam.custom_gop_size; i++)
  {
    int check_reordering_num = 0;
    int num_reorder = 0;

    ni_logan_gop_params_t *gopPicParam = &gopParam.pic_param[i];

    for(j = 0; j < gopParam.custom_gop_size; j++)
    {
      ni_logan_gop_params_t *gopPicParamCand = &gopParam.pic_param[j];
      if (gopPicParamCand->poc_offset <= gopPicParam->poc_offset)
        check_reordering_num = j;
    }

    for(j = 0; j < check_reordering_num; j++)
    {
      ni_logan_gop_params_t *gopPicParamCand = &gopParam.pic_param[j];

      if (gopPicParamCand->temporal_id <= gopPicParam->temporal_id &&
          gopPicParamCand->poc_offset > gopPicParam->poc_offset)
        num_reorder++;
    }
    ret_num_reorder = num_reorder;
  }
  return ret_num_reorder;
}

static inline int ni_logan_min(int a, int b)
{
  return a < b ? a : b;
}

static inline int ni_logan_max(int a, int b)
{
  return a > b ? a : b;
}

static inline float ni_logan_minf(float a, float b)
{
  return a < b ? a : b;
}

static inline float ni_logan_maxf(float a, float b)
{
  return a > b ? a : b;
}

static int get_max_dec_pic_buffering_of_gop_structure(
  ni_logan_encoder_params_t *param)
{
  int max_dec_pic_buffering;
  max_dec_pic_buffering = ni_logan_min(16/*MAX_NUM_REF*/, ni_logan_max(get_num_reorder_of_gop_structure(param) + 2, 6 /*maxnumreference in spec*/) + 1);
  return max_dec_pic_buffering;
}

static int get_poc_of_gop_structure(ni_logan_encoder_params_t *param,
                                    uint32_t frame_idx)
{
  int low_delay;
  int gopSize;
  int poc;
  int gopIdx;
  int gopNum;
  ni_logan_custom_gop_params_t gopParam;

  init_gop_param(&gopParam, param);
  gopSize = gopParam.custom_gop_size;
  low_delay = check_low_delay_flag(param, &gopParam);

  if (low_delay)
  {
    poc = frame_idx;
  }
  else
  {
    gopIdx = frame_idx % gopSize;
    gopNum = frame_idx / gopSize;
    poc = gopParam.pic_param[gopIdx].poc_offset + (gopSize * gopNum);
  }

  poc += gopSize - 1; // use gop_size - 1 as offset
  return poc;
}

static inline int calc_scale(uint32_t x)
{
  static uint8_t lut[16] = {4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
  int y, z = (((x & 0xffff) - 1) >> 27) & 16;
  x >>= z;
  z += y = (((x & 0xff) - 1) >> 28) & 8;
  x >>= y;
  z += y = (((x & 0xf) - 1) >> 29) & 4;
  x >>= y;
  return z + lut[x&0xf];
}

static inline int clip3(int min, int max, int a)
{
  return ni_logan_min(ni_logan_max(min, a), max);
}

static inline float clip3f(float min, float max, float a)
{
  return ni_logan_minf(ni_logan_maxf(min, a), max);
}

static inline int calc_length(uint32_t x)
{
  static uint8_t lut[16] = {4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  int y, z = (((x >> 16) - 1) >> 27) & 16;
  x >>= z ^ 16;
  z += y = ((x - 0x100) >> 28) & 8;
  x >>= y ^ 8;
  z += y = ((x - 0x10) >> 29) & 4;
  x >>= y ^ 4;
  return z + lut[x];
}

#define BR_SHIFT  6
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
int ni_logan_should_send_sei_with_frame(ni_logan_session_context_t* p_enc_ctx,
                                  ni_logan_pic_type_t pic_type,
                                  ni_logan_encoder_params_t *p_param)
{
  // repeatHeaders = 0. send on first frame only (IDR)
  // repeatHeaders = 1. send on every I-frame including I-frames generated
  // on the intraPeriod interval as well as I-frames that are forced.
  if (0 == p_enc_ctx->frame_num ||
      LOGAN_PIC_TYPE_IDR == pic_type ||
      (p_param->hevc_enc_params.forced_header_enable &&
       p_param->hevc_enc_params.intra_period &&
       0 == (p_enc_ctx->frame_num % p_param->hevc_enc_params.intra_period)))
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

// compare two rationals:
// return: 0 if a == b
//         1 if a > b
//        -1 if a < b
static int ni_cmp_rational(ni_rational_t a, ni_rational_t b)
{
  const int64_t tmp = a.num * (int64_t)b.den - b.num * (int64_t)a.den;

  if (tmp) return (int)((tmp ^ a.den ^ b.den)>>63)|1;
  else if(b.den && a.den) return 0;
  else if(a.num && b.num) return (a.num>>31) - (b.num>>31);
  return 0;
}

/*!*****************************************************************************
 *  \brief  Set SPS VUI part of encoded stream header
 *
 *  \param[in/out]  p_param encoder parameters, its VUI data member will be
 *                  updated.
 *  \param[in]  color_primaries color primaries
 *  \param[in]  color_trc color transfer characteristic
 *  \param[in]  color_space YUV colorspace type
 *  \param[in]  video_full_range_flag
 *  \param[in]  sar_num/sar_den sample aspect ration in numerator/denominator
 *  \param[in]  codec_format H.264 or H.265
 *  \param[out]  hrd_params struct for HRD parameters, may be updated.
 *
 *  \return NONE
 ******************************************************************************/
void ni_logan_set_vui(ni_logan_encoder_params_t *p_param,
                ni_color_primaries_t color_primaries,
                ni_color_transfer_characteristic_t color_trc,
                ni_color_space_t color_space,
                int video_full_range_flag,
                int sar_num, int sar_den, ni_logan_codec_format_t codec_format,
                ni_hrd_params_t *hrd_params)
{
  ni_bitstream_writer_t rbsp;
  unsigned int aspect_ratio_idc = 255; // default: extended_sar
  int nal_hrd_parameters_present_flag=1, vcl_hrd_parameters_present_flag=0;
  int layer, cpb;
  int maxcpboutputdelay;
  int maxdpboutputdelay;
  int maxdelay;
  uint32_t vbvbuffersize = (p_param->bitrate / 1000) * p_param->hevc_enc_params.rc.rc_init_delay;
  uint32_t vbvmaxbitrate = p_param->bitrate;
  uint32_t vps_max_sub_layers_minus1 = SAMPLE_SPS_MAX_SUB_LAYERS_MINUS1;
  uint32_t bit_rate_value_minus1[MAX_CPB_COUNT][MAX_VPS_MAX_SUB_LAYERS];
  uint32_t cpb_size_value_minus1[MAX_CPB_COUNT][MAX_VPS_MAX_SUB_LAYERS];
  uint32_t cpb_cnt_minus1[MAX_VPS_MAX_SUB_LAYERS];

  uint32_t fixed_pic_rate_general_flag[MAX_VPS_MAX_SUB_LAYERS];
  uint32_t fixed_pic_rate_within_cvs_flag[MAX_VPS_MAX_SUB_LAYERS];
  uint32_t elemental_duration_in_tc_minus1[MAX_VPS_MAX_SUB_LAYERS];

  uint32_t bit_rate_scale = 2;
  uint32_t cpb_size_scale = 5;
  uint32_t numUnitsInTick = 1000;
  uint32_t timeScale;
  int32_t i32frameRateInfo = p_param->hevc_enc_params.frame_rate;

  ni_log(NI_LOG_TRACE, "ni_logan_set_vui color_primaries %d color_trc %d color_space "
         "%d video_full_range_flag %d sar %d/%d codec %s\n",
         color_primaries, color_trc, color_space, video_full_range_flag,
         sar_num, sar_den, NI_LOGAN_CODEC_FORMAT_H264==codec_format?"H.264":"H.265");

  ni_bitstream_writer_init(&rbsp);

  if (0 == sar_num)
  {
    // sample aspect ratio is 0, don't include aspect_ratio_idc in vui
    ni_bs_writer_put(&rbsp, 0, 1); // aspect_ratio_info_present_flag=0
  }
  else
  {
    // sample aspect ratio is non-zero, include aspect_ratio_idc in vui
    ni_bs_writer_put(&rbsp, 1, 1);  //  aspect_ratio_info_present_flag=1

    if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                          ni_make_rational(1, 1)))
    {
      aspect_ratio_idc = 1;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(12, 11)))
    {
      aspect_ratio_idc = 2;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(10, 11)))
    {
      aspect_ratio_idc = 3;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(16, 11)))
    {
      aspect_ratio_idc = 4;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(40, 33)))
    {
      aspect_ratio_idc = 5;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(24, 11)))
    {
      aspect_ratio_idc = 6;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(20, 11)))
    {
      aspect_ratio_idc = 7;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(32, 11)))
    {
      aspect_ratio_idc = 8;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(80, 33)))
    {
      aspect_ratio_idc = 9;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(18, 11)))
    {
      aspect_ratio_idc = 10;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(15, 11)))
    {
      aspect_ratio_idc = 11;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(64, 33)))
    {
      aspect_ratio_idc = 12;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(160, 99)))
    {
      aspect_ratio_idc = 13;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(4, 3)))
    {
      aspect_ratio_idc = 14;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(3, 2)))
    {
      aspect_ratio_idc = 15;
    }
    else if (! ni_cmp_rational(ni_make_rational(sar_num, sar_den),
                               ni_make_rational(2, 1)))
    {
      aspect_ratio_idc = 16;
    }

    ni_bs_writer_put(&rbsp, aspect_ratio_idc, 8);  // aspect_ratio_idc
    if (255 == aspect_ratio_idc)
    {
      ni_bs_writer_put(&rbsp, sar_num, 16); // sar_width
      ni_bs_writer_put(&rbsp, sar_den, 16); // sar_height
    }
  }

  ni_bs_writer_put(&rbsp, 0, 1); // overscan_info_present_flag=0

  // VUI Parameters
  ni_bs_writer_put(&rbsp, 1, 1);  //  video_signal_type_present_flag=1
  ni_bs_writer_put(&rbsp, 5, 3);  //  video_format=5 (unspecified)
  ni_bs_writer_put(&rbsp, video_full_range_flag, 1);  //  video_full_range_flag
  ni_bs_writer_put(&rbsp, 1, 1);  //  colour_description_presenty_flag=1
  ni_bs_writer_put(&rbsp, color_primaries, 8);  //  color_primaries
  ni_bs_writer_put(&rbsp, color_trc, 8);  //  color_trc
  ni_bs_writer_put(&rbsp, color_space, 8);  //  color_space

  ni_bs_writer_put(&rbsp, 0, 1);      //  chroma_loc_info_present_flag=0

  if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
  {   // H.265 Only VUI parameters
    ni_bs_writer_put(&rbsp, 0, 1);  //  neutral_chroma_indication_flag=0
    ni_bs_writer_put(&rbsp, 0, 1);  //  field_seq_flag=0
    ni_bs_writer_put(&rbsp, 0, 1);  //  frame_field_info_present_flag=0
    ni_bs_writer_put(&rbsp, 0, 1);  //  default_display_window_flag=0
  }

  ni_bs_writer_put(&rbsp, 1, 1);      //  vui_timing_info_present_flag=1
  p_param->pos_num_units_in_tick = (uint32_t)ni_bs_writer_tell(&rbsp);
  ni_bs_writer_put(&rbsp, 0, 32);    //  vui_num_units_in_tick
  p_param->pos_time_scale = (uint32_t)ni_bs_writer_tell(&rbsp);
  ni_bs_writer_put(&rbsp, 0, 32);         //  vui_time_scale

  if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
  {
    // H.265 Only VUI parameters
    ni_bs_writer_put(&rbsp, 0, 1);  //  vui_poc_proportional_to_timing_flag=0
    if (! p_param->hrd_enable)
    {
      ni_bs_writer_put(&rbsp, 0, 1);  //  vui_hrd_parameters_present_flag=0
    }
    else
    {
      ni_bs_writer_put(&rbsp, 1, 1);  //  vui_hrd_parameters_present_flag=1

      ni_bs_writer_put(&rbsp, 1, 1); // nal_hrd_parameters_present_flag=1
      ni_bs_writer_put(&rbsp, 1, 0); // vcl_hrd_parameters_present_flag=0

      ni_bs_writer_put(&rbsp, 1, 0); // sub_pic_hrd_params_present_flag=0

      hrd_params->initial_cpb_removal_delay_length_minus1 = 23;
      hrd_params->au_cpb_removal_delay_length_minus1 = 23;

      bit_rate_value_minus1[0][0] = 59374;
      cpb_size_value_minus1[0][0] = 59374;
      cpb_cnt_minus1[0] = 0;
      fixed_pic_rate_general_flag[0] = 1;
      fixed_pic_rate_within_cvs_flag[0] = 1;
      elemental_duration_in_tc_minus1[0] = 0;

      // normalize hrd size and rate to the value / scale notation
      bit_rate_scale = clip3(0, 15, calc_scale(vbvmaxbitrate) - BR_SHIFT);
      bit_rate_value_minus1[0][0] = (vbvmaxbitrate >> (bit_rate_scale + BR_SHIFT)) - 1;

      cpb_size_scale = clip3(0, 15, calc_scale(vbvbuffersize) - CPB_SHIFT);
      cpb_size_value_minus1[0][0] = (vbvbuffersize >> (cpb_size_scale + CPB_SHIFT)) - 1;

      hrd_params->bit_rate_unscale = (bit_rate_value_minus1[0][0]+1) << (bit_rate_scale + BR_SHIFT);
      hrd_params->cpb_size_unscale = (cpb_size_value_minus1[0][0]+1) << (cpb_size_scale + CPB_SHIFT);

      if (p_param->fps_denominator != 0 &&
          (p_param->fps_number % p_param->fps_denominator) != 0)
      {
        numUnitsInTick += 1;
        i32frameRateInfo += 1;
      }
      timeScale = i32frameRateInfo * 1000;

      maxcpboutputdelay = ni_logan_min((int)(p_param->hevc_enc_params.intra_period * MAX_DURATION * timeScale / numUnitsInTick), INT_MAX);
      maxdpboutputdelay = (int)(get_max_dec_pic_buffering_of_gop_structure(p_param) * MAX_DURATION * timeScale / numUnitsInTick);
      maxdelay = (int)(90000.0 * hrd_params->cpb_size_unscale / hrd_params->bit_rate_unscale + 0.5);

      hrd_params->initial_cpb_removal_delay_length_minus1 =
      2 + clip3(4, 22, 32 - calc_length(maxdelay)) - 1;
      hrd_params->au_cpb_removal_delay_length_minus1 =
      clip3(4, 31, 32 - calc_length(maxcpboutputdelay)) - 1;
      hrd_params->dpb_output_delay_length_minus1 =
      clip3(4, 31, 32 - calc_length(maxdpboutputdelay)) - 1;

      ni_bs_writer_put(&rbsp, bit_rate_scale, 4); // bit_rate_scale
      ni_bs_writer_put(&rbsp, cpb_size_scale, 4); // cpb_size_scale

      ni_bs_writer_put(&rbsp, hrd_params->initial_cpb_removal_delay_length_minus1, 5);
      ni_bs_writer_put(&rbsp, hrd_params->au_cpb_removal_delay_length_minus1, 5);
      ni_bs_writer_put(&rbsp, hrd_params->dpb_output_delay_length_minus1, 5);

      for (layer = 0; layer <= (int32_t)vps_max_sub_layers_minus1; layer++)
      {
        ni_bs_writer_put(&rbsp, fixed_pic_rate_general_flag[layer], 1);

        if (! fixed_pic_rate_general_flag[layer])
        {
          ni_bs_writer_put(&rbsp, fixed_pic_rate_within_cvs_flag[layer], 1);
        }

        if (fixed_pic_rate_within_cvs_flag[layer])
        {
          ni_bs_writer_put_ue(&rbsp, elemental_duration_in_tc_minus1[layer]);
        }

        // low_delay_hrd_flag[layer] is not present and inferred to be 0

        ni_bs_writer_put_ue(&rbsp, cpb_cnt_minus1[layer]);

        if ((layer == 0 && nal_hrd_parameters_present_flag) ||
            (layer == 1 && vcl_hrd_parameters_present_flag))
        {
          for (cpb = 0; cpb <= (int32_t)cpb_cnt_minus1[layer]; cpb++)
          {
            ni_bs_writer_put_ue(&rbsp, bit_rate_value_minus1[cpb][layer]);

            ni_bs_writer_put_ue(&rbsp, cpb_size_value_minus1[cpb][layer]);

            // cbr_flag is inferred to be 0 as well ?
            ni_bs_writer_put(&rbsp, 0, 1/*cbr_flag[cpb][layer]*/);
          }
        }
      }
    }
    ni_bs_writer_put(&rbsp, 0, 1);      //  bitstream_restriction_flag=0
  }
  else
  {
    // H.264 Only VUI parameters
    ni_bs_writer_put(&rbsp, 1, 1);  // fixed_frame_rate_flag=1
    ni_bs_writer_put(&rbsp, 0, 1);  // nal_hrd_parameters_present_flag=0
    ni_bs_writer_put(&rbsp, 0, 1);  // vui_hrd_parameters_present_flag=0
    ni_bs_writer_put(&rbsp, 0, 1);  // pic_struct_present_flag=0

    // this flag is set to 1 for H.264 to reduce decode delay, and fill in
    // the rest of the section accordingly
    ni_bs_writer_put(&rbsp, 1, 1);  // bitstream_restriction_flag=1
    ni_bs_writer_put(&rbsp, 1, 1);  // motion_vectors_over_pic_boundaries_flag=1

    ni_bs_writer_put_ue(&rbsp, 2); // max_bytes_per_pic_denom=2 (default)
    ni_bs_writer_put_ue(&rbsp, 1); // max_bits_per_mb_denom=1 (default)
    ni_bs_writer_put_ue(&rbsp, 15); //log2_max_mv_length_horizontal=15 (default)
    ni_bs_writer_put_ue(&rbsp, 15); // log2_max_mv_length_vertical=15 (default)

    // max_num_reorder_frames (0 for low delay gops)
    int max_num_reorder_frames = ni_logan_get_num_reorder_of_gop_structure(p_param);
    ni_bs_writer_put_ue(&rbsp, max_num_reorder_frames);

    // max_dec_frame_buffering
    int num_ref_frames = ni_logan_get_num_ref_frame_of_gop_structure(p_param);
    int max_dec_frame_buffering = (num_ref_frames > max_num_reorder_frames ?
                                   num_ref_frames : max_num_reorder_frames);
    ni_bs_writer_put_ue(&rbsp, max_dec_frame_buffering);
  }

  p_param->ui32VuiDataSizeBits = (uint32_t)ni_bs_writer_tell(&rbsp);
  p_param->ui32VuiDataSizeBytes = (p_param->ui32VuiDataSizeBits + 7) / 8;

  ni_bs_writer_align_zero(&rbsp);

  ni_bs_writer_copy(p_param->ui8VuiRbsp, &rbsp);
  ni_bs_writer_clear(&rbsp);
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
void ni_logan_dec_retrieve_aux_data(ni_logan_frame_t *frame)
{
  uint8_t *sei_buf = NULL;
  ni_aux_data_t *aux_data = NULL;

  // User Data Unregistered SEI if available
  if (frame->sei_user_data_unreg_len && frame->sei_user_data_unreg_offset)
  {
    sei_buf = (uint8_t *)frame->p_data[0] + frame->sei_user_data_unreg_offset;
    if (! ni_logan_frame_new_aux_data_from_raw_data(
          frame, NI_FRAME_AUX_DATA_UDU_SEI, sei_buf,
          frame->sei_user_data_unreg_len))
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data error retrieve User "
             "data unregisted SEI !\n");
    }
  }

  // close caption data if available
  if (frame->sei_cc_len && frame->sei_cc_offset)
  {
    sei_buf = (uint8_t *)frame->p_data[0] + frame->sei_cc_offset;
    if (! ni_logan_frame_new_aux_data_from_raw_data(
          frame, NI_FRAME_AUX_DATA_A53_CC, sei_buf, frame->sei_cc_len))
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data error retrieve close "
             "caption SEI !\n");
    }
  }

  // hdr10 sei data if available

  // mastering display metadata
  if (frame->sei_hdr_mastering_display_color_vol_len &&
      frame->sei_hdr_mastering_display_color_vol_offset)
  {
    aux_data = ni_logan_frame_new_aux_data(
      frame, NI_FRAME_AUX_DATA_MASTERING_DISPLAY_METADATA,
      sizeof(ni_mastering_display_metadata_t));

    if (! aux_data)
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data error retrieve HDR10 "
             "mastering display color SEI !\n");
    }
    else
    {
      ni_mastering_display_metadata_t *mdm = (ni_mastering_display_metadata_t *)
      aux_data->data;
      const int chroma_den = 50000;
      const int luma_den = 10000;
      ni_logan_dec_mastering_display_colour_volume_t* pColourVolume =
      (ni_logan_dec_mastering_display_colour_volume_t*)(
        (uint8_t*)frame->p_data[0] +
        frame->sei_hdr_mastering_display_color_vol_offset);

      // HEVC uses a g,b,r ordering, which we convert to a more natural r,g,b,
      // this is so we are compatible with FFmpeg default soft decoder
      mdm->display_primaries[0][0].num = pColourVolume->display_primaries_x[2];
      mdm->display_primaries[0][0].den = chroma_den;
      mdm->display_primaries[0][1].num = pColourVolume->display_primaries_y[2];
      mdm->display_primaries[0][1].den = chroma_den;
      mdm->display_primaries[1][0].num = pColourVolume->display_primaries_x[0];
      mdm->display_primaries[1][0].den = chroma_den;
      mdm->display_primaries[1][1].num = pColourVolume->display_primaries_y[0];
      mdm->display_primaries[1][1].den = chroma_den;
      mdm->display_primaries[2][0].num = pColourVolume->display_primaries_x[1];
      mdm->display_primaries[2][0].den = chroma_den;
      mdm->display_primaries[2][1].num = pColourVolume->display_primaries_y[1];
      mdm->display_primaries[2][1].den = chroma_den;

      mdm->white_point[0].num = pColourVolume->white_point_x;
      mdm->white_point[0].den = chroma_den;
      mdm->white_point[1].num = pColourVolume->white_point_y;
      mdm->white_point[1].den = chroma_den;

      mdm->min_luminance.num = pColourVolume->min_display_mastering_luminance;
      mdm->min_luminance.den = luma_den;
      mdm->max_luminance.num = pColourVolume->max_display_mastering_luminance;
      mdm->max_luminance.den = luma_den;

      mdm->has_luminance = mdm->has_primaries = 1;
    }
  }

  // hdr10 content light level
  if (frame->sei_hdr_content_light_level_info_len &&
      frame->sei_hdr_content_light_level_info_offset)
  {
    aux_data = ni_logan_frame_new_aux_data(
      frame, NI_FRAME_AUX_DATA_CONTENT_LIGHT_LEVEL,
      sizeof(ni_content_light_level_t));

    if (! aux_data)
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data error retrieve HDR10 "
             "content light level SEI !\n");
    }
    else
    {
      ni_content_light_level_t *clm = (ni_content_light_level_t *)
      aux_data->data;
      ni_logan_content_light_level_info_t* pLightLevel =
      (ni_logan_content_light_level_info_t*)(
        (uint8_t*)frame->p_data[0] +
        frame->sei_hdr_content_light_level_info_offset);

      clm->max_cll  = pLightLevel->max_content_light_level;
      clm->max_fall = pLightLevel->max_pic_average_light_level;
    }
  }

  // hdr10+ sei data if available
  if (frame->sei_hdr_plus_len && frame->sei_hdr_plus_offset)
  {
    aux_data = ni_logan_frame_new_aux_data(frame, NI_FRAME_AUX_DATA_HDR_PLUS,
                                     sizeof(ni_dynamic_hdr_plus_t));

    if (! aux_data)
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data error retrieve HDR10+ "
             "SEI !\n");
    }
    else
    {
      int w, i, j, i_limit, j_limit;
      ni_dynamic_hdr_plus_t *hdrp = (ni_dynamic_hdr_plus_t *)aux_data->data;
      ni_bitstream_reader_t br;

      sei_buf = (uint8_t *)frame->p_data[0] + frame->sei_hdr_plus_offset;
      ni_bitstream_reader_init(&br, sei_buf, 8 * frame->sei_hdr_plus_len);
      hdrp->itu_t_t35_country_code = 0xB5;
      hdrp->application_version = 0;
      // first 7 bytes of t35 SEI data header already matched HDR10+, and:
      ni_bs_reader_skip_bits(&br, 7 * 8);

      // num_windows u(2)
      hdrp->num_windows = ni_bs_reader_get_bits(&br, 2);
      ni_log(NI_LOG_TRACE, "hdr10+ num_windows %u\n", hdrp->num_windows);
      if (! (1 == hdrp->num_windows || 2 == hdrp->num_windows ||
             3 == hdrp->num_windows))
      {
        // wrong format and skip this HDR10+ SEI
      }
      else
      {
        // the following block will be skipped for hdrp->num_windows == 1
        for (w = 1; w < hdrp->num_windows; w++)
        {
          hdrp->params[w - 1].window_upper_left_corner_x = ni_make_q(
            ni_bs_reader_get_bits(&br, 16), 1);
          hdrp->params[w - 1].window_upper_left_corner_y = ni_make_q(
            ni_bs_reader_get_bits(&br, 16), 1);
          hdrp->params[w - 1].window_lower_right_corner_x = ni_make_q(
            ni_bs_reader_get_bits(&br, 16), 1);
          hdrp->params[w - 1].window_lower_right_corner_y = ni_make_q(
            ni_bs_reader_get_bits(&br, 16), 1);
          hdrp->params[w - 1].center_of_ellipse_x =
          ni_bs_reader_get_bits(&br, 16);
          hdrp->params[w - 1].center_of_ellipse_y =
          ni_bs_reader_get_bits(&br, 16);
          hdrp->params[w - 1].rotation_angle = ni_bs_reader_get_bits(&br, 8);
          hdrp->params[w - 1].semimajor_axis_internal_ellipse =
          ni_bs_reader_get_bits(&br, 16);
          hdrp->params[w - 1].semimajor_axis_external_ellipse =
          ni_bs_reader_get_bits(&br, 16);
          hdrp->params[w - 1].semiminor_axis_external_ellipse =
          ni_bs_reader_get_bits(&br, 16);
          hdrp->params[w - 1].overlap_process_option =
          (ni_hdr_plus_overlap_process_option_t)ni_bs_reader_get_bits(&br, 1);
        }

        // values are scaled down according to standard spec
        hdrp->targeted_system_display_maximum_luminance.num =
        ni_bs_reader_get_bits(&br, 27);
        hdrp->targeted_system_display_maximum_luminance.den = 10000;

        hdrp->targeted_system_display_actual_peak_luminance_flag =
        ni_bs_reader_get_bits(&br, 1);

        ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_maximum_luminance "
               "%d\n", hdrp->targeted_system_display_maximum_luminance.num);
        ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_actual_peak_lumi"
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

          ni_log(NI_LOG_TRACE, "hdr10+ num_rows_targeted_system_display_actual"
                 "_peak_luminance x num_cols_targeted_system_display_actual_"
                 "peak_luminance %u x %u\n", i_limit, j_limit);

          i_limit = i_limit > 25 ? 25 : i_limit;
          j_limit = j_limit > 25 ? 25 : j_limit;
          for (i = 0; i < i_limit; i++)
            for (j = 0; j < j_limit; j++)
            {
              hdrp->targeted_system_display_actual_peak_luminance[i][j].num =
              ni_bs_reader_get_bits(&br, 4);
              hdrp->targeted_system_display_actual_peak_luminance[i][j].den =15;
              ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_actual_peak"
                     "_luminance[%d][%d] %d\n", i, j,
                     hdrp->targeted_system_display_actual_peak_luminance[i][j].num);
            }
        }

        for (w = 0; w < hdrp->num_windows; w++)
        {
          for (i = 0; i < 3; i++)
          {
            hdrp->params[w].maxscl[i].num = ni_bs_reader_get_bits(&br, 17);
            hdrp->params[w].maxscl[i].den = 100000;
            ni_log(NI_LOG_TRACE, "hdr10+ maxscl[%d][%d] %d\n", w, i,
                   hdrp->params[w].maxscl[i].num);
          }
          hdrp->params[w].average_maxrgb.num = ni_bs_reader_get_bits(&br, 17);
          hdrp->params[w].average_maxrgb.den = 100000;
          ni_log(NI_LOG_TRACE, "hdr10+ average_maxrgb[%d] %d\n",
                 w, hdrp->params[w].average_maxrgb.num);

          i_limit = hdrp->params[w].num_distribution_maxrgb_percentiles =
          ni_bs_reader_get_bits(&br, 4);
          ni_log(NI_LOG_TRACE,
                 "hdr10+ num_distribution_maxrgb_percentiles[%d] %d\n",
                 w, hdrp->params[w].num_distribution_maxrgb_percentiles);

          i_limit = i_limit > 15 ? 15 : i_limit;
          for (i = 0; i < i_limit; i++)
          {
            hdrp->params[w].distribution_maxrgb[i].percentage =
            ni_bs_reader_get_bits(&br, 7);
            hdrp->params[w].distribution_maxrgb[i].percentile.num =
            ni_bs_reader_get_bits(&br, 17);
            hdrp->params[w].distribution_maxrgb[i].percentile.den = 100000;
            ni_log(NI_LOG_TRACE,
                   "hdr10+ distribution_maxrgb_percentage[%d][%d] %u\n",
                   w, i, hdrp->params[w].distribution_maxrgb[i].percentage);
            ni_log(NI_LOG_TRACE,
                   "hdr10+ distribution_maxrgb_percentile[%d][%d] %d\n",
                   w, i, hdrp->params[w].distribution_maxrgb[i].percentile.num);
          }

          hdrp->params[w].fraction_bright_pixels.num = ni_bs_reader_get_bits(
            &br, 10);
          hdrp->params[w].fraction_bright_pixels.den = 1000;
          ni_log(NI_LOG_TRACE, "hdr10+ fraction_bright_pixels[%d] %d\n",
                 w, hdrp->params[w].fraction_bright_pixels.num);
        }

        hdrp->mastering_display_actual_peak_luminance_flag =
        ni_bs_reader_get_bits(&br, 1);
        ni_log(NI_LOG_TRACE,
               "hdr10+ mastering_display_actual_peak_luminance_flag %u\n",
               hdrp->mastering_display_actual_peak_luminance_flag);
        if (hdrp->mastering_display_actual_peak_luminance_flag)
        {
          i_limit = hdrp->num_rows_mastering_display_actual_peak_luminance =
          ni_bs_reader_get_bits(&br, 5);
          j_limit = hdrp->num_cols_mastering_display_actual_peak_luminance =
          ni_bs_reader_get_bits(&br, 5);
          ni_log(NI_LOG_TRACE, "hdr10+ num_rows_mastering_display_actual_peak_"
                 "luminance x num_cols_mastering_display_actual_peak_luminance "
                 "%u x %u\n", i_limit, j_limit);

          i_limit = i_limit > 25 ? 25 : i_limit;
          j_limit = j_limit > 25 ? 25 : j_limit;
          for (i = 0; i < i_limit; i++)
            for (j = 0; j < j_limit; j++)
            {
              hdrp->mastering_display_actual_peak_luminance[i][j].num =
              ni_bs_reader_get_bits(&br, 4);
              hdrp->mastering_display_actual_peak_luminance[i][j].den = 15;
              ni_log(NI_LOG_TRACE, "hdr10+ mastering_display_actual_peak_lumi"
                     "nance[%d][%d] %d\n", i, j,
                     hdrp->mastering_display_actual_peak_luminance[i][j].num);
            }
        }

        for (w = 0; w < hdrp->num_windows; w++)
        {
          hdrp->params[w].tone_mapping_flag = ni_bs_reader_get_bits(&br, 1);
          ni_log(NI_LOG_TRACE, "hdr10+ tone_mapping_flag[%d] %u\n",
                 w, hdrp->params[w].tone_mapping_flag);

          if (hdrp->params[w].tone_mapping_flag)
          {
            hdrp->params[w].knee_point_x.num = ni_bs_reader_get_bits(&br, 12);
            hdrp->params[w].knee_point_x.den = 4095;
            hdrp->params[w].knee_point_y.num = ni_bs_reader_get_bits(&br, 12);
            hdrp->params[w].knee_point_y.den = 4095;
            ni_log(NI_LOG_TRACE, "hdr10+ knee_point_x[%d] %u\n",
                   w, hdrp->params[w].knee_point_x.num);
            ni_log(NI_LOG_TRACE, "hdr10+ knee_point_y[%d] %u\n",
                   w, hdrp->params[w].knee_point_y.num);

            hdrp->params[w].num_bezier_curve_anchors =
            ni_bs_reader_get_bits(&br, 4);
            ni_log(NI_LOG_TRACE, "hdr10+ num_bezier_curve_anchors[%d] %u\n",
                   w, hdrp->params[w].num_bezier_curve_anchors);
            for (i = 0; i < hdrp->params[w].num_bezier_curve_anchors; i++)
            {
              hdrp->params[w].bezier_curve_anchors[i].num =
              ni_bs_reader_get_bits(&br, 10);
              hdrp->params[w].bezier_curve_anchors[i].den = 1023;
              ni_log(NI_LOG_TRACE, "hdr10+ bezier_curve_anchors[%d][%d] %d\n",
                     w, i, hdrp->params[w].bezier_curve_anchors[i].num);
            }
          }

          hdrp->params[w].color_saturation_mapping_flag =
          ni_bs_reader_get_bits(&br, 1);
          ni_log(NI_LOG_TRACE, "hdr10+ color_saturation_mapping_flag[%d] %u\n",
                 w, hdrp->params[w].color_saturation_mapping_flag);
          if (hdrp->params[w].color_saturation_mapping_flag)
          {
            hdrp->params[w].color_saturation_weight.num =
            ni_bs_reader_get_bits(&br, 6);
            hdrp->params[w].color_saturation_weight.den = 8;
            ni_log(NI_LOG_TRACE, "hdr10+ color_saturation_weight[%d] %d\n",
                   w, hdrp->params[w].color_saturation_weight.num);
          }
        } // num_windows

      } // right number of windows
    } // alloc memory
  } // HDR10+ SEI

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
    sei_buf = (uint8_t *)frame->p_data[0] + frame->vui_offset;

    if (NI_LOGAN_CODEC_FORMAT_H265 == frame->src_codec)
    {
      if (sizeof(ni_logan_dec_h265_vui_param_t) == frame->vui_len)
      {
        ni_logan_dec_h265_vui_param_t *vui = (ni_logan_dec_h265_vui_param_t *)sei_buf;
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

        ni_log(NI_LOG_TRACE, "ni_logan_dec_retrieve_aux_data H.265 VUI "
               "aspect_ratio_info_present_flag %u aspect_ratio_idc %u "
               "sar_width %u sar_height %u "
               "video_signal_type_present_flag %u video_format %d "
               "video_full_range_flag %u colour_description_present_flag %u "
               "color-pri %u color-trc %u color-space %u "
               "vui_timing_info_present_flag %u vui_num_units_in_tick %u "
               "vui_time_scale %u\n",
               vui->aspect_ratio_info_present_flag, frame->aspect_ratio_idc,
               frame->sar_width, frame->sar_height,
               vui->video_signal_type_present_flag, vui->video_format,
               frame->video_full_range_flag,
               vui->colour_description_present_flag,
               frame->color_primaries, frame->color_trc, frame->color_space,
               vui->vui_timing_info_present_flag,
               frame->vui_num_units_in_tick, frame->vui_time_scale);
      }
      else
      {
        ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data VUI, expecting H.265 "
               "VUI struct size %d, got %u, dropped!\n",
               (int)sizeof(ni_logan_dec_h265_vui_param_t), frame->vui_len);
      }
    }
    else if (NI_LOGAN_CODEC_FORMAT_H264 == frame->src_codec)
    {
      if (sizeof(ni_logan_dec_h264_vui_param_t) == frame->vui_len)
      {
        ni_logan_dec_h264_vui_param_t *vui = (ni_logan_dec_h264_vui_param_t *)sei_buf;
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

        ni_log(NI_LOG_TRACE, "ni_logan_dec_retrieve_aux_data H.264 VUI "
               "aspect_ratio_info_present_flag %u aspect_ratio_idc %u "
               "sar_width %u sar_height %u "
               "video_signal_type_present_flag %u video_format %d "
               "video_full_range_flag %u colour_description_present_flag %u "
               "color-pri %u color-trc %u color-space %u "
               "vui_timing_info_present_flag %u vui_num_units_in_tick %u "
               "vui_time_scale %u pic_struct_present_flag %u\n",
               vui->aspect_ratio_info_present_flag, frame->aspect_ratio_idc,
               frame->sar_width, frame->sar_height,
               vui->video_signal_type_present_flag, vui->video_format,
               frame->video_full_range_flag,
               vui->colour_description_present_flag,
               frame->color_primaries, frame->color_trc, frame->color_space,
               vui->vui_timing_info_present_flag,
               frame->vui_num_units_in_tick, frame->vui_time_scale,
               vui->pic_struct_present_flag);
      }
      else
      {
        ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data VUI, expecting H.264 "
               "VUI struct size %d, got %u, dropped!\n",
               (int)sizeof(ni_logan_dec_h264_vui_param_t), frame->vui_len);
      }
    }
    else
    {
      ni_log(NI_LOG_ERROR, "ni_logan_dec_retrieve_aux_data VUI, unsupported codec: "
             "%d, dropped\n", frame->src_codec);
    }
  }

  // alternative transfer characteristics SEI if available
  if (frame->sei_alt_transfer_characteristics_len &&
      frame->sei_alt_transfer_characteristics_offset)
  {
    sei_buf = (uint8_t *)frame->p_data[0] +
    frame->sei_alt_transfer_characteristics_offset;

    // and overwrite the color-trc in the VUI
    ni_log(NI_LOG_TRACE, "ni_logan_dec_retrieve_aux_data alt trc SEI value %u over-"
           "writting VUI color-trc value %u\n", *sei_buf, frame->color_trc);
    frame->color_trc = *sei_buf;
  }
}

// internal function to convert struct of ROIs to NetInt ROI map and store
// them inside the encoder context passed in.
// return 0 if successful, -1 otherwise
static int set_roi_map(ni_logan_session_context_t* p_enc_ctx,
                       ni_logan_codec_format_t codec_format,
                       const ni_aux_data_t *aux_data,
                       int nb_roi, int width, int height, int intra_qp)
{
  int i, j, r, ctu;
  const ni_region_of_interest_t *roi =
  (const ni_region_of_interest_t*)aux_data->data;
  uint32_t self_size = roi->self_size;
  uint8_t set_qp = 0;
  float f_value;

  if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
  {
    // roi for H.264 is specified for 16x16 pixel macroblocks - 1 MB
    // is stored in each custom map entry.
    // number of MBs in each row/column
    int mbWidth = (width + 16 - 1) >> 4;
    int mbHeight = (height + 16 - 1) >> 4;
    int numMbs = mbWidth * mbHeight;
    int customMapSize = sizeof(ni_logan_enc_avc_roi_custom_map_t) * numMbs;
    // make the QP map size 16-aligned to meet VPU requirement for subsequent
    // SEI due to layout of data sent to encoder
    customMapSize = ((customMapSize + 15) / 16) * 16;

    if (! p_enc_ctx->avc_roi_map)
    {
      p_enc_ctx->avc_roi_map =
      (ni_logan_enc_avc_roi_custom_map_t*)malloc(customMapSize);
      if (! p_enc_ctx->avc_roi_map)
      {
        ni_log(NI_LOG_ERROR, "Error set_roi_map malloc failed.\n");
        return -1;
      }
    }

    // init to range midpoint
    memset(p_enc_ctx->avc_roi_map, 0, customMapSize);
    for (i = 0; i < numMbs; i++)
    {
      p_enc_ctx->avc_roi_map[i].field.mb_qp = NI_LOGAN_QP_MID_POINT;
    }

    // iterate ROI list from the last as regions are defined in order of
    // decreasing importance.
    for (r = nb_roi - 1; r >= 0; r--)
    {
      roi = (const ni_region_of_interest_t*)(aux_data->data + self_size * r);
      if (! roi->qoffset.den)
      {
        ni_log(NI_LOG_ERROR, "ni_region_of_interest_t.qoffset.den "
               "must not be zero.\n");
        continue;
      }

      f_value = roi->qoffset.num * 1.0f / roi->qoffset.den;
      f_value = clip3f(-1.0, 1.0, f_value);

      set_qp = (int)(f_value * NI_LOGAN_INTRA_QP_RANGE) + NI_LOGAN_QP_MID_POINT;
      ni_log(NI_LOG_TRACE, "set_roi_map roi %d top %d bot %d left %d "
             "right %d offset %d/%d set_qp %d\n", r, roi->top, roi->bottom,
             roi->left, roi->right, roi->qoffset.num, roi->qoffset.den, set_qp);

      // copy ROI MBs QPs into custom map
      for (j = 0; j < mbHeight; j++)
        for (i = 0; i < mbWidth; i++)
        {
          if (((int)(i % mbWidth) >= (int)((roi->left + 15) / 16) - 1) &&
              ((int)(i % mbWidth) <= (int)((roi->right + 15) / 16) - 1) &&
              ((int)(j % mbHeight) >= (int)((roi->top + 15) / 16) - 1) &&
              ((int)(j % mbHeight) <= (int)((roi->bottom + 15) / 16) - 1))
          {
            p_enc_ctx->avc_roi_map[i + j * mbWidth].field.mb_qp = set_qp;
          }
        }
    } // for each roi

    // average qp is set to midpoint of qp range to work with qp offset
    p_enc_ctx->roi_len = customMapSize;
    p_enc_ctx->roi_avg_qp = NI_LOGAN_QP_MID_POINT;
  }
  else if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
  {
    // ROI for H.265 is specified for 32x32 pixel subCTU blocks -
    // 4 subCTU QPs are stored in each custom CTU map entry.
    // number of CTUs/sub CTUs in each row/column
    int ctuWidth = (width + 64 - 1) >> 6;
    int ctuHeight = (height + 64 - 1) >> 6;
    int subCtuWidth = ctuWidth * 2;
    int subCtuHeight = ctuHeight * 2;
    int numSubCtus = subCtuWidth * subCtuHeight;
    int customMapSize = sizeof(ni_logan_enc_hevc_roi_custom_map_t) *
    ctuWidth * ctuHeight;
    customMapSize = ((customMapSize + 15) / 16) * 16;

    if (! p_enc_ctx->hevc_sub_ctu_roi_buf)
    {
      p_enc_ctx->hevc_sub_ctu_roi_buf = (uint8_t *)malloc(numSubCtus);
      if (! p_enc_ctx->hevc_sub_ctu_roi_buf)
      {
        ni_log(NI_LOG_ERROR, "Error set_roi_map malloc failed.\n");
        return -1;
      }
    }

    if (! p_enc_ctx->hevc_roi_map)
    {
      p_enc_ctx->hevc_roi_map =
      (ni_logan_enc_hevc_roi_custom_map_t *)malloc(customMapSize);
      if (! p_enc_ctx->hevc_roi_map)
      {
        free(p_enc_ctx->hevc_sub_ctu_roi_buf);
        p_enc_ctx->hevc_sub_ctu_roi_buf = NULL;
        ni_log(NI_LOG_ERROR, "Error set_roi_map malloc 2 failed.\n");
        return -1;
      }
    }

    // init to range midpoint
    memset(p_enc_ctx->hevc_roi_map, 0, customMapSize);
    memset(p_enc_ctx->hevc_sub_ctu_roi_buf, NI_LOGAN_QP_MID_POINT, numSubCtus);
    for (r = nb_roi - 1; r >= 0; r--)
    {
      roi = (const ni_region_of_interest_t*)(aux_data->data + self_size * r);
      if (! roi->qoffset.den)
      {
        ni_log(NI_LOG_ERROR, "ni_region_of_interest_t.qoffset.den "
               "must not be zero.\n");
        continue;
      }

      f_value = roi->qoffset.num * 1.0f / roi->qoffset.den;
      f_value = clip3f(-1.0, 1.0, f_value);

      set_qp = (int)(f_value * NI_LOGAN_INTRA_QP_RANGE) + NI_LOGAN_QP_MID_POINT;
      ni_log(NI_LOG_TRACE, "set_roi_map roi %d top %d bot %d left %d "
             "right %d offset %d/%d set_qp %d\n", r, roi->top, roi->bottom,
             roi->left, roi->right, roi->qoffset.num, roi->qoffset.den, set_qp);

      for (j = 0; j < subCtuHeight; j++)
        for (i = 0; i < subCtuWidth; i++)
        {
          if (((int)(i % subCtuWidth) >= (int)((roi->left + 31) / 32) - 1) &&
              ((int)(i % subCtuWidth) <= (int)((roi->right + 31) / 32) - 1) &&
              ((int)(j % subCtuHeight) >= (int)((roi->top + 31) / 32) - 1) &&
              ((int)(j % subCtuHeight) <= (int)((roi->bottom + 31) / 32) - 1))
          {
            p_enc_ctx->hevc_sub_ctu_roi_buf[i + j * subCtuWidth] = set_qp;
          }
        }
    } // for each roi

    // load into final custom map and calculate average qp
    for (i = 0; i < ctuHeight; i++)
    {
      uint8_t *ptr = &p_enc_ctx->hevc_sub_ctu_roi_buf[subCtuWidth * i * 2];
      for (j = 0; j < ctuWidth; j++, ptr += 2)
      {
        ctu = i * ctuWidth + j;
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_0 = *ptr;
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_1 = *(ptr + 1);
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_2 = *(ptr + subCtuWidth);
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_3 = *(ptr + subCtuWidth + 1);
      }
    }
    // average qp is set to midpoint of qp range to work with qp offset
    p_enc_ctx->roi_len = customMapSize;
    p_enc_ctx->roi_avg_qp = NI_LOGAN_QP_MID_POINT;
  }
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
void ni_logan_enc_prep_aux_data(ni_logan_session_context_t* p_enc_ctx,
                          ni_logan_frame_t *p_enc_frame,
                          ni_logan_frame_t *p_dec_frame,
                          ni_logan_codec_format_t codec_format,
                          int should_send_sei_with_frame,
                          uint8_t *mdcv_data,
                          uint8_t *cll_data,
                          uint8_t *cc_data,
                          uint8_t *udu_data,
                          uint8_t *hdrp_data)
{
  uint8_t *dst = NULL;
  ni_aux_data_t *aux_data = NULL;
  ni_logan_encoder_params_t *api_params =
  (ni_logan_encoder_params_t *)p_enc_ctx->p_session_config;

  // reset all auxiliary data flag and length of encode frame
  p_enc_frame->preferred_characteristics_data_len
  = p_enc_frame->use_cur_src_as_long_term_pic
  = p_enc_frame->use_long_term_ref = 0;

  p_enc_frame->sei_total_len
  = p_enc_frame->sei_cc_offset = p_enc_frame->sei_cc_len
  = p_enc_frame->sei_hdr_mastering_display_color_vol_offset
  = p_enc_frame->sei_hdr_mastering_display_color_vol_len
  = p_enc_frame->sei_hdr_content_light_level_info_offset
  = p_enc_frame->sei_hdr_content_light_level_info_len
  = p_enc_frame->sei_user_data_unreg_offset
  = p_enc_frame->sei_user_data_unreg_len
  = p_enc_frame->sei_hdr_plus_offset
  = p_enc_frame->sei_hdr_plus_len = 0;

  // prep SEI for HDR (mastering display color volume)
  aux_data = ni_logan_frame_get_aux_data(
    p_dec_frame, NI_FRAME_AUX_DATA_MASTERING_DISPLAY_METADATA);
  if (aux_data)
  {
    p_enc_ctx->mdcv_max_min_lum_data_len = 8;
    p_enc_ctx->sei_hdr_mastering_display_color_vol_len =
    8 + 6*2 + 2*2 + 2*4 + 1;
    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      p_enc_ctx->sei_hdr_mastering_display_color_vol_len--;
    }

    ni_mastering_display_metadata_t *p_src =
    (ni_mastering_display_metadata_t*)aux_data->data;

    // save a copy
    if (! p_enc_ctx->p_master_display_meta_data)
    {
      p_enc_ctx->p_master_display_meta_data =
      malloc(sizeof(ni_mastering_display_metadata_t));
    }
    if (! p_enc_ctx->p_master_display_meta_data)
    {
      ni_log(NI_LOG_ERROR, "Error mem alloc for mastering display color vol\n");
    }
    else
    {
      memcpy(p_enc_ctx->p_master_display_meta_data, p_src,
             sizeof(ni_mastering_display_metadata_t));

      const int luma_den   = 10000;

      uint32_t uint32_t_tmp = htonl((uint32_t)(lrint(luma_den * ni_q2d(
                                                       p_src->max_luminance))));
      memcpy(p_enc_ctx->ui8_mdcv_max_min_lum_data,
             &uint32_t_tmp, sizeof(uint32_t));
      uint32_t_tmp = htonl((uint32_t)(lrint(luma_den * ni_q2d(
                                              p_src->min_luminance))));
      memcpy(p_enc_ctx->ui8_mdcv_max_min_lum_data + 4,
             &uint32_t_tmp, sizeof(uint32_t));

      // emulation prevention checking of luminance data
      int emu_bytes_inserted = ni_logan_insert_emulation_prevent_bytes(
        p_enc_ctx->ui8_mdcv_max_min_lum_data, 2*4);

      p_enc_ctx->mdcv_max_min_lum_data_len += emu_bytes_inserted;
      p_enc_ctx->sei_hdr_mastering_display_color_vol_len += emu_bytes_inserted;
    }
  }

  if (p_enc_ctx->sei_hdr_mastering_display_color_vol_len &&
      should_send_sei_with_frame)
  {
    dst = mdcv_data;
    dst[0] = dst[1] = dst[2] = 0;
    dst[3] = 1;

    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      dst[4] = 0x6;
      dst[5] = 0x89;  // payload type=137
      dst[6] = 0x18;  // payload size=24
      dst += 7;
    }
    else
    {
      dst[4] = 0x4e;
      dst[5] = 1;
      dst[6] = 0x89;  // payload type=137
      dst[7] = 0x18;  // payload size=24
      dst += 8;
    }

    ni_logan_enc_mastering_display_colour_volume_t *p_mdcv =
    (ni_logan_enc_mastering_display_colour_volume_t*)dst;
    ni_mastering_display_metadata_t *p_src = (ni_mastering_display_metadata_t *)
    p_enc_ctx->p_master_display_meta_data;

    const int chroma_den = 50000;
    const int luma_den   = 10000;

    uint16_t dp00 = 0, dp01 = 0, dp10 = 0, dp11 = 0, dp20 = 0, dp21 = 0,
    wpx = 0, wpy = 0;
    // assuming p_src->has_primaries is always true
    // this is stored in r,g,b order and needs to be in g.b,r order
    // when sent to encoder
    dp00 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[1][0]));
    p_mdcv->display_primaries[0][0] = htons(dp00);
    dp01 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[1][1]));
    p_mdcv->display_primaries[0][1] = htons(dp01);
    dp10 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[2][0]));
    p_mdcv->display_primaries[1][0] = htons(dp10);
    dp11 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[2][1]));
    p_mdcv->display_primaries[1][1] = htons(dp11);
    dp20 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[0][0]));
    p_mdcv->display_primaries[2][0] = htons(dp20);
    dp21 = (uint16_t)lrint(chroma_den * ni_q2d(p_src->display_primaries[0][1]));
    p_mdcv->display_primaries[2][1] = htons(dp21);

    wpx = (uint16_t)lrint(chroma_den * ni_q2d(p_src->white_point[0]));
    p_mdcv->white_point_x = htons(wpx);
    wpy = (uint16_t)lrint(chroma_den * ni_q2d(p_src->white_point[1]));
    p_mdcv->white_point_y = htons(wpy);

    ni_log(NI_LOG_TRACE, "mastering display color volume, primaries "
           "%u/%u/%u/%u/%u/%u white_point_x/y %u/%u max/min_lumi %u/%u\n",
           (uint16_t)dp00, (uint16_t)dp01, (uint16_t)dp10,
           (uint16_t)dp11, (uint16_t)dp20, (uint16_t)dp21,
           (uint16_t)wpx,  (uint16_t)wpy,
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
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame,
                                   NI_FRAME_AUX_DATA_CONTENT_LIGHT_LEVEL);
  if (aux_data)
  {
    // size of: start code + NAL unit header + payload type byte +
    //          payload size byte + payload + rbsp trailing bits, default HEVC
    p_enc_ctx->light_level_data_len = 4;
    p_enc_ctx->sei_hdr_content_light_level_info_len = 8 + 2*2 + 1;
    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      p_enc_ctx->sei_hdr_content_light_level_info_len--;
    }

    uint16_t max_content_light_level =
    htons(((ni_content_light_level_t*)aux_data->data)->max_cll);
    uint16_t max_pic_average_light_level =
    htons(((ni_content_light_level_t *)aux_data->data)->max_fall);

    ni_log(NI_LOG_TRACE, "content light level info, MaxCLL %u MaxFALL %u\n",
           ((ni_content_light_level_t*)aux_data->data)->max_cll,
           ((ni_content_light_level_t*)aux_data->data)->max_fall);

    memcpy(p_enc_ctx->ui8_light_level_data,
           &max_content_light_level, sizeof(uint16_t));
    memcpy(&(p_enc_ctx->ui8_light_level_data[2]),
           &max_pic_average_light_level, sizeof(uint16_t));

    // emulation prevention checking
    int emu_bytes_inserted = ni_logan_insert_emulation_prevent_bytes(
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

    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      dst[4] = 0x6;
      dst[5] = 0x90;  // payload type=144
      dst[6] = 4;     // payload size=4
      dst += 7;
    }
    else
    {
      dst[4] = 0x4e;
      dst[5] = 1;
      dst[6] = 0x90;  // payload type=144
      dst[7] = 4;     // payload size=4
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
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_A53_CC);
  if (aux_data)
  {
    ni_log(NI_LOG_TRACE, "ni_logan_enc_prep_aux_data sei_cc_len %u\n",
           aux_data->size);

    uint8_t cc_data_emu_prevent[NI_LOGAN_MAX_SEI_DATA];
    int cc_size = aux_data->size;
    if (cc_size > NI_LOGAN_MAX_SEI_DATA)
    {
      ni_log(NI_LOG_ERROR, "ni_logan_enc_prep_aux_data sei_cc_len %u > MAX %d !\n",
             aux_data->size, (int)NI_LOGAN_MAX_SEI_DATA);
      cc_size = NI_LOGAN_MAX_SEI_DATA;
    }
    memcpy(cc_data_emu_prevent, aux_data->data, cc_size);
    int cc_size_emu_prevent = cc_size + ni_logan_insert_emulation_prevent_bytes(
      cc_data_emu_prevent, cc_size);
    if (cc_size_emu_prevent != cc_size)
    {
      ni_log(NI_LOG_TRACE, "ni_logan_enc_prep_aux_data: close caption "
             "emulation prevention bytes added: %d\n",
             cc_size_emu_prevent - cc_size);
    }

    dst = cc_data;
    // set header info fields and extra size based on codec
    if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
    {
      p_enc_frame->sei_cc_len =
      NI_CC_SEI_HDR_HEVC_LEN + cc_size_emu_prevent + NI_CC_SEI_TRAILER_LEN;
      p_enc_frame->sei_total_len += p_enc_frame->sei_cc_len;

      p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc[7] = cc_size + 11;
      p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc[16] = (cc_size / 3) | 0xc0;

      memcpy(dst, p_enc_ctx->itu_t_t35_cc_sei_hdr_hevc, NI_CC_SEI_HDR_HEVC_LEN);
      dst += NI_CC_SEI_HDR_HEVC_LEN;
      memcpy(dst, cc_data_emu_prevent, cc_size_emu_prevent);
      dst += cc_size_emu_prevent;
      memcpy(dst, p_enc_ctx->sei_trailer, NI_CC_SEI_TRAILER_LEN);
    }
    else // H.264
    {
      p_enc_frame->sei_cc_len =
      NI_CC_SEI_HDR_H264_LEN + cc_size_emu_prevent + NI_CC_SEI_TRAILER_LEN;
      p_enc_frame->sei_total_len += p_enc_frame->sei_cc_len;

      p_enc_ctx->itu_t_t35_cc_sei_hdr_h264[6] = cc_size + 11;
      p_enc_ctx->itu_t_t35_cc_sei_hdr_h264[15] = (cc_size / 3) | 0xc0;

      memcpy(dst, p_enc_ctx->itu_t_t35_cc_sei_hdr_h264, NI_CC_SEI_HDR_H264_LEN);
      dst += NI_CC_SEI_HDR_H264_LEN;
      memcpy(dst, cc_data_emu_prevent, cc_size_emu_prevent);
      dst += cc_size_emu_prevent;
      memcpy(dst, p_enc_ctx->sei_trailer, NI_CC_SEI_TRAILER_LEN);
    }
  }

  // prep SEI for HDR+
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_HDR_PLUS);
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
    ni_bs_writer_put(&pb, 0x3c, 8); // u16 itu_t_t35_provider_code = 0x003c
    ni_bs_writer_put(&pb, 0, 8);
    // u16 itu_t_t35_provider_oriented_code = 0x0001
    ni_bs_writer_put(&pb, 0x01, 8);
    ni_bs_writer_put(&pb, 4, 8); // u8 application_identifier = 0x04
    ni_bs_writer_put(&pb, 0, 8); // u8 application version = 0x00

    ni_bs_writer_put(&pb, hdrp->num_windows, 2);
    ni_log(NI_LOG_TRACE, "hdr10+ num_windows %u\n", hdrp->num_windows);
    for (w = 1; w < hdrp->num_windows; w++)
    {
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].window_upper_left_corner_x.num, 16);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].window_upper_left_corner_y.num, 16);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].window_lower_right_corner_x.num, 16);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].window_lower_right_corner_y.num, 16);
      ni_bs_writer_put(&pb, hdrp->params[w - 1].center_of_ellipse_x, 16);
      ni_bs_writer_put(&pb, hdrp->params[w - 1].center_of_ellipse_y, 16);
      ni_bs_writer_put(&pb, hdrp->params[w - 1].rotation_angle, 8);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].semimajor_axis_internal_ellipse, 16);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].semimajor_axis_external_ellipse, 16);
      ni_bs_writer_put(&pb,
                       hdrp->params[w - 1].semiminor_axis_external_ellipse, 16);
      ni_bs_writer_put(&pb, hdrp->params[w - 1].overlap_process_option, 1);
    }

    // values are scaled up according to standard spec
    ui_tmp = lrint(10000 *
                   ni_q2d(hdrp->targeted_system_display_maximum_luminance));
    ni_bs_writer_put(&pb, ui_tmp, 27);
    ni_bs_writer_put(&pb,
                     hdrp->targeted_system_display_actual_peak_luminance_flag,
                     1);
    ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_maximum_luminance "
           "%d\n", ui_tmp);
    ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_actual_peak_luminance_"
           "flag %u\n", hdrp->targeted_system_display_actual_peak_luminance_flag);

    if (hdrp->targeted_system_display_actual_peak_luminance_flag)
    {
      ni_bs_writer_put(&pb,
               hdrp->num_rows_targeted_system_display_actual_peak_luminance, 5);
      ni_bs_writer_put(&pb,
               hdrp->num_cols_targeted_system_display_actual_peak_luminance, 5);
      ni_log(NI_LOG_TRACE, "hdr10+ num_rows_targeted_system_display_actual_peak_luminance x num_cols_targeted_system_display_actual_peak_luminance %u x %u\n", 
             hdrp->num_rows_targeted_system_display_actual_peak_luminance,
             hdrp->num_cols_targeted_system_display_actual_peak_luminance);

      for (i = 0;
           i < hdrp->num_rows_targeted_system_display_actual_peak_luminance;
           i++)
        for (j = 0;
             j < hdrp->num_cols_targeted_system_display_actual_peak_luminance;
             j++)
        {
          ui_tmp = lrint(15 * ni_q2d(hdrp->targeted_system_display_actual_peak_luminance[i][j]));
          ni_bs_writer_put(&pb, ui_tmp, 4);
          ni_log(NI_LOG_TRACE, "hdr10+ targeted_system_display_actual_peak_"
                 "luminance[%d][%d] %d\n", i, j, ui_tmp);
        }
    }

    for (w = 0; w < hdrp->num_windows; w++)
    {
      for (i = 0; i < 3; i++)
      {
        ui_tmp = lrint(100000 * ni_q2d(hdrp->params[w].maxscl[i]));
        ni_bs_writer_put(&pb, ui_tmp, 17);
        ni_log(NI_LOG_TRACE, "hdr10+ maxscl[%d][%d] %d\n", w, i,
               ui_tmp);
      }
      ui_tmp = lrint(100000 * ni_q2d(hdrp->params[w].average_maxrgb));
      ni_bs_writer_put(&pb, ui_tmp, 17);
      ni_log(NI_LOG_TRACE, "hdr10+ average_maxrgb[%d] %d\n",
             w, ui_tmp);

      ni_bs_writer_put(&pb,
                       hdrp->params[w].num_distribution_maxrgb_percentiles, 4);
      ni_log(NI_LOG_TRACE,
             "hdr10+ num_distribution_maxrgb_percentiles[%d] %d\n",
             w, hdrp->params[w].num_distribution_maxrgb_percentiles);

      for (i = 0; i < hdrp->params[w].num_distribution_maxrgb_percentiles; i++)
      {
        ni_bs_writer_put(&pb,
                         hdrp->params[w].distribution_maxrgb[i].percentage, 7);
        ui_tmp = lrint(100000 * ni_q2d(hdrp->params[w].distribution_maxrgb[i].percentile));
        ni_bs_writer_put(&pb, ui_tmp, 17);
        ni_log(NI_LOG_TRACE,
               "hdr10+ distribution_maxrgb_percentage[%d][%d] %u\n",
               w, i, hdrp->params[w].distribution_maxrgb[i].percentage);
        ni_log(NI_LOG_TRACE,
               "hdr10+ distribution_maxrgb_percentile[%d][%d] %d\n",
               w, i, ui_tmp);
      }

      ui_tmp = lrint(1000 * ni_q2d(hdrp->params[w].fraction_bright_pixels));
      ni_bs_writer_put(&pb, ui_tmp, 10);
      ni_log(NI_LOG_TRACE, "hdr10+ fraction_bright_pixels[%d] %d\n",
             w, ui_tmp);
    }

    ni_bs_writer_put(&pb,
                     hdrp->mastering_display_actual_peak_luminance_flag, 1);
    ni_log(NI_LOG_TRACE,
           "hdr10+ mastering_display_actual_peak_luminance_flag %u\n",
           hdrp->mastering_display_actual_peak_luminance_flag);
    if (hdrp->mastering_display_actual_peak_luminance_flag)
    {
      ni_bs_writer_put(&pb,
                       hdrp->num_rows_mastering_display_actual_peak_luminance,
                       5);
      ni_bs_writer_put(&pb,
                       hdrp->num_cols_mastering_display_actual_peak_luminance,
                       5);
      ni_log(NI_LOG_TRACE, "hdr10+ num_rows_mastering_display_actual_peak_luminance x num_cols_mastering_display_actual_peak_luminance %u x %u\n", 
             hdrp->num_rows_mastering_display_actual_peak_luminance,
             hdrp->num_cols_mastering_display_actual_peak_luminance);

      for (i = 0;
           i < hdrp->num_rows_mastering_display_actual_peak_luminance; i++)
        for (j = 0;
             j < hdrp->num_cols_mastering_display_actual_peak_luminance; j++)
        {
          ui_tmp = lrint(15 * ni_q2d(hdrp->mastering_display_actual_peak_luminance[i][j]));
          ni_bs_writer_put(&pb, ui_tmp, 4);
          ni_log(NI_LOG_TRACE, "hdr10+ mastering_display_actual_peak_luminance[%d][%d] %d\n", i, j, ui_tmp);
        }
    }

    for (w = 0; w < hdrp->num_windows; w++)
    {
      ni_bs_writer_put(&pb, hdrp->params[w].tone_mapping_flag, 1);
      ni_log(NI_LOG_TRACE, "hdr10+ tone_mapping_flag[%d] %u\n",
             w, hdrp->params[w].tone_mapping_flag);

      if (hdrp->params[w].tone_mapping_flag)
      {
        ui_tmp = lrint(4095 * ni_q2d(hdrp->params[w].knee_point_x));
        ni_bs_writer_put(&pb, ui_tmp, 12);
        ni_log(NI_LOG_TRACE, "hdr10+ knee_point_x[%d] %u\n",
               w, ui_tmp);

        ui_tmp = lrint(4095 * ni_q2d(hdrp->params[w].knee_point_y));
        ni_bs_writer_put(&pb, ui_tmp, 12);
        ni_log(NI_LOG_TRACE, "hdr10+ knee_point_y[%d] %u\n",
               w, ui_tmp);

        ni_bs_writer_put(&pb, hdrp->params[w].num_bezier_curve_anchors, 4);
        ni_log(NI_LOG_TRACE,
               "hdr10+ num_bezier_curve_anchors[%d] %u\n",
               w, hdrp->params[w].num_bezier_curve_anchors);
        for (i = 0; i < hdrp->params[w].num_bezier_curve_anchors; i++)
        {
          ui_tmp = lrint(1023 * ni_q2d(hdrp->params[w].bezier_curve_anchors[i]));
          ni_bs_writer_put(&pb, ui_tmp, 10);
          ni_log(NI_LOG_TRACE,
                 "hdr10+ bezier_curve_anchors[%d][%d] %d\n", w, i, ui_tmp);
        }
      }

      ni_bs_writer_put(&pb, hdrp->params[w].color_saturation_mapping_flag, 1);
      ni_log(NI_LOG_TRACE,
             "hdr10+ color_saturation_mapping_flag[%d] %u\n",
             w, hdrp->params[w].color_saturation_mapping_flag);
      if (hdrp->params[w].color_saturation_mapping_flag)
      {
        ui_tmp = lrint(8 * ni_q2d(hdrp->params[w].color_saturation_weight));
        ni_bs_writer_put(&pb, 6, ui_tmp);
        ni_log(NI_LOG_TRACE, "hdr10+ color_saturation_weight[%d] %d\n",
               w, ui_tmp);
      }
    } // num_windows

    uint32_t hdr10p_num_bytes = (uint32_t)((ni_bs_writer_tell(&pb) + 7) / 8);
    ni_log(NI_LOG_TRACE, "hdr10+ total bits: %d -> bytes %"PRIu64"\n",
           (int)ni_bs_writer_tell(&pb), hdr10p_num_bytes);
    ni_bs_writer_align_zero(&pb);

    dst = hdrp_data;

    // emulation prevention checking of payload
    int emu_bytes_inserted = 0;

    // set header info fields and extra size based on codec
    if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
    {
      p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_hevc[7] =
      hdr10p_num_bytes + NI_RBSP_TRAILING_BITS_LEN;

      memcpy(dst, p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_hevc,
             NI_HDR10P_SEI_HDR_HEVC_LEN);
      dst += NI_HDR10P_SEI_HDR_HEVC_LEN;
      ni_bs_writer_copy(dst, &pb);

      emu_bytes_inserted = ni_logan_insert_emulation_prevent_bytes(
        dst, hdr10p_num_bytes);
      dst += hdr10p_num_bytes + emu_bytes_inserted;
      *dst = p_enc_ctx->sei_trailer[1];
      dst += NI_RBSP_TRAILING_BITS_LEN;

      p_enc_frame->sei_hdr_plus_len = NI_HDR10P_SEI_HDR_HEVC_LEN +
      hdr10p_num_bytes + emu_bytes_inserted + NI_RBSP_TRAILING_BITS_LEN;
      p_enc_frame->sei_total_len += p_enc_frame->sei_hdr_plus_len;
    }
    else if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_h264[6] =
      hdr10p_num_bytes + NI_RBSP_TRAILING_BITS_LEN;

      memcpy(dst, p_enc_ctx->itu_t_t35_hdr10p_sei_hdr_h264,
             NI_HDR10P_SEI_HDR_H264_LEN);
      dst += NI_HDR10P_SEI_HDR_H264_LEN;
      ni_bs_writer_copy(dst, &pb);

      emu_bytes_inserted = ni_logan_insert_emulation_prevent_bytes(
        dst, hdr10p_num_bytes);
      dst += hdr10p_num_bytes + emu_bytes_inserted;
      *dst = p_enc_ctx->sei_trailer[1];
      dst += NI_RBSP_TRAILING_BITS_LEN;

      p_enc_frame->sei_hdr_plus_len = NI_HDR10P_SEI_HDR_H264_LEN +
      hdr10p_num_bytes + emu_bytes_inserted + NI_RBSP_TRAILING_BITS_LEN;
      p_enc_frame->sei_total_len += p_enc_frame->sei_hdr_plus_len;
    }
    else
    {
      ni_log(NI_LOG_ERROR, "ni_logan_enc_prep_aux_data: codec %d not "
             "supported for HDR10+ SEI !\n", codec_format);
      p_enc_frame->sei_hdr_plus_len = 0;
    }

    ni_bs_writer_clear(&pb);
  } // hdr10+

  // prep SEI for User Data Unregistered
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_UDU_SEI);
  if (aux_data)
  {
    ni_log(NI_LOG_TRACE, "ni_logan_enc_prep_aux_data sei_user_data_unreg_len %u\n",
           aux_data->size);

    // emulation prevention checking: a working buffer of size in worst case
    // that each two bytes comes with 1B emulation prevention byte
    int udu_sei_size = aux_data->size;
    int ext_udu_sei_size = 0, sei_len = 0;

    uint8_t *sei_data = malloc(udu_sei_size * 3 / 2);
    if (sei_data)
    {
      memcpy(sei_data, (uint8_t *)aux_data->data, udu_sei_size);
      int emu_bytes_inserted = ni_logan_insert_emulation_prevent_bytes(
        sei_data, udu_sei_size);

      ext_udu_sei_size = udu_sei_size + emu_bytes_inserted;

      if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
      {
        /* 4B long start code + 1B nal header + 1B SEI type + Bytes of
           payload length + Bytes of SEI payload + 1B trailing */
        sei_len = 6 + ((udu_sei_size + 0xFE) / 0xFF) + ext_udu_sei_size + 1;
      }
      else
      {
        /* 4B long start code + 2B nal header + 1B SEI type + Bytes of
           payload length + Bytes of SEI payload + 1B trailing */
        sei_len = 7 + ((udu_sei_size + 0xFE) / 0xFF) + ext_udu_sei_size + 1;
      }

      // discard this UDU SEI if the total SEI size exceeds the max size
      if (p_enc_frame->sei_total_len + sei_len > NI_LOGAN_ENC_MAX_SEI_BUF_SIZE)
      {
        ni_log(NI_LOG_ERROR, "ni_logan_enc_prep_aux_data sei total length %u + "
               "sei_len %d exceeds maximum sei size %u, discarding it !\n",
               p_enc_frame->sei_total_len, sei_len, NI_LOGAN_ENC_MAX_SEI_BUF_SIZE);
      }
      else
      {
        int payload_size = udu_sei_size;

        dst = udu_data;
        *dst++ = 0x00;   // long start code
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x01;

        if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
        {
          *dst++ = 0x06;   // nal type: SEI
        }
        else
        {
          *dst++ = 0x4e;   // nal type: SEI
          *dst++ = 0x01;
        }
        *dst++ = 0x05;     // SEI type: user data unregistered

        // original payload size
        while (payload_size > 0)
        {
          *dst++ = (payload_size > 0xFF ? 0xFF : (uint8_t)payload_size);
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

  // supply QP map if ROI enabled and if ROIs passed in
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame,
                                   NI_FRAME_AUX_DATA_REGIONS_OF_INTEREST);
  if (api_params->hevc_enc_params.roi_enable && aux_data)
  {
    int is_new_rois = 1;
    const ni_region_of_interest_t *roi = NULL;
    uint32_t self_size = 0;

    roi = (const ni_region_of_interest_t*)aux_data->data;
    self_size = roi->self_size;
    if (! self_size || aux_data->size % self_size)
    {
      ni_log(NI_LOG_ERROR, "Invalid ni_region_of_interest_t.self_size, "
             "aux_data size %d self_size %u\n", aux_data->size, self_size);
    }
    else
    {
      int nb_roi = aux_data->size / self_size;

      // update ROI(s) if new/different from last one
      if (0 == p_enc_ctx->nb_rois || 0 == p_enc_ctx->roi_side_data_size ||
          ! p_enc_ctx->av_rois || p_enc_ctx->nb_rois != nb_roi ||
          p_enc_ctx->roi_side_data_size != aux_data->size ||
          memcmp(p_enc_ctx->av_rois, aux_data->data, aux_data->size))
      {
        p_enc_ctx->roi_side_data_size = aux_data->size;
        p_enc_ctx->nb_rois = nb_roi;

        free(p_enc_ctx->av_rois);
        p_enc_ctx->av_rois = malloc(aux_data->size);
        if (! p_enc_ctx->av_rois)
        {
          ni_log(NI_LOG_ERROR, "malloc ROI aux_data failed.\n");
          is_new_rois = 0;
        }
        else
        {
          memcpy(p_enc_ctx->av_rois, aux_data->data, aux_data->size);
        }
      }
      else
      {
        is_new_rois = 0;
      }

      if (is_new_rois)
      {
        if (set_roi_map(p_enc_ctx, codec_format, aux_data, nb_roi,
                        api_params->source_width, api_params->source_height,
                        api_params->hevc_enc_params.rc.intra_qp))
        {
          ni_log(NI_LOG_ERROR, "set_roi_map failed\n");
        }
      }
    }

    // ROI data in the frame
    p_enc_frame->extra_data_len += p_enc_ctx->roi_len;
    p_enc_frame->roi_len = p_enc_ctx->roi_len;
  }

  // if ROI cache is enabled, supply cached QP map if no ROI aux data is
  // passed in with this frame
  if (api_params->hevc_enc_params.roi_enable && ! aux_data &&
      api_params->cacheRoi)
  {
    p_enc_frame->extra_data_len += p_enc_ctx->roi_len;
    p_enc_frame->roi_len = p_enc_ctx->roi_len;

    ni_log(NI_LOG_TRACE, "ni_logan_enc_prep_aux_data: supply cached QP map.\n");
  }

  // prep for NetInt long term reference frame support
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame,NI_FRAME_AUX_DATA_LONG_TERM_REF);
  if (aux_data)
  {
    ni_long_term_ref_t *ltr = (ni_long_term_ref_t *)aux_data->data;

    p_enc_frame->use_cur_src_as_long_term_pic =
    ltr->use_cur_src_as_long_term_pic;
    p_enc_frame->use_long_term_ref = ltr->use_long_term_ref;
  }

  // prep for NetInt target bitrate reconfiguration support
  aux_data = ni_logan_frame_get_aux_data(p_dec_frame, NI_FRAME_AUX_DATA_BITRATE);
  if (aux_data)
  {
    int32_t bitrate = *((int32_t *)aux_data->data);
    if (! p_enc_ctx->enc_change_params)
    {
      p_enc_ctx->enc_change_params =
      calloc(1, sizeof(ni_logan_encoder_change_params_t));
    }

    if (! p_enc_ctx->enc_change_params)
    {
      ni_log(NI_LOG_ERROR, "Error ni_logan_enc_prep_aux_data malloc for "
             "enc_change_params!\n");
    }
    else
    {
      p_enc_ctx->enc_change_params->enable_option |=
      NI_LOGAN_SET_CHANGE_PARAM_RC_TARGET_RATE;
      p_enc_ctx->enc_change_params->bitRate = bitrate;
      if (p_enc_frame->reconf_len == 0)
      {
        p_enc_frame->reconf_len = sizeof(ni_logan_encoder_change_params_t);
        p_enc_frame->extra_data_len += p_enc_frame->reconf_len;
      }
    }
  }

  // prep for alternative preferred transfer characteristics SEI
  if (api_params->hevc_enc_params.preferred_transfer_characteristics >= 0 &&
      should_send_sei_with_frame)
  {
    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format)
    {
      p_enc_frame->preferred_characteristics_data_len = 9;
    }
    else
    {
      p_enc_frame->preferred_characteristics_data_len = 10;
    }

    p_enc_ctx->preferred_characteristics_data =
    (uint8_t)api_params->hevc_enc_params.preferred_transfer_characteristics;
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
 *
 *  \return NONE
 ******************************************************************************/
void ni_logan_enc_copy_aux_data(ni_logan_session_context_t* p_enc_ctx,
                          ni_logan_frame_t *p_enc_frame,
                          ni_logan_frame_t *p_dec_frame,
                          ni_logan_codec_format_t codec_format,
                          const uint8_t *mdcv_data,
                          const uint8_t *cll_data,
                          const uint8_t *cc_data,
                          const uint8_t *udu_data,
                          const uint8_t *hdrp_data)
{
  // fill in extra data  (skipping meta data header)
  // Note: this handles both regular YUV layout and YUVbypass cases, while
  //       the later has no YUV but hwframe_surface_t as actual data and its
  //       size stored in data_len[3]
  uint8_t *dst = (uint8_t *)p_enc_frame->p_data[3] +
  p_enc_frame->data_len[3] + NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE;

  // fill in reconfig data if enabled; even if it's disabled, keep the space
  // for it if SEI or ROI is present;
  if (p_enc_frame->reconf_len || p_enc_frame->roi_len ||
      p_enc_frame->sei_total_len)
  {
    ni_log(NI_LOG_TRACE, "ni_logan_enc_copy_aux_data: keep reconfig space: %" PRId64 "\n",
           sizeof(ni_logan_encoder_change_params_t));

    memset(dst, 0, sizeof(ni_logan_encoder_change_params_t));

    if (p_enc_frame->reconf_len && p_enc_ctx->enc_change_params)
    {
      memcpy(dst, p_enc_ctx->enc_change_params, p_enc_frame->reconf_len);
    }

    dst += sizeof(ni_logan_encoder_change_params_t);
  }

  // fill in ROI map, if enabled
  if (p_enc_frame->roi_len)
  {
    if (NI_LOGAN_CODEC_FORMAT_H264 == codec_format && p_enc_ctx->avc_roi_map)
    {
      memcpy(dst, p_enc_ctx->avc_roi_map, p_enc_frame->roi_len);
    }
    else if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format && p_enc_ctx->hevc_roi_map)
    {
      memcpy(dst, p_enc_ctx->hevc_roi_map, p_enc_frame->roi_len);
    }
    // as long as the frame ROI len is set, even if ctx->roi_map is not init
    // (in the case of hardcoding ROI in nienc), still reserve the ROI space
    dst += p_enc_frame->roi_len;
  }

  // HDR SEI: mastering display color volume
  if (p_enc_frame->sei_hdr_mastering_display_color_vol_len)
  {
    ni_log(NI_LOG_TRACE, "ni_logan_enc_copy_aux_data: HDR SEI mdcv size: %u\n",
           p_enc_frame->sei_hdr_mastering_display_color_vol_len);
    memcpy(dst, mdcv_data,p_enc_frame->sei_hdr_mastering_display_color_vol_len);
    dst += p_enc_frame->sei_hdr_mastering_display_color_vol_len;
  }

  // HDR SEI: content light level info
  if (p_enc_frame->sei_hdr_content_light_level_info_len)
  {
    ni_log(NI_LOG_TRACE, "ni_logan_enc_copy_aux_data: HDR SEI cll size: %u\n",
           p_enc_frame->sei_hdr_content_light_level_info_len);

    memcpy(dst, cll_data, p_enc_frame->sei_hdr_content_light_level_info_len);
    dst += p_enc_frame->sei_hdr_content_light_level_info_len;
  }

  // HLG SEI: preferred characteristics
  if (p_enc_frame->preferred_characteristics_data_len)
  {
    dst[0] = dst[1] = dst[2] = 0;
    dst[3] = 1;
    if (NI_LOGAN_CODEC_FORMAT_H265 == codec_format)
    {
      dst[4] = 0x4e;
      dst[5] = 1;
      dst[6] = 0x93;  // payload type=147
      dst[7] = 1;     // payload size=1
      dst += 8;
    }
    else
    {
      dst[4] = 0x6;
      dst[5] = 0x93;  // payload type=147
      dst[6] = 1;     // payload size=1
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
    ni_log(NI_LOG_TRACE, "ni_logan_enc_copy_aux_data: close caption size: %u\n",
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
    dst += p_enc_frame->sei_user_data_unreg_len;
  }
}
