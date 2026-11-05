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
#define MODE_COUNT                  3U

#define RECEIVE_PRIORITY            1U
#define GGS1_PRIORITY               2U
#define GGS2_PRIORITY               2U

#define RECEIVE_COMPATIBILITY       0U
#define GGS1_COMPATIBILITY          (1U << GGS2)
#define GGS2_COMPATIBILITY          (1U << GGS1)

#define INITIAL_ACTIVE_MODES        0U
#define AUTO_RECEIVE_DEFAULT        false

#define ACHTUNG_INITIAL_LEVEL       true
#define TRK_INITIAL_LEVEL           true

#define POLL_INTERVAL_MS        10U
#define RECEIVE_TIMEOUT_MS      500U

#define PRM_COMBO_BUTTON_COUNT      1U
#define GGS_SINGLE_BUTTON_COUNT     1U
#define GGS_PAIR_BUTTON_COUNT       2U

typedef enum
{
    RECEIVE = 0,
    GGS1,
    GGS2
} app_mode_t;

typedef struct
{
    source_t source;
} mode_state_t;

typedef struct
{
    uint8_t active_modes;
    mode_state_t modes[MODE_COUNT];

    bool auto_receive;

    bool last_achtung;
    bool last_trk;
} app_runtime_t;

// таблицы приоритетов и совместимости
static const uint8_t mode_priorities[MODE_COUNT] = {
    RECEIVE_PRIORITY,
    GGS1_PRIORITY,
    GGS2_PRIORITY
};

static const uint8_t mode_compatibility[MODE_COUNT] = {
    RECEIVE_COMPATIBILITY,
    GGS1_COMPATIBILITY,
    GGS2_COMPATIBILITY
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

    .modes = {
        [RECEIVE] = {.source = OUTSOURCE},
        [GGS1] = {.source = OUTSOURCE},
        [GGS2] = {.source = OUTSOURCE}
    },

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
// Проверка активности режима
static inline bool mode_is_active(app_mode_t mode)
{
    return (app.active_modes & (1U << mode)) != 0U;
}
// Проверка совместимости двух режимов
static inline bool modes_are_compatible(app_mode_t first, app_mode_t second)
{
    return ((mode_compatibility[first] & (1U << second)) != 0U &&
            (mode_compatibility[second] & (1U << first)) != 0U);
}
// Завершение приёма
static void receive_end(source_t source)
{
    if (!mode_is_active(RECEIVE))
    {
        return;
    }

    printf("END RECEIVE: источник события = %s, источник приёма = %s\r\n",
           source_name(source), source_name(app.modes[RECEIVE].source));

    app.active_modes &= ~(1U << RECEIVE);
    app.modes[RECEIVE].source = OUTSOURCE;
}

// Завершение GGS1
static void ggs1_end(source_t source)
{
    if (!mode_is_active(GGS1) || app.modes[GGS1].source != source)
    {
        return;
    }

    printf("END GGS1: источник = %s\r\n", source_name(source));

    app.active_modes &= ~(1U << GGS1);
    app.modes[GGS1].source = OUTSOURCE;
}

// Завершение GGS2
static void ggs2_end(source_t source)
{
    if (!mode_is_active(GGS2) || app.modes[GGS2].source != source)
    {
        return;
    }

    printf("END GGS2: источник = %s\r\n", source_name(source));

    app.active_modes &= ~(1U << GGS2);
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

        default:
            break;
    }
}

// Проверка возможности запуска режима и вытеснение несовместимых
static bool prepare_mode_start(app_mode_t mode)
{
    if (mode_is_active(mode))
    {
        return false;
    }

    // Сначала проверяем все конфликты
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

    // После успешной проверки завершаем несовместимые режимы
    for (uint8_t i = 0; i < MODE_COUNT; i++)
    {
        app_mode_t active = (app_mode_t)i;

        if (mode_is_active(active) && !modes_are_compatible(mode, active))
        {
            end_mode(active, app.modes[active].source);
        }
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

    if (!prepare_mode_start(RECEIVE))
    {
        return;
    }

    app.modes[RECEIVE].source = source;
    app.active_modes |= 1U << RECEIVE;

    printf("START RECEIVE: источник = %s\r\n", source_name(source));
}

// Запуск GGS1
static void ggs1_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(GGS1))
    {
        return;
    }

    app.modes[GGS1].source = source;
    app.active_modes |= 1U << GGS1;

    printf("START GGS1: источник = %s\r\n", source_name(source));
}

// Запуск GGS2
static void ggs2_start(source_t source)
{
    if (source != REMOTE_1 && source != REMOTE_2)
    {
        return;
    }

    if (!prepare_mode_start(GGS2))
    {
        return;
    }

    app.modes[GGS2].source = source;
    app.active_modes |= 1U << GGS2;

    printf("START GGS2: источник = %s\r\n", source_name(source));
}

// Включение автоматического приёма
static void auto_receive_start(source_t source)
{
    app.auto_receive = true;

    printf("IDLE: автоматический приём включён, источник = %s\r\n",
           source_name(source));
}

// Выключение автоматического приёма
static void auto_receive_end(source_t source)
{
    app.auto_receive = false;

    printf("IDLE: автоматический приём выключен, источник = %s\r\n",
           source_name(source));
}

// Преобразование комбинации в событие
void flush_combo(FlushContext context)
{
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
}
// Диспетчер событий машины состояний
static void process_state_event(app_event_t event, source_t source)
{
    switch (event)
    {
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

        case ACHTUNG_PRESSED:
        case ACHTUNG_RELEASED:
        case TRK_RELEASED:
        default:
            break;
    }
}

// Переодическое обслуживание текущего состояния
static void process_state_logic(void)
{

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
 * Обработчик системного таймера
 * ========================================================================== */

void SysTick_Handler(void)
{
    uptime_ms++;
}