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

    while (!kthread_should_stop() &&
           (READ_ONCE(ctx->remaining_cycles) > 0 || READ_ONCE(ctx->remaining_cycles) == -1)) {

        (void)fij_perform_bitflip(ctx);

        if (READ_ONCE(ctx->remaining_cycles) > 0)
            WRITE_ONCE(ctx->remaining_cycles, ctx->remaining_cycles - 1);

        msleep(READ_ONCE(ctx->interval_ms));
    }

    WRITE_ONCE(ctx->running, 0);
    return 0;
}

int fij_perform_bitflip(struct fij_ctx *ctx)
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
