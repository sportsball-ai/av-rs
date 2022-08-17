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
*  \file   ni_device_api_priv.c
*
*  \brief  Private functions used by main ni_device_api_c file
*
*******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <linux/types.h>
#endif
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <limits.h>
#include <signal.h>
#include <dirent.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <stdint.h>
#include <poll.h>
#include "ni_p2p_ioctl.h"
#endif
#include "inttypes.h"
#include "ni_nvme.h"
#include "ni_device_api.h"
#include "ni_device_api_priv.h"
#include "ni_util.h"
#include "ni_lat_meas.h"

typedef enum _ni_t35_sei_mesg_type
{
    NI_T35_SEI_CLOSED_CAPTION = 0,
    NI_T35_SEI_HDR10_PLUS = 1
} ni_t35_sei_mesg_type_t;

static uint8_t g_itu_t_t35_cc_sei_hdr_hevc[NI_CC_SEI_HDR_HEVC_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x4e,
    0x01,   // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
    // nuh_layer_id=0 nuh_temporal_id_plus1=1)
    0x04,     // payloadType= 4 (user_data_registered_itu_t_t35)
    0 + 11,   // payLoadSize= ui16Len + 11; to be set (index 7)
    0xb5,     //  itu_t_t35_country_code =181 (North America)
    0x00,
    0x31,   //  itu_t_t35_provider_code = 49
    0x47, 0x41, 0x39,
    0x34,       // ATSC_user_identifier = "GA94"
    0x03,       // ATSC1_data_user_data_type_code=3
    0 | 0xc0,   // (ui16Len/3) | 0xc0 (to be set; index 16) (each CC character
    //is 3 bytes)
    0xFF   // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_hevc[NI_HDR10P_SEI_HDR_HEVC_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x4e,
    0x01,   // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
    // nuh_layer_id=0 nuh_temporal_id_plus1=1)
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0x00,   // payLoadSize; to be set (index 7)
    0xb5,   //  u8 itu_t_t35_country_code =181 (North America)
    //0x00,
    //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
    //0x00,
    //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
    // payLoadSize count starts from itu_t_t35_provider_code and goes until
    // and including trailer
};

static uint8_t g_itu_t_t35_cc_sei_hdr_h264[NI_CC_SEI_HDR_H264_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x06,   // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0 + 11,   // payLoadSize= ui16Len + 11; to be set (index 6)
    0xb5,     //  itu_t_t35_country_code =181 (North America)
    0x00,
    0x31,   //  itu_t_t35_provider_code = 49
    0x47, 0x41, 0x39,
    0x34,       // ATSC_user_identifier = "GA94"
    0x03,       // ATSC1_data_user_data_type_code=3
    0 | 0xc0,   // (ui16Len/3) | 0xc0 (to be set; index 15) (each CC character
    //is 3 bytes)
    0xFF   // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_h264[NI_HDR10P_SEI_HDR_H264_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x06,   // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0x00,   // payLoadSize; to be set (index 6)
    0xb5,   //  itu_t_t35_country_code =181 (North America)
    //0x00,
    //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
    //0x00,
    //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
    // payLoadSize count starts from itu_t_t35_provider_code and goes until
    // and including trailer
};

static uint8_t g_sei_trailer[NI_CC_SEI_TRAILER_LEN] = {
    0xFF,   // marker_bits = 255
    0x80   // RBSP trailing bits - rbsp_stop_one_bit and 7 rbsp_alignment_zero_bit
};

#define NI_XCODER_FAILURES_MAX 25

//Following macros will only check for critical failures.
//After the macro runs, rc should contain the status of the last command sent
//For non-critical failures, it is assumed in the code that eventually after enough retries, a command will succeed.
//Invalid parameters or resource busy do not account for failures that cause error count to be incremented so they can be retried indefinitely
#ifdef _WIN32
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)                    \
    {                                                                          \
        ni_session_stats_t err_rc_info = {0};                                  \
        int err_rc =                                                           \
            ni_query_session_stats(ctx, type, &err_rc_info, rc, opcode);       \
        if (err_rc != NI_RETCODE_SUCCESS)                                      \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "PANIC!!!! - Query for stats failed. What to do?\n");       \
        }                                                                      \
        rc = err_rc_info.ui32LastTransactionCompletionStatus;                  \
        int tmp_rc = NI_RETCODE_FAILURE;                                       \
        if ((ctx->rc_error_count >= NI_XCODER_FAILURES_MAX) ||                 \
            (tmp_rc =                                                          \
                 ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)))  \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "Persistent failures detected, %s() line-%d: "              \
                   "session_no 0x%x sess_err_count %u inst_err_no %u "         \
                   "rc_error_count: %d\n",                                     \
                   __func__, __LINE__, *inst_id, err_rc_info.ui16ErrorCount,   \
                   err_rc_info.ui32LastErrorStatus, ctx->rc_error_count);      \
            rc = tmp_rc;                                                       \
            LRETURN;                                                           \
        }                                                                      \
        if (type == NI_DEVICE_TYPE_AI && err_rc_info.ui32LastErrorStatus != 0) \
        {                                                                      \
            rc = NI_RETCODE_FAILURE;                                           \
        }                                                                      \
    }

#define CHECK_ERR_RC2(ctx, rc, info, opcode, type, hw_id, inst_id)             \
    {                                                                          \
        (rc) = (info).ui32LastTransactionCompletionStatus;                     \
        if ((info).ui16ErrorCount || (rc))                                     \
            (ctx)->rc_error_count++;                                           \
        else                                                                   \
            (ctx)->rc_error_count = 0;                                         \
        int tmp_rc = NI_RETCODE_FAILURE;                                       \
        if ((info).ui16ErrorCount ||                                           \
            (ctx)->rc_error_count >= NI_XCODER_FAILURES_MAX ||                 \
            (tmp_rc = ni_nvme_check_error_code(rc, (opcode), (type), (hw_id),  \
                                               (inst_id))))                    \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "Persistent failures detected, %s() line-%d: session_no "   \
                   "0x%x sess_err_no %u "                                      \
                   "inst_err_no %u rc_error_count: %d\n",                      \
                   __func__, __LINE__, *(inst_id), (info).ui16ErrorCount,      \
                   (info).ui32LastTransactionCompletionStatus,                 \
                   (ctx)->rc_error_count);                                     \
            (rc) = tmp_rc;                                                     \
            LRETURN;                                                           \
        }                                                                      \
    }

#elif __linux__ || __APPLE__
static struct stat g_nvme_stat = {0};

#ifdef XCODER_SELF_KILL_ERR
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)                    \
    {                                                                          \
        ni_session_stats_t err_rc_info = {0};                                  \
        int err_rc =                                                           \
            ni_query_session_stats(ctx, type, &err_rc_info, rc, opcode);       \
        if (err_rc != NI_RETCODE_SUCCESS)                                      \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "PANIC!!!! - Query for stats failed. What to do?\n");       \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
        rc = err_rc_info.ui32LastTransactionCompletionStatus;                  \
        int tmp_rc = NI_RETCODE_FAILURE;                                       \
        if ((ctx->rc_error_count >= NI_XCODER_FAILURES_MAX) ||                 \
            (tmp_rc =                                                          \
                 ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)))  \
        {                                                                      \
            ni_log(                                                            \
                NI_LOG_ERROR,                                                  \
                "Persistent failures detected, %s() line-%d: session_no 0x%x " \
                "sess_err_count %u inst_err_no %u rc_error_count: %d\n",       \
                __func__, __LINE__, *inst_id, err_rc_info.ui16ErrorCount,      \
                err_rc_info.ui32LastErrorStatus, ctx->rc_error_count);         \
            rc = tmp_rc;                                                       \
            kill(getpid(), SIGTERM);                                           \
        }                                                                      \
    }

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id)      \
    {                                                                          \
        (rc) = (err_rc_info).ui32LastTransactionCompletionStatus;              \
        if ((err_rc_info).ui16ErrorCount || rc)                                \
            (ctx)->rc_error_count++;                                           \
        else                                                                   \
            (ctx)->rc_error_count = 0;                                         \
        if ((err_rc_info).ui16ErrorCount ||                                    \
            (ctx)->rc_error_count >= NI_XCODER_FAILURES_MAX ||                 \
            ni_nvme_check_error_code((rc), (opcode), (type), (hw_id),          \
                                     (inst_id)))                               \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "Terminating due to persistent failures, %s() line-%d: "    \
                   "session_no 0x%x "                                          \
                   "sess_err_no %u inst_err_no %u "                            \
                   "rc_error_count: %d\n",                                     \
                   __func__, __LINE__, *(inst_id),                             \
                   (err_rc_info).ui16ErrorCount,                               \
                   (err_rc_info).ui32LastTransactionCompletionStatus,          \
                   (ctx)->rc_error_count);                                     \
            kill(getpid(), SIGTERM);                                           \
        }                                                                      \
    }
#else
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)                    \
    {                                                                          \
        ni_session_stats_t err_rc_info = {0};                                  \
        int err_rc =                                                           \
            ni_query_session_stats(ctx, type, &err_rc_info, rc, opcode);       \
        if (err_rc != NI_RETCODE_SUCCESS)                                      \
        {                                                                      \
            ni_log(NI_LOG_ERROR,                                               \
                   "PANIC!!!! - Query for stats failed. What to do?\n");       \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
        (rc) = err_rc_info.ui32LastTransactionCompletionStatus;                \
        int tmp_rc = NI_RETCODE_FAILURE;                                       \
        if (((ctx)->rc_error_count >= NI_XCODER_FAILURES_MAX) ||               \
            (tmp_rc =                                                          \
                 ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)))  \
        {                                                                      \
            ni_log(                                                            \
                NI_LOG_ERROR,                                                  \
                "Persistent failures detected, %s() line-%d: session_no 0x%x " \
                "sess_err_count %u inst_err_no %u rc_error_count: %d\n",       \
                __func__, __LINE__, *(inst_id), err_rc_info.ui16ErrorCount,    \
                err_rc_info.ui32LastErrorStatus, (ctx)->rc_error_count);       \
            (rc) = tmp_rc;                                                     \
            LRETURN;                                                           \
        }                                                                      \
    }

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id)      \
    {                                                                          \
        (rc) = (err_rc_info).ui32LastTransactionCompletionStatus;              \
        if ((err_rc_info).ui16ErrorCount || (rc))                              \
            (ctx)->rc_error_count++;                                           \
        else                                                                   \
            (ctx)->rc_error_count = 0;                                         \
        int tmp_rc = NI_RETCODE_FAILURE;                                       \
        if ((err_rc_info).ui16ErrorCount ||                                    \
            (ctx)->rc_error_count >= NI_XCODER_FAILURES_MAX ||                 \
            (tmp_rc = ni_nvme_check_error_code((rc), (opcode), (type),         \
                                               (hw_id), (inst_id))))           \
        {                                                                      \
            ni_log(NI_LOG_ERROR, "Persistent failures detected, %s() line-%d: "\
                   "session_no 0x%x sess_err_no %u inst_err_no %u "            \
                   "rc_error_count: %d\n", __func__, __LINE__, *(inst_id),     \
                   (err_rc_info).ui16ErrorCount,                               \
                   (err_rc_info).ui32LastTransactionCompletionStatus,          \
                   (ctx)->rc_error_count);                                     \
            (rc) = tmp_rc;                                                     \
            LRETURN;                                                           \
        }                                                                      \
    }
#endif

#endif

#define CHECK_VPU_RECOVERY(ret)                                                \
    {                                                                          \
        if (NI_RETCODE_NVME_SC_VPU_RECOVERY == (ret))                          \
        {                                                                      \
            ni_log(NI_LOG_ERROR, "Error, vpu reset.\n");                       \
            (ret) = NI_RETCODE_ERROR_VPU_RECOVERY;                             \
            LRETURN;                                                           \
        }                                                                      \
    }

// create folder bearing the card name (nvmeX) if not existing
// start working inside this folder: nvmeX
// find the earliest saved and/or non-existing stream folder and use it as
// the pkt saving destination; at most 128 such folders to be checked/created;
// folder name is in the format of: streamY, where Y is [1, 128]
static void decoder_dump_dir_open(ni_session_context_t *p_ctx)
{
#ifdef _WIN32
#elif __linux__ || __APPLE__
    FILE *fp;
    char dir_name[128] = {0};
    char file_name[512] = {0};
    ni_device_context_t *p_device_context;
    DIR *dir;
    struct dirent *stream_folder;
    int curr_stream_idx = 0;
    int earliest_stream_idx = 0;
    int max_exist_idx = 0;
    time_t earliest_time = 0;
    struct stat file_stat;

    p_device_context =
        ni_rsrc_get_device_context(p_ctx->device_type, p_ctx->hw_id);
    if (!p_device_context)
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
            ni_log(NI_LOG_ERROR, "Error create folder %s, errno %d\n", dir_name,
                           NI_ERRNO);
        } else
        {
            ni_log(NI_LOG_DEBUG, "Created pkt folder for: %s\n", dir_name);
        }
    }

    if (NULL == (dir = opendir(dir_name)))
    {
        ni_log(NI_LOG_ERROR, "Error %d: failed to open directory %s\n",
                       NI_ERRNO, dir_name);
    } else
    {
        // have a quick first pass of streamY folders, and if existing Y < 128,
        // create a new folder Y+1 directly without checking existing ones'
        // content
        while ((stream_folder = readdir(dir)))
        {
            if (!strncmp(stream_folder->d_name, "stream", strlen("stream")))
            {
                curr_stream_idx =
                    atoi(&(stream_folder->d_name[strlen("stream")]));
                if (curr_stream_idx > 0)
                {
                    if (curr_stream_idx > max_exist_idx)
                    {
                        max_exist_idx = curr_stream_idx;
                    }
                    if (NI_MAX_CONTEXTS_PER_HW_INSTANCE == curr_stream_idx)
                    {
                        break;
                    }
                }
            }
        }

        // if less than 128 streams created then create a new one, otherwise have
        // to pick the stream folder that has the earliest modified file which
        // is most likely done by finished session.
        if (max_exist_idx < NI_MAX_CONTEXTS_PER_HW_INSTANCE)
        {
            curr_stream_idx = max_exist_idx + 1;
        } else
        {
            rewinddir(dir);
            while ((stream_folder = readdir(dir)))
            {
                // go through each of these streamY folders and get modified
                // time of the first pkt-* file to simplify the searching
                if (!strncmp(stream_folder->d_name, "stream", strlen("stream")))
                {
                    snprintf(file_name, sizeof(file_name), "%s/%s/pkt-0001.bin",
                             dir_name, stream_folder->d_name);

                    curr_stream_idx =
                        atoi(&(stream_folder->d_name[strlen("stream")]));

                    if (curr_stream_idx > 0 && 0 == access(file_name, F_OK))
                    {
                        // just take pkt-0001 file timestamp to simplify search
                        if (stat(file_name, &file_stat))
                        {
                            ni_log(NI_LOG_ERROR, "Error %d: failed to stat file %s\n",
                                           NI_ERRNO,
                                           file_name);
                        } else
                        {
                            if (0 == earliest_stream_idx ||
                                file_stat.st_mtime < earliest_time)
                            {
                                earliest_stream_idx = curr_stream_idx;
                                earliest_time = file_stat.st_mtime;
                            }
                        }
                    }   // check first file in streamX
                }       // go through each streamX folder
            }           // read all files in nvmeY

            curr_stream_idx = earliest_stream_idx;

            // set the access/modified time of chosen pkt file to NOW so its
            // stream folder won't be taken by other sessions.
            snprintf(file_name, sizeof(file_name), "%s/stream%03d/pkt-0001.bin",
                     dir_name, curr_stream_idx);
            if (utime(file_name, NULL))
            {
                ni_log(NI_LOG_ERROR, "Error utime %s\n", file_name);
            }
        }   // 128 streams in nvmeY already
        closedir(dir);
    }

    snprintf(p_ctx->stream_dir_name, sizeof(p_ctx->stream_dir_name),
             "%s/stream%03d", dir_name, curr_stream_idx);

    if (0 != access(p_ctx->stream_dir_name, F_OK))
    {
        if (0 != mkdir(p_ctx->stream_dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            ni_log(NI_LOG_ERROR, "Error create stream folder %s, errno %d\n",
                           p_ctx->stream_dir_name, NI_ERRNO);
        } else
        {
            ni_log(NI_LOG_DEBUG, "Created stream sub folder: %s\n",
                           p_ctx->stream_dir_name);
        }
    } else
    {
        ni_log(NI_LOG_DEBUG, "Reusing stream sub folder: %s\n",
                       p_ctx->stream_dir_name);
    }

    flock(p_device_context->lock, LOCK_UN);
    ni_rsrc_free_device_context(p_device_context);

    snprintf(file_name, sizeof(file_name), "%s/process_session_id.txt",
             p_ctx->stream_dir_name);

    fp = fopen(file_name, "wb");
    if (fp)
    {
        char number[64] = {'\0'};
        ni_log(NI_LOG_DEBUG, "Decoder pkt dump log created: %s\n", file_name);
        snprintf(number, sizeof(number), "proc id: %ld\nsession id: %u\n",
                 (long)getpid(), p_ctx->session_id);
        fwrite(number, strlen(number), 1, fp);
        fclose(fp);
    } else
    {
        ni_log(NI_LOG_ERROR, "Error create decoder pkt dump log: %s\n", file_name);
    }
#endif
}

// check if fw_rev is higher than the specified
static uint32_t is_fw_rev_higher(ni_session_context_t* p_ctx, uint8_t major, uint8_t minor)
{
    if ((p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] > major) ||
        ((p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] == major) &&
         (p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX] > minor)))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*!******************************************************************************
 *  \brief  Open a xcoder decoder instance
 *
 *  \param
 *
 *  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_xcoder_params_t *p_param = NULL;
  uint32_t buffer_size = 0;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_DECODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;
    p_ctx->session_timestamp = 0;
    p_ctx->is_dec_pkt_512_aligned = 0;
    p_ctx->p_all_zero_buf = NULL;
    memset(p_ctx->pkt_custom_sei_set, 0, NI_FIFO_SZ * sizeof(ni_custom_sei_set_t *));

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %d: %s() alloc decoder all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        ni_aligned_free(p_ctx->p_all_zero_buf);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_DECODER, ni_htonl(p_ctx->codec_format), p_ctx->hw_action);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp =
        ni_htonll(((ni_session_stats_t *)p_buffer)->ui64Session_timestamp);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log(NI_LOG_DEBUG, "Decoder open session ID:0x%x, timestamp:%" PRIu64 "\n",
                   p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_DEBUG, "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW version is higher than major 6 minor 1
    if (is_fw_rev_higher(p_ctx, (uint8_t)'6', (uint8_t)'1'))
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_log(NI_LOG_DEBUG, "%s sw_version major %c minor %c fw_rev major %c minor %c\n", __func__,
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX]);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

    //VP9 requires a scaler session to be opened internally and attached as
    //well
    if(p_ctx->codec_format == NI_CODEC_FORMAT_VP9)
    {
        ni_log(NI_LOG_DEBUG, "Adding scaling session to Vp9 decoder\n");
        ui32LBA = OPEN_ADD_CODEC(NI_DEVICE_TYPE_SCALER, ni_htonl(NI_SCALER_OPCODE_SCALE), ni_htons(p_ctx->session_id));
        retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                       p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_open,
                       p_ctx->device_type, p_ctx->hw_id,
                       &(p_ctx->session_id));
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log(NI_LOG_ERROR, "ERROR couldn't add vp9 scaler to decoding session\n");
          ni_log(NI_LOG_ERROR,
                 "ERROR %s(): p_ctx->device_handle=%" PRIx64 ", "
                 "p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
                 __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
                 p_ctx->session_id);
          ni_decoder_session_close(p_ctx, 0);
          LRETURN;
        }
    }

    ni_log(NI_LOG_DEBUG,
           "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
           "p_ctx->session_id=%d\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
  }

  //start dec config
  retval = ni_config_instance_set_decoder_params(p_ctx, 0);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: calling ni_config_instance_set_decoder_params(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    //ni_decoder_session_close(p_ctx, 0); //close happens on above
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  //end dec config

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = NI_NOPTS_VALUE;
  p_ctx->last_dts = NI_NOPTS_VALUE;
  p_ctx->last_dts_interval = 0;
  p_ctx->pts_correction_last_dts = INT64_MIN;
  p_ctx->pts_correction_last_pts = INT64_MIN;

  //p_ctx->p_leftover = malloc(NI_MAX_PACKET_SZ * 2);
  p_ctx->p_leftover = malloc(p_ctx->max_nvme_io_size * 2);
  if (!p_ctx->p_leftover)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Cannot allocate leftover buffer.\n",
             __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      //ni_decoder_session_close(p_ctx, 0);
      LRETURN;
  }

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "dec_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "dec_dts");

  if (p_ctx->p_session_config)
  {
      p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
      ni_params_print(p_param);
  }

  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->actual_video_width = 0;

  ni_log(NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

  if (p_param && p_param->dec_input_params.nb_save_pkt)
  {
      decoder_dump_dir_open(p_ctx);
  }

#ifdef XCODER_DUMP_DATA
  char dir_name[256] = {0};

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-dec-fme", (long)getpid(),
           p_ctx->session_id);
  DIR *dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log(NI_LOG_DEBUG, "Decoder frame dump dir created: %s\n", dir_name);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  send a keep alive message to firmware
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_send_session_keep_alive(uint32_t session_id, ni_device_handle_t device_handle, ni_event_handle_t event_handle, void *p_data)
{
    ni_retcode_t retval;
    uint32_t ui32LBA = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);
    if (NI_INVALID_SESSION_ID == session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Invalid session ID!, return\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    if (NI_INVALID_DEVICE_HANDLE == device_handle)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): xcoder instance id < 0, return\n",
              __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }
 
    ui32LBA = CONFIG_SESSION_KeepAlive_W(session_id);
    if (ni_nvme_send_write_cmd(device_handle, event_handle, p_data,
                               NI_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): device_handle=%" PRIx64 " , "
               "session_id=%d\n", __func__, (int64_t)device_handle, session_id);
        retval = NI_RETCODE_FAILURE;
    }
    else
    {  
        ni_log(NI_LOG_TRACE, "SUCCESS %s(): device_handle=%" PRIx64 " , "
               "session_id=%d\n", __func__, (int64_t)device_handle, session_id);
        retval = NI_RETCODE_SUCCESS;
    }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Flush decoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_flush(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s(): xcoder instance id < 0, return\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->ready_to_close = 1;
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): success exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder decoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int counter = 0;
  int ret = 0;
  int i;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: malloc decoder close data buffer failed\n", NI_ERRNO);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log(NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): command failed!\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          break;
      } else
    {
      //Close should always succeed
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
	    break;
    }
    /*
    else if (*((ni_retcode_t *)p_buffer) == RETCODE_SUCCESS)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "%s(): wait for close\n", __func__);
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
    retry++;
  }

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);
    ni_aligned_free(p_ctx->p_leftover);

    if (p_ctx->pts_table)
    {
        ni_queue_free(&p_ctx->pts_table->list, p_ctx->buffer_pool);
        ni_aligned_free(p_ctx->pts_table);
        ni_log(NI_LOG_DEBUG, "ni_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
        ni_queue_free(&p_ctx->dts_queue->list, p_ctx->buffer_pool);
        ni_aligned_free(p_ctx->dts_queue);
        ni_log(NI_LOG_DEBUG, "ni_timestamp_done: success\n");
    }

    ni_buffer_pool_free(p_ctx->buffer_pool);
    p_ctx->buffer_pool = NULL;

    ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
    p_ctx->dec_fme_buf_pool = NULL;

    for (i = 0; i < NI_FIFO_SZ; i++)
    {
        ni_aligned_free(p_ctx->pkt_custom_sei_set[i]);
    }

    ni_log(NI_LOG_DEBUG, "%s():  CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a video p_packet to decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_write(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  uint32_t sent_size = 0;
  uint32_t packet_size = 0;
  uint32_t write_size_bytes = 0;
  uint32_t actual_sent_size = 0;
  uint32_t pkt_chunk_count = 0;
  int current_pkt_size;
  int retval = NI_RETCODE_SUCCESS;
  ni_xcoder_params_t *p_param;
  ni_instance_buf_info_t buf_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;
#ifdef MEASURE_LATENCY
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_packet))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if ((NI_INVALID_SESSION_ID == p_ctx->session_id))
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_packet->dts);
  }
#endif

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  packet_size = p_packet->data_len;
  current_pkt_size = packet_size;

  for (;;)
  {
    query_retry++;
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                        NI_DEVICE_TYPE_DECODER, &buf_info);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);


    if (p_ctx->biggest_bitstream_buffer_allocated < buf_info.buf_avail_size)
    {
      p_ctx->biggest_bitstream_buffer_allocated = buf_info.buf_avail_size;
    }
    if (p_ctx->biggest_bitstream_buffer_allocated < packet_size)
    {
      // Reallocate decoder bitstream buffers to accomodate.
      retval = ni_config_instance_set_decoder_params(p_ctx, packet_size);
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log(NI_LOG_ERROR, "%s(): failed to reallocate bitstream\n", __FUNCTION__);
          LRETURN;
      }
      query_retry--;
      continue;
    }

    if (NI_RETCODE_SUCCESS != retval ||
        buf_info.buf_avail_size < packet_size)
    {
        ni_log(NI_LOG_DEBUG,
               "Warning dec write query fail rc %d or available "
               "buf size %u < pkt size %u , retry: %d\n",
               retval, buf_info.buf_avail_size, packet_size, query_retry);
        if (query_retry > NI_MAX_TX_RETRIES)
        {
            p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
            retval = NI_RETCODE_SUCCESS;
            LRETURN;
        }
      ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
      ni_usleep(100);
      ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "Info dec write query success, available buf "
                     "size %u >= pkt size %u !\n",
                     buf_info.buf_avail_size, packet_size);
      break;
    }
  }

  //Configure write size for the buffer
  retval = ni_config_instance_set_write_len(p_ctx, NI_DEVICE_TYPE_DECODER,
		  	  	  	  	  	  	  	  	  packet_size);
  CHECK_ERR_RC(p_ctx, retval, nvme_config_xcoder_config_set_write_legth,
				   p_ctx->device_type, p_ctx->hw_id,
				   &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): config pkt size command failed\n",
             __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  //check for start of stream flag
  if (p_packet->start_of_stream)
  {
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Failed to send SOS.\n", __func__);
        LRETURN;
    }

    p_packet->start_of_stream = 0;
  }

  if (p_packet->p_data)
  {
      ni_log(NI_LOG_DEBUG,
             "%s() had data to send: packet_size=%u, "
             "p_packet->sent_size=%d, p_packet->data_len=%u, "
             "p_packet->start_of_stream=%u, p_packet->end_of_stream=%u, "
             "p_packet->video_width=%u, p_packet->video_height=%u\n",
             __func__, packet_size, p_packet->sent_size, p_packet->data_len,
             p_packet->start_of_stream, p_packet->end_of_stream,
             p_packet->video_width, p_packet->video_height);

      uint32_t send_count = 0;
      uint8_t *p_data = (uint8_t *)p_packet->p_data;
      // Note: session status is NOT reset but tracked between send
      // and recv to catch and recover from a loop condition
      // p_ctx->status = 0;

    if (packet_size % NI_MEM_PAGE_ALIGNMENT) //packet size, already aligned
    {
        packet_size = ( (packet_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, packet_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // reset session status after successful send
    p_ctx->status = 0;

    sent_size = p_packet->data_len;
    p_packet->data_len = 0;

    if (p_param->dec_input_params.nb_save_pkt)
    {
        char dump_file[512] = {0};
        long curr_pkt_num =
            ((long)p_ctx->pkt_num % p_param->dec_input_params.nb_save_pkt) + 1;
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
    retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Failed to send EOS.\n", __func__);
        LRETURN;
    }

    p_packet->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }

  if (p_ctx->is_dec_pkt_512_aligned)
  {
    // save NI_MAX_DEC_REJECT pts values and their corresponding number of 512 aligned data
    if (p_ctx->is_first_frame && (p_ctx->pkt_index != -1))
    {
      p_ctx->pts_offsets[p_ctx->pkt_index] = p_packet->pts;
      p_ctx->flags_array[p_ctx->pkt_index] = p_packet->flags;
      p_ctx->pkt_offsets_index[p_ctx->pkt_index] = current_pkt_size/512; // assuming packet_size is 512 aligned
      ni_log(NI_LOG_DEBUG,
             "%s:  pkt_index %d pkt_offsets_index %" PRIu64
             " pts_offsets %" PRId64 "\n",
             __func__, p_ctx->pkt_index,
             p_ctx->pkt_offsets_index[p_ctx->pkt_index],
             p_ctx->pts_offsets[p_ctx->pkt_index]);
      p_ctx->pkt_index ++;
      if (p_ctx->pkt_index >= NI_MAX_DEC_REJECT)
      {
          ni_log(NI_LOG_DEBUG,
                 "%s(): more than NI_MAX_DEC_REJECT frames are rejected by the "
                 "decoder. Increase NI_MAX_DEC_REJECT is required or default "
                 "gen pts values will be used !\n",
                 __func__);
          p_ctx->pkt_index = -1;   // signaling default pts gen
      }
    }
  }
  else
  {
    p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->pts;
    p_ctx->flags_array[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->flags;
    if (p_ctx->pkt_index == 0)
    {
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = 0;
      /* minus 1 here. ffmpeg parses the msb 0 of long start code as the last packet's payload for hevc bitstream (hevc_parse).
       * move 1 byte forward on all the pkt_offset so that frame_offset coming from fw can fall into the correct range. */
      //p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = current_pkt_size - 1;
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = current_pkt_size;
      ni_log(NI_LOG_DEBUG,
             "%s: (first packet) pkt_index %d i %u "
             "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index %" PRIu64
             " pts_offsets %" PRId64 "\n",
             __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
             p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);
    }
    else
    {
      // cumulate sizes to correspond to FW offsets
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ];
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ] + current_pkt_size;
      ni_log(NI_LOG_DEBUG,
             "%s: pkt_index %d i %u pkt_offsets_index_min "
             "%" PRIu64 " pkt_offsets_index %" PRIu64 " pts_offsets %" PRId64
             "\n",
             __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
             p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);

      //Wrapping 32 bits since FW send u32 wrapped values
      if (p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] > 0xFFFFFFFF)
      {
        p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] - (0x100000000);
        p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] + current_pkt_size;
        ni_log(NI_LOG_DEBUG,
               "%s: (wrap) pkt_index %d i %u "
               "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index %" PRIu64
               " pts_offsets %" PRId64 "\n",
               __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
               p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
               p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
               p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);
      }
    }

    /* if this wrap-around pkt_offset_index spot is about to be overwritten, free the previous one. */
    ni_aligned_free(p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ]);

    if (p_packet->p_custom_sei_set)
    {
      p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ] = malloc(sizeof(ni_custom_sei_set_t));
      if (p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ])
      {
        ni_custom_sei_set_t *p_custom_sei_set = p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ];
        memcpy(p_custom_sei_set, p_packet->p_custom_sei_set, sizeof(ni_custom_sei_set_t));
      }
      else
      {
        /* warn and lose the sei data. */
        ni_log(NI_LOG_ERROR,
               "Error %s: failed to allocate custom SEI buffer for pkt.\n",
               __func__);
      }
    }
    else
    {
      p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ] = NULL;
    }

    p_ctx->pkt_index++;
  }

  retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_packet->dts, 0);
  if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR %s(): ni_timestamp_register() for dts returned %d\n",
             __func__, retval);
  }
END:

    if (NI_RETCODE_SUCCESS == retval)
    {
        ni_log(NI_LOG_TRACE,
               "%s(): exit: packets: %" PRIu64 " offset %" PRIx64 ""
               "sent_size = %u, status=%d\n",
               __func__, p_ctx->pkt_num, (uint64_t)p_packet->pos, sent_size,
               p_ctx->status);
        return sent_size;
    } else
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): exit: returnErr: %d, p_ctx->status: %d\n", __func__,
               retval, p_ctx->status);
        return retval;
    }
}

static int64_t guess_correct_pts(ni_session_context_t* p_ctx, int64_t reordered_pts, int64_t dts)
{
  int64_t pts = NI_NOPTS_VALUE;
  if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_dts += dts <= p_ctx->pts_correction_last_dts;
    p_ctx->pts_correction_last_dts = dts;
    ni_log(NI_LOG_DEBUG,
           "%s: pts_correction_last_dts %" PRId64 " "
           "pts_correction_num_faulty_dts %d\n",
           __func__, p_ctx->pts_correction_last_dts,
           p_ctx->pts_correction_num_faulty_dts);
  }
  else if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_dts = reordered_pts;
    ni_log(NI_LOG_DEBUG, "%s: pts_correction_last_dts %" PRId64 "\n", __func__,
           p_ctx->pts_correction_last_dts);
  }
  if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_pts += reordered_pts <= p_ctx->pts_correction_last_pts;
    p_ctx->pts_correction_last_pts = reordered_pts;
    ni_log(NI_LOG_DEBUG,
           "%s: pts_correction_last_pts %" PRId64 " "
           "pts_correction_num_faulty_pts %d\n",
           __func__, p_ctx->pts_correction_last_pts,
           p_ctx->pts_correction_num_faulty_pts);
  }
  else if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_pts = dts;
    ni_log(NI_LOG_DEBUG, "%s: pts_correction_last_pts %" PRId64 "\n", __func__,
           p_ctx->pts_correction_last_pts);
  }
  if ((p_ctx->pts_correction_num_faulty_pts<=p_ctx->pts_correction_num_faulty_dts || dts == NI_NOPTS_VALUE)
     && reordered_pts != NI_NOPTS_VALUE)
  {
    pts = reordered_pts;
    ni_log(NI_LOG_DEBUG, "%s: (reordered_pts) pts %" PRId64 "\n", __func__,
           pts);
  }
  else
  {
      if ((NI_NOPTS_VALUE == reordered_pts) ||
          (NI_NOPTS_VALUE == p_ctx->last_pts) || (dts >= p_ctx->last_pts))
      {
          pts = dts;
      } else
      {
          pts = reordered_pts;
      }

    ni_log(NI_LOG_DEBUG, "%s: (dts) pts %" PRId64 "\n", __func__, pts);
  }
  return pts;
}

static int rotated_array_binary_search(uint64_t *lefts, uint64_t *rights,
                                       int32_t size, uint64_t target)
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
            } else
            {
                // Rotation in (lo, mid)
                lo = mid + 1;
            }
        } else
        {
            if (rights[mid] < target && target < rights[hi])
            {
                // Elements are all monotonous in (lo, mid)
                lo = mid + 1;
            } else
            {
                // Rotation in (lo, mid)
                hi = mid - 1;
            }
        }
    }

    return -1;
}

