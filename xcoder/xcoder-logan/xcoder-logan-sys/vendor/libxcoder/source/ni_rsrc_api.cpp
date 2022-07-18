/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_rsrc_api.c
*
*  \brief  Exported routines related to resource management of NI T-408 devices
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h> /* For O_* constants */
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/file.h> /* For flock constants */
#endif

#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"

static uint32_t decoder_guid = 0;
static uint32_t encoder_guid = 0;

// Decoder/Encoder reference (resolution_width, resolution_height, framerate) table
ni_rsrc_device_video_ref_cap_t g_device_reference_table[2][2] = 
{
  // decoder
  {
    {1920, 1080, 240}, // H264
    {1920, 1080, 240}, // H265
  },
  // encoder
  {
    {1920, 1080, 240}, // H264
    {1920, 1080, 240}, // H265
  }
};

#ifdef _ANDROID

#include "ni_rsrc_api_android.h"

sp<ISharedBuffer> service = NULL;

/*!*****************************************************************************
 *  \brief   Init android net.int.SharedBuffer service for binder using.
 *
 *	 \param 	  none
 *
 *	 \return      service (= 0) if get service , < 0 otherwise
 *
 ******************************************************************************/
int ni_rsrc_android_init()
{
  if (service == NULL)
  {
    sp<IBinder> binder = defaultServiceManager()->getService(String16(SHARED_BUFFER_SERVICE));
    if (binder == NULL)
    {
      ni_log(NI_LOG_INFO, "Failed to get service: %s.\n", SHARED_BUFFER_SERVICE);
      return -1;
    }

    service = ISharedBuffer::asInterface(binder);
    if (service == NULL)
    {
      return -2;
    }
  }
  return 0;
}

#endif


#ifdef _WIN32

/*!******************************************************************************
 *  \brief  Scans system for all NVMe devices and returns the system device
 *   names to the user which were identified as NETINT transcoder deivices.
 *   Names are suitable for OpenFile api usage afterwards
 *
 *
 *  \param[out] ni_devices  List of device names identified as NETINT NVMe transcoders
 *  \param[in]  max_handles Max number of device names to return
 *
 *  \return     Number if devices found if successfull operation completed
 *              0 if no NETINT NVMe transcoder devices were found
 *              NI_RETCODE_ERROR_MEM_ALOC if memory allocation failed
 *******************************************************************************/
