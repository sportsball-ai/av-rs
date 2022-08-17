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
*   \file   ni_rsrc_api.c
*
*  \brief  Exported routines related to resource management of NI Quadra devices
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#if __linux__ || __APPLE__
#include <unistd.h>
#include <fcntl.h> /* For O_* constants */
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h> /* For mode constants */
#include <signal.h>
#include "setjmp.h"
#endif

#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"


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

// return true if string key is found in array of strings, false otherwise
static bool is_str_in_str_array(const char key[],
                                char arr[][NI_MAX_DEVICE_NAME_LEN],
                                int array_size)
{
    int i;

    for (i = 0; i < array_size; i++)
    {
        if (0 == strcmp(key, arr[i]))
        {
            return true;
        }
    }
    return false;
}

void print_device(ni_device_t *p_device)
{
    if (!p_device)
    {
        ni_log(NI_LOG_INFO, "WARNING: NULL parameter passed in!\n");
        return;
    }

    ni_device_info_t *p_dev_info = NULL;
    for (size_t xcoder_index_1 = 0;
         xcoder_index_1 < p_device->xcoder_cnt[NI_DEVICE_TYPE_DECODER];
         xcoder_index_1++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_DECODER][xcoder_index_1];
        ni_log(NI_LOG_INFO, "Device #%zu:\n", xcoder_index_1);
        ni_log(NI_LOG_INFO, "  Serial number: %.*s\n",
               (int)sizeof(p_dev_info->serial_number),
               p_dev_info->serial_number);
        ni_log(NI_LOG_INFO, "  Model number: %.*s\n",
               (int)sizeof(p_dev_info->model_number),
               p_dev_info->model_number);
        ni_log(NI_LOG_INFO, "  F/W rev: %.*s\n",
               (int)sizeof(p_dev_info->fw_rev), p_dev_info->fw_rev);
        ni_log(NI_LOG_INFO, "  F/W & S/W compatibility: %s\n",
               p_dev_info->fw_ver_compat_warning ?
               "no, possible missing features" : "yes");
        ni_log(NI_LOG_INFO, "  F/W branch: %s\n",
               p_dev_info->fw_branch_name);
        ni_log(NI_LOG_INFO, "  F/W commit time: %s\n",
               p_dev_info->fw_commit_time);
        ni_log(NI_LOG_INFO, "  F/W commit hash: %s\n",
               p_dev_info->fw_commit_hash);
        ni_log(NI_LOG_INFO, "  F/W build time: %s\n",
               p_dev_info->fw_build_time);
        ni_log(NI_LOG_INFO, "  F/W build id: %s\n",p_dev_info->fw_build_id);
        ni_log(NI_LOG_INFO, "  DeviceID: %s\n", p_dev_info->dev_name);
        ni_log(NI_LOG_INFO, "  BlockDeviceID: %s\n", p_dev_info->blk_name);
        ni_log(NI_LOG_INFO, "  PixelFormats: yuv420p, yuv420p10le, nv12, p010le"
               ", ni_quadra\n");

        for (size_t dev_type = NI_DEVICE_TYPE_DECODER;
             dev_type != NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            for (size_t xcoder_index_2 = 0;
                 xcoder_index_2 < p_device->xcoder_cnt[NI_DEVICE_TYPE_DECODER];
                 xcoder_index_2++)
            {
                if (strcmp(p_dev_info->dev_name,
                           p_device->xcoders[dev_type][xcoder_index_2].dev_name)
	            == 0 && p_dev_info->module_id >= 0)
                {
                    ni_rsrc_print_device_info(&(p_device->xcoders[dev_type]
                                                [xcoder_index_2]));
                }
            }
        }
    }
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
    char xcoder_dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
    int xcoder_dev_count = 0;
    char curr_dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
    int curr_dev_count = 0;
    int i = 0;
    ni_device_t saved_coders = {0};

    // retrieve saved info from resource pool at start up
    if (NI_RETCODE_SUCCESS ==
        ni_rsrc_list_devices(
            NI_DEVICE_TYPE_DECODER,
            saved_coders.xcoders[NI_DEVICE_TYPE_DECODER],
            &(saved_coders.xcoder_cnt[NI_DEVICE_TYPE_DECODER])))
    {
        for (i = 0; i < saved_coders.xcoder_cnt[NI_DEVICE_TYPE_DECODER];
              i++)
        {
            strcpy(xcoder_dev_names[i],
                    saved_coders.xcoders[NI_DEVICE_TYPE_DECODER][i]
                        .dev_name);
        }
        xcoder_dev_count =
            saved_coders.xcoder_cnt[NI_DEVICE_TYPE_DECODER];
        ni_log(NI_LOG_INFO,
                "%d devices retrieved from current pool at start up\n",
                xcoder_dev_count);
    } else
    {
        ni_log(NI_LOG_ERROR, "Error retrieving from current pool at start "
                "up\n");
    }

    curr_dev_count =
        ni_rsrc_get_local_device_list(curr_dev_names, NI_MAX_DEVICE_CNT);
    // remove from resource pool any device that is not available now
    for (i = 0; i < xcoder_dev_count; i++)
    {
        if (!is_str_in_str_array(xcoder_dev_names[i], curr_dev_names,
                                 curr_dev_count))
        {
            ni_log(NI_LOG_INFO,
                   "\n\n%d. %s NOT in current scanned list, removing !\n", i,
                   xcoder_dev_names[i]);
            if (NI_RETCODE_SUCCESS ==
                ni_rsrc_remove_device(xcoder_dev_names[i]))
            {
                ni_log(NI_LOG_INFO, "%s deleted successfully !\n",
                       xcoder_dev_names[i]);
            } else
            {
                ni_log(NI_LOG_ERROR, "%s failed to delete !\n",
                       xcoder_dev_names[i]);
            }
        }
    }

    // and add into resource pool any newly discoved ones
    for (i = 0; i < curr_dev_count; i++)
    {
        if (!is_str_in_str_array(curr_dev_names[i], xcoder_dev_names,
                                 xcoder_dev_count))
        {
            ni_log(NI_LOG_INFO, "\n\n%s NOT in previous list, adding !\n",
                   curr_dev_names[i]);
            if (NI_RETCODE_SUCCESS ==
                ni_rsrc_add_device(curr_dev_names[i], should_match_rev))
            {
                ni_log(NI_LOG_INFO, "%s added successfully !\n", curr_dev_names[i]);
            } else
            {
                ni_log(NI_LOG_ERROR, "%s failed to add !\n", curr_dev_names[i]);
            }
        }
    }

    return NI_RETCODE_SUCCESS;
}

#ifdef _ANDROID

#include "ni_rsrc_api_android.h"

