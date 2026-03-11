#!/bin/bash
# usb_select.sh — Detect connected USB mass storage devices and let the user
# pick one to pass through to QEMU via USB host passthrough.
#
# Outputs the QEMU flags to stdout. If no device is selected, outputs flags
# for no USB device (EHCI controller only).

set -e

echo "=== USB Flash Drive Detector ===" >&2
echo "" >&2

# Find removable USB block devices
DEVICES=()
LABELS=()

while IFS= read -r line; do
    # Parse lsblk output: NAME VENDOR MODEL SIZE TRAN RM
    dev=$(echo "$line" | awk '{print $1}')
    vendor=$(echo "$line" | awk '{print $2}')
    model=$(echo "$line" | awk '{for(i=3;i<=NF-3;i++) printf "%s ", $i; print ""}' | sed 's/ *$//')
    size=$(echo "$line" | awk '{print $(NF-2)}')
    tran=$(echo "$line" | awk '{print $(NF-1)}')
    rm_flag=$(echo "$line" | awk '{print $NF}')

    # Only USB removable devices
    if [[ "$tran" == "usb" && "$rm_flag" == "1" ]]; then
        DEVICES+=("$dev")
        LABELS+=("$dev  $size  $vendor $model")
    fi
done < <(lsblk -dnpo NAME,VENDOR,MODEL,SIZE,TRAN,RM 2>/dev/null | grep -v "^$")

if [[ ${#DEVICES[@]} -eq 0 ]]; then
    echo "No USB flash drives detected." >&2
    echo "" >&2
    echo "Options:" >&2
    echo "  0) Boot without USB device" >&2
    echo "  1) Use a 32MB virtual FAT32 image instead" >&2
    echo "" >&2
    read -p "Choice [0]: " choice >&2

    if [[ "$choice" == "1" ]]; then
        # Create virtual flash image if it doesn't exist
        FLASH_IMG="build/flash.img"
        if [[ ! -f "$FLASH_IMG" ]]; then
            dd if=/dev/zero of="$FLASH_IMG" bs=1M count=32 2>/dev/null
            mkfs.vfat -F 32 "$FLASH_IMG" >/dev/null
            echo "Created 32MB virtual FAT32 image" >&2
        fi
        echo "-device usb-ehci,id=ehci -drive if=none,id=usbflash,file=$FLASH_IMG,format=raw -device usb-storage,bus=ehci.0,drive=usbflash"
    else
        echo "-device usb-ehci,id=ehci"
    fi
    exit 0
fi

echo "Found USB flash drives:" >&2
echo "" >&2
for i in "${!LABELS[@]}"; do
    echo "  $((i+1))) ${LABELS[$i]}" >&2
done
echo "" >&2
echo "  0) Boot without USB device" >&2
echo "  v) Use a 32MB virtual FAT32 image" >&2
echo "" >&2
read -p "Select device to pass to QEMU [0]: " choice >&2

if [[ "$choice" == "v" || "$choice" == "V" ]]; then
    FLASH_IMG="build/flash.img"
    if [[ ! -f "$FLASH_IMG" ]]; then
        dd if=/dev/zero of="$FLASH_IMG" bs=1M count=32 2>/dev/null
        mkfs.vfat -F 32 "$FLASH_IMG" >/dev/null
        echo "Created 32MB virtual FAT32 image" >&2
    fi
    echo "-device usb-ehci,id=ehci -drive if=none,id=usbflash,file=$FLASH_IMG,format=raw -device usb-storage,bus=ehci.0,drive=usbflash"
    exit 0
fi

if [[ -z "$choice" || "$choice" == "0" ]]; then
    echo "-device usb-ehci,id=ehci"
    exit 0
fi

# Validate selection
idx=$((choice - 1))
if [[ $idx -lt 0 || $idx -ge ${#DEVICES[@]} ]]; then
    echo "Invalid selection, booting without USB." >&2
    echo "-device usb-ehci,id=ehci"
    exit 0
fi

SELECTED="${DEVICES[$idx]}"
echo "" >&2
echo "Selected: $SELECTED" >&2

# Get USB vendor:product ID from udevadm
UDEV_INFO=$(udevadm info --query=property --name="$SELECTED" 2>/dev/null)
USB_VID=$(echo "$UDEV_INFO" | grep "^ID_VENDOR_ID=" | cut -d= -f2)
USB_PID=$(echo "$UDEV_INFO" | grep "^ID_MODEL_ID=" | cut -d= -f2)

if [[ -z "$USB_VID" || -z "$USB_PID" ]]; then
    echo "ERROR: Could not determine USB vendor/product ID." >&2
    echo "-device usb-ehci,id=ehci"
    exit 1
fi

echo "USB ID: ${USB_VID}:${USB_PID}" >&2

# Check if device is mounted and warn
MOUNT_POINTS=$(findmnt -rno TARGET "$SELECTED"* 2>/dev/null || true)
if [[ -n "$MOUNT_POINTS" ]]; then
    echo "" >&2
    echo "WARNING: Device has mounted partitions:" >&2
    echo "$MOUNT_POINTS" | while read -r mp; do echo "  $mp" >&2; done
    echo "" >&2
    read -p "Unmount and continue? [y/N]: " confirm >&2
    if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
        echo "Aborted." >&2
        echo "-device usb-ehci,id=ehci"
        exit 0
    fi
    # Unmount all partitions
    for part in $(findmnt -rno SOURCE "$SELECTED"* 2>/dev/null); do
        echo "Unmounting $part..." >&2
        sudo umount "$part" 2>/dev/null || true
    done
fi

echo "" >&2
echo "Passing USB device ${USB_VID}:${USB_PID} to QEMU (requires sudo)" >&2
echo "-device usb-ehci,id=ehci -device usb-host,bus=ehci.0,vendorid=0x${USB_VID},productid=0x${USB_PID}"
