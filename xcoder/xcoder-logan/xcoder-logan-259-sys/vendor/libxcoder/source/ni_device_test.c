/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_test.c
*
*  \brief  Example code on how to programmatically work with NI T-408 using
*          libxcoder API
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#elif __linux__
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <semaphore.h>
#include <time.h>
#endif

#include <fcntl.h>
#include <errno.h>
#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_util.h"
#include "ni_device_test.h"


volatile int send_fin_flag = 0, recieve_fin_flag = 0, err_flag = 0;
volatile uint32_t number_of_frames = 0;
volatile uint32_t number_of_packets = 0;
struct timeval start_time, previous_time, current_time;
time_t start_timestamp = 0, privious_timestamp = 0, current_timestamp = 0;

#ifdef __linux__
sem_t nvme_mutex;
#endif

// 1.8 GB file cache
#define MAX_CACHE_FILE_SIZE 0x70000000
static uint8_t g_file_cache[MAX_CACHE_FILE_SIZE];
uint8_t *g_curr_cache_pos = g_file_cache;
volatile unsigned long total_file_size = 0;
volatile uint32_t data_left_size = 0;

// return actual bytes copied from cache
int read_next_chunk(uint8_t *p_dst, uint32_t to_read)
{
  int to_copy = to_read;

  if (data_left_size == 0) 
  {
    return 0;
  } 
  else if (data_left_size < to_read) 
  {
    to_copy = data_left_size;
  }

  memcpy(p_dst, g_curr_cache_pos, to_copy);
  g_curr_cache_pos += to_copy;
  data_left_size -= to_copy;

  return to_copy;
}

