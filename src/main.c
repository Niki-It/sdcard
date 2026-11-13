#include "main.h"
#include "string.h"

#define DEBOUNCE_PERIOD_MS          1U
#define ACHTUNG_DEBOUNCE_THRESHOLD  5U
#define TRK_DEBOUNCE_THRESHOLD      15U
#define BUTTON_COMBO_TIMEOUT_MS     150U
#define ACHTUNG_INITIAL_LEVEL       true
#define TRK_INITIAL_LEVEL           true
#define INITIAL_ACTIVE_MODES        0U

/* Один условный цикл Python заменён одной секундой реального времени. */
#define RECEIVE_TIMEOUT_MS          3000U
#define AUTO_ANNOUNCE_1_PERIOD_MS    5000U
#define AUTO_ANNOUNCE_2_PERIOD_MS    7000U
#define AUTO_ANNOUNCE_3_PERIOD_MS    9000U
#define ANNOUNCE_DURATION_MS        3000U
#define PHRASE_DURATION_MS          3000U
#define UNLOCK_RECORD_TIMEOUT_MS    5000U

#define CHANNEL_1                   (1U << 0)
#define CHANNEL_2                   (1U << 1)
#define CHANNEL_PRM                 (1U << 2)
#define ANNOUNCE_CHANNELS           (CHANNEL_1 | CHANNEL_2)

#ifndef ANNOUNCE_REQUIRE_ALL_CHANNELS
#define ANNOUNCE_REQUIRE_ALL_CHANNELS true
#endif

#define MODE_MASK(mode)             (UINT32_C(1) << (mode))
#define NO_BUTTON                   (-1)
#define NO_OUTPUT                   (-1)
#define VOLUME_INITIAL              50U
#define VOLUME_MIN                  0
#define VOLUME_MAX                  100
#define VOLUME_STEP                 5

/* В C кнопка B_GGS из Python называется B_GGS1.
 * Привязка APB взята из Python: B_APB -> APB1, B_PRD -> APB2. */
typedef enum
{
    ALERT = 0, ACHTUNG, RECEIVE, GGS1, GGS2, APB1, APB2,
    ANNOUNCE, PHRASE, RECORD, 
    MODE_COUNT
} app_mode_t;

typedef enum
{
    VOLUME_RECEIVE = 0, VOLUME_GGS1, VOLUME_GGS2, VOLUME_APB1,
    VOLUME_APB2, VOLUME_ANNOUNCE, VOLUME_PHRASE_1, VOLUME_PHRASE_2,
    VOLUME_COUNT
} volume_target_t;

typedef struct
{
    source_t source;
    bool is_holding;
} mode_params_t;

typedef struct
{
    // состояние машины
    uint32_t active_modes;
    mode_params_t modes[MODE_COUNT];
    // кнопки trk и achtung
    bool last_achtung;
    bool last_trk;
    // режим receive
    bool auto_receive;
    uint32_t receive_timeout_started_ms;
    // режим announce
    bool auto_announce_1;
    bool auto_announce_2;
    uint32_t auto_announce_1_started_ms;
    uint32_t auto_announce_2_started_ms;
    uint32_t auto_announce_3_started_ms;
    uint8_t announce_channels;
    uint32_t announce_started_ms;
    int announce_button;
    // режим фразы
    uint8_t phrase_number;
    int phrase_output;
    uint8_t phrase_channel;
    uint32_t phrase_started_ms;
    // режим записи
    bool unlock_record;
    uint32_t unlock_record_started_ms;
    int record_button;
    bool record_invalid;
    // громкость
    uint8_t volume[VOLUME_COUNT];
} app_runtime_t;

static const char *const mode_names[MODE_COUNT] = {
    "ALERT", "ACHTUNG", "RECEIVE", "GGS1", "GGS2", "APB1", "APB2",
    "ANNOUNCE", "PHRASE", "RECORD"
};
static const uint32_t mode_compatibility[MODE_COUNT] = {
    [ALERT] = 0U,
    [ACHTUNG] = 0U,
    [RECEIVE] = MODE_MASK(ANNOUNCE) | MODE_MASK(PHRASE),
    [GGS1] = MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE) | MODE_MASK(PHRASE),
    [GGS2] = MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE) | MODE_MASK(PHRASE),
    [APB1] = MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE) | MODE_MASK(PHRASE),
    [APB2] = MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE) | MODE_MASK(PHRASE),
    [ANNOUNCE] = MODE_MASK(RECEIVE) | MODE_MASK(GGS1) | MODE_MASK(GGS2) |
                 MODE_MASK(APB1) | MODE_MASK(APB2) | MODE_MASK(PHRASE),
    [PHRASE] = MODE_MASK(RECEIVE) | MODE_MASK(GGS1) | MODE_MASK(GGS2) |
               MODE_MASK(APB1) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE),
    [RECORD] = 0U
};

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

