#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include "fij_internal.h"

/* * Helper Init Function
 * Runs in the new process context, BEFORE the binary is exec'd.
 */
static int helper_child_init(struct subprocess_info *info, struct cred *new)
{
    /* 1. Retrieve the context passed from call_usermodehelper_setup */
    struct fij_ctx *ctx = (struct fij_ctx *)info->data;
    ctx->target_tgid = task_tgid_vnr(current);
    
    struct file *log_file;
    struct file *null_file;
    int fd_stdin, fd_stdout, fd_stderr;
    char *path = ctx->exec.params.log_path;

    /* If the log file's path is specified by the userspace the STOUT AND STDERR are associated to it */
    if(path && path[0]!= "\0") {
        /* 2. Open the Log File */
        log_file = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (IS_ERR(log_file)) {
            pr_err("fij: Failed to open log file: %s\n", path);
            return PTR_ERR(log_file);
        }
    
        /* 3. Open /dev/null for Stdin */
        null_file = filp_open("/dev/null", O_RDWR, 0);
        if (IS_ERR(null_file)) {
            fput(log_file); // Clean up log_file if null open fails
            return PTR_ERR(null_file);
        }
    
        /* 4. Reserve File Descriptors 0, 1, 2 */
        fd_stdin  = get_unused_fd_flags(0);
        fd_stdout = get_unused_fd_flags(0);
        fd_stderr = get_unused_fd_flags(0);
    
        if (fd_stdin >= 0) {
            fd_install(fd_stdin, null_file);
        } else {
            fput(null_file);
        }
    
        if (fd_stdout >= 0) {
            fd_install(fd_stdout, log_file);
        }
    
        if (fd_stderr >= 0) {
            fd_install(fd_stderr, log_file);
        }
    }

    send_sig(SIGSTOP, current, 0);
    return 0;
}

int fij_exec_and_stop(const char *path, char *const argv[], struct fij_ctx *ctx)
{
    static char *envp[] = {
        "HOME=/",
        "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
        NULL,
    };
    struct subprocess_info *sub_info;
    int ret;

    /* * Setup the helper.
     * The last argument is the 'void *data' that gets passed to helper_child_init.
     * We pass 'ctx' here so we can access log_path inside the helper.
     */
    sub_info = call_usermodehelper_setup(path, (char **)argv, envp, GFP_KERNEL,
                                         helper_child_init, 
                                         NULL, 
                                         ctx);

    if (!sub_info) {
        return -ENOMEM;
    }

    ret = call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);

    if (ret)
        pr_err("fij: exec failed (%d)\n", ret);
    
    return ret;
}