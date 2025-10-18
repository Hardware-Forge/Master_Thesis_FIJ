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


/* Forward decl */
struct task_struct;

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
    
    /* threads completion */
    struct completion monitor_done;
    struct completion bitflip_done;

    /* processes */
    pid_t *targets;   /* array of TGIDs root included */
    int    ntargets;  /* number of valid entries in targets[] */
    int    capacity;

    struct fij_params parameters;
};

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

/* TEMPORARY */
static inline unsigned long *
fij_reg_ptr_from_ptregs(struct pt_regs *regs, int id)
{
    return fij_reg_ptr_from_ptregs_legacy(regs, id);
}


/* ---- char device ---- */
int  fij_chardev_register(struct fij_ctx *ctx);
void fij_chardev_unregister(void);

/* ioctl entrypoint */
long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* ---- bitflip ---- */
int fij_flip_register_from_ptregs(struct fij_ctx *ctx, struct pt_regs *regs);
int  fij_perform_mem_bitflip(struct fij_ctx *ctx);
int  fij_start_bitflip_thread(struct fij_ctx *ctx);
void fij_stop_bitflip_thread(struct fij_ctx *ctx);
int fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t);
int fij_stop_flip_resume_one_random(struct fij_ctx *ctx);
int fij_group_stop(pid_t tgid);
void fij_group_cont(pid_t tgid);

/* ---- uprobes ---- */
int  fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va);
void fij_uprobe_schedule_disarm(struct fij_ctx *ctx);
void fij_uprobe_disarm_sync(struct fij_ctx *ctx);

/* ---- monitor ---- */
int  fij_monitor_start(struct fij_ctx *ctx);
void fij_monitor_stop(struct fij_ctx *ctx);
int fij_wait_task_stopped(struct task_struct *t, long timeout_jiffies);

/* ---- processes ---- */
int fij_stop_descendants_top_down(struct fij_ctx *ctx, pid_t root_tgid);
int fij_restart_descendants_top_down(const struct fij_ctx *ctx);


/* ---- exec helper ---- */
int  fij_exec_and_stop(const char *path, char *const argv[], pid_t *target_tgid);

/* ---- utilities ---- */
int   fij_va_to_file_off(struct task_struct *t, unsigned long va,
                         struct inode **out_inode, loff_t *out_off);
int   fij_send_cont(pid_t tgid);
struct task_struct *fij_pick_random_user_thread(int tgid);
struct task_struct *fij_pick_user_thread_by_index(int tgid, int n1);
struct task_struct *fij_rcu_find_get_task_by_tgid(pid_t tgid);
int fij_pick_random_bit64(void);
enum fij_reg_id fij_pick_random_reg_any(void);
bool choose_register_target(int weight_mem, int only_mem);

#endif /* _LINUX_FIJ_INTERNAL_H */
