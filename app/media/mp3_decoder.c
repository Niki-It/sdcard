#include "mp3_decoder.h"
#include "mp3dec.h"

#include <stdint.h>
#include <string.h>

#define MP3_INPUT_BYTES    4096U
#define MP3_PCM_BYTES      4608U
#define MP3_FRAMES_PER_CALL 4U

_Static_assert(sizeof(short) == 2U, "Helix requires 16-bit short");

static struct
{
    FIL *file;
    HMP3Decoder decoder;
    uint8_t input[MP3_INPUT_BYTES];
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


FRESULT mp3_decoder_prepare(FIL *file)
{
    mp3_decoder_close();

    if (file == NULL)
        return FR_INVALID_PARAMETER;

    FRESULT error = seek_to(file, 0U);
    if (error != FR_OK)
        return error;

    uint8_t header[10];
    UINT received = 0U;

    error = f_read(file, header, sizeof(header), &received);
    if (error != FR_OK)
        return error;
    if (received != sizeof(header))
        return FR_INT_ERR;

    FSIZE_t offset = 0U;

    if (memcmp(header, "ID3", 3U) == 0)
    {
        if (header[3] < 2U || header[3] > 4U ||
            header[4] == 0xFFU ||
            ((header[6] | header[7] |
              header[8] | header[9]) & 0x80U))
        {
            return FR_INVALID_PARAMETER;
        }

        uint32_t size =
            ((uint32_t)header[6] << 21) |
            ((uint32_t)header[7] << 14) |
            ((uint32_t)header[8] << 7) |
            header[9];

        offset = 10U + size;

        /* У ID3v2.4 может быть дополнительный footer. */
        if (header[3] == 4U && (header[5] & 0x10U))
            offset += 10U;
    }

    error = seek_to(file, offset);
    if (error != FR_OK)
        return error;

    mp3.decoder = MP3InitDecoder();
    if (mp3.decoder == NULL)
        return FR_NOT_ENOUGH_CORE; 

    mp3.file = file;

    error = refill();
    if (error != FR_OK)
    {
        mp3_decoder_close();
        return error;
    }

    if (mp3.available < 6U)
    {
        mp3_decoder_close();
        return FR_INT_ERR;
    }

    MP3FrameInfo info;

    if (MP3FindSyncWord(mp3.input, 2) != 0 ||
        MP3GetNextFrameInfo(mp3.decoder, &info, mp3.input) != 0)
    {
        mp3_decoder_close();
        return FR_INT_ERR;
    }

    if (info.version != MPEG1 ||
        info.layer != 3 ||
        info.bitsPerSample != 16 ||
        (info.nChans != 1 && info.nChans != 2) ||
        info.outputSamps != 1152 * info.nChans ||
        (info.samprate != 32000 &&
        info.samprate != 44100 &&
        info.samprate != 48000) ||
        info.bitrate <= 0)
    {
        mp3_decoder_close();
        return FR_INVALID_PARAMETER;
    }

    uint32_t frame_bytes =
        144U * (uint32_t)info.bitrate /
        (uint32_t)info.samprate +
        ((mp3.input[2] >> 1) & 1U);

    if (frame_bytes > mp3.available)
    {
        mp3_decoder_close();
        return FR_INT_ERR;
    }

    mp3.sample_rate = (uint32_t)info.samprate;

    /*
    * Первый кадр остаётся в input.
    * read_pcm() декодирует его обычным порядком.
    */
    return FR_OK;
}


FRESULT mp3_decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
)
{
    if (result == NULL)
        return FR_INVALID_PARAMETER;

    result->bytes = 0U;
    result->end = false;

    if (mp3.decoder == NULL)
        return FR_NOT_READY;

    if (mp3.eof && mp3.available == 0U)
    {
        result->end = true;
        return FR_OK;
    }

    if (destination == NULL ||
        ((uintptr_t)destination % _Alignof(short)) != 0U ||
        capacity < MP3_PCM_BYTES)
    {
        return FR_INVALID_PARAMETER;
    }

    FRESULT error = refill();
    if (error != FR_OK)
        return error;

    for (uint32_t frame = 0U; frame < MP3_FRAMES_PER_CALL; frame++)
    {
        uint8_t *input = mp3.input + mp3.position;

        /* ID3v1 занимает последние 128 байт файла. */
        if (mp3.eof && mp3.available == 128U &&
            memcmp(input, "TAG", 3U) == 0)
        {
            mp3.available = 0U;
        }

        if (mp3.available == 0U)
            break;

        if (capacity - result->bytes < MP3_PCM_BYTES)
            break;

        /* Заголовок плюс возможные два байта CRC. */
        if (mp3.available < 6U)
        {
            if (mp3.eof)
                return FR_INT_ERR;
            break;
        }

        MP3FrameInfo info;

        
        if (MP3FindSyncWord(input, 2) != 0 ||
            MP3GetNextFrameInfo(mp3.decoder, &info, input) != 0)
        {
            return FR_INT_ERR;
        }

                if (info.version != MPEG1 || info.layer != 3 ||
            info.bitsPerSample != 16 ||
            (info.nChans != 1 && info.nChans != 2) ||
            info.outputSamps != 1152 * info.nChans)
        {
            return FR_INVALID_PARAMETER;
        }

        if ((uint32_t)info.samprate != mp3.sample_rate)
        {
            return FR_INVALID_PARAMETER;
        }

        /*
         * Для free-format bitrate нельзя определить
         * размер кадра этой формулой.
         */
        if (info.bitrate <= 0)
        {
            return FR_INVALID_PARAMETER;
        }

        uint32_t frame_bytes =
            144U * (uint32_t)info.bitrate /
            (uint32_t)info.samprate +
            ((input[2] >> 1) & 1U);

        if (frame_bytes > MP3_INPUT_BYTES)
        {
            return FR_INVALID_PARAMETER;
        }

        /* Helix получает только полностью загруженный кадр. */
        if (mp3.available < frame_bytes)
        {
            if (mp3.eof)
                return FR_INT_ERR;
            break;
        }

        short *pcm = (short *)(destination + result->bytes);
        const unsigned char *next = input;
        size_t remaining = (size_t)frame_bytes;

        int status = MP3Decode(
            mp3.decoder,
            &next,
            &remaining,
            pcm,
            0
        );

        if (status != ERR_MP3_NONE &&
            status != ERR_MP3_MAINDATA_UNDERFLOW)
        {
            return FR_INT_ERR;
        }

        if (remaining != 0 || next != input + frame_bytes)
            return FR_INT_ERR;

        mp3.position += frame_bytes;
        mp3.available -= frame_bytes;

        /* Кадр пополнил bit reservoir, но PCM ещё не получен. */
        if (status == ERR_MP3_MAINDATA_UNDERFLOW)
            continue;

        if (info.nChans == 1)
        {
            /* Расширяем с конца, чтобы не затереть исходное моно. */
            for (int i = 1151; i >= 0; i--)
            {
                short sample = pcm[i];
                pcm[2 * i] = sample;
                pcm[2 * i + 1] = sample;
            }
        }

        result->bytes += MP3_PCM_BYTES;
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
    if (mp3.decoder != NULL)
        MP3FreeDecoder(mp3.decoder);

    mp3.decoder = NULL;
    mp3.file = NULL;
    mp3.position = 0U;
    mp3.available = 0U;
    mp3.sample_rate = 0U;
    mp3.eof = false;
}