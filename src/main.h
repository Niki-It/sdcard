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
#include "count_bits_table.h"
#include "amp.h"
#include "announce.h"

// Состояния
typedef enum {
    IDLE,
    NOTIFICATION,
    BROADCAST,
    PRM,
    FIRE,
    ALARM, 
    PLAYING_FR1,
    PLAYING_FR2,
    RECORDING_FR1,
    RECORDING_FR2
} app_state_t;

// События
typedef enum {
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

    // Запись ФР1, ФР2
    APP_EVENT_FR1_RECORD_START,
    APP_EVENT_FR1_RECORD_ERROR,
    APP_EVENT_FR1_RECORD_FINISH,

    APP_EVENT_FR2_RECORD_START,
    APP_EVENT_FR2_RECORD_ERROR,
    APP_EVENT_FR2_RECORD_FINISH,

    // Тревога
    APP_EVENT_ALARM_START,
    APP_EVENT_ALARM_FINISH,

    // Воспроизведение
    APP_EVENT_PLAYING_FR1_START,
    APP_EVENT_PLAYING_FR1_FINISH,
    APP_EVENT_PLAYING_FR2_START,
    APP_EVENT_PLAYING_FR2_FINISH,
    // Окончание FR и Alarm
    APP_EVENT_ALARM_FR_FINISH,
    APP_EVENT_PLAYBACK_ERROR,
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

/* Обработка событий и обслуживание текущего состояния. */
static int process_common_event(app_event_t event);
static void process_state_event(app_event_t event, FRESULT error);
static void process_state_logic(void);


// ------------------------ ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ СТЕЙТ МАШИНЫ --------------------
static void start_bypass(void);
static void stop_bypass(void);

static FRESULT start_recording(const char* path);
static void stop_recording(FRESULT error);
static void app_playback_abort(void);
static FRESULT start_playback(const char* path);
static void finish_playback(FRESULT error);
static void play_next_announce(void);
static void playback_file_finished(void);
static void switch_active_remote_mux(void);