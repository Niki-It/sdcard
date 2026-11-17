#include "main.h"
#include "string.h"

#define DEBOUNCE_PERIOD_MS          1U
#define ACHTUNG_DEBOUNCE_THRESHOLD  5U
#define TRK_DEBOUNCE_THRESHOLD      15U
#define BUTTON_COMBO_TIMEOUT_MS     150U
#define ACHTUNG_INITIAL_LEVEL       true
#define TRK_INITIAL_LEVEL           true
#define INITIAL_ACTIVE_MODES        0U
#define ALERT_FILENAME              "alarm.mp3"
#define ACHTUNG_FILENAME            "fire.mp3"

/* Один условный цикл Python заменён одной секундой реального времени. */
#define RECEIVE_TIMEOUT_MS          3000U
#define UNLOCK_RECORD_TIMEOUT_MS    15000U
#define RECORD_1_FILENAME           "frase1.wav"
#define RECORD_2_FILENAME           "frase2.wav"
#define PHRASE_1_FILENAME           RECORD_1_FILENAME
#define PHRASE_2_FILENAME           RECORD_2_FILENAME
#define RECORD_1_TEMP_FILENAME      "FR1TMP.WAV"
#define RECORD_2_TEMP_FILENAME      "FR2TMP.WAV"
#define RECORD_1_BACKUP_FILENAME    "FR1BAK.WAV"
#define RECORD_2_BACKUP_FILENAME    "FR2BAK.WAV"

#define CHANNEL_1                   (1U << 0)
#define CHANNEL_2                   (1U << 1)
#define CHANNEL_PRM                 (1U << 2)
#define ANNOUNCE_CHANNELS           (CHANNEL_1 | CHANNEL_2)

/* CHANNEL_1 = ГГС1, CHANNEL_2 = ГГС2, оба канала = CHANNEL_1 | CHANNEL_2. */
#define ALERT_CHANNELS              (CHANNEL_1 | CHANNEL_2)
#define ACHTUNG_CHANNELS            (CHANNEL_1 | CHANNEL_2)

#if (ALERT_CHANNELS == 0U) || ((ALERT_CHANNELS & ~(CHANNEL_1 | CHANNEL_2)) != 0U)
#error "ALERT_CHANNELS: select CHANNEL_1, CHANNEL_2 or both"
#endif

#if (ACHTUNG_CHANNELS == 0U) || ((ACHTUNG_CHANNELS & ~(CHANNEL_1 | CHANNEL_2)) != 0U)
#error "ACHTUNG_CHANNELS: select CHANNEL_1, CHANNEL_2 or both"
#endif

/* При старте: отказ при занятом канале или воспроизведение на свободных. */
#define ANNOUNCE_START_REJECT_ALL       0U
#define ANNOUNCE_START_USE_FREE         1U
#ifndef ANNOUNCE_START_POLICY
#define ANNOUNCE_START_POLICY           ANNOUNCE_START_REJECT_ALL
#endif

/* При занятии канала ГГС/АПБ/фразой: остановить всё или освободить только канал. */
#define ANNOUNCE_INTERRUPT_STOP_ALL     0U
#define ANNOUNCE_INTERRUPT_DROP_CHANNEL 1U
#ifndef ANNOUNCE_INTERRUPT_POLICY
#define ANNOUNCE_INTERRUPT_POLICY       ANNOUNCE_INTERRUPT_DROP_CHANNEL
#endif

/* Порядок для ANNOUNCE3: чередовать папки или проигрывать их по очереди. */
#ifndef ANNOUNCE3_ORDER
#define ANNOUNCE3_ORDER                 ANNOUNCE_ORDER_ALTERNATE
#endif

/* Ручной ANNOUNCE: остановка после последнего файла или повтор до отпускания. */
#define ANNOUNCE_MANUAL_STOP_AT_END      0U
#define ANNOUNCE_MANUAL_LOOP             1U
#ifndef ANNOUNCE_MANUAL_END_POLICY
#define ANNOUNCE_MANUAL_END_POLICY       ANNOUNCE_MANUAL_LOOP
#endif

#if ANNOUNCE_MANUAL_END_POLICY != ANNOUNCE_MANUAL_STOP_AT_END && ANNOUNCE_MANUAL_END_POLICY != ANNOUNCE_MANUAL_LOOP
#error "Invalid ANNOUNCE_MANUAL_END_POLICY"
#endif

#if ANNOUNCE_START_POLICY != ANNOUNCE_START_REJECT_ALL && ANNOUNCE_START_POLICY != ANNOUNCE_START_USE_FREE
#error "Invalid ANNOUNCE_START_POLICY"
#endif

#if ANNOUNCE_INTERRUPT_POLICY != ANNOUNCE_INTERRUPT_STOP_ALL && ANNOUNCE_INTERRUPT_POLICY != ANNOUNCE_INTERRUPT_DROP_CHANNEL
#error "Invalid ANNOUNCE_INTERRUPT_POLICY"
#endif

#define MODE_MASK(mode)             (UINT32_C(1) << (mode))
#define NO_BUTTON                   (-1)
#define NO_OUTPUT                   (-1)
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
    uint8_t hardware;   /* Код ослабления AIC3104, не проценты. */
    uint16_t software;  /* Счётчик допустимых шагов, шаг 5. */
} volume_state_t;

