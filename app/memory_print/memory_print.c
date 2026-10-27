#include "memory_print.h"
#include "SEGGER_RTT.h"


#ifdef MEMORY_LOG
static volatile char log_buffer[LOG_BUFFER_SIZE];
const char *log_lines[LOG_MAX_LINES];

volatile uint32_t log_pos = 0;
volatile uint32_t log_count = 0;
volatile uint32_t log_dropped = 0;

static uint8_t log_line_started = 0;

int _write(int file, char *ptr, int len)
{
    (void)file;

    if (!ptr || len <= 0)
        return 0;

    for (int i = 0; i < len; i++)
    {
        char c = ptr[i];

        if (c == '\r')
            continue;

        // Новую строку уже нельзя создать
        if (!log_line_started && log_count >= LOG_MAX_LINES)
        {
            log_dropped++;
            break;
        }

        // Резервируем байт для завершающего '\0'
        if (log_pos >= LOG_BUFFER_SIZE - 1)
        {
            log_dropped++;
            break;
        }

        if (!log_line_started)
        {
            log_lines[log_count++] =
                (const char *)&log_buffer[log_pos];

            log_line_started = 1;
        }

        if (c == '\n')
        {
            log_buffer[log_pos++] = '\0';
            log_line_started = 0;
        }
        else
        {
            log_buffer[log_pos++] = c;
            log_buffer[log_pos] = '\0';
        }
    }

    return len;
}
#endif

#ifdef SEGGER_LOG
int _write(int file, char *ptr, int len)
{
    (void)file;

    if (ptr == NULL || len <= 0)
    {
        return 0;
    }

    return (int)SEGGER_RTT_Write(0, ptr, (unsigned)len);
}
#endif