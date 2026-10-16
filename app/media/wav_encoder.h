#pragma once

#include <stdint.h>
#include "ff.h"
#include "i2s/i2s2.h"

/* Must match the actual I2S/codec configuration. */
#define WAV_SAMPLE_RATE      48000U
#define WAV_CHANNELS         2U
#define WAV_BITS_PER_SAMPLE  16U
#define WAV_RING_BLOCK_BYTES 2048U

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


/* Call from main only; FatFs must already be mounted and I2S initialized.
 * start: creates a NEW file; an existing filename returns FR_EXIST.
 * task: writes at most one queued block; an empty queue returns FR_OK.
 * end: stops RX, drains complete queued blocks, patches sizes, closes file.
 * On a task error, RX is stopped. Call end to attempt finalization/close.
 * FR_NOT_READY can also mean I2S start failed; FR_DENIED means already open,
 * a short write (full medium), or the classic WAV/FAT32 size limit.
 */
FRESULT wav_recording_start(const TCHAR *filename);
FRESULT wav_recording_task(void);
FRESULT wav_recording_end(void);