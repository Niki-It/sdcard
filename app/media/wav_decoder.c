#include "wav_decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WAV_FORMAT_PCM          1U
#define WAV_CHANNELS            2U
#define WAV_SAMPLE_RATE         48000U
#define WAV_BITS_PER_SAMPLE     16U

#define WAV_PCM_FRAME_BYTES \
    (WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8U))

#define WAV_BYTE_RATE \
    (WAV_SAMPLE_RATE * WAV_PCM_FRAME_BYTES)


typedef struct
{
    FIL *file;
    uint32_t remaining_bytes;
    FRESULT error;
} wav_decoder_context_t;

static wav_decoder_context_t wav;


static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)(
        (uint16_t)data[0] |
        ((uint16_t)data[1] << 8)
    );
}


static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}


/*
 * Читает ровно указанное количество байтов.
 * Неожиданный физический EOF считаем повреждением файла.
 */
static FRESULT read_exact(
    FIL *file,
    void *destination,
    UINT bytes
)
{
    UINT bytes_read = 0U;

    FRESULT result = f_read(
        file,
        destination,
        bytes,
        &bytes_read
    );

    if (result != FR_OK)
    {
        return result;
    }

    if (bytes_read != bytes)
    {
        return FR_INT_ERR;
    }

    return FR_OK;
}


static FRESULT seek_exact(FIL *file, FSIZE_t offset)
{
    FRESULT result = f_lseek(file, offset);

    if (result != FR_OK)
    {
        return result;
    }

    if (f_tell(file) != offset)
    {
        return FR_INT_ERR;
    }

    return FR_OK;
}


