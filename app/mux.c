#include "mux.h"
#include "stm32f4xx_ll_gpio.h"

void mux_select(uint8_t mux_number, mux_channel_t channel) {
    // Извлекаем биты A0, A1, A2 из номера канала
    uint8_t a0 = (channel >> 0) & 0x01;
    uint8_t a1 = (channel >> 1) & 0x01;
    uint8_t a2 = (channel >> 2) & 0x01;
    
    if (mux_number == 1) {
        // Устанавливаем уровни на пинах MUX1 (PA4, PA5, PA6)
        if (a0) LL_GPIO_SetOutputPin(MUX1_PORT, MUX1_A0_PIN);
        else    LL_GPIO_ResetOutputPin(MUX1_PORT, MUX1_A0_PIN);
        
        if (a1) LL_GPIO_SetOutputPin(MUX1_PORT, MUX1_A1_PIN);
        else    LL_GPIO_ResetOutputPin(MUX1_PORT, MUX1_A1_PIN);
        
        if (a2) LL_GPIO_SetOutputPin(MUX1_PORT, MUX1_A2_PIN);
        else    LL_GPIO_ResetOutputPin(MUX1_PORT, MUX1_A2_PIN);
    }
    else if (mux_number == 2) {
        // Устанавливаем уровни на пинах MUX2 (PC1, PC2, PC3)
        if (a0) LL_GPIO_SetOutputPin(MUX2_PORT, MUX2_A0_PIN);
        else    LL_GPIO_ResetOutputPin(MUX2_PORT, MUX2_A0_PIN);
        
        if (a1) LL_GPIO_SetOutputPin(MUX2_PORT, MUX2_A1_PIN);
        else    LL_GPIO_ResetOutputPin(MUX2_PORT, MUX2_A1_PIN);
        
        if (a2) LL_GPIO_SetOutputPin(MUX2_PORT, MUX2_A2_PIN);
        else    LL_GPIO_ResetOutputPin(MUX2_PORT, MUX2_A2_PIN);
    }
}