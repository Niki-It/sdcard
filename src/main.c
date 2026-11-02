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
void flush_combo(uint8_t buttons_count, uint8_t buttons_mask, const LedEvent events[BUTTON_COMBO_COUNT])
{
    
}

// Обработка переходов между состояниями
static void process_state_event(app_event_t event)
{
    
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
    static uint8_t previous_state = 1;

    uint8_t current_state = debounced_pins[DEBOUNCE_INPUT_ACHTUNG].state;

    /* Событие формируется только при изменении обработанного уровня. */
    if (current_state == previous_state) {
        return;
    }

    previous_state = current_state;

    if (current_state == 0U) {
        if (state == IDLE) {
            process_state_event(APP_EVENT_FIRE_START);
        }
    } else {
        if (state == FIRE) {
            process_state_event(APP_EVENT_FIRE_FINISH);
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
            process_state_event(APP_EVENT_PRM_START);
        }
    } else {
        if (state == PRM) {
            process_state_event(APP_EVENT_PRM_FINISH);
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