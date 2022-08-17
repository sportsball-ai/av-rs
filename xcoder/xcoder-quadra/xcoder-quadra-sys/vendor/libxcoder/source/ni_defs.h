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
*  \file   ni_defs.h
*
*  \brief  Common NETINT definitions used by all modules
*
*******************************************************************************/

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <malloc.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <linux/types.h>
#endif
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ni_release_info.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(NI_WARN_AS_ERROR) && !defined(__clang__) && defined(__GNUC__)
    #define NI_DEPRECATED __attribute__((deprecated))
    #define NI_DEPRECATED_MACRO                                                \
        _Pragma("GCC warning \"A Netint macro on this line is deprecated\"")
    #define NI_DEPRECATE_MACRO(X)
#elif !defined(NI_WARN_AS_ERROR) && !defined(__clang__) && defined(_MSC_VER)
    #define NI_DEPRECATED __declspec(deprecated)
    #define NI_DEPRECATED_MACRO
    #define NI_DEPRECATE_MACRO(X) __pragma(deprecated(X))
#else
    #define NI_DEPRECATED
    #define NI_DEPRECATED_MACRO
    #define NI_DEPRECATE_MACRO(X)
#endif

// NI_XCODER_REVISION can be read to determine release package version and FW to
// SW compatibility. Recommend using ni_get_*_ver() functions in ni_util.h to
// read correct version numbers if updating libxcoder but not linked apps.
// NI_XCODER_REVISION[0:2] = SW release version
// NI_XCODER_REVISION[3]   = compatible FW API semantic major version
// NI_XCODER_REVISION[4]   = compatible FW API semantic minor version
// NI_XCODER_REVISION[5:7] = optional
// reference: https://netint.atlassian.net/l/c/woqFMHES
#define NI_XCODER_REVISION "40064rcB"
#define NI_XCODER_REVISION_API_MAJOR_VER_IDX 3
#define NI_XCODER_REVISION_API_MINOR_VER_IDX 4

// LIBXCODER_API_VERSION can be read to determine libxcoder to linked apps/APIs
// compatibility. Recommend using ni_get_*_ver() functions in ni_util.h to
// read correct version numbers if updating libxcoder but not linked apps.
// reference: https://netint.atlassian.net/l/c/fVEGmYEZ
#define MACRO_TO_STR(s) #s
#define MACROS_TO_VER_STR(a, b) MACRO_TO_STR(a.b)
#define LIBXCODER_API_VERSION_MAJOR 2   // Libxcoder API semantic major version
#define LIBXCODER_API_VERSION_MINOR 4   // Libxcoder API semantic minor version
#define LIBXCODER_API_VERSION MACROS_TO_VER_STR(LIBXCODER_API_VERSION_MAJOR, \
                                                LIBXCODER_API_VERSION_MINOR)

#define NI_LITTLE_ENDIAN_PLATFORM 0  /*!!< platform is little endian */
#define NI_BIG_ENDIAN_PLATFORM    1  /*!!< platform is big endian */

#define QUADRA  1

#ifndef PLATFORM_ENDIANESS
#define PLATFORM_ENDIANESS NI_LITTLE_ENDIAN_PLATFORM
#endif

#define NETINT_PCI_VENDOR_ID 0x1D82 /*!!< NETINT PCIe VENDOR ID */

#ifdef _WIN32
#else
#endif

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
#define NI_INVALID_DEVICE_HANDLE (INVALID_HANDLE_VALUE)
#define NI_INVALID_EVENT_HANDLE (NULL)
#define NI_INVALID_LOCK_HANDLE (NULL)
#ifdef XCODER_DLL
#ifdef LIB_EXPORTS
#define LIB_API __declspec(dllexport)
#else
#define LIB_API __declspec(dllimport)
#endif
#else
#define LIB_API
#endif

#define NI_MAX_FRAME_CHUNK_SZ 0x80000   //0x10000
#define NI_MAX_PACKET_SZ 0x20000        //0x10000
#define NI_MAX_ENC_PACKET_SZ 3133440

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
#define NI_INVALID_EVENT_HANDLE (-1)
#define NI_INVALID_LOCK_HANDLE (-1)
#define LIB_API

#define SYS_PARAMS_PREFIX_PATH "/sys/block/"
#define SYS_PREFIX_SZ strlen(SYS_PARAMS_PREFIX_PATH)
#define KERNEL_NVME_MAX_SEG_PATH "/queue/max_segments"
#define KERNEL_NVME_MAX_SEG_SZ_PATH "/queue/max_segment_size"
#define KERNEL_NVME_MIN_IO_SZ_PATH "/queue/minimum_io_size"
#define KERNEL_NVME_MAX_HW_SEC_KB_PATH "/queue/max_hw_sectors_kb"
#define MIN_NVME_DEV_NAME_LEN 7   //has to be at least nvmeXnY
#define KERNEL_NVME_FILE_NAME_MAX_SZ                                           \
    (SYS_PREFIX_SZ + strlen(KERNEL_NVME_MAX_SEG_SZ_PATH) +                     \
     MIN_NVME_DEV_NAME_LEN + 10)
