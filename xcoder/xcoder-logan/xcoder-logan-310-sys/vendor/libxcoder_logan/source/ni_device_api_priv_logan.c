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
*  \file   ni_device_api_priv_logan.c
*
*  \brief  Private functions used by main ni_device_api file
*
*******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <sys/types.h>
#endif
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <utime.h>
#include <signal.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <dirent.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "ni_nvme_logan.h"
#include "ni_device_api_logan.h"
#include "ni_device_api_priv_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_util_logan.h"
#include "ni_av_codec_logan.h"

#ifdef XCODER_SIGNATURE_FILE
// this file has an array defined for signature content: ni_logan_session_sign
#include "../build/xcoder_signature_headers.h"
#endif

typedef enum _ni_logan_t35_sei_mesg_type
{
  NI_LOGAN_T35_SEI_CLOSED_CAPTION = 0,
  NI_LOGAN_T35_SEI_HDR10_PLUS = 1
} ni_logan_t35_sei_mesg_type_t;

static uint8_t g_itu_t_t35_cc_sei_hdr_hevc[NI_CC_SEI_HDR_HEVC_LEN] =
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x4e,
  0x01, // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
  // nuh_layer_id=0 nuh_temporal_id_plus1=1)
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0 + 11, // payLoadSize= ui16Len + 11; to be set (index 7)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  0x00,
  0x31, //  itu_t_t35_provider_code = 49
  0x47, 0x41, 0x39,
  0x34, // ATSC_user_identifier = "GA94"
  0x03, // ATSC1_data_user_data_type_code=3
  0 | 0xc0, // (ui16Len/3) | 0xc0 (to be set; index 16) (each CC character
  //is 3 bytes)
  0xFF  // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_hevc[NI_HDR10P_SEI_HDR_HEVC_LEN] =
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x4e,
  0x01, // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
  // nuh_layer_id=0 nuh_temporal_id_plus1=1)
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0x00, // payLoadSize; to be set (index 7)
  0xb5, //  u8 itu_t_t35_country_code =181 (North America)
  //0x00,
  //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
  //0x00,
  //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
  // payLoadSize count starts from itu_t_t35_provider_code and goes until
  // and including trailer
};

static uint8_t g_itu_t_t35_cc_sei_hdr_h264[NI_CC_SEI_HDR_H264_LEN] =
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x06, // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0 + 11, // payLoadSize= ui16Len + 11; to be set (index 6)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  0x00,
  0x31, //  itu_t_t35_provider_code = 49
  0x47, 0x41, 0x39,
  0x34, // ATSC_user_identifier = "GA94"
  0x03, // ATSC1_data_user_data_type_code=3
  0 | 0xc0, // (ui16Len/3) | 0xc0 (to be set; index 15) (each CC character
  //is 3 bytes)
  0xFF  // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_h264[NI_HDR10P_SEI_HDR_H264_LEN] =
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x06, // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0x00, // payLoadSize; to be set (index 6)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  //0x00,
  //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
  //0x00,
  //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
  // payLoadSize count starts from itu_t_t35_provider_code and goes until
  // and including trailer
};

static uint8_t g_sei_trailer[NI_CC_SEI_TRAILER_LEN] =
{
  0xFF, // marker_bits = 255
  0x80  // RBSP trailing bits - rbsp_stop_one_bit and 7 rbsp_alignment_zero_bit
};

#define NI_LOGAN_XCODER_FAILURES_MAX 25

#ifdef _WIN32
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)               \
{                                                                    \
  ni_logan_instance_status_info_t err_rc_info = { 0 };                  \
  int err_rc = ni_logan_query_status_info(ctx, type, &err_rc_info, rc, opcode); \
  rc = err_rc_info.inst_err_no;                                 \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_LOGAN_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      (tmp_rc = ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u " \
           "inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}

#define CHECK_ERR_RC2(ctx, rc, info, opcode, type, hw_id, inst_id)   \
{                                                                    \
  rc = info.inst_err_no;                                    \
  if (info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_LOGAN_RETCODE_FAILURE; \
  if (info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      (tmp_rc = ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u " \
           "inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           info.sess_err_no, info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}
#elif __linux__ || __APPLE__
static struct stat g_nvme_stat = {0};

#ifdef XCODER_SELF_KILL_ERR
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)           \
{                                                                    \
  ni_logan_instance_status_info_t err_rc_info = { 0 };                     \
  int err_rc = ni_logan_query_status_info(ctx, type, &err_rc_info, rc, opcode);    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)) {  \
    ni_log(NI_LOG_INFO, "Terminating due to persistent failures, %s() line-%d: session_no 0x%x "  \
           "sess_err_no %u inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    kill(getpid(), SIGTERM);                                         \
  }                                                                  \
}

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id) \
{                                                                    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)) {  \
    ni_log(NI_LOG_INFO, "Terminating due to persistent failures, %s() line-%d: session_no 0x%x " \
           "sess_err_no %u inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    kill(getpid(), SIGTERM);                                         \
  }                                                                  \
}
#else
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)           \
{                                                                    \
  ni_logan_instance_status_info_t err_rc_info = { 0 };                     \
  int err_rc = ni_logan_query_status_info(ctx, type, &err_rc_info, rc, opcode);    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_LOGAN_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      (tmp_rc = ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u " \
           "inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id)  \
{                                                                    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_LOGAN_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_LOGAN_XCODER_FAILURES_MAX || \
      (tmp_rc = ni_logan_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u " \
           "inst_err_no %u rc_error_count: %d\n", __FUNCTION__, __LINE__, *inst_id, \
           err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}
#endif

#endif

#define CHECK_VPU_RECOVERY(ret) \
{ \
  if (NI_LOGAN_RETCODE_NVME_SC_VPU_RECOVERY == ret) { \
    ni_log(NI_LOG_TRACE, "Error, vpu reset.\n"); \
    ret = NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY; \
    LRETURN; \
  } \
}

static int rotated_array_binary_search(uint64_t *lefts, uint64_t *rights, uint32_t size, uint64_t target)
{
  int lo = 0;
  int hi = size - 1;
  while (lo <= hi)
  {
    int mid = lo + (hi - lo) / 2;
    if (lefts[mid] <= target && target < rights[mid])
    {
      return mid;
    }

    if (rights[mid] == 0)
    {
      // empty in (mid, hi)
      hi = mid - 1;
      continue;
    }

    if (rights[lo] <= rights[mid])
    {
      if (lefts[lo] <= target && target < lefts[mid])
      {
        // Elements are all monotonous in (lo, mid)
        hi = mid - 1;
      }
      else
      {
        // Rotation in (lo, mid)
        lo = mid + 1;
      }
    }
    else
    {
      if (rights[mid] < target && target < rights[hi])
      {
        // Elements are all monotonous in (lo, mid)
        lo = mid + 1;
      }
      else
      {
        // Rotation in (lo, mid)
        hi = mid - 1;
      }
    }
  }

  return -1;
}

// create folder bearing the card name (nvmeX) if not existing
// start working inside this folder: nvmeX
// find the earliest saved and/or non-existing stream folder and use it as
// the pkt saving destination; at most 32 such folders to be checked/created;
// folder name is in the format of: streamY, where Y is [1, 32]
static void decoder_dump_dir_open(ni_logan_session_context_t* p_ctx)
{
#ifdef _WIN32
#elif __linux__  || __APPLE__
  FILE *fp;
  char dir_name[128] = { 0 };
  char file_name[128] = { 0 };
  ni_logan_device_context_t *p_device_context;
  DIR* dir;
  struct dirent *stream_folder;
  int curr_stream_idx = 0;
  int earliest_stream_idx = 0;
  int max_exist_idx = 0;
  time_t earliest_time = 0;
  struct stat file_stat;

  p_device_context = ni_logan_rsrc_get_device_context(p_ctx->device_type,
                                                p_ctx->hw_id);
  if (! p_device_context)
  {
    ni_log(NI_LOG_ERROR, "Error retrieve device context for decoder guid %d\n",
           p_ctx->hw_id);
    return;
  }

  flock(p_device_context->lock, LOCK_EX);

  strcpy(dir_name, &p_ctx->dev_xcoder_name[5]);
  if (0 != access(dir_name, F_OK))
  {
    if (0 != mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
    {
      ni_log(NI_LOG_ERROR, "Error create folder %s, errno %d\n",
             dir_name, errno);
    }
    else
    {
      ni_log(NI_LOG_INFO, "Created pkt folder for: %s\n", dir_name);
    }
  }

  if (NULL == (dir = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "Error %d: failed to open directory %s\n",
           NI_ERRNO, dir_name);
  }
  else
  {
    // have a quick first pass of streamY folders, and if existing Y < 32,
    // create a new folder Y+1 directly without checking existing ones content
    while ((stream_folder = readdir(dir)))
    {
      if (! strncmp(stream_folder->d_name, "stream", strlen("stream")))
      {
        curr_stream_idx = atoi(&(stream_folder->d_name[strlen("stream")]));
        if (curr_stream_idx > 0)
        {
          if (curr_stream_idx > max_exist_idx)
          {
            max_exist_idx = curr_stream_idx;
          }
          if (32 == curr_stream_idx)
          {
            break;
          }
        }
      }
    }

    // if less than 32 streams created then create a new one, otherwise have to
    // pick the stream folder that has the earliest modified file which is
    // most likely done by finished session.
    if (max_exist_idx < 32)
    {
      curr_stream_idx = max_exist_idx + 1;
    }
    else
    {
      rewinddir(dir);
      curr_stream_idx = 0;
      while ((stream_folder = readdir(dir)))
      {
        // go through each of these streamY folders and get modified time
        // of the first pkt-* file to simplify the searching
        if (! strncmp(stream_folder->d_name, "stream", strlen("stream")))
        {
          snprintf(file_name, sizeof(file_name), "%s/%s/pkt-0001.bin",
                   dir_name, stream_folder->d_name);

          curr_stream_idx = atoi(&(stream_folder->d_name[strlen("stream")]));

          if (curr_stream_idx > 0 && 0 == access(file_name, F_OK))
          {
            // just take pkt-0001 file timestamp to simplify search
            if (stat(file_name, &file_stat))
            {
              ni_log(NI_LOG_ERROR, "Error %d: failed to stat file %s\n",
                     NI_ERRNO, file_name);
            }
            else
            {
              if (0 == earliest_stream_idx ||
                  file_stat.st_mtime < earliest_time)
              {
                earliest_stream_idx = curr_stream_idx;
                earliest_time = file_stat.st_mtime;
              }
            }
          } // check first file in streamX
        } // go through each streamX folder
      } // read all files in nvmeY

      curr_stream_idx = earliest_stream_idx;

      // set the access/modified time of chosen pkt file to NOW so its stream
      // folder won't be taken by other sessions.
      snprintf(file_name, sizeof(file_name), "%s/stream%02d/pkt-0001.bin",
               dir_name, curr_stream_idx);
      if (utime(file_name, NULL))
      {
        ni_log(NI_LOG_ERROR, "Error utime %s\n", file_name);
      }
    } // 32 streams in nvmeY already
    closedir(dir);
  }

  snprintf(p_ctx->stream_dir_name, sizeof(p_ctx->stream_dir_name),
           "%s/stream%02d", dir_name, curr_stream_idx);

  if (0 != access(p_ctx->stream_dir_name, F_OK))
  {
    if (0 != mkdir(p_ctx->stream_dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
    {
      ni_log(NI_LOG_ERROR, "Error create stream folder %s, errno %d\n",
             p_ctx->stream_dir_name, errno);
    }
    else
    {
      ni_log(NI_LOG_INFO, "Created stream sub folder: %s\n",
             p_ctx->stream_dir_name);
    }
  }
  else
  {
    ni_log(NI_LOG_INFO, "Reusing stream sub folder: %s\n",
           p_ctx->stream_dir_name);
  }

  flock(p_device_context->lock, LOCK_UN);
  ni_logan_rsrc_free_device_context(p_device_context);

  snprintf(file_name, sizeof(file_name), "%s/process_session_id.txt",
           p_ctx->stream_dir_name);

  fp = fopen(file_name, "wb");
  if (fp)
  {
    char number[64] = {'\0'};
    ni_log(NI_LOG_INFO, "Decoder pkt dump log created: %s\n", file_name);
    snprintf(number, sizeof(number), "proc id: %ld\nsession id: %u\n",
             (long) getpid(), p_ctx->session_id);
    fwrite(number, strlen(number), 1, fp);
    fclose(fp);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "Error create decoder pkt dump log: %s\n", file_name);
  }
#endif
}


//Only create the dump dir
static void decoder_dump_dir_create(ni_logan_session_context_t* p_ctx)
{
#ifdef _WIN32
#elif __linux__
  FILE *fp;
  char dir_name[128] = { 0 };
  char file_name[128] = { 0 };
  ni_logan_device_context_t *p_device_context;
  DIR* dir;
  struct dirent *stream_folder;
  int curr_stream_idx = 0;
  int earliest_stream_idx = 0;
  int max_exist_idx = 0;
  time_t earliest_time = 0;
  struct stat file_stat;

  p_device_context = ni_logan_rsrc_get_device_context(p_ctx->device_type,
                                                p_ctx->hw_id);
  if (! p_device_context)
  {
    ni_log(NI_LOG_ERROR, "Error retrieve device context for decoder guid %d\n",
           p_ctx->hw_id);
    return;
  }

  flock(p_device_context->lock, LOCK_EX);

  strcpy(dir_name, &p_ctx->dev_xcoder_name[5]);
  if (0 != access(dir_name, F_OK))
  {
    if (0 != mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
    {
      ni_log(NI_LOG_ERROR, "Error create folder %s, errno %d\n",
             dir_name, errno);
    }
    else
    {
      ni_log(NI_LOG_INFO, "Created pkt folder for: %s\n", dir_name);
    }
  }

  if (NULL == (dir = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "Error %d: failed to open directory %s\n",
           NI_ERRNO, dir_name);
  }
  else
  {
    // have a quick first pass of streamY folders, and if existing Y < 32,
    // create a new folder Y+1 directly without checking existing ones content
    while ((stream_folder = readdir(dir)))
    {
      if (! strncmp(stream_folder->d_name, "stream", strlen("stream")))
      {
        curr_stream_idx = atoi(&(stream_folder->d_name[strlen("stream")]));
        if (curr_stream_idx > 0)
        {
          if (curr_stream_idx > max_exist_idx)
          {
            max_exist_idx = curr_stream_idx;
          }
          if (32 == curr_stream_idx)
          {
            break;
          }
        }
      }
    }

    // if less than 32 streams created then create a new one, otherwise have to
    // pick the stream folder that has the earliest modified file which is
    // most likely done by finished session.
    if (max_exist_idx < 32)
    {
      curr_stream_idx = max_exist_idx + 1;
    }
    else
    {
      rewinddir(dir);
      curr_stream_idx = 0;
      while ((stream_folder = readdir(dir)))
      {
        // go through each of these streamY folders and get modified time
        // of the first pkt-* file to simplify the searching
        if (! strncmp(stream_folder->d_name, "stream", strlen("stream")))
        {
          curr_stream_idx = atoi(&(stream_folder->d_name[strlen("stream")]));
          if (curr_stream_idx > 0)
          {
            if (0 == earliest_stream_idx ||
                file_stat.st_mtime < earliest_time)
            {
              earliest_stream_idx = curr_stream_idx;
              earliest_time = file_stat.st_mtime;
            }
          }
        } // go through each streamX folder
      } // read all files in nvmeY

      curr_stream_idx = earliest_stream_idx;
    } // 32 streams in nvmeY already
    closedir(dir);
  }

  snprintf(p_ctx->stream_dir_name, sizeof(p_ctx->stream_dir_name),
           "%s/stream%02d", dir_name, curr_stream_idx);

  if (0 != access(p_ctx->stream_dir_name, F_OK))
  {
    if (0 != mkdir(p_ctx->stream_dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
    {
      ni_log(NI_LOG_ERROR, "Error create stream folder %s, errno %d\n",
             p_ctx->stream_dir_name, errno);
    }
    else
    {
      ni_log(NI_LOG_INFO, "Created stream sub folder: %s\n",
             p_ctx->stream_dir_name);
    }
  }
  else
  {
    ni_log(NI_LOG_INFO, "Reusing stream sub folder: %s\n",
           p_ctx->stream_dir_name);
  }

  ni_log(NI_LOG_ERROR, "create stream folder %s, errno %d\n",
            p_ctx->stream_dir_name, errno);

  flock(p_device_context->lock, LOCK_UN);
  ni_logan_rsrc_free_device_context(p_device_context);
#endif
}

/*!******************************************************************************
 *  \brief  Open a xcoder decoder instance
 *
 *  \param
 *
 *  \return
*******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_open(ni_logan_session_context_t* p_ctx)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t model_load = 0;
  uint32_t low_delay_mode = 0;
  void *p_signature = NULL;
  uint32_t buffer_size = 0;
  void* p_buffer = NULL;
  ni_logan_decoder_session_open_info_t session_info = { 0 };
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR %s(): passed parameters are null!, return\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    int i;

    p_ctx->device_type = NI_LOGAN_DEVICE_TYPE_DECODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->lone_sei_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->required_buf_size = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;
    p_ctx->session_timestamp = 0;
    p_ctx->decoder_drop_frame_num = 0;

    for (i = 0; i < NI_LOGAN_FIFO_SZ; i++)
    {
      p_ctx->pkt_custom_sei[i] = NULL;
    }

    p_ctx->codec_total_ticks = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->p_dec_packet_inf_buf = NULL;
    struct timeval tv;
    ni_logan_gettimeofday(&tv, NULL);
    p_ctx->codec_start_time = tv.tv_sec*1000000ULL + tv.tv_usec;

#ifdef _WIN32
    p_ctx->event_handle = ni_logan_create_event();
    if (p_ctx->event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }

    p_ctx->thread_event_handle = ni_logan_create_event();
    if (p_ctx->thread_event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }
#endif

    if (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->fps_denominator != 0)
    {
      model_load = (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_width *
                   ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_height *
                   ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->fps_number) /
                   (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->fps_denominator);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "fps_denominator should not be 0 at this point\n"
                           "Setting model load with guess of 30fps\n");
      model_load = (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_width *
                    ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_height *
                    30);
      // assert(false);
    }

    ni_log(NI_LOG_TRACE, "Model load info:: W:%d H:%d F:%d :%d Load:%d",
           ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_width,
           ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->source_height,
           ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->fps_number,
           ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->fps_denominator,
           model_load);

    //malloc zero data buffer
    if(ni_logan_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc decoder all zero buffer failed\n",
             NI_ERRNO, __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //malloc decoder packet info buffer
    if(ni_logan_posix_memalign(&p_ctx->p_dec_packet_inf_buf, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d:%s() alloc decoder packet info buffer failed\n",
             NI_ERRNO, __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_dec_packet_inf_buf, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //malloc data buffer
    if(ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
             NI_ERRNO, __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_logan_get_session_id_t *)p_buffer)->session_id = NI_LOGAN_INVALID_SESSION_ID;

    // Get session ID
    ui32LBA = OPEN_GET_SID_R(0, NI_LOGAN_DEVICE_TYPE_DECODER);
    retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme read command failed, blk_io_handle:"
             "%" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    p_ctx->session_id = ni_logan_ntohl(((ni_logan_get_session_id_t *)p_buffer)->session_id);
    ni_log(NI_LOG_TRACE, "%s ID:0x%x\n", __FUNCTION__, p_ctx->session_id);
    if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): query session ID failed, "
             "p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }

    //Send session Info
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);
    session_info.codec_format = ni_logan_htonl(p_ctx->codec_format);
    session_info.model_load = ni_logan_htonl(model_load);
    if(p_ctx->decoder_low_delay != 0)
    {
      ni_log(NI_LOG_TRACE, "%s low_delay_mode %d\n", __FUNCTION__, low_delay_mode);
      low_delay_mode = 1;
    }

    session_info.low_delay_mode = ni_logan_htonl(low_delay_mode);
    session_info.hw_desc_mode = ni_logan_htonl(p_ctx->hw_action);
    session_info.set_high_priority = ni_logan_htonl(p_ctx->set_high_priority);
    memcpy(p_buffer, &session_info, sizeof(ni_logan_decoder_session_open_info_t));
    ui32LBA = OPEN_SESSION_W(0, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme write command failed blk_io_handle"
             ": %" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout = p_ctx->keep_alive_timeout * 1000000;  //send us to FW
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_TRACE, "%s keep_alive_timeout %" PRIx64 "\n",
           __FUNCTION__, keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
		                            p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme write keep_alive_timeout command "
             "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    ni_log(NI_LOG_TRACE, "%s(): p_ctx->device_handle=%" PRIx64 " p_ctx->hw_id=%d"
           ", p_ctx->session_id=%d\n", __FUNCTION__,
           (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
  }

  // init for frame pts calculation
  p_ctx->last_pts = NI_LOGAN_NOPTS_VALUE;
  p_ctx->last_dts = NI_LOGAN_NOPTS_VALUE;
  p_ctx->last_dts_interval = 0;
  p_ctx->pts_correction_num_faulty_dts = 0;
  p_ctx->pts_correction_last_dts = 0;
  p_ctx->pts_correction_num_faulty_pts = 0;
  p_ctx->pts_correction_last_pts = 0;

  //p_ctx->p_leftover = malloc(NI_LOGAN_MAX_PACKET_SZ * 2);
  p_ctx->p_leftover = malloc(p_ctx->max_nvme_io_size * 2);
  if (!p_ctx->p_leftover)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s(): Cannot allocate leftover buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    ni_logan_decoder_session_close(p_ctx, 0);
    LRETURN;
  }

  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *) & (p_ctx->pts_table), "dec_pts");
  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *) & (p_ctx->dts_queue), "dec_dts");

  if (p_ctx->p_session_config)
  {
    ni_logan_encoder_params_t* p_param = (ni_logan_encoder_params_t*)p_ctx->p_session_config;
    ni_logan_params_print(p_param, NI_LOGAN_DEVICE_TYPE_DECODER);
  }

  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->active_bit_depth = 0;

  ni_log(NI_LOG_TRACE, "%s(): p_ctx->device_handle=%" PRIx64 " p_ctx->hw_id=%d "
         "p_ctx->session_id=%d\n", __FUNCTION__,
         (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  if (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->nb_save_pkt)
  {
    decoder_dump_dir_open(p_ctx);
  }

  END:

  ni_logan_aligned_free(p_buffer);
  ni_logan_aligned_free(p_signature);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

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
                                        void *p_data)
{
  ni_logan_retcode_t retval;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (NI_LOGAN_INVALID_SESSION_ID == session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Invalid session ID!, return\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (NI_INVALID_DEVICE_HANDLE == device_handle)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): xcoder instance id < 0, return\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ui32LBA = CONFIG_SESSION_KeepAlive_W(session_id);
  if (ni_logan_nvme_send_write_cmd(device_handle, event_handle, p_data, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): device_handle=%" PRIx64 " , session_id=%d\n",
           __FUNCTION__, (int64_t) device_handle, session_id);
    retval = NI_LOGAN_RETCODE_FAILURE;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "SUCCESS %s(): device_handle=%" PRIx64 " , session_id=%d\n",
           __FUNCTION__, (int64_t) device_handle, session_id);
    retval = NI_LOGAN_RETCODE_SUCCESS;
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Flush decoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_flush(ni_logan_session_context_t* p_ctx)
{
  ni_logan_retcode_t retval;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): xcoder instance id < 0, return\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_logan_config_instance_eos(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    p_ctx->ready_to_close = 1;
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): success exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder decoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_close(ni_logan_session_context_t* p_ctx, int eos_recieved)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  int counter = 0;
  int ret = 0;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int i;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR %s(): Cannot allocate leftover buffer.\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_SUCCESS;
    LRETURN;
  }

  ni_log(NI_LOG_ERROR, "Decoder_complete_info: session_id 0x%x, total frames input: %u  "
               "buffered: %u  completed: %u  output: %u  dropped: %u ,  "
               "inst_errors: %u\n", p_ctx->session_id, p_ctx->session_stats.frames_input,
               p_ctx->session_stats.frames_buffered, p_ctx->session_stats.frames_completed,
               p_ctx->session_stats.frames_output, p_ctx->session_stats.frames_dropped,
               p_ctx->session_stats.inst_errors);

  //malloc data buffer
  if(ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: malloc decoder close data buffer failed\n",
           NI_ERRNO);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

  int retry = 0;
  while (retry < NI_LOGAN_MAX_SESSION_CLOSE_RETRIES)  // 10 retries
  {
    ni_log(NI_LOG_TRACE, "%s(): p_ctx->blk_io_handle=%" PRIx64 ", "
           "p_ctx->hw_id=%d, p_ctx->session_id=%d, close_mode=1\n", __FUNCTION__,
           (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): command failed!\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      break;
    }
    else if(((ni_logan_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_LOGAN_RETCODE_SUCCESS;
      p_ctx->session_id = NI_LOGAN_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "%s(): wait for close\n", __FUNCTION__);
      ni_logan_usleep(NI_LOGAN_SESSION_CLOSE_RETRY_INTERVAL_US); // 500000 us
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    }

    retry++;
  }

  END:

  ni_logan_aligned_free(p_buffer);
  ni_logan_aligned_free(p_ctx->p_all_zero_buf);
  ni_logan_aligned_free(p_ctx->p_dec_packet_inf_buf);

  if (NULL != p_ctx->p_leftover)
  {
    free(p_ctx->p_leftover);
    p_ctx->p_leftover = NULL;
  }

  if (p_ctx->pts_table)
  {
    ni_logan_timestamp_table_t* p_pts_table = p_ctx->pts_table;
    ni_logan_queue_free(&p_pts_table->list, p_ctx->buffer_pool);
    free(p_ctx->pts_table);
    p_ctx->pts_table = NULL;
    ni_log(NI_LOG_TRACE, "ni_logan_timestamp_done: success\n");
  }

  if (p_ctx->dts_queue)
  {
    ni_logan_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
    ni_logan_queue_free(&p_dts_queue->list, p_ctx->buffer_pool);
    free(p_ctx->dts_queue);
    p_ctx->dts_queue = NULL;
    ni_log(NI_LOG_TRACE, "ni_logan_timestamp_done: success\n");
  }

  ni_logan_buffer_pool_free(p_ctx->buffer_pool);
  p_ctx->buffer_pool = NULL;

  ni_logan_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
  p_ctx->dec_fme_buf_pool = NULL;

  for (i = 0; i < NI_LOGAN_FIFO_SZ; i++)
  {
    free(p_ctx->pkt_custom_sei[i]);
    p_ctx->pkt_custom_sei[i] = NULL;
  }

  struct timeval tv;
  ni_logan_gettimeofday(&tv, NULL);
  uint64_t codec_end_time = tv.tv_sec*1000000ULL + tv.tv_usec;

  //if close immediately after opened, end time may equals to start time
  if (p_ctx->codec_total_ticks && codec_end_time - p_ctx->codec_start_time)
  {
    uint32_t ni_logan_usage = (uint32_t)((p_ctx->codec_total_ticks / NI_LOGAN_VPU_FREQ) * 100 /
                                   (codec_end_time - p_ctx->codec_start_time));
    ni_log(NI_LOG_INFO, "Decoder HW[%d] INST[%d]-average usage:%d%%\n",
           p_ctx->hw_id, (p_ctx->session_id&0x7F), ni_logan_usage);
  }
  else if (p_ctx->codec_start_time == 0)
  {
    ni_log(NI_LOG_INFO, "Uploader close HW[%d] INST[%d]\n",
           p_ctx->hw_id, (p_ctx->session_id&0x7F));
  }
  else
  {
    ni_log(NI_LOG_INFO, "Warning Decoder HW[%d] INST[%d]-average usage equals to 0\n",
           p_ctx->hw_id, (p_ctx->session_id&0x7F));
  }

  ni_log(NI_LOG_TRACE, "decoder total_pkt:%" PRIu64 ", total_ticks:%" PRIu64 " "
         "total_time:%" PRIu64 " us\n", p_ctx->frame_num,
         p_ctx->codec_total_ticks, codec_end_time - p_ctx->codec_start_time);

  ni_log(NI_LOG_TRACE, "%s():  CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
         __FUNCTION__, (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  // Prevent session open during closing.
  p_ctx->ready_to_close = 0;

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);
  return retval;
}

/*!******************************************************************************
 *  \brief  Send a video p_packet to decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_decoder_session_write(ni_logan_session_context_t* p_ctx, ni_logan_packet_t* p_packet)
{
  uint32_t sent_size = 0;
  uint32_t packet_size = 0;
  uint32_t write_size_bytes = 0;
  uint32_t actual_sent_size = 0;
  uint32_t pkt_chunk_count = 0;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_instance_status_info_t inst_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;
#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_packet)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if ((NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id))
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): xcoder instance id < 0, return\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_LOGAN_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
      abs_time_ns = ni_logan_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_logan_lat_meas_q_add_entry(p_ctx->frame_time_q, abs_time_ns, p_packet->dts);
  }
#endif

  packet_size = p_packet->data_len;
  int current_pkt_size = p_packet->data_len;

  for (; ;)
  {
    query_retry++;
    retval = ni_logan_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval ||
        inst_info.wr_buf_avail_size < packet_size)
    {
      ni_log(NI_LOG_TRACE, "Warning dec write query fail rc %d or available "
             "buf size %u < pkt size %u , retry: %d\n", retval,
             inst_info.wr_buf_avail_size, packet_size, query_retry);
      if (query_retry > NI_LOGAN_MAX_DEC_SESSION_WRITE_QUERY_RETRIES)
      {
        p_ctx->required_buf_size = packet_size;
        p_ctx->status = NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
      ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_100US);  // 100 us
      ni_logan_pthread_mutex_lock(&p_ctx->mutex);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "Info dec write query success, available buf size "
             "%u >= pkt size %u!\n", inst_info.wr_buf_avail_size, packet_size);
      break;
    }
  }

  ui32LBA = WRITE_INSTANCE_W(0, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

  //check for start of stream flag
  if (p_packet->start_of_stream)
  {
    retval = ni_logan_config_instance_sos(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR %s(): Failed to send SOS.\n", __FUNCTION__);
      LRETURN;
    }

    p_packet->start_of_stream = 0;
  }

  if (p_packet->p_data)
  {
    ni_log(NI_LOG_TRACE, "%s() had data to send: packet_size=%u, "
           "p_packet->sent_size=%d, p_packet->data_len=%d, "
           "p_packet->start_of_stream=%d, p_packet->end_of_stream=%d, "
           "p_packet->video_width=%d, p_packet->video_height=%d\n",
           __FUNCTION__, packet_size, p_packet->sent_size, p_packet->data_len,
           p_packet->start_of_stream, p_packet->end_of_stream,
           p_packet->video_width, p_packet->video_height);

    uint32_t send_count = 0;
    uint8_t* p_data = (uint8_t*)p_packet->p_data;
                // Note: session status is NOT reset but tracked between send
                // and recv to catch and recover from a loop condition
    // p_ctx->status = 0;

    ni_logan_instance_dec_packet_info_t *p_dec_packet_info;
    p_dec_packet_info = (ni_logan_instance_dec_packet_info_t *)p_ctx->p_dec_packet_inf_buf;
    p_dec_packet_info->packet_size = packet_size;
    retval = ni_logan_nvme_send_write_cmd(
      p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_dec_packet_inf_buf,
      NI_LOGAN_DATA_BUFFER_LEN, CONFIG_INSTANCE_SetPktSize_W(0, p_ctx->session_id,
                                                       NI_LOGAN_DEVICE_TYPE_DECODER));
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): config pkt size command failed\n",
             __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    if (packet_size % NI_LOGAN_MEM_PAGE_ALIGNMENT) //packet size, already aligned
    {
        packet_size = ( (packet_size / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, packet_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    // reset session status after successful send
    p_ctx->status = 0;
    p_ctx->required_buf_size = 0;

    sent_size = p_packet->data_len;
    p_packet->data_len = 0;

    if (((ni_logan_encoder_params_t*)p_ctx->p_session_config)->nb_save_pkt)
    {
      char dump_file[128] = { 0 };
      long curr_pkt_num = ((long)p_ctx->pkt_num %
      ((ni_logan_encoder_params_t*)p_ctx->p_session_config)->nb_save_pkt) + 1;

      snprintf(dump_file, sizeof(dump_file), "%s/pkt-%04ld.bin",
               p_ctx->stream_dir_name, curr_pkt_num);

      FILE *f = fopen(dump_file, "wb");
      if (f)
      {
        fwrite(p_packet->p_data, sent_size, 1, f);
        fflush(f);
        fclose(f);
      }
    }

    p_ctx->pkt_num++;
  }

  //Handle end of stream flag
  if (p_packet->end_of_stream)
  {
    retval = ni_logan_config_instance_eos(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR %s(): Failed to send EOS.\n", __FUNCTION__);
      LRETURN;
    }

    p_packet->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }
  if (p_ctx->is_dec_pkt_512_aligned)
  {
    // save NI_LOGAN_MAX_DEC_REJECT pts values and their corresponding number of 512 aligned data
    if (p_ctx->is_sequence_change && p_ctx->pkt_index != -1)
    {
      p_ctx->pts_offsets[p_ctx->pkt_index] = p_packet->pts;
      p_ctx->flags_array[p_ctx->pkt_index] = p_packet->flags;
      p_ctx->pkt_offsets_index[p_ctx->pkt_index] = current_pkt_size/512; // assuming packet_size is 512 aligned
      p_ctx->pkt_index++;
      if (p_ctx->pkt_index >= NI_LOGAN_MAX_DEC_REJECT)
      {
        ni_log(NI_LOG_DEBUG, "%s(): more than NI_LOGAN_MAX_DEC_REJECT frames are "
               "rejected by the decoder. Increase NI_LOGAN_MAX_DEC_REJECT is required "
               "or default gen pts values will be used !\n", __FUNCTION__);
        p_ctx->pkt_index = -1; // signaling default pts gen
      }
    }
  }
  else
  {
    p_ctx->pts_offsets[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_packet->pts;
    p_ctx->flags_array[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_packet->flags;
    if (p_ctx->pkt_index == 0)
    {
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = 0;
      /* minus 1 here. ffmpeg parses the msb 0 of long start code as the last packet's payload for hevc bitstream (hevc_parse).
       * move 1 byte forward on all the pkt_offset so that frame_offset coming from fw can fall into the correct range. */
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = current_pkt_size - 1;
    }
    else
    {
      // cumulate sizes to correspond to FW offsets
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_LOGAN_FIFO_SZ];
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_LOGAN_FIFO_SZ] + current_pkt_size;

      //Wrapping 32 bits since FW send u32 wrapped values
      if (p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] > 0xFFFFFFFF)
      {
        p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] - (0x100000000);
        p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] + current_pkt_size;
      }
    }

    /* if this wrap-around pkt_offset_index spot is about to be overwritten, free the previous one. */
    free(p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ]);

    if (p_packet->p_all_custom_sei)
    {
      p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = malloc(sizeof(ni_logan_all_custom_sei_t));
      if (p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ])
      {
        memcpy(p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ], p_packet->p_all_custom_sei,
               sizeof(ni_logan_all_custom_sei_t));
      }
      else
      {
        /* warn and lose the sei data. */
        ni_log(NI_LOG_ERROR, "%s: failed to allocate custom SEI buffer for pkt error=%d\n",
               __FUNCTION__, NI_ERRNO);
      }
    }
    else
    {
      p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_LOGAN_FIFO_SZ] = NULL;
    }

    p_ctx->pkt_index++;
  }

  retval = ni_logan_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_packet->dts, 0);
  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ERROR %s(): ni_logan_timestamp_register() for dts returned "
           "%d\n", __FUNCTION__, retval);
  }

  END:

  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    ni_log(NI_LOG_TRACE, "%s(): exit: packets: %" PRIu64 " offset %" PRIu64 " "
           "sent_size = %u, available_space = %u, status=%d\n", __FUNCTION__,
           p_ctx->pkt_num, (uint64_t)p_packet->pos, sent_size,
           inst_info.wr_buf_avail_size, p_ctx->status);
    return sent_size;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ERROR %s(): exit: returnErr: %d, p_ctx->status: %d\n",
           __FUNCTION__, retval, p_ctx->status);
    return retval;
  }
}

