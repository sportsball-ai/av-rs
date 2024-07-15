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
 *  \file   ni_rsrc_mon.c
 *
 *  \brief  Application to query and print live performance/load info of
 *          registered NETINT video processing devices on system
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <unistd.h>
#include <signal.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"

#define MAX_DEVICE_NAME_SIZE (9)
#define ABSOLUTE_TEMP_ZERO (-273)
#define NP_LOAD (0)
#define TP_LOAD (1)
#define PCIE_LOAD (2)

uint32_t* g_temp_load = NULL; //TP load storage
uint32_t* g_temp_pload = NULL; //Pcie load storage
uint32_t* g_temp_pthroughput = NULL; //Pcie throughput storage
uint32_t* g_temp_sharemem = NULL; //TP sharedmem storage

ni_device_handle_t device_handles[NI_DEVICE_TYPE_XCODER_MAX]
                                 [NI_MAX_DEVICE_CNT] = {0};

#ifdef _ANDROID

#include <cutils/properties.h>
#define PROP_DECODER_TYPE "nidec_service_init"
#define LOG_TAG "ni_rsrc_mon"
#endif

enum outFormat
{
  FMT_TEXT,
  FMT_FULL_TEXT,
  FMT_SIMPLE_TEXT,
  FMT_JSON,
  FMT_JSON1,
  FMT_JSON2,
  FMT_EXTRA
};

#ifdef _WIN32
#include "ni_getopt.h"

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
  g_xcoder_stop_process = 1;
  return TRUE;
/*!
  switch (ctrl_type)
  {
    case CTRL_C_EVENT: // Ctrl+C
    case CTRL_BREAK_EVENT: // Ctrl+Break
    case CTRL_CLOSE_EVENT: // Closing the console window
    case CTRL_LOGOFF_EVENT: // User logs off. Passed only to services!
    case CTRL_SHUTDOWN_EVENT: // System is shutting down. Passed only to services!
    {
      g_xcoder_stop_process = 1;
      break;
      return TRUE;
    }
    default: break;
  }

  // Return TRUE if handled this message, further handler functions won't be called.
  // Return FALSE to pass this message to further handlers until default handler calls ExitProcess().
  return FALSE;
*/
}

#elif __linux__
/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void sig_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT || sig == SIGHUP)
    {
        g_xcoder_stop_process = 1;
    }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void setup_signal_handler(void)
{
    if (signal(SIGTERM, sig_handler) == SIG_ERR ||
        signal(SIGHUP, sig_handler) == SIG_ERR ||
        signal(SIGINT, sig_handler) == SIG_ERR)
    {
        perror("ERROR: signal handler setup");
    }
}

/*!******************************************************************************
 *  \brief     get PCIe address
 *
 *  \param[in] char *device_name e.g. /dev/nvme0n1
 *
 *  \return    void
 *  *******************************************************************************/
void get_pcie_addr(char *device_name, char *pcie)
{
  get_dev_pcie_addr(device_name, pcie, NULL, NULL, NULL, NULL);
}

/*!******************************************************************************
 *  \brief     get linux numa_node
 *
 *  \param[in] char *device_name
 *
 *  \return    int atoi(cmd_ret)
 *******************************************************************************/
int get_numa_node(char *device_name)
{
  int ret = -1;
  FILE *cmd_fp;
  char *ptr = NULL;
  char cmd[128] = {0};
  char cmd_ret[64] = {0};
  
  if(!device_name)
  {
    return ret;
  }
  ptr = device_name + 5;
  snprintf(cmd, sizeof(cmd) - 1, "cat /sys/block/%s/device/*/numa_node",ptr);
  cmd_fp = popen(cmd, "r");
  if (!cmd_fp)
  {
    return ret;
  }
  if (fgets(cmd_ret, sizeof(cmd_ret)/sizeof(cmd_ret[0]), cmd_fp) == 0)
  {
    goto get_numa_node_ret;
  }
  ret = atoi(cmd_ret);

get_numa_node_ret:
  pclose(cmd_fp);
  return ret;
}

#endif //__linux__

/*!******************************************************************************
 *  \brief    remove one device from stored device_handles
 *
 *  \param    ni_device_type_t device_type
 *
 *  \param    int32_t module_id
 *
 *  \param    ni_device_handle_t device_handle
 *
 *  \return   int 0 for success, -1 for failure
 *******************************************************************************/
int remove_device_from_saved(ni_device_type_t device_type, int32_t module_id,
                             ni_device_handle_t device_handle)
{
    if (!IS_XCODER_DEVICE_TYPE(device_type))
    {
        fprintf(stderr, "Error: device_type %d is not a valid device type\n",
                device_type);
        return -1;
    }
    ni_device_type_t xcoder_device_type = GET_XCODER_DEVICE_TYPE(device_type);
    if (device_handles[xcoder_device_type][module_id] == device_handle)
    {
        device_handles[xcoder_device_type][module_id] =
            NI_INVALID_DEVICE_HANDLE;
        return 0;
    } else
    {
        fprintf(stderr,
                "Error: device_handle to remove %d"
                "not match device_handles[%d][%d]=%d\n",
                device_handle, device_type, module_id,
                device_handles[xcoder_device_type][module_id]);
        return -1;
    }
}

/*!******************************************************************************
 *  \brief    convert number from argv input to integer if safe
 *
 *  \param    char *numArray
 *
 *  \return   int atoi(numArray)
 *******************************************************************************/
int argToI(char *numArray)
{
  int i;
  
  if( !numArray )
  {
    return 0;
  }
  
  const size_t len = strlen(numArray);

  for (i = 0; i < len; i++)
  {
    if (!isdigit(numArray[i]))
    {
      fprintf(stderr, "invalid, ABORTING\n");
      abort();
    }
  }

  return len == i ? atoi(numArray) : 0;
}

/*!******************************************************************************
 *  \brief     compare two int32_t for qsort
 *
 *  \param[in] const void *a
 *  \param[in] const void *b
 *
 *  \return    int atoi(numArray)
 *******************************************************************************/
int compareInt32_t(const void *a, const void *b)
{
  if ( *(int32_t*)a <  *(int32_t*)b ) return -1;
  if ( *(int32_t*)a >  *(int32_t*)b ) return 1;
  return 0;
}

unsigned int get_modules(ni_device_type_t device_type,
                         ni_device_queue_t *p_device_queue,
                         char *device_name,
                         int32_t **module_ids)
{
  unsigned int device_count;
  size_t size_of_i32;

  size_of_i32 = sizeof(int32_t);

  device_count = p_device_queue->xcoder_cnt[device_type];
  *module_ids = malloc(size_of_i32 * device_count);
  if (!(*module_ids))
  {
    fprintf(stderr, "ERROR: malloc() failed for module_ids\n");
    return 0;
  }

  memcpy(*module_ids,
         p_device_queue->xcoders[device_type],
         size_of_i32 * device_count);

  qsort(*module_ids,
        device_count,
        size_of_i32,
        compareInt32_t);

  strncpy(device_name,
          g_device_type_str[device_type],
          MAX_DEVICE_NAME_SIZE);

  return device_count;
}