int ni_rsrc_get_local_device_list(
  char   ni_devices[][MAX_DEVICE_NAME_LEN],
  int    max_handles
)
{
  if ((ni_devices == NULL)||(max_handles == 0))
  {
    ni_log(NI_LOG_INFO, "Error with input parameters\n");
    return 0;
  }
  return ni_rsrc_enumerate_devices(ni_devices, max_handles);
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_device_pool_t* ni_rsrc_get_device_pool(void)
{
  ni_device_queue_t* p_device_queue = NULL;
  ni_device_pool_t* p_device_pool = NULL;
  HANDLE map_file_handle = NULL;
  HANDLE mutex_handle = NULL;

  //Create a mutex for protecting the memory area
  mutex_handle = CreateMutex(
      NULL,            // default security attributes
      FALSE,            // initially owned
      CODERS_LCK_NAME  // unnamed mutex
  );

  if (NULL == mutex_handle)
  {
    ni_log(NI_LOG_INFO, "CreateMutex %s failed: %d\n", CODERS_LCK_NAME, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NULL;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(mutex_handle, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_get_device_pool() failed to obtain mutex: %p\n", mutex_handle);
  }

  map_file_handle = OpenFileMapping(
      FILE_MAP_ALL_ACCESS,   // read/write access
      FALSE,                 // do not inherit the name
      CODERS_SHM_NAME        // name of mapping object
  );

  if (NULL == map_file_handle)
  {
    ReleaseMutex(mutex_handle);
    ni_log(NI_LOG_INFO, "Could not open file mapping object %s, error: %d.\n", CODERS_SHM_NAME, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NULL;
  }

  p_device_queue = (  ni_device_queue_t*)MapViewOfFile(
                      map_file_handle,   // handle to map object
                      FILE_MAP_ALL_ACCESS, // read/write permission
                      0,
                      0,
                      sizeof(ni_device_queue_t)
                  );

  if (NULL == p_device_queue)
  {
    ReleaseMutex(mutex_handle);
    CloseHandle(map_file_handle);
    return NULL;
  }

  p_device_pool = (ni_device_pool_t *)malloc(sizeof(ni_device_pool_t));
  if (NULL == p_device_pool)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_rsrc_get_device_pool() malloc failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    UnmapViewOfFile(p_device_queue);
  }
  else
  {
    p_device_pool->lock = mutex_handle;
    p_device_pool->p_device_queue = p_device_queue;
  }

  ReleaseMutex(mutex_handle);
  CloseHandle(map_file_handle);
  return p_device_pool;
}

/*!******************************************************************************
 *  \brief   Initialize and create all resources required to work with NETINT NVMe
 *           transcoder devices. This is a high level API function which is used
 *           mostly with user application like FFMpeg that relies on those resources.
 *           In case of custom application integration, revised functionality might
 *           be necessary utilizing coresponding API functions.
 *
 *  \param[in]   should_match_rev  0: transcoder firmware revision matching the
 *                             library's version is NOT required for placing
 *                             the transcoder into resource pool; 1: otherwise
 *               timeout_seconds    0: No timeout amount, loop until init success
 *                              or fail; else: timeout will fail init once reached
 *
 *  \return
 *           NI_RETCODE_SUCCESS on success
 *           NI_RETCODE_INIT_ALREADY on already init
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init(int should_match_rev, int timeout_seconds)
{
#define SLEEPLOOP 3
  /*! list all XCoder devices under /dev/.. */
  char dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = { 0 };
  int i = 0, j = 0, k = 0, xcoder_device_cnt = 0, fw_ver_compat_warning = 0;
  int runtime = 0;
  DWORD rc = 0;
  int retval = NI_RETCODE_SUCCESS;
  ni_device_info_t device_info = { 0 };
  ni_device_info_t* p_device_info = NULL;
  ni_device_queue_t* p_device_queue = NULL;
  HANDLE lock = NULL;
  HANDLE lock_encoder = NULL;
  HANDLE lock_decoder = NULL;
  HANDLE map_file_handle = NULL;
  ni_device_capability_t device_capabilites = { 0 };
  ni_device_handle_t handle = 0;

  map_file_handle = CreateFileMapping(
      INVALID_HANDLE_VALUE,     // use paging file
      NULL,                     // default security
      PAGE_READWRITE,           // read/write access
      0,                        // maximum object size (high-order DWORD)
      sizeof(ni_device_queue_t),// maximum object size (low-order DWORD)
      CODERS_SHM_NAME           // name of mapping object
  );

  if (NULL == map_file_handle)
  {
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_INFO, "CreateFileMapping returned (%d).\n", rc);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
  else
  {
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    if (ERROR_ALREADY_EXISTS == rc)
    {
      ni_log(NI_LOG_INFO, "NETINT resources have been initialized already, exiting ..\n");
      retval = NI_RETCODE_INIT_ALREADY;
      LRETURN;
    }
    else
    {
      ni_log(NI_LOG_INFO, "NETINT resources not initialized, starting initialization ..\n");
    }
  }

  while (0 == xcoder_device_cnt)
  {
    xcoder_device_cnt = ni_rsrc_enumerate_devices(dev_names, MAX_DEVICE_CNT);

    if (xcoder_device_cnt < 0)
    {
      ni_log(NI_LOG_INFO, "ni_rsrc_init() fatal error %d, exiting ...\n", xcoder_device_cnt);
      retval = NI_RETCODE_FAILURE;
      LRETURN;
    }
    else if (0 == xcoder_device_cnt)
    {
      ni_log(NI_LOG_INFO, "Devices not ready, will retry again ...\n");
      if (stop_process)
      {
        ni_log(NI_LOG_INFO, "Requested to stop, exiting ...\n");
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
      runtime += SLEEPLOOP;
      Sleep(SLEEPLOOP * 1000);
      if (runtime >= timeout_seconds && timeout_seconds != 0)
      {
        ni_log(NI_LOG_INFO, "Timeout reached at %d seconds! Failing\n", runtime);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
    }
  }

  /*! store the guid and number of coders in a shared memory too,
     for later retrieval; the guid order is init'd based on value here,
     and will be re-ordered based on resource distribution logic in
     resource allocation functions */


  p_device_queue = (ni_device_queue_t*)MapViewOfFile(
      map_file_handle,   // handle to map object
      FILE_MAP_ALL_ACCESS, // read/write permission
      0,
      0,
      sizeof(ni_device_queue_t)
  );

  if (NULL == p_device_queue)
  {
    ni_log(NI_LOG_INFO, "Could not map view of file, p_last error (%d).\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  /*! create the lock */
  lock = CreateMutex(
      NULL,            // default security attributes
      FALSE,            // initially owned
      CODERS_LCK_NAME  // unnamed mutex
  );
  if (NULL == lock)
  {
    ni_log(NI_LOG_INFO, "Init CreateMutex %s failed: %d\n", CODERS_LCK_NAME, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(lock, INFINITE)) // no time-out interval)
  {
    ni_log(NI_LOG_INFO, "ERROR %d: failed to obtain mutex: %p\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, lock);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  // init the ni_device_queue_t
  p_device_queue->decoders_cnt = p_device_queue->encoders_cnt = 0;

  for (i = 0; i < NI_MAX_HW_DECODER_COUNT; i++)
  {
    p_device_queue->decoders[i] = -1;
  }

  for (i = 0; i < NI_MAX_HW_ENCODER_COUNT; i++)
  {
    p_device_queue->encoders[i] = -1;
  }

  uint32_t tmp_io_size;
  for (i = 0; i < xcoder_device_cnt; i++)
  {

    /*! retrieve decoder and encoder info and create shared memory
       and named lock accordingly, using NVMe "Identify Controller" */
       /*! check whether Xcoder is supported and retrieve the xcoder info */
    snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s", dev_names[i]);
    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

    handle = ni_device_open(device_info.dev_name, &tmp_io_size);
    if (NI_INVALID_DEVICE_HANDLE == handle)
    {
      continue;
    }

    retval = ni_device_capability_query(handle, &device_capabilites);
    if ((NI_RETCODE_SUCCESS == retval) &&
        (device_capabilites.device_is_xcoder) &&
        (! should_match_rev ||
         (should_match_rev && ni_is_fw_compatible(device_capabilites.fw_rev))))
    {
      fw_ver_compat_warning = 0;
      if (ni_is_fw_compatible(device_capabilites.fw_rev) == 2)
      {
        ni_log(NI_LOG_INFO, "WARNING - Query %s FW version: %.*s is below the minimum support version for "
               "this SW version. Some features may be missing.\n",
               device_info.dev_name, (int) sizeof(device_capabilites.fw_rev),
               device_capabilites.fw_rev);
        fw_ver_compat_warning = 1;
      }
      int total_modules = device_capabilites.h264_decoders_cnt + device_capabilites.h264_encoders_cnt +
          device_capabilites.h265_decoders_cnt + device_capabilites.h265_encoders_cnt;

      for (j = 0; j < total_modules; j++)
      {
        p_device_info = NULL;
        memset(&device_info, 0, sizeof(ni_device_info_t));
        snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s", dev_names[i]);
        ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));

        device_info.hw_id = device_capabilites.xcoder_devices[j].hw_id;
        device_info.fw_ver_compat_warning = fw_ver_compat_warning;
        memcpy(device_info.fw_rev, device_capabilites.fw_rev,
               sizeof(device_info.fw_rev));
        memcpy(device_info.fw_commit_hash, device_capabilites.fw_commit_hash,
               sizeof(device_info.fw_commit_hash) - 1);
        memcpy(device_info.fw_commit_time, device_capabilites.fw_commit_time,
               sizeof(device_info.fw_commit_time) - 1);
        memcpy(device_info.fw_branch_name, device_capabilites.fw_branch_name,
               sizeof(device_info.fw_branch_name) - 1);
        device_info.max_fps_1080p = device_capabilites.xcoder_devices[j].max_1080p_fps;
        device_info.max_instance_cnt = device_capabilites.xcoder_devices[j].max_number_of_contexts;
        device_info.device_type = (ni_device_type_t)device_capabilites.xcoder_devices[j].codec_type;

        int device_cnt_so_far = (NI_DEVICE_TYPE_DECODER == device_info.device_type ? p_device_queue->decoders_cnt : p_device_queue->encoders_cnt);
        int tmp_guid = -1;

        // check if entry has been created for this h/w (hw_id):
        // if not, then create a new entry; otherwise just update it
        for (k = 0; k < device_cnt_so_far; k++)
        {
          tmp_guid = (NI_DEVICE_TYPE_DECODER == device_info.device_type ? p_device_queue->decoders[k] : p_device_queue->encoders[k]);
          ni_device_context_t* p_device_context = ni_rsrc_get_device_context(device_info.device_type, tmp_guid);
          if ( (p_device_context) && (strcmp(p_device_context->p_device_info->dev_name, device_info.dev_name) == 0 ) &&
               (p_device_context->p_device_info->hw_id == device_info.hw_id) )
          {
            p_device_info = p_device_context->p_device_info;
            break;
          }
        }

        ni_codec_t fmt = (ni_codec_t)device_capabilites.xcoder_devices[j].codec_format;

        if (p_device_info)
        {
          ni_log(NI_LOG_INFO, "%s h/w id %d update\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" : "encoder", device_info.hw_id);
          retval = ni_rsrc_fill_device_info(p_device_info, fmt, device_info.device_type, &device_capabilites.xcoder_devices[j]);
        }
        else
        {
          ni_log(NI_LOG_INFO, "%s h/w id %d create\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" : "encoder", device_info.hw_id);
          p_device_info = &device_info;

          retval = ni_rsrc_fill_device_info(p_device_info, fmt, device_info.device_type, &device_capabilites.xcoder_devices[j]);

          if (NI_RETCODE_SUCCESS == retval)
          {
            /*! add the h/w device_info entry */
            if (NI_DEVICE_TYPE_DECODER == device_info.device_type)
            {
              p_device_info->module_id = decoder_guid++;
              p_device_queue->decoders_cnt = decoder_guid;
              p_device_queue->decoders[p_device_info->module_id] = p_device_info->module_id;
            }
            else
            {
              p_device_info->module_id = encoder_guid++;
              p_device_queue->encoders_cnt = encoder_guid;
              p_device_queue->encoders[p_device_info->module_id] = p_device_info->module_id;
            }
            ni_rsrc_get_one_device_info(&device_info);
          }
        }
      } /*! for each device_info */
    } /*! if device supports xcoder */
    else
    {
      ni_log(NI_LOG_INFO, "Query %s rc %d NOT xcoder-support: %u, or mismatch revision: "
             "%s; Not added\n", device_info.dev_name, retval,
             device_capabilites.device_is_xcoder,
             device_capabilites.fw_rev);
    }

    ni_device_close(handle);
  } /*! for each nvme device */

  p_device_queue->decoders_cnt = decoder_guid;
  p_device_queue->encoders_cnt = encoder_guid;

  retval = NI_RETCODE_SUCCESS;

  END;
  if (NI_RETCODE_SUCCESS != retval)
  {
    if (NULL != p_device_queue)
    {
      UnmapViewOfFile(p_device_queue);
    }
    if (NULL != map_file_handle)
    {
      CloseHandle(map_file_handle);
    }
    if (NULL != lock)
    {
      ReleaseMutex(lock);
      CloseHandle(lock);
    }
  }
  else
  {
    UnmapViewOfFile(p_device_queue);
    ReleaseMutex(lock);
    CloseHandle(lock);
  }
  return retval;
}

// return 1 if string key is found in array of strings, 0 otherwise
static int is_str_in_str_array(const char key[],
                               char arr[][MAX_DEVICE_NAME_LEN],
                               int array_size)
{
  int found = 0, i;

  for (i = 0; i < array_size; i++)
  {
    if (0 == strcmp(key, arr[i]))
    {
      found = 1;
      break;
    }
  }
  return found;
}

/*!*****************************************************************************
 *  \brief   Scan and refresh all resources on the host, taking into account
 *           hot-plugged and pulled out cards.
 *
 *  \param[in]   should_match_rev  0: transcoder firmware revision matching the
 *                             library's version is NOT required for placing
 *                             the transcoder into resource pool; 1: otherwise
 *
 *  \return
 *           NI_RETCODE_SUCCESS on success
 *           NI_RETCODE_FAILURE on failure
 *
 ******************************************************************************/
ni_retcode_t ni_rsrc_refresh(int should_match_rev)
{
  static char xcoder_dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  static int xcoder_dev_count = 0;
  char curr_dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  int curr_dev_count = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int i = 0;
  ni_device_t saved_coders = {0};

  if (0 == xcoder_dev_count)
  {
    // retrieve saved info from resource pool at start up
    if ((NI_RETCODE_SUCCESS == ni_rsrc_list_devices(NI_DEVICE_TYPE_DECODER, saved_coders.decoders,&(saved_coders.decoders_cnt))) &&
        (NI_RETCODE_SUCCESS == ni_rsrc_list_devices(NI_DEVICE_TYPE_ENCODER, saved_coders.encoders,&(saved_coders.encoders_cnt))))
    {
      // check if the decoder count equals to the encoder count
      if (saved_coders.decoders_cnt == saved_coders.encoders_cnt)
      {
        xcoder_dev_count = saved_coders.decoders_cnt;
        ni_log(NI_LOG_INFO, "%d devices retrieved from current pool at start up\n",
               xcoder_dev_count);
      }
      else
      {
        retval = NI_RETCODE_FAILURE;
        ni_log(NI_LOG_ERROR, "Error decoder count %d doesn't equal to encoder count %d\n",
               saved_coders.decoders_cnt, saved_coders.encoders_cnt);
        LRETURN;
      }

      // check the device name
      for (i = 0; i < saved_coders.decoders_cnt; i++)
      {
        if (0 == strcmp(saved_coders.decoders[i].dev_name, saved_coders.encoders[i].dev_name))
        {
          xcoder_dev_names[i][0] = '\0';
          strncat(xcoder_dev_names[i], saved_coders.decoders[i].dev_name, strlen(saved_coders.decoders[i].dev_name));
        }
        else
        {
          retval = NI_RETCODE_FAILURE;
          ni_log(NI_LOG_ERROR, "Error the names of the encoding(%s) and decoding(%s) devices don't match, Index:%d\n",
                 saved_coders.decoders[i].dev_name, saved_coders.encoders[i].dev_name, i);
          LRETURN;
        }
      }
    }
    else
    {
      retval = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_INFO, "Error retrieving from current pool at start up\n");
      LRETURN;
    }
  }

  curr_dev_count = ni_rsrc_get_local_device_list(curr_dev_names,
                                                 MAX_DEVICE_CNT);
  // remove from resource pool any device that is not available now
  for (i = 0; i < xcoder_dev_count; i++)
  {
    if (! is_str_in_str_array(xcoder_dev_names[i], curr_dev_names,
                              curr_dev_count))
    {
      ni_log(NI_LOG_INFO, "\n\n%d. %s NOT in current scanned list, removing !\n", i,
             xcoder_dev_names[i]);
      if (NI_RETCODE_SUCCESS ==
          ni_rsrc_remove_device(xcoder_dev_names[i]))
      {
        ni_log(NI_LOG_INFO, "%s deleted successfully !\n", xcoder_dev_names[i]);
      }
      else
      {
        ni_log(NI_LOG_INFO, "%s failed to delete !\n", xcoder_dev_names[i]);
      }
    }

  }

  // and add into resource pool any newly discoved ones
  for (i = 0; i < curr_dev_count; i++)
  {
    if (! is_str_in_str_array(curr_dev_names[i], xcoder_dev_names,
                              xcoder_dev_count))
    {
      ni_log(NI_LOG_INFO, "\n\n%s NOT in previous list, adding !\n",curr_dev_names[i]);
      if (NI_RETCODE_SUCCESS == ni_rsrc_add_device(curr_dev_names[i],
                                                   should_match_rev))
      {
        ni_log(NI_LOG_INFO, "%s refresh added successfully !\n", curr_dev_names[i]);
      }
      else
      {
        ni_log(NI_LOG_INFO, "%s failed to add !\n", curr_dev_names[i]);
      }
    }
  }

  // update the saved device name list
  for (i = 0; i < curr_dev_count; i++)
  {
    xcoder_dev_names[i][0] = '\0';
    strncat(xcoder_dev_names[i], curr_dev_names[i], strlen(curr_dev_names[i]));
  }
  xcoder_dev_count = curr_dev_count;

  END;

  return retval;
}


/*!******************************************************************************
*  \brief      Allocates and returns a pointer to ni_device_context_t struct
*              based on provided device_type and guid.
*              To be used for load update and codec query.
*
*  \param[in]  device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*  \param[in]  guid         GUID of the encoder or decoder device
*
*  \return     pointer to ni_device_context_t if found, NULL otherwise
*
*  Note:       The returned ni_device_context_t content is not supposed to be used by
*              caller directly: should only be passed to API in the subsequent
*              calls; also after its use, the context should be released by
*              calling ni_rsrc_free_device_context.
*******************************************************************************/
ni_device_context_t* ni_rsrc_get_device_context(ni_device_type_t device_type, int guid)
{
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  ni_device_context_t* p_device_context = NULL;
  ni_device_info_t* p_device_queue = NULL;
  HANDLE map_file_handle = NULL;
  HANDLE mutex_handle = NULL;

  /*! get names of shared mem and lock by GUID */
  ni_rsrc_get_shm_name(device_type, guid, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(device_type, guid, lck_name, sizeof(lck_name));
  //Create a mutex for protecting the memory area
  mutex_handle = CreateMutex(
      NULL,              // default security attributes
      FALSE,             // initially owned
      lck_name);             // unnamed mutex

  if (NULL == mutex_handle)
  {
    ni_log(NI_LOG_INFO, "CreateMutex %s error: %d\n", lck_name, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    p_device_context = NULL;
    LRETURN;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(mutex_handle, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_get_device_context() failed to obtain mutex: %p\n", mutex_handle);
  }

  map_file_handle = OpenFileMapping(
                                      FILE_MAP_ALL_ACCESS,   // read/write access
                                      FALSE,                 // do not inherit the name
                                      (LPCSTR)shm_name
                                   );               // name of mapping object

  if (NULL == map_file_handle)
  {
    ni_log(NI_LOG_INFO, "Could not open file mapping object %s, error: %d.\n", shm_name, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    p_device_context = NULL;
    LRETURN;
  }

  p_device_queue = (ni_device_info_t*)MapViewOfFile(
                                                      map_file_handle,   // handle to map object
                                                      FILE_MAP_ALL_ACCESS, // read/write permission
                                                      0,
                                                      0,
                                                      sizeof(ni_device_info_t)
                                                    );

  if (NULL == p_device_queue)
  {
    ni_log(NI_LOG_INFO, "Could not map view of file, p_last error (%d).\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    /*! lock the file for access */
    p_device_context = NULL;
    LRETURN;
  }

  p_device_context = (ni_device_context_t *)malloc(sizeof(ni_device_context_t));
  if (NULL == p_device_context)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_rsrc_get_device_context() malloc failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    p_device_context = NULL;
    LRETURN;
  }

  p_device_context->shm_name[0] = '\0';
  strncat(p_device_context->shm_name, shm_name, sizeof(p_device_context->shm_name));
  p_device_context->lock = mutex_handle;
  p_device_context->p_device_info = p_device_queue;

  END;

  if (NULL == p_device_context)
  {
    if (NULL != p_device_queue)
    {
      UnmapViewOfFile(p_device_queue);
    }
    if (NULL != mutex_handle)
    {
      ReleaseMutex(mutex_handle);
      CloseHandle(mutex_handle);
    }

    if (NULL !=  map_file_handle)
    {
      CloseHandle(map_file_handle);
    }
  }
  else
  {
    ReleaseMutex(mutex_handle);
    CloseHandle(map_file_handle);
  }

  return p_device_context;
}
#elif __linux__

/*!*****************************************************************************
 *  \brief  Scans system for all NVMe devices and returns the system device
 *   names to the user which were identified as NETINT transcoder deivices.
 *   Names are suitable for resource management api usage afterwards
 *
 *
 *  \param[out] ni_devices  List of device names identified as NETINT NVMe
 *                          transcoders
 *  \param[in]  max_handles Max number of device names to return
 *
 *  \return     Number if devices found if successfull operation completed
 *              0 if no NETINT NVMe transcoder devices were found
 ******************************************************************************/
int ni_rsrc_get_local_device_list(char   ni_devices[][MAX_DEVICE_NAME_LEN],
                                  int    max_handles)
{
  /* list all XCoder devices under /dev/.. */
  const char* dir_name = "/dev";
  char dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  int i, num_dev = 0, xcoder_device_cnt = 0;
  DIR* FD;
  struct dirent* in_file;
  ni_device_info_t device_info;
  ni_device_capability_t device_capabilites;
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE;
  ni_retcode_t rc;

  if ((ni_devices == NULL)||(max_handles == 0))
  {
    ni_log(NI_LOG_INFO, "Error with input parameters\n");
    return 0;
  }

  if (NULL == (FD = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "Error %d: failed to open directory %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, dir_name);
    return 0;
  }

  /* collect all the available NVMe devices and sort */
  while ((in_file = readdir(FD)))
  {
    /*! skip current and parent directory */
    if (!strcmp(in_file->d_name, ".") || !strcmp(in_file->d_name, ".."))
    {
      continue;
    }

    /* pick only those files with name nvmeX where X consists of 1-n
       digits */
    size_t lenstr = strlen(in_file->d_name);
    if (!strncmp(in_file->d_name, NI_NVME_PREFIX, strlen(NI_NVME_PREFIX)))
    {
      int i;
      int skip_this = 0;
      for (i = strlen(NI_NVME_PREFIX); i < lenstr; i++)
      {
#ifdef XCODER_LINUX_VIRTIO_DRIVER_ENABLED
        //In Linux virtual machine with VirtIO driver, the device name does not contain a number, which like "vda", "vdb", etc.
        if (isdigit(in_file->d_name[i]))
#else
        if (!isdigit(in_file->d_name[i]))
#endif
        {
          skip_this = 1;
          break;
        }
      }
      if (skip_this == 0)
      {
        dev_names[num_dev][0] = '\0';
        strncat(dev_names[num_dev], in_file->d_name, strlen(in_file->d_name));
        num_dev++;
      }
    }
  }
  closedir(FD);

  if (num_dev)
  {
    qsort(dev_names, num_dev, sizeof(dev_names[0]), ni_rsrc_strcmp);

    uint32_t tmp_io_size;
    for (i = 0; i < num_dev; i++)
    {
      memset(&device_info, 0, sizeof(ni_device_info_t));
      sprintf(device_info.dev_name, "/dev/%s", dev_names[i]);
      memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

#ifdef XCODER_IO_RW_ENABLED
      ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
      dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);
#else
      dev_handle = ni_device_open(device_info.dev_name, &tmp_io_size);
#endif
      if (NI_INVALID_DEVICE_HANDLE != dev_handle)
      {
        rc = ni_device_capability_query(dev_handle,&device_capabilites);
        if (NI_RETCODE_SUCCESS == rc)
        {
          if (device_capabilites.device_is_xcoder)
          {
            ni_devices[xcoder_device_cnt][0] = '\0';
            strncat(ni_devices[xcoder_device_cnt],
                   device_info.dev_name, strlen(device_info.dev_name));
            xcoder_device_cnt++;
          }
        }
        ni_device_close(dev_handle);
      }
    }
  }

  return xcoder_device_cnt;
}

/*!*****************************************************************************
 *  \brief  Create and return the allocated ni_device_pool_t struct
 *
 *  \param  None
 *
 *  \return Pointer to ni_device_pool_t struct on success, or NULL on failure
 *******************************************************************************/
ni_device_pool_t* ni_rsrc_get_device_pool(void)
{
  int shm_fd = 0;
  ni_device_queue_t* p_device_queue = NULL;
  ni_lock_handle_t lock;
  ni_device_pool_t* p_device_pool = NULL;

#ifdef _ANDROID
  char workDir[] = "/dev/shm";
  if (0 != access(workDir,0))
  {
    if (0 != mkdir(workDir,777))
    {
      ni_log(NI_LOG_ERROR, "Error create /dev/shm folder...\n");
      return NULL;
    }
  }
#endif
  lock = open(CODERS_LCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open CODERS_LCK_NAME...\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NULL;
  }
  flock(lock, LOCK_EX);

#ifdef _ANDROID
  /*! return if init has already been done */
  int ret = ni_rsrc_android_init();

  String8 param;
  param.setTo(CODERS_SHM_NAME, STR_BUFFER_LEN);
  if (service == NULL)
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_pool Error service ..\n");
    LRETURN;
  }
  shm_fd = service->getFd(param);
  if (shm_fd < 0)
  {
    shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
    if (shm_fd >= 0)
    {
      service->setFd(param, shm_fd);
    }
  }
#else
  shm_fd = shm_open(CODERS_SHM_NAME, O_RDWR, 0777);
#endif
  
  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: shm_open SHM_CODERS ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    LRETURN;
  }

  p_device_queue = (ni_device_queue_t*)mmap(0, sizeof(ni_device_queue_t), PROT_READ | PROT_WRITE,
    MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
    ni_log(NI_LOG_ERROR, "Error %d: mmap ni_device_queue_t ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    LRETURN;
  }

  p_device_pool = (ni_device_pool_t *)malloc(sizeof(ni_device_pool_t));
  if (NULL == p_device_pool)
  {
    ni_log(NI_LOG_ERROR, "Error %d: malloc ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    munmap(p_device_queue, sizeof(ni_device_queue_t));
  }
  else
  {
    p_device_pool->lock = lock;
    p_device_pool->p_device_queue = p_device_queue;
  }

  END;

  flock(lock, LOCK_UN);
  
#ifndef _ANDROID
  close(shm_fd);
#endif

  return p_device_pool;
}

/*!******************************************************************************
 *  \brief   Initialize and create all resources required to work with NETINT NVMe
 *           transcoder devices. This is a high level API function which is used
 *           mostly with user application like FFMpeg that relies on those resources.
 *           In case of custom application integration, revised functionality might
 *           be necessary utilizing corresponding API functions.
 *
 *  \param[in]   should_match_rev  0: transcoder firmware revision matching the
 *                             library's version is NOT required for placing
 *                             the transcoder into resource pool; 1: otherwise
 *               timeout_seconds   0: No timeout amount, loop until init success
 *                              or fail; else: timeout will fail init once reached
 *
 *  \return
 *           NI_RETCODE_SUCCESS on success
 *           NI_RETCODE_INIT_ALREADY on already init
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init(int should_match_rev, int timeout_seconds)
{
#define SLEEPLOOP 3
  /*! list all XCoder devices under /dev/.. */
  const char* dir_name = "/dev";
  char dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  int i, j, k, num_dev = 0, xcoder_device_cnt = 0, fw_ver_compat_warning = 0;
  int runtime = 0;
  DIR* FD;
  struct dirent* in_file;
  ni_device_info_t device_info;
  ni_device_queue_t* p_device_queue;
  ni_lock_handle_t lock = NI_INVALID_DEVICE_HANDLE;
  ni_lock_handle_t locken = NI_INVALID_DEVICE_HANDLE;
  ni_lock_handle_t lockde = NI_INVALID_DEVICE_HANDLE;
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE;
  ni_device_capability_t device_capabilites;
  ni_retcode_t rc;
  int retval;

#ifdef _ANDROID
  /*! return if init has already been done */
  int ret = ni_rsrc_android_init();

  char workDir[] = "/dev/shm";
  if (0 != access(workDir,0))
  {
    if (0 != mkdir(workDir,777))
    {
      ni_log(NI_LOG_ERROR, "Error create /dev/shm folder...\n");
      return 1;
    }
  }
  
  String8 param;
  param.setTo(CODERS_SHM_NAME, STR_BUFFER_LEN);
  if (service == NULL)
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_init 000 Error service ..\n");
    return NI_RETCODE_FAILURE;
  }
  int shm_fd = service->getFd(param);
  if (shm_fd < 0)
  {
    shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
    if (shm_fd >= 0)
    {
      service->setFd(param, shm_fd);
    }
  }
  else
  {
    ni_log(NI_LOG_INFO, "NI resource init'd already ..\n");
    return NI_RETCODE_INIT_ALREADY;
  }
#else
  int shm_fd = shm_open(CODERS_SHM_NAME, O_RDWR, 0777);

  if (shm_fd >= 0)
  {
    ni_log(NI_LOG_INFO, "NI resource init'd already ..\n");
    close(shm_fd);
    return NI_RETCODE_INIT_ALREADY;
  }
  else
  {
    if (ENOENT == NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR)
    {
      ni_log(NI_LOG_INFO, "NI resource not init'd, continue ..\n");
    }
    else
    {
      ni_log(NI_LOG_ERROR, "Error access to NI resources, check privileges ..\n");
      return NI_RETCODE_FAILURE;
    }
  }
#endif

read_dev_files:
  num_dev = 0;

  if (NULL == (FD = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "Error %d: failed to open directory %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, dir_name);
    return NI_RETCODE_FAILURE;
  }

  /*! collect all the available NVMe devices and sort */
  while ((in_file = readdir(FD)))
  {
    /*! skip current and parent directory */
    if (!strcmp(in_file->d_name, ".") || !strcmp(in_file->d_name, ".."))
    {
      continue;
    }

    /*! pick only those files with name nvmeX where X consists of 1-n digits */
    size_t lenstr = strlen(in_file->d_name);
    int skip_this = 0;
    if (!strncmp(in_file->d_name, NI_NVME_PREFIX, strlen(NI_NVME_PREFIX)))
    {
      int i;
      for (i = strlen(NI_NVME_PREFIX); i < lenstr; i++)
      {
#ifdef XCODER_LINUX_VIRTIO_DRIVER_ENABLED
        //In Linux virtual machine with VirtIO driver, the device name does not contain a number, which like "vda", "vdb", etc.
        if (isdigit(in_file->d_name[i]))
#else
        if (!isdigit(in_file->d_name[i]))
#endif
        {
          skip_this = 1;
          break;
        }
      }
      if (skip_this == 0)
      {
        ni_log(NI_LOG_INFO, "Reading device file: %s\n", in_file->d_name);
        dev_names[num_dev][0] = '\0';
        strncat(dev_names[num_dev], in_file->d_name, strlen(in_file->d_name));
        num_dev++;
      }
    }
  }
  closedir(FD);
  if (num_dev == 0)
  {
    ni_log(NI_LOG_INFO, "NVMe Devices not ready, wait ..\n");
    if (stop_process)
    {
      ni_log(NI_LOG_INFO, "Requested to stop ..\n");
      return NI_RETCODE_FAILURE;
    }

    runtime += SLEEPLOOP;
    sleep(SLEEPLOOP);
    if (runtime >= timeout_seconds && timeout_seconds != 0)
    {
      ni_log(NI_LOG_INFO, "Timeout reached at %d seconds! Failing\n", runtime);
      return NI_RETCODE_FAILURE;
    }
    goto read_dev_files;
  }

  qsort(dev_names, num_dev, sizeof(dev_names[0]), ni_rsrc_strcmp);

  /*! go through the NVMe devices to check if there is any supporting Xcoder */
  ni_log(NI_LOG_INFO, "Compatible minimum FW ver: %s, FW API flavors: %s, minimum API ver: %d\n",
         NI_XCODER_FW_VER_SUPPORTED_MIN,
         NI_XCODER_FW_API_FLAVORS_SUPPORTED,
         NI_XCODER_FW_API_VER_SUPPORTED_MIN);
  xcoder_device_cnt = 0;
  uint32_t tmp_io_size;
  for (i = 0; i < num_dev; i++)
  {
    memset(&device_info, 0, sizeof(ni_device_info_t));
    sprintf(device_info.dev_name, "/dev/%s", dev_names[i]);
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

#ifdef XCODER_IO_RW_ENABLED
    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);
#else
    dev_handle = ni_device_open(device_info.dev_name, &tmp_io_size);
#endif
    if (NI_INVALID_DEVICE_HANDLE != dev_handle)
    {
      rc = ni_device_capability_query(dev_handle, &device_capabilites);
      if (NI_RETCODE_SUCCESS == rc)
      {
        if (device_capabilites.device_is_xcoder &&
            (! should_match_rev ||
             (should_match_rev &&
              ni_is_fw_compatible(device_capabilites.fw_rev))))
        {
          ni_log(NI_LOG_INFO, "%d. %s %s num_hw: %d\n", i+1, device_info.dev_name, device_info.blk_name,
                 device_capabilites.hw_elements_cnt);
          xcoder_device_cnt++;
        }
        else
        {
          ni_log(NI_LOG_INFO, "Device %s %s not added as it is not an xcoder, or has incompatible FW rev: %s\n",
                 device_info.dev_name,
                 device_info.blk_name,
                 device_capabilites.fw_rev);
        }
      }

      ni_device_close(dev_handle);
    }
  }

  if (0 == xcoder_device_cnt)
  {
    ni_log(NI_LOG_INFO, "NVMe Devices supporting XCoder not ready, wait ..\n");
    if (stop_process)
    {
      ni_log(NI_LOG_INFO, "Requested to stop ..\n");
      return NI_RETCODE_FAILURE;
    }
    runtime += SLEEPLOOP;
    sleep(SLEEPLOOP);
    ni_log(NI_LOG_INFO, "runtime at %d seconds! Timeout at %d \n", runtime, timeout_seconds);
    if (runtime >= timeout_seconds && timeout_seconds != 0)
    {
      ni_log(NI_LOG_INFO, "Timeout reached at %d seconds! Failing\n", runtime);
      return NI_RETCODE_FAILURE;
    }

    goto read_dev_files;
  }

  /*! store the guid and number of coders in a shared memory too,
     for later retrieval; the guid order is init'd based on value here,
     and will be re-ordered based on resource distribution logic in
     resource allocation functions */
  ni_log(NI_LOG_INFO, "Creating shm_name: %s  lck_name: %s\n",
      CODERS_SHM_NAME, CODERS_LCK_NAME);

#ifdef _ANDROID
  /*! return if init has already been done */
  ret = ni_rsrc_android_init(); 

  param.setTo(CODERS_SHM_NAME, STR_BUFFER_LEN);
  if (service == NULL)
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_init 111 Error service ..\n");
    return NI_RETCODE_FAILURE;
  }
  shm_fd = service->getFd(param);
  if (shm_fd < 0)
  {
    shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
    if (shm_fd >= 0)
    {
      service->setFd(param, shm_fd);
    }
  }
#else
  shm_fd = shm_open(CODERS_SHM_NAME, O_CREAT | O_RDWR, 0777);
#endif
  
  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: shm_open ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NI_RETCODE_FAILURE;
  }

#ifndef _ANDROID
  if (ftruncate(shm_fd, sizeof(ni_device_queue_t)) < 0)
  {
    ni_log(NI_LOG_ERROR, "Error ftruncate ..\n");
    //TODO: Return without closing the shared memory file descriptor flose(shm_fd)!!!
    return NI_RETCODE_FAILURE;
  }
#endif

  p_device_queue = (ni_device_queue_t*)mmap(0, sizeof(ni_device_queue_t), PROT_READ | PROT_WRITE,
      MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
    ni_log(NI_LOG_ERROR, "Error %d: mmap ni_device_queue_t ... trouble ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    //TODO: Return without closing the shared memory file descriptor flose(shm_fd)!!!
    return NI_RETCODE_FAILURE;
  }

#ifndef _ANDROID
  close(shm_fd);
#endif

  /*! create the lock */
  lock = open(CODERS_LCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ....\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    //TODO: Return without umappint the shared memory munmap(p_device_queue)!!!
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  /*! create the lock for encoder*/
  locken = open(CODERS_RETRY_ENLCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
  if (locken < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ....\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    //TODO: Return without umappint the shared memory munmap(p_device_queue)!!!
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
  /*! create the lock for decoder*/
  lockde = open(CODERS_RETRY_DELCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
  if (lockde < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ....\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    //TODO: Return without umappint the shared memory munmap(p_device_queue)!!!
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  flock(lock, LOCK_EX);

  // init the ni_device_queue_t
  p_device_queue->decoders_cnt = p_device_queue->encoders_cnt = 0;
  for (i = 0; i < NI_MAX_HW_DECODER_COUNT; i++)
  {
    p_device_queue->decoders[i] = -1;
  }
  for (i = 0; i < NI_MAX_HW_ENCODER_COUNT; i++)
  {
    p_device_queue->encoders[i] = -1;
  }

  for (i = 0; i < num_dev; i++)
  {
    ni_log(NI_LOG_INFO, "%d. %s\n", i, dev_names[i]);

    /*! retrieve decoder and encoder info and create shared memory
       and named lock accordingly, using NVMe "Identify Controller" */
       /*! check whether Xcoder is supported and retrieve the xcoder info */
    sprintf(device_info.dev_name, "/dev/%s", dev_names[i]);
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));
    uint32_t tmp_io_size;

#ifdef XCODER_IO_RW_ENABLED
    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);
#else
    dev_handle = ni_device_open(device_info.dev_name, &tmp_io_size);
#endif
    if (NI_INVALID_DEVICE_HANDLE != dev_handle)
    {
      rc = ni_device_capability_query(dev_handle, &device_capabilites);
      if ((NI_RETCODE_SUCCESS == rc) &&
          (device_capabilites.device_is_xcoder) &&
          (! should_match_rev ||
           (should_match_rev &&
            ni_is_fw_compatible(device_capabilites.fw_rev))))
      {
        fw_ver_compat_warning = 0;
        if (ni_is_fw_compatible(device_capabilites.fw_rev) == 2)
        {
          ni_log(NI_LOG_INFO, "WARNING - Query %s %s FW version: %.*s is below the minimum support version for "
                 "this SW version. Some features may be missing.\n",
                 device_info.dev_name,
                 device_info.blk_name,
                 (int) sizeof(device_capabilites.fw_rev),
                 device_capabilites.fw_rev);
          fw_ver_compat_warning = 1;
        }

        int total_modules = device_capabilites.h264_decoders_cnt + device_capabilites.h264_encoders_cnt +
            device_capabilites.h265_decoders_cnt + device_capabilites.h265_encoders_cnt;

        ni_device_info_t* p_device_info = NULL;

        for (j = 0; j < total_modules; j++)
        {
          p_device_info = NULL;

          memset(&device_info, 0, sizeof(ni_device_info_t));
          sprintf(device_info.dev_name, "/dev/%s", dev_names[i]);
          ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
          device_info.hw_id = device_capabilites.xcoder_devices[j].hw_id;
          device_info.fw_ver_compat_warning = fw_ver_compat_warning;
          memcpy(device_info.fw_rev, device_capabilites.fw_rev,
                 sizeof(device_info.fw_rev));
          memcpy(device_info.fw_commit_hash, device_capabilites.fw_commit_hash,
                 sizeof(device_info.fw_commit_hash) - 1);
          memcpy(device_info.fw_commit_time, device_capabilites.fw_commit_time,
                 sizeof(device_info.fw_commit_time) - 1);
          memcpy(device_info.fw_branch_name, device_capabilites.fw_branch_name,
                 sizeof(device_info.fw_branch_name) - 1);
          device_info.max_fps_1080p = device_capabilites.xcoder_devices[j].max_1080p_fps;
          device_info.max_instance_cnt = device_capabilites.xcoder_devices[j].max_number_of_contexts;
          device_info.device_type = (ni_device_type_t)device_capabilites.xcoder_devices[j].codec_type;

          int device_cnt_so_far = (NI_DEVICE_TYPE_DECODER == device_info.device_type ? p_device_queue->decoders_cnt : p_device_queue->encoders_cnt);
          int tmp_guid = -1;
          // check if entry has been created for this h/w (hw_id):
          // if not, then create a new entry; otherwise just update it
          for (k = 0; k < device_cnt_so_far; k++)
          {
            tmp_guid = (NI_DEVICE_TYPE_DECODER == device_info.device_type ? p_device_queue->decoders[k] : p_device_queue->encoders[k]);
            ni_device_context_t* p_device_context = ni_rsrc_get_device_context(device_info.device_type, tmp_guid);
            if (p_device_context && strcmp(p_device_context->p_device_info->dev_name, device_info.dev_name) == 0 &&
                p_device_context->p_device_info->hw_id == device_info.hw_id)
            {
              p_device_info = p_device_context->p_device_info;
              break;
            }
            ni_rsrc_free_device_context(p_device_context);
          }

          ni_codec_t fmt = (ni_codec_t)device_capabilites.xcoder_devices[j].codec_format;
          int rc;

          if (p_device_info)
          {
            ni_log(NI_LOG_INFO, "%s h/w id %d update\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" : "encoder", device_info.hw_id);
            rc = ni_rsrc_fill_device_info(p_device_info, fmt, device_info.device_type, &device_capabilites.xcoder_devices[j]);
          }
          else
          {
            ni_log(NI_LOG_INFO, "%s h/w id %d create\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" : "encoder", device_info.hw_id);
            p_device_info = &device_info;

            rc = ni_rsrc_fill_device_info(p_device_info, fmt, device_info.device_type, &device_capabilites.xcoder_devices[j]);

            if (rc == 0)
            {
              /*! add the h/w device_info entry */
              if (NI_DEVICE_TYPE_DECODER == device_info.device_type)
              {
                p_device_info->module_id = decoder_guid++;
                p_device_queue->decoders_cnt = decoder_guid;
                p_device_queue->decoders[p_device_info->module_id] = p_device_info->module_id;
              }
              else
              {
                p_device_info->module_id = encoder_guid++;
                p_device_queue->encoders_cnt = encoder_guid;
                p_device_queue->encoders[p_device_info->module_id] = p_device_info->module_id;
              }
              ni_rsrc_get_one_device_info(&device_info);
            }
          }

        } /*! for each device_info module */
      }   /*! if device supports xcoder */
      else
      {
        ni_log(NI_LOG_INFO, "Device %s not added as it is not an xcoder, or has incompatible FW rev: %s\n",
               device_info.dev_name,
               device_capabilites.fw_rev);
      }

      ni_device_close(dev_handle);
    }
  } /*! for each nvme device */

  p_device_queue->decoders_cnt = decoder_guid;
  p_device_queue->encoders_cnt = encoder_guid;

  //ni_log(NI_LOG_INFO, "About to return from ni_rsrc_init()\n");

  flock(lock, LOCK_UN);

  retval = NI_RETCODE_SUCCESS;

  END;
  if (lock >= 0)
  {
    close(lock);
  }
  if (locken >= 0)
  {
    close(locken);
  }
  if (lockde >= 0)
  {
    close(lockde);
  }
  return retval;
}

// return 1 if string key is found in array of strings, 0 otherwise
static int is_str_in_str_array(const char key[],
                               char arr[][MAX_DEVICE_NAME_LEN],
                               int array_size)
{
  int found = 0, i;

  for (i = 0; i < array_size; i++)
  {
    if (0 == strcmp(key, arr[i]))
    {
      found = 1;
      break;
    }
  }
  return found;
}

/*!*****************************************************************************
 *  \brief   Scan and refresh all resources on the host, taking into account
 *           hot-plugged and pulled out cards.
 *
 *  \param[in]   should_match_rev  0: transcoder firmware revision matching the
 *                             library's version is NOT required for placing
 *                             the transcoder into resource pool; 1: otherwise
 *
 *  \return
 *           NI_RETCODE_SUCCESS on success
 *           NI_RETCODE_FAILURE on failure
 *
 ******************************************************************************/
ni_retcode_t ni_rsrc_refresh(int should_match_rev)
{
  static char xcoder_dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  static int xcoder_dev_count = 0;
  char curr_dev_names[MAX_DEVICE_CNT][MAX_DEVICE_NAME_LEN] = {0};
  int curr_dev_count = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int i = 0;
  ni_device_t saved_coders = {0};

  if (0 == xcoder_dev_count)
  {
    // retrieve saved info from resource pool at start up
    if ((NI_RETCODE_SUCCESS == ni_rsrc_list_devices(NI_DEVICE_TYPE_DECODER, saved_coders.decoders,&(saved_coders.decoders_cnt))) &&
        (NI_RETCODE_SUCCESS == ni_rsrc_list_devices(NI_DEVICE_TYPE_ENCODER, saved_coders.encoders,&(saved_coders.encoders_cnt))))
    {
      // check if the decoder count equals to the encoder count
      if (saved_coders.decoders_cnt == saved_coders.encoders_cnt)
      {
        xcoder_dev_count = saved_coders.decoders_cnt;
        ni_log(NI_LOG_INFO, "%d devices retrieved from current pool at start up\n",
               xcoder_dev_count);
      }
      else
      {
        retval = NI_RETCODE_FAILURE;
        ni_log(NI_LOG_ERROR, "Error decoder count %d doesn't equal to encoder count %d\n",
               saved_coders.decoders_cnt, saved_coders.encoders_cnt);
        LRETURN;
      }

      // check the device name
      for (i = 0; i < saved_coders.decoders_cnt; i++)
      {
        if (0 == strcmp(saved_coders.decoders[i].dev_name, saved_coders.encoders[i].dev_name))
        {
          xcoder_dev_names[i][0] = '\0';
          strncat(xcoder_dev_names[i], saved_coders.decoders[i].dev_name, strlen(saved_coders.decoders[i].dev_name));
        }
        else
        {
          retval = NI_RETCODE_FAILURE;
          ni_log(NI_LOG_ERROR, "Error the names of the encoding(%s) and decoding(%s) devices don't match, Index:%d\n",
                 saved_coders.decoders[i].dev_name, saved_coders.encoders[i].dev_name, i);
          LRETURN;
        }
      }
    }
    else
    {
      retval = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_INFO, "Error retrieving from current pool at start up\n");
      LRETURN;
    }
  }

  curr_dev_count = ni_rsrc_get_local_device_list(curr_dev_names,
                                                 MAX_DEVICE_CNT);
  // remove from resource pool any device that is not available now
  for (i = 0; i < xcoder_dev_count; i++)
  {
    if (! is_str_in_str_array(xcoder_dev_names[i], curr_dev_names,
                              curr_dev_count))
    {
      ni_log(NI_LOG_INFO, "\n\n%d. %s NOT in current scanned list, removing !\n", i,
             xcoder_dev_names[i]);
      if (NI_RETCODE_SUCCESS ==
          ni_rsrc_remove_device(xcoder_dev_names[i]))
      {
        ni_log(NI_LOG_INFO, "%s deleted successfully !\n", xcoder_dev_names[i]);
      }
      else
      {
        ni_log(NI_LOG_INFO, "%s failed to delete !\n", xcoder_dev_names[i]);
      }
    }

  }

  // and add into resource pool any newly discoved ones
  for (i = 0; i < curr_dev_count; i++)
  {
    if (! is_str_in_str_array(curr_dev_names[i], xcoder_dev_names,
                              xcoder_dev_count))
    {
      ni_log(NI_LOG_INFO, "\n\n%s NOT in previous list, adding !\n",curr_dev_names[i]);
      if (NI_RETCODE_SUCCESS == ni_rsrc_add_device(curr_dev_names[i],
                                                   should_match_rev))
      {
        ni_log(NI_LOG_INFO, "%s refresh added successfully !\n", curr_dev_names[i]);
      }
      else
      {
        ni_log(NI_LOG_INFO, "%s failed to add !\n", curr_dev_names[i]);
      }
    }
  }

  // update the saved device name list
  for (i = 0; i < curr_dev_count; i++)
  {
    xcoder_dev_names[i][0] = '\0';
    strncat(xcoder_dev_names[i], curr_dev_names[i], strlen(curr_dev_names[i]));
  }
  xcoder_dev_count = curr_dev_count;

  END;

  return retval;
}

/*!******************************************************************************
* \brief      Allocates and returns a pointer to ni_device_context_t struct
*             based on provided device_type and guid.
*             To be used for load update and codec query.
*
 *  \param[in]  device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
 *  \param[in]  guid         GUID of the encoder or decoder device
*
* \return     pointer to ni_device_context_t if found, NULL otherwise
*
*  Note:     The returned ni_device_context_t content is not supposed to be used by
*            caller directly: should only be passed to API in the subsequent
*            calls; also after its use, the context should be released by
*            calling ni_rsrc_free_device_context.
*******************************************************************************/
ni_device_context_t* ni_rsrc_get_device_context(ni_device_type_t device_type, int guid)
{
    /*! get names of shared mem and lock by GUID */
  int shm_fd;
  int lock;
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  ni_device_context_t *p_device_context = NULL;
  ni_device_info_t *p_device_queue = NULL;

  ni_rsrc_get_shm_name(device_type, guid, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(device_type, guid, lck_name, sizeof(lck_name));

#ifdef _ANDROID
  lock = open(lck_name, O_CREAT | O_RDWR | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
#else
  lock = open(lck_name, O_RDWR | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ..... %s %d \n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, lck_name, guid);
    return NULL;
  }

  flock(lock, LOCK_EX);

#ifdef _ANDROID
  int ret = ni_rsrc_android_init();

  String8 param;
  param.setTo(shm_name, STR_BUFFER_LEN);
  if (service == NULL)
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_context Error service ..\n");
    LRETURN;
  }
  shm_fd = service->getFd(param);
  if (shm_fd < 0)
  {
    shm_fd = ashmem_create_region(shm_name, sizeof(ni_device_info_t));
    if (shm_fd >= 0)
    {
      service->setFd(param, shm_fd);
    }
  }
#else
  shm_fd = shm_open(shm_name, O_RDWR, 0777);  //just open the share memory, not create
#endif

  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: shm_open ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    LRETURN;
  }

  p_device_queue = (ni_device_info_t *)mmap(0, sizeof(ni_device_info_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
    ni_log(NI_LOG_ERROR, "Error %d: mmap ni_rsrc_get_device_context ...\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    LRETURN;
  }

  p_device_context = (ni_device_context_t *)malloc(sizeof(ni_device_context_t));
  if (!p_device_context)
  {
    ni_log(NI_LOG_ERROR, "Error %d: malloc ..\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    munmap((void *)p_device_queue, sizeof(ni_device_info_t));
    
    LRETURN;
  }

  strncpy(p_device_context->shm_name, shm_name, sizeof(shm_name));
  p_device_context->lock = lock;
  p_device_context->p_device_info = p_device_queue;

  END;

  flock(lock, LOCK_UN);
  
#ifndef _ANDROID
  close(shm_fd);
#endif

  return p_device_context;
}
#endif

/*!******************************************************************************
 *  \brief    Free previously allocated device context
 *
 *  \param    p_device_context Pointer to previously allocated device context
 *
 *  \return   None
 *******************************************************************************/
void ni_rsrc_free_device_context(ni_device_context_t *p_device_context)
{
  if (p_device_context)
  {
#ifdef _WIN32
    UnmapViewOfFile(p_device_context->p_device_info);
    CloseHandle(p_device_context->lock);
#elif __linux__
    close(p_device_context->lock);
    munmap((void *)p_device_context->p_device_info, sizeof(ni_device_info_t));
#endif
    free(p_device_context);
    p_device_context = NULL;
  }
}

/*!******************************************************************************
*  \brief        List device(s) based on device type with full information
*                including s/w instances on the system.
*
*   \param[in]   device_type     NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[out]  p_device        The device information returned.
*   \param[out]  p_device_count  The number of ni_device_info_t structs returned.
*
*   \return
*                NI_RETCODE_SUCCESS
*                NI_RETCODE_FAILURE
*
*   Note: Caller is responsible for allocating memory for "p_device".
*******************************************************************************/
ni_retcode_t ni_rsrc_list_devices(ni_device_type_t device_type,
               ni_device_info_t *p_device_info, int * p_device_count)
{
  int i, count;
  ni_device_queue_t *p_device_queue = NULL;
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_context_t *p_device_context = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  bool b_release_pool_mtx = false;

  if ( (NULL == p_device_info) || (NULL == p_device_count) )
  {
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  p_device_pool = ni_rsrc_get_device_pool();
  if (NULL == p_device_pool)
  {
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_list_devices() failed to obtain mutex: %p\n", p_device_pool->lock);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
#elif __linux__
  flock(p_device_pool->lock, LOCK_EX);
#endif

  b_release_pool_mtx = true;

  p_device_queue = p_device_pool->p_device_queue;
  if (NI_DEVICE_TYPE_DECODER == device_type)
  {
    count = p_device_queue->decoders_cnt;
  }
  else
  {
    count = p_device_queue->encoders_cnt;
  }
  *p_device_count=0;
  for (i = 0; i < count; i++)
  {
    int guid = -1;
    if (NI_DEVICE_TYPE_DECODER == device_type)
    {
      guid = p_device_queue->decoders[i];
    }
    else
    {
      guid = p_device_queue->encoders[i];
    }

    p_device_context = ni_rsrc_get_device_context(device_type, guid);
    if (p_device_context)
    {
#ifdef _WIN32
      if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
      {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_list_devices() failed to obtain mutex: %p\n", p_device_context->lock);
        ReleaseMutex(p_device_pool->lock);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }

      memcpy(&p_device_info[i], p_device_context->p_device_info, sizeof(ni_device_info_t));
      ReleaseMutex(p_device_context->lock);
#elif __linux__
      flock(p_device_context->lock, LOCK_EX);
      memcpy(&p_device_info[i], p_device_context->p_device_info, sizeof(ni_device_info_t));
      flock(p_device_context->lock, LOCK_UN);
#endif

      ni_rsrc_free_device_context(p_device_context);

      (*p_device_count)++;
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error find decoder guid: %d\n", guid);
      retval = NI_RETCODE_FAILURE;
      LRETURN;
    }
  }

  END;

  if (b_release_pool_mtx)
  {
#ifdef _WIN32
      ReleaseMutex(p_device_pool->lock);
#elif __linux__
      flock(p_device_pool->lock, LOCK_UN);
#endif
  }

  ni_rsrc_free_device_pool(p_device_pool);

  return retval;
}

/*!******************************************************************************
*  \brief        List all devices with full information including s/w instances
*                on the system.

*   \param[out]  p_device  The device information returned.
*
*   \return
*                NI_RETCODE_SUCCESS
*                NI_RETCODE_INVALID_PARAM
*                NI_RETCODE_FAILURE
*
*   Note: Caller is responsible for allocating memory for "p_device".
*******************************************************************************/
ni_retcode_t ni_rsrc_list_all_devices(ni_device_t *p_device)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if (NULL == p_device)
  {
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  retval = ni_rsrc_list_devices(NI_DEVICE_TYPE_DECODER, p_device->decoders, &(p_device->decoders_cnt));
  if ( NI_RETCODE_FAILURE == retval )
  {
    ni_log(NI_LOG_INFO, "Error retrieving decoders\n");
  }

  retval = ni_rsrc_list_devices(NI_DEVICE_TYPE_ENCODER, p_device->encoders, &(p_device->encoders_cnt));
  if (NI_RETCODE_FAILURE == retval)
  {
    ni_log(NI_LOG_INFO, "Error retrieving encoders\n");
  }

  END;

  return retval;
}

void ni_rsrc_print_device_info(const ni_device_info_t *p_device_info)
{
  int32_t i;

  if(!p_device_info)
  {
    ni_log(NI_LOG_INFO, "ERROR: Cannot print device info!\n");
  }
  else
  {
    ni_log(NI_LOG_INFO, "%s #%d\n", p_device_info->device_type == NI_DEVICE_TYPE_DECODER ? "Decoder" : "Encoder", p_device_info->module_id);
    ni_log(NI_LOG_INFO, "  DeviceID: %s\n", p_device_info->dev_name);
    ni_log(NI_LOG_INFO, "  BlockID: %s\n", p_device_info->blk_name);
    ni_log(NI_LOG_INFO, "  H/W ID: %d\n", p_device_info->hw_id);
    ni_log(NI_LOG_INFO, "  F/W rev: %2.*s\n", (int)sizeof(p_device_info->fw_rev), p_device_info->fw_rev);
    ni_log(NI_LOG_INFO, "  F/W & S/W compatibility: %s\n", p_device_info->fw_ver_compat_warning ? "no, possible missing features" : "yes");
    ni_log(NI_LOG_INFO, "  F/W branch: %s\n", p_device_info->fw_branch_name);
    ni_log(NI_LOG_INFO, "  F/W commit hash: %s\n", p_device_info->fw_commit_hash);
    ni_log(NI_LOG_INFO, "  F/W commit time: %s\n", p_device_info->fw_commit_time);
    ni_log(NI_LOG_INFO, "  MaxNumInstances: %d\n", p_device_info->max_instance_cnt);
    ni_log(NI_LOG_INFO, "  ActiveNumInstances: %d\n", p_device_info->active_num_inst);
    ni_log(NI_LOG_INFO, "  Max1080pFps: %d\n", p_device_info->max_fps_1080p);
    ni_log(NI_LOG_INFO, "  CurrentLoad: %d\n", p_device_info->load);
    ni_log(NI_LOG_INFO, "  H.264Capabilities:\n");
    ni_log(NI_LOG_INFO, "    Supported: %s\n", p_device_info->supports_h264 ? "yes" : "no");
    ni_log(NI_LOG_INFO, "    MaxResolution: %dx%d\n", p_device_info->h264_cap.max_res_width,
           5120); // TODO: Change this value to HW capability reported by FW once HW capabilities repoting format has been revamped
          //  p_device_info->h264_cap.max_res_height);
    ni_log(NI_LOG_INFO, "    MinResolution: %dx%d\n", p_device_info->h264_cap.min_res_width,
           p_device_info->h264_cap.min_res_height);
    ni_log(NI_LOG_INFO, "    Profiles: %s\n", p_device_info->h264_cap.profiles_supported);
    ni_log(NI_LOG_INFO, "    level: %s\n", p_device_info->h264_cap.level);
    ni_log(NI_LOG_INFO, "    additional info: %s\n", p_device_info->h264_cap.additional_info);
    ni_log(NI_LOG_INFO, "  H.265Capabilities:\n");
    ni_log(NI_LOG_INFO, "    Supported: %s\n", p_device_info->supports_h265 ? "yes" : "no");
    ni_log(NI_LOG_INFO, "    MaxResolution: %dx%d\n", p_device_info->h265_cap.max_res_width,
           5120); // TODO: Change this value to HW capability reported by FW once HW capabilities repoting format has been revamped
          //  p_device_info->h265_cap.max_res_height);
    ni_log(NI_LOG_INFO, "    MinResolution: %dx%d\n", p_device_info->h265_cap.min_res_width,
           p_device_info->h265_cap.min_res_height);
    ni_log(NI_LOG_INFO, "    Profiles: %s\n", p_device_info->h265_cap.profiles_supported);
    ni_log(NI_LOG_INFO, "    level: %s\n", p_device_info->h265_cap.level);
    ni_log(NI_LOG_INFO, "    additional info: %s\n", p_device_info->h265_cap.additional_info);

    ni_log(NI_LOG_INFO, "  num. s/w instances: %d\n", p_device_info->active_num_inst);
    for (i = 0; i < p_device_info->active_num_inst; i++) {
      ni_log(NI_LOG_INFO, "      [id]: %d\n", p_device_info->sw_instance[i].id);
      ni_log(NI_LOG_INFO, "      status: %s\n", p_device_info->sw_instance[i].status == EN_IDLE ? "Idle" : "Active");
      ni_log(NI_LOG_INFO, "      codec: %s\n", p_device_info->sw_instance[i].codec == EN_H264 ? "H.264" : "H.265");
      ni_log(NI_LOG_INFO, "      width:  %d\n", p_device_info->sw_instance[i].width);
      ni_log(NI_LOG_INFO, "      height: %d\n", p_device_info->sw_instance[i].height);
      ni_log(NI_LOG_INFO, "      fps:    %d\n", p_device_info->sw_instance[i].fps);
    }
    ni_log(NI_LOG_INFO, "\n");
  }
}

/*!*****************************************************************************
*  \brief        Print detailed capability information of all devices
*                on the system.

*   \param       none
*
*   \return      none
*
*******************************************************************************/
LIB_API void ni_rsrc_print_all_devices_capability(void)
{
  int32_t i = 0;
  ni_device_t* p_xcoders = NULL;

  p_xcoders = (ni_device_t *) malloc(sizeof(ni_device_t));

  if (NULL != p_xcoders)
  {
    if (NI_RETCODE_SUCCESS == ni_rsrc_list_all_devices(p_xcoders))
    {
      /*! print out coders in the order based on their guid */
      ni_log(NI_LOG_INFO, "Num decoders: %d\n", p_xcoders->decoders_cnt);

      for (i = 0; i < p_xcoders->decoders_cnt; i++) {
        ni_rsrc_print_device_info(&(p_xcoders->decoders[i]));
      }
      ni_log(NI_LOG_INFO, "Num encoders: %d\n", p_xcoders->encoders_cnt);
      for (i = 0; i < p_xcoders->encoders_cnt; i++) {
        ni_rsrc_print_device_info(&(p_xcoders->encoders[i]));
      }
    }
  }
  else
  {
    ni_log(NI_LOG_ERROR, "Error %d: malloc for ni_rsrc_print_all_devices_capability()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
  }
}

/*!******************************************************************************
*  \brief        Query a specific device with detailed information on the system

*   \param[in]   device_type     NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]   guid            unique device(decoder or encoder) id
*
*   \return
*                pointer to ni_device_info_t if found
*                NULL otherwise
*
*   Note: Caller is responsible for releasing memory that was allocated for the
*         returned pointer
*******************************************************************************/
ni_device_info_t *ni_rsrc_get_device_info(ni_device_type_t device_type, int guid)
{
  ni_device_info_t *p_device_info = NULL;
  ni_device_context_t* p_device_context = NULL;

  p_device_context = ni_rsrc_get_device_context(device_type, guid);
  if (NULL == p_device_context)
  {
    LRETURN;
  }

  p_device_info = (ni_device_info_t *)malloc(sizeof(ni_device_info_t));
  if (NULL == p_device_info)
  {
    ni_log(NI_LOG_ERROR, "Error %d: malloc for ni_rsrc_get_device_info()", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    LRETURN;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_get_device_info() failed to obtain mutex: %p\n", p_device_context->lock);
    free(p_device_info);
    LRETURN;
  }

  memcpy(p_device_info, p_device_context->p_device_info, sizeof(ni_device_info_t));
  ReleaseMutex(p_device_context->lock);
#elif __linux
  flock(p_device_context->lock, LOCK_EX);

  memcpy(p_device_info, p_device_context->p_device_info, sizeof(ni_device_info_t));

  flock(p_device_context->lock, LOCK_UN);
#endif

  END;

  ni_rsrc_free_device_context(p_device_context);

  return p_device_info;
}

/*!****************************************************************************

* \brief      Get the least used device that can handle decoding or encoding
*             a video stream of certain resolution/frame-rate/codec.
*
* \param[in]  width      width of video resolution
* \param[in]  height     height of video resolution
* \param[in]  frame_rate video stream frame rate
* \param[in]  codec      EN_H264 or EN_H265
* \param[in]  type       NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
* \param[out] info       detailed device information. If is non-NULL, the
*                        device info is stored in the memory pointed to by it.
*
* \return     device GUID (>= 0) if found , -1 otherwise
*******************************************************************************/
int ni_rsrc_get_available_device(int width, int height, int frame_rate,
                     ni_codec_t codec, ni_device_type_t device_type,
                     ni_device_info_t *p_device_info)
{
  int i, rc;
  int guid = -1;
  int load = 0;
  int num_sw_instances = 0;
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_info_t *p_dev_info = NULL;
  ni_device_context_t *p_device_context = NULL;
  ni_session_context_t p_session_context = {0};
  ni_device_info_t dev_info = { 0 };
  int num_coders = 0;
  int least_model_load = 0;

  // query all the coders of device_type and update their status, and find the
  // device_info with the least load and least number of instances.
  p_device_pool = ni_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    ni_log(NI_LOG_INFO, "Error get Coders p_device_info ..\n");
    return NI_RETCODE_FAILURE;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_get_available_device() failed to obtain mutex: %p\n", p_device_pool->lock);
    ni_rsrc_free_device_pool(p_device_pool);
    return NI_RETCODE_FAILURE;
  }
#elif __linux__
  flock(p_device_pool->lock, LOCK_EX);
#endif

  if (NI_DEVICE_TYPE_DECODER == device_type)
  {
    num_coders = p_device_pool->p_device_queue->decoders_cnt;
  }
  else
  {
    num_coders = p_device_pool->p_device_queue->encoders_cnt;
  }

  int tmp_id = -1;
  for (i = 0; i < num_coders; i++)
  {
    if (NI_DEVICE_TYPE_DECODER == device_type)
    {
      tmp_id = p_device_pool->p_device_queue->decoders[i];
    } else
    {
      tmp_id = p_device_pool->p_device_queue->encoders[i];
    }

    p_device_context = ni_rsrc_get_device_context(device_type, tmp_id);
#ifdef XCODER_IO_RW_ENABLED
    p_session_context.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name, &p_session_context.max_nvme_io_size);
    p_session_context.device_handle = p_session_context.blk_io_handle;
#else
    p_session_context.device_handle = ni_device_open(p_device_context->p_device_info->dev_name, &p_session_context.max_nvme_io_size);
#endif
    if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
    {
      ni_rsrc_free_device_context(p_device_context);
      ni_log(NI_LOG_INFO, "Error open device %s %s\n", p_device_context->p_device_info->dev_name, p_device_context->p_device_info->blk_name);
      ni_log(NI_LOG_ERROR, "Error open device");
      continue;
    }

    p_session_context.hw_id = p_device_context->p_device_info->hw_id;

#ifdef _WIN32
    p_session_context.event_handle = ni_create_event();
    if (NI_INVALID_EVENT_HANDLE == p_session_context.event_handle)
    {
      ni_rsrc_free_device_context(p_device_context);
      ni_device_close(p_session_context.device_handle);
      ni_log(NI_LOG_ERROR, "Error create envet");
      continue;
    }
#endif

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
      ni_rsrc_free_device_context(p_device_context);
      ni_log(NI_LOG_INFO, "Error query %s %s %s.%d\n",
             NI_DEVICE_TYPE_DECODER == device_type ? "decoder" : "encoder",
             p_device_context->p_device_info->dev_name,
             p_device_context->p_device_info->blk_name,
             p_device_context->p_device_info->hw_id);
      continue;
    }

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_get_available_device() failed to obtain mutex: %p\n", p_device_context->lock);
        //ni_rsrc_free_device_pool(p_device_pool);
        //return -1;
    }
#elif __linux__
    flock(p_device_context->lock, LOCK_EX);
#endif
    ni_rsrc_update_record(p_device_context, &p_session_context);

    p_dev_info = p_device_context->p_device_info;

    if (i == 0 || p_dev_info->model_load < least_model_load ||
        (p_dev_info->model_load == least_model_load &&
         p_dev_info->active_num_inst < num_sw_instances))
    {
      guid = tmp_id;
      least_model_load = p_dev_info->model_load;
      num_sw_instances = p_dev_info->active_num_inst;
      memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
    }


#ifdef _WIN32
    ReleaseMutex(p_device_context->lock);
#elif __linux__
    flock(p_device_context->lock, LOCK_UN);
#endif
    ni_rsrc_free_device_context(p_device_context);
  }

  if (guid >= 0)
  {
    // calculate the load this stream will generate based on its resolution and
    // fps required
    p_device_context = ni_rsrc_get_device_context(device_type, guid);
    ni_rsrc_device_video_ref_cap_t refCap = g_device_reference_table[device_type][codec];
    if (refCap.fps == 0)
    {
      guid = -1;
    }
    else if ((device_type == NI_DEVICE_TYPE_ENCODER) || (device_type == NI_DEVICE_TYPE_DECODER))
    {
      unsigned long total_cap = refCap.width * refCap.height * refCap.fps;
      unsigned long xcode_cap = width * height * frame_rate;
      if (xcode_cap + p_device_context->p_device_info->xcode_load_pixel > total_cap)
      {
        ni_log(NI_LOG_INFO, "Warning xcode cap: %ld (%.1f) + current load %ld (%.1f) "
               "> total %ld (1) ..\n",
               xcode_cap, (float)xcode_cap / total_cap,
               p_device_context->p_device_info->xcode_load_pixel,
               (float)p_device_context->p_device_info->xcode_load_pixel / total_cap, total_cap);
        guid = -1;
      }
    }
    else
    {
      float fRef = (float)refCap.width * refCap.height * refCap.fps;
      float fLoad = (float)width * height * frame_rate * 100;
      if ((fLoad / fRef) > (100.0 - (float)load))
      {
        guid = -1;
      }
    }
  }

#ifdef _WIN32
  ReleaseMutex(p_device_pool->lock);
#elif __linux__
  flock(p_device_pool->lock, LOCK_UN);
#endif

  ni_rsrc_free_device_pool(p_device_pool);

  ni_log(NI_LOG_INFO, "Get %s for %dx%d fps %d : %d %s.%d\n",
         NI_DEVICE_TYPE_DECODER == device_type ? "decoder" : "encoder", width, height, frame_rate,
         guid, guid == -1 ? "NULL" : dev_info.dev_name,
         guid == -1 ? -1 : dev_info.hw_id);

  //Copy to user only if the pointer is not NULL
  if ( (p_device_info) && (guid >= 0) )
  {
    memcpy(p_device_info, &dev_info, sizeof(ni_device_info_t));
  }

  return guid;
}

/*!*****************************************************************************
*   \brief Update the load value and s/w instances info of a specific decoder or
*    encoder. This is used by resource management daemon to update periodically.
*
*   \param[in]  p_ctxt           The device context returned by ni_rsrc_get_device_context
*   \param[in]  p_load           The latest load value to update
*   \param[in]  sw_instance_cnt  Number of s/w instances
*   \param[in]  sw_instance_info Info of s/w instances
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_rsrc_update_device_load(ni_device_context_t *p_device_context, int load,
               int sw_instance_cnt, const ni_sw_instance_info_t sw_instance_info[])
{
  int i;
  if (!p_device_context || !sw_instance_info)
  {
    ni_log(NI_LOG_INFO, "Error in resource update device load: invalid input pointers  ..\n");
    return NI_RETCODE_FAILURE;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
      ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_update_device_load() failed to obtain mutex: %p\n", p_device_context->lock);
      return NI_RETCODE_FAILURE;
  }
#elif __linux__
  flock(p_device_context->lock, LOCK_EX);
#endif

  p_device_context->p_device_info->load = load;
  p_device_context->p_device_info->active_num_inst = sw_instance_cnt;
  for (i = 0; i < sw_instance_cnt; i++)
  {
    p_device_context->p_device_info->sw_instance[i] = sw_instance_info[i];
  }

#ifdef _WIN32
  ReleaseMutex(p_device_context->lock);
#elif __linux__
  flock(p_device_context->lock, LOCK_UN);
#endif

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
/*! this function must be called inside the protected region by p_device_pool
   if query, checking, allocation and update should be done atomically */
static void ni_rsrc_move_device_to_end_of_pool(ni_device_type_t type, int guid,
                           ni_device_pool_t *p_device_pool)
{
  int *coders;
  int i, count;

  ni_log(NI_LOG_INFO, "Moving %s %d to queue end ..\n", type == NI_DEVICE_TYPE_DECODER ? "decoder" : "encoder", guid);

  coders = (type == NI_DEVICE_TYPE_DECODER ? p_device_pool->p_device_queue->decoders : p_device_pool->p_device_queue->encoders);

  count = (type == NI_DEVICE_TYPE_DECODER ? p_device_pool->p_device_queue->decoders_cnt : p_device_pool->p_device_queue->encoders_cnt);

  i = 0;
  while (i < count)
  {
    if (coders[i] == guid)
    {
      ni_log(NI_LOG_INFO, "Found id %d at pos: %d\n", guid, i);
      break;
    }
    else
    {
      i++;
    }
  }
  if (i < count)
  {
    /*! move all the coders after i forward one position and put i at the
       end */
    while (i < count - 1)
    {
      coders[i] = coders[i + 1];
      i++;
    }
    coders[count - 1] = guid;
  }
}

/*!*****************************************************************************
*   \brief      Allocate resources for decoding/encoding, based on the provided rule
*
*   \param[in]  type      NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]  rule      allocation rule
*   \param[in]  codec     EN_H264 or EN_H265
*   \param[in]  width     width of video resolution
*   \param[in]  height    height of video resolution
*   \param[in]  frame_rate video stream frame rate
*   \param[out] p_load      the p_load that will be generated by this encoding
*                       task. Returned *only* for encoder for now.
*
*   \return     pointer to ni_device_context_t if found, NULL otherwise
*
*   Note:  codec, width, height, fps need to be supplied for NI_DEVICE_TYPE_ENCODER only,
*          they are ignored otherwize.
*   Note:  the returned ni_device_context_t content is not supposed to be used by
*          caller directly: should only be passed to API in the subsequent
*          calls; also after its use, the context should be released by
*          calling ni_rsrc_free_device_context.
*******************************************************************************/
ni_device_context_t *ni_rsrc_allocate_auto
(
    ni_device_type_t device_type,
    ni_alloc_rule_t rule,
    ni_codec_t codec,
    int width, int height,
    int frame_rate,
    unsigned long* p_load
)
{
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_info_t *p_device_info = NULL;
  ni_device_context_t *p_device_context = NULL;
  ni_session_context_t p_session_context = { 0 };
  int *coders = NULL;
  int i, count, rc;
  int guid = -1;
  int load = 0;
  int num_sw_instances = 0;
  int least_model_load = 0;

  /*! retrieve the record and based on the allocation rule specified, find the
     least loaded or least number of s/w instances among the coders */
  p_device_pool = ni_rsrc_get_device_pool();
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_allocate_auto() failed to obtain mutex: %p\n", p_device_pool->lock);
    }
#elif __linux__
    flock(p_device_pool->lock, LOCK_EX);
#endif

    coders = ( NI_DEVICE_TYPE_DECODER == device_type ? p_device_pool->p_device_queue->decoders : p_device_pool->p_device_queue->encoders);

    count = ( NI_DEVICE_TYPE_DECODER == device_type ? p_device_pool->p_device_queue->decoders_cnt : p_device_pool->p_device_queue->encoders_cnt);

    i = 0;
    while (i < count)
    {
      /*! get the individual device_info info and check the load/num-of-instances */
      p_device_context = ni_rsrc_get_device_context(device_type, coders[i]);

      // p_first retrieve status from f/w and update storage  
#ifdef XCODER_IO_RW_ENABLED
      p_session_context.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name, &p_session_context.max_nvme_io_size);
      p_session_context.device_handle = p_session_context.blk_io_handle;
#else
      p_session_context.device_handle = ni_device_open(p_device_context->p_device_info->dev_name, &p_session_context.max_nvme_io_size);
#endif
      if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
      {
        ni_log(NI_LOG_INFO, "Error open device %s %s\n",
            p_device_context->p_device_info->dev_name,
            p_device_context->p_device_info->blk_name);
        ni_log(NI_LOG_ERROR, "Error open device");
        ni_rsrc_free_device_context(p_device_context);
        i++;
        continue;
      }

      p_session_context.hw_id = p_device_context->p_device_info->hw_id;

#ifdef _WIN32
      p_session_context.event_handle = ni_create_event();
      if (NI_INVALID_EVENT_HANDLE != p_session_context.event_handle)
      {
        ni_rsrc_free_device_context(p_device_context);
        ni_device_close(p_session_context.device_handle);
        ni_log(NI_LOG_ERROR, "Error create envet");
        continue;
      }
#endif

      rc = ni_device_session_query(&p_session_context, device_type);

      ni_device_close(p_session_context.device_handle);
#ifdef _WIN32
      ni_close_event(p_session_context.event_handle);
#endif

      if (NI_RETCODE_SUCCESS != rc)
      {
        ni_log(NI_LOG_INFO, "Error query %s %s %s.%d\n",
               NI_DEVICE_TYPE_DECODER == device_type ? "decoder" : "encoder",
               p_device_context->p_device_info->dev_name,
               p_device_context->p_device_info->blk_name,
               p_device_context->p_device_info->hw_id);
        ni_rsrc_free_device_context(p_device_context);
        i++;
        continue;
      }

#ifdef _WIN32
      if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
      {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_allocate_auto() failed to obtain mutex: %p\n", p_device_context->lock);
      }
#elif __linux__
      flock(p_device_context->lock, LOCK_EX);
#endif
      ni_rsrc_update_record(p_device_context, &p_session_context);

      p_device_info = p_device_context->p_device_info;
      if (i == 0)
      {
        guid = coders[i];
        load = p_device_info->load;
        least_model_load = p_device_info->model_load;
        num_sw_instances = p_device_info->active_num_inst;
      }

      ni_log(NI_LOG_INFO, "Coder [%d]: %d , load: %d (%d), activ_inst: %d , max_inst %d\n",
             i, coders[i], p_device_info->load, p_device_info->model_load, p_device_info->active_num_inst,
          p_device_info->max_instance_cnt);

      switch (rule)
      {
        case EN_ALLOC_LEAST_INSTANCE:
        {
          if (p_device_info->active_num_inst < num_sw_instances)
          {
            guid = coders[i];
            num_sw_instances = p_device_info->active_num_inst;
          }
          break;
        }

        case EN_ALLOC_LEAST_LOAD:
        default:
        {
          if (NI_DEVICE_TYPE_ENCODER == device_type)
          {
            if (p_device_info->model_load < least_model_load)
            {
              guid = coders[i];
              least_model_load = p_device_info->model_load;
            }
          }
          else if (p_device_info->load < load)
          {
            guid = coders[i];
            load = p_device_info->load;
          }
          break;
        }
      }

#ifdef _WIN32
      ReleaseMutex(p_device_context->lock);
#elif __linux__
      flock(p_device_context->lock, LOCK_UN);
#endif
      ni_rsrc_free_device_context(p_device_context);
      i++;
    }

    if (guid >= 0)
    {
      p_device_context = ni_rsrc_get_device_context(device_type, guid);
      if (NI_DEVICE_TYPE_ENCODER == device_type)
      {
        ni_rsrc_device_video_ref_cap_t refCap = g_device_reference_table[device_type][codec];
        unsigned long total_cap = refCap.width * refCap.height * refCap.fps;
        if (total_cap == 0)
        {
          ni_log(NI_LOG_INFO, "Capacity is 0, guid %d ..\n", guid);
          p_device_context = NULL;
          LRETURN;
        }
        else
        {
          *p_load = width * height * frame_rate;

#ifdef _WIN32
          if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
          {
              ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_allocate_auto() failed to obtain mutex: %p\n", p_device_context->lock);
          }
#elif __linux__
          flock(p_device_context->lock, LOCK_EX);
#endif
          p_device_context->p_device_info->xcode_load_pixel += *p_load;
          // Remove as the value is getting from the FW
          //p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#ifdef _WIN32
          ReleaseMutex(p_device_context->lock);
#elif __linux__
          if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t),
                    MS_SYNC | MS_INVALIDATE))
          {
            ni_log(NI_LOG_ERROR, "ni_rsrc_allocate_auto msync");
          }
          flock(p_device_context->lock, LOCK_UN);
#endif
        }
      }

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error find guid ..\n");
      p_device_context = NULL;
    }

  END;
#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__
    flock(p_device_pool->lock, LOCK_UN);
#endif
    ni_rsrc_free_device_pool(p_device_pool);
  }
  return p_device_context;
}

/*!*****************************************************************************
*   \brief      Allocate resources for decoding/encoding, by designating explicitly
*               the device to use.
*
*   \param[in]  device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]  guid         unique device (decoder or encoder) module id
*   \param[in]  codec        EN_H264 or EN_H265
*   \param[in]  width        width of video resolution
*   \param[in]  height       height of video resolution
*   \param[in]  frame_rate   video stream frame rate
*   \param[out] p_load       the load that will be generated by this encoding
*                          task. Returned *only* for encoder for now.
*
*   \return     pointer to ni_device_context_t if found, NULL otherwise
*
*   Note:  codec, width, height, fps need to be supplied by encoder; they
*          are ignored for decoder.
*
*   Note:  the returned ni_device_context_t content is not supposed to be used by
*          caller directly: should only be passed to API in the subsequent
*          calls; also after its use, the context should be released by
*          calling ni_rsrc_free_device_context.
*******************************************************************************/
ni_device_context_t *ni_rsrc_allocate_direct
(
  ni_device_type_t device_type,
  int guid,
  ni_codec_t codec,
  int width,
  int height,
  int frame_rate,
  unsigned long* p_load
)
{
  ni_device_pool_t *p_device_pool = NULL;

  ni_device_context_t *p_device_context = ni_rsrc_get_device_context(device_type, guid);
  if (p_device_context)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_allocate_direct() failed to obtain mutex: %p\n", p_device_context->lock);
    }
#elif __linux__
    flock(p_device_context->lock, LOCK_EX);
#endif

    /*! call libxcoder to allocate a s/w instance on the specified
       h/w device_info; this might not be necessary as the actual allocation of
       s/w instance happens when decoding/encoding starts in the codec */
    /*! update modelled load for encoder */
    if (NI_DEVICE_TYPE_ENCODER == device_type)
    {
      ni_rsrc_device_video_ref_cap_t refCap = g_device_reference_table[device_type][codec];
      unsigned long total_cap = refCap.width * refCap.height * refCap.fps;
      if (total_cap > 0)
      {
        *p_load = width * height * frame_rate;
        p_device_context->p_device_info->xcode_load_pixel += *p_load;
        // Remove as the value is getting from the FW
        //p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#ifdef __linux__
        if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t),
                  MS_SYNC | MS_INVALIDATE))
        {
          ni_log(NI_LOG_ERROR, "ni_rsrc_allocate_direct msync");
        }
#endif
      }
    }

#ifdef _WIN32
    ReleaseMutex(p_device_context->lock);
#elif __linux__
    flock(p_device_context->lock, LOCK_UN);
#endif

    /*! then move this device_info to the end of the device_info queue so that it will
       be selected p_last in the auto-allocate selection process, for load
       balancing */
    p_device_pool = ni_rsrc_get_device_pool();

    if (p_device_pool)
    {
#ifdef _WIN32
      if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
      {
        ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_allocate_direct() failed to obtain mutex: %p\n", p_device_pool->lock);
      }

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);

      ReleaseMutex(p_device_pool->lock);
#elif __linux__
      flock(p_device_pool->lock, LOCK_EX);

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);

      flock(p_device_pool->lock, LOCK_UN);
