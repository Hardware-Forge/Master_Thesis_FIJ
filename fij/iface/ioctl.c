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

long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct fij_ctx *ctx = file->private_data;

    switch (cmd) {
    case IOCTL_EXEC_AND_FAULT: {
        char **argv = NULL;
        char *path_copy = NULL, *args_copy = NULL, *cursor = NULL;
        int argc = 0, err = 0;

        if (copy_from_user(&ctx->parameters, (void __user *)arg, sizeof(ctx->parameters)))
            return -EFAULT;

        if (READ_ONCE(ctx->running))
            return -EBUSY;

        WRITE_ONCE(ctx->running, 1);

        argv = kcalloc(FIJ_MAX_ARGC + 2, sizeof(char *), GFP_KERNEL);
        if (!argv) { err = -ENOMEM; goto fail_start; }

        path_copy = kstrdup(ctx->parameters.process_path, GFP_KERNEL);
        if (!path_copy) { err = -ENOMEM; goto fail_start; }
        argv[argc++] = path_copy;

        if (ctx->parameters.process_args[0]) {
            args_copy = kstrdup(ctx->parameters.process_args, GFP_KERNEL);
            if (!args_copy) { err = -ENOMEM; goto fail_start; }

            cursor = args_copy;
            while (argc < FIJ_MAX_ARGC + 1) {
                char *tok = strsep(&cursor, " ");
                if (!tok) break;
                if (*tok) argv[argc++] = tok;
            }
        }
        argv[argc] = NULL;

        err = fij_exec_and_stop(path_copy, argv, &ctx->target_tgid);
        if (err)
            goto fail_start;

        if (ctx->target_tgid < 0) {
            pr_err("launched '%s' not found\n", ctx->parameters.process_name);
            err = -ESRCH;
            goto fail_start;
        }
        pr_info("launched '%s' (TGID %d)\n", ctx->parameters.process_name, ctx->target_tgid);

        WRITE_ONCE(ctx->target_alive, true);

        /* if PC delay is specified initialize parameter */
        if (ctx->parameters.target_pc_present) {
            /* Compute absolute VA */
            if (ctx->parameters.target_pc) {
                struct pid *p_tmp = find_get_pid(ctx->target_tgid);
                struct task_struct *t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
                struct mm_struct *m = t ? get_task_mm(t) : NULL;
    
                if (!t || !m) {
                    if (m) mmput(m);
                    if (p_tmp) put_pid(p_tmp);
                    err = -EFAULT;
                    goto fail_start;
                }

                ctx->target_pc = m->start_code + ctx->parameters.target_pc;
                mmput(m);
                put_pid(p_tmp);
                
            } else {
                struct pid *p_tmp = find_get_pid(ctx->target_tgid);
                struct task_struct *t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
                struct mm_struct *m = t ? get_task_mm(t) : NULL;
                ctx->target_pc = m->start_code;
            }
        }

        /* Start monitor thread to clean up when target exits */
        err = fij_monitor_start(ctx);
        if (err)
            goto fail_start;

        /* Resume process */
        err = fij_send_cont(ctx->target_tgid);
        if (err)
            goto fail_start;

fail_start:
        /* best-effort cleanup / state reset */
        kfree(argv);
        kfree(args_copy);
        kfree(path_copy);
        return err;
    }

    default:
        return -EINVAL;
    }
}
