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
 *  \file   ni_device_api.c
 *
 *  \brief  Public definitions for operating NETINT video processing devices for
 *          video processing
 ******************************************************************************/

#if __linux__ || __APPLE__
#define _GNU_SOURCE //O_DIRECT is Linux-specific.  One must define _GNU_SOURCE to obtain its definitions
#if __linux__
#include <linux/types.h>
#endif
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "inttypes.h"
#include "ni_device_api.h"
#include "ni_device_api_priv.h"
#include "ni_nvme.h"
#include "ni_util.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_lat_meas.h"
#ifdef _WIN32
#include <shlobj.h>
#else
#include "ni_p2p_ioctl.h"
#endif

const char *const g_xcoder_preset_names[NI_XCODER_PRESET_NAMES_ARRAY_LEN] = {
    NI_XCODER_PRESET_NAME_DEFAULT, NI_XCODER_PRESET_NAME_CUSTOM, 0};
const char *const g_xcoder_log_names[NI_XCODER_LOG_NAMES_ARRAY_LEN] = {
    NI_XCODER_LOG_NAME_NONE,
    NI_XCODER_LOG_NAME_ERROR,
    NI_XCODER_LOG_NAME_WARN,
    NI_XCODER_LOG_NAME_INFO,
    NI_XCODER_LOG_NAME_DEBUG,
    NI_XCODER_LOG_NAME_FULL,
    0};
#if __linux__ || __APPLE__
static struct stat g_nvme_stat = { 0 };

static int close_fd_zero_atexit = 0;

static void close_fd_zero(void)
{
    close(0);
}

#endif

/*!*****************************************************************************
 *  \brief  Allocate and initialize a new ni_session_context_t struct
 *
 *
 *  \return On success returns a valid pointer to newly allocated context
 *          On failure returns NULL
 ******************************************************************************/
ni_session_context_t *ni_device_session_context_alloc_init(void)
{
    ni_session_context_t *p_ctx = NULL;

    p_ctx = malloc(sizeof(ni_session_context_t));
    if (!p_ctx)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s() Failed to allocate memory for session context\n",
               __func__);
    } else
    {
        memset(p_ctx, 0, sizeof(ni_session_context_t));

        if (ni_device_session_context_init(p_ctx) != NI_RETCODE_SUCCESS)
        {
            ni_log(NI_LOG_ERROR, "ERROR: %s() Failed to init session context\n",
                   __func__);
            ni_device_session_context_free(p_ctx);
            return NULL;
        }
    }
    return p_ctx;
}

/*!*****************************************************************************
 *  \brief  Free previously allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *              struct
 *
 ******************************************************************************/
void ni_device_session_context_free(ni_session_context_t *p_ctx)
{
    if (p_ctx)
    {
        ni_device_session_context_clear(p_ctx);
#ifdef MEASURE_LATENCY
        if (p_ctx->frame_time_q)
        {
            ni_lat_meas_q_destroy(p_ctx->frame_time_q);
        }
#endif
        free(p_ctx);
    }
}

/*!*****************************************************************************
 *  \brief  Initialize already allocated session context to a known state
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *              struct
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_FAILURE
 ******************************************************************************/
ni_retcode_t ni_device_session_context_init(ni_session_context_t *p_ctx)
{
    int bitrate = 0;
    int framerate_num = 0;
    int framerate_denom = 0;

    if (!p_ctx)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        // session context will reset so save the last values for sequence change
        bitrate = p_ctx->last_bitrate;
        framerate_num = p_ctx->last_framerate.framerate_num;
        framerate_denom = p_ctx->last_framerate.framerate_denom;
    }

    memset(p_ctx, 0, sizeof(ni_session_context_t));

    p_ctx->last_bitrate = bitrate;
    p_ctx->last_framerate.framerate_num = framerate_num;
    p_ctx->last_framerate.framerate_denom = framerate_denom;

    // Xcoder thread mutex init
    if (ni_pthread_mutex_init(&p_ctx->mutex))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): init xcoder_mutex fail, return\n",
               __func__);
        return NI_RETCODE_FAILURE;
    }
    p_ctx->pext_mutex = &p_ctx->mutex; //used exclusively for hwdl

    // low delay send/recv sync init
    if (ni_pthread_mutex_init(&p_ctx->low_delay_sync_mutex))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
               "ERROR %s(): init xcoder_low_delay_sync_mutex fail return\n",
               __func__);
        return NI_RETCODE_FAILURE;
    }
    if (ni_pthread_cond_init(&p_ctx->low_delay_sync_cond, NULL))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
               "ERROR %s(): init xcoder_low_delay_sync_cond fail return\n",
               __func__);
        return NI_RETCODE_FAILURE;
    }
    
    p_ctx->mutex_initialized = true;

    // Init the max IO size to be invalid
    p_ctx->max_nvme_io_size = NI_INVALID_IO_SIZE;
    p_ctx->session_id = NI_INVALID_SESSION_ID;
    p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
    p_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_ctx->hw_id = NI_INVALID_HWID;
    p_ctx->event_handle = NI_INVALID_EVENT_HANDLE;
    p_ctx->thread_event_handle = NI_INVALID_EVENT_HANDLE;
    p_ctx->xcoder_state = NI_XCODER_IDLE_STATE;
    p_ctx->keep_alive_thread = (ni_pthread_t){0};
    p_ctx->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;
    p_ctx->decoder_low_delay = 0;
    p_ctx->enable_low_delay_check = 0;
    p_ctx->low_delay_sync_flag = 0;
    p_ctx->async_mode = 0;
    p_ctx->pixel_format = NI_PIX_FMT_YUV420P;
    // by default, select the least model load card
    strncpy(p_ctx->dev_xcoder_name, NI_BEST_MODEL_LOAD_STR,
            MAX_CHAR_IN_DEVICE_NAME);
#ifdef MY_SAVE
    p_ctx->debug_write_ptr = NULL;
    p_ctx->debug_write_index_ptr = NULL;
    p_ctx->debug_write_sent_size = 0;
#endif

#ifdef MEASURE_LATENCY
    p_ctx->frame_time_q = (void *)ni_lat_meas_q_create(2000);
#endif

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Clear already allocated session context
 *
 *  \param[in]  p_ctx Pointer to an already allocated ni_session_context_t
 *
 *
 ******************************************************************************/
void ni_device_session_context_clear(ni_session_context_t *p_ctx)
{
    if(p_ctx->mutex_initialized)
    {
        p_ctx->mutex_initialized = false;
        ni_pthread_mutex_destroy(&p_ctx->mutex);
        ni_pthread_mutex_destroy(&p_ctx->low_delay_sync_mutex);
        ni_pthread_cond_destroy(&p_ctx->low_delay_sync_cond);
    }
}

/*!*****************************************************************************
 *  \brief  Create event and return event handle if successful (Windows only)
 *
 *  \return On success returns a event handle
 *          On failure returns NI_INVALID_EVENT_HANDLE
 ******************************************************************************/
ni_event_handle_t ni_create_event(void)
{
#ifdef _WIN32
    ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

    // Input-0 determines whether the returned handle can be inherited by the child process.If lpEventAttributes is NULL, this handle cannot be inherited.
    // Input-1 specifies whether the event object is created to be restored manually or automatically.If set to FALSE, when a thread waits for an event signal, the system automatically restores the event state to a non-signaled state.
    // Input-2 specifies the initial state of the event object.If TRUE, the initial state is signaled;Otherwise, no signal state.
    // Input-3 If the lpName is NULL, a nameless event object is created.。
    event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (event_handle == NULL)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() create event failed\n",
               NI_ERRNO, __func__);
        return NI_INVALID_EVENT_HANDLE;
    }
  return event_handle;
#else
  return NI_INVALID_EVENT_HANDLE;
#endif
}

/*!*****************************************************************************
 *  \brief  Close event and release resources (Windows only)
 *
 *  \return NONE
 *
 ******************************************************************************/
void ni_close_event(ni_event_handle_t event_handle)
{
  if ( NI_INVALID_DEVICE_HANDLE == event_handle )
  {
      ni_log(NI_LOG_DEBUG, "Warning %s: null parameter passed %x\n", __func__,
             event_handle);
      return;
  }

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

#ifdef _WIN32
  BOOL retval;
  ni_log(NI_LOG_DEBUG, "%s(): closing %p\n", event_handle);

  retval = CloseHandle(event_handle);
  if (FALSE == retval)
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s(): closing event_handle %p failed\n",
             NI_ERRNO, __func__, event_handle);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s(): device %p closed successfuly\n", __func__,
             event_handle);
  }
#else
  int err = 0;
  ni_log(NI_LOG_DEBUG, "%s(): closing %d\n", __func__, event_handle);
  err = close(event_handle);
  if (err)
  {
      char error_message[100] = {'\0'};
      char unknown_error_message[20] = {'\0'};
      sprintf(error_message, "ERROR: %s(): ", __func__);
      switch (err)
      {
          case EBADF:
              strcat(error_message, "EBADF\n");
              break;
          case EINTR:
              strcat(error_message, "EINTR\n");
              break;
          case EIO:
              strcat(error_message, "EIO\n");
              break;
          default:
              sprintf(unknown_error_message, "Unknown error %d\n", err);
              strcat(error_message, unknown_error_message);
      }
      ni_log(NI_LOG_ERROR, "%s\n", error_message);
  }
#endif
  ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
}

/*!*****************************************************************************
 *  \brief  Open device and return device device_handle if successful
 *
 *  \param[in]  p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_max_io_size_out Maximum IO Transfer size supported, could be
 *              NULL
 *
 *  \return On success returns a device device_handle
 *          On failure returns NI_INVALID_DEVICE_HANDLE
 ******************************************************************************/
ni_device_handle_t ni_device_open(const char * p_dev, uint32_t * p_max_io_size_out)
{
#ifdef _WIN32
    DWORD retval;
    HANDLE device_handle;

    if (!p_dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
        return NI_INVALID_DEVICE_HANDLE;
    }

    if (p_max_io_size_out != NULL && *p_max_io_size_out == NI_INVALID_IO_SIZE)
    {
        // For now, we just use it to allocate p_leftover buffer
        // NI_MAX_PACKET_SZ is big enough
        *p_max_io_size_out = NI_MAX_PACKET_SZ;
    }

    device_handle = CreateFile(p_dev, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

    ni_log(NI_LOG_DEBUG, "%s() device_name: %s", __func__, p_dev);
    if (INVALID_HANDLE_VALUE == device_handle)
    {
        retval = GetLastError();
        ni_log(NI_LOG_ERROR, "Failed to open %s, retval %d \n", p_dev, retval);
    } else
    {
        ni_log(NI_LOG_DEBUG, "Found NVME Controller at %s \n", p_dev);
    }

    return device_handle;
#else
    int retval = -1;
    ni_device_handle_t fd = NI_INVALID_DEVICE_HANDLE;

    if (!p_dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_max_io_size_out != NULL && *p_max_io_size_out == NI_INVALID_IO_SIZE)
    {
	#if __linux__
            *p_max_io_size_out = ni_get_kernel_max_io_size(p_dev);
	#elif __APPLE__
            *p_max_io_size_out = MAX_IO_TRANSFER_SIZE;
	#endif
    }

    ni_log(NI_LOG_DEBUG, "%s: opening regular-io enabled %s\n", __func__,
           p_dev);
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
            ni_log(NI_LOG_ERROR, "ERROR: %d %s open() failed on %s\n", NI_ERRNO,
                   strerror(NI_ERRNO), p_dev);
            ni_log(NI_LOG_ERROR, "ERROR: %s() failed!\n", __func__);
            fd = NI_INVALID_DEVICE_HANDLE;
            LRETURN;
    }
    else if(fd == 0)
    {
            //this code is just for the case that we do not initialize all fds to NI_INVALID_DEVICE_HANDLE

            //if we make sure that all the fds in libxcoder is initialized to NI_INVALID_DEVICE_HANDLE
            //we should remove this check

            //we hold the fd = 0, so other threads could not visit these code
            //thread safe unless other thread close fd=0 accidently
            ni_log(NI_LOG_ERROR, "open fd = 0 is not as expeceted\n");
            if(close_fd_zero_atexit == 0)
            {
              close_fd_zero_atexit = 1;
              atexit(close_fd_zero);
            }
            else
            {
              ni_log(NI_LOG_ERROR, "libxcoder has held the fd=0, but open fd=0 again, maybe fd=0 was closed accidently.");
            }
            fd = NI_INVALID_DEVICE_HANDLE;
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
        ni_log(NI_LOG_ERROR, "ERROR: fstat() failed on %s\n", p_dev);
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed!\n", __func__);
        close(fd);
        fd = NI_INVALID_DEVICE_HANDLE;
        LRETURN;
    }

    if (!S_ISCHR(g_nvme_stat.st_mode) && !S_ISBLK(g_nvme_stat.st_mode))
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s is not a block or character device\n",
               p_dev);
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed!\n", __func__);
        close(fd);
        fd = NI_INVALID_DEVICE_HANDLE;
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG, "%s: success, fd=%d\n", __func__, fd);

END:

    return fd;
#endif
}

/*!*****************************************************************************
 *  \brief  Close device and release resources
 *
 *  \param[in] device_handle Device handle obtained by calling ni_device_open()
 *
 *  \return NONE
 *
 ******************************************************************************/
void ni_device_close(ni_device_handle_t device_handle)
{
  if ( NI_INVALID_DEVICE_HANDLE == device_handle )
  {
      ni_log(NI_LOG_ERROR, "ERROR %s: null parameter passed %x\n", __func__,
             device_handle);
      return;
  }

  if(device_handle == 0)
  {
      //this code is just for the case that we do not initialize all fds to NI_INVALID_DEVICE_HANDLE

      //if we make sure that all the fds in libxcoder is initialized to NI_INVALID_DEVICE_HANDLE
      //we should remove this check
      ni_log(NI_LOG_ERROR, "%s close fd=0 is not as expected", __func__);
      return;
  }

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

#ifdef _WIN32
  BOOL retval;

  ni_log(NI_LOG_DEBUG, "%s(): closing %p\n", __func__, device_handle);

  retval = CloseHandle(device_handle);
  if (FALSE == retval)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR: %s(): closing device device_handle %p failed, error: %d\n",
             __func__, device_handle, NI_ERRNO);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s(): device %p closed successfuly\n", __func__,
             device_handle);
  }
#else
  int err = 0;
  ni_log(NI_LOG_DEBUG, "%s(): closing fd %d\n", __func__, device_handle);
  err = close(device_handle);
  if (err == -1)
  {
      char error_message[100] = {'\0'};
      char unknown_error_message[20] = {'\0'};
      sprintf(error_message, "ERROR: %s(): ", __func__);
      switch (errno)
      {
          case EBADF:
              strcat(error_message, "EBADF\n");
              break;
          case EINTR:
              strcat(error_message, "EINTR\n");
              break;
          case EIO:
              strcat(error_message, "EIO\n");
              break;
          default:
              sprintf(unknown_error_message, "Unknown error %d\n", err);
              strcat(error_message, unknown_error_message);
      }
      ni_log(NI_LOG_ERROR, "%s\n", strerror(errno));
      ni_log(NI_LOG_ERROR, "%s\n", error_message);
  }
#endif
  ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
}

/*!*****************************************************************************
 *  \brief  Query device and return device capability structure
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] p_cap  Pointer to a caller allocated ni_device_capability_t
 *                    struct
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_ERROR_MEM_ALOC
 *                     NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_capability_query(ni_device_handle_t device_handle, ni_device_capability_t* p_cap)
{
  void * p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if ( (NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_cap) )
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters are null, return\n",
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

  uint32_t ui32LBA = IDENTIFY_DEVICE_R;
  if (ni_nvme_send_read_cmd(device_handle, event_handle, p_buffer, NI_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  ni_populate_device_capability_struct(p_cap, p_buffer);

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_DEBUG, "%s(): retval: %d\n", __func__, retval);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Open a new device session depending on the device_type parameter
 *          If device_type is NI_DEVICE_TYPE_DECODER opens decoding session
 *          If device_type is NI_DEVICE_TYPE_ENCODER opens encoding session
 *          If device_type is NI_DEVICE_TYPE_SCALER opens scaling session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER,
 *                          or NI_DEVICE_TYPE_SCALER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_open(ni_session_context_t *p_ctx,
                                    ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_context_t *p_device_context = NULL;
  ni_device_info_t *p_dev_info = NULL;
  ni_device_info_t dev_info = { 0 };
  /*! resource management context */
  ni_device_context_t *rsrc_ctx = NULL;
  int i = 0;
  int rc = 0;
  int num_coders = 0;
  bool use_model_load = true;
  int least_load = 0;
  int curr_load = 0;
  int guid = -1;
  uint32_t num_sw_instances = 0;
  int user_handles = false;
  ni_lock_handle_t lock = NI_INVALID_LOCK_HANDLE;
  ni_device_handle_t handle = NI_INVALID_DEVICE_HANDLE;
  ni_device_handle_t handle1 = NI_INVALID_DEVICE_HANDLE;
  // For none nvme block device we just need to pass in dummy
  uint32_t dummy_io_size = 0;
  ni_session_context_t p_session_context = {0};
  ni_device_type_t query_type = device_type;

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

#ifdef _WIN32
  if (!IsUserAnAdmin())
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s must be in admin priviledge\n", __func__);
      return NI_RETCODE_ERROR_PERMISSION_DENIED;
  }
#endif

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_OPEN_STATE;

  ni_device_session_context_init(&p_session_context);

  if (NI_INVALID_SESSION_ID != p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: trying to overwrite existing session, "
             "device_type %d, session id %d\n",
             device_type, p_ctx->session_id);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  p_ctx->p_hdr_buf = NULL;
  p_ctx->hdr_buf_size = 0;

  p_ctx->roi_side_data_size = p_ctx->nb_rois = 0;
  p_ctx->av_rois = NULL;
  p_ctx->roi_map = NULL;
  p_ctx->avc_roi_map = NULL;
  p_ctx->hevc_roi_map = NULL;
  p_ctx->hevc_sub_ctu_roi_buf = NULL;
  p_ctx->p_master_display_meta_data = NULL;

  p_ctx->enc_change_params = NULL;

  p_ctx->target_bitrate = -1;
  p_ctx->force_idr_frame = 0;
  p_ctx->ltr_to_set.use_cur_src_as_long_term_pic = 0;
  p_ctx->ltr_to_set.use_long_term_ref = 0;
  p_ctx->ltr_interval = -1;
  p_ctx->ltr_frame_ref_invalid = -1;
  p_ctx->framerate.framerate_num = 0;
  p_ctx->framerate.framerate_denom = 0;
  p_ctx->vui.colorDescPresent = 0;
  p_ctx->vui.colorPrimaries = 2;  // 2 is unspecified
  p_ctx->vui.colorTrc = 2;        // 2 is unspecified
  p_ctx->vui.colorSpace = 2;      // 2 is unspecified
  p_ctx->vui.aspectRatioWidth = 0;
  p_ctx->vui.aspectRatioHeight = 0;
  p_ctx->vui.videoFullRange = 0;
  p_ctx->max_frame_size = 0;
  p_ctx->reconfig_crf = -1;
  p_ctx->reconfig_crf_decimal = 0;
  p_ctx->reconfig_vbv_buffer_size = 0;
  p_ctx->reconfig_vbv_max_rate = 0;
  p_ctx->last_gop_size = 0;
  p_ctx->initial_frame_delay = 0;
  p_ctx->current_frame_delay = 0;
  p_ctx->max_frame_delay = 0;
  p_ctx->av1_pkt_num = 0;
  p_ctx->pool_type = NI_POOL_TYPE_NONE;
  p_ctx->force_low_delay = false;
  p_ctx->force_low_delay_cnt = 0;
  p_ctx->pkt_delay_cnt = 0;
  p_ctx->reconfig_intra_period = -1;
  p_ctx->reconfig_slice_arg = 0;

  handle = p_ctx->device_handle;
  handle1 = p_ctx->blk_io_handle;

  p_device_pool = ni_rsrc_get_device_pool();

  if (!p_device_pool)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: Error calling ni_rsrc_get_device_pool()\n");
    retval =  NI_RETCODE_ERROR_GET_DEVICE_POOL;
    LRETURN;
  }

  ni_log2(p_ctx, NI_LOG_DEBUG, 
         "%s: device type %d hw_id %d blk_dev_name: %s dev_xcoder_name: %s.\n",
         __func__, device_type, p_ctx->hw_id, p_ctx->blk_dev_name,
         p_ctx->dev_xcoder_name);

  if (device_type == NI_DEVICE_TYPE_UPLOAD)
  {
      //uploader shares resources with encoder to query as encoder for load info
      query_type = NI_DEVICE_TYPE_ENCODER;
  }

  // if caller requested device by block device name, try to find its GUID and
  // use it to override the hw_id which is the passed in device GUID
  if (0 != strcmp(p_ctx->blk_dev_name, ""))
  {
      int tmp_guid_id;
      tmp_guid_id =
          ni_rsrc_get_device_by_block_name(p_ctx->blk_dev_name, device_type);
      if (tmp_guid_id != NI_RETCODE_FAILURE)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG, 
                 "%s: block device name %s type %d guid %d is to "
                 "override passed in guid %d\n",
                 __func__, p_ctx->blk_dev_name, device_type, tmp_guid_id,
                 p_ctx->hw_id);
          p_ctx->hw_id = tmp_guid_id;
      } else
      {
          ni_log(NI_LOG_INFO, "%s: block device name %s type %d NOT found ..\n",
                 __func__, p_ctx->blk_dev_name, device_type);
      }
  }

  // User did not pass in any handle, so we create it for them
  if ((handle1 == NI_INVALID_DEVICE_HANDLE) && (handle == NI_INVALID_DEVICE_HANDLE))
  {
    if (p_ctx->hw_id >=0) // User selected the encder/ decoder number
    {
      if ((rsrc_ctx = ni_rsrc_allocate_simple_direct(device_type, p_ctx->hw_id)) == NULL)
      {

        ni_log2(p_ctx, NI_LOG_ERROR,  "Error XCoder resource allocation: inst %d\n",
               p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        LRETURN;
      }
      ni_log2(p_ctx, NI_LOG_DEBUG,  "device %p\n", rsrc_ctx);
      // Now the device name is in the rsrc_ctx, we open this device to get the file handles

#ifdef _WIN32
      if ((handle = ni_device_open(rsrc_ctx->p_device_info->blk_name,
                                   &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE)
      {
          ni_rsrc_free_device_context(rsrc_ctx);
          retval = NI_RETCODE_ERROR_OPEN_DEVICE;
          LRETURN;
      } else
      {
          user_handles = true;
          p_ctx->device_handle = handle;
          p_ctx->blk_io_handle = handle;
          p_ctx->max_nvme_io_size = dummy_io_size;
          handle1 = handle;
          strcpy(p_ctx->dev_xcoder_name, rsrc_ctx->p_device_info->dev_name);
          strcpy(p_ctx->blk_xcoder_name, rsrc_ctx->p_device_info->blk_name);
          ni_rsrc_free_device_context(rsrc_ctx);
      }
#else
      //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice.
      if (((handle = ni_device_open(rsrc_ctx->p_device_info->dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(rsrc_ctx->p_device_info->dev_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
      {
          ni_rsrc_free_device_context(rsrc_ctx);
          retval = NI_RETCODE_ERROR_OPEN_DEVICE;
          LRETURN;
      } else
      {
        user_handles = true;
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle1;
        // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker): Couldn't determine why it will be NULL.
        strcpy(p_ctx->dev_xcoder_name, rsrc_ctx->p_device_info->dev_name);
        strcpy(p_ctx->blk_xcoder_name, rsrc_ctx->p_device_info->blk_name);

        ni_rsrc_free_device_context(rsrc_ctx);
      }
#endif
    } else
    {
        int tmp_id = -1;

        // Decide on model load or real load to be used, based on name passed
        // in from p_ctx->dev_xcoder_name; after checking it will be used to
        // store device file name.
        if (0 == strcmp(p_ctx->dev_xcoder_name, NI_BEST_REAL_LOAD_STR))
        {
            use_model_load = false;
        } else if (0 != strcmp(p_ctx->dev_xcoder_name, NI_BEST_MODEL_LOAD_STR))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s unrecognized option: %s.\n",
                   __func__, p_ctx->dev_xcoder_name);
            retval = NI_RETCODE_INVALID_PARAM;
            LRETURN;
        }

        if (ni_rsrc_lock_and_open(query_type, &lock) != NI_RETCODE_SUCCESS)
        {
            retval = NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
            LRETURN;
        }

        // We need to query through all the boards to confirm the least load.

        if (IS_XCODER_DEVICE_TYPE(query_type))
        {
            num_coders = p_device_pool->p_device_queue->xcoder_cnt[query_type];
        } else
        {
            retval = NI_RETCODE_INVALID_PARAM;
            if (ni_rsrc_unlock(query_type, lock) != NI_RETCODE_SUCCESS)
            {
                retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
                LRETURN;
            }
            LRETURN;
        }

        for (i = 0; i < num_coders; i++)
        {
            tmp_id = p_device_pool->p_device_queue->xcoders[query_type][i];
            p_device_context = ni_rsrc_get_device_context(query_type, tmp_id);

            if (p_device_context == NULL)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, 
                       "ERROR: %s() ni_rsrc_get_device_context() failed\n",
                       __func__);
                continue;
            }

            // Code is included in the for loop. In the loop, the device is
            // just opened once, and it will be closed once too.
            p_session_context.blk_io_handle = ni_device_open(
                p_device_context->p_device_info->dev_name, &dummy_io_size);
            p_session_context.device_handle = p_session_context.blk_io_handle;

            if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
            {
                ni_log2(p_ctx, NI_LOG_ERROR,  "Error open device");
                ni_rsrc_free_device_context(p_device_context);
                continue;
            }

            p_session_context.hw_id = p_device_context->p_device_info->hw_id;
            rc = ni_device_session_query(&p_session_context, query_type);
            if (NI_INVALID_DEVICE_HANDLE != p_session_context.device_handle)
            {
                ni_device_close(p_session_context.device_handle);
            }

            if (NI_RETCODE_SUCCESS != rc)
            {
                ni_log2(p_ctx, NI_LOG_ERROR,  "Error query %s %s.%d\n",
                       g_device_type_str[query_type],
                       p_device_context->p_device_info->dev_name,
                       p_device_context->p_device_info->hw_id);
                ni_rsrc_free_device_context(p_device_context);
                continue;
            }
            ni_rsrc_update_record(p_device_context, &p_session_context);
            p_dev_info = p_device_context->p_device_info;

            // here we select the best load
            // for decoder/encoder: check the model_load/real_load
            // for hwuploader: check directly hwupload count in query result
            if (NI_DEVICE_TYPE_UPLOAD == device_type)
            {
                if (i == 0 ||
                    p_session_context.load_query.active_hwuploaders <
                        num_sw_instances)
                {
                    guid = tmp_id;
                    num_sw_instances =
                        p_session_context.load_query.active_hwuploaders;
                    memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
                }
            } else
            {
                if (use_model_load)
                {
                    curr_load = p_dev_info->model_load;
                } else
                {
                    curr_load = p_dev_info->load;
                }

                if (i == 0 || curr_load < least_load ||
                    (curr_load == least_load &&
                     p_dev_info->active_num_inst < num_sw_instances))
                {
                    guid = tmp_id;
                    least_load = curr_load;
                    num_sw_instances = p_dev_info->active_num_inst;
                    memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
                }
            }
            ni_rsrc_free_device_context(p_device_context);
        }

#ifdef _WIN32
        // Now we have the device info that has the least load of the FW
        // we open this device and assign the FD
        if ((handle = ni_device_open(dev_info.blk_name, &dummy_io_size)) ==
            NI_INVALID_DEVICE_HANDLE)
        {
            retval = NI_RETCODE_ERROR_OPEN_DEVICE;
            if (ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
            {
                retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
                LRETURN;
            }
            LRETURN;
      } else
      {
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle;
        p_ctx->max_nvme_io_size = dummy_io_size;
        handle1 = handle;
        p_ctx->hw_id = guid;
        strcpy(p_ctx->dev_xcoder_name, dev_info.dev_name);
        strcpy(p_ctx->blk_xcoder_name, dev_info.blk_name);
      }
#else
      //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
      //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice.
      if (((handle = ni_device_open(dev_info.dev_name, &dummy_io_size)) == NI_INVALID_DEVICE_HANDLE) ||
          ((handle1 = ni_device_open(dev_info.dev_name, &p_ctx->max_nvme_io_size)) == NI_INVALID_DEVICE_HANDLE))
      {
        retval = NI_RETCODE_ERROR_OPEN_DEVICE;
        if (ni_rsrc_unlock(query_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
        LRETURN;
      }
      else
      {
        p_ctx->device_handle = handle;
        p_ctx->blk_io_handle = handle1;
        p_ctx->hw_id = guid;
        strcpy(p_ctx->dev_xcoder_name, dev_info.dev_name);
        strcpy(p_ctx->blk_xcoder_name, dev_info.blk_name);
      }
#endif
    }
  }
  // user passed in the handle, but one of them is invalid, this is error case so we return error
  else if((handle1 == NI_INVALID_DEVICE_HANDLE) || (handle == NI_INVALID_DEVICE_HANDLE))
  {
    retval = NI_RETCODE_ERROR_INVALID_HANDLE;
    LRETURN;
  }
  // User passed in both handles, so we do not need to allocate for it
  else
  {
    user_handles = true;
  }

  ni_log2(p_ctx, NI_LOG_DEBUG, 
         "Finish open the session dev:%s guid:%d handle:%p handle1:%p\n",
         p_ctx->dev_xcoder_name, p_ctx->hw_id,
         p_ctx->device_handle, p_ctx->blk_io_handle);

  // get FW API version
  p_device_context = ni_rsrc_get_device_context(device_type, p_ctx->hw_id);
  if (p_device_context == NULL)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
             "ERROR: %s() ni_rsrc_get_device_context() failed\n",
             __func__);
      retval = NI_RETCODE_ERROR_OPEN_DEVICE;
      LRETURN;
  }

  if (!(strcmp(p_ctx->dev_xcoder_name, "")) || !(strcmp(p_ctx->dev_xcoder_name, NI_BEST_MODEL_LOAD_STR)) ||
      !(strcmp(p_ctx->dev_xcoder_name, NI_BEST_REAL_LOAD_STR)))
  {
      strcpy(p_ctx->dev_xcoder_name, p_device_context->p_device_info->dev_name);
  }

  memcpy(p_ctx->fw_rev , p_device_context->p_device_info->fw_rev, 8);

  ni_rsrc_free_device_context(p_device_context);

  retval = ni_device_get_ddr_configuration(p_ctx);
  if (retval != NI_RETCODE_SUCCESS)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
             "ERROR: %s() cannot retrieve DDR configuration\n",
             __func__);
      LRETURN;
  }

  if (p_ctx->keep_alive_timeout == 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
             "ERROR: %s() keep_alive_timeout was 0, should be between 1-100. "
             "Setting to default of %u\n",
             __func__, NI_DEFAULT_KEEP_ALIVE_TIMEOUT);
      p_ctx->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;
  }
  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_open(p_ctx);
      if (user_handles != true)
      {
        if( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
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
#ifndef _WIN32
        // p2p is not supported on Windows, so skip following assignments.
        ni_xcoder_params_t *p_enc_params;

        p_enc_params = p_ctx->p_session_config;

        if (p_enc_params && p_enc_params->hwframes &&
            p_enc_params->p_first_frame)
        {
            niFrameSurface1_t *pSurface;
            pSurface =
                (niFrameSurface1_t *)p_enc_params->p_first_frame->p_data[3];
            p_ctx->sender_handle =
                (ni_device_handle_t)(int64_t)pSurface->device_handle;
            p_enc_params->rootBufId = pSurface->ui16FrameIdx;

            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: sender_handle and rootBufId %d set\n",
                   __func__, p_enc_params->rootBufId);
        }
#endif

        retval = ni_encoder_session_open(p_ctx);
        if (user_handles != true)
        {
            if (ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
            {
                retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
                LRETURN;
            }
        }
        // send keep alive signal thread
        if (NI_RETCODE_SUCCESS != retval)
        {
            LRETURN;
        }
        break;
    }
    case NI_DEVICE_TYPE_UPLOAD:
    {
      retval = ni_uploader_session_open(p_ctx);
      if (user_handles != true)
      {
          if (ni_rsrc_unlock(query_type, lock) != NI_RETCODE_SUCCESS)
          {
              retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
              LRETURN;
          }
      }
      // send keep alive signal thread
      if (NI_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    case NI_DEVICE_TYPE_SCALER:
    {
      retval = ni_scaler_session_open(p_ctx);
      if (user_handles != true)
      {
        if( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      // send keep alive signal thread
      if (NI_RETCODE_SUCCESS != retval)
      {
        LRETURN;
      }
      break;
    }
    case NI_DEVICE_TYPE_AI:
    {
        retval = ni_ai_session_open(p_ctx);
        if (user_handles != true)
        {
            if (ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
            {
                retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
                LRETURN;
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
        if( ni_rsrc_unlock(device_type, lock) != NI_RETCODE_SUCCESS)
        {
          retval = NI_RETCODE_ERROR_UNLOCK_DEVICE;
          LRETURN;
        }
      }
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
      LRETURN;
    }
  }

  p_ctx->keep_alive_thread_args = (ni_thread_arg_struct_t *) malloc(sizeof(ni_thread_arg_struct_t));
  if (!p_ctx->keep_alive_thread_args)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: thread_args allocation failed!\n");
    ni_device_session_close(p_ctx, 0, device_type);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  p_ctx->keep_alive_thread_args->session_id = p_ctx->session_id;
  p_ctx->keep_alive_thread_args->session_timestamp = p_ctx->session_timestamp;
  p_ctx->keep_alive_thread_args->device_type = device_type;
  p_ctx->keep_alive_thread_args->device_handle = p_ctx->blk_io_handle;
  p_ctx->keep_alive_thread_args->thread_event_handle = p_ctx->event_handle;
  p_ctx->keep_alive_thread_args->close_thread = false;
  p_ctx->keep_alive_thread_args->keep_alive_timeout = p_ctx->keep_alive_timeout;
  p_ctx->keep_alive_thread_args->plast_access_time = &p_ctx->last_access_time;
  p_ctx->keep_alive_thread_args->p_mutex = &p_ctx->mutex;
  p_ctx->keep_alive_thread_args->hw_id = p_ctx->hw_id;
  p_ctx->last_access_time = ni_gettime_ns();
  
  if (ni_posix_memalign(&p_ctx->keep_alive_thread_args->p_buffer,
                        sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: keep alive p_buffer allocation failed!\n");
      ni_device_session_close(p_ctx, 0, device_type);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_ctx->keep_alive_thread_args->p_buffer, 0, NI_DATA_BUFFER_LEN);

  if (0 !=
      ni_pthread_create(&p_ctx->keep_alive_thread, NULL,
                        ni_session_keep_alive_thread,
                        (void *)p_ctx->keep_alive_thread_args))
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: failed to create keep alive thread\n");
    p_ctx->keep_alive_thread = (ni_pthread_t){0};
    ni_aligned_free(p_ctx->keep_alive_thread_args->p_buffer);
    ni_memfree(p_ctx->keep_alive_thread_args);
    ni_device_session_close(p_ctx, 0, device_type);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Enabled keep alive thread\n");

  // allocate memory for encoder change data to be reused
  p_ctx->enc_change_params = calloc(1, sizeof(ni_encoder_change_params_t));
  if (!p_ctx->enc_change_params)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: enc_change_params allocation failed!\n");
      ni_device_session_close(p_ctx, 0, device_type);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
  }

END:
    ni_device_session_context_clear(&p_session_context);

    if (p_device_pool)
    {
        ni_rsrc_free_device_pool(p_device_pool);
        p_device_pool = NULL;
    }

    p_ctx->xcoder_state &= ~NI_XCODER_OPEN_STATE;

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Close device session that was previously opened by calling
 *          ni_device_session_open()
 *          If device_type is NI_DEVICE_TYPE_DECODER closes decoding session
 *          If device_type is NI_DEVICE_TYPE_ENCODER closes encoding session
 *          If device_type is NI_DEVICE_TYPE_SCALER closes scaling session
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] eos_received Flag indicating if End Of Stream indicator was
 *                          received
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER,
 *                          or NI_DEVICE_TYPE_SCALER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_close(ni_session_context_t *p_ctx,
                                     int eos_recieved,
                                     ni_device_type_t device_type)
{
    int ret;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_CLOSE_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

#ifdef _WIN32
    if (p_ctx->keep_alive_thread.handle && p_ctx->keep_alive_thread_args)
#else
    if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args)
#endif
    {
        p_ctx->keep_alive_thread_args->close_thread = true;
        ret = ni_pthread_join(p_ctx->keep_alive_thread, NULL);
        if (ret)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, 
                   "join keep alive thread fail! : sid %u ret %d\n",
                   p_ctx->session_id, ret);
        }
        ni_aligned_free(p_ctx->keep_alive_thread_args->p_buffer);
        ni_memfree(p_ctx->keep_alive_thread_args);
    } else
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "invalid keep alive thread: %u\n",
               p_ctx->session_id);
    }

    switch (device_type)
    {
        case NI_DEVICE_TYPE_DECODER:
        {
            retval = ni_decoder_session_close(p_ctx, eos_recieved);
            break;
        }
        case NI_DEVICE_TYPE_UPLOAD:
        {
            ni_uploader_session_close(p_ctx);
            // fall through
        }
        case NI_DEVICE_TYPE_ENCODER:
        {
            retval = ni_encoder_session_close(p_ctx, eos_recieved);
            break;
        }
        case NI_DEVICE_TYPE_SCALER:
        {
            retval = ni_scaler_session_close(p_ctx, eos_recieved);
            break;
        }
        case NI_DEVICE_TYPE_AI:
        {
            retval = ni_ai_session_close(p_ctx, eos_recieved);
            break;
        }
        default:
        {
            retval = NI_RETCODE_INVALID_PARAM;
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
                   __func__, device_type);
            break;
        }
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    // need set invalid after closed. May cause open invalid parameters.
    p_ctx->session_id = NI_INVALID_SESSION_ID;

    ni_memfree(p_ctx->p_hdr_buf);
    ni_memfree(p_ctx->av_rois);
    ni_memfree(p_ctx->roi_map);
    ni_memfree(p_ctx->avc_roi_map);
    ni_memfree(p_ctx->hevc_roi_map);
    ni_memfree(p_ctx->hevc_sub_ctu_roi_buf);
    ni_memfree(p_ctx->p_master_display_meta_data);
    ni_memfree(p_ctx->enc_change_params);
    p_ctx->hdr_buf_size = 0;
    p_ctx->roi_side_data_size = 0;
    p_ctx->nb_rois = 0;
    p_ctx->target_bitrate = -1;
    p_ctx->force_idr_frame = 0;
    p_ctx->ltr_to_set.use_cur_src_as_long_term_pic = 0;
    p_ctx->ltr_to_set.use_long_term_ref = 0;
    p_ctx->ltr_interval = -1;
    p_ctx->ltr_frame_ref_invalid = -1;
    p_ctx->max_frame_size = 0;
    p_ctx->reconfig_crf = -1;
    p_ctx->reconfig_crf_decimal = 0;
    p_ctx->reconfig_vbv_buffer_size = 0;
    p_ctx->reconfig_vbv_max_rate = 0;
    p_ctx->last_gop_size = 0;
    p_ctx->initial_frame_delay = 0;
    p_ctx->current_frame_delay = 0;
    p_ctx->max_frame_delay = 0;
    p_ctx->av1_pkt_num = 0;
    p_ctx->reconfig_intra_period = -1;
    p_ctx->reconfig_slice_arg = 0;

    p_ctx->xcoder_state &= ~NI_XCODER_CLOSE_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Send a flush command to the device
 *          If device_type is NI_DEVICE_TYPE_DECODER sends EOS command to
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER sends EOS command to
 *          encoder
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_FLUSH_STATE;

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_send_eos(p_ctx);
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_send_eos(p_ctx);
      break;
    }
    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
      break;
    }
  }
  p_ctx->ready_to_close = (NI_RETCODE_SUCCESS == retval);
  p_ctx->xcoder_state &= ~NI_XCODER_FLUSH_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);
  return retval;
}

