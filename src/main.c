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
#include <stm32f4xx_ll_bus.h>
#include "stm32f4xx_ll_rcc.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"
#include "stm32f4xx_ll_tim.h"

Leds leds = {
    .VD1 = 1,
    .VD2 = 1,
    .VD3 = 1,
    .VD4 = 1,
    .VD5 = 1,
    .VD6 = 1,
    .VD7 = 1,
    .VD8 = 1,
    .VD9 = 1
};

Leds leds2 = {
    .VD1 = 0,
    .VD2 = 0,
    .VD3 = 0,
    .VD4 = 0,
    .VD5 = 0,
    .VD6 = 0,
    .VD7 = 0,
    .VD8 = 0,
    .VD9 = 0
};
bool next_led = false;
uint32_t tick_counter = 1;
uint32_t message_counter = 0;
uint32_t send_counter = 0;

void periph_init();
int main(void) 
{
    periph_init();
    SDIO_TestCard();
    //SDIO_RunBenchmark();
    //tusb_init();
    ButtonStatus button_status;
    while (1) 
    {
        button_status = poll_button_events();
        if(button_status.ready == 1)
        {
            message_counter++;
        }
    }
}
void tim6_handler(void)
{
    send_SetLedState(USART2);
    send_counter++;

    if(next_led)
    {
        set_leds_state(leds);
        next_led = false;
    }
    else
    {
        set_leds_state(leds2);
        next_led = true;
    }
}
void SysTick_Handler(void)
{
    if(tick_counter % 1000 == 0)
    {
        // SEGGER_RTT_printf(0, "messages received: %u \r\n", message_counter);
        // tick_counter = 0;
    }
    tick_counter++;
}

void periph_init()
{
    
    // Инициализация RTT (SEGGER RTT)
    SEGGER_RTT_Init();

    // Инициализация пинов и тактирования
    Clock_Init();
    // Применение настроек тактирования из регистров
    SystemCoreClockUpdate();

    SysTick_Config(SystemCoreClock / 1000);

    // LL_RCC_ClocksTypeDef clocks = {0};
    // LL_RCC_GetSystemClocksFreq(&clocks);

    GPIO_Init();
    NVIC_Init();
    NVIC_EnableIRQ(SysTick_IRQn);

    SDIO_Periph_Init();
    LedController_PeriphInit();
}


