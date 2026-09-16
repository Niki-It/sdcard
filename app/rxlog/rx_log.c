#include "rx_log.h"
#include "stm32f4xx.h"

volatile RxLog rx_log = {0};





// Чтение лога из основного цикла (безопасная копия)
void rx_log_read(uint8_t *out_buffer, uint8_t *out_count)
{
    // Отключаем прерывания на время копирования
    __disable_irq();
    
    uint8_t start_idx;
    uint8_t num_bytes;
    
    if (rx_log.count >= RX_LOG_SIZE) {
        // Буфер переполнен — выводим последние RX_LOG_SIZE байтов
        num_bytes = RX_LOG_SIZE;
        start_idx = rx_log.write_idx;  // Самый старый байт — там, куда сейчас писать
    } else {
        // Буфер не полный — выводим всё, что есть
        num_bytes = rx_log.count;
        start_idx = 0;
    }
    
    for (uint8_t i = 0; i < num_bytes; i++) {
        out_buffer[i] = rx_log.buffer[(start_idx + i) & (RX_LOG_SIZE - 1)];
    }
    
    *out_count = num_bytes;
    
    __enable_irq();
}


// Сброс лога
void rx_log_clear(void)
{
    __disable_irq();
    rx_log.write_idx = 0;
    rx_log.count = 0;
    __enable_irq();
}