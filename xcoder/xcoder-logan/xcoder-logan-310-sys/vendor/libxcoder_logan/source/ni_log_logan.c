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
*  \file   ni_log_logan.c
*
*  \brief  Exported logging routines definition
*
*******************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#if __linux__  || __APPLE__
#include <sys/time.h>
#include <inttypes.h>
#endif
#include "ni_log_logan.h"

#ifndef QUADRA

static ni_log_level_t ni_log_level = NI_LOG_INFO;
static void (*ni_log_callback)(int, const char*, va_list) = ni_log_default_callback;

/*!******************************************************************************
 *  \brief Default ni_log() callback
 *
 *  \param[in] level  log level
 *  \param[in] fmt    printf format specifier
 *  \param[in] vl     variadric args list
 *
 *  \return
 *******************************************************************************/
void ni_log_default_callback(int level, const char* fmt, va_list vl)
{
  if (level <= ni_log_level)
  {
# ifdef NI_LOG_TRACE_TIMESTAMPS
    if (level == NI_LOG_TRACE)
    {
      struct timeval tv;
      ni_logan_gettimeofday(&tv, NULL);
      fprintf(stderr, "[%" PRIu64 "] ", tv.tv_sec * 1000000LL + tv.tv_usec);
    }
# endif
    vfprintf(stderr, fmt, vl);
  }
}

/*!******************************************************************************
 *  \brief  Set ni_log() callback
 *
 *  \param[in] callback
 *
 *  \return
 *******************************************************************************/
void ni_log_set_callback(void (*log_callback)(int, const char*, va_list))
{
  ni_log_callback = log_callback;
}

/*!******************************************************************************
 *  \brief  print log message using ni_log_callback
 *
 *  \param[in] level  log level
 *  \param[in] format printf format specifier
 *  \param[in] ...    additional arguments
 *
 *  \return
 *******************************************************************************/
void ni_log(ni_log_level_t level, const char *fmt, ...)
{
  va_list vl;
  void (*log_callback)(int, const char*, va_list);

  va_start(vl, fmt);
  if (log_callback = ni_log_callback)
    log_callback(level, fmt, vl);
  va_end(vl);
}

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
  ni_log_level_t converted_ni_log_level = NI_LOG_ERROR;
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
#endif // #ifndef QUADRA
