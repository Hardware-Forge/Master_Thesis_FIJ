#include "fij_internal.h"
#include <linux/module.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Runtime-controlled Fault Injection Kernel Module");
MODULE_VERSION("0.2");

void fij_ctx_init(struct fij_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    INIT_WORK(&ctx->uprobe_disarm_work, NULL);
    INIT_WORK(&ctx->uprobe_disarm_work, (work_func_t)NULL); /* avoid uninit warning */

    extern void fij_uprobe_init_work(struct fij_ctx *ctx);
    fij_uprobe_init_work(ctx);

    atomic_set(&ctx->uprobe_disarm_queued, 0);
}

static int __init fij_init(void)
{
    int err;

    err = fij_chardev_register();
    if (err) {
        pr_err("failed to register misc device: %d\n", err);
        return err;
    }

    pr_info("module loaded. Use /dev/%s to control it.\n", FIJ_DEVICE_NAME);
    return 0;
}

static void __exit fij_exit(void)
{
    pr_info("fij: EXIT begin\n");

    pr_info("fij: chardev_unregister() begin\n");
    fij_chardev_unregister();
    pr_info("fij: chardev_unregister() done\n");

    pr_info("fij: EXIT end\n");
}

module_init(fij_init);
module_exit(fij_exit);
