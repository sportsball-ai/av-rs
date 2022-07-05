/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_rsrc_priv.cpp
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

#ifdef __linux__
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
#include "ni_util.h"

uint32_t stop_process = 0;

/*!******************************************************************************
 *  \brief Update codec record info with retrieved device info from HW
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_rsrc_fill_device_info(ni_device_info_t* p_device_info, ni_codec_t fmt, ni_device_type_t type,
  ni_hw_capability_t* p_hw_cap)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  if(!p_device_info)
  {
    ni_log(NI_LOG_INFO, "Error Null pointer parameter passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_DEVICE_TYPE_DECODER == type)
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
                NI_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h264_cap.level[0] = '\0';
        strncat(p_device_info->h264_cap.level, "Level 6.2", NI_MAX_LEVEL_NAME_LEN);
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
                "Main, Main10", NI_MAX_PROFILE_NAME_LEN);
      }
      if (0 == p_hw_cap->video_level)
      {
        p_device_info->h265_cap.level[0] = '\0';
        strncat(p_device_info->h265_cap.level, "Level 6.2 Main-Tier", NI_MAX_LEVEL_NAME_LEN);
      }
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error Decoder Codec format %d is not supported\n", fmt);
      retval = NI_RETCODE_FAILURE;
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
                "Baseline, Extended, Main, High, High10", NI_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h264_cap.level[0] = '\0';
        strncat(p_device_info->h264_cap.level, "Level 6.2", NI_MAX_LEVEL_NAME_LEN);
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
                "Main, Main10", NI_MAX_PROFILE_NAME_LEN);
      }
      if (p_hw_cap->video_level == 0)
      {
        p_device_info->h265_cap.level[0] = '\0';
        strncat(p_device_info->h265_cap.level, "Level 6.2 Main-Tier", NI_MAX_LEVEL_NAME_LEN);
      }
    }
    else
    {
      ni_log(NI_LOG_INFO, "Error Encoder Codec format %d is not supported\n", fmt);
      retval =  NI_RETCODE_FAILURE;
    }
  }

  END;

  return retval;
}

/*!******************************************************************************
 *  \brief String comparison function
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_strcmp(const void * p_str, const void* p_str1)
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
void ni_rsrc_get_lock_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
  char type = (NI_DEVICE_TYPE_DECODER == device_type ? 'd' : 'e');
  if (NULL != p_name)
  {
    snprintf(p_name, max_name_len, "%s/NI_lck_%c%d", LOCK_DIR, type, guid);
  }
}

/*!******************************************************************************
 *  \brief Returns the name of shared memory of the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_shm_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
  char type = (device_type == NI_DEVICE_TYPE_DECODER ? 'd' : 'e');
  /*! assume there is enough space allocated in name */
  if (NULL != p_name)
  {
    snprintf(p_name, max_name_len, "NI_shm_%c%d", type, guid);
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
int ni_is_fw_compatible(uint8_t fw_rev[8])
{
  char str_fw_ver[4], str_fw_API_fla[3], str_fw_API_ver[3];
  int fw_API_ver;
  char *strtok_token;
  char str_fw_API_flav_support[] = NI_XCODER_FW_API_FLAVORS_SUPPORTED;
  const char str_API_flavor_list_delim[2] = ",";
  int api_flavor_matched = 0;

  // extract fw version
  memcpy(str_fw_ver, fw_rev, NI_XCODER_VER_SZ);
  str_fw_ver[NI_XCODER_VER_SZ] = '\0';

  // extract fw API flavor
  memcpy(str_fw_API_fla, &(fw_rev[NI_XCODER_VER_SZ + 1]), NI_XCODER_API_FLAVOR_SZ);
  str_fw_API_fla[NI_XCODER_API_FLAVOR_SZ] = '\0';

  // extract fw API version
  memcpy(str_fw_API_ver, &(fw_rev[NI_XCODER_VER_SZ + 1 + NI_XCODER_API_FLAVOR_SZ]),
         NI_XCODER_API_VER_SZ);
  str_fw_API_ver[NI_XCODER_API_VER_SZ] = '\0';

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
         fw_API_ver >= NI_XCODER_FW_API_VER_SUPPORTED_MIN))
  {
    /* ni_log(NI_LOG_INFO, "API-flavor: %s, API-version: %s, is not supported by "
           "this libxcoder version: %s\n", str_fw_API_fla,
           str_fw_API_ver, NI_XCODER_REVISION); */
    return 0; // FWrev is not compatible
  }
  else if (strncmp(str_fw_ver, NI_XCODER_FW_VER_SUPPORTED_MIN, NI_XCODER_VER_SZ)
           < 0)
  {
    /* ni_log(NI_LOG_INFO, "WARNING - FW version: %s is below the minimum support version "
           "(%s) of this libxcoder: %s\n", str_fw_ver,
           NI_XCODER_FW_VER_SUPPORTED_MIN, NI_XCODER_REVISION); */
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
int ni_rsrc_enumerate_devices(
    char   ni_devices[][MAX_DEVICE_NAME_LEN],
    int    max_handles
)
{
  return ni_nvme_enumerate_devices(ni_devices, max_handles);
}

/*!******************************************************************************
 *  \brief Retrieve codec record info
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
  ni_log(NI_LOG_INFO, "Creating shm_name: %s \n", shm_name);

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
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_INFO, "ERROR %d: ni_rsrc_get_one_device_info() CreateFileMapping failed for %s\n", rc, shm_name);
    LRETURN;
  }
  else
  {
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    if (ERROR_ALREADY_EXISTS == rc)
    {
      ni_log(NI_LOG_INFO, "CreateFileMapping returned existing handle for %s  ..\n", shm_name);
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
    rc = NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR;
    ni_log(NI_LOG_INFO, "ERROR %d: ni_rsrc_get_one_device_info() Could not map view of file\n", rc);
    LRETURN;
  }

  memcpy(p_coder_info_map, p_device_info, sizeof(ni_device_info_t));

  END;

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
void ni_rsrc_update_record(ni_device_context_t* p_device_context, ni_session_context_t* p_session_context)
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
#elif __linux__

/*!******************************************************************************
 *  \brief Retrieve codec record info
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

  String8 param;
  param.setTo(shm_name, STR_BUFFER_LEN);
  shm_fd = service->getFd(param);
  if (service == NULL)
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_get_one_device_info Error service ..");
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
  shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0777);
#endif
  
  
  if (shm_fd < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: shm_open ..", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }

#ifndef _ANDROID
  /*! configure the size to ni_device_info_t */
  if (ftruncate(shm_fd, sizeof(ni_device_info_t)) < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: ftruncate ..", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }
#endif

  /*! map the shared memory segment */
  p_coder_info_dst = (ni_device_info_t *)mmap(0, sizeof(ni_device_info_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == p_coder_info_dst)
  {
    ni_log(NI_LOG_ERROR, "Error %d: mmap ...", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }

  memcpy(p_coder_info_dst, p_device_info, sizeof(ni_device_info_t));

  if (msync((void*)p_coder_info_dst, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
    ni_log(NI_LOG_ERROR, "ni_rsrc_get_one_device_info msync");
  }
  else
  {
    ni_log(NI_LOG_INFO, "ni_rsrc_get_one_device_info written out.\n");
  }

  /*! create the lock */
  lock = open(lck_name, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU | S_IRWXG | S_IRWXO );
  if (lock < 0)
  {
    ni_log(NI_LOG_ERROR, "Error %d: open lock file ... %s", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, lck_name) ;
    error = NI_RETCODE_FAILURE;
    LRETURN;
  }

  END;

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
 *  \brief Updates the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t *p_device_context, ni_session_context_t *p_session_context)
{
  int j;

  if( (!p_device_context) || (!p_session_context) )
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
    ni_log(NI_LOG_ERROR, "ni_rsrc_update_record msync");
  }
}
#endif
