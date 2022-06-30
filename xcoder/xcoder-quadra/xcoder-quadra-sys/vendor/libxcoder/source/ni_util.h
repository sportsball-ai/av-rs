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
*   \file   ni_util.h
*
*  \brief  Exported utility routines definition
*
*******************************************************************************/

#pragma once

#ifdef _WIN32
#define _SC_PAGESIZE 0
#define sysconf(x)   0
#else
#endif

#include "ni_device_api.h"
#include "ni_log.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline int ni_min(int a, int b)
{
    return a < b ? a : b;
}

static inline int ni_max(int a, int b)
{
    return a > b ? a : b;
}

static inline float ni_minf(float a, float b)
{
    return a < b ? a : b;
}

static inline float ni_maxf(float a, float b)
{
    return a > b ? a : b;
}

static inline int clip3(int min, int max, int a)
{
    return ni_min(ni_max(min, a), max);
}

static inline float clip3f(float min, float max, float a)
{
    return ni_minf(ni_maxf(min, a), max);
}

// Compile time assert macro
#define COMPILE_ASSERT(condition) ((void)sizeof(char[1 - 2 * !(condition)]))

#define XCODER_MAX_NUM_TS_TABLE 32
#define XCODER_FRAME_OFFSET_DIFF_THRES 100
#define XCODER_MAX_ENC_PACKETS_PER_READ 16
#define XCODER_MAX_NUM_QUEUE_ENTRIES 6000
#define XCODER_MAX_NUM_TEMPORAL_LAYER 7
#define BUFFER_POOL_SZ_PER_CONTEXT 300

// for _T400_ENC
#define XCODER_MIN_ENC_PIC_WIDTH 144
#define XCODER_MIN_ENC_PIC_HEIGHT 128
#define XCODER_MAX_ENC_PIC_WIDTH 8192
#define XCODER_MAX_ENC_PIC_HEIGHT 8192

#define NI_DEC_FRAME_BUF_POOL_SIZE_INIT   20
#define NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND 20


// memory buffer pool operations (one use is for decoder frame buffer pool)
ni_buf_t *ni_buf_pool_get_buffer(ni_buf_pool_t *p_buffer_pool);

void ni_buf_pool_return_buffer(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool);

ni_buf_t *ni_buf_pool_allocate_buffer(ni_buf_pool_t *p_buffer_pool, int buffer_size);

// decoder frame buffer pool init & free
int32_t ni_dec_fme_buffer_pool_initialize(ni_session_context_t* p_ctx, int32_t number_of_buffers, int width, int height, int height_align, int factor);
void ni_dec_fme_buffer_pool_free(ni_buf_pool_t *p_buffer_pool);

// timestamp buffer pool operations
void ni_buffer_pool_free(ni_queue_buffer_pool_t *p_buffer_pool);

ni_retcode_t ni_find_blk_name(const char *p_dev, char *p_out_buf, int out_buf_len);
ni_retcode_t ni_timestamp_init(ni_session_context_t* p_ctx, ni_timestamp_table_t **pp_table, const char *name);
ni_retcode_t ni_timestamp_done(ni_timestamp_table_t *p_table, ni_queue_buffer_pool_t *p_buffer_pool);
ni_retcode_t ni_timestamp_register(ni_queue_buffer_pool_t *p_buffer_pool, ni_timestamp_table_t *p_table, int64_t timestamp, uint64_t data_info);
ni_retcode_t ni_timestamp_get(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);
ni_retcode_t ni_timestamp_get_v2(ni_timestamp_table_t *p_table, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, ni_queue_buffer_pool_t *p_buffer_pool);

ni_retcode_t ni_timestamp_get_with_threshold(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);

void ni_timestamp_scan_cleanup(ni_timestamp_table_t *pts_list,
                               ni_timestamp_table_t *dts_list,
                               ni_queue_buffer_pool_t *p_buffer_pool);