static int64_t guess_correct_pts(ni_logan_session_context_t* p_ctx, int64_t reordered_pts, int64_t dts, int64_t last_pts)
{
  int64_t pts = NI_LOGAN_NOPTS_VALUE;
  if (dts != NI_LOGAN_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_dts += dts <= p_ctx->pts_correction_last_dts;
    p_ctx->pts_correction_last_dts = dts;
  }
  else if (reordered_pts != NI_LOGAN_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_dts = reordered_pts;
  }
  if (reordered_pts != NI_LOGAN_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_pts += reordered_pts <= p_ctx->pts_correction_last_pts;
    p_ctx->pts_correction_last_pts = reordered_pts;
  }
  else if (dts != NI_LOGAN_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_pts = dts;
  }
  if ((p_ctx->pts_correction_num_faulty_pts<=p_ctx->pts_correction_num_faulty_dts || dts == NI_LOGAN_NOPTS_VALUE)
     && reordered_pts != NI_LOGAN_NOPTS_VALUE)
  {
    pts = reordered_pts;
  }
  else
  {
    if (NI_LOGAN_NOPTS_VALUE == last_pts)
    {
      pts = dts;
    }
    else if (NI_LOGAN_NOPTS_VALUE != last_pts && dts >= last_pts)
    {
      pts = dts;
    }
    else
    {
      pts = reordered_pts;
    }
  }
  //printf("here pts = %d\n", pts);
  return pts;
}
/*!******************************************************************************
 *  \brief  Retrieve a YUV p_frame from decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_decoder_session_read(ni_logan_session_context_t* p_ctx, ni_logan_frame_t* p_frame)
{
  ni_logan_instance_mgr_stream_info_t data = { 0 };
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t* p_data_buffer = (uint8_t*) p_frame->p_buffer;
  uint32_t data_buffer_size = p_frame->buffer_size;
  int i = 0;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  int metadata_hdr_size = NI_LOGAN_FW_META_DATA_SZ - sizeof(ni_logan_hwframe_surface_t);
  int sei_size = 0;
  int frame_cycle = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  int keep_processing = 1;
  ni_logan_instance_status_info_t inst_info = { 0 };
  int query_retry = 0;
  int max_query_retries = (p_ctx->decoder_low_delay? (p_ctx->decoder_low_delay * 1000 / NI_LOGAN_RETRY_INTERVAL_200US + 1) : \
                           NI_LOGAN_MAX_DEC_SESSION_READ_QUERY_RETRIES);
  uint32_t ui32LBA = 0;
  unsigned int bytes_read_so_far = 0;

#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR %s(): xcoder instance id < 0, return\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.

  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
  p_frame->data_len[2] + metadata_hdr_size;
  ni_log(NI_LOG_TRACE, "Total bytes to read %d \n",total_bytes_to_read);
  for (; ;)
  {
    query_retry++;
    retval = ni_logan_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_TRACE, "Info query inst_info.rd_buf_avail_size = %u\n",
           inst_info.rd_buf_avail_size);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "Warning dec read query fail rc %d retry %d\n",
             retval, query_retry);

      if (query_retry >= 1000)
      {
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
      ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_100US);  // 100 us
      ni_logan_pthread_mutex_lock(&p_ctx->mutex);
    }
    else if (inst_info.rd_buf_avail_size == metadata_hdr_size)
    {
      ni_log(NI_LOG_TRACE, "Info only metadata hdr is available, seq change?\n");
      total_bytes_to_read = metadata_hdr_size;
      break;
    }
    else if (0 == inst_info.rd_buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        ni_log(NI_LOG_TRACE, "Info dec query, ready_to_close %u, ctx status %d,"
               " try %d\n", p_ctx->ready_to_close, p_ctx->status, query_retry);
        retval = ni_logan_query_stream_info(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER, &data, 0);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (data.is_flushed ||
            query_retry >= NI_LOGAN_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)  // 15000 retries
        {
          ni_log(NI_LOG_DEBUG, "Info eos reached: is_flushed %u try %d.\n",
                 data.is_flushed, query_retry);
          if (query_retry >= NI_LOGAN_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   //15000 retries
          {
            ni_log(NI_LOG_INFO, "Info eos reached exceeding max retries: is_flushed %u try %d.\n",
                   data.is_flushed, query_retry);
          }
          p_frame->end_of_stream = 1;
          retval = NI_LOGAN_RETCODE_SUCCESS;
          LRETURN;
        }
        else
        {
          ni_log(NI_LOG_TRACE, "Dec read available buf size == 0, query try %d,"
                 " retrying ..\n", query_retry);
          ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
          ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_200US);  // 200 us
          ni_logan_pthread_mutex_lock(&p_ctx->mutex);
          continue;
        }
      }

      if (p_ctx->decoder_low_delay && inst_info.frames_dropped && p_ctx->is_sequence_change)
      {
        ni_log(NI_LOG_TRACE, "Info inst_info.frames_dropped = %u\n",
               inst_info.frames_dropped);
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }

      ni_log(NI_LOG_TRACE, "Warning dec read available buf size == 0, eos %u "
             "nb try %d\n", p_frame->end_of_stream, query_retry);

      if (((NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) ||
           (p_ctx->frame_num < p_ctx->pkt_num && p_ctx->decoder_low_delay)) &&
          (query_retry < max_query_retries))
      {
        if ((NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) &&
            (inst_info.wr_buf_avail_size > p_ctx->required_buf_size))
        {
          ni_log(NI_LOG_TRACE, "Info dec write buffer is enough, available buf "
                 "size %u >= required size %u !\n",
                 inst_info.wr_buf_avail_size, p_ctx->required_buf_size);
          p_ctx->status = 0;
          p_ctx->required_buf_size = 0;
        }
        else
        {
          ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
          ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_200US);  // 200 us
          ni_logan_pthread_mutex_lock(&p_ctx->mutex);
          continue;
        }
      }
      else if (p_ctx->frame_num < p_ctx->pkt_num && p_ctx->decoder_low_delay)
      {
        if((p_ctx->pkt_num - p_ctx->frame_num) != p_ctx->decoder_drop_frame_num)
        {
            p_ctx->decoder_drop_frame_num = p_ctx->pkt_num - p_ctx->frame_num;
            ni_log(NI_LOG_INFO, "Warning: time out on receiving a decoded frame"
                   "from the decoder, assume dropped, received frame_num: "
                   "%" PRIu64 ", sent pkt_num: %" PRIu64 ", pkt_num-frame_num: "
                   "%d, sending another packet.\n", p_ctx->frame_num,
                   p_ctx->pkt_num, p_ctx->decoder_drop_frame_num);
        }
      }
      retval = NI_LOGAN_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
      // We have to ensure there are adequate number of DTS for picture
      // reorder delay otherwise wait for more packets to be sent to decoder.
      ni_logan_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
      if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
          !p_ctx->ready_to_close)
      {
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }

      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_logan_query_stream_info(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER, &data, 0);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_TRACE, "Info dec YUV query, pic size %ux%u xfer frame size "
               "%ux%u frame-rate %u is_flushed %u\n", data.picture_width,
               data.picture_height, data.transfer_frame_stride,
               data.transfer_frame_height, data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->active_bit_depth = (p_ctx->bit_depth_factor==2) ? 10 : 8;
        p_ctx->is_sequence_change = 1;

        ni_log(NI_LOG_TRACE, "Info dec YUV, adjust frame size from %ux%u %dbits to %ux%u\n",
               p_frame->video_width, p_frame->video_height,p_ctx->active_bit_depth,
               p_ctx->active_video_width, p_ctx->active_video_height);

        ni_logan_decoder_frame_buffer_free(p_frame);

        // set up decoder YUV frame buffer pool
        if (ni_logan_dec_fme_buffer_pool_initialize(
              p_ctx, NI_LOGAN_DEC_FRAME_BUF_POOL_SIZE_INIT, p_ctx->active_video_width,
              p_ctx->active_video_height,
              p_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
              p_ctx->bit_depth_factor))
        {
          ni_log(NI_LOG_ERROR, "ERROR %s(): Cannot allocate fme buf pool.\n",
                 __FUNCTION__);
          retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
          ni_logan_decoder_session_close(p_ctx, 0);
#ifdef XCODER_SELF_KILL_ERR
          // if need to terminate at such occasion when continuing is not
          // possible, trigger a codec closure
          ni_log(NI_LOG_ERROR, "Terminating due to persistent failures\n");
          kill(getpid(), SIGTERM);
#endif
          LRETURN;
        }

        retval = ni_logan_decoder_frame_buffer_alloc(
          p_ctx->dec_fme_buf_pool, p_frame, 1, // get mem buffer
          p_ctx->active_video_width, p_ctx->active_video_height,
          p_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
          p_ctx->bit_depth_factor);

        if (NI_LOGAN_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
        p_frame->data_len[2] + metadata_hdr_size;
        p_data_buffer = (uint8_t*) p_frame->p_buffer;

        // make sure we don't read more than available
        ni_log(NI_LOG_TRACE, "Info dec buf size: %u YUV frame + meta-hdr size: %u "
               "available: %u\n", p_frame->buffer_size, total_bytes_to_read,
               inst_info.rd_buf_avail_size);
      }
      break;
    }
  }

    ni_log(NI_LOG_TRACE, "total_bytes_to_read %d max_nvme_io_size %d ylen %d "
           "cr len %d cb len %d hdr %d\n", total_bytes_to_read,
           p_ctx->max_nvme_io_size, p_frame->data_len[0], p_frame->data_len[1],
           p_frame->data_len[2], metadata_hdr_size);

    if (inst_info.rd_buf_avail_size < total_bytes_to_read ||
        inst_info.rd_buf_avail_size > p_frame->buffer_size)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s() avaliable size(%u) less than needed (%u), "
             "or more than buffer allocated (%u)\n", __FUNCTION__,
             inst_info.rd_buf_avail_size, total_bytes_to_read, p_frame->buffer_size);
      ni_logan_assert(0);
    }

    read_size_bytes = inst_info.rd_buf_avail_size;
    ui32LBA = READ_INSTANCE_R(0, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
    if (read_size_bytes % NI_LOGAN_MEM_PAGE_ALIGNMENT)
    {
      read_size_bytes = ( (read_size_bytes / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data_buffer, read_size_bytes, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    else
    {
      // command issued successfully, now exit

      ni_logan_metadata_dec_frame_t* p_meta =
      (ni_logan_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer
      + p_frame->data_len[0] + p_frame->data_len[1]
      + p_frame->data_len[2]);

      if (inst_info.rd_buf_avail_size != metadata_hdr_size)
      {
        sei_size = p_meta->sei_size;
        frame_cycle = p_meta->frame_cycle;
      }
      p_ctx->codec_total_ticks += frame_cycle;
      total_bytes_to_read = total_bytes_to_read + sei_size;

      ni_log(NI_LOG_TRACE, "%s success, size %d total_bytes_to_read include "
             "sei %d sei_size %d frame_cycle %d\n", __FUNCTION__, retval,
             total_bytes_to_read, sei_size, frame_cycle);
    }

  bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  p_frame->src_codec = p_ctx->codec_format;
  rx_size = ni_logan_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
  p_ctx->frame_pkt_offset = frame_offset;

  // if using old firmware, bit_depth=0 so use bit_depth_factor
  if (!p_frame->bit_depth)
    p_frame->bit_depth = (p_ctx->bit_depth_factor==2)?10:8;

  // if sequence change, update bit depth factor
  if  ((rx_size == 0))
    p_ctx->bit_depth_factor = (p_frame->bit_depth==10) ? 2: 1;

  if (rx_size > 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): s-state %d seq change %d\n", __FUNCTION__,
           p_ctx->session_run_state, p_ctx->is_sequence_change);
    if (ni_logan_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)&p_frame->dts,
                                        LOGAN_XCODER_FRAME_OFFSET_DIFF_THRES, (p_ctx->frame_num % 500 == 0),
                                        p_ctx->buffer_pool) != NI_LOGAN_RETCODE_SUCCESS)
    {
      if (p_ctx->last_dts != NI_LOGAN_NOPTS_VALUE && !p_ctx->ready_to_close)
      {
        // Mark as DTS padding for offset compensation
        p_ctx->pic_reorder_delay++;
        p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
        ni_log(NI_LOG_ERROR, "Padding DTS:%ld.\n", p_frame->dts);
      }
      else
      {
        p_frame->dts = NI_LOGAN_NOPTS_VALUE;
      }
    }

    // Read the following DTS for picture reorder delay
    if (p_ctx->is_sequence_change)
    {
      for (i = 0; i < p_ctx->pic_reorder_delay; i++)
      {
        if (p_ctx->last_pts == NI_LOGAN_NOPTS_VALUE && p_ctx->last_dts == NI_LOGAN_NOPTS_VALUE)
        {
          // If the p_frame->pts is unknown in the very beginning of the stream
          // (video stream only) we assume p_frame->pts == 0 as well as DTS less
          // than PTS by 1000 * 1/timebase
          if (p_frame->pts >= p_frame->dts && p_frame->pts - p_frame->dts < 1000)
          {
            break;
          }
        }

        if (ni_logan_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)&p_frame->dts,
                                            LOGAN_XCODER_FRAME_OFFSET_DIFF_THRES, (p_ctx->frame_num % 500 == 0),
                                            p_ctx->buffer_pool) != NI_LOGAN_RETCODE_SUCCESS)
        {
          p_frame->dts = NI_LOGAN_NOPTS_VALUE;
        }
      }
      // Reset for DTS padding counting
      p_ctx->pic_reorder_delay = 0;
    }

    if (p_ctx->is_dec_pkt_512_aligned)
    {
      if (p_ctx->is_sequence_change)
      {
        if (p_ctx->pts_offsets[0] != NI_LOGAN_NOPTS_VALUE && p_ctx->pkt_index != -1)
        {
          uint32_t idx = 0;
          uint64_t cumul = p_ctx->pkt_offsets_index[0];
          while (cumul < frame_offset) // look for pts index
          {
            if (idx == NI_LOGAN_MAX_DEC_REJECT)
            {
              ni_log(NI_LOG_INFO, "Invalid index computation oversizing NI_LOGAN_MAX_DEC_REJECT! \n");
              break;
            }
            else
            {
              cumul += p_ctx->pkt_offsets_index[idx];
              idx++;
            }
          }

          if (idx != NI_LOGAN_MAX_DEC_REJECT && idx > 0)
          {
            p_frame->pts = p_ctx->pts_offsets[idx];
          }
          else if (p_ctx->session_run_state == LOGAN_SESSION_RUN_STATE_RESETTING)
          {
            ni_log(NI_LOG_TRACE, "%s(): session %d recovering and adjusting ts\n",
                   __FUNCTION__, p_ctx->session_id);
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_NORMAL;
          }
          else // use pts = 0 as offset
          {
            p_frame->pts = 0;
          }
        }
        else
        {
          p_frame->pts = 0;
        }
      }
      else
      {
        p_frame->pts = p_ctx->last_pts + p_ctx->last_dts_interval;
      }
    }
    else
    {
      i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                      p_ctx->pkt_offsets_index,
                                      NI_LOGAN_FIFO_SZ, frame_offset);
      if (i >= 0)
      {
        // According to LGXCOD-3099 the frame_offset would be less than
        // expected by the size of SEI unit when there is malformed SEI
        // being sent into decoder. That will lead to mismatch of the
        // true PTS offset with the decoded frame. So we need to correct
        // the offset range when there is suspicious frame_offset for
        // decoded frames.
        uint64_t d1 = frame_offset - p_ctx->pkt_offsets_index_min[i];
        uint64_t d2 = p_ctx->pkt_offsets_index[i] - frame_offset;
        if (d1 > d2)
        {
          // When the frame_offset is closer to the right boundary, the
          // right margin is caused by the missing SEI size.
          i++;
        }

        p_frame->pts = p_ctx->pts_offsets[i];
        p_frame->flags = p_ctx->flags_array[i];
        p_frame->p_custom_sei = (uint8_t *)p_ctx->pkt_custom_sei[i % NI_LOGAN_FIFO_SZ];
        p_ctx->pkt_custom_sei[i % NI_LOGAN_FIFO_SZ] = NULL;
      }
      else
      {
        //backup solution pts
        p_frame->pts = p_ctx->last_pts + p_ctx->last_dts_interval;
        ni_log(NI_LOG_ERROR, "ERROR: NO pts found consider increasing NI_LOGAN_FIFO_SZ!\n");
      }
    }

    p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts, p_ctx->last_pts);
    p_ctx->last_pts = p_frame->pts;
    if ((0 == p_ctx->is_sequence_change) && (NI_LOGAN_NOPTS_VALUE != p_frame->dts) && (NI_LOGAN_NOPTS_VALUE != p_ctx->last_dts))
    {
      p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    }
    p_ctx->last_dts = p_frame->dts;
    p_ctx->is_sequence_change = 0;
    p_ctx->frame_num++;
  }

  ni_log(NI_LOG_TRACE, "%s(): received data [0x%08x]\n", __FUNCTION__, rx_size);
  ni_log(NI_LOG_TRACE, "%s(): p_frame->start_of_stream=%d "
         "p_frame->end_of_stream=%d p_frame->video_width=%d "
         "p_frame->video_height=%d\n", __FUNCTION__, p_frame->start_of_stream,
         p_frame->end_of_stream, p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_TRACE, "%s(): p_frame->data_len[0/1/2]=%d/%d/%d\n", __FUNCTION__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
    ni_log(NI_LOG_TRACE, "Decoder pts queue size = %d dts queue size = %d\n\n",
      ((ni_logan_timestamp_table_t*)p_ctx->pts_table)->list.count,
      ((ni_logan_timestamp_table_t*)p_ctx->dts_queue)->list.count);
    // scan and clean up
    ni_logan_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue, p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if (p_frame->dts != NI_LOGAN_NOPTS_VALUE && p_ctx->frame_time_q != NULL)
  {
#ifdef _WIN32
      abs_time_ns = ni_logan_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,dLAT:%lu;\n",
             p_frame->dts,
             abs_time_ns - p_ctx->prev_read_frame_time,
             ni_logan_lat_meas_q_check_latency(p_ctx->frame_time_q, abs_time_ns, p_frame->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

  END:

  if(retval == NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY)
  {
    ni_log(NI_LOG_ERROR, "ni_logan_decoder_session_read(): bad exit, retval = %d\n",retval);
    void* p_buffer = NULL;
    void* p_debug_data_buffer = NULL;
    uint32_t dataLen = ((sizeof(ni_logan_instance_debugInfo_t) + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;
    ni_logan_instance_debugInfo_t ni_logan_instance_debugInfo;
    uint32_t debug_dataLen;
    if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: ni_logan_query_instance_buf_info() Cannot allocate buffer.\n", NI_ERRNO);
        retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
        return retval;
    }

    memset(p_buffer, 0, dataLen);

    ui32LBA = QUERY_INSTANCE_DEBUG_INFO_R(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

    if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR, " QUERY_INSTANCE_DEBUG_INFO_R(): NVME command Failed\n");
        retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
        ni_logan_aligned_free(p_buffer);
        LRETURN;
    }

    //No need to flip the bytes since the datastruct has only uint8_t datatypes
    memcpy((void*)&ni_logan_instance_debugInfo, p_buffer, sizeof(ni_logan_instance_debugInfo_t));

    ni_log(NI_LOG_ERROR, "DEBUG INFO(): Core[%d][%d] ErrInst 0x%x, PktTotal %d, PktSize 0x%x, BufAddr 0x%x, WrPtr 0x%x, RdPtr 0x%x, Size %d\n",
                    ni_logan_instance_debugInfo.ui8VpuCoreError,
                    ni_logan_instance_debugInfo.ui8VpuInstId,
                    ni_logan_instance_debugInfo.ui32VpuInstError,
                    ni_logan_instance_debugInfo.ui8DataPktSeqTotal,
                    ni_logan_instance_debugInfo.ui32DataPktSize,
                    ni_logan_instance_debugInfo.ui32BufferAddr,
                    ni_logan_instance_debugInfo.ui32BufferWrPt,
                    ni_logan_instance_debugInfo.ui32BufferRdPt,
                    ni_logan_instance_debugInfo.ui32BufferSize);

    if(ni_logan_instance_debugInfo.ui32VpuInstError & (1 << ni_logan_instance_debugInfo.ui8VpuInstId))
    {
        //Debug data buffer
        debug_dataLen = ((ni_logan_instance_debugInfo.ui32DataPktSize + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;
        if (ni_logan_posix_memalign(&p_debug_data_buffer, sysconf(_SC_PAGESIZE), debug_dataLen))
        {
            ni_log(NI_LOG_ERROR, "ERROR %d: ni_logan_query_instance_buf_info() Cannot allocate buffer.\n", NI_ERRNO);
            retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
            ni_logan_aligned_free(p_buffer);
            return retval;
        }
#define DUMP
#ifdef DUMP
        char dump_info_file[128] = { 0 };
        char dump_data_file[128] = { 0 };
        char dump_info[256] = { 0 };
        struct timeval tv;
        struct tm cur_tm;
        char cur_date[20];
        char cur_time[20];

#ifdef _WIN32
        localtime_s(&cur_tm, (time_t*)&tv.tv_sec);
#else
        localtime_r((time_t*)&tv.tv_sec, &cur_tm);
#endif
        (void) ni_logan_gettimeofday(&tv, NULL);
        decoder_dump_dir_create(p_ctx);

        snprintf(cur_date,20,"%d-%02d-%02d", cur_tm.tm_year+1900,cur_tm.tm_mon+1,cur_tm.tm_mday);
        snprintf(cur_time,20,"%02d_%02d_%02d", cur_tm.tm_hour,cur_tm.tm_min,cur_tm.tm_sec);

        ni_log(NI_LOG_ERROR, "current data %s, time is %s\n", cur_date, cur_time);

        snprintf(dump_info_file, sizeof(dump_info_file), "%s/%s_%s_%u_%u_%u_DEBUG_INFO.txt",
                  p_ctx->stream_dir_name,
                  cur_date, cur_time,
                  p_ctx->session_id,
                  ni_logan_instance_debugInfo.ui8VpuCoreError,
                  ni_logan_instance_debugInfo.ui8VpuInstId);

        FILE *fp_info = fopen(dump_info_file, "wb");
        if (!fp_info)
        {
            ni_log(NI_LOG_ERROR, "Error create decoder pkt dump log: %s\n", dump_info_file);
        }

        //Dump info
        snprintf(dump_info, sizeof(dump_info), "DEBUG INFO(): Core[%d][%d] ErrInst 0x%x, PktTotal %d, PktSize 0x%x, BufAddr 0x%x, WrPtr 0x%x, RdPtr 0x%x, Size %d\n",
                    ni_logan_instance_debugInfo.ui8VpuCoreError,
                    ni_logan_instance_debugInfo.ui8VpuInstId,
                    ni_logan_instance_debugInfo.ui32VpuInstError,
                    ni_logan_instance_debugInfo.ui8DataPktSeqTotal,
                    ni_logan_instance_debugInfo.ui32DataPktSize,
                    ni_logan_instance_debugInfo.ui32BufferAddr,
                    ni_logan_instance_debugInfo.ui32BufferWrPt,
                    ni_logan_instance_debugInfo.ui32BufferRdPt,
                    ni_logan_instance_debugInfo.ui32BufferSize);

        if (fp_info)
        {
            fwrite(dump_info, strlen(dump_info), 1, fp_info);
            fflush(fp_info);
            fclose(fp_info);
        }

        //Dump data
        snprintf(dump_data_file, sizeof(dump_data_file), "%s/%s_%s_%u_%u_%u_DEBUG_DATA.bin",
                p_ctx->stream_dir_name,
                cur_date, cur_time,
                p_ctx->session_id,
                ni_logan_instance_debugInfo.ui8VpuCoreError,
                ni_logan_instance_debugInfo.ui8VpuInstId);

        FILE *fp_data = fopen(dump_data_file, "wb");
        if (!fp_data)
        {
            ni_log(NI_LOG_ERROR, "Error create decoder pkt dump log: %s\n", dump_data_file);
        }
#endif

        while(ni_logan_instance_debugInfo.ui8DataPktSeqNum < (ni_logan_instance_debugInfo.ui8DataPktSeqTotal - 1))
        {
            memset(p_buffer, 0, dataLen);

            ui32LBA = QUERY_INSTANCE_DEBUG_INFO_R(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

            if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
            {
                ni_log(NI_LOG_ERROR, " QUERY_INSTANCE_DEBUG_INFO_R(): NVME command Failed\n");
                retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
                ni_logan_aligned_free(p_debug_data_buffer);
                ni_logan_aligned_free(p_buffer);
                LRETURN;
            }

            memcpy((void*)&ni_logan_instance_debugInfo, p_buffer, sizeof(ni_logan_instance_debugInfo_t));

            ni_log(NI_LOG_ERROR, "DEBUG INFO(): Dumping data packet - %d/%d\n",
                        ni_logan_instance_debugInfo.ui8DataPktSeqNum,
                        ni_logan_instance_debugInfo.ui8DataPktSeqTotal - 1);

            ui32LBA = QUERY_INSTANCE_DEBUG_DATA_R(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

            memset(p_debug_data_buffer, 0, debug_dataLen);

            if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_debug_data_buffer, debug_dataLen, ui32LBA) < 0)
            {
                ni_log(NI_LOG_ERROR, " QUERY_INSTANCE_DEBUG_DATA_R(): NVME command Failed\n");
                retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
                ni_logan_aligned_free(p_debug_data_buffer);
                ni_logan_aligned_free(p_buffer);
                LRETURN;
            }

#ifdef DUMP
            //TODO: Write out p_debug_data_buffer to a file and concat them at the end
            if (fp_data)
            {
                fwrite(p_debug_data_buffer, debug_dataLen, 1, fp_data);
                fflush(fp_data);
            }
#endif
        }
        ni_logan_aligned_free(p_debug_data_buffer);

#ifdef DUMP
        if (fp_data)
        {
            fclose(fp_data);
        }
#endif

    }
    ni_logan_aligned_free(p_buffer);
  }

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "%s(): bad exit, retval = %d\n", __FUNCTION__, retval);
    return retval;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __FUNCTION__, rx_size);
    return rx_size;
  }
}

/*!******************************************************************************
 *  \brief  Query current decoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_query(ni_logan_session_context_t* p_ctx)
{
  ni_logan_instance_mgr_general_status_t data;
  int retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  retval = ni_logan_query_general_status(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER, &data);

  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    p_ctx->load_query.current_load = (uint32_t)data.process_load_percent;
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.total_contexts = (uint32_t)data.active_sub_instances_cnt;
    ni_log(NI_LOG_TRACE, "%s current_load:%d fw_model_load:%d fw_video_mem_usage:%d "
           "active_contexts %d\n", __FUNCTION__, p_ctx->load_query.current_load,
           p_ctx->load_query.fw_model_load, p_ctx->load_query.fw_video_mem_usage,
           p_ctx->load_query.total_contexts);
  }

  return retval;
}

/*!******************************************************************************
 *  \brief  Open a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_open(ni_logan_session_context_t* p_ctx)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t buffer_size = 0;
  void *p_signature = NULL;
  ni_logan_encoder_params_t* p_cfg = (ni_logan_encoder_params_t*)p_ctx->p_session_config;
  uint32_t model_load = 0;
  ni_logan_instance_status_info_t inst_info = { 0 };
  void* p_buffer = NULL;
  ni_logan_encoder_session_open_info_t session_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_cfg)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): NULL pointer p_config passed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  p_ctx->auto_dl_handle = NI_INVALID_DEVICE_HANDLE;

  // In yuvbypass mode, encoder device must keep consistent with uploader or decoder device
  // check the serial number here to confirm this.
  if (p_ctx->hw_action == NI_LOGAN_CODEC_HW_ENABLE)
  {
    int c;
    ni_logan_device_handle_map_SN(p_ctx->sender_handle, &p_ctx->d_serial_number);
    ni_logan_device_handle_map_SN(p_ctx->blk_io_handle, &p_ctx->e_serial_number);

    for (c = 0; c < 20; c++)
    {
      if (p_ctx->e_serial_number.ai8Sn[c] != p_ctx->d_serial_number.ai8Sn[c])
      {
        //QDFW-315 Autodownload
        p_ctx->auto_dl_handle = p_ctx->sender_handle;
        ni_log(NI_LOG_INFO, "Autodownload device handle set %d!\n", p_ctx->auto_dl_handle);
        p_ctx->hw_action = NI_LOGAN_CODEC_HW_NONE;
        break;
      }
    }
  }

  //Check if there is an instance or we need a new one
  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    int i;
    p_ctx->device_type = NI_LOGAN_DEVICE_TYPE_ENCODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->keyframe_factor = 1;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->force_frame_type = 0;
    p_ctx->required_buf_size = 0;
    p_ctx->ready_to_close = 0;
    // Sequence change tracking related stuff
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->active_bit_depth = 0;
    p_ctx->enc_pts_w_idx = 0;
    p_ctx->enc_pts_r_idx = 0;
    p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_NORMAL;
    p_ctx->codec_total_ticks = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->p_dec_packet_inf_buf = NULL;
    p_ctx->session_timestamp = 0;

    for (i = 0; i < NI_LOGAN_FIFO_SZ; i++)
    {
      p_ctx->pkt_custom_sei[i] = NULL;
    }

    struct timeval tv;
    ni_logan_gettimeofday(&tv, NULL);
    p_ctx->codec_start_time = tv.tv_sec*1000000ULL + tv.tv_usec;

#ifdef _WIN32
    p_ctx->event_handle = ni_logan_create_event();
    if (p_ctx->event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }

    p_ctx->thread_event_handle = ni_logan_create_event();
    if (p_ctx->thread_event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }
#endif

    memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));
    model_load = (uint32_t)p_cfg->source_width * (uint32_t)p_cfg->source_height * (uint32_t)p_cfg->hevc_enc_params.frame_rate;

    ni_log(NI_LOG_TRACE, "Model load info: Width:%d Height:%d FPS:%d Load %ld\n",
           p_cfg->source_width, p_cfg->source_height, p_cfg->hevc_enc_params.frame_rate, model_load);

    //malloc zero data buffer
    if(ni_logan_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
             NI_ERRNO, __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //malloc data buffer
    if(ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
             NI_ERRNO, __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

    // Get session ID
    ((ni_logan_get_session_id_t *)p_buffer)->session_id = NI_LOGAN_INVALID_SESSION_ID;
    ui32LBA = OPEN_GET_SID_R((p_ctx->hw_action == NI_LOGAN_CODEC_HW_ENABLE), NI_LOGAN_DEVICE_TYPE_ENCODER);
    retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme read command failed blk_io_handle "
             "%" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    p_ctx->session_id = ni_logan_ntohl(((ni_logan_get_session_id_t *)p_buffer)->session_id);
    ni_log(NI_LOG_TRACE, "%s ID:0x%x\n", __FUNCTION__, p_ctx->session_id);
    if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): query session ID failed, p_ctx->"
             "blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
             __FUNCTION__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);
      ni_logan_encoder_session_close(p_ctx, 0);
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }

    //Send session Info
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);
    session_info.codec_format = ni_logan_htonl(p_ctx->codec_format);
    session_info.i32picWidth = ni_logan_htonl(p_cfg->source_width);
    session_info.i32picHeight = ni_logan_htonl(p_cfg->source_height);
    session_info.model_load = ni_logan_htonl(model_load);
#ifdef ENCODER_SYNC_QUERY //enable with parameter "-q"
    if (((ni_logan_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode)
    {
      //In low latency mode, encoder read packet will just send query command one time. Set 1 to notify the FW.
      ni_log(NI_LOG_TRACE, "Low latency mode support encoder read sync query\n");
      session_info.EncoderReadSyncQuery = ni_logan_htonl(0x01);
    }
    else
    {
      session_info.EncoderReadSyncQuery = ni_logan_htonl(0x00);
    }
#else
    session_info.EncoderReadSyncQuery = ni_logan_htonl(0x00);
#endif
    session_info.hw_desc_mode = ni_logan_htonl(p_ctx->hw_action);
    session_info.set_high_priority = ni_logan_htonl(p_ctx->set_high_priority);
    memcpy(p_buffer, &session_info, sizeof(ni_logan_encoder_session_open_info_t));

    ui32LBA = OPEN_SESSION_W((p_ctx->hw_action == NI_LOGAN_CODEC_HW_ENABLE), p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_ENCODER);
    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme write command failed blk_io_handle: "
             "%" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    ni_log(NI_LOG_TRACE, "%s completed\n", __FUNCTION__);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout = p_ctx->keep_alive_timeout * 1000000;  //send us to FW
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_TRACE, "%s keep_alive_timeout %" PRIx64 "\n",
           __FUNCTION__, keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme write keep_alive_timeout command "
             "failed blk_io_handle: %" PRIx64 ", hw_id, %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
  }

  retval = ni_logan_config_instance_set_encoder_params(p_ctx);

  if (NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY == retval)
  {
    ni_log(NI_LOG_DEBUG, "Warning: ni_logan_config_instance_set_encoder_params() vpu recovery\n");
    LRETURN;
  }
  else if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: calling ni_logan_config_instance_set_encoder_params(): p_ctx->device_handle="
           "%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
           (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    ni_logan_encoder_session_close(p_ctx, 0);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "Encoder params sent\n");

  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *)& p_ctx->pts_table, "enc_pts");
  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *)& p_ctx->dts_queue, "enc_dts");

  // init close caption SEI header and trailer
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_hevc, g_itu_t_t35_cc_sei_hdr_hevc,
         NI_CC_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_h264, g_itu_t_t35_cc_sei_hdr_h264,
         NI_CC_SEI_HDR_H264_LEN);
  memcpy(p_ctx->sei_trailer, g_sei_trailer, NI_CC_SEI_TRAILER_LEN);
  // init hdr10+ SEI header
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_hevc, g_itu_t_t35_hdr10p_sei_hdr_hevc,
         NI_HDR10P_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_h264, g_itu_t_t35_hdr10p_sei_hdr_h264,
         NI_HDR10P_SEI_HDR_H264_LEN);

  // query to check the final encoder config status
  for (; ;)
  {
    query_retry++;
    retval = ni_logan_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);

    if (inst_info.sess_err_no ||
        NI_LOGAN_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT == inst_info.inst_err_no)
    {
      ni_log(NI_LOG_ERROR, "ERROR: session error %u or VPU_RSRC_INSUFFICIENT\n",
             inst_info.sess_err_no);
      retval = NI_LOGAN_RETCODE_FAILURE;
      LRETURN;
    }
    else if (inst_info.wr_buf_avail_size > 0)
    {
      ni_log(NI_LOG_TRACE, "%s(): wr_buf_avail_size %u\n", __FUNCTION__,
             inst_info.wr_buf_avail_size);
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "ni_logan_query_status_info ret %d, sess_err_no %u "
             "inst_err_no %u inst_info.wr_buf_avail_size %d retry ..\n",
             retval, inst_info.sess_err_no, inst_info.inst_err_no,
             inst_info.wr_buf_avail_size);
      if (query_retry > NI_LOGAN_MAX_ENC_SESSION_OPEN_QUERY_RETRIES)  // 3000 retries
      {
        ni_log(NI_LOG_ERROR, "ERROR: %s timeout\n", __FUNCTION__);
        retval = NI_LOGAN_RETCODE_FAILURE;
        LRETURN;
      }
      ni_logan_usleep(NI_LOGAN_ENC_SESSION_OPEN_RETRY_INTERVAL_US);  // 1000us
    }
  }

  ni_log(NI_LOG_TRACE, "%s(): p_ctx->device_handle=%" PRIx64 " "
         "p_ctx->hw_id=%d, p_ctx->session_id=%d\n", __FUNCTION__,
         (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  END:

  ni_logan_aligned_free(p_signature);
  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Flush encoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_flush(ni_logan_session_context_t* p_ctx)
{
  ni_logan_retcode_t retval;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: session context is null, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session id, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_logan_config_instance_eos(p_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    p_ctx->ready_to_close = 1;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(), return\n", __FUNCTION__);
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): success exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_session_close(ni_logan_session_context_t* p_ctx, int eos_recieved)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int counter = 0;
  int ret = 0;
  int i;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null! return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_SUCCESS;
    LRETURN;
  }

  ni_log(NI_LOG_ERROR, "Encoder_complete_info: session_id 0x%x, total frames input: %u  "
               "buffered: %u  completed: %u  output: %u  dropped: %u ,  "
               "inst_errors: %u\n", p_ctx->session_id, p_ctx->session_stats.frames_input,
               p_ctx->session_stats.frames_buffered, p_ctx->session_stats.frames_completed,
               p_ctx->session_stats.frames_output, p_ctx->session_stats.frames_dropped,
               p_ctx->session_stats.inst_errors);

  //malloc data buffer
  if(ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_ENCODER);

  int retry = 0;
  while (retry < NI_LOGAN_MAX_SESSION_CLOSE_RETRIES)  // 10 retries
  {
    ni_log(NI_LOG_TRACE, "%s(): p_ctx->blk_io_handle=%" PRIx64 " p_ctx->hw_id=%d "
           "p_ctx->session_id=%d, close_mode=1\n", __FUNCTION__,
           (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): command failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      break;
    }
    else if(((ni_logan_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_LOGAN_RETCODE_SUCCESS;
      p_ctx->session_id = NI_LOGAN_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "%s(): wait for close\n", __FUNCTION__);
      ni_logan_usleep(NI_LOGAN_SESSION_CLOSE_RETRY_INTERVAL_US); // 500000 us
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    }

    retry++;
  }

  END:

  ni_logan_aligned_free(p_buffer);
  ni_logan_aligned_free(p_ctx->p_all_zero_buf);

  //Sequence change related stuff cleanup here
  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->active_bit_depth = 0;
  //End of sequence change related stuff cleanup

  if ((ni_logan_timestamp_table_t*)p_ctx->pts_table)
  {
    if (p_ctx->pts_table)
    {
      ni_logan_timestamp_table_t* p_pts_table = p_ctx->pts_table;
      ni_logan_queue_free(&p_pts_table->list, p_ctx->buffer_pool);
      free(p_ctx->pts_table);
      p_ctx->pts_table = NULL;
      ni_log(NI_LOG_TRACE, "ni_logan_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
      ni_logan_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
      ni_logan_queue_free(&p_dts_queue->list, p_ctx->buffer_pool);
      free(p_ctx->dts_queue);
      p_ctx->dts_queue = NULL;
      ni_log(NI_LOG_TRACE, "ni_logan_timestamp_done: success\n");
    }
  }

  ni_logan_buffer_pool_free(p_ctx->buffer_pool);
  p_ctx->buffer_pool = NULL;

  for (i = 0; i < NI_LOGAN_FIFO_SZ; i++)
  {
    free(p_ctx->pkt_custom_sei[i]);
    p_ctx->pkt_custom_sei[i] = NULL;
  }

  struct timeval tv;
  ni_logan_gettimeofday(&tv, NULL);
  uint64_t codec_end_time = tv.tv_sec*1000000ULL + tv.tv_usec;

  //if close immediately after opened, end time may equals to start time
  if (p_ctx->codec_total_ticks && codec_end_time - p_ctx->codec_start_time)
  {
    uint32_t ni_logan_usage = (uint32_t)((p_ctx->codec_total_ticks / NI_LOGAN_VPU_FREQ) * 100 /
                                   (codec_end_time - p_ctx->codec_start_time));
    ni_log(NI_LOG_INFO, "Encoder HW[%d] INST[%d]-average usage:%d%%\n",
           p_ctx->hw_id, (p_ctx->session_id&0x7F), ni_logan_usage);
  }
  else
  {
    ni_log(NI_LOG_INFO, "Warning Encoder HW[%d] INST[%d]-average usage equals to 0\n",
           p_ctx->hw_id, (p_ctx->session_id&0x7F));
  }

  ni_log(NI_LOG_TRACE, "encoder total_pkt:%" PRIu64 " total_ticks:%" PRIu64 " "
         "total_time:%" PRIu64 " us\n", p_ctx->pkt_num, p_ctx->codec_total_ticks,
         codec_end_time - p_ctx->codec_start_time);

  ni_log(NI_LOG_TRACE, "%s(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
         __FUNCTION__, (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a YUV p_frame to encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_encoder_session_write(ni_logan_session_context_t* p_ctx, ni_logan_frame_t* p_frame)
{
  bool ishwframe = p_ctx->hw_action & NI_LOGAN_CODEC_HW_ENABLE;
  uint32_t size = 0;
  uint32_t metadata_size = NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t send_count = 0;
  uint32_t i = 0;
  uint32_t tx_size = 0, aligned_tx_size = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  int retval = 0;
  ni_logan_instance_status_info_t inst_info = { 0 };

#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invlid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_LOGAN_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
      abs_time_ns = ni_logan_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_logan_lat_meas_q_add_entry(p_ctx->frame_time_q, abs_time_ns, p_frame->dts);
  }
#endif

  /*!********************************************************************/
  /*!************ Sequence Change related stuff *************************/
  //First check squence changed related stuff.
  //We need to record the current hight/width params if we didn't do it before:

  if ( p_frame->video_height)
  {
    p_ctx->active_video_width = p_frame->data_len[0] / p_frame->video_height;
    p_ctx->active_video_height = p_frame->video_height;
    p_ctx->active_bit_depth = p_frame->bit_depth;
  }
  else if (p_frame->video_width)
  {
    ni_log(NI_LOG_TRACE, "WARNING: passed video_height is not valid! return\n");
    p_ctx->active_video_height = p_frame->data_len[0] / p_frame->video_width;
    p_ctx->active_video_width = p_frame->video_width;
    p_ctx->active_bit_depth = p_frame->bit_depth;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed video_height and video_width are not valid! return\n");
    retval = NI_LOGAN_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  /*!************ Sequence Change related stuff end*************************/
  /*!********************************************************************/

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + p_frame->extra_data_len;

  // this is a temporary fix to avoid hw frame buffer pool full when enable yuvbypass
  if (ishwframe && (p_ctx->frame_num >= (p_ctx->pkt_num + 8)) &&
      (p_ctx->session_run_state == LOGAN_SESSION_RUN_STATE_NORMAL))
  {
    p_ctx->status = NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
    ni_log(NI_LOG_TRACE, "Set Enc buffer full\n");
  }

  for (; ;)
  {
    retval = ni_logan_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval ||
        inst_info.wr_buf_avail_size < frame_size_bytes)
    {
      ni_log(NI_LOG_TRACE, "Warning enc write query try %u fail rc %d or "
             "available buf size %u < frame size %u !\n", send_count, retval,
             inst_info.wr_buf_avail_size, frame_size_bytes);
      if (send_count >= NI_LOGAN_MAX_ENC_SESSION_WRITE_QUERY_RETRIES) // 2000 retries
      {
        ni_log(NI_LOG_TRACE, "ERROR enc query buf info exceeding max retries:%d",
               NI_LOGAN_MAX_ENC_SESSION_WRITE_QUERY_RETRIES);
        p_ctx->required_buf_size = frame_size_bytes;
        p_ctx->status = NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }
      send_count++;
      ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
      ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_100US);  // 100 us
      ni_logan_pthread_mutex_lock(&p_ctx->mutex);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "Info enc write query success, available buf "
             "size %u >= frame size %u !\n", inst_info.wr_buf_avail_size,
             frame_size_bytes);
      break;
    }
  }

  // fill in metadata such as timestamp
  ni_logan_metadata_enc_frame_t *p_meta =
    (ni_logan_metadata_enc_frame_t *)((uint8_t *)p_frame->p_data[2 + ishwframe] +
                                p_frame->data_len[2 + ishwframe]);

  if (p_meta) //When hwframe xcoding reaches eos, frame looks like swframe but no allocation for p_meta
  {
    p_meta->metadata_common.ui64_data.frame_tstamp = (uint64_t)p_frame->pts;

    p_meta->force_headers = 0; // p_frame->force_headers not implemented/used
    p_meta->use_cur_src_as_long_term_pic = p_frame->use_cur_src_as_long_term_pic;
    p_meta->use_long_term_ref            = p_frame->use_long_term_ref;

    p_meta->frame_force_type_enable = p_meta->frame_force_type = 0;
    // frame type to be forced to is supposed to be set correctly
    // in p_frame->ni_logan_pict_type
    if (1 == p_ctx->force_frame_type || p_frame->force_key_frame)
    {
      if (p_frame->ni_logan_pict_type)
      {
        p_meta->frame_force_type_enable = 1;
        p_meta->frame_force_type = p_frame->ni_logan_pict_type;
      }
      ni_log(NI_LOG_TRACE, "%s(): ctx->force_frame_type %d "
             "frame->force_key_frame %d force frame_num %lu type to %d\n",
             __FUNCTION__, p_ctx->force_frame_type, p_frame->force_key_frame,
             p_ctx->frame_num, p_frame->ni_logan_pict_type);
    }
    ni_log(NI_LOG_TRACE, "%s(): ctx->force_frame_type %d "
           "frame->force_key_frame %d force frame_num %" PRIu64 " type to %d\n",
           __FUNCTION__, p_ctx->force_frame_type, p_frame->force_key_frame,
           p_ctx->frame_num, p_frame->ni_logan_pict_type);
  }

  // force pic qp if specified
  p_meta->force_pic_qp_enable = p_meta->force_pic_qp_i =
  p_meta->force_pic_qp_p = p_meta->force_pic_qp_b = 0;
  if (p_frame->force_pic_qp)
  {
    p_meta->force_pic_qp_enable = 1;
    p_meta->force_pic_qp_i = p_meta->force_pic_qp_p =
    p_meta->force_pic_qp_b = p_frame->force_pic_qp;
  }
  p_meta->frame_sei_data_size = p_frame->sei_total_len;
  p_meta->frame_roi_map_size = p_frame->roi_len;
  p_meta->frame_roi_avg_qp = p_ctx->roi_avg_qp;
  p_meta->enc_reconfig_data_size = p_frame->reconf_len;

  ni_log(NI_LOG_TRACE, "%s(): %d.%d p_ctx->frame_num=%" PRIu64 ", p_frame->start_of_stream=%u, end_of_stream=%u, "
         "video_width=%u, video_height=%u, pts=0x%08x 0x%08x, dts=0x%08x 0x%08x, sei_len=%u, roi size=%u "
         "avg_qp=%u reconf_len=%u force_pic_qp=%u force_headers=%u frame_force_type_enable=%u frame_force_type=%u "
         "force_pic_qp_enable=%u force_pic_qp_i/p/b=%u use_cur_src_as_long_term_pic %u use_long_term_ref %u\n",
         __FUNCTION__,  p_ctx->hw_id, p_ctx->session_id, p_ctx->frame_num,
         p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height,
         (uint32_t)((p_frame->pts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_frame->pts & 0xFFFFFFFF),
         (uint32_t)((p_frame->dts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_frame->dts & 0xFFFFFFFF),
         p_meta->frame_sei_data_size, p_meta->frame_roi_map_size,
         p_meta->frame_roi_avg_qp, p_meta->enc_reconfig_data_size,
         p_meta->force_pic_qp_i, p_meta->force_headers,
         p_meta->frame_force_type_enable, p_meta->frame_force_type,
         p_meta->force_pic_qp_enable, p_meta->force_pic_qp_i,
         p_meta->use_cur_src_as_long_term_pic, p_meta->use_long_term_ref);

  if (p_frame->start_of_stream)
  {
    //Send Start of stream p_config command here
    retval = ni_logan_config_instance_sos(p_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }

    p_frame->start_of_stream = 0;
  }

  // skip direct to send eos without sending the passed in p_frame as it's been sent already
  if (p_frame->end_of_stream)
  {
    retval = ni_logan_config_instance_eos(p_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }

    p_frame->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }
  else //handle regular frame sending
  {
    if (p_frame->p_data)
    {
      retval = ni_logan_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_frame->dts, 0);
      if (NI_LOGAN_RETCODE_SUCCESS != retval)
      {
        ni_log(NI_LOG_ERROR, "ERROR %s(): ni_logan_timestamp_register() for dts returned: %d\n", __FUNCTION__, retval);
      }

      uint32_t ui32LBA = WRITE_INSTANCE_W(ishwframe, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_ENCODER);
      ni_log(NI_LOG_TRACE, "%s: p_data = %p, p_frame->buffer_size = %u, "
             "p_ctx->frame_num = %"PRIu64", LBA = 0x%x\n", __FUNCTION__,
             p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num, ui32LBA);
      sent_size = frame_size_bytes;
      if (sent_size % NI_LOGAN_MEM_PAGE_ALIGNMENT)
      {
        sent_size = ( (sent_size / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
      }

      retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_frame->p_buffer, sent_size, ui32LBA);
      CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);
      if (retval < 0)
      {
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
        retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
      }

      size = frame_size_bytes;
      p_ctx->frame_num++;
    }
  }

  retval = size;

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

int ni_logan_encoder_session_read(ni_logan_session_context_t* p_ctx, ni_logan_packet_t* p_packet)
{
  ni_logan_instance_mgr_stream_info_t data = { 0 };
  bool ishwframe = p_ctx->hw_action & NI_LOGAN_CODEC_HW_ENABLE;
  uint32_t chunk_max_size = p_ctx->max_nvme_io_size;
  uint32_t actual_read_size = 0, chunk_size, end_of_pkt;
  uint32_t actual_read_size_aligned = 0;
  int reading_partial_pkt = 0;
  uint32_t to_read_size = 0;
  int size = 0;
  uint32_t query_return_size = 0;
  uint8_t* p_data = NULL;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t query_retry = 0;
  ni_logan_metadata_enc_bstream_t *p_meta = NULL;
  ni_logan_instance_status_info_t inst_info = { 0 };

#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_packet || !p_packet->p_data)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "xcoder instance id == 0, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  p_packet->data_len = 0;
  p_packet->pts = NI_LOGAN_NOPTS_VALUE;
  p_packet->dts = 0;

enc_read_query:
  query_retry = 0;

  while (1)
  {
    query_retry++;
    retval = ni_logan_query_status_info(p_ctx, p_ctx->device_type, &inst_info,retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_TRACE, "Info enc read query try %u rc %d, available buf size %u, "
           "frame_num=%"PRIu64", pkt_num=%"PRIu64" reading_partial_pkt %d\n",
           query_retry, retval, inst_info.rd_buf_avail_size, p_ctx->frame_num,
           p_ctx->pkt_num, reading_partial_pkt);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "Buffer info query failed in encoder read!!!!\n");
      LRETURN;
    }
    else if (0 == inst_info.rd_buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (! reading_partial_pkt && p_ctx->ready_to_close)
      {
        retval = ni_logan_query_stream_info(p_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER, &data, ishwframe);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_LOGAN_RETCODE_SUCCESS != retval)
        {
          ni_log(NI_LOG_ERROR, "Stream info query failed in encoder read !!\n");
          LRETURN;
        }

        if (data.is_flushed)
        {
          p_packet->end_of_stream = 1;
        }
      }
      ni_log(NI_LOG_TRACE, "Info enc read available buf size %u, eos %u !\n",
             inst_info.rd_buf_avail_size, p_packet->end_of_stream);

      if (((ni_logan_encoder_params_t *)p_ctx->p_session_config)->strict_timeout_mode
          && (query_retry > NI_LOGAN_MAX_ENC_SESSION_READ_QUERY_RETRIES)) // 3000 retries
      {
        ni_log(NI_LOG_ERROR, "ERROR Receive Packet Strict Timeout, Encoder low "
               "latency mode %d, buf_full %d eos sent %d, frame_num %"PRIu64" "
               ">= %"PRIu64" pkt_num, retry limit exceeded and exit encoder "
               "pkt reading.\n",
               ((ni_logan_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode,
               NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status,
               p_ctx->ready_to_close, p_ctx->frame_num, p_ctx->pkt_num);
        retval = NI_LOGAN_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
        LRETURN;
      }

      if ((((ni_logan_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode ||
           (NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) ||
           p_ctx->ready_to_close || reading_partial_pkt) &&
          ! p_packet->end_of_stream && p_ctx->frame_num >= p_ctx->pkt_num)
      {
        ni_log(NI_LOG_TRACE, "Encoder low latency mode %d, buf_full %d eos sent"
               " %d, reading_partial_pkt %d, frame_num %"PRIu64" >= %"PRIu64" "
               "pkt_num, keep querying.\n",
               ((ni_logan_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode,
               NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status,
               p_ctx->ready_to_close, reading_partial_pkt,
               p_ctx->frame_num, p_ctx->pkt_num);
        ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
        ni_logan_usleep(NI_LOGAN_RETRY_INTERVAL_200US);  // 200 us
        ni_logan_pthread_mutex_lock(&p_ctx->mutex);
        continue;
      }
      retval = NI_LOGAN_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
      if ((NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) &&
          (inst_info.wr_buf_avail_size > p_ctx->required_buf_size))
      {
        p_ctx->status = 0;
      }
      break;
    }
  }
  ni_log(NI_LOG_TRACE, "Encoder read buf_avail_size %u\n",
         inst_info.rd_buf_avail_size);

  to_read_size = inst_info.rd_buf_avail_size;

  ui32LBA = READ_INSTANCE_R(ishwframe, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_ENCODER);

  if (to_read_size % NI_LOGAN_MEM_PAGE_ALIGNMENT)
  {
    to_read_size = ((to_read_size / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                    NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 (uint8_t *)p_packet->p_data + actual_read_size_aligned,
                                 to_read_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read,
               p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): read command failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  // retrieve metadata pkt related info only once, when eop = 1
  p_meta = (ni_logan_metadata_enc_bstream_t *)((uint8_t *)p_packet->p_data +
                                         actual_read_size_aligned);
  chunk_size = p_meta->bs_frame_size;
  end_of_pkt = p_meta->end_of_packet;
  if (end_of_pkt)
  {
    p_packet->frame_type = p_meta->frame_type;
    p_packet->pts = (int64_t)(p_meta->frame_tstamp);
    p_ctx->codec_total_ticks += p_meta->frame_cycle;
    p_packet->avg_frame_qp = p_meta->avg_frame_qp;
    p_packet->recycle_index = p_meta->recycle_index;
    ni_log(NI_LOG_TRACE, "RECYCLE INDEX = %d!!! \n", p_meta->recycle_index);
  }

  if (0 == actual_read_size)
  {
    actual_read_size = sizeof(ni_logan_metadata_enc_bstream_t) + chunk_size;
  }
  else
  {
    memmove((uint8_t *)p_packet->p_data + actual_read_size,
            (uint8_t*)p_packet->p_data + actual_read_size_aligned +
            sizeof(ni_logan_metadata_enc_bstream_t), chunk_size);
    actual_read_size += chunk_size;
  }

  actual_read_size_aligned = actual_read_size;
  if (actual_read_size_aligned % NI_LOGAN_MEM_PAGE_ALIGNMENT)
  {
    actual_read_size_aligned = ((actual_read_size / NI_LOGAN_MEM_PAGE_ALIGNMENT)
                         * NI_LOGAN_MEM_PAGE_ALIGNMENT ) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
  }

  ni_log(NI_LOG_TRACE, "%s(): read %u so far %u (%u) bytes, end_of_pkt: %u\n",
         __FUNCTION__, chunk_size, actual_read_size, actual_read_size_aligned,
         end_of_pkt);

  if (! end_of_pkt)
  {
    reading_partial_pkt = 1;
    goto enc_read_query;
  }

  p_packet->data_len = actual_read_size;

  size = p_packet->data_len;

  if (size > 0)
  {
    if (p_ctx->pkt_num >= 1)
    {
      if (ni_logan_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)& p_packet->dts, 0,
                                          p_ctx->pkt_num % 500 == 0, p_ctx->buffer_pool) != NI_LOGAN_RETCODE_SUCCESS)
      {
        p_packet->dts = NI_LOGAN_NOPTS_VALUE;
      }
      p_ctx->pkt_num++;
    }

  }

  ni_log(NI_LOG_TRACE, "%s(): %d.%d p_packet->start_of_stream=%d end_of_stream=%d "
         "video_width=%d, video_height=%d, dts=0x%08x 0x%08x, pts=0x%08x 0x%08x, "
         "type=%u, avg_frame_qp=%u\n", __FUNCTION__, p_ctx->hw_id,
         p_ctx->session_id, p_packet->start_of_stream, p_packet->end_of_stream,
         p_packet->video_width, p_packet->video_height,
         (uint32_t)((p_packet->dts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_packet->dts & 0xFFFFFFFF),
         (uint32_t)((p_packet->pts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_packet->pts & 0xFFFFFFFF), p_packet->frame_type,
         p_packet->avg_frame_qp);

  ni_log(NI_LOG_TRACE, "%s(): p_packet->data_len=%u, size=%u\n",
         __FUNCTION__, p_packet->data_len, size);

  if (p_ctx->pkt_num % 500 == 0)
  {
    ni_log(NI_LOG_TRACE, "Encoder pts queue size = %d dts queue size = %d\n\n",
    ((ni_logan_timestamp_table_t*)p_ctx->pts_table)->list.count,
      ((ni_logan_timestamp_table_t*)p_ctx->dts_queue)->list.count);
  }

  retval = size;

#ifdef MEASURE_LATENCY
  if (p_packet->dts != NI_LOGAN_NOPTS_VALUE && p_ctx->frame_time_q != NULL)
  {
#ifdef _WIN32
    abs_time_ns = ni_logan_gettime_ns();
#else
    clock_gettime(CLOCK_REALTIME, &logtv);
    abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
    ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,eLAT:%lu;\n",
           p_packet->dts,
           abs_time_ns - p_ctx->prev_read_frame_time,
           ni_logan_lat_meas_q_check_latency(p_ctx->frame_time_q, abs_time_ns, p_packet->dts));
    p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Query current encoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_encoder_session_query(ni_logan_session_context_t* p_ctx)
{
  ni_logan_instance_mgr_general_status_t data;
  int retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null! return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  retval = ni_logan_query_general_status(p_ctx, NI_LOGAN_DEVICE_TYPE_ENCODER, &data);

  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    p_ctx->load_query.current_load = (uint32_t)data.process_load_percent;
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.total_contexts =
    (uint32_t)data.active_sub_instances_cnt;
    ni_log(NI_LOG_TRACE, "%s current_load:%d fw_model_load:%d fw_video_mem_usage:%d "
           "active_contexts %d\n", __FUNCTION__, p_ctx->load_query.current_load,
           p_ctx->load_query.fw_model_load, p_ctx->load_query.fw_video_mem_usage,
           p_ctx->load_query.total_contexts);
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

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
int ni_logan_query_general_status(ni_logan_session_context_t* p_ctx,
                            ni_logan_device_type_t device_type,
                            ni_logan_instance_mgr_general_status_t* p_gen_status)
{
  void* p_buffer = NULL;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_logan_instance_mgr_general_status_t) + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                      NI_LOGAN_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_gen_status)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ui32LBA = QUERY_GENERAL_GET_STATUS_R(device_type);

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  //No need to flip the bytes since the datastruct has only uint8_t datatypes
  memcpy((void*)p_gen_status, p_buffer, sizeof(ni_logan_instance_mgr_general_status_t));

  ni_log(NI_LOG_TRACE, "%s(): model_load:%d qc:%d percent:%d\n", __FUNCTION__,
                 p_gen_status->fw_model_load, p_gen_status->cmd_queue_count,
                 p_gen_status->process_load_percent);
  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

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
                                  bool is_hw)
{
  void* p_buffer = NULL;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_logan_instance_mgr_stream_info_t) + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                     NI_LOGAN_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_stream_info)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_STREAM_INFO_R(is_hw, p_ctx->session_id, device_type);

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_stream_info, p_buffer, sizeof(ni_logan_instance_mgr_stream_info_t));

  //flip the bytes to host order
  p_stream_info->picture_width = ni_logan_htons(p_stream_info->picture_width);
  p_stream_info->picture_height = ni_logan_htons(p_stream_info->picture_height);
  p_stream_info->frame_rate = ni_logan_htons(p_stream_info->frame_rate);
  p_stream_info->is_flushed = ni_logan_htons(p_stream_info->is_flushed);

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get status Info data
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_logan_instance_status_info_t *out - Struct preallocated from the
 *           caller where the resulting data will be placed
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *            on failure
 ******************************************************************************/
ni_logan_retcode_t ni_logan_query_status_info(ni_logan_session_context_t* p_ctx,
                                  ni_logan_device_type_t device_type,
                                  ni_logan_instance_status_info_t* p_status_info,
                                  int rc,
                                  int opcode)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_logan_instance_status_info_t) + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                     NI_LOGAN_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_status_info)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_CUR_STATUS_INFO_R(p_ctx->session_id, device_type);

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  // There are some cases that FW will not dma the data but just return error status, so add a default error number.
  // for example open more than 32 encoder sessions at the same time
  ((ni_logan_instance_status_info_t *)p_buffer)->sess_err_no = NI_LOGAN_RETCODE_DEFAULT_SESSION_ERR_NO;
  if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA)
      < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): read command Failed\n", __FUNCTION__);
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_status_info, p_buffer, sizeof(ni_logan_instance_status_info_t));

  // flip the bytes to host order
  p_status_info->sess_err_no = ni_logan_htons(p_status_info->sess_err_no);
  p_status_info->sess_rsvd = ni_logan_htons(p_status_info->sess_rsvd);
  p_status_info->inst_state = ni_logan_htons(p_status_info->inst_state);
  p_status_info->inst_err_no = ni_logan_htons(p_status_info->inst_err_no);
  p_status_info->wr_buf_avail_size = ni_logan_htonl(p_status_info->wr_buf_avail_size);
  p_status_info->rd_buf_avail_size = ni_logan_htonl(p_status_info->rd_buf_avail_size);
  p_status_info->sess_timestamp = ni_logan_htonll(p_status_info->sess_timestamp);
  // session statistics
  p_status_info->frames_input = ni_logan_htonl(p_status_info->frames_input);
  p_status_info->frames_buffered = ni_logan_htonl(p_status_info->frames_buffered);
  p_status_info->frames_completed = ni_logan_htonl(p_status_info->frames_completed);
  p_status_info->frames_output = ni_logan_htonl(p_status_info->frames_output);
  p_status_info->frames_dropped = ni_logan_htonl(p_status_info->frames_dropped);
  p_status_info->inst_errors = ni_logan_htonl(p_status_info->inst_errors);

  // get the session timestamp when open session
  // check the timestamp during transcoding
  if (NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP == opcode)
  {
    p_ctx->session_timestamp = p_status_info->sess_timestamp;
    ni_log(NI_LOG_TRACE, "Session Open instance id:%u, timestamp:%" PRIu64 "\n",
           p_ctx->session_id, p_ctx->session_timestamp);
  }
  else if (p_ctx->session_timestamp != p_status_info->sess_timestamp &&
           ni_logan_xcoder_resource_recovery != p_status_info->inst_err_no) // if VPU recovery, the session timestamp will be reset.
  {
    ni_log(NI_LOG_ERROR, "instance id invalid:%u, timestamp:%" PRIu64 ", "
           "query timestamp:%" PRIu64 ", sess_err_no:%d\n",
           p_ctx->session_id, p_ctx->session_timestamp,
           p_status_info->sess_timestamp, p_status_info->sess_err_no);
    p_status_info->sess_err_no = NI_LOGAN_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
  }

  // map the ni_logan_xcoder_mgr_retcode_t (regular i/o rc) to ni_logan_retcode_t
  switch (p_status_info->inst_err_no)
  {
  case ni_logan_xcoder_request_success:
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_SUCCESS;
    break;
  case ni_logan_xcoder_general_error:
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
    break;
  case ni_logan_xcoder_request_pending:
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_REQUEST_IN_PROGRESS;
    break;
  case ni_logan_xcoder_resource_recovery:
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_VPU_RECOVERY;
    break;
  case ni_logan_xcoder_resource_insufficient:
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT;
    break;
  default:
    ; // kept unchanged
  }

  // check rc here, if rc != NI_LOGAN_RETCODE_SUCCESS, it means that last read/write command failed
  // failures may be link layer errors, such as physical link errors or ERROR_WRITE_PROTECT in windows.
  if (NI_LOGAN_RETCODE_SUCCESS != rc)
  {
    ni_log(NI_LOG_ERROR, "%s():last command Failed: rc %d\n", __FUNCTION__, rc);
    p_status_info->inst_err_no = NI_LOGAN_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s stats, frames input: %u buffered: %u "
           "completed: %u  output: %u  dropped: %u ,inst_errors: %u\n",
           __FUNCTION__, p_status_info->frames_input,
           p_status_info->frames_buffered, p_status_info->frames_completed,
           p_status_info->frames_output, p_status_info->frames_dropped,
           p_status_info->inst_errors);
  }

  ni_log(NI_LOG_TRACE, "%s(): sess_err_no %u inst_state %u inst_err_no 0x%x\n",
         __FUNCTION__, p_status_info->sess_err_no, p_status_info->inst_state,
         p_status_info->inst_err_no);

  p_ctx->session_stats = *p_status_info;

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}


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
                                        bool is_hw)
{
  void* p_buffer = NULL;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_logan_instance_buf_info_t) +
                      (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT
                     ) * NI_LOGAN_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_inst_buf_info)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (INST_BUF_INFO_RW_READ == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_RBUFF_SIZE_R(is_hw, p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_WRITE == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_WBUFF_SIZE_R(is_hw, p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_UPLOAD == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_UPLOAD_ID_R(is_hw, p_ctx->session_id, device_type);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ERROR: Unknown query type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_inst_buf_info, p_buffer, sizeof(ni_logan_instance_buf_info_t));

  p_inst_buf_info->buf_avail_size = ni_logan_htonl(p_inst_buf_info->buf_avail_size);

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Configure the read/write pipe for a session
 *          Use HW frame index to read/write the YUV from/to HW
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_session_config_rw_t rw_type
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   uint8_t enable
 *  \param   uint8_t hw_action
 *  \param   uint16_t frame_id
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
 *            NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_rw(ni_logan_session_context_t* p_ctx,
                                   ni_logan_inst_config_rw_type_t rw_type,
                                   uint8_t enable,
                                   uint8_t hw_action,
                                   uint16_t frame_id)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;
  ni_logan_inst_config_rw_t * rw_config = NULL;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if ( !((INST_READ_CONFIG == rw_type) ||
         (INST_WRITE_CONFIG == rw_type)))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Unknown rw type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  buffer_size = ((sizeof(ni_logan_inst_config_rw_t) + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;
  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate read write config buffer.\n");
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, buffer_size);

  rw_config = (ni_logan_inst_config_rw_t *)p_buffer;
  rw_config->ui8Enable = enable;
  rw_config->ui8HWAccess = hw_action;
  switch(rw_type)
  {
    case INST_READ_CONFIG:
      rw_config->uHWAccessField.ui16ReadFrameId = frame_id;
      break;
    case INST_WRITE_CONFIG:
      rw_config->uHWAccessField.ui16WriteFrameId = frame_id;
      break;
    default:
      ni_log(NI_LOG_ERROR, "ERROR: Unknown rw type, return\n");
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  switch(rw_type)
  {
    case INST_READ_CONFIG:
      ui32LBA = CONFIG_INSTANCE_FrameIdx_W(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
      break;
    case INST_WRITE_CONFIG:
      ui32LBA = CONFIG_SESSION_Write_W(p_ctx->session_id);
      break;
    default:
      ni_log(NI_LOG_ERROR, "ERROR: Unknown rw type, return\n");
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  if (ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for Start Of Stream
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder, Decoder or Uploader
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION. NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_sos(ni_logan_session_context_t* p_ctx,
                                    ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  bool is_hw = (NI_LOGAN_DEVICE_TYPE_UPLOAD == device_type);

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_UPLOAD == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetSOS_W(is_hw, p_ctx->session_id, device_type);

  if (ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf,
                             NI_LOGAN_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, " %s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for End Of Stream
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *  \param   ni_logan_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION, NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_eos(ni_logan_session_context_t* p_ctx,
                                    ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ||
         NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetEOS_W(p_ctx->session_id, device_type);

  if (ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf,
      NI_LOGAN_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}


/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding parameters.
 *
 *  \param   ni_logan_session_context_t p_ctx - xcoder Context
 *
 *  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION, NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_logan_retcode_t ni_logan_config_instance_set_encoder_params(ni_logan_session_context_t* p_ctx)
{
  void* p_encoder_config = NULL;
  ni_logan_encoder_config_t* p_cfg = NULL;
  uint32_t buffer_size = sizeof(ni_logan_encoder_config_t);
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t i = 0;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): NULL pointer p_config passed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;
  if (ni_logan_posix_memalign(&p_encoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate encConf buffer.\n", NI_ERRNO);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_encoder_config, 0, buffer_size);

  ni_logan_set_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config);
  if (NI_LOGAN_RETCODE_SUCCESS !=ni_logan_validate_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config,
                                                       p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg)))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_logan_validate_custom_template failed. %s\n",
                   p_ctx->param_err_msg);
    ni_log(NI_LOG_INFO, "ERROR: ni_logan_validate_custom_template failed. %s\n",
                   p_ctx->param_err_msg);
    fflush(stderr);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  // configure the session
  ui32LBA = CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_ENCODER);

  //Flip the bytes!!
  p_cfg = (ni_logan_encoder_config_t*)p_encoder_config;

  uint8_t str_vui[4 * NI_LOGAN_MAX_VUI_SIZE];
  for (i = 0; i < p_cfg->ui32VuiDataSizeBytes; i++)
  {
    snprintf(&str_vui[i * 3], 4, "%.2x ", p_cfg->ui8VuiRbsp[i]);
  }
  str_vui[3 * p_cfg->ui32VuiDataSizeBytes] = '\0';
  ni_log(NI_LOG_DEBUG, "VUI = %s\n", str_vui);

  p_cfg->i32picWidth = ni_logan_htonl(p_cfg->i32picWidth);
  p_cfg->i32picHeight = ni_logan_htonl(p_cfg->i32picHeight);
  p_cfg->i32meBlkMode = ni_logan_htonl(p_cfg->i32meBlkMode);
  p_cfg->i32frameRateInfo = ni_logan_htonl(p_cfg->i32frameRateInfo);
  p_cfg->i32vbvBufferSize = ni_logan_htonl(p_cfg->i32vbvBufferSize);
  p_cfg->i32userQpMax = ni_logan_htonl(p_cfg->i32userQpMax);
  p_cfg->i32maxIntraSize = ni_logan_htonl(p_cfg->i32maxIntraSize);
  p_cfg->i32userMaxDeltaQp = ni_logan_htonl(p_cfg->i32userMaxDeltaQp);
  p_cfg->i32userMinDeltaQp = ni_logan_htonl(p_cfg->i32userMinDeltaQp);
  p_cfg->i32userQpMin = ni_logan_htonl(p_cfg->i32userQpMin);
  p_cfg->i32bitRate = ni_logan_htonl(p_cfg->i32bitRate);
  p_cfg->i32bitRateBL = ni_logan_htonl(p_cfg->i32bitRateBL);
  p_cfg->i32srcBitDepth = ni_logan_htonl(p_cfg->i32srcBitDepth);
  p_cfg->hdrEnableVUI = ni_logan_htonl(p_cfg->hdrEnableVUI);
  p_cfg->ui32VuiDataSizeBits = ni_logan_htonl(p_cfg->ui32VuiDataSizeBits);
  p_cfg->ui32VuiDataSizeBytes = ni_logan_htonl(p_cfg->ui32VuiDataSizeBytes);
  p_cfg->ui32flushGop = ni_logan_htonl(p_cfg->ui32flushGop);
  p_cfg->ui32minIntraRefreshCycle = ni_logan_htonl(p_cfg->ui32minIntraRefreshCycle);
  p_cfg->ui32fillerEnable = ni_logan_htonl(p_cfg->ui32fillerEnable);
  p_cfg->ui8hwframes = ni_logan_htonl(p_cfg->ui8hwframes);
  p_cfg->ui8explicitRefListEnable = ni_logan_htonl(p_cfg->ui8explicitRefListEnable);
  // no flipping reserved field as enableAUD now takes one byte from it

  // flip the NI_LOGAN_MAX_VUI_SIZE bytes of the VUI field using 32 bits pointers
  for (i = 0 ; i < (NI_LOGAN_MAX_VUI_SIZE >> 2) ; i++) // apply on 32 bits
  {
    ((uint32_t*)p_cfg->ui8VuiRbsp)[i] = ni_logan_htonl(((uint32_t*)p_cfg->ui8VuiRbsp)[i]);
  }

  retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_encoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_logan_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64 ", "
           "hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_logan_encoder_session_close(p_ctx, 0);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_logan_encoder_session_close failed: blk_io_handle: %" PRIx64 ", "
             "hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  END:

  ni_logan_aligned_free(p_encoder_config);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}


// return non-0 if SEI of requested type is found, 0 otherwise
static int find_sei(uint32_t sei_header,
                    ni_logan_sei_user_data_entry_t *pEntry,
                    ni_logan_h265_sei_user_data_type_t type,
                    uint32_t *pSeiOffset,
                    uint32_t *pSeiSize)
{
  int ret = 0;

  if ( (!pEntry) || (!pSeiOffset) || (!pSeiSize) )
  {
    return  ret;
  }

  if (sei_header & (1 << type))
  {
    *pSeiOffset = pEntry[type].offset;
    *pSeiSize = pEntry[type].size;
    ni_log(NI_LOG_TRACE, "%s sei type %d, offset: %u  size: %u\n",
           __FUNCTION__, type, *pSeiOffset, *pSeiSize);
    ret = 1;
  }

  return ret;
}

// return non-0 if prefix or suffix T.35 message is found
static int find_prefix_suffix_t35(uint32_t sei_header,
                                  ni_logan_t35_sei_mesg_type_t t35_type,
                                  ni_logan_sei_user_data_entry_t *pEntry,
                                  ni_logan_h265_sei_user_data_type_t type,
                                  uint32_t *pCcOffset,
                                  uint32_t *pCcSize)
{
  int ret = 0;
  uint8_t *ptr;

  if (!pEntry || !pCcOffset || !pCcSize)
  {
    return ret;
  }

  // Find first t35 message with CEA708 close caption (first 7
  // bytes are itu_t_t35_country_code 0xB5 0x00 (181),
  // itu_t_t35_provider_code = 0x31 (49),
  // ATSC_user_identifier = 0x47 0x41 0x39 0x34 ("GA94")
  // or HDR10+ header bytes
  if (sei_header & (1 << type))
  {
    ptr = (uint8_t*) pEntry + pEntry[type].offset;
    if (NI_LOGAN_T35_SEI_CLOSED_CAPTION == t35_type &&
        ptr[0] == NI_CC_SEI_BYTE0 && ptr[1] == NI_CC_SEI_BYTE1 &&
        ptr[2] == NI_CC_SEI_BYTE2 && ptr[3] == NI_CC_SEI_BYTE3 &&
        ptr[4] == NI_CC_SEI_BYTE4 && ptr[5] == NI_CC_SEI_BYTE5 &&
        ptr[6] == NI_CC_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_TRACE, "%s: close Caption SEI found in T.35 type %d, offset: %u  size: %u\n",
             __FUNCTION__, type, *pCcOffset, *pCcSize);
      ret = 1;
    }
    else if (NI_LOGAN_T35_SEI_HDR10_PLUS == t35_type &&
             ptr[0] == NI_HDR10P_SEI_BYTE0 && ptr[1] == NI_HDR10P_SEI_BYTE1 &&
             ptr[2] == NI_HDR10P_SEI_BYTE2 && ptr[3] == NI_HDR10P_SEI_BYTE3 &&
             ptr[4] == NI_HDR10P_SEI_BYTE4 && ptr[5] == NI_HDR10P_SEI_BYTE5 &&
             ptr[6] == NI_HDR10P_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_TRACE, "%s: HDR10+ SEI found in T.35 type %d, offset: %u  size: %u\n",
             __FUNCTION__, type, *pCcOffset, *pCcSize);
      ret = 1;
    }
  }

  return ret;
}

// return non-0 when HDR10+/close-caption is found, 0 otherwise
static int find_t35_sei(uint32_t sei_header,
                        ni_logan_t35_sei_mesg_type_t t35_type,
                        ni_logan_sei_user_data_entry_t *pEntry,
                        uint32_t *pCcOffset,
                        uint32_t *pCcSize)
{
  int ret = 0;

  if ( (!pEntry) || (!pCcOffset) || (!pCcSize) )
  {
    return ret;
  }

  *pCcOffset = *pCcSize = 0;

  // Check up to 3 T35 Prefix and Suffix SEI for close captions
  if (find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE_1,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_PRE_2,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF_1,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_LOGAN_H265_USERDATA_FLAG_ITU_T_T35_SUF_2,
                             pCcOffset, pCcSize)
    )
  {
    ret = 1;
  }
  return ret;
}

/*!******************************************************************************
 *  \brief  Get info from received p_frame
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_create_frame(ni_logan_frame_t* p_frame, uint32_t read_length, uint64_t* p_frame_offset, bool is_hw_frame)
{
  uint32_t rx_size = read_length; //get the length since its the only thing in DW10 now

  if (!p_frame || !p_frame_offset)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n", __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  uint8_t* p_buf = (uint8_t*) p_frame->p_buffer;

  *p_frame_offset = 0;

  int metadata_size = NI_LOGAN_FW_META_DATA_SZ - (1 - is_hw_frame) * sizeof(ni_logan_hwframe_surface_t);
  unsigned int video_data_size = p_frame->data_len[0] + p_frame->data_len[1] + \
                                 p_frame->data_len[2] + ((is_hw_frame) ? p_frame->data_len[3] : 0);
  ni_log(NI_LOG_TRACE, "%s rx_size = %d video_data_size = %d metadataSize = %d\n",
         __FUNCTION__, rx_size, video_data_size, metadata_size);

  p_frame->p_custom_sei = NULL;

  if (rx_size == metadata_size)
  {
    video_data_size = 0;
  }

  if (rx_size > video_data_size)
  {
    ni_logan_metadata_dec_frame_t* p_meta = (ni_logan_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer + video_data_size);


    *p_frame_offset = p_meta->metadata_common.ui64_data.frame_offset;
    rx_size -= metadata_size;
    p_frame->crop_top = p_meta->metadata_common.crop_top;
    p_frame->crop_bottom = p_meta->metadata_common.crop_bottom;
    p_frame->crop_left = p_meta->metadata_common.crop_left;
    p_frame->crop_right = p_meta->metadata_common.crop_right;
    p_frame->ni_logan_pict_type = p_meta->metadata_common.frame_type;
    p_frame->bit_depth =  p_meta->metadata_common.bit_depth;
    p_frame->video_width = p_meta->metadata_common.frame_width;
    p_frame->video_height = p_meta->metadata_common.frame_height;

    ni_log(NI_LOG_TRACE, "%s: is_hw_frame=%d, [metadata] cropRight=%u, cropLeft=%u, "
           "cropBottom=%u, cropTop=%u, frame_offset=%" PRIu64 ", pic=%ux%u %dbits, pict_type=%d, "
           "crop=%ux%u, sei header: 0x%0x  number %u  size %u \n", __FUNCTION__,
           is_hw_frame, p_frame->crop_right, p_frame->crop_left, p_frame->crop_bottom, p_frame->crop_top,
           p_meta->metadata_common.ui64_data.frame_offset, p_meta->metadata_common.frame_width,
           p_meta->metadata_common.frame_height, p_meta->metadata_common.bit_depth, p_frame->ni_logan_pict_type,
           p_frame->crop_right - p_frame->crop_left, p_frame->crop_bottom - p_frame->crop_top,
           p_meta->sei_header, p_meta->sei_number, p_meta->sei_size);

    p_frame->sei_total_len =
    p_frame->sei_cc_offset = p_frame->sei_cc_len =
    p_frame->sei_hdr_mastering_display_color_vol_offset =
    p_frame->sei_hdr_mastering_display_color_vol_len =
    p_frame->sei_hdr_content_light_level_info_offset =
    p_frame->sei_hdr_content_light_level_info_len =
    p_frame->sei_hdr_plus_offset = p_frame->sei_hdr_plus_len =
    p_frame->sei_alt_transfer_characteristics_offset =
    p_frame->sei_alt_transfer_characteristics_len =
    p_frame->vui_offset = p_frame->vui_len =
    p_frame->sei_user_data_unreg_offset = p_frame->sei_user_data_unreg_len = 0;
    if (p_meta->sei_header && p_meta->sei_number && p_meta->sei_size)
    {
      ni_logan_sei_user_data_entry_t *pEntry;
      uint32_t ui32CCOffset = 0, ui32CCSize = 0;

      rx_size -= p_meta->sei_size;

      pEntry = (ni_logan_sei_user_data_entry_t *)((uint8_t*)p_meta + metadata_size);

      if (find_t35_sei(p_meta->sei_header, NI_LOGAN_T35_SEI_HDR10_PLUS, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_plus_len = ui32CCSize;
        p_frame->sei_hdr_plus_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_TRACE, "%s: hdr10+ size=%u hdr10+ offset=%u\n",
               __FUNCTION__, p_frame->sei_hdr_plus_len, p_frame->sei_hdr_plus_offset);
      }
      else
      {
        ni_log(NI_LOG_TRACE, "%s: hdr+ NOT found in meta data!\n", __FUNCTION__);
      }

      if (find_t35_sei(p_meta->sei_header, NI_LOGAN_T35_SEI_CLOSED_CAPTION, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        uint8_t *ptr;
        // Found CC data at pEntry + ui32CCOffset
        ptr = (uint8_t*)pEntry + ui32CCOffset;
        // number of 3 byte close captions is bottom 5 bits of
        // 9th byte of T35 payload
        ui32CCSize = (ptr[8] & 0x1F) * 3;

        // return close caption data offset and length, and
        // skip past 10 header bytes to close caption data
        p_frame->sei_cc_len = ui32CCSize;
        p_frame->sei_cc_offset = video_data_size + metadata_size
        + ui32CCOffset + 10;

        p_frame->sei_total_len += p_frame->sei_cc_len;

        ni_log(NI_LOG_TRACE, "%s: close caption size %u offset %u = video "
               "size %u meta size %u off %u + 10\n", __FUNCTION__,
               p_frame->sei_cc_len, p_frame->sei_cc_offset, video_data_size,
               metadata_size, ui32CCOffset);
      }
      else
      {
        ni_log(NI_LOG_TRACE, "%s: close caption NOT found in meta data!\n", __FUNCTION__);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USERDATA_FLAG_MASTERING_COLOR_VOL,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_mastering_display_color_vol_len = ui32CCSize;
        p_frame->sei_hdr_mastering_display_color_vol_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_logan_dec_mastering_display_colour_volume_t* pColourVolume =
        (ni_logan_dec_mastering_display_colour_volume_t*)((uint8_t*)pEntry + ui32CCOffset);

        ni_log(NI_LOG_TRACE, "Display Primaries x[0]=%u y[0]=%u\n",
                       pColourVolume->display_primaries_x[0],
                       pColourVolume->display_primaries_y[0]);
        ni_log(NI_LOG_TRACE, "Display Primaries x[1]=%u y[1]=%u\n",
                       pColourVolume->display_primaries_x[1],
                       pColourVolume->display_primaries_y[1]);
        ni_log(NI_LOG_TRACE, "Display Primaries x[2]=%u y[2]=%u\n",
                       pColourVolume->display_primaries_x[2],
                       pColourVolume->display_primaries_y[2]);

        ni_log(NI_LOG_TRACE, "White Point x=%u y=%u\n",
                       pColourVolume->white_point_x,
                       pColourVolume->white_point_y);
        ni_log(NI_LOG_TRACE, "Display Mastering Lum, Max=%u Min=%u\n",
                       pColourVolume->max_display_mastering_luminance, pColourVolume->min_display_mastering_luminance);
      }
      if (find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_content_light_level_info_len = ui32CCSize;
        p_frame->sei_hdr_content_light_level_info_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_logan_content_light_level_info_t* pLightLevel =
        (ni_logan_content_light_level_info_t*)((uint8_t*)pEntry + ui32CCOffset);
        ni_log(NI_LOG_TRACE, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
               pLightLevel->max_content_light_level, pLightLevel->max_pic_average_light_level);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USERDATA_FLAG_UNREGISTERED_PRE,
                   &ui32CCOffset, &ui32CCSize) ||
          find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USERDATA_FLAG_UNREGISTERED_SUF,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_user_data_unreg_len = ui32CCSize;
        p_frame->sei_user_data_unreg_offset =
        video_data_size + metadata_size + ui32CCOffset;
        p_frame->sei_total_len += ui32CCSize;
        ni_log(NI_LOG_TRACE, "User Data Unreg size = %u, offset %u\n",
               ui32CCSize, ui32CCOffset);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USERDATA_FLAG_VUI, &ui32CCOffset, &ui32CCSize))
      {
        p_frame->vui_len = ui32CCSize;
        p_frame->vui_offset = video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_TRACE, "VUI size=%u\n", ui32CCSize);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_LOGAN_H265_USERDATA_FLAG_ALTERNATIVE_TRANSFER_CHARACTERISTICS,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_alt_transfer_characteristics_len = ui32CCSize;
        p_frame->sei_alt_transfer_characteristics_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_TRACE, "alternative transfer characteristics=%u %u "
               "bytes\n", *((uint8_t*)pEntry + ui32CCOffset), ui32CCSize);
      }

      if (0 == p_frame->sei_total_len)
      {
        ni_log(NI_LOG_DEBUG, "retrieved 0 supported SEI !");
      }
    }
  }

  p_frame->dts = 0;
  p_frame->pts = 0;
  //p_frame->end_of_stream = isEndOfStream;
  p_frame->start_of_stream = 0;

  if (rx_size == 0)
  {
    p_frame->data_len[0] = 0;
    p_frame->data_len[1] = 0;
    p_frame->data_len[2] = 0;
    p_frame->data_len[3] = 0;
  }

  ni_log(NI_LOG_TRACE, "received [0x%08x] data size: %d, end of stream=%d\n",
         read_length, rx_size, p_frame->end_of_stream);

  return rx_size;
}

/*!******************************************************************************
 *  \brief  Get info from received xcoder capability
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_populate_device_capability_struct(ni_logan_device_capability_t* p_cap, void* p_data)
{
  int i, total_modules;
  ni_logan_nvme_identity_t* p_id_data = (ni_logan_nvme_identity_t*)p_data;

  if ( (!p_cap) || (!p_data) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n", __FUNCTION__);
    LRETURN;
  }

  if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
    (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
  {
    LRETURN;
  }

  memset(p_cap->fw_rev, 0, sizeof(p_cap->fw_rev));
  memcpy(p_cap->fw_rev, p_id_data->ai8Fr, sizeof(p_cap->fw_rev));
  ni_log(NI_LOG_TRACE, "F/W rev: %2.*s\n", (int)sizeof(p_cap->fw_rev),
                 p_cap->fw_rev);

  p_cap->device_is_xcoder = p_id_data->device_is_xcoder;
  ni_log(NI_LOG_TRACE, "device_is_xcoder: %d\n", p_cap->device_is_xcoder);
  if (0 == p_cap->device_is_xcoder)
  {
    LRETURN;
  }

  p_cap->hw_elements_cnt = p_id_data->xcoder_num_hw;
  if (3 == p_cap->hw_elements_cnt)
  {
    ni_log(NI_LOG_ERROR, "hw_elements_cnt is 3, Rev A NOT supported !\n");
    LRETURN;
  }

  p_cap->h264_decoders_cnt = p_id_data->xcoder_num_h264_decoder_hw;
  p_cap->h264_encoders_cnt = p_id_data->xcoder_num_h264_encoder_hw;
  p_cap->h265_decoders_cnt = p_id_data->xcoder_num_h265_decoder_hw;
  p_cap->h265_encoders_cnt = p_id_data->xcoder_num_h265_encoder_hw;
  ni_log(NI_LOG_TRACE, "hw_elements_cnt: %d\n", p_cap->hw_elements_cnt);
  ni_log(NI_LOG_TRACE, "h264_decoders_cnt: %d\n", p_cap->h264_decoders_cnt);
  ni_log(NI_LOG_TRACE, "h264_encoders_cnt: %d\n", p_cap->h264_encoders_cnt);
  ni_log(NI_LOG_TRACE, "h265_decoders_cnt: %d\n", p_cap->h265_decoders_cnt);
  ni_log(NI_LOG_TRACE, "h265_encoders_cnt: %d\n", p_cap->h265_encoders_cnt);

  total_modules = p_cap->h264_decoders_cnt + p_cap->h264_encoders_cnt +
    p_cap->h265_decoders_cnt + p_cap->h265_encoders_cnt;

  if (total_modules >= 1)
  {
    p_cap->xcoder_devices[0].hw_id = p_id_data->hw0_id;
    p_cap->xcoder_devices[0].max_number_of_contexts =
    NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[0].max_1080p_fps = NI_LOGAN_MAX_1080P_FPS;
    p_cap->xcoder_devices[0].codec_format = p_id_data->hw0_codec_format;
    p_cap->xcoder_devices[0].codec_type = p_id_data->hw0_codec_type;
    p_cap->xcoder_devices[0].max_video_width = NI_LOGAN_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[0].max_video_height = NI_LOGAN_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[0].min_video_width = NI_LOGAN_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[0].min_video_height = NI_LOGAN_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[0].video_profile = p_id_data->hw0_video_profile;
    p_cap->xcoder_devices[0].video_level = p_id_data->hw0_video_level;
  }
  if (total_modules >= 2)
  {
    p_cap->xcoder_devices[1].hw_id = p_id_data->hw1_id;
    p_cap->xcoder_devices[1].max_number_of_contexts =
    NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[1].max_1080p_fps = NI_LOGAN_MAX_1080P_FPS;
    p_cap->xcoder_devices[1].codec_format = p_id_data->hw1_codec_format;
    p_cap->xcoder_devices[1].codec_type = p_id_data->hw1_codec_type;
    p_cap->xcoder_devices[1].max_video_width = NI_LOGAN_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[1].max_video_height = NI_LOGAN_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[1].min_video_width = NI_LOGAN_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[1].min_video_height = NI_LOGAN_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[1].video_profile = p_id_data->hw1_video_profile;
    p_cap->xcoder_devices[1].video_level = p_id_data->hw1_video_level;
  }
  if (total_modules >= 3)
  {
    p_cap->xcoder_devices[2].hw_id = p_id_data->hw2_id;
    p_cap->xcoder_devices[2].max_number_of_contexts =
    NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[2].max_1080p_fps = NI_LOGAN_MAX_1080P_FPS;
    p_cap->xcoder_devices[2].codec_format = p_id_data->hw2_codec_format;
    p_cap->xcoder_devices[2].codec_type = p_id_data->hw2_codec_type;
    p_cap->xcoder_devices[2].max_video_width = NI_LOGAN_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[2].max_video_height = NI_LOGAN_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[2].min_video_width = NI_LOGAN_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[2].min_video_height = NI_LOGAN_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[2].video_profile = p_id_data->hw2_video_profile;
    p_cap->xcoder_devices[2].video_level = p_id_data->hw2_video_level;
  }
  if (total_modules >= 4)
  {
    p_cap->xcoder_devices[3].hw_id = p_id_data->hw3_id;
    p_cap->xcoder_devices[3].max_number_of_contexts =
    NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[3].max_1080p_fps = NI_LOGAN_MAX_1080P_FPS;
    p_cap->xcoder_devices[3].codec_format = p_id_data->hw3_codec_format;
    p_cap->xcoder_devices[3].codec_type = p_id_data->hw3_codec_type;
    p_cap->xcoder_devices[3].max_video_width = NI_LOGAN_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[3].max_video_height = NI_LOGAN_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[3].min_video_width = NI_LOGAN_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[3].min_video_height = NI_LOGAN_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[3].video_profile = p_id_data->hw3_video_profile;
    p_cap->xcoder_devices[3].video_level = p_id_data->hw3_video_level;
  }

  for (i = 0; i < NI_LOGAN_MAX_DEVICES_PER_HW_INSTANCE; i++)
  {
    ni_log(NI_LOG_TRACE, "HW%d hw_id: %d\n", i, p_cap->xcoder_devices[i].hw_id);
    ni_log(NI_LOG_TRACE, "HW%d max_number_of_contexts: %d\n", i, p_cap->xcoder_devices[i].max_number_of_contexts);
    ni_log(NI_LOG_TRACE, "HW%d max_1080p_fps: %d\n", i, p_cap->xcoder_devices[i].max_1080p_fps);
    ni_log(NI_LOG_TRACE, "HW%d codec_format: %d\n", i, p_cap->xcoder_devices[i].codec_format);
    ni_log(NI_LOG_TRACE, "HW%d codec_type: %d\n", i, p_cap->xcoder_devices[i].codec_type);
    ni_log(NI_LOG_TRACE, "HW%d max_video_width: %d\n", i, p_cap->xcoder_devices[i].max_video_width);
    ni_log(NI_LOG_TRACE, "HW%d max_video_height: %d\n", i, p_cap->xcoder_devices[i].max_video_height);
    ni_log(NI_LOG_TRACE, "HW%d min_video_width: %d\n", i, p_cap->xcoder_devices[i].min_video_width);
    ni_log(NI_LOG_TRACE, "HW%d min_video_height: %d\n", i, p_cap->xcoder_devices[i].min_video_height);
    ni_log(NI_LOG_TRACE, "HW%d video_profile: %d\n", i, p_cap->xcoder_devices[i].video_profile);
    ni_log(NI_LOG_TRACE, "HW%d video_level: %d\n", i, p_cap->xcoder_devices[i].video_level);
  }

  memset(p_cap->fw_commit_hash, 0, sizeof(p_cap->fw_commit_hash));
  memcpy(p_cap->fw_commit_hash, p_id_data->fw_commit_hash, sizeof(p_cap->fw_commit_hash) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_commit_hash);
  memset(p_cap->fw_commit_time, 0, sizeof(p_cap->fw_commit_time));
  memcpy(p_cap->fw_commit_time, p_id_data->fw_commit_time, sizeof(p_cap->fw_commit_time) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_commit_time);
  memset(p_cap->fw_branch_name, 0, sizeof(p_cap->fw_branch_name));
  memcpy(p_cap->fw_branch_name, p_id_data->fw_branch_name, sizeof(p_cap->fw_branch_name) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_branch_name);

  END:
  return;
}

static uint32_t presetGopSize[] = {
  1, /*! Custom GOP, Not used */
  1, /*! All Intra */
  1, /*! IPP Cyclic GOP size 1 */
  1, /*! IBB Cyclic GOP size 1 */
  2, /*! IBP Cyclic GOP size 2 */
  4, /*! IBBBP */
  4,
  4,
  8 };

static uint32_t presetGopKeyFrameFactor[] = {
  1, /*! Custom GOP, Not used */
  1, /*! All Intra */
  1, /*! IPP Cyclic GOP size 1 */
  1, /*! IBB Cyclic GOP size 1 */
  2, /*! IBP Cyclic GOP size 2 */
  4, /*! IBBBP */
  1,
  1,
  1 };

/*!******************************************************************************
 *  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_logan_set_custom_template(ni_logan_session_context_t* p_ctx,
                            ni_logan_encoder_config_t* p_cfg,
                            ni_logan_encoder_params_t* p_src)
{

  ni_logan_t408_config_t* p_t408 = &(p_cfg->niParamT408);
  ni_logan_h265_encoder_params_t* p_enc = &p_src->hevc_enc_params;
  int i = 0;

  if ( (!p_ctx) || (!p_cfg) || (!p_src) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() Null pointer parameters passed\n",
           __FUNCTION__);
    return;
  }

  ni_logan_set_default_template(p_ctx, p_cfg);

  if (p_cfg->i32picWidth != p_src->source_width)
  {
    p_cfg->i32picWidth = p_src->source_width;
  }

  if (p_cfg->i32picHeight != p_src->source_height)
  {
    p_cfg->i32picHeight = p_src->source_height;
  }

  if (p_t408->gop_preset_index != p_enc->gop_preset_index)
  {
    p_t408->gop_preset_index = p_enc->gop_preset_index;
  }

  if (p_t408->use_recommend_enc_params != p_enc->use_recommend_enc_params)
  {
    p_t408->use_recommend_enc_params = p_enc->use_recommend_enc_params;
  }

  // trans_rate, enable_hvs_qp_scale:
  // are not present in Rev B p_config

  if (p_cfg->ui8rcEnable != p_enc->rc.enable_rate_control)
  {
    p_cfg->ui8rcEnable = p_enc->rc.enable_rate_control;
  }

  if (p_src->bitrate != 0)
  {
    p_cfg->i32bitRate = p_src->bitrate;
  }

  if (p_t408->enable_cu_level_rate_control != p_enc->rc.enable_cu_level_rate_control)
  {
    p_t408->enable_cu_level_rate_control = p_enc->rc.enable_cu_level_rate_control;
  }

  if (p_t408->enable_hvs_qp != p_enc->rc.enable_hvs_qp)
  {
    p_t408->enable_hvs_qp = p_enc->rc.enable_hvs_qp;
  }

  if (p_t408->hvs_qp_scale != p_enc->rc.hvs_qp_scale)
  {
    p_t408->hvs_qp_scale = p_enc->rc.hvs_qp_scale;
  }

  if (p_t408->minQpI != p_enc->rc.min_qp)
  {
    p_t408->minQpI = p_enc->rc.min_qp;
  }

  if (p_t408->minQpP != p_enc->rc.min_qp)
  {
    p_t408->minQpP = p_enc->rc.min_qp;
  }

  if (p_t408->minQpB != p_enc->rc.min_qp)
  {
    p_t408->minQpB = p_enc->rc.min_qp;
  }

  if (p_t408->maxQpI != p_enc->rc.max_qp)
  {
    p_t408->maxQpI = p_enc->rc.max_qp;
  }

  if (p_t408->maxQpP != p_enc->rc.max_qp)
  {
    p_t408->maxQpP = p_enc->rc.max_qp;
  }

  if (p_t408->maxQpB != p_enc->rc.max_qp)
  {
    p_t408->maxQpB = p_enc->rc.max_qp;
  }
  // TBD intraMinQp and intraMaxQp are not configurable in Rev A; should it
  // be in Rev B?

  if (p_t408->max_delta_qp != p_enc->rc.max_delta_qp)
  {
    p_t408->max_delta_qp = p_enc->rc.max_delta_qp;
  }

  if (p_cfg->i32vbvBufferSize != p_enc->rc.rc_init_delay)
  {
    p_cfg->i32vbvBufferSize = p_enc->rc.rc_init_delay;
  }

  if (p_t408->intra_period != p_enc->intra_period)
  {
    p_t408->intra_period = p_enc->intra_period;
  }

  if (p_t408->roiEnable != p_enc->roi_enable)
  {
      p_t408->roiEnable = p_enc->roi_enable;
  }

  if (p_t408->useLongTerm != p_enc->long_term_ref_enable)
  {
    p_t408->useLongTerm = p_enc->long_term_ref_enable;
  }

  if (p_t408->losslessEnable != p_enc->lossless_enable)
  {
    p_t408->losslessEnable = p_enc->lossless_enable;
  }

  if (p_t408->conf_win_top != p_enc->conf_win_top)
  {
    p_t408->conf_win_top = p_enc->conf_win_top;
  }

  if (p_t408->conf_win_bottom != p_enc->conf_win_bottom)
  {
    p_t408->conf_win_bottom = p_enc->conf_win_bottom;
  }

  if (p_t408->conf_win_left != p_enc->conf_win_left)
  {
    p_t408->conf_win_left = p_enc->conf_win_left;
  }

  if (p_t408->conf_win_right != p_enc->conf_win_right)
  {
    p_t408->conf_win_right = p_enc->conf_win_right;
  }

  if (p_t408->avcIdrPeriod != p_enc->intra_period)
  {
    p_t408->avcIdrPeriod = p_enc->intra_period;
  }

  if ((p_cfg->i32frameRateInfo != p_enc->frame_rate) && (!p_src->enable_vfr))
  {
    p_cfg->i32frameRateInfo = p_enc->frame_rate;
    p_t408->numUnitsInTick = 1000;
    if (p_src->fps_denominator != 0 &&
       (p_src->fps_number % p_src->fps_denominator) != 0)
    {
      p_t408->numUnitsInTick += 1;
      p_cfg->i32frameRateInfo += 1;
    }
    p_t408->timeScale = p_cfg->i32frameRateInfo * 1000;
    if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
    {
      p_t408->timeScale *= 2;
    }
  }
  else if (p_src->enable_vfr)
  {
    if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
    {
      p_t408->timeScale = p_ctx->ui32timing_scale * 2;
    }
    else
    {
      p_t408->timeScale = p_ctx->ui32timing_scale;
    }
    p_t408->numUnitsInTick  = p_ctx->ui32num_unit_in_tick;
    p_cfg->i32frameRateInfo = p_enc->frame_rate;
  }

  if (p_t408->intra_qp != p_enc->rc.intra_qp)
  {
    p_t408->intra_qp = p_enc->rc.intra_qp;
  }

  // "repeatHeaders" value 1 (all Key frames) and value 2 (all I frames)
  // map to forcedHeaderEnable value 2; all other values are ignored
  if (p_t408->forcedHeaderEnable != p_enc->forced_header_enable &&
     (p_enc->forced_header_enable == NI_LOGAN_ENC_REPEAT_HEADERS_ALL_KEY_FRAMES) ||
     (p_enc->forced_header_enable == NI_LOGAN_ENC_REPEAT_HEADERS_ALL_I_FRAMES))
    p_t408->forcedHeaderEnable = 2;

  if (p_t408->decoding_refresh_type != p_enc->decoding_refresh_type)
  {
    p_t408->decoding_refresh_type = p_enc->decoding_refresh_type;
  }

  if (p_t408->independSliceMode != p_enc->slice_mode)
    p_t408->independSliceMode = p_enc->slice_mode;

  if (p_t408->dependSliceMode != p_enc->slice_mode)
    p_t408->dependSliceMode = p_enc->slice_mode;

  if (p_t408->independSliceModeArg != p_enc->slice_arg)
    p_t408->independSliceModeArg = p_enc->slice_arg;

  if (p_t408->dependSliceModeArg != p_enc->slice_arg)
    p_t408->dependSliceModeArg = p_enc->slice_arg;


  // Rev. B: H.264 only parameters.
  if (p_t408->enable_transform_8x8 != p_enc->enable_transform_8x8)
  {
    p_t408->enable_transform_8x8 = p_enc->enable_transform_8x8;
  }

  if (p_t408->avc_slice_mode != p_enc->avc_slice_mode)
  {
    p_t408->avc_slice_mode = p_enc->avc_slice_mode;
  }

  if (p_t408->avc_slice_arg != p_enc->avc_slice_arg)
  {
    p_t408->avc_slice_arg = p_enc->avc_slice_arg;
  }

  if (p_t408->entropy_coding_mode != p_enc->entropy_coding_mode)
  {
    p_t408->entropy_coding_mode = p_enc->entropy_coding_mode;
  }

  // Rev. B: shared between HEVC and H.264
  if (p_t408->intra_mb_refresh_mode != p_enc->intra_mb_refresh_mode)
  {
    p_t408->intraRefreshMode = p_t408->intra_mb_refresh_mode =
    p_enc->intra_mb_refresh_mode;
  }

  if (p_t408->intra_mb_refresh_arg != p_enc->intra_mb_refresh_arg)
  {
    p_t408->intraRefreshArg = p_t408->intra_mb_refresh_arg =
    p_enc->intra_mb_refresh_arg;
  }

  // TBD Rev. B: could be shared for HEVC and H.264
  if (p_t408->enable_mb_level_rc != p_enc->rc.enable_mb_level_rc)
  {
    p_t408->enable_mb_level_rc = p_enc->rc.enable_mb_level_rc;
  }

  // profile setting: if user specified profile
  if (0 != p_enc->profile)
  {
    p_t408->profile = p_enc->profile;
  }

  if (p_t408->level != p_enc->level_idc)
  {
    p_t408->level = p_enc->level_idc;
  }

  // main, extended or baseline profile of 8 bit H.264 requires the following:
  // main:     profile = 2  transform8x8Enable = 0
  // extended: profile = 3  entropyCodingMode = 0, transform8x8Enable = 0
  // baseline: profile = 1  entropyCodingMode = 0, transform8x8Enable = 0 and
  //                        gop with no B frames (gopPresetIdx=1, 2, 6, or 0
  //                        (custom with no B frames)
  if (STD_AVC == p_cfg->ui8bitstreamFormat && 8 == p_ctx->src_bit_depth)
  {
    if (2 == p_t408->profile)
    {
      p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_TRACE, "enable_transform_8x8 set to 0 for profile 2 (main)\n");
    }
    else if (3 == p_t408->profile || 1 == p_t408->profile)
    {
      p_t408->entropy_coding_mode = p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_TRACE, "entropy_coding_mode and enable_transform_8x8 set to 0 "
             "for profile 3 (extended) or 1 (baseline)\n");
    }
  }

  if (GOP_PRESET_IDX_CUSTOM == p_t408->gop_preset_index)
  {
    p_t408->custom_gop_params.custom_gop_size = p_enc->custom_gop_params.custom_gop_size;
    for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
    {
      p_t408->custom_gop_params.pic_param[i].pic_type = p_enc->custom_gop_params.pic_param[i].pic_type;
      p_t408->custom_gop_params.pic_param[i].poc_offset = p_enc->custom_gop_params.pic_param[i].poc_offset;
      p_t408->custom_gop_params.pic_param[i].pic_qp = p_enc->custom_gop_params.pic_param[i].pic_qp + p_t408->intra_qp;
      p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0 = p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0;
      p_t408->custom_gop_params.pic_param[i].ref_poc_L0 = p_enc->custom_gop_params.pic_param[i].ref_poc_L0;
      p_t408->custom_gop_params.pic_param[i].ref_poc_L1 = p_enc->custom_gop_params.pic_param[i].ref_poc_L1;
      p_t408->custom_gop_params.pic_param[i].temporal_id = p_enc->custom_gop_params.pic_param[i].temporal_id;
    }
  }

  p_ctx->key_frame_type = p_t408->decoding_refresh_type; //Store to use when force key p_frame

  // forceFrameType=1 requires intraPeriod=0 and avcIdrPeriod=0 and gopPresetIdx=8
  if (1 == p_src->force_frame_type)
  {
    p_t408->intra_period = 0;
    p_t408->avcIdrPeriod = 0;
    p_t408->gop_preset_index = 8;
    p_ctx->force_frame_type = 1;
  }

  if (p_cfg->hdrEnableVUI != p_src->hdrEnableVUI)
  {
    p_cfg->hdrEnableVUI = p_src->hdrEnableVUI;
  }

  // check auto_dl_handle and hwframes to determine if enable hw yuvbypass here.
  // if the encoder device is different from decoder/uploader, disable yuvbypass.
  // if default config value is different from setting value, update it.
  if (p_cfg->ui8hwframes != p_src->hwframes)
  {
    if (p_src->hwframes && p_ctx->auto_dl_handle == NI_INVALID_DEVICE_HANDLE)
    {
      p_cfg->ui8hwframes = p_src->hwframes;
    }
    else
    {
      p_cfg->ui8hwframes = 0;
    }
  }

  if (p_cfg->ui8explicitRefListEnable != p_src->enable_explicit_rpl)
  {
    p_cfg->ui8explicitRefListEnable = p_src->enable_explicit_rpl;
  }

  if (p_cfg->ui8EnableAUD != p_src->enable_aud)
  {
    p_cfg->ui8EnableAUD = p_src->enable_aud;
  }

  if (p_cfg->ui32minIntraRefreshCycle != p_src->ui32minIntraRefreshCycle)
  {
    p_cfg->ui32minIntraRefreshCycle = p_src->ui32minIntraRefreshCycle;
  }

  // set VUI info
  p_cfg->ui32VuiDataSizeBits = p_src->ui32VuiDataSizeBits;
  p_cfg->ui32VuiDataSizeBytes = p_src->ui32VuiDataSizeBytes;
  memcpy(p_cfg->ui8VuiRbsp, p_src->ui8VuiRbsp, NI_LOGAN_MAX_VUI_SIZE);
  if (p_src->pos_num_units_in_tick > p_src->ui32VuiDataSizeBits ||
      p_src->pos_time_scale > p_src->ui32VuiDataSizeBits)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() VUI filling error\n", __FUNCTION__);
    return;
  }
  else
  {
    ni_logan_overwrite_specified_pos(p_cfg->ui8VuiRbsp, p_src->pos_num_units_in_tick, p_t408->numUnitsInTick);
    ni_logan_overwrite_specified_pos(p_cfg->ui8VuiRbsp, p_src->pos_time_scale, p_t408->timeScale);
  }

  // CRF mode forces the following setting:
  if (p_src->crf)
  {
    p_cfg->ui8rcEnable = 0;
    p_t408->intra_qp = p_src->crf;
    p_t408->enable_hvs_qp = 1;
    p_t408->hvs_qp_scale = 2;
    p_t408->max_delta_qp = 51;
    ni_log(NI_LOG_TRACE, "crf=%d forces the setting of: rcEnable=0, intraQP=%d,"
           " hvsQPEnable=1, hvsQPScale=2, maxDeltaQP=51.\n",
           p_src->crf, p_t408->intra_qp);
  }

  // CBR mode
  if ((p_cfg->ui32fillerEnable != p_src->cbr) && (p_cfg->ui8rcEnable == 1))
  {
    p_cfg->ui32fillerEnable = p_src->cbr;
  }

  // GOP flush
  if (p_cfg->ui32flushGop != p_src->ui32flushGop)
  {
    p_cfg->ui32flushGop = p_src->ui32flushGop;
  }

  ni_log(NI_LOG_DEBUG, "lowDelay=%d\n", p_src->low_delay_mode);
  ni_log(NI_LOG_DEBUG, "strictTimeout=%d\n", p_src->strict_timeout_mode);
  ni_log(NI_LOG_DEBUG, "crf=%u\n", p_src->crf);
  ni_log(NI_LOG_DEBUG, "cbr=%u\n", p_src->cbr);
  ni_log(NI_LOG_DEBUG, "ui32flushGop=%u\n", p_src->ui32flushGop);
  ni_log(NI_LOG_DEBUG, "ui8bitstreamFormat=%d\n", p_cfg->ui8bitstreamFormat);
  ni_log(NI_LOG_DEBUG, "i32picWidth=%d\n", p_cfg->i32picWidth);
  ni_log(NI_LOG_DEBUG, "i32picHeight=%d\n", p_cfg->i32picHeight);
  ni_log(NI_LOG_DEBUG, "i32meBlkMode=%d\n", p_cfg->i32meBlkMode);
  ni_log(NI_LOG_DEBUG, "ui8sliceMode=%d\n", p_cfg->ui8sliceMode);
  ni_log(NI_LOG_DEBUG, "i32frameRateInfo=%d\n", p_cfg->i32frameRateInfo);
  ni_log(NI_LOG_DEBUG, "i32vbvBufferSize=%d\n", p_cfg->i32vbvBufferSize);
  ni_log(NI_LOG_DEBUG, "i32userQpMax=%d\n", p_cfg->i32userQpMax);

  ni_log(NI_LOG_DEBUG, "i32maxIntraSize=%d\n", p_cfg->i32maxIntraSize);
  ni_log(NI_LOG_DEBUG, "i32userMaxDeltaQp=%d\n", p_cfg->i32userMaxDeltaQp);
  ni_log(NI_LOG_DEBUG, "i32userMinDeltaQp=%d\n", p_cfg->i32userMinDeltaQp);
  ni_log(NI_LOG_DEBUG, "i32userQpMin=%d\n", p_cfg->i32userQpMin);
  ni_log(NI_LOG_DEBUG, "i32bitRate=%d\n", p_cfg->i32bitRate);
  ni_log(NI_LOG_DEBUG, "i32bitRateBL=%d\n", p_cfg->i32bitRateBL);
  ni_log(NI_LOG_DEBUG, "ui8rcEnable=%d\n", p_cfg->ui8rcEnable);
  ni_log(NI_LOG_DEBUG, "i32srcBitDepth=%d\n", p_cfg->i32srcBitDepth);
  ni_log(NI_LOG_DEBUG, "ui8enablePTS=%d\n", p_cfg->ui8enablePTS);
  ni_log(NI_LOG_DEBUG, "ui8lowLatencyMode=%d\n", p_cfg->ui8lowLatencyMode);
  ni_log(NI_LOG_DEBUG, "ui32sourceEndian=%d\n", p_cfg->ui32sourceEndian);
  ni_log(NI_LOG_DEBUG, "hdrEnableVUI=%u\n", p_cfg->hdrEnableVUI);
  ni_log(NI_LOG_DEBUG, "ui32minIntraRefreshCycle=%u\n",
         p_cfg->ui32minIntraRefreshCycle);
  ni_log(NI_LOG_DEBUG, "ui32fillerEnable=%u\n", p_cfg->ui32fillerEnable);
  ni_log(NI_LOG_DEBUG, "ui8hwframes=%u\n", p_cfg->ui8hwframes);
  ni_log(NI_LOG_DEBUG, "ui8explicitRefListEnable=%u\n", p_cfg->ui8explicitRefListEnable);

  ni_log(NI_LOG_DEBUG, "** ni_logan_t408_config_t: \n");
  ni_log(NI_LOG_DEBUG, "profile=%d\n", p_t408->profile);
  ni_log(NI_LOG_DEBUG, "level=%d\n", p_t408->level);
  ni_log(NI_LOG_DEBUG, "tier=%d\n", p_t408->tier);

  ni_log(NI_LOG_DEBUG, "internalBitDepth=%d\n", p_t408->internalBitDepth);
  ni_log(NI_LOG_DEBUG, "losslessEnable=%d\n", p_t408->losslessEnable);
  ni_log(NI_LOG_DEBUG, "constIntraPredFlag=%d\n", p_t408->constIntraPredFlag);

  ni_log(NI_LOG_DEBUG, "decoding_refresh_type=%d\n", p_t408->decoding_refresh_type);
  ni_log(NI_LOG_DEBUG, "intra_qp=%d\n", p_t408->intra_qp);
  ni_log(NI_LOG_DEBUG, "intra_period=%d\n", p_t408->intra_period);
  ni_log(NI_LOG_DEBUG, "roi_enable=%d\n", p_t408->roiEnable);
  ni_log(NI_LOG_DEBUG, "useLongTerm=%u\n", p_t408->useLongTerm);

  ni_log(NI_LOG_DEBUG, "conf_win_top=%d\n", p_t408->conf_win_top);
  ni_log(NI_LOG_DEBUG, "conf_win_bottom=%d\n", p_t408->conf_win_bottom);
  ni_log(NI_LOG_DEBUG, "conf_win_left=%d\n", p_t408->conf_win_left);
  ni_log(NI_LOG_DEBUG, "conf_win_right=%d\n", p_t408->conf_win_right);

  ni_log(NI_LOG_DEBUG, "independSliceMode=%d\n", p_t408->independSliceMode);
  ni_log(NI_LOG_DEBUG, "independSliceModeArg=%d\n", p_t408->independSliceModeArg);

  ni_log(NI_LOG_DEBUG, "dependSliceMode=%d\n", p_t408->dependSliceMode);
  ni_log(NI_LOG_DEBUG, "dependSliceModeArg=%d\n", p_t408->dependSliceModeArg);

  ni_log(NI_LOG_DEBUG, "intraRefreshMode=%d\n", p_t408->intraRefreshMode);

  ni_log(NI_LOG_DEBUG, "intraRefreshArg=%d\n", p_t408->intraRefreshArg);

  ni_log(NI_LOG_DEBUG, "use_recommend_enc_params=%d\n", p_t408->use_recommend_enc_params);
  ni_log(NI_LOG_DEBUG, "scalingListEnable=%d\n", p_t408->scalingListEnable);

  ni_log(NI_LOG_DEBUG, "cu_size_mode=%d\n", p_t408->cu_size_mode);
  ni_log(NI_LOG_DEBUG, "tmvpEnable=%d\n", p_t408->tmvpEnable);
  ni_log(NI_LOG_DEBUG, "wppEnable=%d\n", p_t408->wppEnable);
  ni_log(NI_LOG_DEBUG, "max_num_merge=%d\n", p_t408->max_num_merge);
  ni_log(NI_LOG_DEBUG, "disableDeblk=%d\n", p_t408->disableDeblk);
  ni_log(NI_LOG_DEBUG, "lfCrossSliceBoundaryEnable=%d\n", p_t408->lfCrossSliceBoundaryEnable);
  ni_log(NI_LOG_DEBUG, "betaOffsetDiv2=%d\n", p_t408->betaOffsetDiv2);
  ni_log(NI_LOG_DEBUG, "tcOffsetDiv2=%d\n", p_t408->tcOffsetDiv2);
  ni_log(NI_LOG_DEBUG, "skipIntraTrans=%d\n", p_t408->skipIntraTrans);
  ni_log(NI_LOG_DEBUG, "saoEnable=%d\n", p_t408->saoEnable);
  ni_log(NI_LOG_DEBUG, "intraNxNEnable=%d\n", p_t408->intraNxNEnable);
  ni_log(NI_LOG_DEBUG, "bitAllocMode=%d\n", p_t408->bitAllocMode);

  ni_log(NI_LOG_DEBUG, "enable_cu_level_rate_control=%d\n", p_t408->enable_cu_level_rate_control);

  ni_log(NI_LOG_DEBUG, "enable_hvs_qp=%d\n", p_t408->enable_hvs_qp);

  ni_log(NI_LOG_DEBUG, "hvs_qp_scale=%d\n", p_t408->hvs_qp_scale);

  ni_log(NI_LOG_DEBUG, "max_delta_qp=%d\n", p_t408->max_delta_qp);

  // CUSTOM_GOP
  ni_log(NI_LOG_DEBUG, "gop_preset_index=%d\n", p_t408->gop_preset_index);
  if (p_t408->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
  {
    ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
    for (i = 0; i < 8; i++)
    //for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
    {
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].pic_type);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].poc_offset);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_qp=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].pic_qp);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].ref_poc_L0);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].ref_poc_L1);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n",
             i, p_t408->custom_gop_params.pic_param[i].temporal_id);
    }
  }

  ni_log(NI_LOG_DEBUG, "roiEnable=%d\n", p_t408->roiEnable);

  ni_log(NI_LOG_DEBUG, "numUnitsInTick=%d\n", p_t408->numUnitsInTick);
  ni_log(NI_LOG_DEBUG, "timeScale=%d\n", p_t408->timeScale);
  ni_log(NI_LOG_DEBUG, "numTicksPocDiffOne=%d\n", p_t408->numTicksPocDiffOne);

  ni_log(NI_LOG_DEBUG, "chromaCbQpOffset=%d\n", p_t408->chromaCbQpOffset);
  ni_log(NI_LOG_DEBUG, "chromaCrQpOffset=%d\n", p_t408->chromaCrQpOffset);

  ni_log(NI_LOG_DEBUG, "initialRcQp=%d\n", p_t408->initialRcQp);

  ni_log(NI_LOG_DEBUG, "nrYEnable=%d\n", p_t408->nrYEnable);
  ni_log(NI_LOG_DEBUG, "nrCbEnable=%d\n", p_t408->nrCbEnable);
  ni_log(NI_LOG_DEBUG, "nrCrEnable=%d\n", p_t408->nrCrEnable);

  // ENC_NR_WEIGHT
  ni_log(NI_LOG_DEBUG, "nrIntraWeightY=%d\n", p_t408->nrIntraWeightY);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCb=%d\n", p_t408->nrIntraWeightCb);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCr=%d\n", p_t408->nrIntraWeightCr);
  ni_log(NI_LOG_DEBUG, "nrInterWeightY=%d\n", p_t408->nrInterWeightY);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCb=%d\n", p_t408->nrInterWeightCb);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCr=%d\n", p_t408->nrInterWeightCr);

  ni_log(NI_LOG_DEBUG, "nrNoiseEstEnable=%d\n", p_t408->nrNoiseEstEnable);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaY=%d\n", p_t408->nrNoiseSigmaY);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCb=%d\n", p_t408->nrNoiseSigmaCb);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCr=%d\n", p_t408->nrNoiseSigmaCr);

  ni_log(NI_LOG_DEBUG, "useLongTerm=%d\n", p_t408->useLongTerm);

  // newly added for T408_520
  ni_log(NI_LOG_DEBUG, "monochromeEnable=%d\n", p_t408->monochromeEnable);
  ni_log(NI_LOG_DEBUG, "strongIntraSmoothEnable=%d\n", p_t408->strongIntraSmoothEnable);

  ni_log(NI_LOG_DEBUG, "weightPredEnable=%d\n", p_t408->weightPredEnable);
  ni_log(NI_LOG_DEBUG, "bgDetectEnable=%d\n", p_t408->bgDetectEnable);
  ni_log(NI_LOG_DEBUG, "bgThrDiff=%d\n", p_t408->bgThrDiff);
  ni_log(NI_LOG_DEBUG, "bgThrMeanDiff=%d\n", p_t408->bgThrMeanDiff);
  ni_log(NI_LOG_DEBUG, "bgLambdaQp=%d\n", p_t408->bgLambdaQp);
  ni_log(NI_LOG_DEBUG, "bgDeltaQp=%d\n", p_t408->bgDeltaQp);

  ni_log(NI_LOG_DEBUG, "customLambdaEnable=%d\n", p_t408->customLambdaEnable);
  ni_log(NI_LOG_DEBUG, "customMDEnable=%d\n", p_t408->customMDEnable);
  ni_log(NI_LOG_DEBUG, "pu04DeltaRate=%d\n", p_t408->pu04DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08DeltaRate=%d\n", p_t408->pu08DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16DeltaRate=%d\n", p_t408->pu16DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32DeltaRate=%d\n", p_t408->pu32DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraPlanarDeltaRate=%d\n", p_t408->pu04IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraDcDeltaRate=%d\n", p_t408->pu04IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraAngleDeltaRate=%d\n", p_t408->pu04IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraPlanarDeltaRate=%d\n", p_t408->pu08IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraDcDeltaRate=%d\n", p_t408->pu08IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraAngleDeltaRate=%d\n", p_t408->pu08IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraPlanarDeltaRate=%d\n", p_t408->pu16IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraDcDeltaRate=%d\n", p_t408->pu16IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraAngleDeltaRate=%d\n", p_t408->pu16IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraPlanarDeltaRate=%d\n", p_t408->pu32IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraDcDeltaRate=%d\n", p_t408->pu32IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraAngleDeltaRate=%d\n", p_t408->pu32IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08IntraDeltaRate=%d\n", p_t408->cu08IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08InterDeltaRate=%d\n", p_t408->cu08InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08MergeDeltaRate=%d\n", p_t408->cu08MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16IntraDeltaRate=%d\n", p_t408->cu16IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16InterDeltaRate=%d\n", p_t408->cu16InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16MergeDeltaRate=%d\n", p_t408->cu16MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32IntraDeltaRate=%d\n", p_t408->cu32IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32InterDeltaRate=%d\n", p_t408->cu32InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32MergeDeltaRate=%d\n", p_t408->cu32MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "coefClearDisable=%d\n", p_t408->coefClearDisable);
  ni_log(NI_LOG_DEBUG, "minQpI=%d\n", p_t408->minQpI);
  ni_log(NI_LOG_DEBUG, "maxQpI=%d\n", p_t408->maxQpI);
  ni_log(NI_LOG_DEBUG, "minQpP=%d\n", p_t408->minQpP);
  ni_log(NI_LOG_DEBUG, "maxQpP=%d\n", p_t408->maxQpP);
  ni_log(NI_LOG_DEBUG, "minQpB=%d\n", p_t408->minQpB);
  ni_log(NI_LOG_DEBUG, "maxQpB=%d\n", p_t408->maxQpB);

  // for H.264 on T408
  ni_log(NI_LOG_DEBUG, "avcIdrPeriod=%d\n", p_t408->avcIdrPeriod);
  ni_log(NI_LOG_DEBUG, "rdoSkip=%d\n", p_t408->rdoSkip);
  ni_log(NI_LOG_DEBUG, "lambdaScalingEnable=%d\n", p_t408->lambdaScalingEnable);
  ni_log(NI_LOG_DEBUG, "enable_transform_8x8=%d\n", p_t408->enable_transform_8x8);
  ni_log(NI_LOG_DEBUG, "avc_slice_mode=%d\n", p_t408->avc_slice_mode);
  ni_log(NI_LOG_DEBUG, "avc_slice_arg=%d\n", p_t408->avc_slice_arg);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_mode=%d\n", p_t408->intra_mb_refresh_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_arg=%d\n", p_t408->intra_mb_refresh_arg);
  ni_log(NI_LOG_DEBUG, "enable_mb_level_rc=%d\n", p_t408->enable_mb_level_rc);
  ni_log(NI_LOG_DEBUG, "entropy_coding_mode=%d\n", p_t408->entropy_coding_mode);
  ni_log(NI_LOG_DEBUG, "forcedHeaderEnable=%d\n", p_t408->forcedHeaderEnable);
}

/*!******************************************************************************
 *  \brief  Setup and initialize all xcoder configuration to default (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_logan_set_default_template(ni_logan_session_context_t* p_ctx, ni_logan_encoder_config_t* p_config)
{
  uint8_t i = 0;

  if (!p_ctx || !p_config)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() Null pointer parameters passed\n", __FUNCTION__);
    return;
  }

  memset(p_config, 0, sizeof(ni_logan_encoder_config_t));

  // fill in common attributes values
  p_config->i32picWidth = 720;
  p_config->i32picHeight = 480;
  p_config->i32meBlkMode = 0; // (AVC ONLY) syed: 0 means use all possible block partitions
  p_config->ui8sliceMode = 0; // syed: 0 means 1 slice per picture
  p_config->i32frameRateInfo = 30;
  p_config->i32vbvBufferSize = 3000; //0; // syed: parameter is ignored if rate control is off,
                                     //if rate control is on, 0 means do not check vbv constraints
  p_config->i32userQpMax = 51;       // syed todo: this should also be h264-only parameter

  // AVC only
  if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->i32maxIntraSize = 8000000; // syed: how big an intra p_frame can get?
    p_config->i32userMaxDeltaQp = 51;
    p_config->i32userMinDeltaQp = 51;
    p_config->i32userQpMin = 8;
  }

  p_config->i32bitRate = 0;   //1000000; // syed todo: check if this is applicable (could be coda9 only)
  p_config->i32bitRateBL = 0; // syed todo: no documentation on this parameter in documents
  p_config->ui8rcEnable = 0;
  p_config->i32srcBitDepth = p_ctx->src_bit_depth;
  p_config->ui8enablePTS = 0;
  p_config->ui8lowLatencyMode = 0;

  // profiles for H.264: 1 = baseline, 2 = main, 3 = extended, 4 = high
  //                     5 = high10  (default 8 bit: 4, 10 bit: 5)
  // profiles for HEVC:  1 = main, 2 = main10  (default 8 bit: 1, 10 bit: 2)

  // bitstream type: H.264 or HEVC
  if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->ui8bitstreamFormat = STD_AVC;

    p_config->niParamT408.profile = 4;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 5;
    }
  }
  else
  {
    ni_logan_assert(NI_LOGAN_CODEC_FORMAT_H265 == p_ctx->codec_format);

    p_config->ui8bitstreamFormat = STD_HEVC;

    p_config->niParamT408.profile = 1;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 2;
    }
  }

  p_config->ui32fillerEnable = 0;
  p_config->hdrEnableVUI = 0;
  p_config->ui8EnableAUD = 0;
  p_config->ui32flushGop = 0;
  p_config->ui32minIntraRefreshCycle = 0;
  p_config->ui32sourceEndian = p_ctx->src_endian;
  p_config->ui8explicitRefListEnable = 0;

  p_config->niParamT408.level = 0;   // TBD
  p_config->niParamT408.tier = 0;    // syed 0 means main tier

  p_config->niParamT408.internalBitDepth = p_ctx->src_bit_depth;
  p_config->niParamT408.losslessEnable = 0;
  p_config->niParamT408.constIntraPredFlag = 0;

  p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_IBBBP;

  p_config->niParamT408.decoding_refresh_type = 2;
  p_config->niParamT408.intra_qp = NI_LOGAN_DEFAULT_INTRA_QP;
  // avcIdrPeriod (H.264 on T408), NOT shared with intra_period
  p_config->niParamT408.intra_period = 92;
  p_config->niParamT408.avcIdrPeriod = 92;

  p_config->niParamT408.conf_win_top = 0;
  p_config->niParamT408.conf_win_bottom = 0;
  p_config->niParamT408.conf_win_left = 0;
  p_config->niParamT408.conf_win_right = 0;

  p_config->niParamT408.independSliceMode = 0;
  p_config->niParamT408.independSliceModeArg = 0;
  p_config->niParamT408.dependSliceMode = 0;
  p_config->niParamT408.dependSliceModeArg = 0;
  p_config->niParamT408.intraRefreshMode = 0;
  p_config->niParamT408.intraRefreshArg = 0;

  p_config->niParamT408.use_recommend_enc_params = 0; //1;
  p_config->niParamT408.scalingListEnable = 0;

  //It is hardcode the default value is 7 which enable 8x8, 16x16, 32x32 coding unit size
  p_config->niParamT408.cu_size_mode = NI_LOGAN_DEFAULT_CU_SIZE_MODE;

  p_config->niParamT408.tmvpEnable = 1;
  p_config->niParamT408.wppEnable = 0;
  p_config->niParamT408.max_num_merge = 2;  // It is hardcode the max merge candidates default 2
  p_config->niParamT408.disableDeblk = 0;
  p_config->niParamT408.lfCrossSliceBoundaryEnable = 1;
  p_config->niParamT408.betaOffsetDiv2 = 0;
  p_config->niParamT408.tcOffsetDiv2 = 0;
  p_config->niParamT408.skipIntraTrans = 1; // syed todo: do more investigation
  p_config->niParamT408.saoEnable = 1;
  p_config->niParamT408.intraNxNEnable = 1;

  p_config->niParamT408.bitAllocMode = 0;

  for (i = 0; i < NI_LOGAN_MAX_GOP_NUM; i++)
  {
    p_config->niParamT408.fixedBitRatio[i] = 1;
  }

  p_config->niParamT408.enable_cu_level_rate_control = 1; //0;

  p_config->niParamT408.enable_hvs_qp = 0;
  p_config->niParamT408.hvs_qp_scale = 2; // syed todo: do more investigation

  p_config->niParamT408.max_delta_qp = NI_LOGAN_DEFAULT_MAX_DELTA_QP;

  // CUSTOM_GOP
  p_config->niParamT408.custom_gop_params.custom_gop_size = 0;
  for (i = 0; i < p_config->niParamT408.custom_gop_params.custom_gop_size; i++)
  {
    p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = LOGAN_PIC_TYPE_I;
    p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].pic_qp = 0;
    // ToDo: value of added num_ref_pic_L0 ???
    p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L0 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L1 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
  }

  p_config->niParamT408.roiEnable = 0;

  p_config->niParamT408.numUnitsInTick = 1000;
  p_config->niParamT408.timeScale = p_config->i32frameRateInfo * 1000;
  if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->niParamT408.timeScale *= 2;
  }

  p_config->niParamT408.numTicksPocDiffOne = 0; // syed todo: verify, set to zero to try to match the model's output encoding

  p_config->niParamT408.chromaCbQpOffset = 0;
  p_config->niParamT408.chromaCrQpOffset = 0;

  p_config->niParamT408.initialRcQp = 63; //-1;

  p_config->niParamT408.nrYEnable = 0;
  p_config->niParamT408.nrCbEnable = 0;
  p_config->niParamT408.nrCrEnable = 0;

  // ENC_NR_WEIGHT
  p_config->niParamT408.nrIntraWeightY = 7;
  p_config->niParamT408.nrIntraWeightCb = 7;
  p_config->niParamT408.nrIntraWeightCr = 7;
  p_config->niParamT408.nrInterWeightY = 4;
  p_config->niParamT408.nrInterWeightCb = 4;
  p_config->niParamT408.nrInterWeightCr = 4;

  p_config->niParamT408.nrNoiseEstEnable = 0;
  p_config->niParamT408.nrNoiseSigmaY = 0;
  p_config->niParamT408.nrNoiseSigmaCb = 0;
  p_config->niParamT408.nrNoiseSigmaCr = 0;

  p_config->niParamT408.useLongTerm = 0; // syed: keep disabled for now, need to experiment later

  // newly added for T408_520
  p_config->niParamT408.monochromeEnable = 0; // syed: do we expect monochrome input?
  p_config->niParamT408.strongIntraSmoothEnable = 1;

  p_config->niParamT408.weightPredEnable = 0; //1; // syed: enabling for better quality
                                              //            but need to keep an eye on performance penalty
  p_config->niParamT408.bgDetectEnable = 0;
  p_config->niParamT408.bgThrDiff = 8;     // syed: matching the C-model
  p_config->niParamT408.bgThrMeanDiff = 1; // syed: matching the C-model
  p_config->niParamT408.bgLambdaQp = 32;   // syed: matching the C-model
  p_config->niParamT408.bgDeltaQp = 3;     // syed: matching the C-model

  p_config->niParamT408.customLambdaEnable = 0;
  p_config->niParamT408.customMDEnable = 0;
  p_config->niParamT408.pu04DeltaRate = 0;
  p_config->niParamT408.pu08DeltaRate = 0;
  p_config->niParamT408.pu16DeltaRate = 0;
  p_config->niParamT408.pu32DeltaRate = 0;
  p_config->niParamT408.pu04IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu04IntraDcDeltaRate = 0;
  p_config->niParamT408.pu04IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu08IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu08IntraDcDeltaRate = 0;
  p_config->niParamT408.pu08IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu16IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu16IntraDcDeltaRate = 0;
  p_config->niParamT408.pu16IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu32IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu32IntraDcDeltaRate = 0;
  p_config->niParamT408.pu32IntraAngleDeltaRate = 0;
  p_config->niParamT408.cu08IntraDeltaRate = 0;
  p_config->niParamT408.cu08InterDeltaRate = 0;
  p_config->niParamT408.cu08MergeDeltaRate = 0;
  p_config->niParamT408.cu16IntraDeltaRate = 0;
  p_config->niParamT408.cu16InterDeltaRate = 0;
  p_config->niParamT408.cu16MergeDeltaRate = 0;
  p_config->niParamT408.cu32IntraDeltaRate = 0;
  p_config->niParamT408.cu32InterDeltaRate = 0;
  p_config->niParamT408.cu32MergeDeltaRate = 0;
  p_config->niParamT408.coefClearDisable = 0;
  p_config->niParamT408.minQpI = 8;
  p_config->niParamT408.maxQpI = 51;
  p_config->niParamT408.minQpP = 8;
  p_config->niParamT408.maxQpP = 51;
  p_config->niParamT408.minQpB = 8;
  p_config->niParamT408.maxQpB = 51;

  // for H.264 on T408
  p_config->niParamT408.avcIdrPeriod = 92; // syed todo: check that 0 means encoder decides
  p_config->niParamT408.rdoSkip = 0;
  p_config->niParamT408.lambdaScalingEnable = 0;
  p_config->niParamT408.enable_transform_8x8 = 1;
  p_config->niParamT408.avc_slice_mode = 0;
  p_config->niParamT408.avc_slice_arg = 0;
  p_config->niParamT408.intra_mb_refresh_mode = 0;
  p_config->niParamT408.intra_mb_refresh_arg = 0;
  p_config->niParamT408.enable_mb_level_rc = 1;
  p_config->niParamT408.entropy_coding_mode = 1; // syed: 1 means CABAC, make sure profile is main or above,
                                                 //       can't have CABAC in baseline
  p_config->niParamT408.forcedHeaderEnable = 0; // first IDR frame
}

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
                                         uint32_t max_err_len)
{
  ni_logan_retcode_t param_ret = NI_LOGAN_RETCODE_SUCCESS;
  int i;

  if ( (!p_ctx) || (!p_cfg) || (!p_src) || (!p_param_err) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() Null pointer parameters passed\n", __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (0 == p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid frame_rate of 0 value", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_FRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate <= p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid i32bitRate: smaller than or equal to frame rate", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate > 700000000)
  {
    strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate < 0)
  {
    strncpy(p_param_err, "Invalid i32bitRate of 0 value", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_src->source_width < LOGAN_XCODER_MIN_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too small", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_width > LOGAN_XCODER_MAX_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too big", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_height < LOGAN_XCODER_MIN_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too small", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (p_src->source_height > LOGAN_XCODER_MAX_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too big", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  // number of MB (AVC, default) or CTU (HEVC) per row/column
  int32_t num_mb_or_ctu_row = (p_src->source_height + 16 - 1) / 16;
  int32_t num_mb_or_ctu_col = (p_src->source_width + 16 - 1) / 16;
  if (NI_LOGAN_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    num_mb_or_ctu_row = (p_src->source_height + 64 - 1) / 64;
    num_mb_or_ctu_col = (p_src->source_width + 64 - 1) / 64;
  }

  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    strncpy(p_param_err, "Invalid intraRefreshMode: 4 not supported for AVC",
            max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg <= 0)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should be greater than 0",
            max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  if (1 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg > num_mb_or_ctu_row)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of height when intraRefreshMode=1", max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  if (2 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg > num_mb_or_ctu_col)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of width when intraRefreshMode=2", max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  if ((3 == p_cfg->niParamT408.intra_mb_refresh_mode ||
       4 == p_cfg->niParamT408.intra_mb_refresh_mode) &&
      (p_cfg->niParamT408.intra_mb_refresh_arg >
       num_mb_or_ctu_row * num_mb_or_ctu_col))
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of frame when intraRefreshMode=3/4", max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.losslessEnable)
  {
    strncpy(p_param_err, "Error: lossless coding should be disabled when "
            "intraRefreshMode=4", max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.roiEnable)
  {
    strncpy(p_param_err, "Error: ROI should be disabled when "
            "intraRefreshMode=4", max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    if (10 == p_ctx->src_bit_depth)
    {
      if (p_cfg->niParamT408.profile != 5)
      {
        strncpy(p_param_err, "Invalid profile: must be 5 (high10)",
                max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
    }
    else
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                " 3 (extended), 4 (high), or 5 (high10)", max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }

      if (1 == p_cfg->niParamT408.profile &&
          ! (0 == p_cfg->niParamT408.gop_preset_index ||
             1 == p_cfg->niParamT408.gop_preset_index ||
             2 == p_cfg->niParamT408.gop_preset_index ||
             6 == p_cfg->niParamT408.gop_preset_index ||
	     9 == p_cfg->niParamT408.gop_preset_index))
      {
        strncpy(p_param_err, "Invalid gopPresetIdx for H.264 baseline profile:"
                " must be 1, 2, 6, 9 or 0 (custom with no B frames)", max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }

      if (1 == p_cfg->niParamT408.profile &&
          GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                    "have B frames", max_err_len);
            return NI_LOGAN_RETCODE_INVALID_PARAM;
          }
        }
      }
    }

    if (1 == p_cfg->niParamT408.avc_slice_mode)
    {
      // validate range of avcSliceArg: 1 - number-of-MBs-in-frame
      int32_t numMbs = ((p_cfg->i32picWidth + 16 - 1) >> 4) *
      ((p_cfg->i32picHeight + 16 - 1) >> 4);
      if (p_cfg->niParamT408.avc_slice_arg < 1 ||
          p_cfg->niParamT408.avc_slice_arg > numMbs)
      {
        strncpy(p_param_err, "Invalid avcSliceArg: must be between 1 and number"
                " of 16x16 pixel MBs in a frame", max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
    }
    if (1 == p_cfg->niParamT408.independSliceMode)
    {
      // validate range of sliceArg: 1 - number-of-MBs-in-frame
      int32_t numMbs = ((p_cfg->i32picWidth + 16 - 1) >> 4) *
      ((p_cfg->i32picHeight + 16 - 1) >> 4);
      if (p_cfg->niParamT408.independSliceModeArg < 1 ||
          p_cfg->niParamT408.independSliceModeArg > numMbs)
      {
        strncpy(p_param_err, "Invalid sliceArg: must be between 1 and number"
                " of 16x16 pixel MBs in a frame", max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
    }
  }
  else if (NI_LOGAN_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    if (10 == p_ctx->src_bit_depth)
    {
      if (p_cfg->niParamT408.profile != 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 2 (main10)",
                max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
    }
    else
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                max_err_len);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
    }
  }

  if (1 == p_cfg->niParamT408.independSliceMode)
  {
    // validate range of sliceArg: 1 - number-of-CTUs-in-frame
    int32_t numCtus = ((p_cfg->i32picWidth + 64 - 1) >> 6) *
    ((p_cfg->i32picHeight + 64 - 1) >> 6);
    if (p_cfg->niParamT408.independSliceModeArg < 1 ||
        p_cfg->niParamT408.independSliceModeArg > numCtus)
    {
      strncpy(p_param_err, "Invalid sliceArg: must be between 1 and number"
              " of 64x64 pixel CTUs in a frame", max_err_len);
      return NI_LOGAN_RETCODE_INVALID_PARAM;
    }
  }

  if (p_src->force_frame_type != 0 && p_src->force_frame_type != 1)
  {
    strncpy(p_param_err, "Invalid forceFrameType: out of range",
            max_err_len);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (p_cfg->niParamT408.forcedHeaderEnable < 0 ||
      p_cfg->niParamT408.forcedHeaderEnable > 2)
  {
    strncpy(p_param_err, "Invalid forcedHeaderEnable: out of range",
            max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  if (p_cfg->niParamT408.decoding_refresh_type < 0 ||
    p_cfg->niParamT408.decoding_refresh_type > 2)
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: out of range", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE;
    LRETURN;
  }

  if (p_cfg->niParamT408.gop_preset_index < NI_LOGAN_MIN_GOP_PRESET_IDX ||
      p_cfg->niParamT408.gop_preset_index > NI_LOGAN_MAX_GOP_PRESET_IDX)
  {
    snprintf(p_param_err, max_err_len, "Invalid gop_preset_index: out of range");
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_GOP_PRESET;
    LRETURN;
  }

  if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
  {
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
    {
      strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
      NI_LOGAN_MAX_GOP_NUM)
    {
      strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
  }

  if (p_cfg->niParamT408.use_recommend_enc_params < 0 ||
    p_cfg->niParamT408.use_recommend_enc_params > 3)
  {
    strncpy(p_param_err, "Invalid use_recommend_enc_params: out of range", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM;
    LRETURN;
  }

  switch (p_cfg->niParamT408.use_recommend_enc_params)
  {
    case 0:
    case 2:
    case 3:
    {
      if (p_cfg->niParamT408.use_recommend_enc_params != 3)
      {
        // in FAST mode (recommendEncParam==3), max_num_merge value will be
        // decided in FW
        if (p_cfg->niParamT408.max_num_merge < 0 ||
          p_cfg->niParamT408.max_num_merge > 3)
        {
          strncpy(p_param_err, "Invalid max_num_merge: out of range", max_err_len);
          param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_MAXNUMMERGE;
          LRETURN;
        }
      }
      break;
    }

    default: break;
  }

  if ( p_cfg->niParamT408.intra_qp < NI_LOGAN_MIN_INTRA_QP ||
       p_cfg->niParamT408.intra_qp > NI_LOGAN_MAX_INTRA_QP )
  {
    strncpy(p_param_err, "Invalid intra_qp: out of range", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_QP;
    LRETURN;
  }

  if ( p_cfg->niParamT408.enable_mb_level_rc != 1 &&
       p_cfg->niParamT408.enable_mb_level_rc != 0 )
  {
    strncpy(p_param_err, "Invalid enable_mb_level_rc: out of range", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_RCENABLE;
    LRETURN;
  }

  if (1 == p_cfg->niParamT408.enable_mb_level_rc)
  {
    if ( p_cfg->niParamT408.minQpI < 0 ||
         p_cfg->niParamT408.minQpI > 51 )
    {
      strncpy(p_param_err, "Invalid min_qp: out of range", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_MN_QP;
      LRETURN;
    }

    if ( p_cfg->niParamT408.maxQpI < 0 ||
         p_cfg->niParamT408.maxQpI > 51 )
    {
      strncpy(p_param_err, "Invalid max_qp: out of range", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
    // TBD minQpP minQpB maxQpP maxQpB

    if ( p_cfg->niParamT408.enable_cu_level_rate_control != 1 &&
         p_cfg->niParamT408.enable_cu_level_rate_control != 0 )
    {
      strncpy(p_param_err, "Invalid enable_cu_level_rate_control: out of range", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_CU_LVL_RC_EN;
      LRETURN;
    }

    if (p_cfg->niParamT408.enable_cu_level_rate_control == 1)
    {
      if ( p_cfg->niParamT408.enable_hvs_qp != 1 &&
           p_cfg->niParamT408.enable_hvs_qp != 0 )
      {
        strncpy(p_param_err, "Invalid enable_hvs_qp: out of range", max_err_len);
        param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_EN;
        LRETURN;
      }

      if (p_cfg->niParamT408.enable_hvs_qp)
      {
        if ( p_cfg->niParamT408.max_delta_qp < NI_LOGAN_MIN_MAX_DELTA_QP ||
             p_cfg->niParamT408.max_delta_qp > NI_LOGAN_MAX_MAX_DELTA_QP )
        {
          strncpy(p_param_err, "Invalid max_delta_qp: out of range", max_err_len);
          param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_MX_DELTA_QP;
          LRETURN;
        }
#if 0
        // TBD missing enable_hvs_qp_scale?
        if ( p_cfg->niParamT408.enable_hvs_qp_scale != 1 &&
             p_cfg->niParamT408.enable_hvs_qp_scale != 0 )
        {
          snprintf(p_param_err, max_err_len,
                   "Invalid enable_hvs_qp_scale: out of range");
          return NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_SCL;
        }

        if (p_cfg->niParamT408.enable_hvs_qp_scale == 1)
        {
          if ( p_cfg->niParamT408.hvs_qp_scale < 0 ||
               p_cfg->niParamT408.hvs_qp_scale > 4 )
          {
            snprintf(p_param_err, max_err_len, "Invalid hvs_qp_scale: out of range");
            return NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_SCL;
          }
        }
#endif
      }
    }
    // TBD rc_init_delay/i32vbvBufferSize same thing in Rev. B ?
    if (p_cfg->i32vbvBufferSize < 10 || p_cfg->i32vbvBufferSize > 3000)
    {
      strncpy(p_param_err, "Invalid i32vbvBufferSize: out of range", max_err_len);
      param_ret = NI_LOGAN_RETCODE_PARAM_ERROR_RCINITDELAY;
      LRETURN;
    }
  }

  // check compatibility between GOP size and Intra period
  if (((GOP_PRESET_IDX_IBPBP == p_cfg->niParamT408.gop_preset_index &&
        (p_cfg->niParamT408.intra_period % 2) != 0) ||
       ((GOP_PRESET_IDX_IBBBP == p_cfg->niParamT408.gop_preset_index ||
         GOP_PRESET_IDX_IPPPP == p_cfg->niParamT408.gop_preset_index ||
         GOP_PRESET_IDX_IBBBB == p_cfg->niParamT408.gop_preset_index) &&
        (p_cfg->niParamT408.intra_period % 4) != 0) ||
       (GOP_PRESET_IDX_RA_IB == p_cfg->niParamT408.gop_preset_index &&
        (p_cfg->niParamT408.intra_period % 8) != 0)))
  {
    strncpy(p_param_err, "Error: intra_period and gop_preset_index are "
            "incompatible", max_err_len);
    param_ret = NI_LOGAN_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE;
    LRETURN;
  }

  // check valid for common param
  param_ret = ni_logan_check_common_params(&p_cfg->niParamT408, p_src, p_param_err, max_err_len);
  if (param_ret != NI_LOGAN_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  // check valid for RC param
  param_ret = ni_logan_check_ratecontrol_params(p_cfg, p_param_err, max_err_len);
  if (param_ret != NI_LOGAN_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  // after validation adjust intra_period/avcIdrPeriod values for internal usage
  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    switch (p_cfg->niParamT408.decoding_refresh_type)
    {
      case 0: // Non-IRAP I-p_frame
      {
        // intra_period set to user-configured (above), avcIdrPeriod set to 0
        p_cfg->niParamT408.avcIdrPeriod = 0;
        break;
      }
      case 1: // CRA
      case 2: // IDR
      {
        // intra_period set to 0, avcIdrPeriod set to user-configured (above)
        p_cfg->niParamT408.intra_period = 0;
        break;
      }
      default:
      {
        ni_log(NI_LOG_TRACE, "ERROR: %s() unknown value for niParamT408.decoding_refresh_type: %d\n",
               __FUNCTION__, p_cfg->niParamT408.decoding_refresh_type);
      }
    }
  }
  else if (STD_HEVC == p_cfg->ui8bitstreamFormat)
  {
    p_cfg->niParamT408.avcIdrPeriod = 0;
  }

  if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_CUSTOM)
  {
    p_ctx->keyframe_factor =
      presetGopKeyFrameFactor[p_cfg->niParamT408.gop_preset_index];
  }

  param_ret = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_DEBUG, "useLowDelayPocType=%d\n", p_src->use_low_delay_poc_type);
  // after validation, convert gopPresetIdx based on useLowDelayPocType flag
  // for H.264 to enable poc_type = 2
  if (NI_LOGAN_CODEC_FORMAT_H264 == p_ctx->codec_format &&
      p_src->use_low_delay_poc_type)
  {
    switch (p_cfg->niParamT408.gop_preset_index)
    {
    case GOP_PRESET_IDX_ALL_I:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_17;
      break;
    case GOP_PRESET_IDX_IPP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_18;
      break;
    case GOP_PRESET_IDX_IBBB:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_19;
      break;
    case GOP_PRESET_IDX_IPPPP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_20;
      break;
    case GOP_PRESET_IDX_IBBBB:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_21;
      break;
    case GOP_PRESET_IDX_SP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_22;
      break;
    }
    ni_log(NI_LOG_DEBUG, "final gop_preset_index=%d\n",
           p_cfg->niParamT408.gop_preset_index);
  }

  END:

  return param_ret;
}

ni_logan_retcode_t ni_logan_check_common_params(ni_logan_t408_config_t* p_param,
                                    ni_logan_encoder_params_t* p_src,
                                    char* p_param_err,
                                    uint32_t max_err_len)
{
  ni_logan_retcode_t ret = NI_LOGAN_RETCODE_SUCCESS;
  int32_t low_delay = 0;
  int32_t intra_period_gop_step_size;
  int32_t i, j;

  if (!p_param || !p_src || !p_param_err)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() Null pointer parameters passed\n", __FUNCTION__);
    ret = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  // check low-delay gop structure
  if (0 == p_param->gop_preset_index) // custom gop
  {
    int32_t minVal = 0;
    low_delay = (p_param->custom_gop_params.custom_gop_size == 1);

    if (p_param->custom_gop_params.custom_gop_size > 1)
    {
      minVal = p_param->custom_gop_params.pic_param[0].poc_offset;
      low_delay = 1;
      for (i = 1; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (minVal > p_param->custom_gop_params.pic_param[i].poc_offset)
        {
          low_delay = 0;
          break;
        }
        else
        {
          minVal = p_param->custom_gop_params.pic_param[i].poc_offset;
        }
      }
    }
  }
  else if (1 == p_param->gop_preset_index || 2 == p_param->gop_preset_index ||
           3 == p_param->gop_preset_index || 6 == p_param->gop_preset_index ||
           7 == p_param->gop_preset_index || 9 == p_param->gop_preset_index)
  {
    low_delay = 1;
  }

  if (p_src->low_delay_mode && ! low_delay)
  {
    strncpy(p_param_err, "GOP size must be 1 or frames must be in sequence "
            "when lowDelay is enabled", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_GOP_PRESET;
    LRETURN;
  }

  if (low_delay)
  {
    intra_period_gop_step_size = 1;
  }
  else
  {
    if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      intra_period_gop_step_size = p_param->custom_gop_params.custom_gop_size;
    }
    else
    {
      intra_period_gop_step_size = presetGopSize[p_param->gop_preset_index];
    }
  }

  if (((p_param->intra_period != 0) && ((p_param->intra_period < intra_period_gop_step_size+1) == 1)) ||
      ((p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod < intra_period_gop_step_size+1) == 1)))
  {
    strncpy(p_param_err, "Invalid intra_period and gop_preset_index: gop structure is larger than intra period",
            max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }

  if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) != 0)) ||
      ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) != 0)))
  {
    strncpy(p_param_err, "Invalid intra_period and gop_preset_index: intra period is not a multiple of gop structure size",
            max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }

  // TODO: this error check will never get triggered. remove? (SZ)
  if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) == 1) &&
        p_param->decoding_refresh_type == 0) ||
      ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) == 1) &&
        p_param->decoding_refresh_type == 0))
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: not support decoding refresh type I p_frame for closed gop structure",
            max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }

  if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
  {
    int temp_poc[NI_LOGAN_MAX_GOP_NUM];
    int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
    for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
    {
      if (p_param->custom_gop_params.pic_param[i].poc_offset >
          p_param->custom_gop_params.custom_gop_size)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: poc_offset larger"
                " than GOP size", max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }

      if (p_param->custom_gop_params.pic_param[i].temporal_id >= LOGAN_XCODER_MAX_NUM_TEMPORAL_LAYER)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }

      if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is zero or negative", max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      temp_poc[i] = p_param->custom_gop_params.pic_param[i].poc_offset;
      if (min_poc > temp_poc[i])
      {
        min_poc = temp_poc[i];
      }
    }
    int count_pos = 0;
    for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
    {
      for (j = 0; j < p_param->custom_gop_params.custom_gop_size; j++)
      {
        if (temp_poc[j] == min_poc)
        {
          count_pos++;
          min_poc++;
        }
      }
    }
    if (count_pos != p_param->custom_gop_params.custom_gop_size)
    {
      strncpy(p_param_err, "Invalid custom gop parameters: poc_offset is invalid", max_err_len);
      ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
  }

  if (0 == p_param->use_recommend_enc_params)
  {
    // RDO
    {
      int align_32_width_flag = p_src->source_width % 32;
      int align_16_width_flag = p_src->source_width % 16;
      int align_8_width_flag = p_src->source_width % 8;
      int align_32_height_flag = p_src->source_height % 32;
      int align_16_height_flag = p_src->source_height % 16;
      int align_8_height_flag = p_src->source_height % 8;

      if (((p_param->cu_size_mode & 0x1) == 0) && ((align_8_width_flag != 0) || (align_8_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be "
                "aligned with 8 pixels when enable CU8x8 of cu_size_mode. Recommend to set cu_size_mode |= 0x1 (CU8x8)",
                max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) &&
               ((align_16_width_flag != 0) || (align_16_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be "
                "aligned with 16 pixels when enable CU16x16 of cu_size_mode. Recommend to set cu_size_mode |= 0x2 (CU16x16)",
                max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) &&
               ((p_param->cu_size_mode & 0x4) == 0) && ((align_32_width_flag != 0) || (align_32_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be "
                "aligned with 32 pixels when enable CU32x32 of cu_size_mode. Recommend to set cu_size_mode |= 0x4 (CU32x32)",
                max_err_len);
        ret = NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN;
        LRETURN;
      }
    }
  }

  if ((p_param->conf_win_top < 0) || (p_param->conf_win_top > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_top: out of range", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }
  if (p_param->conf_win_top % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_top: not multiple of 2", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }

  if ((p_param->conf_win_bottom < 0) || (p_param->conf_win_bottom > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: out of range", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }
  if (p_param->conf_win_bottom % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: not multiple of 2", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }

  if ((p_param->conf_win_left < 0) || (p_param->conf_win_left > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_left: out of range", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }
  if (p_param->conf_win_left % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_left: not multiple of 2", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }

  if (p_param->conf_win_right < 0 || p_param->conf_win_right > 8192)
  {
    strncpy(p_param_err, "Invalid conf_win_right: out of range", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_R;
    LRETURN;
  }
  if (p_param->conf_win_right % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_right: not multiple of 2", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_R;
  }

  END:

  return ret;
}

ni_logan_retcode_t ni_logan_check_ratecontrol_params(ni_logan_encoder_config_t* p_cfg,
                                         char* p_param_err,
                                         uint32_t max_err_len)
{
  ni_logan_retcode_t ret = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_t408_config_t* p_param = &p_cfg->niParamT408;

  if (!p_cfg || !p_param_err)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s() Null pointer parameters passed\n", __FUNCTION__);
    ret = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (p_param->roiEnable != 0 && p_param->roiEnable != 1)
  {
    strncpy(p_param_err, "Invalid roiEnable: out of range", max_err_len);
    ret = NI_LOGAN_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  // RevB
  if (p_cfg->ui8rcEnable == 1)
  {
    if (p_param->minQpP > p_param->maxQpP || p_param->minQpB > p_param->maxQpB)
    {
      strncpy(p_param_err, "Invalid min_qp(P/B) and max_qp(P/B): min_qp cannot be larger than max_qp", max_err_len);
      ret = NI_LOGAN_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
  }

  END:

  return ret;
}


/*!******************************************************************************
 *  \brief  Print xcoder user configurations
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_params_print(ni_logan_encoder_params_t* const p_codec_params, ni_logan_device_type_t device_type)
{
  if (!p_codec_params)
  {
    return;
  }

  if (NI_LOGAN_DEVICE_TYPE_DECODER == device_type)
  {
    ni_logan_decoder_input_params_t* p_dec = &p_codec_params->dec_input_params;

    ni_log(NI_LOG_TRACE, "XCoder Decoder Params:\n");

    ni_log(NI_LOG_TRACE, "fps_number / fps_denominator=%d / %d\n", p_codec_params->fps_number, p_codec_params->fps_denominator);
    ni_log(NI_LOG_TRACE, "source_width x source_height=%dx%d\n", p_codec_params->source_width, p_codec_params->source_height);
    ni_log(NI_LOG_TRACE, "bitrate=%d\n", p_codec_params->bitrate);

    ni_log(NI_LOG_TRACE, "hwframes=%d\n", p_dec->hwframes);
  }
  else if (NI_LOGAN_DEVICE_TYPE_ENCODER)
  {
    ni_logan_h265_encoder_params_t* p_enc = &p_codec_params->hevc_enc_params;

    ni_log(NI_LOG_TRACE, "XCoder Encoder Params:\n");

    ni_log(NI_LOG_TRACE, "preset=%d\n", p_codec_params->preset);
    ni_log(NI_LOG_TRACE, "fps_number / fps_denominator=%d / %d\n",
           p_codec_params->fps_number, p_codec_params->fps_denominator);

    ni_log(NI_LOG_TRACE, "source_width x source_height=%dx%d\n",
           p_codec_params->source_width, p_codec_params->source_height);
    ni_log(NI_LOG_TRACE, "bitrate=%d\n", p_codec_params->bitrate);

    ni_log(NI_LOG_TRACE, "profile=%d\n", p_enc->profile);
    ni_log(NI_LOG_TRACE, "level_idc=%d\n", p_enc->level_idc);
    ni_log(NI_LOG_TRACE, "high_tier=%d\n", p_enc->high_tier);

    ni_log(NI_LOG_TRACE, "frame_rate=%d\n", p_enc->frame_rate);

    ni_log(NI_LOG_TRACE, "use_recommend_enc_params=%d\n", p_enc->use_recommend_enc_params);

    // trans_rate not available in Rev B
    ni_log(NI_LOG_TRACE, "enable_rate_control=%d\n", p_enc->rc.enable_rate_control);
    ni_log(NI_LOG_TRACE, "enable_cu_level_rate_control=%d\n", p_enc->rc.enable_cu_level_rate_control);
    ni_log(NI_LOG_TRACE, "enable_hvs_qp=%d\n", p_enc->rc.enable_hvs_qp);
    ni_log(NI_LOG_TRACE, "enable_hvs_qp_scale=%d\n", p_enc->rc.enable_hvs_qp_scale);
    ni_log(NI_LOG_TRACE, "hvs_qp_scale=%d\n", p_enc->rc.hvs_qp_scale);
    ni_log(NI_LOG_TRACE, "min_qp=%d\n", p_enc->rc.min_qp);
    ni_log(NI_LOG_TRACE, "max_qp=%d\n", p_enc->rc.max_qp);
    ni_log(NI_LOG_TRACE, "max_delta_qp=%d\n", p_enc->rc.max_delta_qp);
    ni_log(NI_LOG_TRACE, "rc_init_delay=%d\n", p_enc->rc.rc_init_delay);

    ni_log(NI_LOG_TRACE, "forcedHeaderEnable=%d\n", p_enc->forced_header_enable);
    ni_log(NI_LOG_TRACE, "roi_enable=%d\n", p_enc->roi_enable);
    ni_log(NI_LOG_TRACE, "long_term_ref_enable=%d\n", p_enc->long_term_ref_enable);
    ni_log(NI_LOG_TRACE, "conf_win_top=%d\n", p_enc->conf_win_top);
    ni_log(NI_LOG_TRACE, "conf_win_bottom=%d\n", p_enc->conf_win_bottom);
    ni_log(NI_LOG_TRACE, "conf_win_left=%d\n", p_enc->conf_win_left);
    ni_log(NI_LOG_TRACE, "conf_win_right=%d\n", p_enc->conf_win_right);

    ni_log(NI_LOG_TRACE, "intra_qp=%d\n", p_enc->rc.intra_qp);
    ni_log(NI_LOG_TRACE, "enable_mb_level_rc=%d\n", p_enc->rc.enable_mb_level_rc);

    ni_log(NI_LOG_TRACE, "intra_period=%d\n", p_enc->intra_period);
    ni_log(NI_LOG_TRACE, "decoding_refresh_type=%d\n", p_enc->decoding_refresh_type);

    // Rev. B: H.264 only or HEVC-shared parameters, in ni_logan_t408_config_t
    ni_log(NI_LOG_TRACE, "enable_transform_8x8=%d\n", p_enc->enable_transform_8x8);
    ni_log(NI_LOG_TRACE, "avc_slice_mode=%d\n", p_enc->avc_slice_mode);
    ni_log(NI_LOG_TRACE, "avc_slice_arg=%d\n", p_enc->avc_slice_arg);
    ni_log(NI_LOG_TRACE, "entropy_coding_mode=%d\n", p_enc->entropy_coding_mode);
    ni_log(NI_LOG_TRACE, "intra_mb_refresh_mode=%d\n", p_enc->intra_mb_refresh_mode);
    ni_log(NI_LOG_TRACE, "intra_mb_refresh_arg=%d\n", p_enc->intra_mb_refresh_arg);

    ni_log(NI_LOG_TRACE, "gop_preset_index=%d\n", p_enc->gop_preset_index);
    if (p_enc->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      int i;
      ni_log(NI_LOG_TRACE, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
      for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
      {
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].pic_type=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].poc_offset=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].pic_qp=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].pic_qp);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].ref_poc_L0);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].ref_poc_L1);
        ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].temporal_id=%d\n",
               i, p_enc->custom_gop_params.pic_param[i].temporal_id);
      }
    }
  }
  else if (NI_LOGAN_DEVICE_TYPE_UPLOAD == device_type)
  {
    ni_logan_decoder_input_params_t* p_dec = &p_codec_params->dec_input_params;

    ni_log(NI_LOG_TRACE, "XCoder Uploader Params:\n");

    ni_log(NI_LOG_TRACE, "hwframes=%d\n", p_dec->hwframes);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "XCoder not supported device type:%d\n", device_type);
  }

  return;
}

/*!******************************************************************************
 *  \brief  decoder keep alive thread function triggers every 1 second
 *
 *  \param void thread args
 *
 *  \return void
 *******************************************************************************/
void *ni_logan_session_keep_alive_thread(void *arguments)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_thread_arg_struct_t *args = (ni_logan_thread_arg_struct_t *) arguments;
  ni_logan_instance_status_info_t inst_info = { 0 };
  ni_logan_session_context_t ctx = {0};
  uint32_t loop = 0;
  uint64_t endtime = ni_logan_gettime_ns();
  //interval(nanoseconds) is equals to ctx.keep_alive_timeout/3(330,000,000ns approximately equal to 1/3 second).
  uint64_t interval = args->keep_alive_timeout * 330000000LL;
#ifdef __linux__

#ifndef _ANDROID
  struct sched_param sched_param;
  // Linux has a wide variety of signals, Windows has a few.
  // A large number of signals will interrupt the thread, which will cause heartbeat command interval more than 1 second.
  // So just mask the unuseful signals in Linux
  sigset_t signal;
  sigfillset(&signal);
  ni_logan_pthread_sigmask(SIG_BLOCK, &signal, NULL);

  /* set up schedule priority
   * first try to run with RR mode.
   * if fails, try to set nice value.
   * if fails either, ignore it and run with default priority.
   */
  if (((sched_param.sched_priority = sched_get_priority_max(SCHED_RR)) == -1) ||
        sched_setscheduler(syscall(SYS_gettid), SCHED_RR, &sched_param) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s cannot set scheduler: %s\n",
           __FUNCTION__, strerror(errno));
    if (setpriority(PRIO_PROCESS, 0, -20) != 0)
    {
      ni_log(NI_LOG_TRACE, "%s cannot set nice value: %s\n",
             __FUNCTION__, strerror(errno));
    }
  }

#elif defined(_WIN32)
  /* set up schedule priority.
   * try to set the current thread to time critical level which is the highest prioriy
   * level.
   */
  if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) == 0)
  {
    ni_log(NI_LOG_TRACE, "%s cannot set priority: %d.\n",
           __FUNCTION__, GetLastError());
  }
