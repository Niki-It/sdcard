
#pragma once

#include <stdint.h>
#include "led_controller/led_controller.h"

#define BUTTON_COMBO_COUNT 8U

void button_combo_init(uint32_t timeout_ms);
void process_button_combinations(const ButtonStatus *status, uint32_t uptime_ms);


// Weak-функция, которую можно переопределить в другом .c файле
void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT]);