#endif

      ni_rsrc_free_device_pool(p_device_pool);
    }
  }
  return p_device_context;
}


/*!*****************************************************************************
*   \brief      Allocate resources for decoding/encoding, by designating explicitly
*               the device to use. do not track the load on the host side
*
*   \param[in]  device_type  NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]  guid         unique device (decoder or encoder) module id
*   \return     pointer to ni_device_context_t if found, NULL otherwise
*
*   Note:  only need to specify the device type and guid and codec type
*
*
*   Note:  the returned ni_device_context_t content is not supposed to be used by
*          caller directly: should only be passed to API in the subsequent
*          calls; also after its use, the context should be released by
*          calling ni_rsrc_free_device_context.
*******************************************************************************/
ni_device_context_t *ni_rsrc_allocate_simple_direct
(
  ni_device_type_t device_type,
  int guid
)
{
  ni_device_pool_t *p_device_pool = NULL;

  ni_device_context_t *p_device_context = ni_rsrc_get_device_context(device_type, guid);
 
  return p_device_context;
}


/*!*****************************************************************************
*   \brief       Release resources allocated for decoding/encoding.
*                function This *must* be called at the end of transcoding
*                with previously assigned load value by allocate* functions.
*
*   \param[in/]  p_ctxt  the device context
*   \param[in]   codec   EN_H264 or EN_H265
*   \param[in]   load    the load value returned by allocate* functions
*
*   \return      None
*   THE API needs to be removed from this fild and related test needs to cleanup
*******************************************************************************/
void ni_rsrc_release_resource(ni_device_context_t *p_device_context, ni_codec_t codec,
                              unsigned long load)
{
  ni_rsrc_device_video_ref_cap_t refCap = g_device_reference_table[p_device_context->p_device_info->device_type][codec];
  unsigned long total_cap = refCap.width * refCap.height * refCap.fps;

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_release_resource() failed to obtain mutex: %p\n", p_device_context->lock);
  }
