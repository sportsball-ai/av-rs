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
 *  \file   ni_rsrc_priv.cpp
 *
 *  \brief  Private definitions used by ni_rsrc_api.cpp for management of
 *          NETINT video processing devices
 ******************************************************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <setjmp.h>

#if __linux__ || __APPLE__
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif

#if __APPLE__
#include <sys/syslimits.h>
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

#if __linux__ || __APPLE__
#ifndef _ANDROID
jmp_buf env;
#endif
#endif

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
        p_device_info->dev_cap[2].min_res_width = NI_PARAM_MIN_WIDTH;
        p_device_info->dev_cap[2].min_res_height = NI_PARAM_MIN_HEIGHT;

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
    char type = g_device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    if (NULL != p_name)
    {
        if (strcmp(CODERS_SHM_NAME, "NI_QUADRA_SHM_CODERS") == 0)
        {
          snprintf(p_name, max_name_len, "%s/NI_QUADRA_lck_%c%d", LOCK_DIR, type, guid);
        }
        else
        {
          snprintf(p_name, max_name_len, "%s/NI_lck_%c%d", LOCK_DIR, type, guid);
        }
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
    char type = g_device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    /*! assume there is enough space allocated in name */
    if (NULL != p_name)
    {
        if (strcmp(CODERS_SHM_NAME, "NI_QUADRA_SHM_CODERS") == 0)
        {
          snprintf(p_name, max_name_len, "NI_QUADRA_shm_%c%d", type, guid);
        }
        else
        {
          snprintf(p_name, max_name_len, "NI_shm_%c%d", type, guid);
        }
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
        if (ni_cmp_fw_api_ver((char*) &fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              &NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX]))
        {
            return 2;
        } else
        {
            return 1;
        }
    } else
    {
        return 0;
    }
}

static bool check_device_capability(const ni_device_handle_t device_handle,
                                    ni_device_capability_t *device_capability,
                                    const char *device_name,
                                    const int should_match_rev,
                                    int *compatibility)
{
    ni_retcode_t retcode;

    memset(device_capability, 0, sizeof(ni_device_capability_t));
    retcode = ni_device_capability_query(device_handle, device_capability);
    if (retcode != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR,
               "Capability query failed for %s! Skipping...\n",
               device_name);
        return false;
    }
    if (!is_supported_xcoder(device_capability->device_is_xcoder))
    {
        ni_log(NI_LOG_INFO,
               "%s not supported! Skipping...\n",
               device_name);
        return false;
    }
    *compatibility = ni_is_fw_compatible(device_capability->fw_rev);
    if (should_match_rev && !(*compatibility))
    {
        ni_log(NI_LOG_INFO,
               "%s with FW revision %.*s not compatible! Skipping...\n",
               device_name,
               (int)sizeof(device_capability->fw_rev),
               device_capability->fw_rev);
        return false;
    }

    return true;
}

static void fill_device_info(ni_device_info_t *device_info,
                             const int module_id,
                             const ni_device_handle_t device_handle,
                             const char device_name[NI_MAX_DEVICE_NAME_LEN],
                             const int compatibility,
                             ni_device_capability_t *device_capability,
                             const ni_device_type_t device_type)
{
    ni_hw_capability_t *hw_capability;

    hw_capability = &device_capability->xcoder_devices[device_type];

    strncpy(device_info->dev_name, device_name, (MAX_CHAR_IN_DEVICE_NAME-1));
    strncpy(device_info->blk_name, device_name, (MAX_CHAR_IN_DEVICE_NAME-1));
    device_info->hw_id = hw_capability->hw_id;
    device_info->module_id = module_id;
    device_info->fw_ver_compat_warning = (compatibility == 2);

    memcpy(device_info->fw_rev,
           device_capability->fw_rev,
           sizeof(device_info->fw_rev));
    memcpy(device_info->fw_branch_name,
           device_capability->fw_branch_name,
           sizeof(device_info->fw_branch_name) - 1);
    memcpy(device_info->fw_commit_time,
           device_capability->fw_commit_time,
           sizeof(device_info->fw_commit_time) - 1);
    memcpy(device_info->fw_commit_hash,
           device_capability->fw_commit_hash,
           sizeof(device_info->fw_commit_hash) - 1);
    memcpy(device_info->fw_build_time,
           device_capability->fw_build_time,
           sizeof(device_info->fw_build_time) - 1);
    memcpy(device_info->fw_build_id,
           device_capability->fw_build_id,
           sizeof(device_info->fw_build_id) - 1);
    memcpy(device_info->serial_number,
           device_capability->serial_number,
           sizeof(device_info->serial_number));
    memcpy(device_info->model_number,
           device_capability->model_number,
           sizeof(device_info->model_number));

    ni_query_fl_fw_versions(device_handle, device_info);

    device_info->max_fps_4k = hw_capability->max_4k_fps;
    device_info->max_instance_cnt = hw_capability->max_number_of_contexts;
    device_info->device_type = device_type;

    ni_rsrc_fill_device_info(device_info,
                             (ni_codec_t)hw_capability->codec_format,
                             device_type,
                             hw_capability);
}