/*!******************************************************************************
 *  \brief  Retrieve a YUV p_frame from decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  ni_instance_mgr_stream_info_t data = { 0 };
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t *p_data_buffer;
  int i = 0;
  int is_planar;
  int retval = NI_RETCODE_SUCCESS;
  int metadata_hdr_size = NI_FW_META_DATA_SZ - NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
  int sei_size = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  int keep_processing = 1;
  ni_instance_buf_info_t buf_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;
  unsigned int bytes_read_so_far = 0;
  int query_type = INST_BUF_INFO_RW_READ;
#ifdef MEASURE_LATENCY 
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_frame))
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): xcoder instance id < 0, return\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.
  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
      p_frame->data_len[2] + metadata_hdr_size;
  ni_log(NI_LOG_DEBUG, "Total bytes to read %u, low_delay %u\n",
         total_bytes_to_read, p_ctx->decoder_low_delay);

  if (p_ctx->decoder_low_delay > 0 && !p_ctx->ready_to_close)
  {
      ni_log(NI_LOG_DEBUG, "frame_num = %" PRIu64 ", pkt_num = %" PRIu64 "\n",
             p_ctx->frame_num, p_ctx->pkt_num);
      if (p_ctx->frame_num >= p_ctx->pkt_num)
      {
          //nothing to query, leave
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
      }
      query_type = INST_BUF_INFO_RW_READ_BUSY;
  }
  for (;;)
  {
    query_retry++;
    retval = ni_query_instance_buf_info(p_ctx, query_type,
                                        NI_DEVICE_TYPE_DECODER, &buf_info);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_DEBUG, "Info query buf_info.size = %u\n",
                   buf_info.buf_avail_size);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_DEBUG, "Warning dec read query fail rc %d retry %d\n",
                     retval, query_retry);

      if (query_retry >= 1000)
      {
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
      ni_usleep(100);
      ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
    }
    else if (buf_info.buf_avail_size == metadata_hdr_size)
    {
      ni_log(NI_LOG_DEBUG, "Info only metadata hdr is available, seq change?\n");
      total_bytes_to_read = metadata_hdr_size;
      break;
    }
    else if (0 == buf_info.buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        ni_log(NI_LOG_DEBUG, "Info dec query, ready_to_close %u, query eos\n",
                       p_ctx->ready_to_close);
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (data.is_flushed)
        {
          ni_log(NI_LOG_DEBUG, "Info eos reached.\n");
          p_frame->end_of_stream = 1;
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
        }
      }

      ni_log(NI_LOG_TRACE, "Dec read available buf size == 0. retry=%d, eos=%d"
             "\n", query_retry, p_frame->end_of_stream);
      if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status &&
          query_retry < 1000 / 2)
      {
          ni_usleep(25);
          continue;
      } else
      {
        if(p_ctx->decoder_low_delay>0)
        {
            ni_log(NI_LOG_ERROR, "Warning: low_delay mode non sequential input? Disabling LD\n");
            p_ctx->decoder_low_delay = -1;
        }
        ni_log(NI_LOG_DEBUG, "Warning: dec read failed %d retries. rc=%d; eos=%d\n",
               query_retry, p_ctx->status, p_frame->end_of_stream);
        
        ni_log(NI_LOG_DEBUG, "Warning: dec read query failed %d retries. rc=%d"
               "\n", query_retry, retval);
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
        // We have to ensure there are adequate number of DTS for picture
        // reorder delay otherwise wait for more packets to be sent to decoder.
        ni_timestamp_table_t *p_dts_queue = p_ctx->dts_queue;
        if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
            !p_ctx->ready_to_close)
        {
            retval = NI_RETCODE_SUCCESS;
            ni_log(NI_LOG_DEBUG,
                   "At least %d packets should be sent before reading the "
                   "first frame!\n",
                   p_ctx->pic_reorder_delay + 1);
            LRETURN;
        }
      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_DEBUG, "Info dec YUV query, pic size %ux%u xfer frame size "
                       "%ux%u frame-rate %u is_flushed %u\n",
                       data.picture_width, data.picture_height,
                       data.transfer_frame_stride, data.transfer_frame_height,
                       data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->actual_video_width = data.picture_width;
        p_ctx->pixel_format = data.pix_format;
        is_planar = (p_ctx->pixel_format == NI_PIX_FMT_YUV420P) ||
            (p_ctx->pixel_format == NI_PIX_FMT_YUV420P10LE);
        p_ctx->bit_depth_factor = ni_get_bitdepth_factor_from_pixfmt(p_ctx->pixel_format);
        //p_ctx->bit_depth_factor = data.transfer_frame_stride / data.picture_width;
        p_ctx->is_first_frame = 1;

        ni_log(NI_LOG_DEBUG, "Info dec YUV, adjust frame size from %ux%u to "
                       "%ux%u format = %d\n", p_frame->video_width, p_frame->video_height,
                       p_ctx->active_video_width, p_ctx->active_video_height, p_ctx->pixel_format);

        ni_decoder_frame_buffer_free(p_frame);

        // set up decoder YUV frame buffer pool
        if (ni_dec_fme_buffer_pool_initialize(
              p_ctx, NI_DEC_FRAME_BUF_POOL_SIZE_INIT, p_ctx->actual_video_width,
              p_ctx->active_video_height,
              p_ctx->codec_format == NI_CODEC_FORMAT_H264,
              p_ctx->bit_depth_factor))
        {
            ni_log(NI_LOG_ERROR, "ERROR %s(): Cannot allocate fme buf pool.\n",
                   __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            ni_decoder_session_close(p_ctx, 0);
#ifdef XCODER_SELF_KILL_ERR
          // if need to terminate at such occasion when continuing is not
          // possible, trigger a codec closure
          ni_log(NI_LOG_ERROR, "Terminating due to unable to allocate fme buf "
                 "pool.\n");
          kill(getpid(), SIGTERM);
#endif
          LRETURN;
        }

        retval = ni_decoder_frame_buffer_alloc(
            p_ctx->dec_fme_buf_pool, p_frame, 1,   // get mem buffer
            p_ctx->actual_video_width, p_ctx->active_video_height,
            p_ctx->codec_format == NI_CODEC_FORMAT_H264,
            p_ctx->bit_depth_factor, is_planar);

        if (NI_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
            p_frame->data_len[2] + metadata_hdr_size;
        p_data_buffer = (uint8_t*) p_frame->p_buffer;

        // make sure we don't read more than available
        ni_log(NI_LOG_DEBUG, "Info dec buf size: %u YUV frame + meta-hdr size: %u "
                       "available: %u\n", p_frame->buffer_size,
                       total_bytes_to_read, buf_info.buf_avail_size);
      }
      break;
    }
  }

  ni_log(NI_LOG_DEBUG, "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  if (buf_info.buf_avail_size < total_bytes_to_read)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR %s() avaliable size(%u)"
             "less than needed (%u)\n",
             __func__, buf_info.buf_avail_size, total_bytes_to_read);
      abort();
  }

    read_size_bytes = buf_info.buf_avail_size;
    ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
    if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
    {
        read_size_bytes = ((read_size_bytes / NI_MEM_PAGE_ALIGNMENT) *
                           NI_MEM_PAGE_ALIGNMENT) +
            NI_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data_buffer, read_size_bytes, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    } else
    {
        // command issued successfully, now exit
        ni_metadata_dec_frame_t *p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                        p_frame->data_len[0] +
                                        p_frame->data_len[1] +
                                        p_frame->data_len[2]);

        if (buf_info.buf_avail_size != metadata_hdr_size)
        {
            sei_size = p_meta->sei_size;
        }
        total_bytes_to_read = total_bytes_to_read + sei_size;
        ni_log(NI_LOG_DEBUG, "decoder read success, size %d total_bytes_to_read "
                       "include sei %u sei_size %d\n",
                       retval, total_bytes_to_read, sei_size);
    }

  bytes_read_so_far = total_bytes_to_read ;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  rx_size = ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
  p_ctx->frame_pkt_offset = frame_offset;

  if (rx_size > 0)
  {
      ni_log(NI_LOG_DEBUG, "%s(): s-state %d first_frame %d\n", __func__,
             p_ctx->session_run_state, p_ctx->is_first_frame);
      if (ni_timestamp_get_with_threshold(
              p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
              XCODER_FRAME_OFFSET_DIFF_THRES, 0,
              p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
          if (p_ctx->last_dts != NI_NOPTS_VALUE && !p_ctx->ready_to_close)
          {
              p_ctx->pic_reorder_delay++;
              p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
              ni_log(NI_LOG_DEBUG, "Padding DTS: %" PRId64 "\n", p_frame->dts);
          } else
          {
              p_frame->dts = NI_NOPTS_VALUE;
          }
      }

      if (p_ctx->is_first_frame)
      {
          for (i = 0; i < p_ctx->pic_reorder_delay; i++)
          {
              if (p_ctx->last_pts == NI_NOPTS_VALUE &&
                  p_ctx->last_dts == NI_NOPTS_VALUE)
              {
                  // If the p_frame->pts is unknown in the very beginning we assume
                  // p_frame->pts == 0 as well as DTS less than PTS by 1000 * 1/timebase
                  if (p_frame->pts >= p_frame->dts &&
                      p_frame->pts - p_frame->dts < 1000)
                  {
                      break;
                  }
              }

              if (ni_timestamp_get_with_threshold(
                      p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                      XCODER_FRAME_OFFSET_DIFF_THRES,
                      p_ctx->frame_num % 500 == 0,
                      p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
              {
                  p_frame->dts = NI_NOPTS_VALUE;
              }
          }
          // Reset for DTS padding counting
          p_ctx->pic_reorder_delay = 0;
      }

    if (p_ctx->is_dec_pkt_512_aligned)
    {
      if (p_ctx->is_first_frame)
      {
        p_ctx->is_first_frame = 0;

        if (p_frame->dts == NI_NOPTS_VALUE)
        {
          p_frame->pts = NI_NOPTS_VALUE;
        }
        // if not a bitstream retrieve the pts of the frame corresponding to the first YUV output
        else if((p_ctx->pts_offsets[0] != NI_NOPTS_VALUE) && (p_ctx->pkt_index != -1))
        {
          ni_metadata_dec_frame_t* p_metadata =
                  (ni_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer +
                  p_frame->data_len[0] + p_frame->data_len[1] +
                  p_frame->data_len[2] + p_frame->data_len[3]);
          uint64_t num_fw_pkts =
              p_metadata->metadata_common.ui64_data.frame_offset / 512;
          ni_log(NI_LOG_DEBUG,
                 "%s: num_fw_pkts %" PRIu64 " frame_offset %" PRIu64 "\n",
                 __func__, num_fw_pkts,
                 p_metadata->metadata_common.ui64_data.frame_offset);
          int idx = 0;
          uint64_t cumul = p_ctx->pkt_offsets_index[0];
          bool bFound = (num_fw_pkts >= cumul);
          while (cumul < num_fw_pkts) // look for pts index
          {
              ni_log(NI_LOG_DEBUG, "%s: cumul %" PRIu64 "\n", __func__, cumul);
              if (idx == NI_MAX_DEC_REJECT)
              {
                  ni_log(NI_LOG_ERROR, "Invalid index computation > "
                         "NI_MAX_DEC_REJECT!\n");
                  break;
              } else
              {
              idx++;
              cumul += p_ctx->pkt_offsets_index[idx];
              ni_log(NI_LOG_DEBUG,
                     "%s: idx %d "
                     "pkt_offsets_index[idx] %" PRIu64 "\n",
                     __func__, idx, p_ctx->pkt_offsets_index[idx]);
              }
          }
          //if ((idx != NI_MAX_DEC_REJECT) && (idx >= 0))
          if ((idx != NI_MAX_DEC_REJECT) && bFound)
          {
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_frame->flags = p_ctx->flags_array[idx];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
            ni_log(NI_LOG_DEBUG,
                   "%s: (first frame) idx %d last_dts %" PRId64 ""
                   "dts %" PRId64 " last_pts %" PRId64 " pts %" PRId64 "\n",
                   __func__, idx, p_ctx->last_dts, p_frame->dts,
                   p_ctx->last_pts, p_frame->pts);
          } else if ((idx != NI_MAX_DEC_REJECT) &&
                     (p_ctx->session_run_state == SESSION_RUN_STATE_RESETTING))
          {
              ni_log(NI_LOG_DEBUG,
                     "%s(): session %u recovering and "
                     "adjusting ts.\n",
                     __func__, p_ctx->session_id);
              p_frame->pts = p_ctx->pts_offsets[idx];
              p_frame->flags = p_ctx->flags_array[idx];
              p_ctx->last_pts = p_frame->pts;
              p_ctx->last_dts = p_frame->dts;
              p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
          }
          else // use pts = 0 as offset
          {
            p_frame->pts = 0;
            ni_log(NI_LOG_DEBUG,
                   "%s: (zero default) dts %" PRId64 " pts "
                   "%" PRId64 "\n",
                   __func__, p_frame->dts, p_frame->pts);
          }
        }
        else
        {
          p_frame->pts = 0;
          ni_log(NI_LOG_DEBUG,
                 "%s:  (not bitstream) dts %" PRId64 " pts "
                 "%" PRId64 "\n",
                 __func__, p_frame->dts, p_frame->pts);
        }
      }
      else
      {
          int64_t pts_delta = p_frame->dts - p_ctx->last_dts;
          p_frame->pts = p_ctx->last_pts + pts_delta;

          ni_log(NI_LOG_DEBUG,
                 "%s:  (!is_first_frame idx) last_dts %" PRId64 ""
                 "dts %" PRId64 " pts_delta %" PRId64 " last_pts "
                 "%" PRId64 " pts %" PRId64 "\n",
                 __func__, p_ctx->last_dts, p_frame->dts, pts_delta,
                 p_ctx->last_pts, p_frame->pts);
      }
    }
    else
    {
        ni_log(NI_LOG_DEBUG, "%s: frame_offset %" PRIu64 "\n", __func__,
               frame_offset);
        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
        // search for the pkt_offsets of received frame according to frame_offset.
        // here we get the index(i) which promises (p_ctx->pkt_offsets_index_min[i] <= frame_offset && p_ctx->pkt_offsets_index[i] > frame_offset)
        // i = -1 if not found
        i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                        p_ctx->pkt_offsets_index, NI_FIFO_SZ,
                                        frame_offset);
        if (i >= 0)
        {
            p_frame->pts = p_ctx->pts_offsets[i];
            p_frame->flags = p_ctx->flags_array[i];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
            ni_log(NI_LOG_DEBUG,
                   "%s: (found pts) dts %" PRId64 " pts "
                   "%" PRId64 " frame_offset %" PRIu64 " i %d "
                   "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index "
                   "%" PRIu64 "\n",
                   __func__, p_frame->dts, p_frame->pts, frame_offset, i,
                   p_ctx->pkt_offsets_index_min[i],
                   p_ctx->pkt_offsets_index[i]);

            p_frame->p_custom_sei_set = p_ctx->pkt_custom_sei_set[i];
            p_ctx->pkt_custom_sei_set[i] = NULL;
        } else
        {
            //backup solution pts
            p_frame->pts = p_ctx->last_pts + (p_frame->dts - p_ctx->last_dts);
            ni_log(NI_LOG_ERROR,
                   "ERROR: NO pts found consider increasing NI_FIFO_SZ!\n");
            ni_log(NI_LOG_DEBUG,
                   "%s: (not found use default) dts %" PRId64 " pts %" PRId64
                   "\n",
                   __func__, p_frame->dts, p_frame->pts);
        }
    }
    p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts);
    p_ctx->last_pts = p_frame->pts;
    if (p_frame->dts != NI_NOPTS_VALUE && p_ctx->last_dts != NI_NOPTS_VALUE)
        p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    p_ctx->last_dts = p_frame->dts;
    ni_log(NI_LOG_DEBUG, "%s: (best_effort_timestamp) pts %" PRId64 "\n",
           __func__, p_frame->pts);
    p_ctx->frame_num++;

#ifdef XCODER_DUMP_DATA
    char dump_file[256];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-dec-fme/fme-%04ld.yuv",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);
    FILE *f = fopen(dump_file, "wb");
    fwrite(p_frame->p_buffer,
           p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
           1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log(NI_LOG_DEBUG, "%s(): received data: [0x%08x]\n", __func__, rx_size);
  ni_log(NI_LOG_DEBUG,
         "%s(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_DEBUG, "%s(): p_frame->data_len[0/1/2]=%u/%u/%u\n", __func__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
      ni_log(NI_LOG_DEBUG,
             "Decoder pts queue size = %u  dts queue size = %u\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
      // scan and clean up
      ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue,
                                p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,dLAT:%lu;\n", p_frame->dts,
             abs_time_ns - p_ctx->prev_read_frame_time,
             ni_lat_meas_q_check_latency((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                                         abs_time_ns, p_frame->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __func__, retval);
        return retval;
    } else
    {
        ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __func__, rx_size);
        return rx_size;
    }
}

/*!******************************************************************************
 *  \brief  Query current xcoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_xcoder_session_query(ni_session_context_t *p_ctx,
                            ni_device_type_t device_type)
{
  ni_instance_mgr_general_status_t data;
  int retval = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s(): device_type %d:%s; enter\n", __func__,
         device_type, device_type_str[device_type]);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

#if 1
  retval = ni_query_general_status(p_ctx, device_type, &data);
#else
  ni_log(NI_LOG_DEBUG, "%s(): query skipped\n", __func__);
#endif
  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->load_query.current_load = (uint32_t)data.process_load_percent;
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.total_contexts = (uint32_t)data.active_sub_instances_cnt;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.fw_share_mem_usage = (uint32_t)data.fw_share_mem_usage;
    p_ctx->load_query.fw_p2p_mem_usage = (uint32_t)data.fw_p2p_mem_usage;
    p_ctx->load_query.active_hwuploaders =
    (uint32_t)data.active_hwupload_sub_inst_cnt;
    ni_log(NI_LOG_DEBUG, "%s current_load:%u fw_model_load:%u "
           "total_contexts:%u fw_video_mem_usage:%u "
           "fw_share_mem_usage:%u fw_p2p_mem_usage:%u "
           "active_hwuploaders:%u\n", __func__,
           p_ctx->load_query.current_load, p_ctx->load_query.fw_model_load,
           p_ctx->load_query.total_contexts,
           p_ctx->load_query.fw_video_mem_usage,
           p_ctx->load_query.fw_share_mem_usage,
           p_ctx->load_query.fw_p2p_mem_usage,
           p_ctx->load_query.active_hwuploaders);
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Open a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/

ni_retcode_t ni_encoder_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t buffer_size = 0;
  ni_xcoder_params_t *p_param;
  void *p_buffer = NULL;
  ni_instance_buf_info_t buf_info = {0};
  uint32_t ui32LBA = 0;
  uint32_t max_cu_size;
  uint32_t block_size;

  if (!p_ctx || !p_ctx->p_session_config)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  ni_log(NI_LOG_TRACE, "%s(): enter hwframes = %d\n", __func__,
         p_param->hwframes);

  // calculate encoder ROI map size: each QP info takes 8-bit, represent 8 x 8
  // pixel block
  max_cu_size = (NI_CODEC_FORMAT_H264 == p_ctx->codec_format) ? 16 : 64;
  block_size =
      ((p_param->source_width + max_cu_size - 1) & (~(max_cu_size - 1))) *
      ((p_param->source_height + max_cu_size - 1) & (~(max_cu_size - 1))) /
      (8 * 8);
  p_ctx->roi_len = ((block_size + 63) & (~63));   // align to 64 bytes

  ni_log(NI_LOG_DEBUG, "%s(): finish init\n", __func__);

  //Check if there is an instance or we need a new one
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_ENCODER;
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
    p_ctx->ready_to_close = 0;
    p_ctx->auto_dl_handle = 0;
    //Sequence change tracking reated stuff
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->actual_video_width = 0;
    p_ctx->enc_pts_w_idx = 0;
    p_ctx->enc_pts_r_idx = 0;
    p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
    p_ctx->session_timestamp = 0;
    memset(p_ctx->pkt_custom_sei_set, 0, NI_FIFO_SZ * sizeof(ni_custom_sei_set_t *));
    memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               __func__, NI_ERRNO);
        ni_aligned_free(p_ctx->p_all_zero_buf);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_ENCODER,
                                 ni_htonl(p_ctx->codec_format), 0);
    ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer,
                          NI_DATA_BUFFER_LEN, ui32LBA);
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id =
        ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp =
        ni_htonll(((ni_session_stats_t *)p_buffer)->ui64Session_timestamp);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64 ", "
               "p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        ni_encoder_session_close(p_ctx, 0);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log(NI_LOG_DEBUG, "Encoder open session ID:0x%x timestamp:%" PRIu64 "\n",
                   p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_DEBUG, "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW version is higher than major 6 minor 1
    if (is_fw_rev_higher(p_ctx, (uint8_t)'6', (uint8_t)'1'))
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_log(NI_LOG_DEBUG, "%s sw_version major %c minor %c fw_rev major %c minor %c\n", __func__,
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX]);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        // For FW rev 61 or newer, initialize with the most current size
        p_ctx->meta_size = sizeof(ni_metadata_enc_bstream_t);
    }
    else
    {
        // For FW rev 60 or older, initialize with metadata size 32
        p_ctx->meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE;
    }

    ni_log(NI_LOG_DEBUG, "Open session completed\n");
  }

  if (p_ctx->hw_action == NI_CODEC_HW_ENABLE)
  {
      ni_device_capability_t sender_cap, receiver_cap;

      retval = ni_device_capability_query(p_ctx->sender_handle, &sender_cap);
      if (retval != NI_RETCODE_SUCCESS)
      {
          ni_log(NI_LOG_ERROR, "ERROR: ni_device_capability_query returned %d\n",
                         retval);
          LRETURN;
      }

      retval = ni_device_capability_query(p_ctx->blk_io_handle, &receiver_cap);

      if (retval != NI_RETCODE_SUCCESS)
      {
          ni_log(NI_LOG_ERROR, "ERROR: ni_device_capability_query returned %d\n",
                         retval);
          LRETURN;
      }

      for (uint8_t ui8Index = 0; ui8Index < 20; ui8Index++)
      {
          if (sender_cap.serial_number[ui8Index] !=
              receiver_cap.serial_number[ui8Index])
          {
              // QDFW-315 Autodownload
              p_ctx->auto_dl_handle = p_ctx->sender_handle;
              ni_log(NI_LOG_DEBUG, "Autodownload device handle set %p!\n",
                     p_ctx->auto_dl_handle);
              p_ctx->hw_action = NI_CODEC_HW_NONE;
              break;
          }
      }
  }

  retval = ni_config_instance_set_encoder_params(p_ctx);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: calling ni_config_instance_set_encoder_params(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    ni_encoder_session_close(p_ctx, 0);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "enc_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "enc_dts");

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
  for (;;)
  {
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                        NI_DEVICE_TYPE_ENCODER, &buf_info);

    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));

    if (NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT == retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: VPU_RSRC_INSUFFICIENT\n");
      LRETURN;
    }
    else if (buf_info.buf_avail_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "%s(): buf_avail_size %u\n", __func__,
               buf_info.buf_avail_size);
        break;
    }
    else
    {
        ni_usleep(1000);
    }
  }

  ni_log(NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

#ifdef XCODER_DUMP_DATA
  char dir_name[256] = {0};
  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-pkt", (long)getpid(),
           p_ctx->session_id);
  DIR *dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log(NI_LOG_DEBUG, "Encoder pkt dump dir created: %s\n", dir_name);
  }

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-fme", (long)getpid(),
           p_ctx->session_id);
  dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log(NI_LOG_DEBUG, "Encoder frame dump dir created: %s\n", dir_name);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Flush encoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_flush(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);
  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if(NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(), return\n", __func__);
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): success exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int counter = 0;
  int ret = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
             NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log(NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): command failed\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          break;
      } else
    {
      //Close should always succeed
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
	    break;
    }
    /*
    else if(((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "%s(): wait for close\n");
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
    retry++;
  }

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);

    //Sequence change related stuff cleanup here
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;
    //End of sequence change related stuff cleanup

    if (p_ctx->pts_table)
    {
        ni_queue_free(&p_ctx->pts_table->list, p_ctx->buffer_pool);
        ni_aligned_free(p_ctx->pts_table);
        ni_log(NI_LOG_DEBUG, "ni_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
        ni_queue_free(&p_ctx->dts_queue->list, p_ctx->buffer_pool);
        ni_aligned_free(p_ctx->dts_queue);
        ni_log(NI_LOG_DEBUG, "ni_timestamp_done: success\n");
    }

    ni_buffer_pool_free(p_ctx->buffer_pool);
    p_ctx->buffer_pool = NULL;

    int i;
    for (i = 0; i < NI_FIFO_SZ; i++)
    {
        ni_aligned_free(p_ctx->pkt_custom_sei_set[i]);
    }

    ni_log(NI_LOG_DEBUG, "%s(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

#ifdef XCODER_DUMP_ENABLED
  if (p_ctx->p_dump[0])
  {
    fclose(p_ctx->p_dump[0]);
    p_ctx->p_dump[0] = NULL;
  }
  if (p_ctx->p_dump[1])
  {
    fclose(p_ctx->p_dump[1]);
    p_ctx->p_dump[1] = NULL;
  }
#endif

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}


/*!******************************************************************************
 *  \brief  Send a YUV p_frame to encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_encoder_session_write(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  bool ishwframe = p_ctx->hw_action & NI_CODEC_HW_ENABLE;
  uint32_t size = 0;
  uint32_t metadata_size = NI_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t send_count = 0;
  uint32_t i = 0;
  uint32_t tx_size = 0, aligned_tx_size = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  int retval = 0;
  ni_instance_buf_info_t buf_info = { 0 };
#ifdef MEASURE_LATENCY
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter hw=%d\n", __func__, ishwframe);
  //niFrameSurface1_t* p_data3 = (niFrameSurface1_t*)((uint8_t*)p_frame->p_data[3]);
  ////p_data3->rsvd = p_meta->hwdesc->rsvd;
  //ni_log(NI_LOG_DEBUG, "%s:mar16 HW=%d ui16FrameIdx=%d i8InstID=%d device_handle=%d\n",
  //               ishwframe, p_data3->ui16FrameIdx, p_data3->i8InstID, p_data3->device_handle);
  //ni_log(NI_LOG_DEBUG, "%s:Jun 29 device_handle=%d\n", p_ctx->device_handle);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_frame->dts);
  }
#endif

  /*!********************************************************************/
  /*!************ Sequence Change related stuff *************************/
  //First check squence changed related stuff.
  //We need to record the current hight/width params if we didn't do it before:

  if( p_frame->video_height)
  {
    p_ctx->active_video_width = p_frame->data_len[0] / p_frame->video_height;
    p_ctx->active_video_height = p_frame->video_height;
  }
  else if (p_frame->video_width)
  {
    ni_log(NI_LOG_DEBUG, "WARNING: passed video_height is not valid!, return\n");
    p_ctx->active_video_height = p_frame->data_len[0] / p_frame->video_width;
    p_ctx->active_video_width = p_frame->video_width;
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed video_height and video_width are not valid!, return\n");
    retval = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }
  // record actual width (in pixels / without padding) for sequnce change detection
  p_ctx->actual_video_width = p_frame->video_width;

  /*!************ Sequence Change related stuff end*************************/
  /*!********************************************************************/

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + p_frame->extra_data_len;
  ni_log(NI_LOG_DEBUG,
         "%s: data_len[0] %u data_len[1] %u "
         "data_len[2] %u extra_data_len %u frame_size_bytes %u\n",
         __func__, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2], p_frame->extra_data_len, frame_size_bytes);

  // skip query write buffer because we just send EOS
  if (!p_frame->end_of_stream)
  {
      for (;;)
      {
          retval = ni_query_instance_buf_info(
              p_ctx, INST_BUF_INFO_RW_WRITE, NI_DEVICE_TYPE_ENCODER, &buf_info);
          CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                       p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
          CHECK_VPU_RECOVERY(retval);

          if (NI_RETCODE_SUCCESS != retval ||
              buf_info.buf_avail_size < frame_size_bytes)
          {
              ni_log(NI_LOG_TRACE,
                     "Enc write query retry %d. rc=%d. Available buf size %u < "
                     "frame size %u\n", retval, send_count,
                     buf_info.buf_avail_size, frame_size_bytes);
              // extend to 5000 retries for 8K encode on FPGA
              if (send_count >= NI_MAX_ENCODER_QUERY_RETRIES)
              {
                  ni_log(NI_LOG_ERROR,
                         "ERROR enc query buf info exceeding max retries: "
                         "%d, ",
                         NI_MAX_ENCODER_QUERY_RETRIES);
                  p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
                  retval = NI_RETCODE_SUCCESS;
                  LRETURN;
              }
              send_count++;
              ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
              ni_usleep(100);
              ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
          } else
          {
              ni_log(NI_LOG_DEBUG,
                     "Info enc write query success, available buf "
                     "size %u >= frame size %u !\n",
                     buf_info.buf_avail_size, frame_size_bytes);
              break;
          }
    }
  }

  // fill in metadata such as timestamp
  ni_metadata_enc_frame_t *p_meta =
      (ni_metadata_enc_frame_t *)((uint8_t *)p_frame->p_data[2 + ishwframe] +
                                  p_frame->data_len[2 + ishwframe]);
  if (p_meta) //When hwframe xcoding reaches eos, frame looks like swframe but no allocation for p_meta
  {
    p_meta->metadata_common.ui64_data.frame_tstamp = (uint64_t)p_frame->pts;

    p_meta->force_headers = 0;   // p_frame->force_headers not implemented/used
    p_meta->use_cur_src_as_long_term_pic =
        p_frame->use_cur_src_as_long_term_pic;
    p_meta->use_long_term_ref = p_frame->use_long_term_ref;

    ni_log(NI_LOG_DEBUG,
           "%s: p_meta "
           "use_cur_src_as_long_term_pic %d use_long_term_ref %d\n",
           __func__, p_meta->use_cur_src_as_long_term_pic,
           p_meta->use_long_term_ref);

    p_meta->frame_force_type_enable = p_meta->frame_force_type = 0;
    // frame type to be forced to is supposed to be set correctly
    // in p_frame->ni_pict_type
    if (1 == p_ctx->force_frame_type || p_frame->force_key_frame)
    {
      if (p_frame->ni_pict_type)
      {
        p_meta->frame_force_type_enable = 1;
        p_meta->frame_force_type = p_frame->ni_pict_type;
      }
      ni_log(NI_LOG_DEBUG,
             "%s(): ctx->force_frame_type"
             " %d frame->force_key_frame %d force frame_num %" PRIu64 ""
             " type to %d\n",
             __func__, p_ctx->force_frame_type, p_frame->force_key_frame,
             p_ctx->frame_num, p_frame->ni_pict_type);
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

    ni_log(
        NI_LOG_DEBUG,
        "%s(): %d.%u p_ctx->frame_num=%" PRIu64 ", "
        "p_frame->start_of_stream=%u, p_frame->end_of_stream=%u, "
        "p_frame->video_width=%u, p_frame->video_height=%u, pts=0x%08x 0x%08x, "
        "dts=0x%08x 0x%08x, sei_len=%u, roi size=%u avg_qp=%u reconf_len=%u "
        "force_pic_qp=%u use_cur_src_as_long_term_pic %u use_long_term_ref "
        "%u\n",
        __func__, p_ctx->hw_id, p_ctx->session_id, p_ctx->frame_num,
        p_frame->start_of_stream, p_frame->end_of_stream, p_frame->video_width,
        p_frame->video_height, (uint32_t)((p_frame->pts >> 32) & 0xFFFFFFFF),
        (uint32_t)(p_frame->pts & 0xFFFFFFFF),
        (uint32_t)((p_frame->dts >> 32) & 0xFFFFFFFF),
        (uint32_t)(p_frame->dts & 0xFFFFFFFF), p_meta->frame_sei_data_size,
        p_meta->frame_roi_map_size, p_meta->frame_roi_avg_qp,
        p_meta->enc_reconfig_data_size, p_meta->force_pic_qp_i,
        p_meta->use_cur_src_as_long_term_pic, p_meta->use_long_term_ref);
  }
  if (p_frame->start_of_stream)
  {
    //Send Start of stream p_config command here
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }

    p_frame->start_of_stream = 0;
  }

  // skip direct to send eos without sending the passed in p_frame as it's been sent already
  if (p_frame->end_of_stream)
  {
      retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
      CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);
      if (NI_RETCODE_SUCCESS != retval)
      {
          LRETURN;
      }

      p_frame->end_of_stream = 0;
      p_ctx->ready_to_close = 1;
  } else   //handle regular frame sending
  {
      retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue,
                                     p_frame->dts, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR %s(): "
                 "ni_timestamp_register() for dts returned: %d\n",
                 __func__, retval);
      }

      uint32_t ui32LBA =
          WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
      ni_log(NI_LOG_DEBUG,
             "Encoder session write: p_data = %p, p_frame->buffer_size "
             "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
             __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
             ui32LBA);
      sent_size = frame_size_bytes;
      if (sent_size % NI_MEM_PAGE_ALIGNMENT)
      {
          sent_size =
              ((sent_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) +
              NI_MEM_PAGE_ALIGNMENT;
      }

      retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_frame->p_buffer, sent_size, ui32LBA);
      CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write, p_ctx->device_type,
                   p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);
      if (retval < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          LRETURN;
      }

      p_ctx->frame_num++;
      size = frame_size_bytes;

#ifdef XCODER_DUMP_DATA
      char dump_file[256];
      snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-fme/fme-%04ld.yuv",
               (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

      FILE *f = fopen(dump_file, "wb");
      fwrite(p_frame->p_buffer,
             p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
             1, f);
      fflush(f);
      fclose(f);
#endif
  }

  retval = size;

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}


