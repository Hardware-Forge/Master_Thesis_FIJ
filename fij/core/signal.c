#include <linux/sched/signal.h>
#include "fij_internal.h"

int fij_send_sigkill(struct fij_ctx *ctx)
{
    struct pid *pid;
    int ret;
    /* If we don't have a valid target, bail out */
    if (ctx->target_tgid <= 0)
        return -ESRCH;

    rcu_read_lock();
    pid = find_get_pid(ctx->target_tgid);
    if (!pid) {
        rcu_read_unlock();
        return -ESRCH;
    }

    ret = kill_pid(pid, SIGKILL, 1);

    put_pid(pid);
    rcu_read_unlock();

    return ret;
}