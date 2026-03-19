#include <kernel/kconfig.h>
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"

// Shell doesn't need core headers anymore!
// We'll use K-ABI functions only.

// Sysmon module functions
extern void sysmon_top(void);
extern void sysmon_ps_verbose(void);
extern void sysmon_mem_stats(void);
#include "../drivers/ide.h"

static int strcmp(char s1[], char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

static int strlen(char s[]) {
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

static int startsWith(char *str, char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int execute_elf(const char *path, int argc, char **argv) {
    int pid = kabi_fork();
    if (pid == 0) {
        // Child: Execute the new program
        int result;
        __asm__ __volatile__ (
            "int $0x80"
            : "=a" (result)
            : "a" (31), "b" (path), "c" (argv)
        );
        
        // If we reach here, exec failed
        if (kabi_debug_enabled()) {
            kprint("[CHILD] exec failed: ");
            kprint((char*)path);
            kprint("\n");
        }
        kabi_task_exit();
        return -1;
    } else if (pid > 0) {
        // Parent: Wait for child
        kabi_wait_for_children();
        return 0;
    } else {
        kprint("[SHELL] Fork failed.\n");
        return -1;
    }
}

void shell_user_input(char *input) {
    char *argv[16];
    int argc = 0;
    char *p = input;
    
    // Simple tokenizer
    while (*p && argc < 15) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc] = 0;

    if (argc == 0) return;
    char *cmd = argv[0];

    if (strcmp(cmd, "HELP") == 0) {
        kprint("Curls OS K-ABI Shell\n"
               "Commands: \nHELP, \nCLEAR, \nDEVS, \nIDETEST, \nFATWRITE <path>, \nMOUNT <dev> <path>, \nLS <path>, \nCAT <path>, \nPS, \nMEM, \nTOP, \nMEMSTAT, \nPSV, \nTEST, \nSTRESS, \nCORE, \nUSER, \nKILL <pid>, \nTERM <pid>, \nDEBUG <ON/OFF>, \nEND, \nREBOOT\n");
    } else if (strcmp(cmd, "DEBUG") == 0) {
        if (argc < 2) {
            kprint("DEBUG is "); kprint(kabi_debug_enabled() ? "ON\n" : "OFF\n");
        } else {
            if (strcmp(argv[1], "ON") == 0) kabi_set_debug(1);
            else if (strcmp(argv[1], "OFF") == 0) kabi_set_debug(0);
            else kprint("Usage: DEBUG <ON/OFF>\n");
        }
    } else if (strcmp(cmd, "CLEAR") == 0) {
        kabi_clear_screen();
    } else if (strcmp(cmd, "DEVS") == 0) {
        int count = kabi_get_device_count();
        char s[32];
        kprint("Block devices:\n");
        for (int i = 0; i < count; i++) {
            char name[32];
            if (kabi_get_device_name(i, name) == 0) {
                kprint("  dev");
                kabi_int_to_ascii(i, s); kprint(s);
                kprint(": ");
                kprint(name);
                uint64_t sectors = kabi_get_device_size(i);
                if (sectors > 0) {
                    uint64_t mb = sectors / 2048;
                    kprint("  (");
                    kabi_int_to_ascii(sectors, s); kprint(s);
                    kprint(" sectors, ");
                    if (mb > 0) {
                        kabi_int_to_ascii(mb, s); kprint(s);
                        kprint(" MB");
                    } else {
                        kabi_int_to_ascii(sectors / 2, s); kprint(s);
                        kprint(" KB");
                    }
                    kprint(")");
                }
                kprint("\n");
            }
        }
        kprint("\nUse: MOUNT <dev#> <path>  (e.g. MOUNT 2 /usb)\n");
    } else if (strcmp(cmd, "IDETEST") == 0) {
        uint8_t write_buf[512];
        uint8_t read_buf[512];
        for (int i = 0; i < 512; i++) write_buf[i] = i % 256;

        if (kabi_debug_enabled()) kprint("[IDETEST] Writing sector 500...\n");
        if (ide_write_sector(500, write_buf) != 0) {
            kprint("[IDETEST] Write FAILED\n");
        } else {
            if (kabi_debug_enabled()) kprint("[IDETEST] Reading sector 500...\n");
            memory_set(read_buf, 0, 512); // Clear buffer first
            if (ide_read_sector(500, read_buf) != 0) {
                kprint("[IDETEST] Read FAILED\n");
            } else {
                int match = 1;
                for (int i = 0; i < 512; i++) {
                    if (write_buf[i] != read_buf[i]) {
                        match = 0;
                        break;
                    }
                }
                if (kabi_debug_enabled()) {
                    if (match) kprint("[IDETEST] SUCCESS: Sector verified!\n");
                    else kprint("[IDETEST] FAILURE: Data mismatch!\n");
                }
            }
        }
    } else if (strcmp(cmd, "FATWRITE") == 0) {
        if (argc < 2) {
            kprint("Usage: FATWRITE <path>\n");
        } else {
            char *path = argv[1];
            kabi_fs_node_t *node = kabi_vfs_resolve_path(path);
            if (node) {
                char *msg = "KERNEL_WAS_HERE_2026";
                uint32_t len = strlen(msg);
                if (kabi_debug_enabled()) {
                    kprint("[FATWRITE] Overwriting start of "); kprint(path); kprint("...\n");
                }
                uint64_t written = kabi_vfs_write(node, 0, len, (uint8_t*)msg);
                if (written == len) {
                    if (kabi_debug_enabled()) {
                        char buf[32];
                        kprint("[FATWRITE] SUCCESS: wrote "); kabi_int_to_ascii(written, buf); kprint(buf); kprint(" bytes.\n");
                    }
                } else {
                    char buf[32];
                    kprint("[FATWRITE] FAILURE: only wrote "); kabi_int_to_ascii(written, buf); kprint(buf); kprint(" bytes.\n");
                }
            } else {
                kprint("[FATWRITE] File not found.\n");
            }
        }
    } else if (strcmp(cmd, "END") == 0) {
        if (kabi_request_shutdown() < 0) {
            kprint("Permission denied or shutdown failed.\n");
        }
    } else if (strcmp(cmd, "MOUNT") == 0) {
        if (argc < 3) {
            kprint("Usage: MOUNT <dev> <path>\n");
            kprint("  dev: device index (e.g. 2) or name (e.g. usb0, dev0)\n");
        } else {
            char *dev_str = argv[1];
            char *path_str = argv[2];
            int dev_id = -1;

            /* Try by name first */
            int count = kabi_get_device_count();
            for (int i = 0; i < count; i++) {
                char name[32];
                if (kabi_get_device_name(i, name) == 0) {
                    if (strcmp(dev_str, name) == 0) {
                        dev_id = i;
                        break;
                    }
                }
            }

            /* Try as "devN" */
            if (dev_id < 0 && dev_str[0] == 'd' && dev_str[1] == 'e' && dev_str[2] == 'v') {
                dev_id = dev_str[3] - '0';
            }

            /* Try as plain number */
            if (dev_id < 0 && dev_str[0] >= '0' && dev_str[0] <= '9') {
                dev_id = dev_str[0] - '0';
            }

            if (dev_id < 0 || dev_id >= count) {
                kprint("Unknown device: "); kprint(dev_str); kprint("\n");
            } else {
                extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
                if (fat32_vfs_mount(dev_id, path_str) == 0) {
                    kprint("Mounted "); kprint(dev_str); kprint(" at "); kprint(path_str); kprint("\n");
                } else {
                    kprint("Mount failed (device may not have a FAT32 partition).\n");
                }
            }
        }
    } else if (strcmp(cmd, "LS") == 0) {
        char *path = "/";
        if (argc > 1) path = argv[1];

        kabi_fs_node_t *node = kabi_vfs_resolve_path(path);

        if (node) {
            int i = 0;
            kabi_dirent_t *ent;
            while ((ent = kabi_vfs_readdir(node, i++)) != 0) {
                kprint(ent->name);
                kprint("\n");
                kfree(ent);
            }
        } else {
            kprint("Path not found.\n");
        }
    } else if (strcmp(cmd, "CAT") == 0) {
        if (argc < 2) {
             kprint("Usage: CAT <path>\n");
        } else {
            char *path = argv[1];
            kabi_fs_node_t *node = kabi_vfs_resolve_path(path);
            
            if (node) {
                uint8_t buf[513];
                uint64_t offset = 0;
                uint64_t sz;
                while ((sz = kabi_vfs_read(node, offset, 512, buf)) > 0) {
                    buf[sz] = 0;
                    kprint((char*)buf);
                    offset += sz;
                }
                kprint("\n");
            } else {
                kprint("File not found.\n");
            }
        }

        // PROC/MEM COMMANDS
    } else if (strcmp(cmd, "EXEC") == 0) {
        if (argc < 2) {
            kprint("Usage: EXEC <path> [args...]\n");
        } else {
            execute_elf(argv[1], argc - 1, &argv[1]);
        }
    } else if (strcmp(cmd, "PS") == 0) {
        kabi_ps();
    } else if (strcmp(cmd, "MEM") == 0) {
        kabi_heap_stats_t heap;
        kabi_pmm_stats_t pmm;
        kabi_get_heap_stats(&heap);
        kabi_get_pmm_stats(&pmm);
        
        kprint("--- Memory Statistics ---\n");
        kprint("Kernel Heap: ");
        char buf[32];
        kabi_int_to_ascii(heap.used_size / 1024, buf); kprint(buf); kprint("/");
        kabi_int_to_ascii(heap.total_size / 1024, buf); kprint(buf); kprint(" KB used\n");
        
        kprint("Physical RAM: ");
        kabi_int_to_ascii(pmm.used_frames * 4, buf); kprint(buf); kprint("/");
        kabi_int_to_ascii(pmm.total_frames * 4, buf); kprint(buf); kprint(" KB used\n");

        // SYS MON COMMANDS
    } else if (strcmp(input, "TOP") == 0) {
        sysmon_top();
    } else if (strcmp(input, "MEMSTAT") == 0) {
        sysmon_mem_stats();
    } else if (strcmp(input, "PSV") == 0) {
        sysmon_ps_verbose();

        // TEST/STRESS COMMANDS
    } else if (strcmp(cmd, "TEST") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_PAGING, argc > 1 ? argv[1] : "");
#else
        kprint("Debug tests disabled in production ABI.\n");
#endif
    } else if (strcmp(cmd, "STRESS") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_STRESS, argc > 1 ? argv[1] : "");
#else
        kprint("Stress tests disabled in production ABI.\n");
#endif
    } else if (strcmp(cmd, "CORE") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_CORE, "");
#else
        kprint("Core tests disabled in production ABI.\n");
#endif
    } else if (strcmp(input, "USER") == 0) {
        extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
        
        // Auto-mount dev0 if /BIN/INIT.ELF is not found
        if (!kabi_vfs_resolve_path("/BIN/INIT.ELF")) {
            if (kabi_debug_enabled()) kprint("[USER] Mounting dev0 at / ...\n");
            if (fat32_vfs_mount(0, "/") != 0) {
                kprint("[USER] Mount failed! Cannot proceed.\n");
                return;
            }
        }

        if (kabi_debug_enabled()) kprint("[USER] Executing /BIN/INIT.ELF ...\n");
        char *init_argv[] = {"/BIN/INIT.ELF", 0};
        execute_elf("/BIN/INIT.ELF", 1, init_argv);

#ifdef ARCH_X86_64
        if (kabi_debug_enabled()) kprint("[USER] Starting User Shell /BIN/SH64.ELF ...\n");
        char *sh_argv[] = {"/BIN/SH64.ELF", 0};
        execute_elf("/BIN/SH64.ELF", 1, sh_argv);
#else
        if (kabi_debug_enabled()) kprint("[USER] Starting User Shell /BIN/SH.ELF ...\n");
        char *sh_argv[] = {"/BIN/SH.ELF", 0};
        execute_elf("/BIN/SH.ELF", 1, sh_argv);
#endif
    } else if (startsWith(input, "KILL ")) {
        int pid = 0;
        char *p = input + 5;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (*p - '0');
            p++;
        }
        if (kabi_task_signal(pid, KABI_SIGNAL_KILL) < 0) {
            kprint("Permission denied or kill failed.\n");
        }
    } else if (startsWith(input, "TERM ")) {
        int pid = 0;
        char *p = input + 5;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (*p - '0');
            p++;
        }
        if (kabi_task_signal(pid, KABI_SIGNAL_TERM) < 0) {
            kprint("Permission denied or termination failed.\n");
        }
    } else if (strcmp(input, "REBOOT") == 0) {
        kabi_request_reboot(REBOOT_REASON_ADMIN);
    } else {
        kprint("Unknown command: ");
        kprint(cmd);
        kprint("\n");
    }
}

void shell_init() {
    kabi_clear_screen();
    kprint("\n[SHELL] Curls OS K-ABI Shell v2\n");
}

void kernel_shell() {
#ifdef KABI_DEBUG
    shell_user_input("CORE");
    shell_user_input("USER"); // DIAGNOSTIC run

    char input[256];
    while (1) {
        kprint("(KABI)> ");
        kabi_get_line(input);
        shell_user_input(input);
    }
#else
    shell_user_input("USER");
    shell_user_input("END");

    while (1) {
        asm volatile("hlt");
    }
#endif
}

/* --- Module Registration --- */
static int shell_module_init(void) { shell_init(); return 0; }

kabi_module_t __kabi_module_shell = {
    .name           = "shell",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = shell_module_init,
    .exit           = NULL,
    .description    = "K-ABI interactive shell"
};