static app_runtime_t app;
sd_card_info_t card_info;

static void process_state_event(app_event_t event, source_t source, int button);
static void process_state_logic(void);
static void process_achtung_button(void);
static void process_trk_button(void);

static void app_runtime_init(void)
{
    memset(&app, 0, sizeof(app));
    app.active_modes = INITIAL_ACTIVE_MODES;
    app.last_achtung = ACHTUNG_INITIAL_LEVEL;
    app.last_trk = TRK_INITIAL_LEVEL;
    app.announce_button = NO_BUTTON;
    app.record_button = NO_BUTTON;
    app.phrase_output = NO_OUTPUT;
    for (unsigned i = 0; i < MODE_COUNT; ++i)
        app.modes[i].source = OUTSOURCE;
    for (unsigned i = 0; i < VOLUME_COUNT; ++i)
        app.volume[i] = VOLUME_INITIAL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    app_runtime_init();
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

        LedController_Update(uptime_ms);

        ButtonStatus button_status = LedController_GetButtons();
        process_button_combinations(&button_status, uptime_ms);

        process_state_logic();
        tud_task();
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

    LedController_Init();
    i2c_init();

    /* Карта памяти и её подключение к дисковому интерфейсу. */
    sd_status_t status = sd_init(&card_info);

    if (status != SD_OK)
    {
        printf("SD init error: %d\n", status);
        return;
    }

    diskio_set_sd_card(&card_info);

    /* Счётчик измерения производительности. */
    perf_counter_init();
}


/* Проверка интервала безопасна при переполнении uint32_t uptime_ms.
 * Обслуживание должно выполняться чаще полного оборота таймера (~49.7 суток). */
static bool interval_expired(uint32_t now, uint32_t started, uint32_t duration)
{
    return (uint32_t)(now - started) >= duration;
}

static bool remote_source(source_t source)
{
    return source == REMOTE_1 || source == REMOTE_2;
}

static bool mode_is_active(app_mode_t mode)
{
    return (app.active_modes & MODE_MASK(mode)) != 0U;
}

static bool modes_are_compatible(app_mode_t first, app_mode_t second)
{
    return (mode_compatibility[first] & MODE_MASK(second)) != 0U &&
           (mode_compatibility[second] & MODE_MASK(first)) != 0U;
}

/* Только действия входа/выхода. Условия допуска находятся в диспетчере.
 * Аппаратные действия и аудио пока остаются моками, как в Python. */
static void activate_mode(app_mode_t mode, source_t source, bool is_holding)
{
    app.modes[mode].source = source;
    app.modes[mode].is_holding = is_holding;
    app.active_modes |= MODE_MASK(mode);
    printf("START %s: source=%u holding=%u\r\n", mode_names[mode],
           (unsigned)source, (unsigned)is_holding);
}

static void deactivate_mode(app_mode_t mode)
{
    app.active_modes &= ~MODE_MASK(mode);
    app.modes[mode].source = OUTSOURCE;
    app.modes[mode].is_holding = false;
    printf("END %s\r\n", mode_names[mode]);
}

