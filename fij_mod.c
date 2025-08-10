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
#include <linux/device.h>
#include <linux/string.h>
#include <linux/sched/signal.h>
#include <linux/uprobes.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/kmod.h>
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Runtime-controlled Fault Injection Kernel Module");
MODULE_VERSION("0.2");

#define DEVICE_NAME "fij"
#define CLASS_NAME  "fij_class"
#define MAX_ARGC 6

#define IOCTL_START_FAULT     _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT      _IO('f', 2)
#define IOCTL_GET_STATUS      _IOR('f', 3, int)
#define IOCTL_EXEC_AND_FAULT  _IOW('f', 4, struct fij_params)

struct fij_params {
    char process_name[256];
    char process_path[256];
    char process_args[256];
    int  cycles;                 // 0 = infinite
    unsigned long target_pc;     // offset from start_code
};

/************** Globals **************/
static struct uprobe_consumer inj_uc;
static struct inode *inj_inode;
static struct uprobe *inj_uprobe;   /* handle returned by uprobe_register() */
static loff_t inj_off;
static bool uprobe_active;

static unsigned long target_pc;
static struct task_struct *pc_monitor_thread;

static struct class *fij_class;
static struct device *fij_device;
static int dev_major;

static struct task_struct *bitflip_thread;
static int running;
static int remaining_cycles;
static pid_t target_tgid = -1; // target group id
static unsigned long interval_ms = 1000;
module_param(interval_ms, ulong, 0644);
MODULE_PARM_DESC(interval_ms, "Delay between bitflips (ms)");

// Uprobe disarm vars
static void uprobe_disarm_fn(struct work_struct *work);
static DECLARE_WORK(uprobe_disarm_work, uprobe_disarm_fn);
static atomic_t uprobe_disarm_queued = ATOMIC_INIT(0);

/* Forward declarations */
static int bitflip_thread_fn(void *data);
static int uprobe_hit(struct uprobe_consumer *uc, struct pt_regs *regs, u64 *bp_addr);

/************** Function: Find PID by name **************/
static pid_t find_pid_by_name(const char *name)
{
    struct task_struct *task;
    for_each_process(task) {
        if (strcmp(task->comm, name) == 0)
            return task->pid;
    }
    return -1;
}

/****************** Monitor process ******************************/
static int monitor_thread_fn(void *data)
{
    struct task_struct *leader = (struct task_struct *)data;
    int exit_code = 0;
    bool exited = false;

    /* Wait until the task sets exit_state */
    while (!kthread_should_stop()) {
        if (READ_ONCE(leader->exit_state)) {
            exit_code = READ_ONCE(leader->exit_code);
            exited = true;
            break;
        }
        msleep(50);
    }

    /* Stop periodic injector if it’s still running */
    if (bitflip_thread) {
        kthread_stop(bitflip_thread);
        bitflip_thread = NULL;
    }

    /* Make sure any active uprobe is gone */
    if (uprobe_active && inj_uprobe) {
        uprobe_unregister_nosync(inj_uprobe, &inj_uc);
        uprobe_unregister_sync();
        inj_uprobe = NULL;
        uprobe_active = false;
    }
    if (inj_inode) {
        iput(inj_inode);
        inj_inode = NULL;
    }

    /* Transition to idle now */
    running = 0;

    if (exited) {
        int sig = exit_code & 0x7f;
        bool coredump = !!(exit_code & 0x80);
        int status = (exit_code >> 8) & 0xff;

        if (sig)
            printk(KERN_INFO "fij: TGID %d terminated by signal %d%s\n",
                   target_tgid, sig, coredump ? " (core)" : "");
        else
            printk(KERN_INFO "fij: TGID %d exited with status %d\n",
                   target_tgid, status);
    } else {
        printk(KERN_INFO "fij: monitor thread stopped before target exited\n");
    }

    put_task_struct(leader);  /* drop our ref */
    return 0;
}

/*************** Uprobe disarm function to quit at program exit ****************/
static void uprobe_disarm_fn(struct work_struct *work)
{
    if (uprobe_active && inj_uprobe) {
        uprobe_unregister_nosync(inj_uprobe, &inj_uc);
        uprobe_unregister_sync();   /* safe here; handler already returned */
        inj_uprobe = NULL;
        uprobe_active = false;
    }
    if (inj_inode) {
        iput(inj_inode);
        inj_inode = NULL;
    }
}

