#include "soft_i2c.h"
#include "stm32f4xx_ll_bus.h"

// =============================================================================
// ВНУТРЕННИЕ МАКРОСЫ (Атомарное управление через BSRR)
// =============================================================================
#define I2C_SCL_LOW()   (SOFT_I2C_PORT->BSRR = (SOFT_I2C_SCL_PIN << 16))
#define I2C_SCL_HIGH()  (SOFT_I2C_PORT->BSRR = SOFT_I2C_SCL_PIN)
#define I2C_SDA_LOW()   (SOFT_I2C_PORT->BSRR = (SOFT_I2C_SDA_PIN << 16))
#define I2C_SDA_HIGH()  (SOFT_I2C_PORT->BSRR = SOFT_I2C_SDA_PIN)
#define I2C_SDA_READ()  ((SOFT_I2C_PORT->IDR & SOFT_I2C_SDA_PIN) != 0)

// =============================================================================
// НИЗКОУРОВНЕВЫЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =============================================================================

static inline void i2c_delay(void) {
    // Динамический расчет задержки для ~100 кГц (полупериод ~5 мкс)
    // Например: 16 МГц / 200000 = 80 циклов; 168 МГц / 200000 = 840 циклов
    volatile uint32_t count = (SystemCoreClock / 200000);
    if (count < 10) count = 10; // Гарантированный минимум
    while (count--) {
        __NOP();
    }
}

static inline void i2c_wait_scl_high(void) {
    // Защита от Clock Stretching (растягивания такта ведомым устройством)
    uint32_t timeout = 100000;
    while ((SOFT_I2C_PORT->IDR & SOFT_I2C_SCL_PIN) == 0) {
        if (--timeout == 0) break; // Аварийный выход при зависании шины
    }
}

static void i2c_start(void) {
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
    i2c_delay();
    I2C_SDA_LOW();  // SDA падает при высоком SCL -> Старт
    i2c_delay();
    I2C_SCL_LOW();  // Подготовка к передаче данных
    i2c_delay();
}

static void i2c_stop(void) {
    I2C_SDA_LOW();
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_delay();
    I2C_SDA_HIGH(); // SDA растет при высоком SCL -> Стоп
    i2c_delay();
}

static void i2c_write_bit(uint8_t bit) {
    if (bit) I2C_SDA_HIGH();
    else     I2C_SDA_LOW();
    
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_wait_scl_high();
    i2c_delay();
    I2C_SCL_LOW();
    i2c_delay();
}

static uint8_t i2c_read_bit(void) {
    uint8_t bit;
    I2C_SDA_HIGH(); // Освобождаем линию для чтения
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_wait_scl_high();
    i2c_delay();
    bit = I2C_SDA_READ() ? 1 : 0;
    I2C_SCL_LOW();
    i2c_delay();
    return bit;
}

static uint8_t i2c_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        i2c_write_bit((data >> (7 - i)) & 0x01);
    }
    return i2c_read_bit(); // Возвращает 0 (ACK) или 1 (NACK)
}

static uint8_t i2c_read_byte(uint8_t send_nack) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data <<= 1;
        data |= i2c_read_bit();
    }
    i2c_write_bit(send_nack ? 1 : 0); // ACK (0) или NACK (1)
    return data;
}

// =============================================================================
// РЕАЛИЗАЦИЯ ПУБЛИЧНОГО API
// =============================================================================

void soft_i2c_init(void) {
    // Гарантируем, что шина начинается в состоянии IDLE (обе линии высокие)
    I2C_SCL_HIGH();
    I2C_SDA_HIGH();
    i2c_delay();
}

int8_t soft_i2c_master_tx(uint8_t dev_addr, const uint8_t* buffer, uint16_t len, uint16_t timeout_ms) {
    (void)timeout_ms; // Таймаут управляется внутренним счетчиком i2c_wait_scl_high

    i2c_start();
    
    if (i2c_write_byte((dev_addr << 1) | 0x00) != 0) {
        i2c_stop();
        return -2; // NACK
    }
    
    for (uint16_t i = 0; i < len; i++) {
        if (i2c_write_byte(buffer[i]) != 0) {
            i2c_stop();
            return -2; // NACK во время передачи данных
        }
    }
    
    i2c_stop();
    return 0;
}

