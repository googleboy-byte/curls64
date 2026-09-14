#!/bin/bash
# Test script: run with configurable -smp count, capture serial output
# Usage: bash test_smp_verify.sh <smp_count>

SMP_COUNT="${1:-1}"
LOG_FILE="logs/smp${SMP_COUNT}_verify.log"
SERIAL_LOG="logs/smp${SMP_COUNT}_serial.log"
mkdir -p logs

echo "=== Testing with -smp $SMP_COUNT ==="

# Create input pipe
INPUT_PIPE=$(mktemp -u)
mkfifo "$INPUT_PIPE"

# Start QEMU in background
qemu-system-x86_64 \
    -cdrom build/curls.iso \
    -hda build/disk.img \
    -boot d \
    -m 256 \
    -smp "$SMP_COUNT" \
    -nographic \
    -serial file:"$SERIAL_LOG" \
    < "$INPUT_PIPE" &
QEMU_PID=$!

# Feed commands through the pipe
(
    sleep 10   # Wait for boot + core tests + module tests
    echo "USER"
    sleep 4   # Wait for sh64 to start
    echo "ls bin"
    sleep 4   # Wait for ls output
    echo "echo USERLAND_OK"
    sleep 3
) > "$INPUT_PIPE" &

# Wait for enough time then kill QEMU
sleep 25
kill $QEMU_PID 2>/dev/null
wait $QEMU_PID 2>/dev/null
rm -f "$INPUT_PIPE"

echo ""
echo "=== Serial output (smp=$SMP_COUNT) ==="
echo "--- tail of serial log ---"
tail -30 "$SERIAL_LOG" 2>/dev/null || echo "(no serial log)"

echo ""
echo "=== Verdict ==="
if grep -q "Page Fault" "$SERIAL_LOG" 2>/dev/null; then
    echo "RESULT: PAGE FAULT DETECTED (smp=$SMP_COUNT)"
    grep "Page Fault" "$SERIAL_LOG"
elif grep -q "USERLAND_OK" "$SERIAL_LOG" 2>/dev/null; then
    echo "RESULT: USERLAND OK — no page fault (smp=$SMP_COUNT)"
elif grep -q "ls bin" "$SERIAL_LOG" 2>/dev/null; then
    echo "RESULT: ls bin was sent but no USERLAND_OK — check log"
else
    echo "RESULT: INCONCLUSIVE — check $SERIAL_LOG"
fi
