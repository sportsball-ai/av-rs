/******************************************************************************
 *
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************
 * Originally developed and contributed by Ittiam Systems Pvt. Ltd, Bangalore
*/

/*****************************************************************************/
/*                                                                           */
/*  File Name         : ifwh264d_api.c                                         */
/*                                                                           */
/*  Description       : Has all  API related functions                       */
/*                                                                           */
/*                                                                           */
/*  List of Functions : api_check_struct_sanity                              */
/*          ih264d_set_processor                                             */
/*          ifwh264d_create                                                    */
/*          ifwh264d_delete                                                    */
/*          ih264d_init                                                      */
/*          ih264d_map_error                                                 */
/*          ifwh264d_video_decode                                              */
/*          ifwh264d_get_version                                               */
/*          ih264d_get_display_frame                                         */
/*          ih264d_set_display_frame                                         */
/*          ifwh264d_fwcheck                                                 */
/*          ifwh264d_set_flush_mode                                            */
/*          ih264d_get_status                                                */
/*          ih264d_get_buf_info                                              */
/*          ifwh264d_set_params                                                */
/*          ifwh264d_set_default_params                                        */
/*          ifwh264d_reset                                                     */
/*          ifwh264d_ctl                                                       */
/*          ih264d_rel_display_frame                                         */
/*          ih264d_set_degrade                                               */
/*          ih264d_get_frame_dimensions                                      */
/*          ifwh264d_set_num_cores                                             */
/*          ifwh264d_fill_output_struct_from_context                           */
/*          ifwh264d_api_function                                              */
/*                                                                           */
/*  Issues / Problems : None                                                 */
/*                                                                           */
/*  Revision History  :                                                      */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         14 10 2008   100356(SKV)     Draft                                */
/*                                                                           */
/*****************************************************************************/

#define LOG_NDEBUG 0
//#undef NDEBUG

#include <utils/Log.h>

#include "ifwh264_typedefs.h"
#include "ifwh264_macros.h"
#include "ifwv.h"
#include "ifwvd.h"
#include "ifwh264d.h"
#include "ifwh264d_defs.h"
#include "ifwh264d_debug.h"

#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "ifwh264d_structs.h"

#include "ifwh264d_error_handler.h"

#include "ifwh264d_defs.h"

#include "ifwh264d_utils.h"

#include "ifwthread.h"
#include <assert.h>

#include <stdint.h>
#include <fcntl.h>
#include <errno.h>

#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_device_api.h"
#include "ni_util.h"
#include "ni_device_test.h"

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>   // for close

/*********************/
/* Codec Versioning  */
/*********************/

#define LOG_TAG "ifwh264d_api!!"

//Move this to where it is used
#define CODEC_NAME "H264VDEC"
#define CODEC_RELEASE_TYPE "production"
#define CODEC_RELEASE_VER "05.00"
#define CODEC_VENDOR "ITTIAM"
#define MAXVERSION_STRLEN 511
#ifdef __ANDROID__
#define VERSION(version_string, codec_name, codec_release_type,                \
                codec_release_ver, codec_vendor)                               \
    snprintf(version_string, MAXVERSION_STRLEN,                                \
             "@(#)Id:%s_%s Ver:%s Released by %s", codec_name,                 \
             codec_release_type, codec_release_ver, codec_vendor)
#else
#define VERSION(version_string, codec_name, codec_release_type,                \
                codec_release_ver, codec_vendor)                               \
    snprintf(version_string, MAXVERSION_STRLEN,                                \
             "@(#)Id:%s_%s Ver:%s Released by %s Build: %s @ %s", codec_name,  \
             codec_release_type, codec_release_ver, codec_vendor, __DATE__,    \
             __TIME__)
#endif

#define MIN_IN_BUFS 1
#define MIN_OUT_BUFS_420 3
#define MIN_OUT_BUFS_422ILE 1
#define MIN_OUT_BUFS_RGB565 1
#define MIN_OUT_BUFS_420SP 2

#define NUM_FRAMES_LIMIT_ENABLED 0

#if NUM_FRAMES_LIMIT_ENABLED
#define NUM_FRAMES_LIMIT 10000
#else
#define NUM_FRAMES_LIMIT 0x7FFFFFFF
#endif

#include <cutils/properties.h>
#define PROP_DECODER_LOGLEVEL "use_decode_loglevel"
#define PROP_DECODER_HW_ID "use_decode_hw_id"

volatile int send_fin_flag = 0, recieve_fin_flag = 0, err_flag = 0;
volatile uint32_t number_of_frames = 0;
volatile uint32_t number_of_packets = 0;
struct timeval start_time, previous_time, current_time;
time_t start_timestamp = 0, privious_timestamp = 0, current_timestamp = 0;

// 1.8 GB file cache
volatile unsigned long total_file_size = 0;
volatile uint32_t data_left_size = 0;

char *mInFile = "/data/misc/media/avcd_input.h264";

char *mdecoderFile = "/data/misc/media/avcd_decoder.yuv";

FILE *pIn_file = NULL;
FILE *p_file = NULL;

int send_dump = 0;
int receive_dump = 0;

/*!******************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t decoder_send_data_buf(
    ni_session_context_t *p_dec_ctx, ni_session_data_io_t *p_in_data,
    int sos_flag, int input_video_width, int input_video_height, FILE *p_file,
    uint8_t *pkt_buf, int pkt_size, int file_size,
    unsigned long *total_bytes_sent, int print_time, int write_to_file,
    device_state_t *p_device_state)
{
    //static uint8_t tmp_buf[NI_MAX_PACKET_SZ - 32] = { 0 };
    //uint8_t *tmp_buf_ptr = pkt_buf;
    int packet_size = pkt_size;
    //int chunk_size = 0;
    int frame_pkt_size = 0;
    //int nal_size = 0;
    //int nal_type = -1;
    int tx_size = 0;
    int send_size = 0;
    int new_packet = 0;
    ni_packet_t *p_in_pkt = &(p_in_data->data.packet);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    ni_log(NI_LOG_DEBUG, "===> decoder_send_data <===\n");

    /*if(pkt_size > 0 && p_file && write_to_file)
  {
      if (fwrite(pkt_buf, pkt_size, 1, p_file) != 1)
      {
        ni_log(NI_LOG_ERROR, "decoder_send_data Writing data %d bytes error !\n", pkt_size);
        ni_log(NI_LOG_ERROR, "decoder_send_data ferror rc = %d\n", ferror(p_file));
      }
      if (fflush(p_file))
      {
        ni_log(NI_LOG_ERROR, "decoder_receive_data_buf Writing data frame flush failed! errno %d\n", errno);
      }
  }*/

    if (p_device_state->dec_eos_sent)
    {
        ni_log(NI_LOG_DEBUG,
               "decoder_send_data: ALL data (incl. eos) sent "
               "already !\n");
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG,
           "decoder_send_data 000 size pkt_size %d, p_in_pkt->data_len %d, "
           "p_dec_ctx->prev_size %d\n",
           pkt_size, p_in_pkt->data_len, p_dec_ctx->prev_size);

    if (0 == p_in_pkt->data_len)
    {
        memset(p_in_pkt, 0, sizeof(ni_packet_t));
        /*if (NI_CODEC_FORMAT_H264 == p_dec_ctx->codec_format)
    {
      // send whole encoded packet which ends with a slice NAL
      while ((nal_size = find_h264_next_nalu(tmp_buf_ptr, &nal_type)) > 0)
      {
        frame_pkt_size += nal_size;
        tmp_buf_ptr += nal_size;
#if 0
        printf("nal %d  nal_size %d frame_pkt_size %d\n", nal_type, nal_size,
               frame_pkt_size);
#endif
        if (H264_NAL_SLICE == nal_type || H264_NAL_IDR_SLICE == nal_type)
        {
          break;
        }
      }
    }
    else
    {
      frame_pkt_size = chunk_size = packet_size;
      //chunk_size = read_next_chunk(tmp_buf, packet_size);
    }*/

        //frame_pkt_size = chunk_size = packet_size;
        frame_pkt_size = packet_size;

        /*if(pkt_size > NI_MAX_PACKET_SZ){
       ni_log(NI_LOG_ERROR, "decoder_send_data error !!! size pkt_size %d, NI_MAX_PACKET_SZ %d\n", pkt_size, NI_MAX_PACKET_SZ);
    }*/
        //memcpy(tmp_buf, pkt_buf, packet_size);

        p_in_pkt->p_data = NULL;
        p_in_pkt->data_len = frame_pkt_size;

        if (frame_pkt_size + p_dec_ctx->prev_size > 0)
        {
            ni_packet_buffer_alloc(p_in_pkt,
                                   frame_pkt_size + p_dec_ctx->prev_size);
        }

        new_packet = 1;
        send_size = frame_pkt_size + p_dec_ctx->prev_size;
    } else
    {
        send_size = p_in_pkt->data_len;
    }

    ni_log(NI_LOG_DEBUG,
           "decoder_send_data 001 send_size %d, new_packet %d, "
           "p_dec_ctx->prev_size %d, frame_pkt_size %d\n",
           send_size, new_packet, p_dec_ctx->prev_size, frame_pkt_size);

    p_in_pkt->start_of_stream = sos_flag;
    p_in_pkt->end_of_stream = 0;
    p_in_pkt->video_width = input_video_width;
    p_in_pkt->video_height = input_video_height;

    if (send_size == 0)
    {
        if (new_packet)
        {
            //send_size = ni_packet_copy(p_in_pkt->p_data, tmp_buf, 0,
            //                            p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);

            send_size =
                ni_packet_copy(p_in_pkt->p_data, pkt_buf, 0,
                               p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
            // todo save offset
        }

        p_in_pkt->data_len = send_size;

        p_in_pkt->end_of_stream = 1;
        ni_log(NI_LOG_DEBUG, "Sending p_last packet (size %d) + eos\n",
               p_in_pkt->data_len);
    } else
    {
        if (new_packet)
        {
            //send_size = ni_packet_copy(p_in_pkt->p_data, tmp_buf, frame_pkt_size,
            //                                p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);

            send_size =
                ni_packet_copy(p_in_pkt->p_data, pkt_buf, frame_pkt_size,
                               p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
            // todo: update offset with send_size
            // p_in_pkt->data_len is the actual packet size to be sent to decoder
        }
    }

    ni_log(NI_LOG_DEBUG,
           "decoder_send_data 002 send_size %d, p_in_pkt->data_len %d, "
           "p_dec_ctx->prev_size %d, new_packet %d\n",
           send_size, p_in_pkt->data_len, p_dec_ctx->prev_size, new_packet);

    if (packet_size > 0 && p_file && write_to_file)
    {
        if (fwrite(p_in_pkt->p_data, p_in_pkt->data_len, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR,
                   "decoder_send_data Writing data %d bytes error !\n",
                   packet_size);
            ni_log(NI_LOG_ERROR, "decoder_send_data ferror rc = %d\n",
                   ferror(p_file));
        }
        if (fflush(p_file))
        {
            ni_log(NI_LOG_ERROR,
                   "decoder_receive_data_buf Writing data frame flush failed! "
                   "errno %d\n",
                   errno);
        }
    }

    tx_size =
        ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

    ni_log(NI_LOG_DEBUG,
           "decoder_send_data 003 tx_size %d, p_dec_ctx->prev_size %d\n",
           tx_size, p_dec_ctx->prev_size);

    if (tx_size < 0)
    {
        // Error
        fprintf(stderr, "Sending data error. rc:%d\n", tx_size);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    } else if (tx_size == 0)
    {
        ni_log(NI_LOG_DEBUG,
               "0 bytes sent this time, sleep and re-try p_next time .\n");
        usleep(10000);
    } else if (tx_size < send_size)
    {
        if (print_time)
        {
            ni_log(
                NI_LOG_DEBUG,
                "decoder_send_data 004 Sent %d < %d , re-try p_next time ?\n",
                tx_size, send_size);
        }
    }

    *total_bytes_sent += tx_size;

    if (p_dec_ctx->ready_to_close)
    {
        p_device_state->dec_eos_sent = 1;
    }

    if (print_time)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: success, total sent: %ld\n",
               *total_bytes_sent);
    }

    if (tx_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: reset packet_buffer.\n");
        ni_packet_buffer_free(p_in_pkt);
    }

