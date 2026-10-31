#include "announce.h"
#include <stdio.h>
#include "ff.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include "stdio.h"

static uint16_t number = 1;

static audio_file_t find_audio_file(const char *folder, uint8_t number);
audio_file_t next_announce(
    const char *folder,
    const char *fallback
)
{
    audio_file_t file = {0};

    if (number == 0 || number > 255)
        return file;

    file = find_audio_file(folder, (uint8_t)number);

    if (file.result) {
        number++;
        return file;
    }

    if (number == 1) {
        file.result = true;
        file.delay = 0;
        snprintf(file.pathfile, sizeof(file.pathfile),
                 "%s", fallback);
    }

    // После fallback или отсутствующего файла
    // последовательность закончена
    number = 0;

    return file;
}

static uint32_t delay_start = 0;
static uint32_t delay_ms = 0;
static bool delay_active = false;

void reset_announce(void)
{
    number = 1;

    delay_start = 0;
    delay_ms = 0;
    delay_active = false;
}
// ------------------- Реализация задержки после воспроизведения ---------------

void start_announce_delay(uint32_t seconds, uint32_t uptime_ms)
{
    delay_start = uptime_ms;
    delay_ms = seconds * 1000U;
    delay_active = true;
}

bool check_announce_delay(uint32_t uptime_ms)
{
    if (!delay_active)
        return false;

    if ((uint32_t)(uptime_ms - delay_start) < delay_ms)
        return false;

    delay_start = 0;
    delay_ms = 0;
    delay_active = false;

    return true;
}

bool announce_delay_active(void)
{
    return delay_active;
}

// --------------------- Вспомогательные функции для поиска файла с порядковым номером ---------------------
static bool parse_audio_filename(const char *filename, uint8_t number, uint16_t *delay)
{
    if (strlen(filename) != AUDIO_FILENAME_LEN)
    {
        return false;
    }

    // Проверка порядкового номера
    for (uint8_t i = 0; i < 2; i++)
    {
        if (filename[i] < '0' || filename[i] > '9')
        {
            return false;
        }
    }

    uint8_t file_number = (filename[0] - '0') * 10 +
                          (filename[1] - '0');

    if (file_number != number)
    {
        return false;
    }

    // Проверка трех латинских букв
    for (uint8_t i = 2; i < 5; i++)
    {
        if (!((filename[i] >= 'A' && filename[i] <= 'Z') ||
              (filename[i] >= 'a' && filename[i] <= 'z')))
        {
            return false;
        }
    }

    // Проверка трех цифр паузы
    for (uint8_t i = 5; i < 8; i++)
    {
        if (filename[i] < '0' || filename[i] > '9')
        {
            return false;
        }
    }

    // Проверка расширения
    if (strcasecmp(&filename[8], ".mp3") != 0 &&
        strcasecmp(&filename[8], ".wav") != 0)
    {
        return false;
    }

    *delay = (filename[5] - '0') * 100 +
             (filename[6] - '0') * 10 +
             (filename[7] - '0');

    return true;
}

static audio_file_t find_audio_file(const char *folder, uint8_t number)
{
    audio_file_t result = {0};

    if(number > 99)
    {
        return result;
    }

    DIR dir;
    FILINFO info;

    FRESULT res = f_opendir(&dir, folder);

    if (res != FR_OK)
    {
        return result;
    }

    while (1)
    {
        res = f_readdir(&dir, &info);

        if (res != FR_OK || info.fname[0] == '\0')
        {
            break;
        }

        // это дирректория 
        if (info.fattrib & AM_DIR)
        {
            continue;
        }

        uint16_t delay = 0;

        if (!parse_audio_filename(info.fname, number, &delay))
        {
            continue;
        }

        int len = snprintf(
            result.pathfile,
            sizeof(result.pathfile),
            "%s/%s",
            folder,
            info.fname
        );

        if (len < 0 || (size_t)len >= sizeof(result.pathfile))
        {
            result.pathfile[0] = '\0';
            break;
        }

        result.delay = delay;

        if (f_closedir(&dir) == FR_OK)
        {
            result.result = true;
        }

        return result;
    }

    f_closedir(&dir);

    return result;
}
