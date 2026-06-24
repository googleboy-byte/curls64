# Architectural Decision Records

> Records of non-obvious technical decisions — what we chose, what we considered, and why.

---

## Shared `SMP_MAX_CPUS` constant vs per-file `#define`

**Context:**
`cpu_local[]` array in `gdt.c` used a local `#define MAX_SMP_CPUS 8`, while `ap_trampoline_stacks[]` in `smp.c` hardcoded `[8]`. Both are governed by the same constraint (uint32_t bitmask width for `ap_ready_flags` and `pending_mask`). If one changes without the other, out-of-bounds access or lost CPU bits result.

**Options considered:**
1. Keep per-file defines, document the coupling in comments
2. A shared `smp_config.h` header with a single `SMP_MAX_CPUS`
3. Derive the limit at runtime from the bitmask type (`sizeof(uint32_t) * 8`)

**Decision:**
Option 2 — shared header `kernel/include/smp_config.h`.

**Rationale:**
Option 1 is fragile; comments drift. Option 3 adds runtime complexity for a compile-time constant. A shared header is zero-cost, grep-friendly, and makes the coupling explicit. Named `SMP_MAX_CPUS` (not `MAX_CPUS`) to avoid collision with the existing `MAX_CPU 1024` in `task.h` which governs GDT TSS slot count, not the SMP AP limit.

---

## Atomic `__sync_fetch_and_or` for `ap_ready_flags` vs spinlock

**Context:**
Multiple APs set their ready bit in `ap_ready_flags` during boot. The plain `|=` is a non-atomic read-modify-write that can lose a bit if two APs execute it concurrently.

**Options considered:**
1. Wrap the `|=` in a spinlock
2. Use `__sync_fetch_and_or` (GCC atomic builtin)
3. Use `lock bts` inline assembly

**Decision:**
Option 2 — `__sync_fetch_and_or`.

**Rationale:**
A spinlock is overkill for a single atomic OR. `lock bts` is correct but less readable and ties us to x86 assembly. The GCC builtin is portable across our supported compilers, generates `lock or` on x86, and is the same pattern already used for `pending_mask` clear (`__sync_fetch_and_and`).

---

## GS-base per-CPU storage via `wrmsr`/`rdmsr` vs segment-based or array lookup
*(Commits: `dac4df9`, `395ccae`)*

**Context:**
SMP requires each CPU to find its own `cpu_local_t` struct without locks. On x86_64, the GS base register can hold an arbitrary 64-bit pointer, readable from both C (via `rdmsr`) and assembly (via `gs:offset`).

**Options considered:**
1. CPUID-based array lookup: `cpu_local[get_apic_id()]` — requires CPUID instruction on every access
2. GS-base pointer: `wrmsr(IA32_GS_BASE, &cpu_local[id])` at boot, then `rdmsr` or `gs:` prefix for access
3. Thread-local storage (`__thread`) — requires linker/runtime support not available in a freestanding kernel

**Decision:**
Option 2 — GS base via `wrmsr`/`rdmsr` (`MSR 0xC0000101`).

**Rationale:**
CPUID is a serialising instruction, far too expensive for the hot path (every `current_task` access). GS base is set once per CPU at boot and then read via a fast MSR read in C (`get_cpu_local()`) or zero-cost `gs:` prefix in assembly. The initial UP implementation (commit `c48d95a`) used a simple `&cpu_local[0]` inline function with `#error` guards for `SMP` — the GS-base version was a drop-in replacement once `wrmsr` was wired up.

---

## Per-CPU `task_switch_rsp` via GS base vs global variable
*(Commit: `a02dd47`)*

**Context:**
The interrupt return path in `interrupt64.asm` checks a `task_switch_rsp` variable to decide whether to switch stacks. Originally this was a single global variable (`extern volatile uint64_t task_switch_rsp`). When SMP was enabled, an AP returning from a TLB shootdown IPI could read the BSP's pending `task_switch_rsp`, steal its stack, and cause a GPF. The initial fix (commit `2d7d9ba`) was to zero the global in the TLB handler — correct but fragile.

**Options considered:**
1. Keep global, zero it in every IPI handler — requires remembering to add the neutralisation in every new IPI handler
2. Move `task_switch_rsp` into `cpu_local_t`, access via `gs:CPU_LOCAL_TASK_SWITCH_RSP` in assembly
3. Use a per-CPU array indexed by APIC ID from assembly

**Decision:**
Option 2 — `_task_switch_rsp` field in `cpu_local_t`, accessed via `mov rax, [gs:40]`.

**Rationale:**
Option 1 is a ticking time bomb — every new IPI handler must remember to zero it. Option 3 requires an expensive CPUID in the ISR hot path. GS-base access is a single `mov` instruction, matches the existing per-CPU pattern, and makes the isolation structural: an AP's `gs:` points to its own `cpu_local_t`, so it can never read another CPU's pending switch. A `_Static_assert` in `cpu_local_offsets.h` guards the hardcoded offset `40`.

