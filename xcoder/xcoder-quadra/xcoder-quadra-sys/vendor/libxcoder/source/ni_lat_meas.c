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
*  \file   ni_lat_meas.c
*
*  \brief  utility functions for measuring frame latency
*
*******************************************************************************/

#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ni_lat_meas.h"
#include "ni_log.h"
#include "ni_util.h"

/*!*****************************************************************************
 *  \brief  Create a latency measurement queue object of a given capacity
 *
 *  \param  capacity maximum size of queue
 *
 *  \return ni_lat_meas_q_t latency measurement queue structure
 *
 ******************************************************************************/
ni_lat_meas_q_t *ni_lat_meas_q_create(int capacity)
{
    if (1 > capacity)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: ni_lat_meas_q_create() called with capacity less than 1"
               "\n");
        return NULL;
    }

    ni_lat_meas_q_t *queue = (ni_lat_meas_q_t *)malloc(sizeof(ni_lat_meas_q_t));
    if (!queue)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %d: Failed to allocate memory for "
               "lat_meas-queue queue\n",
               NI_ERRNO);
        return NULL;
    }
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->array = (ni_lat_meas_q_entry_t *)malloc(
        queue->capacity * sizeof(ni_lat_meas_q_entry_t));
    if (!queue->array)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR %d: Failed to allocate memory for "
               "lat_meas_queue queue->array\n",
               NI_ERRNO);
        free(queue);
        return NULL;
    }
    return queue;
}

/*!*****************************************************************************
 *  \brief  Destroy a latency measurement queue object
 *
 *  \param  frame_time_q pointer to ni_lat_meas_q_t object
 *
 *  \return
 *
 ******************************************************************************/
void ni_lat_meas_q_destroy(ni_lat_meas_q_t *frame_time_q)
{
    free(frame_time_q->array);
    free(frame_time_q);
}

/*!*****************************************************************************
 *  \brief  Push an item onto the queue
 *
 *  \param  queue pointer to latency queue
 *  \param  item ni_lat_meas_q_entry_t item to push onto the queue
 *
 *  \return void 1 if success, NULL if failed
 *
 ******************************************************************************/
void *ni_lat_meas_q_enqueue(ni_lat_meas_q_t *queue, ni_lat_meas_q_entry_t item)
{
    if (queue->size == queue->capacity)
    {
        return NULL;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
    return (void *)1;
}

/*!*****************************************************************************
 *  \brief  Pop an item from the queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to popped item
 *
 ******************************************************************************/
void *ni_lat_meas_q_dequeue(ni_lat_meas_q_t *queue)
{
    ni_lat_meas_q_entry_t *dequeue_item;
    if (queue->size == 0)
    {
        return NULL;
    }
    dequeue_item = &queue->array[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size = queue->size - 1;
    return dequeue_item;
}

/*!*****************************************************************************
 *  \brief  Get a pointer to rear of queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to rear of queue
 *
 ******************************************************************************/
void *ni_lat_meas_q_rear(ni_lat_meas_q_t *queue)
{
    return queue->size == 0 ? NULL : &queue->array[queue->rear];
}

/*!*****************************************************************************
 *  \brief  Get a pointer to front of queue
 *
 *  \param  queue pointer to latency queue
 *
 *  \return void pointer to front of queue
 *
 ******************************************************************************/
void *ni_lat_meas_q_front(ni_lat_meas_q_t *queue)
{
    return queue->size == 0 ? NULL : &queue->array[queue->front];
}

/*!*****************************************************************************
 *  \brief  Add a new entry to latency queue
 *
 *  \param  frame_time_q pointer to latency queue
 *  \param  abs_time frame start time for latency comparison
 *  \param  ts_time reference frame timestamp time
 *
 *  \return void 1 if success, NULL if failed
 *
 ******************************************************************************/
void *ni_lat_meas_q_add_entry(ni_lat_meas_q_t *frame_time_q, uint64_t abs_time,
                              int64_t ts_time)
{
    // ni_log(NI_LOG_DEBUG, "ni_lat_meas_q_add_entry abs_time=%lu ts_time="
    //        "%ld\n", abs_time, ts_time);
    ni_lat_meas_q_entry_t entry = {.abs_timenano = abs_time,
                                   .ts_time = ts_time};
    return ni_lat_meas_q_enqueue(frame_time_q, entry);
}

/*!*****************************************************************************
 *  \brief  Check latency of a frame referenced by its timestamp
 *
 *  \param  frame_time_q pointer to latency queue
 *  \param  abs_time frame end time for latency comparison
 *  \param  ts_time reference frame timestamp time
 *
 *  \return uint64_t value of latency if suceeded, -1 if failed
 *
 ******************************************************************************/
uint64_t ni_lat_meas_q_check_latency(ni_lat_meas_q_t *frame_time_q,
                                     uint64_t abs_time, int64_t ts_time)
{
    // ni_log(NI_LOG_DEBUG, "ni_lat_meas_q_check_latency abs_time=%lu ts_time="
    //        "%ld\n", abs_time, ts_time);
    uint32_t dequeue_count = 0;
    ni_lat_meas_q_entry_t *entry = ni_lat_meas_q_front(frame_time_q);

    if (entry == NULL)
    {
        return -1;
    }

    if (entry->ts_time == ts_time)
    {
        ni_lat_meas_q_dequeue(frame_time_q);
        dequeue_count++;
    } else
    {   // queue miss, perhaps frame was not decoded properly or TS was offset
        while (entry->ts_time < ts_time)
        {
            entry = ni_lat_meas_q_dequeue(frame_time_q);
            dequeue_count++;
            if (entry == NULL)
            {
                return -1;
            }
        }
    }
    ni_log(NI_LOG_DEBUG, "DQ_CNT:%u,QD:%d", dequeue_count, frame_time_q->size);

    if ((entry == NULL) || (entry->ts_time > ts_time))
    {   // queue overrun OR
        // queue miss, perhaps frame was not enqueued properly or TS was offset
        return -1;
    } else if (entry->ts_time == ts_time)
    {   // queue item is perfectly matched, calculate latency
        return (abs_time - entry->abs_timenano);
    }

    return -1;
}
