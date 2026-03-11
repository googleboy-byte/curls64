echo ===========================================
echo   Curls OS USB Partition Automated Test
echo ===========================================
echo
echo WARNING: This test will search for usb0 partitions,
echo MOUNT them, and CLEAR all existing data by writing
echo unique test signatures to each.
echo
echo Press ENTER to continue or Ctrl+C to abort...
read _

echo Searching for partitions on usb0...

# We try to mount standard partition names
# Partition 1
echo [ usb0p1 ] Attempting to mount...
mount usb0p1 /mnt
if $? == 0
  echo Successfully mounted usb0p1 to /mnt
  echo Writing unique signature...
  write /mnt/USB_TEST.TXT USB0_PARTITION_1_VERIFIED
  SIG=$(cat /mnt/USB_TEST.TXT)
  echo Verification signature: $SIG
  echo Unmounting...
  umount /mnt
else
  echo Partition usb0p1 not found or mount failed.
endif

echo

# Partition 2
echo [ usb0p2 ] Attempting to mount...
mount usb0p2 /mnt
if $? == 0
  echo Successfully mounted usb0p2 to /mnt
  echo Writing unique signature...
  write /mnt/USB_TEST.TXT USB0_PARTITION_2_VERIFIED
  SIG=$(cat /mnt/USB_TEST.TXT)
  echo Verification signature: $SIG
  echo Unmounting...
  umount /mnt
else
  echo Partition usb0p2 not found or mount failed.
endif

echo
echo === USB Test Complete ===
echo Note: If partitions were not found, ensure they are 
echo formatted as FAT32 and detectable in 'devs' output.
