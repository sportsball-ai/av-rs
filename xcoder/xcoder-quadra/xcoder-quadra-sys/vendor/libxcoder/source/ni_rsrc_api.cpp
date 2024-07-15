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
 *  \file   ni_rsrc_api.cpp
 *
 *  \brief  Public definitions for managing NETINT video processing devices
 ******************************************************************************/

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
#include <regex.h>
#endif

#if __APPLE__
#include <sys/syslimits.h>
#endif

#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"

static const char *ni_codec_format_str[] = {"H.264", "H.265", "VP9", "JPEG",
                                            "AV1"};
static const char *ni_dec_name_str[] = {"h264_ni_quadra_dec", "h265_ni_quadra_dec",
                                        "vp9_ni_quadra_dec", "jpeg_ni_quadra_dec"};
static const char *ni_enc_name_str[] = {"h264_ni_quadra_enc", "h265_ni_quadra_enc", "empty",
                                        "jpeg_ni_quadra_enc", "av1_ni_quadra_enc"};

char **g_xcoder_refresh_dev_names = NULL;
int g_xcoder_refresh_dev_count = 0;
bool g_device_in_ctxt = false;
ni_device_handle_t g_dev_handle = NI_INVALID_DEVICE_HANDLE;

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
    for (int xcoder_index_1 = 0;
         xcoder_index_1 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
         xcoder_index_1++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_ENCODER][xcoder_index_1];
        ni_log(NI_LOG_INFO, "Device #%d:\n", xcoder_index_1);
        ni_log(NI_LOG_INFO, "  Serial number: %.*s\n",
               (int)sizeof(p_dev_info->serial_number),
               p_dev_info->serial_number);
        ni_log(NI_LOG_INFO, "  Model number: %.*s\n",
               (int)sizeof(p_dev_info->model_number),
               p_dev_info->model_number);
        ni_log(NI_LOG_INFO, "  Last ran firmware loader version: %.8s\n",
               p_dev_info->fl_ver_last_ran);
        ni_log(NI_LOG_INFO, "  NOR flash firmware loader version: %.8s\n",
               p_dev_info->fl_ver_nor_flash);
        ni_log(NI_LOG_INFO, "  Current firmware revision: %.8s\n",
               p_dev_info->fw_rev);
        ni_log(NI_LOG_INFO, "  NOR flash firmware revision: %.8s\n",
               p_dev_info->fw_rev_nor_flash);
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
        ni_log(NI_LOG_INFO, "  PixelFormats: yuv420p, yuv420p10le, nv12, p010le"
               ", ni_quadra\n");

        for (size_t dev_type = NI_DEVICE_TYPE_DECODER;
             dev_type != NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            for (int xcoder_index_2 = 0;
                 xcoder_index_2 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
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
    ni_device_t *saved_coders = NULL;
    saved_coders = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (!saved_coders)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() failed to malloc memory: %s\n",
               __func__, strerror(NI_ERRNO));
        return NI_RETCODE_FAILURE;
    }
    memset(saved_coders, 0, sizeof(ni_device_t));

    // retrieve saved info from resource pool at start up
    if (NI_RETCODE_SUCCESS ==
        ni_rsrc_list_devices(
            NI_DEVICE_TYPE_ENCODER,
            saved_coders->xcoders[NI_DEVICE_TYPE_ENCODER],
            &(saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER])))
    {
        for (i = 0; i < saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
              i++)
        {
            strcpy(xcoder_dev_names[i],
                    saved_coders->xcoders[NI_DEVICE_TYPE_ENCODER][i]
                        .dev_name);
        }
        xcoder_dev_count =
            saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
#ifdef XCODER_311
        ni_log(NI_LOG_DEBUG,"%d devices retrieved from current pool at start up\n",
                xcoder_dev_count);
#else
        ni_log(NI_LOG_INFO,"%d devices retrieved from current pool at start up\n",
                xcoder_dev_count);
#endif
                
    } else
    {
        ni_log(NI_LOG_ERROR, "Error retrieving from current pool at start "
                "up\n");
    }
    free(saved_coders);

    if (xcoder_dev_count > 0)
    {
        g_xcoder_refresh_dev_names = (char **)malloc(xcoder_dev_count *
                                             sizeof(char *));
        for (i = 0; i < xcoder_dev_count; i++)
        {
            g_xcoder_refresh_dev_names[i] = (char *)malloc(NI_MAX_DEVICE_NAME_LEN);
            strcpy(g_xcoder_refresh_dev_names[i], xcoder_dev_names[i]);
        }
        g_xcoder_refresh_dev_count = xcoder_dev_count;
    }

    curr_dev_count =
        ni_rsrc_get_local_device_list(curr_dev_names, NI_MAX_DEVICE_CNT);
    if (0 == curr_dev_count)
    {
        ni_log(NI_LOG_ERROR, "No devices found on the host\n");   
    }
    int devices_removed = 0;
    int devices_added = 0;
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
                devices_removed++;
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
                devices_added++;
                ni_log(NI_LOG_INFO, "%s added successfully !\n", curr_dev_names[i]);
            } else
            {
                ni_log(NI_LOG_ERROR, "%s failed to add !\n", curr_dev_names[i]);
            }
        }
    }

    if (devices_added != devices_removed)
    {
        ni_log(NI_LOG_ERROR, "Total devices added %d removed %d\n",devices_added,
               devices_removed);
        for (i = 0; i < xcoder_dev_count; i++)
        {
            ni_log(NI_LOG_ERROR, "Previous device %s\n", xcoder_dev_names[i]);
        }
        for (i = 0; i < curr_dev_count; i++)
        {
            ni_log(NI_LOG_ERROR, "Current device %s\n", curr_dev_names[i]);
        }
    }
    if (g_xcoder_refresh_dev_names) {
        for (i = 0; i < g_xcoder_refresh_dev_count; i++)
            free(g_xcoder_refresh_dev_names[i]);
        free(g_xcoder_refresh_dev_names);
        g_xcoder_refresh_dev_names = NULL;
        g_xcoder_refresh_dev_count = 0;
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
        service = INidec::tryGetService();
        if (service == nullptr)
        {
            ni_log(NI_LOG_ERROR, "Failed to get Netint service, maybe it's not launched\n");
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
 *  \return     Number of devices found if successful operation completed
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
    char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = { 0 };
    char api_version[5];
    int number_of_devices = 0;
    int runtime = 0;
    while (0 == number_of_devices)
    {
        number_of_devices = ni_rsrc_get_local_device_list(device_names, NI_MAX_DEVICE_CNT);

        if (NI_RETCODE_ERROR_MEM_ALOC == number_of_devices)
        {
            ni_log(NI_LOG_FATAL, "FATAL: memory allocation failed\n");
            return NI_RETCODE_FAILURE;
        }
        else if (0 == number_of_devices)
        {
            ni_log(NI_LOG_INFO, "Quadra devices not ready\n");
            if (g_xcoder_stop_process)
            {
                ni_log(NI_LOG_ERROR, "User requested to stop checking\n");
                return NI_RETCODE_FAILURE;
            }
            runtime += 3;
            Sleep(3 * 1000);
            if (runtime >= timeout_seconds && timeout_seconds != 0)
            {
                ni_log(NI_LOG_ERROR, "Timeout reached at %d seconds! Failing\n",
                        runtime);
                return NI_RETCODE_FAILURE;
            }
        }
    }

    ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          api_version);
    ni_log(NI_LOG_INFO, "Compatible FW API version: %s\n", api_version);

    return ni_rsrc_init_priv(should_match_rev, number_of_devices, device_names);
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
#if __APPLE__
#define DEV_NAME_PREFIX "rdisk"
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
#define DEV_NAME_PREFIX "vd"
#else
#define DEV_NAME_PREFIX "nvme"
#endif

/*!*****************************************************************************
 *  \brief  Scans system for all NVMe devices and returns the system device
 *          names to the user which were identified as NETINT transcoder
 *          devices. Names are suitable for resource management API usage
 *          afterwards
 *
 *  \param[out]  ni_devices  List of device names identified as NETINT NVMe
 *                           transcoders
 *  \param[in]   max_handles Max number of device names to return
 *
 *  \return  Number of devices found. 0 if unsucessful.
 ******************************************************************************/
int ni_rsrc_get_local_device_list(char ni_devices[][NI_MAX_DEVICE_NAME_LEN],
                                  int max_handles)
{
  /* list all XCoder devices under /dev/.. */
#ifdef _ANDROID
#define ANDROID_MAX_DIR_NUM 2
  int android_dir_num = 0;
  const char* dir_name_array[ANDROID_MAX_DIR_NUM];
  dir_name_array[0] = "/dev";
  dir_name_array[1] = "/dev/block";
#else
  const char* dir_name = "/dev";
#endif
  int i, xcoder_device_cnt = 0;
  DIR* FD;
  struct dirent* in_file;
  ni_device_info_t device_info;
  ni_device_capability_t device_capabilites;
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE;
  ni_retcode_t rc;
  uint32_t tmp_io_size;
  g_device_in_ctxt = false;

  if ((ni_devices == NULL)||(max_handles == 0))
  {
    ni_log(NI_LOG_ERROR, "ERROR: bad input parameters\n");
    return 0;
  }

  int nvme_dev_cnt = 0;
  char nvme_devices[200][NI_MAX_DEVICE_NAME_LEN];

  regex_t regex;
  // GNU ERE not support /d, use [0-9] or [[:digit:]] instead
#if __APPLE__
  const char *pattern = "^rdisk[0-9]+$";
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
  const char *pattern = "^vd[a-z]$";
#else
  const char *pattern = "^nvme[0-9]+(c[0-9]+)?n[0-9]+$";
#endif
  // Compile the regular expression
  if(regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB)) {
    ni_log(NI_LOG_ERROR, "Could not compile regex\n");
    return 0;
  }

#ifdef _ANDROID
 //find XCoder devices in folders of dir_name_array until find in one folder
 //or not find in any folders
 while(xcoder_device_cnt == 0 && android_dir_num < ANDROID_MAX_DIR_NUM)
 {
  const char *dir_name = dir_name_array[android_dir_num];
  ++android_dir_num;

  nvme_dev_cnt = 0;
  g_device_in_ctxt = false;
  dev_handle = NI_INVALID_DEVICE_HANDLE;
  // g_dev_handle = NI_INVALID_DEVICE_HANDLE;
  size_t size_of_nvme_devices_x = sizeof(nvme_devices)/sizeof(nvme_devices[0]);
  for(size_t dimx = 0; dimx < size_of_nvme_devices_x; ++dimx)
  {
    memset(nvme_devices[dimx], 0, sizeof(nvme_devices[0]));
  }
 //}//while brace below will end this
#endif

  if (NULL == (FD = opendir(dir_name)))
  {
    
#ifdef _ANDROID
    ni_log(NI_LOG_INFO, "Failed to open directory %s\n", dir_name);
    if(android_dir_num < ANDROID_MAX_DIR_NUM)
    {
        continue;
    }
    regfree(&regex);
    return 0;
#else
    ni_log(NI_LOG_ERROR, "ERROR: failed to open directory %s\n", dir_name);
    regfree(&regex);
    return 0;
#endif
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
      if (!strncmp(in_file->d_name, DEV_NAME_PREFIX, strlen(DEV_NAME_PREFIX)))
      {
        if (nvme_dev_cnt < 200)
        {
            int write_len = snprintf(nvme_devices[nvme_dev_cnt],
                                        NI_MAX_DEVICE_NAME_LEN - 6, "%s/%s",
                                        dir_name, in_file->d_name);
            if (write_len < 0 || write_len >= (NI_MAX_DEVICE_NAME_LEN - 6))
            {
                ni_log(NI_LOG_ERROR,
                        "ERROR: failed to copy device %d name %s\n",
                        nvme_dev_cnt, in_file->d_name);
            }
            nvme_dev_cnt++;
        }
          int skip_this = 0;
          skip_this = regexec(&regex, in_file->d_name, 0, NULL, 0);
          ni_log(NI_LOG_TRACE, "name: %s skip %d\n", in_file->d_name, skip_this);

          if (!skip_this)
          {
              memset(&device_info, 0, sizeof(ni_device_info_t));
              if (snprintf(device_info.dev_name, NI_MAX_DEVICE_NAME_LEN - 6,
                           "%s/%s", dir_name, in_file->d_name) < 0)
              {
                  ni_log(NI_LOG_ERROR,
                         "ERROR: failed an snprintf() in "
                         "ni_rsrc_get_local_device_list()\n");
                  continue;
              }
              strncpy(device_info.blk_name, device_info.dev_name,
                      NI_MAX_DEVICE_NAME_LEN);
              memset(&device_capabilites, 0, sizeof(ni_device_capability_t));

              g_device_in_ctxt = false; 
              for (int j = 0; j < g_xcoder_refresh_dev_count; j++)
              {
                  if (0 ==
                      strcmp(device_info.dev_name,
                             g_xcoder_refresh_dev_names[j]))
                  {
                      g_device_in_ctxt = true;
                      break;
                  }
              }
              if (NI_RETCODE_SUCCESS != ni_check_dev_name(device_info.dev_name))
              { 
                  continue;
              }
              dev_handle = ni_device_open(device_info.dev_name, &tmp_io_size);

              if (NI_INVALID_DEVICE_HANDLE != dev_handle)
              {
                g_dev_handle = dev_handle;
                  rc = ni_device_capability_query(dev_handle,
                                                  &device_capabilites);
                  if (NI_RETCODE_SUCCESS == rc)
                  {
                      if (is_supported_xcoder(
                              device_capabilites.device_is_xcoder))
                      {
                          ni_devices[xcoder_device_cnt][0] = '\0';
                          strcat(ni_devices[xcoder_device_cnt],
                                 device_info.dev_name);
                          xcoder_device_cnt++;
                      }
                  }
                g_dev_handle = NI_INVALID_DEVICE_HANDLE;

                  ni_device_close(dev_handle);
              }
          }
      }
      if ((NI_MAX_DEVICE_CNT <= xcoder_device_cnt) ||
          (max_handles <= xcoder_device_cnt))
      {
          ni_log(NI_LOG_ERROR,
                 "Disregarding some Netint devices on system over "
                 "limit of NI_MAX_DEVICE_CNT(%d) or max_handles(%d)\n",
                 NI_MAX_DEVICE_CNT, max_handles);
          break;
      }
  }
  closedir(FD);

#ifdef _ANDROID
 }//while brace
#endif

  regfree(&regex);

  qsort(ni_devices, xcoder_device_cnt, (size_t)NI_MAX_DEVICE_NAME_LEN,
        ni_rsrc_strcmp);
  if (0 == xcoder_device_cnt)
  {
      ni_log(NI_LOG_INFO, "Found %d NVMe devices on system, none of them xcoder\n", nvme_dev_cnt);
      for (i = 0; i < nvme_dev_cnt; i++)
      {
          ni_log(NI_LOG_INFO, "NVMe device %d: %s\n", i, nvme_devices[i]);
      }
  }
  g_device_in_ctxt = false;
  g_dev_handle = NI_INVALID_DEVICE_HANDLE;

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
  int shm_fd = -1;
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

  int retry_cnt = 0;
  //use non blocking F_TLOCK in case broken instance has indefinitely locked it
  while (lockf(lock, F_TLOCK, 0) != 0) 
  {
      retry_cnt++;
      ni_usleep(LOCK_WAIT);   //10ms
      if (retry_cnt >= 900) //9s
      {
          ni_log(NI_LOG_ERROR, "ERROR %s() lockf() CODERS_LCK_NAME: %s\n",
                  __func__, strerror(NI_ERRNO));
          ni_log(NI_LOG_ERROR, "ERROR %s() If persists, stop traffic and run rm /dev/shm/NI_*\n",
                 __func__);
          close(lock);
          return NULL;
      }
  }


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
  if (shm_fd < 0)
  {
      int fd = ashmem_create_region(CODERS_SHM_NAME, sizeof(ni_device_queue_t));
      if (fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = fd;
          service->SetAppFlag(param, handle);
          shm_fd = dup(fd);
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

  if (shm_fd >= 0)
  {
    close(shm_fd);
  }

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
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init(int should_match_rev, int timeout_seconds)
{
    char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN];
    char api_version[5];
    int number_of_devices;
    int runtime;

    runtime = 0;
    while (1)
    {
        number_of_devices = ni_rsrc_get_local_device_list(device_names,
                                                          NI_MAX_DEVICE_CNT);
        if (number_of_devices > 0)
        {
            break;
        }
        else
        {
            ni_log(NI_LOG_INFO, "Quadra devices not ready\n");
            if (g_xcoder_stop_process)
            {
                ni_log(NI_LOG_ERROR, "User requested to stop checking\n");
                return 1;
            }

            sleep(3);
            runtime += 3;
            if (runtime > timeout_seconds)
            {
                ni_log(NI_LOG_ERROR,
                       "Timeout exceeded/reached after %u seconds!\n",
                       runtime);
                return 1;
            }
        }
    }

    ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          api_version);
    ni_log(NI_LOG_INFO, "Compatible FW API version: %s\n", api_version);

    return ni_rsrc_init_priv(should_match_rev, number_of_devices, device_names);
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
  int shm_fd = -1;
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

  int retry_cnt = 0;
  //use non blocking F_TLOCK in case broken instance has indefinitely locked it
  while (lockf(lock, F_TLOCK, 0) != 0)
  {
    retry_cnt++;
    ni_usleep(LOCK_WAIT);    //10ms
    if (retry_cnt >= 900)   //10s
    {
          ni_log(NI_LOG_ERROR, "ERROR %s() lockf() %s: %s\n", __func__,
                 lck_name, strerror(NI_ERRNO));
          ni_log(
              NI_LOG_ERROR,
              "ERROR %s() If persists, stop traffic and run rm /dev/shm/NI_*\n",
              __func__);
          close(lock);
          return NULL;
    }
  }

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

  if (shm_fd < 0)
  {
      int fd = ashmem_create_region(shm_name, sizeof(ni_device_info_t));
      if (fd >= 0)
      {
          native_handle_t *handle = native_handle_create(1, 0);
          handle->data[0] = fd;
          service->SetAppFlag(param, handle);
          shm_fd = dup(fd);
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

  strncpy(p_device_context->shm_name, shm_name, sizeof(p_device_context->shm_name));
  p_device_context->lock = lock;
  p_device_context->p_device_info = p_device_queue;

END:
  lockf(lock, F_ULOCK, 0);

  if (shm_fd >= 0)
  {
    close(shm_fd);
  }

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
ni_retcode_t ni_rsrc_list_all_devices2(ni_device_t* p_device, bool list_uninitialized)
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
         dev_index < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER]; dev_index++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_ENCODER][dev_index];
        strcpy(initialized_dev_names[dev_index], p_dev_info->dev_name);
    }

    /* Retrieve uninitialized devices. */

    char dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
    int dev_count = ni_rsrc_get_local_device_list(dev_names, NI_MAX_DEVICE_CNT);

    uint32_t tmp_io_size;
    ni_device_capability_t capability;
    ni_device_handle_t fd;

    for (int dev_index = 0; dev_index < dev_count; dev_index++)
    {
        if (is_str_in_str_array(dev_names[dev_index],
                                initialized_dev_names, NI_MAX_DEVICE_CNT))
        {
            continue;
        }

        fd = ni_device_open(dev_names[dev_index], &tmp_io_size);
        if (NI_INVALID_DEVICE_HANDLE == fd)
        {
            ni_log(NI_LOG_ERROR, "Failed to open device: %s\n",
                   dev_names[dev_index]);
            return NI_RETCODE_FAILURE;
        }

        retval = ni_device_capability_query(fd, &capability);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_device_close(fd);
            ni_log(NI_LOG_ERROR, "Failed to query device capability: %s\n",
                   dev_names[dev_index]);
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
            strcpy(p_dev_info->dev_name, dev_names[dev_index]);
            memcpy(p_dev_info->blk_name, p_dev_info->dev_name,
                   sizeof(p_dev_info->dev_name));
            p_dev_info->device_type = (ni_device_type_t)dev_type;
            p_dev_info->module_id = -1; /* special value to indicate device is
                                           not initialized */

            ni_query_fl_fw_versions(fd, p_dev_info);
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
               GET_XCODER_DEVICE_TYPE_STR(p_device_info->device_type),
               p_device_info->module_id);
        ni_log(NI_LOG_INFO, "  H/W ID: %d\n", p_device_info->hw_id);
        ni_log(NI_LOG_INFO, "  MaxNumInstances: %d\n",
               p_device_info->max_instance_cnt);

        if (NI_DEVICE_TYPE_SCALER == p_device_info->device_type)
        {
            ni_log(NI_LOG_INFO, "  Capabilities:\n");
            ni_log(NI_LOG_INFO,
                   "    Operations: Crop (ni_quadra_crop), Scale (ni_quadra_scale), Pad "
                   "(ni_quadra_pad), Overlay (ni_quadra_overlay)\n"
                   "                Drawbox (ni_quadra_drawbox), Rotate (ni_quadra_rotate), XStack (ni_quadra_xstack)\n");
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
void ni_rsrc_print_all_devices_capability(void)
{
    ni_device_t *device = NULL;
    device = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (!device)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() failed to malloc memory: %s\n",
               __func__, strerror(NI_ERRNO));
        return;
    }
    memset(device, 0, sizeof(ni_device_t));

    if (NI_RETCODE_SUCCESS != ni_rsrc_list_all_devices(device))
    {
        free(device);
        return;
    }

    print_device(device);
    free(device);
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
void ni_rsrc_print_all_devices_capability2(bool list_uninitialized)
{
    ni_device_t *device = NULL;
    device = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (!device)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() failed to malloc memory: %s\n",
               __func__, strerror(NI_ERRNO));
        return;
    }
    memset(device, 0, sizeof(ni_device_t));

    if (NI_RETCODE_SUCCESS != ni_rsrc_list_all_devices2(device, list_uninitialized))
    {
        free(device);
        return;
    }

    print_device(device);
    free(device);
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
* \brief      Get GUID of the device by block device name and type
*
* \param[in]  blk_name   device's block name
* \param[in]  type       device type
*
* \return     device GUID (>= 0) if found, NI_RETCODE_FAILURE (-1) otherwise
*******************************************************************************/
int ni_rsrc_get_device_by_block_name(const char *blk_name,
                                     ni_device_type_t device_type)
{
    int i;
    int guid = NI_RETCODE_FAILURE, tmp_id;
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_context_t *p_device_context = NULL;
    int num_coders = 0;

    //uploader shares instance with encoder
    if(device_type == NI_DEVICE_TYPE_UPLOAD)
    {
        device_type = NI_DEVICE_TYPE_ENCODER;
    }

    p_device_pool = ni_rsrc_get_device_pool();
    if (!p_device_pool)
    {
        ni_log(NI_LOG_ERROR, "ERROR: cannot get p_device_pool\n");
        return guid;
    }

#ifdef _WIN32
    // no time-out interval (we got the mutex)
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE))
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s failed to obtain mutex: %p\n",
               __FUNCTION__, p_device_pool->lock);
        ni_rsrc_free_device_pool(p_device_pool);
        return NI_RETCODE_FAILURE;
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    num_coders = p_device_pool->p_device_queue->xcoder_cnt[device_type];

    for (i = 0; i < num_coders; i++)
    {
        tmp_id = p_device_pool->p_device_queue->xcoders[device_type][i];
        p_device_context = ni_rsrc_get_device_context(device_type, tmp_id);

        if (p_device_context &&
            0 == strcmp(p_device_context->p_device_info->dev_name, blk_name))
        {
            guid = p_device_context->p_device_info->module_id;
            ni_rsrc_free_device_context(p_device_context);
            break;
        }

        ni_rsrc_free_device_context(p_device_context);
    }

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_ULOCK, 0);
#endif

    ni_rsrc_free_device_pool(p_device_pool);

    ni_log(NI_LOG_DEBUG, "%s %s got guid: %d\n", __FUNCTION__, blk_name, guid);

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
*******************************************************************************/
void ni_rsrc_release_resource(ni_device_context_t *p_device_context,
                              uint64_t load)
{

#if 1
    (void) p_device_context;
    (void) load;
    return;

#else
  if (!p_device_context)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() invalid input pointers\n", __func__);
      return;
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

    if (!IS_XCODER_DEVICE_TYPE(device_type))
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
    } else if (NI_DEVICE_TYPE_ENCODER == device_type)
    {
        if (ni_encoder_init_default_params(
                &api_param, 30, 1, NI_MIN_BITRATE, XCODER_MIN_ENC_PIC_WIDTH,
                XCODER_MIN_ENC_PIC_HEIGHT, NI_CODEC_FORMAT_H264) < 0)
        {
            ni_log(NI_LOG_ERROR, "ERROR: set encoder default params error\n");
            return NI_RETCODE_INVALID_PARAM;
        }
    } else if (NI_DEVICE_TYPE_SCALER == device_type)
    {
        session_ctx.device_type       = NI_DEVICE_TYPE_SCALER;
        session_ctx.scaler_operation  = NI_SCALER_OPCODE_SCALE;
    } else
    {
        session_ctx.device_type       = NI_DEVICE_TYPE_AI;
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
            p_device_ctx->p_device_info->dev_name, &max_nvme_io_size);
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
                       "guid %d. %s is not avaiable, type: %d, retval:%d\n",
                       guid, p_device_ctx->p_device_info->dev_name,
                       device_type, retval);
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
                        ni_log(NI_LOG_INFO, "guid %d. %s is avaiable\n",
                               guid, p_device_ctx->p_device_info->dev_name);
                        break;
                    } else if (
                        retry_cnt < 10 &&
                        retval ==
                            NI_RETCODE_ERROR_VPU_RECOVERY)   // max 2 seconds
                    {
                        ni_log(NI_LOG_INFO,
                               "vpu recovery happened on guid %d. %s, retry "
                               "cnt:%d\n",
                               guid, p_device_ctx->p_device_info->dev_name,
                               retry_cnt);
#ifndef _WIN32
                        ni_usleep(200000);   // 200 ms
#endif
                        continue;
                    } else
                    {
                        ni_log(NI_LOG_ERROR,
                               "session open error guid %d. %s, type: %d, "
                               "retval:%d\n",
                               guid, p_device_ctx->p_device_info->dev_name,
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
    ni_device_session_context_clear(&session_ctx);
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
#if __linux__ || __APPLE__
#ifndef _ANDROID
    char lck_name[32];
#endif
#endif
    int return_value = NI_RETCODE_SUCCESS;
    int32_t guid;
    int32_t guids[NI_MAX_DEVICE_CNT];
    unsigned int guid_index_i, guid_index_j, ui_device_type;
#ifdef _WIN32
    DWORD rValue;
#endif
    ni_device_context_t *p_device_context;
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_queue_t *p_device_queue;
    ni_device_type_t device_type;

    if (!dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() dev is NULL\n", __FUNCTION__);
        return_value = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    p_device_pool = ni_rsrc_get_device_pool();
    if (!p_device_pool)
    {
        return_value = NI_RETCODE_FAILURE;
        LRETURN;
    }

#ifdef _WIN32
    rValue = WaitForSingleObject(p_device_pool->lock, INFINITE);
    if (rValue != WAIT_OBJECT_0)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s() Failed to obtain mutex %p\n",
               __FUNCTION__,
               p_device_pool->lock);
        return_value = NI_RETCODE_FAILURE;
        LRETURN;
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    p_device_queue = p_device_pool->p_device_queue;
    for (guid_index_i = 0; guid_index_i < NI_MAX_DEVICE_CNT; guid_index_i++)
    {
        for (ui_device_type = NI_DEVICE_TYPE_DECODER;
             ui_device_type < NI_DEVICE_TYPE_XCODER_MAX;
             ui_device_type++)
        {
            guid = p_device_queue->xcoders[ui_device_type][guid_index_i];
            if (guid == -1)
            {
                continue;
            }
            device_type = (ni_device_type_t)ui_device_type;
            p_device_context = ni_rsrc_get_device_context(device_type, guid);
            if (!p_device_context)
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR: %s() Failed to obtain device context for "
                       "%s with GUID %u! Undefined behavior!\n",
                       GET_XCODER_DEVICE_TYPE_STR(device_type),
                       guid);
                return_value = NI_RETCODE_FAILURE;
                continue;
            }

            if (strncmp(dev,
                        p_device_context->p_device_info->dev_name,
                        NI_MAX_DEVICE_NAME_LEN) != 0)
            {
                continue;
            }

#ifdef _WIN32
            CloseHandle(p_device_context->lock);
#elif __linux__ || __APPLE__
#ifndef _ANDROID
            if (!shm_unlink(p_device_context->shm_name))
            {
                ni_log(NI_LOG_INFO,
                       "%s %s %s deleted\n",
                       dev,
                       GET_XCODER_DEVICE_TYPE_STR(device_type),
                       p_device_context->shm_name);
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR: %s(): %s %s %s failed to delete %s\n",
                       __FUNCTION__,
                       dev,
                       GET_XCODER_DEVICE_TYPE_STR(device_type),
                       p_device_context->shm_name,
                       strerror(NI_ERRNO));
                return_value = NI_RETCODE_FAILURE;
            }
#endif
#endif

            ni_rsrc_free_device_context(p_device_context);

#if __linux__ || __APPLE__
#ifndef _ANDROID
            ni_rsrc_get_lock_name(device_type, guid, lck_name, 32);
            if (!unlink(lck_name))
            {
                ni_log(NI_LOG_INFO,
                       "%s %s %s deleted\n",
                       dev,
                       GET_XCODER_DEVICE_TYPE_STR(device_type),
                       lck_name);
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "ERROR: %s(): %s %s %s failed to delete %s\n",
                       __FUNCTION__,
                       dev,
                       GET_XCODER_DEVICE_TYPE_STR(device_type),
                       lck_name,
                       strerror(NI_ERRNO));
                return_value = NI_RETCODE_FAILURE;
            }
