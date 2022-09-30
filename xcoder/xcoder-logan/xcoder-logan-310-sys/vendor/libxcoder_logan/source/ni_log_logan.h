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
*  \file   ni_log_logan.h
*
*  \brief  Exported logging routines definition
*
*******************************************************************************/

#pragma once

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#ifdef LIBXCODER_OBJS_BUILD
#include "../build/xcoder_auto_headers.h"
#endif

#ifdef _WIN32
  #ifdef XCODER_DLL
    #ifdef LIB_EXPORTS
      #define LIB_API_LOG __declspec(dllexport)
    #else
      #define LIB_API_LOG __declspec(dllimport)
    #endif
  #else
    #define LIB_API_LOG
  #endif
#elif __linux__ || __APPLE__
  #define LIB_API_LOG
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef QUADRA

typedef enum
{
  NI_LOG_NONE  = 0,
  NI_LOG_FATAL = 1,
  NI_LOG_ERROR = 2,
  NI_LOG_INFO  = 3,
  NI_LOG_DEBUG = 4,
  NI_LOG_TRACE = 5
} ni_log_level_t;

/*!*************************************/
// libxcoder logging utility
/*!******************************************************************************
 *  \brief  Default ni_log() callback
 *
 *  \param[in] level  log level
 *  \param[in] fmt    printf format specifier
 *  \param[in] vl     variadric args list
 *
 *  \return
 *******************************************************************************/
LIB_API_LOG void ni_log_default_callback(int level, const char* fmt, va_list vl);

/*!******************************************************************************
 *  \brief  Set ni_log() callback
 *
 *  \param[in] callback
 *
 *  \return
 *******************************************************************************/
LIB_API_LOG void ni_log_set_callback(void (*log_callback)(int, const char*, va_list));

/*!******************************************************************************
 *  \brief  print log message using ni_log_callback
 *
 *  \param[in] level  log level
 *  \param[in] format printf format specifier
 *  \param[in] ...    additional arguments
 *
 *  \return
 *******************************************************************************/
LIB_API_LOG void ni_log(ni_log_level_t level, const char *fmt, ...);

/*!******************************************************************************
 *  \brief  Set ni_log_level
 *
 *  \param  level log level
 *
 *  \return
 *******************************************************************************/
LIB_API_LOG void ni_log_set_level(ni_log_level_t level);

/*!******************************************************************************
 *  \brief Get ni_log_level
 *
 *  \return ni_log_level
 *******************************************************************************/
LIB_API_LOG ni_log_level_t ni_log_get_level(void);

/*!******************************************************************************
 *  \brief Convert ffmpeg log level integer to appropriate ni_log_level_t
 *
 *  \param fflog_level integer representation of FFmpeg log level
 *
 *  \return ni_log_level
 *******************************************************************************/
LIB_API_LOG ni_log_level_t ff_to_ni_log_level(int fflog_level);

#endif // #ifndef QUADRA

#ifdef __cplusplus
}
#endif
