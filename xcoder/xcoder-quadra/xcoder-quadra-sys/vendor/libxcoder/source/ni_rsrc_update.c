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
 *   \file          ni_rsrc_update.c
 *
 *   @date          may 10, 2019
 *
 *   \brief
 *
 *   @author        
 *
 ******************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "ni_log.h"

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#endif 

#include "ni_defs.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"

#ifdef _WIN32
#include "ni_getopt.h"
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
    printf("Usage: ni_rsrc_update [OPTION]\n"
           "Update NetInt xcoder resource (encoders and decoders) status.\n"
           "\n"
           "  -a device_file    Create a resource entry for a newly active "
           "transcoder card on host\n"
           "  -d device_file    Delete the resource entry for a transcoder "
           "card removed from host\n"
           "  -r                Init transcoder card resource regardless "
           "firmware release version\n"
           "                    Default is to only init cards matching current "
           "release version\n"
           "  -l                Set loglevel of libxcoder API.\n"
           "                    [none, fatal, error, info, debug, trace]\n"
           "                    Default: info\n"
           "  -h                Display this help and exit\n"
           "  -v                Print version info and exit\n");
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
  int add_dev = 1; // default is to add(not delete) a resource
  ni_log_level_t log_level = NI_LOG_INFO;

  if (argc == 1) {
    display_help();
    return 0;
  }

    // arg handling
    while ((opt = getopt(argc, argv, "hvra:d:l:")) != -1)
    {
        switch (opt)
        {
            case 'd':
                add_dev = 0;
            case 'a':
#ifdef __linux__
                rc = get_dev_name(optarg, char_dev_name);
                if (rc)
                {
                    fprintf(stderr, "ERROR: get_dev_name() returned %d\n", rc);
                    return EXIT_FAILURE;
                }
#endif
                break;
            case 'r':
                should_match_rev = 0;
                break;
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                } else {
                    fprintf(stderr, "FATAL: invalid log level selected: %s\n",
                            optarg);
                    exit(1);
                }
                break;
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                return 0;
            case 'h':
            default:
                display_help();
                return 0;
        }
    }

  if (add_dev)
  {
      rc = ni_rsrc_add_device(char_dev_name, should_match_rev);
      if (rc)
          printf("%s not added as transcoder.\n", optarg);
      else
          printf("Added transcoder %s successfully.\n", char_dev_name);
      return rc;
  }
  else
  {
      rc = ni_rsrc_remove_device(char_dev_name);
      if (rc)
          printf("%s not removed as transcoder.\n", optarg);
      else
          printf("Removed transcoder %s successfully.\n", char_dev_name);
      return rc;
  }
}
