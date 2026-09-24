#pragma once

#include "stdint.h"
#include "stdbool.h"
#include <stm32f405xx.h>
#include "led_controller/led_controller.h"

// Ответ от пульта

extern RawResponce raw_response;
extern volatile uint8_t is_response_ready;
extern volatile uint8_t frame_number;
// ------------ ТЕСТ -------------------
extern volatile uint32_t rx_counter;

// Отправить команду
void send_command_sync(USART_TypeDef *USARTx, SetLedStateCommand cmd);


// ----------- Настройка периферии -------------------
void led_controller_nvic_init();
void led_controller_gpio_init();
void led_controller_usart_init(void);
void led_controller_clock_init(void);

void usart2_handler(void);
void uart4_handler(void);
void TIM6_Init(void);
extern void tim6_handler(void);