#endif

#endif
  // Initializes the session context variables that keep alive command and query status command need.
  ctx.hw_id = args->hw_id;
  ctx.session_id = args->session_id;
  ctx.session_timestamp = args->session_timestamp;
  ctx.device_type = args->device_type;
  ctx.blk_io_handle = args->device_handle;
  ctx.event_handle = args->thread_event_handle;
  ctx.p_all_zero_buf = args->p_buffer;
  ctx.keep_alive_timeout = args->keep_alive_timeout;
  ni_log(NI_LOG_TRACE, "%s ctx.keep_alive_timeout: %d.\n", __FUNCTION__,
         ctx.keep_alive_timeout);

  for (; ;)// condition TBD
  {
    retval = ni_logan_send_session_keep_alive(ctx.session_id, ctx.blk_io_handle, ctx.event_handle, ctx.p_all_zero_buf);
    retval = ni_logan_query_status_info(&ctx, ctx.device_type, &inst_info, retval, nvme_admin_cmd_xcoder_config);
    CHECK_ERR_RC2((&ctx), retval, inst_info, nvme_admin_cmd_xcoder_config,
                  ctx.device_type, ctx.hw_id, &(ctx.session_id));

    // 1. If received failure, set the close_thread flag to TRUE, and exit,
    //    then main thread will check this flag and return failure directly;
    // 2. skip checking VPU recovery.
    //    If keep_alive thread detect the VPU RECOVERY before main thread,
    //    the close_thread flag may damage the vpu recovery handling process.
    if ((NI_LOGAN_RETCODE_SUCCESS != retval) &&
        (NI_LOGAN_RETCODE_NVME_SC_VPU_RECOVERY != retval))
    {
       LRETURN;
    }

    endtime += interval;
    while (ni_logan_gettime_ns() < endtime)
    {
      if (args->close_thread)
      {
        LRETURN;
      }
      ni_logan_usleep(10000); // 10ms per loop
    }
  }

  END:
  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
     ni_log(NI_LOG_ERROR, "%s abormal closed:%d\n", __FUNCTION__, retval);
     args->close_thread = true; // changing the value to be True here means the thread has been closed.
  }

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return NULL;
}

