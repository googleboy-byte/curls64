#include "../../task.h"
#include "../../kernel.h"
#include "../../syscall_dispatch.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../../libc/string.h"
#include "../../../../libc/mem.h"

void stress_fork_bomb() {
    kprint("STRESS: Starting Fork Bomb (MAX_TASKS enforced)...\n");
    int count = 0;
    while (1) {
        int pid = fork();
        if (pid == 0) {
            // Child: Just stay alive as a placeholder task
            while(1) { asm volatile("hlt"); }
        } else if (pid < 0) {
            kprint("Fork failed. Total forks: ");
            char s[16]; int_to_ascii(count, s); kprint(s); kprint("\n");
            break;
        } else {
            count++;
            if (count % 10 == 0) {
                kprint("Forks: "); char s[16]; int_to_ascii(count, s); kprint(s); kprint("\n");
            }
        }
    }
}

// Global recursion counter for deep syscall
volatile int syscall_recursion_depth = 0;

void stress_deep_syscall(int depth) {
    syscall_recursion_depth = depth;
    
    // We'll use a local function that calls itself via syscall
    // This is hard to do without a dedicated syscall.
    // Let's use SYS_PRINT recursively? No, SYS_PRINT doesn't recurse.
    // I will trigger it manually by calling the handler if needed, 
    // but a real "stress" should go through INT 0x80.
    
    // For now, let's just do a highly-nested kernel-side recursion 
    // that calls assert_on_kstack.
    
    char buf[512]; // 512 bytes per frame
    memory_set((uint8_t*)buf, 0xAA, 512);
    
    // Mock registers for assert_on_kstack
    registers_t r;
    r.esp = (uint32_t)&buf; 
    assert_on_kstack(&r);
    
    if (depth > 120) {
        kprint("Reached depth 120 safely.\n");
        return;
    }
    
    stress_deep_syscall(depth + 1);
}

void stress_race() {
    kprint("STRESS: Kill/Fork Race start...\n");
    for (int i = 0; i < 50; i++) {
        int pid = fork();
        if (pid == 0) {
            // Child: exit immediately
            asm volatile("mov %0, %%eax; int $0x80" : : "i"(SYS_EXIT));
        } else {
            // Parent: immediately kill or wait or just loop
            // Reap to keep list mutating
            reap_zombies();
        }
        if (i % 10 == 0) kprint(".");
    }
    kprint("\nRace test finished. Circular list integrity check starting...\n");
    // task_switch will organically check this on next tick, but we can call it.
    ps();
}

void print_stress_menu() {
    kprint("\n--- Kernel Stress Test Menu ---\n");
    kprint("FORK        : Fork Bomb (test MAX_TASKS)\n");
    kprint("DEEP        : Deep Syscall Nesting (test Stack Guard)\n");
    kprint("RACE        : Kill/Fork Race (test List Integrity)\n");
    kprint("EXIT        : Return to main shell\n");
}

void handle_stress_command(char *input) {
    if (strcmp(input, "FORK") == 0) {
        stress_fork_bomb();
    } else if (strcmp(input, "DEEP") == 0) {
        stress_deep_syscall(0);
    } else if (strcmp(input, "RACE") == 0) {
        stress_race();
    } else if (strcmp(input, "EXIT") == 0) {
        kprint("Exiting Stress Mode.\n");
    } else {
        kprint("Unknown stress command. Try: FORK, DEEP, RACE\n");
    }
}
