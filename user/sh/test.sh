echo === Curls OS Automated Utility Tests ===
echo

# ════════════════════════════════════════════
#  SECTION 1: Basic Utility Tests
# ════════════════════════════════════════════

# Test 1: pwd
echo [Test 1] Checking pwd...
CUR_DIR=$(pwd)
if $CUR_DIR == /
  echo PASS: pwd is /
else
  echo FAIL: pwd is $CUR_DIR
endif

# Test 2: File Writing and Reading
echo [Test 2] Checking write and cat...
write UTILITY_TEST.TXT Hello_CurlsOS
FILE_VAL=$(cat UTILITY_TEST.TXT)
if $FILE_VAL == Hello_CurlsOS
  echo PASS: File content matches
else
  echo FAIL: File content is $FILE_VAL
endif

# Test 3: File Appending
echo [Test 3] Checking write_a (append)...
write_a UTILITY_TEST.TXT _v2
APPEND_VAL=$(cat UTILITY_TEST.TXT)
if $APPEND_VAL == Hello_CurlsOS_v2
  echo PASS: Append works
else
  echo FAIL: Append result is $APPEND_VAL
endif

# Test 4: Directory and Path Resolution
echo [Test 4] Checking mkdir and path access...
mkdir TEST_DIR
touch TEST_DIR/NESTED.TXT
write TEST_DIR/NESTED.TXT NestedData
NESTED_VAL=$(cat TEST_DIR/NESTED.TXT)
if $NESTED_VAL == NestedData
  echo PASS: Nested file access works
else
  echo FAIL: Nested data is $NESTED_VAL
endif

# Test 6: rm
echo [Test 6] Checking rm...
write RM_ME.TXT data
rm RM_ME.TXT
VAL=$(cat RM_ME.TXT)
# Check for correct error message from ls/cat when file missing
if $VAL == cat: RM_ME.TXT: No such file or directory
  echo PASS: rm successfully deleted the file
else
  echo FAIL: rm failed, cat result: $VAL
endif

# Test 7: For loop iteration over utilities
echo [Test 7] Iterating over common utilities...
for util in ls ps help
do
  echo Checking $util existence...
  $util > /dev/null
  if $? == 0
    echo PASS: $util exists
  else
    echo FAIL: $util not found
  endif
done

# ════════════════════════════════════════════
#  SECTION 2: UABI Validation Tests
#  Tests valid AND invalid inputs to prove
#  the validation layer catches bad syscalls
# ════════════════════════════════════════════

echo
echo === UABI Validation Unit Tests ===
echo

# ── UABI_OPEN (20): Valid input ──
echo [V-01] UABI_OPEN valid path...
MOTD=$(cat /ETC/MOTD)
if $MOTD == Welcome to Curls OS!
  echo PASS: open+read valid path works
else
  echo FAIL: unexpected MOTD content
endif

# ── UABI_OPEN (20): Invalid input - non-existent file ──
echo [V-02] UABI_OPEN invalid path...
BAD=$(cat /NONEXISTENT_FILE_XYZ.TXT)
if $BAD == cat: /NONEXISTENT_FILE_XYZ.TXT: No such file or directory
  echo PASS: open rejects non-existent path
else
  echo FAIL: unexpected result: $BAD
endif

# ── UABI_GETCWD (25): Valid input ──
echo [V-03] UABI_GETCWD valid...
CWD=$(pwd)
if $CWD == /
  echo PASS: getcwd returns root
else
  echo FAIL: getcwd returned $CWD
endif

# ── UABI_CHDIR (26): Valid input ──
echo [V-04] UABI_CHDIR valid directory...
cd /BIN
AFTER=$(pwd)
if $AFTER == /BIN
  echo PASS: chdir to /BIN works
else
  echo FAIL: cwd after cd is $AFTER
endif
cd /

# ── UABI_CHDIR (26): Invalid input - non-existent dir ──
echo [V-05] UABI_CHDIR invalid directory...
cd /THIS_DIR_DOES_NOT_EXIST
STILL=$(pwd)
if $STILL == /
  echo PASS: chdir rejects bad path, cwd unchanged
else
  echo FAIL: cwd changed to $STILL after bad cd
endif