static void fill_shared_memory(ni_device_queue_t *device_queue,
                               const int should_match_rev,
                               const int existing_number_of_devices,
                               const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    int i, j, compatible_device_counter;

    memset(device_queue->xcoder_cnt, 0, sizeof(device_queue->xcoder_cnt));
    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        for (j = 0; j < NI_MAX_DEVICE_CNT; j++)
        {
            device_queue->xcoders[i][j] = -1;
        }
    }

    compatible_device_counter = 0;
    for (i = 0; i < existing_number_of_devices; i++)
    {
        if (!add_to_shared_memory(device_names[i],
                                 false,
                                 should_match_rev,
                                 device_queue))
        {
            continue;
        }
        compatible_device_counter++;
        if (compatible_device_counter >= NI_MAX_DEVICE_CNT)
        {
            ni_log(NI_LOG_INFO,
                   "Maximum number of supported and compatible devices "
                   "reached. Ignoring other supported and compatible "
                   "devices.\n");
            break;
        }
    }
}

bool find_available_guid(ni_device_queue_t *device_queue, int device_type, int *guidn)
{
    /*
    Create a mask of all 128 guids, mark used by 1 and unused by 0.
    */
    int32_t temp_guid;
    int32_t i, j;
    uint32_t guid_mask[4] = {0};
    
    for (i = 0; i < NI_MAX_DEVICE_CNT; i++)
    {
        temp_guid = device_queue->xcoders[device_type][i];
        if (temp_guid >= 0 && temp_guid < NI_MAX_DEVICE_CNT)
        {
            guid_mask[temp_guid / 32] |= (1u << ((uint32_t)temp_guid % 32));
        }
    }
    //from the masks find the first available guidn
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 32; j++)
        {
            if ((guid_mask[i] & (1u << j)) == 0)
            {
                *guidn = (i * 32) + j;
                return true;
            }
        }
    }

    return false;
}

bool add_to_shared_memory(const char device_name[NI_MAX_DEVICE_NAME_LEN],
                          const bool device_open_should_succeed,
                          const int should_match_rev,
                          ni_device_queue_t *device_queue)
{
    int compatibility;
    int32_t guid;
    int i, j;
    uint32_t max_io_size;

    ni_device_capability_t device_capability;
    ni_device_handle_t device_handle;
    ni_device_info_t device_info;
    bool success = true;

    max_io_size = NI_INVALID_IO_SIZE;
    device_handle = ni_device_open(device_name, &max_io_size);
    if (device_handle == NI_INVALID_DEVICE_HANDLE)
    {
        if (device_open_should_succeed)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): Failed to add %s\n: Failed ni_device_open()\n",
                   __FUNCTION__,
                   device_name);
            return false;
        }
        return true;
    }

    if (!check_device_capability(device_handle,
                                 &device_capability,
                                 device_name,
                                 should_match_rev,
                                 &compatibility))
    {
        LRETURN;
    }

    if (compatibility == 2)
    {
        ni_log(NI_LOG_INFO,
               "WARNING: %s with FW revision %.*s is below the minimum "
               "supported version of this SW revision. Some features "
               "may be missing!\n",
               device_name,
               (int)sizeof(device_capability.fw_rev),
               device_capability.fw_rev);
    }


    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        if (!device_capability.xcoder_cnt[i])
        {
            ni_log(NI_LOG_DEBUG,
                   "%s %s disabled...\n",
                   device_name,
                   GET_XCODER_DEVICE_TYPE_STR(i));
            continue;
        }

        j = device_queue->xcoder_cnt[i];
        guid = j ? device_queue->xcoders[i][j-1] + 1 : 0;
        if (guid >= NI_MAX_DEVICE_CNT && !find_available_guid(device_queue, i, &guid))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): Failed to add %s: too many devices\n",
                   __FUNCTION__,
                   device_name);
            success = false;
            LRETURN;
        }
        device_queue->xcoders[i][j] = guid;
        device_queue->xcoder_cnt[i]++;

        fill_device_info(&device_info,
                         guid,
                         device_handle,
                         device_name,
                         compatibility,
                         &device_capability,
                         (ni_device_type_t)i);

        ni_rsrc_get_one_device_info(&device_info);
    }

