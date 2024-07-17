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
 *  \file   ni_rsrc_namespace.c
 *
 *  \brief  This utility aims to set the NVMe namespace number for a Quadra NVMe
 *          block device. It can operate on physical devices (PCIe physical
 *          function) or virtual devices (PCIe virtual function). Before setting
 *          namespace number, use SR-IOV to create the PCIe virtual function.
 *          Note that only block device name is accepted for this utility.
 *
 *          To effect the name space change, reload the NVMe driver:
 *              sudo modprobe -r nvme
 *              sudo modprobe nvme
 *              sudo nvme list  #check the result with nvme list
 ******************************************************************************/

#include "ni_device_api.h"
#include "ni_nvme.h"
#ifdef _WIN32
#include "ni_getopt.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define NI_NAMESPACE_SZ                 32

static void usage(void)
{
    printf("usage: ni_rsrc_namespace [OPTION]\n"
           "Provides NETINT QUADRA NVMe block device namespace IO operations.\n"
           "  -v    show version.\n"
           "  -d    the nvme block namespace. Only PF with nsid 1 allowed\n"
           "  -D    the nvme block namespace for target over provision setting\n"
           "  -n    the nvme block namespace count.\n"
           "        Default: 0\n"
           "  -p    overprovision percent. Use exclusive of -n and -s and -q\n"
           "  -q    namespace QoS setting. Use exclusive of -n and -s and -p\n"
           "        Default: 0 disabled.\n"
           "                 1 enable qos\n"
           "                 2 enable qos with overprovision\n"
           "  -s    index of virtual PCIe functions in SR-IOV tied to the \n"
           "        physical PCIe function. '0' to select physical PCIe \n"
           "        function.\n"
           "        Eg. '1' to select the first virtual SR-IOV device tied \n"
           "        to the physical block device defined by '-d' option.\n"
           "        Default: 0\n"
           "  -h    help info.\n");
}

int send_config_ns_command(char* dev, int ns, int sr)
{
    ni_device_handle_t handle = ni_device_open(dev, NULL);
    ni_retcode_t retval;
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                strerror(NI_ERRNO));
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }

    retval = ni_device_config_namespace_num(handle, ns, sr);
    if (retval != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                strerror(NI_ERRNO));
    }
    ni_device_close(handle);
    return retval;
}

int send_config_qos_mode(char *dev, int value)
{
    ni_device_handle_t handle = ni_device_open(dev, NULL);
    ni_retcode_t retval;
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                strerror(NI_ERRNO));
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }

    retval = ni_device_config_qos(handle, value);
    if (retval != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                strerror(NI_ERRNO));
    }
    ni_device_close(handle);
    return retval;
}

int send_config_qos_op(char *dev, char *devt, int op)
{
    ni_retcode_t retval;
    ni_device_handle_t handle = ni_device_open(dev, NULL);
    if (handle == NI_INVALID_DEVICE_HANDLE)
    {
        fprintf(stderr, "ERROR: open %s failure for %s\n", dev,
                strerror(NI_ERRNO));
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }
    ni_device_handle_t handle_t = ni_device_open(devt, NULL);
    if (handle_t == NI_INVALID_DEVICE_HANDLE)
    {
        fprintf(stderr, "ERROR: open %s failure for %s\n", devt,
                strerror(NI_ERRNO));
        ni_device_close(handle);
        exit(-1);
    } else
    {
        printf("Succeed to open block namespace %s\n", dev);
    }

    retval = ni_device_config_qos_op(handle, handle_t, op);
    if (retval != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "ERROR: Config setting failure for %s\n",
                strerror(NI_ERRNO));
    }
    ni_device_close(handle);
    ni_device_close(handle_t);
    return retval;
}