/*!******************************************************************************
*  \brief  Open a xcoder uploader instance
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*  \return
*******************************************************************************/
ni_logan_retcode_t ni_logan_uploader_session_open(ni_logan_session_context_t* p_ctx)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  void * p_buffer = NULL;
  uint32_t ui32LBA = 0;
  uint32_t model_load = 0;
  uint32_t low_delay_mode = 0;
  uint32_t buffer_size = 0;
  ni_logan_decoder_session_open_info_t session_info = { 0 };

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): passed parameters are null! return\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_LOGAN_DEVICE_TYPE_DECODER;//NI_LOGAN_DEVICE_TYPE_UPLOAD;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;
    p_ctx->codec_start_time = 0;
    p_ctx->codec_total_ticks = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->p_dec_packet_inf_buf = NULL;

#ifdef _WIN32
    p_ctx->event_handle = ni_logan_create_event();
    if (p_ctx->event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }

    p_ctx->thread_event_handle = ni_logan_create_event();
    if (p_ctx->thread_event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }
#endif

    //malloc zero data buffer
    if(ni_logan_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR: %s() alloc all zero buffer failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //malloc decoder packet info buffer
    if(ni_logan_posix_memalign(&p_ctx->p_dec_packet_inf_buf, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): alloc decoder packet info buffer failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_dec_packet_inf_buf, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //malloc data buffer
    if(ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR: %s() alloc data buffer failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_logan_get_session_id_t *)p_buffer)->session_id = NI_LOGAN_INVALID_SESSION_ID;

    // Get session ID
    ui32LBA = OPEN_GET_SID_R(1, NI_LOGAN_DEVICE_TYPE_DECODER);
    retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);

    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_logan_ntohl(((ni_logan_get_session_id_t *)p_buffer)->session_id);
    if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): p_ctx->device_handle=%" PRIx64 ", "
             "p_ctx->hw_id=%d, p_ctx->session_id=%d\n", __FUNCTION__,
             (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
      ni_logan_decoder_session_close(p_ctx, 0);
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }
    //Send session Info
    memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);
    session_info.codec_format = ni_logan_htonl(p_ctx->codec_format);
    session_info.model_load = ni_logan_htonl(model_load);
    session_info.hw_desc_mode = ni_logan_htonl(NI_LOGAN_CODEC_HW_UPLOAD);

    memcpy(p_buffer, &session_info, sizeof(ni_logan_decoder_session_open_info_t));
    ui32LBA = OPEN_SESSION_W(1, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme write command failed blk_io_handle"
             ": %" PRIx64 " hw_id %d\n", __FUNCTION__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    ni_log(NI_LOG_TRACE, "%s ID:0x%x\n", __FUNCTION__, p_ctx->session_id);
    ni_logan_aligned_free(p_buffer);
  }

  // init for frame pts calculation
  p_ctx->last_pts = NI_LOGAN_NOPTS_VALUE;
  p_ctx->last_dts = NI_LOGAN_NOPTS_VALUE;
  p_ctx->last_dts_interval = 0;
  p_ctx->pts_correction_num_faulty_dts = 0;
  p_ctx->pts_correction_last_dts = 0;
  p_ctx->pts_correction_num_faulty_pts = 0;
  p_ctx->pts_correction_last_pts = 0;

