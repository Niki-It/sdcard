#include "aic3104.h"
#include "i2c.h"
#include <stdbool.h>
#include <stddef.h>
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_utils.h"

#define I2C_TIMEOUT_MS 50

#define AIC3104_PAGE_0                  0x00
#define AIC3104_REG_LINE1LP_LEFT_ADC   19
#define AIC3104_REG_LINE1RP_RIGHT_ADC  22
#define AIC3104_REG_LEFT_PGA_GAIN      15
#define AIC3104_REG_RIGHT_PGA_GAIN     16
#define AIC3104_REG_PGA_L_HPLOUT       46
#define AIC3104_REG_DAC_L_HPLOUT       47
#define AIC3104_REG_HPLOUT_LEVEL       51
#define AIC3104_REG_PGA_R_HPROUT       63
#define AIC3104_REG_DAC_R_HPROUT       64
#define AIC3104_REG_HPROUT_LEVEL       65
#define AIC3104_REG_DAC_DATA_PATH      7
#define AIC3104_REG_DAC_POWER          37
#define AIC3104_REG_DAC_OUTPUT_SELECT  41
#define AIC3104_REG_LEFT_DAC_VOLUME    43
#define AIC3104_REG_RIGHT_DAC_VOLUME   44

#define AIC3104_LEFT_DAC_PATH_MASK     0x18U
#define AIC3104_RIGHT_DAC_PATH_MASK    0x06U
#define AIC3104_LEFT_DAC_FROM_LEFT     0x08U
#define AIC3104_RIGHT_DAC_MONO_MIX     0x06U
#define AIC3104_LEFT_DAC_POWER         0x80U
#define AIC3104_RIGHT_DAC_POWER        0x40U
#define AIC3104_DAC_UNMUTED            0x00U
#define AIC3104_DAC_MUTED              0x80U
#define AIC3104_I2S_OUTPUT_ON          0x0DU

#define AIC3104_INPUT_0DB_ADC_ON       0x04
#define AIC3104_PGA_0DB_UNMUTED        0x00
#define AIC3104_ROUTE_ENABLE          0x80
#define AIC3104_OUTPUT_ON_UNMUTED      0x09
#define AIC3104_OUTPUT_OFF            0x04

/* ============================================================================
 * Инициализация
 * ========================================================================== */

void aic3104_soft_reset()
{
    aic3104_write_reg(1, 0x80);
}

void aic3104_init(void) {
    // Выполняем последовательность аппаратного сброса
    LL_GPIO_ResetOutputPin(AIC3104_RESET_PORT, AIC3104_RESET_PIN); // RESET = LOW
    LL_mDelay(50);                                                  // Держим 50 мс
    LL_GPIO_SetOutputPin(AIC3104_RESET_PORT, AIC3104_RESET_PIN);   // RESET = HIGH
    LL_mDelay(10);                                                  // Ждем стабилизации
}

void aic3104_init_clocking(void) {
    aic3104_write_reg(0x00, 0x00);
    aic3104_write_reg(0x01, 0x80); // исправлена запись
    
    // R 101: CODEC_CLKIN от CLKDIV_OUT
    aic3104_write_reg(101, 0x01);
    LL_mDelay(1);
}


/* ============================================================================
 * Настройка громкости
 * ========================================================================== */

/* Аппаратное значение: код ослабления без бита включения маршрута.
 * Сохраняем диапазон и шаг исходного драйвера: 72..0, шаг 3. */
#define AIC3104_VOLUME_MIN 72U
#define AIC3104_VOLUME_MAX 0U
#define AIC3104_VOLUME_STEP 3U

uint8_t aic3104_get_min_volume(void) { return AIC3104_VOLUME_MIN; }
uint8_t aic3104_get_max_volume(void) { return AIC3104_VOLUME_MAX; }

bool aic3104_volume_up(uint8_t *hardware)
{
    if (hardware == NULL || *hardware > AIC3104_VOLUME_MIN ||
        *hardware == AIC3104_VOLUME_MAX) return false;
    *hardware = *hardware <= AIC3104_VOLUME_STEP ? AIC3104_VOLUME_MAX :
                (uint8_t)(*hardware - AIC3104_VOLUME_STEP);
    return true;
}

bool aic3104_volume_down(uint8_t *hardware)
{
    if (hardware == NULL || *hardware >= AIC3104_VOLUME_MIN) return false;
    *hardware = *hardware >= AIC3104_VOLUME_MIN - AIC3104_VOLUME_STEP ?
                AIC3104_VOLUME_MIN : (uint8_t)(*hardware + AIC3104_VOLUME_STEP);
    return true;
}

static uint8_t aic3104_route_value(uint8_t hardware)
{
    if (hardware > AIC3104_VOLUME_MIN) hardware = AIC3104_VOLUME_MIN;
    return AIC3104_ROUTE_ENABLE | hardware;
}

/* Не включаем выключенные маршруты и не затрагиваем соседний источник.
 * false: ошибка обмена, недопустимый аргумент или выключенный маршрут.
 * При ошибке записи пытаемся вернуть предыдущие значения. */
