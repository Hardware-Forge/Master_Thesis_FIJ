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

int DEFAULT_MIN_DELAY_MS = 0;
int DEFAULT_MAX_DELAY_MS = 1000;

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

void fij_group_cont(pid_t tgid)
{
    struct task_struct *g = NULL;

    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (g)
        send_sig(SIGCONT, g, 1);
    rcu_read_unlock();
}

static int fij_sleep_hrtimeout_interruptible_ns(u64 delay_ns)
{
    ktime_t kt;

    if (!delay_ns)
        return 0;

    kt = ktime_set(0, delay_ns);

    set_current_state(TASK_INTERRUPTIBLE);
    return schedule_hrtimeout(&kt, HRTIMER_MODE_REL);
}

/* Helper: random ms in [min,max] (inclusive) */
int fij_random_ms(int min_ms, int max_ms)
{
    if (max_ms < min_ms) {
        int tmp = min_ms; min_ms = max_ms; max_ms = tmp;
    }
    int span = (max_ms - min_ms) + 1u;

    return min_ms + (int)get_random_u32_below(span);
    
}

int fij_sleep_hrtimeout_interruptible(unsigned int delay_us)
{
    ktime_t kt;

    if (!delay_us)
        return 0;

    /* schedule_hrtimeout expects ktime in ns */
    kt = ktime_set(0, (u64)delay_us * NSEC_PER_USEC);
    pr_info("FIJ: sleep %u us (%lld ns)\n",
            delay_us, (long long)ktime_to_ns(kt));

    /* Put task into interruptible state so signals/kthread_stop can wake it */
    set_current_state(TASK_INTERRUPTIBLE);
    return schedule_hrtimeout(&kt, HRTIMER_MODE_REL);
}

int bitflip_thread_fn(void *data)
{
    struct fij_ctx *ctx = data;
    int min = READ_ONCE(ctx->parameters.min_delay_ms);
    int max = READ_ONCE(ctx->parameters.max_delay_ms);
    int min_ms = (min > 0) ? min : DEFAULT_MIN_DELAY_MS;
    int max_ms = max ? max : DEFAULT_MAX_DELAY_MS;
    int delay_ms, ret;

    init_completion(&ctx->bitflip_done);
    set_freezable();

    if (READ_ONCE(ctx->parameters.target_pc_present)) {
        /* Deterministic mode: sleep until uprobe triggers us */
        pr_info("fij: bitflip_thread: waiting for uprobe trigger\n");

        /* Wait until flip_triggered is set, or thread stop requested */
        wait_event_killable(ctx->flip_wq,
                            atomic_read(&ctx->flip_triggered) || kthread_should_stop());

        if (kthread_should_stop())
            goto out;

        /* If target died, abort */
        if (!READ_ONCE(ctx->target_alive)) {
            pr_info("fij: bitflip_thread: target not alive, abort\n");
            goto out;
        }

        /* perform the single injection */
        ret = fij_stop_flip_resume_one_random(ctx);
        if (ret == -ESRCH)
            pr_info("FIJ: target TGID %d gone; aborting bitflip\n", ctx->target_tgid);

        /* Clear trigger (not strictly required since thread exits) */
        atomic_set(&ctx->flip_triggered, 0);

    } else {
        /* Nondeterministic mode: sleep a random interval then inject */

        /* Normalize bounds */
        if (max_ms < min_ms) {
            int tmp = min_ms;
            min_ms = max_ms;
            max_ms = tmp;
        }

        if (max_ms <= 0) {
            delay_ms = 0;
        } else if (max_ms < 500) {
            /*
             * For short windows (< 500 ms), use high-resolution sleep.
             * min/max are in ms; we pick a random delay in [min_ms, max_ms] ms
             * and convert it to ns for schedule_hrtimeout.
             *
             * Example: max_ms = 350
             *  -> delay_ms in [0, 350]
             *  -> delay_ns in [0, 350 * 1_000_000] ns
             */
            delay_ms = fij_random_ms(min_ms, max_ms);
            if (delay_ms > 0) {
                u64 delay_ns = (u64)delay_ms * 1000000ULL; /* ms -> ns */
                ret = fij_sleep_hrtimeout_interruptible_ns(delay_ns);
                if (ret) /* interrupted by signal / kthread_stop */
                    goto out;
            }
        } else {
            /*
             * For longer windows (>= 500 ms), msleep is sufficient and cheaper.
             */
            delay_ms = fij_random_ms(min_ms, max_ms);
            if (delay_ms > 0) {
                if (msleep_interruptible(delay_ms))
                    goto out;  /* interrupted */
            }
        }

        if (!READ_ONCE(ctx->target_alive) || kthread_should_stop())
            goto out;

        if (fij_stop_flip_resume_one_random(ctx) == -ESRCH)
            pr_info("FIJ: target TGID %d gone; aborting bitflip\n", ctx->target_tgid);
    }

out:
    complete(&ctx->bitflip_done);
    WRITE_ONCE(ctx->bitflip_thread, NULL);
    return 0;
}


