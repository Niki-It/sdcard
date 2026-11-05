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
    GGS2_FINISH
} app_event_t;

extern void diskio_set_sd_card(sd_card_info_t *info);


/* ============================================================================
 * Прототипы функций
 * ========================================================================== */

int main(void);
void periph_init(void);
void SysTick_Handler(void);

/* Стейт машина */
void flush_combo(FlushContext context);
static void process_state_event(app_event_t event, source_t source);
static void process_state_logic(void);

// Внешние сигналы
static void process_achtung_button(void);
static void process_trk_button(void);

// ------------------------ ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ СТЕЙТ МАШИНЫ --------------------
// static void start_bypass(void);
// static void stop_bypass(void);

// static FRESULT start_recording(const char* path);
// static void stop_recording(FRESULT error);
// static void app_playback_abort(void);
// static FRESULT start_playback(const char* path);
// static void finish_playback(FRESULT error);
// static void play_next_announce(void);
// static void playback_file_finished(void);
// static void switch_active_remote_mux(void);