#endif
#endif

            if (return_value != NI_RETCODE_SUCCESS)
            {
                continue;
            }

            p_device_queue->xcoders[ui_device_type][guid_index_i] = -1;
            p_device_queue->xcoder_cnt[ui_device_type]--;
        }
    }

    // Take p_device_queue->xcoder_cnt[ui_device_type] to contain the value 2.
    // p_device_queue->xcoders[ui_device_type]  could be [0, -1, 2, ...]
    // p_device_queue->xcoders[ui_device_type] should be [0, 2, -1, ...]
    for (ui_device_type = NI_DEVICE_TYPE_DECODER;
         ui_device_type < NI_DEVICE_TYPE_XCODER_MAX;
         ui_device_type++)
    {
        memset(guids, -1, sizeof(guids));
        guid_index_j = 0;
        for (guid_index_i = 0; guid_index_i < NI_MAX_DEVICE_CNT; guid_index_i++)
        {
            guid = p_device_queue->xcoders[ui_device_type][guid_index_i];
            if (guid != -1)
            {
                guids[guid_index_j] = guid;
                guid_index_j++;
                if (guid_index_j == p_device_queue->xcoder_cnt[ui_device_type])
                {
                    break;
                }
            }
        }
        memcpy(p_device_queue->xcoders[ui_device_type], guids, sizeof(guids));
    }

