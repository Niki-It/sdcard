#ifndef AIC3104_H
#define AIC3104_H

#include <stdint.h>
#include <stdbool.h>

#define AIC3104_I2C_ADDR    0x18
#define AIC3104_RESET_PORT  GPIOC
#define AIC3104_RESET_PIN   LL_GPIO_PIN_7

/**
 * @brief Инициализация пина RESET и выполнение аппаратного сброса кодека
 */
void aic3104_init(void);
void aic3104_soft_reset();
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
void aic3104_line1lp_hplout_bypass(uint8_t hardware);
void aic3104_line1rp_hprout_bypass(uint8_t hardware);
void aic3104_line1lp_hplout_bypass_stop(void);
void aic3104_line1rp_hprout_bypass_stop(void);
/* false on I2C error: caller must keep OPER muted. */
bool aic3104_line1lp_right_lop_bypass(uint8_t hardware);
void aic3104_line1lp_right_lop_bypass_stop(void);
void aic3104_line1lp_i2s(void);
void aic3104_i2s_hprout(uint8_t hardware);
void aic3104_i2s_hplout(uint8_t hardware);
bool aic3104_i2s_right_lop(uint8_t hardware);
void aic3104_i2s_hprout_stop(void);
void aic3104_i2s_hplout_stop(void);
void aic3104_i2s_right_lop_stop(void);

/* Битовая маска конкретных маршрутов (канал 1 = HPROUT). */
#define AIC3104_VOLUME_BYPASS_1 0x01U
#define AIC3104_VOLUME_BYPASS_2 0x02U
#define AIC3104_VOLUME_I2S_1    0x04U
#define AIC3104_VOLUME_I2S_2    0x08U
#define AIC3104_VOLUME_RECEIVE  0x10U

/* Значение ослабления без D7: минимум громкости = 72, максимум = 0.
 * Шаговые функции только вычисляют новое значение; I2C не используют.
 * false означает границу/невалидный аргумент, значение не изменяется. */
uint8_t aic3104_get_min_volume(void);
uint8_t aic3104_get_max_volume(void);
bool aic3104_volume_up(uint8_t *hardware);
bool aic3104_volume_down(uint8_t *hardware);
bool aic3104_apply_volume(uint8_t routes, uint8_t hardware);

#endif
