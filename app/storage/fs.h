#include "ff.h"
#include "../lib/FatFs/src/diskio.h"
#include "string.h"
#include "stdbool.h"

extern FATFS fs;

FRESULT fs_unit(bool allow_format);