#include "wav_decoder.h"


#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "i2s/i2s2.h"
#include "circular/ring.h"
#include "perf_counter/perf_counter.h"

/* Единственная внешняя переменная, связывающая модули. */
extern ring_t audio_ring;

#define WAV_HEADER_BYTES       44U
#define WAV_SAMPLE_RATE        48000U
#define WAV_CHANNELS           2U
#define WAV_BITS_PER_SAMPLE    16U
#define WAV_FRAME_BYTES        4U
#define WAV_BYTE_RATE          192000U

#define WAV_READ_BLOCKS        8U
#define WAV_READ_BYTES \
    (WAV_READ_BLOCKS * I2S_AUDIO_BLOCK_BYTES)

_Static_assert(
    I2S_AUDIO_BLOCK_BYTES % WAV_FRAME_BYTES == 0U,
    "Audio block must contain complete stereo frames"
);

_Static_assert(
    I2S_AUDIO_BLOCK_COUNT % WAV_READ_BLOCKS == 0U,
    "Ring capacity must be divisible by read batch"
);

static FIL wav_file;

static bool file_open;
static bool owns_i2s;

static uint32_t remaining_bytes;

static wav_playback_state_t playback_state = WAV_PLAYBACK_IDLE;
static FRESULT playback_error = FR_OK;

static wav_playback_stats_t playback_stats;

/*
 * Маленький буфер заголовка, не PCM-буфер.
 * static: не зависит от размещения стека.
 */
static uint8_t header[WAV_HEADER_BYTES]
    __attribute__((aligned(4)));


static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(
        (uint16_t)p[0] |
        ((uint16_t)p[1] << 8)
    );
}


static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}


/*
 * Только формат заголовка, создаваемый твоим encoder.
 * Для неподдерживаемого/повреждённого WAV возвращаем
 * FR_INVALID_PARAMETER.
 */
static FRESULT read_header(void)
{
    UINT bytes_read = 0U;

    FRESULT result = f_read(
        &wav_file,
        header,
        sizeof(header),
        &bytes_read
    );

    if (result != FR_OK)
    {
        return result;
    }

    if (bytes_read != sizeof(header))
    {
        return FR_INVALID_PARAMETER;
    }

    if (memcmp(header + 0U,  "RIFF", 4U) != 0 ||
        memcmp(header + 8U,  "WAVE", 4U) != 0 ||
        memcmp(header + 12U, "fmt ", 4U) != 0 ||
        memcmp(header + 36U, "data", 4U) != 0)
    {
        return FR_INVALID_PARAMETER;
    }

    if (read_le32(header + 16U) != 16U ||
        read_le16(header + 20U) != 1U ||
        read_le16(header + 22U) != WAV_CHANNELS ||
        read_le32(header + 24U) != WAV_SAMPLE_RATE ||
        read_le32(header + 28U) != WAV_BYTE_RATE ||
        read_le16(header + 32U) != WAV_FRAME_BYTES ||
        read_le16(header + 34U) != WAV_BITS_PER_SAMPLE)
    {
        return FR_INVALID_PARAMETER;
    }

    uint32_t data_bytes = read_le32(header + 40U);
    uint32_t riff_bytes = read_le32(header + 4U);

    if (data_bytes % WAV_FRAME_BYTES != 0U)
    {
        return FR_INVALID_PARAMETER;
    }

    /*
     * uint64_t исключает переполнение при проверке
     * повреждённых размеров из заголовка.
     */
    uint64_t data_end = (uint64_t)WAV_HEADER_BYTES + data_bytes;
    uint64_t riff_end = (uint64_t)riff_bytes + 8U;

    if (riff_end < data_end ||
        riff_end > (uint64_t)f_size(&wav_file))
    {
        return FR_INVALID_PARAMETER;
    }

    remaining_bytes = data_bytes;
    playback_stats.data_bytes = data_bytes;

    /* Позиция файла уже равна 44 — началу PCM. */
    return FR_OK;
}


/*
 * Общая остановка/закрытие.
 * Не вызывается, пока существует наш активный резерв.
 */
