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

// Настройки машины состояний
#define MODE_COUNT 18U
#define INITIAL_ACTIVE_MODES        0U
#define AUTO_RECEIVE_DEFAULT        false

#define ACHTUNG_INITIAL_LEVEL       true
#define TRK_INITIAL_LEVEL           true

#define POLL_INTERVAL_MS        10U
#define RECEIVE_TIMEOUT_MS      500U

#define ANNOUNCE1_PERIOD_MS         500U
#define ANNOUNCE2_PERIOD_MS         600U
#define ANNOUNCE1_AUDIO_MS          200U  // Mock duration; replace with audio driver later
#define ANNOUNCE2_AUDIO_MS          300U  // Mock duration; replace with audio driver later

#define VOLUME_MIN                  0
#define VOLUME_MAX                  100
#define VOLUME_STEP                 5
#define VOLUME_CURRENT_MODE        (-1)
#define VOLUME_UP                   1
#define VOLUME_DOWN                (-1)

#define ALERT_COMBO_BUTTON_COUNT           1U
#define PRM_COMBO_BUTTON_COUNT             1U
#define GGS_SINGLE_BUTTON_COUNT            1U
#define GGS_PAIR_BUTTON_COUNT              2U
#define APB_SINGLE_BUTTON_COUNT            1U
#define APB_PAIR_BUTTON_COUNT              2U
#define AUTO_ANNOUNCE_SINGLE_BUTTON_COUNT  1U
#define AUTO_ANNOUNCE_PAIR_BUTTON_COUNT    2U
#define VOLUME_SINGLE_BUTTON_COUNT         1U
#define VOLUME_HELD_BUTTON_COUNT           2U
#define RECORD_SINGLE_BUTTON_COUNT         1U
#define PHRASE_PLAY_BUTTON_COUNT           2U


typedef enum
{
    RECEIVE = 0,
    GGS1,
    GGS2,
    APB1,
    APB2,
    ANNOUNCE1,
    ANNOUNCE2,
    AUTO_ANNOUNCE1,
    AUTO_ANNOUNCE2,
    ALERT,
    ACHTUNG,
    RECORD1,
    RECORD2,
    PLAY_GGS1,
    PLAY_GGS2,
    PLAY_APB1,
    PLAY_APB2,
    PLAY_RECEIVE
} app_mode_t;

#define PLAYBACK_COUNT 5U
#define RECORD_MIN_MS  POLL_INTERVAL_MS
#define RECORD_MAX_MS  60000U
static const app_mode_t playback_modes[PLAYBACK_COUNT] = {
    PLAY_GGS1,
    PLAY_GGS2,
    PLAY_APB1,
    PLAY_APB2,
    PLAY_RECEIVE
};


#define PHRASE_COUNT        2U
#define PHRASE1 0U
#define PHRASE2 1U

#define VOLUME_COUNT        9U
#define VOLUME_DEFAULT     50U
#define NO_PHRASE          (-1)
typedef struct
{
    source_t source;
    int8_t phrase;
    uint32_t duration_ms;
    uint32_t elapsed_ms;
} mode_state_t;

typedef struct
{
    uint32_t duration_ms;
    uint32_t record_elapsed_ms;
    bool volume_adjusted;
} phrase_state_t;

typedef struct
{
    uint32_t active_modes;
    mode_state_t modes[MODE_COUNT];

    uint8_t volumes[VOLUME_COUNT];
    phrase_state_t phrases[PHRASE_COUNT];

    bool automation_paused;
    uint32_t saved_auto_modes;
    source_t saved_auto1_source;
    source_t saved_auto2_source;
    bool priority_transition;

    bool auto_receive;

    uint32_t receive_timeout_elapsed_ms;

    uint32_t announce1_wait_elapsed_ms;
    uint32_t announce2_wait_elapsed_ms;

    uint32_t announce1_audio_elapsed_ms;
    uint32_t announce2_audio_elapsed_ms;

    bool last_achtung;
    bool last_trk;
} app_runtime_t;

// таблицы приоритетов и совместимости
#define MODE_MASK(mode) (UINT32_C(1) << (mode))

#define AUTO_ANNOUNCE_MASK   (MODE_MASK(AUTO_ANNOUNCE1) | MODE_MASK(AUTO_ANNOUNCE2))
#define HIGH_PRIORITY_MASK  (MODE_MASK(ALERT) | MODE_MASK(ACHTUNG))
#define RECORD_MASK         (MODE_MASK(RECORD1) | MODE_MASK(RECORD2))

#define MANUAL_MIC_MASK     (MODE_MASK(GGS1) | MODE_MASK(GGS2) | MODE_MASK(APB1) | MODE_MASK(APB2))

#define PLAY_CHANNEL1_MASK  (MODE_MASK(PLAY_GGS1) | MODE_MASK(PLAY_APB1))
#define PLAY_CHANNEL2_MASK  (MODE_MASK(PLAY_GGS2) | MODE_MASK(PLAY_APB2))

#define PLAYBACK_MASK       (PLAY_CHANNEL1_MASK | PLAY_CHANNEL2_MASK | MODE_MASK(PLAY_RECEIVE))

#define ALL_MODES_MASK      (MODE_MASK(MODE_COUNT) - 1U)

#define RECEIVE_PRIORITY        2U
#define GGS1_PRIORITY           3U
#define GGS2_PRIORITY           3U
#define APB1_PRIORITY           3U
#define APB2_PRIORITY           3U
#define ANNOUNCE1_PRIORITY      1U
#define ANNOUNCE2_PRIORITY      1U
#define AUTO_ANNOUNCE1_PRIORITY 0U
#define AUTO_ANNOUNCE2_PRIORITY 0U
#define ALERT_PRIORITY          4U
#define ACHTUNG_PRIORITY        5U
#define RECORD1_PRIORITY        3U
#define RECORD2_PRIORITY        3U
#define PLAY_GGS1_PRIORITY      1U
#define PLAY_GGS2_PRIORITY      1U
#define PLAY_APB1_PRIORITY      1U
#define PLAY_APB2_PRIORITY      1U
#define PLAY_RECEIVE_PRIORITY   1U

static const uint8_t mode_priorities[MODE_COUNT] = {
    [RECEIVE]        = RECEIVE_PRIORITY,
    [GGS1]           = GGS1_PRIORITY,
    [GGS2]           = GGS2_PRIORITY,
    [APB1]           = APB1_PRIORITY,
    [APB2]           = APB2_PRIORITY,
    [ANNOUNCE1]      = ANNOUNCE1_PRIORITY,
    [ANNOUNCE2]      = ANNOUNCE2_PRIORITY,
    [AUTO_ANNOUNCE1] = AUTO_ANNOUNCE1_PRIORITY,
    [AUTO_ANNOUNCE2] = AUTO_ANNOUNCE2_PRIORITY,
    [ALERT]          = ALERT_PRIORITY,
    [ACHTUNG]        = ACHTUNG_PRIORITY,
    [RECORD1]        = RECORD1_PRIORITY,
    [RECORD2]        = RECORD2_PRIORITY,
    [PLAY_GGS1]      = PLAY_GGS1_PRIORITY,
    [PLAY_GGS2]      = PLAY_GGS2_PRIORITY,
    [PLAY_APB1]      = PLAY_APB1_PRIORITY,
    [PLAY_APB2]      = PLAY_APB2_PRIORITY,
    [PLAY_RECEIVE]   = PLAY_RECEIVE_PRIORITY
};


