#pragma once 
#include "stdint.h"
#include "stm32f4xx_ll_usart.h"
#include "led_uart/led_uart.h"

//extern volatile uint8_t frame_number;
void usart2_handler(void);

void send_command(USART_TypeDef *USARTx, SetLedStateCommand cmd);
void usart2_handler(void);
void uart4_handler();
void send_sync(USART_TypeDef *USARTx, const uint8_t* raw, uint8_t len);

//void send_sync(USART_TypeDef *USARTx, const uint8_t* raw, uint8_t len);

/**
 * Инициализация прерываний необходимых для связи с пультом
 */
void led_controller_nvic_init();

/**
 * Инициализация пиноов необходимых для связи с пультом
 */
void led_controller_gpio_init();

/**
 * Инициализация USART ов необходимых для связи с пультом
 */
void led_controller_usart_init(void);

/**
 * Настройка тактирования ов необходимых для связи с пультом
 */
void led_controller_clock_init(void);

extern LedEvents led_events;