int ni_encoder_session_read(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  ni_instance_mgr_stream_info_t data = { 0 };
  uint32_t actual_read_size = 0;
  uint32_t to_read_size = 0;
  int size = 0;
  uint32_t query_return_size = 0;
  uint32_t partial_data = 0;
  uint8_t* p_data = NULL;
  static long long encq_count = 0LL;
  int retval = NI_RETCODE_SUCCESS;
  int query_retry = 0;
  int query_type = INST_BUF_INFO_RW_READ;
  uint32_t ui32LBA = 0;
  ni_metadata_enc_bstream_t *p_meta = NULL;
  ni_metadata_enc_bstream_rev61_t *p_meta_rev61 = NULL;
  ni_instance_buf_info_t buf_info = { 0 };
#ifdef MEASURE_LATENCY
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx || !p_packet || !p_packet->p_data)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ni_log(NI_LOG_DEBUG, "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 "\n",
                 p_ctx->frame_num, p_ctx->pkt_num);
  if (((ni_xcoder_params_t *)p_ctx->p_session_config)->low_delay_mode)
  {
      query_type = INST_BUF_INFO_RW_READ_BUSY;
      if (!p_packet->end_of_stream && p_ctx->frame_num < p_ctx->pkt_num)
      {
          if (p_ctx->ready_to_close)
          {
              query_type = INST_BUF_INFO_RW_READ;
          } else
          {   //nothing to query, leave
              retval = NI_RETCODE_SUCCESS;
              LRETURN;
          }
      }
  }
  for (;;)
  {
      retval = ni_query_instance_buf_info(p_ctx, query_type,
                                          NI_DEVICE_TYPE_ENCODER, &buf_info);
      CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);

      ni_log(NI_LOG_DEBUG, "Info enc read query rc %d, available buf size %u, "
                     "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 "\n",
                     retval, buf_info.buf_avail_size, p_ctx->frame_num,
                     p_ctx->pkt_num);

      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log(NI_LOG_ERROR, "Buffer info query failed in encoder read!!!!\n");
          LRETURN;
      } else if (0 == buf_info.buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_ENCODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log(NI_LOG_ERROR, "Stream info query failed in encoder read!!!!\n");
          LRETURN;
        }

        if (data.is_flushed)
        {
            p_packet->end_of_stream = 1;
        }
      }
      ni_log(NI_LOG_DEBUG, "Info enc read available buf size %u, eos %u !\n",
                     buf_info.buf_avail_size, p_packet->end_of_stream);

      if (((ni_xcoder_params_t *)p_ctx->p_session_config)->low_delay_mode &&
          !p_packet->end_of_stream && p_ctx->frame_num >= p_ctx->pkt_num)
      {
          ni_log(NI_LOG_DEBUG, "Encoder low latency mode, eos not sent, frame_num "
                         "%" PRIu64 " >= %" PRIu64 " pkt_num, keep querying\n",
                         p_ctx->frame_num, p_ctx->pkt_num);
          ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
          ni_usleep(200);
          ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
          continue;
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    } else
    {
        break;
    }
  }
  ni_log(NI_LOG_DEBUG, "Encoder read buf_avail_size %u\n", buf_info.buf_avail_size);

  to_read_size = buf_info.buf_avail_size;

  p_packet->data_len = 0;
  p_packet->pts = NI_NOPTS_VALUE;
  p_packet->dts = 0;

  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
  actual_read_size = to_read_size;
  if (actual_read_size % NI_MEM_PAGE_ALIGNMENT)
  {
      actual_read_size =
          ((actual_read_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) +
          NI_MEM_PAGE_ALIGNMENT;
  }

  if (p_packet->buffer_size < actual_read_size)
  {
      if (ni_packet_buffer_alloc(p_packet, actual_read_size))
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR %s(): packet buffer size %u allocation "
                 "failed\n",
                 __func__, actual_read_size);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
  }

  retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 p_packet->p_data, actual_read_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  // SSIM is supported if fw_rev is higher than major 6 minor 1.
  if (is_fw_rev_higher(p_ctx, (uint8_t)'6', (uint8_t)'1'))
  {
      p_meta = (ni_metadata_enc_bstream_t *)p_packet->p_data;
      p_packet->pts = (int64_t)(p_meta->frame_tstamp);
      p_packet->frame_type = p_meta->frame_type;
      p_packet->avg_frame_qp = p_meta->avg_frame_qp;
      p_packet->recycle_index = p_meta->recycle_index;
      p_packet->av1_show_frame = p_meta->av1_show_frame;
      ni_log(NI_LOG_DEBUG, "%s RECYCLE INDEX = %u!!!\n", __FUNCTION__, p_meta->recycle_index);

      p_ctx->meta_size = p_meta->metadata_size;

      if (p_meta->ssimY != 0)
      {
          // The SSIM Y, U, V values returned by FW are 4 decimal places multiplied by 10000.
	  //Divide by 10000 to get the original value.
          ni_log(NI_LOG_DEBUG, "%s: ssim Y %.4f U %.4f V %.4f\n", __FUNCTION__,
                 (float)p_meta->ssimY/10000, (float)p_meta->ssimU/10000, (float)p_meta->ssimV/10000);
      }
  }
  else
  {
      // Up to fw_rev major 6 and minor 1, use the old meta data structure
      p_meta_rev61 = (ni_metadata_enc_bstream_rev61_t *)p_packet->p_data;
      p_packet->pts = (int64_t)(p_meta_rev61->frame_tstamp);
      p_packet->frame_type = p_meta_rev61->frame_type;
      p_packet->avg_frame_qp = p_meta_rev61->avg_frame_qp;
      p_packet->recycle_index = p_meta_rev61->recycle_index;
      p_packet->av1_show_frame = p_meta_rev61->av1_show_frame;
      ni_log(NI_LOG_DEBUG, "%s RECYCLE INDEX = %u!!!\n", __FUNCTION__, p_meta_rev61->recycle_index);

  }

  p_packet->data_len = to_read_size;

  size = p_packet->data_len;

  if (size > 0)
  {
    if (p_ctx->pkt_num >= 1)
    {
      if (ni_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)& p_packet->dts, 0, encq_count % 500 == 0, p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
        p_packet->dts = NI_NOPTS_VALUE;
      }
      p_ctx->pkt_num++;
    }

    encq_count++;

#ifdef XCODER_DUMP_DATA
    char dump_file[256];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-pkt/pkt-%04ld.bin",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->pkt_num);

    FILE *f = fopen(dump_file, "wb");
    fwrite((uint8_t *)p_packet->p_data + sizeof(ni_metadata_enc_bstream_t),
           p_packet->data_len - sizeof(ni_metadata_enc_bstream_t), 1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log(
      NI_LOG_DEBUG,
      "%s(): %d.%u p_packet->start_of_stream=%u, "
      "p_packet->end_of_stream=%u, p_packet->video_width=%u, "
      "p_packet->video_height=%u, p_packet->dts=0x%08x 0x%08x, "
      "p_packet->pts=0x%08x 0x%08x, type=%u, avg_frame_qp=%u, show_frame=%d\n",
      __func__, p_ctx->hw_id, p_ctx->session_id, p_packet->start_of_stream,
      p_packet->end_of_stream, p_packet->video_width, p_packet->video_height,
      (uint32_t)((p_packet->dts >> 32) & 0xFFFFFFFF),
      (uint32_t)(p_packet->dts & 0xFFFFFFFF),
      (uint32_t)((p_packet->pts >> 32) & 0xFFFFFFFF),
      (uint32_t)(p_packet->pts & 0xFFFFFFFF), p_packet->frame_type,
      p_packet->avg_frame_qp, p_packet->av1_show_frame);

  ni_log(NI_LOG_DEBUG, "%s(): p_packet->data_len=%u, size=%d\n", __func__,
         p_packet->data_len, size);

  if (encq_count % 500 == 0)
  {
      ni_log(NI_LOG_DEBUG,
             "Encoder pts queue size = %u  dts queue size = %u\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
  }

  retval = size;

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,eLAT:%lu;\n", p_packet->dts,
             abs_time_ns - p_ctx->prev_read_frame_time,
             ni_lat_meas_q_check_latency((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                                         abs_time_ns, p_packet->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send sequnce change to a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_sequence_change(ni_session_context_t* p_ctx, ni_resolution_t *p_resolution)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);    

    //Configure encoder sequence change
    retval = ni_config_instance_set_sequence_change(p_ctx, NI_DEVICE_TYPE_ENCODER,
                                                                        p_resolution);
    CHECK_ERR_RC(p_ctx, retval, nvme_config_xcoder_config_set_sequence_change,
                            p_ctx->device_type, p_ctx->hw_id,
                            &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): config encoder sequence change command failed\n",
               __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

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
int ni_scaler_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_ctx->scaler_operation == NI_SCALER_OPCODE_STACK)
  {
    if ((p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] < '6') ||
         ((p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] == '6') &&
          (p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX] < '4')))
    {
      ni_log(NI_LOG_ERROR, "ERROR: "
             "FW revision %c%c is less then the minimum required 64\n",
             p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
             p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX]
             );
      return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }
  }

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_SCALER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_SCALER, ni_htonl(p_ctx->scaler_operation), 0);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp =
        ni_htonll(((ni_session_stats_t *)p_buffer)->ui64Session_timestamp);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log(NI_LOG_DEBUG, "Scaler open session ID:0x%x\n",p_ctx->session_id);
    ni_log(NI_LOG_DEBUG,
           "%s(): p_ctx->device_handle=%" PRIx64
           ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_DEBUG, "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW version is higher than major 6 minor 1
    if (is_fw_rev_higher(p_ctx, (uint8_t)'6', (uint8_t)'1'))
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_log(NI_LOG_DEBUG, "%s sw_version major %c minor %c fw_rev major %c minor %c\n", __func__,
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                p_ctx->fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX]);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }
  }

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = 0;
  p_ctx->last_dts = 0;
  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->actual_video_width = 0;

#ifndef _WIN32
  if (p_ctx->isP2P)
  {
      int ret;
      char line[256];
      char syspath[256];
      struct stat bstat;
      char *block_dev;
      char *dom, *bus, *dev, *fnc;
      FILE *fp;

      p_ctx->netint_fd = open("/dev/netint", O_RDWR);
      if (p_ctx->netint_fd < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR: Can't open device /dev/netint\n");
          return NI_RETCODE_FAILURE;
      }

      block_dev = &p_ctx->blk_xcoder_name[0];
      if (stat(block_dev, &bstat) < 0)
      {
          ni_log(NI_LOG_ERROR, "failed to get stat of file %s\n", block_dev);
          return NI_RETCODE_FAILURE;
      }

      if ((bstat.st_mode & S_IFMT) != S_IFBLK)
      {
          ni_log(NI_LOG_ERROR, "%s is not a block device\n", block_dev);
          return NI_RETCODE_FAILURE;
      }

      ret = snprintf(syspath, sizeof(syspath) - 1,
                     "/sys/block/%s/device/address", block_dev + 5);
      syspath[ret] = '\0';

      fp = fopen(syspath, "r");

      if (fp == NULL)
      {
          ni_log(NI_LOG_ERROR, "Failed to read address\n");
          return NI_RETCODE_FAILURE;
      }

      if (fgets(line, 256, fp) == NULL)
      {
          ni_log(NI_LOG_ERROR, "Failed to read line from address\n");
          fclose(fp);
          return NI_RETCODE_FAILURE;
      }

      fclose(fp);

      errno = 0;
      p_ctx->domain = strtoul(line, &dom, 16);
      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI domain\n");
          return NI_RETCODE_FAILURE;
      }

      errno = 0;
      p_ctx->bus = strtoul(dom + 1, &bus, 16);
      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI bus\n");
          return NI_RETCODE_FAILURE;
      }

      errno = 0;
      p_ctx->dev = strtoul(bus + 1, &dev, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI device\n");
          return NI_RETCODE_FAILURE;
      }

      errno = 0;
      p_ctx->fn = strtoul(dev + 1, &fnc, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Falied to read PCI function\n");
          return NI_RETCODE_FAILURE;
      }

      ni_log(NI_LOG_DEBUG, "PCI slot = %d:%d:%d:%d\n", p_ctx->domain,
             p_ctx->bus, p_ctx->dev, p_ctx->fn);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    return retval;
}

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
ni_retcode_t ni_scaler_session_close(ni_session_context_t* p_ctx, int eos_received)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void * p_buffer = NULL;
  uint32_t ui32LBA = 0;

  if (!p_ctx)
  {
    return NI_RETCODE_INVALID_PARAM;
  }

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() malloc data buffer failed\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_SCALER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log(NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): command failed!\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          break;
      } else
    {
      //Close should always succeed
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
	  break;
    }
    retry++;
  }

#ifndef _WIN32
  if (p_ctx->isP2P)
  {
      if (p_ctx->netint_fd)
      {
          close(p_ctx->netint_fd);
      }
  }
#endif

END:

    ni_aligned_free(p_ctx->p_all_zero_buf);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure scaling parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_scaler_params_t * params - pointer to the scaler ni_scaler_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_scaler_params(ni_session_context_t *p_ctx,
                                                  ni_scaler_params_t *p_params)
{
    void *p_scaler_config = NULL;
    ni_scaler_config_t *p_cfg = NULL;
    uint32_t buffer_size = sizeof(ni_scaler_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size =
        ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) *
        NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_scaler_config, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() malloc p_scaler_config buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_scaler_config, 0, buffer_size);

    //configure the session here
    ui32LBA = CONFIG_INSTANCE_SetScalerPara_W(p_ctx->session_id,
                                              NI_DEVICE_TYPE_SCALER);

    //Flip the bytes!!
    p_cfg = (ni_scaler_config_t *)p_scaler_config;
    p_cfg->filterblit = p_params->filterblit;
    p_cfg->numInputs = p_params->nb_inputs;

    if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                               p_scaler_config, buffer_size, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %d, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                   "hw_id, %d, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_scaler_config);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

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
ni_retcode_t ni_scaler_alloc_frame(ni_session_context_t* p_ctx,
                                   int width,
                                   int height,
                                   int format,
                                   int options,
                                   int rectangle_width,
                                   int rectangle_height,
                                   int rectangle_x,
                                   int rectangle_y,
                                   int rgba_color,
                                   int frame_index)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data->picture_width = width;
    p_data->picture_height = height;
    p_data->picture_format = format;
    p_data->options = options;
    p_data->rectangle_width = rectangle_width;
    p_data->rectangle_height = rectangle_height;
    p_data->rectangle_x = rectangle_x;
    p_data->rectangle_y = rectangle_y;
    p_data->rgba_color = rgba_color;
    p_data->frame_index = frame_index;

    ni_log(NI_LOG_DEBUG,
           "Dev alloc frame: W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
           p_data->picture_width, p_data->picture_height,
           p_data->picture_format, p_data->rectangle_width,
           p_data->rectangle_height, p_data->rectangle_x, p_data->rectangle_y);
    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: "
               "blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

// cppcheck-suppress unusedLabel
END:

    ni_aligned_free(p_data);
    return retval;
}

/*!******************************************************************************
 *  \brief  config a frame in the scaler
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
                                    ni_frame_config_t *p_cfg)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx || !p_cfg)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data->picture_width = p_cfg->picture_width;
    p_data->picture_height = p_cfg->picture_height;
    p_data->picture_format = p_cfg->picture_format;
    p_data->options = p_cfg->options;

    p_data->rectangle_width = p_cfg->rectangle_width;
    p_data->rectangle_height = p_cfg->rectangle_height;
    p_data->rectangle_x = p_cfg->rectangle_x;
    p_data->rectangle_y = p_cfg->rectangle_y;
    p_data->rgba_color = p_cfg->rgba_color;
    p_data->frame_index = p_cfg->frame_index;
    p_data->session_id = p_cfg->session_id;
    p_data->output_index = p_cfg->output_index;

    ni_log(NI_LOG_DEBUG,
           "Dev config frame: W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
           p_data->picture_width, p_data->picture_height,
           p_data->picture_format, p_data->rectangle_width,
           p_data->rectangle_height, p_data->rectangle_x, p_data->rectangle_y);

    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

        ni_log(NI_LOG_ERROR,
               "ERROR ni_scaler_config(): nvme command failed!\n");
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

// cppcheck-suppress unusedLabel
END:

    ni_aligned_free(p_data);
    return retval;
}

/*!******************************************************************************
 *  \brief  config multiple frames in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_cfg_in            pointer to input frame config array
 *  \param[in]  numInCfgs           number of input frame configs in the p_cfg array
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
                                          ni_frame_config_t *p_cfg_out)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data, *p_data_orig;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;
    int i;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) * (numInCfgs + 1) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx || (!p_cfg_in && numInCfgs))
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data_orig = p_data;

    for (i = 0; i < numInCfgs; i++)
    {
        p_data->picture_width = p_cfg_in[i].picture_width;
        p_data->picture_height = p_cfg_in[i].picture_height;
        p_data->picture_format = p_cfg_in[i].picture_format;
        p_data->options = p_cfg_in[i].options & ~NI_SCALER_FLAG_IO;

        p_data->rectangle_width = p_cfg_in[i].rectangle_width;
        p_data->rectangle_height = p_cfg_in[i].rectangle_height;
        p_data->rectangle_x = p_cfg_in[i].rectangle_x;
        p_data->rectangle_y = p_cfg_in[i].rectangle_y;
        p_data->rgba_color = p_cfg_in[i].rgba_color;
        p_data->frame_index = p_cfg_in[i].frame_index;
        p_data->session_id = p_cfg_in[i].session_id;
        p_data->output_index = p_cfg_in[i].output_index;

        ni_log(NI_LOG_DEBUG,
               "Dev in config frame %d: W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
               i, p_data->picture_width, p_data->picture_height,
               p_data->picture_format, p_data->rectangle_width,
               p_data->rectangle_height, p_data->rectangle_x, p_data->rectangle_y);

        p_data++;
    }

    if (p_cfg_out)
    {
        p_data->picture_width = p_cfg_out->picture_width;
        p_data->picture_height = p_cfg_out->picture_height;
        p_data->picture_format = p_cfg_out->picture_format;
        p_data->options = p_cfg_out->options | NI_SCALER_FLAG_IO;

        p_data->rectangle_width = p_cfg_out->rectangle_width;
        p_data->rectangle_height = p_cfg_out->rectangle_height;
        p_data->rectangle_x = p_cfg_out->rectangle_x;
        p_data->rectangle_y = p_cfg_out->rectangle_y;
        p_data->rgba_color = p_cfg_out->rgba_color;
        p_data->frame_index = p_cfg_out->frame_index;

        ni_log(NI_LOG_DEBUG,
               "Dev out config frame: W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
               p_data->picture_width, p_data->picture_height,
               p_data->picture_format, p_data->rectangle_width,
               p_data->rectangle_height, p_data->rectangle_x,
               p_data->rectangle_y);
    }

    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data_orig, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

        ni_log(NI_LOG_ERROR,
               "ERROR ni_scaler_config(): nvme command failed!\n");
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

// cppcheck-suppress unusedLabel
END:

    ni_aligned_free(p_data_orig);
    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get GeneralStatus data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_general_status_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_general_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_general_status_t* p_gen_status)
{
  void* p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_general_status_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_gen_status))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!IS_XCODER_DEVICE_TYPE(device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ui32LBA = QUERY_GENERAL_GET_STATUS_R(device_type);

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  //No need to flip the bytes since the datastruct has only uint8_t datatypes
  memcpy((void*)p_gen_status, p_buffer, sizeof(ni_instance_mgr_general_status_t));

  ni_log(NI_LOG_DEBUG, "%s(): model_load:%u qc:%d percent:%d\n", __func__,
         p_gen_status->fw_model_load, p_gen_status->cmd_queue_count,
         p_gen_status->process_load_percent);
END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get Stream Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_stream_info_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_query_stream_info(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_info_t* p_stream_info)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_stream_info))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_STREAM_INFO_R(p_ctx->session_id, device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_stream_info, p_buffer, sizeof(ni_instance_mgr_stream_info_t));

  //flip the bytes to host order
  p_stream_info->picture_width = ni_htons(p_stream_info->picture_width);
  p_stream_info->picture_height = ni_htons(p_stream_info->picture_height);
  p_stream_info->frame_rate = ni_htons(p_stream_info->frame_rate);
  p_stream_info->is_flushed = ni_htons(p_stream_info->is_flushed);
  p_stream_info->transfer_frame_stride = ni_htons(p_stream_info->transfer_frame_stride);
  ni_log(NI_LOG_DEBUG, "%s(): pix_format = %d\n", __func__,
         p_stream_info->pix_format);   //temp

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular session to get the stats info
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_session_stats_t *out - Struct preallocated from the
 *           caller where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED
 *            on failure
 ******************************************************************************/
ni_retcode_t ni_query_session_stats(ni_session_context_t *p_ctx,
                                    ni_device_type_t device_type,
                                    ni_session_stats_t *p_session_stats, int rc,
                                    int opcode)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_session_stats_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_device_type_t xc_device_type =
      (device_type != NI_DEVICE_TYPE_UPLOAD ? device_type :
                                              NI_DEVICE_TYPE_ENCODER);

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_session_stats))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!IS_XCODER_DEVICE_TYPE(xc_device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_SESSION_STATS_R(p_ctx->session_id, xc_device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  // Set session ID to be invalid. In case, the last command fails because the invalid session ID was submitted
  // with the command, the session id would remain invalid.
  // If the last command is processed successfully in session manager, the session id would become valid.
  ((ni_session_stats_t *)p_buffer)->ui16SessionId =
      (uint16_t)NI_INVALID_SESSION_ID;

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA)
      < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): read command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_session_stats, p_buffer, sizeof(ni_session_stats_t));

  // flip the bytes to host order
  // all query commands are guaranteed success once a session is opened
  p_session_stats->ui16SessionId = ni_htons(p_session_stats->ui16SessionId);
  p_session_stats->ui16ErrorCount = ni_htons(p_session_stats->ui16ErrorCount);
  p_session_stats->ui32LastTransactionId = ni_htons(p_session_stats->ui32LastTransactionId);
  p_session_stats->ui32LastTransactionCompletionStatus = ni_htons(p_session_stats->ui32LastTransactionCompletionStatus);
  p_session_stats->ui32LastErrorTransactionId = ni_htonl(p_session_stats->ui32LastErrorTransactionId);
  p_session_stats->ui32LastErrorStatus = ni_htonl(p_session_stats->ui32LastErrorStatus);
  p_session_stats->ui64Session_timestamp =
      ni_htonll(p_session_stats->ui64Session_timestamp);

  // get the session timestamp when open session
  // check the timestamp during transcoding
  if ((p_ctx->session_timestamp != p_session_stats->ui64Session_timestamp) &&
      (ni_xcoder_resource_recovery != p_session_stats->ui32LastErrorStatus))
  // if VPU recovery, the session timestamp will be reset.
  {
      p_session_stats->ui32LastErrorStatus =
          NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE;
      ni_log(NI_LOG_DEBUG, "instance id invalid:%u, timestamp:%" PRIu64
                     ", query timestamp:%" PRIu64 "\n",
                     p_ctx->session_id, p_ctx->session_timestamp,
                     p_session_stats->ui64Session_timestamp);
  }

  // check rc here, if rc != NI_RETCODE_SUCCESS, it means that last read/write command failed
  // failures may be link layer errors, such as physical link errors or ERROR_WRITE_PROTECT in windows.
  if (NI_RETCODE_SUCCESS != rc)
  {
      ni_log(NI_LOG_ERROR, "%s():last command Failed: rc %d\n", __func__, rc);
      p_session_stats->ui32LastTransactionCompletionStatus =
          NI_RETCODE_ERROR_NVME_CMD_FAILED;
      p_session_stats->ui32LastErrorStatus = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  } else if (p_ctx->session_id != p_session_stats->ui16SessionId)
  {
      ni_log(NI_LOG_ERROR,
             "%s():last command Failed because "
             "session ID 0x%x is invalid\n",
             __func__, p_ctx->session_id);
      p_session_stats->ui32LastErrorStatus = NI_RETCODE_ERROR_INVALID_SESSION;

      // Mark session id to INVALID so that all commands afterward are blocked
      p_ctx->session_id = NI_INVALID_SESSION_ID;
  } else
  {
	  // Acknowledge the total error count here
	  p_ctx->rc_error_count = p_session_stats->ui16ErrorCount;
  }

  ni_log(NI_LOG_TRACE, "%s(): error count %u last rc 0x%x inst_err_no 0x%x\n",
         __func__, p_session_stats->ui16ErrorCount,
         p_session_stats->ui32LastTransactionCompletionStatus,
         p_session_stats->ui32LastErrorStatus);

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get End of Output data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   InstMgrStreamComp *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_complete_t* p_stream_complete)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_complete_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx || !p_stream_complete)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_EOS_R(p_ctx->session_id, device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate buffer.\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_stream_complete, p_buffer, sizeof(ni_instance_mgr_stream_complete_t));

  //flip the bytes to host order
  p_stream_complete->is_flushed = ni_htons(p_stream_complete->is_flushed);

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get buffer/data Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_instance_buf_info_rw_type_t rw_type
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_buf_info_t *out - Struct preallocated from the caller
 *           where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_retcode_t ni_query_instance_buf_info(ni_session_context_t *p_ctx,
                                        ni_instance_buf_info_rw_type_t rw_type,
                                        ni_device_type_t device_type,
                                        ni_instance_buf_info_t *p_inst_buf_info)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen =
      ((sizeof(ni_instance_buf_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) /
       NI_MEM_PAGE_ALIGNMENT) *
      NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx || !p_inst_buf_info)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_DECODER == device_type ||
        NI_DEVICE_TYPE_ENCODER == device_type ||
        NI_DEVICE_TYPE_SCALER == device_type ||
        NI_DEVICE_TYPE_AI == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (INST_BUF_INFO_RW_READ == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_RBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_WRITE == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_WBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_UPLOAD == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_UPLOAD_ID_R(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_R_ACQUIRE == rw_type)
  {
      ui32LBA = QUERY_INSTANCE_ACQUIRE_BUF(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_RW_READ_BUSY == rw_type)
  {
      ui32LBA =
          QUERY_INSTANCE_RBUFF_SIZE_BUSY_R(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_RW_WRITE_BUSY == rw_type)
  {
      ui32LBA =
          QUERY_INSTANCE_WBUFF_SIZE_BUSY_R(p_ctx->session_id, device_type);
  } else
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown query type %d, return\n",
           __func__, rw_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_inst_buf_info, p_buffer, sizeof(ni_instance_buf_info_t));

  p_inst_buf_info->buf_avail_size = ni_htonl(p_inst_buf_info->buf_avail_size);

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Configure the read/write pipe for a session to control its behavior
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_session_config_rw_t rw_type
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   uint8_t enable
 *  \param   uint8_t hw_action
 *  \param   uint16_t frame_id
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_retcode_t ni_config_session_rw(ni_session_context_t *p_ctx,
                                  ni_session_config_rw_type_t rw_type,
                                  uint8_t enable, uint8_t hw_action,
                                  uint16_t frame_id)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;
  ni_session_config_rw_t * rw_config = NULL;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!((SESSION_READ_CONFIG == rw_type) || (SESSION_WRITE_CONFIG == rw_type)))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
           __func__, rw_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  buffer_size = ((sizeof(ni_session_config_rw_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  rw_config = (ni_session_config_rw_t *)p_buffer;
  rw_config->ui8Enable = enable;
  rw_config->ui8HWAccess = hw_action;
  switch(rw_type)
  {
    case SESSION_READ_CONFIG:
      rw_config->uHWAccessField.ui16ReadFrameId = frame_id;
      break;
    case SESSION_WRITE_CONFIG:
      rw_config->uHWAccessField.ui16WriteFrameId = frame_id;
      break;
    default:
      ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
             __func__, rw_type);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  switch(rw_type)
  {
    case SESSION_READ_CONFIG:
      ui32LBA = CONFIG_SESSION_Read_W(p_ctx->session_id);
      break;
    case SESSION_WRITE_CONFIG:
      ui32LBA = CONFIG_SESSION_Write_W(p_ctx->session_id);
      break;
    default:
      ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
             __func__, rw_type);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for Start Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION. NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_sos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetSOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for End Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetEOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to flush the stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t xcoder_config_instance_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_Flush_W(p_ctx->session_id, device_type);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN,
                                  ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_RETCODE_SUCCESS != retval)
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
  }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to set the length for the incoming write packet
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_write_len(ni_session_context_t* p_ctx, ni_device_type_t device_type, uint32_t len)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_DECODER == device_type ||
        NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((sizeof(len) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  memcpy(p_buffer, &len, sizeof(len));

  ui32LBA = CONFIG_INSTANCE_SetPktSize_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to inform encoder sequence change
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_resolution_t p_resolution - sequence change resolution
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_sequence_change(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_resolution_t *p_resolution)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Seq Change not supported for device type %d, return\n", device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((sizeof(ni_resolution_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  memcpy(p_buffer, p_resolution, sizeof(ni_resolution_t));

  ui32LBA = CONFIG_INSTANCE_SetSeqChange_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_params(ni_session_context_t* p_ctx)
{
  void* p_encoder_config = NULL;
  ni_encoder_config_t* p_cfg = NULL;
  uint32_t buffer_size = sizeof(ni_encoder_config_t);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int i = 0;
  uint32_t ui32LBA = 0;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_encoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_encoder_config, 0, buffer_size);

  ni_set_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config);
  retval = ni_validate_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config, p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg));
  if (NI_RETCODE_PARAM_WARN == retval)
  {
      ni_log(NI_LOG_INFO, "WARNING: ni_validate_custom_template() . %s\n", p_ctx->param_err_msg);
      fflush(stdout);
  }
  else if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log(NI_LOG_ERROR, "ERROR: ni_validate_custom_dec_template() failed. %s\n", p_ctx->param_err_msg);
      fflush(stdout);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  //configure the session here
  ui32LBA = CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  //Flip the bytes!!
  p_cfg = (ni_encoder_config_t*)p_encoder_config;
  p_cfg->i32picWidth = ni_htonl(p_cfg->i32picWidth);
  p_cfg->i32picHeight = ni_htonl(p_cfg->i32picHeight);
  p_cfg->i32meBlkMode = ni_htonl(p_cfg->i32meBlkMode);
  p_cfg->i32frameRateInfo = ni_htonl(p_cfg->i32frameRateInfo);
  p_cfg->i32vbvBufferSize = ni_htonl(p_cfg->i32vbvBufferSize);
  p_cfg->i32userQpMax = ni_htonl(p_cfg->i32userQpMax);
  p_cfg->i32maxIntraSize = ni_htonl(p_cfg->i32maxIntraSize);
  p_cfg->i32userMaxDeltaQp = ni_htonl(p_cfg->i32userMaxDeltaQp);
  p_cfg->i32userMinDeltaQp = ni_htonl(p_cfg->i32userMinDeltaQp);
  p_cfg->i32userQpMin = ni_htonl(p_cfg->i32userQpMin);
  p_cfg->i32bitRate = ni_htonl(p_cfg->i32bitRate);
  p_cfg->i32bitRateBL = ni_htonl(p_cfg->i32bitRateBL);
  p_cfg->i32srcBitDepth = ni_htonl(p_cfg->i32srcBitDepth);
  p_cfg->hdrEnableVUI = ni_htonl(p_cfg->hdrEnableVUI);
  p_cfg->ui32VuiDataSizeBits = ni_htonl(p_cfg->ui32VuiDataSizeBits);
  p_cfg->ui32VuiDataSizeBytes = ni_htonl(p_cfg->ui32VuiDataSizeBytes);
  p_cfg->i32hwframes = ni_htonl(p_cfg->i32hwframes);
  p_cfg->ui16HDR10MaxLight = ni_htons(p_cfg->ui16HDR10MaxLight);
  p_cfg->ui16HDR10AveLight = ni_htons(p_cfg->ui16HDR10AveLight);
  p_cfg->ui16gdrDuration = ni_htons(p_cfg->ui16gdrDuration);
  p_cfg->ui32ltrRefInterval = ni_htonl(p_cfg->ui32ltrRefInterval);
  p_cfg->i32ltrRefQpOffset = ni_htonl(p_cfg->i32ltrRefQpOffset);
  p_cfg->ui32ltrFirstGap = ni_htonl(p_cfg->ui32ltrFirstGap);
  p_cfg->i32tolCtbRcInter = ni_htonl(p_cfg->i32tolCtbRcInter);
  p_cfg->i32tolCtbRcIntra = ni_htonl(p_cfg->i32tolCtbRcIntra);
  p_cfg->i16bitrateWindow = ni_htons(p_cfg->i16bitrateWindow);
  // flip the NI_MAX_VUI_SIZE bytes of the VUI field using 32 bits pointers
  for (i = 0 ; i < (NI_MAX_VUI_SIZE >> 2) ; i++) // apply on 32 bits
  {
    ((uint32_t*)p_cfg->ui8VuiRbsp)[i] = ni_htonl(((uint32_t*)p_cfg->ui8VuiRbsp)[i]);
  }

  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_encoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_encoder_config);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding p_frame parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_encoder_frame_params_t * params - pointer to the encoder ni_encoder_frame_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_frame_params(ni_session_context_t* p_ctx, ni_encoder_frame_params_t* p_params)
{
    ni_encoder_frame_params_t *p_cfg;
    uint32_t buffer_size = sizeof(ni_encoder_frame_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign((void **)&p_cfg, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  //configure the session here
  ui32LBA = CONFIG_INSTANCE_SetEncFramePara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  //Flip the bytes!!
  p_cfg->force_picture_type = ni_htons(p_params->force_picture_type);
  p_cfg->data_format = ni_htons(p_params->data_format);
  p_cfg->picture_type = ni_htons(p_params->picture_type);
  p_cfg->video_width = ni_htons(p_params->video_width);
  p_cfg->video_height = ni_htons(p_params->video_height);
  p_cfg->timestamp = ni_htonl(p_params->timestamp);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_cfg,
                             buffer_size, ui32LBA) < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it as per Farhan
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  ni_aligned_free(p_cfg);
  ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}

#if 0 // ndef QUADRA_SEI_FMT
// return non-0 if SEI of requested type is found, 0 otherwise
static int find_sei(uint32_t sei_header, ni_sei_user_data_entry_t *pEntry,
                    ni_h265_sei_user_data_type_t type,
                    uint32_t *pSeiOffset, uint32_t *pSeiSize)
{
  int ret = 0;


  if( (!pEntry) || (!pSeiOffset) || (!pSeiSize) )
  {
    return  ret;
  }


  if (sei_header & (1 << type))
  {
    *pSeiOffset = pEntry[type].offset;
    *pSeiSize = pEntry[type].size;
    ni_log(NI_LOG_DEBUG, "find_sei sei type %d, offset: %u  size: %u\n",
                   type, *pSeiOffset, *pSeiSize);
    ret = 1;
  }

  return ret;
}

// return non-0 if prefix or suffix T.35 message is found
static int find_prefix_suffix_t35(uint32_t sei_header,
                                  ni_t35_sei_mesg_type_t t35_type,
                                  ni_sei_user_data_entry_t *pEntry,
                                  ni_h265_sei_user_data_type_t type,
                                  uint32_t *pCcOffset, uint32_t *pCcSize)
{
  int ret = 0;
  uint8_t *ptr;


  if( (!pEntry) || (!pCcOffset) || (!pCcSize) )
  {
    return  ret;
  }


  // Find first t35 message with CEA708 close caption (first 7
  // bytes are itu_t_t35_country_code 0xB5 0x00 (181),
  // itu_t_t35_provider_code = 0x31 (49),
  // ATSC_user_identifier = 0x47 0x41 0x39 0x34 ("GA94")
  // or HDR10+ header bytes
  if (sei_header & (1 << type))
  {
    ptr = (uint8_t*)pEntry + pEntry[type].offset;
    if (NI_T35_SEI_CLOSED_CAPTION == t35_type &&
        ptr[0] == NI_CC_SEI_BYTE0 && ptr[1] == NI_CC_SEI_BYTE1 &&
        ptr[2] == NI_CC_SEI_BYTE2 && ptr[3] == NI_CC_SEI_BYTE3 &&
        ptr[4] == NI_CC_SEI_BYTE4 && ptr[5] == NI_CC_SEI_BYTE5 &&
        ptr[6] == NI_CC_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_DEBUG, "find_prefix_suffix_t35: close Caption SEI found in T.35 type %d, offset: %u  size: %u\n", type, *pCcOffset, *pCcSize);
      ret = 1;
    }
    else if (NI_T35_SEI_HDR10_PLUS == t35_type &&
             ptr[0] == NI_HDR10P_SEI_BYTE0 && ptr[1] == NI_HDR10P_SEI_BYTE1 &&
             ptr[2] == NI_HDR10P_SEI_BYTE2 && ptr[3] == NI_HDR10P_SEI_BYTE3 &&
             ptr[4] == NI_HDR10P_SEI_BYTE4 && ptr[5] == NI_HDR10P_SEI_BYTE5 &&
             ptr[6] == NI_HDR10P_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_DEBUG, "find_prefix_suffix_t35: HDR10+ SEI found in T.35 type %d, offset: %u  size: %u\n", type, *pCcOffset, *pCcSize);
      ret = 1;
    }
  }

  return ret;
}

// return non-0 when HDR10+/close-caption is found, 0 otherwise
static int find_t35_sei(uint32_t sei_header, ni_t35_sei_mesg_type_t t35_type,
                        ni_sei_user_data_entry_t *pEntry,
                        uint32_t *pCcOffset, uint32_t *pCcSize)
{
  int ret = 0;

  if( (!pEntry) || (!pCcOffset) || (!pCcSize) )
  {
    return  ret;
  }

  *pCcOffset = *pCcSize = 0;

  // Check up to 3 T35 Prefix and Suffix SEI for close captions
  if (find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_1,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_2,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_1,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_2,
                             pCcOffset, pCcSize)
    )
  {
    ret = 1;
  }
  return ret;
}
#endif // QUADRA_SEI_FMT

