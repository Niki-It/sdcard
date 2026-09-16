#ifndef AIC3104_H
#define AIC3104_H

#include <stdint.h>

#define AIC3104_I2C_ADDR    0x18
#define AIC3104_RESET_PORT  GPIOC
#define AIC3104_RESET_PIN   LL_GPIO_PIN_7

/**
 * @brief Инициализация пина RESET и выполнение аппаратного сброса кодека
 */
void aic3104_init(void);

/**
 * @brief Проверка присутствия кодека на шине I2C
 * @return 0 если кодек ответил, -2 если NACK
 */
int8_t aic3104_ping(void);

/**
 * @brief Запись значения в регистр кодека
 */
void aic3104_write_reg(uint8_t reg, uint8_t val);

/**
 * @brief Чтение значения регистра кодека
 * @return Значение регистра, 0xFF при ошибке
 */
uint8_t aic3104_read_reg(uint8_t reg);

void aic3104_init_clocking(void);
void aic3104_init_analog_bypass(void);

#endif