android::sp<INidec> service = NULL;

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
        service = INidec::getService();
        if (service == nullptr)
        {
            ni_log(NI_LOG_ERROR, "ni_rsrc_android_init error\n");
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
  char   ni_devices[][NI_MAX_DEVICE_NAME_LEN],
  int    max_handles
)
{
  if ((ni_devices == NULL)||(max_handles == 0))
  {
    ni_log(NI_LOG_ERROR, "Error with input parameters\n");
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
  mutex_handle = CreateMutex(NULL,             // default security attributes
                             FALSE,            // initially owned
                             CODERS_LCK_NAME   // unnamed mutex
  );

  if (NULL == mutex_handle)
  {
    ni_log(NI_LOG_ERROR, "CreateMutex %s failed: %d\n", CODERS_LCK_NAME,
           NI_ERRNO);
    return NULL;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(mutex_handle, INFINITE))
  {
      ni_log(NI_LOG_ERROR, "ERROR: Failed to obtain mutex: %p\n", mutex_handle);
  }

  map_file_handle = OpenFileMapping(
      FILE_MAP_ALL_ACCESS,   // read/write access
      FALSE,                 // do not inherit the name
      CODERS_SHM_NAME        // name of mapping object
  );

  if (NULL == map_file_handle)
  {
      ReleaseMutex(mutex_handle);
      ni_log(NI_LOG_ERROR, "Could not open file mapping object %s, error: %d\n",
             CODERS_SHM_NAME, NI_ERRNO);
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
      ni_log(NI_LOG_ERROR, "ERROR %s() malloc() ni_device_pool_t: %s\n", __func__,
             strerror(NI_ERRNO));
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
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init(int should_match_rev, int timeout_seconds)
{
#define SLEEPLOOP 3
  /*! list all XCoder devices under /dev/.. */
  char dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = { 0 };
  int i = 0, j = 0, k = 0, xcoder_device_cnt = 0, fw_ver_compat_warning = 0;
  int runtime = 0;
  DWORD rc = 0;
  uint32_t tmp_io_size;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_device_info_t device_info = { 0 };
  ni_device_info_t* p_device_info = NULL;
  ni_device_queue_t* p_device_queue = NULL;
  HANDLE lock = NULL;
  HANDLE map_file_handle = NULL;
  ni_device_capability_t device_capabilites = { 0 };
  ni_device_handle_t handle;
  uint32_t xcoder_guid[NI_DEVICE_TYPE_XCODER_MAX] = {0};

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
    rc = NI_ERRNO;
    ni_log(NI_LOG_ERROR, "ERROR: CreateFileMapping returned: %d\n", rc);
    return NI_RETCODE_FAILURE;
  }
  else
  {
    rc = NI_ERRNO;
    if (ERROR_ALREADY_EXISTS == rc)
    {
      ni_log(NI_LOG_INFO, "NETINT resources have been initialized already, exiting ..\n");
      CloseHandle(map_file_handle);
      return NI_RETCODE_SUCCESS;
    }
    else
    {
      ni_log(NI_LOG_INFO, "NETINT resources not initialized, starting initialization ..\n");
    }
  }

  while (0 == xcoder_device_cnt)
  {
    xcoder_device_cnt = ni_rsrc_enumerate_devices(dev_names, NI_MAX_DEVICE_CNT);

    if (NI_RETCODE_ERROR_MEM_ALOC == xcoder_device_cnt)
    {
      ni_log(NI_LOG_FATAL, "FATAL: memory allocation failed\n");
      CloseHandle(map_file_handle);
      return NI_RETCODE_FAILURE;
    }
    else if (0 == xcoder_device_cnt)
    {
      ni_log(NI_LOG_INFO, "NVMe Devices not ready, will retry again ...\n");
      if (g_xcoder_stop_process)
      {
        ni_log(NI_LOG_ERROR, "Requested to stop, exiting ...\n");
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
      }
      runtime += SLEEPLOOP;
      Sleep(SLEEPLOOP * 1000);
      if (runtime >= timeout_seconds && timeout_seconds != 0)
      {
          ni_log(NI_LOG_ERROR, "Timeout reached at %d seconds! Failing\n",
                 runtime);
          CloseHandle(map_file_handle);
          return NI_RETCODE_FAILURE;
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
    ni_log(NI_LOG_ERROR, "Could not map view of file, p_last error (%d).\n",
           NI_ERRNO);
    CloseHandle(map_file_handle);
    return NI_RETCODE_FAILURE;
  }

  lock = CreateMutex(NULL, FALSE, CODERS_LCK_NAME);
  if (NULL == lock)
  {
      ni_log(NI_LOG_ERROR, "Init CreateMutex %s failed: %d\n", CODERS_LCK_NAME,
             NI_ERRNO);
      CloseHandle(map_file_handle);
      return NI_RETCODE_FAILURE;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(lock, INFINITE))
  {
      ni_log(NI_LOG_ERROR, "ERROR %d: failed to obtain mutex: %p\n",
             NI_ERRNO, lock);
      CloseHandle(map_file_handle);
      return NI_RETCODE_FAILURE;
  }

  // init the ni_device_queue_t
  for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
  {
      p_device_queue->xcoder_cnt[k] = 0;
      for (i = 0; i < NI_MAX_DEVICE_CNT; i++)
      {
          p_device_queue->xcoders[k][i] = -1;
      }
  }

  for (i = 0; i < xcoder_device_cnt; i++)
  {

    /*! retrieve decoder and encoder info and create shared memory
       and named lock accordingly, using NVMe "Identify Controller" */
       /*! check whether Xcoder is supported and retrieve the xcoder info */
    int ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s",
                       dev_names[i]);
    if (ret < 0)
    {
      return -1;
    }
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

    handle = ni_device_open(device_info.dev_name, &tmp_io_size);
    if (NI_INVALID_DEVICE_HANDLE == handle)
    {
      continue;
    }

    retval = ni_device_capability_query(handle, &device_capabilites);
    if ((NI_RETCODE_SUCCESS == retval) &&
        (is_supported_xcoder(device_capabilites.device_is_xcoder)) &&
        (!should_match_rev || ni_is_fw_compatible(device_capabilites.fw_rev)))
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
      int total_modules = device_capabilites.xcoder_devices_cnt;

      for (j = 0; j < total_modules; j++)
      {
        p_device_info = NULL;
        memset(&device_info, 0, sizeof(ni_device_info_t));
        ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s",
                       dev_names[i]);
        if (ret < 0)
        {
          return -1;
        }

        ni_find_blk_name(device_info.dev_name, device_info.blk_name,
                         sizeof(device_info.blk_name));
        device_info.hw_id = device_capabilites.xcoder_devices[j].hw_id;
        device_info.fw_ver_compat_warning = fw_ver_compat_warning;
        memcpy(device_info.serial_number, device_capabilites.serial_number,
               sizeof(device_info.serial_number));
        memcpy(device_info.model_number, device_capabilites.model_number,
               sizeof(device_info.model_number));
        memcpy(device_info.fw_rev, device_capabilites.fw_rev,
               sizeof(device_info.fw_rev));
        memcpy(device_info.fw_branch_name, device_capabilites.fw_branch_name,
               sizeof(device_info.fw_branch_name) - 1);
        memcpy(device_info.fw_commit_time, device_capabilites.fw_commit_time,
               sizeof(device_info.fw_commit_time) - 1);
        memcpy(device_info.fw_commit_hash, device_capabilites.fw_commit_hash,
               sizeof(device_info.fw_commit_hash) - 1);
        memcpy(device_info.fw_build_time, device_capabilites.fw_build_time,
               sizeof(device_info.fw_build_time) - 1);
        memcpy(device_info.fw_build_id, device_capabilites.fw_build_id,
               sizeof(device_info.fw_build_id) - 1);

        device_info.max_fps_4k =
            device_capabilites.xcoder_devices[j].max_4k_fps;
        device_info.max_instance_cnt = device_capabilites.xcoder_devices[j].max_number_of_contexts;
        device_info.device_type = (ni_device_type_t)device_capabilites.xcoder_devices[j].codec_type;

        int device_cnt_so_far =
            p_device_queue->xcoder_cnt[device_info.device_type];
        int tmp_guid = -1;

        // check if entry has been created for this h/w (hw_id):
        // if not, then create a new entry; otherwise just update it
        for (k = 0; k < device_cnt_so_far; k++)
        {
            tmp_guid = p_device_queue->xcoders[device_info.device_type][k];
            ni_device_context_t *p_device_context =
                ni_rsrc_get_device_context(device_info.device_type, tmp_guid);
            if ((p_device_context) &&
                (strcmp(p_device_context->p_device_info->dev_name,
                        device_info.dev_name) == 0) &&
                (p_device_context->p_device_info->hw_id == device_info.hw_id))
            {
                p_device_info = p_device_context->p_device_info;
                break;
            }
        }

        ni_codec_t fmt = (ni_codec_t)device_capabilites.xcoder_devices[j].codec_format;

        if (p_device_info)
        {
            ni_log(NI_LOG_INFO, "%s h/w id %d update\n",
                   device_type_str[device_info.device_type], device_info.hw_id);
            rc = ni_rsrc_fill_device_info(
                p_device_info, fmt, device_info.device_type,
                &device_capabilites.xcoder_devices[j]);
        }
        else
        {
            ni_log(NI_LOG_INFO, "%s h/w id %d create\n",
                   device_type_str[device_info.device_type], device_info.hw_id);

            p_device_info = &device_info;

            rc = ni_rsrc_fill_device_info(
                p_device_info, fmt, device_info.device_type,
                &device_capabilites.xcoder_devices[j]);

            if (NI_RETCODE_SUCCESS == rc)
            {
                /*! add the h/w device_info entry */
                p_device_info->module_id =
                    xcoder_guid[device_info.device_type]++;
                p_device_queue->xcoder_cnt[device_info.device_type] =
                    xcoder_guid[device_info.device_type];
                p_device_queue->xcoders[device_info.device_type]
                                       [p_device_info->module_id] =
                    p_device_info->module_id;
                ni_rsrc_get_one_device_info(&device_info);
            }
        }
      } /*! for each device_info */
    } /*! if device supports xcoder */
    else
    {
      ni_log(NI_LOG_INFO, "Query %s rc %d NOT xcoder-support: %u, or mismatch "
             "revision: %.*s; Not added\n", device_info.dev_name, retval,
             device_capabilites.device_is_xcoder,
             (int) sizeof(device_capabilites.fw_rev),
             device_capabilites.fw_rev);
    }

    ni_device_close(handle);
  } /*! for each nvme device */

  for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
  {
      p_device_queue->xcoder_cnt[k] = xcoder_guid[k];
  }
  rc = NI_RETCODE_SUCCESS;
  if (NULL != p_device_queue)
  {
      UnmapViewOfFile(p_device_queue);
  }
  if (NULL != lock)
  {
      ReleaseMutex(lock);
      CloseHandle(lock);
  }
  return rc;
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
  mutex_handle = CreateMutex(NULL,        // default security attributes
                             FALSE,       // initially owned
                             lck_name);   // unnamed mutex

  if (NULL == mutex_handle)
  {
    ni_log(NI_LOG_ERROR, "CreateMutex error: %d\n", NI_ERRNO);
    p_device_context = NULL;
    LRETURN;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(mutex_handle, INFINITE))
  {
      ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_get_device_context() failed to "
             "obtain mutex: %p\n", mutex_handle);
  }

  map_file_handle = OpenFileMapping(
                                      FILE_MAP_ALL_ACCESS,   // read/write access
                                      FALSE,                 // do not inherit the name
                                      (LPCSTR)shm_name
                                   );               // name of mapping object

  if (NULL == map_file_handle)
  {
    ni_log(NI_LOG_ERROR, "Could not open file mapping object %s, error: %d.\n",
           shm_name, NI_ERRNO);
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
    ni_log(NI_LOG_ERROR, "Could not map view of file, p_last error (%d).\n",
           NI_ERRNO);
    /*! lock the file for access */
    p_device_context = NULL;
    LRETURN;
  }

  p_device_context = (ni_device_context_t *)malloc(sizeof(ni_device_context_t));
  if (NULL == p_device_context)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() malloc() ni_device_context_t: %s\n",
             __func__, strerror(NI_ERRNO));
      p_device_context = NULL;
      LRETURN;
  }

  strncpy(p_device_context->shm_name, shm_name, sizeof(p_device_context->shm_name));
  p_device_context->lock = mutex_handle;
  p_device_context->p_device_info = p_device_queue;

END:

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

        if (NULL != map_file_handle)
        {
            CloseHandle(map_file_handle);
        }
    } else
    {
        ReleaseMutex(mutex_handle);
        CloseHandle(map_file_handle);
    }

    return p_device_context;
}

#elif __linux__ || __APPLE__
jmp_buf shm_open_test_buf;

#if __APPLE__
#define DEV_NAME_PREFIX "rdisk"
#else
#define DEV_NAME_PREFIX "nvme"
#endif

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
int ni_rsrc_get_local_device_list(char   ni_devices[][NI_MAX_DEVICE_NAME_LEN],
                                  int    max_handles)
{
  /* list all XCoder devices under /dev/.. */
  const char* dir_name = "/dev";
  char dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN];
  int i, num_dev = 0, xcoder_device_cnt = 0;
  DIR* FD;
  struct dirent* in_file;
  ni_device_info_t device_info;
  ni_device_capability_t device_capabilites;
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE;
  ni_retcode_t rc;

  if ((ni_devices == NULL)||(max_handles == 0))
  {
    ni_log(NI_LOG_ERROR, "ERROR: bad input parameters\n");
    return 0;
  }

  if (NULL == (FD = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "ERROR: failed to open directory %s\n", dir_name);
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
    if (!strncmp(in_file->d_name, DEV_NAME_PREFIX, strlen(DEV_NAME_PREFIX)))
    {
      for (i = strlen(DEV_NAME_PREFIX); i < lenstr; i++)
      {
        if (!isdigit(in_file->d_name[i]))
        {
          goto read_next;
        }
      }

      strcpy(dev_names[num_dev], in_file->d_name);
      num_dev++;
    read_next: ;
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
      int ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN - 6,
                         "/dev/%s", dev_names[i]);
      if (ret < 0)
      {
        return -1;
      }
      memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

      ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
      dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);

      if (NI_INVALID_DEVICE_HANDLE != dev_handle)
      {
        rc = ni_device_capability_query(dev_handle,&device_capabilites);
        if (NI_RETCODE_SUCCESS == rc)
        {
            if (is_supported_xcoder(device_capabilites.device_is_xcoder))
            {
                ni_devices[xcoder_device_cnt][0] = '\0';
                strcat(ni_devices[xcoder_device_cnt], device_info.dev_name);
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
  if (0 != access(LOCK_DIR, 0))
  {
      if (0 != mkdir(LOCK_DIR, 777))
      {
          ni_log(NI_LOG_ERROR, "ERROR: Could not create the %s directory",
                 LOCK_DIR);
          return NULL;
      }
  }
#endif

  lock = open(CODERS_LCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

  if (lock < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() open() CODERS_LCK_NAME: %s\n", __func__,
             strerror(NI_ERRNO));
      return NULL;
  }
  lockf(lock, F_LOCK, 0);

#ifdef _ANDROID
  /*! return if init has already been done */
  int ret = ni_rsrc_android_init();
  if (service == NULL)
  {
      ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_pool Error service ..\n");
      return NULL;
  }

  string param = CODERS_SHM_NAME;
  Return<void> retvalue =
      service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
          ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
          if (ret > 0)
          {
              shm_fd = dup(handle->data[0]);
              ni_log(NI_LOG_INFO, "vendor:GetAppFlag shm_fd:%d\n", shm_fd);

          } else
          {
              ni_log(NI_LOG_ERROR, "Error %d: shm_get shm_fd ..\n",
                     NI_ERRNO);
          }
      });
  if (!retvalue.isOk())
  {
      ni_log(NI_LOG_ERROR, "service->GetAppFlag ret failed ..\n");
      LRETURN;
  }
  if (shm_fd <= 0)
  {
      shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
      if (shm_fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = shm_fd;
          service->SetAppFlag(param, handle);
          ni_log(NI_LOG_ERROR, "Create shm fd %d\n", shm_fd);
      }
  }
#else
  shm_fd =
      shm_open(CODERS_SHM_NAME, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif
  if (shm_fd < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() shm_open() CODERS_SHM_NAME: %s\n",
             __func__, strerror(NI_ERRNO));
      LRETURN;
  }

  p_device_queue = (ni_device_queue_t*)mmap(0, sizeof(ni_device_queue_t), PROT_READ | PROT_WRITE,
    MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() mmap() ni_device_queue_t: %s\n",
             __func__, strerror(NI_ERRNO));
      LRETURN;
  }

  p_device_pool = (ni_device_pool_t *)malloc(sizeof(ni_device_pool_t));
  if (NULL == p_device_pool)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() malloc() ni_device_pool_t: %s\n",
             __func__, strerror(NI_ERRNO));
      munmap(p_device_queue, sizeof(ni_device_queue_t));
  }
  else
  {
    p_device_pool->lock = lock;
    p_device_pool->p_device_queue = p_device_queue;
  }

