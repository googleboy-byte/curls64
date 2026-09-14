#!/bin/bash
# Final verification script: pipes commands directly into QEMU -nographic
# Uses stdin piping which proved reliable in the first test
# Usage: bash test_final.sh <smp_count>

SMP_COUNT="${1:-4}"
LOG="logs/final_smp${SMP_COUNT}.log"
mkdir -p logs

echo "=== FINAL TEST: -smp $SMP_COUNT ==="

# Pipe many commands — the race is non-deterministic so we need several fork+exec cycles
timeout 40 bash -c '
sleep 10
echo "ls"
sleep 2
echo "ls bin"
sleep 2
echo "echo TEST1_OK"
sleep 2
echo "ls etc"
sleep 2
echo "echo TEST2_OK"
sleep 2
echo "cat /etc/motd"
sleep 2
echo "echo TEST3_OK"
sleep 2
echo "ps"
sleep 2
echo "echo ALL_TESTS_OK"
sleep 2
' | qemu-system-x86_64 \
    -cdrom build/curls.iso \
    -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
    -boot d \
    -m 256 \
    -smp "$SMP_COUNT" \
    -nographic \
    2>/dev/null | tee "$LOG"

echo ""
echo "=== VERDICT (smp=$SMP_COUNT) ==="
PF_COUNT=$(grep -c "Page Fault" "$LOG" 2>/dev/null || echo 0)
if [ "$PF_COUNT" -gt 0 ]; then
    echo "RESULT: PAGE FAULT DETECTED ($PF_COUNT occurrences)"
    grep "Page Fault" "$LOG"
elif grep -q "ALL_TESTS_OK" "$LOG"; then
    echo "RESULT: ALL TESTS PASSED — no page fault"
elif grep -q "TEST3_OK" "$LOG"; then
    echo "RESULT: MOSTLY PASSED (TEST3_OK seen, no ALL_TESTS_OK)"
elif grep -q "TEST1_OK" "$LOG"; then
    echo "RESULT: PARTIAL — TEST1_OK seen but later tests missing"
else
    echo "RESULT: INCONCLUSIVE — check $LOG"
fi
