#include "audio_playback.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "stdio.h"

#include "stm32f4xx.h"

#include "audio_pcm.h"
#include "wav_decoder.h"
#include "mp3_decoder.h"

#include "i2s/i2s2.h"
#include "circular/ring.h"

#define AUDIO_READ_BLOCKS       4U
#define AUDIO_PREFILL_BLOCKS    4U
#define AUDIO_PCM_FRAME_BYTES   4U

extern ring_t audio_ring;

_Static_assert(
    I2S_AUDIO_BLOCK_BYTES % AUDIO_PCM_FRAME_BYTES == 0U,
    "Audio block must contain complete PCM frames"
);

_Static_assert(
    AUDIO_READ_BLOCKS <= I2S_AUDIO_BLOCK_COUNT,
    "Read batch exceeds ring capacity"
);

_Static_assert(
    AUDIO_PREFILL_BLOCKS >= 2U &&
    AUDIO_PREFILL_BLOCKS <= I2S_AUDIO_BLOCK_COUNT,
    "Invalid prefill size"
);

typedef struct
{
    FIL file;

    bool file_open;
    bool owns_i2s;
    bool decoder_active;

    audio_format_t format;
    audio_playback_state_t state;
    FRESULT error;

    uint8_t *destination;
    uint32_t reserved_bytes;
    uint32_t written_bytes;
} audio_playback_context_t;

static audio_playback_context_t playback;


static uint32_t irq_lock(void)
{
    uint32_t key = __get_PRIMASK();

    __disable_irq();
    __DMB();

    return key;
}


static void irq_unlock(uint32_t key)
{
    __DMB();
    __set_PRIMASK(key);
}


static FRESULT decoder_prepare(void)
{
    switch (playback.format)
    {
        case AUDIO_FORMAT_WAV:
            return wav_decoder_prepare(&playback.file);

        case AUDIO_FORMAT_MP3:
            return mp3_decoder_prepare(&playback.file);

        default:
            return FR_INVALID_PARAMETER;
    }
}


static FRESULT decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
)
{
    switch (playback.format)
    {
        case AUDIO_FORMAT_WAV:
            return wav_decoder_read_pcm(
                destination,
                capacity,
                result
            );

        case AUDIO_FORMAT_MP3:
            return mp3_decoder_read_pcm(
                destination,
                capacity,
                result
            );

        default:
            return FR_INVALID_PARAMETER;
    }
}


static void decoder_close(void)
{
    switch (playback.format)
    {
        case AUDIO_FORMAT_WAV:
            wav_decoder_close();
            break;

        case AUDIO_FORMAT_MP3:
            mp3_decoder_close();
            break;

        default:
            break;
    }
}


static void reset_reservation(void)
{
    playback.destination = NULL;
    playback.reserved_bytes = 0U;
    playback.written_bytes = 0U;
}


static FRESULT finish_playback(
    FRESULT result,
    audio_playback_state_t final_state
)
{
    /*
     * Сначала прекращаем доступ IRQ к кольцу.
     */
    if (playback.owns_i2s)
    {
        i2s_stop();

        if (result == FR_OK && i2s_tx_has_error())
        {
            result = FR_INT_ERR;
        }

        if (playback.destination != NULL)
        {
            if (!ring_cancel_reserve(&audio_ring) &&
                result == FR_OK)
            {
                result = FR_INT_ERR;
            }
        }

        reset_reservation();

        if (!ring_clear(&audio_ring) && result == FR_OK)
        {
            result = FR_INT_ERR;
        }

        playback.owns_i2s = false;
    }

    if (playback.decoder_active)
    {
        decoder_close();
        playback.decoder_active = false;
    }

    if (playback.file_open)
    {
        FRESULT close_result = f_close(&playback.file);

        if (close_result == FR_OK)
        {
            playback.file_open = false;
        }

        if (result == FR_OK)
        {
            result = close_result;
        }
    }

    playback.error = result;

    playback.state = result == FR_OK
        ? final_state
        : AUDIO_PLAYBACK_ERROR;

    return result;
}


/*
 * Получает новый резерв либо сообщает, что пока
 * недостаточно свободного места.
 *
 * Индекс записи меняет только audio_playback.
 * IRQ извлекает опубликованные блоки.
 */
