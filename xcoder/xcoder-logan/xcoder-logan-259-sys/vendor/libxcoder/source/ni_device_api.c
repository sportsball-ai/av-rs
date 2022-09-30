/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_api.c
*
*  \brief  Main NETINT device API file
*           provides the ability to communicate with NI T-408 type hardware 
*           transcoder devices
*
*******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#define _GNU_SOURCE //O_DIRECT is Linux-specific.  One must define _GNU_SOURCE to obtain its definitions
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <semaphore.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "ni_device_api.h"
#include "ni_device_api_priv.h"
#include "ni_nvme.h"
#include "ni_util.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"


const char * const g_xcoder_preset_names[NI_XCODER_PRESET_NAMES_ARRAY_LEN] = 
                                            { NI_XCODER_PRESET_NAME_DEFAULT, NI_XCODER_PRESET_NAME_CUSTOM, 0 };
const char * const g_xcoder_log_names[NI_XCODER_LOG_NAMES_ARRAY_LEN] = 
                                            { NI_XCODER_LOG_NAME_NONE, NI_XCODER_LOG_NAME_ERROR, NI_XCODER_LOG_NAME_WARN,
                                            NI_XCODER_LOG_NAME_INFO, NI_XCODER_LOG_NAME_DEBUG, NI_XCODER_LOG_NAME_FULL, 0 };

#ifdef __linux__
static struct stat g_nvme_stat = { 0 };
#endif

/*!******************************************************************************
 *  \brief  Allocates and initializes a new ni_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 *******************************************************************************/
 ni_session_context_t * ni_device_session_context_alloc_init(void)
 {
   ni_session_context_t *p_ctx = NULL;
   
   p_ctx = malloc(sizeof(ni_session_context_t));
   if (!p_ctx)
   {
     ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for session context\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
   }
   else 
   {
     p_ctx->needs_dealoc = 1;
     ni_device_session_context_init(p_ctx);
   }
   
   return p_ctx;
 }
 
 /*!******************************************************************************
 *  \brief  Frees previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t struct
 *
 *******************************************************************************/
 void ni_device_session_context_free(ni_session_context_t *p_ctx)
 {
   if (p_ctx && p_ctx->needs_dealoc)
   {
     free(p_ctx);
   }
 }

/*!******************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t struct
 *
 *
 *******************************************************************************/
 void ni_device_session_context_init(ni_session_context_t *p_ctx)
 {
   if (!p_ctx)
   {
     return;
   }
   
   memset(p_ctx, 0, sizeof(ni_session_context_t));
  // Init the max IO size to be invalid
   p_ctx->max_nvme_io_size = NI_INVALID_IO_SIZE;
   p_ctx->session_id = NI_INVALID_SESSION_ID;
   p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
   p_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
   p_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
   p_ctx->hw_id = NI_INVALID_HWID;
   p_ctx->dev_xcoder[0] = '\0';
   p_ctx->event_handle = NI_INVALID_EVENT_HANDLE;
   p_ctx->thread_event_handle = NI_INVALID_EVENT_HANDLE;
   p_ctx->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;

   strncat(p_ctx->dev_xcoder, BEST_MODEL_LOAD_STR, strlen(BEST_MODEL_LOAD_STR));
#ifdef MEASURE_LATENCY
   p_ctx->frame_time_q = ni_lat_meas_q_create(2000);
   p_ctx->prev_read_frame_time = 0;
#endif
 }

/*!******************************************************************************
 *  \brief  Create event and returnes event handle if successful
 *
 *  \return On success returns a event handle
 *          On failure returns NI_INVALID_EVENT_HANDLE
 *******************************************************************************/
ni_event_handle_t ni_create_event()
{
#ifdef _WIN32
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
  DWORD retval = 0;

  // Input-0 determines whether the returned handle can be inherited by the child process.If lpEventAttributes is NULL, this handle cannot be inherited.
  // Input-1 specifies whether the event object is created to be restored manually or automatically.If set to FALSE, when a thread waits for an event signal, the system automatically restores the event state to a non-signaled state.
  // Input-2 specifies the initial state of the event object.If TRUE, the initial state is signaled;Otherwise, no signal state.
  // Input-3 If the lpName is NULL, a nameless event object is created.
  event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (event_handle == NI_INVALID_EVENT_HANDLE)
  {
    retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_ERROR, "ERROR %d: create event failed\n",retval);
    return NI_INVALID_EVENT_HANDLE;
  }
  return event_handle;
#else
  return NI_INVALID_EVENT_HANDLE;
#endif
}

/*!******************************************************************************
 *  \brief  Closes event and releases resources
 *
 *  \return NONE
 *          
 *******************************************************************************/
void ni_close_event(ni_event_handle_t event_handle)
{
  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    ni_log(NI_LOG_TRACE, "Warning ni_close_event: null parameter passed %d\n", event_handle);
    return;
  }
  
  ni_log(NI_LOG_TRACE, "ni_close_event(): enter\n");
  
#ifdef _WIN32
  BOOL retval;
  ni_log(NI_LOG_TRACE, "ni_close_event(): closing %p\n", event_handle);
  
  retval = CloseHandle(event_handle);
  if (FALSE == retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_device_close(): closing event_handle %p failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, event_handle);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_device_close(): device %p closed successfuly\n", event_handle);
  }
#elif __linux__
  int err = 0;
  ni_log(NI_LOG_TRACE, "ni_close_event(): closing %d\n", event_handle);
  err = close(event_handle);
  if (err)
  {
    switch (err)
    {
      case EBADF:
        ni_log(NI_LOG_TRACE, "ERROR: ni_close_event(): failed, error EBADF\n");
        break;
      case EINTR:
        ni_log(NI_LOG_TRACE, "ERROR: ni_close_event(): error EINTR\n");
        break;
      case EIO:
        ni_log(NI_LOG_TRACE, "ERROR: ni_close_event(): error EIO\n");
        break;
      default:
        ni_log(NI_LOG_TRACE, "ERROR: ni_close_event(): unknoen error %d\n", err);
    }

  }
#endif //__linux__
  ni_log(NI_LOG_TRACE, "ni_device_close(): exit\n");
}

/*!******************************************************************************
 *  \brief  Opens device and returnes device device_handle if successful
 *
 *  \param[in]  p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_max_io_size_out Maximum IO Transfer size supported
 *
 *  \return On success returns a device device_handle
 *          On failure returns NI_INVALID_DEVICE_HANDLE
 *******************************************************************************/
ni_device_handle_t ni_device_open(const char * p_dev, uint32_t * p_max_io_size_out)
{
  ni_log(NI_LOG_TRACE, "ni_device_open: opening %s\n", p_dev);
  if ( (!p_dev) || (!p_max_io_size_out) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
#ifdef _WIN32
    return NI_INVALID_DEVICE_HANDLE;
#else
    return NI_RETCODE_INVALID_PARAM;
#endif
  }
  
#ifdef _WIN32
  DWORD retval = 0;
  
  *p_max_io_size_out =  0; 

#ifdef XCODER_IO_RW_ENABLED
  HANDLE device_handle = CreateFile( p_dev,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
                NULL
                            );
#else
  HANDLE device_handle = CreateFile( p_dev,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
                            );
#endif

  if (INVALID_HANDLE_VALUE == device_handle)
  {
    retval = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_TRACE, "Failed to open %s, retval %d \n", p_dev, retval);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "Found NVME Controller at %s \n", p_dev);
  }
  return device_handle;

#elif __linux__

  int retval = -1;
  ni_device_handle_t fd = NI_INVALID_DEVICE_HANDLE;
  
  if (*p_max_io_size_out == NI_INVALID_IO_SIZE)
  {
    *p_max_io_size_out = ni_get_kernel_max_io_size(p_dev);
  }

#ifdef XCODER_IO_RW_ENABLED
  ni_log(NI_LOG_TRACE, "ni_device_open: opening XCODER_IO_RW_ENABLED %s\n", p_dev);
  //O_SYNC is added to ensure that data is written to the card when the pread/pwrite function returns
  //O_DIRECT is added to ensure that data can be sent directly to the card instead of to cache memory
  fd = open(p_dev, O_RDWR|O_SYNC|O_DIRECT);
#else
  fd = open(p_dev, O_RDONLY);
#endif
  if (fd < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: open() failed on %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, p_dev);
    ni_log(NI_LOG_ERROR, "ERROR: ni_device_open() failed!\n");
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }

  retval = fstat(fd, &g_nvme_stat);
  if (retval < 0)
  {
    ni_log(NI_LOG_TRACE, "ERROR: fstat() failed on %s\n", p_dev);
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_open() failed!\n");
    close(fd);
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }
  
  if (!S_ISCHR(g_nvme_stat.st_mode) && !S_ISBLK(g_nvme_stat.st_mode))
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s is not a block or character device\n", p_dev);
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_open() failed!\n");
    close(fd);
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }
  
  ni_log(NI_LOG_TRACE, "ni_device_open: success, fd=%d\n", fd);

  END;

  return (fd);
#endif //__linux__

}

/*!******************************************************************************
 *  \brief  Closes device and releases resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *
 *  \return NONE
 *          
 *******************************************************************************/
void ni_device_close(ni_device_handle_t device_handle)
{
  if ( NI_INVALID_DEVICE_HANDLE == device_handle )
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_device_close: null parameter passed\n");
    return;
  }
  
  ni_log(NI_LOG_TRACE, "ni_device_close(): enter\n");
  
#ifdef _WIN32
  BOOL retval;
  // windows nvme dirver will send retry command when I/O error, which will takes more than 4 senconds. 
  // so we add CancelIo here to wait the command finished. And avoid re-opening the open session.
  retval = CancelIo(device_handle);
  if (FALSE == retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_device_close() cancel io failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
  }

  ni_log(NI_LOG_TRACE, "ni_device_close(): closing %p\n", device_handle);
  
  retval = CloseHandle(device_handle);
  if (FALSE == retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_device_close(): closing device device_handle %p failed, error: %d\n", device_handle, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_device_close(): device %p closed successfuly\n", device_handle);
  }
#elif __linux__
  int err = 0;
  ni_log(NI_LOG_TRACE, "ni_device_close(): closing %d\n", device_handle);
  err = close(device_handle);
  if (err)
  {
    switch (err)
    {
      case EBADF:
        ni_log(NI_LOG_TRACE, "ERROR: ni_device_close(): failed, error EBADF\n");
        break;
      case EINTR:
        ni_log(NI_LOG_TRACE, "ERROR: ni_device_close(): error EINTR\n");
        break;
      case EIO:
        ni_log(NI_LOG_TRACE, "ERROR: ni_device_close(): error EIO\n");
        break;
      default:
        ni_log(NI_LOG_TRACE, "ERROR: ni_device_close(): unknoen error %d\n", err);
    }

  }
