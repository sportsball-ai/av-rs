/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*!*****************************************************************************
*  \file   ni_device_api_priv.c
*
*  \brief  Private functions used by main ni_device_api file
*
*******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <semaphore.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <dirent.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "ni_nvme.h"
#include "ni_device_api.h"
#include "ni_device_api_priv.h"
#include "ni_util.h"

#ifdef XCODER_SIGNATURE_FILE
// this file has an array defined for signature content: ni_session_sign
#include "../build/xcoder_signature_headers.h"
#endif

typedef enum _ni_t35_sei_mesg_type
{
  NI_T35_SEI_CLOSED_CAPTION = 0,
  NI_T35_SEI_HDR10_PLUS = 1
} ni_t35_sei_mesg_type_t;

static uint8_t g_itu_t_t35_cc_sei_hdr_hevc[NI_CC_SEI_HDR_HEVC_LEN] = 
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x4e, 
  0x01, // nal_unit_header() {forbidden bit=0 nal_unit_type=39, 
  // nuh_layer_id=0 nuh_temporal_id_plus1=1)
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0 + 11, // payLoadSize= ui16Len + 11; to be set (index 7)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  0x00,
  0x31, //  itu_t_t35_provider_code = 49
  0x47, 0x41, 0x39,
  0x34, // ATSC_user_identifier = "GA94"
  0x03, // ATSC1_data_user_data_type_code=3
  0 | 0xc0, // (ui16Len/3) | 0xc0 (to be set; index 16) (each CC character 
  //is 3 bytes)
  0xFF  // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_hevc[NI_HDR10P_SEI_HDR_HEVC_LEN] = 
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x4e, 
  0x01, // nal_unit_header() {forbidden bit=0 nal_unit_type=39, 
  // nuh_layer_id=0 nuh_temporal_id_plus1=1)
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0x00, // payLoadSize; to be set (index 7)
  0xb5, //  u8 itu_t_t35_country_code =181 (North America)
  //0x00,
  //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
  //0x00,
  //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
  // payLoadSize count starts from itu_t_t35_provider_code and goes until
  // and including trailer
};

static uint8_t g_itu_t_t35_cc_sei_hdr_h264[NI_CC_SEI_HDR_H264_LEN] = 
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x06, // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0 + 11, // payLoadSize= ui16Len + 11; to be set (index 6)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  0x00,
  0x31, //  itu_t_t35_provider_code = 49
  0x47, 0x41, 0x39,
  0x34, // ATSC_user_identifier = "GA94"
  0x03, // ATSC1_data_user_data_type_code=3
  0 | 0xc0, // (ui16Len/3) | 0xc0 (to be set; index 15) (each CC character 
  //is 3 bytes)
  0xFF  // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_h264[NI_HDR10P_SEI_HDR_H264_LEN] = 
{
  0x00, 0x00, 0x00, 0x01, // NAL start code 00 00 00 01
  0x06, // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
  0x04, // payloadType= 4 (user_data_registered_itu_t_t35)
  0x00, // payLoadSize; to be set (index 6)
  0xb5, //  itu_t_t35_country_code =181 (North America)
  //0x00,
  //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
  //0x00,
  //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
  // payLoadSize count starts from itu_t_t35_provider_code and goes until
  // and including trailer
};

static uint8_t g_sei_trailer[NI_CC_SEI_TRAILER_LEN] =
{
  0xFF, // marker_bits = 255
  0x80  // RBSP trailing bits - rbsp_stop_one_bit and 7 rbsp_alignment_zero_bit
};

#define NI_XCODER_FAILURES_MAX 25

#ifdef _WIN32
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)               \
{                                                                    \
  ni_instance_status_info_t err_rc_info = { 0 };                  \
  int err_rc = ni_query_status_info(ctx, type, &err_rc_info, rc, opcode); \
  rc = err_rc_info.inst_err_no;                                 \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || (tmp_rc = ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}

#define CHECK_ERR_RC2(ctx, rc, info, opcode, type, hw_id, inst_id)   \
{                                                                    \
  rc = info.inst_err_no;                                    \
  if (info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_RETCODE_FAILURE; \
  if (info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || (tmp_rc = ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, info.sess_err_no, info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}
#elif __linux__
static struct stat g_nvme_stat = {0};

#ifdef XCODER_SELF_KILL_ERR
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)           \
{                                                                    \
  ni_instance_status_info_t err_rc_info = { 0 };                     \
  int err_rc = ni_query_status_info(ctx, type, &err_rc_info, rc, opcode);    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)) {  \
    ni_log(NI_LOG_INFO, "Terminating due to persistent failures, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    kill(getpid(), SIGTERM);                                         \
  }                                                                  \
}

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id) \
{                                                                    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id)) {  \
    ni_log(NI_LOG_INFO, "Terminating due to persistent failures, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    kill(getpid(), SIGTERM);                                         \
  }                                                                  \
}
#else
#define CHECK_ERR_RC(ctx, rc, opcode, type, hw_id, inst_id)           \
{                                                                    \
  ni_instance_status_info_t err_rc_info = { 0 };                     \
  int err_rc = ni_query_status_info(ctx, type, &err_rc_info, rc, opcode);    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || (tmp_rc = ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}

#define CHECK_ERR_RC2(ctx, rc, err_rc_info, opcode, type, hw_id, inst_id)  \
{                                                                    \
  rc = err_rc_info.inst_err_no;                                    \
  if (err_rc_info.sess_err_no || rc) ctx->rc_error_count++; else ctx->rc_error_count = 0;   \
  int tmp_rc = NI_RETCODE_FAILURE; \
  if (err_rc_info.sess_err_no || ctx->rc_error_count >= NI_XCODER_FAILURES_MAX || (tmp_rc = ni_nvme_check_error_code(rc, opcode, type, hw_id, inst_id))) { \
    ni_log(NI_LOG_INFO, "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u inst_err_no %u rc_error_count: %d\n", \
           __FUNCTION__, __LINE__, *inst_id, err_rc_info.sess_err_no, err_rc_info.inst_err_no, ctx->rc_error_count); \
    rc = tmp_rc; \
    LRETURN; \
  }                                                                   \
}
#endif                                                             

#endif

#define CHECK_VPU_RECOVERY(ret) \
{ \
  if (NI_RETCODE_NVME_SC_VPU_RECOVERY == ret) { \
    ni_log(NI_LOG_TRACE, "Error, vpu reset.\n"); \
    ret = NI_RETCODE_ERROR_VPU_RECOVERY; \
    LRETURN; \
  } \
}

#ifdef XCODER_IO_RW_ENABLED
// This is a simple implementation of round buffer queue
// once the last item in the queue is used, it will start to use the first index
// Call the init function to init the queue fist and then check if the queue is full
// Before enqueu the item

//thread mutex alloc&init
bool pthread_mutex_alloc_and_init(pthread_mutex_t **mutex)
{
  int ret = 0;
  *mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  if (!(*mutex))
  {
    return false;
  }
  ret = pthread_mutex_init(*mutex, NULL);
  if (ret != 0)
  {
    free(*mutex);
    return false;
  }

  return true;
}

//thread mutex free&destroy
bool pthread_mutex_free_and_destroy(pthread_mutex_t **mutex)
{
  int ret = 0;
  if (*mutex != NULL)
  {
    ret = pthread_mutex_destroy(*mutex);
    free(*mutex);
    *mutex = NULL;
    return ret == 0 ? true : false;
  }
  else
  {
    return false;
  }
}

#else //XCODER_IO_RW_ENABLED
// This is a simple implementation of round buffer queue
// once the last item in the queue is used, it will start to use the first index
// Call the init function to init the queue fist and then check if the queue is full
// Before enqueu the item

bool worker_q_init(queue_info *local_queue_info, worker_queue_item *queue_stack)
{
  local_queue_info->headidx = 0;
  local_queue_info->tailidx = 1;
  local_queue_info->worker_queue_head = queue_stack;
  return true;

}

bool is_worker_q_full(queue_info *local_queue_info)
{
  if ((local_queue_info->tailidx) == (WORKER_QUEUE_DEPTH - 1)) // the tail is at the end of the queue buffer
  {
    if (local_queue_info->headidx == 0) // if the head is still at the idx 0, then the queue is full
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    if ((local_queue_info->tailidx+1 )== (local_queue_info->headidx))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
}

bool is_worker_q_empty(queue_info *local_queue_info)
{   
  if ((local_queue_info->tailidx) == 0)
  {
    if (local_queue_info->headidx == (WORKER_QUEUE_DEPTH - 1))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    if (local_queue_info->headidx+1 == local_queue_info->tailidx)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
}
int32_t worker_q_enq(queue_info *local_queue_info, worker_queue_item worker_q_item)
{
  if (is_worker_q_full(local_queue_info))
  {
    return RC_ERROR;
  }
  else
  {
    local_queue_info->worker_queue_head[local_queue_info->tailidx].opcode          = worker_q_item.opcode;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].handle          = worker_q_item.handle;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].p_ni_nvme_cmd   = worker_q_item.p_ni_nvme_cmd;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].data_len        = worker_q_item.data_len;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].p_data          = worker_q_item.p_data;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].p_result        = worker_q_item.p_result;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].frame_chunk_index = worker_q_item.frame_chunk_index;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].frame_chunk_size  = worker_q_item.frame_chunk_size;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].dw14_yuv_offset = worker_q_item.dw14_yuv_offset;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].dw15_len        = worker_q_item.dw15_len;
    local_queue_info->worker_queue_head[local_queue_info->tailidx].session_id      = worker_q_item.session_id;
    ni_log(NI_LOG_TRACE, "enq opcode %x tailidx %d\n", local_queue_info->worker_queue_head[local_queue_info->tailidx].opcode, local_queue_info->tailidx );
    if ((local_queue_info->tailidx) == WORKER_QUEUE_DEPTH - 1) //  the queue is wrapping around
    {
      (local_queue_info->tailidx) = 0;
    }
    else
    {
      local_queue_info->tailidx++;
    }
  }
  return RC_SUCCESS;
}

int32_t worker_q_deq(queue_info *local_queue_info, worker_queue_item* worker_q_item)
{
  if (is_worker_q_empty(local_queue_info))
  {
    return RC_ERROR;
  }
  else
  {
    if ((local_queue_info->headidx) == WORKER_QUEUE_DEPTH - 1) //wrap around
    {
      (local_queue_info->headidx) = 0;
    }
    else
    {
      local_queue_info->headidx++;
    }
    worker_q_item->opcode          = local_queue_info->worker_queue_head[local_queue_info->headidx].opcode;
    worker_q_item->handle          = local_queue_info->worker_queue_head[local_queue_info->headidx].handle;
    worker_q_item->p_ni_nvme_cmd   = local_queue_info->worker_queue_head[local_queue_info->headidx].p_ni_nvme_cmd;
    worker_q_item->data_len        = local_queue_info->worker_queue_head[local_queue_info->headidx].data_len;
    worker_q_item->p_data          = local_queue_info->worker_queue_head[local_queue_info->headidx].p_data;
    worker_q_item->p_result        = local_queue_info->worker_queue_head[local_queue_info->headidx].p_result;
    worker_q_item->frame_chunk_index = local_queue_info->worker_queue_head[local_queue_info->headidx].frame_chunk_index;
    worker_q_item->frame_chunk_size  = local_queue_info->worker_queue_head[local_queue_info->headidx].frame_chunk_size;
    worker_q_item->dw14_yuv_offset = local_queue_info->worker_queue_head[local_queue_info->headidx].dw14_yuv_offset;
    worker_q_item->dw15_len        = local_queue_info->worker_queue_head[local_queue_info->headidx].dw15_len;
    worker_q_item->session_id      = local_queue_info->worker_queue_head[local_queue_info->headidx].session_id;

    ni_log(NI_LOG_TRACE, "deq opcode %d headidx %d\n", worker_q_item->opcode,local_queue_info->headidx );
    return RC_SUCCESS;
  }
  return RC_SUCCESS;
}

//thread mutex/cond and semaphore alloc&init
bool pthread_mutex_alloc_and_init(pthread_mutex_t **mutex)
{
  int ret = 0;
  *mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  if (!(*mutex))
  {
    return false;
  }
  ret = pthread_mutex_init(*mutex, NULL);
  return ret == 0 ? true : false;
}

bool pthread_cond_alloc_and_init(pthread_cond_t **cond)
{
  int ret = 0;
  *cond = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
  if (!(*cond))
  {
    return false;
  }
  ret = pthread_cond_init(*cond, NULL);
  return ret == 0 ? true : false;
}

bool sem_alloc_and_init(sem_t **sem)
{
  int ret = 0;
  *sem = (sem_t *)calloc(1, sizeof(sem_t));
  if (!(*sem))
  {
    return false;
  }
  ret = sem_init(*sem,0,0);
  return ret == 0 ? true : false;
}

//thread mutex/cond and semaphore free&destroy
bool pthread_mutex_free_and_destroy(pthread_mutex_t **mutex)
{
  int ret = 0;
  if (*mutex != NULL)
  {
    ret = pthread_mutex_destroy(*mutex);
    free(*mutex);
    *mutex = NULL;
    return ret == 0 ? true : false;
  }
  else
  {
    return false;
  }
}


bool pthread_cond_free_and_destroy(pthread_cond_t **cond)
{
  int ret = 0;
  if (*cond != NULL)
  {
    ret = pthread_cond_destroy(*cond);
    free(*cond);
    *cond = NULL;
    return ret == 0 ? true : false;
  }
  else
  {
    return false;
  }
}

bool sem_free_and_destroy(sem_t **sem)
{
  int ret = 0;
  if (*sem != NULL)
  {
    ret = sem_destroy(*sem);
    free(*sem);
    *sem = NULL;
    return ret == 0 ? true : false;
  }
  else
  {
    return false;
  }
}

