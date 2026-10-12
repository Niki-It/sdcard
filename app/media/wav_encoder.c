#include "wav_encoder.h"

#include <stdbool.h>
#include <stddef.h>
#include "circular/ring.h"       /* Change this include to your ring_t header. */
#include "stm32f4xx.h"  /* CMSIS interrupt intrinsics. */
#include "perf_counter/perf_counter.h"

extern ring_t audio_ring;
extern int i2s_rx_start_block(void);
extern int i2s_start_blocks(void);
extern void i2s_stop(void);

/* Integration contract:
 * - RX IRQ is the only ring producer; main is its only consumer.
 * - ring_push/pop copy block_size bytes and do not change IRQ masks.
 * - i2s_stop() stops RX DMA and prevents subsequent ring pushes, including
 *   pending HT/TC events. It must be safe after partial start failure.
 * - start is called only when I2S is idle; this module owns RX until end.
 * - All functions here run in main, never inside an interrupt.
 * - Put this module's data in DMA-accessible SRAM if diskio uses SDIO DMA.
 *
 * With only the requested ring/start/stop API, DMA errors and ring overruns
 * cannot be detected here. Propagate those separately in the application.
 */

#define WAV_BLOCK_ALIGN (WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8U))
/* Also keep the total physical size <= UINT32_MAX for FAT32. */
#define WAV_MAX_DATA_BYTES \
    ((UINT32_MAX - 44U) / WAV_BLOCK_ALIGN * WAV_BLOCK_ALIGN)

_Static_assert(sizeof(wav_header_t) == 44U, "WAV header must be 44 bytes");
_Static_assert(offsetof(wav_header_t, file_size) == 4U, "RIFF size offset");
_Static_assert(offsetof(wav_header_t, data_size) == 40U, "data size offset");
_Static_assert(WAV_RING_BLOCK_BYTES % WAV_BLOCK_ALIGN == 0U,
               "Ring block must contain complete PCM frames");

static FIL wav_file;
static bool file_open;
static uint32_t data_size;
static FRESULT recording_error = FR_OK;
/* uint32_t backing guarantees 4-byte alignment for an SDIO DMA driver. */
static uint32_t write_buffer[WAV_RING_BLOCK_BYTES / sizeof(uint32_t)];

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
    bool available = ring_pop(&audio_ring, (uint8_t *)write_buffer);
    ring_unlock(previous);
    return available;
}

static FRESULT write_exact(const void *buffer, UINT bytes)
{
    UINT written = 0;
    FRESULT result = f_write(&wav_file, buffer, bytes, &written);
    if (result != FR_OK)
    {
        return result;
    }
    return written == bytes ? FR_OK : FR_DENIED;
}

static FRESULT write_header(void)
{
    /* STM32F405 is little-endian, as required by RIFF/WAVE. */
    const wav_header_t header __attribute__((aligned(4))) =
    {
        .riff = {'R', 'I', 'F', 'F'},
        .file_size = 36U,
        .wave = {'W', 'A', 'V', 'E'},
        .fmt = {'f', 'm', 't', ' '},
        .fmt_size = 16U,
        .audio_format = 1U,
        .channels = WAV_CHANNELS,
        .sample_rate = WAV_SAMPLE_RATE,
        .byte_rate = WAV_SAMPLE_RATE * WAV_BLOCK_ALIGN,
        .block_align = WAV_BLOCK_ALIGN,
        .bits_per_sample = WAV_BITS_PER_SAMPLE,
        .data = {'d', 'a', 't', 'a'},
        .data_size = 0U
    };
    return write_exact(&header, (UINT)sizeof(header));
}

static FRESULT finish_header(void)
{
    const uint32_t riff_size = data_size + 36U;
    FRESULT result = f_lseek(&wav_file,
                            (FSIZE_t)offsetof(wav_header_t, file_size));
    if (result != FR_OK)
    {
        return result;
    }
    result = write_exact(&riff_size, (UINT)sizeof(riff_size));
    if (result != FR_OK)
    {
        return result;
    }
    result = f_lseek(&wav_file,
                    (FSIZE_t)offsetof(wav_header_t, data_size));
    if (result != FR_OK)
    {
        return result;
    }
    return write_exact(&data_size, (UINT)sizeof(data_size));
}

static FRESULT write_block(void)
{
    if (data_size > WAV_MAX_DATA_BYTES - WAV_RING_BLOCK_BYTES)
    {
        return FR_DENIED;
    }

    UINT written = 0;
    FRESULT result = f_write(&wav_file, write_buffer,
                            WAV_RING_BLOCK_BYTES, &written);
    /* FatFs reports the actual byte count even if the call fails. */
    data_size += written;
    if (result != FR_OK)
    {
        return result;
    }
    return written == WAV_RING_BLOCK_BYTES ? FR_OK : FR_DENIED;
}

FRESULT wav_recording_start(const TCHAR *filename)
{
    if (file_open)
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

    FRESULT result = f_open(&wav_file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK)
    {
        return result;
    }

    file_open = true;
    data_size = 0U;
    recording_error = FR_OK;

    result = write_header();
    /* Commit header before enabling the continuous producer. */
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

    uint32_t previous = ring_lock();
    ring_clear(&audio_ring);
    ring_unlock(previous);

    if (i2s_rx_start_block() != 0 || i2s_start_blocks() != 0)
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

volatile uint32_t wav_write_last_us = 0;
volatile uint32_t wav_write_max_us = 0;
volatile uint32_t wav_write_calls = 0;

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
    if (!pop_block())
    {
        return FR_OK;
    }

    uint32_t start = perf_counter_now();

    FRESULT result = write_block();

    uint32_t cycles = perf_counter_elapsed(start);
    uint32_t elapsed_us = perf_counter_cycles_to_us(cycles);

    wav_write_last_us = elapsed_us;

    if (elapsed_us > wav_write_max_us)
    {
        wav_write_max_us = elapsed_us;
    }
    wav_write_calls++;

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

    /* No new pushes after stop: drain all complete queued blocks. */
    while (recording_error == FR_OK && pop_block())
    {
        recording_error = write_block();
    }

    FRESULT result = recording_error;
    /* A short write may leave an incomplete stereo frame. Drop it. */
    if (data_size % WAV_BLOCK_ALIGN != 0U)
    {
        data_size -= data_size % WAV_BLOCK_ALIGN;
        FRESULT trim_result = f_lseek(&wav_file,
                                     (FSIZE_t)sizeof(wav_header_t) + data_size);
        if (trim_result == FR_OK)
        {
            trim_result = f_truncate(&wav_file);
        }
        if (result == FR_OK)
        {
            result = trim_result;
        }
    }

    /* Attempt to preserve the successfully written prefix even after error. */
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
        /* Keep the object for a later end/close retry; never drain again. */
        recording_error = result;
    }
    return result;
}