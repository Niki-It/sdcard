
#include "mp3_decoder.h"

#define MP3_FILE_NAME          "muse.mp3"
#define MP3_BUFFER_SIZE        4096
#define PCM_BUFFER_SAMPLES     2304
#define MP3_PRINT_ALL_PCM      1

typedef struct
{
    FIL *file;

    uint8_t buffer[MP3_BUFFER_SIZE];

    uint32_t bytes_valid;
    uint8_t eof;

} mp3_stream_t;

static mp3_stream_t mp3_stream;
static int16_t pcm_buffer[PCM_BUFFER_SAMPLES];

/// Основная функция обработки MP3-файла.
static int skip_header(FIL *file);
static int fill_mp3_buffer(mp3_stream_t *stream);
static int read_frame(mp3_stream_t *stream,
                      HMP3Decoder decoder,
                      MP3FrameInfo *frame_info);
static void consume_mp3_bytes(mp3_stream_t *stream, uint32_t count);
static void handle_frame(const MP3FrameInfo *frame_info);

void process_mp3(void)
{
    FIL file;

    FRESULT res = f_open(&file, MP3_FILE_NAME, FA_READ);

    if (res != FR_OK)
    {
        SEGGER_RTT_printf(0, "MP3 open error: %d\r\n", res);
        return;
    }

    if (skip_header(&file) != 0)
    {
        SEGGER_RTT_printf(0, "MP3 header error\r\n");
        f_close(&file);
        return;
    }

    HMP3Decoder decoder = MP3InitDecoder();

    if (decoder == NULL)
    {
        SEGGER_RTT_printf(0, "MP3 decoder init error\r\n");
        f_close(&file);
        return;
    }

    mp3_stream.file = &file;
    mp3_stream.bytes_valid = 0;
    mp3_stream.eof = 0;

    MP3FrameInfo frame_info;

    uint32_t frame_count = 0;

    while (1)
    {
        int result = read_frame(&mp3_stream,
                                decoder,
                                &frame_info);

        if (result == 0)
        {
            SEGGER_RTT_printf(0, "MP3 end of file\r\n");
            break;
        }

        if (result < 0)
        {
            SEGGER_RTT_printf(0, "MP3 processing stopped\r\n");
            break;
        }

        frame_count++;

        SEGGER_RTT_printf(0,
                          "Frame %lu: ",
                          (unsigned long)frame_count);

        handle_frame(&frame_info);
    }

    MP3FreeDecoder(decoder);
    f_close(&file);
}
/// Читает и декодирует один MP3-кадр.
///
/// Возвращает:
///  1 — кадр успешно декодирован, PCM готов
///  0 — конец файла
/// -1 — ошибка чтения или декодирования
// Дочитывает MP3-данные в свободную часть входного буфера.
static int fill_mp3_buffer(mp3_stream_t *stream)
{
    if (stream->eof || stream->bytes_valid == MP3_BUFFER_SIZE)
    {
        return 0;
    }

    UINT bytes_read = 0;

    UINT free_space = MP3_BUFFER_SIZE - stream->bytes_valid;

    FRESULT res = f_read(stream->file,
                         stream->buffer + stream->bytes_valid,
                         free_space,
                         &bytes_read);

    if (res != FR_OK)
    {
        SEGGER_RTT_printf(0, "MP3 read error: %d\r\n", res);
        return -1;
    }

    stream->bytes_valid += bytes_read;

    if (bytes_read < free_space)
    {
        stream->eof = 1;
    }

    return 0;
}
static int read_frame(mp3_stream_t *stream,
                      HMP3Decoder decoder,
                      MP3FrameInfo *frame_info)
{
    while (1)
    {
        if (fill_mp3_buffer(stream) != 0)
        {
            return -1;
        }

        if (stream->bytes_valid < 4)
        {
            return stream->eof ? 0 : -1;
        }

        // Ищем начало MP3-кадра.
        int sync_offset = MP3FindSyncWord(
            stream->buffer,
            (int)stream->bytes_valid
        );

        if (sync_offset < 0)
        {
            if (stream->eof)
            {
                return 0;
            }

            // Сохраняем последние 3 байта:
            // синхрослово может пересекать границу чтений.
            consume_mp3_bytes(stream,
                              stream->bytes_valid - 3);

            continue;
        }

        if (sync_offset > 0)
        {
            consume_mp3_bytes(stream, (uint32_t)sync_offset);
        }

        // input_ptr указывает на очередной MP3-кадр.
        const unsigned char *input_ptr = stream->buffer;

        // Сколько MP3-байтов доступно декодеру.
        unsigned int bytes_left = (int)stream->bytes_valid;

        int error = MP3Decode(decoder,
                              &input_ptr,
                              &bytes_left,
                              pcm_buffer,
                              0);

        // Helix изменяет input_ptr и bytes_left.
        uint32_t bytes_consumed =
            stream->bytes_valid - (uint32_t)bytes_left;

        if (error == ERR_MP3_INDATA_UNDERFLOW)
        {
            // Входной буфер содержит неполный кадр.
            // Данные не выбрасываем, пробуем дочитать.
            if (stream->eof)
            {
                return 0;
            }

            if (stream->bytes_valid == MP3_BUFFER_SIZE)
            {
                SEGGER_RTT_printf(0,
                                  "MP3 input buffer too small or invalid frame\r\n");
                return -1;
            }

            continue;
        }

        if (bytes_consumed > 0)
        {
            consume_mp3_bytes(stream, bytes_consumed);
        }
        else
        {
            // Ошибка без продвижения по входным данным.
            // Пропускаем байт для повторной синхронизации.
            consume_mp3_bytes(stream, 1);
        }

        if (error == ERR_MP3_MAINDATA_UNDERFLOW)
        {
            // Не хватает данных bit reservoir предыдущих кадров.
            // Продолжаем декодировать следующие кадры.
            continue;
        }

        if (error != ERR_MP3_NONE)
        {
            SEGGER_RTT_printf(0,
                              "MP3 decode error: %d\r\n",
                              error);

            continue;
        }

        MP3GetLastFrameInfo(decoder, frame_info);

        if (frame_info->outputSamps <= 0 ||
            frame_info->outputSamps > PCM_BUFFER_SAMPLES)
        {
            SEGGER_RTT_printf(0, "Invalid PCM size\r\n");
            return -1;
        }

        return 1;
    }
}
// Удаляет обработанные байты из начала входного буфера.
static void consume_mp3_bytes(mp3_stream_t *stream, uint32_t count)
{
    if (count >= stream->bytes_valid)
    {
        stream->bytes_valid = 0;
        return;
    }

    stream->bytes_valid -= count;

    memmove(stream->buffer,
            stream->buffer + count,
            stream->bytes_valid);
}
/// Пропускает ID3v2, если он присутствует.
static int skip_header(FIL *file)
{
    uint8_t header[10];

    UINT bytes_read = 0;

    FRESULT res = f_read(file, header, sizeof(header), &bytes_read);

    if (res != FR_OK)
    {
        return -1;
    }

    if (bytes_read != sizeof(header))
    {
        return -1;
    }

    if (memcmp(header, "ID3", 3) != 0)
    {
        return (f_lseek(file, 0) == FR_OK) ? 0 : -1;
    }

    // Поддерживаем ID3v2.2, ID3v2.3 и ID3v2.4.
    if (header[3] < 2 || header[3] > 4 ||
        header[4] == 0xFF)
    {
        return -1;
    }

    for (uint32_t i = 6; i < 10; i++)
    {
        if (header[i] & 0x80)
        {
            return -1;
        }
    }

    uint32_t tag_size =
        ((uint32_t)header[6] << 21) |
        ((uint32_t)header[7] << 14) |
        ((uint32_t)header[8] << 7)  |
        ((uint32_t)header[9]);

    uint32_t offset = 10 + tag_size;

    if (header[3] == 4 && (header[5] & 0x10))
    {
        offset += 10;
    }

    if ((FSIZE_t)offset > f_size(file))
    {
        return -1;
    }

    SEGGER_RTT_printf(0,
                      "Skip ID3: %lu bytes\r\n",
                      (unsigned long)offset);

    return (f_lseek(file, offset) == FR_OK) ? 0 : -1;
}