#if __linux__ || __APPLE__
#ifndef _ANDROID
    if (!msync((void *)p_device_queue,
               sizeof(ni_device_queue_t),
               MS_SYNC|MS_INVALIDATE))
    {
        ni_log(NI_LOG_INFO, "%s deleted\n", dev);
    }
    else
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): msync() failed to delete %s: %s\n",
               __FUNCTION__,
               dev,
               strerror(NI_ERRNO));
        return_value = NI_RETCODE_FAILURE;
    }
#endif
#endif

#ifdef _WIN32
    ReleaseMutex(p_device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_ULOCK, 0);
#endif

end:
    ni_rsrc_free_device_pool(p_device_pool);

    return return_value;
}

/*!*****************************************************************************
*   \brief      Remove all NetInt h/w devices from resource pool on the host.
*
*   \param      none
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_rsrc_remove_all_devices(void)
{
    char xcoder_dev_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN] = {0};
    int xcoder_dev_count = 0;
    int i = 0;
    ni_device_t *saved_coders = NULL;
    saved_coders = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (!saved_coders)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() failed to malloc memory: %s\n",
               __func__, strerror(NI_ERRNO));
        return NI_RETCODE_FAILURE;
    }
    memset(saved_coders, 0, sizeof(ni_device_t));

    // retrieve saved info from resource pool at start up
    if (NI_RETCODE_SUCCESS ==
        ni_rsrc_list_devices(
            NI_DEVICE_TYPE_ENCODER,
            saved_coders->xcoders[NI_DEVICE_TYPE_ENCODER],
            &(saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER])))
    {
        for (i = 0; i < saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
             i++)
        {
            strcpy(xcoder_dev_names[i],
                    saved_coders->xcoders[NI_DEVICE_TYPE_ENCODER][i]
                        .dev_name);
            ni_log(NI_LOG_INFO, "device %d %s retrieved\n", i,
                   xcoder_dev_names[i]);
        }
        xcoder_dev_count =
            saved_coders->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
#ifdef XCODER_311
        ni_log(NI_LOG_DEBUG,
                "%d devices retrieved from current pool at start up\n",
                xcoder_dev_count);
#else
        ni_log(NI_LOG_INFO,
                "%d devices retrieved from current pool at start up\n",
                xcoder_dev_count);
#endif
    } else
    {
        ni_log(NI_LOG_ERROR, "Error retrieving from current pool at start "
                "up\n");
    }
    free(saved_coders);

    // remove from resource pool all devices
    for (i = 0; i < xcoder_dev_count; i++)
    {
        ni_log(NI_LOG_INFO, "removing device %d %s !\n", i,
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

#if __linux__ || __APPLE__
#ifndef _ANDROID
    if (0 == shm_unlink(CODERS_SHM_NAME))
    {
        ni_log(NI_LOG_INFO, "%s deleted.\n", CODERS_SHM_NAME);
    }
    else
    {
        ni_log(NI_LOG_ERROR, "%s failed to delete !\n", CODERS_SHM_NAME);
    }

    for (i = 0; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        if (0 == unlink(XCODERS_RETRY_LCK_NAME[i]))
        {
            ni_log(NI_LOG_INFO, "%d %s deleted.\n",
                   i, XCODERS_RETRY_LCK_NAME[i]);
        }
        else
        {
            ni_log(NI_LOG_ERROR, "%d %s failed to delete !\n",
                   i, XCODERS_RETRY_LCK_NAME[i]);
        }
    }

    if (0 == unlink(CODERS_LCK_NAME))
    {
        ni_log(NI_LOG_INFO, "%s deleted.\n", CODERS_LCK_NAME);
    }
    else
    {
        ni_log(NI_LOG_ERROR, "%s failed to delete !\n", CODERS_LCK_NAME);
    }
#endif
#endif

    return NI_RETCODE_SUCCESS;
}

/*!*****************************************************************************
*   \brief      Add an NetInt h/w device into resource pool on the host.
*
*   \param[in]  p_dev  Device name represented as C string. ex "/dev/nvme0"
*   \param[in]  should_match_rev  0: transcoder firmware revision matching the
*                             library's version is NOT required for placing
*                             the transcoder into resource pool; 1: otherwise
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_INVALID_PARAM
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_rsrc_add_device(const char* dev, int should_match_rev)
{
    uint32_t i, existing_number_of_devices;

    ni_device_type_t device_type;
    ni_device_context_t *device_context;
    ni_device_pool_t *device_pool;
    ni_device_queue_t *device_queue;
    ni_retcode_t retcode;

    if (!dev)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): dev is NULL\n", __FUNCTION__);
        return NI_RETCODE_INVALID_PARAM;
    }

    device_pool = ni_rsrc_get_device_pool();
    if (!device_pool)
    {
        return NI_RETCODE_SUCCESS;
    }

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(device_pool->lock, INFINITE))
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Failed to obtain lock %p\n",
               __FUNCTION__,
               device_pool->lock);
        return NI_RETCODE_FAILURE;
    }
#elif __linux__ || __APPLE__
    lockf(device_pool->lock, F_LOCK, 0);
#endif

    retcode = NI_RETCODE_SUCCESS;

    device_type = NI_DEVICE_TYPE_ENCODER;
    device_queue = device_pool->p_device_queue;
    existing_number_of_devices = device_queue->xcoder_cnt[device_type];

    if (existing_number_of_devices == NI_MAX_DEVICE_CNT)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Limit of NI_MAX_DEVICE_CNT(%d) existing Quadra "
               "devices previously reached. Not adding %s.\n",
               __FUNCTION__,
               NI_MAX_DEVICE_CNT,
               dev);
        retcode = NI_RETCODE_FAILURE;
        LRETURN;
    }

    for (i = 0; i < existing_number_of_devices; i++)
    {
        device_context =
            ni_rsrc_get_device_context(device_type,
                                       device_queue->xcoders[device_type][i]);
        if (device_context && !strncmp(device_context->p_device_info->dev_name,
                                       dev,
                                       NI_MAX_DEVICE_NAME_LEN))
        {
            retcode = NI_RETCODE_FAILURE;
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): %s already exists in resource pool\n",
                   __FUNCTION__,
                   dev);
            ni_rsrc_free_device_context(device_context);
            LRETURN;
        }
        ni_rsrc_free_device_context(device_context);
    }

    if (!add_to_shared_memory(dev,
                              true,
                              should_match_rev,
                              device_queue))
    {
        retcode = NI_RETCODE_FAILURE;
    }

end:
#ifdef _WIN32
    ReleaseMutex(device_pool->lock);
#elif __linux__ || __APPLE__
    lockf(device_pool->lock, F_ULOCK, 0);
#endif
    return retcode;
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

static int int_cmp(const void *a, const void *b)
{
  const int *ia = (const int *)a;
  const int *ib = (const int *)b;
  return (*ia == *ib) ? 0 : ((*ia  > *ib) ? 1 : -1);
  /* integer comparison: returns negative if b > a , 0 if a == b,and positive if a > b */
}

