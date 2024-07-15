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
 *  \file   init_rsrc.c
 *
 *  \brief  Application for registering Netint transcoding devices on system
 *          for use by libxcoder
 ******************************************************************************/

#include <stdio.h>
#include <fcntl.h> 

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#elif _WIN32
#include "ni_getopt.h"
#endif 

#include "ni_defs.h"
#include "ni_log.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"

int main(int argc, char *argv[])
{
    int should_match_rev = 1;
    int opt;
    int timeout_seconds = 0;
    ni_log_level_t log_level = NI_LOG_INFO;

    // arg handling
    while ((opt = getopt(argc, argv, "hrt:l:v")) != -1)
    {
        switch (opt)
        {
            case 'h':
                // help message
                printf("-------- init_rsrc v%s --------\n"
                       "Initialize NetInt transcoder resource pool\n"
                       "\n"
                       "-r  Init transcoder card resource regardless of firmware release version to \n"
                       "    libxcoder release version compatibility. Default is to only init cards with \n"
                       "    compatible firmware version.\n"
                       "-t  Set timeout time in seconds for device polling, will exit with failure if \n"
                       "    reached. Default 0s which means no timeout.\n"
                       "-l  Set loglevel of libxcoder API.\n"
                       "    [none, fatal, error, info, debug, trace]\n"
                       "    Default: info\n"
                       "-h  Display this help and exit.\n"
                       "-v  Print version info.\n",
                       NI_XCODER_REVISION);
                return 0;
            case 'r':
                should_match_rev = 0;
                break;
            case 't':
                timeout_seconds = atoi(optarg);
                printf("Timeout will be set %d\n", timeout_seconds);
                break;
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                } else {
                    fprintf(stderr, "FATAL: invalid log level selected: %s\n",
                            optarg);
                    return 1;
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
            default:
                fprintf(stderr, "FATAL: invalid arg '%c'\n", opt);
                return 1;
        }
  }

#if __linux__ || __APPLE__
  return ni_rsrc_init(should_match_rev, timeout_seconds);
#elif _WIN32
  ni_retcode_t retval = ni_rsrc_init(should_match_rev, timeout_seconds);
  if (NI_RETCODE_SUCCESS == retval)
  {
    printf("NETINT Resources Intitialized Successfully\n");
  }

  while (1)
    Sleep(1000);

  return retval;
#endif
}
