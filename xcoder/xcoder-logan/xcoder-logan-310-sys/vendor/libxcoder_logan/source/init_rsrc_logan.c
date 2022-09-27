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
 *   \file          init_rsrc_logan.c
 *
 *   @date          April 1, 2018
 *
 *   \brief         NETINT T4XX resource init utility program
 *
 *   @author        
 *
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h> 

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#elif _WIN32
#include "ni_getopt_logan.h"
#endif 

#include "ni_defs_logan.h"
#include "ni_rsrc_api_logan.h"
#include "ni_rsrc_priv_logan.h"
#include "ni_util_logan.h"


/*!******************************************************************************
 *  \brief  
 *
 *  \param  
 *
 *  \return
 *******************************************************************************/
int main(int argc, char *argv[])
{
  ni_logan_retcode_t retval;
  int should_match_rev = 1;
  int opt;
  int timeout_seconds = 0;
  
  // arg handling
  while ((opt = getopt(argc, argv, "hvrt:l:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      // help message
      printf("-------- init_rsrc_logan v%s --------\n"
             "Initialize NetInt Logan transcoder resource pool\n"
             "-r    Init transcoder card resource regardless of firmware release version to \n"
             "      libxcoder_logan release version compatibility. Default is to only init cards with \n"
             "      compatible firmware version.\n"
             "-t    Set timeout time in seconds for device polling, will exit with failure \n"
             "      if reached. Default 0s which means no timeout.\n"
             "-l    Set loglevel of libxcoder_logan API.\n"
             "      [none, fatal, error, info, debug, trace]\n"
             "      Default: info\n"
             "-v    Show libxcoder_logan version.\n"
             "-h    Display this help and exit.\n", NI_LOGAN_XCODER_REVISION);
      return 0;
    case 'r':
      should_match_rev = 0;
      break;
    case 't':
      timeout_seconds = atoi(optarg);
      printf("Timeout will be set %d\n", timeout_seconds);
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
    default:
      fprintf(stderr, "Wrong option\n");
      return 1;
    }
  }

#if __linux__ || __APPLE__
  retval = ni_logan_rsrc_init(should_match_rev, timeout_seconds);
  return retval;
#elif _WIN32
  retval = ni_logan_rsrc_init(should_match_rev, timeout_seconds);
  if (NI_LOGAN_RETCODE_SUCCESS == retval)
  {
    printf("NETINT Logan Resources Intitialized Successfully\n");
    // keep the process only on success
    // just return on init already or failure
    while (1)
      Sleep(1000);
  }
  return retval;
#endif
}