#if 0
bytes_sent += chunk_size;
ni_log(NI_LOG_DEBUG, "[W] %d percent %d bytes sent. rc:%d result:%d\n", bytes_sent*100/file_size, chunk_size, rc, result);
sos_flag = 0;
if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == rc)
{
  ni_log(NI_LOG_DEBUG, "Buffer Full.\n");
}
else if (rc != 0)
{
  // Error
  fprintf(stderr, "Sending data error. rc:%d result:%d.\n", rc, result);
  err_flag = 1;
  return 2;
}
#endif

    //retval = NI_RETCODE_SUCCESS;

    END;

    return retval;
}

/*!******************************************************************************
 *  \brief  Receive output data from decoder
 *
 *  \param
 *
 *  \return 0: got YUV frame;  1: end-of-stream;  2: got nothing
 *******************************************************************************/
int decoder_receive_data_buf(ni_session_context_t *p_dec_ctx,
                             ni_session_data_io_t *p_out_data,
                             int output_video_width, int output_video_height,
                             FILE *p_file, int *bytes_recv,
                             unsigned long long *total_bytes_recieved,
                             int print_time, int write_to_file,
                             device_state_t *p_device_state)
{
    int rc = NI_RETCODE_FAILURE;
    int end_flag = 0;
    int rx_size = 0;
    ni_frame_t *p_out_frame = &(p_out_data->data.frame);
    int width, height;

    ni_log(NI_LOG_DEBUG, "===> decoder_receive_data <===\n");

    if (p_device_state->dec_eos_received)
    {
        ni_log(NI_LOG_DEBUG,
               "decoder_receive_data: eos received already, Done !\n");
        LRETURN;
    }

    // prepare memory buffer for receiving decoded frame
    width = p_dec_ctx->active_video_width > 0 ? p_dec_ctx->active_video_width :
                                                output_video_width;
    height = p_dec_ctx->active_video_height > 0 ?
        p_dec_ctx->active_video_height :
        output_video_height;

    // allocate memory only after resolution is known (for buffer pool set up)
    int alloc_mem = (p_dec_ctx->active_video_width > 0 &&
                             p_dec_ctx->active_video_height > 0 ?
                         1 :
                         0);
    rc = ni_decoder_frame_buffer_alloc(
        p_dec_ctx->dec_fme_buf_pool, &(p_out_data->data.frame), alloc_mem,
        width, height, p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264,
        p_dec_ctx->bit_depth_factor, 1);

    /*rc = ni_frame_buffer_alloc(p_out_frame, output_video_width,
                          output_video_height,
                          p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
                          1); // default to AV_PIX_FMT_YUV420P 8bit*/
    if (NI_RETCODE_SUCCESS != rc)
    {
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG, "decoder_receive_data_buf 000\n");

    rx_size =
        ni_device_session_read(p_dec_ctx, p_out_data, NI_DEVICE_TYPE_DECODER);

    ni_log(NI_LOG_DEBUG, "decoder_receive_data_buf 001\n");

    ni_log(NI_LOG_DEBUG,
           "decoder_receive_data_buf 002 p_dec_ctx->frame_num %" PRIu64
           ", rx_size %d\n",
           p_dec_ctx->frame_num, rx_size);

    end_flag = p_out_frame->end_of_stream;

    *bytes_recv = rx_size;

    if (rx_size < 0)
    {
        // Error
        fprintf(stderr, "Receiving data error. rc:%d\n", rx_size);
        ni_decoder_frame_buffer_free(&(p_out_data->data.frame));
        rc = NI_RETCODE_FAILURE;
        LRETURN;
    } else if (rx_size > 0)
    {
        number_of_frames++;
        ni_log(NI_LOG_DEBUG,
               "Got frame # %" PRIu64 " rx_size %d, p_out_frame->data_len[0] "
               "%d, p_out_frame->data_len[1] %d, "
               "p_out_frame->data_len[2] %d, sum %d\n",
               p_dec_ctx->frame_num, rx_size, p_out_frame->data_len[0],
               p_out_frame->data_len[1], p_out_frame->data_len[2],
               p_out_frame->data_len[0] + p_out_frame->data_len[1] +
                   p_out_frame->data_len[2]);
    }
    // rx_size == 0 means no decoded frame is available now

    *total_bytes_recieved += rx_size;

    //TODO: If this a BUG?? we have already updated the *total_bytes_recieved above
    if (write_to_file)
    {
        *total_bytes_recieved += rx_size;
    }

#if 0
  for(i=0;i<NI_MAX_NUM_DATA_POINTERS;i++)
  {
    if(p_out_frame->data_len[i] > 0)
    {
      ni_log(NI_LOG_DEBUG, "decoder_receive_data: writing data size=%d, %d\n", p_out_frame->data_len[i], i);
      fwrite(p_out_frame->p_data[i], p_out_frame->data_len[i], 1, p_file);
      if (fflush(p_file))
      {
        ni_log(NI_LOG_ERROR, "decoder_receive_data_buf Writing data frame flush failed! errno %d\n", errno);
      }
      *total_bytes_recieved += p_out_frame->data_len[i];
    }
  }
#endif

    ni_log(NI_LOG_DEBUG, "decoder_receive_data_buf 003\n");

    /*
  if (print_time)
  {
    ni_log(NI_LOG_DEBUG, "[R] Got:%d  Frames= %ul  fps=%lu  Total bytes %llu\n",
        rx_size, number_of_frames, (unsigned long) (number_of_frames/(current_time.tv_sec - start_time.tv_sec)), (unsigned long long) *total_bytes_recieved);
  }
*/
    if (end_flag)
    {
        ni_log(NI_LOG_DEBUG, "Receiving done.\n");
        p_device_state->dec_eos_received = 1;
        rc = 1;
    } else if (0 == rx_size)
    {
        rc = 2;
    }

    ni_log(NI_LOG_DEBUG, "decoder_receive_data: success rc %d\n", rc);

    END;

    return rc;
}

WORD32 ifwh264d_set_num_cores(iv_obj_t *dec_hdl, void *pv_api_ip,
                              void *pv_api_op);

WORD32 ifwh264d_get_vui_params(iv_obj_t *dec_hdl, void *pv_api_ip,
                               void *pv_api_op);

void ifwh264d_fill_output_struct_from_context(dec_struct_t *ps_dec,
                                              ivd_video_decode_op_t *ps_dec_op);

