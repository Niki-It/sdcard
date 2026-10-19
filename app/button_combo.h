#pragma once
#include "SEGGER_RTT.h"

void set_comdo_cycle_timeout(uint8_t timeout);
void process_button_combinations(const ButtonStatus* status);
void flush_combo(void);

extern LedEvent combo_events[8]; 
extern uint8_t  active_buttons_mask;