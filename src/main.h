#pragma once

#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_ll_utils.h"
#include <stm32f4xx_ll_bus.h>
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"

#include "SEGGER_RTT.h"

#include "NVIC.h"
#include "gpio.h"
#include "clock.h"
#include "aic3104.h"
#include "mux.h"
#include "debounce.h"
#include "i2s/i2s2.h"
#include "tusb.h"
#include "sdio/sdio.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw.h"
#include "button_combo/button_combo.h"
#include "storage/fs.h"
#include "../lib/Helix/src/pub/mp3dec.h"
#include "media/mp3_decoder.h"
#include "media/wav_encoder.h"
#include "media/wav_decoder.h"
#include "media/audio_playback.h"
#include "perf_counter/perf_counter.h"
#include "i2c.h"
#include "stdbool.h"
#include "stdio.h"
#include "memory.h"
#include "amp.h"
#include "announce.h"
#include "button_combo/source.h"


/* ============================================================================
 * Прототипы внешних функций
 * ========================================================================== */
typedef enum
{
    HANDLER_PROCESSING = 0,
    HANDLER_FINISHED,
    HANDLER_ERROR
} handler_responce_t;
typedef enum
{
    ACHTUNG_PRESSED = 0,
    ACHTUNG_RELEASED,
    TRK_PRESSED,
    TRK_RELEASED,
    RECEIVE_START,
    RECEIVE_FINISH,
    RECEIVE_TOGGLE,
    AUTO_RECEIVE_TOGGLE,
    RECEIVE_TIMEOUT,
    GGS1_START,
    GGS1_FINISH,
    GGS2_START,
    GGS2_FINISH,
    APB1_START,
    APB1_FINISH,
    APB2_START,
    APB2_FINISH,
    AUTO_ANNOUNCE1_TOGGLE,
    AUTO_ANNOUNCE2_TOGGLE,
    ANNOUNCE1_START,
    ANNOUNCE1_FINISH,
    ANNOUNCE2_START,
    ANNOUNCE2_FINISH,
    VOLUME_CHANGE,
    ALERT_TOGGLE,
    RECORD1_START,
    RECORD1_FINISH,
    RECORD2_START,
    RECORD2_FINISH,
    PLAY1_GGS1_START,
    PLAY1_GGS2_START,
    PLAY1_APB1_START,
    PLAY1_APB2_START,
    PLAY1_RECEIVE_START,
    PLAY2_GGS1_START,
    PLAY2_GGS2_START,
    PLAY2_APB1_START,
    PLAY2_APB2_START,
    PLAY2_RECEIVE_START,
    PLAY_GGS1_FINISH,
    PLAY_GGS2_FINISH,
    PLAY_APB1_FINISH,
    PLAY_APB2_FINISH,
    PLAY_RECEIVE_FINISH
} app_event_t;

extern void diskio_set_sd_card(sd_card_info_t *info);


/* ============================================================================
 * Прототипы функций
 * ========================================================================== */

int main(void);
void periph_init(void);
void SysTick_Handler(void);

/* Entry point for button combination events; implementation in main.c. */
void flush_combo(FlushContext context);