END:
    ni_device_close(device_handle);

    return success;
}

#ifdef _ANDROID
/*!*****************************************************************************
 *  \brief Open file and create file descriptor for mmap in android
 *
 *  \param[out] fd file descriptor
 * 
 *  \param[in] path file path
 * 
 *  \param[in] flag only O_CREAT and O_EXCL now, other flags will be ignored
 * 
 *  \param[in] size set the size of file (do not use ftruncate after the file descriptor is created in android)
 * 
 *  \return If flag is O_CREAT|O_EXCL and file already exist return -17
 *          If get one descriptor return 0
 *          If can not get one file descriptor return -1
 ******************************************************************************/
static int android_open_shm_file(int &fd, const char *path, int flag, size_t size)
{
    int ret = ni_rsrc_android_init();
    if (service == NULL)
    {
        ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_pool Error service ..\n");
        return -1;
    }

    string param(path);

    Return<void> retvalue =
        service->GetAppFlag(param, [&](int32_t ret, const hidl_handle &handle) {
            ni_log(NI_LOG_INFO, "GetAppFlag: ret %d\n", ret);
            if (ret > 0)
            {
                fd = dup(handle->data[0]);
                ni_log(NI_LOG_INFO, "vendor:GetAppFlag fd:%d\n", fd);
            } else
            {
                ni_log(NI_LOG_ERROR, "Error %d: get fd ..\n", NI_ERRNO);
                fd = -1;
                
            }
        });

    if (!retvalue.isOk())
    {
        ni_log(NI_LOG_ERROR, "service->GetAppFlag ret failed ..\n");
        fd = -1;
        return -1;
    }

    if(fd != -1)
    {
        if(flag & O_CREAT && flag & O_EXCL)
        {
            //when O_CREAT|O_EXCL and fd already exist, shm_open() will set errno = 17 and return -1
            //fd = -1;//do not set fd to -1 as shm_open(),return the existing fd and set the return value to -17
            return -17;
        }
        else
        {
            return 0;
        }
    }
    else if(flag & O_CREAT)
    {
        int shm_fd = ashmem_create_region(path, size);
        if(shm_fd >= 0)
        {
            native_handle_t *handle = native_handle_create(1, 0);
            handle->data[0] = shm_fd;
            hidl_handle this_hidl_handle;
            this_hidl_handle.setTo(handle, true);
            service->SetAppFlag(param, this_hidl_handle);
            fd = dup(handle->data[0]);
            if(fd == -1)
            {
                ni_log(NI_LOG_ERROR, "shm_fd %d was created but dup failed. errno:%s\n", handle->data[0], strerror(errno));
                return -1;
            }
        }
        else
        {
            ni_log(NI_LOG_ERROR, "Could not create shm fd\n");
            fd = -1;
            return -1;
        }
    }
    else
    {
        ni_log(NI_LOG_ERROR, "%s does not exist and O_CREAT was not set", path);
        fd = -1;
        return -1;
    }
    return 0;
}
#endif

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
int ni_rsrc_init_priv(const int should_match_rev,
                      const int existing_number_of_devices,
                      const char existing_device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    DWORD rc = 0;
    HANDLE lock = NULL;
    HANDLE map_file_handle = NULL;
    ni_device_queue_t* p_device_queue = NULL;
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
        if(ERROR_ALREADY_EXISTS == rc)
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
        UnmapViewOfFile(p_device_queue);
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
    }

    if (WAIT_ABANDONED == WaitForSingleObject(lock, INFINITE))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: failed to obtain mutex: %p\n",
                NI_ERRNO, lock);
        ReleaseMutex(lock);
        UnmapViewOfFile(p_device_queue);
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
    }

    fill_shared_memory(p_device_queue,
                       should_match_rev,
                       existing_number_of_devices,
                       existing_device_names);

    UnmapViewOfFile(p_device_queue);
    ReleaseMutex(lock);
    return NI_RETCODE_SUCCESS;
}

