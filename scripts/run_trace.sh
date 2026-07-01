#!/bin/bash
# scripts/run_trace.sh - Runs QEMU and GDB tracing pipeline
set -e

# Make sure logs directory exists
mkdir -p logs

echo "[TRACE RUNNER] Starting QEMU in background with GDB stub..."
# Start QEMU with SMP=4, frozen on boot (-S), listening to GDB on 1234 (-s)
setsid qemu-system-x86_64 -cdrom build/curls.iso -hda build/disk.img -boot d -m 256 -smp 4 -nographic -s -S > logs/qemu-trace-qemu.log 2>&1 < /dev/null &
QEMU_PID=$!

# Ensure QEMU is killed when this script exits
trap "echo '[TRACE RUNNER] Cleaning up QEMU (PID: $QEMU_PID)...'; kill $QEMU_PID 2>/dev/null || true" EXIT

# Wait for GDB stub port to open
sleep 5

gdb -ex "target remote localhost:1234" -ex "symbol-file build/kernel64_verify.elf" -x scripts/gdb_trace.py

echo "[TRACE RUNNER] Complete."