//TODO: Elegantley exit a thread
void *workerthread_decoder_read(void *p_ctx)
{
  ni_session_context_t *local_ctx = p_ctx;
  int32_t localret = -1;
  int32_t frame_finish_flag = false;
  worker_queue_item decoder_read_localq;
  worker_queue_item* worker_q_item_local = &decoder_read_localq;
  while(1){
    localret = -1;
    sem_wait(local_ctx->decoder_read_semaphore);
    if (local_ctx->close_decoder_read_thread)
    {
        ni_log(NI_LOG_TRACE, "workerthread_decoder_read closed\n");
        break;
    }
    pthread_mutex_lock(local_ctx->decoder_read_mutex);
    worker_q_deq(&(local_ctx->decoder_read_workerqueue), worker_q_item_local);

    pthread_mutex_unlock(local_ctx->decoder_read_mutex);
    WRITE_INSTANCE_SET_DW10_SUBTYPE(worker_q_item_local->p_ni_nvme_cmd->cdw10, worker_q_item_local->session_id);
    WRITE_INSTANCE_SET_DW11_INSTANCE(worker_q_item_local->p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_DECODER);
    WRITE_INSTANCE_SET_DW11_PAGEOFFSET(worker_q_item_local->p_ni_nvme_cmd->cdw11, worker_q_item_local->frame_chunk_index);  // set page index based on 4096 bytes.
    WRITE_INSTANCE_SET_DW15_SIZE(worker_q_item_local->p_ni_nvme_cmd->cdw15, worker_q_item_local->data_len);

    ni_log(NI_LOG_TRACE, "decoder_read opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x,  data_len %d, chunk idx %d, session ID %d\n",  \
                    worker_q_item_local->opcode, \
                    worker_q_item_local->handle, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw10, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw11, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw15, \
                    worker_q_item_local->data_len, \
                    worker_q_item_local->frame_chunk_index, \
                    worker_q_item_local->session_id);
    int32_t retry_count = 0;
    while((localret!=0)){
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
        localret = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_read, worker_q_item_local->handle, worker_q_item_local->p_ni_nvme_cmd, worker_q_item_local->data_len, ((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), worker_q_item_local->p_result);
#else
        localret = ni_nvme_send_io_cmd(worker_q_item_local->opcode, worker_q_item_local->handle, worker_q_item_local->p_ni_nvme_cmd, worker_q_item_local->data_len, ((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), worker_q_item_local->p_result);
#endif
        CHECK_ERR_RC(local_ctx, localret, worker_q_item_local->opcode, local_ctx->device_type, local_ctx->hw_id, &(local_ctx->session_id));
        if (NI_RETCODE_SUCCESS == localret ||
           NI_RETCODE_NVME_SC_VPU_RECOVERY == localret)
        {
          ni_log(NI_LOG_TRACE, "decoder_read chunk idx %d, session ID %d ret %s\n",
                         worker_q_item_local->frame_chunk_index,
                         worker_q_item_local->session_id,
                         NI_RETCODE_SUCCESS == localret ? "success" : "recovery");
          *(worker_q_item_local->p_result) = worker_q_item_local->data_len;
          break;
        }
        else
        {
          ni_log(NI_LOG_TRACE, "decoder read return failed\n");
          *(worker_q_item_local->p_result) = 0;
          usleep(100);
          retry_count++;
          if (retry_count >= 6000){
            ni_log(NI_LOG_TRACE, "ERROR: something wrong in the decoder read\n");
            assert(false);
          }
          continue;
        }
    }
    ni_log(NI_LOG_TRACE, "decoder_read success data_len %d\n", worker_q_item_local->data_len);

    pthread_mutex_lock(local_ctx->decoder_read_mutex_len);
    local_ctx->decoder_read_processed_data_len += *(worker_q_item_local->p_result);
    //here we check if the last chunk in the frame had been sent out
    if (local_ctx->decoder_read_processed_data_len == local_ctx->decoder_read_total_num_of_bytes){
      frame_finish_flag = true;
    }
    pthread_mutex_unlock(local_ctx->decoder_read_mutex_len);

    if (frame_finish_flag)
    {
      pthread_cond_signal(local_ctx->decoder_read_cond);
    }

    END;
  }

  pthread_exit (NULL);
}


void *workerthread_encoder_read(void *p_ctx)
{
  ni_session_context_t *local_ctx = p_ctx;
  int32_t localret = -1;
  int32_t frame_finish_flag = false;
  worker_queue_item encoder_read_localq;
  worker_queue_item* worker_q_item_local = &encoder_read_localq;

  ni_log(NI_LOG_TRACE, "Enter the encoder read\n");
  while(1){
    localret = -1;
    sem_wait(local_ctx->encoder_read_sem);
    if (local_ctx->close_encoder_read_thread)
    {
      ni_log(NI_LOG_TRACE, "workerthread_encoder_read closed\n");
      break;
    }
    pthread_mutex_lock(local_ctx->encoder_read_mutex);
    worker_q_deq(&(local_ctx->encoder_read_workerqueue), worker_q_item_local);
    pthread_mutex_unlock(local_ctx->encoder_read_mutex);

    WRITE_INSTANCE_SET_DW10_SUBTYPE(worker_q_item_local->p_ni_nvme_cmd->cdw10, worker_q_item_local->session_id);
    WRITE_INSTANCE_SET_DW11_INSTANCE(worker_q_item_local->p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_ENCODER);
    WRITE_INSTANCE_SET_DW11_PAGEOFFSET(worker_q_item_local->p_ni_nvme_cmd->cdw11, worker_q_item_local->frame_chunk_index);  // set page index based on 4k
    WRITE_INSTANCE_SET_DW15_SIZE(worker_q_item_local->p_ni_nvme_cmd->cdw15, worker_q_item_local->data_len);

    ni_log(NI_LOG_TRACE, "decoder_read opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x,  data_len %d, chunk idx %d, session ID %d\n",  \
                    worker_q_item_local->opcode, \
                    worker_q_item_local->handle, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw10, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw11, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw15, \
                    worker_q_item_local->data_len, \
                    worker_q_item_local->frame_chunk_index, \
                    worker_q_item_local->session_id);
    int32_t retry_count = 0;
    while((localret!=0)){
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
      localret = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_read, worker_q_item_local->handle, worker_q_item_local->p_ni_nvme_cmd, worker_q_item_local->data_len, ((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), worker_q_item_local->p_result);
#else
      localret = ni_nvme_send_io_cmd(worker_q_item_local->opcode, worker_q_item_local->handle, worker_q_item_local->p_ni_nvme_cmd, worker_q_item_local->data_len, ((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), worker_q_item_local->p_result);
#endif
      CHECK_ERR_RC(local_ctx, localret, worker_q_item_local->opcode, local_ctx->device_type, local_ctx->hw_id, &(local_ctx->session_id));

      if (localret != NI_RETCODE_SUCCESS)
      {
        ni_log(NI_LOG_TRACE, "encoder read return failed\n");
        *(worker_q_item_local->p_result) = 0;
        usleep(100);
        retry_count++;
        if (retry_count>=6000){
          ni_log(NI_LOG_TRACE, "ERROR: something wrong in the decoder read");
          assert(false);
        }
        continue;
      }
      else
      {
        *(worker_q_item_local->p_result) = worker_q_item_local->data_len;
        break;
      }
    }
    
    pthread_mutex_lock(local_ctx->encoder_read_mutex_len);
    local_ctx->encoder_read_processed_data_len += *(worker_q_item_local->p_result);
    if (local_ctx->encoder_read_processed_data_len == local_ctx->encoder_read_total_num_of_bytes){
      frame_finish_flag = true;
    }
    pthread_mutex_unlock(local_ctx->encoder_read_mutex_len);
    if (frame_finish_flag)
    {
      pthread_cond_signal(local_ctx->encoder_read_cond);
    }

    END;
  }
  pthread_exit (NULL);
}

void *workerthread_encoder_write(void *p_ctx)
{
  ni_session_context_t *local_ctx = p_ctx;
  int32_t localret = -1;
  int32_t ew_frame_finish_flag = false;
  int32_t data_lens = 0;
  worker_queue_item encoder_write_worker={0};
  worker_queue_item* worker_q_item_local = &encoder_write_worker;
  ni_nvme_passthrough_cmd_t nvme_cmd;
#ifdef MSVC_BUILD
  ni_log(NI_LOG_TRACE,"Woker thread enter the encoder write\n");
#else
  ni_log(NI_LOG_TRACE,"TID: %ld Enter the encoder write\n", pthread_self());
#endif
 
  while(1){
    localret = -1;
    sem_wait(local_ctx->encoder_write_semaphore);
    if (local_ctx->close_encoder_write_thread)
    {
      ni_log(NI_LOG_TRACE, "workerthread_encoder_write closed\n");
      break;
    }
    pthread_mutex_lock(local_ctx->encoder_write_mutex);
    worker_q_deq(&(local_ctx->encoder_write_workerqueue), worker_q_item_local);
    pthread_mutex_unlock(local_ctx->encoder_write_mutex);

    WRITE_INSTANCE_SET_DW10_SUBTYPE(worker_q_item_local->p_ni_nvme_cmd->cdw10, worker_q_item_local->session_id);
    WRITE_INSTANCE_SET_DW11_INSTANCE(worker_q_item_local->p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_ENCODER);
    WRITE_INSTANCE_SET_DW11_PAGEOFFSET(worker_q_item_local->p_ni_nvme_cmd->cdw11, worker_q_item_local->frame_chunk_index);  // set page index based on 4k
    WRITE_INSTANCE_SET_DW14_YUV_BYTEOFFSET(worker_q_item_local->p_ni_nvme_cmd->cdw14, worker_q_item_local->dw14_yuv_offset);
    WRITE_INSTANCE_SET_DW15_SIZE(worker_q_item_local->p_ni_nvme_cmd->cdw15, worker_q_item_local->data_len);
    data_lens = worker_q_item_local->data_len;
    if (data_lens % 512)
    {
      data_lens = ((worker_q_item_local->data_len)/512 + 1)*512;
    }


#ifdef MSVC_BUILD
    ni_log(NI_LOG_TRACE,"Worker thread encoder write opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x, data_len %d, chunk idx %d, session ID %d dw14_yuv_offset %d\n",  \
                    worker_q_item_local->opcode, \
                    worker_q_item_local->handle, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw10, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw11, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw15, \
                    worker_q_item_local->data_len, \
                    worker_q_item_local->frame_chunk_index, \
                    worker_q_item_local->session_id,
                    worker_q_item_local->dw14_yuv_offset);
#else
    ni_log(NI_LOG_TRACE,"TID:%lu Encoder write opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x, data_len %d, chunk idx %d, session ID %d dw14_yuv_offset %d\n",  \
                    pthread_self(), \
                    worker_q_item_local->opcode, \
                    worker_q_item_local->handle, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw10, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw11, \
                    worker_q_item_local->p_ni_nvme_cmd->cdw15, \
                    worker_q_item_local->data_len, \
                    worker_q_item_local->frame_chunk_index, \
                    worker_q_item_local->session_id,
                    worker_q_item_local->dw14_yuv_offset);
#endif
    int32_t retry_count = 0;
    while((localret!=0)){
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
      localret = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_write, \
                                     worker_q_item_local->handle, \
                                     worker_q_item_local->p_ni_nvme_cmd, \
                                     worker_q_item_local->data_len, \
                                     (((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE)), \
                                     worker_q_item_local->p_result);
#else

      localret = ni_nvme_send_io_cmd(worker_q_item_local->opcode, \
                                     worker_q_item_local->handle, \
                                     worker_q_item_local->p_ni_nvme_cmd, \
                                     data_lens, \
                                     (((worker_q_item_local->p_data)+(worker_q_item_local->frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE)), \
                                     worker_q_item_local->p_result);

#endif
      CHECK_ERR_RC(local_ctx, localret, worker_q_item_local->opcode, local_ctx->device_type, local_ctx->hw_id, &(local_ctx->session_id));
      if (localret != 0)
      {
        ni_log(NI_LOG_TRACE, "encoder write return failed\n");
        *(worker_q_item_local->p_result) = 0;
        usleep(100);
        retry_count++;
        if (retry_count >= 6000){
          ni_log(NI_LOG_TRACE, "ERROR: something wrong in the decoder read");
          assert(false);
        }
        continue;
      }
      else
      {
        *(worker_q_item_local->p_result) = worker_q_item_local->data_len;
      }
      
    }
    // We now protect this
    pthread_mutex_lock(local_ctx->encoder_write_mutex_len);
    local_ctx->encoder_write_processed_data_len += *(worker_q_item_local->p_result);
    ni_log(NI_LOG_TRACE,"encoder_write_processed_data_len in thread %d %d\n", local_ctx->encoder_write_processed_data_len, local_ctx->encoder_write_total_num_of_bytes);
    //here we check if the last chunk in the frame had been sent out
    if (local_ctx->encoder_write_processed_data_len == local_ctx->encoder_write_total_num_of_bytes){
      ew_frame_finish_flag = true;
    }
    pthread_mutex_unlock(local_ctx->encoder_write_mutex_len);

    if (ew_frame_finish_flag){
      pthread_cond_signal(local_ctx->encoder_write_cond);
    }

    END;
  }
  pthread_exit (NULL);
}
#endif //XCODER_IO_RW_ENABLED

/*!******************************************************************************
 *  \brief  Open a xcoder decoder instance
 *
 *  \param
 *
 *  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_open(ni_session_context_t* p_ctx)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t model_load = 0;
  void *p_signature = NULL;
  uint32_t buffer_size = 0;
  void* p_buffer = NULL;
  ni_decoder_session_open_info_t session_info = { 0 };
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "ni_decoder_session_open(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_open(): passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

#ifndef XCODER_IO_RW_ENABLED //No need background RW if XCODER_IO_RW_ENABLED
  //thread mutex/cond and semaphore init
  if (!pthread_mutex_alloc_and_init(&p_ctx->decoder_read_mutex))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): alloc decoder_read_mutex fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!pthread_mutex_alloc_and_init(&p_ctx->decoder_read_mutex_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): alloc decoder_read_mutex_len fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  if (!pthread_cond_alloc_and_init(&p_ctx->decoder_read_cond))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): alloc decoder_read_cond fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  if (!sem_alloc_and_init(&p_ctx->decoder_read_semaphore))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): alloc decoder_read_semaphore fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  p_ctx->close_decoder_read_thread = false;
  worker_q_init(&(p_ctx->decoder_read_workerqueue), p_ctx->worker_queue_decoder_read);
  // Here we create a worker thread pool
  int counter = 0; 
  for(counter = 0; counter< MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    if (pthread_create(&(p_ctx->ThreadID_decoder_read[counter]), NULL, workerthread_decoder_read, p_ctx))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_decoder_session_open failed to create decoder read threads !\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
  }
#endif

  //Create the session if the create session flag is set
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    int i;

    p_ctx->device_type = NI_DEVICE_TYPE_DECODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->lone_sei_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->required_buf_size = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;
    p_ctx->session_timestamp = 0;

    for (i = 0; i < NI_FIFO_SZ; i++)
    {
      p_ctx->pkt_custom_sei[i] = NULL;
      p_ctx->pkt_custom_sei_len[i] = 0;
    }
    p_ctx->last_pkt_custom_sei = NULL;
    p_ctx->last_pkt_custom_sei_len = 0;

    p_ctx->codec_total_ticks = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->p_dec_packet_inf_buf = NULL;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    p_ctx->codec_start_time = tv.tv_sec*1000000ULL + tv.tv_usec;
#if NI_DEBUG_LATENCY
    p_ctx->microseconds_r = 0;
    p_ctx->microseconds_w_prev = 0;
    p_ctx->microseconds_w = 0;
#endif

#ifdef _WIN32
    p_ctx->event_handle = ni_create_event();
    if (p_ctx->event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }

    p_ctx->thread_event_handle = ni_create_event();
    if (p_ctx->thread_event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }
#endif

    if (((ni_encoder_params_t*)p_ctx->p_session_config)->fps_denominator != 0)
    {
      model_load = (((ni_encoder_params_t*)p_ctx->p_session_config)->source_width *
                   ((ni_encoder_params_t*)p_ctx->p_session_config)->source_height *
                   ((ni_encoder_params_t*)p_ctx->p_session_config)->fps_number) /
                   (((ni_encoder_params_t*)p_ctx->p_session_config)->fps_denominator);
    }
    else
    {
      ni_log(NI_LOG_TRACE, "fps_denominator should not be 0 at this point\n");
      assert(false);
    }

    ni_log(NI_LOG_TRACE, "Model load info:: W:%d H:%d F:%d :%d Load:%d",
                  ((ni_encoder_params_t*)p_ctx->p_session_config)->source_width,
                  ((ni_encoder_params_t*)p_ctx->p_session_config)->source_height,
                  ((ni_encoder_params_t*)p_ctx->p_session_config)->fps_number,
                  ((ni_encoder_params_t*)p_ctx->p_session_config)->fps_denominator,
                  model_load);

#ifdef XCODER_IO_RW_ENABLED
    //malloc zero data buffer
    if(posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_decoder_session_open() alloc decoder all zero buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc decoder packet info buffer
    if(posix_memalign(&p_ctx->p_dec_packet_inf_buf, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d:ni_decoder_session_open() alloc decoder packet info buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_dec_packet_inf_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if(posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_decoder_session_open() alloc data buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    // Get session ID
    ui32LBA = OPEN_GET_SID_R(NI_DEVICE_TYPE_DECODER);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): nvme read command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    p_ctx->session_id = ((ni_get_session_id_t *)p_buffer)->session_id;
    ni_log(NI_LOG_TRACE, "Decoder open session ID:0x%x\n",p_ctx->session_id);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): query session ID failed, p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }

    //Send session Info
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    session_info.codec_format = ni_htonl(p_ctx->codec_format);
    session_info.model_load = ni_htonl(model_load);
    memcpy(p_buffer, &session_info, sizeof(ni_decoder_session_open_info_t));
    ui32LBA = OPEN_SESSION_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_open,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): nvme write command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout = p_ctx->keep_alive_timeout * 1000000;  //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
		                            p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_open(): nvme write keep_alive_timeout command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
#else //XCODER_IO_RW_ENABLED

    CREATE_SESSION_SET_DW10_SUBTYPE(cmd.cdw10);
    CREATE_SESSION_SET_DW11_INSTANCE(cmd.cdw11, NI_DEVICE_TYPE_DECODER);
    CREATE_SESSION_SET_DW12_DEC_CID(cmd.cdw12, p_ctx->codec_format);
    // Later if the model load is exceeding uint32 max, then we can start to divide some value in order to make it fit to uint32
    CREATE_SESSION_SET_DW14_MODEL_LOAD(cmd.cdw14, model_load);

#ifdef XCODER_SIGNATURE_FILE
    CREATE_SESSION_SET_DW15_SIZE(cmd.cdw15, NI_SIGNATURE_SIZE);

    buffer_size = NI_SIGNATURE_SIZE;
    buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
    if (posix_memalign(&p_signature, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate signature buffer.\n");
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    memset((uint8_t*)p_signature, 0, buffer_size);
    memcpy((uint8_t*)p_signature, ni_session_sign,
           ni_session_sign_len < NI_SIGNATURE_SIZE ? 
           ni_session_sign_len : NI_SIGNATURE_SIZE);
#endif

    if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_open, p_ctx->device_handle, &cmd, buffer_size, p_signature, &(p_ctx->session_id)))
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_open(): p_ctx->device_handle=%" PRIx64 " , p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_open(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }
#endif //XCODER_IO_RW_ENABLED

    ni_log(NI_LOG_TRACE, "ni_decoder_session_open(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
  }

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = 0;
  p_ctx->last_dts = 0;
  p_ctx->pts_correction_num_faulty_dts = 0;
  p_ctx->pts_correction_last_dts = 0;
  p_ctx->pts_correction_num_faulty_pts = 0;
  p_ctx->pts_correction_last_pts = 0;

  //p_ctx->p_leftover = malloc(NI_MAX_PACKET_SZ * 2);
  p_ctx->p_leftover = malloc(p_ctx->max_nvme_io_size * 2);
  if (!p_ctx->p_leftover)
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_decoder_session_open(): Cannot allocate leftover buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    ni_decoder_session_close(p_ctx, 0);
    LRETURN;
  }

  ni_timestamp_init(p_ctx, (ni_timestamp_table_t * *) & (p_ctx->pts_table), "dec_pts");
  ni_timestamp_init(p_ctx, (ni_timestamp_table_t * *) & (p_ctx->dts_queue), "dec_dts");

  if (p_ctx->p_session_config)
  {
    ni_encoder_params_t* p_param = (ni_encoder_params_t*)p_ctx->p_session_config;
    ni_params_print(p_param);
  }
  
  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;

  ni_log(NI_LOG_TRACE, "ni_decoder_session_open(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
    "p_ctx->session_id=%d\n",
    (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

#ifdef XCODER_DUMP_DATA
  char dir_name[128] = { 0 };
  snprintf(dir_name, sizeof(dir_name), "%ld-%u-dec-pkt", (long)getpid(),
           p_ctx->session_id);

  DIR* dir = opendir(dir_name);
  if (! dir && ENOENT == errno)
  {
    mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
    ni_log(NI_LOG_INFO, "Decoder pkt dump dir created: %s\n", dir_name);
  }

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-dec-fme", (long)getpid(),
           p_ctx->session_id);
  dir = opendir(dir_name);
  if (! dir && ENOENT == errno)
  {
    mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
    ni_log(NI_LOG_INFO, "Decoder frame dump dir created: %s\n", dir_name);
  }
#endif

  END;

  aligned_free(p_buffer);
  aligned_free(p_signature);

  ni_log(NI_LOG_TRACE, "ni_decoder_session_open(): exit\n");
  
  return retval;
}

/*!******************************************************************************
 *  \brief  send a keep alive message to firmware
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_send_session_keep_alive(uint32_t session_id, ni_device_handle_t device_handle, ni_event_handle_t event_handle, void *p_data)
{
  ni_nvme_command_t cmd = { 0 };
  ni_nvme_result_t nvme_result = 0;
  ni_retcode_t retval;
  uint32_t ui32LBA = 0;
  
  if (NI_INVALID_SESSION_ID == session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_session_keep_alive(): Invalid session ID!, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (NI_INVALID_DEVICE_HANDLE == device_handle)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_session_keep_alive(): xcoder instance id < 0, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  
#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_SESSION_KeepAlive_W(session_id);
  if (ni_nvme_send_write_cmd(device_handle, event_handle, p_data, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_session_keep alive(): device_handle=%" PRIx64 " , session_id=%d\n", (int64_t)device_handle, session_id);
    retval = NI_RETCODE_FAILURE;
  }
  else
  {  
    ni_log(NI_LOG_TRACE, "SUCCESS ni_session_keep alive(): device_handle=%" PRIx64 " , session_id=%d\n", (int64_t)device_handle, session_id);
    retval = NI_RETCODE_SUCCESS;
  }
#else
  CONFIG_SESSION_SET_DW10_SESSION_ID(cmd.cdw10, session_id);
  CONFIG_SESSION_SET_DW11_SUBTYPE(cmd.cdw11, nvme_config_xcoder_config_session_keep_alive);
  retval = ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, device_handle, &cmd, 0, NULL, &nvme_result);
  if (NI_RETCODE_SUCCESS == retval)
  {
    ni_log(NI_LOG_TRACE, "SUCCESS ni_session_keep alive(): device_handle=%" PRIx64 " , session_id=%d\n", (int64_t)device_handle, session_id);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_session_keep alive(): device_handle=%" PRIx64 " , session_id=%d\n", (int64_t)device_handle, session_id);
  }
#endif

  END;
  ni_log(NI_LOG_TRACE, "ni_session_keepalive(): exit\n");
  return retval;
}

/*!******************************************************************************
 *  \brief  Flush decoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_flush(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log(NI_LOG_TRACE, "ni_decoder_session_flush(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_decoder_session_flush(): xcoder instance id < 0, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->ready_to_close = 1;
  }

  END;
  ni_log(NI_LOG_TRACE, "ni_decoder_session_flush(): success exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder decoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  int counter = 0;
  int ret = 0;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int i;

  ni_log(NI_LOG_TRACE, "ni_decoder_session_close(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_decoder_session_close() passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

#ifndef XCODER_IO_RW_ENABLED //No need background RW if XCODER_IO_RW_ENABLED
  // Here we close all session read/write threads
  p_ctx->close_decoder_read_thread = true; //set 1 to close decoder worker read thread
  for (counter = 0; counter< MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    if (p_ctx->ThreadID_decoder_read[counter])
    {
      sem_post(p_ctx->decoder_read_semaphore);
    }
  }
  for (counter = 0; counter< MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    if (p_ctx->ThreadID_decoder_read[counter])
    {
      ret = pthread_join(p_ctx->ThreadID_decoder_read[counter],NULL);
      if (ret)
      {
        ni_log(NI_LOG_ERROR, "ERROR %d: encoder read thread cancle wait finished failed:%d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, ret);
        ni_log(NI_LOG_TRACE, "Count:%d, ThreadID:%d\n",counter, (uint32_t)p_ctx->ThreadID_decoder_read[counter]);
        retval = NI_RETCODE_FAILURE;
      }
      p_ctx->ThreadID_decoder_read[counter] = 0;
      ni_log(NI_LOG_TRACE, "Cancel decoder read thread-%d: %d\n", counter,p_ctx->session_id);
    }
  }
  p_ctx->close_decoder_read_thread = false;

  //pthread mutex/cond and semaphore free&destroy
  sem_free_and_destroy(&p_ctx->decoder_read_semaphore);

  pthread_cond_free_and_destroy(&p_ctx->decoder_read_cond);

  pthread_mutex_free_and_destroy(&p_ctx->decoder_read_mutex);
  pthread_mutex_free_and_destroy(&p_ctx->decoder_read_mutex_len);
#endif

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_close(): Cannot allocate leftover buffer.\n");
    retval = NI_RETCODE_SUCCESS;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  //malloc data buffer
  if(posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: malloc decoder close data buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    
  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  int retry = 0;
  while (retry < NI_MAX_SESSION_CLOSE_RETRIES)  // 10 retries
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_close(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
           "p_ctx->session_id=%d, close_mode=1\n",
           (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_close(): command failed!\n");
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      break;
    }
    else if (((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "ni_decoder_session_close(): wait for close\n");
      usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US); // 500000 us
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    retry++;
  }
#else //XCODER_IO_RW_ENABLED

  DESTROY_SESSION_SET_DW10_INSTANCE(cmd.cdw10, p_ctx->session_id);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_close(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
      "p_ctx->session_id=%d, close_mode=1\n",
      (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_close, p_ctx->device_handle, &cmd, 0, NULL, &nvme_result))
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_close(): nvme command failed!\n");
      usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    else
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    retry++;
  }
#endif //XCODER_IO_RW_ENABLED

  END;

  aligned_free(p_buffer);
  aligned_free(p_ctx->p_all_zero_buf);
  aligned_free(p_ctx->p_dec_packet_inf_buf);

  if (NULL != p_ctx->p_leftover)
  {
    free(p_ctx->p_leftover);
    p_ctx->p_leftover = NULL;
  }

  if (p_ctx->pts_table)
  {
    ni_timestamp_table_t* p_pts_table = p_ctx->pts_table;
    ni_queue_free(&p_pts_table->list, p_ctx->buffer_pool);
    free(p_ctx->pts_table);
    p_ctx->pts_table = NULL;
    ni_log(NI_LOG_TRACE, "ni_timestamp_done: success\n");
  }

  if (p_ctx->dts_queue)
  {
    ni_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
    ni_queue_free(&p_dts_queue->list, p_ctx->buffer_pool);
    free(p_ctx->dts_queue);
    p_ctx->dts_queue = NULL;
    ni_log(NI_LOG_TRACE, "ni_timestamp_done: success\n");
  }

  ni_buffer_pool_free(p_ctx->buffer_pool);
  p_ctx->buffer_pool = NULL;

  ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
  p_ctx->dec_fme_buf_pool = NULL;

  for (i = 0; i < NI_FIFO_SZ; i++)
  {
    free(p_ctx->pkt_custom_sei[i]);
    p_ctx->pkt_custom_sei[i] = NULL;
    p_ctx->pkt_custom_sei_len[i] = 0;
  }

  free(p_ctx->last_pkt_custom_sei);
  p_ctx->last_pkt_custom_sei = NULL;
  p_ctx->last_pkt_custom_sei_len = 0;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t codec_end_time = tv.tv_sec*1000000ULL + tv.tv_usec;
  if ((p_ctx->codec_total_ticks) && (codec_end_time - p_ctx->codec_start_time)) //if close immediately after opened, end time may equals to start time
  {
    uint32_t ni_usage = (p_ctx->codec_total_ticks/NI_VPU_FREQ)*100/(codec_end_time - p_ctx->codec_start_time);
    ni_log(NI_LOG_INFO, "Decoder HW[%d] INST[%d]-average usage:%d%%\n", p_ctx->hw_id, (p_ctx->session_id&0x7F), ni_usage);
  }
  else
  {
    ni_log(NI_LOG_INFO, "Warning Decoder HW[%d] INST[%d]-average usage equals to 0\n", p_ctx->hw_id, (p_ctx->session_id&0x7F));
  }

  ni_log(NI_LOG_TRACE, "decoder total_pkt:%" PRIx64 ", total_ticks:%" PRIx64 ", total_time:%" PRIx64 " us\n",
         p_ctx->frame_num,
         p_ctx->codec_total_ticks,
         (codec_end_time - p_ctx->codec_start_time));

  ni_log(NI_LOG_TRACE, "ni_decoder_session_close():  CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  ni_log(NI_LOG_TRACE, "ni_decoder_session_close(): exit\n");
  return retval;
}

/*!******************************************************************************
 *  \brief  Send a video p_packet to decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_write(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  uint32_t sent_size = 0;
  uint32_t packet_size = 0;
  uint32_t write_size_bytes = 0;
  uint32_t actual_sent_size = 0;
  uint32_t pkt_chunk_count = 0;
  int retval = NI_RETCODE_SUCCESS;
  ni_instance_status_info_t inst_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;
#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "ni_decoder_session_write(): enter\n");

  if ((!p_ctx) || (!p_packet))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  if ((NI_INVALID_SESSION_ID == p_ctx->session_id))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_decoder_session_write(): xcoder instance id < 0, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
      abs_time_ns = ni_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_lat_meas_q_add_entry(p_ctx->frame_time_q, abs_time_ns, p_packet->dts);
  }
#endif

  packet_size = p_packet->data_len;
  int current_pkt_size = p_packet->data_len;

  while (1)
  {
    query_retry++;
    retval = ni_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval ||
        inst_info.wr_buf_avail_size < packet_size)
    {
      ni_log(NI_LOG_TRACE, "Warning dec write query fail rc %d or available "
                     "buf size %u < pkt size %u , retry: %d\n", retval, 
                     inst_info.wr_buf_avail_size, packet_size, query_retry);
      if (query_retry > NI_MAX_DEC_SESSION_WRITE_QUERY_RETRIES)
      {
        p_ctx->required_buf_size = packet_size;
        p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
      }
      usleep(NI_RETRY_INTERVAL_100US);  // 100 us
    }
    else
    {
      ni_log(NI_LOG_TRACE, "Info dec write query success, available buf "
                     "size %u >= pkt size %u !\n",
                     inst_info.wr_buf_avail_size, packet_size);
      break;
    }
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
#else
  WRITE_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  WRITE_INSTANCE_SET_DW11_INSTANCE(cmd.cdw11, NI_DEVICE_TYPE_DECODER);
#endif

  //check for start of stream flag
  if (p_packet->start_of_stream)
  {
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_write(): Failed to send SOS.\n");
      LRETURN;
    }

    p_packet->start_of_stream = 0;
  }

  if (p_packet->p_data)
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_write() had data to send: packet_size=%u, p_packet->sent_size=%d, p_packet->data_len=%d, p_packet->start_of_stream=%d, p_packet->end_of_stream=%d, p_packet->video_width=%d, p_packet->video_height=%d\n",
                           packet_size, p_packet->sent_size, p_packet->data_len, p_packet->start_of_stream, p_packet->end_of_stream, p_packet->video_width, p_packet->video_height);

    uint32_t send_count = 0;
    uint8_t* p_data = (uint8_t*)p_packet->p_data;
                // Note: session status is NOT reset but tracked between send
                // and recv to catch and recover from a loop condition
    // p_ctx->status = 0;

#ifdef XCODER_IO_RW_ENABLED
    ni_instance_dec_packet_info_t *p_dec_packet_info;
    p_dec_packet_info = (ni_instance_dec_packet_info_t *)p_ctx->p_dec_packet_inf_buf;
    p_dec_packet_info->packet_size = packet_size;
    retval = ni_nvme_send_write_cmd(
      p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_dec_packet_inf_buf,
      NI_DATA_BUFFER_LEN, CONFIG_INSTANCE_SetPktSize_W(p_ctx->session_id,
                                                       NI_DEVICE_TYPE_DECODER));
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_write(): config pkt size command failed\n");
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    if (packet_size % NI_MEM_PAGE_ALIGNMENT) //packet size, already aligned
    {
        packet_size = ( (packet_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    } 

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, packet_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_write(): nvme command failed\n");
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    // reset session status after successful send
    p_ctx->status = 0;
    p_ctx->required_buf_size = 0;

    sent_size = p_packet->data_len;
    p_packet->data_len = 0;

    p_ctx->pkt_num++;

#ifdef XCODER_DUMP_DATA
    char dump_file[128] = { 0 };
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-dec-pkt/pkt-%04ld.bin",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->pkt_num);

    FILE *f = fopen(dump_file, "wb");
    fwrite(p_packet->p_data, sent_size, 1, f);
    fflush(f);
    fclose(f);
#endif

#else //XCODER_IO_RW_ENABLED

    //Checks if there was half a p_packet sent previously.
    //If so, update the necessary variables to inidicate how much data is sent
    //This is needed since FFMPEG deals in p_frame atomically, where as transcoder deals with data as bytestream.
    if (p_ctx->sent_size)
    {
      p_packet->sent_size += p_ctx->sent_size;
      p_packet->data_len -= p_ctx->sent_size;
      packet_size -= p_ctx->sent_size;
      // for old nvme driver, left data has already been
      // copied to the start position so don't move pointer
#ifndef XCODER_OLD_NVME_DRIVER_ENABLED
      p_data += p_ctx->sent_size;
#endif
      sent_size = p_ctx->sent_size;
      p_ctx->sent_size = 0;
    }

    // Here we calculate how many chunks are in this pkt, still need to add 1 as it will round down
    pkt_chunk_count = packet_size/p_ctx->max_nvme_io_size;
    pkt_chunk_count++;
    while (packet_size > 0)
    {
      actual_sent_size = write_size_bytes = (packet_size <= p_ctx->max_nvme_io_size) ? packet_size : p_ctx->max_nvme_io_size;

      ni_log(NI_LOG_TRACE, "ni_decoder_session_write(): packet_size=%u, write_size_bytes=%u, p_packet->sent_size=%d, p_packet->start_of_stream=%d, p_packet->end_of_stream=%d, retry=%u\n",
                     packet_size, write_size_bytes, p_packet->sent_size, p_packet->start_of_stream, p_packet->end_of_stream, send_count);

      WRITE_INSTANCE_SET_DW15_SIZE(cmd.cdw15, write_size_bytes);
      if (pkt_chunk_count == 1)
      {
        WRITE_INSTANCE_SET_DW11_PAGEOFFSET(cmd.cdw11,true);
      }
      else
      {
        WRITE_INSTANCE_SET_DW11_PAGEOFFSET(cmd.cdw11,false);
      }

      // Adjust the actual TX size here to be multiples of 512
      // bytes to prevent user->kernel copy
      if (write_size_bytes < p_ctx->max_nvme_io_size)
      {
        if (write_size_bytes % NI_MEM_PAGE_ALIGNMENT)
        {
          write_size_bytes = ( (write_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
        }
      }

#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
      retval = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_write, p_ctx->blk_io_handle, &cmd, write_size_bytes, p_data, &nvme_result);
#else
      retval = ni_nvme_send_io_cmd(nvme_cmd_xcoder_write, p_ctx->blk_io_handle, &cmd, write_size_bytes, p_data, &nvme_result);
#endif
      CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                   p_ctx->device_type, p_ctx->hw_id,
                   &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);

      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log(NI_LOG_TRACE, "Warn: ni_nvme_send_io_cmd(nvme_cmd_xcoder_write). retval=%d, status=%d, total_size=%d\n", retval, p_ctx->status, p_packet->data_len);
        // return code other than 0 requires resend
        nvme_result = 0;
        send_count++;

        // If p_frame is partial written, usually because
        // sending failure (esp. centos), retry a number of
        // times; usually retry would be successful;
        // In the case of exceeding retry limit, record 
        // how much was written so we can continue from 
        // there in the next round; at the same time, since
        // a p_frame write is atomic, tell upper layer we 
        // did not write anything.
        if (send_count >= NI_MAX_TX_RETRIES)
        {
          if (p_ctx->status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
          {
            ni_log(NI_LOG_TRACE, "WARNING decoder: sending pkt %" PRIu64 " getting in write BUF_FULL condition again after previous BUF_FULL; nvme_result=%u, p_packet->sent_size=%d, p_packet->data_len=%d, p_ctx->sent_size=%d\n",
                           p_ctx->pkt_num, nvme_result, p_packet->sent_size, p_packet->data_len, p_ctx->sent_size);
          }

          p_ctx->sent_size = sent_size;
          p_packet->sent_size -= sent_size;
          p_packet->data_len += sent_size;
          p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
          ni_log(NI_LOG_TRACE, "ni_decoder_session_write(): result=%u, status=%d, p_packet->sent_size=%d, p_packet->data_len=%d, p_ctx->sent_size=%d\n", nvme_result, p_ctx->status, p_packet->sent_size, p_packet->data_len, p_ctx->sent_size);
          sent_size = 0;
          LRETURN;
        }
      }
      else // rc == 0 means sending success now
      {
        pkt_chunk_count--;
        nvme_result = actual_sent_size;

        // reset session status after successful send
        p_ctx->status = 0;
      }

      //result will have the amount of bytes actually written
      sent_size += nvme_result;
      packet_size -= nvme_result;
      p_packet->sent_size += nvme_result;
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
      memmove(p_data, p_data + nvme_result, packet_size);
#else
      p_data += nvme_result;
#endif

      p_packet->data_len -= nvme_result;

    }

    if (0 == p_packet->data_len)
    {
      sent_size = p_packet->sent_size;
    }

    p_ctx->pkt_num++;
#endif //XCODER_IO_RW_ENABLED
  }

  //Handle end of stream flag
  if (p_packet->end_of_stream)
  {
    retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_write(): Failed to send EOS.\n");
      LRETURN;
    }

    p_packet->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }
  if (p_ctx->is_dec_pkt_512_aligned)
  {
    // save NI_MAX_DEC_REJECT pts values and their corresponding number of 512 aligned data
    if (p_ctx->is_first_frame && (p_ctx->pkt_index != -1))
    {
      p_ctx->pts_offsets[p_ctx->pkt_index] = p_packet->pts;
      p_ctx->pkt_offsets_index[p_ctx->pkt_index] = current_pkt_size/512; // assuming packet_size is 512 aligned
      p_ctx->pkt_index ++;
      if (p_ctx->pkt_index >= NI_MAX_DEC_REJECT)
      {
        ni_log(NI_LOG_DEBUG, "ni_decoder_session_write(): more than NI_MAX_DEC_REJECT frames are rejected by the decoder. Increase NI_MAX_DEC_REJECT is required or default gen pts values will be used !\n");
        p_ctx->pkt_index = -1; // signaling default pts gen
      }
    }
  }
  else
  {
    p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->pts;
    if (p_ctx->pkt_index == 0)
    {
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = 0;
      /* minus 1 here. ffmpeg parses the msb 0 of long start code as the last packet's payload for hevc bitstream (hevc_parse).
       * move 1 byte forward on all the pkt_offset so that frame_offset coming from fw can fall into the correct range. */
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = current_pkt_size - 1;
    }
    else
    {
      // cumulate sizes to correspond to FW offsets
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ];
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ] + current_pkt_size;

      //Wrapping 32 bits since FW send u32 wrapped values
      if (p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] > 0xFFFFFFFF)
      {
        p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] - (0x100000000);
        p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] + current_pkt_size;
      }
    }

    /* if this wrap-around pkt_offset_index spot is about to be overwritten, free the previous one. */
    free(p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ]);

    if (p_packet->p_custom_sei)
    {
      p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ] = malloc(p_packet->custom_sei_len);
      if (p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ])
      {
        memcpy(p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ], p_packet->p_custom_sei,
               p_packet->custom_sei_len);
        p_ctx->pkt_custom_sei_len[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->custom_sei_len;
      }
      else
      {
        /* warn and lose the sei data. */
        ni_log(NI_LOG_ERROR, "%s: failed to allocate custom SEI buffer for pkt.\n",
               __func__);
      }

      if (p_packet->no_slice)
      {
        /* this pkt contains custom sei data, but no slice data.
         * store it for the next pkt which contains slice data. */
        p_ctx->last_pkt_custom_sei = malloc(p_packet->custom_sei_len);
        if (p_ctx->last_pkt_custom_sei)
        {
          memcpy(p_ctx->last_pkt_custom_sei, p_packet->p_custom_sei, p_packet->custom_sei_len);
          p_ctx->last_pkt_custom_sei_len = p_packet->custom_sei_len;
        }
        else
        {
          /* warn and lose the sei data. */
          ni_log(NI_LOG_ERROR, "%s: failed to allocate custom SEI buffer for pkt.\n",
                 __func__);
        }
      }
      else
      {
        if (p_ctx->last_pkt_custom_sei)
        {
          /* this pkt contains slice data and sei data. lose the previous sei data. */
          free(p_ctx->last_pkt_custom_sei);
          p_ctx->last_pkt_custom_sei = NULL;
          p_ctx->last_pkt_custom_sei_len = 0;
        }
      }
    }
    else if (p_ctx->last_pkt_custom_sei)
    {
      /* last pkt contains sei data without slice data, this pkt does't contain sei data,
       * take the last stored pkt sei as this pkt's sei data
       * and insert into this pkt_offset_index spot. */
      p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->last_pkt_custom_sei;
      p_ctx->pkt_custom_sei_len[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->last_pkt_custom_sei_len;
      p_ctx->last_pkt_custom_sei = NULL;
      p_ctx->last_pkt_custom_sei_len = 0;
    }
    else
    {
      p_ctx->pkt_custom_sei[p_ctx->pkt_index % NI_FIFO_SZ] = NULL;
      p_ctx->pkt_custom_sei_len[p_ctx->pkt_index % NI_FIFO_SZ] = 0;
    }

    p_ctx->pkt_index ++;
  }

  retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_packet->dts, 0);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_write(): ni_timestamp_register() for dts returned %d\n", retval);
  }

  END;
  
  if (NI_RETCODE_SUCCESS == retval)
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_write(): exit: packets: %" PRIu64 " offset %" PRIx64 " sent_size = %u, available_space = %u, status=%d\n", p_ctx->pkt_num, (uint64_t)p_packet->pos, sent_size, inst_info.wr_buf_avail_size, p_ctx->status);

    return sent_size;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_write(): exit: returnErr: %d, p_ctx->status: %d\n", retval, p_ctx->status);
    return retval;
  }
}

