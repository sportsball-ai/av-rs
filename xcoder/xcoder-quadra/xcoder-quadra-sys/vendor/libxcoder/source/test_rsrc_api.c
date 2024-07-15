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
 *  \file   test_rsrc_api.c
 *
 *  \brief  Application for manually managing NETINT video processing devices on
 *          system. Its code provides examples on how to programatically use
 *          ni_rsrc_api.h for NETINT resource management
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
static void getStr(const char *prompt, char *str)
{
  char para[64];
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
static int getCmd(const char *prompt)
{
    char cmd[64] = {'\0'};

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
static void change_log_level(void)
{
    char log_str[64];
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
static int getInt(const char *prompt)
{
  char para[64];

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
NI_UNUSED static float getFloat(const char *prompt)
{
  char para[64];

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
NI_UNUSED static long getLong(const char *prompt)
{
  char para[64];

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
static void listOneTypeCoders(void)
{
  ni_device_type_t type;
  int i, count = 0;
  ni_device_info_t coders[NI_MAX_DEVICE_CNT] = {0};

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  if (ni_rsrc_list_devices(type, coders, &count) == 0)
  {
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
static void listModuleId(void)
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
    if (lockf(p_device_pool->lock, F_LOCK, 0))
    {
        fprintf(stderr, "ERROR: %s() lockf() failed: %s\n", __func__,
                strerror(NI_ERRNO));
    }
#endif

    /* print out coders in their current order */
    ptr = p_device_pool->p_device_queue;
    for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
    {
        printf("Num %ss: %u\n", g_device_type_str[k], ptr->xcoder_cnt[k]);
        for (i = 0; i < ptr->xcoder_cnt[k]; i++)
        {
            ni_device_info_t *p_device_info =
                ni_rsrc_get_device_info(k, ptr->xcoders[k][i]);
            printf("[%u]. %d (load: %d inst: %u   %s.%d)\n", i,
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
    if (lockf(p_device_pool->lock, F_ULOCK, 0))
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
static void listAllCodersFull(void)
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
            printf("Num %ss: %d\n", g_device_type_str[k],
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
static void getCoderDetailInfo(void)
{
  ni_device_type_t type;
  int guid;
  ni_device_info_t *p_device_info;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  guid = getInt("Coder module ID: ");
  printf("type: %d  id  %d\n", type, guid);
  p_device_info = ni_rsrc_get_device_info(type, guid);
  if (p_device_info)
  {
    ni_rsrc_print_device_info(p_device_info);
    free(p_device_info);
  }
}

/*!******************************************************************************
 *  \brief     compare two int32_t for qsort
 *
 *  \param[in] const void *a
 *  \param[in] const void *b
 *
 *  \return    int atoi(numArray)
 *******************************************************************************/
static int compareInt32_t(const void *a, const void *b)
{
    if (*(int32_t *)a < *(int32_t *)b)
    {
        return -1;
    }
    else if (*(int32_t *)a > *(int32_t *)b)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void displayRsrcMon(void)
{
    ni_device_pool_t *p_device_pool = NULL;
    ni_device_queue_t *coders = NULL;
    ni_session_context_t xCtxt = {0};
    int k;

    p_device_pool = ni_rsrc_get_device_pool();
    if (!p_device_pool)
    {
        fprintf(stderr, "FATAL: cannot get devices info\n");
        return;
    }

    printf("**************************************************\n");
    ni_device_session_context_init(&xCtxt);
    coders = p_device_pool->p_device_queue;

    for (k = 0; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
    {
        ni_device_type_t module_type = k;
        ni_session_context_t *sessionCtxt = &xCtxt;
        int i;   // used in later FOR-loop when compiled without c99
        int module_count = 0;
        char module_name[8] = {'\0'};
        int32_t *module_id_arr = NULL;
        ni_device_context_t *p_device_context = NULL;

        if (!IS_XCODER_DEVICE_TYPE(module_type))
        {
            fprintf(stderr, "ERROR: unsupported module_type %d\n", module_type);
            break;
        }

        module_count = coders->xcoder_cnt[module_type];
        strcpy(module_name, g_device_type_str[module_type]);

        module_id_arr = malloc(sizeof(*module_id_arr) * module_count);
        if (!module_id_arr)
        {
            fprintf(stderr, "ERROR: malloc() failed for module_id_arr\n");
            break;
        }
        memcpy(module_id_arr, coders->xcoders[module_type],
               sizeof(int32_t) * module_count);

        printf("Num %ss: %d\n", g_device_type_str[module_type], module_count);

        // sort module IDs used
        qsort(module_id_arr, module_count, sizeof(int32_t), compareInt32_t);

        // Print performance info headings
        if (IS_XCODER_DEVICE_TYPE(module_type))
        {
            printf("%-5s %-4s %-10s %-4s %-4s %-9s %-7s %-14s\n", "INDEX",
                   "LOAD", "MODEL_LOAD", "INST", "MEM", "SHARE_MEM", "P2P_MEM",
                   "DEVICE");
        }

        /*! query each coder and print out their status */
        for (i = 0; i < module_count; i++)
        {
            p_device_context =
                ni_rsrc_get_device_context(module_type, module_id_arr[i]);

            /*! libxcoder query to get status info including load and instances;*/
            if (p_device_context)
            {
                sessionCtxt->device_handle =
                    ni_device_open(p_device_context->p_device_info->dev_name,
                                   &sessionCtxt->max_nvme_io_size);
                sessionCtxt->blk_io_handle = sessionCtxt->device_handle;

                // Check device can be opened
                if (NI_INVALID_DEVICE_HANDLE == sessionCtxt->device_handle)
                {
                    fprintf(stderr,
                            "ERROR: ni_device_open() failed for %s: %s\n",
                            p_device_context->p_device_info->dev_name,
                            strerror(NI_ERRNO));
                    ni_rsrc_free_device_context(p_device_context);
                    continue;
                }

                sessionCtxt->hw_id = p_device_context->p_device_info->hw_id;
                if (NI_RETCODE_SUCCESS !=
                    ni_device_session_query(sessionCtxt, module_type))
                {
                    ni_device_close(sessionCtxt->device_handle);
                    fprintf(stderr, "Error query %s %s.%d\n", module_name,
                            p_device_context->p_device_info->dev_name,
                            p_device_context->p_device_info->hw_id);
                    ni_rsrc_free_device_context(p_device_context);
                    continue;
                }
                // printf("Done query %s %s.%d\n", module_name,
                //        p_device_context->p_device_info->dev_name,
                //        p_device_context->p_device_info->hw_id);
                ni_device_close(sessionCtxt->device_handle);

                if (0 == sessionCtxt->load_query.total_contexts)
                {
                    sessionCtxt->load_query.current_load = 0;
                }

                // Print performance info row
                if (IS_XCODER_DEVICE_TYPE(module_type))
                {
                    printf("%-5d %-4u %-10u %-4u %-4u %-9u %-7u %-14s\n",
                           p_device_context->p_device_info->module_id,
                           sessionCtxt->load_query.current_load,
                           sessionCtxt->load_query.fw_model_load,
                           sessionCtxt->load_query.total_contexts,
                           sessionCtxt->load_query.fw_video_mem_usage,
                           sessionCtxt->load_query.fw_share_mem_usage,
                           sessionCtxt->load_query.fw_p2p_mem_usage,
                           p_device_context->p_device_info->dev_name);
                }
                ni_rsrc_free_device_context(p_device_context);
            }
        }
        free(module_id_arr);
    }

    ni_device_session_context_clear(&xCtxt);
    ni_rsrc_free_device_pool(p_device_pool);

    printf("**************************************************\n");
}

/*******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void addCoder(void)
{
  char dev[64];
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
static void initRsrc(void)
{
    int should_match_rev =
        getInt("should match current release version, yes (1) no (0): ");
    int timeout_seconds = getInt("timeout_seconds: ");
    ni_rsrc_init(should_match_rev, timeout_seconds);
}

/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void niRsrcRefresh(void)
{
    int should_match_rev =
        getInt("should match current release version, yes (1) no (0): ");
    ni_rsrc_refresh(should_match_rev);
}

/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void checkHwAvailable(void)
{
    int guid = getInt("guid :");
    int deviceType=getInt("deviceType : ");
    ni_rsrc_check_hw_available(guid,deviceType);
}


/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void getlocaldevicelist(void)
{
    ni_rsrc_print_all_devices_capability();
}

/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void deleteCoder(void)
{
  char dev[64];
  getStr("device name (/dev/*): ", dev);
  ni_rsrc_remove_device(dev);
}

/******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 ******************************************************************************/
static void removeAllDevices(void)
{
    ni_rsrc_remove_all_devices();
}


static void checkHWInfo(void)
{
    int task_mode = 0, device_type = 0, hw_mode = 0, consider_mem = 0;
    printf("Please enter task mode, device type, hw mode and if to consider memory (Divided by spaces)\n");
    int ret_scanf = scanf("%d %d %d %d",&task_mode,&device_type,&hw_mode,&consider_mem);
    if(ret_scanf == EOF || ret_scanf < 4)
    {
        fprintf(stderr, "%s, read parameters failed", __func__);
    }
    ni_hw_device_info_quadra_t *device_info = NULL;
    ni_hw_device_info_quadra_threshold_param_t threshold[4];
    threshold[0].device_type = NI_DEVICE_TYPE_DECODER;//0
    threshold[0].load_threshold = 10;
    threshold[0].task_num_threshold = 10;
    threshold[1].device_type = NI_DEVICE_TYPE_ENCODER;//1
    threshold[1].load_threshold = 20;
    threshold[1].task_num_threshold = 10;
    threshold[2].device_type = NI_DEVICE_TYPE_SCALER;//2
    threshold[2].load_threshold = 30;
    threshold[2].task_num_threshold = 10;
    threshold[3].device_type = NI_DEVICE_TYPE_AI;//3
    threshold[3].load_threshold = 40;
    threshold[3].task_num_threshold = 10;

    ni_hw_device_info_quadra_coder_param_t *coder_param = ni_create_hw_device_info_quadra_coder_param(10);
    coder_param->encoder_param->w = 7680;
    coder_param->encoder_param->h = 4320;
    coder_param->encoder_param->lookaheadDepth = 20;
    coder_param->decoder_param->w = 7680;
    coder_param->decoder_param->h = 4320;
    coder_param->decoder_param->hw_frame = 1;
    coder_param->scaler_param->h = 7680;
    coder_param->scaler_param->w = 1920;
    coder_param->scaler_param->bit_8_10 = 8;
    int ret = ni_check_hw_info(&device_info,task_mode,threshold,device_type,coder_param,hw_mode,consider_mem);
    // int ret = ni_check_hw_info(&device_info, 1, threshold, 1, coder_param, 1, 0);
    printf("ret = %d\n",ret);
    ni_destory_hw_device_info_quadra_coder_param(coder_param);
    ni_hw_device_info_free_quadra(device_info);
}

/*******************************************************************************
 *  @brief
 *
 *  @param
 *
 *  @return
 *******************************************************************************/
static void allocAuto(void)
{
  ni_device_context_t *p_device_context;
  ni_device_type_t type;
  ni_alloc_rule_t rule;
  uint64_t model_load;

  type = (ni_device_type_t)getInt("coder type, decoder (0) encoder (1): ");
  rule = (ni_alloc_rule_t)getInt("auto-alloc rule, least-load (0) load-instance (1): ");
  printf("type: %d  rule  %d\n", type, rule);

  p_device_context = ni_rsrc_allocate_auto(type, rule, EN_H265, 1920, 1080, 30, &model_load);
  if (p_device_context)
  {
    printf("Successfully auto-allocated s/w instance on:\n");
    ni_rsrc_print_device_info(p_device_context->p_device_info);
    printf("Allocated load: %"PRIu64"\n", model_load);
    ni_rsrc_free_device_context(p_device_context);
  }
}


int main(void)
{
    int stop = 0;

    while (!stop)
    {
        printf("Key function\n"
            "?       show this help\n"
            "v       change libxcoder log level\n"
            "l       list all decoders, or all encoders\n"
            "L       list all coders detailed info\n"
            "o       list all coders' module #, in order of position in queue\n"
            "g       get a coder's info\n"
#ifndef _WIN32
            "i       Initialize and create all resources required to work with \n"
#endif
            "d       delete xcoder from host resource pool\n"
            "D       Remove all NetInt h/w devices from resource pool on the host.\n"
            "x       add xcoder into host resource pool\n"
            "m       display ni_rsrc_mom value\n"
            "s       Scan and refresh all resources on the host, taking into account\n"
            "        hot-plugged and pulled out cards.\n"
            "r       check the NetInt h/w device in resource pool on the host.\n"
            "p       Print detailed capability information of all devices on the system.\n"
            "C       check hardware device detailed info\n"
            "a       allocate automatically a s/w instance\n"
            "q       quit\n");

        int control = getCmd("> ");
        switch (control)
        {
        case '?':
            continue;
#ifndef _WIN32
        case 'i':
            initRsrc();
            break;
#endif
        case 'D':
            removeAllDevices();
            break;
        case 'v':
            change_log_level();
            break;
        case 'V':
            printf("Release ver: %s\n"
                "API ver:     %s\n"
                "Date:        %s\n"
                "ID:          %s\n",
                NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
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
        case 's':
            niRsrcRefresh();
            break;
        case 'p':
            getlocaldevicelist();
            break;
        case 'x':
            addCoder();
            break;
        case 'd':
            deleteCoder();
            break;
        case 'r':
            checkHwAvailable();
            break;
        case 'm':
            displayRsrcMon();
            break;
        case 'C':
            checkHWInfo();
            break;
        case 'a':
            allocAuto();
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
