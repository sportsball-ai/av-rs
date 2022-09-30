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
*  \file   ni_rsrc_priv_logan.cpp
*
*  \brief  Private routines related to resource management of NI T-408 devices
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
#include "ni_rsrc_api_android_logan.h"
#endif


#include "ni_device_api_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_rsrc_priv_logan.h"
#include "ni_nvme_logan.h"
#include "ni_util_logan.h"

uint32_t g_logan_xcoder_stop_process = 0;

/*!******************************************************************************
 *  \brief Update codec record info with retrieved device info from HW
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_logan_retcode_t ni_logan_rsrc_fill_device_info(ni_logan_device_info_t* p_device_info,
                                      ni_codec_t fmt,
                                      ni_logan_device_type_t type,
                                      ni_logan_hw_capability_t* p_hw_cap)
{
  ni_logan_retcode_t retval = NI_LOGAN_RETCODE_SUCCESS;

  if(!p_device_info)
  {
    ni_log(NI_LOG_INFO, "Error Null pointer parameter passed\n");
    retval = NI_LOGAN_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_LOGAN_DEVICE_TYPE_DECODER == type)
  {
    if (EN_H264 == fmt)
    {
      p_device_info->supports_h264 = 1;
      p_device_info->h264_cap.max_res_width = p_hw_cap->max_video_width;
      p_device_info->h264_cap.max_res_height = p_hw_cap->max_video_height;
      p_device_info->h264_cap.min_res_width = p_hw_cap->min_video_width;
      p_device_info->h264_cap.min_res_height = p_hw_cap->min_video_height;

      if (p_hw_cap->video_profile == 0)
      {
        p_device_info->h264_cap.profiles_supported[0] = '\0';
        strncat(p_device_info->h264_cap.profiles_supported,
                "Baseline, Constrained Baseline, Main, High, High10",
                NI_LOGAN_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h264_cap.level[0] = '\0';
        strncat(p_device_info->h264_cap.level, "Level 6.2", NI_LOGAN_MAX_LEVEL_NAME_LEN);
      }
    }
    else if (EN_H265 == fmt)
    {
      p_device_info->supports_h265 = 1;

      p_device_info->h265_cap.max_res_width = p_hw_cap->max_video_width;
      p_device_info->h265_cap.max_res_height = p_hw_cap->max_video_height;
      p_device_info->h265_cap.min_res_width = p_hw_cap->min_video_width;
      p_device_info->h265_cap.min_res_height = p_hw_cap->min_video_height;

      if (0 == p_hw_cap->video_profile)
      {
        p_device_info->h265_cap.profiles_supported[0] = '\0';
        strncat(p_device_info->h265_cap.profiles_supported,
                "Main, Main10", NI_LOGAN_MAX_PROFILE_NAME_LEN);
      }
      if (0 == p_hw_cap->video_level)
      {
        p_device_info->h265_cap.level[0] = '\0';
        strncat(p_device_info->h265_cap.level, "Level 6.2 Main-Tier", NI_LOGAN_MAX_LEVEL_NAME_LEN);
      }
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error Decoder Codec format %d is not supported\n", fmt);
      retval = NI_LOGAN_RETCODE_FAILURE;
      LRETURN;
    }
  }
  else
  { /*! encoder */

    if (EN_H264 == fmt)
    {
      p_device_info->supports_h264 = 1;
      p_device_info->h264_cap.max_res_width = p_hw_cap->max_video_width;
      p_device_info->h264_cap.max_res_height = p_hw_cap->max_video_height;
      p_device_info->h264_cap.min_res_width = p_hw_cap->min_video_width;
      p_device_info->h264_cap.min_res_height = p_hw_cap->min_video_height;

      if (p_hw_cap->video_profile == 0)
      {
        p_device_info->h264_cap.profiles_supported[0] = '\0';
        strncat(p_device_info->h264_cap.profiles_supported,
                "Baseline, Extended, Main, High, High10", NI_LOGAN_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h264_cap.level[0] = '\0';
        strncat(p_device_info->h264_cap.level, "Level 6.2", NI_LOGAN_MAX_LEVEL_NAME_LEN);
      }
    }
    else if (EN_H265 == fmt)
    {
      p_device_info->supports_h265 = 1;

      p_device_info->h265_cap.max_res_width = p_hw_cap->max_video_width;
      p_device_info->h265_cap.max_res_height = p_hw_cap->max_video_height;
      p_device_info->h265_cap.min_res_width = p_hw_cap->min_video_width;
      p_device_info->h265_cap.min_res_height = p_hw_cap->min_video_height;

      if (p_hw_cap->video_profile == 0)
      {
        p_device_info->h265_cap.profiles_supported[0] = '\0';
        strncat(p_device_info->h265_cap.profiles_supported,
                "Main, Main10", NI_LOGAN_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h265_cap.level[0] = '\0';
        strncat(p_device_info->h265_cap.level, "Level 6.2 Main-Tier", NI_LOGAN_MAX_LEVEL_NAME_LEN);
      }
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error Encoder Codec format %d is not supported\n", fmt);
      retval =  NI_LOGAN_RETCODE_FAILURE;
    }
  }

  END:

  return retval;
}