static int64_t guess_correct_pts(ni_session_context_t* p_ctx, int64_t reordered_pts, int64_t dts)
{
  int64_t pts = NI_NOPTS_VALUE;
  if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_dts += dts <= p_ctx->pts_correction_last_dts;
    p_ctx->pts_correction_last_dts = dts;
  }
  else if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_dts = reordered_pts;
  }
  if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_pts += reordered_pts <= p_ctx->pts_correction_last_pts;
    p_ctx->pts_correction_last_pts = reordered_pts;
  }
  else if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_pts = dts;
  }
  if ((p_ctx->pts_correction_num_faulty_pts<=p_ctx->pts_correction_num_faulty_dts || dts == NI_NOPTS_VALUE)
     && reordered_pts != NI_NOPTS_VALUE)
  {
    pts = reordered_pts;
  }
  else
  {
    pts = dts;
  }
  //printf("here pts = %d\n", pts);
  return pts;
}
/*!******************************************************************************
 *  \brief  Retrieve a YUV p_frame from decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  ni_nvme_result_t nvme_result[WORKER_QUEUE_DEPTH];
  ni_nvme_command_t cmd[WORKER_QUEUE_DEPTH];
  queue_info decoder_read_workerqueue;
  ni_instance_mgr_stream_info_t data = { 0 };
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint16_t yuvW = 0;
  uint16_t yuvH = 0;
  uint8_t* p_data_buffer = (uint8_t*) p_frame->p_buffer;
  uint32_t data_buffer_size = p_frame->buffer_size;
  int i = 0;
  int retval = NI_RETCODE_SUCCESS;
  int metadata_hdr_size = NI_FW_META_DATA_SZ;
  int sei_size = 0;
  uint32_t totall_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t actual_read_size = 0;
  static long long decq_count = 0LL;
  int keep_processing = 1;
  ni_instance_status_info_t inst_info = { 0 };
  int query_retry = 0;
  int max_query_retries = (p_ctx->decoder_low_delay? (p_ctx->decoder_low_delay * 1000 / NI_RETRY_INTERVAL_200US + 1) : \
                           NI_MAX_DEC_SESSION_READ_QUERY_RETRIES);
  uint32_t ui32LBA = 0;
  p_ctx->decoder_read_processed_data_len = 0;
  worker_queue_item last_pkt_chunk_read = { 0 };
#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "ni_decoder_session_read(): enter\n");

  if ((!p_ctx) || (!p_frame))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR ni_decoder_session_read(): xcoder instance id < 0, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.
  
  totall_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] + 
  p_frame->data_len[2] + metadata_hdr_size;
  ni_log(NI_LOG_TRACE, "Total bytes to read %d \n",totall_bytes_to_read);
  while (1)
  {
    query_retry++;
    retval = ni_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_TRACE, "Info query inst_info.rd_buf_avail_size = %u\n",
           inst_info.rd_buf_avail_size);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "Warning dec read query fail rc %d retry %d\n",
                     retval, query_retry);

      if (query_retry >= 1000)
      {
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
      }
      usleep(NI_RETRY_INTERVAL_100US);  // 100 us
    }
    else if (inst_info.rd_buf_avail_size == metadata_hdr_size)
    {
      ni_log(NI_LOG_TRACE, "Info only metadata hdr is available, seq change?\n");
      totall_bytes_to_read = metadata_hdr_size;
      break;
    }
    else if (0 == inst_info.rd_buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        ni_log(NI_LOG_TRACE, "Info dec query, ready_to_close %u, ctx status %d,"
               " try %d\n", p_ctx->ready_to_close, p_ctx->status, query_retry);
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (data.is_flushed || query_retry >= NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)  // 15000 retries
        {
          ni_log(NI_LOG_DEBUG, "Info eos reached: is_flushed %u try %d.\n",
                 data.is_flushed, query_retry);
          if (query_retry >= NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   //15000 retries
          {
            ni_log(NI_LOG_INFO, "Info eos reached exceeding max retries: is_flushed %u try %d.\n",
                   data.is_flushed, query_retry);
          }
          p_frame->end_of_stream = 1;
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
        }
        else
        {
          ni_log(NI_LOG_TRACE, "Dec read available buf size == 0, query try %d,"
                 " retrying ..\n", query_retry);
          usleep(NI_RETRY_INTERVAL_200US);  // 200 us
          continue;
        }
      }

      ni_log(NI_LOG_TRACE, "Warning dec read available buf size == 0, eos %u  nb"
                     " try %d\n", p_frame->end_of_stream, query_retry);

      if (((NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) || 
           (p_ctx->frame_num < p_ctx->pkt_num && p_ctx->decoder_low_delay)) &&
          (query_retry < max_query_retries))
      {
        if ((NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) &&
            (inst_info.wr_buf_avail_size > p_ctx->required_buf_size))
        {
          ni_log(NI_LOG_TRACE, "Info dec write buffer is enough, available buf "
                 "size %u >= required size %u !\n",
                 inst_info.wr_buf_avail_size, p_ctx->required_buf_size);
          p_ctx->status = 0;
          p_ctx->required_buf_size = 0;
        }
        else
        {
          usleep(NI_RETRY_INTERVAL_200US);  // 200 us
          continue;
        }
      }
      else if (p_ctx->frame_num < p_ctx->pkt_num && p_ctx->decoder_low_delay)
      {
        ni_log(NI_LOG_INFO, "Warning: no frames from the decoder after exceeding retry limit,"
               "revert from decoder low delay mode to normal mode.\n");
        p_ctx->decoder_low_delay = 0;
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_TRACE, "Info dec YUV query, pic size %ux%u xfer frame size "
                       "%ux%u frame-rate %u is_flushed %u\n",
                       data.picture_width, data.picture_height,
                       data.transfer_frame_stride, data.transfer_frame_height,
                       data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;

        ni_log(NI_LOG_TRACE, "Info dec YUV, adjust frame size from %ux%u to "
                       "%ux%u\n", p_frame->video_width, p_frame->video_height,
                       p_ctx->active_video_width, p_ctx->active_video_height);

        ni_decoder_frame_buffer_free(p_frame);

        // set up decoder YUV frame buffer pool
        if (ni_dec_fme_buffer_pool_initialize(
              p_ctx, NI_DEC_FRAME_BUF_POOL_SIZE_INIT, p_ctx->active_video_width,
              p_ctx->active_video_height,
              p_ctx->codec_format == NI_CODEC_FORMAT_H264,
              p_ctx->bit_depth_factor))
        {
          ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_read(): Cannot allocate fme buf pool.\n");
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          ni_decoder_session_close(p_ctx, 0);
#ifdef XCODER_SELF_KILL_ERR
          // if need to terminate at such occasion when continuing is not
          // possible, trigger a codec closure
          ni_log(NI_LOG_ERROR, "Terminating due to persistent failures\n");
          kill(getpid(), SIGTERM);
#endif
          LRETURN;
        }

        retval = ni_decoder_frame_buffer_alloc(
          p_ctx->dec_fme_buf_pool, p_frame, 1, // get mem buffer
          p_ctx->active_video_width, p_ctx->active_video_height,
          p_ctx->codec_format == NI_CODEC_FORMAT_H264,
          p_ctx->bit_depth_factor);

        if (NI_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        totall_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] + 
        p_frame->data_len[2] + metadata_hdr_size;
        p_data_buffer = (uint8_t*) p_frame->p_buffer;

        // make sure we don't read more than available
        ni_log(NI_LOG_TRACE, "Info dec buf size: %u YUV frame + meta-hdr size: %u "
                       "available: %u\n", p_frame->buffer_size,
                       totall_bytes_to_read, inst_info.rd_buf_avail_size);
      }
      break;
    }
  }
  unsigned int bytes_read_so_far = 0;

#ifdef XCODER_IO_RW_ENABLED
    ni_log(NI_LOG_TRACE, "totall_bytes_to_read %d max_nvme_io_size %d ylen %d cr len %d cb len %d hdr %d\n", \
                    totall_bytes_to_read, \
                    p_ctx->max_nvme_io_size, \
                    p_frame->data_len[0], \
                    p_frame->data_len[1], \
                    p_frame->data_len[2], \
                    metadata_hdr_size);
  
    if (inst_info.rd_buf_avail_size < totall_bytes_to_read || inst_info.rd_buf_avail_size > p_frame->buffer_size)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_read() avaliable size(%u)"
             "less than needed (%u), or more than buffer allocated (%u)\n",
             inst_info.rd_buf_avail_size, totall_bytes_to_read, p_frame->buffer_size);
      ni_assert(0);
    }
    read_size_bytes = inst_info.rd_buf_avail_size;
    ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
    if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
    {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data_buffer, read_size_bytes, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_decoder_session_read(): nvme command failed\n");
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    else
    {
      // command issued successfully, now exit
  
      ni_metadata_dec_frame_t* p_meta = 
      (ni_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer
      + p_frame->data_len[0] + p_frame->data_len[1] 
      + p_frame->data_len[2]);
      p_ctx->codec_total_ticks += p_meta->frame_cycle;

      if (inst_info.rd_buf_avail_size != metadata_hdr_size)
      {
        sei_size = p_meta->sei_size;
      }
      totall_bytes_to_read = totall_bytes_to_read + sei_size;
      p_ctx->decoder_read_total_num_of_bytes = totall_bytes_to_read;
      ni_log(NI_LOG_TRACE, "decoder read success, size %d totall_bytes_to_read include sei %d sei_size %d frame_cycle %d\n", 
             retval, totall_bytes_to_read, sei_size, p_meta->frame_cycle);
    }
  
#else //XCODER_IO_RW_ENABLED

  p_ctx->decoder_read_total_num_of_chunks = totall_bytes_to_read/(p_ctx->max_nvme_io_size);
  if (totall_bytes_to_read%(p_ctx->max_nvme_io_size))
  {
      p_ctx->decoder_read_total_num_of_chunks++;
  }

  uint32_t frame_chunk_counter = 0;
  ni_log(NI_LOG_TRACE, "total_num_of_chunksdr %d totall_bytes_to_read %d max_nvme_io_size %d ylen %d cr len %d cb len %d hdr %d\n", \
                  p_ctx->encoder_read_total_num_of_chunks, \
                  totall_bytes_to_read, \
                  p_ctx->max_nvme_io_size, \
                  p_frame->data_len[0], \
                  p_frame->data_len[1], \
                  p_frame->data_len[2], \
                  metadata_hdr_size);


  {
    actual_read_size = read_size_bytes = (totall_bytes_to_read <= p_ctx->max_nvme_io_size) ? totall_bytes_to_read : p_ctx->max_nvme_io_size;

    //READ_INSTANCE_SET_DW15_SIZE(cmd.cdw15, read_size_bytes);

    if (read_size_bytes < p_ctx->max_nvme_io_size)
    {
      if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
      {
        read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
      }
    }

    // Now we enqueu all the decoder chunks except the last chunk, that's why minus 1
    int32_t frame_remain_bytes = totall_bytes_to_read;
    p_ctx->decoder_read_total_num_of_bytes = totall_bytes_to_read;

    int32_t one_chunk_flag = false;
     // if there is only one chunk, we do not enqueue anything
    ni_log(NI_LOG_TRACE, "frame_remain_bytes %d,  max_nvme_io_size %d\n",frame_remain_bytes , p_ctx->max_nvme_io_size);
    if (frame_remain_bytes<=p_ctx->max_nvme_io_size){
      one_chunk_flag = true;
      assert(p_ctx->decoder_read_total_num_of_chunks < 2);
    }
    if (!one_chunk_flag)
    {
      for(;frame_chunk_counter < p_ctx->decoder_read_total_num_of_chunks -1;frame_chunk_counter++)
      {
        p_ctx->args_decoder_read[frame_chunk_counter].opcode = nvme_cmd_xcoder_read;
        p_ctx->args_decoder_read[frame_chunk_counter].handle = p_ctx->blk_io_handle;
        p_ctx->args_decoder_read[frame_chunk_counter].p_ni_nvme_cmd = &(cmd[frame_chunk_counter]);
        p_ctx->args_decoder_read[frame_chunk_counter].data_len = (frame_remain_bytes < p_ctx->max_nvme_io_size) ? (frame_remain_bytes) : (p_ctx->max_nvme_io_size);
        p_ctx->args_decoder_read[frame_chunk_counter].p_data = p_data_buffer;
        p_ctx->args_decoder_read[frame_chunk_counter].p_result = &(nvme_result[frame_chunk_counter]);
        p_ctx->args_decoder_read[frame_chunk_counter].frame_chunk_index = ((frame_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE);
        p_ctx->args_decoder_read[frame_chunk_counter].frame_chunk_size = p_ctx->max_nvme_io_size;
        p_ctx->args_decoder_read[frame_chunk_counter].session_id = p_ctx->session_id;
        frame_remain_bytes = frame_remain_bytes - p_ctx->args_decoder_read[frame_chunk_counter].data_len;
        ni_log(NI_LOG_TRACE, "Decoder read frame_chunk_counter %d totall_bytes_to_read %d p_ctx->decoder_read_total_num_of_chunks %d chunk index %d frame_remain_bytes %d\n", \
                        frame_chunk_counter, \
                        totall_bytes_to_read, \
                        p_ctx->decoder_read_total_num_of_chunks, \
                        p_ctx->args_decoder_read[frame_chunk_counter].frame_chunk_index, \
                        frame_remain_bytes);
        while(is_worker_q_full(&(p_ctx->decoder_read_workerqueue))) // The queue should not full otherwise assert
        {
          usleep(100);
          continue;
        } // we still have some space in the queue so enque the argment
        {
          worker_q_enq(&(p_ctx->decoder_read_workerqueue), p_ctx->args_decoder_read[frame_chunk_counter]);
        }
      }
    }
    ni_log(NI_LOG_TRACE, "Send the last chunk frame_chunk_counter %d\n",frame_chunk_counter);
    // Nowe send the last chunk first and make sure it returned, due to FW limitation last chunk needs to go first
    // assert if the last chunk is bigger than the max nvme io size
    assert(frame_remain_bytes<=p_ctx->max_nvme_io_size);
    last_pkt_chunk_read.opcode = nvme_cmd_xcoder_read;
    last_pkt_chunk_read.handle =  p_ctx->blk_io_handle;
    last_pkt_chunk_read.p_ni_nvme_cmd = &(cmd[frame_chunk_counter]);
    last_pkt_chunk_read.data_len =  frame_remain_bytes + (inst_info.rd_buf_avail_size - totall_bytes_to_read);
    last_pkt_chunk_read.p_data = p_data_buffer;
    last_pkt_chunk_read.p_result =  &(nvme_result[frame_chunk_counter]);
    last_pkt_chunk_read.frame_chunk_index = ((frame_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE);
    last_pkt_chunk_read.frame_chunk_size = p_ctx->max_nvme_io_size;
    last_pkt_chunk_read.session_id = p_ctx->session_id;

    WRITE_INSTANCE_SET_DW10_SUBTYPE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw10, last_pkt_chunk_read.session_id);
    WRITE_INSTANCE_SET_DW11_INSTANCE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_DECODER);
    WRITE_INSTANCE_SET_DW11_PAGEOFFSET(last_pkt_chunk_read.p_ni_nvme_cmd->cdw11, last_pkt_chunk_read.frame_chunk_index);  // set page index based on 4k
    WRITE_INSTANCE_SET_DW15_SIZE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw15, last_pkt_chunk_read.data_len);  // this one we read all the way end to the buffer

    int32_t ret_last = -1;
    int32_t retry_count = 0;
    uint32_t data_len_local = last_pkt_chunk_read.data_len;
    while(ret_last != 0){
      ni_log(NI_LOG_TRACE, "decoder_rread opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x,  data_len %d, pdataoff %p \n",\
                    last_pkt_chunk_read.opcode, \
                    last_pkt_chunk_read.handle, \
                    last_pkt_chunk_read.p_ni_nvme_cmd->cdw10, \
                    last_pkt_chunk_read.p_ni_nvme_cmd->cdw11, \
                    last_pkt_chunk_read.p_ni_nvme_cmd->cdw15, \
                    last_pkt_chunk_read.data_len, \
                    ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE)
                    );
      if (last_pkt_chunk_read.data_len % NI_MEM_PAGE_ALIGNMENT)
      {
        last_pkt_chunk_read.data_len = ( (last_pkt_chunk_read.data_len / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
      }
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
      ret_last = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_read, \
                                    last_pkt_chunk_read.handle, \
                                    last_pkt_chunk_read.p_ni_nvme_cmd, \
                                    last_pkt_chunk_read.data_len, \
                                    ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), \
                                    last_pkt_chunk_read.p_result);
#else
      ret_last = ni_nvme_send_io_cmd(last_pkt_chunk_read.opcode, \
                                    last_pkt_chunk_read.handle, \
                                    last_pkt_chunk_read.p_ni_nvme_cmd, \
                                    last_pkt_chunk_read.data_len, \
                                    ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), \
                                    last_pkt_chunk_read.p_result);
#endif

      if (ret_last != NI_RETCODE_SUCCESS)
      {
        retval = ret_last;
        CHECK_VPU_RECOVERY(retval);

        ni_log(NI_LOG_TRACE, "decoder read failed retry\n");
        *(last_pkt_chunk_read.p_result) = 0;
        retry_count++;
        usleep(100);
        if (retry_count == 6000)
        {
          ni_log(NI_LOG_TRACE, "Something is wrong as the decoder read command retried 6000 times, now assert\n");
          assert(false);
        }
        continue;
      }
      else
      {
        // command issued successfully, now exit
        
        ni_metadata_dec_frame_t* p_meta = 
        (ni_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer
        + p_frame->data_len[0] + p_frame->data_len[1] 
        + p_frame->data_len[2]);
        p_ctx->codec_total_ticks += p_meta->frame_cycle;
        
        if (inst_info.rd_buf_avail_size != metadata_hdr_size)
        {
          sei_size = p_meta->sei_size;
        }
        *(last_pkt_chunk_read.p_result) =  data_len_local;
        totall_bytes_to_read = totall_bytes_to_read + sei_size;
        p_ctx->decoder_read_total_num_of_bytes = p_ctx->decoder_read_total_num_of_bytes + sei_size;
        ni_log(NI_LOG_TRACE, "decoder read success, size %d local %d totall_bytes_to_read include sei %d sei_size %d frame_cycle %d\n", 
               *(last_pkt_chunk_read.p_result), data_len_local, totall_bytes_to_read, sei_size, p_meta->frame_cycle);
        break;
      }
    }
    p_ctx->decoder_read_processed_data_len +=*(last_pkt_chunk_read.p_result);

    if (!one_chunk_flag){
      ni_log(NI_LOG_TRACE, "Totally_decoder_read enqueued %d\n", frame_chunk_counter);
      // After the queue is filled up, we release the worker threads
      // Here we need to pull untill the queue is empty,
      // There is a mutex to protect each queue check
      int num_of_thread_to_release = frame_chunk_counter-1;
      while((num_of_thread_to_release >= 0)){
        // Looks like we have workers avaiable and there some work to do,
        // So compete to lock the mutex
        ni_log(NI_LOG_TRACE, "Release one thread for decoder read ntr %d\n", num_of_thread_to_release);
        sem_post(p_ctx->decoder_read_semaphore);
        ni_log(NI_LOG_TRACE, "Finish release one thread decoder read num_of_thread_to_release %d\n", num_of_thread_to_release);
        num_of_thread_to_release--;
      }

      // here we waif for one worker thread to notify the main thread that all the chunks had been sent out
      pthread_mutex_lock(p_ctx->decoder_read_mutex_len);
      while(totall_bytes_to_read != p_ctx->decoder_read_processed_data_len){
        pthread_cond_wait(p_ctx->decoder_read_cond, p_ctx->decoder_read_mutex_len);
      }
      pthread_mutex_unlock(p_ctx->decoder_read_mutex_len);

      ni_log(NI_LOG_TRACE, "No more decoder read ongoing progressed bytes %d total len %d frame num %lu\n", p_ctx->decoder_read_processed_data_len, totall_bytes_to_read, p_ctx->frame_num);
    }
  }
#endif //XCODER_IO_RW_ENABLED

  bytes_read_so_far = totall_bytes_to_read ;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  rx_size = ni_create_frame(p_frame, bytes_read_so_far, &frame_offset);

  if (rx_size > 0)
  {
    ni_log(NI_LOG_TRACE, "xcoder_dec_receive(): s-state %d first_frame %d\n", p_ctx->session_run_state, p_ctx->is_first_frame);
    if (ni_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)& p_frame->dts, XCODER_FRAME_OFFSET_DIFF_THRES, (decq_count % 500 == 0), p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
    {
      p_frame->dts = NI_NOPTS_VALUE;
    }

    int64_t pts_delta = 0;
    if (p_ctx->is_dec_pkt_512_aligned)
    {
      if (p_ctx->is_first_frame)
      {
        p_ctx->is_first_frame = 0;

        if (p_frame->dts == NI_NOPTS_VALUE)
        {
          p_frame->pts = NI_NOPTS_VALUE;
          p_ctx->last_dts = p_ctx->last_pts = NI_NOPTS_VALUE;
        }
        // if not a bitstream retrieve the pts of the frame corresponding to the first YUV output
        else if ((p_ctx->pts_offsets[0] != NI_NOPTS_VALUE) && (p_ctx->pkt_index != -1))
        {
          int idx = 0;
          int cumul = p_ctx->pkt_offsets_index[0];
          while (cumul < frame_offset) // look for pts index
          {
            if (idx == NI_MAX_DEC_REJECT)
            {
              ni_log(NI_LOG_INFO, "Invalid index computation oversizing NI_MAX_DEC_REJECT! \n");
              break;
            }
            else
            {
              cumul += p_ctx->pkt_offsets_index[idx];
              idx ++;
            }
          }
          if ((idx != NI_MAX_DEC_REJECT) && (idx > 0))
          {
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_ctx->last_pts = p_ctx->pts_offsets[idx - 1];
            p_ctx->last_dts = p_frame->dts;
          }
          else if (p_ctx->session_run_state == SESSION_RUN_STATE_RESETTING)
          {
            ni_log(NI_LOG_TRACE, "xcoder_dec_receive(): session %d recovering and "
                                 "adjusting ts.\n", p_ctx->session_id);
            p_frame->pts = p_ctx->pts_offsets[idx];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
            p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
          }
          else // use pts = 0 as offset
          {
            p_frame->pts = 0;
            p_ctx->last_pts = 0;
            p_ctx->last_dts = p_frame->dts;
          }
        }
        else
        {
          p_frame->pts = 0;
          p_ctx->last_pts = 0;
          p_ctx->last_dts = p_frame->dts;
        }
      }
      else
      {
        pts_delta = p_frame->dts - p_ctx->last_dts;
        p_frame->pts = p_ctx->last_pts + pts_delta;

        p_ctx->last_pts = p_frame->pts;
        p_ctx->last_dts = p_frame->dts;
      }
    }
    else
    {
      //ignore timestamps if bitstream
      if (p_frame->dts == NI_NOPTS_VALUE)
      {
        p_frame->pts = NI_NOPTS_VALUE;
        p_ctx->last_dts = p_ctx->last_pts = NI_NOPTS_VALUE;
      }
      else
      {
        if (p_ctx->is_first_frame)
        {
          p_ctx->is_first_frame = 0;
        }
        if (frame_offset == 0)
        {
          if (p_ctx->pts_offsets[0] == NI_NOPTS_VALUE)
          {
            p_frame->pts = NI_NOPTS_VALUE;
            p_ctx->last_dts = p_ctx->last_pts = NI_NOPTS_VALUE;
          }
          else
          {
            p_frame->pts = p_ctx->pts_offsets[0];
            p_ctx->last_pts = p_frame->pts;
            p_ctx->last_dts = p_frame->dts;
          }

          if (p_ctx->pkt_custom_sei[0])
          {
            p_frame->p_custom_sei = p_ctx->pkt_custom_sei[0];
            p_frame->custom_sei_len = p_ctx->pkt_custom_sei_len[0];
            p_ctx->pkt_custom_sei[0] = NULL;
            p_ctx->pkt_custom_sei_len[0] = 0;
          }
        }
        else
        {
          for (i = 0 ; i < NI_FIFO_SZ ; i++)
          {
            if ((frame_offset >= p_ctx->pkt_offsets_index_min[i])
                &&(frame_offset  < p_ctx->pkt_offsets_index[i]))
            {
              if (p_ctx->pts_offsets[i] == NI_NOPTS_VALUE)
              {
                //bitstream case
                p_frame->pts = NI_NOPTS_VALUE;
                p_ctx->last_dts = p_ctx->last_pts = NI_NOPTS_VALUE;
              }
              else
              {
                p_frame->pts = p_ctx->pts_offsets[i];
                p_ctx->last_pts = p_frame->pts;
                p_ctx->last_dts = p_frame->dts;
              }

              if (p_ctx->pkt_custom_sei[i % NI_FIFO_SZ])
              {
                p_frame->p_custom_sei = p_ctx->pkt_custom_sei[i % NI_FIFO_SZ];
                p_frame->custom_sei_len = p_ctx->pkt_custom_sei_len[i % NI_FIFO_SZ];
                p_ctx->pkt_custom_sei[i % NI_FIFO_SZ] = NULL;
                p_ctx->pkt_custom_sei_len[i % NI_FIFO_SZ] = 0;
              }
              break;
            }
            if (i == (NI_FIFO_SZ-1))
            {
              //backup solution pts
              p_frame->pts = p_ctx->last_pts + (p_frame->dts - p_ctx->last_dts);
              p_ctx->last_pts = p_frame->pts;
              p_ctx->last_dts = p_frame->dts;
              ni_log(NI_LOG_ERROR, "ERROR: NO pts found consider increasing NI_FIFO_SZ!\n");
            }
          }
        }
      }
    }
    int64_t best_effort_timestamp = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts);
    p_frame->pts = best_effort_timestamp;
    decq_count++;
    p_ctx->frame_num++;

#ifdef XCODER_DUMP_DATA
    char dump_file[128];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-dec-fme/fme-%04ld.yuv",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);
    FILE *f = fopen(dump_file, "wb");
    fwrite(p_frame->p_buffer, p_frame->data_len[0] + p_frame->data_len[1] +
           p_frame->data_len[2], 1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log(NI_LOG_TRACE, "xcoder_dec_receive(): received data: [0x%08x]\n", rx_size);
  ni_log(NI_LOG_TRACE, "xcoder_dec_receive(): p_frame->start_of_stream=%d, p_frame->end_of_stream=%d, p_frame->video_width=%d, p_frame->video_height=%d\n", p_frame->start_of_stream, p_frame->end_of_stream, p_frame->video_width, p_frame->video_height);
  ni_log(NI_LOG_TRACE, "xcoder_dec_receive(): p_frame->data_len[0/1/2]=%d/%d/%d\n", p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (decq_count % 500 == 0)
  {
    ni_log(NI_LOG_TRACE, "Decoder pts queue size = %d  dts queue size = %d\n\n",
      ((ni_timestamp_table_t*)p_ctx->pts_table)->list.count,
      ((ni_timestamp_table_t*)p_ctx->dts_queue)->list.count);
    // scan and clean up
    ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue, p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
      abs_time_ns = ni_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,dLAT:%lu;\n", p_frame->dts, abs_time_ns - p_ctx->prev_read_frame_time, ni_lat_meas_q_check_latency(p_ctx->frame_time_q, abs_time_ns, p_frame->dts));
      p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif

  END;

  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_read(): bad exit, retval = %d\n",retval);
    return retval;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_decoder_session_read(): exit, rx_size = %d\n",rx_size);                       
    return rx_size;
  }
}

/*!******************************************************************************
 *  \brief  Query current decoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_query(ni_session_context_t* p_ctx)
{
  ni_instance_mgr_general_status_t data;
  int retval = NI_RETCODE_SUCCESS;
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_decoder_session_query() passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  ni_log(NI_LOG_TRACE, "ni_decoder_session_query(): enter\n");
  retval = ni_query_general_status(p_ctx, NI_DEVICE_TYPE_DECODER, &data);

  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->load_query.current_load = (uint32_t)data.process_load_percent;
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.total_contexts = (uint32_t)data.active_sub_instances_cnt;
    ni_log(NI_LOG_TRACE, "ni_decoder_session_query current_load:%d fw_model_load:%d fw_video_mem_usage:%d active_contexts %d\n", p_ctx->load_query.current_load, p_ctx->load_query.fw_model_load, p_ctx->load_query.fw_video_mem_usage, p_ctx->load_query.total_contexts);
  }

  END;

  return retval;
}

/*!******************************************************************************
 *  \brief  Open a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_open(ni_session_context_t* p_ctx)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t buffer_size = 0;
  void *p_signature = NULL;
  ni_encoder_params_t* p_cfg = (ni_encoder_params_t*)p_ctx->p_session_config;
  uint32_t model_load = 0;
  ni_instance_status_info_t inst_info = { 0 };
  void* p_buffer = NULL;
  ni_encoder_session_open_info_t session_info = { 0 };
  int query_retry = 0;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): enter\n");

  if ((!p_ctx) || (!p_cfg))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_open(): NULL pointer p_config passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //thread mutex/cond and semaphore init
  if (!pthread_mutex_alloc_and_init(&p_ctx->dts_queue_mutex))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc dts_queue_mutex fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
#ifndef XCODER_IO_RW_ENABLED //No need background RW if XCODER_IO_RW_ENABLED
  if (!pthread_mutex_alloc_and_init(&p_ctx->encoder_write_mutex))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_write_mutex fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!pthread_mutex_alloc_and_init(&p_ctx->encoder_write_mutex_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_write_mutex_len fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!pthread_mutex_alloc_and_init(&p_ctx->encoder_read_mutex))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_read_mutex fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!pthread_mutex_alloc_and_init(&p_ctx->encoder_read_mutex_len))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_read_mutex_len fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  if (!pthread_cond_alloc_and_init(&p_ctx->encoder_write_cond))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_write_cond fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!pthread_cond_alloc_and_init(&p_ctx->encoder_read_cond))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_read_cond fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  if (!sem_alloc_and_init(&p_ctx->encoder_write_semaphore))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_write_semaphore fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  if (!sem_alloc_and_init(&p_ctx->encoder_read_sem))
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): alloc encoder_read_sem fail!, return\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  p_ctx->close_encoder_read_thread = false;
  p_ctx->close_encoder_write_thread = false;
  worker_q_init(&(p_ctx->encoder_write_workerqueue), p_ctx->worker_queue_encoder_write); // init the queue with encoder write stack, reset all the head and tail
  worker_q_init(&(p_ctx->encoder_read_workerqueue), p_ctx->worker_queue_encoder_read); // init the queue with encoder read stack, reset all the head and tail
#endif

  ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): finish init\n");

#ifndef XCODER_IO_RW_ENABLED //No need background RW if XCODER_IO_RW_ENABLED
  // Here we create a worker thread pool
  int counter = 0;
  for(counter = 0; counter<MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    ni_log(NI_LOG_TRACE, "create thread read %d\n",counter);
    if (pthread_create(&(p_ctx->ThreadID_encoder_read[counter]), NULL, workerthread_encoder_read, (void*)(p_ctx)))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_encoder_session_open failed to create encoder read threads !\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    ni_log(NI_LOG_TRACE, "create thread write %d\n",counter);
    if (pthread_create(&(p_ctx->ThreadID_encoder_write[counter]), NULL, workerthread_encoder_write, (void*)p_ctx))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_encoder_session_open failed to create encoder write threads !\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
  }
  ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): finish create\n");
#endif
  
  if ((!p_ctx) || (!p_cfg))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_open(): NULL pointer p_config passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Check if there is an instance or we need a new one
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    int i;
    p_ctx->device_type = NI_DEVICE_TYPE_ENCODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->keyframe_factor = 1;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->force_frame_type = 0;
    p_ctx->required_buf_size = 0;
    p_ctx->ready_to_close = 0;
    // Sequence change tracking related stuff
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->enc_pts_w_idx = 0;
    p_ctx->enc_pts_r_idx = 0;
    p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
    p_ctx->codec_total_ticks = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->p_dec_packet_inf_buf = NULL;
    p_ctx->session_timestamp = 0;

    for (i = 0; i < NI_FIFO_SZ; i++)
    {
      p_ctx->pkt_custom_sei[i] = NULL;
      p_ctx->pkt_custom_sei_len[i] = 0;
    }
    p_ctx->last_pkt_custom_sei = NULL;
    p_ctx->last_pkt_custom_sei_len = 0;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    p_ctx->codec_start_time = tv.tv_sec*1000000ULL + tv.tv_usec;

#ifdef _WIN32
    p_ctx->event_handle = ni_create_event();
    if (p_ctx->event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }

    p_ctx->thread_event_handle = ni_create_event();
    if (p_ctx->thread_event_handle == NI_INVALID_EVENT_HANDLE)
    {
      retval = NI_RETCODE_ERROR_INVALID_HANDLE;
      LRETURN;
    }
#endif

    memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));
    if (p_ctx->p_session_config !=NULL )
    {
      model_load = ((ni_encoder_params_t*)(p_ctx->p_session_config))->source_width *
                   ((ni_encoder_params_t*)(p_ctx->p_session_config))->source_height *
                   (((ni_encoder_params_t*)(p_ctx->p_session_config))->hevc_enc_params).frame_rate;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "Session config should not be NULL at this point\n");
      assert(false);
    }

    ni_log(NI_LOG_TRACE, "Model load info: Width:%d Height:%d FPS:%d Load %d\n",
                  ((ni_encoder_params_t*)(p_ctx->p_session_config))->source_width,
                  ((ni_encoder_params_t*)(p_ctx->p_session_config))->source_height,
                  (((ni_encoder_params_t*)(p_ctx->p_session_config))->hevc_enc_params).frame_rate,
                  model_load);

#ifdef XCODER_IO_RW_ENABLED
    //malloc zero data buffer
    if(posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_encoder_session_open() alloc all zero buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if(posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
      ni_log(NI_LOG_ERROR, "ERROR %d: ni_encoder_session_open() alloc data buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    // Get session ID
    ui32LBA = OPEN_GET_SID_R(NI_DEVICE_TYPE_ENCODER);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): nvme read command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    p_ctx->session_id = ((ni_get_session_id_t *)p_buffer)->session_id;
    ni_log(NI_LOG_TRACE, "Encoder open session ID:0x%x\n",p_ctx->session_id);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): query session ID failed, p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      ni_encoder_session_close(p_ctx, 0);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }

    //Send session Info
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    session_info.codec_format = ni_htonl(p_ctx->codec_format);
    session_info.i32picWidth = ni_htonl(p_cfg->source_width);
    session_info.i32picHeight = ni_htonl(p_cfg->source_height);
    session_info.model_load = ni_htonl(model_load);
#ifdef ENCODER_SYNC_QUERY //enable with parameter "-q"
    if (((ni_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode)
    {
      //In low latency mode, encoder read packet will just send query command one time. Set 1 to notify the FW.
      ni_log(NI_LOG_TRACE, "Low latency mode support encoder read sync query\n");
      session_info.EncoderReadSyncQuery = ni_htonl(0x01);
    }
    else
    {
      session_info.EncoderReadSyncQuery = ni_htonl(0x00);
    }
#else
    session_info.EncoderReadSyncQuery = ni_htonl(0x00);
#endif
    memcpy(p_buffer, &session_info, sizeof(ni_encoder_session_open_info_t));

    ui32LBA = OPEN_SESSION_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_open,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): nvme write command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
    ni_log(NI_LOG_TRACE, "Open session completed\n");

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout = p_ctx->keep_alive_timeout * 1000000;  //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
		                            p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_open(): nvme write keep_alive_timeout command failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }
#else //XCODER_IO_RW_ENABLED

    CREATE_SESSION_SET_DW10_SUBTYPE(cmd.cdw10);
    CREATE_SESSION_SET_DW11_INSTANCE(cmd.cdw11, NI_DEVICE_TYPE_ENCODER);
    CREATE_SESSION_SET_DW12_ENC_CID_FRWIDTH(cmd.cdw12, p_ctx->codec_format, p_cfg->source_width);
    CREATE_SESSION_SET_DW13_ENC_FRHIGHT(cmd.cdw13, p_cfg->source_height);
    CREATE_SESSION_SET_DW14_MODEL_LOAD(cmd.cdw14, model_load);

#ifdef XCODER_SIGNATURE_FILE
    CREATE_SESSION_SET_DW15_SIZE(cmd.cdw15, NI_SIGNATURE_SIZE);

    buffer_size = NI_SIGNATURE_SIZE;
    buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
    if (posix_memalign(&p_signature, sysconf(_SC_PAGESIZE), buffer_size))
    {
      ni_log(NI_LOG_ERROR, "ERROR: Cannot allocate signature buffer.\n");
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
    }

    memset((uint8_t*)p_signature, 0, buffer_size);
    memcpy((uint8_t*)p_signature, ni_session_sign,
           ni_session_sign_len < NI_SIGNATURE_SIZE ?
           ni_session_sign_len : NI_SIGNATURE_SIZE);
#endif

    if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_open, p_ctx->device_handle, &cmd, buffer_size, p_signature, &p_ctx->session_id))
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_admin_cmd failed: device_handle: %" PRIx64 ", hw_id, %d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
    }

    //Flip the bytes
    p_ctx->session_id = ni_ntohl(p_ctx->session_id);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log(NI_LOG_TRACE, "ERROR: no InstanceID received, ni_encoder_session_open(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
      ni_encoder_session_close(p_ctx, 0);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
    }
#endif //XCODER_IO_RW_ENABLED
  }

  retval = ni_config_instance_set_encoder_params(p_ctx);

  if (NI_RETCODE_ERROR_VPU_RECOVERY == retval)
  {
    ni_log(NI_LOG_DEBUG, "Warning: ni_config_instance_set_encoder_params() vpu recovery\n");
    LRETURN;
  }
  else if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_ERROR, "ERROR: calling ni_config_instance_set_encoder_params(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    ni_encoder_session_close(p_ctx, 0);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ni_log(NI_LOG_TRACE, "Encoder params sent\n");

  ni_timestamp_init(p_ctx, (ni_timestamp_table_t * *)& p_ctx->pts_table, "enc_pts");
  ni_timestamp_init(p_ctx, (ni_timestamp_table_t * *)& p_ctx->dts_queue, "enc_dts");

  // init close caption SEI header and trailer
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_hevc, g_itu_t_t35_cc_sei_hdr_hevc,
         NI_CC_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_h264, g_itu_t_t35_cc_sei_hdr_h264,
         NI_CC_SEI_HDR_H264_LEN);
  memcpy(p_ctx->sei_trailer, g_sei_trailer, NI_CC_SEI_TRAILER_LEN);
  // init hdr10+ SEI header
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_hevc, g_itu_t_t35_hdr10p_sei_hdr_hevc,
         NI_HDR10P_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_h264, g_itu_t_t35_hdr10p_sei_hdr_h264,
         NI_HDR10P_SEI_HDR_H264_LEN);

  // query to check the final encoder config status
  while (1)
  {
    query_retry++;
    retval = ni_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);

    if (inst_info.sess_err_no ||
        NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT == inst_info.inst_err_no)
    {
      ni_log(NI_LOG_ERROR, "ERROR: session error %u or VPU_RSRC_INSUFFICIENT\n",
             inst_info.sess_err_no);
      retval = NI_RETCODE_FAILURE;
      LRETURN;
    }
    else if (inst_info.wr_buf_avail_size > 0)
    {
      ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): wr_buf_avail_size %u\n",
             inst_info.wr_buf_avail_size);
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "ni_query_status_info ret %d, sess_err_no %u "
             "inst_err_no %u inst_info.wr_buf_avail_size %d retry ..\n",
             retval, inst_info.sess_err_no, inst_info.inst_err_no,
             inst_info.wr_buf_avail_size);
      if (query_retry > NI_MAX_ENC_SESSION_OPEN_QUERY_RETRIES)  // 3000 retries
      {
        ni_log(NI_LOG_ERROR, "ERROR: ni_encoder_session_open timeout\n");
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
      usleep(NI_ENC_SESSION_OPEN_RETRY_INTERVAL_US);  // 1000us
    }
  }

  ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
    "p_ctx->session_id=%d\n",
    (int64_t)p_ctx->device_handle,
    p_ctx->hw_id, p_ctx->session_id);

#ifdef XCODER_DUMP_DATA
  char dir_name[128] = { 0 };
  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-pkt", (long)getpid(),
           p_ctx->session_id);
  DIR* dir = opendir(dir_name);
  if (! dir && ENOENT == errno)
  {
    mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
    ni_log(NI_LOG_INFO, "Encoder pkt dump dir created: %s\n", dir_name);
  }

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-fme", (long)getpid(),
           p_ctx->session_id);
  dir = opendir(dir_name);
  if (! dir && ENOENT == errno)
  {
    mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
    ni_log(NI_LOG_INFO, "Encoder frame dump dir created: %s\n", dir_name);
  }
#endif

  END;

  aligned_free(p_signature);
  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_encoder_session_open(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Flush encoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_flush(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log(NI_LOG_TRACE, "ni_encoder_session_flush(): enter\n");
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: session context is null, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session id, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_config_instance_eos(), return\n");
  }
  
  END;
  
  ni_log(NI_LOG_TRACE, "ni_encoder_session_flush(): success exit\n");
  
  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int counter = 0; 
  int ret=0;
  int i;

  ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): exit\n");
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef XCODER_IO_RW_ENABLED
  pthread_mutex_free_and_destroy(&p_ctx->dts_queue_mutex);
#else
  // Here we close all session read/write threads
  p_ctx->close_encoder_write_thread = true; //set 1 to close encoder worker write thread
  p_ctx->close_encoder_read_thread = true; //set 1 to close encoder worker read thread
  for (counter = 0; counter< MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    if (p_ctx->ThreadID_encoder_write[counter])
    {
      sem_post(p_ctx->encoder_write_semaphore);
    }
    if (p_ctx->ThreadID_encoder_read[counter])
    {
      sem_post(p_ctx->encoder_read_sem);
    }
  }
  for (counter = 0; counter< MAX_NUM_OF_THREADS_PER_FRAME; counter++)
  {
    if (p_ctx->ThreadID_encoder_write[counter])
    {
      ret = pthread_join(p_ctx->ThreadID_encoder_write[counter],NULL);
      if (ret)
      {
        ni_log(NI_LOG_ERROR, "ERROR %d: encoder write thread cancle wait finished failed:%d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, ret);
        ni_log(NI_LOG_TRACE, "Count:%d, ThreadID:%d\n",counter, (uint32_t)p_ctx->ThreadID_encoder_write[counter]);
        retval = NI_RETCODE_FAILURE;
      }
      p_ctx->ThreadID_encoder_write[counter] =  0;
      ni_log(NI_LOG_TRACE, "Cancel encoder write thread-%d: %d", counter,p_ctx->session_id);
    }
    if (p_ctx->ThreadID_encoder_read[counter])
    {
      ret = pthread_join(p_ctx->ThreadID_encoder_read[counter],NULL);
      if (ret)
      {
        ni_log(NI_LOG_ERROR, "ERROR %d: encoder read thread cancle wait finished failed:%d\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR, ret);
        ni_log(NI_LOG_TRACE, "Count:%d, ThreadID:%d\n",counter, (uint32_t)p_ctx->ThreadID_encoder_read[counter]);
        retval = NI_RETCODE_FAILURE;
      }
      p_ctx->ThreadID_encoder_read[counter] = 0;
      ni_log(NI_LOG_TRACE, "Cancel encoder read thread-%d: %d\n", counter,p_ctx->session_id);
    }
  }
  p_ctx->close_encoder_write_thread = false;
  p_ctx->close_encoder_read_thread = false;

  //pthread mutex/cond and semaphore free&destroy
  sem_free_and_destroy(&p_ctx->encoder_write_semaphore);
  sem_free_and_destroy(&p_ctx->encoder_read_sem);

  pthread_cond_free_and_destroy(&p_ctx->encoder_write_cond);
  pthread_cond_free_and_destroy(&p_ctx->encoder_read_cond);

  pthread_mutex_free_and_destroy(&p_ctx->dts_queue_mutex);
  pthread_mutex_free_and_destroy(&p_ctx->encoder_write_mutex);
  pthread_mutex_free_and_destroy(&p_ctx->encoder_write_mutex_len);
  pthread_mutex_free_and_destroy(&p_ctx->encoder_read_mutex);
  pthread_mutex_free_and_destroy(&p_ctx->encoder_read_mutex_len);
#endif //XCODER_IO_RW_ENABLED

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_SUCCESS;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  //malloc data buffer
  if(posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_encoder_session_close() alloc data buffer failed\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  int retry = 0;
  while (retry < NI_MAX_SESSION_CLOSE_RETRIES)  // 10 retries
  {
    ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
           "p_ctx->session_id=%d, close_mode=1\n",
           (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
      ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_close(): command failed\n");
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      break;
    }
    else if(((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): wait for close\n");
      usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US); // 500000 us
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    retry++;
  }
#else //XCODER_IO_RW_ENABLED

  DESTROY_SESSION_SET_DW10_INSTANCE(cmd.cdw10, p_ctx->session_id);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
    ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
      "p_ctx->session_id=%d, close_mode=1\n",
      (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

    if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_close, p_ctx->device_handle, &cmd, 0, NULL, &nvme_result))
    {
      ni_log(NI_LOG_TRACE, "ERROR: Invalide session ID, return\n");
      usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    else
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    retry++;
  }
#endif //XCODER_IO_RW_ENABLED

  END;

  aligned_free(p_buffer);
  aligned_free(p_ctx->p_all_zero_buf);

  //Sequence change related stuff cleanup here
  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  //End of sequence change related stuff cleanup

  if ((ni_timestamp_table_t*)p_ctx->pts_table)
  {
    if (p_ctx->pts_table)
    {
      ni_timestamp_table_t* p_pts_table = p_ctx->pts_table;
      ni_queue_free(&p_pts_table->list, p_ctx->buffer_pool);
      free(p_ctx->pts_table);
      p_ctx->pts_table = NULL;
      ni_log(NI_LOG_TRACE, "ni_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
      ni_timestamp_table_t* p_dts_queue = p_ctx->dts_queue;
      ni_queue_free(&p_dts_queue->list, p_ctx->buffer_pool);
      free(p_ctx->dts_queue);
      p_ctx->dts_queue = NULL;
      ni_log(NI_LOG_TRACE, "ni_timestamp_done: success\n");
    }
  }

  ni_buffer_pool_free(p_ctx->buffer_pool);
  p_ctx->buffer_pool = NULL;

  for (i = 0; i < NI_FIFO_SZ; i++)
  {
    free(p_ctx->pkt_custom_sei[i]);
    p_ctx->pkt_custom_sei[i] = NULL;
    p_ctx->pkt_custom_sei_len[i] = 0;
  }

  free(p_ctx->last_pkt_custom_sei);
  p_ctx->last_pkt_custom_sei = NULL;
  p_ctx->last_pkt_custom_sei_len = 0;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t codec_end_time = tv.tv_sec*1000000ULL + tv.tv_usec;
  if ((p_ctx->codec_total_ticks) && (codec_end_time - p_ctx->codec_start_time)) //if close immediately after opened, end time may equals to start time
  {
    uint32_t ni_usage = (p_ctx->codec_total_ticks/NI_VPU_FREQ)*100/(codec_end_time - p_ctx->codec_start_time);
    ni_log(NI_LOG_INFO, "Encoder HW[%d] INST[%d]-average usage:%d%%\n", p_ctx->hw_id, (p_ctx->session_id&0x7F), ni_usage);
  }
  else
  {
    ni_log(NI_LOG_INFO, "Warning Encoder HW[%d] INST[%d]-average usage equals to 0\n", p_ctx->hw_id, (p_ctx->session_id&0x7F));
  }

  ni_log(NI_LOG_TRACE, "encoder total_pkt:%" PRIx64 ", total_ticks:%" PRIx64 ", total_time:%" PRIx64 " us\n",
         p_ctx->pkt_num,
         p_ctx->codec_total_ticks,
         (codec_end_time - p_ctx->codec_start_time));

  ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);

  ni_log(NI_LOG_TRACE, "ni_encoder_session_close(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a YUV p_frame to encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_encoder_session_write(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  ni_nvme_result_t nvme_result[WORKER_QUEUE_DEPTH];
  ni_nvme_command_t cmd[WORKER_QUEUE_DEPTH];

  uint32_t size = 0;
  uint32_t metadata_size = NI_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t send_count = 0;
  uint32_t i = 0;
  uint32_t tx_size = 0, aligned_tx_size = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  int retval = 0;
  ni_instance_status_info_t inst_info = { 0 };
  worker_queue_item last_frame_chunk_send = {0};
#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "%s(): enter\n", __FUNCTION__);

  if ( (!p_ctx) || (!p_frame) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invlid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  
#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
      abs_time_ns = ni_gettime_ns();
#else
      clock_gettime(CLOCK_REALTIME, &logtv);
      abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
      ni_lat_meas_q_add_entry(p_ctx->frame_time_q, abs_time_ns, p_frame->dts);
  }
#endif

  /*!********************************************************************/
  /*!************ Sequence Change related stuff *************************/
  //First check squence changed related stuff.
  //We need to record the current hight/width params if we didn't do it before:

  if ( p_frame->video_height)
  {
    p_ctx->active_video_width = p_frame->data_len[0] / p_frame->video_height;
    p_ctx->active_video_height = p_frame->video_height;
  }
  else if (p_frame->video_width)
  {
    ni_log(NI_LOG_TRACE, "WARNING: passed video_height is not valid!, return\n");
    p_ctx->active_video_height = p_frame->data_len[0] / p_frame->video_width;
    p_ctx->active_video_width = p_frame->video_width;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed video_height and video_width are not valid!, return\n");
    retval = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  /*!************ Sequence Change related stuff end*************************/
  /*!********************************************************************/

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->extra_data_len;

  while (1)
  {
    retval = ni_query_status_info(p_ctx, p_ctx->device_type, &inst_info, retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval ||
        inst_info.wr_buf_avail_size < frame_size_bytes)
    {
      ni_log(NI_LOG_TRACE, "Warning enc write query try %u fail rc %d or "
             "available buf size %u < frame size %u !\n", send_count, retval,
             inst_info.wr_buf_avail_size, frame_size_bytes);
      if (send_count >= NI_MAX_ENC_SESSION_WRITE_QUERY_RETRIES) // 2000 retries
      {
        ni_log(NI_LOG_TRACE, "ERROR enc query buf info exceeding max retries: "
                       "%d, ", NI_MAX_ENC_SESSION_WRITE_QUERY_RETRIES);
        if (((ni_encoder_params_t *)p_ctx->p_session_config)->strict_timeout_mode)
        {
          p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
        }
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
      }
      send_count++;
      usleep(NI_RETRY_INTERVAL_100US);  // 100 us
    }
    else
    {
      ni_log(NI_LOG_TRACE, "Info enc write query success, available buf "
             "size %u >= frame size %u !\n",
             inst_info.wr_buf_avail_size, frame_size_bytes);
      break;
    }
  }
  
  p_ctx->ready_to_close = p_frame->end_of_stream;	
  send_count = 0;
  
  // fill in metadata such as timestamp
  ni_metadata_enc_frame_t *p_meta =  
  (ni_metadata_enc_frame_t *)((uint8_t *)p_frame->p_data[2] + p_frame->data_len[2]);
  p_meta->metadata_common.ui64_data.frame_tstamp = (uint64_t)p_frame->pts;
  
  p_meta->force_headers = 0; // p_frame->force_headers not implemented/used
  p_meta->use_cur_src_as_long_term_pic = p_frame->use_cur_src_as_long_term_pic;
  p_meta->use_long_term_ref            = p_frame->use_long_term_ref;

  p_meta->frame_force_type_enable = p_meta->frame_force_type = 0;
  // frame type to be forced to is supposed to be set correctly
  // in p_frame->ni_pict_type
  if (1 == p_ctx->force_frame_type || p_frame->force_key_frame)
  {
    if (p_frame->ni_pict_type)
    {
      p_meta->frame_force_type_enable = 1;
      p_meta->frame_force_type = p_frame->ni_pict_type;
    }
    ni_log(NI_LOG_TRACE, "%s(): ctx->force_frame_type"
                   " %d frame->force_key_frame %d force frame_num %lu"
                   " type to %d\n", 
                   __FUNCTION__,
                   p_ctx->force_frame_type, p_frame->force_key_frame,
                   p_ctx->frame_num, p_frame->ni_pict_type);
  }
  
  // force pic qp if specified
  p_meta->force_pic_qp_enable = p_meta->force_pic_qp_i = 
  p_meta->force_pic_qp_p = p_meta->force_pic_qp_b = 0;
  if (p_frame->force_pic_qp)
  {
    p_meta->force_pic_qp_enable = 1;
    p_meta->force_pic_qp_i = p_meta->force_pic_qp_p = 
    p_meta->force_pic_qp_b = p_frame->force_pic_qp;
  }
  p_meta->frame_sei_data_size = p_frame->sei_total_len;
  p_meta->frame_roi_map_size = p_frame->roi_len;
  p_meta->frame_roi_avg_qp = p_ctx->roi_avg_qp;
  p_meta->enc_reconfig_data_size = p_frame->reconf_len;
  
  ni_log(NI_LOG_TRACE, "%s(): %d.%d p_ctx->frame_num=%lu, p_frame->start_of_stream=%u, end_of_stream=%u, video_width=%u, video_height=%u, pts=0x%08x 0x%08x, dts=0x%08x 0x%08x, sei_len=%u, roi size=%u avg_qp=%u reconf_len=%u force_pic_qp=%u force_headers=%u frame_force_type_enable=%u frame_force_type=%u force_pic_qp_enable=%u force_pic_qp_i/p/b=%u use_cur_src_as_long_term_pic %u use_long_term_ref %u\n",
         __FUNCTION__,  p_ctx->hw_id, p_ctx->session_id, p_ctx->frame_num,
         p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height,
         (uint32_t)((p_frame->pts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_frame->pts & 0xFFFFFFFF),
         (uint32_t)((p_frame->dts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_frame->dts & 0xFFFFFFFF),
         p_meta->frame_sei_data_size, p_meta->frame_roi_map_size,
         p_meta->frame_roi_avg_qp, p_meta->enc_reconfig_data_size,
         p_meta->force_pic_qp_i, p_meta->force_headers,
         p_meta->frame_force_type_enable, p_meta->frame_force_type,
         p_meta->force_pic_qp_enable, p_meta->force_pic_qp_i,
         p_meta->use_cur_src_as_long_term_pic, p_meta->use_long_term_ref);
  
  if (p_frame->start_of_stream)
  {
    //Send Start of stream p_config command here
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }
  
    p_frame->start_of_stream = 0;
  }
  
  // skip direct to send eos without sending the passed in p_frame as it's been sent already
  if (p_frame->end_of_stream)
  {
    retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }
  
    p_frame->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }
  else //handle regular frame sending
  {
    if (p_frame->p_data)
    {
#ifdef XCODER_IO_RW_ENABLED
      pthread_mutex_lock(p_ctx->dts_queue_mutex);
      retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_frame->dts, 0);
      pthread_mutex_unlock(p_ctx->dts_queue_mutex);
      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_write(): ni_timestamp_register() for dts returned: %d\n", retval);
      }
      
      uint32_t ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
      ni_log(NI_LOG_TRACE, "Encoder session write: p_data = %p, p_frame->buffer_size = %u, p_ctx->frame_num = %"PRIu64", LBA = 0x%x\n", \
                        p_frame->p_data, \
                        p_frame->buffer_size, \
                        p_ctx->frame_num, \
                        ui32LBA);
      sent_size = frame_size_bytes;
      if (sent_size % NI_MEM_PAGE_ALIGNMENT)
      {
        sent_size = ( (sent_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
      }

      retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_frame->p_buffer, sent_size, ui32LBA);
      CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_write,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
      CHECK_VPU_RECOVERY(retval);
      if (retval < 0)
      {
        ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_write(): nvme command failed\n");
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
      }

      p_ctx->frame_num++;
      size = frame_size_bytes;

#ifdef XCODER_DUMP_DATA
      char dump_file[128];
      snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-fme/fme-%04ld.yuv",
               (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

      FILE *f = fopen(dump_file, "wb");
      fwrite(p_frame->p_buffer, p_frame->data_len[0] + p_frame->data_len[1] +
             p_frame->data_len[2], 1, f);
      fflush(f);
      fclose(f);
#endif

#else //XCODER_IO_RW_ENABLED

      uint8_t* p_data = p_frame->p_buffer;
      int meta_sent = 0;
      uint32_t frame_chunk_index = 0;
      uint32_t total_num_of_chunks = 0; 
      total_num_of_chunks = frame_size_bytes/(p_ctx->max_nvme_io_size);
      if (frame_size_bytes % (p_ctx->max_nvme_io_size))
      {
        total_num_of_chunks++;//total frame chunks that need to be transfered
      }
      uint32_t frame_chunk_counter = 0; // current index of the frame that is being transfered
      ni_log(NI_LOG_TRACE, "Total num of chunks %d, fsb %d, pm %d\n",total_num_of_chunks, frame_size_bytes, p_ctx->max_nvme_io_size);
     // while (frame_size_bytes > 0)
      {
 
        ni_log(NI_LOG_TRACE, "WARN: p_data = %p, p_frame->buffer_size = %u frame_chunk_index = %d\n", \
                        p_data, \
                        p_frame->buffer_size, \
                        frame_chunk_index);
        tx_size = (frame_size_bytes <= p_ctx->max_nvme_io_size) ? frame_size_bytes : p_ctx->max_nvme_io_size;
  
        aligned_tx_size = tx_size;
        ni_log(NI_LOG_TRACE, "WARN: original tx_size = %u\n",tx_size);
        // Adjust the actual TX size here to be multiples
        // of 512 bytes to prevent user->kernel copy
        if ( (tx_size > NI_MEM_PAGE_ALIGNMENT) &&
             (tx_size != p_ctx->max_nvme_io_size) )
        {
          ni_log(NI_LOG_TRACE, "WARN: tx_size > NI_MEM_PAGE_ALIGNMENT and tx_size != NI_MAX_FRAME_CHUNK_SZ, tx_size %% NI_MEM_PAGE_ALIGNMENT = %u\n",
                         tx_size % NI_MEM_PAGE_ALIGNMENT);
          if (tx_size % NI_MEM_PAGE_ALIGNMENT)
          {
            ni_log(NI_LOG_TRACE, "WARN: before calc tx_size = %u\n",tx_size);
            aligned_tx_size = ( (tx_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  
            ni_log(NI_LOG_TRACE, "WARN: after calc tx_size = %u aligned_tx_size %d \n",tx_size, aligned_tx_size);
          }
        }

        uint32_t one_chunk_flag = false;

        if (frame_size_bytes<=p_ctx->max_nvme_io_size)
        {
          assert(total_num_of_chunks<2);
          one_chunk_flag = true;
        }
  
        //WRITE_INSTANCE_SET_DW15_SIZE(cmd.cdw15, tx_size);

        p_ctx->encoder_write_processed_data_len = 0;

        pthread_mutex_lock(p_ctx->dts_queue_mutex);
        retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_frame->dts, 0);
        pthread_mutex_unlock(p_ctx->dts_queue_mutex);
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_write(): ni_timestamp_register() for dts returned: %d\n", retval);
        }

        // Now we enqueue the arguments to the queue
        // one argument represents one write request
        // We fill all the encoder write except the last chunk request untill the queue is full

        // NOTICE: we only enque write chunk that is not the last chunk, last chunk can contain the metadata header
        // the frame chunk that contians the metadata header will have to be sent first.
        // There is a possibility that the metadata somehow acrossed 2 chunks, this piece of code does not solve this issue
        // In the real world, this may not happen either

        // For example for 4k yuv420p frame there are 4 chunks, the last chunk will not be in the queue

        // In this function, the last chunk will be sent out and make sure it return first then release all the threads

        // TODO from Zhong's opinion:
        // This possibility can be eliminated if you divide YUV and the rest of the data (including metadata hdr and metadata body),
        // since you already know the size of YUV. The rest of data can almost always be sent in one chunk because of its size.
        
        int32_t frame_remain_bytes = frame_size_bytes;

        if (!one_chunk_flag){
          for(; frame_chunk_counter < total_num_of_chunks-1; frame_chunk_counter++)
          {
              ni_log(NI_LOG_TRACE, "frame chunk counter %d  opcode %d remain %d dl %d\n" ,\
                              frame_chunk_counter, \
                              p_ctx->args_encoder_write[frame_chunk_counter].opcode,frame_remain_bytes,
                              ((frame_remain_bytes) < (p_ctx->max_nvme_io_size)) ? frame_remain_bytes :((p_ctx->max_nvme_io_size)));
              p_ctx->args_encoder_write[frame_chunk_counter].opcode = nvme_cmd_xcoder_write;
              p_ctx->args_encoder_write[frame_chunk_counter].handle = p_ctx->blk_io_handle;
              p_ctx->args_encoder_write[frame_chunk_counter].p_ni_nvme_cmd = &(cmd[frame_chunk_counter]);
              p_ctx->args_encoder_write[frame_chunk_counter].data_len = ((frame_remain_bytes) < (p_ctx->max_nvme_io_size)) ? frame_remain_bytes :((p_ctx->max_nvme_io_size));
              p_ctx->args_encoder_write[frame_chunk_counter].p_data = p_data;
              p_ctx->args_encoder_write[frame_chunk_counter].p_result = &(nvme_result[frame_chunk_counter]);
              p_ctx->args_encoder_write[frame_chunk_counter].frame_chunk_index = ((frame_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE); // address of set that is 4K based
              p_ctx->args_encoder_write[frame_chunk_counter].frame_chunk_size = p_ctx->max_nvme_io_size;
              p_ctx->args_encoder_write[frame_chunk_counter].session_id = p_ctx->session_id;
              p_ctx->args_encoder_write[frame_chunk_counter].dw14_yuv_offset = 0;
              frame_remain_bytes = (frame_remain_bytes - p_ctx->args_encoder_write[frame_chunk_counter].data_len);
              
              while(is_worker_q_full(&(p_ctx->encoder_write_workerqueue))) // if queue if full then we need to jump out this enqueu loop to check if there is worker availe and then trigger the worker with the argument in the queue
              {
                ni_log(NI_LOG_TRACE, "The queue should never be full!!");
                usleep(100);
                continue;
              }
               // we still have some space in the queue so enque the argment,
              {
                pthread_mutex_lock(p_ctx->encoder_write_mutex);
                worker_q_enq(&(p_ctx->encoder_write_workerqueue), p_ctx->args_encoder_write[frame_chunk_counter]);
                pthread_mutex_unlock(p_ctx->encoder_write_mutex);
              }
              
          }
        }

        ni_log(NI_LOG_TRACE, "frame_opc %d\n", frame_chunk_counter);

        // Now we construct the last frame chunk
        last_frame_chunk_send.opcode = nvme_cmd_xcoder_write;
        last_frame_chunk_send.handle = p_ctx->blk_io_handle;
        last_frame_chunk_send.p_ni_nvme_cmd = &(cmd[frame_chunk_counter]);
        last_frame_chunk_send.data_len = ((frame_remain_bytes) < (p_ctx->max_nvme_io_size)) ? frame_remain_bytes :((p_ctx->max_nvme_io_size));
        last_frame_chunk_send.p_data = p_data;
        last_frame_chunk_send.p_result = &(nvme_result[frame_chunk_counter]);
        last_frame_chunk_send.frame_chunk_index = ((frame_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE);
        last_frame_chunk_send.frame_chunk_size = p_ctx->max_nvme_io_size;
        last_frame_chunk_send.session_id = p_ctx->session_id;
        last_frame_chunk_send.dw14_yuv_offset = 0;

        WRITE_INSTANCE_SET_DW10_SUBTYPE(last_frame_chunk_send.p_ni_nvme_cmd->cdw10, last_frame_chunk_send.session_id);
        WRITE_INSTANCE_SET_DW11_INSTANCE(last_frame_chunk_send.p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_ENCODER);
        WRITE_INSTANCE_SET_DW11_PAGEOFFSET(last_frame_chunk_send.p_ni_nvme_cmd->cdw11, last_frame_chunk_send.frame_chunk_index);  // set page index based on 4k
        WRITE_INSTANCE_SET_DW15_SIZE(last_frame_chunk_send.p_ni_nvme_cmd->cdw15,last_frame_chunk_send.data_len);
        WRITE_INSTANCE_SET_DW14_YUV_BYTEOFFSET(last_frame_chunk_send.p_ni_nvme_cmd->cdw14,0);

        int32_t ret_last = -1;
        int32_t retry_count = 0;
        uint32_t data_len_local = last_frame_chunk_send.data_len;
        while(ret_last != 0 ){
          ni_log(NI_LOG_TRACE, "encoder_wwrite opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x,  data_len %d, pdataoff %p \n",\
                      last_frame_chunk_send.opcode, \
                      last_frame_chunk_send.handle, \
                      last_frame_chunk_send.p_ni_nvme_cmd->cdw10, \
                      last_frame_chunk_send.p_ni_nvme_cmd->cdw11, \
                      last_frame_chunk_send.p_ni_nvme_cmd->cdw15, \
                      last_frame_chunk_send.data_len, \
                      ((last_frame_chunk_send.p_data)+(last_frame_chunk_send.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE)
                      );
          if (last_frame_chunk_send.data_len % NI_MEM_PAGE_ALIGNMENT)
          {
            last_frame_chunk_send.data_len = ( (last_frame_chunk_send.data_len / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
          }
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
          ret_last = ni_nvme_send_io_cmd_thru_admin_queue(nvme_admin_cmd_xcoder_write, \
                               last_frame_chunk_send.handle, \
                               last_frame_chunk_send.p_ni_nvme_cmd, \
                               last_frame_chunk_send.data_len, \
                               ((last_frame_chunk_send.p_data)+(last_frame_chunk_send.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), \
                               last_frame_chunk_send.p_result);
#else
          ret_last = ni_nvme_send_io_cmd(last_frame_chunk_send.opcode, \
                               last_frame_chunk_send.handle, \
                               last_frame_chunk_send.p_ni_nvme_cmd, \
                               last_frame_chunk_send.data_len, \
                               ((last_frame_chunk_send.p_data)+(last_frame_chunk_send.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE), \
                               last_frame_chunk_send.p_result);
#endif
          if (ret_last != NI_RETCODE_SUCCESS)
          {
            retval = ret_last;
            CHECK_VPU_RECOVERY(retval);

            ni_log(NI_LOG_TRACE, "encoder write failed retry\n");
            *(last_frame_chunk_send.p_result) = 0;
            retry_count++;
            usleep(100);
            if (retry_count == 6000)
            {
              ni_log(NI_LOG_TRACE, "Something is wrong as the encoder write command retried 6000 times, now assert\n");
              assert(false);
            }
            continue;
          }
          else
          {
            // command issued successfully
            *(last_frame_chunk_send.p_result) =  data_len_local;
            break;
          }
        }
        p_ctx->encoder_write_processed_data_len +=*(last_frame_chunk_send.p_result);    
        // Now the last frame chunk is sent down and also confirmed received by FW
        // We can use thread to transfer the reset of the chunks to FW now
        ni_log(NI_LOG_TRACE, "Totally %d commands enqueued, %d needed, empty %d \n", \
                      frame_chunk_counter, \
                      total_num_of_chunks, \
                      is_worker_q_empty(&(p_ctx->encoder_write_workerqueue)));
        int num_of_thread_to_release = total_num_of_chunks-1;    
        if (!one_chunk_flag){
          while(num_of_thread_to_release > 0)
          {
          
            ni_log(NI_LOG_TRACE, "One worker thread released encoder write\n");
            sem_post(p_ctx->encoder_write_semaphore);
            //pthread_cond_signal(&(encoder_write_condition));
            num_of_thread_to_release--;
            ni_log(NI_LOG_TRACE, "One worker thread finsh release encoder write\n");
          }
          p_ctx->encoder_write_total_num_of_bytes = frame_size_bytes;
          // Here we need to wait untill all the threads are finished. all the latency comes now
          pthread_mutex_lock(p_ctx->encoder_write_mutex_len);
          while(p_ctx->encoder_write_processed_data_len != frame_size_bytes){
            pthread_cond_wait(p_ctx->encoder_write_cond,p_ctx->encoder_write_mutex_len);
          }
          pthread_mutex_unlock(p_ctx->encoder_write_mutex_len);
          ni_log(NI_LOG_TRACE, "all thread finished frame num:%lu\n", p_ctx->frame_num);
        }

        ni_log(NI_LOG_TRACE, "ni_encoder_session_write: sent data retries=%u, retval=%d\n", send_count, retval);
      }
      
      p_ctx->frame_num++;
      
      //size = p_frame->data_len[0];
      size = frame_size_bytes;
      
#if 0 // for debug purpose
      //  ni_log(NI_LOG_INFO, "Enc -> pts %lld (dts %lld) \n", p_frame->pts, p_frame->dts);
#endif
#endif //XCODER_IO_RW_ENABLED
    }
  }

  retval = size;

  END;
  ni_log(NI_LOG_TRACE, "ni_encoder_session_write(): exit\n");

  return retval;
}


int ni_encoder_session_read(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  ni_nvme_result_t nvme_result[WORKER_QUEUE_DEPTH];
  ni_nvme_command_t cmd[WORKER_QUEUE_DEPTH];
  ni_instance_mgr_stream_info_t data = { 0 };
  uint32_t chunk_max_size = p_ctx->max_nvme_io_size;
  uint32_t actual_read_size = 0, chunk_size, end_of_pkt;
  uint32_t actual_read_size_aligned = 0;
  int reading_partial_pkt = 0;
  uint32_t to_read_size = 0;
  int size = 0;
  uint32_t query_return_size = 0;
  uint8_t* p_data = NULL;
  static long long encq_count = 0LL;
  int retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t query_retry = 0;
  ni_metadata_enc_bstream_t *p_meta = NULL;
  ni_instance_status_info_t inst_info = { 0 };
  worker_queue_item last_pkt_chunk_read = { 0 };
  p_ctx->encoder_read_processed_data_len = 0;
#ifdef MEASURE_LATENCY
  struct timespec logtv;
  uint64_t abs_time_ns;
#endif

  ni_log(NI_LOG_TRACE, "ni_encoder_session_read(): enter\n");

  if ((!p_ctx) || (!p_packet) || (!p_packet->p_data))
  {
    ni_log(NI_LOG_ERROR, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_ERROR, "xcoder instance id == 0, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  p_packet->data_len = 0;
  p_packet->pts = NI_NOPTS_VALUE;
  p_packet->dts = 0;

enc_read_query:
  query_retry = 0;

  while (1)
  {
    query_retry++;

    retval = ni_query_status_info(p_ctx, p_ctx->device_type, &inst_info,retval, nvme_admin_cmd_xcoder_query);
    CHECK_ERR_RC2(p_ctx, retval, inst_info, nvme_admin_cmd_xcoder_query,
                  p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
    CHECK_VPU_RECOVERY(retval);

    ni_log(NI_LOG_TRACE, "Info enc read query try %u rc %d, available buf size %u, "
           "frame_num=%"PRIu64", pkt_num=%"PRIu64" reading_partial_pkt %d\n",
           query_retry, retval, inst_info.rd_buf_avail_size, p_ctx->frame_num,
           p_ctx->pkt_num, reading_partial_pkt);

    if (NI_RETCODE_SUCCESS != retval)
    {   
      ni_log(NI_LOG_ERROR, "Buffer info query failed in encoder read!!!!\n");
      LRETURN;
    }
    else if (0 == inst_info.rd_buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (! reading_partial_pkt && p_ctx->ready_to_close)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_ENCODER, &data);
        CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id));
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log(NI_LOG_ERROR, "Stream info query failed in encoder read !!\n");
          LRETURN;
        }
      
        if (data.is_flushed)
        {
          p_packet->end_of_stream = 1;
        }
      }
      ni_log(NI_LOG_TRACE, "Info enc read available buf size %u, eos %u !\n",
             inst_info.rd_buf_avail_size, p_packet->end_of_stream);

      if (((ni_encoder_params_t *)p_ctx->p_session_config)->strict_timeout_mode
          && (query_retry > NI_MAX_ENC_SESSION_READ_QUERY_RETRIES)) // 3000 retries
      {
        ni_log(NI_LOG_ERROR, "ERROR Receive Packet Strict Timeout, Encoder low "
               "latency mode %d, buf_full %d eos sent %d, frame_num %"PRIu64" "
               ">= %"PRIu64" pkt_num, retry limit exceeded and exit encoder "
               "pkt reading.\n",
               ((ni_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode,
               NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status,
               p_ctx->ready_to_close, p_ctx->frame_num, p_ctx->pkt_num);
        retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
        LRETURN;
      }

      if ((((ni_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode ||
           (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status) ||
           p_ctx->ready_to_close || reading_partial_pkt) &&
          ! p_packet->end_of_stream && p_ctx->frame_num >= p_ctx->pkt_num)
      {
        ni_log(NI_LOG_TRACE, "Encoder low latency mode %d, buf_full %d eos sent"
               " %d, reading_partial_pkt %d, frame_num %"PRIu64" >= %"PRIu64" "
               "pkt_num, keep querying.\n",
               ((ni_encoder_params_t *)p_ctx->p_session_config)->low_delay_mode,
               NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status,
               p_ctx->ready_to_close, reading_partial_pkt,
               p_ctx->frame_num, p_ctx->pkt_num);
        usleep(NI_RETRY_INTERVAL_200US);  // 200 us
        continue;
      }

      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
      if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status)
      {
        p_ctx->status = 0;
      }
      break;
    }
  }
  ni_log(NI_LOG_TRACE, "Encoder read buf_avail_size %u \n",
         inst_info.rd_buf_avail_size);

  to_read_size = inst_info.rd_buf_avail_size;

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  if (to_read_size % NI_MEM_PAGE_ALIGNMENT)
  {
    to_read_size = ((to_read_size / NI_MEM_PAGE_ALIGNMENT) *
                    NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 (uint8_t *)p_packet->p_data + actual_read_size_aligned,
                                 to_read_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_cmd_xcoder_read,
               p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
    ni_log(NI_LOG_ERROR, "ERROR ni_encoder_session_read(): read command "
           "failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  // retrieve metadata pkt related info only once, when eop = 1
  p_meta = (ni_metadata_enc_bstream_t *)((uint8_t *)p_packet->p_data +
                                         actual_read_size_aligned);
  chunk_size = p_meta->bs_frame_size;
  end_of_pkt = p_meta->end_of_packet;
  if (end_of_pkt)
  {
    p_packet->frame_type = p_meta->frame_type;
    p_packet->pts = (int64_t)(p_meta->frame_tstamp);
    p_ctx->codec_total_ticks += p_meta->frame_cycle;
    p_packet->avg_frame_qp = p_meta->avg_frame_qp;
  }

  if (0 == actual_read_size)
  {
    actual_read_size = sizeof(ni_metadata_enc_bstream_t) + chunk_size;
  }
  else
  {
    memmove((uint8_t *)p_packet->p_data + actual_read_size,
            (uint8_t*)p_packet->p_data + actual_read_size_aligned +
            sizeof(ni_metadata_enc_bstream_t), chunk_size);
    actual_read_size += chunk_size;
  }

  actual_read_size_aligned = actual_read_size;
  if (actual_read_size_aligned % NI_MEM_PAGE_ALIGNMENT)
  {
    actual_read_size_aligned = ((actual_read_size / NI_MEM_PAGE_ALIGNMENT)
                         * NI_MEM_PAGE_ALIGNMENT ) + NI_MEM_PAGE_ALIGNMENT;
  }

  ni_log(NI_LOG_TRACE, "ni_encoder_session_read(): read %u so far %u (%u) bytes"
         ", end_of_pkt: %u\n", chunk_size, actual_read_size,
         actual_read_size_aligned, end_of_pkt);

  if (! end_of_pkt)
  {
    reading_partial_pkt = 1;
    goto enc_read_query;
  }

  p_packet->data_len = actual_read_size;

  size = p_packet->data_len;
#else //XCODER_IO_RW_ENABLED

  p_data = (uint8_t*)p_packet->p_data;
  p_packet->data_len = 0;
  p_packet->pts = NI_NOPTS_VALUE;
  p_packet->dts = 0;


  uint32_t total_num_of_pkt_chunks = 0;
  uint32_t pkt_chunk_counter = 0;
  total_num_of_pkt_chunks = to_read_size/(p_ctx->max_nvme_io_size);
  p_ctx->encoder_read_total_num_of_chunks = total_num_of_pkt_chunks;
  if (to_read_size%(p_ctx->max_nvme_io_size))
  {
      total_num_of_pkt_chunks++;
  }

  actual_read_size = chunk_max_size = (to_read_size <= p_ctx->max_nvme_io_size)
  ? to_read_size : p_ctx->max_nvme_io_size;

  if (chunk_max_size < p_ctx->max_nvme_io_size)
  {
    if (chunk_max_size % NI_MEM_PAGE_ALIGNMENT)
    {
      chunk_max_size = ((chunk_max_size / NI_MEM_PAGE_ALIGNMENT) *
                        NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    }
  }

  uint32_t one_chunk_flag = false;
  if (to_read_size <= p_ctx->max_nvme_io_size)
  {
    assert(total_num_of_pkt_chunks<2);
    one_chunk_flag = true;
  }

  // Now we enque the chunks to read to the encoder read queue
  // We fill in all the encoder read entries except the last chunk, thus
  // minus 1 in the for loop counter

  int32_t pkt_remain_bytes = to_read_size;
  if (!one_chunk_flag)
  {
    ni_log(NI_LOG_TRACE, "Before the loop total_num_of_pkt_chunks %d \n", total_num_of_pkt_chunks);
    for (; pkt_chunk_counter < total_num_of_pkt_chunks-1; pkt_chunk_counter++)
    {
      ni_log(NI_LOG_TRACE, "rpkt_chunk_counter %d total_num_of_pkt_chunks %d to_read_size %u p_ctx->max_nvme_io_size %d pkt_remain_bytes %d\n",
             pkt_chunk_counter, total_num_of_pkt_chunks, to_read_size,
             p_ctx->max_nvme_io_size, pkt_remain_bytes);

      p_ctx->args_encoder_read[pkt_chunk_counter].opcode = nvme_cmd_xcoder_read;
      p_ctx->args_encoder_read[pkt_chunk_counter].handle = p_ctx->blk_io_handle;
      p_ctx->args_encoder_read[pkt_chunk_counter].p_ni_nvme_cmd = &(cmd[pkt_chunk_counter]);
      p_ctx->args_encoder_read[pkt_chunk_counter].data_len = (pkt_remain_bytes < p_ctx->max_nvme_io_size) ? (pkt_remain_bytes) : (p_ctx->max_nvme_io_size);
      p_ctx->args_encoder_read[pkt_chunk_counter].p_data = p_data;
      p_ctx->args_encoder_read[pkt_chunk_counter].p_result = &(nvme_result[pkt_chunk_counter]);
      p_ctx->args_encoder_read[pkt_chunk_counter].frame_chunk_index = ((pkt_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE);
      p_ctx->args_encoder_read[pkt_chunk_counter].frame_chunk_size = p_ctx->max_nvme_io_size;
      p_ctx->args_encoder_read[pkt_chunk_counter].session_id = p_ctx->session_id;
      pkt_remain_bytes = pkt_remain_bytes -  p_ctx->args_encoder_read[pkt_chunk_counter].data_len;

      // if queue is full then we need to jump out this enque loop to check 
      // if there is worker available and then trigger the worker with the 
      // argument in the queue
      while (is_worker_q_full(&(p_ctx->encoder_read_workerqueue)))
      {
          usleep(100);
          continue;
      }
      // we now have some space in the queue so enque the argument
      worker_q_enq(&(p_ctx->encoder_read_workerqueue),
                   p_ctx->args_encoder_read[pkt_chunk_counter]);
    } // for num_of_chunks - 1
  } // if multi-chunks to read

  // now we enqued all the chunks except the last one, read the last pkt 
  // chunk first
  last_pkt_chunk_read.opcode = nvme_cmd_xcoder_read;
  last_pkt_chunk_read.handle =  p_ctx->blk_io_handle;
  last_pkt_chunk_read.p_ni_nvme_cmd = &(cmd[pkt_chunk_counter]);
  last_pkt_chunk_read.data_len = (pkt_remain_bytes < p_ctx->max_nvme_io_size) ? (pkt_remain_bytes) : (p_ctx->max_nvme_io_size);
  last_pkt_chunk_read.p_data =  p_data;
  last_pkt_chunk_read.p_result =  &(nvme_result[pkt_chunk_counter]);
  last_pkt_chunk_read.frame_chunk_index = ((pkt_chunk_counter*p_ctx->max_nvme_io_size)/FRAME_CHUNK_INDEX_SIZE);
  last_pkt_chunk_read.frame_chunk_size = p_ctx->max_nvme_io_size;
  last_pkt_chunk_read.session_id = p_ctx->session_id;

  WRITE_INSTANCE_SET_DW10_SUBTYPE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw10, last_pkt_chunk_read.session_id);
  WRITE_INSTANCE_SET_DW11_INSTANCE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw11, NI_DEVICE_TYPE_ENCODER);
  WRITE_INSTANCE_SET_DW11_PAGEOFFSET(last_pkt_chunk_read.p_ni_nvme_cmd->cdw11, last_pkt_chunk_read.frame_chunk_index);  // set page index based on 4k
  WRITE_INSTANCE_SET_DW15_SIZE(last_pkt_chunk_read.p_ni_nvme_cmd->cdw15, last_pkt_chunk_read.data_len);

  int32_t ret_last = -1;
  int32_t retry_count = 0;
  int32_t data_len_local = last_pkt_chunk_read.data_len;
  while (ret_last != 0)
  {
    ni_log(NI_LOG_TRACE, "encoder read opcode %d, handle %d, p_ni_nvme_cmd dw10:0x%8x dw11:0x%8x dw15:0x%8x,  data_len %d, pdataoff %p \n",
           last_pkt_chunk_read.opcode, last_pkt_chunk_read.handle,
           last_pkt_chunk_read.p_ni_nvme_cmd->cdw10,
           last_pkt_chunk_read.p_ni_nvme_cmd->cdw11,
           last_pkt_chunk_read.p_ni_nvme_cmd->cdw15,
           last_pkt_chunk_read.data_len,
           ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE));
    if (last_pkt_chunk_read.data_len % NI_MEM_PAGE_ALIGNMENT)
    {
      last_pkt_chunk_read.data_len = ((last_pkt_chunk_read.data_len / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    }
#ifdef XCODER_OLD_NVME_DRIVER_ENABLED
    ret_last = ni_nvme_send_io_cmd_thru_admin_queue(
      nvme_admin_cmd_xcoder_read, last_pkt_chunk_read.handle,
      last_pkt_chunk_read.p_ni_nvme_cmd, last_pkt_chunk_read.data_len,
      ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE),
      last_pkt_chunk_read.p_result);
#else

    ret_last = ni_nvme_send_io_cmd(
      last_pkt_chunk_read.opcode, last_pkt_chunk_read.handle,
      last_pkt_chunk_read.p_ni_nvme_cmd, last_pkt_chunk_read.data_len,
      ((last_pkt_chunk_read.p_data)+(last_pkt_chunk_read.frame_chunk_index)*FRAME_CHUNK_INDEX_SIZE),
      last_pkt_chunk_read.p_result);
#endif
    if (ret_last != NI_RETCODE_SUCCESS)
    {
      retval = ret_last;
      CHECK_VPU_RECOVERY(retval);
      ni_log(NI_LOG_ERROR, "encoder read failed retry\n");
      *(last_pkt_chunk_read.p_result) = 0;
      retry_count++;
      usleep(100);
      if (retry_count == 6000)
      {
        ni_log(NI_LOG_ERROR, "Something is wrong as the encoder read command retried 6000 times, now assert\n");
        assert(false);
      }
      continue;
    }
    else
    {
      // command issued successfully, now exit
      *(last_pkt_chunk_read.p_result) = data_len_local;
      break;
    }
  } // keep sending read request until success
  p_ctx->encoder_read_processed_data_len += *(last_pkt_chunk_read.p_result);

  ni_log(NI_LOG_TRACE, "Totally_encoder_read enqueued %d total_num_of_chunks %d \n", pkt_chunk_counter, p_ctx->encoder_read_total_num_of_chunks);

  // Now the last pkt chunk is read back from the FW, we can signal the 
  // worker threads to work on the rest of the pkt chunks

  int num_of_thread_to_release = p_ctx->encoder_read_total_num_of_chunks - 1;
  if (!one_chunk_flag)
  {
    while (num_of_thread_to_release >= 0)
    {
      void *exit_statues;
      ni_log(NI_LOG_TRACE, "Release one thread for encoder read\n");
      sem_post(p_ctx->encoder_read_sem);
      ni_log(NI_LOG_TRACE, "Finish release one thread decoder read\n");
      num_of_thread_to_release--;
    }
    p_ctx->encoder_read_total_num_of_bytes = to_read_size;

    pthread_mutex_lock(p_ctx->encoder_read_mutex_len);
    while (to_read_size != p_ctx->encoder_read_processed_data_len)
    {
      pthread_cond_wait(p_ctx->encoder_read_cond, p_ctx->encoder_read_mutex_len);
    }
    pthread_mutex_unlock(p_ctx->encoder_read_mutex_len);
    ni_log(NI_LOG_TRACE, "No more encoder read ongoing progressed bytes %d total len %d \n", p_ctx->encoder_read_processed_data_len, to_read_size);
  } // multi-chunks

  p_meta = (ni_metadata_enc_bstream_t *)p_packet->p_data;
  p_packet->pts = (int64_t)(p_meta->frame_tstamp);
  p_packet->frame_type = p_meta->frame_type;
  p_packet->avg_frame_qp = p_meta->avg_frame_qp;
  p_ctx->codec_total_ticks += p_meta->frame_cycle;

  p_packet->data_len = p_ctx->encoder_read_processed_data_len ;

  size = p_packet->data_len;
  ni_log(NI_LOG_TRACE, "Data length is p_packet->data_len %d \n", p_packet->data_len);
#endif //XCODER_IO_RW_ENABLED

  if (size > 0)
  {
    if (p_ctx->pkt_num >= 1)
    {
      pthread_mutex_lock(p_ctx->dts_queue_mutex);
      if (ni_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)& p_packet->dts, 0, encq_count % 500 == 0, p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
        p_packet->dts = NI_NOPTS_VALUE;
      }
      pthread_mutex_unlock(p_ctx->dts_queue_mutex);
      p_ctx->pkt_num++;
    }
    
    encq_count++;

#ifdef XCODER_DUMP_DATA
    char dump_file[64];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-pkt/pkt-%04ld.bin",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->pkt_num);

    FILE *f = fopen(dump_file, "wb");
    fwrite((uint8_t*)p_packet->p_data + sizeof(ni_metadata_enc_bstream_t),
           p_packet->data_len - sizeof(ni_metadata_enc_bstream_t), 1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log(NI_LOG_TRACE, "ni_encoder_session_read(): %d.%d p_packet->start_of_stream=%d, end_of_stream=%d, video_width=%d, video_height=%d, dts=0x%08x 0x%08x, pts=0x%08x 0x%08x, type=%u, avg_frame_qp=%u\n",
         p_ctx->hw_id, p_ctx->session_id, p_packet->start_of_stream,
         p_packet->end_of_stream, p_packet->video_width, p_packet->video_height,
         (uint32_t)((p_packet->dts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_packet->dts & 0xFFFFFFFF),
         (uint32_t)((p_packet->pts >> 32) & 0xFFFFFFFF),
         (uint32_t)(p_packet->pts & 0xFFFFFFFF), p_packet->frame_type,
         p_packet->avg_frame_qp);

  ni_log(NI_LOG_TRACE, "ni_encoder_session_read(): p_packet->data_len=%u, size=%u\n", p_packet->data_len, size);

  if (encq_count % 500 == 0)
  {
    ni_log(NI_LOG_TRACE, "Encoder pts queue size = %d  dts queue size = %d\n\n",
    ((ni_timestamp_table_t*)p_ctx->pts_table)->list.count,
      ((ni_timestamp_table_t*)p_ctx->dts_queue)->list.count);
  }

  retval = size;

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL)) {
#ifdef _WIN32
    abs_time_ns = ni_gettime_ns();
#else
    clock_gettime(CLOCK_REALTIME, &logtv);
    abs_time_ns = (logtv.tv_sec*1000000000LL+logtv.tv_nsec);
#endif
    ni_log(NI_LOG_INFO, "DTS:%lld,DELTA:%lu,eLAT:%lu;\n", p_packet->dts, abs_time_ns - p_ctx->prev_read_frame_time, ni_lat_meas_q_check_latency(p_ctx->frame_time_q, abs_time_ns, p_packet->dts));
    p_ctx->prev_read_frame_time = abs_time_ns;
  }
#endif


  END;

  ni_log(NI_LOG_TRACE, "ni_encoder_session_read(): exit\n");

  return retval;
}




/*!******************************************************************************
 *  \brief  Query current encoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_encoder_session_query(ni_session_context_t* p_ctx)
{
  ni_instance_mgr_general_status_t data;
  int retval = NI_RETCODE_SUCCESS;
  
  ni_log(NI_LOG_TRACE, "ni_encoder_session_query(): enter\n");
  
  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  retval = ni_query_general_status(p_ctx, NI_DEVICE_TYPE_ENCODER, &data);

  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->load_query.current_load = (uint32_t)data.process_load_percent;
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.total_contexts =
    (uint32_t)data.active_sub_instances_cnt;
    ni_log(NI_LOG_TRACE, "ni_encoder_session_query current_load:%d fw_model_load:%d fw_video_mem_usage:%d active_contexts %d\n", p_ctx->load_query.current_load, p_ctx->load_query.fw_model_load, p_ctx->load_query.fw_video_mem_usage, p_ctx->load_query.total_contexts);
  }
  
  END;
  
  ni_log(NI_LOG_TRACE, "ni_encoder_session_query(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get GeneralStatus data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_general_status_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_general_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_general_status_t* p_gen_status)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_general_status_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "ni_query_general_status(): enter\n");

  if ((!p_ctx) || (!p_gen_status))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = QUERY_GENERAL_GET_STATUS_R(device_type);
#else
  QUERY_GENERAL_SET_DW10_SUBTYPE(cmd.cdw10);
  QUERY_GENERAL_SET_DW11_INSTANCE_STATUS(cmd.cdw11, device_type);
  QUERY_INSTANCE_SET_DW15_SIZE(cmd.cdw15, sizeof(ni_instance_mgr_general_status_t));
#endif

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_query_instance_buf_info() Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

#ifdef XCODER_IO_RW_ENABLED
  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
#else
  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_query, p_ctx->device_handle, &cmd, dataLen, p_buffer, &nvme_result))
#endif
  {
    ni_log(NI_LOG_TRACE, " ni_query_general_status(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  //No need to flip the bytes since the datastruct has only uint8_t datatypes
  memcpy((void*)p_gen_status, p_buffer, sizeof(ni_instance_mgr_general_status_t));
  
  ni_log(NI_LOG_TRACE, "ni_query_general_status(): model_load:%d qc:%d percent:%d\n",
                 p_gen_status->fw_model_load, p_gen_status->cmd_queue_count,
                 p_gen_status->process_load_percent);
  END;

  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_query_general_status(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get Stream Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_stream_info_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_query_stream_info(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_info_t* p_stream_info)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "ni_query_stream_info(): enter\n");

  if ((!p_ctx) || (!p_stream_info))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = QUERY_INSTANCE_STREAM_INFO_R(p_ctx->session_id, device_type);
#else
  QUERY_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  QUERY_INSTANCE_SET_DW11_INSTANCE_STREAM_INFO(cmd.cdw11, device_type);
  QUERY_INSTANCE_SET_DW15_SIZE(cmd.cdw15, sizeof(ni_instance_mgr_stream_info_t));
#endif

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_query_general_status() Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

#ifdef XCODER_IO_RW_ENABLED
  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
#else
  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_query, p_ctx->device_handle, &cmd, dataLen, p_buffer, &nvme_result))
#endif
  {
    ni_log(NI_LOG_TRACE, " ni_query_stream_info(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_stream_info, p_buffer, sizeof(ni_instance_mgr_stream_info_t));

  //flip the bytes to host order
  p_stream_info->picture_width = ni_htons(p_stream_info->picture_width);
  p_stream_info->picture_height = ni_htons(p_stream_info->picture_height);
  p_stream_info->frame_rate = ni_htons(p_stream_info->frame_rate);
  p_stream_info->is_flushed = ni_htons(p_stream_info->is_flushed);

  END;

  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_query_stream_info(): exit\n");

  return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get status Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_status_info_t *out - Struct preallocated from the
 *           caller where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED
 *            on failure
 ******************************************************************************/
ni_retcode_t ni_query_status_info(ni_session_context_t* p_ctx,
                                  ni_device_type_t device_type,
                                  ni_instance_status_info_t* p_status_info,
                                  int rc,
                                  int opcode)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
#ifdef XCODER_IO_RW_ENABLED
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_status_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "ni_query_status_info(): enter\n");

  if ((!p_ctx) || (!p_status_info))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_CUR_STATUS_INFO_R(p_ctx->session_id, device_type);

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_query_stream_info() Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  // There are some cases that FW will not dma the data but just return error status, so add a default error number.
  // for example open more than 32 encoder sessions at the same time
  ((ni_instance_status_info_t *)p_buffer)->sess_err_no = NI_RETCODE_DEFAULT_SESSION_ERR_NO;
  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA)
      < 0)
  {
    ni_log(NI_LOG_ERROR, "ni_query_status_info(): read command Failed\n");
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_status_info, p_buffer, sizeof(ni_instance_status_info_t));

  // flip the bytes to host order
  p_status_info->sess_err_no = ni_htons(p_status_info->sess_err_no);
  p_status_info->sess_rsvd = ni_htons(p_status_info->sess_rsvd);
  p_status_info->inst_state = ni_htons(p_status_info->inst_state);
  p_status_info->inst_err_no = ni_htons(p_status_info->inst_err_no);
  p_status_info->wr_buf_avail_size = ni_htonl(p_status_info->wr_buf_avail_size);
  p_status_info->rd_buf_avail_size = ni_htonl(p_status_info->rd_buf_avail_size);
  p_status_info->sess_timestamp = ni_htonl(p_status_info->sess_timestamp);
  // session statistics
  p_status_info->frames_input = ni_htonl(p_status_info->frames_input);
  p_status_info->frames_buffered = ni_htonl(p_status_info->frames_buffered);
  p_status_info->frames_completed = ni_htonl(p_status_info->frames_completed);
  p_status_info->frames_output = ni_htonl(p_status_info->frames_output);
  p_status_info->frames_dropped = ni_htonl(p_status_info->frames_dropped);
  p_status_info->inst_errors = ni_htonl(p_status_info->inst_errors);

  // get the session timestamp when open session
  // check the timestamp during transcoding
  if (nvme_admin_cmd_xcoder_open == opcode)
  {
    p_ctx->session_timestamp = p_status_info->sess_timestamp;
    ni_log(NI_LOG_TRACE, "Session Open instance id:%u, timestamp:%" PRIx64 "\n", p_ctx->session_id, p_ctx->session_timestamp);
  }
  else if ((p_ctx->session_timestamp != p_status_info->sess_timestamp) &&
           (ni_xcoder_resource_recovery != p_status_info->inst_err_no))     // if VPU recovery, the session timestamp will be reset.
  {
    p_status_info->sess_err_no = NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE;
    ni_log(NI_LOG_ERROR, "instance id invalid:%u, timestamp:%" PRIx64 ", query timestamp:%" PRIx64 "\n",
           p_ctx->session_id,
           p_ctx->session_timestamp,
           p_status_info->sess_timestamp);
  }

  // map the ni_xcoder_mgr_retcode_t (regular i/o rc) to ni_retcode_t
  switch (p_status_info->inst_err_no)
  {
  case ni_xcoder_request_success:
    p_status_info->inst_err_no = NI_RETCODE_SUCCESS;
    break;
  case ni_xcoder_general_error:
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
    break;
  case ni_xcoder_request_pending:
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_REQUEST_IN_PROGRESS;
    break;
  case ni_xcoder_resource_recovery:
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_VPU_RECOVERY;
    break;
  case ni_xcoder_resource_insufficient:
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT;
    break;
  default:
    ; // kept unchanged
  }

  // check rc here, if rc != NI_RETCODE_SUCCESS, it means that last read/write command failed
  // failures may be link layer errors, such as physical link errors or ERROR_WRITE_PROTECT in windows.
  if (NI_RETCODE_SUCCESS != rc)
  {
    ni_log(NI_LOG_ERROR, "ni_query_status_info():last command Failed: rc %d\n", rc);
    p_status_info->inst_err_no = NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR;
  }
  else
  {
    ni_log(NI_LOG_TRACE, "ni_query_status_info stats, frames input: %u  "
           "buffered: %u  completed: %u  output: %u  dropped: %u ,  "
           "inst_errors: %u\n", p_status_info->frames_input,
           p_status_info->frames_buffered, p_status_info->frames_completed,
           p_status_info->frames_output, p_status_info->frames_dropped,
           p_status_info->inst_errors);
  }

  ni_log(NI_LOG_TRACE, "ni_query_status_info(): sess_err_no %u inst_state %u "
         "inst_err_no 0x%x\n", p_status_info->sess_err_no,
         p_status_info->inst_state, p_status_info->inst_err_no);

  p_ctx->session_stats = *p_status_info;

  END;

  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_query_status_info(): exit\n");
#else
  // for non-regular-io case, simply return the rc sent back by fw previously
  p_status_info->inst_err_no = rc;
#endif
  return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get End of Output data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   InstMgrStreamComp *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_complete_t* p_stream_complete)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_complete_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "ni_query_eos(): enter\n");

  if ((!p_ctx) || (!p_stream_complete))
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = QUERY_INSTANCE_EOS_R(p_ctx->session_id, device_type);
#else
  QUERY_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  QUERY_INSTANCE_SET_DW11_INSTANCE_END_OF_OUTPUT(cmd.cdw11, device_type);
  QUERY_INSTANCE_SET_DW15_SIZE(cmd.cdw15, sizeof(ni_instance_mgr_stream_complete_t));
#endif

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_query_status_info() Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

#ifdef XCODER_IO_RW_ENABLED
  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
#else
  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_query, p_ctx->device_handle, &cmd, dataLen, p_buffer, &nvme_result))
#endif
  {
    ni_log(NI_LOG_TRACE, " ni_query_eos(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }

  memcpy((void*)p_stream_complete, p_buffer, sizeof(ni_instance_mgr_stream_complete_t));

  //flip the bytes to host order
  p_stream_complete->is_flushed = ni_htons(p_stream_complete->is_flushed);

  END;

  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_query_eos(): exit\n");

  return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get buffer/data Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_instance_buf_info_rw_type_t rw_type
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_buf_info_t *out - Struct preallocated from the caller 
 *           where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, 
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on 
 *            failure
 ******************************************************************************/
ni_retcode_t ni_query_instance_buf_info(ni_session_context_t* p_ctx,
                                        ni_instance_buf_info_rw_type_t rw_type,
                                        ni_device_type_t device_type, 
                                        ni_instance_buf_info_t *p_inst_buf_info)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_buf_info_t) + 
                      (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT
                     ) * NI_MEM_PAGE_ALIGNMENT;

  ni_log(NI_LOG_TRACE, "ni_query_instance_buf_info(): enter\n");

  if (!p_ctx || !p_inst_buf_info)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  if (INST_BUF_INFO_RW_READ == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_RBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_WRITE == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_WBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ERROR: Unknown query type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
#else
  QUERY_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  
  if (INST_BUF_INFO_RW_READ == rw_type)
  {
    QUERY_INSTANCE_SET_DW11_INSTANCE_BUF_INFO(
      cmd.cdw11, nvme_query_xcoder_instance_read_buf_size, device_type);
  }
  else if (INST_BUF_INFO_RW_WRITE == rw_type)
  {
    QUERY_INSTANCE_SET_DW11_INSTANCE_BUF_INFO(
      cmd.cdw11, nvme_query_xcoder_instance_write_buf_size, device_type);
  }
  else
  {
    ni_log(NI_LOG_ERROR, "ERROR: Unknown query type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  QUERY_INSTANCE_SET_DW15_SIZE(cmd.cdw15, sizeof(ni_instance_buf_info_t));
#endif

  if (posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: ni_query_eos() Cannot allocate buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  
  memset(p_buffer, 0, dataLen);

#ifdef XCODER_IO_RW_ENABLED
  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, " ni_query_instance_buf_info(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }
#else
  if (retval = 
      ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_query, p_ctx->device_handle,
                             &cmd, dataLen, p_buffer, &nvme_result))
  {
    ni_log(NI_LOG_TRACE, " ni_query_instance_buf_info(): NVME command Failed\n");
    //retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    LRETURN;
  }
#endif

  memcpy((void*)p_inst_buf_info, p_buffer, sizeof(ni_instance_buf_info_t));

  p_inst_buf_info->buf_avail_size = ni_htonl(p_inst_buf_info->buf_avail_size);

  END;

  aligned_free(p_buffer);

  ni_log(NI_LOG_TRACE, "ni_query_instance_buf_info(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for Start Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION. NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_sos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "ni_config_instance_sos(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_INSTANCE_SetSOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, " ni_config_instance_sos(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#else
  CONFIG_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  CONFIG_INSTANCE_SET_DW11_SOS(cmd.cdw11, device_type);
  CONFIG_INSTANCE_SET_DW15_SIZE(cmd.cdw15, 0);

  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, p_ctx->device_handle, &cmd, 0, NULL, &nvme_result))
  {
    ni_log(NI_LOG_TRACE, " ni_config_instance_sos(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#endif

  END;

  ni_log(NI_LOG_TRACE, "ni_config_instance_sos(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for End Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "ni_config_instance_eos(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_INSTANCE_SetEOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, " ni_config_instance_eos(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#else
  CONFIG_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  CONFIG_INSTANCE_SET_DW11_EOS(cmd.cdw11, device_type);
  CONFIG_INSTANCE_SET_DW15_SIZE(cmd.cdw15, 0);

  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, p_ctx->device_handle, &cmd, 0, NULL, &nvme_result))
  {
    ni_log(NI_LOG_TRACE, " ni_config_instance_eos(): NVME command Failed\n");
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#endif

  END;

  ni_log(NI_LOG_TRACE, "ni_config_instance_eos(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to flush the stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t xcoder_config_instance_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "xcoder_config_instance_flush(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: passed parameters are null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log(NI_LOG_TRACE, "ERROR: Unknown device type, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_INSTANCE_Flush_W(p_ctx->session_id, device_type);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN,
                                  ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);

  if (NI_RETCODE_SUCCESS != retval)
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    ni_log(NI_LOG_TRACE, " xcoder_config_instance_flush(): NVME command Failed\n");
  }
#else
  CONFIG_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  CONFIG_INSTANCE_SET_DW11_FLUSH(cmd.cdw11, device_type);
  CONFIG_INSTANCE_SET_DW15_SIZE(cmd.cdw15, 0);

  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, p_ctx->device_handle, &cmd, 0, NULL, &nvme_result))
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    ni_log(NI_LOG_TRACE, " xcoder_config_instance_flush(): NVME command Failed\n");
  }
#endif

  END;

  ni_log(NI_LOG_TRACE, "xcoder_config_instance_flush(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_params(ni_session_context_t* p_ctx)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_encoder_config = NULL;
  ni_encoder_config_t* p_cfg = NULL;
  uint32_t buffer_size = sizeof(ni_encoder_config_t);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  int i = 0;
  ni_log(NI_LOG_TRACE, "ni_config_instance_set_encoder_params(): enter\n");

  if (!p_ctx)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_config_instance_set_encoder_params(): NULL pointer p_config passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (posix_memalign(&p_encoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate encConf buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  ni_set_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config);
  if (NI_RETCODE_SUCCESS != ni_validate_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config, p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg)))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_validate_custom_template failed. %s\n",
                   p_ctx->param_err_msg);
    ni_log(NI_LOG_INFO, "ERROR: ni_validate_custom_template failed. %s\n",
                   p_ctx->param_err_msg);
    fflush(stderr);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  // configure the session
#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
#else
  CONFIG_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  CONFIG_INSTANCE_SET_DW11_ENC_PARAMS(cmd.cdw11, NI_DEVICE_TYPE_ENCODER);
  CONFIG_INSTANCE_SET_DW15_SIZE(cmd.cdw15, buffer_size);
#endif

  //Flip the bytes!!
  p_cfg = (ni_encoder_config_t*)p_encoder_config;

  uint8_t str_vui[4 * NI_MAX_VUI_SIZE];
  for (i = 0; i < p_cfg->ui32VuiDataSizeBytes; i++)
  {
    snprintf(&str_vui[i * 3], 4, "%.2x ", p_cfg->ui8VuiRbsp[i]);
  }
  str_vui[3 * p_cfg->ui32VuiDataSizeBytes] = '\0';
  ni_log(NI_LOG_DEBUG, "VUI = %s\n", str_vui);

  p_cfg->i32picWidth = ni_htonl(p_cfg->i32picWidth);
  p_cfg->i32picHeight = ni_htonl(p_cfg->i32picHeight);
  p_cfg->i32meBlkMode = ni_htonl(p_cfg->i32meBlkMode);
  p_cfg->i32frameRateInfo = ni_htonl(p_cfg->i32frameRateInfo);
  p_cfg->i32vbvBufferSize = ni_htonl(p_cfg->i32vbvBufferSize);
  p_cfg->i32userQpMax = ni_htonl(p_cfg->i32userQpMax);
  p_cfg->i32maxIntraSize = ni_htonl(p_cfg->i32maxIntraSize);
  p_cfg->i32userMaxDeltaQp = ni_htonl(p_cfg->i32userMaxDeltaQp);
  p_cfg->i32userMinDeltaQp = ni_htonl(p_cfg->i32userMinDeltaQp);
  p_cfg->i32userQpMin = ni_htonl(p_cfg->i32userQpMin);
  p_cfg->i32bitRate = ni_htonl(p_cfg->i32bitRate);
  p_cfg->i32bitRateBL = ni_htonl(p_cfg->i32bitRateBL);
  p_cfg->i32srcBitDepth = ni_htonl(p_cfg->i32srcBitDepth);
  p_cfg->hdrEnableVUI = ni_htonl(p_cfg->hdrEnableVUI);
  p_cfg->ui32VuiDataSizeBits = ni_htonl(p_cfg->ui32VuiDataSizeBits);
  p_cfg->ui32VuiDataSizeBytes = ni_htonl(p_cfg->ui32VuiDataSizeBytes);
  p_cfg->ui32flushGop = ni_htonl(p_cfg->ui32flushGop);
  p_cfg->ui32minIntraRefreshCycle = ni_htonl(p_cfg->ui32minIntraRefreshCycle);
  p_cfg->ui32fillerEnable = ni_htonl(p_cfg->ui32fillerEnable);
  // no flipping reserved field as enableAUD now takes one byte from it

  // flip the NI_MAX_VUI_SIZE bytes of the VUI field using 32 bits pointers
  for (i = 0 ; i < (NI_MAX_VUI_SIZE >> 2) ; i++) // apply on 32 bits
  {
    ((uint32_t*)p_cfg->ui8VuiRbsp)[i] = ni_htonl(((uint32_t*)p_cfg->ui8VuiRbsp)[i]);
  }

#ifdef XCODER_IO_RW_ENABLED
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_encoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id));
  CHECK_VPU_RECOVERY(retval);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#else
  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, p_ctx->device_handle, &cmd, buffer_size, p_encoder_config, &nvme_result))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_admin_cmd failed: device_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_close failed: device_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    }
    
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#endif

  END;

  aligned_free(p_encoder_config);

  ni_log(NI_LOG_TRACE, "ni_config_instance_set_encoder_params(): exit\n");

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding p_frame parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_encoder_frame_params_t * params - pointer to the encoder ni_encoder_frame_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_frame_params(ni_session_context_t* p_ctx, ni_encoder_frame_params_t* p_params)
{
  ni_nvme_result_t nvme_result = 0;
  ni_nvme_command_t cmd = { 0 };
  void* p_encoder_config = NULL;
  ni_encoder_frame_params_t* p_cfg = (ni_encoder_frame_params_t*)p_encoder_config;
  uint32_t buffer_size = sizeof(ni_encoder_frame_params_t);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "ni_config_instance_set_encoder_frame_params(): enter\n");

  if ((!p_ctx) || (!p_params) || (!p_cfg))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_config_instance_set_encoder_frame_params(): NULL pointer p_config passed\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log(NI_LOG_TRACE, "ERROR: Invalid session ID, return\n");
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (posix_memalign(&p_encoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: Cannot allocate encConf buffer.\n", NI_XCODER_NUM_OF_SYSTEM_LAST_ERROR);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  //consigure the session here as per Farhan
#ifdef XCODER_IO_RW_ENABLED
  ui32LBA = CONFIG_INSTANCE_SetEncFramePara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
#else
  CONFIG_INSTANCE_SET_DW10_SUBTYPE(cmd.cdw10, p_ctx->session_id);
  CONFIG_INSTANCE_SET_DW11_ENC_FRAME_PARAMS(cmd.cdw11, NI_DEVICE_TYPE_ENCODER);
  CONFIG_INSTANCE_SET_DW15_SIZE(cmd.cdw15, buffer_size);
#endif

  //Flip the bytes!!
  p_cfg->force_picture_type = ni_htons(p_params->force_picture_type);
  p_cfg->data_format = ni_htons(p_params->data_format);
  p_cfg->picture_type = ni_htons(p_params->picture_type);
  p_cfg->video_width = ni_htons(p_params->video_width);
  p_cfg->video_height = ni_htons(p_params->video_height);
  p_cfg->timestamp = ni_htonl(p_params->timestamp);

#ifdef XCODER_IO_RW_ENABLED
  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_encoder_config, buffer_size, ui32LBA) < 0)
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it as per Farhan
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#else
  if (ni_nvme_send_admin_cmd(nvme_admin_cmd_xcoder_config, p_ctx->device_handle, &cmd, buffer_size, p_encoder_config, &p_ctx->session_id))
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_nvme_send_admin_cmd failed: device_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it as per Farhan
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log(NI_LOG_TRACE, "ERROR: ni_encoder_session_close failed: device_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }
#endif

  END;

  aligned_free(p_encoder_config);

  ni_log(NI_LOG_TRACE, "ni_config_instance_set_encoder_frame_params(): exit\n");

  return retval;
}

// return non-0 if SEI of requested type is found, 0 otherwise
static int find_sei(uint32_t sei_header, ni_sei_user_data_entry_t *pEntry,
                    ni_h265_sei_user_data_type_t type,
                    uint32_t *pSeiOffset, uint32_t *pSeiSize)
{
  int ret = 0;
  
  
  if ( (!pEntry) || (!pSeiOffset) || (!pSeiSize) )
  {
    return  ret;
  }
  
  
  if (sei_header & (1 << type))
  {
    *pSeiOffset = pEntry[type].offset;
    *pSeiSize = pEntry[type].size;
    ni_log(NI_LOG_TRACE, "find_sei sei type %d, offset: %u  size: %u\n",
                   type, *pSeiOffset, *pSeiSize);
    ret = 1;
  }
  
  return ret;
}

// return non-0 if prefix or suffix T.35 message is found
static int find_prefix_suffix_t35(uint32_t sei_header,
                                  ni_t35_sei_mesg_type_t t35_type,
                                  ni_sei_user_data_entry_t *pEntry,
                                  ni_h265_sei_user_data_type_t type,
                                  uint32_t *pCcOffset, uint32_t *pCcSize)
{
  int ret = 0;
  uint8_t *ptr;

  
  if ( (!pEntry) || (!pCcOffset) || (!pCcSize) )
  {
    return  ret;
  }
  

  // Find first t35 message with CEA708 close caption (first 7
  // bytes are itu_t_t35_country_code 0xB5 0x00 (181),
  // itu_t_t35_provider_code = 0x31 (49),
  // ATSC_user_identifier = 0x47 0x41 0x39 0x34 ("GA94")
  // or HDR10+ header bytes 
  if (sei_header & (1 << type))
  {
    ptr = (uint8_t*)pEntry + pEntry[type].offset;
    if (NI_T35_SEI_CLOSED_CAPTION == t35_type &&
        ptr[0] == NI_CC_SEI_BYTE0 && ptr[1] == NI_CC_SEI_BYTE1 &&
        ptr[2] == NI_CC_SEI_BYTE2 && ptr[3] == NI_CC_SEI_BYTE3 &&
        ptr[4] == NI_CC_SEI_BYTE4 && ptr[5] == NI_CC_SEI_BYTE5 &&
        ptr[6] == NI_CC_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_TRACE, "find_prefix_suffix_t35: close Caption SEI found in T.35 type %d, offset: %u  size: %u\n", type, *pCcOffset, *pCcSize);
      ret = 1;
    }
    else if (NI_T35_SEI_HDR10_PLUS == t35_type &&
             ptr[0] == NI_HDR10P_SEI_BYTE0 && ptr[1] == NI_HDR10P_SEI_BYTE1 &&
             ptr[2] == NI_HDR10P_SEI_BYTE2 && ptr[3] == NI_HDR10P_SEI_BYTE3 &&
             ptr[4] == NI_HDR10P_SEI_BYTE4 && ptr[5] == NI_HDR10P_SEI_BYTE5 &&
             ptr[6] == NI_HDR10P_SEI_BYTE6)
    {
      *pCcOffset = pEntry[type].offset;
      *pCcSize = pEntry[type].size;
      ni_log(NI_LOG_TRACE, "find_prefix_suffix_t35: HDR10+ SEI found in T.35 type %d, offset: %u  size: %u\n", type, *pCcOffset, *pCcSize);
      ret = 1;
    }
  }

  return ret;
}

// return non-0 when HDR10+/close-caption is found, 0 otherwise
static int find_t35_sei(uint32_t sei_header, ni_t35_sei_mesg_type_t t35_type,
                        ni_sei_user_data_entry_t *pEntry,
                        uint32_t *pCcOffset, uint32_t *pCcSize)
{
  int ret = 0;
  
  if ( (!pEntry) || (!pCcOffset) || (!pCcSize) )
  {
    return  ret;
  }
  
  *pCcOffset = *pCcSize = 0;

  // Check up to 3 T35 Prefix and Suffix SEI for close captions
  if (find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_1,
                             pCcOffset, pCcSize)  ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_PRE_2,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_1,
                             pCcOffset, pCcSize) ||
      find_prefix_suffix_t35(sei_header, t35_type, pEntry,
                             NI_H265_USERDATA_FLAG_ITU_T_T35_SUF_2,
                             pCcOffset, pCcSize)
    )
  {
    ret = 1;
  }
  return ret;
}

