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
    uint32_t rx_blocks;
    uint32_t overruns;

    uint32_t loss_series_count;
    uint32_t loss_max_blocks;

    uint32_t loss_log_count;
    uint32_t loss_unlogged_series;

    audio_loss_entry_t loss_log[AUDIO_LOSS_LOG_CAPACITY];

    uint32_t current_blocks;
    uint32_t current_index;
} audio_rx_stats_t;

extern volatile audio_rx_stats_t audio_rx_stats;

void audio_loss_reset(volatile audio_rx_stats_t* audio_loss)
{
    *audio_loss = (audio_rx_stats_t){0};

    audio_loss->current_blocks = 0U;
    audio_loss->current_index = UINT32_MAX;
}