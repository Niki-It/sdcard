#pragma once

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

typedef enum
{
    WAV_PLAYBACK_IDLE = 0,
    WAV_PLAYBACK_RUNNING,
    WAV_PLAYBACK_DONE,
    WAV_PLAYBACK_STOPPED,
    WAV_PLAYBACK_ERROR
} wav_playback_state_t;

typedef struct
{
    uint32_t data_bytes;
    uint32_t loaded_bytes;

    uint32_t read_calls;
    uint32_t read_last_us;
    uint32_t read_max_us;

    uint32_t underrun_blocks;
} wav_playback_stats_t;

/*
 * Все функции вызываются из main.
 * Вызовы не должны быть реентерабельными.
 *
 * Поддерживается стандартный заголовок 44 байта:
 * RIFF/WAVE, fmt размером 16, PCM 48k/stereo/16bit, data.
 */
FRESULT wav_playback_start(const TCHAR *filename);

/*
 * Один вызов читает не больше восьми блоков.
 * При штатном завершении останавливает I2S
 * и закрывает файл автоматически.
 */
FRESULT wav_playback_task(void);

/*
 * Принудительная остановка.
 * Также позволяет повторить f_close(), если он завершился
 * ошибкой при предыдущей попытке.
 */
FRESULT wav_playback_end(void);

bool wav_playback_is_active(void);

wav_playback_state_t wav_playback_get_state(void);
FRESULT wav_playback_get_error(void);

void wav_playback_get_stats(wav_playback_stats_t *stats);