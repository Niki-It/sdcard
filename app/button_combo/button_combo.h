
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "string.h"
#include "source.h"
#include "led_controller/led_controller.h"

#define BUTTON_COMBO_COUNT 10U

#define B_ALERT  0U  // S1  !
#define B_PRM    1U  // S2  ПРМ
#define B_GGS    2U  // S3  ГГС1
#define B_F1     3U  // S4  Ф1
#define B_GGS2   4U  // S5  ГГС2
#define B_F2     5U  // S6  Ф2
#define B_APB    6U  // S7  АПБ
#define B_PLUS   7U  // S8  +
#define B_MINUS  8U  // S9  -
#define B_PRD    9U  // S10 ПРД

typedef struct
{
    source_t source;
    uint8_t count;
    uint16_t mask;
    ButtonEvent events[BUTTON_COMBO_COUNT];
} FlushContext;

void button_combo_init(uint32_t timeout_ms);
void process_button_combinations(const ButtonStatus *status, uint32_t uptime_ms);

static inline bool context_has(FlushContext context, uint8_t button, ButtonEvent event)
{
    if (button >= BUTTON_COMBO_COUNT)
    {
        return false;
    }

    return (context.mask & (1U << button)) != 0U &&
           context.events[button] == event;
}

void flush_combo(FlushContext context);
