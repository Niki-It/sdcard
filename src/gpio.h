#ifndef GPIO_H
#define GPIO_H

#include "stm32f4xx.h"
#include "stm32f4xx_ll_gpio.h"

#include "aic3104.h"
#include "mux.h"
#include "led_controller/led_raw.h"

#define CODEC_I2C_PORT       GPIOB
#define CODEC_I2C_SCL_PIN    LL_GPIO_PIN_10
#define CODEC_I2C_SDA_PIN    LL_GPIO_PIN_11
#define CODEC_I2C_GPIO_AF    LL_GPIO_AF_4

#define AMP_PORT        GPIOB
#define APB_PIN         LL_GPIO_PIN_6
#define OPR_PIN         LL_GPIO_PIN_7
#define MUTE_AMP1_PIN   LL_GPIO_PIN_8
#define MUTE_AMP2_PIN   LL_GPIO_PIN_9

void GPIO_Init(void);

#endif