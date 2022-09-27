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
*  \file   ni_device_api_logan.c
*
*  \brief  Main NETINT device API file
*           provides the ability to communicate with NI T-408 type hardware
*           transcoder devices
*
*******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__ || __APPLE__
#define _GNU_SOURCE //O_DIRECT is Linux-specific.  One must define _GNU_SOURCE to obtain its definitions
#if __linux__
#include <sys/types.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#endif

#include "ni_device_api_logan.h"
#include "ni_device_api_priv_logan.h"
#include "ni_nvme_logan.h"
#include "ni_util_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_rsrc_priv_logan.h"


const char * const g_logan_xcoder_preset_names[NI_LOGAN_XCODER_PRESET_NAMES_ARRAY_LEN] = {
  NI_LOGAN_XCODER_PRESET_NAME_DEFAULT, NI_LOGAN_XCODER_PRESET_NAME_CUSTOM, 0
};

const char * const g_logan_xcoder_log_names[NI_LOGAN_XCODER_LOG_NAMES_ARRAY_LEN] = {
  NI_LOGAN_XCODER_LOG_NAME_NONE, NI_LOGAN_XCODER_LOG_NAME_ERROR, NI_LOGAN_XCODER_LOG_NAME_WARN,
  NI_LOGAN_XCODER_LOG_NAME_INFO, NI_LOGAN_XCODER_LOG_NAME_DEBUG, NI_LOGAN_XCODER_LOG_NAME_FULL, 0
};

#if __linux__ || __APPLE__
static struct stat g_nvme_stat = { 0 };
#endif

/*!******************************************************************************
 *  \brief  Allocates and initializes a new ni_logan_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 *******************************************************************************/
ni_logan_session_context_t * ni_logan_device_session_context_alloc_init(void)
{
  ni_logan_session_context_t *p_ctx = NULL;

  p_ctx = malloc(sizeof(ni_logan_session_context_t));
  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for session context\n",
           NI_ERRNO);
  }
  else
  {
    p_ctx->needs_dealoc = 1;
    ni_logan_device_session_context_init(p_ctx);
  }

  return p_ctx;
}

 /*!******************************************************************************
 *  \brief  Frees previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 *******************************************************************************/
void ni_logan_device_session_context_free(ni_logan_session_context_t *p_ctx)
{
  if (p_ctx && p_ctx->needs_dealoc)
  {
    ni_logan_device_session_context_clear(p_ctx);
    free(p_ctx);
  }
}

/*!******************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 *
 *******************************************************************************/
void ni_logan_device_session_context_init(ni_logan_session_context_t *p_ctx)
{
  if (!p_ctx)
  {
    return;
  }

  memset(p_ctx, 0, sizeof(ni_logan_session_context_t));

  // Init the max IO size to be invalid
  p_ctx->max_nvme_io_size = NI_INVALID_IO_SIZE;
  p_ctx->session_id = NI_LOGAN_INVALID_SESSION_ID;
  p_ctx->session_run_state = LOGAN_SESSION_RUN_STATE_NORMAL;
  p_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
  p_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
  p_ctx->hw_id = NI_INVALID_HWID;
  p_ctx->dev_xcoder[0] = '\0';
  p_ctx->event_handle = NI_INVALID_EVENT_HANDLE;
  p_ctx->thread_event_handle = NI_INVALID_EVENT_HANDLE;
  p_ctx->keep_alive_timeout = NI_LOGAN_DEFAULT_KEEP_ALIVE_TIMEOUT;
  ni_logan_pthread_mutex_init(&p_ctx->mutex);

  strncat(p_ctx->dev_xcoder, BEST_MODEL_LOAD_STR, strlen(BEST_MODEL_LOAD_STR));
#ifdef MEASURE_LATENCY
  p_ctx->frame_time_q = ni_logan_lat_meas_q_create(2000);
  p_ctx->prev_read_frame_time = 0;
#endif
}

/*!******************************************************************************
 *  \brief  Clear already allocated session context to all zeros
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_logan_session_context_t struct
 *
 *
 *******************************************************************************/
 void ni_logan_device_session_context_clear(ni_logan_session_context_t *p_ctx)
 {
   ni_logan_pthread_mutex_destroy(&p_ctx->mutex);
   memset(p_ctx, 0, sizeof(ni_logan_session_context_t));
 }

/*!******************************************************************************
 *  \brief  Create event and returnes event handle if successful
 *
 *  \return On success returns a event handle
 *          On failure returns NI_INVALID_EVENT_HANDLE
 *******************************************************************************/
ni_event_handle_t ni_logan_create_event(void)
{
#ifdef _WIN32
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
  DWORD retval = 0;

  // Input-0 determines whether the returned handle can be inherited by the child process.
  //         If lpEventAttributes is NULL, this handle cannot be inherited.
  // Input-1 specifies whether the event object is created to be restored manually or automatically.
  //         If set to FALSE, when a thread waits for an event signal,
  //         the system automatically restores the event state to a non-signaled state.
  // Input-2 specifies the initial state of the event object.
  //         If TRUE, the initial state is signaled;Otherwise, no signal state.
  // Input-3 If the lpName is NULL, a nameless event object is created.
  event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (event_handle == NI_INVALID_EVENT_HANDLE)
  {
    retval = NI_ERRNO;
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
void ni_logan_close_event(ni_event_handle_t event_handle)
{
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    ni_log(NI_LOG_TRACE, "Warning %s: null parameter passed %" PRIx64 "\n",
           __FUNCTION__, (int64_t)event_handle);
    return;
  }

#ifdef _WIN32
  BOOL retval;
  ni_log(NI_LOG_TRACE, "%s(): closing %p\n", __FUNCTION__, event_handle);

  retval = CloseHandle(event_handle);
  if (FALSE == retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s(): closing event_handle %p failed\n",
           __FUNCTION__, NI_ERRNO, event_handle);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): device %p closed successfuly\n",
           __FUNCTION__, event_handle);
  }
#elif __linux__ || __APPLE__
  int err = 0;
  ni_log(NI_LOG_TRACE, "%s(): closing %d\n", __FUNCTION__, event_handle);
  err = close(event_handle);
  if (err)
  {
    switch (err)
    {
      case EBADF:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): failed error EBADF\n", __FUNCTION__);
        break;
      case EINTR:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): error EINTR\n", __FUNCTION__);
        break;
      case EIO:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): error EIO\n", __FUNCTION__);
        break;
      default:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): unknoen error %d\n", __FUNCTION__, err);
    }

  }
#endif //__linux__

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);
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
ni_device_handle_t ni_logan_device_open(const char * p_dev, uint32_t * p_max_io_size_out)
{
  if (!p_dev || !p_max_io_size_out)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
#ifdef _WIN32
    return NI_INVALID_DEVICE_HANDLE;
#else
    return NI_LOGAN_RETCODE_INVALID_PARAM;
#endif
  }

  ni_log(NI_LOG_TRACE, "%s: opening %s\n", __FUNCTION__, p_dev);

#ifdef _WIN32
  DWORD retval = 0;

  *p_max_io_size_out =  0;

  HANDLE device_handle = CreateFile( p_dev,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_NO_BUFFERING,
                NULL
                            );

  if (INVALID_HANDLE_VALUE == device_handle)
  {
    retval = NI_ERRNO;
    ni_log(NI_LOG_TRACE, "Failed to open %s, retval %d \n", p_dev, retval);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "Found NVME Controller at %s \n", p_dev);
  }
  return device_handle;

#elif __linux__ || __APPLE__

  int retval = -1;
  ni_device_handle_t fd = NI_INVALID_DEVICE_HANDLE;

  if (*p_max_io_size_out == NI_INVALID_IO_SIZE)
  {
#if __linux__
    *p_max_io_size_out = ni_logan_get_kernel_max_io_size(p_dev);
#elif __APPLE__
    *p_max_io_size_out = MAX_IO_TRANSFER_SIZE;
#endif
  }

  ni_log(NI_LOG_TRACE, "%s: opening reg i/o %s\n", __FUNCTION__, p_dev);
  //O_SYNC is added to ensure that data is written to the card when the pread/pwrite function returns
#if __linux__ 
  //O_DIRECT is added to ensure that data can be sent directly to the card instead of to cache memory
  fd = open(p_dev, O_RDWR | O_SYNC | O_DIRECT);
#elif __APPLE__
	//O_DIRECT isn't available, so instead we use F_NOCACHE below
  fd = open(p_dev, O_RDWR | O_SYNC);
#endif

  if (fd < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: open() failed on %s\n", NI_ERRNO, p_dev);
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed!\n", __FUNCTION__);
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }
#if __APPLE__
  //F_NOCACHE is set to ensure that data can be sent directly to the card instead of to cache memory
  retval = fcntl(fd, F_NOCACHE, 1);
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR: fnctl() failed on %s\n", p_dev);
      ni_log(NI_LOG_ERROR, "ERROR: ni_device_open() failed!\n");
      close(fd);
      fd = NI_INVALID_DEVICE_HANDLE;
      LRETURN;
  }
#endif
  retval = fstat(fd, &g_nvme_stat);
  if (retval < 0)
  {
    ni_log(NI_LOG_TRACE, "ERROR: fstat() failed on %s\n", p_dev);
    ni_log(NI_LOG_TRACE, "ERROR: %s() failed!\n", __FUNCTION__);
    close(fd);
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }

  if (!S_ISCHR(g_nvme_stat.st_mode) && !S_ISBLK(g_nvme_stat.st_mode))
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s is not a block or character device\n", p_dev);
    ni_log(NI_LOG_TRACE, "ERROR: %s() failed!\n", __FUNCTION__);
    close(fd);
    fd = NI_INVALID_DEVICE_HANDLE;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "%s: success, fd=%d\n", __FUNCTION__, fd);

  END:

  return (fd);
#endif //__linux__
}

/*!******************************************************************************
 *  \brief  Closes device and releases resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_logan_device_open()
 *
 *  \return NONE
 *
 *******************************************************************************/
void ni_logan_device_close(ni_device_handle_t device_handle)
{
  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if ( NI_INVALID_DEVICE_HANDLE == device_handle )
  {
    ni_log(NI_LOG_TRACE, "ERROR %s: null parameter passed\n", __FUNCTION__);
    return;
  }

#ifdef _WIN32
  BOOL retval;

  ni_log(NI_LOG_TRACE, "%s(): closing %p\n", __FUNCTION__, device_handle);

  retval = CloseHandle(device_handle);
  if (FALSE == retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): closing device device_handle %p failed, "
           "error: %d\n", __FUNCTION__, device_handle,
           NI_ERRNO);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): device %p closed successfuly\n",
           __FUNCTION__, device_handle);
  }
#elif __linux__ || __APPLE__
  int err = 0;
  ni_log(NI_LOG_TRACE, "%s(): closing %d\n", __FUNCTION__, device_handle);
  err = close(device_handle);
  if (err)
  {
    switch (err)
    {
      case EBADF:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): failed error EBADF\n", __FUNCTION__);
        break;
      case EINTR:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): error EINTR\n", __FUNCTION__);
        break;
      case EIO:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): error EIO\n", __FUNCTION__);
        break;
      default:
        ni_log(NI_LOG_TRACE, "ERROR: %s(): unknoen error %d\n", __FUNCTION__, err);
    }
  }
#endif //__linux__

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);
}


/*!******************************************************************************
 *  \brief  Queries device and returns device capability structure
 *
 *  \param[in] device_handle Device handle obtained by calling ni_logan_device_open()
 *  \param[in] p_cap  Pointer to a caller allocated ni_logan_device_capability_t struct
 *  \return On success
 *                     NI_LOGAN_RETCODE_SUCCESS
 *          On failure
                       NI_LOGAN_RETCODE_INVALID_PARAM
                       NI_LOGAN_RETCODE_ERROR_MEM_ALOC
                       NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *******************************************************************************/
