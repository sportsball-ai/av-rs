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
 *
 *   \file          ni_rsrc_update_logan.c
 *
 *   @date          May 10, 2019
 *
 *   \brief         NETINT T408 resource update utility program
 *
 *   @author        
 *
 ******************************************************************************/

#include <stdio.h>
#include <fcntl.h> 
#include <string.h>

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#endif 

#include "ni_defs_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_rsrc_priv_logan.h"
#include "ni_util_logan.h"

#ifdef _WIN32
#include "ni_getopt_logan.h"
#define DEV_NAME_PREFIX "\\\\.\\Scsi"
#elif __linux__
#define DEV_NAME_PREFIX "/dev/nvme"
#elif __APPLE__
#define DEV_NAME_PREFIX "/dev/disk"
#endif

/*!******************************************************************************
 *  \brief  get the NVMe device's character device name (e.g. /dev/nvmeX)
 *
 *  \param  in        whole device name passed in
 *          dev_name  full device name without name-space/partition-number
 *
 *  \return    0 if value device name is found, -1 otherwise
 ******************************************************************************/
static int get_dev_name(const char *in, char *dev_name)
{
  char *tmp = NULL;
  
  if ( dev_name && (strlen(in) > strlen(DEV_NAME_PREFIX) && strstr(in, DEV_NAME_PREFIX)) ) 
  {
    tmp = (char *)in + strlen(DEV_NAME_PREFIX);
    while (isdigit(*tmp))
      tmp++;
    strncpy(dev_name, in, tmp - in);
    dev_name[tmp - in] = '\0';
    return 0;
  }
  return -1;
}

static void display_help(void)
{
  printf("-------- ni_rsrc_update_logan v%s --------\n"
         "Update NetInt Logan transcoder resource (encoders and decoders) status.\n"
         "-a <nvme_device>    Create a resource entry for a newly active transcoder. (eg. /dev/nvme0) \n"
         "-d <nvme_device>    Delete the resource entry for a removed transcoder card. (eg. /dev/nvme0)\n"
         "-r    Init transcoder card resource regardless firmware release version.\n"
         "      Default is to only init cards matching current release version.\n"
         "      Warning: this must be used with -a option.\n"
         "-l    Set loglevel of libxcoder_logan API.\n"
         "      [none, fatal, error, info, debug, trace]\n"
         "      Default: info\n"
         "-v    Show libxcoder_logan version.\n"
         "-h    Display this help and exit.\n", NI_LOGAN_XCODER_REVISION);
}

/*!******************************************************************************
 *  \brief  
 *
 *  \param  
 *
 *  \return
 ******************************************************************************/
int main(int argc, char *argv[])
{
  int opt, rc = 0;
  char char_dev_name[64];
  int should_match_rev = 1;
  int add_dev = 0;
  int delete_dev = 0;

  if (argc == 1) {
    display_help();
    return 0;
  }

  // arg handling
  while ((opt = getopt(argc, argv, "hvra:d:l:")) != -1) {
    switch (opt) {
    case 'a':
#ifdef __linux__
      rc = get_dev_name(optarg, char_dev_name);
      add_dev = 1;
#endif
      break;
    case 'd':
#ifdef __linux__
      rc = get_dev_name(optarg, char_dev_name);
      delete_dev = 1;
#endif
      break;
    case 'r':
      should_match_rev = 0;
      break;
    case 'l':
      if (!strcmp(optarg, "none")) {
        ni_log_set_level(NI_LOG_NONE);
      } else if (!strcmp(optarg, "fatal")) {
        ni_log_set_level(NI_LOG_FATAL);
      } else if (!strcmp(optarg, "error")) {
        ni_log_set_level(NI_LOG_ERROR);
      } else if (!strcmp(optarg, "info")) {
        ni_log_set_level(NI_LOG_INFO);
      } else if (!strcmp(optarg, "debug")) {
        ni_log_set_level(NI_LOG_DEBUG);
      } else if (!strcmp(optarg, "trace")) {
        ni_log_set_level(NI_LOG_TRACE);
      } else {
        fprintf(stderr, "unknown log level selected: %s", optarg);
        return 1;
      }
      break;
    case 'v':
      printf("%s\n", NI_LOGAN_XCODER_REVISION);
      return 0;
    case 'h':
    default:
      display_help();
      return 0;
    }
  }

  // check option
  if (add_dev && delete_dev)
  {
    fprintf(stderr, "Error: can not add and delete device at the same time\n\n");
    display_help();
    return 1;
  }
  if (!should_match_rev && !add_dev)
  {
    fprintf(stderr, "Error: -r option must be used with -a option\n\n");
    display_help();
    return 1;
  }

  if (add_dev)
  {
    if (rc || (rc = ni_logan_rsrc_add_device(char_dev_name, should_match_rev)))
      printf("%s not added as transcoder.\n", optarg);
    else
      printf("Added transcoder %s successfully.\n", char_dev_name);
    return rc;
  }
  else if (delete_dev)
  {
    if (rc || (rc = ni_logan_rsrc_remove_device(char_dev_name)))
      printf("%s not removed as transcoder.\n", optarg);
    else
      printf("Removed transcoder %s successfully.\n", char_dev_name);
    return rc;
  }
}
