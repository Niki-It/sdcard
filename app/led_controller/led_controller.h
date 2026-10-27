#pragma once 
#include <stdint.h>
#include <stdbool.h>
#include <stm32f4xx_ll_usart.h>

typedef struct 
{
    bool VD1;
    bool VD2;
    bool VD3;
    bool VD4;
    bool VD5;
    bool VD6;
    bool VD7;
    bool VD8;
    bool VD9;
} Leds;

typedef struct 
{
    uint8_t data[3];
} SetLedStateCommand;

typedef struct
{
    uint8_t data[4];
} RawResponce;

typedef enum
{
    NO_EVENT,
    SHORT_EVENT,
    DOUBLE_SHORT,
    LONG_EVENT,
} LedEvent;
typedef struct
{
    LedEvent VD1;
    LedEvent VD2;
    LedEvent VD3;
    LedEvent VD4;
    LedEvent VD5;
    LedEvent VD6;
    LedEvent VD7;
    LedEvent VD8;
} ButtonEvents;

#define LED_VD1  (1U << 0)
#define LED_VD2  (1U << 1)
#define LED_VD3  (1U << 2)
#define LED_VD4  (1U << 3)
#define LED_VD5  (1U << 4)
#define LED_VD6  (1U << 5)
#define LED_VD7  (1U << 6)
#define LED_VD8  (1U << 7)
#define LED_VD9  (1U << 8)

#define LED_ALL  ((1U << 9) - 1U)

typedef struct 
{
    // 1 - готов, 0 не готов
    uint8_t ready;
    LedEvent VD1;
    LedEvent VD2;
    LedEvent VD3;
    LedEvent VD4;
    LedEvent VD5;
    LedEvent VD6;
    LedEvent VD7;
    LedEvent VD8;
} ButtonStatus;

// ---------------- Core логика ---------------------------
ButtonStatus poll_button_events(void);
void set_all_leds(Leds leds);
void set_leds(uint16_t mask, bool enabled);

void send_SetLedState(USART_TypeDef *USARTx);
void tim6_handler(void);
void tim6_callback(void);
uint8_t can_send_command(void);
// ------ Вспомогательные функции ---------------------
void LedController_PeriphInit(void);
void write_events(RawResponce response);

/* Идентификатор для мигания — индекс, а LED_VDx — маска для set_leds. */
typedef enum {
    VD1, VD2, VD3, VD4, VD5, VD6, VD7, VD8, VD9,
    LED_BLINKING_COUNT
} blinking_led_t;

/* count полных циклов (2 * count переключений).
 * Первое переключение немедленное; таймер запускается первым blinking_process.
 * period_ms — интервал между переключениями; 0 заменяется на 1 мс.
 * count == 0 отменяет мигание, сохраняя текущее состояние.
 * Новый запрос заменяет предыдущий, начиная с текущего состояния LED.
 */
void request_blinking(blinking_led_t led, uint16_t count, uint32_t period_ms);

/* Вызывать из основного цикла с монотонным счётчиком миллисекунд uint32_t.
 * Не более одного переключения на LED за вызов; пропущенные интервалы
 * не догоняются. Переполнение счётчика поддерживается (пауза < 2^32 мс).
 */
void blinking_process(uint32_t current_tick);
void led_controller_process(uint32_t uptime_ms);

/* Управление состоянием и миганием — только из одного основного контекста.
 * set_leds отменяет мигание выбранных LED, set_all_leds — всех LED.
 * tim6_handler разрешён в ISR; callback не должен менять состояние LED.
 * write_events и poll_button_events вызываются вне прерываний.
 * Готовность публикует led_raw
 * через is_response_ready ПОСЛЕ write_events, как в исходном интерфейсе.
 */
