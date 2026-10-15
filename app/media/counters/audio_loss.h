#pragma once
#include <stdint.h>

#define AUDIO_LOSS_LOG_CAPACITY 256U

typedef struct
{
    uint32_t first_rx_event;
    uint32_t lost_blocks;
} audio_loss_entry_t;

typedef struct
{
    // принято блоков - блоков всего
    uint32_t rx_accepted_blocks;
    uint32_t rx_overruns;

    // количество серий потерь
    uint32_t rx_loss_series_count;
    // максимально блоков потеряно в серии
    uint32_t rx_loss_max_blocks;

    uint32_t rx_loss_log_count;
    audio_loss_entry_t loss_log[AUDIO_LOSS_LOG_CAPACITY];

    // Текущие значения - не интересны для статистики
    uint32_t rx_current_blocks;
    uint32_t current_index;


    uint32_t tx_underruns;
} audio_rx_stats_t;

extern volatile audio_rx_stats_t audio_rx_stats;

void audio_loss_reset(volatile audio_rx_stats_t* audio_loss)
{
    *audio_loss = (audio_rx_stats_t){0};

    audio_loss->rx_current_blocks = 0U;
    audio_loss->current_index = UINT32_MAX;
}