/*!******************************************************************************
 *  \brief  Get info from received p_frame
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_create_frame(ni_frame_t* p_frame, uint32_t read_length, uint64_t* p_frame_offset, bool is_hw_frame)
{
    uint32_t rx_size =
        read_length;   //get the length since its the only thing in DW10 now

    if (!p_frame || !p_frame_offset)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    *p_frame_offset = 0;

    unsigned int metadata_size = NI_FW_META_DATA_SZ -
        NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
    unsigned int video_data_size = p_frame->data_len[0] + p_frame->data_len[1] +
        p_frame->data_len[2] + ((is_hw_frame) ? p_frame->data_len[3] : 0);
    ni_log(NI_LOG_DEBUG, "rx_size = %d metadataSize = %d\n", rx_size,
           metadata_size);

    p_frame->p_custom_sei_set = NULL;

    if (rx_size == metadata_size)
    {
        video_data_size = 0;
    }

    if (rx_size > video_data_size)
    {
        ni_metadata_dec_frame_t *p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                        video_data_size);

        *p_frame_offset = p_meta->metadata_common.ui64_data.frame_offset;
        rx_size -= metadata_size;
        p_frame->crop_top = p_meta->metadata_common.crop_top;
        p_frame->crop_bottom = p_meta->metadata_common.crop_bottom;
        p_frame->crop_left = p_meta->metadata_common.crop_left;
        p_frame->crop_right = p_meta->metadata_common.crop_right;
        p_frame->ni_pict_type = p_meta->metadata_common.frame_type;

        p_frame->video_width = p_meta->metadata_common.frame_width;
        p_frame->video_height = p_meta->metadata_common.frame_height;

        ni_log(
            NI_LOG_DEBUG,
            "%s: [metadata] cropRight=%u, cropLeft=%u, "
            "cropBottom=%u, cropTop=%u, frame_offset=%" PRIu64 ", pic=%ux%u, "
            "pict_type=%d, crop=%ux%u, sei header: 0x%0x  number %u size %u\n",
            __func__, p_frame->crop_right, p_frame->crop_left,
            p_frame->crop_bottom, p_frame->crop_top,
            p_meta->metadata_common.ui64_data.frame_offset,
            p_meta->metadata_common.frame_width,
            p_meta->metadata_common.frame_height, p_frame->ni_pict_type,
            p_frame->crop_right - p_frame->crop_left,
            p_frame->crop_bottom - p_frame->crop_top, p_meta->sei_header,
            p_meta->sei_number, p_meta->sei_size);

        p_frame->sei_total_len = 0;
        p_frame->sei_cc_offset = 0;
        p_frame->sei_cc_len = 0;
        p_frame->sei_hdr_mastering_display_color_vol_offset = 0;
        p_frame->sei_hdr_mastering_display_color_vol_len = 0;
        p_frame->sei_hdr_content_light_level_info_offset = 0;
        p_frame->sei_hdr_content_light_level_info_len = 0;
        p_frame->sei_hdr_plus_offset = 0;
        p_frame->sei_hdr_plus_len = 0;
        p_frame->sei_user_data_unreg_offset = 0;
        p_frame->sei_user_data_unreg_len = 0;

        if (p_meta->sei_number)
        {
#if 1 // QUADRA_SEI_FMT
    ni_log(NI_LOG_DEBUG, "ui32SeiHeader 0x%x ui16SeiNumber %d ui16SeiSize %d SEI 0x%02x%02x\n",
      p_meta->sei_header, p_meta->sei_number, p_meta->sei_size,
      *((uint8_t*)p_meta + metadata_size),
      *((uint8_t*)p_meta + metadata_size+1));

     { // retrieve sei from new format
      uint16_t ui16SeiProcessed = 0;
      ni_sei_header_t * pEntryHeader = &p_meta->first_sei_header;
      uint32_t ui32Offset = 0;
      uint32_t ui32Size;

      rx_size -= p_meta->sei_size;

      do
      {
        ui16SeiProcessed++;

        if (pEntryHeader->status)
        {
          ui32Size = pEntryHeader->size;
        }
        else
        {
          ui32Size = 0;
        }

        ni_log(NI_LOG_DEBUG, "SEI #%x st %d size %d ty %d sz/of %u/%u 0x%02x%02x\n",
                       ui16SeiProcessed, pEntryHeader->status,
                       pEntryHeader->size, pEntryHeader->type, ui32Size,
                       ui32Offset,
                       *((uint8_t *)p_meta + metadata_size + ui32Offset),
                       *((uint8_t *)p_meta + metadata_size + ui32Offset + 1));

        // - if multiple entries with same SEI type/subtype, only the last entry is saved;
        //   consider to use sei_offset[] array instead of explicit sei_*_offset
        // - user_data_unreg (UDU) and custom sei passthru via HW and SW respectively
        //   thus custom SEI is not processed here as it is by SW.

        switch(pEntryHeader->type)
        {
          case 4: //HEVC_SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35
            if (ui32Size)
            {
              uint8_t *ptr = (uint8_t*)p_meta + metadata_size + ui32Offset;
              if(ptr[0] == NI_HDR10P_SEI_BYTE0 && ptr[1] == NI_HDR10P_SEI_BYTE1 &&
                 ptr[2] == NI_HDR10P_SEI_BYTE2 && ptr[3] == NI_HDR10P_SEI_BYTE3 &&
                 ptr[4] == NI_HDR10P_SEI_BYTE4 && ptr[5] == NI_HDR10P_SEI_BYTE5 &&
                 ptr[6] == NI_HDR10P_SEI_BYTE6)
              {
                p_frame->sei_hdr_plus_len = ui32Size;
                p_frame->sei_hdr_plus_offset =
                video_data_size + metadata_size + ui32Offset;

                p_frame->sei_total_len += ui32Size;

                ni_log(NI_LOG_DEBUG, "%s: hdr10+ size=%u hdr10+ offset=%u\n",
                       __func__, p_frame->sei_hdr_plus_len,
                       p_frame->sei_hdr_plus_offset);
              }
              else if(ptr[0] == NI_CC_SEI_BYTE0 && ptr[1] == NI_CC_SEI_BYTE1 &&
                      ptr[2] == NI_CC_SEI_BYTE2 && ptr[3] == NI_CC_SEI_BYTE3 &&
                      ptr[4] == NI_CC_SEI_BYTE4 && ptr[5] == NI_CC_SEI_BYTE5 &&
                      ptr[6] == NI_CC_SEI_BYTE6)
              {
                  // Found CC data
                  // number of 3 byte close captions is bottom 5 bits of
                  // 9th byte of T35 payload
                  // uint32_t ui32CCSize = (ptr[8] & 0x1F) * 3; // avoid overwriting ui32CCSize

                  // return close caption data offset and length, and
                  // skip past 10 header bytes to close caption data
                  p_frame->sei_cc_len = (ptr[8] & 0x1F) * 3; // ui32CCSize;
                  p_frame->sei_cc_offset = video_data_size + metadata_size
                  + ui32Offset + 10;

                  p_frame->sei_total_len += p_frame->sei_cc_len;

                  ni_log(NI_LOG_DEBUG,
                         "%s: close caption size %u ,"
                         "offset %u = video size %u meta size %d off "
                         " %u + 10\n",
                         __func__, p_frame->sei_cc_len, p_frame->sei_cc_offset,
                         video_data_size, metadata_size, ui32Offset);
              }
              else
              {
                  ni_log(NI_LOG_DEBUG,
                         "%s: unsupported T35; type %u size %u status %u "
                         "offset %u\n",
                         __func__, pEntryHeader->type, pEntryHeader->size,
                         pEntryHeader->status, ui32Offset);
              }
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: T35 (missing payload); type %u size %u "
                       "status %u offset %u\n",
                       __func__, pEntryHeader->type, pEntryHeader->size,
                       pEntryHeader->status, ui32Offset);
            }
            break;

          case 5: // HEVC_SEI_TYPE_USER_DATA_UNREGISTERED
            // set offset now so len=0 will signify an error if this SEI is dropped
            p_frame->sei_user_data_unreg_offset = video_data_size + metadata_size + ui32Offset;
            if (ui32Size)
            {
              p_frame->sei_user_data_unreg_len = ui32Size;
              p_frame->sei_total_len += ui32Size;
              ni_log(NI_LOG_DEBUG, "User Data Unreg size = %u\n", ui32Size);
            }
            else
            {
              p_frame->sei_user_data_unreg_len = 0; // in case there are multiple UDU SEI entries
              ni_log(NI_LOG_ERROR,
                     "Error %s: User Data Unreg dropped (missing payload); "
                     "type %u size %u status %u offset %u\n",
                     __func__, pEntryHeader->type, pEntryHeader->size,
                     pEntryHeader->status, ui32Offset);
            }
            break;

          case 137: //HEVC_SEI_TYPE_MASTERING_DISPLAY_INFO
            if (ui32Size)
            {
              p_frame->sei_hdr_mastering_display_color_vol_len = ui32Size;
              p_frame->sei_hdr_mastering_display_color_vol_offset =
              video_data_size + metadata_size + ui32Offset;

              p_frame->sei_total_len += ui32Size;
              ni_dec_mastering_display_colour_volume_bytes_t *pColourVolume =
                  (ni_dec_mastering_display_colour_volume_bytes_t
                       *)((uint8_t *)p_meta + metadata_size + ui32Offset);

              ni_log(NI_LOG_DEBUG, "Display Primaries x[0]=%u y[0]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[0][0]),
                             ni_ntohs(pColourVolume->display_primaries[0][1]));
              ni_log(NI_LOG_DEBUG, "Display Primaries x[1]=%u y[1]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[1][0]),
                             ni_ntohs(pColourVolume->display_primaries[1][1]));
              ni_log(NI_LOG_DEBUG, "Display Primaries x[2]=%u y[2]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[2][0]),
                             ni_ntohs(pColourVolume->display_primaries[2][1]));

              ni_log(NI_LOG_DEBUG, "White Point x=%u y=%u\n",
                             ni_ntohs(pColourVolume->white_point_x),
                             ni_ntohs(pColourVolume->white_point_y));
              ni_log(NI_LOG_DEBUG, "Display Mastering Lum, Max=%u Min=%u\n",
                             ni_ntohl(pColourVolume->max_display_mastering_luminance),
                             ni_ntohl(pColourVolume->min_display_mastering_luminance));
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: mastering display info dropped (missing "
                       "payload); type %u size %u status %u offset %u\n",
                       __func__, pEntryHeader->type, pEntryHeader->size,
                       pEntryHeader->status, ui32Offset);
            }
            break;

          case 144: //HEVC_SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO
            if (ui32Size)
            {
              p_frame->sei_hdr_content_light_level_info_len = ui32Size;
              p_frame->sei_hdr_content_light_level_info_offset =
              video_data_size + metadata_size + ui32Offset;

              p_frame->sei_total_len += ui32Offset;

              ni_content_light_level_info_bytes_t* pLightLevel =
                (ni_content_light_level_info_bytes_t*)(
                  (uint8_t*)p_meta + metadata_size + ui32Offset);

              ni_log(NI_LOG_DEBUG, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
                             ni_ntohs(pLightLevel->max_content_light_level),
                             ni_ntohs(pLightLevel->max_pic_average_light_level));
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: content light level info dropped (missing "
                       "payload); type %u size %u status %u offset %u\n",
                       __func__, pEntryHeader->type, pEntryHeader->size,
                       pEntryHeader->status, ui32Offset);
            }
            break;

          default:
              ni_log(NI_LOG_ERROR,
                     "Error %s: SEI message dropped (unsupported - check "
                     "decoder SEI bitmap settings);"
                     " type %u size %u status %u offset %u payload bytes %u\n",
                     __func__, pEntryHeader->type, pEntryHeader->size,
                     pEntryHeader->status, ui32Offset, ui32Size);
              break;
        }
        ui32Offset += ui32Size;
        pEntryHeader = (ni_sei_header_t *)((uint8_t*)p_meta + metadata_size + ui32Offset);
        ui32Offset += sizeof(ni_sei_header_t);
      } while (ui32Offset <= p_meta->sei_size && ui16SeiProcessed < p_meta->sei_number);

      if (p_meta->sei_number != ui16SeiProcessed)
      {
          ni_log(
              NI_LOG_ERROR,
              "Error %s: number of SEI messages reported %u != processed %u\n",
              __func__, p_meta->sei_number, ui16SeiProcessed);
      }
     }

#else // QUADRA_SEI_FMT

      // ni_assert(p_meta->sei_header);
      // else // backward compatibility
     {
      ni_sei_user_data_entry_t *pEntry;
      uint32_t ui32CCOffset = 0, ui32CCSize = 0;

      rx_size -= p_meta->sei_size;

      pEntry = (ni_sei_user_data_entry_t *)((uint8_t*)p_meta + metadata_size);

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_HDR10_PLUS, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_plus_len = ui32CCSize;
        p_frame->sei_hdr_plus_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_DEBUG, "%s: hdr10+ size=%u hdr10+ offset=%u\n", __func__,
               p_frame->sei_hdr_plus_len, p_frame->sei_hdr_plus_offset);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: hdr+ NOT found in meta data!\n", __func__);
      }

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_CLOSED_CAPTION, pEntry,
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

        ni_log(NI_LOG_DEBUG,
               "%s: close caption size %u ,"
               "offset %u = video size %u meta size %u off "
               " %u + 10\n",
               __func__, p_frame->sei_cc_len, p_frame->sei_cc_offset,
               video_data_size, metadata_size, ui32CCOffset);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: close caption NOT found in meta data!\n",
                 __func__);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_MASTERING_COLOR_VOL,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_mastering_display_color_vol_len = ui32CCSize;
        p_frame->sei_hdr_mastering_display_color_vol_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_dec_mastering_display_colour_volume_t* pColourVolume =
        (ni_dec_mastering_display_colour_volume_t*)((uint8_t*)pEntry + ui32CCOffset);

        ni_log(NI_LOG_DEBUG, "Display Primaries x[0]=%u y[0]=%u\n",
                       pColourVolume->display_primaries_x[0],
                       pColourVolume->display_primaries_y[0]);
        ni_log(NI_LOG_DEBUG, "Display Primaries x[1]=%u y[1]=%u\n",
                       pColourVolume->display_primaries_x[1],
                       pColourVolume->display_primaries_y[1]);
        ni_log(NI_LOG_DEBUG, "Display Primaries x[2]=%u y[2]=%u\n",
                       pColourVolume->display_primaries_x[2],
                       pColourVolume->display_primaries_y[2]);

        ni_log(NI_LOG_DEBUG, "White Point x=%u y=%u\n",
                       pColourVolume->white_point_x,
                       pColourVolume->white_point_y);
        ni_log(NI_LOG_DEBUG, "Display Mastering Lum, Max=%u Min=%u\n",
                       pColourVolume->max_display_mastering_luminance, pColourVolume->min_display_mastering_luminance);
      }
      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_content_light_level_info_len = ui32CCSize;
        p_frame->sei_hdr_content_light_level_info_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_content_light_level_info_t* pLightLevel =
        (ni_content_light_level_info_t*)((uint8_t*)pEntry + ui32CCOffset);
        ni_log(NI_LOG_DEBUG, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
                       pLightLevel->max_content_light_level, pLightLevel->max_pic_average_light_level);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_PRE,
                   &ui32CCOffset, &ui32CCSize) ||
          find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_SUF,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_user_data_unreg_len = ui32CCSize;
        p_frame->sei_user_data_unreg_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_DEBUG, "User Data Unreg size = %u\n", ui32CCSize);
      }
     }
#endif // QUADRA_SEI_FMT
      if (0 == p_frame->sei_total_len)
      {
        ni_log(NI_LOG_DEBUG, "Warning retrieved 0 supported SEI !\n");
      }
        }
    }

    p_frame->dts = NI_NOPTS_VALUE;
    p_frame->pts = NI_NOPTS_VALUE;
    //p_frame->end_of_stream = isEndOfStream;
    p_frame->start_of_stream = 0;

    if (rx_size == 0)
    {
        p_frame->data_len[0] = 0;
        p_frame->data_len[1] = 0;
        p_frame->data_len[2] = 0;
        p_frame->data_len[3] = 0;
    }

  ni_log(NI_LOG_DEBUG, "received [0x%08x] data size: %d, end of stream=%u\n",
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
void ni_populate_device_capability_struct(ni_device_capability_t* p_cap, void* p_data)
{
    int i, total_types = 0, total_modules = 0;
    ni_nvme_identity_t *p_id_data = (ni_nvme_identity_t *)p_data;

    COMPILE_ASSERT(sizeof(p_cap->xcoder_cnt) <= sizeof(p_id_data->xcoder_cnt) &&
                   NI_DEVICE_TYPE_XCODER_MAX *
                           sizeof(p_id_data->xcoder_cnt[0]) ==
                       sizeof(p_cap->xcoder_cnt));

    if (!p_cap || !p_data)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
               __func__);
        LRETURN;
    }

  if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
    (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
  {
    LRETURN;
  }

  memcpy(p_cap->serial_number, p_id_data->ai8Sn, sizeof(p_cap->serial_number));
  memcpy(p_cap->model_number, p_id_data->ai8Mn, sizeof(p_cap->model_number));

  memset(p_cap->fw_rev, 0, sizeof(p_cap->fw_rev));
  memcpy(p_cap->fw_rev, p_id_data->ai8Fr, sizeof(p_cap->fw_rev));
  ni_log(NI_LOG_DEBUG, "F/W rev: %.*s\n", (int)sizeof(p_cap->fw_rev),
                 p_cap->fw_rev);

  if (p_id_data->xcoder_num_elements)
  {
      ni_log(NI_LOG_DEBUG, "xcoder_num_elements: %d xcoder_num_devices: %d\n",
                     p_id_data->xcoder_num_elements,
                     p_id_data->xcoder_num_devices);

      for (i = 0; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
      {
          if (p_id_data->xcoder_cnt[i])
          {
              total_types++;
              total_modules += p_id_data->xcoder_cnt[i];
              ni_log(NI_LOG_DEBUG, "type #%d: xcoder_cnt[%d] = %d\n", total_types, i,
                             p_id_data->xcoder_cnt[i]);
          }
      }

      if (p_id_data->xcoder_num_elements != total_types ||
          p_id_data->xcoder_num_devices != total_modules)
      {
          ni_log(NI_LOG_ERROR,
                 "Error: mismatch; xcoder_num_elements: %d (calculated: %d) "
                 "xcoder_num_devices: %d (calculated: %d)\n",
                 p_id_data->xcoder_num_elements, total_types,
                 p_id_data->xcoder_num_devices, total_modules);
          LRETURN;
      }

      p_cap->device_is_xcoder = NI_XCODER_QUADRA;   // data format version 2
      p_cap->hw_elements_cnt = p_id_data->xcoder_num_elements;
      p_cap->xcoder_devices_cnt = p_id_data->xcoder_num_devices;
      memcpy(p_cap->xcoder_cnt, p_id_data->xcoder_cnt,
             NI_DEVICE_TYPE_XCODER_MAX * sizeof(p_id_data->xcoder_cnt[0]));

      for (i = 0; i < total_modules; i++)
      {
          p_cap->xcoder_devices[i].hw_id = p_id_data->xcoder_devices[i].hw_id;
          p_cap->xcoder_devices[i].max_number_of_contexts =
              NI_MAX_CONTEXTS_PER_HW_INSTANCE;
          p_cap->xcoder_devices[i].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
          p_cap->xcoder_devices[i].codec_format =
              p_id_data->xcoder_devices[i].hw_codec_format;
          p_cap->xcoder_devices[i].codec_type =
              p_id_data->xcoder_devices[i].hw_codec_type;
          p_cap->xcoder_devices[i].max_video_width = NI_PARAM_MAX_WIDTH;
          p_cap->xcoder_devices[i].max_video_height = NI_PARAM_MAX_HEIGHT;
          p_cap->xcoder_devices[i].min_video_width = NI_PARAM_MIN_WIDTH;
          p_cap->xcoder_devices[i].min_video_height = NI_PARAM_MIN_HEIGHT;
          p_cap->xcoder_devices[i].video_profile =
              p_id_data->xcoder_devices[i].hw_video_profile;
          p_cap->xcoder_devices[i].video_level =
              p_id_data->xcoder_devices[i].hw_video_level;
      }

      goto CAP_POPULATED;
  }

  p_cap->device_is_xcoder = p_id_data->device_is_xcoder;
  ni_log(NI_LOG_DEBUG, "device_is_xcoder: value in id cmd: %u\n",
         p_cap->device_is_xcoder);
  if (0 == p_cap->device_is_xcoder)
  {
    LRETURN;
  }

  p_cap->hw_elements_cnt = p_id_data->xcoder_num_hw;

  total_modules = p_cap->xcoder_cnt[NI_DEVICE_TYPE_DECODER] =
      p_id_data->xcoder_num_h264_decoder_hw +
      p_id_data->xcoder_num_h265_decoder_hw;

  p_cap->xcoder_cnt[NI_DEVICE_TYPE_ENCODER] =
      p_id_data->xcoder_num_h264_encoder_hw +
      p_id_data->xcoder_num_h265_encoder_hw;

  total_modules += p_cap->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];

  p_cap->xcoder_devices_cnt = total_modules;

  if (total_modules >= 1)
  {
    p_cap->xcoder_devices[0].hw_id = p_id_data->hw0_id;
    p_cap->xcoder_devices[0].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[0].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[0].codec_format = p_id_data->hw0_codec_format;
    p_cap->xcoder_devices[0].codec_type = p_id_data->hw0_codec_type;
    p_cap->xcoder_devices[0].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[0].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[0].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[0].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[0].video_profile = p_id_data->hw0_video_profile;
    p_cap->xcoder_devices[0].video_level = p_id_data->hw0_video_level;
  }
  if (total_modules >= 2)
  {
    p_cap->xcoder_devices[1].hw_id = p_id_data->hw1_id;
    p_cap->xcoder_devices[1].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[1].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[1].codec_format = p_id_data->hw1_codec_format;
    p_cap->xcoder_devices[1].codec_type = p_id_data->hw1_codec_type;
    p_cap->xcoder_devices[1].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[1].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[1].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[1].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[1].video_profile = p_id_data->hw1_video_profile;
    p_cap->xcoder_devices[1].video_level = p_id_data->hw1_video_level;
  }
  if (total_modules >= 3)
  {
    p_cap->xcoder_devices[2].hw_id = p_id_data->hw2_id;
    p_cap->xcoder_devices[2].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[2].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[2].codec_format = p_id_data->hw2_codec_format;
    p_cap->xcoder_devices[2].codec_type = p_id_data->hw2_codec_type;
    p_cap->xcoder_devices[2].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[2].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[2].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[2].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[2].video_profile = p_id_data->hw2_video_profile;
    p_cap->xcoder_devices[2].video_level = p_id_data->hw2_video_level;
  }
  if (total_modules >= 4)
  {
    p_cap->xcoder_devices[3].hw_id = p_id_data->hw3_id;
    p_cap->xcoder_devices[3].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[3].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[3].codec_format = p_id_data->hw3_codec_format;
    p_cap->xcoder_devices[3].codec_type = p_id_data->hw3_codec_type;
    p_cap->xcoder_devices[3].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[3].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[3].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[3].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[3].video_profile = p_id_data->hw3_video_profile;
    p_cap->xcoder_devices[3].video_level = p_id_data->hw3_video_level;
  }

CAP_POPULATED:

    for (i = 0; i < NI_MAX_DEVICES_PER_HW_INSTANCE; i++)
    {
        ni_log(NI_LOG_DEBUG, "HW%d hw_id: %d\n", i, p_cap->xcoder_devices[i].hw_id);
        ni_log(NI_LOG_DEBUG, "HW%d max_number_of_contexts: %d\n", i,
                       p_cap->xcoder_devices[i].max_number_of_contexts);
        ni_log(NI_LOG_DEBUG, "HW%d max_4k_fps: %d\n", i,
               p_cap->xcoder_devices[i].max_4k_fps);
        ni_log(NI_LOG_DEBUG, "HW%d codec_format: %d\n", i,
                       p_cap->xcoder_devices[i].codec_format);
        ni_log(NI_LOG_DEBUG, "HW%d codec_type: %d\n", i,
                       p_cap->xcoder_devices[i].codec_type);
        ni_log(NI_LOG_DEBUG, "HW%d max_video_width: %d\n", i,
                       p_cap->xcoder_devices[i].max_video_width);
        ni_log(NI_LOG_DEBUG, "HW%d max_video_height: %d\n", i,
                       p_cap->xcoder_devices[i].max_video_height);
        ni_log(NI_LOG_DEBUG, "HW%d min_video_width: %d\n", i,
                       p_cap->xcoder_devices[i].min_video_width);
        ni_log(NI_LOG_DEBUG, "HW%d min_video_height: %d\n", i,
                       p_cap->xcoder_devices[i].min_video_height);
        ni_log(NI_LOG_DEBUG, "HW%d video_profile: %d\n", i,
                       p_cap->xcoder_devices[i].video_profile);
        ni_log(NI_LOG_DEBUG, "HW%d video_level: %d\n", i,
                       p_cap->xcoder_devices[i].video_level);
    }

    memset(p_cap->fw_branch_name, 0, sizeof(p_cap->fw_branch_name));
    memcpy(p_cap->fw_branch_name, p_id_data->fw_branch_name,
           sizeof(p_cap->fw_branch_name) - 1);
    ni_log(NI_LOG_DEBUG, "F/W branch name: %s\n", p_cap->fw_branch_name);
    memset(p_cap->fw_commit_time, 0, sizeof(p_cap->fw_commit_time));
    memcpy(p_cap->fw_commit_time, p_id_data->fw_commit_time,
           sizeof(p_cap->fw_commit_time) - 1);
    ni_log(NI_LOG_DEBUG, "F/W commit time: %s\n", p_cap->fw_commit_time);
    memset(p_cap->fw_commit_hash, 0, sizeof(p_cap->fw_commit_hash));
    memcpy(p_cap->fw_commit_hash, p_id_data->fw_commit_hash,
           sizeof(p_cap->fw_commit_hash) - 1);
    ni_log(NI_LOG_DEBUG, "F/W commit hash: %s\n", p_cap->fw_commit_hash);
    memset(p_cap->fw_build_time, 0, sizeof(p_cap->fw_build_time));
    memcpy(p_cap->fw_build_time, p_id_data->fw_build_time,
           sizeof(p_cap->fw_build_time) - 1);
    ni_log(NI_LOG_DEBUG, "F/W build time: %s\n", p_cap->fw_build_time);
    memset(p_cap->fw_build_id, 0, sizeof(p_cap->fw_build_id));
    memcpy(p_cap->fw_build_id, p_id_data->fw_build_id,
           sizeof(p_cap->fw_build_id) - 1);
    ni_log(NI_LOG_DEBUG, "F/W build id: %s\n", p_cap->fw_build_id);

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
 *  \brief  insert the 32 bits of integer value at bit position pos
 *
 *  \param int pos, int value
 *
 *  \return void
 ******************************************************************************/
void ni_fix_VUI(uint8_t *vui, int pos, int value)
{
  int pos_byte    = (pos/8);
  int pos_in_byte = pos%8;
  int remaining_bytes_in_current_byte = 8 - pos_in_byte;

  if (pos_in_byte == 0) // at beginning of the byte
  {
    vui[pos_byte] = (uint8_t)(value >> 24);
    vui[pos_byte+1] = (uint8_t)(value >> 16);
    vui[pos_byte+2] = (uint8_t)(value >> 8);
    vui[pos_byte+3] = (uint8_t)(value);
  }
  else
  {
    vui[pos_byte]   = vui[pos_byte] + (uint8_t)(value >> (32-remaining_bytes_in_current_byte));
    vui[pos_byte+1] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-8));
    vui[pos_byte+2] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-16));
    vui[pos_byte+3] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-24));
    vui[pos_byte+4] = vui[pos_byte+4] + ((uint8_t)(value << remaining_bytes_in_current_byte));
  }

}

/*!******************************************************************************
*  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
*
*  \param
*
*  \return
******************************************************************************/
void ni_set_custom_dec_template(ni_session_context_t *p_ctx,
                                ni_decoder_config_t *p_cfg,
                                ni_xcoder_params_t *p_src,
                                uint32_t max_pkt_size)
{
  int i,j;
  ni_decoder_input_params_t* p_dec = &p_src->dec_input_params;
  if ((!p_ctx) || (!p_cfg) || (!p_src))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      return;
  }
  int w = p_src->source_width;
  int h = p_src->source_height;
  bool shift_params = false;

  p_cfg->ui8HWFrame = p_dec->hwframes;
  p_cfg->ui8MCMode = p_dec->mcmode;
  p_cfg->ui8UduSeiEnabled = p_ctx->enable_user_data_sei_passthru;
  p_cfg->ui16MaxSeiDataSize = NI_MAX_SEI_DATA;

  if (max_pkt_size)
  {
    p_cfg->ui32MaxPktSize = max_pkt_size;
  } else {
    // p_cfg->ui32MaxPktSize = width x height x 3/2 x min compression ratio(QP=0);
    // temp: set min compression ratio = 1/2, so MaxPktSize = w * h * 3/4
    p_cfg->ui32MaxPktSize = w * h * 3 / 4;
  }
  // packet buffer aligned to NI_MAX_PACKET_SZ(128k)
  p_cfg->ui32MaxPktSize =
      (((p_cfg->ui32MaxPktSize) / NI_MAX_PACKET_SZ) + 1) * NI_MAX_PACKET_SZ;

  p_cfg->fps_number =
      ((ni_xcoder_params_t *)p_ctx->p_session_config)->fps_number;
  p_cfg->fps_denominator =
      ((ni_xcoder_params_t *)p_ctx->p_session_config)->fps_denominator,

  p_cfg->asOutputConfig[0].ui8Enabled = 1;   // always enabled
  p_cfg->asOutputConfig[1].ui8Enabled = p_dec->enable_out1;
  p_cfg->asOutputConfig[2].ui8Enabled = p_dec->enable_out2;
  if (p_cfg->asOutputConfig[2].ui8Enabled && p_cfg->asOutputConfig[1].ui8Enabled == 0)
  {
    p_cfg->asOutputConfig[1].ui8Enabled = 1;
    p_cfg->asOutputConfig[2].ui8Enabled = 0;
    shift_params = true;
    ni_log(NI_LOG_DEBUG, "Output 2 used before output 1, Shifting output2 settings to output1 and disabling output 2\n");
  }

  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    if (!shift_params || i == 0)
    {
      j = i;
    }
    else
    {
      j = (i == 1) ? 2 : 1; //swap settings
    }
    p_cfg->asOutputConfig[i].ui8Force8Bit = p_dec->force_8_bit[j];
    p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled = p_dec->semi_planar[j];
    p_cfg->asOutputConfig[i].ui8CropMode = p_dec->crop_mode[j];
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16W =
        (uint16_t)((p_dec->crop_whxy[j][0]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16H =
        (uint16_t)((p_dec->crop_whxy[j][1]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16X =
        (uint16_t)((p_dec->crop_whxy[j][2]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y =
        (uint16_t)((p_dec->crop_whxy[j][3]) & 0xFFFE);

    //Offset resized if out of bounds
    if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16X + p_cfg->asOutputConfig[i].sCroppingRectable.ui16W > w)
    {
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16X = w - p_cfg->asOutputConfig[i].sCroppingRectable.ui16W;
    }
    if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y + p_cfg->asOutputConfig[i].sCroppingRectable.ui16H > h)
    {
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y = h - p_cfg->asOutputConfig[i].sCroppingRectable.ui16H;
    }

    p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width =
        (uint16_t)((p_dec->scale_wh[j][0]+1) & 0xFFFE);
    p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height =
        (uint16_t)((p_dec->scale_wh[j][1]+1) & 0xFFFE);
    if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width ||
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height)
    {
      p_cfg->asOutputConfig[i].ui8ScaleEnabled = 1;
    }
    else
    {
      p_cfg->asOutputConfig[i].ui8ScaleEnabled = 0;
    }
  }

  //print it all out
  ni_log(NI_LOG_DEBUG, "ui8HWFrame = %d\n", p_cfg->ui8HWFrame);
  ni_log(NI_LOG_DEBUG, "ui8MCMode = %d\n", p_cfg->ui8MCMode);
  ni_log(NI_LOG_DEBUG, "ui8UduSeiEnabled = %d\n", p_cfg->ui8UduSeiEnabled);
  ni_log(NI_LOG_DEBUG, "ui16MaxSeiDataSize = %d\n", p_cfg->ui16MaxSeiDataSize);
  ni_log(NI_LOG_DEBUG, "ui8Enabled0 = %d\n", p_cfg->asOutputConfig[0].ui8Enabled);
  ni_log(NI_LOG_DEBUG, "ui8Enabled1 = %d\n", p_cfg->asOutputConfig[1].ui8Enabled);
  ni_log(NI_LOG_DEBUG, "ui8Enabled2 = %d\n", p_cfg->asOutputConfig[2].ui8Enabled);
  ni_log(NI_LOG_DEBUG, "ui32MaxPktSize = %u\n", p_cfg->ui32MaxPktSize);
  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    ni_log(NI_LOG_DEBUG, "[%d] ui8Force8Bit %d\n", i, p_cfg->asOutputConfig[i].ui8Force8Bit);
    ni_log(NI_LOG_DEBUG, "[%d] ui8SemiPlanarEnabled %d\n", i, p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled);
    ni_log(NI_LOG_DEBUG, "[%d] ui8CropMode %d\n", i, p_cfg->asOutputConfig[i].ui8CropMode);
    ni_log(NI_LOG_DEBUG, "[%d] sCroppingRectable.ui16XYWH %d,%d - %d x %d\n", i,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16X,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16W,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16H);
    ni_log(NI_LOG_DEBUG, "[%d] sOutputPictureSize.ui16Width x height %d x %d\n", i,
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width,
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height);
    ni_log(NI_LOG_DEBUG, "[%d] ui8ScaleEnabled %d\n", i, p_cfg->asOutputConfig[i].ui8ScaleEnabled);
  }

}


