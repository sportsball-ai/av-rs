/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_util.c
*
*  \brief  Exported utility routines
*
*******************************************************************************/

#ifdef __linux__
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>
#include <dirent.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "ni_nvme.h"
#include "ni_util.h"

#ifdef XCODER_SIM_ENABLED
int32_t sim_run_count = 0;
int32_t sim_eos_flag = 0;
#endif

#ifdef _WIN32
/*! FILETIME of Jan 1 1970 00:00:00. */
static const unsigned __int64 epoch = ((unsigned __int64)116444736000000000ULL);

/*!
 * timezone information is stored outside the kernel so tzp isn't used anymore.
 *
 * Note: this function is not for Win32 high precision timing purpose. See
 * elapsed_time().
 */
int32_t gettimeofday(struct timeval *p_tp, struct timezone *p_tzp)
{
  FILETIME    file_time;
  SYSTEMTIME  system_time;
  ULARGE_INTEGER ularge;

  (void)p_tzp; /*!Remove compiler warnings*/

  GetSystemTime(&system_time);
  SystemTimeToFileTime(&system_time, &file_time);
  ularge.LowPart = file_time.dwLowDateTime;
  ularge.HighPart = file_time.dwHighDateTime;

  p_tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
  p_tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

  return 0;
}

/*Note: this function is for Win32 high precision timing purpose.*/
uint64_t ni_gettime_ns()
{
  LARGE_INTEGER frequency;
  LARGE_INTEGER count;
  uint64_t time_sec, time_nsec;

  // Get frequency firstly
  QueryPerformanceFrequency(&frequency);

  QueryPerformanceCounter(&count);

  time_sec = count.QuadPart / frequency.QuadPart;
  time_nsec = (count.QuadPart - time_sec * frequency.QuadPart) * 1000000000LL / frequency.QuadPart;

  return (time_sec * 1000000000LL + time_nsec);
}

uint32_t ni_round_up(uint32_t  number_to_round, uint32_t  multiple)
{
  if (0 == multiple)
  {
    return number_to_round;
  }

  uint32_t remainder = number_to_round % multiple;
  if (0 == remainder)
  {
    return number_to_round;
  }

  return (number_to_round + multiple - remainder);
}

int32_t posix_memalign(void ** pp_memptr, size_t alignment, size_t size)
{
  *pp_memptr = malloc(size);
  if (NULL == *pp_memptr)
  {
    return 1;
  }
  else
  {
    ZeroMemory(*pp_memptr, size);
    return 0;
  }
}

void usleep(int64_t usec)
{
  if(usec < 5000)  //this will be more accurate when less than 5000
  {
    LARGE_INTEGER Count;
    LARGE_INTEGER Count_;
    LARGE_INTEGER Frequency;
    QueryPerformanceCounter(&Count);
    QueryPerformanceFrequency(&Frequency);
    Count_.QuadPart = Count.QuadPart + usec*(Frequency.QuadPart/1000000);
    do
    {
      QueryPerformanceCounter(&Count);
    }while(Count.QuadPart<Count_.QuadPart);
  }
  else
  {
    HANDLE timer = NULL;
    LARGE_INTEGER ft = { 0 };
    BOOL retval = FALSE;

    ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (NULL != timer)
    {
      retval = SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
      if (retval)
      {
        WaitForSingleObject(timer, INFINITE);
      }
    }

    if (timer)
    {
      CloseHandle(timer);
    }
  }
}

void aligned_free(void* const p_memptr)
{
  if (p_memptr)
  {
    free(p_memptr);
  }
}

