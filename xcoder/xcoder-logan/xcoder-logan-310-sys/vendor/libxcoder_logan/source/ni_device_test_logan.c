/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_test_logan.c
*
*  \brief  Example code on how to programmatically work with NI T-408 using
*          libxcoder API
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include "ni_getopt_logan.h"
#elif __linux__  || __APPLE__
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#endif

#include <fcntl.h>
#include <errno.h>
#include "ni_device_api_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_util_logan.h"
#include "ni_device_test_logan.h"
#include "ni_bitstream_logan.h"

typedef struct _ni_logan_err_rc_txt_entry
{
  ni_logan_retcode_t rc;
  const char  *txt;
} ni_logan_err_rc_txt_entry_t;

static const ni_logan_err_rc_txt_entry_t ni_logan_err_rc_description[] =
{
  NI_LOGAN_RETCODE_SUCCESS, "SUCCESS",
  NI_LOGAN_RETCODE_FAILURE, "FAILURE",
  NI_LOGAN_RETCODE_INVALID_PARAM, "INVALID_PARAM",
  NI_LOGAN_RETCODE_ERROR_MEM_ALOC, "ERROR_MEM_ALOC",
  NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED, "ERROR_NVME_CMD_FAILED",
  NI_LOGAN_RETCODE_ERROR_INVALID_SESSION, "ERROR_INVALID_SESSION",
  NI_LOGAN_RETCODE_ERROR_RESOURCE_UNAVAILABLE, "ERROR_RESOURCE_UNAVAILABLE",
  NI_LOGAN_RETCODE_PARAM_INVALID_NAME, "PARAM_INVALID_NAME",
  NI_LOGAN_RETCODE_PARAM_INVALID_VALUE, "PARAM_INVALID_VALUE",
  NI_LOGAN_RETCODE_PARAM_ERROR_FRATE, "PARAM_ERROR_FRATE",
  NI_LOGAN_RETCODE_PARAM_ERROR_BRATE, "PARAM_ERROR_BRATE",
  NI_LOGAN_RETCODE_PARAM_ERROR_TRATE, "PARAM_ERROR_TRATE",
  NI_LOGAN_RETCODE_PARAM_ERROR_RC_INIT_DELAY, "PARAM_ERROR_RC_INIT_DELAY",
  NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_PERIOD, "PARAM_ERROR_INTRA_PERIOD",
  NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_QP, "PARAM_ERROR_INTRA_QP",
  NI_LOGAN_RETCODE_PARAM_ERROR_GOP_PRESET, "PARAM_ERROR_GOP_PRESET",
  NI_LOGAN_RETCODE_PARAM_ERROR_CU_SIZE_MODE, "PARAM_ERROR_CU_SIZE_MODE",
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_NUM_MERGE, "PARAM_ERROR_MX_NUM_MERGE",
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_8X8_EN, "PARAM_ERROR_DY_MERGE_8X8_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_16X16_EN, "PARAM_ERROR_DY_MERGE_16X16_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_32X32_EN, "PARAM_ERROR_DY_MERGE_32X32_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_CU_LVL_RC_EN, "PARAM_ERROR_CU_LVL_RC_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_EN, "PARAM_ERROR_HVS_QP_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_SCL, "PARAM_ERROR_HVS_QP_SCL",
  NI_LOGAN_RETCODE_PARAM_ERROR_MN_QP, "PARAM_ERROR_MN_QP",
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_QP, "PARAM_ERROR_MX_QP",
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_DELTA_QP, "PARAM_ERROR_MX_DELTA_QP",
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_TOP, "PARAM_ERROR_CONF_WIN_TOP",
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_BOT, "PARAM_ERROR_CONF_WIN_BOT",
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_L, "PARAM_ERROR_CONF_WIN_L",
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_R, "PARAM_ERROR_CONF_WIN_R",
  NI_LOGAN_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM, "PARAM_ERROR_USR_RMD_ENC_PARAM",
  NI_LOGAN_RETCODE_PARAM_ERROR_BRATE_LT_TRATE, "PARAM_ERROR_BRATE_LT_TRATE",
  NI_LOGAN_RETCODE_PARAM_ERROR_RCINITDELAY, "PARAM_ERROR_RCINITDELAY",
  NI_LOGAN_RETCODE_PARAM_ERROR_RCENABLE, "PARAM_ERROR_RCENABLE",
  NI_LOGAN_RETCODE_PARAM_ERROR_MAXNUMMERGE, "PARAM_ERROR_MAXNUMMERGE",
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP, "PARAM_ERROR_CUSTOM_GOP",
  NI_LOGAN_RETCODE_PARAM_ERROR_PIC_WIDTH, "PARAM_ERROR_PIC_WIDTH",
  NI_LOGAN_RETCODE_PARAM_ERROR_PIC_HEIGHT, "PARAM_ERROR_PIC_HEIGHT",
  NI_LOGAN_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE, "PARAM_ERROR_DECODING_REFRESH_TYPE",
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN, "PARAM_ERROR_CUSIZE_MODE_8X8_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN, "PARAM_ERROR_CUSIZE_MODE_16X16_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN, "PARAM_ERROR_CUSIZE_MODE_32X32_EN",
  NI_LOGAN_RETCODE_PARAM_ERROR_TOO_BIG, "PARAM_ERROR_TOO_BIG",
  NI_LOGAN_RETCODE_PARAM_ERROR_TOO_SMALL, "PARAM_ERROR_TOO_SMALL",
  NI_LOGAN_RETCODE_PARAM_ERROR_ZERO, "PARAM_ERROR_ZERO",
  NI_LOGAN_RETCODE_PARAM_ERROR_OOR, "PARAM_ERROR_OOR",
  NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG, "PARAM_ERROR_WIDTH_TOO_BIG",
  NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL, "PARAM_ERROR_WIDTH_TOO_SMALL",
  NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG, "PARAM_ERROR_HEIGHT_TOO_BIG",
  NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL, "PARAM_ERROR_HEIGHT_TOO_SMALL",
  NI_LOGAN_RETCODE_PARAM_ERROR_AREA_TOO_BIG, "PARAM_ERROR_AREA_TOO_BIG",
  NI_LOGAN_RETCODE_ERROR_EXCEED_MAX_NUM_SESSIONS, "ERROR_EXCEED_MAX_NUM_SESSIONS",
  NI_LOGAN_RETCODE_ERROR_GET_DEVICE_POOL, "ERROR_GET_DEVICE_POOL",
  NI_LOGAN_RETCODE_ERROR_LOCK_DOWN_DEVICE, "ERROR_LOCK_DOWN_DEVICE",
  NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE, "ERROR_UNLOCK_DEVICE",
  NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE, "ERROR_OPEN_DEVICE",
  NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE, "ERROR_INVALID_HANDLE",
  NI_LOGAN_RETCODE_ERROR_INVALID_ALLOCATION_METHOD, "ERROR_INVALID_ALLOCATION_METHOD",
  NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY, "ERROR_VPU_RECOVERY",
  NI_LOGAN_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE, "PARAM_GOP_INTRA_INCOMPATIBLE",

  NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL, "NVME_SC_WRITE_BUFFER_FULL",
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE, "NVME_SC_RESOURCE_UNAVAILABLE",
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_IS_EMPTY, "NVME_SC_RESOURCE_IS_EMPTY",
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_NOT_FOUND, "NVME_SC_RESOURCE_NOT_FOUND",
  NI_LOGAN_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED, "NVME_SC_REQUEST_NOT_COMPLETED",
  NI_LOGAN_RETCODE_NVME_SC_REQUEST_IN_PROGRESS, "NVME_SC_REQUEST_IN_PROGRESS",
  NI_LOGAN_RETCODE_NVME_SC_INVALID_PARAMETER, "NVME_SC_INVALID_PARAMETER",
  NI_LOGAN_RETCODE_NVME_SC_VPU_RECOVERY, "NVME_SC_VPU_RECOVERY",
  NI_LOGAN_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT, "NVME_SC_VPU_RSRC_INSUFFICIENT",
  NI_LOGAN_RETCODE_NVME_SC_VPU_GENERAL_ERROR, "NVME_SC_VPU_GENERAL_ERROR",

  NI_LOGAN_RETCODE_DEFAULT_SESSION_ERR_NO, "DEFAULT_SESSION_ERR_NO",
};

typedef enum _ni_logan_nalu_type
{
  H264_NAL_UNSPECIFIED     = 0,
  H264_NAL_SLICE           = 1,
  H264_NAL_DPA             = 2,
  H264_NAL_DPB             = 3,
  H264_NAL_DPC             = 4,
  H264_NAL_IDR_SLICE       = 5,
  H264_NAL_SEI             = 6,
  H264_NAL_SPS             = 7,
  H264_NAL_PPS             = 8,
  H264_NAL_AUD             = 9,
  H264_NAL_END_SEQUENCE    = 10,
  H264_NAL_END_STREAM      = 11,
  H264_NAL_FILLER_DATA     = 12,
  H264_NAL_SPS_EXT         = 13,
  H264_NAL_PREFIX          = 14,
  H264_NAL_SUB_SPS         = 15,
  H264_NAL_DPS             = 16,
  H264_NAL_AUXILIARY_SLICE = 19,
} ni_logan_nalu_type_t;

#define MAX_LOG2_MAX_FRAME_NUM    (12 + 4)
#define MIN_LOG2_MAX_FRAME_NUM    4
#define EXTENDED_SAR              255
#define QP_MAX_NUM (51 + 6*6) // The maximum supported qp

/**
 * Picture parameter set
 */
typedef struct _ni_logan_h264_pps_t
{
  unsigned int sps_id;
  int cabac;                  ///< entropy_coding_mode_flag
  int pic_order_present;      ///< pic_order_present_flag
  int slice_group_count;      ///< num_slice_groups_minus1 + 1
  int mb_slice_group_map_type;
  unsigned int ref_count[2];  ///< num_ref_idx_l0/1_active_minus1 + 1
  int weighted_pred;          ///< weighted_pred_flag
  int weighted_bipred_idc;
  int init_qp;                ///< pic_init_qp_minus26 + 26
  int init_qs;                ///< pic_init_qs_minus26 + 26
  int chroma_qp_index_offset[2];
  int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
  int constrained_intra_pred;     ///< constrained_intra_pred_flag
  int redundant_pic_cnt_present;  ///< redundant_pic_cnt_present_flag
  int transform_8x8_mode;         ///< transform_8x8_mode_flag
  uint8_t scaling_matrix4[6][16];
  uint8_t scaling_matrix8[6][64];
  uint8_t chroma_qp_table[2][QP_MAX_NUM+1];  ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
  int chroma_qp_diff;
  uint8_t data[4096];
  size_t data_size;

  uint32_t dequant4_buffer[6][QP_MAX_NUM + 1][16];
  uint32_t dequant8_buffer[6][QP_MAX_NUM + 1][64];
  uint32_t(*dequant4_coeff[6])[16];
  uint32_t(*dequant8_coeff[6])[64];
} ni_logan_h264_pps_t;

volatile int send_fin_flag = 0, receive_fin_flag = 0, flush_fin_flag = 0, err_flag = 0;
volatile uint32_t number_of_frames = 0;
volatile uint32_t number_of_packets = 0;
static volatile uint32_t dec_flush_sec = 0;
static volatile uint32_t dec_flush_cnt = 0;
static volatile uint32_t dec_flush_pkt = 0;
static int g_file_loop = 1;
struct timeval start_time, previous_time, current_time;
time_t start_timestamp = 0, privious_timestamp = 0, current_timestamp = 0;

// max YUV frame size
#define MAX_YUV_FRAME_SIZE (7680 * 4320 * 3 / 2)

static uint8_t *g_file_cache = NULL;
static uint8_t *g_curr_cache_pos = NULL;

volatile long total_file_size = 0;
volatile uint32_t data_left_size = 0;

static const char* ni_logan_get_rc_txt(ni_logan_retcode_t rc)
{
  int i;
  for (i = 0; i < sizeof(ni_logan_err_rc_description) / sizeof(ni_logan_err_rc_txt_entry_t);
       i++)
  {
    if (rc == ni_logan_err_rc_description[i].rc)
    {
      return ni_logan_err_rc_description[i].txt;
    }
  }
  return "rc not supported";
}

void arg_error_exit(char* arg_name, char* param)
{
  fprintf(stderr, "Error: unrecognized argument for %s, \"%s\"\n",
          arg_name, param);
  exit(-1);
}

// return actual bytes copied from cache, in requested size
int read_next_chunk(uint8_t *p_dst, uint32_t to_read)
{
  int to_copy = to_read;

  if (data_left_size == 0) 
  {
    return 0;
  } 
  else if (data_left_size < to_read) 
  {
    to_copy = data_left_size;
  }

  memcpy(p_dst, g_curr_cache_pos, to_copy);
  g_curr_cache_pos += to_copy;
  data_left_size -= to_copy;

  return to_copy;
}

// current position of the input data buffer
static int curr_nal_start = 0;
static int curr_found_pos = 0;

// reset input data buffer position to the start
void reset_data_buf_pos(void)
{
  curr_nal_start = 0;
  curr_found_pos = 0;
}

// rewind input data buffer position by a number of bytes, if possible
void rewind_data_buf_pos_by(int nb_bytes)
{
  if (curr_found_pos > nb_bytes)
  {
    curr_found_pos -= nb_bytes;
  }
  else
  {
    ni_log(NI_LOG_ERROR, "Error %s %d bytes!\n", __FUNCTION__, nb_bytes);
  }
}

// find/copy next H.264 NAL unit (including start code) and its type;
// return NAL data size if found, 0 otherwise
int find_h264_next_nalu(uint8_t *p_dst, int *nal_type)
{
  int data_size;

  int i = curr_found_pos;

  if (i + 3 >= total_file_size)
  {
    ni_log(NI_LOG_TRACE, "%s reaching end, curr_pos %d total input size %lu\n",
           __FUNCTION__, curr_found_pos, total_file_size);
    return 0;
  }

  // search for start code 0x000001 or 0x00000001
  while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i+1] != 0x00 ||
          g_curr_cache_pos[i+2] != 0x01) &&
         (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i+1] != 0x00 ||
          g_curr_cache_pos[i+2] != 0x00 || g_curr_cache_pos[i+3] != 0x01))
  {
    i++;
    if (i + 3 > total_file_size)
    {
      return 0;
    }
  }

  // found start code, advance to NAL unit start depends on actual start code
  if (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i+1] != 0x00 ||
      g_curr_cache_pos[i+2] != 0x01)
  {
    i++;
  }

  i += 3;
  curr_nal_start = i;

  // get the NAL type
  *nal_type = (g_curr_cache_pos[i] & 0x1f);

  // advance to the end of NAL, or stream
  while ((g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i+1] != 0x00 ||
          g_curr_cache_pos[i+2] != 0x00) &&
         (g_curr_cache_pos[i] != 0x00 || g_curr_cache_pos[i+1] != 0x00 ||
          g_curr_cache_pos[i+2] != 0x01))
  {
    i++;
    // if reaching the stream end
    if (i + 3 > total_file_size)
    {
      data_size = total_file_size - curr_found_pos;
      memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
      curr_found_pos = total_file_size;
      return data_size;
    }
  }

  data_size = i - curr_found_pos;
  memcpy(p_dst, &g_curr_cache_pos[curr_found_pos], data_size);
  curr_found_pos = i;
  return data_size;
}

