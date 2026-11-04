
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "count_bits_table.h"
#include "button_combo.h"

#define BUTTON_REMOTE_COUNT 2U

// ============================================================================
// Внутреннее состояние модуля
// ============================================================================

typedef struct
{
    uint32_t combo_last_event_ms;
    uint8_t combo_active;

    uint16_t active_buttons_mask;

    ButtonEvent combo_events[BUTTON_COMBO_COUNT];
    ButtonEvent prev_button_states[BUTTON_COMBO_COUNT];
} ButtonComboContext;

// Общий интервал ожидания, независимые таймеры для каждого пульта.
static uint32_t combo_timeout_ms = 0;

// Отдельные состояния для REMOTE_1 и REMOTE_2.
static ButtonComboContext combo_contexts[BUTTON_REMOTE_COUNT];

// ============================================================================
// Вспомогательные функции
// ============================================================================

// Добавление кнопки в комбинацию.
static inline void add_to_combo(ButtonComboContext *context, uint8_t btn_idx, ButtonEvent state)
{
    context->active_buttons_mask |= (uint16_t)(1U << btn_idx);
    context->combo_events[btn_idx] = state;
}

// Добавляет все кнопки, находящиеся в LONG_EVENT.
static inline void add_active_long_buttons(ButtonComboContext *context, const ButtonEvent *buttons[])
{
    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
    {
        if (*buttons[i] == LONG_EVENT)
        {
            add_to_combo(context, i, LONG_EVENT);
        }
    }
}

// Сброс текущей комбинации одного пульта.
static inline void reset_combo(ButtonComboContext *context)
{
    context->combo_active = 0;
    context->combo_last_event_ms = 0;
    context->active_buttons_mask = 0;

    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
    {
        context->combo_events[i] = NO_EVENT;
    }
}

// Выдача накопленной комбинации одного пульта.
static inline void emit_combo(ButtonComboContext *context, source_t source)
{
    if (!context->combo_active || context->active_buttons_mask == 0U)
    {
        return;
    }

    FlushContext flush_context = {
        .source = source,
        .count = count_bits((uint8_t)context->active_buttons_mask) +
                 count_bits((uint8_t)(context->active_buttons_mask >> 8)),
        .mask = context->active_buttons_mask
    };

    memcpy(flush_context.events, context->combo_events, sizeof(flush_context.events));

    flush_combo(flush_context);
}

// Проверка таймаутов обоих пультов.
static inline void check_combo_timeouts(uint32_t uptime_ms)
{
    for (uint8_t i = 0; i < BUTTON_REMOTE_COUNT; i++)
    {
        ButtonComboContext *context = &combo_contexts[i];

        if (context->combo_active &&
            (uint32_t)(uptime_ms - context->combo_last_event_ms) >= combo_timeout_ms)
        {
            emit_combo(context, (source_t)i);
            reset_combo(context);
        }
    }
}

// ============================================================================
// Weak-обработчик комбинации по умолчанию
// ============================================================================

__attribute__((weak))
void flush_combo(FlushContext context)
{
    (void)context;
}

// ============================================================================
// Инициализация
// ============================================================================

// timeout_ms — время ожидания после последнего нового события.
// Значение 0 означает выдачу комбинации без ожидания.
void button_combo_init(uint32_t timeout_ms)
{
    combo_timeout_ms = timeout_ms;

    for (uint8_t i = 0; i < BUTTON_REMOTE_COUNT; i++)
    {
        ButtonComboContext *context = &combo_contexts[i];

        reset_combo(context);

        for (uint8_t j = 0; j < BUTTON_COMBO_COUNT; j++)
        {
            context->prev_button_states[j] = NO_EVENT;
        }
    }
}

// ============================================================================
// Основная обработка
// ============================================================================

void process_button_combinations(const ButtonStatus *status, uint32_t uptime_ms)
{
    // Проверяем таймауты обоих пультов независимо от наличия ответа.
    check_combo_timeouts(uptime_ms);

    if (status == NULL || status->ready != 1U)
    {
        return;
    }

    // Определяем источник события.
    source_t source;

    if (status->active_remote == USART2)
    {
        source = REMOTE_1;
    }
    else if (status->active_remote == UART4)
    {
        source = REMOTE_2;
    }
    else
    {
        return;
    }

    ButtonComboContext *context = &combo_contexts[source];

    // Порядок строго соответствует S1..S10.
    const ButtonEvent *btn_ptr[BUTTON_COMBO_COUNT] = {
        &status->S1,
        &status->S2,
        &status->S3,
        &status->S4,
        &status->S5,
        &status->S6,
        &status->S7,
        &status->S8,
        &status->S9,
        &status->S10
    };

    // ========================================================================
    // 1. Детектирование новых событий
    // ========================================================================

    uint16_t new_events_mask = 0;

    for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
    {
        ButtonEvent prev = context->prev_button_states[i];
        ButtonEvent curr = *btn_ptr[i];

        // Начало события: NO_EVENT -> любое другое.
        // Отпускание после LONG_EVENT: LONG_EVENT -> NO_EVENT.
        if ((prev == NO_EVENT && curr != NO_EVENT) ||
            (prev == LONG_EVENT && curr == NO_EVENT))
        {
            new_events_mask |= (uint16_t)(1U << i);
        }

        context->prev_button_states[i] = curr;
    }

    if (new_events_mask == 0U)
    {
        return;
    }

    // ========================================================================
    // 2. Проверка конфликта
    // ========================================================================

    uint16_t overlap = new_events_mask & context->active_buttons_mask;

    if (overlap != 0U)
    {
        // Повторное событие кнопки из текущей комбинации.
        // Новые неконфликтующие кнопки добавляются в старую комбинацию.
        uint16_t non_conflicting =
            new_events_mask & (uint16_t)~context->active_buttons_mask;

        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
        {
            if (non_conflicting & (1U << i))
            {
                add_to_combo(context, i, *btn_ptr[i]);
            }
        }

        emit_combo(context, source);
        reset_combo(context);

        // Начало новой комбинации.
        context->combo_active = 1;
        context->combo_last_event_ms = uptime_ms;

        // Наследуем кнопки, удерживаемые в LONG_EVENT.
        add_active_long_buttons(context, btn_ptr);

        // Добавляем события, вызвавшие конфликт,
        // включая NO_EVENT при отпускании LONG_EVENT.
        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
        {
            if (overlap & (1U << i))
            {
                add_to_combo(context, i, *btn_ptr[i]);
            }
        }
    }
    else
    {
        // Новые кнопки без конфликта.
        if (!context->combo_active)
        {
            context->combo_active = 1;

            // Наследуем удерживаемые LONG_EVENT.
            add_active_long_buttons(context, btn_ptr);
        }

        // Продлеваем окно накопления только данного пульта.
        context->combo_last_event_ms = uptime_ms;

        for (uint8_t i = 0; i < BUTTON_COMBO_COUNT; i++)
        {
            if (new_events_mask & (1U << i))
            {
                add_to_combo(context, i, *btn_ptr[i]);
            }
        }
    }

    if (combo_timeout_ms == 0U)
    {
        emit_combo(context, source);
        reset_combo(context);
    }
}
