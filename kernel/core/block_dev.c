#include "block_dev.h"
#include "../../libc/string.h"
#include "../../libc/mem.h"

#define MAX_BLOCK_DEVICES 16

static kabi_block_device_t devices[MAX_BLOCK_DEVICES];
static int device_count = 0;

void block_dev_init() {
    device_count = 0;
    memory_set((uint8_t*)devices, 0, sizeof(devices));
}

int block_dev_register(kabi_block_device_t dev) {
    if (device_count >= MAX_BLOCK_DEVICES) return -1;
    devices[device_count] = dev;
    int idx = device_count;
    device_count++;

    /* If it's a new physical device (not a partition), scan for partitions */
    if (!dev.is_partition) {
        block_dev_scan_partitions(idx);
    }

    return idx;
}

void block_dev_scan_partitions(int dev_id) {
    kabi_partition_t parts[4];
    int count = mbr_scan(dev_id, parts, 4);
    if (count <= 0) return;

    kabi_block_device_t *parent = &devices[dev_id];

    for (int i = 0; i < count; i++) {
        kabi_block_device_t pdev;
        memory_set((uint8_t*)&pdev, 0, sizeof(pdev));
        
        // Name: usb0 -> usb0p1
        strcpy(pdev.name, parent->name);
        int len = strlen(pdev.name);
        pdev.name[len] = 'p';
        pdev.name[len+1] = '0' + parts[i].index;
        pdev.name[len+2] = '\0';

        pdev.read_sector = parent->read_sector;
        pdev.write_sector = parent->write_sector;
        pdev.size = parts[i].sector_count;
        pdev.is_partition = 1;
        pdev.parent_dev = dev_id;
        pdev.start_lba = parts[i].start_lba;

        // Register without recursing (internal register)
        if (device_count < MAX_BLOCK_DEVICES) {
            devices[device_count] = pdev;
            device_count++;
        }
    }
}

int block_dev_get_count() {
    return device_count;
}

kabi_block_device_t* block_dev_get_by_index(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &devices[index];
}

kabi_block_device_t* block_dev_get_by_name(const char *name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}
