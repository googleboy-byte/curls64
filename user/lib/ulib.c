#include "../../include/module/module_abi_v1.h"

// ============================================================================
// User-Space C Library (Freestanding)
// ============================================================================

// String length
size_t ulib_strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

// String compare
int ulib_strcmp(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] == s2[i]) {
        if (s1[i] == '\0') return 0;
        i++;
    }
    return s1[i] - s2[i];
}

// String compare (n characters)
int ulib_strncmp(const char *s1, const char *s2, int n) {
    if (n == 0) return 0;
    int i = 0;
    while (s1[i] == s2[i] && i < n - 1) {
        if (s1[i] == '\0') return 0;
        i++;
    }
    return s1[i] - s2[i];
}

// String copy
char *ulib_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

// String concatenate
char *ulib_strcat(char *dest, const char *src) {
    size_t dest_len = ulib_strlen(dest);
    int i = 0;
    while (src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
    return dest;
}

// String starts with
int ulib_startswith(const char *str, const char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

// Find substring
char *ulib_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return 0;
}

// Memory copy
void *ulib_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

// Memory set
void *ulib_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

// Integer to string
void ulib_int_to_str(int n, char *str) {
    int i = 0;
    int is_negative = 0;
    
    if (n < 0) {
        is_negative = 1;
        n = -n;
    }
    
    if (n == 0) {
        str[i++] = '0';
    } else {
        char temp[16];
        int j = 0;
        while (n > 0) {
            temp[j++] = '0' + (n % 10);
            n /= 10;
        }
        if (is_negative) {
            str[i++] = '-';
        }
        while (j > 0) {
            str[i++] = temp[--j];
        }
    }
    str[i] = '\0';
}

// 64-bit integer to hex string (16 chars + null)
void ulib_u64_to_hex(uint64_t val, char *buf) {
    const char *hex_chars = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
}

// String to integer
int ulib_str_to_int(const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;
    
    if (str[0] == '-') {
        sign = -1;
        i = 1;
    }
    
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    return result * sign;
}

// Print string to stdout (FD 1)
void ulib_print(const char *str) {
    uint32_t len = (uint32_t)ulib_strlen(str);
    if (len > 0) uabi_write(1, str, len);
}

// Position cursor
void ulib_gotoxy(int col, int row) {
    uabi_gotoxy(col, row);
}

#ifdef ARCH_X86_64
void ulib_u64_to_dec(uint64_t val, char *buf) {
    if (val == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[21]; int i=0;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    int j=0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}
#endif
