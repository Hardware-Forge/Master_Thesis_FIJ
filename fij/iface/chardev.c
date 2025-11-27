#include "fij_internal.h"
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static int fij_open(struct inode *inode, struct file *file)
{
    struct fij_ctx *ctx;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    fij_ctx_init(ctx);  /* the helper from main.c */

    file->private_data = ctx;
    return 0;
}

static int fij_release(struct inode *inode, struct file *file)
{
    struct fij_ctx *ctx = file->private_data;

    if (!ctx) return 0;

    /* 1. Stop threads */
    fij_monitor_stop(ctx);
    fij_stop_bitflip_thread(ctx);

    /* 2. CRITICAL: Cancel Workqueues */
    /* If you omit this, a delayed injection work item will run 
       after kfree(ctx), corrupting memory. */
    cancel_work_sync(&ctx->uprobe_disarm_work);
    cancel_work_sync(&ctx->inject_work);

    /* 3. Cleanup Resources */
    fij_uprobe_disarm_sync(ctx);
    kfree(ctx->targets);
    kfree(ctx); // Free the context
    
    file->private_data = NULL;
    return 0;
}

/* ioctl is implemented in iface/ioctl.c */
extern long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations fij_fops = {
    .owner          = THIS_MODULE,
    .open           = fij_open,
    .release        = fij_release,
    .unlocked_ioctl = fij_unlocked_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = fij_unlocked_ioctl,
#endif
};

static struct miscdevice fij_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = FIJ_DEVICE_NAME,
    .fops  = &fij_fops,
    .mode  = 0666,
};


int fij_chardev_register(void)
{
    return misc_register(&fij_miscdev);
}

void fij_chardev_unregister(void)
{
    misc_deregister(&fij_miscdev);
}