#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stm32f405xx.h>

#include "led_controller/led_controller.h"

/* Обмен с каждым USART имеет независимый контекст приёма. */
void send_command_sync(USART_TypeDef *USARTx, SetLedStateCommand cmd);
// добавление 1 байта, на случай если пульт подключен во время передачи и его счетчик считает часть команды первым байтом
void send_command_sync_padded(USART_TypeDef *USARTx, SetLedStateCommand cmd);
bool led_raw_take_response(USART_TypeDef *USARTx, RawResponce *response);
void led_raw_cancel_response(USART_TypeDef *USARTx);

/* Настройка периферии. */
void led_controller_nvic_init(void);
void led_controller_gpio_init(void);
void led_controller_usart_init(void);
void led_controller_clock_init(void);
void TIM6_Init(void);

void usart2_handler(void);
void uart4_handler(void);

typedef struct
{
    uint32_t rx_bytes;
    uint32_t packets;
    uint32_t ore_errors;
    uint32_t fe_errors;
    uint32_t ne_errors;
} LedRawDiagnostics;

void led_raw_get_diagnostics(USART_TypeDef *USARTx, LedRawDiagnostics *diagnostics);