#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stm32f4xx.h"

typedef struct {
    GPIO_TypeDef    *port;      // Порт пина
    uint32_t        pin;        // Обрабатываемый пин
    uint8_t         state;      // Обработанный уровень сигнала: 0 или 1.
    uint16_t        threshold;  // Количество последовательных подтверждений нового уровня.
    uint8_t         raw_state;  // Служебное
    uint16_t        counter;    // Служебное
} pin_debounce_t;

/*
 * Массив должен существовать всё время работы модуля.
 * Период задаётся в миллисекундах.
 * Начальное обработанное состояние всех пинов устанавливается в 0.
 * Высокий уровень при запуске проходит обычное подтверждение.
 */
void debounce_init(
    pin_debounce_t *pins,
    size_t count,
    uint32_t period_ms
);

/* Считывает текущие уровни всех зарегистрированных пинов. */
void pin_polling(void);

/* Обрабатывает считанные уровни с ограничением периодичности. */
void debounce_process(uint32_t current_tick);