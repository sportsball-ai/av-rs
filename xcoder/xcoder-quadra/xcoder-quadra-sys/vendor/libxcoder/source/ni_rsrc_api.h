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
 *  \file   ni_rsrc_api.h
 *
 *  \brief  Public definitions for managing NETINT video processing devices
 ******************************************************************************/

#pragma once

#include "ni_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NI_PROFILES_SUPP_STR_LEN   128
#define NI_LEVELS_SUPP_STR_LEN     64
#define NI_ADDITIONAL_INFO_STR_LEN 64

typedef struct _ni_rsrc_video_ref_cap
{
  int width;
  int height;
  int fps;
} ni_rsrc_device_video_ref_cap_t;

typedef enum
{
    EN_INVALID = -1,
    EN_H264 = 0,
    EN_H265,
    EN_VP9,
    EN_JPEG,
    EN_AV1,
    EN_CODEC_MAX
} ni_codec_t;

extern bool g_device_in_ctxt;
extern ni_device_handle_t g_dev_handle;

typedef enum
{
    EN_IDLE,
    EN_ACTIVE
} ni_sw_instance_status_t;

typedef struct _ni_device_queue
{
    uint32_t xcoder_cnt[NI_DEVICE_TYPE_XCODER_MAX];
    int32_t xcoders[NI_DEVICE_TYPE_XCODER_MAX][NI_MAX_DEVICE_CNT];
} ni_device_queue_t;

typedef struct _ni_device_video_capability
{
    /*! supported codec format of enc/dec/scaler/ai of this module */
    int supports_codec;   // -1 (none), value of type ni_codec_t
    int max_res_width;    /*! max resolution */
    int max_res_height;
    int min_res_width; /*! min resolution */
    int min_res_height;
    char profiles_supported[NI_PROFILES_SUPP_STR_LEN];
    char level[NI_LEVELS_SUPP_STR_LEN];
    char additional_info[NI_ADDITIONAL_INFO_STR_LEN];
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
  uint64_t               xcode_load_pixel; /*! xcode p_load in pixels: encoder only */
  int                    fw_ver_compat_warning; // fw revision is not supported by this libxcoder
  uint8_t fl_ver_nor_flash[8]; // firmware loader version stored in nor flash
  uint8_t fl_ver_last_ran[8];
  uint8_t fw_rev_nor_flash[8]; // fw revision stored in nor flash
  uint8_t fw_rev[8]; // fw revision loaded, i.e., running
  uint8_t                fw_branch_name[256];
  uint8_t fw_commit_time[26];
  uint8_t fw_commit_hash[41];
  uint8_t fw_build_time[26];
  uint8_t fw_build_id[256];

  uint8_t serial_number[20];
  uint8_t model_number[40];

  /*! general capability attributes */
  int max_fps_4k;                          /*! max fps for 4K */
  int                    max_instance_cnt; /*! max number of instances */
  uint32_t active_num_inst;                /*! active numver of instances */
  ni_device_type_t       device_type;     /*! decoder or encoder */

  /*! decoder/encoder/scaler/ai codec support capabilities */
  ni_device_video_capability_t dev_cap[EN_CODEC_MAX];

  ni_sw_instance_info_t sw_instance[NI_MAX_CONTEXTS_PER_HW_INSTANCE];
  ni_lock_handle_t lock;
} ni_device_info_t;

// This structure is very big (2.6MB). Recommend storing in heap
typedef struct _ni_device 
{
    int xcoder_cnt[NI_DEVICE_TYPE_XCODER_MAX];
    ni_device_info_t xcoders[NI_DEVICE_TYPE_XCODER_MAX][NI_MAX_DEVICE_CNT];
} ni_device_t;

typedef struct _ni_device_context 
{
  char   shm_name[NI_MAX_DEVICE_NAME_LEN];
  ni_lock_handle_t    lock;
  ni_device_info_t * p_device_info;
} ni_device_context_t;