/*!******************************************************************************
 *  \brief String comparison function
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_rsrc_strcmp(const void * p_str, const void* p_str1)
{
  const char* l = (const char *)p_str;
  const char* r = (const char *)p_str1;
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
 *  \brief Returns the device lock name
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_get_lock_name(ni_logan_device_type_t device_type,
                           int32_t guid,
                           char* p_name,
                           size_t max_name_len)
{
  char type = (NI_LOGAN_DEVICE_TYPE_DECODER == device_type ? 'd' : 'e');
  if (NULL != p_name)
  {
    snprintf(p_name, max_name_len, "%s/NI_LOGAN_lck_%c%d", LOCK_DIR, type, guid);
  }
}

/*!******************************************************************************
 *  \brief Returns the name of shared memory of the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_get_shm_name(ni_logan_device_type_t device_type,
                          int32_t guid,
                          char* p_name,
                          size_t max_name_len)
{
  char type = (device_type == NI_LOGAN_DEVICE_TYPE_DECODER ? 'd' : 'e');
  /*! assume there is enough space allocated in name */
  if (NULL != p_name)
  {
    snprintf(p_name, max_name_len, "NI_LOGAN_shm_%c%d", type, guid);
  }
}

/*!*****************************************************************************
 *  \brief Check if a FW_rev retrieved from card is supported by libxcoder.
 *         Support is checked for API flavor, API version, and SW/FW release version.
 *
 *  \param[in] fw_rev FW revision queried from card firmware
 *
 *  \return If FW is fully compatible return 1
 *          If FW not compatible return 0
 *          If FW is partially compatible return 2
 ******************************************************************************/
int ni_logan_is_fw_compatible(uint8_t fw_rev[8])
{
  char str_fw_ver[4], str_fw_API_fla[3], str_fw_API_ver[3];
  int fw_API_ver;
  char *strtok_token;
  char str_fw_API_flav_support[] = NI_LOGAN_XCODER_FW_API_FLAVORS_SUPPORTED;
  const char str_API_flavor_list_delim[2] = ",";
  int api_flavor_matched = 0;

  // extract fw version
  memcpy(str_fw_ver, fw_rev, NI_LOGAN_XCODER_VER_SZ);
  str_fw_ver[NI_LOGAN_XCODER_VER_SZ] = '\0';

  // extract fw API flavor
  memcpy(str_fw_API_fla, &(fw_rev[NI_LOGAN_XCODER_VER_SZ + 1]), NI_LOGAN_XCODER_API_FLAVOR_SZ);
  str_fw_API_fla[NI_LOGAN_XCODER_API_FLAVOR_SZ] = '\0';

  // extract fw API version
  memcpy(str_fw_API_ver, &(fw_rev[NI_LOGAN_XCODER_VER_SZ + 1 + NI_LOGAN_XCODER_API_FLAVOR_SZ]),
         NI_LOGAN_XCODER_API_VER_SZ);
  str_fw_API_ver[NI_LOGAN_XCODER_API_VER_SZ] = '\0';

  fw_API_ver = atoi(str_fw_API_ver);

  // check multiple API flavor support
  strtok_token = strtok(str_fw_API_flav_support, str_API_flavor_list_delim);
  while(strtok_token != NULL)
  {
    if (0 == strcmp(str_fw_API_fla, strtok_token))
    {
      api_flavor_matched = 1;
      break;
    }
    strtok_token = strtok(NULL, str_API_flavor_list_delim);
  }

  if (! (api_flavor_matched &&
         fw_API_ver >= NI_LOGAN_XCODER_FW_API_VER_SUPPORTED_MIN))
  {
    /* ni_log(NI_LOG_INFO, "API-flavor: %s, API-version: %s, is not supported by "
           "this libxcoder version: %s\n", str_fw_API_fla,
           str_fw_API_ver, NI_LOGAN_XCODER_REVISION); */
    return 0; // FWrev is not compatible
  }
  else if (strncmp(str_fw_ver, NI_LOGAN_XCODER_FW_VER_SUPPORTED_MIN, NI_LOGAN_XCODER_VER_SZ)
           < 0)
  {
    /* ni_log(NI_LOG_INFO, "WARNING - FW version: %s is below the minimum support version "
           "(%s) of this libxcoder: %s\n", str_fw_ver,
           NI_LOGAN_XCODER_FW_VER_SUPPORTED_MIN, NI_LOGAN_XCODER_REVISION); */
    return 2; // FWrev compatability supports core features but may not support some extra features
  }
  return 1; // FWrev is fully compatible
}