static FRESULT finish_playback(
    FRESULT result,
    wav_playback_state_t final_state
)
{
    if (owns_i2s)
    {
        i2s_stop();

        playback_stats.underrun_blocks =
            i2s_tx_underrun_count();

        if (result == FR_OK && i2s_tx_has_error())
        {
            result = FR_INT_ERR;
        }

        owns_i2s = false;

        if (!ring_clear(&audio_ring) && result == FR_OK)
        {
            result = FR_INT_ERR;
        }
    }

    if (file_open)
    {
        FRESULT close_result = f_close(&wav_file);

        if (close_result == FR_OK)
        {
            file_open = false;
        }

        if (result == FR_OK)
        {
            result = close_result;
        }
    }

    playback_error = result;

    playback_state = result == FR_OK
                         ? final_state
                         : WAV_PLAYBACK_ERROR;

    return result;
}


/*
 * Выполняет максимум одно чтение.
 *
 * made_progress=false:
 * данных больше нет либо пока не хватает места.
 */
static FRESULT read_batch(bool *made_progress)
{
    *made_progress = false;

    if (remaining_bytes == 0U)
    {
        return FR_OK;
    }

    uint32_t bytes =
        remaining_bytes > WAV_READ_BYTES
            ? WAV_READ_BYTES
            : remaining_bytes;

    uint32_t blocks =
        (bytes + I2S_AUDIO_BLOCK_BYTES - 1U) /
        I2S_AUDIO_BLOCK_BYTES;

    if (ring_free_count(&audio_ring) < blocks)
    {
        return FR_OK;
    }

    uint8_t *destination = NULL;

    /*
     * Позиции обычных порций: 0, 8, 16, 24.
     * Последняя порция может быть меньше.
     *
     * При достаточном free_count отказ здесь означает
     * нарушение ожидаемого состояния/владения очередью.
     */
    if (!ring_reserve(&audio_ring, blocks, &destination))
    {
        return FR_INT_ERR;
    }

    UINT bytes_read = 0U;

    uint32_t started = perf_counter_now();

    FRESULT result = f_read(
        &wav_file,
        destination,
        (UINT)bytes,
        &bytes_read
    );

    uint32_t elapsed_us = perf_counter_cycles_to_us(
        perf_counter_elapsed(started)
    );

    playback_stats.read_calls++;
    playback_stats.read_last_us = elapsed_us;

    if (elapsed_us > playback_stats.read_max_us)
    {
        playback_stats.read_max_us = elapsed_us;
    }

    if (result != FR_OK || bytes_read != bytes)
    {
        /*
         * Короткое чтение здесь не является штатным EOF:
         * ожидаемый размер уже известен из data_size.
         */
        bool cancelled = ring_cancel_reserve(&audio_ring);

        if (result != FR_OK)
        {
            return result;
        }

        return cancelled ? FR_DISK_ERR : FR_INT_ERR;
    }

    /*
     * За время блокирующего f_read мог возникнуть
     * аппаратный сбой TX.
     */
    if (i2s_tx_has_error())
    {
        (void)ring_cancel_reserve(&audio_ring);
        return FR_INT_ERR;
    }

    uint32_t reserved_bytes =
        blocks * I2S_AUDIO_BLOCK_BYTES;

    /*
     * Последний неполный блок дополняем тишиной
     * до публикации. Дополнительный буфер не нужен.
     */
    if (bytes < reserved_bytes)
    {
        memset(
            destination + bytes,
            0,
            reserved_bytes - bytes
        );
    }

    if (!ring_commit(&audio_ring, blocks))
    {
        (void)ring_cancel_reserve(&audio_ring);
        return FR_INT_ERR;
    }

    remaining_bytes -= bytes;
    playback_stats.loaded_bytes += bytes;

    *made_progress = true;
    return FR_OK;
}


