#pragma once

#include <stddef.h>
#include <stdint.h>

#define WAV_FORMAT_PCM         1U
#define WAV_PCM_FMT_BYTES      16U
#define WAV_PCM_HEADER_BYTES   44U

/*
 * Компактный заголовок PCM WAV, который создаёт наш encoder.
 *
 * Произвольный WAV может содержать дополнительные chunks,
 * поэтому эта структура не заменяет разбор chunks в decoder.
 */
typedef struct __attribute__((packed))
{
    char riff[4];
    uint32_t file_size;
    char wave[4];

    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    char data[4];
    uint32_t data_size;
} wav_header_t;

_Static_assert(
    sizeof(wav_header_t) == WAV_PCM_HEADER_BYTES,
    "WAV header must be 44 bytes"
);

_Static_assert(
    offsetof(wav_header_t, file_size) == 4U,
    "RIFF size offset"
);

_Static_assert(
    offsetof(wav_header_t, data_size) == 40U,
    "Data size offset"
);