#elif __linux__ || __APPLE__

static ni_retcode_t create_or_read_shm(int &shm_fd,
                                       int *oflag,
                                       const mode_t mode)
{
    *oflag = O_CREAT | O_EXCL | O_RDWR;
#ifdef _ANDROID
    int android_open_shm_file_ret = android_open_shm_file(shm_fd, CODERS_SHM_NAME, *oflag, sizeof(ni_device_queue_t));
    if(android_open_shm_file_ret == -17)/* EEXIST */
    {
        *oflag = O_RDWR;
        ni_log(NI_LOG_INFO, "%s exists\n", CODERS_SHM_NAME);
    }
    else if(android_open_shm_file_ret == -1)
    {
        ni_log(NI_LOG_ERROR, "failed to open file: %s\n", CODERS_SHM_NAME);
        return NI_RETCODE_FAILURE;
    }
#else
    shm_fd = shm_open(CODERS_SHM_NAME, *oflag, mode);
    while (shm_fd == -1)
    {
        if (NI_ERRNO == 17 /* EEXIST */)
        {
            ni_log(NI_LOG_INFO, "%s exists\n", CODERS_SHM_NAME);
            *oflag = O_RDWR;
            shm_fd = shm_open(CODERS_SHM_NAME, *oflag, mode);
        }
        else
        {
           ni_log(NI_LOG_ERROR,
                  "ERROR: %s(): shm_open failed for %s: %s\n",
                  __func__,
                  CODERS_SHM_NAME,
                  strerror(NI_ERRNO));
           return NI_RETCODE_FAILURE;
        }
    }
#endif

    if (*oflag == O_RDWR)
    {
        ni_log(NI_LOG_INFO, "checking correctness of %s\n", CODERS_SHM_NAME);
    }

#ifndef _ANDROID
    else
    {
        if (ftruncate(shm_fd, sizeof(ni_device_queue_t)) == -1)
        {
           ni_log(NI_LOG_ERROR,
                  "ERROR: %s(): ftruncate for %s: %s\n",
                  __func__,
                  CODERS_SHM_NAME,
                  strerror(NI_ERRNO));
           shm_unlink(CODERS_SHM_NAME);
           return NI_RETCODE_FAILURE;
        }
        ni_log(NI_LOG_INFO, "%s created\n", CODERS_SHM_NAME);
    }
#endif

    return NI_RETCODE_SUCCESS;
}

static ni_retcode_t get_lock(int *lck_fd, const mode_t mode)
{
    int lockf_obtained, flags, xcoder_lck_fd;
    unsigned int i;

    flags = O_RDWR|O_CREAT|O_CLOEXEC;

#ifdef _ANDROID
    if (0 != access(LOCK_DIR, 0))
    {
        if (0 != mkdir(LOCK_DIR, 777))
        {
            ni_log(NI_LOG_ERROR, "ERROR: Could not create the %s directory",
                    LOCK_DIR);
            return NI_RETCODE_FAILURE;
        }
    }
#endif

    *lck_fd = open(CODERS_LCK_NAME, flags, mode);
    if (*lck_fd == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): open failed for %s: %s\n",
               __func__,
               CODERS_LCK_NAME,
               strerror(NI_ERRNO));
        return NI_RETCODE_FAILURE;
    }

    lockf_obtained = 0;
    for (i = 0; i < 5 && !lockf_obtained; i++)
    {
        if (!lockf(*lck_fd, F_TLOCK, 0))
        {
            lockf_obtained = 1;
        }
        else
        {
            ni_usleep(1000000); /* 1 second */
        }
    }

    if (!lockf_obtained)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): lock failed for %s: %s\n",
               __func__,
               CODERS_LCK_NAME,
               strerror(NI_ERRNO));
        return NI_RETCODE_FAILURE;
    }

    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        xcoder_lck_fd = open(XCODERS_RETRY_LCK_NAME[i], flags, mode);
        if (xcoder_lck_fd == -1)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): open failed for %s: %s\n",
                   __func__,
                   XCODERS_RETRY_LCK_NAME[i],
                   strerror(NI_ERRNO));
            return NI_RETCODE_FAILURE;
        }
    }

    return NI_RETCODE_SUCCESS;
}

