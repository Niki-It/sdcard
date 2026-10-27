#include "wav_encoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx.h"
#include "circular/ring.h"
#include "perf_counter/perf_counter.h"

extern ring_t audio_ring;

#define WAV_BLOCK_ALIGN \
    (WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8U))

#define WAV_MAX_DATA_BYTES \
    ((UINT32_MAX - 44U) / WAV_BLOCK_ALIGN * WAV_BLOCK_ALIGN)

#define WRITE_TIME_LOG_COUNT 2048U
#define WAV_SAMPLE_RATE 48000U

static FIL wav_file;
static bool file_open;
static uint32_t data_size;
static FRESULT recording_error = FR_OK;

static uint32_t write_buffer[
    WAV_RING_BLOCK_BYTES / sizeof(uint32_t)
];

volatile uint32_t wav_write_last_us = 0;
volatile uint32_t wav_write_max_us = 0;
volatile uint32_t wav_write_calls = 0;

volatile uint32_t wav_ring_before_write = 0;
volatile uint32_t wav_ring_at_max_write = 0;

volatile uint32_t wav_write_started_cycles = 0;
volatile uint32_t wav_write_active = 0;

volatile uint32_t write_time_us[WRITE_TIME_LOG_COUNT];
volatile uint32_t write_time_count = 0;


static void write_stats_reset(void)
{
    wav_write_last_us = 0;
    wav_write_max_us = 0;
    wav_write_calls = 0;

    wav_ring_before_write = 0;
    wav_ring_at_max_write = 0;

    wav_write_started_cycles = 0;
    wav_write_active = 0;

    write_time_count = 0;
}


static uint32_t ring_lock(void)
{
    uint32_t previous = __get_PRIMASK();

    __disable_irq();
    __DMB();

    return previous;
}


static void ring_unlock(uint32_t previous)
{
    __DMB();
    __set_PRIMASK(previous);
}


static bool pop_block(void)
{
    uint32_t previous = ring_lock();

    bool available =
        ring_pop(&audio_ring, (uint8_t *)write_buffer);

    ring_unlock(previous);

    return available;
}


static uint32_t ring_count_safe(void)
{
    uint32_t previous = ring_lock();

    uint32_t count = (uint32_t)ring_count(&audio_ring);

    ring_unlock(previous);

    return count;
}


static FRESULT write_exact(const void *buffer, UINT bytes)
{
    UINT written = 0;

    FRESULT result =
        f_write(&wav_file, buffer, bytes, &written);

    if (result != FR_OK)
    {
        return result;
    }

    return written == bytes ? FR_OK : FR_DENIED;
}


static FRESULT write_header(void)
{
    const wav_header_t header __attribute__((aligned(4))) =
    {
        .riff            = {'R', 'I', 'F', 'F'},
        .file_size       = WAV_PCM_HEADER_BYTES - 8U,
        .wave            = {'W', 'A', 'V', 'E'},

        .fmt             = {'f', 'm', 't', ' '},
        .fmt_size        = WAV_PCM_FMT_BYTES,
        .audio_format    = WAV_FORMAT_PCM,
        .channels        = WAV_CHANNELS,
        .sample_rate     = WAV_SAMPLE_RATE,
        .byte_rate       = WAV_SAMPLE_RATE * WAV_BLOCK_ALIGN,
        .block_align     = WAV_BLOCK_ALIGN,
        .bits_per_sample = WAV_BITS_PER_SAMPLE,

        .data            = {'d', 'a', 't', 'a'},
        .data_size       = 0U
    };

    return write_exact(&header, (UINT)sizeof(header));
}


static FRESULT finish_header(void)
{
    const uint32_t riff_size = data_size + 36U;

    FRESULT result = f_lseek(
        &wav_file,
        (FSIZE_t)offsetof(wav_header_t, file_size)
    );

    if (result != FR_OK)
    {
        return result;
    }

    result = write_exact(&riff_size, (UINT)sizeof(riff_size));

    if (result != FR_OK)
    {
        return result;
    }

    result = f_lseek(
        &wav_file,
        (FSIZE_t)offsetof(wav_header_t, data_size)
    );

    if (result != FR_OK)
    {
        return result;
    }

    return write_exact(&data_size, (UINT)sizeof(data_size));
}


