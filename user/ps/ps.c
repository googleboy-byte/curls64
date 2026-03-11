#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    static uabi_proc_info_t procs[32];
    int count = uabi_ps(procs, 32);
    
    if (count < 0) {
        ulib_print("ps: failed to get process list\n");
        uabi_exit(1);
    }
    
    ulib_print("PID  PPID STATE EIP      ESP\n");
    for (int i = 0; i < count; i++) {
        char buf[16];
        
        ulib_int_to_str(procs[i].pid, buf);
        ulib_print(buf);
        ulib_print("    ");
        
        ulib_int_to_str(procs[i].parent_pid, buf);
        ulib_print(buf);
        ulib_print("    ");
        
        ulib_int_to_str(procs[i].state, buf);
        ulib_print(buf);
        ulib_print("     ");
        
        ulib_int_to_str(procs[i].user_eip, buf);
        ulib_print(buf);
        ulib_print(" ");
        
        ulib_int_to_str(procs[i].user_esp, buf);
        ulib_print(buf);
        ulib_print("\n");
    }
    uabi_exit(0);
}