static int ni_hw_device_info_quadra_threshold_param_t_compare(const void *pl, const void *pr)
{
    const ni_hw_device_info_quadra_threshold_param_t *ll = (const ni_hw_device_info_quadra_threshold_param_t *)pl;
    const ni_hw_device_info_quadra_threshold_param_t *rr = (const ni_hw_device_info_quadra_threshold_param_t *)pr;
    if(!ll || !rr)
    {
        return 0;
    }
    return (ll->device_type == rr->device_type) ? 0 : ((ll->device_type > rr->device_type) ? 1 : -1);
}


/*!*********************************************************************************************
*  \brief  Calculate decoder load used in ni_check_hw_info()
*          The computing method is from firmware for 770.
*          This function is used for ni_check_hw_info()
*
*  \param  decoder_param
*
*  \return task decoder load
***********************************************************************************************/
static int check_hw_info_decoder_need_load(ni_hw_device_info_quadra_decoder_param_t *decoder_param)
{
    int factor_8_10 = 1;
    uint32_t resolution = decoder_param->h * decoder_param->w;
    if(decoder_param->bit_8_10 == 10)
    {
        factor_8_10 = 2;
    }
    return resolution >= 1920*1080 ?
                            (int)(((uint64_t)(resolution)*(uint64_t)(decoder_param->fps)*(uint64_t)(factor_8_10)*100)/1440/1920/1080) :
                            (int)((uint64_t)(resolution)*(uint64_t)(decoder_param->fps)*(uint64_t)(factor_8_10)*100/2880/1280/720);
}

/*!**************************************************************************************
*  \brief  Calculate encoder load used in ni_check_hw_info()
*          The computing method is from firmware->ModelLoadSet
*          This function is used for ni_check_hw_info()
*
*  \param  encoder_param
*
*  \return task encoder load
****************************************************************************************/
static int check_hw_info_encoder_need_load(ni_hw_device_info_quadra_encoder_param_t *encoder_param)
{
    double factor = 1.0;
    double factor_codec = 1.0;
    double factor_rdoq = 1.0;
    double factor_rdoLevel = 1.0;
    double factor_lookahead = 1.0;
    double factor_8_10_bit = 1.0;
    double factor_720p = 1.0;

    int resolution = (int)(encoder_param->w * encoder_param->h);


    if(encoder_param->code_format == 3)//STD_JPGE
    {
        factor_codec = 0.67;
    }
    else{
        if((encoder_param->code_format == 0 || encoder_param->code_format == 1) &&
                encoder_param->ui8enableRdoQuant)//264 or 265
        {
            factor_rdoq = 1.32;
        }

        if((encoder_param->code_format == 1 || encoder_param->code_format ==2) &&
                encoder_param->rdoLevel > 1)
        {
            factor_rdoLevel = (encoder_param->rdoLevel == 2) ? 1.91 : 3.28;
        }

        if(encoder_param->lookaheadDepth != 0)
        {
            factor_lookahead = (double)(encoder_param->lookaheadDepth * 0.0014 + 1.012);
        }
    }
    
    if(encoder_param->bit_8_10 == 10)
    {
        factor_8_10_bit = 2;
    }
    
    factor_720p = 1.125;//consider h and w
    factor = factor_codec * factor_8_10_bit * factor_rdoq * factor_rdoLevel
        * factor_lookahead * factor_720p;
    
    //ENC_TOTAL_BIT_VOLUME_1_SEC (3840 * 2160 * 60ULL)
    //PERF_MODEL_LOAD_PERCENT = ((ENC_TOTAL_BIT_VOLUME_1_SEC / 100) * ENCODER_MULTICORE_NUM)
    //encoedr_need_load = sample_model_load/PERF_MODEL_LOAD_PERCENT
    return uint32_t(
                    ((uint32_t)((uint32_t)factor*encoder_param->fps * resolution)) / 
                                ((3840 * 2160 * 60ULL) / 100 * 4));
}

/*!*****************************************************************************
*  \brief  Calculate shared memeory uasge buffer scale
*          The computing method is from MemUsageCalculation_SR
*          This function is used for ni_check_hw_info()
*
*  \param  h
*  \param  w
*  \param  bit_8_10
*  \param  rgba
*
*  \return task shared memeory uasge buffer scale
********************************************************************************/
static int check_hw_info_shared_mem_calculate_b_scale(int h,int w,int bit_8_10,int rgba)
{
    const int stride = 128;
    int estimated_yuv_size = rgba ?
                                w * h * 4 :
                                bit_8_10 == 8 ?
                                    ( ((w + stride - 1)/stride)*stride + ((w/2 + stride - 1)/stride)*stride ) * h:
                                    ( ((w * 2 + stride - 1)/stride)*stride + ((w + stride - 1)/stride)*stride ) * h;
    const int b_unit = 1601536;//Bin_Unit_Size
    int b_scale = (estimated_yuv_size + b_unit -1) / b_unit;
    return b_scale;
}

/*!*****************************************************************************
*  \brief  Calculate encoder shared memory usage
*          The computing method is from MemUsageCalculation_SR
*          This function is used for ni_check_hw_info()
*
*  \param  encoder_param
*
*  \return task encoder shared memeory uasge
********************************************************************************/
static int check_hw_info_encoder_shared_mem_usage(const ni_hw_device_info_quadra_encoder_param_t *encoder_param)
{
    // const int p_1080 = 2073600;
    // const int p_1440 = 3686400;
    // const int p_4k = 8294400;

    if(!encoder_param)
    {
        return 0;
    }

    //cppcheck do not allow this
    // const int v32_ofCores = 0;

    // int encoder_shared_mem_usage = 0;

    // int v32_ofCores_calculate = ((v32_ofCores>0)?v32_ofCores:1)-1;
    //cppcheck do not allow this

    const int v32_ofCores_calculate = 0;

    int b_counts = (int)(v32_ofCores_calculate +
                    ((encoder_param->lookaheadDepth > 0) ? encoder_param->lookaheadDepth + 8/2 + v32_ofCores_calculate : 0 )+
                    8 +
                    (encoder_param->uploader ? 1 : 0) * 3 +
                    1) * 1;
    int b_scale = check_hw_info_shared_mem_calculate_b_scale(encoder_param->h, encoder_param->w,
                                                             encoder_param->bit_8_10, encoder_param->rgba);
    return  b_scale * b_counts;

}

/*!*****************************************************************************
*  \brief  Calculate decoder shared memory usage
*          The computing method is from MemUsageCalculation_SR
*          This function is used for ni_check_hw_info()
*
*  \param  decoder_param
*  \param  estimated_max_dpb
*
*  \return task decoder shared memeory uasge
********************************************************************************/
static int check_hw_info_decoder_shared_mem_usage(const ni_hw_device_info_quadra_decoder_param_t *decoder_param,int estimated_max_dpb)
{
    // const int p_1080 = 2073600;
    // const int p_1440 = 3686400;
    // const int p_4k = 8294400;

    if(!decoder_param)
    {
        return 0;
    }
    const int hw_frame = decoder_param->hw_frame;
    
    const int v30_xlsx = 0;
    int b_counts = 1 * (v30_xlsx + 
                        (hw_frame ? 0 : 1)*3 +
                        estimated_max_dpb);
    int b_scale = check_hw_info_shared_mem_calculate_b_scale(decoder_param->h, decoder_param->w,
                                                             decoder_param->bit_8_10, decoder_param->rgba);
    return b_scale * b_counts;
}

