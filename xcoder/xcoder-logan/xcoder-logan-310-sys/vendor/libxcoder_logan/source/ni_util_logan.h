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
*  \file   ni_util_logan.h
*
*  \brief  Exported utility routines definition
*
*******************************************************************************/

#pragma once

#ifdef _WIN32
#define _SC_PAGESIZE 8
#define sysconf(x)   4096
#endif

#include "ni_device_api_logan.h"
#include "ni_log_logan.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef XCODER_LINUX_CUSTOM_DRIVER
#define NI_LOGAN_NVME_PREFIX "ninvme"
#define NI_LOGAN_NVME_PREFIX_SZ 6
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
#define NI_LOGAN_NVME_PREFIX "vd"
#define NI_LOGAN_NVME_PREFIX_SZ 2
#elif __APPLE__
#define NI_LOGAN_NVME_PREFIX "rdisk"
#define NI_LOGAN_NVME_PREFIX_SZ 4
#else
#define NI_LOGAN_NVME_PREFIX "nvme"
#define NI_LOGAN_NVME_PREFIX_SZ 4
#endif

//#define XCODER_MAX_NUM_TS_TABLE 32
#define LOGAN_XCODER_FRAME_OFFSET_DIFF_THRES 100
//#define XCODER_MAX_ENC_PACKETS_PER_READ 16
#define LOGAN_XCODER_MAX_NUM_QUEUE_ENTRIES 6000
#define LOGAN_XCODER_MAX_NUM_TEMPORAL_LAYER 7
#define LOGAN_BUFFER_POOL_SZ_PER_CONTEXT 300

// for _T400_ENC
#define LOGAN_XCODER_MIN_ENC_PIC_WIDTH 256
#define LOGAN_XCODER_MIN_ENC_PIC_HEIGHT 128
#define LOGAN_XCODER_MAX_ENC_PIC_WIDTH 8192
#define LOGAN_XCODER_MAX_ENC_PIC_HEIGHT 8192

#define NI_LOGAN_DEC_FRAME_BUF_POOL_SIZE_INIT   20
#define NI_LOGAN_DEC_FRAME_BUF_POOL_SIZE_EXPAND 20

#define NI_LOGAN_VPU_FREQ  450  //450Mhz

#define MAX_THREADS 1000

#ifdef XCODER_SIM_ENABLED
extern int32_t sim_eos_flag;
#endif

typedef struct _ni_logan_queue_t
{
  char name[32];
  uint32_t count;
  ni_logan_queue_node_t *p_first;
  ni_logan_queue_node_t *p_last;

} ni_logan_queue_t;

typedef struct _ni_logan_timestamp_table_t
{
  ni_logan_queue_t list;

} ni_logan_timestamp_table_t;

typedef struct ni_task
{
  void *(*run)(void *args); //process function
  void *arg;                //param
  struct ni_task *next;        //the next task of the queue
} ni_task_t;

typedef struct threadpool
{
  ni_pthread_mutex_t pmutex; //mutex
  ni_pthread_cond_t pcond;   //cond
  ni_task_t *first;          //the first task of the queue
  ni_task_t *last;           //the last task of the queue
  int counter;            //current thread number of the pool
  int idle;               //current idle thread number of the pool
  int max_threads;        //The max thread number of the pool
  int quit;               //exit sign
} threadpool_t;

// memory buffer pool operations (one use is for decoder frame buffer pool)

/*!******************************************************************************
 *  \brief get a free memory buffer from the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_buf_t *ni_logan_buf_pool_get_buffer(ni_logan_buf_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief return a used memory buffer to the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_buf_pool_return_buffer(ni_logan_buf_t *buf, ni_logan_buf_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief allocate a memory buffer and place it in the pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_buf_t *ni_logan_buf_pool_allocate_buffer(ni_logan_buf_pool_t *p_buffer_pool, int buffer_size);

// decoder frame buffer pool init & free

/*!******************************************************************************
 *  \brief decoder frame buffer pool init & free
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_logan_dec_fme_buffer_pool_initialize(ni_logan_session_context_t* p_ctx,
                                                int32_t number_of_buffers,
                                                int width,
                                                int height,
                                                int height_align,
                                                int factor);

/*!******************************************************************************
 *  \brief free decoder frame buffer pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_dec_fme_buffer_pool_free(ni_logan_buf_pool_t *p_buffer_pool);

// timestamp buffer pool operations

/*!******************************************************************************
 *  \brief free buffer memory pool
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_buffer_pool_free(ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Find NVMe name space block from device name
 *          If none is found, assume nvme multi-pathing is disabled and return /dev/nvmeXn1
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[in] out_buf Output buffer to put NVMe name space block. Must be at least length 21
 *
 *  \return On success returns NI_LOGAN_RETCODE_SUCCESS
 *          On failure returns NI_LOGAN_RETCODE_FAILURE
 *******************************************************************************/
