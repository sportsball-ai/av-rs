/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
 *
 *   \file          ni_rsrc_mon.c
 *
 *   @date          April 1, 2018
 *
 *   \brief         NETINT T408 resource monitor utility program
 *
 *   @author
 *
 ******************************************************************************/

#ifdef __linux__
#include <unistd.h>
#include <signal.h>
#include <sys/file.h> /* For flock constants */
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

#ifdef _WIN32
#include "XGetopt.h"

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
  stop_process = 1;
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
      stop_process = 1;
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
void setup_signal_handler(void);


/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void sig_handler(int sig)
{
  if (sig == SIGTERM || sig == SIGINT)
  {
    stop_process = 1;
  }
  else if (sig == SIGHUP)
  {
    printf("SIGHUP, reloading p_config ? ");
    setup_signal_handler();
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
    fprintf(stderr, "Error %d: signal handler setup\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
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
  if ( *(int32_t*)a == *(int32_t*)b ) return 0;
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
  int module_count;
  char module_name[8];
  ni_device_context_t *dev_ctxt_arr = NULL;
  int32_t *module_id_arr = NULL;
  int32_t best_load_module_id = -1;
  ni_device_context_t *p_device_context = NULL;
  
  if (module_type == NI_DEVICE_TYPE_DECODER)
  {
    module_count = coders->decoders_cnt;
    snprintf(module_name, strlen("decoder"), "decoder");
    dev_ctxt_arr = (ni_device_context_t *)malloc(sizeof(ni_device_context_t) * module_count);
    if(!dev_ctxt_arr)
    {
      fprintf(stderr, "ERROR %d: Failed to allocate memory for ni_rsrc_mon print_perf()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return;
    }
    module_id_arr = (int32_t *)malloc(sizeof(int32_t) * module_count);
    if(!module_id_arr)
    {
      fprintf(stderr, "ERROR %d: Failed to allocate memory for ni_rsrc_mon print_perf()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      free(dev_ctxt_arr);
      return;
    }
    memcpy(module_id_arr, coders->decoders, sizeof(int32_t) * module_count);
  }
  else if (module_type == NI_DEVICE_TYPE_ENCODER)
  {
    module_count = coders->encoders_cnt;
    snprintf(module_name, strlen("encoder"), "encoder");
    dev_ctxt_arr = (ni_device_context_t *)malloc(sizeof(ni_device_context_t) * module_count);
    if(!dev_ctxt_arr)
    {
      fprintf(stderr, "ERROR %d: Failed to allocate memory for ni_rsrc_mon print_perf()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return;
    }
    module_id_arr = (int32_t *)malloc(sizeof(int32_t) * module_count);
    if(!module_id_arr)
    {
      fprintf(stderr, "ERROR %d: Failed to allocate memory for ni_rsrc_mon print_perf()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      free(dev_ctxt_arr);
      return;
    }
    memcpy(module_id_arr, coders->encoders, sizeof(int32_t) * module_count);
  }
  
  // First module ID in coders->decoders/encoders is of lowest load
  memcpy((void *) &best_load_module_id, module_id_arr, sizeof(int32_t));

  printf("Num %ss: %d\n", module_name, module_count);
  
  // sort module IDs used
  qsort(module_id_arr, module_count, sizeof(int32_t), compareInt32_t);
  
  // Print performance info headings
  printf("%-4s %-5s %-4s %-10s %-4s %-4s %-14s %-20s\n", "BEST", "INDEX",
         "LOAD", "MODEL_LOAD", "MEM", "INST", "DEVICE", "NAMESPACE");
    
  /*! query each coder and print out their status */
  for (i = 0; i < module_count; i++)
  {
    p_device_context = ni_rsrc_get_device_context(module_type, module_id_arr[i]);

    /*! libxcoder query to get status info including load and instances;*/
    if (p_device_context)
    {
#ifdef XCODER_IO_RW_ENABLED
      sessionCtxt->blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name,
                                                  &sessionCtxt->max_nvme_io_size);
      sessionCtxt->device_handle = sessionCtxt->blk_io_handle;
#else
      sessionCtxt->device_handle = ni_device_open(p_device_context->p_device_info->dev_name,
                                                  &sessionCtxt->max_nvme_io_size);
#endif
      
      // Check device can be opened
      if (NI_INVALID_DEVICE_HANDLE == sessionCtxt->device_handle)
      {
        fprintf(stderr, "Error open device %s, blk device %s\n", p_device_context->p_device_info->dev_name, p_device_context->p_device_info->blk_name);
        ni_rsrc_free_device_context(p_device_context);
        continue;
      }

#ifdef _WIN32
      sessionCtxt->event_handle = ni_create_event();
      if (NI_INVALID_EVENT_HANDLE == sessionCtxt->event_handle)
      {
        ni_rsrc_free_device_context(p_device_context);
        ni_device_close(sessionCtxt->device_handle);
        fprintf(stderr, "ERROR %d: print_perf() create envet\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
        continue;
      }
#endif

      // Check dec/enc can be queried
      sessionCtxt->hw_id = p_device_context->p_device_info->hw_id;
      if (NI_RETCODE_SUCCESS != ni_device_session_query(sessionCtxt, module_type))
      {
        ni_device_close(sessionCtxt->device_handle);
#ifdef _WIN32
        ni_close_event(sessionCtxt->event_handle);
#endif
        fprintf(stderr, "Error query %s %s %s.%d\n", module_name, 
               p_device_context->p_device_info->dev_name,
               p_device_context->p_device_info->blk_name,
               p_device_context->p_device_info->hw_id);
        ni_rsrc_free_device_context(p_device_context);
        continue;
      }
      ni_device_close(sessionCtxt->device_handle);
#ifdef _WIN32
      ni_close_event(sessionCtxt->event_handle);
#endif

      if (0 == sessionCtxt->load_query.total_contexts)
      {
        sessionCtxt->load_query.current_load = 0;
      }
      
      // Evaluate if module is of best load
      char best_load_print[2] = " ";
      if (best_load_module_id == p_device_context->p_device_info->module_id)
      {
        strncpy(best_load_print, "L", 1);
      }
      
      // Print performance info row
      printf("%s%s%s  %-5d %-4d %-10d %-4d %-4d %-14s %-20s\n", best_load_print, " ", " ",
             p_device_context->p_device_info->module_id,
             sessionCtxt->load_query.current_load,
             sessionCtxt->load_query.fw_model_load,
             sessionCtxt->load_query.fw_video_mem_usage,
             sessionCtxt->load_query.total_contexts,
             p_device_context->p_device_info->dev_name,
             p_device_context->p_device_info->blk_name);
      
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
 *          0 on success
 *          1 on failure
 *******************************************************************************/
int main(int argc, char *argv[])
{
  int rc = 0;
  int checkInterval;
  int should_match_rev = 1;
  enum outFormat
  {
    text,
    json
  } printFormat;
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_queue_t *coders = NULL;
  ni_device_context_t *p_device_context = NULL;
  ni_session_context_t xCtxt = { 0 };
  time_t startTime = { 0 }, now = { 0 };
  int timeout_seconds = 0;
  struct tm *ltime = NULL;
  char buf[64] = { 0 };
  time_t hours, minutes, seconds;
  int opt;
  printFormat = text;
  checkInterval = 0;
  int refresh = 1;

#ifdef _WIN32
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#elif __linux__
  setup_signal_handler();
#endif

  // arg handling
  while ((opt = getopt(argc, argv, "hrn:o:t:l:i")) != -1)
  {
    switch (opt)
    {
    case 'o':
      // Output print format
      if (!strcmp(optarg, "json"))
      {
        printFormat = json;
      }
      else if (!strcmp(optarg, "text"))
      {
        printFormat = text;
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
    case 'r':
      should_match_rev = 0;
      break;
    case 't':
      timeout_seconds = atoi(optarg);
      printf("Timeout will be set %d\n", timeout_seconds);
      break;
    case 'l':
      if (!strcmp(optarg, "none")) {
        ni_log_set_level(NI_LOG_NONE);
      } else if (!strcmp(optarg, "fatal")) {
        ni_log_set_level(NI_LOG_FATAL);
      } else if (!strcmp(optarg, "error")) {
        ni_log_set_level(NI_LOG_ERROR);
      } else if (!strcmp(optarg, "info")) {
        ni_log_set_level(NI_LOG_INFO);
      } else if (!strcmp(optarg, "debug")) {
        ni_log_set_level(NI_LOG_DEBUG);
      } else if (!strcmp(optarg, "trace")) {
        ni_log_set_level(NI_LOG_TRACE);
      } else {
        fprintf(stderr, "unknown log level selected: %s", optarg);
        return 1;
      }
      break;
    case 'i':
      refresh = 0;
      break;
    case 'h':
      // help message
      printf("-------- ni_rsrc_mon v%s --------\n"
             "The ni_rsrc_mon program provides a real-time view of NETINT T408 \n"
             "resources running on the system.\n"
             "return 0 on success\n"
             "return 1 on failure\n"
             "Usage: sudo ni_rsrc_mon [OPTIONS]\n"
             "-n\tSpecify reporting interval in one second interval.\n"
             "\tIf 0 or no selection, report only once.\n\tDefault: 0\n"
             "\n-r\tInit transcoder card resource regardless firmware release \n"
             "\tversion to libxcoder version compatibility.\n"
             "\tDefault: only init cards with compatible firmware version.\n"
             "\n-t\tSet timeout time in seconds for device polling. Program will \n"
             "\texit with failure if timeout is reached without finding at \n"
             "\tleast one device. If 0 or no selection, poll indefinitely \n"
             "\tuntil a T408 device is found.\n"
             "\tDefault: 0\n"
             "\n-l\tSet loglevel of libxcoder API.\n"
             "\t[none, fatal, error, info, debug, trace]\n"
             "\tDefault: info\n"
             "\n-i\tDo not refresh the devices list.\n"
             "\n-h\tOpen this help message.\n"
             "\nReporting columns\n"
             "BEST          flag showing card of lowest realtime load and to be \n"
             "              selected for next auto allocated job\n"
             "INDEX         index number used by resource manager to identify the \n"
             "              resource\n"
             "LOAD          realtime load\n"
             "MODEL_LOAD    estimated load based on framerate and resolution\n"
             "INST          number of job instances\n"
             "DEVICE        path to NVMe device file handle\n"
             "NAMESPACE     path to NVMe namespace file handle\n", NI_XCODER_REVISION);
      return 0;
    case '?':
#ifdef _WIN32
    fprintf(stderr, "Option -o, -n, or -l require an argument, use option -h for help, all other options are not supported.\n");
    return 1;
#elif __linux__
      if ((optopt == 'o') || (optopt == 'n') || (optopt == 'l'))
      {
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        return 1;
      }
      else if (isprint(optopt))
      {
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        return 1;
      }
      else
      {
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        return 1;
      }
#endif
      break;
    default:
      fprintf(stderr, "ABORTING\n");
      abort();
    }
  }

  if ((argc <= 2) && (optind == 1))
  {
    for (; optind < argc; optind++)
    {
      checkInterval = argToI(argv[optind]);
    }
  }

  if (ni_rsrc_init(should_match_rev,timeout_seconds) < 0)
  {
    fprintf(stderr, "Error access NI resource, quit ..\n");
    return 1;
  }

  p_device_pool = ni_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    fprintf(stderr, "Error get Coders info ..\n");
    return 1;
  }

  printf("**************************************************\n");
  startTime = time(NULL);
  while (!stop_process)
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

    if (refresh)
    {
      if (NI_RETCODE_SUCCESS != ni_rsrc_refresh(should_match_rev))
      {
        printf("Error: resource pool records might be corrupted!!\n");
        return 1;
      }
    }

    printf("%s up %02ld:%02ld:%02ld v%s\n", buf, hours, minutes % 60, seconds % 60,
           NI_XCODER_REVISION);

    /*! print out coders in their current order */

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      fprintf(stderr, "ERROR: Failed to obtain mutex: %p\n", p_device_pool->lock);
      return 1;
    }
#elif __linux__
    if ( flock(p_device_pool->lock, LOCK_EX) )
    {
      fprintf(stderr, "Error flock() failed\n");
    }
#endif

    coders = p_device_pool->p_device_queue;

#ifdef _WIN32
    ReleaseMutex((HANDLE)p_device_pool->lock);
#elif __linux__
    if ( flock(p_device_pool->lock, LOCK_UN) )
    {
      fprintf(stderr, "Error flock() failed\n");
    }
#endif
  
    print_perf(NI_DEVICE_TYPE_DECODER, coders, &xCtxt);
    print_perf(NI_DEVICE_TYPE_ENCODER, coders, &xCtxt);

    printf("**************************************************\n");
    fflush(stderr);

    if (checkInterval == 0)
    {
      // run once
      break;
    }
#ifdef _WIN32
  usleep(checkInterval * 1000 * 1000);
#elif __linux
  sleep(checkInterval);
#endif
  }
  ni_rsrc_free_device_pool(p_device_pool);
  
  return 0;
}
