#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <asm/ptrace.h>
#include "fij.h"
#include "fij_regs.h"

/* ---------- x86 (both 64-bit and compat) ---------- */
#ifdef CONFIG_X86

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
    case FIJ_REG_RIP: out->ptr  = (void *)&((struct pt_regs *)regs)->ip; return 0;
    case FIJ_REG_R8:  out->ptr  = (void *)&((struct pt_regs *)regs)->r8;     return 0;
    case FIJ_REG_R9:  out->ptr  = (void *)&((struct pt_regs *)regs)->r9;     return 0;
    case FIJ_REG_R10: out->ptr  = (void *)&((struct pt_regs *)regs)->r10;    return 0;
    case FIJ_REG_R11: out->ptr  = (void *)&((struct pt_regs *)regs)->r11;    return 0;
    case FIJ_REG_R12: out->ptr  = (void *)&((struct pt_regs *)regs)->r12;    return 0;
    case FIJ_REG_R13: out->ptr  = (void *)&((struct pt_regs *)regs)->r13;    return 0;
    case FIJ_REG_R14: out->ptr  = (void *)&((struct pt_regs *)regs)->r14;    return 0;
    case FIJ_REG_R15: out->ptr  = (void *)&((struct pt_regs *)regs)->r15;    return 0;
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
    case FIJ_REG_X0:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[0];  return 0;
    case FIJ_REG_X1:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[1];  return 0;
    case FIJ_REG_X2:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[2];  return 0;
    case FIJ_REG_X3:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[3];  return 0;
    case FIJ_REG_X4:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[4];  return 0;
    case FIJ_REG_X5:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[5];  return 0;
    case FIJ_REG_X6:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[6];  return 0;
    case FIJ_REG_X7:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[7];  return 0;
    case FIJ_REG_X8:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[8];  return 0;
    case FIJ_REG_X9:  out->ptr = (void *)&((struct pt_regs *)regs)->regs[9];  return 0;
    case FIJ_REG_X10: out->ptr = (void *)&((struct pt_regs *)regs)->regs[10]; return 0;
    case FIJ_REG_X11: out->ptr = (void *)&((struct pt_regs *)regs)->regs[11]; return 0;
    case FIJ_REG_X12: out->ptr = (void *)&((struct pt_regs *)regs)->regs[12]; return 0;
    case FIJ_REG_X13: out->ptr = (void *)&((struct pt_regs *)regs)->regs[13]; return 0;
    case FIJ_REG_X14: out->ptr = (void *)&((struct pt_regs *)regs)->regs[14]; return 0;
    case FIJ_REG_X15: out->ptr = (void *)&((struct pt_regs *)regs)->regs[15]; return 0;
    case FIJ_REG_X16: out->ptr = (void *)&((struct pt_regs *)regs)->regs[16]; return 0;
    case FIJ_REG_X17: out->ptr = (void *)&((struct pt_regs *)regs)->regs[17]; return 0;
    case FIJ_REG_X18: out->ptr = (void *)&((struct pt_regs *)regs)->regs[18]; return 0;
    case FIJ_REG_X19: out->ptr = (void *)&((struct pt_regs *)regs)->regs[19]; return 0;
    case FIJ_REG_X20: out->ptr = (void *)&((struct pt_regs *)regs)->regs[20]; return 0;
    case FIJ_REG_X21: out->ptr = (void *)&((struct pt_regs *)regs)->regs[21]; return 0;
    case FIJ_REG_X22: out->ptr = (void *)&((struct pt_regs *)regs)->regs[22]; return 0;
    case FIJ_REG_X23: out->ptr = (void *)&((struct pt_regs *)regs)->regs[23]; return 0;
    case FIJ_REG_X24: out->ptr = (void *)&((struct pt_regs *)regs)->regs[24]; return 0;
    case FIJ_REG_X25: out->ptr = (void *)&((struct pt_regs *)regs)->regs[25]; return 0;
    case FIJ_REG_X26: out->ptr = (void *)&((struct pt_regs *)regs)->regs[26]; return 0;
    case FIJ_REG_X27: out->ptr = (void *)&((struct pt_regs *)regs)->regs[27]; return 0;
    case FIJ_REG_X28: out->ptr = (void *)&((struct pt_regs *)regs)->regs[28]; return 0;
    case FIJ_REG_X29: out->ptr = (void *)&((struct pt_regs *)regs)->regs[29]; return 0;
    case FIJ_REG_X30: out->ptr = (void *)&((struct pt_regs *)regs)->regs[30]; return 0;

    case FIJ_REG_SP:  out->ptr = (void *)&((struct pt_regs *)regs)->sp;        return 0;
    case FIJ_REG_PC:  out->ptr = (void *)&((struct pt_regs *)regs)->pc;        return 0;

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
    /* x0 (zero) has no dedicated field in pt_regs; treat as unsupported here */
    case FIJ_REG_ZERO:
        return -EINVAL;

    case FIJ_REG_RA:   out->ptr = (void *)&((struct pt_regs *)regs)->ra;   return 0; /* x1  */
    case FIJ_REG_SP:   out->ptr = (void *)&((struct pt_regs *)regs)->sp;   return 0; /* x2  */
    case FIJ_REG_GP:   out->ptr = (void *)&((struct pt_regs *)regs)->gp;   return 0; /* x3  */
    case FIJ_REG_TP:   out->ptr = (void *)&((struct pt_regs *)regs)->tp;   return 0; /* x4  */

    case FIJ_REG_T0:   out->ptr = (void *)&((struct pt_regs *)regs)->t0;   return 0; /* x5  */
    case FIJ_REG_T1:   out->ptr = (void *)&((struct pt_regs *)regs)->t1;   return 0; /* x6  */
    case FIJ_REG_T2:   out->ptr = (void *)&((struct pt_regs *)regs)->t2;   return 0; /* x7  */

    case FIJ_REG_S0:   out->ptr = (void *)&((struct pt_regs *)regs)->s0;   return 0; /* x8  */
    case FIJ_REG_S1:   out->ptr = (void *)&((struct pt_regs *)regs)->s1;   return 0; /* x9  */

    case FIJ_REG_A0:   out->ptr = (void *)&((struct pt_regs *)regs)->a0;   return 0; /* x10 */
    case FIJ_REG_A1:   out->ptr = (void *)&((struct pt_regs *)regs)->a1;   return 0; /* x11 */
    case FIJ_REG_A2:   out->ptr = (void *)&((struct pt_regs *)regs)->a2;   return 0; /* x12 */
    case FIJ_REG_A3:   out->ptr = (void *)&((struct pt_regs *)regs)->a3;   return 0; /* x13 */
    case FIJ_REG_A4:   out->ptr = (void *)&((struct pt_regs *)regs)->a4;   return 0; /* x14 */
    case FIJ_REG_A5:   out->ptr = (void *)&((struct pt_regs *)regs)->a5;   return 0; /* x15 */
    case FIJ_REG_A6:   out->ptr = (void *)&((struct pt_regs *)regs)->a6;   return 0; /* x16 */
    case FIJ_REG_A7:   out->ptr = (void *)&((struct pt_regs *)regs)->a7;   return 0; /* x17 */

    case FIJ_REG_S2:   out->ptr = (void *)&((struct pt_regs *)regs)->s2;   return 0; /* x18 */
    case FIJ_REG_S3:   out->ptr = (void *)&((struct pt_regs *)regs)->s3;   return 0; /* x19 */
    case FIJ_REG_S4:   out->ptr = (void *)&((struct pt_regs *)regs)->s4;   return 0; /* x20 */
    case FIJ_REG_S5:   out->ptr = (void *)&((struct pt_regs *)regs)->s5;   return 0; /* x21 */
    case FIJ_REG_S6:   out->ptr = (void *)&((struct pt_regs *)regs)->s6;   return 0; /* x22 */
    case FIJ_REG_S7:   out->ptr = (void *)&((struct pt_regs *)regs)->s7;   return 0; /* x23 */
    case FIJ_REG_S8:   out->ptr = (void *)&((struct pt_regs *)regs)->s8;   return 0; /* x24 */
    case FIJ_REG_S9:   out->ptr = (void *)&((struct pt_regs *)regs)->s9;   return 0; /* x25 */
    case FIJ_REG_S10:  out->ptr = (void *)&((struct pt_regs *)regs)->s10;  return 0; /* x26 */
    case FIJ_REG_S11:  out->ptr = (void *)&((struct pt_regs *)regs)->s11;  return 0; /* x27 */

    case FIJ_REG_T3:   out->ptr = (void *)&((struct pt_regs *)regs)->t3;   return 0; /* x28 */
    case FIJ_REG_T4:   out->ptr = (void *)&((struct pt_regs *)regs)->t4;   return 0; /* x29 */
    case FIJ_REG_T5:   out->ptr = (void *)&((struct pt_regs *)regs)->t5;   return 0; /* x30 */
    case FIJ_REG_T6:   out->ptr = (void *)&((struct pt_regs *)regs)->t6;   return 0; /* x31 */

    case FIJ_REG_PC:   out->ptr = (void *)&((struct pt_regs *)regs)->epc;  return 0;

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
#elif CONFIG_ARM64
    return fij_arch_map_arm64(regs, id, out);
#elif CONFIG_RISCV
    return fij_arch_map_riscv(regs, id, out);
#else
    return -EOPNOTSUPP;
#endif
}
EXPORT_SYMBOL_GPL(fij_arch_map);