bool aic3104_apply_volume(uint8_t routes, uint8_t hardware)
{
    static const uint8_t registers[] = {
        AIC3104_REG_PGA_R_HPROUT, AIC3104_REG_PGA_L_HPLOUT,
        AIC3104_REG_DAC_R_HPROUT, AIC3104_REG_DAC_L_HPLOUT
    };
    uint8_t previous[4] = {0};
    uint8_t buf[2] = {0, AIC3104_PAGE_0};
    if (hardware > AIC3104_VOLUME_MIN || routes == 0U || (routes & 0xF0U)) return false;
    if (i2c_master_tx(AIC3104_I2C_ADDR, buf, 2, I2C_TIMEOUT_MS) != 0) return false;
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!(routes & (1U << i))) continue;
        previous[i] = aic3104_read_reg(registers[i]);
        if (previous[i] == 0xFFU || !(previous[i] & AIC3104_ROUTE_ENABLE)) return false;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!(routes & (1U << i))) continue;
        buf[0] = registers[i];
        buf[1] = aic3104_route_value(hardware);
        if (i2c_master_tx(AIC3104_I2C_ADDR, buf, 2, I2C_TIMEOUT_MS) != 0)
        {
            for (unsigned j = 0; j <= i; ++j)
            {
                if (!(routes & (1U << j))) continue;
                buf[0] = registers[j]; buf[1] = previous[j];
                (void)i2c_master_tx(AIC3104_I2C_ADDR, buf, 2, I2C_TIMEOUT_MS);
            }
            return false;
        }
    }
    return true;
}


/* ============================================================================
 * Режимы работы
 * ========================================================================== */

void aic3104_line1lp_hplout_bypass(uint8_t hardware)
{
    aic3104_write_reg(0x00, AIC3104_PAGE_0);

    // LINE1LP -> PGA_L, 0 dB, single-ended; R19 D2 включает левый ADC.
    aic3104_write_reg(AIC3104_REG_LINE1LP_LEFT_ADC, AIC3104_INPUT_0DB_ADC_ON);
    aic3104_write_reg(AIC3104_REG_LEFT_PGA_GAIN, AIC3104_PGA_0DB_UNMUTED);

    // PGA_L -> HPLOUT, переданная аппаратная громкость.
    aic3104_write_reg(AIC3104_REG_PGA_L_HPLOUT, aic3104_route_value(hardware));
    aic3104_write_reg(AIC3104_REG_HPLOUT_LEVEL, AIC3104_OUTPUT_ON_UNMUTED);
}

void aic3104_line1rp_hprout_bypass(uint8_t hardware)
{
    aic3104_write_reg(0x00, AIC3104_PAGE_0);

    // LINE1RP -> PGA_R, 0 dB, single-ended; R22 D2 включает правый ADC.
    aic3104_write_reg(AIC3104_REG_LINE1RP_RIGHT_ADC, AIC3104_INPUT_0DB_ADC_ON);
    aic3104_write_reg(AIC3104_REG_RIGHT_PGA_GAIN, AIC3104_PGA_0DB_UNMUTED);

    // PGA_R -> HPROUT, переданная аппаратная громкость.
    aic3104_write_reg(AIC3104_REG_PGA_R_HPROUT, aic3104_route_value(hardware));
    aic3104_write_reg(AIC3104_REG_HPROUT_LEVEL, AIC3104_OUTPUT_ON_UNMUTED);
}

void aic3104_line1lp_hplout_bypass_stop(void)
{
    aic3104_write_reg(0x00, AIC3104_PAGE_0);

    // Отключить только PGA_L -> HPLOUT и сам HPLOUT.
    aic3104_write_reg(AIC3104_REG_PGA_L_HPLOUT, 0x00);
    aic3104_write_reg(AIC3104_REG_HPLOUT_LEVEL, AIC3104_OUTPUT_OFF);
}

void aic3104_line1rp_hprout_bypass_stop(void)
{
    aic3104_write_reg(0x00, AIC3104_PAGE_0);

    // Отключить только PGA_R -> HPROUT и сам HPROUT.
    aic3104_write_reg(AIC3104_REG_PGA_R_HPROUT, 0x00);
    aic3104_write_reg(AIC3104_REG_HPROUT_LEVEL, AIC3104_OUTPUT_OFF);
}

void aic3104_line1lp_i2s(void)
{
    aic3104_write_reg(0x00, 0x00);

    // включение ADC
    aic3104_write_reg(19, 0x04);
    aic3104_write_reg(22, 0b01111100);
    aic3104_write_reg(24, 0);
    // включение PGA
    aic3104_write_reg(15, 0x00);
    aic3104_write_reg(16, 0x00);

    aic3104_write_reg(0x0C, 0x00);
    aic3104_write_reg(107, 0x30);
}

/* R7 и R37 общие для обоих DAC. Меняем только биты нужного канала. */
static bool aic3104_update_reg_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = aic3104_read_reg(reg);
    if (current == 0xFFU) return false;

    aic3104_write_reg(reg, (uint8_t)((current & (uint8_t)~mask) | (value & mask)));
    return true;
}

