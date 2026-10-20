#pragma once
#include "stdint.h"

typedef enum {
    VD1,
    VD2,
    VD3,
    VD4,
    VD5,
    VD6,
    VD7,
    VD8,
    VD9,
    LED_BLINKING_COUNT
} blinking_led_t;

void request_blinking(
    blinking_led_t led,
    uint16_t count,
    uint32_t period_ms
);

/* Вызывать на каждом проходе основного цикла. */
void process_leds(uint32_t current_tick);