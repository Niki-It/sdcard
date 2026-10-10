#include <stdint.h>
#include "led_controller/led_controller.h"
#include "button_combo.h"

#define DEFAULT_COMBO_TIMEOUT 50 // Количество циклов опроса с ready == 1 до выдачи таймаута

// ============================================================================
// 3. Статические переменные состояния (инкапсуляция логики)
// ============================================================================
static uint8_t  combo_timeout = DEFAULT_COMBO_TIMEOUT;
static uint8_t  combo_active = 0;          
static uint16_t combo_timer = 0;           
static uint8_t  active_buttons_mask = 0;   // Битовая маска кнопок в текущей комбинации (бит 0 = VD1 и т.д.)
static LedEvent combo_events[8] = {NO_EVENT};     
static LedEvent prev_button_states[8] = {NO_EVENT}; 

// ============================================================================
// 4. Вспомогательные функции
// ============================================================================
static inline void add_to_combo(uint8_t btn_idx, LedEvent state) {
    active_buttons_mask |= (1U << btn_idx);
    combo_events[btn_idx] = state;
}

static void flush_combo(void) {
    if (!combo_active) return;

    SEGGER_RTT_printf(0, "=== start ===\r\n");
    for (uint8_t i = 0; i < 8; i++) {
        if (active_buttons_mask & (1U << i)) {
            // Выводим номер кнопки (i+1) и её зафиксированное состояние (0, 1, 2 или 3)
            SEGGER_RTT_printf(0, "VD%d: %d\r\n", i + 1, combo_events[i]);
        }
    }
    SEGGER_RTT_printf(0, "=== end ===\r\n");
}

static void reset_combo(void) {
    combo_active = 0;
    combo_timer = 0;
    active_buttons_mask = 0;
    for (uint8_t i = 0; i < 8; i++) {
        combo_events[i] = NO_EVENT;
    }
}

// ============================================================================
// 5. Основная функция обработки (вызывать в while(1))
// ============================================================================
void set_comdo_cycle_timeout(uint8_t timeout) {
    combo_timeout = timeout;
}

void process_button_combinations(const ButtonStatus* status) {
    if (status->ready != 1) {
        return; // Обрабатываем только новые сообщения
    }

    // Локальный массив указателей для прямого доступа к полям VD1..VD8 по индексу 0..7
    // Без заглушек, без выравнивания: индекс 0 строго соответствует VD1, индекс 7 строго VD8
    const LedEvent* btn_ptr[8] = {
        &status->VD1, &status->VD2, &status->VD3, &status->VD4,
        &status->VD5, &status->VD6, &status->VD7, &status->VD8
    };

    // 1. Проверка таймаута
    if (combo_active) {
        combo_timer++;
        if (combo_timer > combo_timeout) {
            flush_combo();
            reset_combo();
        }
    }

    // 2. Детектирование новых событий в текущем цикле
    uint8_t new_events_mask = 0;

    for (uint8_t i = 0; i < 8; i++) {
        LedEvent prev = prev_button_states[i];
        LedEvent curr = *btn_ptr[i];

        // Новое событие: переход 0 -> 1/2/3 ИЛИ переход 3 -> 0
        // (Переходы 1->0 и 2->0 игнорируются, так как это просто возврат модуля в исходное состояние)
        if ((prev == NO_EVENT && curr != NO_EVENT) || (prev == LONG_EVENT && curr == NO_EVENT)) {
            new_events_mask |= (1U << i);
        }
        
        // Обновляем предыдущее состояние для следующего цикла
        prev_button_states[i] = curr;
    }

    // 3. Обработка новых событий
    if (new_events_mask != 0) {
        uint8_t overlap = new_events_mask & active_buttons_mask;

        if (overlap != 0) {
            // --- СЦЕНАРИЙ А: Конфликт (событие на кнопке, которая уже есть в комбинации) ---
            
            // Шаг 1: Добавляем НЕ конфликтующие новые кнопки в текущую (старую) комбинацию
            uint8_t non_conflicting = new_events_mask & ~active_buttons_mask;
            for (uint8_t i = 0; i < 8; i++) {
                if (non_conflicting & (1U << i)) {
                    add_to_combo(i, *btn_ptr[i]);
                }
            }
            
            // Шаг 2: Выдаём сформированную комбинацию
            flush_combo();
            
            // Шаг 3: Сбрасываем состояние для новой комбинации
            reset_combo();
            combo_active = 1;
            combo_timer = 0;
            
            // Шаг 4: Правило "если на момент начала какие-то кнопки уже находятся в состоянии 3"
            for (uint8_t i = 0; i < 8; i++) {
                if (*btn_ptr[i] == LONG_EVENT) {
                    add_to_combo(i, LONG_EVENT);
                }
            }
            
            // Шаг 5: Добавляем в новую комбинацию кнопки, вызвавшие конфликт (overlap)
            // Это перезапишет состояние, если на шаге 4 кнопка была добавлена как LONG_EVENT, 
            // что корректно, так как её новое состояние (например, NO_EVENT при отпускании) является триггером.
            for (uint8_t i = 0; i < 8; i++) {
                if (overlap & (1U << i)) {
                    add_to_combo(i, *btn_ptr[i]);
                }
            }
        } else {
            // --- СЦЕНАРИЙ Б: Нет конфликта (новые кнопки или старт новой комбинации) ---
            
            if (!combo_active) {
                combo_active = 1;
                combo_timer = 0;
                
                // Правило: при старте накопления добавляем все кнопки, уже находящиеся в состоянии 3
                for (uint8_t i = 0; i < 8; i++) {
                    if (*btn_ptr[i] == LONG_EVENT) {
                        add_to_combo(i, LONG_EVENT);
                    }
                }
            } else {
                // Комбинация уже активна, просто сбрасываем таймаут
                combo_timer = 0;
            }
            
            // Добавляем все новые события в текущую комбинацию
            for (uint8_t i = 0; i < 8; i++) {
                if (new_events_mask & (1U << i)) {
                    add_to_combo(i, *btn_ptr[i]);
                }
            }
        }
    }
}