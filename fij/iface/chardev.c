#include "fij_internal.h"
#include <linux/module.h>
#include <linux/miscdevice.h>

static struct fij_ctx *g_ctx;  /* set at register time */

static int fij_open(struct inode *inode, struct file *file)
{
    file->private_data = g_ctx;
    return 0;
}

/* ioctl is implemented in iface/ioctl.c */
extern long fij_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations fij_fops = {
    .owner          = THIS_MODULE,
    .open           = fij_open,
    .unlocked_ioctl = fij_unlocked_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = fij_unlocked_ioctl,
#endif
};

int fij_chardev_register(struct fij_ctx *ctx)
{
    g_ctx = ctx;

    ctx->miscdev.minor = MISC_DYNAMIC_MINOR;
    ctx->miscdev.name  = FIJ_DEVICE_NAME;
    ctx->miscdev.fops  = &fij_fops;
    ctx->miscdev.mode  = 0666;

    return misc_register(&ctx->miscdev);
}

void fij_chardev_unregister(void)
{
    if (g_ctx) {
        misc_deregister(&g_ctx->miscdev);
        g_ctx = NULL;
    }
}
