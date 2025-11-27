// bitflip_thread.c

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

/*
 * These are declared in bitflip_ops.c and/or other TU's.
 * Make sure fij_internal.h declares them too.
 */
extern int fij_stop_flip_resume_one_random(struct fij_ctx *ctx);

/* Internal helper: high-resolution sleep in ns */
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
    int min = READ_ONCE(ctx->exec.params.min_delay_ms);
    int max = READ_ONCE(ctx->exec.params.max_delay_ms);
    int min_ms = (min > 0) ? min : DEFAULT_MIN_DELAY_MS;
    int max_ms = max ? max : DEFAULT_MAX_DELAY_MS;
    int delay_ms, ret;
    u64 duration_ns = 0;

    init_completion(&ctx->bitflip_done);
    set_freezable();

    if (READ_ONCE(ctx->exec.params.target_pc_present)) {
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
            delay_ms = fij_random_ms(min_ms, max_ms * NSEC_PER_MSEC);
            if (delay_ms > 0) {
                duration_ns = (u64)delay_ms;
                ret = fij_sleep_hrtimeout_interruptible_ns(duration_ns);
                if (ret) /* interrupted by signal / kthread_stop */
                    goto out;
            }
        } else {
            /*
             * For longer windows (>= 500 ms), msleep is sufficient and cheaper.
             */
            delay_ms = fij_random_ms(min_ms, max_ms);
            duration_ns = (u64)delay_ms * NSEC_PER_MSEC;
            if (delay_ms > 0) {
                if (msleep_interruptible(delay_ms))
                    goto out;  /* interrupted */
            }
        }

        if (ret || !READ_ONCE(ctx->target_alive) || kthread_should_stop())
            goto out;

        if (fij_stop_flip_resume_one_random(ctx) == -ESRCH)
            pr_info("FIJ: target TGID %d gone; aborting bitflip\n", ctx->target_tgid);
    }

out:
    WRITE_ONCE(ctx->exec.result.injection_time_ns, duration_ns);
    complete(&ctx->bitflip_done);
    WRITE_ONCE(ctx->bitflip_thread, NULL);
    return 0;
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
    } else {
        pr_info("fij: bitflip_stop: no thread\n");
    }
}
