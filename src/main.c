#include "main.h"
#include "aic3104.h"
#include "mux.h"
#include "debounce.h"
#include "i2s/i2s2.h"
#include "tusb.h"
#include "sdio/sdio.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"
#include "button_combo.h"
#include "storage/fs.h"
#include "../lib/Helix/src/pub/mp3dec.h"
#include "media/mp3_decoder.h"
#include "media/wav_encoder.h"
#include "media/wav_decoder.h"
#include "media/audio_playback.h"
#include "perf_counter/perf_counter.h"
#include "i2c.h"
#include "led_blinking.h"
#include "stdbool.h"

/* ============================================================================
 * Константы 
 * ========================================================================== */

#define AMP_PORT        GPIOB
#define APB_PIN         LL_GPIO_PIN_6
#define OPR_PIN         LL_GPIO_PIN_7
#define MUTE_AMP1_PIN   LL_GPIO_PIN_8
#define MUTE_AMP2_PIN   LL_GPIO_PIN_9

#define AUDIO_FILENAME "record.wav"

#define DEBOUNCE_PERIOD_MS          1U  // Периодичность вызова обработчика антидребезга
#define ACHTUNG_DEBOUNCE_THRESHOLD  5U  // Количество вызовов обработчика с неизменным состоянием сигнала до его подтверждения
#define TRK_DEBOUNCE_THRESHOLD      15U

/* ============================================================================
 * Типы данных
 * ========================================================================== */

// Состояния
typedef enum {
    IDLE,
    TRK,
    RECORDING,
    PLAYING,
    BYPASS,
    ACHTUNG
} app_state_t;

// События
typedef enum {
    APP_EVENT_RECORD_START,
    APP_EVENT_RECORD_STOP,

    APP_EVENT_PLAY_REQUEST,
    APP_EVENT_PLAYBACK_FINISHED,
    APP_EVENT_PLAYBACK_ERROR,

    APP_EVENT_BYPASS_START,
    APP_EVENT_BYPASS_STOP,

    APP_EVENT_VOLUME_UP,
    APP_EVENT_VOLUME_DOWN,

    APP_EVENT_RECORDING_ERROR,

    APP_EVENT_ACHTUNG_ON,
    APP_EVENT_ACHTUNG_OFF,

    APP_EVENT_TRK_PRESSED,
    APP_EVENT_TRK_RELEASED
} app_event_t;

// Пины с антидребезгом
typedef enum {
    DEBOUNCE_INPUT_ACHTUNG,
    DEBOUNCE_INPUT_TRK,
    DEBOUNCE_INPUT_COUNT
} debounce_input_t;


/* ============================================================================
 * Глобальные переменные
 * ========================================================================== */

uint32_t tick_counter = 1;
uint32_t message_counter = 0;
uint32_t send_counter = 0;


sd_card_info_t card_info;

static pin_debounce_t debounced_pins[DEBOUNCE_INPUT_COUNT] = {
    [DEBOUNCE_INPUT_ACHTUNG] = {
        .port = GPIOC,
        .pin = LL_GPIO_PIN_15,
        .threshold = ACHTUNG_DEBOUNCE_THRESHOLD
    },
    [DEBOUNCE_INPUT_TRK] = {
        .port = GPIOC,
        .pin = LL_GPIO_PIN_13,
        .threshold = TRK_DEBOUNCE_THRESHOLD
     }
};

// Отсчитывает миллисекунды - не удалять
volatile uint32_t uptime_ms = 0;
static app_state_t state = IDLE;


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


// TODO: убрать это говно и переписать по нормальному
extern uint8_t loud_max;
extern uint8_t loud_min;
/* ============================================================================
 * Точка входа и основной цикл
 * ========================================================================== */

int main(void)
{
    periph_init();
    fs_unit(true);
    tusb_init();

    mux_select(2, MUX_CHANNEL_X1);
    aic3104_init();
    i2s_dma_init();

    debounce_init(debounced_pins, DEBOUNCE_INPUT_COUNT, DEBOUNCE_PERIOD_MS);

    mute_amplifiers();

    static uint32_t max_end = 0;

    set_leds(VD1 | VD2 | VD3, true);

    while (1) 
    {
        uint32_t start = perf_counter_now();

        pin_polling();
        debounce_process(uptime_ms);
        process_achtung_button();
        process_trk_button();
        
        ButtonStatus button_status = poll_button_events();

        process_button_combinations(&button_status);
        process_leds(uptime_ms);
        process_state_logic();

        uint32_t end = perf_counter_elapsed(start);

        if(end > max_end)
        {
            max_end = end;
        }

        if (can_send_command()) 
        {
            send_SetLedState(USART2);
        }
    }
}


