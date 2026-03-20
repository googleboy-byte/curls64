#include "../../vfs_core.h"
#include <cpu_local.h>
#include "../../pipe.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../../libc/string.h"
#include "../../../../libc/mem.h"
#include "../../task.h"

void test_fd_open_close() {
    kprint("Opening test file '/testfile'...\n");

    int fd = open("/testfile", O_CREAT | O_RDWR);
    if (fd < 0) {
        kprint("FAILED: open()\n");
        return;
    }

    kprint("Opened FD: ");
    char s[16];
    int_to_ascii(fd, s);
    kprint(s);
    kprint("\n");

    kprint("Closing FD...\n");
    close(fd);

    kprint("FD open/close test complete.\n");
}

void test_fd_rw() {
    kprint("Testing Write/Read...\n");
    int fd = open("/rwtest", O_CREAT | O_RDWR);

    const char *msg = "hello kernel fs";
    write(fd, msg, strlen((char*)msg));

    kprint("Seeking to 0...\n");
    seek(fd, 0, KABI_SEEK_SET);

    char buf[32];
    memory_set((uint8_t*)buf, 0, 32);
    int r = read(fd, buf, sizeof(buf)-1);
    
    kprint("Read back (");
    char sr[10];
    int_to_ascii(r, sr);
    kprint(sr);
    kprint(" bytes): ");
    kprint(buf);
    kprint("\n");

    close(fd);
    kprint("FD read/write test complete.\n");
}

void test_fd_reuse() {
    kprint("Testing FD reuse...\n");
    int fd1 = open("/a", O_CREAT);
    close(fd1);
    int fd2 = open("/b", O_CREAT);

    kprint("FD1 was ");
    char s[16];
    int_to_ascii(fd1, s);
    kprint(s);

    kprint(", FD2 is ");
    int_to_ascii(fd2, s);
    kprint(s);
    kprint("\n");

    if (fd1 == fd2)
        kprint("SUCCESS: FD reuse OK\n");
    else
        kprint("FAILED: FD reuse FAILED\n");

    close(fd2);
}

// Fork test depends on a working fork(). Our current implementation is simplified.
// But we can test if kids inherit FDs.
void test_fd_fork() {
    kprint("Testing FD inheritance (simulated/limited)...\n");
    
    // In our kernel, we don't have a full fork() that copies all state.
    // spawn_process() doesn't inherit FDs right now.
    // Let's at least verify current process behavior.
    
    int fd = open("/forktest", O_CREAT | O_RDWR);
    char s[16];
    int_to_ascii(fd, s);
    kprint("Opened parent FD: "); kprint(s); kprint("\n");
    
    close(fd);
    kprint("FD inheritance test skipped (fork() not fully implemented).\n");
}

void test_fd_stdio() {
    kprint("Testing Standard Streams...\n");
    kprint("Writing to STDOUT (FD 1)...\n");
    write(1, "  [STDOUT TEST SUCCESS]\n", 24);
    
    kprint("Writing to STDERR (FD 2)...\n");
    write(2, "  [STDERR TEST SUCCESS]\n", 24);
    
    kprint("Type something for STDIN (FD 0): ");
    char buf[64];
    int r = read(0, buf, 63);
    buf[r] = '\0';
    kprint("STDIN Read: "); kprint(buf); kprint("\n");
}

void test_fd_dup_offset() {
    kprint("Testing shared offset across dup'd FDs...\n");
    int fd1 = open("/duptest", O_CREAT | O_RDWR);
    int fd2 = dup(fd1);
    
    write(fd1, "Hello", 5);
    
    // Check if fd2 offset is updated
    int off2 = seek(fd2, 0, KABI_SEEK_CUR); 
    // Since fd1 and fd2 point to same file_t*, it MUST be the same.
    // fd1 was at 5 (wrote "Hello")
    if (off2 != 5) {
        kprint("FAILED: dup() offset sync FAILED\n");
    }
    // There's no way to "tell" current offset without a syscall that returns it.
    // Our seek() returns the offset it set to.
    
    // Actually, our seek implementation is:
    /*
    int seek(int fd, int offset) {
        if (fd < 0 || fd >= MAX_FD || !current_task->fd_table[fd]) return -1;
        current_task->fd_table[fd]->offset = offset;
        return offset;
    }
    */
    // Since fd1 and fd2 point to same file_t*, it MUST be the same.
    
    kprint("Seek(fd1, 10) called.\n");
    seek(fd1, 10, KABI_SEEK_SET);
    
    // Verify by manually checking the task struct if needed, or just assume success if it doesn't crash?
    // No, let's write at end of fd1, then read from fd2 and see if we get nothing.
    write(fd1, "A", 1); // at 10
    seek(fd2, 10, KABI_SEEK_SET);
    char c = 0;
    read(fd2, &c, 1);
    
    if (c == 'A') {
        kprint("SUCCESS: FDs share file object (offset check OK)\n");
    } else {
        kprint("FAILED: FDs do not share file object\n");
    }
    
    close(fd1);
    close(fd2);
}

