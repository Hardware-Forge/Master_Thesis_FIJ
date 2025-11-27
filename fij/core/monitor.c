#include "fij_internal.h"

#include <linux/kthread.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/jiffies.h>

/* Per-module waitqueue to let kthread_stop()/stop path wake the monitor */
static DECLARE_WAIT_QUEUE_HEAD(fij_mon_wq);

struct monitor_args {
    struct task_struct *leader;
    struct fij_ctx     *ctx;
};

static int monitor_thread_fn(void *data)
{
    struct monitor_args *ma = data;
    struct task_struct *leader = ma->leader;
    struct fij_ctx *ctx = ma->ctx;
    int exit_code = 0;
    bool exited = false;

    set_freezable();

    for (;;) {
        bool target_exited = READ_ONCE(leader->exit_state) != 0;
        if (target_exited) {
            exited = true;
            exit_code = READ_ONCE(leader->exit_code);
            break;
        }
        if (kthread_should_stop())
            break;

        wait_event_killable_timeout(fij_mon_wq,
            kthread_should_stop() || READ_ONCE(leader->exit_state),
            msecs_to_jiffies(1));
        try_to_freeze();
    }

    WRITE_ONCE(ctx->target_alive, 0);

    if (ctx->bitflip_thread) {
        pr_info("monitor: target finished, stopping bitflip thread\n");
        if (waitqueue_active(&ctx->flip_wq)) {
            wake_up_all(&ctx->flip_wq);
        }
        fij_stop_bitflip_thread(ctx); 
    }

    if (exited) {
        int sig = exit_code & 0x7f;
        bool coredump = !!(exit_code & 0x80);
        int status = (exit_code >> 8) & 0xff;

        if (sig)
            pr_info("TGID %d terminated by signal %d%s\n",
                    ctx->target_tgid, sig, coredump ? " (core)" : "");
        else
            pr_info("TGID %d exited with status %d\n",
                    ctx->target_tgid, status);
    } else {
        pr_info("monitor thread stopped before target exited\n");
    }

    if (ctx->exec.params.target_pc_present) {
        pr_info("fij: monitor_thread: target exited ... disarming probe\n");
        fij_uprobe_disarm_sync(ctx);
    }

    WRITE_ONCE(ctx->exec.result.exit_code, exit_code);
    
    WRITE_ONCE(ctx->running, 0);
    complete(&ctx->monitor_done);
    WRITE_ONCE(ctx->pc_monitor_thread, NULL);
    

    put_task_struct(leader);
    kfree(ma);
    return 0;
}


int fij_monitor_start(struct fij_ctx *ctx)
{
    init_completion(&ctx->monitor_done);

    struct task_struct *leader;
    struct monitor_args *ma;

    if (READ_ONCE(ctx->pc_monitor_thread))
        return -EBUSY;

    rcu_read_lock();
    leader = pid_task(find_vpid(READ_ONCE(ctx->target_tgid)), PIDTYPE_TGID);
    if (leader)
        get_task_struct(leader);
    rcu_read_unlock();

    if (!leader)
        return -ESRCH;

    ma = kzalloc(sizeof(*ma), GFP_KERNEL);
    if (!ma) {
        put_task_struct(leader);
        return -ENOMEM;
    }
    ma->leader = leader;
    ma->ctx = ctx;

    ctx->pc_monitor_thread = kthread_run(monitor_thread_fn, ma, "fij_monitor");
    int err = 0;
    if (IS_ERR(ctx->pc_monitor_thread)) {
        err = PTR_ERR(ctx->pc_monitor_thread);
        ctx->pc_monitor_thread = NULL;
        put_task_struct(leader);
        kfree(ma);
        return err;
    }

    /* if no_injection == 1, we only monitor; never arm injection */
    if (ctx->exec.params.no_injection)
        return 0;

    /* monitor thread decides the injection method (DET vs NON-DET) */

    init_waitqueue_head(&ctx->flip_wq);
    atomic_set(&ctx->flip_triggered, 0);

    /* bitflip thread is started */
    err = fij_start_bitflip_thread(ctx);

    if (err)
        return err;

    /* Arm uprobe if target_pc != NULL */
    if (ctx->exec.params.target_pc_present) {
        /* probe is inserted at specified PC to start injection deterministically */
        err = fij_uprobe_arm(ctx, ctx->target_pc);
    }

    return err;
}

void fij_monitor_stop(struct fij_ctx *ctx)
{
    struct task_struct *t = READ_ONCE(ctx->pc_monitor_thread);

    if (t) {
        pr_info("fij: monitor_stop: waiting for monitor to finish\n");
        WRITE_ONCE(ctx->target_alive, 0);
        complete(&ctx->monitor_done);
        pr_info("fij: monitor_stop: monitor finished\n");
        WRITE_ONCE(ctx->pc_monitor_thread, NULL);
        pr_info("fij: thread stop");
    }
     else {
        pr_info("fij: monitor_stop: no thread\n");
    }
}

/* wait until 't' is actually stopped by SIGSTOP */
int fij_wait_task_stopped(struct task_struct *t, long timeout_jiffies)
{
    /* We rely on TASK_STOPPED | __TASK_TRACED states */
    while (timeout_jiffies > 0) {
        unsigned long state = READ_ONCE(t->__state);
        if (state & (TASK_STOPPED | __TASK_TRACED))
            return 0;

        if (fatal_signal_pending(current))
            return -EINTR;

        /* Sleep in small chunks to observe state changes */
        set_current_state(TASK_UNINTERRUPTIBLE);
        timeout_jiffies -= schedule_timeout(msecs_to_jiffies(10));
    }
    return -ETIMEDOUT;
}
