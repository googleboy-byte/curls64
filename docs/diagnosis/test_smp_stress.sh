#!/bin/bash
# Stress test: run multiple userland commands to trigger the SMP race
# Usage: bash test_smp_stress.sh <smp_count>

SMP_COUNT="${1:-4}"
SERIAL_LOG="logs/smp${SMP_COUNT}_stress.log"
mkdir -p logs
rm -f "$SERIAL_LOG"

echo "=== Stress testing with -smp $SMP_COUNT ==="

# Run QEMU in background with monitor on stdio, serial to file
timeout 45 qemu-system-x86_64 \
    -cdrom build/curls.iso \
    -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
    -boot d \
    -m 256 \
    -smp "$SMP_COUNT" \
    -nographic \
    -serial file:"$SERIAL_LOG" \
    -monitor none \
    </dev/null &
QEMU_PID=$!

# Wait for boot
sleep 12

# The kernel auto-enters the shell. Send many commands via the monitor.
# Actually, -nographic makes the console available via stdio.
# Let's use a different approach: use -chardev + sendkey via monitor.

# Since we can't easily send keystrokes, let's check the serial log
# for the auto-test output. The kernel runs tests then enters K-ABI shell.
# We need keyboard input which requires the QEMU monitor.

# Alternative: just check if the existing user's run (with -smp 4) shows the fault
# The user already confirmed it happens. Let's just verify with serial log.

sleep 20

kill $QEMU_PID 2>/dev/null
wait $QEMU_PID 2>/dev/null

echo ""
echo "=== Serial log tail (smp=$SMP_COUNT) ==="
tail -20 "$SERIAL_LOG" 2>/dev/null

echo ""
echo "=== Page fault check ==="
if grep -q "Page Fault" "$SERIAL_LOG" 2>/dev/null; then
    echo "RESULT: PAGE FAULT FOUND"
    grep "Page Fault" "$SERIAL_LOG"
else
    echo "No page fault in boot sequence (need interactive input to test userland)"
fi
