#ifndef _NETINT_IOCTL_H
#define _NETINT_IOCTL_H

#define NI_DMABUF_READ_FENCE (1 << 0)
#define NI_DMABUF_SYNC_FILE_OUT_FENCE (1 << 1)
#define NI_DMABUF_SYNC_FILE_IN_FENCE (1 << 2)

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

struct netint_iocmd_issue_request
{
    int fd;
    unsigned int len;
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
#endif /* _NETINT_IOCTL_H */
