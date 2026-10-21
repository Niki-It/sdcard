#include "aic3104.h"
#include "i2c.h"
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
    return i2c_ping(AIC3104_I2C_ADDR, I2C_TIMEOUT_MS);
}

void aic3104_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_master_tx(AIC3104_I2C_ADDR, buf, 2, I2C_TIMEOUT_MS);
}

uint8_t aic3104_read_reg(uint8_t reg) {
    uint8_t val = 0xFF;
    int8_t res = i2c_write_read(AIC3104_I2C_ADDR, 
                                      &reg, 1, 
                                      &val, 1, 
                                      I2C_TIMEOUT_MS);
    return (res == 0) ? val : 0xFF;
}

void aic3104_init_clocking(void) {
    aic3104_write_reg(0x00, 0x00);
    aic3104_write_reg(0x01, 0x80); // исправлена запись
    
    // R 101: CODEC_CLKIN от CLKDIV_OUT
    aic3104_write_reg(101, 0x01);
    LL_mDelay(1);
}
static uint8_t loud = 128 + 72;


void aic3104_init_analog_bypass(void) 
{   
    aic3104_write_reg(0x00, 0x00);
    
    // R 19: MIC1LP/LINE1LP to Left-ADC Control Register
    aic3104_write_reg(0x13, 0x04);
    
    // R 15: Left-ADC PGA Gain Control Register
    aic3104_write_reg(0x0F, 0x00);
    
    // R 46: PGA_L to HPLOUT Volume Control Register
    aic3104_write_reg(0x2E, loud);

    // R 60: PGA_L to HPROUT Volume Control Register
    aic3104_write_reg(0x3C, loud);
    
    // R 51: HPLOUT Output Level Control Register
    aic3104_write_reg(0x33, 0x09);

    // R 65: HPROUT Output Level Control Register
    aic3104_write_reg(0x41, 0x09);
}

void aic3104_line1lp_to_i2s(void)
{
    aic3104_write_reg(0x00, 0x00);

    // включение ADC
    aic3104_write_reg(19, 0x04);
    aic3104_write_reg(22, 0b01111100);
    aic3104_write_reg(24, 0);
    // включение PGA
    aic3104_write_reg(0x0F, 0x00);
    aic3104_write_reg(16, 0x00);

    aic3104_write_reg(0x0C, 0x00);
    aic3104_write_reg(107, 0x30);
}

void aic3104_line1rp_to_i2s(void)
{
    aic3104_write_reg(0x00, 0x00);

    // включение ADC
    aic3104_write_reg(19, 0b01111100);
    aic3104_write_reg(21, 0);
    aic3104_write_reg(22, 0x04);
    // включение PGA
    aic3104_write_reg(0x0F, 0x00);
    aic3104_write_reg(16, 0x00);

    aic3104_write_reg(0x0C, 0x00);
    aic3104_write_reg(107, 0x30);
}


void aic3104_i2s_to_hplout(){
        // Page 0
    aic3104_write_reg(0, 0x00);

    // R7: Left DIN -> Left DAC, Normal Rate
    aic3104_write_reg(7, 0x08);

    // R37: включить Left DAC
    aic3104_write_reg(37, 0x80);

    // R41: Left DAC -> DAC_L1, Right DAC -> DAC_R1
    aic3104_write_reg(41, 0x00);

    // R43: Left DAC, 0 dB, Mute OFF
    aic3104_write_reg(43, 0x00);

    // R47: подключить DAC_L1 -> HPLOUT, 0 dB
    aic3104_write_reg(47, loud);

    // R51: HPLOUT Power ON, Mute OFF, 0 dB
    aic3104_write_reg(51, 0x0D);
}


void aic3104_i2s_to_hprout(){
        // Page 0
    aic3104_write_reg(0, 0x00);

    // R7: Right DIN -> Right DAC, Normal Rate
    aic3104_write_reg(7, 0x02);

    // R37: включить Right DAC
    aic3104_write_reg(37, 0x40);

    // R41: Left DAC -> DAC_L1, Right DAC -> DAC_R1
    aic3104_write_reg(41, 0x00);

    // R43: Right DAC, 0 dB, Mute OFF
    aic3104_write_reg(44, 0x00);

    // R47: подключить DAC_L1 -> HPLOUT, 0 dB
    aic3104_write_reg(64, loud);

    // R51: HPLOUT Power ON, Mute OFF, 0 dB
    aic3104_write_reg(65, 0x0D);
}

void aic3104_soft_reset()
{
    aic3104_write_reg(1, 0x80);
}

uint8_t loud_max;
uint8_t loud_min;

void aic3104_volume_down()
{

    uint8_t absolute = loud & 0b01111111;

    absolute += 3;

    if(absolute >= 72)
    {
        absolute = 72;
        loud_min = 1;
    }
    else
    {
        loud_min = 0;
        loud_max = 0;
    }

    loud = absolute | 0b10000000;
    aic3104_write_reg(64, loud);
    aic3104_write_reg(47, loud);
    aic3104_write_reg(0x2E, loud);
   aic3104_write_reg(0x3C, loud);
}

void aic3104_volume_up()
{

    uint8_t absolute = loud & 0b01111111;

    if(absolute <= 3 || absolute > 127)
    {
        absolute = 3;
        loud_max = 1;
    }
    else
    {
        loud_min = 0;
        loud_max = 0;
    }
    absolute -= 3;

    loud = absolute | 0b10000000;
    aic3104_write_reg(64, loud);
    aic3104_write_reg(47, loud);
    aic3104_write_reg(0x2E, loud);
    aic3104_write_reg(0x3C, loud);
}

uint8_t is_max_volume(){
    return loud_max;
} 

uint8_t is_min_volume(){
    return loud_min;
}