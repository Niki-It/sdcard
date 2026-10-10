
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

#define AMP_PORT        GPIOB
#define APB_PIN         LL_GPIO_PIN_6
#define OPR_PIN         LL_GPIO_PIN_7
#define MUTE_AMP1_PIN   LL_GPIO_PIN_8
#define MUTE_AMP2_PIN   LL_GPIO_PIN_9

uint32_t tick_counter = 1;
uint32_t message_counter = 0;
uint32_t send_counter = 0;

sd_card_info_t card_info;
void diskio_set_sd_card(sd_card_info_t *info);


void periph_init();
int main(void) 
{       
    periph_init();
    fs_unit(true);
    tusb_init();
    ButtonStatus button_status;
    I2S2_StartTransmitIT();
    mux_select(2, MUX_CHANNEL_X1);
    aic3104_init();
    aic3104_init_clocking();
    aic3104_line1lp_toi2c();
    aic3104_read_adc_status();

    while (1) 
    {
        tud_task();
        button_status = poll_button_events();
        if(button_status.ready == 1)
        {
            if(button_status.VD1 == DOUBLE_SHORT)
            {
                message_counter++;
            }
        }
        if(can_send_command())
        {
            send_SetLedState(UART4);
        }
    }
    
}

void SysTick_Handler(void)
{
    // if(tick_counter % 1000 == 0)
    // {
    //     SEGGER_RTT_printf(0, "messages received: %u \r\n", message_counter);
    //     tick_counter = 0;
    // }
    // tick_counter++;
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

    GPIO_Init();
    NVIC_Init();

    LedController_PeriphInit();

    soft_i2c_init();
    I2S2_Init();

    sd_status_t status = sd_init(&card_info);
    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD init error: %d\r\n", status);
        return;
    }
    diskio_set_sd_card(&card_info);
}



