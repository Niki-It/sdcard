#include "aic3104.h"
#include "soft_i2c.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_utils.h"
#include "SEGGER_RTT.h"

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

void aic3104_init_analog_bypass(void) 
{   
    aic3104_write_reg(0x00, 0x00);
    
    // R 19: MIC1LP/LINE1LP to Left-ADC Control Register
    aic3104_write_reg(0x13, 0x04);
    
    // R 15: Left-ADC PGA Gain Control Register
    aic3104_write_reg(0x0F, 0x00);
    
    // R 46: PGA_L to HPLOUT Volume Control Register
    aic3104_write_reg(0x2E, 0x80);

    // R 60: PGA_L to HPROUT Volume Control Register
    aic3104_write_reg(0x3C, 0b10111111);
    
    // R 51: HPLOUT Output Level Control Register
    aic3104_write_reg(0x33, 0x09);

    // R 65: HPROUT Output Level Control Register
    aic3104_write_reg(0x41, 0x09);
}

void aic3104_line1lp_toi2c(void)
{
    aic3104_write_reg(0x00, 0x00);

    // включение ADC
    aic3104_write_reg(19, 0x04);
    // включение PGA
    aic3104_write_reg(0x0F, 0x00);

    // 1 HP filter
    aic3104_write_reg(0x0C, 0x00);
    aic3104_write_reg(107, 0x30);


}
void aic3104_line1rp_toi2c(void)
{
    aic3104_write_reg(0x00, 0x00);

    
}
void aic3104_read_adc_status(void)
{
    uint8_t adc_status = aic3104_read_reg(36);

    SEGGER_RTT_printf(0, "ADC STATUS: 0x%02X\r\n", adc_status); 
    SEGGER_RTT_printf(0, "R3   = 0x%02X\r\n", aic3104_read_reg(3));
    SEGGER_RTT_printf(0, "R8   = 0x%02X\r\n", aic3104_read_reg(8));
    SEGGER_RTT_printf(0, "R9   = 0x%02X\r\n", aic3104_read_reg(9));
    SEGGER_RTT_printf(0, "R15  = 0x%02X\r\n", aic3104_read_reg(15));
    SEGGER_RTT_printf(0, "R19  = 0x%02X\r\n", aic3104_read_reg(19));
    SEGGER_RTT_printf(0, "R36  = 0x%02X\r\n", aic3104_read_reg(36));
    SEGGER_RTT_printf(0, "R101 = 0x%02X\r\n", aic3104_read_reg(101));
    SEGGER_RTT_printf(0, "R107 = 0x%02X\r\n", aic3104_read_reg(107));
}