#elif __linux__
  flock(p_device_context->lock, LOCK_EX);
#endif

  if (p_device_context->p_device_info->xcode_load_pixel < load)
  {
    ni_log(NI_LOG_INFO, "Warning: releasing resource load %ld > current load %ld\n",
           load, p_device_context->p_device_info->xcode_load_pixel);
  }
  else
  {
    p_device_context->p_device_info->xcode_load_pixel -= load;
    // Remove as the value is getting from the FW
    // p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#ifdef __linux__
    if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
    {
      ni_log(NI_LOG_ERROR, "ni_rsrc_release_resource msync");
    }
#endif
  }

#ifdef _WIN32
  ReleaseMutex(p_device_context->lock);
#elif __linux__
  flock(p_device_context->lock, LOCK_UN);
#endif
}

/*!*****************************************************************************
*   \brief      Check software instance.
*
*   \param[in]  p_device_context  the device context
*
*   \return     the count of active instances
*               if error, return NI_RETCODE_FAILURE
*
*******************************************************************************/
int ni_rsrc_check_sw_instance(ni_device_context_t *p_device_context, ni_device_type_t device_type)
{
  int retval = NI_RETCODE_SUCCESS;
  int count = 0;    // default the count is zero.
  ni_session_context_t session_ctx = { 0 };
#ifdef XCODER_IO_RW_ENABLED
  session_ctx.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name,
                                              &session_ctx.max_nvme_io_size);
  session_ctx.device_handle = session_ctx.blk_io_handle;
