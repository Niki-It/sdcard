#ifndef MY_HARD_FAULT
#define MY_HARD_FAULT
#include <stdint.h>

void HardFault_Handler(void);
void HardFault_Delay(volatile uint32_t count);

#endif