/* ============================================================================
 * Преобразование комбинаций кнопок в события приложения
 * ========================================================================== */

static int combo_has(uint8_t mask, const LedEvent events[BUTTON_COMBO_COUNT], uint8_t button, LedEvent event)
{
    return (mask & (1U << button)) != 0U && events[button] == event;
}

void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT])
{
    /*
     * Отпускание кнопки текущего режима завершает режим и поглощает
     * всю комбинацию. Остальные события этой комбинации игнорируются.
     */
    if (state == RECORDING &&
        combo_has(buttons_mask, events, 0, NO_EVENT)) {
        process_state_event(APP_EVENT_RECORD_STOP, FR_OK);
        return;
    }

    if (state == BYPASS &&
        combo_has(buttons_mask, events, 2, NO_EVENT)) {
        process_state_event(APP_EVENT_BYPASS_STOP, FR_OK);
        return;
    }

    /*
     * Из одной комбинации выбирается не более одного запроса режима.
     * Если запрос отклонён или запуск не удался, другой режим не запускается.
     */
    if (combo_has(buttons_mask, events, 0, LONG_EVENT)) {
        process_state_event(APP_EVENT_RECORD_START, FR_OK);
    } else if (combo_has(buttons_mask, events, 1, SHORT_EVENT)) {
        process_state_event(APP_EVENT_PLAY_REQUEST, FR_OK);
    } else if (combo_has(buttons_mask, events, 2, LONG_EVENT)) {
        process_state_event(APP_EVENT_BYPASS_START, FR_OK);
    }

    /* Регулировка громкости обрабатывается после запроса режима. */
    if (combo_has(buttons_mask, events, 6, SHORT_EVENT)) {
        process_state_event(APP_EVENT_VOLUME_UP, FR_OK);
    }

    if (combo_has(buttons_mask, events, 7, SHORT_EVENT)) {
        process_state_event(APP_EVENT_VOLUME_DOWN, FR_OK);
    }
}


/* ============================================================================
 * Обработка событий и переходов между состояниями
 * ========================================================================== */

static void process_state_event(app_event_t event, FRESULT error)
{
    /* Включение ACHTUNG разрешено из любого обычного режима. */
    if (event == APP_EVENT_ACHTUNG_ON) {
        if (state != ACHTUNG) {
            interrupt_mode(state);

            state = ACHTUNG;
            SEGGER_RTT_printf(0, "ACHTUNG started\r\n");
        }

        return;
    }

    /* В режиме ACHTUNG обрабатывается только событие выключения. */
    if (state == ACHTUNG) {
        if (event == APP_EVENT_ACHTUNG_OFF) {
            state = IDLE;
            SEGGER_RTT_printf(0, "ACHTUNG finished\r\n");
        }

        return;
    }

    /* Общие команды не изменяют состояние. */
    if (process_common_event(event)) {
        return;
    }

    /* Переходы обычных режимов. */
    switch (state) {
    case IDLE:
        switch (event) {
        case APP_EVENT_RECORD_START:
            if (start_recording() == FR_OK) {
                state = RECORDING;
            }
            break;

        case APP_EVENT_PLAY_REQUEST:
            if (start_playback() == FR_OK) {
                state = PLAYING;
            }
            break;

        case APP_EVENT_TRK_PRESSED:
            start_trk();
            break;

        case APP_EVENT_BYPASS_START:
            start_bypass();
            state = BYPASS;
            break;

        default:
            break;
        }
        break;

    case RECORDING:
        if (event == APP_EVENT_RECORD_STOP ||
            event == APP_EVENT_RECORDING_ERROR) {
            stop_recording(event, error);
            state = IDLE;
        }
        break;

    case PLAYING:
        if (event == APP_EVENT_PLAYBACK_FINISHED ||
            event == APP_EVENT_PLAYBACK_ERROR) {
            finish_playback(event, error);
            state = IDLE;
        }
        break;

    case BYPASS:
        if (event == APP_EVENT_BYPASS_STOP) {
            stop_bypass();
            state = IDLE;
        }
        break;
    case TRK:
        if (event == APP_EVENT_TRK_RELEASED) {
            stop_trk();
        }
        break;
    default:
        break;
    }
}


