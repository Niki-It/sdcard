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

int main(void) {
    // Инициализация RTT (SEGGER RTT)
    SEGGER_RTT_Init();

    // Инициализация пинов и тактирования
    Clock_Init();
    GPIO_Init();
    NVIC_Init();

    SDIO_Periph_Init();

    // Инициализация задержек
    LL_Init1msTick(SystemCoreClock);

    SDIO_TestCard();

    SDIO_RunBenchmark();
    
    // // Инициализация модулей
    // soft_i2c_init();
    // aic3104_init();
    // I2S2_Init();

    // mux_select(2, MUX_CHANNEL_X2);
    // I2S2_StartTransmitIT();

    // aic3104_init_clocking();
    // aic3104_init_analog_bypass();

    tusb_init();

    while (1) {
        tud_task();
    }
}