/*!******************************************************************************
 *  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_custom_template(ni_session_context_t *p_ctx,
                            ni_encoder_config_t *p_cfg,
                            ni_xcoder_params_t *p_src)
{

  ni_t408_config_t* p_t408 = &(p_cfg->niParamT408);
  ni_encoder_cfg_params_t *p_enc = &p_src->cfg_enc_params;
  int i = 0;

  if ((!p_ctx) || (!p_cfg) || (!p_src))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      return;
  }

  ni_set_default_template(p_ctx, p_cfg);

    p_cfg->i32picWidth = p_src->source_width;
    p_cfg->i32picHeight = p_src->source_height;
    p_t408->gop_preset_index = p_enc->gop_preset_index;
    p_t408->use_recommend_enc_params = p_enc->use_recommend_enc_params;
    p_t408->cu_size_mode = p_enc->cu_size_mode;
    p_t408->max_num_merge = p_enc->max_num_merge;

  // enable_dynamic_8x8_merge, enable_dynamic_16x16_merge, enable_dynamic_32x32_merge,
  // trans_rate, enable_hvs_qp_scale:
  // are not present in Rev B p_config

    p_cfg->ui8rcEnable = p_enc->rc.enable_rate_control;

  if (p_src->bitrate != 0)
  {
    p_cfg->i32bitRate = p_src->bitrate;
  }

    p_t408->enable_cu_level_rate_control = p_enc->rc.enable_cu_level_rate_control;
    p_t408->enable_hvs_qp = p_enc->rc.enable_hvs_qp;
    p_t408->hvs_qp_scale = p_enc->rc.hvs_qp_scale;
    p_t408->minQpI = p_enc->rc.min_qp;
    p_t408->minQpP = p_enc->rc.min_qp;
    p_t408->minQpB = p_enc->rc.min_qp;
    p_t408->maxQpI = p_enc->rc.max_qp;
    p_t408->maxQpP = p_enc->rc.max_qp;
    p_t408->maxQpB = p_enc->rc.max_qp;

  // TBD intraMinQp and intraMaxQp are not configurable in Rev A; should it
  // be in Rev B?

    p_t408->max_delta_qp = p_enc->rc.max_delta_qp;
    p_cfg->i32vbvBufferSize = p_enc->rc.vbv_buffer_size;
    p_cfg->i8intraQpDelta = p_enc->rc.intra_qp_delta;
    p_cfg->ui8fillerEnable = p_enc->rc.enable_filler;
    p_cfg->ui8picSkipEnable = p_enc->rc.enable_pic_skip;
    p_cfg->ui16maxFrameSize = p_enc->maxFrameSize / 2000;
    p_t408->intra_period = p_enc->intra_period;
    p_t408->roiEnable = p_enc->roi_enable;
    p_t408->useLongTerm = p_enc->long_term_ref_enable;
    if (QUADRA)
    {
        p_cfg->ui32setLongTermInterval = p_enc->long_term_ref_interval;
        p_cfg->ui8setLongTermCount = p_enc->long_term_ref_count;
    }
    p_t408->conf_win_top = p_enc->conf_win_top;
    p_t408->conf_win_bottom = p_enc->conf_win_bottom;
    p_t408->conf_win_left = p_enc->conf_win_left;
    p_t408->conf_win_right = p_enc->conf_win_right;
    p_t408->avcIdrPeriod = p_enc->intra_period;

  if (QUADRA)
  {
    if (p_cfg->i32frameRateInfo != p_enc->frame_rate)
    {
      p_cfg->i32frameRateInfo = p_enc->frame_rate;
      p_cfg->i32frameRateDenominator = 1;
      if (p_src->fps_denominator != 0 &&
          (p_src->fps_number % p_src->fps_denominator) != 0)
      {
          uint32_t numUnitsInTick = 1000;
          p_cfg->i32frameRateDenominator = numUnitsInTick + 1;
          p_cfg->i32frameRateInfo += 1;
          p_cfg->i32frameRateInfo *= numUnitsInTick;
      }
    }
  }
  else
  {
    if (p_cfg->i32frameRateInfo != p_enc->frame_rate)
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
      if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
      {
        p_t408->timeScale *= 2;
      }
    }
  }

    p_t408->intra_qp = p_enc->rc.intra_qp;

  // "repeatHeaders" value 1 (all I frames) maps to forcedHeaderEnable
  // value 2; all other values are ignored
  if (p_t408->forcedHeaderEnable != p_enc->forced_header_enable &&
    p_enc->forced_header_enable == NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES)
  {
    p_t408->forcedHeaderEnable = 2;
    p_cfg->ui8repeatHeaders = p_enc->forced_header_enable;
  }

    p_t408->decoding_refresh_type = p_enc->decoding_refresh_type;

  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    switch (p_t408->decoding_refresh_type)
    {
    case 0: // Non-IRAP I-p_frame
    {
      // intra_period set to user-configured (above), avcIdrPeriod set to 0
      p_t408->avcIdrPeriod = 0;
      break;
    }
    case 1: // CRA
    case 2: // IDR
    {
      // intra_period set to 0, avcIdrPeriod set to user-configured (above)
      p_t408->intra_period = 0;
      break;
    }
    default:
    {
        ni_log(
            NI_LOG_ERROR,
            "ERROR: %s() unknown value for p_t408->decoding_refresh_type: %d\n",
            __func__, p_t408->decoding_refresh_type);
        break;
    }
    }
  } else if (STD_HEVC == p_cfg->ui8bitstreamFormat ||
             STD_AV1 == p_cfg->ui8bitstreamFormat)
  {
    p_t408->avcIdrPeriod = 0;
  }

  // Rev. B: H.264 only parameters.
    p_t408->enable_transform_8x8 = p_enc->enable_transform_8x8;
    p_t408->avc_slice_mode = p_enc->avc_slice_mode;
    p_t408->avc_slice_arg = p_enc->avc_slice_arg;
    p_t408->entropy_coding_mode = p_enc->entropy_coding_mode;

  // Rev. B: shared between HEVC and H.264
  if (p_t408->intra_mb_refresh_mode != p_enc->intra_mb_refresh_mode)
  {
    p_t408->intra_mb_refresh_mode = p_enc->intra_mb_refresh_mode;
    if (1 != p_t408->intra_mb_refresh_mode)
    {
      p_t408->intra_mb_refresh_mode = 1;
      ni_log(NI_LOG_DEBUG, "force intraRefreshMode to 1 because quadra only supports intra refresh by rows\n");
    }
  }

  if (p_t408->intra_mb_refresh_arg != p_enc->intra_mb_refresh_arg)
  {
    p_t408->intra_mb_refresh_arg = p_enc->intra_mb_refresh_arg;
    if (1 == p_t408->intra_mb_refresh_mode)
    {
      p_cfg->ui16gdrDuration = (p_cfg->i32picHeight / ((p_cfg->ui8bitstreamFormat == STD_AVC) ? 16 : 64)) / p_t408->intra_mb_refresh_arg;
      if (!p_cfg->ui16gdrDuration)
          p_cfg->ui16gdrDuration = p_cfg->i32picHeight / ((p_cfg->ui8bitstreamFormat == STD_AVC) ? 16 : 64);
    }
  }

  // TBD Rev. B: could be shared for HEVC and H.264
    p_t408->enable_mb_level_rc = p_enc->rc.enable_mb_level_rc;

  // profile setting: if user specified profile
  if (0 != p_enc->profile)
  {
    p_t408->profile = p_enc->profile;
  }

  p_t408->level = p_enc->level_idc;

  // main, extended or baseline profile of 8 bit (if input is 10 bit, Quadra auto converts to 8 bit) H.264 requires the following:
  // main:     profile = 2  transform8x8Enable = 0
  // extended: profile = 3  entropyCodingMode = 0, transform8x8Enable = 0
  // baseline: profile = 1  entropyCodingMode = 0, transform8x8Enable = 0 and
  //                        gop with no B frames (gopPresetIdx=1, 2, 6, or 0
  //                        (custom with no B frames)
  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    if (2 == p_t408->profile)
    {
      p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_DEBUG, "enable_transform_8x8 set to 0 for profile 2 (main)\n");
    }
    else if (3 == p_t408->profile || 1 == p_t408->profile)
    {
      p_t408->entropy_coding_mode = p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_DEBUG, "entropy_coding_mode and enable_transform_8x8 set to 0 "
        "for profile 3 (extended) or 1 (baseline)\n");
    }
  }

  if (QUADRA)
  {
    if (0 == p_t408->entropy_coding_mode && 1 == p_enc->EnableRdoQuant)
    {
      ni_log(NI_LOG_DEBUG, "RDOQ does not support entropy_coding_mode 0 (CAVLC) "
        "force EnableRdoQuant 0 to accommodate HW limiation\n");
      p_enc->EnableRdoQuant = 0;
    }
  }

#ifndef QUADRA
  if (!QUADRA)
  {
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
  }
  else // QUADRA
#endif
  {
      if (p_enc->custom_gop_params.custom_gop_size &&
          (GOP_PRESET_IDX_CUSTOM == p_t408->gop_preset_index ||
           GOP_PRESET_IDX_DEFAULT == p_t408->gop_preset_index))
      {
          p_t408->custom_gop_params.custom_gop_size =
              p_enc->custom_gop_params.custom_gop_size;
          for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
          {
              p_t408->custom_gop_params.pic_param[i].poc_offset =
                  p_enc->custom_gop_params.pic_param[i].poc_offset;
              p_t408->custom_gop_params.pic_param[i].qp_offset =
                  p_enc->custom_gop_params.pic_param[i].qp_offset;
              p_t408->custom_gop_params.pic_param[i].qp_factor =
                  p_enc->custom_gop_params.pic_param[i].qp_factor;
              p_t408->custom_gop_params.pic_param[i].temporal_id =
                  p_enc->custom_gop_params.pic_param[i].temporal_id;
              p_t408->custom_gop_params.pic_param[i].pic_type =
                  p_enc->custom_gop_params.pic_param[i].pic_type;
              p_t408->custom_gop_params.pic_param[i].num_ref_pics =
                  p_enc->custom_gop_params.pic_param[i].num_ref_pics;
              for (int j = 0;
                   j < p_enc->custom_gop_params.pic_param[i].num_ref_pics; j++)
              {
                  p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic =
                      p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic;
                  p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic_used =
                      p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic_used;
              }
          }
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

    p_cfg->hdrEnableVUI = p_src->hdrEnableVUI;

  if (p_cfg->i32hwframes != p_src->hwframes)
  {
    if (p_src->hwframes && p_ctx->auto_dl_handle == 0)
    {
      p_cfg->i32hwframes = p_src->hwframes;
    }
    else
    {
      p_cfg->i32hwframes = 0;
    }
  }
    p_cfg->ui16rootBufId = p_src->rootBufId;

  //set VUI info
  p_cfg->ui32VuiDataSizeBits = p_src->ui32VuiDataSizeBits;
  p_cfg->ui32VuiDataSizeBytes = p_src->ui32VuiDataSizeBytes;
  memcpy(p_cfg->ui8VuiRbsp, p_src->ui8VuiRbsp, NI_MAX_VUI_SIZE);
  if ((p_src->pos_num_units_in_tick > p_src->ui32VuiDataSizeBits) || (p_src->pos_time_scale > p_src->ui32VuiDataSizeBits))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() VUI filling error\n", __func__);
      return;
  }
  else
  {
    ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_num_units_in_tick, p_t408->numUnitsInTick);
    ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_time_scale, p_t408->timeScale);
  }
  if (p_src->enable_vfr)
  {
      p_cfg->ui8fixedframerate = 0;
  } else
  {
      p_cfg->ui8fixedframerate = 1;
  }

  //new QUADRA param
  if (p_enc->EnableAUD != 0)
  {
    p_cfg->ui8EnableAUD = p_enc->EnableAUD;
  }
  if (p_enc->lookAheadDepth != 0)
  {
    p_cfg->ui8LookAheadDepth = p_enc->lookAheadDepth;
  }
  if (p_enc->rdoLevel != 1)
  {
    p_cfg->ui8rdoLevel = p_enc->rdoLevel;
  }
  if (p_enc->crf != -1)
  {
    p_cfg->i8crf = p_enc->crf;
  }
  if (p_enc->HDR10MaxLight != 0)
  {
    p_cfg->ui16HDR10MaxLight = p_enc->HDR10MaxLight;
  }
  if (p_enc->HDR10AveLight != 0)
  {
    p_cfg->ui16HDR10AveLight = p_enc->HDR10AveLight;
  }
  if (p_enc->HDR10CLLEnable != 0)
  {
      p_cfg->ui8HDR10CLLEnable = p_enc->HDR10CLLEnable;
  }
  if (p_enc->EnableRdoQuant != 0)
  {
    p_cfg->ui8EnableRdoQuant = p_enc->EnableRdoQuant;
  }
  if (p_enc->ctbRcMode != 0)
  {
    p_cfg->ui8ctbRcMode = p_enc->ctbRcMode;
  }
  if (p_enc->gopSize != 0)
  {
    p_cfg->ui8gopSize = p_enc->gopSize;
  }
  if (p_src->use_low_delay_poc_type != 0)
  {
    p_cfg->ui8useLowDelayPocType = p_src->use_low_delay_poc_type;
  }
  if (p_enc->gopLowdelay != 0)
  {
    p_cfg->ui8gopLowdelay = p_enc->gopLowdelay;
  }
  if (p_enc->gdrDuration != 0)
  {
    p_cfg->ui16gdrDuration = p_enc->gdrDuration;
  }
  if (p_enc->colorDescPresent)
  {
    p_cfg->ui8colorDescPresent = 1;
    p_cfg->ui8colorPrimaries = p_enc->colorPrimaries;
    p_cfg->ui8colorTrc = p_enc->colorTrc;
    p_cfg->ui8colorSpace = p_enc->colorSpace;
    p_cfg->ui8videoFullRange = p_enc->videoFullRange;
  }
  if (p_enc->videoFullRange)
  {
      p_cfg->ui8videoFullRange = p_enc->videoFullRange;
  }
  if (p_enc->hrdEnable != 0)
  {
    p_cfg->ui8hrdEnable = p_enc->hrdEnable;
  }

  p_cfg->ui8planarFormat = p_src->cfg_enc_params.planar;
  p_cfg->ui16aspectRatioWidth = p_enc->aspectRatioWidth;
  p_cfg->ui16aspectRatioHeight = p_enc->aspectRatioHeight;

  if (p_enc->ltrRefInterval != 0)
  {
    p_cfg->ui32ltrRefInterval = p_enc->ltrRefInterval;
    p_cfg->i32ltrRefQpOffset = p_enc->ltrRefQpOffset;
    p_cfg->ui32ltrFirstGap = p_enc->ltrFirstGap;
    p_cfg->ui32ltrNextInterval = p_enc->ltrNextInterval;
  }
  if (p_enc->multicoreJointMode != 0)
  {
      p_cfg->ui8multicoreJointMode = p_enc->multicoreJointMode;
  }
  p_cfg->ui32QLevel = p_enc->qlevel;

  if (p_enc->chromaQpOffset != 0)
  {
      p_cfg->i8chromaQpOffset = p_enc->chromaQpOffset;
  }

  if (p_enc->tolCtbRcInter != 0.1)
  {
      p_cfg->i32tolCtbRcInter = (int32_t)(p_enc->tolCtbRcInter * 1000);
  }

  if (p_enc->tolCtbRcIntra != 0.1)
  {
      p_cfg->i32tolCtbRcIntra = (int32_t)(p_enc->tolCtbRcIntra * 1000);
  }

  if (p_enc->bitrateWindow != -255)
  {
      p_cfg->i16bitrateWindow = p_enc->bitrateWindow;
  }

  if (p_enc->inLoopDSRatio != 1)
  {
      p_cfg->ui8inLoopDSRatio = p_enc->inLoopDSRatio;
  }

  if (p_enc->blockRCSize != 0)
  {
      p_cfg->ui8blockRCSize = p_enc->blockRCSize;
  }

  if (p_enc->rcQpDeltaRange != 10)
  {
      p_cfg->ui8rcQpDeltaRange = p_enc->rcQpDeltaRange;
  }

  // convert enable_mb_level_rc, enable_cu_level_rate_control, and enable_hvs_qp to ctbRcMode
  if (QUADRA)
  {
    // ctbRcMode has priority over enable_mb_level_rc, enable_cu_level_rate_control, and enable_hvs_qp
    if (!p_cfg->ui8ctbRcMode)
    {
      if (p_t408->enable_mb_level_rc || p_t408->enable_cu_level_rate_control)
      {
        if (p_t408->enable_hvs_qp)
        {
          p_cfg->ui8ctbRcMode = 3;
        }
        else
        {
          p_cfg->ui8ctbRcMode = 2;
        }
      }
      else if (p_t408->enable_hvs_qp)
      {
        p_cfg->ui8ctbRcMode = 1;
      }
    }
  }

  if (p_src->low_delay_mode != 0)
  {
      p_cfg->ui8LowDelay = p_src->low_delay_mode;
  }

  if (p_enc->enable_ssim != 0)
  {
      p_cfg->ui8enableSSIM = p_enc->enable_ssim;
  }

  ni_log(NI_LOG_DEBUG, "lowDelay=%d\n", p_src->low_delay_mode);
  ni_log(NI_LOG_DEBUG, "ui8bitstreamFormat=%d\n", p_cfg->ui8bitstreamFormat);
  ni_log(NI_LOG_DEBUG, "i32picWidth=%d\n", p_cfg->i32picWidth);
  ni_log(NI_LOG_DEBUG, "i32picHeight=%d\n", p_cfg->i32picHeight);
  ni_log(NI_LOG_DEBUG, "i32meBlkMode=%d\n", p_cfg->i32meBlkMode);
  ni_log(NI_LOG_DEBUG, "ui8sliceMode=%d\n", p_cfg->ui8sliceMode);
  ni_log(NI_LOG_DEBUG, "i32frameRateInfo=%d\n", p_cfg->i32frameRateInfo);
  ni_log(NI_LOG_DEBUG, "i32vbvBufferSize=%d\n", p_cfg->i32vbvBufferSize);
  ni_log(NI_LOG_DEBUG, "i32userQpMax=%d\n", p_cfg->i32userQpMax);
  ni_log(NI_LOG_DEBUG, "enableSSIM=%d\n", p_cfg->ui8enableSSIM);
  // AVC only
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
  ni_log(NI_LOG_DEBUG, "ui32sourceEndian=%u\n", p_cfg->ui32sourceEndian);
  ni_log(NI_LOG_DEBUG, "hdrEnableVUI=%u\n", p_cfg->hdrEnableVUI);
  ni_log(NI_LOG_DEBUG, "i32hwframes=%i\n", p_cfg->i32hwframes);

  ni_log(NI_LOG_DEBUG, "** ni_t408_config_t: \n");
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
  ni_log(NI_LOG_DEBUG, "setLongTermInterval=%u\n", p_cfg->ui32setLongTermInterval);
  ni_log(NI_LOG_DEBUG, "setLongTermCount=%u\n", p_cfg->ui8setLongTermCount);

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
#ifndef QUADRA
  if (!QUADRA)
  {
    if (p_t408->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
      for (i = 0; i < 8; i++)
        //for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_qp);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L1);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_t408->custom_gop_params.pic_param[i].temporal_id);
      }
    }
  }
  else // QUADRA
#endif
  {
    if (p_t408->custom_gop_params.custom_gop_size)
    {
      int j;
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
      for (i = 0; i < NI_MAX_GOP_NUM; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].qp_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_factor=%f\n", i, p_t408->custom_gop_params.pic_param[i].qp_factor);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_t408->custom_gop_params.pic_param[i].temporal_id);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pics=%d\n", i, p_t408->custom_gop_params.pic_param[i].num_ref_pics);
        for (j = 0; j < NI_MAX_REF_PIC; j++)
        {
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic=%d\n", i, j, p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic);
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic_used=%d\n", i, j, p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic_used);
        }
      }
    }
  }

  ni_log(NI_LOG_DEBUG, "roiEnable=%d\n", p_t408->roiEnable);

  ni_log(NI_LOG_DEBUG, "numUnitsInTick=%u\n", p_t408->numUnitsInTick);
  ni_log(NI_LOG_DEBUG, "timeScale=%u\n", p_t408->timeScale);
  ni_log(NI_LOG_DEBUG, "numTicksPocDiffOne=%u\n", p_t408->numTicksPocDiffOne);

  ni_log(NI_LOG_DEBUG, "chromaCbQpOffset=%d\n", p_t408->chromaCbQpOffset);
  ni_log(NI_LOG_DEBUG, "chromaCrQpOffset=%d\n", p_t408->chromaCrQpOffset);

  ni_log(NI_LOG_DEBUG, "initialRcQp=%d\n", p_t408->initialRcQp);

  ni_log(NI_LOG_DEBUG, "nrYEnable=%u\n", p_t408->nrYEnable);
  ni_log(NI_LOG_DEBUG, "nrCbEnable=%u\n", p_t408->nrCbEnable);
  ni_log(NI_LOG_DEBUG, "nrCrEnable=%u\n", p_t408->nrCrEnable);

  // ENC_NR_WEIGHT
  ni_log(NI_LOG_DEBUG, "nrIntraWeightY=%u\n", p_t408->nrIntraWeightY);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCb=%u\n", p_t408->nrIntraWeightCb);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCr=%u\n", p_t408->nrIntraWeightCr);
  ni_log(NI_LOG_DEBUG, "nrInterWeightY=%u\n", p_t408->nrInterWeightY);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCb=%u\n", p_t408->nrInterWeightCb);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCr=%u\n", p_t408->nrInterWeightCr);

  ni_log(NI_LOG_DEBUG, "nrNoiseEstEnable=%u\n", p_t408->nrNoiseEstEnable);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaY=%u\n", p_t408->nrNoiseSigmaY);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCb=%u\n", p_t408->nrNoiseSigmaCb);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCr=%u\n", p_t408->nrNoiseSigmaCr);

  // newly added for T408
  ni_log(NI_LOG_DEBUG, "monochromeEnable=%u\n", p_t408->monochromeEnable);
  ni_log(NI_LOG_DEBUG, "strongIntraSmoothEnable=%u\n",
                 p_t408->strongIntraSmoothEnable);

  ni_log(NI_LOG_DEBUG, "weightPredEnable=%u\n", p_t408->weightPredEnable);
  ni_log(NI_LOG_DEBUG, "bgDetectEnable=%u\n", p_t408->bgDetectEnable);
  ni_log(NI_LOG_DEBUG, "bgThrDiff=%u\n", p_t408->bgThrDiff);
  ni_log(NI_LOG_DEBUG, "bgThrMeanDiff=%u\n", p_t408->bgThrMeanDiff);
  ni_log(NI_LOG_DEBUG, "bgLambdaQp=%u\n", p_t408->bgLambdaQp);
  ni_log(NI_LOG_DEBUG, "bgDeltaQp=%d\n", p_t408->bgDeltaQp);

  ni_log(NI_LOG_DEBUG, "customLambdaEnable=%u\n", p_t408->customLambdaEnable);
  ni_log(NI_LOG_DEBUG, "customMDEnable=%u\n", p_t408->customMDEnable);
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
  ni_log(NI_LOG_DEBUG, "forcedHeaderEnable=%u\n", p_t408->forcedHeaderEnable);

  //QUADRA
  ni_log(NI_LOG_DEBUG, "ui8EnableAUD=%d\n", p_cfg->ui8EnableAUD);
  ni_log(NI_LOG_DEBUG, "ui8LookAheadDepth=%d\n", p_cfg->ui8LookAheadDepth);
  ni_log(NI_LOG_DEBUG, "ui8rdoLevel=%d\n", p_cfg->ui8rdoLevel);
  ni_log(NI_LOG_DEBUG, "i8crf=%d\n", p_cfg->i8crf);
  ni_log(NI_LOG_DEBUG, "ui16HDR10MaxLight=%d\n", p_cfg->ui16HDR10MaxLight);
  ni_log(NI_LOG_DEBUG, "ui16HDR10AveLight=%d\n", p_cfg->ui16HDR10AveLight);
  ni_log(NI_LOG_DEBUG, "ui8HDR10CLLEnable=%d\n", p_cfg->ui8HDR10CLLEnable);
  ni_log(NI_LOG_DEBUG, "ui8EnableRdoQuant=%d\n", p_cfg->ui8EnableRdoQuant);
  ni_log(NI_LOG_DEBUG, "ui8ctbRcMode=%d\n", p_cfg->ui8ctbRcMode);
  ni_log(NI_LOG_DEBUG, "ui8gopSize=%d\n", p_cfg->ui8gopSize);
  ni_log(NI_LOG_DEBUG, "ui8useLowDelayPocType=%d\n", p_cfg->ui8useLowDelayPocType);
  ni_log(NI_LOG_DEBUG, "ui8gopLowdelay=%d\n", p_cfg->ui8gopLowdelay);
  ni_log(NI_LOG_DEBUG, "ui16gdrDuration=%d\n", p_cfg->ui16gdrDuration);
  ni_log(NI_LOG_DEBUG, "ui8hrdEnable=%d\n", p_cfg->ui8hrdEnable);
  ni_log(NI_LOG_DEBUG, "ui8colorDescPresent=%d\n", p_cfg->ui8colorDescPresent);
  ni_log(NI_LOG_DEBUG, "ui8colorPrimaries=%d\n", p_cfg->ui8colorPrimaries);
  ni_log(NI_LOG_DEBUG, "ui8colorTrc=%d\n", p_cfg->ui8colorTrc);
  ni_log(NI_LOG_DEBUG, "ui8colorSpace=%d\n", p_cfg->ui8colorSpace);
  ni_log(NI_LOG_DEBUG, "ui16aspectRatioWidth=%d\n", p_cfg->ui16aspectRatioWidth);
  ni_log(NI_LOG_DEBUG, "ui16aspectRatioHeight=%d\n", p_cfg->ui16aspectRatioHeight);
  ni_log(NI_LOG_DEBUG, "ui16rootBufId=%d\n", p_cfg->ui16rootBufId);
  ni_log(NI_LOG_DEBUG, "ui8planarFormat=%d\n", p_cfg->ui8planarFormat);
  ni_log(NI_LOG_DEBUG, "ui32ltrRefInterval=%u\n", p_cfg->ui32ltrRefInterval);
  ni_log(NI_LOG_DEBUG, "i32ltrRefQpOffset=%d\n", p_cfg->i32ltrRefQpOffset);
  ni_log(NI_LOG_DEBUG, "ui32ltrFirstGap=%u\n", p_cfg->ui32ltrFirstGap);
  ni_log(NI_LOG_DEBUG, "ui32ltrNextInterval=%u\n", p_cfg->ui32ltrNextInterval);
  ni_log(NI_LOG_DEBUG, "ui8multicoreJointMode=%d\n", p_cfg->ui8multicoreJointMode);
  ni_log(NI_LOG_DEBUG, "ui8videoFullRange=%u\n", p_cfg->ui8videoFullRange);
  ni_log(NI_LOG_DEBUG, "ui32QLevel=%u\n", p_cfg->ui32QLevel);
  ni_log(NI_LOG_DEBUG, "i8chromaQpOffset=%d\n", p_cfg->i8chromaQpOffset);
  ni_log(NI_LOG_DEBUG, "i32tolCtbRcInter=0x%x\n", p_cfg->i32tolCtbRcInter);
  ni_log(NI_LOG_DEBUG, "i32tolCtbRcIntra=0x%x\n", p_cfg->i32tolCtbRcIntra);
  ni_log(NI_LOG_DEBUG, "i16bitrateWindow=%d\n", p_cfg->i16bitrateWindow);
  ni_log(NI_LOG_DEBUG, "ui8inLoopDSRatio=%u\n", p_cfg->ui8inLoopDSRatio);
  ni_log(NI_LOG_DEBUG, "ui8blockRCSize=%u\n", p_cfg->ui8blockRCSize);
  ni_log(NI_LOG_DEBUG, "ui8rcQpDeltaRange=%u\n", p_cfg->ui8rcQpDeltaRange);
  ni_log(NI_LOG_DEBUG, "ui8LowDelay=%d\n", p_cfg->ui8LowDelay);
}

/*!******************************************************************************
 *  \brief  Setup and initialize all xcoder configuration to default (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_default_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_config)
{
  uint8_t i = 0;

  if( (!p_ctx) || (!p_config) )
  {
      ni_log(
          NI_LOG_ERROR,
          "ERROR: ni_set_default_template() Null pointer parameters passed\n",
          __func__);
      return;
  }

  memset(p_config, 0, sizeof(ni_encoder_config_t));

  // fill in common attributes values
  p_config->i32picWidth = 720;
  p_config->i32picHeight = 480;
  p_config->i32meBlkMode = 0; // (AVC ONLY) 0 means use all possible block partitions
  p_config->ui8sliceMode = 0; // 0 means 1 slice per picture
  p_config->i32frameRateInfo = 30;
  p_config->i32frameRateDenominator = 1;
  p_config->i32vbvBufferSize = 3000; //0; parameter is ignored if rate control is off, if rate control is on, 0 means do not check vbv constraints
  p_config->i32userQpMax = 51;       // this should also be h264-only parameter

  // AVC only
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->i32maxIntraSize = 8000000; // how big an intra p_frame can get?
    p_config->i32userMaxDeltaQp = 51;
    p_config->i32userMinDeltaQp = 51;
    p_config->i32userQpMin = 8;
  }

  p_config->i32bitRate = 0;
  p_config->i32bitRateBL = 0; // no documentation on this parameter in documents
  p_config->ui8rcEnable = 0;
  p_config->i32srcBitDepth = p_ctx->src_bit_depth;
  p_config->ui8enablePTS = 0;
  p_config->ui8lowLatencyMode = 0;

  // profiles for H.264: 1 = baseline, 2 = main, 3 = extended, 4 = high
  //                     5 = high10  (default 8 bit: 4, 10 bit: 5)
  // profiles for HEVC:  1 = main, 2 = main10  (default 8 bit: 1, 10 bit: 2)

  // bitstream type: H.264 or HEVC
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->ui8bitstreamFormat = STD_AVC;

    p_config->niParamT408.profile = 4;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 5;
    }
  } else if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format)
  {
      p_config->ui8bitstreamFormat = STD_JPEG;
  } else if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
  {
      p_config->ui8bitstreamFormat = STD_AV1;

      p_config->niParamT408.profile = 1;
  } else
  {
    ni_assert(NI_CODEC_FORMAT_H265 == p_ctx->codec_format);

    p_config->ui8bitstreamFormat = STD_HEVC;

    p_config->niParamT408.profile = 1;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 2;
    }
  }

  p_config->hdrEnableVUI = 0;
  p_config->ui32sourceEndian = p_ctx->src_endian;

  p_config->niParamT408.level = 0;
  p_config->niParamT408.tier = 0;    // 0 means main tier

  p_config->niParamT408.internalBitDepth = p_ctx->src_bit_depth;
  p_config->niParamT408.losslessEnable = 0;
  p_config->niParamT408.constIntraPredFlag = 0;

  if (QUADRA)
      p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_DEFAULT;
  else
      p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_IBBBP;

  p_config->niParamT408.decoding_refresh_type = 1;
  p_config->niParamT408.intra_qp = 22;
  // avcIdrPeriod (H.264 on T408), NOT shared with intra_period
  p_config->niParamT408.intra_period = 120;
  p_config->niParamT408.avcIdrPeriod = 120;

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

  p_config->niParamT408.cu_size_mode = 7;
  p_config->niParamT408.tmvpEnable = 1;
  p_config->niParamT408.wppEnable = 0;
  p_config->niParamT408.max_num_merge = 2;
  p_config->niParamT408.disableDeblk = 0;
  p_config->niParamT408.lfCrossSliceBoundaryEnable = 1;
  p_config->niParamT408.betaOffsetDiv2 = 0;
  p_config->niParamT408.tcOffsetDiv2 = 0;
  p_config->niParamT408.skipIntraTrans = 1;
  p_config->niParamT408.saoEnable = 1;
  p_config->niParamT408.intraNxNEnable = 1;

  p_config->niParamT408.bitAllocMode = 0;

  for (i = 0; i < NI_MAX_GOP_NUM; i++)
  {
    p_config->niParamT408.fixedBitRatio[i] = 1;
  }

  if (QUADRA)
    p_config->niParamT408.enable_cu_level_rate_control = 0;
  else
    p_config->niParamT408.enable_cu_level_rate_control = 1;

  p_config->niParamT408.enable_hvs_qp = 0;
  p_config->niParamT408.hvs_qp_scale = 2;

  p_config->niParamT408.max_delta_qp = 10;

  // CUSTOM_GOP
  p_config->niParamT408.custom_gop_params.custom_gop_size = 0;
#ifndef QUADRA
  if (!QUADRA)
  {
    for (i = 0; i < p_config->niParamT408.custom_gop_params.custom_gop_size; i++)
    {
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_qp = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L0 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L1 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
    }
  }
  else // QUADRA
#endif
  {
    int j;
    for (i = 0; i < NI_MAX_GOP_NUM; i++)
    {
      p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].qp_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].qp_factor =
          (float)0.3;   // QP Factor range is between 0.3 and 1, higher values mean lower quality and less bits
      p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pics= 0;
      for (j = 0; j < NI_MAX_REF_PIC; j++)
      {
        p_config->niParamT408.custom_gop_params.pic_param[i].rps[j].ref_pic = 0;
        p_config->niParamT408.custom_gop_params.pic_param[i].rps[j].ref_pic_used = 0;
      }
    }
  }

  p_config->niParamT408.roiEnable = 0;

  p_config->niParamT408.numUnitsInTick = 1000;
  p_config->niParamT408.timeScale = p_config->i32frameRateInfo * 1000;
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->niParamT408.timeScale *= 2;
  }

  p_config->niParamT408.numTicksPocDiffOne = 0;

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

  p_config->niParamT408.useLongTerm = 0;

  // newly added for T408
  p_config->niParamT408.monochromeEnable = 0;
  p_config->niParamT408.strongIntraSmoothEnable = 1;

  p_config->niParamT408.weightPredEnable = 0;
  p_config->niParamT408.bgDetectEnable = 0;
  p_config->niParamT408.bgThrDiff = 8;     // matching the C-model
  p_config->niParamT408.bgThrMeanDiff = 1; // matching the C-model
  p_config->niParamT408.bgLambdaQp = 32;   // matching the C-model
  p_config->niParamT408.bgDeltaQp = 3;     // matching the C-model

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
  p_config->niParamT408.rdoSkip = 0;
  p_config->niParamT408.lambdaScalingEnable = 0;
  p_config->niParamT408.enable_transform_8x8 = 1;
  p_config->niParamT408.avc_slice_mode = 0;
  p_config->niParamT408.avc_slice_arg = 0;
  p_config->niParamT408.intra_mb_refresh_mode = 0;
  p_config->niParamT408.intra_mb_refresh_arg = 0;
  if (QUADRA)
    p_config->niParamT408.enable_mb_level_rc = 0;
  else
    p_config->niParamT408.enable_mb_level_rc = 1;
  p_config->niParamT408.entropy_coding_mode = 1; // 1 means CABAC, make sure profile is main or above, can't have CABAC in baseline
  p_config->niParamT408.forcedHeaderEnable = 0; // first IDR frame

  //QUADRA
  p_config->ui8EnableAUD = 0;
  p_config->ui8LookAheadDepth = 0;
  p_config->ui8rdoLevel = 1;
  p_config->i8crf = -1;
  p_config->ui16HDR10MaxLight = 0;
  p_config->ui16HDR10AveLight = 0;
  p_config->ui8HDR10CLLEnable = 0;
  p_config->ui8EnableRdoQuant = 0;
  p_config->ui8ctbRcMode = 0;
  p_config->ui8gopSize = 0;
  p_config->ui8useLowDelayPocType = 0;
  p_config->ui8gopLowdelay = 0;
  p_config->ui16gdrDuration = 0;
  p_config->ui8hrdEnable = 0;
  p_config->ui8colorDescPresent = 0;
  p_config->ui8colorPrimaries = 0;
  p_config->ui8colorTrc = 0;
  p_config->ui8colorSpace = 0;
  p_config->ui16aspectRatioWidth = 0;
  p_config->ui16aspectRatioHeight = 0;
  p_config->ui8planarFormat = 1;
  p_config->ui32ltrRefInterval = 0;
  p_config->i32ltrRefQpOffset = 0;
  p_config->ui32ltrFirstGap= 0;
  p_config->ui32ltrNextInterval = 1;
  p_config->ui8multicoreJointMode = 0;
  p_config->ui8videoFullRange = 0;
  p_config->ui32setLongTermInterval = 0;
  p_config->ui8setLongTermCount = 2;
  p_config->ui32QLevel = -1;
  p_config->i8chromaQpOffset = 0;
  p_config->i32tolCtbRcInter = (int32_t)(0.1 * 1000);
  p_config->i32tolCtbRcIntra = (int32_t)(0.1 * 1000);
  p_config->i16bitrateWindow = -255;
  p_config->ui8inLoopDSRatio = 1;
  p_config->ui8blockRCSize = 0;
  p_config->ui8rcQpDeltaRange = 10;
  p_config->ui8LowDelay = 0;
  p_config->ui8fixedframerate = 1;
  p_config->ui8enableSSIM = 0;
}

/*!******************************************************************************
*  \brief  Perform validation on custom dec parameters (Rev. B)
*
*  \param
*
*  \return
******************************************************************************/
ni_retcode_t ni_validate_custom_dec_template(ni_xcoder_params_t *p_src,
                                             ni_session_context_t *p_ctx,
                                             ni_decoder_config_t *p_cfg,
                                             char *p_param_err,
                                             uint32_t max_err_len)
{
  ni_retcode_t param_ret = NI_RETCODE_SUCCESS;
  ni_retcode_t warning = NI_RETCODE_SUCCESS;
  int w = p_src->source_width;
  int h = p_src->source_height;
  char *p_param_warn = NULL;
  int i;

  if (!p_ctx || !p_cfg || !p_param_err)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  // Zero out the error buffer
  p_param_warn = malloc(sizeof(p_ctx->param_err_msg));
  if (!p_param_warn)
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() p_param_warn malloc failure\n",
             NI_ERRNO, __func__);
      param_ret = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }

  memset(p_param_err, 0, max_err_len);
  memset(p_param_warn, 0, max_err_len);


  //if (p_cfg->i32bitRate > NI_MAX_BITRATE)
  //{
  //  strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
  //  param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
  //  LRETURN;
  //}

  if (p_cfg->ui8HWFrame == 0 &&
    (p_cfg->asOutputConfig[1].ui8Enabled || p_cfg->asOutputConfig[2].ui8Enabled))
  {
    strncpy(p_param_err, "Incompatible output format: hw frame must be used if out1 or out2 used", max_err_len);
    param_ret = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    //checking cropping param
    if (p_cfg->asOutputConfig[i].ui8CropMode == NI_DEC_CROP_MODE_MANUAL)
    {
      if (NI_MINIMUM_CROPPED_LENGTH > w - p_cfg->asOutputConfig[i].sCroppingRectable.ui16X ||
        NI_MINIMUM_CROPPED_LENGTH > h - p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y)
      {
        strncpy(p_param_err, "Invalid crop offset: extends past 48x48 minimum window", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (NI_MINIMUM_CROPPED_LENGTH > p_cfg->asOutputConfig[i].sCroppingRectable.ui16W ||
        NI_MINIMUM_CROPPED_LENGTH > p_cfg->asOutputConfig[i].sCroppingRectable.ui16H)
      {
        strncpy(p_param_err, "Invalid crop w or h: must be at least 48x48 minimum window", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_TOO_SMALL;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16W > w ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H > h)
      {
        strncpy(p_param_err, "Invalid crop w or h: must be smaller than input", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16W + p_cfg->asOutputConfig[i].sCroppingRectable.ui16X > w ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H + p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y > h)
      {
        strncpy(p_param_err, "Invalid crop rect: must fit in input", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16X & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16W & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H & 1)
      {
        strncpy(p_param_err, "Invalid crop value: even values only", max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
      {
        strncpy(p_param_warn, "crop param used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }

    }

    //checking scaling param
    if (p_cfg->asOutputConfig[i].ui8ScaleEnabled)
    {
      if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height > h ||
        p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width > w)
      { //may need to revisit if based on cropped input or base input for w/h limit
        strncpy(p_param_err, "Invalid scale dimensions: must downscale only", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height == 0 ||
        p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width == 0)
      { //may need to revisit if based on cropped input or base input for w/h limit
        strncpy(p_param_err, "Invalid scale dimensions: zero", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_TOO_SMALL;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height & 1 ||
        p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width & 1 )
      {
        strncpy(p_param_err, "Invalid scale value: even values only", max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].ui8CropMode == NI_DEC_CROP_MODE_MANUAL)
      {
        //reject if scale dimensions exceed crop dimensions
        if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height > p_cfg->asOutputConfig[i].sCroppingRectable.ui16H ||
          p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width > p_cfg->asOutputConfig[i].sCroppingRectable.ui16W)
        {
          strncpy(p_param_err, "Invalid scale dimensions: downscale only after cropping", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_OOR;
          LRETURN;
        }
      }
      if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
      {
        strncpy(p_param_warn, "scale param used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
    }
    if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
    {
      if (p_cfg->asOutputConfig[i].ui8Force8Bit || p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled)
      {
        strncpy(p_param_warn, "force8bit or semiPlanar used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
    }
  }//end forloop
  if (warning == NI_RETCODE_PARAM_WARN && param_ret == NI_RETCODE_SUCCESS)
  {
    param_ret = NI_RETCODE_PARAM_WARN;
    strncpy(p_param_err, p_param_warn, max_err_len);
  }

END:

    ni_aligned_free(p_param_warn);
    return param_ret;
}

// Check encoder level parameters
static ni_retcode_t ni_check_level(int level, int codec_id)
{
    const int l_levels_264[] = {10, 11, 12, 13, 20, 21, 22, 30, 31, 32,
                                40, 41, 42, 50, 51, 52, 60, 61, 62, 0};
    const int l_levels_265[] = {10, 20, 21, 30, 31, 40, 41,
                                50, 51, 52, 60, 61, 62, 0};
    const int l_levels_av1[] = {20, 21, 30, 31, 40, 41, 50, 51, 0};
    const int *l_levels = l_levels_264;

    if (level == 0)
    {
        return NI_RETCODE_SUCCESS;
    }

    if (codec_id == NI_CODEC_FORMAT_H265)
    {
        l_levels = l_levels_265;
    } else if (codec_id == NI_CODEC_FORMAT_AV1)
    {
        l_levels = l_levels_av1;
    }

    while (*l_levels != 0)
    {
        if (*l_levels == level)
        {
            return NI_RETCODE_SUCCESS;
        }
        l_levels++;
    }

    return NI_RETCODE_FAILURE;
}

/*!******************************************************************************
 *  \brief  Perform validation on custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_validate_custom_template(ni_session_context_t *p_ctx,
                                         ni_encoder_config_t *p_cfg,
                                         ni_xcoder_params_t *p_src,
                                         char *p_param_err,
                                         uint32_t max_err_len)
{
  ni_retcode_t param_ret = NI_RETCODE_SUCCESS;
  ni_retcode_t warning = NI_RETCODE_SUCCESS;
  char* p_param_warn = malloc(sizeof(p_ctx->param_err_msg));
  int i;

  if( (!p_ctx) || (!p_cfg) || (!p_src) || (!p_param_err) )
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);
  memset(p_param_warn, 0, max_err_len);

  if (0 == p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid frame_rate of 0 value", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_FRATE;
    LRETURN;
  }

  if (((p_cfg->i32frameRateInfo + p_cfg->i32frameRateDenominator - 1) /
       p_cfg->i32frameRateDenominator) > NI_MAX_FRAMERATE)
  {
      strncpy(p_param_err, "Invalid i32frameRateInfo: too big", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_FRATE;
      LRETURN;
  }

  if (p_cfg->i32bitRate > NI_MAX_BITRATE)
  {
    strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate < NI_MIN_BITRATE )
  {
    strncpy(p_param_err, "Invalid i32bitRate: too low", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_src->source_width < XCODER_MIN_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_width > XCODER_MAX_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_height < XCODER_MIN_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (p_src->source_height > XCODER_MAX_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (NI_RETCODE_SUCCESS !=
      ni_check_level(p_src->cfg_enc_params.level_idc, p_ctx->codec_format))
  {
      strncpy(p_param_err, "Invalid Encoder Level: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_OOR;
      LRETURN;
  }

  if ((p_src->cfg_enc_params.intra_mb_refresh_mode < 0 ||
       p_src->cfg_enc_params.intra_mb_refresh_mode > 4) ||
      (NI_CODEC_FORMAT_H264 == p_ctx->codec_format &&
       p_src->cfg_enc_params.intra_mb_refresh_mode == 4))
  {
      strncpy(p_param_err, "Invalid intra_mb_refresh_mode: out of range",
              max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_OOR;
      LRETURN;
  }

  if (!QUADRA)
  {
    if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
        NI_MAX_GOP_NUM)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }
  }
  else // QUADRA
  {
      if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
          if (!p_cfg->niParamT408.custom_gop_params.custom_gop_size)
          {
              strncpy(p_param_err,
                      "Invalid custom GOP paramaters: custom gop size must > 0",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
              LRETURN;
          }
      }
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
    {
        if (GOP_PRESET_IDX_CUSTOM != p_cfg->niParamT408.gop_preset_index &&
            GOP_PRESET_IDX_DEFAULT != p_cfg->niParamT408.gop_preset_index)
        {
            strncpy(p_param_err,
                    "Invalid custom GOP paramaters: selected gopPresetIdx is "
                    "not compatible with custom gop",
                    max_err_len);
            param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
            LRETURN;
        }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
        NI_MAX_GOP_NUM)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }

    if(p_cfg->ui8enableSSIM != 0)
    {
        if (!is_fw_rev_higher(p_ctx, (uint8_t)'6', (uint8_t)'1'))
        {
            p_cfg->ui8enableSSIM = 0;
            strncpy(p_param_warn, "enableSSIM is not supported for FW lower than Rev 62. All ssim will be 0.", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
        if ((NI_CODEC_FORMAT_H264 != p_ctx->codec_format) && (NI_CODEC_FORMAT_H265 != p_ctx->codec_format))
        {
            p_cfg->ui8enableSSIM = 0;
            strncpy(p_param_warn, "enableSSIM is only supported on H.264 or H.265", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
    }
  }

  if (QUADRA)
  {
      if (p_cfg->ui8LookAheadDepth != 0)
      {
          if (1 == p_cfg->niParamT408.gop_preset_index ||
              3 == p_cfg->niParamT408.gop_preset_index ||
              7 == p_cfg->niParamT408.gop_preset_index ||
              9 == p_cfg->niParamT408.gop_preset_index)
          {
              strncpy(p_param_err,
                      "this gopPreset is not supported when lookaheadDepth > 0",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
              LRETURN;
          }
          if (p_src->source_width < 288)
          {
              strncpy(p_param_err,
                      "lookAheadDepth requires input width of at least 288",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
              LRETURN;
          }
      }

      if (GOP_PRESET_IDX_DEFAULT == p_cfg->niParamT408.gop_preset_index)
      {
          // when gop_preset_index = -1 (default), gop pattern is decided by gopSize and gopLowdelay
          //p_cfg->ui8gopSize = 0;
          //p_cfg->ui8gopLowdelay = 0;
      }
      if (1 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->niParamT408.intra_period = 1;
          p_cfg->niParamT408.avcIdrPeriod = 1;
          p_cfg->ui8gopSize = 1;
          p_cfg->ui16gdrDuration = 0;
      }
      if (2 == p_cfg->niParamT408.gop_preset_index)
      {
          strncpy(p_param_err,
                  "gopPresetIdx 2 is obsolete, suggest to use gopPresetIdx 9 "
                  "instead",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
      }
      if (3 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 1;
          p_cfg->ui8gopLowdelay = 1;
      }
      if (4 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 2;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (5 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 4;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (6 == p_cfg->niParamT408.gop_preset_index)
      {
          strncpy(p_param_err,
                  "gopPresetIdx 6 is obsolete, suggest to use gopPresetIdx 7 "
                  "instead",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
      }
      if (7 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 4;
          p_cfg->ui8gopLowdelay = 1;
      }
      if (8 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 8;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (9 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 1;
          p_cfg->ui8gopLowdelay = 0;
      }
  }

  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    if (!QUADRA)
    {
      if (10 == p_ctx->src_bit_depth)
      {
        if (p_cfg->niParamT408.profile != 5)
        {
          strncpy(p_param_err, "Invalid profile: must be 5 (high10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      else
      {
        if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5)
        {
          strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                  " 3 (extended), 4 (high), or 5 (high10)", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      if (1 == p_cfg->niParamT408.profile &&
          ! (0 == p_cfg->niParamT408.gop_preset_index ||
             1 == p_cfg->niParamT408.gop_preset_index ||
             2 == p_cfg->niParamT408.gop_preset_index ||
             6 == p_cfg->niParamT408.gop_preset_index))
      {
        strncpy(p_param_err, "Invalid gopPresetIdx for H.264 baseline profile:"
                " must be 1, 2, 6 or 0 (custom with no B frames)", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (1 == p_cfg->niParamT408.profile &&
          GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size;
             i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                    "have B frames", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      }
    }
    else // QUADRA
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5 || p_cfg->niParamT408.profile == 3)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                " 4 (high), or 5 (high10)", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (10 == p_ctx->src_bit_depth && p_cfg->niParamT408.profile != 5)
      {
        strncpy(p_param_warn, "AVC Baseline/Main/High Profile do not support 10-bit, auto convert to 8-bit", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
      if (1 == p_cfg->niParamT408.profile)
      {
        if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
        {
          for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
          {
            if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
            {
              strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                      "have B frames", max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
            }
          }
        } else if (((p_cfg->ui8gopSize != 1 || p_cfg->ui8gopLowdelay)) &&
                   (p_cfg->niParamT408.intra_period != 1))
        {
            if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT ||
                p_cfg->ui8gopSize != 0 ||
                p_cfg
                    ->ui8gopLowdelay)   // if gopSize is 0 (default / adapative gop), autoset to 1
            {
                strncpy(p_param_err,
                        "GOP size must be 1 and must not use low delay GOP (no "
                        "B frames) for profile 1",
                        max_err_len);
                param_ret = NI_RETCODE_INVALID_PARAM;
                LRETURN;
            }
          p_cfg->ui8gopSize = 1; //autoset to 1
        }
      }
    }
  }
  else if (NI_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    if (!QUADRA)
    {
      if (10 == p_ctx->src_bit_depth)
      {
        if (p_cfg->niParamT408.profile != 2)
        {
          strncpy(p_param_err, "Invalid profile: must be 2 (main10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      else
      {
        if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
        {
          strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
    }
    else // QUADRA
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (10 == p_ctx->src_bit_depth && p_cfg->niParamT408.profile != 2)
      {
        strncpy(p_param_warn, "HEVC Main Profile does not support 10-bit, auto convert to 8-bit", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
    }
  } else if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
  {
      if (p_cfg->niParamT408.profile != 1)
      {
          strncpy(p_param_err, "Invalid profile: must be 1 (main)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
      if (p_cfg->niParamT408.level)   // 0 means auto level
      {
          if (p_cfg->niParamT408.level < 20)
          {
              p_cfg->niParamT408.level = 20;
              strncpy(p_param_warn,
                      "AV1 level < 2.0 is not supported, change to level 2.0",
                      max_err_len);
              warning = NI_RETCODE_PARAM_WARN;
          } else if (p_cfg->niParamT408.level > 51)
          {
              p_cfg->niParamT408.level = 51;
              strncpy(p_param_warn,
                      "AV1 level > 5.1 is not supported, change to level 5.1",
                      max_err_len);
              warning = NI_RETCODE_PARAM_WARN;
          }
      }

      if (p_cfg->niParamT408.conf_win_top != 0)
      {
          p_cfg->niParamT408.conf_win_top = p_src->cfg_enc_params.conf_win_top =
              0;
          strncpy(p_param_warn, "confWinTop is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_bottom != 0)
      {
          p_cfg->niParamT408.conf_win_bottom =
              p_src->cfg_enc_params.conf_win_bottom = 0;
          strncpy(p_param_warn, "confWinBottom is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_left != 0)
      {
          p_cfg->niParamT408.conf_win_left =
              p_src->cfg_enc_params.conf_win_left = 0;
          strncpy(p_param_warn, "confWinLeft is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_right != 0)
      {
          p_cfg->niParamT408.conf_win_right =
              p_src->cfg_enc_params.conf_win_right = 0;
          strncpy(p_param_warn, "confWinRight is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
  }

  if (p_src->force_frame_type != 0 && p_src->force_frame_type != 1)
  {
      strncpy(p_param_err, "Invalid forceFrameType: out of range",
              max_err_len);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (p_cfg->niParamT408.forcedHeaderEnable > 2)
  {
      strncpy(p_param_err, "Invalid forcedHeaderEnable: out of range",
              max_err_len);
      param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
      LRETURN;
  }

  if (p_cfg->niParamT408.decoding_refresh_type < 0 ||
    p_cfg->niParamT408.decoding_refresh_type > 2)
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE;
    LRETURN;
  }

  if (!QUADRA)
  {
    if (p_cfg->niParamT408.gop_preset_index < 0 ||
      p_cfg->niParamT408.gop_preset_index > 8)
    {
      strcpy(p_param_err, "Invalid gop_preset_index: out of range");
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }

    if (p_src->low_delay_mode && 1 != p_cfg->niParamT408.gop_preset_index &&
        2 != p_cfg->niParamT408.gop_preset_index &&
        3 != p_cfg->niParamT408.gop_preset_index &&
        6 != p_cfg->niParamT408.gop_preset_index &&
        7 != p_cfg->niParamT408.gop_preset_index &&
        !(0 == p_cfg->niParamT408.gop_preset_index &&
          1 == p_cfg->niParamT408.custom_gop_params.custom_gop_size))
    {
      strcpy(p_param_err, "GOP size must be 1 when lowDelay is enabled");
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }
  }
  else // QUADRA
  {
      if (p_cfg->ui8gopSize > 8)
      {
          strncpy(p_param_err, "Invalid gopSize out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
      }

    if (p_cfg->ui8gopLowdelay &&
        p_cfg->ui8gopSize > 4)
    {
      strncpy(p_param_err, "GOP size must be <= 4 for low delay GOP", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }

    if (p_cfg->ui8LookAheadDepth)
    {
      if (p_cfg->ui8LookAheadDepth < 4 || p_cfg->ui8LookAheadDepth > 40)
      {
        strncpy(p_param_err, "Invalid LookAheadDepth: out of range. <[4-40]>", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
        LRETURN;
      }
      if (p_cfg->ui8gopLowdelay)
      {
        strncpy(p_param_err, "2-pass encode does not support low delay GOP", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
        LRETURN;
      }

    }

    if (p_src->low_delay_mode || p_cfg->ui8picSkipEnable)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
        {
          if (p_cfg->niParamT408.custom_gop_params.pic_param[i].poc_offset != (i+1))
          {
            if (p_src->low_delay_mode)
              strncpy(p_param_err, "Custom GOP must not include backward prediction when lowDelay is enabled", max_err_len);
            else
              strncpy(p_param_err, "Custom GOP must not include backward prediction when picSkip is enabled", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      } else if (1 != p_cfg->ui8gopSize && !p_cfg->ui8gopLowdelay &&
                 p_cfg->niParamT408.intra_period != 1)
      {
        if (p_src->low_delay_mode)
          strncpy(p_param_err, "Must use low delay GOP (gopPresetIdx 1,3,7,9) when lowDelay is enabled", max_err_len);
        else
          strncpy(p_param_err, "Must use low delay GOP (gopPresetIdx 1,3,7,9) when picSkip is enabled", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
        LRETURN;
      }

      if (p_cfg->ui8multicoreJointMode)
      {
          if (p_src->low_delay_mode)
            strncpy(p_param_err,
                    "Cannot use multicoreJointMode when lowDelay is enabled",
                    max_err_len);
          else
            strncpy(p_param_err,
                    "Cannot use multicoreJointMode when picSkip is enabled",
                    max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
    }

    if (p_src->low_delay_mode)
    {
      if (p_cfg->ui16maxFrameSize == 0)
      {
          p_cfg->ui16maxFrameSize = ((p_src->source_width * p_src->source_height * 3 / 4) * p_ctx->bit_depth_factor) / 2000;
          ni_log(NI_LOG_ERROR, "%s: maxFrameSize is not set in low delay mode. Set it to half of frame size %d\n",
                 __func__, p_cfg->ui16maxFrameSize*2000);
      }

      // minimum acceptable value of maxFrameSize is bitrate / framerate in byte
      uint32_t min_maxFrameSize = ((p_cfg->i32bitRate / p_cfg->i32frameRateInfo * p_cfg->i32frameRateDenominator) / 8) / 2000;

      if (p_cfg->ui16maxFrameSize < min_maxFrameSize)
      {
          ni_log(NI_LOG_ERROR, "%s: maxFrameSize %d is too small. Changed to minimum value (bitrate/framerate in byte): %d\n",
                 __func__, p_cfg->ui16maxFrameSize*2000, min_maxFrameSize*2000);
          p_cfg->ui16maxFrameSize = min_maxFrameSize;
      }
    }
    else    
    {
      if (p_cfg->ui16maxFrameSize != 0)
      {
          strncpy(p_param_err, "maxFrameSize can only be used when lowDelay is enabled", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
    }
  }

  if (QUADRA)
  {
    if (p_cfg->ui16gdrDuration)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "Custom GOP can not have B frames for intra refresh", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      }
      else if (p_cfg->ui8gopSize != 1 || p_cfg->ui8gopLowdelay)
      {
          if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT)
          {
              strncpy(p_param_err,
                      "GOP must not contain B frames for gdrDuration",
                      max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
          }
        strncpy(p_param_warn, "GOP size forced to 1 and low delay GOP force disabled (no B frames) for intra refresh", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8gopSize = 1;
        p_cfg->ui8gopLowdelay = 0;
      }
      if (p_cfg->ui16gdrDuration == 1)
      {
        strncpy(p_param_err,
                "intra refresh cycle (height / intraRefreshArg MB or CTU) must > 1",
                max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (p_cfg->ui8LookAheadDepth != 0)
      {
        strncpy(p_param_warn, "lookaheadDepth forced to 0 (disable 2-pass) for intra refresh", max_err_len);
        p_cfg->ui8LookAheadDepth = 0;
      }
      if (STD_HEVC == p_cfg->ui8bitstreamFormat ||
          STD_AV1 == p_cfg->ui8bitstreamFormat)
      {
        if (p_cfg->niParamT408.intra_period < p_cfg->ui16gdrDuration)
        {
          strncpy(p_param_warn, "intraPeriod forced to match intra refersh cycle (intraPeriod must >= intra refersh cycle)", max_err_len);
          p_cfg->niParamT408.intra_period = p_cfg->ui16gdrDuration;
        }
      }
      else if (STD_AVC == p_cfg->ui8bitstreamFormat)
      {
        if (p_cfg->niParamT408.avcIdrPeriod < p_cfg->ui16gdrDuration)
        {
          strncpy(p_param_warn, "intraPeriod forced to match intra refersh cycle (intraPeriod must >= intra refersh cycle)", max_err_len);
          p_cfg->niParamT408.avcIdrPeriod = p_cfg->ui16gdrDuration;
        }
      }
    }

    if ((p_cfg->ui8hrdEnable) || (p_cfg->ui8fillerEnable))
    {
     // enable rate control
     if (p_cfg->ui8rcEnable == 0)
     {
         p_cfg->ui8rcEnable = p_src->cfg_enc_params.rc.enable_rate_control = 1;
     }

     // enable hrd if it is off
     if (p_cfg->i32vbvBufferSize == 0)
     {
       p_cfg->i32vbvBufferSize = 3000;
     }
    }

    if (p_cfg->ui32ltrRefInterval || p_cfg->niParamT408.useLongTerm)
    {
        if (p_cfg->ui32ltrRefInterval && p_cfg->niParamT408.useLongTerm)
        {
            strncpy(p_param_err,
                    "Can't enable ltrRefInterval and longTermReferenceEnable "
                    "at same time",
                    max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
        }

      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        if (p_cfg->niParamT408.custom_gop_params.custom_gop_size > 1)
        {
          strncpy(p_param_err, "Custom GOP size can not be > 1 for long term reference", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      } else if ((p_cfg->ui8gopSize != 1) && (p_cfg->ui8gopLowdelay == 0) &&
                 (p_cfg->niParamT408.intra_period != 1))
      {
          if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT)
          {
              strncpy(p_param_err,
                      "GOP size can not be > 1 for long term reference",
                      max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
          }
        strncpy(p_param_warn, "GOP size forced to 1 for long term reference", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8gopSize = 1;
      }

      if (p_cfg->ui8LookAheadDepth != 0)
      {
        strncpy(p_param_warn, "lookaheadDepth forced to 0 (disable 2-pass) for long term reference", max_err_len);
        p_cfg->ui8LookAheadDepth = 0;
      }
    }

    if (p_cfg->ui32setLongTermInterval && (p_cfg->niParamT408.useLongTerm == 0))
    {
        strncpy(
            p_param_err,
            "Must set longTermReferenceEnable for longTermReferenceInterval",
            max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }
  }

  if (p_cfg->niParamT408.cu_size_mode < 0 ||
    p_cfg->niParamT408.cu_size_mode > 7)
  {
    strncpy(p_param_err, "Invalid cu_size_mode: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_CU_SIZE_MODE;
    LRETURN;
  }



  if (p_cfg->niParamT408.use_recommend_enc_params < 0 ||
    p_cfg->niParamT408.use_recommend_enc_params > 3)
  {
    strncpy(p_param_err, "Invalid use_recommend_enc_params: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM;
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
          param_ret = NI_RETCODE_PARAM_ERROR_MAXNUMMERGE;
          LRETURN;
        }
      }
      break;
    }

    default: break;
  }

  if ( p_cfg->niParamT408.intra_qp < -1 ||
       p_cfg->niParamT408.intra_qp > 51 )
  {
    strncpy(p_param_err, "Invalid intra_qp: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_INTRA_QP;
    LRETURN;
  }

  if (QUADRA)
  {
    if (p_cfg->i8crf >= 0 && p_cfg->i8crf <= 51)
    {
      if (p_cfg->ui8LookAheadDepth < 4 || p_cfg->ui8LookAheadDepth > 40)
      {
        strncpy(p_param_err, "CRF requres LookAheadDepth <[4-40]>", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
        LRETURN;
      }
      if (p_cfg->ui8rcEnable == 1)
      {
        strncpy(p_param_err, "CRF requires RcEnable 0", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
        LRETURN;
      }
      if (p_cfg->ui8ctbRcMode > 0)
      {
        strncpy(p_param_warn, "Lookahead with CtbRcMode > 0 may degrade quality", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        //LRETURN;
      }
    }
  }

  if ( p_cfg->niParamT408.enable_mb_level_rc != 1 &&
       p_cfg->niParamT408.enable_mb_level_rc != 0 )
  {
    strncpy(p_param_err, "Invalid enable_mb_level_rc: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
    LRETURN;
  }

  {
    if ( p_cfg->niParamT408.minQpI < 0 ||
         p_cfg->niParamT408.minQpI > 51 )
    {
      strncpy(p_param_err, "Invalid min_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MN_QP;
      LRETURN;
    }

    if ( p_cfg->niParamT408.maxQpI < 0 ||
         p_cfg->niParamT408.maxQpI > 51 )
    {
      strncpy(p_param_err, "Invalid max_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
    // TBD minQpP minQpB maxQpP maxQpB

    if ( p_cfg->niParamT408.enable_cu_level_rate_control != 1 &&
         p_cfg->niParamT408.enable_cu_level_rate_control != 0 )
    {
      strncpy(p_param_err, "Invalid enable_cu_level_rate_control: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN;
      LRETURN;
    }

    //if (p_cfg->niParamT408.enable_cu_level_rate_control == 1)
    {
      if ( p_cfg->niParamT408.enable_hvs_qp != 1 &&
           p_cfg->niParamT408.enable_hvs_qp != 0 )
      {
        strncpy(p_param_err, "Invalid enable_hvs_qp: out of range", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_HVS_QP_EN;
        LRETURN;
      }

      if (p_cfg->niParamT408.enable_hvs_qp)
      {
        if ( p_cfg->niParamT408.max_delta_qp < 0 ||
             p_cfg->niParamT408.max_delta_qp > 51 )
        {
          strncpy(p_param_err, "Invalid max_delta_qp: out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_MX_DELTA_QP;
          LRETURN;
        }
#if 0 // TBD missing enable_hvs_qp_scale?
        if ( p_cfg->niParamT408.enable_hvs_qp_scale != 1 &&
             p_cfg->niParamT408.enable_hvs_qp_scale != 0 )
        {
          strcpy(p_param_err, "Invalid enable_hvs_qp_scale: out of range");
          param_ret = NI_RETCODE_PARAM_ERROR_HVS_QP_SCL;
          LRETURN;
        }

        if (p_cfg->niParamT408.enable_hvs_qp_scale == 1) {
          if ( p_cfg->niParamT408.hvs_qp_scale < 0 ||
               p_cfg->niParamT408.hvs_qp_scale > 4 )
          {
            strcpy(p_param_err, "Invalid hvs_qp_scale: out of range");
            param_ret = NI_RETCODE_PARAM_ERROR_HVS_QP_SCL;
            LRETURN;
          }
        }
#endif
      }
    }
    // hrd is off when i32vbvBufferSize is 0
    if ((p_cfg->i32vbvBufferSize < 10 && p_cfg->i32vbvBufferSize != 0) || p_cfg->i32vbvBufferSize > 3000)
    {
      strncpy(p_param_err, "Invalid i32vbvBufferSize: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_VBV_BUFFER_SIZE;
      LRETURN;
    }
  }

  // check valid for common param
  param_ret = ni_check_common_params(&p_cfg->niParamT408, p_src, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  // check valid for RC param
  param_ret = ni_check_ratecontrol_params(p_cfg, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_CUSTOM)
  {
    p_ctx->keyframe_factor =
      presetGopKeyFrameFactor[p_cfg->niParamT408.gop_preset_index];
  }
  if (warning == NI_RETCODE_PARAM_WARN && param_ret == NI_RETCODE_SUCCESS)
  {
    param_ret = NI_RETCODE_PARAM_WARN;
    strncpy(p_param_err, p_param_warn, max_err_len);
  }

END:
    ni_aligned_free(p_param_warn);
    return param_ret;
}

ni_retcode_t ni_check_common_params(ni_t408_config_t *p_param,
                                    ni_xcoder_params_t *p_src,
                                    char *p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  int32_t low_delay = 0;
  int32_t intra_period_gop_step_size;
  int32_t i, j;

  if (!p_param || !p_src || !p_param_err)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  // check low-delay gop structure
  if (!QUADRA)
  {
    if (0 == p_param->gop_preset_index) // common gop
    {
      if (p_param->custom_gop_params.custom_gop_size > 1)
      {
          int minVal = p_param->custom_gop_params.pic_param[0].poc_offset;
          low_delay = 1;
          for (i = 1; i < p_param->custom_gop_params.custom_gop_size; i++)
          {
              if (minVal > p_param->custom_gop_params.pic_param[i].poc_offset)
              {
                  low_delay = 0;
                  break;
              } else
              {
                  minVal = p_param->custom_gop_params.pic_param[i].poc_offset;
              }
        }
      }
    }
    else if (p_param->gop_preset_index == 2 ||
      p_param->gop_preset_index == 3 ||
      p_param->gop_preset_index == 6 ||
      p_param->gop_preset_index == 7) // low-delay case (IPPP, IBBB)
    {
      low_delay = 1;
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
      strncpy(p_param_err, "Invalid intra_period and gop_preset_index: gop structure is larger than intra period", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
      LRETURN;
    }

    if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) != 0)) ||
        ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) != 0)))
    {
      strncpy(p_param_err, "Invalid intra_period and gop_preset_index: intra period is not a multiple of gop structure size", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
      LRETURN;
    }

    if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      int temp_poc[NI_MAX_GOP_NUM];
      int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (p_param->custom_gop_params.pic_param[i].temporal_id >= XCODER_MAX_NUM_TEMPORAL_LAYER)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is zero or negative", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
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
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }
  }
  else // QUADRA
  {
    if (p_param->custom_gop_params.custom_gop_size)
    {
      int temp_poc[NI_MAX_GOP_NUM];
      int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (p_param->custom_gop_params.pic_param[i].temporal_id >= XCODER_MAX_NUM_TEMPORAL_LAYER)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is negative", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        for (j = 0; j < p_param->custom_gop_params.pic_param[i].num_ref_pics; j++)
        {
          if (p_param->custom_gop_params.pic_param[i].rps[j].ref_pic == 0)
          {
            strncpy(p_param_err, "Invalid custom gop parameters: ref pic delta cannot be 0", max_err_len);
            ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
            LRETURN;
          }
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
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
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
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 8 pixels when enable CU8x8 of cu_size_mode. Recommend to set cu_size_mode |= 0x1 (CU8x8)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((align_16_width_flag != 0) || (align_16_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 16 pixels when enable CU16x16 of cu_size_mode. Recommend to set cu_size_mode |= 0x2 (CU16x16)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((p_param->cu_size_mode & 0x4) == 0) && ((align_32_width_flag != 0) || (align_32_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 32 pixels when enable CU32x32 of cu_size_mode. Recommend to set cu_size_mode |= 0x4 (CU32x32)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN;
        LRETURN;
      }
    }
  }

  if ((p_param->conf_win_top < 0) || (p_param->conf_win_top > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_top: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }
  if (p_param->conf_win_top % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_top: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }

  if ((p_param->conf_win_bottom < 0) || (p_param->conf_win_bottom > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }
  if (p_param->conf_win_bottom % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }

  if ((p_param->conf_win_left < 0) || (p_param->conf_win_left > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_left: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }
  if (p_param->conf_win_left % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_left: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }

  if (p_param->conf_win_right < 0 || p_param->conf_win_right > 8192)
  {
    strncpy(p_param_err, "Invalid conf_win_right: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
    LRETURN;
  }
  if (p_param->conf_win_right % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_right: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
  }

END:

    return ret;
}

ni_retcode_t ni_check_ratecontrol_params(ni_encoder_config_t* p_cfg, char* p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  ni_t408_config_t* p_param = &p_cfg->niParamT408;

  if( (!p_cfg) || (!p_param_err) )
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (p_param->roiEnable != 0 && p_param->roiEnable != 1)
  {
    strncpy(p_param_err, "Invalid roiEnable: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  if (p_param->roiEnable && p_param->enable_hvs_qp)
  {
    strncpy(p_param_err, "hvsQPEnable and roiEnable: not mutually exclusive", max_err_len);
    ret = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  // TBD RevB
  if (p_cfg->ui8rcEnable == 1)
  {
    if (p_param->minQpP > p_param->maxQpP || p_param->minQpB > p_param->maxQpB)
    {
      strncpy(p_param_err, "Invalid min_qp(P/B) and max_qp(P/B): min_qp cannot be larger than max_qp", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_MX_QP;
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
void ni_params_print(ni_xcoder_params_t *const p_encoder_params)
{
  if (!p_encoder_params)
  {
    return;
  }

  ni_encoder_cfg_params_t *p_enc = &p_encoder_params->cfg_enc_params;

  ni_log(NI_LOG_DEBUG, "XCoder Params:\n");

  ni_log(NI_LOG_DEBUG, "preset=%d\n", p_encoder_params->preset);
  ni_log(NI_LOG_DEBUG, "fps_number / fps_denominator=%u / %u\n",
                 p_encoder_params->fps_number,
                 p_encoder_params->fps_denominator);

  ni_log(NI_LOG_DEBUG, "source_width x source_height=%dx%d\n", p_encoder_params->source_width, p_encoder_params->source_height);
  ni_log(NI_LOG_DEBUG, "bitrate=%d\n", p_encoder_params->bitrate);

  ni_log(NI_LOG_DEBUG, "profile=%d\n", p_enc->profile);
  ni_log(NI_LOG_DEBUG, "level_idc=%d\n", p_enc->level_idc);
  ni_log(NI_LOG_DEBUG, "high_tier=%d\n", p_enc->high_tier);

  ni_log(NI_LOG_DEBUG, "frame_rate=%d\n", p_enc->frame_rate);

  ni_log(NI_LOG_DEBUG, "use_recommend_enc_params=%d\n", p_enc->use_recommend_enc_params);
  ni_log(NI_LOG_DEBUG, "cu_size_mode=%d\n", p_enc->cu_size_mode);
  ni_log(NI_LOG_DEBUG, "max_num_merge=%d\n", p_enc->max_num_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_8x8_merge=%d\n", p_enc->enable_dynamic_8x8_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_16x16_merge=%d\n", p_enc->enable_dynamic_16x16_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_32x32_merge=%d\n", p_enc->enable_dynamic_32x32_merge);
  // trans_rate not available in Rev B
  ni_log(NI_LOG_DEBUG, "enable_rate_control=%d\n", p_enc->rc.enable_rate_control);
  ni_log(NI_LOG_DEBUG, "enable_cu_level_rate_control=%d\n", p_enc->rc.enable_cu_level_rate_control);
  ni_log(NI_LOG_DEBUG, "enable_hvs_qp=%d\n", p_enc->rc.enable_hvs_qp);
  ni_log(NI_LOG_DEBUG, "enable_hvs_qp_scale=%d\n", p_enc->rc.enable_hvs_qp_scale);
  ni_log(NI_LOG_DEBUG, "hvs_qp_scale=%d\n", p_enc->rc.hvs_qp_scale);
  ni_log(NI_LOG_DEBUG, "min_qp=%d\n", p_enc->rc.min_qp);
  ni_log(NI_LOG_DEBUG, "max_qp=%d\n", p_enc->rc.max_qp);
  ni_log(NI_LOG_DEBUG, "max_delta_qp=%d\n", p_enc->rc.max_delta_qp);
  ni_log(NI_LOG_DEBUG, "vbv_buffer_size=%d\n", p_enc->rc.vbv_buffer_size);
  ni_log(NI_LOG_DEBUG, "enable_filler=%d\n", p_enc->rc.enable_filler);
  ni_log(NI_LOG_DEBUG, "enable_pic_skip=%d\n", p_enc->rc.enable_pic_skip);

  ni_log(NI_LOG_DEBUG, "forcedHeaderEnable=%d\n", p_enc->forced_header_enable);
  ni_log(NI_LOG_DEBUG, "roi_enable=%d\n", p_enc->roi_enable);
  ni_log(NI_LOG_DEBUG, "long_term_ref_enable=%d\n", p_enc->long_term_ref_enable);
  ni_log(NI_LOG_DEBUG, "long_term_ref_interval=%d\n", p_enc->long_term_ref_interval);
  ni_log(NI_LOG_DEBUG, "long_term_ref_count=%d\n", p_enc->long_term_ref_count);
  ni_log(NI_LOG_DEBUG, "conf_win_top=%d\n", p_enc->conf_win_top);
  ni_log(NI_LOG_DEBUG, "conf_win_bottom=%d\n", p_enc->conf_win_bottom);
  ni_log(NI_LOG_DEBUG, "conf_win_left=%d\n", p_enc->conf_win_left);
  ni_log(NI_LOG_DEBUG, "conf_win_right=%d\n", p_enc->conf_win_right);

  ni_log(NI_LOG_DEBUG, "intra_qp=%d\n", p_enc->rc.intra_qp);
  ni_log(NI_LOG_DEBUG, "enable_mb_level_rc=%d\n", p_enc->rc.enable_mb_level_rc);

  ni_log(NI_LOG_DEBUG, "intra_period=%d\n", p_enc->intra_period);
  ni_log(NI_LOG_DEBUG, "decoding_refresh_type=%d\n", p_enc->decoding_refresh_type);

  // Rev. B: H.264 only or HEVC-shared parameters, in ni_t408_config_t
  ni_log(NI_LOG_DEBUG, "enable_transform_8x8=%d\n", p_enc->enable_transform_8x8);
  ni_log(NI_LOG_DEBUG, "avc_slice_mode=%d\n", p_enc->avc_slice_mode);
  ni_log(NI_LOG_DEBUG, "avc_slice_arg=%d\n", p_enc->avc_slice_arg);
  ni_log(NI_LOG_DEBUG, "entropy_coding_mode=%d\n", p_enc->entropy_coding_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_mode=%d\n", p_enc->intra_mb_refresh_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_arg=%d\n", p_enc->intra_mb_refresh_arg);

  ni_log(NI_LOG_DEBUG, "gop_preset_index=%d\n", p_enc->gop_preset_index);
#ifndef QUADRA
  if (!QUADRA)
  {
    if (p_enc->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      int i;
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
      for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_qp);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L1);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_enc->custom_gop_params.pic_param[i].temporal_id);
      }
    }
  }
  else // QUADRA
#endif
  {
    if (p_enc->custom_gop_params.custom_gop_size)
    {
      int i, j;
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
      for (i = 0; i < NI_MAX_GOP_NUM; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].qp_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_factor=%lf\n", i, p_enc->custom_gop_params.pic_param[i].qp_factor);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_enc->custom_gop_params.pic_param[i].temporal_id);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pics=%d\n", i, p_enc->custom_gop_params.pic_param[i].num_ref_pics);
        for (j = 0; j < NI_MAX_REF_PIC; j++)
        {
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic=%d\n", i, j, p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic);
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic_used=%d\n", i, j, p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic_used);
        }
      }
    }
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
void *ni_session_keep_alive_thread(void *arguments)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_thread_arg_struct_t *args = (ni_thread_arg_struct_t *)arguments;
    ni_session_stats_t inst_info = {0};
    ni_session_context_t ctx = {0};
    uint32_t loop = 0;
    uint64_t endtime = ni_gettime_ns();
    //interval(nanoseconds) is equals to ctx.keep_alive_timeout/3(330,000,000ns approximately equal to 1/3 second).
    uint64_t interval = args->keep_alive_timeout * 330000000LL;
#ifndef _ANDROID
#ifdef __linux__
    struct sched_param sched_param;

    // Linux has a wide variety of signals, Windows has a few.
    // A large number of signals will interrupt the thread, which will cause heartbeat command interval more than 1 second.
    // So just mask the unuseful signals in Linux
    sigset_t signal;
    sigfillset(&signal);
    ni_pthread_sigmask(SIG_BLOCK, &signal, NULL);

    /* set up schedule priority
     * first try to run with RR mode.
     * if fails, try to set nice value.
     * if fails either, ignore it and run with default priority.
     * Note: Scheduling requires root permission. App is probably exectued
     *       without root so the priority for this thread might just end up
     *       being default.
     */
    if (((sched_param.sched_priority = sched_get_priority_max(SCHED_RR)) ==
         -1) ||
        sched_setscheduler(syscall(SYS_gettid), SCHED_RR, &sched_param) < 0)
    {
        ni_log(NI_LOG_DEBUG, "%s cannot set scheduler: %s\n", __func__,
               strerror(NI_ERRNO));
        if (setpriority(PRIO_PROCESS, 0, -20) != 0)
        {
            ni_log(NI_LOG_DEBUG, "%s cannot set nice value: %s\n", __func__,
                   strerror(NI_ERRNO));
        }
    }

#elif defined(_WIN32)
    /* set up schedule priority.
   * try to set the current thread to time critical level which is the highest prioriy
   * level.
   */
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) ==
        0)
    {
        ni_log(NI_LOG_DEBUG, "%s cannot set priority: %d.\n", __func__,
               GetLastError());
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
    ni_log(NI_LOG_DEBUG, "%s ctx.keep_alive_timeout: %us.\n", __func__,
           ctx.keep_alive_timeout);

    for (;;)   // condition TBD
    {
        retval =
            ni_send_session_keep_alive(ctx.session_id, ctx.blk_io_handle,
                                       ctx.event_handle, ctx.p_all_zero_buf);
        retval = ni_query_session_stats(&ctx, ctx.device_type, &inst_info,
                                        retval, nvme_admin_cmd_xcoder_config);
        (void)retval;
        CHECK_ERR_RC2((&ctx), retval, inst_info, nvme_admin_cmd_xcoder_config,
                      ctx.device_type, ctx.hw_id, &(ctx.session_id));

        // 1. If received failure, set the close_thread flag to TRUE, and exit,
        //    then main thread will check this flag and return failure directly;
        // 2. skip checking VPU recovery.
        //    If keep_alive thread detect the VPU RECOVERY before main thread,
        //    the close_thread flag may damage the vpu recovery handling process.
        if ((NI_RETCODE_SUCCESS != retval) &&
            (NI_RETCODE_NVME_SC_VPU_RECOVERY != retval))
        {
            LRETURN;
        }

        endtime += interval;
        while (ni_gettime_ns() < endtime)
        {
            if (args->close_thread)
            {
                LRETURN;
            }
            ni_usleep(10000);   // 10ms per loop
        }
    }

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "%s abormal closed:%d\n", __func__, retval);
        // changing the value to be True here means the thread has been closed.
        args->close_thread = true;
    }

    ni_log(NI_LOG_DEBUG, "%s(): exit\n", __func__);

    return NULL;
}

/*!******************************************************************************
*  \brief  Open a xcoder upload instance
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
ni_retcode_t ni_uploader_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void * p_buffer = NULL;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_UPLOAD;
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

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_ENCODER, ni_htonl(p_ctx->codec_format), 1/*1 for uploadMode*/);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp =
        ni_htonll(((ni_session_stats_t *)p_buffer)->ui64Session_timestamp);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        ni_encoder_session_close(p_ctx, 0);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log(NI_LOG_DEBUG,
           "Uploader open session ID:0x%x,timestamp:%" PRIu64 "\n",
           p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log(NI_LOG_DEBUG, "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG, "Open session completed\n");
  }

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = 0;
  p_ctx->last_dts = 0;

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "dec_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "dec_dts");

  //p_ctx->active_video_width = 0;
  //p_ctx->active_video_height = 0;

  ni_log(NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

  p_ctx->hw_action = NI_CODEC_HW_NONE;

#ifndef _WIN32
  // If this is a P2P upload session, open the Netint kernel driver
  if (p_ctx->isP2P)
  {
      int ret;
      char line[256];
      char syspath[256];
      struct stat bstat;
      char *block_dev;
      char *dom, *bus, *dev, *fnc;
      FILE *fp;

      p_ctx->netint_fd = open("/dev/netint", O_RDWR);
      if (p_ctx->netint_fd < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR: %s() Can't open device: %s\n", __func__,
                 strerror(NI_ERRNO));
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      // find the PCI domain:bus:device:function
      block_dev = &p_ctx->blk_xcoder_name[0];
      if (stat(block_dev, &bstat) < 0)
      {
          ni_log(NI_LOG_ERROR, "failed to get stat of file %s\n", block_dev);
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      if ((bstat.st_mode & S_IFMT) != S_IFBLK)
      {
          ni_log(NI_LOG_ERROR, "%s is not a block device\n", block_dev);
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      ret = snprintf(syspath, sizeof(syspath) - 1,
                     "/sys/block/%s/device/address", block_dev + 5);
      syspath[ret] = '\0';

      fp = fopen(syspath, "r");

      if (fp == NULL)
      {
          ni_log(NI_LOG_ERROR, "Failed to read address\n");
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      if (fgets(line, 256, fp) == NULL)
      {
          ni_log(NI_LOG_ERROR, "Failed to read line from address\n");
          fclose(fp);
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      fclose(fp);

      errno = 0;
      p_ctx->domain = strtoul(line, &dom, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI domain\n");
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      errno = 0;
      p_ctx->bus = strtoul(dom + 1, &bus, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI bus\n");
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      errno = 0;
      p_ctx->dev = strtoul(bus + 1, &dev, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI device\n");
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      errno = 0;
      p_ctx->fn = strtoul(dev + 1, &fnc, 16);

      if (errno < 0)
      {
          ni_log(NI_LOG_ERROR, "Failed to read PCI function\n");
          retval = NI_RETCODE_FAILURE;
          LRETURN;
      }

      ni_log(NI_LOG_DEBUG, "PCI slot = %d:%d:%d:%d\n", p_ctx->domain, p_ctx->bus,
                     p_ctx->dev, p_ctx->fn);
  }
#endif

#ifdef XCODER_DUMP_ENABLED
  char dump_file[256] = { 0 };

  snprintf(dump_file, sizeof dump_file, "%s%u%s", "decoder_in_id",
           p_ctx->session_id, ".264");
  p_ctx->p_dump[0] = fopen(dump_file, "wb");
  ni_log(NI_LOG_DEBUG, "dump_file = %s\n", dump_file);

  snprintf(dump_file, sizeof dump_file, "%s%u%s", "decoder_out_id",
           p_ctx->session_id, ".yuv");
  p_ctx->p_dump[1] = fopen(dump_file, "wb");
  ni_log(NI_LOG_DEBUG, "dump_file = %s\n", dump_file);

  if (!p_ctx->p_dump[0] || !p_ctx->p_dump[1])
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s(): Cannot open dump file\n", __func__);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
*  \brief  Copy a xcoder decoder worker thread info
*
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_copy_internal(ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx)
{
  if (!src_p_ctx || !dst_p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  //dst_p_ctx->dts_queue_mutex          = src_p_ctx->dts_queue_mutex;
  //dst_p_ctx->decoder_read_mutex       = src_p_ctx->decoder_read_mutex;
  //dst_p_ctx->decoder_read_mutex_len   = src_p_ctx->decoder_read_mutex_len;
  //dst_p_ctx->decoder_read_cond        = src_p_ctx->decoder_read_cond;
  //dst_p_ctx->decoder_read_semaphore   = src_p_ctx->decoder_read_semaphore;
  //dst_p_ctx->decoder_read_workerqueue = src_p_ctx->decoder_read_workerqueue;
  dst_p_ctx->max_nvme_io_size = src_p_ctx->max_nvme_io_size;
  dst_p_ctx->device_handle = src_p_ctx->device_handle;
  dst_p_ctx->blk_io_handle = src_p_ctx->blk_io_handle;
  dst_p_ctx->hw_id = src_p_ctx->hw_id;
  //
  ////dst_p_ctx->worker_queue_decoder_read = src_p_ctx->worker_queue_decoder_read;
  //// Here we copy a worker thread pool
  //int counter;
  //for (counter = 0; counter < MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  //{
  //    dst_p_ctx->ThreadID_decoder_read[counter] = src_p_ctx->ThreadID_decoder_read[counter];
  //}

  //pthread_mutex_alloc_and_init(&dst_p_ctx->dts_queue_mutex);

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
*  \brief  Send a YUV p_frame to upload session
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwupload_session_write(ni_session_context_t *p_ctx, ni_frame_t *p_frame,
                              niFrameSurface1_t *hwdesc)
{
  int retval = 0;
  uint32_t size = 0;
  //uint32_t metadata_size = NI_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t i = 0;
  uint32_t tx_size = 0, aligned_tx_size = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  uint32_t retry_count = 0;
  ni_instance_buf_info_t buf_info = { 0 };
#ifdef MEASURE_LATENCY
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);
  //niFrameSurface1_t* p_data3 = (niFrameSurface1_t*)((uint8_t*)p_frame->p_data[3]);
  ////p_data3->rsvd = p_meta->hwdesc->rsvd;
  //ni_log(NI_LOG_DEBUG, "%s:mar16 HW=%d ui16FrameIdx=%d i8InstID=%d device_handle=%d\n",
  //               ishwframe, p_data3->ui16FrameIdx, p_data3->i8InstID, p_data3->device_handle);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_frame->dts);
  }
#endif

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2];// +p_frame->data_len[3] + p_frame->extra_data_len;
  ni_log(NI_LOG_DEBUG, "frame size bytes =%u  %d is metadata!\n", frame_size_bytes,
                 0);
  p_ctx->status = 0;
  for (;;)
  {
      //retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
      //                                    NI_DEVICE_TYPE_ENCODER, &buf_info);

      retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                          NI_DEVICE_TYPE_ENCODER, &buf_info);
      CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      //CHECK_VPU_RECOVERY(retval); No need for this because
      //no VPU is involved for upload
      if (NI_RETCODE_SUCCESS != retval ||
          (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count >= 500))
      {
          ni_log(NI_LOG_TRACE, "Upload write query retry %d\n", retry_count);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
          LRETURN;
      }
      if (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count < 500)
      {
          retry_count++;
          ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
          ni_usleep(100);
          ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
      } else   //available
      {
          ni_log(NI_LOG_DEBUG, "%s: avail %d, FID %d\n", __func__,
                 buf_info.hw_inst_ind.buffer_avail,
                 buf_info.hw_inst_ind.frame_index);
          break;
      }
  }
  ni_log(NI_LOG_DEBUG, "Info hwupload write query success, available buf "
                 "size %u >= frame size %u , retry %u\n",
                 buf_info.buf_avail_size, frame_size_bytes, retry_count);

  {
#ifdef XCODER_TIMESTAMP_DTS_ENABLED
      retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue,
                                     p_frame->dts, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR %s(): ni_timestamp_register() for dts "
                 "returned: %d\n",
                 __func__, retval);
      }
#endif

      //Apply write configuration here
      retval = ni_config_session_rw(p_ctx, SESSION_WRITE_CONFIG, 1,
                                    NI_CODEC_HW_UPLOAD, 0);
      CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);

      uint32_t ui32LBA =
          WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
      ni_log(NI_LOG_DEBUG,
             "%s: p_data = %p, p_frame->buffer_size = %u, "
             "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
             __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
             ui32LBA);
      sent_size = frame_size_bytes;
      if (sent_size % NI_MEM_PAGE_ALIGNMENT)
      {
          sent_size =
              ((sent_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) +
              NI_MEM_PAGE_ALIGNMENT;
      }

      retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_frame->p_buffer, sent_size, ui32LBA);
      CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write, p_ctx->device_type,
                   p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);
      if (retval < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          LRETURN;
      }

      hwdesc->ui16FrameIdx = buf_info.hw_inst_ind.frame_index;
      hwdesc->ui16session_ID = p_ctx->session_id;
      hwdesc->device_handle =
          (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
      hwdesc->bit_depth = p_ctx->bit_depth_factor;
      hwdesc->src_cpu = (uint8_t)NI_DEVICE_TYPE_ENCODER;
      hwdesc->output_idx = 0;

      p_ctx->frame_num++;
      size = frame_size_bytes;

#ifdef XCODER_DUMP_DATA
      char dump_file[256];
      snprintf(dump_file, sizeof(dump_file), "%ld-%u-hwup-fme/fme-%04ld.yuv",
               (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

      FILE *f = fopen(dump_file, "wb");
      fwrite(p_frame->p_buffer,
             p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
             1, f);
      fflush(f);
      fclose(f);
#endif
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_log(NI_LOG_INFO, "DTS:%" PRId64 ",DELTA:%lu,uLAT:%lu;\n", p_frame->dts,
             abs_time_ns - p_ctx->prev_read_frame_time,
             ni_lat_meas_q_check_latency((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                                         abs_time_ns, p_frame->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

  retval = size;

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

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
                                    niFrameSurface1_t *hwdesc)
{
  int retval = 0;
  ni_instance_buf_info_t hwdesc_info = { 0 };
  int query_retry = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx || !hwdesc)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
         __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  for (;;)
  {
    query_retry++;
#ifndef _WIN32
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_R_ACQUIRE,
                                        NI_DEVICE_TYPE_ENCODER, &hwdesc_info);
#else
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                        NI_DEVICE_TYPE_ENCODER, &hwdesc_info);
#endif
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
      p_ctx->device_type, p_ctx->hw_id,
      &(p_ctx->session_id));

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_DEBUG, "Warning upload read hwdesc fail rc %d or ind "
        "!\n", retval);

      if (query_retry >= 1000)
      {
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "Info hwupload read hwdesc success, "
             "frame_ind=%d !\n", hwdesc_info.hw_inst_ind.frame_index);

      hwdesc->ui16FrameIdx = hwdesc_info.hw_inst_ind.frame_index;
      hwdesc->ui16session_ID = p_ctx->session_id;
      hwdesc->device_handle =
          (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
      hwdesc->bit_depth = p_ctx->bit_depth_factor;
      hwdesc->src_cpu = (uint8_t)NI_DEVICE_TYPE_ENCODER;
      hwdesc->output_idx = 0;
      LRETURN;
    }
  }

END:
    return retval;
}

/*!*****************************************************************************
*  \brief  clear a particular xcoder instance buffer/data
*
*  \param   ni_session_context_t p_ctx - xcoder Context
*  \param   ni_instance_buf_info_rw_type_t rw_type
*  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
*  \param   ni_instance_buf_info_t *out - Struct preallocated from the caller
*           where the resulting data will be placed
*
*  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
*            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
*            failure
******************************************************************************/
ni_retcode_t ni_clear_instance_buf(niFrameSurface1_t *surface,
                                   int32_t device_handle)
{
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t dataLen = 0;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter - device_handle %d\n", __func__,
         device_handle);

  if ((uint16_t)NI_INVALID_SESSION_ID == surface->ui16session_ID)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
  //maybe just set 13 aqs inst id again?
  ni_log(NI_LOG_DEBUG, "%s():cdw10=0x%x FID = %d\n", __func__, cmd.cdw10,
         surface->ui16FrameIdx);
  ui32LBA = CLEAR_INSTANCE_BUF_W(((uint16_t)surface->ui16FrameIdx));
  retval = ni_nvme_send_write_cmd((ni_device_handle_t)(int64_t)device_handle,
                                  NI_INVALID_DEVICE_HANDLE, p_buffer,
                                  NI_DATA_BUFFER_LEN, ui32LBA);
  //Cannot check sessio stats here since this isn't a session command.
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
*  \brief  Retrieve a hw desc p_frame from decoder
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_read_desc(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
    //Needs serious editing to support hwdesc read again, this is currently vanilla read
    //queue_info decoder_read_workerqueue;
    ni_instance_mgr_stream_info_t data = {0};
    int rx_size = 0;
    uint64_t frame_offset = 0;
    uint16_t yuvW = 0;
    uint16_t yuvH = 0;
    uint8_t *p_data_buffer;
    int i = 0;
    int retval = NI_RETCODE_SUCCESS;
    int metadata_hdr_size = NI_FW_META_DATA_SZ -
        NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
    int sei_size = 0;
    uint32_t total_bytes_to_read = 0;
    uint32_t total_yuv_met_size = 0;
    uint32_t read_size_bytes = 0;
    uint32_t actual_read_size = 0;
    int keep_processing = 1;
    ni_instance_buf_info_t buf_info = {0};
    int query_retry = 0;
    uint32_t ui32LBA = 0;
    unsigned int bytes_read_so_far = 0;
    int query_type = INST_BUF_INFO_RW_READ;
#ifdef MEASURE_LATENCY 
    uint64_t abs_time_ns;
#endif

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_frame)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.

  total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;
  total_yuv_met_size = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + metadata_hdr_size;
  ni_log(NI_LOG_DEBUG,
         "Total bytes to read %u total_yuv_met_size %u, low_delay %u\n",
         total_bytes_to_read, total_yuv_met_size, p_ctx->decoder_low_delay);
  if (p_ctx->decoder_low_delay > 0 && !p_ctx->ready_to_close)
  {
      ni_log(NI_LOG_DEBUG, "frame_num = %" PRIu64 ", pkt_num = %" PRIu64 "\n",
             p_ctx->frame_num, p_ctx->pkt_num);
      if (p_ctx->frame_num >= p_ctx->pkt_num)
      {
          //nothing to query, leave
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
      }
      query_type = INST_BUF_INFO_RW_READ_BUSY;
  }
  for (;;)
  {
    query_retry++;
    retval = ni_query_instance_buf_info(p_ctx, query_type,
                                        NI_DEVICE_TYPE_DECODER, &buf_info);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
      p_ctx->device_type, p_ctx->hw_id,
      &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_DEBUG, "Info query buf_info.size = %u\n",
      buf_info.buf_avail_size);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "Dec read desc failed. Retry %d\n", query_retry);

      if (query_retry >= 1000)
      {
        ni_log(NI_LOG_DEBUG, "Warning: dec read desc failed %d retries. rc=%d"
               "\n", query_retry, retval);
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_pthread_mutex_unlock(p_ctx->xcoder_mutex);
      ni_usleep(100);
      ni_pthread_mutex_lock(p_ctx->xcoder_mutex);
    } else if (buf_info.buf_avail_size == metadata_hdr_size)
    {
        ni_log(NI_LOG_DEBUG,
               "(HwFrame) Info only metadata hdr is available, seq change?\n");
        total_bytes_to_read = metadata_hdr_size;
        break;
    } else if (buf_info.buf_avail_size < total_yuv_met_size)
    {
      ni_log(NI_LOG_TRACE, "Dec read desc buf_size < frame_size. Retry %d\n", query_retry);

      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        ni_log(NI_LOG_DEBUG, "Info dec query, ready_to_close %u, query eos\n",
          p_ctx->ready_to_close);
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (data.is_flushed)
        {
          ni_log(NI_LOG_DEBUG, "Info eos reached.\n");
          p_frame->end_of_stream = 1;
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
        }
      }

      if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status &&
          query_retry < 1000 / 2)
      {
          ni_usleep(100);
          continue;
      } else
      {
          if (p_ctx->decoder_low_delay>0)
          {
              ni_log(NI_LOG_ERROR, "Warning: low_delay mode non sequential input? Disabling LD\n");
              p_ctx->decoder_low_delay = -1;
          }
          ni_log(NI_LOG_DEBUG, "Warning: dec read desc failed %d retries. rc=%d"
                 "\n", query_retry, retval);
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
        // We have to ensure there are adequate number of DTS for picture
        // reorder delay otherwise wait for more packets to be sent to decoder.
        ni_timestamp_table_t *p_dts_queue = p_ctx->dts_queue;
        if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
            !p_ctx->ready_to_close)
        {
            retval = NI_RETCODE_SUCCESS;
            ni_log(NI_LOG_DEBUG,
                   "At least %d packets should be sent before reading the "
                   "first frame!\n",
                   p_ctx->pic_reorder_delay + 1);
            LRETURN;
        }
      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_DEBUG, "Info dec YUV query, pic size %ux%u xfer frame size "
          "%ux%u frame-rate %u is_flushed %u\n",
          data.picture_width, data.picture_height,
          data.transfer_frame_stride, data.transfer_frame_height,
          data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->actual_video_width = data.picture_width;
        p_ctx->pixel_format = data.pix_format;
        p_ctx->bit_depth_factor = ni_get_bitdepth_factor_from_pixfmt(p_ctx->pixel_format);
        //p_ctx->bit_depth_factor = data.transfer_frame_stride / data.picture_width;
        p_ctx->is_first_frame = 1;

        ni_log(NI_LOG_DEBUG, "Info dec YUV, adjust frame size from %ux%u to "
          "%ux%u\n", p_frame->video_width, p_frame->video_height,
          p_ctx->active_video_width, p_ctx->active_video_height);

        retval = ni_frame_buffer_alloc(
            p_frame, p_ctx->actual_video_width, p_ctx->active_video_height,
            p_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
            p_ctx->bit_depth_factor,
            3,   // Alloc space for write to data[3] and metadata
            1);

        if (NI_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;
        p_data_buffer = (uint8_t*)p_frame->p_buffer;
        // make sure we don't read more than available
        ni_log(NI_LOG_DEBUG, "Info dec buf size: %u YUV frame + meta-hdr size: %u "
          "available: %u\n", p_frame->buffer_size,
          total_bytes_to_read, buf_info.buf_avail_size);
      }
      break;
    }
  }// end while1 query retry

  ni_log(NI_LOG_DEBUG, "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  ni_log(NI_LOG_DEBUG, "p_frame->data_len[3] = %u\n", p_frame->data_len[3]);
  if (buf_info.buf_avail_size < total_bytes_to_read)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR %s() avaliable size(%u) less than "
             "needed (%u)\n",
             __func__, buf_info.buf_avail_size, total_bytes_to_read);
      abort();
  }

  //Apply read configuration here
  retval = ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 1,
                                NI_CODEC_HW_ENABLE, 0);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  read_size_bytes = total_bytes_to_read;
  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
  if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
  {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 p_data_buffer, read_size_bytes, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else
  {
      // command issued successfully, now exit
      ni_metadata_dec_frame_t *p_meta =
          (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                      p_frame->data_len[0] +
                                      p_frame->data_len[1] +
                                      p_frame->data_len[2] +
                                      p_frame->data_len[3]);

      if (buf_info.buf_avail_size != metadata_hdr_size)
      {
          // shift metadata to end of triple output
#ifdef _WIN32
          p_data_buffer = (uint8_t *)p_frame->p_buffer +
              sizeof(niFrameSurface1_t) * NI_MAX_NUM_OF_DECODER_OUTPUTS;
          memcpy(p_meta, p_data_buffer, metadata_hdr_size);
#else
          memcpy(p_meta,
                 p_frame->p_buffer +
                     sizeof(niFrameSurface1_t) * NI_MAX_NUM_OF_DECODER_OUTPUTS,
                 metadata_hdr_size);
#endif
          sei_size = p_meta->sei_size;
          niFrameSurface1_t *p_data3 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    p_frame->data_len[0] +
                                    p_frame->data_len[1] +
                                    p_frame->data_len[2]);

          niFrameSurface1_t *p_data3_1 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    sizeof(niFrameSurface1_t));
          niFrameSurface1_t *p_data3_2 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    2 * sizeof(niFrameSurface1_t));
          // Libxcoder knows the handle so overwrite here
          p_data3->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3_1->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3_2->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3->ui16session_ID = p_data3_1->ui16session_ID =
              p_data3_2->ui16session_ID = (uint16_t)p_ctx->session_id;
          p_data3->src_cpu = p_data3_1->src_cpu = p_data3_2->src_cpu =
              (uint8_t)NI_DEVICE_TYPE_DECODER;

          p_data3->output_idx = 0;
          p_data3_1->output_idx = 1;
          p_data3_2->output_idx = 2;

          ni_log(NI_LOG_DEBUG, 
              "p_data3_1:sei_size=%d device_handle=%d == hw_id=%d ses_id=%d\n",
              sei_size, p_data3_1->device_handle, p_ctx->hw_id,
              p_data3_1->ui16session_ID);
          ni_log(NI_LOG_DEBUG,
                 "p_data3_1: ui16FrameIdx=%d NodeAddre=0x%x planar=%d\n",
                 p_data3_1->ui16FrameIdx, p_data3_1->ui32nodeAddress,
                 p_data3_1->encoding_type);
          ni_log(
              NI_LOG_DEBUG,
              "p_data3_2:sei_size=%d device_handle=%d == hw_id=%d ses_id=%d\n",
              sei_size, p_data3_2->device_handle, p_ctx->hw_id,
              p_data3_2->ui16session_ID);
          ni_log(NI_LOG_DEBUG,
                 "p_data3_2: ui16FrameIdx=%d NodeAddre=0x%x planar=%d\n",
                 p_data3_2->ui16FrameIdx, p_data3_2->ui32nodeAddress,
                 p_data3_2->encoding_type);

          ni_log(NI_LOG_DEBUG,
                 "%s:sei_size=%d device_handle=%d == hw_id=%d "
                 "ses_id=%d\n",
                 __func__, sei_size, p_data3->device_handle, p_ctx->hw_id,
                 p_data3->ui16session_ID);
          ni_log(NI_LOG_DEBUG,
                 "%s: ui16FrameIdx=%d NodeAddre=0x%x, "
                 "planar=%d\n",
                 __func__, p_data3->ui16FrameIdx, p_data3->ui32nodeAddress,
                 p_data3->encoding_type);
      }

      total_bytes_to_read = total_bytes_to_read + sei_size;
      ni_log(NI_LOG_DEBUG,
             "decoder read desc success, retval %d "
             "total_bytes_to_read include sei %u sei_size %d\n",
             __func__, retval, total_bytes_to_read, sei_size);

      if (total_bytes_to_read > NI_MEM_PAGE_ALIGNMENT)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): Oversized metadata!\n", __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
  }

  //bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition
  //total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + metadata_hdr_size + sei_size; //since only HW desc
  bytes_read_so_far = total_bytes_to_read;
  //bytes_read_so_far = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + metadata_hdr_size + sei_size; //since only HW desc
  rx_size = ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, true);
  p_ctx->frame_pkt_offset = frame_offset;

  if (rx_size > 0)
  {
      ni_log(NI_LOG_DEBUG, "%s(): s-state %d first_frame %d\n", __func__,
             p_ctx->session_run_state, p_ctx->is_first_frame);
      if (ni_timestamp_get_with_threshold(
              p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
              XCODER_FRAME_OFFSET_DIFF_THRES, 0,
              p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
          if (p_ctx->last_dts != NI_NOPTS_VALUE && !p_ctx->ready_to_close)
          {
              p_ctx->pic_reorder_delay++;
              p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
              ni_log(NI_LOG_DEBUG, "Padding DTS: %" PRId64 "\n", p_frame->dts);
          } else
          {
              p_frame->dts = NI_NOPTS_VALUE;
          }
      }

      if (p_ctx->is_first_frame)
      {
          for (i = 0; i < p_ctx->pic_reorder_delay; i++)
          {
              if (p_ctx->last_pts == NI_NOPTS_VALUE &&
                  p_ctx->last_dts == NI_NOPTS_VALUE)
              {
                  // If the p_frame->pts is unknown in the very beginning we assume
                  // p_frame->pts == 0 as well as DTS less than PTS by 1000 * 1/timebase
                  if (p_frame->pts >= p_frame->dts &&
                      p_frame->pts - p_frame->dts < 1000)
                  {
                      break;
                  }
              }

              if (ni_timestamp_get_with_threshold(
                      p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                      XCODER_FRAME_OFFSET_DIFF_THRES,
                      p_ctx->frame_num % 500 == 0,
                      p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
              {
                  p_frame->dts = NI_NOPTS_VALUE;
              }
          }
          // Reset for DTS padding counting
          p_ctx->pic_reorder_delay = 0;
      }

    if (p_ctx->is_dec_pkt_512_aligned)
    {
      if (p_ctx->is_first_frame)
      {
        p_ctx->is_first_frame = 0;

        if (p_frame->dts == NI_NOPTS_VALUE)
        {
          p_frame->pts = NI_NOPTS_VALUE;
        }
        // if not a bitstream retrieve the pts of the frame corresponding to the first YUV output
        else if((p_ctx->pts_offsets[0] != NI_NOPTS_VALUE) && (p_ctx->pkt_index != -1))
        {
          ni_metadata_dec_frame_t* p_metadata =
                  (ni_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer +
                  p_frame->data_len[0] + p_frame->data_len[1] +
                  p_frame->data_len[2] + p_frame->data_len[3]);
          int num_fw_pkts =
              (int)p_metadata->metadata_common.ui64_data.frame_offset / 512;
          ni_log(NI_LOG_DEBUG, "%s: num_fw_pkts %d frame_offset %" PRIu64 "\n",
                 __func__, num_fw_pkts,
                 p_metadata->metadata_common.ui64_data.frame_offset);
          int idx = 0;
          uint64_t cumul = p_ctx->pkt_offsets_index[0];
          bool bFound = (num_fw_pkts >= cumul);
          while (cumul < num_fw_pkts) // look for pts index
          {
              ni_log(NI_LOG_DEBUG, "%s: cumul %" PRIu64 "\n", __func__, cumul);
              if (idx == NI_MAX_DEC_REJECT)
              {
                  ni_log(NI_LOG_ERROR,
                         "Invalid index computation > "
                         "NI_MAX_DEC_REJECT!\n");
                  break;
              } else
            {
              idx ++;
              cumul += p_ctx->pkt_offsets_index[idx];
              ni_log(NI_LOG_DEBUG,
                     "%s:  idx %d pkt_offsets_index[idx] %" PRIu64 "\n",
                     __func__, idx, p_ctx->pkt_offsets_index[idx]);
            }
          }
          //if ((idx != NI_MAX_DEC_REJECT) && (idx >= 0))
          if ((idx != NI_MAX_DEC_REJECT) && bFound)
          {
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_frame->flags = p_ctx->flags_array[idx];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
            ni_log(NI_LOG_DEBUG,
                   "%s: (first frame) idx %d last_dts %" PRId64 ""
                   " dts %" PRId64 " last_pts %" PRId64 " pts %" PRId64 "\n",
                   __func__, idx, p_ctx->last_dts, p_frame->dts,
                   p_ctx->last_pts, p_frame->pts);
          } else if (idx != NI_MAX_DEC_REJECT &&
                     p_ctx->session_run_state == SESSION_RUN_STATE_RESETTING)
          {
              ni_log(NI_LOG_DEBUG,
                     "xcoder_dec_receive(): session %u recovering and "
                     "adjusting ts.\n",
                     __func__, p_ctx->session_id);
              p_frame->pts = p_ctx->pts_offsets[idx];
              p_frame->flags = p_ctx->flags_array[idx];
              p_ctx->last_pts = p_frame->pts;
              p_ctx->last_dts = p_frame->dts;
              p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
          }
          else // use pts = 0 as offset
          {
            p_frame->pts = 0;
            ni_log(NI_LOG_DEBUG,
                   "%s: (zero default) dts %" PRId64 " pts "
                   "%" PRId64 "\n",
                   __func__, p_frame->dts, p_frame->pts);
          }
        }
        else
        {
          p_frame->pts = 0;
          ni_log(NI_LOG_DEBUG,
                 "%s:  (not bitstream) dts %" PRId64 " pts "
                 "%" PRId64 "\n",
                 __func__, p_frame->dts, p_frame->pts);
        }
      }
      else
      {
          int64_t pts_delta = p_frame->dts - p_ctx->last_dts;
          p_frame->pts = p_ctx->last_pts + pts_delta;

          ni_log(NI_LOG_DEBUG,
                 "%s:  (!is_first_frame idx) last_dts %" PRId64 ""
                 " dts %" PRId64 " pts_delta %" PRId64 " last_pts "
                 "%" PRId64 " pts %" PRId64 "\n",
                 __func__, p_ctx->last_dts, p_frame->dts, pts_delta,
                 p_ctx->last_pts, p_frame->pts);
      }
    }
    else
    {
        ni_log(NI_LOG_DEBUG, "%s: frame_offset %" PRIu64 "\n", __func__,
               frame_offset);
        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
        // search for the pkt_offsets of received frame according to frame_offset.
        // here we get the index(i) which promises (p_ctx->pkt_offsets_index_min[i] <= frame_offset && p_ctx->pkt_offsets_index[i] > frame_offset)
        // i = -1 if not found
        i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                        p_ctx->pkt_offsets_index, NI_FIFO_SZ,
                                        frame_offset);
        if (i >= 0)
        {
            p_frame->pts = p_ctx->pts_offsets[i];
            p_frame->flags = p_ctx->flags_array[i];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
            ni_log(NI_LOG_DEBUG,
                   "%s: (found pts) dts %" PRId64 " pts "
                   "%" PRId64 " frame_offset %" PRIu64 " i %d "
                   "pkt_offsets_index_min %" PRIu64 " "
                   "pkt_offsets_index %" PRIu64 "\n",
                   __func__, p_frame->dts, p_frame->pts, frame_offset, i,
                   p_ctx->pkt_offsets_index_min[i],
                   p_ctx->pkt_offsets_index[i]);

            p_frame->p_custom_sei_set = p_ctx->pkt_custom_sei_set[i];
            p_ctx->pkt_custom_sei_set[i] = NULL;
        } else
        {
            //backup solution pts
            p_frame->pts = p_ctx->last_pts + (p_frame->dts - p_ctx->last_dts);
            ni_log(NI_LOG_ERROR,
                   "ERROR: NO pts found consider increasing NI_FIFO_SZ!\n");
            ni_log(NI_LOG_DEBUG,
                   "%s: (not found use default) dts %" PRId64 " pts %" PRId64
                   "\n",
                   __func__, p_frame->dts, p_frame->pts);
        }
    }

    p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts);
    p_ctx->last_pts = p_frame->pts;
    if (p_frame->dts != NI_NOPTS_VALUE && p_ctx->last_dts != NI_NOPTS_VALUE)
        p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    p_ctx->last_dts = p_frame->dts;
    ni_log(NI_LOG_DEBUG, "%s: (best_effort_timestamp) pts %" PRId64 "\n",
           __func__, p_frame->pts);
    p_ctx->frame_num++;
  }

#ifdef XCODER_DUMP_ENABLED
  fwrite(p_frame->data[0], rx_size, 1, p_ctx->p_dump[1]);
#endif

  ni_log(NI_LOG_DEBUG, "%s(): received data: [0x%08x]\n", __func__, rx_size);
  ni_log(NI_LOG_DEBUG,
         "%s(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_DEBUG, "%s(): p_frame->data_len[0/1/2]=%u/%u/%u\n", __func__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
      ni_log(NI_LOG_DEBUG,
             "Decoder pts queue size = %d  dts queue size = %d\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
      // scan and clean up
      ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue,
                                p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      abs_time_ns = ni_gettime_ns();
      ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,dLAT:%lu;\n", p_frame->dts,
          abs_time_ns - p_ctx->prev_read_frame_time,
          ni_lat_meas_q_check_latency((ni_lat_meas_q_t *)p_ctx->frame_time_q,
              abs_time_ns, p_frame->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_TRACE, "%s(): bad exit, retval = %d\n", __func__, retval);
        return retval;
    } else
    {
        ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __func__, rx_size);
        return rx_size;
    }
}

/*!******************************************************************************
*  \brief  Retrieve a YUV p_frame from decoder
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwdownload_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame, niFrameSurface1_t* hwdesc)
{
  int retval = NI_RETCODE_SUCCESS;
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t *p_data_buffer;
  int i = 0;
  int metadata_hdr_size = NI_FW_META_DATA_SZ -
      NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
  int sei_size = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  static long long decq_count = 0LL;
  int keep_processing = 1;
  int query_retry = 0;
  uint32_t ui32LBA = 0;

  //ni_log(NI_LOG_DEBUG, "hwcontext.c:ni_hwdl_frame() hwdesc %d %d %d\n",
  //    hwdesc->ui16FrameIdx,
  //    hwdesc->i8InstID,
  //    hwdesc->ui16session_ID);

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_frame))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  for (i = 0; i < NI_MAX_NUM_DATA_POINTERS - 1; i++) //discount the hwdesc
  {
    if (!p_frame->p_data[i])
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): No receive buffer allocated.\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }
  }

  if (0 == p_frame->data_len[0])
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): p_frame->data_len[0] = 0!.\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (hwdesc->encoding_type == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR)
  {
      p_frame->data_len[2] = 0;
  }
  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
    p_frame->data_len[2];// +metadata_hdr_size;
  unsigned int bytes_read_so_far = 0;
  uint32_t output_chunk_offset = hwdesc->ui32nodeAddress / FRAME_CHUNK_INDEX_SIZE; //for reading output1 or output2
  uint32_t output_minor_offset = hwdesc->ui32nodeAddress - output_chunk_offset * FRAME_CHUNK_INDEX_SIZE;
  ni_log(NI_LOG_DEBUG, "Total bytes to download %u, start offset = %u, chunkOffset "
                 "%u, minorOffset %u\n",
                 total_bytes_to_read, hwdesc->ui32nodeAddress,
                 output_chunk_offset, output_minor_offset);

  ni_log(NI_LOG_DEBUG, "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  //Apply read configuration here
  retval =
      ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 1,
                           (output_minor_offset << NI_CODEC_HW_PAYLOAD_OFFSET) |
                               NI_CODEC_HW_ENABLE | NI_CODEC_HW_DOWNLOAD,
                           hwdesc->ui16FrameIdx);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  read_size_bytes = total_bytes_to_read;
  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, hwdesc->src_cpu);
  ui32LBA += output_chunk_offset;
  if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
  {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_nvme_send_read_cmd(
      (ni_device_handle_t)(int64_t)hwdesc->device_handle,
      NI_INVALID_DEVICE_HANDLE, p_data_buffer, read_size_bytes, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else
  {
      ni_log(NI_LOG_DEBUG,
             "decoder read desc success, retval %d "
             "total_bytes_to_read include sei %u sei_size %d\n",
             __func__, retval, total_bytes_to_read, sei_size);
  }

  //Unset applied read configuration here
  retval = ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 0, 0, 0);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  if (p_ctx->is_auto_dl)
      rx_size = (int)bytes_read_so_far;
  else
  {
      rx_size =
          ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
      p_ctx->frame_pkt_offset = frame_offset;
  }


#ifdef XCODER_DUMP_ENABLED
  fwrite(p_frame->data[0], rx_size, 1, p_ctx->p_dump[1]);
#endif

  ni_log(NI_LOG_DEBUG, "xcoder_dec_receive(): received data: [0x%08x]\n",
         __func__, rx_size);
  ni_log(NI_LOG_DEBUG,
         "xcoder_dec_receive(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_DEBUG,
         "%s(): p_ctx->frame_num %" PRIu64 ", "
         "p_frame->data_len[0/1/2]=%u/%u/%u\n",
         __func__, p_ctx->frame_num, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2]);

  //if (decq_count % 500 == 0)
  //{
  //    ni_log(NI_LOG_DEBUG, "Decoder pts queue size = %d  dts queue size = %d\n\n",
  //        p_ctx->pts_table->list.count, p_ctx->dts_queue)->list.count);
  //    // scan and clean up
  //    ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue, p_ctx->buffer_pool);
  //}

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __func__, retval);
        return retval;
    } else
    {
        ni_log(NI_LOG_TRACE, "%s(): exit, rx_size = %d\n", __func__, rx_size);
        return rx_size;
    }
}

/*!******************************************************************************
*  \brief  Close an xcoder upload instance
*
*  \param   p_ctx     pointer to uploader session context
*
*  \return  NI_RETCODE_SUCCESS
*******************************************************************************/
ni_retcode_t ni_uploader_session_close(ni_session_context_t* p_ctx)
{
#ifndef _WIN32
    if (p_ctx->isP2P)
    {
        if (p_ctx->netint_fd)
        {
            ni_log(NI_LOG_DEBUG, "%s: close driver fd %d\n", __func__,
                   p_ctx->netint_fd);
            close(p_ctx->netint_fd);
        }
    }
#endif
    return NI_RETCODE_SUCCESS;
}

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
                                                    uint32_t pool)
{
    void* p_uploader_config = NULL;
    ni_uploader_config_t* p_cfg = NULL;
    uint32_t buffer_size = sizeof(ni_encoder_config_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    int i = 0;
    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_uploader_config, sysconf(_SC_PAGESIZE),
                          buffer_size))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_uploader_config, 0, buffer_size);//

    p_cfg = (ni_uploader_config_t*)p_uploader_config;
    p_cfg->ui16picWidth = p_ctx->active_video_width;
    p_cfg->ui16picHeight = p_ctx->active_video_height;
    p_cfg->ui8poolSize = pool_size;
    p_cfg->ui8PixelFormat = p_ctx->pixel_format;
    p_cfg->ui8Pool = pool;

    ni_log(NI_LOG_DEBUG, "ni_config_instance_set_uploader_params():%d x %d x Format %d with %d framepool\n",
        p_cfg->ui16picWidth, p_cfg->ui16picHeight, p_cfg->ui8PixelFormat, p_cfg->ui8poolSize);

    //configure the session here
    ui32LBA =
        CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_uploader_config, buffer_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      //Close the session since we can't configure it
      retval = ni_encoder_session_close(p_ctx, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log(NI_LOG_ERROR, "ERROR: ni_uploader_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      }

      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_uploader_config);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
*  \brief  Send a p_config command to configure decoding parameters.
*
*  \param   ni_session_context_t p_ctx - xcoder Context
*  \param   uint32_t max_pkt_size - overwrite maximum packet size if nonzero
*
*  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_config_instance_set_decoder_params(ni_session_context_t* p_ctx, uint32_t max_pkt_size)
{
  void* p_decoder_config = NULL;
  ni_decoder_config_t* p_cfg = NULL;
  uint32_t buffer_size = sizeof(ni_decoder_config_t);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int i = 0;
  uint32_t ui32LBA = 0;
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_decoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_decoder_config buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_decoder_config, 0, buffer_size);

  ni_set_custom_dec_template(p_ctx, p_decoder_config, p_ctx->p_session_config, max_pkt_size);
  retval = ni_validate_custom_dec_template(p_ctx->p_session_config, p_ctx, p_decoder_config, p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg));
  if (NI_RETCODE_PARAM_WARN == retval)
  {
      ni_log(NI_LOG_INFO, "WARNING: %s . %s\n", __func__, p_ctx->param_err_msg);
      fflush(stdout);
  }
  else if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s failed. %s\n", __func__,
             p_ctx->param_err_msg);
      fflush(stdout);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  // configure the session here
  ui32LBA = CONFIG_INSTANCE_SetDecPara_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  //Flip the bytes!!
  //p_cfg = (ni_decoder_config_t*)p_decoder_config;
  //p_cfg->i32picWidth = ni_htonl(p_cfg->i32picWidth);
  //p_cfg->i32picHeight = ni_htonl(p_cfg->i32picHeight);
  //p_cfg->i32meBlkMode = ni_htonl(p_cfg->i32meBlkMode);
  //p_cfg->i32frameRateInfo = ni_htonl(p_cfg->i32frameRateInfo);
  //p_cfg->i32vbvBufferSize = ni_htonl(p_cfg->i32vbvBufferSize);
  //p_cfg->i32userQpMax = ni_htonl(p_cfg->i32userQpMax);
  //p_cfg->i32maxIntraSize = ni_htonl(p_cfg->i32maxIntraSize);
  //p_cfg->i32userMaxDeltaQp = ni_htonl(p_cfg->i32userMaxDeltaQp);
  //p_cfg->i32userMinDeltaQp = ni_htonl(p_cfg->i32userMinDeltaQp);
  //p_cfg->i32userQpMin = ni_htonl(p_cfg->i32userQpMin);
  //p_cfg->i32bitRate = ni_htonl(p_cfg->i32bitRate);
  //p_cfg->i32bitRateBL = ni_htonl(p_cfg->i32bitRateBL);
  //p_cfg->i32srcBitDepth = ni_htonl(p_cfg->i32srcBitDepth);
  //p_cfg->hdrEnableVUI = ni_htonl(p_cfg->hdrEnableVUI);
  //p_cfg->ui32VuiDataSizeBits = ni_htonl(p_cfg->ui32VuiDataSizeBits);
  //p_cfg->ui32VuiDataSizeBytes = ni_htonl(p_cfg->ui32VuiDataSizeBytes);
  //p_cfg->i32hwframes = ni_htonl(p_cfg->i32hwframes);
  // flip the 16 bytes of the reserved field using 32 bits pointers
  //for (i = 0; i < (16 >> 2); i++)
  //{
  //  ((uint32_t*)p_cfg->ui8Reserved)[i] = ni_htonl(((uint32_t*)p_cfg->ui8Reserved)[i]);
  //}
  //// flip the NI_MAX_VUI_SIZE bytes of the VUI field using 32 bits pointers
  //for (i = 0; i < (NI_MAX_VUI_SIZE >> 2); i++) // apply on 32 bits
  //{
  //  ((uint32_t*)p_cfg->ui8VuiRbsp)[i] = ni_htonl(((uint32_t*)p_cfg->ui8VuiRbsp)[i]);
  //}

  ni_log(NI_LOG_DEBUG, "%s: ui32LBA  = 0x%x\n", __func__, ui32LBA);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_decoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_decoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_decoder_config);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

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
    ni_frame_t *p_frame)
{
    ni_retcode_t retval;
    ni_instance_buf_info_t sInstanceBuf = {0};
    niFrameSurface1_t *pFrameSurface;
    int query_retry = 0;

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    for (;;)
    {
        query_retry++;

        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                            NI_DEVICE_TYPE_SCALER, &sInstanceBuf);

        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
            p_ctx->device_type, p_ctx->hw_id,
            &(p_ctx->session_id));

        if (retval != NI_RETCODE_SUCCESS)
        {
            if (query_retry >= 1000)
            {
                ni_log(NI_LOG_DEBUG, "Warning hwdesc read fail rc %d\n", retval);
                LRETURN;
            }
        }
        else
        {
            pFrameSurface = (niFrameSurface1_t *) p_frame->p_data[3];
            pFrameSurface->ui16FrameIdx = sInstanceBuf.hw_inst_ind.frame_index;
            pFrameSurface->ui16session_ID = p_ctx->session_id;
            pFrameSurface->device_handle =
                (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
            pFrameSurface->src_cpu = (uint8_t) NI_DEVICE_TYPE_SCALER;
            pFrameSurface->output_idx = 0;

            /* A frame index of zero is invalid, the memory acquisition failed */
            if (pFrameSurface->ui16FrameIdx == 0)
            {
                if (query_retry >= 1000)
                {
                    ni_log(NI_LOG_ERROR, "Warning: 2D could not acquire frame\n");
                    retval = NI_RETCODE_FAILURE;
                    LRETURN;
                }
                continue;
            }
            LRETURN;
        }
    }

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_DEBUG,
               "Warning scalar read hwdesc fail rc %d or ind "
               "!\n",
               __func__, retval);
        retval = NI_RETCODE_FAILURE;
    }

    return retval;
}