int8_t soft_i2c_master_rx(uint8_t dev_addr, uint8_t* buffer, uint8_t len, uint16_t timeout_ms) {
    (void)timeout_ms;
    if (len == 0) return 0;
    
    i2c_start();
    
    if (i2c_write_byte((dev_addr << 1) | 0x01) != 0) {
        i2c_stop();
        return -2; // NACK
    }
    
    for (uint8_t i = 0; i < len; i++) {
        uint8_t send_nack = (i == len - 1) ? 1 : 0;
        buffer[i] = i2c_read_byte(send_nack);
    }
    
    i2c_stop();
    return 0;
}

int8_t soft_i2c_ping(uint8_t dev_addr, uint16_t timeout_ms) {
    (void)timeout_ms;
    
    i2c_start();
    uint8_t ack = i2c_write_byte((dev_addr << 1) | 0x00); // Адрес + W
    i2c_stop();
    
    return (ack == 0) ? 0 : -2;
}

int8_t soft_i2c_write_read(uint8_t dev_addr, 
                           const uint8_t* write_buf, uint16_t write_len,
                           uint8_t* read_buf, uint16_t read_len,
                           uint16_t timeout_ms) {
    (void)timeout_ms;
    if (read_len == 0) return 0;
    
    // --- ФАЗА 1: Запись (адрес регистра) ---
    i2c_start();
    
    if (i2c_write_byte((dev_addr << 1) | 0x00) != 0) {
        i2c_stop();
        return -2; // NACK на адресе
    }
    
    for (uint16_t i = 0; i < write_len; i++) {
        if (i2c_write_byte(write_buf[i]) != 0) {
            i2c_stop();
            return -2; // NACK на данных
        }
    }
    
    // --- ФАЗА 2: Repeated Start (без предварительного STOP!) ---
    // Функция i2c_start() корректно отработает, так как SCL в конце записи 
    // остается низким, а SDA освобожденной (высокой).
    i2c_start();
    
    if (i2c_write_byte((dev_addr << 1) | 0x01) != 0) {
        i2c_stop();
        return -2; // NACK на адресе чтения
    }
    
    // --- ФАЗА 3: Чтение данных ---
    for (uint16_t i = 0; i < read_len; i++) {
        uint8_t send_nack = (i == read_len - 1) ? 1 : 0;
        read_buf[i] = i2c_read_byte(send_nack);
    }
    
    i2c_stop();
    return 0;
}

int8_t soft_i2c_read_string(uint8_t dev_addr, uint8_t* buffer, uint8_t max_len, uint16_t timeout_ms) {
    (void)timeout_ms;
    if (max_len == 0) return 0;

    i2c_start();
    
    // Запрос на чтение
    if (i2c_write_byte((dev_addr << 1) | 0x01) != 0) {
        i2c_stop();
        return -2; // NACK от устройства
    }

    for (uint8_t i = 0; i < max_len; i++) {
        // Мы обязаны послать NACK только если это ПОСЛЕДНИЙ байт в нашем буфере.
        // Для всех остальных шлем ACK (0).
        uint8_t is_last_possible = (i == max_len - 1);
        uint8_t byte = i2c_read_byte(is_last_possible ? 1 : 0);
        
        buffer[i] = byte;

        // Если встретили конец строки, немедленно завершаем транзакцию
        if (byte == '\0') {
            i2c_stop();
            return 0; // Успех, строка прочитана
        }
    }

    // Если цикл завершился, а '\0' не найден, значит строка длиннее max_len.
    // Мы уже послали NACK на последнем байте и теперь делаем STOP.
    i2c_stop();
    buffer[max_len - 1] = '\0'; // Гарантируем безопасность строки
    return -1; // Код ошибки: переполнение буфера (строка обрезана)
}