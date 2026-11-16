#pragma once

#include "stdint.h"
#include "stdbool.h"

#define AUDIO_PATH_MAX       320
#define AUDIO_FILENAME_LEN   12

typedef struct
{
    bool result;
    char pathfile[AUDIO_PATH_MAX];
    uint16_t delay;
} audio_file_t;

typedef enum
{
    ANNOUNCE_FOLDER_1,
    ANNOUNCE_FOLDER_2,
    ANNOUNCE_FOLDER_BOTH
} announce_folder_t;

typedef enum
{
    ANNOUNCE_ORDER_ALTERNATE,
    ANNOUNCE_ORDER_SEQUENTIAL
} announce_order_t;

typedef struct
{
    announce_folder_t folder;
    announce_order_t order;
    bool repeat;
    uint8_t next_number[2];
    uint8_t current_folder;
} announce_sequence_t;

void announce_sequence_reset(announce_sequence_t *sequence, announce_folder_t folder, announce_order_t order, bool repeat);
bool announce_sequence_next(announce_sequence_t *sequence, audio_file_t *file);

audio_file_t next_announce(
    const char *folder,
    const char *fallback
);
void reset_announce(void);


void start_announce_delay(uint32_t seconds, uint32_t uptime_ms);
bool check_announce_delay(uint32_t uptime_ms);
bool announce_delay_active(void);
