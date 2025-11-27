#include "fij_internal.h"
#include <linux/uaccess.h>
#include <linux/slab.h>

int fij_build_argv_from_params(const struct fij_params *params, char ***argv_out, char **path_out, char **args_buf_out)
{
    char **argv = NULL;
    char *path_copy = NULL, *args_copy = NULL, *cursor = NULL;
    int argc = 0;

    argv = kcalloc(FIJ_MAX_ARGC + 2, sizeof(char *), GFP_KERNEL);
    if (!argv) return -ENOMEM;

    path_copy = kstrdup(params->process_path, GFP_KERNEL);
    if (!path_copy) { kfree(argv); return -ENOMEM; }
    argv[argc++] = path_copy;

    if (params->process_args[0]) {
        args_copy = kstrdup(params->process_args, GFP_KERNEL);
        if (!args_copy) { kfree(argv); kfree(path_copy); return -ENOMEM; }

        cursor = args_copy;
        while (argc < FIJ_MAX_ARGC + 1) {
            char *tok = strsep(&cursor, " ");
            if (!tok) break;
            if (*tok) argv[argc++] = tok;
        }
    }
    argv[argc] = NULL;

    *argv_out = argv;
    *path_out = path_copy;
    *args_buf_out = args_copy;
    return 0;
}

static int fij_start_exec(struct fij_ctx *ctx)
{
    char **argv = NULL;
    char *path_copy = NULL;
    char *args_buf = NULL;
    int err = 0;

    if (READ_ONCE(ctx->running))
        return -EBUSY;

    /* Build argv[] and copies from ctx->exec.params */
    err = fij_build_argv_from_params(&ctx->exec.params,
                                     &argv, &path_copy, &args_buf);
    if (err)
        goto out;

    /* Exec target and stop it under our control */
    err = fij_exec_and_stop(path_copy, argv, ctx);
    if (err)
        goto out;

    WRITE_ONCE(ctx->running, 1);

    if (ctx->target_tgid < 0) {
        pr_err("launched '%s' not found\n", ctx->exec.params.process_name);
        err = -ESRCH;
        goto out;
    }

    pr_info("launched '%s' (TGID %d)\n",
            ctx->exec.params.process_name, ctx->target_tgid);

    WRITE_ONCE(ctx->target_alive, true);

    /* If PC delay is specified initialize parameter */
    if (ctx->exec.params.target_pc_present) {
        struct pid *p_tmp;
        struct task_struct *t;
        struct mm_struct *m;

        p_tmp = find_get_pid(ctx->target_tgid);
        t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
        m = t ? get_task_mm(t) : NULL;

        if (!t || !m) {
            if (m)
                mmput(m);
            if (p_tmp)
                put_pid(p_tmp);
            err = -EFAULT;
            goto out;
        }

        if (ctx->exec.params.target_pc)
            ctx->target_pc = m->start_code + ctx->exec.params.target_pc;
        else
            ctx->target_pc = m->start_code;

        mmput(m);
        put_pid(p_tmp);
    }

    /* Start monitor thread to clean up when target exits */
    err = fij_monitor_start(ctx);
    if (err)
        goto out;

    /* Resume process */
    err = fij_send_cont(ctx->target_tgid);
    if (err)
        goto out;

    /* Success: we return without waiting for monitor_done */
    goto out;

out:
    /* best-effort cleanup / state reset */
    kfree(argv);
    kfree(args_buf);
    kfree(path_copy);

    if (err)
        WRITE_ONCE(ctx->running, 0);

    return err;
}

long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct fij_ctx *ctx = file->private_data;

    switch (cmd) {

    case IOCTL_SEND_MSG: {
        struct fij_params u;

        if (copy_from_user(&u, (void __user *)arg, sizeof(u)))
            return -EFAULT;

        /* store params in ctx */
        ctx->exec.params = u;
        ctx->exec.result.iteration_number = u.iteration_number;
        pr_info("send iteration number %d", ctx->exec.result.iteration_number);

        /* we are starting a fresh run */
        reinit_completion(&ctx->monitor_done);

        return fij_start_exec(ctx);
    }

    case IOCTL_RECEIVE_MSG: {
        struct fij_result res;

        /*
         * if the monitor hasn't completed yet,
         * tell userspace to try again later.
         */
        if (!completion_done(&ctx->monitor_done))
            return -EAGAIN;

        res = ctx->exec.result;
        pr_info("receive iteration number %d", res.iteration_number);
        pr_info("receive targetid PID %d", res.target_tgid);

        if (copy_to_user((void __user *)arg, &res, sizeof(res)))
            return -EFAULT;

        return 0;
    }

    case IOCTL_EXEC_AND_FAULT: {
        pr_info("started IOCTL EXEC");
        struct fij_exec u;
        int err;

        /* Only params are input, result is output */
        if (copy_from_user(&u, (void __user *)arg, sizeof(u.params)))
            return -EFAULT;

        ctx->exec.params = u.params;
        ctx->exec.result.iteration_number = u.result.iteration_number;

        reinit_completion(&ctx->monitor_done);

        err = fij_start_exec(ctx);
        if (err)
            return err;

        /* Old behaviour: block until monitor is done */
        if (wait_for_completion_interruptible(&ctx->monitor_done))
            return -ERESTARTSYS;

        if (copy_to_user(&((struct fij_exec __user *)arg)->result,
                         &ctx->exec.result,
                         sizeof(ctx->exec.result)))
            return -EFAULT;

        return 0;
    }
    
    case IOCTL_KILL_TARGET: {
        int ret;

        /*
         * Optional: only allow if we think there is a running target.
         * This is cheap and avoids pointless kill attempts.
         */
        if (!READ_ONCE(ctx->running) || ctx->target_tgid <= 0)
            return -ESRCH;

        pr_info("IOCTL_KILL_TARGET: sending SIGKILL to TGID %d\n",
                ctx->target_tgid);

        ret = fij_send_sigkill(ctx);

        /*
         * We *don't* complete monitor_done or clear running here.
         * The existing monitor/cleanup path should run when the
         * target actually exits on SIGKILL and set completion etc.
         */
        if (!ret)
            WRITE_ONCE(ctx->target_alive, false);

        return ret;
    }

    default:
        return -EINVAL;
    }
}