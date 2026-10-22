#pragma once

#include "audio_pcm.h"

FRESULT mp3_decoder_prepare(FIL *file);

FRESULT mp3_decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
);

void mp3_decoder_close(void);
uint32_t mp3_decoder_get_sample_rate(void);