#pragma once
#include "stdint.h"

#define RX_LOG_SIZE 64

typedef struct {
    uint8_t  buffer[RX_LOG_SIZE]; 
    uint8_t  write_idx;           
    uint8_t  count;               
} RxLog;


extern volatile RxLog rx_log;

// Запись в лог (вызывается из прерывания)
static inline void rx_log_write(uint8_t byte)
{
    rx_log.buffer[rx_log.write_idx] = byte;
    
    // Инкремент с автоматическим переносом (только если размер = степень 2!)
    rx_log.write_idx = (rx_log.write_idx + 1) & (RX_LOG_SIZE - 1);
    
    if (rx_log.count < 255) {
        rx_log.count++;
    }
}