// Volume setting index for each mode; -1 means no volume control.
static const int8_t volume_settings[MODE_COUNT] = {
    [RECEIVE] = 0,
    [GGS1] = 1,
    [GGS2] = 2,
    [APB1] = 3,
    [APB2] = 4,
    [ANNOUNCE1] = 5,
    [ANNOUNCE2] = 6,
    [AUTO_ANNOUNCE1] = 5,
    [AUTO_ANNOUNCE2] = 6,
    [ALERT] = -1,
    [ACHTUNG] = -1,
    [RECORD1] = 7,
    [RECORD2] = 8,
    [PLAY_GGS1] = 1,
    [PLAY_GGS2] = 2,
    [PLAY_APB1] = 3,
    [PLAY_APB2] = 4,
    [PLAY_RECEIVE] = 0
};

static const app_event_t playback_finish_events[PLAYBACK_COUNT] = {
    PLAY_GGS1_FINISH,
    PLAY_GGS2_FINISH,
    PLAY_APB1_FINISH,
    PLAY_APB2_FINISH,
    PLAY_RECEIVE_FINISH
};

static const char *const mode_names[MODE_COUNT] = {
    "RECEIVE", "GGS1", "GGS2", "APB1", "APB2",
    "ANNOUNCE1", "ANNOUNCE2", "AUTO_ANNOUNCE1", "AUTO_ANNOUNCE2",
    "ALERT", "ACHTUNG", "RECORD1", "RECORD2",
    "PLAY_GGS1", "PLAY_GGS2", "PLAY_APB1", "PLAY_APB2", "PLAY_RECEIVE"
};

static const uint32_t mode_compatibility[MODE_COUNT] = {
    [RECEIVE] =
        MODE_MASK(ANNOUNCE1) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK,

    [GGS1] =
        MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL2_MASK,

    [GGS2] =
        MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE1) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL1_MASK,

    [APB1] =
        MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL2_MASK,

    [APB2] =
        MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE1) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL1_MASK,

    [ANNOUNCE1] =
        MODE_MASK(RECEIVE) | MODE_MASK(GGS2) | MODE_MASK(APB2) |
        MODE_MASK(ANNOUNCE2) | AUTO_ANNOUNCE_MASK |
        RECORD_MASK | PLAY_CHANNEL2_MASK | MODE_MASK(PLAY_RECEIVE),

    [ANNOUNCE2] =
        MODE_MASK(RECEIVE) | MODE_MASK(GGS1) | MODE_MASK(APB1) |
        MODE_MASK(ANNOUNCE1) | AUTO_ANNOUNCE_MASK |
        RECORD_MASK | PLAY_CHANNEL1_MASK | MODE_MASK(PLAY_RECEIVE),

    [AUTO_ANNOUNCE1] =
        ALL_MODES_MASK & ~(HIGH_PRIORITY_MASK | MODE_MASK(AUTO_ANNOUNCE1)),

    [AUTO_ANNOUNCE2] =
        ALL_MODES_MASK & ~(HIGH_PRIORITY_MASK | MODE_MASK(AUTO_ANNOUNCE2)),

    [ALERT] = 0U,

    [ACHTUNG] = 0U,

    [RECORD1] =
        ALL_MODES_MASK & ~(HIGH_PRIORITY_MASK | MODE_MASK(RECORD1)),

    [RECORD2] =
        ALL_MODES_MASK & ~(HIGH_PRIORITY_MASK | MODE_MASK(RECORD2)),

    [PLAY_GGS1] =
        MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL2_MASK,

    [PLAY_GGS2] =
        MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE1) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL1_MASK,

    [PLAY_APB1] =
        MODE_MASK(GGS2) | MODE_MASK(APB2) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL2_MASK,

    [PLAY_APB2] =
        MODE_MASK(GGS1) | MODE_MASK(APB1) | MODE_MASK(ANNOUNCE1) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK | PLAY_CHANNEL1_MASK,

    [PLAY_RECEIVE] =
        MODE_MASK(ANNOUNCE1) | MODE_MASK(ANNOUNCE2) |
        AUTO_ANNOUNCE_MASK | RECORD_MASK
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

static app_runtime_t app = {
    .active_modes = INITIAL_ACTIVE_MODES,
    .auto_receive = AUTO_RECEIVE_DEFAULT,
    .last_achtung = ACHTUNG_INITIAL_LEVEL,
    .last_trk = TRK_INITIAL_LEVEL
};

sd_card_info_t card_info;

// нужно для announce
static audio_file_t next_file;
// файл который проигрывается 
static const char *playing_filename = NULL;

/* ============================================================================
 * Основная логика
 * ========================================================================== */
// Internal state machine functions
static void app_runtime_init(void);
static const char *source_name(source_t source);
static inline bool mode_is_active(app_mode_t mode);
static inline bool modes_are_compatible(app_mode_t first, app_mode_t second);
static void process_state_event(app_event_t event, source_t source);
static void process_state_logic(void);
static void process_achtung_button(void);
static void process_trk_button(void);
static void end_mode(app_mode_t mode, source_t source);
static bool prepare_mode_start(app_mode_t mode, source_t source);
static void pause_automation(void);
static void resume_automation(void);

static void receive_start(source_t source);
static void receive_end(source_t source);
static void ggs1_start(source_t source);
static void ggs1_end(source_t source);
static void ggs2_start(source_t source);
static void ggs2_end(source_t source);
static void apb1_start(source_t source);
static void apb1_end(source_t source);
static void apb2_start(source_t source);
static void apb2_end(source_t source);
static void alert_start(source_t source);
static void alert_end(source_t source);
static void achtung_start(source_t source);
static void achtung_end(source_t source);
static void auto_announce1_start(source_t source);
static void auto_announce1_end(source_t source);
static void auto_announce2_start(source_t source);
static void auto_announce2_end(source_t source);
static void announce1_start(source_t source);
static void announce1_end(source_t source);
static void announce2_start(source_t source);
static void announce2_end(source_t source);
static void auto_receive_start(source_t source);
static void auto_receive_end(source_t source);
static bool record1_start(source_t source);
static void record1_end(source_t source, bool save);
static bool record2_start(source_t source);
static void record2_end(source_t source, bool save);
static bool play_ggs1_start(source_t source, int8_t phrase);
static void play_ggs1_end(source_t source);
static bool play_ggs2_start(source_t source, int8_t phrase);
static void play_ggs2_end(source_t source);
static bool play_apb1_start(source_t source, int8_t phrase);
static void play_apb1_end(source_t source);
static bool play_apb2_start(source_t source, int8_t phrase);
static void play_apb2_end(source_t source);
static bool play_receive_start(source_t source, int8_t phrase);
static void play_receive_end(source_t source);
static void process_common_event(app_event_t event, source_t source, int32_t target, int8_t direction);