static IV_API_CALL_STATUS_T
api_check_struct_sanity(iv_obj_t *ps_handle, void *pv_api_ip, void *pv_api_op)
{
    IVD_API_COMMAND_TYPE_T e_cmd;
    UWORD32 *pu4_api_ip;
    UWORD32 *pu4_api_op;

    if (NULL == pv_api_op)
        return (IV_FAIL);

    if (NULL == pv_api_ip)
        return (IV_FAIL);

    pu4_api_ip = (UWORD32 *)pv_api_ip;
    pu4_api_op = (UWORD32 *)pv_api_op;
    e_cmd = *(pu4_api_ip + 1);

    /* error checks on handle */
    ni_log(NI_LOG_DEBUG, "ifwh264d:  api_check_struct_sanity e_cmd: %d", e_cmd);

    switch ((WORD32)e_cmd)
    {
        case IVD_CMD_CREATE:
            break;
        case IVD_CMD_FWCHECK:
            break;

        case IVD_CMD_VIDEO_DECODE:
        case IVD_CMD_DELETE:
        case IVD_CMD_VIDEO_CTL:
            if (ps_handle == NULL)
            {
                *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
                *(pu4_api_op + 1) |= IVD_HANDLE_NULL;
                return IV_FAIL;
            }

            if (ps_handle->u4_size != sizeof(iv_obj_t))
            {
                *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
                *(pu4_api_op + 1) |= IVD_HANDLE_STRUCT_SIZE_INCORRECT;
                return IV_FAIL;
            }

            if (ps_handle->pv_fxns != ifwh264d_api_function)
            {
                *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
                *(pu4_api_op + 1) |= IVD_INVALID_HANDLE_NULL;
                return IV_FAIL;
            }

            if (ps_handle->pv_codec_handle == NULL)
            {
                *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
                *(pu4_api_op + 1) |= IVD_INVALID_HANDLE_NULL;
                return IV_FAIL;
            }
            break;
        default:
            *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
            *(pu4_api_op + 1) |= IVD_INVALID_API_CMD;
            return IV_FAIL;
    }

    switch ((WORD32)e_cmd)
    {
        case IVD_CMD_CREATE:
        {
            ifwh264d_create_ip_t *ps_ip = (ifwh264d_create_ip_t *)pv_api_ip;
            ifwh264d_create_op_t *ps_op = (ifwh264d_create_op_t *)pv_api_op;

            ps_op->s_ivd_create_op_t.u4_error_code = 0;

            if ((ps_ip->s_ivd_create_ip_t.u4_size >
                 sizeof(ifwh264d_create_ip_t)) ||
                (ps_ip->s_ivd_create_ip_t.u4_size < sizeof(ivd_create_ip_t)))
            {
                ps_op->s_ivd_create_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_create_op_t.u4_error_code |=
                    IVD_IP_API_STRUCT_SIZE_INCORRECT;
                ni_log(NI_LOG_DEBUG, "\n");
                return (IV_FAIL);
            }

            if ((ps_op->s_ivd_create_op_t.u4_size !=
                 sizeof(ifwh264d_create_op_t)) &&
                (ps_op->s_ivd_create_op_t.u4_size != sizeof(ivd_create_op_t)))
            {
                ps_op->s_ivd_create_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_create_op_t.u4_error_code |=
                    IVD_OP_API_STRUCT_SIZE_INCORRECT;
                ni_log(NI_LOG_DEBUG, "\n");
                return (IV_FAIL);
            }

            if ((ps_ip->s_ivd_create_ip_t.e_output_format != IV_YUV_420P) &&
                (ps_ip->s_ivd_create_ip_t.e_output_format != IV_YUV_422ILE) &&
                (ps_ip->s_ivd_create_ip_t.e_output_format != IV_RGB_565) &&
                (ps_ip->s_ivd_create_ip_t.e_output_format != IV_YUV_420SP_UV) &&
                (ps_ip->s_ivd_create_ip_t.e_output_format != IV_YUV_420SP_VU))
            {
                ps_op->s_ivd_create_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_create_op_t.u4_error_code |=
                    IVD_INIT_DEC_COL_FMT_NOT_SUPPORTED;
                ni_log(NI_LOG_DEBUG, "\n");
                return (IV_FAIL);
            }
        }
        break;

        case IVD_CMD_FWCHECK:
        {
            ifwh264d_fwcheck_ip_t *ps_ip = (ifwh264d_fwcheck_ip_t *)pv_api_ip;
            ifwh264d_fwcheck_op_t *ps_op = (ifwh264d_fwcheck_op_t *)pv_api_op;

            ps_op->s_ivd_fwcheck_op_t.u4_error_code = 0;

            if (ps_ip->s_ivd_fwcheck_ip_t.u4_size !=
                sizeof(ifwh264d_fwcheck_ip_t))
            {
                ps_op->s_ivd_fwcheck_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_fwcheck_op_t.u4_error_code |=
                    IVD_IP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }

            if (ps_op->s_ivd_fwcheck_op_t.u4_size !=
                sizeof(ifwh264d_fwcheck_op_t))
            {
                ps_op->s_ivd_fwcheck_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_fwcheck_op_t.u4_error_code |=
                    IVD_OP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }
        }
        break;

        case IVD_CMD_VIDEO_DECODE:
        {
            ifwh264d_video_decode_ip_t *ps_ip =
                (ifwh264d_video_decode_ip_t *)pv_api_ip;
            ifwh264d_video_decode_op_t *ps_op =
                (ifwh264d_video_decode_op_t *)pv_api_op;

            ni_log(NI_LOG_DEBUG, "The input bytes is: %d",
                   ps_ip->s_ivd_video_decode_ip_t.u4_num_Bytes);
            ps_op->s_ivd_video_decode_op_t.u4_error_code = 0;

            if (ps_ip->s_ivd_video_decode_ip_t.u4_size !=
                    sizeof(ifwh264d_video_decode_ip_t) &&
                ps_ip->s_ivd_video_decode_ip_t.u4_size !=
                    offsetof(ivd_video_decode_ip_t, s_out_buffer))
            {
                ps_op->s_ivd_video_decode_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_video_decode_op_t.u4_error_code |=
                    IVD_IP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }

            if (ps_op->s_ivd_video_decode_op_t.u4_size !=
                    sizeof(ifwh264d_video_decode_op_t) &&
                ps_op->s_ivd_video_decode_op_t.u4_size !=
                    offsetof(ivd_video_decode_op_t, u4_output_present))
            {
                ps_op->s_ivd_video_decode_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_video_decode_op_t.u4_error_code |=
                    IVD_OP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }
        }
        break;

        case IVD_CMD_DELETE:
        {
            ifwh264d_delete_ip_t *ps_ip = (ifwh264d_delete_ip_t *)pv_api_ip;
            ifwh264d_delete_op_t *ps_op = (ifwh264d_delete_op_t *)pv_api_op;

            ps_op->s_ivd_delete_op_t.u4_error_code = 0;

            if (ps_ip->s_ivd_delete_ip_t.u4_size !=
                sizeof(ifwh264d_delete_ip_t))
            {
                ps_op->s_ivd_delete_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_delete_op_t.u4_error_code |=
                    IVD_IP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }

            if (ps_op->s_ivd_delete_op_t.u4_size !=
                sizeof(ifwh264d_delete_op_t))
            {
                ps_op->s_ivd_delete_op_t.u4_error_code |= 1
                    << IVD_UNSUPPORTEDPARAM;
                ps_op->s_ivd_delete_op_t.u4_error_code |=
                    IVD_OP_API_STRUCT_SIZE_INCORRECT;
                return (IV_FAIL);
            }
        }
        break;

        case IVD_CMD_VIDEO_CTL:
        {
            UWORD32 *pu4_ptr_cmd;
            UWORD32 sub_command;

            pu4_ptr_cmd = (UWORD32 *)pv_api_ip;
            pu4_ptr_cmd += 2;
            sub_command = *pu4_ptr_cmd;

            switch (sub_command)
            {
                case IVD_CMD_CTL_SETPARAMS:
                {
                    ih264d_ctl_set_config_ip_t *ps_ip;
                    ih264d_ctl_set_config_op_t *ps_op;
                    ps_ip = (ih264d_ctl_set_config_ip_t *)pv_api_ip;
                    ps_op = (ih264d_ctl_set_config_op_t *)pv_api_op;

                    if (ps_ip->s_ivd_ctl_set_config_ip_t.u4_size !=
                        sizeof(ih264d_ctl_set_config_ip_t))
                    {
                        ps_op->s_ivd_ctl_set_config_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_set_config_op_t.u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                }
                break;

                case IVD_CMD_CTL_GETVERSION:
                {
                    ih264d_ctl_getversioninfo_ip_t *ps_ip;
                    ih264d_ctl_getversioninfo_op_t *ps_op;
                    ps_ip = (ih264d_ctl_getversioninfo_ip_t *)pv_api_ip;
                    ps_op = (ih264d_ctl_getversioninfo_op_t *)pv_api_op;
                    if (ps_ip->s_ivd_ctl_getversioninfo_ip_t.u4_size !=
                        sizeof(ih264d_ctl_getversioninfo_ip_t))
                    {
                        ps_op->s_ivd_ctl_getversioninfo_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_getversioninfo_op_t.u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                    if (ps_op->s_ivd_ctl_getversioninfo_op_t.u4_size !=
                        sizeof(ih264d_ctl_getversioninfo_op_t))
                    {
                        ps_op->s_ivd_ctl_getversioninfo_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_getversioninfo_op_t.u4_error_code |=
                            IVD_OP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                }
                break;

                case IVD_CMD_CTL_FLUSH:
                {
                    ih264d_ctl_flush_ip_t *ps_ip;
                    ih264d_ctl_flush_op_t *ps_op;
                    ps_ip = (ih264d_ctl_flush_ip_t *)pv_api_ip;
                    ps_op = (ih264d_ctl_flush_op_t *)pv_api_op;
                    if (ps_ip->s_ivd_ctl_flush_ip_t.u4_size !=
                        sizeof(ih264d_ctl_flush_ip_t))
                    {
                        ps_op->s_ivd_ctl_flush_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_flush_op_t.u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                    if (ps_op->s_ivd_ctl_flush_op_t.u4_size !=
                        sizeof(ih264d_ctl_flush_op_t))
                    {
                        ps_op->s_ivd_ctl_flush_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_flush_op_t.u4_error_code |=
                            IVD_OP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                }
                break;

                case IVD_CMD_CTL_RESET:
                {
                    ih264d_ctl_reset_ip_t *ps_ip;
                    ih264d_ctl_reset_op_t *ps_op;
                    ps_ip = (ih264d_ctl_reset_ip_t *)pv_api_ip;
                    ps_op = (ih264d_ctl_reset_op_t *)pv_api_op;
                    if (ps_ip->s_ivd_ctl_reset_ip_t.u4_size !=
                        sizeof(ih264d_ctl_reset_ip_t))
                    {
                        ps_op->s_ivd_ctl_reset_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_reset_op_t.u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                    if (ps_op->s_ivd_ctl_reset_op_t.u4_size !=
                        sizeof(ih264d_ctl_reset_op_t))
                    {
                        ps_op->s_ivd_ctl_reset_op_t.u4_error_code |= 1
                            << IVD_UNSUPPORTEDPARAM;
                        ps_op->s_ivd_ctl_reset_op_t.u4_error_code |=
                            IVD_OP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }
                    break;
                }

                case IH264D_CMD_CTL_GET_VUI_PARAMS:
                {
                    ih264d_ctl_get_vui_params_ip_t *ps_ip;
                    ih264d_ctl_get_vui_params_op_t *ps_op;

                    ps_ip = (ih264d_ctl_get_vui_params_ip_t *)pv_api_ip;
                    ps_op = (ih264d_ctl_get_vui_params_op_t *)pv_api_op;

                    if (ps_ip->u4_size !=
                        sizeof(ih264d_ctl_get_vui_params_ip_t))
                    {
                        ps_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                        ps_op->u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }

                    if (ps_op->u4_size !=
                        sizeof(ih264d_ctl_get_vui_params_op_t))
                    {
                        ps_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                        ps_op->u4_error_code |=
                            IVD_OP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }

                    break;
                }
                case IFWH264D_CMD_CTL_SET_NUM_CORES:
                {
                    ifwh264d_ctl_set_num_cores_ip_t *ps_ip;
                    ifwh264d_ctl_set_num_cores_op_t *ps_op;

                    ps_ip = (ifwh264d_ctl_set_num_cores_ip_t *)pv_api_ip;
                    ps_op = (ifwh264d_ctl_set_num_cores_op_t *)pv_api_op;

                    if (ps_ip->u4_size !=
                        sizeof(ifwh264d_ctl_set_num_cores_ip_t))
                    {
                        ps_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                        ps_op->u4_error_code |=
                            IVD_IP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }

                    if (ps_op->u4_size !=
                        sizeof(ifwh264d_ctl_set_num_cores_op_t))
                    {
                        ps_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                        ps_op->u4_error_code |=
                            IVD_OP_API_STRUCT_SIZE_INCORRECT;
                        return IV_FAIL;
                    }

                    if ((ps_ip->u4_num_cores != 1) &&
                        (ps_ip->u4_num_cores != 2) &&
                        (ps_ip->u4_num_cores != 3) &&
                        (ps_ip->u4_num_cores != 4))
                    {
                        ps_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                        return IV_FAIL;
                    }
                    break;
                }
                default:
                    *(pu4_api_op + 1) |= 1 << IVD_UNSUPPORTEDPARAM;
                    *(pu4_api_op + 1) |= IVD_UNSUPPORTED_API_CMD;
                    return IV_FAIL;
                    break;
            }
        }
        break;
    }

    return IV_SUCCESS;
}

/**************************************************************************
 * \if Function name : ifwh264d_init_decoder \endif
 *
 *
 * \brief
 *    Initializes the decoder
 *
 * \param apiVersion               : Version of the api being used.
 * \param errorHandlingMechanism   : Mechanism to be used for errror handling.
 * \param postFilteringType: Type of post filtering operation to be used.
 * \param uc_outputFormat: Format of the decoded picture [default 4:2:0].
 * \param uc_dispBufs: Number of Display Buffers.
 * \param p_NALBufAPI: Pointer to NAL Buffer API.
 * \param p_DispBufAPI: Pointer to Display Buffer API.
 * \param ih264d_dec_mem_manager  :Pointer to the function that will be called by decoder
 *                        for memory allocation and freeing.
 *
 * \return
 *    0 on Success and -1 on error
 *
 **************************************************************************
 */
void ifwh264d_init_decoder(void *ps_dec_params)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_init_decoder");

    char value[PROPERTY_VALUE_MAX];
    property_get(PROP_DECODER_LOGLEVEL, value, "3");
    unsigned loglevel = atoi(value);
    ni_log(NI_LOG_DEBUG, "ifwh264d:  AVC decoder using %s, hw_id %d\n",
           PROP_DECODER_LOGLEVEL, loglevel);
    if (loglevel > NI_LOG_TRACE)
    {
        loglevel = NI_LOG_TRACE;
    }
    ni_log_set_level(loglevel);

    ni_log(NI_LOG_DEBUG, "ifwh264d:  ni_log_get_level: %d", ni_log_get_level());

    dec_struct_t *ps_dec = (dec_struct_t *)ps_dec_params;

    /* decParams Initializations */
    ps_dec->u4_app_disp_width = 0;
    ps_dec->i4_header_decoded = 0;
    ps_dec->u4_total_frames_decoded = 0;

    ps_dec->i4_error_code = 0;
    ps_dec->i4_content_type = -1;

    ps_dec->u2_pic_ht = ps_dec->u2_pic_wd = 0;

    /* Set the cropping parameters as zero */
    ps_dec->u2_crop_offset_y = 0;
    ps_dec->u2_crop_offset_uv = 0;

    /* The Initial Frame Rate Info is not Present */
    ps_dec->i4_vui_frame_rate = -1;
    ps_dec->i4_pic_type = -1;
    ps_dec->i4_frametype = -1;

    ps_dec->u1_res_changed = 0;

    /* Initializing flush frame u4_flag */
    ps_dec->u1_flushfrm = 0;
    ps_dec->u1_resetfrm = 0;

    XcodecDecStruct *xcodec_dec = ps_dec->xcodec_dec;

    ni_device_session_context_init(&(xcodec_dec->dec_ctx));
    xcodec_dec->dec_ctx.codec_format = NI_CODEC_FORMAT_H264;
    xcodec_dec->dec_ctx.bit_depth_factor = 1;
    xcodec_dec->dec_ctx.keep_alive_timeout = 10;

    xcodec_dec->sdPara.p_dec_ctx = (void *)&(xcodec_dec->dec_ctx);
    xcodec_dec->rcPara.p_dec_ctx = (void *)&(xcodec_dec->dec_ctx);

    ni_log(NI_LOG_DEBUG,
           "ifwh264d:  ifwh264d_init_decoder xcodec_dec->input_video_width %d, "
           "xcodec_dec->input_video_height %d",
           xcodec_dec->input_video_width, xcodec_dec->input_video_height);

    if (ni_decoder_init_default_params(&(xcodec_dec->api_param_dec), 25, 1,
                                       200000, xcodec_dec->input_video_width,
                                       xcodec_dec->input_video_height) < 0)
    {
        ni_log(NI_LOG_DEBUG, "ni_decoder_init_default_params failure !\n");

        return;
    }

    ni_log(NI_LOG_DEBUG, "ni_decoder_init_default_params success !\n");

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get(PROP_DECODER_HW_ID, value, 0);
    unsigned hw_id = atoi(value);
    ni_log(NI_LOG_DEBUG, "AVC decoder using %s, hw_id %d\n", PROP_DECODER_HW_ID,
           hw_id);
    if (hw_id > 32)
    {
        ni_log(NI_LOG_ERROR, "Error card hw_id %d!!!!!!!!!!!!!!!!!!!\n", hw_id);
        hw_id = 0;
    }

    xcodec_dec->dec_ctx.hw_id = hw_id;

    // default: 8 bit, little endian
    xcodec_dec->dec_ctx.p_session_config = &(xcodec_dec->api_param_dec);
    xcodec_dec->dec_ctx.src_bit_depth = 8;
    xcodec_dec->dec_ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;

    int err =
        ni_device_session_open(&(xcodec_dec->dec_ctx), NI_DEVICE_TYPE_DECODER);
    if (err < 0)
    {
        ni_log(NI_LOG_DEBUG, "ni_decoder_session_open failure !\n");
        return;
    }

    ni_log(NI_LOG_DEBUG, "ni_device_session_open success !\n");

    xcodec_dec->sos_flag = 1;
    xcodec_dec->edFlag = 0;
    xcodec_dec->bytes_sent = 0;
    xcodec_dec->bytes_recv = 0;
    xcodec_dec->total_bytes_recieved = 0;
    xcodec_dec->xcodeRecvTotal = 0;
    xcodec_dec->total_bytes_sent = 0;

    if (xcodec_dec->input_video_width == 0 ||
        xcodec_dec->input_video_height == 0)
    {
        xcodec_dec->input_video_width = 1280;
        xcodec_dec->input_video_height = 720;
    }

    xcodec_dec->output_video_width = xcodec_dec->input_video_width;
    xcodec_dec->output_video_height = xcodec_dec->input_video_height;

    memset(&(xcodec_dec->in_pkt), 0, sizeof(ni_session_data_io_t));
    memset(&(xcodec_dec->out_frame), 0, sizeof(ni_session_data_io_t));

    ps_dec->u1_frame_decoded_flag = 0;

    ps_dec->init_done = 1;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_create                                              */
/*                                                                           */
/*  Description   : creates decoder                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_allocate_static_bufs(iv_obj_t **dec_hdl, void *pv_api_ip,
                                     void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_allocate_static_bufs");

    ifwh264d_create_ip_t *ps_create_ip;
    ifwh264d_create_op_t *ps_create_op;
    void *pv_buf;
    dec_struct_t *ps_dec;
    void *(*pf_aligned_alloc)(void *pv_mem_ctxt, WORD32 alignment, WORD32 size);
    void (*pf_aligned_free)(void *pv_mem_ctxt, void *pv_buf);
    void *pv_mem_ctxt;

    if ((NULL == dec_hdl) || (NULL == pv_api_ip) || (NULL == pv_api_op))
    {
        return IV_FAIL;
    }

    ps_create_ip = (ifwh264d_create_ip_t *)pv_api_ip;
    ps_create_op = (ifwh264d_create_op_t *)pv_api_op;

    ps_create_op->s_ivd_create_op_t.u4_error_code = 0;

    pf_aligned_alloc = ps_create_ip->s_ivd_create_ip_t.pf_aligned_alloc;
    pf_aligned_free = ps_create_ip->s_ivd_create_ip_t.pf_aligned_free;
    pv_mem_ctxt = ps_create_ip->s_ivd_create_ip_t.pv_mem_ctxt;

    /* Initialize return handle to NULL */
    ps_create_op->s_ivd_create_op_t.pv_handle = NULL;
    pv_buf = pf_aligned_alloc(pv_mem_ctxt, 128, sizeof(iv_obj_t));
    //RETURN_IF((NULL == pv_buf), IV_FAIL);
    if (NULL == pv_buf)
    {
        return IV_FAIL;
    }
    *dec_hdl = (iv_obj_t *)pv_buf;
    ps_create_op->s_ivd_create_op_t.pv_handle = *dec_hdl;

    (*dec_hdl)->pv_codec_handle = NULL;
    pv_buf = pf_aligned_alloc(pv_mem_ctxt, 128, sizeof(dec_struct_t));
    //RETURN_IF((NULL == pv_buf), IV_FAIL);
    if (NULL == pv_buf)
    {
        return IV_FAIL;
    }
    (*dec_hdl)->pv_codec_handle = (dec_struct_t *)pv_buf;

    ps_dec = (dec_struct_t *)pv_buf;
    memset(ps_dec, 0, sizeof(dec_struct_t));

    ps_dec->pf_aligned_alloc = pf_aligned_alloc;
    ps_dec->pf_aligned_free = pf_aligned_free;
    ps_dec->pv_mem_ctxt = pv_mem_ctxt;

    pv_buf = pf_aligned_alloc(pv_mem_ctxt, 128, sizeof(XcodecDecStruct));
    RETURN_IF((NULL == pv_buf), IV_FAIL);
    if (NULL == pv_buf)
    {
        return IV_FAIL;
    }
    ps_dec->xcodec_dec = pv_buf;

    return IV_SUCCESS;
}

WORD32 ifwh264d_free_static_bufs(iv_obj_t *dec_hdl)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_free_static_bufs");
    dec_struct_t *ps_dec;

    void (*pf_aligned_free)(void *pv_mem_ctxt, void *pv_buf);
    void *pv_mem_ctxt;

    ps_dec = (dec_struct_t *)dec_hdl->pv_codec_handle;
    pf_aligned_free = ps_dec->pf_aligned_free;
    pv_mem_ctxt = ps_dec->pv_mem_ctxt;

    PS_DEC_ALIGNED_FREE(ps_dec, ps_dec->xcodec_dec);

    PS_DEC_ALIGNED_FREE(ps_dec, dec_hdl->pv_codec_handle);

    if (dec_hdl)
    {
        pf_aligned_free(pv_mem_ctxt, dec_hdl);
    }
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_free_static_bufs end");
    return IV_SUCCESS;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_create                                              */
/*                                                                           */
/*  Description   : creates decoder                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_create(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_create");

    ifwh264d_create_ip_t *ps_create_ip;
    ifwh264d_create_op_t *ps_create_op;

    WORD32 ret;

    ps_create_ip = (ifwh264d_create_ip_t *)pv_api_ip;
    ps_create_op = (ifwh264d_create_op_t *)pv_api_op;

    ps_create_op->s_ivd_create_op_t.u4_error_code = 0;
    dec_hdl = NULL;
    ret = ifwh264d_allocate_static_bufs(&dec_hdl, pv_api_ip, pv_api_op);

    /* If allocation of some buffer fails, then free buffers allocated till then */
    if (IV_FAIL == ret)
    {
        if (dec_hdl)
        {
            if (dec_hdl->pv_codec_handle)
            {
                ifwh264d_free_static_bufs(dec_hdl);
            } else
            {
                void (*pf_aligned_free)(void *pv_mem_ctxt, void *pv_buf);
                void *pv_mem_ctxt;

                pf_aligned_free =
                    ps_create_ip->s_ivd_create_ip_t.pf_aligned_free;
                pv_mem_ctxt = ps_create_ip->s_ivd_create_ip_t.pv_mem_ctxt;
                pf_aligned_free(pv_mem_ctxt, dec_hdl);
            }
        }
        //ps_create_op->s_ivd_create_op_t.u4_error_code = IVD_MEM_ALLOC_FAILED;
        ps_create_op->s_ivd_create_op_t.u4_error_code = 1 << IVD_FATALERROR;

        return IV_FAIL;
    }

    if (send_dump != 0)
    {
        pIn_file = fopen(mInFile, "ab");
        if (pIn_file == NULL)
        {
            ni_log(NI_LOG_ERROR, "ERROR: Cannot open mInFile\n");
        }
    }

    if (receive_dump != 0)
    {
        p_file = fopen(mdecoderFile, "ab");
        if (p_file == NULL)
        {
            ni_log(NI_LOG_ERROR, "ERROR: Cannot open mdecoderFile\n");
        }
    }

    //FW_CREATE_DUMP_FILE(mInFile);

    return IV_SUCCESS;
}

UWORD32 ifwh264d_get_outbuf_size(WORD32 pic_wd, UWORD32 pic_ht,
                                 UWORD8 u1_chroma_format, UWORD32 *p_buf_size)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_get_outbuf_size");

    UWORD32 u4_min_num_out_bufs = 0;

    if (u1_chroma_format == IV_YUV_420P)
        u4_min_num_out_bufs = MIN_OUT_BUFS_420;
    else if (u1_chroma_format == IV_YUV_422ILE)
        u4_min_num_out_bufs = MIN_OUT_BUFS_422ILE;
    else if (u1_chroma_format == IV_RGB_565)
        u4_min_num_out_bufs = MIN_OUT_BUFS_RGB565;
    else if ((u1_chroma_format == IV_YUV_420SP_UV) ||
             (u1_chroma_format == IV_YUV_420SP_VU))
        u4_min_num_out_bufs = MIN_OUT_BUFS_420SP;

    if (u1_chroma_format == IV_YUV_420P)
    {
        p_buf_size[0] = (pic_wd * pic_ht);
        p_buf_size[1] = (pic_wd * pic_ht) >> 2;
        p_buf_size[2] = (pic_wd * pic_ht) >> 2;
    } else if (u1_chroma_format == IV_YUV_422ILE)
    {
        p_buf_size[0] = (pic_wd * pic_ht) * 2;
        p_buf_size[1] = p_buf_size[2] = 0;
    } else if (u1_chroma_format == IV_RGB_565)
    {
        p_buf_size[0] = (pic_wd * pic_ht) * 2;
        p_buf_size[1] = p_buf_size[2] = 0;
    } else if ((u1_chroma_format == IV_YUV_420SP_UV) ||
               (u1_chroma_format == IV_YUV_420SP_VU))
    {
        p_buf_size[0] = (pic_wd * pic_ht);
        p_buf_size[1] = (pic_wd * pic_ht) >> 1;
        p_buf_size[2] = 0;
    }

    return u4_min_num_out_bufs;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name :  ifwh264d_video_decode                                     */
/*                                                                           */
/*  Description   :  handle video decode API command                         */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/

WORD32 ifwh264d_video_decode(iv_obj_t *dec_hdl, void *pv_api_ip,
                             void *pv_api_op)
{
    /* ! */

    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_video_decode");

    dec_struct_t *ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);

    UWORD8 *pu1_buf = NULL;
    device_state_t xcodeState = {0};

    UWORD32 u4_next_is_aud;
    WORD32 api_ret_value = IV_SUCCESS;
    WORD32 header_data_left = 0, frame_data_left = 0;
    ivd_video_decode_ip_t *ps_dec_ip;
    ivd_video_decode_op_t *ps_dec_op;

    ps_dec_ip = (ivd_video_decode_ip_t *)pv_api_ip;
    ps_dec_op = (ivd_video_decode_op_t *)pv_api_op;

    {
        UWORD32 u4_size;
        u4_size = ps_dec_op->u4_size;
        memset(ps_dec_op, 0, sizeof(ivd_video_decode_op_t));
        ps_dec_op->u4_size = u4_size;
    }

    ps_dec_op->u4_num_bytes_consumed = 0;

    ps_dec->u4_ts = ps_dec_ip->u4_ts;
    ps_dec_op->u4_error_code = 0;
    ps_dec_op->u4_output_present = ps_dec->u4_output_present = 0;
    ps_dec_op->u4_frame_decoded_flag = 0;
    //ps_dec_op->u4_output_present = ps_dec->u4_output_present = 1;

    XcodecDecStruct *xcodec_dec = ps_dec->xcodec_dec;

    int output_video_width = xcodec_dec->input_video_width;
    int output_video_height = xcodec_dec->input_video_height;

    /* In case the deocder is not in flush mode(in shared mode),
     then decoder has to pick up a buffer to write current frame.
     Check if a frame is available in such cases */

    ni_log(NI_LOG_DEBUG,
           "ifwh264d_video_decode ps_dec_ip->u4_num_Bytes: %d, "
           "ps_dec_op->u4_num_bytes_consumed: %d\n",
           ps_dec_ip->u4_num_Bytes, ps_dec_op->u4_num_bytes_consumed);

    send_fin_flag = 0;
    recieve_fin_flag = 0;
    xcodec_dec->bytes_recv = 0;

    (void)gettimeofday(&current_time, NULL);

    int flush_enable = ps_dec->u1_flushfrm;
    do
    {
        pu1_buf = (UWORD8 *)ps_dec_ip->pv_stream_buffer +
            ps_dec_op->u4_num_bytes_consumed;

        UWORD32 u4_max_ofst =
            ps_dec_ip->u4_num_Bytes - ps_dec_op->u4_num_bytes_consumed;

        (void)gettimeofday(&current_time, NULL);
        int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

        if (ps_dec->u1_resetfrm == 0 && ps_dec_ip->u4_num_Bytes == 0)
        {
            ni_log(NI_LOG_DEBUG,
                   "ifwh264d_video_decode ps_dec_ip->u4_num_Bytes: %d\n",
                   ps_dec_ip->u4_num_Bytes);
        } else
        {
            //FW_DUMP_TO_FILE(mInFile, pu1_buf, ps_dec_ip->u4_num_Bytes, 0);

            // Sending
            send_fin_flag = decoder_send_data_buf(
                &xcodec_dec->dec_ctx, &xcodec_dec->in_pkt, xcodec_dec->sos_flag,
                xcodec_dec->input_video_width, xcodec_dec->input_video_height,
                pIn_file, pu1_buf, ps_dec_ip->u4_num_Bytes, u4_max_ofst,
                &xcodec_dec->total_bytes_sent, print_time, send_dump,
                &xcodeState);
            xcodec_dec->sos_flag = 0;
            if (send_fin_flag < 0)   //Error
            {
                ni_log(NI_LOG_ERROR,
                       "Error: decoder_send_data failed, rc: %d\n",
                       send_fin_flag);
                break;
            }

            /* If dynamic bitstream buffer is not allocated and
             * header decode is done, then allocate dynamic bitstream buffer
             */
        }

        ps_dec_op->u4_num_bytes_consumed = ps_dec_ip->u4_num_Bytes;

        u4_next_is_aud = 0;

        header_data_left =
            ((ps_dec->i4_decode_header == 1) &&
             (ps_dec->i4_header_decoded != 3) &&
             (ps_dec_op->u4_num_bytes_consumed < ps_dec_ip->u4_num_Bytes));
        frame_data_left =
            (((ps_dec->i4_decode_header == 0) &&
              ((ps_dec->u1_pic_decode_done == 0) || (u4_next_is_aud == 1))) &&
             (ps_dec_op->u4_num_bytes_consumed < ps_dec_ip->u4_num_Bytes));

        if (flush_enable)
        {
            frame_data_left = 1;
        }

        ps_dec->ps_out_buffer = NULL;

        if (ps_dec_ip->u4_size >= offsetof(ivd_video_decode_ip_t, s_out_buffer))
            ps_dec->ps_out_buffer = &ps_dec_ip->s_out_buffer;

        ni_log(NI_LOG_DEBUG, "ifwh264d_video_decode 000\n");
        // Receiving
        recieve_fin_flag = decoder_receive_data_buf(
            &xcodec_dec->dec_ctx, &xcodec_dec->out_frame, output_video_width,
            output_video_height, p_file, &xcodec_dec->bytes_recv,
            &xcodec_dec->total_bytes_recieved, print_time, receive_dump,
            &xcodeState);

        ni_log(NI_LOG_DEBUG, "ifwh264d_video_decode 001 recieve_fin_flag %d\n",
               recieve_fin_flag);
        if (print_time)
        {
            previous_time = current_time;
        }

        if (recieve_fin_flag < 0 ||
            xcodec_dec->out_frame.data.frame.end_of_stream)   //Error or eos
        {
            break;
        }

        ni_log(NI_LOG_DEBUG,
               "ifwh264d_video_decode 003 xcodec_dec->bytes_recv %d, Y %d, U "
               "%d, V %d\n",
               xcodec_dec->bytes_recv,
               xcodec_dec->out_frame.data.frame.data_len[0],
               xcodec_dec->out_frame.data.frame.data_len[1],
               xcodec_dec->out_frame.data.frame.data_len[2]);
#if 1
        ps_dec->ps_out_buffer->u4_num_bufs_size = xcodec_dec->bytes_recv;
        if (xcodec_dec->bytes_recv > 0)
        {
            int i, j;
            ni_session_context_t *p_dec_ctx = &xcodec_dec->dec_ctx;
            ni_session_data_io_t *p_out_data = &xcodec_dec->out_frame;
            ni_frame_t *p_out_frame = &(p_out_data->data.frame);
            for (i = 0; i < 3; i++)
            {
                uint8_t *src = p_out_frame->p_data[i];
                uint8_t *dst = ps_dec->ps_out_buffer->pu1_bufs[i];
                int plane_height = p_dec_ctx->active_video_height;
                int plane_width = p_dec_ctx->active_video_width;
                int write_height = output_video_height;
                int write_width = output_video_width;

                // support for 8/10 bit depth
                write_width *= p_dec_ctx->bit_depth_factor;

                if (i == 1 || i == 2)
                {
                    plane_height /= 2;
                    plane_width = (((int)(p_dec_ctx->actual_video_width) / 2 *
                                        p_dec_ctx->bit_depth_factor +
                                    127) /
                                   128) *
                        128;
                    write_height /= 2;
                    write_width /= 2;
                }

                // apply the cropping windown in writing out the YUV frame
                // for now the windown is usually crop-left = crop-top = 0, and we use
                // this to simplify the cropping logic
                for (j = 0; j < plane_height; j++)
                {
                    if (j < write_height)
                    {
                        memcpy(dst, src, write_width);
                        dst += write_width;
                        p_out_frame->data_len[i] += write_width;
                    }
                    src += plane_width;
                }
            }

            if (flush_enable)
            {
                flush_enable = 0;
            }

            //memcpy(ps_dec->ps_out_buffer->pu1_bufs[0], xcodec_dec->out_frame.data.frame.p_data[0], xcodec_dec->out_frame.data.frame.data_len[0]);
            //memcpy(ps_dec->ps_out_buffer->pu1_bufs[1], xcodec_dec->out_frame.data.frame.p_data[1], xcodec_dec->out_frame.data.frame.data_len[1]);
            //memcpy(ps_dec->ps_out_buffer->pu1_bufs[2], xcodec_dec->out_frame.data.frame.p_data[2], xcodec_dec->out_frame.data.frame.data_len[2]);

            ps_dec_op->u4_ts = ps_dec->u4_ts;
            ps_dec_op->u4_output_present = ps_dec->u4_output_present = 1;
        }

        ni_log(NI_LOG_DEBUG,
               "ifwh264d_video_decode 004 after padding xcodec_dec->bytes_recv "
               "%d, Y %d, U %d, V %d\n",
               xcodec_dec->bytes_recv,
               xcodec_dec->out_frame.data.frame.data_len[0],
               xcodec_dec->out_frame.data.frame.data_len[1],
               xcodec_dec->out_frame.data.frame.data_len[2]);

        ni_decoder_frame_buffer_free(&(xcodec_dec->out_frame.data.frame));

#endif

#if 0

        UWORD32 i;
        int x, y;
        static int frameNumb = 0;
        for(i = 0; i < ps_dec->ps_out_buffer->u4_num_bufs; i++)
        {
            if(ps_dec->ps_out_buffer->pu1_bufs[i] == NULL)
            {
                ps_dec_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                ps_dec_op->u4_error_code |= IVD_DISP_FRM_OP_BUF_NULL;
                return IV_FAIL;
            }

            if(ps_dec->ps_out_buffer->u4_min_out_buf_size[i] == 0)
            {
                ps_dec_op->u4_error_code |= 1 << IVD_UNSUPPORTEDPARAM;
                ps_dec_op->u4_error_code |=
                                IVD_DISP_FRM_ZERO_OP_BUF_SIZE;
                return IV_FAIL;
            }

            if(i == 0){
                for (y = 0; y < output_video_height; y++) {
                    for (x = 0; x < output_video_width; x++) {
                        ps_dec->ps_out_buffer->pu1_bufs[i][y * output_video_width + x] = x + y + frameNumb * 3;
                    }
                }
            }else{
                for (y = 0; y < output_video_height/2; y++) {
                    for (x = 0; x < output_video_width/2; x++) {
                        ps_dec->ps_out_buffer->pu1_bufs[i][y * output_video_width/2 + x] = 128 + y + frameNumb * 2;
                        ps_dec->ps_out_buffer->pu1_bufs[i][y * output_video_width/2 + x] = 64 + x + frameNumb * 5;
                    }
                }
            }
        }

        ps_dec_op->u4_ts = ps_dec->u4_ts;
        ps_dec_op->u4_output_present = ps_dec->u4_output_present = 1;

        frameNumb++;
        if(frameNumb == 25){
            frameNumb = 0;
        }
#endif

    } while ((header_data_left == 1) || (frame_data_left == 1));

    ifwh264d_fill_output_struct_from_context(ps_dec, ps_dec_op);

    ni_log(NI_LOG_DEBUG,
           "ifwh264d_video_decode The num bytes consumed: %d, "
           "ps_dec_op->u4_output_present %d\n",
           ps_dec_op->u4_num_bytes_consumed, ps_dec_op->u4_output_present);
    return api_ret_value;
}

WORD32 ifwh264d_get_version(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_get_version");

    char version_string[MAXVERSION_STRLEN + 1];
    UWORD32 version_string_len;

    ivd_ctl_getversioninfo_ip_t *ps_ip;
    ivd_ctl_getversioninfo_op_t *ps_op;

    ps_ip = (ivd_ctl_getversioninfo_ip_t *)pv_api_ip;
    ps_op = (ivd_ctl_getversioninfo_op_t *)pv_api_op;

    ps_op->u4_error_code = IV_SUCCESS;

    VERSION(version_string, CODEC_NAME, CODEC_RELEASE_TYPE, CODEC_RELEASE_VER,
            CODEC_VENDOR);

    if ((WORD32)ps_ip->u4_version_buffer_size <= 0)
    {
        return (IV_FAIL);
    }

    version_string_len = strnlen(version_string, MAXVERSION_STRLEN) + 1;

    if (ps_ip->u4_version_buffer_size >=
        version_string_len)   //(WORD32)sizeof(sizeof(version_string)))
    {
        memcpy(ps_ip->pv_version_buffer, version_string, version_string_len);
        ps_op->u4_error_code = IV_SUCCESS;
    } else
    {
        return IV_FAIL;
    }

    return (IV_SUCCESS);
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name :  ifwh264d_fwcheck                                            */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Globals       : <Does it use any global variables?>                      */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_fwcheck(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_fwcheck");

    //ifwh264d_fwcheck_ip_t *ps_ip = (ifwh264d_fwcheck_ip_t *)pv_api_ip;
    ifwh264d_fwcheck_op_t *ps_op = (ifwh264d_fwcheck_op_t *)pv_api_op;

    ps_op->s_ivd_fwcheck_op_t.u4_error_code = 0;

    char value[PROPERTY_VALUE_MAX];
    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get(PROP_DECODER_HW_ID, value, 0);
    unsigned hw_id = atoi(value);
    ni_log(NI_LOG_DEBUG, "AVC decoder using %s, hw_id %d\n", PROP_DECODER_HW_ID,
           hw_id);
    if (hw_id > 32)
    {
        ni_log(NI_LOG_ERROR, "Error card hw_id %d!!!!!!!!!!!!!!!!!!!\n", hw_id);
        hw_id = 0;
    }

    if (ni_rsrc_check_hw_available(hw_id, NI_DEVICE_TYPE_DECODER) ==
        NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_DEBUG, "Find netint devices %d", hw_id);
        ps_op->s_ivd_fwcheck_op_t.u4_fw_status = 1;
    } else
    {
        ni_log(NI_LOG_ERROR, "Failed to find netint device %d", hw_id);
        ps_op->s_ivd_fwcheck_op_t.u4_fw_status = 0;
    }

    return IV_SUCCESS;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_set_flush_mode                                    */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Globals       : <Does it use any global variables?>                      */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_set_flush_mode(iv_obj_t *dec_hdl, void *pv_api_ip,
                               void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_set_flush_mode");

    dec_struct_t *ps_dec;
    ivd_ctl_flush_op_t *ps_ctl_op = (ivd_ctl_flush_op_t *)pv_api_op;
    ps_ctl_op->u4_error_code = 0;

    ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);
    /* ! */
    /* Signal flush frame control call */
    ps_dec->u1_flushfrm = 1;

    ni_device_session_flush(&ps_dec->xcodec_dec->dec_ctx,
                            NI_DEVICE_TYPE_DECODER);

    return IV_SUCCESS;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_set_params                                        */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_set_params(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_set_params");

    dec_struct_t *ps_dec;
    WORD32 ret = IV_SUCCESS;

    if ((NULL == dec_hdl) || (NULL == pv_api_ip) || (NULL == pv_api_op))
    {
        return IV_FAIL;
    }

    ivd_ctl_set_config_ip_t *ps_ctl_ip = (ivd_ctl_set_config_ip_t *)pv_api_ip;
    ivd_ctl_set_config_op_t *ps_ctl_op = (ivd_ctl_set_config_op_t *)pv_api_op;

    ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);
    if (NULL == ps_dec)
    {
        return IV_FAIL;
    }

    ps_ctl_op->u4_error_code = 0;

    ps_dec->i4_app_skip_mode = ps_ctl_ip->e_frm_skip_mode;

    /*Is it really supported test it when you so the corner testing using test app*/

    ni_log(NI_LOG_DEBUG,
           "ifwh264d:  ifwh264d_set_params 000 ps_ctl_ip->u4_disp_ht %d, "
           "ps_ctl_ip->u4_disp_wd %d",
           ps_ctl_ip->u4_disp_ht, ps_ctl_ip->u4_disp_wd);

    if (ps_ctl_ip->u4_disp_ht != 0)
    {
        ps_dec->u4_app_disp_height = ps_ctl_ip->u4_disp_ht;
    }

    if (ps_ctl_ip->u4_disp_wd != 0)
    {
        ps_dec->u4_app_disp_width = ps_ctl_ip->u4_disp_wd;
    } else
    {
        /*
         * Set the display width to zero. This will ensure that the wrong value we had stored (0xFFFFFFFF)
         * does not propogate.
         */
        ps_dec->u4_app_disp_width = 0;
        ps_ctl_op->u4_error_code |= (1 << IVD_UNSUPPORTEDPARAM);
        ps_ctl_op->u4_error_code |= ERROR_DISP_WIDTH_INVALID;
        ret = IV_FAIL;
    }

    ps_dec->xcodec_dec->input_video_width = ps_dec->u4_app_disp_width;
    ps_dec->xcodec_dec->input_video_height = ps_dec->u4_app_disp_height;

    ni_log(NI_LOG_DEBUG,
           "ifwh264d:  ifwh264d_set_params 001 ps_dec->u4_app_disp_width %d, "
           "ps_dec->u4_app_disp_height %d",
           ps_dec->u4_app_disp_width, ps_dec->u4_app_disp_height);

    if (ps_ctl_ip->e_vid_dec_mode == IVD_DECODE_FRAME)
        ps_dec->i4_decode_header = 0;
    else if (ps_ctl_ip->e_vid_dec_mode == IVD_DECODE_HEADER)
        ps_dec->i4_decode_header = 1;
    else
    {
        ps_ctl_op->u4_error_code = (1 << IVD_UNSUPPORTEDPARAM);
        ps_dec->i4_decode_header = 1;
        ret = IV_FAIL;
    }
    //ps_dec->e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;

    if ((ps_ctl_ip->e_frm_out_mode != IVD_DECODE_FRAME_OUT) &&
        (ps_ctl_ip->e_frm_out_mode != IVD_DISPLAY_FRAME_OUT))
    {
        ps_ctl_op->u4_error_code = (1 << IVD_UNSUPPORTEDPARAM);
        ret = IV_FAIL;
    }
    ps_dec->e_frm_out_mode = ps_ctl_ip->e_frm_out_mode;

    //dec_struct_t * ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);

    {
        ifwh264d_init_decoder(ps_dec);
    }

    return ret;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_set_default_params                                */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 08 2011   100421          Copied from set_params               */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_set_default_params(iv_obj_t *dec_hdl, void *pv_api_ip,
                                   void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_set_default_params");

    WORD32 ret = IV_SUCCESS;

    ivd_ctl_set_config_op_t *ps_ctl_op = (ivd_ctl_set_config_op_t *)pv_api_op;

    {
        ps_ctl_op->u4_error_code = 0;
    }

    return ret;
}
/*****************************************************************************/
/*                                                                           */
/*  Function Name :  ifwh264d_delete                                            */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Globals       : <Does it use any global variables?>                      */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_delete(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_delete");

    if (pIn_file)
        fclose(pIn_file);

    if (p_file)
        fclose(p_file);

    //ifwh264d_delete_ip_t *ps_ip = (ifwh264d_delete_ip_t *)pv_api_ip;
    ifwh264d_delete_op_t *ps_op = (ifwh264d_delete_op_t *)pv_api_op;
    dec_struct_t *ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);
    if (ps_dec != NULL)
    {
        if (ps_dec->u1_flushfrm)
        {
            ps_dec->u4_output_present = 0;
        }
        XcodecDecStruct *xcodec_dec = ps_dec->xcodec_dec;

        ni_device_session_close(&(xcodec_dec->dec_ctx), 1,
                                NI_DEVICE_TYPE_DECODER);

        ni_packet_buffer_free(&(xcodec_dec->in_pkt.data.packet));
        ni_decoder_frame_buffer_free(&(xcodec_dec->out_frame.data.frame));
    }

    ps_op->s_ivd_delete_op_t.u4_error_code = 0;
    ifwh264d_free_static_bufs(dec_hdl);
    return IV_SUCCESS;
}
/*****************************************************************************/
/*                                                                           */
/*  Function Name :  ifwh264d_reset                                            */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Globals       : <Does it use any global variables?>                      */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_reset(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_reset");

    ivd_ctl_reset_op_t *ps_ctl_op = (ivd_ctl_reset_op_t *)pv_api_op;
    ps_ctl_op->u4_error_code = 0;

    dec_struct_t *ps_dec = (dec_struct_t *)(dec_hdl->pv_codec_handle);
    ps_dec->u1_resetfrm = 1;

    return IV_SUCCESS;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name :  ifwh264d_ctl                                              */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
WORD32 ifwh264d_ctl(iv_obj_t *dec_hdl, void *pv_api_ip, void *pv_api_op)
{
    ivd_ctl_set_config_ip_t *ps_ctl_ip;
    ivd_ctl_set_config_op_t *ps_ctl_op;
    WORD32 ret = IV_SUCCESS;
    UWORD32 subcommand;

    ps_ctl_ip = (ivd_ctl_set_config_ip_t *)pv_api_ip;
    ps_ctl_op = (ivd_ctl_set_config_op_t *)pv_api_op;
    ps_ctl_op->u4_error_code = 0;
    subcommand = ps_ctl_ip->e_sub_cmd;

    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_ctl subcommand %d", subcommand);

    switch (subcommand)
    {
        case IVD_CMD_CTL_SETPARAMS:
            ret = ifwh264d_set_params(dec_hdl, (void *)pv_api_ip,
                                      (void *)pv_api_op);
            break;
        case IVD_CMD_CTL_RESET:
            ret = ifwh264d_reset(dec_hdl, (void *)pv_api_ip, (void *)pv_api_op);
            break;

        case IVD_CMD_CTL_FLUSH:
            ret = ifwh264d_set_flush_mode(dec_hdl, (void *)pv_api_ip,
                                          (void *)pv_api_op);
            break;
        case IVD_CMD_CTL_GETVERSION:
            ret = ifwh264d_get_version(dec_hdl, (void *)pv_api_ip,
                                       (void *)pv_api_op);
            break;
        case IH264D_CMD_CTL_GET_VUI_PARAMS:
            ret = ifwh264d_get_vui_params(dec_hdl, (void *)pv_api_ip,
                                          (void *)pv_api_op);
            break;
        case IFWH264D_CMD_CTL_SET_NUM_CORES:
            ret = ifwh264d_set_num_cores(dec_hdl, (void *)pv_api_ip,
                                         (void *)pv_api_op);
            break;
        default:
            ni_log(NI_LOG_DEBUG, "\ndo nothing\n");
            break;
    }

    return ret;
}

WORD32 ifwh264d_set_num_cores(iv_obj_t *dec_hdl, void *pv_api_ip,
                              void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_set_num_cores");

    ifwh264d_ctl_set_num_cores_ip_t *ps_ip;
    ifwh264d_ctl_set_num_cores_op_t *ps_op;
    dec_struct_t *ps_dec = dec_hdl->pv_codec_handle;

    ps_ip = (ifwh264d_ctl_set_num_cores_ip_t *)pv_api_ip;
    ps_op = (ifwh264d_ctl_set_num_cores_op_t *)pv_api_op;
    ps_op->u4_error_code = 0;
    ps_dec->u4_num_cores = ps_ip->u4_num_cores;
    if (ps_dec->u4_num_cores == 1)
    {
        ps_dec->u1_separate_parse = 0;
    } else
    {
        ps_dec->u1_separate_parse = 1;
    }

    /*using only upto three threads currently*/
    if (ps_dec->u4_num_cores > 3)
        ps_dec->u4_num_cores = 3;

    return IV_SUCCESS;
}

WORD32 ifwh264d_get_vui_params(iv_obj_t *dec_hdl, void *pv_api_ip,
                               void *pv_api_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_get_vui_params");

    ih264d_ctl_get_vui_params_ip_t *ps_ip;
    ih264d_ctl_get_vui_params_op_t *ps_op;
    //dec_struct_t *ps_dec = dec_hdl->pv_codec_handle;
    //dec_seq_params_t *ps_sps;
    //vui_t *ps_vui;
    UWORD32 u4_size;

    ps_ip = (ih264d_ctl_get_vui_params_ip_t *)pv_api_ip;
    ps_op = (ih264d_ctl_get_vui_params_op_t *)pv_api_op;
    UNUSED(ps_ip);

    u4_size = ps_op->u4_size;
    memset(ps_op, 0, sizeof(ih264d_ctl_get_vui_params_op_t));
    ps_op->u4_size = u4_size;

    ps_op->u1_aspect_ratio_idc = 1;
    ps_op->u2_sar_width = 1;
    ps_op->u2_sar_height = 1;
    ps_op->u1_overscan_appropriate_flag = 1;
    ps_op->u1_video_format = 1;
    ps_op->u1_video_full_range_flag = 1;
    ps_op->u1_colour_primaries = 1;
    ps_op->u1_tfr_chars = 1;
    ps_op->u1_matrix_coeffs = 1;
    ps_op->u1_cr_top_field = 1;
    ps_op->u1_cr_bottom_field = 1;
    ps_op->u4_num_units_in_tick = 1;
    ps_op->u4_time_scale = 1;
    ps_op->u1_fixed_frame_rate_flag = 1;
    ps_op->u1_nal_hrd_params_present = 1;
    ps_op->u1_vcl_hrd_params_present = 1;
    ps_op->u1_low_delay_hrd_flag = 1;
    ps_op->u1_pic_struct_present_flag = 1;
    ps_op->u1_bitstream_restriction_flag = 1;
    ps_op->u1_mv_over_pic_boundaries_flag = 1;
    ps_op->u4_max_bytes_per_pic_denom = 1;
    ps_op->u4_max_bits_per_mb_denom = 1;
    ps_op->u4_log2_max_mv_length_horz = 1;
    ps_op->u4_log2_max_mv_length_vert = 1;
    ps_op->u4_num_reorder_frames = 1;
    ps_op->u4_max_dec_frame_buffering = 1;

    ni_log(NI_LOG_DEBUG,
           "ifwh264d_get_vui_params = ps_op->u1_colour_primaries %d\n",
           ps_op->u1_colour_primaries);
    ni_log(NI_LOG_DEBUG, "ifwh264d_get_vui_params = ps_op->u1_tfr_chars %d\n",
           ps_op->u1_tfr_chars);
    ni_log(NI_LOG_DEBUG,
           "ifwh264d_get_vui_params = ps_op->u1_matrix_coeffs %d\n",
           ps_op->u1_matrix_coeffs);
    ni_log(NI_LOG_DEBUG,
           "ifwh264d_get_vui_params = ps_op->u1_video_full_range_flag %d\n",
           ps_op->u1_video_full_range_flag);

    return IV_SUCCESS;
}

void ifwh264d_fill_output_struct_from_context(dec_struct_t *ps_dec,
                                              ivd_video_decode_op_t *ps_dec_op)
{
    ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_fill_output_struct_from_context");

    if ((ps_dec_op->u4_error_code & 0xff) !=
        ERROR_DYNAMIC_RESOLUTION_NOT_SUPPORTED)
    {
        ps_dec_op->u4_pic_wd = (UWORD32)ps_dec->u2_disp_width;
        ps_dec_op->u4_pic_ht = (UWORD32)ps_dec->u2_disp_height;
    }

    ps_dec_op->u4_new_seq = 0;
    ps_dec_op->u4_output_present = ps_dec->u4_output_present;
    ps_dec_op->u4_progressive_frame_flag =
        ps_dec->s_disp_op.u4_progressive_frame_flag;

    ps_dec_op->u4_is_ref_flag = 1;

    ps_dec_op->e_output_format = ps_dec->s_disp_op.e_output_format;
    ps_dec_op->s_disp_frm_buf = ps_dec->s_disp_op.s_disp_frm_buf;
    ps_dec_op->e4_fld_type = ps_dec->s_disp_op.e4_fld_type;
    ps_dec_op->u4_ts = ps_dec->s_disp_op.u4_ts;
    ps_dec_op->u4_disp_buf_id = ps_dec->s_disp_op.u4_disp_buf_id;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ifwh264d_api_function                                      */
/*                                                                           */
/*  Description   :                                                          */
/*                                                                           */
/*  Inputs        :iv_obj_t decoder handle                                   */
/*                :pv_api_ip pointer to input structure                      */
/*                :pv_api_op pointer to output structure                     */
/*  Outputs       :                                                          */
/*  Returns       : void                                                     */
/*                                                                           */
/*  Issues        : none                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         22 10 2008    100356         Draft                                */
/*                                                                           */
/*****************************************************************************/
IV_API_CALL_STATUS_T ifwh264d_api_function(iv_obj_t *dec_hdl, void *pv_api_ip,
                                           void *pv_api_op)
{
    UWORD32 command;
    UWORD32 *pu2_ptr_cmd;
    UWORD32 u4_api_ret;
    IV_API_CALL_STATUS_T e_status;
    e_status = api_check_struct_sanity(dec_hdl, pv_api_ip, pv_api_op);

    if (e_status != IV_SUCCESS)
    {
        UWORD32 *ptr_err;

        ptr_err = (UWORD32 *)pv_api_op;
        ni_log(NI_LOG_DEBUG, "error code = %d\n", *(ptr_err + 1));
        return IV_FAIL;
    }

    pu2_ptr_cmd = (UWORD32 *)pv_api_ip;
    pu2_ptr_cmd++;

    command = *pu2_ptr_cmd;
    //ni_log(NI_LOG_DEBUG, "ifwh264d:  ifwh264d_api_function command: %d\n",command);

    switch (command)
    {
        case IVD_CMD_CREATE:
            u4_api_ret =
                ifwh264d_create(dec_hdl, (void *)pv_api_ip, (void *)pv_api_op);
            ni_log(NI_LOG_DEBUG, "ifwh264d_create u4_api_ret = %d\n",
                   u4_api_ret);
            break;

        case IVD_CMD_FWCHECK:
            u4_api_ret =
                ifwh264d_fwcheck(dec_hdl, (void *)pv_api_ip, (void *)pv_api_op);
            ni_log(NI_LOG_DEBUG, "ifwh264d_fwcheck u4_api_ret = %d\n",
                   u4_api_ret);
            break;

        case IVD_CMD_DELETE:
            u4_api_ret =
                ifwh264d_delete(dec_hdl, (void *)pv_api_ip, (void *)pv_api_op);
            ni_log(NI_LOG_DEBUG, "ifwh264d_delete u4_api_ret = %d\n",
                   u4_api_ret);
            break;

        case IVD_CMD_VIDEO_DECODE:
            u4_api_ret = ifwh264d_video_decode(dec_hdl, (void *)pv_api_ip,
                                               (void *)pv_api_op);
            break;

        case IVD_CMD_VIDEO_CTL:
            u4_api_ret =
                ifwh264d_ctl(dec_hdl, (void *)pv_api_ip, (void *)pv_api_op);
            break;
        default:
            u4_api_ret = IV_FAIL;
            break;
    }

    return u4_api_ret;
}