/*!*****************************************************************************
 *  \brief  Save a stream's headers in a decoder session that can be used later
 *          for continuous decoding from the same source.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] hdr_data     Pointer to header data
 *  \param[in] hdr_size     Size of header data in bytes
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_dec_session_save_hdrs(ni_session_context_t *p_ctx,
                                             uint8_t *hdr_data,
                                             uint8_t hdr_size)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s p_ctx null, return\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (!hdr_data)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s hdr_data null, return\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    } else if (p_ctx->p_hdr_buf && p_ctx->hdr_buf_size == hdr_size &&
               0 == memcmp(p_ctx->p_hdr_buf, hdr_data, hdr_size))
    {
        // no change from the saved headers, success !
        return retval;
    }

    // update the saved header data
    free(p_ctx->p_hdr_buf);
    p_ctx->hdr_buf_size = 0;
    p_ctx->p_hdr_buf = malloc(hdr_size);
    if (p_ctx->p_hdr_buf)
    {
        memcpy(p_ctx->p_hdr_buf, hdr_data, hdr_size);
        p_ctx->hdr_buf_size = hdr_size;
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s saved hdr size %u\n", __func__,
               p_ctx->hdr_buf_size);
    } else
    {
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s no memory.\n", __func__);
    }
    return retval;
}

/*!*****************************************************************************
 *  \brief  Flush a decoder session to get ready to continue decoding.
 *  Note: this is different from ni_device_session_flush in that it closes the
 *        current decode session and opens a new one for continuous decoding.
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_dec_session_flush(ni_session_context_t *p_ctx)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s ctx null, return\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    retval = ni_decoder_session_flush(p_ctx);
    if (NI_RETCODE_SUCCESS == retval) {
        p_ctx->ready_to_close = 0;
    }
    ni_pthread_mutex_unlock(&p_ctx->mutex);
    return retval;
}

/*!*****************************************************************************
 *  \brief  Sends data to the device
 *          If device_type is NI_DEVICE_TYPE_DECODER sends data packet to
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER sends data frame to encoder
 *          If device_type is NI_DEVICE_TYPE_AI sends data frame to ai engine
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated
 *                          ni_session_data_io_t struct which contains either a
 *                          ni_frame_t data frame or ni_packet_t data packet to
 *                          send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER or
 *                          NI_DEVICE_TYPE_AI
 *                          If NI_DEVICE_TYPE_DECODER is specified, it is
 *                          expected that the ni_packet_t struct inside the
 *                          p_data pointer contains data to send.
 *                          If NI_DEVICE_TYPE_ENCODER or NI_DEVICE_TYPE_AI is
 *                          specified, it is expected that the ni_frame_t
 *                          struct inside the p_data pointer contains data to
 *                          send.
 *  \return On success
 *                          Total number of bytes written
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
int ni_device_session_write(ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (!p_ctx || !p_data)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }
  // Here check if keep alive thread is closed.
#ifdef _WIN32
  if (p_ctx->keep_alive_thread.handle &&
      p_ctx->keep_alive_thread_args->close_thread)
#else
  if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args->close_thread)
#endif
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
             "ERROR: %s() keep alive thread has been closed, "
             "hw:%d, session:%d\n",
             __func__, p_ctx->hw_id, p_ctx->session_id);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  // In close state, let the close process execute first.
  if (p_ctx->xcoder_state & NI_XCODER_CLOSE_STATE)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s close state, return\n", __func__);
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(100);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }
  p_ctx->xcoder_state |= NI_XCODER_WRITE_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      retval = ni_decoder_session_write(p_ctx, &(p_data->data.packet));
      break;
    }
    case NI_DEVICE_TYPE_ENCODER:
    {
      retval = ni_encoder_session_write(p_ctx, &(p_data->data.frame));
      break;
    }
    case NI_DEVICE_TYPE_AI:
    {
        retval = ni_ai_session_write(p_ctx, &(p_data->data.frame));
        break;
    }
    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
      break;
    }
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state &= ~NI_XCODER_WRITE_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);
  return retval;
}

/*!*****************************************************************************
 *  \brief  Read data from the device
 *          If device_type is NI_DEVICE_TYPE_DECODER reads data packet from
 *          decoder
 *          If device_type is NI_DEVICE_TYPE_ENCODER reads data frame from
 *          encoder
 *          If device_type is NI_DEVICE_TYPE_AI reads data frame from AI engine
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] p_data       Pointer to a caller allocated ni_session_data_io_t
 *                          struct which contains either a ni_frame_t data frame
 *                          or ni_packet_t data packet to send
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER, NI_DEVICE_TYPE_ENCODER, or
 *                          NI_DEVICE_TYPE_SCALER
 *                          If NI_DEVICE_TYPE_DECODER is specified, data that
 *                          was read will be placed into ni_frame_t struct
 *                          inside the p_data pointer
 *                          If NI_DEVICE_TYPE_ENCODER is specified, data that
 *                          was read will be placed into ni_packet_t struct
 *                          inside the p_data pointer
 *                          If NI_DEVICE_TYPE_AI is specified, data that was
 *                          read will be placed into ni_frame_t struct inside
 *                          the p_data pointer
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
int ni_device_session_read(ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if ((!p_ctx) || (!p_data))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  // Here check if keep alive thread is closed.
#ifdef _WIN32
  if (p_ctx->keep_alive_thread.handle &&
      p_ctx->keep_alive_thread_args->close_thread)
#else
  if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args->close_thread)
#endif
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
             "ERROR: %s() keep alive thread has been closed, "
             "hw:%d, session:%d\n",
             __func__, p_ctx->hw_id, p_ctx->session_id);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  // In close state, let the close process execute first.
  if (p_ctx->xcoder_state & NI_XCODER_CLOSE_STATE)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s close state, return\n", __func__);
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(100);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }
  p_ctx->xcoder_state |= NI_XCODER_READ_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  switch (device_type)
  {
    case NI_DEVICE_TYPE_DECODER:
    {
      int seq_change_read_count = 0;
      p_data->data.frame.src_codec = p_ctx->codec_format;
      for (;;)
      {
        retval = ni_decoder_session_read(p_ctx, &(p_data->data.frame));
        // check resolution change only after initial setting obtained
        // p_data->data.frame.video_width is picture width and will be 32-align
        // adjusted to frame size; p_data->data.frame.video_height is the same as
        // frame size, then compare them to saved one for resolution checking
        //
        uint32_t aligned_width;
        if(QUADRA)
        {
            aligned_width = ((((p_data->data.frame.video_width * p_ctx->bit_depth_factor) + 127) / 128) * 128);
        }
        else
        {
            aligned_width = ((p_data->data.frame.video_width + 31) / 32) * 32;
        }

        if (0 == retval && seq_change_read_count)
        {
            ni_log2(p_ctx, NI_LOG_DEBUG, 
                   "%s (decoder): seq change NO data, next time.\n", __func__);
            p_ctx->active_video_width = 0;
            p_ctx->active_video_height = 0;
            p_ctx->actual_video_width = 0;
            break;
        }
        else if (retval < 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "%s (decoder): failure ret %d, return ..\n",
                   __func__, retval);
            break;
        }
        // aligned_width may equal to active_video_width if bit depth and width
        // are changed at the same time. So, check video_width != actual_video_width.
        else if (p_ctx->frame_num && (p_ctx->pixel_format_changed ||
                 (p_data->data.frame.video_width &&
                  p_data->data.frame.video_height &&
                  (aligned_width != p_ctx->active_video_width ||
                   p_data->data.frame.video_height != p_ctx->active_video_height))))
        {
            ni_log2(
                p_ctx, NI_LOG_DEBUG,
                "%s (decoder): resolution change, frame size %ux%u -> %ux%u, "
                "width %u bit %d, pix_fromat_changed %d, actual_video_width %d, continue read ...\n",
                __func__, p_ctx->active_video_width, p_ctx->active_video_height,
                aligned_width, p_data->data.frame.video_height,
                p_data->data.frame.video_width, p_ctx->bit_depth_factor,
                p_ctx->pixel_format_changed, p_ctx->actual_video_width);
            // reset active video resolution to 0 so it can be queried in the re-read
            p_ctx->active_video_width = 0;
            p_ctx->active_video_height = 0;
            p_ctx->actual_video_width = 0;
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
    case NI_DEVICE_TYPE_AI:
    {
        retval = ni_ai_session_read(p_ctx, &(p_data->data.packet));
        break;
    }
    default:
    {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
      break;
    }
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state &= ~NI_XCODER_READ_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);
  return retval;
}

/*!*****************************************************************************
 *  \brief  Query session data from the device -
 *          If device_type is valid, will query session data
 *          from specified device type
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or
 *                          NI_DEVICE_TYPE_ENCODER or
 *                          NI_DEVICE_TYPE_SCALER or
 *                          NI_DEVICE_TYPE_AI or
 *                          NI_DEVICE_TYPE_UPLOADER
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_query(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (IS_XCODER_DEVICE_TYPE(device_type))
  {
      retval = ni_xcoder_session_query(p_ctx, device_type);
  } else
  {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
  }

  return retval;
}

/*!*****************************************************************************
 *  \brief  Query detail session data from the device -
 *          If device_type is valid, will query session data
 *          from specified device type
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or
 *                          NI_DEVICE_TYPE_ENCODER or
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_query_detail(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_detail_status_t *detail_data)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (IS_XCODER_DEVICE_TYPE(device_type))
  {
      retval = ni_xcoder_session_query_detail(p_ctx, device_type, detail_data, 0);
  } else
  {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
  }

  return retval;
}

/*!*****************************************************************************
 *  \brief  Query detail session data from the device -
 *          If device_type is valid, will query session data
 *          from specified device type
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] device_type  NI_DEVICE_TYPE_DECODER or
 *                          NI_DEVICE_TYPE_ENCODER or
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 ******************************************************************************/
ni_retcode_t ni_device_session_query_detail_v1(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_detail_status_v1_t *detail_data)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (IS_XCODER_DEVICE_TYPE(device_type))
  {
      retval = ni_xcoder_session_query_detail(p_ctx, device_type, detail_data, 1);
  } else
  {
      retval = NI_RETCODE_INVALID_PARAM;
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
             __func__, device_type);
  }

  return retval;
}

/*!*****************************************************************************
 *  \brief  Send namespace num and SRIOv index to the device with specified logic block
 *          address.
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] namespace_num  Set the namespace number with designated sriov
 *  \param[in] sriov_index    Identify which sriov need to be set
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_config_namespace_num(ni_device_handle_t device_handle,
                                       uint32_t namespace_num, uint32_t sriov_index)
{
    ni_log(NI_LOG_DEBUG, "%s namespace_num %u sriov_index %u\n",
           __func__, namespace_num, sriov_index);
  return ni_device_config_ns_qos(device_handle, namespace_num, sriov_index);
}

/*!*****************************************************************************
 *  \brief  Send qos mode to the device with specified logic block
 *          address.
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] mode           The requested qos mode
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_config_qos(ni_device_handle_t device_handle,
                                            uint32_t mode)
{
  ni_log(NI_LOG_DEBUG, "%s device_handle %p mode %u\n",
         __func__, device_handle, mode);
  return ni_device_config_ns_qos(device_handle, QOS_NAMESPACE_CODE, mode);
}

/*!*****************************************************************************
 *  \brief  Send qos over provisioning mode to target namespace with specified logic 
 *          block address.
 *
 *  \param[in] device_handle   Device handle obtained by calling ni_device_open
 *  \param[in] device_handle_t Target device handle of namespace required for OP
 *  \param[in] over_provision  The request overprovision percent
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_config_qos_op(ni_device_handle_t device_handle,
                                     ni_device_handle_t device_handle_t,
                                     uint32_t over_provision)
{
  ni_retcode_t retval;
  ni_log(NI_LOG_DEBUG, "%s device_handle %p target %p over_provision %u\n",
         __func__, device_handle, device_handle_t, over_provision);
  retval = ni_device_config_ns_qos(device_handle, QOS_OP_CONFIG_REC_OP_CODE,
                                   over_provision);
  if (NI_RETCODE_SUCCESS != retval)
  {
      return retval;
  }
  retval = ni_device_config_ns_qos(device_handle_t, QOS_OP_CONFIG_CODE,
                                   0);
  return retval;
}

/*!*****************************************************************************
 *  \brief  Allocate preliminary memory for the frame buffer based on provided
 *          parameters. Applicable to YUV420 Planar pixel (8 or 10 bit/pixel)
 *          format or 32-bit RGBA.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                           ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel,
 *                           4 for 32 bits/pixel (RGBA)
 *  \param[in] hw_frame_count Number of hw descriptors stored
 *  \param[in] is_planar     0 if semiplanar else planar
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_frame_buffer_alloc(ni_frame_t *p_frame, int video_width,
                                   int video_height, int alignment,
                                   int metadata_flag, int factor,
                                   int hw_frame_count, int is_planar)
{
  void* p_buffer = NULL;
  int metadata_size = 0;
  int retval = NI_RETCODE_SUCCESS;
  int width_aligned = video_width;
  int height_aligned = video_height;

  if ((!p_frame) || ((factor!=1) && (factor!=2) && (factor !=4))
     || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width<=0)
     || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
             "factor %d, video_width %d, video_height %d\n",
             __func__, factor, video_width, video_height);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (metadata_flag)
  {
    metadata_size = NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;
  }

  if (QUADRA)
  {
    switch (factor)
    {
      case 1: /* 8-bit YUV420 */
      case 2: /* 10-bit YUV420 */
        width_aligned = ((((video_width * factor) + 127) / 128) * 128) / factor;
        height_aligned = ((video_height + 1) / 2) * 2;
        break;
      case 4: /* 32-bit RGBA */
        //64byte aligned => 16 rgba pixels
        width_aligned = NI_VPU_ALIGN16(video_width);
        height_aligned = ((video_height + 1) / 2) * 2;
        break;
      default:
        return NI_RETCODE_INVALID_PARAM;
    }
  }
  else
  {
    width_aligned = ((video_width + 31) / 32) * 32;
    height_aligned = ((video_height + 7) / 8) * 8;
    if (alignment)
    {
      height_aligned = ((video_height + 15) / 16) * 16;
    }
  }

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size;
  int chroma_r_size;
  if (QUADRA)
  {
    int chroma_width_aligned = ((((video_width / 2 * factor) + 127) / 128) * 128) / factor;
    if (is_planar == NI_PIXEL_PLANAR_FORMAT_TILED4X4 ||
          is_planar == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR)
    {
        chroma_width_aligned =
            ((((video_width * factor) + 127) / 128) * 128) / factor;
    }
    int chroma_height_aligned = height_aligned / 2;
    chroma_b_size = chroma_r_size = chroma_width_aligned * chroma_height_aligned * factor;
    if (is_planar == NI_PIXEL_PLANAR_FORMAT_TILED4X4 ||
        is_planar == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR)
    {
        chroma_r_size = 0;
    }
    if (4 == factor)
    {
      chroma_b_size = chroma_r_size = 0;
    }
    //ni_log(NI_LOG_DEBUG, "%s: factor %d chroma_aligned=%dx%d org=%dx%d\n", __func__, factor, chroma_width_aligned, chroma_height_aligned, video_width, video_height);
  }
  else
  {
    chroma_b_size = luma_size / 4;
    chroma_r_size = chroma_b_size;
  }
  int buffer_size;

  /* if hw_frame_count is zero, this is a software frame */
  if (hw_frame_count == 0)
    buffer_size = luma_size + chroma_b_size + chroma_r_size + metadata_size;
  else
      buffer_size =
          (int)sizeof(niFrameSurface1_t) * hw_frame_count + metadata_size;

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT * 3;
  //ni_log(NI_LOG_DEBUG, "%s: luma_size %d chroma_b_size %d chroma_r_size %d metadata_size %d buffer_size %d\n", __func__, luma_size, chroma_b_size, chroma_r_size, metadata_size, buffer_size);

  //Check if need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
      ni_log(NI_LOG_DEBUG,
             "%s: free current p_frame, p_frame->buffer_size=%u\n", __func__,
             p_frame->buffer_size);
      ni_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
      if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
      {
          ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_frame buffer.\n",
                 NI_ERRNO, __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }

    // init once after allocation
    //memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    ni_log(NI_LOG_DEBUG, "%s: Allocate new p_frame buffer\n", __func__);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
  }

  if (hw_frame_count)
  {
    p_frame->data_len[0] = 0;
    p_frame->data_len[1] = 0;
    p_frame->data_len[2] = 0;
    p_frame->data_len[3] = sizeof(niFrameSurface1_t)*hw_frame_count;
  }
  else
  {
    p_frame->data_len[0] = luma_size;
    p_frame->data_len[1] = chroma_b_size;
    p_frame->data_len[2] = chroma_r_size;
    p_frame->data_len[3] = 0;//unused by hwdesc
  }

  p_frame->p_data[0] = p_frame->p_buffer;
  p_frame->p_data[1] = p_frame->p_data[0] + p_frame->data_len[0];
  p_frame->p_data[2] = p_frame->p_data[1] + p_frame->data_len[1];
  p_frame->p_data[3] = p_frame->p_data[2] + p_frame->data_len[2]; //hwdescriptor

  // init p_data[3] to 0 so that ni_frame_buffer_free frees only valid DMA buf
  // fd in hw frame read from fw
  if (hw_frame_count)
  {
      memset(p_frame->p_data[3], 0, sizeof(niFrameSurface1_t) * hw_frame_count);
  }

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_DEBUG, "ni_frame_buffer_alloc: p_buffer %p p_data [%p %p %p %p] data_len [%d %d %d %d] video_width %d video_height %d\n", p_frame->p_buffer, p_frame->p_data[0], p_frame->p_data[1], p_frame->p_data[2], p_frame->p_data[3], p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2], p_frame->data_len[3], p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_DEBUG, "%s: success: p_frame->buffer_size=%u\n", __func__,
         p_frame->buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

/*!*****************************************************************************
 *  \brief  Wrapper function for ni_frame_buffer_alloc. Meant to handle RGBA min.
 * resoulution considerations for encoder.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                           ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] metadata_flag Flag indicating if space for additional metadata
 *                                               should be allocated
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel,
 *                           4 for 32 bits/pixel (RGBA)
 *  \param[in] hw_frame_count Number of hw descriptors stored
 *  \param[in] is_planar     0 if semiplanar else planar
 *  \param[in] pix_fmt       pixel format to distinguish between planar types 
 *                            and/or components
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_enc_frame_buffer_alloc(ni_frame_t *p_frame, int video_width,
                                       int video_height, int alignment,
                                       int metadata_flag, int factor,
                                       int hw_frame_count, int is_planar, 
                                       ni_pix_fmt_t pix_fmt)
{
    int extra_len = 0;
    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};

    if (((factor!=1) && (factor!=2) && (factor !=4))
      || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width<=0)
      || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
              "factor %d, video_width %d, video_height %d\n",
              __func__, factor, video_width, video_height);
        return NI_RETCODE_INVALID_PARAM;
    }

    switch (pix_fmt)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
        case NI_PIX_FMT_NV16:
        case NI_PIX_FMT_YUYV422:
        case NI_PIX_FMT_UYVY422:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_BGR0:
            break;
        default:
            ni_log(NI_LOG_ERROR, "ERROR: %s pix_fmt %d not supported \n",
                  __func__, pix_fmt);
            return NI_RETCODE_INVALID_PARAM;
    }
    //Get stride info for original resolution
    ni_get_frame_dim(video_width, video_height,
                     pix_fmt,
                     dst_stride, height_aligned);
    
    if (metadata_flag)
    {
        extra_len = NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;
    }

    return ni_frame_buffer_alloc_pixfmt(p_frame, pix_fmt, video_width, 
                                        video_height, dst_stride, alignment,
                                        extra_len);
}

/*!*****************************************************************************
 *  \brief  Allocate preliminary memory for the frame buffer based on provided
 *          parameters.
 *
 *  \param[in] p_frame       Pointer to a caller allocated
 *                           ni_frame_t struct
 *  \param[in] video_width   Width of the video frame
 *  \param[in] video_height  Height of the video frame
 *  \param[in] alignment     Allignment requirement
 *  \param[in] pixel_format  Format for input
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_frame_buffer_alloc_dl(ni_frame_t *p_frame, int video_width,
                                          int video_height, int pixel_format)
{
    void *p_buffer = NULL;
    int retval = NI_RETCODE_SUCCESS;
    int width_aligned = video_width;
    int height_aligned = video_height;
    int buffer_size;
    int luma_size;
    int chroma_b_size;
    int chroma_r_size;

    if ((!p_frame) || (video_width > NI_MAX_RESOLUTION_WIDTH) ||
        (video_width <= 0) || (video_height > NI_MAX_RESOLUTION_HEIGHT) ||
        (video_height <= 0))
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s passed parameters are null or not supported, "
               "video_width %d, video_height %d\n",
               __func__, video_width, video_height);
        return NI_RETCODE_INVALID_PARAM;
    }

    switch (pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
            width_aligned = NI_VPU_ALIGN128(video_width);
            height_aligned = NI_VPU_CEIL(video_height, 2);

            luma_size = width_aligned * height_aligned;
            chroma_b_size =
                NI_VPU_ALIGN128(video_width / 2) * height_aligned / 2;
            chroma_r_size = chroma_b_size;
            break;
        case NI_PIX_FMT_YUV420P10LE:
            width_aligned = NI_VPU_ALIGN128(video_width * 2) / 2;
            height_aligned = NI_VPU_CEIL(video_height, 2);

            luma_size = width_aligned * height_aligned * 2;
            chroma_b_size = NI_VPU_ALIGN128(video_width) * height_aligned / 2;
            chroma_r_size = chroma_b_size;
            break;
        case NI_PIX_FMT_NV12:
            width_aligned = NI_VPU_ALIGN128(video_width);
            height_aligned = NI_VPU_CEIL(video_height, 2);

            luma_size = width_aligned * height_aligned;
            chroma_b_size = width_aligned * height_aligned / 2;
            chroma_r_size = 0;
            break;
        case NI_PIX_FMT_P010LE:
            width_aligned = NI_VPU_ALIGN128(video_width * 2) / 2;
            height_aligned = NI_VPU_CEIL(video_height, 2);

            luma_size = width_aligned * height_aligned * 2;
            chroma_b_size = NI_VPU_ALIGN128(video_width) * height_aligned;
            chroma_r_size = 0;
            break;
        case NI_PIX_FMT_NV16:
            width_aligned = NI_VPU_ALIGN64(video_width);
            height_aligned = video_height;

            luma_size = width_aligned * height_aligned;
            chroma_b_size = luma_size;
            chroma_r_size = 0;
            break;
        case NI_PIX_FMT_YUYV422:
        case NI_PIX_FMT_UYVY422:
            width_aligned = NI_VPU_ALIGN16(video_width);
            height_aligned = video_height;

            luma_size = width_aligned * height_aligned * 2;
            chroma_b_size = 0;
            chroma_r_size = 0;
            break;
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_BGR0:
            width_aligned = NI_VPU_ALIGN16(video_width);
            height_aligned = video_height;

            luma_size = width_aligned * height_aligned * 4;
            chroma_b_size = 0;
            chroma_r_size = 0;
            break;
        case NI_PIX_FMT_BGRP:
            width_aligned = NI_VPU_ALIGN32(video_width);
            height_aligned = video_height;

            luma_size = width_aligned * height_aligned;
            chroma_b_size = luma_size;
            chroma_r_size = luma_size;
            break;
        default:
            ni_log(NI_LOG_ERROR, "Unknown pixel format %d\n", pixel_format);
            return NI_RETCODE_INVALID_PARAM;
    }

    /* Allocate local memory to hold a software ni_frame */
    buffer_size = luma_size + chroma_b_size + chroma_r_size;

    /* Round up to nearest 4K block */
    buffer_size = NI_VPU_ALIGN4096(buffer_size);

    ni_log(NI_LOG_DEBUG, "%s: Rlen %d Glen %d Blen %d buffer_size %d\n",
           __func__, luma_size, chroma_b_size, chroma_r_size, buffer_size);

    /* If the frame has changed, reallocate it */
    if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
    {
        ni_log(NI_LOG_DEBUG,
               "%s: free current p_frame, p_frame->buffer_size=%u\n", __func__,
               p_frame->buffer_size);
        ni_frame_buffer_free(p_frame);
    }

    /* Check if need to reallocate */
    if (p_frame->buffer_size != buffer_size)
    {
        ni_log(NI_LOG_DEBUG, "%s: Allocate new p_frame buffer\n", __func__);
        if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %d: %s() Cannot allocate p_frame buffer.\n", NI_ERRNO,
                   __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
    } else
    {
        ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
        p_buffer = p_frame->p_buffer;
    }

    // init once after allocation
    // memset(p_buffer, 0, buffer_size);

    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    p_frame->data_len[0] = luma_size;
    p_frame->data_len[1] = chroma_b_size;
    p_frame->data_len[2] = chroma_r_size;
    p_frame->data_len[3] = 0;

    p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
    p_frame->p_data[1] = (uint8_t *)p_frame->p_data[0] + p_frame->data_len[0];
    p_frame->p_data[2] = (uint8_t *)p_frame->p_data[1] + p_frame->data_len[1];
    p_frame->p_data[3] = (uint8_t *)p_frame->p_data[2] + p_frame->data_len[2];

    // init p_data[3] to 0 so that ni_frame_buffer_free frees only valid DMA buf
    // fd in hw frame read from fw
    p_frame->video_width = width_aligned;
    p_frame->video_height = height_aligned;

    // ni_log(NI_LOG_DEBUG, "ni_frame_buffer_alloc: p_buffer %p p_data [%p %p %p %p] data_len [%d %d %d %d] video_width %d video_height %d\n", p_frame->p_buffer, p_frame->p_data[0], p_frame->p_data[1], p_frame->p_data[2], p_frame->p_data[3], p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2], p_frame->data_len[3], p_frame->video_width, p_frame->video_height);
    ni_log(NI_LOG_DEBUG, "%s: success: p_frame->buffer_size=%u\n", __func__,
           p_frame->buffer_size);
END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
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
 *  \param[in] alignment     Alignment requirement
 *  \param[in] factor        1 for 8 bits/pixel format, 2 for 10 bits/pixel
 *  \param[in] is_planar     0 if semiplanar else planar
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_decoder_frame_buffer_alloc(ni_buf_pool_t *p_pool,
                                           ni_frame_t *p_frame, int alloc_mem,
                                           int video_width, int video_height,
                                           int alignment, int factor,
                                           int is_planar)
{
  int retval = NI_RETCODE_SUCCESS;

  int width_aligned;
  int height_aligned;

  if ((!p_frame) || ((factor!=1) && (factor!=2))
     || (video_width > NI_MAX_RESOLUTION_WIDTH) || (video_width <= 0)
      || (video_height > NI_MAX_RESOLUTION_HEIGHT) || (video_height <= 0))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
             "factor %d, video_width %d, video_height %d\n",
             __func__, factor, video_width, video_height);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (QUADRA)
  {
    width_aligned = ((((video_width * factor) + 127) / 128) * 128) / factor;
    height_aligned = video_height;
  }
  else
  {
    width_aligned = ((video_width + 31) / 32) * 32;
    height_aligned = ((video_height + 7) / 8) * 8;
    if (alignment)
    {
      height_aligned = ((video_height + 15) / 16) * 16;
    }
  }

  ni_log(NI_LOG_DEBUG, "%s: aligned=%dx%d orig=%dx%d\n", __func__,
         width_aligned, height_aligned, video_width, video_height);

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size;
  int chroma_r_size;
  if (QUADRA)
  {
    int chroma_width_aligned = ((((video_width / 2 * factor) + 127) / 128) * 128) / factor;
    if (!is_planar)
    {
        chroma_width_aligned =
            ((((video_width * factor) + 127) / 128) * 128) / factor;
    }
    int chroma_height_aligned = height_aligned / 2;
    chroma_b_size = chroma_r_size = chroma_width_aligned * chroma_height_aligned * factor;
    if (!is_planar)
    {
        chroma_r_size = 0;
    }
  }
  else
  {
    chroma_b_size = luma_size / 4;
    chroma_r_size = chroma_b_size;
  }
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
        ni_log(NI_LOG_ERROR, "ERROR %s: invalid pool!\n", __func__);
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

    ni_log(NI_LOG_DEBUG, "%s: got new frame ptr %p buffer %p\n", __func__,
           p_frame->p_buffer, p_frame->dec_buf);
  }
  else
  {
    p_frame->dec_buf = NULL;
    p_frame->p_buffer = NULL;
    ni_log(NI_LOG_DEBUG, "%s: NOT alloc mem buffer\n", __func__);
  }

  if (p_frame->p_buffer)
  {
    p_frame->p_data[0] = p_frame->p_buffer;
    p_frame->p_data[1] = p_frame->p_data[0] + luma_size;
    p_frame->p_data[2] = p_frame->p_data[1] + chroma_b_size;
    p_frame->p_data[3] = p_frame->p_data[2] + chroma_r_size;
  }
  else
  {
    p_frame->p_data[0] = p_frame->p_data[1] = p_frame->p_data[2] = NULL;
  }

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;
  p_frame->data_len[3] = 0; //for hwdesc

  p_frame->video_width = width_aligned;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_DEBUG, "%s: success: p_frame->buffer_size=%u\n", __func__,
         p_frame->buffer_size);

END:

    return retval;
}