static void app_runtime_init(void)
{
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app.modes[i].source = OUTSOURCE;
        app.modes[i].phrase = NO_PHRASE;
    }

    for (uint8_t i = 0; i < VOLUME_COUNT; i++)
    {
        app.volumes[i] = VOLUME_DEFAULT;
    }

    app.saved_auto1_source = OUTSOURCE;
    app.saved_auto2_source = OUTSOURCE;
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
    set_all_leds(LED_VD1);

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

    LedController_PeriphInit();
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


/* ============================================================================
 * Стейт машина
 * ========================================================================== */

 // Название источника для логирования
static const char *source_name(source_t source)
{
    switch (source)
    {
        case REMOTE_1:
            return "REMOTE_1";

        case REMOTE_2:
            return "REMOTE_2";

        case OUTSOURCE:
            return "OUTSOURCE";

        default:
            return "UNKNOWN";
    }
}
static inline bool mode_is_active(app_mode_t mode)
{
    return (app.active_modes & MODE_MASK(mode)) != 0U;
}

static inline bool modes_are_compatible(app_mode_t first, app_mode_t second)
{
    return ((mode_compatibility[first] & MODE_MASK(second)) != 0U &&
            (mode_compatibility[second] & MODE_MASK(first)) != 0U);
}
// Завершение приёма
static void receive_end(source_t source)
{
    if (!mode_is_active(RECEIVE))
    {
        return;
    }

    printf("END RECEIVE: event source = %s, receive source = %s\r\n",
           source_name(source), source_name(app.modes[RECEIVE].source));

    app.active_modes &= ~MODE_MASK(RECEIVE);
    app.modes[RECEIVE].source = OUTSOURCE;
    app.receive_timeout_elapsed_ms = 0U;
}

// Завершение GGS1
static void ggs1_end(source_t source)
{
    if (!mode_is_active(GGS1) || app.modes[GGS1].source != source)
    {
        return;
    }

    printf("END GGS1: source = %s\r\n", source_name(source));

    app.active_modes &= ~MODE_MASK(GGS1);
    app.modes[GGS1].source = OUTSOURCE;
}

// Завершение GGS2
static void ggs2_end(source_t source)
{
    if (!mode_is_active(GGS2) || app.modes[GGS2].source != source)
    {
        return;
    }

    printf("END GGS2: source = %s\r\n", source_name(source));

    app.active_modes &= ~MODE_MASK(GGS2);
    app.modes[GGS2].source = OUTSOURCE;
}

// Диспетчер завершения режимов
static void end_mode(app_mode_t mode, source_t source)
{
    switch (mode)
    {
        case RECEIVE:
            receive_end(source);
            break;

        case GGS1:
            ggs1_end(source);
            break;

        case GGS2:
            ggs2_end(source);
            break;

        case APB1:
            apb1_end(source);
            break;

        case APB2:
            apb2_end(source);
            break;

        case ANNOUNCE1:
            announce1_end(source);
            break;

        case ANNOUNCE2:
            announce2_end(source);
            break;

        case AUTO_ANNOUNCE1:
            auto_announce1_end(source);
            break;

        case AUTO_ANNOUNCE2:
            auto_announce2_end(source);
            break;

        case ALERT:
            alert_end(source);
            break;

        case ACHTUNG:
            achtung_end(source);
            break;

        case RECORD1:
            record1_end(source, false);
            break;

        case RECORD2:
            record2_end(source, false);
            break;

        case PLAY_GGS1:
            play_ggs1_end(source);
            break;

        case PLAY_GGS2:
            play_ggs2_end(source);
            break;

        case PLAY_APB1:
            play_apb1_end(source);
            break;

        case PLAY_APB2:
            play_apb2_end(source);
            break;

        case PLAY_RECEIVE:
            play_receive_end(source);
            break;

        default:
            break;
    }
}

static void resume_automation(void)
{
    if (!app.automation_paused ||
        app.priority_transition ||
        (app.active_modes & HIGH_PRIORITY_MASK) != 0U)
    {
        return;
    }

    if (app.saved_auto_modes & MODE_MASK(AUTO_ANNOUNCE1))
    {
        auto_announce1_start(app.saved_auto1_source);
    }

    if (app.saved_auto_modes & MODE_MASK(AUTO_ANNOUNCE2))
    {
        auto_announce2_start(app.saved_auto2_source);
    }

    app.automation_paused = false;
    app.saved_auto_modes = 0U;
    app.saved_auto1_source = OUTSOURCE;
    app.saved_auto2_source = OUTSOURCE;
}
static void pause_automation(void)
{
    if (app.automation_paused)
    {
        return;
    }

    app.automation_paused = true;
    app.saved_auto_modes = app.active_modes & AUTO_ANNOUNCE_MASK;
    app.saved_auto1_source = app.modes[AUTO_ANNOUNCE1].source;
    app.saved_auto2_source = app.modes[AUTO_ANNOUNCE2].source;
}
// Проверка возможности запуска режима и вытеснение несовместимых
static bool prepare_mode_start(app_mode_t mode, source_t source)
{
    if (mode_is_active(mode))
    {
        return false;
    }

    // Проверка занятости микрофона своего пульта
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app_mode_t active = (app_mode_t)i;

        if (mode_is_active(active) && app.modes[active].source == source)
        {
            if (((MODE_MASK(mode) & RECORD_MASK) &&
                 (MODE_MASK(active) & (RECORD_MASK | MANUAL_MIC_MASK))) ||
                ((MODE_MASK(mode) & MANUAL_MIC_MASK) &&
                 (MODE_MASK(active) & RECORD_MASK)))
            {
                return false;
            }
        }
    }

    // Проверка всех конфликтов до вытеснения
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app_mode_t active = (app_mode_t)i;

        if (mode_is_active(active) && !modes_are_compatible(mode, active))
        {
            if (mode_priorities[mode] <= mode_priorities[active])
            {
                return false;
            }
        }
    }

    // Сохранение автоматики перед приоритетным вытеснением
    bool priority_start = (mode == ALERT || mode == ACHTUNG);

    if (priority_start)
    {
        pause_automation();
        app.priority_transition = true;
    }

    // Завершение несовместимых режимов
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app_mode_t active = (app_mode_t)i;

        if (mode_is_active(active) && !modes_are_compatible(mode, active))
        {
            end_mode(active, app.modes[active].source);
        }
    }

    if (priority_start)
    {
        app.priority_transition = false;
    }

    return true;
}
// Запуск приёма
static void receive_start(source_t source)
{
    if (source == OUTSOURCE && !app.auto_receive)
    {
        return;
    }

    if (!prepare_mode_start(RECEIVE, source))
    {
        return;
    }

    app.modes[RECEIVE].source = source;
    app.receive_timeout_elapsed_ms = 0U;
    app.active_modes |= MODE_MASK(RECEIVE);

    printf("START RECEIVE: source = %s\r\n", source_name(source));
}

// Запуск GGS1
static void ggs1_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(GGS1, source))
    {
        return;
    }

    app.modes[GGS1].source = source;
    app.active_modes |= MODE_MASK(GGS1);

    printf("START GGS1: source = %s\r\n", source_name(source));
}

