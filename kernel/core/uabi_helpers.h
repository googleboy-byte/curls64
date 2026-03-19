#ifndef UABI_HELPERS_H
#define UABI_HELPERS_H

#include <stdint.h>

// U-ABI syscall helper functions
int sys_readdir(const char *path, void *entries_buf, int max_entries, int is64);
int sys_getcwd(char *buf, uint32_t size);
int sys_chdir(const char *path);
int sys_stat(const char *path, void *stat_buf, int is64);
int sys_ps(void *procs_buf, int max_procs, int is64);
int sys_memstat(void *stat_buf, int is64);
int sys_mkdir(const char *path);
int sys_unlink(const char *path);
int sys_devinfo(int index, void *info_buf, int is64);

#endif