#elif __linux__
/*!******************************************************************************
 *  \brief  Get max io transfer size from the kernel
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
uint32_t ni_get_kernel_max_io_size(const char * p_dev)
{
  FILE *p_file = NULL;   /* file pointer*/
  char file_name[KERNEL_NVME_FILE_NAME_MAX_SZ];
  uint32_t max_segments = 0, min_io_size = 0, max_hw_sectors_kb = 0;
  uint32_t io_size = DEFAULT_IO_TRANSFER_SIZE;
  int len = 0, err = 0;
  
  
  if( !p_dev )
  {
    ni_log(NI_LOG_TRACE, "Invalid Arguments\n");
    LRETURN;
  }
  
  len = strlen(p_dev) - 5;
  if(len < MIN_NVME_BLK_NAME_LEN)
  {
    ni_log(NI_LOG_TRACE, "p_dev length is %d\n",len);
    LRETURN;
  }
  
  // Get Max number of segments from /sys 
  memset(file_name,0,sizeof(file_name));
  memcpy(file_name,SYS_PARAMS_PREFIX_PATH,SYS_PREFIX_SZ);
  //start from 5 chars ahead to not copy the "/dev/" since we only need whats after it 
  if (strstr(p_dev, "block"))
  {
    strncat(file_name, (char*)(p_dev + 11) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  else
  {
    strncat(file_name, (char*)(p_dev + 5) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  strncat(file_name, KERNEL_NVME_MAX_SEG_PATH, sizeof(file_name) - SYS_PREFIX_SZ - len);
  ni_log(NI_LOG_TRACE, "file_name  is %s\n",file_name);
  p_file = fopen(file_name,"r");
  if(!p_file)
  {
    ni_log(NI_LOG_ERROR, "Error %d: file_name  failed to open: %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, file_name);
    LRETURN;
  }

  err = fscanf(p_file,"%u",&max_segments);
  if(EOF == err)
  {  
    ni_log(NI_LOG_ERROR, "Error %d: fscanf failed on: %s max_segments\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, file_name);
    LRETURN;
  }
  
  fclose(p_file); 
  p_file = NULL;
  // Get Max segment size from /sys 
  memset(file_name,0,sizeof(file_name));
  memcpy(file_name,SYS_PARAMS_PREFIX_PATH,SYS_PREFIX_SZ);
  if (strstr(p_dev, "block"))
  {
    strncat(file_name, (char*)(p_dev + 11) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  else
  {
    strncat(file_name, (char*)(p_dev + 5) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  strncat(file_name, KERNEL_NVME_MIN_IO_SZ_PATH, sizeof(file_name) - SYS_PREFIX_SZ - len);
  ni_log(NI_LOG_TRACE, "file_name  is %s\n",file_name);
  p_file = fopen(file_name,"r");
  if(!p_file)
  {
    ni_log(NI_LOG_ERROR, "Error %d: file_name  failed to open: %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, file_name);
    LRETURN;
  }
  
  err = fscanf(p_file,"%u",&min_io_size);
  if(EOF == err)
  {
    ni_log(NI_LOG_TRACE, "fscanf failed on: %s min_io_size\n",file_name);
    LRETURN;
  }
  
  fclose(p_file);
  p_file = NULL;
  //Now get max_hw_sectors_kb
  memset(file_name,0,sizeof(file_name));
  memcpy(file_name,SYS_PARAMS_PREFIX_PATH,SYS_PREFIX_SZ);
  if (strstr(p_dev, "block"))
  {
    strncat(file_name, (char*)(p_dev + 11) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  else
  {
    strncat(file_name, (char*)(p_dev + 5) , sizeof(file_name) - SYS_PREFIX_SZ);
  }
  strncat(file_name, KERNEL_NVME_MAX_HW_SEC_KB_PATH, sizeof(file_name) - SYS_PREFIX_SZ - len);
  ni_log(NI_LOG_TRACE, "file_name  is %s\n",file_name);
  p_file = fopen(file_name,"r");
  if(!p_file)
  {
    ni_log(NI_LOG_ERROR, "Error %d: file_name  failed to open: %s\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, file_name);
    LRETURN;
  }
  
  err = fscanf(p_file,"%u",&max_hw_sectors_kb);
  if(EOF == err)
  {
    ni_log(NI_LOG_TRACE, "fscanf failed on: %s min_io_size\n",file_name);
    LRETURN;
  }
  
  if( NI_MIN(min_io_size * max_segments, max_hw_sectors_kb * 1024) > MAX_IO_TRANSFER_SIZE )
  {
    io_size = MAX_IO_TRANSFER_SIZE;
    // ni_log(NI_LOG_DEBUG, "max_io_size is set to: %d because its bigger than maximum limit of: %d\n",io_size, MAX_IO_TRANSFER_SIZE);
  }
  else 
  {
    io_size = NI_MIN( min_io_size * max_segments, max_hw_sectors_kb * 1024);
  }
  
  ni_log(NI_LOG_DEBUG, "\nMAX NVMe IO Size of %d was calculated for this platform and will be used unless overwritten by user settings\n", io_size);
  fflush(stderr);
  
  END;
  
  if(p_file)
  {
    fclose(p_file);
  }
  
  return io_size;
}

#endif

// Set default ni_log_level to be same as ffmpeg ('info')
ni_log_level_t ni_log_level = NI_LOG_INFO;

/*!******************************************************************************
 *  \brief  Set ni_log_level
 *
 *  \param  level log level
 *
 *  \return
 *******************************************************************************/
void ni_log_set_level(ni_log_level_t level)
{
  ni_log_level = level;
}

/*!******************************************************************************
 *  \brief Get ni_log_level
 *
 *  \return ni_log_level
 *******************************************************************************/
ni_log_level_t ni_log_get_level(void)
{
  return ni_log_level;
}

/*!******************************************************************************
 *  \brief Convert ffmpeg log level integer to appropriate ni_log_level_t
 *
 *  \param fflog_level integer representation of FFmpeg log level
 *
 *  \return ni_log_level
 *******************************************************************************/
ni_log_level_t ff_to_ni_log_level(int fflog_level)
{
  ni_log_level_t converted_ni_log_level;
  if (fflog_level >= -8)
  {
    converted_ni_log_level = NI_LOG_NONE;
  }
  if (fflog_level >= 8)
  {
    converted_ni_log_level = NI_LOG_FATAL;
  }
  if (fflog_level >= 16)
  {
    converted_ni_log_level = NI_LOG_ERROR;
  }
  if (fflog_level >= 32)
  {
    converted_ni_log_level = NI_LOG_INFO;
  }
  if (fflog_level >= 48)
  {
    converted_ni_log_level = NI_LOG_DEBUG;
  }
  if (fflog_level >= 56)
  {
    converted_ni_log_level = NI_LOG_TRACE;
  }
  return converted_ni_log_level;
}

// memory buffer pool operations (one use is for decoder frame buffer pool)
//

// expand buffer pool by a pre-defined size
ni_buf_t *ni_buf_pool_expand(ni_buf_pool_t *pool)
{
  int32_t i;
  for (i = 0; i < NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND; i++)
  {
    if( NULL == ni_buf_pool_allocate_buffer(pool, pool->buf_size) )
    {
      ni_log(NI_LOG_ERROR, "FATAL ERROR: Failed to expand allocate pool buffer for "
              "pool :%p  current size: %d\n", pool, pool->number_of_buffers);
      return NULL;
    }
  }
  pool->number_of_buffers += NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND;
  return pool->p_free_head;
}

// get a free memory buffer from the pool
ni_buf_t *ni_buf_pool_get_buffer(ni_buf_pool_t *p_buffer_pool)
{
  ni_buf_t *buf = NULL;

  if (NULL == p_buffer_pool)
  {
    return NULL;
  }

  pthread_mutex_lock(&(p_buffer_pool->mutex));
  buf = p_buffer_pool->p_free_head;

  // find and return a free buffer
  if (NULL == buf)
  {
    ni_log(NI_LOG_ERROR, "Expanding dec fme buffer_pool from %d to %d \n",
            p_buffer_pool->number_of_buffers,
            p_buffer_pool->number_of_buffers+NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND);

    buf = ni_buf_pool_expand(p_buffer_pool);

    if (NULL == buf)
    {
      pthread_mutex_unlock(&(p_buffer_pool->mutex));
      return NULL;
    }
  }

  // remove it from free list head; reconnect the linked list, the p_next
  // will become the new head now
  p_buffer_pool->p_free_head = buf->p_next_buffer;

  if ( NULL != buf->p_next_buffer )
  {
    buf->p_next_buffer->p_previous_buffer = NULL;
  }
  else
  {
    p_buffer_pool->p_free_tail = NULL;
  }

  // add it to the used list tail
  buf->p_previous_buffer = p_buffer_pool->p_used_tail;
  buf->p_next_buffer = NULL;

  if ( NULL != p_buffer_pool->p_used_tail )
  {
    p_buffer_pool->p_used_tail->p_next_buffer = buf;
  }
  else
  {
    p_buffer_pool->p_used_head = buf;
  }

  p_buffer_pool->p_used_tail = buf;

  pthread_mutex_unlock(&(p_buffer_pool->mutex));

  ni_log(NI_LOG_TRACE, "ni_buf_pool_get_buffer ptr %p  buf %p\n",
                 buf->buf, buf);
  return buf;
}

// return a used memory buffer to the pool
void ni_buf_pool_return_buffer(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool)
{
  // p_buffer_pool could be null in case of delayed buffer return after pool
  // has been freed
  if(! buf)
  {
    return;
  }

  ni_log(NI_LOG_TRACE, "ni_buf_pool_return_buffer ptr %p  buf %p\n",
                 buf->buf, buf);
  
  if (! p_buffer_pool)
  {
    ni_log(NI_LOG_TRACE, "ni_buf_pool_return_buffer: pool already freed, self destroy\n");
    free(buf->buf);
    free(buf);
    return;
  }

  pthread_mutex_lock(&(p_buffer_pool->mutex));

  // remove buf from the used list
  if (NULL != buf->p_previous_buffer)
  {
    buf->p_previous_buffer->p_next_buffer = buf->p_next_buffer;
  }
  else
  {
    p_buffer_pool->p_used_head = buf->p_next_buffer;
  }

  if (NULL != buf->p_next_buffer)
  {
    buf->p_next_buffer->p_previous_buffer = buf->p_previous_buffer;
  }
  else
  {
    p_buffer_pool->p_used_tail = buf->p_previous_buffer;
  }

  // put it on the tail of free buffers list
  buf->p_previous_buffer = p_buffer_pool->p_free_tail;
  buf->p_next_buffer = NULL;

  if (NULL != p_buffer_pool->p_free_tail)
  {
    p_buffer_pool->p_free_tail->p_next_buffer = buf;
  }
  else
  {
    p_buffer_pool->p_free_head = buf;
  }

  p_buffer_pool->p_free_tail = buf;

  // shrink the buffer pool by EXPAND size if free list size is at least
  // 2x EXPAND to manage buffer pool expansion
  int count = 0;
  ni_buf_t *p_tmp = p_buffer_pool->p_free_head;
  while (p_tmp)
  {
    count++;
    p_tmp = p_tmp->p_next_buffer;
  }

  if (count >= 2 * NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND)
  {
    ni_log(NI_LOG_INFO, "ni_buf_pool_return_buffer shrink buf pool free size"
           " from %d by %d\n", count, NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND);

    p_tmp = p_buffer_pool->p_free_head;
    while (p_tmp && count > NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND)
    {
      p_buffer_pool->p_free_head = p_tmp->p_next_buffer;
      free(p_tmp->buf);
      free(p_tmp);
      p_tmp = p_buffer_pool->p_free_head;
      count--;
      p_buffer_pool->number_of_buffers--;
    }
  }

  pthread_mutex_unlock(&(p_buffer_pool->mutex));
}

// allocate a memory buffer and place it in the pool
ni_buf_t *ni_buf_pool_allocate_buffer(ni_buf_pool_t *p_buffer_pool,
                                      int buffer_size)
{
  ni_buf_t *p_buffer = NULL;
  void *p_buf = NULL;

  if (NULL != p_buffer_pool &&
      (p_buffer = (ni_buf_t *)malloc(sizeof(ni_buf_t))) != NULL)
  {
    // init the struct
    memset(p_buffer, 0, sizeof(ni_buf_t));

    if (posix_memalign(&p_buf, sysconf(_SC_PAGESIZE), buffer_size))
    {
      free(p_buffer);
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_buf_pool_allocate_buffer() failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return NULL;
    }
    ni_log(NI_LOG_TRACE, "ni_buf_pool_allocate_buffer ptr %p  buf %p\n",
                   p_buf, p_buffer);
    p_buffer->buf = p_buf;
    p_buffer->pool = p_buffer_pool;

    // add buffer to the buf pool list
    p_buffer->p_prev = NULL;
    p_buffer->p_next = NULL;
    p_buffer->p_previous_buffer = p_buffer_pool->p_free_tail;

    if (p_buffer_pool->p_free_tail != NULL)
    {
      p_buffer_pool->p_free_tail->p_next_buffer = p_buffer;
    }
    else
    {
      p_buffer_pool->p_free_head = p_buffer;
    }

    p_buffer_pool->p_free_tail = p_buffer;
  }

  return p_buffer;
}

// decoder frame buffer pool init & free
int32_t ni_dec_fme_buffer_pool_initialize(ni_session_context_t* p_ctx,
                                          int32_t number_of_buffers,
                                          int width, int height,
                                          int height_align, int factor)
{
  int32_t i;
  int width_aligned = width;
  int height_aligned = height;

  ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_initialize: enter\n");

  width_aligned = ((width + 31) / 32) * 32;
  height_aligned = ((height + 7) / 8) * 8;
  if (height_align)
  {
    height_aligned = ((height + 15) / 16) * 16;
  }

  int luma_size = width_aligned * height_aligned * factor;
  int chroma_b_size = luma_size / 4;
  int chroma_r_size = chroma_b_size;
  int buffer_size = luma_size + chroma_b_size + chroma_r_size +
  NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;

  // added 2 blocks of 512 bytes buffer space to handle any extra metadata
  // retrieval from fw
  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT * 3;

  if (p_ctx->dec_fme_buf_pool != NULL)
  {
    ni_log(NI_LOG_TRACE, "Warning init dec_fme Buf pool already with size %d\n",
                   p_ctx->dec_fme_buf_pool->number_of_buffers);

    if (buffer_size > p_ctx->dec_fme_buf_pool->buf_size)
    {
      ni_log(NI_LOG_ERROR, "Warning resolution %dx%d memory buffer size %d > %d "
              "(existing buffer size), re-allocating !\n", width, height,
              buffer_size, p_ctx->dec_fme_buf_pool->buf_size);

      ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
    }
    else
    {
      ni_log(NI_LOG_ERROR, "INFO resolution %dx%d memory buffer size %d <= %d "
              "(existing buffer size), continue !\n", width, height,
              buffer_size, p_ctx->dec_fme_buf_pool->buf_size);
      return 0;
    }
  }

  if ((p_ctx->dec_fme_buf_pool = (ni_buf_pool_t *)
       malloc(sizeof(ni_buf_pool_t))) == NULL)
  {
    ni_log(NI_LOG_ERROR, "Error %d: alloc for dec fme buf pool\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return -1;
  }

  // init the struct
  memset(p_ctx->dec_fme_buf_pool, 0, sizeof(ni_buf_pool_t));
  pthread_mutex_init(&(p_ctx->dec_fme_buf_pool->mutex), NULL);
  p_ctx->dec_fme_buf_pool->number_of_buffers = number_of_buffers;

  ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_initialize: entries %d  entry size "
                 "%d\n", number_of_buffers, buffer_size);

  p_ctx->dec_fme_buf_pool->buf_size = buffer_size;
  for (i = 0; i < number_of_buffers; i++)
  {
    if (NULL == ni_buf_pool_allocate_buffer(
          p_ctx->dec_fme_buf_pool, buffer_size))
    {
      // release everything we have allocated so far and exit
      ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
      return -1;
    }
  }

  ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_initialize: exit\n");
  return 0;
}

void ni_dec_fme_buffer_pool_free(ni_buf_pool_t *p_buffer_pool)
{
  ni_buf_t *buf, *p_next;
  int32_t count_free = 0;

  if (p_buffer_pool)
  {
    ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_free: enter.\n");

    // mark used buf not returned at pool free time by setting pool ptr in used
    // buf to NULL, so they will self-destroy when time is due eventually
    pthread_mutex_lock(&(p_buffer_pool->mutex));

    if (buf = p_buffer_pool->p_used_head)
    {
      p_next = buf;
      while (buf)
      {
        p_next = buf->p_next_buffer;
        ni_log(NI_LOG_TRACE, "Release ownership of ptr %p buf %p\n", buf->buf, buf);

        buf->pool = NULL;
        buf = p_next;
      }
    }
    pthread_mutex_unlock(&(p_buffer_pool->mutex));

    buf = p_buffer_pool->p_free_head;
    p_next = buf;
    // free all the buffers in the free list
    while (buf)
    {
      p_next = buf->p_next_buffer;
      free(buf->buf);
      free(buf);
      buf = p_next;
      count_free++;
    }

    if (count_free != p_buffer_pool->number_of_buffers)
    {
      ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_free free %d  != number_of_buffers"
                     " %d\n", count_free, p_buffer_pool->number_of_buffers);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "ni_dec_fme_buffer_pool_free all buffers freed: %d.\n",
                     count_free);
    }
    free(p_buffer_pool);
    p_buffer_pool = NULL;
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ni_dec_fme_buffer_pool_free: NOT allocated\n");
  }
}

void ni_buffer_pool_free(ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *buf, *p_next;
  int32_t count = 0;

  if (p_buffer_pool)
  {
    buf = p_buffer_pool->p_free_head;
    p_next = buf;
    // free all the buffers in the free and used list
    while (buf)
    {
      p_next = buf->p_next_buffer;
      free(buf);
      buf = p_next;
      count++;
    }

    buf = p_buffer_pool->p_used_head;
    p_next = buf;
    while (buf)
    {
      p_next = buf->p_next_buffer;
      free(buf);
      buf = p_next;
      count++;
    }

    if (count != p_buffer_pool->number_of_buffers)
    {
      ni_log(NI_LOG_ERROR, "??? freed %d != number_of_buffers %d\n", count, p_buffer_pool->number_of_buffers);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "p_buffer_pool freed %d buffers.\n", count);
    }
    free(p_buffer_pool);
    p_buffer_pool = NULL;
    ni_log(NI_LOG_TRACE, "ni_buffer_pool_free: enter.\n");
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ni_buffer_pool_free: NOT allocated\n");
  }
}

ni_queue_node_t *ni_buffer_pool_allocate_buffer(ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *p_buffer = NULL;

  if ( (NULL != p_buffer_pool) && ( (p_buffer = (ni_queue_node_t *)malloc(sizeof(ni_queue_node_t))) != NULL) )
  {
    //Inititalise the struct
    memset(p_buffer,0,sizeof(ni_queue_node_t));
    // add buffer to the buf pool list
    p_buffer->p_prev = NULL;
    p_buffer->p_next = NULL;
    p_buffer->p_previous_buffer = p_buffer_pool->p_free_tail;
    
    if (p_buffer_pool->p_free_tail != NULL)
    {
      p_buffer_pool->p_free_tail->p_next_buffer = p_buffer;
    }
    else
    {
      p_buffer_pool->p_free_head = p_buffer;
    }

    p_buffer_pool->p_free_tail = p_buffer;
  }

  return p_buffer;
}

int32_t ni_buffer_pool_initialize(ni_session_context_t* p_ctx, int32_t number_of_buffers)
{
  int32_t i;
  ni_log(NI_LOG_TRACE, "ni_buffer_pool_initialize: enter\n");
  if (p_ctx->buffer_pool != NULL)
  {
    ni_log(NI_LOG_TRACE, "Warn init Buf pool already with size %d\n",
           p_ctx->buffer_pool->number_of_buffers);
    return -1;
  }

  if ((p_ctx->buffer_pool = (ni_queue_buffer_pool_t *)
      malloc(sizeof(ni_queue_buffer_pool_t))) == NULL)
  {
    ni_log(NI_LOG_ERROR, "Error %d: alloc for pool\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return -1;
  }

  //initialise the struct
  memset(p_ctx->buffer_pool,0,sizeof(ni_queue_buffer_pool_t));
  p_ctx->buffer_pool->number_of_buffers = number_of_buffers;
  //p_buffer_pool->p_free_head = NULL;
  //p_buffer_pool->p_free_tail = NULL;
  //p_buffer_pool->p_used_head = NULL;
  //p_buffer_pool->p_used_tail = NULL;

  for (i = 0; i < number_of_buffers; i++)
  {
    if (NULL == ni_buffer_pool_allocate_buffer(p_ctx->buffer_pool))
    {
      //Release everything we have allocated so far and exit
      ni_buffer_pool_free(p_ctx->buffer_pool);
      return -1;
    }
  }

  return 0;
}

ni_queue_node_t *ni_buffer_pool_expand(ni_queue_buffer_pool_t *pool)
{
  int32_t i;
  for (i = 0; i < 200; i++)
  {
    //TODO: What happens is ni_buffer_pool_allocate_buffer fails and returns a NULL pointer?
    if( NULL == ni_buffer_pool_allocate_buffer(pool) )
    {
      ni_log(NI_LOG_ERROR, "FATAL ERROR: Failed to allocate pool buffer for pool :%p\n", pool);
      return NULL;
    }
  }
  pool->number_of_buffers += 200;
  return pool->p_free_head;
}

ni_queue_node_t *ni_buffer_pool_get_queue_buffer(ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *buf = NULL;
  
  if( NULL == p_buffer_pool)
  {
    return NULL;
  }

  buf = p_buffer_pool->p_free_head;
  
  // find and return a free buffer
  if ( NULL == buf )
  {
    ni_log(NI_LOG_ERROR, "Expanding p_buffer_pool from %d to %d \n",
            p_buffer_pool->number_of_buffers, p_buffer_pool->number_of_buffers + 200);
    buf = ni_buffer_pool_expand(p_buffer_pool);
    if (NULL == buf)
    {
      return NULL; //return null otherwise there will be null derefferencing later 
    }
  }

  buf->checkout_timestamp = time(NULL);
  // remove it from free list head; reconnect the linked list, the p_next
  // will become the new head now
  p_buffer_pool->p_free_head = buf->p_next_buffer;

  if ( NULL != buf->p_next_buffer )
  {
    buf->p_next_buffer->p_previous_buffer = NULL;
  }
  else
  {
    p_buffer_pool->p_free_tail = NULL;
  }

  // add it to the used list tail
  buf->p_previous_buffer = p_buffer_pool->p_used_tail;
  buf->p_next_buffer = NULL;

  if ( NULL != p_buffer_pool->p_used_tail )
  {
    p_buffer_pool->p_used_tail->p_next_buffer = buf;
  }
  else
  {
    p_buffer_pool->p_used_head = buf;
  }

  p_buffer_pool->p_used_tail = buf;

  return buf;
}

void ni_buffer_pool_return_buffer(ni_queue_node_t *buf, ni_queue_buffer_pool_t *p_buffer_pool)
{
  
  if( (!buf) || !(p_buffer_pool) )
  {
    return;
  }

  // remove buf from the used list
  if ( NULL != buf->p_previous_buffer )
  {
    buf->p_previous_buffer->p_next_buffer = buf->p_next_buffer;
  }
  else
  {
    p_buffer_pool->p_used_head = buf->p_next_buffer;
  }

  if ( NULL != buf->p_next_buffer )
  {
    buf->p_next_buffer->p_previous_buffer = buf->p_previous_buffer;
  }
  else
  {
    p_buffer_pool->p_used_tail = buf->p_previous_buffer;
  }

  // put it on the tail of free buffers list
  buf->p_previous_buffer = p_buffer_pool->p_free_tail;
  buf->p_next_buffer = NULL;

  if ( NULL != p_buffer_pool->p_free_tail )
  {
    p_buffer_pool->p_free_tail->p_next_buffer = buf;
  }
  else
  {
    p_buffer_pool->p_free_head = buf;
  }

  p_buffer_pool->p_free_tail = buf;
}

/*!******************************************************************************
 *  \brief  Remove a string-pattern from a string in-place.
 *
 *  \param[in,out] main_str Null terminated array of characters to operate upon in-place.
 *  \param[in] pattern Null terminated array of characters to remove from main_str.
 *                     Supports special characters '#' and '+' for digit matching and
 *                     repeated matching respectively. Note, there is no way to \a escape
 *                     the special characters.
 *                     \b Example:
 *                     char a_str[10] = "aaa123qwe";
 *                     char b_str[5] = "a+#+";
 *                     remove_substring_pattern(a_str, b_str);
 *                     printf("%s\n", a_str);
 *                     \b Output:
 *                     qwe
 *
 *  \return If characters removed, returns 1
 *          If no characters removed, returns 0
 *******************************************************************************/
uint32_t remove_substring_pattern(char *main_str, const char *pattern)
{
  uint32_t i, j;                     // for loop counters
  uint32_t match_length;             // length of matching substring
  uint32_t matched_chr;              // boolean flag for when match is found for a character in the pattern
  char char_match_pattern[11] = "";  // what characters to look for when evaluating a character in main_str
  uint32_t pattern_matched = 0;      // boolean flag for when who pattern match is found
  uint32_t pattern_start = 0;        // starting index in main_str of substring matching pattern
  const char digit_match_pattern[11] = "0123456789";  // set of numeric digits for expansion of special character '#'
  
  // do not accept zero length main_str or pattern
  if((!main_str) || (!pattern) || (!*main_str) || (!*pattern))
  {
    return 0;
  }
  
  // iterate over all characters in main_str looking for starting index of matching pattern
  for (i = 0; i < strlen(main_str) && !pattern_matched; i++)
  {
    pattern_matched = 0;
    match_length = 0;
    // iterate over the characters of the pattern
    for (j = 0; j < strlen(pattern); j++)
    {
      matched_chr = 0;
      // set which characters to look for, special cases for special control characters
      if (pattern[j] == '+')
      {
        // immediately fail as entering this branch means either the first character is a '+', or a '+" following a "+'
        return 0;
      }
      else if (pattern[j] == '#')
      {
        memcpy(char_match_pattern, digit_match_pattern, strlen(digit_match_pattern) + 1);
      }
      else
      {
        memcpy(char_match_pattern, pattern + j, 1);
        memset(char_match_pattern + 1, 0, 1);
      }
      // check if char is in match_pattern
      if (pattern[j+1] == '+')
      {
        while (main_str[i + match_length] && strchr(char_match_pattern, (int) main_str[i + match_length]))
        {
          match_length++;
          matched_chr = 1;
        }
        j++;
      }
      else if (main_str[i + match_length] && strchr(char_match_pattern, (int) main_str[i + match_length]))
      {
        match_length++;
        matched_chr = 1;
      }
      // if no matches were found, then this segment is not the sought pattern
      if (!matched_chr)
      {
        break;
      }
      // if all of pattern has been processed and matched, mark sucessful whole pattern match
      else if ((j + 1) >= strlen(pattern))
      {
        pattern_matched = 1;
        pattern_start = i;
      }
    }
  }

  // remove sub-string if its pattern was found in main_str
  if (pattern_matched)
  {
    uint32_t orig_main_str_len = strlen(main_str);
    memmove(main_str + pattern_start, main_str + pattern_start + match_length, strlen(main_str + pattern_start + match_length));
    main_str[orig_main_str_len - match_length] = 0;
    return 1;
  }
  else
  {
    return 0;
  }
}

#ifdef _ANDROID
/*!******************************************************************************
 *	\brief	use cmd to search nvme block file
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[in] search cmd
 *  \param[in] cmd_ret length
 *  \param[out] cmd_ret search result for nvme block file
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE
 *          On failure returns NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
static ni_retcode_t ni_search_file(const char *p_dev, char *cmd, char *cmd_ret, int cmd_ret_len)
{
  FILE *cmd_fp;

  if (access(p_dev, F_OK) == -1)
  {
    return NI_RETCODE_FAILURE;
  }

  // look for child block in sysfs mapping tree
  cmd_fp = popen(cmd, "r");
  if (!cmd_fp)
  {
    return NI_RETCODE_FAILURE;
  }

  if (fgets(cmd_ret, cmd_ret_len, cmd_fp) == 0)
  {
    return NI_RETCODE_INVALID_PARAM;
  }

  return NI_RETCODE_SUCCESS;
}
#endif

/*!******************************************************************************
 *  \brief  Find NVMe name space block from device name
 *          If none is found, assume nvme multi-pathing is disabled and return /dev/nvmeXn1
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[out] p_out_buf Output buffer to put NVMe name space block. Must be at least length 21
 *  \param[in] out_buf_len Length of memory allocated to p_out_buf
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_find_blk_name(const char *p_dev, char *p_out_buf, int out_buf_len)
{
  if( (!p_dev) || (!p_out_buf) )
  {
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef _WIN32
  ni_log(NI_LOG_TRACE, "In Windows block name equals to device name\n");
  snprintf(p_out_buf, out_buf_len, "%s", p_dev);
  return NI_RETCODE_SUCCESS;
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
  ni_log(NI_LOG_TRACE, "The device is already considered as a block divice in Linux virtual machine with VirtIO driver.\n");
  snprintf(p_out_buf, out_buf_len, "%s", p_dev);
  return NI_RETCODE_SUCCESS;
#else
  FILE *cmd_fp;
  char cmd[128] = "";
  char cmd_ret[22] = "";

#ifdef _ANDROID
  // assumes no indexing differences between sysfs and udev on Android (ie. no nvme multi-pathing)
  snprintf(cmd, sizeof(cmd) - 1, "ls /sys/class/nvme/%s/ | grep %s", &p_dev[5], NI_NVME_PREFIX);  // p_dev[5] is p_dev without '/dev/'
#else
  // Note, we are using udevadm through terminal instead of libudev.c to avoid requiring extra kernel dev packges
  snprintf(cmd, sizeof(cmd) - 1, "ls /sys/`udevadm info -q path -n %s` | grep -m 1 -P \"%s(\\d+c)?\\d+n\\d+\"", p_dev, NI_NVME_PREFIX);
#endif

  // check p_dev exists in /dev/ folder. If not, return NI_RETCODE_FAILURE
  if (access(p_dev, F_OK) == -1)
  {
    return NI_RETCODE_FAILURE;
  }

  // look for child block in sysfs mapping tree
  cmd_fp = popen(cmd, "r");
  if(!cmd_fp)
  {
    return NI_RETCODE_FAILURE;
  }

  if (fgets(cmd_ret, sizeof(cmd_ret)/sizeof(cmd_ret[0]), cmd_fp) == 0)
  {
    ni_log(NI_LOG_TRACE, "Failed to find namespaceID. Using guess.\n");
    snprintf(p_out_buf, out_buf_len, "%sn1", p_dev);
  }
  else
  {
    cmd_ret[strcspn(cmd_ret, "\r\n")] = 0;
#ifdef _ANDROID
    ni_retcode_t ret = NI_RETCODE_SUCCESS;
    snprintf(cmd, sizeof(cmd) - 1, "ls /dev/ | grep %s", cmd_ret); // cmd_ret is block device name
    int cmd_ret_len = sizeof(cmd_ret)/sizeof(cmd_ret[0]);
    ret = ni_search_file(p_dev, cmd, cmd_ret, cmd_ret_len);
    if (ret == NI_RETCODE_SUCCESS)
    {
      char *tmp = NULL;
      if ((tmp = strstr(cmd_ret, "\n")))
      {
        *tmp = '\0';
      }
      remove_substring_pattern(cmd_ret, "c#+");
      snprintf(p_out_buf, out_buf_len, "/dev/%s", cmd_ret);
    }
    else if (ret == NI_RETCODE_INVALID_PARAM)
    {
      snprintf(cmd, sizeof(cmd) - 1, "ls /dev/block/ | grep %s", cmd_ret); // cmd_ret is block device name
      ret = ni_search_file(p_dev, cmd, cmd_ret, cmd_ret_len);
      if (ret == NI_RETCODE_SUCCESS)
      {
        char *tmp = NULL;
        if ((tmp = strstr(cmd_ret, "\n")))
        {
          *tmp = '\0';
        }
        remove_substring_pattern(cmd_ret, "c#+");
        snprintf(p_out_buf, out_buf_len, "/dev/block/%s", cmd_ret);
      }
      else if (ret == NI_RETCODE_INVALID_PARAM)
      {
        ni_log(NI_LOG_ERROR, "Error: ni_find_blk_name can not find block device %s\n", cmd_ret);
      }
      else
      {
        return ret;
      }
    }
    else
    {
      return ret;
    }
#else
    // On systems with virtualized NVMe functions sysfs will include context index in namespaceID. But, udev will not.
    // Thus, it will be necessary to remove the context index from the namespaceID when mapping deviceID to namespaceID through sysfs.
    // ex. change "/dev/nvme0c0n2" to "/dev/nvme0n2"
    remove_substring_pattern(cmd_ret, "c#+");
    snprintf(p_out_buf, out_buf_len, "/dev/%s", cmd_ret);
#endif
  }
  
  pclose(cmd_fp);

  return NI_RETCODE_SUCCESS;
#endif
}

/*!******************************************************************************
 *  \brief  Initialize timestamp handling 
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_init(ni_session_context_t* p_ctx, ni_timestamp_table_t **pp_table, const char *name)
{
  ni_timestamp_table_t *ptemp;

  ni_log(NI_LOG_TRACE, "ni_timestamp_init: enter\n");

  if (*pp_table != NULL)
  {
    ni_log(NI_LOG_TRACE, "ni_timestamp_init: previously allocated, reallocating now\n");
    ni_queue_free(&(*pp_table)->list, p_ctx->buffer_pool);
    free(*pp_table);
  }
  ni_log(NI_LOG_TRACE, "ni_timestamp_init: Malloc\n");
  ptemp = (ni_timestamp_table_t *)malloc(sizeof(ni_timestamp_table_t));
  if (!ptemp)
  {
    ni_log(NI_LOG_ERROR, "Error %d: ni_timestamp_init\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }
  
  //initialise the struct
  memset(ptemp,0,sizeof(ni_timestamp_table_t));

  ni_queue_init(p_ctx, &ptemp->list, name); //buffer_pool_initialize runs in here

  *pp_table = ptemp;

  ni_log(NI_LOG_TRACE, "ni_timestamp_init: success\n");

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Clean up timestamp handling
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_done(ni_timestamp_table_t *p_table, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_log(NI_LOG_TRACE, "ni_timestamp_done: enter\n");

  if (!p_table)
  {
    ni_log(NI_LOG_TRACE, "ni_timestamp_done: no pts table to free\n");
    return NI_RETCODE_SUCCESS;
  }
  ni_queue_free(&p_table->list, p_buffer_pool);

  free(p_table);
  p_table = NULL;
  ni_log(NI_LOG_TRACE, "ni_timestamp_done: success\n");

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Register timestamp in timestamp/frameoffset table
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_register(ni_queue_buffer_pool_t *p_buffer_pool, ni_timestamp_table_t *p_table, int64_t timestamp, uint64_t data_info)
{
  ni_retcode_t err = NI_RETCODE_SUCCESS;

  err = ni_queue_push(p_buffer_pool, &p_table->list, data_info, timestamp);
  
  if(NI_RETCODE_SUCCESS == err)
  {
    ni_log(NI_LOG_TRACE, "ni_timestamp_register: success\n");
  }
  else 
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_timestamp_register: FAILED\n");
  }

  return err;
}

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frameoffset info
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_retcode_t err = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "ni_timestamp_get: getting timestamp with frame_info=%" PRId64 "\n", frame_info);

  err = ni_queue_pop( &p_table->list, frame_info, p_timestamp, threshold, print, p_buffer_pool);
  if(NI_RETCODE_SUCCESS != err)
  {
    ni_log(NI_LOG_TRACE, "ni_timestamp_get: error getting timestamp\n");
  }

  ni_log(NI_LOG_TRACE, "ni_timestamp_get: timestamp=%" PRId64 ", frame_info=%" PRId64 ", err=%d\n", *p_timestamp, frame_info, err);

  return err;
}

ni_retcode_t ni_timestamp_get_with_threshold(ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
  return ( ni_queue_pop_threshold(&p_table->list, frame_info, p_timestamp, threshold, print, p_buffer_pool) );
}

void ni_timestamp_scan_cleanup(ni_timestamp_table_t *pts_list,
                                   ni_timestamp_table_t *dts_list,
                                ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_t *pts_q = &pts_list->list;
  ni_queue_t *dts_q = &dts_list->list;
  ni_queue_node_t *temp = pts_q->p_first;
  ni_queue_node_t *p_prev = NULL;
  time_t now = time(NULL);
  int64_t dts;
  
  if( (!pts_list) || (!dts_list) )
  {
    return;
  }

  while (temp)
  {
    if (now - temp->checkout_timestamp > 30)
    {
      // remove temp from pts and pop dts
      if (pts_q->p_first == pts_q->p_last)
      {
        // if only one entry
        ni_buffer_pool_return_buffer(pts_q->p_first, p_buffer_pool);
        pts_q->p_first = NULL;
        pts_q->p_last = NULL;
        temp = NULL;
      }
      else if (!p_prev)
      {
        // the p_first one
        pts_q->p_first = temp->p_next;
        temp->p_next->p_prev = NULL;
        ni_buffer_pool_return_buffer(temp, p_buffer_pool);

        temp = pts_q->p_first;
        p_prev = NULL;
      }
      else
      {
        p_prev->p_next = temp->p_next;
        if (temp->p_next)
        {
          temp->p_next->p_prev = p_prev;
        }
        else
        {
          pts_q->p_last = p_prev;
        }
        ni_buffer_pool_return_buffer(temp, p_buffer_pool);

        temp = p_prev->p_next;
      }
      pts_q->count--;
      ni_timestamp_get_with_threshold(dts_list, 0, &dts,
                                          XCODER_FRAME_OFFSET_DIFF_THRES, 0, p_buffer_pool);
    }
    else
    {
      break; // p_first one should be the oldest one
      p_prev = temp;
      temp = temp->p_next;
    }
  }
}

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frameoffset info
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get_v2(ni_timestamp_table_t *p_table, uint64_t frame_offset, int64_t *p_timestamp, int32_t threshold, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_retcode_t err = NI_RETCODE_SUCCESS;
  
  if( (!p_table) || (!p_timestamp) || (!p_buffer_pool) )
  {
    err = NI_RETCODE_INVALID_PARAM; 
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "ni_timestamp_get: getting timestamp with frame_offset=%" PRId64 "\n", frame_offset);

  err = ni_queue_pop(&p_table->list, frame_offset, p_timestamp, threshold, 0, p_buffer_pool);
  if( NI_RETCODE_SUCCESS != err)
  {
    ni_log(NI_LOG_TRACE, "ni_timestamp_get: error getting timestamp\n");
  }

  ni_log(NI_LOG_TRACE, "ni_timestamp_get: timestamp=%" PRId64 ", frame_offset=%" PRId64 ", err=%d\n", *p_timestamp, frame_offset, err);
  
  END;

  return err;
}

/*!******************************************************************************
 *  \brief  Initialize xcoder queue
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_init(ni_session_context_t* p_ctx, ni_queue_t *p_queue, const char *name)
{
  ni_log(NI_LOG_TRACE, "ni_queue_init: enter\n");
  
  if ( (!p_queue) || (!name) )
  {
    return NI_RETCODE_INVALID_PARAM;
  }
  p_queue->name[0] = '\0';
  strncat(p_queue->name, name, strlen(name));
  ni_buffer_pool_initialize(p_ctx, BUFFER_POOL_SZ_PER_CONTEXT);

  p_queue->p_first = NULL;
  p_queue->p_last = NULL;
  p_queue->count = 0;
  
  ni_log(NI_LOG_TRACE, "ni_queue_init: exit\n");

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Push into xcoder queue
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_push(ni_queue_buffer_pool_t *p_buffer_pool, ni_queue_t *p_queue, uint64_t frame_info, int64_t timestamp)
{
  ni_retcode_t err = NI_RETCODE_SUCCESS;
  ni_queue_node_t *temp = NULL;
  
  if ( !p_queue )
  {
    ni_log(NI_LOG_TRACE, "ni_queue_push: error, null pointer parameters passed\n");
    err = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  temp = ni_buffer_pool_get_queue_buffer(p_buffer_pool);

  if (!temp)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_push: error, cannot allocate memory\n");
    err = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  //ni_log(NI_LOG_TRACE, "ni_queue_push enter: p_first=%"PRId64", p_last=%"PRId64", count=%d\n", p_queue->p_first, p_queue->p_last, p_queue->count);

  temp->timestamp = timestamp;
  temp->frame_info = frame_info;

  temp->p_next = NULL;

  if (!p_queue->p_first)
  {
    p_queue->p_first = p_queue->p_last = temp;
    p_queue->p_first->p_prev = NULL;
  }
  else
  {
    p_queue->p_last->p_next = temp;
    temp->p_prev = p_queue->p_last;
    p_queue->p_last = temp;
  }

  p_queue->count++;

  //ni_log(NI_LOG_TRACE, "ni_queue_push exit: p_first=%"PRId64", p_last=%"PRId64", count=%d\n", p_queue->p_first, p_queue->p_last, p_queue->count);

  if (p_queue->count > XCODER_MAX_NUM_QUEUE_ENTRIES)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_push: queue overflow, remove oldest entry, count=%d\n", p_queue->count);
    ni_assert(0);
    //Remove oldest one
    temp = p_queue->p_first->p_next;
    // free(p_queue->p_first);
    ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);
    p_queue->p_first = temp;
    p_queue->p_first->p_prev = NULL;
    p_queue->count--;
  }

  END;

  return err;
}

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_pop(ni_queue_t *p_queue, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *temp;
  ni_queue_node_t *temp_prev = NULL;
  int32_t found = 0;
  int32_t count = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  
  if ( (!p_queue) || (!p_timestamp) )
  {
    ni_log(NI_LOG_TRACE, "ni_queue_push: error, null pointer parameters passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if ( NULL == p_queue->p_first )
  {
    ni_log(NI_LOG_TRACE, "ni_queue_pop: queue is empty...\n");
    retval =  NI_RETCODE_FAILURE;
    LRETURN;
  }

  if (p_queue->p_first == p_queue->p_last)
  {
    /*! If only one entry, retrieve timestamp without checking */
    *p_timestamp = p_queue->p_first->timestamp;
    // free(p_queue->p_first);
    ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);

    p_queue->p_first = NULL;
    p_queue->p_last = NULL;
    p_queue->count--;
    ni_assert(p_queue->count == 0);
    found = 1;
  }
  else
  {
    temp = p_queue->p_first;
    while (temp && !found)
    {
      if (frame_info < temp->frame_info)
      {
        if (!temp->p_prev)
        {
          ni_log(NI_LOG_TRACE, "First in ts list, return it\n");
          *p_timestamp = temp->timestamp;

          p_queue->p_first = temp->p_next;
          temp->p_next->p_prev = NULL;

          ni_buffer_pool_return_buffer(temp, p_buffer_pool);
          p_queue->count--;
          found = 1;
          break;
        }

        // retrieve from p_prev and delete p_prev !
        *p_timestamp = temp->p_prev->timestamp;
        temp = temp->p_prev;
        temp_prev = temp->p_prev;

        if (!temp_prev)
        {
          p_queue->p_first = temp->p_next;
          temp->p_next->p_prev = NULL;
        }
        else
        {
          temp_prev->p_next = temp->p_next;
          if (temp->p_next)
          {
            temp->p_next->p_prev = temp_prev;
          }
          else
          {
            p_queue->p_last = temp_prev;
          }
        }
        //free(temp);
        ni_buffer_pool_return_buffer(temp, p_buffer_pool);
        p_queue->count--;
        found = 1;
        break;
      }
      temp_prev = temp;
      temp = temp->p_next;
      count++;
    }
  }

  if (print)
  {
      ni_log(NI_LOG_TRACE, "ni_queue_pop %s %d iterations ..\n", p_queue->name, count);
  }
  if (p_queue->count < 0)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_pop: error p_queue->count=%d\n", p_queue->count);
    ni_assert(0);
  }

  if (!found)
  {
      retval = NI_RETCODE_FAILURE;
  }

  END;

  return retval;
}

ni_retcode_t ni_queue_pop_threshold(ni_queue_t *p_queue, uint64_t frame_info, int64_t *p_timestamp, int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *temp;
  ni_queue_node_t *temp_prev = NULL;
  int32_t found = 0;
  int32_t count = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  
  if ( (!p_queue) || (!p_timestamp) )
  {
    ni_log(NI_LOG_TRACE, "ni_queue_push: error, null pointer parameters passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_queue->p_first == NULL)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_pop: queue is empty...\n");
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }

  if (p_queue->p_first == p_queue->p_last)
  {
    /*! If only one entry, retrieve timestamp without checking */
    *p_timestamp = p_queue->p_first->timestamp;
    // free(p_queue->p_first);
    ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);

    p_queue->p_first = NULL;
    p_queue->p_last = NULL;
    p_queue->count--;
    found = 1;
  }
  else
  {
    temp = p_queue->p_first;
    while ( (temp) && (!found) )
    {
      if (llabs(frame_info - temp->frame_info) <= threshold)
      {
        *p_timestamp = temp->timestamp;
        if (!temp_prev)
        {
          p_queue->p_first = temp->p_next;
          temp->p_next->p_prev = NULL;
        }
        else
        {
          temp_prev->p_next = temp->p_next;
          if (temp->p_next)
          {
            temp->p_next->p_prev = temp_prev;
          }
          else
          {
            p_queue->p_last = temp_prev;
          }
        }
        // free(temp);
        ni_buffer_pool_return_buffer(temp, p_buffer_pool);
        p_queue->count--;
        found = 1;
        break;
      }
      temp_prev = temp;
      temp = temp->p_next;
      count++;
    }
  }
  if (print)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_pop_threshold %s %d iterations ..\n", p_queue->name, count);
  }

  if (p_queue->count < 0)
  {
    ni_log(NI_LOG_TRACE, "ni_queue_pop_threshold: error p_queue->count=%d\n", p_queue->count);
  }

  if (!found)
  {
    retval =  NI_RETCODE_FAILURE;
  }

  END;

  return retval;
}

/*!******************************************************************************
 *  \brief  Free xcoder queue
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_free(ni_queue_t *p_queue, ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_queue_node_t *temp = NULL;
  ni_queue_node_t *temp_next;
  int32_t left = 0;

  if (!p_queue)
  {
    return NI_RETCODE_SUCCESS;
  }

  ni_log(NI_LOG_TRACE, "Entries before clean up: \n");
  ni_queue_print(p_queue);

  temp = p_queue->p_first;
  while (temp)
  {
    temp_next = temp->p_next;
    //free(temp);
    ni_buffer_pool_return_buffer(temp, p_buffer_pool);
    temp = temp_next;
    left++;
  }
  ni_log(NI_LOG_TRACE, "Entries cleaned up at ni_queue_free: %d, count: %d\n",
              left, p_queue->count);

  //ni_queue_print(p_queue);

  p_queue->count = 0;

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Print xcoder queue info
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_print(ni_queue_t *p_queue)
{
  ni_queue_node_t *temp = NULL;
  struct tm* ltime = NULL;
  char buff[20] = { 0 };

  if (!p_queue)
  {
    return NI_RETCODE_SUCCESS;
  }

  ni_log(NI_LOG_TRACE, "Queue [%s] Count: %d\n", p_queue->name, p_queue->count);

  ni_log(NI_LOG_TRACE, "\nForward:\n");

  temp = p_queue->p_first;

  ni_log(NI_LOG_TRACE, "ni_queue_print enter: p_first=%p, p_last=%p, count=%d, temp=%p\n", p_queue->p_first, p_queue->p_last, p_queue->count, temp);

  //  ni_log(NI_LOG_TRACE, "ni_queue_print enter: p_first=%" PRId64 ", p_last=%" PRId64 ", count=%d, temp=%" PRId64 "\n", p_queue->p_first, p_queue->p_last, p_queue->count, temp);

  while (temp)
  {
    ltime = localtime(&temp->checkout_timestamp);
    if (ltime)
    {
      strftime(buff, 20, "%Y-%m-%d %H:%M:%S", ltime);
      ni_log(NI_LOG_TRACE, " %s [%" PRId64 ", %" PRId64 "]", buff, temp->timestamp, temp->frame_info);
    }
    temp = temp->p_next;
  }

  ni_log(NI_LOG_TRACE, "\nBackward:");

  temp = p_queue->p_last;
  while (temp)
  {
    ni_log(NI_LOG_TRACE, " [%" PRId64 ", %" PRId64 "]\n", temp->timestamp, temp->frame_info);
    temp = temp->p_prev;
  }
  ni_log(NI_LOG_TRACE, "\n");

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Convert string to boolean
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_atobool(const char *p_str, bool b_error)
{  
  if (!strcmp(p_str, "1") ||
      !strcmp(p_str, "true") ||
      !strcmp(p_str, "yes"))
  {
    return 1;
  }

  if (!strcmp(p_str, "0") ||
      !strcmp(p_str, "false") ||
      !strcmp(p_str, "no"))
  {
    return 0;
  }

  b_error = true;
  return 0;
}

/*!******************************************************************************
 *  \brief  Convert string to integer
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_atoi(const char *p_str, bool b_error)
{
  char *end;
  int32_t v = strtol(p_str, &end, 0);

  if (end == p_str || *end != '\0')
  {
    b_error = true;
  }

  return v;
}

/*!******************************************************************************
 *  \brief  Convert string to floating
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
double ni_atof(const char *p_str, bool b_error)
{
  char *end;
  double v = strtod(p_str, &end);

  if (end == p_str || *end != '\0')
  {
    b_error = true;
  }

  return v;
}

/*!******************************************************************************
 *  \brief  Parse name
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int32_t ni_parse_name(const char *arg, const char *const *names, bool b_error)
{
  int32_t i;
  for (i = 0; names[i]; i++)
  {
    if (!strcmp(arg, names[i]))
    {
      return i;
    }
  }

  return ni_atoi(arg, b_error);
}

/*!******************************************************************************
 *  \brief  Get system time for log
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
uint64_t ni_get_utime(void)
{
  struct timeval tv;
  (void)gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000LL + tv.tv_usec);
}

/*!*****************************************************************************
 *  \brief   threadpool control
 *
 *  \param[in]   params
 *
 *  \return  NULL
 *
 *
 ******************************************************************************/
void *thread_routine(void *arg)
{
  // pthread create start
#ifdef MSVC_BUILD
  ni_log(NI_LOG_TRACE, "thread is starting\n");
#else
  ni_log(NI_LOG_TRACE, "thread %lld is starting\n", ((unsigned long long)pthread_self()) % 100);
#endif
  threadpool_t *pool = (threadpool_t *)arg;
  // Loop the pool fetch the thread to run
  while (1)
  {
    //set mutex
    pthread_mutex_lock(&pool->pmutex);
    pool->idle++;
    //pool is empty and pool is not quit then wait
    // add timeout，timeout=1
    while (pool->first == NULL && !pool->quit)
    {
#ifdef MSVC_BUILD
      ni_log(NI_LOG_TRACE, "thread_routine is waiting\n");
#else
      ni_log(NI_LOG_TRACE, "thread %lld is waiting\n", ((unsigned long long)pthread_self()) % 100);
#endif
      pthread_cond_wait(&pool->pcond, &pool->pmutex);
    }

    // the pool is not empty
    // idle--，start run the task
    pool->idle--;
    if (pool->first != NULL)
    {
      task_t *t = pool->first;
      pool->first = t->next;
      pthread_mutex_unlock(&pool->pmutex);
      t->run(t->arg);
      free(t);
      pthread_mutex_lock(&pool->pmutex);
    }
    //pool is quit and the pool is empty
    // break
    if (pool->quit && pool->first == NULL)
    {
      pool->counter--;
      if (pool->counter == 0)
      {
        pthread_cond_signal(&pool->pcond);
      }
      pthread_mutex_unlock(&pool->pmutex);
      break;
    }
    pthread_mutex_unlock(&pool->pmutex);
  }
#ifdef MSVC_BUILD
  ni_log(NI_LOG_TRACE, "thread_routine is exiting\n");
#else
  ni_log(NI_LOG_TRACE, "thread %lld is exiting\n", ((unsigned long long)pthread_self()) % 100);
#endif
  return NULL;
}

/*!*****************************************************************************
 *  \brief   Init the threadpool
 *
 *  \param[in]   pool  threadpool address
 *
 *  \return      NULL
 *
 *
 ******************************************************************************/
void threadpool_init(threadpool_t *pool)
{
  pthread_mutex_init(&pool->pmutex, NULL);
  pthread_cond_init(&pool->pcond, NULL);
  pool->first = NULL;
  pool->last = NULL;
  pool->counter = 0;
  pool->idle = 0;
  pool->max_threads = MAX_THREADS;
  pool->quit = 0;
}

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
int threadpool_auto_add_task_thread(threadpool_t *pool, void *(*run)(void *arg), void *arg, int newThread)
{
  int ret = 0;
  // create new task
  task_t *newtask = (task_t *)malloc(sizeof(task_t));
  if (!newtask)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: threadpool_auto_add_task_thread Failed to allocate memory\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return -1;
  }
  newtask->run = run;
  newtask->arg = arg;
  newtask->next = NULL;

  // add mutex
  pthread_mutex_lock(&pool->pmutex);
  // add newtask to the pool
  if (pool->first == NULL)
  {
    pool->first = newtask;
  }
  else
  {
    pool->last->next = newtask;
  }
  pool->last = newtask;

  // If their are idle tasks, wake up the tasks
  ni_log(NI_LOG_TRACE, "threadpool_auto_add_task_thread pool->idle %d\n", pool->idle);
  if (pool->idle > 0)
  {
    pthread_cond_signal(&pool->pcond);
  }
  // no idle tasks，create a new thread，run the thread_routine
  else
  {
    if (pool->counter < pool->max_threads)
    {
      if (newThread == 1)
      {
	    pthread_t tid;
        pthread_create(&tid, NULL, thread_routine, pool);
        if (ret)
	    {
          ni_log(NI_LOG_ERROR, "ERROR %d: threadpool_auto_add_task_thread pthread_create failed : %d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, ret);
          return -1;
        }
        pool->counter++;
      }
    }
    else
    {
      while(pool->idle==0)
      {
        usleep(1000);
      }
      pthread_cond_signal(&pool->pcond);
    }
  }
  ni_log(NI_LOG_TRACE, "threadpool_auto_add_task_thread pool->counter %d\n", pool->counter);
  pthread_mutex_unlock(&pool->pmutex);
  return ret;
}


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
 *
 ******************************************************************************/
int threadpool_add_task(threadpool_t *pool, void *(*run)(void *arg), void *arg)
{
  int ret = 0;
  // create new task
  task_t *newtask = (task_t *)malloc(sizeof(task_t));
  if (!newtask)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: threadpool_add_task Failed to allocate memory\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    return -1;
  }
  newtask->run = run;
  newtask->arg = arg;
  newtask->next = NULL;

  // add mutex
  pthread_mutex_lock(&pool->pmutex);
  // add newtask to the pool
  if (pool->first == NULL)
  {
    pool->first = newtask;
  }
  else
  {
    pool->last->next = newtask;
  }
  pool->last = newtask;

  // If their are idle tasks, wake up the tasks
  ni_log(NI_LOG_TRACE, "threadpool_add_task pool->idle %d\n", pool->idle);
  if (pool->idle > 0)
  {
    pthread_cond_signal(&pool->pcond);
  }
  // no idle tasks，create a new thread，run the thread_routine
  else if (pool->counter < pool->max_threads)
  {
    pthread_t tid;
    ret = pthread_create(&tid, NULL, thread_routine, pool);
    if (ret)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: threadpool_add_task pthread_create failed : %d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, ret);
      return -1;
    }
    pool->counter++;
  }
  ni_log(NI_LOG_TRACE, "threadpool_add_task pool->counter %d\n", pool->counter);
  pthread_mutex_unlock(&pool->pmutex);
  return ret;
}


/*!*****************************************************************************
 *  \brief   destroy threadpool
 *
 *  \param[in]   pool  threadpool address
 *
 *  \return  NULL
 *
 *
 ******************************************************************************/
void threadpool_destroy(threadpool_t *pool)
{
  ni_log(NI_LOG_TRACE, "destroy start!\n");
  if (pool->quit)
  {
    return;
  }
  // add mutex
  pthread_mutex_lock(&pool->pmutex);
  pool->quit = 1;

  if (pool->counter > 0)
  {
    // if has idle task, wake up them
    if (pool->idle > 0)
    {
      ni_log(NI_LOG_TRACE, "destroy broadcast!\n");
      // wake up all the idle tasks
      pthread_cond_broadcast(&pool->pcond);
    }
    // if the process is running, wait it finish
    while (pool->counter)
    {
      pthread_cond_wait(&pool->pcond, &pool->pmutex);
    }
  }
  pthread_mutex_unlock(&pool->pmutex);
  pthread_mutex_destroy(&pool->pmutex);
  pthread_cond_destroy(&pool->pcond);
}

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
int insert_emulation_prevent_bytes(uint8_t *buf, int size)
{
  int insert_bytes = 0;
  uint8_t *buf_curr = buf;
  uint8_t *buf_end = buf + size - 1;
  int zeros = 0, insert_ep3_byte = 0;

  ni_log(NI_LOG_TRACE, "insert_emulation_prevent_bytes: enter\n");

  for (; buf_curr <= buf_end; buf_curr++)
  {
    if (zeros == 2)
    {
      insert_ep3_byte = (*buf_curr <= 3);
      if (insert_ep3_byte)
      {
        // move bytes from curr to end 1 position to make space for ep3
        memmove(buf_curr + 1, buf_curr, buf_end - buf_curr + 1);
        *buf_curr = 0x3;

        buf_curr++;
        buf_end++;
        insert_bytes++;
      }

      zeros = 0;
    }

    if (! *buf_curr)
    {
      zeros++;
    }
    else
    {
      zeros = 0;
    }
  }

  ni_log(NI_LOG_TRACE, "insert_emulation_prevent_bytes: %d, exit\n",
         insert_bytes);
  return insert_bytes;
}

#ifdef MEASURE_LATENCY
/*!******************************************************************************
 *  \brief  Create a latency measurement queue object of a given capacity
 *
 *  \param  capacity maximum size of queue
 *
 *  \return ni_lat_meas_q_t latency measurement queue structure
 *          
 *******************************************************************************/
ni_lat_meas_q_t * ni_lat_meas_q_create(unsigned capacity)
{
    ni_lat_meas_q_t* queue = (ni_lat_meas_q_t*) malloc(sizeof(ni_lat_meas_q_t));
    if(!queue)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for lat_meas-queue queue", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return NULL;
    }
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->array = (ni_lat_meas_q_entry_t*) malloc(queue->capacity * sizeof(ni_lat_meas_q_entry_t));
    if(!queue->array)
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: Failed to allocate memory for lat_meas_queue queue->array", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      return NULL;
    }
    return queue;
}