END:

    lockf(lock, F_ULOCK, 0);

    if (NULL == p_device_pool)
    {
      close(lock);
    }
#ifndef _ANDROID
    close(shm_fd);
#endif

    return p_device_pool;
}

#ifndef _ANDROID

/*!******************************************************************************
 *  \brief   This function handles SIGBUS signal, it restores the stack and
 *           jumps back.
 *
 *  \param[in]   sig   Signal to be handled
 *
 *  \return
 *           none
 *
 *******************************************************************************/
static void handle_sigbus(int sig)
{
    siglongjmp(shm_open_test_buf, sig);
}

static void rm_shm_files()
{
    // remove shm files
    // Q058279 verifies that these dont exist
    int temp_ret = system("rm -f " LOCK_DIR "/NI_SHM_CODERS");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_LCK_CODERS");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_RETRY_LCK_DECODERS");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_RETRY_LCK_SCALERS");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_RETRY_LCK_ENCODERS");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_RETRY_LCK_AI");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_lck_*");
    (void)temp_ret;
    temp_ret = system("rm -f " LOCK_DIR "/NI_shm_*");
    (void)temp_ret;
}

/*!******************************************************************************
 *  \brief   This function sets up a SIGBUS handler, opens the the file and
 *           does a test. If the read results in SIGBUS the handler
 *           cleans up the LOCK_DIR
 *
 *  \return
 *           file descriptor on success
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
static int shm_open_and_test()
{
    struct sigaction new_act, old_act;
    bool pool_locked = false;
    int cleanup = 0;
    // save the old handler
    sigaction(SIGINT, NULL, &old_act);

    new_act.sa_handler = &handle_sigbus;
    new_act.sa_flags = SA_NODEFER;
    sigemptyset(&new_act.sa_mask);
    // assign the new handler
    sigaction(SIGBUS, &new_act, NULL);

    int shm_fd = shm_open(CODERS_SHM_NAME, O_RDWR,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (shm_fd < 0)
    {
        // restore the old handler
        sigaction(SIGBUS, &old_act, NULL);
        return shm_fd;
    }
    ni_device_pool_t *p_device_pool = NULL;
    p_device_pool = ni_rsrc_get_device_pool();
    if (NULL == p_device_pool)
    {
        // restore the old handler
        sigaction(SIGBUS, &old_act, NULL);
        return NI_RETCODE_FAILURE;
    }

    lockf(p_device_pool->lock, F_LOCK, 0);
    pool_locked = true;

    ni_device_queue_t *p_device_queue = NULL;
    p_device_queue = p_device_pool->p_device_queue;

    // test the file and sig handle
    if (0 == (cleanup = sigsetjmp(shm_open_test_buf, 0)))
    {
        //volatile to escape optimization, without this there were some hangs 
        //when reading the following field later
        volatile int count = 0;
        count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_DECODER];
        cleanup |= count == 0 ? 1 : 0;
        ni_log(NI_LOG_DEBUG, "DEBUG: Decoder cnt = %d/*\n", count);

        count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
        cleanup |= count == 0 ? 1 : 0;
        ni_log(NI_LOG_DEBUG, "DEBUG: Encoder cnt = %d/*\n", count);

        count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_SCALER];
        cleanup |= count == 0 ? 1 : 0;
        ni_log(NI_LOG_DEBUG, "DEBUG: Scaler cnt = %d/*\n", count);

        count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_AI];
        cleanup |= count == 0 ? 1 : 0;
        ni_log(NI_LOG_DEBUG, "DEBUG: AI cnt = %d/*\n", count);
        
        count = p_device_queue->xcoders[NI_DEVICE_TYPE_AI][NI_MAX_DEVICE_CNT-1];
        (void)count;
    } 
    if (1 == cleanup)
    {
        ni_log(NI_LOG_ERROR, "ERROR: Caught a SIGBUS or invalid device count! Removing files in %s/*\n",
               LOCK_DIR);
        lockf(p_device_pool->lock, F_ULOCK, 0);
        pool_locked = false;
        close(shm_fd);
        rm_shm_files();

        // call again to set errno
        shm_fd = shm_open(CODERS_SHM_NAME, O_RDWR,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    }
    // restore the old handler
    sigaction(SIGBUS, &old_act, NULL);

    if (true == pool_locked)
    {
        lockf(p_device_pool->lock, F_ULOCK, 0);
    }
    ni_rsrc_free_device_pool(p_device_pool);

    return shm_fd;
}
#endif

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
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init(int should_match_rev, int timeout_seconds)
{
#define SLEEPLOOP 3
  /*! list all XCoder devices under /dev/.. */
  const char* dir_name = "/dev";
  char dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
  int i, j, k, num_dev = 0, xcoder_device_cnt = 0, fw_ver_compat_warning = 0;
  int ret = 0;
  int runtime = 0;
  DIR* FD;
  struct dirent* in_file;
  ni_device_info_t device_info;
  ni_device_queue_t* p_device_queue;
  ni_lock_handle_t lock = NI_INVALID_DEVICE_HANDLE;
  ni_lock_handle_t lockxc = NI_INVALID_DEVICE_HANDLE;
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE;
  ni_device_capability_t device_capabilites;
  ni_retcode_t rc;
  uint32_t xcoder_guid[NI_DEVICE_TYPE_XCODER_MAX] = {0};

  /*! return if init has already been done */
#ifdef _ANDROID
  ret = ni_rsrc_android_init();

  if (0 != access(LOCK_DIR, 0))
  {
      if (0 != mkdir(LOCK_DIR, 777))
      {
          ni_log(NI_LOG_ERROR, "ERROR: Could not create the %s directory",
                 LOCK_DIR);
          return 1;
      }
  }

  if (service == NULL)
  {
      ni_log(NI_LOG_ERROR, "ni_rsrc_init 000 Error service ..\n");
      return NI_RETCODE_FAILURE;
  }

  int32_t shm_fd = 0;
  string param = CODERS_SHM_NAME;
  Return<void> retvalue =
      service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
          ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
          if (ret > 0)
          {
              shm_fd = dup(handle->data[0]);
              ni_log(NI_LOG_INFO, "vendor:GetAppFlag shm_fd:%d\n", shm_fd);
          } else
          {
              ni_log(NI_LOG_ERROR, "Error %d: shm_get shm_fd ..\n",
                     NI_ERRNO);
          }
      });
  if (!retvalue.isOk())
  {
      ni_log(NI_LOG_ERROR, "service->GetAppFlag ret failed ..\n");
      return NI_RETCODE_FAILURE;
  }
  if (shm_fd <= 0)
  {
      shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
      if (shm_fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = shm_fd;
          service->SetAppFlag(param, handle);
          ni_log(NI_LOG_ERROR, "Create shm fd %d\n", shm_fd);
      }
  }
#else
  int shm_fd = shm_open_and_test();
  if (shm_fd >= 0)
  {
    ni_log(NI_LOG_INFO, "NI resource init'd already ..\n");
    close(shm_fd);
    return 0;
  }
  else
  {
    if (ENOENT == NI_ERRNO)
    {
      ni_log(NI_LOG_INFO, "NI resource not init'd, continue ..\n");
    }
    else
    {
        ni_log(NI_LOG_ERROR, "ERROR: cannot access NI resources: %s\n",
               strerror(NI_ERRNO));
        return 1;
    }
  }
#endif

read_dev_files:
  num_dev = 0;

  if (NULL == (FD = opendir(dir_name)))
  {
    ni_log(NI_LOG_ERROR, "ERROR: failed to open directory %s\n", dir_name);
    return 1;
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
    if (!strncmp(in_file->d_name, DEV_NAME_PREFIX, strlen(DEV_NAME_PREFIX)))
    {
      for (i = strlen(DEV_NAME_PREFIX); i < lenstr; i++)
      {
        if (!isdigit(in_file->d_name[i]))
        {
          goto read_next;
        }
      }

      ni_log(NI_LOG_INFO, "Reading device file: %s\n", in_file->d_name);
      strcpy(dev_names[num_dev], in_file->d_name);
      num_dev++;
      read_next:;
    }
  }
  closedir(FD);
  if (num_dev == 0)
  {
    ni_log(NI_LOG_INFO, "NVMe Devices not ready, wait ..\n");
    if (g_xcoder_stop_process)
    {
      ni_log(NI_LOG_ERROR, "Requested to stop ..\n");
      return 1;
    }

    runtime += SLEEPLOOP;
    sleep(SLEEPLOOP);
    if (runtime >= timeout_seconds && timeout_seconds != 0)
    {
        ni_log(NI_LOG_ERROR, "Timeout reached at %d seconds! Failing\n",
               runtime);
        return 1;
    }
    goto read_dev_files;
  }

  qsort(dev_names, num_dev, sizeof(dev_names[0]), ni_rsrc_strcmp);

  /*! go through the NVMe devices to check if there is any Xcoders with supported FW */
  ni_log(NI_LOG_INFO, "Compatible FW API ver: %c%c\n",
         NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
         NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX]);
  xcoder_device_cnt = 0;
  uint32_t tmp_io_size;
  for (i = 0; i < num_dev; i++)
  {
    memset(&device_info, 0, sizeof(ni_device_info_t));
    ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN - 6, "/dev/%s",
                   dev_names[i]);
    if (ret < 0)
    {
      return -1;
    }
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    ni_log(NI_LOG_INFO, "Block name %s\n", device_info.blk_name);
    dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);

    if (NI_INVALID_DEVICE_HANDLE != dev_handle)
    {
      rc = ni_device_capability_query(dev_handle, &device_capabilites);
      if (NI_RETCODE_SUCCESS == rc)
      {
          if (is_supported_xcoder(device_capabilites.device_is_xcoder) &&
              (!should_match_rev ||
               ni_is_fw_compatible(device_capabilites.fw_rev)))
          {
              ni_log(NI_LOG_INFO, "%d. %s  num_hw: %d\n", i + 1, device_info.dev_name,
                     device_capabilites.hw_elements_cnt);
              xcoder_device_cnt++;
          } else
        {
            ni_log(NI_LOG_INFO, "Device %s not added as it is not a supported "
                   "xcoder %u, or has incompatible FW rev: %.*s\n",
                   device_info.dev_name, device_capabilites.device_is_xcoder,
                   (int) sizeof(device_capabilites.fw_rev),
                   device_capabilites.fw_rev);
        }
      }

      ni_device_close(dev_handle);
    }
  }

  if (0 == xcoder_device_cnt)
  {
    ni_log(NI_LOG_INFO, "NVMe Devices supporting XCoder not ready, wait ..\n");
    if (g_xcoder_stop_process)
    {
      ni_log(NI_LOG_ERROR, "Requested to stop ..\n");
      return 1;
    }
    runtime += SLEEPLOOP;
    sleep(SLEEPLOOP);
    ni_log(NI_LOG_INFO, "runtime at %d seconds! Timeout at %d \n", runtime, timeout_seconds);
    if (runtime >= timeout_seconds && timeout_seconds != 0)
    {
        ni_log(NI_LOG_ERROR, "Timeout reached at %d seconds! Failing\n",
               runtime);
        return 1;
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
  ret = ni_rsrc_android_init();
  if (service == NULL)
  {
      ni_log(NI_LOG_ERROR, "ni_rsrc_init 111 Error service ..\n");
      return NI_RETCODE_FAILURE;
  }

  param = CODERS_SHM_NAME;
  retvalue = service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
      ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
      if (ret > 0)
      {
          shm_fd = dup(handle->data[0]);
          ni_log(NI_LOG_INFO, "vendor:GetAppFlag shm_fd:%d\n", shm_fd);
      } else
      {
          ni_log(NI_LOG_ERROR, "Error %d: shm_get shm_fd ..\n",
                 NI_ERRNO);
      }
  });

  if (!retvalue.isOk())
  {
      ni_log(NI_LOG_ERROR, "service->GetAppFlag ret failed ..\n");
      return NI_RETCODE_FAILURE;
  }

  if (shm_fd <= 0)
  {
      shm_fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
      if (shm_fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = shm_fd;
          service->SetAppFlag(param, handle);
          ni_log(NI_LOG_ERROR, "Create shm fd %d\n", shm_fd);
      }
  }
