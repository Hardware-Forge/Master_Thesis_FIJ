#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include "fij.h"
#include "fij_regs.h"

/* ---------- x86 (both 64-bit and compat) ---------- */
#ifdef CONFIG_X86
#include <asm/ptrace.h>

static int fij_arch_map_x86(const struct pt_regs *regs, int id,
                            struct fij_reg_view *out)
{
    switch (id) {
    case FIJ_REG_RAX: out->ptr = (void *)&((struct pt_regs *)regs)->ax; return 0;
    case FIJ_REG_RBX: out->ptr = (void *)&((struct pt_regs *)regs)->bx; return 0;
    case FIJ_REG_RCX: out->ptr = (void *)&((struct pt_regs *)regs)->cx; return 0;
    case FIJ_REG_RDX: out->ptr = (void *)&((struct pt_regs *)regs)->dx; return 0;
    case FIJ_REG_RSI: out->ptr = (void *)&((struct pt_regs *)regs)->si; return 0;
    case FIJ_REG_RDI: out->ptr = (void *)&((struct pt_regs *)regs)->di; return 0;
    case FIJ_REG_RBP: out->ptr = (void *)&((struct pt_regs *)regs)->bp; return 0;
    case FIJ_REG_RSP: out->ptr = (void *)&((struct pt_regs *)regs)->sp; return 0;
    case FIJ_REG_RIP:
        out->ptr  = (void *)&((struct pt_regs *)regs)->ip;
        return 0;
    default:
        return -EINVAL;
    }
}
#endif /* CONFIG_X86 */


/* ---------- arm64 (AArch64) ---------- */
#ifdef CONFIG_ARM64
#include <asm/ptrace.h>

static int fij_arch_map_arm64(const struct pt_regs *regs, int id,
                              struct fij_reg_view *out)
{
    out->width = 64;
    switch (id) {
    /* Map a reasonable subset for now; extend as you evolve your UAPI */
    case FIJ_REG_RAX: /* treat as x0 for cross-arch convenience */
        out->ptr = (void *)&((struct pt_regs *)regs)->regs[0]; return 0;
    case FIJ_REG_RBX: out->ptr = (void *)&((struct pt_regs *)regs)->regs[1]; return 0;
    case FIJ_REG_RCX: out->ptr = (void *)&((struct pt_regs *)regs)->regs[2]; return 0;
    case FIJ_REG_RDX: out->ptr = (void *)&((struct pt_regs *)regs)->regs[3]; return 0;
    case FIJ_REG_RSI: out->ptr = (void *)&((struct pt_regs *)regs)->regs[4]; return 0;
    case FIJ_REG_RDI: out->ptr = (void *)&((struct pt_regs *)regs)->regs[5]; return 0;
    case FIJ_REG_RBP: out->ptr = (void *)&((struct pt_regs *)regs)->regs[29]; return 0; /* FP */
    case FIJ_REG_RSP: out->ptr = (void *)&((struct pt_regs *)regs)->sp; return 0;
    case FIJ_REG_RIP:
        out->ptr  = (void *)&((struct pt_regs *)regs)->pc;
        return 0;
    default:
        return -EINVAL;
    }
}
#endif /* CONFIG_ARM64 */


/* ---------- RISC-V (rv64/rv32) ---------- */
#ifdef CONFIG_RISCV
#include <asm/ptrace.h>

static inline u8 riscv_width_bits(void)
{
#ifdef CONFIG_64BIT
    return 64;
#else
    return 32;
#endif
}

static int fij_arch_map_riscv(const struct pt_regs *regs, int id,
                              struct fij_reg_view *out)
{
    out->width = riscv_width_bits();
    switch (id) {
    /* Provide a pragmatic mapping for your current UAPI names */
    case FIJ_REG_RAX: out->ptr = (void *)&((struct pt_regs *)regs)->a0; return 0; /* x10 */
    case FIJ_REG_RBX: out->ptr = (void *)&((struct pt_regs *)regs)->s1; return 0; /* x9  */
    case FIJ_REG_RCX: out->ptr = (void *)&((struct pt_regs *)regs)->a1; return 0; /* x11 */
    case FIJ_REG_RDX: out->ptr = (void *)&((struct pt_regs *)regs)->a2; return 0; /* x12 */
    case FIJ_REG_RSI: out->ptr = (void *)&((struct pt_regs *)regs)->a3; return 0; /* x13 */
    case FIJ_REG_RDI: out->ptr = (void *)&((struct pt_regs *)regs)->a0; return 0; /* x10 */
    case FIJ_REG_RBP: out->ptr = (void *)&((struct pt_regs *)regs)->s0; return 0; /* x8  */
    case FIJ_REG_RSP: out->ptr = (void *)&((struct pt_regs *)regs)->sp; return 0; /* x2  */
    case FIJ_REG_RIP:
        out->ptr  = (void *)&((struct pt_regs *)regs)->epc;
        return 0;
    default:
        return -EINVAL;
    }
}
#endif /* CONFIG_RISCV */


/* ---------- dispatcher ---------- */
int fij_arch_map(const struct pt_regs *regs, int id, struct fij_reg_view *out)
{
    if (!regs || !out)
        return -EINVAL;

#ifdef CONFIG_X86
    return fij_arch_map_x86(regs, id, out);
#elif defined(CONFIG_ARM64)
    return fij_arch_map_arm64(regs, id, out);
#elif defined(CONFIG_RISCV)
    return fij_arch_map_riscv(regs, id, out);
#else
    return -EOPNOTSUPP;
#endif
}
EXPORT_SYMBOL_GPL(fij_arch_map);
