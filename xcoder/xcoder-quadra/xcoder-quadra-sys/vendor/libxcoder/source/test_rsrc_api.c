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

/*******************************************************************************
 *
 *   @file          test_rsrc_api.p_device_info
 *
 *   @date          April 1, 2018
 *
 *   @brief
 *
 *   @author        
 *
 ******************************************************************************/
#ifdef __linux__
#include <unistd.h>
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
      para[len - 1] = '\0';
    strcpy(str, para);
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
    char cmd[16] = {'\0'};

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
    char log_str[16];
    ni_log_level_t log_level;

    getStr("set log level to [none, fatal, error, info, debug, trace]: ",
           log_str);
    
    log_level = arg_to_ni_log_level(log_str);
    if (log_level != NI_LOG_INVALID)
    {
        ni_log_set_level(log_level);
    } else {
        fprintf(stderr, "ERROR: unknown log level selected: %s", log_str);
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
  ni_device_info_t coders[NI_MAX_DEVICE_CNT] = {0};

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  if (ni_rsrc_list_devices(type, coders, &count) == 0) {
      for (i = 0; i < count; i++)
      {
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
  uint32_t i, k;
  ni_device_queue_t *ptr;
  ni_device_pool_t *p_device_pool;
  
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
  if( lockf(p_device_pool->lock, F_LOCK, 0) )
  {
      fprintf(stderr, "ERROR: %s() lockf() failed: %s\n", __func__,
              strerror(NI_ERRNO));
  }
#endif

    /* print out coders in their current order */
    ptr = p_device_pool->p_device_queue;
    for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
    {
        printf("Num %ss: %u\n", device_type_str[k], ptr->xcoder_cnt[k]);
        for (i = 0; i < ptr->xcoder_cnt[k]; i++)
        {
            ni_device_info_t *p_device_info =
                ni_rsrc_get_device_info(k, ptr->xcoders[k][i]);
            printf("[%u]. %d (load: %d inst: %d   %s.%d)\n", i,
                   ptr->xcoders[k][i], p_device_info->load,
                   p_device_info->active_num_inst, p_device_info->dev_name,
                   p_device_info->hw_id);
            free(p_device_info);
        }
        printf("\n\n");
    }

#ifdef _WIN32
  ReleaseMutex(p_device_pool->lock);
#elif __linux__
    if( lockf(p_device_pool->lock, F_ULOCK, 0) )
  {
    fprintf(stderr, "Error lockf() failed\n");
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
    int i, k;
    ni_device_t *p_coders = NULL;
    p_coders = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (p_coders == NULL)
    {
        fprintf(stderr,
                "Error: memory allocation failed, fatal error, exiting\n");
        return;
    }

    if (ni_rsrc_list_all_devices(p_coders) == 0)
    {
        for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
        {
            /* print out coders in the order based on their guid */
            printf("Num %ss: %d\n", device_type_str[k],
                   p_coders->xcoder_cnt[k]);

            for (i = 0; i < p_coders->xcoder_cnt[k]; i++)
            {
                ni_rsrc_print_device_info(&(p_coders->xcoders[k][i]));
            }
        }
    }
    free(p_coders);
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
      fprintf(stderr, "ERROR: netint device not found\n");
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
      fprintf(stderr, "ERROR: netint device not found\n");
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
    printf("Allocated load: %lu\n", model_load);
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
  printf("id  %d   load: %lu\n", guid, load);
  p_device_context = ni_rsrc_get_device_context(NI_DEVICE_TYPE_ENCODER, guid);
  if (p_device_context) {
      ni_rsrc_release_resource(p_device_context, load);
      ni_rsrc_free_device_context(p_device_context);
  } else {
      fprintf(stderr, "ERROR: netint device not found\n");
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
 *******************************************************************************/
int main(void)
{
  int stop = 0;
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
           "q       quit\n");
    int control = getCmd("> ");

    switch (control) {
    case '?': 
      continue;
    case 'v':
      change_log_level();
      break;
    case 'V':
        printf("Ver:  %s\n"
               "Date: %s\n"
               "ID:   %s\n",
               NI_XCODER_REVISION, NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
        break;
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