/*!*****************************************************************************
 *  \brief  Check if incoming frame is encoder zero copy compatible or not
 *
 *  \param[in] p_enc_ctx    pointer to encoder context
 *        [in] p_enc_params pointer to encoder parameters
 *        [in] width        input width
 *        [in] height       input height
 *        [in] linesize      input linesizes (pointer to array)
 *        [in] set_linesize   setup linesizes 0 means not setup linesizes, 1 means setup linesizes (before encoder open)
 *
 *  \return on success and can do zero copy
 *          NI_RETCODE_SUCCESS
 *
 *          cannot do zero copy
 *          NI_RETCODE_ERROR_UNSUPPORTED_FEATURE
 *          NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 *          NI_RETCODE_INVALID_PARAM
 *
*******************************************************************************/
ni_retcode_t ni_encoder_frame_zerocopy_check(ni_session_context_t *p_enc_ctx,
                                               ni_xcoder_params_t *p_enc_params,
                                               int width, int height,
                                               const int linesize[],
                                               bool set_linesize)
{
    // check pixel format / width / height / linesize can be supported
    if ((!p_enc_ctx) || (!p_enc_params) || (!linesize) 
       || (linesize[0]<=0)
       || (width>NI_MAX_RESOLUTION_WIDTH) || (width<=0)
       || (height>NI_MAX_RESOLUTION_HEIGHT) || (height<=0))
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s passed parameters are null or not supported, "
               "p_enc_ctx %p, p_enc_params %p, linesize %p, "
               "width %d, height %d linesize[0] %d\n",
               __func__, p_enc_ctx, p_enc_params, linesize,
               width, height, (linesize) ? linesize[0] : 0);
        return NI_RETCODE_INVALID_PARAM;
    }

    // check fw revision (if fw_rev has been populated in open session)
    if (p_enc_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] && 
        (ni_cmp_fw_api_ver((char*) &p_enc_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                           "6Q") < 0))
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s: not supported on device with FW API version < 6.Q\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    bool isrgba = false;
    bool isplanar = false;
    bool issemiplanar = false;
    
    switch (p_enc_ctx->pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
            isplanar = true;
            break;
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            issemiplanar = true;
            break;
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            isrgba = true;
            break;
        default:
            ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s: pixel_format %d not supported\n", __func__);
            return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    // check fw revision (if fw_rev has been populated in open session)
    if (issemiplanar && 
        p_enc_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] && 
        (ni_cmp_fw_api_ver((char*) &p_enc_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                           "6q") < 0))
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s: semi-planar not supported on device with FW API version < 6.q\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    // check zero copy compatibilty and set linesize
    if (p_enc_params->zerocopy_mode) // always allow zero copy for RGBA, because RGBA pixel format data copy currently not supported
    {
        if (set_linesize)
        {
            bool ishwframe = (p_enc_params->hwframes) ? true : false;
            int max_linesize = isrgba ? (NI_MAX_RESOLUTION_RGBA_WIDTH*4) : NI_MAX_RESOLUTION_LINESIZE;
            ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s  isrgba %u issemiplanar %u, ishwframe %u, "
                   "p_enc_ctx %p, p_enc_params %p, linesize %p, "
                   "width %d, height %d, linesize[0] %d linesize[1] %d\n",
                   __func__, isrgba, issemiplanar, ishwframe, p_enc_ctx, p_enc_params, linesize,
                   width, height,  linesize[0], linesize[1]);
            
            if (linesize[0] <= max_linesize &&
                linesize[0] % 2 == 0 && //even stride
                linesize[1] % 2 == 0 && //even stride
                width % 2 == 0 && //even width
                height % 2 == 0 && //even height
                width >= NI_MIN_WIDTH &&
                height >= NI_MIN_HEIGHT &&
                !ishwframe &&
                (!isplanar || linesize[2] == linesize[1]) // for planar, make sure cb linesize equal to cr linesize
               )
            {
                // send luma / chorma linesize to device (device is also aware frame will not be padded for 2-pass workaround)
                p_enc_params->luma_linesize = linesize[0];
                p_enc_params->chroma_linesize = (isrgba) ? 0 : linesize[1]; // gstreamer assigns stride length to linesize[0] linesize[1] linesize[2] for RGBA pixel format
                return NI_RETCODE_SUCCESS;
            }
            else
            {
                p_enc_params->luma_linesize = 0;
                p_enc_params->chroma_linesize = 0;
            }
        }
        else if (p_enc_params->luma_linesize || 
                   p_enc_params->chroma_linesize)
        {
            ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s "
                   "luma_linesize %d, chroma_linesize %d, "
                   "linesize[0] %d, linesize[1] %d\n",
                   __func__, p_enc_params->luma_linesize, p_enc_params->chroma_linesize,
                   linesize[0], linesize[1]);        
            if (p_enc_params->luma_linesize != linesize[0] ||
                (p_enc_params->chroma_linesize && p_enc_params->chroma_linesize != linesize[1]) // gstreamer assigns stride length to linesize[0] linesize[1] linesize[2] for RGBA pixel format
               )
            {
#ifndef XCODER_311
                // linesizes can change during SW frame seqeunce change transcoding when FFmpeg noautoscale option is not set
                ni_log2(p_enc_ctx, NI_LOG_ERROR,  "%s: linesize changed from %u %u to %u %u - resolution change?\n", __func__,
                    p_enc_params->luma_linesize, p_enc_params->chroma_linesize,
                    linesize[0], linesize[1]);
#endif
            }
            else
                return NI_RETCODE_SUCCESS;
        }
    }

    return NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for encoder zero copy (metadata, etc.)
  *          for encoding based on given
  *          parameters, taking into account pic linesize and extra data.
  *          Applicable to YUV planr / semi-planar 8 or 10 bit and RGBA pixel formats.
  *
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] data         Picture data pointers (for each of YUV planes)
  *  \param[in] extra_len     Extra data size (incl. meta data)
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_frame_zerocopy_buffer_alloc(ni_frame_t *p_frame, 
                                           int video_width, int video_height,
                                           const int linesize[], const uint8_t *data[],
                                           int extra_len)
{
  int retval = NI_RETCODE_SUCCESS;

  if ((!p_frame) || (!linesize) || (!data))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
             "p_frame %p, linesize %p, data %p\n",
             __func__, p_frame, linesize, data);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_log(NI_LOG_DEBUG,
         "%s: resolution=%dx%d linesize=%d/%d/%d "
         "data=%p %p %p extra_len=%d\n",
         __func__, video_width, video_height,
         linesize[0], linesize[1], linesize[2], 
         data[0], data[1], data[2], extra_len);

  if (p_frame->buffer_size)
  {
      p_frame->buffer_size = 0;  //notify p_frame->p_buffer is not allocated
      ni_aligned_free(p_frame->p_buffer); // also free the temp p_buffer allocated for niFrameSurface1_t in encoder init stage
  }

  p_frame->p_buffer = (uint8_t *)data[0];
  p_frame->p_data[0] = (uint8_t *)data[0];
  p_frame->p_data[1] = (uint8_t *)data[1];
  p_frame->p_data[2] = (uint8_t *)data[2];

  int luma_size = linesize[0] * video_height;
  int chroma_b_size = 0;
  int chroma_r_size = 0;

  // gstreamer assigns stride length to linesize[0] linesize[1] linesize[2] for RGBA pixel format, but only data[0] pointer is populated 
  if (data[1]) // cb size is 0 for RGBA pixel format
    chroma_b_size = linesize[1] * (video_height / 2);

  if (data[2]) // cr size is 0 for semi-planar or RGBA pixel format
    chroma_r_size = linesize[2] * (video_height / 2);
  
  //ni_log(NI_LOG_DEBUG, "%s: luma_size=%d chroma_b_size=%d chroma_r_size=%d\n", __func__, luma_size, chroma_b_size, chroma_r_size);
  
  uint32_t start_offset;
  uint32_t total_start_len = 0;
  int i;

  p_frame->inconsecutive_transfer = 0;

   // rgba has one data pointer, semi-planar has two data pointers
  if ((data[1] && (data[0] + luma_size != data[1]))
      || (data[2] && (data[1] + chroma_b_size != data[2])))
  {
      p_frame->inconsecutive_transfer = 1;
  }

  if (p_frame->inconsecutive_transfer)
  {
      for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
      {
          start_offset = (uintptr_t)p_frame->p_data[i] % NI_MEM_PAGE_ALIGNMENT;
          p_frame->start_len[i] = start_offset ? (NI_MEM_PAGE_ALIGNMENT - start_offset) : 0;
          total_start_len += p_frame->start_len[i];
      }
  }
  else
  {
      start_offset = (uintptr_t)p_frame->p_data[0] % NI_MEM_PAGE_ALIGNMENT;
      p_frame->start_len[0] = start_offset ? (NI_MEM_PAGE_ALIGNMENT - start_offset) : 0;
      p_frame->start_len[1] = p_frame->start_len[2] = 0;
      total_start_len = p_frame->start_len[0];
  }  
  p_frame->total_start_len = total_start_len;

  if (ni_encoder_metadata_buffer_alloc(p_frame, extra_len))
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_metadata_buffer buffer.\n",
             NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  p_frame->separate_metadata = 1;

  if (total_start_len)
  {
      if (ni_encoder_start_buffer_alloc(p_frame))
      {
          ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_start_buffer buffer.\n",
                 NI_ERRNO, __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
      p_frame->separate_start = 1;

      // copy non-4k-aligned part at the start of YUV data
      int start_buffer_offset = 0;
      for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
      {
          if (p_frame->p_data[i])
          {
              memcpy(p_frame->p_start_buffer+start_buffer_offset, p_frame->p_data[i],
                     p_frame->start_len[i]);
              start_buffer_offset += p_frame->start_len[i];
          }
      }
  }

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;
  p_frame->data_len[3] = 0;//unused by hwdesc

  p_frame->video_width = video_width;
  p_frame->video_height = video_height;

  ni_log(NI_LOG_DEBUG,
         "%s: success: p_metadata_buffer %p metadata_buffer_size %u "
         "p_start_buffer %p start_buffer_size %u data_len %u %u %u\n",
         __func__, p_frame->p_metadata_buffer, p_frame->metadata_buffer_size,
         p_frame->p_start_buffer, p_frame->start_buffer_size,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

END:

    return retval;
}


/*!*****************************************************************************
 *  \brief  Check if incoming frame is hwupload zero copy compatible or not
 *
 *  \param[in] p_upl_ctx    pointer to uploader context
 *        [in] width        input width
 *        [in] height       input height
 *        [in] linesize      input linesizes (pointer to array)
 *        [in] pixel_format   input pixel format
 *
 *  \return on success and can do zero copy
 *          NI_RETCODE_SUCCESS
 *
 *          cannot do zero copy
 *          NI_RETCODE_ERROR_UNSUPPORTED_FEATURE
 *          NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 *          NI_RETCODE_INVALID_PARAM
 *
*******************************************************************************/
ni_retcode_t ni_uploader_frame_zerocopy_check(ni_session_context_t *p_upl_ctx,
                                               int width, int height,
                                               const int linesize[], int pixel_format)
{
    // check pixel format / width / height / linesize can be supported
    if ((!p_upl_ctx) || (!linesize) 
       || (linesize[0]<=0) || (linesize[0]>NI_MAX_RESOLUTION_LINESIZE)
       || (width>NI_MAX_RESOLUTION_WIDTH) || (width<=0)
       || (height>NI_MAX_RESOLUTION_HEIGHT) || (height<=0))
    {
        ni_log2(p_upl_ctx, NI_LOG_DEBUG,  "%s passed parameters are null or not supported, "
               "p_enc_ctx %p, linesize %p, "
               "width %d, height %d linesize[0] %d\n",
               __func__, p_upl_ctx, linesize,
               width, height, (linesize) ? linesize[0] : 0);
        return NI_RETCODE_INVALID_PARAM;
    }

    // check fw revision (if fw_rev has been populated in open session)
    if (p_upl_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] && 
        (ni_cmp_fw_api_ver((char*) &p_upl_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                           "6S") < 0))
    {
        ni_log2(p_upl_ctx, NI_LOG_DEBUG,  "%s: not supported on device with FW API version < 6.S\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    // upload does not have zeroCopyMode parameter, currently only allows resolution >= 1080p
    if ((width * height) < NI_NUM_OF_PIXELS_1080P)
        return NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;    

    // check zero copy compatibilty
    ni_log2(p_upl_ctx, NI_LOG_DEBUG,  "%s  pixel_format %d "
           "p_upl_ctx %p, linesize %p, "
           "width %d, height %d, linesize[0] %d\n",
           __func__, pixel_format, p_upl_ctx, linesize,
           width, height,  linesize[0]);

    int bit_depth_factor;
    bool isrgba = false;
    bool isplanar = false;
    bool issemiplanar = false;     

    switch (pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
            isplanar = true;
            bit_depth_factor = 1;
            break;
        case NI_PIX_FMT_YUV420P10LE:
            isplanar = true;
            bit_depth_factor = 2;
            break;
        case NI_PIX_FMT_NV12:
            issemiplanar = true;
            bit_depth_factor = 1;
            break;
        case NI_PIX_FMT_P010LE:
            issemiplanar = true;
            bit_depth_factor = 2;
            break;
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            isrgba = true;
            bit_depth_factor = 4; // not accurate, only for linesize check
            break;
        default:
            ni_log2(p_upl_ctx, NI_LOG_DEBUG,  "%s: pixel_format %d not supported\n", __func__);
            return NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
    }

    // check fw revision (if fw_rev has been populated in open session)
    if (issemiplanar && 
        p_upl_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] && 
        (ni_cmp_fw_api_ver((char*) &p_upl_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                           "6q") < 0))
    {
        ni_log2(p_upl_ctx, NI_LOG_DEBUG,  "%s: semi-planar not supported on device with FW API version < 6.q\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }
    
    int max_linesize = isrgba ? (NI_MAX_RESOLUTION_RGBA_WIDTH*4) : NI_MAX_RESOLUTION_LINESIZE;
    if (linesize[0] <= max_linesize &&
        width % 2 == 0 &&    //even width
        height % 2 == 0 &&   //even height
        width >= NI_MIN_WIDTH && height >= NI_MIN_HEIGHT)
    {
        // yuv only support default 128 bytes aligned linesize, because downstream filter or encoder expect HW frame 128 bytes aligned
        if (isplanar && 
           linesize[0] == NI_VPU_ALIGN128(width * bit_depth_factor) &&
           linesize[1] == NI_VPU_ALIGN128(width * bit_depth_factor / 2) &&
           linesize[2] == linesize[1])
            return NI_RETCODE_SUCCESS;

        // yuv only support default 128 bytes aligned linesize, because downstream filter or encoder expect HW frame 128 bytes aligned
        if (issemiplanar && 
           linesize[0] == NI_VPU_ALIGN128(width * bit_depth_factor) &&
           linesize[1] == linesize[0])
            return NI_RETCODE_SUCCESS;

        // rgba only support 64 bytes aligned for 2D
        if (isrgba && 
           linesize[0] == NI_VPU_ALIGN64(width * bit_depth_factor))
            return NI_RETCODE_SUCCESS;
    }

    return NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data.
  *          Applicable to YUV420p AVFrame only. 8 or 10 bit/pixel.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement
  *  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not 
  *                           to allocate any buffer (zero-copy from existing)
  *  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_frame_buffer_alloc(ni_frame_t *p_frame, int video_width,
                                           int video_height, int linesize[],
                                           int alignment, int extra_len,
                                           bool alignment_2pass_wa)
{
  void* p_buffer = NULL;
  int height_aligned;
  int retval = NI_RETCODE_SUCCESS;

  if ((!p_frame) || (!linesize) || (linesize[0]<=0) || (linesize[0]>NI_MAX_RESOLUTION_LINESIZE)
     || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width<=0)
     || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height<=0))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
             "p_frame %p, linesize %p, video_width %d, video_height %d\n",
             __func__, p_frame, linesize, video_width, video_height);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (QUADRA)
  {
    height_aligned = ((video_height + 1) / 2) * 2;
  } else
  {
      height_aligned = ((video_height + 7) / 8) * 8;

      if (alignment)
      {
          height_aligned = ((video_height + 15) / 16) * 16;
      }
  }
  if (height_aligned < NI_MIN_HEIGHT)
  {
    height_aligned = NI_MIN_HEIGHT;
  }

  ni_log(NI_LOG_DEBUG,
         "%s: aligned=%dx%d org=%dx%d linesize=%d/%d/%d "
         "extra_len=%d\n",
         __func__, video_width, height_aligned, video_width, video_height,
         linesize[0], linesize[1], linesize[2], extra_len);

  int luma_size = linesize[0] * height_aligned;
  int chroma_b_size;
  int chroma_r_size;
  if (QUADRA)
  {
    chroma_b_size = chroma_r_size = linesize[1] * (height_aligned / 2);
    if (alignment_2pass_wa)
    {
        // for 2-pass encode output mismatch WA, need to extend (and pad) Cr plane height, because 1st pass assume input 32 align
        chroma_r_size = linesize[1] * (((height_aligned + 31) / 32) * 32) / 2;
    }
    //ni_log(NI_LOG_DEBUG, "%s: luma_size=%d chroma_b_size=%d chroma_r_size=%d\n", __func__, luma_size, chroma_b_size, chroma_r_size);
  }
  else
  {
    chroma_b_size = luma_size / 4;
    chroma_r_size = luma_size / 4;
  }
  if(extra_len >= 0)
  {
      int buffer_size = luma_size + chroma_b_size + chroma_r_size + extra_len;

      buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT;

      //Check if Need to free
      if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
      {
          ni_log(NI_LOG_DEBUG,
                 "%s: free current p_frame, "
                 "p_frame->buffer_size=%u\n",
                 __func__, p_frame->buffer_size);
          ni_frame_buffer_free(p_frame);
      }

      //Check if need to realocate
      if (p_frame->buffer_size != buffer_size)
      {
          if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
          {
              ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_frame buffer.\n",
                     NI_ERRNO, __func__);
              retval = NI_RETCODE_ERROR_MEM_ALOC;
              LRETURN;
          }

          // init once after allocation
          memset(p_buffer, 0, buffer_size);
          p_frame->buffer_size = buffer_size;
          p_frame->p_buffer = p_buffer;

          ni_log(NI_LOG_DEBUG, "%s: allocated new p_frame buffer\n", __func__);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
      }
      p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
      p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
      p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_b_size;
  }
  else
  {      
      p_frame->buffer_size = 0; //no ownership
  }

  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_b_size;
  p_frame->data_len[2] = chroma_r_size;
  p_frame->data_len[3] = 0;//unused by hwdesc

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_DEBUG,
         "%s: success: p_frame->p_buffer %p "
         "p_frame->buffer_size=%u\n",
         __func__, p_frame->p_buffer, p_frame->buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

/*!*****************************************************************************
 *  \brief  allocate device destination frame from scaler hwframe pool
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
ni_retcode_t ni_scaler_dest_frame_alloc(ni_session_context_t *p_ctx,
                                        ni_scaler_input_params_t scaler_params,
                                        niFrameSurface1_t *p_surface)
{
    int ret = 0;
    if (scaler_params.op != NI_SCALER_OPCODE_OVERLAY && scaler_params.op != NI_SCALER_OPCODE_WATERMARK)
    {
        ret = ni_device_alloc_frame(
            p_ctx, scaler_params.output_width, scaler_params.output_height,
            scaler_params.output_format, NI_SCALER_FLAG_IO,
            scaler_params.out_rec_width, scaler_params.out_rec_height,
            scaler_params.out_rec_x, scaler_params.out_rec_y,
            scaler_params.rgba_color, -1, NI_DEVICE_TYPE_SCALER);
    } else
    {
        // in vf_overlay_ni.c: flags = (s->alpha_format ? NI_SCALER_FLAG_PA : 0) | NI_SCALER_FLAG_IO;
        ret = ni_device_alloc_frame(
            p_ctx, scaler_params.output_width, scaler_params.output_height,
            scaler_params.output_format, NI_SCALER_FLAG_IO,
            scaler_params.out_rec_width, scaler_params.out_rec_height,
            scaler_params.out_rec_x, scaler_params.out_rec_y,
            p_surface->ui32nodeAddress, p_surface->ui16FrameIdx,
            NI_DEVICE_TYPE_SCALER);
    }
    return ret;
}

/*!*****************************************************************************
 *  \brief  allocate device input frame by hw descriptor. This call won't actually allocate
            a frame but sends the incoming hardware frame index to the scaler manager
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
ni_retcode_t ni_scaler_input_frame_alloc(ni_session_context_t *p_ctx,
                                         ni_scaler_input_params_t scaler_params,
                                         niFrameSurface1_t *p_src_surface)
{
    int ret = 0;

    ret = ni_device_alloc_frame(
        p_ctx, scaler_params.input_width, scaler_params.input_height,
        scaler_params.input_format, 0, scaler_params.in_rec_width,
        scaler_params.in_rec_height, scaler_params.in_rec_x,
        scaler_params.in_rec_y, p_src_surface->ui32nodeAddress,
        p_src_surface->ui16FrameIdx, NI_DEVICE_TYPE_SCALER);
    return ret;
}

/*!*****************************************************************************
 *  \brief  init output pool of scaler frames
 *
 *  \param  
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
ni_retcode_t ni_scaler_frame_pool_alloc(ni_session_context_t *p_ctx,
                                        ni_scaler_input_params_t scaler_params)
{
    int rc = 0;
    int options = NI_SCALER_FLAG_IO | NI_SCALER_FLAG_PC;
    if (p_ctx->isP2P)
        options |= NI_SCALER_FLAG_P2;

    /* Allocate a pool of frames by the scaler */
    rc = ni_device_alloc_frame(p_ctx, scaler_params.output_width,
                               scaler_params.output_height,
                               scaler_params.output_format, options,
                               0,           // rec width
                               0,           // rec height
                               0,           // rec X pos
                               0,           // rec Y pos
                               NI_MAX_FILTER_POOL_SIZE,  // rgba color/pool size
                               0,           // frame index
                               NI_DEVICE_TYPE_SCALER);
    return rc;
}

/*!*****************************************************************************
*  \brief  Allocate memory for the frame buffer based on provided parameters
*          taking into account pic line size and extra data.
*          Applicable to nv12 AVFrame only. Cb/Cr size matches that of Y.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] linesize      Picture line size
*  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not
*                           to allocate any buffer (zero-copy from existing)
*  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
ni_retcode_t ni_frame_buffer_alloc_nv(ni_frame_t *p_frame, int video_width,
                                      int video_height, int linesize[],
                                      int extra_len, bool alignment_2pass_wa)
{
  void* p_buffer = NULL;
  int height_aligned;
  int retval = NI_RETCODE_SUCCESS;

  if ((!p_frame) || (!linesize) || (linesize[0] <= 0) || (linesize[0]>NI_MAX_RESOLUTION_LINESIZE)
    || (video_width>NI_MAX_RESOLUTION_WIDTH) || (video_width <= 0)
    || (video_height>NI_MAX_RESOLUTION_HEIGHT) || (video_height <= 0))
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s passed parameters are null or not supported, "
             "p_frame %p, linesize %p, video_width %d, video_height %d\n",
             __func__, p_frame, linesize, video_width, video_height);
      return NI_RETCODE_INVALID_PARAM;
  }

  height_aligned = ((video_height + 1) / 2) * 2;

  if (height_aligned < NI_MIN_HEIGHT)
  {
    height_aligned = NI_MIN_HEIGHT;
  }

  ni_log(NI_LOG_DEBUG,
         "%s: aligned=%dx%d org=%dx%d linesize=%d/%d/%d extra_len=%d\n",
         __func__, video_width, height_aligned, video_width, video_height,
         linesize[0], linesize[1], linesize[2], extra_len);

  int luma_size = linesize[0] * height_aligned;
  int chroma_br_size = luma_size / 2;
  //int chroma_r_size = luma_size / 4;
  if (alignment_2pass_wa)
  {
      // for 2-pass encode output mismatch WA, need to extend (and pad) CbCr plane height, because 1st pass assume input 32 align
      chroma_br_size = linesize[0] * ((((height_aligned + 31) / 32) * 32) / 2);
  }
  if (extra_len >= 0)
  {
      int buffer_size = luma_size + chroma_br_size + extra_len;
      
      buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT;
      
      //Check if Need to free
      if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
      {
          ni_log(NI_LOG_DEBUG, "%s: free current p_frame->buffer_size=%u\n",
                 __func__, p_frame->buffer_size);
          ni_frame_buffer_free(p_frame);
      }
      
      //Check if need to realocate
      if (p_frame->buffer_size != buffer_size)
      {
          if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
          {
              ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_frame buffer.\n",
                     NI_ERRNO, __func__);
              retval = NI_RETCODE_ERROR_MEM_ALOC;
              LRETURN;
          }
      
          // init once after allocation
          memset(p_buffer, 0, buffer_size);
          p_frame->buffer_size = buffer_size;
          p_frame->p_buffer = p_buffer;
          
          ni_log(NI_LOG_DEBUG, "%s: allocated new p_frame buffer\n", __func__);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
      }
      
      p_frame->p_data[0] = (uint8_t*)p_frame->p_buffer;
      p_frame->p_data[1] = (uint8_t*)p_frame->p_data[0] + luma_size;
      p_frame->p_data[2] = (uint8_t*)p_frame->p_data[1] + chroma_br_size;
  }
  else
  {
      p_frame->buffer_size = 0; //no ownership
  }
  p_frame->data_len[0] = luma_size;
  p_frame->data_len[1] = chroma_br_size;
  p_frame->data_len[2] = 0;

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_DEBUG, "%s: success: p_frame->buffer_size=%u\n", __func__,
         p_frame->buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

/*!*****************************************************************************
  *  \brief  This API is a wrapper for ni_encoder_frame_buffer_alloc(), used
  *          for planar pixel formats, and ni_frame_buffer_alloc_nv(), used for
  *          semi-planar pixel formats. This API is meant to combine the
  *          functionality for both individual format APIs.
  *          Allocate memory for the frame buffer for encoding based on given
  *          parameters, taking into account pic line size and extra data.
  *          Applicable to YUV420p(8 or 10 bit/pixel) or nv12 AVFrame.
  *          Cb/Cr size matches that of Y.
  *
  *  \param[in] planar        true: if planar:
  *                           pixel_format == (NI_PIX_FMT_YUV420P ||
  *                               NI_PIX_FMT_YUV420P10LE ||NI_PIX_FMT_RGBA).
  *                           false: semi-planar:
  *                           pixel_format == (NI_PIX_FMT_NV12 ||
  *                                NI_PIX_FMT_P010LE).
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *  \param[in] video_width   Width of the video frame
  *  \param[in] video_height  Height of the video frame
  *  \param[in] linesize      Picture line size
  *  \param[in] alignment     Allignment requirement. Only used for planar format.
  *  \param[in] extra_len     Extra data size (incl. meta data). < 0 means not 
  *                           to allocate any buffer (zero-copy from existing)
  *  \param[in] alignment_2pass_wa set alignment to work with 2pass encode
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_sw_frame_buffer_alloc(bool planar, ni_frame_t *p_frame,
                                              int video_width, int video_height,
                                              int linesize[], int alignment,
                                              int extra_len,
                                              bool alignment_2pass_wa)
{
    if (true == planar)
    {
        return ni_encoder_frame_buffer_alloc(p_frame, video_width, video_height,
                                             linesize, alignment, extra_len,
                                             alignment_2pass_wa);
    }
    else
    {
        return ni_frame_buffer_alloc_nv(p_frame, video_width, video_height,
                                        linesize, extra_len,
                                        alignment_2pass_wa);
    }
}

/*!*****************************************************************************
 *  \brief  Free frame buffer that was previously allocated with either
 *          ni_frame_buffer_alloc or ni_encoder_frame_buffer_alloc or
 *          ni_frame_buffer_alloc_nv
 *
 *  \param[in] p_frame    Pointer to a previously allocated ni_frame_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_frame_buffer_free(ni_frame_t* p_frame)
{
  int i;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "%s: enter\n", __func__);

  if (!p_frame)
  {
      ni_log(NI_LOG_DEBUG, "WARN: %s(): p_frame is NULL\n", __func__);
      LRETURN;
  }

  if (!p_frame->p_buffer)
  {
      ni_log(NI_LOG_DEBUG, "WARN: %s(): already freed, nothing to free\n",
             __func__);
  }

#ifndef _WIN32
  // If this is a hardware frame with a DMA buf fd attached, close the DMA buf fd
  if ((p_frame->data_len[3] > 0) && (p_frame->p_data[3] != NULL))
  {
      niFrameSurface1_t *p_surface = (niFrameSurface1_t *)p_frame->p_data[3];
      if ((p_surface->dma_buf_fd > 0) && (p_surface->ui16FrameIdx > 0))
      {
          // Close the DMA buf fd
          ni_log(NI_LOG_DEBUG,
                 "%s: close p_surface->dma_buf_fd %d "
                 "ui16FrameIdx %u\n",
                 __func__, p_surface->dma_buf_fd, p_surface->ui16FrameIdx);
          close(p_surface->dma_buf_fd);
      }
  }
#endif

  if (p_frame->buffer_size)
  {
      p_frame->buffer_size = 0;
      ni_aligned_free(p_frame->p_buffer);
  }
  
  for (i = 0; i < NI_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }
  
  ni_frame_wipe_aux_data(p_frame);
  
  if (p_frame->metadata_buffer_size)
  {
      p_frame->metadata_buffer_size = 0;
      ni_aligned_free(p_frame->p_metadata_buffer);
  }
  p_frame->separate_metadata = 0;

  if (p_frame->start_buffer_size)
  {
      p_frame->start_buffer_size = 0;
      ni_aligned_free(p_frame->p_start_buffer);
  }
  p_frame->separate_start = 0;
  memset(p_frame->start_len, 0, sizeof(p_frame->start_len));
  p_frame->total_start_len = 0;
  p_frame->inconsecutive_transfer = 0;

END:

    ni_log(NI_LOG_TRACE, "%s: exit\n", __func__);

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

  ni_log(NI_LOG_TRACE, "%s: enter\n", __func__);

  if (!p_frame)
  {
      ni_log(NI_LOG_DEBUG, "WARN: %s(): p_frame is NULL\n", __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (p_frame->dec_buf)
  {
    ni_buf_pool_return_buffer(p_frame->dec_buf, (ni_buf_pool_t *)p_frame->dec_buf->pool);
    ni_log(NI_LOG_DEBUG, "%s(): Mem buf returned ptr %p buf %p !\n", __func__,
           p_frame->dec_buf->buf, p_frame->dec_buf);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s(): NO mem buf returned !\n", __func__);
  }

  p_frame->dec_buf = NULL;
  p_frame->p_buffer = NULL;
  p_frame->buffer_size = 0;
  for (i = 0; i < NI_MAX_NUM_DATA_POINTERS; i++)
  {
    p_frame->data_len[i] = 0;
    p_frame->p_data[i] = NULL;
  }
  ni_frame_wipe_aux_data(p_frame);

END:

    ni_log(NI_LOG_TRACE, "%s: exit\n", __func__);

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
void ni_decoder_frame_buffer_pool_return_buf(ni_buf_t *buf,
                                             ni_buf_pool_t *p_buffer_pool)
{
  ni_buf_pool_return_buffer(buf, p_buffer_pool);
}

/*!*****************************************************************************
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
 ******************************************************************************/
ni_retcode_t ni_packet_buffer_alloc(ni_packet_t* p_packet, int packet_size)
{
  void* p_buffer = NULL;
  int metadata_size = 0;

  metadata_size = NI_FW_META_DATA_SZ;

  int buffer_size = (((packet_size + metadata_size) / NI_MAX_PACKET_SZ) + 1) * NI_MAX_PACKET_SZ;

  ni_log(NI_LOG_TRACE, "%s: packet_size=%d\n", __func__,
         packet_size + metadata_size);

  if (!p_packet || !packet_size)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s: null pointer parameters passed\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (buffer_size % NI_MEM_PAGE_ALIGNMENT)
  {
    buffer_size = ( (buffer_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT ) + NI_MEM_PAGE_ALIGNMENT;
  }

  if (p_packet->buffer_size == buffer_size)
  {
      // Already allocated the exact size.
      p_packet->p_data = p_packet->p_buffer;
      ni_log(NI_LOG_DEBUG, "%s: reuse current p_packet buffer\n", __func__);
      ni_log(NI_LOG_TRACE, "%s: exit: p_packet->buffer_size=%u\n", __func__,
             p_packet->buffer_size);
      return NI_RETCODE_SUCCESS;
  }

  if (p_packet->buffer_size)
  {
      ni_log(NI_LOG_DEBUG,
             "%s: free current p_packet, p_packet->buffer_size=%u\n", __func__,
             p_packet->buffer_size);
      ni_packet_buffer_free(p_packet);
  }

  ni_log(NI_LOG_DEBUG, "%s: Allocating p_frame buffer, buffer_size=%d\n",
         __func__, buffer_size);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_packet buffer.\n",
             NI_ERRNO, __func__);
      ni_log(NI_LOG_TRACE, "%s: exit: p_packet->buffer_size=%u\n", __func__,
             p_packet->buffer_size);
      return NI_RETCODE_ERROR_MEM_ALOC;
  }

  p_packet->buffer_size = buffer_size;
  p_packet->p_buffer = p_buffer;
  p_packet->p_data = p_packet->p_buffer;

  ni_log(NI_LOG_TRACE, "%s: exit: p_packet->buffer_size=%u\n", __func__,
         p_packet->buffer_size);

  return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Allocate packet buffer using a user provided pointer, the memory
 *   is expected to have already been allocated.
 *
 *   For ideal performance memory should be 4k aligned. If it is not 4K aligned
 *   then a temporary 4k aligned memory will be used to copy data to and from
 *   when writing and reading. This will negatively impact performance.
 *
 *   This API will overwrite p_packet->buffer_size, p_packet->p_buffer and
 *   p_packet->p_data fields in p_packet.
 *
 *   This API will not free any memory associated with p_packet->p_buffer and
 *   p_packet->p_data fields in p_packet.
 *   Common use case could be,
 *       1. Allocate memory to pointer
 *       2. Call ni_custom_packet_buffer_alloc() with allocated pointer.
 *       3. Use p_packet as required.
 *       4. Call ni_packet_buffer_free() to free up the memory.
 *
 *  \param[in] p_buffer      User provided pointer to be used for buffer
 *  \param[in] p_packet      Pointer to a caller allocated
 *                                               ni_packet_t struct
 *  \param[in] buffer_size   Buffer size
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_custom_packet_buffer_alloc(void *p_buffer,
                                           ni_packet_t *p_packet,
                                           int buffer_size)
{
    ni_log(NI_LOG_TRACE, "%s(): enter buffer_size=%d\n", __func__, buffer_size);

    if (!p_buffer || !p_packet || !buffer_size)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s: null pointer parameters passed\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    if (((uintptr_t)p_buffer) % NI_MEM_PAGE_ALIGNMENT)
    {
        ni_log(NI_LOG_INFO, "Info: %s: Warning buffer not 4k aligned = %p!. Will do an extra copy\n",
               __func__, p_buffer);
    }

    p_packet->buffer_size = buffer_size;
    p_packet->p_buffer = p_buffer;
    p_packet->p_data = p_packet->p_buffer;

    ni_log(NI_LOG_TRACE, "%s: exit: \n", __func__);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Free packet buffer that was previously allocated with
 *          ni_packet_buffer_alloc
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_packet_buffer_free(ni_packet_t* p_packet)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_packet)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): p_packet is NULL\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

  if (!p_packet->p_buffer)
  {
      ni_log(NI_LOG_DEBUG, "%s(): already freed, nothing to free\n", __func__);
      LRETURN;
  }

  ni_aligned_free(p_packet->p_buffer);
  p_packet->p_buffer = NULL;
  p_packet->buffer_size = 0;
  p_packet->data_len = 0;
  p_packet->p_data = NULL;

  ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

END:

    return retval;
}

/*!*****************************************************************************
 *  \brief  Free packet buffer that was previously allocated with
 *          ni_packet_buffer_alloc for AV1 packets merge
 *
 *  \param[in] p_packet    Pointer to a previously allocated ni_packet_t struct
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_packet_buffer_free_av1(ni_packet_t *p_packet)
{
    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    int i;

    if (!p_packet)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): p_packet is NULL\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    if (!p_packet->av1_buffer_index)
    {
        ni_log(NI_LOG_DEBUG,
               "%s(): no need to free previous av1 packet buffers\n", __func__);
        LRETURN;
    }

    for (i = 0; i < p_packet->av1_buffer_index; i++)
    {
        ni_log(NI_LOG_DEBUG, "%s(): free previous av1 packet buffer %d\n",
               __func__, i);
        ni_aligned_free(p_packet->av1_p_buffer[i]);
        p_packet->av1_p_buffer[i] = NULL;
        p_packet->av1_p_data[i] = NULL;
        p_packet->av1_buffer_size[i] = 0;
        p_packet->av1_data_len[i] = 0;
    }

    p_packet->av1_buffer_index = 0;

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Copy video packet accounting for alignment
 *
 *  \param[in] p_destination  Destination to where to copy to
 *  \param[in] p_source       Source from where to copy from
 *  \param[in] cur_size       current size
 *  \param[out] p_leftover    Pointer to the data that was left over
 *  \param[out] p_prev_size   Size of the data leftover ??
 *
 *  \return On success        Total number of bytes that were copied
 *          On failure        NI_RETCODE_FAILURE
 ******************************************************************************/