static const uint8_t default_scaling4[2][16] =
{
  {  6, 13, 20, 28, 13, 20, 28, 32,
     20, 28, 32, 37, 28, 32, 37, 42 },
  { 10, 14, 20, 24, 14, 20, 24, 27,
    20, 24, 27, 30, 24, 27, 30, 34 }
};

static const uint8_t default_scaling8[2][64] =
{
  {  6, 10, 13, 16, 18, 23, 25, 27,
     10, 11, 16, 18, 23, 25, 27, 29,
     13, 16, 18, 23, 25, 27, 29, 31,
     16, 18, 23, 25, 27, 29, 31, 33,
     18, 23, 25, 27, 29, 31, 33, 36,
     23, 25, 27, 29, 31, 33, 36, 38,
     25, 27, 29, 31, 33, 36, 38, 40,
     27, 29, 31, 33, 36, 38, 40, 42 },
  {  9, 13, 15, 17, 19, 21, 22, 24,
     13, 13, 17, 19, 21, 22, 24, 25,
     15, 17, 19, 21, 22, 24, 25, 27,
     17, 19, 21, 22, 24, 25, 27, 28,
     19, 21, 22, 24, 25, 27, 28, 30,
     21, 22, 24, 25, 27, 28, 30, 32,
     22, 24, 25, 27, 28, 30, 32, 33,
     24, 25, 27, 28, 30, 32, 33, 35 }
};

const uint8_t ni_logan_zigzag_direct[64] =
{
  0,   1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

const uint8_t ni_logan_zigzag_scan[16+1] =
{
  0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4,
  1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
  1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
  3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

// HRD parsing: return 0 if parsing ok, -1 otherwise
int parse_hrd(ni_bitstream_reader_t *br, ni_logan_h264_sps_t *sps)
{
  int cpb_count, i;
  unsigned int dummy;

  cpb_count = ni_bs_reader_get_ue(br) + 1;
  if (cpb_count > 32U)
  {
    ni_log(NI_LOG_ERROR, "%s invalid cpb_count %d\n", __FUNCTION__, cpb_count);
    return -1;
  }

  ni_bs_reader_get_bits(br, 4); // bit_rate_scale
  ni_bs_reader_get_bits(br, 4); // cpb_size_scale
  for (i = 0; i < cpb_count; i++)
  {
    dummy = ni_bs_reader_get_ue(br); // bit_rate_value_minus1
    dummy = ni_bs_reader_get_ue(br); // cpb_size_value_minus1
    dummy = ni_bs_reader_get_bits(br, 1); // cbr_flag
  }
  sps->initial_cpb_removal_delay_length = ni_bs_reader_get_bits(br, 5) + 1;
  sps->cpb_removal_delay_length         = ni_bs_reader_get_bits(br, 5) + 1;
  sps->dpb_output_delay_length          = ni_bs_reader_get_bits(br, 5) + 1;
  sps->time_offset_length               = ni_bs_reader_get_bits(br, 5);
  sps->cpb_cnt                          = cpb_count;
  return 0;
}

// VUI parsing: return 0 if parsing ok, -1 otherwise
int parse_vui(ni_bitstream_reader_t *br, ni_logan_h264_sps_t *sps)
{
  int ret = -1, aspect_ratio_info_present_flag;
  unsigned int aspect_ratio_idc, dummy;

  aspect_ratio_info_present_flag = ni_bs_reader_get_bits(br, 1);
  if (aspect_ratio_info_present_flag)
  {
    aspect_ratio_idc = ni_bs_reader_get_bits(br, 8);
    if (EXTENDED_SAR == aspect_ratio_idc)
    {
      sps->sar.num = ni_bs_reader_get_bits(br, 16);
      sps->sar.den = ni_bs_reader_get_bits(br, 16);
    }
    else if (aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
    {
      sps->sar = ni_h264_pixel_aspect_list[aspect_ratio_idc];
    }
    else
    {
      ni_log(NI_LOG_ERROR, "%s: illegal aspect ratio %u\n",
             __FUNCTION__, aspect_ratio_idc);
      goto end;
    }
  }
  else
  {
    sps->sar.num = sps->sar.den = 0;
  }

  if (ni_bs_reader_get_bits(br, 1)) // overscan_info_present_flag
  {
    ni_bs_reader_get_bits(br, 1); // overscan_appropriate_flag
  }
  sps->video_signal_type_present_flag = ni_bs_reader_get_bits(br, 1);
  if (sps->video_signal_type_present_flag)
  {
    ni_bs_reader_get_bits(br, 3); // video_format
    sps->full_range = ni_bs_reader_get_bits(br, 1); // video_full_range_flag

    sps->colour_description_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->colour_description_present_flag)
    {
      sps->color_primaries = ni_bs_reader_get_bits(br, 8);
      sps->color_trc       = ni_bs_reader_get_bits(br, 8);
      sps->colorspace      = ni_bs_reader_get_bits(br, 8);
      if (sps->color_primaries < NI_COL_PRI_RESERVED0 ||
          sps->color_primaries >= NI_COL_PRI_NB)
      {
        sps->color_primaries = NI_COL_PRI_UNSPECIFIED;
      }
      if (sps->color_trc < NI_COL_TRC_RESERVED0 ||
          sps->color_trc >= NI_COL_TRC_NB)
      {
        sps->color_trc = NI_COL_TRC_UNSPECIFIED;
      }
      if (sps->colorspace < NI_COL_SPC_RGB ||
          sps->colorspace >= NI_COL_SPC_NB)
      {
        sps->color_trc = NI_COL_SPC_UNSPECIFIED;
      }
    }
  }

  if (ni_bs_reader_get_bits(br, 1)) // chroma_location_info_present_flag
  {
    dummy = ni_bs_reader_get_ue(br); // chroma_sample_location_type_top_field
    dummy = ni_bs_reader_get_ue(br); // chroma_sample_location_type_bottom_field
  }

  sps->timing_info_present_flag = ni_bs_reader_get_bits(br, 1);
  if (sps->timing_info_present_flag)
  {
    unsigned num_units_in_tick = ni_bs_reader_get_bits(br, 32);
    unsigned time_scale        = ni_bs_reader_get_bits(br, 32);
    if (! num_units_in_tick || ! time_scale)
    {
      ni_log(NI_LOG_ERROR, "%s: error num_units_in_tick/time_scale (%u/%u)\n",
             __FUNCTION__, num_units_in_tick, time_scale);
      sps->timing_info_present_flag = 0;
    }
    sps->fixed_frame_rate_flag = ni_bs_reader_get_bits(br, 1);
  }

  sps->nal_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
  if (sps->nal_hrd_parameters_present_flag && parse_hrd(br, sps) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s: nal_hrd_parameters_present and error "
           "parse_hrd !\n", __FUNCTION__);
    goto end;
  }

  sps->vcl_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
  if (sps->vcl_hrd_parameters_present_flag && parse_hrd(br, sps) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s: vcl_hrd_parameters_present and error "
           "parse_hrd !\n", __FUNCTION__);
    goto end;
  }

  if (sps->nal_hrd_parameters_present_flag ||
      sps->vcl_hrd_parameters_present_flag)
  {
    ni_bs_reader_get_bits(br, 1); // low_delay_hrd_flag
  }

  sps->pic_struct_present_flag = ni_bs_reader_get_bits(br, 1);

  sps->bitstream_restriction_flag = ni_bs_reader_get_bits(br, 1);
  if (sps->bitstream_restriction_flag)
  {
    ni_bs_reader_get_bits(br, 1); // motion_vectors_over_pic_boundaries_flag
    ni_bs_reader_get_ue(br); // max_bytes_per_pic_denom
    ni_bs_reader_get_ue(br); // max_bits_per_mb_denom
    ni_bs_reader_get_ue(br); // log2_max_mv_length_horizontal
    ni_bs_reader_get_ue(br); // log2_max_mv_length_vertical
    sps->num_reorder_frames = ni_bs_reader_get_ue(br);
    sps->max_dec_frame_buffering = ni_bs_reader_get_ue(br);

    if (sps->num_reorder_frames > 16U)
    {
      ni_log(NI_LOG_ERROR, "%s: clip illegal num_reorder_frames %d !\n",
             __FUNCTION__, sps->num_reorder_frames);
      sps->num_reorder_frames = 16;
      goto end;
    }
  }

  // everything is fine
  ret = 0;

end:
  return ret;
}

int parse_scaling_list(ni_bitstream_reader_t *br, uint8_t *factors, int size,
                       const uint8_t *jvt_list, const uint8_t *fallback_list)
{
  int i, last = 8, next = 8;
  const uint8_t *scan = (size == 16 ? ni_logan_zigzag_scan : ni_logan_zigzag_direct);

  // matrix not written, we use the predicted one */
  if (! ni_bs_reader_get_bits(br, 1))
  {
    memcpy(factors, fallback_list, size * sizeof(uint8_t));
  }
  else
  {
    for (i = 0; i < size; i++)
    {
      if (next)
      {
        int v = ni_bs_reader_get_se(br);
        if (v < -128 || v > 127)
        {
          ni_log(NI_LOG_ERROR, "delta scale %d is invalid\n", v);
          return -1;
        }
        next = (last + v) & 0xff;
      }
      if (! i && ! next)
      { // matrix not written, we use the preset one
        memcpy(factors, jvt_list, size * sizeof(uint8_t));
        break;
      }
      last = (factors[scan[i]] = next ? next : last);
    }
  }
  return 0;
}

// SPS seq scaling matrices parsing: return 0 if parsing ok, -1 otherwise
int parse_scaling_matrices(ni_bitstream_reader_t *br, const ni_logan_h264_sps_t *sps,
                           const ni_logan_h264_pps_t *pps, int is_sps,
                           uint8_t(*scaling_matrix4)[16],
                           uint8_t(*scaling_matrix8)[64])
{
  int ret = 0;
  int fallback_sps = !is_sps && sps->scaling_matrix_present;
  const uint8_t *fallback[4] = {
    fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
    fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
    fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
    fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]
  };

  if (ni_bs_reader_get_bits(br, 1)) // scaling_matrix_present
  {
    // retrieve matrices
    ret |= parse_scaling_list(br, scaling_matrix4[0], 16, default_scaling4[0],
                              fallback[0]);        // Intra, Y
    ret |= parse_scaling_list(br, scaling_matrix4[1], 16, default_scaling4[0],
                              scaling_matrix4[0]); // Intra, Cr
    ret |= parse_scaling_list(br, scaling_matrix4[2], 16, default_scaling4[0],
                              scaling_matrix4[1]); // Intra, Cb
    ret |= parse_scaling_list(br, scaling_matrix4[3], 16, default_scaling4[1],
                              fallback[1]);        // Inter, Y
    ret |= parse_scaling_list(br, scaling_matrix4[4], 16, default_scaling4[1],
                              scaling_matrix4[3]); // Inter, Cr
    ret |= parse_scaling_list(br, scaling_matrix4[5], 16, default_scaling4[1],
                              scaling_matrix4[4]); // Inter, Cb

    if (is_sps || pps->transform_8x8_mode)
    {
      ret |= parse_scaling_list(br, scaling_matrix8[0], 64,
                                default_scaling8[0], fallback[2]); // Intra, Y
      ret |= parse_scaling_list(br, scaling_matrix8[3], 64,
                                default_scaling8[1], fallback[3]); // Inter, Y
      if (sps->chroma_format_idc == 3)
      {
        ret |= parse_scaling_list(br, scaling_matrix8[1], 64,  // Intra, Cr
                                  default_scaling8[0], scaling_matrix8[0]);
        ret |= parse_scaling_list(br, scaling_matrix8[4], 64,  // Inter, Cr
                                  default_scaling8[1], scaling_matrix8[3]);
        ret |= parse_scaling_list(br, scaling_matrix8[2], 64,  // Intra, Cb
                                  default_scaling8[0], scaling_matrix8[1]);
        ret |= parse_scaling_list(br, scaling_matrix8[5], 64,  // Inter, Cb
                                  default_scaling8[1], scaling_matrix8[4]);
      }
    }
    if (! ret)
    {
      ret = is_sps;
    }
  }
  return ret;
}

