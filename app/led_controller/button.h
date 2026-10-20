#pragma once
#include <stdbool.h>
#include "stdint.h"

#define BUTTON_DEBOUNCE_MS 15u

typedef struct {
    bool prev;
    bool curr;
    uint32_t since_ms;
    bool pending;
} Button;

extern volatile uint32_t uptime_ms;

void next(Button *btn, bool value);

bool pressed(Button *btn);
bool released(Button *btn);