static void handle_frame(const MP3FrameInfo *frame_info)
{
    uint32_t pcm_samples = (uint32_t)frame_info->outputSamps;

    if (frame_info->nChans == 1)
    {
        // Моно -> стерео прямо в pcm_buffer.
        if (pcm_samples * 2 > PCM_BUFFER_SAMPLES)
        {
            SEGGER_RTT_printf(0, "Mono PCM buffer overflow\r\n");
            return;
        }

        for (int32_t i = (int32_t)pcm_samples - 1; i >= 0; i--)
        {
            int16_t sample = pcm_buffer[i];

            pcm_buffer[i * 2]     = sample;
            pcm_buffer[i * 2 + 1] = sample;
        }

        pcm_samples *= 2;
    }
    else if (frame_info->nChans != 2)
    {
        SEGGER_RTT_printf(0, "Unsupported channel count\r\n");
        return;
    }

    uint32_t nonzero = 0;

    for (uint32_t i = 0; i < pcm_samples; i++)
    {
        if (pcm_buffer[i] != 0)
        {
            nonzero++;
        }
    }

    SEGGER_RTT_printf(0,
                      "MP3: %d Hz, %d ch, %d kbps, PCM=%lu, nonzero=%lu\r\n",
                      frame_info->samprate,
                      frame_info->nChans,
                      frame_info->bitrate / 1000,
                      (unsigned long)pcm_samples,
                      (unsigned long)nonzero);

#if MP3_PRINT_ALL_PCM

    for (uint32_t i = 0; i < pcm_samples; i++)
    {
        SEGGER_RTT_printf(0, "%d ", pcm_buffer[i]);

        if ((i + 1) % 20 == 0)
        {
            SEGGER_RTT_printf(0, "\r\n");
        }
    }

    if (pcm_samples % 20 != 0)
    {
        SEGGER_RTT_printf(0, "\r\n");
    }

#endif

    // В будущем:
    // i2s_enqueue_pcm(pcm_buffer, pcm_samples);
}
