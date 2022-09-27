/*!****************************************************************************
*
* Copyright (C)  2018 NETINT Technologies. 
*
* Permission to use, copy, modify, and/or distribute this software for any 
* purpose with or without fee is hereby granted.
*
*******************************************************************************/

/*******************************************************************************
 *
 *   @file          test_rsrc_api.c
 *
 *   @date          April 1, 2018
 *
 *   @brief         Example code to test resource API
 *
 *   @author        
 *
 ******************************************************************************/

#ifdef __linux__
#include <unistd.h>
#include <sys/file.h> /* For flock constants */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <string.h>
#include <errno.h>

#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_util.h"


/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void getStr(const char *prompt, char *str)
{
  char para[16];
  printf("%s", prompt);

  if (fgets(para, sizeof(para), stdin) != 0) {
    size_t len = strlen(para);
    if (len > 0 && para[len - 1] == '\n')
    {
      para[len - 1] = '\0';
    }
    str[0] = '\0';
    strncat(str, para, strlen(para));
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
int getCmd(const char *prompt)
{
  char cmd[16];

  getStr(prompt, cmd);
  return (int)cmd[0];
}

/******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void change_log_level(void)
{
  char log_level[16];
  getStr("set log level to [info, debug, trace]: ", log_level);
  if (!strcmp(log_level, "none")) {
    ni_log_set_level(NI_LOG_NONE);
  } else if (!strcmp(log_level, "fatal")) {
    ni_log_set_level(NI_LOG_FATAL);
  } else if (!strcmp(log_level, "error")) {
    ni_log_set_level(NI_LOG_ERROR);
  } else if (!strcmp(log_level, "info")) {
    ni_log_set_level(NI_LOG_INFO);
  } else if (!strcmp(log_level, "debug")) {
    ni_log_set_level(NI_LOG_DEBUG);
  } else if (!strcmp(log_level, "trace")) {
    ni_log_set_level(NI_LOG_TRACE);
  } else {
    printf("unknown log level selected: %s", log_level);
    return;
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
int getInt(const char *prompt)
{
  char para[16];

  getStr(prompt, para);
  return atoi(para);
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
float getFloat(const char *prompt)
{
  char para[16];

  getStr(prompt, para);
  return (float)atof(para);
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
long getLong(const char *prompt)
{
  char para[32];

  getStr(prompt, para);
  return atol(para);
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void listOneTypeCoders(void)
{
  ni_device_type_t type;
  int i, count = 0;
  ni_device_info_t coders[NI_MAX_HW_DECODER_COUNT] = {0};
  
  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  if (ni_rsrc_list_devices(type, coders, &count) == 0) {
    for (i = 0; i < count; i++) {
      ni_rsrc_print_device_info(&coders[i]);
    }
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void listModuleId(void)
{
  /* read back the stored coders numbers */
  uint32_t i;
  ni_device_queue_t *ptr;
  ni_device_pool_t *p_device_pool;
  ni_device_info_t *p_device_info;
  
  p_device_pool = ni_rsrc_get_device_pool();
  if (p_device_pool) 
  {
#ifdef _WIN32
    if (WAIT_ABANDONED == WaitForSingleObject(p_device_pool->lock, INFINITE)) // no time-out interval)
    {
      fprintf(stderr, "ERROR: listModuleId() failed o obtain mutex: %p", p_device_pool->lock);
      return;
    }
#elif __linux__
  if( flock(p_device_pool->lock, LOCK_EX) )
  {
    fprintf(stderr, "Error flock() failed\n");
  }
#endif

    /* print out coders in their current order */
    ptr = p_device_pool->p_device_queue;
    printf("Num decoders: %d\n", ptr->decoders_cnt);
    for (i = 0; i < ptr->decoders_cnt; i++) {
      p_device_info = ni_rsrc_get_device_info(NI_DEVICE_TYPE_DECODER, ptr->decoders[i]);
      printf("[%d]. %d (load: %d inst: %d   %s.%d)\n", i, ptr->decoders[i],
             p_device_info->load, p_device_info->active_num_inst, p_device_info->dev_name, p_device_info->hw_id);
      free(p_device_info);
    }
    printf("\n\nNum encoders: %d\n", ptr->encoders_cnt);
    for (i = 0; i < ptr->encoders_cnt; i++) {
      p_device_info = ni_rsrc_get_device_info(NI_DEVICE_TYPE_ENCODER, ptr->encoders[i]);
      printf("[%d]. %d (load: %d inst: %d   %s.%d)\n", i, ptr->encoders[i],
             p_device_info->load, p_device_info->active_num_inst, p_device_info->dev_name, p_device_info->hw_id);
      free(p_device_info);
    }
    printf("\n\n");
    
#ifdef _WIN32
  ReleaseMutex(p_device_pool->lock);
#elif __linux__
    if( flock(p_device_pool->lock, LOCK_UN) )
  {
    fprintf(stderr, "Error flock() failed\n");
  }
#endif
    ni_rsrc_free_device_pool(p_device_pool);
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void listAllCodersFull(void)
{
  int i;
  ni_device_t coders;

  if (ni_rsrc_list_all_devices(&coders) == 0) {
    /* print out coders in the order based on their guid */
    printf("Num decoders: %d\n", coders.decoders_cnt);

    for (i = 0; i < coders.decoders_cnt; i++) {
      ni_rsrc_print_device_info(&(coders.decoders[i]));
    }
    printf("Num encoders: %d\n", coders.encoders_cnt);
    for (i = 0; i < coders.encoders_cnt; i++) {
      ni_rsrc_print_device_info(&(coders.encoders[i]));
    }
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void updateCoderLoad(void)
{
  ni_device_type_t type;
  int guid;
  int load;
  ni_device_context_t *p_device_context;
  int i, nb_insts;
  ni_sw_instance_info_t sw_insts[8];

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  guid = getInt("Coder module ID: ");
  load = getInt("load value: ");
  nb_insts = getInt("Number of s/w instances: ");
  for (i = 0; i < nb_insts; i++) {
    sw_insts[i].id = getInt("s/w inst. id: ");
    sw_insts[i].status = (ni_sw_instance_status_t)getInt("s/w inst. status, idle (0) active (1): ");
    sw_insts[i].codec = (ni_codec_t)getInt("s/w inst. codec, H.264 (0) H.265 (1): ");
    sw_insts[i].width = getInt("s/w inst. width: ");
    sw_insts[i].height = getInt("s/w inst. height: ");
    sw_insts[i].fps = getInt("s/w inst. fps: ");
  }

  p_device_context = ni_rsrc_get_device_context(type, guid);
  if (p_device_context) {
    ni_rsrc_update_device_load(p_device_context, load, nb_insts, sw_insts);
    ni_rsrc_free_device_context(p_device_context);
  } else {
    printf("Error coder not found ..\n");
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void getCoderDetailInfo(void)
{
  ni_device_type_t type;
  int guid;
  ni_device_info_t *p_device_info;
  
  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  guid = getInt("Coder module ID: ");
  printf("type: %d  id  %d\n", type, guid);
  p_device_info = ni_rsrc_get_device_info(type, guid);
  if (p_device_info) {
    ni_rsrc_print_device_info(p_device_info);
    free(p_device_info);
  }
}

/******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void getLeastUsedCoderForVideo(void)
{
  int guid, width, height, fps;
  ni_codec_t codec;
  ni_device_type_t type;
  ni_device_info_t info;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  codec = (ni_codec_t)getInt("codec, H.264 (0) H.265 (1): ");
  width = getInt("video width: ");
  height = getInt("video height: ");
  fps = getInt("video frame rate: ");
  guid = ni_rsrc_get_available_device(width, height, fps, codec, type, &info);
  if (guid == -1) {
    printf("Error coder not found ..\n");
  } else {
    ni_rsrc_print_device_info(&info);
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void allocAuto(void)
{
  ni_device_context_t *p_device_context;
  ni_device_type_t type;
  ni_alloc_rule_t rule;
  unsigned long model_load;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  rule = (ni_alloc_rule_t)getInt("auto-alloc rule, least-load (0) load-instance (1): ");
  printf("type: %d  rule  %d\n", type, rule);
  
  p_device_context = ni_rsrc_allocate_auto(type, rule, EN_H265, 1920, 1080, 30, &model_load);
  if (p_device_context) {
    printf("Successfully auto-allocated s/w instance on:\n");
    ni_rsrc_print_device_info(p_device_context->p_device_info);
    printf("Allocated load: %ld\n", model_load);
    ni_rsrc_free_device_context(p_device_context);
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
void allocDirect(void)
{
  ni_device_context_t *p_device_context;
  ni_device_type_t type;
  int guid;
  unsigned long model_load;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  guid = getInt("Coder module ID: ");
  printf("type: %d  id  %d\n", type, guid);

  p_device_context = ni_rsrc_allocate_direct(type, guid, EN_H264, 1920, 1080, 30, &model_load);
  if (p_device_context) {
    printf("Successfully allocated directly the s/w instance ..\n");
    ni_rsrc_free_device_context(p_device_context);
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void releaseEncRsrc(void)
{
  unsigned long load;
  int guid;
  ni_device_context_t *p_device_context;
  //ni_device_info_t *p_device_info;
  
  guid = getInt("Coder module ID: ");
  load = (unsigned long)getLong("load value: ");
  printf("id  %d   load: %ld\n", guid, load);
  p_device_context = ni_rsrc_get_device_context(NI_DEVICE_TYPE_ENCODER, guid);
  if (p_device_context) {
    ni_rsrc_release_resource(p_device_context, NI_DEVICE_TYPE_ENCODER, load);
    ni_rsrc_free_device_context(p_device_context);
  } else {
    printf("Error coder not found ..\n");
  }
}

/******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void deleteCoder(void)
{
  char dev[16];
  getStr("device name (/dev/*): ", dev);
  ni_rsrc_remove_device(dev);
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void addCoder(void)
{
  char dev[16];
  int should_match_rev = 0;
  getStr("device name (/dev/*): ", dev);
  should_match_rev = getInt("should match current release version, yes (1) no (0): ");
  ni_rsrc_add_device(dev, should_match_rev);
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 ******************************************************************************/
void checkHW(void)
{
  ni_device_type_t type;
  int guid;
  int rc;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  guid = getInt("Coder module ID (GUID): ");
  printf("type: %d  id  %d\n", type, guid);

  rc = ni_rsrc_codec_is_available(guid, type);
  if (rc == NI_RETCODE_SUCCESS)
  {
    printf("%s module ID (GUID) %d is good\n", (type ==0 ? "decoder":"encoder"), guid);
  }
  else
  {
    printf("%s module ID (GUID) %d is bad\n", (type ==0 ? "decoder":"encoder"), guid);
  }
}

/*******************************************************************************
 *  @brief  
 *
 *  @param  
 *
 *  @return
 *******************************************************************************/
int main(void)
{
  int stop = 0;
  int control;
  while (!stop) {
    printf("Key     function\n"
           "?       show this help\n"
           "v       change libxcoder log level\n"
           "l       list all decoders, or all encoders\n"
           "L       list all coders detailed info\n"
           "o       list all coders' module #, in order of position in queue\n"
           "g       get a coder's info\n"
           "G       get the least used coder for a video stream\n"
           "u       update a coder's load/instances value\n"
           "A       allocate directly a s/w instance on a h/w coder\n"
           "a       allocate automatically a s/w instance\n"
           "r       release encoding resource (load) from a h/w instance\n"
           "d       delete xcoder from host resource pool\n"
           "x       add    xcoder into host resource pool\n"
           "c       check hardware status in resource pool\n"
           "q       quit\n");
    control = getCmd("> ");

    switch (control) {
    case '?': 
      continue;
    case 'v':
      change_log_level();
    case 'l':
      listOneTypeCoders();
      break;
    case 'L':
      listAllCodersFull();
      break;
    case 'o':
      listModuleId();
      break;
    case 'g':
      getCoderDetailInfo();
      break;
    case 'G':
      getLeastUsedCoderForVideo();
      break;
    case 'u':
      updateCoderLoad();
      break;
    case 'A':
      allocDirect();
      break;
    case 'a':
      allocAuto();
      break;
    case 'r':
      releaseEncRsrc();
      break;
    case 'd':
      deleteCoder();
      break;
    case 'x':
      addCoder();
      break;
    case 'c':
      checkHW();
      break;

    case 'q':
    case EOF:
      stop = 1;
      break;
    default:
      continue;
    }
  }
  return 0;
}
