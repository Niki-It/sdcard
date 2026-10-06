#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "SEGGER_RTT.h"

#include "tusb.h"
#include <sdio/sdio.h>

extern sd_card_info_t card_info;
static bool ejected = false;
#define MSC_SECTOR_SIZE 512U

static bool msc_range_valid
(
    uint32_t lba,
    uint32_t offset,
    uint32_t bufsize
)
{
    if (offset >= MSC_SECTOR_SIZE) {
        return false;
    }

    const uint64_t capacity =
        (uint64_t)card_info.block_count * MSC_SECTOR_SIZE;

    const uint64_t byte_pos =
        (uint64_t)lba * MSC_SECTOR_SIZE + offset;

    return byte_pos <= capacity &&
           bufsize <= capacity - byte_pos;
}

int32_t tud_msc_read10_cb(
    uint8_t lun,
    uint32_t lba,
    uint32_t offset,
    void *buffer,
    uint32_t bufsize)
{
    uint8_t sector_buf[MSC_SECTOR_SIZE];
    uint8_t *dst = buffer;
    uint32_t remaining = bufsize;

    if (buffer == NULL || bufsize == 0 ||
        card_info.block_size != MSC_SECTOR_SIZE ||
        !msc_range_valid(lba, offset, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    /*
     * 1. Частичный первый сектор.
     * Если offset == 0, этот этап пропускаем.
     */
    if (offset != 0) {
        uint32_t size = MSC_SECTOR_SIZE - offset;

        if (size > remaining) {
            size = remaining;
        }

        if (sd_read_blocks(&card_info, lba,
                           sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(dst, &sector_buf[offset], size);

        dst += size;
        remaining -= size;
        lba++;
    }

    /*
     * 2. Полные секторы.
     * Читаем сразу пачкой в буфер TinyUSB.
     */
    uint32_t full_blocks = remaining / MSC_SECTOR_SIZE;

    if (full_blocks > 0) {
        if (sd_read_blocks(&card_info, lba,
                           dst, full_blocks) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        uint32_t size = full_blocks * MSC_SECTOR_SIZE;

        dst += size;
        remaining -= size;
        lba += full_blocks;
    }

    /*
     * 3. Частичный последний сектор.
     */
    if (remaining > 0) {
        if (sd_read_blocks(&card_info, lba,
                           sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(dst, sector_buf, remaining);
        dst += remaining;
    }

    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(
    uint8_t lun,
    uint32_t lba,
    uint32_t offset,
    uint8_t *buffer,
    uint32_t bufsize)
{
    uint8_t sector_buf[MSC_SECTOR_SIZE];
    const uint8_t *src = buffer;
    uint32_t remaining = bufsize;

    if (buffer == NULL || bufsize == 0 ||
        card_info.block_size != MSC_SECTOR_SIZE ||
        !msc_range_valid(lba, offset, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    /*
     * 1. Частичный первый сектор.
     * Если offset == 0, этот этап пропускаем.
     */
    if (offset != 0) {
        uint32_t size = MSC_SECTOR_SIZE - offset;

        if (size > remaining) {
            size = remaining;
        }

        if (sd_read_blocks(&card_info, lba,
                           sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(&sector_buf[offset], src, size);

        if (sd_write_blocks(&card_info, lba,
                            sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x02);
            return -1;
        }

        src += size;
        remaining -= size;
        lba++;
    }

    /*
     * 2. Полные секторы.
     * Записываем сразу пачкой из буфера TinyUSB.
     */
    uint32_t full_blocks = remaining / MSC_SECTOR_SIZE;

    if (full_blocks > 0) {
        if (sd_write_blocks(&card_info, lba,
                            src, full_blocks) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x02);
            return -1;
        }

        uint32_t size = full_blocks * MSC_SECTOR_SIZE;

        src += size;
        remaining -= size;
        lba += full_blocks;
    }

    /*
     * 3. Частичный последний сектор.
     */
    if (remaining > 0) {
        if (sd_read_blocks(&card_info, lba,
                           sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(sector_buf, src, remaining);

        if (sd_write_blocks(&card_info, lba,
                            sector_buf, 1) != SD_OK) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x02);
            return -1;
        }
    }

    return (int32_t)bufsize;
}

void tud_msc_capacity_cb(
    uint8_t lun,
    uint32_t *block_count,
    uint16_t *block_size)
{
    (void)lun;

    *block_count = card_info.block_count;
    *block_size = card_info.block_size;
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
    const char pid[] = "SD CARD";
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

//extern uint32_t reconnect_timer;
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
        ejected = !start;
    }

    return true;
}

void tud_mount_cb(void)
{
    ejected = false;
}

void tud_umount_cb(void)
{
    // ничего
}