#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "tusb.h"
#include "ram_disk.h"

static bool ejected = false;

int32_t tud_msc_read10_cb
(
    uint8_t lun,
    uint32_t lba,
    uint32_t offset,
    void *buffer,
    uint32_t bufsize
)
{
    if (lba >= RAM_DISK_SECTOR_COUNT ||
        offset >= RAM_DISK_SECTOR_SIZE) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    // Общий адрес в RAM-диске. Буфер g_ram_disk непрерывный,
    // поэтому передача может продолжаться через границу сектора.
    const uint64_t byte_pos =
        (uint64_t)lba * RAM_DISK_SECTOR_SIZE + offset;

    const uint64_t disk_size =
        (uint64_t)RAM_DISK_SECTOR_COUNT * RAM_DISK_SECTOR_SIZE;

    if (byte_pos > disk_size || bufsize > disk_size - byte_pos) 
    {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    memcpy(buffer, &g_ram_disk[byte_pos], bufsize);

    return (int32_t)bufsize;
}
int32_t tud_msc_write10_cb
(
    uint8_t lun,
    uint32_t lba,
    uint32_t offset,
    uint8_t *buffer,
    uint32_t bufsize
)
{
    if (lba >= RAM_DISK_SECTOR_COUNT ||
        offset >= RAM_DISK_SECTOR_SIZE) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    const uint64_t byte_pos =
        (uint64_t)lba * RAM_DISK_SECTOR_SIZE + offset;

    const uint64_t disk_size =
        (uint64_t)RAM_DISK_SECTOR_COUNT * RAM_DISK_SECTOR_SIZE;

    if (byte_pos > disk_size || bufsize > disk_size - byte_pos) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    memcpy(&g_ram_disk[byte_pos], buffer, bufsize);

    return (int32_t)bufsize;
}

void tud_msc_capacity_cb
(
    uint8_t lun,
    uint32_t *block_count,
    uint16_t *block_size
)
{
    (void)lun;

    *block_count = RAM_DISK_SECTOR_COUNT;
    *block_size = RAM_DISK_SECTOR_SIZE;
}

// прочие callbacks
void tud_msc_inquiry_cb
(
    uint8_t lun,
    uint8_t vendor_id[8],
    uint8_t product_id[16],
    uint8_t product_rev[4]
)
{
    (void)lun;

    const char vid[] = "STM32";
    const char pid[] = "RAM Disk";
    const char rev[] = "1.0";

    memcpy(vendor_id, vid, sizeof(vid) - 1);
    memcpy(product_id, pid, sizeof(pid) - 1);
    memcpy(product_rev, rev, sizeof(rev) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (ejected) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }

    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

int32_t tud_msc_scsi_cb
(
    uint8_t lun,               
    uint8_t const scsi_cmd[16],              
    void *buffer,          
    uint16_t bufsize
)
{
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;

    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

bool tud_msc_start_stop_cb
(
    uint8_t lun,
    uint8_t power_condition,
    bool start,
    bool load_eject
)
{
    (void)lun;
    (void)power_condition;

    if (load_eject) {
        // Host попросил извлечь диск.
        if (!start) {
            ejected = true;
        }
        // Host снова загрузил диск.
        else {
            ejected = false;
        }
    }

    return true;
}