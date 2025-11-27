#include "fij_internal.h"
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/sched.h>
#include <linux/hrtimer.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/highmem.h>

/* Stop entire thread group */
int fij_group_stop(pid_t tgid)
{
    struct task_struct *g = NULL;

    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (g)
        get_task_struct(g);
    rcu_read_unlock();

    if (!g)
        return -ESRCH;

    send_sig(SIGSTOP, g, 1);
    put_task_struct(g);
    return 0;
}

/* Continue entire thread group */
void fij_group_cont(pid_t tgid)
{
    struct task_struct *g = NULL;

    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (g)
        send_sig(SIGCONT, g, 1);
    rcu_read_unlock();
}

/* Flip a register bit from pt_regs */
int fij_flip_register_from_ptregs(struct fij_ctx *ctx, struct pt_regs *regs, pid_t tgid)
{
    int target_reg = ctx->exec.params.target_reg;
    pr_info("target reg is: %d", target_reg);
    /* if reg is null pick random value */
    if (!ctx->exec.params.target_reg)
        target_reg = fij_pick_random_reg_any();
    /* if bit is null pick a random value */
    int bit = ctx->exec.params.reg_bit_present ? ctx->exec.params.reg_bit : fij_pick_random_bit64();
    unsigned long *p = fij_reg_ptr_from_ptregs(regs, target_reg);

    if (!p) {
        pr_err("bad reg (reg=%d)\n", target_reg);
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
            fij_reg_name(target_reg), bit, before, after, tgid);

    strscpy(ctx->exec.result.register_name, fij_reg_name(target_reg), sizeof(ctx->exec.result.register_name));
    WRITE_ONCE(ctx->exec.result.memory_flip, 0);
    WRITE_ONCE(ctx->exec.result.target_before, before);
    WRITE_ONCE(ctx->exec.result.target_after, after);

    return 0;
}

/* Flip a random memory bit in the target process */
int fij_perform_mem_bitflip(struct fij_ctx *ctx, pid_t tgid)
{
    struct task_struct *task = NULL;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;
    bool is_file_backed = false;

    rcu_read_lock();
    task = pid_task(find_vpid(READ_ONCE(tgid)), PIDTYPE_TGID);
    if (!task) {
        rcu_read_unlock();
        pr_err("TGID %d not found\n", tgid);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        pr_err("failed to get mm for TGID %d\n", tgid);
        ret = -EINVAL;
        goto out_put_task;
    }

    mmap_read_lock(mm);
    {
        /* 1. Count valid VMAs */
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
        /* 2. Select a VMA */
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
        /* 3. Check if file-backed */
        if (vma->vm_file) {
            is_file_backed = true;
        }

        vma_size = vma->vm_end - vma->vm_start;
        offset = get_random_u32() % vma_size;
        target_addr = vma->vm_start + offset;
    }
    mmap_read_unlock(mm);
    /* 4. Read Original Byte */
    if (access_process_vm(task, target_addr, &orig_byte, 1, 0) != 1) {
        ret = -EFAULT;
        goto out_put_mm;
    }

    bit_to_flip = get_random_u32() % 8;
    flipped_byte = orig_byte ^ (1 << bit_to_flip);
    /* 5. Write Flipped Byte */
    if (access_process_vm(task, target_addr, &flipped_byte, 1, FOLL_WRITE | FOLL_FORCE) != 1) {
        ret = -EFAULT;
        goto out_put_mm;
    }

    /* * 6. Capture page for restoration if it was file-backed.
     * We do this AFTER the write to ensure we grab the specific page 
     * that was modified (in case of COW).
     */
    if (is_file_backed) {
        struct page *pinned_page = NULL;

        mmap_read_lock(mm);
        int gup_ret = get_user_pages_remote(mm, target_addr, 1,
                                            FOLL_WRITE | FOLL_FORCE, 
                                            &pinned_page, NULL);
        mmap_read_unlock(mm);
        
        if (gup_ret == 1) {
            set_page_dirty_lock(pinned_page);
            ctx->restore.page = pinned_page;
            ctx->restore.offset = target_addr & ~PAGE_MASK; // Offset within the page
            ctx->restore.orig_byte = orig_byte;
            ctx->restore.active = true;
            pr_info("FIJ: File-backed injection detected at 0x%lx. Scheduled for restore.\n", target_addr);
        } else {
            pr_warn("FIJ: Failed to pin file-backed page for restore at 0x%lx\n", target_addr);
        }
    }

    pr_info("bit flipped at 0x%lx (TGID %d): 0x%02x -> 0x%02x\n",
            target_addr, tgid, orig_byte, flipped_byte);
    
    WRITE_ONCE(ctx->exec.result.memory_flip, 1);
    WRITE_ONCE(ctx->exec.result.target_address, target_addr);
    WRITE_ONCE(ctx->exec.result.target_before, orig_byte);
    WRITE_ONCE(ctx->exec.result.target_after, flipped_byte);
    strscpy(ctx->exec.result.register_name, "none", 5);

out_put_mm:
    if (mm) mmput(mm);
out_put_task:
    if (task) put_task_struct(task);
    return ret;
}

