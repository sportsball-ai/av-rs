/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_test.h
*
*  \brief  Example code on how to programmatically work with NI T-408 using
*          libxcoder API
*
*******************************************************************************/

#pragma once

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
 "C" {
#endif

#ifdef _WIN32
#define open  _open
#define close _close
#define read  _read
#define write _write
#define lseek _lseek
#endif

#if defined(LRETURN)
#undef LRETURN
#define LRETURN goto end;
#undef END
#define END                                     \
  end:
#else
#define LRETURN goto end;
#define END                                     \
  end:
#endif

#define NVME_CMD_SEM_PROTECT 			1


#define FILE_NAME_LEN 	256

#define XCODER_APP_TRANSCODE  0
#define XCODER_APP_DECODE     1
#define XCODER_APP_ENCODE     2

#define ENC_CONF_STRUCT_SIZE 						0x100

typedef struct _device_state
{
  int dec_eos_sent;
  int dec_eos_received;
  int enc_eos_sent;
  int enc_eos_received;
} device_state_t;

typedef struct _tx_data
{
  char fileName[FILE_NAME_LEN];
  uint32_t DataSizeLimit;

  int device_handle;
  int mode;
  ni_session_context_t *p_dec_ctx;
  ni_session_context_t *p_enc_ctx;
  ni_device_context_t *p_dec_rsrc_ctx;
  ni_device_context_t *p_enc_rsrc_ctx;
  int arg_width;
  int arg_height;

} tx_data_t;

typedef struct RecvDataStruct_
{
  char fileName[FILE_NAME_LEN];
  uint32_t DataSizeLimit;

  int device_handle;
  int mode;
  ni_session_context_t *p_dec_ctx;
  ni_session_context_t *p_enc_ctx;
  ni_device_context_t *p_dec_rsrc_ctx;
  ni_device_context_t *p_enc_rsrc_ctx;

  int arg_width;
  int arg_height;

} rx_data_t;

int decoder_send_data(ni_session_context_t* p_dec_ctx, ni_session_data_io_t* p_in_data, int stFlag, int input_video_width, int input_video_height, int pfs, int fileSize, unsigned long *sentTotal, int printT, device_state_t *xState);
int decoder_receive_data(ni_session_context_t* p_dec_ctx, ni_session_data_io_t* p_out_data, int output_video_width, int output_video_height, FILE* pfr, unsigned long long *recvTotal, int printT, int writeToFile, device_state_t *xState);
int encoder_send_data(ni_session_context_t* p_enc_ctx, ni_session_data_io_t* p_in_data, int stFlag, int input_video_width, int input_video_height, int pfs, int fileSize, unsigned long *sentSize, int height_aligned, device_state_t *xState);
int encoder_send_data2(ni_session_context_t* p_enc_ctx, ni_session_data_io_t* p_out_data, int stFlag, int input_video_width, int input_video_height, int pfs, int fileSize, unsigned long *sentSize, int aligned_height, device_state_t *xState);

#ifdef __cplusplus
}
#endif