/* Логические линии не зависят от нумерации отдельных микросхем. */
typedef enum
{
    TRANSMISSION_LINE_1,
    TRANSMISSION_LINE_2,
    TRANSMISSION_LINE_COUNT
} transmission_line_t;

typedef struct
{
    uint8_t mux_number;
    uint8_t relay_number;
    uint8_t amplifier_channel;
    uint8_t channel_mask;
    void (*start_bypass)(uint8_t hardware);
    void (*stop_bypass)(void);
    void (*start_i2s)(uint8_t hardware);
    void (*stop_i2s)(void);
} transmission_line_config_t;

static const transmission_line_config_t transmission_lines[TRANSMISSION_LINE_COUNT] =
{
    [TRANSMISSION_LINE_1] =
    {
        .mux_number = 1U,
        .relay_number = 0U,
        .amplifier_channel = 0U,
        .channel_mask = CHANNEL_1,
        .start_bypass = aic3104_line1rp_hprout_bypass,
        .stop_bypass = aic3104_line1rp_hprout_bypass_stop,
        .start_i2s = aic3104_i2s_hprout,
        .stop_i2s = aic3104_i2s_hprout_stop
    },
    [TRANSMISSION_LINE_2] =
    {
        .mux_number = 2U,
        .relay_number = 1U,
        .amplifier_channel = 1U,
        .channel_mask = CHANNEL_2,
        .start_bypass = aic3104_line1lp_hplout_bypass,
        .stop_bypass = aic3104_line1lp_hplout_bypass_stop,
        .start_i2s = aic3104_i2s_hplout,
        .stop_i2s = aic3104_i2s_hplout_stop
    }
};

typedef struct
{
    source_t source;
    bool is_holding;
} mode_params_t;

typedef struct
{
    const char *filename;
    const char *temporary;
    const char *backup;
} record_file_paths_t;

static const record_file_paths_t record_files[2] =
{
    {RECORD_1_FILENAME, RECORD_1_TEMP_FILENAME, RECORD_1_BACKUP_FILENAME},
    {RECORD_2_FILENAME, RECORD_2_TEMP_FILENAME, RECORD_2_BACKUP_FILENAME}
};

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
    announce_sequence_t auto_announce_sequence;
    announce_sequence_t manual_announce_sequence;
    audio_file_t auto_announce_file;
    audio_file_t manual_announce_file;
    bool auto_announce_file_ready;
    bool auto_announce_waiting;
    uint32_t auto_announce_wait_started_ms;
    uint32_t auto_announce_wait_ms;
    bool manual_announce_waiting;
    uint32_t manual_announce_wait_started_ms;
    uint32_t manual_announce_wait_ms;
    uint8_t announce_channels;
    bool announce_audio_running;
    int announce_button;
    // режим фразы
    uint8_t phrase_number;
    int phrase_output;
    uint8_t phrase_channel;
    // режим записи
    bool unlock_record;
    uint32_t unlock_record_started_ms;
    int record_button;
    bool record_invalid;
    bool record_storage_locked;
    // громкость
    volume_state_t volume[VOLUME_COUNT];
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
                 MODE_MASK(APB1) | MODE_MASK(APB2),
    [PHRASE] = MODE_MASK(RECEIVE) | MODE_MASK(GGS1) | MODE_MASK(GGS2) |
               MODE_MASK(APB1) | MODE_MASK(APB2),
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
static void record_recover_files(void);

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

}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    app_runtime_init();
    button_combo_init(BUTTON_COMBO_TIMEOUT_MS);
    periph_init();
    fs_unit(true);
    record_recover_files();
    tusb_init();

    if (app.record_storage_locked)
    {
        tud_disconnect();
    }
    aic3104_init();
    aic3104_init_clocking();
    for (unsigned i = 0; i < VOLUME_COUNT; ++i)
    {
        app.volume[i].hardware = aic3104_get_min_volume();
        app.volume[i].software = 0U;
    }
    printf("%d", aic3104_read_reg(101));
    i2s_dma_init();
    debounce_init(debounced_pins, DEBOUNCE_INPUT_COUNT, DEBOUNCE_PERIOD_MS);

    for (unsigned line = 0U; line < TRANSMISSION_LINE_COUNT; ++line)
    {
        mute_amplifier(transmission_lines[line].amplifier_channel);
    }

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

static const transmission_line_config_t *transmission_line_for_mode(app_mode_t mode)
{
    switch (mode)
    {
        case GGS1:
        case APB1:
            return &transmission_lines[TRANSMISSION_LINE_1];

        case GGS2:
        case APB2:
            return &transmission_lines[TRANSMISSION_LINE_2];

        default:
            return NULL;
    }
}

