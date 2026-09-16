#include "events_buffer.h"

LedEvents events_buffer[EVENTS_BUFFER_SIZE];
volatile uint8_t events_count = 0;

void events_buffer_push(const LedEvents events)
{
    if (events_count >= EVENTS_BUFFER_SIZE) {
        return;  // Буфер полон — отбрасываем новый пакет
    }

    events_buffer[events_count] = events;
    events_count++;
}