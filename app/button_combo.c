
#include <stdint.h>
#include <stddef.h>

#include "SEGGER_RTT.h"
#include "button_combo.h"

#define DEFAULT_COMBO_TIMEOUT 50U

// ============================================================================
// Внутреннее состояние модуля
// ============================================================================

static uint8_t combo_timeout = DEFAULT_COMBO_TIMEOUT;
static uint8_t combo_active = 0;
static uint16_t combo_timer = 0;

static uint8_t active_buttons_mask = 0;

static LedEvent combo_events[BUTTON_COMBO_COUNT] = {NO_EVENT};
static LedEvent prev_button_states[BUTTON_COMBO_COUNT] = {NO_EVENT};

// ============================================================================
// Вспомогательные функции
// ============================================================================

static inline void add_to_combo(uint8_t btn_idx, LedEvent state)
{
    active_buttons_mask |= (uint8_t)(1U << btn_idx);
    combo_events[btn_idx] = state;
}

// Добавляет все кнопки, находящиеся в LONG_EVENT
static void add_active_long_buttons(const LedEvent *buttons[])
{
    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
        if (*buttons[i] == LONG_EVENT) {
            add_to_combo(i, LONG_EVENT);
        }
    }
}

// Сброс текущей комбинации
static void reset_combo(void)
{
    combo_active = 0;
    combo_timer = 0;
    active_buttons_mask = 0;

    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
        combo_events[i] = NO_EVENT;
    }
}

// Выдача накопленной комбинации
static void emit_combo(void)
{
    if (!combo_active || active_buttons_mask == 0U) {
        return;
    }

    flush_combo(active_buttons_mask, combo_events);
}

// ============================================================================
// Weak-обработчик комбинации по умолчанию
// ============================================================================

__attribute__((weak))
void flush_combo(uint8_t buttons_mask,
                 const LedEvent events[BUTTON_COMBO_COUNT])
{
    SEGGER_RTT_printf(0, "=== start ===\r\n");

    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
        if (buttons_mask & (1U << i)) {
            SEGGER_RTT_printf(0, "VD%u: %u\r\n",
                              (unsigned)(i + 1U),
                              (unsigned)events[i]);
        }
    }

    SEGGER_RTT_printf(0, "=== end ===\r\n");
}

// ============================================================================
// Настройка таймаута
// ============================================================================

void set_combo_cycle_timeout(uint8_t timeout)
{
    combo_timeout = timeout;
}

// ============================================================================
// Основная обработка
// ============================================================================

void process_button_combinations(const ButtonStatus *status)
{
    if (status == NULL || status->ready != 1) {
        return;
    }

    // Порядок строго соответствует VD1..VD8
    const LedEvent *btn_ptr[BUTTON_COMBO_COUNT] = {
        &status->VD1,
        &status->VD2,
        &status->VD3,
        &status->VD4,
        &status->VD5,
        &status->VD6,
        &status->VD7,
        &status->VD8
    };

    // ========================================================================
    // 1. Проверка таймаута
    // ========================================================================

    if (combo_active) {
        combo_timer++;

        if (combo_timer >= combo_timeout) {
            emit_combo();
            reset_combo();
        }
    }

    // ========================================================================
    // 2. Детектирование новых событий
    // ========================================================================

    uint8_t new_events_mask = 0;

    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
        LedEvent prev = prev_button_states[i];
        LedEvent curr = *btn_ptr[i];

        // Начало события: NO_EVENT -> любое другое
        // Отпускание после LONG_EVENT: LONG_EVENT -> NO_EVENT
        if ((prev == NO_EVENT && curr != NO_EVENT) ||
            (prev == LONG_EVENT && curr == NO_EVENT)) {

            new_events_mask |= (uint8_t)(1U << i);
        }

        prev_button_states[i] = curr;
    }

    // Нет новых событий
    if (new_events_mask == 0U) {
        return;
    }

    // ========================================================================
    // 3. Проверка конфликта
    // ========================================================================

    uint8_t overlap = new_events_mask & active_buttons_mask;

    if (overlap != 0U) {

        // --------------------------------------------------------------------
        // Сценарий А: повторное событие кнопки из текущей комбинации
        // --------------------------------------------------------------------

        // Новые неконфликтующие кнопки добавляются в старую комбинацию
        uint8_t non_conflicting =
            new_events_mask & (uint8_t)~active_buttons_mask;

        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
            if (non_conflicting & (1U << i)) {
                add_to_combo(i, *btn_ptr[i]);
            }
        }

        // Завершение предыдущей комбинации
        emit_combo();
        reset_combo();

        // Начало новой комбинации
        combo_active = 1;

        // Наследуем кнопки, удерживаемые в LONG_EVENT
        add_active_long_buttons(btn_ptr);

        // Добавляем события, вызвавшие конфликт
        // Включая NO_EVENT при отпускании LONG_EVENT
        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
            if (overlap & (1U << i)) {
                add_to_combo(i, *btn_ptr[i]);
            }
        }

    } else {

        // --------------------------------------------------------------------
        // Сценарий Б: новые кнопки без конфликта
        // --------------------------------------------------------------------

        if (!combo_active) {
            combo_active = 1;

            // Наследуем удерживаемые LONG_EVENT
            add_active_long_buttons(btn_ptr);
        }

        // Продлеваем окно накопления комбинации
        combo_timer = 0;

        // Добавляем новые события
        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++) {
            if (new_events_mask & (1U << i)) {
                add_to_combo(i, *btn_ptr[i]);
            }
        }
    }
}
