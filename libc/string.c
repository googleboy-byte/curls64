#include "string.h"
#include <stdint.h>

/**
 * K&R implementation of int_to_ascii
 */
void int_to_ascii(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    reverse(str);
}

void hex_to_ascii(uint64_t n, char str[]) {
    str[0] = '0';
    str[1] = 'x';
    int i = 2;
    char zeros = 0;

    uint64_t tmp;
    int j;
    for (j = 60; j >= 0; j -= 4) {
        tmp = (n >> j) & 0xF;
        if (tmp == 0 && zeros == 0 && j > 0) continue;
        zeros = 1;
        if (tmp >= 0xA) str[i++] = (char)(tmp - 0xA + 'a');
        else str[i++] = (char)(tmp + '0');
    }
    str[i] = '\0';
}

void hex64_to_ascii(uint64_t n, char str[]) {
    hex_to_ascii(n, str);
}

/* K&R */
void reverse(char s[]) {
    int c, i, j;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* K&R */
int strlen(const char s[]) {
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

void append(char s[], char n) {
    int len = strlen(s);
    s[len] = n;
    s[len+1] = '\0';
}

void backspace(char s[]) {
    int len = strlen(s);
    if (len > 0) s[len-1] = '\0';
}

int strcmp(const char s1[], const char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

char *strcat(char *dest, const char *src) {
    int dest_len = strlen(dest);
    int i = 0;
    while (src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
    return dest;
}
