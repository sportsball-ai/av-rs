/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_util.h
*
*  \brief  Exported utility routines definition
*
*******************************************************************************/

#pragma once

#ifdef _WIN32
#define _SC_PAGESIZE 8
#define sysconf(x)   4096
#ifdef LIBXCODER_OBJS_BUILD
#include "../build/xcoder_auto_headers.h"
#endif

#elif __linux__
#include <linux/types.h>
#include <sys/time.h>
#ifdef LIBXCODER_OBJS_BUILD
#include "../build/xcoder_auto_headers.h"
#endif
#endif

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include <inttypes.h>
#include "ni_defs.h"
#include "ni_device_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef XCODER_LINUX_CUSTOM_DRIVER
#define NI_NVME_PREFIX "ninvme"
#define NI_NVME_PREFIX_SZ 6
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
#define NI_NVME_PREFIX "vd"
#define NI_NVME_PREFIX_SZ 2
#else
#define NI_NVME_PREFIX "nvme"
#define NI_NVME_PREFIX_SZ 4
#endif

#define XCODER_MAX_NUM_TS_TABLE 32
#define XCODER_FRAME_OFFSET_DIFF_THRES 100
#define XCODER_MAX_ENC_PACKETS_PER_READ 16
#define XCODER_MAX_NUM_QUEUE_ENTRIES 6000
#define XCODER_MAX_NUM_TEMPORAL_LAYER 7
#define BUFFER_POOL_SZ_PER_CONTEXT 300

// for _T400_ENC
#define XCODER_MIN_ENC_PIC_WIDTH 256
#define XCODER_MIN_ENC_PIC_HEIGHT 128
#define XCODER_MAX_ENC_PIC_WIDTH 8192
#define XCODER_MAX_ENC_PIC_HEIGHT 8192

#define NI_DEC_FRAME_BUF_POOL_SIZE_INIT   20
#define NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND 20

#define NI_VPU_FREQ  450  //450Mhz

#define MAX_THREADS 1000

#ifdef XCODER_SIM_ENABLED
extern int32_t sim_eos_flag;
#endif

typedef enum
{
  NI_LOG_NONE  = 0,
  NI_LOG_FATAL = 1,
  NI_LOG_ERROR = 2,
  NI_LOG_INFO  = 3,
  NI_LOG_DEBUG = 4,
  NI_LOG_TRACE = 5
} ni_log_level_t;

extern LIB_API ni_log_level_t ni_log_level;

typedef struct _ni_queue_t
{
  char name[32];
  uint32_t count;
  ni_queue_node_t *p_first;
  ni_queue_node_t *p_last;

} ni_queue_t;

typedef struct _ni_timestamp_table_t
{
  ni_queue_t list;

} ni_timestamp_table_t;

typedef struct task
{
  void *(*run)(void *args); //process function
  void *arg;                //param
  struct task *next;        //the next task of the queue
} task_t;

typedef struct threadpool
{
  pthread_mutex_t pmutex; //mutex
  pthread_cond_t pcond;   //cond
  task_t *first;          //the first task of the queue
  task_t *last;           //the last task of the queue
  int counter;            //current thread number of the pool
  int idle;               //current idle thread number of the pool
  int max_threads;        //The max thread number of the pool
  int quit;               //exit sign
} threadpool_t;


/*!*************************************/
// libxcoder logging utility
/*!******************************************************************************
 *  \brief Print log message to stderr depending on verbosity level.
 *
 *  \param[in] level  log level
 *  \param[in] format fprintf format specifier
 *  \param[in] ...    additional arguments
 *
 *  \return
 *******************************************************************************/