FRESULT wav_decoder_prepare(FIL *file)
{
    /*
     * Сбрасываем собственное состояние.
     * Сам файл принадлежит audio_playback.
     */
    wav_decoder_close();

    if (file == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    FRESULT result = seek_exact(file, 0U);

    if (result != FR_OK)
    {
        return result;
    }

    uint8_t header[12];

    result = read_exact(file, header, sizeof(header));

    if (result != FR_OK)
    {
        return result;
    }

    if (memcmp(header, "RIFF", 4U) != 0 ||
        memcmp(header + 8U, "WAVE", 4U) != 0)
    {
        return FR_INVALID_PARAMETER;
    }

    uint32_t riff_bytes = read_le32(header + 4U);

    /*
     * Размер RIFF включает WAVE и последующие секции,
     * но не первые восемь байтов файла.
     *
     * Проверка до сложения защищает от переполнения
     * FSIZE_t при 32-битной конфигурации FatFs.
     */
    FSIZE_t file_bytes = f_size(file);

    if (riff_bytes < 4U ||
        file_bytes < sizeof(header) ||
        (FSIZE_t)riff_bytes > file_bytes - 8U)
    {
        return FR_INT_ERR;
    }

    FSIZE_t riff_end = (FSIZE_t)riff_bytes + 8U;

    bool format_found = false;
    bool data_found = false;

    FSIZE_t data_offset = 0U;
    uint32_t data_bytes = 0U;

    while (!format_found || !data_found)
    {
        FSIZE_t position = f_tell(file);

        if (position > riff_end ||
            riff_end - position < 8U)
        {
            /* Обязательная секция отсутствует. */
            return FR_INT_ERR;
        }

        uint8_t chunk_header[8];

        result = read_exact(
            file,
            chunk_header,
            sizeof(chunk_header)
        );

        if (result != FR_OK)
        {
            return result;
        }

        uint32_t chunk_bytes =
            read_le32(chunk_header + 4U);

        FSIZE_t chunk_offset = f_tell(file);

        if ((FSIZE_t)chunk_bytes > riff_end - chunk_offset)
        {
            return FR_INT_ERR;
        }

        FSIZE_t next_chunk =
            chunk_offset + (FSIZE_t)chunk_bytes;

        /*
         * Секции RIFF выровнены на два байта.
         * Байт выравнивания не входит в chunk_bytes.
         */
        if ((chunk_bytes & 1U) != 0U)
        {
            if (next_chunk >= riff_end)
            {
                return FR_INT_ERR;
            }

            next_chunk++;
        }

        if (memcmp(chunk_header, "fmt ", 4U) == 0)
        {
            if (format_found || chunk_bytes < 16U)
            {
                return FR_INT_ERR;
            }

            uint8_t format[16];

            result = read_exact(
                file,
                format,
                sizeof(format)
            );

            if (result != FR_OK)
            {
                return result;
            }

            uint16_t format_tag = read_le16(format);
            uint16_t channels = read_le16(format + 2U);
            uint32_t sample_rate = read_le32(format + 4U);
            uint32_t byte_rate = read_le32(format + 8U);
            uint16_t block_align = read_le16(format + 12U);
            uint16_t bits_per_sample = read_le16(format + 14U);

            if (format_tag != WAV_FORMAT_PCM ||
                channels != WAV_CHANNELS ||
                sample_rate != WAV_SAMPLE_RATE ||
                bits_per_sample != WAV_BITS_PER_SAMPLE)
            {
                return FR_INVALID_PARAMETER;
            }

            if (block_align != WAV_PCM_FRAME_BYTES ||
                byte_rate != WAV_BYTE_RATE)
            {
                return FR_INT_ERR;
            }

            format_found = true;
        }
        else if (memcmp(chunk_header, "data", 4U) == 0)
        {
            if (data_found)
            {
                return FR_INT_ERR;
            }

            /*
             * Последняя стереопара должна быть полной.
             */
            if (chunk_bytes % WAV_PCM_FRAME_BYTES != 0U)
            {
                return FR_INT_ERR;
            }

            data_offset = chunk_offset;
            data_bytes = chunk_bytes;
            data_found = true;
        }

        if (format_found && data_found)
        {
            break;
        }

        result = seek_exact(file, next_chunk);

        if (result != FR_OK)
        {
            return result;
        }
    }

    result = seek_exact(file, data_offset);

    if (result != FR_OK)
    {
        return result;
    }

    /*
     * Активируем декодер только после успешной подготовки.
     */
    wav.file = file;
    wav.remaining_bytes = data_bytes;
    wav.error = FR_OK;

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

    if (wav.error != FR_OK)
    {
        return wav.error;
    }

    /*
     * Включая пустую секцию data и повторный вызов
     * после завершения.
     */
    if (wav.remaining_bytes == 0U)
    {
        result->end = true;
        return FR_OK;
    }

    if (destination == NULL ||
        capacity == 0U ||
        capacity % WAV_PCM_FRAME_BYTES != 0U)
    {
        return FR_INVALID_PARAMETER;
    }

    uint32_t bytes = capacity;

    if (bytes > wav.remaining_bytes)
    {
        bytes = wav.remaining_bytes;
    }

    /*
     * f_read принимает UINT. На STM32 это 32 бита,
     * но проверяем, что преобразование не обрезало размер.
     */
    UINT requested = (UINT)bytes;

    if ((uint32_t)requested != bytes)
    {
        return FR_INVALID_PARAMETER;
    }

    FRESULT read_result = read_exact(
        wav.file,
        destination,
        requested
    );

    if (read_result != FR_OK)
    {
        /*
         * При ошибке резерв не публикуется.
         * Повторное чтение запрещаем до нового prepare.
         */
        wav.error = read_result;
        return read_result;
    }

    wav.remaining_bytes -= bytes;

    result->bytes = bytes;
    result->end = wav.remaining_bytes == 0U;

    return FR_OK;
}


void wav_decoder_close(void)
{
    /*
     * Не вызываем f_close: файл закрывает audio_playback.
     */
    wav.file = NULL;
    wav.remaining_bytes = 0U;
    wav.error = FR_OK;
}