---

## Introduce spinlocks and cpu_local before SMP (UP stubs)
*(Commit: `c48d95a`)*

**Context:**
SMP was planned but not yet implemented. The question was whether to introduce the concurrency abstractions (spinlocks, `cpu_local`, `current_task` macro) early or wait until SMP was actually enabled.

**Options considered:**
1. Wait until SMP is enabled, then retrofit spinlocks everywhere
2. Introduce spinlock and cpu_local now as UP stubs (`cli`/`sti` for locks, `&cpu_local[0]` for local), with `#error` guards on the SMP path

**Decision:**
Option 2 — UP stubs introduced immediately, with `#ifdef SMP #error` placeholders.

**Rationale:**
Retrofitting concurrency is much harder than building it in. The UP spinlock stubs (`cli`/`sti`) have zero overhead — they compile to the same instructions that were already used inline. The `current_task` macro hid the global `cpu_local[0]._current` behind an accessor, so when SMP was enabled later (commit `4c51338`), the only change was filling in the real `spin_lock` and `get_cpu_local` implementations. PMM was protected with spinlocks at this point, which paid off immediately when SMP booted.

---

## SIGKILL on COW OOM instead of kernel panic
*(Commit: `d1e110b`)*

**Context:**
When a copy-on-write page fault occurs and the PMM has no free frames, the kernel must decide what to do. The original code called `PANIC()`, halting the entire system.

**Options considered:**
1. Kernel panic — safe but takes down the whole system for one process's failure
2. Deliver `SIGKILL` to the faulting process — kills only the offending process, system continues

**Decision:**
Option 2 — deliver `SIGKILL` to the faulting task.

**Rationale:**
A kernel panic for OOM is disproportionate. The faulting process hasn't corrupted kernel state — it simply can't get a page. SIGKILL is the standard Unix response: uncatchable, immediate termination, frees the process's resources. This is critical for SMP where one AP's process shouldn't halt all CPUs.

---

## CR3-aware TLB shootdown filtering
*(Commit: `2d7d9ba`)*

**Context:**
TLB shootdown IPIs were broadcast to all CPUs. When a user-space page is unmapped, only CPUs running the same address space (same CR3) need to invalidate. Kernel-space addresses (>= `0xFFFF800000000000`) are shared across all address spaces and must always be invalidated.

**Options considered:**
1. Always invalidate on all CPUs — simple, correct, but wasteful
2. Filter by CR3 in the handler: skip `invlpg` if the shootdown targets user-space and the receiver has a different CR3
3. Track which CPUs are running which address space and only send IPIs to relevant CPUs

**Decision:**
Option 2 — CR3 check in the handler with a `target_cr3` field in the shootdown struct.