#endif //__linux__
  ni_log(NI_LOG_TRACE, "ni_device_close(): exit\n");
}


/*!******************************************************************************
 *  \brief  Queries device and returns device capability structure
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *  \param[in] p_cap  Pointer to a caller allocated ni_device_capability_t struct
 *  \return On success 
 *                     NI_RETCODE_SUCCESS
 *          On failure 
                       NI_RETCODE_INVALID_PARAM
                       NI_RETCODE_ERROR_MEM_ALOC
                       NI_RETCODE_ERROR_NVME_CMD_FAILED
 *******************************************************************************/
ni_retcode_t ni_device_capability_query(ni_device_handle_t device_handle, ni_device_capability_t* p_cap)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void * p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  ni_log(NI_LOG_TRACE, "ni_device_capability_query(): enter\n");

  if ( (NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_cap) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_capability_query(): passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_NVME_IDENTITY_CMD_DATA_SZ))
  {
    ni_log(NI_LOG_TRACE, "ERROR %d: ni_device_capability_query(): Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, NI_NVME_IDENTITY_CMD_DATA_SZ);

#ifdef XCODER_IO_RW_ENABLED
#ifdef _WIN32
  event_handle = ni_create_event();
  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    retval = NI_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN;
  }
#endif

  uint32_t ui32LBA = IDENTIFY_DEVICE_R;
  if (ni_nvme_send_read_cmd(device_handle, event_handle, p_buffer, NI_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }
#else

  cmd.cdw10 = ni_htonl(1); // to return id controller data structure

  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_identify, device_handle, &cmd, NI_NVME_IDENTITY_CMD_DATA_SZ, p_buffer, &nvme_result))
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  nvme_result = ni_htonl(nvme_result);
#endif

  ni_populate_device_capability_struct(p_cap, p_buffer);

  END;

  aligned_free(p_buffer);
#ifdef _WIN32
  ni_close_event(event_handle);
#endif

  ni_log(NI_LOG_TRACE, "ni_device_capability_query(): retval: %d\n", retval);

  return retval;
}