typedef struct _ni_card_info_quadra
{
  int card_idx;
  int load;
  int model_load;
  int task_num;
  int max_task_num;
  int shared_mem_usage;
} ni_card_info_quadra_t;

typedef struct _ni_hw_device_info_quadra
{
  int available_card_num;
  int device_type_num;
  int consider_mem;
  ni_device_type_t *device_type;
  ni_card_info_quadra_t **card_info; 
  int card_current_card;
  int err_code;
} ni_hw_device_info_quadra_t;

typedef struct _ni_hw_device_info_quadra_encoder_param
{
    uint32_t fps;/*! encoder fps*/
    uint32_t h;/*! height*/
    uint32_t w;/*! width*/
    uint32_t code_format;/*! 1 for h264,2 for h265,3 for av1,4 for JPEG*/
    uint32_t ui8enableRdoQuant;
    uint32_t rdoLevel;
    uint32_t lookaheadDepth;/*! lookaheadDepth [0:40]*/
    uint32_t bit_8_10;/*! 8 for 8 bit,10 for 10 bit*/
    int uploader;/*1 for uploader,0 for not uploader*/
    int rgba;/*1 for rgba,0 for not*/
}ni_hw_device_info_quadra_encoder_param_t;

typedef struct _ni_hw_device_info_quadra_decoder_param
{
    uint32_t fps;/*! decoder fps*/
    uint32_t h;/*! height*/
    uint32_t w;/*! width*/
    uint32_t bit_8_10;/*! 8 for 8 bit,10 for 10 bit*/
    int rgba;/*!decoder for rgba? 1 for rgba,0 for not*/
    int hw_frame;/*if out=hw,1 for out = hw_frame,0 for not*/
}ni_hw_device_info_quadra_decoder_param_t;

typedef struct _ni_hw_device_info_quadra_scaler_param
{
    uint32_t h;/*!output height*/
    uint32_t w;/*!output width*/
    uint32_t bit_8_10;/*! 8 for 8 bit,10 for 10 bit*/
    int rgba;/*!output for rgba? 1 for rgba,0 for not*/
}ni_hw_device_info_quadra_scaler_param_t;

typedef struct _ni_hw_device_info_quadra_ai_param
{
    uint32_t h;/*!output height*/
    uint32_t w;/*!output width*/
    uint32_t bit_8_10;/*! 8 for 8 bit,10 for 10 bit*/
    int rgba;/*!output for rgba? 1 for rgba,0 for not*/
}ni_hw_device_info_quadra_ai_param_t;

typedef struct _ni_hw_device_info_quadra_threshold_param
{
    ni_device_type_t device_type;
    int load_threshold;
    int task_num_threshold;
}ni_hw_device_info_quadra_threshold_param_t;

typedef struct _ni_hw_device_info_quadra_coder_param
{
    int hw_mode;
    ni_hw_device_info_quadra_encoder_param_t *encoder_param;
    ni_hw_device_info_quadra_decoder_param_t *decoder_param;
    ni_hw_device_info_quadra_scaler_param_t *scaler_param;
    ni_hw_device_info_quadra_ai_param_t *ai_param;
}ni_hw_device_info_quadra_coder_param_t;
typedef struct _ni_device_vf_ns_id
{
    uint16_t vf_id;
    uint16_t ns_id;
} ni_device_vf_ns_id_t;

typedef struct _ni_device_temp
{
    int32_t composite_temp;
    int32_t on_board_temp;
    int32_t on_die_temp;
} ni_device_temp_t;

typedef struct _ni_device_extra_info
{
    int32_t composite_temp;
    int32_t on_board_temp;
    int32_t on_die_temp;
    uint32_t power_consumption;
    uint32_t reserve[4];
} ni_device_extra_info_t;

typedef enum
{
    EN_ALLOC_LEAST_LOAD,
    EN_ALLOC_LEAST_INSTANCE
} ni_alloc_rule_t;

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
LIB_API int ni_rsrc_get_local_device_list(char ni_devices[][NI_MAX_DEVICE_NAME_LEN],
                                          int max_handles);
    
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

