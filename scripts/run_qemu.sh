#!/bin/bash
# run_qemu.sh — Interactive USB device selection + QEMU launcher.
# Updated by Antigravity (Unix line endings fixed).
# For real USB drives: uses UHCI controller only (full-speed compatible).
# For virtual drives: uses EHCI controller (high-speed usb-storage emulation).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(dirname "$SCRIPT_DIR")/build"

echo "=== USB Flash Drive Selector ==="
echo ""

# Find removable USB block devices with filesystem info
DEVICES=()
LABELS=()

while IFS= read -r line; do
    dev=$(echo "$line" | awk '{print $1}')
    vendor=$(echo "$line" | awk '{print $2}')
    model=$(echo "$line" | awk '{for(i=3;i<=NF-3;i++) printf "%s ", $i; print ""}' | sed 's/ *$//')
    size=$(echo "$line" | awk '{print $(NF-2)}')
    tran=$(echo "$line" | awk '{print $(NF-1)}')
    rm_flag=$(echo "$line" | awk '{print $NF}')

    if [[ "$tran" == "usb" && "$rm_flag" == "1" ]]; then
        fs_info=""
        while IFS= read -r pline; do
            pfstype=$(echo "$pline" | awk '{print $2}')
            if [[ -n "$pfstype" ]]; then
                fs_info="$fs_info [$pfstype]"
            fi
        done < <(lsblk -npo NAME,FSTYPE "$dev" 2>/dev/null | tail -n +2)
        [[ -z "$fs_info" ]] && fs_info=" [unknown fs]"

        # Get USB bus/device for unique identification
        UDEV_INFO=$(udevadm info --query=property --name="$dev" 2>/dev/null)
        busnum=$(echo "$UDEV_INFO" | grep "^BUSNUM=" | cut -d= -f2 2>/dev/null || true)
        devnum=$(echo "$UDEV_INFO" | grep "^DEVNUM=" | cut -d= -f2 2>/dev/null || true)
        vid=$(echo "$UDEV_INFO" | grep "^ID_VENDOR_ID=" | cut -d= -f2 2>/dev/null || true)
        pid=$(echo "$UDEV_INFO" | grep "^ID_MODEL_ID=" | cut -d= -f2 2>/dev/null || true)

        # Fallback: resolve from sysfs
        if [[ -z "$busnum" || -z "$devnum" ]]; then
            syspath=$(udevadm info --query=path --name="$dev" 2>/dev/null || true)
            usbdev_path="/sys${syspath}"
            while [[ -n "$usbdev_path" && ! -f "$usbdev_path/busnum" ]]; do
                usbdev_path=$(dirname "$usbdev_path")
            done
            if [[ -f "$usbdev_path/busnum" ]]; then
                busnum=$(cat "$usbdev_path/busnum" 2>/dev/null)
                devnum=$(cat "$usbdev_path/devnum" 2>/dev/null)
            fi
        fi

        DEVICES+=("$dev|$busnum|$devnum|$vid|$pid")
        LABELS+=("$dev  $size  $vendor $model$fs_info  (bus$busnum:dev$devnum)")
    fi
done < <(lsblk -dnpo NAME,VENDOR,MODEL,SIZE,TRAN,RM 2>/dev/null | grep -v "^$")

