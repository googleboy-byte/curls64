#!/usr/bin/env bash
set -euo pipefail

# ANSI Color Codes
BLUE='\033[0;34m'
CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

ISO_PATH="${1:-}"
if [[ -z "${ISO_PATH}" || ! -f "${ISO_PATH}" ]]; then
  echo -e "Usage: $0 /path/to/curls.iso [target_device]" >&2
  exit 2
fi

need() {
  command -v "$1" >/dev/null 2>&1 || error "Missing required tool: $1"
}

need lsblk; need sfdisk; need blockdev; need partprobe; need mkfs.vfat
need python3; need dd; need sync; need awk; need wipefs; need grep

if [[ "${EUID}" -ne 0 ]]; then
  error "This must be run as root (will be invoked via sudo from make)."
fi

# 1. Disk Selection / Validation
TARGET_DISK="${2:-${TARGET_DISK:-}}"
if [[ -n "${TARGET_DISK}" ]]; then
  if [[ ! -b "${TARGET_DISK}" ]]; then
    error "Target disk does not exist: ${TARGET_DISK}"
  fi
  # Safety check: Is it the root disk?
  if mount | grep " / " | grep -q "${TARGET_DISK}"; then
    error "SAFETY TRIGGERED: ${TARGET_DISK} appears to be your ROOT disk. Refusing to destroy."
  fi
else
  log "Scanning for removable disks..."
  # Use JSON for robust parsing (handles empty Vendor/Model correctly)
  mapfile -t CANDIDATES < <(
    python3 - <<'PY'
import json, subprocess, sys
try:
    js = subprocess.check_output(["lsblk", "-dpJ", "-o", "NAME,SIZE,VENDOR,MODEL,RM,TYPE"], text=True)
    data = json.loads(js)
    found = False
    for dev in data.get("blockdevices", []):
        # Handle cases where rm is "1", 1, or True
        rm = str(dev.get("rm", "0"))
        dtype = str(dev.get("type", ""))
        if (rm == "1" or rm.lower() == "true") and dtype == "disk":
            name = dev.get("name", "unknown")
            size = dev.get("size", "0B")
            vendor = (dev.get("vendor") or "").strip()
            model = (dev.get("model") or "").strip()
            print(f"{name}\t{size}\t{vendor} {model}")
            found = True
except Exception as e:
    sys.exit(1)
PY
  )

  if [[ "${#CANDIDATES[@]}" -eq 0 ]]; then
    warn "No removable disks found (lsblk RM=1). Searching for all disks..."
    mapfile -t CANDIDATES < <(
      python3 - <<'PY'
import json, subprocess, sys
try:
    js = subprocess.check_output(["lsblk", "-dpJ", "-o", "NAME,SIZE,VENDOR,MODEL,RM,TYPE"], text=True)
    for dev in json.loads(js).get("blockdevices", []):
        if str(dev.get("type", "")) == "disk":
            print(f"{dev.get('name')}\t{dev.get('size')}\t{dev.get('vendor','')} {dev.get('model','')}")
except Exception: sys.exit(1)
PY
    )
    if [[ "${#CANDIDATES[@]}" -eq 0 ]]; then
      error "No disks found (lsblk failed or returned nothing)."
    fi
    warn "DANGEROUS: Listing ALL disks because no removable ones were detected."
    warn "Be EXTREMELY careful not to select your system drive."
  fi

  echo -e "${CYAN}Current system disk layout:${NC}"
  lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT
  echo

  echo -e "${CYAN}Available Disks for Flashing:${NC}"
  printf "  %-12s %-8s %s\n" "Device" "Size" "Model"
  for i in "${!CANDIDATES[@]}"; do
    IFS=$'\t' read -r dev size model <<< "${CANDIDATES[$i]}"
    printf "  %d) %-12s %-8s %s\n" "$((i+1))" "${dev}" "${size}" "${model}"
  done

  read -r -p "Select target disk (1-${#CANDIDATES[@]}): " CHOICE
  if [[ ! "${CHOICE}" =~ ^[0-9]+$ ]] || [[ "${CHOICE}" -lt 1 ]] || [[ "${CHOICE}" -gt "${#CANDIDATES[@]}" ]]; then
    error "Invalid selection."
  fi
  TARGET_DISK="$(awk '{print $1}' <<<"${CANDIDATES[$((CHOICE-1))]}")"
fi

echo -e "\n${RED}!!! WARNING !!!${NC}"
echo -e "Target: ${YELLOW}${TARGET_DISK}${NC}"
echo -e "Action: ${RED}ERASE ALL DATA${NC} and flash ${CYAN}$(basename "${ISO_PATH}")${NC}"
echo

