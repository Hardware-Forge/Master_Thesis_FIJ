#include "fij_internal.h"
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define FIJ_SNAP_WINDOW 16

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

/* uprobe hit: called in target context */
static int uprobe_hit(struct uprobe_consumer *uc, struct pt_regs *regs, u64 *bp_addr)
{
    pr_info("fij: uprobe_hit: enter pid=%d\n", current->pid);
    struct fij_ctx *ctx = container_of(uc, struct fij_ctx, uc);

    /* Allow any thread inside the TGID */
    if (current->tgid != READ_ONCE(ctx->target_tgid))
        return 0;

    if(READ_ONCE(ctx->target_reg) != FIJ_REG_NONE) {
        int bit = READ_ONCE(ctx->reg_bit);
        unsigned long *p = fij_reg_ptr_from_ptregs(regs, ctx->target_reg);
        if (!p || bit<0 || bit>63) {
            pr_err("bad reg/bit (reg=%d bit=%d)\n", ctx->target_reg, bit);
            return 0;
        }
        unsigned long before = READ_ONCE(*p);
        unsigned long after  = before ^ (1UL << bit);
        WRITE_ONCE(*p, after);
        pr_info("FIJ: flipped %s bit %d (LSB=0): 0x%lx -> 0x%lx (TGID %d)\n",
                fij_reg_name(ctx->target_reg), bit, before, after, current->tgid);
        
        /* One-shot uprobe, then schedule disarm */
        if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
            if (atomic_xchg(&ctx->uprobe_disarm_queued, 1) == 0)
                schedule_work(&ctx->uprobe_disarm_work);
        }
        
    } else {
        (void)fij_perform_bitflip(ctx);
    
        /* One-shot uprobe, then schedule disarm */
        if (READ_ONCE(ctx->uprobe_active) && ctx->inj_uprobe) {
            if (atomic_xchg(&ctx->uprobe_disarm_queued, 1) == 0)
                schedule_work(&ctx->uprobe_disarm_work);
        }
    
        /* If more cycles requested, spin up the periodic thread */
        if (READ_ONCE(ctx->remaining_cycles) != 1 && !ctx->bitflip_thread)
            (void)fij_start_bitflip_thread(ctx);
    
        pr_info("fij: uprobe_hit: exit  pid=%d\n", current->pid);
    }
    return 0;
    
}

// static int fij_try_register(struct fij_ctx *ctx, unsigned long va,
//                             struct inode **inode_out, loff_t *off_out,
//                             void **handle_out)
// {
//     int err;
//     struct pid *p = NULL;
//     struct task_struct *t = NULL;

//     rcu_read_lock();
//     p = find_get_pid(READ_ONCE(ctx->target_tgid));
//     t = p ? pid_task(p, PIDTYPE_TGID) : NULL;
//     if (t) get_task_struct(t);
//     rcu_read_unlock();

//     if (!t) { if (p) put_pid(p); return -ESRCH; }

//     err = fij_va_to_file_off(t, va, inode_out, off_out);
//     put_task_struct(t);
//     if (p) put_pid(p);
//     if (err) return err;

//     ctx->uc.handler     = uprobe_hit;
//     ctx->uc.ret_handler = NULL;
//     ctx->uc.filter      = NULL;

//     *handle_out = uprobe_register(*inode_out, *off_out, 0, &ctx->uc);
//     if (IS_ERR(*handle_out)) {
//         err = PTR_ERR(*handle_out);
//         *handle_out = NULL;
//         iput(*inode_out);
//         *inode_out = NULL;
//         return err;
//     }
//     return 0;
// }

// int fij_uprobe_arm(struct fij_ctx *ctx, unsigned long target_va)
// {
//     int err;
//     struct pid *p = NULL;
//     struct task_struct *t = NULL;
//     struct mm_struct *m = NULL;
//     struct vm_area_struct *vma;

//     if (READ_ONCE(ctx->uprobe_active))
//         return -EBUSY;

