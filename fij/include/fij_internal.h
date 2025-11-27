#ifndef _LINUX_FIJ_INTERNAL_H
#define _LINUX_FIJ_INTERNAL_H

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/uprobes.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <uapi/linux/fij.h>

#include "fij_regs.h"

static DECLARE_WAIT_QUEUE_HEAD(fij_mon_wq);


/* Forward decl */
struct task_struct;

struct fij_restore_info {
    struct page *page;      /* The physical page frame to restore */
    unsigned long offset;   /* Offset within that page */
    unsigned char orig_byte;
    bool active;            /* Is there something to restore? */
};

struct fij_ctx {
    /* targeting */
    pid_t              target_tgid;
    unsigned long      target_pc;          /* absolute VA */

    /* status */
    int                running;
    bool target_alive;

    /* threads */
    struct task_struct *bitflip_thread;
    struct task_struct *pc_monitor_thread;

    /* bitflip thread control */
    wait_queue_head_t   flip_wq;          /* thread sleeps here */
    atomic_t            flip_triggered;   /* 0 = idle, 1 = wake/request */

    /* uprobes */
    struct uprobe_consumer uc;
    struct inode       *inj_inode;
    struct uprobe      *inj_uprobe;
    loff_t              inj_off;
    bool                uprobe_active;
    atomic_t            uprobe_disarm_queued;
    struct work_struct  uprobe_disarm_work;
    struct work_struct inject_work;
    atomic_t inject_work_queued;
    
    /* threads completion */
    struct completion monitor_done;
    struct completion bitflip_done;

    /* processes */
    pid_t *targets;   /* array of TGIDs root included */
    int    ntargets;  /* number of valid entries in targets[] */
    int    capacity;

    struct fij_exec exec;
    struct fij_restore_info restore;
};

static const char *fij_reg_name(int id)
{
    switch (id) {
#ifdef CONFIG_X86
    case FIJ_REG_RAX: return "RAX";
    case FIJ_REG_RBX: return "RBX";
    case FIJ_REG_RCX: return "RCX";
    case FIJ_REG_RDX: return "RDX";
    case FIJ_REG_RSI: return "RSI";
    case FIJ_REG_RDI: return "RDI";
    case FIJ_REG_RBP: return "RBP";
    case FIJ_REG_RSP: return "RSP";
    case FIJ_REG_RIP: return "RIP";
    case FIJ_REG_R8:  return "R8";
    case FIJ_REG_R9:  return "R9";
    case FIJ_REG_R10: return "R10";
    case FIJ_REG_R11: return "R11";
    case FIJ_REG_R12: return "R12";
    case FIJ_REG_R13: return "R13";
    case FIJ_REG_R14: return "R14";
    case FIJ_REG_R15: return "R15";
#endif

#ifdef CONFIG_ARM64
    case FIJ_REG_X0:  return "X0";
    case FIJ_REG_X1:  return "X1";
    case FIJ_REG_X2:  return "X2";
    case FIJ_REG_X3:  return "X3";
    case FIJ_REG_X4:  return "X4";
    case FIJ_REG_X5:  return "X5";
    case FIJ_REG_X6:  return "X6";
    case FIJ_REG_X7:  return "X7";
    case FIJ_REG_X8:  return "X8";
    case FIJ_REG_X9:  return "X9";
    case FIJ_REG_X10: return "X10";
    case FIJ_REG_X11: return "X11";
    case FIJ_REG_X12: return "X12";
    case FIJ_REG_X13: return "X13";
    case FIJ_REG_X14: return "X14";
    case FIJ_REG_X15: return "X15";
    case FIJ_REG_X16: return "X16";
    case FIJ_REG_X17: return "X17";
    case FIJ_REG_X18: return "X18";
    case FIJ_REG_X19: return "X19";
    case FIJ_REG_X20: return "X20";
    case FIJ_REG_X21: return "X21";
    case FIJ_REG_X22: return "X22";
    case FIJ_REG_X23: return "X23";
    case FIJ_REG_X24: return "X24";
    case FIJ_REG_X25: return "X25";
    case FIJ_REG_X26: return "X26";
    case FIJ_REG_X27: return "X27";
    case FIJ_REG_X28: return "X28";
    case FIJ_REG_X29: return "X29";      /* FP */
    case FIJ_REG_X30: return "X30";      /* LR */
    case FIJ_REG_SP:  return "SP";
    case FIJ_REG_PC:  return "PC";
#endif

#ifdef CONFIG_RISCV
    case FIJ_REG_ZERO: return "zero";   /* x0  */
    case FIJ_REG_RA:   return "ra";     /* x1  */
    case FIJ_REG_SP:   return "sp";     /* x2  */
    case FIJ_REG_GP:   return "gp";     /* x3  */
    case FIJ_REG_TP:   return "tp";     /* x4  */
    case FIJ_REG_T0:   return "t0";     /* x5  */
    case FIJ_REG_T1:   return "t1";     /* x6  */
    case FIJ_REG_T2:   return "t2";     /* x7  */
    case FIJ_REG_S0:   return "s0";     /* x8  / fp */
    case FIJ_REG_S1:   return "s1";     /* x9  */
    case FIJ_REG_A0:   return "a0";     /* x10 */
    case FIJ_REG_A1:   return "a1";     /* x11 */
    case FIJ_REG_A2:   return "a2";     /* x12 */
    case FIJ_REG_A3:   return "a3";     /* x13 */
    case FIJ_REG_A4:   return "a4";     /* x14 */
    case FIJ_REG_A5:   return "a5";     /* x15 */
    case FIJ_REG_A6:   return "a6";     /* x16 */
    case FIJ_REG_A7:   return "a7";     /* x17 */
    case FIJ_REG_S2:   return "s2";     /* x18 */
    case FIJ_REG_S3:   return "s3";     /* x19 */
    case FIJ_REG_S4:   return "s4";     /* x20 */
    case FIJ_REG_S5:   return "s5";     /* x21 */
    case FIJ_REG_S6:   return "s6";     /* x22 */
    case FIJ_REG_S7:   return "s7";     /* x23 */
    case FIJ_REG_S8:   return "s8";     /* x24 */
    case FIJ_REG_S9:   return "s9";     /* x25 */
    case FIJ_REG_S10:  return "s10";    /* x26 */
    case FIJ_REG_S11:  return "s11";    /* x27 */
    case FIJ_REG_T3:   return "t3";     /* x28 */
    case FIJ_REG_T4:   return "t4";     /* x29 */
    case FIJ_REG_T5:   return "t5";     /* x30 */
    case FIJ_REG_T6:   return "t6";     /* x31 */
    case FIJ_REG_PC:   return "pc";
#endif
    default: return "NONE";
    }
}

