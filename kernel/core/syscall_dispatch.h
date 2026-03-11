#ifndef SYSCALL_DISPATCH_H
#define SYSCALL_DISPATCH_H

#include "../cpu/isr.h"

#define SYS_PRINT 0
#define SYS_EXIT  1
#define SYS_OPEN  2
#define SYS_CLOSE 3
#define SYS_READ  4
#define SYS_WRITE 5
#define SYS_SEEK  6
#define SYS_DUP   7
#define SYS_DUP2  8
#define SYS_PIPE  9
#define SYS_FORK  10
#define SYS_EXECVE 11

void init_syscalls();

int sys_execve(const char *path, char **argv, registers_t *regs);

/*
 * Arch-transparent register accessor macros for syscall_dispatch.c.
 * On i386: int 0x80 ABI uses eax=sysno, ebx=arg1, ecx=arg2, edx=arg3.
 * On x86_64: int 0x80 ABI uses rax=sysno, rbx=arg1, rcx=arg2, rdx=arg3.
 * Using macros keeps syscall_dispatch.c source-identical across arches.
 */
#ifdef ARCH_X86_64
#  define REGS_SYSNO(r)   ((r)->rax)
#  define REGS_ARG1(r)    ((r)->rbx)
#  define REGS_ARG2(r)    ((r)->rcx)
#  define REGS_ARG3(r)    ((r)->rdx)
#  define REGS_RET(r)     ((r)->rax)
#  define REGS_IP(r)      ((r)->rip)
#  define REGS_SP(r)      ((r)->rsp)
#else
#  define REGS_SYSNO(r)   ((r)->eax)
#  define REGS_ARG1(r)    ((r)->ebx)
#  define REGS_ARG2(r)    ((r)->ecx)
#  define REGS_ARG3(r)    ((r)->edx)
#  define REGS_RET(r)     ((r)->eax)
#  define REGS_IP(r)      ((r)->eip)
#  define REGS_SP(r)      ((r)->esp)
#endif

#endif
