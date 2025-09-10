/* SPDX-License-Identifier: GPL-2.0 */
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
};

/* ---- char device ---- */
int  fij_chardev_register(struct fij_ctx *ctx);
void fij_chardev_unregister(void);

/* ioctl entrypoint */
long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* ---- bitflip ---- */
int  fij_perform_bitflip(struct fij_ctx *ctx);
int  fij_start_bitflip_thread(struct fij_ctx *ctx);
void fij_stop_bitflip_thread(struct fij_ctx *ctx);

/* ---- uprobes ---- */
int  fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va);
void fij_uprobe_schedule_disarm(struct fij_ctx *ctx);
void fij_uprobe_disarm_sync(struct fij_ctx *ctx);

/* ---- monitor ---- */
int  fij_monitor_start(struct fij_ctx *ctx);
void fij_monitor_stop(struct fij_ctx *ctx);

/* ---- exec helper ---- */
int  fij_exec_and_stop(const char *path, char *const argv[]);

/* ---- utilities ---- */
pid_t fij_find_pid_by_name(const char *name);
int   fij_va_to_file_off(struct task_struct *t, unsigned long va,
                         struct inode **out_inode, loff_t *out_off);
int   fij_send_cont(pid_t tgid);

#endif /* _LINUX_FIJ_INTERNAL_H */