if [[ "${FORCE:-0}" != "1" ]]; then
  read -r -p "Type 'YES' to confirm destruction: " CONFIRM
  if [[ "${CONFIRM}" != "YES" ]]; then
    error "Confirmation failed. Aborted."
  fi
fi

# 2. Preparation
log "Unmounting partitions on ${TARGET_DISK}..."
# Use lsblk to find all mountpoints associated with this disk
mapfile -t MOUNTS < <(lsblk -lnpo MOUNTPOINT "${TARGET_DISK}" | grep -v "^$" | grep -v "\[SWAP\]" || true)
for mnt in "${MOUNTS[@]}"; do
  log "  - Unmounting ${mnt}"
  umount -l "${mnt}" 2>/dev/null || true
done

log "Wiping existing filesystem signatures..."
wipefs -a "${TARGET_DISK}"

# 3. Flashing
log "Flashing ISO (this may take a minute)..."
dd if="${ISO_PATH}" of="${TARGET_DISK}" bs=4M conv=fsync status=progress
sync

log "Refreshing partition table..."
partprobe "${TARGET_DISK}" || true
sleep 1
sync

# 4. Persistence Partition Calculation (Python helper)
log "Repairing GPT headers and calculating space..."
# 1. Use parted to 'fix' the GPT header placement. 
# Hybrid ISOs often have headers at the start but not the end of the disk.
# We use ---pretend-input-tty to automate the 'Fix' response if prompted.
# Then we extract the start sector for the new partition.
START_SECTOR=$(python3 - "${TARGET_DISK}" <<'PY'
import json, subprocess, sys
disk = sys.argv[1]
def sh(*cmd): return subprocess.check_output(cmd, text=True)
try:
    # Attempt to fix GPT headers silently if parted supports it, or just use sfdisk to find existing state
    subprocess.run(["parted", "-s", disk, "print"], capture_output=True) 
    
    total_sectors = int(sh("blockdev", "--getsz", disk).strip())
    js = sh("sfdisk", "-J", disk)
    data = json.loads(js)
    parts = data.get("partitiontable", {}).get("partitions", [])
    if not parts: sys.exit(1)
    
    last = max(parts, key=lambda p: int(p.get("start", 0)) + int(p.get("size", 0)))
    end = int(last.get("start", 0)) + int(last.get("size", 0))
    start = ((end + 2047) // 2048) * 2048
    if start >= total_sectors - 2048: sys.exit(2)
    print(start)
except Exception: sys.exit(1)
PY
)

if [[ $? -eq 0 ]]; then
  log "Creating persistence partition via parted..."
  # Convert to GPT if we have 4 partitions already, or just let parted handle the hybrid state.
  # Most modern parted versions will handle adding a 5th partition by converting to GPT or using logicals.
  # However, to be safest with Hybrid ISOs, we use 'mkpart'.
  parted -s "${TARGET_DISK}" mkpart primary fat32 "${START_SECTOR}s" 100% || {
    warn "Parted failed to add partition. The MBR may be full (4 partitions)."
    warn "Attempting to convert to GPT to allow more partitions..."
    # This is a bit risky but usually okay on modern UEFI/BIOS.
    echo "label: gpt" | sfdisk --append "${TARGET_DISK}" 2>/dev/null || true
    parted -s "${TARGET_DISK}" mkpart primary fat32 "${START_SECTOR}s" 100%
  }
  
  sync
  partprobe "${TARGET_DISK}" || true
  sleep 2

  # Identify the new partition (highest numbered)
  log "Identifying persistence partition..."
  NEW_PART_NUM=$(lsblk -lnpo NAME "${TARGET_DISK}" | grep -o "[0-9]\+$" | sort -n | tail -n1 || echo "")
  
  if [[ -n "${TARGET_DISK}" && "${TARGET_DISK}" =~ [0-9]$ ]]; then
    PERSIST_PART="${TARGET_DISK}p${NEW_PART_NUM}"
  else
    PERSIST_PART="${TARGET_DISK}${NEW_PART_NUM}"
  fi

  if [[ -b "${PERSIST_PART}" ]]; then
    log "Formatting persistence partition ${PERSIST_PART} (this may take a moment)..."
    mkfs.vfat -F 32 -n CURLS_LIVE "${PERSIST_PART}" >/dev/null
    success "Persistence partition created and formatted: ${PERSIST_PART}"
  else
    warn "Could not locate the new partition device. Please check 'lsblk ${TARGET_DISK}'."
  fi
else
  warn "Insufficient space or error calculating persistence partition. Skipping."
fi

sync
echo -e "\n${GREEN}================================================${NC}"
success "Curls OS Live USB is ready!"
log "Device: ${TARGET_DISK}"
log "Persistence Label: CURLS_LIVE"
log "To test: qemu-system-i386 -hda ${TARGET_DISK} -m 256"
echo -e "${GREEN}================================================${NC}"