/*!******************************************************************************
 *  \brief  Get info from received p_frame
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_create_frame(ni_frame_t* p_frame, uint32_t read_length, uint64_t* p_frame_offset)
{
  int rx_size = read_length; //get the length since its the only thing in DW10 now
  
  if ( (!p_frame) || (!p_frame_offset) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_create_frame(): Null pointer parameters passed\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  uint8_t* p_buf = (uint8_t*)p_frame->p_buffer;

  *p_frame_offset = 0;

  int metadata_size = NI_FW_META_DATA_SZ;
  unsigned int video_data_size = p_frame->data_len[0] +
  p_frame->data_len[1] + p_frame->data_len[2];

  p_frame->p_custom_sei = NULL;
  p_frame->custom_sei_len = 0;

  if (rx_size == metadata_size)
  {
      video_data_size = 0;
  }

  if (rx_size > video_data_size)
  {
    ni_metadata_dec_frame_t* p_meta = (ni_metadata_dec_frame_t*)((uint8_t*)p_frame->p_buffer + video_data_size);


    *p_frame_offset = p_meta->metadata_common.ui64_data.frame_offset;
    rx_size -= metadata_size;
    p_frame->crop_top = p_meta->metadata_common.crop_top;
    p_frame->crop_bottom = p_meta->metadata_common.crop_bottom;
    p_frame->crop_left = p_meta->metadata_common.crop_left;
    p_frame->crop_right = p_meta->metadata_common.crop_right;
    p_frame->ni_pict_type = p_meta->metadata_common.frame_type;

    p_frame->video_width = p_meta->metadata_common.frame_width;
    p_frame->video_height = p_meta->metadata_common.frame_height;

    ni_log(NI_LOG_TRACE, "ni_create_frame: [metadata] cropRight=%u, cropLeft=%u, cropBottom=%u, cropTop=%u, frame_offset=%" PRIu64 ", pic=%ux%u, pict_type=%d, crop=%ux%u, sei header: 0x%0x  number %u  size %u\n", p_frame->crop_right, p_frame->crop_left, p_frame->crop_bottom, p_frame->crop_top, p_meta->metadata_common.ui64_data.frame_offset, p_meta->metadata_common.frame_width, p_meta->metadata_common.frame_height,
                   p_frame->ni_pict_type,
                   p_frame->crop_right - p_frame->crop_left,
                   p_frame->crop_bottom - p_frame->crop_top,
                   p_meta->sei_header, p_meta->sei_number,
                   p_meta->sei_size);

    p_frame->sei_total_len =
    p_frame->sei_cc_offset = p_frame->sei_cc_len =
    p_frame->sei_hdr_mastering_display_color_vol_offset =
    p_frame->sei_hdr_mastering_display_color_vol_len =
    p_frame->sei_hdr_content_light_level_info_offset =
    p_frame->sei_hdr_content_light_level_info_len =
    p_frame->sei_hdr_plus_offset = p_frame->sei_hdr_plus_len =
    p_frame->sei_user_data_unreg_offset = p_frame->sei_user_data_unreg_len = 0;
    if (p_meta->sei_header && p_meta->sei_number && p_meta->sei_size)
    {
      ni_sei_user_data_entry_t *pEntry;
      uint32_t ui32CCOffset = 0, ui32CCSize = 0;

      rx_size -= p_meta->sei_size;

      pEntry = (ni_sei_user_data_entry_t *)((uint8_t*)p_meta + metadata_size);

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_HDR10_PLUS, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_plus_len = ui32CCSize;
        p_frame->sei_hdr_plus_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_TRACE, "ni_create_frame: hdr10+ size=%u hdr10+ offset=%u\n",
                       p_frame->sei_hdr_plus_len, p_frame->sei_hdr_plus_offset);
      }
      else
      {
        ni_log(NI_LOG_TRACE, "ni_create_frame: hdr+ NOT found in meta data!\n");
      }

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_CLOSED_CAPTION, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        uint8_t *ptr;
        // Found CC data at pEntry + ui32CCOffset
        ptr = (uint8_t*)pEntry + ui32CCOffset;
        // number of 3 byte close captions is bottom 5 bits of
        // 9th byte of T35 payload
        ui32CCSize = (ptr[8] & 0x1F) * 3;

        // return close caption data offset and length, and
        // skip past 10 header bytes to close caption data
        p_frame->sei_cc_len = ui32CCSize;
        p_frame->sei_cc_offset = video_data_size + metadata_size
        + ui32CCOffset + 10;

        p_frame->sei_total_len += p_frame->sei_cc_len;

        ni_log(NI_LOG_TRACE, "ni_create_frame: close caption size %u ,"
                       "offset %u = video size %u meta size %u off "
                       " %u + 10\n", p_frame->sei_cc_len,
                       p_frame->sei_cc_offset, video_data_size,
                       metadata_size, ui32CCOffset);
      }
      else
      {
        ni_log(NI_LOG_TRACE, "ni_create_frame: close caption NOT found in meta data!\n");
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_MASTERING_COLOR_VOL,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_mastering_display_color_vol_len = ui32CCSize;
        p_frame->sei_hdr_mastering_display_color_vol_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_dec_mastering_display_colour_volume_t* pColourVolume =
        (ni_dec_mastering_display_colour_volume_t*)((uint8_t*)pEntry + ui32CCOffset);

        ni_log(NI_LOG_TRACE, "Display Primaries x[0]=%u y[0]=%u\n",
                       pColourVolume->display_primaries_x[0],
                       pColourVolume->display_primaries_y[0]);
        ni_log(NI_LOG_TRACE, "Display Primaries x[1]=%u y[1]=%u\n",
                       pColourVolume->display_primaries_x[1],
                       pColourVolume->display_primaries_y[1]);
        ni_log(NI_LOG_TRACE, "Display Primaries x[2]=%u y[2]=%u\n",
                       pColourVolume->display_primaries_x[2],
                       pColourVolume->display_primaries_y[2]);

        ni_log(NI_LOG_TRACE, "White Point x=%u y=%u\n",
                       pColourVolume->white_point_x,
                       pColourVolume->white_point_y);
        ni_log(NI_LOG_TRACE, "Display Mastering Lum, Max=%u Min=%u\n",
                       pColourVolume->max_display_mastering_luminance, pColourVolume->min_display_mastering_luminance);
      }
      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_content_light_level_info_len = ui32CCSize;
        p_frame->sei_hdr_content_light_level_info_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_content_light_level_info_t* pLightLevel =
        (ni_content_light_level_info_t*)((uint8_t*)pEntry + ui32CCOffset);
        ni_log(NI_LOG_TRACE, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
                       pLightLevel->max_content_light_level, pLightLevel->max_pic_average_light_level);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_PRE,
                   &ui32CCOffset, &ui32CCSize) ||
          find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_SUF,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_user_data_unreg_len = ui32CCSize;
        p_frame->sei_user_data_unreg_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_TRACE, "User Data Unreg size = %u, offset %u\n", ui32CCSize, ui32CCOffset);
      }

      if (0 == p_frame->sei_total_len)
      {
        ni_log(NI_LOG_DEBUG, "retrieved 0 supported SEI !");
      }
    }
  }

  p_frame->dts = 0;
  p_frame->pts = 0;
  //p_frame->end_of_stream = isEndOfStream;
  p_frame->start_of_stream = 0;

  if (rx_size == 0)
  {
    p_frame->data_len[0] = 0;
    p_frame->data_len[1] = 0;
    p_frame->data_len[2] = 0;
  }

  ni_log(NI_LOG_TRACE, "received [0x%08x] data size: %d, end of stream=%d\n", read_length, rx_size, p_frame->end_of_stream);

  return rx_size;
}

/*!******************************************************************************
 *  \brief  Get info from received xcoder capability
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_populate_device_capability_struct(ni_device_capability_t* p_cap, void* p_data)
{
  int i, total_modules;
  ni_nvme_identity_t* p_id_data = (ni_nvme_identity_t*)p_data;
  
  if ( (!p_cap) || (!p_data) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_populate_device_capability_struct(): Null pointer parameters passed\n");
    LRETURN;
  }

  if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
    (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
  {
    LRETURN;
  }

  memset(p_cap->fw_rev, 0, sizeof(p_cap->fw_rev));
  memcpy(p_cap->fw_rev, p_id_data->ai8Fr, sizeof(p_cap->fw_rev));
  ni_log(NI_LOG_TRACE, "F/W rev: %2.*s\n", (int)sizeof(p_cap->fw_rev),
                 p_cap->fw_rev);

  p_cap->device_is_xcoder = p_id_data->device_is_xcoder;
  ni_log(NI_LOG_TRACE, "device_is_xcoder: %d\n", p_cap->device_is_xcoder);
  if (0 == p_cap->device_is_xcoder)
  {
    LRETURN;
  }

  p_cap->hw_elements_cnt = p_id_data->xcoder_num_hw;
  if (3 == p_cap->hw_elements_cnt)
  {
    ni_log(NI_LOG_ERROR, "hw_elements_cnt is 3, Rev A NOT supported !\n");
    LRETURN;
  }

  p_cap->h264_decoders_cnt = p_id_data->xcoder_num_h264_decoder_hw;
  p_cap->h264_encoders_cnt = p_id_data->xcoder_num_h264_encoder_hw;
  p_cap->h265_decoders_cnt = p_id_data->xcoder_num_h265_decoder_hw;
  p_cap->h265_encoders_cnt = p_id_data->xcoder_num_h265_encoder_hw;
  ni_log(NI_LOG_TRACE, "hw_elements_cnt: %d\n", p_cap->hw_elements_cnt);
  ni_log(NI_LOG_TRACE, "h264_decoders_cnt: %d\n", p_cap->h264_decoders_cnt);
  ni_log(NI_LOG_TRACE, "h264_encoders_cnt: %d\n", p_cap->h264_encoders_cnt);
  ni_log(NI_LOG_TRACE, "h265_decoders_cnt: %d\n", p_cap->h265_decoders_cnt);
  ni_log(NI_LOG_TRACE, "h265_encoders_cnt: %d\n", p_cap->h265_encoders_cnt);

  total_modules = p_cap->h264_decoders_cnt + p_cap->h264_encoders_cnt +
    p_cap->h265_decoders_cnt + p_cap->h265_encoders_cnt;

  if (total_modules >= 1)
  {
    p_cap->xcoder_devices[0].hw_id = p_id_data->hw0_id;
    p_cap->xcoder_devices[0].max_number_of_contexts = 
    NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[0].max_1080p_fps = NI_MAX_1080P_FPS;
    p_cap->xcoder_devices[0].codec_format = p_id_data->hw0_codec_format;
    p_cap->xcoder_devices[0].codec_type = p_id_data->hw0_codec_type;
    p_cap->xcoder_devices[0].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[0].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[0].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[0].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[0].video_profile = p_id_data->hw0_video_profile;
    p_cap->xcoder_devices[0].video_level = p_id_data->hw0_video_level;
  }
  if (total_modules >= 2)
  {
    p_cap->xcoder_devices[1].hw_id = p_id_data->hw1_id;
    p_cap->xcoder_devices[1].max_number_of_contexts = 
    NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[1].max_1080p_fps = NI_MAX_1080P_FPS;
    p_cap->xcoder_devices[1].codec_format = p_id_data->hw1_codec_format;
    p_cap->xcoder_devices[1].codec_type = p_id_data->hw1_codec_type;
    p_cap->xcoder_devices[1].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[1].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[1].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[1].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[1].video_profile = p_id_data->hw1_video_profile;
    p_cap->xcoder_devices[1].video_level = p_id_data->hw1_video_level;
  }
  if (total_modules >= 3)
  {
    p_cap->xcoder_devices[2].hw_id = p_id_data->hw2_id;
    p_cap->xcoder_devices[2].max_number_of_contexts = 
    NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[2].max_1080p_fps = NI_MAX_1080P_FPS;
    p_cap->xcoder_devices[2].codec_format = p_id_data->hw2_codec_format;
    p_cap->xcoder_devices[2].codec_type = p_id_data->hw2_codec_type;
    p_cap->xcoder_devices[2].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[2].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[2].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[2].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[2].video_profile = p_id_data->hw2_video_profile;
    p_cap->xcoder_devices[2].video_level = p_id_data->hw2_video_level;
  }
  if (total_modules >= 4)
  {
    p_cap->xcoder_devices[3].hw_id = p_id_data->hw3_id;
    p_cap->xcoder_devices[3].max_number_of_contexts = 
    NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[3].max_1080p_fps = NI_MAX_1080P_FPS;
    p_cap->xcoder_devices[3].codec_format = p_id_data->hw3_codec_format;
    p_cap->xcoder_devices[3].codec_type = p_id_data->hw3_codec_type;
    p_cap->xcoder_devices[3].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[3].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[3].min_video_width = NI_PARAM_MIN_WIDTH;
    p_cap->xcoder_devices[3].min_video_height = NI_PARAM_MIN_HEIGHT;
    p_cap->xcoder_devices[3].video_profile = p_id_data->hw3_video_profile;
    p_cap->xcoder_devices[3].video_level = p_id_data->hw3_video_level;
  }

  for (i = 0; i < NI_MAX_DEVICES_PER_HW_INSTANCE; i++)
  {
    ni_log(NI_LOG_TRACE, "HW%d hw_id: %d\n", i, p_cap->xcoder_devices[i].hw_id);
    ni_log(NI_LOG_TRACE, "HW%d max_number_of_contexts: %d\n", i, p_cap->xcoder_devices[i].max_number_of_contexts);
    ni_log(NI_LOG_TRACE, "HW%d max_1080p_fps: %d\n", i, p_cap->xcoder_devices[i].max_1080p_fps);
    ni_log(NI_LOG_TRACE, "HW%d codec_format: %d\n", i, p_cap->xcoder_devices[i].codec_format);
    ni_log(NI_LOG_TRACE, "HW%d codec_type: %d\n", i, p_cap->xcoder_devices[i].codec_type);
    ni_log(NI_LOG_TRACE, "HW%d max_video_width: %d\n", i, p_cap->xcoder_devices[i].max_video_width);
    ni_log(NI_LOG_TRACE, "HW%d max_video_height: %d\n", i, p_cap->xcoder_devices[i].max_video_height);
    ni_log(NI_LOG_TRACE, "HW%d min_video_width: %d\n", i, p_cap->xcoder_devices[i].min_video_width);
    ni_log(NI_LOG_TRACE, "HW%d min_video_height: %d\n", i, p_cap->xcoder_devices[i].min_video_height);
    ni_log(NI_LOG_TRACE, "HW%d video_profile: %d\n", i, p_cap->xcoder_devices[i].video_profile);
    ni_log(NI_LOG_TRACE, "HW%d video_level: %d\n", i, p_cap->xcoder_devices[i].video_level);
  }

  memset(p_cap->fw_commit_hash, 0, sizeof(p_cap->fw_commit_hash));
  memcpy(p_cap->fw_commit_hash, p_id_data->fw_commit_hash, sizeof(p_cap->fw_commit_hash) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_commit_hash);
  memset(p_cap->fw_commit_time, 0, sizeof(p_cap->fw_commit_time));
  memcpy(p_cap->fw_commit_time, p_id_data->fw_commit_time, sizeof(p_cap->fw_commit_time) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_commit_time);
  memset(p_cap->fw_branch_name, 0, sizeof(p_cap->fw_branch_name));
  memcpy(p_cap->fw_branch_name, p_id_data->fw_branch_name, sizeof(p_cap->fw_branch_name) - 1);
  ni_log(NI_LOG_TRACE, "F/W commit hash: %s\n", p_cap->fw_branch_name);

  END;
  return;
}

static uint32_t presetGopSize[] = {
  1, /*! Custom GOP, Not used */
  1, /*! All Intra */
  1, /*! IPP Cyclic GOP size 1 */
  1, /*! IBB Cyclic GOP size 1 */
  2, /*! IBP Cyclic GOP size 2 */
  4, /*! IBBBP */
  4,
  4,
  8 };

