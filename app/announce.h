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

audio_file_t next_announce(
    const char *folder,
    const char *fallback
);
void reset_announce(void);


void start_announce_delay(uint32_t seconds, uint32_t uptime_ms);
bool check_announce_delay(uint32_t uptime_ms);
bool announce_delay_active(void);