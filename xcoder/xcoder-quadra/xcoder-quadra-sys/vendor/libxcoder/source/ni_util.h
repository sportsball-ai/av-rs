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
 *  \file   ni_util.h
 *
 *  \brief  Utility definitions
 ******************************************************************************/

#pragma once

#ifdef _WIN32
#define _SC_PAGESIZE 4096
#define sysconf(x)   x
#endif

#include "ni_device_api.h"
#include "ni_log.h"
#include "ni_defs.h"

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

#define NIALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
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
ni_retcode_t ni_check_dev_name(const char *p_dev);
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
 *  \param[in]  is_semiplanar  non-0 for semiplnar frame, 0 otherwise
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return Y/Cb/Cr stride and height info
 *
 ******************************************************************************/
LIB_API void ni_get_hw_yuv420p_dim(int width, int height, int bit_depth_factor,
                                   int is_semiplanar,
                                   int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                                   int plane_height[NI_MAX_NUM_DATA_POINTERS]);

/*!*****************************************************************************
 *  \brief  Get dimension information of frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *          The returned stride and height info will take alignment 
 *          requirements into account.
 *
 *  \param[in]  width   source frame width
 *  \param[in]  height  source frame height
 *  \param[in]  pix_fmt  ni pixel format
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return stride and height info
 *
 ******************************************************************************/
LIB_API void ni_get_frame_dim(int width, int height,
                              ni_pix_fmt_t pix_fmt,
                              int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                              int plane_height[NI_MAX_NUM_DATA_POINTERS]);

/*!*****************************************************************************
 *  \brief  Get dimension information of frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *          The returned stride and height info will take into account both min
 *          resolution and alignment requirements.
 *
 *  \param[in]  width   source frame width
 *  \param[in]  height  source frame height
 *  \param[in]  pix_fmt  ni pixel format
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return stride and height info
 *
 ******************************************************************************/
LIB_API void ni_get_min_frame_dim(int width, int height,
                                      ni_pix_fmt_t pix_fmt,
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
 *  \param[in]  is_semiplanar  non-0 for semiplanar frame, 0 otherwise
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
                                int is_semiplanar, int conf_win_right,
                                int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                                int dst_height[NI_MAX_NUM_DATA_POINTERS],
                                int src_stride[NI_MAX_NUM_DATA_POINTERS],
                                int src_height[NI_MAX_NUM_DATA_POINTERS]);

/*!*****************************************************************************
 *  \brief  Copy RGBA or YUV data to Netint HW frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers to which data is copied
 *  \param[in]  p_src  pointers from which data is copied
 *  \param[in]  width  source frame width
 *  \param[in]  height source frame height
 *  \param[in]  factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  pix_fmt  pixel format to distinguish between planar types and/or components
 *  \param[in]  conf_win_right  right offset of conformance window
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *
 *  \return copied data
 *
 ******************************************************************************/
LIB_API void ni_copy_frame_data(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                                uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                                int frame_width, int frame_height,
                                int factor, ni_pix_fmt_t pix_fmt,
                                int conf_win_right,
                                int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                                int dst_height[NI_MAX_NUM_DATA_POINTERS],
                                int src_stride[NI_MAX_NUM_DATA_POINTERS],
                                int src_height[NI_MAX_NUM_DATA_POINTERS]);

/*!*****************************************************************************
 *  \brief  Copy yuv444p data to yuv420p frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out]    p_dst0  pointers of Y/Cb/Cr as yuv420p output0
 *  \param[out]    p_dst1  pointers of Y/Cb/Cr as yuv420p output1
 *  \param[in]     p_src  pointers of Y/Cb/Cr as yuv444p intput
 *  \param[in]     width  source YUV frame width
 *  \param[in]     height source YUV frame height
 *  \param[in]     factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]     mode 0 for
 *                 out0 is Y+1/2V, with the original input as the out0, 1/4V
 *                 copy to data[1] 1/4V copy to data[2]
 *                 out1 is U+1/2V, U copy to data[0], 1/4V copy to data[1], 1/4V
 *                 copy to data[2]
 *                 mode 1 for
 *                 out0 is Y+1/2u+1/2v, with the original input as the output0,
 *                 1/4U copy to data[1] 1/4V copy to data[2]
 *                 out1 is (1/2U+1/2V)+1/4U+1/4V, 1/2U & 1/2V copy to data[0],
 *                 1/4U copy to data[1], 1/4V copy to data[2]
 *
 *  \return Y/Cb/Cr data
 *
 ******************************************************************************/
