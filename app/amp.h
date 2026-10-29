#pragma once

typedef enum 
{
    GGC = 0,
    APB = 1,
    OPER = 2
} RelayMode;

void swich_relay(RelayMode mode);
void mute_amplifiers(void);
void unmute_amplifiers(void);