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
    ALERT_TOGGLE = 0,
    ACHTUNG_PRESSED,
    ACHTUNG_RELEASED,
    TRK_PRESSED,
    TRK_RELEASED,
    AUTO_RECEIVE_TOGGLE,
    RECEIVE_TOGGLE,
    RECEIVE_HOLD,
    RECEIVE_RELEASE,
    RECEIVE_FINISHED,
    GGS1_HOLD,
    GGS2_HOLD,
    APB1_HOLD,
    APB2_HOLD,
    GGS1_RELEASE,
    GGS2_RELEASE,
    APB1_RELEASE,
    APB2_RELEASE,
    AUTO_ANNOUNCE_1_TOGGLE,
    AUTO_ANNOUNCE_2_TOGGLE,
    ANNOUNCE_START,
    ANNOUNCE_FINISHED,
    ANNOUNCE_1_HOLD,
    ANNOUNCE_2_HOLD,
    ANNOUNCE_1_RELEASE,
    ANNOUNCE_2_RELEASE,
    PHRASE_1_PRM,
    PHRASE_1_GGS1,
    PHRASE_1_GGS2,
    PHRASE_1_APB1,
    PHRASE_1_APB2,
    PHRASE_2_PRM,
    PHRASE_2_GGS1,
    PHRASE_2_GGS2,
    PHRASE_2_APB1,
    PHRASE_2_APB2,
    PHRASE_FINISHED,
    RECORD_UNLOCK,
    RECORD_UNLOCK_EXPIRED,
    RECORD_CHECK_COMBINATION,
    VOLUME_UP,
    VOLUME_DOWN
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

/* Миллисекунды от SysTick; переполнение учитывается при вычитании. */
extern volatile uint32_t uptime_ms;