//  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *) & (p_ctx->pts_table), "dec_pts");
//  ni_logan_timestamp_init(p_ctx, (ni_logan_timestamp_table_t * *) & (p_ctx->dts_queue), "dec_dts");

  ni_log(NI_LOG_TRACE, "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d"
         " p_ctx->session_id=%d\n", __FUNCTION__, (int64_t)p_ctx->device_handle,
         p_ctx->hw_id, p_ctx->session_id);
  p_ctx->hw_action = NI_LOGAN_CODEC_HW_NONE;

#ifdef XCODER_DUMP_ENABLED
  char dump_file[256] = { 0 };

  snprintf(dump_file, sizeof dump_file, "%s%d%s", "decoder_in_id", p_ctx->session_id, ".264");
  p_ctx->p_dump[0] = fopen(dump_file, "wb");
  ni_log(NI_LOG_TRACE, "dump_file = %s\n", dump_file);

  snprintf(dump_file, sizeof dump_file, "%s%d%s", "decoder_out_id", p_ctx->session_id, ".yuv");
  p_ctx->p_dump[1] = fopen(dump_file, "wb");
  ni_log(NI_LOG_TRACE, "dump_file = %s\n", dump_file);

  if (!p_ctx->p_dump[0] || !p_ctx->p_dump[1])
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Cannot open dump file\n", __FUNCTION__);
  }