int ni_packet_copy(void* p_destination, const void* const p_source, int cur_size, void* p_leftover, int* p_prev_size)
{
  int copy_size = 0;
  int padding_size = 0;
  int prev_size = p_prev_size == NULL? 0 : *p_prev_size;

  int total_size = cur_size + prev_size;
  uint8_t* p_src = (uint8_t*)p_source;
  uint8_t* p_dst = (uint8_t*)p_destination;
  uint8_t* p_lftover = (uint8_t*)p_leftover;

  if (!p_prev_size)
  {
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_log(NI_LOG_TRACE, "%s(): enter, *prev_size=%d\n", __func__, *p_prev_size);

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

  memcpy(p_dst, p_src, cur_size);

  if (padding_size)
  {
    p_dst += cur_size;
    memset(p_dst, 0, padding_size);
  }

  ni_log(NI_LOG_TRACE,
         "%s(): exit, cur_size=%d, copy_size=%d, "
         "prev_size=%d, padding_size=%d\n", __func__, cur_size,
         copy_size, *p_prev_size, padding_size);

  *p_prev_size = 0;

  return copy_size;
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
ni_aux_data_t *ni_frame_new_aux_data(ni_frame_t *frame, ni_aux_data_type_t type,
                                     int data_size)
{
    ni_aux_data_t *ret;

    if (frame->nb_aux_data >= NI_MAX_NUM_AUX_DATA_PER_FRAME ||
        !(ret = malloc(sizeof(ni_aux_data_t))))
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s No memory or exceeding max aux_data number !\n",
               __func__);
        return NULL;
    }

    ret->type = type;
    ret->size = data_size;
    ret->data = calloc(1, data_size);
    if (!ret->data)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s No memory for aux data !\n", __func__);
        free(ret);
        ret = NULL;
    } else
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
ni_aux_data_t *ni_frame_new_aux_data_from_raw_data(ni_frame_t *frame,
                                                   ni_aux_data_type_t type,
                                                   const uint8_t *raw_data,
                                                   int data_size)
{
    ni_aux_data_t *ret = ni_frame_new_aux_data(frame, type, data_size);
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
ni_aux_data_t *ni_frame_get_aux_data(const ni_frame_t *frame,
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
void ni_frame_free_aux_data(ni_frame_t *frame, ni_aux_data_type_t type)
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
void ni_frame_wipe_aux_data(ni_frame_t *frame)
{
    int i;
    ni_aux_data_t *aux;

    for (i = 0; i < frame->nb_aux_data; i++)
    {
        aux = frame->aux_data[i];
        free(aux->data);
        free(aux);
    }
    frame->nb_aux_data = 0;
}

/*!*****************************************************************************
 *  \brief  Initialize default encoder parameters
 *
 *  \param[out] param        Pointer to a user allocated ni_xcoder_params_t
 *                           to initialize to default parameters
 *  \param[in] fps_num       Frames per second
 *  \param[in] fps_denom     FPS denomination
 *  \param[in] bit_rate      bit rate
 *  \param[in] width         frame width
 *  \param[in] height        frame height
 *  \param[in] codec_format  codec from ni_codec_format_t
 *
 *  \return On success
 *                           NI_RETCODE_SUCCESS
 *          On failure
 *                           NI_RETCODE_FAILURE
 *                           NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_encoder_init_default_params(ni_xcoder_params_t *p_param,
                                            int fps_num, int fps_denom,
                                            long bit_rate, int width,
                                            int height,
                                            ni_codec_format_t codec_format)
{
    ni_encoder_cfg_params_t *p_enc = NULL;
    int i = 0;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    //Initialize p_param structure
    if (!p_param)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): null pointer parameters passed\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG, "%s()\n", __func__);

    //Initialize p_param structure
    memset(p_param, 0, sizeof(ni_xcoder_params_t));

    p_enc = &p_param->cfg_enc_params;

    // Rev. B: unified for HEVC/H.264
    p_enc->profile = 0;
    p_enc->level_idc = 0;
    p_enc->high_tier = 0;

    if (QUADRA)
        p_enc->gop_preset_index = GOP_PRESET_IDX_DEFAULT;
    else
        p_enc->gop_preset_index = GOP_PRESET_IDX_IBBBP;

    p_enc->use_recommend_enc_params = 0;
    p_enc->cu_size_mode = 7;
    p_enc->max_num_merge = 2;
    p_enc->enable_dynamic_8x8_merge = 1;
    p_enc->enable_dynamic_16x16_merge = 1;
    p_enc->enable_dynamic_32x32_merge = 1;

    p_enc->rc.trans_rate = 0;

    p_enc->rc.enable_rate_control = 0;
    if (QUADRA)
        p_enc->rc.enable_cu_level_rate_control = 0;
    else
        p_enc->rc.enable_cu_level_rate_control = 1;
    p_enc->rc.enable_hvs_qp = 0;
    p_enc->rc.enable_hvs_qp_scale = 1;
    p_enc->rc.hvs_qp_scale = 2;
    p_enc->rc.min_qp = 8;
    p_enc->rc.max_qp = 51;
    p_enc->rc.max_delta_qp = 10;

    // hrd is disabled if vbv_buffer_size=0, hrd is enabled if vbv_buffer_size is [10, 3000]
    p_enc->rc.vbv_buffer_size = -1;
    p_enc->rc.enable_filler = 0;
    p_enc->rc.enable_pic_skip = 0;
    p_enc->rc.vbv_max_rate = 0;

    p_enc->roi_enable = 0;

    p_enc->forced_header_enable = NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES;
    // feature not supported on JPEG/AV1 - disable by default
    if (codec_format == NI_CODEC_FORMAT_JPEG ||
        codec_format == NI_CODEC_FORMAT_AV1)
    {
        p_enc->forced_header_enable = NI_ENC_REPEAT_HEADERS_FIRST_IDR;
    }

    p_enc->long_term_ref_enable = 0;
    p_enc->long_term_ref_interval = 0;
    p_enc->long_term_ref_count = 2;

    p_enc->conf_win_top = 0;
    p_enc->conf_win_bottom = 0;
    p_enc->conf_win_left = 0;
    p_enc->conf_win_right = 0;

    p_enc->intra_period = 120;
    p_enc->rc.intra_qp = 22;
    p_enc->rc.intra_qp_delta = -2;
    if (QUADRA)
        p_enc->rc.enable_mb_level_rc = 0;
    else
        p_enc->rc.enable_mb_level_rc = 1;

    p_enc->decoding_refresh_type = 1;

    p_enc->slice_mode = 0;
    p_enc->slice_arg = 0;

    // Rev. B: H.264 only parameters.
    p_enc->enable_transform_8x8 = 1;
    p_enc->entropy_coding_mode = 1;

    p_enc->intra_mb_refresh_mode = 0;
    p_enc->intra_mb_refresh_arg = 0;
    p_enc->intra_reset_refresh = 0;

    p_enc->custom_gop_params.custom_gop_size = 0;
#ifndef QUADRA
  if (!QUADRA)
  {
    for (i = 0; i < NI_MAX_GOP_NUM; i++)
    {
      p_enc->custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_enc->custom_gop_params.pic_param[i].poc_offset = 0;
      p_enc->custom_gop_params.pic_param[i].pic_qp = 0;
      p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;
      p_enc->custom_gop_params.pic_param[i].ref_poc_L0 = 0;
      p_enc->custom_gop_params.pic_param[i].ref_poc_L1 = 0;
      p_enc->custom_gop_params.pic_param[i].temporal_id = 0;
    }
  }
  else // QUADRA
#endif
  {
    int j;
    for (i = 0; i < NI_MAX_GOP_NUM; i++)
    {
      p_enc->custom_gop_params.pic_param[i].poc_offset = 0;
      p_enc->custom_gop_params.pic_param[i].qp_offset = 0;
      p_enc->custom_gop_params.pic_param[i].qp_factor =
          (float)0.3;   // QP Factor range is between 0.3 and 1, higher values mean lower quality and less bits
      p_enc->custom_gop_params.pic_param[i].temporal_id = 0;
      p_enc->custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_enc->custom_gop_params.pic_param[i].num_ref_pics= 0;
      for (j = 0; j < NI_MAX_REF_PIC; j++)
      {
        p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic = 0;
        p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic_used = -1;
      }
    }
  }

  p_param->source_width = width;
  p_param->source_height = height;

  p_param->fps_number = fps_num;
  p_param->fps_denominator = fps_denom;

  if (p_param->fps_number && p_param->fps_denominator)
  {
      p_enc->frame_rate = (int)(p_param->fps_number / p_param->fps_denominator);
  }
  else
  {
    p_enc->frame_rate = 30;
  }

  p_param->bitrate = (int)bit_rate;
  p_param->roi_demo_mode = 0;
  p_param->reconf_demo_mode = 0; // NETINT_INTERNAL - currently only for internal testing
  p_param->force_pic_qp_demo_mode = 0;
  p_param->force_frame_type = 0;
  p_param->hdrEnableVUI = 0;
  p_param->cacheRoi = 0;
  p_param->low_delay_mode = 0;
  p_param->padding = 1;
  p_param->generate_enc_hdrs = 0;
  p_param->use_low_delay_poc_type = 0;
  p_param->rootBufId = 0;
  p_param->staticMmapThreshold = 0;
  p_param->zerocopy_mode = 1;
  p_param->luma_linesize = 0;
  p_param->chroma_linesize = 0;

  p_enc->preferred_transfer_characteristics = -1;

  p_param->dolby_vision_profile = 0;

  // encoder stream header VUI setting
  p_param->color_primaries = 2;                 // default COL_PRI_UNSPECIFIED
  p_param->color_transfer_characteristic = 2;   // default COL_TRC_UNSPECIFIED
  p_param->color_space = 2;                     // default COL_SPC_UNSPECIFIED
  p_param->sar_num = 0;                         // default SAR numerator 0
  p_param->sar_denom = 1;                       // default SAR denominator 1
  p_param->video_full_range_flag = -1;

  p_param->enable2PassGop = 0;
  p_param->ddr_priority_mode = NI_DDR_PRIORITY_NONE;
  p_param->minFramesDelay = 0;

  //QUADRA
  p_enc->EnableAUD = 0;
  p_enc->lookAheadDepth = 0;
  p_enc->rdoLevel = (codec_format == NI_CODEC_FORMAT_JPEG) ? 0 : 1;
  p_enc->crf = -1;
  p_enc->HDR10MaxLight = 0;
  p_enc->HDR10AveLight = 0;
  p_enc->HDR10CLLEnable = 0;
  p_enc->EnableRdoQuant = 0;
  p_enc->ctbRcMode = 0;
  p_enc->gopSize = 0;
  p_enc->gopLowdelay = 0;
  p_enc->gdrDuration = 0;
  p_enc->hrdEnable = 0;
  p_enc->ltrRefInterval = 0;
  p_enc->ltrRefQpOffset = 0;
  p_enc->ltrFirstGap = 0;
  p_enc->ltrNextInterval = 1;
  p_enc->multicoreJointMode = 0;
  p_enc->qlevel = -1;
  p_enc->maxFrameSize = 0;
  p_enc->maxFrameSizeRatio = 0;
  p_enc->chromaQpOffset = 0;
  p_enc->tolCtbRcInter = (float)0.1;
  p_enc->tolCtbRcIntra = (float)0.1;
  p_enc->bitrateWindow = -255;
  p_enc->inLoopDSRatio = 1;
  p_enc->blockRCSize = 0;
  p_enc->rcQpDeltaRange = 10;
  p_enc->ctbRowQpStep = 0;
  p_enc->newRcEnable = -1;
  p_enc->colorDescPresent = 0;
  p_enc->colorPrimaries = 2;
  p_enc->colorTrc = 2;
  p_enc->colorSpace = 2;
  p_enc->aspectRatioWidth = 0;
  p_enc->aspectRatioHeight = 1;
  p_enc->videoFullRange = 0;
  p_enc->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;
  p_enc->enable_ssim = 0;
  p_enc->HDR10Enable = 0;
  p_enc->HDR10dx0 = 0;
  p_enc->HDR10dy0 = 0;
  p_enc->HDR10dx1 = 0;
  p_enc->HDR10dy1 = 0;
  p_enc->HDR10dx2 = 0;
  p_enc->HDR10dy2 = 0;
  p_enc->HDR10wx = 0;
  p_enc->HDR10wy = 0;
  p_enc->HDR10maxluma = 0;
  p_enc->HDR10minluma = 0;
  p_enc->av1_error_resilient_mode = 0;
  p_enc->temporal_layers_enable = 0;
  p_enc->crop_width = 0;
  p_enc->crop_height = 0;
  p_enc->hor_offset = 0;
  p_enc->ver_offset = 0;
  p_enc->crfMax = -1;
  p_enc->qcomp = (float)0.6;
  p_enc->noMbtree = 0;
  p_enc->noHWMultiPassSupport = 0;
  p_enc->cuTreeFactor = 5;
  p_enc->ipRatio = (float)1.4;
  p_enc->pbRatio = (float)1.3;
  p_enc->cplxDecay = (float)0.5;
  p_enc->pps_init_qp = -1;
  p_enc->bitrateMode = -1;   // -1 = not set, bitrateMode 0 = max bitrate (actual bitrate can be lower than bitrate), bitrateMode 1 = average bitrate (actual bitrate approximately equal to bitrate)
  p_enc->pass1_qp = -1;  
  p_enc->crfFloat = -1.0f;
  p_enc->hvsBaseMbComplexity = 15;
  p_enc->statistic_output_level = 0;
  p_enc->skip_frame_enable = 0;
  p_enc->max_consecutive_skip_num = 1;
  p_enc->skip_frame_interval = 0;
  p_enc->enableipRatio = 0;
  p_enc->enable_all_sei_passthru = 0;
  p_enc->iframe_size_ratio = 100;
  p_enc->crf_max_iframe_enable = 0;
  p_enc->vbv_min_rate = 0;
  p_enc->disable_adaptive_buffers = 0;
  p_enc->disableBframeRdoq = 0;
  p_enc->forceBframeQpfactor = (float)-1.0;
  p_enc->tune_bframe_visual = 0;
  p_enc->enable_acq_limit = 0;

  if (codec_format == NI_CODEC_FORMAT_AV1)
  {
      if (p_param->source_width < NI_PARAM_AV1_MIN_WIDTH)
      {
          retval = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL;
          LRETURN;
      }
      if (p_param->source_height < NI_PARAM_AV1_MIN_HEIGHT)
      {
          retval = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL;
          LRETURN;
      }
      if (p_param->source_width > NI_PARAM_AV1_MAX_WIDTH)
      {
          retval = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
          LRETURN;
      }
      if (p_param->source_height > NI_PARAM_AV1_MAX_HEIGHT)
      {
          retval = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
          LRETURN;
      }
      if (p_param->source_height * p_param->source_width >
          NI_PARAM_AV1_MAX_AREA)
      {
          retval = NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
          LRETURN;
      }
      // AV1 8x8 alignment HW limitation is now worked around by FW cropping input resolution
      if (p_param->source_width % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
      {
          ni_log(NI_LOG_ERROR, "AV1 Picture Width not aligned to %d - picture will be cropped\n",
                 NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);
      }
      if (p_param->source_height % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
      {
          ni_log(NI_LOG_ERROR,  "AV1 Picture Height not aligned to %d - picture will be cropped\n",
                 NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);
      }
  }

  if (codec_format == NI_CODEC_FORMAT_JPEG)
  {
      if (p_param->source_width < NI_PARAM_JPEG_MIN_WIDTH)
      {
          retval = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL;
          LRETURN;
      }
      if (p_param->source_height < NI_PARAM_JPEG_MIN_HEIGHT)
      {
          retval = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL;
          LRETURN;
      }
  }

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

END:

    return retval;
}

/*!*****************************************************************************
 *  \brief  Initialize default decoder parameters
 *
 *  \param[out] param     Pointer to a user allocated ni_xcoder_params_t
 *                                    to initialize to default parameters
 *  \param[in] fps_num    Frames per second
 *  \param[in] fps_denom  FPS denomination
 *  \param[in] bit_rate   bit rate
 *  \param[in] width      frame width
 *  \param[in] height     frame height
 *
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_decoder_init_default_params(ni_xcoder_params_t *p_param,
                                            int fps_num, int fps_denom,
                                            long bit_rate, int width,
                                            int height)
{
  ni_decoder_input_params_t* p_dec = NULL;
  int i;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  //Initialize p_param structure
  if (!p_param)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s(): null pointer parameter passed\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  ni_log(NI_LOG_DEBUG, "%s\n", __func__);

  //Initialize p_param structure
  memset(p_param, 0, sizeof(ni_xcoder_params_t));

  p_dec = &p_param->dec_input_params;

  p_param->source_width = width;
  p_param->source_height = height;

  if(fps_num <= 0 || fps_denom <= 0)
  {
      fps_num = 30;
      fps_denom = 1;
      ni_log(NI_LOG_INFO, "%s(): FPS is not set, setting the default FPS to 30\n", __func__);
  }

  p_param->fps_number = fps_num;
  p_param->fps_denominator = fps_denom;

  p_dec->hwframes = 0;
  p_dec->mcmode = 0;
  p_dec->nb_save_pkt = 0;
  p_dec->enable_out1 = 0;
  p_dec->enable_out2 = 0;
  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    p_dec->force_8_bit[i] = 0;
    p_dec->semi_planar[i] = 0;
    p_dec->crop_mode[i] = NI_DEC_CROP_MODE_AUTO;
    p_dec->crop_whxy[i][0] = width;
    p_dec->crop_whxy[i][1] = height;
    p_dec->crop_whxy[i][2] = 0;
    p_dec->crop_whxy[i][3] = 0;
    p_dec->scale_wh[i][0] = 0;
    p_dec->scale_wh[i][1] = 0;
    p_dec->scale_long_short_edge[i] = 0;
    p_dec->scale_resolution_ceil[i] = 2;
    p_dec->scale_round[i] = -1;
  }
  p_dec->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;
  p_dec->decoder_low_delay = 0;
  p_dec->force_low_delay = false;
  p_dec->enable_low_delay_check = 0;
  p_dec->enable_user_data_sei_passthru = 0;
  p_dec->custom_sei_passthru = -1;
  p_dec->svct_decoding_layer = NI_INVALID_SVCT_DECODING_LAYER;
  p_dec->ec_policy = NI_EC_POLICY_DEFAULT;
  p_dec->enable_advanced_ec = 1;
  p_dec->enable_ppu_scale_adapt = 0;
  p_dec->enable_ppu_scale_limit = 0;
  p_dec->max_extra_hwframe_cnt = 255; //uint8_max
  p_dec->pkt_pts_unchange = 0;
  p_dec->enable_all_sei_passthru = 0;
  p_dec->min_packets_delay = false;
  p_dec->enable_follow_iframe = 0;
  p_param->ddr_priority_mode = NI_DDR_PRIORITY_NONE;
  p_dec->disable_adaptive_buffers = 0;

  //-------init unused param start----------

  p_param->bitrate = (int)bit_rate;
  p_param->reconf_demo_mode = 0; // NETINT_INTERNAL - currently only for internal testing
  p_param->force_pic_qp_demo_mode = 0;
  p_param->force_frame_type = 0;
  p_param->hdrEnableVUI = 0;
  p_param->cacheRoi = 0;
  p_param->low_delay_mode = 0;
  p_param->padding = 1;
  p_param->generate_enc_hdrs = 0;
  p_param->use_low_delay_poc_type = 0;
  p_param->dolby_vision_profile = 0;

  // encoder stream header VUI setting
  p_param->color_primaries = 2;                 // default COL_PRI_UNSPECIFIED
  p_param->color_transfer_characteristic = 2;   // default COL_TRC_UNSPECIFIED
  p_param->color_space = 2;                     // default COL_SPC_UNSPECIFIED
  p_param->sar_num = 0;                         // default SAR numerator 0
  p_param->sar_denom = 1;                       // default SAR denominator 1
  p_param->video_full_range_flag = -1;

  //-------init unused param done----------

END:

    return retval;
}

// read demo reconfig data file and parse out reconfig key/values in the format:
// key:val1,val2,val3,...val9 (max 9 values); only digit/:/,/newline is allowed
ni_retcode_t ni_parse_reconf_file(const char *reconf_file,
                                  int hash_map[][NI_BITRATE_RECONFIG_FILE_MAX_ENTRIES_PER_LINE])
{
  char keyChar[10] = "";
  int key;
  char valChar[10] = "";
  int val;
  int valIdx = 1;
  int parseKey = 1;
  int idx = 0;
  int readc = EOF;
  FILE *reconf = NULL;

  if (!reconf_file)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  reconf = fopen(reconf_file, "r");
  if (!reconf)
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: %s(): Cannot open reconfig_file: %s\n",
             NI_ERRNO, __func__, reconf_file);
      return NI_RETCODE_PARAM_INVALID_VALUE;
  }
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

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
      if (valIdx >= NI_BITRATE_RECONFIG_FILE_MAX_ENTRIES_PER_LINE)
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR: Number of entries per line in reconfig file is greater then the "
                 "limit of %d\n",
                 NI_BITRATE_RECONFIG_FILE_MAX_ENTRIES_PER_LINE);
          retval = NI_RETCODE_INVALID_PARAM;
          break;
      }
      val = atoi(valChar);
      hash_map[idx][valIdx] = val;
      valIdx++;
      memset(valChar, 0, 10);
    }
    else if (readc == '\n')
    {
      if (idx >= NI_BITRATE_RECONFIG_FILE_MAX_LINES)
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR: Number of lines in reconfig file is greater then the "
                 "limit of %d\n",
                 NI_BITRATE_RECONFIG_FILE_MAX_LINES);
          retval = NI_RETCODE_INVALID_PARAM;
          break;
      }
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
      ni_log(NI_LOG_ERROR, "ERROR: character %c in reconfig file. this may lead to mistaken reconfiguration values\n", readc);
    }
  }

  fclose(reconf);

  if (NI_RETCODE_SUCCESS == retval && parseKey != 1)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR %d: %s(): Incorrect format / "
             "incomplete Key/Value pair in reconfig_file: %s\n",
             NI_ERRNO, __func__, reconf_file);
      return NI_RETCODE_PARAM_ERROR_ZERO;
  }

  return retval;
}


#undef atoi
#undef atof
#define atoi(p_str) ni_atoi(p_str, &b_error)
#define atof(p_str) ni_atof(p_str, &b_error)
#define atobool(p_str) (ni_atobool(p_str, &b_error))
/*!*****************************************************************************
*  \brief  Set value referenced by name in decoder parameters structure
*
*  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t (used
*                        for decoder too for now ) to find and set a particular
*                        parameter
*  \param[in] name       String represented parameter name to search
*  \param[in] value      Parameter value to set
*
*  \return On success
*                        NI_RETCODE_SUCCESS
*          On failure
*                        NI_RETCODE_FAILURE
*                        NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_decoder_params_set_value(ni_xcoder_params_t *p_params,
                                         const char *name, char *value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_decoder_input_params_t* p_dec = NULL;
  char nameBuf[64] = { 0 };
  const char delim[2] = ",";
  const char xdelim[2] = "x";
  char *chunk;//for parsing out multi param input
  int i, j, k;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_params)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (!name)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null name pointer parameters passed\n",
           __func__);
    return NI_RETCODE_PARAM_INVALID_NAME;
  }
  p_dec = &p_params->dec_input_params;

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

  if (!value)
  {
      value = "true";
  }
  else if (value[0] == '=')
  {
      value++;
  }

#if defined(_MSC_VER)
#define OPT(STR) else if (!_stricmp(name, STR))
#define OPT2(STR1, STR2)                                                       \
    else if (!_stricmp(name, STR1) || !_stricmp(name, STR2))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#define OPT2(STR1, STR2) else if (!strcasecmp(name, STR1) || !strcasecmp(name, STR2))
#endif
  if (0); // suppress cppcheck
  OPT(NI_DEC_PARAM_OUT)
  {
    if (!strncmp(value, "hw", sizeof("hw"))){
      p_dec->hwframes = 1;
    }
    else if (!strncmp(value, "sw", sizeof("sw"))) {
      p_dec->hwframes = 0;
    }
    else{
        ni_log(NI_LOG_ERROR, "ERROR: %s(): out can only be <hw,sw> got %s\n",
               __func__, value);
        return NI_RETCODE_PARAM_INVALID_VALUE;
    }
  }
  OPT(NI_DEC_PARAM_ENABLE_OUT_1)
  {
    if (atoi(value) == 1)
      p_dec->enable_out1 = 1;
  }
  OPT(NI_DEC_PARAM_ENABLE_OUT_2)
  {
    if (atoi(value) == 1)
      p_dec->enable_out2 = 1;
  }
  OPT(NI_DEC_PARAM_FORCE_8BIT_0)
  {
    if (atoi(value) == 1)
      p_dec->force_8_bit[0] = 1;
  }
  OPT(NI_DEC_PARAM_FORCE_8BIT_1)
  {
    if (atoi(value) == 1)
      p_dec->force_8_bit[1] = 1;
  }
  OPT(NI_DEC_PARAM_FORCE_8BIT_2)
  {
    if (atoi(value) == 1)
      p_dec->force_8_bit[2] = 1;
  }
  OPT(NI_DEC_PARAM_SEMI_PLANAR_0)
  {
    if (atoi(value) == 1 || atoi(value) == 2)
      p_dec->semi_planar[0] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SEMI_PLANAR_1)
  {
    if (atoi(value) == 1 || atoi(value) == 2)
      p_dec->semi_planar[1] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SEMI_PLANAR_2)
  {
    if (atoi(value) == 1 || atoi(value) == 2)
      p_dec->semi_planar[2] = atoi(value);
  }
  OPT(NI_DEC_PARAM_CROP_MODE_0)
  {
    if (!strncmp(value, "manual", sizeof("manual"))) {
      p_dec->crop_mode[0] = NI_DEC_CROP_MODE_MANUAL;
    }
    else if (!strncmp(value, "auto", sizeof("auto"))) {
      p_dec->crop_mode[0] = NI_DEC_CROP_MODE_AUTO;
    }
    else{
        ni_log(NI_LOG_ERROR,
               "ERROR: %s():cropMode0 input can only be <manual,auto> got %s\n",
               __func__, value);
        return NI_RETCODE_PARAM_INVALID_VALUE;
    }
  }
  OPT(NI_DEC_PARAM_CROP_MODE_1)
  {
    if (!strncmp(value, "manual", sizeof("manual"))) {
      p_dec->crop_mode[1] = NI_DEC_CROP_MODE_MANUAL;
    }
    else if (!strncmp(value, "auto", sizeof("auto"))) {
      p_dec->crop_mode[1] = NI_DEC_CROP_MODE_AUTO;
    }
    else {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s():cropMode1 input can only be <manual,auto> got %s\n",
               __func__, value);
        return NI_RETCODE_PARAM_INVALID_VALUE;
    }
  }
  OPT(NI_DEC_PARAM_CROP_MODE_2)
  {
    if (!strncmp(value, "manual", sizeof("manual"))) {
      p_dec->crop_mode[2] = NI_DEC_CROP_MODE_MANUAL;
    }
    else if (!strncmp(value, "auto", sizeof("auto"))) {
      p_dec->crop_mode[2] = NI_DEC_CROP_MODE_AUTO;
    }
    else {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s():cropMode2 input can only be <manual,auto> got %s\n",
               __func__, value);
        return NI_RETCODE_PARAM_INVALID_VALUE;
    }
  }
  OPT(NI_DEC_PARAM_CROP_PARAM_0)
  {
    char *saveptr = NULL;
    chunk = ni_strtok(value, delim, &saveptr);
    for (i = 0; i < 4; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->cr_expr[0][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, delim, &saveptr);
      }
      else if (i == 2 ) //default offsets to centered image if not specified, may need recalc
      {
        strcpy(p_dec->cr_expr[0][i], "in_w/2-out_w/2");
      }
      else if (i == 3)
      {
        strcpy(p_dec->cr_expr[0][i], "in_h/2-out_h/2");
      } else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_CROP_PARAM_1)
  {
    char *saveptr = NULL;
    chunk = ni_strtok(value, delim, &saveptr);
    for (i = 0; i < 4; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->cr_expr[1][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, delim, &saveptr);
      }
      else if (i == 2) //default offsets to centered image if not specified, may need recalc
      {

        strcpy(p_dec->cr_expr[1][i], "in_w/2-out_w/2");
      }
      else if (i == 3)
      {
        strcpy(p_dec->cr_expr[1][i], "in_h/2-out_h/2");
      }
      else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_CROP_PARAM_2)
  {
    char *saveptr = NULL;
    chunk = ni_strtok(value, delim, &saveptr);
    for (i = 0; i < 4; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->cr_expr[2][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, delim, &saveptr);
      }
      else if (i == 2) //default offsets to centered image if not specified, may need recalc
      {

        strcpy(p_dec->cr_expr[2][i], "in_w/2-out_w/2");
      }
      else if (i == 3)
      {
        strcpy(p_dec->cr_expr[2][i], "in_h/2-out_h/2");
      }
      else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_SCALE_0)
  {
    chunk = value;
    i = 0; // 'x' character counter
    while (*chunk++) {
      if (*chunk == xdelim[0]) {
        i++;
      }
    }
    if (i != 1) {
      return NI_RETCODE_PARAM_INVALID_VALUE;
    }
    chunk = NULL;

    char *saveptr = NULL;
    chunk = ni_strtok(value, xdelim, &saveptr);
    for (i = 0; i < 2; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->sc_expr[0][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, xdelim, &saveptr);
      }
      else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_SCALE_1)
  {
    chunk = value;
    i = 0; // 'x' character counter
    while (*chunk++) {
      if (*chunk == xdelim[0]) {
        i++;
      }
    }
    if (i != 1) {
      return NI_RETCODE_PARAM_INVALID_VALUE;
    }
    chunk = NULL;

    char *saveptr = NULL;
    chunk = ni_strtok(value, xdelim, &saveptr);
    for (i = 0; i < 2; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->sc_expr[1][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, xdelim, &saveptr);
      }
      else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_SCALE_2)
  {
    chunk = value;
    i = 0; // 'x' character counter
    while (*chunk++) {
      if (*chunk == xdelim[0]) {
        i++;
      }
    }
    if (i != 1) {
      return NI_RETCODE_PARAM_INVALID_VALUE;
    }
    chunk = NULL;

    char *saveptr = NULL;
    chunk = ni_strtok(value, xdelim, &saveptr);
    for (i = 0; i < 2; i++)
    {
      if (chunk != NULL)
      {
        j = k = 0;
        while (chunk[j])
        {
          if (chunk[j] != '\"' && chunk[j] != '\'')
          {
            p_dec->sc_expr[2][i][k] = chunk[j];
            k++;
          }
          if (++j > NI_MAX_PPU_PARAM_EXPR_CHAR)
          {
            return NI_RETCODE_PARAM_ERROR_TOO_BIG;
          }
        }
        chunk = ni_strtok(NULL, xdelim, &saveptr);
      }
      else
      {
        return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
  }
  OPT(NI_DEC_PARAM_SCALE_0_LONG_SHORT_ADAPT)
  {
      if ((atoi(value) < 0) || (atoi(value) > 2))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_long_short_edge[0] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_1_LONG_SHORT_ADAPT)
  {
      if ((atoi(value) < 0) || (atoi(value) > 2))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_long_short_edge[1] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_2_LONG_SHORT_ADAPT)
  {
      if ((atoi(value) < 0) || (atoi(value) > 2))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_long_short_edge[2] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_0_RES_CEIL)
  {
      if (atoi(value) < 2 || atoi(value) % 2 != 0 || atoi(value) > 128)
      {
          ni_log(NI_LOG_ERROR, "ERROR: %s(): %s must be greater than or equal to 2 "
                "and must be even number and less than or equal to 128. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_0_RES_CEIL, value);

          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_resolution_ceil[0] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_1_RES_CEIL)
  {
      if (atoi(value) < 2 || atoi(value) % 2 != 0 || atoi(value) > 128)
      {
          ni_log(NI_LOG_ERROR, "ERROR: %s(): %s must be greater than or equal to 2 "
                "and must be even number and less than or equal to 128. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_1_RES_CEIL, value);
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_resolution_ceil[1] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_2_RES_CEIL)
  {
      if (atoi(value) < 2 || atoi(value) % 2 != 0 || atoi(value) > 128)
      {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): %s must be greater than or equal to 2 "
                "and must be even number and less than or equal to 128. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_2_RES_CEIL, value);
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->scale_resolution_ceil[2] = atoi(value);
  }
  OPT(NI_DEC_PARAM_SCALE_0_ROUND)
  {
      if (!strncmp(value, "up", sizeof("up"))){
        p_dec->scale_round[0] = 0;
      }
      else if (!strncmp(value, "down", sizeof("down"))) {
        p_dec->scale_round[0] = 1;
      }
      else{
          ni_log(NI_LOG_ERROR, "ERROR: %s(): %s can only be {up, down}. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_0_ROUND, value);
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }
  }
  OPT(NI_DEC_PARAM_SCALE_1_ROUND)
  {
      if (!strncmp(value, "up", sizeof("up"))){
        p_dec->scale_round[1] = 0;
      }
      else if (!strncmp(value, "down", sizeof("down"))) {
        p_dec->scale_round[1] = 1;
      }
      else{
          ni_log(NI_LOG_ERROR, "ERROR: %s(): %s can only be {up, down}. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_1_ROUND, value);
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }
  }
  OPT(NI_DEC_PARAM_SCALE_2_ROUND)
  {
      if (!strncmp(value, "up", sizeof("up"))){
        p_dec->scale_round[2] = 0;
      }
      else if (!strncmp(value, "down", sizeof("down"))) {
        p_dec->scale_round[2] = 1;
      }
      else{
          ni_log(NI_LOG_ERROR, "ERROR: %s(): %s can only be {up, down}. Got: %s\n",
                __func__, NI_DEC_PARAM_SCALE_2_ROUND, value);
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }
  }
  OPT(NI_DEC_PARAM_MULTICORE_JOINT_MODE)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->mcmode = atoi(value);
  }
  OPT(NI_DEC_PARAM_SAVE_PKT)
  {
      if (atoi(value) < 0)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->nb_save_pkt = atoi(value);
  }
  OPT(NI_KEEP_ALIVE_TIMEOUT)
  {
      if ((atoi(value) < NI_MIN_KEEP_ALIVE_TIMEOUT) ||
          (atoi(value) > NI_MAX_KEEP_ALIVE_TIMEOUT))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->keep_alive_timeout = atoi(value);
  }
  OPT(NI_DEC_PARAM_LOW_DELAY)
  {
      if (atoi(value) < 0)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->decoder_low_delay = atoi(value);
  }
  OPT(NI_DEC_PARAM_FORCE_LOW_DELAY)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->force_low_delay = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_LOW_DELAY_CHECK)
  {
      if (atoi(value) < 0)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_low_delay_check = atoi(value);
  }
  OPT(NI_DEC_PARAM_MIN_PACKETS_DELAY)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->min_packets_delay = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_USR_DATA_SEI_PASSTHRU)
  {
      if (atoi(value) != NI_ENABLE_USR_DATA_SEI_PASSTHRU &&
          atoi(value) != NI_DISABLE_USR_DATA_SEI_PASSTHRU)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_user_data_sei_passthru = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_CUSTOM_SEI_PASSTHRU)
  {
      if (atoi(value) < NI_MIN_CUSTOM_SEI_PASSTHRU ||
          atoi(value) > NI_MAX_CUSTOM_SEI_PASSTHRU)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->custom_sei_passthru = atoi(value);
  }
  OPT(NI_DEC_PARAM_SVC_T_DECODING_LAYER)
  {
      if (atoi(value) < NI_INVALID_SVCT_DECODING_LAYER)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->svct_decoding_layer = atoi(value);
  }
  OPT(NI_DEC_PARAM_DDR_PRIORITY_MODE)
  {
      if (atoi(value) >= NI_DDR_PRIORITY_MAX || 
          atoi(value) <= NI_DDR_PRIORITY_NONE)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_params->ddr_priority_mode = atoi(value);
  }
  OPT(NI_DEC_PARAM_EC_POLICY)
  {
      if (strncmp(value, "tolerant", sizeof("tolerant")) == 0) {
          p_dec->ec_policy = NI_EC_POLICY_TOLERANT;
      } else if (strncmp(value, "ignore", sizeof("ignore")) == 0) {
          p_dec->ec_policy = NI_EC_POLICY_IGNORE;
      } else if (strncmp(value, "skip", sizeof("skip")) == 0) {
          p_dec->ec_policy = NI_EC_POLICY_SKIP;
      } else if (strncmp(value, "best_effort", sizeof("best_effort")) == 0) {
          p_dec->ec_policy = NI_EC_POLICY_BEST_EFFORT;
      } else {
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }
  }
  OPT(NI_DEC_PARAM_ENABLE_ADVANCED_EC)
  {
      if (atoi(value) != 0 &&
          atoi(value) != 1 &&
          atoi(value) != 2)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_advanced_ec = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_PPU_SCALE_ADAPT)
  {
      if (atoi(value) < 0 || (atoi(value) > 2))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_ppu_scale_adapt = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_PPU_SCALE_LIMIT)
  {
      if (atoi(value) < 0 || (atoi(value) > 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_ppu_scale_limit = atoi(value);
  }
  OPT(NI_DEC_PARAM_MAX_EXTRA_HW_FRAME_CNT)
  {
      if (atoi(value) < 0 || atoi(value) > 255)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->max_extra_hwframe_cnt = atoi(value);
  }
  OPT(NI_DEC_PARAM_SKIP_PTS_GUESS)
  {
      if (atoi(value) < 0 || atoi(value) > 1)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->skip_pts_guess = atoi(value);
  }
  OPT(NI_DEC_PARAM_PKT_PTS_UNCHANGE)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->pkt_pts_unchange = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_ALL_SEI_PASSTHRU)
  {
      if (atoi(value) < 0 ||
          atoi(value) > 1)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->enable_all_sei_passthru = atoi(value);
  }
  OPT(NI_DEC_PARAM_ENABLE_FOLLOW_IFRAME)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_dec->enable_follow_iframe = atoi(value);
  }
  OPT(NI_DEC_PARAM_DISABLE_ADAPTIVE_BUFFERS)
  {
      if (atoi(value) < 0 ||
          atoi(value) > 1)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_dec->disable_adaptive_buffers = atoi(value);
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

  ni_log(NI_LOG_TRACE, "%s: exit, b_error=%d\n", __func__, b_error);

  return b_error ? NI_RETCODE_PARAM_INVALID_VALUE : NI_RETCODE_SUCCESS;
}

#undef atoi
#undef atof
#define atoi(p_str) ni_atoi(p_str, &b_error)
#define atof(p_str) ni_atof(p_str, &b_error)
#define atobool(p_str) (ni_atobool(p_str, &b_error))

/*!*****************************************************************************
 *  \brief  Set value referenced by name in encoder parameters structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t
 *                        to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_encoder_params_set_value(ni_xcoder_params_t *p_params,
                                         const char *name, const char *value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_encoder_cfg_params_t *p_enc = NULL;
  char nameBuf[64] = { 0 };
  int i,j,k;

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_params)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if ( !name )
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null name pointer parameters passed\n",
           __func__);
    return NI_RETCODE_PARAM_INVALID_NAME;
  }
  p_enc = &p_params->cfg_enc_params;
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

  if (!value)
  {
      value = "true";
  }
  else if (value[0] == '=')
  {
      value++;
  }

#if defined(_MSC_VER)
#define OPT(STR) else if (!_stricmp(name, STR))
#define OPT2(STR1, STR2)                                                       \
    else if (!_stricmp(name, STR1) || !_stricmp(name, STR2))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#define OPT2(STR1, STR2) else if (!strcasecmp(name, STR1) || !strcasecmp(name, STR2))
#endif
#define COMPARE(STR1, STR2, STR3)                                              \
    if ((atoi(STR1) > (STR2)) || (atoi(STR1) < (STR3)))                        \
    {                                                                          \
        return NI_RETCODE_PARAM_ERROR_OOR;                                     \
    }
  if (0); // suppress cppcheck
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
    if ((p_params->roi_demo_mode < 0) || (p_params->roi_demo_mode > 2))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
  }
  OPT( NI_ENC_PARAM_LOW_DELAY )
  {
      if (0 > atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
    p_params->low_delay_mode = atoi(value);
  }
  OPT (NI_ENC_PARAM_MIN_FRAMES_DELAY)
  {
       if (0 != atoi(value) && 1 != atoi(value))
       {
          return NI_RETCODE_PARAM_ERROR_OOR;
       }
       p_params->minFramesDelay = atoi(value);
  }
  OPT( NI_ENC_PARAM_PADDING )
  {
    p_params->padding = atoi(value);
  }
  OPT( NI_ENC_PARAM_GEN_HDRS )
  {
      if (0 != atoi(value) && 1 != atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
    p_params->generate_enc_hdrs = atoi(value);
  }
  OPT(NI_ENC_PARAM_USE_LOW_DELAY_POC_TYPE)
  {
      if (0 != atoi(value) && 1 != atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
    p_params->use_low_delay_poc_type = atoi(value);
  }
  OPT( NI_ENC_PARAM_FORCE_FRAME_TYPE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    p_params->force_frame_type = atoi(value);
  }
  OPT( NI_ENC_PARAM_PROFILE )
  {
    p_enc->profile = atoi(value);
  }
  OPT(NI_ENC_PARAM_LEVEL)
  {
    /*! allow "5.1" or "51", both converted to integer 51 */
    /*! if level-idc specifies an obviously wrong value in either float or int,
    throw error consistently. Stronger level checking will be done in encoder_open() */
    if (atof(value) <= 10)
    {
      p_enc->level_idc = (int)(10 * atof(value) + .5);
    }
    else
    {
        p_enc->level_idc = atoi(value);
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
      p_params->log = ni_parse_name(value, g_xcoder_log_names, &b_error) - 1;
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
  OPT( NI_ENC_PARAM_CU_SIZE_MODE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if (((atoi(value) > NI_MAX_CU_SIZE_MODE) || (atoi(value) < NI_MIN_CU_SIZE_MODE)) && (atoi(value) != NI_DEFAULT_CU_SIZE_MODE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->cu_size_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_MAX_NUM_MERGE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if ((atoi(value) > NI_MAX_MAX_NUM_MERGE) || (atoi(value) < NI_MIN_MAX_NUM_MERGE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->max_num_merge = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_DYNAMIC_8X8_MERGE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if ((atoi(value) > NI_MAX_DYNAMIC_MERGE) || (atoi(value) < NI_MIN_DYNAMIC_MERGE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->enable_dynamic_8x8_merge = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_DYNAMIC_16X16_MERGE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if ((atoi(value) > NI_MAX_DYNAMIC_MERGE) || (atoi(value) < NI_MIN_DYNAMIC_MERGE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->enable_dynamic_16x16_merge = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_DYNAMIC_32X32_MERGE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if ((atoi(value) > NI_MAX_DYNAMIC_MERGE) || (atoi(value) < NI_MIN_DYNAMIC_MERGE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->enable_dynamic_32x32_merge = atoi(value);
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
    if ((atoi(value) > NI_MAX_BIN) || (atoi(value) < NI_MIN_BIN))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rc.enable_hvs_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_HVS_QP_SCALE )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    p_enc->rc.enable_hvs_qp_scale = atoi(value);
  }
  OPT( NI_ENC_PARAM_HVS_QP_SCALE )
  {
    p_enc->rc.hvs_qp_scale = atoi(value);
  }
  OPT( NI_ENC_PARAM_MIN_QP )
  {
    p_enc->rc.min_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_MAX_QP )
  {
    p_enc->rc.max_qp = atoi(value);
  }
  OPT( NI_ENC_PARAM_MAX_DELTA_QP )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    p_enc->rc.max_delta_qp = atoi(value);
  }
  OPT(NI_ENC_PARAM_CONSTANT_RATE_FACTOR)
  {
    if ((atoi(value) > 51) || (atoi(value) < -1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->crf = atoi(value);
  }
  OPT( NI_ENC_PARAM_RC_INIT_DELAY )
  {
    p_enc->rc.vbv_buffer_size = atoi(value);
    if (QUADRA)
    {
      // RcInitDelay is deprecated and replaced with vbvBufferSize. But still accept the value.
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
  }
  OPT( NI_ENC_PARAM_VBV_BUFFER_SIZE )
  {
    p_enc->rc.vbv_buffer_size = atoi(value);
  }
  OPT( NI_ENC_PARAM_VBV_MAXRAE)
  {
    p_enc->rc.vbv_max_rate = atoi(value);
  }
  OPT( NI_ENC_PARAM_CBR )
  {
    p_enc->rc.enable_filler = atoi(value);
    if (QUADRA)
    {
      // cbr is deprecated and replaced with fillerEnable. But still accept the value.
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
  }
  OPT( NI_ENC_PARAM_ENABLE_FILLER )
  {
    p_enc->rc.enable_filler = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_PIC_SKIP )
  {
    if (0 != atoi(value) && 1 != atoi(value))
    {
        return NI_RETCODE_PARAM_ERROR_OOR;
    }
    // Currenly pic skip is supported for low delay gops only - pic skip issues tracked by QDFW-1785/1958
    p_enc->rc.enable_pic_skip = atoi(value);
  }
  OPT2(NI_ENC_PARAM_MAX_FRAME_SIZE_LOW_DELAY, NI_ENC_PARAM_MAX_FRAME_SIZE_BYTES_LOW_DELAY)
  {
#ifdef _MSC_VER
      if (!_strnicmp(value, "ratio", 5))
#else
      if (!strncasecmp(value, "ratio", 5))
#endif
      {
          char value_buf[32] = {0};
          for (i = 0; i < sizeof(value_buf); i++)
          {
              if (value[i+6] == ']')
              {
                  break;
              }
              value_buf[i] = value[i+6];
          }
          if (i == sizeof(value_buf) || atoi(value_buf) < 0)
          {
              return NI_RETCODE_PARAM_ERROR_OOR;
          }

          p_enc->maxFrameSizeRatio = atoi(value_buf);
      }
      else
      {
          int size = atoi(value);
          if (size < NI_MIN_FRAME_SIZE)
          {
              return NI_RETCODE_PARAM_ERROR_OOR;
          }
          p_enc->maxFrameSize = (size > NI_MAX_FRAME_SIZE) ? NI_MAX_FRAME_SIZE : size;
      }
  }
  OPT(NI_ENC_PARAM_MAX_FRAME_SIZE_BITS_LOW_DELAY)
  {
#ifdef _MSC_VER
      if (!_strnicmp(value, "ratio", 5))
#else
      if (!strncasecmp(value, "ratio", 5))
#endif
      {
          char value_buf[32] = {0};
          for (i = 0; i < sizeof(value_buf); i++)
          {
              if (value[i+6] == ']')
              {
                  break;
              }
              value_buf[i] = value[i+6];
          }
          if (i == sizeof(value_buf) || atoi(value_buf) < 0)
          {
              return NI_RETCODE_PARAM_ERROR_OOR;
          }

          p_enc->maxFrameSizeRatio = atoi(value_buf);
      }
      else
      {
          int size = atoi(value) / 8;
          if (size < NI_MIN_FRAME_SIZE)
          {
              return NI_RETCODE_PARAM_ERROR_OOR;
          }
          p_enc->maxFrameSize = (size > NI_MAX_FRAME_SIZE) ? NI_MAX_FRAME_SIZE : size;
      }
  }
  OPT ( NI_ENC_PARAM_FORCED_HEADER_ENABLE )
  {
      if (0 != atoi(value) && 1 != atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
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
  OPT2(NI_ENC_PARAM_INTRA_PERIOD, NI_ENC_PARAM_INTRA_REFRESH_MIN_PERIOD)
  {
    p_enc->intra_period = atoi(value);
    //p_enc->bitrateWindow = p_enc->intra_period;
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
      if (atoi(value) <= 0)
      {
          return NI_RETCODE_PARAM_ERROR_ZERO;
      }
    p_params->fps_denominator = atoi(value);
    p_enc->frame_rate = (int)(p_params->fps_number / p_params->fps_denominator);
  }
  OPT( NI_ENC_PARAM_INTRA_QP )
  {
    if ((atoi(value) > NI_MAX_INTRA_QP) || (atoi(value) < NI_MIN_INTRA_QP))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }

    p_enc->rc.intra_qp = atoi(value);
  }
  OPT(NI_ENC_PARAM_INTRA_QP_DELTA)
  {
      if ((atoi(value) > NI_MAX_INTRA_QP_DELTA) ||
          (atoi(value) < NI_MIN_INTRA_QP_DELTA))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }

      p_enc->rc.intra_qp_delta = atoi(value);
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
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    if ((atoi(value) > NI_MAX_DECODING_REFRESH_TYPE) || (atoi(value) < NI_MIN_DECODING_REFRESH_TYPE))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->decoding_refresh_type = atoi(value);
  }
  OPT(NI_ENC_PARAM_INTRA_REFRESH_RESET)
  {
      if (0 != atoi(value) && 1 != atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->intra_reset_refresh = atoi(value);
  }
// Rev. B: H.264 only parameters.
  OPT( NI_ENC_PARAM_ENABLE_8X8_TRANSFORM )
  {
    if (QUADRA)
    {
      return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    p_enc->enable_transform_8x8 = atoi(value);
  }
  OPT( NI_ENC_PARAM_SLICE_MODE )
  {
    if ((atoi(value) > NI_MAX_BIN) || (atoi(value) < NI_MIN_BIN))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->slice_mode = atoi(value);
  }
  OPT( NI_ENC_PARAM_SLICE_ARG )
  {
    p_enc->slice_arg = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENTROPY_CODING_MODE)
  {
      if (0 != atoi(value) && 1 != atoi(value))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
    p_enc->entropy_coding_mode = atoi(value);
  }
// Rev. B: shared between HEVC and H.264
  OPT2(NI_ENC_PARAM_INTRA_MB_REFRESH_MODE, NI_ENC_PARAM_INTRA_REFRESH_MODE)
  {
    p_enc->intra_mb_refresh_mode = atoi(value);
  }
  OPT2(NI_ENC_PARAM_INTRA_MB_REFRESH_ARG, NI_ENC_PARAM_INTRA_REFRESH_ARG)
  {
    p_enc->intra_mb_refresh_arg = atoi(value);
  }
  OPT( NI_ENC_PARAM_ENABLE_MB_LEVEL_RC )
  {
    if ((atoi(value) > NI_MAX_BIN) || (atoi(value) < NI_MIN_BIN))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rc.enable_mb_level_rc = atoi(value);
    if (QUADRA)
    {
        // mbLevelRcEnable will be deprecated and cuLevelRCEnable should be used instead. But still accept the value.
        return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
  }
  OPT( NI_ENC_PARAM_PREFERRED_TRANSFER_CHARACTERISTICS)
  {
    if ((atoi(value) > 255) || (atoi(value) < 0))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->preferred_transfer_characteristics = atoi(value);
  }
  OPT(NI_ENC_PARAM_DOLBY_VISION_PROFILE)
  {
    if (atoi(value) != 0 && atoi(value) != 5)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->dolby_vision_profile = atoi(value);
  }
  OPT(NI_ENC_PARAM_RDO_LEVEL)
  {
    if ((atoi(value) > 3) || (atoi(value) < 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->rdoLevel = atoi(value);
  }
  OPT(NI_ENC_PARAM_MAX_CLL)
  {
    const char delim[2] = ",";
    char *chunk;
#ifdef _MSC_VER
    char *v = _strdup(value);
#else
    char *v = strdup(value);
#endif
    char *saveptr = NULL;
    chunk = ni_strtok(v, delim, &saveptr);
    if (chunk != NULL)
    {
      if ((atoi(chunk) > 65535) || (atoi(chunk) < 0))
      {
          free(v);
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->HDR10MaxLight = atoi(chunk);
      chunk = ni_strtok(NULL, delim, &saveptr);
      if (chunk != NULL)
      {
        if ((atoi(chunk) > 65535) || (atoi(chunk) < 0))
        {
            free(v);
            return NI_RETCODE_PARAM_ERROR_OOR;
        }
        p_enc->HDR10AveLight = atoi(chunk);
        p_enc->HDR10CLLEnable = 1;   //Both param populated so enable
        free(v);
      }
      else
      {
          free(v);
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }
    }
    else
    {
        free(v);
        return NI_RETCODE_PARAM_INVALID_VALUE;
    }
  }

  OPT(NI_ENC_PARAM_MASTER_DISPLAY)
  {
#ifdef _MSC_VER
#define STRDUP(value)  _strdup(value);
#else
#define STRDUP(value)  strdup(value);
#endif  
      const char G[2] = "G";
      const char B[2] = "B";
      const char R[2] = "R";
      const char W[2] = "W";
      const char L[2] = "L";
      const char P[2] = "P";
      const char parL[2] = "(";
      const char comma[2] = ",";
      const char parR[2] = ")";
      int synCheck_GBRWLPCP[8];
      int posCheck_GBRWL[5] = {0};
      char *chunk;//for parsing out more complex inputs
      char *subchunk;
      char *v = STRDUP(value);
      //basic check syntax correct
      for (i = 0; i<8; i++)
      {
          synCheck_GBRWLPCP[i] = 0;
      }
      chunk = v;
      i = 0; // character index

      //count keys and punctuation, save indicies to be parsed
      while (*chunk) {
          if (*chunk == G[0])
          {
              synCheck_GBRWLPCP[0]++;
              posCheck_GBRWL[0] = i;
          }
          else if (*chunk == B[0])
          {
              synCheck_GBRWLPCP[1]++;
              posCheck_GBRWL[1] = i;
          }
          else if (*chunk == R[0])
          {
              synCheck_GBRWLPCP[2]++;
              posCheck_GBRWL[2] = i;
          }
          else if (*chunk == W[0])
          {
              synCheck_GBRWLPCP[3]++;
              posCheck_GBRWL[3] = i;
          }
          else if (*chunk == L[0])
          {
              synCheck_GBRWLPCP[4]++;
              posCheck_GBRWL[4] = i;
          }
          else if (*chunk == parL[0])
          {
              synCheck_GBRWLPCP[5]++;
          }
          else if (*chunk == comma[0])
          {
              synCheck_GBRWLPCP[6]++;
          }
          else if (*chunk == parR[0])
          {
              synCheck_GBRWLPCP[7]++;
          }
          chunk++;
          i++;
      }
      free(v);
      if (synCheck_GBRWLPCP[0] != 1 || synCheck_GBRWLPCP[1] != 1 || synCheck_GBRWLPCP[2] != 1 ||
          synCheck_GBRWLPCP[3] != 1 || synCheck_GBRWLPCP[4] != 1 || synCheck_GBRWLPCP[5] != 5 ||
          synCheck_GBRWLPCP[6] != 5 || synCheck_GBRWLPCP[7] != 5)
      {
          return NI_RETCODE_PARAM_INVALID_VALUE;
      }

      //Parse a key-value set like G(%hu, %hu)
#define GBRWLPARSE(OUT1,OUT2,OFF,IDX)                                       \
{                                                                           \
    char *v = STRDUP(value);                                                \
    chunk = v + posCheck_GBRWL[IDX];                                        \
    i = j = k = 0;                                                          \
    while (chunk != NULL)                                                   \
    {                                                                       \
        if (*chunk == parL[0] && i == 1+(OFF))                              \
        {                                                                   \
            j = 1;                                                          \
        }                                                                   \
        if((OFF) == 1 && *chunk != P[0] &&  i == 1)                         \
        {                                                                   \
            break;                                                          \
        }                                                                   \
        if (*chunk == parR[0])                                              \
        {                                                                   \
            k = 1;                                                          \
            break;                                                          \
        }                                                                   \
        i++;                                                                \
        chunk++;                                                            \
    }                                                                       \
    if (!j || !k)                                                           \
    {                                                                       \
        free(v);                                                            \
        return NI_RETCODE_PARAM_INVALID_VALUE;                              \
    }                                                                       \
    subchunk = malloc(i - 1 - (OFF));                                       \
    if (subchunk == NULL)                                                   \
    {                                                                       \
        free(v);                                                            \
        return NI_RETCODE_ERROR_MEM_ALOC;                                   \
    }                                                                       \
    memcpy(subchunk, v + posCheck_GBRWL[IDX] + 2 + (OFF), i - 2 - (OFF));   \
    subchunk[i - 2 - (OFF)] = '\0';                                         \
    char *saveptr = NULL;                                                   \
    chunk = ni_strtok(subchunk, comma, &saveptr);                           \
    if (chunk != NULL)                                                      \
    {                                                                       \
        if(atoi(chunk) < 0)                                                 \
        {                                                                   \
          free(v);                                                          \
          if(subchunk != NULL){                                             \
              free(subchunk);                                               \
          }                                                                 \
          return NI_RETCODE_PARAM_INVALID_VALUE;                            \
        }                                                                   \
        *(OUT1) = atoi(chunk);                                              \
    }                                                                       \
    chunk = ni_strtok(NULL, comma, &saveptr);                                            \
    if (chunk != NULL)                                                      \
    {                                                                       \
        if(atoi(chunk) < 0)                                                 \
        {                                                                   \
          free(v);                                                          \
          if(subchunk != NULL){                                             \
              free(subchunk);                                               \
          }                                                                 \
          return NI_RETCODE_PARAM_INVALID_VALUE;                            \
        }                                                                   \
        *(OUT2) = atoi(chunk);                                              \
    }                                                                       \
    free(subchunk);                                                         \
    free(v);                                                                \
}
      GBRWLPARSE(&p_enc->HDR10dx0, &p_enc->HDR10dy0, 0, 0);
      GBRWLPARSE(&p_enc->HDR10dx1, &p_enc->HDR10dy1, 0, 1);
      GBRWLPARSE(&p_enc->HDR10dx2, &p_enc->HDR10dy2, 0, 2);
      GBRWLPARSE(&p_enc->HDR10wx, &p_enc->HDR10wy, 1, 3);
      GBRWLPARSE(&p_enc->HDR10maxluma, &p_enc->HDR10minluma, 0, 4);
      p_enc->HDR10Enable = 1;
  }
  OPT(NI_ENC_PARAM_LOOK_AHEAD_DEPTH)
  {
    if (atoi(value)!= 0 && ((atoi(value) > 40 ) || (atoi(value) < 4)))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->lookAheadDepth = atoi(value);
  }
  OPT(NI_ENC_PARAM_HRD_ENABLE)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->hrdEnable = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENABLE_AUD)
  {
    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->EnableAUD = atoi(value);
  }
  OPT( NI_ENC_PARAM_CACHE_ROI )
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_params->cacheRoi = atoi(value);
  }
  OPT(NI_ENC_PARAM_LONG_TERM_REFERENCE_ENABLE)
  {
      if (atoi(value) != 0 && atoi(value) != 1)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->long_term_ref_enable = atoi(value);
  }
  OPT(NI_ENC_PARAM_LONG_TERM_REFERENCE_INTERVAL)
  {
      p_enc->long_term_ref_interval = atoi(value);
  }
  OPT(NI_ENC_PARAM_LONG_TERM_REFERENCE_COUNT)
  {
      if (atoi(value) < 1 || atoi(value) > 2)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->long_term_ref_count = atoi(value);
  }
  OPT(NI_ENC_PARAM_RDO_QUANT)
  {
    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->EnableRdoQuant = atoi(value);
  }
  OPT(NI_ENC_PARAM_CTB_RC_MODE)
  {
      if (QUADRA)
      {
          return NI_RETCODE_PARAM_WARNING_DEPRECATED;
      }

      if ((atoi(value) < 0) || (atoi(value) > 3))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
    p_enc->ctbRcMode = atoi(value);
  }
  OPT(NI_ENC_PARAM_GOP_SIZE)
  {
      if (QUADRA)
      {
          return NI_RETCODE_PARAM_WARNING_DEPRECATED;
      }

    p_enc->gopSize = atoi(value);
  }
  OPT(NI_ENC_PARAM_GOP_LOW_DELAY)
  {
      if (QUADRA)
      {
          return NI_RETCODE_PARAM_WARNING_DEPRECATED;
      }

    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->gopLowdelay = atoi(value);
  }
  OPT(NI_ENC_PARAM_GDR_DURATION)
  {
    if (QUADRA)
    {
        return NI_RETCODE_PARAM_WARNING_DEPRECATED;
    }
    p_enc->gdrDuration = atoi(value);
  }
  OPT(NI_ENC_PARAM_LTR_REF_INTERVAL)
  {
    p_enc->ltrRefInterval = atoi(value);
  }
  OPT(NI_ENC_PARAM_LTR_REF_QPOFFSET)
  {
    p_enc->ltrRefQpOffset = atoi(value);
  }
  OPT(NI_ENC_PARAM_LTR_FIRST_GAP)
  {
    p_enc->ltrFirstGap = atoi(value);
  }
  OPT(NI_ENC_PARAM_LTR_NEXT_INTERVAL) { p_enc->ltrNextInterval = atoi(value); }
  OPT(NI_ENC_PARAM_MULTICORE_JOINT_MODE)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->multicoreJointMode = atoi(value);
  }
  OPT(NI_ENC_PARAM_JPEG_QLEVEL)
  {
      if ((atoi(value) < 0) || (atoi(value) > 9))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->qlevel = atoi(value);
  }
  OPT(NI_ENC_PARAM_CHROMA_QP_OFFSET)
  {
      if ((atoi(value) > 12) || (atoi(value) < -12))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->chromaQpOffset = atoi(value);
  }
  OPT(NI_ENC_PARAM_TOL_RC_INTER) { p_enc->tolCtbRcInter = (float)atof(value); }
  OPT(NI_ENC_PARAM_TOL_RC_INTRA) { p_enc->tolCtbRcIntra = (float)atof(value); }
  OPT(NI_ENC_PARAM_BITRATE_WINDOW)
  {
      if ((atoi(value) > 300) || (atoi(value) < 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->bitrateWindow = atoi(value);
  }
  OPT(NI_ENC_BLOCK_RC_SIZE)
  {
      if ((atoi(value) > 2) || (atoi(value) < 0))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->blockRCSize = atoi(value);
  }
  OPT(NI_ENC_RC_QP_DELTA_RANGE)
  {
      if ((atoi(value) > 15) || (atoi(value) < 0))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->rcQpDeltaRange = atoi(value);
  }
  OPT(NI_ENC_CTB_ROW_QP_STEP)
  {
      if ((atoi(value) > 500) || (atoi(value) < 0))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->ctbRowQpStep = atoi(value);
  }
  OPT(NI_ENC_NEW_RC_ENABLE)
  {
      if ((atoi(value) > 1) || (atoi(value) < 0))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->newRcEnable = atoi(value);
  }
  OPT(NI_ENC_INLOOP_DS_RATIO)
  {
      if ((atoi(value) > 1) || (atoi(value) < 0))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->inLoopDSRatio = atoi(value);
  }
  OPT(NI_ENC_PARAM_COLOR_PRIMARY)
  {
      COMPARE(value, 22, 0)
      p_params->color_primaries = p_enc->colorPrimaries = atoi(value);
      p_enc->colorDescPresent = 1;
  }
  OPT(NI_ENC_PARAM_COLOR_TRANSFER_CHARACTERISTIC)
  {
      COMPARE(value, 18, 0)
      p_params->color_transfer_characteristic = p_enc->colorTrc = atoi(value);
      p_enc->colorDescPresent = 1;
  }
  OPT(NI_ENC_PARAM_COLOR_SPACE)
  {
      COMPARE(value, 14, 0)
      p_params->color_space = p_enc->colorSpace = atoi(value);
      p_enc->colorDescPresent = 1;
  }
  OPT(NI_ENC_PARAM_SAR_NUM)
  {
      p_params->sar_num = p_enc->aspectRatioWidth = atoi(value);
  }
  OPT(NI_ENC_PARAM_SAR_DENOM)
  {
      p_params->sar_denom = p_enc->aspectRatioHeight = atoi(value);
  }
  OPT(NI_ENC_PARAM_VIDEO_FULL_RANGE_FLAG)
  {
      p_params->video_full_range_flag = p_enc->videoFullRange = atoi(value);
  }
  OPT(NI_KEEP_ALIVE_TIMEOUT)
  {
      if ((atoi(value) < NI_MIN_KEEP_ALIVE_TIMEOUT) ||
          (atoi(value) > NI_MAX_KEEP_ALIVE_TIMEOUT))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->keep_alive_timeout = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENABLE_VFR)
  {
      if (atoi(value) != 0 && atoi(value) != 1)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_params->enable_vfr = atoi(value);
  }
  OPT(NI_ENC_ENABLE_SSIM)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
		  return NI_RETCODE_PARAM_ERROR_OOR;
	      }
	      p_enc->enable_ssim = atoi(value);
	  }
	  OPT(NI_ENC_PARAM_AV1_ERROR_RESILIENT_MODE)
	  {
	      if ((atoi(value) != 0) && (atoi(value) != 1))
	      {
		  return NI_RETCODE_PARAM_ERROR_OOR;
	      }
	      p_enc->av1_error_resilient_mode = atoi(value);
	  }  
	  OPT(NI_ENC_PARAM_STATIC_MMAP_THRESHOLD)
	  {
	      if ((atoi(value) != 0) && (atoi(value) != 1))
	      {
		  return NI_RETCODE_PARAM_ERROR_OOR;
	      }
	      p_params->staticMmapThreshold = atoi(value);
	  }
	  OPT(NI_ENC_PARAM_TEMPORAL_LAYERS_ENABLE)
      {
          p_enc->temporal_layers_enable = atoi(value);
      }
	  OPT(NI_ENC_PARAM_ENABLE_AI_ENHANCE)
	  {
	      if ((atoi(value) != 0) && (atoi(value) != 1))
	      {
		  return NI_RETCODE_PARAM_ERROR_OOR;
	      }
	      p_params->enable_ai_enhance = atoi(value);
      }
    OPT(NI_ENC_PARAM_ENABLE_2PASS_GOP)
    {
        if ((atoi(value) != 0) && (atoi(value) != 1))
        {
          return NI_RETCODE_PARAM_ERROR_OOR;
        }
        p_params->enable2PassGop = atoi(value);
    }
    OPT( NI_ENC_PARAM_ZEROCOPY_MODE )
    {
        if ((atoi(value) != 0) && (atoi(value) != 1) && (atoi(value) != -1))
        {
          return NI_RETCODE_PARAM_ERROR_OOR;
        }
      p_params->zerocopy_mode = atoi(value);
    }
      OPT(NI_ENC_PARAM_AI_ENHANCE_LEVEL)
      {
          if ((atoi(value) == 0) || (atoi(value) > 3))
          {
          return NI_RETCODE_PARAM_ERROR_OOR;
          }
          p_params->ai_enhance_level = atoi(value);
      }
    OPT( NI_ENC_PARAM_CROP_WIDTH )
    {
      if ((atoi(value) < NI_MIN_WIDTH) ||
          (atoi(value) > NI_PARAM_MAX_WIDTH))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->crop_width = atoi(value);
    }
    OPT( NI_ENC_PARAM_CROP_HEIGHT )
    {
      if ((atoi(value) < NI_MIN_HEIGHT) ||
          (atoi(value) > NI_PARAM_MAX_HEIGHT))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->crop_height = atoi(value);
    }
    OPT( NI_ENC_PARAM_HORIZONTAL_OFFSET )
    {
      if ((atoi(value) < 0) ||
          (atoi(value) > NI_PARAM_MAX_WIDTH))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->hor_offset = atoi(value);
    }
    OPT( NI_ENC_PARAM_VERTICAL_OFFSET )
    {
      if ((atoi(value) < 0) ||
          (atoi(value) > NI_PARAM_MAX_HEIGHT))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->ver_offset = atoi(value);
    }
  OPT(NI_ENC_PARAM_CONSTANT_RATE_FACTOR_MAX)
  {
    if ((atoi(value) > 51) || (atoi(value) < -1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->crfMax = atoi(value);
  }
  OPT(NI_ENC_PARAM_QCOMP)
  {
    if ((atof(value) > 1.0) || (atof(value) < 0.0))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->qcomp = (float)atof(value); 
  }
  OPT(NI_ENC_PARAM_NO_MBTREE)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->noMbtree = atoi(value);
  }
  OPT(NI_ENC_PARAM_NO_HW_MULTIPASS_SUPPORT)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->noHWMultiPassSupport = atoi(value);
  }
  OPT(NI_ENC_PARAM_CU_TREE_FACTOR)
  {
    if ((atoi(value) > 10) || (atoi(value) < 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->cuTreeFactor = atoi(value);
  }
  OPT(NI_ENC_PARAM_IP_RATIO)
  {
    if ((atof(value) > 10.0) || (atof(value) < 0.01))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->ipRatio = (float)atof(value);
  }
  OPT(NI_ENC_PARAM_ENABLE_IP_RATIO)
  {
    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->enableipRatio = atoi(value);
  }
  OPT(NI_ENC_PARAM_PB_RATIO)
  {
    if ((atof(value) > 10.0) || (atof(value) < 0.01))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->pbRatio = (float)atof(value);
  }
  OPT(NI_ENC_PARAM_CPLX_DECAY)
  {
    if ((atof(value) > 1.0) || (atof(value) < 0.1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->cplxDecay = (float)atof(value);
  }
  OPT(NI_ENC_PARAM_PPS_INIT_QP)
  {
    if ((atoi(value) > 51) || (atoi(value) < -1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->pps_init_qp = atoi(value);
  }
  OPT(NI_ENC_PARAM_DDR_PRIORITY_MODE)
  {
      if (atoi(value) >= NI_DDR_PRIORITY_MAX || 
          atoi(value) <= NI_DDR_PRIORITY_NONE)
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_params->ddr_priority_mode = atoi(value);
  }
  OPT(NI_ENC_PARAM_BITRATE_MODE)
  {
    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
        return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->bitrateMode = atoi(value);
  }
  OPT(NI_ENC_PARAM_PASS1_QP)
  {
    if ((atoi(value) > 51) || (atoi(value) < -1))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->pass1_qp = atoi(value);
  }  
  OPT(NI_ENC_PARAM_CONSTANT_RATE_FACTOR_FLOAT)
  {
      if (((atof(value) < 0.0) && (atof(value) != -1.0)) ||
          (atof(value) > 51.00))
      {
        return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->crfFloat = (float)atof(value);
  }
  OPT(NI_ENC_PARAM_HVS_BASE_MB_COMPLEXITY)
  {
      if ((atoi(value) < 0) || (atoi(value) > 31))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->hvsBaseMbComplexity = atoi(value);
  }
  OPT(NI_ENC_PARAM_STATISTIC_OUTPUT_LEVEL)
  {
    if (atoi(value) < 0 || atoi(value) > 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->statistic_output_level = atoi(value);
  }
  OPT(NI_ENC_PARAM_SKIP_FRAME_ENABLE)
  {
    if (atoi(value) < 0 || atoi(value) > 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->skip_frame_enable = atoi(value);
  }
  OPT(NI_ENC_PARAM_MAX_CONSUTIVE_SKIP_FRAME_NUMBER)
  {
    if (atoi(value) < 0)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->max_consecutive_skip_num = atoi(value);
  }
  OPT(NI_ENC_PARAM_SKIP_FRAME_INTERVAL)
  {
    if (atoi(value) <= 0 || atoi(value) > 255)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->skip_frame_interval = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENABLE_ALL_SEI_PASSTHRU)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->enable_all_sei_passthru = atoi(value);
  }
  OPT(NI_ENC_PARAM_IFRAME_SIZE_RATIO)
  {
    if (atoi(value) <= 0)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->iframe_size_ratio = atoi(value);
  }
  OPT(NI_ENC_PARAM_CRF_MAX_IFRAME_ENABLE)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->crf_max_iframe_enable = atoi(value);
  }
  OPT( NI_ENC_PARAM_VBV_MINRATE)
  {
    p_enc->vbv_min_rate = atoi(value);
  }
  OPT(NI_ENC_PARAM_DISABLE_ADAPTIVE_BUFFERS)
  {
    if (atoi(value) != 0 && atoi(value) != 1)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->disable_adaptive_buffers = atoi(value);
  }
  OPT(NI_ENC_PARAM_DISABLE_BFRAME_RDOQ)
  {
      if ((atoi(value) != 0) && (atoi(value) != 1))
      {
          return NI_RETCODE_PARAM_ERROR_OOR;
      }
      p_enc->disableBframeRdoq = atoi(value);
  }
  OPT(NI_ENC_PARAM_FORCE_BFRAME_QPFACTOR)
  {
    if ((atof(value) > 1.0) || (atof(value) < 0.0))
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->forceBframeQpfactor = (float)atof(value);
  }
  OPT(NI_ENC_PARAM_TUNE_BFRAME_VISUAL)
  {
    if (atoi(value) < 0 || atoi(value) > 2)
    {
      return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->tune_bframe_visual = atoi(value);
  }
  OPT(NI_ENC_PARAM_ENABLE_ACQUIRE_LIMIT)
  {
    if ((atoi(value) != 0) && (atoi(value) != 1))
    {
        return NI_RETCODE_PARAM_ERROR_OOR;
    }
    p_enc->enable_acq_limit = atoi(value);
  }
  else { return NI_RETCODE_PARAM_INVALID_NAME; }

#undef OPT
#undef OPT2
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "%s: exit, b_error=%d\n", __func__, b_error);

  return b_error ? NI_RETCODE_PARAM_INVALID_VALUE : NI_RETCODE_SUCCESS;
}

#undef atoi
#undef atof
#define atoi(p_str) ni_atoi(p_str, &b_error)
#define atof(p_str) ni_atof(p_str, &b_error)
#define atobool(p_str) (ni_atobool(p_str, &b_error))

/*!*****************************************************************************
 *  \brief  Set GOP parameter value referenced by name in encoder parameters
 *          structure
 *
 *  \param[in] p_params   Pointer to a user allocated ni_xcoder_params_t
 *                        to find and set a particular parameter
 *  \param[in] name       String represented parameter name to search
 *  \param[in] value      Parameter value to set
*
 *  \return On success
 *                        NI_RETCODE_SUCCESS
 *          On failure
 *                        NI_RETCODE_FAILURE
 *                        NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_encoder_gop_params_set_value(ni_xcoder_params_t *p_params,
                                             const char *name,
                                             const char *value)
{
  bool b_error = false;
  bool bNameWasBool = false;
  bool bValueWasNull = !value;
  ni_encoder_cfg_params_t *p_enc = NULL;
  ni_custom_gop_params_t* p_gop = NULL;
  char nameBuf[64] = { 0 };

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_params)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if ( !name )
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): Null name pointer parameters passed\n",
           __func__);
    return NI_RETCODE_PARAM_INVALID_NAME;
  }
  p_enc = &p_params->cfg_enc_params;
  p_gop = &p_enc->custom_gop_params;

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

  if (!value)
  {
      value = "true";
  }
  else if (value[0] == '=')
  {
      value++;
  }

#if defined(_MSC_VER)
#define OPT(STR) else if (!_stricmp(name, STR))
#else
#define OPT(STR) else if (!strcasecmp(name, STR))
#endif
  if (0); // suppress cppcheck
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

#ifndef QUADRA
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
      ni_log(NI_LOG_ERROR, "%s(): Invalid parameter name passed\n", __func__);
      return NI_RETCODE_PARAM_INVALID_NAME;
  }
#else
  OPT(NI_ENC_GOP_PARAMS_G0_POC_OFFSET)
  {
    p_gop->pic_param[0].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_QP_OFFSET)
  {
    p_gop->pic_param[0].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G0_QP_FACTOR)
  {
    p_gop->pic_param[0].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G0_TEMPORAL_ID)
  {
    p_gop->pic_param[0].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_PIC_TYPE)
  {
    p_gop->pic_param[0].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PICS)
  {
      p_gop->pic_param[0].num_ref_pics = atoi(value);
      //ni_log(NI_LOG_DEBUG, "%s(): Frame1 num_ref_pics %d\n", __func__, p_gop->pic_param[0].num_ref_pics);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0)
  {
    p_gop->pic_param[0].rps[0].ref_pic = atoi(value);
    //ni_log(NI_LOG_DEBUG, "%s(): Frame1 %d rps[0].ref_pic %d\n", __func__, p_gop->pic_param[0].rps[0].ref_pic);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[0].rps[0].ref_pic_used = atoi(value);
    //ni_log(NI_LOG_DEBUG, "%s(): Frame1 %d rps[0].ref_pic_used %d\n", __func__, p_gop->pic_param[0].rps[0].ref_pic_used);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1)
  {
    p_gop->pic_param[0].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[0].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2)
  {
    p_gop->pic_param[0].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[0].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3)
  {
    p_gop->pic_param[0].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[0].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G1_POC_OFFSET)
  {
    p_gop->pic_param[1].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_QP_OFFSET)
  {
    p_gop->pic_param[1].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G1_QP_FACTOR)
  {
    p_gop->pic_param[1].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G1_TEMPORAL_ID)
  {
    p_gop->pic_param[1].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_PIC_TYPE)
  {
    p_gop->pic_param[1].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PICS)
  {
    p_gop->pic_param[1].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0)
  {
    p_gop->pic_param[1].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[1].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1)
  {
    p_gop->pic_param[1].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[1].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2)
  {
    p_gop->pic_param[1].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[1].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3)
  {
    p_gop->pic_param[1].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[1].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G2_POC_OFFSET)
  {
    p_gop->pic_param[2].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_QP_OFFSET)
  {
    p_gop->pic_param[2].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G2_QP_FACTOR)
  {
    p_gop->pic_param[2].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G2_TEMPORAL_ID)
  {
    p_gop->pic_param[2].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_PIC_TYPE)
  {
    p_gop->pic_param[2].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PICS)
  {
    p_gop->pic_param[2].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0)
  {
    p_gop->pic_param[2].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[2].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1)
  {
    p_gop->pic_param[2].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[2].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2)
  {
    p_gop->pic_param[2].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[2].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3)
  {
    p_gop->pic_param[2].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[2].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G3_POC_OFFSET)
  {
    p_gop->pic_param[3].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_QP_OFFSET)
  {
    p_gop->pic_param[3].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G3_QP_FACTOR)
  {
    p_gop->pic_param[3].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G3_TEMPORAL_ID)
  {
    p_gop->pic_param[3].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_PIC_TYPE)
  {
    p_gop->pic_param[3].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PICS)
  {
    p_gop->pic_param[3].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0)
  {
    p_gop->pic_param[3].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[3].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1)
  {
    p_gop->pic_param[3].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[3].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2)
  {
    p_gop->pic_param[3].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[3].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3)
  {
    p_gop->pic_param[3].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[3].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G4_POC_OFFSET)
  {
    p_gop->pic_param[4].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_QP_OFFSET)
  {
    p_gop->pic_param[4].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G4_QP_FACTOR)
  {
    p_gop->pic_param[4].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G4_TEMPORAL_ID)
  {
    p_gop->pic_param[4].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_PIC_TYPE)
  {
    p_gop->pic_param[4].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PICS)
  {
    p_gop->pic_param[4].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0)
  {
    p_gop->pic_param[4].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[4].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1)
  {
    p_gop->pic_param[4].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[4].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2)
  {
    p_gop->pic_param[4].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[4].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3)
  {
    p_gop->pic_param[4].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[4].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G5_POC_OFFSET)
  {
    p_gop->pic_param[5].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_QP_OFFSET)
  {
    p_gop->pic_param[5].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G5_QP_FACTOR)
  {
    p_gop->pic_param[5].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G5_TEMPORAL_ID)
  {
    p_gop->pic_param[5].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_PIC_TYPE)
  {
    p_gop->pic_param[5].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PICS)
  {
    p_gop->pic_param[5].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0)
  {
    p_gop->pic_param[5].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[5].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1)
  {
    p_gop->pic_param[5].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[5].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2)
  {
    p_gop->pic_param[5].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[5].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3)
  {
    p_gop->pic_param[5].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[5].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G6_POC_OFFSET)
  {
    p_gop->pic_param[6].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_QP_OFFSET)
  {
    p_gop->pic_param[6].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G6_QP_FACTOR)
  {
    p_gop->pic_param[6].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G6_TEMPORAL_ID)
  {
    p_gop->pic_param[6].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_PIC_TYPE)
  {
    p_gop->pic_param[6].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PICS)
  {
    p_gop->pic_param[6].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0)
  {
    p_gop->pic_param[6].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[6].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1)
  {
    p_gop->pic_param[6].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[6].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2)
  {
    p_gop->pic_param[6].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[6].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3)
  {
    p_gop->pic_param[6].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[6].rps[3].ref_pic_used = atoi(value);
  }

  OPT(NI_ENC_GOP_PARAMS_G7_POC_OFFSET)
  {
    p_gop->pic_param[7].poc_offset = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_QP_OFFSET)
  {
    p_gop->pic_param[7].qp_offset = atoi(value);
  }
  /*
  OPT(NI_ENC_GOP_PARAMS_G7_QP_FACTOR)
  {
    p_gop->pic_param[7].qp_factor = atof(value);
  }
  */
  OPT(NI_ENC_GOP_PARAMS_G7_TEMPORAL_ID)
  {
    p_gop->pic_param[7].temporal_id = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_PIC_TYPE)
  {
    p_gop->pic_param[7].pic_type = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PICS)
  {
    p_gop->pic_param[7].num_ref_pics = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0)
  {
    p_gop->pic_param[7].rps[0].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0_USED)
  {
    p_gop->pic_param[7].rps[0].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1)
  {
    p_gop->pic_param[7].rps[1].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1_USED)
  {
    p_gop->pic_param[7].rps[1].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2)
  {
    p_gop->pic_param[7].rps[2].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2_USED)
  {
    p_gop->pic_param[7].rps[2].ref_pic_used = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3)
  {
    p_gop->pic_param[7].rps[3].ref_pic = atoi(value);
  }
  OPT(NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3_USED)
  {
    p_gop->pic_param[7].rps[3].ref_pic_used = atoi(value);
  }
  else
  {
      ni_log(NI_LOG_ERROR, "%s(): Invalid parameter name passed\n", __func__);
      return NI_RETCODE_PARAM_INVALID_NAME;
  }
#endif

#undef OPT
#undef OPT2
#undef atobool
#undef atoi
#undef atof

  b_error |= bValueWasNull && !bNameWasBool;

  ni_log(NI_LOG_TRACE, "%s(): exit, b_error=%d\n", __func__, b_error);

  return (b_error ? NI_RETCODE_PARAM_INVALID_VALUE : NI_RETCODE_SUCCESS);
}

/*!*****************************************************************************
*  \brief  Copy existing decoding session params for hw frame usage
*
*  \param[in] src_p_ctx    Pointer to a caller allocated source session context
*  \param[in] dst_p_ctx    Pointer to a caller allocated destination session
*                          context
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
******************************************************************************/
ni_retcode_t ni_device_session_copy(ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx)
{
  return ni_decoder_session_copy_internal(src_p_ctx, dst_p_ctx);
}

/*!*****************************************************************************
*  \brief  Read data from the device
*          If device_type is NI_DEVICE_TYPE_DECODER reads data hwdesc from
*          decoder
*          If device_type is NI_DEVICE_TYPE_SCALER reads data hwdesc from
*          scaler
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains either a
*                          ni_frame_t data frame or ni_packet_t data packet to
*                          send
*  \param[in] device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_SCALER
*                          If NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_SCALER is specified,
*                          hw descriptor info will be stored in p_data ni_frame
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
******************************************************************************/
int ni_device_session_read_hwdesc(ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type)
{
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s start\n", __func__);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    if ((!p_ctx) || (!p_data))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    // Here check if keep alive thread is closed.
#ifdef _WIN32
    if (p_ctx->keep_alive_thread.handle &&
        p_ctx->keep_alive_thread_args->close_thread)
#else
    if (p_ctx->keep_alive_thread && p_ctx->keep_alive_thread_args->close_thread)
#endif
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
               "ERROR: %s() keep alive thread has been closed, "
               "hw:%d, session:%d\n",
               __func__, p_ctx->hw_id, p_ctx->session_id);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
  // In close state, let the close process execute first.
  if (p_ctx->xcoder_state & NI_XCODER_CLOSE_STATE)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s close state, return\n", __func__);
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(100);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }
  p_ctx->xcoder_state |= NI_XCODER_READ_DESC_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  switch (device_type)
  {
  case NI_DEVICE_TYPE_DECODER:
  {
    int seq_change_read_count = 0;
    p_data->data.frame.src_codec = p_ctx->codec_format;
    for (;;)
    {
      //retval = ni_decoder_session_read(p_ctx, &(p_data->data.frame));
      retval = ni_decoder_session_read_desc(p_ctx, &(p_data->data.frame));
      // check resolution change only after initial setting obtained
      // p_data->data.frame.video_width is picture width and will be 32-align
      // adjusted to frame size; p_data->data.frame.video_height is the same as
      // frame size, then compare them to saved one for resolution checking
      //
      uint32_t aligned_width;
      if(QUADRA)
      {
          aligned_width = ((((p_data->data.frame.video_width * p_ctx->bit_depth_factor) + 127) / 128) * 128);
      }
      else
      {
          aligned_width = ((p_data->data.frame.video_width + 31) / 32) * 32;
      }

      ni_log2(p_ctx, NI_LOG_DEBUG, 
             "FNum %" PRIu64
             ", DFVWxDFVH %u x %u, AlWid %u, AVW x AVH %u x %u\n",
             p_ctx->frame_num, p_data->data.frame.video_width,
             p_data->data.frame.video_height, aligned_width,
             p_ctx->active_video_width, p_ctx->active_video_height);

      if (0 == retval && seq_change_read_count)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,  "%s (decoder): seq change NO data, next time.\n",
                 __func__);
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          p_ctx->actual_video_width = 0;
          break;
      }
      else if (retval < 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "%s (decoder): failure ret %d, return ..\n",
                 __func__, retval);
          break;
      }
      // aligned_width may equal to active_video_width if bit depth and width
      // are changed at the same time. So, check video_width != actual_video_width.
      else if (p_ctx->frame_num && (p_ctx->pixel_format_changed ||
               (p_data->data.frame.video_width &&
                p_data->data.frame.video_height &&
                (aligned_width != p_ctx->active_video_width ||
                 p_data->data.frame.video_height != p_ctx->active_video_height))))
      {
          ni_log2(
              p_ctx, NI_LOG_DEBUG,
              "%s (decoder): resolution change, frame size %ux%u -> %ux%u, "
              "width %u bit %d, pix_fromat_changed %d, actual_video_width %d, continue read ...\n",
              __func__, p_ctx->active_video_width, p_ctx->active_video_height,
              aligned_width, p_data->data.frame.video_height,
              p_data->data.frame.video_width, p_ctx->bit_depth_factor,
              p_ctx->pixel_format_changed, p_ctx->actual_video_width);
          // reset active video resolution to 0 so it can be queried in the re-read
          p_ctx->active_video_width = 0;
          p_ctx->active_video_height = 0;
          p_ctx->actual_video_width = 0;
          seq_change_read_count++;
          //break;
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
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: Encoder has no hwdesc to read\n");
      return NI_RETCODE_INVALID_PARAM;
  }

  case NI_DEVICE_TYPE_SCALER:
  {
    retval = ni_scaler_session_read_hwdesc(p_ctx, &(p_data->data.frame));
    break;
  }

  case NI_DEVICE_TYPE_AI:
  {
      retval = ni_ai_session_read_hwdesc(p_ctx, &(p_data->data.frame));
      break;
  }

  default:
  {
    retval = NI_RETCODE_INVALID_PARAM;
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() Unrecognized device type: %d",
           __func__, device_type);
    break;
  }
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state &= ~NI_XCODER_READ_DESC_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
*  \brief  Reads YUV data from hw descriptor stored location on device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                          ni_session_context_t struct
*  \param[in] p_data       Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains either a
*                          ni_frame_t data frame or ni_packet_t data packet to
*                          send
*  \param[in] hwdesc       HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_device_session_hwdl(ni_session_context_t* p_ctx, ni_session_data_io_t *p_data, niFrameSurface1_t* hwdesc)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if ((!hwdesc) || (!p_data))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
      if (p_ctx->pext_mutex == &(p_ctx->mutex))
      {
          ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session\n",
                __func__);
          return NI_RETCODE_ERROR_INVALID_SESSION;
      }
  }

  ni_pthread_mutex_lock(p_ctx->pext_mutex);
  bool use_external_mutex = false;
  uint32_t orig_session_id = p_ctx->session_id;
  ni_device_handle_t orig_blk_io_handle = p_ctx->blk_io_handle;
  uint32_t orig_codec_format = p_ctx->codec_format;
  int orig_bit_depth_factor = p_ctx->bit_depth_factor;
  int orig_hw_action = p_ctx->hw_action;

  ni_pthread_mutex_t *p_ctx_mutex = &(p_ctx->mutex);
  if ((p_ctx_mutex != p_ctx->pext_mutex) ||
      ni_cmp_fw_api_ver(
          (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
          "6r8") < 0)
  {
      use_external_mutex = true;
      p_ctx->session_id = hwdesc->ui16session_ID;
      p_ctx->blk_io_handle = (ni_device_handle_t)(int64_t)hwdesc->device_handle;
      p_ctx->codec_format = NI_CODEC_FORMAT_H264; //unused
      p_ctx->bit_depth_factor = (int)hwdesc->bit_depth;
      p_ctx->hw_action = NI_CODEC_HW_DOWNLOAD;
  }

  p_ctx->xcoder_state |= NI_XCODER_HWDL_STATE;

  retval = ni_hwdownload_session_read(p_ctx, &(p_data->data.frame), hwdesc); //cut me down as needed

  p_ctx->xcoder_state &= ~NI_XCODER_HWDL_STATE;
  if (use_external_mutex)
  {
      p_ctx->session_id = orig_session_id;
      p_ctx->blk_io_handle = orig_blk_io_handle;
      p_ctx->codec_format = orig_codec_format;
      p_ctx->bit_depth_factor = orig_bit_depth_factor;
      p_ctx->hw_action = orig_hw_action;
  }
  ni_pthread_mutex_unlock(p_ctx->pext_mutex);

  return retval;
}

/*!*****************************************************************************
*  \brief  Sends raw YUV input to uploader instance and retrieves a HW descriptor
*          to represent it
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_session_context_t struct
*  \param[in] p_src_data   Pointer to a caller allocated
*                          ni_session_data_io_t struct which contains a
*                          ni_frame_t data frame to send to uploader
*  \param[out] hwdesc      HW descriptor to find frame in XCODER
*  \return On success
*                          Total number of bytes read
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_device_session_hwup(ni_session_context_t* p_ctx, ni_session_data_io_t *p_src_data, niFrameSurface1_t* hwdesc)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if ((!hwdesc) || (!p_src_data))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }
  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_HWUP_STATE;

  retval = ni_hwupload_session_write(p_ctx, &p_src_data->data.frame, hwdesc);

  p_ctx->xcoder_state &= ~NI_XCODER_HWUP_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
*  \brief  Allocate memory for the hwDescriptor buffer based on provided
*          parameters taking into account pic size and extra data.
*
*  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
*
*  \param[in] video_width   Width of the video frame
*  \param[in] video_height  Height of the video frame
*  \param[in] extra_len     Extra data size (incl. meta data)
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_MEM_ALOC
*****************************************************************************/
ni_retcode_t ni_frame_buffer_alloc_hwenc(ni_frame_t* p_frame, int video_width,
  int video_height, int extra_len)
{
  void* p_buffer = NULL;
  int height_aligned = video_height;
  int retval = NI_RETCODE_SUCCESS;

  if (!p_frame)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_log(NI_LOG_DEBUG, "%s:  extra_len=%d\n", __func__, extra_len);

  int buffer_size = (int)sizeof(niFrameSurface1_t) + extra_len;

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT;

  //Check if Need to free
  if ((p_frame->buffer_size != buffer_size) && (p_frame->buffer_size > 0))
  {
      ni_log(NI_LOG_DEBUG, "%s: free current p_frame->buffer_size=%u\n",
             __func__, p_frame->buffer_size);
      ni_frame_buffer_free(p_frame);
  }

  //Check if need to realocate
  if (p_frame->buffer_size != buffer_size)
  {
      if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
      {
          ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_frame buffer.\n",
                 NI_ERRNO, __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }

    // init once after allocation
    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;

    ni_log(NI_LOG_DEBUG, "%s: allocated new p_frame buffer\n", __func__);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
  }

  p_frame->p_data[3] = p_frame->p_buffer;
  p_frame->p_data[0] = NULL;
  p_frame->p_data[1] = NULL;
  p_frame->p_data[2] = NULL;

  p_frame->data_len[0] = 0;//luma_size;
  p_frame->data_len[1] = 0;//chroma_b_size;
  p_frame->data_len[2] = 0;//chroma_r_size;
  p_frame->data_len[3] = sizeof(niFrameSurface1_t);

  p_frame->video_width = video_width;
  p_frame->video_height = height_aligned;

  ((niFrameSurface1_t*)p_frame->p_data[3])->device_handle = NI_INVALID_DEVICE_HANDLE;

  ni_log(NI_LOG_DEBUG, "%s: success: p_frame->buffer_size=%u\n", __func__,
         p_frame->buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

/*!*****************************************************************************
*  \brief  Recycle a frame buffer on card
*
*  \param[in] surface   Struct containing device and frame location to clear out
*  \param[in] device_handle  handle to access device memory buffer is stored in
*
*  \return On success    NI_RETCODE_SUCCESS
*          On failure    NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_hwframe_buffer_recycle(niFrameSurface1_t *surface,
                                       int32_t device_handle)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (surface)
  {
      ni_log(NI_LOG_DEBUG, "%s(): Start cleaning out buffer\n", __func__);
      ni_log(NI_LOG_DEBUG,
             "%s(): ui16FrameIdx=%d sessionId=%d device_handle=0x%x\n",
             __func__, surface->ui16FrameIdx, surface->ui16session_ID,
             device_handle);
      retval = ni_clear_instance_buf(surface);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s(): Surface is empty\n", __func__);
  }

  return retval;
}

/*!*****************************************************************************
*  \brief  Recycle a frame buffer on card, only hwframe descriptor is needed
*
*  \param[in] surface   Struct containing device and frame location to clear out
*
*  \return On success    NI_RETCODE_SUCCESS
*          On failure    NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_hwframe_buffer_recycle2(niFrameSurface1_t *surface)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int32_t saved_dma_buf_fd;
  if (surface)
  {
      if(surface->ui16FrameIdx == 0)
      {
        ni_log(NI_LOG_DEBUG, "%s(): Invaild frame index\n", __func__);
        return retval;
      }
      ni_log(NI_LOG_DEBUG, "%s(): Start cleaning out buffer\n", __func__);
      ni_log(NI_LOG_DEBUG,
             "%s(): ui16FrameIdx=%d sessionId=%d device_handle=0x%x\n",
             __func__, surface->ui16FrameIdx, surface->ui16session_ID,
             surface->device_handle);
      retval = ni_clear_instance_buf(surface);
      saved_dma_buf_fd = surface->dma_buf_fd;
      memset(surface, 0, sizeof(niFrameSurface1_t));
      surface->dma_buf_fd = saved_dma_buf_fd;
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s(): Surface is empty\n", __func__);
      retval = NI_RETCODE_INVALID_PARAM;
  }

  return retval;
}

/*!*****************************************************************************
*  \brief  Sends frame pool setup info to device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_session_context_t struct
*  \param[in] pool_size    Upload session initial allocated frames count
*                          must be > 0,
*  \param[in] pool         0 use the normal pool
*                          1 use a dedicated P2P pool
*
*  \return On success      Return code
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*                          NI_RETCODE_ERROR_MEM_ALOC
*******************************************************************************/
int ni_device_session_init_framepool(ni_session_context_t *p_ctx,
                                     uint32_t pool_size, uint32_t pool)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }
  if (pool_size == 0 || pool_size > NI_MAX_UPLOAD_INSTANCE_FRAMEPOOL)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: Invalid poolsize == 0 or > 100\n");
      return NI_RETCODE_INVALID_PARAM;
  }  
  if (pool & NI_UPLOADER_FLAG_LM)
  {
    ni_log2(p_ctx, NI_LOG_DEBUG, "uploader buffer acquisition is limited!\n");
  }
  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

  retval = ni_config_instance_set_uploader_params(p_ctx, pool_size, pool);

  p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
*  \brief  Sends frame pool change info to device
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                              ni_session_context_t struct
*  \param[in] pool_size    if pool_size = 0, free allocated device memory buffers
*                          if pool_size > 0, expand device frame buffer pool of
*                                 current instance with pool_size more frame buffers
*
*  \return On success      Return code
*          On failure
*                          NI_RETCODE_FAILURE
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*                          NI_RETCODE_ERROR_MEM_ALOC
*                          NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
*******************************************************************************/
ni_retcode_t ni_device_session_update_framepool(ni_session_context_t *p_ctx,
                                                uint32_t pool_size)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }
  if (ni_cmp_fw_api_ver(
          (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
          "6r3") < 0)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, 
           "ERROR: %s function not supported in FW API version < 6r3\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }
  if (p_ctx->pool_type == NI_POOL_TYPE_NONE)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, 
          "ERROR: can't free or expand framepool of session 0x%x "
          "before init framepool\n", p_ctx->session_id);
    return NI_RETCODE_FAILURE;
  }
  if (pool_size > NI_MAX_UPLOAD_INSTANCE_FRAMEPOOL)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, 
            "ERROR: Invalid poolsize > %u\n", NI_MAX_UPLOAD_INSTANCE_FRAMEPOOL);
      return NI_RETCODE_INVALID_PARAM;
  }
  if (pool_size == 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Free frame pool of session 0x%x\n", p_ctx->session_id);
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

  retval = ni_config_instance_set_uploader_params(p_ctx, pool_size, p_ctx->pool_type);

  p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Set parameters on the device for the 2D engine
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  p_params    pointer to scaler parameters
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_scaler_set_params(ni_session_context_t *p_ctx,
                                  ni_scaler_params_t *p_params)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx || !p_params)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    retval = ni_config_instance_set_scaler_params(p_ctx, p_params);

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure scaling drawbox parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_scaler_params_t * params - pointer to the scaler ni_scaler_drawbox params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_scaler_set_drawbox_params(ni_session_context_t *p_ctx,
                                                ni_scaler_drawbox_params_t *p_params)
{
    void *p_scaler_config = NULL;
    uint32_t buffer_size = sizeof(ni_scaler_multi_drawbox_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Invalid session ID, return.\n", __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size =
            ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) *
            NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_scaler_config, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() malloc p_scaler_config buffer failed\n",
                NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_scaler_config, 0, buffer_size);

    //configure the session here
    ui32LBA = CONFIG_INSTANCE_SetScalerDrawBoxPara_W(p_ctx->session_id,
                                            NI_DEVICE_TYPE_SCALER);

    memcpy(p_scaler_config, p_params, buffer_size);

    if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
            p_scaler_config, buffer_size, ui32LBA) < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
                "ERROR: ni_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %d, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, 
                    "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                    "hw_id, %d, xcoder_inst_id: %d\n",
                    __func__,
                    (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                    p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_scaler_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure scaling watermark parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_scaler_params_t * params - pointer to the scaler ni_scaler_watermark_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_scaler_set_watermark_params(ni_session_context_t *p_ctx,
                                                ni_scaler_watermark_params_t *p_params)
{
    void *p_scaler_config = NULL;
    uint32_t buffer_size = sizeof(ni_scaler_multi_watermark_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Invalid session ID, return.\n", __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size =
            ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) *
            NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_scaler_config, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() malloc p_scaler_config buffer failed\n",
                NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_scaler_config, 0, buffer_size);

    //configure the session here
    ui32LBA = CONFIG_INSTANCE_SetScalerWatermarkPara_W(p_ctx->session_id,
                                            NI_DEVICE_TYPE_SCALER);

    memcpy(p_scaler_config, p_params, buffer_size);

    if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
            p_scaler_config, buffer_size, ui32LBA) < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
                "ERROR: ni_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %d, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, 
                    "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                    "hw_id, %d, xcoder_inst_id: %d\n",
                    __func__,
                    (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                    p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_scaler_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}


/*!*****************************************************************************
 *  \brief  Allocate a frame on the device for 2D engine or AI engine
 *          to work on based on provided parameters
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  width       width, in pixels
 *  \param[in]  height      height, in pixels
 *  \param[in]  format      pixel format
 *  \param[in]  options     options bitmap flags, bit 0 (NI_SCALER_FLAG_IO) is
 *              0=input frame or 1=output frame. Bit 1 (NI_SCALER_FLAG_PC) is
 *              0=single allocation, 1=create pool. Bit 2 (NI_SCALER_FLAG_PA) is
 *              0=straight alpha, 1=premultiplied alpha
 *  \param[in]  rectangle_width     clipping rectangle width
 *  \param[in]  rectangle_height    clipping rectangle height
 *  \param[in]  rectangle_x         horizontal position of clipping rectangle
 *  \param[in]  rectangle_y         vertical position of clipping rectangle
 *  \param[in]  rgba_color          RGBA fill colour (for padding only)
 *  \param[in]  frame_index         input hwdesc index
 *  \param[in]  device_type         only NI_DEVICE_TYPE_SCALER
 *              and NI_DEVICE_TYPE_AI (only needs p_ctx and frame_index)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_device_alloc_frame(ni_session_context_t* p_ctx,
                                   int width,
                                   int height,
                                   int format,
                                   int options,
                                   int rectangle_width,
                                   int rectangle_height,
                                   int rectangle_x,
                                   int rectangle_y,
                                   int rgba_color,
                                   int frame_index,
                                   ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }
  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

  switch (device_type)
  {
    case NI_DEVICE_TYPE_SCALER:
      retval = ni_scaler_alloc_frame(p_ctx,width, height, format, options,
                                     rectangle_width, rectangle_height,
                                     rectangle_x, rectangle_y,
                                     rgba_color, frame_index);
      break;

    case NI_DEVICE_TYPE_AI:
        retval = ni_ai_alloc_hwframe(p_ctx, width, height, options, rgba_color,
                                     frame_index);
        break;

    case NI_DEVICE_TYPE_ENCODER:
    case NI_DEVICE_TYPE_DECODER:
       /* fall through */

    default:
      ni_log2(p_ctx, NI_LOG_ERROR,  "Bad device type %d\n", device_type);
      retval = NI_RETCODE_INVALID_PARAM;
      break;
    }

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Allocate a frame on the device and return the frame index
 *
 *  \param[in]  p_ctx  pointer to session context
 *  \param[in]  p_out_surface  pointer to output frame surface
 *  \param[in]  device_type    currently only NI_DEVICE_TYPE_AI
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_device_alloc_dst_frame(ni_session_context_t *p_ctx,
                                      niFrameSurface1_t *p_out_surface,
                                      ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, %p, return\n",
             __func__, p_ctx);
      return NI_RETCODE_INVALID_PARAM;
  }
  ni_pthread_mutex_lock(&p_ctx->mutex);
  p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

  switch (device_type)
  {
    case NI_DEVICE_TYPE_AI:
        retval = ni_ai_alloc_dst_frame(p_ctx, p_out_surface);
        break;

    case NI_DEVICE_TYPE_SCALER:
    case NI_DEVICE_TYPE_ENCODER:
    case NI_DEVICE_TYPE_DECODER:
       /* fall through */

    default:
      ni_log2(p_ctx, NI_LOG_ERROR,  "Bad device type %d\n", device_type);
      retval = NI_RETCODE_INVALID_PARAM;
      break;
  }

  p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  return retval;
}

/*!*****************************************************************************
 *  \brief  Copy the data of src hwframe to dst hwframe
 *
 *  \param[in]  p_ctx  pointer to session context
 *  \param[in]  p_frameclone_desc  pointer to the frameclone descriptor
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_device_clone_hwframe(ni_session_context_t *p_ctx,
                                     ni_frameclone_desc_t *p_frameclone_desc)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (ni_cmp_fw_api_ver(
          (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
          "6rL") < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
                "Error: %s function not supported on device with FW API version < 6rL\n",
                __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;
    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }

    retval = ni_hwframe_clone(p_ctx, p_frameclone_desc);

END:
    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Configure the 2D engine to work based on provided parameters
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  p_cfg       pointer to frame configuration
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_device_config_frame(ni_session_context_t *p_ctx,
                                    ni_frame_config_t *p_cfg)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx || !p_cfg)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    switch (p_ctx->device_type)
    {
        case NI_DEVICE_TYPE_SCALER:
            retval = ni_scaler_config_frame(p_ctx, p_cfg);
            break;

        default:
            ni_log2(p_ctx, NI_LOG_ERROR,  "Bad device type %d\n", p_ctx->device_type);
            retval = NI_RETCODE_INVALID_PARAM;
            break;
    }

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Configure the 2D engine to work based on provided parameters
 *
 *  \param[in]  p_ctx       pointer to session context
 *  \param[in]  p_cfg_in    pointer to input frame configuration
 *  \param[in]  numInCfgs   number of input frame configurations
 *  \param[in]  p_cfg_out   pointer to output frame configuration
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 ******************************************************************************/
ni_retcode_t ni_device_multi_config_frame(ni_session_context_t *p_ctx,
                                          ni_frame_config_t p_cfg_in[],
                                          int numInCfgs,
                                          ni_frame_config_t *p_cfg_out)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx || !p_cfg_in)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    switch (p_ctx->device_type)
    {
        case NI_DEVICE_TYPE_SCALER:
            retval = ni_scaler_multi_config_frame(p_ctx, p_cfg_in, numInCfgs, p_cfg_out);
            break;

        case NI_DEVICE_TYPE_AI:
            retval = ni_ai_multi_config_frame(p_ctx, p_cfg_in, numInCfgs, p_cfg_out);
            break;

        default:
            ni_log2(p_ctx, NI_LOG_ERROR,  "Bad device type %d\n", p_ctx->device_type);
            retval = NI_RETCODE_INVALID_PARAM;
            break;
    }

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!*****************************************************************************
 *  \brief   Calculate the total size of a frame based on the upload
 *           context attributes and includes rounding up to the page size
 *
 *  \param[in] p_upl_ctx    pointer to an uploader session context
 *  \param[in] linesize     array of line stride
 *
 *  \return  size
 *           NI_RETCODE_INVALID_PARAM
 *
 ******************************************************************************/
int ni_calculate_total_frame_size(const ni_session_context_t *p_upl_ctx,
                                  const int linesize[])
{
    int pixel_format;
    int width, height;
    int alignedh;
    int luma, chroma_b, chroma_r;
    int total;

    pixel_format = p_upl_ctx->pixel_format;
    width = p_upl_ctx->active_video_width;
    height = p_upl_ctx->active_video_height;

    switch (pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            if (width < 0 || width > NI_MAX_RESOLUTION_WIDTH)
            {
                return NI_RETCODE_INVALID_PARAM;
            }

            if ((height < 0) || (height > NI_MAX_RESOLUTION_HEIGHT))
            {
                return NI_RETCODE_INVALID_PARAM;
            }
            break;

        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_BGR0:
            if ((width < 0) || (width > NI_MAX_RESOLUTION_WIDTH))
            {
                return NI_RETCODE_INVALID_PARAM;
            }

            if ((height < 0) || (height > NI_MAX_RESOLUTION_HEIGHT))
            {
                return NI_RETCODE_INVALID_PARAM;
            }
            break;

        default:
            return NI_RETCODE_INVALID_PARAM;
    }

    alignedh = NI_VPU_CEIL(height, 2);

    switch (pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
            luma = linesize[0] * alignedh;
            chroma_b = linesize[1] * alignedh / 2;
            chroma_r = linesize[2] * alignedh / 2;
            total =
                luma + chroma_b + chroma_r + NI_APP_ENC_FRAME_META_DATA_SIZE;
            break;

        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            luma = linesize[0] * alignedh;
            chroma_b = linesize[1] * alignedh / 2;
            chroma_r = 0;
            total =
                luma + chroma_b + chroma_r + NI_APP_ENC_FRAME_META_DATA_SIZE;
            break;

        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_BGR0:
            total = width * height * 4 + NI_APP_ENC_FRAME_META_DATA_SIZE;
            break;

        default:
            return NI_RETCODE_INVALID_PARAM;
            break;
    }

    total = NI_VPU_CEIL(total, NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;

    return total;
}

/*!*****************************************************************************
 *  \brief   Allocate memory for the frame buffer based on provided parameters
 *           taking into account the pixel format, width, height, stride,
 *           alignment, and extra data
 *  \param[in]  p_frame         Pointer to caller allocated ni_frame_t
 *  \param[in]  pixel_format    a pixel format in ni_pix_fmt_t enum
 *  \param[in]  video_width     width, in pixels
 *  \param[in]  video_height    height, in pixels
 *  \param[in]  linesize        horizontal stride
 *  \param[in]  alignment       apply a 16 pixel height alignment (T408 only)
 *  \param[in]  extra_len       meta data size
 *
 *  \return     NI_RETCODE_SUCCESS
 *              NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_MEM_ALOC
 *
 ******************************************************************************/
ni_retcode_t ni_frame_buffer_alloc_pixfmt(ni_frame_t *p_frame, int pixel_format,
                                          int video_width, int video_height,
                                          int linesize[], int alignment,
                                          int extra_len)
{
  int buffer_size;
  void *p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;
  int height_aligned;
  int luma_size = 0;
  int chroma_b_size = 0;
  int chroma_r_size = 0;

  if (!p_frame)
  {
    ni_log(NI_LOG_ERROR, "Invalid frame pointer\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  switch (pixel_format)
  {
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
    case NI_PIX_FMT_NV16:
    case NI_PIX_FMT_YUYV422:
    case NI_PIX_FMT_UYVY422:
        if ((video_width < 0) || (video_width > NI_MAX_RESOLUTION_WIDTH))
        {
            ni_log(NI_LOG_ERROR, "Video resolution width %d out of range\n",
                   video_width);
            return NI_RETCODE_INVALID_PARAM;
        }

        if ((video_height < 0) || (video_height > NI_MAX_RESOLUTION_HEIGHT))
        {
            ni_log(NI_LOG_ERROR, "Video resolution height %d out of range\n",
                   video_width);
            return NI_RETCODE_INVALID_PARAM;
        }
        break;

    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_BGR0:
    case NI_PIX_FMT_BGRP:
        /*
         * For 2D engine using RGBA, the minimum width is 32. There is no
         * height restriction. The 2D engine supports a height/width of up to
         * 32K but but we will limit the max height and width to 8K.
        */
        if ((video_width < 0) || (video_width > NI_MAX_RESOLUTION_WIDTH))
        {
            ni_log(NI_LOG_ERROR, "Video resolution width %d out of range\n",
                   video_width);
            return NI_RETCODE_INVALID_PARAM;
        }

        if ((video_height <= 0) || (video_height > NI_MAX_RESOLUTION_HEIGHT))
        {
            ni_log(NI_LOG_ERROR, "Video resolution height %d out of range\n",
                   video_height);
            return NI_RETCODE_INVALID_PARAM;
        }
        break;

    default:
      ni_log(NI_LOG_ERROR, "Unknown pixel format %d\n",pixel_format);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (QUADRA)
  {
      /* Quadra requires an even-numbered height/width */
      height_aligned = NI_VPU_CEIL(video_height, 2);
  }
  else
  {
    height_aligned = NI_VPU_ALIGN8(video_height);

    if (alignment)
    {
      /* 16-pixel aligned pixel height for Quadra */
      height_aligned = NI_VPU_ALIGN16(video_height);
    }
  }

  switch (pixel_format)
  {
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
      luma_size = linesize[0] * height_aligned;

      if (QUADRA)
      {
        chroma_b_size = linesize[1] * height_aligned / 2;
        chroma_r_size = linesize[2] * height_aligned / 2;
      }
      else
      {
        chroma_b_size = luma_size / 4;
        chroma_r_size = luma_size / 4;
      }
      break;

    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_BGR0:
        luma_size = linesize[0] * video_height;
        chroma_b_size = 0;
        chroma_r_size = 0;
        break;

    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
      if (QUADRA)
      {
        luma_size = linesize[0] * height_aligned;
        chroma_b_size = linesize[1] * height_aligned / 2;
        chroma_r_size = 0;
        break;
      }
    case NI_PIX_FMT_NV16:
        if (QUADRA)
        {
            luma_size = linesize[0] * video_height;
            chroma_b_size = linesize[1] * video_height;
            chroma_r_size = 0;
            break;
        }
    case NI_PIX_FMT_YUYV422:
    case NI_PIX_FMT_UYVY422:
        if (QUADRA)
        {
            luma_size = linesize[0] * video_height;
            chroma_b_size = 0;
            chroma_r_size = 0;
            break;
        }
    case NI_PIX_FMT_BGRP:
        if (QUADRA)
        {
            luma_size = NI_VPU_ALIGN32(linesize[0] * video_height);
            chroma_b_size = NI_VPU_ALIGN32(linesize[1] * video_height);
            chroma_r_size = NI_VPU_ALIGN32(linesize[2] * video_height);
            break;
        }
        /*fall through*/
    default:
      ni_log(NI_LOG_ERROR, "Error: unsupported pixel format %d\n",pixel_format);
      return NI_RETCODE_INVALID_PARAM;
  }

  buffer_size = luma_size + chroma_b_size + chroma_r_size + extra_len;

  /* Allocate a buffer size that is page aligned for the host */
  buffer_size = NI_VPU_CEIL(buffer_size,NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;

  /* If this buffer has a different size, realloc a new buffer */
  if ((p_frame->buffer_size > 0) && (p_frame->buffer_size != buffer_size))
  {
      ni_log(NI_LOG_DEBUG, "Free current p_frame, p_frame->buffer_size %u\n",
                     p_frame->buffer_size);
      ni_frame_buffer_free(p_frame);
  }

  if (p_frame->buffer_size != buffer_size)
  {
      if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
      {
          ni_log(NI_LOG_ERROR, "Error: Cannot allocate p_frame\n");
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }

    memset(p_buffer, 0, buffer_size);
    p_frame->buffer_size = buffer_size;
    p_frame->p_buffer = p_buffer;
    ni_log(NI_LOG_DEBUG, "%s: allocated new p_frame buffer\n", __func__);
  }
  else
  {
      ni_log(NI_LOG_DEBUG, "%s: reuse p_frame buffer\n", __func__);
  }

  switch (pixel_format)
  {
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
      p_frame->p_data[0] = p_frame->p_buffer;
      p_frame->p_data[1] = p_frame->p_data[0] + luma_size;
      p_frame->p_data[2] = p_frame->p_data[1] + chroma_b_size;
      p_frame->p_data[3] = NULL;

      p_frame->data_len[0] = luma_size;
      p_frame->data_len[1] = chroma_b_size;
      p_frame->data_len[2] = chroma_r_size;
      p_frame->data_len[3] = 0;
      video_width = NI_VPU_ALIGN128(video_width);
      break;

    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_BGR0:
        p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
        p_frame->p_data[1] = NULL;
        p_frame->p_data[2] = NULL;
        p_frame->p_data[3] = NULL;

        p_frame->data_len[0] = luma_size;
        p_frame->data_len[1] = 0;
        p_frame->data_len[2] = 0;
        p_frame->data_len[3] = 0;
        video_width = NI_VPU_ALIGN16(video_width);
        break;

    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
        if (QUADRA)
        {
            p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
            p_frame->p_data[1] = (uint8_t *)p_frame->p_data[0] + luma_size;
            p_frame->p_data[2] = NULL;
            p_frame->p_data[3] = NULL;

            p_frame->data_len[0] = luma_size;
            p_frame->data_len[1] = chroma_b_size;
            p_frame->data_len[2] = 0;
            p_frame->data_len[3] = 0;

            video_width = NI_VPU_ALIGN128(video_width);
            break;
        }
    case NI_PIX_FMT_NV16:
        if (QUADRA)
        {
            p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
            p_frame->p_data[1] = (uint8_t *)p_frame->p_data[0] + luma_size;
            p_frame->p_data[2] = NULL;
            p_frame->p_data[3] = NULL;

            p_frame->data_len[0] = luma_size;
            p_frame->data_len[1] = chroma_b_size;
            p_frame->data_len[2] = 0;
            p_frame->data_len[3] = 0;

            video_width = NI_VPU_ALIGN64(video_width);
            break;
        }

    case NI_PIX_FMT_YUYV422:
    case NI_PIX_FMT_UYVY422:
        if (QUADRA)
        {
            p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
            p_frame->p_data[1] = NULL;
            p_frame->p_data[2] = NULL;
            p_frame->p_data[3] = NULL;

            p_frame->data_len[0] = luma_size;
            p_frame->data_len[1] = 0;
            p_frame->data_len[2] = 0;
            p_frame->data_len[3] = 0;
            video_width = NI_VPU_ALIGN16(video_width);
            break;
        }
    case NI_PIX_FMT_BGRP:
        if (QUADRA)
        {
            p_frame->p_data[0] = (uint8_t *)p_frame->p_buffer;
            p_frame->p_data[1] = (uint8_t *)p_frame->p_data[0] + luma_size;
            p_frame->p_data[2] = (uint8_t *)p_frame->p_data[1] + chroma_b_size;
            p_frame->p_data[3] = NULL;

            p_frame->data_len[0] = luma_size;
            p_frame->data_len[1] = chroma_b_size;
            p_frame->data_len[2] = chroma_r_size;
            p_frame->data_len[3] = 0;
            break;
        }
        /* fall through */
    default:
      ni_log(NI_LOG_ERROR, "Error: unsupported pixel format %d\n",pixel_format);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  p_frame->video_width  = video_width;
  p_frame->video_height = height_aligned;

  ni_log(NI_LOG_DEBUG, "%s success: w=%d; h=%d; aligned buffer size=%d\n",
         __func__, video_width, video_height, buffer_size);

END:

    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

ni_retcode_t ni_ai_config_network_binary(ni_session_context_t *p_ctx,
                                         ni_network_data_t *p_network,
                                         const char *file)
{
    FILE *fp = NULL;
    struct stat file_stat;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    unsigned char *buffer = NULL;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (stat(file, &file_stat) != 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: failed to get network binary file stat, %s\n",
               __func__, strerror(NI_ERRNO));
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    if (file_stat.st_size == 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: network binary size is null\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    fp = fopen(file, "rb");
    if (!fp)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: failed to open network binary, %s\n", __func__,
                       strerror(NI_ERRNO));
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    buffer = malloc(file_stat.st_size);
    if (!buffer)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: failed to alloate memory\n", __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    if (fread(buffer, file_stat.st_size, 1, fp) != 1)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: failed to read network binary\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    retval =
        ni_config_instance_network_binary(p_ctx, buffer, file_stat.st_size);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: failed to configure instance, retval %d\n",
               __func__, retval);
        LRETURN;
    }

    retval = ni_config_read_inout_layers(p_ctx, p_network);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: failed to read network layers, retval %d\n",
                       retval);
    }

END:

    if (fp)
    {
        fclose(fp);
    }
    free(buffer);

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

ni_retcode_t ni_ai_frame_buffer_alloc(ni_frame_t *p_frame,
                                      ni_network_data_t *p_network)
{
    uint32_t buffer_size = 0;
    void *p_buffer = NULL;
    int retval = NI_RETCODE_SUCCESS;
    uint32_t i, this_size;
    ni_network_layer_info_t *p_linfo;

    if (!p_frame || !p_network)
    {
        ni_log(NI_LOG_ERROR, "Invalid frame or network layer pointer\n");
        return NI_RETCODE_INVALID_PARAM;
    }

    p_linfo = &p_network->linfo;
    for (i = 0; i < p_network->input_num; i++)
    {
        this_size = ni_ai_network_layer_size(&p_linfo->in_param[i]);
        this_size =
            (this_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        if (p_network->inset[i].offset != buffer_size)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): invalid buffer_size of network\n", __func__);
            return NI_RETCODE_INVALID_PARAM;
        }
        buffer_size += this_size;
    }

    /* fixed size */
    p_frame->data_len[0] = buffer_size;

    /* Allocate a buffer size that is page aligned for the host */
    buffer_size = (buffer_size + NI_MEM_PAGE_ALIGNMENT - 1) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);

    /* If this buffer has a different size, realloc a new buffer */
    if ((p_frame->buffer_size > 0) && (p_frame->buffer_size != buffer_size))
    {
        ni_log(NI_LOG_DEBUG, "Free current p_frame, p_frame->buffer_size %u\n",
                       p_frame->buffer_size);
        ni_frame_buffer_free(p_frame);
    }

    if (p_frame->buffer_size != buffer_size)
    {
        if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
        {
            ni_log(NI_LOG_ERROR, "Error: Cannot allocate p_frame\n");
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }

        //    memset(p_buffer, 0, buffer_size);
        p_frame->buffer_size = buffer_size;
        p_frame->p_buffer = p_buffer;
        ni_log(NI_LOG_DEBUG, "%s(): allocated new p_frame buffer\n", __func__);
    } else
    {
        ni_log(NI_LOG_DEBUG, "%s(): reuse p_frame buffer\n", __func__);
    }

    p_frame->p_data[0] = p_frame->p_buffer;
    p_frame->p_data[1] = NULL;
    p_frame->p_data[2] = NULL;
    p_frame->p_data[3] = NULL;

    ni_log(NI_LOG_DEBUG, "%s() success: aligned buffer size=%u\n", __func__,
           buffer_size);

END:

    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_aligned_free(p_buffer);
    }

    return retval;
}

ni_retcode_t ni_ai_packet_buffer_alloc(ni_packet_t *p_packet,
                                       ni_network_data_t *p_network)
{
    void *p_buffer = NULL;
    int retval = NI_RETCODE_SUCCESS;
    uint32_t buffer_size = 0;
    uint32_t i, data_size;
    ni_network_layer_info_t *p_linfo;

    if (!p_packet || !p_network)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): null pointer parameters passed\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_linfo = &p_network->linfo;
    for (i = 0; i < p_network->output_num; i++)
    {
        data_size = ni_ai_network_layer_size(&p_linfo->out_param[i]);
        data_size =
            (data_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        if (p_network->outset[i].offset != buffer_size)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): invalid buffer_size of network\n", __func__);
            return NI_RETCODE_INVALID_PARAM;
        }
        buffer_size += data_size;
    }
    data_size = buffer_size;

    ni_log(NI_LOG_DEBUG, "%s(): packet_size=%u\n", __func__, buffer_size);

    if (buffer_size & (NI_MEM_PAGE_ALIGNMENT - 1))
    {
        buffer_size = (buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
    }

    if (p_packet->buffer_size == buffer_size)
    {
        p_packet->p_data = p_packet->p_buffer;
        ni_log(NI_LOG_DEBUG, "%s(): reuse current p_packet buffer\n", __func__);
        LRETURN;   //Already allocated the exact size
    } else if (p_packet->buffer_size > 0)
    {
        ni_log(NI_LOG_DEBUG,
               "%s(): free current p_packet,  p_packet->buffer_size=%u\n",
               __func__, p_packet->buffer_size);
        ni_packet_buffer_free(p_packet);
    }
    ni_log(NI_LOG_DEBUG, "%s(): Allocating p_packet buffer, buffer_size=%u\n",
           __func__, buffer_size);

    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_packet buffer.\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    p_packet->buffer_size = buffer_size;
    p_packet->p_buffer = p_buffer;
    p_packet->p_data = p_packet->p_buffer;
    p_packet->data_len = data_size;

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(p_buffer);
    }

    ni_log(NI_LOG_TRACE, "%s(): exit: p_packet->buffer_size=%u\n", __func__,
           p_packet->buffer_size);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Reconfigure bitrate dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] bitrate    Target bitrate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_bitrate(ni_session_context_t *p_ctx, int32_t bitrate)
{
    if (!p_ctx || bitrate < NI_MIN_BITRATE || bitrate > NI_MAX_BITRATE)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid bitrate passed in %d\n",
               __func__, bitrate);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->target_bitrate > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning: %s(): bitrate %d overwriting current one %d\n",
               __func__, bitrate, p_ctx->target_bitrate);
    }

    p_ctx->target_bitrate = bitrate;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure intraPeriod dynamically during encoding.
 *
 *  \param[in] p_ctx          Pointer to caller allocated ni_session_context_t
 *  \param[in] intra_period    Target intra period to set
 *
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *
 *  NOTE - the frame upon which intra period is reconfigured is encoded as IDR frame
 *  NOTE - reconfigure intra period is not allowed if intraRefreshMode is enabled or if gopPresetIdx is 1
 *
 ******************************************************************************/
ni_retcode_t ni_reconfig_intraprd(ni_session_context_t *p_ctx,
                                  int32_t intra_period)
{
    if (!p_ctx || intra_period < 0 || intra_period > 1024)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): invalid intraPeriod passed in %d\n",
               __func__, intra_period);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->reconfig_intra_period >= 0)
    {
        ni_log(NI_LOG_DEBUG,
               "Warning: %s(): intraPeriod %d overwriting current one %d\n",
               __func__, intra_period, p_ctx->reconfig_intra_period);
    }

    p_ctx->reconfig_intra_period = intra_period;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure VUI HRD dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] bitrate    Target bitrate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_vui(ni_session_context_t *p_ctx, ni_vui_hrd_t *vui)
{
    if (!p_ctx || vui->colorDescPresent < 0 || vui->colorDescPresent > 1)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid colorDescPresent passed in %d\n",
               __func__, vui->colorDescPresent);
        return NI_RETCODE_INVALID_PARAM;
    }

    if ((vui->aspectRatioWidth > NI_MAX_ASPECTRATIO) || (vui->aspectRatioHeight > NI_MAX_ASPECTRATIO))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid aspect ratio passed in (%dx%d)\n",
               __func__, vui->aspectRatioWidth, vui->aspectRatioHeight);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (vui->videoFullRange < 0 || vui->videoFullRange > 1)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid videoFullRange passed in %d\n",
               __func__, vui->videoFullRange);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    p_ctx->vui.colorDescPresent = vui->colorDescPresent;
    p_ctx->vui.colorPrimaries = vui->colorPrimaries;
    p_ctx->vui.colorTrc = vui->colorTrc;
    p_ctx->vui.colorSpace = vui->colorSpace;
    p_ctx->vui.aspectRatioWidth = vui->aspectRatioWidth;
    p_ctx->vui.aspectRatioHeight = vui->aspectRatioHeight;
    p_ctx->vui.videoFullRange = vui->videoFullRange;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Force next frame to be IDR frame during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *
 *  \return On success    NI_RETCODE_SUCCESS
 ******************************************************************************/
ni_retcode_t ni_force_idr_frame_type(ni_session_context_t *p_ctx)
{
    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->force_idr_frame)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning: %s(): already forcing IDR frame\n",
               __func__);
    }

    p_ctx->force_idr_frame = 1;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Set a frame's support of Long Term Reference frame during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] ltr        Pointer to struct specifying LTR support
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_set_ltr(ni_session_context_t *p_ctx, ni_long_term_ref_t *ltr)
{
    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);

    p_ctx->ltr_to_set.use_cur_src_as_long_term_pic =
        ltr->use_cur_src_as_long_term_pic;
    p_ctx->ltr_to_set.use_long_term_ref = ltr->use_long_term_ref;

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Set Long Term Reference interval
 *
 *  \param[in] p_ctx         Pointer to caller allocated ni_session_context_t
 *  \param[in] ltr_interval  the new long term reference inteval value
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_set_ltr_interval(ni_session_context_t *p_ctx,
                                 int32_t ltr_interval)
{
    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);

    p_ctx->ltr_interval = ltr_interval;

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Set frame reference invalidation
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] frame_num  frame number after which all references shall be
 *                        invalidated
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_set_frame_ref_invalid(ni_session_context_t *p_ctx,
                                      int32_t frame_num)
{
    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->ltr_frame_ref_invalid = frame_num;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure framerate dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] framerate    Target framerate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_framerate(ni_session_context_t *p_ctx,
                                   ni_framerate_t *framerate)
{
    int32_t framerate_num = framerate->framerate_num;
    int32_t framerate_denom = framerate->framerate_denom;
    if (!p_ctx || framerate_num <= 0 || framerate_denom <= 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, 
               "ERROR: %s(): invalid framerate passed in (%d/%d)\n", __func__,
               framerate_num, framerate_denom);
        return NI_RETCODE_INVALID_PARAM;
    }

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
        ni_log2(p_ctx, NI_LOG_ERROR, 
               "ERROR: %s(): invalid framerate passed in (%d/%d)\n", __func__,
               framerate->framerate_num, framerate->framerate_denom);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->framerate.framerate_num > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning: %s(): framerate (%d/%d) overwriting current "
               "one (%d/%d)\n",
               __func__, framerate_num, framerate_denom,
               p_ctx->framerate.framerate_num,
               p_ctx->framerate.framerate_denom);
    }

    p_ctx->framerate.framerate_num = framerate_num;
    p_ctx->framerate.framerate_denom = framerate_denom;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure maxFrameSize dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] max_frame_size    maxFrameSize to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *
 *  NOTE - maxFrameSize_Bytes value less than ((bitrate / 8) / framerate) will be rejected
 *
 ******************************************************************************/