/*!******************************************************************************
*  \brief  Grab bitdepth factor from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         1 or 2 for success, -1 for error
*******************************************************************************/
int ni_get_bitdepth_factor_from_pixfmt(int pix_fmt)
{
  switch (pix_fmt)
  {
  case NI_PIX_FMT_YUV420P:
  case NI_PIX_FMT_NV12:
    return 1;
  case NI_PIX_FMT_YUV420P10LE:
  case NI_PIX_FMT_P010LE:
    return 2;
  default:
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() non applicable format %d\n", __func__,
               pix_fmt);
        break;
    }
  }
  return -1;
}

/*!******************************************************************************
*  \brief  Grab planar info from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         0 or 1 for success, -1 for error
*******************************************************************************/
int ni_get_planar_from_pixfmt(int pix_fmt)
{
  switch (pix_fmt)
  {
  case NI_PIX_FMT_YUV420P:
  case NI_PIX_FMT_YUV420P10LE:
    return 1;
    break;
  case NI_PIX_FMT_NV12:
  case NI_PIX_FMT_P010LE:
    return 0;
    break;
  default:
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() non applicable format %d\n", __func__,
               pix_fmt);
        break;
    }
  }
  return -1;
}

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
ni_retcode_t ni_get_memory_offset(ni_session_context_t *p_ctx, const niFrameSurface1_t *hwdesc,
                                  uint32_t *p_offset)
{
    if (!hwdesc)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed null parameter\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (hwdesc->ui16FrameIdx <= NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config) ||
        hwdesc->ui16FrameIdx > NI_GET_MAX_HWDESC_P2P_BUF_ID(p_ctx->ddr_config))
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() pass invalid data\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    *p_offset = (hwdesc->ui16FrameIdx - NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config)) *
        NI_HWDESC_UNIFIED_MEMBIN_SIZE;

    return NI_RETCODE_SUCCESS;
}
#endif

