#include "ff.h"

DWORD get_fattime(void)
{
    /*
     * 2024-01-01 00:00:00
     * Формат FatFs:
     * bits 31..25: year - 1980
     * bits 24..21: month
     * bits 20..16: day
     * bits 15..11: hour
     * bits 10..5:  minute
     * bits 4..0:   second / 2
     */
    return ((DWORD)(2024 - 1980) << 25)
         | ((DWORD)1 << 21)
         | ((DWORD)1 << 16);
}