/*!******************************************************************************
*  \brief        Grabs information for every initialized and uninitialized
*                device.

*   \param[out]  p_device  The device information returned.
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
LIB_API ni_retcode_t ni_rsrc_list_all_devices2(ni_device_t* p_device, bool list_uninitialized);

/*!*****************************************************************************
*  \brief        Print detailed capability information of all devices 
*                on the system.

*   \param       none
*
*   \return      none
*
*******************************************************************************/
LIB_API void ni_rsrc_print_all_devices_capability(void);

/*!*****************************************************************************
*  \brief        Prints detailed capability information for all initialized
*                devices and general information about uninitialized devices.

*   \param       list_uninitialized Flag to determine if uninitialized devices
*                                   should be grabbed.
*
*   \return      none
*
*******************************************************************************/
LIB_API void ni_rsrc_print_all_devices_capability2(bool list_uninitialized);

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
* \brief      Get GUID of the device by block device name and type
*
* \param[in]  blk_name   device's block name
* \param[in]  type       device type
*
* \return     device GUID (>= 0) if found, NI_RETCODE_FAILURE (-1) otherwise
*******************************************************************************/
LIB_API int ni_rsrc_get_device_by_block_name(const char *blk_name,
                                             ni_device_type_t device_type);

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
int ni_rsrc_update_device_load(ni_device_context_t* p_ctxt, int load,
    int sw_instance_cnt, const ni_sw_instance_info_t sw_instance_info[]);

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
);

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
LIB_API void ni_rsrc_release_resource(ni_device_context_t *p_ctxt,
                                      uint64_t load);

/*!*****************************************************************************
*   \brief      check the NetInt h/w device in resource pool on the host.
*
*   \param[in]  guid  the global unique device index in resource pool
*               device_type     NI_DEVICE_TYPE_DECODER or NI_DEVICE_TYPE_ENCODER
*
*   \return
*               NI_RETCODE_SUCCESS
*******************************************************************************/
LIB_API int ni_rsrc_check_hw_available(int guid, ni_device_type_t device_type);

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
*   \brief      Remove all NetInt h/w devices from resource pool on the host.
*
*   \param      none
*
*   \return
*               NI_RETCODE_SUCCESS
*               NI_RETCODE_FAILURE
*******************************************************************************/
LIB_API int ni_rsrc_remove_all_devices(void);

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
*               NI_RETCODE_INVALID_PARAM
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

/*!*****************************************************************************
*  \brief  check if device FW revision is compatible with SW API
*
*  \param fw_rev
*
*  \return 1 for full compatibility, 2 for partial, 0 for none
*******************************************************************************/
LIB_API int ni_rsrc_is_fw_compat(uint8_t fw_rev[8]);

/*!******************************************************************************
 *  \brief  Create a pointer to  hw_device_info_coder_param_t instance .This instance will be created and 
 *          set to default vaule by param mode.You may change the resolution fps bit_8_10 or other vaule you want to set.
 *  
 *  \param[in] mode:0:create instance with decoder_param ,encoder_param, scaler_param and ai_param will be set to NULL
 *                  1:create instance with encoder_param ,decoder_param, scaler_param and ai_param will be set to NULL
 *                  2:create instance with scaler_param ,decoder_param, encoder_param and ai_param will be set to NULL
 *                  3:create instance with ai_param ,decoder_param, encoder_param and scaler_param will be set to NULL
 *                  >= 4:create instance with decoder_param encoder_param scaler_param and ai_param for ni_check_hw_info() hw_mode
 * 
 *  \return  NULL-error,pointer to an instance when success
 *******************************************************************************/
LIB_API ni_hw_device_info_quadra_coder_param_t *ni_create_hw_device_info_quadra_coder_param(int mode);

/*!******************************************************************************
 *  \brief  Release a pointer to  hw_device_info_coder_param_t instance created by create_hw_device_info_coder_param
 *
 *  
 *  \param[in] p_hw_device_info_coder_param:pointer to a hw_device_info_coder_param_t instance created by create_hw_device_info_coder_param
 *                     
 *******************************************************************************/
