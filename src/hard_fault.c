#include "hard_fault.h"
#include "stm32f4xx_ll_utils.h"

void HardFault_Delay(volatile uint32_t count) {
    while (count--) {
        __NOP();
    }
}

void HardFault_Handler(void) {
    __disable_irq();
    while (1) {
        HardFault_Delay(200000);
    }
}