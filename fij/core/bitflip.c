#include "fij_internal.h"
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

static int bitflip_thread_fn(void *data)
{
    struct fij_ctx *ctx = data;
    int infinite = (READ_ONCE(ctx->remaining_cycles) < 0);

    init_completion(&ctx->bitflip_done);

    while (!kthread_should_stop()) {
        
        if (!READ_ONCE(ctx->target_alive))
            break;

        int rem = READ_ONCE(ctx->remaining_cycles);
        if (!infinite && rem <= 0)
            break;

        if (fij_stop_flip_resume_one_random(ctx) == -ESRCH) {
            pr_info("FIJ: target TGID %d gone; stopping bitflip\n", ctx->target_tgid);
            break;
        }

        if (!infinite)
            WRITE_ONCE(ctx->remaining_cycles, rem - 1);

        if (msleep_interruptible(ctx->interval_ms))
            break;
    }

    complete(&ctx->bitflip_done);
    WRITE_ONCE(ctx->bitflip_thread, NULL);

    return 0;
}


int fij_flip_register_from_ptregs(struct fij_ctx *ctx, struct pt_regs *regs)
{
    /* if reg is null pick random value */
    if (!ctx->target_reg)
    ctx->target_reg = fij_pick_random_reg_any();
    /* if bit is null pick a random value */
    int bit = (ctx->reg_bit >= 0) ? ctx->reg_bit : fij_pick_random_bit64();
    unsigned long *p = fij_reg_ptr_from_ptregs(regs, ctx->target_reg);

    if (!p) {
        pr_err("bad reg (reg=%d)\n", ctx->target_reg);
        return -EINVAL;
    }
    if (bit < 0 || bit > 63) {
        pr_err("bad bit (bit=%d)\n", bit);
        return -EINVAL;
    }

    unsigned long before = READ_ONCE(*p);
    unsigned long mask = (1UL << bit);
    unsigned long after = before ^ mask;
    WRITE_ONCE(*p, after);

    pr_info("FIJ: flipped %s bit %d (LSB=0): 0x%lx -> 0x%lx (TGID %d)\n",
            fij_reg_name(ctx->target_reg), bit, before, after, current->tgid);

    return 0;
}

int fij_perform_mem_bitflip(struct fij_ctx *ctx)
{
    struct task_struct *task = NULL;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;

    rcu_read_lock();
    task = pid_task(find_vpid(READ_ONCE(ctx->target_tgid)), PIDTYPE_TGID);
    if (!task) {
        rcu_read_unlock();
        pr_err("TGID %d not found\n", ctx->target_tgid);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        pr_err("failed to get mm for TGID %d\n", ctx->target_tgid);
        ret = -EINVAL;
        goto out_put_task;
    }

    mmap_read_lock(mm);
    {
        VMA_ITERATOR(vmi, mm, 0);
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!(vma->vm_flags & VM_IO) && !(vma->vm_flags & VM_PFNMAP))
                count++;
        }

        if (count == 0) {
            mmap_read_unlock(mm);
            ret = -ENOENT;
            goto out_put_mm;
        }

        target_idx = get_random_u32() % count;
        count = 0;
        vma = NULL;
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!(vma->vm_flags & VM_IO) && !(vma->vm_flags & VM_PFNMAP)) {
                if (count == target_idx)
                    break;
                count++;
            }
        }
        if (!vma) {
            mmap_read_unlock(mm);
            ret = -ENOENT;
            goto out_put_mm;
        }

        vma_size = vma->vm_end - vma->vm_start;
        offset = get_random_u32() % vma_size;
        target_addr = vma->vm_start + offset;
    }
    mmap_read_unlock(mm);

    if (access_process_vm(task, target_addr, &orig_byte, 1, 0) != 1) {
        ret = -EFAULT;
        goto out_put_mm;
    }

    bit_to_flip = get_random_u32() % 8;
    flipped_byte = orig_byte ^ (1 << bit_to_flip);

    if (access_process_vm(task, target_addr, &flipped_byte, 1, FOLL_WRITE | FOLL_FORCE) != 1) {
        ret = -EFAULT;
        goto out_put_mm;
    }

    pr_info("bit flipped at 0x%lx (TGID %d): 0x%02x -> 0x%02x\n",
            target_addr, ctx->target_tgid, orig_byte, flipped_byte);

out_put_mm:
    if (mm) mmput(mm);
out_put_task:
    if (task) put_task_struct(task);
    return ret;
}

int fij_stop_flip_resume_one_random(struct fij_ctx *ctx)
{
    struct task_struct *t;
    int ret = 0;

    t = fij_pick_random_user_thread(ctx->target_tgid);
    if (!t)
        return -ESRCH;

    /* Group-stop the process via SIGSTOP (affects all threads) */
    {
        struct task_struct *g;
        rcu_read_lock();
        g = fij_rcu_find_get_task_by_tgid(ctx->target_tgid);
        if (g) get_task_struct(g);
        rcu_read_unlock();

        if (!g) { put_task_struct(t); return -ESRCH; }
        /* Send SIGSTOP to group leader to ensure a group stop */
        send_sig(SIGSTOP, g, 1);
        put_task_struct(g);
    }

    /* Wait for the chosen thread to be stopped */
    ret = fij_wait_task_stopped(t, msecs_to_jiffies(500));
    if (!ret) {
        /* Flip only this thread's saved user regs */
        ret = fij_flip_for_task(ctx, t);
    }

    /* Resume the whole group */
    {
        struct task_struct *g;
        rcu_read_lock();
        g = fij_rcu_find_get_task_by_tgid(ctx->target_tgid);
        if (g) {
            /* SIGCONT wakes the group */
            send_sig(SIGCONT, g, 1);
        }
        rcu_read_unlock();
    }

    put_task_struct(t);
    return ret;
}

int fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t)
{
    struct pt_regs *regs = task_pt_regs(t);

    if (choose_register_target(ctx->weight_mem)) {
        if (!regs)
            return -EINVAL;
        return fij_flip_register_from_ptregs(ctx, regs);
    }
    else {
        return fij_perform_mem_bitflip(ctx);
    }
    
}

int fij_start_bitflip_thread(struct fij_ctx *ctx)
{
    if (ctx->bitflip_thread)
        return -EBUSY;

    WRITE_ONCE(ctx->running, 1);
    ctx->bitflip_thread = kthread_run(bitflip_thread_fn, ctx, "fij_bitflip");
    if (IS_ERR(ctx->bitflip_thread)) {
        int err = PTR_ERR(ctx->bitflip_thread);
        ctx->bitflip_thread = NULL;
        WRITE_ONCE(ctx->running, 0);
        return err;
    }
    return 0;
}

void fij_stop_bitflip_thread(struct fij_ctx *ctx)
{
    if (ctx->bitflip_thread) {
        pr_info("fij: bitflip_stop: waking and stopping thread pid=%d\n",
                ctx->bitflip_thread->pid);
        kthread_stop(ctx->bitflip_thread);
        pr_info("fij: bitflip_stop: thread stopped\n");
        ctx->bitflip_thread = NULL;
        WRITE_ONCE(ctx->running, 0);
    }
    else {
        pr_info("fij: bitflip_stop: no thread\n");
    }
}
