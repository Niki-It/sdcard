#pragma once
#include "stdint.h"
#include "stm32f4xx.h"

void perf_counter_init(void);

uint32_t perf_counter_now(void);
uint32_t perf_counter_elapsed(uint32_t start);

uint32_t perf_counter_cycles_to_us(uint32_t cycles);