ni_retcode_t ni_reconfig_max_frame_size(ni_session_context_t *p_ctx, int32_t max_frame_size)
{
    ni_xcoder_params_t *api_param;
    int32_t bitrate, framerate_num, framerate_denom;
    uint32_t maxFrameSize = (uint32_t)max_frame_size / 2000;
    uint32_t min_maxFrameSize;
  
    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
  
    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;

    if (!api_param->low_delay_mode)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): max_frame_size is valid only when lowDelay mode is enabled\n",
               __func__, max_frame_size);
        return NI_RETCODE_INVALID_PARAM;
    }    
    
    bitrate = (p_ctx->target_bitrate > 0) ?  p_ctx->target_bitrate : api_param->bitrate;      
  
    if ((p_ctx->framerate.framerate_num > 0) && (p_ctx->framerate.framerate_denom > 0))
    {
      framerate_num = p_ctx->framerate.framerate_num;
      framerate_denom = p_ctx->framerate.framerate_denom;
    }
    else
    {
      framerate_num = (int32_t) api_param->fps_number;
      framerate_denom = (int32_t) api_param->fps_denominator;
    }
    
    min_maxFrameSize = (((uint32_t)bitrate / framerate_num * framerate_denom) / 8) / 2000;
  
    if (maxFrameSize < min_maxFrameSize)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): max_frame_size %d is too small (invalid)\n",
               __func__, max_frame_size);
        return NI_RETCODE_INVALID_PARAM;
    }
    if (max_frame_size > NI_MAX_FRAME_SIZE) {
        max_frame_size = NI_MAX_FRAME_SIZE;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->max_frame_size > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning: %s(): max_frame_size %d overwriting current one %d\n",
               __func__, max_frame_size, p_ctx->max_frame_size);
    }

    p_ctx->max_frame_size = max_frame_size;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure min&max qp dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] ni_rc_min_max_qp Target min&max qp to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_min_max_qp(ni_session_context_t *p_ctx,
                                    ni_rc_min_max_qp *p_min_max_qp)
{
    int32_t minQpI, maxQpI, maxDeltaQp, minQpPB, maxQpPB;

    if (!p_ctx || !p_min_max_qp)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid ni_session_context_t or p_min_max_qp pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    minQpI      = p_min_max_qp->minQpI;
    maxQpI      = p_min_max_qp->maxQpI;
    maxDeltaQp  = p_min_max_qp->maxDeltaQp;
    minQpPB     = p_min_max_qp->minQpPB;
    maxQpPB     = p_min_max_qp->maxQpPB;

    if (minQpI > maxQpI || minQpPB > maxQpPB ||
        maxQpI > 51 || minQpI < 0 || maxQpPB > 51 || minQpPB < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid qp setting <%d %d %d %d %d>\n",
              __func__, minQpI, maxQpI, maxDeltaQp, minQpPB, maxQpPB);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    p_ctx->enc_change_params->minQpI = minQpI;
    p_ctx->enc_change_params->maxQpI = maxQpI;
    p_ctx->enc_change_params->maxDeltaQp = maxDeltaQp;
    p_ctx->enc_change_params->minQpPB = minQpPB;
    p_ctx->enc_change_params->maxQpPB = maxQpPB;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure crf value dynamically during encoding.
 *
 *  \param[in] p_ctx         Pointer to caller allocated ni_session_context_t
 *  \param[in] crf             crf value to reconfigure
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_crf(ni_session_context_t *p_ctx,
                                 int32_t crf)
{
    ni_xcoder_params_t *api_param;

    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;

    if (api_param->cfg_enc_params.crf < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): reconfigure crf value %d is valid only in CRF mode\n",
               __func__, crf);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (crf < 0 || crf > 51)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): crf value %d is invalid (valid range in [0..51])\n",
               __func__, crf);
        return NI_RETCODE_INVALID_PARAM;
    }
    
    ni_pthread_mutex_lock(&p_ctx->mutex);

    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->reconfig_crf >= 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning: %s(): crf reconfig value %d overwriting current reconfig_crf %d\n",
               __func__, crf, p_ctx->reconfig_crf);
    }

    p_ctx->reconfig_crf = crf;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure crf float point value dynamically during encoding.
 *
 *  \param[in] p_ctx         Pointer to caller allocated ni_session_context_t
 *  \param[in] crf             crf float point value to reconfigure
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_crf2(ni_session_context_t *p_ctx,
                              float crf)
{
    ni_xcoder_params_t *api_param;

    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;

    if (api_param->cfg_enc_params.crfFloat < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): reconfigure crf value %f is valid only in CRF mode\n",
               __func__, crf);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (crf < 0.0 || crf > 51.0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): crf value %f is invalid (valid range in [0..51])\n",
               __func__, crf);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->reconfig_crf >= 0 || p_ctx->reconfig_crf_decimal > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning: %s(): crf reconfig value %d overwriting current "
               "reconfig_crf %d, reconfig_crf_decimal %d\n", __func__,
               crf, p_ctx->reconfig_crf, p_ctx->reconfig_crf_decimal);
    }

    p_ctx->reconfig_crf = (int)crf;
    p_ctx->reconfig_crf_decimal = (int)((crf - (float)p_ctx->reconfig_crf) * 100);

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure vbv buffer size and vbv max rate dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] vbvBufferSize Target vbvBufferSize to set
 *  \param[in] vbvMaxRate    Target vbvMaxRate to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_vbv_value(ni_session_context_t *p_ctx,
                                   int32_t vbvMaxRate, int32_t vbvBufferSize)
{
    ni_xcoder_params_t *api_param;
    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
    if ((vbvBufferSize < 10 && vbvBufferSize != 0) || vbvBufferSize > 3000)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): vbvBufferSize value %d\n",
               __func__, vbvBufferSize);
        return NI_RETCODE_INVALID_PARAM;
    }
    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
    if (api_param->bitrate > 0 && vbvMaxRate > 0 && vbvMaxRate < api_param->bitrate) {
        ni_log2(p_ctx, NI_LOG_ERROR, "vbvMaxRate %u cannot be smaller than bitrate %d\n",
                vbvMaxRate, api_param->bitrate);
        return NI_RETCODE_INVALID_PARAM;
    }
    if (vbvBufferSize == 0 && vbvMaxRate > 0) {
        ni_log2(p_ctx, NI_LOG_INFO, "vbvMaxRate %d does not take effect when "
                "vbvBufferSize is 0, force vbvMaxRate to 0\n",
                vbvMaxRate);
        vbvMaxRate = 0;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    p_ctx->reconfig_vbv_buffer_size = vbvBufferSize;
    p_ctx->reconfig_vbv_max_rate = vbvMaxRate;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure maxFrameSizeRatio dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] max_frame_size_ratio    maxFrameSizeRatio to set
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_max_frame_size_ratio(ni_session_context_t *p_ctx, int32_t max_frame_size_ratio)
{
    ni_xcoder_params_t *api_param;
    int32_t bitrate, framerate_num, framerate_denom;
    uint32_t min_maxFrameSize, maxFrameSize;
  
    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
  
    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;

    if (!api_param->low_delay_mode)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): max_frame_size_ratio is valid only when lowDelay mode is enabled\n",
               __func__, max_frame_size_ratio);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (max_frame_size_ratio < 1) {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): max_frame_size_ratio %d cannot < 1\n",
                       max_frame_size_ratio);
        return NI_RETCODE_INVALID_PARAM;
    }
    
    bitrate = (p_ctx->target_bitrate > 0) ?  p_ctx->target_bitrate : api_param->bitrate;      
  
    if ((p_ctx->framerate.framerate_num > 0) && (p_ctx->framerate.framerate_denom > 0))
    {
      framerate_num = p_ctx->framerate.framerate_num;
      framerate_denom = p_ctx->framerate.framerate_denom;
    }
    else
    {
      framerate_num = (int32_t) api_param->fps_number;
      framerate_denom = (int32_t) api_param->fps_denominator;
    }
    
    min_maxFrameSize = (((uint32_t)bitrate / framerate_num * framerate_denom) / 8) / 2000;
  
    maxFrameSize = min_maxFrameSize * max_frame_size_ratio > NI_MAX_FRAME_SIZE ?
                    NI_MAX_FRAME_SIZE : min_maxFrameSize * max_frame_size_ratio;

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    if (p_ctx->max_frame_size > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "Warning: %s(): max_frame_size %d overwriting current one %d\n",
               __func__, maxFrameSize, p_ctx->max_frame_size);
    }

    p_ctx->max_frame_size = maxFrameSize;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Reconfigure sliceArg dynamically during encoding.
 *
 *  \param[in] p_ctx      Pointer to caller allocated ni_session_context_t
 *  \param[in] sliceArg        the new sliceArg value
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_reconfig_slice_arg(ni_session_context_t *p_ctx, int16_t sliceArg)
{
    ni_xcoder_params_t *api_param;
    ni_encoder_cfg_params_t *p_enc;
  
    if (!p_ctx || !p_ctx->p_session_config)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid ni_session_context_t or p_session_config pointer\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }
  
    api_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
    p_enc = &api_param->cfg_enc_params;
    if (p_enc->slice_mode == 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s():not support to reconfig slice_arg when slice_mode disable.\n",
                __func__);
        sliceArg = 0;
    }
    if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format || NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s():sliceArg is only supported for H.264 or H.265.\n",
                __func__);
        sliceArg = 0;
    }
    int ctu_mb_size = (NI_CODEC_FORMAT_H264 == p_ctx->codec_format) ? 16 : 64;
    int max_num_ctu_mb_row = (api_param->source_height + ctu_mb_size - 1) / ctu_mb_size;
    if (sliceArg < 1 || sliceArg > max_num_ctu_mb_row)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): invalid data sliceArg %d\n", __func__,
                sliceArg);
        sliceArg = 0;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state |= NI_XCODER_GENERAL_STATE;

    p_ctx->reconfig_slice_arg = sliceArg;

    p_ctx->xcoder_state &= ~NI_XCODER_GENERAL_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return NI_RETCODE_SUCCESS;
}