/**
 * @brief This function is used to check if the existing device queue matches the expectation.
*/
static bool check_correctness_count(const ni_device_queue_t *existing_device_queue,
                                    const int should_match_rev,
                                    const int existing_number_of_devices,
                                    const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    int compatibility;
    uint32_t max_io_size;
    int i, j, k;

    ni_device_capability_t device_capability;
    ni_device_handle_t device_handle;
    ni_device_queue_t device_queue;

    memset(device_queue.xcoder_cnt, 0, sizeof(device_queue.xcoder_cnt));

    max_io_size = NI_INVALID_IO_SIZE;
    for (i = 0; i < existing_number_of_devices; i++)
    {
        device_handle = ni_device_open(device_names[i], &max_io_size);
        if (device_handle == NI_INVALID_DEVICE_HANDLE)
        {
            continue;
        }

        if (!check_device_capability(device_handle,
                                     &device_capability,
                                     device_names[i],
                                     should_match_rev,
                                     &compatibility))
        {
            goto NEXT;
        }

        for (j = NI_DEVICE_TYPE_DECODER; j < NI_DEVICE_TYPE_XCODER_MAX; j++)
        {
            //Don't count if not in queue. 
            if (existing_device_queue->xcoders[j][i] == -1 && device_capability.xcoder_cnt[j] != 0)
            {
                //If not in queue then it shouldn't be in any device module.
                for (k = NI_DEVICE_TYPE_DECODER; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
                {
                    if (existing_device_queue->xcoders[k][i] != -1)
                    {
                        ni_log(NI_LOG_ERROR,
                               "ERROR: %s(): Discovered device %s is not in queue for module %s but is in %s\n",
                               __func__,
                               device_names[i],
                               GET_XCODER_DEVICE_TYPE_STR(j), GET_XCODER_DEVICE_TYPE_STR(k));
                        ni_device_close(device_handle);
                        return false;
                    }
                }
                continue;
            }
            device_queue.xcoder_cnt[j] += device_capability.xcoder_cnt[j];
        }
NEXT:
        ni_device_close(device_handle);
    }

    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        if (device_queue.xcoder_cnt[i] == existing_device_queue->xcoder_cnt[i])
        {
            continue;
        }
        ni_log(NI_LOG_ERROR,
               "WARNING: %s(): Discovered blocks %u != Existing blocks %u\n",
               __func__,
               device_queue.xcoder_cnt[i],
               existing_device_queue->xcoder_cnt[i]);
        return false;
    }

    return true;
}

static bool check_device_queue(const ni_device_queue_t *existing_device_queue,
                               const int existing_number_of_devices,
                               const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    int32_t module_id;
    int i, j;

    ni_device_context_t *device_context;

    for (i = 0; i < existing_number_of_devices; i++)
    {
        for (j = NI_DEVICE_TYPE_DECODER; j < NI_DEVICE_TYPE_XCODER_MAX; j++)
        {
            module_id = existing_device_queue->xcoders[j][i];
            if (module_id == -1)
            {
                break;
            }
            device_context = ni_rsrc_get_device_context((ni_device_type_t)j,
                                                        module_id);
            if (!device_context)
            {
               ni_log(NI_LOG_ERROR,
                      "WARNING: %s(): Missing device context for %s %s\n",
                      __func__,
                      device_names[i],
                      GET_XCODER_DEVICE_TYPE_STR(j));
               return false;
            }
            ni_rsrc_free_device_context(device_context);
        }
    }

    return true;
}

#ifndef _ANDROID
static void sigbus_handler(int signal)
{
    siglongjmp(env, 1);
}