FRESULT wav_playback_start(const TCHAR *filename)
{
    if (file_open ||
        owns_i2s ||
        playback_state == WAV_PLAYBACK_RUNNING ||
        i2s_is_busy())
    {
        return FR_DENIED;
    }

    if (filename == NULL || filename[0] == 0)
    {
        return FR_INVALID_NAME;
    }

    /*
     * Размеры после ring_init неизменны.
     * Сами индексы/счётчики здесь напрямую не используем.
     */
    if (audio_ring.storage == NULL ||
        audio_ring.block_size != I2S_AUDIO_BLOCK_BYTES ||
        audio_ring.block_count != I2S_AUDIO_BLOCK_COUNT)
    {
        return FR_INVALID_PARAMETER;
    }

    playback_stats = (wav_playback_stats_t){0};
    playback_error = FR_OK;
    playback_state = WAV_PLAYBACK_IDLE;
    remaining_bytes = 0U;

    FRESULT result = f_open(
        &wav_file,
        filename,
        FA_READ
    );

    if (result != FR_OK)
    {
        playback_error = result;
        playback_state = WAV_PLAYBACK_ERROR;
        return result;
    }

    file_open = true;

    result = read_header();

    if (result != FR_OK)
    {
        return finish_playback(result, WAV_PLAYBACK_ERROR);
    }

    /* Пустой корректный WAV не требует запуска DMA. */
    if (remaining_bytes == 0U)
    {
        return finish_playback(FR_OK, WAV_PLAYBACK_DONE);
    }

    /* Деление с округлением вверх без риска переполнения. */
    uint32_t total_blocks =
        remaining_bytes / I2S_AUDIO_BLOCK_BYTES;

    if (remaining_bytes % I2S_AUDIO_BLOCK_BYTES != 0U)
    {
        total_blocks++;
    }

    if (i2s_tx_prepare(total_blocks) != 0)
    {
        return finish_playback(FR_NOT_READY, WAV_PLAYBACK_ERROR);
    }

    owns_i2s = true;

    /*
     * Предзаполнение до 32 блоков.
     * Для длинного файла — четыре чтения по 16 КиБ.
     */
    for (;;)
    {
        bool made_progress = false;

        result = read_batch(&made_progress);

        if (result != FR_OK)
        {
            return finish_playback(result, WAV_PLAYBACK_ERROR);
        }

        if (!made_progress)
        {
            break;
        }
    }

    if (i2s_tx_init() != 0 ||
        i2s_start() != 0)
    {
        return finish_playback(FR_NOT_READY, WAV_PLAYBACK_ERROR);
    }

    playback_state = WAV_PLAYBACK_RUNNING;
    return FR_OK;
}


FRESULT wav_playback_task(void)
{
    if (playback_state == WAV_PLAYBACK_ERROR)
    {
        return playback_error;
    }

    if (playback_state != WAV_PLAYBACK_RUNNING)
    {
        return FR_OK;
    }

    if (i2s_tx_has_error())
    {
        return finish_playback(FR_INT_ERR, WAV_PLAYBACK_ERROR);
    }

    if (i2s_tx_is_done())
    {
        return finish_playback(FR_OK, WAV_PLAYBACK_DONE);
    }

    bool made_progress = false;

    FRESULT result = read_batch(&made_progress);

    playback_stats.underrun_blocks =
        i2s_tx_underrun_count();

    if (result != FR_OK)
    {
        return finish_playback(result, WAV_PLAYBACK_ERROR);
    }

    if (i2s_tx_has_error())
    {
        return finish_playback(FR_INT_ERR, WAV_PLAYBACK_ERROR);
    }

    if (i2s_tx_is_done())
    {
        return finish_playback(FR_OK, WAV_PLAYBACK_DONE);
    }

    return FR_OK;
}


FRESULT wav_playback_end(void)
{
    if (!file_open && !owns_i2s)
    {
        return playback_error;
    }

    return finish_playback(
        playback_error,
        WAV_PLAYBACK_STOPPED
    );
}


bool wav_playback_is_active(void)
{
    return playback_state == WAV_PLAYBACK_RUNNING;
}


wav_playback_state_t wav_playback_get_state(void)
{
    return playback_state;
}


FRESULT wav_playback_get_error(void)
{
    return playback_error;
}


void wav_playback_get_stats(wav_playback_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    *stats = playback_stats;

    if (owns_i2s)
    {
        stats->underrun_blocks = i2s_tx_underrun_count();
    }
}