/*!******************************************************************************
 *  \brief  Opens a new device session depending on the device_type parameter
 *          If device_type is NI_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_DEVICE_TYPE_EECODER opens encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated 
 *                                               ni_session_context_t struct
 *  \param[in] p_config     Pointer to a caller allocated 
 *                                               ni_session_config_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_retcode_t ni_device_session_open( ni_session_context_t *p_ctx, ni_device_type_t device_type )
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_device_pool_t *p_device_pool = NULL;
  ni_nvme_result_t nvme_result = {0};
  ni_device_context_t *p_device_context = NULL;
  ni_device_info_t *p_dev_info = NULL;
  ni_session_context_t p_session_context = {0};
  ni_device_info_t dev_info = { 0 };
    /*! resource management context */
  ni_device_context_t *rsrc_ctx = NULL;
  int i = 0;
  int rc = 0;
  int num_coders = 0;
  int least_model_load = 0;
  int least_instance = 0;
  int guid = NI_INVALID_HWID;
  int num_sw_instances = 0;
  int user_handles = false;
  ni_lock_handle_t lock = NI_INVALID_LOCK_HANDLE;
  ni_device_handle_t handle = NI_INVALID_DEVICE_HANDLE;
  ni_device_handle_t handle1 = NI_INVALID_DEVICE_HANDLE;
  // For none nvme block device we just need to pass in dummy
  uint32_t dummy_io_size = 0;


  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_device_session_open passed parameters are null!, return\n");
    retval =  NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  handle = p_ctx->device_handle;
  handle1 = p_ctx->blk_io_handle;
  
  if ( NI_INVALID_SESSION_ID != p_ctx->session_id )
  {
    ni_log(NI_LOG_ERROR, "ERROR: trying to overwrite existing session\n");
    retval =  NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  p_device_pool = ni_rsrc_get_device_pool();
  
  if (!p_device_pool)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Error calling ni_rsrc_get_device_pool()\n");
    retval =  NI_RETCODE_ERROR_GET_DEVICE_POOL;
    LRETURN;
  }

  // User did not pass in any handle, so we create it for them
  if ((handle1 == NI_INVALID_DEVICE_HANDLE) && (handle == NI_INVALID_DEVICE_HANDLE))
  {
    if (p_ctx->hw_id >=0) // User selected the encoder/ decoder number
    {
      if ((rsrc_ctx = ni_rsrc_allocate_simple_direct(device_type, p_ctx->hw_id)) == NULL)
      {

        ni_log(NI_LOG_INFO, "Error XCoder resource allocation: inst %d \n", p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        LRETURN;
      }
      ni_log(NI_LOG_TRACE, "device %p\n", rsrc_ctx);
      // Now the device name is in the rsrc_ctx, we open this device to get the file handles
  
#ifdef _WIN32
      //This is windows version!!!
      if ((handle = ni_device_open(rsrc_ctx->p_device_info->dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) 
      {
        ni_rsrc_free_device_context(rsrc_ctx);
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        LRETURN;
      } 
      else 
      {
        user_handles = true;
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle;
        handle1 = handle;
        p_ctx->dev_xcoder_name[0] = '\0';
        p_ctx->blk_xcoder_name[0] = '\0';
        strncat(p_ctx->dev_xcoder_name, rsrc_ctx->p_device_info->dev_name, strlen(rsrc_ctx->p_device_info->dev_name));
        strncat(p_ctx->blk_xcoder_name, rsrc_ctx->p_device_info->blk_name, strlen(rsrc_ctx->p_device_info->blk_name));

        // no longer accept 512-aligned bitstream
        p_ctx->is_dec_pkt_512_aligned = 0;

        ni_rsrc_free_device_context(rsrc_ctx);
      }  
#elif __linux__
#ifdef XCODER_IO_RW_ENABLED 
      //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice. 
      if (((handle = ni_device_open(rsrc_ctx->p_device_info->blk_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(rsrc_ctx->p_device_info->blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
#else
      if (((handle = ni_device_open(rsrc_ctx->p_device_info->dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(rsrc_ctx->p_device_info->blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
#endif
      {
        ni_rsrc_free_device_context(rsrc_ctx);
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        LRETURN;
      }
      else
      {
        user_handles = true;
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle1;
        p_ctx->dev_xcoder_name[0] = '\0';
        p_ctx->blk_xcoder_name[0] = '\0';
        strncat(p_ctx->dev_xcoder_name, rsrc_ctx->p_device_info->dev_name, strlen(rsrc_ctx->p_device_info->dev_name));
        strncat(p_ctx->blk_xcoder_name, rsrc_ctx->p_device_info->blk_name, strlen(rsrc_ctx->p_device_info->blk_name));

        // no longer accept 512-aligned bitstream
        p_ctx->is_dec_pkt_512_aligned = 0;

        ni_rsrc_free_device_context(rsrc_ctx);
      }
#endif
    }
    // we use this to support the best model load & best instance 
    else if ((0 == strcmp(p_ctx->dev_xcoder, BEST_MODEL_LOAD_STR)) || (0 == strcmp(p_ctx->dev_xcoder, BEST_DEVICE_INST_STR)))
    {
      if (ni_rsrc_lock_and_open(device_type, &lock) != NI_RETCODE_SUCCESS)
      {
        retval = NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
        LRETURN;
      }

      // Then we need to query through all the board to confirm the least model load/ instance load board
      if ((NI_DEVICE_TYPE_DECODER == device_type) || (NI_DEVICE_TYPE_ENCODER == device_type))
      {
        num_coders = p_device_pool->p_device_queue->decoders_cnt;
      }
      else
      {
        retval = NI_RETCODE_INVALID_PARAM;
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
        LRETURN;
      }
      
      int tmp_id = -1;
      for (i = 0; i < num_coders; i++)
      {
        if (NI_DEVICE_TYPE_DECODER == device_type) 
        {
          tmp_id = p_device_pool->p_device_queue->decoders[i];
        }
        else if (NI_DEVICE_TYPE_ENCODER == device_type)
        {
          tmp_id = p_device_pool->p_device_queue->encoders[i];
        }
        p_device_context = ni_rsrc_get_device_context(device_type, tmp_id);
        if (! p_device_context)
        {
          ni_log(NI_LOG_INFO, "Error getting device type %d id %d",
                 device_type, tmp_id);
          continue;
        }

#ifdef XCODER_IO_RW_ENABLED
        //Code is included in the for loop. In the loop, the device is just opened once, and it will be closed once too. 
        p_session_context.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name, &dummy_io_size);
        p_session_context.device_handle = p_session_context.blk_io_handle;
#else
        p_session_context.device_handle = ni_device_open(p_device_context->p_device_info->dev_name, &dummy_io_size);
#endif
        if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
        {
          ni_rsrc_free_device_context(p_device_context);
          ni_log(NI_LOG_TRACE, "Error open device");
          continue;
        }

#ifdef _WIN32
        p_session_context.event_handle = ni_create_event();
        if (NI_INVALID_EVENT_HANDLE == p_session_context.event_handle)
        {
          ni_rsrc_free_device_context(p_device_context);
          ni_device_close(p_session_context.device_handle);
          ni_log(NI_LOG_TRACE, "Error create envet");
          continue;
        }
#endif

        char str_fw_API_ver[3];
        int fw_API_ver;
        memcpy(str_fw_API_ver,
               &(p_device_context->p_device_info->fw_rev[NI_XCODER_VER_SZ + 1 + NI_XCODER_API_FLAVOR_SZ]),
               NI_XCODER_API_VER_SZ);
        str_fw_API_ver[NI_XCODER_API_VER_SZ] = '\0';
        fw_API_ver = atoi(str_fw_API_ver);
        ni_log(NI_LOG_TRACE,"Current FW API version is: %d\n", fw_API_ver);

        p_session_context.hw_id = p_device_context->p_device_info->hw_id;
        rc = ni_device_session_query(&p_session_context, device_type);
        if (NI_INVALID_DEVICE_HANDLE != p_session_context.device_handle)
        {
          ni_device_close(p_session_context.device_handle);
        }

#ifdef _WIN32
        if (NI_INVALID_EVENT_HANDLE != p_session_context.event_handle)
        {
          ni_close_event(p_session_context.event_handle);
        }
#endif
        if (NI_RETCODE_SUCCESS != rc)
        {
          ni_log(NI_LOG_TRACE, "Error query %s %s.%d\n",
                  NI_DEVICE_TYPE_DECODER == device_type ? "decoder" : "encoder",
                  p_device_context->p_device_info->dev_name, p_device_context->p_device_info->hw_id);
          ni_rsrc_free_device_context(p_device_context);
          continue;
        }
        ni_rsrc_update_record(p_device_context, &p_session_context);
        p_dev_info = p_device_context->p_device_info;

        // here we select the best load
        if ((0 == strcmp(p_ctx->dev_xcoder, BEST_MODEL_LOAD_STR)))
        {
          if (((guid == NI_INVALID_HWID) || p_dev_info->model_load < least_model_load ||
               ((p_dev_info->model_load == least_model_load) && (p_dev_info->active_num_inst < num_sw_instances))) &&
              (p_dev_info->active_num_inst < p_dev_info->max_instance_cnt))
          {
            guid = tmp_id;
            least_model_load = p_dev_info->model_load;
            num_sw_instances = p_dev_info->active_num_inst;
            memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
          }
        }
        else
        {
          // here we select the best instance
          if (((guid == NI_INVALID_HWID) || (p_dev_info->active_num_inst < least_instance) || (p_dev_info->active_num_inst == least_instance)) && 
               (p_dev_info->active_num_inst < p_dev_info->max_instance_cnt))
          {
            guid = tmp_id;
            least_instance = p_dev_info->active_num_inst;
            memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
          }
        }
        ni_rsrc_free_device_context(p_device_context);
      }

      // Now we have the device info that has the least model load/least instance of the FW
      // we open this device and assign the FD
#ifdef _WIN32
      //This is windows version!!!
      if ((handle = ni_device_open(dev_info.dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) 
      {
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
        LRETURN
      } 
      else 
      {
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle;
        handle1 = handle;
        p_ctx->hw_id = guid;
        p_ctx->dev_xcoder_name[0] = '\0';
        p_ctx->blk_xcoder_name[0] = '\0';
        strncat(p_ctx->dev_xcoder_name, dev_info.dev_name, strlen(dev_info.dev_name));
        strncat(p_ctx->blk_xcoder_name, dev_info.blk_name, strlen(dev_info.blk_name));

        // no longer accept 512-aligned bitstream
        p_ctx->is_dec_pkt_512_aligned = 0;

      }  
#elif __linux__
#ifdef XCODER_IO_RW_ENABLED
      //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice.
      if (((handle = ni_device_open(dev_info.blk_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(dev_info.blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
#else
      if (((handle = ni_device_open(dev_info.dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(dev_info.blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
#endif
      {
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
        LRETURN
      }
      else
      {
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle1;
        p_ctx->hw_id = guid;
        p_ctx->dev_xcoder_name[0] = '\0';
        p_ctx->blk_xcoder_name[0] = '\0';
        strncat(p_ctx->dev_xcoder_name, dev_info.dev_name, strlen(dev_info.dev_name));
        strncat(p_ctx->blk_xcoder_name, dev_info.blk_name, strlen(dev_info.blk_name));

        // no longer accept 512-aligned bitstream
        p_ctx->is_dec_pkt_512_aligned = 0;

      }
#endif
    }
    // Otherwise the command passed in is invalid
    else
    {
      ni_log(NI_LOG_TRACE, "Error XCoder command line options\n");
      retval = NI_RETCODE_ERROR_INVALID_ALLOCATION_METHOD;
      LRETURN
    }
    
  }
  // user passed in the handle, but one of them is invalid, this is error case so we return error
  else if ((handle1 == NI_INVALID_DEVICE_HANDLE) || (handle == NI_INVALID_DEVICE_HANDLE))
  {
    retval = NI_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN
  }
  // User passed in both handles, so we do not need to allocate for it
  else
  {
    user_handles = true;
  }

  ni_log(NI_LOG_TRACE, "Finish open the session dev:%s blk:%s guid:%d handle:%d handle1%d\n", p_ctx->dev_xcoder_name, p_ctx->blk_xcoder_name, p_ctx->hw_id, p_ctx->device_handle, p_ctx->blk_io_handle);
  
  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      for (i = 0; i< NI_MAX_SESSION_OPEN_RETRIES; i++)  // 20 retries
      {
        retval = ni_decoder_session_open(p_ctx);
        if (NI_RETCODE_ERROR_VPU_RECOVERY == retval)
        {
          ni_decoder_session_close(p_ctx, 0);
          ni_log(NI_LOG_DEBUG, "Encoder vpu recovery retry count: %d", i);
          usleep(NI_SESSION_OPEN_RETRY_INTERVAL_US);  // 200 us
          continue;
        }
        else
        {
          break;
        }
      }
      if (user_handles != true)
      {
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
      }
      // send keep alive signal thread
      if (NI_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      for (i = 0; i< NI_MAX_SESSION_OPEN_RETRIES; i++)  // 20 retries
      {
        retval = ni_encoder_session_open(p_ctx);
        if (NI_RETCODE_ERROR_VPU_RECOVERY == retval)
        {
          ni_encoder_session_close(p_ctx, 0);
          ni_log(NI_LOG_DEBUG, "Encoder vpu recovery retry count: %d", i);
          usleep(NI_SESSION_OPEN_RETRY_INTERVAL_US);  // 200 us
          continue;
        }
        else
        {
          break;
        }
      }
      if (user_handles != true)
      {
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
      }
      // send keep alive signal thread
      if (NI_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    default:
    {
      if (user_handles != true)
      {
        if ( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN
        }
      }
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "Unrecognized device type: %d", device_type);
      LRETURN;
      break;
    }
  }

  
  p_ctx->keep_alive_thread_args = (ni_thread_arg_struct_t *) malloc (sizeof(ni_thread_arg_struct_t));
  if (!p_ctx->keep_alive_thread_args)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: thread_args allocation failed!\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    ni_device_session_close(p_ctx,0,device_type);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  // Initializes the variables required by the keep alive thread
  p_ctx->keep_alive_thread_args->hw_id                = p_ctx->hw_id;
  p_ctx->keep_alive_thread_args->session_id           = p_ctx->session_id;
  p_ctx->keep_alive_thread_args->session_timestamp    = p_ctx->session_timestamp;
  p_ctx->keep_alive_thread_args->device_type          = p_ctx->device_type;
  p_ctx->keep_alive_thread_args->device_handle        = p_ctx->device_handle;
  p_ctx->keep_alive_thread_args->close_thread         = false;
  p_ctx->keep_alive_thread_args->p_buffer             = p_ctx->p_all_zero_buf;
  p_ctx->keep_alive_thread_args->thread_event_handle  = p_ctx->thread_event_handle;
  p_ctx->keep_alive_thread_args->keep_alive_timeout   = p_ctx->keep_alive_timeout;

  if ( 0 != pthread_create(&p_ctx->keep_alive_thread, NULL, ni_session_keep_alive_thread, (void *)p_ctx->keep_alive_thread_args) )
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: failed to create keep alive thread\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_FAILURE;
#ifdef MSVC_BUILD
    p_ctx->keep_alive_thread = (pthread_t){ 0 };
#else
    p_ctx->keep_alive_thread = 0;
#endif
    free(p_ctx->keep_alive_thread_args);
    p_ctx->keep_alive_thread_args = NULL;
    ni_device_session_close(p_ctx,0,device_type);
  }

  END;
  
  if (p_device_pool)
  {
    ni_rsrc_free_device_pool(p_device_pool);
    p_device_pool = NULL;
  }
  
  return retval;
}

/*!******************************************************************************
 *  \brief  Closes device session that was previously opened by calling
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_DEVICE_TYPE_EECODER closes encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] eos_recieved Flag indicating if End Of Stream indicator was recieved
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_retcode_t ni_device_session_close(ni_session_context_t* p_ctx, int eos_recieved, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_session_close passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef MSVC_BUILD
  if (p_ctx->keep_alive_thread.handle)
#else
  if (p_ctx->keep_alive_thread)
#endif
  {
    p_ctx->keep_alive_thread_args->close_thread = true;
    pthread_join(p_ctx->keep_alive_thread, NULL);
    if (p_ctx->keep_alive_thread_args)
    {
      free(p_ctx->keep_alive_thread_args);
    }
#ifdef MSVC_BUILD
    p_ctx->keep_alive_thread = (pthread_t){ 0 };
#else
    p_ctx->keep_alive_thread = 0;
#endif
    p_ctx->keep_alive_thread_args = NULL;
  }
  else
  {
     ni_log(NI_LOG_TRACE, "Cancel invalid keep alive thread: %d", p_ctx->session_id);
  }

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_close(p_ctx, eos_recieved);
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_close(p_ctx, eos_recieved);
      break;
    }

    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

#ifdef _WIN32
  if (p_ctx->event_handle != NI_INVALID_EVENT_HANDLE)
  {
    ni_close_event(p_ctx->event_handle);
    p_ctx->event_handle = NI_INVALID_EVENT_HANDLE;
  }

  if (p_ctx->thread_event_handle != NI_INVALID_EVENT_HANDLE)
  {
    ni_close_event(p_ctx->thread_event_handle);
    p_ctx->thread_event_handle = NI_INVALID_EVENT_HANDLE;
  }

  p_ctx->session_id = NI_INVALID_SESSION_ID; // need set invalid after closed. May cause open invalid parameters.
#endif

  return retval;
}

/*!******************************************************************************
 *  \brief  Sends a flush command to the device
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER sends flush command to decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER sends flush command to decoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_retcode_t ni_device_session_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_session_flush passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_flush(p_ctx);
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_flush(p_ctx);
      break;
    }

    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }
  p_ctx->ready_to_close = (NI_RETCODE_SUCCESS == retval);
  return retval;
}

/*!******************************************************************************
 *  \brief  Sends data the device 
 *          If device_type is NI_DEVICE_TYPE_DECODER sends data packet to decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER sends data frame to encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a 
 *                          ni_frame_t data frame or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *                          If NI_DEVICE_TYPE_DECODER is specified, it is expected
 *                          that the ni_packet_t struct inside the p_data pointer
 *                          contains data to send.
 *                          If NI_DEVICE_TYPE_ENCODER is specified, it is expected
 *                          that the ni_frame_t struct inside the p_data pointer
 *                          contains data to send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
int ni_device_session_write(ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  
  if ((!p_ctx) || (!p_data))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_session_write passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  // Here check if keep alive thread is closed.
#ifdef MSVC_BUILD
  if ((p_ctx->keep_alive_thread.handle) && (p_ctx->keep_alive_thread_args->close_thread))
#else
  if ((p_ctx->keep_alive_thread) && (p_ctx->keep_alive_thread_args->close_thread))
#endif
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_device_session_write() keep alive thread has been closed, hw:%d, session:%d\n",
           p_ctx->hw_id, p_ctx->session_id);
    return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_write(p_ctx, &(p_data->data.packet));
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
#if NI_DEBUG_LATENCY
      struct timeval tv;
      gettimeofday(&tv, NULL);
      //p_ctx->microseconds_w_prev = p_ctx->microseconds_w;
      p_ctx->microseconds_w = tv.tv_sec*1000000 + tv.tv_usec;
#endif

      retval = ni_encoder_session_write(p_ctx, &(p_data->data.frame));
      break;
    }

    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

  return retval;
}

/*!******************************************************************************
 *  \brief  Reads data the device
 *          If device_type is NI_DEVICE_TYPE_DECODER reads data packet from decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER reads data frame from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a
 *                          ni_frame_t data frame or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *                          If NI_DEVICE_TYPE_DECODER is specified, data that was 
 *                          read will be placed into ni_frame_t struct inside the p_data pointer
 *                          If NI_DEVICE_TYPE_ENCODER is specified,  data that was 
 *                          read will be placed into ni_packet_t struct inside the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
int ni_device_session_read(ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if ((!p_ctx) || (!p_data))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_session_read passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  // Here check if keep alive thread is closed.
#ifdef MSVC_BUILD
  if ((p_ctx->keep_alive_thread.handle) && (p_ctx->keep_alive_thread_args->close_thread))
#else
  if ((p_ctx->keep_alive_thread) && (p_ctx->keep_alive_thread_args->close_thread))
#endif
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_device_session_read() keep alive thread has been closed, hw:%d, session:%d\n",
           p_ctx->hw_id, p_ctx->session_id);
    return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      int seq_change_read_count = 0;
      while (1)
      {
        retval = ni_decoder_session_read(p_ctx, &(p_data->data.frame));
        // check resolution change only after initial setting obtained
        // p_data->data.frame.video_width is picture width and will be 32-align
        // adjusted to frame size; p_data->data.frame.video_height is the same as
        // frame size, then compare them to saved one for resolution checking
        // 
        int aligned_width = ((p_data->data.frame.video_width + 31) / 32) * 32;
        if ((0 == retval) && seq_change_read_count)
        {
          ni_log(NI_LOG_TRACE, "ni_device_session_read (decoder): seq change NO data, next time.\n");
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          break;
        }
        else if (retval < 0)
        {
          ni_log(NI_LOG_TRACE, "ni_device_session_read (decoder): failure ret %d, "
                         "return ..\n", retval);
          break;
        }
        else if (p_ctx->frame_num && p_data->data.frame.video_width &&
          p_data->data.frame.video_height &&
          ((aligned_width != p_ctx->active_video_width) ||
          (p_data->data.frame.video_height != p_ctx->active_video_height)))
        {
          ni_log(NI_LOG_TRACE, "ni_device_session_read (decoder): resolution "
                         "change, frame size %ux%u -> %ux%u, continue read ...\n",
                         p_ctx->active_video_width,
                         p_ctx->active_video_height,
                         p_data->data.frame.video_width,
                         p_data->data.frame.video_height);
          // reset active video resolution to 0 so it can be queried in the re-read
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          seq_change_read_count++;
        }
        else
        {
          break;
        }
      }
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_read(p_ctx, &(p_data->data.packet));
      break;
    }

    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }
  
  return retval;
}

/*!******************************************************************************
 *  \brief  Query session data from the device - Currently not implemented
 *          If device_type is NI_DEVICE_TYPE_DECODER query session data
 *          from decoder
 *          If device_type is NI_DEVICE_TYPE_EECODER query session data 
 *          from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *                          
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_retcode_t ni_device_session_query(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_device_session_query passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_query(p_ctx);
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_query(p_ctx);
      break;
    }

    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }
  return retval;
}

/*!******************************************************************************
 *  \brief  Allocate memory for the frame buffer based on provided parameters
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                                               ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_frame_buffer_alloc(ni_frame_t* p_frame, int video_width, int video_height, int alignment, int metadata_flag, int factor)
{
  void* p_buffer = NULL;
  int metadata_size = 0;
  int retval = NI_RETCODE_SUCCESS;
  // TBD for sequence change (non-regular resolution video sample):
  // width has to be 16-aligned to prevent f/w assertion
  int width_aligned = video_width;
  int height_aligned = video_height;

  if ((!p_frame) || ((factor!=1) && (factor!=2))
     || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width<=0)
     || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_frame_buffer_alloc passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  if (metadata_flag)
  {
    metadata_size = NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;
  }

  width_aligned = ((video_width + 31) / 32) * 32;
  height_aligned = ((video_height + 7) / 8) * 8;
  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc: aligned=%dx%d org=%dx%d\n", width_aligned, height_aligned, video_width, video_height);

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = chroma_b_size;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size + metadata_size;

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT * 3;

  //Check if need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc: free current p_frame,  p_frame->buffer_size=%d\n", p_frame->buffer_size);
    ni_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
#ifndef XCODER_SIM_ENABLED
    if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#else
    buffer = malloc(buffer_size);
    if (!buffer)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#endif
    // init once after allocation
    //memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc: Allocate new p_frame buffer\n");
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc: reuse p_frame buffer\n");
  }

  p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
  p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
  p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc: success: p_frame->buffer_size=%d\n", p_frame->buffer_size);

  END;

  if (NI_RETCODE_SUCCESS != retval)
  {
    aligned_free(p_buffer);
  }

  return retval;
}

/*!*****************************************************************************
 *  \brief  Allocate memory for decoder frame buffer based on provided 
 *          parameters; the memory is retrieved from a buffer pool and will be
 *          returned to the same buffer pool by ni_decoder_frame_buffer_free.
 *  Note:   all attributes of ni_frame_t will be set up except for memory and
 *          buffer, which rely on the pool being allocated; the pool will be
 *          allocated only after the frame resolution is known.
 *
 *  \param[in] p_pool        Buffer pool to get the memory from
 *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
 *  \param[in] alloc_mem     Whether to get memory from buffer pool
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_decoder_frame_buffer_alloc(
  ni_buf_pool_t* p_pool, ni_frame_t *p_frame, int alloc_mem,
  int video_width, int video_height, int alignment, int factor)
{
  void* p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;

  int width_aligned = video_width;
  int height_aligned = video_height;

  if ((!p_frame) || ((factor!=1) && (factor!=2))
     || (video_width > NI_MAX_RESOLUTION_WIDTH) || (video_width <= 0)
      || (video_height > NI_MAX_RESOLUTION_HEIGHT) || (video_height <= 0))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_decoder_frame_buffer_alloc passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  width_aligned = ((video_width + 31) / 32) * 32;
  height_aligned = ((video_height + 7) / 8) * 8;
  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_alloc: aligned=%dx%d orig=%dx%d\n",
                 width_aligned, height_aligned, video_width, video_height);

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = chroma_b_size;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size +
  NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT * 3;

  p_frame->buffer_size = buffer_size;

  // if need to get a buffer from pool, pool must have been set up
  if (alloc_mem)
  {
    if (! p_pool)
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_frame_buffer_alloc: invalid pool!\n");
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    p_frame->dec_buf = ni_buf_pool_get_buffer(p_pool);
    if (! p_frame->dec_buf)
    {
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    p_frame->p_buffer = p_frame->dec_buf->buf;

    ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_alloc: got new frame ptr %p "
                   "buffer %p\n", p_frame->p_buffer, p_frame->dec_buf);
  }
  else
  {
    p_frame->dec_buf = p_frame->p_buffer = NULL;
    ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_alloc: NOT alloc mem buffer\n");
  }

  if (p_frame->p_buffer)
  {
    p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
    p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
    p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;
  }
  else
  {
    p_frame->p_data[0] = p_frame->p_data[1] = p_frame->p_data[2] = NULL;
  }

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_alloc: success: "
                 "p_frame->buffer_size=%d\n", p_frame->buffer_size);

  END;

  return retval;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer based on provided parameters
  *          taking into account pic line size and extra data. 
  *          Applicable to YUV420p AVFrame only. Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data)
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_frame_buffer_alloc_v3(ni_frame_t* p_frame, int video_width,
                                      int video_height, int linesize[], 
                                      int alignment, int extra_len)
{
  void* p_buffer = NULL;
  int height_aligned = video_height;
  int retval = NI_RETCODE_SUCCESS;

  if ((!p_frame)  || (!linesize)|| (linesize[0]<=0) || (linesize[0]>NI_MAX_RESOLUTION_WIDTH)
     || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width<=0)
     || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_frame_buffer_alloc_v3 passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  // TBD height has to be 8-aligned; had a checking at codec opening, but
  // if still getting non-8-aligned here, that could only be the case of
  // resolution change mid-stream: device_handle it by making them 8-aligned
  height_aligned = ((video_height + 7) / 8) * 8;

  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  if (height_aligned < NI_MIN_HEIGHT)
  {
    height_aligned = NI_MIN_HEIGHT;
  }

  ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc_v3: aligned=%dx%d org=%dx%d "
                 "linesize=%d/%d/%d extra_len=%d\n",
                 video_width, height_aligned, video_width, video_height,
                 linesize[0], linesize[1], linesize[2], extra_len);

  int luma_size = linesize[0] * height_aligned;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = luma_size / 4;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size + extra_len;

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT;

  //Check if Need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc_v3: free current p_frame,  "
                   "p_frame->buffer_size=%d\n", p_frame->buffer_size);
    ni_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
#ifndef XCODER_SIM_ENABLED
    if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#else
    p_buffer = malloc(buffer_size);
    if (!p_buffer)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#endif
    // init once after allocation
    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc_v3: allocated new p_frame buffer\n");
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc_v3: reuse p_frame buffer\n");
  }

  p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
  p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
  p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "ni_frame_buffer_alloc_v3: success: p_frame->buffer_size="
                 "%d\n", p_frame->buffer_size);

  END;

  if (NI_RETCODE_SUCCESS != retval)
  {
    aligned_free(p_buffer);
  }

  return retval;
}

/*!******************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_frame_buffer_alloc or ni_frame_buffer_alloc_v3
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_frame_buffer_free(ni_frame_t* p_frame)
{
  int i;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "ni_frame_buffer_free: enter\n");

  if (!p_frame)
  {
    ni_log(NI_LOG_TRACE, "WARN: ni_frame_buffer_free(): p_frame is NULL\n");
    LRETURN;
  }
  
  if (!p_frame->p_buffer)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_frame_buffer_free(): already freed, nothing to free\n");
    LRETURN;
  }

  aligned_free(p_frame->p_buffer);
  p_frame->p_buffer = NULL;

  p_frame->buffer_size = 0;
  for (i = 0; i < NI_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }

  END;
  
  ni_log(NI_LOG_TRACE, "ni_frame_buffer_free: exit\n");
  
  return retval;
}

/*!*****************************************************************************
 *  \brief  Free decoder frame buffer that was previously allocated with
 *          ni_decoder_frame_buffer_alloc, returning memory to a buffer pool.
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_decoder_frame_buffer_free(ni_frame_t *p_frame)
{
  int i;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_free: enter\n");

  if (!p_frame)
  {
    ni_log(NI_LOG_TRACE, "WARN: ni_decoder_frame_buffer_free(): p_frame is NULL\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_frame->dec_buf)
  {
    ni_buf_pool_return_buffer(p_frame->dec_buf, (ni_buf_pool_t *)p_frame->dec_buf->pool);
    ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_free(): Mem buf returned ptr %p "
                   "buf %p !\n", p_frame->dec_buf->buf, p_frame->dec_buf);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_free(): NO mem buf returned !\n");
  }

  p_frame->dec_buf = p_frame->p_buffer = NULL;

  p_frame->buffer_size = 0;
  for (i = 0; i < NI_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }

  END;

  ni_log(NI_LOG_TRACE, "ni_decoder_frame_buffer_free: exit\n");

  return retval;
}

/*!*****************************************************************************
 *  \brief  Return a memory buffer to memory buffer pool.
 *
 *  \param[in] buf              Buffer to be returned.
 *  \param[in] p_buffer_pool    Buffer pool to return buffer to.
 *
 *  \return None
 ******************************************************************************/
void ni_decoder_frame_buffer_pool_return_buf(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool)
{
  ni_buf_pool_return_buffer(buf, p_buffer_pool);
}

/*!******************************************************************************
 *  \brief  Allocate memory for the packet buffer based on provided packet size
 *
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_packet_t struct
 *  \param[in] packet_size   Required allocation size
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_packet_buffer_alloc(ni_packet_t* p_packet, int packet_size)
{
  void* p_buffer = NULL;
  int metadata_size = 0;
  int retval = NI_RETCODE_SUCCESS;

  metadata_size = NI_FW_META_DATA_SZ;

  int buffer_size = (((packet_size + metadata_size) / NI_MAX_PACKET_SZ) + 1) * NI_MAX_PACKET_SZ;
    
  ni_log(NI_LOG_TRACE, "ni_packet_buffer_alloc: packet_size=%d\n", packet_size + metadata_size);
  
  if (!p_packet || !packet_size)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_packet_buffer_alloc(): null pointer parameters passed\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  
  if (buffer_size % NI_MEM_PAGE_ALIGNMENT)
  {
    buffer_size = ( (buffer_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT ) + NI_MEM_PAGE_ALIGNMENT;
  }

  if (p_packet->buffer_size == buffer_size)
  {
    p_packet->p_data = p_packet->p_buffer;
    ni_log(NI_LOG_TRACE, "ni_packet_buffer_alloc(): reuse current p_packet buffer\n");
    LRETURN; //Already allocated the exact size
  }
  else if (p_packet->buffer_size > 0)
  {
    ni_log(NI_LOG_TRACE, "ni_packet_buffer_alloc(): free current p_packet,  p_packet->buffer_size=%d\n", p_packet->buffer_size);
    ni_packet_buffer_free(p_packet);
  }
  ni_log(NI_LOG_TRACE, "ni_packet_buffer_alloc(): Allocating p_frame buffer, buffer_size=%d\n", buffer_size);
#ifndef XCODER_SIM_ENABLED
  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_TRACE, "ERROR %d: ni_packet_buffer_alloc() Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
#else
  p_buffer = malloc(buffer_size);
  if (p_buffer == NULL)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_packet_buffer_alloc(): Cannot allocate p_frame buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
#endif
  //memset(p_buffer, 0, buffer_size);
  p_packet->buffer_size = buffer_size;
  p_packet->p_buffer = p_buffer;
  p_packet->p_data = p_packet->p_buffer;

  END;

  if (NI_RETCODE_SUCCESS != retval)
  {
    aligned_free(p_buffer);
  }
  
  ni_log(NI_LOG_TRACE, "ni_packet_buffer_alloc: exit: p_packet->buffer_size=%d\n", p_packet->buffer_size);

  return retval;
}

/*!******************************************************************************
 *  \brief  Free packet buffer that was previously allocated with either
 *          ni_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_packet_buffer_free(ni_packet_t* p_packet)
{
  ni_log(NI_LOG_TRACE, "ni_packet_buffer_free(): enter\n");
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (!p_packet)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_packet_buffer_free(): p_packet is NULL\n");
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
  
  if (!p_packet->p_buffer)
  {
    ni_log(NI_LOG_TRACE, "ni_packet_buffer_free(): already freed, nothing to free\n");
    LRETURN;
  }

  aligned_free(p_packet->p_buffer);
  p_packet->p_buffer = NULL;

  p_packet->buffer_size = 0;
  p_packet->data_len = 0;
  p_packet->p_data = NULL;
  
  END;

  ni_log(NI_LOG_TRACE, "ni_packet_buffer_free(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Copy video packet accounting for allighment
 *
 *  \param[in] p_destination  Destination to where to copy to
 *  \param[in] p_source       Source from where to copy from
 *  \param[in] cur_size       current size
 *  \param[out] p_leftover    Pointer to the data that was left over
 *  \param[out] p_prev_size   Size of the data leftover ??
 *
 *  \return On success        Total number of bytes that were copied
 *          On failure        NI_RETCODE_FAILURE
 *******************************************************************************/
int ni_packet_copy(void* p_destination, const void* const p_source, int cur_size, void* p_leftover, int* p_prev_size)
{
  int copy_size = 0;
  int remain_size = 0;
  int padding_size = 0;
  int prev_size = p_prev_size == NULL? 0 : *p_prev_size;

  int total_size = cur_size + prev_size;
  uint8_t* p_src = (uint8_t*)p_source;
  uint8_t* p_dst = (uint8_t*)p_destination;
  uint8_t* p_lftover = (uint8_t*)p_leftover;

  ni_log(NI_LOG_TRACE, "ni_packet_copy(): enter, *prev_size=%d\n", *p_prev_size);

  if ((0 == cur_size) && (0 == prev_size))
  {
    return copy_size;
  }

  if (((0 != cur_size) && (!p_source)) || (!p_destination) || (!p_leftover))
  {
    return NI_RETCODE_FAILURE;
  }

  copy_size = ((total_size + NI_MEM_PAGE_ALIGNMENT - 1) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (copy_size > total_size)
  {
    padding_size = copy_size - total_size;
  }

  if (prev_size > 0)
  {
    memcpy(p_dst, p_lftover, prev_size);
  }
  
  p_dst += prev_size;

  memcpy(p_dst, p_src, total_size);

  p_src += total_size;

  if (padding_size)
  {
    p_dst += total_size;
    memset(p_dst, 0, padding_size);
  }
  
  if (p_prev_size)
  {
    *p_prev_size=remain_size;
  }

  ni_log(NI_LOG_TRACE, "ni_packet_copy(): exit, cur_size=%d, copy_size=%d, prev_size=%d\n", cur_size, copy_size, *p_prev_size);

  return copy_size;
}

/*!******************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success   
 *                        NI_RETCODE_SUCCESS
 *          On failure        
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_encoder_init_default_params
( 
  ni_encoder_params_t* p_param, 
  int fps_num, 
  int fps_denom, 
  long bit_rate, 
  int width, 
  int height
)
{
  ni_h265_encoder_params_t* p_enc = NULL;
  int i = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  //Initialize p_param structure
  if (!p_param)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_init_default_params(): null pointer parameters passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  ni_log(NI_LOG_TRACE, "ni_encoder_init_default_params()\n");

  //Initialize p_param structure
  memset(p_param, 0, sizeof(ni_encoder_params_t));

  p_enc = &p_param->hevc_enc_params;

  // Rev. B: unified for HEVC/H.264
  p_enc->profile = 0;
  p_enc->level_idc = 0;
  p_enc->high_tier = 0;

  p_enc->gop_preset_index = GOP_PRESET_IDX_IBBBP;
  p_enc->use_recommend_enc_params = 0; // TBD: remove this param from API as it is revA specific

  p_enc->rc.trans_rate = 0;

  p_enc->rc.enable_rate_control = 0;
  p_enc->rc.enable_cu_level_rate_control = 1;
  p_enc->rc.enable_hvs_qp = 0;
  p_enc->rc.enable_hvs_qp_scale = 1;
  p_enc->rc.hvs_qp_scale = 2;
  p_enc->rc.min_qp = NI_DEFAULT_MIN_QP;
  p_enc->rc.max_qp = NI_DEFAULT_MAX_QP;
  p_enc->rc.max_delta_qp = NI_DEFAULT_MAX_DELTA_QP;
  p_enc->rc.rc_init_delay = 3000;

  p_enc->roi_enable = 0;

  p_enc->forced_header_enable = NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES;

  p_enc->long_term_ref_enable = 0;

  p_enc->lossless_enable = 0;

  p_enc->conf_win_top = 0;
  p_enc->conf_win_bottom = 0;
  p_enc->conf_win_left = 0;
  p_enc->conf_win_right = 0;

  p_enc->intra_period = 92;
  p_enc->rc.intra_qp = NI_DEFAULT_INTRA_QP;
  // TBD Rev. B: could be shared for HEVC and H.264 ?
  p_enc->rc.enable_mb_level_rc = 1;

  p_enc->decoding_refresh_type = 2;

  // Rev. B: H.264 only parameters.
  p_enc->enable_transform_8x8 = 1;
  p_enc->avc_slice_mode = 0;
  p_enc->avc_slice_arg = 0;
  p_enc->entropy_coding_mode = 1;

  p_enc->intra_mb_refresh_mode = 0;
  p_enc->intra_mb_refresh_arg = 0;

  p_enc->custom_gop_params.custom_gop_size = 0;
  for (i = 0; i < NI_MAX_GOP_NUM; i++)
  {
    p_enc->custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
    p_enc->custom_gop_params.pic_param[i].poc_offset = 0;
    p_enc->custom_gop_params.pic_param[i].pic_qp = 0;
    p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0 = 0; // syed todo: check this initial value, added for T408 5
    p_enc->custom_gop_params.pic_param[i].ref_poc_L0 = 0;
    p_enc->custom_gop_params.pic_param[i].ref_poc_L1 = 0;
    p_enc->custom_gop_params.pic_param[i].temporal_id = 0;
  }

  p_param->source_width = width;
  p_param->source_height = height;

  p_param->fps_number = fps_num;
  p_param->fps_denominator = fps_denom;

  if (p_param->fps_number && p_param->fps_denominator)
  {
    p_enc->frame_rate = p_param->fps_number / p_param->fps_denominator;
  }
  else
  {
    p_enc->frame_rate = 30;
  }

  p_param->bitrate = bit_rate;
  p_param->roi_demo_mode = 0;
  p_param->reconf_demo_mode = 0; // NETINT_INTERNAL - currently only for internal testing
  p_param->force_pic_qp_demo_mode = 0;
  p_param->force_frame_type = 0;
  p_param->hdrEnableVUI = 0;
  p_param->crf = 0;
  p_param->cbr = 0;
  p_param->ui32flushGop = 0;
  p_param->ui32minIntraRefreshCycle = 0;
  p_param->low_delay_mode = 0;
  p_param->padding = 1;
  p_param->generate_enc_hdrs = 0;
  p_param->use_low_delay_poc_type = 0;
  p_param->strict_timeout_mode = 0;

  p_enc->preferred_transfer_characteristics = -1;

  p_param->dolby_vision_profile = 0;
  p_param->hrd_enable = 0;
  p_param->enable_aud = 0;

  if (p_param->source_width > NI_PARAM_MAX_WIDTH)
  {
    retval = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
    LRETURN;
  }
  if (p_param->source_width < NI_PARAM_MIN_WIDTH)
  {
    retval = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL;
    LRETURN;
  }

  if (p_param->source_height > NI_PARAM_MAX_HEIGHT)
  {
    retval = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
    LRETURN;
  }
  if (p_param->source_height < NI_PARAM_MIN_HEIGHT)
  {
    retval =  NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL;
    LRETURN;
  }
  if (p_param->source_height*p_param->source_width > NI_MAX_RESOLUTION_AREA)
  {
    retval = NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
    LRETURN;
  }

  END;

  return retval;
}

ni_retcode_t ni_decoder_init_default_params
(
  ni_encoder_params_t* p_param, 
  int fps_num, 
  int fps_denom, 
  long bit_rate, 
  int width, 
  int height
)
{
  ni_h265_encoder_params_t* p_enc = NULL;
  int i = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  //Initialize p_param structure
  if (!p_param)
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_init_default_params(): null pointer parameter passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  ni_log(NI_LOG_TRACE, "ni_decoder_init_default_params\n");

  //Initialize p_param structure
  memset(p_param, 0, sizeof(ni_encoder_params_t));

  p_enc = &p_param->hevc_enc_params;

  // Rev. B: unified for HEVC/H.264
  p_enc->profile = 0;
  p_enc->level_idc = 0;
  p_enc->high_tier = 0;

  p_enc->gop_preset_index = GOP_PRESET_IDX_IBBBP;
  p_enc->use_recommend_enc_params = 0; // TBD: remove this param from API as it is revA specific

  p_enc->rc.trans_rate = 0;

  p_enc->rc.enable_rate_control = 0;
  p_enc->rc.enable_cu_level_rate_control = 1;
  p_enc->rc.enable_hvs_qp = 0;
  p_enc->rc.enable_hvs_qp_scale = 1;
  p_enc->rc.hvs_qp_scale = 2;
  p_enc->rc.min_qp = NI_DEFAULT_MIN_QP;
  p_enc->rc.max_qp = NI_DEFAULT_MAX_QP;
  p_enc->rc.max_delta_qp = NI_DEFAULT_MAX_DELTA_QP;
  p_enc->rc.rc_init_delay = 3000;

  p_enc->roi_enable = 0;

  p_enc->forced_header_enable = NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES;

  p_enc->long_term_ref_enable = 0;

  p_enc->lossless_enable = 0;

  p_enc->conf_win_top = 0;
  p_enc->conf_win_bottom = 0;
  p_enc->conf_win_left = 0;
  p_enc->conf_win_right = 0;

  p_enc->intra_period = 92;
  p_enc->rc.intra_qp = NI_DEFAULT_INTRA_QP;
  // TBD Rev. B: could be shared for HEVC and H.264 ?
  p_enc->rc.enable_mb_level_rc = 1;

  p_enc->decoding_refresh_type = 2;

  // Rev. B: H.264 only parameters.
  p_enc->enable_transform_8x8 = 1;
  p_enc->avc_slice_mode = 0;
  p_enc->avc_slice_arg = 0;
  p_enc->entropy_coding_mode = 1;

  p_enc->intra_mb_refresh_mode = 0;
  p_enc->intra_mb_refresh_arg = 0;

  p_enc->custom_gop_params.custom_gop_size = 0;
  for (i = 0; i < NI_MAX_GOP_NUM; i++)
  {
    p_enc->custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
    p_enc->custom_gop_params.pic_param[i].poc_offset = 0;
    p_enc->custom_gop_params.pic_param[i].pic_qp = 0;
    p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0 = 0; // syed todo: check this initial value, added for T408 5
    p_enc->custom_gop_params.pic_param[i].ref_poc_L0 = 0;
    p_enc->custom_gop_params.pic_param[i].ref_poc_L1 = 0;
    p_enc->custom_gop_params.pic_param[i].temporal_id = 0;
  }

  p_param->source_width = width;
  p_param->source_height = height;

  p_param->fps_number = fps_num;
  p_param->fps_denominator = fps_denom;

  if (p_param->fps_number && p_param->fps_denominator)
  {
    p_enc->frame_rate = p_param->fps_number / p_param->fps_denominator;
  }
  else
  {
    p_enc->frame_rate = 30;
  }

  p_param->bitrate = bit_rate;
  p_param->reconf_demo_mode = 0; // NETINT_INTERNAL - currently only for internal testing
  p_param->force_pic_qp_demo_mode = 0;
  p_param->force_frame_type = 0;
  p_param->hdrEnableVUI = 0;
  p_param->crf = 0;
  p_param->cbr = 0;
  p_param->ui32flushGop = 0;
  p_param->ui32minIntraRefreshCycle = 0;
  p_param->low_delay_mode = 0;
  p_param->padding = 1;
  p_param->generate_enc_hdrs = 0;
  p_param->use_low_delay_poc_type = 0;
  p_param->strict_timeout_mode = 0;

  p_enc->preferred_transfer_characteristics = -1;

  p_param->dolby_vision_profile = 0;
  p_param->hrd_enable = 0;
  p_param->enable_aud = 0;

  END;

  return retval;
}

// read demo reconfig data file and parse out reconfig key/values in the format:
// key:val1,val2,val3,...val9 (max 9 values); only digit/:/,/newline is allowed
ni_retcode_t ni_parse_reconf_file(const char* reconf_file, int hash_map[100][10])
{
  char keyChar[10] = "";
  int key = 0;
  char valChar[10] = "";
  int val = 0;
  int valIdx = 1;
  int parseKey = 1;
  int idx = 0;
  int readc = EOF;
    
  if (!reconf_file)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_parse_reconf_file(): Null pointer parameters passed\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  
  FILE *reconf = fopen(reconf_file, "r");
  if (!reconf)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_parse_reconf_file(): Cannot open reconfig_file: %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, reconf_file);
    return NI_RETCODE_FAILURE;
  }

  while ((readc = fgetc(reconf)) != EOF)
  {
    //parse lines
    if (isdigit(readc))
    {
      if (parseKey)
      {
        strncat(keyChar, (const char *)(&readc), 1);
      }
      else
      {
        strncat(valChar, (const char *)(&readc), 1);
      }
    }
    else if (readc == ':')
    {
      parseKey = 0;
      key = atoi(keyChar);
      hash_map[idx][0] = key;
    }
    else if (readc == ',')
    {
      val = atoi(valChar);
      hash_map[idx][valIdx] = val;
      valIdx++;
      memset(valChar, 0, 10);
    }
    else if (readc == '\n')
    {
      parseKey = 1;
      val = atoi (valChar);
      hash_map[idx][valIdx] = val;
      valIdx = 1;
      memset(keyChar,0,10);
      memset(valChar,0,10);
      idx ++;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "error character %c in reconfig file. this may lead to mistaken reconfiguration values \n", readc);
    }
  }
  
  fclose(reconf);
  
  return NI_RETCODE_SUCCESS;
}

#undef atoi
#undef atof
#define atoi(p_str) ni_atoi(p_str, b_error)
#define atof(p_str) ni_atof(p_str, b_error)
#define atobool(p_str) (ni_atobool(p_str, b_error))

/*!******************************************************************************
 *  \brief  Set value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_encoder_params_set_value(ni_encoder_params_t* p_params, const char* name, const char* value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_h265_encoder_params_t* p_enc = &p_params->hevc_enc_params;
  char nameBuf[64] = { 0 };

  ni_log(NI_LOG_TRACE, "ni_encoder_params_set_value(): enter\n");

  if ( !p_params ) 
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_params_set_value(): Null pointer parameters passed\n");
    return NI_RETCODE_INVALID_PARAM;
  }
   
  if ( !name )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_params_set_value(): Null pointer parameters passed\n");
    return NI_RETCODE_PARAM_INVALID_NAME;
  }

  // skip -- prefix if provided
  if (name[0] == '-' && name[1] == '-')
  {
    name += 2;
  }

  // s/_/-/g
  if (strlen(name) + 1 < sizeof(nameBuf) && strchr(name, '_'))
  {
    char* c;
    nameBuf[0] = '\0';
    strncat(nameBuf, name, strlen(name));
    while ((c = strchr(nameBuf, '_')) != 0)
    {
      *c = '-';
    }

    name = nameBuf;
  }

  if (!strncmp(name, "no-", 3))
  {
    name += 3;
    value = !value || ni_atobool(value, b_error) ? "false" : "true";
  }
  else if (!strncmp(name, "no", 2))
  {
    name += 2;
    value = !value || ni_atobool(value, b_error) ? "false" : "true";
  }
  else if (!value)
  {
    value = "true";
  }
  else if (value[0] == '=')
  {
    value++;
  }

#if defined(_MSC_VER)
#pragma warning(disable : 4127) // conditional expression is constant
#endif
#ifdef MSVC_BUILD
#define OPT(STR) else if (!_stricmp(name, STR))
#define OPT2(STR1, STR2) else if (!_stricmp(name, STR1) || !_stricmp(name, STR2))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#define OPT2(STR1, STR2) else if (!strcasecmp(name, STR1) || !strcasecmp(name, STR2))
#endif
#define COMPARE(STR1, STR2, STR3)                \
if ((atoi(STR1) > STR2) || (atoi(STR1) < STR3)) \
{                                                \
  return NI_RETCODE_PARAM_ERROR_OOR;             \
}

  if (0)
      ;
  OPT( NI_ENC_PARAM_BITRATE )
  {
    if (AV_CODEC_DEFAULT_BITRATE == p_params->bitrate)
    {
      if (atoi(value) > NI_MAX_BITRATE)
      {
        return NI_RETCODE_PARAM_ERROR_TOO_BIG;
      }
      if (atoi(value) < NI_MIN_BITRATE)
      {
        return NI_RETCODE_PARAM_ERROR_TOO_SMALL;
      }
      p_params->bitrate = atoi(value);
    }
  }
  OPT( NI_ENC_PARAM_RECONF_DEMO_MODE )
  {
    p_params->reconf_demo_mode = atoi(value); // NETINT_INTERNAL - currently only for internal testing
  }
  OPT( NI_ENC_PARAM_RECONF_FILE )
  {
    ni_retcode_t retval = ni_parse_reconf_file(value, p_params->reconf_hash);
    if (retval != NI_RETCODE_SUCCESS) // NETINT_INTERNAL - currently only for internal testing
    {
      return retval;
    }
  }
  OPT( NI_ENC_PARAM_ROI_DEMO_MODE )
  {
    // NETINT_INTERNAL - currently only for internal testing
    p_params->roi_demo_mode = atoi(value);
    if (1 != p_params->roi_demo_mode && 2 != p_params->roi_demo_mode)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
  }
  OPT( NI_ENC_PARAM_LOW_DELAY )
  {
    p_params->low_delay_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_PADDING )
  {
    p_params->padding = atoi(value);
  }
  OPT( NI_ENC_PARAM_GEN_HDRS )
  {
    p_params->generate_enc_hdrs = atoi(value);
  }
  OPT( NI_ENC_PARAM_USE_LOW_DELAY_POC_TYPE )
  {
    p_params->use_low_delay_poc_type = atoi(value);
  }
  OPT( NI_ENC_PARAM_FORCE_FRAME_TYPE )
  {
    p_params->force_frame_type = atoi(value);
  }
  OPT( NI_ENC_PARAM_PROFILE )
  {
    p_enc->profile = atoi(value);
  }
  OPT2( NI_ENC_PARAM_LEVEL_IDC, NI_ENC_PARAM_LEVEL )
  {
    /*! allow "5.1" or "51", both converted to integer 51 */
    /*! if level-idc specifies an obviously wrong value in either float or int,
    throw error consistently. Stronger level checking will be done in encoder_open() */
    if ( atof(value) < 10 )
    {
      p_enc->level_idc = (int)(10 * atof(value) + .5);
    }
    else if ( atoi(value) < 100 )
    {
      p_enc->level_idc = atoi(value);
    }
    else
    {
      b_error = true;
    }
  }
  OPT( NI_ENC_PARAM_HIGH_TIER )
  {
    p_enc->high_tier = atobool(value);
  }
  OPT2( NI_ENC_PARAM_LOG_LEVEL, NI_ENC_PARAM_LOG )
  {
    p_params->log = atoi(value);
    if (b_error)
    {
      b_error = false;
      p_params->log = ni_parse_name(value, g_xcoder_log_names, b_error) - 1;
    }
  }
  OPT( NI_ENC_PARAM_GOP_PRESET_IDX )
  {
    if ((atoi(value) > NI_MAX_GOP_PRESET_IDX) || (atoi(value) < NI_MIN_GOP_PRESET_IDX))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->gop_preset_index = atoi(value);
  }
  OPT( NI_ENC_PARAM_USE_RECOMMENDED_ENC_PARAMS )
  {
    if ((atoi(value) > NI_MAX_USE_RECOMMENDED_ENC_PARAMS) || (atoi(value) < NI_MIN_USE_RECOMMENDED_ENC_PARAMS))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->use_recommend_enc_params = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_RATE_CONTROL )
  {
    if ((atoi(value) > NI_MAX_BIN) || (atoi(value) < NI_MIN_BIN))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->rc.enable_rate_control = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_CU_LEVEL_RATE_CONTROL )
  {
    if ((atoi(value) > NI_MAX_BIN) || (atoi(value) < NI_MIN_BIN))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->rc.enable_cu_level_rate_control = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_HVS_QP )
  {
    p_enc->rc.enable_hvs_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_HVS_QP_SCALE )
  {
    p_enc->rc.enable_hvs_qp_scale = atoi(value);
  }
  OPT( NI_ENC_PARAM_HVS_QP_SCALE )
  {
    p_enc->rc.hvs_qp_scale = atoi(value);
  }
  OPT( NI_ENC_PARAM_MIN_QP )
  {
    if ((atoi(value) > NI_MAX_MIN_QP) || (atoi(value) < NI_MIN_MIN_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    else if (atoi(value) > p_enc->rc.max_qp)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rc.min_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_MAX_QP )
  {
    if ((atoi(value) > NI_MAX_MAX_QP) || (atoi(value) < NI_MIN_MAX_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    else if (atoi(value) < p_enc->rc.min_qp)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rc.max_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_MAX_DELTA_QP )
  {
    if ((atoi(value) > NI_MAX_MAX_DELTA_QP) || (atoi(value) < NI_MIN_MAX_DELTA_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rc.max_delta_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_RC_INIT_DELAY )
  {
    p_enc->rc.rc_init_delay = atoi(value);
  }
  OPT ( NI_ENC_PARAM_FORCED_HEADER_ENABLE )
  {
    p_enc->forced_header_enable = atoi(value);
  }
  OPT ( NI_ENC_PARAM_ROI_ENABLE )
  {
    p_enc->roi_enable = atoi(value);
  }
  OPT( NI_ENC_PARAM_CONF_WIN_TOP )
  {
    p_enc->conf_win_top = atoi(value);
  }
  OPT( NI_ENC_PARAM_CONF_WIN_BOTTOM )
  {
    p_enc->conf_win_bottom = atoi(value);
  }
  OPT( NI_ENC_PARAM_CONF_WIN_LEFT )
  {
    p_enc->conf_win_left = atoi(value);
  }
  OPT( NI_ENC_PARAM_CONF_WIN_RIGHT )
  {
    p_enc->conf_win_right = atoi(value);
  }
  OPT( NI_ENC_PARAM_INTRA_PERIOD )
  {
    if ((atoi(value) > NI_MAX_INTRA_PERIOD) || (atoi(value) < NI_MIN_INTRA_PERIOD))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    // check compatibility between GOP size and Intra period
    if ((p_enc->gop_preset_index == GOP_PRESET_IDX_IBPBP) && ((atoi(value)%2) != 0))
    {
      return NI_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE;
    }
    else if (((p_enc->gop_preset_index == GOP_PRESET_IDX_IBBBP) ||
              (p_enc->gop_preset_index == GOP_PRESET_IDX_IPPPP) ||
              (p_enc->gop_preset_index == GOP_PRESET_IDX_IBBBB)) &&
             ((atoi(value)%4) != 0))
    {
      return NI_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE;
    }
    else if ((p_enc->gop_preset_index == GOP_PRESET_IDX_RA_IB) && ((atoi(value)%8) != 0))
    {
      return NI_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE;
    }
    p_enc->intra_period = atoi(value);
  }
  OPT( NI_ENC_PARAM_TRANS_RATE )
  {
    if (atoi(value) > NI_MAX_BITRATE)
    {
      return NI_RETCODE_PARAM_ERROR_TOO_BIG;
    }
    if (atoi(value) < NI_MIN_BITRATE)
    {
      return NI_RETCODE_PARAM_ERROR_TOO_SMALL;
    }
    p_enc->rc.trans_rate = atoi(value);
  }
  OPT( NI_ENC_PARAM_FRAME_RATE )
  {
    if (atoi(value) <= 0 )
    {
      return NI_RETCODE_PARAM_ERROR_ZERO;
    }
    p_params->fps_number = atoi(value);
    p_params->fps_denominator = 1;
    p_enc->frame_rate = p_params->fps_number;
  }
  OPT( NI_ENC_PARAM_FRAME_RATE_DENOM )
  {
    p_params->fps_denominator = atoi(value);
    p_enc->frame_rate = p_params->fps_number / p_params->fps_denominator;
  }
  OPT( NI_ENC_PARAM_INTRA_QP )
  {
    if ((atoi(value) > NI_MAX_INTRA_QP) || (atoi(value) < NI_MIN_INTRA_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->rc.intra_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_FORCE_PIC_QP_DEMO_MODE )
  {
    if ((atoi(value) > NI_MAX_INTRA_QP) || (atoi(value) < NI_MIN_INTRA_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->force_pic_qp_demo_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_DECODING_REFRESH_TYPE )
  {
    if ((atoi(value) > NI_MAX_DECODING_REFRESH_TYPE) || (atoi(value) < NI_MIN_DECODING_REFRESH_TYPE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->decoding_refresh_type = atoi(value);
  }
// Rev. B: H.264 only parameters.
  OPT( NI_ENC_PARAM_ENABLE_8X8_TRANSFORM )
  {
    p_enc->enable_transform_8x8 = atoi(value);
  }
  OPT( NI_ENC_PARAM_AVC_SLICE_MODE )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->avc_slice_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_AVC_SLICE_ARG )
  {
    p_enc->avc_slice_arg = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENTROPY_CODING_MODE)
  {
    p_enc->entropy_coding_mode = atoi(value);
  }
// Rev. B: shared between HEVC and H.264
  OPT( NI_ENC_PARAM_INTRA_MB_REFRESH_MODE )
  {
    p_enc->intra_mb_refresh_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_INTRA_MB_REFRESH_ARG )
  {
    p_enc->intra_mb_refresh_arg = atoi(value);
  }
  OPT( NI_ENC_PARAM_INTRA_REFRESH_MODE )
  {
    p_enc->intra_mb_refresh_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_INTRA_REFRESH_ARG )
  {
    p_enc->intra_mb_refresh_arg = atoi(value);
  }
// TBD Rev. B: could be shared for HEVC and H.264
  OPT( NI_ENC_PARAM_ENABLE_MB_LEVEL_RC )
  {
    p_enc->rc.enable_mb_level_rc = atoi(value);
  }
  OPT( NI_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS)
  {
    if ((atoi(value) > 255) || (atoi(value) < 0))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->preferred_transfer_characteristics = atoi(value);
  }
  OPT( NI_ENC_PARAM_DOLBY_VISION_PROFILE )
  {
    if (atoi(value) != 0 && atoi(value) != 5)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->dolby_vision_profile = atoi(value);
  }
  OPT( NI_ENC_PARAM_HRD_ENABLE )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->hrd_enable = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_AUD )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->enable_aud = atoi(value);
  }
  OPT( NI_ENC_PARAM_CRF )
  {
    if (atoi(value) > NI_MAX_CRF || atoi(value) < NI_MIN_CRF)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->crf = atoi(value);
  }
  OPT( NI_ENC_PARAM_CBR )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->cbr = atoi(value);
  }
  OPT( NI_ENC_PARAM_FLUSH_GOP )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->ui32flushGop = atoi(value);
  }
  OPT( NI_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD )
  {
    if (atoi(value) > NI_MAX_INTRA_REFRESH_MIN_PERIOD ||
        atoi(value) < NI_MIN_INTRA_REFRESH_MIN_PERIOD)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->ui32minIntraRefreshCycle = atoi(value);
  }
  OPT( NI_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->long_term_ref_enable = atoi(value);
  }
  OPT( NI_ENC_PARAM_STRICT_TIMEOUT_MODE )
  {
    p_params->strict_timeout_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_LOSSLESS_ENABLE )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->lossless_enable = atoi(value);
  }

  else
  {
    return NI_RETCODE_PARAM_INVALID_NAME;
  }


#undef OPT
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "ni_encoder_params_set_value: exit, b_error=%d\n", b_error);

  return b_error ? NI_RETCODE_PARAM_INVALID_VALUE : NI_RETCODE_SUCCESS;
}

#undef atoi
#undef atof
#define atoi(p_str) ni_atoi(p_str, b_error)
#define atof(p_str) ni_atof(p_str, b_error)
#define atobool(p_str) (ni_atobool(p_str, b_error))

/*!******************************************************************************
 *  \brief  Set got parameter value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_encoder_gop_params_set_value(ni_encoder_params_t* p_params, const char* name, const char* value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_h265_encoder_params_t* p_enc = &p_params->hevc_enc_params;
  ni_custom_gop_params_t* p_gop = &p_enc->custom_gop_params;

  char nameBuf[64] = { 0 };

  ni_log(NI_LOG_TRACE, "ni_encoder_gop_params_set_value(): enter\n");

  if ( (!p_params) || (!name) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_gop_params_set_value(): Null pointer parameters passed\n");
    return NI_RETCODE_PARAM_INVALID_NAME;
  }

  // skip -- prefix if provided
  if ((name[0] == '-') && (name[1] == '-'))
  {
    name += 2;
  }

  // s/_/-/g
  if (strlen(name) + 1 < sizeof(nameBuf) && strchr(name, '_'))
  {
    char* c;
    nameBuf[0] = '\0';
    strncat(nameBuf, name, strlen(name));
    while ((c = strchr(nameBuf, '_')) != 0)
    {
      *c = '-';
    }

    name = nameBuf;
  }

  if (!strncmp(name, "no-", 3))
  {
    name += 3;
    value = !value || ni_atobool(value, b_error) ? "false" : "true";
  }
  else if (!strncmp(name, "no", 2))
  {
    name += 2;
    value = !value || ni_atobool(value, b_error) ? "false" : "true";
  }
  else if (!value)
  {
    value = "true";
  }
  else if (value[0] == '=')
  {
    value++;
  }

#if defined(_MSC_VER)
#pragma warning(disable : 4127) // conditional expression is constant
#endif
#ifdef MSVC_BUILD
#define OPT(STR) else if (!_stricmp(name, STR))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#endif
  if (0)
      ;
  OPT(NI_ENC_GOP_PARAMS_CUSTOM_GOP_SIZE)
  {
    if (atoi(value) > NI_MAX_GOP_SIZE)
    {
      return NI_RETCODE_PARAM_ERROR_TOO_BIG;
    }
    if (atoi(value) < NI_MIN_GOP_SIZE)
    {
      return NI_RETCODE_PARAM_ERROR_TOO_SMALL;
    }
    p_gop->custom_gop_size = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G0_PIC_TYPE)
  {
    p_gop->pic_param[0].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_POC_OFFSET)
  {
    p_gop->pic_param[0].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[0].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[0].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_POC_L0)
  {
    p_gop->pic_param[0].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_POC_L1)
  {
    p_gop->pic_param[0].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_TEMPORAL_ID)
  {
    p_gop->pic_param[0].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G1_PIC_TYPE)
  {
    p_gop->pic_param[1].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_POC_OFFSET)
  {
    p_gop->pic_param[1].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[1].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[1].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_POC_L0)
  {
    p_gop->pic_param[1].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_POC_L1)
  {
    p_gop->pic_param[1].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_TEMPORAL_ID)
  {
    p_gop->pic_param[1].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G2_PIC_TYPE)
  {
    p_gop->pic_param[2].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_POC_OFFSET)
  {
    p_gop->pic_param[2].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_PIC_QP)
  {
	COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[2].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[2].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_POC_L0)
  {
    p_gop->pic_param[2].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_POC_L1)
  {
    p_gop->pic_param[2].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_TEMPORAL_ID)
  {
    p_gop->pic_param[2].temporal_id = atoi(value);
  }
  
  OPT(NI_ENC_GOP_PARAMS_G3_PIC_TYPE)
  {
    p_gop->pic_param[3].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_POC_OFFSET)
  {
    p_gop->pic_param[3].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[3].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[3].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_POC_L0)
  {
    p_gop->pic_param[3].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_POC_L1)
  {
    p_gop->pic_param[3].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_TEMPORAL_ID)
  {
    p_gop->pic_param[3].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G4_PIC_TYPE)
  {
    p_gop->pic_param[4].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_POC_OFFSET)
  {
    p_gop->pic_param[4].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[4].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[4].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_POC_L0)
  {
    p_gop->pic_param[4].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_POC_L1)
  {
    p_gop->pic_param[4].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_TEMPORAL_ID)
  {
    p_gop->pic_param[4].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G5_PIC_TYPE)
  {
    p_gop->pic_param[5].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_POC_OFFSET)
  {
    p_gop->pic_param[5].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[5].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[5].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_POC_L0)
  {
    p_gop->pic_param[5].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_POC_L1)
  {
    p_gop->pic_param[5].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_TEMPORAL_ID)
  {
    p_gop->pic_param[5].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G6_PIC_TYPE)
  {
    p_gop->pic_param[6].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_POC_OFFSET)
  {
    p_gop->pic_param[6].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[6].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[6].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_POC_L0)
  {
    p_gop->pic_param[6].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_POC_L1)
  {
    p_gop->pic_param[6].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_TEMPORAL_ID)
  {
    p_gop->pic_param[6].temporal_id = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G7_PIC_TYPE)
  {
    p_gop->pic_param[7].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_POC_OFFSET)
  {
    p_gop->pic_param[7].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_PIC_QP)
  {
    COMPARE(value, NI_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[7].pic_qp = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[7].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_POC_L0)
  {
    p_gop->pic_param[7].ref_poc_L0 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_POC_L1)
  {
    p_gop->pic_param[7].ref_poc_L1 = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_TEMPORAL_ID)
  {
    p_gop->pic_param[7].temporal_id = atoi(value);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_encoder_gop_params_set_value(): Invalid parameter name passed\n");
    return NI_RETCODE_PARAM_INVALID_NAME;
  }

#undef OPT
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "ni_encoder_gop_params_set_value(): exit, b_error=%d\n", b_error);

  return (b_error ? NI_RETCODE_PARAM_INVALID_VALUE : NI_RETCODE_SUCCESS);
}

/*!*****************************************************************************
 *  \brief  Get GOP's max number of reorder frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *
 *  \return max number of reorder frames of the GOP, -1 on error
 ******************************************************************************/
int ni_get_num_reorder_of_gop_structure(ni_encoder_params_t * p_params)
{
  int gopPreset = p_params->hevc_enc_params.gop_preset_index;
  int reorder = 0;
  int i;
  ni_h265_encoder_params_t* p_enc = NULL;

  if (! p_params)
  {
    ni_log(NI_LOG_ERROR, "ni_get_num_reorder_of_gop_structure: NULL input!\n");
    return -1;
  }
  p_enc = &p_params->hevc_enc_params;

  // low delay gopPreset (1, 2, 3, 6, 7, 9) has reorder of 0
  if (GOP_PRESET_IDX_ALL_I == gopPreset || GOP_PRESET_IDX_IPP == gopPreset ||
      GOP_PRESET_IDX_IBBB == gopPreset || GOP_PRESET_IDX_IPPPP == gopPreset ||
      GOP_PRESET_IDX_IBBBB == gopPreset || GOP_PRESET_IDX_SP == gopPreset)
  {
    reorder = 0;
  }
  else if (GOP_PRESET_IDX_IBPBP == gopPreset)
  {
    reorder = 1;
  }
  else if (GOP_PRESET_IDX_IBBBP == gopPreset)
  {
    reorder = 3;
  }
  else if (GOP_PRESET_IDX_RA_IB == gopPreset)
  {
    reorder = 7;
  }
  else if (GOP_PRESET_IDX_CUSTOM == gopPreset)
  {
    for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
    {
      if (abs(p_enc->custom_gop_params.pic_param[i].poc_offset - i - 1) >
          reorder)
      {
        reorder = abs(p_enc->custom_gop_params.pic_param[i].poc_offset - i - 1);
      }
    }
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ni_get_num_reorder_of_gop_structure: gopPresetIdx="
           "%d not supported\n", gopPreset);
  }
  return reorder;
}

/*!*****************************************************************************
 *  \brief  Get GOP's number of reference frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_encoder_params_t
 *
 *  \return number of reference frames of the GOP
 ******************************************************************************/
int ni_get_num_ref_frame_of_gop_structure(ni_encoder_params_t * p_params)
{
  int gopPreset = p_params->hevc_enc_params.gop_preset_index;
  int ref_frames = 0;
  int i;
  ni_h265_encoder_params_t* p_enc = NULL;

  if (! p_params)
  {
    ni_log(NI_LOG_ERROR, "ni_get_num_ref_frame_of_gop_structure: NULL input!\n");
    return -1;
  }
  p_enc = &p_params->hevc_enc_params;

  if (GOP_PRESET_IDX_ALL_I == gopPreset)
  {
    ref_frames = 0;
  }
  else if (gopPreset >= GOP_PRESET_IDX_IPP && gopPreset <= GOP_PRESET_IDX_RA_IB)
  {
    ref_frames = 2;
  }
  else if (GOP_PRESET_IDX_SP == gopPreset)
  {
    ref_frames = 1;
  }
  else if (GOP_PRESET_IDX_CUSTOM == gopPreset)
  {
    for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
    {
      if (p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0 > ref_frames)
      {
        ref_frames = p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0;
      }
    }
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ni_get_num_ref_frame_of_gop_structure: gopPresetIdx="
           "%d not supported\n", gopPreset);
  }

  return ref_frames;
}