LIB_API void ni_destory_hw_device_info_quadra_coder_param(ni_hw_device_info_quadra_coder_param_t *p_hw_device_info_quadra_coder_param);

/*!******************************************************************************
 *  \brief  Create a pointer to ni_hw_device_info_quadra_t instance .
 *  
 *  \param[in] device_type_num:number of device type to be allocated in this function
 *                 
 *  \param[in] avaliable_card_num:number of avaliable card per device to be allocated in this function
 * 
 *  \return  NULL-error,pointer to an instance when success
 *******************************************************************************/
LIB_API ni_hw_device_info_quadra_t *ni_hw_device_info_alloc_quadra(int device_type_num,int avaliable_card_num);

/*!******************************************************************************
 *  \brief  Release a pointer to  ni_hw_device_info_quadra_t instance created by create_hw_device_info_coder_param
 *
 *  
 *  \param[in] p_hw_device_info:pointer to a ni_hw_device_info_quadra_t instance created by create_hw_device_info_coder_param
 *                     
 *******************************************************************************/
LIB_API void ni_hw_device_info_free_quadra(ni_hw_device_info_quadra_t *p_hw_device_info);

/*!*****************************************************************************
 *  \brief  check hw info, return the appropriate card number to use depends on the load&task_num&used resource
 *
 *  \param[out] pointer_to_p_hw_device_info : pointer to user-supplied ni_hw_device_info_quadra_t (allocated by ni_hw_device_info_alloc).
 *                                            May be a ponter to NULL ,in which case a ni_hw_device_info_quadra_coder_param_t is allocated by this function
 *                                            and written to pointer_to_p_hw_device_info.
 *                                            record the device info, including  available card num and which card to select,
 *                                            and each card's informaton, such as, the load, task num, device type
 *  \param[in] task_mode: affect the scheduling strategy,
 *                        1 - both the load_num and task_num should consider, usually applied to live scenes
 *                        0 - only consider the task_num, don not care the load_num
 *  \param[in] hw_info_threshold_param : an array of threshold including device type task threshold and load threshold
 *                                       in hw_mode fill the arry with both encoder and decoder threshold or 
 *                                       fill the arry with preferential device type threshold when don not in hw_mode
 *                                       load threshold in range[0:100] task num threshold in range [0:32]
 *  \param[in] preferential_device_type : which device type is preferential 0:decode 1:encode .
 *                                        This need to set to encoder/decoder even if in sw_mode to check whether coder_param is wrong.
 *  \param[in] coder_param : encoder and decoder information that helps to choose card .This coder_param can be created and 
 *                           set to default value by function hw_device_info_coder_param_t * create_hw_device_info_coder_param().
 *                           You may change the resolution fps bit_8_10 or other vaule you want to use
 *  \param[in] hw_mode:Set 1 then this function will choose encoder and decoder in just one card .
 *                     When no card meets the conditions ,NO card will be choosed.
 *                     You can try to use set hw_mode 0 to use sw_mode to do encoder/decoder in different card when hw_mode reports an error
 *                     In hw_mode set both encoder_param and decoder_param in coder_param.
 *                     Set 0 then just consider sw_mode to choose which card to do encode/decode,
 *                     In sw_mode set one param in coder_param the other one will be set to NULL.
 *  \param[in] consider_mem : set 1 this function will consider memory usage extra
 *                            set 0 this function will not consider memory usage
 *
 *  \return  0-error 1-success
 *******************************************************************************/
LIB_API int ni_check_hw_info(ni_hw_device_info_quadra_t **pointer_to_p_hw_device_info, 
                             int task_mode,
                             ni_hw_device_info_quadra_threshold_param_t *hw_info_threshold_param,
                             ni_device_type_t preferential_device_type,
                             ni_hw_device_info_quadra_coder_param_t * coder_param,
                             int hw_mode,
                             int consider_mem);


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
                                                    uint64_t *p_load);

#ifdef __cplusplus
}
#endif
