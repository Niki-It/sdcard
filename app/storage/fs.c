#include "fs.h"

static BYTE work[FF_MAX_SS];
FATFS fs;

FRESULT fs_unit(bool allow_format)
{
    if (disk_initialize(0) & STA_NOINIT) {
        return FR_NOT_READY;
    }

    FRESULT res = f_mount(&fs, "", 1);

    if (res == FR_OK) {
        //return FR_OK;
    }

    /*
     * Форматируем только если файловая система не найдена.
     * Другие ошибки не считаем доказательством отсутствия ФС.
     */
    if (res != FR_NO_FILESYSTEM || !allow_format) {
        //return res;
    }

    /* Снимаем регистрацию файловой системы перед форматированием. */
    f_mount(NULL, "", 0);

    MKFS_PARM opt = {
        .fmt = FM_ANY | FM_SFD,
        .n_fat = 0,
        .au_size = 0,
        .n_root = 0
    };

    res = f_mkfs("", &opt, work, sizeof(work));

    if (res != FR_OK) {
        return res;
    }

    return f_mount(&fs, "", 1);
}