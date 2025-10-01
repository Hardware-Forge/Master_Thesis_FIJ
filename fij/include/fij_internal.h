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

/* Forward decl */
struct task_struct;

struct fij_ctx {
    /* targeting */
    pid_t              target_tgid;
    unsigned long      target_pc;          /* absolute VA */

    /* status */
    int                running;            /* 0/1 */
    int                remaining_cycles;   /* -1 = infinite */
    unsigned long      interval_ms;

    /* threads */
    struct task_struct *bitflip_thread;
    struct task_struct *pc_monitor_thread;

    /* uprobes */
    struct uprobe_consumer uc;
    struct inode       *inj_inode;
    struct uprobe      *inj_uprobe;
    loff_t              inj_off;
    bool                uprobe_active;
    atomic_t            uprobe_disarm_queued;
    struct work_struct  uprobe_disarm_work;

    /* device */
    struct miscdevice   miscdev;

    struct completion monitor_done;

    int  target_reg;             /* enum fij_reg_id */
    int  reg_bit;
};

static inline unsigned long *fij_reg_ptr_from_ptregs(struct pt_regs *regs, int id)
{
    switch (id) {
    case FIJ_REG_RAX: return &regs->ax;
    case FIJ_REG_RBX: return &regs->bx;
    case FIJ_REG_RCX: return &regs->cx;
    case FIJ_REG_RDX: return &regs->dx;
    case FIJ_REG_RSI: return &regs->si;
    case FIJ_REG_RDI: return &regs->di;
    case FIJ_REG_RBP: return &regs->bp;
    case FIJ_REG_RSP: return &regs->sp;
    case FIJ_REG_RIP: return &regs->ip;
    default: return NULL;
    }
}

static const char *fij_reg_name(int id)
{
    switch (id) {
    case FIJ_REG_RAX: return "RAX"; case FIJ_REG_RBX: return "RBX";
    case FIJ_REG_RCX: return "RCX"; case FIJ_REG_RDX: return "RDX";
    case FIJ_REG_RSI: return "RSI"; case FIJ_REG_RDI: return "RDI";
    case FIJ_REG_RBP: return "RBP"; case FIJ_REG_RSP: return "RSP";
    case FIJ_REG_RIP: return "RIP";
    default: return "NONE";
    }
}

/* ---- char device ---- */
int  fij_chardev_register(struct fij_ctx *ctx);
void fij_chardev_unregister(void);

/* ioctl entrypoint */
long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* ---- bitflip ---- */
int fij_flip_register_from_ptregs(struct fij_ctx *ctx, struct pt_regs *regs);
int  fij_perform_bitflip(struct fij_ctx *ctx);
int  fij_start_bitflip_thread(struct fij_ctx *ctx);
void fij_stop_bitflip_thread(struct fij_ctx *ctx);
int fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t);
int fij_stop_flip_resume_one_random(struct fij_ctx *ctx);

/* ---- uprobes ---- */
int  fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va);
void fij_uprobe_schedule_disarm(struct fij_ctx *ctx);
void fij_uprobe_disarm_sync(struct fij_ctx *ctx);

/* ---- monitor ---- */
int  fij_monitor_start(struct fij_ctx *ctx);
void fij_monitor_stop(struct fij_ctx *ctx);
int fij_wait_task_stopped(struct task_struct *t, long timeout_jiffies);

/* ---- exec helper ---- */
int  fij_exec_and_stop(const char *path, char *const argv[], pid_t *target_tgid);

/* ---- utilities ---- */
pid_t fij_find_pid_by_name(const char *name);
int   fij_va_to_file_off(struct task_struct *t, unsigned long va,
                         struct inode **out_inode, loff_t *out_off);
int   fij_send_cont(pid_t tgid);
struct task_struct *fij_pick_random_user_thread(int tgid);
struct task_struct *fij_rcu_find_get_task_by_tgid(pid_t tgid);

#endif /* _LINUX_FIJ_INTERNAL_H */
