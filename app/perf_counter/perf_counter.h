#pragma once

#include "stdint.h"
#include <core_cm4.h>
#include <system_stm32f4xx.h>

void perf_counter_init(void)
{
    // Обновляем значение частоты ядра после настройки clock.
    SystemCoreClockUpdate();

    // Разрешаем DWT.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Сбрасываем счётчик один раз при инициализации.
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __DSB();
    __ISB();
}

uint32_t perf_counter_now(void)
{
    return DWT->CYCCNT;
}

uint32_t perf_counter_elapsed(uint32_t start)
{
    return (uint32_t)(DWT->CYCCNT - start);
}

uint32_t perf_counter_cycles_to_us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / SystemCoreClock);
}