// Запуск GGS2
static void ggs2_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(GGS2, source))
    {
        return;
    }

    app.modes[GGS2].source = source;
    app.active_modes |= MODE_MASK(GGS2);

    printf("START GGS2: source = %s\r\n", source_name(source));
}

// Включение автоматического приёма
static void auto_receive_start(source_t source)
{
    app.auto_receive = true;

    printf("IDLE: auto receive on, source = %s\r\n",
           source_name(source));
}

// Выключение автоматического приёма
static void auto_receive_end(source_t source)
{
    app.auto_receive = false;

    printf("IDLE: auto receive off, source = %s\r\n",
           source_name(source));
}

static void volume_changed_handler(app_mode_t mode, uint8_t volume, source_t source)
{
    // The codec volume is not changed yet: this is a state-machine mock.
    printf("VOLUME: %s=%u source=%s\r\n",
           mode_names[mode], (unsigned)volume, source_name(source));
}

static void volume_rejected_handler(source_t source, const char *reason)
{
    printf("VOLUME REJECT: %s source=%s\r\n", reason, source_name(source));
}

static void volume_change(app_mode_t mode, int8_t direction, source_t source)
{
    int8_t setting = volume_settings[mode];
    int32_t value = (int32_t)app.volumes[setting] + (int32_t)direction * VOLUME_STEP;

    if (value < VOLUME_MIN)
    {
        value = VOLUME_MIN;
    }

    if (value > VOLUME_MAX)
    {
        value = VOLUME_MAX;
    }

    app.volumes[setting] = (uint8_t)value;

    if (mode == RECORD1)
    {
        app.phrases[PHRASE1].volume_adjusted = true;
    }
    else if (mode == RECORD2)
    {
        app.phrases[PHRASE2].volume_adjusted = true;
    }

    volume_changed_handler(mode, (uint8_t)value, source);
}

static void process_common_event(app_event_t event, source_t source, int32_t target, int8_t direction)
{
    if (event != VOLUME_CHANGE)
    {
        return;
    }

    if ((source != REMOTE_1 && source != REMOTE_2) ||
        (direction != VOLUME_UP && direction != VOLUME_DOWN))
    {
        volume_rejected_handler(source, "invalid_request");
        return;
    }

    if ((app.active_modes & HIGH_PRIORITY_MASK) != 0U)
    {
        volume_rejected_handler(source, "unsupported_mode");
        return;
    }

    // Background automation and its audio share one logical mode for volume.
    uint32_t active = app.active_modes & ~AUTO_ANNOUNCE_MASK;

    if (mode_is_active(AUTO_ANNOUNCE1))
    {
        active |= MODE_MASK(ANNOUNCE1);
    }

    if (mode_is_active(AUTO_ANNOUNCE2))
    {
        active |= MODE_MASK(ANNOUNCE2);
    }

    if (active == 0U)
    {
        volume_rejected_handler(source, "no_active_mode");
        return;
    }

    if ((active & (active - 1U)) != 0U)
    {
        volume_rejected_handler(source, "multiple_modes");
        return;
    }

    app_mode_t mode = RECEIVE;

    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        if ((active & MODE_MASK(i)) != 0U)
        {
            mode = (app_mode_t)i;
            break;
        }
    }

    if (target == VOLUME_CURRENT_MODE)
    {
        if (mode != RECEIVE && mode != ANNOUNCE1 && mode != ANNOUNCE2 &&
            (MODE_MASK(mode) & PLAYBACK_MASK) == 0U)
        {
            volume_rejected_handler(source, "hold_required");
            return;
        }
    }
    else
    {
        if (target != (int32_t)mode ||
            (mode != GGS1 && mode != GGS2 && mode != APB1 && mode != APB2 &&
             mode != RECORD1 && mode != RECORD2))
        {
            volume_rejected_handler(source, "wrong_mode");
            return;
        }

        if (app.modes[mode].source != source)
        {
            volume_rejected_handler(source, "not_owner");
            return;
        }
    }

    volume_change(mode, direction, source);
}

