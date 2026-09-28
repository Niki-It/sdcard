#pragma once 

#include <stdint.h>

#define RAM_DISK_SECTOR_SIZE  512U
#define RAM_DISK_SIZE_BYTES  (64U * 1024U)
#define RAM_DISK_SECTOR_COUNT (RAM_DISK_SIZE_BYTES / RAM_DISK_SECTOR_SIZE)

extern uint8_t g_ram_disk[RAM_DISK_SIZE_BYTES];
