#pragma once
#include "stdint.h"

typedef enum relay_port_t
{
    GGS,
    APB
} relay_port_t;

void swich_relay(uint8_t relay, relay_port_t port);
void mute_amplifier(uint8_t channel);
void unmute_amplifiers(uint8_t channel);