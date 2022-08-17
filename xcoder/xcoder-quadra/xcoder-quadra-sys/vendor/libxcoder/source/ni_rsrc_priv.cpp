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
*   \file   ni_rsrc_priv.c
*
*  \brief  Private routines related to resource management of NI Quadra devices
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#if __linux__ || __APPLE__
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif

#ifdef _ANDROID
#include "ni_rsrc_api_android.h"
#endif

#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_nvme.h"
#include "ni_log.h"
#include "ni_util.h"

uint32_t g_xcoder_stop_process = 0;

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_rsrc_fill_device_info(ni_device_info_t* p_device_info, ni_codec_t fmt, ni_device_type_t type,
  ni_hw_capability_t* p_hw_cap)
{
    int i;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_device_info)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() p_device_info is null\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    ni_log(NI_LOG_INFO, "%s type %d fmt %d\n", __func__, type, fmt);

    for (i = 0; i < EN_CODEC_MAX; i++)
    {
        p_device_info->dev_cap[i].supports_codec = EN_INVALID;
    }

    if (NI_DEVICE_TYPE_DECODER == type)
    {
        p_device_info->dev_cap[0].supports_codec = EN_H264;
        p_device_info->dev_cap[0].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[0].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[0].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[0].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[0].profiles_supported,
                "Baseline, Main, High, High10", NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[0].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[1].supports_codec = EN_H265;
        p_device_info->dev_cap[1].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[1].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[1].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[1].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[1].profiles_supported, "Main, Main10",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[1].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[2].supports_codec = EN_JPEG;
        p_device_info->dev_cap[2].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[2].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[2].min_res_width = NI_MIN_RESOLUTION_WIDTH_JPEG;
        p_device_info->dev_cap[2].min_res_height =
            NI_MIN_RESOLUTION_HEIGHT_JPEG;

        strncpy(p_device_info->dev_cap[2].profiles_supported, "Baseline",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[2].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[3].supports_codec = EN_VP9;
        p_device_info->dev_cap[3].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[3].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[3].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[3].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[3].profiles_supported, "0, 2",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[3].level, "6.2", NI_LEVELS_SUPP_STR_LEN);
    } else if (NI_DEVICE_TYPE_ENCODER == type)
    { /*! encoder */
        p_device_info->dev_cap[0].supports_codec = EN_H264;
        p_device_info->dev_cap[0].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[0].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[0].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[0].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[0].profiles_supported,
                "Baseline, Main, High, High10", NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[0].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[1].supports_codec = EN_H265;
        p_device_info->dev_cap[1].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[1].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[1].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[1].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[1].profiles_supported, "Main, Main10",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[1].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[2].supports_codec = EN_JPEG;
        p_device_info->dev_cap[2].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[2].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[2].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[2].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[2].profiles_supported, "Main",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[2].level, "5.1", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[3].supports_codec = EN_AV1;
        p_device_info->dev_cap[3].max_res_width = NI_PARAM_AV1_MAX_WIDTH;
        p_device_info->dev_cap[3].max_res_height = NI_PARAM_AV1_MAX_HEIGHT;
        p_device_info->dev_cap[3].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[3].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[3].profiles_supported, "Main",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[3].level, "5.1", NI_LEVELS_SUPP_STR_LEN);
    } else if (NI_DEVICE_TYPE_SCALER == type || NI_DEVICE_TYPE_AI == type)
    {
        p_device_info->dev_cap[0].supports_codec =
            p_device_info->dev_cap[1].supports_codec =
                p_device_info->dev_cap[2].supports_codec =
                    p_device_info->dev_cap[3].supports_codec = EN_INVALID;
    }
END:

    return retval;
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_strcmp(const void * p_str, const void* p_str1)
{
    const char *l = (const char *)p_str;
    const char *r = (const char *)p_str1;
    int vl, vr;

    while (!isdigit(*l) && (*l) != '\0')
    {
        l++;
    }
  while (!isdigit(*r) && (*r) != '\0')
  {
    r++;
  }
  vl = atoi(l);
  vr = atoi(r);
  if (vl == vr)
  {
    return 0;
  }
  else if (vl < vr)
  {
    return -1;
  }
  else
  {
    return 1;
  }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_lock_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
    char type = device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    if (NULL != p_name)
    {
        snprintf(p_name, max_name_len, "%s/NI_lck_%c%d", LOCK_DIR, type, guid);
    }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_shm_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
    char type = device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    /*! assume there is enough space allocated in name */
    if (NULL != p_name)
    {
        snprintf(p_name, max_name_len, "NI_shm_%c%d", type, guid);
    }
}

