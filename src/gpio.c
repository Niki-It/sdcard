#include "stm32f4xx.h"
#include "gpio.h"
#include "stm32f4xx_ll_gpio.h"

#include "soft_i2c.h"
#include "aic3104.h"
#include "mux.h"
#include "led_controller/led_raw/led_raw.h"

void GPIO_Init(void)
{
    // === 1. Настройка пинов Soft I2C (PB10, PB11) ===
    LL_GPIO_InitTypeDef gpio_i2c = {0};
    gpio_i2c.Pin = SOFT_I2C_SCL_PIN | SOFT_I2C_SDA_PIN;
    gpio_i2c.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_i2c.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_i2c.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio_i2c.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(SOFT_I2C_PORT, &gpio_i2c);

    // === 2. Настройка пина RESET кодека (PC7) ===
    LL_GPIO_InitTypeDef gpio_rst = {0};
    gpio_rst.Pin = AIC3104_RESET_PIN;
    gpio_rst.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_rst.Speed = LL_GPIO_SPEED_FREQ_LOW; // Для RESET высокая скорость не нужна
    gpio_rst.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_rst.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(AIC3104_RESET_PORT, &gpio_rst);

    // === 3. Настройка пинов MUX1 (PA4, PA5, PA6) ===
    LL_GPIO_InitTypeDef gpio_mux1 = {0};
    gpio_mux1.Pin = MUX1_A0_PIN | MUX1_A1_PIN | MUX1_A2_PIN;
    gpio_mux1.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_mux1.Speed = LL_GPIO_SPEED_FREQ_LOW; // Для управления MUX высокая скорость не нужна
    gpio_mux1.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_mux1.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(MUX1_PORT, &gpio_mux1);
    
    // === 4. Настройка пинов MUX2 (PC1, PC2, PC3) ===
    LL_GPIO_InitTypeDef gpio_mux2 = {0};
    gpio_mux2.Pin = MUX2_A0_PIN | MUX2_A1_PIN | MUX2_A2_PIN;
    gpio_mux2.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_mux2.Speed = LL_GPIO_SPEED_FREQ_LOW;
    gpio_mux2.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_mux2.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(MUX2_PORT, &gpio_mux2);

    // === 5. Настройка пина MCLK (PC6) ===
    LL_GPIO_InitTypeDef gpio_i2s2 = {0};
    gpio_i2s2.Pin        = LL_GPIO_PIN_6;
    gpio_i2s2.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio_i2s2.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_i2s2.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_i2s2.Pull       = LL_GPIO_PULL_NO;
    gpio_i2s2.Alternate  = LL_GPIO_AF_5;  // AF5 для SPI2/I2S2
    LL_GPIO_Init(GPIOC, &gpio_i2s2);

    // === 6. Настройка пинов USB OTG FS (PA9, PA10, PA11, PA12) ===
    // PA11 (D-) и PA12 (D+) - Линии данных
    LL_GPIO_InitTypeDef gpio_usb_data = {0};
    gpio_usb_data.Pin        = LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
    gpio_usb_data.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio_usb_data.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH; // Критично для целостности фронтов сигнала USB 2.0 FS
    gpio_usb_data.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_usb_data.Pull       = LL_GPIO_PULL_NO;              // Строго без подтяжки!
    gpio_usb_data.Alternate  = LL_GPIO_AF_10;                // AF10 для USB OTG FS
    LL_GPIO_Init(GPIOA, &gpio_usb_data);

    // PA10 (ID) - Определение роли (Host/Device)
    LL_GPIO_InitTypeDef gpio_usb_id = {0};
    gpio_usb_id.Pin        = LL_GPIO_PIN_10;
    gpio_usb_id.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio_usb_id.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_usb_id.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;       // Open-Drain по спецификации USB для пина ID
    gpio_usb_id.Pull       = LL_GPIO_PULL_UP;                // Подтяжка вверх (высокий уровень = режим Device)
    gpio_usb_id.Alternate  = LL_GPIO_AF_10;
    LL_GPIO_Init(GPIOA, &gpio_usb_id);

    // PA9 (VBUS) - Датчик наличия питания (с делителем на плате)
    LL_GPIO_InitTypeDef gpio_usb_vbus = {0};
    gpio_usb_vbus.Pin        = LL_GPIO_PIN_9;
    gpio_usb_vbus.Mode       = LL_GPIO_MODE_ALTERNATE;       // Передаем управление пину периферии USB
    gpio_usb_vbus.Speed      = LL_GPIO_SPEED_FREQ_LOW;       // Для входа VBUS высокая скорость не нужна
    gpio_usb_vbus.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_usb_vbus.Pull       = LL_GPIO_PULL_DOWN;            // Подтяжка вниз, чтобы гарантировать '0' при отключенном кабеле
    gpio_usb_vbus.Alternate  = LL_GPIO_AF_10;
    LL_GPIO_Init(GPIOA, &gpio_usb_vbus);

    // === 7. Настройка пинов SDIO ===
    // Порт C (PC8: DAT0, PC9: DAT1, PC10: DAT2, PC11: DAT3, PC12: CLK)
    LL_GPIO_InitTypeDef gpio_sdio_c = {0};
    gpio_sdio_c.Pin        = LL_GPIO_PIN_8 | LL_GPIO_PIN_9 | LL_GPIO_PIN_10 | LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
    gpio_sdio_c.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio_sdio_c.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH; // Критично для работы на высоких частотах SDIO (до 48 МГц)
    gpio_sdio_c.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    // gpio_sdio_c.Pull       = LL_GPIO_PULL_UP;              // Подтяжка уже выполнена аппаратно
    gpio_sdio_c.Alternate  = LL_GPIO_AF_12;                // AF12 для SDIO в STM32F4
    LL_GPIO_Init(GPIOC, &gpio_sdio_c);

    // Порт D (PD2: CMD)
    LL_GPIO_InitTypeDef gpio_sdio_d = {0};
    gpio_sdio_d.Pin        = LL_GPIO_PIN_2;
    gpio_sdio_d.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio_sdio_d.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_sdio_d.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    // gpio_sdio_d.Pull       = LL_GPIO_PULL_UP;              // Подтяжка уже выполнена аппаратно
    gpio_sdio_d.Alternate  = LL_GPIO_AF_12;                // AF12 для SDIO в STM32F4
    LL_GPIO_Init(GPIOD, &gpio_sdio_d);

    led_controller_gpio_init();
}