#else
  shm_fd = shm_open(CODERS_SHM_NAME, O_CREAT | O_RDWR,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif

  if (shm_fd < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() shm_open() CODERS_SHM_NAME: %s\n",
             __func__, strerror(NI_ERRNO));
      return 1;
  }

#ifndef _ANDROID
  if (ftruncate(shm_fd, sizeof(ni_device_queue_t)) < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() ftruncate() shm_fd: %s\n", __func__,
           strerror(NI_ERRNO));
    close(shm_fd);
    return 1;
  }
#endif

  p_device_queue = (ni_device_queue_t*)mmap(0, sizeof(ni_device_queue_t), PROT_READ | PROT_WRITE,
      MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() mmap() ni_device_queue_t: %s\n", __func__,
             strerror(NI_ERRNO));
    close(shm_fd);
    return 1;
  }

#ifndef _ANDROID
  close(shm_fd);
#endif

  /*! create the lock */
  lock = open(CODERS_LCK_NAME, O_RDWR | O_CREAT | O_CLOEXEC,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() open() CODERS_LCK_NAME: %s\n", __func__,
           strerror(NI_ERRNO));
    munmap(p_device_queue, sizeof(ni_device_queue_t));
    return 1;
  }

  // init the ni_device_queue_t
  for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
  {
      /*! create the lock for xcoder*/
      lockxc = open(XCODERS_RETRY_LCK_NAME[k], O_RDWR | O_CREAT | O_CLOEXEC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      if (lockxc < 0)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s() open() XCODERS_RETRY_LCK_NAME[%d]: "
                 "%s\n", __func__, k, strerror(NI_ERRNO));
          munmap(p_device_queue, sizeof(ni_device_queue_t));
          return 1;
      }

      p_device_queue->xcoder_cnt[k] = 0;
      for (i = 0; i < NI_MAX_DEVICE_CNT; i++)
      {
          p_device_queue->xcoders[k][i] = -1;
      }
  }

  for (i = 0; i < num_dev; i++)
  {
    ni_log(NI_LOG_INFO, "%d. %s\n", i, dev_names[i]);

    /*! retrieve decoder and encoder info and create shared memory
       and named lock accordingly, using NVMe "Identify Controller" */
       /*! check whether Xcoder is supported and retrieve the xcoder info */
    ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN - 6, "/dev/%s",
                   dev_names[i]);
    if (ret < 0)
    {
      return -1;
    }
    memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    dev_handle = ni_device_open(device_info.blk_name, &tmp_io_size);

    if (NI_INVALID_DEVICE_HANDLE != dev_handle)
    {
      rc = ni_device_capability_query(dev_handle, &device_capabilites);
      if ((NI_RETCODE_SUCCESS == rc) &&
          (is_supported_xcoder(device_capabilites.device_is_xcoder)) &&
          (!should_match_rev || ni_is_fw_compatible(device_capabilites.fw_rev)))
      {
        fw_ver_compat_warning = 0;
        if (ni_is_fw_compatible(device_capabilites.fw_rev) == 2)
        {
          ni_log(NI_LOG_INFO, "WARNING - Query %s %s FW version: %.*s is below "
                 "the minimum support version for this SW version. Some "
                 "features may be missing.\n", device_info.dev_name,
                 device_info.blk_name, (int) sizeof(device_capabilites.fw_rev),
                 device_capabilites.fw_rev);
          fw_ver_compat_warning = 1;
        }

        int total_modules = device_capabilites.xcoder_devices_cnt;

        ni_device_info_t* p_device_info = NULL;

        for (j = 0; j < total_modules; j++)
        {
          p_device_info = NULL;

          memset(&device_info, 0, sizeof(ni_device_info_t));
          ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN - 6,
                         "/dev/%s", dev_names[i]);
          if (ret < 0)
          {
            return -1;
          }
          ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));

          device_info.hw_id = device_capabilites.xcoder_devices[j].hw_id;
          device_info.fw_ver_compat_warning = fw_ver_compat_warning;
          memcpy(device_info.serial_number, device_capabilites.serial_number,
                 sizeof(device_info.serial_number));
          memcpy(device_info.model_number, device_capabilites.model_number,
                 sizeof(device_info.model_number));
          memcpy(device_info.fw_rev, device_capabilites.fw_rev,
                 sizeof(device_info.fw_rev));
          memcpy(device_info.fw_branch_name, device_capabilites.fw_branch_name,
                 sizeof(device_info.fw_branch_name) - 1);
          memcpy(device_info.fw_commit_time, device_capabilites.fw_commit_time,
                 sizeof(device_info.fw_commit_time) - 1);
          memcpy(device_info.fw_commit_hash, device_capabilites.fw_commit_hash,
                 sizeof(device_info.fw_commit_hash) - 1);
          memcpy(device_info.fw_build_time, device_capabilites.fw_build_time,
                 sizeof(device_info.fw_build_time) - 1);
          memcpy(device_info.fw_build_id, device_capabilites.fw_build_id,
                 sizeof(device_info.fw_build_id) - 1);

          device_info.max_fps_4k =
              device_capabilites.xcoder_devices[j].max_4k_fps;
          device_info.max_instance_cnt = device_capabilites.xcoder_devices[j].max_number_of_contexts;
          device_info.device_type = (ni_device_type_t)device_capabilites.xcoder_devices[j].codec_type;

          int device_cnt_so_far =
              p_device_queue->xcoder_cnt[device_info.device_type];
          int tmp_guid = -1;
          // check if entry has been created for this h/w (hw_id):
          // if not, then create a new entry; otherwise just update it
          for (k = 0; k < device_cnt_so_far; k++)
          {
              tmp_guid = p_device_queue->xcoders[device_info.device_type][k];
              ni_device_context_t *p_device_context =
                  ni_rsrc_get_device_context(device_info.device_type, tmp_guid);
              if (p_device_context &&
                  strcmp(p_device_context->p_device_info->dev_name,
                         device_info.dev_name) == 0 &&
                  p_device_context->p_device_info->hw_id == device_info.hw_id)
              {
                  p_device_info = p_device_context->p_device_info;
                  break;
              }
            ni_rsrc_free_device_context(p_device_context);
          }

          ni_codec_t fmt = (ni_codec_t)device_capabilites.xcoder_devices[j].codec_format;

          if (p_device_info)
          {
              ni_log(NI_LOG_INFO, "%s h/w id %d update\n",
                     device_type_str[device_info.device_type],
                     device_info.hw_id);
              ni_rsrc_fill_device_info(p_device_info, fmt,
                                       device_info.device_type,
                                       &device_capabilites.xcoder_devices[j]);
          }
          else
          {
              ni_log(NI_LOG_INFO, "%s h/w id %d create\n",
                     device_type_str[device_info.device_type],
                     device_info.hw_id);
              p_device_info = &device_info;

              rc = ni_rsrc_fill_device_info(
                  p_device_info, fmt, device_info.device_type,
                  &device_capabilites.xcoder_devices[j]);

              if (rc == 0)
              {
                  /*! add the h/w device_info entry */
                  p_device_info->module_id =
                      xcoder_guid[device_info.device_type]++;
                  p_device_queue->xcoder_cnt[device_info.device_type] =
                      xcoder_guid[device_info.device_type];
                  p_device_queue->xcoders[device_info.device_type]
                                         [p_device_info->module_id] =
                      p_device_info->module_id;
                  ni_rsrc_get_one_device_info(&device_info);
              }
          }

        } /*! for each device_info module */
      }   /*! if device supports xcoder */
      else
      {
          ni_log(NI_LOG_INFO, "Device %s not added as %u is not a supported "
                 "xcoder, or has incompatible FW rev: %.*s\n",
                 device_info.dev_name, device_capabilites.device_is_xcoder,
                 (int) sizeof(device_capabilites.fw_rev),
                 device_capabilites.fw_rev);
      }

      ni_device_close(dev_handle);
    }
  } /*! for each nvme device */

  for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
  {
      p_device_queue->xcoder_cnt[k] = xcoder_guid[k];
  }
  return 0;
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
  int shm_fd = 0;
  int lock;
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  ni_device_context_t *p_device_context = NULL;
  ni_device_info_t *p_device_queue = NULL;

  ni_rsrc_get_shm_name(device_type, guid, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(device_type, guid, lck_name, sizeof(lck_name));

#ifdef _ANDROID
  lock =
      open(lck_name, O_CREAT | O_RDWR | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO);
#else
  lock =
      open(lck_name, O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif
  if (lock < 1)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() open() %s: %s\n", __func__, lck_name,
           strerror(NI_ERRNO));
    return NULL;
  }

  lockf(lock, F_LOCK, 0);