static uint32_t presetGopKeyFrameFactor[] = {
  1, /*! Custom GOP, Not used */
  1, /*! All Intra */
  1, /*! IPP Cyclic GOP size 1 */
  1, /*! IBB Cyclic GOP size 1 */
  2, /*! IBP Cyclic GOP size 2 */
  4, /*! IBBBP */
  1,
  1,
  1 };

/*!******************************************************************************
 *  \brief  insert the 32 bits of integer value at bit position pos
 *
 *  \param int pos, int value
 *
 *  \return void
 ******************************************************************************/
void ni_fix_VUI(uint8_t *vui, int pos, int value)
{
  int pos_byte    = (pos/8);
  int pos_in_byte = pos%8;
  int remaining_bytes_in_current_byte = 8 - pos_in_byte;

  if (pos_in_byte == 0) // at beginning of the byte
  {
    vui[pos_byte] = (uint8_t)(value >> 24);
    vui[pos_byte+1] = (uint8_t)(value >> 16);
    vui[pos_byte+2] = (uint8_t)(value >> 8);
    vui[pos_byte+3] = (uint8_t)(value);
  }
  else
  {
    vui[pos_byte]   = vui[pos_byte] + (uint8_t)(value >> (32-remaining_bytes_in_current_byte));
    vui[pos_byte+1] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-8));
    vui[pos_byte+2] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-16));
    vui[pos_byte+3] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-24));
    vui[pos_byte+4] = vui[pos_byte+4] + ((uint8_t)(value << remaining_bytes_in_current_byte));
  }

}