int main(int argc, char *argv[])
{
    ni_retcode_t retval;
    int opt;
    char device_namespace[NI_NAMESPACE_SZ] = {'\0'};
    char device_namespace_OP[NI_NAMESPACE_SZ] = {'\0'};
    int namespace_num = 1;
    int over_provision_percent = -1;
    int sriov_index = 0;
    int qos_mode = -1;
#ifdef __linux__
    struct stat sb;
#endif

    while ((opt = getopt(argc, argv, "d:D:n:p:q:s:hv")) != EOF)
    {
        switch (opt)
        {
            case 'h':
                usage();
                exit(0);
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                exit(0);
            case 'd':
                strcpy(device_namespace, optarg);
#ifdef __linux__
                if (lstat(device_namespace, &sb) != 0 ||
                    (sb.st_mode & S_IFMT) != S_IFBLK)
                {
                    fprintf(stderr, "ERROR: Only block device is supported! "
                            "%s is not block device!\n", device_namespace);
                    exit(-1);
                }
#endif
                break;
            case 'D':
                strcpy(device_namespace_OP, optarg);
                break;
            case 'n':
                // A maximum of 64 namespaces are supported for firmware
                namespace_num = atoi(optarg);
                if (namespace_num < 0 || namespace_num > NI_NAMESPACE_MAX_NUM)
                {
                    fprintf(stderr, "ERROR: The number of namespace cannot "
                            "exceed %d\n", NI_NAMESPACE_MAX_NUM);
                    exit(-1);
                }
                break;
            case 'p':
                // set overprovision %
                over_provision_percent = atoi(optarg);
                if (over_provision_percent > 100 || over_provision_percent < 0)
                {
                    fprintf(stderr, "ERROR: Overprovision percent cannot "
                                    "exceed 100%% or become negative\n");
                    exit(-1);
                }
                break;
            case 'q':
                // 0 disabled, 1 enabled - no idle sharing
                qos_mode = atoi(optarg);
                if (qos_mode < QOS_MODE_DISABLED ||
                    qos_mode > QOS_MODE_ENABLED_SHARE)
                {
                    fprintf(stderr,
                            "ERROR: QoS mode %d not supported\n",
                            qos_mode);
                    exit(-1);
                }
                break;
            case 's':
                sriov_index = atoi(optarg);
                if (sriov_index < 0)
                {
                    fprintf(stderr, "ERROR: Invalid SR-IOV device index: %d\n",
                            sriov_index);
                    exit(-1);
                }
                break;
            default:
                fprintf(stderr, "ERROR: Invalid option: %c\n", opt);
                exit(-1);
        }
    }

    if (device_namespace[0] == '\0')
    {
        fprintf(stderr, "ERROR: missing argument for -d\n");
        exit(-1);
    }
    if (strlen(device_namespace) < 3 ||
        strcmp(device_namespace + strlen(device_namespace) - 2, "n1") != 0)
    {
        fprintf(stderr, "ERROR: Invalid device name %s, need n1, no vf\n", device_namespace);
        exit(-1);
    }

    if (qos_mode != -1)
    {
        if (namespace_num != 1 || sriov_index || over_provision_percent != -1)
        {
            fprintf(stderr,
                    "ERROR: QoS mode -q mutually exclusive of namespace and "
                    "SR-IOV\n");
            exit(-1);
        }
        retval = send_config_qos_mode(device_namespace, qos_mode);
        if (retval == NI_RETCODE_SUCCESS)
        {
            printf("QoS mode setting succeed with number of %d\n", qos_mode);
        }
        return retval;
    }

    if (over_provision_percent != -1)
    {
        if (device_namespace_OP[0] == '\0')
        {
            fprintf(stderr, "ERROR: missing argument for -D\n");
            exit(-1);
        }
        if (namespace_num != 1 || sriov_index || qos_mode != -1)
        {
            fprintf(stderr,
                            "ERROR: Overprovision percent -p mutually exclusive of "
                            "namespace and SR-IOV and QOS mode\n");
            exit(-1);
        }
        retval = send_config_qos_op(device_namespace, device_namespace_OP,
                                    over_provision_percent);
        if (retval == NI_RETCODE_SUCCESS)
        {
            printf("Overprovision percent setting succeed with number of %d\n",
                   over_provision_percent);
        }
        return retval;
    }

    retval = send_config_ns_command(device_namespace, namespace_num, sriov_index);
    if (retval == NI_RETCODE_SUCCESS)
    {
        printf("Namespace setting succeed with number of %d and SR-IOV "
               "index %d\n",
               namespace_num, sriov_index);
    }
    return retval;
}
