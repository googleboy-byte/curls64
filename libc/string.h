#ifndef STRINGS_H
#define STRINGS_H

#include <stdint.h>
#include <stddef.h>

void int_to_ascii(int n, char str[]);
void hex_to_ascii(uint64_t n, char str[]);
void hex64_to_ascii(uint64_t n, char str[]);
void reverse(char s[]);
int strlen(const char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(const char s1[], const char s2[]);
void strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);

#endif
