#include "amp.h"
#include "gpio.h"

void swich_relay(RelayMode mode)
{
    switch (mode)
    {
        case GGC:
            LL_GPIO_ResetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
        case APB:
            LL_GPIO_SetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
        case OPER:
            LL_GPIO_SetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_SetOutputPin(AMP_PORT, OPR_PIN);
            break;
        default:
            LL_GPIO_ResetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
    }
}

void mute_amplifiers(void)
{
    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
}

void unmute_amplifiers(void)
{
    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
}