/*!*****************************************************************************
*  \brief  Calculate scaler shared memory usage
*          The computing method is from MemUsageCalculation_SR
*          This function is used for ni_check_hw_info()
*
*  \param  scaler_param
*  \param  estimated_max_dpb
*
*  \return task scaler shared memeory uasge
********************************************************************************/
static int check_hw_info_scaler_shared_mem_usage(const ni_hw_device_info_quadra_scaler_param_t *scaler_param,int estimated_max_dpb)
{
    // const int p_1080 = 2073600;
    // const int p_1440 = 3686400;
    // const int p_4k = 8294400;


    if(!scaler_param)
    {
        return 0;
    }
    // const int hw_frame = 1;
    //cppcheck do not allow this
    
    const int v30_xlsx = 0;
    int b_counts = 1 * (v30_xlsx + 
                        0/* (hw_frame ? 0 : 1)*3 */ +
                        estimated_max_dpb);
    int b_scale = check_hw_info_shared_mem_calculate_b_scale(scaler_param->h, scaler_param->w,
                                                             scaler_param->bit_8_10, scaler_param->rgba);
    return b_scale * b_counts;
}


/*!*************************************************************************************************
*  \brief  Remove unsupported card in ni_check_hw_info() by memroy
*          This function is used for ni_check_hw_info()
*          
*  \param[in]  card_remove
*  \param[in]  ni_card_info
*  \param[in]  card_num
*  \param[in]  coder_param
*  \param[in]  mode mode == 0->decoder, mode == 1->encoder, mode == 2->scaler, mode > 3->all
*
*  \return     NONE
****************************************************************************************************/
static void check_hw_info_remove_card_with_memory(int *card_remove, const ni_hw_device_info_quadra_t *ni_card_info, int card_num, const ni_hw_device_info_quadra_coder_param_t *coder_param,int mode)
{
    const int total = 2456;//buffer count summary
    int task_mem_usage = 0;
    int task_mem_precentage = 0;
    int i;

    if(!ni_card_info)
    {
        ni_log(NI_LOG_ERROR, "card_info is NULL mark all cards as removed\n");
        for(i = 0; i < card_num; ++i)
        {
            card_remove[i] = 1;
        }
        return;
    }

    if(mode == 0)
    {
        task_mem_usage = check_hw_info_decoder_shared_mem_usage(coder_param->decoder_param,16);
    }
    else if(mode == 1)
    {
        task_mem_usage = check_hw_info_encoder_shared_mem_usage(coder_param->encoder_param);
    }
    else if(mode == 2)
    {
        task_mem_usage = check_hw_info_scaler_shared_mem_usage(coder_param->scaler_param,16);
    }
    else if(mode > 3)
    {
        int decoder_shared_mem_usage = check_hw_info_decoder_shared_mem_usage(coder_param->decoder_param,16);
        int encoder_shared_mem_usage = check_hw_info_encoder_shared_mem_usage(coder_param->encoder_param);
        int scaler_shared_mem_usage = check_hw_info_scaler_shared_mem_usage(coder_param->scaler_param,16);
        task_mem_usage = decoder_shared_mem_usage + ((encoder_shared_mem_usage > scaler_shared_mem_usage) ? 
                                                        encoder_shared_mem_usage : scaler_shared_mem_usage);
    }
    else
    {
        ni_log(NI_LOG_ERROR, "parameter:mode is out of range\n");
        task_mem_usage = check_hw_info_decoder_shared_mem_usage(coder_param->decoder_param,16) + 
                    check_hw_info_encoder_shared_mem_usage(coder_param->encoder_param)+ 
                    check_hw_info_scaler_shared_mem_usage(coder_param->scaler_param,16);
    }

    task_mem_precentage = 100 * task_mem_usage / total;

    if(task_mem_precentage > 90)
    {
        //calculate mem usage is an estimated num , maybe too big
        task_mem_precentage = 90;
    }

    for(i = 0;i<card_num;++i)
    {
        if(card_remove[i] == 1)
        {
            continue;
        }

        if(task_mem_precentage + ni_card_info->card_info[0][i].shared_mem_usage >= 100)//all shared memory usages are the same in one card
        {
            card_remove[i] = 1;
        }
    }

}

