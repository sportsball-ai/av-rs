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
 *  \file   ni_p2p_ioctl.h
 *
 *  \brief  Definitions related to NETINT P2P kernel driver interface
 ******************************************************************************/

#ifndef _NETINT_IOCTL_H
#define _NETINT_IOCTL_H

#define NI_DMABUF_READ_FENCE (1 << 0)
#define NI_DMABUF_SYNC_FILE_OUT_FENCE (1 << 1)
#define NI_DMABUF_SYNC_FILE_IN_FENCE (1 << 2)

enum
{
    NI_DMABUF_READ_FROM_DEVICE = 0,
    NI_DMABUF_WRITE_TO_DEVICE = 1,
};

struct netint_iocmd_export_dmabuf
{
    int fd;
    unsigned int flags;
    int domain;
    int bus;
    int dev;
    int fn;
    int bar;
    unsigned long offset;
    unsigned long length;
};

struct netint_iocmd_import_dmabuf {
    int fd;
    unsigned int flags; // reserved for future use
    int domain;
    int bus;
    int dev;
    int fn;
#ifdef __linux__
    __u64 dma_addr;
#else
    unsigned long dma_addr;
#endif
};

struct netint_iocmd_issue_request
{
    int fd;
    unsigned int len;
    int dir;
    unsigned char *data;
};

struct netint_iocmd_attach_rfence
{
    int fd;
    int fence_fd;
    unsigned int flags;
};

struct netint_iocmd_signal_rfence
{
    int fd;
};

#define NETINT_IOCTL_ID _IO('N', 0x80)
#define NETINT_IOCTL_EXPORT_DMABUF                                             \
    _IOWR('N', 0x81, struct netint_iocmd_export_dmabuf)
#define NETINT_IOCTL_ATTACH_RFENCE                                             \
    _IOW('N', 0x82, struct netint_iocmd_attach_rfence)
#define NETINT_IOCTL_SIGNAL_RFENCE                                             \
    _IOW('N', 0x83, struct netint_iocmd_signal_rfence)
#define NETINT_IOCTL_ISSUE_REQ                                                 \
    _IOW('N', 0x85, struct netint_iocmd_issue_request)
#define NETINT_IOCTL_IMPORT_DMABUF                                             \
    _IOW('N', 0x88, struct netint_iocmd_import_dmabuf)
#endif /* _NETINT_IOCTL_H */
