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
    int target_pc;     /* offset from start_code in INT */
    int target_pc_present;
    int target_reg;             /* enum fij_reg_id */
    /* bit to flip in register */
    int reg_bit;
    int reg_bit_present;
    /* The formula that decides the probability is 1/(1+mem_wheight) */
    int weight_mem;
    /* param to inject only in memory */
    int only_mem;
    /* process duration. min DEFAUTLS to 0, max should be specified by the user but DEFAULTS to 200ms */
    int min_delay_ms;
    int max_delay_ms;
    /* params for deterministic thread injection */
    int thread_present;
    int thread;
    int all_threads;
    /* params for deterministic process injection */
    int nprocess; // order in array is root-lchild-lchildchild1-...-rchild-rchildchild1-...
    int process_present;
    int no_injection; // no injection is performed
};

struct fij_result {
    __s32 exit_code;
    __s32 target_tgid;
    __s32 fault_injected; // 1/0
    __u64 duration_ns;
    __u64 seq_no;
};

struct fij_exec {
    struct fij_params params;  // [in]  from userspace
    struct fij_result result;  // [out] to userspace
};

/* IOCTLs */
#define IOCTL_START_FAULT     _IOW('f', 1, struct fij_params)
#define IOCTL_EXEC_AND_FAULT  _IOWR('f', 2, struct fij_exec)

#endif /* _UAPI_LINUX_FIJ_H */