static void start_alert(source_t source) { activate_mode(ALERT, source, false); }
static void end_alert(void) { deactivate_mode(ALERT); }
static void start_achtung(void) { activate_mode(ACHTUNG, OUTSOURCE, false); }
static void end_achtung(void) { deactivate_mode(ACHTUNG); }
static void start_auto_receive(void)
{
    app.auto_receive = true;
    printf("AUTO_RECEIVE: on\r\n");
}
static void end_auto_receive(void)
{
    app.auto_receive = false;
    printf("AUTO_RECEIVE: off\r\n");
}
static void start_receive(source_t source, bool is_holding)
{
    app.receive_timeout_started_ms = uptime_ms;
    activate_mode(RECEIVE, source, is_holding);
}
static void end_receive(void)
{
    deactivate_mode(RECEIVE);
    app.receive_timeout_started_ms = 0U;
}
static void start_transmission(app_mode_t mode, source_t source)
{
    /* TODO: включить соответствующий выход и микрофон источника. */
    activate_mode(mode, source, true);
}
static void end_transmission(app_mode_t mode)
{
    /* TODO: выключить соответствующий выход. */
    deactivate_mode(mode);
}
static void set_auto_announce_1(bool enabled)
{
    app.auto_announce_1 = enabled;
    printf("AUTO_ANNOUNCE_1: %u\r\n", (unsigned)enabled);
}
static void set_auto_announce_2(bool enabled)
{
    app.auto_announce_2 = enabled;
    printf("AUTO_ANNOUNCE_2: %u\r\n", (unsigned)enabled);
}
static void reset_auto_announce_timers(void)
{
    uint32_t now = uptime_ms;
    app.auto_announce_1_started_ms = now;
    app.auto_announce_2_started_ms = now;
    app.auto_announce_3_started_ms = now;
}
static void start_announce(uint8_t channels, source_t source, int button)
{
    app.announce_button = button;
    app.announce_channels = channels;
    app.announce_started_ms = uptime_ms;
    activate_mode(ANNOUNCE, source, source != OUTSOURCE);
}
static void end_announce(void)
{
    deactivate_mode(ANNOUNCE);
    app.announce_channels = 0U;
    app.announce_started_ms = 0U;
    app.announce_button = NO_BUTTON;
}
static void set_announce_channels(uint8_t channels)
{
    /* Оставшиеся каналы продолжают воспроизведение без перезапуска. */
    app.announce_channels = channels;
    printf("ANNOUNCE: channels=%u\r\n", (unsigned)channels);
}
static void start_phrase(source_t source, uint8_t number, app_mode_t output, uint8_t channel)
{
    app.phrase_number = number;
    app.phrase_output = (int)output;
    app.phrase_channel = channel;
    app.phrase_started_ms = uptime_ms;
    activate_mode(PHRASE, source, false);
}
static void end_phrase(void)
{
    deactivate_mode(PHRASE);
    app.phrase_number = 0U;
    app.phrase_output = NO_OUTPUT;
    app.phrase_channel = 0U;
    app.phrase_started_ms = 0U;
}
static void enable_record_unlock(void)
{
    app.unlock_record = true;
    app.unlock_record_started_ms = uptime_ms;
    printf("UNLOCK_RECORD: on\r\n");
}
static void clear_record_unlock(void)
{
    app.unlock_record = false;
    app.unlock_record_started_ms = 0U;
    printf("UNLOCK_RECORD: expired\r\n");
}
static void start_record(source_t source, int button)
{
    app.record_button = button;
    app.record_invalid = false;
    app.unlock_record = false;
    app.unlock_record_started_ms = 0U;
    activate_mode(RECORD, source, true);
}
static void invalidate_record(void)
{
    app.record_invalid = true;
    printf("RECORD: invalid\r\n");
}
static void end_record(bool invalid)
{
    /* TODO: завершить запись; сохранить только при !invalid. */
    printf("RECORD: button=%d invalid=%u\r\n", app.record_button, (unsigned)invalid);
    deactivate_mode(RECORD);
    app.record_button = NO_BUTTON;
    app.record_invalid = false;
}
static void set_volume(volume_target_t target, uint8_t value)
{
    app.volume[target] = value;
    printf("VOLUME: target=%u value=%u\r\n", (unsigned)target, (unsigned)value);
}

