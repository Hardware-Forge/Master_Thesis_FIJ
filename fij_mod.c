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
#include <linux/sched/signal.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Runtime-controlled Fault Injection Kernel Module");
MODULE_VERSION("0.2");

#define DEVICE_NAME "fij"
#define CLASS_NAME  "fij_class"
#define MAX_ARGC 6

#define IOCTL_START_FAULT  _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT   _IO('f', 2)
#define IOCTL_GET_STATUS   _IOR('f', 3, int)
#define IOCTL_EXEC_AND_FAULT _IOW('f', 4, struct fij_params)

struct fij_params {
    char process_name[256];
    char process_path[256];      // e.g., "/usr/bin/myapp"
    char process_args[256];      // e.g., "arg1 arg2"
    int cycles;                  // Number of bitflips to inject (0 = infinite)
    unsigned long target_pc;     // Target instruction pointer to wait for
};

/************** Globals **************/
static unsigned long target_pc = 0;
static struct task_struct *pc_monitor_thread = NULL;

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

/****************************** Program Counter Check ******************************/
static int pc_monitor_thread_fn(void *data) {
    struct task_struct *task;
    struct pt_regs *regs;

    while (!kthread_should_stop()) {
        rcu_read_lock();
       struct pid *pid_struct = find_get_pid(target_pid);
	if (!pid_struct) {
	    rcu_read_unlock();
	    printk(KERN_ERR "fij: failed to get pid_struct for %d\n", target_pid);
	    break;
	}
	task = pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);

	if (!task) {
	    rcu_read_unlock();
	    printk(KERN_ERR "fij: task %d not found (pid_task returned NULL)\n", target_pid);
	    break;
	}

	if (!pid_alive(task) || (task->flags & PF_EXITING)) {
	    rcu_read_unlock();
	    printk(KERN_ERR "fij: task %d has exited or is exiting\n", target_pid);
	    break;
	}        if (!task) {
            rcu_read_unlock();
            printk(KERN_ERR "fij: task %d not found in pc_monitor_thread\n", target_pid);
            break;
        }

       if (!pid_alive(task) || (task->flags & PF_EXITING)) {
	    rcu_read_unlock();
	    printk(KERN_ERR "fij: task %d has exited or is dying\n", target_pid);
	    break;
	}

        regs = task_pt_regs(task);
        if (regs && regs->ip == target_pc) {
            rcu_read_unlock();
            printk(KERN_INFO "fij: target PC 0x%lx reached by PID %d\n", target_pc, target_pid);
            perform_bitflip();

            struct pid *pid_struct = find_get_pid(target_pid);
            if (pid_struct) {
                send_sig(SIGCONT, pid_task(pid_struct, PIDTYPE_PID), 0);
                put_pid(pid_struct);
                printk(KERN_INFO "fij: sent SIGCONT to PID %d\n", target_pid);
            }
            break;
        }

        rcu_read_unlock();
        msleep(10); // Tune as needed
    }

    running = 0;
    return 0;
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

    /************** Inject SIGSTOP Before Exec **************/
    static int helper_child_init(struct subprocess_info *info, struct cred *new) {
        // This executes in the child process context before exec
        send_sig(SIGSTOP, current, 0);  // Stop self
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
case IOCTL_EXEC_AND_FAULT: {
    struct fij_params params;
    char *argv[MAX_ARGC + 2];
    char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
    char *path_copy = NULL, *args_copy = NULL, *token;
    int argc = 0, ret;
    struct subprocess_info *sub_info;

    /* 1) grab user params */
    if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
        return -EFAULT;

    /* not allowed to re-enter */
    if (running)
        return -EBUSY;

    /* prepare globals */
    remaining_cycles = (params.cycles == 0) ? -1 : params.cycles;
    running = 1;

    /* 2) build argv[] */
    path_copy = kstrdup(params.process_path, GFP_KERNEL);
    if (!path_copy) {
        running = 0;
        return -ENOMEM;
    }
    argv[argc++] = path_copy;

    if (params.process_args[0]) {
        args_copy = kstrdup(params.process_args, GFP_KERNEL);
        if (!args_copy) {
            kfree(path_copy);
            running = 0;
            return -ENOMEM;
        }
        while ((token = strsep(&args_copy, " ")) && argc < MAX_ARGC+1) {
            if (*token)
                argv[argc++] = token;
        }
    }
    argv[argc] = NULL;

    /* 3) fork+exec, child will SIGSTOP in helper_child_init */
    sub_info = call_usermodehelper_setup(
        path_copy, argv, envp, GFP_KERNEL,
        helper_child_init, /* no prep_fn */ NULL, /* no data */ NULL
    );
    if (!sub_info) {
        kfree(path_copy);
        running = 0;
        return -ENOMEM;
    }
    ret = call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);
    if (ret) {
        printk(KERN_ERR "fij: exec failed (%d)\n", ret);
        kfree(path_copy);
        running = 0;
        return ret;
    }

    /* 4) child is now SIGSTOPPED — find its PID */
    target_pid = find_pid_by_name(params.process_name);
    if (target_pid < 0) {
        printk(KERN_ERR "fij: launched '%s' not found\n", params.process_name);
        kfree(path_copy);
        running = 0;
        return -ESRCH;
    }
    printk(KERN_INFO "fij: launched '%s' (PID %d)\n",
           params.process_name, target_pid);

    /* 5) compute absolute injection address */
    if (params.target_pc) {
        struct pid *p_tmp = find_get_pid(target_pid);
        if (!p_tmp) {
            kfree(path_copy);
            running = 0;
            return -ESRCH;
        }
        {
            struct task_struct *t = pid_task(p_tmp, PIDTYPE_PID);
            struct mm_struct   *m = t ? get_task_mm(t) : NULL;
            if (!t || !m) {
                put_pid(p_tmp);
                kfree(path_copy);
                running = 0;
                return -EFAULT;
            }
            target_pc = m->start_code + params.target_pc;
            mmput(m);
        }
        put_pid(p_tmp);
    } else {
        /* inject before first exec instruction */
        target_pc = 0;
    }

    /* 6) resume the child now that target_pc is set */
    {
        struct pid *p = find_get_pid(target_pid);
        if (p) {
            send_sig(SIGCONT, pid_task(p, PIDTYPE_PID), 0);
            put_pid(p);
            printk(KERN_INFO "fij: SIGCONT → PID %d\n", target_pid);
        }
    }

    /* 7) do the injection: immediate if target_pc==0, else spin in thread */
    if (target_pc == 0) {
        ret = perform_bitflip();
        /* maybe start periodic if cycles >1 (or infinite) */
        if (remaining_cycles != 1) {
            bitflip_thread = kthread_run(bitflip_thread_fn, NULL, "bitflip_thread");
            if (IS_ERR(bitflip_thread)) {
                printk(KERN_ERR "fij: failed to start bitflip thread\n");
                running = 0;
                ret = PTR_ERR(bitflip_thread);
            }
        } else {
            running = 0;
        }
    } else {
        pc_monitor_thread = kthread_run(pc_monitor_thread_fn, NULL, "pc_monitor_thread");
        if (IS_ERR(pc_monitor_thread)) {
            printk(KERN_ERR "fij: failed to start PC monitor thread\n");
            running = 0;
            ret = PTR_ERR(pc_monitor_thread);
        }
    }

    kfree(path_copy);
    return ret;
}
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
    /* Signal threads to stop */
    if (bitflip_thread) {
        kthread_stop(bitflip_thread);
        bitflip_thread = NULL;
    }
    if (pc_monitor_thread) {
        kthread_stop(pc_monitor_thread);
        pc_monitor_thread = NULL;
    }

    /* Clean up device */
    device_destroy(fij_class, MKDEV(dev_major, 0));
    if (fij_class) {
        class_unregister(fij_class);
        class_destroy(fij_class);
        fij_class = NULL;
    }

    unregister_chrdev(dev_major, DEVICE_NAME);

    printk(KERN_INFO "fij: module unloaded\n");
}

module_init(fij_init);
module_exit(fij_exit);