ni_logan_retcode_t ni_logan_find_blk_name(const char *p_dev, char *p_out_buf, int out_buf_len);

/*!******************************************************************************
 *  \brief  Initialize timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_init(ni_logan_session_context_t* p_ctx,
                                           ni_logan_timestamp_table_t **pp_table,
                                           const char *name);

/*!******************************************************************************
 *  \brief  Clean up timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_done(ni_logan_timestamp_table_t *p_table,
                                           ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Register timestamp in timestamp/frame offset table
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_register(ni_logan_queue_buffer_pool_t *p_buffer_pool,
                                               ni_logan_timestamp_table_t *p_table,
                                               int64_t timestamp,
                                               uint64_t data_info);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_get(ni_logan_timestamp_table_t *p_table,
                                          uint64_t frame_info,
                                          int64_t *p_timestamp,
                                          int32_t threshold,
                                          int32_t print,
                                          ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_get_v2(ni_logan_timestamp_table_t *p_table,
                                             uint64_t frame_offset,
                                             int64_t *p_timestamp,
                                             int32_t threshold,
                                             ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frame offset info with respect
 *  to threshold
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_timestamp_get_with_threshold(ni_logan_timestamp_table_t *p_table,
                                                         uint64_t frame_info,
                                                         int64_t *p_timestamp,
                                                         int32_t threshold,
                                                         int32_t print,
                                                         ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief Timestamp queue clean up
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_logan_timestamp_scan_cleanup(ni_logan_timestamp_table_t *pts_list,
                                     ni_logan_timestamp_table_t *dts_list,
                                     ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Initialize xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_init(ni_logan_session_context_t* p_ctx,
                                       ni_logan_queue_t *p_queue,
                                       const char *name);

/*!******************************************************************************
 *  \brief  Push into xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_push(ni_logan_queue_buffer_pool_t *p_buffer_pool,
                                       ni_logan_queue_t *p_queue,
                                       uint64_t frame_offset,
                                       int64_t timestamp);

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_pop(ni_logan_queue_t *p_queue,
                                      uint64_t frame_offset,
                                      int64_t *p_timestamp,
                                      int32_t threshold,
                                      int32_t print,
                                      ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue with respect to threshold
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_pop_threshold(ni_logan_queue_t *p_queue,
                                                uint64_t frame_offset,
                                                int64_t *p_timestamp,
                                                int32_t threshold,
                                                int32_t print,
                                                ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Free xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_free(ni_logan_queue_t *p_queue, ni_logan_queue_buffer_pool_t *p_buffer_pool);

/*!******************************************************************************
 *  \brief  Print xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_logan_retcode_t ni_logan_queue_print(ni_logan_queue_t *p_queue);

// Type conversion functions
/*!******************************************************************************
 *  \brief  convert string to boolean
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_logan_atobool(const char *p_str, bool *b_error);

/*!******************************************************************************
 *  \brief  convert string to integer
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_logan_atoi(const char *p_str, bool *b_error);

/*!******************************************************************************
 *  \brief  convert string to float
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
double ni_logan_atof(const char *p_str, bool *b_error);

/*!******************************************************************************
 *  \brief  string parser
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_logan_parse_name(const char *arg, const char *const *names, bool *b_error);

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
int threadpool_auto_add_task_thread(threadpool_t *pool,
                                    void *(*run)(void *arg),
                                    void *arg,
                                    int newThread);


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

// Netint HW YUV420p data layout related utility functions

/*!*****************************************************************************
 *  \brief  Get dimension information of Netint HW YUV420p frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_logan_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *
 *  \param[in]  width   source YUV frame width
 *  \param[in]  height  source YUV frame height
 *  \param[in]  bit_depth_factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_h264  non-0 for H.264 codec, 0 otherwise (H.265)
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return Y/Cb/Cr stride and height info
 *
 ******************************************************************************/
LIB_API void ni_logan_get_hw_yuv420p_dim(int width, int height, int bit_depth_factor,
                                         int is_h264,
                                         int plane_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                         int plane_height[NI_LOGAN_MAX_NUM_DATA_POINTERS]);


/*!*****************************************************************************
 *  \brief  Copy YUV data to Netint HW YUV420p frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_logan_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers of Y/Cb/Cr to which data is copied
 *  \param[in]  p_src  pointers of Y/Cb/Cr from which data is copied
 *  \param[in]  width  source YUV frame width
 *  \param[in]  height source YUV frame height
 *  \param[in]  bit_depth_factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *
 *  \return Y/Cb/Cr data
 *
 ******************************************************************************/
