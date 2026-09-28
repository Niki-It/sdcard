
#include "soft_i2c.h"
#include "aic3104.h"
#include "main.h"
#include "mux.h"
#include "i2s2.h"

#include "tusb.h"
#include "ff.h"
#include "sdio/sdio.h"

#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"


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


void periph_init();
int main(void) 
{
    periph_init();
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

static void SD_Test(void)
{
    sd_status_t status;

    status = sd_init(&card_info);

    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD init error: %d\r\n", status);
        return;
    }


    uint8_t write_buffer[512];
    uint8_t read_buffer[512];

    for (uint32_t i = 0; i < 512; i++)
        write_buffer[i] = (uint8_t)i;

    status = sd_write_blocks(
        &card_info,
        1,
        write_buffer,
        1
    );

    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD WRITE ERROR: %d\r\n", status);
        return;
    }

    SEGGER_RTT_printf(0, "SD WRITE OK\r\n");

    status = sd_read_blocks(
        &card_info,
        1000,
        read_buffer,
        1
    );

    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD READ ERROR: %d\r\n", status);
        return;
    }

    SEGGER_RTT_printf(0, "SD READ OK\r\n");

    for (uint32_t i = 0; i < 512; i++)
    {
        if (write_buffer[i] != read_buffer[i])
        {
            SEGGER_RTT_printf(
                0,
                "SD VERIFY ERROR: offset=%u W=%02X R=%02X\r\n",
                i,
                write_buffer[i],
                read_buffer[i]
            );

            return;
        }
    }

    SEGGER_RTT_printf(0, "SD VERIFY OK 6 MHZ\r\n");
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

    SD_Test();

    //SDIO_Periph_Init();
    //SDIO_TestCard();
}

#define RAM_DISK_SIZE   (64 * 1024)   // 64 KB
#define SECTOR_SIZE     512
#define NUM_SECTORS     (RAM_DISK_SIZE / SECTOR_SIZE)

static FATFS fs;
static FIL file;

static uint8_t work_buffer[FF_MAX_SS];

static uint8_t ram_disk[RAM_DISK_SIZE];
void file_test()
{
    
}

