#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_ll_utils.h"
#include "soft_i2c.h"
#include "aic3104.h"
#include "gpio.h"
#include "clock.h"
#include "mux.h"
#include "i2s2.h"
#include "NVIC.h"
#include "tusb.h"
#include "sdio.h"
#include "SEGGER_RTT.h"
#include "led_controller/led_controller.h"
#include <stm32f4xx_ll_bus.h>
#include "stm32f4xx_ll_rcc.h"

static uint8_t frame_number = 0;
void inc_frame_number();

int main(void)
{

    Clock_Init();

    LL_Init1msTick(SystemCoreClock); 
    NVIC_SetPriority(SysTick_IRQn, 0);

    // LL_RCC_ClocksTypeDef clocks = {0};
    // LL_RCC_GetSystemClocksFreq(&clocks);


    GPIO_Init();
    NVIC_Init();
    led_controller_usart_init();

    Leds leds = {
        .VD1 = 1,
        .VD2 = 1,
        .VD3 = 1,
        .VD4 = 1,
        .VD5 = 0,
        .VD6 = 0,
        .VD7 = 0,
        .VD8 = 0,
        .VD9 = 0
    };

    Leds leds2 = {
        .VD1 = 0,
        .VD2 = 0,
        .VD3 = 0,
        .VD4 = 0,
        .VD5 = 1,
        .VD6 = 1,
        .VD7 = 1,
        .VD8 = 1,
        .VD9 = 1
    };
    
    while(1)
    { 
        SetLedStateCommand cmd = create_set_led_state(leds, frame_number);
        send_sync(USART2, cmd.data, 3);
        LL_mDelay(100);

        inc_frame_number();
        
        SetLedStateCommand cmd2 = create_set_led_state(leds2, frame_number);
        send_sync(USART2, cmd2.data, 3);

        LL_mDelay(100);
        inc_frame_number();
    }

    return 0;
}

void inc_frame_number()
{
    frame_number++;
    if (frame_number > 3) 
    {
        frame_number = 0;
    }
}
