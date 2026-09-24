#pragma once
#include "sdio_ll.h"
// ------------------ Легаси API ------------------------------

void SDIO_Periph_Init(void);
uint32_t SDIO_TestCard(void);
void SDIO_RunBenchmark();

// ---------------- Новый API ---------------------------- 

typedef struct {
    uint64_t capacity_bytes;
    uint32_t block_count;
    uint32_t block_size;
    bool high_capacity;
} sd_card_info_t;


sd_status_t sd_init(sd_card_info_t *info);


sd_status_t sd_read_blocks(
    uint32_t lba,
    uint8_t *buffer,
    uint32_t count
);

sd_status_t sd_write_blocks(
    uint32_t lba,
    const uint8_t *buffer,
    uint32_t count
);

sd_status_t sd_sync(void); // не используется