#ifdef _WIN32

/*!******************************************************************************
 *  \brief List NETINT nvme devices
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_rsrc_enumerate_devices(char ni_logan_devices[][NI_LOGAN_MAX_DEVICE_NAME_LEN],
                              int max_handles)
{
  return ni_logan_nvme_enumerate_devices(ni_logan_devices, max_handles);
}

/*!******************************************************************************
 *  \brief Retrieve codec record info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_get_one_device_info(ni_logan_device_info_t* p_device_info)
{
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  DWORD  rc = 0;
  ni_logan_device_info_t * p_coder_info_map = NULL;
  HANDLE map_file_handle = NULL;
  ni_lock_handle_t mutex_handle = NULL;
  SECURITY_DESCRIPTOR security_descriptor = { 0 };

  if(!p_device_info)
  {
    return;
  }

  ni_logan_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_logan_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));
  ni_log(NI_LOG_INFO, "Creating shm_name: %s \n", shm_name);

  InitializeSecurityDescriptor(&security_descriptor, SECURITY_DESCRIPTOR_REVISION);
  //security_descriptor.Control

  map_file_handle = CreateFileMapping(
                      INVALID_HANDLE_VALUE,    // use paging file
                      NULL,                    // default security
                      PAGE_READWRITE,          // read/write access
                      0,                       // maximum object size (high-order DWORD)
                      sizeof(ni_logan_device_info_t),// maximum object size (low-order DWORD)
                      (LPCSTR)shm_name         // name of mapping object
                    );

  if (NULL == map_file_handle)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_INFO, "ERROR %d: ni_logan_rsrc_get_one_device_info() CreateFileMapping failed for %s\n",
           rc, shm_name);
    LRETURN;
  }
  else
  {
    rc = NI_ERRNO;
    if (ERROR_ALREADY_EXISTS == rc)
    {
      ni_log(NI_LOG_INFO, "CreateFileMapping returned existing handle for %s  ..\n", shm_name);
    }
    else
    {
      ni_log(NI_LOG_INFO, "CreateFileMapping created a new mapFile for %s, handle: %p  ..\n",
             shm_name, map_file_handle);
    }
  }

  p_coder_info_map = (ni_logan_device_info_t *) MapViewOfFile(
                              map_file_handle,   // handle to map object
                              FILE_MAP_ALL_ACCESS, // read/write permission
                              0,
                              0,
                              sizeof(ni_logan_device_info_t)
                            );

  if (NULL == p_coder_info_map)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_INFO, "ERROR %d: ni_logan_rsrc_get_one_device_info() Could not map view of file\n", rc);
    LRETURN;
  }

  memcpy(p_coder_info_map, p_device_info, sizeof(ni_logan_device_info_t));

  END:

  if (p_coder_info_map)
  {
    UnmapViewOfFile(p_coder_info_map);
  }

}





/*!******************************************************************************
 *  \brief Updates the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_update_record(ni_logan_device_context_t* p_device_context,
                           ni_logan_session_context_t* p_session_context)
{
  int i = 0;

  if( (!p_device_context) || (!p_session_context) )
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
 *  \brief Retrieve codec record info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_get_one_device_info (ni_logan_device_info_t * p_device_info)
{
  int32_t shm_fd = -1;
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  int32_t lock = -1;
  ni_logan_retcode_t error = NI_LOGAN_RETCODE_SUCCESS;
  ni_logan_device_info_t * p_coder_info_dst = NULL;

  if( !p_device_info )
  {
    return;
  }

  ni_logan_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_logan_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));

  ni_log(NI_LOG_INFO, "Creating shm_name: %s , lck_name %s\n", shm_name, lck_name);

#ifdef _ANDROID
  int ret = ni_logan_rsrc_android_init();

  if (service_logan == NULL)
  {
	  ni_log(NI_LOG_ERROR, "ni_logan_rsrc_get_one_device_info Error service ..");
      return;
  }
  string param = shm_name;
  Return<void> retvalue = service_logan->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
			ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
			if(ret > 0){
				shm_fd = dup(handle->data[0]);
				ni_log(NI_LOG_INFO, "vendor:GetAppFlag shm_fd:%d\n", shm_fd);
			}else{
				ni_log(NI_LOG_ERROR, "Error %d: shm_get shm_fd ..\n", NI_ERRNO);
			}
  });
  if(!retvalue.isOk()) {
		ni_log(NI_LOG_ERROR, "service_logan->GetAppFlag ret failed ..\n");
		LRETURN;
  }
  if (shm_fd <= 0)
  {
		shm_fd = ashmem_create_region(shm_name, sizeof(ni_logan_device_info_t));
		if (shm_fd >= 0)
		{
		  native_handle_t* handle = native_handle_create(1, 0);
		  handle->data[0] = shm_fd;
		  service_logan->SetAppFlag(param, handle);
		  ni_log(NI_LOG_ERROR, "Create shm fd %d\n", shm_fd);
		}
  }

  /*String8 param;
  param.setTo(shm_name, STR_BUFFER_LEN);
  shm_fd = service->getFd(param);
  if (service == NULL)
  {
    perror("ni_logan_rsrc_get_one_device_info Error service ..");
    LRETURN;
  }
  shm_fd = service->getFd(param);
  if (shm_fd < 0)
  {
    shm_fd = ashmem_create_region(shm_name, sizeof(ni_logan_device_info_t));
    if (shm_fd >= 0)
    {
      service->setFd(param, shm_fd);
    }
  }*/