#ifdef _ANDROID
  int ret = ni_rsrc_android_init();
  if (service == NULL)
  {
      ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_context Error service ..\n");
      return NULL;
  }

  string param = shm_name;
  Return<void> retvalue =
      service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
          ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
          if (ret > 0)
          {
              shm_fd = dup(handle->data[0]);
              ni_log(NI_LOG_INFO, "vendor:GetAppFlag shm_fd:%d\n", shm_fd);
          } else
          {
              ni_log(NI_LOG_ERROR, "Error %d: shm_get shm_fd ..\n",
                     NI_ERRNO);
          }
      });

  if (!retvalue.isOk())
  {
      ni_log(NI_LOG_ERROR, "service->GetAppFlag ret failed ..\n");
      LRETURN;
  }

  if (shm_fd <= 0)
  {
      shm_fd = ashmem_create_region(shm_name, sizeof(ni_device_info_t));
      if (shm_fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = shm_fd;
          service->SetAppFlag(param, handle);
          ni_log(NI_LOG_ERROR, "Create shm fd %d\n", shm_fd);
      }
  }
#else
  shm_fd = shm_open(shm_name, O_CREAT | O_RDWR,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif
  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() shm_open() %s: %s\n", __func__, shm_name,
           strerror(NI_ERRNO));
    LRETURN;
  }

  p_device_queue = (ni_device_info_t *)mmap(0, sizeof(ni_device_info_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_device_queue)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() mmap() ni_device_info_t: %s\n", __func__,
           strerror(NI_ERRNO));
    LRETURN;
  }

  p_device_context = (ni_device_context_t *)malloc(sizeof(ni_device_context_t));
  if (!p_device_context)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() malloc() ni_device_context_t: %s\n",
           __func__, strerror(NI_ERRNO));
    munmap((void *)p_device_queue, sizeof(ni_device_info_t));
    LRETURN;
  }

  strncpy(p_device_context->shm_name, shm_name, sizeof(shm_name));
  p_device_context->lock = lock;
  p_device_context->p_device_info = p_device_queue;

END:

    lockf(lock, F_ULOCK, 0);

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
    ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
    close(p_device_context->lock);
    munmap((void *)p_device_context->p_device_info, sizeof(ni_device_info_t));
#endif
    free(p_device_context);
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
    ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_list_devices() failed to obtain "
           "mutex: %p\n", p_device_pool->lock);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
#elif __linux__ || __APPLE__
  lockf(p_device_pool->lock, F_LOCK, 0);
#endif

  b_release_pool_mtx = true;

  p_device_queue = p_device_pool->p_device_queue;
  count = p_device_queue->xcoder_cnt[device_type];

  *p_device_count=0;
  for (i = 0; i < count; i++)
  {
    int guid = -1;
    guid = p_device_queue->xcoders[device_type][i];
    p_device_context = ni_rsrc_get_device_context(device_type, guid);
    if (p_device_context)
    {
#ifdef _WIN32
      if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
      {
        ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_list_devices() failed to obtain "
               "mutex: %p\n", p_device_context->lock);
        ReleaseMutex(p_device_pool->lock);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }

      memcpy(&p_device_info[i], p_device_context->p_device_info, sizeof(ni_device_info_t));
      ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
      lockf(p_device_context->lock, F_LOCK, 0);
      memcpy(&p_device_info[i], p_device_context->p_device_info, sizeof(ni_device_info_t));
      lockf(p_device_context->lock, F_ULOCK, 0);
#endif

      ni_rsrc_free_device_context(p_device_context);

      (*p_device_count)++;
    }
    else
    {
        ni_log(NI_LOG_ERROR, "ERROR: cannot find decoder guid: %d\n", guid);
    }
  }

END:

    if (b_release_pool_mtx)
    {
#ifdef _WIN32
      ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
      lockf(p_device_pool->lock, F_ULOCK, 0);
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
    int k = 0;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (NULL == p_device)
    {
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
    {
        retval = ni_rsrc_list_devices((ni_device_type_t)k, p_device->xcoders[k],
                                      &(p_device->xcoder_cnt[k]));
        if (NI_RETCODE_FAILURE == retval)
        {
            ni_log(NI_LOG_ERROR, "ERROR: could not retrieve info for %d type "
                   "devices\n", k);
            LRETURN;
        }
    }

END:

    return retval;
}

/*!******************************************************************************
*  \brief        Grabs information for every initialized and uninitialized
*                device.

*   \param       list_uninitialized Flag to determine if uninitialized devices
*                                   should be grabbed.
*
*   \return
*                NI_RETCODE_SUCCESS
*                NI_RETCODE_INVALID_PARAM
*                NI_RETCODE_FAILURE
*
*   Note: Caller is responsible for allocating memory for "p_device".
*******************************************************************************/
LIB_API ni_retcode_t ni_rsrc_list_all_devices2(ni_device_t* p_device, bool list_uninitialized)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    if (!p_device)
    {
        retval = NI_RETCODE_INVALID_PARAM;
        return retval;
    }

    /* Grab initialized devices. */

    ni_log_level_t log_level = ni_log_get_level();

    if (list_uninitialized)
    {
        ni_log_set_level(NI_LOG_NONE);
    }

    ni_rsrc_list_all_devices(p_device);

    if (!list_uninitialized)
    {
        return retval;
    }

    ni_log_set_level(log_level);

    /* Store device names of initialized devices. */

    ni_device_info_t *p_dev_info;
    char initialized_dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};

    for (int dev_index = 0;
         dev_index < p_device->xcoder_cnt[NI_DEVICE_TYPE_DECODER]; dev_index++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_DECODER][dev_index];
        strcpy(initialized_dev_names[dev_index], p_dev_info->dev_name);
    }

    /* Retrieve uninitialized devices. */

    char dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
    int dev_count = ni_rsrc_get_local_device_list(dev_names, NI_MAX_DEVICE_CNT);

    uint32_t tmp_io_size;
    ni_device_capability_t capability;
    ni_device_handle_t fd;
    ni_device_info_t dev_info;

    for (int dev_index = 0; dev_index < dev_count; dev_index++)
    {
        if (is_str_in_str_array(dev_names[dev_index],
                                initialized_dev_names, NI_MAX_DEVICE_CNT))
        {
            continue;
        }

        memset(&dev_info, 0, sizeof(ni_device_info_t));

        strcpy(dev_info.dev_name, dev_names[dev_index]);

        retval = ni_find_blk_name(dev_info.dev_name, dev_info.blk_name,
                                  sizeof(dev_info.blk_name));
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(NI_LOG_ERROR, "Failed to find block device ID for %s\n",
                   dev_info.dev_name);
            return retval;
        }

        fd = ni_device_open(dev_info.blk_name, &tmp_io_size);
        if (NI_INVALID_DEVICE_HANDLE == fd)
        {
            ni_log(NI_LOG_ERROR, "Failed to open device: %s\n",
                   dev_info.dev_name);
            return NI_RETCODE_FAILURE;
        }

        retval = ni_device_capability_query(fd, &capability);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_device_close(fd);
            ni_log(NI_LOG_ERROR, "Failed to query device capability: %s\n",
                   dev_info.dev_name);
            return retval;
        }

        for (int dev_type = 0; dev_type < NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            p_device->xcoder_cnt[dev_type]++;

            p_dev_info = &p_device->xcoders[dev_type][dev_index];
            memcpy(p_dev_info->serial_number, capability.serial_number,
                   sizeof(capability.serial_number));
            memcpy(p_dev_info->model_number, capability.model_number,
                   sizeof(capability.model_number));
            memcpy(p_dev_info->fw_rev, capability.fw_rev,
                   sizeof(capability.fw_rev));
            if (ni_is_fw_compatible(capability.fw_rev) == 2)
            {
                p_dev_info->fw_ver_compat_warning = 1;
            }
            memcpy(p_dev_info->fw_branch_name, capability.fw_branch_name,
                   sizeof(capability.fw_branch_name));
            memcpy(p_dev_info->fw_commit_time, capability.fw_commit_time,
                   sizeof(capability.fw_commit_time));
            memcpy(p_dev_info->fw_commit_hash, capability.fw_commit_hash,
                   sizeof(capability.fw_commit_hash));
            memcpy(p_dev_info->fw_build_time, capability.fw_build_time,
                   sizeof(capability.fw_build_time));
            memcpy(p_dev_info->fw_build_id, capability.fw_build_id,
                   sizeof(capability.fw_build_id));
            memcpy(p_dev_info->dev_name, dev_info.dev_name,
                   sizeof(dev_info.dev_name));
            memcpy(p_dev_info->blk_name, dev_info.blk_name,
                   sizeof(dev_info.blk_name));
            p_dev_info->device_type = (ni_device_type_t)dev_type;
            p_dev_info->module_id = -1; /* special value to indicate device is
                                           not initialized */
        }

        ni_device_close(fd);
    }

    return retval;
}


void ni_rsrc_print_device_info(const ni_device_info_t *p_device_info)
{
    int i;

    if (!p_device_info)
    {
        ni_log(NI_LOG_ERROR, "ERROR: Cannot print device info!\n");
    } else
    {
        ni_log(NI_LOG_INFO, " %s #%d\n",
               device_type_str[p_device_info->device_type],
               p_device_info->module_id);
        ni_log(NI_LOG_INFO, "  H/W ID: %d\n", p_device_info->hw_id);
        ni_log(NI_LOG_INFO, "  MaxNumInstances: %d\n",
               p_device_info->max_instance_cnt);

        if (NI_DEVICE_TYPE_SCALER == p_device_info->device_type)
        {
            ni_log(NI_LOG_INFO, "  Capabilities:\n");
            ni_log(NI_LOG_INFO,
                   "    Operations: Crop (ni_quadra_crop), Scale (ni_quadra_scale), Pad "
                   "(ni_quadra_pad), Overlay (ni_quadra_overlay)\n");
        } else if (NI_DEVICE_TYPE_AI == p_device_info->device_type)
        {
            ni_log(NI_LOG_INFO, "  Capabilities:\n");
            ni_log(
                NI_LOG_INFO,
                "    Operations: ROI (ni_quadra_roi), Background Replace (ni_quadra_bg)\n");
        } else if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type ||
                   NI_DEVICE_TYPE_ENCODER == p_device_info->device_type)
        {
            ni_log(NI_LOG_INFO, "  Max4KFps: %d\n", p_device_info->max_fps_4k);
            for (i = 0; i < EN_CODEC_MAX; i++)
            {
                if (EN_INVALID != p_device_info->dev_cap[i].supports_codec)
                {
                    ni_log(NI_LOG_INFO, "  %s ",
                           ni_codec_format_str[p_device_info->dev_cap[i]
                                                   .supports_codec]);
                    ni_log(NI_LOG_INFO, "(%s) Capabilities:\n",
                           NI_DEVICE_TYPE_DECODER ==
                                   p_device_info->device_type ?
                               ni_dec_name_str[p_device_info->dev_cap[i]
                                                   .supports_codec] :
                               ni_enc_name_str[p_device_info->dev_cap[i]
                                                   .supports_codec]);
                    ni_log(NI_LOG_INFO, "    MaxResolution: %dx%d\n",
                           p_device_info->dev_cap[i].max_res_width,
                           p_device_info->dev_cap[i].max_res_height);
                    ni_log(NI_LOG_INFO, "    MinResolution: %dx%d\n",
                           p_device_info->dev_cap[i].min_res_width,
                           p_device_info->dev_cap[i].min_res_height);

                    // no profile for JPEG encode, or level for JPEG
                    if (! (NI_DEVICE_TYPE_ENCODER == p_device_info->device_type &&
                           EN_JPEG == p_device_info->dev_cap[i].supports_codec))
                    {
                        ni_log(NI_LOG_INFO, "    Profiles: %s\n",
                               p_device_info->dev_cap[i].profiles_supported);
                    }
                    if (EN_JPEG != p_device_info->dev_cap[i].supports_codec)
                    {
                        ni_log(NI_LOG_INFO, "    Level: %s\n",
                               p_device_info->dev_cap[i].level);
                    }
                }
            }
        }
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
    ni_device_t device = {0};

    if (NI_RETCODE_SUCCESS != ni_rsrc_list_all_devices(&device))
    {
        return;
    }

    print_device(&device);
}

