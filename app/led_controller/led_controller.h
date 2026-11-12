#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "button_combo/source.h"

// Светодиоды
#define LED_VD1  (1U << 0)
#define LED_VD2  (1U << 1)
#define LED_VD3  (1U << 2)
#define LED_VD4  (1U << 3)
#define LED_VD5  (1U << 4)
#define LED_VD6  (1U << 5)
#define LED_VD7  (1U << 6)
#define LED_VD8  (1U << 7)
#define LED_VD9  (1U << 8)
#define LED_VD10 (1U << 9)
#define LED_VD11 (1U << 10)

#define LED_ALL ((1U << 11) - 1U)

// События кнопок
typedef enum
{
    NO_EVENT,
    SHORT_EVENT,
    DOUBLE_SHORT,
    LONG_EVENT
} ButtonEvent;

typedef struct
{
    uint8_t ready;
    source_t source;

    ButtonEvent S1;
    ButtonEvent S2;
    ButtonEvent S3;
    ButtonEvent S4;
    ButtonEvent S5;
    ButtonEvent S6;
    ButtonEvent S7;
    ButtonEvent S8;
    ButtonEvent S9;
    ButtonEvent S10;
} ButtonStatus;

// Инициализация и обслуживание
void LedController_Init(void);
void LedController_Update(uint32_t current_tick);
ButtonStatus LedController_GetButtons(void);

// Управление светодиодами
/// @brief Зажечь или погасить указанные светодиоды enabled = true (зажечь) enabled = false (погасить)
void LedController_Set(uint16_t mask, bool enabled);
/// @brief Указанные светодиоды зажечь, неуказанные будут погашены
void LedController_SetAll(uint16_t mask);
uint16_t LedController_Get(uint16_t mask);

// Мигание светодиодов
void LedController_Blink(uint16_t mask, uint16_t count, uint32_t period_ms);
void LedController_StopBlink(uint16_t mask);

// Обработчик таймера и callback
void tim6_handler(void);
void tim6_callback(void);

// Типы для низкоуровневого драйвера led_raw
typedef struct
{
    uint8_t data[3];
} SetLedStateCommand;

typedef struct
{
    uint8_t data[4];
} RawResponce;