#else
  session_ctx.device_handle = ni_device_open(p_device_context->p_device_info->dev_name,
                                              &session_ctx.max_nvme_io_size);
#endif

  // Check if device can be opened
  if (NI_INVALID_DEVICE_HANDLE == session_ctx.device_handle)
  {
    // Suppose that no instance is active when the device cannot be opened.
    ni_log(NI_LOG_INFO, "open device %s failed, remove it\n", p_device_context->p_device_info->dev_name);
    return count;
  }

  session_ctx.hw_id = p_device_context->p_device_info->hw_id;
#ifdef _WIN32
  session_ctx.event_handle = ni_create_event();
  if (NI_INVALID_EVENT_HANDLE == session_ctx.event_handle)
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_check_sw_instance() create envent\n");
    retval = NI_RETCODE_FAILURE;
    return retval;
  }
#endif

  retval = ni_device_session_query(&session_ctx, device_type);
  if (NI_RETCODE_SUCCESS != retval)
  {
    // Suppose that no instance is active when query fails.
    ni_log(NI_LOG_INFO, "query device %s failed, retval=%d\n", p_device_context->p_device_info->dev_name, retval);
  }
  else
  {
    count = session_ctx.load_query.total_contexts;
    ni_log(NI_LOG_INFO, "device %s active %s instance conut is %d\n", 
           device_type == NI_DEVICE_TYPE_DECODER ? "decoder" : "encoder",
           p_device_context->p_device_info->dev_name,
           count);
  }

  ni_device_close(session_ctx.device_handle);

