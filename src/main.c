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

void periph_init(void);

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

int main(void) 
{
    periph_init();

    // SDIO_TestCard();
    // SDIO_RunBenchmark();
    tusb_init();

    while (1) 
    {
        SetLedStateCommand cmd = create_set_led_state(leds, frame_number);
        send_command(USART2, cmd);
        LL_mDelay(30);
        
        SetLedStateCommand cmd2 = create_set_led_state(leds2, frame_number);
        send_command(USART2, cmd2);


        LL_mDelay(30);
        //tud_task();
    }
}

void periph_init()
{
    // Применение настроек тактирования из регистров
    SystemCoreClockUpdate();

    // Инициализация RTT (SEGGER RTT)
    SEGGER_RTT_Init();

    // Инициализация пинов и тактирования
    Clock_Init();
    GPIO_Init();
    NVIC_Init();

    SDIO_Periph_Init();

    // Инициализация задержек
    LL_Init1msTick(SystemCoreClock);

    led_controller_usart_init();
    // // Инициализация модулей
    // soft_i2c_init();
    // aic3104_init();
    // I2S2_Init();

    // mux_select(2, MUX_CHANNEL_X2);
    // I2S2_StartTransmitIT();

    // aic3104_init_clocking();
    // aic3104_init_analog_bypass();
}

