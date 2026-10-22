#include "wav_decoder.h"
#include "wav_format.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WAV_CHANNELS          2U
#define WAV_BITS_PER_SAMPLE   16U

#define WAV_RIFF_SIZE_BYTES   8U
#define WAV_BLOCK_ALIGN \
    (WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8U))

static struct
{
    FIL *file;
    uint32_t remaining;
    uint32_t sample_rate;
} wav;


static FRESULT read_header(FIL *file, wav_header_t *header)
{
    FRESULT result = f_lseek(file, 0U);

    if (result != FR_OK)
    {
        return result;
    }

    if (f_tell(file) != 0U)
    {
        return FR_INT_ERR;
    }

    UINT received = 0U;

    result = f_read(
        file,
        header,
        (UINT)sizeof(*header),
        &received
    );

    if (result != FR_OK)
    {
        return result;
    }

    if (received != sizeof(*header))
    {
        return FR_INT_ERR;
    }

    if (memcmp(header->riff, "RIFF", 4U) != 0 ||
        memcmp(header->wave, "WAVE", 4U) != 0 ||
        memcmp(header->fmt, "fmt ", 4U) != 0)
    {
        return FR_INVALID_PARAMETER;
    }

    if (header->fmt_size != WAV_PCM_FMT_BYTES ||
        header->audio_format != WAV_FORMAT_PCM ||
        header->channels != WAV_CHANNELS ||
        header->sample_rate == 0U ||
        header->bits_per_sample != WAV_BITS_PER_SAMPLE ||
        header->block_align != WAV_BLOCK_ALIGN ||
        (uint64_t)header->byte_rate !=
            (uint64_t)header->sample_rate * WAV_BLOCK_ALIGN)
    {
        return FR_INVALID_PARAMETER;
    }
    uint64_t declared_size =
    (uint64_t)header->file_size + WAV_RIFF_SIZE_BYTES;

    if (declared_size < (uint64_t)f_tell(file) ||
        declared_size != (uint64_t)f_size(file))
    {
        return FR_INT_ERR;
    }

    // скип чанков хэдера
    for (;;)
    {
        uint64_t chunk_end =
            (uint64_t)f_tell(file) + header->data_size;

        uint64_t next_chunk =
            chunk_end + (header->data_size & 1U);

        if (next_chunk > declared_size)
        {
            return FR_INT_ERR;
        }

        if (memcmp(header->data, "data", 4U) == 0)
        {
            break; /* Файл уже стоит на первом байте аудио. */
        }

        /* Следующий заголовок чанка занимает 8 байт. */
        if (declared_size - next_chunk < 8U)
        {
            return FR_INT_ERR;
        }

        result = f_lseek(file, next_chunk);

        if (result != FR_OK)
        {
            return result;
        }

        if ((uint64_t)f_tell(file) != next_chunk)
        {
            return FR_INT_ERR;
        }

        uint8_t chunk[8];

        result = f_read(file, chunk, sizeof(chunk), &received);

        if (result != FR_OK)
        {
            return result;
        }

        if (received != sizeof(chunk))
        {
            return FR_INT_ERR;
        }

        memcpy(header->data, chunk, 4U);
        memcpy(&header->data_size, chunk + 4U, 4U);
    }

    if (header->data_size % WAV_BLOCK_ALIGN != 0U)
    {
        return FR_INT_ERR;
    }

    return FR_OK;
}


FRESULT wav_decoder_prepare(FIL *file)
{
    wav_decoder_close();

    if (file == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    wav_header_t header __attribute__((aligned(4)));

    FRESULT result = read_header(file, &header);

    if (result != FR_OK)
    {
        return result;
    }

    wav.file = file;
    wav.remaining = header.data_size;
    wav.sample_rate = header.sample_rate;

    return FR_OK;
}


FRESULT wav_decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
)
{
    if (result == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    result->bytes = 0U;
    result->end = false;

    if (wav.file == NULL)
    {
        return FR_NOT_READY;
    }

    if (wav.remaining == 0U)
    {
        result->end = true;
        return FR_OK;
    }

    if (destination == NULL ||
        capacity < WAV_BLOCK_ALIGN ||
        capacity % WAV_BLOCK_ALIGN != 0U)
    {
        return FR_INVALID_PARAMETER;
    }

    uint32_t bytes = capacity;

    if (bytes > wav.remaining)
    {
        bytes = wav.remaining;
    }

    UINT received = 0U;

    FRESULT error = f_read(
        wav.file,
        destination,
        (UINT)bytes,
        &received
    );

    if (error != FR_OK)
    {
        return error;
    }

    if (received != bytes)
    {
        return FR_INT_ERR;
    }

    wav.remaining -= received;

    result->bytes = received;
    result->end = wav.remaining == 0U;

    return FR_OK;
}


void wav_decoder_close(void)
{
    wav.file = NULL;
    wav.remaining = 0U;
    wav.sample_rate = 0U;
}

uint32_t wav_decoder_get_sample_rate(void)
{
    return wav.sample_rate;
}