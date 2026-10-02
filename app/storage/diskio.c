#include "diskio.h"
#include "ram_disk.h"

#include <string.h>

static DSTATUS ram_disk_status = STA_NOINIT;


DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) {
        return STA_NOINIT;
    }

    /*
     * Для RAM-диска отдельная процедура инициализации
     * не требуется: память уже доступна.
     */
    ram_disk_status = RES_OK;

    return ram_disk_status;
}


DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) {
        return STA_NOINIT;
    }

    return ram_disk_status;
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

    if (ram_disk_status & STA_NOINIT) {
        return RES_NOTRDY;
    }

    /*
     * Проверяем границы без сложения sector + count,
     * чтобы избежать переполнения.
     */
    if (sector >= RAM_DISK_SECTOR_COUNT ||
        count > RAM_DISK_SECTOR_COUNT - sector) {
        return RES_PARERR;
    }

    const uint32_t offset =
        (uint32_t)sector * RAM_DISK_SECTOR_SIZE;

    const uint32_t size =
        (uint32_t)count * RAM_DISK_SECTOR_SIZE;

    memcpy(buff, &g_ram_disk[offset], size);

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

    if (ram_disk_status & STA_NOINIT) {
        return RES_NOTRDY;
    }

    if (sector >= RAM_DISK_SECTOR_COUNT ||
        count > RAM_DISK_SECTOR_COUNT - sector) {
        return RES_PARERR;
    }

    const uint32_t offset =
        (uint32_t)sector * RAM_DISK_SECTOR_SIZE;

    const uint32_t size =
        (uint32_t)count * RAM_DISK_SECTOR_SIZE;

    memcpy(&g_ram_disk[offset], buff, size);

    return RES_OK;
}



DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) {
        return RES_PARERR;
    }

    if (ram_disk_status & STA_NOINIT) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        /*
         * RAM-диск не имеет отложенной записи,
         * поэтому синхронизация ничего не делает.
         */
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }

        *(LBA_t *)buff = (LBA_t)RAM_DISK_SECTOR_COUNT;
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }

        *(WORD *)buff = (WORD)RAM_DISK_SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }

        /*
         * Минимальная единица стирания/записи —
         * один сектор.
         */
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}