# ── UABI_READDIR (24): Valid input ──
echo [V-06] UABI_READDIR valid directory...
ls /BIN > /dev/null
if $? == 0
  echo PASS: readdir /BIN succeeded
else
  echo FAIL: readdir /BIN failed
endif

# ── UABI_READDIR (24): Invalid input - non-existent dir ──
echo [V-07] UABI_READDIR non-existent directory...
LS_BAD=$(ls /FAKE_DIR_999)
# Corrected expected error message from ls.c
if $LS_BAD == ls: cannot access '/FAKE_DIR_999': No such file or directory
  echo PASS: readdir rejects bad path
else
  echo FAIL: unexpected result: $LS_BAD
endif

# ── UABI_MKDIR (52): Valid input ──
echo [V-08] UABI_MKDIR valid...
mkdir V_TEST_DIR
if $? == 0
  echo PASS: mkdir returned success
else
  echo FAIL: mkdir returned error
endif

# ── UABI_WRITE (22) + UABI_READ (21): Valid round-trip ──
echo [V-09] UABI_WRITE+READ valid round-trip...
write V_TEST_DIR/VFILE.TXT validation_ok
RD=$(cat V_TEST_DIR/VFILE.TXT)
if $RD == validation_ok
  echo PASS: write+read round-trip verified
else
  echo FAIL: read returned $RD
endif

# ── UABI_UNLINK (53): Valid input ──
echo [V-10] UABI_UNLINK valid file...
write V_DEL.TXT delete_me
rm V_DEL.TXT
DEL_CHECK=$(cat V_DEL.TXT)
if $DEL_CHECK == cat: V_DEL.TXT: No such file or directory
  echo PASS: unlink removed file
else
  echo FAIL: file still exists: $DEL_CHECK
endif

# ── UABI_GETPID (34): Valid ──
echo [V-11] UABI_GETPID valid...
# ps internally uses getpid; test by running it
ps > /dev/null
if $? == 0
  echo PASS: getpid works (tested via ps)
else
  echo FAIL: ps failed
endif

# ── UABI_FORK (30) + UABI_WAIT (33): Valid ──
echo [V-12] UABI_FORK+WAIT valid...
echo hello > /dev/null
if $? == 0
  echo PASS: fork+wait cycle completed
else
  echo FAIL: fork or wait failed
endif

# ── UABI_PIPE (35) + UABI_DUP2 (36): Valid via pipeline ──
echo [V-13] UABI_PIPE+DUP2 valid via pipeline...
echo hello | cat > /dev/null
if $? == 0
  echo PASS: pipe+dup2 works
else
  echo FAIL: pipe failed
endif

# ── UABI_SLEEP (51): Valid input ──
echo [V-14] UABI_SLEEP valid (50ms)...
sleep 50
if $? == 0
  echo PASS: sleep returned success
else
  echo FAIL: sleep failed
endif

# ── UABI_PRINT (40): Valid input ──
echo [V-15] UABI_PRINT valid...
echo Validation_layer_active
if $? == 0
  echo PASS: print works
else
  echo FAIL: print failed
endif

# ── UABI_CLEAR (42): Valid ──
echo [V-16] UABI_CLEAR valid...
clear > /dev/null
if $? == 0
  echo PASS: clear call survived
else
  echo FAIL: clear failed
endif

# ── UABI_PS (45): Valid ──
echo [V-17] UABI_PS valid...
ps > /dev/null
if $? == 0
  echo PASS: ps syscall reached
else
  echo FAIL: ps failed
endif

# ── UABI_EXEC (31): Invalid - non-existent binary ──
echo [V-18] UABI_EXEC invalid binary...
# Note: external binary execution will set $? = 127 if not found
/BIN/NONEXISTENT_BY_VALIDATION.ELF > /dev/null
if $? == 127
  echo PASS: exec of bad binary handled gracefully (127)
else
  echo FAIL: unexpected result: $?
endif

# Cleanup
echo
echo Cleaning up...
rm UTILITY_TEST.TXT
rm V_TEST_DIR/VFILE.TXT
rm V_TEST_DIR
rm TEST_DIR/NESTED.TXT
rm TEST_DIR

echo
echo === All Tests Complete ===
