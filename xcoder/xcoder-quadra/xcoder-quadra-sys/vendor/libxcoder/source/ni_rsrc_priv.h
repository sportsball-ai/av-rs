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
 *  \file   ni_rsrc_priv.h
 *
 *  \brief  Private definitions used by ni_rsrc_api.cpp for management of
 *          NETINT video processing devices
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_device_api.h"

// The macro definition in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h need to be synchronized with libxcoder
// If you change this,you should also change LOCK_DIR in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h
#ifdef _ANDROID
#define LOCK_DIR 		"/dev/shm_netint"
#elif __APPLE__
#define LOCK_DIR        "/tmp"
#else
#define LOCK_DIR        "/dev/shm"
#endif

#define CODERS_LCK_NAME LOCK_DIR "/NI_LCK_CODERS"

NI_UNUSED static const char *XCODERS_RETRY_LCK_NAME[] = {
    LOCK_DIR "/NI_RETRY_LCK_DECODERS", LOCK_DIR "/NI_RETRY_LCK_ENCODERS",
    LOCK_DIR "/NI_RETRY_LCK_SCALERS", LOCK_DIR "/NI_RETRY_LCK_AI"};

#define CODERS_SHM_NAME "NI_SHM_CODERS"

// The macro definition in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h need to be synchronized with libxcoder
// If you change this,you should also change MAX_LOCK_RETRY LOCK_WAIT in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h
#define MAX_LOCK_RETRY  6000
#define LOCK_WAIT       10000  // wait in us

extern LIB_API uint32_t g_xcoder_stop_process;

// The macro definition in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h need to be synchronized with libxcoder
// If you change these functions,you should also change these functions in libxcoder_FFmpeg3.1.1only/source/ni_rsrc_priv.h
void ni_rsrc_get_lock_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len);
void ni_rsrc_get_shm_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len);
void ni_rsrc_update_record(ni_device_context_t *p_device_context, ni_session_context_t *p_session_ctx);
void ni_rsrc_get_one_device_info(ni_device_info_t *p_device_info);
ni_retcode_t ni_rsrc_fill_device_info(ni_device_info_t* p_device_info, ni_codec_t fmt, ni_device_type_t type, ni_hw_capability_t* p_hw_cap);
int ni_rsrc_init_priv(const int should_match_rev,
                      const int existing_number_of_devices,
                      const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN]);
int ni_rsrc_enumerate_devices(char ni_devices[][NI_MAX_DEVICE_NAME_LEN],
                              int max_handles);
bool add_to_shared_memory(const char device_name[NI_MAX_DEVICE_NAME_LEN],
                          const bool device_open_should_succeed,
                          const int should_match_rev,
                          ni_device_queue_t *device_queue);

int ni_rsrc_strcmp(const void* p_str, const void* p_str1);

// return 1 if fw_rev is compatible with NI_XCODER_REVISION, 0 otherwise
int ni_is_fw_compatible(uint8_t fw_rev[8]);

void get_dev_pcie_addr(char *device_name,
                       char *pcie, 
                       char *domain, char *slot, char *dev, char *func);

#ifdef __cplusplus
}
#endif