#else
  shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif

  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: shm_open ..", NI_ERRNO);
    error = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }

#ifndef _ANDROID
  /*! configure the size to ni_logan_device_info_t */
  if (ftruncate(shm_fd, sizeof(ni_logan_device_info_t)) < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: ftruncate ..", NI_ERRNO);
    error = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }
#endif

  /*! map the shared memory segment */
  p_coder_info_dst = (ni_logan_device_info_t *)mmap(0, sizeof(ni_logan_device_info_t), PROT_READ | PROT_WRITE,
                                              MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_coder_info_dst)
  {
    ni_log(NI_LOG_ERROR, "Error %d: mmap ...", NI_ERRNO);
    error = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }

  memcpy(p_coder_info_dst, p_device_info, sizeof(ni_logan_device_info_t));

  if (msync((void*)p_coder_info_dst, sizeof(ni_logan_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
    ni_log(NI_LOG_ERROR, "%s msync\n", __FUNCTION__);
  }
  else
  {
    ni_log(NI_LOG_INFO, "%s written out.\n", __FUNCTION__);
  }

  /*! create the lock */
  lock = open(lck_name, O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP );
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ... %s",
           NI_ERRNO, lck_name) ;
    error = NI_LOGAN_RETCODE_FAILURE;
    LRETURN;
  }

  END:

#ifndef _ANDROID
  if (shm_fd > 0)
  {
    close(shm_fd);
  }
#endif

  if ((NI_LOGAN_RETCODE_SUCCESS != error) && (lock > 0))
  {
    close(lock);
  }

}

/*!******************************************************************************
 *  \brief Updates the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_update_record(ni_logan_device_context_t *p_device_context,
                           ni_logan_session_context_t *p_session_context)
{
  int j;

  if (!p_device_context || !p_session_context)
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
  if (msync((void *)p_device_context->p_device_info, sizeof(ni_logan_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
    ni_log(NI_LOG_ERROR, "%s msync\n", __FUNCTION__);
  }
}
#endif