/*!*********************************************************************************************
*  \brief  Create and alloc a pointer to ni_hw_device_info_quadra_coder_param_t
*          This function is used for ni_check_hw_info()
*          
*  \param[in]  mode  0 for decoder,1 for encoder,2 for scaler,3 for AI, >= 4 for hw_mode
*
*  \return     a pointer to ni_hw_device_info_quadra_coder_param_t on success,NULL for otherwise
***********************************************************************************************/
ni_hw_device_info_quadra_coder_param_t *ni_create_hw_device_info_quadra_coder_param(int mode)
{
    ni_hw_device_info_quadra_coder_param_t *p_coder_param = NULL;
    p_coder_param = (ni_hw_device_info_quadra_coder_param_t *)malloc(sizeof(ni_hw_device_info_quadra_coder_param_t));
    if(p_coder_param == NULL)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_coder_param\n");
        return p_coder_param;
    }
    memset(p_coder_param,0,sizeof(ni_hw_device_info_quadra_coder_param_t));
    p_coder_param->hw_mode = 0;
    if(mode == 0)//decoder
    {
        p_coder_param->decoder_param = (ni_hw_device_info_quadra_decoder_param_t *)malloc(sizeof(ni_hw_device_info_quadra_decoder_param_t));
        if(p_coder_param->decoder_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_decoder_param\n");
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
    }
    else if(mode == 1)//encoder
    {
        p_coder_param->encoder_param = (ni_hw_device_info_quadra_encoder_param_t *)malloc(sizeof(ni_hw_device_info_quadra_encoder_param_t));
        if(p_coder_param->encoder_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_encoder_param\n");
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
    }
    else if(mode == 2)//scaler
    {
        p_coder_param->scaler_param = (ni_hw_device_info_quadra_scaler_param_t *)malloc(sizeof(ni_hw_device_info_quadra_scaler_param_t));
        if(p_coder_param->scaler_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_scaler_param\n");
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
    }
    else if(mode == 3)//AI
    {
        p_coder_param->ai_param = (ni_hw_device_info_quadra_ai_param_t *)malloc(sizeof(ni_hw_device_info_quadra_ai_param_t));
        if(p_coder_param->ai_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_ai_param\n");
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
    }
    else//hw_mode
    {
        p_coder_param->encoder_param = (ni_hw_device_info_quadra_encoder_param_t *)malloc(sizeof(ni_hw_device_info_quadra_encoder_param_t));
        if(p_coder_param->encoder_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_encoder_param\n");
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
        p_coder_param->decoder_param = (ni_hw_device_info_quadra_decoder_param_t *)malloc(sizeof(ni_hw_device_info_quadra_decoder_param_t));
        if(p_coder_param->decoder_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_decoder_param\n");
            free(p_coder_param->encoder_param);
            p_coder_param->encoder_param = NULL;
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
        p_coder_param->scaler_param = (ni_hw_device_info_quadra_scaler_param_t *)malloc(sizeof(ni_hw_device_info_quadra_scaler_param_t));
        if(p_coder_param->scaler_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_scaler_param\n");
            free(p_coder_param->encoder_param);
            p_coder_param->encoder_param = NULL;
            free(p_coder_param->decoder_param);
            p_coder_param->decoder_param = NULL;
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
        p_coder_param->ai_param = (ni_hw_device_info_quadra_ai_param_t *)malloc(sizeof(ni_hw_device_info_quadra_ai_param_t));
        if(p_coder_param->ai_param == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for hw_device_info_quadra_ai_param\n");
            free(p_coder_param->encoder_param);
            p_coder_param->encoder_param = NULL;
            free(p_coder_param->decoder_param);
            p_coder_param->decoder_param = NULL;
            free(p_coder_param->scaler_param);
            p_coder_param->scaler_param = NULL;
            free(p_coder_param);
            p_coder_param = NULL;
            return p_coder_param;
        }
        p_coder_param->hw_mode = 1;
    }
    if(p_coder_param->encoder_param)
    {
        p_coder_param->encoder_param->bit_8_10 = 8;
        p_coder_param->encoder_param->code_format = 0;
        p_coder_param->encoder_param->fps = 30;
        p_coder_param->encoder_param->lookaheadDepth = 0;
        p_coder_param->encoder_param->rdoLevel = 0;
        p_coder_param->encoder_param->w = 1920;
        p_coder_param->encoder_param->h = 1080;
        p_coder_param->encoder_param->ui8enableRdoQuant = 0;
        p_coder_param->encoder_param->uploader = 0;
        p_coder_param->encoder_param->rgba = 0;
    }
    if(p_coder_param->decoder_param)
    {
        p_coder_param->decoder_param->w = 1920;
        p_coder_param->decoder_param->h = 1080;
        p_coder_param->decoder_param->bit_8_10 = 8;
        p_coder_param->decoder_param->fps = 30;
        p_coder_param->decoder_param->rgba = 0;
        p_coder_param->decoder_param->hw_frame = 1;
    }
    if(p_coder_param->scaler_param)
    {
        p_coder_param->scaler_param->h = 1920;
        p_coder_param->scaler_param->w = 1080;
        p_coder_param->scaler_param->bit_8_10 = 8;
        p_coder_param->scaler_param->rgba = 0;
    }
    if(p_coder_param->ai_param)
    {
        p_coder_param->ai_param->h = 1920;
        p_coder_param->ai_param->w = 1080;
        p_coder_param->ai_param->bit_8_10 = 8;
        p_coder_param->ai_param->rgba = 0;
    }
    return p_coder_param; 
}


/*!*********************************************************************************************
*  \brief   Free resource in p_hw_device_info_quadra_coder_param
*           This function is used for ni_check_hw_info()
*          
*  \param[in]  device_type_num
*
*  \param[in]  avaliable_card_num
*
*  \return     a pointer to ni_hw_device_info_quadra_t on success,NULL for otherwise
***********************************************************************************************/
void ni_destory_hw_device_info_quadra_coder_param(ni_hw_device_info_quadra_coder_param_t *p_hw_device_info_quadra_coder_param)
{
    if(p_hw_device_info_quadra_coder_param == NULL)
    {
        return;
    }
    if(p_hw_device_info_quadra_coder_param->encoder_param)
    {
        free(p_hw_device_info_quadra_coder_param->encoder_param);
        p_hw_device_info_quadra_coder_param->encoder_param = NULL;
    }
    if(p_hw_device_info_quadra_coder_param->decoder_param)
    {
        free(p_hw_device_info_quadra_coder_param->decoder_param);
        p_hw_device_info_quadra_coder_param->decoder_param = NULL;
    }
    if(p_hw_device_info_quadra_coder_param->scaler_param)
    {
        free(p_hw_device_info_quadra_coder_param->scaler_param);
        p_hw_device_info_quadra_coder_param->scaler_param = NULL;
    }
    if(p_hw_device_info_quadra_coder_param->ai_param)
    {
        free(p_hw_device_info_quadra_coder_param->ai_param);
        p_hw_device_info_quadra_coder_param->ai_param = NULL;
    }
    free(p_hw_device_info_quadra_coder_param);
    return;
}

/*!*********************************************************************************************
*  \brief      Create a ni_hw_device_info_quadra_t
*              This function is used for ni_check_hw_info()
*
*  \param[in]  device_type_num
*  \param[in]  avaliable_card_num
*
*  \return     a pointer to ni_hw_device_info_quadra_t on success,NULL for otherwise
***********************************************************************************************/
ni_hw_device_info_quadra_t *ni_hw_device_info_alloc_quadra(int device_type_num,int avaliable_card_num)
{
    int i;
    ni_hw_device_info_quadra_t *p_hw_device_info = (ni_hw_device_info_quadra_t *)malloc(sizeof(ni_hw_device_info_quadra_t));
    if(!p_hw_device_info)
    {
        ni_log(NI_LOG_ERROR,"ERROR: Failed to allocate memory for p_hw_device_info_quadra_t\n");
        goto p_hw_device_info_end;
    }

    p_hw_device_info->device_type_num = device_type_num;
    p_hw_device_info->device_type = (ni_device_type_t *)malloc(sizeof(ni_device_type_t) * device_type_num);
    if(!p_hw_device_info->device_type)
    {
        ni_log(NI_LOG_ERROR,"ERROR: Failed to allocate memory for p_hw_device_info_quadra_t->device_type\n");
        goto device_type_end;
    }

    p_hw_device_info->card_info =  (ni_card_info_quadra_t **)malloc(sizeof(ni_card_info_quadra_t*) * p_hw_device_info->device_type_num);
    if(p_hw_device_info->card_info == NULL)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for p_hw_device_info_quadra_t->card_info\n", NI_ERRNO);
        p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
        //unlock_and_return
        goto card_info_end;
    }
    memset(p_hw_device_info->card_info, 0, sizeof(ni_card_info_quadra_t*) * p_hw_device_info->device_type_num);

    p_hw_device_info->available_card_num = avaliable_card_num;
    p_hw_device_info->consider_mem = 0;
    for(i = 0; i < p_hw_device_info->device_type_num; ++i)
    {
        p_hw_device_info->card_info[i] = (ni_card_info_quadra_t *)malloc(sizeof(ni_card_info_quadra_t) * p_hw_device_info->available_card_num);
        if(p_hw_device_info->card_info[i] == NULL)
        {
            ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for p_hw_device_info_quadra_t->card_info\n", NI_ERRNO);
            goto card_info_i_end;
        }
    }

    return p_hw_device_info;

card_info_i_end:
    for(i = 0; i < p_hw_device_info->device_type_num; ++i)
    {
        if(p_hw_device_info->card_info[i])
        {
            free(p_hw_device_info->card_info[i]);
            p_hw_device_info->card_info[i] = NULL;
        }
    }
    free(p_hw_device_info->card_info);
    p_hw_device_info->card_info = NULL;
card_info_end:
    free(p_hw_device_info->device_type);
    p_hw_device_info->device_type = NULL;
device_type_end:
    free(p_hw_device_info);
    p_hw_device_info = NULL;
p_hw_device_info_end:
    return NULL;
}

/*!*********************************************************************************************
*  \brief    Free resource in a pointer of ni_hw_device_info_quadra_t
*            This function is used for ni_check_hw_info()
*   
*  \param[in]  p_hw_device_info  Poiner to a ni_hw_device_info_quadra_t struct
*
*  \return     None
***********************************************************************************************/
void ni_hw_device_info_free_quadra(ni_hw_device_info_quadra_t *p_hw_device_info)
{
    int i;
    if(!p_hw_device_info)
    {
        return;
    }
    free(p_hw_device_info->device_type);
    p_hw_device_info->device_type = NULL;
    for(i = 0; i < p_hw_device_info->device_type_num; ++i)
    {
        free(p_hw_device_info->card_info[i]);
        p_hw_device_info->card_info[i] = NULL;
    }
    free(p_hw_device_info->card_info);
    p_hw_device_info->card_info = NULL;
    free(p_hw_device_info);
    return;
}

int ni_check_hw_info(ni_hw_device_info_quadra_t **pointer_to_p_hw_device_info, 
                     int task_mode,
                     ni_hw_device_info_quadra_threshold_param_t *hw_info_threshold_param,
                     ni_device_type_t preferential_device_type,
                     ni_hw_device_info_quadra_coder_param_t * coder_param,
                     int hw_mode,
                     int consider_mem)
{
    //order of ni_hw_device_info_quadra_threshold_param_t *hw_info_threshold_param, order
    //order of ni_device_type_t all_need_device_type
    //ni_device_type_t preferential_device_type, in the first place
    //other order of device_type ni_device_type_t adjust to the order of all_need_device_type
    //int hw_info_param_num = hw_mode ? 2 : 1;adjust to 3
    int should_match_rev = 1;
    int module_count = 1;
    ni_device_context_t *dev_ctxt_arr = NULL;
    int32_t *module_id_arr = NULL;
    // int32_t best_load_module_id = -1;
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_queue_t *coders = NULL;
    ni_device_context_t *p_device_context = NULL;
    ni_session_context_t xCtxt = { 0 };
    int *card_remove = NULL;//cards which are not satisfied threshold 
    // uint64_t reserved_memory = 0;
    // int needed_memory = 0;
    uint64_t decoder_need_load = 0;
    uint64_t encoder_need_load = 0;
    int retval = 0;//1 for success , 0 for failure
    int ret = 0;
    int hw_info_param_num = hw_mode ? 4 : 1;
    int device_type_num = 1;
    const int all_device_type_num = 4;

    ni_device_type_t all_need_device_type[4] = {NI_DEVICE_TYPE_DECODER,NI_DEVICE_TYPE_ENCODER,NI_DEVICE_TYPE_SCALER,NI_DEVICE_TYPE_AI};//decoder encoder scaler and AI
    /*note: preference device type will be moved to all_need_device_type[0],
    * and sequence of device type in other structs(such as p_hw_device_info.device_type) will be changed in the order of all_need_device_type
    */

    int i = 0;
    bool b_valid = false;


    // int *mem_usage = NULL;

    ni_hw_device_info_quadra_t *p_hw_device_info = NULL;

    //check hw_info_param
    if(!pointer_to_p_hw_device_info)
    {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: pointer_to_p_hw_device_info is NULL.\n");
        return retval;
    }

    if(*pointer_to_p_hw_device_info)
    {
        p_hw_device_info = *pointer_to_p_hw_device_info;
        b_valid = true;
    }

    // Check input parameters here
    if(!coder_param){
      if(b_valid)
      {
        p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
      }
      ni_log(NI_LOG_ERROR, "Error: Invalid input params: coder_param is NULL.\n");
      return retval;
    }else if(hw_mode && hw_mode != coder_param->hw_mode)
    {
      ni_log(NI_LOG_ERROR, "Error: Invalid input params: hw_mode = %d, coder_param->hw_mode = %d\n",
             hw_mode,coder_param->hw_mode);
      return retval;
    }else if(!hw_mode)
    {
      if(preferential_device_type == NI_DEVICE_TYPE_ENCODER && coder_param->encoder_param == NULL)
      {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: preferential_device_type == NI_DEVICE_TYPE_ENCODER but coder_param->encoder_param == NULL\n");
        return retval;
      }
      if(preferential_device_type == NI_DEVICE_TYPE_DECODER && coder_param->decoder_param == NULL)
      {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: preferential_device_type == NI_DEVICE_TYPE_DECODER but coder_param->decoder_param == NULL\n");
        return retval;
      }
      if(preferential_device_type == NI_DEVICE_TYPE_SCALER && coder_param->scaler_param == NULL)
      {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: preferential_device_type == NI_DEVICE_TYPE_SCALER but coder_param->scaler_param == NULL\n");
        return retval;
      }
      if(preferential_device_type == NI_DEVICE_TYPE_AI && coder_param->ai_param == NULL)
      {
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: preferential_device_type == NI_DEVICE_TYPE_AI but coder_param->ai_param == NULL\n");
        return retval;
      }
    }


    if((task_mode != 0 && task_mode != 1) ||
       (preferential_device_type != NI_DEVICE_TYPE_DECODER && preferential_device_type != NI_DEVICE_TYPE_ENCODER && 
        preferential_device_type != NI_DEVICE_TYPE_SCALER && preferential_device_type != NI_DEVICE_TYPE_AI))//scaler
    {
        if(b_valid)
        {
            p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
        }
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: realtime %d device_type %d\n",
               task_mode, preferential_device_type);
        return retval;
    }

    if(!hw_info_threshold_param)
    {
        if(b_valid)
        {
            p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
        }
        ni_log(NI_LOG_ERROR, "Error: Invalid input params: hw_info_threshold_param is NULL.\n");
        return retval;
    }else{
      for(i = 0;i < hw_info_param_num; ++i)
      {
        if(hw_info_threshold_param[i].load_threshold < 0||//check this
           hw_info_threshold_param[i].load_threshold > 100 ||
           hw_info_threshold_param[i].task_num_threshold < 0 ||
           hw_info_threshold_param[i].task_num_threshold > NI_MAX_CONTEXTS_PER_HW_INSTANCE||
           (   hw_info_threshold_param[i].device_type != NI_DEVICE_TYPE_DECODER &&
               hw_info_threshold_param[i].device_type != NI_DEVICE_TYPE_ENCODER &&
               hw_info_threshold_param[i].device_type != NI_DEVICE_TYPE_SCALER &&
               hw_info_threshold_param[i].device_type != NI_DEVICE_TYPE_AI))
        {
          if(b_valid)
          {
            p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
          }
          ni_log(NI_LOG_ERROR, "Error: Invalid input params: In %s, hw_info_threshold_param[%d].device_type = %d, hw_info_threshold_param[%d].load_threshold = %d, hw_info_threshold_param[%d].task_num_threshold = %d\n",
                 (hw_mode ? "hardware_mode":"software_mode"), i, hw_info_threshold_param[i].device_type, i, hw_info_threshold_param[i].load_threshold, i, hw_info_threshold_param[i].task_num_threshold);
          return retval;
        }
      }
    }
    // all parameters have been checked

    /*Customer wants to set fps = 120 when fps > 120 and set fps = 5 when fps < 5*/
    if(coder_param->encoder_param)
    {
        if(coder_param->encoder_param->fps > 120)
        {
            coder_param->encoder_param->fps = 120;
        }
        if(coder_param->encoder_param->fps < 5)
        {
            coder_param->encoder_param->fps = 5;
        }
    }
    if(coder_param->decoder_param)
    {
        if(coder_param->decoder_param->fps > 120)
        {
            coder_param->decoder_param->fps = 120;
        }
        if(coder_param->decoder_param->fps < 5)
        {
            coder_param->decoder_param->fps = 5;
        }
    }

    //ni_rsrc_init() is unsafe in multi process/thread ,do not call ni_rsrc_init() here

    if (NI_RETCODE_SUCCESS != (ret = ni_rsrc_refresh(should_match_rev)))
    {
      ni_log(NI_LOG_ERROR, "Error: resource pool records might be corrupted!!\n");
      if(b_valid)
      {
        p_hw_device_info->err_code = ret;
      }
      return retval;
    }

    p_device_pool = ni_rsrc_get_device_pool();
    if (!p_device_pool)
    {
        ni_log(NI_LOG_ERROR, "Error:Can not get device pool info ..\n");
        if(b_valid)
        {
            p_hw_device_info->err_code = NI_RETCODE_ERROR_GET_DEVICE_POOL;
        }
        return retval;
    }

#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      ni_log(NI_LOG_ERROR, "ERROR: Failed to obtain mutex: %p\n", p_device_pool->lock);
      LRETURN;
    }
#elif defined(__linux__)
    if (lockf(p_device_pool->lock, F_LOCK, 0))
    {
        ni_log(NI_LOG_ERROR, "Error lockf() failed\n");
        if(b_valid)
        {
            p_hw_device_info->err_code = NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
        }
        LRETURN;
    }
#endif

    coders = p_device_pool->p_device_queue;
    if (!coders)
    {
        ni_log(NI_LOG_ERROR, "Error pdevice_queue null!!\n");
        if(b_valid)
        {
            p_hw_device_info->err_code =  NI_RETCODE_ERROR_GET_DEVICE_POOL;
        }
        LRETURN;
    }

    //////Init p_hw_device_info start//////


     // move preferential_device_type in all_need_device_type to top
    for(i = 0; i < all_device_type_num; ++i)
    {
        if(all_need_device_type[i] == preferential_device_type)
        {
            ni_device_type_t tmp = all_need_device_type[i];
            all_need_device_type[i] = all_need_device_type[0];
            all_need_device_type[0] = tmp;
            break;
        }
        //ni_device_type_t is a enum
    }
    qsort(&all_need_device_type[1], all_device_type_num - 1, sizeof(ni_device_type_t), int_cmp);

    //adjust order of hw_info_threshold_param as all_need_device_type
    if(hw_mode)
    {
        // default device_type_num is 1
        device_type_num = 4;//decoder and encoder and scaler and AI
        //adjust order of hw_info_threshold_param as all_need_device_type
        for(i = 0; i < device_type_num; ++i)
        {
            if(hw_info_threshold_param[i].device_type == preferential_device_type)
            {
                ni_hw_device_info_quadra_threshold_param_t tmp = hw_info_threshold_param[i];
                hw_info_threshold_param[i] = hw_info_threshold_param[0];
                hw_info_threshold_param[0] = tmp;
                break;
            }
        }
        qsort(&hw_info_threshold_param[1], device_type_num - 1, 
              sizeof(ni_hw_device_info_quadra_threshold_param_t), ni_hw_device_info_quadra_threshold_param_t_compare);
    }

    if(!(*pointer_to_p_hw_device_info))
    {
        p_hw_device_info = ni_hw_device_info_alloc_quadra(device_type_num,coders->xcoder_cnt[preferential_device_type]);
        if(!p_hw_device_info)
        {
            ni_log(NI_LOG_ERROR,"ERROR: Failed to allocate memory for p_hw_device_info\n");
            LRETURN;
        }
        *pointer_to_p_hw_device_info = p_hw_device_info;
        b_valid = true;
    }
    else
    {
        // p_hw_device_info = *pointer_to_p_hw_device_info;
        //already set this before

        //if user alloc hw_device_info themself check it
        if(!p_hw_device_info->device_type)
        {
            ni_log(NI_LOG_ERROR,"ERROR: pointer_to_p_hw_device_info is not a pointer to NULL, but ->device_type is NULL\n");
            p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
        if(!p_hw_device_info->card_info)
        {
            ni_log(NI_LOG_ERROR,"ERROR: pointer_to_p_hw_device_info is not a pointer to NULL, but ->card_info is NULL\n");
            p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
        for(i = 0; i < device_type_num; ++i)
        {
            if(!p_hw_device_info->card_info[i])
            {
                ni_log(NI_LOG_ERROR,"ERROR: pointer_to_p_hw_device_info is not a pointer to NULL, but ->card_info[%d] is NULL\n", i);
                p_hw_device_info->err_code = NI_RETCODE_PARAM_INVALID_VALUE;
                LRETURN;
            }
        }
    }
    //p_hw_device_info alloc succeed

    //coders->decoders_cnt == coder->encoders_cnt has checked in the ni_logan_rsrc_refresh
    p_hw_device_info->available_card_num = coders->xcoder_cnt[preferential_device_type];
    p_hw_device_info->device_type_num = device_type_num;

    for(i = 0; i < p_hw_device_info->device_type_num; ++i)
    {
        p_hw_device_info->device_type[i] = all_need_device_type[i];
        for(int j = 0; j < p_hw_device_info->available_card_num; ++j)
        {
            p_hw_device_info->card_info[i][j].card_idx = -1;
            p_hw_device_info->card_info[i][j].load = -1;
            p_hw_device_info->card_info[i][j].model_load = -1;
            p_hw_device_info->card_info[i][j].task_num = -1;
            p_hw_device_info->card_info[i][j].max_task_num = NI_MAX_CONTEXTS_PER_HW_INSTANCE;
            p_hw_device_info->card_info[i][j].shared_mem_usage = -1;
        }
    }

    p_hw_device_info->card_current_card = -1;
    p_hw_device_info->err_code = NI_RETCODE_SUCCESS;
    //////Init p_hw_device_info done//////

    if(consider_mem)
    {
        // mem_usage = (int *)malloc(sizeof(int) * (p_hw_device_info->available_card_num));
        // if(!mem_usage)
        // {
        //     ni_log(NI_LOG_ERROR,"ERROR: Failed to allocate memory for p_hw_device_info\n");
        //     p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
        //     LRETURN;
        // }
        p_hw_device_info->consider_mem = 1;
    }
    else
    {
        p_hw_device_info->consider_mem = 0;
    }
    
    if (p_hw_device_info->available_card_num <= 0)
    {
        LRETURN;
    }

    card_remove = (int32_t *)malloc(sizeof(int32_t) * p_hw_device_info->available_card_num);
    if (!card_remove)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for card_remove\n", NI_ERRNO);
        p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(card_remove, 0, sizeof(int32_t) * p_hw_device_info->available_card_num);


    module_count = coders->xcoder_cnt[preferential_device_type];
    dev_ctxt_arr = (ni_device_context_t *)malloc(sizeof(ni_device_context_t) * module_count);
    if (!dev_ctxt_arr)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for ni_rsrc_detect\n", NI_ERRNO);
        p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    module_id_arr = (int32_t *)malloc(sizeof(int32_t) * module_count);
    if (!module_id_arr)
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for ni_rsrc_detect\n", NI_ERRNO);
        p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    //info start
    memcpy(module_id_arr, coders->xcoders[preferential_device_type], sizeof(int32_t) * module_count);//copy useful card

    //First module ID in coders->decoders/encoders is of lowest load
    // memcpy((void *)&best_load_module_id, module_id_arr, sizeof(int32_t));
    qsort(module_id_arr, module_count, sizeof(int32_t), int_cmp);// why?

    // p_hw_device_info->card_current_card = best_load_module_id;

    for(i = 0; i < p_hw_device_info->available_card_num; ++i)
    {
        for(int j = 0; j < device_type_num; ++j)//need to fix this
        {
            memset(&xCtxt, 0, sizeof(xCtxt));
            p_device_context = ni_rsrc_get_device_context(all_need_device_type[j], module_id_arr[i]);
            if(p_device_context)
            {
                xCtxt.blk_io_handle = ni_device_open(p_device_context->p_device_info->blk_name,
                                                  &(xCtxt.max_nvme_io_size));
                xCtxt.device_handle = xCtxt.blk_io_handle;
                //Check device can be opend
                if (NI_INVALID_DEVICE_HANDLE == xCtxt.device_handle)
                {
                    ni_log(NI_LOG_ERROR, "Error open device %s, blk device %s\n",
                            p_device_context->p_device_info->dev_name,
                            p_device_context->p_device_info->blk_name);
                    ni_rsrc_free_device_context(p_device_context);
                    card_remove[i] = 1;
                    continue;
                }
#ifdef _WIN32
                xCtxt.event_handle = ni_create_event();
                if (NI_INVALID_EVENT_HANDLE == xCtxt.event_handle)
                {
                    ni_rsrc_free_device_context(p_device_context);
                    ni_device_close(xCtxt.device_handle);
                    ni_log(NI_LOG_ERROR, "ERROR %d: print_perf() create event\n", NI_ERRNO);
                    card_remove[i] = 1;
                    continue;
                }
#endif
                //Check decode/encode can be queired
                xCtxt.hw_id = p_device_context->p_device_info->hw_id;
                if (NI_RETCODE_SUCCESS != ni_device_session_query(&xCtxt, all_need_device_type[j]))
                {
                    ni_device_close(xCtxt.device_handle);
#ifdef _WIN32
                    ni_close_event(xCtxt.event_handle);
#endif
                    ni_log(NI_LOG_ERROR, "Error query %s %s %s.%d\n", 
                                          (all_need_device_type[j] == NI_DEVICE_TYPE_DECODER) ? "decoder" : 
                                          (all_need_device_type[j] == NI_DEVICE_TYPE_ENCODER) ? "encoder" :
                                          (all_need_device_type[j] == NI_DEVICE_TYPE_SCALER) ? "scaler " : "AIs   ",
                            p_device_context->p_device_info->dev_name,
                            p_device_context->p_device_info->blk_name,
                            p_device_context->p_device_info->hw_id);
                    ni_rsrc_free_device_context(p_device_context);
                    card_remove[i] = 1;
                    continue;
                }
                ni_device_close(xCtxt.device_handle);
#ifdef _WIN32
                ni_close_event(xCtxt.event_handle);
#endif
                if (0 == xCtxt.load_query.total_contexts)
                {
                    xCtxt.load_query.current_load = 0;
                }

                p_hw_device_info->card_info[j][i].card_idx      = p_device_context->p_device_info->module_id;

                //use model_load in card remove for encoder just like before
#ifdef XCODER_311
                p_hw_device_info->card_info[j][i].load          = xCtxt.load_query.current_load; //monitor changes this
                                                                  
#else
                p_hw_device_info->card_info[j][i].load          =
                                (xCtxt.load_query.total_contexts == 0 || xCtxt.load_query.current_load > xCtxt.load_query.fw_load) ? xCtxt.load_query.current_load : xCtxt.load_query.fw_load;
#endif
                // module_load_user_can_not_see[j][i] = xCtxt.load_query.fw_model_load;
                p_hw_device_info->card_info[j][i].model_load = xCtxt.load_query.fw_model_load;

                p_hw_device_info->card_info[j][i].task_num      = xCtxt.load_query.total_contexts;
                p_hw_device_info->card_info[j][i].max_task_num  = NI_MAX_CONTEXTS_PER_HW_INSTANCE;
                if(consider_mem)
                {
                    // mem_usage[i] = xCtxt.load_query.fw_share_mem_usage;
                    p_hw_device_info->card_info[j][i].shared_mem_usage = xCtxt.load_query.fw_share_mem_usage;
                }
                ni_rsrc_free_device_context(p_device_context);
                p_device_context = NULL;
            }
            else
            {
                ni_log(NI_LOG_ERROR, "ERROR %d: Failed to get device context\n", NI_ERRNO);
                p_hw_device_info->err_code = NI_RETCODE_ERROR_MEM_ALOC;
                LRETURN;
            }
        }

    }

    if(coder_param->decoder_param)
    {
        decoder_need_load = check_hw_info_decoder_need_load(coder_param->decoder_param);
    }
    if(coder_param->encoder_param)
    {
        encoder_need_load = check_hw_info_encoder_need_load(coder_param->encoder_param);
    }
    // if(coder_param->scaler_param)
    // {
            //cane not estimate scaler load now
    // }

    //mark the card removed depends on the load_threshold or task_num_threshold
    //in hw_mode ,device_type_num == 3 but only encoder and decoder will be taken into account,we can not estimate scaler load now
    //in sw mode ,device_type_num == 1 only preferitial_device_type will be taken into account
    for(i = 0; i < p_hw_device_info->available_card_num; ++i)
    {
        for(int j = 0; j < p_hw_device_info->device_type_num; ++j)
        {
#ifdef XCODER_311
            ni_log(NI_LOG_DEBUG, "%s Card[%3d], load: %3d,  task_num: %3d,  model_load: %3d, shared_mem_usage: %3d\n",
#else
            ni_log(NI_LOG_INFO, "%s Card[%3d], load: %3d,  task_num: %3d,  model_load: %3d, shared_mem_usage: %3d\n",
#endif
                    (p_hw_device_info->device_type[j] == NI_DEVICE_TYPE_DECODER ? "Decoder" : 
                    ((p_hw_device_info->device_type[j] == NI_DEVICE_TYPE_ENCODER) ? "Encoder" :
                    (p_hw_device_info->device_type[j] == NI_DEVICE_TYPE_SCALER) ? "Scaler " : "AIs    ")),
                    p_hw_device_info->card_info[j][i].card_idx,
                    p_hw_device_info->card_info[j][i].load,
                    p_hw_device_info->card_info[j][i].task_num,
                    p_hw_device_info->card_info[j][i].model_load,
                    p_hw_device_info->card_info[j][i].shared_mem_usage);
            
            if(card_remove[i] == 1)
            {
                continue;//for print
            }

            if(all_need_device_type[j] == NI_DEVICE_TYPE_DECODER)
            {
                if(decoder_need_load + p_hw_device_info->card_info[j][i].model_load >= 100)
                {
                    card_remove[i] = 1;
                }
            }

            if(all_need_device_type[j] == NI_DEVICE_TYPE_ENCODER)
            {
                if(encoder_need_load + p_hw_device_info->card_info[j][i].model_load >= 100)
                {
                    card_remove[i] =1;
                }
            }
            //TODO scaler load

            if (task_mode)
            {
                //in the past customer use model_load to represent load
                //for encoder consider model_load like before
                if(all_need_device_type[i] == NI_DEVICE_TYPE_ENCODER)
                {
                    if (p_hw_device_info->card_info[j][i].model_load > hw_info_threshold_param[j].load_threshold ||
                    p_hw_device_info->card_info[j][i].task_num + 1 >= hw_info_threshold_param[j].task_num_threshold)
                    {
                        card_remove[i] = 1;
                    }
                }
                else
                {
                    if (p_hw_device_info->card_info[j][i].load > hw_info_threshold_param[j].load_threshold ||
                    p_hw_device_info->card_info[j][i].task_num + 1 >= hw_info_threshold_param[j].task_num_threshold)
                    {
                        card_remove[i] = 1;
                    }
                }
            }
            else
            {
                if (p_hw_device_info->card_info[j][i].task_num > hw_info_threshold_param[j].task_num_threshold)
                {
                    card_remove[i] = 1;
                }
            }
        }
    }

    if(consider_mem)
    {
        if(hw_mode)
        {
            check_hw_info_remove_card_with_memory(card_remove,p_hw_device_info,p_hw_device_info->available_card_num,coder_param,10);
        }
        else if(preferential_device_type == 0)
        {
            check_hw_info_remove_card_with_memory(card_remove,p_hw_device_info,p_hw_device_info->available_card_num,coder_param,0);
        }
        else if(preferential_device_type == 1)
        {
            check_hw_info_remove_card_with_memory(card_remove,p_hw_device_info,p_hw_device_info->available_card_num,coder_param,1);
        }
        else if(preferential_device_type == 2)
        {
            check_hw_info_remove_card_with_memory(card_remove,p_hw_device_info,p_hw_device_info->available_card_num,coder_param,2);
        }
    }

    if (task_mode)
    {
        //select the min_task_num
        //p_hw_device_info->card_info[0] is the preferential_device_type
        int min_task_num = p_hw_device_info->card_info[0][0].max_task_num;
        for (i = 0; i < p_hw_device_info->available_card_num; i++)
        {
            if (p_hw_device_info->card_info[0][i].task_num < min_task_num &&
                card_remove[i] == 0)
            {
                min_task_num = p_hw_device_info->card_info[0][i].task_num;
            }
        }
        //select the min load
        int min_load = 100;
        for (i = 0; i < p_hw_device_info->available_card_num; i++)
        {
            if (p_hw_device_info->card_info[0][i].load < min_load &&
                card_remove[i] == 0 &&
                p_hw_device_info->card_info[0][i].task_num == min_task_num)
            {
                min_load = p_hw_device_info->card_info[0][i].load;
                p_hw_device_info->card_current_card = p_hw_device_info->card_info[0][i].card_idx;
            }
        }
    }
    else
    {
        //p_hw_device_info->card_info[0] is the preferential_device_type
        //select the min task num
        int min_task_num = p_hw_device_info->card_info[0][0].max_task_num;
        for (i = 0; i < p_hw_device_info->available_card_num; i++)
        {
            if (p_hw_device_info->card_info[0][i].task_num < min_task_num &&
                card_remove[i] == 0)
            {
                p_hw_device_info->card_current_card = p_hw_device_info->card_info[0][i].card_idx;
                min_task_num = p_hw_device_info->card_info[0][i].task_num;
            }
        }
    }

    if (p_hw_device_info->card_current_card >= 0)
    {
        retval = 1; //has the card to use
    }

END:
#ifdef _WIN32
  ReleaseMutex((HANDLE)p_device_pool->lock);

#elif defined(__linux__)
  if (lockf(p_device_pool->lock, F_ULOCK,0))
  {
    ni_log(NI_LOG_ERROR, "Error lockf() failed\n");
    if(p_hw_device_info)
    {
      p_hw_device_info->err_code = NI_RETCODE_ERROR_UNLOCK_DEVICE;
    }
    retval = 0;
  }
  ni_rsrc_free_device_pool(p_device_pool);
  p_device_context = NULL;
#endif
  fflush(stderr);
  if (module_id_arr)
  {
    free(module_id_arr);
    module_id_arr = NULL;
  }
  if (dev_ctxt_arr)
  {
    free(dev_ctxt_arr);
    dev_ctxt_arr = NULL;
  }
  if (card_remove)
  {
    free(card_remove);
    card_remove = NULL;
  }
  if(b_valid)
  {
    if(hw_mode)
    {
#ifdef XCODER_311
      ni_log(NI_LOG_DEBUG, "In hw_mode select card_current_card %d retval %d\n",
#else
      ni_log(NI_LOG_INFO, "In hw_mode select card_current_card %d retval %d\n",
#endif
            p_hw_device_info->card_current_card, retval);
    }
    else
    {
#ifdef XCODER_311
      ni_log(NI_LOG_DEBUG, "In sw_mode select device_type %s card_current_card %d retval %d\n",
#else
      ni_log(NI_LOG_INFO, "In sw_mode select device_type %s card_current_card %d retval %d\n",
#endif
             ((preferential_device_type == 0) ? "decode" : (preferential_device_type == 1 ? "encode" : "scaler")), p_hw_device_info->card_current_card, retval);
    }
  }
  return retval;
}


/*!*****************************************************************************
*   \brief      Allocate resources for decoding/encoding, based on the provided rule
*
*   \param[in]  device_type NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]  rule        allocation rule
*   \param[in]  codec       EN_H264 or EN_H265
*   \param[in]  width       width of video resolution
*   \param[in]  height      height of video resolution
*   \param[in]  frame_rate   video stream frame rate
*   \param[out] p_load      the p_load that will be generated by this encoding
*                           task. Returned *only* for encoder for now.
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
ni_device_context_t *ni_rsrc_allocate_auto(
    ni_device_type_t device_type,
    ni_alloc_rule_t rule,
    ni_codec_t codec,
    int width, int height,
    int frame_rate,
    uint64_t *p_load)
{
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_info_t *p_device_info = NULL;
    ni_device_context_t *p_device_context = NULL;
    ni_session_context_t p_session_context = {0};
    int *coders = NULL;
    int i = 0, count = 0, rc = NI_RETCODE_FAILURE;
    int guid = -1;
    int load = 0;
    uint32_t num_sw_instances = 0;
    int least_model_load = 0;
    uint64_t job_mload = 0;


    if(device_type != NI_DEVICE_TYPE_DECODER && device_type != NI_DEVICE_TYPE_ENCODER)
    {
        ni_log2(NULL, NI_LOG_ERROR, "ERROR: Device type %d is not allowed\n", device_type);
        return NULL;
    }
    /*! retrieve the record and based on the allocation rule specified, find the
       least loaded or least number of s/w instances among the coders */
    p_device_pool = ni_rsrc_get_device_pool();

    if (!p_device_pool)
    {
        ni_log2(NULL, NI_LOG_ERROR, "ERROR: %s() Could not get device pool\n", __func__);
        return NULL;
    }

    ni_device_session_context_init(&p_session_context);
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval) //we got the mutex
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() failed to obtain mutex: %p\n", __func__, p_device_pool->lock);
    }
#elif __linux__ || __APPLE__
    lockf(p_device_pool->lock, F_LOCK, 0);
#endif

    coders = p_device_pool->p_device_queue->xcoders[device_type];
    count = p_device_pool->p_device_queue->xcoder_cnt[device_type];

    for (i = 0; i < count; i++)
    {
        /*! get the individual device_info info and check the load/num-of-instances */
        p_device_context = ni_rsrc_get_device_context(device_type, coders[i]);
        if (!p_device_context)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s() ni_rsrc_get_device_context() failed\n", __func__);
            continue;
        }

        // p_first retrieve status from f/w and update storage

        p_session_context.blk_io_handle = ni_device_open(p_device_context->p_device_info->dev_name, &p_session_context.max_nvme_io_size);
        p_session_context.device_handle = p_session_context.blk_io_handle;

        if (NI_INVALID_DEVICE_HANDLE == p_session_context.device_handle)
        {
            ni_log(NI_LOG_ERROR, "ERROR %s() ni_device_open() %s: %s\n",
                   __func__, p_device_context->p_device_info->dev_name,
                   strerror(NI_ERRNO));
            ni_rsrc_free_device_context(p_device_context);
            continue;
        }

        p_session_context.hw_id = p_device_context->p_device_info->hw_id;
        rc = ni_device_session_query(&p_session_context, device_type);

        ni_device_close(p_session_context.device_handle);

        if (NI_RETCODE_SUCCESS != rc)
        {
            ni_log(NI_LOG_ERROR, "ERROR: query %s %s.%d\n",
                   g_device_type_str[device_type],
                   p_device_context->p_device_info->dev_name,
                   p_device_context->p_device_info->hw_id);
            ni_rsrc_free_device_context(p_device_context);
            continue;
        }

#ifdef _WIN32
        if (WAIT_ABANDONED == WaitForSingleObject(p_device_context->lock, INFINITE)) // no time-out interval) //we got the mutex
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
    }

    if (guid >= 0)
    {
        p_device_context = ni_rsrc_get_device_context(device_type, guid);
        if (!p_device_context)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s() ni_rsrc_get_device_context() failed\n", __func__);
            LRETURN;
        }

        if (NI_DEVICE_TYPE_ENCODER == device_type)
        {
            job_mload = width * height * frame_rate;
        }
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
    ni_device_session_context_clear(&p_session_context);
    ni_rsrc_free_device_pool(p_device_pool);

    if (p_load)
    {
        *p_load = job_mload;
    }
    return p_device_context;
}
