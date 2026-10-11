#pragma once
#include "SEGGER_RTT.h"

void set_comdo_cycle_timeout(uint8_t timeout);
void process_button_combinations(const ButtonStatus* status);