/*!*****************************************************************************
 *  \brief Check if a FW_rev retrieved from card is supported by this version of
 *         libxcoder.
 *
 *  \param[in] fw_rev FW revision queried from card firmware
 *
 *  \return If FW is fully compatible return 1
 *          If FW not compatible return 0
 *          If FW is partially compatible return 2
 ******************************************************************************/
int ni_is_fw_compatible(uint8_t fw_rev[8])
{
    if ((uint8_t)fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] ==
        (uint8_t)NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX])
    {
        if ((uint8_t)fw_rev[NI_XCODER_REVISION_API_MINOR_VER_IDX] ==
            (uint8_t)NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX])
        {
            return 1;
        } else
        {
            return 2;
        }
    } else
  {
      return 0;
  }
}

#ifdef _WIN32

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_enumerate_devices(
    char   ni_devices[][NI_MAX_DEVICE_NAME_LEN],
    int    max_handles
)
{
    return ni_nvme_enumerate_devices(ni_devices, max_handles);
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_one_device_info(ni_device_info_t* p_device_info)
{
  char shm_name[32] = { 0 };
    char lck_name[32] = { 0 };
  DWORD  rc = 0;
  ni_device_info_t * p_coder_info_map = NULL;
  HANDLE map_file_handle = NULL;
  ni_lock_handle_t mutex_handle = NULL;
  SECURITY_DESCRIPTOR security_descriptor = { 0 };

  if(!p_device_info)
  {
    return;
  }

  ni_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));
  ni_log(NI_LOG_INFO, "Creating shm_name: %s\n", shm_name);

  InitializeSecurityDescriptor(&security_descriptor, SECURITY_DESCRIPTOR_REVISION);
  //security_descriptor.Control

  map_file_handle = CreateFileMapping(
                      INVALID_HANDLE_VALUE,    // use paging file
                      NULL,                    // default security
                      PAGE_READWRITE,          // read/write access
                      0,                       // maximum object size (high-order DWORD)
                      sizeof(ni_device_info_t),// maximum object size (low-order DWORD)
                      (LPCSTR)shm_name         // name of mapping object
                    );

  if (NULL == map_file_handle)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_ERROR, "ERROR: CreateFileMapping returned (%d) for %s\n",
           rc, shm_name);
    LRETURN;
  }
  else
  {
    rc = NI_ERRNO;
    if (ERROR_ALREADY_EXISTS == rc)
    {
        ni_log(NI_LOG_ERROR, "CreateFileMapping returned existing handle for"
              " %s\n", shm_name);
    }
    else
    {
      ni_log(NI_LOG_INFO, "CreateFileMapping created a new mapFile for %s, handle: %p  ..\n", shm_name, map_file_handle);
    }
  }

  p_coder_info_map = (ni_device_info_t *) MapViewOfFile(
                              map_file_handle,   // handle to map object
                              FILE_MAP_ALL_ACCESS, // read/write permission
                              0,
                              0,
                              sizeof(ni_device_info_t)
                            );

  if (NULL == p_coder_info_map)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_INFO, "Could not map view of file, p_last error (%d).\n", rc);
    LRETURN;
  }

  memcpy(p_coder_info_map, p_device_info, sizeof(ni_device_info_t));

END:

    if (p_coder_info_map)
    {
        UnmapViewOfFile(p_coder_info_map);
    }
}





