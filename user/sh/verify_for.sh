echo === For Loop Verification ===
echo

echo [1] Simple iteration
for i in A B C
do
  echo i is $i
done

echo [2] Nested If in For
for x in 1 2 3
do
  if $x == 2
    echo Found TWO
  else
    echo x is $x
  endif
done

echo [3] Variable in list
LIST="X Y"
for item in $LIST Z
do
  echo item: $item
done

echo
echo === Verification Complete ===
