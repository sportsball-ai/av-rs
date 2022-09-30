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
*  \file   ni_defs_logan.h
*
*  \brief  Common NETINT definitions used by all modules
*
*******************************************************************************/

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#include <process.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <linux/types.h>
#endif
#include <pthread.h>
#include <sys/time.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

// REVISION = SW_VERSION + "R" + API_FLAVOR + API_VER
#define NI_LOGAN_XCODER_REVISION        "310R2013"
#define NI_LOGAN_XCODER_FW_VER_SUPPORTED_MIN     "300"
#define NI_LOGAN_XCODER_FW_API_FLAVORS_SUPPORTED "20" // Should match one API flavor in NI_LOGAN_XCODER_REVISION
#define NI_LOGAN_XCODER_FW_API_VER_SUPPORTED_MIN 13

#define NI_LOGAN_XCODER_VER_SZ                   3
#define NI_LOGAN_XCODER_API_FLAVOR_SZ            2
#define NI_LOGAN_XCODER_API_VER_SZ               2

#ifndef QUADRA

#define NI_LITTLE_ENDIAN_PLATFORM 0  /*!!< platform is little endian */
#define NI_BIG_ENDIAN_PLATFORM    1  /*!!< platform is big endian */

#ifndef PLATFORM_ENDIANESS
#define PLATFORM_ENDIANESS NI_LITTLE_ENDIAN_PLATFORM
#endif

#define NETINT_PCI_VENDOR_ID 0x1D82 /*!!< NETINT PCIe VENDOR ID */

#ifdef _WIN32
typedef struct _ni_pthread_t
{
    void *handle;
    void *(*start_routine)(void *arg);
    void *arg;
    void *rc;
} ni_pthread_t;
typedef CRITICAL_SECTION ni_pthread_mutex_t;
typedef CONDITION_VARIABLE ni_pthread_cond_t;
typedef void ni_pthread_attr_t;
typedef void ni_pthread_condattr_t;
typedef void ni_pthread_mutexattr_t;
typedef void ni_sigset_t;
typedef HANDLE ni_device_handle_t;
typedef HANDLE ni_event_handle_t;
typedef HANDLE ni_lock_handle_t;
typedef HANDLE sem_t;
#define NI_INVALID_DEVICE_HANDLE  (INVALID_HANDLE_VALUE)
#define NI_INVALID_EVENT_HANDLE   (NULL)                  // Failure to create an Event returns NULL
#define NI_INVALID_LOCK_HANDLE    (NULL)                 // Failure to create a Mutex returns NULL
#ifdef XCODER_DLL
  #ifdef LIB_EXPORTS
    #define LIB_API __declspec(dllexport)
  #else
    #define LIB_API __declspec(dllimport)
  #endif
#else
  #define LIB_API
#endif
#elif __linux__
typedef pthread_t ni_pthread_t;
typedef pthread_mutex_t ni_pthread_mutex_t;
typedef pthread_cond_t ni_pthread_cond_t;
typedef pthread_attr_t ni_pthread_attr_t;
typedef pthread_condattr_t ni_pthread_condattr_t;
typedef pthread_mutexattr_t ni_pthread_mutexattr_t;
typedef sigset_t ni_sigset_t;
typedef int32_t ni_device_handle_t;
typedef int32_t ni_event_handle_t;
typedef int32_t ni_lock_handle_t;
#define NI_INVALID_DEVICE_HANDLE (-1)
#define NI_INVALID_EVENT_HANDLE  (-1)
#define NI_INVALID_LOCK_HANDLE   (-1)
#define LIB_API
#define SYS_PARAMS_PREFIX_PATH          "/sys/block/"
#define SYS_PREFIX_SZ                   strlen(SYS_PARAMS_PREFIX_PATH)
#define KERNEL_NVME_MAX_SEG_PATH        "/queue/max_segments"
#define KERNEL_NVME_MAX_SEG_SZ_PATH     "/queue/max_segment_size"
#define KERNEL_NVME_MIN_IO_SZ_PATH      "/queue/minimum_io_size"
#define KERNEL_NVME_MAX_HW_SEC_KB_PATH  "/queue/max_hw_sectors_kb"
#define MIN_NVME_BLK_NAME_LEN           7 //has to be at least nvmeXnY
#define KERNEL_NVME_FILE_NAME_MAX_SZ    (SYS_PREFIX_SZ + strlen(KERNEL_NVME_MAX_SEG_SZ_PATH) + \
                                              MIN_NVME_BLK_NAME_LEN + 10)
#define DEFAULT_IO_TRANSFER_SIZE        520192
#define MAX_IO_TRANSFER_SIZE            3133440

