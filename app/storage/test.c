#include "ff.h"
#include "../lib/FatFs/src/diskio.h"

static FATFS fs;
static BYTE work[FF_MAX_SS];
void fs_unit()
{
    FIL file;
    FRESULT result;
    UINT bytes_written;

    /* Подготавливаем RAM-диск для FatFs. */
    DSTATUS status = disk_initialize(0);

    /* Форматируем диск: FatFs сам выберет FAT12 или FAT16. */
    MKFS_PARM format_options = {
        .fmt = FM_FAT | FM_SFD,
        .n_fat = 1,
        .align = 0,
        .n_root = 0,
        .au_size = 0
    };

    result = f_mkfs(
        "",
        &format_options,
        work,
        sizeof(work)
    );

    if (result != FR_OK) {
        return;
    }

    result = f_mount(&fs, "", 1);
    if (result != FR_OK) 
    {
        return;
    }


    result = f_open(&file, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        return;
    }

    const char text[] = "FatFs test on SD card\r\n";

    result = f_write(
        &file,
        text,
        sizeof(text) - 1,
        &bytes_written
    );

    if (result != FR_OK || bytes_written != sizeof(text) - 1) {
        f_close(&file);
        return;
    }
    result = f_close(&file);
    if (result != FR_OK) {
        return;
    }
    
}