#define DEFAULT_IO_TRANSFER_SIZE 520192
#define MAX_IO_TRANSFER_SIZE 3133440

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

#define NI_MAX_DEVICE_CNT      128
#define NI_MAX_DEVICE_NAME_LEN 32

#define NI_MAX_PACKET_SZ                0x20000
#define NI_POLL_INTERVAL                (2 * 1000)

#define NI_MAX_NUM_DATA_POINTERS    4

#define NI_MAX_CONTEXTS_PER_HW_INSTANCE 128
#define NI_MAX_4K_FPS_QUADRA 240

#define NI_MAX_DEVICES_PER_HW_INSTANCE 4

#define NI_MAX_NUM_OF_DECODER_OUTPUTS 3

#define NI_MAX_PPU_PARAM_EXPR_CHAR 20

#define NI_MAX_TX_SZ 0xA00000

#define NI_MEM_PAGE_ALIGNMENT 0x1000

#define NI_MAX_DR_HWDESC_FRAME_INDEX 5363
  // If P2P area changes in firmware these constants must be updated
#define NI_MIN_HWDESC_P2P_BUF_ID 5364
#define NI_MAX_HWDESC_P2P_BUF_ID 5525

#define NI_MAX_SR_HWDESC_FRAME_INDEX 2457
  // If P2P area changes in firmware these constants must be updated
#define NI_MIN_SR_HWDESC_P2P_BUF_ID 2458
#define NI_MAX_SR_HWDESC_P2P_BUF_ID 2619

  //Feed this ni_session_context_t->ddr_config
#define NI_GET_MIN_HWDESC_P2P_BUF_ID(x)  (x==1?NI_MIN_SR_HWDESC_P2P_BUF_ID:NI_MIN_HWDESC_P2P_BUF_ID)
#define NI_GET_MAX_HWDESC_P2P_BUF_ID(x)  (x==1?NI_MAX_SR_HWDESC_P2P_BUF_ID:NI_MAX_HWDESC_P2P_BUF_ID)

//use NI_MAX_DR_HWDESC_FRAME_INDEX or NI_GET_MAX_HWDESC_FRAME_INDEX
NI_DEPRECATE_MACRO(NI_MAX_HWDESC_FRAME_INDEX) 
#define NI_MAX_HWDESC_FRAME_INDEX NI_DEPRECATED_MACRO NI_MAX_DR_HWDESC_FRAME_INDEX

//input param is DDR config of target device
#define NI_GET_MAX_HWDESC_FRAME_INDEX(x) (x==1?NI_MAX_SR_HWDESC_FRAME_INDEX:NI_MAX_DR_HWDESC_FRAME_INDEX)

#define NI_MAX_UPLOAD_INSTANCE_FRAMEPOOL 100

  // for pts values storage
#define NI_MAX_DEC_REJECT 1024
  //DTS PTS FIFO size
#define NI_FIFO_SZ 1024

//PTS gap to signal to signal pts jump
#define NI_MAX_PTS_GAP 32

//PTS gap to signal to signal pts jump
#define NI_MAX_I_P_DIST 8

// invalid sei type
#define NI_INVALID_SEI_TYPE (-1)

// max count of custom sei per packet
#define NI_MAX_CUSTOM_SEI_CNT 10

//bytes size of meta data sent together with YUV: from f/w decoder to app
#define NI_FW_META_DATA_SZ  104 //metadataDecFrame on dec FW (including hwdesc x3)
// size of meta data sent together with YUV data: from app to f/w encoder
#define NI_APP_ENC_FRAME_META_DATA_SIZE 56
// size of meta data sent together with bitstream: from f/w encoder to app
#define NI_FW_ENC_BITSTREAM_META_DATA_SIZE 32 //might need to edit for yuvbypass quadra

#define MAX_AV1_ENCODER_GOP_NUM 8

#if defined(LRETURN)
#undef LRETURN
#define LRETURN goto end
#undef END
#define END end
#else
#define LRETURN goto end
#define END end
#endif

#define ni_assert(expression) assert(expression)

typedef enum _ni_xcoder_prod_line
{
    NI_XCODER_NONE = 0,
    NI_XCODER_LOGAN = 1,
    NI_XCODER_QUADRA = 2,
} ni_xcoder_prod_line_t;