#elif __APPLE__
typedef pthread_t ni_pthread_t;
typedef pthread_mutex_t ni_pthread_mutex_t;
typedef pthread_cond_t ni_pthread_cond_t;
typedef pthread_attr_t ni_pthread_attr_t;
typedef pthread_condattr_t ni_pthread_condattr_t;
typedef pthread_mutexattr_t ni_pthread_mutexattr_t;
typedef sigset_t ni_sigset_t;
typedef int32_t  ni_device_handle_t;
typedef int32_t  ni_event_handle_t;
typedef int32_t  ni_lock_handle_t;
#define NI_INVALID_DEVICE_HANDLE (-1)
#define NI_INVALID_EVENT_HANDLE  (-1)
#define NI_INVALID_LOCK_HANDLE   (-1)
#define LIB_API
#define MAX_IO_TRANSFER_SIZE            3133440
#endif /* _WIN32 */

// number of system last error
#ifdef _WIN32
#define NI_ERRNO  (GetLastError())
#else
#define NI_ERRNO  (errno)
#endif

#define NI_INVALID_HWID       (-1)
#define NI_INVALID_IO_SIZE    (0)

#endif // #ifndef QUADRA

#define LOGAN_MAX_DEVICE_CNT 256
#define NI_LOGAN_MAX_DEVICE_NAME_LEN 32

#define NI_LOGAN_MAX_PACKET_SZ                0x20000
#define NI_LOGAN_POLL_INTERVAL                (2 * 1000)

#define NI_LOGAN_MAX_NUM_DATA_POINTERS    4

#define NI_LOGAN_MAX_CONTEXTS_PER_HW_INSTANCE  32
#define NI_LOGAN_MAX_1080P_FPS                 240
#define NI_LOGAN_MAX_CONTEXTS_PER_DECODER_INSTANCE 4

#define NI_LOGAN_MAX_DEVICES_PER_HW_INSTANCE 4

#define NI_LOGAN_MAX_TX_SZ 0x4B00000

#define NI_LOGAN_MEM_PAGE_ALIGNMENT 0x200

// for pts values storage
#define NI_LOGAN_MAX_DEC_REJECT 1024
//DTS PTS FIFO size
#define NI_LOGAN_FIFO_SZ 1024

//PTS gap to signal to signal pts jump
#define NI_LOGAN_MAX_PTS_GAP 32

//PTS gap to signal to signal pts jump
#define NI_LOGAN_MAX_I_P_DIST 8

// invalid sei type
#define NI_LOGAN_INVALID_SEI_TYPE (-1)
// max length of custom sei
#define NI_LOGAN_MAX_CUSTOM_SEI_SZ 8192
// max count of custom sei per packet
#define NI_LOGAN_MAX_CUSTOM_SEI_CNT 10

//bytes size of meta data sent together with YUV: from f/w decoder to app
#define NI_LOGAN_FW_META_DATA_SZ  64
// size of meta data sent together with YUV data: from app to f/w encoder
#define NI_LOGAN_APP_ENC_FRAME_META_DATA_SIZE 64
// size of meta data sent together with bitstream: from f/w encoder to app
#define NI_LOGAN_FW_ENC_BITSTREAM_META_DATA_SIZE 40

//size of str_fw_API_ver
#define NI_LOGAN_XCODER_FW_API_VER_SZ (NI_LOGAN_XCODER_API_VER_SZ + 1)
// size of ui8_light_level_data
#define NI_LOGAN_LIGHT_LEVEL_DATA_SZ  5
// ui8_mdcv_max_min_lum_data size
#define NI_LOGAN_MDCV_LUM_DATA_SZ 9
typedef enum
{
  NI_LOGAN_Y_ELEMENT = 0,
  NI_LOGAN_U_ELEMENT = 1,
  NI_LOGAN_V_ELEMENT = 2,
  NI_LOGAN_META_ELEMENT = 3,
} ni_logan_device_element_index;

//for custom driver
#define NI_LOGAN_MAX_NAMESPACE           16
#define NI_LOGAN_MAX_PROFILE_NAME_LEN    128
#define NI_LOGAN_MAX_LEVEL_NAME_LEN      64
#define NI_LOGAN_MAX_ADDITIONAL_INFO_LEN 64

#if defined(LRETURN)
#undef LRETURN
#define LRETURN goto END
#undef END
#define END end
#else
#define LRETURN goto END
#define END end
#endif

#define ni_logan_assert(expression) assert(expression)

typedef enum
{
  NI_LOGAN_DEVICE_TYPE_DECODER = 0,
  NI_LOGAN_DEVICE_TYPE_ENCODER = 1,
  NI_LOGAN_DEVICE_TYPE_UPLOAD =  3,
} ni_logan_device_type_t;

// return number for init
// if init already, return this
#define NI_LOGAN_RETCODE_INIT_ALREADY (1)

