#include "fij_internal.h"
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/slab.h>

static void uprobe_disarm_workfn(struct work_struct *work)
{
    struct fij_ctx *ctx = container_of(work, struct fij_ctx, uprobe_disarm_work);

    if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
        uprobe_unregister_nosync(ctx->inj_uprobe, &ctx->uc);
        uprobe_unregister_sync();
        ctx->inj_uprobe = NULL;
        WRITE_ONCE(ctx->uprobe_active, false);
    }
    if (ctx->inj_inode) {
        iput(ctx->inj_inode);
        ctx->inj_inode = NULL;
    }
    atomic_set(&ctx->uprobe_disarm_queued, 0);
}

static void fij_uprobe_post_actions(struct fij_ctx *ctx)
{
    /* One-shot uprobe, then schedule disarm */
    if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
        if (atomic_xchg(&ctx->uprobe_disarm_queued, 1) == 0)
            schedule_work(&ctx->uprobe_disarm_work);
    }

    /* If more cycles requested, spin up the periodic thread */
    if (READ_ONCE(ctx->remaining_cycles) != 1 && !ctx->bitflip_thread)
        (void)fij_start_bitflip_thread(ctx);
}

/* uprobe hit: called in target context */
static int uprobe_hit(struct uprobe_consumer *uc, struct pt_regs *regs, u64 *bp_addr)
{
    pr_info("fij: uprobe_hit: enter pid=%d\n", current->pid);
    struct fij_ctx *ctx = container_of(uc, struct fij_ctx, uc);

    /* Allow any thread inside the TGID */
    if (current->tgid != READ_ONCE(ctx->target_tgid))
        return 0;

    if (READ_ONCE(ctx->parameters.target_reg) != FIJ_REG_NONE) {
        /* flip the selected register bit */
        (void)fij_flip_register_from_ptregs(ctx, regs);
    } else {
        /* flip memory via existing function */
        (void)fij_perform_mem_bitflip(ctx);
    }
    fij_uprobe_post_actions(ctx);
    return 0;
    
}


int fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va)
{
    struct pid *p = NULL;
    struct task_struct *t = NULL;
    int err;

    if (READ_ONCE(ctx->uprobe_active))
        return -EBUSY;

    rcu_read_lock();
    p = find_get_pid(READ_ONCE(ctx->target_tgid));
    t = p ? pid_task(p, PIDTYPE_TGID) : NULL;
    if (t)
        get_task_struct(t);
    rcu_read_unlock();

    if (!t) {
        if (p) put_pid(p);
        return -ESRCH;
    }

    err = fij_va_to_file_off(t, target_va, &ctx->inj_inode, &ctx->inj_off);

    put_task_struct(t);
    if (p) put_pid(p);

    if (err) {
        pr_err("could not map VA 0x%lx to file offset (%d)\n", target_va, err);
        ctx->inj_inode = NULL;
        return err;
    }

    ctx->uc.handler = uprobe_hit;
    ctx->uc.ret_handler = NULL;
    ctx->uc.filter = NULL;

    ctx->inj_uprobe = uprobe_register(ctx->inj_inode, ctx->inj_off, 0, &ctx->uc);
    if (IS_ERR(ctx->inj_uprobe)) {
        err = PTR_ERR(ctx->inj_uprobe);
        pr_err("uprobe_register failed (%d)\n", err);
        iput(ctx->inj_inode);
        ctx->inj_inode = NULL;
        ctx->inj_uprobe = NULL;
        return err;
    }

    WRITE_ONCE(ctx->uprobe_active, true);
    return 0;
}

void fij_uprobe_schedule_disarm(struct fij_ctx *ctx)
{
    if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
        if (atomic_xchg(&ctx->uprobe_disarm_queued, 1) == 0)
            schedule_work(&ctx->uprobe_disarm_work);
    }
}

void fij_uprobe_disarm_sync(struct fij_ctx *ctx)
{
    pr_info("fij: uprobe_disarm_sync: begin\n");
    /* Flush any in-progress handler and then fully disarm */
    flush_work(&ctx->uprobe_disarm_work);

    if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
        pr_info("fij: uprobe_unregister_nosync()\n");
        uprobe_unregister_nosync(ctx->inj_uprobe, &ctx->uc);
        pr_info("fij: uprobe_unregister_sync()\n");
        uprobe_unregister_sync();
        pr_info("fij: uprobe_unregister_sync() done\n");
        ctx->inj_uprobe = NULL;
        WRITE_ONCE(ctx->uprobe_active, false);
    }
    if (ctx->inj_inode) {
        iput(ctx->inj_inode);
        ctx->inj_inode = NULL;
    }
    atomic_set(&ctx->uprobe_disarm_queued, 0);
}

/* Expose work init helper to main init */
void __init_or_module fij_uprobe_init_work(struct fij_ctx *ctx)
{
    INIT_WORK(&ctx->uprobe_disarm_work, uprobe_disarm_workfn);
    atomic_set(&ctx->uprobe_disarm_queued, 0);
}