#endif

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);
  return retval;
}

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
                                              ni_logan_session_context_t *dst_p_ctx)
{
  if (!src_p_ctx || !dst_p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  dst_p_ctx->max_nvme_io_size         = src_p_ctx->max_nvme_io_size;
  dst_p_ctx->device_handle            = src_p_ctx->device_handle;
  dst_p_ctx->blk_io_handle            = src_p_ctx->blk_io_handle;
  dst_p_ctx->hw_id                    = src_p_ctx->hw_id;

  return NI_LOGAN_RETCODE_SUCCESS;
}

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
                              ni_logan_frame_t* p_frame)
{
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t size = 0;
  uint32_t i = 0;
  uint32_t tx_size = 0, aligned_tx_size = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  ni_logan_instance_buf_info_t buf_info = { 0 };

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR: Invlid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2];
  ni_log(NI_LOG_TRACE, "%s(): frame size bytes =%d  %d is metadata!\n",
         __FUNCTION__, frame_size_bytes, 0);

  retval = ni_logan_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                      NI_LOGAN_DEVICE_TYPE_DECODER, &buf_info, 1);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));

  if (NI_LOGAN_RETCODE_SUCCESS != retval ||
      buf_info.buf_avail_size == 0)
  {
    ni_log(NI_LOG_ERROR, "Warning upload write query fail rc %d or available "
           "buf size %u < frame size %u !\n", retval,
           buf_info.buf_avail_size, frame_size_bytes);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "Info hwupload write query success, available buf "
         "size %u >= frame size %u !\n",
         buf_info.buf_avail_size, frame_size_bytes);

  if (!p_ctx->frame_num)
  {
    retval = ni_logan_config_instance_sos(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR %s(): Failed to send SOS.\n", __FUNCTION__);
      LRETURN;
    }
  }

  if (p_frame->p_data)
  {
    ni_logan_instance_dec_packet_info_t *p_dec_packet_info;
    p_dec_packet_info = (ni_logan_instance_dec_packet_info_t *)p_ctx->p_dec_packet_inf_buf;
    p_dec_packet_info->packet_size = frame_size_bytes;
    retval = ni_logan_nvme_send_write_cmd(
      p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_dec_packet_inf_buf,
      NI_LOGAN_DATA_BUFFER_LEN, CONFIG_INSTANCE_SetPktSize_W(1, p_ctx->session_id,
                                                       NI_LOGAN_DEVICE_TYPE_DECODER));
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): config pkt size command failed\n",
             __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    uint32_t ui32LBA = WRITE_INSTANCE_W(1, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
    ni_log(NI_LOG_TRACE, "%s: p_data=%p, p_frame->buffer_size=%u "
           "p_ctx->frame_num=%" PRIu64 ", LBA=0x%x, Session ID=%d\n",
           __FUNCTION__, p_frame->p_data, p_frame->buffer_size,
           p_ctx->frame_num, ui32LBA, p_ctx->session_id);
    sent_size = frame_size_bytes;
    if (sent_size % NI_LOGAN_MEM_PAGE_ALIGNMENT)
    {
      sent_size = ( (sent_size / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_frame->p_buffer, sent_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    p_ctx->frame_num++;
    size = frame_size_bytes;

#ifdef XCODER_DUMP_DATA
    char dump_file[128];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-hwup-fme/fme-%04ld.yuv",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

    FILE *f = fopen(dump_file, "wb");
    fwrite(p_frame->p_buffer, p_frame->data_len[0] + p_frame->data_len[1] +
           p_frame->data_len[2], 1, f);
    fflush(f);
    fclose(f);
#endif
  }

  retval = size;

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);
  return retval;
}

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
                                    ni_logan_hwframe_surface_t* hwdesc)
{
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_instance_buf_info_t hwdesc_info = { 0 };
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !hwdesc)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR: Invlid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  retval = ni_logan_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
    NI_LOGAN_DEVICE_TYPE_DECODER, &hwdesc_info, 1);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
    p_ctx->device_type, p_ctx->hw_id,
    &(p_ctx->session_id));

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "Warning upload read hwdesc fail rc %d or ind "
           "!\n", retval);
    retval = NI_LOGAN_RETCODE_FAILURE;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "Info hwupload read hwdesc success, FrameIndex=%d !\n",
           hwdesc_info.hw_inst_ind.frame_index);
    hwdesc->i8InstID = (int8_t) hwdesc_info.hw_inst_ind.inst_id;
    hwdesc->i8FrameIdx = (int8_t) hwdesc_info.hw_inst_ind.frame_index;
    hwdesc->ui16SessionID = p_ctx->session_id;
    hwdesc->encoding_type = (int8_t)p_ctx->codec_format;