#ifdef _WIN32
  ni_close_event(session_ctx.event_handle);
#endif

  return count;
}

/*!*****************************************************************************
*   \brief      check the NetInt h/w device in resource pool on the host.
*
*   \param[in]  guid  the global unique device index in resource pool
*               device_type     NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*
*   \return
*               NI_RETCODE_SUCCESS if codec is available,
*               otherwise ni_retcode_t errors (negative values)
*******************************************************************************/
int ni_rsrc_codec_is_available(int guid, ni_device_type_t device_type)
{
  ni_device_pool_t *p_device_pool = NULL;
  ni_device_context_t *p_device_ctx = NULL;
  ni_session_context_t session_ctx = {0};
  uint32_t max_nvme_io_size = 0;
  bool b_release_pool_mtx = false;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  session_ctx.device_handle = NI_INVALID_DEVICE_HANDLE;
  session_ctx.blk_io_handle = NI_INVALID_DEVICE_HANDLE;
  session_ctx.event_handle = NI_INVALID_EVENT_HANDLE;

  if (guid < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR invalid guid:%d\n", guid);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_ERROR, "ERROR: Unknown device type:%d\n", device_type);
    return NI_RETCODE_INVALID_PARAM;
  }

  p_device_pool = ni_rsrc_get_device_pool();
  if (!p_device_pool)
  {
    ni_log(NI_LOG_ERROR, "ERROR: get device poll failed\n");
    retval =  NI_RETCODE_ERROR_GET_DEVICE_POOL;
    LRETURN;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
  {
    ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_list_devices() failed to obtain mutex: %p\n", p_device_pool->lock);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
#elif __linux__
  flock(p_device_pool->lock, LOCK_EX);
#endif
  b_release_pool_mtx = true;

  // get device context
  p_device_ctx = ni_rsrc_get_device_context(device_type, guid);
  if (p_device_ctx)
  {
#ifdef XCODER_IO_RW_ENABLED
    session_ctx.device_handle = ni_device_open(p_device_ctx->p_device_info->blk_name,
                                                &max_nvme_io_size);
    session_ctx.blk_io_handle = session_ctx.device_handle;
#else
    session_ctx.device_handle = ni_device_open(p_device_ctx->p_device_info->dev_name,
                                                &max_nvme_io_size);
#endif
    if (NI_INVALID_DEVICE_HANDLE == session_ctx.device_handle)
    {
      ni_log(NI_LOG_ERROR, "open device failed: %d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_INVALID_HANDLE;
    }
    else
    {
#ifdef XCODER_IO_RW_ENABLED
#ifdef _WIN32
      session_ctx.event_handle = ni_create_event();
      if (NI_INVALID_EVENT_HANDLE == session_ctx.event_handle)
      {
        ni_log(NI_LOG_INFO, "Error create envent:%d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
#endif
#endif

      retval = ni_device_session_query(&session_ctx, device_type);
      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log(NI_LOG_ERROR, "guid %d. %s, %s is not avaiable, type: %d, retval:%d\n",
               guid,
               p_device_ctx->p_device_info->dev_name,
               p_device_ctx->p_device_info->blk_name,
               device_type,
               retval);
        retval = NI_RETCODE_FAILURE;
      }
      else
      {
        ni_log(NI_LOG_INFO, "guid %d. %s %s is avaiable\n",
               guid,
               p_device_ctx->p_device_info->dev_name,
               p_device_ctx->p_device_info->blk_name);
      }
    }
  }
  else
  {
    ni_log(NI_LOG_ERROR, "Error get device resource: guid %d, device_ctx %p\n",
           guid, p_device_ctx);
    retval = NI_RETCODE_FAILURE;
  }

  END;

  if (b_release_pool_mtx)
  {
#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__
    flock(p_device_pool->lock, LOCK_UN);
#endif
  }

  ni_close_event(session_ctx.event_handle);
  ni_device_close(session_ctx.device_handle);

  ni_rsrc_free_device_context(p_device_ctx);

  ni_rsrc_free_device_pool(p_device_pool);

  return retval;
}

/*!*****************************************************************************
*   \brief      Remove an NetInt h/w device from resource pool on the host.
*
*   \param[in]  p_dev  the NVMe device name
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_rsrc_remove_device(const char* dev)
{
  int i, j, count, rc = NI_RETCODE_FAILURE;
  ni_device_queue_t *p_device_queue = NULL;
  ni_device_pool_t *p_device_pool = ni_rsrc_get_device_pool();
  ni_device_context_t *p_decoder_device_ctx = NULL, *p_encoder_device_ctx = NULL;

  if (!dev)
  {
    ni_log(NI_LOG_INFO, "ERROR input parameter in ni_rsrc_remove_device() \n");
    return NI_RETCODE_FAILURE;
  }
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
      ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_release_resource() failed to obtain mutex: %p\n", p_device_pool->lock);
    }
#elif __linux__
    flock(p_device_pool->lock, LOCK_EX);
#endif

    p_device_queue = p_device_pool->p_device_queue;
    count = p_device_queue->decoders_cnt;
    int guid = -1;
    for (i = 0; i < count; i++)
    {
      guid = p_device_queue->decoders[i];
      p_decoder_device_ctx = ni_rsrc_get_device_context(NI_DEVICE_TYPE_DECODER, guid);
      p_encoder_device_ctx = ni_rsrc_get_device_context(NI_DEVICE_TYPE_ENCODER, guid);
      if (p_decoder_device_ctx && p_encoder_device_ctx && 0 == strcmp(p_decoder_device_ctx->p_device_info->dev_name, dev) &&
          0 == strcmp(p_encoder_device_ctx->p_device_info->dev_name, dev))
      {
        // found the device_info and remove its lock and storage
        char enc_lck_name[32] = {0};
        char dec_lck_name[32] = {0};
        ni_rsrc_get_lock_name(NI_DEVICE_TYPE_ENCODER, guid, enc_lck_name, sizeof(enc_lck_name));
        ni_rsrc_get_lock_name(NI_DEVICE_TYPE_DECODER, guid, dec_lck_name, sizeof(dec_lck_name));
        ni_log(NI_LOG_INFO, "dec_guid %d shm_name: %s  lck_name: %s, "
               "enc_guid %d shm_name: %s  lck_name: %s \n",
               guid, p_decoder_device_ctx->shm_name, dec_lck_name,
               guid, p_encoder_device_ctx->shm_name, enc_lck_name);
        // check if device can be removed
        if (0 != ni_rsrc_check_sw_instance(p_decoder_device_ctx, NI_DEVICE_TYPE_DECODER)) //check decoder
        {
          ni_rsrc_free_device_context(p_decoder_device_ctx);
          ni_rsrc_free_device_context(p_encoder_device_ctx);
          break;
        }
        else if (0 != ni_rsrc_check_sw_instance(p_encoder_device_ctx, NI_DEVICE_TYPE_ENCODER)) //check encoder
        {
          ni_rsrc_free_device_context(p_decoder_device_ctx);
          ni_rsrc_free_device_context(p_encoder_device_ctx);
          break;
        }

        // remove it from coders queue and decrement device_info counter, re-arrange
        // coders queue
#ifdef _WIN32
        CloseHandle(p_decoder_device_ctx->lock);
        CloseHandle(p_encoder_device_ctx->lock);
        rc = NI_RETCODE_SUCCESS;
#elif __linux__

#ifndef _ANDROID

        if (0 == shm_unlink(p_decoder_device_ctx->shm_name))
        {
          ni_log(NI_LOG_INFO, "dec shm_name %s deleted.\n", p_decoder_device_ctx->shm_name);
        }
        else
        {
          ni_log(NI_LOG_INFO, "dec shm_name %s deletion failure.\n", p_decoder_device_ctx->shm_name);
          break;
        }

        if (0 == shm_unlink(p_encoder_device_ctx->shm_name))
        {
          ni_log(NI_LOG_INFO, "enc shm_name %s deleted.\n", p_encoder_device_ctx->shm_name);
        }
        else
        {
          ni_log(NI_LOG_INFO, "enc shm_name %s deletion failure.\n", p_encoder_device_ctx->shm_name);
          break;
        }

        if (0 == unlink(dec_lck_name))
        {
          ni_log(NI_LOG_INFO, "dec lck_name %s deleted.\n", dec_lck_name);
        }
        else
        {
          ni_log(NI_LOG_INFO, "dec lck_name %s deletion failure.\n", dec_lck_name);
          break;
        }

        if (0 == unlink(enc_lck_name))
        {
          ni_log(NI_LOG_INFO, "enc lck_name %s deleted.\n", enc_lck_name);
        }
        else
        {
          ni_log(NI_LOG_INFO, "enc lck_name %s deletion failure.\n", enc_lck_name);
          break;
        }
#endif

#endif
        // move everything after position i forward
        for (j = i + 1; j < count; j++)
        {
          p_device_queue->decoders[j - 1] = p_device_queue->decoders[j];
          p_device_queue->encoders[j - 1] = p_device_queue->encoders[j];
        }
        p_device_queue->decoders[count - 1] = -1;
        p_device_queue->encoders[count - 1] = -1;
        p_device_queue->decoders_cnt--;
        p_device_queue->encoders_cnt--;
#ifdef __linux__
        if (msync((void *)p_device_pool->p_device_queue, sizeof(ni_device_queue_t),
                  MS_SYNC | MS_INVALIDATE))
        {
          ni_log(NI_LOG_ERROR, "ni_rsrc_remove_device msync");
        }
        else
        {
          ni_log(NI_LOG_INFO, "%s deleted successfully !\n", dev);
          rc = NI_RETCODE_SUCCESS;
        }
#endif
        break;
      }

      ni_rsrc_free_device_context(p_decoder_device_ctx);
      ni_rsrc_free_device_context(p_encoder_device_ctx);
    }

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__
    flock(p_device_pool->lock, LOCK_UN);
#endif
    ni_rsrc_free_device_pool(p_device_pool);
  }

  return rc;
}

static int int_cmp(const void *a, const void *b)
{
  const int *ia = (const int *)a;
  const int *ib = (const int *)b;
  return *ia  - *ib;
  /* integer comparison: returns negative if b > a and positive if a > b */
}


/*!*****************************************************************************
*   \brief      Add an NetInt h/w device into resource pool on the host.
*
*   \param[in]  p_dev  the NVMe device name
*   \param[in]  should_match_rev  0: transcoder firmware revision matching the
*                             library's version is NOT required for placing
*                             the transcoder into resource pool; 1: otherwise
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_rsrc_add_device(const char* dev, int should_match_rev)
{
  uint32_t i=0, j=0, k=0, count=0, fw_ver_compat_warning=0;
  ni_retcode_t rc = NI_RETCODE_SUCCESS;
  ni_device_handle_t fd = 0;
  ni_device_queue_t *p_device_queue;
  ni_device_info_t device_info;
  ni_device_capability_t cap;
  ni_device_pool_t *p_device_pool = ni_rsrc_get_device_pool();
  int guids[MAX_DEVICE_CNT];

  if (!dev)
  {
    ni_log(NI_LOG_INFO, "ERROR input parameter in ni_rsrc_add_device() \n");
    return NI_RETCODE_FAILURE;
  }
  // get the first free guid that is in sequence 0,1,2, ... n, so that empty
  // index left behind by device removal can be reused
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
      ni_log(NI_LOG_INFO, "ERROR: ni_rsrc_release_resource() failed to obtain mutex: %p\n", p_device_pool->lock);
    }
#elif __linux__
    flock(p_device_pool->lock, LOCK_EX);
#endif

    p_device_queue = p_device_pool->p_device_queue;
    count = p_device_queue->decoders_cnt;
    int32_t guid = -1;
    for (i = 0; i < count; i++)
    {
      ni_device_context_t *tmp_ctxt = ni_rsrc_get_device_context(NI_DEVICE_TYPE_DECODER, p_device_queue->decoders[i]);
      if (tmp_ctxt && 0 == strcmp(tmp_ctxt->p_device_info->dev_name, dev))
      {
        rc = NI_RETCODE_FAILURE;
        ni_log(NI_LOG_INFO, "Transcoder %s already active, guid: %d\n",
               dev, p_device_queue->decoders[i]);
        ni_rsrc_free_device_context(tmp_ctxt);
        LRETURN;
      }
      else
      {
        ni_rsrc_free_device_context(tmp_ctxt);
      }

      guids[i] = p_device_queue->decoders[i];
    }

    // sort guid index in ascending order and pick the first free guid
    qsort(guids, count, sizeof(int), int_cmp);
    for (i = 0; i < count; i++)
    {
      if (1 == guids[i] - guid)
      {
        guid = guids[i];
      }
      else
      {
        break;
      }
    }
    guid++;
    ni_log(NI_LOG_INFO, "GUID to be added: %d\n", guid);

    // retrieve the device_info capability info from f/w
    memset(&device_info, 0, sizeof(ni_device_info_t));
    sprintf(device_info.dev_name, "%s", dev);
    memset(&cap, 0, sizeof(ni_device_capability_t));

    uint32_t tmp_io_size;
#ifdef XCODER_IO_RW_ENABLED
    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    fd = ni_device_open(device_info.blk_name, &tmp_io_size);
#else
    fd = ni_device_open(device_info.dev_name, &tmp_io_size);
#endif
    if (NI_INVALID_DEVICE_HANDLE == fd)
    {
      rc = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_INFO, "Failed to open device: %s %s\n", device_info.dev_name, device_info.blk_name);
      LRETURN;
    }

    rc = ni_device_capability_query(fd, &cap);
    if ((NI_RETCODE_SUCCESS == rc) && (cap.device_is_xcoder) &&
        (! should_match_rev ||
         (should_match_rev && ni_is_fw_compatible(cap.fw_rev))))
    {
      fw_ver_compat_warning = 0;
      if (ni_is_fw_compatible(cap.fw_rev) == 2)
      {
        ni_log(NI_LOG_INFO, "WARNING - Query %s FW version: %.*s is below the minimum support version for "
               "this SW version. Some features may be missing.\n",
               device_info.dev_name, (int) sizeof(cap.fw_rev), cap.fw_rev);
        fw_ver_compat_warning = 1;
      }

      ni_log(NI_LOG_INFO, "%s  num_hw: %d\n", device_info.dev_name, cap.hw_elements_cnt);
      uint32_t total_modules = cap.h264_decoders_cnt + cap.h264_encoders_cnt +
      cap.h265_decoders_cnt + cap.h265_encoders_cnt;

      ni_device_info_t *pCoderInfo = NULL;
      int decoders_cnt = 0, encoders_cnt = 0;
      for (j = 0; j < total_modules; j++)
      {
        int rc;
        pCoderInfo = NULL;

        memset(&device_info, 0, sizeof(ni_device_info_t));
        sprintf(device_info.dev_name, "%s", dev);
        ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
        device_info.hw_id = cap.xcoder_devices[j].hw_id;
        device_info.fw_ver_compat_warning = fw_ver_compat_warning;
        memcpy(device_info.fw_rev, cap.fw_rev, sizeof(device_info.fw_rev));
        memcpy(device_info.fw_commit_hash, cap.fw_commit_hash, sizeof(device_info.fw_commit_hash) - 1);
        memcpy(device_info.fw_commit_time, cap.fw_commit_time, sizeof(device_info.fw_commit_time) - 1);
        memcpy(device_info.fw_branch_name, cap.fw_branch_name, sizeof(device_info.fw_branch_name) - 1);
        device_info.max_fps_1080p = cap.xcoder_devices[j].max_1080p_fps;
        device_info.max_instance_cnt = cap.xcoder_devices[j].max_number_of_contexts;
        device_info.device_type = (ni_device_type_t) cap.xcoder_devices[j].codec_type;

        // check if entry has been created for this h/w (hw_id):
        // if not, then create a new entry; otherwise just update it
        ni_device_context_t *p_device_context = ni_rsrc_get_device_context(device_info.device_type, guid);
        if ( (p_device_context) && (p_device_context->p_device_info->hw_id == device_info.hw_id) )
        {
          pCoderInfo = p_device_context->p_device_info;
        }
        ni_codec_t fmt = (ni_codec_t) cap.xcoder_devices[j].codec_format;
        if (pCoderInfo)
        {
          ni_log(NI_LOG_INFO, "%s h/w id %d update\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" :
                 "encoder", device_info.hw_id);
          rc = ni_rsrc_fill_device_info(pCoderInfo, fmt, device_info.device_type, &cap.xcoder_devices[j]);
          if (NI_RETCODE_SUCCESS == rc)
          {
            if (NI_DEVICE_TYPE_DECODER == device_info.device_type)
            {
              decoders_cnt++;
            }
            else
            {
              encoders_cnt++;
            }
          }
        }
        else
        {
          ni_log(NI_LOG_INFO, "%s h/w id %d create\n", NI_DEVICE_TYPE_DECODER == device_info.device_type ? "decoder" :
                 "encoder", device_info.hw_id);
          pCoderInfo = &device_info;

          rc = ni_rsrc_fill_device_info(pCoderInfo, fmt, device_info.device_type, &cap.xcoder_devices[j]);
          if (NI_RETCODE_SUCCESS == rc)
          {
            /*! add the h/w device_info entry */
            pCoderInfo->module_id = guid;
            if (NI_DEVICE_TYPE_DECODER == device_info.device_type)
            {
              decoders_cnt++;
            }
            else
            {
              encoders_cnt++;
            }
            ni_rsrc_get_one_device_info(&device_info);
          }
        }
      } // for each device_info module

      // there are some cases that updating failed or NI_shm_* already exist.
      // so check and update device pool here
      if ((decoders_cnt == (cap.h264_decoders_cnt + cap.h265_decoders_cnt)) &&
          (encoders_cnt == (cap.h264_encoders_cnt + cap.h265_encoders_cnt)))
      {
        p_device_queue->decoders_cnt++;
        p_device_queue->decoders[p_device_queue->decoders_cnt - 1] = pCoderInfo->module_id;
        p_device_queue->encoders_cnt++;
        p_device_queue->encoders[p_device_queue->encoders_cnt - 1] = pCoderInfo->module_id;
      }
      else
      {
        rc = NI_RETCODE_FAILURE;
        ni_log(NI_LOG_INFO, "Failed to update share memory: %s %s\n", device_info.dev_name, device_info.blk_name);
        LRETURN;
      }

