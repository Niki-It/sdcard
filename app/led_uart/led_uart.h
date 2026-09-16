#pragma once 
#include "stdint.h"
#include "stdbool.h"

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
} LedEvents;

LedEvents parse_events(uint16_t raw_led_data);

SetLedStateCommand create_set_led_state(Leds leds, uint8_t frame_number);