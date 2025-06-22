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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Carbonetti");
MODULE_DESCRIPTION("Fault Injection Module to test program resiliency");
MODULE_VERSION("0.1");

/**************** PARAMETERS ****************/
static int pid = 1;
module_param(pid, int, 0644);
MODULE_PARM_DESC(pid, "Target process PID");

static unsigned long interval_ms = 1000; // Default: 1 second
module_param(interval_ms, ulong, 0644);
MODULE_PARM_DESC(interval_ms, "Delay between bit flips in milliseconds");

/**************** GLOBALS ****************/
static struct task_struct *bitflip_thread;

/**************** FUNCTION: PERFORM BITFLIP ****************/
static int perform_bitflip(void) {
    struct task_struct *task = NULL;
    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    unsigned long vma_size, offset, target_addr;
    unsigned char orig_byte, flipped_byte;
    int bytes_read, bytes_written;
    int count = 0, target_idx, bit_to_flip;
    int ret = 0;

    // Find the task by PID
    rcu_read_lock();
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        printk(KERN_ERR "Process with PID %d not found\n", pid);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    // Get memory descriptor
    mm = get_task_mm(task);
    if (!mm) {
        printk(KERN_ERR "Failed to get mm_struct for PID %d\n", pid);
        ret = -EINVAL;
        goto cleanup;
    }

    // First pass: count VMAs
    mmap_read_lock(mm);
    {
        VMA_ITERATOR(vmi, mm, 0);
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!(vma->vm_flags & VM_IO) && !(vma->vm_flags & VM_PFNMAP)) {
                count++;
            }
        }

        if (count == 0) {
            mmap_read_unlock(mm);
            printk(KERN_ERR "No suitable VMAs found for PID %d\n", pid);
            ret = -ENOENT;
            goto cleanup;
        }

        // Choose random VMA index
        target_idx = get_random_u32() % count;

        // Second pass: find target VMA
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
            printk(KERN_ERR "Failed to find target VMA\n");
            ret = -ENOENT;
            goto cleanup;
        }

        // Compute target address
        vma_size = vma->vm_end - vma->vm_start;
        offset = get_random_u32() % vma_size;
        target_addr = vma->vm_start + offset;
    }
    mmap_read_unlock(mm);

    // Read original byte
    bytes_read = access_process_vm(task, target_addr, &orig_byte, 1, 0);
    if (bytes_read != 1) {
        printk(KERN_ERR "Failed to read memory at 0x%lx for PID %d\n", target_addr, pid);
        ret = -EFAULT;
        goto cleanup;
    }

    // Flip a random bit
    bit_to_flip = get_random_u32() % 8;
    flipped_byte = orig_byte ^ (1 << bit_to_flip);

    // Write flipped byte
    bytes_written = access_process_vm(task, target_addr, &flipped_byte, 1, FOLL_WRITE | FOLL_FORCE);
    if (bytes_written != 1) {
        printk(KERN_ERR "Failed to write memory at 0x%lx for PID %d\n", target_addr, pid);
        ret = -EFAULT;
        goto cleanup;
    }

    printk(KERN_INFO "Bitflip injected at address 0x%lx (PID %d): 0x%02x --> 0x%02x (bit %d flipped)\n", 
           target_addr, pid, orig_byte, flipped_byte, bit_to_flip);

cleanup:
    if (mm)
        mmput(mm);
    if (task)
        put_task_struct(task);
    return ret;
}

/**************** KERNEL THREAD FUNCTION ****************/
static int bitflip_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        perform_bitflip();
        msleep(interval_ms);
    }
    return 0;
}

/**************** INIT & EXIT ****************/
static int __init bitflip_init(void) {
    if(pid == 1) {
	printk(KERN_ERR "refusing to inject faults into PID 1");
	return -EINVAL;
    }
    bitflip_thread = kthread_run(bitflip_thread_fn, NULL, "bitflip_thread");
    if (IS_ERR(bitflip_thread)) {
        printk(KERN_ERR "Failed to create bitflip thread\n");
        return PTR_ERR(bitflip_thread);
    }

    printk(KERN_INFO "Bitflip module loaded. Target PID: %d, Interval: %lums\n", pid, interval_ms);
    return 0;
}

static void __exit bitflip_exit(void) {
    if (bitflip_thread) {
        kthread_stop(bitflip_thread);
        printk(KERN_INFO "Bitflip thread stopped\n");
    }
    printk(KERN_INFO "Bitflip module unloaded\n");
}

module_init(bitflip_init);
module_exit(bitflip_exit);