#ifndef _WIN32
/*!*****************************************************************************
*  \brief  Acquire a P2P frame buffer from the hwupload session
*
*  \param[in] p_ctx        Pointer to a caller allocated
*                                               ni_session_context_t struct
*  \param[out] p_frame     Pointer to a caller allocated hw frame
*
*  \return On success
*                          NI_RETCODE_SUCCESS
*          On failure
*                          NI_RETCODE_INVALID_PARAM
*                          NI_RETCODE_ERROR_NVME_CMD_FAILED
*                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_device_session_acquire(ni_session_context_t *p_ctx, ni_frame_t *p_frame)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    struct netint_iocmd_export_dmabuf uexp;
    unsigned int offset;
    int ret, is_semi_planar;
    int linestride[NI_MAX_NUM_DATA_POINTERS];
    int alignedheight[NI_MAX_NUM_DATA_POINTERS];
    niFrameSurface1_t hwdesc = {0};
    niFrameSurface1_t *p_surface;

    if (p_ctx == NULL || p_frame == NULL || p_frame->p_data[3] == NULL)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_frame->p_data[3];

    ni_pthread_mutex_lock(&p_ctx->mutex);
    p_ctx->xcoder_state = NI_XCODER_HWUP_STATE;

    retval = ni_hwupload_session_read_hwdesc(p_ctx, &hwdesc);

    p_ctx->xcoder_state = NI_XCODER_IDLE_STATE;
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: hwdesc read failure %d\n", retval);
        return retval;
    }

    retval = ni_get_memory_offset(p_ctx, &hwdesc, &offset);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: bad buffer id\n");
        return NI_RETCODE_INVALID_PARAM;
    }

    is_semi_planar = ((p_ctx->pixel_format == NI_PIX_FMT_NV12) ||
                      p_ctx->pixel_format == NI_PIX_FMT_P010LE) ?
        1 :
        0;

    ni_get_hw_yuv420p_dim(p_ctx->active_video_width, p_ctx->active_video_height,
                          p_ctx->bit_depth_factor, is_semi_planar, linestride,
                          alignedheight);

    uexp.fd = -1;
    uexp.flags = 0;
    uexp.offset = offset;

    uexp.length = ni_calculate_total_frame_size(p_ctx, linestride);
    uexp.domain = p_ctx->domain;
    uexp.bus = p_ctx->bus;
    uexp.dev = p_ctx->dev;
    uexp.fn = p_ctx->fn;
    uexp.bar = 4;   // PCI BAR4 configuration space

    ret = ioctl(p_ctx->netint_fd, NETINT_IOCTL_EXPORT_DMABUF, &uexp);
    if (ret < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: Failed to export dmabuf %d errno %d\n",
               __func__, ret, NI_ERRNO);
        return NI_RETCODE_FAILURE;
    }

    *p_surface = hwdesc;
    p_surface->ui16width = p_ctx->active_video_width;
    p_surface->ui16height = p_ctx->active_video_height;
    p_surface->ui32nodeAddress = offset;
    p_surface->encoding_type = is_semi_planar ?
        NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR :
        NI_PIXEL_PLANAR_FORMAT_PLANAR;
    p_surface->dma_buf_fd = uexp.fd;

    return retval;
}

/*!*****************************************************************************
 *  \brief  Lock a hardware P2P frame prior to encoding
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated upload context
 *        [in] p_frame      pointer to caller allocated hardware P2P frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure      NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_uploader_frame_buffer_lock(ni_session_context_t *p_upl_ctx,
                                           ni_frame_t *p_frame)
{
    int ret;
    struct netint_iocmd_attach_rfence uatch = {0};
    struct pollfd pfds[1] = {0};
    niFrameSurface1_t *p_surface;

    if (p_upl_ctx == NULL || p_frame == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: bad parameters\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_frame->p_data[3] == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: not a hardware frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_frame->p_data[3];

    pfds[0].fd = p_surface->dma_buf_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    ret = poll(pfds, 1, -1);
    if (ret < 0)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s:failed to poll dmabuf fd errno %s\n", __func__,
               strerror(NI_ERRNO));
        return ret;
    }

    uatch.fd = p_surface->dma_buf_fd;
    ret = ioctl(p_upl_ctx->netint_fd, NETINT_IOCTL_ATTACH_RFENCE, &uatch);
    if (ret < 0)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR, 
               "%s: failed to attach dmabuf read fence errno %s\n", __func__,
               strerror(NI_ERRNO));
        return ret;
    }

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Unlock a hardware P2P frame after encoding
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated upload context
 *        [in] p_frame      pointer to caller allocated hardware P2P frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure      NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_uploader_frame_buffer_unlock(ni_session_context_t *p_upl_ctx,
                                             ni_frame_t *p_frame)
{
    int ret;
    struct netint_iocmd_signal_rfence usigl = {0};
    niFrameSurface1_t *p_surface;

    if ((p_upl_ctx == NULL) || (p_frame == NULL))
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: Invalid parameters %p %p\n", __func__,
               p_upl_ctx, p_frame);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_frame->p_data[3];

    if (p_surface == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: Invalid hw frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    usigl.fd = p_surface->dma_buf_fd;
    ret = ioctl(p_upl_ctx->netint_fd, NETINT_IOCTL_SIGNAL_RFENCE, &usigl);
    if (ret < 0)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "Failed to signal dmabuf read fence\n");
        return NI_RETCODE_FAILURE;
    }

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Special P2P test API function. Copies YUV data from the software
 *          frame to the hardware P2P frame on the Quadra device
 *
 *  \param[in] p_upl_ctx    pointer to caller allocated uploader session
 *                          context
 *        [in] p_swframe    pointer to a caller allocated software frame
 *        [in] p_hwframe    pointer to a caller allocated hardware frame
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_FAILURE
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_uploader_p2p_test_send(ni_session_context_t *p_upl_ctx,
                                       uint8_t *p_data, uint32_t len,
                                       ni_frame_t *p_hwframe)
{
    int ret;
    struct netint_iocmd_issue_request uis = {0};
    niFrameSurface1_t *p_surface;

    if (p_upl_ctx == NULL || p_data == NULL || p_hwframe == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: invalid null parameters\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_hwframe->p_data[3] == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: empty frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_hwframe->p_data[3];

    uis.fd = p_surface->dma_buf_fd;
    uis.data = p_data;
    uis.len = len;
    uis.dir = NI_DMABUF_WRITE_TO_DEVICE;

    ret = ioctl(p_upl_ctx->netint_fd, NETINT_IOCTL_ISSUE_REQ, &uis);
    if (ret < 0)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR, 
               "%s: Failed to request dmabuf rendering errno %d\n", __func__,
               NI_ERRNO);
        return NI_RETCODE_FAILURE;
    }

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Recycle hw P2P frames
 *
 *  \param [in] p_frame     pointer to an acquired P2P hw frame
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *
 *          on failure
 *              NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_hwframe_p2p_buffer_recycle(ni_frame_t *p_frame)
{
    niFrameSurface1_t *p_surface;

    if (p_frame == NULL)
    {
        ni_log(NI_LOG_ERROR, "%s: Invalid frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_frame->p_data[3];
    if (p_surface == NULL)
    {
        ni_log(NI_LOG_ERROR, "%s: Invalid surface data\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    return ni_hwframe_buffer_recycle2(p_surface);
}

/*!*****************************************************************************
 *  \brief  Acquire the scaler P2P DMA buffer for read/write
 *
 *  \param [in] p_ctx       pointer to caller allocated upload context
 *         [in] p_surface   pointer to a caller allocated hardware frame
 *         [in] data_len    scaler frame buffer data length
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *
 *          on failure
 *              NI_RETCODE_FAILURE
*******************************************************************************/
ni_retcode_t ni_scaler_p2p_frame_acquire(ni_session_context_t *p_ctx,
                                         niFrameSurface1_t *p_surface,
                                         int data_len)
{
    unsigned int offset;
    int ret;

    ret = ni_get_memory_offset(p_ctx, p_surface, &offset);
    if (ret != 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error: bad buffer id\n");
        return NI_RETCODE_FAILURE;
    }
    p_surface->ui32nodeAddress = 0;

    struct netint_iocmd_export_dmabuf uexp;
    uexp.fd = -1;
    uexp.flags = 0;
    uexp.offset = offset;
    uexp.length = data_len;
    uexp.domain = p_ctx->domain;
    uexp.bus = p_ctx->bus;
    uexp.dev = p_ctx->dev;
    uexp.fn = p_ctx->fn;
    uexp.bar = 4;
    ret = ioctl(p_ctx->netint_fd, NETINT_IOCTL_EXPORT_DMABUF, &uexp);
    if (ret < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "failed to export dmabuf: %s\n", strerror(errno));
        return NI_RETCODE_FAILURE;
    }
    p_surface->dma_buf_fd = uexp.fd;
    return ret;
}
#endif

