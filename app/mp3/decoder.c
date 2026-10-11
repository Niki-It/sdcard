
#include "decoder.h"

static uint8_t mp3Buffer[MP3_INPUT_BUFFER_SIZE];
static int16_t pcmBuffer[MP3_PCM_BUFFER_SAMPLES];

void MP3_TestDecode(void)
{
    FIL file;
    UINT bytesRead = 0;
    FRESULT res;

    HMP3Decoder decoder;
    MP3FrameInfo frameInfo;

    res = f_open(&file, "muse.mp3", FA_READ);

    if (res != FR_OK)
    {
        SEGGER_RTT_printf(0, "MP3 open error: %d\r\n", res);
        return;
    }

    // Проверяем ID3v2 в начале файла
    uint8_t header[10];

    res = f_read(&file, header, sizeof(header), &bytesRead);

    if (res != FR_OK || bytesRead != sizeof(header))
    {
        SEGGER_RTT_printf(0, "MP3 header read error\r\n");
        f_close(&file);
        return;
    }

    if (memcmp(header, "ID3", 3) == 0)
    {
        uint32_t tagSize =
            ((uint32_t)header[6] << 21) |
            ((uint32_t)header[7] << 14) |
            ((uint32_t)header[8] << 7) |
            ((uint32_t)header[9]);

        uint32_t offset = 10 + tagSize;

        if (header[3] == 4 && (header[5] & 0x10))
        {
            offset += 10;
        }

        if (offset > f_size(&file))
        {
            SEGGER_RTT_printf(0, "Invalid ID3 size\r\n");
            f_close(&file);
            return;
        }

        res = f_lseek(&file, offset);
    }
    else
    {
        res = f_lseek(&file, 0);
    }

    if (res != FR_OK)
    {
        f_close(&file);
        return;
    }

    // Читаем MP3-данные с SD
    res = f_read(&file,
                 mp3Buffer,
                 sizeof(mp3Buffer),
                 &bytesRead);

    f_close(&file);

    if (res != FR_OK || bytesRead == 0)
    {
        SEGGER_RTT_printf(0, "MP3 data read error\r\n");
        return;
    }

    SEGGER_RTT_printf(0, "Read %u MP3 bytes\r\n", bytesRead);

    // Создаём декодер
    decoder = MP3InitDecoder();

    if (decoder == NULL)
    {
        SEGGER_RTT_printf(0, "MP3 init error\r\n");
        return;
    }

    const unsigned char *readPtr = mp3Buffer;
    int bytesLeft = (int)bytesRead;
    int decoded = 0;

    while (bytesLeft > 4)
    {
        // Ищем начало MP3-кадра
        int offset = MP3FindSyncWord(readPtr, bytesLeft);

        if (offset < 0)
        {
            break;
        }

        readPtr += offset;
        bytesLeft -= offset;

        unsigned char *frameStart = readPtr;
        int previousBytesLeft = bytesLeft;

        // Декодируем один MP3-кадр
        int error = MP3Decode(decoder,
                              &readPtr,
                              &bytesLeft,
                              (short *)pcmBuffer,
                              0);

        if (error == ERR_MP3_NONE)
        {
            MP3GetLastFrameInfo(decoder, &frameInfo);

            SEGGER_RTT_printf(0,
                              "MP3: %d Hz, %d channels, %d samples\r\n",
                              frameInfo.samprate,
                              frameInfo.nChans,
                              frameInfo.outputSamps);

            
            SEGGER_RTT_printf(0, "PCM buffer:\r\n");

            for (int i = 0; i < frameInfo.outputSamps; i++)
            {
                SEGGER_RTT_printf(0, "%d ", pcmBuffer[i]);

                if ((i + 1) % 20 == 0)
                {
                    SEGGER_RTT_printf(0, "\r\n");
                }
            }

            if (frameInfo.outputSamps % 20 != 0)
            {
                SEGGER_RTT_printf(0, "\r\n");
            }

            decoded = 1;
        }

        if (error == ERR_MP3_INDATA_UNDERFLOW)
        {
            // Не хватает входных данных для полного кадра
            break;
        }

        if (bytesLeft == previousBytesLeft)
        {
            // При ошибке заголовка Helix мог не сдвинуть указатель
            readPtr = frameStart + 1;
            bytesLeft = previousBytesLeft - 1;
        }

        // При MAINDATA_UNDERFLOW Helix уже мог потребить кадр.
        // Продолжаем со следующего кадра, сохраняя состояние декодера.
    }

    if (!decoded)
    {
        SEGGER_RTT_printf(0, "No decoded MP3 frame in input buffer\r\n");
    }

    MP3FreeDecoder(decoder);
}