/*!******************************************************************************
 *  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_custom_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_cfg,
  ni_encoder_params_t* p_src)
{
  
  ni_t408_config_t* p_t408 = &(p_cfg->niParamT408);
  ni_h265_encoder_params_t* p_enc = &p_src->hevc_enc_params;
  int i = 0;
  
  if ( (!p_ctx) || (!p_cfg) || (!p_src) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_set_custom_template() Null pointer parameters passed\n");
    return;
  }

  ni_set_default_template(p_ctx, p_cfg);

  if (p_cfg->i32picWidth != p_src->source_width)
  {
    p_cfg->i32picWidth = p_src->source_width;
  }

  if (p_cfg->i32picHeight != p_src->source_height)
  {
    p_cfg->i32picHeight = p_src->source_height;
  }

  if (p_t408->gop_preset_index != p_enc->gop_preset_index)
  {
    p_t408->gop_preset_index = p_enc->gop_preset_index;
  }

  if (p_t408->use_recommend_enc_params != p_enc->use_recommend_enc_params)
  {
    p_t408->use_recommend_enc_params = p_enc->use_recommend_enc_params;
  }

  // trans_rate, enable_hvs_qp_scale:
  // are not present in Rev B p_config

  if (p_cfg->ui8rcEnable != p_enc->rc.enable_rate_control)
  {
    p_cfg->ui8rcEnable = p_enc->rc.enable_rate_control;
  }

  if (p_src->bitrate != 0)
  {
    p_cfg->i32bitRate = p_src->bitrate;
  }

  if (p_t408->enable_cu_level_rate_control != p_enc->rc.enable_cu_level_rate_control)
  {
    p_t408->enable_cu_level_rate_control = p_enc->rc.enable_cu_level_rate_control;
  }

  if (p_t408->enable_hvs_qp != p_enc->rc.enable_hvs_qp)
  {
    p_t408->enable_hvs_qp = p_enc->rc.enable_hvs_qp;
  }

  if (p_t408->hvs_qp_scale != p_enc->rc.hvs_qp_scale)
  {
    p_t408->hvs_qp_scale = p_enc->rc.hvs_qp_scale;
  }

  if (p_t408->minQpI != p_enc->rc.min_qp)
  {
    p_t408->minQpI = p_enc->rc.min_qp;
  }

  if (p_t408->minQpP != p_enc->rc.min_qp)
  {
    p_t408->minQpP = p_enc->rc.min_qp;
  }

  if (p_t408->minQpB != p_enc->rc.min_qp)
  {
    p_t408->minQpB = p_enc->rc.min_qp;
  }

  if (p_t408->maxQpI != p_enc->rc.max_qp)
  {
    p_t408->maxQpI = p_enc->rc.max_qp;
  }

  if (p_t408->maxQpP != p_enc->rc.max_qp)
  {
    p_t408->maxQpP = p_enc->rc.max_qp;
  }

  if (p_t408->maxQpB != p_enc->rc.max_qp)
  {
    p_t408->maxQpB = p_enc->rc.max_qp;
  }
  // TBD intraMinQp and intraMaxQp are not configurable in Rev A; should it
  // be in Rev B?

  if (p_t408->max_delta_qp != p_enc->rc.max_delta_qp)
  {
    p_t408->max_delta_qp = p_enc->rc.max_delta_qp;
  }

  if (p_cfg->i32vbvBufferSize != p_enc->rc.rc_init_delay)
  {
    p_cfg->i32vbvBufferSize = p_enc->rc.rc_init_delay;
  }

  if (p_t408->intra_period != p_enc->intra_period)
  {
    p_t408->intra_period = p_enc->intra_period;
  }

  if (p_t408->roiEnable != p_enc->roi_enable)
  {
      p_t408->roiEnable = p_enc->roi_enable;
  }

  if (p_t408->useLongTerm != p_enc->long_term_ref_enable)
  {
    p_t408->useLongTerm = p_enc->long_term_ref_enable;
  }

  if (p_t408->losslessEnable != p_enc->lossless_enable)
  {
    p_t408->losslessEnable = p_enc->lossless_enable;
  }

  if (p_t408->conf_win_top != p_enc->conf_win_top)
  {
    p_t408->conf_win_top = p_enc->conf_win_top;
  }

  if (p_t408->conf_win_bottom != p_enc->conf_win_bottom)
  {
    p_t408->conf_win_bottom = p_enc->conf_win_bottom;
  }

  if (p_t408->conf_win_left != p_enc->conf_win_left)
  {
    p_t408->conf_win_left = p_enc->conf_win_left;
  }

  if (p_t408->conf_win_right != p_enc->conf_win_right)
  {
    p_t408->conf_win_right = p_enc->conf_win_right;
  }

  if (p_t408->avcIdrPeriod != p_enc->intra_period)
  {
    p_t408->avcIdrPeriod = p_enc->intra_period;
  }

  if (p_cfg->i32frameRateInfo != p_enc->frame_rate)
  {
    p_cfg->i32frameRateInfo = p_enc->frame_rate;
    p_t408->numUnitsInTick = 1000;
    if (p_src->fps_denominator != 0 &&
       (p_src->fps_number % p_src->fps_denominator) != 0)
    {
      p_t408->numUnitsInTick += 1;
      p_cfg->i32frameRateInfo += 1;
    }
    p_t408->timeScale = p_cfg->i32frameRateInfo * 1000;
    if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
    {
      p_t408->timeScale *= 2;
    }
  }

  if (p_t408->intra_qp != p_enc->rc.intra_qp)
  {
    p_t408->intra_qp = p_enc->rc.intra_qp;
  }

  // "repeatHeaders" value 1 (all I frames) maps to forcedHeaderEnable
  // value 2; all other values are ignored
  if (p_t408->forcedHeaderEnable != p_enc->forced_header_enable &&
     p_enc->forced_header_enable == NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES)
    p_t408->forcedHeaderEnable = 2;

  if (p_t408->decoding_refresh_type != p_enc->decoding_refresh_type)
  {
    p_t408->decoding_refresh_type = p_enc->decoding_refresh_type;
  }

  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    switch (p_t408->decoding_refresh_type)
    {
      case 0: // Non-IRAP I-p_frame
      {
        // intra_period set to user-configured (above), avcIdrPeriod set to 0
        p_t408->avcIdrPeriod = 0;
        break;
      }
      case 1: // CRA
      case 2: // IDR
      {
        // intra_period set to 0, avcIdrPeriod set to user-configured (above)
        p_t408->intra_period = 0;
        break;
      }
      default:
      {
        ni_log(NI_LOG_TRACE, "ERROR: ni_set_custom_template() unknown value for p_t408->decoding_refresh_type: %d\n", p_t408->decoding_refresh_type);
        break;
      }
    }
  }
  else if (STD_HEVC == p_cfg->ui8bitstreamFormat)
  {
    p_t408->avcIdrPeriod = 0;
  }

  // Rev. B: H.264 only parameters.
  if (p_t408->enable_transform_8x8 != p_enc->enable_transform_8x8)
  {
    p_t408->enable_transform_8x8 = p_enc->enable_transform_8x8;
  }

  if (p_t408->avc_slice_mode != p_enc->avc_slice_mode)
  {
    p_t408->avc_slice_mode = p_enc->avc_slice_mode;
  }

  if (p_t408->avc_slice_arg != p_enc->avc_slice_arg)
  {
    p_t408->avc_slice_arg = p_enc->avc_slice_arg;
  }

  if (p_t408->entropy_coding_mode != p_enc->entropy_coding_mode)
  {
    p_t408->entropy_coding_mode = p_enc->entropy_coding_mode;
  }

  // Rev. B: shared between HEVC and H.264
  if (p_t408->intra_mb_refresh_mode != p_enc->intra_mb_refresh_mode)
  {
    p_t408->intraRefreshMode = p_t408->intra_mb_refresh_mode =
    p_enc->intra_mb_refresh_mode;
  }

  if (p_t408->intra_mb_refresh_arg != p_enc->intra_mb_refresh_arg)
  {
    p_t408->intraRefreshArg = p_t408->intra_mb_refresh_arg =
    p_enc->intra_mb_refresh_arg;
  }

  // TBD Rev. B: could be shared for HEVC and H.264
  if (p_t408->enable_mb_level_rc != p_enc->rc.enable_mb_level_rc)
  {
    p_t408->enable_mb_level_rc = p_enc->rc.enable_mb_level_rc;
  }

  // profile setting: if user specified profile
  if (0 != p_enc->profile)
  {
    p_t408->profile = p_enc->profile;
  }

  if (p_t408->level != p_enc->level_idc)
  {
    p_t408->level = p_enc->level_idc;
  }

  // main, extended or baseline profile of 8 bit H.264 requires the following:
  // main:     profile = 2  transform8x8Enable = 0
  // extended: profile = 3  entropyCodingMode = 0, transform8x8Enable = 0
  // baseline: profile = 1  entropyCodingMode = 0, transform8x8Enable = 0 and
  //                        gop with no B frames (gopPresetIdx=1, 2, 6, or 0 
  //                        (custom with no B frames)
  if (STD_AVC == p_cfg->ui8bitstreamFormat && 8 == p_ctx->src_bit_depth)
  {
    if (2 == p_t408->profile)
    {
      p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_TRACE, "enable_transform_8x8 set to 0 for profile 2 (main)\n");
    }
    else if (3 == p_t408->profile || 1 == p_t408->profile)
    {
      p_t408->entropy_coding_mode = p_t408->enable_transform_8x8 = 0;
      ni_log(NI_LOG_TRACE, "entropy_coding_mode and enable_transform_8x8 set to 0 "
                     "for profile 3 (extended) or 1 (baseline)\n");
    }
  }

  if (GOP_PRESET_IDX_CUSTOM == p_t408->gop_preset_index)
  {
    p_t408->custom_gop_params.custom_gop_size = p_enc->custom_gop_params.custom_gop_size;
    for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
    {
      p_t408->custom_gop_params.pic_param[i].pic_type = p_enc->custom_gop_params.pic_param[i].pic_type;
      p_t408->custom_gop_params.pic_param[i].poc_offset = p_enc->custom_gop_params.pic_param[i].poc_offset;
      p_t408->custom_gop_params.pic_param[i].pic_qp = p_enc->custom_gop_params.pic_param[i].pic_qp + p_t408->intra_qp;
      p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0 = p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0;
      p_t408->custom_gop_params.pic_param[i].ref_poc_L0 = p_enc->custom_gop_params.pic_param[i].ref_poc_L0;
      p_t408->custom_gop_params.pic_param[i].ref_poc_L1 = p_enc->custom_gop_params.pic_param[i].ref_poc_L1;
      p_t408->custom_gop_params.pic_param[i].temporal_id = p_enc->custom_gop_params.pic_param[i].temporal_id;
    }
  }

  p_ctx->key_frame_type = p_t408->decoding_refresh_type; //Store to use when force key p_frame

  // forceFrameType=1 requires intraPeriod=0 and avcIdrPeriod=0 and gopPresetIdx=8
  if (1 == p_src->force_frame_type)
  {
    p_t408->intra_period = 0;
    p_t408->avcIdrPeriod = 0;
    p_t408->gop_preset_index = 8;
    p_ctx->force_frame_type = 1;
  }

  if (p_cfg->hdrEnableVUI != p_src->hdrEnableVUI)
  {
    p_cfg->hdrEnableVUI = p_src->hdrEnableVUI;
  }

  if (p_cfg->ui8EnableAUD != p_src->enable_aud)
  {
    p_cfg->ui8EnableAUD = p_src->enable_aud;
  }

  if (p_cfg->ui32minIntraRefreshCycle != p_src->ui32minIntraRefreshCycle)
  {
    p_cfg->ui32minIntraRefreshCycle = p_src->ui32minIntraRefreshCycle;
  }

  // set VUI info
  p_cfg->ui32VuiDataSizeBits = p_src->ui32VuiDataSizeBits;
  p_cfg->ui32VuiDataSizeBytes = p_src->ui32VuiDataSizeBytes;
  memcpy(p_cfg->ui8VuiRbsp, p_src->ui8VuiRbsp, NI_MAX_VUI_SIZE);
  if ((p_src->pos_num_units_in_tick > p_src->ui32VuiDataSizeBits) || (p_src->pos_time_scale > p_src->ui32VuiDataSizeBits))
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_set_custom_template() VUI filling error\n");
    return;
  }
  else
  {
    ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_num_units_in_tick, p_t408->numUnitsInTick);
    ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_time_scale, p_t408->timeScale);
  }

  // CRF mode forces the following setting:
  if (p_src->crf)
  {
    p_cfg->ui8rcEnable = 0;
    p_t408->intra_qp = p_src->crf;
    p_t408->enable_hvs_qp = 1;
    p_t408->hvs_qp_scale = 2;
    p_t408->max_delta_qp = 51;
    ni_log(NI_LOG_TRACE, "crf=%d forces the setting of: rcEnable=0, intraQP=%d,"
           " hvsQPEnable=1, hvsQPScale=2, maxDeltaQP=51.\n",
           p_src->crf, p_t408->intra_qp);
  }

  // CBR mode
  if ((p_cfg->ui32fillerEnable != p_src->cbr) && (p_cfg->ui8rcEnable == 1))
  {
    p_cfg->ui32fillerEnable = p_src->cbr;
  }

  // GOP flush
  if (p_cfg->ui32flushGop != p_src->ui32flushGop)
  {
    p_cfg->ui32flushGop = p_src->ui32flushGop;
  }

  ni_log(NI_LOG_DEBUG, "lowDelay=%d\n", p_src->low_delay_mode);
  ni_log(NI_LOG_DEBUG, "strictTimeout=%d\n", p_src->strict_timeout_mode);
  ni_log(NI_LOG_DEBUG, "crf=%u\n", p_src->crf);
  ni_log(NI_LOG_DEBUG, "cbr=%u\n", p_src->cbr);
  ni_log(NI_LOG_DEBUG, "ui32flushGop=%u\n", p_src->ui32flushGop);
  ni_log(NI_LOG_DEBUG, "ui8bitstreamFormat=%d\n", p_cfg->ui8bitstreamFormat);
  ni_log(NI_LOG_DEBUG, "i32picWidth=%d\n", p_cfg->i32picWidth);
  ni_log(NI_LOG_DEBUG, "i32picHeight=%d\n", p_cfg->i32picHeight);
  ni_log(NI_LOG_DEBUG, "i32meBlkMode=%d\n", p_cfg->i32meBlkMode);
  ni_log(NI_LOG_DEBUG, "ui8sliceMode=%d\n", p_cfg->ui8sliceMode);
  ni_log(NI_LOG_DEBUG, "i32frameRateInfo=%d\n", p_cfg->i32frameRateInfo);
  ni_log(NI_LOG_DEBUG, "i32vbvBufferSize=%d\n", p_cfg->i32vbvBufferSize);
  ni_log(NI_LOG_DEBUG, "i32userQpMax=%d\n", p_cfg->i32userQpMax);

  ni_log(NI_LOG_DEBUG, "i32maxIntraSize=%d\n", p_cfg->i32maxIntraSize);
  ni_log(NI_LOG_DEBUG, "i32userMaxDeltaQp=%d\n", p_cfg->i32userMaxDeltaQp);
  ni_log(NI_LOG_DEBUG, "i32userMinDeltaQp=%d\n", p_cfg->i32userMinDeltaQp);
  ni_log(NI_LOG_DEBUG, "i32userQpMin=%d\n", p_cfg->i32userQpMin);
  ni_log(NI_LOG_DEBUG, "i32bitRate=%d\n", p_cfg->i32bitRate);
  ni_log(NI_LOG_DEBUG, "i32bitRateBL=%d\n", p_cfg->i32bitRateBL);
  ni_log(NI_LOG_DEBUG, "ui8rcEnable=%d\n", p_cfg->ui8rcEnable);
  ni_log(NI_LOG_DEBUG, "i32srcBitDepth=%d\n", p_cfg->i32srcBitDepth);
  ni_log(NI_LOG_DEBUG, "ui8enablePTS=%d\n", p_cfg->ui8enablePTS);
  ni_log(NI_LOG_DEBUG, "ui8lowLatencyMode=%d\n", p_cfg->ui8lowLatencyMode);
  ni_log(NI_LOG_DEBUG, "ui32sourceEndian=%d\n", p_cfg->ui32sourceEndian);
  ni_log(NI_LOG_DEBUG, "hdrEnableVUI=%u\n", p_cfg->hdrEnableVUI);
  ni_log(NI_LOG_DEBUG, "ui32minIntraRefreshCycle=%u\n",
         p_cfg->ui32minIntraRefreshCycle);
  ni_log(NI_LOG_DEBUG, "ui32fillerEnable=%u\n", p_cfg->ui32fillerEnable);

  ni_log(NI_LOG_DEBUG, "** ni_t408_config_t: \n");
  ni_log(NI_LOG_DEBUG, "profile=%d\n", p_t408->profile);
  ni_log(NI_LOG_DEBUG, "level=%d\n", p_t408->level);
  ni_log(NI_LOG_DEBUG, "tier=%d\n", p_t408->tier);

  ni_log(NI_LOG_DEBUG, "internalBitDepth=%d\n", p_t408->internalBitDepth);
  ni_log(NI_LOG_DEBUG, "losslessEnable=%d\n", p_t408->losslessEnable);
  ni_log(NI_LOG_DEBUG, "constIntraPredFlag=%d\n", p_t408->constIntraPredFlag);

  ni_log(NI_LOG_DEBUG, "decoding_refresh_type=%d\n", p_t408->decoding_refresh_type);
  ni_log(NI_LOG_DEBUG, "intra_qp=%d\n", p_t408->intra_qp);
  ni_log(NI_LOG_DEBUG, "intra_period=%d\n", p_t408->intra_period);
  ni_log(NI_LOG_DEBUG, "roi_enable=%d\n", p_t408->roiEnable);
  ni_log(NI_LOG_DEBUG, "useLongTerm=%u\n", p_t408->useLongTerm);

  ni_log(NI_LOG_DEBUG, "conf_win_top=%d\n", p_t408->conf_win_top);
  ni_log(NI_LOG_DEBUG, "conf_win_bottom=%d\n", p_t408->conf_win_bottom);
  ni_log(NI_LOG_DEBUG, "conf_win_left=%d\n", p_t408->conf_win_left);
  ni_log(NI_LOG_DEBUG, "conf_win_right=%d\n", p_t408->conf_win_right);

  ni_log(NI_LOG_DEBUG, "independSliceMode=%d\n", p_t408->independSliceMode);
  ni_log(NI_LOG_DEBUG, "independSliceModeArg=%d\n", p_t408->independSliceModeArg);

  ni_log(NI_LOG_DEBUG, "dependSliceMode=%d\n", p_t408->dependSliceMode);
  ni_log(NI_LOG_DEBUG, "dependSliceModeArg=%d\n", p_t408->dependSliceModeArg);

  ni_log(NI_LOG_DEBUG, "intraRefreshMode=%d\n", p_t408->intraRefreshMode);

  ni_log(NI_LOG_DEBUG, "intraRefreshArg=%d\n", p_t408->intraRefreshArg);

  ni_log(NI_LOG_DEBUG, "use_recommend_enc_params=%d\n", p_t408->use_recommend_enc_params);
  ni_log(NI_LOG_DEBUG, "scalingListEnable=%d\n", p_t408->scalingListEnable);

  ni_log(NI_LOG_DEBUG, "cu_size_mode=%d\n", p_t408->cu_size_mode);
  ni_log(NI_LOG_DEBUG, "tmvpEnable=%d\n", p_t408->tmvpEnable);
  ni_log(NI_LOG_DEBUG, "wppEnable=%d\n", p_t408->wppEnable);
  ni_log(NI_LOG_DEBUG, "max_num_merge=%d\n", p_t408->max_num_merge);
  ni_log(NI_LOG_DEBUG, "disableDeblk=%d\n", p_t408->disableDeblk);
  ni_log(NI_LOG_DEBUG, "lfCrossSliceBoundaryEnable=%d\n", p_t408->lfCrossSliceBoundaryEnable);
  ni_log(NI_LOG_DEBUG, "betaOffsetDiv2=%d\n", p_t408->betaOffsetDiv2);
  ni_log(NI_LOG_DEBUG, "tcOffsetDiv2=%d\n", p_t408->tcOffsetDiv2);
  ni_log(NI_LOG_DEBUG, "skipIntraTrans=%d\n", p_t408->skipIntraTrans);
  ni_log(NI_LOG_DEBUG, "saoEnable=%d\n", p_t408->saoEnable);
  ni_log(NI_LOG_DEBUG, "intraNxNEnable=%d\n", p_t408->intraNxNEnable);
  ni_log(NI_LOG_DEBUG, "bitAllocMode=%d\n", p_t408->bitAllocMode);

  ni_log(NI_LOG_DEBUG, "enable_cu_level_rate_control=%d\n", p_t408->enable_cu_level_rate_control);

  ni_log(NI_LOG_DEBUG, "enable_hvs_qp=%d\n", p_t408->enable_hvs_qp);

  ni_log(NI_LOG_DEBUG, "hvs_qp_scale=%d\n", p_t408->hvs_qp_scale);

  ni_log(NI_LOG_DEBUG, "max_delta_qp=%d\n", p_t408->max_delta_qp);

  // CUSTOM_GOP
  ni_log(NI_LOG_DEBUG, "gop_preset_index=%d\n", p_t408->gop_preset_index);
  if (p_t408->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
  {
    ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
    for (i = 0; i < 8; i++)
    //for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
    {
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_type);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].poc_offset);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_qp);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L0);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L1);
      ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_t408->custom_gop_params.pic_param[i].temporal_id);
    }
  }

  ni_log(NI_LOG_DEBUG, "roiEnable=%d\n", p_t408->roiEnable);

  ni_log(NI_LOG_DEBUG, "numUnitsInTick=%d\n", p_t408->numUnitsInTick);
  ni_log(NI_LOG_DEBUG, "timeScale=%d\n", p_t408->timeScale);
  ni_log(NI_LOG_DEBUG, "numTicksPocDiffOne=%d\n", p_t408->numTicksPocDiffOne);

  ni_log(NI_LOG_DEBUG, "chromaCbQpOffset=%d\n", p_t408->chromaCbQpOffset);
  ni_log(NI_LOG_DEBUG, "chromaCrQpOffset=%d\n", p_t408->chromaCrQpOffset);

  ni_log(NI_LOG_DEBUG, "initialRcQp=%d\n", p_t408->initialRcQp);

  ni_log(NI_LOG_DEBUG, "nrYEnable=%d\n", p_t408->nrYEnable);
  ni_log(NI_LOG_DEBUG, "nrCbEnable=%d\n", p_t408->nrCbEnable);
  ni_log(NI_LOG_DEBUG, "nrCrEnable=%d\n", p_t408->nrCrEnable);

  // ENC_NR_WEIGHT
  ni_log(NI_LOG_DEBUG, "nrIntraWeightY=%d\n", p_t408->nrIntraWeightY);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCb=%d\n", p_t408->nrIntraWeightCb);
  ni_log(NI_LOG_DEBUG, "nrIntraWeightCr=%d\n", p_t408->nrIntraWeightCr);
  ni_log(NI_LOG_DEBUG, "nrInterWeightY=%d\n", p_t408->nrInterWeightY);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCb=%d\n", p_t408->nrInterWeightCb);
  ni_log(NI_LOG_DEBUG, "nrInterWeightCr=%d\n", p_t408->nrInterWeightCr);

  ni_log(NI_LOG_DEBUG, "nrNoiseEstEnable=%d\n", p_t408->nrNoiseEstEnable);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaY=%d\n", p_t408->nrNoiseSigmaY);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCb=%d\n", p_t408->nrNoiseSigmaCb);
  ni_log(NI_LOG_DEBUG, "nrNoiseSigmaCr=%d\n", p_t408->nrNoiseSigmaCr);

  ni_log(NI_LOG_DEBUG, "useLongTerm=%d\n", p_t408->useLongTerm);

  // newly added for T408_520
  ni_log(NI_LOG_DEBUG, "monochromeEnable=%d\n", p_t408->monochromeEnable);
  ni_log(NI_LOG_DEBUG, "strongIntraSmoothEnable=%d\n", p_t408->strongIntraSmoothEnable);

  ni_log(NI_LOG_DEBUG, "weightPredEnable=%d\n", p_t408->weightPredEnable);
  ni_log(NI_LOG_DEBUG, "bgDetectEnable=%d\n", p_t408->bgDetectEnable);
  ni_log(NI_LOG_DEBUG, "bgThrDiff=%d\n", p_t408->bgThrDiff);
  ni_log(NI_LOG_DEBUG, "bgThrMeanDiff=%d\n", p_t408->bgThrMeanDiff);
  ni_log(NI_LOG_DEBUG, "bgLambdaQp=%d\n", p_t408->bgLambdaQp);
  ni_log(NI_LOG_DEBUG, "bgDeltaQp=%d\n", p_t408->bgDeltaQp);

  ni_log(NI_LOG_DEBUG, "customLambdaEnable=%d\n", p_t408->customLambdaEnable);
  ni_log(NI_LOG_DEBUG, "customMDEnable=%d\n", p_t408->customMDEnable);
  ni_log(NI_LOG_DEBUG, "pu04DeltaRate=%d\n", p_t408->pu04DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08DeltaRate=%d\n", p_t408->pu08DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16DeltaRate=%d\n", p_t408->pu16DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32DeltaRate=%d\n", p_t408->pu32DeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraPlanarDeltaRate=%d\n", p_t408->pu04IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraDcDeltaRate=%d\n", p_t408->pu04IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu04IntraAngleDeltaRate=%d\n", p_t408->pu04IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraPlanarDeltaRate=%d\n", p_t408->pu08IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraDcDeltaRate=%d\n", p_t408->pu08IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu08IntraAngleDeltaRate=%d\n", p_t408->pu08IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraPlanarDeltaRate=%d\n", p_t408->pu16IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraDcDeltaRate=%d\n", p_t408->pu16IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu16IntraAngleDeltaRate=%d\n", p_t408->pu16IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraPlanarDeltaRate=%d\n", p_t408->pu32IntraPlanarDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraDcDeltaRate=%d\n", p_t408->pu32IntraDcDeltaRate);
  ni_log(NI_LOG_DEBUG, "pu32IntraAngleDeltaRate=%d\n", p_t408->pu32IntraAngleDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08IntraDeltaRate=%d\n", p_t408->cu08IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08InterDeltaRate=%d\n", p_t408->cu08InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu08MergeDeltaRate=%d\n", p_t408->cu08MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16IntraDeltaRate=%d\n", p_t408->cu16IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16InterDeltaRate=%d\n", p_t408->cu16InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu16MergeDeltaRate=%d\n", p_t408->cu16MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32IntraDeltaRate=%d\n", p_t408->cu32IntraDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32InterDeltaRate=%d\n", p_t408->cu32InterDeltaRate);
  ni_log(NI_LOG_DEBUG, "cu32MergeDeltaRate=%d\n", p_t408->cu32MergeDeltaRate);
  ni_log(NI_LOG_DEBUG, "coefClearDisable=%d\n", p_t408->coefClearDisable);
  ni_log(NI_LOG_DEBUG, "minQpI=%d\n", p_t408->minQpI);
  ni_log(NI_LOG_DEBUG, "maxQpI=%d\n", p_t408->maxQpI);
  ni_log(NI_LOG_DEBUG, "minQpP=%d\n", p_t408->minQpP);
  ni_log(NI_LOG_DEBUG, "maxQpP=%d\n", p_t408->maxQpP);
  ni_log(NI_LOG_DEBUG, "minQpB=%d\n", p_t408->minQpB);
  ni_log(NI_LOG_DEBUG, "maxQpB=%d\n", p_t408->maxQpB);

  // for H.264 on T408
  ni_log(NI_LOG_DEBUG, "avcIdrPeriod=%d\n", p_t408->avcIdrPeriod);
  ni_log(NI_LOG_DEBUG, "rdoSkip=%d\n", p_t408->rdoSkip);
  ni_log(NI_LOG_DEBUG, "lambdaScalingEnable=%d\n", p_t408->lambdaScalingEnable);
  ni_log(NI_LOG_DEBUG, "enable_transform_8x8=%d\n", p_t408->enable_transform_8x8);
  ni_log(NI_LOG_DEBUG, "avc_slice_mode=%d\n", p_t408->avc_slice_mode);
  ni_log(NI_LOG_DEBUG, "avc_slice_arg=%d\n", p_t408->avc_slice_arg);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_mode=%d\n", p_t408->intra_mb_refresh_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_arg=%d\n", p_t408->intra_mb_refresh_arg);
  ni_log(NI_LOG_DEBUG, "enable_mb_level_rc=%d\n", p_t408->enable_mb_level_rc);
  ni_log(NI_LOG_DEBUG, "entropy_coding_mode=%d\n", p_t408->entropy_coding_mode);
  ni_log(NI_LOG_DEBUG, "forcedHeaderEnable=%d\n", p_t408->forcedHeaderEnable);
}

/*!******************************************************************************
 *  \brief  Setup and initialize all xcoder configuration to default (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_default_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_config)
{
  uint8_t i = 0;

  if ( (!p_ctx) || (!p_config) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_set_default_template() Null pointer parameters passed\n");
    return;
  }
  
  memset(p_config, 0, sizeof(ni_encoder_config_t));

  // fill in common attributes values
  p_config->i32picWidth = 720;
  p_config->i32picHeight = 480;
  p_config->i32meBlkMode = 0; // (AVC ONLY) syed: 0 means use all possible block partitions
  p_config->ui8sliceMode = 0; // syed: 0 means 1 slice per picture
  p_config->i32frameRateInfo = 30;
  p_config->i32vbvBufferSize = 3000; //0; // syed: parameter is ignored if rate control is off, if rate control is on, 0 means do not check vbv constraints
  p_config->i32userQpMax = 51;       // syed todo: this should also be h264-only parameter

  // AVC only
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->i32maxIntraSize = 8000000; // syed: how big an intra p_frame can get?
    p_config->i32userMaxDeltaQp = 51;
    p_config->i32userMinDeltaQp = 51;
    p_config->i32userQpMin = 8;
  }

  p_config->i32bitRate = 0;   //1000000; // syed todo: check if this is applicable (could be coda9 only)
  p_config->i32bitRateBL = 0; // syed todo: no documentation on this parameter in documents
  p_config->ui8rcEnable = 0;
  p_config->i32srcBitDepth = p_ctx->src_bit_depth;
  p_config->ui8enablePTS = 0;
  p_config->ui8lowLatencyMode = 0;

  // profiles for H.264: 1 = baseline, 2 = main, 3 = extended, 4 = high
  //                     5 = high10  (default 8 bit: 4, 10 bit: 5)
  // profiles for HEVC:  1 = main, 2 = main10  (default 8 bit: 1, 10 bit: 2)

  // bitstream type: H.264 or HEVC
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->ui8bitstreamFormat = STD_AVC;
    
    p_config->niParamT408.profile = 4;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 5;
    }
  }
  else
  {
    ni_assert(NI_CODEC_FORMAT_H265 == p_ctx->codec_format);

    p_config->ui8bitstreamFormat = STD_HEVC;
    
    p_config->niParamT408.profile = 1;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 2;
    }
  }

  p_config->ui32fillerEnable = 0;
  p_config->hdrEnableVUI = 0;
  p_config->ui8EnableAUD = 0;
  p_config->ui32flushGop = 0;
  p_config->ui32minIntraRefreshCycle = 0;
  p_config->ui32sourceEndian = p_ctx->src_endian;

  p_config->niParamT408.level = 0;   // TBD
  p_config->niParamT408.tier = 0;    // syed 0 means main tier

  p_config->niParamT408.internalBitDepth = p_ctx->src_bit_depth;
  p_config->niParamT408.losslessEnable = 0;
  p_config->niParamT408.constIntraPredFlag = 0;

  p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_IBBBP;

  p_config->niParamT408.decoding_refresh_type = 2;
  p_config->niParamT408.intra_qp = NI_DEFAULT_INTRA_QP;
  // avcIdrPeriod (H.264 on T408), NOT shared with intra_period
  p_config->niParamT408.intra_period = 92;
  p_config->niParamT408.avcIdrPeriod = 92;

  p_config->niParamT408.conf_win_top = 0;
  p_config->niParamT408.conf_win_bottom = 0;
  p_config->niParamT408.conf_win_left = 0;
  p_config->niParamT408.conf_win_right = 0;

  p_config->niParamT408.independSliceMode = 0;
  p_config->niParamT408.independSliceModeArg = 0;
  p_config->niParamT408.dependSliceMode = 0;
  p_config->niParamT408.dependSliceModeArg = 0;
  p_config->niParamT408.intraRefreshMode = 0;
  p_config->niParamT408.intraRefreshArg = 0;

  p_config->niParamT408.use_recommend_enc_params = 0; //1;
  p_config->niParamT408.scalingListEnable = 0;

  p_config->niParamT408.cu_size_mode = NI_DEFAULT_CU_SIZE_MODE;  //It is hardcode the default value is 7 which enable 8x8, 16x16, 32x32 coding unit size
  p_config->niParamT408.tmvpEnable = 1;
  p_config->niParamT408.wppEnable = 0;
  p_config->niParamT408.max_num_merge = 2;  // It is hardcode the max merge candidates default 2
  p_config->niParamT408.disableDeblk = 0;
  p_config->niParamT408.lfCrossSliceBoundaryEnable = 1;
  p_config->niParamT408.betaOffsetDiv2 = 0;
  p_config->niParamT408.tcOffsetDiv2 = 0;
  p_config->niParamT408.skipIntraTrans = 1; // syed todo: do more investigation
  p_config->niParamT408.saoEnable = 1;
  p_config->niParamT408.intraNxNEnable = 1;

  p_config->niParamT408.bitAllocMode = 0;

  for (i = 0; i < NI_MAX_GOP_NUM; i++)
  {
    p_config->niParamT408.fixedBitRatio[i] = 1;
  }

  p_config->niParamT408.enable_cu_level_rate_control = 1; //0;

  p_config->niParamT408.enable_hvs_qp = 0;
  p_config->niParamT408.hvs_qp_scale = 2; // syed todo: do more investigation

  p_config->niParamT408.max_delta_qp = NI_DEFAULT_MAX_DELTA_QP;

  // CUSTOM_GOP
  p_config->niParamT408.custom_gop_params.custom_gop_size = 0;
  for (i = 0; i < p_config->niParamT408.custom_gop_params.custom_gop_size; i++)
  {
    p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
    p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].pic_qp = 0;
    // ToDo: value of added num_ref_pic_L0 ???
    p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L0 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L1 = 0;
    p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
  }

  p_config->niParamT408.roiEnable = 0;

  p_config->niParamT408.numUnitsInTick = 1000;
  p_config->niParamT408.timeScale = p_config->i32frameRateInfo * 1000;
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->niParamT408.timeScale *= 2;
  }

  p_config->niParamT408.numTicksPocDiffOne = 0; // syed todo: verify, set to zero to try to match the model's output encoding

  p_config->niParamT408.chromaCbQpOffset = 0;
  p_config->niParamT408.chromaCrQpOffset = 0;

  p_config->niParamT408.initialRcQp = 63; //-1;

  p_config->niParamT408.nrYEnable = 0;
  p_config->niParamT408.nrCbEnable = 0;
  p_config->niParamT408.nrCrEnable = 0;

  // ENC_NR_WEIGHT
  p_config->niParamT408.nrIntraWeightY = 7;
  p_config->niParamT408.nrIntraWeightCb = 7;
  p_config->niParamT408.nrIntraWeightCr = 7;
  p_config->niParamT408.nrInterWeightY = 4;
  p_config->niParamT408.nrInterWeightCb = 4;
  p_config->niParamT408.nrInterWeightCr = 4;

  p_config->niParamT408.nrNoiseEstEnable = 0;
  p_config->niParamT408.nrNoiseSigmaY = 0;
  p_config->niParamT408.nrNoiseSigmaCb = 0;
  p_config->niParamT408.nrNoiseSigmaCr = 0;

  p_config->niParamT408.useLongTerm = 0; // syed: keep disabled for now, need to experiment later

  // newly added for T408_520
  p_config->niParamT408.monochromeEnable = 0; // syed: do we expect monochrome input?
  p_config->niParamT408.strongIntraSmoothEnable = 1;

  p_config->niParamT408.weightPredEnable = 0; //1; // syed: enabling for better quality but need to keep an eye on performance penalty
  p_config->niParamT408.bgDetectEnable = 0;
  p_config->niParamT408.bgThrDiff = 8;     // syed: matching the C-model
  p_config->niParamT408.bgThrMeanDiff = 1; // syed: matching the C-model
  p_config->niParamT408.bgLambdaQp = 32;   // syed: matching the C-model
  p_config->niParamT408.bgDeltaQp = 3;     // syed: matching the C-model

  p_config->niParamT408.customLambdaEnable = 0;
  p_config->niParamT408.customMDEnable = 0;
  p_config->niParamT408.pu04DeltaRate = 0;
  p_config->niParamT408.pu08DeltaRate = 0;
  p_config->niParamT408.pu16DeltaRate = 0;
  p_config->niParamT408.pu32DeltaRate = 0;
  p_config->niParamT408.pu04IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu04IntraDcDeltaRate = 0;
  p_config->niParamT408.pu04IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu08IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu08IntraDcDeltaRate = 0;
  p_config->niParamT408.pu08IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu16IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu16IntraDcDeltaRate = 0;
  p_config->niParamT408.pu16IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu32IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu32IntraDcDeltaRate = 0;
  p_config->niParamT408.pu32IntraAngleDeltaRate = 0;
  p_config->niParamT408.cu08IntraDeltaRate = 0;
  p_config->niParamT408.cu08InterDeltaRate = 0;
  p_config->niParamT408.cu08MergeDeltaRate = 0;
  p_config->niParamT408.cu16IntraDeltaRate = 0;
  p_config->niParamT408.cu16InterDeltaRate = 0;
  p_config->niParamT408.cu16MergeDeltaRate = 0;
  p_config->niParamT408.cu32IntraDeltaRate = 0;
  p_config->niParamT408.cu32InterDeltaRate = 0;
  p_config->niParamT408.cu32MergeDeltaRate = 0;
  p_config->niParamT408.coefClearDisable = 0;
  p_config->niParamT408.minQpI = 8;
  p_config->niParamT408.maxQpI = 51;
  p_config->niParamT408.minQpP = 8;
  p_config->niParamT408.maxQpP = 51;
  p_config->niParamT408.minQpB = 8;
  p_config->niParamT408.maxQpB = 51;

  // for H.264 on T408
  p_config->niParamT408.avcIdrPeriod = 92; // syed todo: check that 0 means encoder decides
  p_config->niParamT408.rdoSkip = 0;
  p_config->niParamT408.lambdaScalingEnable = 0;
  p_config->niParamT408.enable_transform_8x8 = 1;
  p_config->niParamT408.avc_slice_mode = 0;
  p_config->niParamT408.avc_slice_arg = 0;
  p_config->niParamT408.intra_mb_refresh_mode = 0;
  p_config->niParamT408.intra_mb_refresh_arg = 0;
  p_config->niParamT408.enable_mb_level_rc = 1;
  p_config->niParamT408.entropy_coding_mode = 1; // syed: 1 means CABAC, make sure profile is main or above, can't have CABAC in baseline
  p_config->niParamT408.forcedHeaderEnable = 0; // first IDR frame
}

/*!******************************************************************************
 *  \brief  Perform validation on custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_validate_custom_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_cfg,
  ni_encoder_params_t* p_src, char* p_param_err, uint32_t max_err_len)
{
  ni_retcode_t param_ret = NI_RETCODE_SUCCESS;
  int i;
  
  if ( (!p_ctx) || (!p_cfg) || (!p_src) || (!p_param_err) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_validate_custom_template() Null pointer parameters passed\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (0 == p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid frame_rate of 0 value", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_FRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate <= p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid i32bitRate: smaller than or equal to frame rate", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate > 700000000)
  {
    strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate < 0)
  {
    strncpy(p_param_err, "Invalid i32bitRate of 0 value", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_src->source_width < XCODER_MIN_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_width > XCODER_MAX_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_height < XCODER_MIN_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (p_src->source_height > XCODER_MAX_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  // number of MB (AVC, default) or CTU (HEVC) per row/column
  int32_t num_mb_or_ctu_row = (p_src->source_height + 16 - 1) / 16;
  int32_t num_mb_or_ctu_col = (p_src->source_width + 16 - 1) / 16;
  if (NI_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    num_mb_or_ctu_row = (p_src->source_height + 64 - 1) / 64;
    num_mb_or_ctu_col = (p_src->source_width + 64 - 1) / 64;
  }

  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    strncpy(p_param_err, "Invalid intraRefreshMode: 4 not supported for AVC",
            max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg <= 0)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should be greater than 0",
            max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }
  if (1 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg > num_mb_or_ctu_row)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of height when intraRefreshMode=1", max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }
  if (2 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.intra_mb_refresh_arg > num_mb_or_ctu_col)
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of width when intraRefreshMode=2", max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }
  if ((3 == p_cfg->niParamT408.intra_mb_refresh_mode ||
       4 == p_cfg->niParamT408.intra_mb_refresh_mode) &&
      (p_cfg->niParamT408.intra_mb_refresh_arg >
       num_mb_or_ctu_row * num_mb_or_ctu_col))
  {
    strncpy(p_param_err, "Invalid intraRefreshArg: should not be greater than "
            "number of MB/CTU of frame when intraRefreshMode=3/4", max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }
  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.losslessEnable)
  {
    strncpy(p_param_err, "Error: lossless coding should be disabled when "
            "intraRefreshMode=4", max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }
  if (4 == p_cfg->niParamT408.intra_mb_refresh_mode &&
      p_cfg->niParamT408.roiEnable)
  {
    strncpy(p_param_err, "Error: ROI should be disabled when "
            "intraRefreshMode=4", max_err_len);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    if (10 == p_ctx->src_bit_depth)
    {
      if (p_cfg->niParamT408.profile != 5)
      {
        strncpy(p_param_err, "Invalid profile: must be 5 (high10)",
                max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }
    }
    else
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                " 3 (extended), 4 (high), or 5 (high10)", max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }

      if (1 == p_cfg->niParamT408.profile &&
          ! (0 == p_cfg->niParamT408.gop_preset_index ||
             1 == p_cfg->niParamT408.gop_preset_index ||
             2 == p_cfg->niParamT408.gop_preset_index ||
             6 == p_cfg->niParamT408.gop_preset_index))
      {
        strncpy(p_param_err, "Invalid gopPresetIdx for H.264 baseline profile:"
                " must be 1, 2, 6 or 0 (custom with no B frames)", max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }

      if (1 == p_cfg->niParamT408.profile &&
          GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size;
             i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                    "have B frames", max_err_len);
            return NI_RETCODE_INVALID_PARAM;
          }
        }
      }
    }

    if (1 == p_cfg->niParamT408.avc_slice_mode)
    {
      // validate range of avcSliceArg: 1 - number-of-MBs-in-frame
      uint32_t numMbs = ((p_cfg->i32picWidth + 16 - 1) >> 4) *
      ((p_cfg->i32picHeight + 16 - 1) >> 4);
      if (p_cfg->niParamT408.avc_slice_arg < 1 ||
          p_cfg->niParamT408.avc_slice_arg > numMbs)
      {
        strncpy(p_param_err, "Invalid avcSliceArg: must be between 1 and number"
                " of 16x16 pixel MBs in a frame", max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }
    }
  }
  else if (NI_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    if (10 == p_ctx->src_bit_depth)
    {
      if (p_cfg->niParamT408.profile != 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 2 (main10)",
                max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }
    }
    else
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                max_err_len);
        return NI_RETCODE_INVALID_PARAM;
      }
    }
  }

  if (p_src->force_frame_type != 0 && p_src->force_frame_type != 1)
  {
      strncpy(p_param_err, "Invalid forceFrameType: out of range",
              max_err_len);
      return NI_RETCODE_INVALID_PARAM;
  }

  if (p_cfg->niParamT408.forcedHeaderEnable < 0 ||
      p_cfg->niParamT408.forcedHeaderEnable > 2)
  {
      strncpy(p_param_err, "Invalid forcedHeaderEnable: out of range",
              max_err_len);
      param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
      LRETURN;
  }

  if (p_cfg->niParamT408.decoding_refresh_type < 0 ||
    p_cfg->niParamT408.decoding_refresh_type > 2)
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE;
    LRETURN;
  }

  if (p_cfg->niParamT408.gop_preset_index < NI_MIN_GOP_PRESET_IDX ||
      p_cfg->niParamT408.gop_preset_index > NI_MAX_GOP_PRESET_IDX)
  {
    snprintf(p_param_err, strlen("Invalid gop_preset_index: out of range"), "Invalid gop_preset_index: out of range");
    param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
    LRETURN;
  }

  if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
  {
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
    {
      strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
      NI_MAX_GOP_NUM)
    {
      strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
  }

  if (p_cfg->niParamT408.use_recommend_enc_params < 0 ||
    p_cfg->niParamT408.use_recommend_enc_params > 3)
  {
    strncpy(p_param_err, "Invalid use_recommend_enc_params: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM;
    LRETURN;
  }

  switch (p_cfg->niParamT408.use_recommend_enc_params)
  {
    case 0:
    case 2:
    case 3:
    {
      if (p_cfg->niParamT408.use_recommend_enc_params != 3)
      {
        // in FAST mode (recommendEncParam==3), max_num_merge value will be
        // decided in FW
        if (p_cfg->niParamT408.max_num_merge < 0 ||
          p_cfg->niParamT408.max_num_merge > 3)
        {
          strncpy(p_param_err, "Invalid max_num_merge: out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_MAXNUMMERGE;
          LRETURN;
        }
      }
      break;
    }

    default: break;
  }

  if ( p_cfg->niParamT408.intra_qp < NI_MIN_INTRA_QP ||
       p_cfg->niParamT408.intra_qp > NI_MAX_INTRA_QP )
  {
    strncpy(p_param_err, "Invalid intra_qp: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_INTRA_QP;
    LRETURN;
  }

  if ( p_cfg->niParamT408.enable_mb_level_rc != 1 &&
       p_cfg->niParamT408.enable_mb_level_rc != 0 )
  {
    strncpy(p_param_err, "Invalid enable_mb_level_rc: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
    LRETURN;
  }

  if (1 == p_cfg->niParamT408.enable_mb_level_rc)
  {
    if ( p_cfg->niParamT408.minQpI < 0 ||
         p_cfg->niParamT408.minQpI > 51 )
    {
      strncpy(p_param_err, "Invalid min_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MN_QP;
      LRETURN;
    }

    if ( p_cfg->niParamT408.maxQpI < 0 ||
         p_cfg->niParamT408.maxQpI > 51 )
    {
      strncpy(p_param_err, "Invalid max_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
    // TBD minQpP minQpB maxQpP maxQpB

    if ( p_cfg->niParamT408.enable_cu_level_rate_control != 1 &&
         p_cfg->niParamT408.enable_cu_level_rate_control != 0 )
    {
      strncpy(p_param_err, "Invalid enable_cu_level_rate_control: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN;
      LRETURN;
    }

    if (p_cfg->niParamT408.enable_cu_level_rate_control == 1)
    {
      if ( p_cfg->niParamT408.enable_hvs_qp != 1 &&
           p_cfg->niParamT408.enable_hvs_qp != 0 )
      {
        strncpy(p_param_err, "Invalid enable_hvs_qp: out of range", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_HVS_QP_EN;
        LRETURN;
      }

      if (p_cfg->niParamT408.enable_hvs_qp)
      {
        if ( p_cfg->niParamT408.max_delta_qp < NI_MIN_MAX_DELTA_QP ||
             p_cfg->niParamT408.max_delta_qp > NI_MAX_MAX_DELTA_QP )
        {
          strncpy(p_param_err, "Invalid max_delta_qp: out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_MX_DELTA_QP;
          LRETURN;
        }
#if 0 // TBD missing enable_hvs_qp_scale?
        if ( p_cfg->niParamT408.enable_hvs_qp_scale != 1 &&
             p_cfg->niParamT408.enable_hvs_qp_scale != 0 ) 
        {
          snprintf(p_param_err, strlen("Invalid enable_hvs_qp_scale: out of range"), "Invalid enable_hvs_qp_scale: out of range");
          return NI_RETCODE_PARAM_ERROR_HVS_QP_SCL;
        }

        if (p_cfg->niParamT408.enable_hvs_qp_scale == 1) {
          if ( p_cfg->niParamT408.hvs_qp_scale < 0 ||
               p_cfg->niParamT408.hvs_qp_scale > 4 ) 
          {
            snprintf(p_param_err, strlen("Invalid hvs_qp_scale: out of range"), "Invalid hvs_qp_scale: out of range");
            return NI_RETCODE_PARAM_ERROR_HVS_QP_SCL;
          }
        }
#endif
      }
    }
    // TBD rc_init_delay/i32vbvBufferSize same thing in Rev. B ?
    if (p_cfg->i32vbvBufferSize < 10 || p_cfg->i32vbvBufferSize > 3000)
    {
      strncpy(p_param_err, "Invalid i32vbvBufferSize: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_RCINITDELAY;
      LRETURN;
    }
  }

  // check valid for common param
  param_ret = ni_check_common_params(&p_cfg->niParamT408, p_src, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  // check valid for RC param
  param_ret = ni_check_ratecontrol_params(p_cfg, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_CUSTOM)
  {
    p_ctx->keyframe_factor =
      presetGopKeyFrameFactor[p_cfg->niParamT408.gop_preset_index];
  }

  param_ret = NI_RETCODE_SUCCESS;

  ni_log(NI_LOG_DEBUG, "useLowDelayPocType=%d\n",
         p_src->use_low_delay_poc_type);
  // after validation, convert gopPresetIdx based on useLowDelayPocType flag
  // for H.264 to enable poc_type = 2
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format &&
      p_src->use_low_delay_poc_type)
  {
    switch (p_cfg->niParamT408.gop_preset_index)
    {
    case GOP_PRESET_IDX_ALL_I:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_17;
      break;
    case GOP_PRESET_IDX_IPP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_18;
      break;
    case GOP_PRESET_IDX_IBBB:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_19;
      break;
    case GOP_PRESET_IDX_IPPPP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_20;
      break;
    case GOP_PRESET_IDX_IBBBB:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_21;
      break;
    case GOP_PRESET_IDX_SP:
      p_cfg->niParamT408.gop_preset_index = GOP_PRESET_IDX_22;
      break;
    }
    ni_log(NI_LOG_DEBUG, "final gop_preset_index=%d\n",
           p_cfg->niParamT408.gop_preset_index);
  }

  END;

  return param_ret;
}

ni_retcode_t ni_check_common_params(ni_t408_config_t* p_param, ni_encoder_params_t* p_src,
  char* p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  int32_t low_delay = 0;
  int32_t intra_period_gop_step_size;
  int32_t i, j;
    
  if ( (!p_param) || (!p_src) || (!p_param_err) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_check_common_params() Null pointer parameters passed\n");
    ret = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  // check low-delay gop structure
  if (0 == p_param->gop_preset_index) // custom gop
  {
    int32_t minVal = 0;
    low_delay = (p_param->custom_gop_params.custom_gop_size == 1);
    
    if (p_param->custom_gop_params.custom_gop_size > 1)
    {
      minVal = p_param->custom_gop_params.pic_param[0].poc_offset;
      low_delay = 1;
      for (i = 1; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (minVal > p_param->custom_gop_params.pic_param[i].poc_offset)
        {
          low_delay = 0;
          break;
        }
        else
        {
          minVal = p_param->custom_gop_params.pic_param[i].poc_offset;
        }
      }
    }
  }
  else if (1 == p_param->gop_preset_index || 2 == p_param->gop_preset_index ||
           3 == p_param->gop_preset_index || 6 == p_param->gop_preset_index ||
           7 == p_param->gop_preset_index || 9 == p_param->gop_preset_index)
  {
    low_delay = 1;
  }

  if (p_src->low_delay_mode && ! low_delay)
  {
    strncpy(p_param_err, "GOP size must be 1 or frames must be in sequence "
            "when lowDelay is enabled", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
    LRETURN;
  }

  if (low_delay)
  {
    intra_period_gop_step_size = 1;
  }
  else
  {
    if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      intra_period_gop_step_size = p_param->custom_gop_params.custom_gop_size;
    }
    else
    {
      intra_period_gop_step_size = presetGopSize[p_param->gop_preset_index];
    }
  }
  
  if (((p_param->intra_period != 0) && ((p_param->intra_period < intra_period_gop_step_size+1) == 1)) ||
      ((p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod < intra_period_gop_step_size+1) == 1)))
  {
    strncpy(p_param_err, "Invalid intra_period and gop_preset_index: gop structure is larger than intra period", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }
  
  if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) != 0)) ||
      ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) != 0)))
  {
    strncpy(p_param_err, "Invalid intra_period and gop_preset_index: intra period is not a multiple of gop structure size", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }

  // TODO: this error check will never get triggered. remove? (SZ)
  if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) == 1) && p_param->decoding_refresh_type == 0) ||
      ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) == 1) && p_param->decoding_refresh_type == 0))
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: not support decoding refresh type I p_frame for closed gop structure", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
    LRETURN;
  }

  if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
  {
    int temp_poc[NI_MAX_GOP_NUM];
    int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
    for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
    {
      if (p_param->custom_gop_params.pic_param[i].poc_offset >
          p_param->custom_gop_params.custom_gop_size)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: poc_offset larger"
                " than GOP size", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }

      if (p_param->custom_gop_params.pic_param[i].temporal_id >= XCODER_MAX_NUM_TEMPORAL_LAYER)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }

      if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is zero or negative", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      temp_poc[i] = p_param->custom_gop_params.pic_param[i].poc_offset;
      if (min_poc > temp_poc[i])
      {
        min_poc = temp_poc[i];
      }
    }
    int count_pos = 0;
    for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
    {
      for (j = 0; j < p_param->custom_gop_params.custom_gop_size; j++)
      {
        if (temp_poc[j] == min_poc)
        {
          count_pos++;
          min_poc++;
        }
      }
    }
    if (count_pos != p_param->custom_gop_params.custom_gop_size)
    {
      strncpy(p_param_err, "Invalid custom gop parameters: poc_offset is invalid", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
      LRETURN;
    }
  }

  if (0 == p_param->use_recommend_enc_params)
  {
    // RDO
    {
      int align_32_width_flag = p_src->source_width % 32;
      int align_16_width_flag = p_src->source_width % 16;
      int align_8_width_flag = p_src->source_width % 8;
      int align_32_height_flag = p_src->source_height % 32;
      int align_16_height_flag = p_src->source_height % 16;
      int align_8_height_flag = p_src->source_height % 8;

      if (((p_param->cu_size_mode & 0x1) == 0) && ((align_8_width_flag != 0) || (align_8_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 8 pixels when enable CU8x8 of cu_size_mode. Recommend to set cu_size_mode |= 0x1 (CU8x8)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((align_16_width_flag != 0) || (align_16_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 16 pixels when enable CU16x16 of cu_size_mode. Recommend to set cu_size_mode |= 0x2 (CU16x16)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((p_param->cu_size_mode & 0x4) == 0) && ((align_32_width_flag != 0) || (align_32_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 32 pixels when enable CU32x32 of cu_size_mode. Recommend to set cu_size_mode |= 0x4 (CU32x32)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN;
        LRETURN;
      }
    }
  }

  if ((p_param->conf_win_top < 0) || (p_param->conf_win_top > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_top: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }
  if (p_param->conf_win_top % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_top: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }

  if ((p_param->conf_win_bottom < 0) || (p_param->conf_win_bottom > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }
  if (p_param->conf_win_bottom % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }

  if ((p_param->conf_win_left < 0) || (p_param->conf_win_left > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_left: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }
  if (p_param->conf_win_left % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_left: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }

  if (p_param->conf_win_right < 0 || p_param->conf_win_right > 8192)
  {
    strncpy(p_param_err, "Invalid conf_win_right: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
    LRETURN;
  }
  if (p_param->conf_win_right % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_right: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
  }

  END;

  return ret;
}

ni_retcode_t ni_check_ratecontrol_params(ni_encoder_config_t* p_cfg, char* p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  ni_t408_config_t* p_param = &p_cfg->niParamT408;
  
  if ( (!p_cfg) || (!p_param_err) )
  {
    ni_log(NI_LOG_TRACE, "ERROR: ni_check_ratecontrol_params() Null pointer parameters passed\n");
    ret = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  
  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (p_param->roiEnable != 0 && p_param->roiEnable != 1)
  {
    strncpy(p_param_err, "Invalid roiEnable: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  // RevB
  if (p_cfg->ui8rcEnable == 1)
  {
    if (p_param->minQpP > p_param->maxQpP || p_param->minQpB > p_param->maxQpB)
    {
      strncpy(p_param_err, "Invalid min_qp(P/B) and max_qp(P/B): min_qp cannot be larger than max_qp", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
  }

  END;

  return ret;
}


/*!******************************************************************************
 *  \brief  Print xcoder user configurations
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_params_print(ni_encoder_params_t* const p_encoder_params)
{
  if (!p_encoder_params)
  {
    return;
  }

  ni_h265_encoder_params_t* p_enc = &p_encoder_params->hevc_enc_params;

  ni_log(NI_LOG_TRACE, "XCoder Params:\n");

  ni_log(NI_LOG_TRACE, "preset=%d\n", p_encoder_params->preset);
  ni_log(NI_LOG_TRACE, "fps_number / fps_denominator=%d / %d\n", p_encoder_params->fps_number, p_encoder_params->fps_denominator);

  ni_log(NI_LOG_TRACE, "source_width x source_height=%dx%d\n", p_encoder_params->source_width, p_encoder_params->source_height);
  ni_log(NI_LOG_TRACE, "bitrate=%d\n", p_encoder_params->bitrate);

  ni_log(NI_LOG_TRACE, "profile=%d\n", p_enc->profile);
  ni_log(NI_LOG_TRACE, "level_idc=%d\n", p_enc->level_idc);
  ni_log(NI_LOG_TRACE, "high_tier=%d\n", p_enc->high_tier);

  ni_log(NI_LOG_TRACE, "frame_rate=%d\n", p_enc->frame_rate);

  ni_log(NI_LOG_TRACE, "use_recommend_enc_params=%d\n", p_enc->use_recommend_enc_params);

  // trans_rate not available in Rev B
  ni_log(NI_LOG_TRACE, "enable_rate_control=%d\n", p_enc->rc.enable_rate_control);
  ni_log(NI_LOG_TRACE, "enable_cu_level_rate_control=%d\n", p_enc->rc.enable_cu_level_rate_control);
  ni_log(NI_LOG_TRACE, "enable_hvs_qp=%d\n", p_enc->rc.enable_hvs_qp);
  ni_log(NI_LOG_TRACE, "enable_hvs_qp_scale=%d\n", p_enc->rc.enable_hvs_qp_scale);
  ni_log(NI_LOG_TRACE, "hvs_qp_scale=%d\n", p_enc->rc.hvs_qp_scale);
  ni_log(NI_LOG_TRACE, "min_qp=%d\n", p_enc->rc.min_qp);
  ni_log(NI_LOG_TRACE, "max_qp=%d\n", p_enc->rc.max_qp);
  ni_log(NI_LOG_TRACE, "max_delta_qp=%d\n", p_enc->rc.max_delta_qp);
  ni_log(NI_LOG_TRACE, "rc_init_delay=%d\n", p_enc->rc.rc_init_delay);

  ni_log(NI_LOG_TRACE, "forcedHeaderEnable=%d\n", p_enc->forced_header_enable);
  ni_log(NI_LOG_TRACE, "roi_enable=%d\n", p_enc->roi_enable);
  ni_log(NI_LOG_TRACE, "long_term_ref_enable=%d\n", p_enc->long_term_ref_enable);
  ni_log(NI_LOG_TRACE, "conf_win_top=%d\n", p_enc->conf_win_top);
  ni_log(NI_LOG_TRACE, "conf_win_bottom=%d\n", p_enc->conf_win_bottom);
  ni_log(NI_LOG_TRACE, "conf_win_left=%d\n", p_enc->conf_win_left);
  ni_log(NI_LOG_TRACE, "conf_win_right=%d\n", p_enc->conf_win_right);

  ni_log(NI_LOG_TRACE, "intra_qp=%d\n", p_enc->rc.intra_qp);
  ni_log(NI_LOG_TRACE, "enable_mb_level_rc=%d\n", p_enc->rc.enable_mb_level_rc);

  ni_log(NI_LOG_TRACE, "intra_period=%d\n", p_enc->intra_period);
  ni_log(NI_LOG_TRACE, "decoding_refresh_type=%d\n", p_enc->decoding_refresh_type);

  // Rev. B: H.264 only or HEVC-shared parameters, in ni_t408_config_t
  ni_log(NI_LOG_TRACE, "enable_transform_8x8=%d\n", p_enc->enable_transform_8x8);
  ni_log(NI_LOG_TRACE, "avc_slice_mode=%d\n", p_enc->avc_slice_mode);
  ni_log(NI_LOG_TRACE, "avc_slice_arg=%d\n", p_enc->avc_slice_arg);
  ni_log(NI_LOG_TRACE, "entropy_coding_mode=%d\n", p_enc->entropy_coding_mode);
  ni_log(NI_LOG_TRACE, "intra_mb_refresh_mode=%d\n", p_enc->intra_mb_refresh_mode);
  ni_log(NI_LOG_TRACE, "intra_mb_refresh_arg=%d\n", p_enc->intra_mb_refresh_arg);

  ni_log(NI_LOG_TRACE, "gop_preset_index=%d\n", p_enc->gop_preset_index);
  if (p_enc->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
  {
    int i;
    ni_log(NI_LOG_TRACE, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
    for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
    {
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_type);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].poc_offset);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_qp);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L0);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L1);
      ni_log(NI_LOG_TRACE, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_enc->custom_gop_params.pic_param[i].temporal_id);
    }
  }

  return;
}

/*!******************************************************************************
 *  \brief  decoder keep alive thread function triggers every 1 second
 *
 *  \param void thread args
 *
 *  \return void
 *******************************************************************************/