#define ni_log(level, format, ...)                      \
{                                                      \
  if (level <= ni_log_level)                            \
  {                                                     \
    if (level == NI_LOG_TRACE)                          \
    {                                                   \
      fprintf(stderr, "[%" PRId64 "] ", ni_get_utime());\
    }                                                   \
    fprintf(stderr, format, ##__VA_ARGS__);             \
  }                                                     \
} // note, this is defined as a macro function in header instead of regular function as va_list does not support printing literals

/*!******************************************************************************
 *  \brief  Set ni_log_level
 *
 *  \param  level log level
 *
 *  \return
 *******************************************************************************/
LIB_API void ni_log_set_level(ni_log_level_t level);

/*!******************************************************************************
 *  \brief Get ni_log_level
 *
 *  \return ni_log_level
 *******************************************************************************/
LIB_API ni_log_level_t ni_log_get_level(void);

/*!******************************************************************************
 *  \brief Convert ffmpeg log level integer to appropriate ni_log_level_t
 *
 *  \param fflog_level integer representation of FFmpeg log level
 *
 *  \return ni_log_level
 *******************************************************************************/
ni_log_level_t ff_to_ni_log_level(int fflog_level);

// memory buffer pool operations (one use is for decoder frame buffer pool)

/*!******************************************************************************
 *  \brief get a free memory buffer from the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_buf_t *ni_buf_pool_get_buffer(ni_buf_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief return a used memory buffer to the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_buf_pool_return_buffer(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief allocate a memory buffer and place it in the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_buf_t *ni_buf_pool_allocate_buffer(ni_buf_pool_t *p_buffer_pool, int buffer_size);

// decoder frame buffer pool init & free

/*!******************************************************************************
 *  \brief decoder frame buffer pool init & free
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_dec_fme_buffer_pool_initialize(ni_session_context_t* p_ctx, int32_t number_of_buffers, int width, int height, int height_align, int factor);

/*!******************************************************************************
 *  \brief free decoder frame buffer pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_dec_fme_buffer_pool_free(ni_buf_pool_t *p_buffer_pool);

// timestamp buffer pool operations

/*!******************************************************************************
 *  \brief free buffer memory pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_buffer_pool_free(ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Find NVMe name space block from device name
 *          If none is found, assume nvme multi-pathing is disabled and return /dev/nvmeXn1
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[in] out_buf Output buffer to put NVMe name space block. Must be at least length 21
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_find_blk_name(const char *p_dev, char *p_out_buf, int out_buf_len);

/*!******************************************************************************
 *  \brief  Initialize timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_init(ni_session_context_t* p_ctx, ni_timestamp_table_t **pp_table, const char *name);

/*!******************************************************************************
 *  \brief  Clean up timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_done(ni_timestamp_table_t *p_table, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Register timestamp in timestamp/frame offset table
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_register(ni_queue_buffer_pool_t *p_buffer_pool, ni_timestamp_table_t *p_table, int64_t timestamp, uint64_t data_info);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get_v2(ni_timestamp_table_t *p_table, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info with respect
 *  to threshold
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get_with_threshold(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief Timestamp queue clean up
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_timestamp_scan_cleanup(ni_timestamp_table_t *pts_list,
                                   ni_timestamp_table_t *dts_list,
                            ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Initialize xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_init(ni_session_context_t* p_ctx, ni_queue_t *p_queue, const char *name);

/*!******************************************************************************
 *  \brief  Push into xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_push(ni_queue_buffer_pool_t *p_buffer_pool, ni_queue_t *p_queue, uint64_t frame_offset, int64_t timestamp);

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_pop(ni_queue_t *p_queue, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue with respect to threshold
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_pop_threshold(ni_queue_t *p_queue, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Free xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_free(ni_queue_t *p_queue, ni_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Print xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_print(ni_queue_t *p_queue);

// Type conversion functions
/*!******************************************************************************
 *  \brief  convert string to boolean
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_atobool(const char *p_str, bool b_error);

/*!******************************************************************************
 *  \brief  convert string to integer
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_atoi(const char *p_str, bool b_error);

/*!******************************************************************************
 *  \brief  convert string to float
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
double ni_atof(const char *p_str, bool b_error);

/*!******************************************************************************
 *  \brief  string parser
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_parse_name(const char *arg, const char *const *names, bool b_error);

/*!******************************************************************************
 *  \brief  get time in microseconds
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
LIB_API uint64_t ni_get_utime(void);

/*!*****************************************************************************
 *  \brief   Init the threadpool
 *
 *  \param[in]   pool  threadpool address
 *
 *  \return      NULL
 *
 *
 ******************************************************************************/
void threadpool_init(threadpool_t *pool);


/*!*****************************************************************************
 *  \brief   add task to threadpool
 *
 *  \param[in]   pool  threadpool address
 *  \param[in]   run   run function
 *  \param[in]   arg   run function params
 *
 *  \return      0     success
 *              <0     failed
 *
 ******************************************************************************/
int threadpool_add_task(threadpool_t *pool, void *(*run)(void *arg), void *arg);


/*!*****************************************************************************
 *  \brief   add task to threadpool using newThread control it
 *
 *  \param[in]   pool        threadpool address
 *  \param[in]   run         run function
 *  \param[in]   arg         run function params
 *  \param[in]   newThread   1: create a new thread.
 *                           0: do not create a new thread.
 *
 *  \return      0     success
 *              <0     failed
 *
 *
 ******************************************************************************/
int threadpool_auto_add_task_thread(threadpool_t *pool, void *(*run)(void *arg), void *arg, int newThread);


/*!*****************************************************************************
 *  \brief   destroy threadpool
 *
 *  \param[in]   pool  threadpool address
 *
 *  \return  NULL
 *
 *
 ******************************************************************************/
void threadpool_destroy(threadpool_t *pool);


/*!*****************************************************************************
 *  \brief   threadpool control
 *
 *  \param[in]   params
 *
 *  \return  NULL
 *
 *
 ******************************************************************************/
void *thread_routine(void *arg);


// NAL operations

/*!*****************************************************************************
 *  \brief  Insert emulation prevention byte(s) as needed into the data buffer
 *
 *  \param  buf   data buffer to be worked on - new byte(s) will be inserted
 *          size  number of bytes starting from buf to check
 *
 *  \return the number of emulation prevention bytes inserted into buf, 0 if
 *          none.
 *
 *  Note: caller *MUST* ensure for newly inserted bytes, buf has enough free
 *        space starting from buf + size
 ******************************************************************************/
LIB_API int insert_emulation_prevent_bytes(uint8_t *buf, int size);

#ifdef MEASURE_LATENCY
// NI latency measurement queue operations
ni_lat_meas_q_t * ni_lat_meas_q_create(unsigned capacity);
void * ni_lat_meas_q_add_entry(ni_lat_meas_q_t *dec_frame_time_q, uint64_t abs_time, int64_t ts_time);
uint64_t ni_lat_meas_q_check_latency(ni_lat_meas_q_t *dec_frame_time_q, uint64_t abs_time, int64_t ts_time);
#endif

#ifdef _WIN32
LIB_API void usleep(int64_t usec);
LIB_API int32_t gettimeofday(struct timeval* p_tp, struct timezone* p_tzp);
LIB_API uint64_t ni_gettime_ns();

int32_t posix_memalign(void **pp_memptr, size_t alignment, size_t size);
uint32_t ni_round_up(uint32_t number_to_round, uint32_t multiple);
void aligned_free(void* const p_memptr);
#elif __linux__
#define aligned_free(x) if(x){free(x); x=NULL;}
uint32_t ni_get_kernel_max_io_size(const char * p_dev);
#endif

#ifdef __cplusplus
}
#endif