// Преобразование комбинации в событие
void flush_combo(FlushContext context)
{
    // ALERT
    if (context.count == ALERT_COMBO_BUTTON_COUNT &&
        context_has(context, B_ALERT, SHORT_EVENT))
    {
        process_state_event(ALERT_TOGGLE, context.source);
    }

    // RECEIVE
    if (context_has(context, B_PRM, NO_EVENT))
    {
        process_state_event(RECEIVE_FINISH, context.source);
    }

    if (context.count == PRM_COMBO_BUTTON_COUNT &&
        context_has(context, B_PRM, SHORT_EVENT))
    {
        process_state_event(RECEIVE_TOGGLE, context.source);
    }

    if (context.count == PRM_COMBO_BUTTON_COUNT &&
        context_has(context, B_PRM, DOUBLE_SHORT))
    {
        process_state_event(AUTO_RECEIVE_TOGGLE, context.source);
    }

    if (context.count == PRM_COMBO_BUTTON_COUNT &&
        context_has(context, B_PRM, LONG_EVENT))
    {
        process_state_event(RECEIVE_START, context.source);
    }

    // GGS1 / GGS2
    if (context_has(context, B_GGS1, NO_EVENT))
    {
        process_state_event(GGS1_FINISH, context.source);
    }

    if (context_has(context, B_GGS2, NO_EVENT))
    {
        process_state_event(GGS2_FINISH, context.source);
    }

    if (context.count == GGS_SINGLE_BUTTON_COUNT &&
        context_has(context, B_GGS1, LONG_EVENT))
    {
        process_state_event(GGS1_START, context.source);
    }

    if (context.count == GGS_SINGLE_BUTTON_COUNT &&
        context_has(context, B_GGS2, LONG_EVENT))
    {
        process_state_event(GGS2_START, context.source);
    }

    if (context.count == GGS_PAIR_BUTTON_COUNT &&
        context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_GGS2, LONG_EVENT))
    {
        process_state_event(GGS1_START, context.source);
        process_state_event(GGS2_START, context.source);
    }

    // APB1 / APB2
    if (context_has(context, B_PRD, NO_EVENT))
    {
        process_state_event(APB1_FINISH, context.source);
    }

    if (context_has(context, B_APB, NO_EVENT))
    {
        process_state_event(APB2_FINISH, context.source);
    }

    if (context.count == APB_SINGLE_BUTTON_COUNT &&
        context_has(context, B_PRD, LONG_EVENT))
    {
        process_state_event(APB1_START, context.source);
    }

    if (context.count == APB_SINGLE_BUTTON_COUNT &&
        context_has(context, B_APB, LONG_EVENT))
    {
        process_state_event(APB2_START, context.source);
    }

    if (context.count == APB_PAIR_BUTTON_COUNT &&
        context_has(context, B_PRD, LONG_EVENT) &&
        context_has(context, B_APB, LONG_EVENT))
    {
        process_state_event(APB1_START, context.source);
        process_state_event(APB2_START, context.source);
    }

    // AUTO ANNOUNCE
    if (context.count == AUTO_ANNOUNCE_SINGLE_BUTTON_COUNT &&
        context_has(context, B_F1, DOUBLE_SHORT))
    {
        process_state_event(AUTO_ANNOUNCE1_TOGGLE, context.source);
    }

    if (context.count == AUTO_ANNOUNCE_SINGLE_BUTTON_COUNT &&
        context_has(context, B_F2, DOUBLE_SHORT))
    {
        process_state_event(AUTO_ANNOUNCE2_TOGGLE, context.source);
    }

    if (context.count == AUTO_ANNOUNCE_PAIR_BUTTON_COUNT &&
        context_has(context, B_F1, DOUBLE_SHORT) &&
        context_has(context, B_F2, DOUBLE_SHORT))
    {
        process_state_event(AUTO_ANNOUNCE1_TOGGLE, context.source);
        process_state_event(AUTO_ANNOUNCE2_TOGGLE, context.source);
    }

    // VOLUME UP
    if (context.count == VOLUME_SINGLE_BUTTON_COUNT &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, VOLUME_CURRENT_MODE, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, GGS1, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_GGS2, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, GGS2, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_PRD, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, APB1, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, APB2, VOLUME_UP);
    }

    // VOLUME DOWN
    if (context.count == VOLUME_SINGLE_BUTTON_COUNT &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, VOLUME_CURRENT_MODE, VOLUME_DOWN);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_GGS1, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, GGS1, VOLUME_DOWN);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_GGS2, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, GGS2, VOLUME_DOWN);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_PRD, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, APB1, VOLUME_DOWN);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_APB, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, APB2, VOLUME_DOWN);
    }

    // RECORD1 / PHRASE1
    if (context_has(context, B_F1, NO_EVENT))
    {
        process_state_event(RECORD1_FINISH, context.source);
    }

    if (context.count == RECORD_SINGLE_BUTTON_COUNT &&
        context_has(context, B_F1, LONG_EVENT))
    {
        process_state_event(RECORD1_START, context.source);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_F1, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, RECORD1, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_F1, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, RECORD1, VOLUME_DOWN);
    }

    // PLAY PHRASE1
    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_GGS1, SHORT_EVENT))
    {
        process_state_event(PLAY1_GGS1_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_GGS2, SHORT_EVENT))
    {
        process_state_event(PLAY1_GGS2_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_PRD, SHORT_EVENT))
    {
        process_state_event(PLAY1_APB1_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_APB, SHORT_EVENT))
    {
        process_state_event(PLAY1_APB2_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F1, SHORT_EVENT) &&
        context_has(context, B_PRM, SHORT_EVENT))
    {
        process_state_event(PLAY1_RECEIVE_START, context.source);
    }

    // RECORD2 / PHRASE2
    if (context_has(context, B_F2, NO_EVENT))
    {
        process_state_event(RECORD2_FINISH, context.source);
    }

    if (context.count == RECORD_SINGLE_BUTTON_COUNT &&
        context_has(context, B_F2, LONG_EVENT))
    {
        process_state_event(RECORD2_START, context.source);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_F2, LONG_EVENT) &&
        context_has(context, B_PLUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, RECORD2, VOLUME_UP);
    }

    if (context.count == VOLUME_HELD_BUTTON_COUNT &&
        context_has(context, B_F2, LONG_EVENT) &&
        context_has(context, B_MINUS, SHORT_EVENT))
    {
        process_common_event(VOLUME_CHANGE, context.source, RECORD2, VOLUME_DOWN);
    }

    // PLAY PHRASE2
    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_GGS1, SHORT_EVENT))
    {
        process_state_event(PLAY2_GGS1_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_GGS2, SHORT_EVENT))
    {
        process_state_event(PLAY2_GGS2_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_PRD, SHORT_EVENT))
    {
        process_state_event(PLAY2_APB1_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_APB, SHORT_EVENT))
    {
        process_state_event(PLAY2_APB2_START, context.source);
    }

    if (context.count == PHRASE_PLAY_BUTTON_COUNT &&
        context_has(context, B_F2, SHORT_EVENT) &&
        context_has(context, B_PRM, SHORT_EVENT))
    {
        process_state_event(PLAY2_RECEIVE_START, context.source);
    }
}
// Диспетчер событий машины состояний
static void process_state_event(app_event_t event, source_t source)
{
    // Блокировка обычных событий в приоритетных режимах
    if ((app.active_modes & HIGH_PRIORITY_MASK) != 0U)
    {
        if (event != ALERT_TOGGLE &&
            event != ACHTUNG_PRESSED &&
            event != ACHTUNG_RELEASED)
        {
            return;
        }
    }

    switch (event)
    {
        // Приоритетные режимы
        case ALERT_TOGGLE:
            if (mode_is_active(ALERT))
            {
                alert_end(source);
            }
            else
            {
                alert_start(source);
            }
            break;

        case ACHTUNG_PRESSED:
            achtung_start(source);
            break;

        case ACHTUNG_RELEASED:
            achtung_end(source);
            break;

        // RECEIVE
        case TRK_PRESSED:
            if (mode_is_active(RECEIVE) &&
                app.modes[RECEIVE].source == OUTSOURCE)
            {
                receive_end(OUTSOURCE);
            }

            receive_start(OUTSOURCE);
            break;

        case AUTO_RECEIVE_TOGGLE:
            if (app.active_modes == 0U)
            {
                if (app.auto_receive)
                {
                    auto_receive_end(source);
                }
                else
                {
                    auto_receive_start(source);
                }
            }
            break;

        case RECEIVE_TOGGLE:
            if (mode_is_active(RECEIVE))
            {
                receive_end(source);
            }
            else
            {
                receive_start(source);
            }
            break;

        case RECEIVE_START:
            receive_start(source);
            break;

        case RECEIVE_FINISH:
            if (source != OUTSOURCE ||
                app.modes[RECEIVE].source == OUTSOURCE)
            {
                receive_end(source);
            }
            break;

        case RECEIVE_TIMEOUT:
            if (source == OUTSOURCE &&
                mode_is_active(RECEIVE) &&
                app.modes[RECEIVE].source == OUTSOURCE &&
                app.last_trk)
            {
                receive_end(source);
            }
            break;

        // GGS
        case GGS1_START:
            ggs1_start(source);
            break;

        case GGS1_FINISH:
            ggs1_end(source);
            break;

        case GGS2_START:
            ggs2_start(source);
            break;

        case GGS2_FINISH:
            ggs2_end(source);
            break;

        // APB
        case APB1_START:
            apb1_start(source);
            break;

        case APB1_FINISH:
            apb1_end(source);
            break;

        case APB2_START:
            apb2_start(source);
            break;

        case APB2_FINISH:
            apb2_end(source);
            break;

        // Автоматические оповещения
        case AUTO_ANNOUNCE1_TOGGLE:
            if (mode_is_active(AUTO_ANNOUNCE1))
            {
                auto_announce1_end(source);
            }
            else
            {
                auto_announce1_start(source);
            }
            break;

        case AUTO_ANNOUNCE2_TOGGLE:
            if (mode_is_active(AUTO_ANNOUNCE2))
            {
                auto_announce2_end(source);
            }
            else
            {
                auto_announce2_start(source);
            }
            break;

        // Оповещения
        case ANNOUNCE1_START:
            announce1_start(source);
            break;

        case ANNOUNCE1_FINISH:
            announce1_end(source);
            break;

        case ANNOUNCE2_START:
            announce2_start(source);
            break;

        case ANNOUNCE2_FINISH:
            announce2_end(source);
            break;

        // Запись фраз
        case RECORD1_START:
            record1_start(source);
            break;

        case RECORD1_FINISH:
            record1_end(source, true);
            break;

        case RECORD2_START:
            record2_start(source);
            break;

        case RECORD2_FINISH:
            record2_end(source, true);
            break;

        // Воспроизведение первой фразы
        case PLAY1_GGS1_START:
            play_ggs1_start(source, PHRASE1);
            break;

        case PLAY1_GGS2_START:
            play_ggs2_start(source, PHRASE1);
            break;

        case PLAY1_APB1_START:
            play_apb1_start(source, PHRASE1);
            break;

        case PLAY1_APB2_START:
            play_apb2_start(source, PHRASE1);
            break;

        case PLAY1_RECEIVE_START:
            play_receive_start(source, PHRASE1);
            break;

        // Воспроизведение второй фразы
        case PLAY2_GGS1_START:
            play_ggs1_start(source, PHRASE2);
            break;

        case PLAY2_GGS2_START:
            play_ggs2_start(source, PHRASE2);
            break;

        case PLAY2_APB1_START:
            play_apb1_start(source, PHRASE2);
            break;

        case PLAY2_APB2_START:
            play_apb2_start(source, PHRASE2);
            break;

        case PLAY2_RECEIVE_START:
            play_receive_start(source, PHRASE2);
            break;

        // Завершение воспроизведения
        case PLAY_GGS1_FINISH:
            play_ggs1_end(source);
            break;

        case PLAY_GGS2_FINISH:
            play_ggs2_end(source);
            break;

        case PLAY_APB1_FINISH:
            play_apb1_end(source);
            break;

        case PLAY_APB2_FINISH:
            play_apb2_end(source);
            break;

        case PLAY_RECEIVE_FINISH:
            play_receive_end(source);
            break;

        // Обрабатываются отдельно либо не требуют действий
        case TRK_RELEASED:
        case VOLUME_CHANGE:
        default:
            break;
    }
}