static FRESULT reserve_batch(bool *available)
{
    *available = false;

    if (playback.destination != NULL)
    {
        *available = true;
        return FR_OK;
    }

    uint32_t key = irq_lock();

    if (ring_free_count(&audio_ring) < AUDIO_READ_BLOCKS)
    {
        irq_unlock(key);
        return FR_OK;
    }

    if (audio_ring.write_index >= audio_ring.block_count)
    {
        irq_unlock(key);
        return FR_INT_ERR;
    }

    uint32_t contiguous =
        audio_ring.block_count - audio_ring.write_index;

    uint32_t blocks = AUDIO_READ_BLOCKS;

    if (blocks > contiguous)
    {
        blocks = contiguous;
    }

    uint8_t *destination = NULL;

    bool reserved = ring_reserve(
        &audio_ring,
        blocks,
        &destination
    );

    irq_unlock(key);

    if (!reserved)
    {
        return FR_INT_ERR;
    }

    playback.destination = destination;
    playback.reserved_bytes =
        blocks * I2S_AUDIO_BLOCK_BYTES;
    playback.written_bytes = 0U;

    *available = true;
    return FR_OK;
}


/*
 * Публикует полностью заполненный резерв.
 * При end публикует только необходимые блоки.
 */
static FRESULT publish_batch(bool end)
{
    uint32_t bytes = playback.written_bytes;

    uint32_t blocks = bytes / I2S_AUDIO_BLOCK_BYTES;

    if (bytes % I2S_AUDIO_BLOCK_BYTES != 0U)
    {
        blocks++;
    }

    uint32_t published_bytes =
        blocks * I2S_AUDIO_BLOCK_BYTES;

    if (published_bytes > bytes)
    {
        memset(
            playback.destination + bytes,
            0,
            published_bytes - bytes
        );
    }

    /*
     * Публикация последней порции и флаг EOF должны
     * быть видны IRQ как одно изменение.
     *
     * Здесь нет чтения SD, декодирования или memset.
     */
    uint32_t key = irq_lock();

    bool committed = ring_commit(&audio_ring, blocks);

    if (committed && end)
    {
        i2s_tx_finish();
    }

    irq_unlock(key);

    if (!committed)
    {
        return FR_INT_ERR;
    }

    reset_reservation();
    return FR_OK;
}


static FRESULT start_output(bool end)
{
    if (ring_count(&audio_ring) == 0U)
    {
        if (end)
        {
            return finish_playback(
                FR_OK,
                AUDIO_PLAYBACK_DONE
            );
        }

        return FR_OK;
    }

    if (!end &&
        ring_count(&audio_ring) < AUDIO_PREFILL_BLOCKS)
    {
        return FR_OK;
    }

    if (i2s_tx_init() != 0 || i2s_start() != 0)
    {
        return finish_playback(
            FR_INT_ERR,
            AUDIO_PLAYBACK_ERROR
        );
    }

    playback.state = end
        ? AUDIO_PLAYBACK_DRAINING
        : AUDIO_PLAYBACK_PLAYING;

    return FR_OK;
}


FRESULT audio_playback_start(const TCHAR *filename)
{
    if (audio_playback_is_active() ||
        playback.file_open ||
        playback.owns_i2s ||
        i2s_is_busy())
    {
        return FR_DENIED;
    }

    if (filename == NULL || filename[0] == 0)
    {
        return FR_INVALID_NAME;
    }

    const char *ext = strrchr(filename, '.');

    if (ext == NULL) {
        return FR_INVALID_NAME;
    }

    if (strcmp(ext, ".wav") == 0) {
        playback.format = AUDIO_FORMAT_WAV;
    }
    else if (strcmp(ext, ".mp3") == 0) {
        playback.format = AUDIO_FORMAT_MP3;
    }
    else {
        return FR_INVALID_PARAMETER;
    }

    if (audio_ring.storage == NULL ||
        audio_ring.block_size != I2S_AUDIO_BLOCK_BYTES ||
        audio_ring.block_count != I2S_AUDIO_BLOCK_COUNT)
    {
        return FR_INVALID_PARAMETER;
    }

    playback.state = AUDIO_PLAYBACK_IDLE;
    playback.error = FR_OK;

    reset_reservation();

    FRESULT result = f_open(
        &playback.file,
        filename,
        FA_READ
    );

    if (result != FR_OK)
    {
        return finish_playback(
            result,
            AUDIO_PLAYBACK_ERROR
        );
    }

    playback.file_open = true;

    /*
     * Флаг устанавливаем до prepare, чтобы close
     * очистил и частично подготовленный декодер.
     */
    playback.decoder_active = true;

    result = decoder_prepare();

    if (result != FR_OK)
    {
        return finish_playback(
            result,
            AUDIO_PLAYBACK_ERROR
        );
    }

    uint32_t sample_rate = 48000U;

    if (playback.format == AUDIO_FORMAT_MP3)
    {
        sample_rate = mp3_decoder_get_sample_rate();
    }
    if(playback.format == AUDIO_FORMAT_WAV)
    {
        sample_rate = wav_decoder_get_sample_rate();
    }

    /*
    * Проверка в начале audio_playback_start() уже гарантирует,
    * что I2S свободен. Настраиваем частоту до подготовки TX.
    */
    if (i2s_set_sample_rate(sample_rate) != 0)
    {
        return finish_playback(
            FR_NOT_READY,
            AUDIO_PLAYBACK_ERROR
        );
    }

    if (i2s_tx_prepare() != 0)
    {
        return finish_playback(
            FR_NOT_READY,
            AUDIO_PLAYBACK_ERROR
        );
    }

    playback.owns_i2s = true;
    playback.state = AUDIO_PLAYBACK_PREFILL;

    return FR_OK;
}


