#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stm32f4xx_ll_usart.h>

typedef struct
{
    bool VD1;
    bool VD2;
    bool VD3;
    bool VD4;
    bool VD5;
    bool VD6;
    bool VD7;
    bool VD8;
    bool VD9;
} Leds;

typedef struct
{
    uint8_t data[3];
} SetLedStateCommand;

typedef struct
{
    uint8_t data[4];
} RawResponce;

typedef enum
{
    NO_EVENT,
    SHORT_EVENT,
    DOUBLE_SHORT,
    LONG_EVENT
} LedEvent;

typedef struct
{
    LedEvent VD1;
    LedEvent VD2;
    LedEvent VD3;
    LedEvent VD4;
    LedEvent VD5;
    LedEvent VD6;
    LedEvent VD7;
    LedEvent VD8;
} ButtonEvents;

#define LED_VD1  (1U << 0)
#define LED_VD2  (1U << 1)
#define LED_VD3  (1U << 2)
#define LED_VD4  (1U << 3)
#define LED_VD5  (1U << 4)
#define LED_VD6  (1U << 5)
#define LED_VD7  (1U << 6)
#define LED_VD8  (1U << 7)
#define LED_VD9  (1U << 8)

#define LED_ALL  ((1U << 9) - 1U)

typedef struct
{
    // 1 - есть новое состояние кнопок, 0 - нет.
    uint8_t ready;
    LedEvent VD1;
    LedEvent VD2;
    LedEvent VD3;
    LedEvent VD4;
    LedEvent VD5;
    LedEvent VD6;
    LedEvent VD7;
    LedEvent VD8;
} ButtonStatus;

ButtonStatus poll_button_events(void);

/* USART, с которого пришло последнее событие кнопок. */
USART_TypeDef *led_controller_active_usart(void);

void set_all_leds(uint16_t mask);
uint16_t get_leds(uint16_t mask);
void set_leds(uint16_t mask, bool enabled);
void send_SetLedState(USART_TypeDef *USARTx);

void tim6_handler(void);
void tim6_callback(void);
uint8_t can_send_command(void);
void LedController_PeriphInit(void);

typedef enum
{
    VD1, VD2, VD3, VD4, VD5, VD6, VD7, VD8, VD9,
    LED_BLINKING_COUNT
} blinking_led_t;

/*
 * count полных циклов (2 * count переключений).
 * Первое переключение немедленное; period_ms — интервал переключений.
 * count == 0 отменяет мигание, сохраняя текущее состояние.
 */
void request_blinking(blinking_led_t led, uint16_t count, uint32_t period_ms);
void blinking_process(uint32_t current_tick);
void led_controller_process(uint32_t uptime_ms);

/*
 * Управление светодиодами, обменом и опрос событий — из основного цикла.
 * tim6_handler вызывается в ISR. Состояние LED общее для обоих пультов.
 * Для каждого USART отдельные RX, номер кадра и ожидание ответа.
 */
