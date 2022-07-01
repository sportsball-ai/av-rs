/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_rsrc_api.h
*
*  \brief  Exported definitions related to resource management of NI T-408 
*          devices
*
*******************************************************************************/

#pragma once

#include "ni_defs.h"

#ifdef _ANDROID
#include <cutils/ashmem.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NI_MAX_HW_DECODER_COUNT    128
#define NI_MAX_HW_ENCODER_COUNT    128
#define NI_MAX_SW_INSTANCE_COUNT   8
#define NI_MAX_DEVICE_NAME_LEN     32
#define NI_MAX_PROFILE_NAME_LEN    128
#define NI_MAX_LEVEL_NAME_LEN      64
#define NI_MAX_ADDITIONAL_INFO_LEN 64

typedef struct _ni_rsrc_video_ref_cap
{
  int width;
  int height;
  int fps;
} ni_rsrc_device_video_ref_cap_t;

typedef enum 
{
  EN_H264,
  EN_H265
}ni_codec_t;
 
typedef enum
{
  EN_IDLE,
  EN_ACTIVE
}ni_sw_instance_status_t;

typedef enum 
{
  EN_ALLOC_LEAST_LOAD,
  EN_ALLOC_LEAST_INSTANCE
}ni_alloc_rule_t;

typedef struct _ni_device_queue
{
  uint32_t decoders_cnt;
  uint32_t encoders_cnt;
  int32_t decoders[NI_MAX_HW_DECODER_COUNT];
  int32_t encoders[NI_MAX_HW_ENCODER_COUNT];
} ni_device_queue_t;

typedef struct _ni_device_video_capability
{
  int                    max_res_width; /*! max resolution */
  int                    max_res_height;
  int                    min_res_width; /*! min resolution */
  int                    min_res_height;
  char                   profiles_supported[NI_MAX_PROFILE_NAME_LEN];
  char                   level[NI_MAX_LEVEL_NAME_LEN];
  char                   additional_info[NI_MAX_ADDITIONAL_INFO_LEN];
} ni_device_video_capability_t;

typedef struct _ni_sw_instance_info
{
  int                 id;
  ni_sw_instance_status_t status;
  ni_codec_t          codec;
  int                 width;
  int                 height;
  int                 fps;
} ni_sw_instance_info_t;
  
typedef struct _ni_device_pool
{
  ni_lock_handle_t lock;
  ni_device_queue_t *p_device_queue;
} ni_device_pool_t;

typedef struct _ni_device_info
{
  char                   dev_name[NI_MAX_DEVICE_NAME_LEN];
  char                   blk_name[NI_MAX_DEVICE_NAME_LEN];
  int                    hw_id;
  int                    module_id; /*! global unique id, assigned at creation */
  int                    load;       /*! p_load value retrieved from f/w */
  int                    model_load; /*! p_load value modelled internally */
  unsigned long          xcode_load_pixel; /*! xcode p_load in pixels: encoder only */
  int                    fw_ver_compat_warning; // fw revision is not supported by this libxcoder
  uint8_t                fw_rev[8]; // fw revision
  uint8_t                fw_commit_hash[41];
  uint8_t                fw_commit_time[26];
  uint8_t                fw_branch_name[256];

  /*! general capability attributes */
  int                    max_fps_1080p; /*! max fps for 1080p (1920x1080) */
  int                    max_instance_cnt; /*! max number of instances */
  int                    active_num_inst; /*! active numver of instances */
  ni_device_type_t       device_type;     /*! decoder or encoder */

  /*! decoder/encoder capabilities */
  int                    supports_h264; /*! supports "type" (enc/dec) of H.264 */
  ni_device_video_capability_t h264_cap;

  int                    supports_h265; /*! supports "type" (enc/dec) of H.265 */
  ni_device_video_capability_t h265_cap;

  ni_sw_instance_info_t sw_instance[NI_MAX_SW_INSTANCE_COUNT];
} ni_device_info_t;

typedef struct _ni_device 
{
  int decoders_cnt;
  int encoders_cnt;
  ni_device_info_t decoders[NI_MAX_HW_DECODER_COUNT];
  ni_device_info_t encoders[NI_MAX_HW_ENCODER_COUNT];
} ni_device_t;

typedef struct _ni_device_context 
{
  char   shm_name[NI_MAX_DEVICE_NAME_LEN];
  ni_lock_handle_t    lock;
  ni_device_info_t * p_device_info;
} ni_device_context_t;

typedef struct _ni_card_info 
{
  char *name;
  int   index;
  bool  isActive;
  const char *fwVer;
  const char *hwVer;
} ni_card_info_t;

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
LIB_API int ni_rsrc_init(int should_match_rev, int timeout_seconds);

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
LIB_API ni_retcode_t ni_rsrc_refresh(int should_match_rev);

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
LIB_API int ni_rsrc_get_local_device_list(char ni_devices[][MAX_DEVICE_NAME_LEN], int max_handles);
    
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
LIB_API ni_device_context_t * ni_rsrc_get_device_context(ni_device_type_t type, int guid);

/*!******************************************************************************
 *  \brief    Free previously allocated device context
 *
 *  \param    p_device_context Pointer to previously allocated device context
 *
 *  \return   None
 *******************************************************************************/
