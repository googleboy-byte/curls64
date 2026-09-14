#!/bin/bash
# Test script: run with -smp 1 to verify if the COW page fault is SMP-related
# Sends commands to the kernel shell and captures output

SMP_COUNT="${1:-1}"
LOG_FILE="logs/smp${SMP_COUNT}_test.log"
mkdir -p logs

echo "=== Testing with -smp $SMP_COUNT ==="

# Run QEMU with a timeout, sending commands via stdin
timeout 30 bash -c '
sleep 8   # Wait for boot + core tests
echo "USER"
sleep 3   # Wait for sh64 to start
echo "ls bin"
sleep 3   # Wait for ls output
echo "echo USERLAND_OK"
sleep 2
echo "exit"
sleep 1
' | qemu-system-x86_64 \
    -cdrom build/curls.iso \
    -hda build/disk.img \
    -boot d \
    -m 256 \
    -smp "$SMP_COUNT" \
    -nographic \
    2>/dev/null | tee "$LOG_FILE"

echo ""
echo "=== Results (smp=$SMP_COUNT) ==="
if grep -q "Page Fault" "$LOG_FILE"; then
    echo "RESULT: PAGE FAULT DETECTED"
    grep "Page Fault" "$LOG_FILE"
elif grep -q "USERLAND_OK" "$LOG_FILE"; then
    echo "RESULT: USERLAND OK — no page fault"
else
    echo "RESULT: INCONCLUSIVE — check $LOG_FILE"
fi