LIB_API void ni_copy_yuv_444p_to_420p(uint8_t *p_dst0[NI_MAX_NUM_DATA_POINTERS],
                                      uint8_t *p_dst1[NI_MAX_NUM_DATA_POINTERS],
                                      uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                                      int width, int height, int factor,
                                      int mode);

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

/*!*****************************************************************************
 *  \brief Get time for logs with microsecond timestamps
 *
 *  \param[in/out] p_tp   timeval struct
 *  \param[in] p_tzp      void *
 *
 *  \return return 0 for success, -1 for error
 ******************************************************************************/
LIB_API int32_t ni_gettimeofday(struct timeval *p_tp, void *p_tzp);

/*!*****************************************************************************
 *  \brief Allocate aligned memory
 *
 *  \param[in/out] memptr  The address of the allocated memory will be a
 *                         multiple of alignment, which must be a power of two
 *                         and a multiple of sizeof(void *).  If size is 0, then
 *                         the value placed is either NULL, or a unique pointer
 *                         value that can later be successfully passed to free.
 *  \param[in] alignment   The alignment value of the allocated value.
 *  \param[in] size        The allocated memory size.
 *
 *  \return                0 for success, ENOMEM for error
 ******************************************************************************/
LIB_API int ni_posix_memalign(void **memptr, size_t alignment, size_t size);

uint32_t ni_round_up(uint32_t number_to_round, uint32_t multiple);

#ifdef _WIN32
#define ni_aligned_free(p_memptr)                                              \
{                                                                              \
    _aligned_free(p_memptr);                                                   \
    p_memptr = NULL;                                                           \
}
#else
#define ni_aligned_free(p_memptr)                                              \
{                                                                              \
    free(p_memptr);                                                            \
    p_memptr = NULL;                                                           \
}
#endif

// This method is used in device session close function to unset all the
// pointers in the session context.
#define ni_memfree(p_memptr)                                                   \
{                                                                              \
    free(p_memptr);                                                            \
    p_memptr = NULL;                                                           \
}

#if __linux__ || __APPLE__
uint32_t ni_get_kernel_max_io_size(const char * p_dev);
#endif

LIB_API uint64_t ni_gettime_ns(void);
LIB_API void ni_usleep(int64_t usec);
LIB_API char *ni_strtok(char *s, const char *delim, char **saveptr);
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

/*!*****************************************************************************
 *  \brief  Get libxcoder API version
 *
 *  \return char pointer to libxcoder API version
 ******************************************************************************/
LIB_API char* ni_get_libxcoder_api_ver(void);

/*!*****************************************************************************
 *  \brief  Get FW API version libxcoder is compatible with
 *
 *  \return char pointer to FW API version libxcoder is compatible with
 ******************************************************************************/
LIB_API NI_DEPRECATED char* ni_get_compat_fw_api_ver(void);

/*!*****************************************************************************
 *  \brief  Get formatted FW API version string from unformatted FW API version
 *          string
 *
 *  \param[in]   ver_str  pointer to string containing FW API. Only up to 3
 *                        characters will be read
 *  \param[out]  fmt_str  pointer to string buffer of at least size 5 to output
 *                        formated version string to
 *
 *  \return none
 ******************************************************************************/
LIB_API void ni_fmt_fw_api_ver_str(const char ver_str[], char fmt_str[]);

/*!*****************************************************************************
 *  \brief  Compare two 3 character strings containing a FW API version. Handle
 *          comparision when FW API version format length changed from 2 to 3.
 *
 *  \param[in]  ver1  pointer to string containing FW API. Only up to 3
 *                    characters will be read
 *  \param[in]  ver2  pointer to string containing FW API. Only up to 3
 *                    characters will be read
 *
 *  \return 0 if ver1 == ver2, 1 if ver1 > ver2, -1 if ver1 < ver2
 ******************************************************************************/
LIB_API int ni_cmp_fw_api_ver(const char ver1[], const char ver2[]);

/*!*****************************************************************************
 *  \brief  Get libxcoder SW release version
 *
 *  \return char pointer to libxcoder SW release version
 ******************************************************************************/
LIB_API char* ni_get_libxcoder_release_ver(void);

