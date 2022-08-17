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
*  \file   ni_lat_meas.h
*
*  \brief  utility functions for measuring frame latency
*
*******************************************************************************/

#pragma once

#include <stdint.h>

typedef struct _ni_lat_meas_q_entry_t
{
    uint64_t abs_timenano;
    int64_t ts_time;
} ni_lat_meas_q_entry_t;

typedef struct _ni_lat_meas_q_t
{
    int front, rear, size, capacity;
    ni_lat_meas_q_entry_t *array;
} ni_lat_meas_q_t;

// NI latency measurement queue operations
ni_lat_meas_q_t *ni_lat_meas_q_create(int capacity);

void ni_lat_meas_q_destroy(ni_lat_meas_q_t *frame_time_q);

void *ni_lat_meas_q_add_entry(ni_lat_meas_q_t *frame_time_q, uint64_t abs_time,
                              int64_t ts_time);

uint64_t ni_lat_meas_q_check_latency(ni_lat_meas_q_t *frame_time_q,
                                     uint64_t abs_time, int64_t ts_time);