/* AI functions */
ni_retcode_t ni_config_instance_network_binary(ni_session_context_t *p_ctx,
                                               void *nb_data, uint32_t nb_size)
{
    void *p_ai_config = NULL;
    void *p_nb_data = NULL;
    uint32_t buffer_size;
    //    uint8_t *p_data;
    //    uint32_t transferred, this_size;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t config_size;
    int32_t retry_count = 0;
    void *p_buffer = NULL;
    uint32_t dataLen;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    config_size = (sizeof(ni_ai_config_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_ai_config, sysconf(_SC_PAGESIZE), config_size))
    {
        ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate ai config buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    ((ni_ai_config_t *)p_ai_config)->ui32NetworkBinarySize = nb_size;
    ni_calculate_sha256(nb_data, nb_size,
                        ((ni_ai_config_t *)p_ai_config)->ui8Sha256);

    buffer_size =
        (nb_size + (NI_MEM_PAGE_ALIGNMENT - 1)) & ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_nb_data, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate encConf buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    /* FIXME: can be avoided? */
    memcpy(p_nb_data, nb_data, nb_size);

    /* configure network binary size to be written */
    ui32LBA = CONFIG_INSTANCE_SetAiPara_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log(NI_LOG_DEBUG, "%s(): LBA 0x%x, nb_size %u\n", __func__, ui32LBA,
           nb_size);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_ai_config, config_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_ai_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                   ", hw_id, %u, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    /* test if the model is already exist. if not, then continue to write binary data */
    dataLen = (sizeof(ni_network_layer_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, dataLen);

    ui32LBA = QUERY_INSTANCE_NL_SIZE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, dataLen, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "%s(): network binary registered\n", __func__);
        LRETURN;
    }

#if 1
    /* write network binary data */
    ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log(NI_LOG_DEBUG, "%s(): write nb LBA 0x%x\n", __func__, ui32LBA);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_nb_data, buffer_size, ui32LBA);
    ni_log(NI_LOG_DEBUG, "write complete retval %d\n", retval);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_ai_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                   ", hw_id, %u, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

