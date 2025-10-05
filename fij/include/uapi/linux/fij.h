#ifndef _UAPI_LINUX_FIJ_H
#define _UAPI_LINUX_FIJ_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FIJ_DEVICE_NAME "fij"
#define FIJ_MAX_ARGC    4

enum fij_reg_id {
    FIJ_REG_NONE = 0,
    FIJ_REG_RAX, FIJ_REG_RBX, FIJ_REG_RCX, FIJ_REG_RDX,
    FIJ_REG_RSI, FIJ_REG_RDI, FIJ_REG_RBP, FIJ_REG_RSP,
    FIJ_REG_RIP,        /* PC */
    FIJ_REG_MAX
};


struct fij_params {
    char process_name[256];
    char process_path[256];
    char process_args[256];
    int  cycles;
    int target_pc;     /* offset from start_code in INT */
    int  target_reg;             /* enum fij_reg_id */
    int  reg_bit;
    int weight_mem;
};

/* IOCTLs */
#define IOCTL_START_FAULT     _IOW('f', 1, struct fij_params)
#define IOCTL_EXEC_AND_FAULT  _IOW('f', 2, struct fij_params)

#endif /* _UAPI_LINUX_FIJ_H */
