#ifndef _UAPI_LINUX_FIJ_H
#define _UAPI_LINUX_FIJ_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include "fij_config.h"

#define FIJ_DEVICE_NAME "fij"
#define FIJ_MAX_ARGC    4

enum fij_reg_id {
    FIJ_REG_NONE = 0,

#ifdef CONFIG_X86
    FIJ_REG_RAX, FIJ_REG_RBX, FIJ_REG_RCX, FIJ_REG_RDX,
    FIJ_REG_RSI, FIJ_REG_RDI, FIJ_REG_RBP, FIJ_REG_RSP,
    FIJ_REG_RIP,        /* PC */
    FIJ_REG_R8,
    FIJ_REG_R9,
    FIJ_REG_R10,
    FIJ_REG_R11,
    FIJ_REG_R12,
    FIJ_REG_R13,
    FIJ_REG_R14,
    FIJ_REG_R15,
#endif

#ifdef CONFIG_ARM64
    /* AArch64 general-purpose registers x0–x30, plus SP and PC */
    FIJ_REG_X0,
    FIJ_REG_X1,
    FIJ_REG_X2,
    FIJ_REG_X3,
    FIJ_REG_X4,
    FIJ_REG_X5,
    FIJ_REG_X6,
    FIJ_REG_X7,
    FIJ_REG_X8,
    FIJ_REG_X9,
    FIJ_REG_X10,
    FIJ_REG_X11,
    FIJ_REG_X12,
    FIJ_REG_X13,
    FIJ_REG_X14,
    FIJ_REG_X15,
    FIJ_REG_X16,
    FIJ_REG_X17,
    FIJ_REG_X18,
    FIJ_REG_X19,
    FIJ_REG_X20,
    FIJ_REG_X21,
    FIJ_REG_X22,
    FIJ_REG_X23,
    FIJ_REG_X24,
    FIJ_REG_X25,
    FIJ_REG_X26,
    FIJ_REG_X27,
    FIJ_REG_X28,
    FIJ_REG_X29,
    FIJ_REG_X30,
    FIJ_REG_SP,
    FIJ_REG_PC,         /* PC */
#endif

#ifdef CONFIG_RISCV
    /* RISC-V integer regs using ABI names, plus PC */
    FIJ_REG_ZERO,       /* x0  */
    FIJ_REG_RA,         /* x1  */
    FIJ_REG_SP,         /* x2  */
    FIJ_REG_GP,         /* x3  */
    FIJ_REG_TP,         /* x4  */
    FIJ_REG_T0,         /* x5  */
    FIJ_REG_T1,         /* x6  */
    FIJ_REG_T2,         /* x7  */
    FIJ_REG_S0,         /* x8  / fp */
    FIJ_REG_S1,         /* x9  */
    FIJ_REG_A0,         /* x10 */
    FIJ_REG_A1,         /* x11 */
    FIJ_REG_A2,         /* x12 */
    FIJ_REG_A3,         /* x13 */
    FIJ_REG_A4,         /* x14 */
    FIJ_REG_A5,         /* x15 */
    FIJ_REG_A6,         /* x16 */
    FIJ_REG_A7,         /* x17 */
    FIJ_REG_S2,         /* x18 */
    FIJ_REG_S3,         /* x19 */
    FIJ_REG_S4,         /* x20 */
    FIJ_REG_S5,         /* x21 */
    FIJ_REG_S6,         /* x22 */
    FIJ_REG_S7,         /* x23 */
    FIJ_REG_S8,         /* x24 */
    FIJ_REG_S9,         /* x25 */
    FIJ_REG_S10,        /* x26 */
    FIJ_REG_S11,        /* x27 */
    FIJ_REG_T3,         /* x28 */
    FIJ_REG_T4,         /* x29 */
    FIJ_REG_T5,         /* x30 */
    FIJ_REG_T6,         /* x31 */
    FIJ_REG_PC,         /* PC */
#endif

    FIJ_REG_MAX

};


struct fij_params {
    char process_name[256];
    char process_path[1024];
    char process_args[4096];
    char log_path[1024];
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

    int iteration_number;
};

struct fij_result {
    __s32 iteration_number;
    __s32 exit_code;
    __s32 sigal;
    __s32 target_tgid;
    __s32 fault_injected; // 1/0
    __u64 injection_time_ns;
    __u32 memory_flip;
    __u64 target_address;
    __u64 target_before;
    __u64 target_after;
    char register_name[8];
};

struct fij_exec {
    struct fij_params params;  // [in]  from userspace
    struct fij_result result;  // [out] to userspace
};

/* IOCTLs */
#define IOCTL_START_FAULT     _IOW('f', 1, struct fij_params)
#define IOCTL_EXEC_AND_FAULT  _IOWR('f', 2, struct fij_exec)

#define IOCTL_SEND_MSG        _IOW('f', 3, struct fij_params)
#define IOCTL_RECEIVE_MSG     _IOR('f', 4, struct fij_result)
#define IOCTL_KILL_TARGET     _IO('f', 5)

#endif /* _UAPI_LINUX_FIJ_H */