inline int is_supported_xcoder(int x)
{
    return (NI_XCODER_QUADRA == x);
}

  typedef enum
  {
      NI_DEVICE_TYPE_MIN = -1,

      // xcoder instance types
      NI_DEVICE_TYPE_DECODER = 0,
      NI_DEVICE_TYPE_ENCODER = 1,
      NI_DEVICE_TYPE_SCALER = 2,
      NI_DEVICE_TYPE_AI = 3,
      NI_DEVICE_TYPE_XCODER_MAX = 4,

      // pseudo types
      NI_DEVICE_TYPE_UPLOAD = 4,   // share instance with NI_DEVICE_TYPE_ENCODER

      NI_DEVICE_TYPE_MAX = 5,
  } ni_device_type_t;

#define IS_XCODER_DEVICE_TYPE(t)                                               \
    (t > NI_DEVICE_TYPE_MIN && t < NI_DEVICE_TYPE_XCODER_MAX)
#define GET_XCODER_DEVICE_TYPE(t)                                              \
    (t == NI_DEVICE_TYPE_UPLOAD ? NI_DEVICE_TYPE_ENCODER : t)

  static const char *device_type_str[] = {"decoder", "encoder", "scaler", "AI"};
  static const char device_type_chr[] = {'d', 'e', 's', 'a'};

  typedef enum
  {
      NI_RETCODE_SUCCESS = 0,  /*!!< successful return code */
      NI_RETCODE_FAILURE = -1, /*!!< unrecoverable failure */
      NI_RETCODE_INVALID_PARAM =
          -2, /*!!< invalid/uninitialized/null pointer parameter encountered */
      NI_RETCODE_ERROR_MEM_ALOC = -3,        /*!!< memory allocation failure */
      NI_RETCODE_ERROR_NVME_CMD_FAILED = -4, /*!!< nvme command failure */
      NI_RETCODE_ERROR_INVALID_SESSION = -5, /*!!< invalid session */
      NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE =
          -6, /*!!< resource currently unavailable */
      NI_RETCODE_PARAM_INVALID_NAME = -7,  /*!!< invalid parameter name */
      NI_RETCODE_PARAM_INVALID_VALUE = -8, /*!!< invalid parameter value */
      NI_RETCODE_PARAM_ERROR_FRATE =
          -9, /*!!< invalid frame rate parameter value */
      NI_RETCODE_PARAM_ERROR_BRATE =
          -10, /*!!< invalid bit rate parameter value */
      NI_RETCODE_PARAM_ERROR_TRATE =
          -11, /*!!< invalid bit rate parameter value */
      NI_RETCODE_PARAM_ERROR_VBV_BUFFER_SIZE = -12,
      NI_RETCODE_PARAM_ERROR_INTRA_PERIOD =
          -13, /*!!< invalid intra period parameter value */
      NI_RETCODE_PARAM_ERROR_INTRA_QP = -14, /*!!< invalid qp parameter value */
      NI_RETCODE_PARAM_ERROR_GOP_PRESET =
          -15, /*!!< invalid got preset parameter value */
      NI_RETCODE_PARAM_ERROR_CU_SIZE_MODE = -16,
      NI_RETCODE_PARAM_ERROR_MX_NUM_MERGE = -17,
      NI_RETCODE_PARAM_ERROR_DY_MERGE_8X8_EN = -18,
      NI_RETCODE_PARAM_ERROR_DY_MERGE_16X16_EN = -19,
      NI_RETCODE_PARAM_ERROR_DY_MERGE_32X32_EN = -20,
      NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN =
          -21, /*!!< invalid cu level rate control parameter value */
      NI_RETCODE_PARAM_ERROR_HVS_QP_EN = -22,
      NI_RETCODE_PARAM_ERROR_HVS_QP_SCL = -23,
      NI_RETCODE_PARAM_ERROR_MN_QP =
          -24, /*!!< invalid minimum qp parameter value */
      NI_RETCODE_PARAM_ERROR_MX_QP =
          -25, /*!!< invalid maximum qp parameter value */
      NI_RETCODE_PARAM_ERROR_MX_DELTA_QP =
          -26, /*!!< invalid maximum delta qp parameter value */
      NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP =
          -27, /*!!< invalid top offset of conformance window parameter value */
      NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT =
          -28, /*!!< invalid bottom offset of conformance window parameter value */
      NI_RETCODE_PARAM_ERROR_CONF_WIN_L =
          -29, /*!!< invalid left offset of conformance window parameter value */
      NI_RETCODE_PARAM_ERROR_CONF_WIN_R =
          -30, /*!!< invalid right offset of conformance window parameter value */
      NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM =
          -31, /*!!< invalid user recommended parameter value */
      NI_RETCODE_PARAM_ERROR_BRATE_LT_TRATE = -32,
      NI_RETCODE_PARAM_ERROR_RCENABLE = -33,
      NI_RETCODE_PARAM_ERROR_MAXNUMMERGE = -34,
      NI_RETCODE_PARAM_ERROR_CUSTOM_GOP =
          -35, /*!!< invalid custom gop preset parameter value */
      NI_RETCODE_PARAM_ERROR_PIC_WIDTH =
          -36, /*!!< invalid picture width parameter value */
      NI_RETCODE_PARAM_ERROR_PIC_HEIGHT =
          -37, /*!!< invalid picture height parameter value */
      NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE =
          -38, /*!!< invalid decoding refresh type parameter value */
      NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN = -39,
      NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN = -40,
      NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN = -41,
      NI_RETCODE_PARAM_ERROR_TOO_BIG = -42, /*!!< parameter value is too big */
      NI_RETCODE_PARAM_ERROR_TOO_SMALL =
          -43,                           /*!!< parameter value is too small */
      NI_RETCODE_PARAM_ERROR_ZERO = -44, /*!!< parameter value is zero */
      NI_RETCODE_PARAM_ERROR_OOR = -45, /*!!< parameter value is out of range */
      NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG =
          -46, /*!!< parameter width value is too big */
      NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL =
          -47, /*!!< parameter width value is too small */
      NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG =
          -48, /*!!< parameter height value is too big */
      NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL =
          -49, /*!!< parameter height value is too small */
      NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG =
          -50, /*!!< parameter heightxwidth value is too big */
      NI_RETCODE_ERROR_EXCEED_MAX_NUM_SESSIONS =
          -51, /*!!< exceeding the max number of 64 sessions */
      NI_RETCODE_ERROR_GET_DEVICE_POOL =
          -52, /*!!< cannot get info from device */
      NI_RETCODE_ERROR_LOCK_DOWN_DEVICE =
          -53, /*!!< cannot obtain the file lock across all the process for query */
      NI_RETCODE_ERROR_UNLOCK_DEVICE = -54, /*!!< cannot unlock the lock */
      NI_RETCODE_ERROR_OPEN_DEVICE = -55,   /*!!< cannot open the device */
      NI_RETCODE_ERROR_INVALID_HANDLE =
          -56, /*!!< the handles that passed in is wrong */
      NI_RETCODE_ERROR_INVALID_ALLOCATION_METHOD =
          -57, /*!!< the handles that passed in is wrong */
      NI_RETCODE_ERROR_VPU_RECOVERY = -58, /*!!< VPU in recovery mode */
      NI_RETCODE_PARAM_WARNING_DEPRECATED = -59,
      NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH =
          -60, /*!!< invalid lookahead depth */
      NI_RETCODE_PARAM_ERROR_FILLER = -61,
      NI_RETCODE_PARAM_ERROR_PICSKIP = -62,
      NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION = -63,

      NI_RETCODE_PARAM_WARN =
          0x100, /*!!<Just a warning to print, safe to continue */

      /*!!< nvme device write buffer is full: not returned by fw but defined for
    use to track write-buffer full condition */
      NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL = 0x200,
      /*! Hardware Specific Error codes */
      NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE = 0x301,
      NI_RETCODE_NVME_SC_RESOURCE_IS_EMPTY =
          0x302, /*!!< Invalid resource, recommand application termination */
      NI_RETCODE_NVME_SC_RESOURCE_NOT_FOUND =
          0x303, /*!!< Invalid resource, recommand application termination */
      NI_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED = 0x304,
      NI_RETCODE_NVME_SC_REQUEST_IN_PROGRESS =
          0x305, /*!!< Request in progress, recommend application retry */
      NI_RETCODE_NVME_SC_INVALID_PARAMETER = 0x306,
      NI_RETCODE_NVME_SC_VPU_RECOVERY = 0x3FD,
      NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT =
          0x3FE, /*!!< Insufficient resource, recommend application termination */
      NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR =
          0x3FF, /*!!< General VPU error, recommend application termination */
  } ni_retcode_t;

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

typedef enum _ni_scaler_opcode
{
  NI_SCALER_OPCODE_SCALE   = 0,
  NI_SCALER_OPCODE_CROP    = 1,
  NI_SCALER_OPCODE_PAD     = 2,
  NI_SCALER_OPCODE_OVERLAY = 3,
  NI_SCALER_OPCODE_STACK   = 4
} ni_scaler_opcode_t;

#ifdef __cplusplus
}
#endif