/*!*****************************************************************************
 *  \brief  initialize a mutex
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_mutex_init(ni_pthread_mutex_t *mutex);

/*!*****************************************************************************
 *  \brief  destory a mutex
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_mutex_destroy(ni_pthread_mutex_t *mutex);

/*!*****************************************************************************
 *  \brief  thread mutex lock
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_mutex_lock(ni_pthread_mutex_t *mutex);

/*!*****************************************************************************
 *  \brief  thread mutex unlock
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_mutex_unlock(ni_pthread_mutex_t *mutex);

/*!*****************************************************************************
 *  \brief  create a new thread
 *
 *  \param[in] thread          thread id 
 *  \param[in] attr            attributes to the new thread 
 *  \param[in] start_routine   entry of the thread routine 
 *  \param[in] arg             sole argument of the routine 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_create(ni_pthread_t *thread,
                              const ni_pthread_attr_t *attr,
                              void *(*start_routine)(void *), void *arg);

/*!*****************************************************************************
 *  \brief  join with a terminated thread
 *
 *  \param[in]  thread     thread id 
 *  \param[out] value_ptr  return status 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_join(ni_pthread_t thread, void **value_ptr);

/*!*****************************************************************************
 *  \brief  initialize condition variables
 *
 *  \param[in] cond  condition variable 
 *  \param[in] attr  attribute to the condvar 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_cond_init(ni_pthread_cond_t *cond,
                                 const ni_pthread_condattr_t *attr);

/*!*****************************************************************************
 *  \brief  destroy condition variables
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_cond_destroy(ni_pthread_cond_t *cond);

/*!*****************************************************************************
 *  \brief  broadcast a condition
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_cond_broadcast(ni_pthread_cond_t *cond);

/*!*****************************************************************************
 *  \brief  wait on a condition
 *
 *  \param[in] cond  condition variable
 *  \param[in] mutex mutex related to the condvar
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_cond_wait(ni_pthread_cond_t *cond,
                                 ni_pthread_mutex_t *mutex);

/*!******************************************************************************
 *  \brief  signal a condition
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
LIB_API int ni_pthread_cond_signal(ni_pthread_cond_t *cond);

/*!*****************************************************************************
 *  \brief  wait on a condition
 *
 *  \param[in] cond    condition variable
 *  \param[in] mutex   mutex related to the condvar
 *  \param[in[ abstime abstract value of timeout
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_cond_timedwait(ni_pthread_cond_t *cond,
                                      ni_pthread_mutex_t *mutex,
                                      const struct timespec *abstime);

/*!*****************************************************************************
 *  \brief  examine and change mask of blocked signals
 *
 *  \param[in] how     behavior of this call, can be value of SIG_BLOCK,
 *                     SIG_UNBLOCK and  SIG_SETMASK 
 *  \param[in] set     current value of the signal mask. If NULL, the mask keeps
 *                     unchanged. 
 *  \param[in] old_set previous value of the signal mask, can be NULL. 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
LIB_API int ni_pthread_sigmask(int how, const ni_sigset_t *set,
                               ni_sigset_t *oldset);

/*!*****************************************************************************
 *  \brief  Get text string for the provided error
 *
 *  \return char pointer for the provided error
 ******************************************************************************/
LIB_API const char *ni_get_rc_txt(ni_retcode_t rc);

/*!*****************************************************************************
 *  \brief  Retrieve key and value from 'key=value' pair
 *
 *  \param[in]   p_str    pointer to string to extract pair from
 *  \param[out]  key      pointer to key 
 *  \param[out]  value    pointer to value
 *
 *  \return return 0 if successful, otherwise 1
 *
 ******************************************************************************/
LIB_API int ni_param_get_key_value(char *p_str, char *key, char *value);

/*!*****************************************************************************
 *  \brief  Retrieve encoder config parameter values from --xcoder-params
 *
 *  \param[in]   xcoderParams    pointer to string containing xcoder params
 *  \param[out]  params          pointer to xcoder params to fill out 
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
LIB_API int ni_retrieve_xcoder_params(char xcoderParams[],
                                      ni_xcoder_params_t *params,
                                      ni_session_context_t *ctx);

/*!*****************************************************************************
 *  \brief  Retrieve custom gop config values from --xcoder-gop
 *
 *  \param[in]   xcoderGop       pointer to string containing xcoder gop
 *  \param[out]  params          pointer to xcoder params to fill out
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
LIB_API int ni_retrieve_xcoder_gop(char xcoderGop[],
                                      ni_xcoder_params_t *params,
                                      ni_session_context_t *ctx);

/*!*****************************************************************************
 *  \brief  Retrieve decoder config parameter values from --decoder-params
 *
 *  \param[in]   xcoderParams    pointer to string containing xcoder params
 *  \param[out]  params          pointer to xcoder params to fill out 
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
LIB_API int ni_retrieve_decoder_params(char xcoderParams[],
                                       ni_xcoder_params_t *params,
                                       ni_session_context_t *ctx);

/*!*****************************************************************************
 *  \brief  return error string according to error code from firmware
 *
 *  \param[in] rc      error code return from firmware
 *
 *  \return error string
 ******************************************************************************/
LIB_API const char *ni_ai_errno_to_str(int rc);
#ifdef __cplusplus
}
#endif