static void setup_signal_handler(struct sigaction *p, const int signum)
{
    struct sigaction c;

    memset(&c, 0, sizeof(struct sigaction));
    if (sigemptyset(&c.sa_mask) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not initialize signal set: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
    c.sa_handler = sigbus_handler;

    if (sigaction(signum, NULL, p) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not save previous signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }

    if (sigaction(signum, &c, NULL) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not register signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
}
#endif

static bool check_correctness(const ni_device_queue_t *existing_device_queue,
                              const int should_match_rev,
                              const int existing_number_of_devices,
                              const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    bool result = false;
#ifndef _ANDROID
    const int signum = SIGBUS;
    struct sigaction p;

    setup_signal_handler(&p, signum);

    if (sigsetjmp(env, 1))
    {
        LRETURN;
    }
#endif

    if (!check_correctness_count(existing_device_queue,
                                 should_match_rev,
                                 existing_number_of_devices,
                                 device_names))
    {
        LRETURN;
    }

    if (!check_device_queue(existing_device_queue,
                            existing_number_of_devices,
                            device_names))
    {
        LRETURN;
    }

    result = true;
    ni_log(NI_LOG_INFO, "%s ok\n", CODERS_SHM_NAME);

end:
#ifndef _ANDROID
    if (sigaction(signum, &p, NULL) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not restore previous signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
#endif
    return result;
}

static void delete_shm(void)
{
#ifdef _ANDROID
    /*! return if init has already been done */
    int ret = ni_rsrc_android_init();
    if (service == NULL)
    {
        ni_log(NI_LOG_ERROR, "ni_rsrc_get_device_pool Error service ..\n");
        return;
    }

    Return<void> retvalue = service->RemoveAllAppFlags();

    if (!retvalue.isOk())
    {
        ni_log(NI_LOG_ERROR, "service->RemoveAllAppFlags ret failed ..\n");
        return;
    }
#else
    DIR *dir;
    struct dirent *dirent;
    char path_to_remove[PATH_MAX];

    ni_log(NI_LOG_ERROR, "Deleting shared memory files in %s\n", LOCK_DIR);

    dir = opendir(LOCK_DIR);
    if (!dir)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): opendir failed for %s: %s\n",
               __func__,
               LOCK_DIR,
               strerror(NI_ERRNO));
        return;
    }

    while ((dirent = readdir(dir)) != NULL)
    {
        if (strncmp(dirent->d_name, "NI_", 3) != 0)
        {
            continue;
        }
        snprintf(path_to_remove, PATH_MAX, "%s/%s", LOCK_DIR, dirent->d_name);
        remove(path_to_remove);
    }

    if (closedir(dir) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): closedir failed for %s: %s\n",
               __func__,
               LOCK_DIR,
               strerror(NI_ERRNO));
        return;
    }
#endif
    ni_log(NI_LOG_INFO, "Deleted shared memory files in %s\n", LOCK_DIR);
}

int ni_rsrc_init_priv(const int should_match_rev,
                      const int existing_number_of_devices,
                      const char existing_device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

    int return_value = 0;
    int lck_fd = -1;
    int shm_fd = -1;
    int shm_flag;

    ni_device_queue_t *p_device_queue;

    if ((create_or_read_shm(shm_fd, &shm_flag, mode) != NI_RETCODE_SUCCESS) ||
        (get_lock(&lck_fd, mode) != NI_RETCODE_SUCCESS))
    {
        return_value = 1;
        LRETURN;
    }

    p_device_queue = (ni_device_queue_t *)mmap(0,
                                               sizeof(ni_device_queue_t),
                                               PROT_READ | PROT_WRITE,
                                               MAP_SHARED,
                                               shm_fd,
                                               0);
    if (p_device_queue == MAP_FAILED)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): mmap failed: %s\n",
               __func__,
               strerror(NI_ERRNO));
        return_value = 1;
        LRETURN;
    }

    if (shm_flag == O_RDWR)
    {
        if (check_correctness(p_device_queue,
                              should_match_rev,
                              existing_number_of_devices,
                              existing_device_names))
        {
            LRETURN;
        }

        munmap(p_device_queue, sizeof(ni_device_queue_t));
        lockf(lck_fd, F_ULOCK, 0);
        close(shm_fd);
        delete_shm();
        return ni_rsrc_init_priv(should_match_rev,
                                 existing_number_of_devices,
                                 existing_device_names);
    }

    fill_shared_memory(p_device_queue,
                       should_match_rev,
                       existing_number_of_devices,
                       existing_device_names);