//     /* Lookup + basic VMA validation/logging */
//     rcu_read_lock();
//     p = find_get_pid(READ_ONCE(ctx->target_tgid));
//     t = p ? pid_task(p, PIDTYPE_TGID) : NULL;
//     if (t) get_task_struct(t);
//     rcu_read_unlock();
//     if (!t) { if (p) put_pid(p); return -ESRCH; }

//     m = get_task_mm(t);
//     if (!m) { put_task_struct(t); if (p) put_pid(p); return -EFAULT; }

//     mmap_read_lock(m);
//     vma = find_vma(m, target_va);
//     if (!vma || target_va < vma->vm_start) {
//         mmap_read_unlock(m); mmput(m); put_task_struct(t); if (p) put_pid(p);
//         pr_warn("FIJ: VA 0x%lx not mapped in target (TGID %d)\n",
//                 target_va, ctx->target_tgid);
//         return -EINVAL;
//     }
//     pr_info("FIJ: arming uprobe at VA 0x%lx in VMA [0x%lx-0x%lx] flags=%c%c%c\n",
//             target_va, vma->vm_start, vma->vm_end,
//             (vma->vm_flags & VM_READ)  ? 'r' : '-',
//             (vma->vm_flags & VM_WRITE) ? 'w' : '-',
//             (vma->vm_flags & VM_EXEC)  ? 'x' : '-');

//     if (!(vma->vm_flags & VM_EXEC)) {
//         mmap_read_unlock(m); mmput(m); put_task_struct(t); if (p) put_pid(p);
//         pr_warn("FIJ: VA 0x%lx is not executable; refusing to arm\n", target_va);
//         return -EINVAL;
//     }
//     /* Remember VMA range for snapping bounds */
//     unsigned long vma_start = vma->vm_start, vma_end = vma->vm_end;
//     mmap_read_unlock(m);
//     mmput(m);
//     put_task_struct(t);
//     if (p) put_pid(p);

//     /* First attempt: exact VA */
//     {
//         void *handle = NULL;
//         struct inode *inode = NULL;
//         loff_t off = 0;
//         err = fij_try_register(ctx, target_va, &inode, &off, &handle);
//         if (!err) {
//             ctx->inj_inode  = inode;
//             ctx->inj_off    = off;
//             ctx->inj_uprobe = handle;
//             WRITE_ONCE(ctx->uprobe_active, true);
//             return 0;
//         }
//         if (err != -ENOTSUPP) {
//             if (err == -524) err = -EOPNOTSUPP; /* friendlier errno to userspace */
//             pr_err("FIJ: uprobe_register at 0x%lx failed (%d)\n", target_va, err);
//             return err;
//         }
//     }

//     /* Snap forward within FIJ_SNAP_WINDOW (don’t cross VMA end) */
//     for (unsigned int delta = 1; delta <= FIJ_SNAP_WINDOW; delta++) {
//         unsigned long cand = target_va + delta;
//         if (cand >= vma_end) break;

//         void *handle = NULL;
//         struct inode *inode = NULL;
//         loff_t off = 0;

//         err = fij_try_register(ctx, cand, &inode, &off, &handle);
//         if (!err) {
//             pr_warn("FIJ: snapped probe from 0x%lx to next boundary 0x%lx (+%u)\n",
//                     target_va, cand, delta);
//             ctx->inj_inode  = inode;
//             ctx->inj_off    = off;
//             ctx->inj_uprobe = handle;
//             WRITE_ONCE(ctx->uprobe_active, true);
//             return 0;
//         }
//         if (err != -ENOTSUPP) {
//             if (err == -524) err = -EOPNOTSUPP;
//             pr_err("FIJ: uprobe_register at 0x%lx failed (%d) during snap-forward\n",
//                    cand, err);
//             return err;
//         }
//     }

//     pr_warn("FIJ: could not find a valid instruction boundary near 0x%lx (+/-%d bytes); "
//             "try a different pc\n", target_va, FIJ_SNAP_WINDOW);
//     return -EOPNOTSUPP;
// }


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

    /* Newer kernels: uprobe_register returns a handle (struct uprobe *) */
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