// Advance a mock timer using elapsed milliseconds instead of call counts.
static inline uint32_t advance_elapsed_ms(uint32_t current, uint32_t delta, uint32_t limit)
{
    if (current >= limit || delta >= limit - current)
    {
        return limit;
    }

    return current + delta;
}

static handler_responce_t receive_timeout_handler(uint32_t delta_ms)
{
    app.receive_timeout_elapsed_ms = advance_elapsed_ms(
        app.receive_timeout_elapsed_ms, delta_ms, RECEIVE_TIMEOUT_MS);

    if (app.receive_timeout_elapsed_ms >= RECEIVE_TIMEOUT_MS)
    {
        return HANDLER_FINISHED;
    }

    return HANDLER_PROCESSING;
}

// PROCESSING indicates that a new announcement is due, as in the Python model.
static handler_responce_t auto_announce1_handler(uint32_t delta_ms)
{
    app.announce1_wait_elapsed_ms = advance_elapsed_ms(
        app.announce1_wait_elapsed_ms, delta_ms, ANNOUNCE1_PERIOD_MS);

    return app.announce1_wait_elapsed_ms >= ANNOUNCE1_PERIOD_MS
        ? HANDLER_PROCESSING : HANDLER_FINISHED;
}

static handler_responce_t auto_announce2_handler(uint32_t delta_ms)
{
    app.announce2_wait_elapsed_ms = advance_elapsed_ms(
        app.announce2_wait_elapsed_ms, delta_ms, ANNOUNCE2_PERIOD_MS);

    return app.announce2_wait_elapsed_ms >= ANNOUNCE2_PERIOD_MS
        ? HANDLER_PROCESSING : HANDLER_FINISHED;
}

static handler_responce_t announce1_handler(uint32_t delta_ms)
{
    app.announce1_audio_elapsed_ms = advance_elapsed_ms(
        app.announce1_audio_elapsed_ms, delta_ms, ANNOUNCE1_AUDIO_MS);

    return app.announce1_audio_elapsed_ms >= ANNOUNCE1_AUDIO_MS
        ? HANDLER_FINISHED : HANDLER_PROCESSING;
}

static handler_responce_t announce2_handler(uint32_t delta_ms)
{
    app.announce2_audio_elapsed_ms = advance_elapsed_ms(
        app.announce2_audio_elapsed_ms, delta_ms, ANNOUNCE2_AUDIO_MS);

    return app.announce2_audio_elapsed_ms >= ANNOUNCE2_AUDIO_MS
        ? HANDLER_FINISHED : HANDLER_PROCESSING;
}

static void record1_handler(uint32_t delta_ms)
{
    app.phrases[PHRASE1].record_elapsed_ms = advance_elapsed_ms(
        app.phrases[PHRASE1].record_elapsed_ms, delta_ms, RECORD_MAX_MS);
}

static void record2_handler(uint32_t delta_ms)
{
    app.phrases[PHRASE2].record_elapsed_ms = advance_elapsed_ms(
        app.phrases[PHRASE2].record_elapsed_ms, delta_ms, RECORD_MAX_MS);
}

static handler_responce_t phrase_playback_handler(app_mode_t mode, uint32_t delta_ms)
{
    mode_state_t *state = &app.modes[mode];
    state->elapsed_ms = advance_elapsed_ms(state->elapsed_ms, delta_ms, state->duration_ms);

    return state->elapsed_ms >= state->duration_ms
        ? HANDLER_FINISHED : HANDLER_PROCESSING;
}

