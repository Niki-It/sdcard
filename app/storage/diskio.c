#include "diskio.h"

#include <string.h>
#include <sdio/sdio.h>


static sd_card_info_t *sd_card = NULL;
static DSTATUS sd_disk_status = STA_NOINIT;

void diskio_set_sd_card(sd_card_info_t *info)
{
    sd_card = info;

    if (sd_card != NULL) {
        sd_disk_status = 0;
    } else {
        sd_disk_status = STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0 || sd_card == NULL) {
        return STA_NOINIT;
    }

    if (sd_card->block_size != 512U ||
        sd_card->block_count == 0U) {
        sd_disk_status = STA_NOINIT;
        return sd_disk_status;
    }

    sd_disk_status = 0;
    return sd_disk_status;
}


DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0 || sd_card == NULL) {
        return STA_NOINIT;
    }

    return sd_disk_status;
}


DRESULT disk_read(
    BYTE pdrv,
    BYTE *buff,
    LBA_t sector,
    UINT count
)
{
    if (pdrv != 0 || buff == NULL || count == 0) {
        return RES_PARERR;
    }

    if (sd_card == NULL || (sd_disk_status & STA_NOINIT)) {
        return RES_NOTRDY;
    }

    if (sector >= sd_card->block_count ||
        count > sd_card->block_count - sector) {
        return RES_PARERR;
    }

    sd_status_t status = sd_read_blocks(
        sd_card,
        (uint32_t)sector,
        buff,
        (uint32_t)count
    );

    if (status != SD_OK) {
        return RES_ERROR;
    }

    return RES_OK;
}


DRESULT disk_write(
    BYTE pdrv,
    const BYTE *buff,
    LBA_t sector,
    UINT count
)
{
    if (pdrv != 0 || buff == NULL || count == 0) {
        return RES_PARERR;
    }

    if (sd_card == NULL || (sd_disk_status & STA_NOINIT)) {
        return RES_NOTRDY;
    }

    if (sector >= sd_card->block_count ||
        count > sd_card->block_count - sector) {
        return RES_PARERR;
    }

    sd_status_t status = sd_write_blocks(
        sd_card,
        (uint32_t)sector,
        buff,
        (uint32_t)count
    );

    if (status != SD_OK) {
        return RES_ERROR;
    }

    return RES_OK;
}


DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) {
        return RES_PARERR;
    }

    if (sd_card == NULL || (sd_disk_status & STA_NOINIT)) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        /*
         * Если sd_write_blocks() возвращается только после
         * завершения записи, дополнительная синхронизация
         * здесь не нужна.
         *
         * Если драйвер использует DMA или отложенную запись,
         * здесь нужно дождаться завершения операций.
         */
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }

        *(LBA_t *)buff = (LBA_t)sd_card->block_count;
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }

        *(WORD *)buff = (WORD)sd_card->block_size;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }

        /*
         * Пока размер erase-блока из SD API не предоставлен.
         * 1 сектор — безопасная базовая подсказка для FatFs.
         */
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}