void test_fd_dup2_redirect() {
    kprint("Testing dup2(fd, 1) stdout redirection...\n");
    
    // 1. Save original stdout
    int saved_stdout = dup(1);
    
    // 2. Open a redirection target
    int fd = open("/redirect_test", O_CREAT | O_RDWR);
    
    // 3. Redirect stdout to file
    dup2(fd, 1);
    
    // 4. This should NOT be visible on screen
    write(1, "THIS GOES TO FILE\n", 18);
    
    // 5. Restore original stdout
    dup2(saved_stdout, 1);
    close(saved_stdout);
    
    kprint("Restored STDOUT. Verifying file content...\n");
    seek(fd, 0, KABI_SEEK_SET);
    char buf[32];
    memory_set((uint8_t*)buf, 0, 32);
    read(fd, buf, 18);
    
    if (strcmp(buf, "THIS GOES TO FILE\n") == 0) {
        kprint("SUCCESS: stdout redirection worked.\n");
    } else {
        kprint("FAILED: redirection failed, got: ");
        kprint(buf); kprint("\n");
    }
    
    close(fd);
}

void test_fd_pipe() {
    kprint("Testing Pipes...\n");
    int fds[2];
    if (pipe(fds) < 0) {
        kprint("FAILED: pipe() creation\n");
        return;
    }

    kprint("Pipe FDs: ");
    char s[10];
    int_to_ascii(fds[0], s); kprint(s); kprint(", ");
    int_to_ascii(fds[1], s); kprint(s); kprint("\n");

    const char *msg = "pipe data test";
    write(fds[1], msg, strlen((char*)msg));
    kprint("Wrote to pipe.\n");

    char buf[32];
    memory_set((uint8_t*)buf, 0, 32);
    int r = read(fds[0], buf, 31);
    
    kprint("Read from pipe: ");
    kprint(buf);
    kprint("\n");

    if (strcmp(buf, (char*)msg) == 0) {
        kprint("SUCCESS: pipe data transfer OK\n");
    } else {
        kprint("FAILED: pipe data transfer FAILED\n");
    }

    close(fds[0]);
    close(fds[1]);
}

void test_fd_pipe_eof() {
    kprint("Testing Pipe EOF...\n");
    int fds[2];
    pipe(fds);
    
    close(fds[1]); // Close writer
    
    char buf[10];
    int r = read(fds[0], buf, 10);
    if (r == 0) {
        kprint("SUCCESS: EOF detected on closed pipe\n");
    } else {
        kprint("FAILED: EOF not detected, got ");
        char rs[10]; int_to_ascii(r, rs); kprint(rs); kprint("\n");
    }
    close(fds[0]);
}

void test_fd_pipe_epipe() {
    kprint("Testing Pipe EPIPE...\n");
    int fds[2];
    pipe(fds);
    
    close(fds[0]); // Close reader
    
    int r = write(fds[1], "A", 1);
    if (r == -1) {
        kprint("SUCCESS: Write returned EPIPE (-1) on broken pipe\n");
    } else {
        kprint("FAILED: Write on broken pipe returned ");
        char rs[10]; int_to_ascii(r, rs); kprint(rs);
        kprint(" (expected -1)\n");
    }
    close(fds[1]);
}

// FD Leak Stress Test
// Simulates repeated: open file + pipe() + close all
// If open() or pipe() ever returns -1 we have an FD leak.
void test_fd_pipe_leak() {
    kprint("Testing FD leak (50 iterations of pipe+open+close)...\n");

    int leak_detected = 0;
    for (int i = 0; i < 50; i++) {
        // Create a pipe
        int fds[2];
        if (pipe(fds) < 0) {
            kprint("FAILED: pipe() returned -1 on iteration ");
            char s[16]; int_to_ascii(i, s); kprint(s); kprint(" - FD leak!\n");
            leak_detected = 1;
            break;
        }

        // Open a file
        int fd = open("/leaktest", O_CREAT | O_RDWR);
        if (fd < 0) {
            kprint("FAILED: open() returned -1 on iteration ");
            char s[16]; int_to_ascii(i, s); kprint(s); kprint(" - FD leak!\n");
            // Close pipe before giving up
            close(fds[0]);
            close(fds[1]);
            leak_detected = 1;
            break;
        }

        // Clean up all three
        close(fds[0]);
        close(fds[1]);
        close(fd);

        // Verify all user FDs are free
        for (int j = FIRST_USER_FD; j < MAX_FD; j++) {
            if (current_task->fd_table[j] != 0) {
                kprint("FAILED: FD ");
                char s[16]; int_to_ascii(j, s); kprint(s);
                kprint(" still open after iteration ");
                int_to_ascii(i, s); kprint(s); kprint(" - FD leak!\n");
                leak_detected = 1;
                break;
            }
        }
        if (leak_detected) break;
    }

    if (!leak_detected)
        kprint("SUCCESS: FD leak test passed (50 iterations clean)\n");
}

void run_fd_tests() {
    kprint("\n--- [ TEST ] File Descriptor Suite ---\n");
    test_fd_stdio();
    test_fd_open_close();
    test_fd_rw();
    test_fd_reuse();
    test_fd_dup_offset();
    test_fd_dup2_redirect();
    test_fd_pipe();
    test_fd_pipe_eof();
    test_fd_pipe_epipe();
    test_fd_fork();
    test_fd_pipe_leak();
    kprint("--- [ DONE ] File Descriptor Suite ---\n\n");
}