// Periodic servicing of the active modes (mock audio, no HAL or codec calls).
static void process_state_logic(void)
{
    static uint32_t last_poll_ms = 0U;
    uint32_t now = uptime_ms;
    uint32_t delta_ms = now - last_poll_ms;

    if (delta_ms < POLL_INTERVAL_MS)
    {
        return;
    }

    last_poll_ms = now;

    if (mode_is_active(RECORD1))
    {
        record1_handler(delta_ms);
    }

    if (mode_is_active(RECORD2))
    {
        record2_handler(delta_ms);
    }

    if (mode_is_active(RECEIVE) &&
        app.modes[RECEIVE].source == OUTSOURCE && app.last_trk)
    {
        if (receive_timeout_handler(delta_ms) == HANDLER_FINISHED)
        {
            process_state_event(RECEIVE_TIMEOUT, OUTSOURCE);
        }
    }

    // Run automation before its audio. Wait timers are paused during playback.
    if (mode_is_active(AUTO_ANNOUNCE1) && !mode_is_active(ANNOUNCE1))
    {
        if (auto_announce1_handler(delta_ms) == HANDLER_PROCESSING)
        {
            process_state_event(ANNOUNCE1_START, OUTSOURCE);
        }
    }

    if (mode_is_active(AUTO_ANNOUNCE2) && !mode_is_active(ANNOUNCE2))
    {
        if (auto_announce2_handler(delta_ms) == HANDLER_PROCESSING)
        {
            process_state_event(ANNOUNCE2_START, OUTSOURCE);
        }
    }

    if (mode_is_active(ANNOUNCE1))
    {
        handler_responce_t result = announce1_handler(delta_ms);

        if (result == HANDLER_FINISHED || result == HANDLER_ERROR)
        {
            if (result == HANDLER_ERROR)
            {
                printf("ERROR ANNOUNCE1: playback stopped\r\n");
            }

            process_state_event(ANNOUNCE1_FINISH, OUTSOURCE);
        }
    }

    if (mode_is_active(ANNOUNCE2))
    {
        handler_responce_t result = announce2_handler(delta_ms);

        if (result == HANDLER_FINISHED || result == HANDLER_ERROR)
        {
            if (result == HANDLER_ERROR)
            {
                printf("ERROR ANNOUNCE2: playback stopped\r\n");
            }

            process_state_event(ANNOUNCE2_FINISH, OUTSOURCE);
        }
    }

    for (uint8_t i = 0; i < PLAYBACK_COUNT; i++)
    {
        app_mode_t mode = playback_modes[i];

        if (mode_is_active(mode))
        {
            handler_responce_t result = phrase_playback_handler(mode, delta_ms);

            if (result == HANDLER_FINISHED || result == HANDLER_ERROR)
            {
                if (result == HANDLER_ERROR)
                {
                    printf("ERROR PLAYBACK: mode=%s\r\n", mode_names[mode]);
                }

                process_state_event(playback_finish_events[i], OUTSOURCE);
            }
        }
    }
}

/* ============================================================================
 * Обработка внешних сигналов
 * ========================================================================== */
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
        process_state_event(ACHTUNG_PRESSED, OUTSOURCE);
    }
    else
    {
        process_state_event(ACHTUNG_RELEASED, OUTSOURCE);
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
        process_state_event(TRK_PRESSED, OUTSOURCE);
    }
    else
    {
        process_state_event(TRK_RELEASED, OUTSOURCE);
    }
}


/* ============================================================================
 * Обработчики режимов
 * ========================================================================== */

static void alert_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(ALERT, source))
    {
        return;
    }

    app.modes[ALERT].source = source;
    app.active_modes |= MODE_MASK(ALERT);

    printf("START ALERT: source=%s\r\n", source_name(source));
}

static void alert_end(source_t source)
{
    if (!mode_is_active(ALERT))
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(ALERT);
    app.modes[ALERT].source = OUTSOURCE;

    printf("END ALERT: source=%s\r\n", source_name(source));

    resume_automation();
}

static void achtung_start(source_t source)
{
    if (source != OUTSOURCE)
    {
        return;
    }

    if (!prepare_mode_start(ACHTUNG, source))
    {
        return;
    }

    app.modes[ACHTUNG].source = source;
    app.active_modes |= MODE_MASK(ACHTUNG);

    printf("START ACHTUNG: source=%s\r\n", source_name(source));
}

static void achtung_end(source_t source)
{
    if (!mode_is_active(ACHTUNG))
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(ACHTUNG);
    app.modes[ACHTUNG].source = OUTSOURCE;

    printf("END ACHTUNG: source=%s\r\n", source_name(source));

    resume_automation();
}

static void apb1_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(APB1, source))
    {
        return;
    }

    app.modes[APB1].source = source;
    app.active_modes |= MODE_MASK(APB1);

    printf("START APB1: source=%s\r\n", source_name(source));
}

static void apb1_end(source_t source)
{
    if (!mode_is_active(APB1) || app.modes[APB1].source != source)
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(APB1);
    app.modes[APB1].source = OUTSOURCE;

    printf("END APB1: source=%s\r\n", source_name(source));
}

static void apb2_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(APB2, source))
    {
        return;
    }

    app.modes[APB2].source = source;
    app.active_modes |= MODE_MASK(APB2);

    printf("START APB2: source=%s\r\n", source_name(source));
}

static void apb2_end(source_t source)
{
    if (!mode_is_active(APB2) || app.modes[APB2].source != source)
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(APB2);
    app.modes[APB2].source = OUTSOURCE;

    printf("END APB2: source=%s\r\n", source_name(source));
}

static void auto_announce1_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(AUTO_ANNOUNCE1, source))
    {
        return;
    }

    app.modes[AUTO_ANNOUNCE1].source = source;
    app.active_modes |= MODE_MASK(AUTO_ANNOUNCE1);

    if (!app.automation_paused)
    {
        app.announce1_wait_elapsed_ms = 0U;
    }

    printf("START AUTO_ANNOUNCE1: source=%s\r\n", source_name(source));

    if (!app.automation_paused)
    {
        announce1_start(OUTSOURCE);
    }
}

static void auto_announce1_end(source_t source)
{
    if (!mode_is_active(AUTO_ANNOUNCE1))
    {
        return;
    }

    announce1_end(source);

    app.active_modes &= ~MODE_MASK(AUTO_ANNOUNCE1);
    app.modes[AUTO_ANNOUNCE1].source = OUTSOURCE;

    if (!app.automation_paused)
    {
        app.announce1_wait_elapsed_ms = 0U;
    }

    printf("END AUTO_ANNOUNCE1: source=%s\r\n", source_name(source));
}

static void announce1_start(source_t source)
{
    if (!mode_is_active(AUTO_ANNOUNCE1) || mode_is_active(ANNOUNCE1))
    {
        return;
    }

    app.announce1_wait_elapsed_ms = 0U;

    if (!prepare_mode_start(ANNOUNCE1, source))
    {
        printf("SKIP ANNOUNCE1: channel busy\r\n");
        return;
    }

    app.modes[ANNOUNCE1].source = source;
    app.announce1_audio_elapsed_ms = 0U;
    app.active_modes |= MODE_MASK(ANNOUNCE1);

    printf("START ANNOUNCE1: source=%s\r\n", source_name(source));
}

static void announce1_end(source_t source)
{
    if (!mode_is_active(ANNOUNCE1))
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(ANNOUNCE1);
    app.modes[ANNOUNCE1].source = OUTSOURCE;
    app.announce1_audio_elapsed_ms = 0U;

    if (!app.automation_paused)
    {
        app.announce1_wait_elapsed_ms = 0U;
    }

    printf("END ANNOUNCE1: source=%s\r\n", source_name(source));
}

static void auto_announce2_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(AUTO_ANNOUNCE2, source))
    {
        return;
    }

    app.modes[AUTO_ANNOUNCE2].source = source;
    app.active_modes |= MODE_MASK(AUTO_ANNOUNCE2);

    if (!app.automation_paused)
    {
        app.announce2_wait_elapsed_ms = 0U;
    }

    printf("START AUTO_ANNOUNCE2: source=%s\r\n", source_name(source));

    if (!app.automation_paused)
    {
        announce2_start(OUTSOURCE);
    }
}

static void auto_announce2_end(source_t source)
{
    if (!mode_is_active(AUTO_ANNOUNCE2))
    {
        return;
    }

    announce2_end(source);

    app.active_modes &= ~MODE_MASK(AUTO_ANNOUNCE2);
    app.modes[AUTO_ANNOUNCE2].source = OUTSOURCE;

    if (!app.automation_paused)
    {
        app.announce2_wait_elapsed_ms = 0U;
    }

    printf("END AUTO_ANNOUNCE2: source=%s\r\n", source_name(source));
}

