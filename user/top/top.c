#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    while (1) {
        uabi_clear();
        uabi_memstat_t memstat;
        if (uabi_memstat(&memstat) < 0) {
            ulib_print("top: failed to get memory stats\n");
            break;
        }
        
        ulib_print("=== Curls OS Top ===\n");
        ulib_print("Memory: ");
        char buf[16];
        ulib_int_to_str(memstat.used_frames * 4, buf); ulib_print(buf); ulib_print("KB / ");
        ulib_int_to_str(memstat.total_frames * 4, buf); ulib_print(buf); ulib_print("KB total\n\n");

        ulib_print("PID  Ticks  State    \n");
        ulib_print("---  -----  -----    \n");

        uabi_proc_info_t procs[32];
        int num_procs = uabi_ps(procs, 32);
        if (num_procs > 0) {
            for (int i = 0; i < num_procs; i++) {
                // PID
                ulib_int_to_str(procs[i].pid, buf);
                ulib_print(buf);
                int len = ulib_strlen(buf);
                for (int j = 0; j < 5 - len; j++) ulib_print(" ");

                // Ticks
#ifdef ARCH_X86_64
                ulib_u64_to_hex(procs[i].ticks, buf);
#else
                ulib_int_to_str(procs[i].ticks, buf);
#endif
                ulib_print(buf);
                len = ulib_strlen(buf);
                for (int j = 0; j < 7 - len; j++) ulib_print(" ");

                // State
                if (procs[i].state == 0) ulib_print("READY\n");
                else if (procs[i].state == 1) ulib_print("RUNNING\n");
                else if (procs[i].state == 2) ulib_print("WAITING\n");
                else if (procs[i].state == 3) ulib_print("ZOMBIE\n");
            }
        }

        uabi_sleep(500);
    }
    uabi_exit(0);
}