#ifdef __linux__
      if (msync((void *)p_device_pool->p_device_queue, sizeof(ni_device_queue_t),
                MS_SYNC | MS_INVALIDATE))
      {
        ni_log(NI_LOG_ERROR, "ni_rsrc_add_device msync\n");
        rc = NI_RETCODE_FAILURE;
      } else
        ni_log(NI_LOG_INFO, "%s added successfully !\n", dev);
#endif
    }
    else
    { // xcoder NOT supported
      rc = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_INFO, "Query %s rc %d NOT xcoder-support: %u, or mismatch revision: "
             "%s; not added\n", dev, rc, cap.device_is_xcoder, cap.fw_rev);
    }

    END;
    if (NI_INVALID_DEVICE_HANDLE != fd)
    {
      ni_device_close(fd);
    }

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__
    flock(p_device_pool->lock, LOCK_UN);
#endif

    ni_rsrc_free_device_pool(p_device_pool);
  }

  return rc;
}

/*!*****************************************************************************
*   \brief      Free all resources taken by the device pool
*
*   \param[in]  p_device_pool  Poiner to a device pool struct
*
*   \return     None
*******************************************************************************/
void ni_rsrc_free_device_pool(ni_device_pool_t* p_device_pool)
{
  if (p_device_pool)
  {
    if (NI_INVALID_LOCK_HANDLE != p_device_pool->lock)
    {
#ifdef _WIN32
        CloseHandle(p_device_pool->lock);
#elif __linux__
        close(p_device_pool->lock);
#endif
    }
#ifdef _WIN32
    UnmapViewOfFile(p_device_pool->p_device_queue);
#else
    munmap(p_device_pool->p_device_queue, sizeof(ni_device_queue_t));
#endif

    free(p_device_pool);
    p_device_pool = NULL;
  }
}