/*!*****************************************************************************
*  \brief        Prints detailed capability information for all initialized
*                devices and general information about uninitialized devices.

*   \param       list_uninitialized Flag to determine if uninitialized devices
*                                   should be grabbed.
*
*   \return      none
*
*******************************************************************************/
LIB_API void ni_rsrc_print_all_devices_capability2(bool list_uninitialized)
{
    ni_device_t device = {0};

    if (NI_RETCODE_SUCCESS != ni_rsrc_list_all_devices2(&device, list_uninitialized))
    {
        return;
    }

    print_device(&device);
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
    LRETURN;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_get_device_info() failed to obtain "
           "mutex: %p\n", p_device_context->lock);
    free(p_device_info);
    LRETURN;
  }

  memcpy(p_device_info, p_device_context->p_device_info, sizeof(ni_device_info_t));
  ReleaseMutex(p_device_context->lock);
#elif __linux
  lockf(p_device_context->lock, F_LOCK, 0);

  memcpy(p_device_info, p_device_context->p_device_info, sizeof(ni_device_info_t));

  lockf(p_device_context->lock, F_ULOCK, 0);
#endif

END:

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
  uint32_t num_sw_instances = 0;
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
      ni_log(NI_LOG_ERROR, "ERROR: cannot get p_device_pool\n");
      return NI_RETCODE_FAILURE;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
      ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_get_available_device() failed to "
             "obtain mutex: %p\n", p_device_pool->lock);
      ni_rsrc_free_device_pool(p_device_pool);
      return NI_RETCODE_FAILURE;
  }
#elif __linux__ || __APPLE__
  lockf(p_device_pool->lock, F_LOCK, 0);
#endif

  num_coders = p_device_pool->p_device_queue->xcoder_cnt[device_type];

  int tmp_id;
  for (i = 0; i < num_coders; i++)
  {
      tmp_id = p_device_pool->p_device_queue->xcoders[device_type][i];
      p_device_context = ni_rsrc_get_device_context(device_type, tmp_id);

      p_session_context.blk_io_handle =
          ni_device_open(p_device_context->p_device_info->blk_name,
                         &p_session_context.max_nvme_io_size);
      p_session_context.device_handle = p_session_context.blk_io_handle;

      if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s() ni_device_open() %s: %s\n",
                 __func__, p_device_context->p_device_info->blk_name,
                 strerror(NI_ERRNO));
          ni_rsrc_free_device_context(p_device_context);
          continue;
      }

    p_session_context.hw_id = p_device_context->p_device_info->hw_id;

    rc = ni_device_session_query(&p_session_context, device_type);

    if (NI_INVALID_DEVICE_HANDLE != p_session_context.device_handle)
    {
      ni_device_close(p_session_context.device_handle);
    }

    if (NI_RETCODE_SUCCESS != rc)
    {
        ni_log(NI_LOG_ERROR, "ERROR: query %s %s.%d\n", device_type_str[device_type],
               p_device_context->p_device_info->dev_name,
               p_device_context->p_device_info->hw_id);
        ni_rsrc_free_device_context(p_device_context);
        continue;
    }

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_get_available_device() failed to obtain mutex: %p\n", p_device_context->lock);
        //ni_rsrc_free_device_pool(p_device_pool);
        //return -1;
    }
#elif __linux__ || __APPLE__
    lockf(p_device_context->lock, F_LOCK, 0);
#endif
    ni_rsrc_update_record(p_device_context, &p_session_context);

    p_dev_info = p_device_context->p_device_info;

    // here we select the best load
    // for decoder/encoder: check the model_load
    // for hwuploader: check directly hwupload count in query result
    if (NI_DEVICE_TYPE_UPLOAD == device_type)
    {
        if (i == 0 || p_session_context.load_query.active_hwuploaders < num_sw_instances)
        {
            guid = tmp_id;
            num_sw_instances = p_session_context.load_query.active_hwuploaders;
            memcpy(&dev_info, p_dev_info, sizeof(ni_device_info_t));
        }
    } else if (i == 0 || p_dev_info->model_load < least_model_load ||
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
#elif __linux__ || __APPLE__
    lockf(p_device_context->lock, F_ULOCK, 0);
#endif
    ni_rsrc_free_device_context(p_device_context);
  }

  // scaler and AI load handling will be considered in the future
  if (guid >= 0)
  {
    // calculate the load this stream will generate based on its resolution and
    // fps required
    p_device_context = ni_rsrc_get_device_context(device_type, guid);
    ni_rsrc_device_video_ref_cap_t refCap = g_device_reference_table[device_type][codec];
    if (refCap.fps == 0)
    {
      guid = -1;
    } else if (IS_XCODER_DEVICE_TYPE(device_type))
    {
      unsigned long total_cap = refCap.width * refCap.height * refCap.fps;
      unsigned long xcode_cap = width * height * frame_rate;
      if (xcode_cap + p_device_context->p_device_info->xcode_load_pixel > total_cap)
      {
          ni_log(NI_LOG_INFO, "Warning xcode cap: %lu (%.1f) + current load %lu (%.1f) "
                 "> total %lu (1) ..\n",
                 xcode_cap, (float)xcode_cap / (float)total_cap,
                 p_device_context->p_device_info->xcode_load_pixel,
                 (float)p_device_context->p_device_info->xcode_load_pixel /
                     (float)total_cap,
                 total_cap);
          guid = -1;
      }
    }
    else
    {
        float fRef = (float)(refCap.width * refCap.height * refCap.fps);
        float fLoad = (float)(width * height * frame_rate * 100);
        if ((fLoad / fRef) > 100.0)
        {
            guid = -1;
        }
    }
    ni_rsrc_free_device_context(p_device_context);
  }

#ifdef _WIN32
  ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
  lockf(p_device_pool->lock, F_ULOCK, 0);
#endif

  ni_rsrc_free_device_pool(p_device_pool);

  ni_log(NI_LOG_INFO, "Get %s for %dx%d fps %d : %d %s.%d\n",
         IS_XCODER_DEVICE_TYPE(device_type) ? device_type_str[device_type] :
                                              "UNKNOWN DEVICE",
         width, height, frame_rate, guid,
         guid == -1 ? "NULL" : dev_info.dev_name,
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
    ni_log(NI_LOG_ERROR, "ERROR: %s() invalid input pointers\n", __func__);
    return NI_RETCODE_FAILURE;
  }

#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
             __func__, p_device_context->lock);
      return NI_RETCODE_FAILURE;
  }
#elif __linux__ || __APPLE__
  lockf(p_device_context->lock, F_LOCK, 0);
#endif

  p_device_context->p_device_info->load = load;
  p_device_context->p_device_info->active_num_inst = sw_instance_cnt;
  for (i = 0; i < sw_instance_cnt; i++)
  {
    p_device_context->p_device_info->sw_instance[i] = sw_instance_info[i];
  }

#ifdef _WIN32
  ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
  lockf(p_device_context->lock, F_ULOCK, 0);
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

  ni_log(NI_LOG_INFO, "Moving %s %d to queue end ..\n", device_type_str[type], guid);

  coders = p_device_pool->p_device_queue->xcoders[type];
  count = p_device_pool->p_device_queue->xcoder_cnt[type];

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
*   \param[out] p_load    the p_load that will be generated by this encoding
*                         task. Returned *only* for encoder for now.
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
  uint32_t num_sw_instances = 0;
  int least_model_load = 0;
  unsigned long job_mload = 0;

  /*! retrieve the record and based on the allocation rule specified, find the
     least loaded or least number of s/w instances among the coders */
  p_device_pool = ni_rsrc_get_device_pool();
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n", __func__, p_device_pool->lock);
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    coders = p_device_pool->p_device_queue->xcoders[device_type];
    count = p_device_pool->p_device_queue->xcoder_cnt[device_type];

    i = 0;
    while (i < count)
    {
      /*! get the individual device_info info and check the load/num-of-instances */
      p_device_context = ni_rsrc_get_device_context(device_type, coders[i]);

      // p_first retrieve status from f/w and update storage

      p_session_context.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name, &p_session_context.max_nvme_io_size);
      p_session_context.device_handle = p_session_context.blk_io_handle;

      if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
      {
          ni_log(NI_LOG_ERROR, "ERROR %s() ni_device_open() %s: %s\n",
                 __func__, p_device_context->p_device_info->blk_name,
                 strerror(NI_ERRNO));
          ni_rsrc_free_device_context(p_device_context);
          i++;
          continue;
      }

      p_session_context.hw_id = p_device_context->p_device_info->hw_id;
      rc = ni_device_session_query(&p_session_context, device_type);

      ni_device_close(p_session_context.device_handle);

      if (NI_RETCODE_SUCCESS != rc)
      {
          ni_log(NI_LOG_ERROR, "ERROR: query %s %s.%d\n",
                 device_type_str[device_type],
                 p_device_context->p_device_info->dev_name,
                 p_device_context->p_device_info->hw_id);
          ni_rsrc_free_device_context(p_device_context);
          i++;
          continue;
      }

#ifdef _WIN32
      if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
      {
          ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
                 __func__, p_device_context->lock);
      }
#elif __linux__ || __APPLE__
      lockf(p_device_context->lock, F_LOCK, 0);
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
#elif __linux__ || __APPLE__
      lockf(p_device_context->lock, F_ULOCK, 0);
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
          ni_log(NI_LOG_ERROR, "ERROR: guid %d capacity is 0\n", guid);
          ni_rsrc_free_device_context(p_device_context);
          p_device_context = NULL;
          LRETURN;
        }
        else
        {
            job_mload = width * height * frame_rate;

#ifdef _WIN32
          if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
          {
              ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
                     __func__, p_device_context->lock);
          }
#elif __linux__ || __APPLE__
          lockf(p_device_context->lock, F_LOCK, 0);
#endif
          p_device_context->p_device_info->xcode_load_pixel += job_mload;
          // Remove as the value is getting from the FW
          //p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#ifdef _WIN32
          ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
          if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t),
                    MS_SYNC | MS_INVALIDATE))
          {
              ni_log(NI_LOG_ERROR, "ERROR %s() msync() %s: %s\n", __func__,
                     p_device_context->p_device_info, strerror(NI_ERRNO));
          }
          lockf(p_device_context->lock, F_ULOCK, 0);
#endif
        }
      }

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);
    }
    else
    {
      ni_log(NI_LOG_ERROR, "ERROR: %s() cannot find guid\n", __func__);
      p_device_context = NULL;
    }

  END:
#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_ULOCK, 0);
#endif
    ni_rsrc_free_device_pool(p_device_pool);
  }

  if (p_load)
  {
      *p_load = job_mload;
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
  unsigned long job_mload = 0;

  ni_device_context_t *p_device_context = ni_rsrc_get_device_context(device_type, guid);
  if (p_device_context)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
               __func__, p_device_context->lock);
    }
#elif __linux__ || __APPLE__
    lockf(p_device_context->lock, F_LOCK, 0);
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
          job_mload = width * height * frame_rate;
          p_device_context->p_device_info->xcode_load_pixel += job_mload;
          // Remove as the value is getting from the FW
          //p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#if __linux__ || __APPLE__
        if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t),
                  MS_SYNC | MS_INVALIDATE))
        {
            ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_context->"
                   "p_device_info: %s\n", __func__, strerror(NI_ERRNO));
        }
#endif
      }
    }

#ifdef _WIN32
    ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_context->lock, F_ULOCK, 0);
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
          ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
                 __func__, p_device_pool->lock);
      }

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);

      ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
      lockf(p_device_pool->lock, F_LOCK, 0);

      ni_rsrc_move_device_to_end_of_pool(device_type, guid, p_device_pool);

      lockf(p_device_pool->lock, F_ULOCK, 0);
#endif

      ni_rsrc_free_device_pool(p_device_pool);
    }
  }

  if (p_load)
  {
      *p_load = job_mload;
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
*   \param[in/out]  p_ctxt  the device context
*   \param[in]   load    the load value returned by allocate* functions
*
*   \return      None
*   THE API needs to be removed from this fild and related test needs to cleanup
*******************************************************************************/
void ni_rsrc_release_resource(ni_device_context_t *p_device_context,
                              unsigned long load)
{
#ifdef _WIN32
  if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE))  // no time-out interval) //we got the mutex
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
             __func__, p_device_context->lock);
  }
#elif __linux__ || __APPLE__
  lockf(p_device_context->lock, F_LOCK, 0);
#endif

  if (p_device_context->p_device_info->xcode_load_pixel < load)
  {
      ni_log(NI_LOG_INFO, "Warning: releasing resource load %lu > current load %lu\n", load,
             p_device_context->p_device_info->xcode_load_pixel);
  }
  else
  {
    p_device_context->p_device_info->xcode_load_pixel -= load;
    // Remove as the value is getting from the FW
    // p_device_context->p_device_info->model_load = (int)((double)(p_device_context->p_device_info->xcode_load_pixel) * 100 / total_cap);
#if __linux__ || __APPLE__
    if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_context->"
               "p_device_info: %s\n", __func__, strerror(NI_ERRNO));
    }
#endif
  }

#ifdef _WIN32
  ReleaseMutex(p_device_context->lock);
#elif __linux__ || __APPLE__
  lockf(p_device_context->lock, F_ULOCK, 0);
#endif
}

/*!*****************************************************************************
*   \brief      check the NetInt h/w device in resource pool on the host.
*
*   \param[in]  guid  the global unique device index in resource pool
*               device_type     NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*
*   \return
*               NI_RETCODE_SUCCESS
*******************************************************************************/
int ni_rsrc_check_hw_available(int guid, ni_device_type_t device_type)
{
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_context_t *p_device_ctx = NULL;
    ni_session_context_t session_ctx = {0};
    ni_xcoder_params_t api_param = {0};
    uint32_t max_nvme_io_size = 0;
    bool b_release_pool_mtx = false;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    int retry_cnt = 0;

    if (guid < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR invalid guid:%d\n", guid);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (!(NI_DEVICE_TYPE_DECODER == device_type ||
          NI_DEVICE_TYPE_ENCODER == device_type))
    {
        ni_log(NI_LOG_ERROR, "ERROR: Unknown device type:%d\n", device_type);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_device_session_context_init(&session_ctx);
    session_ctx.keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;
    session_ctx.src_bit_depth = 8;
    session_ctx.hw_id = guid;

    if (NI_DEVICE_TYPE_DECODER == device_type)
    {
        if (ni_decoder_init_default_params(&api_param, 30, 1, NI_MIN_BITRATE,
                                           XCODER_MIN_ENC_PIC_WIDTH,
                                           XCODER_MIN_ENC_PIC_HEIGHT) < 0)
        {
            ni_log(NI_LOG_ERROR, "ERROR: set decoder default params error\n");
            return NI_RETCODE_INVALID_PARAM;
        }
    } else
    {
        if (ni_encoder_init_default_params(
                &api_param, 30, 1, NI_MIN_BITRATE, XCODER_MIN_ENC_PIC_WIDTH,
                XCODER_MIN_ENC_PIC_HEIGHT, NI_CODEC_FORMAT_H264) < 0)
        {
            ni_log(NI_LOG_ERROR, "ERROR: set encoder default params error\n");
            return NI_RETCODE_INVALID_PARAM;
        }
    }
    session_ctx.p_session_config = &api_param;

    p_device_pool = ni_rsrc_get_device_pool();
    if (!p_device_pool)
    {
        ni_log(NI_LOG_ERROR, "ERROR: get device poll failed\n");
        retval = NI_RETCODE_ERROR_GET_DEVICE_POOL;
        LRETURN;
    }

#ifdef _WIN32
    if (WAIT_ABANDONED ==
        WaitForSingleObject(p_device_pool->lock,
                            INFINITE))   // no time-out interval)
    {
        ni_log(NI_LOG_INFO,
               "ERROR: ni_rsrc_list_devices() failed to obtain mutex: %p\n",
               p_device_pool->lock);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }
#elif __linux__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif
    b_release_pool_mtx = true;

    // get device context
    p_device_ctx = ni_rsrc_get_device_context(device_type, guid);
    if (p_device_ctx)
    {
        session_ctx.device_handle = ni_device_open(
            p_device_ctx->p_device_info->blk_name, &max_nvme_io_size);
        session_ctx.blk_io_handle = session_ctx.device_handle;
        if (NI_INVALID_DEVICE_HANDLE == session_ctx.device_handle)
        {
            ni_log(NI_LOG_ERROR, "open device failed: %d\n", errno);
            retval = NI_RETCODE_ERROR_INVALID_HANDLE;
        } else
        {
#ifdef _WIN32
            session_ctx.event_handle = ni_create_event();
            if (NI_INVALID_EVENT_HANDLE == session_ctx.event_handle)
            {
                ni_log(NI_LOG_INFO, "Error create envent:%d\n", GetLastError());
                retval = NI_RETCODE_FAILURE;
                LRETURN;
            }
#endif
            retval = ni_device_session_query(&session_ctx, device_type);
            if (NI_RETCODE_SUCCESS != retval)
            {
                ni_log(NI_LOG_ERROR,
                       "guid %d. %s, %s is not avaiable, type: %d, retval:%d\n",
                       guid, p_device_ctx->p_device_info->dev_name,
                       p_device_ctx->p_device_info->blk_name, device_type,
                       retval);
                retval = NI_RETCODE_FAILURE;
            } else
            {
                while (1)
                {
                    retry_cnt++;
                    retval = ni_device_session_open(&session_ctx, device_type);
                    ni_device_session_close(&session_ctx, 0, device_type);
                    if (retval == NI_RETCODE_SUCCESS)
                    {
                        ni_log(NI_LOG_INFO, "guid %d. %s %s is avaiable\n",
                               guid, p_device_ctx->p_device_info->dev_name,
                               p_device_ctx->p_device_info->blk_name);
                        break;
                    } else if (
                        retry_cnt < 10 &&
                        retval ==
                            NI_RETCODE_ERROR_VPU_RECOVERY)   // max 2 seconds
                    {
                        ni_log(NI_LOG_INFO,
                               "vpu recovery happened on guid %d. %s %s, retry "
                               "cnt:%d\n",
                               guid, p_device_ctx->p_device_info->dev_name,
                               p_device_ctx->p_device_info->blk_name,
                               retry_cnt);
#ifndef _WIN32
                        ni_usleep(200000);   // 200 ms
#endif
                        continue;
                    } else
                    {
                        ni_log(NI_LOG_ERROR,
                               "session open error guid %d. %s, %s, type: %d, "
                               "retval:%d\n",
                               guid, p_device_ctx->p_device_info->dev_name,
                               p_device_ctx->p_device_info->blk_name,
                               device_type, retval);
                        retval = NI_RETCODE_FAILURE;
                        break;
                    }
                }
            }
        }
    } else
    {
        ni_log(NI_LOG_ERROR,
               "Error get device resource: guid %d, device_ctx %p\n", guid,
               p_device_ctx);
        retval = NI_RETCODE_FAILURE;
    }

END:

    if (b_release_pool_mtx)
    {
#ifdef _WIN32
        ReleaseMutex(p_device_pool->lock);
#elif __linux__
        lockf(p_device_pool->lock, F_ULOCK, 0);
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
    int i, j, k, count, rc = NI_RETCODE_FAILURE;
    ni_device_queue_t *p_device_queue = NULL;
    ni_device_pool_t *p_device_pool = ni_rsrc_get_device_pool();

    if (!dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: bad input parameter in %s()\n", __func__);
        return NI_RETCODE_FAILURE;
    }
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
      ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
             __func__, p_device_pool->lock);
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    p_device_queue = p_device_pool->p_device_queue;

    // assume all XCODER types are grouped
    count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_DECODER];
    int guid;
    for (i = 0; i < count; i++)
    {
        int has_xcoder[NI_DEVICE_TYPE_XCODER_MAX] = {0};
        char xc_lck_name[NI_DEVICE_TYPE_XCODER_MAX][32] = {0};
        ni_device_context_t *p_xcoder_device_ctx[NI_DEVICE_TYPE_XCODER_MAX] = {
            NULL};

        guid = p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i];
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            if (p_device_queue->xcoder_cnt[k])
            {
                p_xcoder_device_ctx[k] =
                    ni_rsrc_get_device_context((ni_device_type_t)k, guid);
                if (!p_xcoder_device_ctx[k] ||
                    0 !=
                        strcmp(p_xcoder_device_ctx[k]->p_device_info->dev_name,
                               dev))
                {
                    break;
                }
                has_xcoder[k] = 1;
            }
        }
        if (k < NI_DEVICE_TYPE_XCODER_MAX)
        {
            // previous loop ended prematurely
            for (j = 0; j <= k; j++)
            {
                if (!p_xcoder_device_ctx[j])
                {
                    continue;
                }
                ni_rsrc_free_device_context(p_xcoder_device_ctx[j]);
            }
            continue;
        }

        // found the device_info and remove its lock and storage
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            if (has_xcoder[k])
            {
                ni_rsrc_get_lock_name((ni_device_type_t)k, guid, xc_lck_name[k],
                                      sizeof(xc_lck_name[0]));
                ni_log(NI_LOG_INFO, "type %d guid %d shm_name: %s  lck_name: %s, ", k, guid,
                       p_xcoder_device_ctx[k]->shm_name, xc_lck_name[k]);
            }
        }
        // remove it from coders queue and decrement device_info counter, re-arrange
        // coders queue
#ifdef _WIN32
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            if (has_xcoder[k])
            {
                CloseHandle(p_xcoder_device_ctx[k]->lock);
            }
        }
        rc = NI_RETCODE_SUCCESS;
#elif __linux__ || __APPLE__