/*!*****************************************************************************
 *  \brief  Set the incoming frame format for the encoder
 *
 *  \param[in] p_enc_ctx    pointer to encoder context
 *        [in] p_enc_params pointer to encoder parameters
 *        [in] width        input width
 *        [in] height       input height
 *        [in] bit_depth    8 for 8-bit YUV, 10 for 10-bit YUV
 *        [in] src_endian   NI_FRAME_LITTLE_ENDIAN or NI_FRAME_BIG_ENDIAN
 *        [in] planar       0 for semi-planar YUV, 1 for planar YUV
 *
 *  \return on success
 *          NI_RETCODE_SUCCESS
 *
 *          on failure
 *          NI_RETCODE_INVALID_PARAM
 *
*******************************************************************************/
ni_retcode_t ni_encoder_set_input_frame_format(ni_session_context_t *p_enc_ctx,
                                               ni_xcoder_params_t *p_enc_params,
                                               int width, int height,
                                               int bit_depth, int src_endian,
                                               int planar)
{
    int alignedw;
    int alignedh;

    if (p_enc_ctx == NULL || p_enc_params == NULL)
    {
        ni_log2(p_enc_ctx, NI_LOG_ERROR,  "%s: null ptr\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (!(bit_depth == 8) && !(bit_depth == 10))
    {
        ni_log2(p_enc_ctx, NI_LOG_ERROR,  "%s: bad bit depth %d\n", __func__, bit_depth);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (!(src_endian == NI_FRAME_LITTLE_ENDIAN) &&
        !(src_endian == NI_FRAME_BIG_ENDIAN))
    {
        ni_log2(p_enc_ctx, NI_LOG_ERROR,  "%s: bad endian %d\n", __func__, src_endian);
        return NI_RETCODE_INVALID_PARAM;
    }

    if ((planar < 0) || (planar >= NI_PIXEL_PLANAR_MAX))
    {
        ni_log2(p_enc_ctx, NI_LOG_ERROR,  "%s: bad planar value %d\n", __func__, planar);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_enc_ctx->src_bit_depth = bit_depth;
    p_enc_ctx->bit_depth_factor = (bit_depth == 8) ? 1 : 2;
    p_enc_ctx->src_endian = src_endian;

    alignedw = width;

    if (alignedw < NI_MIN_WIDTH)
    {
        p_enc_params->cfg_enc_params.conf_win_right +=
            (NI_MIN_WIDTH - width) / 2 * 2;
        alignedw = NI_MIN_WIDTH;
    } else
    {
        alignedw = ((width + 1) / 2) * 2;
        p_enc_params->cfg_enc_params.conf_win_right +=
            (alignedw - width) / 2 * 2;
    }

    p_enc_params->source_width = alignedw;
    alignedh = height;

    if (alignedh < NI_MIN_HEIGHT)
    {
        p_enc_params->cfg_enc_params.conf_win_bottom +=
            (NI_MIN_HEIGHT - height) / 2 * 2;
        alignedh = NI_MIN_HEIGHT;
    } else
    {
        alignedh = ((height + 1) / 2) * 2;
        p_enc_params->cfg_enc_params.conf_win_bottom +=
            (alignedh - height) / 2 * 2;
    }

    p_enc_params->source_height = alignedh;
    p_enc_params->cfg_enc_params.planar = planar;

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Set the outgoing frame format for the uploader
 *
 *  \param[in]  p_upl_ctx       pointer to uploader context
 *        [in]  width           width
 *        [in]  height          height
 *        [in]  pixel_format    pixel format
 *        [in]  isP2P           0 = normal, 1 = P2P
 *
 *  \return on success
 *          NI_RETCODE_SUCCESS
 *
 *          on failure
 *          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_uploader_set_frame_format(ni_session_context_t *p_upl_ctx,
                                          int width, int height,
                                          ni_pix_fmt_t pixel_format, int isP2P)
{
    if (p_upl_ctx == NULL)
    {
        ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: null ptr\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    switch (pixel_format)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_NV12:
            p_upl_ctx->src_bit_depth = 8;
            p_upl_ctx->bit_depth_factor = 1;
            break;
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_P010LE:
            p_upl_ctx->src_bit_depth = 10;
            p_upl_ctx->bit_depth_factor = 2;
            break;
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_BGR0:
        case NI_PIX_FMT_BGRP:
            p_upl_ctx->src_bit_depth = 8;
            p_upl_ctx->bit_depth_factor = 4;
            break;
        default:
            ni_log2(p_upl_ctx, NI_LOG_ERROR,  "%s: Invalid pixfmt %d\n", __func__,
                   pixel_format);
            return NI_RETCODE_INVALID_PARAM;
    }

    p_upl_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
    p_upl_ctx->pixel_format = pixel_format;
    p_upl_ctx->active_video_width = width;
    p_upl_ctx->active_video_height = height;
    p_upl_ctx->isP2P = isP2P;

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Read encoder stream header from the device
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct from encoder
 *  \param[in] p_data       Pointer to a caller allocated ni_session_data_io_t
 *                          struct which contains a ni_packet_t data packet to
 *                          receive
 *  \return On success
 *                          Total number of bytes read
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
*******************************************************************************/
int ni_encoder_session_read_stream_header(ni_session_context_t *p_ctx,
                                          ni_session_data_io_t *p_data)
{
    int rx_size;
    int done = 0;
    int bytes_read = 0;

    /* This function should be called once at the start of encoder read */
    if (p_ctx->pkt_num != 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error: stream header has already been read\n");
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    while (!done)
    {
        rx_size = ni_device_session_read(p_ctx, p_data, NI_DEVICE_TYPE_ENCODER);

        if (rx_size > (int)p_ctx->meta_size)
        {
            /* stream header has been read, return size */
            bytes_read += (rx_size - (int)p_ctx->meta_size);

            p_ctx->pkt_num = 1;
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Got encoded stream header\n");
            done = 1;
        } else if (rx_size != 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "Error: received rx_size = %d\n", rx_size);
            bytes_read = -1;
            done = 1;
        } else
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "No data, keep reading..\n");
            continue;
        }
    }

    return bytes_read;
}

/*!*****************************************************************************
 *  \brief  Get the DMA buffer file descriptor from the P2P frame
 *
 *  \param[in]  p_frame     pointer to a P2P frame
 *
 *  \return     On success
 *                          DMA buffer file descriptor
 *              On failure
 *                          NI_RETCODE_INVALID_PARAM
*******************************************************************************/
int32_t ni_get_dma_buf_file_descriptor(const ni_frame_t* p_frame)
{
    const niFrameSurface1_t *p_surface;

    if (p_frame == NULL)
    {
        ni_log(NI_LOG_ERROR, "%s: NULL frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    p_surface = (niFrameSurface1_t *)p_frame->p_data[3];

    if (p_surface == NULL)
    {
        ni_log(NI_LOG_ERROR, "%s: Invalid hw frame\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    return p_surface->dma_buf_fd;
}

/*!*****************************************************************************
 *  \brief  Send sequence change information to device
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] width        input width
 *  \param[in] height       input height
 *  \param[in] bit_depth_factor    1 for 8-bit YUV, 2 for 10-bit YUV
 *  \param[in] device_type  device type (must be encoder)
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_INVALID_PARAM
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                          NI_RETCODE_ERROR_INVALID_SESSION
 *                          NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 ******************************************************************************/
ni_retcode_t ni_device_session_sequence_change(ni_session_context_t *p_ctx,
                                            int width, int height, int bit_depth_factor, ni_device_type_t device_type)
{
    ni_resolution_t resolution;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_xcoder_params_t *p_param = NULL;

    // requires API version >= 54
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "54") < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error: %s function not supported on device with FW API version < 5.4\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    /* This function should be called only if sequence change is detected */
    if (p_ctx->session_run_state != SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error: stream header has already been read\n");
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    resolution.width = width;
    resolution.height = height;
    resolution.bit_depth_factor = bit_depth_factor;
    resolution.luma_linesize = 0;
    resolution.chroma_linesize = 0;
    if (p_ctx->p_session_config)
    {
        p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
        resolution.luma_linesize = p_param->luma_linesize;
        resolution.chroma_linesize = p_param->chroma_linesize;
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: resolution change config - width %d height %d bit_depth_factor %d "
        "luma_linesize %d chroma_linesize %d\n", __func__,
        resolution.width, resolution.height, resolution.bit_depth_factor,
        resolution.luma_linesize, resolution.chroma_linesize);

    switch (device_type)
    {
        case NI_DEVICE_TYPE_ENCODER:
        {
            // config sequence change
            retval = ni_encoder_session_sequence_change(p_ctx, &resolution);
            break;
        }
        default:
        {
            retval = NI_RETCODE_INVALID_PARAM;
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: Config sequence change not supported for device type: %d", device_type);
            return retval;
        }
    }
    return retval;
}

ni_retcode_t ni_ai_session_read_metrics(ni_session_context_t *p_ctx,
                                        ni_network_perf_metrics_t *p_metrics)
{
    return ni_ai_session_query_metrics(p_ctx, p_metrics);
}

ni_retcode_t ni_query_fl_fw_versions(ni_device_handle_t device_handle,
                                     ni_device_info_t *p_dev_info)
{
  void *buffer;
  uint32_t size;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
  ni_log_fl_fw_versions_t fl_fw_versions;

  if ((NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_dev_info))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters are null, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (ni_cmp_fw_api_ver((char*) &p_dev_info->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "6h") < 0)
  {
    ni_log(NI_LOG_ERROR,
           "ERROR: %s function not supported on device with FW API version < 6.h\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }

  buffer = NULL;
  size = ((sizeof(ni_log_fl_fw_versions_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (ni_posix_memalign((void **)&buffer, sysconf(_SC_PAGESIZE), size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n", NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(buffer, 0, size);

  if (ni_nvme_send_read_cmd(device_handle,
                            event_handle,
                            buffer,
                            size,
                            QUERY_GET_VERSIONS_R) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
    ni_aligned_free(buffer);
    return NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  memcpy(&fl_fw_versions, buffer, sizeof(ni_log_fl_fw_versions_t));

  memcpy(p_dev_info->fl_ver_last_ran,
         &fl_fw_versions.last_ran_fl_version,
         NI_VERSION_CHARACTER_COUNT);
  memcpy(p_dev_info->fl_ver_nor_flash,
         &fl_fw_versions.nor_flash_fl_version,
         NI_VERSION_CHARACTER_COUNT);
  memcpy(p_dev_info->fw_rev_nor_flash,
         &fl_fw_versions.nor_flash_fw_revision,
         NI_VERSION_CHARACTER_COUNT);

  ni_aligned_free(buffer);
  return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_query_nvme_status(ni_session_context_t *p_ctx,
                                  ni_load_query_t *p_load_query)
{
  void *buffer;
  uint32_t size;

  ni_instance_mgr_general_status_t general_status;

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: p_ctx cannot be null\n");
    return NI_RETCODE_INVALID_PARAM;
  }
  if (!p_load_query)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: p_load_query cannot be null\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "6O") < 0)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, 
           "ERROR: %s function not supported on device with FW API version < 6.O\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }

  buffer = NULL;
  size = ((sizeof(ni_instance_mgr_general_status_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (ni_posix_memalign((void **)&buffer, sysconf(_SC_PAGESIZE), size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() Cannot allocate buffer\n", NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(buffer, 0, size);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle,
                            p_ctx->event_handle,
                            buffer,
                            size,
                            QUERY_GET_NVME_STATUS_R) < 0)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "%s(): NVME command Failed\n", __func__);
    ni_aligned_free(buffer);
    return NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  memcpy(&general_status, buffer, sizeof(ni_instance_mgr_general_status_t));

  p_load_query->tp_fw_load = general_status.tp_fw_load;
  p_load_query->pcie_load = general_status.pcie_load;
  p_load_query->pcie_throughput = general_status.pcie_throughput;
  p_load_query->fw_load = general_status.fw_load;
  p_load_query->fw_share_mem_usage = general_status.fw_share_mem_usage;

  ni_aligned_free(buffer);
  return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_query_vf_ns_id(ni_device_handle_t device_handle,
                               ni_device_vf_ns_id_t *p_dev_ns_vf,
                               uint8_t fw_rev[])
{
  void *p_buffer = NULL;
  uint32_t size;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  if ((NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_dev_ns_vf))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters are null, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "6m") < 0)
  {
    ni_log(NI_LOG_ERROR,
           "ERROR: %s function not supported on device with FW API version < 6.m\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }

  size = ((sizeof(ni_device_vf_ns_id_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n", NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(p_buffer, 0, size);

  if (ni_nvme_send_read_cmd(device_handle,
                            event_handle,
                            p_buffer,
                            size,
                            QUERY_GET_NS_VF_R) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
    ni_aligned_free(p_buffer);
    return NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  ni_device_vf_ns_id_t *p_dev_ns_vf_data = (ni_device_vf_ns_id_t *)p_buffer;
  p_dev_ns_vf->vf_id = p_dev_ns_vf_data->vf_id;
  p_dev_ns_vf->ns_id = p_dev_ns_vf_data->ns_id;
  ni_log(NI_LOG_DEBUG, "%s(): NS/VF %u/%u\n", __func__, p_dev_ns_vf->ns_id, p_dev_ns_vf->vf_id);
  ni_aligned_free(p_buffer);
  return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_query_temperature(ni_device_handle_t device_handle,
                                  ni_device_temp_t *p_dev_temp,
                                  uint8_t fw_rev[])
{
  void *p_buffer = NULL;
  uint32_t size;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  if ((NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_dev_temp))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters are null, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "6rC") < 0)
  {
    ni_log(NI_LOG_ERROR,
           "ERROR: %s function not supported on device with FW API version < 6rC\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }

  size = ((sizeof(ni_device_temp_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n", NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(p_buffer, 0, size);

  if (ni_nvme_send_read_cmd(device_handle,
                            event_handle,
                            p_buffer,
                            size,
                            QUERY_GET_TEMPERATURE_R) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
    ni_aligned_free(p_buffer);
    return NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  ni_device_temp_t *p_dev_temp_data = (ni_device_temp_t *)p_buffer;
  p_dev_temp->composite_temp = p_dev_temp_data->composite_temp;
  p_dev_temp->on_board_temp = p_dev_temp_data->on_board_temp;
  p_dev_temp->on_die_temp = p_dev_temp_data->on_die_temp;
  ni_log(NI_LOG_DEBUG, "%s(): current composite temperature %d on board temperature %d on die temperature %d\n", 
         __func__, p_dev_temp->composite_temp, p_dev_temp->on_board_temp, p_dev_temp->on_die_temp);
  ni_aligned_free(p_buffer);
  return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_query_extra_info(ni_device_handle_t device_handle,
                                 ni_device_extra_info_t *p_dev_extra_info,
                                 uint8_t fw_rev[])
{
  void *p_buffer = NULL;
  uint32_t size;
  ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;

  if ((NI_INVALID_DEVICE_HANDLE == device_handle) || (!p_dev_extra_info))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): passed parameters are null, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "6rC") < 0)
  {
    ni_log(NI_LOG_ERROR,
           "ERROR: %s function not supported on device with FW API version < 6rC\n",
           __func__);
    return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
  }

  size = ((sizeof(ni_device_extra_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n", NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  memset(p_buffer, 0, size);

  if (ni_nvme_send_read_cmd(device_handle,
                            event_handle,
                            p_buffer,
                            size,
                            QUERY_GET_EXTTRA_INFO_R) < 0)
  {
    ni_log(NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
    ni_aligned_free(p_buffer);
    return NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  ni_device_extra_info_t *p_dev_extra_info_data = (ni_device_extra_info_t *)p_buffer;
  p_dev_extra_info->composite_temp = p_dev_extra_info_data->composite_temp;
  p_dev_extra_info->on_board_temp = p_dev_extra_info_data->on_board_temp;
  p_dev_extra_info->on_die_temp = p_dev_extra_info_data->on_die_temp;
  if (ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rC") >= 0 &&
      ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rR") < 0)
  {
    p_dev_extra_info->power_consumption = 0xFFFFFFFF;
  }
  else
  {
    p_dev_extra_info->power_consumption = p_dev_extra_info_data->power_consumption;
  }
  ni_log(NI_LOG_DEBUG, "%s(): current composite temperature %d on board temperature %d "
         "on die temperature %d power consumption %d\n", 
         __func__, p_dev_extra_info->composite_temp, p_dev_extra_info->on_board_temp, 
         p_dev_extra_info->on_die_temp, p_dev_extra_info->power_consumption);
  ni_aligned_free(p_buffer);
  return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Allocate log buffer if needed and retrieve firmware logs from device
 *
 *  \param[in] p_ctx        Pointer to a caller allocated
 *                          ni_session_context_t struct
 *  \param[in] p_log_buffer        Reference to pointer to a log buffer
 *                          If log buffer pointer is NULL, this function will allocate log buffer
 *                          NOTE caller is responsible for freeing log buffer after calling this function
 *  \param[in] gen_log_file        Indicating whether it is required to generate log files
 *
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *
 *          on failure
 *              NI_RETCODE_ERROR_MEM_ALOC
 *              NI_RETCODE_INVALID_PARAM
*******************************************************************************/
ni_retcode_t ni_device_alloc_and_get_firmware_logs(ni_session_context_t *p_ctx, void** p_log_buffer, bool gen_log_file)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    if (*p_log_buffer == NULL)
    {
        if (ni_posix_memalign(p_log_buffer, sysconf(_SC_PAGESIZE), TOTAL_CPU_LOG_BUFFER_SIZE))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() Cannot allocate log buffer\n",
                   NI_ERRNO, __func__);
            return NI_RETCODE_ERROR_MEM_ALOC;
        }
    }

    if(*p_log_buffer != NULL){
        memset(*p_log_buffer, 0, TOTAL_CPU_LOG_BUFFER_SIZE);
        retval = ni_dump_log_all_cores(p_ctx, *p_log_buffer, gen_log_file); // dump FW logs
    }else{
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    return retval;
}

/*!*****************************************************************************
 *  \brief  Set up hard coded demo ROI map
 *
 *  \param[in] p_enc_ctx   Pointer to a caller allocated  
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *
 *          on failure
 *              NI_RETCODE_ERROR_MEM_ALOC
*******************************************************************************/
ni_retcode_t ni_set_demo_roi_map(ni_session_context_t *p_enc_ctx)
{
  ni_xcoder_params_t *p_param =
    (ni_xcoder_params_t *)(p_enc_ctx->p_session_config);
  int sumQp = 0, ctu, i, j;
  // mode 1: Set QP for center 1/3 of picture to highest - lowest quality
  // the rest to lowest - highest quality;
  // mode non-1: reverse of mode 1
  int importanceLevelCentre = p_param->roi_demo_mode == 1 ? 40 : 10;
  int importanceLevelRest   = p_param->roi_demo_mode == 1 ? 10 : 40;
  int linesize_aligned = p_param->source_width;
  int height_aligned = p_param->source_height;
  if (QUADRA)
  {
    uint32_t block_size, max_cu_size, customMapSize;
    uint32_t mbWidth;
    uint32_t mbHeight;
    uint32_t numMbs;
    uint32_t roiMapBlockUnitSize;
    uint32_t entryPerMb;
    uint32_t entryWidth;
    uint32_t entryPos;
    bool bIsCenter;

    max_cu_size = p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264 ? 16: 64;
    // AV1 non-8x8-aligned resolution is implicitly cropped due to Quadra HW limitation
    if (NI_CODEC_FORMAT_AV1 == p_enc_ctx->codec_format)
    {
      linesize_aligned = (linesize_aligned / 8) * 8;
      height_aligned = (height_aligned / 8) * 8;
    }
    
    // (ROI map version >= 1) each QP info takes 8-bit, represent 8 x 8
    // pixel block
    block_size =
        ((linesize_aligned + max_cu_size - 1) & (~(max_cu_size - 1))) *
        ((height_aligned + max_cu_size - 1) & (~(max_cu_size - 1))) /
        (8 * 8);

    // need to align to 64 bytes
    customMapSize = ((block_size + 63) & (~63));
    if (!p_enc_ctx->roi_map)
    {
      p_enc_ctx->roi_map =
          (ni_enc_quad_roi_custom_map *)calloc(1, customMapSize);
    }
    if (!p_enc_ctx->roi_map)
    {
      return NI_RETCODE_ERROR_MEM_ALOC;
    }

    // for H.264, select ROI Map Block Unit Size: 16x16
    // for H.265, select ROI Map Block Unit Size: 64x64
    roiMapBlockUnitSize = p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264 ? 16 : 64;

    mbWidth =
        ((linesize_aligned + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    mbHeight =
        ((height_aligned + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    numMbs = mbWidth * mbHeight;

    // copy roi MBs QPs into custom map
    // number of qp info (8x8) per mb or ctb
    entryPerMb = (roiMapBlockUnitSize / 8) * (roiMapBlockUnitSize / 8);
    entryWidth = roiMapBlockUnitSize / 8;
    for (i = 0; i < numMbs; i++)
    {
      for (j = 0; j < entryPerMb; j++)
      {
        entryPos = (i % mbWidth) * entryWidth + j % entryWidth;
        bIsCenter = (entryPos >= mbWidth * entryWidth / 3) && (entryPos < mbWidth * entryWidth * 2 / 3);
        /*
        g_quad_roi_map[i*4+j].field.skip_flag = 0; // don't force
        skip mode g_quad_roi_map[i*4+j].field.roiAbsQp_flag = 1; //
        absolute QP g_quad_roi_map[i*4+j].field.qp_info = bIsCenter
        ? importanceLevelCentre : importanceLevelRest;
        */
        p_enc_ctx->roi_map[i * entryPerMb + j].field.ipcm_flag =
            0; // don't force skip mode
        p_enc_ctx->roi_map[i * entryPerMb + j]
            .field.roiAbsQp_flag = 1; // absolute QP
        p_enc_ctx->roi_map[i * entryPerMb + j].field.qp_info =
            bIsCenter ? importanceLevelCentre : importanceLevelRest;
        sumQp += p_enc_ctx->roi_map[i * entryPerMb + j].field.qp_info;
      }
    }
    p_enc_ctx->roi_len = customMapSize;
    p_enc_ctx->roi_avg_qp =
        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        (sumQp + ((numMbs * entryPerMb) >> 1)) / (numMbs * entryPerMb); // round off
    ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "%s() set avg_qp %d\n",
            __func__, p_enc_ctx->roi_avg_qp);
  }
  else if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264)
  {
    // roi for H.264 is specified for 16x16 pixel macroblocks - 1 MB
    // is stored in each custom map entry

    // number of MBs in each row
    uint32_t mbWidth = (linesize_aligned + 16 - 1) >> 4;
    // number of MBs in each column
    uint32_t mbHeight = (height_aligned + 16 - 1) >> 4;
    uint32_t numMbs   = mbWidth * mbHeight;
    uint32_t customMapSize =
        sizeof(ni_enc_avc_roi_custom_map_t) * numMbs;
    p_enc_ctx->avc_roi_map =
        (ni_enc_avc_roi_custom_map_t *)calloc(1, customMapSize);
    if (!p_enc_ctx->avc_roi_map)
    {
      return NI_RETCODE_ERROR_MEM_ALOC;
    }

    // copy roi MBs QPs into custom map
    for (i = 0; i < numMbs; i++)
    {
      if ((i % mbWidth > mbWidth / 3) && (i % mbWidth < mbWidth * 2 / 3))
      {
        p_enc_ctx->avc_roi_map[i].field.mb_qp = importanceLevelCentre;
      }
      else
      {
        p_enc_ctx->avc_roi_map[i].field.mb_qp = importanceLevelRest;
      }
      sumQp += p_enc_ctx->avc_roi_map[i].field.mb_qp;
    }
    p_enc_ctx->roi_len = customMapSize;
    p_enc_ctx->roi_avg_qp =
        (sumQp + (numMbs >> 1)) / numMbs; // round off
  }
  else if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_H265)
  {
    // roi for H.265 is specified for 32x32 pixel subCTU blocks - 4
    // subCTU QPs are stored in each custom CTU map entry

    // number of CTUs in each row
    uint32_t ctuWidth = (linesize_aligned + 64 - 1) >> 6;
    // number of CTUs in each column
    uint32_t ctuHeight = (height_aligned + 64 - 1) >> 6;
    // number of sub CTUs in each row
    uint32_t subCtuWidth = ctuWidth * 2;
    // number of CTUs in each column
    uint32_t subCtuHeight = ctuHeight * 2;
    uint32_t numSubCtus   = subCtuWidth * subCtuHeight;

    p_enc_ctx->hevc_sub_ctu_roi_buf = (uint8_t *)malloc(numSubCtus);
    if (!p_enc_ctx->hevc_sub_ctu_roi_buf)
    {
      return NI_RETCODE_ERROR_MEM_ALOC;
    }
    for (i = 0; i < numSubCtus; i++)
    {
      if ((i % subCtuWidth > subCtuWidth / 3) &&
          (i % subCtuWidth < subCtuWidth * 2 / 3))
      {
        p_enc_ctx->hevc_sub_ctu_roi_buf[i] = importanceLevelCentre;
      }
      else
      {
        p_enc_ctx->hevc_sub_ctu_roi_buf[i] = importanceLevelRest;
      }
    }
    p_enc_ctx->hevc_roi_map = (ni_enc_hevc_roi_custom_map_t *)calloc(
        1, sizeof(ni_enc_hevc_roi_custom_map_t) * ctuWidth * ctuHeight);
    if (!p_enc_ctx->hevc_roi_map)
    {
      return NI_RETCODE_ERROR_MEM_ALOC;
    }

    for (i = 0; i < ctuHeight; i++)
    {
      uint8_t *ptr = &p_enc_ctx->hevc_sub_ctu_roi_buf[subCtuWidth * i * 2];
      for (j = 0; j < ctuWidth; j++, ptr += 2)
      {
        ctu = (int)(i * ctuWidth + j);
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_0 = *ptr;
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_1 = *(ptr + 1);
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_2 =
            *(ptr + subCtuWidth);
        p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_3 =
            *(ptr + subCtuWidth + 1);
        sumQp += (p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_0 +
                  p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_1 +
                  p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_2 +
                  p_enc_ctx->hevc_roi_map[ctu].field.sub_ctu_qp_3);
      }
    }
    p_enc_ctx->roi_len =
        ctuWidth * ctuHeight * sizeof(ni_enc_hevc_roi_custom_map_t);
    p_enc_ctx->roi_avg_qp =
        (sumQp + (numSubCtus >> 1)) / numSubCtus; // round off.
  }
  return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_enc_prep_reconf_demo_data(ni_session_context_t *p_enc_ctx, ni_frame_t *p_frame)
{
  // NETINT_INTERNAL - currently only for internal testing
  // reset encoder change data buffer for reconf parameters
  ni_retcode_t retval = 0;
  ni_xcoder_params_t *p_param =
    (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
  ni_aux_data_t *aux_data = NULL;
  if (p_param->reconf_demo_mode > XCODER_TEST_RECONF_OFF &&
      p_param->reconf_demo_mode < XCODER_TEST_RECONF_END)
  {
    memset(p_enc_ctx->enc_change_params, 0, sizeof(ni_encoder_change_params_t));
  }

  switch (p_param->reconf_demo_mode) {
    case XCODER_TEST_RECONF_BR:
      if (p_enc_ctx->frame_num ==
          p_param->reconf_hash[p_enc_ctx->reconfigCount][0])
      {
          aux_data = ni_frame_new_aux_data(
              p_frame, NI_FRAME_AUX_DATA_BITRATE, sizeof(int32_t));
          if (!aux_data)
          {
            return NI_RETCODE_ERROR_MEM_ALOC;
          }
          *((int32_t *)aux_data->data) =
              p_param->reconf_hash[p_enc_ctx->reconfigCount][1];

          p_enc_ctx->reconfigCount++;
          if (p_param->cfg_enc_params.hrdEnable)
          {
            p_frame->force_key_frame = 1;
            p_frame->ni_pict_type = PIC_TYPE_IDR;
          }
      }
      break;
    // reconfig intraperiod param
    case XCODER_TEST_RECONF_INTRAPRD:
      if (p_enc_ctx->frame_num ==
          p_param->reconf_hash[p_enc_ctx->reconfigCount][0])
      {
        aux_data = ni_frame_new_aux_data(
              p_frame, NI_FRAME_AUX_DATA_INTRAPRD, sizeof(int32_t));
        if (!aux_data)
        {
          return NI_RETCODE_ERROR_MEM_ALOC;
        }
        int32_t intraprd = *((int32_t *)aux_data->data) =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
        ni_log2(p_enc_ctx, NI_LOG_TRACE,
              "xcoder_send_frame: frame #%lu reconf "
                "intraPeriod %d\n",
                p_enc_ctx->frame_num,
                intraprd);
        p_enc_ctx->reconfigCount++;
      }
      break;
    // reconfig VUI parameters
    case XCODER_TEST_RECONF_VUI_HRD:
      if (p_enc_ctx->frame_num ==
          p_param->reconf_hash[p_enc_ctx->reconfigCount][0])
      {
        aux_data = ni_frame_new_aux_data(p_frame,
                                          NI_FRAME_AUX_DATA_VUI,
                                          sizeof(ni_vui_hrd_t));
        if (!aux_data)
        {
          return NI_RETCODE_ERROR_MEM_ALOC;
        }
        ni_vui_hrd_t *vui = (ni_vui_hrd_t *)aux_data->data;
        vui->colorDescPresent =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
        vui->colorPrimaries =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
        vui->colorTrc =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][3];
        vui->colorSpace =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][4];
        vui->aspectRatioWidth =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][5];
        vui->aspectRatioHeight =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][6];
        vui->videoFullRange =
            p_param->reconf_hash[p_enc_ctx->reconfigCount][7];
        ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                "xcoder_send_frame: frame #%lu reconf "
                "vui colorDescPresent %d colorPrimaries %d "
                "colorTrc %d colorSpace %d aspectRatioWidth %d "
                "aspectRatioHeight %d videoFullRange %d\n",
                p_enc_ctx->frame_num, vui->colorDescPresent,
                vui->colorPrimaries, vui->colorTrc,
                vui->colorSpace, vui->aspectRatioWidth,
                vui->aspectRatioHeight, vui->videoFullRange);

        p_enc_ctx->reconfigCount++;
      }
      break;
    // long term ref
    case XCODER_TEST_RECONF_LONG_TERM_REF:
      // the reconf file data line format for this is:
      // <frame-number>:useCurSrcAsLongtermPic,useLongtermRef where
      // values will stay the same on every frame until changed.
      if (p_enc_ctx->frame_num ==
          p_param->reconf_hash[p_enc_ctx->reconfigCount][0])
      {
        ni_long_term_ref_t *p_ltr;
        aux_data = ni_frame_new_aux_data(
            p_frame, NI_FRAME_AUX_DATA_LONG_TERM_REF,
            sizeof(ni_long_term_ref_t));
        if (!aux_data)
        {
          return NI_RETCODE_ERROR_MEM_ALOC;
        }
        p_ltr = (ni_long_term_ref_t *)aux_data->data;
        p_ltr->use_cur_src_as_long_term_pic =
            (uint8_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
        p_ltr->use_long_term_ref =
            (uint8_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
        ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                "xcoder_send_frame: frame #%lu metadata "
                "use_cur_src_as_long_term_pic %d use_long_term_ref "
                "%d\n",
                p_enc_ctx->frame_num,
                p_ltr->use_cur_src_as_long_term_pic,
                p_ltr->use_long_term_ref);
        p_enc_ctx->reconfigCount++;
      }
      break;
      // reconfig min / max QP
    case XCODER_TEST_RECONF_RC_MIN_MAX_QP:
    case XCODER_TEST_RECONF_RC_MIN_MAX_QP_REDUNDANT:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
              p_frame, NI_FRAME_AUX_DATA_MAX_MIN_QP, sizeof(ni_rc_min_max_qp));
            if (!aux_data) {
              return NI_RETCODE_ERROR_MEM_ALOC;
            }

            ni_rc_min_max_qp *qp_info = (ni_rc_min_max_qp *)aux_data->data;
            qp_info->minQpI     = p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            qp_info->maxQpI     = p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
            qp_info->maxDeltaQp = p_param->reconf_hash[p_enc_ctx->reconfigCount][3];
            qp_info->minQpPB    = p_param->reconf_hash[p_enc_ctx->reconfigCount][4];
            qp_info->maxQpPB    = p_param->reconf_hash[p_enc_ctx->reconfigCount][5];

            p_enc_ctx->reconfigCount++;
        }
        break;
#ifdef QUADRA
    // reconfig LTR interval
    case XCODER_TEST_RECONF_LTR_INTERVAL:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(p_frame,
                                              NI_FRAME_AUX_DATA_LTR_INTERVAL,
                                              sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "ltrInterval %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    // invalidate reference frames
    case XCODER_TEST_INVALID_REF_FRAME:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_INVALID_REF_FRAME,
                sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "invalidFrameNum %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    // reconfig framerate
    case XCODER_TEST_RECONF_FRAMERATE:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            ni_framerate_t *framerate;

            aux_data = ni_frame_new_aux_data(p_frame,
                                              NI_FRAME_AUX_DATA_FRAMERATE,
                                              sizeof(ni_framerate_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }

            framerate = (ni_framerate_t *)aux_data->data;
            framerate->framerate_num =
                (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            framerate->framerate_denom =
                (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "framerate (%d/%d)\n",
                    p_enc_ctx->frame_num, framerate->framerate_num,
                    framerate->framerate_denom);
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_MAX_FRAME_SIZE:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "maxFrameSize %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_CRF:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_CRF, sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "crf %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_CRF_FLOAT:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_CRF_FLOAT, sizeof(float));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            float crf = (float)(p_param->reconf_hash[p_enc_ctx->reconfigCount][1] +
            (float)p_param->reconf_hash[p_enc_ctx->reconfigCount][2] / 100.0);
            *((float *)aux_data->data) = crf;
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "crf %f\n",
                    p_enc_ctx->frame_num, crf);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_VBV:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            int32_t vbvBufferSize = p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
            int32_t vbvMaxRate = p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            if ((vbvBufferSize < 10 && vbvBufferSize != 0) || vbvBufferSize > 3000)
            {
                ni_log2(p_enc_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid vbvBufferSize value %d\n",
                      __func__, vbvBufferSize);
                return NI_RETCODE_INVALID_PARAM;
            }
            if (p_param->bitrate > 0 && vbvMaxRate > 0 && vbvMaxRate < p_param->bitrate) {
                ni_log2(p_enc_ctx, NI_LOG_ERROR, "vbvMaxRate %u cannot be smaller than bitrate %d\n",
                       vbvMaxRate, p_param->bitrate);
                return NI_RETCODE_INVALID_PARAM;
            }
            if (vbvBufferSize == 0 && vbvMaxRate > 0) {
                ni_log2(p_enc_ctx, NI_LOG_INFO, "vbvMaxRate %d does not take effect when "
                       "vbvBufferSize is 0, force vbvMaxRate to 0\n",
                       vbvMaxRate);
                vbvMaxRate = 0;
            }

            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_VBV_MAX_RATE, sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) = vbvMaxRate;
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_VBV_BUFFER_SIZE, sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int32_t *)aux_data->data) = vbvBufferSize;
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                   "xcoder_send_frame: frame #%lu reconfig vbvMaxRate %d vbvBufferSize "
                   "%d by frame aux data\n",
                    p_enc_ctx->frame_num, vbvMaxRate, vbvBufferSize);
            
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            int maxFrameSizeRatio = p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            if (maxFrameSizeRatio < 1) {
                ni_log2(p_enc_ctx, NI_LOG_ERROR, "maxFrameSizeRatio %d cannot < 1\n",
                       maxFrameSizeRatio);
                return NI_RETCODE_INVALID_PARAM;
            }
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }

            int32_t bitrate, framerate_num, framerate_denom;
            uint32_t min_maxFrameSize, maxFrameSize;
            bitrate = (p_enc_ctx->target_bitrate > 0) ?  p_enc_ctx->target_bitrate : p_param->bitrate;      

            if ((p_enc_ctx->framerate.framerate_num > 0) && (p_enc_ctx->framerate.framerate_denom > 0))
            {
                framerate_num = p_enc_ctx->framerate.framerate_num;
                framerate_denom = p_enc_ctx->framerate.framerate_denom;
            }
            else
            {
                framerate_num = (int32_t) p_param->fps_number;
                framerate_denom = (int32_t) p_param->fps_denominator;
            }

            min_maxFrameSize = ((uint32_t)bitrate / framerate_num * framerate_denom) / 8;
            maxFrameSize = min_maxFrameSize * maxFrameSizeRatio > NI_MAX_FRAME_SIZE ?
                           NI_MAX_FRAME_SIZE : min_maxFrameSize * maxFrameSizeRatio;
            *((int32_t *)aux_data->data) = maxFrameSize;
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu reconf "
                    "maxFrameSizeRatio %d maxFrameSize %d\n",
                    p_enc_ctx->frame_num, maxFrameSizeRatio, maxFrameSize);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_SLICE_ARG:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            aux_data = ni_frame_new_aux_data(
                p_frame, NI_FRAME_AUX_DATA_SLICE_ARG, sizeof(int16_t));
            if (!aux_data) {
                return NI_RETCODE_ERROR_MEM_ALOC;
            }
            *((int16_t *)aux_data->data) =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu reconf "
                    "sliceArg %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    // force IDR frame through API test code
    case XCODER_TEST_FORCE_IDR_FRAME:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            ni_force_idr_frame_type(p_enc_ctx);
            ni_log2(p_enc_ctx, NI_LOG_TRACE, 
                    "xcoder_send_frame: frame #%lu force IDR frame\n",
                    p_enc_ctx->frame_num);

            p_enc_ctx->reconfigCount++;
        }
        break;
    // reconfig bit rate through API test code
    case XCODER_TEST_RECONF_BR_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_reconfig_bitrate(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))){
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig BR %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
      case XCODER_TEST_RECONF_INTRAPRD_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            int32_t intraprd = 
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            if ((retval = ni_reconfig_intraprd(p_enc_ctx, intraprd))){
                return retval;
            }
            ni_log(NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig intraPeriod %d\n",
                    p_enc_ctx->frame_num,
                    intraprd);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_VUI_HRD_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            ni_vui_hrd_t vui;
            vui.colorDescPresent =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            vui.colorPrimaries =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
            vui.colorTrc =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][3];
            vui.colorSpace =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][4];
            vui.aspectRatioWidth =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][5];
            vui.aspectRatioHeight =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][6];
            vui.videoFullRange =
                p_param->reconf_hash[p_enc_ctx->reconfigCount][7];
            if ((retval = ni_reconfig_vui(p_enc_ctx, &vui))){
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu reconf "
                    "vui colorDescPresent %d colorPrimaries %d "
                    "colorTrc %d colorSpace %d aspectRatioWidth %d "
                    "aspectRatioHeight %d videoFullRange %d\n",
                    p_enc_ctx->frame_num, vui.colorDescPresent,
                    vui.colorPrimaries, vui.colorTrc,
                    vui.colorSpace, vui.aspectRatioWidth,
                    vui.aspectRatioHeight, vui.videoFullRange);
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_LTR_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            ni_long_term_ref_t ltr;
            ltr.use_cur_src_as_long_term_pic =
                (uint8_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            ltr.use_long_term_ref =
                (uint8_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][2];

            if ((retval = ni_set_ltr(p_enc_ctx, &ltr))) {
               return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame(): frame #%lu API set LTR\n",
                    p_enc_ctx->frame_num);
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API:
    case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API_REDUNDANT:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
              ni_rc_min_max_qp qp_info;
              qp_info.minQpI = (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
              qp_info.maxQpI = (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
              qp_info.maxDeltaQp = (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][3];
              qp_info.minQpPB = (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][4];
              qp_info.maxQpPB = (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][5];
              if ((retval = ni_reconfig_min_max_qp(p_enc_ctx, &qp_info))) {
                 return retval;
              }
              ni_log2(p_enc_ctx, NI_LOG_DEBUG,
                "%s(): frame %d minQpI %d maxQpI %d maxDeltaQp %d minQpPB %d maxQpPB %d\n",
                __func__, p_enc_ctx->frame_num,
                qp_info.minQpI, qp_info.maxQpI, qp_info.maxDeltaQp, qp_info.minQpPB, qp_info.maxQpPB);
              p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_LTR_INTERVAL_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_set_ltr_interval(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, 
                NI_LOG_TRACE,
                "xcoder_send_frame(): frame #%lu API set LTR interval %d\n",
                p_enc_ctx->frame_num,
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_INVALID_REF_FRAME_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_set_frame_ref_invalid(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, 
                NI_LOG_TRACE,
                "xcoder_send_frame(): frame #%lu API set frame ref invalid "
                "%d\n",
                p_enc_ctx->frame_num,
                p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);
            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_FRAMERATE_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            ni_framerate_t framerate;
            framerate.framerate_num =
                (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][1];
            framerate.framerate_denom =
                (int32_t)p_param->reconf_hash[p_enc_ctx->reconfigCount][2];
            if ((retval = ni_reconfig_framerate(p_enc_ctx, &framerate))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig framerate "
                    "(%d/%d)\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1],
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][2]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_MAX_FRAME_SIZE_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_reconfig_max_frame_size(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig maxFrameSize %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_CRF_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_reconfig_crf(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig crf %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_CRF_FLOAT_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            float crf = (float)(p_param->reconf_hash[p_enc_ctx->reconfigCount][1] +
                (float)p_param->reconf_hash[p_enc_ctx->reconfigCount][2] / 100.0);
            if ((retval = ni_reconfig_crf2(p_enc_ctx, crf))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig crf %f\n",
                    p_enc_ctx->frame_num, crf);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_VBV_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_reconfig_vbv_value(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1],
                p_param->reconf_hash[p_enc_ctx->reconfigCount][2]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                   "xcoder_send_frame: frame #%lu API reconfig vbvMaxRate %d vbvBufferSize %d\n",
                   p_enc_ctx->frame_num, 
                   p_param->reconf_hash[p_enc_ctx->reconfigCount][1], 
                   p_param->reconf_hash[p_enc_ctx->reconfigCount][2]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {

            if ((retval = ni_reconfig_max_frame_size_ratio(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu reconf maxFrameSizeRatio %d\n",
                    p_enc_ctx->frame_num, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
    case XCODER_TEST_RECONF_SLICE_ARG_API:
        if (p_enc_ctx->frame_num ==
            p_param->reconf_hash[p_enc_ctx->reconfigCount][0]) {
            if ((retval = ni_reconfig_slice_arg(
                p_enc_ctx, p_param->reconf_hash[p_enc_ctx->reconfigCount][1]))) {
                return retval;
            }
            ni_log2(p_enc_ctx, NI_LOG_TRACE,
                    "xcoder_send_frame: frame #%lu API reconfig sliceArg %d\n",
                    p_enc_ctx->frame_num,
                    p_param->reconf_hash[p_enc_ctx->reconfigCount][1]);

            p_enc_ctx->reconfigCount++;
        }
        break;
#endif
  case XCODER_TEST_RECONF_OFF:
  default:
    ;
  }
  return NI_RETCODE_SUCCESS;
}

/**
 * Locale-independent conversion of ASCII characters to lowercase.
 */
static int ni_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        c ^= 0x20;
    return c;
}

int ni_strcasecmp(const char *a, const char *b)
{
    uint8_t c1, c2;
    do
    {
        c1 = ni_tolower(*a++);
        c2 = ni_tolower(*b++);
    } while (c1 && c1 == c2);
    return c1 - c2;
}

void ni_gop_params_check_set(ni_xcoder_params_t *p_param, char *value)
{
  ni_encoder_cfg_params_t *p_enc = &p_param->cfg_enc_params;
  ni_custom_gop_params_t* p_gop = &p_enc->custom_gop_params;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0))
    p_gop->pic_param[0].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC0_USED))
    p_gop->pic_param[0].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1))
    p_gop->pic_param[0].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC1_USED))
    p_gop->pic_param[0].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2))
    p_gop->pic_param[0].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC2_USED))
    p_gop->pic_param[0].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3))
    p_gop->pic_param[0].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G0_NUM_REF_PIC3_USED))
    p_gop->pic_param[0].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0))
    p_gop->pic_param[1].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC0_USED))
    p_gop->pic_param[1].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1))
    p_gop->pic_param[1].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC1_USED))
    p_gop->pic_param[1].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2))
    p_gop->pic_param[1].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC2_USED))
    p_gop->pic_param[1].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3))
    p_gop->pic_param[1].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G1_NUM_REF_PIC3_USED))
    p_gop->pic_param[1].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0))
    p_gop->pic_param[2].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC0_USED))
    p_gop->pic_param[2].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1))
    p_gop->pic_param[2].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC1_USED))
    p_gop->pic_param[2].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2))
    p_gop->pic_param[2].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC2_USED))
    p_gop->pic_param[2].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3))
    p_gop->pic_param[2].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G2_NUM_REF_PIC3_USED))
    p_gop->pic_param[2].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0))
    p_gop->pic_param[3].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC0_USED))
    p_gop->pic_param[3].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1))
    p_gop->pic_param[3].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC1_USED))
    p_gop->pic_param[3].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2))
    p_gop->pic_param[3].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC2_USED))
    p_gop->pic_param[3].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3))
    p_gop->pic_param[3].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G3_NUM_REF_PIC3_USED))
    p_gop->pic_param[3].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0))
    p_gop->pic_param[4].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC0_USED))
    p_gop->pic_param[4].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1))
    p_gop->pic_param[4].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC1_USED))
    p_gop->pic_param[4].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2))
    p_gop->pic_param[4].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC2_USED))
    p_gop->pic_param[4].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3))
    p_gop->pic_param[4].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G4_NUM_REF_PIC3_USED))
    p_gop->pic_param[4].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0))
    p_gop->pic_param[5].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC0_USED))
    p_gop->pic_param[5].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1))
    p_gop->pic_param[5].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC1_USED))
    p_gop->pic_param[5].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2))
    p_gop->pic_param[5].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC2_USED))
    p_gop->pic_param[5].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3))
    p_gop->pic_param[5].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G5_NUM_REF_PIC3_USED))
    p_gop->pic_param[5].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0))
    p_gop->pic_param[6].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC0_USED))
    p_gop->pic_param[6].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1))
    p_gop->pic_param[6].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC1_USED))
    p_gop->pic_param[6].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2))
    p_gop->pic_param[6].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC2_USED))
    p_gop->pic_param[6].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3))
    p_gop->pic_param[6].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G6_NUM_REF_PIC3_USED))
    p_gop->pic_param[6].rps[3].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0))
    p_gop->pic_param[7].rps[0].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC0_USED))
    p_gop->pic_param[7].rps[0].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1))
    p_gop->pic_param[7].rps[1].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC1_USED))
    p_gop->pic_param[7].rps[1].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2))
    p_gop->pic_param[7].rps[2].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC2_USED))
    p_gop->pic_param[7].rps[2].ref_pic_used = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3))
    p_gop->pic_param[7].rps[3].ref_pic = 1;
  if (!ni_strcasecmp(value, NI_ENC_GOP_PARAMS_G7_NUM_REF_PIC3_USED))
    p_gop->pic_param[7].rps[3].ref_pic_used = 1;

}

