
#pragma once

#include <stdint.h>
#include "led_controller/led_controller.h"

#define BUTTON_COMBO_COUNT 10U

void button_combo_init(uint32_t timeout_ms);
void process_button_combinations(const ButtonStatus *status, uint32_t uptime_ms);

int combo_has(uint16_t mask, const ButtonEvent events[BUTTON_COMBO_COUNT], uint8_t button, ButtonEvent event);

void flush_combo(uint8_t buttons_count, uint16_t buttons_mask, const ButtonEvent events[BUTTON_COMBO_COUNT]);
