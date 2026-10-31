#pragma once

#include <stdbool.h>

#include "ff.h"

typedef enum
{
    AUDIO_FORMAT_WAV = 0,
    AUDIO_FORMAT_MP3
} audio_format_t;

typedef enum
{
    AUDIO_PLAYBACK_IDLE = 0,
    AUDIO_PLAYBACK_PREFILL,
    AUDIO_PLAYBACK_PLAYING,
    AUDIO_PLAYBACK_DRAINING,
    AUDIO_PLAYBACK_DONE,
    AUDIO_PLAYBACK_STOPPED,
    AUDIO_PLAYBACK_ERROR
} audio_playback_state_t;

/*
 * Все функции вызываются из main.
 * Вызовы не должны быть реентерабельными.
 *
 * start открывает файл и подготавливает стратегию.
 * Заполнение кольца выполняется через task.
 */
FRESULT audio_playback_start(const TCHAR *filename);

FRESULT audio_playback_task(void);

/*
 * Принудительная остановка.
 * Также повторяет f_close, если предыдущая попытка
 * закрыть файл закончилась ошибкой.
 */
FRESULT audio_playback_stop(void);

bool audio_playback_is_active(void);

audio_playback_state_t audio_playback_get_state(void);

FRESULT audio_playback_get_error(void);