typedef enum
{
  NI_LOGAN_RETCODE_SUCCESS                             =  0,    /*!!< successful return code */
  NI_LOGAN_RETCODE_FAILURE                             = -1,    /*!!< unrecoverable failure */
  NI_LOGAN_RETCODE_INVALID_PARAM                       = -2,    /*!!< invalid/uninitialized/null pointer parameter encountered */
  NI_LOGAN_RETCODE_ERROR_MEM_ALOC                      = -3,    /*!!< memory allocation failure */
  NI_LOGAN_RETCODE_ERROR_NVME_CMD_FAILED               = -4,    /*!!< nvme command failure */
  NI_LOGAN_RETCODE_ERROR_INVALID_SESSION               = -5,    /*!!< invalid session */
  NI_LOGAN_RETCODE_ERROR_RESOURCE_UNAVAILABLE          = -6,    /*!!< resource currently unavailable */
  NI_LOGAN_RETCODE_PARAM_INVALID_NAME                  = -7,    /*!!< invalid parameter name */
  NI_LOGAN_RETCODE_PARAM_INVALID_VALUE                 = -8,    /*!!< invalid parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_FRATE                   = -9,    /*!!< invalid frame rate parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_BRATE                   = -10,   /*!!< invalid bit rate parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_TRATE                   = -11,   /*!!< invalid bit rate parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_RC_INIT_DELAY           = -12,
  NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_PERIOD            = -13,   /*!!< invalid intra period parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_INTRA_QP                = -14,   /*!!< invalid qp parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_GOP_PRESET              = -15,   /*!!< invalid got preset parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CU_SIZE_MODE            = -16,
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_NUM_MERGE            = -17,
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_8X8_EN         = -18,
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_16X16_EN       = -19,
  NI_LOGAN_RETCODE_PARAM_ERROR_DY_MERGE_32X32_EN       = -20,
  NI_LOGAN_RETCODE_PARAM_ERROR_CU_LVL_RC_EN            = -21,   /*!!< invalid cu level rate control parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_EN               = -22,
  NI_LOGAN_RETCODE_PARAM_ERROR_HVS_QP_SCL              = -23,
  NI_LOGAN_RETCODE_PARAM_ERROR_MN_QP                   = -24,   /*!!< invalid minimum qp parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_QP                   = -25,   /*!!< invalid maximum qp parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_MX_DELTA_QP             = -26,   /*!!< invalid maximum delta qp parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_TOP            = -27,   /*!!< invalid top offset of conformance window parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_BOT            = -28,   /*!!< invalid bottom offset of conformance window parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_L              = -29,   /*!!< invalid left offset of conformance window parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CONF_WIN_R              = -30,   /*!!< invalid right offset of conformance window parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM       = -31,   /*!!< invalid user recommended parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_BRATE_LT_TRATE          = -32,
  NI_LOGAN_RETCODE_PARAM_ERROR_RCINITDELAY             = -33,
  NI_LOGAN_RETCODE_PARAM_ERROR_RCENABLE                = -34,
  NI_LOGAN_RETCODE_PARAM_ERROR_MAXNUMMERGE             = -35,
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSTOM_GOP              = -36,   /*!!< invalid custom gop preset parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_PIC_WIDTH               = -37,   /*!!< invalid picture width parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_PIC_HEIGHT              = -38,   /*!!< invalid picture height parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE   = -39,   /*!!< invalid decoding refresh type parameter value */
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN      = -40,
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN    = -41,
  NI_LOGAN_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN    = -42,
  NI_LOGAN_RETCODE_PARAM_ERROR_TOO_BIG                 = -43,    /*!!< parameter value is too big */
  NI_LOGAN_RETCODE_PARAM_ERROR_TOO_SMALL               = -44,    /*!!< parameter value is too small */
  NI_LOGAN_RETCODE_PARAM_ERROR_ZERO                    = -45,    /*!!< parameter value is zero */
  NI_LOGAN_RETCODE_PARAM_ERROR_OOR                     = -46,    /*!!< parameter value is out of range */
  NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG           = -47,    /*!!< parameter width value is too big */
  NI_LOGAN_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL         = -48,    /*!!< parameter width value is too small */
  NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG          = -49,    /*!!< parameter height value is too big */
  NI_LOGAN_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL        = -50,    /*!!< parameter height value is too small */
  NI_LOGAN_RETCODE_PARAM_ERROR_AREA_TOO_BIG            = -51,    /*!!< parameter heightxwidth value is too big */
  NI_LOGAN_RETCODE_ERROR_EXCEED_MAX_NUM_SESSIONS       = -52,    /*!!< exceeding the max number of 64 sessions */
  NI_LOGAN_RETCODE_ERROR_GET_DEVICE_POOL               = -53,    /*!!< cannot get info from device */
  NI_LOGAN_RETCODE_ERROR_LOCK_DOWN_DEVICE              = -54,    /*!!< cannot obtain the file lock across all the process for query */
  NI_LOGAN_RETCODE_ERROR_UNLOCK_DEVICE                 = -55,    /*!!< cannot unlock the lock */
  NI_LOGAN_RETCODE_ERROR_OPEN_DEVICE                   = -56,    /*!!< cannot open the device */
  NI_LOGAN_RETCODE_ERROR_INVALID_HANDLE                = -57,    /*!!< the handles that passed in is wrong */
  NI_LOGAN_RETCODE_ERROR_INVALID_ALLOCATION_METHOD     = -58,    /*!!< the handles that passed in is wrong */
  NI_LOGAN_RETCODE_ERROR_VPU_RECOVERY                  = -59,    /*!!< VPU in recovery mode */
  NI_LOGAN_RETCODE_PARAM_GOP_INTRA_INCOMPATIBLE        = -60,    /*!!< parameter Intra period incompatible with GOP structure */

  /*!!< nvme device write buffer is full: not returned by fw but defined for
    use to track write-buffer full condition */
  NI_LOGAN_RETCODE_NVME_SC_WRITE_BUFFER_FULL           = 0x200,
  /*! Hardware Specific Error codes */
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE         = 0x301,
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_IS_EMPTY            = 0x302, /*!!< Invalid resource, recommand application termination */
  NI_LOGAN_RETCODE_NVME_SC_RESOURCE_NOT_FOUND           = 0x303, /*!!< Invalid resource, recommand application termination */
  NI_LOGAN_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED        = 0x304,
  NI_LOGAN_RETCODE_NVME_SC_REQUEST_IN_PROGRESS          = 0x305, /*!!< Request in progress, recommend application retry */
  NI_LOGAN_RETCODE_NVME_SC_INVALID_PARAMETER            = 0x306,
  NI_LOGAN_RETCODE_NVME_SC_VPU_RECOVERY                 = 0x3FD,
  NI_LOGAN_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT        = 0x3FE, /*!!< Insufficient resource, recommend application termination */
  NI_LOGAN_RETCODE_NVME_SC_VPU_GENERAL_ERROR            = 0x3FF, /*!!< General VPU error, recommend application termination */

  /*! Xcoder Software Specific Error codes */
  NI_LOGAN_RETCODE_DEFAULT_SESSION_ERR_NO               = 0x4E49,/*!!< The default of session error, 0x4E49 is the ASCII of "NI" */
} ni_logan_retcode_t;


