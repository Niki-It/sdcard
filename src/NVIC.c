#include "stm32f405xx.h"
#include "NVIC.h"
#include "tusb.h"

extern void I2S2_Callback();

void NVIC_Init(void){
    // NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP);

    NVIC_SetPriority(SPI2_IRQn, 1);
    NVIC_EnableIRQ(SPI2_IRQn);

    NVIC_SetPriority(OTG_FS_IRQn, 5);
    NVIC_EnableIRQ(OTG_FS_IRQn);
}

void SPI2_IRQHandler(void) {
    I2S2_Callback();
}

// Если используете USB OTG FS (встроенный разъём):
void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);  // 0 = rhport 0
}

// Если используете USB OTG HS (внешний PHY или FS через внутренний):
// void OTG_HS_IRQHandler(void)
// {
//     tud_int_handler(0);
// }