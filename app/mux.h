#ifndef MUX_H
#define MUX_H

#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"

// =============================================================================
// КОНФИГУРАЦИЯ MUX1 (GPIOA)
// =============================================================================
#define MUX1_PORT           GPIOA
#define MUX1_A0_PIN         LL_GPIO_PIN_4
#define MUX1_A1_PIN         LL_GPIO_PIN_5
#define MUX1_A2_PIN         LL_GPIO_PIN_6

// =============================================================================
// КОНФИГУРАЦИЯ MUX2 (GPIOC)
// =============================================================================
#define MUX2_PORT           GPIOC
#define MUX2_A0_PIN         LL_GPIO_PIN_1
#define MUX2_A1_PIN         LL_GPIO_PIN_2
#define MUX2_A2_PIN         LL_GPIO_PIN_3

// =============================================================================
// ИМЕНОВАННЫЕ КОНСТАНТЫ КАНАЛОВ (соответствуют номерам x0-x7)
// =============================================================================
typedef enum {
    MUX_CHANNEL_X0 = 0,  // SIG_PLT1 (для обоих MUX)
    MUX_CHANNEL_X1 = 1,  // SIG_PLT2 (для обоих MUX)
    MUX_CHANNEL_X2 = 2,  // SIG_TRK (только для MUX2)
    MUX_CHANNEL_X3 = 3,  // Не используется
    MUX_CHANNEL_X4 = 4,  // PLR_IN (для обоих MUX)
    MUX_CHANNEL_X5 = 5,  // Не используется
    MUX_CHANNEL_X6 = 6,  // Не используется
    MUX_CHANNEL_X7 = 7   // Не используется
} mux_channel_t;

/**
 * @brief Выбор канала аналогового мультиплексора
 * @param mux_number Номер микросхемы (1 или 2)
 * @param channel Номер канала (0-7 или именованная константа MUX_CHANNEL_X*)
 */
void mux_select(uint8_t mux_number, mux_channel_t channel);

#endif