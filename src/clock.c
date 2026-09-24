#include "stm32f4xx.h"
#include "clock.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_utils.h"
#include "led_controller/led_raw/led_raw.h"

void Clock_Init(void)
{        
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);   // Для регулятора напряжения

    /* Настройка регулятора напряжения */
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    while (LL_PWR_IsActiveFlag_VOS() == 0);

    /* Включаем HSE */
    LL_RCC_HSE_Enable();
    while(LL_RCC_HSE_IsReady() == 0);

    /* Настройка Flash */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5);
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
    LL_FLASH_EnablePrefetch();

    /* Делители шин */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_4);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);

    /* Конфигурация PLL system clock */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 336, LL_RCC_PLLP_DIV_2);
    LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 336, LL_RCC_PLLQ_DIV_7);
    LL_RCC_PLL_Enable();
    while(LL_RCC_PLL_IsReady() == 0);

    /* Конфигурация PLL i2s clock */
    LL_RCC_PLLI2S_ConfigDomain_I2S(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLI2SM_DIV_8, 258, LL_RCC_PLLI2SR_DIV_3);
    LL_RCC_PLLI2S_Enable();
    while (!LL_RCC_PLLI2S_IsReady());

    /* Переключаем system на PLL */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL);

    /* Обновляем значение переменной SystemCoreClock */
    LL_SetSystemCoreClock(168000000);

    // Включаем тактирование
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA); // Для MUX1, USART2, UART4 , usb
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB); // Для Soft I2C
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC); // Для RESET кодека, MCLK, MUX2 и DATx_SD + CLK_SD
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD); // Для CMD_SD
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);  // Для I2S
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_OTGFS); // Для USB
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SDIO);  // Для SDIO

    // включаем тактирование для UART4 и USART2
    led_controller_clock_init();
}