/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t* p_device_context, ni_session_context_t* p_session_context)
{
    uint32_t i = 0;

    if ((!p_device_context) || (!p_session_context))
    {
        return;
    }

  p_device_context->p_device_info->load = p_session_context->load_query.current_load;
  p_device_context->p_device_info->active_num_inst = p_session_context->load_query.total_contexts;
  // Now we get the model load from the FW
  p_device_context->p_device_info->model_load = p_session_context->load_query.fw_model_load;
  if ( 0 == p_device_context->p_device_info->active_num_inst )
  {
    p_device_context->p_device_info->load = 0;
  }

  for (i = 0; i < p_device_context->p_device_info->active_num_inst; i++)
  {
    p_device_context->p_device_info->sw_instance[i].id =
      p_session_context->load_query.context_status[i].context_id;
    p_device_context->p_device_info->sw_instance[i].status = (ni_sw_instance_status_t)
      p_session_context->load_query.context_status[i].context_status;
    p_device_context->p_device_info->sw_instance[i].codec = (ni_codec_t)
      p_session_context->load_query.context_status[i].codec_format;
    p_device_context->p_device_info->sw_instance[i].width =
      p_session_context->load_query.context_status[i].video_width;
    p_device_context->p_device_info->sw_instance[i].height =
      p_session_context->load_query.context_status[i].video_height;
    p_device_context->p_device_info->sw_instance[i].fps =
      p_session_context->load_query.context_status[i].fps;
  }
}
#elif __linux__ || __APPLE__

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_one_device_info (ni_device_info_t * p_device_info)
{
  int32_t shm_fd = -1;
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  int32_t lock = -1;
  ni_retcode_t error = NI_RETCODE_SUCCESS;
  ni_device_info_t * p_coder_info_dst = NULL;

  if( !p_device_info )
  {
    return;
  }

  ni_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));

  ni_log(NI_LOG_INFO, "Creating shm_name: %s , lck_name %s\n", shm_name, lck_name);

#ifdef _ANDROID
  int ret = ni_rsrc_android_init();

  if (service == NULL)
  {
      ni_log(NI_LOG_ERROR, "ni_rsrc_get_one_device_info Error service ..");
      return;
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
    ni_log(NI_LOG_ERROR, "ERROR %s() shm_open() %s: %s\n",
           __func__, shm_name, strerror(NI_ERRNO));
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }

#ifndef _ANDROID
  /*! configure the size to ni_device_info_t */
  if (ftruncate(shm_fd, sizeof(ni_device_info_t)) < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s() ftruncate() shm_fd: %s\n", __func__,
           strerror(NI_ERRNO));
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }
#endif

  /*! map the shared memory segment */
  p_coder_info_dst = (ni_device_info_t *)mmap(0, sizeof(ni_device_info_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_coder_info_dst)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() mmap() p_coder_info_dst: %s\n", __func__,
             strerror(NI_ERRNO));
      error = NI_RETCODE_FAILURE;
      LRETURN;
  }

  memcpy(p_coder_info_dst, p_device_info, sizeof(ni_device_info_t));

  if (msync((void*)p_coder_info_dst, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_coder_info_dst: %s\n",
             __func__, strerror(NI_ERRNO));
  }
  else
  {
    ni_log(NI_LOG_INFO, "ni_rsrc_get_one_device_info written out.\n");
  }

  /*! create the lock */
  lock = open(lck_name, O_RDWR | O_CREAT | O_CLOEXEC,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (lock < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR: cannot open lock file %s\n", lck_name);
      error = NI_RETCODE_FAILURE;
      LRETURN;
  }

END:

#ifndef _ANDROID
  if (shm_fd > 0)
  {
    close(shm_fd);
  }
#endif

  if ((NI_RETCODE_SUCCESS != error) && (lock > 0))
  {
      close(lock);
  }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t *p_device_context, ni_session_context_t *p_session_context)
{
    uint32_t j;

    if ((!p_device_context) || (!p_session_context))
    {
        return;
    }

  p_device_context->p_device_info->load = p_session_context->load_query.current_load;
  p_device_context->p_device_info->active_num_inst = p_session_context->load_query.total_contexts;
  // Now we get the model load from the FW
  p_device_context->p_device_info->model_load = p_session_context->load_query.fw_model_load;
  if (0 == p_device_context->p_device_info->active_num_inst)
  {
    p_device_context->p_device_info->load = 0;
  }
  for (j = 0; j < p_device_context->p_device_info->active_num_inst; j++)
  {
    p_device_context->p_device_info->sw_instance[j].id =
        p_session_context->load_query.context_status[j].context_id;
    p_device_context->p_device_info->sw_instance[j].status = (ni_sw_instance_status_t)
                                             p_session_context->load_query.context_status[j]
                                                 .context_status;
    p_device_context->p_device_info->sw_instance[j].codec = (ni_codec_t)
                                            p_session_context->load_query.context_status[j]
                                                .codec_format;
    p_device_context->p_device_info->sw_instance[j].width =
        p_session_context->load_query.context_status[j].video_width;
    p_device_context->p_device_info->sw_instance[j].height =
        p_session_context->load_query.context_status[j].video_height;
    p_device_context->p_device_info->sw_instance[j].fps =
        p_session_context->load_query.context_status[j].fps;
  }
  if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_context->"
             "p_device_info: %s\n", __func__, strerror(NI_ERRNO));
  }
}
#endif