static relay_port_t transmission_relay_for_mode(app_mode_t mode)
{
    return mode == GGS1 || mode == GGS2 ? GGS : APB;
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

/* Только действия входа/выхода. Условия допуска находятся в диспетчере. */
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

static bool playback_line_enabled(const transmission_line_config_t *line, uint8_t channels)
{
    return (channels & line->channel_mask) != 0U;
}

static void playback_set_amplifiers(uint8_t channels, bool enabled)
{
    for (unsigned i = 0U; i < TRANSMISSION_LINE_COUNT; ++i)
    {
        const transmission_line_config_t *line = &transmission_lines[i];

        if (playback_line_enabled(line, channels))
        {
            if (enabled)
            {
                unmute_amplifiers(line->amplifier_channel);
            }
            else
            {
                mute_amplifier(line->amplifier_channel);
            }
        }
    }
}

static void playback_set_i2s(uint8_t channels, bool enabled, uint8_t hardware)
{
    for (unsigned i = 0U; i < TRANSMISSION_LINE_COUNT; ++i)
    {
        const transmission_line_config_t *line = &transmission_lines[i];

        if (playback_line_enabled(line, channels))
        {
            if (enabled)
            {
                line->start_i2s(hardware);
            }
            else
            {
                line->stop_i2s();
            }
        }
    }
}

static FRESULT playback_start_file(const char *filename, uint8_t channels)
{
    playback_set_amplifiers(channels, false);

    aic3104_soft_reset();
    aic3104_init_clocking();
    playback_set_i2s(channels, true, aic3104_get_min_volume());

    FRESULT result = audio_playback_start(filename);

    if (result != FR_OK)
    {
        playback_set_i2s(channels, false, aic3104_get_min_volume());
        return result;
    }

    playback_set_amplifiers(channels, true);
    printf("PLAYBACK START: %s\r\n", filename);

    return FR_OK;
}

static void playback_restore_usb(void)
{
    if (audio_playback_is_file_open())
    {
        app.record_storage_locked = true;
        printf("PLAYBACK: file still open, USB remains disconnected\r\n");
        return;
    }

    if (!app.record_storage_locked) tud_connect();
}

static void playback_start_mode(
    app_mode_t mode,
    source_t source,
    uint8_t channels,
    const char *filename
)
{
    if (app.record_storage_locked)
    {
        printf("%s PLAYBACK BLOCKED: SD file is still open\r\n", mode_names[mode]);
        return;
    }

    tud_disconnect();

    for (unsigned i = 0U; i < TRANSMISSION_LINE_COUNT; ++i)
    {
        const transmission_line_config_t *line = &transmission_lines[i];

        if (playback_line_enabled(line, channels))
        {
            swich_relay(line->relay_number, GGS);
        }
    }

    FRESULT result = playback_start_file(filename, channels);

    if (result != FR_OK)
    {
        printf("%s PLAYBACK ERROR: %d\r\n", mode_names[mode], (int)result);
        if (audio_playback_is_file_open()) audio_playback_stop();
        playback_restore_usb();
        return;
    }

    activate_mode(mode, source, false);
}

static void playback_end_mode(app_mode_t mode, uint8_t channels)
{
    playback_set_amplifiers(channels, false);

    if (audio_playback_is_active() || audio_playback_is_file_open())
    {
        FRESULT result = audio_playback_stop();

        if (result != FR_OK)
        {
            printf("%s STOP ERROR: %d\r\n", mode_names[mode], (int)result);
        }
    }

    playback_set_i2s(channels, false, aic3104_get_min_volume());
    deactivate_mode(mode);
    playback_restore_usb();
}

static void start_alert(source_t source)
{
    playback_start_mode(ALERT, source, ALERT_CHANNELS, ALERT_FILENAME);
}

static void end_alert(void)
{
    playback_end_mode(ALERT, ALERT_CHANNELS);
}

static void start_achtung(void)
{
    playback_start_mode(ACHTUNG, OUTSOURCE, ACHTUNG_CHANNELS, ACHTUNG_FILENAME);
}

static void end_achtung(void)
{
    playback_end_mode(ACHTUNG, ACHTUNG_CHANNELS);
}

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
static volume_target_t transmission_volume_target(app_mode_t mode)
{
    switch (mode)
    {
        case GGS1: return VOLUME_GGS1;
        case GGS2: return VOLUME_GGS2;
        case APB1: return VOLUME_APB1;
        default: return VOLUME_APB2; /* Вызывается только для ГГС/АПБ. */
    }
}

static void start_transmission(app_mode_t mode, source_t source)
{
    const transmission_line_config_t *line = transmission_line_for_mode(mode);

    mux_channel_t input = source == REMOTE_1 ? MUX_CHANNEL_X0 : MUX_CHANNEL_X1;

    mux_select(line->mux_number, input);
    swich_relay(line->relay_number, transmission_relay_for_mode(mode));
    line->start_bypass(app.volume[transmission_volume_target(mode)].hardware);
    unmute_amplifiers(line->amplifier_channel);

    activate_mode(mode, source, true);
}

static void end_transmission(app_mode_t mode)
{
    const transmission_line_config_t *line = transmission_line_for_mode(mode);

    line->stop_bypass();
    mute_amplifier(line->amplifier_channel);

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
static void reset_auto_announce_sequence(void)
{
    announce_folder_t folder = ANNOUNCE_FOLDER_BOTH;

    if (app.auto_announce_1 && !app.auto_announce_2) folder = ANNOUNCE_FOLDER_1;
    if (!app.auto_announce_1 && app.auto_announce_2) folder = ANNOUNCE_FOLDER_2;

    announce_sequence_reset(&app.auto_announce_sequence, folder, ANNOUNCE3_ORDER, true);
    app.auto_announce_file_ready = false;
    app.auto_announce_waiting = false;
    app.auto_announce_wait_started_ms = 0U;
    app.auto_announce_wait_ms = 0U;
}

static void start_auto_announce_delay(void)
{
    app.auto_announce_file_ready = false;
    app.auto_announce_waiting = true;
    app.auto_announce_wait_started_ms = uptime_ms;
    app.auto_announce_wait_ms = (uint32_t)app.auto_announce_file.delay * 1000U;
}

static void stop_announce_audio(void)
{
    playback_set_amplifiers(app.announce_channels, false);

    if (audio_playback_is_active() || audio_playback_is_file_open())
    {
        FRESULT result = audio_playback_stop();

        if (result != FR_OK)
        {
            printf("ANNOUNCE STOP ERROR: %d\r\n", (int)result);
        }
    }

    playback_set_i2s(app.announce_channels, false, aic3104_get_min_volume());
    app.announce_audio_running = false;
}

static FRESULT play_announce_file(const audio_file_t *file, uint8_t channels)
{
    playback_set_amplifiers(channels, false);

    for (unsigned i = 0U; i < TRANSMISSION_LINE_COUNT; ++i)
    {
        const transmission_line_config_t *line = &transmission_lines[i];

        if (playback_line_enabled(line, channels))
        {
            swich_relay(line->relay_number, GGS);
        }
    }

    /* Не сбрасываем кодек: соседний канал может передавать звук через bypass. */
    playback_set_i2s(channels, true, app.volume[VOLUME_ANNOUNCE].hardware);

    FRESULT result = audio_playback_start(file->pathfile);

    if (result != FR_OK)
    {
        if (audio_playback_is_active() || audio_playback_is_file_open())
        {
            FRESULT stop_result = audio_playback_stop();

            if (stop_result != FR_OK)
            {
                printf("ANNOUNCE STOP ERROR: %d\r\n", (int)stop_result);
            }
        }

        playback_set_i2s(channels, false, aic3104_get_min_volume());
        printf("ANNOUNCE PLAYBACK ERROR: %s (%d)\r\n", file->pathfile, (int)result);
        return result;
    }

    app.announce_audio_running = true;
    playback_set_amplifiers(channels, true);
    printf("ANNOUNCE PLAYBACK START: %s\r\n", file->pathfile);
    return FR_OK;
}

static bool start_announce(uint8_t channels, source_t source, int button, const audio_file_t *file)
{
    if (app.record_storage_locked) return false;

    tud_disconnect();

    if (play_announce_file(file, channels) != FR_OK)
    {
        playback_restore_usb();
        return false;
    }

    app.announce_button = button;
    app.announce_channels = channels;
    app.manual_announce_waiting = false;
    activate_mode(ANNOUNCE, source, source != OUTSOURCE);
    return true;
}

static void end_announce(void)
{
    bool automatic = app.modes[ANNOUNCE].source == OUTSOURCE;

    stop_announce_audio();
    deactivate_mode(ANNOUNCE);
    app.announce_channels = 0U;
    app.announce_button = NO_BUTTON;
    app.manual_announce_waiting = false;
    playback_restore_usb();

    if (automatic && (app.auto_announce_1 || app.auto_announce_2))
    {
        start_auto_announce_delay();
    }
}

static void set_announce_channels(uint8_t channels)
{
    uint8_t removed = (uint8_t)(app.announce_channels & (uint8_t)~channels);

    playback_set_amplifiers(removed, false);

    if (app.announce_audio_running)
    {
        playback_set_i2s(removed, false, aic3104_get_min_volume());
    }

    /* Декодирование и воспроизведение на оставшихся каналах не прерываются. */
    app.announce_channels = channels;
    printf("ANNOUNCE: channels=%u\r\n", (unsigned)channels);
}
static bool start_phrase(source_t source, uint8_t number, app_mode_t output, uint8_t channel)
{
    if (app.record_storage_locked) return false;

    const transmission_line_config_t *line = transmission_line_for_mode(output);

    if (line == NULL || line->channel_mask != channel) return false;

    const char *filename = number == 1U ? PHRASE_1_FILENAME : PHRASE_2_FILENAME;

    tud_disconnect();
    mute_amplifier(line->amplifier_channel);
    swich_relay(line->relay_number, transmission_relay_for_mode(output));
    line->start_i2s(app.volume[number == 1U ? VOLUME_PHRASE_1 : VOLUME_PHRASE_2].hardware);

    FRESULT result = audio_playback_start(filename);

    if (result != FR_OK)
    {
        printf("PHRASE START ERROR: %s (%d)\r\n", filename, (int)result);
        if (audio_playback_is_file_open()) audio_playback_stop();
        line->stop_i2s();
        playback_restore_usb();
        return false;
    }

    unmute_amplifiers(line->amplifier_channel);
    app.phrase_number = number;
    app.phrase_output = (int)output;
    app.phrase_channel = channel;
    activate_mode(PHRASE, source, false);
    printf("PHRASE START: %s, output=%s\r\n", filename, mode_names[output]);
    return true;
}
static void end_phrase(void)
{
    if (app.phrase_output != NO_OUTPUT)
    {
        const transmission_line_config_t *line = transmission_line_for_mode((app_mode_t)app.phrase_output);

        if (line != NULL)
        {
            mute_amplifier(line->amplifier_channel);

            FRESULT result = audio_playback_stop();

            if (result != FR_OK)
            {
                printf("PHRASE STOP ERROR: %d\r\n", (int)result);
            }

            line->stop_i2s();
            playback_restore_usb();
        }
    }

    deactivate_mode(PHRASE);
    app.phrase_number = 0U;
    app.phrase_output = NO_OUTPUT;
    app.phrase_channel = 0U;
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

static const record_file_paths_t *record_paths_for_button(int button)
{
    if (button == B_F1) return &record_files[0];
    if (button == B_F2) return &record_files[1];
    return NULL;
}

static FRESULT record_file_exists(const char *filename, bool *exists)
{
    FILINFO info;
    FRESULT result = f_stat(filename, &info);

    if (result == FR_NO_FILE)
    {
        *exists = false;
        return FR_OK;
    }

    if (result != FR_OK) return result;
    if ((info.fattrib & AM_DIR) != 0U) return FR_DENIED;

    *exists = true;
    return FR_OK;
}

static FRESULT record_discard_file(const record_file_paths_t *paths)
{
    bool exists = false;
    FRESULT result = record_file_exists(paths->temporary, &exists);

    if (result != FR_OK || !exists) return result;

    return f_unlink(paths->temporary);
}

static FRESULT record_file_complete(const char *filename, bool *complete)
{
    *complete = false;

    FILINFO info;
    FRESULT result = f_stat(filename, &info);

    if (result != FR_OK) return result;
    if (info.fsize <= WAV_PCM_HEADER_BYTES) return FR_OK;

    FIL file;
    result = f_open(&file, filename, FA_READ);

    if (result != FR_OK) return result;

    uint8_t header[WAV_PCM_HEADER_BYTES];
    UINT bytes_read = 0U;
    result = f_read(&file, header, sizeof(header), &bytes_read);

    FRESULT close_result = f_close(&file);

    if (result == FR_OK) result = close_result;
    if (result != FR_OK) return result;
    if (bytes_read != sizeof(header)) return FR_OK;

    if (memcmp(&header[0], "RIFF", 4U) != 0 ||
        memcmp(&header[8], "WAVE", 4U) != 0 ||
        memcmp(&header[12], "fmt ", 4U) != 0 ||
        memcmp(&header[36], "data", 4U) != 0) return FR_OK;

    uint32_t data_bytes = (uint32_t)header[40] |
                          ((uint32_t)header[41] << 8U) |
                          ((uint32_t)header[42] << 16U) |
                          ((uint32_t)header[43] << 24U);

    *complete = data_bytes > 0U && (FSIZE_t)data_bytes <= info.fsize - WAV_PCM_HEADER_BYTES;
    return FR_OK;
}

static FRESULT record_recover_file(const record_file_paths_t *paths)
{
    bool backup_exists = false;
    bool original_exists = false;
    FRESULT result = record_file_exists(paths->backup, &backup_exists);

    if (result != FR_OK) return result;

    if (backup_exists)
    {
        result = record_file_exists(paths->filename, &original_exists);

        if (result != FR_OK) return result;

        if (!original_exists)
        {
            result = f_rename(paths->backup, paths->filename);
        }
        else
        {
            bool complete = false;
            result = record_file_complete(paths->filename, &complete);

            if (result != FR_OK) return result;

            if (!complete)
            {
                result = f_unlink(paths->filename);

                if (result == FR_OK)
                {
                    result = f_rename(paths->backup, paths->filename);
                }
            }
            else
            {
                result = f_unlink(paths->backup);
            }
        }

        if (result != FR_OK) return result;

        printf("RECORD RECOVERY: %s\r\n", paths->filename);
    }

    return record_discard_file(paths);
}

static void record_recover_files(void)
{
    for (unsigned i = 0U; i < 2U; ++i)
    {
        FRESULT result = record_recover_file(&record_files[i]);

        if (result != FR_OK)
        {
            app.record_storage_locked = true;
            printf("RECORD RECOVERY ERROR: %s (%d)\r\n", record_files[i].filename, (int)result);
        }
    }
}

static FRESULT record_commit_file(const record_file_paths_t *paths)
{
    bool original_exists = false;
    bool backup_exists = false;
    bool complete = false;
    FRESULT result = record_file_complete(paths->temporary, &complete);

    if (result != FR_OK) return result;
    if (!complete) return FR_DENIED;

    result = record_file_exists(paths->filename, &original_exists);

    if (result != FR_OK) return result;

    result = record_file_exists(paths->backup, &backup_exists);

    if (result != FR_OK) return result;
    if (backup_exists) return FR_EXIST;

    if (original_exists)
    {
        result = f_rename(paths->filename, paths->backup);

        if (result != FR_OK) return result;
    }

    result = f_rename(paths->temporary, paths->filename);

    if (result != FR_OK)
    {
        if (original_exists)
        {
            FRESULT rollback = f_rename(paths->backup, paths->filename);

            if (rollback != FR_OK)
            {
                printf("RECORD ROLLBACK ERROR: %d\r\n", (int)rollback);
            }
        }

        return result;
    }

    if (original_exists)
    {
        FRESULT cleanup = f_unlink(paths->backup);

        if (cleanup != FR_OK)
        {
            printf("RECORD BACKUP CLEANUP ERROR: %d\r\n", (int)cleanup);
        }
    }

    return FR_OK;
}

static bool start_record(source_t source, int button)
{
    const record_file_paths_t *paths = record_paths_for_button(button);

    if (paths == NULL || app.record_storage_locked) return false;

    tud_disconnect();

    FRESULT result = record_recover_file(paths);
    bool recovered = result == FR_OK;

    if (result == FR_OK)
    {
        mux_channel_t input = source == REMOTE_1 ? MUX_CHANNEL_X0 : MUX_CHANNEL_X1;

        playback_set_amplifiers(CHANNEL_1 | CHANNEL_2, false);
        mux_select(2U, input);
        aic3104_soft_reset();
        aic3104_init_clocking();
        aic3104_line1lp_i2s();

        result = wav_recording_start(paths->temporary);
    }

    app.unlock_record = false;
    app.unlock_record_started_ms = 0U;

    if (result != FR_OK)
    {
        printf("RECORD START ERROR: %s (%d)\r\n", paths->temporary, (int)result);

        if (!recovered || wav_recording_is_open())
        {
            app.record_storage_locked = true;
            printf("RECORD: SD state unresolved, USB remains disconnected\r\n");
        }
        else
        {
            FRESULT cleanup = record_discard_file(paths);

            if (cleanup != FR_OK)
            {
                app.record_storage_locked = true;
                printf("RECORD TEMP CLEANUP ERROR: %d\r\n", (int)cleanup);
            }

            if (!app.record_storage_locked) tud_connect();
        }

        return false;
    }

    app.record_button = button;
    app.record_invalid = false;
    activate_mode(RECORD, source, true);
    printf("RECORD START: %s\r\n", paths->temporary);
    return true;
}
static void invalidate_record(void)
{
    app.record_invalid = true;
    printf("RECORD: invalid\r\n");
}
static void end_record(bool invalid)
{
    const record_file_paths_t *paths = record_paths_for_button(app.record_button);
    FRESULT result = invalid ? wav_recording_abort() : wav_recording_end();

    if (result != FR_OK)
    {
        invalid = true;
        printf("RECORD END ERROR: %d\r\n", (int)result);
    }

    if (wav_recording_is_open())
    {
        app.record_storage_locked = true;
        printf("RECORD: file still open, USB remains disconnected\r\n");
    }
    else if (paths != NULL)
    {
        if (invalid)
        {
            result = record_discard_file(paths);

            if (result != FR_OK) app.record_storage_locked = true;
        }
        else
        {
            result = record_commit_file(paths);

            if (result != FR_OK)
            {
                invalid = true;
                FRESULT recovery = record_recover_file(paths);

                if (recovery != FR_OK)
                {
                    app.record_storage_locked = true;
                    printf("RECORD COMMIT RECOVERY ERROR: %d\r\n", (int)recovery);
                }
            }
        }

        if (result != FR_OK)
        {
            printf("RECORD FILE ERROR: %d\r\n", (int)result);
        }
    }

    if (!app.record_storage_locked) tud_connect();

    printf("RECORD END: button=%d invalid=%u\r\n", app.record_button, (unsigned)invalid);
    deactivate_mode(RECORD);
    app.record_button = NO_BUTTON;
    app.record_invalid = false;
}
/* Только маршруты выбранного владельца. RECEIVE ещё не имеет маршрута,
 * а громкость фразы в RECORD сохраняется для последующего воспроизведения. */
static uint8_t volume_routes(volume_target_t target)
{
    switch (target)
    {
        case VOLUME_GGS1: case VOLUME_APB1: return AIC3104_VOLUME_BYPASS_1;
        case VOLUME_GGS2: case VOLUME_APB2: return AIC3104_VOLUME_BYPASS_2;
        case VOLUME_ANNOUNCE:
            if (!app.announce_audio_running) return 0U;
            return ((app.announce_channels & CHANNEL_1) ? AIC3104_VOLUME_I2S_1 : 0U) |
                   ((app.announce_channels & CHANNEL_2) ? AIC3104_VOLUME_I2S_2 : 0U);
        default: return 0U;
    }
}

static void change_volume(volume_target_t target, bool increase)
{
    volume_state_t next = app.volume[target];
    bool changed = increase ? aic3104_volume_up(&next.hardware) :
                              aic3104_volume_down(&next.hardware);
    if (!changed) return;

    uint8_t routes = volume_routes(target);
    if (routes != 0U && !aic3104_apply_volume(routes, next.hardware))
    {
        printf("VOLUME ERROR: target=%u\r\n", (unsigned)target);
        return;
    }
    next.software = increase ? next.software + VOLUME_STEP : next.software - VOLUME_STEP;
    app.volume[target] = next;
    printf("VOLUME: target=%u hardware=%u software=%u\r\n",
           (unsigned)target, (unsigned)next.hardware, (unsigned)next.software);
}

/* Периодические обработчики не содержат блокирующих ожиданий. */
static handler_responce_t process_playback(
    app_mode_t mode,
    uint8_t channels,
    const char *filename
)
{
    FRESULT result = audio_playback_task();

    if (result != FR_OK)
    {
        printf("%s PLAYBACK ERROR: %d\r\n", mode_names[mode], (int)result);
        return HANDLER_ERROR;
    }

    if (!audio_playback_is_active())
    {
        printf("PLAYBACK FINISHED: %s\r\n", filename);

        result = playback_start_file(filename, channels);

        if (result != FR_OK)
        {
            printf("%s RESTART ERROR: %d\r\n", mode_names[mode], (int)result);
            return HANDLER_ERROR;
        }
    }

    return HANDLER_PROCESSING;
}

static handler_responce_t process_alert(void)
{
    return process_playback(ALERT, ALERT_CHANNELS, ALERT_FILENAME);
}

static handler_responce_t process_achtung(void)
{
    return process_playback(ACHTUNG, ACHTUNG_CHANNELS, ACHTUNG_FILENAME);
}

static handler_responce_t process_receive(void) { return HANDLER_PROCESSING; }
static handler_responce_t process_record(void)
{
    FRESULT result = wav_recording_task();

    if (result != FR_OK)
    {
        printf("RECORD TASK ERROR: %d\r\n", (int)result);
        return HANDLER_ERROR;
    }

    return HANDLER_PROCESSING;
}
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
    FRESULT result = audio_playback_task();

    if (result != FR_OK)
    {
        printf("ANNOUNCE PLAYBACK ERROR: %d\r\n", (int)result);
        return HANDLER_ERROR;
    }

    return audio_playback_is_active() ? HANDLER_PROCESSING : HANDLER_FINISHED;
}

static handler_responce_t process_manual_announce(void)
{
    if (app.manual_announce_waiting)
    {
        if (!interval_expired(uptime_ms, app.manual_announce_wait_started_ms, app.manual_announce_wait_ms))
        {
            return HANDLER_PROCESSING;
        }

        app.manual_announce_waiting = false;

        if (!announce_sequence_next(&app.manual_announce_sequence, &app.manual_announce_file))
        {
            return HANDLER_FINISHED;
        }

        if (play_announce_file(&app.manual_announce_file, app.announce_channels) != FR_OK)
        {
            app.manual_announce_wait_started_ms = uptime_ms;
            app.manual_announce_wait_ms = (uint32_t)app.manual_announce_file.delay * 1000U;
            app.manual_announce_waiting = true;
        }

        return HANDLER_PROCESSING;
    }

    handler_responce_t result = process_announce();

    if (result != HANDLER_PROCESSING)
    {
        if (result == HANDLER_FINISHED)
        {
            printf("ANNOUNCE FILE FINISHED: %s\r\n", app.manual_announce_file.pathfile);
        }

        stop_announce_audio();
        app.manual_announce_wait_started_ms = uptime_ms;
        app.manual_announce_wait_ms = (uint32_t)app.manual_announce_file.delay * 1000U;
        app.manual_announce_waiting = true;
    }

    return HANDLER_PROCESSING;
}
static handler_responce_t process_phrase(void)
{
    FRESULT result = audio_playback_task();

    if (result != FR_OK)
    {
        printf("PHRASE PLAYBACK ERROR: %d\r\n", (int)result);
        return HANDLER_ERROR;
    }

    return audio_playback_is_active() ? HANDLER_PROCESSING : HANDLER_FINISHED;
}
static handler_responce_t process_record_unlock(void)
{
    return interval_expired(uptime_ms, app.unlock_record_started_ms, UNLOCK_RECORD_TIMEOUT_MS)
           ? HANDLER_FINISHED : HANDLER_PROCESSING;
}
static handler_responce_t process_auto_announce(void)
{
    if (app.record_storage_locked) return HANDLER_FINISHED;

    if (mode_is_active(ANNOUNCE) && app.modes[ANNOUNCE].source == OUTSOURCE)
    {
        return HANDLER_FINISHED;
    }

    if (app.auto_announce_waiting)
    {
        if (!interval_expired(uptime_ms, app.auto_announce_wait_started_ms, app.auto_announce_wait_ms))
        {
            return HANDLER_FINISHED;
        }

        app.auto_announce_waiting = false;
    }

    if (!app.auto_announce_file_ready)
    {
        if (!announce_sequence_next(&app.auto_announce_sequence, &app.auto_announce_file))
        {
            return HANDLER_ERROR;
        }

        app.auto_announce_file_ready = true;
    }

    return HANDLER_PROCESSING;
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

        case RECORD_ERROR:
            if (source == OUTSOURCE && mode_is_active(RECORD)) end_record(true);
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
            uint8_t channel = transmission_line_for_mode(mode)->channel_mask;
            if (mode_is_active(ANNOUNCE) && (app.announce_channels & channel) != 0U)
            {
                uint8_t remaining = (uint8_t)(app.announce_channels & (uint8_t)~channel);
                if (ANNOUNCE_INTERRUPT_POLICY == ANNOUNCE_INTERRUPT_STOP_ALL || remaining == 0U)
                    end_announce();
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
            reset_auto_announce_sequence();
            break;

        case AUTO_ANNOUNCE_FINISHED:
            if (source != OUTSOURCE) return;
            if (mode_is_active(ANNOUNCE) && app.modes[ANNOUNCE].source == OUTSOURCE) end_announce();
            if (app.auto_announce_1) set_auto_announce_1(false);
            if (app.auto_announce_2) set_auto_announce_2(false);
            reset_auto_announce_sequence();
            printf("AUTO_ANNOUNCE: no files, disabled\r\n");
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
            if (mode_is_active(RECORD) || mode_is_active(ANNOUNCE) || mode_is_active(PHRASE) ||
                mode_is_active(ALERT) || mode_is_active(ACHTUNG)) return;
            uint8_t requested = ANNOUNCE_CHANNELS & (CHANNEL_1 | CHANNEL_2);
            uint8_t channels = requested;
            if (mode_is_active(GGS1) || mode_is_active(APB1)) channels &= (uint8_t)~CHANNEL_1;
            if (mode_is_active(GGS2) || mode_is_active(APB2)) channels &= (uint8_t)~CHANNEL_2;
            if (channels == 0U || (ANNOUNCE_START_POLICY == ANNOUNCE_START_REJECT_ALL &&
                                   channels != requested)) return;

            if (event == ANNOUNCE_START)
            {
                start_announce(channels, source, button, &app.auto_announce_file);
            }
            else
            {
                announce_sequence_reset(&app.manual_announce_sequence,
                                        button == B_F1 ? ANNOUNCE_FOLDER_1 : ANNOUNCE_FOLDER_2,
                                        ANNOUNCE_ORDER_SEQUENTIAL,
                                        ANNOUNCE_MANUAL_END_POLICY == ANNOUNCE_MANUAL_LOOP);

                if (!announce_sequence_next(&app.manual_announce_sequence, &app.manual_announce_file))
                {
                    printf("ANNOUNCE: no files for F%d\r\n", button == B_F1 ? 1 : 2);
                    return;
                }

                start_announce(channels, source, button, &app.manual_announce_file);
            }
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
            if (app.modes[ANNOUNCE].source == source && app.modes[ANNOUNCE].is_holding &&
                app.announce_button == button) end_announce();
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

            if (channel == CHANNEL_PRM)
            {
                printf("PHRASE %u: OPER playback not implemented\r\n", (unsigned)number);
                return;
            }

            if (app.record_storage_locked) return;

            if (mode_is_active(ANNOUNCE))
            {
                const char *filename = number == 1U ? PHRASE_1_FILENAME : PHRASE_2_FILENAME;
                FILINFO info;
                FRESULT result = f_stat(filename, &info);

                if (result != FR_OK || (info.fattrib & AM_DIR) != 0U ||
                    info.fsize <= WAV_PCM_HEADER_BYTES)
                {
                    printf("PHRASE UNAVAILABLE: %s (%d)\r\n", filename, (int)result);
                    return;
                }

                end_announce();
            }

            start_phrase(source, number, output, channel);
            break;
        }

        case PHRASE_FINISHED:
            if (source == OUTSOURCE && mode_is_active(PHRASE)) end_phrase();
            break;

        case PHRASE_ERROR:
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
            change_volume(target, event == VOLUME_UP);
            break;
        }
        default:
            break;
    }
}