**Rationale:**
Option 1 wastes cycles on CPUs running unrelated processes. Option 3 requires maintaining a global CPU-to-address-space map with its own synchronisation — complex for marginal gain. Option 2 is simple: the initiator records its CR3, the handler compares, and skips `invlpg` on mismatch. The IPI is still broadcast (hardware doesn't support selective delivery easily), but the expensive `invlpg` is skipped. Kernel addresses bypass the filter entirely.

---

## Real-mode trampoline for AP startup via INIT-SIPI-SIPI
*(Commit: `0f66ea4`)*

**Context:**
x86 APs always start in 16-bit real mode at the physical address specified in the SIPI vector. The AP must transition through protected mode to long mode before it can execute 64-bit kernel code.

**Options considered:**
1. Real-mode trampoline at a fixed low physical address (0x70000) with inline mode transitions
2. Use ACPI/firmware wake mechanisms (platform-dependent)

**Decision:**
Option 1 — hand-written trampoline at physical 0x70000.

**Rationale:**
There is no practical alternative on x86 — the INIT-SIPI-SIPI sequence is the standard AP boot protocol. The trampoline must be below 1MB (SIPI vector is a page-aligned physical address encoded in 8 bits). The BSP patches in the PML4 address, GDT pointer, stack, entry point, and CPU ID before each SIPI. A breadcrumb word at physical 0x6000 (`0xCAFE`) confirms the AP executed the trampoline, aiding debuggability. The trampoline binary is assembled separately and embedded via `trampoline_blob.h`.

---

## `int 0x80` for syscall dispatch vs `syscall` instruction
*(Commit: `c5735e5`)*

**Context:**
x86_64 offers two syscall mechanisms: the legacy `int 0x80` software interrupt and the `syscall`/`sysret` fast-path instructions (via MSR setup).

**Options considered:**
1. `int 0x80` — reuses the existing IDT/ISR infrastructure from the 32-bit port
2. `syscall`/`sysret` — dedicated MSRs, faster (no IDT lookup), but requires separate entry/exit code and careful RSP/RIP management
3. Both — `syscall` for 64-bit processes, `int 0x80` for 32-bit compat

**Decision:**
Option 1 — `int 0x80` only, with a `registers_t` abstraction layer.

**Rationale:**
The port from 32-bit was incremental. `int 0x80` was already implemented and tested — all ISR/IRQ stubs, register save/restore, and the dispatch table worked unchanged. `syscall`/`sysret` would have required a parallel entry path, a separate stack switch mechanism (since `syscall` doesn't change RSP), and careful handling of the `RFLAGS` mask. The performance difference is negligible for this kernel's workload. A `registers_t` struct abstracts the register layout, so switching to `syscall` later is a mechanical change to the entry stub, not the dispatch logic.

---

## Deprecate legacy syscall numbers in favour of UABI numbering
*(Commit: `9bc01e8`)*

**Context:**
The 32-bit kernel used syscall numbers 0-11. The 64-bit port introduced a new User ABI (UABI) with different numbering. Both were live simultaneously, creating confusion about which numbers to use.

**Options considered:**
1. Keep both numbering schemes, dispatch based on a flag
2. Remove the legacy 0-11 dispatch entirely, migrate everything to UABI numbers
3. Alias legacy numbers to UABI numbers via a translation table

**Decision:**
Option 2 — remove legacy dispatch, force migration.

**Rationale:**
Maintaining two numbering schemes is a maintenance hazard — every new syscall needs two entries, and user-space code can accidentally use the wrong set. Removing the old dispatch forced all user-space binaries to use the UABI numbers, which are designed to be extensible and consistent across 32-bit and 64-bit modes. The migration was small (6 files, 96 lines changed) because the UABI wrappers were already in place.

---

## 32-bit ELF compatibility on x86_64 via compat GDT segments
*(Commit: `95ac969`)*

**Context:**
The 64-bit kernel needed to run existing 32-bit user-space ELF binaries. x86_64 supports 32-bit compatibility mode via specific GDT segment descriptors.

**Options considered:**
1. Drop 32-bit support entirely — only run 64-bit binaries
2. Add 32-bit compat code segments to the GDT, detect ELF class at exec, set up 32-bit IRET frames
3. Run 32-bit binaries in a full emulation layer

**Decision:**
Option 2 — compat segments in GDT with ELF class detection.

**Rationale:**
Dropping 32-bit would have invalidated the entire existing user-space. Emulation is massive overkill. The hardware supports compatibility mode natively — all that's needed is a 32-bit code segment (`UCode32`) in the GDT and adjusting the IRET frame to push 32-bit-sized values. Signal handling was extended similarly (commit `a3bbd93`) to build 32-bit trampoline frames when the target process is a compat binary.

---

## `free_internal()` split to avoid deadlock in heap allocator

**Context:**
Adding a spinlock (`heap_lock`) to `alloc()` and `free()` in `kheap.c` for SMP safety. Problem: `alloc()` calls `expand()`, which calls `free()` to return the new hole to the index. If `free()` acquires `heap_lock`, and `alloc()` already holds it, the kernel deadlocks.

**Options considered:**
1. Recursive/reentrant spinlock — allows the same core to re-acquire
2. Split `free()` into a lock-free `free_internal()` and a public `free()` that wraps with the lock
3. Release the lock before calling `expand()`, re-acquire after — introduces a window where another core can see inconsistent state

**Decision:**
Option 2 — `free_internal()` + public wrapper.

**Rationale:**
Recursive spinlocks are error-prone and mask real lock ordering bugs. Option 3 introduces a race window (the heap is in a half-expanded state between unlock and re-lock). Option 2 is clean: `free_internal()` does the actual work (mark hole, coalesce, insert into index), `free()` wraps it with `spin_lock_irqsave(&heap_lock)`. The call chain `alloc() → expand() → free_internal()` stays fully under the lock with no re-acquisition needed.

---

## Kernel-only page table locking in `mmu_map_page()` / `get_page()`

**Context:**
`get_or_alloc_table()` in `mmu.c` writes to `parent->entries[index]` without locking. Two cores extending `kernel_directory` concurrently can race on the same PML4/PDPT/PD slot, with one core's newly allocated table being silently overwritten by the other's.

**Options considered:**
1. Lock inside `get_or_alloc_table()` itself — protects individual slot writes
2. Lock at the caller level (`mmu_map_page` / `get_page`) — protects the entire 3-call sequence atomically
3. Lock unconditionally for all page directories (kernel and per-process)

**Decision:**
Option 2, with a guard `if (ctx == kernel_directory)` to only lock for the shared kernel directory.

**Rationale:**
Option 1 doesn't protect the full sequence — core A could allocate a PDPT and core B could see the PML4 entry but race on the PDPT's PD slot before A fills it. The lock must span the entire walk. Option 3 is wasteful: per-process directories are never shared across cores (each process runs on one core at a time, and `clone_page_directory` creates a private copy). Locking only `kernel_directory` avoids contention on the common case (user-space page faults) while protecting the genuinely shared structure.
