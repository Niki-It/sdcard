#include "debounce.h"
#include "stm32f4xx_ll_gpio.h"


/* ============================================================================
 * Внутреннее состояние модуля
 * ========================================================================== */

static pin_debounce_t *debounce_pins = NULL;
static size_t debounce_count = 0;

static uint32_t debounce_period_ms = 1;
static uint32_t last_debounce_tick = 0;
static uint8_t timer_started = 0;


/* ============================================================================
 * Инициализация модуля
 * ========================================================================== */

void debounce_init(pin_debounce_t *pins, size_t count, uint32_t period_ms)
{
    debounce_pins = pins;
    debounce_count = (pins != NULL) ? count : 0;

    debounce_period_ms = (period_ms != 0U) ? period_ms : 1U;

    last_debounce_tick = 0;
    timer_started = 0;

    for (size_t i = 0; i < debounce_count; i++) {
        pin_debounce_t *pin = &debounce_pins[i];

        pin->counter = 0;

        if (pin->threshold == 0U) {
            pin->threshold = 1U;
        }
    }
}


/* ============================================================================
 * Опрос физических входов
 * ========================================================================== */

void pin_polling(void)
{
    for (size_t i = 0; i < debounce_count; i++) {
        pin_debounce_t *pin = &debounce_pins[i];
        pin->raw_state = LL_GPIO_IsInputPinSet(pin->port, pin->pin) ? 1U : 0U;
    }
}


/* ============================================================================
 * Обработка антидребезга
 * ========================================================================== */

void debounce_process(uint32_t current_tick)
{
    if (!timer_started) {
        last_debounce_tick = current_tick;
        timer_started = 1;
        return;
    }

    if ((uint32_t)(current_tick - last_debounce_tick) < debounce_period_ms) {
        return;
    }

    last_debounce_tick = current_tick;

    for (size_t i = 0; i < debounce_count; i++) {
        pin_debounce_t *pin = &debounce_pins[i];

        if (pin->raw_state == pin->state) {
            pin->counter = 0;
            continue;
        }

        pin->counter++;

        if (pin->counter >= pin->threshold) {
            pin->state = pin->raw_state;
            pin->counter = 0;
        }
    }
}