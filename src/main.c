#include "main.h"
#include "string.h"


/* ============================================================================
 * Данные
 * ========================================================================== */
#define DEBOUNCE_PERIOD_MS          1U  // Периодичность вызова обработчика антидребезга
#define ACHTUNG_DEBOUNCE_THRESHOLD  5U  // Количество вызовов обработчика с неизменным состоянием сигнала до его подтверждения
#define TRK_DEBOUNCE_THRESHOLD      15U

#define BUTTON_COMBO_TIMEOUT_MS 150U

#define PLAYBACK_WAV 0
#define PLAYBACK_MP3 1

// Пины с антидребезгом
typedef enum {
    DEBOUNCE_INPUT_ACHTUNG,
    DEBOUNCE_INPUT_TRK,
    DEBOUNCE_INPUT_COUNT
} debounce_input_t;

static pin_debounce_t debounced_pins[DEBOUNCE_INPUT_COUNT] = {
    [DEBOUNCE_INPUT_ACHTUNG] = {
        .port = GPIOC,
        .pin = LL_GPIO_PIN_15,
        .threshold = ACHTUNG_DEBOUNCE_THRESHOLD,
        .raw_state = 1,
        .state = 1
    },
    [DEBOUNCE_INPUT_TRK] = {
        .port = GPIOC,
        .pin = LL_GPIO_PIN_13,
        .threshold = TRK_DEBOUNCE_THRESHOLD,
        .raw_state = 1,
        .state = 1
     }
};

// Отсчитывает миллисекунды
volatile uint32_t uptime_ms = 0;

static app_state_t state = IDLE;
sd_card_info_t card_info;

/* ============================================================================
 * Основная логика
 * ========================================================================== */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    button_combo_init(BUTTON_COMBO_TIMEOUT_MS);
    periph_init();
    tusb_init();
    fs_unit(true);
    aic3104_init();
    i2s_dma_init();
    debounce_init(debounced_pins, DEBOUNCE_INPUT_COUNT, DEBOUNCE_PERIOD_MS);

    mute_amplifiers();
    set_leds(LED_VD1 | LED_VD2 | LED_VD3 | LED_VD4 | LED_VD5, true);

    while (1)
    {
        pin_polling();
        debounce_process(uptime_ms);
        process_achtung_button();
        process_trk_button();
        
        led_controller_process(uptime_ms);
        ButtonStatus button_status = poll_button_events();

        process_button_combinations(&button_status, uptime_ms);
        blinking_process(uptime_ms);
        process_usb(state == IDLE, uptime_ms);
        process_state_logic();
    }
}

// Инициализация переиферии
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
        printf("SD init error: %d\n", status);
        return;
    }

    diskio_set_sd_card(&card_info);

    /* Счётчик измерения производительности. */
    perf_counter_init();
}


/* ============================================================================
 * Стейт машина
 * ========================================================================== */
// Поиск кнопки в комбинации
static int combo_has(uint8_t mask, const LedEvent events[BUTTON_COMBO_COUNT], uint8_t button, LedEvent event)
{
    return (mask & (1U << button)) != 0U && events[button] == event;
}

