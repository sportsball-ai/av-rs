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
*  \file   ni_rsrc_priv_logan.h
*
*  \brief  Private definitions related to resource management of NI T-408
*          devices
*
*******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_device_api_logan.h"

#ifdef _ANDROID
#define LOCK_DIR        "/dev/shm_netint"
#define CODERS_LCK_NAME "/dev/shm_netint/NI_LOGAN_LCK_CODERS"
#define CODERS_RETRY_DELCK_NAME "/dev/shm_netint/NI_LOGAN_RETRY_LCK_DECODERS"
#define CODERS_RETRY_ENLCK_NAME "/dev/shm_netint/NI_LOGAN_RETRY_LCK_ENCODERS"
#elif __APPLE__
#define LOCK_DIR        "/tmp"
#define CODERS_LCK_NAME "/tmp/NI_LOGAN_LCK_CODERS"
#define CODERS_RETRY_DELCK_NAME "/tmp/NI_LOGAN_RETRY_LCK_DECODERS"
#define CODERS_RETRY_ENLCK_NAME "/tmp/NI_LOGAN_RETRY_LCK_ENCODERS"
#else
#define LOCK_DIR        "/dev/shm"
#define CODERS_LCK_NAME "/dev/shm/NI_LOGAN_LCK_CODERS"
#define CODERS_RETRY_DELCK_NAME "/dev/shm/NI_LOGAN_RETRY_LCK_DECODERS"
#define CODERS_RETRY_ENLCK_NAME "/dev/shm/NI_LOGAN_RETRY_LCK_ENCODERS"
#endif

#define CODERS_SHM_NAME "NI_LOGAN_SHM_CODERS"


#define MAX_LOCK_RETRY  6000
#define LOCK_WAIT       10000  // wait in us

extern LIB_API uint32_t g_logan_xcoder_stop_process;

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
                           size_t max_name_len);

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
                          size_t max_name_len);

/*!******************************************************************************
 *  \brief Updates the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_update_record(ni_logan_device_context_t *p_device_context,
                           ni_logan_session_context_t *p_session_ctx);

/*!******************************************************************************
 *  \brief Retrieve codec record info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_rsrc_get_one_device_info(ni_logan_device_info_t *p_device_info);

/*!******************************************************************************
 *  \brief Update codec record info with retrieved device info from HW
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_rsrc_fill_device_info(ni_logan_device_info_t* p_device_info,
                                      ni_codec_t fmt,
                                      ni_logan_device_type_t type,
                                      ni_logan_hw_capability_t* p_hw_cap);

/*!******************************************************************************
 *  \brief List NETINT nvme devices
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_rsrc_enumerate_devices(char ni_logan_devices[][NI_LOGAN_MAX_DEVICE_NAME_LEN], int max_handles);


/*!******************************************************************************
 *  \brief String comparison function
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_logan_rsrc_strcmp(const void* p_str, const void* p_str1);

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
int ni_logan_is_fw_compatible(uint8_t fw_rev[8]);

#ifdef __cplusplus
}
#endif