/* Stop group, flip all threads once, then resume */
static int fij_stop_flip_resume_all_threads(struct fij_ctx *ctx, pid_t tgid)
{
    struct task_struct *g = NULL, *t;
    int ret = 0, first_err = 0;
    bool did_mem = false;

    /* Stop the whole group via helper */
    ret = fij_group_stop(tgid);
    if (ret)
        return ret;

    /* Reacquire leader with a ref for iteration */
    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (g)
        get_task_struct(g);
    rcu_read_unlock();

    /*
     * Iterate all threads in the group.
     * Note: for_each_thread() requires tasklist safety; the group is
     * stopping and we take a ref while touching each 't'.
     */
    for_each_thread(g, t) {
        int this_ret;

        if (t->flags & PF_KTHREAD)   /* skip kernel threads */
            continue;

        get_task_struct(t);

        /* Wait for this thread to reach stopped state */
        this_ret = fij_wait_task_stopped(t, msecs_to_jiffies(100));
        if (this_ret) {
            if (!first_err) first_err = this_ret;
            put_task_struct(t);
            continue;
        }

        /* Perform the flip */
        if (choose_register_target(ctx->exec.params.weight_mem,
                                   ctx->exec.params.only_mem) ||
            ctx->exec.params.target_reg != FIJ_REG_NONE) {
            struct pt_regs *regs = task_pt_regs(t);
            if (!regs) {
                this_ret = -EINVAL;
            } else {
                this_ret = fij_flip_register_from_ptregs(ctx, regs, tgid);
            }
        } else {
            if (!did_mem) {
                this_ret = fij_perform_mem_bitflip(ctx, tgid);
                did_mem = (this_ret == 0);
            } else {
                this_ret = 0; /* already did the process-wide mem flip */
            }
        }

        if (this_ret && !first_err) first_err = this_ret;

        put_task_struct(t);
    }

    /* Resume the whole group via helper */
    fij_group_cont(tgid);
    put_task_struct(g);

    /*
     * If at least one thread op succeeded, return 0.
     * Otherwise return the first error we saw.
     */
    return first_err;
}

int fij_stop_flip_resume_one_random(struct fij_ctx *ctx)
{
    struct task_struct *t;
    int ret = 0;
    int idx;

    /* Collect processes at runtime */
    ret = fij_collect_descendants(ctx, ctx->target_tgid);

    if (ret)
        return ret;

    /* No targets -> nothing to flip */
    if (ctx->ntargets <= 0)
        return -ESRCH;

    if (ctx->exec.params.process_present) {
        /* the index of the process was chosen */
        idx = ctx->exec.params.nprocess;
        if (idx > ctx->ntargets || idx < 0) {
            idx = (int)get_random_u32_below(ctx->ntargets);
        }
    } else {
        /* Choose TGID of process to stop */
        idx = (int)get_random_u32_below(ctx->ntargets);
    }
    pid_t tgid = ctx->targets[idx];
    WRITE_ONCE(ctx->exec.result.target_tgid, tgid);

    if (READ_ONCE(ctx->exec.params.all_threads))
        return fij_stop_flip_resume_all_threads(ctx, tgid);

    if (ctx->exec.params.thread_present)
        t = fij_pick_user_thread_by_index(tgid, ctx->exec.params.thread);
    else
        t = fij_pick_random_user_thread(tgid);
    if (!t)
        return -ESRCH;

    /* Group-stop the process (affects all threads) */
    ret = fij_group_stop(tgid);
    if (ret) {
        put_task_struct(t);
        return ret;
    }

    /* Wait for the chosen thread to be stopped */
    ret = fij_wait_task_stopped(t, msecs_to_jiffies(100));
    if (!ret) {
        /* Flip only this thread's saved user regs */
        ret = fij_flip_for_task(ctx, t, tgid);
    }

    if (ret == 0) {
        WRITE_ONCE(ctx->exec.result.fault_injected, 1);
    }

    /* Resume the whole group */
    fij_group_cont(tgid);

    put_task_struct(t);
    return ret;
}

int fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t, pid_t tgid)
{
    struct pt_regs *regs = task_pt_regs(t);

    if (choose_register_target(ctx->exec.params.weight_mem,
                               ctx->exec.params.only_mem) ||
        ctx->exec.params.target_reg != FIJ_REG_NONE) {
        if (!regs)
            return -EINVAL;
        return fij_flip_register_from_ptregs(ctx, regs, tgid);
    } else {
        return fij_perform_mem_bitflip(ctx, tgid);
    }
}

void fij_revert_file_backed_bitflip(struct fij_ctx *ctx)
{
    if (!ctx->restore.active || !ctx->restore.page)
        return;

    pr_info("FIJ: Restoring file-backed page for TGID %d\n", ctx->target_tgid);

    /* * Map the page into kernel address space. 
     * kmap_local_page is safe for interrupt contexts/kthreads.
     */
    void *kaddr = kmap_local_page(ctx->restore.page);
    unsigned char *ptr = (unsigned char *)kaddr + ctx->restore.offset;
    
    /* Write back the original value */
    *ptr = ctx->restore.orig_byte;
    
    kunmap_local(kaddr);

    /* * We are done with the page. We grabbed a reference 
     * in get_user_pages_remote, so we must put it now.
     */
    put_page(ctx->restore.page);
    
    ctx->restore.page = NULL;
    ctx->restore.active = false;
}