FRESULT audio_playback_task(void)
{
    if (playback.state == AUDIO_PLAYBACK_ERROR)
    {
        return playback.error;
    }

    if (!audio_playback_is_active())
    {
        return FR_OK;
    }

    if (i2s_tx_has_error())
    {
        return finish_playback(
            FR_INT_ERR,
            AUDIO_PLAYBACK_ERROR
        );
    }

    if (playback.state == AUDIO_PLAYBACK_DRAINING)
    {
        if (i2s_tx_is_done())
        {
            return finish_playback(
                FR_OK,
                AUDIO_PLAYBACK_DONE
            );
        }

        return FR_OK;
    }

    bool available = false;

    FRESULT result = reserve_batch(&available);

    if (result != FR_OK)
    {
        return finish_playback(
            result,
            AUDIO_PLAYBACK_ERROR
        );
    }

    if (!available)
    {
        return FR_OK;
    }

    uint32_t capacity =
        playback.reserved_bytes - playback.written_bytes;

    audio_pcm_result_t pcm = {0};

    result = decoder_read_pcm(
        playback.destination + playback.written_bytes,
        capacity,
        &pcm
    );

    if (result != FR_OK)
    {
        return finish_playback(
            result,
            AUDIO_PLAYBACK_ERROR
        );
    }

    /*
     * Проверяем соблюдение контракта стратегии.
     */
    if (pcm.bytes > capacity ||
        pcm.bytes % AUDIO_PCM_FRAME_BYTES != 0U)
    {
        return finish_playback(
            FR_INT_ERR,
            AUDIO_PLAYBACK_ERROR
        );
    }

    if (i2s_tx_has_error())
    {
        return finish_playback(
            FR_INT_ERR,
            AUDIO_PLAYBACK_ERROR
        );
    }

    playback.written_bytes += pcm.bytes;

    bool batch_full =
        playback.written_bytes == playback.reserved_bytes;

    if (!batch_full && !pcm.end)
    {
        return FR_OK;
    }

    result = publish_batch(pcm.end);

    if (result != FR_OK)
    {
        return finish_playback(
            result,
            AUDIO_PLAYBACK_ERROR
        );
    }

    if (playback.state == AUDIO_PLAYBACK_PREFILL)
    {
        return start_output(pcm.end);
    }

    if (pcm.end)
    {
        playback.state = AUDIO_PLAYBACK_DRAINING;
    }

    return FR_OK;
}


FRESULT audio_playback_stop(void)
{
    return finish_playback(
        playback.error,
        AUDIO_PLAYBACK_STOPPED
    );
}


bool audio_playback_is_active(void)
{
    return playback.state == AUDIO_PLAYBACK_PREFILL ||
           playback.state == AUDIO_PLAYBACK_PLAYING ||
           playback.state == AUDIO_PLAYBACK_DRAINING;
}


audio_playback_state_t audio_playback_get_state(void)
{
    return playback.state;
}


FRESULT audio_playback_get_error(void)
{
    return playback.error;
}