// SPS parsing: return 0 if parsing ok, -1 otherwise
int parse_sps(uint8_t *buf, int size_bytes, ni_logan_h264_sps_t *sps)
{
  int ret = -1;
  ni_bitstream_reader_t br;
  int profile_idc, level_idc, constraint_set_flags = 0;
  uint32_t sps_id;
  int i, log2_max_frame_num_minus4;

  ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
  // skip NAL header
  ni_bs_reader_skip_bits(&br, 8);

  profile_idc = ni_bs_reader_get_bits(&br, 8);
  // from constraint_set0_flag to constraint_set5_flag
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 0;
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 1;
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 2;
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 3;
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 4;
  constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 5;
  ni_bs_reader_skip_bits(&br, 2); // reserved_zero_2bits
  level_idc = ni_bs_reader_get_bits(&br, 8);
  sps_id = ni_bs_reader_get_ue(&br);

  sps->sps_id               = sps_id;
  sps->profile_idc          = profile_idc;
  sps->constraint_set_flags = constraint_set_flags;
  sps->level_idc            = level_idc;
  sps->full_range           = -1;

  memset(sps->scaling_matrix4, 16, sizeof(sps->scaling_matrix4));
  memset(sps->scaling_matrix8, 16, sizeof(sps->scaling_matrix8));
  sps->scaling_matrix_present = 0;
  sps->colorspace = 2; // NI_COL_SPC_UNSPECIFIED

  if (100 == profile_idc || 110 == profile_idc || 122 == profile_idc ||
      244 == profile_idc || 44 == profile_idc || 83 == profile_idc ||
      86 == profile_idc || 118 == profile_idc || 128 == profile_idc ||
      138 == profile_idc || 139 == profile_idc || 134 == profile_idc ||
      135 == profile_idc || 144 == profile_idc /* old High444 profile */)
  {
    sps->chroma_format_idc = ni_bs_reader_get_ue(&br);
    if (sps->chroma_format_idc > 3U)
    {
      ni_log(NI_LOG_ERROR, "%s error: chroma_format_idc > 3!\n", __FUNCTION__);
      goto end;
    }
    else if (3 == sps->chroma_format_idc)
    {
      sps->residual_color_transform_flag = ni_bs_reader_get_bits(&br, 1);
      if (sps->residual_color_transform_flag)
      {
        ni_log(NI_LOG_ERROR, "%s error: residual_color_transform not "
               "supported !\n", __FUNCTION__);
        goto end;
      }
    }
    sps->bit_depth_luma   = ni_bs_reader_get_ue(&br) + 8;
    sps->bit_depth_chroma = ni_bs_reader_get_ue(&br) + 8;
    if (sps->bit_depth_luma != sps->bit_depth_chroma)
    {
      ni_log(NI_LOG_ERROR, "%s error: different luma %d & chroma %d bit depth !\n",
             __FUNCTION__, sps->bit_depth_luma, sps->bit_depth_chroma);
      goto end;
    }
    if (sps->bit_depth_luma < 8 || sps->bit_depth_luma > 12 ||
        sps->bit_depth_chroma < 8 || sps->bit_depth_chroma > 12)
    {
      ni_log(NI_LOG_ERROR, "%s error: illegal luma/chroma bit depth value (%d %d)!\n",
             __FUNCTION__, sps->bit_depth_luma, sps->bit_depth_chroma);
      goto end;
    }

    sps->transform_bypass = ni_bs_reader_get_bits(&br, 1);
    ret = parse_scaling_matrices(&br, sps, NULL, 1,
                                 sps->scaling_matrix4, sps->scaling_matrix8);
    if (ret < 0)
    {
      ni_log(NI_LOG_ERROR, "%s error scaling matrices parse failed !\n", __FUNCTION__);
      goto end;
    }
    sps->scaling_matrix_present |= ret;
  } // profile_idc
  else
  {
    sps->chroma_format_idc = 1;
    sps->bit_depth_luma    = 8;
    sps->bit_depth_chroma  = 8;
  }

  log2_max_frame_num_minus4 = ni_bs_reader_get_ue(&br);
  if (log2_max_frame_num_minus4 < MIN_LOG2_MAX_FRAME_NUM - 4 ||
      log2_max_frame_num_minus4 > MAX_LOG2_MAX_FRAME_NUM - 4)
  {
    ni_log(NI_LOG_ERROR, "%s error: log2_max_frame_num_minus4 %d out of "
           "range (0-12)!\n", __FUNCTION__, log2_max_frame_num_minus4);
    goto end;
  }
  sps->log2_max_frame_num = log2_max_frame_num_minus4 + 4;

  sps->poc_type = ni_bs_reader_get_ue(&br);
  if (0 == sps->poc_type)
  {
    uint32_t v = ni_bs_reader_get_ue(&br);
    if (v > 12)
    {
      ni_log(NI_LOG_ERROR, "%s error: log2_max_poc_lsb %d out of range!\n",
             __FUNCTION__, v);
      goto end;
    }
    sps->log2_max_poc_lsb = v + 4;
  }
  else if (1 == sps->poc_type)
  {
    sps->delta_pic_order_always_zero_flag = ni_bs_reader_get_bits(&br, 1);
    sps->offset_for_non_ref_pic           = ni_bs_reader_get_se(&br);
    sps->offset_for_top_to_bottom_field   = ni_bs_reader_get_se(&br);
    sps->poc_cycle_length                 = ni_bs_reader_get_ue(&br);
    if ((unsigned)sps->poc_cycle_length >= 256)
    {
      ni_log(NI_LOG_ERROR, "%s error: poc_cycle_length %d out of range!\n", __FUNCTION__, sps->poc_cycle_length);
      goto end;
    }
    for (i = 0; i < sps->poc_cycle_length; i++)
    {
      sps->offset_for_ref_frame[i] = ni_bs_reader_get_se(&br);
    }
  }
  else if (2 != sps->poc_type)
  {
    ni_log(NI_LOG_ERROR, "%s error: illegal PIC type %d!\n",
           __FUNCTION__, sps->poc_type);
    goto end;
  }
  sps->ref_frame_count = ni_bs_reader_get_ue(&br);
  sps->gaps_in_frame_num_allowed_flag = ni_bs_reader_get_bits(&br, 1);
  sps->mb_width                       = ni_bs_reader_get_ue(&br) + 1;
  sps->mb_height                      = ni_bs_reader_get_ue(&br) + 1;

  sps->frame_mbs_only_flag = ni_bs_reader_get_bits(&br, 1);
  sps->mb_height *= 2 - sps->frame_mbs_only_flag;

  if (! sps->frame_mbs_only_flag)
  {
    sps->mb_aff = ni_bs_reader_get_bits(&br, 1);
  }
  else
  {
    sps->mb_aff = 0;
  }

  sps->direct_8x8_inference_flag = ni_bs_reader_get_bits(&br, 1);

  sps->crop = ni_bs_reader_get_bits(&br, 1);
  if (sps->crop)
  {
    unsigned int crop_left   = ni_bs_reader_get_ue(&br);
    unsigned int crop_right  = ni_bs_reader_get_ue(&br);
    unsigned int crop_top    = ni_bs_reader_get_ue(&br);
    unsigned int crop_bottom = ni_bs_reader_get_ue(&br);

    // no range checking
    int vsub   = (sps->chroma_format_idc == 1) ? 1 : 0;
    int hsub   = (sps->chroma_format_idc == 1 ||
                  sps->chroma_format_idc == 2) ? 1 : 0;
    int step_x = 1 << hsub;
    int step_y = (2 - sps->frame_mbs_only_flag) << vsub;

    sps->crop_left   = crop_left   * step_x;
    sps->crop_right  = crop_right  * step_x;
    sps->crop_top    = crop_top    * step_y;
    sps->crop_bottom = crop_bottom * step_y;
  }
  else
  {
    sps->crop_left   =
    sps->crop_right  =
    sps->crop_top    =
    sps->crop_bottom =
    sps->crop        = 0;
  }

  // deduce real width/heigh
  sps->width = 16 * sps->mb_width - sps->crop_left - sps->crop_right;
  sps->height = 16 * sps->mb_height - sps->crop_top - sps->crop_bottom;

  sps->vui_parameters_present_flag = ni_bs_reader_get_bits(&br, 1);
  if (sps->vui_parameters_present_flag)
  {
    int ret = parse_vui(&br, sps);
    if (ret < 0)
    {
      ni_log(NI_LOG_ERROR, "%s error: parse_vui failed %d!\n", __FUNCTION__, ret);
      goto end;
    }
  }

  // everything is fine
  ret = 0;

end:

  return ret;
}

int parse_sei(uint8_t *buf, int size_bytes, ni_logan_h264_sps_t *sps,
              int *sei_type, int *is_interlaced)
{
  ni_bitstream_reader_t br;
  *is_interlaced = 0;
  int ret = -1, dummy;
  int cpb_dpb_delays_present_flag = (sps->nal_hrd_parameters_present_flag ||
                                     sps->vcl_hrd_parameters_present_flag);
  //pic_struct_present_flag

  ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
  // skip NAL header
  ni_bs_reader_skip_bits(&br, 8);

  while (ni_bs_reader_get_bits_left(&br) > 16)
  {
    int type = 0;
    int size = 0, tmp, next;

    do
    {
      if (ni_bs_reader_get_bits_left(&br) < 8)
      {
        ni_log(NI_LOG_ERROR, "%s type parse error !\n", __FUNCTION__);
        goto end;
      }
      tmp = ni_bs_reader_get_bits(&br, 8);
      type += tmp;
    } while (tmp == 0xFF);

    *sei_type = type;
    do
    {
      if (ni_bs_reader_get_bits_left(&br) < 8)
      {
        ni_log(NI_LOG_ERROR, "%s type %d size parse error!\n",
               __FUNCTION__, type);
        goto end;
      }
      tmp = ni_bs_reader_get_bits(&br, 8);
      size += tmp;
    } while (tmp == 0xFF);

    if (size > ni_bs_reader_get_bits_left(&br) / 8)
    {
      ni_log(NI_LOG_ERROR, "%s SEI type %d size %u truncated at %d\n",
             __FUNCTION__, type, size, ni_bs_reader_get_bits_left(&br));
      goto end;
    }
    next = ni_bs_reader_bits_count(&br) + 8 * size;

    switch (type)
    {
    case NI_H264_SEI_TYPE_PIC_TIMING:
      if (cpb_dpb_delays_present_flag)
      {
        dummy = ni_bs_reader_get_bits(&br, sps->cpb_removal_delay_length);
        dummy = ni_bs_reader_get_bits(&br, sps->dpb_output_delay_length);
      }
      if (sps->pic_struct_present_flag)
      {
        dummy = ni_bs_reader_get_bits(&br, 4);
        if (dummy < NI_H264_SEI_PIC_STRUCT_FRAME ||
            dummy > NI_H264_SEI_PIC_STRUCT_FRAME_TRIPLING)
        {
          ni_log(NI_LOG_ERROR, "%s pic_timing SEI invalid pic_struct: %d\n",
                 __FUNCTION__, dummy);
          goto end;
        }
        if (dummy > NI_H264_SEI_PIC_STRUCT_FRAME)
        {
          *is_interlaced = 1;
        }
        goto success;
      }
      break;
    default:
      // skip all other SEI types
      ;
    }
    ni_bs_reader_skip_bits(&br, next - ni_bs_reader_bits_count(&br));
  } // while in SEI

success:
  ret = 0;

end:
  return ret;
}

