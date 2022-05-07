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
*  \file   ni_log.c
*
*  \brief  Exported logging routines definition
*
*******************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include "ni_log.h"

static ni_log_level_t ni_log_level = NI_LOG_INFO;
static void (*ni_log_callback)(int, const char*, va_list) =
    ni_log_default_callback;

/*!*****************************************************************************
 *  \brief Get time for logs with microsecond timestamps
 *
 *  \param[in/out] p_tp   timeval struct
 *  \param[in] p_tzp      timezone struct
 *
 *  \return return 0 for success, -1 for error
 ******************************************************************************/
int32_t ni_log_gettimeofday(struct timeval *p_tp, struct timezone *p_tzp)
{
#ifdef _WIN32
    FILETIME file_time;
    SYSTEMTIME system_time;
    ULARGE_INTEGER ularge;
    /*! FILETIME of Jan 1 1970 00:00:00. */
    static const unsigned __int64 epoch =
        ((unsigned __int64)116444736000000000ULL);

    // timezone information is stored outside the kernel so tzp isn't used
    (void)p_tzp; /*!Remove compiler warnings*/

    // Note: this function is not a precision timer. See elapsed_time().
    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    ularge.LowPart = file_time.dwLowDateTime;
    ularge.HighPart = file_time.dwHighDateTime;
    // Surpress cppcheck
    (void)ularge.LowPart;
    (void)ularge.HighPart;
    p_tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
    p_tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

    return 0;
#else
    return gettimeofday(p_tp, p_tzp);
#endif
}

/*!*****************************************************************************
 *  \brief Default ni_log() callback
 *
 *  \param[in] level  log level
 *  \param[in] fmt    printf format specifier
 *  \param[in] vl     variadric args list
 *
 *  \return
 ******************************************************************************/
void ni_log_default_callback(int level, const char* fmt, va_list vl)
{
    if (level <= ni_log_level)
    {
#ifndef _ANDROID
#ifdef NI_LOG_TRACE_TIMESTAMPS
        if (level == NI_LOG_TRACE)
        {
            struct timeval tv;
            ni_log_gettimeofday(&tv, NULL);
#ifdef _WIN32
            fprintf(stderr, "[%lld] ", (long long) (tv.tv_sec * 1000000LL + tv.tv_usec));
#else
            fprintf(stderr, "[%" PRIu64 "] ", (uint64_t) (tv.tv_sec * 1000000LL + tv.tv_usec));
#endif
        }
#endif
#endif

#ifdef _ANDROID
        ALOGV(fmt, vl);
#else
        vfprintf(stderr, fmt, vl);
#endif
    }
}

/*!*****************************************************************************
 *  \brief  Set ni_log() callback
 *
 *  \param[in] callback
 *
 *  \return
 ******************************************************************************/
void ni_log_set_callback(void (*log_callback)(int, const char*, va_list))
{
    ni_log_callback = log_callback;
}

/*!*****************************************************************************
 *  \brief  print log message using ni_log_callback
 *
 *  \param[in] level  log level
 *  \param[in] format printf format specifier
 *  \param[in] ...    additional arguments
 *
 *  \return
 ******************************************************************************/
void ni_log(ni_log_level_t level, const char *fmt, ...)
{
    va_list vl;

    va_start(vl, fmt);
    if (ni_log_callback)
    {
        ni_log_callback(level, fmt, vl);
    }
    va_end(vl);
}

/*!*****************************************************************************
 *  \brief  Set ni_log_level
 *
 *  \param  level log level
 *
 *  \return
 ******************************************************************************/
void ni_log_set_level(ni_log_level_t level)
{
    ni_log_level = level;
}

/*!*****************************************************************************
 *  \brief Get ni_log_level
 *
 *  \return ni_log_level
 ******************************************************************************/
ni_log_level_t ni_log_get_level(void)
{
    return ni_log_level;
}

/*!*****************************************************************************
 *  \brief Convert ffmpeg log level integer to appropriate ni_log_level_t
 *
 *  \param fflog_level integer representation of FFmpeg log level
 *
 *  \return ni_log_level
 ******************************************************************************/
ni_log_level_t ff_to_ni_log_level(int fflog_level)
{
    ni_log_level_t converted_ni_log_level = NI_LOG_ERROR;
    if (fflog_level <= -8)
    {
        converted_ni_log_level = NI_LOG_NONE;
    }
    else if (fflog_level <= 8)
    {
        converted_ni_log_level = NI_LOG_FATAL;
    }
    else if (fflog_level <= 16)
    {
        converted_ni_log_level = NI_LOG_ERROR;
    }
    else if (fflog_level <= 32)
    {
        converted_ni_log_level = NI_LOG_INFO;
    }
    else if (fflog_level <= 48)
    {
        converted_ni_log_level = NI_LOG_DEBUG;
    }
    else
    {
        converted_ni_log_level = NI_LOG_TRACE;
    }
    return converted_ni_log_level;
}


/*!*****************************************************************************
 *  \brief Convert terminal arg string to ni_log_level_t
 *
 *  \param log_str character pointer of log level arg in terminal
 *
 *  \return ni_log_level
 ******************************************************************************/
ni_log_level_t arg_to_ni_log_level(const char *arg_str)
{
#ifdef _WIN32
    int (*strcicmp) (const char*, const char *) = &_stricmp;
#else
    int (*strcicmp) (const char*, const char *) = &strcasecmp;
#endif

    if (!(*strcicmp)(arg_str, "none"))
    {
        return NI_LOG_NONE;
    } else if (!(*strcicmp)(arg_str, "fatal"))
    {
        return NI_LOG_FATAL;
    } else if (!(*strcicmp)(arg_str, "error"))
    {
        return NI_LOG_ERROR;
    } else if (!(*strcicmp)(arg_str, "info"))
    {
        return NI_LOG_INFO;
    } else if (!(*strcicmp)(arg_str, "debug"))
    {
        return NI_LOG_DEBUG;
    } else if (!(*strcicmp)(arg_str, "trace"))
    {
        return NI_LOG_TRACE;
    } else {
        return NI_LOG_INVALID;
    }
}