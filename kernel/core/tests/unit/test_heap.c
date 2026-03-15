#include "test_heap.h"
#include "../../../../libc/mem.h"
#include "../../../../libc/string.h"
#include "../../../../include/kabi/kabi_v1.h"

void test_heap() {
    kprint("Testing Kernel Heap...\n");

    uintptr_t a = (uintptr_t)kmalloc(16, 0, 0);
    kprint("Allocated 16 bytes at ");
    char str[32]; // Increased size for safety
    str[0] = '\0';
    hex_to_ascii(a, str);
    kprint(str);
    kprint("\n");

    uintptr_t b = (uintptr_t)kmalloc(64, 0, 0);
    kprint("Allocated 64 bytes at ");
    str[0] = '\0';
    hex_to_ascii(b, str);
    kprint(str);
    kprint("\n");

    kprint("Freeing 16 bytes at ");
    str[0] = '\0';
    hex_to_ascii(a, str);
    kprint(str);
    kprint("\n");
    kfree((void*)a);

    uintptr_t c = (uintptr_t)kmalloc(12, 0, 0);
    kprint("Allocated 12 bytes (should reuse space) at 0x");
    hex_to_ascii(c, str);
    kprint(str);
    kprint("\n");

    kprint("Heap test complete.\n\n(TEST)>");
}