void aic3104_i2s_hprout(uint8_t hardware)
{
    aic3104_write_reg(0, AIC3104_PAGE_0);

    // Right DAC получает моносумму входных I2S каналов.
    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_DATA_PATH,
                                 AIC3104_RIGHT_DAC_PATH_MASK, AIC3104_RIGHT_DAC_MONO_MIX)) return;

    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_POWER,
                                 AIC3104_RIGHT_DAC_POWER, AIC3104_RIGHT_DAC_POWER)) return;

    aic3104_write_reg(AIC3104_REG_DAC_OUTPUT_SELECT, 0x00);
    aic3104_write_reg(AIC3104_REG_RIGHT_DAC_VOLUME, AIC3104_DAC_UNMUTED);
    aic3104_write_reg(AIC3104_REG_DAC_R_HPROUT, aic3104_route_value(hardware));
    aic3104_write_reg(AIC3104_REG_HPROUT_LEVEL, AIC3104_I2S_OUTPUT_ON);
}

void aic3104_i2s_hplout(uint8_t hardware)
{
    aic3104_write_reg(0, AIC3104_PAGE_0);

    // Left DAC получает левый входной канал I2S.
    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_DATA_PATH,
                                 AIC3104_LEFT_DAC_PATH_MASK, AIC3104_LEFT_DAC_FROM_LEFT)) return;

    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_POWER,
                                 AIC3104_LEFT_DAC_POWER, AIC3104_LEFT_DAC_POWER)) return;

    aic3104_write_reg(AIC3104_REG_DAC_OUTPUT_SELECT, 0x00);
    aic3104_write_reg(AIC3104_REG_LEFT_DAC_VOLUME, AIC3104_DAC_UNMUTED);
    aic3104_write_reg(AIC3104_REG_DAC_L_HPLOUT, aic3104_route_value(hardware));
    aic3104_write_reg(AIC3104_REG_HPLOUT_LEVEL, AIC3104_I2S_OUTPUT_ON);
}

void aic3104_i2s_hprout_stop(void)
{
    aic3104_write_reg(0, AIC3104_PAGE_0);

    aic3104_write_reg(AIC3104_REG_HPROUT_LEVEL, AIC3104_OUTPUT_OFF);
    aic3104_write_reg(AIC3104_REG_DAC_R_HPROUT, 0x00);
    aic3104_write_reg(AIC3104_REG_RIGHT_DAC_VOLUME, AIC3104_DAC_MUTED);

    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_POWER,
                                 AIC3104_RIGHT_DAC_POWER, 0x00)) return;

    aic3104_update_reg_bits(AIC3104_REG_DAC_DATA_PATH, AIC3104_RIGHT_DAC_PATH_MASK, 0x00);
}

void aic3104_i2s_hplout_stop(void)
{
    aic3104_write_reg(0, AIC3104_PAGE_0);

    aic3104_write_reg(AIC3104_REG_HPLOUT_LEVEL, AIC3104_OUTPUT_OFF);
    aic3104_write_reg(AIC3104_REG_DAC_L_HPLOUT, 0x00);
    aic3104_write_reg(AIC3104_REG_LEFT_DAC_VOLUME, AIC3104_DAC_MUTED);

    if (!aic3104_update_reg_bits(AIC3104_REG_DAC_POWER,
                                 AIC3104_LEFT_DAC_POWER, 0x00)) return;

    aic3104_update_reg_bits(AIC3104_REG_DAC_DATA_PATH, AIC3104_LEFT_DAC_PATH_MASK, 0x00);
}


/* ============================================================================
 * Вспомогательные функции
 * ========================================================================== */

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


/* ============================================================================
 * TODO
 * ========================================================================== */

// void aic3104_line1rp_to_i2s(void)
// {
//     aic3104_write_reg(0x00, 0x00);

//     // включение ADC
//     aic3104_write_reg(19, 0b01111100);
//     aic3104_write_reg(21, 0);
//     aic3104_write_reg(22, 0x04);
//     // включение PGA
//     aic3104_write_reg(0x0F, 0x00);
//     aic3104_write_reg(16, 0x00);

//     aic3104_write_reg(0x0C, 0x00);
//     aic3104_write_reg(107, 0x30);
// }

// void aic3104_i2s_to_hplout(){
//         // Page 0
//     aic3104_write_reg(0, 0x00);

//     // R7: Left DIN -> Left DAC, Normal Rate
//     aic3104_write_reg(7, 0x08);

//     // R37: включить Left DAC
//     aic3104_write_reg(37, 0x80);

//     // R41: Left DAC -> DAC_L1, Right DAC -> DAC_R1
//     aic3104_write_reg(41, 0x00);

//     // R43: Left DAC, 0 dB, Mute OFF
//     aic3104_write_reg(43, 0x00);

//     // R47: подключить DAC_L1 -> HPLOUT, 0 dB
//     aic3104_write_reg(47, aic3104_route_value(hardware));

//     // R51: HPLOUT Power ON, Mute OFF, 0 dB
//     aic3104_write_reg(51, 0x0D);
// }
