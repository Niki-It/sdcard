#pragma once

#include <stdint.h>

#include "ff.h"
#include "i2s/i2s2.h"
#include "wav_format.h"

/* Должны соответствовать настройкам I2S и кодека. */
#define WAV_SAMPLE_RATE       48000U
#define WAV_CHANNELS          2U
#define WAV_BITS_PER_SAMPLE   16U

/* Размер блока записи совпадает с размером блока I2S. */
#define WAV_RING_BLOCK_BYTES  I2S_AUDIO_BLOCK_BYTES

/*
 * Вызывать из main после монтирования FatFs
 * и инициализации I2S.
 *
 * start: создаёт файл, существующий файл перезаписывается.
 * task: записывает не более одного блока из кольца.
 * end: останавливает RX, дописывает очередь,
 *      обновляет заголовок и закрывает файл.
 *
 * После ошибки task вызвать end для попытки
 * завершения записи и закрытия файла.
 */
FRESULT wav_recording_start(const TCHAR *filename);
FRESULT wav_recording_task(void);
FRESULT wav_recording_end(void);