/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UAPI_LINUX_FIJ_H
#define _UAPI_LINUX_FIJ_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FIJ_DEVICE_NAME "fij"
#define FIJ_MAX_ARGC    6

struct fij_params {
    char process_name[256];
    char process_path[256];
    char process_args[256];
    int  cycles;                 /* 0 = infinite */
    unsigned long target_pc;     /* offset from start_code */
};

/* IOCTLs */
#define IOCTL_START_FAULT     _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT      _IO('f',  2)
#define IOCTL_GET_STATUS      _IOR('f', 3, int)
#define IOCTL_EXEC_AND_FAULT  _IOW('f', 4, struct fij_params)

#endif /* _UAPI_LINUX_FIJ_H */
