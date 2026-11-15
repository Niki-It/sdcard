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

// нужно для announce
static audio_file_t next_file;
// файл который проигрывается 
static const char *playing_filename = NULL;

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
// Преобразование комбинации в событие
void flush_combo(uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT])
{
    // Регулировка громкости
    if(combo_has(buttons_mask, events, 6, SHORT_EVENT))
    {
        process_common_event(APP_EVENT_VOLUME_UP);
    }
    if(combo_has(buttons_mask, events, 7, SHORT_EVENT))
    {
        process_common_event(APP_EVENT_VOLUME_DOWN);
    }
    // Оповещение
    if (state == IDLE && combo_has(buttons_mask, events, 1, LONG_EVENT) && count_bits(buttons_mask) == 1) 
    {
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
            set_all_leds(LED_VD1 | LED_VD2 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_ALARM_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            set_all_leds(LED_VD1 | LED_VD4 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_ALARM_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            set_all_leds(LED_VD1 | LED_VD6 | get_leds(LED_VD7 | LED_VD8));

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
            set_all_leds(LED_VD3 | LED_VD2 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_PLAYING_FR1_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            set_all_leds(LED_VD3 | LED_VD4 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_PLAYING_FR1_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            set_all_leds(LED_VD3 | LED_VD6 | get_leds(LED_VD7 | LED_VD8));

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
            set_all_leds(LED_VD5 | LED_VD2 | get_leds(LED_VD7 | LED_VD8));
            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 3, SHORT_EVENT))
        {
            swich_relay(APB);
            set_all_leds(LED_VD5 | LED_VD4 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
        if(combo_has(buttons_mask,events, 5, SHORT_EVENT))
        {
            swich_relay(OPER);
            set_all_leds(LED_VD5 | LED_VD6 | get_leds(LED_VD7 | LED_VD8));

            process_state_event(APP_EVENT_PLAYING_FR2_START, FR_OK);
            return;
        }
    }

    // Прерывание воспроизведения
    if( (state == PLAYING_FR1 || state == PLAYING_FR2 || state == ALARM)
        && combo_has(buttons_mask, events, 2, SHORT_EVENT) && combo_has(buttons_mask, events, 4, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        process_state_event(APP_EVENT_ALARM_FR_FINISH, FR_OK);
        return;
    }

    
}

// Обработка переходов между состояниями
static void process_state_event(app_event_t event, FRESULT error)
{
    // Оповещение
    if(event == APP_EVENT_NOTIFICATION_START)
    {
        swich_relay(GGC);
        switch_active_remote_mux();
        start_bypass();
        set_all_leds(LED_VD2 | LED_VD9 | get_leds(LED_VD7 | LED_VD8));
        printf("NOTIFICATION START\n");
        state = NOTIFICATION;
    }
    else if(event == APP_EVENT_NOTIFICATION_FINISH)
    {
        stop_bypass();
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        printf("NOTIFICATION FINISH\n");
        state = IDLE;
    }

    // Передача
    else if(event == APP_EVENT_BROADCAST_START)
    {
        swich_relay(APB);
        switch_active_remote_mux();
        start_bypass();
        set_all_leds(LED_VD4 | LED_VD9 | get_leds(LED_VD7 | LED_VD8));
        printf("BROADCAST START\n");
        state = BROADCAST;
    }
    else if(event == APP_EVENT_BROADCAST_FINISH)
    {
        stop_bypass();
        printf("BROADCAST FINISH\n");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Прием
    else if(event == APP_EVENT_PRM_START)
    {
        switch_mux(SIG_TRK);
        swich_relay(OPER);
        start_bypass();
        set_all_leds(LED_VD6 | LED_VD9 | get_leds(LED_VD7 | LED_VD8));
        printf("PRIEM START\n");
        state = PRM;
    }
    else if(event == APP_EVENT_PRM_FINISH)
    {
        switch_active_remote_mux();
        swich_relay(GGC);
        stop_bypass();
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        printf("PRIEM FINISH\n");
        state = IDLE;
    }

    // Запись FR1
    else if(event == APP_EVENT_FR1_RECORD_START)
    {
        if(!usb_stop(uptime_ms))
        {
            return;
        }
        switch_active_remote_mux();
        FRESULT result = start_recording("frase1.wav");

        if (result == FR_OK) {
            printf("FR1 RECORDING START\n");
            state = RECORDING_FR1;
            set_all_leds(LED_VD3 | get_leds(LED_VD7 | LED_VD8));
        }
        else {
            printf("FR1 RECORDING ERROR: %d\n", (int)result);
            state = IDLE;
        }
    }
    else if(event == APP_EVENT_FR1_RECORD_FINISH)
    {
        stop_recording(error);

        printf("FR1 RECORDING FINISH\n");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Запись FR2
    else if (event == APP_EVENT_FR2_RECORD_START)
    {
        if(!usb_stop(uptime_ms))
        {
            return;
        }
        switch_active_remote_mux();

        FRESULT result = start_recording("frase2.wav");

        if (result == FR_OK) {
            printf("FR2 RECORDING START\n");
            state = RECORDING_FR2;
            set_all_leds(LED_VD5 | get_leds(LED_VD7 | LED_VD8));
        }
        else {
            printf("FR2 RECORDING ERROR: %d\n", (int)result);
            state = IDLE;
        }
    }
    else if (event == APP_EVENT_FR2_RECORD_FINISH)
    {
        stop_recording(error);

        printf("FR2 RECORDING FINISH\n");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));

        state = IDLE;
    }
    else if (event == APP_EVENT_FR1_RECORD_ERROR)
    {
        printf("FR1 RECORDING ERROR: %d\n", (int)error);

        stop_recording(error);

        f_unlink("frase1.wav");

        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Ошибка записи FR2
    else if (event == APP_EVENT_FR2_RECORD_ERROR)
    {
        printf("FR2 RECORDING ERROR: %d\n", (int)error);

        stop_recording(error);

        f_unlink("frase2.wav");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Воспроизведение FR1
    else if (event == APP_EVENT_PLAYING_FR1_START)
    {
        next_file.delay = 0;
        if (!usb_stop(uptime_ms))
            return;

        reset_announce();
        state = PLAYING_FR1;
        printf("FR1 PLAYING START\n");
        play_next_announce();
    }
    else if (event == APP_EVENT_PLAYING_FR1_FINISH)
    {
        finish_playback(FR_OK);

        reset_announce();
        printf("FR1 PLAYING FINISH\n");
        state = IDLE;
    }

    // Воспроизведение FR2

    else if (event == APP_EVENT_PLAYING_FR2_START)
    {
        next_file.delay = 0;
        if (!usb_stop(uptime_ms))
            return;

        reset_announce();
        state = PLAYING_FR2;
        printf("FR2 PLAYING START\n");
        play_next_announce();
    }
    else if (event == APP_EVENT_PLAYING_FR2_FINISH)
    {
        finish_playback(FR_OK);
        reset_announce();
        printf("FR2 PLAYING FINISH\n");
        state = IDLE;
    }

    // Общая ошибка воспроизведения
    // FIRE, ALARM, FR1, FR2
    else if (event == APP_EVENT_PLAYBACK_ERROR)
    {
        printf("PLAYBACK ERROR: %s, code: %d\n",
            playing_filename ? playing_filename : "unknown",
            (int)error);

        finish_playback(error);

        playing_filename = NULL;
        reset_announce();
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Речевое пожарное
    else if (event == APP_EVENT_FIRE_START)
    {
        if(!usb_stop(uptime_ms))
        {
            return;
        }
        swich_relay(GGC);
        FRESULT result = start_playback("fire.mp3");

        if (result == FR_OK) {
            printf("FIRE START\n");
            set_all_leds(LED_VD1 | LED_VD2 | LED_VD3 | LED_VD4 | LED_VD5 | LED_VD6  | get_leds(LED_VD7 | LED_VD8));

            state = FIRE;
        } else {
            process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
        }
    }
    else if (event == APP_EVENT_FIRE_FINISH)
    {
        if (audio_playback_is_active()) {
            app_playback_abort();
        } else {
            finish_playback(FR_OK);
        }

        printf("FIRE FINISH\n");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
    }

    // Тревога
    else if (event == APP_EVENT_ALARM_START)
    {
        if(!usb_stop(uptime_ms))
        {
            return;
        }
        FRESULT result = start_playback("alarm.mp3");

        if (result == FR_OK) {
            printf("ALARM START\n");

            state = ALARM;
        } else {
            process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
        }
    }
    else if (event == APP_EVENT_ALARM_FINISH)
    {
        finish_playback(FR_OK);

        printf("ALARM FINISH\n");
        set_all_leds(get_leds(LED_VD7 | LED_VD8));

        state = IDLE;
    }

    // Принудительная остановка ALARM, FR1, FR2
    else if (event == APP_EVENT_ALARM_FR_FINISH)
    {
        app_playback_abort();

        printf("ALARM AND FR STOPPED\n");
        reset_announce();
        set_all_leds(get_leds(LED_VD7 | LED_VD8));
        state = IDLE;
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

        case RECORDING_FR1:
            FRESULT result = wav_recording_task();

            if (result != FR_OK) {
                process_state_event(APP_EVENT_FR1_RECORD_ERROR,result);
            }
            break;

        case RECORDING_FR2:
            FRESULT result2 = wav_recording_task();

            if (result2 != FR_OK) {
                process_state_event(APP_EVENT_FR2_RECORD_ERROR,result2);
            }
            break;
            break;

        case FIRE:
        {
            FRESULT result = audio_playback_task();

            if (result != FR_OK) {
                process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
            }
            else if (!audio_playback_is_active()) {
                playback_file_finished();
                process_state_event(APP_EVENT_FIRE_START, FR_OK);
            }

            break;
        }
        case ALARM:
        {
            FRESULT result = audio_playback_task();

            if (result != FR_OK) {
                process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
            }
            else if (!audio_playback_is_active()) {
                playback_file_finished();
                process_state_event(APP_EVENT_ALARM_START, FR_OK);
            }

            break;
        }
        case PLAYING_FR1:
        case PLAYING_FR2:
        {
            if (announce_delay_active())
            {
                if (check_announce_delay(uptime_ms))
                {
                    FRESULT result = start_playback(next_file.pathfile);

                    if (result != FR_OK) {
                        process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
                    }
                }

                break;
            }

            FRESULT result = audio_playback_task();

            if (result != FR_OK) {
                process_state_event(APP_EVENT_PLAYBACK_ERROR, result);
            }
            else if (!audio_playback_is_active()) 
            {
                playback_file_finished();
                play_next_announce();
            }

            break;
        }   
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
        if (state == IDLE) {
            process_state_event(APP_EVENT_FIRE_START, FR_OK);
        }
    } else {
        if (state == FIRE) {
            process_state_event(APP_EVENT_FIRE_FINISH, FR_OK);
        }
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
        if (state == IDLE) {
            process_state_event(APP_EVENT_PRM_START, FR_OK);
        }
    } else {
        if (state == PRM) {
            process_state_event(APP_EVENT_PRM_FINISH, FR_OK);
        }
    }
}


/* ============================================================================
 * Обработчик системного таймера
 * ========================================================================== */

void SysTick_Handler(void)
{
    uptime_ms++;
}
// ------------------------ ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ СТЕЙТ МАШИНЫ --------------------
static void start_bypass(void)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_line1lp_hprout_bypass();

    unmute_amplifiers();
}
static void stop_bypass(void)
{
    mute_amplifiers();
}

static FRESULT start_recording(const char* path)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_line1lp_i2s();

    FRESULT result = wav_recording_start(path);

    if (result == FR_OK) {
        printf("WAV recording started\n");
    } else {
        printf("WAV recording start error: %d\n", (int)result);
    }

    return result;
}

static void stop_recording(FRESULT error)
{
    FRESULT result = wav_recording_end();

    mute_amplifiers();

    if(error == FR_OK)
    {
        printf( "WAV recording finished: %d\n", (int)result);
        return;
    }
    printf
    (
        "WAV write error: %d, end: %d\n",
        (int)error, (int)result
    );
}


/* ============================================================================
 * Действия режима воспроизведения
 * ========================================================================== */

static FRESULT start_playback(const char *path)
{
    mute_amplifiers();

    aic3104_soft_reset();
    aic3104_init_clocking();
    aic3104_i2s_hprout();

    playing_filename = path;

    FRESULT result = audio_playback_start(path);

    if (result == FR_OK) {
        unmute_amplifiers();
        printf("PLAYBACK START: %s\n", path);
    }

    return result;
}
static void playback_file_finished(void)
{
    if (playing_filename != NULL) {
        printf("PLAYBACK FINISHED: %s\n", playing_filename);
        playing_filename = NULL;
    }
}

/*
 * При штатном завершении и ошибке воспроизведение уже остановлено
 * внутри audio_playback_task().
 */
static void finish_playback(FRESULT error)
{
    (void)error;
    mute_amplifiers();
}
static void app_playback_abort(void)
{
    mute_amplifiers();

    if (playing_filename != NULL) {
        printf("PLAYBACK INTERRUPTED: %s\n", playing_filename);
    }

    FRESULT result = audio_playback_stop();

    if (result != FR_OK) {
        printf("PLAYBACK STOP ERROR: %d\n", (int)result);
    }

    playing_filename = NULL;
}
static void play_next_announce(void)
{
    bool fr1 = (state == PLAYING_FR1);

    uint16_t delay = next_file.delay;

    next_file = next_announce(
        fr1 ? "announce1" : "announce2",
        fr1 ? "frase1.wav" : "frase2.wav"
    );

    if (!next_file.result)
    {
        process_state_event(
            fr1 ? APP_EVENT_PLAYING_FR1_FINISH
                : APP_EVENT_PLAYING_FR2_FINISH,
            FR_OK
        );
        return;
    }

    start_announce_delay(delay, uptime_ms);
}
static void switch_active_remote_mux(void)
{
    if (led_controller_active_usart() == USART2)
    {
        switch_mux(SIG_PLT2);
    }
    else
    {
        switch_mux(SIG_PLT1);
    }
}