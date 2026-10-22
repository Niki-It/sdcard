#include "mp3_decoder.h"

#include <stdint.h>
#include <string.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define MP3_INPUT_BYTES    4096U
#define MP3_PCM_BYTES      4608U
#define MP3_FRAMES_PER_CALL 4U

static struct
{
    FIL *file;
    mp3dec_t decoder;
    uint8_t input[MP3_INPUT_BYTES];
    FSIZE_t input_offset;
    uint32_t position;
    uint32_t available;
    uint32_t sample_rate;
    bool eof;
} mp3;

static FRESULT seek_to(FIL *file, FSIZE_t offset)
{
    if (offset > f_size(file))
        return FR_INT_ERR;

    FRESULT error = f_lseek(file, offset);

    if (error != FR_OK)
        return error;

    return f_tell(file) == offset ? FR_OK : FR_INT_ERR;
}


static FRESULT refill(void)
{
    if (mp3.eof)
        return FR_OK;

    memmove(
        mp3.input,
        mp3.input + mp3.position,
        mp3.available
    );
    mp3.input_offset += mp3.position;
    mp3.position = 0U;

    UINT requested = sizeof(mp3.input) - mp3.available;
    UINT received = 0U;

    if (requested == 0U)
        return FR_OK;

    FRESULT error = f_read(
        mp3.file,
        mp3.input + mp3.available,
        requested,
        &received
    );

    if (error != FR_OK)
        return error;

    mp3.available += received;
    mp3.eof = f_tell(mp3.file) == f_size(mp3.file);

    if (received < requested && !mp3.eof)
        return FR_INT_ERR;

    return FR_OK;
}


#define MP3_ID3_HEADER_BYTES  10U