void *ni_session_keep_alive_thread(void *arguments)
{

  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_thread_arg_struct_t *args = (ni_thread_arg_struct_t *) arguments;
  ni_instance_status_info_t inst_info = { 0 };
  ni_session_context_t ctx = {0};
  uint32_t loop = 0;
#ifdef __linux__
  struct sched_param sched_param;

  // Linux has a wide variety of signals, Windows has a few.
  // A large number of signals will interrupt the thread, which will cause heartbeat command interval more than 1 second.
  // So just mask the unuseful signals in Linux
  sigset_t signal;
  sigfillset(&signal);
  pthread_sigmask(SIG_BLOCK,&signal, NULL);

  /* set up schedule priority
   * first try to run with RR mode.
   * if fails, try to set nice value.
   * if fails either, ignore it and run with default priority.
   */
  if (((sched_param.sched_priority = sched_get_priority_max(SCHED_RR)) == -1) ||
        sched_setscheduler(syscall(SYS_gettid), SCHED_RR, &sched_param) < 0) {
    ni_log(NI_LOG_TRACE, "keep_alive_thread cannot set scheduler: %s\n", strerror(errno));

    if (setpriority(PRIO_PROCESS, 0, -20) != 0)
    {
      ni_log(NI_LOG_TRACE, "keep_alive_thread cannot set nice value: %s\n", strerror(errno));
    }
  }

#elif defined(_WIN32)
  /* set up schedule priority.
   * try to set the current thread to time critical level which is the highest prioriy
   * level.
   */
  if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) == 0)
  {
    ni_log(NI_LOG_TRACE, "keep_alive_thread cannot set priority: %d.\n", GetLastError());
  }