if [[ ${#DEVICES[@]} -gt 0 ]]; then
    echo "Found USB flash drives:"
    echo ""
    for i in "${!LABELS[@]}"; do
        echo "  $((i+1))) ${LABELS[$i]}"
    done
    echo ""
fi

echo "  v) Use a 32MB virtual FAT32 flash drive (default)"
echo "  0) Boot without USB storage"
if [[ ${#DEVICES[@]} -gt 0 ]]; then
    echo ""
    echo "  Select a number to passthrough via UHCI (requires sudo)"
fi
echo ""
read -p "Choice [v]: " choice

[[ -z "$choice" ]] && choice="v"

USB_FLAGS=""
USE_SUDO=0

if [[ "$choice" == "0" ]]; then
    USB_FLAGS="-device usb-ehci,id=ehci"
    echo "Booting with EHCI controller only (no storage)"

elif [[ "$choice" == "v" || "$choice" == "V" ]]; then
    FLASH_IMG="$BUILD_DIR/flash.img"
    if [[ ! -f "$FLASH_IMG" ]]; then
        echo "Creating 32MB FAT32 virtual flash drive..."
        dd if=/dev/zero of="$FLASH_IMG" bs=1M count=32 2>/dev/null
        mkfs.vfat -F 32 "$FLASH_IMG" >/dev/null
    fi
    echo "Using virtual flash drive: $FLASH_IMG"
    USB_FLAGS="-device usb-ehci,id=ehci -drive if=none,id=usbflash,file=$FLASH_IMG,format=raw -device usb-storage,bus=ehci.0,drive=usbflash"

elif [[ "$choice" =~ ^[0-9]+$ && ${#DEVICES[@]} -gt 0 ]]; then
    idx=$((choice - 1))
    if [[ $idx -ge 0 && $idx -lt ${#DEVICES[@]} ]]; then
        IFS='|' read -r SELECTED BUSNUM DEVNUM VID PID <<< "${DEVICES[$idx]}"
        echo "Selected: $SELECTED (bus=$BUSNUM, dev=$DEVNUM, ${VID}:${PID})"

        # Unmount if needed
        MOUNT_POINTS=$(findmnt -rno TARGET "$SELECTED"* 2>/dev/null || true)
        if [[ -n "$MOUNT_POINTS" ]]; then
            echo ""
            echo "WARNING: Device is mounted:"
            echo "$MOUNT_POINTS" | while read -r mp; do echo "  $mp"; done
            read -p "Unmount and continue? [Y/n]: " confirm </dev/tty
            if [[ "$confirm" == "n" || "$confirm" == "N" ]]; then
                echo "Aborted. Using virtual image."
                FLASH_IMG="$BUILD_DIR/flash.img"
                [[ ! -f "$FLASH_IMG" ]] && dd if=/dev/zero of="$FLASH_IMG" bs=1M count=32 2>/dev/null && mkfs.vfat -F 32 "$FLASH_IMG" >/dev/null
                USB_FLAGS="-device usb-ehci,id=ehci -drive if=none,id=usbflash,file=$FLASH_IMG,format=raw -device usb-storage,bus=ehci.0,drive=usbflash"
            else
                for part in $(findmnt -rno SOURCE "$SELECTED"* 2>/dev/null); do
                    echo "Unmounting $part..."
                    sudo umount "$part" 2>/dev/null || true
                done
            fi
        fi

        if [[ -z "$USB_FLAGS" ]]; then
            # Use UHCI controller with emulated USB storage backed by real block device
            # This routes real drive data through QEMU's USB storage emulation on UHCI
            # so our kernel's UHCI driver handles full USB enumeration + SCSI commands
            echo ""
            echo "Emulating $SELECTED as USB storage on UHCI bus"
            echo "NOTE: Requires sudo for block device access"
            USB_FLAGS="-usb -drive if=none,id=usbflash,file=$SELECTED,format=raw -device usb-storage,drive=usbflash"
            USE_SUDO=1
        fi
    else
        echo "Invalid selection. Using virtual image."
        FLASH_IMG="$BUILD_DIR/flash.img"
        [[ ! -f "$FLASH_IMG" ]] && dd if=/dev/zero of="$FLASH_IMG" bs=1M count=32 2>/dev/null && mkfs.vfat -F 32 "$FLASH_IMG" >/dev/null
        USB_FLAGS="-device usb-ehci,id=ehci -drive if=none,id=usbflash,file=$FLASH_IMG,format=raw -device usb-storage,bus=ehci.0,drive=usbflash"
    fi
fi

echo ""
echo "QEMU USB flags: $USB_FLAGS"
echo "=== Launching QEMU ==="
echo ""

if [[ $USE_SUDO -eq 1 ]]; then
    exec sudo qemu-system-i386 "$@" $USB_FLAGS
else
    exec qemu-system-i386 "$@" $USB_FLAGS
fi