/************** Perform Bitflip **************/
static int perform_bitflip(void)
{
    struct task_struct *task;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int bytes_read, bytes_written;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;

    rcu_read_lock();
    /* Use the group leader for the address space; all threads share mm */
    task = pid_task(find_vpid(target_tgid), PIDTYPE_TGID);
    if (!task) {
        rcu_read_unlock();
       printk(KERN_ERR "fij: TGID %d not found\n", target_tgid);
       return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        printk(KERN_ERR "fij: failed to get mm for PID %d\n", target_tgid);
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

    bytes_written = access_process_vm(task, target_addr, &flipped_byte, 1,
                                      FOLL_WRITE | FOLL_FORCE);
    if (bytes_written != 1) {
        ret = -EFAULT;
        goto cleanup;
    }

    printk(KERN_INFO "fij: bit flipped at 0x%lx (TGID %d): 0x%02x -> 0x%02x\n",
          target_addr, target_tgid, orig_byte, flipped_byte);
cleanup:
    if (mm)
        mmput(mm);
    if (task)
        put_task_struct(task);
    return ret;
}

/************************* File offset Helper *****************/
static int va_to_file_off(struct task_struct *t, unsigned long va,
                          struct inode **out_inode, loff_t *out_off)
{
    struct mm_struct *mm = get_task_mm(t);
    struct vm_area_struct *vma;
    int found = 0;

    if (!mm)
        return -ESRCH;

    mmap_read_lock(mm);
    {
        VMA_ITERATOR(vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!vma->vm_file)
                continue;
            if (mm->exe_file && vma->vm_file == mm->exe_file &&
                va >= vma->vm_start && va < vma->vm_end) {
                loff_t file_off = (va - vma->vm_start)
                                  + ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
                *out_inode = igrab(file_inode(vma->vm_file));
                *out_off   = file_off;
                found = 1;
                break;
            }
        }
    }
    mmap_read_unlock(mm);
    mmput(mm);

    return found ? 0 : -ENOENT;
}

/************************ Program Probe for PC monitoring **********************/
static int uprobe_hit(struct uprobe_consumer *uc, struct pt_regs *regs, u64 *bp_addr)
{
    /* Allow any thread of the target process */
    if (current->tgid != target_tgid)
           return 0;  // limit to our child (optional if you use .filter)

    perform_bitflip();

    /* One-shot: unregister immediately */
	if (uprobe_active && inj_uprobe) {
	    if (atomic_xchg(&uprobe_disarm_queued, 1) == 0)
		schedule_work(&uprobe_disarm_work);
	}
    // If user asked for more than one flip, kick off the periodic thread
    if (remaining_cycles != 1) {
        if (!bitflip_thread || IS_ERR(bitflip_thread)) {
            bitflip_thread = kthread_run(bitflip_thread_fn, NULL, "bitflip_thread");
            if (IS_ERR(bitflip_thread)) {
                printk(KERN_ERR "fij: failed to start bitflip thread after uprobe\n");
           
            }
        }
    }
    return 0;
}


