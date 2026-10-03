
#include "soft_i2c.h"
#include "aic3104.h"
#include "main.h"
#include "mux.h"
#include "i2s2.h"

#include "tusb.h"
#include "sdio/sdio.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"
#include "storage/fs.h"


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

sd_card_info_t card_info;
void diskio_set_sd_card(sd_card_info_t *info);


void periph_init();

int main(void) 
{   
    SEGGER_RTT_printf(0, "V0.1  \r\n");
    periph_init();
    fs_unit(true);
    //SDIO_RunBenchmark();
    tusb_init();

    while (1) 
    {
        tud_task();
    }
}
void tim6_handler(void)
{
    send_SetLedState(USART2);

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

    LedController_PeriphInit();

    sd_status_t status = sd_init(&card_info);
    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD init error: %d\r\n", status);
        return;
    }
    diskio_set_sd_card(&card_info);
}



