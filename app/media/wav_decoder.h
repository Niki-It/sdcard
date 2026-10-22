#pragma once

#include "audio_pcm.h"

FRESULT wav_decoder_prepare(FIL *file);

FRESULT wav_decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
);

void wav_decoder_close(void);
uint32_t wav_decoder_get_sample_rate();