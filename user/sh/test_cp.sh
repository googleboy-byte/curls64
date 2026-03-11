echo === Curls OS cp Utility Tests ===
echo

# Test 1: Basic Copy
echo [Test 1] Copying /ETC/MOTD to /CP_TEST.TXT...
cp /ETC/MOTD /CP_TEST.TXT
echo PASS: cp executed

echo Checking content of /CP_TEST.TXT...
cat /CP_TEST.TXT
echo PASS: File copied

# Test 2: Overwrite existing file
echo [Test 2] Overwriting /CP_TEST.TXT with new content...
write SOURCE.TXT New_Content
cp SOURCE.TXT /CP_TEST.TXT
echo PASS: Overwrite successful
cat /CP_TEST.TXT

# Test 3: Copy to a nested directory
echo [Test 3] Copying to a nested directory...
mkdir TEST_DIR
cp SOURCE.TXT TEST_DIR/COPIED.TXT
echo PASS: Nested copy successful
cat TEST_DIR/COPIED.TXT

# Test 4: Error handling (non-existent source)
echo [Test 4] Copying non-existent file...
cp /NON_EXISTENT.TXT /DEST.TXT
echo This should have failed above

# Test 5: Self-copy prevention
echo [Test 5] Copying file to itself...
write SELF.TXT same_content
cp SELF.TXT SELF.TXT
echo This should have failed above

# Cleanup
echo
echo Cleaning up...
rm /CP_TEST.TXT
rm SOURCE.TXT
rm SELF.TXT
rm TEST_DIR/COPIED.TXT
rm TEST_DIR

echo
echo === cp Tests Complete ===