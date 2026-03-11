#include "test_paging.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../cpu/paging.h"
#include "../../../../libc/mem.h"
#include "../../../../libc/string.h"
#include "../../../../libc/function.h"
#include "test_heap.h"
#include "../../task.h"
#include "test_fd.h"
#include "test_pipe.h"
#include "test_signal.h"
#include "../core_tests/core_test_v1.h"
#include "../../../modules/test/module_tests_runner.h"

void print_test_menu() {
    kprint("\n--- Page Fault Test Menu ---\n");
    kprint("FAULTNULL    : Dereference NULL (0x0)\n");
    kprint("FAULTOOB     : Access unmapped memory (0xA0000000)\n");
    kprint("FAULTSTACK   : Infinite recursion (Stack Overflow)\n");
    kprint("FAULTEXEC    : Execute data (Simulated)\n");
    kprint("FAULTKERNEL  : User -> Kernel access (Simulated)\n");
    kprint("\n--- Heap & Tasking ---\n");
    kprint("HEAP         : Test Kernel Heap (Alloc/Free)\n");
    kprint("FORK         : Fork process (Background task)\n");
    kprint("FD           : Test File Descriptor Subsystem\n");
    kprint("PIPE         : Test Anonymous Pipes Suite\n");
    kprint("SIGNAL       : Test Signal System (KILL/TERM/CHLD)\n");
    kprint("\n--- Core Tests ---\n");
    kprint("CORE_V1      : Run Core Test v1.0\n");
    kprint("MODULES      : Run All Module Unit Tests\n");
    kprint("\n--- Exit ---\n");
    kprint("EXIT         : Return to main shell\n");
}

/* Force recursion to overflow stack */
void cause_stack_overflow(int depth) {
    char buffer[1024]; // Allocate 1KB on stack per frame
    // Prevent optimization
    buffer[0] = (char)depth; 
    kprint("."); 
    cause_stack_overflow(depth + 1);
}

void trigger_null() {
    kprint("Unmapping 0x0... ");
    unmap_page(0x0); // Unmap the Null Page
    kprint("Done. Dereferencing 0x0...\n");
    uint32_t *ptr = (uint32_t*)0x0;
    uint32_t val = *ptr; // BOOM
    UNUSED(val);
}

void trigger_oob() {
    kprint("Accessing 0xA0000000 (Unmapped)...\n");
    uint32_t *ptr = (uint32_t*)0xA0000000;
    uint32_t val = *ptr; // BOOM
    UNUSED(val);
}

void trigger_stack() {
    kprint("Starting infinite recursion...\n");
    cause_stack_overflow(0);
}

void trigger_exec() {
    kprint("Jumping to Data (heap)...\n");
    uint32_t *code = (uint32_t*)kmalloc(16, 1, 0);
    *code = 0x90909090; // NOPs
    // Real NX requires hardware support. 
    // This will likely just execute gracefully or crash with Invalid Opcode if garbage.
    // However, if we marked page as User/RO, etc...
    // For now we just jump there. If no NX bit, it runs.
    // To simulate, we can manually call page_fault with Instruction Fetch bit set? No that's cheating.
    // The user asked for "Fault Type: Instruction fetch violation".
    // 32-bit Paging does not support NX bit (Bit 63). 
    // So this test might fail to trigger a fault on x86 unless we are in PAE mode.
    // I will print a warning.
    kprint("WARNING: x86 32-bit Paging does not support NX bit.\n");
    kprint("This will likely NOT fault unless we simulate it.\n");
    
    /* Function pointer cast */
    void (*func)() = (void (*)())code;
    func(); 
}

void trigger_kernel_access() {
    kprint("Simulating User Mode access to Kernel address...\n");
    /* We are in Kernel Mode (Ring 0), so we can access everything.
     * To test this properly, we need to switch to Ring 3.
     * Use a cheat: Manually trigger the Page Fault handler with specific error code?
     * No, let's try to map a page as Supervisor Only, and see if... we update logic?
     * Wait, we ARE supervisor. Supervisor can access Supervisor pages.
     * We cannot trigger "User -> Kernel" fault while IN Kernel mode.
     * We will simulate it by manually raising interrupt 14 logic? 
     * Or just printing "Cannot test without Ring 3 switch".
     * I'll stick to the others for now.
     */
     kprint("Cannot test User->Kernel protection from Ring 0.\n");
}

void handle_test_command(char *input) {
    if (strcmp(input, "FAULTNULL") == 0) {
        trigger_null();
    } else if (strcmp(input, "FAULTOOB") == 0) {
        trigger_oob();
    } else if (strcmp(input, "FAULTSTACK") == 0) {
        trigger_stack();
    } else if (strcmp(input, "FAULTEXEC") == 0) {
        trigger_exec();
    } else if (strcmp(input, "FAULTKERNEL") == 0) {
        trigger_kernel_access();
    } else if (strcmp(input, "HEAP") == 0) {
        test_heap();
    } else if (strcmp(input, "FD") == 0) {
        run_fd_tests();
    } else if (strcmp(input, "PIPE") == 0) {
        run_pipe_tests();
    } else if (strcmp(input, "SIGNAL") == 0) {
        run_signal_tests();
    } else if (strcmp(input, "FORK") == 0) {
        int pid = fork();
        if (pid == 0) {
            // Child
            for(;;) {
                kprint("[CHILD] Pulse...\n");
                // Busy wait
                for(volatile int i=0; i<50000000; i++); 
            }
        } else {
            // Parent
            kprint("Spawned background task PID: ");
            char s[16];
            int_to_ascii(pid, s);
            kprint(s);
            kprint("\n");
        }
    } else if (strcmp(input, "CORE_V1") == 0) {
        run_core_test_v1();
    } else if (strcmp(input, "MODULES") == 0) {
        run_all_module_tests();
    } else if (strcmp(input, "EXIT") == 0) {
        kprint("Exiting Test Mode.\n");
    } else {
        kprint("Unknown command. Try: FAULTNULL, FAULTOOB, FAULTSTACK...\n");
        kprint("(TEST)> ");
    }
}