/*!******************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
ni_retcode_t decoder_send_data
(
  ni_session_context_t* p_dec_ctx,
  ni_session_data_io_t* p_in_data,
  int sos_flag,
  int input_video_width,
  int input_video_height,
  int pkt_size,
  int file_size,
  unsigned long *total_bytes_sent,
  int print_time,
  device_state_t *p_device_state
)
{
  static uint8_t tmp_buf[NI_MAX_PACKET_SZ - 32] = { 0 };
  int packet_size = pkt_size;
  int chunk_size = 0;
  int tx_size = 0;
  int send_size = 0;
  int new_packet = 0;
  ni_packet_t * p_in_pkt =  &(p_in_data->data.packet);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_TRACE, "===> decoder_send_data <===\n");

  if (p_device_state->dec_eos_sent)
  {
    ni_log(NI_LOG_TRACE, "decoder_send_data: ALL data (incl. eos) sent already !\n");
    LRETURN;
  }

  if (0 == p_in_pkt->data_len)
  {
    memset(p_in_pkt, 0, sizeof(ni_packet_t));

    chunk_size = read_next_chunk(tmp_buf, packet_size);

    p_in_pkt->p_data = NULL;
    p_in_pkt->data_len = chunk_size;

    if (chunk_size + p_dec_ctx->prev_size > 0)
    {
      ni_packet_buffer_alloc(p_in_pkt, chunk_size + p_dec_ctx->prev_size);
    }

    new_packet = 1;
    send_size = chunk_size + p_dec_ctx->prev_size;
  }
  else
  {
    send_size = p_in_pkt->data_len;
  }

  p_in_pkt->start_of_stream = sos_flag;
  p_in_pkt->end_of_stream = 0;
  p_in_pkt->video_width = input_video_width;
  p_in_pkt->video_height = input_video_height;

  if (send_size == 0)
  {
    if (new_packet)
    {
      send_size = ni_packet_copy(p_in_pkt->p_data, tmp_buf, 0,
                                  p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
      // todo save offset
    }
    p_in_pkt->data_len = send_size;
    tx_size = ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

    p_in_pkt->end_of_stream = 1;
    printf("Sending p_last packet (size %d) + eos\n", p_in_pkt->data_len);
  }
  else
  {
    if (new_packet)
    {
      send_size = ni_packet_copy(p_in_pkt->p_data, tmp_buf, chunk_size,
                                      p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
      // todo: offset += send_size
      p_in_pkt->data_len = send_size;
    }
    p_in_pkt->data_len = send_size;
  }

  tx_size = ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

  if (tx_size < 0)
  {
    // Error
    fprintf(stderr, "Sending data error. rc:%d\n", tx_size);
    retval = NI_RETCODE_FAILURE;
    LRETURN;
  }
  else if (tx_size == 0)
  {
    ni_log(NI_LOG_TRACE, "0 bytes sent this time, sleep and re-try p_next time .\n");
    usleep(10000);
  }
  else if (tx_size < send_size)
  {
    if (print_time)
    {
      printf("Sent %d < %d , re-try p_next time ?\n", tx_size, send_size);
    }
  }

  *total_bytes_sent += tx_size;

  if (p_dec_ctx->ready_to_close)
  {
    p_device_state->dec_eos_sent = 1;
  }

  if (print_time)
  {
    printf("decoder_send_data: success, total sent: %ld\n", *total_bytes_sent);
  }

  if (tx_size > 0)
  {
    ni_log(NI_LOG_TRACE, "decoder_send_data: reset packet_buffer.\n");
    ni_packet_buffer_free(p_in_pkt);
  }

#if 0
bytes_sent += chunk_size;
printf("[W] %d percent %d bytes sent. rc:%d result:%d\n", bytes_sent*100/file_size, chunk_size, rc, result);
sos_flag = 0;
if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == rc)
{
  printf("Buffer Full.\n");
}
else if (rc != 0)
{
  // Error
  fprintf(stderr, "Sending data error. rc:%d result:%d.\n", rc, result);
  err_flag = 1;
  return 2;
}
#endif

  retval = NI_RETCODE_SUCCESS;

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
int decoder_receive_data
(
  ni_session_context_t* p_dec_ctx,
  ni_session_data_io_t* p_out_data,
  int output_video_width,
  int output_video_height,
  FILE* p_file,
  unsigned long long *total_bytes_recieved,
  int print_time,
  int write_to_file,
  device_state_t *p_device_state
)
{

  int rc = NI_RETCODE_FAILURE;
  int end_flag = 0;
  int rx_size = 0;
  ni_frame_t * p_out_frame =  &(p_out_data->data.frame);

  ni_log(NI_LOG_TRACE, "===> decoder_receive_data <===\n");

  if (p_device_state->dec_eos_received)
  {
    ni_log(NI_LOG_TRACE, "decoder_receive_data: eos received already, Done !\n");
    LRETURN;
  }


  rc = ni_frame_buffer_alloc(p_out_frame, output_video_width,
                          output_video_height,
                          p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
                          1); // default to AV_PIX_FMT_YUV420P 8bit
  if (NI_RETCODE_SUCCESS != rc)
  {
      LRETURN;
  }

  rx_size = ni_device_session_read(p_dec_ctx, p_out_data, NI_DEVICE_TYPE_DECODER);

  end_flag = p_out_frame->end_of_stream;

  if (rx_size < 0)
  {
    // Error
    fprintf(stderr, "Receiving data error. rc:%d\n", rx_size);
    rc =  NI_RETCODE_FAILURE;
    LRETURN;
  }
  else if (rx_size > 0)
  {
    number_of_frames++;
    ni_log(NI_LOG_TRACE, "Got frame # %"PRIu64" bytes %d\n", p_dec_ctx->frame_num,
                   rx_size);
  }

  if(rx_size > 0 && p_file && write_to_file)
  {
    if (fwrite(p_out_frame->p_data[0], rx_size, 1, p_file) != 1)
    {
      fprintf(stderr, "Writing data %d bytes error !\n", rx_size);
      fprintf(stderr, "ferror rc = %d\n", ferror(p_file));
    }
  }

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
    printf("decoder_receive_data: writing data size=%d, %d\n", p_out_frame->data_len[i], i);
    fwrite(p_out_frame->p_data[i], p_out_frame->data_len[i], 1, p_file);
    *total_bytes_recieved += p_out_frame->data_len[i];
  }
}
#endif
  if (print_time)
  {
    printf("[R] Got:%d  Frames= %ul  fps=%lu  Total bytes %llu\n",
        rx_size, number_of_frames, (unsigned long) (number_of_frames/(current_time.tv_sec - start_time.tv_sec)), (unsigned long long) *total_bytes_recieved);
  }

  if (end_flag)
  {
    printf("Receiving done.\n");
    p_device_state->dec_eos_received = 1;
    rc = 1;
  }
  else if (0 == rx_size)
  {
    rc = 2;
  }

  ni_log(NI_LOG_TRACE, "decoder_receive_data: success\n");

  END;

  return rc;
}

/*!******************************************************************************
 *  \brief  Send encoder input data
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
const long MAX_YUV_FRAME_SIZE = 49766400;
// NI_MAX_RESOLUTION_WIDTH * NI_MAX_RESOLUTION_HEIGHT * 3 / 2;

int encoder_send_data
(
  ni_session_context_t* p_enc_ctx,
  ni_session_data_io_t* p_in_data,
  int sos_flag,
  int input_video_width,
  int input_video_height,
  int pfs,
  int file_size,
  unsigned long *bytes_sent,
  int aligned_height,
  device_state_t *p_device_state)
{
  static uint8_t tmp_buf[49766400];
  volatile static int started = 0;
  volatile static int need_to_resend = 0;
  int frame_size = input_video_width * input_video_height * 3 / 2;
  int chunk_size;
  int oneSent;
  int i;
  int height_aligned[4];
  ni_frame_t * p_in_frame =  &(p_in_data->data.frame);
  

  height_aligned[0] = aligned_height;
  height_aligned[1] = height_aligned[2] = height_aligned[0] / 2;

  ni_log(NI_LOG_TRACE, "===> encoder_send_data <===\n");

  if (p_device_state->enc_eos_sent == 1)
  {
    ni_log(NI_LOG_TRACE, "encoder_send_data: ALL data (incl. eos) sent already !\n");
    return 0;
  }


  if (need_to_resend)
    goto send_pkt;

  chunk_size = read_next_chunk(tmp_buf, frame_size);

  p_in_frame->start_of_stream = 0;
  if (! started)
  {
    started = 1;
    p_in_frame->start_of_stream = 1;
  }
  p_in_frame->end_of_stream = 0;
  p_in_frame->force_key_frame = 0;
  if (chunk_size == 0)
  {
    p_in_frame->end_of_stream = 1;
    // just flush and return
    // p_enc_ctx->ready_to_close = 1;
    // ni_decoder_session_flush(p_enc_ctx);
    // return 0;
  }
  p_in_frame->video_width = input_video_width;
  p_in_frame->video_height = input_video_height;

  // only metadata header for now
  p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

  int dst_stride[4];
  dst_stride[0] = ((input_video_width + 31) / 32) * 32;
  dst_stride[1] = dst_stride[2] = dst_stride[0] / 2;
  ni_frame_buffer_alloc_v3(p_in_frame, input_video_width, input_video_height,
                           dst_stride, 
                           p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264,
                           p_in_frame->extra_data_len);
  if (! p_in_frame->p_data[0])
  {
    fprintf(stderr, "Error: allocate YUV frame buffer !");
    return -1;
  }
  int nb_planes = 3;
  int plane_height[4];
  plane_height[0] = input_video_height;
  plane_height[1] = plane_height[2] = input_video_height / 2;
  plane_height[3] = 0;

  ni_log(NI_LOG_TRACE, "p_dst alloc linesize = %d/%d/%d  height = %d/%d/%d\n",
             dst_stride[0], dst_stride[1], dst_stride[2],
             plane_height[0], plane_height[1], plane_height[2]);
  
  uint8_t *src = tmp_buf;
  for (i = 0; i < nb_planes; i++)
  {
    int height = plane_height[i];
    uint8_t *p_dst = p_in_frame->p_data[i];
    memcpy(p_dst, src, dst_stride[i] * height);
    ni_log(NI_LOG_TRACE, "copied plane %d for %d x %d bytes\n", i, dst_stride[i], height);
    p_dst += dst_stride[i] * height;
    src += dst_stride[i] * height;

    // height padding if needed
    int padding_height = height_aligned[i] - plane_height[i];
    if (padding_height > 0) {
      ni_log(NI_LOG_TRACE, "copying plane %d for padding height adjustment: %d\n",
                 i, padding_height);
      // copy the p_last stride as the padding
      src -= dst_stride[i];
      for (; padding_height > 0; padding_height--) {
        memcpy(p_dst, src, dst_stride[i]);
        p_dst += dst_stride[i];
      }
      // re-position
      src += dst_stride[i];      
    }
  }

send_pkt:  oneSent = ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
  if (oneSent < 0)
  {
    fprintf(stderr, "Error: ni_encoder_session_write\n");
    need_to_resend = 1;
    return -1;
  }
  else if (oneSent == 0)
  {
    need_to_resend = 1;
  }
  else
  {
    need_to_resend = 0;

    *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] + p_in_frame->data_len[2];
    ni_log(NI_LOG_TRACE, "encoder_send_data: total sent data size=%lu\n", *bytes_sent);

    ni_log(NI_LOG_TRACE, "encoder_send_data: success\n");

    if (p_enc_ctx->ready_to_close)
    {
        p_device_state->enc_eos_sent = 1;
    }

  }

  return 0;
}

/*******************************************************************************
 *  @brief  Send encoder input data, directly after receiving from decoder
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
int encoder_send_data2(ni_session_context_t* p_enc_ctx, ni_session_data_io_t* p_in_data, int sos_flag, int input_video_width, int input_video_height, int pfs, int file_size, unsigned long *bytes_sent, int aligned_height, device_state_t *p_device_state)
{
  static uint8_t tmp_buf[49766400];
  volatile static int started = 0;
  volatile static int need_to_resend_2 = 0;
  int frame_size = input_video_width * input_video_height * 3 / 2;
  int chunk_size;
  int oneSent;
  int i;
  int height_aligned[4];
  ni_frame_t * p_in_frame =  &(p_in_data->data.frame);

  height_aligned[0] = aligned_height;
  height_aligned[1] = height_aligned[2] = height_aligned[0] / 2;

  ni_log(NI_LOG_TRACE, "===> encoder_send_data2 <===\n");

  if (p_device_state->enc_eos_sent == 1)
  {
    ni_log(NI_LOG_TRACE, "encoder_send_data2: ALL data (incl. eos) sent already !\n");
    return 1;
  }

  if (need_to_resend_2)
    goto send_pkt;

//  chunk_size = read_next_chunk(tmp_buf, frame_size);

  p_in_frame->start_of_stream = 0;
  if (! started) {
    started = 1;
    p_in_frame->start_of_stream = 1;
  }
  // p_in_frame->end_of_stream = 0;
  p_in_frame->force_key_frame = 0;

  p_in_frame->sei_total_len 
  = p_in_frame->sei_cc_offset = p_in_frame->sei_cc_len
  = p_in_frame->sei_hdr_mastering_display_color_vol_offset
  = p_in_frame->sei_hdr_mastering_display_color_vol_len
  = p_in_frame->sei_hdr_content_light_level_info_offset
  = p_in_frame->sei_hdr_content_light_level_info_len
  = p_in_frame->sei_hdr_plus_offset
  = p_in_frame->sei_hdr_plus_len = 0;

  p_in_frame->roi_len = 0;
  p_in_frame->reconf_len = 0;
  p_in_frame->force_pic_qp = 0;
  p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
  p_in_frame->ni_pict_type = 0;

#if 0
  if (chunk_size == 0)
  {
    p_in_frame->end_of_stream = 1;
    // just flush and return
    // p_enc_ctx->ready_to_close = 1;
    // xcoder_dec_flush(p_enc_ctx);
    // return 0;
  }
  p_in_frame->video_width = input_video_width;
  p_in_frame->video_height = input_video_height;

  int dst_stride[4];
  dst_stride[0] = ((input_video_width + 31) / 32) * 32;
  dst_stride[1] = dst_stride[2] = dst_stride[0] / 2;
  ni_frame_buffer_alloc(p_in_frame, input_video_width, input_video_height,
                             dst_stride, 
                             p_enc_ctx->codec_format == XCODER_CODEC_FORMAT_H264);
  if (! p_in_frame->p_data[0])
  {
    fprintf(stderr, "Error: allocate YUV frame buffer !");
    return -1;
  }
  int nb_planes = 3;
  int plane_height[4];
  plane_height[0] = input_video_height;
  plane_height[1] = plane_height[2] = input_video_height / 2;
  plane_height[3] = 0;

  ni_log(NI_LOG_TRACE, "p_dst alloc linesize = %d/%d/%d  height = %d/%d/%d\n",
             dst_stride[0], dst_stride[1], dst_stride[2],
             plane_height[0], plane_height[1], plane_height[2]);
  
  uint8_t *src = tmp_buf;
  for (i = 0; i < nb_planes; i++)
  {
    int height = plane_height[i];
    uint8_t *p_dst = p_in_frame->p_data[i];
    memcpy(p_dst, src, dst_stride[i] * height);
    ni_log(NI_LOG_TRACE, "copied plane %d for %d x %d bytes\n", i, dst_stride[i], height);
    p_dst += dst_stride[i] * height;
    src += dst_stride[i] * height;

    // height padding if needed
    int padding_height = height_aligned[i] - plane_height[i];
    if (padding_height > 0)
    {
      ni_log(NI_LOG_TRACE, "copying plane %d for padding height adjustment: %d\n",
                 i, padding_height);
      // copy the last stride as the padding
      src -= dst_stride[i];
      for (; padding_height > 0; padding_height--)
      {
        memcpy(p_dst, src, dst_stride[i]);
        p_dst += dst_stride[i];
      }
      // re-position
      src += dst_stride[i];      
    }
  }
#endif
send_pkt:  
  oneSent = p_in_frame->data_len[0] + p_in_frame->data_len[1] + 
  p_in_frame->data_len[2];

  if (oneSent > 0 || p_in_frame->end_of_stream)
  {
    // fill in data_len[0] for eos frame so it won't be mistaken by
    // xcoder_enc_send as sequence change
    // ToDo: check xcoder_enc_send for accuracy of that sc checking based
    //       on data_len[0] !
    if (p_in_frame->end_of_stream)
      p_in_frame->data_len[0] = 
      aligned_height * ((input_video_width + 15) / 16) * 16;
    else
    {
      int luma_size = input_video_width * aligned_height;
      int chroma_b_size = luma_size / 4;
      int chroma_r_size = chroma_b_size;
      int buffer_size = luma_size + chroma_b_size + chroma_r_size + 
      p_in_frame->extra_data_len;
      buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT + NI_MEM_PAGE_ALIGNMENT;
      
      p_in_frame->buffer_size = buffer_size;
    }

    oneSent = ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
    p_in_frame->end_of_stream = 0;
  }
  else
  {
    goto end_encoder_send_data2;
  }

  if (oneSent < 0) {
    fprintf(stderr, "Error: xcoder_enc_send\n");
    need_to_resend_2 = 1;
    return -1;
  }
  else if (oneSent == 0)
  {
    if (p_device_state->enc_eos_sent == 0 && p_enc_ctx->ready_to_close) {
      need_to_resend_2 = 0;
      p_device_state->enc_eos_sent = 1;
    }
    else
      need_to_resend_2 = 1;
  }
  else
  {
    need_to_resend_2 = 0;

    if (p_enc_ctx->ready_to_close)
    {
      p_device_state->enc_eos_sent = 1;
    }
#if 0
    *bytes_sent += p_in_frame->data_len[0] + p_in_frame->data_len[1] + p_in_frame->data_len[2];
    ni_log(NI_LOG_TRACE, "encoder_send_data2: total sent data size=%u\n", *bytes_sent);
#endif
    ni_log(NI_LOG_TRACE, "encoder_send_data2: success\n");
  }

end_encoder_send_data2:  return 0;
}


/*!******************************************************************************
 *  \brief  Receive output data from encoder
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int encoder_receive_data(ni_session_context_t* p_enc_ctx, ni_session_data_io_t* p_out_data, int output_video_width, int output_video_height, FILE* p_file, unsigned long long *total_bytes_recieved, int print_time)
{
  int packet_size = NI_MAX_TX_SZ;
  int rc = 0;
  int end_flag = 0;
  int rx_size = 0;
  ni_packet_t * p_out_pkt =  &(p_out_data->data.packet);
  int meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE;
  //int i;

  ni_log(NI_LOG_TRACE, "===> encoder_receive_data <===\n");

  ni_packet_buffer_alloc(p_out_pkt, packet_size);

  rc = ni_device_session_read(p_enc_ctx, p_out_data, NI_DEVICE_TYPE_ENCODER);

  end_flag = p_out_pkt->end_of_stream;
  rx_size = rc;

#if 0
  if (rc != 0)
  {
    // Error
    fprintf(stderr, "Receiving data error. rc:%d result:%d.\n", rc, result);
    return 2;
  }
#endif

  ni_log(NI_LOG_TRACE, "encoder_receive_data: received data size=%d\n", rx_size);

  if(rx_size > meta_size)
  {
    if (0 == p_enc_ctx->pkt_num)
    {
      p_enc_ctx->pkt_num = 1;
    }
    number_of_packets++;

    if (p_file && (fwrite((uint8_t*)p_out_pkt->p_data + meta_size, 
                          p_out_pkt->data_len - meta_size, 1, p_file) != 1)) {
      fprintf(stderr, "Writing data %d bytes error !\n", p_out_pkt->data_len - meta_size);
      fprintf(stderr, "ferror rc = %d\n", ferror(p_file));
    }

    *total_bytes_recieved += rx_size - meta_size;
    ni_log(NI_LOG_TRACE, "Got:   Packets= %u\n", number_of_packets);
  }
  else if (rx_size != 0)
  {
    fprintf(stderr, "Error: received %d bytes, <= metadata size %d!\n",
           rx_size, meta_size);
  }

  if (print_time) {
    int timeDiff = current_time.tv_sec - start_time.tv_sec;
    if (timeDiff == 0)
      timeDiff = 1;
    printf("[R] Got:%d   Packets= %u fps=%d  Total bytes %lld\n", 
           rx_size, number_of_packets, number_of_packets/timeDiff, *total_bytes_recieved);
  }

  if (end_flag)
  {
    printf("Receiving done.\n");
    return 1;
  }

  ni_log(NI_LOG_TRACE, "encoder_receive_data: success\n");

  return 0;
}


void print_usage(void)
{
  printf("Usage: xcoder xcoderGUID inputFileName outputFileName width height mode('decode'/'encode'/'xcode') codec_fmt(0/1: H.264/HEVC  X-Y: transcoding from 'X' to 'Y') pkt_size (<= 131040 for decoder, dummy for encoder) [xcoder-params]\n\n"
         "xcoderGUID: GUID of xcoder. First is 0, second is 1, and so on.\n"
         "inputFileName: name of source file, in H.264/HEVC/YUV format.\n"
         "outputFileName: name of output file, in H.264/HEVC/YUV format. 'null' generates NO output file.\n"
         "width height: source/output file resolution.\n"
         "mode: decoding, encoding or transcoding.\n"
         "codec_fmt: AVC or HEVC.\n"
         "pkt_size: size of bitstream data chunks sent for decoding; dummy for encoding.\n"
         "xcoder-params (optional): ':' separated list of key=value parameters for XCoder encoding configuration; space/quote is not allowed.\n");
}


// retrieve key and value from 'key=value' pair, return 0 if successful
// otherwise non-0
static int get_key_value(char *p_str, char *key, char *value)
{
  if (! p_str || ! key || ! value)
    return 1;

  char *p = strchr(p_str, '=');
  if (! p)
    return 1;
  else {
    *p = '\0';
    key[0] = '\0';
    value[0] = '\0';
    strncat(key, p_str, strlen(p_str));
    strncat(value, p+1, strlen(p+1));
    return 0;
  }
}

/*!******************************************************************************
 *  \brief  main 
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int main(int argc, char *argv[])
{
  tx_data_t sdPara;
  rx_data_t rcPara;
  device_state_t xcodeState = {0};
  int err, pfs, sos_flag, edFlag, bytes_sent, rc, chunk_size;
  char xcoderGUID[32];  // will be reused to stored /dev/nvmeX name !
  char xcoderNSID[32];  // will be reused to stored /dev/nvmeXnY name !
  int iXcoderGUID = -1;
  uint32_t result=0, end_flag, rx_size;
  unsigned long total_bytes_sent;
  unsigned long long total_bytes_recieved;
  unsigned long long xcodeRecvTotal;
  FILE *p_file = NULL;
  size_t wtsize;
  long long int virAddr, phyAddr;
  char mode_c[128];
  int input_video_width;
  int input_video_height;
  int arg_width;
  int arg_height;
  int mode = 0;
  int i;
  int pkt_size;
  ni_encoder_params_t  api_param;
  char encConfXcoderParams[2048] = { 0 };
  ni_device_handle_t dev_handle = NI_INVALID_DEVICE_HANDLE, dev_handle_1 = NI_INVALID_DEVICE_HANDLE;
  int src_codec_format = 0, dst_codec_format = 0;

  if (argc < 9 || argc > 10)
  {
    print_usage();    
    return 1;
  }
  xcoderGUID[0] = '\0';
  strncat(xcoderGUID, argv[1], strlen(argv[1]));
  iXcoderGUID = atoi(xcoderGUID);

  sdPara.fileName[0] = '\0';
  rcPara.fileName[0] = '\0';
  strncat(sdPara.fileName, argv[2], strlen(argv[2]));
  strncat(rcPara.fileName, argv[3], strlen(argv[3]));

  arg_width = atoi(argv[4]);
  arg_height = atoi(argv[5]);

  sdPara.arg_width = arg_width;
  sdPara.arg_height = arg_height;
  rcPara.arg_width = arg_width;
  rcPara.arg_height = arg_height;

  mode_c[0] = '\0';
  strncat(mode_c, argv[6], 6);
  mode_c[6] = '\0';
  if(!strcmp(mode_c, "decode"))
  {
    mode = XCODER_APP_DECODE;
  src_codec_format = atoi(argv[7]);
  }
  else if(!strcmp(mode_c, "encode"))
  {
    mode = XCODER_APP_ENCODE;
  dst_codec_format = atoi(argv[7]);

  }
  else  // transcode
  {
    mode = XCODER_APP_TRANSCODE;
    // transcoding: 'src_fmt'-'dst_fmt'
    if (strlen(argv[7]) != 3 || argv[7][1] != '-') 
  {
      print_usage();
      return -1;
    }
    src_codec_format = argv[7][0] - '0';
    dst_codec_format = argv[7][2] - '0';
  }

  if (src_codec_format != NI_CODEC_FORMAT_H264 &&
      src_codec_format != NI_CODEC_FORMAT_H265 &&
      dst_codec_format != NI_CODEC_FORMAT_H264 &&
      dst_codec_format != NI_CODEC_FORMAT_H265) 
  {
    print_usage();
    return -1;

  }
  
  sdPara.mode = mode;
  rcPara.mode = mode;

  printf("mode=%d, argv[6]=%s, mode_c=%s\n", mode, argv[6], mode_c);
  printf("codec_format = %s\n", argv[7]);

  // set up encoder p_config TBD some hard coded numbers
  if (ni_encoder_init_default_params(&api_param, 25, 1, 200000, 
                           arg_width, arg_height) < 0)
  {
    fprintf(stderr, "Encoder p_config set up error\n");
    return -1;
  }

  pkt_size = atoi(argv[8]);

  if (argc == 10)
  {
    encConfXcoderParams[0] = '\0';
    strncat(encConfXcoderParams, argv[9], strlen(argv[9]));
    char key[16], value[64];
    char *p = encConfXcoderParams;
    char *curr = encConfXcoderParams, *colon_pos;
    while (*curr) {
      colon_pos = strchr(curr, ':');

      if (colon_pos)
        *colon_pos = '\0';

      if (strlen(curr) > sizeof(key) + sizeof(value) - 1 || 
          get_key_value(curr, key, value)) {
        fprintf(stderr, "Error: encoder xcoder-params p_config key/value not retrieved: "
               "%s\n", curr);
        return -1;
      }
      int parse_ret = ni_encoder_params_set_value(&api_param, key, value);
      switch (parse_ret) {
      case NI_RETCODE_PARAM_INVALID_NAME:
        fprintf(stderr, "Error: Unknown option: %s.\n", key);
        break;
      case NI_RETCODE_PARAM_INVALID_VALUE:
        fprintf(stderr, "Error: Invalid value for %s: %s.\n", key, value);
        break;
      default:
        break;
      }
      
      if (colon_pos)
        curr = colon_pos + 1;
      else
        curr += strlen(curr);
    }
    
  }

#ifdef _WIN32
  pfs = open(sdPara.fileName, O_RDONLY | O_BINARY);
#elif __linux__
  pfs = open(sdPara.fileName, O_RDONLY);
#endif

  if (pfs < 0)
  {
    fprintf(stderr, "ERROR: Cannot open %s\n", sdPara.fileName);
    fprintf(stderr, "Input file read failure");
    err_flag = 1;
    goto end;
  }
  printf("SUCCESS: Opened input file: %s with file id = %d\n", sdPara.fileName, pfs);

  lseek(pfs, 0, SEEK_END);
  total_file_size = lseek(pfs, 0, SEEK_CUR);
  lseek(pfs, 0, SEEK_SET);
  unsigned long tmpFileSize = total_file_size;
  if (total_file_size > MAX_CACHE_FILE_SIZE) {
    fprintf(stderr, "ERROR: input file size %lu exceeding max %ul, quit\n", 
           total_file_size, MAX_CACHE_FILE_SIZE);
    goto end;
  }

  printf("Reading %lu bytes in total ..\n", total_file_size);
  while (tmpFileSize)
  {
    int one_read_size = read(pfs, g_curr_cache_pos, 4096);
    if (one_read_size == -1) {
      fprintf(stderr, "ERROR %d: reading file, quit ! left-to-read %lu\n", 
             NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, tmpFileSize);
      fprintf(stderr, "Input file read error");
      goto end;
    } else {
      tmpFileSize -= one_read_size;
      g_curr_cache_pos += one_read_size;
      //      printf("%d bytes read from file, %d left to do, continue ..\n", 
      //       one_read_size, tmpFileSize);
    }
  }
  printf("read %lu bytes from input file into memory\n", total_file_size);
  
  g_curr_cache_pos = g_file_cache;
  data_left_size = total_file_size;

  if (strcmp(rcPara.fileName, "null"))
  {
    p_file = fopen(rcPara.fileName, "wb");
    if (p_file == NULL)
    {
      fprintf(stderr, "ERROR: Cannot open %s\n", rcPara.fileName);
      err_flag = 1;
      goto end;
    }
  }
  printf("SUCCESS: Opened output file: %s\n", rcPara.fileName);

  send_fin_flag = 0;
  recieve_fin_flag = 0;

  ni_session_context_t dec_ctx;
  ni_session_context_t enc_ctx;
  unsigned long load_pixel = 0;

  sdPara.p_dec_ctx = (void *) &dec_ctx;
  sdPara.p_enc_ctx = (void *) &enc_ctx;
  rcPara.p_dec_ctx = (void *) &dec_ctx;
  rcPara.p_enc_ctx = (void *) &enc_ctx;

  if(mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_DECODE)
  { 
    dec_ctx.p_session_config = NULL;
    dec_ctx.session_id = NI_INVALID_SESSION_ID;
    dec_ctx.codec_format = src_codec_format;
    sdPara.p_dec_rsrc_ctx = ni_rsrc_allocate_direct(NI_DEVICE_TYPE_DECODER, iXcoderGUID, 
                                           dec_ctx.codec_format,
                                           sdPara.arg_width,
                                           sdPara.arg_height, 25, &load_pixel);

    if (! sdPara.p_dec_rsrc_ctx)
    {
      fprintf(stderr, "Resource allocation failure !\n");
      return -1;
    }
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx;

    xcoderGUID[0] = '\0';
    xcoderNSID[0] = '\0';
    strncat(xcoderGUID, sdPara.p_dec_rsrc_ctx->p_device_info->dev_name, strlen(sdPara.p_dec_rsrc_ctx->p_device_info->dev_name));
    strncat(xcoderNSID, sdPara.p_dec_rsrc_ctx->p_device_info->blk_name, strlen(sdPara.p_dec_rsrc_ctx->p_device_info->blk_name));
#ifdef _WIN32
    printf("Trying to open device %s",sdPara.p_enc_rsrc_ctx->p_device_info->dev_name);
    dev_handle = ni_device_open(sdPara.p_enc_rsrc_ctx->p_device_info->dev_name, &dec_ctx.max_nvme_io_size);
    dev_handle_1 = dev_handle;
    if (NI_INVALID_DEVICE_HANDLE == dev_handle)
#elif __linux__
#ifdef XCODER_IO_RW_ENABLED
    //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
    //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice.
    if ( (NI_INVALID_DEVICE_HANDLE == (dev_handle = ni_device_open(xcoderNSID, &dec_ctx.max_nvme_io_size)))    ||
          (NI_INVALID_DEVICE_HANDLE == (dev_handle_1 = ni_device_open(xcoderNSID, &dec_ctx.max_nvme_io_size))) )
#else
    if ( (NI_INVALID_DEVICE_HANDLE == (dev_handle = ni_device_open(xcoderGUID, &dec_ctx.max_nvme_io_size)))    ||
          (NI_INVALID_DEVICE_HANDLE == (dev_handle_1 = ni_device_open(xcoderNSID, &dec_ctx.max_nvme_io_size))) )
#endif
#endif
    {
      fprintf(stderr, "ni_device_open failure !\n");
      return -1;
    }
    else
    {
      dec_ctx.device_handle = dec_ctx.device_handle = dev_handle;
      dec_ctx.blk_io_handle = dec_ctx.blk_io_handle = dev_handle_1;
      dec_ctx.hw_id = dec_ctx.hw_id = iXcoderGUID;
    }

#ifdef _WIN32
    dec_ctx.event_handle = ni_create_event();
    if (NI_INVALID_EVENT_HANDLE == dec_ctx.event_handle)
    {
      fprintf(stderr, "ni_create_event failure !\n");
      return -1;
    }
#endif

    // default: 8 bit, little endian
    dec_ctx.src_bit_depth = 8;
    dec_ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;
    dec_ctx.bit_depth_factor = 1;

    err = ni_device_session_open(&dec_ctx, NI_DEVICE_TYPE_DECODER);
    if (err < 0)
    {
      fprintf(stderr, "ni_decoder_session_open failure !\n");
      return -1;
    }
  }

  // for now, setup nothing but confWin offset, for height to be 16-aligned
  // for h.264 encoding !
  int height_aligned = ((arg_height + 7) / 8) * 8;

  if(mode == XCODER_APP_TRANSCODE || mode == XCODER_APP_ENCODE)
  {
    enc_ctx.p_session_config = NULL;
    enc_ctx.session_id = NI_INVALID_SESSION_ID;
    enc_ctx.codec_format = dst_codec_format;
    sdPara.p_enc_rsrc_ctx = ni_rsrc_allocate_direct(NI_DEVICE_TYPE_ENCODER, iXcoderGUID, 
                                           enc_ctx.codec_format,
                                           sdPara.arg_width,
                                           sdPara.arg_height, 25, &load_pixel);
    if (! sdPara.p_enc_rsrc_ctx)
    {
      fprintf(stderr, "Resource allocation failure !\n");
      return -1;
    }
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx;

    xcoderGUID[0] = '\0';
    xcoderNSID[0] = '\0';
    strncat(xcoderGUID, sdPara.p_enc_rsrc_ctx->p_device_info->dev_name, strlen(sdPara.p_enc_rsrc_ctx->p_device_info->dev_name));
    strncat(xcoderNSID, sdPara.p_enc_rsrc_ctx->p_device_info->blk_name, strlen(sdPara.p_enc_rsrc_ctx->p_device_info->blk_name));
#ifdef _WIN32
    dev_handle = ni_device_open(sdPara.p_enc_rsrc_ctx->p_device_info->dev_name, &enc_ctx.max_nvme_io_size);
    dev_handle_1 = dev_handle;
    if (NI_INVALID_DEVICE_HANDLE == dev_handle)
#elif __linux__
#ifdef XCODER_IO_RW_ENABLED
    //The original design (code below) is to open char and block device file separately. And the ffmpeg will close the device twice.
    //However, in I/O version, char device can't be opened. For compatibility, and to avoid errors, open the block device twice.
    if ( (NI_INVALID_DEVICE_HANDLE == (dev_handle = ni_device_open(xcoderNSID, &enc_ctx.max_nvme_io_size))) ||
        (NI_INVALID_DEVICE_HANDLE == (dev_handle_1 = ni_device_open(xcoderNSID, &enc_ctx.max_nvme_io_size))) )
#else
    if ((NI_INVALID_DEVICE_HANDLE == (dev_handle = ni_device_open(xcoderGUID, &enc_ctx.max_nvme_io_size))) ||
        (NI_INVALID_DEVICE_HANDLE == (dev_handle_1 = ni_device_open(xcoderNSID, &enc_ctx.max_nvme_io_size))) )
#endif
#endif
    {
      fprintf(stderr, "ni_device_open failure !\n");
      return -1;
    }
    else
    {
      enc_ctx.device_handle = enc_ctx.device_handle = dev_handle;
      enc_ctx.blk_io_handle = enc_ctx.blk_io_handle = dev_handle_1;
      enc_ctx.hw_id = enc_ctx.hw_id = iXcoderGUID;
    }
#ifdef _WIN32
    enc_ctx.event_handle = ni_create_event();
    if (NI_INVALID_EVENT_HANDLE == enc_ctx.event_handle)
    {
      fprintf(stderr, "ni_create_event failure !\n");
      return -1;
    }
#endif

    enc_ctx.p_session_config = &api_param;
    // default: 8 bit, little endian
    enc_ctx.src_bit_depth = 8;
    enc_ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;
    enc_ctx.bit_depth_factor = 1;

    if (enc_ctx.codec_format == NI_CODEC_FORMAT_H264)
    {
      height_aligned = ((arg_height + 15) / 16) * 16;
      if (height_aligned > arg_height)
      {
        api_param.hevc_enc_params.conf_win_bottom += height_aligned - arg_height;
        printf("H.264 YUV height 16-aligned adjustment, from %d to %d, "
               "conf_win_bottom = %d\n", arg_height, height_aligned,
               api_param.hevc_enc_params.conf_win_bottom);
      }
    }

    err = ni_device_session_open(&enc_ctx, NI_DEVICE_TYPE_ENCODER);
    if (err < 0)
    {
      fprintf(stderr, "ni_encoder_session_open failure !\n");
      return -1;
    }
  }

  if(mode == XCODER_APP_TRANSCODE)
  {
    //err = xcoder_codec_connect(&dec_ctx, &enc_ctx);
  }

  if (err < 0)
  {
    fprintf(stderr, "ERROR: open device error. %d Result:%d\n", err, result);
    return 0;
  }
  else
  {
    printf("Open device %s successful. Result (session id): %d\n", 
           xcoderGUID, err);
  }

  sos_flag = 1;
  edFlag = 0;
  bytes_sent = 0;
  total_bytes_recieved = 0;
  xcodeRecvTotal = 0;
  total_bytes_sent = 0;

  printf("user video resolution: %dx%d\n", arg_width, arg_height);
  if(arg_width == 0 || arg_height == 0)
  {
    input_video_width = 1280;
    input_video_height = 720;
  }
  else
  {
    input_video_width = arg_width;
    input_video_height = arg_height;
  }
  int output_video_width = input_video_width;
  int output_video_height = input_video_height;


  (void) gettimeofday(&start_time, NULL);
  (void) gettimeofday(&previous_time, NULL);
  (void) gettimeofday(&current_time, NULL);
  start_timestamp = privious_timestamp = current_timestamp = time(NULL);

#if 0
#ifdef __linux__
  struct timespec start, end;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
#endif
#endif

 

  if(mode == XCODER_APP_DECODE)
  {
    printf("Decoding Mode: %dx%d to %dx%d\n", input_video_width, input_video_height, output_video_width, output_video_height);
    ni_session_data_io_t in_pkt;
    ni_session_data_io_t out_frame;
    memset(&in_pkt, 0, sizeof(ni_session_data_io_t));
    memset(&out_frame, 0, sizeof(ni_session_data_io_t));
#if 0
    ni_frame_buffer_alloc(&(out_frame.data.frame), arg_width, arg_height, 
                              dec_ctx.codec_format == NI_CODEC_FORMAT_H264,
                              1);
#endif
    
    while (send_fin_flag == 0 || recieve_fin_flag == 0)
    {

      (void) gettimeofday(&current_time, NULL);
      int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

      // Sending
      send_fin_flag = decoder_send_data(&dec_ctx, &in_pkt, sos_flag, input_video_width, input_video_height, pkt_size, total_file_size, &total_bytes_sent, print_time,&xcodeState);
      sos_flag = 0;
      if(send_fin_flag < 0) //Error
      {
        fprintf(stderr, "Error: decoder_send_data failed, rc: %d\n", send_fin_flag);
        break;
      }
    
      // Receiving
      recieve_fin_flag  = decoder_receive_data(&dec_ctx, &out_frame, output_video_width, output_video_height, p_file, &total_bytes_recieved, print_time, 1, &xcodeState);

      if (print_time)
      {
        previous_time = current_time;
      }

      if(recieve_fin_flag < 0 || out_frame.data.frame.end_of_stream) //Error or eos
      {
        break;
      }
    }
    
    int time_diff = current_time.tv_sec - start_time.tv_sec;
    if (time_diff == 0)
      time_diff = 1;

    printf("[R] Got:  Frames= %u  fps=%d  Total bytes %llu\n", 
           number_of_frames, number_of_frames/time_diff, total_bytes_recieved);

    ni_device_session_close(&dec_ctx, 1, NI_DEVICE_TYPE_DECODER);
    ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

    ni_packet_buffer_free(&(in_pkt.data.packet));
    ni_frame_buffer_free(&(out_frame.data.frame));
  }
  else if(mode == XCODER_APP_ENCODE)
  {
    printf("Encoding Mode: %dx%d to %dx%d\n", input_video_width, input_video_height, output_video_width, output_video_height);

    ni_session_data_io_t in_frame;
    ni_session_data_io_t out_packet;
    memset(&in_frame, 0, sizeof(ni_session_data_io_t));
    memset(&out_packet, 0, sizeof(ni_session_data_io_t));
    
    while (send_fin_flag == 0 || recieve_fin_flag == 0)
    {
      (void) gettimeofday(&current_time, NULL);
      int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

      // Sending
      send_fin_flag = encoder_send_data(&enc_ctx, &in_frame, sos_flag, input_video_width, input_video_height, pfs, total_file_size, &total_bytes_sent, height_aligned, &xcodeState);
      sos_flag = 0;
      if(send_fin_flag == 2) //Error
      {
        break;
      }
    
      // Receiving
      recieve_fin_flag  = encoder_receive_data(&enc_ctx, &out_packet, output_video_width, output_video_height, p_file, &total_bytes_recieved, print_time);

      if (print_time)
      {
        previous_time = current_time;
      }

      if(recieve_fin_flag == 2 || out_packet.data.packet.end_of_stream)   //Error or eos
      {
        break;
      }
    }
    
    int timeDiff = current_time.tv_sec - start_time.tv_sec;
    if (timeDiff == 0)
      timeDiff = 1;
    printf("[R] Got:  Packets= %u fps=%d  Total bytes %lld\n", 
           number_of_packets, number_of_packets/timeDiff, total_bytes_recieved);

    ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
    ni_rsrc_release_resource(sdPara.p_enc_rsrc_ctx, NI_DEVICE_TYPE_ENCODER, load_pixel);
    ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;

    ni_frame_buffer_free(&(in_frame.data.frame));
    ni_packet_buffer_free(&(out_packet.data.packet));
  }
  else if (mode == XCODER_APP_TRANSCODE)
  {
    printf("Xcoding Mode: %dx%d to %dx%d\n", input_video_width, 
           input_video_height, output_video_width, output_video_height);

    ni_session_data_io_t in_pkt;
    ni_session_data_io_t out_frame;
    ni_session_data_io_t out_packet;
    memset(&in_pkt, 0, sizeof(ni_session_data_io_t));
    memset(&out_frame, 0, sizeof(ni_session_data_io_t));
    memset(&out_packet, 0, sizeof(ni_session_data_io_t));
    
    while (send_fin_flag == 0 || recieve_fin_flag == 0 )
    {
      (void) gettimeofday(&current_time, NULL);
      int print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

      // bitstream Sending
      send_fin_flag = decoder_send_data(&dec_ctx, &in_pkt, sos_flag, input_video_width, input_video_height, pkt_size, total_file_size, &total_bytes_sent, print_time, &xcodeState);
      sos_flag = 0;
      if(send_fin_flag == 2) //Error
      {
        break;
      }

      // YUV Receiving: not writing to file
      recieve_fin_flag  = decoder_receive_data(&dec_ctx, &out_frame, output_video_width, output_video_height, p_file, &total_bytes_recieved, print_time, 0, &xcodeState);

      if (print_time)
      {
        previous_time = current_time;
      }

      if (2 == recieve_fin_flag)
      {
        goto encode_recv;
      }

      // YUV Sending
      send_fin_flag = encoder_send_data2(&enc_ctx, &out_frame, sos_flag, input_video_width, input_video_height, pfs, total_file_size, &total_bytes_sent, height_aligned, &xcodeState);
      sos_flag = 0;
      if(send_fin_flag == 2) //Error
      {
        break;
      }

      // encoded bitstream Receiving
encode_recv:
      recieve_fin_flag  = encoder_receive_data(&enc_ctx, &out_packet, output_video_width, output_video_height, p_file, &xcodeRecvTotal, print_time);

      if (print_time)
      {
        previous_time = current_time;
      }

      if(recieve_fin_flag == 2 || out_packet.data.packet.end_of_stream)  // Error or encoder eos
      {
        break;
      }
    }

    int time_diff = current_time.tv_sec - start_time.tv_sec;
    if (time_diff == 0)
      time_diff = 1;

    printf("[R] Got:  Frames= %u  fps=%d  Total bytes %llu\n", 
           number_of_frames, number_of_frames/time_diff, total_bytes_recieved);
    printf("[R] Got:  Packets= %u fps=%d  Total bytes %lld\n", 
           number_of_packets, number_of_packets/time_diff, total_bytes_recieved);

    ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_DECODER);
    ni_rsrc_release_resource(sdPara.p_dec_rsrc_ctx, NI_DEVICE_TYPE_DECODER, load_pixel);
    ni_rsrc_free_device_context(sdPara.p_dec_rsrc_ctx);
    rcPara.p_dec_rsrc_ctx = sdPara.p_dec_rsrc_ctx = NULL;

    ni_packet_buffer_free(&(in_pkt.data.packet));
    ni_frame_buffer_free(&(out_frame.data.frame));

    ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
    ni_rsrc_release_resource(sdPara.p_enc_rsrc_ctx, NI_DEVICE_TYPE_ENCODER, load_pixel);
    ni_rsrc_free_device_context(sdPara.p_enc_rsrc_ctx);
    rcPara.p_enc_rsrc_ctx = sdPara.p_enc_rsrc_ctx = NULL;

    ni_packet_buffer_free(&(out_packet.data.packet));
  }

end:  close(pfs);
  if (p_file)
    fclose(p_file);

  // Close NVME device.
  // close(device_handle);
  printf("All Done.\n");

  return 0;
}
