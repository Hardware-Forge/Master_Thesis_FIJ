#ifndef _LINUX_FIJ_REGS_H
#define _LINUX_FIJ_REGS_H

#include <linux/types.h>
#include <linux/ptrace.h>

/* What the caller needs to know about a mapped register */
struct fij_reg_view {
    void *ptr;       /* pointer into pt_regs storage */
    u8    width;     /* in bits: 32 or 64 */
};

/*
 * Map your existing enum fij_reg_id to a concrete register in pt_regs.
 * Returns 0 on success, <0 on error (e.g. -EINVAL when id is unknown on arch).
 */
int fij_arch_map(const struct pt_regs *regs,
                 int fij_reg_id,         /* enum fij_reg_id from your UAPI */
                 struct fij_reg_view *out);

/*
 * Legacy shim for old code that expected "unsigned long *".
 * Prefer fij_arch_map() in new code so you act on the correct width.
 */
static inline unsigned long *
fij_reg_ptr_from_ptregs_legacy(struct pt_regs *regs, int id)
{
    struct fij_reg_view v;
    if (fij_arch_map(regs, id, &v))
        return NULL;

    /*
     * Warning: on 32-bit tasks this narrows to 32 bits; do not use this
     * in new code. Keep only for temporary back-compat.
     */
    return (unsigned long *)v.ptr;
}

#endif /* _LINUX_FIJ_REGS_H */