ni_retcode_t ni_queue_init(ni_session_context_t* p_ctx, ni_queue_t *p_queue, const char *name);
ni_retcode_t ni_queue_push(ni_queue_buffer_pool_t *p_buffer_pool, ni_queue_t *p_queue, uint64_t frame_offset, int64_t timestamp);
ni_retcode_t ni_queue_pop(ni_queue_t *p_queue, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);
ni_retcode_t ni_queue_pop_threshold(ni_queue_t *p_queue, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool);
ni_retcode_t ni_queue_free(ni_queue_t *p_queue, ni_queue_buffer_pool_t *p_buffer_pool);
ni_retcode_t ni_queue_print(ni_queue_t *p_queue);

int32_t ni_atobool(const char *p_str, bool *b_error);
int32_t ni_atoi(const char *p_str, bool *b_error);
double ni_atof(const char *p_str, bool *b_error);
int32_t ni_parse_name(const char *arg, const char *const *names, bool *b_error);

// Netint HW YUV420p data layout related utility functions

/*!*****************************************************************************
 *  \brief  Get dimension information of Netint HW YUV420p frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *
 *  \param[in]  width   source YUV frame width
 *  \param[in]  height  source YUV frame height
 *  \param[in]  bit_depth_factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_nv12  non-0 for NV12 frame, 0 otherwise
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return Y/Cb/Cr stride and height info
 *
 ******************************************************************************/
LIB_API void ni_get_hw_yuv420p_dim(int width, int height, int bit_depth_factor,
                                   int is_nv12,
                                   int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                                   int plane_height[NI_MAX_NUM_DATA_POINTERS]);

/*!*****************************************************************************
 *  \brief  Copy YUV data to Netint HW YUV420p frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers of Y/Cb/Cr to which data is copied
 *  \param[in]  p_src  pointers of Y/Cb/Cr from which data is copied
 *  \param[in]  width  source YUV frame width
 *  \param[in]  height source YUV frame height
 *  \param[in]  bit_depth_factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_nv12  non-0 for NV12 frame, 0 otherwise
 *  \param[in]  conf_win_right  right offset of conformance window
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *
 *  \return Y/Cb/Cr data
 *
 ******************************************************************************/
LIB_API void ni_copy_hw_yuv420p(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                                uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                                int width, int height, int bit_depth_factor,
                                int is_nv12, int conf_win_right,
                                int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                                int dst_height[NI_MAX_NUM_DATA_POINTERS],
                                int src_stride[NI_MAX_NUM_DATA_POINTERS],
                                int src_height[NI_MAX_NUM_DATA_POINTERS]);

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
LIB_API int ni_insert_emulation_prevent_bytes(uint8_t *buf, int size);

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
LIB_API int ni_remove_emulation_prevent_bytes(uint8_t *buf, int size);

#ifdef MEASURE_LATENCY
// NI latency measurement queue operations
ni_lat_meas_q_t *ni_lat_meas_q_create(unsigned capacity);

void *ni_lat_meas_q_add_entry(ni_lat_meas_q_t *dec_frame_time_q,
                              uint64_t abs_time, int64_t ts_time);

uint64_t ni_lat_meas_q_check_latency(ni_lat_meas_q_t *dec_frame_time_q,
                                     uint64_t abs_time, int64_t ts_time);

#endif

/*!*****************************************************************************
 *  \brief Get time for logs with microsecond timestamps
 *
 *  \param[in/out] p_tp   timeval struct
 *  \param[in] p_tzp      void *
 *
 *  \return return 0 for success, -1 for error
 ******************************************************************************/
LIB_API int32_t ni_gettimeofday(struct timeval *p_tp, void *p_tzp);

int32_t ni_posix_memalign(void **pp_memptr, size_t alignment, size_t size);
uint32_t ni_round_up(uint32_t number_to_round, uint32_t multiple);
uint64_t ni_gettime_ns(void);

#define ni_aligned_free(p_memptr)                                              \
{                                                                              \
    free(p_memptr);                                                            \
    p_memptr = NULL;                                                           \
}

#ifdef __linux__
uint32_t ni_get_kernel_max_io_size(const char * p_dev);
#endif

LIB_API uint64_t ni_gettime_ns(void);
LIB_API void ni_usleep(int64_t usec);
LIB_API ni_retcode_t
ni_network_layer_convert_output(float *dst, uint32_t num, ni_packet_t *p_packet,
                                ni_network_data_t *p_network, uint32_t layer);