ni_logan_retcode_t ni_logan_device_capability_query(ni_device_handle_t device_handle,
                                        ni_logan_device_capability_t* p_cap)
{
  ni_logan_nvme_result_t nvme_result = 0;
  ni_logan_nvme_command_t cmd = { 0 };
  void * p_buffer = NULL;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if ( (NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_cap) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): passed parameters are null!, return\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ))
  {
    ni_log(NI_LOG_TRACE, "ERROR %d: %s(): Cannot allocate buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ);

#ifdef _WIN32
  event_handle = ni_logan_create_event();
  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN;
  }
#endif

  uint32_t ui32LBA = IDENTIFY_DEVICE_R;
  if (ni_logan_nvme_send_read_cmd(device_handle, event_handle, p_buffer, NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
  {
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  ni_logan_populate_device_capability_struct(p_cap, p_buffer);

  END:

  ni_logan_aligned_free(p_buffer);
#ifdef _WIN32
  ni_logan_close_event(event_handle);
#endif

  ni_log(NI_LOG_TRACE, "%s(): retval: %d\n", __FUNCTION__, retval);

  return retval;
}

/*!******************************************************************************
 *  \brief  Opens a new device session depending on the device_type parameter
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER opens encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] p_config     Pointer to a caller allocated
 *                                               ni_logan_session_config_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_logan_retcode_t ni_logan_device_session_open(ni_logan_session_context_t *p_ctx,
                                    ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_device_pool_t *p_device_pool = NULL;
  ni_logan_nvme_result_t nvme_result = {0};
  ni_logan_device_context_t *p_device_context = NULL;
  ni_logan_device_info_t *p_dev_info = NULL;
  ni_logan_session_context_t p_session_context = {0};
  ni_logan_device_info_t dev_info = { 0 };
  /*! resource management context */
  ni_logan_device_context_t *rsrc_ctx = NULL;
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
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return  NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  p_ctx->p_hdr_buf = NULL;
  p_ctx->hdr_buf_size = 0;
  p_ctx->roi_side_data_size = p_ctx->nb_rois = 0;
  p_ctx->av_rois = NULL;
  p_ctx->avc_roi_map = NULL;
  p_ctx->hevc_sub_ctu_roi_buf = NULL;
  p_ctx->hevc_roi_map = NULL;
  p_ctx->p_master_display_meta_data = NULL;
  p_ctx->enc_change_params = NULL;

  handle = p_ctx->device_handle;
  handle1 = p_ctx->blk_io_handle;

  if (p_ctx->session_id != NI_LOGAN_INVALID_SESSION_ID || p_ctx->ready_to_close)
  {
    ni_log(NI_LOG_ERROR, "ERROR: trying to overwrite existing session\n");
    retval =  NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }
#ifdef __linux__
  if(p_ctx->set_high_priority == 1)
    ni_logan_change_priority();
#endif
  p_device_pool = ni_logan_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Error calling ni_logan_rsrc_get_device_pool()\n");
    retval =  NI_LOGAN_RETCODE_ERROR_GET_DEVICE_POOL;
    LRETURN;
  }

  // User did not pass in any handle, so we create it for them
  if ((handle1 == NI_INVALID_DEVICE_HANDLE) && (handle == NI_INVALID_DEVICE_HANDLE))
  {
    if (p_ctx->hw_id >=0) // User selected the encoder/ decoder number
    {
      if ((rsrc_ctx = ni_logan_rsrc_allocate_simple_direct(device_type, p_ctx->hw_id)) == NULL)
      {
        ni_log(NI_LOG_INFO, "Error XCoder resource allocation: inst %d \n", p_ctx->hw_id);
        retval = NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE;
        LRETURN;
      }
      ni_log(NI_LOG_TRACE, "device %p\n", rsrc_ctx);
      // Now the device name is in the rsrc_ctx, we open this device to get the file handles

#ifdef _WIN32
      //This is windows version!!!
      if ((handle = ni_logan_device_open(rsrc_ctx->p_device_info->dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE)
      {
        ni_logan_rsrc_free_device_context(rsrc_ctx);
        retval = NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE;
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

        ni_logan_rsrc_free_device_context(rsrc_ctx);
      }
#elif __linux__ || __APPLE__
      //The original design (code below) is to open char and block device file separately.
      //And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened.
      //For compatibility, and to avoid errors, open the block device twice.
      if (((handle = ni_logan_device_open(rsrc_ctx->p_device_info->blk_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_logan_device_open(rsrc_ctx->p_device_info->blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
      {
        ni_logan_rsrc_free_device_context(rsrc_ctx);
        retval = NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE;
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

        ni_logan_rsrc_free_device_context(rsrc_ctx);
      }
#endif
    }
    // we use this to support the best model load & best instance
    else if ((0 == strcmp(p_ctx->dev_xcoder, BEST_MODEL_LOAD_STR)) ||
             (0 == strcmp(p_ctx->dev_xcoder, BEST_DEVICE_INST_STR)))
    {
      if (ni_logan_rsrc_lock_and_open(device_type, &lock) != NI_LOGAN_RETCODE_SUCCESS)
      {
        retval = NI_LOGAN_RETCODE_ERROR_LOCK_DOWN_DEVICE;
        LRETURN;
      }

      // Then we need to query through all the board to confirm the least model load/ instance load board
      if ((NI_LOGAN_DEVICE_TYPE_DECODER == device_type) || (NI_LOGAN_DEVICE_TYPE_ENCODER == device_type))
      {
        num_coders = p_device_pool->p_device_queue->decoders_cnt;
      }
      else
      {
        retval = NI_LOGAN_RETCODE_INVALID_PARAM;
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
        LRETURN;
      }

      int tmp_id = -1;
      for (i = 0; i < num_coders; i++)
      {
        if (NI_LOGAN_DEVICE_TYPE_DECODER == device_type)
        {
          tmp_id = p_device_pool->p_device_queue->decoders[i];
        }
        else if (NI_LOGAN_DEVICE_TYPE_ENCODER == device_type)
        {
          tmp_id = p_device_pool->p_device_queue->encoders[i];
        }
        p_device_context = ni_logan_rsrc_get_device_context(device_type, tmp_id);
        if (! p_device_context)
        {
          ni_log(NI_LOG_INFO, "Error getting device type %d id %d",
                 device_type, tmp_id);
          continue;
        }

        //Code is included in the for loop. In the loop, the device is just opened once, and it will be closed once too.
        p_session_context.blk_io_handle = ni_logan_device_open(p_device_context->p_device_info->blk_name, &dummy_io_size);
        p_session_context.device_handle = p_session_context.blk_io_handle;

        if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
        {
          ni_logan_rsrc_free_device_context(p_device_context);
          ni_log(NI_LOG_TRACE, "Error open device");
          continue;
        }

#ifdef _WIN32
        p_session_context.event_handle = ni_logan_create_event();
        if (NI_INVALID_EVENT_HANDLE == p_session_context.event_handle)
        {
          ni_logan_rsrc_free_device_context(p_device_context);
          ni_logan_device_close(p_session_context.device_handle);
          ni_log(NI_LOG_TRACE, "Error create envet");
          continue;
        }
#endif

        char str_fw_API_ver[3];
        int fw_API_ver;
        memcpy(str_fw_API_ver,
               &(p_device_context->p_device_info->fw_rev[NI_LOGAN_XCODER_VER_SZ + 1 + NI_LOGAN_XCODER_API_FLAVOR_SZ]),
               NI_LOGAN_XCODER_API_VER_SZ);
        str_fw_API_ver[NI_LOGAN_XCODER_API_VER_SZ] = '\0';
        fw_API_ver = atoi(str_fw_API_ver);
        ni_log(NI_LOG_TRACE,"Current FW API version is: %d\n", fw_API_ver);

        p_session_context.hw_id = p_device_context->p_device_info->hw_id;
        rc = ni_logan_device_session_query(&p_session_context, device_type);
        if (NI_INVALID_DEVICE_HANDLE != p_session_context.device_handle)
        {
          ni_logan_device_close(p_session_context.device_handle);
        }

#ifdef _WIN32
        if (NI_INVALID_EVENT_HANDLE != p_session_context.event_handle)
        {
          ni_logan_close_event(p_session_context.event_handle);
        }
#endif
        if (NI_LOGAN_RETCODE_SUCCESS != rc)
        {
          ni_log(NI_LOG_TRACE, "Error query %s %s.%d\n",
                  NI_LOGAN_DEVICE_TYPE_DECODER == device_type ? "decoder" : "encoder",
                  p_device_context->p_device_info->dev_name, p_device_context->p_device_info->hw_id);
          ni_logan_rsrc_free_device_context(p_device_context);
          continue;
        }
        ni_logan_rsrc_update_record(p_device_context, &p_session_context);
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
            memcpy(&dev_info, p_dev_info, sizeof(ni_logan_device_info_t));
          }
        }
        else
        {
          // here we select the best instance
          if (((guid == NI_INVALID_HWID) || (p_dev_info->active_num_inst < least_instance) ||
               (p_dev_info->active_num_inst == least_instance)) &&
              (p_dev_info->active_num_inst < p_dev_info->max_instance_cnt))
          {
            guid = tmp_id;
            least_instance = p_dev_info->active_num_inst;
            memcpy(&dev_info, p_dev_info, sizeof(ni_logan_device_info_t));
          }
        }
        ni_logan_rsrc_free_device_context(p_device_context);
      }

      // Now we have the device info that has the least model load/least instance of the FW
      // we open this device and assign the FD
#ifdef _WIN32
      //This is windows version!!!
      if ((handle = ni_logan_device_open(dev_info.dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE)
      {
        retval = NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE;
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
        LRETURN;
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
#elif __linux__ || __APPLE__
      //The original design (code below) is to open char and block device file separately.
      //And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened.
      //For compatibility, and to avoid errors, open the block device twice.
      if (((handle = ni_logan_device_open(dev_info.blk_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_logan_device_open(dev_info.blk_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
      {
        retval = NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE;
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
        LRETURN;
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
      retval = NI_LOGAN_RETCODE_ERROR_INVALID_ALLOCATION_METHOD;
      LRETURN;
    }

  }
  // user passed in the handle, but one of them is invalid, this is error case so we return error
  else if ((handle1 == NI_INVALID_DEVICE_HANDLE) || (handle == NI_INVALID_DEVICE_HANDLE))
  {
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN;
  }
  // User passed in both handles, so we do not need to allocate for it
  else
  {
    user_handles = true;
  }

#ifdef _WIN32
  ni_log(NI_LOG_TRACE, "Finish open the session dev:%s blk:%s guid:%d handle:%p handle1:%p\n",
         p_ctx->dev_xcoder_name, p_ctx->blk_xcoder_name, p_ctx->hw_id,
         p_ctx->device_handle, p_ctx->blk_io_handle);
#else
  ni_log(NI_LOG_TRACE, "Finish open the session dev:%s blk:%s guid:%d handle:%d handle1:%d\n",
         p_ctx->dev_xcoder_name, p_ctx->blk_xcoder_name, p_ctx->hw_id,
         p_ctx->device_handle, p_ctx->blk_io_handle);
#endif

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    {
      for (i = 0; i< NI_LOGAN_MAX_SESSION_OPEN_RETRIES; i++)  // 20 retries
      {
        retval = ni_logan_decoder_session_open(p_ctx);
        if (NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY == retval)
        {
          ni_logan_decoder_session_close(p_ctx, 0);
          ni_log(NI_LOG_DEBUG, "Encoder vpu recovery retry count: %d", i);
          ni_logan_usleep(NI_LOGAN_SESSION_OPEN_RETRY_INTERVAL_US);  // 200 us
          continue;
        }
        else
        {
          break;
        }
      }
      if (user_handles != true)
      {
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      // send keep alive signal thread
      if (NI_LOGAN_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      for (i = 0; i< NI_LOGAN_MAX_SESSION_OPEN_RETRIES; i++)  // 20 retries
      {
        retval = ni_logan_encoder_session_open(p_ctx);
        if (NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY == retval)
        {
          ni_logan_encoder_session_close(p_ctx, 0);
          ni_log(NI_LOG_DEBUG, "Encoder vpu recovery retry count: %d", i);
          ni_logan_usleep(NI_LOGAN_SESSION_OPEN_RETRY_INTERVAL_US);  // 200 us
          continue;
        }
        else
        {
          break;
        }
      }
      if (user_handles != true)
      {
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      // send keep alive signal thread
      if (NI_LOGAN_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_UPLOAD:
    {
      retval = ni_logan_uploader_session_open(p_ctx);
      if (user_handles != true)
      {
        if( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      // send keep alive signal thread
      if (NI_LOGAN_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    default:
    {
      if (user_handles != true)
      {
        if ( ni_logan_rsrc_unlock(device_type, lock) != NI_LOGAN_RETCODE_SUCCESS)
        {
          retval = NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "Unrecognized device type: %d", device_type);
      LRETURN;
    }
  }

  p_ctx->keep_alive_thread_args = (ni_logan_thread_arg_struct_t *) malloc (sizeof(ni_logan_thread_arg_struct_t));
  if (!p_ctx->keep_alive_thread_args)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: thread_args allocation failed!\n", NI_ERRNO);
    ni_logan_device_session_close(p_ctx,0,device_type);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
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

  if ( 0 != ni_logan_pthread_create(&p_ctx->keep_alive_thread, NULL, ni_logan_session_keep_alive_thread,
                              (void *)p_ctx->keep_alive_thread_args) )
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: failed to create keep alive thread\n", NI_ERRNO);
    retval = NI_LOGAN_RETCODE_FAILURE;
    p_ctx->keep_alive_thread = (ni_pthread_t){ 0 };
    free(p_ctx->keep_alive_thread_args);
    p_ctx->keep_alive_thread_args = NULL;
    ni_logan_device_session_close(p_ctx,0,device_type);
  }

  END:

  if (p_device_pool)
  {
    ni_logan_rsrc_free_device_pool(p_device_pool);
    p_device_pool = NULL;
  }

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Closes device session that was previously opened by calling
 *          ni_logan_device_session_open()
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER closes encoding session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] eos_recieved Flag indicating if End Of Stream indicator was recieved
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_logan_retcode_t ni_logan_device_session_close(ni_logan_session_context_t* p_ctx,
                                     int eos_recieved,
                                     ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null! return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

#ifdef _WIN32
  if (p_ctx->keep_alive_thread.handle)
#else
  if (p_ctx->keep_alive_thread)
#endif
  {
    p_ctx->keep_alive_thread_args->close_thread = true;
    ni_logan_pthread_join(p_ctx->keep_alive_thread, NULL);
    if (p_ctx->keep_alive_thread_args)
    {
      free(p_ctx->keep_alive_thread_args);
    }
    p_ctx->keep_alive_thread = (ni_pthread_t){ 0 };
    p_ctx->keep_alive_thread_args = NULL;
  }
  else
  {
     ni_log(NI_LOG_TRACE, "Cancel invalid keep alive thread: %d",
            p_ctx->session_id);
  }

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    case NI_LOGAN_DEVICE_TYPE_UPLOAD:
    {
      retval = ni_logan_decoder_session_close(p_ctx, eos_recieved);
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      retval = ni_logan_encoder_session_close(p_ctx, eos_recieved);
      break;
    }
#if 0
    case NI_LOGAN_DEVICE_TYPE_UPLOAD:
    {
        retval = ni_logan_uploader_session_close(p_ctx, eos_recieved);
        break;
    }
#endif
    default:
    {
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

#ifdef _WIN32
  if (p_ctx->event_handle != NI_INVALID_EVENT_HANDLE)
  {
    ni_logan_close_event(p_ctx->event_handle);
    p_ctx->event_handle = NI_INVALID_EVENT_HANDLE;
  }

  if (p_ctx->thread_event_handle != NI_INVALID_EVENT_HANDLE)
  {
    ni_logan_close_event(p_ctx->thread_event_handle);
    p_ctx->thread_event_handle = NI_INVALID_EVENT_HANDLE;
  }

  p_ctx->session_id = NI_LOGAN_INVALID_SESSION_ID; /*!* need set invalid after closed.
                                               May cause open invalid parameters. */
#endif

  free(p_ctx->p_hdr_buf);
  p_ctx->p_hdr_buf = NULL;
  p_ctx->hdr_buf_size = 0;

  free(p_ctx->av_rois);
  free(p_ctx->avc_roi_map);
  free(p_ctx->hevc_sub_ctu_roi_buf);
  free(p_ctx->hevc_roi_map);
  free(p_ctx->p_master_display_meta_data);
  p_ctx->av_rois = NULL;
  p_ctx->avc_roi_map = NULL;
  p_ctx->hevc_sub_ctu_roi_buf = NULL;
  p_ctx->hevc_roi_map = NULL;
  p_ctx->p_master_display_meta_data = NULL;
  p_ctx->roi_side_data_size = p_ctx->nb_rois = 0;

  free(p_ctx->enc_change_params);
  p_ctx->enc_change_params = NULL;

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Sends a flush command to the device
 *          ni_logan_device_session_open()
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER sends flush command to decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER sends flush command to decoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_logan_retcode_t ni_logan_device_session_flush(ni_logan_session_context_t* p_ctx,
                                     ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    {
      retval = ni_logan_decoder_session_flush(p_ctx);
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      retval = ni_logan_encoder_session_flush(p_ctx);
      break;
    }
    default:
    {
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Save a stream's headers in a decoder session that can be used later
 *          for continuous decoding from the same source.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] hdr_data     Pointer to header data
 *  \param[in] hdr_size     Size of header data in bytes
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_logan_retcode_t ni_logan_device_dec_session_save_hdrs(ni_logan_session_context_t *p_ctx,
                                                    uint8_t *hdr_data, uint8_t hdr_size)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (! p_ctx || ! hdr_data)
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_logan_device_dec_session_save_hdrs para null, "
           "return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  if (p_ctx->p_hdr_buf && p_ctx->hdr_buf_size == hdr_size &&
      0 == memcmp(p_ctx->p_hdr_buf, hdr_data, hdr_size))
  {
    // no change from the saved headers, success !
    LRETURN;
  }

  // update the saved header data
  free(p_ctx->p_hdr_buf);
  p_ctx->hdr_buf_size = 0;
  p_ctx->p_hdr_buf = malloc(hdr_size);
  if (p_ctx->p_hdr_buf)
  {
    memcpy(p_ctx->p_hdr_buf, hdr_data, hdr_size);
    p_ctx->hdr_buf_size = hdr_size;
    ni_log(NI_LOG_TRACE, "ni_logan_device_dec_session_save_hdrs saved hdr size %u\n",
           p_ctx->hdr_buf_size);
  }
  else
  {
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    ni_log(NI_LOG_ERROR, "ERROR: ni_logan_device_dec_session_save_hdrs no memory.\n");
  }

  END:

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Flush a decoder session to get ready to continue decoding.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
LIB_API ni_logan_retcode_t ni_logan_device_dec_session_flush(ni_logan_session_context_t *p_ctx)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  uint8_t *p_tmp_data = NULL;
  uint8_t tmp_data_size = 0;

  if (! p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_logan_device_dec_session_flush ctx null, "
           "return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  // save the stream header data for the new session to be opened
  if (p_ctx->p_hdr_buf && p_ctx->hdr_buf_size)
  {
    p_tmp_data = malloc(p_ctx->hdr_buf_size);
    if (p_tmp_data)
    {
      memcpy(p_tmp_data, p_ctx->p_hdr_buf, p_ctx->hdr_buf_size);
      tmp_data_size = p_ctx->hdr_buf_size;
    }
    else
    {
      return NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    }
  }

  // close the current session and open a new one
  ni_logan_device_session_close(p_ctx, 0, NI_LOGAN_DEVICE_TYPE_DECODER);

  if ((retval = ni_logan_device_session_open(p_ctx, NI_LOGAN_DEVICE_TYPE_DECODER)) ==
      NI_LOGAN_RETCODE_SUCCESS)
  {
    // copy over the saved stream header to be sent as part of the first data
    // to decoder
    if (p_tmp_data && tmp_data_size && p_ctx->p_leftover)
    {
      p_ctx->p_hdr_buf = p_tmp_data;
      p_ctx->hdr_buf_size = tmp_data_size;

      memcpy(p_ctx->p_leftover, p_ctx->p_hdr_buf, p_ctx->hdr_buf_size);
      p_ctx->prev_size = p_ctx->hdr_buf_size;
    }
    ni_log(NI_LOG_TRACE, "ni_logan_device_dec_session_flush completed, hdr size %u "
           "saved.\n", p_ctx->hdr_buf_size);
  }

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Sends data the device
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER sends data packet to decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER sends data frame to encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_logan_session_data_io_t struct which contains either a
 *                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *                          If NI_LOGAN_DEVICE_TYPE_DECODER is specified, it is expected
 *                          that the ni_logan_packet_t struct inside the p_data pointer
 *                          contains data to send.
 *                          If NI_LOGAN_DEVICE_TYPE_ENCODER is specified, it is expected
 *                          that the ni_logan_frame_t struct inside the p_data pointer
 *                          contains data to send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
int ni_logan_device_session_write(ni_logan_session_context_t *p_ctx,
                            ni_logan_session_data_io_t *p_data,
                            ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_ctx || !p_data)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  // Here check if keep alive thread is closed.
#ifdef _WIN32
  if (p_ctx->keep_alive_thread.handle && p_ctx->keep_alive_thread_args->close_thread)
#else
  if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args->close_thread)
#endif
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() keep alive thread has been closed, "
           "hw:%d, session:%d\n", __FUNCTION__, p_ctx->hw_id, p_ctx->session_id);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    {
      retval = ni_logan_decoder_session_write(p_ctx, &(p_data->data.packet));
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      retval = ni_logan_encoder_session_write(p_ctx, &(p_data->data.frame));
      break;
    }
    default:
    {
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

  END:

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Reads data the device
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER reads data packet from decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER reads data frame from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_logan_session_data_io_t struct which contains either a
 *                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *                          If NI_LOGAN_DEVICE_TYPE_DECODER is specified, data that was
 *                          read will be placed into ni_logan_frame_t struct inside the p_data pointer
 *                          If NI_LOGAN_DEVICE_TYPE_ENCODER is specified,  data that was
 *                          read will be placed into ni_logan_packet_t struct inside the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
int ni_logan_device_session_read(ni_logan_session_context_t *p_ctx,
                           ni_logan_session_data_io_t *p_data,
                           ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_ctx || !p_data)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  // Here check if keep alive thread is closed.
#ifdef _WIN32
  if (p_ctx->keep_alive_thread.handle && p_ctx->keep_alive_thread_args->close_thread)
#else
  if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args->close_thread)
#endif
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() keep alive thread has been closed, hw:%d "
           "session:%d\n", __FUNCTION__, p_ctx->hw_id, p_ctx->session_id);
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    {
      int seq_change_read_count = 0;
      for (; ;)
      {
        retval = ni_logan_decoder_session_read(p_ctx, &(p_data->data.frame));
        // check resolution change only after initial setting obtained
        // p_data->data.frame.video_width is picture width and will be 32-align
        // adjusted to frame size; p_data->data.frame.video_height is the same as
        // frame size, then compare them to saved one for resolution checking
        //
        int aligned_width = ((p_data->data.frame.video_width + 31) / 32) * 32;
        if ((0 == retval) && seq_change_read_count)
        {
          ni_log(NI_LOG_TRACE, "%s (decoder): seq change NO data next time.\n",
                 __FUNCTION__);
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          p_ctx->active_bit_depth = 0;
          break;
        }
        else if (retval < 0)
        {
          ni_log(NI_LOG_TRACE, "%s (decoder): failure return %d ..\n",
                 __FUNCTION__, retval);
          break;
        }
        else if (p_ctx->frame_num && p_data->data.frame.video_width &&
          p_data->data.frame.video_height && p_data->data.frame.bit_depth &&
          ((aligned_width != p_ctx->active_video_width) ||
          (p_data->data.frame.video_height != p_ctx->active_video_height) || (p_data->data.frame.bit_depth != p_ctx->active_bit_depth) ))
        {
          ni_log(NI_LOG_TRACE, "%s (decoder): sequence change, frame size "
                 "%ux%u %dbits -> %ux%u %dbits, continue read \n", __FUNCTION__,
                 p_ctx->active_video_width, p_ctx->active_video_height,
                 p_ctx->active_bit_depth, p_data->data.frame.video_width,
                 p_data->data.frame.video_height, p_data->data.frame.bit_depth);
          // reset active video resolution to 0 so it can be queried in the re-read
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          p_ctx->active_bit_depth = 0;
          seq_change_read_count++;
        }
        else
        {
          break;
        }
      }
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      retval = ni_logan_encoder_session_read(p_ctx, &(p_data->data.packet));
      break;
    }
    default:
    {
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

  END:

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Query session data from the device - Currently not implemented
 *          If device_type is NI_LOGAN_DEVICE_TYPE_DECODER query session data
 *          from decoder
 *          If device_type is NI_LOGAN_DEVICE_TYPE_EECODER query session data
 *          from encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                                               ni_logan_session_context_t struct
 *  \param[in] device_type  NI_LOGAN_DEVICE_TYPE_DECODER or NI_LOGAN_DEVICE_TYPE_ENCODER
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
 *******************************************************************************/
ni_logan_retcode_t ni_logan_device_session_query(ni_logan_session_context_t* p_ctx,
                                     ni_logan_device_type_t device_type)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  switch (device_type)
  {
    case NI_LOGAN_DEVICE_TYPE_DECODER:
    {
      retval = ni_logan_decoder_session_query(p_ctx);
      break;
    }
    case NI_LOGAN_DEVICE_TYPE_ENCODER:
    {
      retval = ni_logan_encoder_session_query(p_ctx);
      break;
    }
    default:
    {
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      ni_log(NI_LOG_TRACE, "ERROR: Unrecognized device type: %d", device_type);
      break;
    }
  }

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief  Allocate preliminary memory for the frame buffer for encoding
 *          based on provided parameters. Applicable to YUV420 Planar pixel
 *          format only, 8 or 10 bit/pixel.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                                               ni_logan_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_logan_retcode_t ni_logan_frame_buffer_alloc(ni_logan_frame_t* p_frame,
                                   int video_width,
                                   int video_height,
                                   int alignment,
                                   int metadata_flag,
                                   int factor,
                                   int hw_frame_count)
{
  void* p_buffer = NULL;
  int metadata_size = 0;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  // TBD for sequence change (non-regular resolution video sample):
  // width has to be 16-aligned to prevent f/w assertion
  int width_aligned = video_width;
  int height_aligned = video_height;

  if ((!p_frame) || ((factor!=1) && (factor!=2))
     || (video_width>NI_LOGAN_MAX_RESOLUTION_WIDTH) || (video_width<=0)
     || (video_height>NI_LOGAN_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (metadata_flag)
  {
    metadata_size = NI_LOGAN_FW_META_DATA_SZ + NI_LOGAN_MAX_SEI_DATA;
  }

  width_aligned = ((video_width + 31) / 32) * 32;
  height_aligned = ((video_height + 7) / 8) * 8;
  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  ni_log(NI_LOG_TRACE, "%s: aligned=%dx%d orig=%dx%d\n", __FUNCTION__,
         width_aligned, height_aligned, video_width, video_height);

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = chroma_b_size;

  int buffer_size = 0;
  /* if hw_frame_count is zero, this is a software frame */
  if (hw_frame_count == 0)
  {
    buffer_size = luma_size + chroma_b_size + chroma_r_size + metadata_size;
  }
  else
  {
    buffer_size = sizeof(ni_logan_hwframe_surface_t)*hw_frame_count + metadata_size;
  }

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                NI_LOGAN_MEM_PAGE_ALIGNMENT + NI_LOGAN_MEM_PAGE_ALIGNMENT * 3;

  //Check if need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
    ni_log(NI_LOG_TRACE, "%s: free current p_frame->buffer_size=%d\n",
           __FUNCTION__, p_frame->buffer_size);
    ni_logan_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
#ifndef XCODER_SIM_ENABLED
    if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n",
             NI_ERRNO);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#else
    buffer = malloc(buffer_size);
    if (!buffer)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n",
             NI_ERRNO);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#endif
    // init once after allocation
    //memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;
    ni_log(NI_LOG_TRACE, "%s: Allocate new p_frame buffer\n", __FUNCTION__);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s: reuse p_frame buffer\n", __FUNCTION__);
  }

  if (hw_frame_count)
  {
    p_frame->data_len[0] = 0;
    p_frame->data_len[1] = 0;
    p_frame->data_len[2] = 0;
    p_frame->data_len[3] = sizeof(ni_logan_hwframe_surface_t) * hw_frame_count;
  }
  else
  {
    p_frame->data_len[0] = luma_size;
    p_frame->data_len[1] = chroma_b_size;
    p_frame->data_len[2] = chroma_r_size;
    p_frame->data_len[3] = 0;//unused by hwdesc
  }

  p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
  p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + p_frame->data_len[0];
  p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + p_frame->data_len[1];
  p_frame->p_data[3] = (uint8_t*)p_frame->p_data[2] + p_frame->data_len[2]; //hwdescriptor

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "%s: success: p_frame->buffer_size=%d\n",
         __FUNCTION__, p_frame->buffer_size);

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_logan_aligned_free(p_buffer);
  }

  return retval;
}

/*!*****************************************************************************
 *  \brief  Allocate memory for decoder frame buffer based on provided
 *          parameters; the memory is retrieved from a buffer pool and will be
 *          returned to the same buffer pool by ni_logan_decoder_frame_buffer_free.
 *  Note:   all attributes of ni_logan_frame_t will be set up except for memory and
 *          buffer, which rely on the pool being allocated; the pool will be
 *          allocated only after the frame resolution is known.
 *
 *  \param[in] p_pool        Buffer pool to get the memory from
 *  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
 *  \param[in] alloc_mem     Whether to get memory from buffer pool
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_frame_buffer_alloc(ni_logan_buf_pool_t* p_pool,
                                           ni_logan_frame_t *p_frame,
                                           int alloc_mem,
                                           int video_width,
                                           int video_height,
                                           int alignment,
                                           int factor)
{
  void* p_buffer = NULL;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  int width_aligned = video_width;
  int height_aligned = video_height;

  if (!p_frame || (factor != 1 && factor != 2) ||
      video_width > NI_LOGAN_MAX_RESOLUTION_WIDTH || video_width <= 0 ||
      video_height > NI_LOGAN_MAX_RESOLUTION_HEIGHT || video_height <= 0)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  width_aligned = ((video_width + 31) / 32) * 32;
  height_aligned = ((video_height + 7) / 8) * 8;
  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  ni_log(NI_LOG_TRACE, "%s: aligned=%dx%d orig=%dx%d\n", __FUNCTION__,
         width_aligned, height_aligned, video_width, video_height);

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = chroma_b_size;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size +
  NI_LOGAN_FW_META_DATA_SZ + NI_LOGAN_MAX_SEI_DATA;

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                NI_LOGAN_MEM_PAGE_ALIGNMENT + NI_LOGAN_MEM_PAGE_ALIGNMENT * 3;

  p_frame->buffer_size = buffer_size;

  // if need to get a buffer from pool, pool must have been set up
  if (alloc_mem)
  {
    if (! p_pool)
    {
      ni_log(NI_LOG_TRACE, "ERROR %s: invalid pool!\n", __FUNCTION__);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    p_frame->dec_buf = ni_logan_buf_pool_get_buffer(p_pool);
    if (! p_frame->dec_buf)
    {
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    p_frame->p_buffer = p_frame->dec_buf->buf;

    ni_log(NI_LOG_TRACE, "%s: got new frame ptr %p buffer %p\n",
           __FUNCTION__, p_frame->p_buffer, p_frame->dec_buf);
  }
  else
  {
    p_frame->dec_buf = p_frame->p_buffer = NULL;
    ni_log(NI_LOG_TRACE, "%s: NOT alloc mem buffer\n", __FUNCTION__);
  }

  if (p_frame->p_buffer)
  {
    p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
    p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
    p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;
    p_frame->p_data[3] = (uint8_t*)p_frame->p_data[2] + chroma_r_size;
  }
  else
  {
    p_frame->p_data[0] = p_frame->p_data[1] = p_frame->p_data[2] = p_frame->p_data[3] = NULL;
  }

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;
  p_frame->data_len[3] = 0; //for hwdesc

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "%s: success: p_frame->buffer_size=%d\n",
         __FUNCTION__, p_frame->buffer_size);

  END:

  return retval;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data.
  *          Applicable to YUV420p AVFrame only. 8 or 10 bit/pixel.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data)
  *
  *  \return On success
  *                          NI_LOGAN_RETCODE_SUCCESS
  *          On failure
  *                          NI_LOGAN_RETCODE_INVALID_PARAM
  *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_logan_retcode_t ni_logan_encoder_frame_buffer_alloc(ni_logan_frame_t* p_frame,
                                           int video_width,
                                           int video_height,
                                           int linesize[],
                                           int alignment,
                                           int extra_len,
                                           int factor)
{
  void* p_buffer = NULL;
  int height_aligned = video_height;
  int retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_frame || !linesize || linesize[0] <= 0 ||
      linesize[0] > NI_LOGAN_MAX_RESOLUTION_WIDTH * factor ||
      video_width > NI_LOGAN_MAX_RESOLUTION_WIDTH || video_width <= 0 ||
      video_height>NI_LOGAN_MAX_RESOLUTION_HEIGHT || video_height<=0)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }
  // TBD height has to be 8-aligned; had a checking at codec opening, but
  // if still getting non-8-aligned here, that could only be the case of
  // resolution change mid-stream: device_handle it by making them 8-aligned
  height_aligned = ((video_height + 7) / 8) * 8;

  if (alignment)
  {
    height_aligned = ((video_height + 15) / 16) * 16;
  }

  if (height_aligned < NI_LOGAN_MIN_HEIGHT)
  {
    height_aligned = NI_LOGAN_MIN_HEIGHT;
  }

  ni_log(NI_LOG_TRACE, "%s: aligned=%dx%d orig=%dx%d linesize=%d/%d/%d "
         "extra_len=%d\n", __FUNCTION__, video_width, height_aligned,
         video_width, video_height, linesize[0], linesize[1], linesize[2],
         extra_len);

  int luma_size = linesize[0] * height_aligned;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = luma_size / 4;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size + extra_len;

  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) *
                NI_LOGAN_MEM_PAGE_ALIGNMENT +NI_LOGAN_MEM_PAGE_ALIGNMENT;

  //Check if Need to free
  if (p_frame->buffer_size != buffer_size && p_frame->buffer_size > 0)
  {
    ni_log(NI_LOG_TRACE, "%s: free current p_frame, p_frame->buffer_size=%d\n",
           __FUNCTION__, p_frame->buffer_size);
    ni_logan_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
#ifndef XCODER_SIM_ENABLED
    if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n",
             NI_ERRNO);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#else
    p_buffer = malloc(buffer_size);
    if (!p_buffer)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate p_frame buffer.\n",
             NI_ERRNO);
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#endif
    // init once after allocation
    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    ni_log(NI_LOG_TRACE, "%s: allocated new p_frame buffer\n", __FUNCTION__);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s: reuse p_frame buffer\n", __FUNCTION__);
  }

  p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
  p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
  p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;
  p_frame->p_data[3] = (uint8_t*)p_frame->p_data[2] + chroma_r_size;

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;
  p_frame->data_len[3] = 0;//unused by hwdesc

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "%s: success: p_frame->buffer_size=%d\n",
         __FUNCTION__, p_frame->buffer_size);

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_logan_aligned_free(p_buffer);
  }

  return retval;
}

/*!******************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_logan_frame_buffer_alloc or ni_logan_encoder_frame_buffer_alloc
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_logan_frame_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_logan_retcode_t ni_logan_frame_buffer_free(ni_logan_frame_t* p_frame)
{
  int i;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s: enter\n", __FUNCTION__);

  if (!p_frame)
  {
    ni_log(NI_LOG_TRACE, "WARN: %s(): p_frame is NULL\n", __FUNCTION__);
    LRETURN;
  }

  if (!p_frame->p_buffer)
  {
    ni_log(NI_LOG_TRACE, "WARN: %s(): already freed, nothing to free\n",
           __FUNCTION__);
    // made idempotent so that aux data of dummy frame can be freed
  }

  ni_logan_aligned_free(p_frame->p_buffer);
  p_frame->p_buffer = NULL;

  p_frame->buffer_size = 0;
  for (i = 0; i < NI_LOGAN_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }
  ni_logan_frame_wipe_aux_data(p_frame);

  END:

  ni_log(NI_LOG_TRACE, "%s: exit\n", __FUNCTION__);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Free decoder frame buffer that was previously allocated with
 *          ni_logan_decoder_frame_buffer_alloc, returning memory to a buffer pool.
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_logan_frame_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_frame_buffer_free(ni_logan_frame_t *p_frame)
{
  int i;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s: enter\n", __FUNCTION__);

  if (!p_frame)
  {
    ni_log(NI_LOG_TRACE, "WARN: %s(): p_frame is NULL\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_frame->dec_buf)
  {
    ni_logan_buf_pool_return_buffer(p_frame->dec_buf, (ni_logan_buf_pool_t *)p_frame->dec_buf->pool);
    ni_log(NI_LOG_TRACE, "%s(): Mem buf returned ptr %p buf %p!\n",
           __FUNCTION__, p_frame->dec_buf->buf, p_frame->dec_buf);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): NO mem buf returned !\n", __FUNCTION__);
  }

  p_frame->dec_buf = p_frame->p_buffer = NULL;

  p_frame->buffer_size = 0;
  for (i = 0; i < NI_LOGAN_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }
  ni_logan_frame_wipe_aux_data(p_frame);

  END:

  ni_log(NI_LOG_TRACE, "%s: exit\n", __FUNCTION__);

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
void ni_logan_decoder_frame_buffer_pool_return_buf(ni_logan_buf_t *buf, ni_logan_buf_pool_t *p_buffer_pool)
{
  ni_logan_buf_pool_return_buffer(buf, p_buffer_pool);
}

/*!******************************************************************************
 *  \brief  Allocate memory for the packet buffer based on provided packet size
 *
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_logan_packet_t struct
 *  \param[in] packet_size   Required allocation size
 *
 *  \return On success
 *                          NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                          NI_LOGAN_RETCODE_INVALID_PARAM
 *                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_logan_retcode_t ni_logan_packet_buffer_alloc(ni_logan_packet_t* p_packet, int packet_size)
{
  void* p_buffer = NULL;
  int metadata_size = 0;
  int retval = NI_LOGAN_RETCODE_SUCCESS;

  metadata_size = NI_LOGAN_FW_META_DATA_SZ;

  int buffer_size = (((packet_size + metadata_size) / NI_LOGAN_MAX_PACKET_SZ) + 1) * NI_LOGAN_MAX_PACKET_SZ;

  ni_log(NI_LOG_TRACE, "%s: packet_size=%d\n",
         __FUNCTION__, packet_size + metadata_size);

  if (!p_packet || !packet_size)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): null pointer parameters passed\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (buffer_size % NI_LOGAN_MEM_PAGE_ALIGNMENT)
  {
    buffer_size = ( (buffer_size / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT ) + NI_LOGAN_MEM_PAGE_ALIGNMENT;
  }

  if (p_packet->buffer_size == buffer_size)
  {
    p_packet->p_data = p_packet->p_buffer;
    ni_log(NI_LOG_TRACE, "%s(): reuse current p_packet buffer\n", __FUNCTION__);
    LRETURN; //Already allocated the exact size
  }
  else if (p_packet->buffer_size > 0)
  {
    ni_log(NI_LOG_TRACE, "%s(): free current p_packet buffer_size=%d\n",
           __FUNCTION__, p_packet->buffer_size);
    ni_logan_packet_buffer_free(p_packet);
  }
  ni_log(NI_LOG_TRACE, "%s(): Allocating p_frame buffer, buffer_size=%d\n",
         __FUNCTION__, buffer_size);
#ifndef XCODER_SIM_ENABLED
  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_TRACE, "ERROR %d: %s() Cannot allocate p_frame buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
#else
  p_buffer = malloc(buffer_size);
  if (p_buffer == NULL)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s(): Cannot allocate p_frame buffer.\n",
           NI_ERRNO, __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
#endif
  //memset(p_buffer, 0, buffer_size);
  p_packet->buffer_size = buffer_size;
  p_packet->p_buffer = p_buffer;
  p_packet->p_data = p_packet->p_buffer;

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_logan_aligned_free(p_buffer);
  }

  ni_log(NI_LOG_TRACE, "%s: exit: p_packet->buffer_size=%d\n",
         __FUNCTION__, p_packet->buffer_size);

  return retval;
}

/*!******************************************************************************
 *  \brief  Free packet buffer that was previously allocated with either
 *          ni_logan_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_logan_packet_t struct
 *
 *  \return On success    NI_LOGAN_RETCODE_SUCCESS
 *          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_logan_retcode_t ni_logan_packet_buffer_free(ni_logan_packet_t* p_packet)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_packet)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): p_packet is NULL\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }

  if (!p_packet->p_buffer)
  {
    ni_log(NI_LOG_TRACE, "%s(): already freed nothing to free\n", __FUNCTION__);
    LRETURN;
  }

  ni_logan_aligned_free(p_packet->p_buffer);
  p_packet->p_buffer = NULL;

  p_packet->buffer_size = 0;
  p_packet->data_len = 0;
  p_packet->p_data = NULL;

  END:

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __FUNCTION__);

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
 *          On failure        NI_LOGAN_RETCODE_FAILURE
 *******************************************************************************/
int ni_logan_packet_copy(void* p_destination,
                   const void* const p_source,
                   int cur_size,
                   void* p_leftover,
                   int* p_prev_size)
{
  int copy_size = 0;
  int padding_size = 0;
  int prev_size = p_prev_size == NULL? 0 : *p_prev_size;
  int total_size = cur_size + prev_size;
  uint8_t* p_src = (uint8_t*)p_source;
  uint8_t* p_dst = (uint8_t*)p_destination;
  uint8_t* p_lftover = (uint8_t*)p_leftover;

  ni_log(NI_LOG_TRACE, "%s(): enter, cur_size=%d, copy_size=%d, prev_size=%d\n",
         __FUNCTION__, cur_size, copy_size, *p_prev_size);

  if (0 == cur_size && 0 == prev_size)
  {
    return copy_size;
  }

  if (((0 != cur_size) && (!p_source)) || (!p_destination) || (!p_leftover))
  {
    return NI_LOGAN_RETCODE_FAILURE;
  }

  copy_size = ((total_size + NI_LOGAN_MEM_PAGE_ALIGNMENT - 1) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT;

  if (copy_size > total_size)
  {
    padding_size = copy_size - total_size;
  }

  if (prev_size > 0)
  {
    memcpy(p_dst, p_lftover, prev_size);
  }

  p_dst += prev_size;

  memcpy(p_dst, p_src, cur_size);

  if (padding_size)
  {
    p_dst += cur_size;
    memset(p_dst, 0, padding_size);
  }

  if (p_prev_size)
  {
    *p_prev_size = 0;
  }

  ni_log(NI_LOG_TRACE, "%s(): exit, cur_size=%d, copy_size=%d, prev_size=%d\n",
         __FUNCTION__, cur_size, copy_size, *p_prev_size);

  return copy_size;
}

/*!******************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      width
 *  \param[in] height     height
 *
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_init_default_params(ni_logan_encoder_params_t* p_param,
                                            int fps_num,
                                            int fps_denom,
                                            long bit_rate,
                                            int width,
                                            int height)
{
  ni_logan_h265_encoder_params_t* p_enc = NULL;
  int i = 0;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  //Initialize p_param structure
  if (!p_param)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): null pointer parameters passed\n",
           __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "%s()\n", __FUNCTION__);

  //Initialize p_param structure
  memset(p_param, 0, sizeof(ni_logan_encoder_params_t));

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
  p_enc->rc.min_qp = NI_LOGAN_DEFAULT_MIN_QP;
  p_enc->rc.max_qp = NI_LOGAN_DEFAULT_MAX_QP;
  p_enc->rc.max_delta_qp = NI_LOGAN_DEFAULT_MAX_DELTA_QP;
  p_enc->rc.rc_init_delay = 3000;

  p_enc->roi_enable = 0;

  p_enc->forced_header_enable = NI_LOGAN_ENC_REPEAT_HEADERS_ALL_KEY_FRAMES;
  p_enc->long_term_ref_enable = 0;

  p_enc->lossless_enable = 0;

  p_enc->conf_win_top = 0;
  p_enc->conf_win_bottom = 0;
  p_enc->conf_win_left = 0;
  p_enc->conf_win_right = 0;

  p_enc->intra_period = 92;
  p_enc->rc.intra_qp = NI_LOGAN_DEFAULT_INTRA_QP;
  // TBD Rev. B: could be shared for HEVC and H.264 ?
  p_enc->rc.enable_mb_level_rc = 1;

  p_enc->decoding_refresh_type = 2;
  p_enc->slice_mode = 0;
  p_enc->slice_arg = 0;

  // Rev. B: H.264 only parameters.
  p_enc->enable_transform_8x8 = 1;
  p_enc->avc_slice_mode = 0;
  p_enc->avc_slice_arg = 0;
  p_enc->entropy_coding_mode = 1;

  p_enc->intra_mb_refresh_mode = 0;
  p_enc->intra_mb_refresh_arg = 0;

  p_enc->custom_gop_params.custom_gop_size = 0;
  for (i = 0; i < NI_LOGAN_MAX_GOP_NUM; i++)
  {
    p_enc->custom_gop_params.pic_param[i].pic_type = LOGAN_PIC_TYPE_I;
    p_enc->custom_gop_params.pic_param[i].poc_offset = 0;
    p_enc->custom_gop_params.pic_param[i].pic_qp = 0;

    // syed todo: check this initial value, added for T408 5
    p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;

    p_enc->custom_gop_params.pic_param[i].ref_poc_L0 = 0;
    p_enc->custom_gop_params.pic_param[i].ref_poc_L1 = 0;
    p_enc->custom_gop_params.pic_param[i].temporal_id = 0;
  }
  p_enc->keep_alive_timeout = NI_LOGAN_DEFAULT_KEEP_ALIVE_TIMEOUT;
  p_enc->set_high_priority = 0;
  
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
  p_param->cacheRoi = 0;
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

  p_param->color_primaries = 2; // default COL_PRI_UNSPECIFIED
  p_param->color_transfer_characteristic = 2; // default COL_TRC_UNSPECIFIED
  p_param->color_space = 2; // default COL_SPC_UNSPECIFIED
  p_param->sar_num = 0;   // default SAR numerator 0
  p_param->sar_denom = 1; // default SAR denominator 1
  p_param->video_full_range_flag = -1;

  p_param->nb_save_pkt = 0;

  if (p_param->source_width > NI_LOGAN_PARAM_MAX_WIDTH)
  {
    retval = NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
    LRETURN;
  }
  if (p_param->source_width < NI_LOGAN_PARAM_MIN_WIDTH)
  {
    retval = NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL;
    LRETURN;
  }

  if (p_param->source_height > NI_LOGAN_PARAM_MAX_HEIGHT)
  {
    retval = NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
    LRETURN;
  }
  if (p_param->source_height < NI_LOGAN_PARAM_MIN_HEIGHT)
  {
    retval =  NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL;
    LRETURN;
  }
  if (p_param->source_height*p_param->source_width > NI_LOGAN_MAX_RESOLUTION_AREA)
  {
    retval = NI_LOGAN_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
    LRETURN;
  }

  END:

  return retval;
}

ni_logan_retcode_t ni_logan_decoder_init_default_params(ni_logan_encoder_params_t* p_param,
                                            int fps_num,
                                            int fps_denom,
                                            long bit_rate,
                                            int width,
                                            int height)
{
  ni_logan_decoder_input_params_t* p_dec = NULL;
  int i = 0;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  //Initialize p_param structure
  if (!p_param)
  {
    ni_log(NI_LOG_TRACE, "%s(): null pointer parameter passed\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "%s\n", __FUNCTION__);

  //Initialize p_param structure
  memset(p_param, 0, sizeof(ni_logan_encoder_params_t));

  p_dec = &p_param->dec_input_params;
  p_dec->hwframes = 0;
  p_dec->keep_alive_timeout = NI_LOGAN_DEFAULT_KEEP_ALIVE_TIMEOUT;
  p_dec->set_high_priority = 0;
  p_dec->enable_user_data_sei_passthru = 0;
  p_dec->check_packet = 0;
  p_dec->custom_sei_passthru = -1;
  p_dec->lowdelay = 0;
  p_param->source_width = width;
  p_param->source_height = height;
  p_param->fps_number = fps_num;
  p_param->fps_denominator = fps_denom;
  p_param->bitrate = bit_rate;
  p_param->reconf_demo_mode = 0; // NETINT_INTERNAL - currently only for internal testing
  p_param->force_pic_qp_demo_mode = 0;
  p_param->force_frame_type = 0;
  p_param->hdrEnableVUI = 0;
  p_param->crf = 0;
  p_param->cbr = 0;
  p_param->cacheRoi = 0;
  p_param->ui32flushGop = 0;
  p_param->ui32minIntraRefreshCycle = 0;
  p_param->low_delay_mode = 0;
  p_param->padding = 1;
  p_param->generate_enc_hdrs = 0;
  p_param->use_low_delay_poc_type = 0;
  p_param->strict_timeout_mode = 0;
  p_param->dolby_vision_profile = 0;
  p_param->hrd_enable = 0;
  p_param->enable_aud = 0;

  p_param->color_primaries = 2; // default COL_PRI_UNSPECIFIED
  p_param->color_transfer_characteristic = 2; // default COL_TRC_UNSPECIFIED
  p_param->color_space = 2; // default COL_SPC_UNSPECIFIED
  p_param->sar_num = 0;   // default SAR numerator 0
  p_param->sar_denom = 1; // default SAR denominator 1
  p_param->video_full_range_flag = -1;

  p_param->nb_save_pkt = 0;

  END:

  return retval;
}

// read demo reconfig data file and parse out reconfig key/values in the format:
// key:val1,val2,val3,...val9 (max 9 values); only digit/:/,/newline is allowed
ni_logan_retcode_t ni_logan_parse_reconf_file(const char* reconf_file, int hash_map[100][10])
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
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  FILE *reconf = fopen(reconf_file, "r");
  if (!reconf)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s(): Cannot open reconfig_file: %s\n",
           NI_ERRNO, __FUNCTION__, reconf_file);
    return NI_LOGAN_RETCODE_FAILURE;
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
      ni_log(NI_LOG_TRACE, "error character %c in reconfig file. this may lead "
             "to mistaken reconfiguration values \n", readc);
    }
  }

  fclose(reconf);

  return NI_LOGAN_RETCODE_SUCCESS;
}

#undef atoi
#undef atof
#define atoi(p_str) ni_logan_atoi(p_str, &b_error)
#define atof(p_str) ni_logan_atof(p_str, &b_error)
#define atobool(p_str) (ni_logan_atobool(p_str, &b_error))
/*!******************************************************************************
*  \brief  Set value referenced by name in decoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_logan_decoder_params_t
*                                    to find and set a particular parameter
*  \param[in] name       String represented parameter name to search
*  \param[in] value      Parameter value to set
*
*  \return On success
*                        NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                        NI_LOGAN_RETCODE_FAILURE
*                        NI_LOGAN_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_logan_retcode_t ni_logan_decoder_params_set_value(ni_logan_encoder_params_t* p_params, const char* name, char* value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_logan_decoder_input_params_t* p_dec = &p_params->dec_input_params;
  char nameBuf[64] = { 0 };
  const char delim[2] = ",";
  const char xdelim[2] = "x";

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_params)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n", __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if (!name)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n", __FUNCTION__);
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
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
    strcpy(nameBuf, name);
    while ((c = strchr(nameBuf, '_')) != 0)
    {
      *c = '-';
    }

    name = nameBuf;
  }

  if (!strncmp(name, "no-", 3))
  {
    name += 3;
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!strncmp(name, "no", 2))
  {
    name += 2;
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!value)
  {
    value = "true";
  }
  else if (value[0] == '=')
  {
    value++;
  }

#ifdef _MSC_VER
#define OPT(STR) else if (!_stricmp(name, STR))
#define OPT2(STR1, STR2) else if (!_stricmp(name, STR1) || !_stricmp(name, STR2))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#define OPT2(STR1, STR2) else if (!strcasecmp(name, STR1) || !strcasecmp(name, STR2))
#endif
  if (0)
    ;
  OPT(NI_LOGAN_DEC_PARAM_OUT)
  {
    if (!strncmp(value, "hw", 2)){
      p_dec->hwframes = 1;
    }
    else if (!strncmp(value, "sw", 2)) {
      p_dec->hwframes = 0;
    }
    else{
      ni_log(NI_LOG_ERROR, "ERROR: %s(): out can only be <hw,sw> got %s\n",
             __FUNCTION__, value);
      return NI_LOGAN_RETCODE_PARAM_INVALID_VALUE;
    }
  }
  OPT( NI_LOGAN_SET_HIGH_PRIORITY )
  {
    if(atoi(value) != NI_LOGAN_MIN_PRIORITY && 
       atoi(value) != NI_LOGAN_MAX_PRIORITY)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->set_high_priority = atoi(value);
  }
  OPT( NI_LOGAN_KEEP_ALIVE_TIMEOUT )
  {
    if(atoi(value) < NI_LOGAN_MIN_KEEP_ALIVE_TIMEOUT ||
       atoi(value) > NI_LOGAN_MAX_KEEP_ALIVE_TIMEOUT)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->keep_alive_timeout = atoi(value);
  }
  OPT( NI_LOGAN_DEC_PARAM_USR_DATA_SEI_PASSTHRU )
  {
    if(atoi(value) != NI_LOGAN_ENABLE_USR_DATA_SEI_PASSTHRU && 
       atoi(value) != NI_LOGAN_DISABLE_USR_DATA_SEI_PASSTHRU)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }   
    p_dec->enable_user_data_sei_passthru = atoi(value);
  }
  OPT( NI_LOGAN_DEC_PARAM_CHECK_PACKET )
  {
    if(atoi(value) != NI_LOGAN_ENABLE_CHECK_PACKET && 
       atoi(value) != NI_LOGAN_DISABLE_CHECK_PACKET)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->check_packet = atoi(value);
  }
  OPT( NI_LOGAN_DEC_PARAM_CUSTOM_SEI_PASSTHRU )
  {
    if(atoi(value) < NI_LOGAN_MIN_CUSTOM_SEI_PASSTHRU ||
       atoi(value) > NI_LOGAN_MAX_CUSTOM_SEI_PASSTHRU)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->custom_sei_passthru = atoi(value);
  }
  OPT( NI_LOGAN_DEC_PARAM_LOW_DELAY )
  {
    p_dec->lowdelay = atoi(value);
  }
  OPT( NI_LOGAN_DEC_PARAM_SAVE_PKT )
  {
    p_params->nb_save_pkt = atoi(value);
  }
  else
  {
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
  }
#undef OPT
#undef atobool
#undef atoi
#undef atof
  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "ni_logan_decoder_params_set_value: exit, b_error=%d\n", b_error);

  return b_error ? NI_LOGAN_RETCODE_PARAM_INVALID_VALUE : NI_LOGAN_RETCODE_SUCCESS;
}


#undef atoi
#undef atof
#define atoi(p_str) ni_logan_atoi(p_str, &b_error)
#define atof(p_str) ni_logan_atof(p_str, &b_error)
#define atobool(p_str) (ni_logan_atobool(p_str, &b_error))

static ni_logan_retcode_t ni_logan_check_level(int level, int codec_id)
{
  const int l_levels_264[] = {10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62, 0};
  const int l_levels_265[] = {10, 20, 21, 30, 31, 40, 41, 50, 51, 52, 60, 61, 62, 0};
  const int *l_levels = l_levels_264;

  if ( level == 0 )
  {
    return NI_LOGAN_RETCODE_SUCCESS;
  }

  if ( codec_id == NI_LOGAN_CODEC_FORMAT_H265 )
  {
    l_levels = l_levels_265;
  }

  while( *l_levels != 0 )
  {
    if ( *l_levels == level )
    {
      return NI_LOGAN_RETCODE_SUCCESS;
    }

    l_levels++;
  }

  return NI_LOGAN_RETCODE_FAILURE;
}


/*!******************************************************************************
 *  \brief  Set value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_params_set_value(ni_logan_encoder_params_t* p_params,
                                         const char* name,
                                         const char* value,
                                         ni_logan_session_context_t *ctx)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_logan_h265_encoder_params_t* p_enc = &p_params->hevc_enc_params;
  char nameBuf[64] = { 0 };

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_params)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  if ( !name )
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
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
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!strncmp(name, "no", 2))
  {
    name += 2;
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!value)
  {
    value = "true";
  }
  else if (value[0] == '=')
  {
    value++;
  }

#ifdef _MSC_VER
#define OPT(STR) else if (!_stricmp(name, STR))
#define OPT2(STR1, STR2) else if (!_stricmp(name, STR1) || !_stricmp(name, STR2))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#define OPT2(STR1, STR2) else if (!strcasecmp(name, STR1) || !strcasecmp(name, STR2))
#endif
#define COMPARE(STR1, STR2, STR3)                \
if ((atoi(STR1) > STR2) || (atoi(STR1) < STR3)) \
{                                                \
  return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;             \
}
#define CHECK2VAL(STR, VAL1, VAL2)        \
if (atoi(STR) != VAL1 && atoi(STR) != VAL2) \
{                                         \
  return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;      \
}

  if (0)
      ;
  OPT( NI_LOGAN_ENC_PARAM_BITRATE )
  {
    if (LOGAN_AV_CODEC_DEFAULT_BITRATE == p_params->bitrate)
    {
      if (atoi(value) > NI_LOGAN_MAX_BITRATE)
      {
        return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_BIG;
      }
      if (atoi(value) < NI_LOGAN_MIN_BITRATE)
      {
        return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_SMALL;
      }
      p_params->bitrate = atoi(value);
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_RECONF_DEMO_MODE )
  {
    p_params->reconf_demo_mode = atoi(value); // NETINT_INTERNAL - currently only for internal testing
  }
  OPT( NI_LOGAN_ENC_PARAM_RECONF_FILE )
  {
    ni_logan_retcode_t retval = ni_logan_parse_reconf_file(value, p_params->reconf_hash);
    if (retval != NI_LOGAN_RETCODE_SUCCESS) // NETINT_INTERNAL - currently only for internal testing
    {
      return retval;
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_ROI_DEMO_MODE )
  {
    // NETINT_INTERNAL - currently only for internal testing
    CHECK2VAL(value, 1, 2)
    p_params->roi_demo_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_LOW_DELAY )
  {
    p_params->low_delay_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_PADDING )
  {
    p_params->padding = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_GEN_HDRS )
  {
    p_params->generate_enc_hdrs = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_USE_LOW_DELAY_POC_TYPE )
  {
    p_params->use_low_delay_poc_type = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_FORCE_FRAME_TYPE )
  {
    p_params->force_frame_type = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_PROFILE )
  {
    p_enc->profile = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_LEVEL )
  {
    p_enc->level_idc = (int)(10 * strtof(value, NULL)); // if value is not digital number, it return 0
    if ( ( p_enc->level_idc== 0 && strcmp(value, "0") )||
        ( strlen(value) > 3 ) ||
        ( ni_logan_check_level(p_enc->level_idc,
            (ctx!=NULL)?ctx->codec_format:NI_LOGAN_CODEC_FORMAT_H265) != NI_LOGAN_RETCODE_SUCCESS) )
    {
      b_error = true;
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_HIGH_TIER )
  {
    p_enc->high_tier = atobool(value);
  }
  OPT2( NI_LOGAN_ENC_PARAM_LOG_LEVEL, NI_LOGAN_ENC_PARAM_LOG )
  {
    p_params->log = atoi(value);
    if (b_error)
    {
      b_error = false;
      p_params->log = ni_logan_parse_name(value, g_logan_xcoder_log_names, &b_error) - 1;
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_GOP_PRESET_IDX )
  {
    COMPARE(value, NI_LOGAN_MAX_GOP_PRESET_IDX, NI_LOGAN_MIN_GOP_PRESET_IDX)
    p_enc->gop_preset_index = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_USE_RECOMMENDED_ENC_PARAMS )
  {
    COMPARE(value, NI_LOGAN_MAX_USE_RECOMMENDED_ENC_PARAMS, NI_LOGAN_MIN_USE_RECOMMENDED_ENC_PARAMS)
    p_enc->use_recommend_enc_params = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_RATE_CONTROL )
  {
    COMPARE(value, NI_LOGAN_MAX_BIN, NI_LOGAN_MIN_BIN)
    p_enc->rc.enable_rate_control = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_CU_LEVEL_RATE_CONTROL )
  {
    COMPARE(value, NI_LOGAN_MAX_BIN, NI_LOGAN_MIN_BIN)
    p_enc->rc.enable_cu_level_rate_control = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_HVS_QP )
  {
    p_enc->rc.enable_hvs_qp = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_HVS_QP_SCALE )
  {
    p_enc->rc.enable_hvs_qp_scale = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_HVS_QP_SCALE )
  {
    p_enc->rc.hvs_qp_scale = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_MIN_QP )
  {
    COMPARE(value, NI_LOGAN_MAX_MIN_QP, NI_LOGAN_MIN_MIN_QP)
    p_enc->rc.min_qp = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_MAX_QP )
  {
    COMPARE(value, NI_LOGAN_MAX_MAX_QP, NI_LOGAN_MIN_MAX_QP)
    p_enc->rc.max_qp = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_MAX_DELTA_QP )
  {
    COMPARE(value, NI_LOGAN_MAX_MAX_DELTA_QP, NI_LOGAN_MIN_MAX_DELTA_QP)
    p_enc->rc.max_delta_qp = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_RC_INIT_DELAY )
  {
    p_enc->rc.rc_init_delay = atoi(value);
  }
  OPT ( NI_LOGAN_ENC_PARAM_FORCED_HEADER_ENABLE )
  {
    p_enc->forced_header_enable = atoi(value);
  }
  OPT ( NI_LOGAN_ENC_PARAM_ROI_ENABLE )
  {
    p_enc->roi_enable = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CONF_WIN_TOP )
  {
    p_enc->conf_win_top = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CONF_WIN_BOTTOM )
  {
    p_enc->conf_win_bottom = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CONF_WIN_LEFT )
  {
    p_enc->conf_win_left = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CONF_WIN_RIGHT )
  {
    p_enc->conf_win_right = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_PERIOD )
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_PERIOD, NI_LOGAN_MIN_INTRA_PERIOD)
    p_enc->intra_period = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_TRANS_RATE )
  {
    if (atoi(value) > NI_LOGAN_MAX_BITRATE)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_BIG;
    }
    if (atoi(value) < NI_LOGAN_MIN_BITRATE)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_SMALL;
    }
    p_enc->rc.trans_rate = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_FRAME_RATE )
  {
    if (atoi(value) <= 0 )
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_ZERO;
    }
    p_params->fps_number = atoi(value);
    p_params->fps_denominator = 1;
    p_enc->frame_rate = p_params->fps_number;
  }
  OPT( NI_LOGAN_ENC_PARAM_FRAME_RATE_DENOM )
  {
    p_params->fps_denominator = atoi(value);
    if(p_params->fps_denominator == 0)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_ZERO;
    }
    p_enc->frame_rate = p_params->fps_number / p_params->fps_denominator;
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_QP )
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP, NI_LOGAN_MIN_INTRA_QP)
    p_enc->rc.intra_qp = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_FORCE_PIC_QP_DEMO_MODE )
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP, NI_LOGAN_MIN_INTRA_QP)
    p_params->force_pic_qp_demo_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_DECODING_REFRESH_TYPE )
  {
    COMPARE(value, NI_LOGAN_MAX_DECODING_REFRESH_TYPE, NI_LOGAN_MIN_DECODING_REFRESH_TYPE)
    p_enc->decoding_refresh_type = atoi(value);
  }
// Rev. B: H.264 only parameters.
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_8X8_TRANSFORM )
  {
    p_enc->enable_transform_8x8 = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_AVC_SLICE_MODE )
  {
    CHECK2VAL(value, 0, 1)
    p_enc->avc_slice_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_AVC_SLICE_ARG )
  {
    p_enc->avc_slice_arg = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_ENTROPY_CODING_MODE)
  {
    p_enc->entropy_coding_mode = atoi(value);
  }
// Rev. B: shared between HEVC and H.264
  OPT( NI_LOGAN_ENC_PARAM_SLICE_MODE )
  {
    CHECK2VAL(value, 0, 1)
    if (ctx->codec_format==NI_LOGAN_CODEC_FORMAT_H264)
    {
      p_enc->avc_slice_mode =  atoi(value);
    }
    else
    {
      p_enc->slice_mode = atoi(value);
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_SLICE_ARG )
  {
    if (ctx->codec_format==NI_LOGAN_CODEC_FORMAT_H264)
    {
      p_enc->avc_slice_arg  = atoi(value);
    }
    else
    {
      p_enc->slice_arg = atoi(value);
    }
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_MB_REFRESH_MODE )
  {
    p_enc->intra_mb_refresh_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_MB_REFRESH_ARG )
  {
    p_enc->intra_mb_refresh_arg = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_REFRESH_MODE )
  {
    p_enc->intra_mb_refresh_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_REFRESH_ARG )
  {
    p_enc->intra_mb_refresh_arg = atoi(value);
  }
// TBD Rev. B: could be shared for HEVC and H.264
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_MB_LEVEL_RC )
  {
    p_enc->rc.enable_mb_level_rc = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS)
  {
    COMPARE(value, 255, 0)
    p_enc->preferred_transfer_characteristics = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_DOLBY_VISION_PROFILE )
  {
    CHECK2VAL(value, 0, 5)
    p_params->dolby_vision_profile = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_HRD_ENABLE )
  {
    CHECK2VAL(value, 0, 1)
    p_params->hrd_enable = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_AUD )
  {
    CHECK2VAL(value, 0, 1)
    p_params->enable_aud = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CRF )
  {
    COMPARE(value, NI_LOGAN_MAX_CRF, NI_LOGAN_MIN_CRF)
    p_params->crf = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CBR )
  {
    CHECK2VAL(value, 0, 1)
    p_params->cbr = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_CACHE_ROI )
  {
    CHECK2VAL(value, 0, 1)
    p_params->cacheRoi = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_FLUSH_GOP )
  {
    CHECK2VAL(value, 0, 1)
    p_params->ui32flushGop = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD )
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_REFRESH_MIN_PERIOD, NI_LOGAN_MIN_INTRA_REFRESH_MIN_PERIOD)
    p_params->ui32minIntraRefreshCycle = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE )
  {
    CHECK2VAL(value, 0, 1)
    p_enc->long_term_ref_enable = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_STRICT_TIMEOUT_MODE )
  {
    p_params->strict_timeout_mode = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_LOSSLESS_ENABLE )
  {
    CHECK2VAL(value, 0, 1)
    p_enc->lossless_enable = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_COLOR_PRIMARY)
  {
    COMPARE(value, 22, 0)
    p_params->color_primaries = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_COLOR_TRANSFER_CHARACTERISTIC)
  {
    COMPARE(value, 18, 0)
    p_params->color_transfer_characteristic = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_COLOR_SPACE)
  {
    COMPARE(value, 14, 0)
    p_params->color_space = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_SAR_NUM)
  {
    p_params->sar_num = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_SAR_DENOM)
  {
    p_params->sar_denom = atoi(value);
  }
  OPT(NI_LOGAN_ENC_PARAM_VIDEO_FULL_RANGE_FLAG)
  {
    p_params->video_full_range_flag = atoi(value);
  }
  OPT( NI_LOGAN_ENC_PARAM_ENABLE_VFR )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->enable_vfr = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_EXPLICIT_RPL )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->enable_explicit_rpl = atoi(value);
  }
  OPT( NI_LOGAN_SET_HIGH_PRIORITY )
  {
    CHECK2VAL(value, NI_LOGAN_MAX_PRIORITY, NI_LOGAN_MIN_PRIORITY)
    p_enc->set_high_priority = atoi(value);
  }
  OPT( NI_LOGAN_KEEP_ALIVE_TIMEOUT )
  {
    COMPARE(value, NI_LOGAN_MAX_KEEP_ALIVE_TIMEOUT, NI_LOGAN_MIN_KEEP_ALIVE_TIMEOUT)
    p_enc->keep_alive_timeout = atoi(value);
  }
  else
  {
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
  }


#undef OPT
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "%s: exit, b_error=%d\n", __FUNCTION__, b_error);

  return b_error ? NI_LOGAN_RETCODE_PARAM_INVALID_VALUE : NI_LOGAN_RETCODE_SUCCESS;
}

/*!*****************************************************************************
*  \brief  Validate relationship of some params in encoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
*  \param[in] codec      encoding codec
*
*  \return On success
*                        NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                        NI_LOGAN_RETCODE_FAILURE
*                        NI_LOGAN_RETCODE_PARAM_ERROR_OOR and other error rc
*******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_params_check(ni_logan_encoder_params_t* p_params,
                                     ni_logan_codec_format_t codec)
{
  ni_logan_h265_encoder_params_t* p_enc = &p_params->hevc_enc_params;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (p_enc->rc.min_qp > p_enc->rc.max_qp)
  {
    ni_log(NI_LOG_ERROR, "MinQP(%d) > MaxQP(%d)\n", p_enc->rc.min_qp,
           p_enc->rc.max_qp);
    return NI_LOGAN_RETCODE_PARAM_ERROR_OOR;
  }

  if (p_enc->lossless_enable)
  {
    if (NI_LOGAN_CODEC_FORMAT_H264 == codec)
    {
      ni_log(NI_LOG_ERROR, "losslessEnable is not valid for H.264\n");
      return NI_LOGAN_RETCODE_INVALID_PARAM;
    }

    if (p_enc->rc.enable_rate_control || p_enc->roi_enable)
    {
      ni_log(NI_LOG_ERROR, "losslessEnable can not be enabled if RcEnable "
             "or roiEnable is enabled\n");
      return NI_LOGAN_RETCODE_INVALID_PARAM;
    }
  }

  ni_log(NI_LOG_TRACE, "%s: exit\n", __FUNCTION__);

  return NI_LOGAN_RETCODE_SUCCESS;
}



#undef atoi
#undef atof
#define atoi(p_str) ni_logan_atoi(p_str, &b_error)
#define atof(p_str) ni_logan_atof(p_str, &b_error)
#define atobool(p_str) (ni_logan_atobool(p_str, &b_error))

/*!******************************************************************************
 *  \brief  Set got parameter value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *                                    to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_LOGAN_RETCODE_SUCCESS
 *          On failure
 *                        NI_LOGAN_RETCODE_FAILURE
 *                        NI_LOGAN_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_logan_retcode_t ni_logan_encoder_gop_params_set_value(ni_logan_encoder_params_t* p_params,
                                             const char* name,
                                             const char* value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_logan_h265_encoder_params_t* p_enc = &p_params->hevc_enc_params;
  ni_logan_custom_gop_params_t* p_gop = &p_enc->custom_gop_params;
  char nameBuf[64] = { 0 };

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (!p_params || !name)
  {
    ni_log(NI_LOG_TRACE, "ERROR: %s(): Null pointer parameters passed\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
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
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!strncmp(name, "no", 2))
  {
    name += 2;
    value = !value || ni_logan_atobool(value, &b_error) ? "false" : "true";
  }
  else if (!value)
  {
    value = "true";
  }
  else if (value[0] == '=')
  {
    value++;
  }

#ifdef _MSC_VER
#define OPT(STR) else if (!_stricmp(name, STR))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#endif
  if (0)
      ;
  OPT(NI_LOGAN_ENC_GOP_PARAMS_CUSTOM_GOP_SIZE)
  {
    if (atoi(value) > NI_LOGAN_MAX_GOP_SIZE)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_BIG;
    }
    if (atoi(value) < NI_LOGAN_MIN_GOP_SIZE)
    {
      return NI_LOGAN_RETCODE_PARAM_ERROR_TOO_SMALL;
    }
    p_gop->custom_gop_size = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_PIC_TYPE)
  {
    p_gop->pic_param[0].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_POC_OFFSET)
  {
    p_gop->pic_param[0].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[0].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[0].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_POC_L0)
  {
    p_gop->pic_param[0].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_NUM_REF_POC_L1)
  {
    p_gop->pic_param[0].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G0_TEMPORAL_ID)
  {
    p_gop->pic_param[0].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_PIC_TYPE)
  {
    p_gop->pic_param[1].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_POC_OFFSET)
  {
    p_gop->pic_param[1].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[1].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[1].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_POC_L0)
  {
    p_gop->pic_param[1].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_NUM_REF_POC_L1)
  {
    p_gop->pic_param[1].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G1_TEMPORAL_ID)
  {
    p_gop->pic_param[1].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_PIC_TYPE)
  {
    p_gop->pic_param[2].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_POC_OFFSET)
  {
    p_gop->pic_param[2].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[2].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[2].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_POC_L0)
  {
    p_gop->pic_param[2].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_NUM_REF_POC_L1)
  {
    p_gop->pic_param[2].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G2_TEMPORAL_ID)
  {
    p_gop->pic_param[2].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_PIC_TYPE)
  {
    p_gop->pic_param[3].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_POC_OFFSET)
  {
    p_gop->pic_param[3].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[3].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[3].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_POC_L0)
  {
    p_gop->pic_param[3].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_NUM_REF_POC_L1)
  {
    p_gop->pic_param[3].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G3_TEMPORAL_ID)
  {
    p_gop->pic_param[3].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_PIC_TYPE)
  {
    p_gop->pic_param[4].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_POC_OFFSET)
  {
    p_gop->pic_param[4].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[4].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[4].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_POC_L0)
  {
    p_gop->pic_param[4].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_NUM_REF_POC_L1)
  {
    p_gop->pic_param[4].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G4_TEMPORAL_ID)
  {
    p_gop->pic_param[4].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_PIC_TYPE)
  {
    p_gop->pic_param[5].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_POC_OFFSET)
  {
    p_gop->pic_param[5].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[5].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[5].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_POC_L0)
  {
    p_gop->pic_param[5].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_NUM_REF_POC_L1)
  {
    p_gop->pic_param[5].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G5_TEMPORAL_ID)
  {
    p_gop->pic_param[5].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_PIC_TYPE)
  {
    p_gop->pic_param[6].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_POC_OFFSET)
  {
    p_gop->pic_param[6].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[6].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[6].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_POC_L0)
  {
    p_gop->pic_param[6].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_NUM_REF_POC_L1)
  {
    p_gop->pic_param[6].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G6_TEMPORAL_ID)
  {
    p_gop->pic_param[6].temporal_id = atoi(value);
  }

  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_PIC_TYPE)
  {
    p_gop->pic_param[7].pic_type = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_POC_OFFSET)
  {
    p_gop->pic_param[7].poc_offset = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_PIC_QP)
  {
    COMPARE(value, NI_LOGAN_MAX_INTRA_QP - p_enc->rc.intra_qp, NI_LOGAN_MIN_INTRA_QP - p_enc->rc.intra_qp)
    p_gop->pic_param[7].pic_qp = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_PIC_L0)
  {
    p_gop->pic_param[7].num_ref_pic_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_POC_L0)
  {
    p_gop->pic_param[7].ref_poc_L0 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_NUM_REF_POC_L1)
  {
    p_gop->pic_param[7].ref_poc_L1 = atoi(value);
  }
  OPT(NI_LOGAN_ENC_GOP_PARAMS_G7_TEMPORAL_ID)
  {
    p_gop->pic_param[7].temporal_id = atoi(value);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): Invalid parameter name passed\n", __FUNCTION__);
    return NI_LOGAN_RETCODE_PARAM_INVALID_NAME;
  }

#undef OPT
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "%s(): exit, b_error=%d\n", __FUNCTION__, b_error);

  return (b_error ? NI_LOGAN_RETCODE_PARAM_INVALID_VALUE : NI_LOGAN_RETCODE_SUCCESS);
}

/*!*****************************************************************************
 *  \brief  Get GOP's max number of reorder frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *
 *  \return max number of reorder frames of the GOP, -1 on error
 ******************************************************************************/
int ni_logan_get_num_reorder_of_gop_structure(ni_logan_encoder_params_t * p_params)
{
  int gopPreset = p_params->hevc_enc_params.gop_preset_index;
  int reorder = 0;
  int i;
  ni_logan_h265_encoder_params_t* p_enc = NULL;

  if (! p_params)
  {
    ni_log(NI_LOG_ERROR, "%s: NULL input!\n", __FUNCTION__);
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
    ni_log(NI_LOG_ERROR, "%s: gopPresetIdx=%d not supported\n",
           __FUNCTION__, gopPreset);
  }
  return reorder;
}

/*!*****************************************************************************
 *  \brief  Get GOP's number of reference frames
 *
 *  \param[in] p_params   Pointer to a user allocated ni_logan_encoder_params_t
 *
 *  \return number of reference frames of the GOP
 ******************************************************************************/
int ni_logan_get_num_ref_frame_of_gop_structure(ni_logan_encoder_params_t * p_params)
{
  int gopPreset = p_params->hevc_enc_params.gop_preset_index;
  int ref_frames = 0;
  int i;
  ni_logan_h265_encoder_params_t* p_enc = NULL;

  if (! p_params)
  {
    ni_log(NI_LOG_ERROR, "%s: NULL input!\n", __FUNCTION__);
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
    ni_log(NI_LOG_ERROR, "%s: gopPresetIdx=%d not supported\n",
           __FUNCTION__, gopPreset);
  }

  return ref_frames;
}

/*!*****************************************************************************
 *  \brief  Add a new auxiliary data to a frame
 *
 *  \param[in/out] frame  a frame to which the auxiliary data should be added
 *  \param[in]     type   type of the added auxiliary data
 *  \param[in]     data_size size of the added auxiliary data
 *
 *  \return a pointer to the newly added aux data on success, NULL otherwise
 ******************************************************************************/
ni_aux_data_t *ni_logan_frame_new_aux_data(ni_logan_frame_t *frame,
                                     ni_aux_data_type_t type,
                                     int data_size)
{
  ni_aux_data_t *ret;

  if (frame->nb_aux_data >= NI_MAX_NUM_AUX_DATA_PER_FRAME ||
      ! (ret = malloc(sizeof(ni_aux_data_t))))
  {
    ni_log(NI_LOG_ERROR, "ni_logan_frame_new_aux_data No memory or exceeding max "
           "aux_data number !\n");
    return NULL;
  }

  ret->type = type;
  ret->size = data_size;
  ret->data = calloc(1, data_size);
  if (! ret->data)
  {
    ni_log(NI_LOG_ERROR, "ni_logan_frame_new_aux_data No memory for aux data !\n");
    free(ret);
    ret = NULL;
  }
  else
  {
    frame->aux_data[frame->nb_aux_data++] = ret;
  }

  return ret;
}

/*!*****************************************************************************
 *  \brief  Add a new auxiliary data to a frame and copy in the raw data
 *
 *  \param[in/out] frame  a frame to which the auxiliary data should be added
 *  \param[in]     type   type of the added auxiliary data
 *  \param[in]     raw_data  the raw data of the aux data
 *  \param[in]     data_size size of the added auxiliary data
 *
 *  \return a pointer to the newly added aux data on success, NULL otherwise
 ******************************************************************************/
ni_aux_data_t *ni_logan_frame_new_aux_data_from_raw_data(ni_logan_frame_t *frame,
                                                   ni_aux_data_type_t type,
                                                   const uint8_t* raw_data,
                                                   int data_size)
{
  ni_aux_data_t *ret = ni_logan_frame_new_aux_data(frame, type, data_size);
  if (ret)
  {
    memcpy(ret->data, raw_data, data_size);
  }
  return ret;
}

/*!*****************************************************************************
 *  \brief  Retrieve from the frame auxiliary data of a given type if exists
 *
 *  \param[in] frame  a frame from which the auxiliary data should be retrieved
 *  \param[in] type   type of the auxiliary data to be retrieved
 *
 *  \return a pointer to the aux data of a given type on success, NULL otherwise
 ******************************************************************************/
ni_aux_data_t *ni_logan_frame_get_aux_data(const ni_logan_frame_t *frame,
                                     ni_aux_data_type_t type)
{
  int i;
  for (i = 0; i < frame->nb_aux_data; i++)
  {
    if (frame->aux_data[i]->type == type)
    {
      return frame->aux_data[i];
    }
  }
  return NULL;
}

/*!*****************************************************************************
 *  \brief  If auxiliary data of the given type exists in the frame, free it
 *          and remove it from the frame.
 *
 *  \param[in/out] frame a frame from which the auxiliary data should be removed
 *  \param[in] type   type of the auxiliary data to be removed
 *
 *  \return None
 ******************************************************************************/
void ni_logan_frame_free_aux_data(ni_logan_frame_t *frame,
                            ni_aux_data_type_t type)
{
  int i;
  ni_aux_data_t *aux;

  for (i = 0; i < frame->nb_aux_data; i++)
  {
    aux = frame->aux_data[i];
    if (aux->type == type)
    {
      frame->aux_data[i] = frame->aux_data[frame->nb_aux_data - 1];
      frame->aux_data[frame->nb_aux_data - 1] = NULL;
      frame->nb_aux_data--;

      free(aux->data);
      free(aux);
    }
  }
}

/*!*****************************************************************************
 *  \brief  Free and remove all auxiliary data from the frame.
 *
 *  \param[in/out] frame a frame from which the auxiliary data should be removed
 *
 *  \return None
 ******************************************************************************/
void ni_logan_frame_wipe_aux_data(ni_logan_frame_t *frame)
{
  int i;
  ni_aux_data_t *aux;

  for (i = 0; i < frame->nb_aux_data; i++)
  {
    aux = frame->aux_data[i];
    free(aux->data);
    free(aux);
    frame->aux_data[i] = NULL;
  }
  frame->nb_aux_data = 0;
}

/*!******************************************************************************
*  \brief  Queries device Serial number
*
*  \param[in] device_handle Device handle used to backtrace serial Num
*  \param[in] p_serial_num  Pointer to a caller allocated ni_logan_serial_num_t struct
*  \return On success
*                     NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                     NI_LOGAN_RETCODE_INVALID_PARAM
*                     NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*                     NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*******************************************************************************/
ni_logan_retcode_t ni_logan_device_handle_map_SN(ni_device_handle_t device_handle,
                                     ni_logan_serial_num_t *p_serial_num)
{
  ni_logan_nvme_result_t nvme_result = 0;
  ni_logan_nvme_command_t cmd = { 0 };
  void * p_buffer = NULL;
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if (NI_INVALID_DEVICE_HANDLE == device_handle || !p_serial_num)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Cannot allocate buffer.\n", __FUNCTION__);
    retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ);

#ifdef _WIN32
  event_handle = ni_logan_create_event();
  if (NI_INVALID_EVENT_HANDLE == event_handle)
  {
    retval = NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN;
  }
#endif

  uint32_t ui32LBA = IDENTIFY_DEVICE_R;
  if (ni_logan_nvme_send_read_cmd(device_handle, event_handle, p_buffer, NI_LOGAN_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
  {
    retval = NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  ni_logan_populate_serial_number(p_serial_num, p_buffer);

  END:

  ni_logan_aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "%s(): retval: %d\n", __FUNCTION__, retval);

  return retval;
}

/*!******************************************************************************
*  \brief  Copies existing decoding session params for hw frame usage
*
*  \param[in] src_p_ctx    Pointer to a caller allocated source
*                                               ni_logan_session_context_t struct
*  \param[in] dst_p_ctx    Pointer to a caller allocated destination
*                                               ni_logan_session_context_t struct
*  \return On success
*                          NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
ni_logan_retcode_t ni_logan_device_session_copy(ni_logan_session_context_t *src_p_ctx,
                                    ni_logan_session_context_t *dst_p_ctx)
{
  ni_logan_retcode_t retval;

  ni_logan_pthread_mutex_lock(&src_p_ctx->mutex);

  // Hold the lock of decoding session context here. 
  retval = ni_logan_decoder_session_copy_internal(src_p_ctx, dst_p_ctx);

  ni_logan_pthread_mutex_unlock(&src_p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
*  \brief  Reads data from the device
*          device_type should be NI_LOGAN_DEVICE_TYPE_DECODER,
*          and reads data hwdesc from decoder when hw transcoding
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_logan_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_logan_device_session_read_hwdesc(ni_logan_session_context_t *p_ctx,
                                  ni_logan_session_data_io_t *p_data)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  int seq_change_read_count = 0;

  if (!p_ctx || !p_data)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  for (; ;)
  {
    int aligned_width;
    //retval = ni_logan_decoder_session_read(p_ctx, &(p_data->data.frame));
    retval = ni_logan_decoder_session_read_desc(p_ctx, &(p_data->data.frame));
    // check resolution change only after initial setting obtained
    // p_data->data.frame.video_width is picture width and will be 32-align
    // adjusted to frame size; p_data->data.frame.video_height is the same as
    // frame size, then compare them to saved one for resolution checking
    //
    aligned_width = ((p_data->data.frame.video_width + 31) / 32) * 32;
    ni_log(NI_LOG_TRACE, "FNum %" PRIu64 ", DFVWxDFVH %u x %u, AlWid %d, "
           "AVW x AVH %u x %u\n ", p_ctx->frame_num,
           p_data->data.frame.video_width, p_data->data.frame.video_height,
           aligned_width, p_ctx->active_video_width, p_ctx->active_video_height);

    if (0 == retval && seq_change_read_count)
    {
      ni_log(NI_LOG_TRACE, "%s (decoder): seq change NO data, next time.\n",
             __FUNCTION__);
      p_ctx->active_video_width = 0;
      p_ctx->active_video_height = 0;
      p_ctx->active_bit_depth = 0;
      break;
    }
    else if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "%s (decoder): failure ret %d, return ..\n",
             __FUNCTION__, retval);
      break;
    }
    else if (p_ctx->frame_num && p_data->data.frame.video_width &&
             p_data->data.frame.video_height && p_data->data.frame.bit_depth &&
             (aligned_width != p_ctx->active_video_width ||
              p_data->data.frame.video_height != p_ctx->active_video_height ||
              p_data->data.frame.bit_depth != p_ctx->active_bit_depth))
    {
      ni_log(NI_LOG_TRACE, "%s (decoder): sequence change, frame size %ux%u %d"
             "bits -> %ux%u %dbits, continue read ...\n", __FUNCTION__,
             p_ctx->active_video_width, p_ctx->active_video_height,
             p_ctx->active_bit_depth, p_data->data.frame.video_width,
             p_data->data.frame.video_height, p_data->data.frame.bit_depth);
      // reset active video resolution to 0 so it can be queried in the re-read
      p_ctx->active_video_width = 0;
      p_ctx->active_video_height = 0;
      p_ctx->active_bit_depth = 0;
      seq_change_read_count++;
    }
    else
    {
      break;
    }
  }

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
*  \brief  Reads data from hw descriptor from decoder output buffer
*
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_logan_device_session_hwdl(ni_logan_session_context_t* p_ctx,
                           ni_logan_session_data_io_t *p_data,
                           ni_logan_hwframe_surface_t* hwdesc)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!hwdesc || !p_data)
  {
    ni_log(NI_LOG_ERROR, "%s(): Error passed parameters are null!, return\n",
           __FUNCTION__);
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  p_ctx->session_id = hwdesc->ui16SessionID;

#ifdef _WIN32
  int64_t handle = (((int64_t) hwdesc->device_handle_ext) << 32) | hwdesc->device_handle;
  p_ctx->blk_io_handle = (ni_device_handle_t) handle;
#else
  p_ctx->blk_io_handle = (ni_device_handle_t) hwdesc->device_handle;
#endif

  p_ctx->codec_format = hwdesc->encoding_type; //unused

  p_ctx->bit_depth_factor = hwdesc->bit_depth;
  p_ctx->hw_action = NI_LOGAN_CODEC_HW_DOWNLOAD;

  ni_log(NI_LOG_TRACE, "%s(): bit_depth_factor %u\n",
         __FUNCTION__, p_ctx->bit_depth_factor);

  retval = ni_logan_hwdownload_session_read(p_ctx, &(p_data->data.frame), hwdesc); //cut me down as needed

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
*  \brief  Writes data and reads back hw descriptor from decoder output buffer
*
*  \param[in] p_src_data   Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_logan_device_session_hwup(ni_logan_session_context_t* p_ctx,
                           ni_logan_session_data_io_t *p_src_data,
                           ni_logan_hwframe_surface_t* hwdesc)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  if (!hwdesc || !p_src_data)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  retval = ni_logan_hwupload_session_write(p_ctx, &p_src_data->data.frame);
  if (retval <= 0)
  {
    return retval;
  }
  retval = ni_logan_hwupload_session_read_hwdesc(p_ctx, hwdesc);

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}


/*!*****************************************************************************
*  \brief  Allocate memory for the frame buffer based on provided parameters
*          taking into account pic line size and extra data.
*          Applicable to YUV420p AVFrame for hw only.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_logan_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] linesize      Picture line size
*  \param[in] alignment     Allignment requirement
*  \param[in] extra_len     Extra data size (incl. meta data)
*
*  \return On success
*                          NI_LOGAN_RETCODE_SUCCESS
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
ni_logan_retcode_t ni_logan_frame_buffer_alloc_hwenc(ni_logan_frame_t* p_frame,
                                         int video_width,
                                         int video_height,
                                         int extra_len)
{
  void* p_buffer = NULL;
  int height_aligned = video_height;
  int retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_log(NI_LOG_TRACE, "%s: extra_len=%d\n", __FUNCTION__, extra_len);

  int luma_size = 0;//linesize[0] * height_aligned;
  int chroma_b_size = 0;//luma_size / 4;
  int chroma_r_size = 0;//luma_size / 4;
  int buffer_size = sizeof(ni_logan_hwframe_surface_t) + extra_len;

  buffer_size = ((buffer_size + (NI_LOGAN_MEM_PAGE_ALIGNMENT - 1)) / NI_LOGAN_MEM_PAGE_ALIGNMENT) * NI_LOGAN_MEM_PAGE_ALIGNMENT + NI_LOGAN_MEM_PAGE_ALIGNMENT;

  //Check if Need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
    ni_log(NI_LOG_TRACE, "%s: free current p_frame p_frame->buffer_size=%d\n",
           __FUNCTION__, p_frame->buffer_size);
    ni_logan_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
#ifndef XCODER_SIM_ENABLED
    if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate p_frame buffer.\n");
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#else
    p_buffer = malloc(buffer_size);
    if (!p_buffer)
    {
      ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate p_frame buffer.\n");
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
#endif
    // init once after allocation
    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;
    ni_log(NI_LOG_TRACE, "%s: allocated new p_frame buffer\n", __FUNCTION__);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s: reuse p_frame buffer\n", __FUNCTION__);
  }

  p_frame->p_data[3] = (uint8_t*)p_frame->p_buffer;
  p_frame->p_data[0] = NULL;
  p_frame->p_data[1] = NULL;
  p_frame->p_data[2] = NULL;

  p_frame->data_len[0] = 0;//luma_size;
  p_frame->data_len[1] = 0;//chroma_b_size;
  p_frame->data_len[2] = 0;//chroma_r_size;
  p_frame->data_len[3] = sizeof(ni_logan_hwframe_surface_t);

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "%s: success: p_frame->buffer_size=%d\n",
         __FUNCTION__, p_frame->buffer_size);

  END:

  if (NI_LOGAN_RETCODE_SUCCESS != retval)
  {
    ni_logan_aligned_free(p_buffer);
  }

  return retval;
}

/*!******************************************************************************
*  \brief  Recycle a frame buffer on card
*
*  \param[in] surface    Stuct containing device and frame location to clear out
*
*  \return On success    NI_LOGAN_RETCODE_SUCCESS
*          On failure    NI_LOGAN_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_logan_retcode_t ni_logan_decode_buffer_free(ni_logan_hwframe_surface_t* surface,
                                   ni_device_handle_t device_handle,
                                   ni_event_handle_t event_handle)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;
  if (surface)
  {
    if (surface->seq_change)
    {
      // clear meta buffer index when sequence change happened
      ni_logan_hwframe_surface_t surface_meta = {0};
      surface_meta.ui16SessionID = surface->ui16SessionID;
      surface_meta.i8FrameIdx = NI_LOGAN_INVALID_HW_META_IDX;
      surface_meta.i8InstID = surface->i8InstID;
      ni_log(NI_LOG_TRACE, "%s(): clear meta buffer\n", __FUNCTION__);
      retval = ni_logan_clear_instance_buf(&surface_meta, device_handle, event_handle);
    }
    ni_log(NI_LOG_TRACE, "%s(): Start cleaning out buffer\n", __FUNCTION__);
    ni_log(NI_LOG_TRACE, "%s(): ui16FrameIdx=%d sessionId=%d device_handle=0x%x\n",
           __FUNCTION__, surface->i8FrameIdx, surface->ui16SessionID, device_handle);
    retval = ni_logan_clear_instance_buf(surface, device_handle, event_handle);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s(): Surface is empty\n", __FUNCTION__);
  }
  return retval;
}

/*!******************************************************************************
*  \brief  Sends frame pool setup info to device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_logan_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_logan_session_data_io_t struct which contains either a
*                          ni_logan_frame_t data frame or ni_logan_packet_t data packet to send
*  \return On success      Return code
*          On failure
*                          NI_LOGAN_RETCODE_INVALID_PARAM
*                          NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_LOGAN_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_logan_device_session_init_framepool(ni_logan_session_context_t *p_ctx,
                                     uint32_t pool_size)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if (!p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  ni_logan_pthread_mutex_lock(&p_ctx->mutex);

  retval = ni_logan_hwupload_init_framepool(p_ctx, pool_size);

  ni_logan_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!******************************************************************************
 *  \brief   Allocate memory for the frame buffer based on provided parameters
 *           taking into account width, height, format, stride, alignment, and
 *           extra data
 *
 *  \param[in]  p_frame         Pointer to caller allocated ni_logan_frame_t
 *  \param[in]  pixel_format    pixel format
 *  \param[in]  video_width     width, in pixels
 *  \param[in]  video_height    height, in pixels
 *  \param[in]  linesize        horizontal stride
 *  \param[in]  alignment       apply a 16 pixel height alignment (T408 only)
 *  \param[in]  extra_len       meta data size
 *
 *  \return     NI_LOGAN_RETCODE_SUCCESS
 *              NI_LOGAN_RETCODE_INVALID_PARAM
 *              NI_LOGAN_RETCODE_ERROR_MEM_ALOC
 *
 *******************************************************************************/
ni_logan_retcode_t ni_logan_frame_buffer_alloc_v4(ni_logan_frame_t* p_frame, int pixel_format,
                                      int video_width, int video_height,
                                      int linesize[], int alignment, int extra_len)
{
  int buffer_size;
  void *p_buffer = NULL;
  int retval = NI_LOGAN_RETCODE_SUCCESS;
  int height_aligned;
  int luma_size, chroma_b_size, chroma_r_size;

  if (!p_frame)
  {
    ni_log(NI_LOG_ERROR, "Invalid frame pointer\n");
    return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  switch (pixel_format)
  {
    case NI_LOGAN_PIX_FMT_YUV420P:
    case NI_LOGAN_PIX_FMT_YUV420P10LE:
      /*
       * The encoder will pad a small frame up to 256x128 before encoding
       * and set the cropping value in the packet header to the original
       * dimension.
       */
      if ((video_width < 0) || (video_width > NI_LOGAN_MAX_RESOLUTION_WIDTH))
      {
        ni_log(NI_LOG_ERROR, "Video resolution width %d out of range\n", video_width);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }

      if ((video_height < 0) || (video_height > NI_LOGAN_MAX_RESOLUTION_HEIGHT))
      {
        ni_log(NI_LOG_ERROR, "Video resolution height %d out of range\n", video_width);
        return NI_LOGAN_RETCODE_INVALID_PARAM;
      }
      break;
    default:
      ni_log(NI_LOG_ERROR, "Unknown pixel format %d\n", pixel_format);
      return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  height_aligned = NI_LOGAN_VPU_ALIGN8(video_height);

  if (alignment)
  {
    /* 16-pixel aligned pixel height for Quadra */
    height_aligned = NI_LOGAN_VPU_ALIGN16(video_height);
  }

  /* Round up to the minimum 256 pixel height if necessary */
  height_aligned = height_aligned > NI_LOGAN_MIN_HEIGHT? height_aligned : NI_LOGAN_MIN_HEIGHT;

  switch (pixel_format)
  {
    case NI_LOGAN_PIX_FMT_YUV420P:
    case NI_LOGAN_PIX_FMT_YUV420P10LE:
      luma_size = linesize[0] * height_aligned;
      chroma_b_size = luma_size / 4;
      chroma_r_size = luma_size / 4;

      buffer_size = luma_size + chroma_b_size + chroma_r_size + extra_len;
      break;
    default:
      ni_log(NI_LOG_ERROR, "Error: unsupported pixel format %d\n", pixel_format);
      return NI_LOGAN_RETCODE_INVALID_PARAM;
  }

  /* Allocate a buffer size that is page aligned for the host */
  buffer_size = NI_LOGAN_VPU_CEIL(buffer_size,NI_LOGAN_MEM_PAGE_ALIGNMENT) + NI_LOGAN_MEM_PAGE_ALIGNMENT;

  /* If this buffer has a different size, realloc a new buffer */
  if ((p_frame->buffer_size > 0) && (p_frame->buffer_size != buffer_size))
  {
    ni_log(NI_LOG_TRACE, "Free current p_frame, p_frame->buffer_size %d\n",
           p_frame->buffer_size);
    ni_logan_frame_buffer_free(p_frame);
  }

  if (p_frame->buffer_size != buffer_size)
  {
    if (ni_logan_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "Error: Cannot allocate p_frame\n");
      retval = NI_LOGAN_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;
    ni_log(NI_LOG_TRACE, "%s: allocated new p_frame buffer\n", __FUNCTION__);
  }
  else
  {
    ni_log(NI_LOG_TRACE, "%s: reuse p_frame buffer\n", __FUNCTION__);
  }

  switch (pixel_format)
  {
    case NI_LOGAN_PIX_FMT_YUV420P:
    case NI_LOGAN_PIX_FMT_YUV420P10LE:
      p_frame->p_data[0] = (uint8_t*) p_frame->p_buffer;
      p_frame->p_data[1] = (uint8_t*) p_frame->p_data[0] + luma_size;
      p_frame->p_data[2] = (uint8_t*) p_frame->p_data[1] + chroma_b_size;
      p_frame->p_data[3] = NULL;

      p_frame->data_len[0] = luma_size;
      p_frame->data_len[1] = chroma_b_size;
      p_frame->data_len[2] = chroma_r_size;
      p_frame->data_len[3] = 0;
      break;
      /* fall through */
    default:
      ni_log(NI_LOG_ERROR, "Error: unsupported pixel format %d\n",pixel_format);
      retval = NI_LOGAN_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  p_frame->video_width  = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_TRACE, "%s success: w=%d; h=%d; aligned buffer size=%d\n",
         __FUNCTION__, video_width, video_height, buffer_size);

  END:

  if (retval != NI_LOGAN_RETCODE_SUCCESS)
  {
    ni_logan_aligned_free(p_buffer);
  }

  return retval;
}
