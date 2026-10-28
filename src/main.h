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
#include "button_combo.h"
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
#include "storage/tinyusb_serve.h"

// Состояния
typedef enum {
    IDLE,
    TRK,
    RECORDING,
    BYPASS,
    ACHTUNG,

    NOTIFICATION,
    BROADCAST,
    PRM,
    FIRE,
    ALARM, 
    PLAYING,
} app_state_t;

// События
typedef enum {
    APP_EVENT_RECORD_START,
    APP_EVENT_RECORD_STOP,

    APP_EVENT_PLAY_REQUEST,
    APP_EVENT_PLAYBACK_FINISHED,
    APP_EVENT_PLAYBACK_STOP,
    APP_EVENT_PLAYBACK_ERROR,

    APP_EVENT_BYPASS_START,
    APP_EVENT_BYPASS_STOP,

    

    APP_EVENT_RECORDING_ERROR,

    APP_EVENT_ACHTUNG_ON,
    APP_EVENT_ACHTUNG_OFF,

    APP_EVENT_TRK_PRESSED,
    APP_EVENT_TRK_RELEASED,

    // режимы

    // Оповещение
    APP_EVENT_NOTIFICATION_START,
    APP_EVENT_NOTIFICATION_FINISH,

    // Прием
    APP_EVENT_PRM_START,
    APP_EVENT_PRM_FINISH,

    // Передача
    APP_EVENT_BROADCAST_START,
    APP_EVENT_BROADCAST_FINISH,

    // Речевое пожарное
    APP_EVENT_FIRE_START,
    APP_EVENT_FIRE_FINISH,

    // Тревога
    APP_EVENT_ALARM_START,
    // Воспроизведение
    APP_EVENT_PLAYING_START,
    APP_EVENT_ALARM_PLAYING_FINISH,

    // + и -
    APP_EVENT_VOLUME_UP,
    APP_EVENT_VOLUME_DOWN,
} app_event_t;

/* ============================================================================
 * Прототипы внешних функций
 * ========================================================================== */

extern void diskio_set_sd_card(sd_card_info_t *info);


/* ============================================================================
 * Прототипы функций
 * ========================================================================== */

int main(void);
void periph_init(void);
void SysTick_Handler(void);

void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT]);

static void process_achtung_button(void);
static void process_trk_button(void);

/* Управление усилителями. */
static void mute_amplifiers(void);
static void unmute_amplifiers(void);

/* Запуск и завершение режимов. */
static FRESULT start_recording(void);
static void stop_recording(app_event_t reason, FRESULT error);

static FRESULT start_playback(void);
static void finish_playback(app_event_t reason, FRESULT error);

static void start_bypass(void);
static void stop_bypass(void);

static void start_trk(void);
static void stop_trk(void);

static void interrupt_mode(app_state_t previous_state);
static void app_playback_abort();

/* Обработка событий и обслуживание текущего состояния. */
static int process_common_event(app_event_t event);
static void process_state_event(app_event_t event, FRESULT error);
static void process_state_logic(void);

/* Проверка события отдельной кнопки в комбинации. */
static int combo_has(uint8_t mask, const LedEvent events[BUTTON_COMBO_COUNT], uint8_t button, LedEvent event);


typedef enum 
{
    GGC = 0,
    APB = 1,
    OPER = 2
} RelayMode;
static void swich_relay(RelayMode mode);

