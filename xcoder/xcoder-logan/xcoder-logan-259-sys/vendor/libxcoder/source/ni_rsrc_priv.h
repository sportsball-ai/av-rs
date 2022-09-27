/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_rsrc_priv.h
*
*  \brief  Private definitions related to resource management of NI T-408
*          devices
*
*******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_device_api.h"

#define LOCK_DIR        "/dev/shm"
#define CODERS_SHM_NAME "NI_SHM_CODERS"
#define CODERS_LCK_NAME "/dev/shm/NI_LCK_CODERS"
#define CODERS_RETRY_DELCK_NAME "/dev/shm/NI_RETRY_LCK_DECODERS"
#define CODERS_RETRY_ENLCK_NAME "/dev/shm/NI_RETRY_LCK_ENCODERS"
#define MAX_LOCK_RETRY  6000
#define LOCK_WAIT       10000  // wait in us

extern LIB_API uint32_t stop_process;

/*!******************************************************************************
 *  \brief Returns the device lock name
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_lock_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len);

/*!******************************************************************************
 *  \brief Returns the name of shared memory of the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_shm_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len);

/*!******************************************************************************
 *  \brief Updates the codec record
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t *p_device_context, ni_session_context_t *p_session_ctx);

/*!******************************************************************************
 *  \brief Retrieve codec record info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_one_device_info(ni_device_info_t *p_device_info);

/*!******************************************************************************
 *  \brief Update codec record info with retrieved device info from HW
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_rsrc_fill_device_info(ni_device_info_t* p_device_info, ni_codec_t fmt, ni_device_type_t type, ni_hw_capability_t* p_hw_cap);

/*!******************************************************************************
 *  \brief List NETINT nvme devices
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_enumerate_devices(char ni_devices[][MAX_DEVICE_NAME_LEN], int max_handles);


/*!******************************************************************************
 *  \brief String comparison function
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_strcmp(const void* p_str, const void* p_str1);

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
int ni_is_fw_compatible(uint8_t fw_rev[8]);

#ifdef __cplusplus
}
#endif
