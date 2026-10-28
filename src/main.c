#include "main.h"
#include "string.h"
/* ============================================================================
 * Константы 
 * ========================================================================== */

#define AMP_PORT        GPIOB
#define APB_PIN         LL_GPIO_PIN_6
#define OPR_PIN         LL_GPIO_PIN_7
#define MUTE_AMP1_PIN   LL_GPIO_PIN_8
#define MUTE_AMP2_PIN   LL_GPIO_PIN_9

#define DEBOUNCE_PERIOD_MS          1U  // Периодичность вызова обработчика антидребезга
#define ACHTUNG_DEBOUNCE_THRESHOLD  5U  // Количество вызовов обработчика с неизменным состоянием сигнала до его подтверждения
#define TRK_DEBOUNCE_THRESHOLD      15U

#define BUTTON_COMBO_TIMEOUT_MS 150U

/* ============================================================================
 * Типы данных
 * ========================================================================== */
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

#define PLAYBACK_WAV 0
#define PLAYBACK_MP3 1
static uint32_t format = 0;

sd_card_info_t card_info;

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

#define USB_FS_DEVICE \
    ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))

// Отсчитывает миллисекунды - не удалять
volatile uint32_t uptime_ms = 0;
static app_state_t state = IDLE;
/* ============================================================================
 * Точка входа и основной цикл
 * ========================================================================== */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    button_combo_init(BUTTON_COMBO_TIMEOUT_MS);
    periph_init();
    tusb_init();
    fs_unit(true);


    mux_select(2, MUX_CHANNEL_X1);
    aic3104_init();
    i2s_dma_init();

    debounce_init(debounced_pins, DEBOUNCE_INPUT_COUNT, DEBOUNCE_PERIOD_MS);

    mute_amplifiers();

    set_leds(LED_VD1 | LED_VD2 | LED_VD3 | LED_VD4 | LED_VD5, true);


    while (1)
    {
        pin_polling();
        debounce_process(uptime_ms);
        
        led_controller_process(uptime_ms);
        ButtonStatus button_status = poll_button_events();

        process_button_combinations(&button_status, uptime_ms);
        blinking_process(uptime_ms);
        process_usb(state == IDLE, uptime_ms);
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
    // Оповещение
    if (state == IDLE && combo_has(buttons_mask, events, 1, LONG_EVENT) 
            && count_bits(buttons_mask) == 1) 
    {
        swich_relay(GGC);
        process_state_event(APP_EVENT_NOTIFICATION_START, FR_OK);
        return;
    }
    if(state == NOTIFICATION && combo_has(buttons_mask, events, 1, NO_EVENT)
        && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_NOTIFICATION_FINISH, FR_OK);
        return;
    }

    // Передача
    if (state == BROADCAST && combo_has(buttons_mask, events, 3, LONG_EVENT)
            && count_bits(buttons_mask) == 1) 
    {
        swich_relay(APB);
        process_state_event(APP_EVENT_BROADCAST_START, FR_OK);
        return;
    }
    if(state == BROADCAST && combo_has(buttons_mask, events, 3, NO_EVENT)
        && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_BROADCAST_FINISH, FR_OK);
        return;
    }

    // Прием
    if (state == PRM && combo_has(buttons_mask, events, 5, LONG_EVENT)
        && count_bits(buttons_mask) == 1) 
    {
        swich_relay(OPER);
        process_state_event(APP_EVENT_BROADCAST_START, FR_OK);
        return;
    }
    if(state == PRM && combo_has(buttons_mask, events, 5, NO_EVENT)
        && count_bits(buttons_mask) == 1)
    {
        process_state_event(APP_EVENT_BROADCAST_FINISH, FR_OK);
        return;
    }

    // Тревога 
    if(state == IDLE && combo_has(buttons_mask, events, 0, SHORT_EVENT)
        && count_bits(buttons_mask) == 2)
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
    // Воспроизведение
    if(state == IDLE && combo_has(buttons_mask, events, 2, SHORT_EVENT)
        && count_bits(buttons_mask) == 2)
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

    if(state == IDLE && combo_has(buttons_mask, events, 4, SHORT_EVENT)
        && count_bits(buttons_mask) == 2)
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

    if( (state == PLAYING_FR1 || state == PLAYING_FR2 || state == ALARM)
        && combo_has(buttons_mask, events, 2, SHORT_EVENT) && combo_has(buttons_mask, events, 4, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        process_state_event(APP_EVENT_ALARM_PLAYING_FINISH, FR_OK);
        return;
    }

    if(state == IDLE && combo_has(buttons_mask, events, 6, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        process_common_event(APP_EVENT_VOLUME_UP);
        return;
    }
    if(state == IDLE && combo_has(buttons_mask, events, 7, SHORT_EVENT) && count_bits(buttons_mask) == 2)
    {
        process_common_event(APP_EVENT_VOLUME_DOWN);
        return;
    }
}


/* ============================================================================
 * Обработка событий и переходов между состояниями
 * ========================================================================== */

static void process_state_event(app_event_t event, FRESULT error)
{
    // Оповещение
    if(event == APP_EVENT_NOTIFICATION_START)
    {
        //start_bypass();
        state = NOTIFICATION;
    }
    else if(event == APP_EVENT_NOTIFICATION_FINISH)
    {
        //stop_bypass();
        state = IDLE;
    }

    // Передача
    else if(event == APP_EVENT_BROADCAST_START)
    {
        //start_bypass();
        state = BROADCAST;
    }
    else if(event == APP_EVENT_BROADCAST_FINISH)
    {
        //stop_bypass();
        state = IDLE;
    }

    // Прием
    else if(event == APP_EVENT_PRM_START)
    {
        //start_bypass();
        state = PRM;
    }
    else if(event == APP_EVENT_PRM_FINISH)
    {
        //stop_bypass();
        state = IDLE;
    }

    // Речевое пожарное 
    else if(event == APP_EVENT_FIRE_START)
    {
        // TODO:
        state = FIRE;
    }
    else if(event == APP_EVENT_FIRE_FINISH)
    {
        // TODO:
        state = FIRE;
    }

    // Запись FR1
    else if(event == APP_EVENT_FR1_RECORD_START)
    {
        // TODO:
        state = RECORDING_FR1;
    }
    else if(event == APP_EVENT_FR1_RECORD_FINISH)
    {
        // TODO:
        state = IDLE;
    }
    // Запись FR2
    else if(event == APP_EVENT_FR2_RECORD_START)
    {
        // TODO:
        state = RECORDING_FR2;
    }
    else if(event == APP_EVENT_FR2_RECORD_FINISH)
    {
        // TODO:
        state = IDLE;
    }

    // Тревога
    else if(event == APP_EVENT_ALARM_START)
    {
        // TODO:
        state = ALARM;
    }

    // воспроизведение фр1, фр2
    else if(event == APP_EVENT_PLAYING_FR1_START)
    {
        //TODO:
        state = PLAYING_FR1;
    }
    else if(event == APP_EVENT_PLAYING_FR2_START)
    {
        // TODO:
        state = PLAYING_FR2;
    }
    // прерывание alarm , fr1, fr2
    else if(event == APP_EVENT_ALARM_PLAYING_FINISH)
    {
        state = IDLE;
    }

}


/* ============================================================================
 * Общие команды без изменения состояния
 * ========================================================================== */

/* Возвращает 1, если событие обработано, иначе 0. */
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


static void swich_relay(RelayMode mode)
{
    switch (mode)
    {
        case GGC:
            LL_GPIO_ResetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
        case APB:
            LL_GPIO_SetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
        case OPER:
            LL_GPIO_SetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_SetOutputPin(AMP_PORT, OPR_PIN);
            break;
        default:
            LL_GPIO_ResetOutputPin(AMP_PORT, APB_PIN);
            LL_GPIO_ResetOutputPin(AMP_PORT, OPR_PIN);
            break;
    
    }
}

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