#endif

  // Initializes the session context variables that keep alive command and query status command need.
  ctx.hw_id = args->hw_id;
  ctx.session_id = args->session_id;
  ctx.session_timestamp = args->session_timestamp;
  ctx.device_type = args->device_type;
  ctx.blk_io_handle = args->device_handle;
  ctx.event_handle = args->thread_event_handle;
  ctx.p_all_zero_buf = args->p_buffer;
  ctx.keep_alive_timeout = args->keep_alive_timeout;
  ni_log(NI_LOG_TRACE, "keep_alive_thread ctx.keep_alive_timeout: %d.\n", ctx.keep_alive_timeout);

  while (1)// condition TBD
  {
    retval = ni_send_session_keep_alive(ctx.session_id, ctx.blk_io_handle, ctx.event_handle, ctx.p_all_zero_buf);
    retval = ni_query_status_info(&ctx, ctx.device_type, &inst_info, retval, nvme_admin_cmd_xcoder_config);
    CHECK_ERR_RC2((&ctx), retval, inst_info, nvme_admin_cmd_xcoder_config,
                  ctx.device_type, ctx.hw_id, &(ctx.session_id));
    
    if (NI_RETCODE_SUCCESS != retval)
    { 
       LRETURN;
    }
#ifdef _WIN32
    //total sleep ctx.keep_alive_timeout/2 second
    for (loop = 0; loop < ctx.keep_alive_timeout * 50; loop++)
    {
      if (args->close_thread)
      {
        LRETURN;
      }
      usleep(10000);
    }
#else
    //total sleep ctx.keep_alive_timeout/2 second
    for (loop = 0; loop < ctx.keep_alive_timeout * 250; loop++)
    {
      if (args->close_thread)
      {
        LRETURN;
      }
      usleep(2000);
    }
#endif
  }

  END;
  if (NI_RETCODE_SUCCESS != retval)
  {
     ni_log(NI_LOG_ERROR, "keep_alive_thread abormal closed:%d\n", retval);
     args->close_thread = true; // changing the value to be True here means the thread has been closed.  
  }

  ni_log(NI_LOG_TRACE, "ni_session_keep_alive_thread(): exit\n");

  return NULL;
}