/*!*****************************************************************************
 *  \brief  lock a file lock and open a session on a device
 *
 *  \param device_type
 *  \param lock
 *
 *  \return None
 *******************************************************************************/
int ni_rsrc_lock_and_open(int device_type, ni_lock_handle_t* lock)
{

  int count = 0;
  int status = NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;

#ifdef _WIN32
  // Here we try to open the file lock, retry if it failed
  if (NI_DEVICE_TYPE_DECODER == device_type)
  {
    //Get the exited Mutex
    *lock = CreateMutex(
        NULL,            // default security attributes
        FALSE,            // initially no owned
        CODERS_RETRY_DELCK_NAME  // unnamed mutex
    );

    if (NULL == *lock)
    {
      ni_log(NI_LOG_ERROR, "ni_rsrc_lock_and_open() CreateMutex %s failed, error=%d\n", CODERS_RETRY_DELCK_NAME, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
    }
  }
  else
  {
    //Get the exited Mutex
    *lock = CreateMutex(
        NULL,            // default security attributes
        FALSE,            // initially no owned
        CODERS_RETRY_ENLCK_NAME  // unnamed mutex
    );
    
    if (NULL == *lock)
    {
      ni_log(NI_LOG_ERROR, "ni_rsrc_lock_and_open() CreateMutex %s failed, error=%d\n", CODERS_RETRY_ENLCK_NAME, NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
    }
  }
#else
  do
  {
    if (count>=1)
    {
      //sleep 10ms if the file lock is locked by other FFmpeg process
      usleep(LOCK_WAIT);
    }
    // Here we try to open the file lock, retry if it failed
    if (NI_DEVICE_TYPE_DECODER == device_type)
    {
      *lock = open(CODERS_RETRY_DELCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
    }
    else
    {
      *lock = open(CODERS_RETRY_ENLCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
    }

    if (*lock < 0)
    {
      count++;
      if (count > MAX_LOCK_RETRY)
      {
        ni_log(NI_LOG_TRACE, "Can not lock down the file lock after 6s\n");
        return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
      }
    }
  }
  while (*lock < 0);
#endif

  // Now the lock is free so we lock it down
  count = 0;
  do
  {
    //sleep 10ms if the file lock is locked by other FFmpeg process
    if (count>=1)
    {
      //sleep 10ms if the file lock is locked by other FFmpeg process
      usleep(LOCK_WAIT);
    }
#ifdef _WIN32
    DWORD ret = WaitForSingleObject(*lock, 1); // time-out 1ms
    if (WAIT_OBJECT_0 == ret)
    {
      status = NI_RETCODE_SUCCESS;
    }
    else if (WAIT_TIMEOUT != ret)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: ni_rsrc_lock_and_open() failed to obtain mutex\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    }
#else
    status = flock(*lock, LOCK_EX);
#endif
    if (status != 0)
    {
      count++;
      if (count > MAX_LOCK_RETRY)
      {
        ni_log(NI_LOG_ERROR, "Can not put down the lock after 6s\n");
        return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
      }
    }
  }
  while (status != 0);

  return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  unlock a file lock
 *
 *  \param device_type
 *  \param lock
 *
 *  \return None
 *******************************************************************************/
int ni_rsrc_unlock(int device_type, ni_lock_handle_t lock)
{
  int status = NI_RETCODE_SUCCESS;
  int count = 0;

  if (lock == NI_INVALID_LOCK_HANDLE)
  {
    return NI_RETCODE_FAILURE;
  }
  else
  {
    do
    {
      if (count>=1)
      {
        usleep(LOCK_WAIT);
      }
#ifdef _WIN32
      if (ReleaseMutex(lock))
      {
        status = NI_RETCODE_SUCCESS;
      }
#else
      status = flock(lock, LOCK_UN);
#endif
      count++;
      if (count > MAX_LOCK_RETRY)
      {
        ni_log(NI_LOG_TRACE, "Can not unlock the lock after 6s\n");
        return NI_RETCODE_ERROR_UNLOCK_DEVICE;
      }
    }
    while (status != NI_RETCODE_SUCCESS);
  }
  return NI_RETCODE_SUCCESS;
}
