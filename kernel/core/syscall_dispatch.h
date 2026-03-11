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

#endif
