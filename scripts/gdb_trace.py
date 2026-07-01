import gdb
import struct

# Safe variable and memory reading helpers
def get_variable(name):
    try:
        frame = gdb.selected_frame()
        return frame.read_var(name)
    except Exception as e1:
        try:
            return gdb.parse_and_eval(name)
        except Exception as e2:
            raise ValueError(f"Could not resolve variable '{name}' (frame error: {e1}, eval error: {e2})")

def get_string_safe(val, max_len=256):
    if val == 0:
        return "NULL"
    try:
        addr = int(val)
        if addr == 0 or addr == 0xcccccccccccccccc:
            return f"(invalid address: {hex(addr)})"
        mem = gdb.selected_inferior().read_memory(addr, max_len).tobytes()
        str_val = mem.split(b'\x00')[0].decode('utf-8', errors='replace')
        return str_val
    except Exception as e:
        return f"(could not read string: {e})"

def walk_argv_safe(argv_val):
    if argv_val == 0:
        return "NULL"
    try:
        argv_addr = int(argv_val)
    except Exception as e:
        return f"(invalid argv value: {e})"
        
    if argv_addr == 0 or argv_addr == 0xcccccccccccccccc:
        return f"(invalid argv pointer: {hex(argv_addr)})"
    
    argv_list = []
    ptr_size = 8  # 64-bit kernel pointers
    
    for i in range(16):  # Limit walk to prevent infinite loops on corrupt frames
        try:
            element_addr = argv_addr + i * ptr_size
            mem = gdb.selected_inferior().read_memory(element_addr, ptr_size).tobytes()
            arg_ptr = struct.unpack("<Q", mem)[0]
            if arg_ptr == 0:
                argv_list.append("NULL")
                break
            if arg_ptr == 0xcccccccccccccccc:
                argv_list.append(f"POISON ({hex(arg_ptr)})")
                break
            str_val = get_string_safe(arg_ptr)
            argv_list.append(f"'{str_val}' ({hex(arg_ptr)})")
        except Exception as e:
            argv_list.append(f"ERROR (argv[{i}] at {hex(element_addr)}: {e})")
            break
    return "[" + ", ".join(argv_list) + "]"

# Define Breakpoints for Critical Subsystems
class ExecveBreakpoint(gdb.Breakpoint):
    def __init__(self):
        # Set breakpoint after prologue to guarantee variables are bound
        super(ExecveBreakpoint, self).__init__("kernel/proc/exec.c:240")

    def stop(self):
        try:
            path_val = get_variable("path")
            path = get_string_safe(path_val)
            argv_val = get_variable("argv")
            argv_str = walk_argv_safe(argv_val)
            
            print(f"\n[GDB TRACE] Breakpoint hit: sys_execve")
            print(f"  path: '{path}' ({path_val})")
            print(f"  argv: {argv_str} ({argv_val})")
            
            # Print backtrace
            print("[GDB TRACE] sys_execve Call Backtrace:")
            gdb.execute("backtrace")
        except Exception as e:
            print(f"[GDB TRACE] Error in sys_execve breakpoint handler: {e}")
        
        return False  # Automatically continue execution

class PageFaultBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PageFaultBreakpoint, self).__init__("page_fault")

    def stop(self):
        try:
            print("\n[GDB TRACE] Breakpoint hit: page_fault")
            try:
                cr2 = gdb.parse_and_eval("read_cr2()")
                print(f"  Faulting address (CR2): {hex(int(cr2))}")
            except Exception as e:
                print(f"  Could not read CR2: {e}")
            gdb.execute("backtrace")
        except Exception as e:
            print(f"[GDB TRACE] Error in page_fault breakpoint handler: {e}")
        return False  # Continue

class PanicBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PanicBreakpoint, self).__init__("panic")

    def stop(self):
        try:
            msg_val = get_variable("message")
            msg = get_string_safe(msg_val)
            print(f"\n[GDB TRACE] KERNEL PANIC HIT: '{msg}'")
            gdb.execute("backtrace")
        except Exception as e:
            print(f"[GDB TRACE] Error in panic breakpoint handler: {e}")
        return True  # Stop execution on panic

# Functions for Analyzing Deadlock and CPU state
def dump_locks():
    print("\n================== SPINLOCK INVENTORY ==================")
    locks = [
        ("rq_lock", "'kernel/core/task.c'::rq_lock"),
        ("pid_lock", "'kernel/core/task.c'::pid_lock"),
        ("sq_lock", "sq_lock"),
        ("global_shootdown.lock", "'kernel/arch/x86_64/smp/smp.c'::global_shootdown.lock")
    ]
    for name, expr in locks:
        try:
            val = gdb.parse_and_eval(expr)
            locked_val = int(val["locked"])
            status = "LOCKED (1)" if locked_val == 1 else f"UNLOCKED ({locked_val})"
            print(f"Lock: {name:<25} Status: {status}")
        except Exception as e:
            print(f"Lock: {name:<25} Error reading: {e}")
    print("========================================================")

def dump_cpu_local_state():
    print("\n================== PER-CPU LOCAL STATE ==================")
    for i in range(4):
        try:
            cpu = gdb.parse_and_eval(f"cpu_local[{i}]")
            cpu_id = int(cpu["id"])
            current = cpu["_current"]
            irq = int(cpu["_irq_depth"])
            print(f"CPU {i}: (configured ID: {cpu_id}, irq_depth: {irq})")
            if current != 0:
                task_id = int(current["id"])
                task_state = int(current["state"])
                task_magic = hex(int(current["magic"]))
                
                state_str = "READY"
                if task_state == 1: state_str = "RUNNING"
                elif task_state == 2: state_str = "WAITING"
                elif task_state == 3: state_str = "ZOMBIE"
                
                print(f"  Current Task: PID {task_id} ({state_str}), magic: {task_magic}")
                try:
                    cwd = current["cwd"].string()
                    print(f"    CWD: '{cwd}'")
                except Exception:
                    pass
            else:
                print("  Current Task: NULL")
        except Exception as e:
            print(f"CPU {i}: Error reading cpu_local[{i}]: {e}")
    print("=========================================================")

def dump_cpu_backtraces():
    print("\n================== ALL CPU BACKTRACES ==================")
    try:
        orig_thread = gdb.selected_thread()
        threads = gdb.selected_inferior().threads()
        for t in threads:
            print(f"\n--- CPU Core {t.num - 1} (GDB Thread ID: {t.num}, ptid: {t.ptid}) ---")
            t.switch()
            gdb.execute("backtrace")
        if orig_thread:
            orig_thread.switch()
    except Exception as e:
        print(f"Error gathering CPU backtraces: {e}")
    print("========================================================")

# Define Custom GDB Command
class DumpCpusCommand(gdb.Command):
    """Dump state of locks, per-CPU structures, and call stacks across all cores.
    Usage: dump-cpus"""
    def __init__(self):
        super(DumpCpusCommand, self).__init__("dump-cpus", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        dump_locks()
        dump_cpu_local_state()
        dump_cpu_backtraces()

# Register Command and Breakpoints
DumpCpusCommand()
ExecveBreakpoint()
PageFaultBreakpoint()
PanicBreakpoint()

print("[GDB TRACE] Breakpoints registered. Custom command 'dump-cpus' is active.")
print("[GDB TRACE] Type 'continue' to start/resume kernel execution.")