/* Периодические обработчики не содержат блокирующих ожиданий. */
static handler_responce_t process_alert(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_achtung(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_receive(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_manual_announce(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_record(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_auto_receive(void)
{
    uint32_t now = uptime_ms;
    if (!app.last_trk)
    {
        app.receive_timeout_started_ms = now;
        return HANDLER_PROCESSING;
    }
    return interval_expired(now, app.receive_timeout_started_ms, RECEIVE_TIMEOUT_MS)
           ? HANDLER_FINISHED : HANDLER_PROCESSING;
}
static handler_responce_t process_announce(void)
{
    return interval_expired(uptime_ms, app.announce_started_ms, ANNOUNCE_DURATION_MS)
           ? HANDLER_FINISHED : HANDLER_PROCESSING;
}
static handler_responce_t process_phrase(void)
{
    return interval_expired(uptime_ms, app.phrase_started_ms, PHRASE_DURATION_MS)
           ? HANDLER_FINISHED : HANDLER_PROCESSING;
}
static handler_responce_t process_record_unlock(void)
{
    return interval_expired(uptime_ms, app.unlock_record_started_ms, UNLOCK_RECORD_TIMEOUT_MS)
           ? HANDLER_FINISHED : HANDLER_PROCESSING;
}
static handler_responce_t process_auto_announce_timer(uint32_t *started_ms, uint32_t period_ms)
{
    uint32_t now = uptime_ms;
    if (interval_expired(now, *started_ms, period_ms))
    {
        /* Один запрос за обслуживание, без серии запусков после задержки цикла. */
        *started_ms = now;
        return HANDLER_PROCESSING;
    }
    return HANDLER_FINISHED;
}
static handler_responce_t process_auto_announce_1(void)
{
    return process_auto_announce_timer(&app.auto_announce_1_started_ms, AUTO_ANNOUNCE_1_PERIOD_MS);
}
static handler_responce_t process_auto_announce_2(void)
{
    return process_auto_announce_timer(&app.auto_announce_2_started_ms, AUTO_ANNOUNCE_2_PERIOD_MS);
}
static handler_responce_t process_auto_announce_3(void)
{
    return process_auto_announce_timer(&app.auto_announce_3_started_ms, AUTO_ANNOUNCE_3_PERIOD_MS);
}

/* Преобразование комбинаций не проверяет состояние машины.
 * Каждое правило — независимый if с полным условием. */
void flush_combo(FlushContext context)
{
    int record_button = NO_BUTTON;
    if (context.count == 1U &&
        (context_has(context, B_F1, LONG_EVENT) || context_has(context, B_F1, NO_EVENT)))
        record_button = B_F1;
    if (context.count == 1U &&
        (context_has(context, B_F2, LONG_EVENT) || context_has(context, B_F2, NO_EVENT)))
        record_button = B_F2;
    if (context.count > 0U)
        process_state_event(RECORD_CHECK_COMBINATION, context.source, record_button);

    if (context.count == 2U && context_has(context, B_F1, LONG_EVENT) &&
        context_has(context, B_F2, LONG_EVENT))
        process_state_event(RECORD_UNLOCK, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_ALERT, SHORT_EVENT))
        process_state_event(ALERT_TOGGLE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_PRM, DOUBLE_SHORT))
        process_state_event(AUTO_RECEIVE_TOGGLE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_PRM, SHORT_EVENT))
        process_state_event(RECEIVE_TOGGLE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_PRM, LONG_EVENT))
        process_state_event(RECEIVE_HOLD, context.source, NO_BUTTON);

    if (context_has(context, B_PRM, NO_EVENT))
        process_state_event(RECEIVE_RELEASE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_F1, DOUBLE_SHORT))
        process_state_event(AUTO_ANNOUNCE_1_TOGGLE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_F2, DOUBLE_SHORT))
        process_state_event(AUTO_ANNOUNCE_2_TOGGLE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_F1, LONG_EVENT))
        process_state_event(ANNOUNCE_1_HOLD, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_F2, LONG_EVENT))
        process_state_event(ANNOUNCE_2_HOLD, context.source, NO_BUTTON);

    if (context_has(context, B_F1, NO_EVENT))
        process_state_event(ANNOUNCE_1_RELEASE, context.source, NO_BUTTON);

    if (context_has(context, B_F2, NO_EVENT))
        process_state_event(ANNOUNCE_2_RELEASE, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_GGS1, LONG_EVENT))
        process_state_event(GGS1_HOLD, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_GGS2, LONG_EVENT))
        process_state_event(GGS2_HOLD, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_APB, LONG_EVENT))
        process_state_event(APB1_HOLD, context.source, NO_BUTTON);

    if (context.count == 1U &&
        context_has(context, B_PRD, LONG_EVENT))
        process_state_event(APB2_HOLD, context.source, NO_BUTTON);

    if (context.count == 2U && context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_GGS2, LONG_EVENT))
    {
        process_state_event(GGS1_HOLD, context.source, NO_BUTTON);
        process_state_event(GGS2_HOLD, context.source, NO_BUTTON);
    }

    if (context.count == 2U && context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_PRD, LONG_EVENT))
    {
        process_state_event(GGS1_HOLD, context.source, NO_BUTTON);
        process_state_event(APB2_HOLD, context.source, NO_BUTTON);
    }

    if (context.count == 2U && context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_GGS2, LONG_EVENT))
    {
        process_state_event(APB1_HOLD, context.source, NO_BUTTON);
        process_state_event(GGS2_HOLD, context.source, NO_BUTTON);
    }

    if (context.count == 2U && context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_PRD, LONG_EVENT))
    {
        process_state_event(APB1_HOLD, context.source, NO_BUTTON);
        process_state_event(APB2_HOLD, context.source, NO_BUTTON);
    }

    if (context_has(context, B_GGS1, NO_EVENT))
        process_state_event(GGS1_RELEASE, context.source, NO_BUTTON);

    if (context_has(context, B_GGS2, NO_EVENT))
        process_state_event(GGS2_RELEASE, context.source, NO_BUTTON);

    if (context_has(context, B_APB, NO_EVENT))
        process_state_event(APB1_RELEASE, context.source, NO_BUTTON);

    if (context_has(context, B_PRD, NO_EVENT))
        process_state_event(APB2_RELEASE, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_PRM, SHORT_EVENT))
        process_state_event(PHRASE_1_PRM, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_GGS1, SHORT_EVENT))
        process_state_event(PHRASE_1_GGS1, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_GGS2, SHORT_EVENT))
        process_state_event(PHRASE_1_GGS2, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_APB, SHORT_EVENT))
        process_state_event(PHRASE_1_APB1, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_PRD, SHORT_EVENT))
        process_state_event(PHRASE_1_APB2, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_PRM, SHORT_EVENT))
        process_state_event(PHRASE_2_PRM, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_GGS1, SHORT_EVENT))
        process_state_event(PHRASE_2_GGS1, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_GGS2, SHORT_EVENT))
        process_state_event(PHRASE_2_GGS2, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_APB, SHORT_EVENT))
        process_state_event(PHRASE_2_APB1, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_PRD, SHORT_EVENT))
        process_state_event(PHRASE_2_APB2, context.source, NO_BUTTON);

    if (context.count == 2U &&
        context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_GGS1);

    if (context.count == 2U &&
        context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_GGS1);

    if (context.count == 2U &&
        context_has(context, B_GGS2, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_GGS2);

    if (context.count == 2U &&
        context_has(context, B_GGS2, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_GGS2);

    if (context.count == 2U &&
        context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_APB);

    if (context.count == 2U &&
        context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_APB);

    if (context.count == 2U &&
        context_has(context, B_PRD, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_PRD);

    if (context.count == 2U &&
        context_has(context, B_PRD, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_PRD);

    if (context.count == 2U &&
        context_has(context, B_PRM, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_PRM);

    if (context.count == 2U &&
        context_has(context, B_PRM, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_PRM);

    if (context.count == 2U &&
        context_has(context, B_F1, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_F1);

    if (context.count == 2U &&
        context_has(context, B_F1, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_F1);

    if (context.count == 2U &&
        context_has(context, B_F2, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
        process_state_event(VOLUME_UP, context.source, B_F2);

    if (context.count == 2U &&
        context_has(context, B_F2, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
        process_state_event(VOLUME_DOWN, context.source, B_F2);
}

/* Входы/выходы разрешает только этот диспетчер.
 * Очередей запросов и восстановления вытесненных режимов нет. */
static void process_state_event(app_event_t event, source_t source, int button)
{
    /* Комбинации обрабатываются раньше process_state_logic(). Не позволяем
     * просроченному разрешению запустить запись на границе тайм-аута. */
    if (app.unlock_record && process_record_unlock() == HANDLER_FINISHED)
        clear_record_unlock();

    switch (event)
    {
        case RECORD_CHECK_COMBINATION:
            if (mode_is_active(RECORD) && app.modes[RECORD].source == source &&
                button != app.record_button && !app.record_invalid)
                invalidate_record();
            break;

        case RECORD_UNLOCK:
            if (!remote_source(source)) return;
            if (mode_is_active(RECORD) ||
                (mode_is_active(ANNOUNCE) && app.modes[ANNOUNCE].is_holding)) return;
            enable_record_unlock();
            break;

        case RECORD_UNLOCK_EXPIRED:
            if (source == OUTSOURCE && app.unlock_record) clear_record_unlock();
            break;

        case ALERT_TOGGLE:
            if (!remote_source(source) || mode_is_active(ACHTUNG)) return;
            if (mode_is_active(ALERT))
            {
                end_alert();
                return;
            }
            if (mode_is_active(RECEIVE)) end_receive();
            for (app_mode_t mode = GGS1; mode <= APB2; ++mode)
                if (mode_is_active(mode)) end_transmission(mode);
            if (mode_is_active(ANNOUNCE)) end_announce();
            if (mode_is_active(PHRASE)) end_phrase();
            if (mode_is_active(RECORD)) end_record(true);
            start_alert(source);
            break;

        case ACHTUNG_PRESSED:
            if (source != OUTSOURCE || mode_is_active(ACHTUNG)) return;
            if (mode_is_active(ALERT)) end_alert();
            if (mode_is_active(RECEIVE)) end_receive();
            for (app_mode_t mode = GGS1; mode <= APB2; ++mode)
                if (mode_is_active(mode)) end_transmission(mode);
            if (mode_is_active(ANNOUNCE)) end_announce();
            if (mode_is_active(PHRASE)) end_phrase();
            if (mode_is_active(RECORD)) end_record(true);
            start_achtung();
            break;

        case ACHTUNG_RELEASED:
            if (source == OUTSOURCE && mode_is_active(ACHTUNG)) end_achtung();
            break;

        case AUTO_RECEIVE_TOGGLE:
            if (app.auto_receive)
            {
                if (mode_is_active(RECEIVE) && app.modes[RECEIVE].source == OUTSOURCE)
                    end_receive();
                end_auto_receive();
            }
            else start_auto_receive();
            break;

        case TRK_PRESSED:
            if (source != OUTSOURCE || !app.auto_receive || mode_is_active(RECEIVE)) return;
            for (app_mode_t mode = ALERT; mode < MODE_COUNT; ++mode)
                if (mode_is_active(mode) && mode != RECORD && !modes_are_compatible(RECEIVE, mode))
                    return;
            if (mode_is_active(PHRASE) && app.phrase_channel == CHANNEL_PRM) end_phrase();
            if (mode_is_active(RECORD)) end_record(true);
            start_receive(OUTSOURCE, false);
            break;

        case TRK_RELEASED:
            /* Начало реального интервала — подтверждённый фронт TRK,
             * а не последний вызов периодического обработчика. */
            if (source == OUTSOURCE && mode_is_active(RECEIVE) &&
                app.modes[RECEIVE].source == OUTSOURCE)
                app.receive_timeout_started_ms = uptime_ms;
            break;

        case RECEIVE_TOGGLE:
        case RECEIVE_HOLD:
            if (!remote_source(source)) return;
            if (mode_is_active(RECEIVE))
            {
                if (event == RECEIVE_TOGGLE && !app.modes[RECEIVE].is_holding) end_receive();
                return;
            }
            for (app_mode_t mode = ALERT; mode < MODE_COUNT; ++mode)
                if (mode_is_active(mode) && mode != RECORD && !modes_are_compatible(RECEIVE, mode))
                    return;
            if (mode_is_active(PHRASE) && app.phrase_channel == CHANNEL_PRM) end_phrase();
            if (mode_is_active(RECORD)) end_record(true);
            start_receive(source, event == RECEIVE_HOLD);
            break;

        case RECEIVE_RELEASE:
            if (!remote_source(source) || !mode_is_active(RECEIVE)) return;
            if (app.modes[RECEIVE].source == OUTSOURCE ||
                (app.modes[RECEIVE].is_holding && app.modes[RECEIVE].source == source))
                end_receive();
            break;

        case RECEIVE_FINISHED:
            if (source == OUTSOURCE && mode_is_active(RECEIVE) &&
                app.modes[RECEIVE].source == OUTSOURCE) end_receive();
            break;

        case GGS1_HOLD:
        case GGS2_HOLD:
        case APB1_HOLD:
        case APB2_HOLD:
        {
            if (!remote_source(source)) return;
            app_mode_t mode = GGS1;
            if (event == GGS2_HOLD) mode = GGS2;
            if (event == APB1_HOLD) mode = APB1;
            if (event == APB2_HOLD) mode = APB2;
            if (mode_is_active(mode)) return;
            /* Сначала все проверки; при отказе ничего не вытесняется. */
            for (app_mode_t active = ALERT; active < MODE_COUNT; ++active)
                if (mode_is_active(active) && active != RECEIVE && active != RECORD &&
                    !modes_are_compatible(mode, active)) return;
            if (mode_is_active(RECEIVE)) end_receive();
            uint8_t channel = (mode == GGS2 || mode == APB2) ? CHANNEL_2 : CHANNEL_1;
            if (mode_is_active(ANNOUNCE) && (app.announce_channels & channel) != 0U)
            {
                uint8_t remaining = (uint8_t)(app.announce_channels & (uint8_t)~channel);
                if (ANNOUNCE_REQUIRE_ALL_CHANNELS || remaining == 0U) end_announce();
                else set_announce_channels(remaining);
            }
            if (mode_is_active(PHRASE) && app.phrase_channel == channel) end_phrase();
            if (mode_is_active(RECORD)) end_record(true);
            start_transmission(mode, source);
            break;
        }

        case GGS1_RELEASE:
        case GGS2_RELEASE:
        case APB1_RELEASE:
        case APB2_RELEASE:
        {
            if (!remote_source(source)) return;
            app_mode_t mode = GGS1;
            if (event == GGS2_RELEASE) mode = GGS2;
            if (event == APB1_RELEASE) mode = APB1;
            if (event == APB2_RELEASE) mode = APB2;
            if (mode_is_active(mode) && app.modes[mode].source == source) end_transmission(mode);
            break;
        }

        case AUTO_ANNOUNCE_1_TOGGLE:
        case AUTO_ANNOUNCE_2_TOGGLE:
            if (mode_is_active(ANNOUNCE) && app.modes[ANNOUNCE].source == OUTSOURCE) end_announce();
            if (event == AUTO_ANNOUNCE_1_TOGGLE) set_auto_announce_1(!app.auto_announce_1);
            else set_auto_announce_2(!app.auto_announce_2);
            reset_auto_announce_timers();
            break;

        case ANNOUNCE_START:
        case ANNOUNCE_1_HOLD:
        case ANNOUNCE_2_HOLD:
        {
            button = NO_BUTTON;
            if (event == ANNOUNCE_START)
            {
                if (source != OUTSOURCE || !(app.auto_announce_1 || app.auto_announce_2)) return;
            }
            else
            {
                if (!remote_source(source)) return;
                button = event == ANNOUNCE_1_HOLD ? B_F1 : B_F2;
            }
            if (event != ANNOUNCE_START && app.unlock_record)
            {
                if (mode_is_active(RECORD)) return;
                if (mode_is_active(ALERT) || mode_is_active(ACHTUNG) ||
                    mode_is_active(RECEIVE) || mode_is_active(GGS1) ||
                    mode_is_active(GGS2) || mode_is_active(APB1) || mode_is_active(APB2)) return;
                if (mode_is_active(ANNOUNCE)) end_announce();
                if (mode_is_active(PHRASE)) end_phrase();
                start_record(source, button);
                return;
            }
            if (mode_is_active(RECORD) || mode_is_active(ANNOUNCE) ||
                mode_is_active(ALERT) || mode_is_active(ACHTUNG)) return;
            uint8_t requested = ANNOUNCE_CHANNELS & (CHANNEL_1 | CHANNEL_2);
            uint8_t channels = requested;
            if (mode_is_active(GGS1) || mode_is_active(APB1)) channels &= (uint8_t)~CHANNEL_1;
            if (mode_is_active(GGS2) || mode_is_active(APB2)) channels &= (uint8_t)~CHANNEL_2;
            if (mode_is_active(PHRASE)) channels &= (uint8_t)~app.phrase_channel;
            if (channels == 0U || (ANNOUNCE_REQUIRE_ALL_CHANNELS && channels != requested)) return;
            start_announce(channels, source, button);
            break;
        }

        case ANNOUNCE_1_RELEASE:
        case ANNOUNCE_2_RELEASE:
            if (!remote_source(source)) return;
            button = event == ANNOUNCE_1_RELEASE ? B_F1 : B_F2;
            if (mode_is_active(RECORD))
            {
                if (app.modes[RECORD].source == source && app.record_button == button)
                    end_record(app.record_invalid);
                return;
            }
            if (!mode_is_active(ANNOUNCE)) return;
            if (app.modes[ANNOUNCE].source == OUTSOURCE ||
                (app.modes[ANNOUNCE].source == source && app.modes[ANNOUNCE].is_holding &&
                 app.announce_button == button)) end_announce();
            break;

        case ANNOUNCE_FINISHED:
            if (source == OUTSOURCE && mode_is_active(ANNOUNCE) &&
                app.modes[ANNOUNCE].source == OUTSOURCE) end_announce();
            break;

        case PHRASE_1_PRM:
        case PHRASE_1_GGS1:
        case PHRASE_1_GGS2:
        case PHRASE_1_APB1:
        case PHRASE_1_APB2:
        case PHRASE_2_PRM:
        case PHRASE_2_GGS1:
        case PHRASE_2_GGS2:
        case PHRASE_2_APB1:
        case PHRASE_2_APB2:
        {
            if (!remote_source(source) || mode_is_active(RECORD) || mode_is_active(PHRASE) ||
                mode_is_active(ALERT) || mode_is_active(ACHTUNG)) return;
            uint8_t number = 1U;
            app_mode_t output = RECEIVE;
            uint8_t channel = CHANNEL_PRM;
            switch (event)
            {
                case PHRASE_1_PRM: break;
                case PHRASE_1_GGS1: output = GGS1; channel = CHANNEL_1; break;
                case PHRASE_1_GGS2: output = GGS2; channel = CHANNEL_2; break;
                case PHRASE_1_APB1: output = APB1; channel = CHANNEL_1; break;
                case PHRASE_1_APB2: output = APB2; channel = CHANNEL_2; break;
                case PHRASE_2_PRM: number = 2U; break;
                case PHRASE_2_GGS1: number = 2U; output = GGS1; channel = CHANNEL_1; break;
                case PHRASE_2_GGS2: number = 2U; output = GGS2; channel = CHANNEL_2; break;
                case PHRASE_2_APB1: number = 2U; output = APB1; channel = CHANNEL_1; break;
                case PHRASE_2_APB2: number = 2U; output = APB2; channel = CHANNEL_2; break;
                default: return;
            }
            if ((channel == CHANNEL_PRM && mode_is_active(RECEIVE)) ||
                (channel == CHANNEL_1 && (mode_is_active(GGS1) || mode_is_active(APB1))) ||
                (channel == CHANNEL_2 && (mode_is_active(GGS2) || mode_is_active(APB2)))) return;
            if (mode_is_active(ANNOUNCE) && (app.announce_channels & channel) != 0U)
            {
                uint8_t remaining = (uint8_t)(app.announce_channels & (uint8_t)~channel);
                if (ANNOUNCE_REQUIRE_ALL_CHANNELS || remaining == 0U) end_announce();
                else set_announce_channels(remaining);
            }
            start_phrase(source, number, output, channel);
            break;
        }

        case PHRASE_FINISHED:
            if (source == OUTSOURCE && mode_is_active(PHRASE)) end_phrase();
            break;

        case VOLUME_UP:
        case VOLUME_DOWN:
        {
            if (!remote_source(source)) return;
            app_mode_t mode;
            volume_target_t target;
            switch (button)
            {
                case B_GGS1: mode = GGS1; target = VOLUME_GGS1; break;
                case B_GGS2: mode = GGS2; target = VOLUME_GGS2; break;
                case B_APB: mode = APB1; target = VOLUME_APB1; break;
                case B_PRD: mode = APB2; target = VOLUME_APB2; break;
                case B_PRM: mode = RECEIVE; target = VOLUME_RECEIVE; break;
                case B_F1:
                case B_F2:
                    if (mode_is_active(RECORD))
                    {
                        mode = RECORD;
                        if (button != app.record_button) return;
                        target = button == B_F1 ? VOLUME_PHRASE_1 : VOLUME_PHRASE_2;
                    }
                    else
                    {
                        mode = ANNOUNCE;
                        if (button != app.announce_button) return;
                        target = VOLUME_ANNOUNCE;
                    }
                    break;
                default: return;
            }
            if (!mode_is_active(mode) || app.modes[mode].source != source ||
                !app.modes[mode].is_holding) return;
            if (mode == RECORD && !app.record_invalid) invalidate_record();
            int value = app.volume[target];
            value += event == VOLUME_UP ? VOLUME_STEP : -VOLUME_STEP;
            if (value < VOLUME_MIN) value = VOLUME_MIN;
            if (value > VOLUME_MAX) value = VOLUME_MAX;
            set_volume(target, (uint8_t)value);
            break;
        }
        default:
            break;
    }
}

static void process_state_logic(void)
{
    if (mode_is_active(ALERT)) process_alert();
    if (mode_is_active(ACHTUNG)) process_achtung();
    if (mode_is_active(RECEIVE))
    {
        if (app.modes[RECEIVE].source == OUTSOURCE)
        {
            if (process_auto_receive() == HANDLER_FINISHED)
                process_state_event(RECEIVE_FINISHED, OUTSOURCE, NO_BUTTON);
        }
        else process_receive();
    }
    if (mode_is_active(ANNOUNCE))
    {
        if (app.modes[ANNOUNCE].source == OUTSOURCE)
        {
            if (process_announce() == HANDLER_FINISHED)
                process_state_event(ANNOUNCE_FINISHED, OUTSOURCE, NO_BUTTON);
        }
        else process_manual_announce();
    }
    if (mode_is_active(PHRASE) && process_phrase() == HANDLER_FINISHED)
        process_state_event(PHRASE_FINISHED, OUTSOURCE, NO_BUTTON);
    if (mode_is_active(RECORD)) process_record();
    if (app.unlock_record && process_record_unlock() == HANDLER_FINISHED)
        process_state_event(RECORD_UNLOCK_EXPIRED, OUTSOURCE, NO_BUTTON);

    /* Автоматика продолжает отсчёт даже при занятых каналах / ALERT / RECORD.
     * Неудачный запрос теряется. Активен только один из трёх таймеров. */
    handler_responce_t result = HANDLER_FINISHED;
    if (app.auto_announce_1 && app.auto_announce_2) result = process_auto_announce_3();
    else if (app.auto_announce_1) result = process_auto_announce_1();
    else if (app.auto_announce_2) result = process_auto_announce_2();
    if (result == HANDLER_PROCESSING)
        process_state_event(ANNOUNCE_START, OUTSOURCE, NO_BUTTON);
}

// Сухой контакт ППК
static void process_achtung_button(void)
{
    bool current_signal = debounced_pins[DEBOUNCE_INPUT_ACHTUNG].state != 0U;

    if (current_signal == app.last_achtung)
    {
        return;
    }

    app.last_achtung = current_signal;

    if (!current_signal)
    {
        process_state_event(ACHTUNG_PRESSED, OUTSOURCE, NO_BUTTON);
    }
    else
    {
        process_state_event(ACHTUNG_RELEASED, OUTSOURCE, NO_BUTTON);
    }
}

// Сигнал TRK
static void process_trk_button(void)
{
    bool current_signal = debounced_pins[DEBOUNCE_INPUT_TRK].state != 0U;

    if (current_signal == app.last_trk)
    {
        return;
    }

    app.last_trk = current_signal;

    if (!current_signal)
    {
        process_state_event(TRK_PRESSED, OUTSOURCE, NO_BUTTON);
    }
    else
    {
        process_state_event(TRK_RELEASED, OUTSOURCE, NO_BUTTON);
    }
}


/* Обработчик системного таймера: только счётчик, без логики режимов. */
void SysTick_Handler(void)
{
    uptime_ms++;
}
