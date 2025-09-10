#include "fij_internal.h"
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>

pid_t fij_find_pid_by_name(const char *name)
{
    struct task_struct *task;

    for_each_process(task) {
        if (strcmp(task->comm, name) == 0)
            return task->pid;  /* TGID of leader or any matching? Ok for your use. */
    }
    return -1;
}

int fij_va_to_file_off(struct task_struct *t, unsigned long va,
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

int fij_send_cont(pid_t tgid)
{
    struct pid *p = find_get_pid(tgid);
    if (!p)
        return -ESRCH;

    /* SIGCONT resumes whole thread group even if sent to one thread */
    send_sig(SIGCONT, pid_task(p, PIDTYPE_TGID), 0);
    put_pid(p);
    pr_info("SIGCONT → TGID %d\n", tgid);
    return 0;
}
