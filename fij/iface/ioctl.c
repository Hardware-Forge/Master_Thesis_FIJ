#include "fij_internal.h"
#include <linux/uaccess.h>
#include <linux/slab.h>

long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct fij_ctx *ctx = file->private_data;
    struct fij_params params;

    switch (cmd) {
    case IOCTL_START_FAULT: {
        char **argv = NULL;
        char *path_copy = NULL, *args_copy = NULL, *cursor = NULL;
        int argc = 0, err = 0;

        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;

        /* Start process */
        argv = kcalloc(FIJ_MAX_ARGC + 2, sizeof(char *), GFP_KERNEL);
        if (!argv) { err = -ENOMEM; goto fail_start; }

        path_copy = kstrdup(params.process_path, GFP_KERNEL);
        if (!path_copy) { err = -ENOMEM; goto fail_start; }
        argv[argc++] = path_copy;

        if (params.process_args[0]) {
            args_copy = kstrdup(params.process_args, GFP_KERNEL);
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
            pr_err("launched '%s' not found\n", params.process_name);
            err = -ESRCH;
            goto fail_start;
        }
        pr_info("launched '%s' (TGID %d)\n", params.process_name, ctx->target_tgid);
        /* End of start process */
        
        if (READ_ONCE(ctx->running))
            return -EBUSY;

        ctx->remaining_cycles = (params.cycles == 0) ? -1 : params.cycles;
        ctx->weight_mem = params.weight_mem;
        err = fij_start_bitflip_thread(ctx);
        if (err)
            return err;

        pr_info("started fault injection on '%s' (TGID %d) for %d cycles\n",
                params.process_name, ctx->target_tgid, params.cycles);
        return 0;
    }

    case IOCTL_STOP_FAULT:
        fij_stop_bitflip_thread(ctx);
        /* Optional: also disarm uprobe if armed */
        fij_uprobe_schedule_disarm(ctx);
        return 0;

    case IOCTL_GET_STATUS:
        if (put_user(READ_ONCE(ctx->running), (int __user *)arg))
            return -EFAULT;
        return 0;

    case IOCTL_EXEC_AND_FAULT: {
        char **argv = NULL;
        char *path_copy = NULL, *args_copy = NULL, *cursor = NULL;
        int argc = 0, err = 0;

        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;

        if (READ_ONCE(ctx->running))
            return -EBUSY;

        ctx->target_reg = params.target_reg;
        ctx->reg_bit    = params.reg_bit;

        // here the remaning cycles are always set to 1. the logic has to be changed from cycles to array of PCs
        ctx->remaining_cycles = 1;
        WRITE_ONCE(ctx->running, 1);

        argv = kcalloc(FIJ_MAX_ARGC + 2, sizeof(char *), GFP_KERNEL);
        if (!argv) { err = -ENOMEM; goto fail_start; }

        path_copy = kstrdup(params.process_path, GFP_KERNEL);
        if (!path_copy) { err = -ENOMEM; goto fail_start; }
        argv[argc++] = path_copy;

        if (params.process_args[0]) {
            args_copy = kstrdup(params.process_args, GFP_KERNEL);
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
            pr_err("launched '%s' not found\n", params.process_name);
            err = -ESRCH;
            goto fail_start;
        }
        pr_info("launched '%s' (TGID %d)\n", params.process_name, ctx->target_tgid);

        /* Compute absolute VA */
        if (params.target_pc) {
            struct pid *p_tmp = find_get_pid(ctx->target_tgid);
            struct task_struct *t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
            struct mm_struct *m = t ? get_task_mm(t) : NULL;

            if (!t || !m) {
                if (m) mmput(m);
                if (p_tmp) put_pid(p_tmp);
                err = -EFAULT;
                goto fail_start;
            }
            ctx->target_pc = m->start_code + params.target_pc;
            mmput(m);
            put_pid(p_tmp);
        } else {
            struct pid *p_tmp = find_get_pid(ctx->target_tgid);
            struct task_struct *t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
            struct mm_struct *m = t ? get_task_mm(t) : NULL;
            ctx->target_pc = m->start_code;
        }

        /* Arm uprobe if target_pc != 0 */
        if (ctx->target_pc != 0) {
            err = fij_uprobe_arm(ctx, ctx->target_pc);
            if (err)
                goto fail_start;
        }

        /* Resume process */
        err = fij_send_cont(ctx->target_tgid);
        if (err)
            goto fail_start;

        /* Start monitor thread to clean up when target exits */
        err = fij_monitor_start(ctx);
        if (err)
            goto fail_start;

        /* Success path: free argv holders (argv elements point into path_copy / args_copy) */
        kfree(argv);
        kfree(args_copy);
        kfree(path_copy);
        return 0;

fail_start:
        /* best-effort cleanup / state reset */
        kfree(argv);
        kfree(args_copy);
        kfree(path_copy);
        WRITE_ONCE(ctx->running, 0);
        return err;
    }

    default:
        return -EINVAL;
    }
}