// probe h.264 stream info; return 0 if stream can be decoded, -1 otherwise
int probe_h264_stream_info(ni_logan_h264_sps_t *sps)
{
  int ret = -1;
  uint8_t *buf = NULL;
  uint8_t *p_buf = buf;
  int nal_size, ep3_removed = 0, vcl_nal_count = 0;
  int nal_type = -1, sei_type = -1;
  int sps_parsed = 0, is_interlaced = 0;

  if (NULL == (buf = calloc(1, NI_LOGAN_MAX_TX_SZ)))
  {
    ni_log(NI_LOG_ERROR, "Error %s: allocate stream buf\n", __FUNCTION__);
    goto end;
  }

  reset_data_buf_pos();
  // probe at most 100 VCL before stops
  while ((! sps_parsed || ! is_interlaced) && vcl_nal_count < 100 &&
         (nal_size = find_h264_next_nalu(buf, &nal_type)) > 0)
  {
    ni_log(NI_LOG_TRACE, "nal %d  nal_size %d\n", nal_type, nal_size);
    p_buf = buf;

    // skip the start code
    while (! (p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
           nal_size > 3)
    {
      p_buf++;
      nal_size--;
    }
    if (nal_size <= 3)
    {
      ni_log(NI_LOG_ERROR, "Error %s NAL has no header\n", __FUNCTION__);
      continue;
    }

    p_buf += 3;
    nal_size -= 3;

    ep3_removed = ni_logan_remove_emulation_prevent_bytes(p_buf, nal_size);
    nal_size -= ep3_removed;

    if (H264_NAL_SPS == nal_type && ! sps_parsed)
    {
      if (vcl_nal_count > 0)
      {
        ni_log(NI_LOG_INFO, "Warning: %s has %d slice NAL units ahead of SPS!\n",
               __FUNCTION__, vcl_nal_count);
      }

      if (parse_sps(p_buf, nal_size, sps))
      {
        ni_log(NI_LOG_ERROR, "%s: parse_sps error\n", __FUNCTION__);
        break;
      }
      sps_parsed = 1;
    }
    else if (H264_NAL_SEI == nal_type)
    {
      parse_sei(p_buf, nal_size, sps, &sei_type, &is_interlaced);
    }
    else if (H264_NAL_SLICE == nal_type || H264_NAL_IDR_SLICE == nal_type)
    {
      vcl_nal_count++;
    }

    if (sps_parsed &&
        (sps->pic_struct_present_flag || sps->nal_hrd_parameters_present_flag ||
         sps->vcl_hrd_parameters_present_flag) &&
        NI_H264_SEI_TYPE_PIC_TIMING == sei_type && is_interlaced)
    {
      ni_log(NI_LOG_ERROR, "probe_h264_stream_info interlaced NOT supported!\n", __FUNCTION__);
      break;
    }
  } // while for each NAL unit

  reset_data_buf_pos();

  ni_log(NI_LOG_INFO, "VCL NAL parsed: %d, SPS parsed: %s, is interlaced: %s\n",
         vcl_nal_count, sps_parsed ? "Yes":"No", is_interlaced ? "Yes":"No");
  if (sps_parsed && ! is_interlaced)
  {
    ret = 0;
  }
  else
  {
    ni_log(NI_LOG_ERROR, "Input is either interlaced, or unable to determine, "
           "probing failed.\n");
  }

  static const char csp[4][5] = { "Gray", "420", "422", "444" };
  ni_log(NI_LOG_INFO, "H.264 stream probed %d VCL NAL units, sps:%u "
         "profile:%d/%d poc %d ref:%d %dx%d [SAR: %d:%d] %s %s "
         "%"PRId32"/%"PRId32" %d bits max_reord:%d max_dec_buf:"
         "%d\n",
         vcl_nal_count, sps->sps_id, sps->profile_idc, sps->level_idc,
         sps->poc_type, sps->ref_frame_count, sps->width, sps->height,
         /*sps->crop_left, sps->crop_right, sps->crop_top, sps->crop_bottom,*/
         sps->sar.num, sps->sar.den,
         sps->vui_parameters_present_flag ? "VUI" : "no-VUI",
         csp[sps->chroma_format_idc],
         sps->timing_info_present_flag ? sps->num_units_in_tick : 0,
         sps->timing_info_present_flag ? sps->time_scale : 0,
         sps->bit_depth_luma,
         sps->bitstream_restriction_flag ? sps->num_reorder_frames : -1,
         sps->bitstream_restriction_flag ? sps->max_dec_frame_buffering : -1);

end:
  free(buf);
  buf = NULL;
  return ret;
}

// parse H.264 slice header to get frame_num; return 0 if success, -1 otherwise
int parse_h264_slice_header(uint8_t *buf, int size_bytes, ni_logan_h264_sps_t *sps,
                            int32_t *frame_num, unsigned int *first_mb_in_slice)
{
  ni_bitstream_reader_t br;
  uint8_t *p_buf = buf;
  unsigned int slice_type, pps_id;

  // skip the start code
  while (! (p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
         size_bytes > 3)
  {
    p_buf++;
    size_bytes--;
  }
  if (size_bytes <= 3)
  {
    ni_log(NI_LOG_ERROR, "Error %s slice has no header\n", __FUNCTION__);
    return -1;
  }

  p_buf += 3;
  size_bytes -= 3;

  ni_bitstream_reader_init(&br, p_buf, 8 * size_bytes);

  // skip NAL header
  ni_bs_reader_skip_bits(&br, 8);

  *first_mb_in_slice = ni_bs_reader_get_ue(&br);
  slice_type = ni_bs_reader_get_ue(&br);
  if (slice_type > 9)
  {
    ni_log(NI_LOG_ERROR, "%s error: slice type %u too large at %u\n",
           __FUNCTION__, slice_type, *first_mb_in_slice);
    return -1;
  }
  pps_id = ni_bs_reader_get_ue(&br);
  *frame_num = ni_bs_reader_get_bits(&br, sps->log2_max_frame_num);

  ni_log(NI_LOG_TRACE, "%s slice type %u frame_num %d pps_id %u size %d first_mb %u\n",
         __FUNCTION__, slice_type, *frame_num, pps_id, size_bytes, *first_mb_in_slice);

  return 0;
}

/*!*****************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
ni_logan_retcode_t decoder_send_data(ni_logan_session_context_t* p_dec_ctx,
                                     ni_logan_session_data_io_t* p_in_data,
                                     int input_video_width,
                                     int input_video_height,
                                     int packet_size,
                                     unsigned long *total_bytes_sent,
                                     int print_time,
                                     device_state_t *p_device_state,
                                     ni_logan_h264_sps_t *sps)
{
  static int sos_flag = 1;
  static uint8_t tmp_buf[NI_LOGAN_MAX_TX_SZ] = { 0 };
  uint8_t *tmp_buf_ptr = tmp_buf;
  int chunk_size = 0;
  int frame_pkt_size = 0, nal_size;
  int nal_type = -1;
  int tx_size = 0;
  int send_size = 0;
  int new_packet = 0;
  int saved_prev_size = 0;
  int32_t frame_num = -1, curr_frame_num;
  unsigned int first_mb_in_slice = 0;
  ni_logan_packet_t * p_in_pkt =  &(p_in_data->data.packet);
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "===> %s <===\n", __FUNCTION__);

  if (p_device_state->dec_eos_sent)
  {
    ni_log(NI_LOG_TRACE, "%s: ALL data (incl. eos) sent already!\n", __FUNCTION__);
    LRETURN;
  }

  // demo usage of ni_device_dec_session_flush()
  if (dec_flush_cnt > 0 && dec_flush_pkt == p_dec_ctx->pkt_num)
  {
    ni_log(NI_LOG_INFO, "calling ni_device_dec_session_flush() at packet "
           "%" PRId64 "\n", p_dec_ctx->pkt_num);
    if (NI_LOGAN_RETCODE_SUCCESS != ni_logan_device_dec_session_flush(p_dec_ctx))
    {
      ni_log(NI_LOG_ERROR, "%s: mid-flush failed!\n", __FUNCTION__);
      exit(-1);
    }
    dec_flush_cnt--;
  }

  if (0 == p_in_pkt->data_len)
  {
    memset(p_in_pkt, 0, sizeof(ni_logan_packet_t));

    if (NI_LOGAN_CODEC_FORMAT_H264 == p_dec_ctx->codec_format)
    {
      // send whole encoded packet which ends with a slice NAL
      while ((nal_size = find_h264_next_nalu(tmp_buf_ptr, &nal_type)) > 0)
      {
        frame_pkt_size += nal_size;
        tmp_buf_ptr += nal_size;
        ni_log(NI_LOG_TRACE, "%s nal %d  nal_size %d\n", __FUNCTION__,
               nal_type, nal_size);

        // save parsed out sps/pps as stream headers in the decode session
        if (H264_NAL_PPS == nal_type)
        {
          if (NI_LOGAN_RETCODE_SUCCESS != ni_logan_device_dec_session_save_hdrs(
                p_dec_ctx, tmp_buf, frame_pkt_size))
          {
            ni_log(NI_LOG_ERROR, "%s: save_hdr failed!\n", __FUNCTION__);
          }
        }

        if (H264_NAL_SLICE == nal_type || H264_NAL_IDR_SLICE == nal_type)
        {
          if (! parse_h264_slice_header(tmp_buf_ptr - nal_size, nal_size, sps,
                                        &curr_frame_num, &first_mb_in_slice))
          {
            if (-1 == frame_num)
            {
              // first slice, continue to check
              frame_num = curr_frame_num;
            }
            else if (curr_frame_num != frame_num || 0 == first_mb_in_slice)
            {
              // this slice has diff. frame_num or first_mb_in_slice addr is
              // 0: not the same frame and return
              rewind_data_buf_pos_by(nal_size);
              frame_pkt_size -= nal_size;
              break;
            }
            // this slice is in the same frame, so continue to check and see
            // if there is more
          }
          else
          {
            ni_log(NI_LOG_ERROR, "%s: parse_slice_header error NAL type %d "
                   "size %d, continue\n", __FUNCTION__, nal_type, nal_size);
          }
        }
        else if (-1 != frame_num)
        {
          // already got a slice and this is non-slice NAL: return
          rewind_data_buf_pos_by(nal_size);
          frame_pkt_size -= nal_size;
          break;
        }
        // otherwise continue until a slice is found
      } // while there is still NAL
    }
    else
    {
      frame_pkt_size = chunk_size = read_next_chunk(tmp_buf, packet_size);
    }
    ni_log(NI_LOG_TRACE, "%s * frame_pkt_size %d\n", __FUNCTION__, frame_pkt_size);

    p_in_pkt->p_data = NULL;
    p_in_pkt->data_len = frame_pkt_size;

    if (frame_pkt_size + p_dec_ctx->prev_size > 0)
    {
      ni_logan_packet_buffer_alloc(p_in_pkt, frame_pkt_size + p_dec_ctx->prev_size);
    }

    new_packet = 1;
    send_size = frame_pkt_size + p_dec_ctx->prev_size;
    saved_prev_size = p_dec_ctx->prev_size;
  }
  else
  {
    send_size = p_in_pkt->data_len;
  }

  p_in_pkt->start_of_stream = sos_flag;
  p_in_pkt->end_of_stream = 0;
  p_in_pkt->video_width = input_video_width;
  p_in_pkt->video_height = input_video_height;
  if (sos_flag)
  {
    sos_flag = 0;
  }

  if (send_size == 0)
  {
    if (new_packet)
    {
      send_size = ni_logan_packet_copy(p_in_pkt->p_data, tmp_buf, 0,
                                 p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
      // todo save offset
    }
    p_in_pkt->data_len = send_size;

    if (--g_file_loop > 0)
    {
      reset_data_buf_pos();
      return 0;
    }
    else
    {
      p_in_pkt->end_of_stream = 1;
      ni_log(NI_LOG_TRACE, "%s sending p_last packet (size %d) + eos\n",
             __FUNCTION__, p_in_pkt->data_len);
    }
  }
  else
  {
    if (new_packet)
    {
      send_size = ni_logan_packet_copy(p_in_pkt->p_data, tmp_buf, frame_pkt_size,
                                 p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
      // todo: update offset with send_size
      // p_in_pkt->data_len is the actual packet size to be sent to decoder
      p_in_pkt->data_len += saved_prev_size;
    }
  }

  tx_size = ni_logan_device_session_write(p_dec_ctx, p_in_data,
                                    NI_LOGAN_DEVICE_TYPE_DECODER);

  if (tx_size < 0)
  {
    // Error
    fprintf(stderr, "Error: sending data error. rc:%d\n", tx_size);
    retval = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }
  else if (tx_size == 0)
  {
    ni_log(NI_LOG_TRACE, "0 byte sent this time, sleep and will re-try.\n");
    ni_logan_usleep(10000);
  }
  else if (tx_size < send_size)
  {
    if (print_time)
    {
      //printf("Sent %d < %d , re-try next time ?\n", tx_size, send_size);
    }
  }

  *total_bytes_sent += tx_size;

  if (p_dec_ctx->ready_to_close)
  {
    p_device_state->dec_eos_sent = 1;
  }

  if (print_time)
  {
    printf("%s: success, total sent: %ld\n", __FUNCTION__, *total_bytes_sent);
  }

  if (tx_size > 0)
  {
    ni_log(NI_LOG_TRACE, "%s: reset packet_buffer.\n", __FUNCTION__);
    ni_logan_packet_buffer_free(p_in_pkt);
  }

  retval = NI_LOGAN_RETCODE_SUCCESS;

  END:

  return retval;

}

/*!*****************************************************************************
 *  \brief  Receive decoded output data from decoder
 *
 *  \param  
 *
 *  \return 0: got YUV frame;  1: end-of-stream;  2: got nothing
 ******************************************************************************/
int decoder_receive_data(ni_logan_session_context_t* p_dec_ctx,
                         ni_logan_session_data_io_t* p_out_data,
                         int output_video_width,
                         int output_video_height,
                         FILE* p_file,
                         unsigned long long *total_bytes_received,
                         int print_time,
                         device_state_t *p_device_state)
{

  int rc = NI_LOGAN_RETCODE_FAILURE;
  int end_flag = 0;
  int rx_size = 0;
  ni_logan_frame_t * p_out_frame =  &(p_out_data->data.frame);
  int width, height;

  ni_log(NI_LOG_TRACE, "===> %s <===\n", __FUNCTION__);

  if (p_device_state->dec_eos_received)
  {
    ni_log(NI_LOG_TRACE, "%s eos received already, Done!\n", __FUNCTION__);
    rc = 2;
    LRETURN;
  }

  // prepare memory buffer for receiving decoded frame
  width = p_dec_ctx->active_video_width > 0 ?
  p_dec_ctx->active_video_width : output_video_width;
  height = p_dec_ctx->active_video_height > 0 ?
  p_dec_ctx->active_video_height : output_video_height;

  // allocate memory only after resolution is known (for buffer pool set up)
  int alloc_mem = (p_dec_ctx->active_video_width > 0 &&
                   p_dec_ctx->active_video_height > 0 ? 1 : 0);
  rc = ni_logan_decoder_frame_buffer_alloc(
    p_dec_ctx->dec_fme_buf_pool, &(p_out_data->data.frame), alloc_mem,
    width, height,
    p_dec_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
    p_dec_ctx->bit_depth_factor);

  if (NI_LOGAN_RETCODE_SUCCESS != rc)
  {
      LRETURN;
  }

  rx_size = ni_logan_device_session_read(p_dec_ctx, p_out_data,
                                   NI_LOGAN_DEVICE_TYPE_DECODER);

  end_flag = p_out_frame->end_of_stream;

  if (rx_size < 0)
  {
    // Error
    fprintf(stderr, "Error: receiving data error. rc:%d\n", rx_size);
    ni_logan_decoder_frame_buffer_free(&(p_out_data->data.frame));
    rc =  NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }
  else if (rx_size > 0)
  {
    number_of_frames++;
    ni_log(NI_LOG_TRACE, "Got frame # %"PRIu64" bytes %d\n",
           p_dec_ctx->frame_num, rx_size);

    ni_logan_dec_retrieve_aux_data(p_out_frame);
  }
  // rx_size == 0 means no decoded frame is available now

  if (rx_size > 0 && p_file)
  {
    int i, j;
    for (i = 0; i < 3; i++)
    {
      uint8_t *src = p_out_frame->p_data[i];
      int plane_height = p_dec_ctx->active_video_height;
      int plane_width = p_dec_ctx->active_video_width;
      int write_height = output_video_height;
      int write_width = output_video_width;
      if (i == 1 || i == 2)
      {
        plane_height /= 2;
        plane_width /= 2;
        write_height /= 2;
        write_width /= 2;
      }

      // support for 8/10 bit depth
      plane_width *= p_dec_ctx->bit_depth_factor;
      write_width *= p_dec_ctx->bit_depth_factor;

      // apply the cropping windown in writing out the YUV frame
      // for now the windown is usually crop-left = crop-top = 0, and we use
      // this to simplify the cropping logic
      for (j = 0; j < plane_height; j++)
      {
        if (j < write_height && fwrite(src, write_width, 1, p_file) != 1)
        {
          fprintf(stderr, "Error: writing data plane %d: height %d error!\n",
                  i, plane_height);
          fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
        }
        src += plane_width;
      }
    }
    if (fflush(p_file))
    {
      fprintf(stderr, "Error: writing data frame flush failed! errno %d\n", errno);
    }
  }

  *total_bytes_received += rx_size;

  if (print_time)
  {
    printf("[R] Got:%d  Frames= %u  fps=%lu  Total bytes %llu\n", rx_size,
           number_of_frames, (unsigned long) (number_of_frames/
           (current_time.tv_sec - start_time.tv_sec)),
           (unsigned long long) *total_bytes_received);
  }

  if (end_flag)
  {
    printf("Decoder Receiving done.\n");
    p_device_state->dec_eos_received = 1;
    rc = 1;
  }
  else if (0 == rx_size)
  {
    rc = 2;
  }

  ni_log(NI_LOG_TRACE, "%s: success\n", __FUNCTION__);

  END:

  return rc;
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from input file
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int encoder_send_data(ni_logan_session_context_t* p_enc_ctx,
                      ni_logan_session_data_io_t* p_in_data,
                      int input_video_width,
                      int input_video_height,
                      unsigned long *bytes_sent,
                      device_state_t *p_device_state)
{
  static uint8_t tmp_buf[MAX_YUV_FRAME_SIZE];
  volatile static int started = 0;
  volatile static int need_to_resend = 0;
  int frame_size = input_video_width * input_video_height * 3 *
  p_enc_ctx->bit_depth_factor / 2;
  int chunk_size;
  int oneSent;
  ni_logan_frame_t * p_in_frame =  &(p_in_data->data.frame);
  
  ni_log(NI_LOG_TRACE, "===> %s <===\n", __FUNCTION__);

  if (p_device_state->enc_eos_sent == 1)
  {
    ni_log(NI_LOG_TRACE, "%s: ALL data (incl. eos) sent already!\n", __FUNCTION__);
    return 0;
  }

  if (need_to_resend)
  {
    goto send_frame;
  }

  chunk_size = read_next_chunk(tmp_buf, frame_size);

  p_in_frame->start_of_stream = 0;
  if (! started)
  {
    started = 1;
    p_in_frame->start_of_stream = 1;
  }
  p_in_frame->end_of_stream = 0;
  p_in_frame->force_key_frame = 0;
  if (chunk_size == 0)
  {
    p_in_frame->end_of_stream = 1;
    ni_log(NI_LOG_TRACE, "%s: read chunk size 0, eos!\n", __FUNCTION__);
  }
  p_in_frame->video_width = input_video_width;
  p_in_frame->video_height = input_video_height;

  // only metadata header for now
  p_in_frame->extra_data_len = NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE;

  int dst_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS] = {0};
  int dst_height_aligned[NI_LOGAN_MAX_NUM_DATA_POINTERS] = {0};
  ni_logan_get_hw_yuv420p_dim(input_video_width, input_video_height,
                        p_enc_ctx->bit_depth_factor,
                        p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
                        dst_stride, dst_height_aligned);

  ni_logan_encoder_frame_buffer_alloc(p_in_frame, input_video_width,
                                input_video_height,
                                dst_stride,
                                p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
                                p_in_frame->extra_data_len,
                                p_enc_ctx->bit_depth_factor);
  if (! p_in_frame->p_data[0])
  {
    fprintf(stderr, "Error: could not allocate YUV frame buffer!");
    return -1;
  }

  ni_log(NI_LOG_TRACE, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
         "dst height aligned = %d/%d/%d  \n",
         dst_stride[0], dst_stride[1], dst_stride[2], input_video_height,
         dst_height_aligned[0], dst_height_aligned[1], dst_height_aligned[2]);
  
  uint8_t *p_src[NI_LOGAN_MAX_NUM_DATA_POINTERS] = { NULL };
  int src_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS] = { 0 };
  int src_height[NI_LOGAN_MAX_NUM_DATA_POINTERS] = { 0 };

  src_stride[0] = input_video_width * p_enc_ctx->bit_depth_factor;
  src_stride[1] =
  src_stride[2] = src_stride[0] / 2;
  src_height[0] = input_video_height;
  src_height[1] =
  src_height[2] = src_height[0] / 2;
  p_src[0] = tmp_buf;
  p_src[1] = tmp_buf + src_stride[0] * src_height[0];
  p_src[2] = p_src[1] + src_stride[1] * src_height[1];

  ni_logan_copy_hw_yuv420p((uint8_t **)(p_in_frame->p_data), p_src,
                     input_video_width, input_video_height,
                     p_enc_ctx->bit_depth_factor,
                     dst_stride, dst_height_aligned,
                     src_stride, src_height);

send_frame:  oneSent = ni_logan_device_session_write(
  p_enc_ctx, p_in_data, NI_LOGAN_DEVICE_TYPE_ENCODER);
  if (oneSent < 0)
  {
    fprintf(stderr, "Error: failed ni_logan_device_session_write() for encoder\n");
    need_to_resend = 1;
    return -1;
  }
  else if (oneSent == 0 && ! p_enc_ctx->ready_to_close)
  {
    need_to_resend = 1;
  }
  else
  {
    need_to_resend = 0;

    *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] +
    p_in_frame->data_len[2];
    ni_log(NI_LOG_TRACE, "%s: total sent data size=%lu\n",
           __FUNCTION__, *bytes_sent);
    ni_log(NI_LOG_TRACE, "%s: success\n", __FUNCTION__);

    if (p_enc_ctx->ready_to_close)
    {
      p_device_state->enc_eos_sent = 1;
    }

  }

  return 0;
}