#ifndef QUADRA
typedef enum _ni_nvme_opcode
{
  nvme_cmd_flush = 0x00,
  nvme_cmd_write = 0x01,
  nvme_cmd_read = 0x02,
  nvme_cmd_write_uncor = 0x04,
  nvme_cmd_compare = 0x05,
  nvme_cmd_write_zeroes = 0x08,
  nvme_cmd_dsm = 0x09,
  nvme_cmd_resv_register = 0x0d,
  nvme_cmd_resv_report = 0x0e,
  nvme_cmd_resv_acquire = 0x11,
  nvme_cmd_resv_release = 0x15,
  nvme_cmd_xcoder_write = 0x83,
  nvme_cmd_xcoder_read = 0x84
} ni_nvme_opcode_t;
#endif // #ifndef QUADRA

typedef struct _ni_logan_nvme_command_t
{
  uint32_t cdw2;
  uint32_t cdw3;
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} ni_logan_nvme_command_t;

typedef struct _ni_logan_lat_meas_q_entry_t
{
  uint64_t abs_timenano;
  int64_t  ts_time;
} ni_logan_lat_meas_q_entry_t;

typedef struct _ni_logan_lat_meas_q_t
{
  int front, rear, size;
  unsigned capacity;
  ni_logan_lat_meas_q_entry_t* array;
} ni_logan_lat_meas_q_t;

typedef struct _ni_logan_instance_status_info
{
  uint16_t sess_err_no;    // See SYSTEM_MANAGER_ERROR_CODES definition
  uint16_t sess_rsvd;      // session reserved
  uint16_t inst_state;     // instance current state
  uint16_t inst_err_no;    // instance error return code: 0 normal, > 0 error
  uint32_t wr_buf_avail_size; // instance write buffer size available
  uint32_t rd_buf_avail_size; // instance read  buffer size available
  uint64_t sess_timestamp;    // session start timestamp
  uint32_t frames_input;
  uint32_t frames_buffered;
  uint32_t frames_completed;
  uint32_t frames_output;
  uint32_t frames_dropped;
  uint32_t inst_errors;
} ni_logan_instance_status_info_t; // 48 bytes (has to be 8 byte aligned)

#ifdef __cplusplus
}
#endif