static void process_state_logic(void)
{
    if (mode_is_active(ALERT) && process_alert() == HANDLER_ERROR) end_alert();
    if (mode_is_active(ACHTUNG) && process_achtung() == HANDLER_ERROR) end_achtung();
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
            handler_responce_t result = process_announce();

            if (result != HANDLER_PROCESSING)
            {
                if (result == HANDLER_FINISHED)
                {
                    printf("ANNOUNCE FILE FINISHED: %s\r\n", app.auto_announce_file.pathfile);
                }

                process_state_event(ANNOUNCE_FINISHED, OUTSOURCE, NO_BUTTON);
            }
        }
        else if (process_manual_announce() != HANDLER_PROCESSING)
        {
            end_announce();
        }
    }
    if (mode_is_active(PHRASE))
    {
        handler_responce_t result = process_phrase();

        if (result == HANDLER_FINISHED)
            process_state_event(PHRASE_FINISHED, OUTSOURCE, NO_BUTTON);
        else if (result == HANDLER_ERROR)
            process_state_event(PHRASE_ERROR, OUTSOURCE, NO_BUTTON);
    }
    if (mode_is_active(RECORD) && process_record() == HANDLER_ERROR)
        process_state_event(RECORD_ERROR, OUTSOURCE, NO_BUTTON);
    if (app.unlock_record && process_record_unlock() == HANDLER_FINISHED)
        process_state_event(RECORD_UNLOCK_EXPIRED, OUTSOURCE, NO_BUTTON);

    if (app.auto_announce_1 || app.auto_announce_2)
    {
        handler_responce_t result = process_auto_announce();

        if (result == HANDLER_ERROR)
        {
            process_state_event(AUTO_ANNOUNCE_FINISHED, OUTSOURCE, NO_BUTTON);
        }
        else if (result == HANDLER_PROCESSING)
        {
            process_state_event(ANNOUNCE_START, OUTSOURCE, NO_BUTTON);
            app.auto_announce_file_ready = false;

            /* Не удалось занять каналы или открыть файл: отсчитываем его задержку. */
            if (!mode_is_active(ANNOUNCE) || app.modes[ANNOUNCE].source != OUTSOURCE)
            {
                start_auto_announce_delay();
            }
        }
    }
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