LIB_API void ni_rsrc_free_device_context(ni_device_context_t *p_ctxt);

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
LIB_API ni_retcode_t ni_rsrc_list_devices(ni_device_type_t device_type,
    ni_device_info_t* p_device_info, int* p_device_count);

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
LIB_API ni_retcode_t ni_rsrc_list_all_devices(ni_device_t* p_device);

/*!*****************************************************************************
*  \brief        Print detailed capability information of all devices 
*                on the system.

*   \param       none
*
*   \return      none
*
*******************************************************************************/
LIB_API void ni_rsrc_print_all_devices_capability(void);

/*!******************************************************************************
*   \brief       Query a specific device with detailed information on the system

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
LIB_API ni_device_info_t* ni_rsrc_get_device_info(ni_device_type_t device_type, int guid);

/*!****************************************************************************

*   \brief      Get the least used device that can handle decoding or encoding
*               a video stream of certain resolution/frame-rate/codec.
*
*   \param[in]  width      width of video resolution
*   \param[in]  height     height of video resolution
*   \param[in]  frame_rate video stream frame rate
*   \param[in]  codec      EN_H264 or EN_H265
*   \param[in]  type       NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[out] info       detailed device information. If is non-NULL, the
*                          device info is stored in the memory pointed to by it.
*
*   \return     device GUID (>= 0) if found , -1 otherwise
*******************************************************************************/
LIB_API int ni_rsrc_get_available_device(int width, int height, int frame_rate,
                            ni_codec_t codec, ni_device_type_t device_type,
                            ni_device_info_t * p_device_info);

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
LIB_API int ni_rsrc_update_device_load(ni_device_context_t* p_ctxt, int load,
    int sw_instance_cnt, const ni_sw_instance_info_t sw_instance_info[]);

/*!*****************************************************************************
*   \brief      Allocate resources for decoding/encoding, based on the provided rule
*
*   \param[in]  device_type NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*   \param[in]  rule        allocation rule
*   \param[in]  codec       EN_H264 or EN_H265
*   \param[in]  width       width of video resolution
*   \param[in]  height      height of video resolution
*   \param[in]  frame_rate  video stream frame rate
*   \param[out] p_load      the load that will be generated by this encoding
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
LIB_API ni_device_context_t* ni_rsrc_allocate_auto( ni_device_type_t device_type,
                                                    ni_alloc_rule_t rule,
                                                    ni_codec_t codec,
                                                    int width, int height, 
                                                    int frame_rate,
                                                    unsigned long *p_load);

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
LIB_API ni_device_context_t* ni_rsrc_allocate_direct( ni_device_type_t device_type, 
                                                      int guid,
                                                      ni_codec_t codec,
                                                      int width, 
                                                      int height,
                                                      int frame_rate,
                                                      unsigned long *p_load);

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
LIB_API ni_device_context_t *ni_rsrc_allocate_simple_direct
(
  ni_device_type_t device_type,
  int guid
);

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
*******************************************************************************/
LIB_API void ni_rsrc_release_resource(ni_device_context_t *p_ctxt, ni_codec_t codec,
                                      unsigned long load);

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
LIB_API int ni_rsrc_codec_is_available(int guid, ni_device_type_t device_type);

/*!*****************************************************************************
*   \brief      Remove an NetInt h/w device from resource pool on the host.
*
*   \param[in]  p_dev  the NVMe device name
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
LIB_API int ni_rsrc_remove_device(const char* p_dev);
 
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
LIB_API int ni_rsrc_add_device(const char* p_dev, int should_match_rev);

/*!*****************************************************************************
 *  \brief  Create and return the allocated ni_device_pool_t struct
 *
 *  \param  None
 *
 *  \return Pointer to ni_device_pool_t struct on success, or NULL on failure
 *******************************************************************************/
LIB_API ni_device_pool_t* ni_rsrc_get_device_pool(void);

/*!*****************************************************************************
*   \brief      Free all resources taken by the device pool
*
*   \param[in]  p_device_pool  Poiner to a device pool struct
*
*   \return     None
*******************************************************************************/
LIB_API void ni_rsrc_free_device_pool(ni_device_pool_t* p_device_pool);

/*!*****************************************************************************
 *  \brief  Print the content of the ni_device_info_t struct
 *
 *  \param  p_device_info - pointer to the ni_device_info_t struct to print
 *
 *  \return None
 *******************************************************************************/
LIB_API void ni_rsrc_print_device_info(const ni_device_info_t* p_device_info);

/*!*****************************************************************************
 *  \brief  lock a file lock and open a session on a device
 *
 *  \param device_type
 *  \param lock
 *
 *  \return None
 *******************************************************************************/
LIB_API int ni_rsrc_lock_and_open(int device_type, ni_lock_handle_t* lock);

/*!*****************************************************************************
 *  \brief  unlock a file lock
 *
 *  \param device_type
 *  \param lock
 *
 *  \return None
 *******************************************************************************/
LIB_API int ni_rsrc_unlock(int device_type, ni_lock_handle_t lock);

#ifdef __cplusplus
}
#endif