// Измеряем запись PCM здесь, чтобы учесть также
// дописывание очереди в wav_recording_end().
static FRESULT write_block(void)
{
    if (data_size > WAV_MAX_DATA_BYTES - WAV_RING_BLOCK_BYTES)
    {
        return FR_DENIED;
    }

    uint32_t queued = ring_count_safe();
    wav_ring_before_write = queued;

    UINT written = 0;

    uint32_t start = perf_counter_now();

    wav_write_started_cycles = start;
    wav_write_active = 1;

    FRESULT result = f_write(
        &wav_file,
        write_buffer,
        WAV_RING_BLOCK_BYTES,
        &written
    );

    uint32_t elapsed_cycles = perf_counter_elapsed(start);

    wav_write_active = 0;

    uint32_t elapsed_us =
        perf_counter_cycles_to_us(elapsed_cycles);

    wav_write_last_us = elapsed_us;

    uint32_t index = write_time_count;

    if (index < WRITE_TIME_LOG_COUNT)
    {
        write_time_us[index] = elapsed_us;
        write_time_count = index + 1U;
    }

    if (elapsed_us > wav_write_max_us)
    {
        wav_write_max_us = elapsed_us;
        wav_ring_at_max_write = queued;
    }

    wav_write_calls++;

    data_size += written;

    if (result != FR_OK)
    {
        return result;
    }

    return written == WAV_RING_BLOCK_BYTES
               ? FR_OK
               : FR_DENIED;
}


FRESULT wav_recording_start(const TCHAR *filename)
{
    if (file_open || i2s_is_busy())
    {
        return FR_DENIED;
    }

    if (filename == NULL || filename[0] == 0)
    {
        return FR_INVALID_NAME;
    }

    if (audio_ring.storage == NULL ||
        audio_ring.block_size != WAV_RING_BLOCK_BYTES ||
        audio_ring.block_count == 0U)
    {
        return FR_INVALID_PARAMETER;
    }

    FRESULT result = f_open(
        &wav_file,
        filename,
        FA_WRITE | FA_CREATE_ALWAYS
    );

    if (result != FR_OK)
    {
        return result;
    }

    file_open = true;
    data_size = 0;
    recording_error = FR_OK;

    write_stats_reset();

    result = write_header();

    if (result == FR_OK)
    {
        result = f_sync(&wav_file);
    }

    if (result != FR_OK)
    {
        recording_error = result;

        FRESULT close_result = f_close(&wav_file);

        if (close_result == FR_OK)
        {
            file_open = false;
        }

        return result;
    }

    // Настраиваем частоту до инициализации и запуска RX.
    if (i2s_set_sample_rate(WAV_SAMPLE_RATE) != 0 ||
        i2s_rx_init() != 0 ||
        i2s_start() != 0)
    {
        i2s_stop();

        recording_error = FR_NOT_READY;

        FRESULT close_result = f_close(&wav_file);

        if (close_result == FR_OK)
        {
            file_open = false;
        }

        return recording_error;
    }

    return FR_OK;
}


FRESULT wav_recording_task(void)
{
    if (!file_open)
    {
        return FR_NOT_READY;
    }

    if (recording_error != FR_OK)
    {
        return recording_error;
    }

    // Это аппаратная ошибка / ошибка структуры очереди.
    // Замена блока при заполнении не устанавливает этот флаг.
    if (i2s_rx_has_error())
    {
        i2s_stop();
        recording_error = FR_INT_ERR;
        return recording_error;
    }

    if (!pop_block())
    {
        return FR_OK;
    }

    FRESULT result = write_block();

    // RX мог сообщить ошибку, пока выполнялся f_write().
    if (result == FR_OK && i2s_rx_has_error())
    {
        result = FR_INT_ERR;
    }

    if (result != FR_OK)
    {
        recording_error = result;
        i2s_stop();
    }

    return result;
}


FRESULT wav_recording_end(void)
{
    if (!file_open)
    {
        return FR_NOT_READY;
    }

    i2s_stop();

    bool rx_failed = i2s_rx_has_error();

    // Дописываем оставшиеся блоки, если ранее не было ошибки.
    while (recording_error == FR_OK && pop_block())
    {
        recording_error = write_block();
    }

    FRESULT result = recording_error;

    if (result == FR_OK && rx_failed)
    {
        result = FR_INT_ERR;
    }

    // После неполной записи оставляем только полные PCM-кадры.
    if (data_size % WAV_BLOCK_ALIGN != 0U)
    {
        data_size -= data_size % WAV_BLOCK_ALIGN;

        FRESULT trim_result = f_lseek(
            &wav_file,
            (FSIZE_t)sizeof(wav_header_t) + data_size
        );

        if (trim_result == FR_OK)
        {
            trim_result = f_truncate(&wav_file);
        }

        if (result == FR_OK)
        {
            result = trim_result;
        }
    }

    FRESULT header_result = finish_header();

    if (result == FR_OK)
    {
        result = header_result;
    }

    FRESULT close_result = f_close(&wav_file);

    if (result == FR_OK)
    {
        result = close_result;
    }

    if (close_result == FR_OK)
    {
        file_open = false;
    }
    else
    {
        // При повторной попытке закрытия очередь не пишем заново.
        recording_error = result;
    }

    return result;
}