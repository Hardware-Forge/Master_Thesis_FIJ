#include "fij_internal.h"
#include <linux/module.h>
#include <linux/version.h>

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Runtime-controlled Fault Injection Kernel Module");
MODULE_VERSION("0.2");

/* Module params */
static unsigned long interval_ms = 1;
module_param(interval_ms, ulong, 0644);
MODULE_PARM_DESC(interval_ms, "Delay between bitflips (ms)");

static struct fij_ctx g_ctx;

static void fij_ctx_init(struct fij_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->interval_ms = interval_ms;

    INIT_WORK(&ctx->uprobe_disarm_work, NULL); /* real fn set below */
    /* We re-init with the proper function pointer here: */
    INIT_WORK(&ctx->uprobe_disarm_work, (work_func_t)NULL); /* avoid uninit warning */
    /* Proper init is done explicitly to the defined workfn in uprobe.c: */
    extern void fij_uprobe_init_work(struct fij_ctx *ctx);
    fij_uprobe_init_work(ctx);

    atomic_set(&ctx->uprobe_disarm_queued, 0);
}

static int __init fij_init(void)
{
    int err;

    fij_ctx_init(&g_ctx);

    err = fij_chardev_register(&g_ctx);
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

    pr_info("fij: monitor_stop() begin\n");
    fij_monitor_stop(&g_ctx);
    pr_info("fij: monitor_stop() done\n");

    pr_info("fij: stop_bitflip_thread() begin\n");
    fij_stop_bitflip_thread(&g_ctx);
    pr_info("fij: stop_bitflip_thread() done\n");

    pr_info("fij: uprobe_disarm_sync() begin\n");
    fij_uprobe_disarm_sync(&g_ctx);
    pr_info("fij: uprobe_disarm_sync() done\n");

    pr_info("fij: chardev_unregister() begin\n");
    fij_chardev_unregister();
    pr_info("fij: chardev_unregister() done\n");

    pr_info("fij: EXIT end\n");
}

module_init(fij_init);
module_exit(fij_exit);
