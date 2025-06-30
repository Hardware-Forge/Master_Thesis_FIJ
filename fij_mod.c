#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/random.h>
#include <linux/moduleparam.h>
#include <linux/pid.h>
#include <linux/version.h>
#include <linux/mmap_lock.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Runtime-controlled Fault Injection Kernel Module");
MODULE_VERSION("0.2");

#define DEVICE_NAME "fij"
#define CLASS_NAME  "fij_class"

#define IOCTL_START_FAULT  _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT   _IO('f', 2)
#define IOCTL_GET_STATUS   _IOR('f', 3, int)

struct fij_params {
    char process_name[256];
    int cycles;
};

/************** Globals **************/
static struct class*  fij_class  = NULL;
static struct device* fij_device = NULL;
static struct cdev    fij_cdev;
static int    dev_major;

static struct task_struct *bitflip_thread = NULL;
static int running = 0;
static int remaining_cycles = 0;
static pid_t target_pid = -1;
static unsigned long interval_ms = 1000;
module_param(interval_ms, ulong, 0644);
MODULE_PARM_DESC(interval_ms, "Delay between bitflips (ms)");

/************** Function: Find PID by name **************/
static pid_t find_pid_by_name(const char *name) {
    struct task_struct *task;
    for_each_process(task) {
        if (strcmp(task->comm, name) == 0)
            return task->pid;
    }
    return -1;
}

/************** Function: Perform Bitflip **************/
static int perform_bitflip(void) {
    struct task_struct *task;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int bytes_read, bytes_written;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;

    rcu_read_lock();
    task = pid_task(find_vpid(target_pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        printk(KERN_ERR "fij: PID %d not found\n", target_pid);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        printk(KERN_ERR "fij: failed to get mm for PID %d\n", target_pid);
        ret = -EINVAL;
        goto cleanup;
    }

    mmap_read_lock(mm);
    {
        VMA_ITERATOR(vmi, mm, 0);
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!(vma->vm_flags & VM_IO) && !(vma->vm_flags & VM_PFNMAP))
                count++;
        }

        if (count == 0) {
            mmap_read_unlock(mm);
            ret = -ENOENT;
            goto cleanup;
        }

        target_idx = get_random_u32() % count;
        count = 0;
        vma = NULL;
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!(vma->vm_flags & VM_IO) && !(vma->vm_flags & VM_PFNMAP)) {
                if (count == target_idx)
                    break;
                count++;
            }
        }

        if (!vma) {
            mmap_read_unlock(mm);
            ret = -ENOENT;
            goto cleanup;
        }

        vma_size = vma->vm_end - vma->vm_start;
        offset = get_random_u32() % vma_size;
        target_addr = vma->vm_start + offset;
    }
    mmap_read_unlock(mm);

    bytes_read = access_process_vm(task, target_addr, &orig_byte, 1, 0);
    if (bytes_read != 1) {
        ret = -EFAULT;
        goto cleanup;
    }

    bit_to_flip = get_random_u32() % 8;
    flipped_byte = orig_byte ^ (1 << bit_to_flip);

    bytes_written = access_process_vm(task, target_addr, &flipped_byte, 1, FOLL_WRITE | FOLL_FORCE);
    if (bytes_written != 1) {
        ret = -EFAULT;
        goto cleanup;
    }

    printk(KERN_INFO "fij: bit flipped at 0x%lx (PID %d): 0x%02x -> 0x%02x\n",
           target_addr, target_pid, orig_byte, flipped_byte);

cleanup:
    if (mm)
        mmput(mm);
    if (task)
        put_task_struct(task);
    return ret;
}

/************** Kernel Thread **************/
static int bitflip_thread_fn(void *data) {
    while (!kthread_should_stop() && (remaining_cycles > 0 || remaining_cycles == -1)) {
        perform_bitflip();
        if (remaining_cycles > 0)
            remaining_cycles--;
        msleep(interval_ms);
    }

    running = 0;
    return 0;
}
/************** IOCTL Handler **************/
static long fij_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct fij_params params;

    switch (cmd) {
    case IOCTL_START_FAULT:
        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;

        target_pid = find_pid_by_name(params.process_name);
        if (target_pid < 0) {
            printk(KERN_ERR "fij: process '%s' not found\n", params.process_name);
            return -ESRCH;
        }

        if (running) {
            printk(KERN_INFO "fij: already running\n");
            return -EBUSY;
        }

	remaining_cycles = (params.cycles == 0) ? -1 : params.cycles;
        running = 1;

        bitflip_thread = kthread_run(bitflip_thread_fn, NULL, "bitflip_thread");
        if (IS_ERR(bitflip_thread)) {
            printk(KERN_ERR "fij: failed to start thread\n");
            running = 0;
            return PTR_ERR(bitflip_thread);
        }

        printk(KERN_INFO "fij: started fault injection on '%s' (PID %d) for %d cycles\n",
               params.process_name, target_pid, params.cycles);
        break;

    case IOCTL_STOP_FAULT:
        if (running && bitflip_thread) {
            kthread_stop(bitflip_thread);
            running = 0;
            printk(KERN_INFO "fij: fault injection stopped manually\n");
        }
        break;

    case IOCTL_GET_STATUS:
        if (put_user(running, (int __user *)arg))
            return -EFAULT;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

/************** File Operations **************/
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = fij_ioctl,
};

/************** Init / Exit **************/
static int __init fij_init(void) {
    int ret;

    dev_major = register_chrdev(0, DEVICE_NAME, &fops);
    if (dev_major < 0) {
        printk(KERN_ALERT "fij: failed to register char device\n");
        return dev_major;
    }

    fij_class = class_create(CLASS_NAME);
    if (IS_ERR(fij_class)) {
        unregister_chrdev(dev_major, DEVICE_NAME);
        return PTR_ERR(fij_class);
    }

    fij_device = device_create(fij_class, NULL, MKDEV(dev_major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(fij_device)) {
        class_destroy(fij_class);
        unregister_chrdev(dev_major, DEVICE_NAME);
        return PTR_ERR(fij_device);
    }

    printk(KERN_INFO "fij: module loaded. Use /dev/%s to control it.\n", DEVICE_NAME);
    return 0;
}

static void __exit fij_exit(void) {
    if (running && bitflip_thread) {
        kthread_stop(bitflip_thread);
    }

    device_destroy(fij_class, MKDEV(dev_major, 0));
    class_unregister(fij_class);
    class_destroy(fij_class);
    unregister_chrdev(dev_major, DEVICE_NAME);

    printk(KERN_INFO "fij: module unloaded\n");
}

module_init(fij_init);
module_exit(fij_exit);

