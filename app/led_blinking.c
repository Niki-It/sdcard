#include "led_blinking.h"

#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"

extern Leds leds;
/* ============================================================================
 * Внутреннее состояние
 * ========================================================================== */

typedef struct {
    uint32_t toggles_left;
    uint32_t period_ms;
    uint32_t last_tick;
    uint8_t timer_started;
} led_blinking_state_t;

static led_blinking_state_t blinking[LED_BLINKING_COUNT] = {0};


/* ============================================================================
 * Переключение светодиода
 * ========================================================================== */

/* Работает в том числе с битовыми полями структуры Leds. */
static void toggle_led(blinking_led_t led)
{
    switch (led) {
    case VD1: leds.VD1 = !leds.VD1; break;
    case VD2: leds.VD2 = !leds.VD2; break;
    case VD3: leds.VD3 = !leds.VD3; break;
    case VD4: leds.VD4 = !leds.VD4; break;
    case VD5: leds.VD5 = !leds.VD5; break;
    case VD6: leds.VD6 = !leds.VD6; break;
    case VD7: leds.VD7 = !leds.VD7; break;
    case VD8: leds.VD8 = !leds.VD8; break;
    case VD9: leds.VD9 = !leds.VD9; break;

    default:
        break;
    }
}


/* ============================================================================
 * Запуск или замена паттерна
 * ========================================================================== */

void request_blinking(
    blinking_led_t led,
    uint16_t count,
    uint32_t period_ms)
{
    if ((unsigned)led >= LED_BLINKING_COUNT) {
        return;
    }

    led_blinking_state_t *item = &blinking[led];

    /* Полностью заменяем предыдущий паттерн. */
    *item = (led_blinking_state_t) {
        .toggles_left = (uint32_t)count * 2U,
        .period_ms = (period_ms != 0U) ? period_ms : 1U,
        .last_tick = 0U,
        .timer_started = 0U
    };

    if (item->toggles_left == 0U) {
        return;
    }

    /* Первое переключение выполняется немедленно. */
    toggle_led(led);
    item->toggles_left--;
}


/* ============================================================================
 * Периодическое обслуживание
 * ========================================================================== */

void process_leds(uint32_t current_tick)
{
    for (unsigned i = 0; i < LED_BLINKING_COUNT; i++) {
        led_blinking_state_t *item = &blinking[i];

        if (item->toggles_left == 0U) {
            continue;
        }

        /* Привязываем новый паттерн к переданному времени. */
        if (!item->timer_started) {
            item->last_tick = current_tick;
            item->timer_started = 1U;
            continue;
        }

        if ((uint32_t)(current_tick - item->last_tick) <
            item->period_ms) {
            continue;
        }

        item->last_tick = current_tick;

        toggle_led((blinking_led_t)i);
        item->toggles_left--;
    }
}