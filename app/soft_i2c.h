#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"

// =============================================================================
// КОНФИГУРАЦИЯ МОДУЛЯ
// =============================================================================
#define SOFT_I2C_PORT       GPIOB
#define SOFT_I2C_SCL_PIN    LL_GPIO_PIN_10
#define SOFT_I2C_SDA_PIN    LL_GPIO_PIN_11

// =============================================================================
// ПУБЛИЧНЫЙ API
// =============================================================================

/**
 * @brief Инициализация пинов для программного I2C (Open-Drain + Pull-Up)
 */
void soft_i2c_init(void);

/**
 * @brief Передача данных по I2C (Master Transmitter)
 * @param dev_addr 7-битный адрес устройства (без бита R/W)
 * @param buffer Указатель на буфер данных для отправки
 * @param len Количество байт для отправки
 * @param timeout_ms Таймаут операции (используется как верхний лимит, 
 *                   основная защита от зависания встроена в драйвер)
 * @return 0 при успехе, -2 при получении NACK
 */
int8_t soft_i2c_master_tx(uint8_t dev_addr, const uint8_t* buffer, uint16_t len, uint16_t timeout_ms);

/**
 * @brief Прием данных фиксированной длины по I2C (Master Receiver)
 * @param dev_addr 7-битный адрес устройства (без бита R/W)
 * @param buffer Указатель на буфер для приема данных
 * @param len Количество байт для приема
 * @param timeout_ms Таймаут операции
 * @return 0 при успехе, -2 при получении NACK
 */
int8_t soft_i2c_master_rx(uint8_t dev_addr, uint8_t* buffer, uint8_t len, uint16_t timeout_ms);

/**
 * @brief Проверяет присутствие устройства на шине (I2C Ping)
 * @return 0 если устройство ответило ACK, -2 если NACK
 */
int8_t soft_i2c_ping(uint8_t dev_addr, uint16_t timeout_ms);

/**
 * @brief Комбинированная операция: запись + Repeated Start + чтение.
 *        Используется для чтения регистров устройств.
 * @param dev_addr 7-битный адрес устройства
 * @param write_buf Байты для записи (обычно адрес регистра)
 * @param write_len Длина данных для записи
 * @param read_buf Буфер для прочитанных данных
 * @param read_len Сколько байт нужно прочитать
 */
int8_t soft_i2c_write_read(uint8_t dev_addr, const uint8_t* write_buf, uint16_t write_len, uint8_t* read_buf, uint16_t read_len, uint16_t timeout_ms);

/**
 * @brief Прием данных переменной длины по I2C (Master Reader)
 * @param dev_addr 7-битный адрес устройства (без бита R/W)
 * @param buffer Указатель на буфер для приема данных
 * @param max_len Максимальное количество байт для приема (чтобы буфер не переполнялся)
 * @param timeout_ms Таймаут операции
 * @return 0 при успехе, -1 при выходе за max_len, -2 при получении NACK
 */
int8_t soft_i2c_read_string(uint8_t dev_addr, uint8_t* buffer, uint8_t max_len, uint16_t timeout_ms);

#endif // SOFT_I2C_H