/*******************************************************************************
 *  @brief  Send encoder input data, directly after receiving from decoder
 *
 *  @param  p_enc_ctx encoder context
 *          p_dec_ctx decoder context
 *          p_dec_out_data frame returned by decoder
 *          p_enc_in_data  frame to be sent to encoder
 *
 *  @return
 ******************************************************************************/
int encoder_send_data2(ni_logan_session_context_t* p_enc_ctx,
                       ni_logan_session_context_t* p_dec_ctx,
                       ni_logan_session_data_io_t* p_dec_out_data,
                       ni_logan_session_data_io_t* p_enc_in_data,
                       int input_video_width, int input_video_height,
                       unsigned long *bytes_sent,
                       device_state_t *p_device_state)
{
  volatile static int started = 0;
  volatile static int need_to_resend_2 = 0;
  int oneSent;
  // pointer to data struct to be sent
  ni_logan_session_data_io_t* p_to_send = NULL;
  // frame pointer to data frame struct to be sent
  ni_logan_frame_t * p_in_frame = NULL;
  ni_logan_encoder_params_t *api_params =
  (ni_logan_encoder_params_t *)p_enc_ctx->p_session_config;

  ni_log(NI_LOG_TRACE, "===> %s <===\n", __FUNCTION__);

  if (p_device_state->enc_eos_sent == 1)
  {
    ni_log(NI_LOG_TRACE, "%s: ALL data (incl. eos) sent already!\n", __FUNCTION__);
    return 1;
  }

  if (need_to_resend_2)
  {
    goto send_frame;
  }

  // if the source and target are of the same codec type, AND there is no
  // other aux data such as close caption, HDR10 etc, AND no padding required
  // (e.g. in the case of 32x32 transcoding that needs padding to 256x128),
  // then reuse the YUV frame data layout passed in because it's already in
  // the required format
  if (p_enc_ctx->codec_format == p_dec_ctx->codec_format &&
      input_video_width >= NI_LOGAN_MIN_WIDTH &&
      input_video_height >= NI_LOGAN_MIN_HEIGHT &&
      ! p_dec_out_data->data.frame.sei_hdr_content_light_level_info_len &&
      ! p_dec_out_data->data.frame.sei_hdr_mastering_display_color_vol_len &&
      ! p_dec_out_data->data.frame.sei_hdr_plus_len &&
      ! p_dec_out_data->data.frame.sei_cc_len &&
      ! p_dec_out_data->data.frame.sei_user_data_unreg_len &&
      ! p_dec_out_data->data.frame.roi_len)
  {
    ni_log(NI_LOG_TRACE, "%s: encoding to the same codec format as the "
           "source: %d, NO SEI, reusing the frame struct!\n",
           __FUNCTION__, p_enc_ctx->codec_format);
    p_to_send = p_dec_out_data;
    p_in_frame = &(p_to_send->data.frame);

    p_in_frame->force_key_frame = 0;

    p_in_frame->sei_total_len
    = p_in_frame->sei_cc_offset = p_in_frame->sei_cc_len
    = p_in_frame->sei_hdr_mastering_display_color_vol_offset
    = p_in_frame->sei_hdr_mastering_display_color_vol_len
    = p_in_frame->sei_hdr_content_light_level_info_offset
    = p_in_frame->sei_hdr_content_light_level_info_len
    = p_in_frame->sei_hdr_plus_offset
    = p_in_frame->sei_hdr_plus_len = 0;

    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->force_pic_qp = 0;
    p_in_frame->extra_data_len = NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE;
    p_in_frame->ni_logan_pict_type = 0;
  }
  else
  {
    // otherwise have to pad/crop the source and copy to a new frame struct
    // and prep for the SEI aux data
    p_to_send = p_enc_in_data;
    p_in_frame = &(p_to_send->data.frame);
    p_in_frame->extra_data_len = NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE;
    p_in_frame->end_of_stream = p_dec_out_data->data.frame.end_of_stream;
    p_in_frame->ni_logan_pict_type = 0;

    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->force_pic_qp = 0;

    int dst_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_LOGAN_MAX_NUM_DATA_POINTERS] = {0};
    ni_logan_get_hw_yuv420p_dim(input_video_width, input_video_height,
                          p_enc_ctx->bit_depth_factor,
                          p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
                          dst_stride, dst_height_aligned);

    int should_send_sei_with_frame = ni_logan_should_send_sei_with_frame(
      p_enc_ctx, p_in_frame->ni_logan_pict_type, api_params);

    // data buffer for various SEI: HDR mastering display color volume, HDR
    // content light level, close caption, User data unregistered, HDR10+ etc.
    uint8_t mdcv_data[NI_LOGAN_MAX_SEI_DATA];
    uint8_t cll_data[NI_LOGAN_MAX_SEI_DATA];
    uint8_t cc_data[NI_LOGAN_MAX_SEI_DATA];
    uint8_t udu_data[NI_LOGAN_MAX_SEI_DATA];
    uint8_t hdrp_data[NI_LOGAN_MAX_SEI_DATA];

    // prep for auxiliary data (various SEI, ROI) in p_in_frame, based on the
    // data returned in decoded frame
    ni_logan_enc_prep_aux_data(p_enc_ctx, p_in_frame, &(p_dec_out_data->data.frame),
                         p_enc_ctx->codec_format, should_send_sei_with_frame,
                         mdcv_data, cll_data, cc_data, udu_data, hdrp_data);

    p_in_frame->extra_data_len += p_in_frame->sei_total_len;

    // layout requirement: leave space for reconfig data if SEI or ROI present
    if ((p_in_frame->sei_total_len || p_in_frame->roi_len) &&
        ! p_in_frame->reconf_len)
    {
      p_in_frame->extra_data_len += sizeof(ni_logan_encoder_change_params_t);
    }

    ni_logan_encoder_frame_buffer_alloc(p_in_frame, input_video_width,
                                  input_video_height,
                                  dst_stride,
                                  p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
                                  p_in_frame->extra_data_len,
                                  p_enc_ctx->bit_depth_factor);
    if (! p_in_frame->p_data[0])
    {
      fprintf(stderr, "Error: cannot allocate YUV frame buffer!");
      return -1;
    }

    ni_log(NI_LOG_TRACE, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
           "dst height aligned = %d/%d/%d force_key_frame=%d, extra_data_len=%d"
           " sei_size=%u (hdr_content_light_level %u hdr_mastering_display_"
           "color_vol %u hdr10+ %u hrd %u) reconf_size=%u roi_size=%u "
           "force_pic_qp=%u udu_sei_size=%u "
           "use_cur_src_as_long_term_pic %u use_long_term_ref %u \n",
           dst_stride[0], dst_stride[1], dst_stride[2], input_video_height,
           dst_height_aligned[0], dst_height_aligned[1], dst_height_aligned[2],
           p_in_frame->force_key_frame, p_in_frame->extra_data_len,
           p_in_frame->sei_total_len,
           p_in_frame->sei_hdr_content_light_level_info_len,
           p_in_frame->sei_hdr_mastering_display_color_vol_len,
           p_in_frame->sei_hdr_plus_len, 0, /* hrd is 0 size for now */
           p_in_frame->reconf_len, p_in_frame->roi_len,
           p_in_frame->force_pic_qp, p_in_frame->sei_user_data_unreg_len,
           p_in_frame->use_cur_src_as_long_term_pic,
           p_in_frame->use_long_term_ref);

    uint8_t *p_src[NI_LOGAN_MAX_NUM_DATA_POINTERS];
    int src_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS];
    int src_height[NI_LOGAN_MAX_NUM_DATA_POINTERS];

    src_stride[0] = p_dec_out_data->data.frame.data_len[0] /
    p_dec_out_data->data.frame.video_height;
    src_stride[1] =
    src_stride[2] = src_stride[0] / 2;

    p_src[0] = p_dec_out_data->data.frame.p_data[0];
    p_src[1] = p_dec_out_data->data.frame.p_data[1];
    p_src[2] = p_dec_out_data->data.frame.p_data[2];
    src_height[0] = p_dec_out_data->data.frame.video_height;
    src_height[1] =
    src_height[2] = src_height[0] / 2;

    // YUV part of the encoder input data layout
    ni_logan_copy_hw_yuv420p((uint8_t **)(p_in_frame->p_data), p_src,
                       input_video_width, input_video_height,
                       p_enc_ctx->bit_depth_factor,
                       dst_stride, dst_height_aligned,
                       src_stride, src_height);

    // auxiliary data part of the encoder input data layout
    ni_logan_enc_copy_aux_data(p_enc_ctx, p_in_frame, &(p_dec_out_data->data.frame),
                         p_enc_ctx->codec_format, mdcv_data, cll_data, cc_data,
                         udu_data, hdrp_data);
  }

  p_in_frame->video_width = input_video_width;
  p_in_frame->video_height = input_video_height;

  p_in_frame->start_of_stream = 0;
  if (! started)
  {
    started = 1;
    p_in_frame->start_of_stream = 1;
  }
  // p_in_frame->end_of_stream = 0;

send_frame:
  oneSent = p_in_frame->data_len[0] + p_in_frame->data_len[1] + 
  p_in_frame->data_len[2];

  if (oneSent > 0 || p_in_frame->end_of_stream)
  {
    oneSent = ni_logan_device_session_write(p_enc_ctx, p_to_send, NI_LOGAN_DEVICE_TYPE_ENCODER);
    p_in_frame->end_of_stream = 0;
  }
  else
  {
    goto end_encoder_send_data2;
  }

  if (oneSent < 0) {
    fprintf(stderr, "Error: %s\n", __FUNCTION__);
    need_to_resend_2 = 1;
    return -1;
  }
  else if (oneSent == 0)
  {
    if (p_device_state->enc_eos_sent == 0 && p_enc_ctx->ready_to_close)
    {
      need_to_resend_2 = 0;
      p_device_state->enc_eos_sent = 1;
    }
    else
    {
      need_to_resend_2 = 1;
    }
  }
  else
  {
    need_to_resend_2 = 0;
    if (p_enc_ctx->ready_to_close)
    {
      p_device_state->enc_eos_sent = 1;
    }
    ni_log(NI_LOG_TRACE, "%s: success\n", __FUNCTION__);
  }

end_encoder_send_data2: 
  return 0;
}


