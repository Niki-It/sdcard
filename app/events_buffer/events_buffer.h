#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "led_uart/led_uart.h"

#define EVENTS_BUFFER_SIZE 64u


extern LedEvents events_buffer[EVENTS_BUFFER_SIZE];
extern volatile uint8_t events_count;

void events_buffer_push(const LedEvents events);