/* ============================================================================
 * Общие команды без изменения состояния
 * ========================================================================== */

/* Возвращает 1, если событие обработано, иначе 0. */
static int process_common_event(app_event_t event)
{
    switch (event) {
    case APP_EVENT_VOLUME_UP:
        aic3104_loud_up();

        if (!loud_min) {
            request_blinking(VD8, 0, 0);
            set_leds(LED_VD8, false);
        }

        request_blinking(VD7, 0, 0);
        set_leds(LED_VD7, loud_max != 0U);

        if (!loud_max) {
            request_blinking(VD7, 2, 200);
        }

        SEGGER_RTT_printf(0, "+\r\n");
        return 1;

    case APP_EVENT_VOLUME_DOWN:
        aic3104_loud_down();

        if (!loud_max) {
            request_blinking(VD7, 0, 0);
            set_leds(LED_VD7, false);
        }

        request_blinking(VD8, 0, 0);
        set_leds(LED_VD8, loud_min != 0U);

        if (!loud_min) {
            request_blinking(VD8, 2, 200);
        }

        SEGGER_RTT_printf(0, "-\r\n");
        return 1;

    default:
        return 0;
    }
}


/* ============================================================================
 * Периодическое обслуживание текущего состояния
 * ========================================================================== */

static void process_state_logic(void)
{
    switch (state) {
    case PLAYING: {
        FRESULT result = audio_playback_task();

        if (result != FR_OK) {
            process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
        } else if (!audio_playback_is_active()) {
            process_state_event(APP_EVENT_PLAYBACK_FINISHED, FR_OK);
        }
        break;
    }

    case RECORDING: {
        FRESULT result = wav_recording_task();

        if (result != FR_OK) {
            process_state_event(APP_EVENT_RECORDING_ERROR, result);
        }
        break;
    }

    // Без переодического обслуживания
    case IDLE:
    case BYPASS:
    case ACHTUNG:
    case TRK:
        break;

    default:
        break;
    }
}


/* ============================================================================
 * Преобразование состояния кнопки ACHTUNG в события
 * ========================================================================== */

static void process_achtung_button(void)
{
    static uint8_t previous_state = 1;

    uint8_t current_state = debounced_pins[DEBOUNCE_INPUT_ACHTUNG].state;

    /* Событие формируется только при изменении обработанного уровня. */
    if (current_state == previous_state) {
        return;
    }

    previous_state = current_state;

    if (current_state != 1U) {
        process_state_event(APP_EVENT_ACHTUNG_ON, FR_OK);
    } else {
        process_state_event(APP_EVENT_ACHTUNG_OFF, FR_OK);
    }
}
static void process_trk_button(void)
{
    static uint8_t previous_state = 1U;
    uint8_t current_state = debounced_pins[DEBOUNCE_INPUT_TRK].state;

    if (current_state == previous_state) {
        return;
    }

    previous_state = current_state;

    if (current_state == 0U) {
        process_state_event(APP_EVENT_TRK_PRESSED, FR_OK);
    } else {
        process_state_event(APP_EVENT_TRK_RELEASED, FR_OK);
    }
}



/* ============================================================================
 * Управление усилителями
 * ========================================================================== */

static void mute_amplifiers(void)
{
    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
}

static void unmute_amplifiers(void)
{
    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
}


/* ============================================================================
 * Действия режима записи
 * ========================================================================== */

/* Функции действий не изменяют состояние и не отправляют события. */
static FRESULT start_recording(void)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_line1lp_to_i2s();

    FRESULT result = wav_recording_start(AUDIO_FILENAME);

    if (result == FR_OK) {
        SEGGER_RTT_printf(0, "WAV recording started\r\n");
    } else {
        SEGGER_RTT_printf(
            0, "WAV recording start error: %d\r\n", (int)result
        );
    }

    return result;
}

