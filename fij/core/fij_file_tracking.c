#include "fij_internal.h"
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>

#define MAX_OUTPUT_FILES 32
#define MAX_PATH_LEN 256

/* Capture all open file descriptors > 2 from the target process */
void fij_capture_output_files(struct fij_ctx *ctx)
{
    struct task_struct *task;
    struct files_struct *files;
    struct fdtable *fdt;
    struct file *file;
    unsigned int fd;
    int count = 0;
    char *buf, *path_str;
    
    /* Get the task struct for the target process */
    rcu_read_lock();
    task = pid_task(find_vpid(ctx->target_tgid), PIDTYPE_TGID);
    if (!task) {
        rcu_read_unlock();
        pr_warn("fij: target TGID %d not found for file capture\n", ctx->target_tgid);
        return;
    }
    
    /* Get the files structure */
    files = task->files;
    if (!files) {
        rcu_read_unlock();
        return;
    }
    
    /* Allocate buffer for path resolution */
    buf = kmalloc(MAX_PATH_LEN, GFP_ATOMIC);
    if (!buf) {
        rcu_read_unlock();
        pr_warn("fij: failed to allocate buffer for file paths\n");
        return;
    }
    
    /* Lock the file descriptor table */
    spin_lock(&files->file_lock);
    fdt = files_fdtable(files);
    
    /* Iterate through all file descriptors */
    for (fd = 3; fd < fdt->max_fds && count < MAX_OUTPUT_FILES; fd++) {
        file = rcu_dereference_check(fdt->fd[fd], 
                                     lockdep_is_held(&files->file_lock));
        
        if (!file)
            continue;
        
        /* Check if this is a regular file (not socket, pipe, etc.) */
        if (!S_ISREG(file_inode(file)->i_mode))
            continue;
        
        /* Check if file was opened for writing */
        if (!(file->f_mode & FMODE_WRITE))
            continue;
        
        /* Get the full path */
        path_str = d_path(&file->f_path, buf, MAX_PATH_LEN);
        if (IS_ERR(path_str))
            continue;
        
        /* Copy to result structure */
        strncpy(ctx->exec.result.output_files[count], 
                path_str, 
                sizeof(ctx->exec.result.output_files[count]) - 1);
        ctx->exec.result.output_files[count][sizeof(ctx->exec.result.output_files[count]) - 1] = '\0';
        
        pr_debug("fij: captured output file fd=%u: %s\n", fd, path_str);
        count++;
    }
    
    spin_unlock(&files->file_lock);
    rcu_read_unlock();
    
    ctx->exec.result.num_output_files = count;
    kfree(buf);
    
    pr_info("fij: captured %d output file(s) for TGID %d\n", count, ctx->target_tgid);
}