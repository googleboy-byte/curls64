#include "test_pipe.h"
#include "../../vfs_core.h"
#include "../../pipe.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../../libc/string.h"
#include "../../../../libc/mem.h"

static void kassert(int condition, const char *msg) {
    if (!condition) {
        kprint("FAILED: ");
        kprint((char*)msg);
        kprint("\n");
        // We don't halt, so other tests can run
    }
}

void test_pipe_basic() {
    kprint("- Test 1: pipe() basic creation\n");
    int fds[2];
    kassert(pipe(fds) == 0, "pipe() syscall failed");
    kassert(fds[0] >= FIRST_USER_FD, "rfd below FIRST_USER_FD");
    kassert(fds[1] >= FIRST_USER_FD, "wfd below FIRST_USER_FD");
    kassert(fds[0] != fds[1], "duplicate FDs returned");
    
    close(fds[0]);
    close(fds[1]);
}

void test_pipe_roundtrip() {
    kprint("- Test 2: write -> read roundtrip\n");
    int fds[2];
    pipe(fds);
    
    char msg[] = "hello pipe";
    char buf[32];
    memory_set((uint8_t*)buf, 0, 32);

    int n = write(fds[1], msg, strlen(msg));
    kassert(n == (int)strlen(msg), "write length mismatch");

    int r = read(fds[0], buf, sizeof(buf));
    kassert(r == (int)strlen(msg), "read length mismatch");
    kassert(strcmp(buf, msg) == 0, "data corruption in pipe");

    close(fds[0]);
    close(fds[1]);
}

void test_pipe_permissions() {
    kprint("- Test 3: write-only / read-only enforcement\n");
    int fds[2];
    pipe(fds);
    
    char buf[1];
    kassert(read(fds[1], buf, 1) == -1, "read allowed on write end");
    kassert(write(fds[0], "x", 1) == -1, "write allowed on read end");

    close(fds[0]);
    close(fds[1]);
}

void test_pipe_offset() {
    kprint("- Test 4: shared offset behavior (invariant)\n");
    int fds[2];
    pipe(fds);
    
    write(fds[1], "abcdef", 6);
    char buf[4];
    memory_set((uint8_t*)buf, 0, 4);

    read(fds[0], buf, 3);
    kassert(strcmp(buf, "abc") == 0, "first read failed");

    memory_set((uint8_t*)buf, 0, 4); // Re-zero between reads for pedantry
    read(fds[0], buf, 3);
    kassert(strcmp(buf, "def") == 0, "second read failed (offset interference?)");

    close(fds[0]);
    close(fds[1]);
}

void test_pipe_dup() {
    kprint("- Test 5: dup() on read end\n");
    int fds[2];
    pipe(fds);
    
    int r2 = dup(fds[0]);
    write(fds[1], "12345", 5);

    char buf[6];
    memory_set((uint8_t*)buf, 0, 6);
    read(r2, buf, 5);
    kassert(strcmp(buf, "12345") == 0, "dup read failed");

    close(r2);
    close(fds[0]);
    close(fds[1]);
}

void test_pipe_redirect() {
    kprint("- Test 6: dup2() redirection (stdout -> pipe)\n");
    int fds[2];
    pipe(fds);
    
    int old_stdout = dup(1);
    dup2(fds[1], 1);
    write(1, "hi", 2); // Use write syscall instead of kprint to honor redirection
    
    dup2(old_stdout, 1); // Restore stdout
    close(old_stdout);

    char buf[3];
    memory_set((uint8_t*)buf, 0, 3);
    read(fds[0], buf, 2);
    kassert(strcmp(buf, "hi") == 0, "stdout redirection to pipe failed");

    close(fds[0]);
    close(fds[1]);
}

void test_pipe_eof() {
    kprint("- Test 7: close write end -> EOF\n");
    int fds[2];
    pipe(fds);
    
    close(fds[1]);
    char buf[1];
    int n = read(fds[0], buf, 1);
    kassert(n == 0, "EOF not detected after writer closed");

    close(fds[0]);
}

void test_pipe_epipe() {
    kprint("- Test 8: close read end -> write error\n");
    int fds[2];
    pipe(fds);
    
    close(fds[0]);
    int n = write(fds[1], "x", 1);
    kassert(n == -1, "Write should fail on broken pipe (-1)");

    close(fds[1]);
}

void test_pipe_full() {
    kprint("- Test 9: full buffer handling (non-blocking)\n");
    int fds[2];
    pipe(fds);
    
    // Create a buffer twice the pipe size to ensure saturation
    uint8_t *big = (uint8_t*)kmalloc(PIPE_SIZE * 2, 0, 0);
    memory_set(big, 'A', PIPE_SIZE * 2);

    int n = write(fds[1], (char*)big, PIPE_SIZE * 2);
    kassert(n == PIPE_SIZE, "partial write to full pipe failed");

    kfree(big);
    close(fds[0]);
    close(fds[1]);
}

void test_pipe_leaks() {
    kprint("- Test 10: close both ends -> free pipe (refcount validation)\n");
    int initial_count = get_debug_pipe_count();
    
    int fds[2];
    pipe(fds);
    kassert(get_debug_pipe_count() == initial_count + 1, "Pipe count did not increase");

    close(fds[0]);
    close(fds[1]);
    
    kassert(get_debug_pipe_count() == initial_count, "Pipe count did not return to initial (LEAK detected!)");
    kprint("SUCCESS: Pipe memory reclaimed.\n");
}

void run_pipe_tests() {
    kprint("\n--- [ TEST ] Anonymous Pipes Suite ---\n");
    test_pipe_basic();
    test_pipe_roundtrip();
    test_pipe_permissions();
    test_pipe_offset();
    test_pipe_dup();
    test_pipe_redirect();
    test_pipe_eof();
    test_pipe_epipe();
    test_pipe_full();
    test_pipe_leaks();
    kprint("--- [ DONE ] Anonymous Pipes Suite ---\n\n(TEST)>");
}
