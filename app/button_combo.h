
#pragma once

#include <stdint.h>
#include "led_controller/led_controller.h"

#define BUTTON_COMBO_COUNT 8U

void set_combo_cycle_timeout(uint8_t timeout);

void process_button_combinations(const ButtonStatus *status);

// Weak-функция, которую можно переопределить в другом .c файле
void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT]);
