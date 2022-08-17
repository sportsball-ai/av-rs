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
 *   \file          ni_rsrc_mon.c
 *
 *   @date          April 1, 2018
 *
 *   \brief
 *
 *   @author
 *
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <unistd.h>
#include <signal.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"

#ifdef _ANDROID

#include <cutils/properties.h>
#define PROP_DECODER_TYPE "nidec_service_init"

#endif

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
#endif //__linux__


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

/*!******************************************************************************
 *  \brief     print performance data for either decoder or encoder
 *
 *  \param[in] ni_device_type_t     module_type
 *  \param[in] ni_device_queue_t    *coders
 *  \param[in] ni_session_context_t *sessionCtxt
 *
 *  \return   ni_retcode_t rc
 *******************************************************************************/
void print_perf(ni_device_type_t module_type, ni_device_queue_t *coders,
                ni_session_context_t *sessionCtxt)
{
  int i; // used in later FOR-loop when compiled without c99
  int module_count = 0;
  char module_name[8];
  ni_device_context_t *dev_ctxt_arr = NULL;
  int32_t *module_id_arr = NULL;
  ni_device_context_t *p_device_context = NULL;

  if (!IS_XCODER_DEVICE_TYPE(module_type))
  {
      fprintf(stderr, "ERROR: unsupported module_type %d\n", module_type);
      return;
  }

  module_count = coders->xcoder_cnt[module_type];
  strcpy(module_name, device_type_str[module_type]);
  dev_ctxt_arr = malloc(sizeof(ni_device_context_t) * module_count);
  if (!dev_ctxt_arr)
  {
      fprintf(stderr, "ERROR: malloc() failed for dev_ctxt_arr\n");
      return;
  }
  module_id_arr = malloc(sizeof(int32_t) * module_count);
  if (!module_id_arr)
  {
      fprintf(stderr, "ERROR: malloc() failed for module_id_arr\n");
      free(dev_ctxt_arr);
      return;
  }
  memcpy(module_id_arr, coders->xcoders[module_type],
         sizeof(int32_t) * module_count);

  printf("Num %ss: %d\n", device_type_str[module_type], module_count);

  // gotta test how long this sorting takes (SZ)
  // sort module IDs used
  qsort(module_id_arr, module_count, sizeof(int32_t), compareInt32_t);
  
  // Print performance info headings
  if (IS_XCODER_DEVICE_TYPE(module_type))
  {
      printf("%-5s %-4s %-10s %-4s %-4s %-9s %-7s %-14s %-20s\n", "INDEX", "LOAD",
             "MODEL_LOAD", "INST", "MEM", "SHARE_MEM", "P2P_MEM", "DEVICE", "NAMESPACE");
  }
    
  /*! query each coder and print out their status */
  for (i = 0; i < module_count; i++)
  {
    p_device_context = ni_rsrc_get_device_context(module_type, module_id_arr[i]);

    /*! libxcoder query to get status info including load and instances;*/
    if (p_device_context)
    {
        sessionCtxt->device_handle =
            ni_device_open(p_device_context->p_device_info->blk_name,
                           &sessionCtxt->max_nvme_io_size);
        sessionCtxt->blk_io_handle = sessionCtxt->device_handle;

        // Check device can be opened
        if (NI_INVALID_DEVICE_HANDLE == sessionCtxt->device_handle)
        {
            fprintf(stderr, "ERROR: ni_device_open() failed for %s: %s\n",
                    p_device_context->p_device_info->blk_name, strerror(NI_ERRNO));
            ni_rsrc_free_device_context(p_device_context);
            continue;
        }

        // Check xcoder can be queried
        sessionCtxt->hw_id = p_device_context->p_device_info->hw_id;
        if (NI_RETCODE_SUCCESS !=
            ni_device_session_query(sessionCtxt, module_type))
        {
            ni_device_close(sessionCtxt->device_handle);
            fprintf(stderr, "Error query %s %s.%d\n", module_name,
                    p_device_context->p_device_info->dev_name,
                    p_device_context->p_device_info->hw_id);
            ni_rsrc_free_device_context(p_device_context);
            continue;
        }
        // printf("Done query %s %s.%d\n", module_name,
        //        p_device_context->p_device_info->dev_name,
        //        p_device_context->p_device_info->hw_id);
        ni_device_close(sessionCtxt->device_handle);

        if (0 == sessionCtxt->load_query.total_contexts)
        {
            sessionCtxt->load_query.current_load = 0;
        }

        // Print performance info row
        if (IS_XCODER_DEVICE_TYPE(module_type))
        {
            printf("%-5d %-4u %-10u %-4u %-4u %-9u %-7u %-14s %-20s\n",
                   p_device_context->p_device_info->module_id,
                   sessionCtxt->load_query.current_load,
                   sessionCtxt->load_query.fw_model_load,
                   sessionCtxt->load_query.total_contexts,
                   sessionCtxt->load_query.fw_video_mem_usage,
                   sessionCtxt->load_query.fw_share_mem_usage,
                   sessionCtxt->load_query.fw_p2p_mem_usage,
                   p_device_context->p_device_info->dev_name,
                   p_device_context->p_device_info->blk_name);
        }
        ni_rsrc_free_device_context(p_device_context);
    }
  }
  free(dev_ctxt_arr);
  free(module_id_arr);
  return;
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int main(int argc, char *argv[])
{
    int k;
    int checkInterval;
    int should_match_rev = 1;
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_queue_t *coders = NULL;
    ni_device_context_t *p_device_context = NULL;
    ni_session_context_t xCtxt = {0};
    time_t startTime = {0}, now = {0};
    int timeout_seconds = 0;
    struct tm *ltime = NULL;
    char buf[64] = {0};
    time_t hours, minutes, seconds;
    int opt;
    ni_log_level_t log_level = NI_LOG_INFO;

    checkInterval = 0;

#ifdef _WIN32
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#elif __linux__
  setup_signal_handler();
#endif

  // arg handling
  while ((opt = getopt(argc, argv, "n:rt:l:hv")) != -1)
  {
    switch (opt)
    {
    case 'n':
      // Output interval
      checkInterval = atoi(optarg);
      break;
    case 'r':
      should_match_rev = 0;
      break;
    case 't':
        timeout_seconds = atoi(optarg);
        printf("Timeout will be set %d\n", timeout_seconds);
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
      // help message
      printf("-------- ni_rsrc_mon v%s --------\n"
             "The ni_rsrc_mon program provides a real-time view of NETINT "
             "QUADRA resources \n"
             "running on the system.\n"
             "\n"
             "Usage: ni_rsrc_mon [OPTIONS]\n"
             "-n  Specify reporting interval in one second interval. If 0 or "
             "no selection,\n"
             "    report only once.\n"
             "    Default: 0\n"
             "-r  Init transcoder card resource regardless firmware release "
             "version to\n"
             "    libxcoder version compatibility.\n"
             "    Default: only init cards with compatible firmware version.\n"
             "-t  Set timeout time in seconds for device polling. Program will "
             "exit with \n"
             "    failure if timeout is reached without finding at least one "
             "device. If 0 or \n"
             "    no selection, poll indefinitely until a QUADRA device is "
             "found.\n"
             "    Default: 0\n"
             "-l  Set loglevel of libxcoder API.\n"
             "    [none, fatal, error, info, debug, trace]\n"
             "    Default: info\n"
             "-h  Open this help message.\n"
             "-v  Print version info.\n"
             "\n"
             "Reporting columns\n"
             "INDEX         index number used by resource manager to identify "
             "the resource\n"
             "LOAD          realtime load\n"
             "MODEL_LOAD    estimated load based on framerate and resolution\n"
             "INST          number of job instances\n"
             "MEM           usage of memory local to the card\n"
             "SHARE_MEM     usage of memory shared across cards on the same "
             "device\n"
             "P2P_MEM       usage of memory by P2P\n"
             "DEVICE        path to NVMe device file handle\n"
             "NAMESPACE     path to NVMe namespace file handle\n",
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
    case ':':
        fprintf(stderr, "FATAL: option '-%c' lacks arg\n", opt);
        return 1;
    default:
        fprintf(stderr, "FATAL: unhandled option\n");
        return 1;
    }
  }

  if ((argc <= 2) && (optind == 1))
  {
    for (; optind < argc; optind++)
    {
      checkInterval = argToI(argv[optind]);
    }
  }

  if (ni_rsrc_init(should_match_rev,timeout_seconds) != 0)
  {
    fprintf(stderr, "FATAL: cannot access NI resource\n");
    return 1;
  }

  p_device_pool = ni_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    fprintf(stderr, "FATAL: cannot get devices info\n");
    return 1;
  }

  printf("**************************************************\n");
  startTime = time(NULL);
#ifdef _ANDROID
  system("chown mediacodec:mediacodec /dev/shm_netint/*");
#endif
  while (!g_xcoder_stop_process)
  {
    now = time(NULL);
    ltime = localtime(&now);
    if (ltime)
    {
      strftime(buf, sizeof(buf), "%c", ltime);
    }
    seconds = now - startTime;
    minutes = seconds / 60;
    hours = minutes / 60;

    ni_rsrc_refresh(should_match_rev);

    printf("%s up %02zu:%02zu:%02zu v%s\n", buf, hours, minutes % 60, seconds % 60,
           NI_XCODER_REVISION);

    /*! print out coders in their current order */

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      fprintf(stderr, "ERROR: Failed to obtain mutex: %p\n", p_device_pool->lock);
      return 1;
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

    for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
    {
        print_perf(k, coders, &xCtxt);
    }

    printf("**************************************************\n");
    fflush(stdout);

    if (checkInterval == 0)
    {
      // run once
      break;
    }
    ni_usleep(checkInterval * 1000 * 1000);
  }
  ni_rsrc_free_device_pool(p_device_pool);
#ifdef _ANDROID
  system("chown mediacodec:mediacodec /dev/shm_netint/*");
  system("chmod 777 /dev/block/nvme*");
#endif

  return 0;
}