bool open_and_query(ni_device_type_t device_type,
                    ni_device_context_t *p_device_context,
                    ni_session_context_t *p_session_context,
                    char *device_name, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1)
{
  ni_retcode_t return_code;

  if (!p_device_context)
  {
    return false;
  }

  // check if device has been opened already
  ni_device_type_t xcoder_device_type = GET_XCODER_DEVICE_TYPE(device_type);
  int module_id = p_device_context->p_device_info->module_id;
  if (device_handles[xcoder_device_type][module_id] != NI_INVALID_DEVICE_HANDLE)
  {
      p_session_context->device_handle =
          device_handles[xcoder_device_type][module_id];
  } else
  {
      p_session_context->device_handle =
          ni_device_open(p_device_context->p_device_info->dev_name,
                         &p_session_context->max_nvme_io_size);
      if (p_session_context->device_handle != NI_INVALID_DEVICE_HANDLE)
      {
          device_handles[xcoder_device_type][module_id] =
              p_session_context->device_handle;
      }
  }

  if (p_session_context->device_handle == NI_INVALID_DEVICE_HANDLE)
  {
    fprintf(stderr,
            "ERROR: ni_device_open() failed for %s: %s\n",
            p_device_context->p_device_info->dev_name,
            strerror(NI_ERRNO));
    ni_rsrc_free_device_context(p_device_context);
    return false;
  }

  p_session_context->blk_io_handle =
    p_session_context->device_handle;
  p_session_context->hw_id =
    p_device_context->p_device_info->hw_id;

  if(detail)
  {
    if (ni_cmp_fw_api_ver((char*) &p_device_context->p_device_info->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6i") < 0)
    {
      fprintf(stderr,
              "ERROR: cannot print detailed info for %s as it has FW API "
              "version < 6.i\n", p_device_context->p_device_info->dev_name);
      return false;
    }
    else if (ni_cmp_fw_api_ver((char*)&p_device_context->p_device_info->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6r6") < 0)
    {
        return_code = ni_device_session_query_detail(p_session_context,
                            GET_XCODER_DEVICE_TYPE(device_type), (ni_instance_mgr_detail_status_t *)detail_data_v1);
    }
    else
    {
        return_code = ni_device_session_query_detail_v1(p_session_context,
                            GET_XCODER_DEVICE_TYPE(device_type), detail_data_v1);
    }
  }
  else
  {
    return_code = ni_device_session_query(p_session_context,
                            GET_XCODER_DEVICE_TYPE(device_type));
  }
  if (return_code != NI_RETCODE_SUCCESS)
  {
    fprintf(stderr,
            "ERROR: ni_device_session_query() returned %d for %s:%s:%d\n",
            return_code,
            device_name,
            p_device_context->p_device_info->dev_name,
            p_device_context->p_device_info->hw_id);
    remove_device_from_saved(device_type, module_id,
                             p_session_context->device_handle);
    ni_device_close(p_session_context->device_handle);
    ni_rsrc_free_device_context(p_device_context);
    return false;
  }

  if (!p_session_context->load_query.total_contexts)
  {
    p_session_context->load_query.current_load = 0;
  }

  memcpy(p_session_context->fw_rev,
         p_device_context->p_device_info->fw_rev,
         8);

  return true;
}

bool open_and_get_log(ni_device_context_t *p_device_context,
                       ni_session_context_t *p_session_context,
                       void** p_log_buffer,
                       bool gen_log_file)
{
  ni_retcode_t return_code;
  if (!p_device_context)
    return false;

  p_session_context->device_handle =
    ni_device_open(p_device_context->p_device_info->blk_name,
                   &p_session_context->max_nvme_io_size);
  if (p_session_context->device_handle == NI_INVALID_DEVICE_HANDLE)
  {
    fprintf(stderr,
            "ERROR: ni_device_open() failed for %s: %s\n",
            p_device_context->p_device_info->blk_name,
            strerror(NI_ERRNO));
    ni_rsrc_free_device_context(p_device_context);
    return false;
  }
  p_session_context->blk_io_handle =
    p_session_context->device_handle;
  p_session_context->hw_id =
    p_device_context->p_device_info->module_id;

  return_code = ni_device_alloc_and_get_firmware_logs(p_session_context, p_log_buffer, gen_log_file);
  return return_code ? false : true;
}

void dump_fw_log(ni_device_queue_t *coders, ni_session_context_t *sessionCtxt, int devid)
{
  int i;
  unsigned int module_count;
  int32_t *module_id_arr = NULL;
  char module_name[MAX_DEVICE_NAME_SIZE];
  ni_device_context_t *p_device_context = NULL;
  ni_device_type_t module_type = NI_DEVICE_TYPE_DECODER;

  module_count = get_modules(module_type, coders, module_name, &module_id_arr);
  if (!module_count) {
    printf("Error: module not found!\n");
    return;
  }

  bool gen_log_file = true; // dump and write fw logs to runtime dir

  void* p_log_buffer = NULL;
  if (devid >= 0 && devid < module_count)
  {
    // dump fw logs of specified card
    p_device_context = ni_rsrc_get_device_context(module_type, module_id_arr[devid]);
    if (p_device_context->p_device_info->module_id == devid)
    {
      if (!open_and_get_log(p_device_context, sessionCtxt, &p_log_buffer, gen_log_file)) {
          printf("Error: failed to dump fw log of card:%d blk_name:%s\n",
                  devid, p_device_context->p_device_info->blk_name);
      } else {
          printf("Success: dumped fw log of card:%d blk_name:%s\n",
                  devid, p_device_context->p_device_info->blk_name);
      }
      ni_device_close(sessionCtxt->device_handle);
      ni_rsrc_free_device_context(p_device_context);
    }
  }
  else
  {
    // dump fw logs of all quadra cards
    for (i = 0; i < module_count; i++)
    {
      p_device_context = ni_rsrc_get_device_context(module_type, module_id_arr[i]);
      if (!open_and_get_log(p_device_context, sessionCtxt, &p_log_buffer, gen_log_file)) {
          printf("Error: failed to dump fw log of card:%d blk_name:%s\n",
                  p_device_context->p_device_info->module_id,
                  p_device_context->p_device_info->blk_name);
      } else {
          printf("Success: dumped fw log of card:%d blk_name:%s\n",
                  p_device_context->p_device_info->module_id,
                  p_device_context->p_device_info->blk_name);
      }
      ni_device_close(sessionCtxt->device_handle);
      ni_rsrc_free_device_context(p_device_context);
    }
  }

  free(p_log_buffer);
  free(module_id_arr);
}

bool swap_encoder_and_uploader(ni_device_type_t *p_device_type,
                               char *device_name)
{
  switch (*p_device_type)
  {
  case NI_DEVICE_TYPE_ENCODER:
    *p_device_type = NI_DEVICE_TYPE_UPLOAD;
    strcpy(device_name, "uploader");
    return true;
  case NI_DEVICE_TYPE_UPLOAD:
    *p_device_type = NI_DEVICE_TYPE_ENCODER;
  default:
    return false;
  }
}

#define DYN_STR_BUF_CHUNK_SIZE 4096
typedef struct dyn_str_buf
{
    int str_len;   // Note: this does not include EOL char
    int buf_size;
    char *str_buf;   // Pointer to string
} dyn_str_buf_t;

/*!*****************************************************************************
 *  \brief     Accumulate string data in a dynamically sized buffer. This is
 *             useful to separate error messages from json and table output.
 *
 *  \param[in] *dyn_str_buf   pointer to structure holding dyn_str_buf info
 *  \param[in] *fmt           printf format specifier
 *  \param[in] ...            additional arguments
 *
 *  \return    0 for success, -1 for error
 ******************************************************************************/
int strcat_dyn_buf(dyn_str_buf_t *dyn_str_buf, const char *fmt, ...)
{
    int avail_buf;
    int formatted_len;
    int add_buf_size = 0;
    char *tmp_char_ptr = NULL;

    if (!dyn_str_buf)
    {
        fprintf(stderr, "ERROR: invalid param *dyn_str_buf\n");
        return -1;
    }

    if (!fmt)
    {
        return 0;
    }

    va_list vl, tmp_vl;
    va_start(vl, fmt);
    va_copy(tmp_vl, vl);

    // determine length of string to add
    formatted_len = vsnprintf(NULL, 0, fmt, tmp_vl);
    va_end(tmp_vl);

    // check if str_buf needs to be expanded in increments of chunk size
    avail_buf = dyn_str_buf->buf_size - dyn_str_buf->str_len;
    add_buf_size = (formatted_len + 1) > avail_buf ?
        ((formatted_len + 1 - avail_buf + DYN_STR_BUF_CHUNK_SIZE - 1) /
         DYN_STR_BUF_CHUNK_SIZE) *
            DYN_STR_BUF_CHUNK_SIZE :
        0;
    
    // realloc() to expand str_buf if necessary
    if (add_buf_size)
    {
        tmp_char_ptr = (char *)realloc(dyn_str_buf->str_buf,
                                       sizeof(char) * dyn_str_buf->buf_size +
                                           add_buf_size);
        if (!tmp_char_ptr)
        {
            fprintf(stderr, "ERROR: strcat_dyn_buf() failed realloc()\n");
            va_end(vl);
            return -1;
        }
        dyn_str_buf->str_buf = tmp_char_ptr;
        dyn_str_buf->buf_size += add_buf_size;
    }

    // concatenate string to buffer
    vsprintf(dyn_str_buf->str_buf + dyn_str_buf->str_len, fmt, vl);
    dyn_str_buf->str_len += formatted_len;

    va_end(vl);
    return 0;
}

void clear_dyn_str_buf(dyn_str_buf_t *dyn_str_buf)
{
    free(dyn_str_buf->str_buf);
    memset(dyn_str_buf, 0, sizeof(dyn_str_buf_t));
}

void print_full_text(ni_device_queue_t *p_device_queue,
                     ni_session_context_t *p_session_context, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1)
{
  char device_name[MAX_DEVICE_NAME_SIZE] = {0};
  unsigned int index, device_count;
  int32_t *module_ids;
  dyn_str_buf_t output_buf = {0};

  ni_device_context_t *p_device_context;
  ni_device_type_t device_type;
  ni_load_query_t load_query;
  ni_retcode_t return_code;

  for (device_type = NI_DEVICE_TYPE_DECODER;
       device_type != NI_DEVICE_TYPE_XCODER_MAX;
       device_type++)
  {
    device_count = get_modules(device_type,
                               p_device_queue,
                               device_name,
                               &module_ids);
    if (!device_count)
    {
      continue;
    }
UPLOADER:
    strcat_dyn_buf(&output_buf, "Num %ss: %u\n", device_name, device_count);

    switch (device_type)
    {
    case NI_DEVICE_TYPE_DECODER:
        strcat_dyn_buf(&output_buf,
                       "INDEX LOAD(VPU MODEL FW ) INST    MEM(TOTAL CRITICAL SHARE    ) "
                       "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
        break;
    case NI_DEVICE_TYPE_SCALER:
    case NI_DEVICE_TYPE_AI:
        strcat_dyn_buf(&output_buf,
                       "INDEX LOAD(VPU       FW ) INST    MEM(TOTAL          SHARE    ) "
                       "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
        break;
    case NI_DEVICE_TYPE_ENCODER:
        strcat_dyn_buf(&output_buf,
                       "INDEX LOAD(VPU MODEL FW ) INST    MEM(TOTAL CRITICAL SHARE P2P) "
                       "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
        break;
    case NI_DEVICE_TYPE_UPLOAD:
        strcat_dyn_buf(&output_buf,
                       "INDEX LOAD(          FW ) INST    MEM(TOTAL          SHARE P2P) "
                       "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
        break;
    default:
      break;
    }

    for (index = 0; index < device_count; index++)
    {
      p_device_context =
        ni_rsrc_get_device_context(device_type,
                                   module_ids[index]);
      if (!open_and_query(device_type,
                          p_device_context,
                          p_session_context,
                          device_name, detail, detail_data_v1))
      {
        continue;
      }

      switch (device_type)
      {
      case NI_DEVICE_TYPE_DECODER:
          strcat_dyn_buf(&output_buf,
                         "%-5d      %-3u %-3u   %-3u  %-3u/%-3d     %-3u   %-3u      "
                         "%-3u        %-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
                         p_device_context->p_device_info->module_id,
                         p_session_context->load_query.current_load,
                         p_session_context->load_query.fw_model_load,
                         p_session_context->load_query.fw_load,
                         p_session_context->load_query.total_contexts,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_video_shared_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash);
          break;
      case NI_DEVICE_TYPE_SCALER:
      case NI_DEVICE_TYPE_AI:
          strcat_dyn_buf(&output_buf,
                         "%-5d      %-3u       %-3u  %-3u/%-3d     %-3u            "
                         "%-3u        %-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
                         p_device_context->p_device_info->module_id,
                         p_session_context->load_query.current_load,
                         p_session_context->load_query.fw_load,
                         p_session_context->load_query.total_contexts,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash);
          break;
      case NI_DEVICE_TYPE_ENCODER:
          strcat_dyn_buf(&output_buf,
                         "%-5d      %-3u %-3u   %-3u  %-3u/%-3d     %-3u   %-3u      "
                         "%-3u   %-3u  %-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
                         p_device_context->p_device_info->module_id,
                         p_session_context->load_query.current_load,
                         p_session_context->load_query.fw_model_load,
                         p_session_context->load_query.fw_load,
                         p_session_context->load_query.total_contexts,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_video_shared_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_session_context->load_query.fw_p2p_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash);
          break;
      case NI_DEVICE_TYPE_UPLOAD:
          strcat_dyn_buf(&output_buf,
                         "%-5d                %-3u  %-3u/%-3d     %-3u            "
                         "%-3u   %-3u  %-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
                         p_device_context->p_device_info->module_id,
                         p_session_context->load_query.fw_load,
                         p_session_context->load_query.active_hwuploaders,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_session_context->load_query.fw_p2p_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash);
          break;
      default:
        break;
      }

      ni_rsrc_free_device_context(p_device_context);
    }

    if (swap_encoder_and_uploader(&device_type, device_name))
    {
      goto UPLOADER;
    }

    free(module_ids);
  }

  //Nvme[0] and TP[1] and Pcie[2] load
  for (int icore = NP_LOAD; icore <= PCIE_LOAD; icore++)
  {
	  // ASSUMPTION: Each Quadra has at least and only one encoder.
	  device_type = NI_DEVICE_TYPE_ENCODER;
	  device_count = get_modules(device_type,
	                             p_device_queue,
	                             device_name,
	                             &module_ids);

	  if (icore == NP_LOAD)
	  {
		strcat_dyn_buf(&output_buf, "Num Nvmes: ");
		g_temp_load = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		if (!g_temp_load)
		{
		  fprintf(stderr, "ERROR: calloc() failed for g_temp_load\n");
		  return;
		}
		g_temp_pload = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		if (!g_temp_pload)
		{
		  fprintf(stderr, "ERROR: calloc() failed for g_temp_pload\n");
		  return;
		}
		g_temp_pthroughput = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		if (!g_temp_pthroughput)
		{
		  fprintf(stderr, "ERROR: calloc() failed for g_temp_pthroughput\n");
		  return;
		}
		g_temp_sharemem = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		if (!g_temp_sharemem)
		{
		  fprintf(stderr, "ERROR: calloc() failed for g_temp_sharemem\n");
		  return;
		}
	  }
	  else
	  {
		(icore == TP_LOAD)?strcat_dyn_buf(&output_buf, "Num TPs: "):strcat_dyn_buf(&output_buf, "Num PCIes: ");
	  }
	  
	  strcat_dyn_buf(&output_buf, "%u\n", device_count);
	  if (icore == PCIE_LOAD)
	  {
		  strcat_dyn_buf(&output_buf,
					 "INDEX LOAD(          FW )                  PCIE_THROUGHPUT GBps "
					 "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
	  }
	  else
	  {
		  strcat_dyn_buf(&output_buf,
					 "INDEX LOAD(          FW )         MEM(               SHARE    ) "
					 "DEVICE       L_FL2V   N_FL2V   FR       N_FR\n");
	  }
	  if (!device_count)
	  {
		if (output_buf.str_buf)
			printf("%s", output_buf.str_buf);
		clear_dyn_str_buf(&output_buf);
		free(g_temp_load);
		free(g_temp_pload);
		free(g_temp_pthroughput);
		free(g_temp_sharemem);
		return;
	  }
	  
	  for (index = 0; index < device_count; index++)
	  {
		p_device_context =
			ni_rsrc_get_device_context(device_type,
									 module_ids[index]);
		if (icore == NP_LOAD)
		{
			if (!open_and_query(device_type,
							p_device_context,
							p_session_context,
							device_name, detail, detail_data_v1))
			{
				continue;
			}
	  
			return_code = ni_query_nvme_status(p_session_context, &load_query);
			if (return_code != NI_RETCODE_SUCCESS)
			{
				fprintf(stderr,
						"ERROR: ni_query_nvme_status() returned %d for %s:%s:%d\n",
						return_code,
						device_name,
						p_device_context->p_device_info->dev_name,
						p_device_context->p_device_info->hw_id);
				remove_device_from_saved(device_type,
								   p_device_context->p_device_info->module_id,
								   p_session_context->device_handle);
				ni_device_close(p_session_context->device_handle);
				goto CLOSE;
			}
			g_temp_load[index] = load_query.tp_fw_load;
			g_temp_pload[index] = load_query.pcie_load;
			g_temp_pthroughput[index] = load_query.pcie_throughput;
                        g_temp_sharemem[index] = load_query.fw_share_mem_usage;
		}
		if (icore==PCIE_LOAD)
		{
			load_query.fw_load = g_temp_pload[index];
			load_query.fw_share_mem_usage = g_temp_pthroughput[index];

			strcat_dyn_buf(&output_buf,
					   "%-5d                %-3u                             %-3.1f        "
					   "%-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
					   p_device_context->p_device_info->module_id,
					   load_query.fw_load,
					   (float)load_query.fw_share_mem_usage/10,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash);
		}
		else
		{
			load_query.fw_load = (icore==TP_LOAD)?g_temp_load[index]:load_query.fw_load;
			load_query.fw_share_mem_usage = (icore==TP_LOAD)?g_temp_sharemem[index]:load_query.fw_share_mem_usage;

			strcat_dyn_buf(&output_buf,
					   "%-5d                %-3u                             %-3u        "
					   "%-11s %-8.8s %-8.8s %-8.8s %-8.8s\n",
					   p_device_context->p_device_info->module_id,
					   load_query.fw_load,
					   load_query.fw_share_mem_usage,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash);
		}

		CLOSE:
			ni_rsrc_free_device_context(p_device_context);
	  }
  }
  if (output_buf.str_buf)
  	printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
  free(g_temp_load);
  free(g_temp_pload);
  free(g_temp_pthroughput);
  free(g_temp_sharemem);
  free(module_ids);
}

void print_simple_text(ni_device_queue_t *p_device_queue,
                       ni_session_context_t *p_session_context, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1)
{
  bool copied_block_name;
  char block_name[NI_MAX_DEVICE_NAME_LEN] = {0};
  char device_name[MAX_DEVICE_NAME_SIZE] = {0};
  int guid;
  unsigned int number_of_quadras;
  unsigned int number_of_device_types_present;
  unsigned int device_type_counter;
  unsigned int *maximum_firmware_loads;
  unsigned int *maximum_firmware_loads_per_quadra;
  dyn_str_buf_t output_buf = {0};

  ni_device_context_t *p_device_context;
  ni_device_type_t device_type;
  ni_device_type_t maximum_device_type;
  ni_load_query_t load_query;
  ni_retcode_t return_code;

  // Use NI_DEVICE_TYPE_ENCODER instead of NI_DEVICE_TYPE_NVME
  // so that Quadras running older firmware will show up.
  // Assumption: Each Quadra has at least one encoder.
  number_of_quadras = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];

  maximum_firmware_loads = calloc(number_of_quadras, sizeof(unsigned int));
  if (!maximum_firmware_loads)
  {
    fprintf(stderr, "calloc() returned NULL\n");
    return;
  }

  maximum_firmware_loads_per_quadra = NULL;
  p_device_context = NULL;

  for (guid = 0; guid < number_of_quadras; guid++)
  {
    maximum_device_type = NI_DEVICE_TYPE_XCODER_MAX;
    maximum_firmware_loads_per_quadra = maximum_firmware_loads + guid;
    number_of_device_types_present = 0;
    device_type_counter = 0;
    copied_block_name = false;

    for (device_type = NI_DEVICE_TYPE_DECODER;
         device_type < maximum_device_type;
         device_type++)
    {
      if (p_device_queue->xcoders[device_type][guid] != -1)
      {
        number_of_device_types_present++;
      }
    }

    for (device_type = NI_DEVICE_TYPE_DECODER;
         device_type < maximum_device_type;
         device_type++)
    {
      memcpy(device_name,
              g_device_type_str[device_type],
              MAX_DEVICE_NAME_SIZE);

      if (p_device_queue->xcoders[device_type][guid] == -1)
      {
          continue;
      }

      p_device_context =
        ni_rsrc_get_device_context(device_type,
                                   p_device_queue->xcoders[device_type][guid]);
      if (!open_and_query(device_type,
                          p_device_context,
                          p_session_context,
                          device_name, detail, detail_data_v1))
      {
        continue;
      }

      if (ni_cmp_fw_api_ver((char*) &p_session_context->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "6O") < 0)
      {
        if (maximum_device_type == NI_DEVICE_TYPE_XCODER_MAX)
        {
            strcat_dyn_buf(&output_buf,
                           "%s: Simple output not supported. Try '-o full' "
                           "instead.\n",
                           p_device_context->p_device_info->dev_name);
            maximum_device_type = NI_DEVICE_TYPE_ENCODER;
        }
        ni_device_close(p_session_context->device_handle);
        ni_rsrc_free_device_context(p_device_context);
        continue;
      }

      if (!copied_block_name)
      {
        strncpy(block_name,
                p_device_context->p_device_info->dev_name,
                NI_MAX_DEVICE_NAME_LEN);
        copied_block_name = true;
      }

      if (*maximum_firmware_loads_per_quadra < p_session_context->load_query.fw_load)
      {
        *maximum_firmware_loads_per_quadra = p_session_context->load_query.fw_load;
      }

      device_type_counter++;
      if (device_type_counter < number_of_device_types_present)
      {
          remove_device_from_saved(device_type,
                                   p_device_context->p_device_info->module_id,
                                   p_session_context->device_handle);
          ni_device_close(p_session_context->device_handle);
          ni_rsrc_free_device_context(p_device_context);
      }
    }

    if (maximum_device_type == NI_DEVICE_TYPE_ENCODER)
    {
      continue;
    }
	
    //Nvme and TP load
    {
		return_code =
			ni_query_nvme_status(p_session_context, &load_query);
		if (return_code == NI_RETCODE_SUCCESS)
		{
			if (*maximum_firmware_loads_per_quadra < load_query.fw_load)
			{
				*maximum_firmware_loads_per_quadra = load_query.fw_load;
			}
			if (*maximum_firmware_loads_per_quadra < load_query.tp_fw_load)
			{
				*maximum_firmware_loads_per_quadra = load_query.tp_fw_load;
			}
			strcat_dyn_buf(&output_buf, "%s: %u%%\n", block_name,
					 *maximum_firmware_loads_per_quadra);
		}
		else
		{
			fprintf(
				stderr, "ERROR: ni_query_nvme_status() returned %d for %s:%s:%d\n",
				return_code, device_name, p_device_context->p_device_info->dev_name,
				p_device_context->p_device_info->hw_id);
			remove_device_from_saved(device_type,
									 p_device_context->p_device_info->module_id,
									 p_session_context->device_handle);
			ni_device_close(p_session_context->device_handle);
		}
    }

    ni_rsrc_free_device_context(p_device_context);
  }

  if (output_buf.str_buf)
    printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
  free(maximum_firmware_loads);
}

void print_json(ni_device_queue_t *p_device_queue,
                ni_session_context_t *p_session_context, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1)
{
  char device_name[MAX_DEVICE_NAME_SIZE] = {0};
  unsigned int index, device_count;
  int instance_count;
  int32_t *module_ids;
  uint32_t total_contexts;
  uint32_t vpu_load;
  uint32_t model_load;
#ifdef __linux__
  char pcie[64] = {0};
  int numa_node;
#endif
  dyn_str_buf_t output_buf = {0};

  ni_device_context_t *p_device_context;
  ni_device_type_t device_type;
  ni_load_query_t load_query;
  ni_retcode_t return_code;
  int max_device_type = NI_DEVICE_TYPE_XCODER_MAX;
  if(detail)
      max_device_type = NI_DEVICE_TYPE_SCALER;

  for (device_type = NI_DEVICE_TYPE_DECODER;
       device_type != max_device_type;
       device_type++)
  {
    instance_count = 0;
    device_count = get_modules(device_type,
                               p_device_queue,
                               device_name,
                               &module_ids);
    if (!device_count)
    {
      continue;
    }

UPLOADER:
    for (index = 0; index < device_count; index++)
    {
      p_device_context =
        ni_rsrc_get_device_context(device_type,
                                   module_ids[index]);
      if (!open_and_query(device_type,
                          p_device_context,
                          p_session_context,
                          device_name, detail, detail_data_v1))
      {
        continue;
      }

      if (device_type == NI_DEVICE_TYPE_UPLOAD)
      {
          total_contexts = p_session_context->load_query.active_hwuploaders;
          vpu_load = 0;
          model_load = 0;
      }
      else
      {
          total_contexts = p_session_context->load_query.total_contexts;
          vpu_load = p_session_context->load_query.current_load;
          model_load = p_session_context->load_query.fw_model_load;
      }
#ifdef __linux__
      get_pcie_addr(p_device_context->p_device_info->dev_name, pcie);
      numa_node = get_numa_node(p_device_context->p_device_info->dev_name);
#endif

      if(detail)
      {
        strcat_dyn_buf(&output_buf,
                         "{ \"%s\" :\n"
                         "\t[\n",
                         device_name
                         );
          for(index = 0; index < NI_MAX_CONTEXTS_PER_HW_INSTANCE; index++)
          {
            if(detail_data_v1->sInstDetailStatus[index].ui16FrameRate)
            {
              if(device_type == NI_DEVICE_TYPE_DECODER)
              {
                strcat_dyn_buf(&output_buf,
                       "\t\t{\n"
                       "\t\t\t\"NUMBER\": %u,\n"
                       "\t\t\t\"INDEX\": %u,\n"
                       "\t\t\t\"AvgCost\": %u,\n"
                       "\t\t\t\"FrameRate\": %u,\n"
                       "\t\t\t\"IDR\": %u,\n"
                       "\t\t\t\"InFrame\": %u,\n"
                       "\t\t\t\"OutFrame\": %u,\n"
                       "\t\t\t\"Width\": %u,\n"
                       "\t\t\t\"Height\": %u,\n"
                       "\t\t\t\"DEVICE\": \"%s\",\n"
                       "\t\t},\n",
                       device_count,
                       instance_count++,
                       detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                       detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                       detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                       detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                       detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                       detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                       detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                       p_device_context->p_device_info->dev_name);
              }
              else if (device_type == NI_DEVICE_TYPE_ENCODER)
              {
                strcat_dyn_buf(&output_buf,
                       "\t\t{\n"
                       "\t\t\t\"NUMBER\": %u,\n"
                       "\t\t\t\"INDEX\": %u,\n"
                       "\t\t\t\"AvgCost\": %u,\n"
                       "\t\t\t\"FrameRate\": %u,\n"
                       "\t\t\t\"IDR\": %u,\n"
                       "\t\t\t\"UserIDR\": %u,\n"
                       "\t\t\t\"InFrame\": %u,\n"
                       "\t\t\t\"OutFrame\": %u,\n"
                       "\t\t\t\"BR\": %u,\n"
                       "\t\t\t\"AvgBR\": %u,\n"
                       "\t\t\t\"Width\": %u,\n"
                       "\t\t\t\"Height\": %u,\n"
                       "\t\t\t\"DEVICE\": \"%s\",\n"
                       "\t\t},\n",
                       device_count,
                       instance_count++,
                       detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                       detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                       detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                       detail_data_v1->sInstDetailStatusAppend[index].ui32UserIDR,
                       detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                       detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                       detail_data_v1->sInstDetailStatus[index].ui32BitRate,
                       detail_data_v1->sInstDetailStatus[index].ui32AvgBitRate,
                       detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                       detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                       p_device_context->p_device_info->dev_name);
              }
            }
          }
          strcat_dyn_buf(&output_buf,
                         "\t]\n"
                         "}\n"
                         );
      }
      else
      {
        if (p_session_context->overall_load_query.admin_queried &&
            device_type != NI_DEVICE_TYPE_UPLOAD)
        {
          strcat_dyn_buf(&output_buf,
                         "{ \"%s\" :\n"
                         "\t[\n"
                         "\t\t{\n"
                         "\t\t\t\"NUMBER\": %u,\n"
                         "\t\t\t\"INDEX\": %d,\n"
                         "\t\t\t\"LOAD\": %u,\n"
                         "\t\t\t\"LOAD-ALL\": %u,\n"
                         "\t\t\t\"MODEL_LOAD\": %u,\n"
                         "\t\t\t\"MODEL_LOAD-ALL\": %u,\n"
                         "\t\t\t\"FW_LOAD\": %u,\n"
                         "\t\t\t\"INST\": %u,\n"
                         "\t\t\t\"INST-ALL\": %u,\n"
                         "\t\t\t\"MAX_INST\": %d,\n"
                         "\t\t\t\"MEM\": %u,\n"
                         "\t\t\t\"CRITICAL_MEM\": %u,\n"
                         "\t\t\t\"SHARE_MEM\": %u,\n"
                         "\t\t\t\"P2P_MEM\": %u,\n"
                         "\t\t\t\"DEVICE\": \"%s\",\n"
                         "\t\t\t\"L_FL2V\": \"%s\",\n"
                         "\t\t\t\"N_FL2V\": \"%s\",\n"
                         "\t\t\t\"FR\": \"%.8s\",\n"
                         "\t\t\t\"N_FR\": \"%.8s\""
  #ifdef __linux__
                         ",\n\t\t\t\"NUMA_NODE\": %d,\n"
                         "\t\t\t\"PCIE_ADDR\": \"%s\"\n"
  #else
                         ",\n"
  #endif
                         "\t\t}\n"
                         "\t]\n"
                         "}\n",
                         device_name, device_count,
                         p_device_context->p_device_info->module_id,
                         vpu_load,
                         p_session_context->overall_load_query.overall_current_load,
                         model_load,
                         p_session_context->overall_load_query.overall_fw_model_load,
                         p_session_context->load_query.fw_load, total_contexts,
                         p_session_context->overall_load_query.overall_instance_count,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_video_shared_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_session_context->load_query.fw_p2p_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash
#ifdef __linux__
                         ,
                         numa_node, pcie
#endif
          );
        }
        else
        {
          strcat_dyn_buf(&output_buf,
                         "{ \"%s\" :\n"
                         "\t[\n"
                         "\t\t{\n"
                         "\t\t\t\"NUMBER\": %u,\n"
                         "\t\t\t\"INDEX\": %d,\n"
                         "\t\t\t\"LOAD\": %u,\n"
                         "\t\t\t\"MODEL_LOAD\": %u,\n"
                         "\t\t\t\"FW_LOAD\": %u,\n"
                         "\t\t\t\"INST\": %u,\n"
                         "\t\t\t\"MAX_INST\": %d,\n"
                         "\t\t\t\"MEM\": %u,\n"
                         "\t\t\t\"CRITICAL_MEM\": %u,\n"
                         "\t\t\t\"SHARE_MEM\": %u,\n"
                         "\t\t\t\"P2P_MEM\": %u,\n"
                         "\t\t\t\"DEVICE\": \"%s\",\n"
                         "\t\t\t\"L_FL2V\": \"%s\",\n"
                         "\t\t\t\"N_FL2V\": \"%s\",\n"
                         "\t\t\t\"FR\": \"%.8s\",\n"
                         "\t\t\t\"N_FR\": \"%.8s\""
  #ifdef __linux__
                         ",\n\t\t\t\"NUMA_NODE\": %d,\n"
                         "\t\t\t\"PCIE_ADDR\": \"%s\"\n"
  #else
                         ",\n"
  #endif
                         "\t\t}\n"
                         "\t]\n"
                         "}\n",
                         device_name, device_count,
                         p_device_context->p_device_info->module_id,
                         vpu_load,
                         model_load,
                         p_session_context->load_query.fw_load, total_contexts,
                         p_device_context->p_device_info->max_instance_cnt,
                         p_session_context->load_query.fw_video_mem_usage,
                         p_session_context->load_query.fw_video_shared_mem_usage,
                         p_session_context->load_query.fw_share_mem_usage,
                         p_session_context->load_query.fw_p2p_mem_usage,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->fl_ver_last_ran,
                         p_device_context->p_device_info->fl_ver_nor_flash,
                         p_device_context->p_device_info->fw_rev,
                         p_device_context->p_device_info->fw_rev_nor_flash
  #ifdef __linux__
                         ,
                         numa_node, pcie
  #endif
          );
        }
      }
      ni_rsrc_free_device_context(p_device_context);
    }

    if(!detail)
    {
        if (swap_encoder_and_uploader(&device_type, device_name))
        {
          goto UPLOADER;
        }
    }

    free(module_ids);
  }
  if(detail)
  {
    if (output_buf.str_buf)
      printf("%s", output_buf.str_buf);
    clear_dyn_str_buf(&output_buf);
    return;
  }

  //Nvme[0] and TP[1] load and PCIe[3] load
  for (int icore = NP_LOAD; icore <= PCIE_LOAD; icore++)
  {
	  // ASSUMPTION: Each Quadra has at least and only one encoder.
	  device_type = NI_DEVICE_TYPE_ENCODER;
	  device_count = get_modules(device_type,
								 p_device_queue,
								 device_name,
								 &module_ids);

	  if (!device_count)
	  {
		  if (output_buf.str_buf)
			  printf("%s", output_buf.str_buf);
		  clear_dyn_str_buf(&output_buf);
		  return;
	  }
	  if (icore == NP_LOAD)
	  {
		  strcpy(device_name, "nvme");
		  g_temp_load = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_load)
		  {
		    fprintf(stderr, "ERROR: calloc() failed for g_temp_load\n");
		    return;
		  }
		  g_temp_pload = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_pload)
		  {
		      fprintf(stderr, "ERROR: calloc() failed for g_temp_pload\n");
			  return;
		  }
		  g_temp_pthroughput = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_pthroughput)
		  {
		      fprintf(stderr, "ERROR: calloc() failed for g_temp_pthroughput\n");
			  return;
		  }
		  g_temp_sharemem = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_sharemem)
		  {
		    fprintf(stderr, "ERROR: calloc() failed for g_temp_sharemem\n");
		    return;
		  }
	  }
	  else
	  {
		  (icore == TP_LOAD)?strcpy(device_name, "tp"):strcpy(device_name, "pcie");
	  }
	  for (index = 0; index < device_count; index++)
	  {
		p_device_context =
				ni_rsrc_get_device_context(device_type,
								 module_ids[index]);
		if (icore == NP_LOAD)
		{
			if (!open_and_query(device_type,
							p_device_context,
							p_session_context,
							device_name, detail, detail_data_v1))
			{
				continue;
			}

			return_code =
			  ni_query_nvme_status(p_session_context, &load_query);
			if (return_code != NI_RETCODE_SUCCESS)
			{
			  fprintf(stderr,
					  "ERROR: ni_query_nvme_status() returned %d for %s:%s:%d\n",
					  return_code,
					  device_name,
					  p_device_context->p_device_info->dev_name,
					  p_device_context->p_device_info->hw_id);
			  remove_device_from_saved(device_type,
									   p_device_context->p_device_info->module_id,
									   p_session_context->device_handle);
			  ni_device_close(p_session_context->device_handle);
			  goto CLOSE;
			}
			g_temp_load[index] = load_query.tp_fw_load;
			g_temp_pload[index] = load_query.pcie_load;
			g_temp_pthroughput[index] = load_query.pcie_throughput;
			g_temp_sharemem[index] = load_query.fw_share_mem_usage;
		}

	#ifdef __linux__
		get_pcie_addr(p_device_context->p_device_info->dev_name, pcie);
		numa_node = get_numa_node(p_device_context->p_device_info->dev_name);
	#endif

		if (icore == PCIE_LOAD)
		{
			strcat_dyn_buf(&output_buf,
					   "{ \"%s\" :\n"
					   "\t[\n"
					   "\t\t{\n"
					   "\t\t\t\"NUMBER\": %u,\n"
					   "\t\t\t\"INDEX\": %d,\n"
					   "\t\t\t\"LOAD\": 0,\n"
					   "\t\t\t\"MODEL_LOAD\": 0,\n"
					   "\t\t\t\"FW_LOAD\": %u,\n"
					   "\t\t\t\"INST\": 0,\n"
					   "\t\t\t\"MAX_INST\": 0,\n"
					   "\t\t\t\"MEM\": 0,\n"
					   "\t\t\t\"CRITICAL_MEM\": 0,\n"
                                           "\t\t\t\"SHARE_MEM\": %u,\n"
					   "\t\t\t\"PCIE_THROUGHPUT\": %.1f,\n"
					   "\t\t\t\"P2P_MEM\": 0,\n"
					   "\t\t\t\"DEVICE\": \"%s\",\n"
					   "\t\t\t\"L_FL2V\": \"%s\",\n"
					   "\t\t\t\"N_FL2V\": \"%s\",\n"
					   "\t\t\t\"FR\": \"%.8s\",\n"
					   "\t\t\t\"N_FR\": \"%.8s\""
	#ifdef __linux__
					   ",\n\t\t\t\"NUMA_NODE\": %d,\n"
					   "\t\t\t\"PCIE_ADDR\": \"%s\"\n"
	#else
					   ",\n"
	#endif
					   "\t\t}\n"
					   "\t]\n"
					   "}\n",
					   device_name, device_count,
					   p_device_context->p_device_info->module_id,
					   g_temp_pload[index], 0, (float)g_temp_pthroughput[index]/10,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash
	#ifdef __linux__
					   ,
					   numa_node, pcie
	#endif
			);
		}
		else
		{
			load_query.fw_load = (icore==TP_LOAD)?g_temp_load[index]:load_query.fw_load;
			load_query.fw_share_mem_usage = (icore==TP_LOAD)?g_temp_sharemem[index]:load_query.fw_share_mem_usage;

			strcat_dyn_buf(&output_buf,
					   "{ \"%s\" :\n"
					   "\t[\n"
					   "\t\t{\n"
					   "\t\t\t\"NUMBER\": %u,\n"
					   "\t\t\t\"INDEX\": %d,\n"
					   "\t\t\t\"LOAD\": 0,\n"
					   "\t\t\t\"MODEL_LOAD\": 0,\n"
					   "\t\t\t\"FW_LOAD\": %u,\n"
					   "\t\t\t\"INST\": 0,\n"
					   "\t\t\t\"MAX_INST\": 0,\n"
					   "\t\t\t\"MEM\": 0,\n"
					   "\t\t\t\"CRITICAL_MEM\": 0,\n"
					   "\t\t\t\"SHARE_MEM\": %u,\n"
					   "\t\t\t\"P2P_MEM\": 0,\n"
					   "\t\t\t\"DEVICE\": \"%s\",\n"
					   "\t\t\t\"L_FL2V\": \"%s\",\n"
					   "\t\t\t\"N_FL2V\": \"%s\",\n"
					   "\t\t\t\"FR\": \"%.8s\",\n"
					   "\t\t\t\"N_FR\": \"%.8s\""
	#ifdef __linux__
					   ",\n\t\t\t\"NUMA_NODE\": %d,\n"
					   "\t\t\t\"PCIE_ADDR\": \"%s\"\n"
	#else
					   ",\n"
	#endif
					   "\t\t}\n"
					   "\t]\n"
					   "}\n",
					   device_name, device_count,
					   p_device_context->p_device_info->module_id,
					   load_query.fw_load, load_query.fw_share_mem_usage,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash
	#ifdef __linux__
					   ,
					   numa_node, pcie
	#endif
			);
		}
	CLOSE:
		ni_rsrc_free_device_context(p_device_context);
	  }
  }

  if (output_buf.str_buf)
      printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
  free(g_temp_load);
  free(g_temp_pload);
  free(g_temp_pthroughput);
  free(g_temp_sharemem);
  free(module_ids);
}

void print_json1(ni_device_queue_t *p_device_queue,
                 ni_session_context_t *p_session_context, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1, int format)
{
  bool has_written_start = false;
  char device_name[MAX_DEVICE_NAME_SIZE] = {0};
  unsigned int index, device_count;
  int32_t *module_ids;
  uint32_t total_contexts;
  uint32_t vpu_load;
  uint32_t model_load;
#ifdef __linux__
  char pcie[64] = {0};
  int numa_node;
#endif
  dyn_str_buf_t output_buf = {0};
  ni_device_extra_info_t p_dev_extra_info = {0};
  char power_consumption[16];

  ni_device_context_t *p_device_context;
  ni_device_type_t device_type;
  ni_load_query_t load_query;
  ni_retcode_t return_code;

  for (device_type = NI_DEVICE_TYPE_DECODER;
       device_type != NI_DEVICE_TYPE_XCODER_MAX;
       device_type++)
  {
    device_count = get_modules(device_type,
                               p_device_queue,
                               device_name,
                               &module_ids);
    if (!device_count)
    {
      continue;
    }

UPLOADER:
    if (!has_written_start)
    {
        strcat_dyn_buf(&output_buf, "{\n  \"%ss\": [\n", device_name);
      has_written_start = true;
    }
    else
    {
        strcat_dyn_buf(&output_buf, "  \"%ss\": [\n", device_name);
    }

    for (index = 0; index < device_count; index++)
    {
      p_device_context =
        ni_rsrc_get_device_context(device_type,
                                   module_ids[index]);
      if (!open_and_query(device_type,
                          p_device_context,
                          p_session_context,
                          device_name, detail, detail_data_v1))
      {
         continue;
      }

      if (device_type == NI_DEVICE_TYPE_UPLOAD)
      {
          total_contexts = p_session_context->load_query.active_hwuploaders;
          vpu_load = 0;
          model_load = 0;
      }
      else
      {
          total_contexts = p_session_context->load_query.total_contexts;
          vpu_load = p_session_context->load_query.current_load;
          model_load = p_session_context->load_query.fw_model_load;
      }
#ifdef __linux__
      get_pcie_addr(p_device_context->p_device_info->dev_name, pcie);
      numa_node = get_numa_node(p_device_context->p_device_info->dev_name);
#endif
      if (format == 2)
      {
          if (ni_query_extra_info(p_session_context->device_handle,
                                  &p_dev_extra_info,
                                  p_device_context->p_device_info->fw_rev))
          {
              p_dev_extra_info.composite_temp = 0;
          }
          if (p_dev_extra_info.power_consumption + 1)
          {
              sprintf(power_consumption, "%umW", p_dev_extra_info.power_consumption);
          }
      }
      if (p_session_context->overall_load_query.admin_queried && 
          device_type != NI_DEVICE_TYPE_UPLOAD)
      {
        strcat_dyn_buf(&output_buf,
                       "\t{\n"
                       "\t\t\"NUMBER\": %u,\n"
                       "\t\t\"INDEX\": %d,\n"
                       "\t\t\"LOAD\": %u,\n"
                       "\t\t\"LOAD-ALL\": %u,\n"
                       "\t\t\"MODEL_LOAD\": %u,\n"
                       "\t\t\"MODEL_LOAD-ALL\": %u,\n"
                       "\t\t\"FW_LOAD\": %u,\n"
                       "\t\t\"INST\": %u,\n"
                       "\t\t\"INST-ALL\": %u,\n"
                       "\t\t\"MAX_INST\": %d,\n"
                       "\t\t\"MEM\": %u,\n"
                       "\t\t\"CRITICAL_MEM\": %u,\n"
                       "\t\t\"SHARE_MEM\": %u,\n"
                       "\t\t\"P2P_MEM\": %u,\n"
                       "\t\t\"DEVICE\": \"%s\",\n"
                       "\t\t\"L_FL2V\": \"%s\",\n"
                       "\t\t\"N_FL2V\": \"%s\",\n"
                       "\t\t\"FR\": \"%.8s\",\n"
                       "\t\t\"N_FR\": \"%.8s\""
#ifdef __linux__
                       ",\n\t\t\"NUMA_NODE\": %d,\n"
                       "\t\t\"PCIE_ADDR\": \"%s\""
#endif
                       ,device_count, p_device_context->p_device_info->module_id,
                       vpu_load,
                       p_session_context->overall_load_query.overall_current_load,
                       model_load,
                       p_session_context->overall_load_query.overall_fw_model_load,
                       p_session_context->load_query.fw_load, total_contexts,
                       p_session_context->overall_load_query.overall_instance_count,
                       p_device_context->p_device_info->max_instance_cnt,
                       p_session_context->load_query.fw_video_mem_usage,
                       p_session_context->load_query.fw_video_shared_mem_usage,
                       p_session_context->load_query.fw_share_mem_usage,
                       p_session_context->load_query.fw_p2p_mem_usage,
                       p_device_context->p_device_info->dev_name,
                       p_device_context->p_device_info->fl_ver_last_ran,
                       p_device_context->p_device_info->fl_ver_nor_flash,
                       p_device_context->p_device_info->fw_rev,
                       p_device_context->p_device_info->fw_rev_nor_flash
#ifdef __linux__
                       ,
                       numa_node, pcie
#endif
        );
      } else
      {
        strcat_dyn_buf(&output_buf,
                       "\t{\n"
                       "\t\t\"NUMBER\": %u,\n"
                       "\t\t\"INDEX\": %d,\n"
                       "\t\t\"LOAD\": %u,\n"
                       "\t\t\"MODEL_LOAD\": %u,\n"
                       "\t\t\"FW_LOAD\": %u,\n"
                       "\t\t\"INST\": %u,\n"
                       "\t\t\"MAX_INST\": %d,\n"
                       "\t\t\"MEM\": %u,\n"
                       "\t\t\"CRITICAL_MEM\": %u,\n"
                       "\t\t\"SHARE_MEM\": %u,\n"
                       "\t\t\"P2P_MEM\": %u,\n"
                       "\t\t\"DEVICE\": \"%s\",\n"
                       "\t\t\"L_FL2V\": \"%s\",\n"
                       "\t\t\"N_FL2V\": \"%s\",\n"
                       "\t\t\"FR\": \"%.8s\",\n"
                       "\t\t\"N_FR\": \"%.8s\""
#ifdef __linux__
                       ",\n\t\t\"NUMA_NODE\": %d,\n"
                       "\t\t\"PCIE_ADDR\": \"%s\""
#endif
                       ,device_count, p_device_context->p_device_info->module_id,
                       vpu_load,
                       model_load,
                       p_session_context->load_query.fw_load, total_contexts,
                       p_device_context->p_device_info->max_instance_cnt,
                       p_session_context->load_query.fw_video_mem_usage,
                       p_session_context->load_query.fw_video_shared_mem_usage,
                       p_session_context->load_query.fw_share_mem_usage,
                       p_session_context->load_query.fw_p2p_mem_usage,
                       p_device_context->p_device_info->dev_name,
                       p_device_context->p_device_info->fl_ver_last_ran,
                       p_device_context->p_device_info->fl_ver_nor_flash,
                       p_device_context->p_device_info->fw_rev,
                       p_device_context->p_device_info->fw_rev_nor_flash
#ifdef __linux__
                       ,
                       numa_node, pcie
#endif
        );
      }
      if (format == 2)
      {
          strcat_dyn_buf(&output_buf, 
                         ",\n\t\t\"TEMP\": %d,\n"
                         "\t\t\"POWER\": \"%s\"\n"
                         "\t}",
                         p_dev_extra_info.composite_temp + ABSOLUTE_TEMP_ZERO,
                         (p_dev_extra_info.power_consumption + 1) ? power_consumption : "N/A");
      }
      else
      {
          strcat_dyn_buf(&output_buf, "\n\t}");
      }
      if (index < device_count - 1)
      {
          strcat_dyn_buf(&output_buf, ",\n");
      }
      else
      {
          strcat_dyn_buf(&output_buf, "\n");
      }

      ni_rsrc_free_device_context(p_device_context);
    }

    strcat_dyn_buf(&output_buf, "  ],\n");

    if (swap_encoder_and_uploader(&device_type, device_name))
    {
      goto UPLOADER;
    }

    free(module_ids);
  }

  //Nvme[0] and TP[1] load
  for (int icore = NP_LOAD; icore <= PCIE_LOAD; icore++)
  {
	  // ASSUMPTION: Each Quadra has at least and only one encoder.
	  device_type = NI_DEVICE_TYPE_ENCODER;
	  device_count = get_modules(device_type,
								 p_device_queue,
								 device_name,
								 &module_ids);

	  if (!device_count)
	  {
		  if (output_buf.str_buf)
			  printf("%s", output_buf.str_buf);
		  clear_dyn_str_buf(&output_buf);
		  return;
	  }
	  if (icore == NP_LOAD)
	  {
	  	  strcat_dyn_buf(&output_buf, "  \"nvmes\": [\n");
	  	  g_temp_load = (uint32_t*)calloc(device_count, sizeof(uint32_t));
	  	  if (!g_temp_load)
           	  {
	          	fprintf(stderr, "ERROR: calloc() failed for g_temp_load\n");
		      	return;
	      	  }
		  g_temp_pload = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_pload)
		  {
		      fprintf(stderr, "ERROR: calloc() failed for g_temp_pload\n");
			  return;
		  }
		  g_temp_pthroughput = (uint32_t*)calloc(device_count, sizeof(uint32_t));
		  if (!g_temp_pthroughput)
		  {
		      fprintf(stderr, "ERROR: calloc() failed for g_temp_pthroughput\n");
			  return;
		  }
	          g_temp_sharemem = (uint32_t*)calloc(device_count, sizeof(uint32_t));
	      	  if (!g_temp_sharemem)
          	  {
	              fprintf(stderr, "ERROR: calloc() failed for g_temp_sharemem\n");
		      return;
	      	  }
	  }
	  else
	  {
		  (icore == TP_LOAD)?strcat_dyn_buf(&output_buf, "  \"tps\": [\n"):strcat_dyn_buf(&output_buf, "  \"pcies\": [\n");
	  }
	  for (index = 0; index < device_count; index++)
	  {
		p_device_context =
				ni_rsrc_get_device_context(device_type,
					module_ids[index]);
		if (icore == NP_LOAD)
		{
			if (!open_and_query(device_type,
							p_device_context,
							p_session_context,
							device_name, detail, detail_data_v1))
			{
				continue;
			}

			return_code =
			  ni_query_nvme_status(p_session_context, &load_query);
			if (return_code != NI_RETCODE_SUCCESS)
			{
			  fprintf(stderr,
					  "ERROR: ni_query_nvme_status() returned %d for %s:%s:%d\n",
					  return_code,
					  device_name,
					  p_device_context->p_device_info->dev_name,
					  p_device_context->p_device_info->hw_id);
			  remove_device_from_saved(device_type,
									   p_device_context->p_device_info->module_id,
									   p_session_context->device_handle);
			  ni_device_close(p_session_context->device_handle);
			  goto CLOSE;
			}
			g_temp_load[index] = load_query.tp_fw_load;
			g_temp_pload[index] = load_query.pcie_load;
			g_temp_pthroughput[index] = load_query.pcie_throughput;
			g_temp_sharemem[index] = load_query.fw_share_mem_usage;
		}
#ifdef __linux__
		get_pcie_addr(p_device_context->p_device_info->dev_name, pcie);
		numa_node = get_numa_node(p_device_context->p_device_info->dev_name);
#endif
    		if (format == 2)
    		{
      			if (ni_query_extra_info(p_session_context->device_handle,
                              &p_dev_extra_info,
                              p_device_context->p_device_info->fw_rev))
      			{
          			p_dev_extra_info.composite_temp = 0;
      			}
      			if (p_dev_extra_info.power_consumption + 1)
      			{
          			sprintf(power_consumption, "%umW", p_dev_extra_info.power_consumption);
      			}
    		}

    		if (icore==PCIE_LOAD)
    		{
			strcat_dyn_buf(&output_buf,
					   "\t{\n"
					   "\t\t\"NUMBER\": %u,\n"
					   "\t\t\"INDEX\": %d,\n"
					   "\t\t\"LOAD\": 0,\n"
					   "\t\t\"MODEL_LOAD\": 0,\n"
					   "\t\t\"FW_LOAD\": %u,\n"
					   "\t\t\"INST\": 0,\n"
					   "\t\t\"MAX_INST\": 0,\n"
					   "\t\t\"MEM\": 0,\n"
					   "\t\t\"CRITICAL_MEM\": 0,\n"
                                           "\t\t\"SHARE_MEM\": %u,\n"
					   "\t\t\"PCIE_THROUGHPUT\": %.1f,\n"
					   "\t\t\"P2P_MEM\": 0,\n"
					   "\t\t\"DEVICE\": \"%s\",\n"
					   "\t\t\"L_FL2V\": \"%s\",\n"
					   "\t\t\"N_FL2V\": \"%s\",\n"
					   "\t\t\"FR\": \"%.8s\",\n"
					   "\t\t\"N_FR\": \"%.8s\""
#ifdef __linux__
             				   ",\n\t\t\"NUMA_NODE\": %d,\n"
             				   "\t\t\"PCIE_ADDR\": \"%s\""
#endif
             				   ,device_count, p_device_context->p_device_info->module_id,
					   g_temp_pload[index], 0, (float)g_temp_pthroughput[index]/10,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash
#ifdef __linux__
					   ,
					   numa_node, pcie
#endif
			);
    		}
    		else
    		{
        		load_query.fw_load = (icore==TP_LOAD)?g_temp_load[index]:load_query.fw_load;
			load_query.fw_share_mem_usage = (icore==TP_LOAD)?g_temp_sharemem[index]:load_query.fw_share_mem_usage;

			strcat_dyn_buf(&output_buf,
					   "\t{\n"
					   "\t\t\"NUMBER\": %u,\n"
					   "\t\t\"INDEX\": %d,\n"
					   "\t\t\"LOAD\": 0,\n"
					   "\t\t\"MODEL_LOAD\": 0,\n"
					   "\t\t\"FW_LOAD\": %u,\n"
					   "\t\t\"INST\": 0,\n"
					   "\t\t\"MAX_INST\": 0,\n"
					   "\t\t\"MEM\": 0,\n"
					   "\t\t\"CRITICAL_MEM\": 0,\n"
					   "\t\t\"SHARE_MEM\": %u,\n"
					   "\t\t\"P2P_MEM\": 0,\n"
					   "\t\t\"DEVICE\": \"%s\",\n"
					   "\t\t\"L_FL2V\": \"%s\",\n"
					   "\t\t\"N_FL2V\": \"%s\",\n"
					   "\t\t\"FR\": \"%.8s\",\n"
					   "\t\t\"N_FR\": \"%.8s\""
#ifdef __linux__
             				   ",\n\t\t\"NUMA_NODE\": %d,\n"
             				   "\t\t\"PCIE_ADDR\": \"%s\""
#endif
             				   ,device_count, p_device_context->p_device_info->module_id,
					   load_query.fw_load, load_query.fw_share_mem_usage,
					   p_device_context->p_device_info->dev_name,
					   p_device_context->p_device_info->fl_ver_last_ran,
					   p_device_context->p_device_info->fl_ver_nor_flash,
					   p_device_context->p_device_info->fw_rev,
					   p_device_context->p_device_info->fw_rev_nor_flash
#ifdef __linux__
					   ,
					   numa_node, pcie
#endif
			);
    		}
    		if (format == 2)
    		{
        		strcat_dyn_buf(&output_buf, 
                        ",\n\t\t\"TEMP\": %d,\n"
                        "\t\t\"POWER\": \"%s\"\n"
                        "\t}",
                        p_dev_extra_info.composite_temp + ABSOLUTE_TEMP_ZERO,
                        (p_dev_extra_info.power_consumption + 1) ? power_consumption : "N/A");
    		}
    		else
    		{
        		strcat_dyn_buf(&output_buf, "\n\t}");
    		}
		if (index < device_count - 1)
		{
			strcat_dyn_buf(&output_buf, ",\n");
		}
		else
		{
			strcat_dyn_buf(&output_buf, "\n");
			if (icore != PCIE_LOAD)
			{
				strcat_dyn_buf(&output_buf, "  ],\n");
			}
		}

CLOSE:
		ni_rsrc_free_device_context(p_device_context);
	  }
  }

  strcat_dyn_buf(&output_buf, "  ]\n}\n");

  if (output_buf.str_buf)
      printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
  free(g_temp_load);
  free(g_temp_pload);
  free(g_temp_pthroughput);
  free(g_temp_sharemem);
  free(module_ids);
}

void print_text(ni_device_queue_t *coders,
                ni_session_context_t *sessionCtxt, int detail, ni_instance_mgr_detail_status_v1_t *detail_data_v1, ni_instance_mgr_detail_status_v1_t (*previous_detail_data_p)[NI_DEVICE_TYPE_XCODER_MAX], int checkInterval)
{
  int i, index, instance_count; // used in later FOR-loop when compiled without c99
  unsigned int module_count;
  char module_name[MAX_DEVICE_NAME_SIZE] = {0};
  int32_t *module_id_arr = NULL;
  dyn_str_buf_t output_buf = {0};
  ni_device_context_t *p_device_context = NULL;
  ni_device_type_t module_type;
  int max_device_type = NI_DEVICE_TYPE_XCODER_MAX;

  if(detail)
    max_device_type = NI_DEVICE_TYPE_SCALER;
  for (module_type = NI_DEVICE_TYPE_DECODER;
       module_type != max_device_type;
       module_type++)
  {
    instance_count = 0;
    module_count = get_modules(module_type,
                               coders,
                               module_name,
                               &module_id_arr);

    if (!module_count)
    {
      continue;
    }

    strcat_dyn_buf(&output_buf, "Num %ss: %u\n", module_name, module_count);
    if(detail)
    {
      if(module_type == NI_DEVICE_TYPE_DECODER)
      {
        if(checkInterval)
        {
          strcat_dyn_buf(&output_buf,
                         "%-5s %-7s %-9s %-5s %-7s %-8s %-4s %-5s %-6s %-14s %-20s\n", "INDEX",
                         "AvgCost", "FrameRate", "IDR", "InFrame", "OutFrame", "fps", "Width", "Height",
                         "DEVICE", "NAMESPACE");
        }
        else
        {
          strcat_dyn_buf(&output_buf,
                         "%-5s %-7s %-9s %-5s %-7s %-8s %-5s %-6s %-14s %-20s\n", "INDEX",
                         "AvgCost", "FrameRate", "IDR", "InFrame", "OutFrame", "Width", "Height",
                         "DEVICE", "NAMESPACE");
        }
      }
      else if (module_type == NI_DEVICE_TYPE_ENCODER)
      {
        if(checkInterval)
        {
          strcat_dyn_buf(&output_buf,
                         "%-5s %-7s %-9s %-5s %-7s %-7s %-8s %-4s %-10s %-10s %-5s %-6s %-14s %-20s\n", "INDEX",
                         "AvgCost", "FrameRate", "IDR", "UserIDR", "InFrame", "OutFrame", "fps", "BR", "AvgBR", "Width", "Height",
                         "DEVICE", "NAMESPACE");
        }
        else
        {
          strcat_dyn_buf(&output_buf,
                         "%-5s %-7s %-9s %-5s %-7s %-7s %-8s %-10s %-10s %-5s %-6s %-14s %-20s\n", "INDEX",
                         "AvgCost", "FrameRate", "IDR", "UserIDR", "InFrame", "OutFrame", "BR", "AvgBR", "Width", "Height",
                         "DEVICE", "NAMESPACE");
        }
      }
    }
    else
      strcat_dyn_buf(&output_buf,
                   "%-5s %-4s %-10s %-4s %-4s %-9s %-7s %-14s\n", "INDEX",
                   "LOAD", "MODEL_LOAD", "INST", "MEM", "SHARE_MEM", "P2P_MEM",
                   "DEVICE");

    /*! query each coder and print out their status */
    for (i = 0; i < module_count; i++)
    {
      p_device_context = ni_rsrc_get_device_context(module_type, module_id_arr[i]);

      /*! libxcoder query to get status info including load and instances;*/
      if (open_and_query(module_type,
                         p_device_context,
                         sessionCtxt,
                         module_name, detail, detail_data_v1))
      {
        if(detail)
        {
          for(index = 0; index < NI_MAX_CONTEXTS_PER_HW_INSTANCE; index++)
          {
            if(detail_data_v1->sInstDetailStatus[index].ui16FrameRate)
            {
              if(previous_detail_data_p && checkInterval)
              {
                if(previous_detail_data_p[module_type][i].sInstDetailStatus[index].ui16FrameRate)
                {
                  if(module_type == NI_DEVICE_TYPE_DECODER)
                  {
                    strcat_dyn_buf(&output_buf,
                           "%-5d %-7d %-9d %-5u %-7d %-8d %-4d %-5d %-6d %-14s %-20s\n",
                           instance_count++,
                           detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                           detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                           detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                           detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                           detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                           (detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame - previous_detail_data_p[module_type][i].sInstDetailStatus[index].ui32NumOutFrame) / checkInterval,
                           detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                           detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                           p_device_context->p_device_info->dev_name,
                           p_device_context->p_device_info->blk_name);
                  }
                  else if (module_type == NI_DEVICE_TYPE_ENCODER)
                  {
                    strcat_dyn_buf(&output_buf,
                           "%-5d %-7d %-9d %-5u %-7d %-7d %-8d %-4d %-10d %-10d %-5d %-6d %-14s %-20s\n",
                           instance_count++,
                           detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                           detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                           detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                           detail_data_v1->sInstDetailStatusAppend[index].ui32UserIDR,
                           detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                           detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                           (detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame - previous_detail_data_p[module_type][i].sInstDetailStatus[index].ui32NumOutFrame) / checkInterval,
                           detail_data_v1->sInstDetailStatus[index].ui32BitRate,
                           detail_data_v1->sInstDetailStatus[index].ui32AvgBitRate,
                           detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                           detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                           p_device_context->p_device_info->dev_name,
                           p_device_context->p_device_info->blk_name);
                  }
                }
              }
              else
              {
                if(module_type == NI_DEVICE_TYPE_DECODER)
                {
                  strcat_dyn_buf(&output_buf,
                         "%-5d %-7d %-9d %-5u %-7d %-8d %-5d %-6d %-14s %-20s\n",
                         instance_count++,
                         detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                         detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                         detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                         detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                         detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                         detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                         detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->blk_name);
                }
                else if (module_type == NI_DEVICE_TYPE_ENCODER)
                {
                  strcat_dyn_buf(&output_buf,
                         "%-5d %-7d %-9d %-5u %-7d %-7d %-8d %-10d %-10d %-5d %-6d %-14s %-20s\n",
                         instance_count++,
                         detail_data_v1->sInstDetailStatus[index].ui8AvgCost,
                         detail_data_v1->sInstDetailStatus[index].ui16FrameRate,
                         detail_data_v1->sInstDetailStatus[index].ui32NumIDR,
                         detail_data_v1->sInstDetailStatusAppend[index].ui32UserIDR,
                         detail_data_v1->sInstDetailStatus[index].ui32NumInFrame,
                         detail_data_v1->sInstDetailStatus[index].ui32NumOutFrame,
                         detail_data_v1->sInstDetailStatus[index].ui32BitRate,
                         detail_data_v1->sInstDetailStatus[index].ui32AvgBitRate,
                         detail_data_v1->sInstDetailStatusAppend[index].ui32Width,
                         detail_data_v1->sInstDetailStatusAppend[index].ui32Height,
                         p_device_context->p_device_info->dev_name,
                         p_device_context->p_device_info->blk_name);
                }
              }
            }
          }
          if(previous_detail_data_p)
          {
            memcpy(&previous_detail_data_p[module_type][i], detail_data_v1, sizeof(ni_instance_mgr_detail_status_v1_t));
          }
        }
        else
        {
          strcat_dyn_buf(&output_buf,
                         "%-5d %-4u %-10u %-4u %-4u %-9u %-7u %-14s\n",
                         p_device_context->p_device_info->module_id,
#ifdef XCODER_311
                         sessionCtxt->load_query.current_load,
#else
                         (sessionCtxt->load_query.total_contexts == 0 || sessionCtxt->load_query.current_load > sessionCtxt->load_query.fw_load) ? sessionCtxt->load_query.current_load : sessionCtxt->load_query.fw_load,
#endif
                         sessionCtxt->load_query.fw_model_load,
                         sessionCtxt->load_query.total_contexts,
                         sessionCtxt->load_query.fw_video_mem_usage,
                         sessionCtxt->load_query.fw_share_mem_usage,
                         sessionCtxt->load_query.fw_p2p_mem_usage,
                         p_device_context->p_device_info->dev_name);
          
        }

          ni_rsrc_free_device_context(p_device_context);
      }
    }
    free(module_id_arr);
  }

  if (output_buf.str_buf)
      printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
}

void print_extra(ni_device_queue_t *p_device_queue, ni_session_context_t *p_session_context)
{
  char device_name[MAX_DEVICE_NAME_SIZE];
  unsigned int index, device_count;
  int32_t *module_ids;
  dyn_str_buf_t output_buf = {0};
  ni_device_context_t *p_device_context;
  ni_device_type_t device_type;
  ni_device_extra_info_t p_dev_extra_info = {0};
  char power_consumption[16];

  // ASSUMPTION: Each Quadra has at least and only one decoder.
  device_type = NI_DEVICE_TYPE_DECODER;
  device_count = get_modules(device_type,
                             p_device_queue,
                             device_name,
                             &module_ids);

  if (!device_count)
  {
      if (output_buf.str_buf)
          printf("%s", output_buf.str_buf);
      clear_dyn_str_buf(&output_buf);
      return;
  }

  for (index = 0; index < device_count; index++)
  {
    p_device_context = ni_rsrc_get_device_context(device_type, module_ids[index]);
    if (!p_device_context)
    {
      continue;
    }
    // check if device has been opened already
    ni_device_type_t xcoder_device_type = GET_XCODER_DEVICE_TYPE(device_type);
    int module_id = p_device_context->p_device_info->module_id;
    if (device_handles[xcoder_device_type][module_id] != NI_INVALID_DEVICE_HANDLE)
    {
        p_session_context->device_handle =
            device_handles[xcoder_device_type][module_id];
    } else
    {
        p_session_context->device_handle =
            ni_device_open(p_device_context->p_device_info->dev_name,
                          &p_session_context->max_nvme_io_size);
        if (p_session_context->device_handle != NI_INVALID_DEVICE_HANDLE)
        {
            device_handles[xcoder_device_type][module_id] =
                p_session_context->device_handle;
        }
    }
    
    if (p_session_context->device_handle == NI_INVALID_DEVICE_HANDLE)
    {
      fprintf(stderr,
              "ERROR: ni_device_open() failed for %s: %s\n",
              p_device_context->p_device_info->dev_name,
              strerror(NI_ERRNO));
      ni_rsrc_free_device_context(p_device_context);
      continue;
    }

    if (ni_query_extra_info(p_session_context->device_handle,
                            &p_dev_extra_info,
                            p_device_context->p_device_info->fw_rev))
    {
        p_dev_extra_info.composite_temp = 0;
    }
    
    if (p_dev_extra_info.power_consumption + 1)
    {
        sprintf(power_consumption, "%umW", p_dev_extra_info.power_consumption);
    }
    strcat_dyn_buf(&output_buf,
                "%-4s %-8s %-14s\n", "TEMP", "POWER", "DEVICE");
    strcat_dyn_buf(&output_buf,
                "%-4d %-8s %-14s\n",
                p_dev_extra_info.composite_temp + ABSOLUTE_TEMP_ZERO,
                (p_dev_extra_info.power_consumption + 1) ? power_consumption : "N/A",
                p_device_context->p_device_info->dev_name);

    ni_rsrc_free_device_context(p_device_context);
  }

  if (output_buf.str_buf)
      printf("%s", output_buf.str_buf);
  clear_dyn_str_buf(&output_buf);
  free(module_ids);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    int checkInterval;
    int skip_init_rsrc = 0;
    int should_match_rev = 1;
    int detail = 0;
    ni_instance_mgr_detail_status_v1_t detail_data_v1 = {0};
    ni_instance_mgr_detail_status_v1_t (*previous_detail_data)[NI_DEVICE_TYPE_XCODER_MAX] = NULL;
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_queue_t *coders = NULL;
    ni_session_context_t *p_xCtxt = NULL;
    time_t startTime = {0}, now = {0};
    int timeout_seconds = 0;
    struct tm *ltime = NULL;
    char buf[64] = {0};
    long long time_diff_hours, time_diff_minutes, time_diff_seconds;
    int opt;
    ni_log_level_t log_level = NI_LOG_INFO;
    enum outFormat printFormat = FMT_TEXT;
    checkInterval = 0;
    bool fw_log_dump = false;
    int devid = -1;
    int refresh_device_pool = 1;
    bool is_first_query = true;

#ifdef _WIN32
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#elif __linux__
  setup_signal_handler();
#endif

  // arg handling
  while ((opt = getopt(argc, argv, "n:o:D:k:R:rt:Sl:hvd")) != -1)
  {
    switch (opt)
    {
    case 'o':
      // Output print format
      if (!strcmp(optarg, "json"))
      {
        printFormat = FMT_JSON;
      }
      else if (!strcmp(optarg, "simple"))
      {
        printFormat = FMT_SIMPLE_TEXT;
      }
      else if (!strcmp(optarg, "text"))
      {
        printFormat = FMT_TEXT;
      }
      else if (!strcmp(optarg, "full"))
      {
        printFormat = FMT_FULL_TEXT;
      }
      else if (!strcmp(optarg, "json1"))
      {
        printFormat = FMT_JSON1;
      }
      else if (!strcmp(optarg, "json2"))
      {
        printFormat = FMT_JSON2;
      }
      else if (!strcmp(optarg, "extra"))
      {
        printFormat = FMT_EXTRA;
      }
      else
      {
        fprintf(stderr, "Error: unknown selection for outputFormat: %s\n", optarg);
        return 1;
      }
      break;
    case 'n':
      // Output interval
      checkInterval = atoi(optarg);
      break;
    case 'D':
      fw_log_dump = atoi(optarg);
      break;
    case 'k':
      devid = atoi(optarg);
      break;
    case 'R':
        refresh_device_pool = atoi(optarg);
    case 'r':
      should_match_rev = 0;
      break;
    case 't':
        timeout_seconds = atoi(optarg);
        printf("Timeout will be set %d\n", timeout_seconds);
        break;
    case 'S':
        skip_init_rsrc = 1;
        break;
    case 'l':
        log_level = arg_to_ni_log_level(optarg);
        if (log_level != NI_LOG_INVALID)
        {
            ni_log_set_level(log_level);
        } else {
            fprintf(stderr, "FATAL: invalid log level selected: %s\n", optarg);
            return 1;
        }
        break;
    case 'h':
      // help message (limit to 80 characters wide)
      printf("-------- ni_rsrc_mon v%s --------\n"
             "The ni_rsrc_mon program provides a real-time view of NETINT Quadra resources\n"
             "running on the system.\n"
             "\n"
             "Usage: ni_rsrc_mon [OPTIONS]\n"
             "-n  Specify reporting interval in one second interval. If 0 or no selection,\n"
             "    report only once.\n"
             "    Default: 0\n"
             "-R  Specify if refresh devices on host in each monitor interval.\n"
             "    If 0, only refresh devices at the start.\n"
             "    Default: 1\n"
             "-o  Output format. [text, simple, full, json, json1, json2, extra]\n"
             "    Default: text\n"
             "-D  Dump firmware logs to current directory. Default: 0(not dump fw log).\n"
             "-k  Specify to dump which card's firmware logs.\n"
             "    Default: -1(dump fw log of all cards).\n"
             "-r  Initialize Quadra device regardless firmware release version to\n"
             "    libxcoder version compatibility.\n"
             "    Default: only initialize devices with compatible firmware version.\n"
             "-t  Set timeout time in seconds for device polling. Program will exit with\n"
             "    failure if timeout is reached without finding at least one device. If 0 or\n"
             "    no selection, poll indefinitely until a Quadra device is found.\n"
             "    Default: 0\n"
             "-S  Skip init_rsrc.\n"
             "-d  Print detailed infomation for decoder/encoder in text and json formats.\n"
             "-l  Set loglevel of libxcoder API.\n"
             "    [none, fatal, error, info, debug, trace]\n"
             "    Default: info\n"
             "-h  Open this help message.\n"
             "-v  Print version info.\n"
             "\n"
             "Simple output shows the maximum firmware load amongst the subsystems on the\n"
             "Quadra device.\n"
             "\n"
             "Reporting columns for text output format\n"
             "INDEX         index number used by resource manager to identify the resource\n"
             "LOAD          realtime load given in percentage. This value is max of VPU and FW load reported in full output format\n"
             "MODEL_LOAD    estimated load based on framerate and resolution\n"
             "INST          number of job instances\n"
             "MEM           usage of memory by the subsystem\n"
             "SHARE_MEM     usage of memory shared across subsystems on the same device\n"
             "P2P_MEM       usage of memory by P2P\n"
             "DEVICE        path to NVMe device file handle\n"
             "NAMESPACE     path to NVMe namespace file handle\n"
             "\n"
             "Additional reporting columns for full output format\n"
             "VPU           same as LOAD in JSON outputs\n"
             "FW            system load\n"
             "TOTAL         same as MEM\n"
             "CRITICAL      usage of memory considered critical\n"
             "L_FL2V        last ran firmware loader 2 version\n"
             "N_FL2V        nor flash firmware loader 2 version\n"
             "FR            current firmware revision\n"
             "N_FR          nor flash firmware revision\n"
             "\n"
             "Additional reporting columns for full JSON formats\n"
             "LOAD          VPU load\n"
             "FW_LOAD	      system load\n",
             NI_XCODER_REVISION);
      return 0;
    case 'v':
        printf("Release ver: %s\n"
               "API ver:     %s\n"
               "Date:        %s\n"
               "ID:          %s\n",
               NI_XCODER_REVISION, LIBXCODER_API_VERSION, NI_SW_RELEASE_TIME,
               NI_SW_RELEASE_ID);
        return 0;
    case '?':
        if (isprint(opt))
        {
            fprintf(stderr, "FATAL: unknown option '-%c'\n", opt);
        } else
        {
            fprintf(stderr, "FATAL: unknown option character '\\x%x'\n", opt);
        }
        return 1;
    case 'd':
        detail = 1;
        break;
    case ':':
        fprintf(stderr, "FATAL: option '-%c' lacks arg\n", opt);
        return 1;
    default:
        fprintf(stderr, "FATAL: unhandled option\n");
        return 1;
    }
  }

  if(checkInterval > 0 && printFormat == FMT_JSON)
  {
    fprintf(stderr, "EXIT: -o json cannot use with -n params\n");
    return 1;
  }

  if ((argc <= 2) && (optind == 1))
  {
    for (; optind < argc; optind++)
    {
      checkInterval = argToI(argv[optind]);
    }
  }

#ifdef _ANDROID
  ni_log_set_log_tag(LOG_TAG);
#endif

  p_xCtxt = ni_device_session_context_alloc_init();
  if(!p_xCtxt)
  {
    fprintf(stderr, "FATAL: cannot allocate momory for ni_session_context_t\n");
    return 1;
  }

  ni_log_set_level(NI_LOG_ERROR); // silence informational prints
  if (!skip_init_rsrc && ni_rsrc_init(should_match_rev,timeout_seconds) != 0)
  {
    fprintf(stderr, "FATAL: cannot access NI resource\n");
    LRETURN;
  }
  ni_log_set_level(log_level);

  p_device_pool = ni_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    fprintf(stderr, "FATAL: cannot get devices info\n");
    LRETURN;
  }

  if (log_level >= NI_LOG_INFO)
  {
    printf("**************************************************\n");
  }

  // initialise device handles
  for (int i = 0; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
  {
      for (int j = 0; j < NI_MAX_DEVICE_CNT; j++)
      {
          device_handles[i][j] = NI_INVALID_DEVICE_HANDLE;
      }
  }

  startTime = time(NULL);
#ifdef _ANDROID
  system("chown mediacodec:mediacodec /dev/shm_netint/*");
#endif
  if(checkInterval)
  {
    int allocate_size = NI_MAX_DEVICE_CNT * NI_DEVICE_TYPE_XCODER_MAX * sizeof(ni_instance_mgr_detail_status_v1_t);
    previous_detail_data = (ni_instance_mgr_detail_status_v1_t (*)[NI_DEVICE_TYPE_XCODER_MAX])malloc(allocate_size);
    if(previous_detail_data == NULL)
    {
        fprintf(stderr, "FATAL: Allocate buffer fail\n");
        return 1;
    }
    memset((void *)previous_detail_data, 0, allocate_size);
  }
  while (!g_xcoder_stop_process)
  {
    now = time(NULL);
    ltime = localtime(&now);
    if (ltime)
    {
      strftime(buf, sizeof(buf), "%c", ltime);
    }
    time_diff_seconds = (long long)difftime(now, startTime);
    time_diff_minutes = time_diff_seconds / 60;
    time_diff_hours = time_diff_minutes / 60;


    if (is_first_query || refresh_device_pool)
    {
        ni_rsrc_refresh(should_match_rev);
    }

    if (log_level >= NI_LOG_INFO)
    {
      printf("%s up %02lld" ":%02lld" ":%02lld"  " v%s\n", buf, time_diff_hours, time_diff_minutes % 60, time_diff_seconds % 60,
              NI_XCODER_REVISION);
    }

    /*! print out coders in their current order */

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      fprintf(stderr, "ERROR: Failed to obtain mutex: %p\n", p_device_pool->lock);
      LRETURN;
    }
#elif __linux__
    if ( lockf(p_device_pool->lock, F_LOCK, 0) )
    {
        perror("ERROR: cannot lock p_device_pool");
    }
#endif

    coders = p_device_pool->p_device_queue;

#ifdef _WIN32
    ReleaseMutex((HANDLE)p_device_pool->lock);
#elif __linux__
    if ( lockf(p_device_pool->lock, F_ULOCK, 0) )
    {
        perror("ERROR: cannot unlock p_device_pool");
    }
#endif

    switch (printFormat)
    {
      case FMT_TEXT:
        print_text(coders, p_xCtxt, detail, &detail_data_v1, previous_detail_data, checkInterval);
        break;
      case FMT_FULL_TEXT:
        print_full_text(coders, p_xCtxt, 0, &detail_data_v1);
        break;
      case FMT_SIMPLE_TEXT:
        print_simple_text(coders, p_xCtxt, 0, &detail_data_v1);
        break;
      case FMT_JSON:
        print_json(coders, p_xCtxt, detail, &detail_data_v1);
        break;
      case FMT_JSON1:
        print_json1(coders, p_xCtxt, 0, &detail_data_v1, 1);
        break;
      case FMT_JSON2:
        print_json1(coders, p_xCtxt, 0, &detail_data_v1, 2);
        break;
      case FMT_EXTRA:
        print_extra(coders, p_xCtxt);
        break;
    }

    is_first_query = false;

    if (log_level >= NI_LOG_INFO)
    {
      printf("**************************************************\n");
    }
 
    fflush(stdout);

    if (checkInterval == 0)
    {
      // run once
      break;
    }
    ni_usleep(checkInterval * 1000 * 1000);
  }

  if (fw_log_dump)
  {
    // dump fw log
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      fprintf(stderr, "ERROR: Failed to obtain mutex: %p\n", p_device_pool->lock);
      LRETURN;
    }
#elif __linux__
    if ( lockf(p_device_pool->lock, F_LOCK, 0) )
    {
        perror("ERROR: cannot lock p_device_pool");
    }
#endif
    coders = p_device_pool->p_device_queue;
#ifdef _WIN32
    ReleaseMutex((HANDLE)p_device_pool->lock);
#elif __linux__
    if ( lockf(p_device_pool->lock, F_ULOCK, 0) )
    {
        perror("ERROR: cannot unlock p_device_pool");
    }
#endif
    dump_fw_log(coders, p_xCtxt, devid);
  }

  // close all opened devices
  for (int i = 0; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
  {
      for (int j = 0; j < NI_MAX_DEVICE_CNT; j++)
      {
          if (device_handles[i][j] != NI_INVALID_DEVICE_HANDLE)
          {
              ni_device_close(device_handles[i][j]);
          }
      }
  }
  ni_device_session_context_clear(p_xCtxt);
  ni_rsrc_free_device_pool(p_device_pool);
#ifdef _ANDROID
  system("chown mediacodec:mediacodec /dev/shm_netint/*");
  system("chmod 777 /dev/block/nvme*");
  system("chmod 777 /dev/nvme*");
#endif
  if(checkInterval)
  {
    free(previous_detail_data);
  }

  free(p_xCtxt);
  return 0;

END:
  free(p_xCtxt);
  return 1;
}