#ifndef _ANDROID
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            if (has_xcoder[k])
            {
                if (0 == shm_unlink(p_xcoder_device_ctx[k]->shm_name))
                {
                    ni_log(NI_LOG_INFO, "dev type %d shm_name %s deleted.\n", k,
                           p_xcoder_device_ctx[k]->shm_name);
                } else
                {
                    ni_log(NI_LOG_ERROR, "dev type %d shm_name %s deletion "
                           "failure.\n", k, p_xcoder_device_ctx[k]->shm_name);
                    break;
                }

                if (0 == unlink(xc_lck_name[k]))
                {
                    ni_log(NI_LOG_INFO, "dev type %d lck_name %s deleted.\n", k,
                           xc_lck_name[k]);
                } else
                {
                    ni_log(NI_LOG_ERROR, "dev type %d lck_name %s deletion "
                           "failure.\n", k, xc_lck_name[k]);
                    break;
                }
            }
        }
#endif

#endif
        // move everything after position i forward
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            for (j = i + 1; j < count; j++)
            {
                p_device_queue->xcoders[k][j - 1] =
                    p_device_queue->xcoders[k][j];
            }
            p_device_queue->xcoders[k][count - 1] = -1;
            p_device_queue->xcoder_cnt[k]--;
        }
#if __linux__ || __APPLE__
        if (msync((void *)p_device_pool->p_device_queue,
                  sizeof(ni_device_queue_t), MS_SYNC | MS_INVALIDATE))
        {
            ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_pool->"
                   "p_device_queue: %s\n", __func__, strerror(NI_ERRNO));
        } else
        {
            ni_log(NI_LOG_INFO, "%s deleted successfully !\n", dev);
            rc = NI_RETCODE_SUCCESS;
        }
#endif
        break;
    }

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_ULOCK, 0);
#endif
    ni_rsrc_free_device_pool(p_device_pool);
  }

  return rc;
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
    uint32_t i, j = 0, k = 0, count, fw_ver_compat_warning = 0;
    int ret = 0;
    ni_retcode_t rc = NI_RETCODE_SUCCESS;
    ni_device_handle_t fd = 0;
    ni_device_queue_t *p_device_queue;
    ni_device_info_t device_info;
    ni_device_capability_t cap;
    ni_device_pool_t *p_device_pool = ni_rsrc_get_device_pool();

    if (!dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: bad input parameter in %s()\n", __func__);
        return NI_RETCODE_FAILURE;
    }
  // get the biggest guid, increment it by 1 and use it as the new guid
  if (p_device_pool)
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))  // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n",
               __func__, p_device_pool->lock);
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    p_device_queue = p_device_pool->p_device_queue;
    count = p_device_queue->xcoder_cnt[NI_DEVICE_TYPE_DECODER];
    int32_t guid = -1;
    for (i = 0; i < count; i++)
    {
        ni_device_context_t *tmp_ctxt = ni_rsrc_get_device_context(
            NI_DEVICE_TYPE_DECODER,
            p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i]);
        if (tmp_ctxt && 0 == strcmp(tmp_ctxt->p_device_info->dev_name, dev))
        {
            rc = NI_RETCODE_FAILURE;
            ni_log(NI_LOG_ERROR, "ERROR: Transcoder %s already active, guid: "
                   "%d\n", dev,
                   p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i]);
            ni_rsrc_free_device_context(tmp_ctxt);
            LRETURN;
        } else
      {
        ni_rsrc_free_device_context(tmp_ctxt);
      }

      if (p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i] > guid)
      {
          guid = p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i];
      }
    }
    guid++;

    // retrieve the device_info capability info from f/w
    memset(&device_info, 0, sizeof(ni_device_info_t));
    ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s", dev);
    if (ret < 0)
    {
        rc = NI_RETCODE_FAILURE;
        LRETURN;
    }
    memset(&cap, 0, sizeof(ni_device_capability_t));

    uint32_t tmp_io_size;

    ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
    fd = ni_device_open(device_info.blk_name, &tmp_io_size);

    if (NI_INVALID_DEVICE_HANDLE == fd)
    {
      rc = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_ERROR, "Failed to open device: %s\n", device_info.dev_name);
      LRETURN;
    }

    rc = ni_device_capability_query(fd, &cap);
    if ((NI_RETCODE_SUCCESS == rc) &&
        (is_supported_xcoder(cap.device_is_xcoder)) &&
        (!should_match_rev || ni_is_fw_compatible(cap.fw_rev)))
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
      uint32_t total_modules = cap.xcoder_devices_cnt;

      ni_device_info_t *pCoderInfo = NULL;
      for (j = 0; j < total_modules; j++)
      {
        pCoderInfo = NULL;

        memset(&device_info, 0, sizeof(ni_device_info_t));
        ret = snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN, "%s", dev);
        if (ret < 0)
        {
            rc = NI_RETCODE_FAILURE;
            LRETURN;
        }
        ni_find_blk_name(device_info.dev_name, device_info.blk_name, sizeof(device_info.blk_name));
        device_info.hw_id = cap.xcoder_devices[j].hw_id;
        device_info.fw_ver_compat_warning = fw_ver_compat_warning;
        memcpy(device_info.fw_rev, cap.fw_rev, sizeof(device_info.fw_rev));

        memcpy(device_info.fw_branch_name, cap.fw_branch_name, sizeof(device_info.fw_branch_name) - 1);
        memcpy(device_info.fw_commit_time, cap.fw_commit_time,
               sizeof(device_info.fw_commit_time) - 1);
        memcpy(device_info.fw_commit_hash, cap.fw_commit_hash,
               sizeof(device_info.fw_commit_hash) - 1);
        memcpy(device_info.fw_build_time, cap.fw_build_time,
               sizeof(device_info.fw_build_time) - 1);
        memcpy(device_info.fw_build_id, cap.fw_build_id,
               sizeof(device_info.fw_build_id) - 1);

        device_info.max_fps_4k = cap.xcoder_devices[j].max_4k_fps;
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
            ni_log(NI_LOG_INFO, "%s h/w id %d update\n",
                   device_type_str[device_info.device_type], device_info.hw_id);
            rc = ni_rsrc_fill_device_info(pCoderInfo, fmt,
                                          device_info.device_type,
                                          &cap.xcoder_devices[j]);
        }
        else
        {
            ni_log(NI_LOG_INFO, "%s h/w id %d create\n",
                   device_type_str[device_info.device_type], device_info.hw_id);
            pCoderInfo = &device_info;

            rc = ni_rsrc_fill_device_info(pCoderInfo, fmt,
                                          device_info.device_type,
                                          &cap.xcoder_devices[j]);
            if (NI_RETCODE_SUCCESS == rc)
            {
                /*! add the h/w device_info entry */
                pCoderInfo->module_id = guid;
                p_device_queue->xcoder_cnt[device_info.device_type]++;
                p_device_queue->xcoders
                    [device_info.device_type]
                    [p_device_queue->xcoder_cnt[device_info.device_type] - 1] =
                    pCoderInfo->module_id;
                ni_rsrc_get_one_device_info(&device_info);
          }
        }
        if (p_device_context)
        {
            ni_rsrc_free_device_context(p_device_context);
        }
      } // for each device_info module
#if __linux__ || __APPLE__
      if (msync((void *)p_device_pool->p_device_queue, sizeof(ni_device_queue_t),
                MS_SYNC | MS_INVALIDATE))
      {
          ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_pool->"
                 "p_device_queue: %s\n", __func__, strerror(NI_ERRNO));
          rc = NI_RETCODE_FAILURE;
      } else
        ni_log(NI_LOG_INFO, "%s added successfully !\n", dev);
#endif
    }
    else
    { // xcoder NOT supported
      rc = NI_RETCODE_FAILURE;
      ni_log(NI_LOG_INFO, "Query %s rc %d not a supported xcoder: %u, or "
             "mismatch revision: %.*s; not added\n", dev, rc,
             cap.device_is_xcoder, (int) sizeof(cap.fw_rev), cap.fw_rev);
    }

    if (NI_INVALID_DEVICE_HANDLE != fd)
    {
      ni_device_close(fd);
    }

  END:

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_ULOCK, 0);
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
#elif __linux__ || __APPLE__
        close(p_device_pool->lock);
#endif
    }
#ifdef _WIN32
    UnmapViewOfFile(p_device_pool->p_device_queue);
#else
    munmap(p_device_pool->p_device_queue, sizeof(ni_device_queue_t));
#endif

    free(p_device_pool);
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
  *lock = CreateMutex(NULL, FALSE, XCODERS_RETRY_LCK_NAME[device_type]);

  if (NULL == *lock)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() CreateMutex() %s failed: %s\n",
             __func__, XCODERS_RETRY_LCK_NAME[device_type],
             strerror(NI_ERRNO));
      return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
  }
#else
  do
  {
    if (count>=1)
    {
      //sleep 10ms if the file lock is locked by other FFmpeg process
      ni_usleep(LOCK_WAIT);
    }
    // Here we try to open the file lock, retry if it failed
    *lock =
        open(XCODERS_RETRY_LCK_NAME[device_type], O_RDWR | O_CREAT | O_CLOEXEC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    if (*lock < 0)
    {
      count++;
      if (count > MAX_LOCK_RETRY)
      {
        ni_log(NI_LOG_ERROR, "Can not lock down the file lock after 6s");
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
      ni_usleep(LOCK_WAIT);
    }
#ifdef _WIN32
    DWORD ret = WaitForSingleObject(*lock, 1);   // time-out 1ms
    if (WAIT_OBJECT_0 == ret)
    {
        status = NI_RETCODE_SUCCESS;
    } else if (WAIT_TIMEOUT != ret)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %s\n",
               __func__, strerror(NI_ERRNO));
    }
#else
    status = lockf(*lock, F_LOCK, 0);
#endif
    if (status != 0)
    {
      count++;
      if (count > MAX_LOCK_RETRY)
      {
        ni_log(NI_LOG_ERROR, "Can not put down the lock after 6s");
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
  if (lock == NI_INVALID_LOCK_HANDLE)
  {
      return NI_RETCODE_FAILURE;
  }

  int count = 0;
  ni_lock_handle_t status = NI_INVALID_LOCK_HANDLE;
  do
  {
      if (count >= 1)
      {
          ni_usleep(LOCK_WAIT);
      }
#ifdef _WIN32
      if (ReleaseMutex(lock))
      {
          status = (ni_lock_handle_t)(0);
      }
#else
      status = lockf(lock, F_ULOCK, 0);
#endif
      count++;
      if (count > MAX_LOCK_RETRY)
      {
          ni_log(NI_LOG_ERROR, "Can not unlock the lock after 6s");
          return NI_RETCODE_ERROR_UNLOCK_DEVICE;
      }
  } while (status != (ni_lock_handle_t)(0));

#ifdef _WIN32
  CloseHandle(lock);
#else
  close(lock);
#endif //_WIN32 defined
  return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
*  \brief  check if device FW revision is compatible with SW API
*
*  \param fw_rev
*
*  \return 1 for full compatibility, 2 for partial, 0 for none
*******************************************************************************/
int ni_rsrc_is_fw_compat(uint8_t fw_rev[8])
{
    return ni_is_fw_compatible(fw_rev);
}