#ifdef _WIN32
    int64_t handle = (int64_t) p_ctx->blk_io_handle;
    hwdesc->device_handle = (int32_t) (handle & 0xFFFFFFFF);
    hwdesc->device_handle_ext = (int32_t) (handle >> 32);
#else
    hwdesc->device_handle = p_ctx->blk_io_handle;
#endif

    hwdesc->bit_depth = p_ctx->bit_depth_factor;
    assert(hwdesc->i8FrameIdx >= 0);
  }
  END:
  return retval;
}

/*!*****************************************************************************
*  \brief  clear a particular xcoder instance buffer/data
*          The device handle is got from decoder or uploader,
*          Sent clear HW frame buffer command here to recycle it.
*
*  \param  ni_logan_hwframe_surface_t* hwdesc - xcoder hardware descriptor
*  \param  ni_device_handle_t device_handle - device handle
*  \param  ni_event_handle_t event_handle - event handle
*
*  \return - NI_LOGAN_RETCODE_SUCCESS on success, NI_LOGAN_RETCODE_ERROR_INVALID_SESSION,
*            NI_LOGAN_RETCODE_ERROR_MEM_ALOC or NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED on
*            failure
******************************************************************************/
ni_logan_retcode_t ni_logan_clear_instance_buf(ni_logan_hwframe_surface_t* surface,
                                   ni_device_handle_t device_handle,
                                   ni_event_handle_t event_handle)
{
  void* p_buffer = NULL;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t dataLen = 0;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter - device_handle %d\n",
         __FUNCTION__, device_handle);

  if (NI_LOGAN_INVALID_SESSION_ID == surface->ui16SessionID)
  {
    ni_log(NI_LOG_ERROR, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  //malloc data buffer
  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() alloc data buffer failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_LOGAN_DATA_BUFFER_LEN);

  ((ni_logan_recycle_buffer_t *)p_buffer)->i8FrameIdx = surface->i8FrameIdx;
  ((ni_logan_recycle_buffer_t *)p_buffer)->i8InstID = surface->i8InstID;
  //maybe just set 13 aqs inst id again?
  ni_log(NI_LOG_TRACE, "%s():i8FrameIdx = %d, i8InstID = %d\n",
         __FUNCTION__, surface->i8FrameIdx, surface->i8InstID);

  ui32LBA = CONFIG_INSTANCE_RecycleBuf_W(surface->ui16SessionID,NI_LOGAN_DEVICE_TYPE_DECODER);
  retval = ni_logan_nvme_send_write_cmd(device_handle, event_handle,
                                  p_buffer, NI_LOGAN_DATA_BUFFER_LEN, ui32LBA);
  //Cannot check sessio stats here since this isn't a session command.
  if(retval < 0)
  {
    ni_log(NI_LOG_ERROR, " %s(): NVME command Failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

/*!******************************************************************************
*  \brief  Get Card Serial Number from received Nvme Indentify info
*
*  \param
*
*  \return
*******************************************************************************/
void ni_logan_populate_serial_number(ni_logan_serial_num_t* p_serial_num, void* p_data)
{
  ni_logan_nvme_identity_t* p_id_data = (ni_logan_nvme_identity_t*)p_data;

  if ((!p_serial_num) || (!p_data))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
           __FUNCTION__);
    LRETURN;
  }

  if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
    (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
  {
    LRETURN;
  }

  memset(p_serial_num->ai8Sn, 0, sizeof(p_serial_num->ai8Sn));
  memcpy(p_serial_num->ai8Sn, p_id_data->ai8Sn, sizeof(p_serial_num->ai8Sn));

  ni_log(NI_LOG_TRACE, "F/W SerialNum: %.20s\n", p_serial_num->ai8Sn);

  END:
  return;
}

/*!******************************************************************************
*  \brief   Retrieve a hw desc p_frame from decoder
*           When yuvbypass enabled, this is used for decoder
*           to read hardware frame index, extra data and meta data
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  ni_logan_frame_t* p_frame - xcoder frame
*
*  \return rx_size on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_session_read_desc(ni_logan_session_context_t* p_ctx,
                                          ni_logan_frame_t* p_frame)
{
  //Needs serious editing to support hwdesc read again, this is currently vanilla read
  ni_logan_instance_mgr_stream_info_t data = { 0 };
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t* p_data_buffer = (uint8_t*)p_frame->p_buffer;
  uint32_t data_buffer_size = p_frame->buffer_size;
  int i = 0;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  int metadata_hdr_size = NI_LOGAN_FW_META_DATA_SZ;
  int sei_size = 0;
  int frame_cycle = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t total_yuv_met_size = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  int keep_processing = 1;
  ni_logan_instance_buf_info_t buf_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): xcoder instance id < 0, return\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.

  total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;
  total_yuv_met_size = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + metadata_hdr_size;
  ni_log(NI_LOG_TRACE, "Total bytes to read %d total_yuv_met_size %d \n", total_bytes_to_read, total_yuv_met_size);
  while (1)
  {
    query_retry++;
    retval = ni_logan_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_READ,
      NI_LOGAN_DEVICE_TYPE_DECODER, &buf_info, 1);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
      p_ctx->device_type, p_ctx->hw_id,
      &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_TRACE, "Info query buf_info.size = %u\n",
      buf_info.buf_avail_size);

    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "Warning dec read query fail rc %d retry %d\n",
        retval, query_retry);

      if (query_retry >= 1000)
      {
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
      ni_logan_usleep(100);
      ni_logan_pthread_mutex_lock(&p_ctx->mutex);
    }
    else if (buf_info.buf_avail_size == (metadata_hdr_size - sizeof(ni_logan_hwframe_surface_t)))
    {
      ni_log(NI_LOG_TRACE, "Info only metadata hdr is available, seq change?\n");
      p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_SEQ_CHANGE_DRAINING;
      total_bytes_to_read = metadata_hdr_size;
      break;
    }
    else if (buf_info.buf_avail_size < total_yuv_met_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        ni_log(NI_LOG_TRACE, "Info dec query, ready_to_close %u, query eos\n",
          p_ctx->ready_to_close);
        retval = ni_logan_query_stream_info(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER, &data, 1);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (data.is_flushed)
        {
          ni_log(NI_LOG_TRACE, "Info eos reached.\n");
          p_frame->end_of_stream = 1;
          retval = NI_LOGAN_RETCODE_SUCCESS;
          LRETURN;
        }
      }

      ni_log(NI_LOG_TRACE, "Warning dec read available buf size == 0 "
             "eos %u  nb try %d\n", p_frame->end_of_stream, query_retry);

      if (NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status &&
        query_retry < 1000)
      {
        ni_logan_pthread_mutex_unlock(&p_ctx->mutex);
        ni_logan_usleep(100);
        ni_logan_pthread_mutex_lock(&p_ctx->mutex);
        continue;
      }
      retval = NI_LOGAN_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
      // We have to ensure there are adequate number of DTS for picture
      // reorder delay otherwise wait for more packets to be sent to decoder.
      ni_logan_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
      if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
          !p_ctx->ready_to_close)
      {
        retval = NI_LOGAN_RETCODE_SUCCESS;
        LRETURN;
      }

      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_logan_query_stream_info(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER, &data, 1);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_TRACE, "Info dec YUV query, pic size %ux%u xfer frame size "
               "%ux%u frame-rate %u is_flushed %u\n",
               data.picture_width, data.picture_height,
               data.transfer_frame_stride, data.transfer_frame_height,
               data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->active_bit_depth = (p_ctx->bit_depth_factor==2)?10:8;
        p_ctx->is_sequence_change = 1;

        ni_log(NI_LOG_TRACE, "Info dec YUV, adjust frame size from %ux%u %dbits to %ux%u\n",
               p_frame->video_width, p_frame->video_height, p_ctx->active_bit_depth,
               p_ctx->active_video_width, p_ctx->active_video_height);

        retval = ni_logan_frame_buffer_alloc(p_frame,
                                       p_ctx->active_video_width,
                                       p_ctx->active_video_height,
                                       p_ctx->codec_format == NI_LOGAN_CODEC_FORMAT_H264,
                                       1,
                                       p_ctx->bit_depth_factor,
                                       1); //Alloc space for write to data[3] and metadata

        if (NI_LOGAN_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;

        p_data_buffer = (uint8_t*)p_frame->p_buffer;

        // make sure we don't read more than available
        ni_log(NI_LOG_TRACE, "Info dec buf size: %u YUV frame + meta-hdr size: %u "
               "available: %u\n", p_frame->buffer_size, total_bytes_to_read,
               buf_info.buf_avail_size);
      }
      break;
    }
  }// end while1 query retry
  unsigned int bytes_read_so_far = 0;

  ni_log(NI_LOG_TRACE, "total_bytes_to_read %d max_nvme_io_size %d ylen %d "
         "cr len %d cb len %d hwdes len %d hdr %d\n", total_bytes_to_read,
         p_ctx->max_nvme_io_size, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2], p_frame->data_len[3], metadata_hdr_size);

  if (total_bytes_to_read == metadata_hdr_size) // metadata alone, seqchange?
  {
    buf_info.buf_avail_size = buf_info.buf_avail_size + sizeof(ni_logan_hwframe_surface_t);
  }
  else
  {
    buf_info.buf_avail_size = buf_info.buf_avail_size + 2 * sizeof(ni_logan_hwframe_surface_t) -
                              (p_ctx->active_video_width * p_ctx->active_video_height * 3 / 2) * p_ctx->bit_depth_factor;
  }
  ni_log(NI_LOG_TRACE, "buf_avail_size = %d \n", buf_info.buf_avail_size);
  if (buf_info.buf_avail_size < total_bytes_to_read)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() avaliable size(%u) less than needed (%u)\n",
           __FUNCTION__, buf_info.buf_avail_size, total_bytes_to_read);
    ni_logan_assert(0);
  }

  read_size_bytes = buf_info.buf_avail_size;
  ui32LBA = READ_INSTANCE_R(1, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);
  if (read_size_bytes % NI_LOGAN_MEM_PAGE_ALIGNMENT)
  {
    read_size_bytes = ( (read_size_bytes / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_logan_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 p_data_buffer, read_size_bytes, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read,
               p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }
  else
  {
    // command issued successfully, now exit
    ni_logan_metadata_dec_frame_t* p_meta =
      (ni_logan_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer
        + p_frame->data_len[0] + p_frame->data_len[1]
        + p_frame->data_len[2] + p_frame->data_len[3]);

    if (buf_info.buf_avail_size != metadata_hdr_size)
    {
      sei_size = p_meta->sei_size;
      frame_cycle = p_meta->frame_cycle;

      ni_logan_hwframe_surface_t* p_data3 = (ni_logan_hwframe_surface_t*)((uint8_t*)p_frame->p_buffer + p_frame->data_len[0]
                                      + p_frame->data_len[1] + p_frame->data_len[2]);

      // Zhong: manually set bit_depth based on known; should this be set by fw ?
      p_data3->bit_depth = p_ctx->bit_depth_factor;


      ni_log(NI_LOG_TRACE, "%s(): i8FrameIdx:%d, %d\n", __FUNCTION__,
             p_data3->i8FrameIdx, p_meta->hwdesc.i8FrameIdx);
#ifdef _WIN32
      int64_t handle = (int64_t) p_ctx->blk_io_handle;
      p_data3->device_handle = (int32_t) (handle & 0xFFFFFFFF);
      p_data3->device_handle_ext = (int32_t) (handle >> 32);
#else
      p_data3->device_handle = p_ctx->blk_io_handle; //Libxcoder knows the handle so overwrite here
#endif
      p_data3->ui16SessionID = (uint16_t)p_ctx->session_id;
      p_data3->encoding_type = (int8_t)p_ctx->codec_format;

      ni_log(NI_LOG_TRACE, "%s:sei_size=%d device_handle=%d == hw_id=%d "
             "ses_id=%d\n", __FUNCTION__, sei_size, p_data3->device_handle,
             p_ctx->hw_id, p_data3->ui16SessionID);
      ni_log(NI_LOG_TRACE, "%s: ui16FrameIdx=%d, bit_depth(in data3)=%d, "
             "bit_depth(in meta)=%d\n", __FUNCTION__, p_data3->i8FrameIdx,
             p_data3->bit_depth, p_meta->hwdesc.bit_depth);

      // set seq_change when received first sequence changed frame.
      if (p_ctx->session_run_state == LOGAN_SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
      {
        ni_log(NI_LOG_TRACE, "%s: sequence change first frame, set seq_change\n", __FUNCTION__);
        p_data3->seq_change = 1;
        p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_NORMAL;
      }
    }
    p_ctx->codec_total_ticks += frame_cycle;
    total_bytes_to_read = total_bytes_to_read + sei_size;

    ni_log(NI_LOG_TRACE, "%s success, retval %d total_bytes_to_read include "
           "sei %d sei_size %d frame_cycle %d\n", __FUNCTION__, retval,
           total_bytes_to_read, sei_size, frame_cycle);
  }


  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition
  bytes_read_so_far = total_bytes_to_read;
  p_frame->src_codec = p_ctx->codec_format;
  rx_size = ni_logan_create_frame(p_frame, bytes_read_so_far, &frame_offset, true);
  p_ctx->frame_pkt_offset = frame_offset;

  // if using old firmware, bit_depth=0 so use bit_depth_factor
  if (!p_frame->bit_depth)
    p_frame->bit_depth = (p_ctx->bit_depth_factor==2)?10:8;

  // if sequence change, update bit depth factor
  if  ((rx_size == 0))
    p_ctx->bit_depth_factor = (p_frame->bit_depth==10) ? 2: 1;

  if (rx_size > 0 && total_bytes_to_read != metadata_hdr_size)
  {
    ni_log(NI_LOG_TRACE, "%s(): s-state %d seq change %d\n", __FUNCTION__,
           p_ctx->session_run_state, p_ctx->is_sequence_change);
    if (ni_logan_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)&p_frame->dts,
                                        LOGAN_XCODER_FRAME_OFFSET_DIFF_THRES, (p_ctx->frame_num % 500 == 0),
                                        p_ctx->buffer_pool) != NI_LOGAN_RETCODE_SUCCESS)
    {
      if (p_ctx->last_dts != NI_LOGAN_NOPTS_VALUE && !p_ctx->ready_to_close)
      {
        // Mark as DTS padding for offset compensation
        p_ctx->pic_reorder_delay++;
        p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
        ni_log(NI_LOG_ERROR, "Padding DTS:%ld.\n", p_frame->dts);
      }
      else
      {
        p_frame->dts = NI_LOGAN_NOPTS_VALUE;
      }
    }

    // Read the following DTS for picture reorder delay
    if (p_ctx->is_sequence_change)
    {
      for (i = 0; i < p_ctx->pic_reorder_delay; i++)
      {
        if (p_ctx->last_pts == NI_LOGAN_NOPTS_VALUE && p_ctx->last_dts == NI_LOGAN_NOPTS_VALUE)
        {
          // If the p_frame->pts is unknown in the very beginning of the stream
          // (video stream only) we assume p_frame->pts == 0 as well as DTS less
          // than PTS by 1000 * 1/timebase
          if (p_frame->pts >= p_frame->dts && p_frame->pts - p_frame->dts < 1000)
          {
            break;
          }
        }

        if (ni_logan_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)&p_frame->dts,
                                            LOGAN_XCODER_FRAME_OFFSET_DIFF_THRES, (p_ctx->frame_num % 500 == 0),
                                            p_ctx->buffer_pool) != NI_LOGAN_RETCODE_SUCCESS)
        {
          p_frame->dts = NI_LOGAN_NOPTS_VALUE;
        }
      }
      // Reset for DTS padding counting
      p_ctx->pic_reorder_delay = 0;
    }

    if (p_ctx->is_dec_pkt_512_aligned)
    {
      if (p_ctx->is_sequence_change)
      {
        // if not a bitstream retrieve the pts of the frame corresponding to the first YUV output
        if (p_ctx->pts_offsets[0] != NI_LOGAN_NOPTS_VALUE && p_ctx->pkt_index != -1)
        {
          ni_logan_metadata_dec_frame_t* p_metadata =
                  (ni_logan_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer +
                  p_frame->data_len[0] + p_frame->data_len[1] +
                  p_frame->data_len[2] + p_frame->data_len[3]);
          uint64_t num_fw_pkts = p_metadata->metadata_common.ui64_data.frame_offset / 512;
          ni_log(NI_LOG_TRACE, "%s: num_fw_pkts %u frame_offset %" PRIu64 " "
                 "ni_logan_create_frame\n", __FUNCTION__, num_fw_pkts,
                 p_metadata->metadata_common.ui64_data.frame_offset);
          int idx = 0;
          uint64_t cumul = p_ctx->pkt_offsets_index[0];
          bool bFound = (num_fw_pkts >= cumul);
          while (cumul < num_fw_pkts) // look for pts index
          {
            ni_log(NI_LOG_TRACE, "%s: cumul %u\n", __FUNCTION__, cumul);
            if (idx == NI_LOGAN_MAX_DEC_REJECT)
            {
              ni_log(NI_LOG_ERROR, "Invalid index computation oversizing NI_LOGAN_MAX_DEC_REJECT! \n");
              break;
            }
            else
            {
              idx++;
              cumul += p_ctx->pkt_offsets_index[idx];
              ni_log(NI_LOG_TRACE, "%s: pkt_offsets_index[%d] %" PRIu64 "\n",
                     __FUNCTION__, idx, p_ctx->pkt_offsets_index[idx]);
            }
          }

          if ((idx != NI_LOGAN_MAX_DEC_REJECT) && bFound)
          {
            p_frame->pts = p_ctx->pts_offsets[idx];
          }
          else if (p_ctx->session_run_state == LOGAN_SESSION_RUN_STATE_RESETTING)
          {
            ni_log(NI_LOG_TRACE, "%s(): session %u recovering and adjusting "
                   "ts.\n", __FUNCTION__, p_ctx->session_id);
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_NORMAL;
          }
          else // use pts = 0 as offset
          {
            p_frame->pts = 0;
          }
        }
        else
        {
          p_frame->pts = 0;
        }
      }
      else
      {
        p_frame->pts = p_ctx->last_pts + p_ctx->last_dts_interval;
      }
    }
    else
    {
      i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                      p_ctx->pkt_offsets_index,
                                      NI_LOGAN_FIFO_SZ, frame_offset);
      if (i >= 0)
      {
        // According to LGXCOD-3099 the frame_offset would be less than
        // expected by the size of SEI unit when there is malformed SEI
        // being sent into decoder. That will lead to mismatch of the
        // true PTS offset with the decoded frame. So we need to correct
        // the offset range when there is suspicious frame_offset for
        // decoded frames.
        uint64_t d1 = frame_offset - p_ctx->pkt_offsets_index_min[i];
        uint64_t d2 = p_ctx->pkt_offsets_index[i] - frame_offset;
        if (d1 > d2)
        {
          // When the frame_offset is closer to the right boundary, the
          // right margin is caused by the missing SEI size.
          i++;
        }

        p_frame->pts = p_ctx->pts_offsets[i];
        p_frame->flags = p_ctx->flags_array[i];
        p_frame->p_custom_sei = (uint8_t *)p_ctx->pkt_custom_sei[i % NI_LOGAN_FIFO_SZ];
        p_ctx->pkt_custom_sei[i % NI_LOGAN_FIFO_SZ] = NULL;
      }
      else
      {
        //backup solution pts
        p_frame->pts = p_ctx->last_pts + p_ctx->last_dts_interval;
        ni_log(NI_LOG_ERROR, "ERROR: NO pts found consider increasing NI_LOGAN_FIFO_SZ!\n");
      }
    }

    p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts, p_ctx->last_pts);
    p_ctx->last_pts = p_frame->pts;
    if ((0 == p_ctx->is_sequence_change) && (NI_LOGAN_NOPTS_VALUE != p_frame->dts) && (NI_LOGAN_NOPTS_VALUE != p_ctx->last_dts))
    {
      p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    }
    p_ctx->last_dts = p_frame->dts;
    p_ctx->is_sequence_change = 0;
    p_ctx->frame_num++;
  }

#ifdef XCODER_DUMP_ENABLED
  fwrite(p_frame->data[0], rx_size, 1, p_ctx->p_dump[1]);
#endif

  ni_log(NI_LOG_TRACE, "%s(): received data: [0x%08x]\n", __FUNCTION__, rx_size);
  ni_log(NI_LOG_TRACE, "%s(): p_frame->start_of_stream=%d, p_frame->end_of_stream=%d, "
         "p_frame->video_width=%d, p_frame->""video_height=%d\n", __FUNCTION__,
         p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_TRACE, "%s(): p_frame->data_len[0/1/2]=%d/%d/%d\n", __FUNCTION__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
    ni_log(NI_LOG_TRACE, "Decoder pts queue size = %d dts queue size = %d\n\n",
      ((ni_logan_timestamp_table_t*)p_ctx->pts_table)->list.count,
      ((ni_logan_timestamp_table_t*)p_ctx->dts_queue)->list.count);
    // scan and clean up
    ni_logan_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue, p_ctx->buffer_pool);
  }

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __FUNCTION__, retval);
    return retval;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __FUNCTION__, rx_size);
    return rx_size;
  }
}

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
                               ni_logan_hwframe_surface_t* hwdesc)
{
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  int rx_size = 0;
  ni_logan_instance_mgr_stream_info_t data = { 0 };
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t* p_data_buffer = (uint8_t*)p_frame->p_buffer;
  uint32_t data_buffer_size = p_frame->buffer_size;
  int i = 0;
  int metadata_hdr_size = NI_LOGAN_FW_META_DATA_SZ;
  int sei_size = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  int keep_processing = 1;
  ni_logan_instance_buf_info_t buf_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;
  unsigned int bytes_read_so_far = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): xcoder instance id < 0, return\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  for (i = 0; i < NI_LOGAN_MAX_NUM_DATA_POINTERS - 1; i++) //discount the hwdesc
  {
    if (!p_frame->p_data[i])
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): No receive buffer allocated.\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      LRETURN;
    }
  }

  if (0 == p_frame->data_len[0])
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): p_frame->data_len[0] = 0!.\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2];// +metadata_hdr_size;

  ni_log(NI_LOG_TRACE, "%s total_bytes_to_read %d max_nvme_io_size %d ylen %d "
         "cr len %d cb len %d hdr %d\n", __FUNCTION__, total_bytes_to_read,
         p_ctx->max_nvme_io_size, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2], metadata_hdr_size);

  //Apply read configuration here
  retval = ni_logan_config_instance_rw(p_ctx, INST_READ_CONFIG,
                                1,
                                NI_LOGAN_CODEC_HW_ENABLE | NI_LOGAN_CODEC_HW_DOWNLOAD,
                                hwdesc->i8FrameIdx);
  CHECK_ERR_RC(p_ctx, retval, NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  read_size_bytes = total_bytes_to_read;
  ui32LBA = READ_INSTANCE_R(0, p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

  if (read_size_bytes % NI_LOGAN_MEM_PAGE_ALIGNMENT)
  {
    read_size_bytes = ((read_size_bytes / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
  }

#ifdef _WIN32
  int64_t handle = (((int64_t) hwdesc->device_handle_ext) << 32) | hwdesc->device_handle;
  retval = ni_logan_nvme_send_read_cmd((ni_device_handle_t) handle, p_ctx->event_handle,
                                 p_data_buffer, read_size_bytes, ui32LBA);
#else
  retval = ni_logan_nvme_send_read_cmd((ni_device_handle_t) hwdesc->device_handle, p_ctx->event_handle,
                                 p_data_buffer, read_size_bytes, ui32LBA);
#endif
  CHECK_ERR_RC(p_ctx, retval, NI_LOGAN_NO_CHECK_TS_NVME_CMD_OP,
               p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s success, retval %d total_bytes_to_read include "
           "sei %d sei_size %d\n", __FUNCTION__, retval, total_bytes_to_read, sei_size);
  }

  ni_log(NI_LOG_TRACE, "%s total_bytes_to_read %d ylen %d cr len %d cb len %d data0 0x%x data_end 0x%x idx %d\n", \
         __FUNCTION__, total_bytes_to_read, p_frame->data_len[0],
         p_frame->data_len[1], p_frame->data_len[2], p_data_buffer[0],
         p_data_buffer[total_bytes_to_read-1], total_bytes_to_read -1);

  bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  rx_size = ni_logan_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
  p_ctx->frame_pkt_offset = frame_offset;

  ni_log(NI_LOG_TRACE, "%s(): received data:[0x%08x]\n", __FUNCTION__, rx_size);
  ni_log(NI_LOG_TRACE, "%s(): p_frame->start_of_stream=%d, "
         "p_frame->end_of_stream=%d, p_frame->video_width=%d, "
         "p_frame->video_height=%d\n", __FUNCTION__, p_frame->start_of_stream,
         p_frame->end_of_stream, p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_TRACE, "%s(): p_ctx->frame_num%ld, p_frame->data_len[0/1/2]=%d/%d/%d\n",
         __FUNCTION__, p_ctx->frame_num, p_frame->data_len[0],
         p_frame->data_len[1], p_frame->data_len[2]);

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __FUNCTION__, retval);
    return retval;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __FUNCTION__, rx_size);
    return rx_size;
  }
}

/*!******************************************************************************
*  \brief  Close an xcoder upload instance
*
*  \param   ni_logan_session_context_t p_ctx - xcoder Context
*
*  \return
*******************************************************************************/
ni_logan_retcode_t ni_logan_uplosader_session_close(ni_logan_session_context_t* p_ctx)
{
  return 0;
}

/*!******************************************************************************
*  \brief  Setup framepool for hwupload. Uses decoder framepool
*
*  \param  ni_logan_session_context_t p_ctx - xcoder Context
*  \param  uint32_t pool_size - buffer pool in HW
*
*  \return NI_LOGAN_RETCODE_SUCCESS on success,
*          negative value like NI_LOGAN_RETCODE_FAILURE in ni_logan_retcode_t on failure
*******************************************************************************/
ni_logan_retcode_t ni_logan_hwupload_init_framepool(ni_logan_session_context_t* p_ctx,
                                        uint32_t pool_size)
{
  ni_logan_init_frames_params_t* p_init_frames_param = NULL;
  uint32_t buffer_size = sizeof(ni_logan_encoder_config_t);
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  int i = 0;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): NULL pointer p_config passed\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR: Invalid session ID, return\n");
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;
  if (ni_logan_posix_memalign((void **)&p_init_frames_param, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate encConf buffer.\n");
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset((void *)p_init_frames_param, 0, buffer_size);

  p_init_frames_param->width = ni_logan_ntohs(p_ctx->active_video_width);
  p_init_frames_param->height = ni_logan_ntohs(p_ctx->active_video_height);
  p_init_frames_param->bit_depth_factor = ni_logan_ntohs(p_ctx->bit_depth_factor);
  p_init_frames_param->pool_size = ni_logan_ntohs(pool_size);

  ni_log(NI_LOG_TRACE, "%s():%d x %d x bitdepth %d with %d framepool \n",
         __FUNCTION__, p_init_frames_param->width, p_init_frames_param->height,
         p_init_frames_param->bit_depth_factor, p_init_frames_param->pool_size);

  ui32LBA = CONFIG_INSTANCE_InitFramePool_W(p_ctx->session_id, NI_LOGAN_DEVICE_TYPE_DECODER);

  retval = ni_logan_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_init_frames_param, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_logan_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_logan_decoder_session_close(p_ctx, 0);
    if (NI_LOGAN_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: ni_logan_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  END:
  ni_logan_aligned_free(p_init_frames_param);

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

  return retval;
}

void ni_logan_change_priority(void)
{
#ifdef __linux__
  struct sched_param sched_param;
  
  //struct sched_param sched_param;
  // Linux has a wide variety of signals, Windows has a few.
  // A large number of signals will interrupt the thread, which will cause heartbeat command interval more than 1 second.
  // So just mask the unuseful signals in Linux
  //sigset_t signal;
  //sigfillset(&signal);
  //ni_logan_pthread_sigmask(SIG_BLOCK, &signal, NULL);

  /* set up schedule priority
   * first try to run with RR mode.
   * if fails, try to set nice value.
   * if fails either, ignore it and run with default priority.
   */
  if (((sched_param.sched_priority = sched_get_priority_max(SCHED_RR)) == -1) ||
      sched_setscheduler(syscall(SYS_gettid), SCHED_RR, &sched_param) < 0)
  {
    ni_log(NI_LOG_TRACE, "%s cannot set scheduler: %s\n",
          __FUNCTION__, strerror(errno));
    if (setpriority(PRIO_PROCESS, 0, -20) != 0)
    {
      ni_log(NI_LOG_TRACE, "%s cannot set nice value: %s\n",
            __FUNCTION__, strerror(errno));
    }
  }
#endif
}
