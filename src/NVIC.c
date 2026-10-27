#include "stm32f405xx.h"
#include "NVIC.h"
#include "tusb.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw.h"
#include "stm32f4xx_ll_tim.h"

void NVIC_Init(void){

    NVIC_SetPriority(SysTick_IRQn, 0);
    NVIC_EnableIRQ(SysTick_IRQn);
    
    NVIC_SetPriority(SPI2_IRQn, 3);
    NVIC_EnableIRQ(SPI2_IRQn);

    NVIC_SetPriority(OTG_FS_IRQn, 5);
    NVIC_EnableIRQ(OTG_FS_IRQn);

    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);

    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    led_controller_nvic_init();
}

void USART2_IRQHandler(void)
{
    usart2_handler();
}

void UART4_IRQHandler(void)
{
    uart4_handler();
}



void TIM6_DAC_IRQHandler(void)
{
    if (LL_TIM_IsActiveFlag_UPDATE(TIM6))
    {
        tim6_handler();
        LL_TIM_ClearFlag_UPDATE(TIM6);
    }
}

// Если используете USB OTG FS (встроенный разъём):
volatile uint32_t usb_reset_count = 0;
volatile uint32_t usb_enum_count = 0;
volatile uint32_t usb_suspend_count = 0;
void OTG_FS_IRQHandler(void)
{
    uint32_t status = USB_OTG_FS->GINTSTS & USB_OTG_FS->GINTMSK;

    if (status & USB_OTG_GINTSTS_USBRST)
    {
        usb_reset_count++;
    }

    if (status & USB_OTG_GINTSTS_ENUMDNE)
    {
        usb_enum_count++;
    }

    if (status & USB_OTG_GINTSTS_USBSUSP)
    {
        usb_suspend_count++;
    }
    tud_int_handler(0);  // 0 = rhport 0
}

// Если используете USB OTG HS (внешний PHY или FS через внутренний):
// void OTG_HS_IRQHandler(void)
// {
//     tud_int_handler(0);
// }