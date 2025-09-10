// SPDX-License-Identifier: GPL-2.0
#include "fij_internal.h"
#include <linux/kmod.h>
#include <linux/sched/signal.h>

static int helper_child_init(struct subprocess_info *info, struct cred *new)
{
    /* Executes in the child process context before exec */
    send_sig(SIGSTOP, current, 0);  /* Stop self */
    return 0;
}

int fij_exec_and_stop(const char *path, char *const argv[])
{
    static char *envp[] = {
        "HOME=/",
        "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
        NULL,
    };
    struct subprocess_info *sub_info;
    int ret;

    sub_info = call_usermodehelper_setup(path, (char **)argv, envp, GFP_KERNEL,
                                         helper_child_init, NULL, NULL);
    if (!sub_info)
        return -ENOMEM;

    ret = call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);
    if (ret)
        pr_err("exec failed (%d)\n", ret);
    return ret;
}