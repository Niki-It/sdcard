#include "aic3104.h"
#include "soft_i2c.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_utils.h"

#define I2C_TIMEOUT_MS 50

void aic3104_init(void) {
    // Выполняем последовательность аппаратного сброса
    LL_GPIO_ResetOutputPin(AIC3104_RESET_PORT, AIC3104_RESET_PIN); // RESET = LOW
    LL_mDelay(50);                                                  // Держим 50 мс
    LL_GPIO_SetOutputPin(AIC3104_RESET_PORT, AIC3104_RESET_PIN);   // RESET = HIGH
    LL_mDelay(10);                                                  // Ждем стабилизации
}

int8_t aic3104_ping(void) {
    return soft_i2c_ping(AIC3104_I2C_ADDR, I2C_TIMEOUT_MS);
}

void aic3104_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    soft_i2c_master_tx(AIC3104_I2C_ADDR, buf, 2, I2C_TIMEOUT_MS);
}

uint8_t aic3104_read_reg(uint8_t reg) {
    uint8_t val = 0xFF;
    int8_t res = soft_i2c_write_read(AIC3104_I2C_ADDR, 
                                      &reg, 1, 
                                      &val, 1, 
                                      I2C_TIMEOUT_MS);
    return (res == 0) ? val : 0xFF;
}

void aic3104_init_clocking(void) {
    aic3104_write_reg(0x00, 0x00);
    aic3104_write_reg(0x01, 0x08);
    
    // R 101: CODEC_CLKIN от CLKDIV_OUT
    aic3104_write_reg(101, 0x01);
    LL_mDelay(50);
}

void aic3104_init_analog_bypass(void) {   
    aic3104_write_reg(0x00, 0x00);
    
    // R 19: MIC1LP/LINE1LP to Left-ADC Control Register
    aic3104_write_reg(0x13, 0x04);
    
    // R 15: Left-ADC PGA Gain Control Register
    aic3104_write_reg(0x0F, 0x00);
    
    // R 46: PGA_L to HPLOUT Volume Control Register
    aic3104_write_reg(0x2E, 0x80);
    
    // R 51: HPLOUT Output Level Control Register
    aic3104_write_reg(0x33, 0x09);

    // aic3104_write_reg(0x13, 0x04);
    // LL_mDelay(50);

    // aic3104_write_reg(0x16, 0x04);
    // LL_mDelay(50);

    // aic3104_write_reg(0x0F, 0x00);
    // LL_mDelay(50);

    // aic3104_write_reg(0x10, 0x00);
    // LL_mDelay(50);

    // aic3104_write_reg(0x07, 0x0A);
    // LL_mDelay(50);

    // aic3104_write_reg(0x25, 0xC0);
    // LL_mDelay(50);

    // aic3104_write_reg(0x2B, 0x00);
    // LL_mDelay(50);

    // aic3104_write_reg(0x2C, 0x00);
    // LL_mDelay(50);

    // aic3104_write_reg(0x52, 0x80);
    // LL_mDelay(50);

    // aic3104_write_reg(0x5C, 0x80);
    // LL_mDelay(50);

    // aic3104_write_reg(0x56, 0x09);
    // LL_mDelay(50);

    // aic3104_write_reg(0x5D, 0x09);
    // LL_mDelay(50);
}