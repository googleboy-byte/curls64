#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>
#include <stddef.h>

// String functions
size_t ulib_strlen(const char *str);
int ulib_strcmp(const char *s1, const char *s2);
int ulib_strncmp(const char *s1, const char *s2, int n);
char *ulib_strcpy(char *dest, const char *src);
char *ulib_strcat(char *dest, const char *src);
int ulib_startswith(const char *str, const char *prefix);
char *ulib_strstr(const char *haystack, const char *needle);
void ulib_print(const char *str);
void ulib_gotoxy(int col, int row);

// UABI Syscalls
int uabi_pipe(int fds[2]);
int uabi_dup2(int oldfd, int newfd);
int uabi_dup(int oldfd);

// Memory functions
void *ulib_memcpy(void *dest, const void *src, size_t n);
void *ulib_memset(void *s, int c, size_t n);

// Conversion functions
void ulib_int_to_str(int n, char *str);
int ulib_str_to_int(const char *str);
void ulib_u64_to_hex(uint64_t val, char *buf);
#ifdef ARCH_X86_64
void ulib_u64_to_dec(uint64_t val, char *buf);
#endif

#endif
