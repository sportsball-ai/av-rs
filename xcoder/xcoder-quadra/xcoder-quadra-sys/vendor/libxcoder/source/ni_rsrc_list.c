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
 * \file   ni_rsrc_list.c
 *
 * \brief  Application to query and print info about NETINT video processing
 *         devices on system
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#elif _WIN32 
#include "ni_getopt.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "ni_rsrc_api.h"
#include "ni_util.h"

int32_t main(int argc, char *argv[])
{
    int opt;
    bool list_uninitialized = false;
    ni_log_level_t log_level = NI_LOG_INFO;

    // arg handling
    while ((opt = getopt(argc, argv, "ahvl:")) != -1)
    {
        switch (opt)
        {
            case 'a':
                list_uninitialized = true;
                break;
            case 'h':
                // help message
                printf("-------- ni_rsrc_list v%s --------\n"
                       "Display information for NETINT hardware.\n"
                       "\n"
                       "-a  Print includes info for uninitialized cards.\n"
                       "-h  Display this help and exit.\n"
                       "-v  Print version info.\n"
                       "-l  Set loglevel of libxcoder API.\n"
                       "    [none, fatal, error, info, debug, trace]\n"
                       "    Default: info\n",
                       NI_XCODER_REVISION);
                return 0;
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                return 0;
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
            default:
                fprintf(stderr, "FATAL: invalid arg '%c'\n", opt);
                return 1;
        }
    }

    ni_rsrc_print_all_devices_capability2(list_uninitialized);
    return 0;
}