static void announce2_start(source_t source)
{
    if (!mode_is_active(AUTO_ANNOUNCE2) || mode_is_active(ANNOUNCE2))
    {
        return;
    }

    app.announce2_wait_elapsed_ms = 0U;

    if (!prepare_mode_start(ANNOUNCE2, source))
    {
        printf("SKIP ANNOUNCE2: channel busy\r\n");
        return;
    }

    app.modes[ANNOUNCE2].source = source;
    app.announce2_audio_elapsed_ms = 0U;
    app.active_modes |= MODE_MASK(ANNOUNCE2);

    printf("START ANNOUNCE2: source=%s\r\n", source_name(source));
}

static void announce2_end(source_t source)
{
    if (!mode_is_active(ANNOUNCE2))
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(ANNOUNCE2);
    app.modes[ANNOUNCE2].source = OUTSOURCE;
    app.announce2_audio_elapsed_ms = 0U;

    if (!app.automation_paused)
    {
        app.announce2_wait_elapsed_ms = 0U;
    }

    printf("END ANNOUNCE2: source=%s\r\n", source_name(source));
}

static void phrase_rejected_handler(source_t source, const char *reason)
{
    printf("PHRASE REJECT: %s source=%s\r\n",
           reason, source_name(source));
}

static bool record_start(app_mode_t mode, uint8_t phrase, source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return false;
    }

    for (uint8_t i = 0; i < PLAYBACK_COUNT; i++)
    {
        app_mode_t playback = playback_modes[i];

        if (mode_is_active(playback) &&
            app.modes[playback].phrase == phrase)
        {
            phrase_rejected_handler(source, "phrase_playing");
            return false;
        }
    }

    if (!prepare_mode_start(mode, source))
    {
        phrase_rejected_handler(source, "record_busy");
        return false;
    }

    app.phrases[phrase].record_elapsed_ms = 0U;
    app.phrases[phrase].volume_adjusted = false;

    app.modes[mode].source = source;
    app.active_modes |= MODE_MASK(mode);

    printf("START RECORD%u: source=%s\r\n",
           (unsigned)(phrase + 1U), source_name(source));

    return true;
}

static void record_end(app_mode_t mode, uint8_t phrase, source_t source, bool save)
{
    if (!mode_is_active(mode) || app.modes[mode].source != source)
    {
        return;
    }

    phrase_state_t *data = &app.phrases[phrase];

    if (save && !data->volume_adjusted &&
        data->record_elapsed_ms >= RECORD_MIN_MS)
    {
        data->duration_ms = data->record_elapsed_ms;

        printf("SAVE PHRASE %u: duration=%lu ms\r\n",
               (unsigned)(phrase + 1U),
               (unsigned long)data->duration_ms);
    }
    else
    {
        printf("DISCARD PHRASE %u: previous duration=%lu ms\r\n",
               (unsigned)(phrase + 1U),
               (unsigned long)data->duration_ms);
    }

    data->record_elapsed_ms = 0U;

    app.active_modes &= ~MODE_MASK(mode);
    app.modes[mode].source = OUTSOURCE;

    printf("END RECORD%u: source=%s\r\n",
           (unsigned)(phrase + 1U), source_name(source));
}
static bool record1_start(source_t source)
{
    return record_start(RECORD1, PHRASE1, source);
}

static void record1_end(source_t source, bool save)
{
    record_end(RECORD1, PHRASE1, source, save);
}

static bool record2_start(source_t source)
{
    return record_start(RECORD2, PHRASE2, source);
}

static void record2_end(source_t source, bool save)
{
    record_end(RECORD2, PHRASE2, source, save);
}

static bool playback_start(app_mode_t mode, int8_t phrase, source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return false;
    }

    if (phrase < 0 || phrase >= (int8_t)PHRASE_COUNT)
    {
        phrase_rejected_handler(source, "invalid_phrase");
        return false;
    }

    app_mode_t recording = (phrase == PHRASE1) ? RECORD1 : RECORD2;

    if (mode_is_active(recording))
    {
        phrase_rejected_handler(source, "phrase_recording");
        return false;
    }

    if (app.phrases[phrase].duration_ms < RECORD_MIN_MS)
    {
        phrase_rejected_handler(source, "empty_phrase");
        return false;
    }

    // Playback never displaces an occupied channel
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app_mode_t active = (app_mode_t)i;

        if (mode_is_active(active) && !modes_are_compatible(mode, active))
        {
            phrase_rejected_handler(source, "channel_busy");
            return false;
        }
    }

    if (!prepare_mode_start(mode, source))
    {
        phrase_rejected_handler(source, "channel_busy");
        return false;
    }

    app.modes[mode].source = source;
    app.modes[mode].phrase = phrase;
    app.modes[mode].duration_ms = app.phrases[phrase].duration_ms;
    app.modes[mode].elapsed_ms = 0U;

    app.active_modes |= MODE_MASK(mode);

    printf("START PLAYBACK: mode=%s phrase=%u duration=%lu ms source=%s\r\n",
           mode_names[mode],
           (unsigned)(phrase + 1),
           (unsigned long)app.modes[mode].duration_ms,
           source_name(source));

    return true;
}

static void playback_end(app_mode_t mode, source_t source)
{
    if (!mode_is_active(mode))
    {
        return;
    }

    app.active_modes &= ~MODE_MASK(mode);

    app.modes[mode].source = OUTSOURCE;
    app.modes[mode].phrase = NO_PHRASE;
    app.modes[mode].duration_ms = 0U;
    app.modes[mode].elapsed_ms = 0U;

    printf("END PLAYBACK: mode=%s source=%s\r\n",
           mode_names[mode], source_name(source));
}
static bool play_ggs1_start(source_t source, int8_t phrase)
{
    return playback_start(PLAY_GGS1, phrase, source);
}

static void play_ggs1_end(source_t source)
{
    playback_end(PLAY_GGS1, source);
}

static bool play_ggs2_start(source_t source, int8_t phrase)
{
    return playback_start(PLAY_GGS2, phrase, source);
}

static void play_ggs2_end(source_t source)
{
    playback_end(PLAY_GGS2, source);
}

static bool play_apb1_start(source_t source, int8_t phrase)
{
    return playback_start(PLAY_APB1, phrase, source);
}

static void play_apb1_end(source_t source)
{
    playback_end(PLAY_APB1, source);
}

static bool play_apb2_start(source_t source, int8_t phrase)
{
    return playback_start(PLAY_APB2, phrase, source);
}

static void play_apb2_end(source_t source)
{
    playback_end(PLAY_APB2, source);
}

static bool play_receive_start(source_t source, int8_t phrase)
{
    return playback_start(PLAY_RECEIVE, phrase, source);
}

static void play_receive_end(source_t source)
{
    playback_end(PLAY_RECEIVE, source);
}
/* ============================================================================
 * Обработчик системного таймера
 * ========================================================================== */
void SysTick_Handler(void)
{
    uptime_ms++;
}