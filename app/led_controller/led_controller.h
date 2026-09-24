#pragma once 
#include "stdint.h"
#include "stdbool.h"
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
    NoEvent,
    Short,
    DoubleShort,
    Long,
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
typedef struct 
{
    // 1 - готов, 0 не готов
    uint8_t ready;
    ButtonEvents button_events;
} ButtonStatus;

// ---------------- Core логика ---------------------------
ButtonStatus poll_button_events();
void set_leds_state(Leds leds);

void send_SetLedState(USART_TypeDef *USARTx);
extern void tim6_handler();
// ------ Вспомогательные функции ---------------------
void LedController_PeriphInit();
void write_events(RawResponce response);
