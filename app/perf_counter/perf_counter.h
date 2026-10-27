#pragma once
#include "stdint.h"
#include "stm32f4xx.h"
#include "stdio.h"
#include "stdbool.h"
#include "string.h"

typedef enum
{
    PERF_LOG_FIRST,
    PERF_LOG_LAST,
    PERF_LOG_MAX,
    PERF_LOG_MIN
} perf_log_policy_t;

typedef struct
{
    uint32_t sequence;
    uint32_t duration_us;
} perf_sample_t;

void perf_log_init(perf_log_policy_t policy);
void perf_log_start(void);
void perf_log_finish(void);
void perf_log_print(void);

void perf_counter_init(void);

uint32_t perf_counter_now(void);
uint32_t perf_counter_elapsed(uint32_t start);

uint32_t perf_counter_cycles_to_us(uint32_t cycles);