bool ni_gop_params_check(ni_xcoder_params_t *p_param)
{
  ni_encoder_cfg_params_t *p_enc = &p_param->cfg_enc_params;
  ni_custom_gop_params_t* p_gop = &p_enc->custom_gop_params;
  int i, j;
  for (i=0; i<NI_MAX_GOP_NUM; i++)
  {
    for (j=0; j<NI_MAX_REF_PIC; j++)
    {
      if (p_gop->pic_param[i].rps[j].ref_pic == 1 &&
          p_gop->pic_param[i].rps[j].ref_pic_used != 1)
      {
        ni_log(NI_LOG_ERROR,
           "g%drefPic%d specified without g%drefPic%dUsed specified!\n",
           i, j, i, j);
        return false;
      }
    }
  }
  // set custom_gop_params default.
  for (i=0; i<NI_MAX_GOP_NUM; i++)
  {
    for (j=0; j<NI_MAX_REF_PIC; j++)
    {
      p_gop->pic_param[i].rps[j].ref_pic = 0;
      p_gop->pic_param[i].rps[j].ref_pic_used = 0;
    }
  }
  return true;
}

/*!*****************************************************************************
 *  \brief   Initiate P2P transfer
 *
 *  \param[in] pSession      Pointer to source card destination
 *  \param[in] source        Pointer to source frame to transmit
 *  \param[in] ui64DestAddr  Destination address on target device
 *  \param[in] ui32FrameSize Size of frame to transfer
 *
 *  \return on success
 *              NI_RETCODE_SUCCESS
 *          on failure
 *              NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION
 *              NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_INVALID_SESSION
 *              NI_RETCODE_ERROR_MEM_ALOC
 *              NI_RETCODE_ERROR_NVME_CMD_FAILED
*******************************************************************************/
ni_retcode_t ni_p2p_xfer(ni_session_context_t *pSession, niFrameSurface1_t *source,
                         uint64_t ui64DestAddr, uint32_t ui32FrameSize)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    /* Firmware compatibility check */
    if ((pSession != NULL) &&
        (ni_cmp_fw_api_ver((char *) &pSession->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rK") < 0))
    {
        ni_log2(pSession, NI_LOG_ERROR, "%s: FW doesn't support this operation\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    if (source)
    {
        retval = ni_send_to_target(source, ui64DestAddr, ui32FrameSize);
    }
    else
    {
        ni_log2(pSession, NI_LOG_ERROR, "%s(): Surface is empty\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
    }

    return retval;
}
