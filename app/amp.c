#include "amp.h"
#include "gpio.h"

void swich_relay(uint8_t relay, relay_port_t port)
{
    
    if(relay == 0){
        if (port == GGS){
            LL_GPIO_ResetOutputPin(AMP_PORT, RELAY_K1);
        } 
        else {
            LL_GPIO_SetOutputPin(AMP_PORT, RELAY_K1);
        }
    } 
    else {
        if (port == GGS)
        {
            LL_GPIO_ResetOutputPin(AMP_PORT, RELAY_K2);
            
        } 
        else {
            LL_GPIO_SetOutputPin(AMP_PORT, RELAY_K2);
        }
    }

}

void mute_amplifier(uint8_t channel)
{
    if(channel == 0)
    {
        LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    }
    else
    {
        LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
    }
}

void unmute_amplifiers(uint8_t channel)
{
    if(channel == 0)
    {
        LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    }
    else
    {
        LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
    }

}