static void stop_recording(app_event_t reason, FRESULT error)
{
    FRESULT result = wav_recording_end();

    mute_amplifiers();

    switch (reason) {
    case APP_EVENT_RECORDING_ERROR:
        SEGGER_RTT_printf(
            0, "WAV write error: %d, end: %d\r\n",
            (int)error, (int)result
        );
        break;

    case APP_EVENT_ACHTUNG_ON:
        SEGGER_RTT_printf(
            0, "WAV recording interrupted: %d\r\n", (int)result
        );
        break;

    default:
        SEGGER_RTT_printf(
            0, "WAV recording finished: %d\r\n", (int)result
        );
        break;
    }
}


/* ============================================================================
 * Действия режима воспроизведения
 * ========================================================================== */

static FRESULT start_playback(void)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_i2s_to_hprout();

    FRESULT result = audio_playback_start(
        AUDIO_FILENAME, AUDIO_FORMAT_WAV
    );

    if (result == FR_OK) {
        unmute_amplifiers();
        SEGGER_RTT_printf(0, "WAV playback started\r\n");
    } else {
        SEGGER_RTT_printf(
            0, "WAV playback start error: %d\r\n", (int)result
        );
    }

    return result;
}

/*
 * При штатном завершении и ошибке воспроизведение уже остановлено
 * внутри audio_playback_task().
 */
static void finish_playback(app_event_t reason, FRESULT error)
{
    mute_amplifiers();

    if (reason == APP_EVENT_PLAYBACK_ERROR) {
        SEGGER_RTT_printf(
            0, "WAV playback error: %d\r\n", (int)error
        );
    } else {
        SEGGER_RTT_printf(0, "WAV playback finished\r\n");
    }
}


/* ============================================================================
 * Действия режима аналогового обхода
 * ========================================================================== */

static void start_bypass(void)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_init_analog_bypass();

    unmute_amplifiers();

    SEGGER_RTT_printf(0, "Bypass started\r\n");
}

static void stop_bypass(void)
{
    mute_amplifiers();

    SEGGER_RTT_printf(0, "Bypass finished\r\n");
}

static void start_trk(void)
{
    mux_select(2, MUX_CHANNEL_X2);
    start_bypass();

    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_7);

    SEGGER_RTT_printf(0, "TRK started\r\n");
    state = TRK;
}

static void stop_trk(void)
{
    stop_bypass();

    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_7);
    mux_select(2, MUX_CHANNEL_X1);

    SEGGER_RTT_printf(0, "TRK finished\r\n");
    state = IDLE;
}


/* ============================================================================
 * Завершение текущего режима перед входом в ACHTUNG
 * ========================================================================== */

static void interrupt_mode(app_state_t previous_state)
{
    mute_amplifiers();

    switch (previous_state) {
    case RECORDING:
        stop_recording(APP_EVENT_ACHTUNG_ON, FR_OK);
        break;

    case PLAYING:
        app_playback_abort();
        break;

    case BYPASS:
        stop_bypass();
        break;
    case TRK:
        stop_trk();
        break;

    default:
        break;
    }
}


/* ============================================================================
 * Инициализация периферии
 * ========================================================================== */

void periph_init(void)
{
    /* Отладочный журнал и системное тактирование. */
    SEGGER_RTT_Init();

    Clock_Init();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    /* Порты, прерывания и интерфейсы периферии. */
    GPIO_Init();
    NVIC_Init();

    LedController_PeriphInit();
    i2c_init();

    /* Карта памяти и её подключение к дисковому интерфейсу. */
    sd_status_t status = sd_init(&card_info);

    if (status != SD_OK) {
        SEGGER_RTT_printf(0, "SD init error: %d\r\n", status);
        return;
    }

    diskio_set_sd_card(&card_info);

    /* Счётчик измерения производительности. */
    perf_counter_init();
}


/* ============================================================================
 * Обработчик системного таймера
 * ========================================================================== */

void SysTick_Handler(void)
{
    uptime_ms++;
    tick_counter++;

    if (tick_counter >= 1000U) {
        tick_counter = 0;
    }
}

// TODO
static void app_playback_abort()
{

}