#else
    ni_log(NI_LOG_DEBUG, "%s(): write nb buffer_size %u\n", __func__,
           buffer_size);
    for (transferred = 0; transferred < buffer_size; transferred += this_size)
    {
        this_size = p_ctx->max_nvme_io_size < (buffer_size - transferred) ?
            p_ctx->max_nvme_io_size :
            (buffer_size - transferred);

        if (this_size & (4096 - 1))
        {
            this_size = (this_size + (4096 - 1)) & ~(4096 - 1);
        }
        /* write network binary data */
        ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI) +
            (transferred >> 12);
        ni_log(NI_LOG_DEBUG,
               "%s(): write nb LBA 0x%x, this_size %u, page_offset %u\n",
               __func__, ui32LBA, this_size, (transferred >> 12));
        p_data = (uint8_t *)p_nb_data + transferred;
        retval = ni_nvme_send_write_cmd(
            p_ctx->blk_io_handle, p_ctx->event_handle,
            (uint8_t *)p_nb_data + transferred, this_size, ui32LBA);
        ni_log(NI_LOG_DEBUG, "%s(): write retval %d\n", __func__, retval);
        CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id));
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(
                NI_LOG_ERROR,
                "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %u, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
            //Close the session since we can't configure it
            retval = ni_ai_session_close(p_ctx, 0);
            if (NI_RETCODE_SUCCESS != retval)
            {
                ni_log(
                    NI_LOG_ERROR,
                    "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                    ", hw_id, %u, xcoder_inst_id: %d\n",
                    (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                    p_ctx->session_id);
            }

            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

#endif

END:

    ni_aligned_free(p_ai_config);
    ni_aligned_free(p_nb_data);
    ni_aligned_free(p_buffer);

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_session_write(ni_session_context_t *p_ctx,
                                 ni_frame_t *p_frame)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    int i = 0;
    uint32_t ui32LBA = 0;
    ni_instance_buf_info_t buf_info = {0};
    uint32_t frame_size_bytes;
    uint32_t sent_size = 0;
    int32_t query_retry = 0;
    int32_t query_count = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_frame)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters is null\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (p_frame->data_len[0] == 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() invalid data length\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    frame_size_bytes = p_frame->data_len[0];
    ni_log(NI_LOG_DEBUG, "%s(): frame_size_bytes %u\n", __func__,
           frame_size_bytes);

    if (!p_frame->end_of_stream)
    {
        for (;;)
        {
            retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                                NI_DEVICE_TYPE_AI, &buf_info);
            CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                         p_ctx->device_type, p_ctx->hw_id,
                         &(p_ctx->session_id));

            if (NI_RETCODE_SUCCESS != retval ||
                buf_info.buf_avail_size < frame_size_bytes)
            {
                
                ni_log(NI_LOG_TRACE, "AI write query failed or buf_size < "
                       "frame_size. Retry %d\n", query_retry);
                // extend to 5000 retries for 8K encode on FPGA
                if (query_retry >= NI_MAX_ENCODER_QUERY_RETRIES)
                {
                    ni_log(NI_LOG_ERROR,
                           "ERROR: ai query buf info exceeding max retries: "
                           "%d, ",
                           NI_MAX_ENCODER_QUERY_RETRIES);
                    p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
                    retval = NI_RETCODE_SUCCESS;
                    LRETURN;
                }
                query_retry++;
                ni_usleep(100);
            } else
            {
                ni_log(NI_LOG_DEBUG, "Info ai write query success, available buf "
                               "size %u >= frame size %u !\n",
                               buf_info.buf_avail_size, frame_size_bytes);
                break;
            }
        }
    }

    ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log(NI_LOG_DEBUG, "Ai session write: p_data = %p, p_frame->buffer_size = %u, "
                   "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                   p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
                   ui32LBA);

    sent_size = frame_size_bytes;
    if (sent_size & (NI_MEM_PAGE_ALIGNMENT - 1))
    {
        sent_size = (sent_size + NI_MEM_PAGE_ALIGNMENT - 1) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
    }

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_frame->p_buffer, sent_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id));
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    p_ctx->frame_num++;

    retval = frame_size_bytes;

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_session_read(ni_session_context_t *p_ctx,
                                ni_packet_t *p_packet)
{
    uint32_t actual_read_size = 0;
    uint8_t *p_data = NULL;
    int retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    ni_instance_buf_info_t buf_info = {0};
    int retry_count = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_packet || !p_packet->p_data)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    for (;;)
    {
        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_READ,
                                            NI_DEVICE_TYPE_AI, &buf_info);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));

        ni_log(NI_LOG_DEBUG, "Info ai read query rc %d, available buf size %u, "
                       "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 "\n",
                       retval, buf_info.buf_avail_size, p_ctx->frame_num,
                       p_ctx->pkt_num);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_DEBUG, "Buffer info query failed in ai read!!!!\n");
            LRETURN;
        } else if (0 == buf_info.buf_avail_size)
        {
            ni_log(NI_LOG_DEBUG, "Info ai read available buf size %u, eos %u !\n",
                           buf_info.buf_avail_size, p_packet->end_of_stream);
            retval = NI_RETCODE_SUCCESS;
            LRETURN;
        } else
        {
            break;
        }
    }
    ni_log(NI_LOG_DEBUG, "Ai read buf_avail_size %u\n", buf_info.buf_avail_size);

    assert(buf_info.buf_avail_size == p_packet->data_len);

    ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    actual_read_size = buf_info.buf_avail_size;
    if (actual_read_size & (NI_MEM_PAGE_ALIGNMENT - 1))
    {
        actual_read_size = (actual_read_size + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
    }

    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_packet->p_data, actual_read_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id));
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    retval = p_packet->data_len;

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_config_read_inout_layers(ni_session_context_t *p_ctx,
                                         ni_network_data_t *p_network)
{
    void *p_buffer = NULL;
    void *p_info = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t dataLen;
    int32_t query_retry = 0;
    int l;
    uint32_t offset = 0;
    ni_network_layer_params_t *layer_param;
    uint32_t buffer_size;
    uint32_t this_size;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx || !p_network)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    /* query available size can be read. */
    dataLen = (sizeof(ni_network_layer_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, dataLen);

    for (;;)
    {
        ui32LBA =
            QUERY_INSTANCE_NL_SIZE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, dataLen, ui32LBA) < 0)
        {
            ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size > 0)
        {
            break;
        }

        query_retry++;
        if (query_retry > 10000)
        {
            ni_log(NI_LOG_DEBUG, "%s: query retry exceeds\n", __func__);
        }
        ni_usleep(200);
        continue;
    }

    if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size == 0)
    {
        retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
        LRETURN;
    }

    assert(((ni_instance_buf_info_t *)p_buffer)->buf_avail_size ==
           sizeof(ni_network_layer_info_t));

    /* query the real network layer data */
    dataLen = (sizeof(ni_network_layer_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_info, sysconf(_SC_PAGESIZE), dataLen))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate info buffer\n",
             NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_info, 0, dataLen);

    ui32LBA = QUERY_INSTANCE_NL_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_info, dataLen, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    p_network->linfo = *((ni_network_layer_info_t *)p_info);

    for (l = 0, p_network->input_num = 0, buffer_size = 0;
         l < NI_MAX_NETWORK_INPUT_NUM; l++)
    {
        layer_param = &p_network->linfo.in_param[l];
        if (layer_param->num_of_dims == 0)
        {
            break;
        }
        this_size = ni_ai_network_layer_size(layer_param);
        this_size =
            (this_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        p_network->inset[l].offset = buffer_size;
        buffer_size += this_size;

        p_network->input_num++;
        ni_log(
            NI_LOG_DEBUG,
            "%s(): network input layer %d: dims %u, %u/%u/%u/%u, f %d, q %d\n",
            __func__, l, layer_param->num_of_dims, layer_param->sizes[0],
            layer_param->sizes[1], layer_param->sizes[2], layer_param->sizes[3],
            layer_param->data_format, layer_param->quant_format);
    }

    for (l = 0, p_network->output_num = 0, buffer_size = 0;
         l < NI_MAX_NETWORK_OUTPUT_NUM; l++)
    {
        layer_param = &p_network->linfo.out_param[l];
        if (layer_param->num_of_dims == 0)
        {
            break;
        }
        this_size = ni_ai_network_layer_size(layer_param);
        this_size =
            (this_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        p_network->outset[l].offset = buffer_size;
        buffer_size += this_size;

        p_network->output_num++;
        ni_log(
            NI_LOG_DEBUG,
            "%s(): network output layer %d: dims %u, %u/%u/%u/%u, f %d, q %d\n",
            __func__, l, layer_param->num_of_dims, layer_param->sizes[0],
            layer_param->sizes[1], layer_param->sizes[2], layer_param->sizes[3],
            layer_param->data_format, layer_param->quant_format);
    }

    retval = NI_RETCODE_SUCCESS;

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_info);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

ni_retcode_t ni_ai_session_open(ni_session_context_t *p_ctx)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t buffer_size = 0;
    void *p_buffer = NULL;
    uint32_t ui32LBA = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    //Check if there is an instance or we need a new one
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        p_ctx->device_type = NI_DEVICE_TYPE_AI;
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
        p_ctx->ready_to_close = 0;
        //Sequence change tracking reated stuff
        p_ctx->active_video_width = 0;
        p_ctx->active_video_height = 0;
        p_ctx->p_all_zero_buf = NULL;
        p_ctx->actual_video_width = 0;
        p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;

        memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));

        //malloc zero data buffer
        if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                              NI_DATA_BUFFER_LEN))
        {
            ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

        //malloc data buffer
        if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE),
                              NI_DATA_BUFFER_LEN))
        {
            ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

        //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
        //In case we can open sesison, the session id would become valid.
        ((ni_session_stats_t *)p_buffer)->ui16SessionId =
            (uint16_t)NI_INVALID_SESSION_ID;

        // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
        ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_AI, 0, p_ctx->hw_action);
        ni_log(NI_LOG_DEBUG, "%s(): LBA 0x%x, hw_action %d\n", __func__,
               ui32LBA, p_ctx->hw_action);
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log(NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
            LRETURN;
        }
        //Open will return a session status structure with a valid session id if it worked.
        //Otherwise the invalid session id set before the open command will stay
        if ((uint16_t)NI_INVALID_SESSION_ID ==
            ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %s(): p_ctx->device_handle=%" PRIx64
                   ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
                   __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
                   p_ctx->session_id);
            ni_ai_session_close(p_ctx, 0);
            retval = NI_RETCODE_ERROR_INVALID_SESSION;
            LRETURN;
        }
        p_ctx->session_id =
            ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
        p_ctx->session_timestamp =
            ni_htonll(((ni_session_stats_t *)p_buffer)->ui64Session_timestamp);
        ni_log(NI_LOG_DEBUG, "Ai open session ID:0x%x,timestamp:%" PRIu64 "\n",
               p_ctx->session_id, p_ctx->session_timestamp);

        ni_log(NI_LOG_DEBUG, "Open session completed\n");
        ni_log(NI_LOG_DEBUG,
               "%s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);

        //Send keep alive timeout Info
        uint64_t keep_alive_timeout =
            p_ctx->keep_alive_timeout * 1000000;   //send us to FW
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
        ni_log(NI_LOG_DEBUG, "%s keep_alive_timeout %" PRIx64 "\n", __func__,
               keep_alive_timeout);
        ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
        retval =
            ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %s(): nvme write keep_alive_timeout command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

    // init for frame pts calculation
    p_ctx->is_first_frame = 1;
    p_ctx->last_pts = 0;
    p_ctx->last_dts = 0;
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

ni_retcode_t ni_ai_session_close(ni_session_context_t *p_ctx, int eos_recieved)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    void *p_buffer = NULL;
    uint32_t ui32LBA = 0;
    int counter = 0;
    int ret = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);

    int retry = 0;
    while (retry < NI_SESSION_CLOSE_RETRY_MAX)
    {
        ni_log(NI_LOG_DEBUG,
               "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
               "p_ctx->session_id=%d, close_mode=1\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
               p_ctx->session_id);

        if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
        {
            ni_log(NI_LOG_ERROR, "ERROR %s(): command failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            break;
        } else
        {
            //Close should always succeed
            retval = NI_RETCODE_SUCCESS;
            p_ctx->session_id = NI_INVALID_SESSION_ID;
            break;
        }
        /*
    else if(((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "%s(): wait for close\n", __func__);
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
        retry++;
    }

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);

    //Sequence change related stuff cleanup here
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;

    //End of sequence change related stuff cleanup
    ni_buffer_pool_free(p_ctx->buffer_pool);
    p_ctx->buffer_pool = NULL;

    ni_log(NI_LOG_DEBUG, "%s(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

#ifdef XCODER_DUMP_ENABLED
    if (p_ctx->p_dump[0])
    {
        fclose(p_ctx->p_dump[0]);
        p_ctx->p_dump[0] = NULL;
    }
    if (p_ctx->p_dump[1])
    {
        fclose(p_ctx->p_dump[1]);
        p_ctx->p_dump[1] = NULL;
    }
#endif

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_alloc_hwframe(ni_session_context_t *p_ctx, int frame_index)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_network_buffer_t *p_data = NULL;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
      ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    dataLen = (sizeof(ni_network_buffer_t) + NI_MEM_PAGE_ALIGNMENT - 1) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    //  memset(p_data, 0x00, dataLen);
    p_data->ui32Address = frame_index;

    ni_log(NI_LOG_DEBUG, "Dev alloc frame: frame_index %u\n", p_data->ui32Address);

    ui32LBA = CONFIG_INSTANCE_SetAiFrm_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

END:
    ni_aligned_free(p_data);
    return retval;
}

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
ni_retcode_t ni_device_get_ddr_configuration(ni_session_context_t *p_ctx)
{
    void *p_buffer = NULL;
    ni_nvme_identity_t *p_id_data;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
    uint32_t ui32LBA = IDENTIFY_DEVICE_R;
    ni_device_handle_t device_handle = p_ctx->blk_io_handle;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (NI_INVALID_DEVICE_HANDLE == device_handle)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): invalid passed parameters\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE),
                          NI_NVME_IDENTITY_CMD_DATA_SZ))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    memset(p_buffer, 0, NI_NVME_IDENTITY_CMD_DATA_SZ);

    if (ni_nvme_send_read_cmd(device_handle, event_handle, p_buffer,
                              NI_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
    {
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    p_id_data = (ni_nvme_identity_t *) p_buffer;
    p_ctx->ddr_config = (p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_SR)
        ? 1 : 2;

    ni_log(NI_LOG_DEBUG, "Memory configuration %d\n",p_ctx->ddr_config);
END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): retval: %d\n", __func__, retval);

    return retval;
}
