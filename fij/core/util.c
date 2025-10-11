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

struct task_struct *fij_rcu_find_get_task_by_tgid(pid_t tgid)
{
    struct task_struct *tsk;

    rcu_read_lock();
    tsk = pid_task(find_vpid(tgid), PIDTYPE_TGID);  /* no ref yet */
    if (tsk)
        get_task_struct(tsk);                       /* take ref */
    rcu_read_unlock();

    return tsk; /* put_task_struct() when done */
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

struct task_struct *fij_pick_random_user_thread(int tgid)
{
    struct task_struct *g, *t, *chosen = NULL;
    unsigned int n = 0, pick, idx = 0;

    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (!g) { rcu_read_unlock(); return NULL; }
    get_task_struct(g);

    /* Count eligible threads (user threads only) */
    for_each_thread(g, t) {
        if (t->mm && !(t->flags & PF_KTHREAD))
            n++;
    }
    if (!n) { rcu_read_unlock(); put_task_struct(g); return NULL; }

    pick = get_random_u32() % n;

    /* Walk again to grab the pick-th eligible thread */
    for_each_thread(g, t) {
        if (t->mm && !(t->flags & PF_KTHREAD)) {
            if (idx++ == pick) {
                get_task_struct(t);
                chosen = t;
                pr_info("thread %d chosen\n", pick);
                break;
            }
        }
    }
    rcu_read_unlock();
    put_task_struct(g);
    return chosen; /* ref held if non-NULL */
}

struct task_struct *fij_pick_user_thread_by_index(int tgid, int n1)
{
    struct task_struct *g, *t, *chosen = NULL;
    unsigned int cnt = 0, idx = 0, target;

    if (n1 <= 0)
        return fij_pick_random_user_thread(tgid);

    rcu_read_lock();
    g = fij_rcu_find_get_task_by_tgid(tgid);
    if (!g) { rcu_read_unlock(); return NULL; }
    get_task_struct(g); /* hold a ref to the group leader */

    /* Count eligible threads (user threads only) */
    for_each_thread(g, t) {
        if (t->mm && !(t->flags & PF_KTHREAD))
            cnt++;
    }
    if (!cnt || (unsigned int)(n1 - 1) >= cnt) {
        rcu_read_unlock();
        put_task_struct(g);
        return NULL;
    }

    /* Convert to 0-index target and walk again to grab it */
    target = (unsigned int)(n1 - 1);

    for_each_thread(g, t) {
        if (t->mm && !(t->flags & PF_KTHREAD)) {
            if (idx++ == target) {
                get_task_struct(t); /* hold a ref for the caller */
                chosen = t;
                break;
            }
        }
    }
    pr_info("thread %d chosen\n", target+1);
    rcu_read_unlock();
    put_task_struct(g);
    return chosen; /* ref held if non-NULL */
}

/* Pick a random register id in [FIJ_REG_RAX .. FIJ_REG_MAX-1] */
enum fij_reg_id fij_pick_random_reg_any(void)
{
    int range = (FIJ_REG_MAX - 1);   /* number of selectable regs */
    return (enum fij_reg_id)(1 + (get_random_u32() % range));
}

/* Pick a random bit for a 64-bit register */
int fij_pick_random_bit64(void)
{
    return get_random_u32() & 63;    /* 0..63 */
}

bool choose_register_target(int weight_mem, int only_mem)
{
    if(only_mem) {
        return 0;
    }
    pr_info("only mem %d, weight mem %d\n", only_mem, weight_mem);
    const u32 weight_regs = 1;

    /* sanitize signed input: negatives behave like 0 */
    u32 wm = (u32)weight_mem;

    /* total = 1 + wm, avoiding wrap to 0 if wm == U32_MAX */
    u32 total = (wm == U32_MAX) ? U32_MAX : (wm + weight_regs);

    if (total == 1)
        return true;  // only regs

    /* Unbiased draw in [0, total) via rejection sampling on u32 */
    {
        u32 r;
        u32 limit = U32_MAX - (U32_MAX % total);
        do {
            r = get_random_u32();
        } while (r >= limit);
        return (r % total) < weight_regs;   // i.e., r % total == 0
    }
}