LIB_API void ni_logan_copy_hw_yuv420p(uint8_t *p_dst[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                      uint8_t *p_src[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                      int width, int height, int bit_depth_factor,
                                      int dst_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                      int dst_height[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                      int src_stride[NI_LOGAN_MAX_NUM_DATA_POINTERS],
                                      int src_height[NI_LOGAN_MAX_NUM_DATA_POINTERS]);


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
LIB_API int ni_logan_insert_emulation_prevent_bytes(uint8_t *buf, int size);

/*!*****************************************************************************
 *  \brief  Remove emulation prevention byte(s) as needed from the data buffer
 *
 *  \param  buf   data buffer to be worked on - emu prevent byte(s) will be
 *                removed from.
 *          size  number of bytes starting from buf to check
 *
 *  \return the number of emulation prevention bytes removed from buf, 0 if
 *          none.
 *
 *  Note: buf will be modified if emu prevent byte(s) found and removed.
 ******************************************************************************/
LIB_API int ni_logan_remove_emulation_prevent_bytes(uint8_t *buf, int size);

/*!******************************************************************************
 *  \brief  overwrite the 32 bits of integer value at bit position pos
 *
 *  \param  buf   data buffer to be worked on
 *          pos   the position to be modified
 *          value The value that needs to be modified to
 *
 *  \return void
 *
 * Note: caller *MUST* ensure that the pos and value won't go beyond the memory
 *        boundary of data. otherwise memory corruption would occur.
 ******************************************************************************/
LIB_API void ni_logan_overwrite_specified_pos(uint8_t *buf, int pos, int value);

#ifdef MEASURE_LATENCY
// NI latency measurement queue operations
ni_logan_lat_meas_q_t * ni_logan_lat_meas_q_create(unsigned capacity);

void * ni_logan_lat_meas_q_add_entry(ni_logan_lat_meas_q_t *dec_frame_time_q,
                               uint64_t abs_time,
                               int64_t ts_time);

uint64_t ni_logan_lat_meas_q_check_latency(ni_logan_lat_meas_q_t *dec_frame_time_q,
                                     uint64_t abs_time,
                                     int64_t ts_time);

#endif

LIB_API uint64_t ni_logan_gettime_ns(void);
LIB_API void ni_logan_usleep(int64_t usec);
LIB_API int32_t ni_logan_gettimeofday(struct timeval* p_tp, void *p_tzp);
int32_t ni_logan_posix_memalign(void **pp_memptr, size_t alignment, size_t size);
uint32_t ni_logan_round_up(uint32_t number_to_round, uint32_t multiple);
LIB_API void ni_logan_aligned_free(void* const p_memptr);
#if __linux__ || __APPLE__
uint32_t ni_logan_get_kernel_max_io_size(const char * p_dev);
#endif

/*!******************************************************************************
 *  \brief  initialize a mutex
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_mutex_init(ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    int rc;
    ni_pthread_mutexattr_t attr;

    rc = pthread_mutexattr_init(&attr);
    if (rc != 0)
    {
        return -1;
    }

    /* For some cases to prevent the lock owner locking twice (i.e. internal
     * API calls or if user application decides to lock the xcoder_mutex outside
     * of API), The recursive mutex is a nice thing to solve the problem.
     */
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    return pthread_mutex_init(mutex, &attr);
#endif
}

/*!******************************************************************************
 *  \brief  destory a mutex
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_mutex_destroy(ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    DeleteCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_destroy(mutex);
#endif
}

/*!******************************************************************************
 *  \brief  thread mutex alloc&init
 *
 *  \param
 *
 *  \return On success returns true
 *          On failure returns false
 *******************************************************************************/
static bool ni_logan_pthread_mutex_alloc_and_init(ni_pthread_mutex_t **mutex)
{
    int rc = 0;
    *mutex = (ni_pthread_mutex_t *)calloc(1, sizeof(ni_pthread_mutex_t));
    if (!(*mutex))
    {
        return false;
    }

    rc = ni_logan_pthread_mutex_init(*mutex);
    if (rc != 0)
    {
        free(*mutex);
        return false;
    }

    return true;
}

/*!******************************************************************************
 *  \brief  thread mutex free&destroy
 *
 *  \param
 *
 *  \return On success returns true
 *          On failure returns false
 *******************************************************************************/
static bool ni_logan_pthread_mutex_free_and_destroy(ni_pthread_mutex_t **mutex)
{
    int rc = 0;
    void *p;
    static void *const tmp = NULL;

    // Avoid static analyzer inspection
    memcpy(&p, mutex, sizeof(p));
    if (p != NULL)
    {
        rc = ni_logan_pthread_mutex_destroy((ni_pthread_mutex_t *)p);
        memcpy(mutex, &tmp, sizeof(p));
        free(p);
    } else
    {
        rc = -1;
    }

    return rc == 0;
}

/*!******************************************************************************
 *  \brief  thread mutex lock
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_mutex_lock(ni_pthread_mutex_t *mutex)
{
    int rc = 0;
    if (mutex != NULL)
    {
#ifdef _WIN32
        EnterCriticalSection(mutex);
#else
        rc = pthread_mutex_lock(mutex);
#endif
    } else
    {
        rc = -1;
    }

    return rc;
}

/*!******************************************************************************
 *  \brief  thread mutex unlock
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_mutex_unlock(ni_pthread_mutex_t *mutex)
{
    int rc = 0;
    if (mutex != NULL)
    {
#ifdef _WIN32
        LeaveCriticalSection(mutex);
#else
        rc = pthread_mutex_unlock(mutex);
#endif
    } else
    {
        rc = -1;
    }

    return rc;
}

#ifdef _WIN32
static unsigned __stdcall __thread_worker(void *arg)
{
    ni_pthread_t *t = (ni_pthread_t *)arg;
    t->rc = t->start_routine(t->arg);
    return 0;
}
#endif

/*!******************************************************************************
 *  \brief  create a new thread
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static int ni_logan_pthread_create(ni_pthread_t *thread,
                             const ni_pthread_attr_t *attr,
                             void *(*start_routine)(void *), void *arg)
{
#ifdef _WIN32
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->handle =
#if HAVE_WINRT
        (void *)CreateThread(NULL, 0, __thread_worker, thread, 0, NULL);
#else
        (void *)_beginthreadex(NULL, 0, __thread_worker, thread, 0, NULL);
#endif
    return !thread->handle;
#else
    return pthread_create(thread, attr, start_routine, arg);
#endif
}

/*!******************************************************************************
 *  \brief  join with a terminated thread
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static int ni_logan_pthread_join(ni_pthread_t thread, void **value_ptr)
{
#ifdef _WIN32
    DWORD rc = WaitForSingleObject(thread.handle, INFINITE);
    if (rc != WAIT_OBJECT_0)
    {
        if (rc == WAIT_ABANDONED)
            return EINVAL;
        else
            return EDEADLK;
    }
    if (value_ptr)
        *value_ptr = thread.rc;
    CloseHandle(thread.handle);
    return 0;
#else
    return pthread_join(thread, value_ptr);
#endif
}

/*!******************************************************************************
 *  \brief  initialize condition variables
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_cond_init(ni_pthread_cond_t *cond,
                                       const ni_pthread_condattr_t *attr)
{
#ifdef _WIN32
    InitializeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_init(cond, attr);
#endif
}

/*!******************************************************************************
 *  \brief  destroy condition variables
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_cond_destroy(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    /* native condition variables do not destroy */
    return 0;
#else
    return pthread_cond_destroy(cond);
#endif
}

/*!******************************************************************************
 *  \brief  broadcast a condition
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_cond_broadcast(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    WakeAllConditionVariable(cond);
    return 0;
#else
    return pthread_cond_broadcast(cond);
#endif
}

/*!******************************************************************************
 *  \brief  wait on a condition
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_cond_wait(ni_pthread_cond_t *cond,
                                       ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
#else
    return pthread_cond_wait(cond, mutex);
#endif
}

/*!******************************************************************************
 *  \brief  signal a condition
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_cond_signal(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    WakeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_signal(cond);
#endif
}

/*!******************************************************************************
 *  \brief  wait on a condition
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static int ni_logan_pthread_cond_timedwait(ni_pthread_cond_t *cond,
                                     ni_pthread_mutex_t *mutex,
                                     const struct timespec *abstime)
{
#ifdef _WIN32
    int64_t abs_ns = abstime->tv_sec * 1000000000LL + abstime->tv_nsec;
    DWORD t = (uint32_t)((abs_ns - ni_logan_gettime_ns()) / 1000000);

    if (!SleepConditionVariableCS(cond, mutex, t))
    {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT)
            return ETIMEDOUT;
        else
            return EINVAL;
    }
    return 0;
#else
    return pthread_cond_timedwait(cond, mutex, abstime);
#endif
}

/*!******************************************************************************
 *  \brief  examine and change mask of blocked signals
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_logan_pthread_sigmask(int how, const ni_sigset_t *set,
                                     ni_sigset_t *oldset)
{
#ifdef _WIN32
    return 0;
#else
    return pthread_sigmask(how, set, oldset);
#endif
}

#ifdef __cplusplus
}
#endif