FRESULT mp3_decoder_prepare(FIL *file)
{
    mp3_decoder_close();

    if (file == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    FRESULT error = seek_to(file, 0U);

    if (error != FR_OK)
    {
        return error;
    }

    /************************************************************
     * ПРОПУСК ПОЛНОГО ТЕГА ID3 ПЕРЕД ПЕРВЫМ MP3-ФРЕЙМОМ
     ************************************************************/

    uint8_t header[MP3_ID3_HEADER_BYTES];
    UINT received = 0U;

    error = f_read(file, header, sizeof(header), &received);

    if (error != FR_OK)
    {
        return error;
    }

    if (received != sizeof(header))
    {
        return FR_INT_ERR;
    }

    FSIZE_t offset = 0U;

    if (memcmp(header, "ID3", 3U) == 0)
    {
        if (header[3] < 2U || header[3] > 4U ||
            header[4] == 0xFFU ||
            ((header[6] | header[7] |
              header[8] | header[9]) & 0x80U) != 0U)
        {
            return FR_INVALID_PARAMETER;
        }

        uint32_t size =
            ((uint32_t)header[6] << 21) |
            ((uint32_t)header[7] << 14) |
            ((uint32_t)header[8] << 7) |
            header[9];

        offset = (FSIZE_t)MP3_ID3_HEADER_BYTES + size;

        /* ID3v2.4 может содержать дополнительный footer. */
        if (header[3] == 4U && (header[5] & 0x10U) != 0U)
        {
            offset += MP3_ID3_HEADER_BYTES;
        }
    }

    /*
     * Если ID3 отсутствует, возвращаемся к началу файла.
     * Если присутствует — переходим за весь тег.
     */
    error = seek_to(file, offset);
    if (error != FR_OK)
    {
        return error;
    }

    /************************************************************
     * КОНЕЦ ПРОПУСКА ПОЛНОГО ТЕГА ID3 ПЕРЕД ПЕРВЫМ MP3-ФРЕЙМОМ
    ************************************************************/

    mp3.file = file;
    mp3.input_offset = f_tell(file);
    mp3dec_init(&mp3.decoder);

    error = refill();

    if (error != FR_OK)
    {
        mp3_decoder_close();
        return error;
    }

    mp3dec_frame_info_t info = {0};

    int samples = mp3dec_decode_frame(
        &mp3.decoder,
        mp3.input + mp3.position,
        (int)mp3.available,
        NULL,
        &info
    );

    /*
     * При pcm == NULL положительный результат означает,
     * что найден полный фрейм и определены его параметры.
     */
    if (samples <= 0)
    {
        mp3_decoder_close();
        return FR_INT_ERR;
    }

    if (info.layer != 3 ||
        (info.channels != 1 && info.channels != 2) ||
        info.hz <= 0)
    {
        mp3_decoder_close();
        return FR_INVALID_PARAMETER;
    }

    /*
     * Пропускаем только байты перед фреймом.
     * Сам первый фрейм остаётся во входном буфере.
     */
    mp3.position += (uint32_t)info.frame_offset;
    mp3.available -= (uint32_t)info.frame_offset;
    mp3.sample_rate = (uint32_t)info.hz;

    /*
     * Просмотр заголовка не заполняет bit reservoir.
     * Реальное декодирование начнём с первого фрейма.
     */
    mp3dec_init(&mp3.decoder);

    return FR_OK;
}


_Static_assert(
    sizeof(mp3d_sample_t) == 2U,
    "MP3 decoder requires signed 16-bit PCM"
);

FRESULT mp3_decoder_read_pcm(
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

    if (mp3.file == NULL)
    {
        return FR_NOT_READY;
    }

    if (mp3.eof && mp3.available == 0U)
    {
        result->end = true;
        return FR_OK;
    }

    if (destination == NULL ||
        ((uintptr_t)destination % _Alignof(mp3d_sample_t)) != 0U ||
        capacity < MP3_PCM_BYTES ||
        capacity % 4U != 0U)
    {
        return FR_INVALID_PARAMETER;
    }

    for (uint32_t frame = 0U; frame < MP3_FRAMES_PER_CALL; frame++)
    {
        // недостаточно место для еще одного раскодировано кадра mp3
        if (capacity - result->bytes < MP3_PCM_BYTES)
        {
            break;
        }

        FRESULT error = refill();

        if (error != FR_OK)
        {
            return error;
        }

        // указываем на первый необработанный байт в input
        uint8_t *input = mp3.input + mp3.position;

        /*
         * Убираем конечный ID3v1 из доступных данных,
         * чтобы minimp3 видел конец аудиопотока без тега.
         */
        if (mp3.eof && mp3.available >= 128U &&
            memcmp(input + mp3.available - 128U, "TAG", 3U) == 0)
        {
            mp3.available -= 128U;
        }

        if (mp3.available == 0U)
        {
            break;
        }

        // определяем адрес записи следующего PCM
        mp3d_sample_t *pcm =
            (mp3d_sample_t *)(destination + result->bytes);

        mp3dec_frame_info_t info = {0};

        // TODO: Проверка для дэбага
        // SEGGER_RTT_printf(
        //     0,
        //     "MP3: file=%lu input=%lu decode=%lu pos=%lu available=%lu\r\n",
        //     (unsigned long)f_tell(mp3.file),
        //     (unsigned long)mp3.input_offset,
        //     (unsigned long)(mp3.input_offset + mp3.position),
        //     (unsigned long)mp3.position,
        //     (unsigned long)mp3.available
        // );
        int samples = mp3dec_decode_frame(
            &mp3.decoder,
            input,
            (int)mp3.available,
            pcm,
            &info
        );
        // TODO: Проверка для дэбага
        // if (samples == 0 && info.hz == 0)
        // {
        //     SEGGER_RTT_printf(
        //         0,
        //         "MP3 NO FRAME: offset=%lu available=%lu frame_bytes=%d\r\n",
        //         (unsigned long)(mp3.input_offset + mp3.position),
        //         (unsigned long)mp3.available,
        //         info.frame_bytes
        //     );

        //     uint32_t count = mp3.available;

        //     if (count > 16U)
        //     {
        //         count = 16U;
        //     }

        //     for (uint32_t i = 0U; i < count; i++)
        //     {
        //         SEGGER_RTT_printf(
        //             0,
        //             "%02X ",
        //             (unsigned int)input[i]
        //         );
        //     }

        //     SEGGER_RTT_printf(0, "MP3 BOUNDARY:\r\n");

        //     for (uint32_t i = 1037U; i < 1053U; i++)
        //     {
        //         SEGGER_RTT_printf(
        //             0,
        //             "%lu:%02X ",
        //             (unsigned long)i,
        //             (unsigned int)input[i]
        //         );
        //     }
        //     SEGGER_RTT_printf(0, "\r\n");

        //     return FR_INT_ERR;
        // }

        /*
         * Ожидаем фрейм прямо в текущей позиции.
         * Автоматическое восстановление после повреждения
         * потока здесь пока не реализуем.
         */
        if (info.frame_bytes <= 0 ||
            (uint32_t)info.frame_bytes > mp3.available ||
            info.frame_offset != 0)
        {
            return FR_INT_ERR;
        }

        // Layer 3 подходим под 44.1 32 48 kHZ
        if (info.layer != 3 ||
            (info.channels != 1 && info.channels != 2) ||
            info.hz <= 0 ||
            (uint32_t)info.hz != mp3.sample_rate)
        {
            return FR_INVALID_PARAMETER;
        }

        if (samples < 0 ||
            samples > MINIMP3_MAX_SAMPLES_PER_FRAME / 2)
        {
            return FR_INT_ERR;
        }

        mp3.position += (uint32_t)info.frame_bytes;
        mp3.available -= (uint32_t)info.frame_bytes;

        /*
         * Фрейм мог пополнить bit reservoir без выдачи PCM.
         * Состояние декодера сохраняем.
         */
        if (samples == 0)
        {
            continue;
        }

        if (info.channels == 1)
        {
            /* Расширяем mono в stereo с конца массива. */
            for (int i = samples - 1; i >= 0; i--)
            {
                mp3d_sample_t sample = pcm[i];

                pcm[2 * i] = sample;
                pcm[2 * i + 1] = sample;
            }
        }

        /*
         * samples — число сэмплов на канал.
         * Выход всегда stereo PCM16: четыре байта на пару.
         */
        result->bytes +=
            (uint32_t)samples * 2U * sizeof(mp3d_sample_t);
    }

    result->end = mp3.eof && mp3.available == 0U;

    return FR_OK;
}

uint32_t mp3_decoder_get_sample_rate(void)
{
    return mp3.sample_rate;
}


void mp3_decoder_close(void)
{
    memset(&mp3.decoder, 0, sizeof(mp3.decoder));

    mp3.file = NULL;
    mp3.position = 0U;
    mp3.available = 0U;
    mp3.sample_rate = 0U;
    mp3.eof = false;
    mp3.input_offset = 0U;
}