// Преобразование комбинации в событие
void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT])
{
    // Оповещение
    if (state == IDLE && combo_has(buttons_mask, events, 1, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
        swich_relay(GGC);
        switch_mux(SIG_PLT1);
        process_state_event(APP_EVENT_NOTIFICATION_START, FR_OK);
        return;
    }
    if(state == NOTIFICATION && combo_has(buttons_mask, events, 1, NO_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_NOTIFICATION_FINISH, FR_OK);
        return;
    }

    // Передача
    if (state == IDLE && combo_has(buttons_mask, events, 3, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
        swich_relay(APB);
        switch_mux(SIG_PLT1);
        process_state_event(APP_EVENT_BROADCAST_START, FR_OK);
        return;
    }
    if(state == BROADCAST && combo_has(buttons_mask, events, 3, NO_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_BROADCAST_FINISH, FR_OK);
        return;
    }

    // Прием
    if (state == IDLE && combo_has(buttons_mask, events, 5, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
        swich_relay(OPER);
        switch_mux(SIG_TRK);
        process_state_event(APP_EVENT_PRM_START, FR_OK);
        return;
    }
    if(state == PRM && combo_has(buttons_mask, events, 5, NO_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_PRM_FINISH, FR_OK);
        return;
    }

    // Запись фразы 1
    if (state == IDLE && combo_has(buttons_mask, events, 2, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
        switch_mux(SIG_PLT1);
        process_state_event(APP_EVENT_FR1_RECORD_START, FR_OK);
        return;
    }
    if(state == RECORDING_FR1 && combo_has(buttons_mask, events, 2, NO_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_FR1_RECORD_FINISH, FR_OK);
        return;
    }

    // Запись фразы 2
    if (state == IDLE && combo_has(buttons_mask, events, 4, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
        switch_mux(SIG_PLT1);
        process_state_event(APP_EVENT_FR2_RECORD_START, FR_OK);
        return;
    }
    if(state == RECORDING_FR2 && combo_has(buttons_mask, events, 4, NO_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_FR2_RECORD_FINISH, FR_OK);
        return;
    }

    // Тревога 
    if(state == IDLE && combo_has(buttons_mask, events, 0, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        if(combo_has(buttons_mask,events, 1, SHORT_EVENT))
        {
            swich_relay(GGC);
            process_state_event(APP_EVENT_ALARM_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            process_state_event(APP_EVENT_ALARM_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            process_state_event(APP_EVENT_ALARM_START, FR_OK);
            return;
        }
    }

    // Воспроизведение ФР1
    if(state == IDLE && combo_has(buttons_mask, events, 2, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        if(combo_has(buttons_mask,events, 1, SHORT_EVENT))
        {
            swich_relay(GGC);
            process_state_event(APP_EVENT_PLAYING_FR1_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            process_state_event(APP_EVENT_PLAYING_FR1_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            process_state_event(APP_EVENT_PLAYING_FR1_START, FR_OK);
            return;
        }
    }

    // Воспроизведение ФР2
    if(state == IDLE && combo_has(buttons_mask, events, 4, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        if(combo_has(buttons_mask,events, 1, SHORT_EVENT))
        {
            swich_relay(GGC);
            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
    }

    // Прерывание воспроизведения
    if( (state == PLAYING_FR1 || state == PLAYING_FR2 || state == ALARM)
        && combo_has(buttons_mask, events, 2, SHORT_EVENT) && combo_has(buttons_mask, events, 4, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        process_state_event(APP_EVENT_ALARM_FINISH, FR_OK);
        return;
    }

    // Регулировка громкости
    if(state == IDLE && combo_has(buttons_mask, events, 6, SHORT_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_common_event(APP_EVENT_VOLUME_UP);
        return;
    }
    if(state == IDLE && combo_has(buttons_mask, events, 7, SHORT_EVENT) && count_bits(buttons_mask) == 1)
    {
        process_common_event(APP_EVENT_VOLUME_DOWN);
        return;
    }
}

// Обработка переходов между состояниями
static void process_state_event(app_event_t event, FRESULT error)
{
    // Оповещение
    if(event == APP_EVENT_NOTIFICATION_START)
    {

        state = NOTIFICATION;
    }
    else if(event == APP_EVENT_NOTIFICATION_FINISH)
    {

        state = IDLE;
    }

    // Передача
    else if(event == APP_EVENT_BROADCAST_START)
    {

        state = BROADCAST;
    }
    else if(event == APP_EVENT_BROADCAST_FINISH)
    {

        state = IDLE;
    }

    // Прием
    else if(event == APP_EVENT_PRM_START)
    {

        state = PRM;
    }
    else if(event == APP_EVENT_PRM_FINISH)
    {

        state = IDLE;
    }

    // Речевое пожарное 
    else if(event == APP_EVENT_FIRE_START)
    {
        state = FIRE;
    }
    else if(event == APP_EVENT_FIRE_FINISH)
    {
        state = IDLE;
    }

    // Запись FR1
    else if(event == APP_EVENT_FR1_RECORD_START)
    {

        state = RECORDING_FR1;
    }
    else if(event == APP_EVENT_FR1_RECORD_FINISH)
    {

        state = IDLE;
    }

    // Запись FR2
    else if(event == APP_EVENT_FR2_RECORD_START)
    {

        state = RECORDING_FR2;
    }
    else if(event == APP_EVENT_FR2_RECORD_FINISH)
    {
        state = IDLE;
    }

    // Тревога
    else if(event == APP_EVENT_ALARM_START)
    {
        state = ALARM;
    }

    // воспроизведение фр1
    else if(event == APP_EVENT_PLAYING_FR1_START)
    {

        state = PLAYING_FR1;
    }
    else if(event == APP_EVENT_PLAYING_FR1_FINISH)
    {

        state = PLAYING_FR1;
    }

    // воспроизведение фр2
    else if(event == APP_EVENT_PLAYING_FR2_START)
    {

        state = PLAYING_FR2;
    }
        else if(event == APP_EVENT_PLAYING_FR2_FINISH)
    {

        state = PLAYING_FR2;
    }

    // прерывание alarm , fr1, fr2
    else if(event == APP_EVENT_ALARM_FINISH)
    {
        state = IDLE;
    }

}

// Переодическое обслуживание текущего состояния
static void process_state_logic(void)
{
    switch (state) {
        case IDLE:
            break;

        case NOTIFICATION:
            break;

        case BROADCAST:
            break;

        case PRM:
            break;

        case FIRE:
            // TODO
            break;

        case RECORDING_FR1:
            // TODO:
            break;

        case RECORDING_FR2:
            // TODO
            break;

        case ALARM:
            // TODO
            break;

        case PLAYING_FR1:
            // TODO
            break;

        case PLAYING_FR2:
            // TODO
            break;
    }
}

// Изменение громкости
static int process_common_event(app_event_t event)
{
    switch (event) 
    {
    case APP_EVENT_VOLUME_UP:
        aic3104_volume_up();

        if (!is_min_volume()) {
            request_blinking(VD8, 0, 0);
            set_leds(LED_VD8, false);
        }

        request_blinking(VD7, 0, 0);
        set_leds(LED_VD7, is_max_volume() != 0U);

        if (!is_max_volume()) {
            request_blinking(VD7, 2, 200);
        }

        printf("+\n");
        return 1;

    case APP_EVENT_VOLUME_DOWN:
        aic3104_volume_down();

        if (!is_max_volume()) {
            request_blinking(VD7, 0, 0);
            set_leds(LED_VD7, false);
        }

        request_blinking(VD8, 0, 0);
        set_leds(LED_VD8, is_min_volume() != 0U);

        if (!is_min_volume()) {
            request_blinking(VD8, 2, 200);
        }

        printf("-\n");
        return 1;

    default:
        return 0;
    }
}

// Сухой контакт ППК
static void process_achtung_button(void)
{
    static uint8_t previous_state = 1;

    uint8_t current_state = debounced_pins[DEBOUNCE_INPUT_ACHTUNG].state;

    /* Событие формируется только при изменении обработанного уровня. */
    if (current_state == previous_state) {
        return;
    }

    previous_state = current_state;

    if (current_state == 0U) {
        process_state_event(APP_EVENT_FIRE_START, FR_OK);
    } else {
        process_state_event(APP_EVENT_FIRE_FINISH, FR_OK);
    }
}

// Сигнал TRK
static void process_trk_button(void)
{
    static uint8_t previous_state = 1U;
    uint8_t current_state = debounced_pins[DEBOUNCE_INPUT_TRK].state;

    if (current_state == previous_state) {
        return;
    }

    previous_state = current_state;

    if (current_state == 0U) {
        process_state_event(APP_EVENT_PRM_START, FR_OK);
    } else {
        process_state_event(APP_EVENT_PRM_FINISH, FR_OK);
    }
}


/* ============================================================================
 * Обработчик системного таймера
 * ========================================================================== */

void SysTick_Handler(void)
{
    uptime_ms++;
}