/* TEMPORARY */
static inline unsigned long *
fij_reg_ptr_from_ptregs(struct pt_regs *regs, int id)
{
    return fij_reg_ptr_from_ptregs_legacy(regs, id);
}


/* ---- char device ---- */
int  fij_chardev_register(void);
void fij_chardev_unregister(void);

void fij_ctx_init(struct fij_ctx *ctx);

/* ioctl entrypoint */
long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* bitflip_thread.c */
int  fij_random_ms(int min_ms, int max_ms);
int  fij_sleep_hrtimeout_interruptible(unsigned int delay_us);
int  bitflip_thread_fn(void *data);
int  fij_start_bitflip_thread(struct fij_ctx *ctx);
void fij_stop_bitflip_thread(struct fij_ctx *ctx);

/* bitflip_ops.c */
int  fij_group_stop(pid_t tgid);
void fij_group_cont(pid_t tgid);
int  fij_flip_register_from_ptregs(struct fij_ctx *ctx,
                                   struct pt_regs *regs, pid_t tgid);
int  fij_perform_mem_bitflip(struct fij_ctx *ctx, pid_t tgid);
int  fij_stop_flip_resume_one_random(struct fij_ctx *ctx);
int  fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t, pid_t tgid);
void fij_revert_file_backed_bitflip(struct fij_ctx *ctx);

/* ---- uprobes ---- */
int  fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va);
void fij_uprobe_schedule_disarm(struct fij_ctx *ctx);
void fij_uprobe_disarm_sync(struct fij_ctx *ctx);

/* ---- monitor ---- */
int  fij_monitor_start(struct fij_ctx *ctx);
void fij_monitor_stop(struct fij_ctx *ctx);
int fij_wait_task_stopped(struct task_struct *t, long timeout_jiffies);

/* ---- processes ---- */
int fij_collect_descendants(struct fij_ctx *ctx, pid_t root_tgid);


/* ---- exec helper ---- */
int  fij_exec_and_stop(const char *path, char *const argv[], struct fij_ctx *ctx);

/* ---- utilities ---- */
int   fij_va_to_file_off(struct task_struct *t, unsigned long va,
                         struct inode **out_inode, loff_t *out_off);
int   fij_send_cont(pid_t tgid);
struct task_struct *fij_pick_random_user_thread(int tgid, struct fij_ctx *ctx);
struct task_struct *fij_pick_user_thread_by_index(int tgid, int n1, struct fij_ctx *ctx);
struct task_struct *fij_rcu_find_get_task_by_tgid(pid_t tgid);
int fij_pick_random_bit64(void);
enum fij_reg_id fij_pick_random_reg_any(void);
bool choose_register_target(int weight_mem, int only_mem);

/* ---- signal ---- */
int fij_send_sigkill(struct fij_ctx *ctx);

#endif /* _LINUX_FIJ_INTERNAL_H */
