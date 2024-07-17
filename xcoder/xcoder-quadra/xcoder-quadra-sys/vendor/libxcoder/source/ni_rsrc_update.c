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
 *  \file   ni_rsrc_update.c
 *
 *  \brief  Application for managing registration/deregistration of individual
 *          NETINT video processing devices on system
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
 *  \brief  get the NVMe device's block device name (e.g. /dev/nvmeXnY)
 *
 *  \param  in        whole device name passed in
 *          dev_name  full block device name 
 *
 *  \return    0 if value device name is found, -1 otherwise
 ******************************************************************************/
static int get_dev_name(const char *in, char *dev_name)
{
    if (!in || !dev_name)
    {
        ni_log(NI_LOG_ERROR,
               "Error: one or more of the given arguments is NULL.\n");
        return -1;
    }

    // for linux blk name (/dev/nvmeXnY)
    // for apple blk name (/dev/diskX)
    // for android blk name (/dev/nvmeXnY or /dev/block/nvmeXnY)
    // for windows blk name (\\\\.\\PHYSICALDRIVEX)
    strcpy(dev_name, in);

    return 0;

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
           "  -D                Delete ALL the resource entries for transcoder "
           "card on this host\n"
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

int main(int argc, char *argv[])
{
    int opt, rc = 0;
    char char_dev_name[64];
    int should_match_rev = 1;
    int add_dev = 0; // default is to add(not delete) a resource
    int del_dev = 0;
    int delete_all = 0; // delete ALL the resources on the host
    ni_log_level_t log_level = NI_LOG_INFO;

    if (argc == 1) {
        display_help();
        return 0;
    }

    // arg handling
    while ((opt = getopt(argc, argv, "hvrDa:d:l:")) != -1)
    {
        switch (opt)
        {
            case 'd':
                rc = get_dev_name(optarg, char_dev_name);
                if (rc)
                {
                    fprintf(stderr, "ERROR: get_dev_name() returned %d\n", rc);
                    return EXIT_FAILURE;
                }
                del_dev = 1;
                break;
            case 'a':
#ifdef __linux__
                rc = get_dev_name(optarg, char_dev_name);
                if (rc)
                {
                    fprintf(stderr, "ERROR: get_dev_name() returned %d\n", rc);
                    return EXIT_FAILURE;
                }
                add_dev = 1;
#endif
                break;
            case 'D':
                delete_all = 1;
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

    // check option
    if (add_dev && (del_dev || delete_all))
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
        rc = ni_rsrc_add_device(char_dev_name, should_match_rev);
        if (rc)
            printf("%s not added as transcoder.\n", char_dev_name);
        else
            printf("Added transcoder %s successfully.\n", char_dev_name);
        return rc;
    }
    else if (delete_all)
    {
        rc = ni_rsrc_remove_all_devices();
        if (rc)
            printf("Error removing all transcoder resources.\n");
        else
            printf("Removing all transcoder resources successfully.\n");
        return rc;
    }
    else if (del_dev)
    {
        rc = ni_rsrc_remove_device(char_dev_name);
        if (rc)
            printf("%s not removed as transcoder.\n", char_dev_name);
        else
            printf("Removed transcoder %s successfully.\n", char_dev_name);
        return rc;
    }
    else
    {
        fprintf(stderr, "Error: ni_rsrc_update option must be used with -a or -b or -D option\n\n");
        display_help();
        return 1;
    }
}