/*!******************************************************************************
 *  \brief  Push an item onto the queue
 *
 *  \param  queue pointer to latency queue
 *  \param  item ni_lat_meas_q_entry_t item to push onto the queue
 *
 *  \return void 1 if success, NULL if failed
 *          
 *******************************************************************************/
void * ni_lat_meas_q_enqueue(ni_lat_meas_q_t* queue, ni_lat_meas_q_entry_t item)
{
    if (queue->size == queue->capacity)
    {
      return NULL;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
    return (void *) 1;
}

/*!******************************************************************************
 *  \brief  Pop an item from the queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to popped item
 *          
 *******************************************************************************/
void * ni_lat_meas_q_dequeue(ni_lat_meas_q_t* queue)
{
    ni_lat_meas_q_entry_t *dequeue_item;
    if (queue->size == 0)
    {
      return NULL;
    }
    dequeue_item = &queue->array[queue->front];
    queue->front = (queue->front + 1)%queue->capacity;
    queue->size = queue->size - 1;
    return dequeue_item;
}

/*!******************************************************************************
 *  \brief  Get a pointer to rear of queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to rear of queue
 *          
 *******************************************************************************/
void * ni_lat_meas_q_rear(ni_lat_meas_q_t* queue)
{
    if (queue->size == 0)
    {
      return NULL;
    }
    return &queue->array[queue->rear];
}

/*!******************************************************************************
 *  \brief  Get a pointer to front of queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to front of queue
 *          
 *******************************************************************************/
void * ni_lat_meas_q_front(ni_lat_meas_q_t* queue)
{
    if (queue->size == 0)
    {
      return NULL;
    }
    return &queue->array[queue->front];
}

/*!******************************************************************************
 *  \brief  Add a new entry to latency queue
 *
 *  \param  dec_frame_time_q pointer to latency queue
 *  \param  abs_time frame start time for latency comparison
 *  \param  ts_time reference frame timestamp time
 *
 *  \return void 1 if success, NULL if failed
 *          
 *******************************************************************************/
void * ni_lat_meas_q_add_entry(ni_lat_meas_q_t *dec_frame_time_q, uint64_t abs_time, int64_t ts_time)
{
    // ni_log(NI_LOG_INFO, "ni_lat_meas_q_add_entry abs_time=%lu ts_time=%ld\n");
    ni_lat_meas_q_entry_t entry = {.abs_timenano = abs_time, .ts_time = ts_time};
    return ni_lat_meas_q_enqueue(dec_frame_time_q, entry);
}

/*!******************************************************************************
 *  \brief  Check latency of a frame referenced by its timestamp
 *
 *  \param  dec_frame_time_q pointer to latency queue
 *  \param  abs_time frame end time for latency comparison
 *  \param  ts_time reference frame timestamp time
 *
 *  \return uint64_t value of latency if suceeded, -1 if failed
 *          
 *******************************************************************************/
uint64_t ni_lat_meas_q_check_latency(ni_lat_meas_q_t *dec_frame_time_q, uint64_t abs_time, int64_t ts_time)
{
    // ni_log(NI_LOG_INFO, "ni_lat_meas_q_check_latency abs_time=%lu ts_time=%ld\n");
    uint32_t dequeue_count = 0;
    ni_lat_meas_q_entry_t * entry = ni_lat_meas_q_front(dec_frame_time_q);

    if (entry == NULL)
    {
      return -1;
    }

    if (entry->ts_time == ts_time) {
      ni_lat_meas_q_dequeue(dec_frame_time_q);
      dequeue_count++;
    }
    else
    {
      while (entry->ts_time < ts_time) // queue miss, perhaps frame was not decoded properly or TS was offset
      {
        entry = ni_lat_meas_q_dequeue(dec_frame_time_q);
        dequeue_count++;
        if (entry == NULL)
        {
           return -1;
        }
      }
    }
    // ni_log(NI_LOG_INFO, "DQ_CNT:%d,QD:%d,", dequeue_count, dec_frame_time_q->size);

    if (entry == NULL)
    { // queue overrun
      return -1;
    }
    else if (entry->ts_time > ts_time)
    { // queue miss, perhaps frame was not enqueued properly or TS was offset
      return -1;
    }
    else if (entry->ts_time == ts_time)
    { // queue item is perfectly matched, calculate latency
      return (abs_time - entry->abs_timenano);
    }
}
#endif
