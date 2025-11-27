#pragma once

#if defined(__x86_64__) || defined(__i386__)
#define CONFIG_X86 1
#endif

#if defined(__aarch64__)
#define CONFIG_ARM64 1
#endif

#if defined(__riscv)
#define CONFIG_RISCV 1
#endif