end:
    lockf(lck_fd, F_ULOCK, 0);

    if (shm_fd >= 0)
    {
        close(shm_fd);
    }

    return return_value;
}

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
  bool skip_ftruncate = false;
  ni_retcode_t error = NI_RETCODE_SUCCESS;
  ni_device_info_t * p_coder_info_dst = NULL;

  if( !p_device_info )
  {
    return;
  }

  ni_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));

  ni_log(NI_LOG_INFO, "shm_name: %s, lck_name %s\n", shm_name, lck_name);

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
  shm_fd = shm_open(shm_name,
                    O_CREAT | O_RDWR | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (shm_fd == -1)
  {
    if (NI_ERRNO == 17 /* EEXIST */)
    {
      skip_ftruncate = true;
      shm_fd = shm_open(shm_name,
                        O_RDWR,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    }
    else
    {
      ni_log(NI_LOG_ERROR,
             "ERROR %s() shm_open() CODERS_SHM_NAME: %s\n",
             __func__,
             strerror(NI_ERRNO));
      error = NI_RETCODE_FAILURE;
      LRETURN;
    }
  }
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
  if (!skip_ftruncate && ftruncate(shm_fd, sizeof(ni_device_info_t)) < 0)
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
  if (shm_fd >= 0)
  {
    close(shm_fd);
  }

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


/*!******************************************************************************
 *  \brief     get PCIe address information from device name
 *
 *  \param[in] char *device_name e.g. /dev/nvme0n1.
 *  \param[out] char *pcie e.g. 0000:0a:00.0. Should be at least 13 bytes including null terminator
 *  \param[out] char *domain, optional. Should be at least 5 bytes including null terminator
 *  \param[out] char *slot, optional. Should be at least 3 bytes including null terminator
 *  \param[out] char *dev, optional. Should be at least 3 bytes including null terminator
 *  \param[out] char *func, optional. Should be at least 2 bytes including null terminator
 *
 *  \return    void
 *  *******************************************************************************/
void get_dev_pcie_addr(char *device_name, 
                       char *pcie, 
                       char *domain, char *slot, char *dev, char *func)
{
#ifndef __linux__
  return;
#else 
  int i=0;
  char *ptr = NULL;
  // path to nvme drive
  char path[PATH_MAX];
  int ret;
  
  if(!device_name || !pcie)
  {
    return ;
  }

  // we need to get device name from '/dev/' and remove the trailing 'n1'
  char *start = device_name + 5; // skip '/dev/'
  char *last_n = strrchr(start, 'n');
  int dev_name_len = (int)(last_n - start);

  // construct the path to /sys/class/nvme/
  snprintf(path, sizeof(path), "/sys/class/nvme/%.*s", dev_name_len, start);
  ni_log2(NULL, NI_LOG_DEBUG,"path:%s\n", path);

  // read the target of the symbolic link
  char target[PATH_MAX];
  //e.g.: ../../devices/pci0000:00/0000:00:03.1/0000:09:00.0/nvme/nvme0
  ssize_t len = readlink(path, target, sizeof(target) - 1);
  if (len == -1) {
      perror("readlink");
      return;
  }
  target[len] = '\0';  // set the null-terminating character
  ni_log2(NULL, NI_LOG_DEBUG,"target:%s\n", target);

  // and find domain and slot from it
  char *saveptr = NULL;
  ptr = ni_strtok(target, "/", &saveptr);
  pcie[4] = pcie[7] = ':';
  pcie[10] = '.';
  //last pcie info is for the device
  while(ptr != NULL) {
      ni_log2(NULL, NI_LOG_DEBUG, "===%d ptr:%s\n", ++i, ptr);
      if (strlen(ptr) == 12)//e.g.: 0000:09:00.0
      {
          ret = sscanf(ptr, "%4c:%2c:%2c.%1c", pcie, pcie+5,pcie+8,pcie+11);
          if (ret != 4)
          {
            ni_log2(NULL, NI_LOG_DEBUG, "\tsscanf error %d errno %d %s\n", ret, errno, strerror(errno));
          }
      }
      ni_log2(NULL, NI_LOG_DEBUG, "=====\n");
      ptr = ni_strtok(NULL, "/", &saveptr);
  }
  pcie[12] = '\0';
  ni_log2(NULL, NI_LOG_DEBUG, "PCIE:%s\n", pcie);
  if (!domain || !slot || !dev || !func)
  {
    goto end;
  }
  domain[4] = slot[2] = dev[2] = func[1] = '\0';
  sscanf(pcie, "%4[^:]:%2[^:]:%2[^.].%1s", domain, slot, dev, func);
  ni_log2(NULL, NI_LOG_DEBUG, "\t%d: Domain: %s, Slot: %s, Device: %s, Function: %s\n", i, domain, slot, dev, func);
end:
  return;
#endif
}

#endif