/*!*****************************************************************************
 *  \brief  Receive output data from encoder
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int encoder_receive_data(ni_logan_session_context_t* p_enc_ctx,
                         ni_logan_session_data_io_t* p_out_data,
                         int output_video_width, int output_video_height,
                         FILE* p_file,
                         unsigned long long *total_bytes_received,
                         int print_time)
{
  int packet_size = NI_LOGAN_MAX_TX_SZ;
  int rc = 0;
  int end_flag = 0;
  int rx_size = 0;
  ni_logan_packet_t * p_out_pkt =  &(p_out_data->data.packet);
  int meta_size = NI_LOGAN_FW_ENC_BITSTREAM_META_DATA_SIZE;

  ni_log(NI_LOG_TRACE, "===> %s <===\n", __FUNCTION__);

  if (NI_LOGAN_INVALID_SESSION_ID == p_enc_ctx->session_id ||
      NI_INVALID_DEVICE_HANDLE == p_enc_ctx->blk_io_handle)
  {
    ni_log(NI_LOG_TRACE, "encode session not opened yet, return\n");
    return 0;
  }

  ni_logan_packet_buffer_alloc(p_out_pkt, packet_size);

receive_data:  rc = ni_logan_device_session_read(p_enc_ctx, p_out_data,
                                           NI_LOGAN_DEVICE_TYPE_ENCODER);

  end_flag = p_out_pkt->end_of_stream;
  rx_size = rc;

  ni_log(NI_LOG_TRACE, "%s: received data size=%d\n", __FUNCTION__, rx_size);

  if (rx_size > meta_size)
  {
    if (p_file && (fwrite((uint8_t*)p_out_pkt->p_data + meta_size, 
                          p_out_pkt->data_len - meta_size, 1, p_file) != 1))
    {
      fprintf(stderr, "Error: writing data %d bytes error!\n",
              p_out_pkt->data_len - meta_size);
      fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
    }

    *total_bytes_received += rx_size - meta_size;

    if (0 == p_enc_ctx->pkt_num)
    {
      p_enc_ctx->pkt_num = 1;
      ni_log(NI_LOG_TRACE, "got encoded stream header, keep reading ..\n");
      goto receive_data;
    }
    number_of_packets++;

    ni_log(NI_LOG_TRACE, "Got:   Packets= %u\n", number_of_packets);
  }
  else if (rx_size != 0)
  {
    fprintf(stderr, "Error: received %d bytes, <= metadata size %d!\n",
           rx_size, meta_size);
  }
  else if (rx_size == 0 && ! end_flag && ((ni_logan_encoder_params_t*)(
    p_enc_ctx->p_session_config))->low_delay_mode)
  {
    ni_log(NI_LOG_TRACE, "low delay mode and NO pkt, keep reading ..\n");
    goto receive_data;
  }

  if (print_time)
  {
    int timeDiff = current_time.tv_sec - start_time.tv_sec;
    if (timeDiff == 0)
    {
      timeDiff = 1;
    }
    printf("[R] Got:%d   Packets= %u fps=%d  Total bytes %lld\n",
           rx_size, number_of_packets, number_of_packets/timeDiff,
           *total_bytes_received);
  }

  if (end_flag)
  {
    printf("Encoder Receiving done.\n");
    return 1;
  }

  ni_log(NI_LOG_TRACE, "%s: success\n", __FUNCTION__);

  return 0;
}

/*!*****************************************************************************
 *  \brief  Encoder session open
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_open_session(ni_logan_session_context_t* p_enc_ctx, int dst_codec_format,
                         int iXcoderGUID, ni_logan_encoder_params_t *p_enc_params,
                         int src_bit_depth, int width, int height,
                         ni_hrd_params_t *hrd_params,
                         ni_color_primaries_t color_primaries,
                         ni_color_transfer_characteristic_t color_trc,
                         ni_color_space_t color_space,
                         int video_full_range_flag,
                         int sar_num, int sar_den)
{
  int ret = 0;

  p_enc_ctx->p_session_config = p_enc_params;
  p_enc_ctx->session_id = NI_LOGAN_INVALID_SESSION_ID;
  p_enc_ctx->codec_format = dst_codec_format;

  // assign the card GUID in the encoder context and let session open
  // take care of the rest
  p_enc_ctx->device_handle = p_enc_ctx->blk_io_handle =
  NI_INVALID_DEVICE_HANDLE;
  p_enc_ctx->hw_id = iXcoderGUID;

  // default: little endian
  p_enc_ctx->src_bit_depth = src_bit_depth;
  p_enc_ctx->src_endian = NI_LOGAN_FRAME_LITTLE_ENDIAN;
  p_enc_ctx->bit_depth_factor = 1;
  if (10 == p_enc_ctx->src_bit_depth)
  {
    p_enc_ctx->bit_depth_factor = 2;
  }

  int linesize_aligned = ((width + 7) / 8) * 8;
  if (p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264)
  {
    linesize_aligned = ((width + 15) / 16) * 16;
  }
  if (linesize_aligned < NI_LOGAN_MIN_WIDTH)
  {
    p_enc_params->hevc_enc_params.conf_win_right += NI_LOGAN_MIN_WIDTH - width;
    linesize_aligned = NI_LOGAN_MIN_WIDTH;
  }
  else if (linesize_aligned > width)
  {
    p_enc_params->hevc_enc_params.conf_win_right += linesize_aligned - width;
  }
  p_enc_params->source_width = linesize_aligned;

  int height_aligned = ((height + 7) / 8) * 8;
  if (p_enc_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264)
  {
    height_aligned = ((height + 15) / 16) * 16;
  }
  if (height_aligned < NI_LOGAN_MIN_HEIGHT)
  {
    p_enc_params->hevc_enc_params.conf_win_bottom += NI_LOGAN_MIN_HEIGHT - height;
    height_aligned = NI_LOGAN_MIN_HEIGHT;
  }
  else if (height_aligned > height)
  {
    p_enc_params->hevc_enc_params.conf_win_bottom += height_aligned - height;
  }
  p_enc_params->source_height = height_aligned;

  // VUI setting including color setting
  ni_logan_set_vui(p_enc_params, color_primaries, color_trc, color_space,
             video_full_range_flag, sar_num, sar_den, dst_codec_format,
             hrd_params);

  ret = ni_logan_device_session_open(p_enc_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER);
  if (ret < 0)
  {
    fprintf(stderr, "Error: %s failure!\n", __FUNCTION__);
  }
  else
  {
    printf("Encoder device %d session open successful.\n", iXcoderGUID);
  }
  return ret;
}

void print_usage(void)
{
  printf("Video decoder/encoder/transcoder application directly using Netint Libxcoder API v%s\n"
         "Usage: xcoder [options]\n"
         "\n"
         "options:\n"
         "-h | --help             Show help.\n"
         "-l | --loglevel         Set loglevel of libxcoder API.\n"
         "                        [none, fatal, error, info, debug, trace]\n"
         "                        (Default: info)\n"
         "-c | --card             Set card index to use.\n"
         "                        See `ni_logan_rsrc_mon` for cards on system.\n"
         "                        (Default: 0)\n"
         "-i | --input            Input file path.\n"
         "-s | --size             Resolution of input file in format WIDTHxHEIGHT.\n"
         "                        (eg. '1920x1080')\n"
         "-m | --mode             Input to output codec processing mode in format:\n"
         "                        INTYPE2OUTTYPE. [a2y, h2y, y2a, y2h, a2a, a2h, h2a, h2h]\n"
         "                        Type notation: y=YUV420P a=AVC, h=HEVC\n"
         "-b | --bitdepth         Input and output bit depth. [8, 10]\n"
         "                        (Default: 8)\n"
         "-x | --xcoder-params    Encoding params. See \"Encoding Parameters\" chapter in\n"
         "                        IntegrationProgrammingGuideT408_T432_FW*.pdf for help.\n"
         "                        (Default: \"\")\n"
         "-o | --output           Output file path.\n"
         "-a | --dec_async        Decoding in asynchronous multi-threading.\n"
         "-e | --dec_async        Encoding in asynchronous multi-threading.\n"
         "-t | --dec_flush_sec    Flush decoder at the specified second\n"
         "-f | --dec_flush        Flush decoder at the specific packet index. Prefix\n"
         "                        number with 'r' to repeatedly flush at packet period.\n"
         "                        (eg. r20)\n", NI_LOGAN_XCODER_REVISION);
}


// retrieve key and value from 'key=value' pair, return 0 if successful
// otherwise non-0
static int get_key_value(char *p_str, char *key, char *value)
{
  if (! p_str || ! key || ! value)
  {
    return 1;
  }

  char *p = strchr(p_str, '=');
  if (! p)
  {
    return 1;
  }
  else
  {
    *p = '\0';
    key[0] = '\0';
    value[0] = '\0';
    strncat(key, p_str, strlen(p_str));
    strncat(value, p+1, strlen(p+1));
    return 0;
  }
}

/* Convert string of log_level to log_level integer */
static int32_t log_str_to_level(char *log_str)
{
  ni_log_level_t converted_log_level;
  size_t i;
  for (i = 0; i < strlen(log_str); i++)
    log_str[i] = tolower((unsigned char) log_str[i]);

  if (strcmp(log_str, "none") == 0)
    converted_log_level = NI_LOG_NONE;
  else if (strcmp(log_str, "fatal") == 0)
    converted_log_level = NI_LOG_FATAL;
  else if (strcmp(log_str, "error") == 0)
    converted_log_level = NI_LOG_ERROR;
  else if (strcmp(log_str, "info") == 0)
    converted_log_level = NI_LOG_INFO;
  else if (strcmp(log_str, "debug") == 0)
    converted_log_level = NI_LOG_DEBUG;
  else if (strcmp(log_str, "trace") == 0)
    converted_log_level = NI_LOG_TRACE;
  else
    converted_log_level = -16;
  return converted_log_level;
}

// retrieve config parameter valus from --xcoder-params,
// return 0 if successful, -1 otherwise
static int retrieve_xcoder_params(char xcoderParams[],
                                  ni_logan_encoder_params_t *params,
                                  ni_logan_session_context_t *ctx)
{
  char key[64], value[64];
  char *p = xcoderParams;
  char *curr = xcoderParams, *colon_pos;
  int ret = 0;

  while (*curr)
  {
    colon_pos = strchr(curr, ':');

    if (colon_pos)
    {
      *colon_pos = '\0';
    }

    if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
        get_key_value(curr, key, value))
    {
      fprintf(stderr, "Error: xcoder-params p_config key/value not "
              "retrieved: %s\n", curr);
      ret = -1;
      break;
    }
    ret = ni_logan_encoder_params_set_value(params, key, value, ctx);
    switch (ret)
    {
    case NI_LOGAN_RETCODE_PARAM_INVALID_NAME:
      fprintf(stderr, "Error: unknown option: %s.\n", key);
      break;
    case NI_LOGAN_RETCODE_PARAM_INVALID_VALUE:
      fprintf(stderr, "Error: invalid value for %s: %s.\n", key, value);
      break;
    default:
      break;
    }

    if (NI_LOGAN_RETCODE_SUCCESS != ret)
    {
      fprintf(stderr, "Error: config parsing failed %d: %s\n", ret,
              ni_logan_get_rc_txt(ret));
      break;
    }

    if (colon_pos)
    {
      curr = colon_pos + 1;
    }
    else
    {
      curr += strlen(curr);
    }
  }
  return ret;
}

// Decoder send thread
static void *decoder_send_routine(void *arg)
{
  dev_send_param_t *p_param = arg;

  for (; ;)
  {
    int ret = decoder_send_data(p_param->p_ctx, p_param->p_data, p_param->input_video_width,
        p_param->input_video_height, p_param->input_size, p_param->p_total_bytes_sent,
        p_param->print_time, p_param->p_xcodeState, p_param->p_SPS);
    if (ret < 0)
    {
      ni_log(NI_LOG_ERROR, "Error: %s failed: %x\n", __FUNCTION__, p_param->p_ctx->session_id);
    }
    else if (p_param->p_xcodeState->dec_eos_sent)
    {
      ni_logan_packet_buffer_free(&p_param->p_data->data.packet);
      break;
    }
  }

  ni_log(NI_LOG_DEBUG, "Exit %s\n", __FUNCTION__);
  return NULL;
}

// Decoder receive thread
static void *decoder_receive_routine(void *arg)
{
  dev_recv_param_t *p_param = arg;
  ni_logan_frame_t *p_frm = &p_param->p_data->data.frame;

  for (; ;)
  {
    int ret = decoder_receive_data(p_param->p_ctx, p_param->p_data,
        p_param->output_video_width, p_param->output_video_height,
        p_param->p_file, p_param->p_total_bytes_received,
        p_param->print_time, p_param->p_xcodeState);
    ni_logan_decoder_frame_buffer_free(p_frm);
    if (ret == 2)
    {
      // EAGAIN
      ni_logan_usleep(1000);
    }
    else if (ret < 0 || p_frm->end_of_stream)
    {
      if (ret == 1)
      {
        // Over.
        break;
      }
      else
      {
        //ni_log(NI_LOG_ERROR, "Error %s failed: %x\n", __FUNCTION__, p_param->p_ctx->session_id);
      }
    }
  }

  ni_log(NI_LOG_DEBUG, "Exit %s\n", __FUNCTION__);
  return NULL;
}