LIB_API uint32_t ni_ai_network_layer_size(ni_network_layer_params_t *p_param);
LIB_API uint32_t ni_ai_network_layer_dims(ni_network_layer_params_t *p_param);
LIB_API ni_retcode_t ni_network_layer_convert_tensor(
    uint8_t *dst, uint32_t dst_len, const char *tensor_file,
    ni_network_layer_params_t *p_param);
LIB_API ni_retcode_t ni_network_convert_tensor_to_data(
    uint8_t *dst, uint32_t dst_len, float *src, uint32_t src_len,
    ni_network_layer_params_t *p_param);
LIB_API ni_retcode_t ni_network_convert_data_to_tensor(
    float *dst, uint32_t dst_len, uint8_t *src, uint32_t src_len,
    ni_network_layer_params_t *p_param);

LIB_API void ni_calculate_sha256(const uint8_t aui8Data[],
                                 size_t ui32DataLength, uint8_t aui8Hash[]);
/*!*****************************************************************************
*  \brief  Copy Descriptor data to Netint HW descriptor frame layout to be sent
*          to encoder for encoding. Data buffer (dst) is usually allocated by
*          ni_encoder_frame_buffer_alloc. Only necessary when metadata size in
*          source is insufficient
*
*  \param[out] p_dst  pointers of Y/Cb/Cr to which data is copied
*  \param[in]  p_src  pointers of Y/Cb/Cr from which data is copied
*
*  \return descriptor data
*
******************************************************************************/
LIB_API void ni_copy_hw_descriptors(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                                    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS]);

/*!******************************************************************************
 *  \brief  initialize a mutex
 *
 *  \param
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
static inline int ni_pthread_mutex_init(ni_pthread_mutex_t *mutex)
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
static inline int ni_pthread_mutex_destroy(ni_pthread_mutex_t *mutex)
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
static bool ni_pthread_mutex_alloc_and_init(ni_pthread_mutex_t **mutex)
{
    int rc = 0;
    *mutex = (ni_pthread_mutex_t *)calloc(1, sizeof(ni_pthread_mutex_t));
    if (!(*mutex))
    {
        return false;
    }

    rc = ni_pthread_mutex_init(*mutex);
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
static bool ni_pthread_mutex_free_and_destroy(ni_pthread_mutex_t **mutex)
{
    int rc = 0;
    void *p;
    static void *const tmp = NULL;

    // Avoid static analyzer inspection
    memcpy(&p, mutex, sizeof(p));
    if (p != NULL)
    {
        rc = ni_pthread_mutex_destroy((ni_pthread_mutex_t *)p);
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
static inline int ni_pthread_mutex_lock(ni_pthread_mutex_t *mutex)
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
static inline int ni_pthread_mutex_unlock(ni_pthread_mutex_t *mutex)
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
static int ni_pthread_create(ni_pthread_t *thread,
                             const ni_pthread_attr_t *attr,
                             void *(*start_routine)(void *), void *arg)
{
#ifdef _WIN32
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->handle =
#if HAVE_WINRT
        (void *)CreateThread(NULL, 0, win32thread_worker, thread, 0, NULL);
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
static int ni_pthread_join(ni_pthread_t thread, void **value_ptr)
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
static inline int ni_pthread_cond_init(ni_pthread_cond_t *cond,
                                       const ni_pthread_condattr_t *unused_attr)
{
#ifdef _WIN32
    InitializeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_init(cond, unused_attr);
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
static inline int ni_pthread_cond_destroy(ni_pthread_cond_t *cond)
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
static inline int ni_pthread_cond_broadcast(ni_pthread_cond_t *cond)
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
static inline int ni_pthread_cond_wait(ni_pthread_cond_t *cond,
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
static inline int ni_pthread_cond_signal(ni_pthread_cond_t *cond)
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
static int ni_pthread_cond_timedwait(ni_pthread_cond_t *cond,
                                     ni_pthread_mutex_t *mutex,
                                     const struct timespec *abstime)
{
#ifdef _WIN32
    int64_t abs_ns = abstime->tv_sec * 1000000000LL + abstime->tv_nsec;
    DWORD t = (uint32_t)((abs_ns - ni_gettime_ns()) / 1000000);

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
static inline int ni_pthread_sigmask(int how, const ni_sigset_t *set,
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
