#include <linux/rcupdate.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "fij_internal.h"

/* Must be called under rcu_read_lock() */
static int fij_count_descendants_rcu(struct task_struct *parent)
{
    struct task_struct *child;
    int n = 0;

    /* parent->children is RCU-protected */
    list_for_each_entry_rcu(child, &parent->children, sibling) {
        /* Skip kernel threads and dead tasks */
        if (child->flags & PF_KTHREAD)
            continue;
        if (READ_ONCE(child->exit_state))
            continue;

        n++;
        n += fij_count_descendants_rcu(child);
    }
    return n;
}

/* Must be called under rcu_read_lock() */
static int fij_collect_descendants_preorder_rcu(struct task_struct *parent,
                                                pid_t *out, int max)
{
    struct task_struct *child;
    int n = 0;

    /* parent->children is RCU-protected */
    list_for_each_entry_rcu(child, &parent->children, sibling) {
        if (child->flags & PF_KTHREAD)
            continue;
        if (READ_ONCE(child->exit_state))
            continue;

        /* Append this child (tg leader has pid==tgid) */
        if (n < max)
            out[n++] = child->tgid;
        else
            break;

        if (n < max)
            n += fij_collect_descendants_preorder_rcu(child, out + n, max - n);
    }
    return n;
}

/* collect root + descendants into ctx->targets at runtime. */
int fij_collect_descendants(struct fij_ctx *ctx, pid_t root_tgid)
{
    struct task_struct *root = NULL;
    int count = 0, n = 0, total;
    pid_t *buf;

    if (!ctx)
        return -EINVAL;

    /* Resolve root under RCU, then pin it for later use */
    rcu_read_lock();
    {
        struct pid *pid = find_vpid(root_tgid);
        root = pid ? pid_task(pid, PIDTYPE_TGID) : NULL;
        if (root)
            get_task_struct(root);  /* pin it across unlocks */
    }
    rcu_read_unlock();

    if (!root)
        return -ESRCH;

    /* First pass: count descendants (under RCU) */
    rcu_read_lock();
    count = fij_count_descendants_rcu(root);
    rcu_read_unlock();

    total = count + 1; /* +1 for root */

    /* Ensure capacity outside of RCU */
    if (ctx->capacity < total) {
        buf = kmalloc_array(total, sizeof(*buf), GFP_KERNEL);
        if (!buf) {
            put_task_struct(root);
            return -ENOMEM;
        }
        kfree(ctx->targets);
        ctx->targets  = buf;
        ctx->capacity = total;
    }

    /* Second pass: root first, then pre-order descendants (under RCU) */
    rcu_read_lock();

    n = 0;
    ctx->targets[n++] = root->tgid;

    n += fij_collect_descendants_preorder_rcu(root,
                                              ctx->targets + n,
                                              ctx->capacity - n);
    rcu_read_unlock();

    put_task_struct(root); /* done with root task pointer */

    ctx->ntargets = n;

    return 0; /* success */
}