// Decoder flush thread
static void *decoder_flush_routine(void *arg)
{
  dev_send_param_t *p_param = arg;

  while (!flush_fin_flag)
  {
    ni_logan_usleep(dec_flush_sec * 1000 * 1000);

    int ret = ni_logan_device_dec_session_flush(p_param->p_ctx);
    if (ret < 0)
    {
      ni_log(NI_LOG_ERROR, "Error %s failed: %x\n", __FUNCTION__, p_param->p_ctx->session_id);
      ret = ni_logan_device_session_close(p_param->p_ctx, 0, NI_LOGAN_DEVICE_TYPE_DECODER);
      break;
    }
  }

  ni_logan_device_session_flush(p_param->p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
  ni_log(NI_LOG_DEBUG, "Exit %s\n", __FUNCTION__);
  return NULL;
}

// Encoder send thread
static void *encoder_send_routine(void *arg)
{
  dev_send_param_t *p_param = arg;

  for (; ;)
  {
    int ret = encoder_send_data(p_param->p_ctx, p_param->p_data,
        p_param->input_video_width, p_param->input_video_height,
        p_param->p_total_bytes_sent, p_param->p_xcodeState);
    if (ret < 0)
    {
      ni_log(NI_LOG_ERROR, "Error: %s failed: %x\n", __FUNCTION__, p_param->p_ctx->session_id);
    }
    else if (p_param->p_xcodeState->enc_eos_sent)
    {
      // Over
      break;
    }
  }

  ni_log(NI_LOG_DEBUG, "Exit %s\n", __FUNCTION__);
  return NULL;
}

// Encoder receive thread
static void *encoder_receive_routine(void *arg)
{
  dev_recv_param_t *p_param = arg;
  ni_logan_packet_t *p_pkt = &p_param->p_data->data.packet;

  for (; ;)
  {
    int ret = encoder_receive_data(p_param->p_ctx, p_param->p_data,
      p_param->output_video_width, p_param->output_video_height,
      p_param->p_file, p_param->p_total_bytes_received, p_param->print_time);
    if (ret)
    {
      if (ret > 0)
      {
        // Over.
        break;
      }
      else
      {
        ni_log(NI_LOG_ERROR, "Error %s failed: %x\n", __FUNCTION__, p_param->p_ctx->session_id);
      }
    }
  }

  ni_log(NI_LOG_DEBUG, "Exit %s\n", __FUNCTION__);
  return NULL;
}

/*!*****************************************************************************
 *  \brief  main 
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int main(int argc, char *argv[])
{
  tx_data_t sdPara = {0};
  rx_data_t rcPara = {0};
  device_state_t xcodeState = {0};
  int err = 0, pfs = 0, sos_flag = 0, edFlag = 0, bytes_sent = 0;
  char xcoderGUID[32];
  int iXcoderGUID = 0;
  uint32_t result=0;
  unsigned long total_bytes_sent;
  unsigned long long total_bytes_received;
  unsigned long long xcodeRecvTotal;
  FILE *p_file = NULL;
  char *n;      // used for parsing width and height from --size
  char mode_description[128];
  int input_video_width;
  int input_video_height;
  int arg_width = 0;
  int arg_height = 0;
  int mode = -1;
  size_t i;
  int pkt_size;
  int dec_async = 0;
  int enc_async = 0;
  ni_logan_encoder_params_t  api_param;
  ni_logan_encoder_params_t  dec_api_param;
  char encConfXcoderParams[2048] = { 0 };
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE, dev_handle_1 = NI_INVALID_DEVICE_HANDLE;
  int src_codec_format = 0, dst_codec_format = 0;
  ni_log_level_t loglevel = NI_LOG_ERROR;
  int bit_depth = 8;
  ni_logan_h264_sps_t SPS = {0}; // input header SPS

  // Input arg handling
  int opt;
  int opt_index;
  const char *opt_string = "aehl:c:f:i:s:m:b:o:p:t:x:";
  static struct option long_options[] =
  {
    {"help",           no_argument,       NULL, 'h'},
    {"dec_async",      no_argument,       NULL, 'a'},
    {"enc_async",      no_argument,       NULL, 'e'},
    {"loglevel",       required_argument, NULL, 'l'},
    {"card",           required_argument, NULL, 'c'},
    {"input",          required_argument, NULL, 'i'},
    {"size",           required_argument, NULL, 's'},
    {"mode",           required_argument, NULL, 'm'},
    {"bitdepth",       required_argument, NULL, 'b'},
    {"xcoder-params",  required_argument, NULL, 'x'},
    {"output",         required_argument, NULL, 'o'},
    {"dec_flush",      required_argument, NULL, 'f'},
    {"dec_flush_sec",  required_argument, NULL, 't'},
    {"loop",           required_argument, NULL, 'p'},
    {NULL,             0,                 NULL,   0},
  };

  while ((opt = getopt_long(argc, argv, opt_string, long_options, &opt_index)) != -1)
  {
    switch (opt)
    {
      case 'h':
        print_usage();
        exit(0);
      case 'a':
        dec_async = 1;
        break;
      case 'e':
        enc_async = 1;
        break;
      case 'l':
        loglevel = log_str_to_level(optarg);
        if (loglevel < NI_LOG_NONE || loglevel > NI_LOG_TRACE)
          arg_error_exit("-l | --loglevel", optarg);
        ni_log_set_level(loglevel);
        break;
      case 'c':
        strcpy(xcoderGUID, optarg);
        iXcoderGUID = strtol(optarg, &n, 10);
        if (n == xcoderGUID)  // no numeric characters found in left side of optarg
          arg_error_exit("-c | --card", optarg);
        break;
      case 'i':
        strcpy(sdPara.fileName, optarg);
        break;
      case 's':
        arg_width = (int) strtol(optarg, &n, 10);
        arg_height = atoi(n + 1);
        if ((*n != 'x') || (!arg_width || !arg_height))
          arg_error_exit("-s | --size", optarg);
        break;
      case 'm':
        if (!(strlen(optarg) == 3))
          arg_error_exit("-, | --mode", optarg);
        // convert to lower case for processing
        for (i = 0; i < strlen(optarg); i++)
          optarg[i] = tolower((unsigned char) optarg[i]);

        if (strcmp(optarg, "y2a") &&
            strcmp(optarg, "y2h") &&
            strcmp(optarg, "a2y") &&
            strcmp(optarg, "a2a") &&
            strcmp(optarg, "a2h") &&
            strcmp(optarg, "h2y") &&
            strcmp(optarg, "h2a") &&
            strcmp(optarg, "h2h"))
          arg_error_exit("-, | --mode", optarg);
        
        // determine dec/enc/xcod mode to use
        if (optarg[0] == 'y')
        {
          sprintf(mode_description, "Encoding");
          mode = XCODER_APP_ENCODE;
        }
        else if (optarg[2] == 'y')
        {
          sprintf(mode_description, "Decoding");
          mode = XCODER_APP_DECODE;
        }
        else if ((optarg[0] == 'y') && (optarg[2] == 'y'))
        {
          arg_error_exit("-, | --mode", optarg);
        }
        else
        {
          sprintf(mode_description, "Transcoding");
          mode = XCODER_APP_TRANSCODE;
        }

        // determine codecs to use
        if (optarg[0] == 'a')
        {
          src_codec_format = NI_LOGAN_CODEC_FORMAT_H264;
          strcat(mode_description, " from AVC");
        }
        if (optarg[0] == 'h')
        {
          src_codec_format = NI_LOGAN_CODEC_FORMAT_H265;
          strcat(mode_description, " from HEVC");
        }
        if (optarg[2] == 'a')
        {
          dst_codec_format = NI_LOGAN_CODEC_FORMAT_H264;
          strcat(mode_description, " to AVC");
        }
        if (optarg[2] == 'h')
        {
          dst_codec_format = NI_LOGAN_CODEC_FORMAT_H265;
          strcat(mode_description, " to HEVC");
        }

        break;
      case 'b':
        if (!(atoi(optarg) == 8 || atoi(optarg) == 10))
          arg_error_exit("-b | --bitdepth", optarg);
        bit_depth = atoi(optarg);
        break;
      case 'p':
        g_file_loop = atoi(optarg);
        break;
      case 'x':
        strcpy(encConfXcoderParams, optarg);
        break;
      case 'o':
        strcpy(rcPara.fileName, optarg);
        break;
      case 't':
        dec_flush_sec = atoi(optarg);
        if (dec_flush_sec < 0)
          arg_error_exit("-t | --dec_flush_sec", optarg);
        break;
      case 'f':
        if (optarg[0] == 'r')
        {
          dec_flush_cnt = -1;
          dec_flush_pkt = atoi(optarg + 1);
        }
        else
        {
          dec_flush_cnt = 1;
          dec_flush_pkt = atoi(optarg);
        }
        if (dec_flush_pkt < 0)
          arg_error_exit("-f | --dec_flush", optarg);
        break;
      default:
        print_usage();
        exit(1);
    }
  }
  
  // Check required args are present
  if (!sdPara.fileName[0])
  {
    printf("Error: missing argument for -i | --input\n");
    exit(-1);
  }
  if ((mode != XCODER_APP_TRANSCODE) && (mode != XCODER_APP_DECODE) && (mode != XCODER_APP_ENCODE))
  {
    printf("Error: missing argument for -m | --mode\n");
    exit(-1);
  }
  if (!rcPara.fileName[0])
  {
    printf("Error: missing argument for -o | --output\n");
    exit(-1);
  }
  
  sdPara.mode = mode;
  rcPara.mode = mode;

  // Print high-level description of processing to occur and codecs involved
  printf("%s...\n", mode_description);

  pkt_size = 131040; // hardcoded input data chunk size (for H.265)


#ifdef _WIN32
  pfs = open(sdPara.fileName, O_RDONLY | O_BINARY);
#elif __linux__
  pfs = open(sdPara.fileName, O_RDONLY);
#endif

  if (pfs < 0)
  {
    fprintf(stderr, "Error: cannot open %s\n", sdPara.fileName);
    fprintf(stderr, "Error: input file read failure\n");
    err_flag = 1;
    goto end;
  }
  printf("SUCCESS: Opened input file: %s with file id = %d\n", sdPara.fileName, pfs);

  lseek(pfs, 0, SEEK_END);
  total_file_size = lseek(pfs, 0, SEEK_CUR);
  lseek(pfs, 0, SEEK_SET);
  unsigned long tmpFileSize = total_file_size;
  // try to allocate memory for input file buffer, quit if failure
  if (! (g_file_cache = malloc(total_file_size)))
  {
    fprintf(stderr, "Error: input file size %lu exceeding max malloc, quit\n",
            total_file_size);
    goto end;
  }
  g_curr_cache_pos = g_file_cache;

  printf("Reading %lu bytes in total ..\n", total_file_size);
  while (tmpFileSize)
  {
    uint32_t chunk_size = tmpFileSize > 4096 ? 4096 : tmpFileSize;
    int one_read_size = read(pfs, g_curr_cache_pos, tmpFileSize);
    if (one_read_size == -1)
    {
      fprintf(stderr, "Error: reading file, quit! left-to-read %lu\n", tmpFileSize);
      goto end;
    }
    else
    {
      tmpFileSize -= one_read_size;
      g_curr_cache_pos += one_read_size;
    }
  }
  printf("read %lu bytes from input file into memory\n", total_file_size);
  
  g_curr_cache_pos = g_file_cache;
  data_left_size = total_file_size;

  if (strcmp(rcPara.fileName, "null"))
  {
    p_file = fopen(rcPara.fileName, "wb");
    if (p_file == NULL)
    {
      fprintf(stderr, "Error: cannot open %s\n", rcPara.fileName);
      err_flag = 1;
      goto end;
    }
  }
  printf("SUCCESS: Opened output file: %s\n", rcPara.fileName);

  // for H.264, probe the source and use the probed source info as defaults
  if (NI_LOGAN_CODEC_FORMAT_H264 == src_codec_format &&
      (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE))
  {
    if (probe_h264_stream_info(&SPS))
    {
      fprintf(stderr, "ERROR: H.264 file probing complete, source file format "
              "not supported !\n");
      goto end;
    }

    ni_log(NI_LOG_INFO, "Using probed H.264 source info: %d bits "
           "resolution %dx%d\n", SPS.bit_depth_luma, SPS.width, SPS.height);
    bit_depth = SPS.bit_depth_luma;
    arg_width = SPS.width;
    arg_height = SPS.height;
  }

  sdPara.arg_width = arg_width;
  sdPara.arg_height = arg_height;
  rcPara.arg_width = arg_width;
  rcPara.arg_height = arg_height;

  // set up decoder p_config with some hard coded numbers
  if (ni_logan_decoder_init_default_params(&dec_api_param, 25, 1, 200000,
                                     arg_width, arg_height) < 0)
  {
    fprintf(stderr, "Error: decoder p_config set up error\n");
    return -1;
  }

  send_fin_flag = 0;
  receive_fin_flag = 0;

  ni_logan_session_context_t dec_ctx = {0};
  ni_logan_session_context_t enc_ctx = {0};

  dec_ctx.keep_alive_timeout = enc_ctx.keep_alive_timeout = 10;

  ni_logan_device_session_context_init(&dec_ctx);
  ni_logan_device_session_context_init(&enc_ctx);

  sdPara.p_dec_ctx = (void *) &dec_ctx;
  sdPara.p_enc_ctx = (void *) &enc_ctx;
  rcPara.p_dec_ctx = (void *) &dec_ctx;
  rcPara.p_enc_ctx = (void *) &enc_ctx;

  enc_ctx.nb_rois = 0;
  enc_ctx.roi_side_data_size = 0;
  enc_ctx.nb_rois = 0;
  enc_ctx.roi_side_data_size = 0;
  enc_ctx.av_rois = NULL;
  enc_ctx.codec_format = dst_codec_format;

  if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE)
  { 
    dec_ctx.p_session_config = NULL;
    dec_ctx.session_id = NI_LOGAN_INVALID_SESSION_ID;
    dec_ctx.codec_format = src_codec_format;

    // no need to directly allocate resource context
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

    // assign the card GUID in the decoder context and let session open
    // take care of the rest
    dec_ctx.device_handle = dec_ctx.blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    dec_ctx.hw_id = iXcoderGUID;

    dec_ctx.p_session_config = &dec_api_param;
    // default: little endian
    dec_ctx.src_bit_depth = bit_depth;
    dec_ctx.src_endian = NI_LOGAN_FRAME_LITTLE_ENDIAN;
    dec_ctx.bit_depth_factor = 1;
    if (10 == dec_ctx.src_bit_depth)
    {
      dec_ctx.bit_depth_factor = 2;
    }

    err = ni_logan_device_session_open(&dec_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
    if (err < 0)
    {
      fprintf(stderr, "Error: ni_logan_decoder_session_open() failure!\n");
      return -1;
    }
    else
    {
      printf("Decoder device %d session open successful.\n", iXcoderGUID);
    }
  }

  if (mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_ENCODE)
  {
    // no need to directly allocate resource context for encoder
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;
  }

  // encoder session open, if needed, will be at the first frame arrival as it
  // carries source stream info that may be useful in encoding config

  sos_flag = 1;
  edFlag = 0;
  bytes_sent = 0;
  total_bytes_received = 0;
  xcodeRecvTotal = 0;
  total_bytes_sent = 0;

  printf("user video resolution: %dx%d\n", arg_width, arg_height);
  if (arg_width == 0 || arg_height == 0)
  {
    input_video_width = 1280;
    input_video_height = 720;
  }
  else
  {
    input_video_width = arg_width;
    input_video_height = arg_height;
  }
  int output_video_width = input_video_width;
  int output_video_height = input_video_height;


  (void) ni_logan_gettimeofday(&start_time, NULL);
  (void) ni_logan_gettimeofday(&previous_time, NULL);
  (void) ni_logan_gettimeofday(&current_time, NULL);
  start_timestamp = privious_timestamp = current_timestamp = time(NULL);



#if 0
#ifdef __linux__
  struct timespec start, end;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
#endif
#endif


  if (mode == XCODER_APP_DECODE)
  {
    ni_logan_session_data_io_t in_pkt = {0};
    ni_logan_session_data_io_t out_frame = {0};

    printf("Decoding Mode: %dx%d to %dx%d\n",
           input_video_width, input_video_height,
           output_video_width, output_video_height);
    
    if (dec_async)
    {
      // As for multi-threading decoding, three threads are created for sending
      // receiving and dec_flushing. When the dec_flush trigger the flush and
      // close functions the send/recv functions would return failure and also
      // call the close funtion according to the client scenario. Fortunately
      // the internal session context mutex will assure the thread safety.
      // After all the threads terminate, the ni_logan_device_session_context_clear
      // function shall be called to destroy the mutex.
      ni_pthread_t send_tid, recv_tid, flush_tid;
      dev_send_param_t send_param = {0};
      dev_recv_param_t recv_param = {0};

      send_param.p_ctx = &dec_ctx;
      send_param.p_data = &in_pkt;
      send_param.input_video_width = input_video_width;
      send_param.input_video_height = input_video_height;
      send_param.input_size = pkt_size;
      send_param.print_time = 0;
      send_param.p_total_bytes_sent = &total_bytes_sent;
      send_param.p_xcodeState = &xcodeState;
      send_param.p_SPS = &SPS;

      recv_param.p_ctx = &dec_ctx;
      recv_param.p_data = &out_frame;
      recv_param.output_video_width = output_video_width;
      recv_param.output_video_height = output_video_height;
      recv_param.print_time = 0;
      recv_param.p_file = p_file;
      recv_param.p_total_bytes_received = &total_bytes_received;
      recv_param.p_xcodeState = &xcodeState;

      if (ni_logan_pthread_create(&send_tid, NULL, decoder_send_routine, &send_param))
      {
        fprintf(stderr, "create send pkt thread failed in decoder mode\n");
        return -1;
      }

      if (ni_logan_pthread_create(&recv_tid, NULL, decoder_receive_routine, &recv_param))
      {
        fprintf(stderr, "create recv frame thread failed in decoder mode\n");
        return -1;
      }

      if (dec_flush_sec > 0 &&
          ni_logan_pthread_create(&flush_tid, NULL, decoder_flush_routine, &send_param))
      {
        fprintf(stderr, "create flush frame thread failed in decoder mode\n");
        return -1;
      }

      ni_logan_pthread_join(send_tid, NULL);
      ni_logan_pthread_join(recv_tid, NULL);
      if (dec_flush_sec > 0)
      {
        flush_fin_flag = 1;
        ni_logan_pthread_join(flush_tid, NULL);
      }
    }
    else
    {
      while (send_fin_flag == 0 || receive_fin_flag == 0)
      {

        (void) ni_logan_gettimeofday(&current_time, NULL);
        int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

        // Sending
        send_fin_flag = decoder_send_data(
          &dec_ctx, &in_pkt, input_video_width, input_video_height,
          pkt_size, &total_bytes_sent, print_time, &xcodeState, &SPS);
        sos_flag = 0;
        if (send_fin_flag < 0)
        {
          fprintf(stderr, "Error: decoder_send_data() failed, rc: %d\n",
                  send_fin_flag);
          break;
        }
      
        // Receiving
        receive_fin_flag  = decoder_receive_data(
          &dec_ctx, &out_frame, output_video_width, output_video_height,
          p_file, &total_bytes_received, print_time, &xcodeState);

        ni_logan_decoder_frame_buffer_free(&(out_frame.data.frame));
        if (print_time)
        {
          previous_time = current_time;
        }

        // Error or eos
        if (receive_fin_flag < 0 || out_frame.data.frame.end_of_stream)
        {
          break;
        }
      }
    } 

    int time_diff = current_time.tv_sec - start_time.tv_sec;
    if (time_diff == 0)
      time_diff = 1;

    printf("[R] Got:  Frames= %u  fps=%d  Total bytes %llu\n", 
           number_of_frames, number_of_frames/time_diff, total_bytes_received);

    ni_logan_device_session_close(&dec_ctx, 1, NI_LOGAN_DEVICE_TYPE_DECODER);
    ni_logan_device_session_context_clear(&dec_ctx);
    ni_logan_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

    ni_logan_packet_buffer_free(&in_pkt.data.packet);
    ni_logan_decoder_frame_buffer_free(&out_frame.data.frame);
  }
  else if (mode == XCODER_APP_ENCODE)
  {
    printf("Encoding Mode: %dx%d to %dx%d\n",
           input_video_width, input_video_height,
           output_video_width, output_video_height);

    // set up encoder p_config, using some hard coded numbers
    if (ni_logan_encoder_init_default_params(&api_param, 30, 1, 200000,
                                       arg_width, arg_height) < 0)
    {
      fprintf(stderr, "Error: encoder init default set up error\n");
      return -1;
    }

    // check and set ni_logan_encoder_params from --xcoder-params
    if (retrieve_xcoder_params(encConfXcoderParams, &api_param, &enc_ctx))
    {
      fprintf(stderr, "Error: encoder p_config parsing error\n");
      return -1;
    }

    ni_logan_session_data_io_t in_frame = {0};
    ni_logan_session_data_io_t out_packet = {0};
    ni_hrd_params_t hrd_params;
    int video_full_range_flag = 0;

    if (api_param.video_full_range_flag >= 0)
    {
      ni_log(NI_LOG_TRACE, "Using user-configured video_full_range_flag "
             "%d\n", api_param.video_full_range_flag);
      video_full_range_flag = api_param.video_full_range_flag;
    }

    // for encode from YUV, use all the parameters specified by user
    if (encoder_open_session(&enc_ctx, dst_codec_format, iXcoderGUID,
                             &api_param, bit_depth, arg_width, arg_height,
                             &hrd_params, api_param.color_primaries,
                             api_param.color_transfer_characteristic,
                             api_param.color_space,
                             video_full_range_flag,
                             api_param.sar_num, api_param.sar_denom))
    {
      goto end;
    }

    if (enc_async)
    {
      dev_send_param_t send_param = {0};
      dev_recv_param_t recv_param = {0};
      ni_pthread_t send_tid, recv_tid;

      send_param.p_ctx = &enc_ctx;
      send_param.p_data = &in_frame;
      send_param.input_video_width = input_video_width;
      send_param.input_video_height = input_video_height;
      send_param.print_time = 0;
      send_param.p_total_bytes_sent = &total_bytes_sent;
      send_param.p_xcodeState = &xcodeState;

      recv_param.p_ctx = &enc_ctx;
      recv_param.p_data = &out_packet;
      recv_param.output_video_width = output_video_width;
      recv_param.output_video_height = output_video_height;
      recv_param.print_time = 0;
      recv_param.p_file = p_file;
      recv_param.p_total_bytes_received = &total_bytes_received;
      recv_param.p_xcodeState = &xcodeState;

      if (ni_logan_pthread_create(&send_tid, NULL, encoder_send_routine, &send_param))
      {
        fprintf(stderr, "create send frame thread failed in encoder mode\n");
        return -1;
      }

      if (ni_logan_pthread_create(&recv_tid, NULL, encoder_receive_routine, &recv_param))
      {
        fprintf(stderr, "create recv pkt thread failed in encoder mode\n");
        return -1;
      }

      ni_logan_pthread_join(send_tid, NULL);
      ni_logan_pthread_join(recv_tid, NULL);
    }
    else
    {
      while (send_fin_flag == 0 || receive_fin_flag == 0)
      {
        (void) ni_logan_gettimeofday(&current_time, NULL);
        int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

        // Sending
        send_fin_flag = encoder_send_data(
          &enc_ctx, &in_frame, input_video_width, input_video_height,
          &total_bytes_sent, &xcodeState);
        sos_flag = 0;
        if (send_fin_flag == 2) //Error
        {
          break;
        }
      
        // Receiving
        receive_fin_flag = encoder_receive_data(
          &enc_ctx, &out_packet, output_video_width, output_video_height,
          p_file, &total_bytes_received, print_time);

        if (print_time)
        {
          previous_time = current_time;
        }

        // Error or eos
        if (receive_fin_flag == 2 || out_packet.data.packet.end_of_stream)
        {
          break;
        }
      }
    }

    int timeDiff = current_time.tv_sec - start_time.tv_sec;
    if (timeDiff == 0)
    {
      timeDiff = 1;
    }

    printf("[R] Got:  Packets= %u fps=%d  Total bytes %lld\n",
           number_of_packets, number_of_packets/timeDiff, total_bytes_received);

    ni_logan_device_session_close(&enc_ctx, 1, NI_LOGAN_DEVICE_TYPE_ENCODER);
    ni_logan_device_session_context_clear(&enc_ctx);
    ni_logan_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;

    ni_logan_frame_buffer_free(&in_frame.data.frame);
    ni_logan_packet_buffer_free(&out_packet.data.packet);
  }
  else if (mode == XCODER_APP_TRANSCODE)
  {
    printf("Xcoding Mode: %dx%d to %dx%d\n", input_video_width, 
           input_video_height, output_video_width, output_video_height);

    ni_logan_session_data_io_t in_pkt = {0};
    ni_logan_session_data_io_t out_frame = {0};
    ni_logan_session_data_io_t enc_in_frame = {0};
    ni_logan_session_data_io_t out_packet = {0};
    
    while (send_fin_flag == 0 || receive_fin_flag == 0)
    {
      (void) ni_logan_gettimeofday(&current_time, NULL);
      int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

      // bitstream Sending
      send_fin_flag = decoder_send_data(
        &dec_ctx, &in_pkt, input_video_width, input_video_height, pkt_size,
        &total_bytes_sent, print_time, &xcodeState, &SPS);

      sos_flag = 0;
      if (send_fin_flag == 2) //Error
      {
        break;
      }

      // YUV Receiving: not writing to file
      receive_fin_flag  = decoder_receive_data(
        &dec_ctx, &out_frame, output_video_width, output_video_height,
        p_file, &total_bytes_received, print_time, &xcodeState);

      if (print_time)
      {
        previous_time = current_time;
      }

      if (2 == receive_fin_flag)
      {
        ni_log(NI_LOG_TRACE, "no decoder output, jump to encoder receive!\n");
        ni_logan_decoder_frame_buffer_free(&(out_frame.data.frame));
        goto encode_recv;
      }
      else if (NI_LOGAN_INVALID_SESSION_ID == enc_ctx.session_id ||
               NI_INVALID_DEVICE_HANDLE == enc_ctx.blk_io_handle)
      {
        // open the encode session when the first frame arrives and the session
        // is not opened yet, with the source stream and user-configured encode
        // info both considered when constructing VUI in the stream headers
        int color_pri = out_frame.data.frame.color_primaries;
        int color_trc = out_frame.data.frame.color_trc;
        int color_space = out_frame.data.frame.color_space;
        int video_full_range_flag = out_frame.data.frame.video_full_range_flag;
        int sar_num = out_frame.data.frame.sar_width;
        int sar_den = out_frame.data.frame.sar_height;
        int fps_num = 0, fps_den = 0;

        // calculate the source fps and set it as the default target fps, based
        // on the timing_info passed in from the decoded frame
        if (out_frame.data.frame.vui_num_units_in_tick &&
            out_frame.data.frame.vui_time_scale)
        {
          if (NI_LOGAN_CODEC_FORMAT_H264 == out_frame.data.frame.src_codec)
          {
            if (0 == (out_frame.data.frame.vui_time_scale % 2))
            {
              fps_num = out_frame.data.frame.vui_time_scale / 2;
              fps_den = out_frame.data.frame.vui_num_units_in_tick;
            }
            else
            {
              fps_num = out_frame.data.frame.vui_time_scale;
              fps_den = 2 * out_frame.data.frame.vui_num_units_in_tick;
            }
          }
          else if (NI_LOGAN_CODEC_FORMAT_H265 == out_frame.data.frame.src_codec)
          {
            fps_num = out_frame.data.frame.vui_time_scale;
            fps_den = out_frame.data.frame.vui_num_units_in_tick;
          }
        }

        // set up encoder p_config, using some info from source
        if (ni_logan_encoder_init_default_params(&api_param, fps_num, fps_den, 200000,
                                           arg_width, arg_height) < 0)
        {
          fprintf(stderr, "Error: encoder init default set up error\n");
          break;
        }

        // check and set ni_logan_encoder_params from --xcoder-params
        // Note: the parameter setting has to be in this order so that user
        //       configured values can overwrite the source/default ones if
        //       desired.
        if (retrieve_xcoder_params(encConfXcoderParams, &api_param, &enc_ctx))
        {
          fprintf(stderr, "Error: encoder p_config parsing error\n");
          break;
        }

        if (color_pri != api_param.color_primaries &&
            NI_COL_PRI_UNSPECIFIED != api_param.color_primaries)
        {
          ni_log(NI_LOG_TRACE, "Using user-configured color primaries %d to "
                 "overwrite source %d\n", api_param.color_primaries, color_pri);
          color_pri = api_param.color_primaries;
        }
        if (color_trc != api_param.color_transfer_characteristic &&
            NI_COL_TRC_UNSPECIFIED != api_param.color_transfer_characteristic)
        {
          ni_log(NI_LOG_TRACE, "Using user-configured color trc %d to overwrite"
                 " source %d\n",
                 api_param.color_transfer_characteristic, color_trc);
          color_trc = api_param.color_transfer_characteristic;
        }
        if (color_space != api_param.color_space &&
            NI_COL_SPC_UNSPECIFIED != api_param.color_space)
        {
          ni_log(NI_LOG_TRACE, "Using user-configured color space %d to "
                 "overwrite source %d\n", api_param.color_space, color_space);
          color_space = api_param.color_space;
        }
        if (api_param.video_full_range_flag >= 0)
        {
          ni_log(NI_LOG_TRACE, "Using user-configured video_full_range_flag "
                 "%d\n", api_param.video_full_range_flag);
          video_full_range_flag = api_param.video_full_range_flag;
        }
        if (out_frame.data.frame.aspect_ratio_idc > 0 &&
            out_frame.data.frame.aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
        {
          sar_num = ni_h264_pixel_aspect_list[
            out_frame.data.frame.aspect_ratio_idc].num;
          sar_den = ni_h264_pixel_aspect_list[
            out_frame.data.frame.aspect_ratio_idc].den;
        }

        ni_hrd_params_t hrd_params;

        if (encoder_open_session(&enc_ctx, dst_codec_format, iXcoderGUID,
                                 &api_param, bit_depth, arg_width, arg_height,
                                 &hrd_params, color_pri, color_trc, color_space,
                                 video_full_range_flag,
                                 sar_num, sar_den))
        {
          ni_log(NI_LOG_ERROR, "Error: encoder_open_session failed, stop!\n");
          break;
        }
      }

      // YUV Sending
      send_fin_flag = encoder_send_data2(
        &enc_ctx, &dec_ctx, &out_frame, &enc_in_frame,
        input_video_width, input_video_height,
        &total_bytes_sent, &xcodeState);
      sos_flag = 0;
      if (send_fin_flag == 2) //Error
      {
        ni_logan_decoder_frame_buffer_free(&(out_frame.data.frame));
        break;
      }

      ni_logan_decoder_frame_buffer_free(&(out_frame.data.frame));

      // encoded bitstream Receiving
encode_recv:
      receive_fin_flag  = encoder_receive_data(
        &enc_ctx, &out_packet, output_video_width, output_video_height,
        p_file, &xcodeRecvTotal, print_time);

      if (print_time)
      {
        previous_time = current_time;
      }

      // Error or encoder eos
      if (receive_fin_flag == 2 || out_packet.data.packet.end_of_stream)
      {
        break;
      }
    }

    int time_diff = current_time.tv_sec - start_time.tv_sec;
    if (time_diff == 0)
      time_diff = 1;

    printf("[R] Got:  Frames= %u  fps=%d  Total bytes %llu\n", 
           number_of_frames, number_of_frames/time_diff, total_bytes_received);
    printf("[R] Got:  Packets= %u fps=%d  Total bytes %llu\n", 
           number_of_packets, number_of_packets/time_diff, xcodeRecvTotal);

    ni_logan_device_session_close(&dec_ctx, 1, NI_LOGAN_DEVICE_TYPE_DECODER);

    ni_logan_device_session_context_clear(&dec_ctx);
    ni_logan_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

    ni_logan_packet_buffer_free(&(in_pkt.data.packet));
    ni_logan_frame_buffer_free(&(out_frame.data.frame));

    ni_logan_device_session_close(&enc_ctx, 1, NI_LOGAN_DEVICE_TYPE_ENCODER);

    ni_logan_device_session_context_clear(&enc_ctx);
    ni_logan_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;

    ni_logan_frame_buffer_free(&(enc_in_frame.data.frame));
    ni_logan_packet_buffer_free(&(out_packet.data.packet));
  }

end:  close(pfs);
  if (p_file)
  {
    fclose(p_file);
  }

  free(g_file_cache);
  g_file_cache = NULL;

  printf("All Done.\n");

  return 0;
}
