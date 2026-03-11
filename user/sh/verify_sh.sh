echo === Curls OS Shell Feature Verification ===
echo

echo [1] Variable Assignment and Expansion
VAR1=Hello
VAR2=World
echo VAR1 is $VAR1
echo VAR2 is $VAR2
if $VAR1 == Hello
  echo PASS: VAR1 matches
else
  echo FAIL: VAR1 mismatch
endif

echo [2] Output Capture
OS_NAME=$(echo CurlsOS)
echo OS Name: $OS_NAME
if $OS_NAME == CurlsOS
  echo PASS: Output capture works
else
  echo FAIL: Output capture failed ($OS_NAME)
endif

echo [3] Nested Control Flow
OUTER=true
INNER=true
if $OUTER == true
  echo Outer IF: true
  if $INNER == true
    echo PASS: Nested Inner IF: true
  else
    echo FAIL: Nested Inner IF: false
  endif
else
  echo FAIL: Outer IF: false
endif

echo [4] Complex Variable Expansion
COMBINED=$VAR1-$VAR2
echo Combined: $COMBINED
if $COMBINED == Hello-World
  echo PASS: Complex expansion works
else
  echo FAIL: Complex expansion failed ($COMBINED)
endif

echo
echo === All Tests Complete ===