int fij_flip_register_from_ptregs(struct fij_ctx *ctx, struct pt_regs *regs, pid_t tgid)
{
    int target_reg = ctx->parameters.target_reg;
    /* if reg is null pick random value */
    if (!ctx->parameters.target_reg)
        target_reg = fij_pick_random_reg_any();
    /* if bit is null pick a random value */
    int bit = ctx->parameters.reg_bit_present ? ctx->parameters.reg_bit : fij_pick_random_bit64();
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

    return 0;
}

int fij_perform_mem_bitflip(struct fij_ctx *ctx, pid_t tgid)
{
    struct task_struct *task = NULL;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;

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
            target_addr, tgid, orig_byte, flipped_byte);

out_put_mm:
    if (mm) mmput(mm);
out_put_task:
    if (task) put_task_struct(task);
    return ret;
}

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
        if (choose_register_target(ctx->parameters.weight_mem, ctx->parameters.only_mem) || ctx->parameters.target_reg != FIJ_REG_NONE) {
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

    if (ctx->parameters.process_present) {
        /* the index of the process was chosen */
        idx = ctx->parameters.nprocess;
        if (idx > ctx->ntargets || idx < 0) {
            idx = (int)get_random_u32_below(ctx->ntargets);
        }
    }
    else {
        /* Choose TGID of process to stop */
        idx = (int)get_random_u32_below(ctx->ntargets);
    }
    pid_t tgid = ctx->targets[idx];

    if (READ_ONCE(ctx->parameters.all_threads))
        return fij_stop_flip_resume_all_threads(ctx, tgid);

    if (ctx->parameters.thread_present)
        t = fij_pick_user_thread_by_index(tgid, ctx->parameters.thread);
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

    /* Resume the whole group */
    fij_group_cont(tgid);

    put_task_struct(t);
    return ret;
}


int fij_flip_for_task(struct fij_ctx *ctx, struct task_struct *t, pid_t tgid)
{
    struct pt_regs *regs = task_pt_regs(t);

    if (choose_register_target(ctx->parameters.weight_mem, ctx->parameters.only_mem) || ctx->parameters.target_reg != FIJ_REG_NONE) {
        if (!regs)
            return -EINVAL;
        return fij_flip_register_from_ptregs(ctx, regs, tgid);
    }
    else {
        return fij_perform_mem_bitflip(ctx, tgid);
    }
    
}

int fij_start_bitflip_thread(struct fij_ctx *ctx)
{
    if (ctx->bitflip_thread)
        return -EBUSY;

    ctx->bitflip_thread = kthread_run(bitflip_thread_fn, ctx, "fij_bitflip");
    if (IS_ERR(ctx->bitflip_thread)) {
        int err = PTR_ERR(ctx->bitflip_thread);
        ctx->bitflip_thread = NULL;
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