/************** Kernel Thread **************/
static int bitflip_thread_fn(void *data)
{
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
static int helper_child_init(struct subprocess_info *info, struct cred *new)
{
    /* This executes in the child process context before exec */
    send_sig(SIGSTOP, current, 0);  // Stop self
    return 0;
}

/************** IOCTL Handler **************/
static long fij_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct fij_params params;

    switch (cmd) {
    case IOCTL_START_FAULT:
        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;

        target_tgid = find_pid_by_name(params.process_name);
	if (target_tgid < 0) {
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

       printk(KERN_INFO "fij: started fault injection on '%s' (TGID %d) for %d cycles\n",
           params.process_name, target_tgid, params.cycles);
       break;

    case IOCTL_STOP_FAULT:
        if (running && bitflip_thread) {
            kthread_stop(bitflip_thread);
            bitflip_thread = NULL;
            running = 0;
            printk(KERN_INFO "fij: fault injection stopped manually\n");
        }
        break;

    case IOCTL_GET_STATUS:
        if (put_user(running, (int __user *)arg))
            return -EFAULT;
        break;

    case IOCTL_EXEC_AND_FAULT: {
        char *argv[MAX_ARGC + 2];
        char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
        char *path_copy = NULL, *args_copy = NULL, *token;
        int argc = 0, ret;
        struct subprocess_info *sub_info;

        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;

        if (running)
            return -EBUSY;

        remaining_cycles = (params.cycles == 0) ? -1 : params.cycles;
        running = 1;

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
            while ((token = strsep(&args_copy, " ")) && argc < MAX_ARGC + 1) {
                if (*token)
                    argv[argc++] = token;
            }
        }
        argv[argc] = NULL;

        sub_info = call_usermodehelper_setup(
            path_copy, argv, envp, GFP_KERNEL,
            helper_child_init, NULL, NULL
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

        target_tgid = find_pid_by_name(params.process_name);
        if (target_tgid < 0) {
	printk(KERN_ERR "fij: launched '%s' not found\n", params.process_name);
            kfree(path_copy);
            running = 0;
            return -ESRCH;
        }
        printk(KERN_INFO "fij: launched '%s' (TGID %d)\n",
               params.process_name, target_tgid);

        if (params.target_pc) {
            struct pid *p_tmp = find_get_pid(target_tgid);
            struct task_struct *t = p_tmp ? pid_task(p_tmp, PIDTYPE_TGID) : NULL;
	    struct mm_struct *m = t ? get_task_mm(t) : NULL;

            if (!t || !m) {
                if (m) mmput(m);
                if (p_tmp) put_pid(p_tmp);
                kfree(path_copy);
                running = 0;
                return -EFAULT;
            }
            target_pc = m->start_code + params.target_pc;
            mmput(m);
            put_pid(p_tmp);
        } else {
            target_pc = 0;
        }

        if (target_pc != 0) {
            struct pid *p = find_get_pid(target_tgid);
            struct task_struct *t = p ? pid_task(p, PIDTYPE_TGID) : NULL;
	    int err = 0;

            if (!t) {
                if (p) put_pid(p);
                kfree(path_copy);
                running = 0;
                return -ESRCH;
            }

            err = va_to_file_off(t, target_pc, &inj_inode, &inj_off);
            put_pid(p);
            if (err) {
                printk(KERN_ERR "fij: could not map VA 0x%lx to file offset (%d)\n", target_pc, err);
                kfree(path_copy);
                running = 0;
                return err;
            }

            inj_uc.handler = uprobe_hit;
            inj_uc.ret_handler = NULL;
            inj_uc.filter = NULL;

            inj_uprobe = uprobe_register(inj_inode, inj_off, 0, &inj_uc);
            if (IS_ERR(inj_uprobe)) {
                err = PTR_ERR(inj_uprobe);
                printk(KERN_ERR "fij: uprobe_register failed (%d)\n", err);
                iput(inj_inode);
                inj_inode = NULL;
                inj_uprobe = NULL;
                kfree(path_copy);
                running = 0;
                return err;
            }
            uprobe_active = true;
        }

        {
            struct pid *p = find_get_pid(target_tgid);
            if (p) {
                /* SIGCONT is job-control and resumes the whole thread group even if sent to one thread */
                send_sig(SIGCONT, pid_task(p, PIDTYPE_TGID), 0);
                put_pid(p);
                printk(KERN_INFO "fij: SIGCONT → TGID %d\n", target_tgid);
            }
        }
	{
	    struct task_struct *leader;

	    rcu_read_lock();
	    leader = pid_task(find_vpid(target_tgid), PIDTYPE_TGID);
	    if (leader)
		get_task_struct(leader);   /* keep it alive for monitoring */
	    rcu_read_unlock();

	    if (!leader) {
		printk(KERN_ERR "fij: cannot get leader for TGID %d\n", target_tgid);
		running = 0;
		return -ESRCH;
	    }

	    pc_monitor_thread = kthread_run(monitor_thread_fn, leader, "fij_monitor");
	    if (IS_ERR(pc_monitor_thread)) {
		put_task_struct(leader);
		printk(KERN_ERR "fij: failed to start monitor thread\n");
		running = 0;
		return PTR_ERR(pc_monitor_thread);
	    }
	}
	
        if (target_pc == 0) {
            int ret2 = perform_bitflip();
            if (ret2)
                printk(KERN_ERR "fij: immediate bitflip failed (%d)\n", ret2);

            if (remaining_cycles != 1) {
                bitflip_thread = kthread_run(bitflip_thread_fn, NULL, "bitflip_thread");
                if (IS_ERR(bitflip_thread)) {
                    printk(KERN_ERR "fij: failed to start bitflip thread\n");
                    
                }
            }        }

        kfree(path_copy);
        break;
    }

    default:
        return -EINVAL;
    }

    return 0;
}

/************** File Operations **************/
static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = fij_ioctl,
};

/************** Init / Exit **************/
static int __init fij_init(void)
{
    dev_major = register_chrdev(0, DEVICE_NAME, &fops);
    if (dev_major < 0) {
        printk(KERN_ALERT "fij: failed to register char device\n");
        return dev_major;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
    fij_class = class_create(CLASS_NAME);
#else
    fij_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
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

    inj_inode = NULL;
    inj_uprobe = NULL;
    uprobe_active = false;
    bitflip_thread = NULL;
    pc_monitor_thread = NULL;
    running = 0;
    remaining_cycles = 0;

    printk(KERN_INFO "fij: module loaded. Use /dev/%s to control it.\n", DEVICE_NAME);
    return 0;
}

static void __exit fij_exit(void)
{
	flush_work(&uprobe_disarm_work);
	
    if (bitflip_thread) {
        kthread_stop(bitflip_thread);
        bitflip_thread = NULL;
    }
    if (pc_monitor_thread) {
        kthread_stop(pc_monitor_thread);
        pc_monitor_thread = NULL;
    }

    if (uprobe_active && inj_uprobe) {
        /* remove our consumer and wait for in-flight handlers to finish */
        uprobe_unregister_nosync(inj_uprobe, &inj_uc);
        uprobe_unregister_sync();
        inj_uprobe = NULL;
        uprobe_active = false;
    }

    if (inj_inode) {
        iput(inj_inode);
        inj_inode = NULL;
    }

    device_destroy(fij_class, MKDEV(dev_major, 0));
    if (fij_class) {
        class_unregister(fij_class);
        class_destroy(fij_class);
        fij_class = NULL;
    }

    unregister_chrdev(dev_major, DEVICE_NAME);
    pr_info("fij: module unloaded\n");
}


module_init(fij_init);
module_exit(fij_exit);

