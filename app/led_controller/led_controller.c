#include "led_controller.h"

#include "customcrc8/new_crc8.h"
#include "led_raw.h"

typedef struct {
    uint32_t toggles_left;
    uint32_t period_ms;
    uint32_t last_tick;
    bool timer_started;
} BlinkState;

#define LED_RESPONSE_TIMEOUT_MS 100U

typedef enum
{
    LED_EXCHANGE_IDLE,
    LED_EXCHANGE_WAIT_RESPONSE,
    LED_EXCHANGE_EVENTS_PENDING
} led_exchange_state_t;

static led_exchange_state_t exchange_state = LED_EXCHANGE_IDLE;
static uint32_t request_tick = 0U;

/* Единственный источник состояния всех девяти светодиодов. */
static uint16_t led_state;
static BlinkState blinking[LED_BLINKING_COUNT];
static ButtonEvents button_events;
static volatile uint8_t is_send_command;

static inline uint16_t leds_to_mask(Leds leds)
{
    return (uint16_t)(
        (leds.VD1 ? LED_VD1 : 0U) | (leds.VD2 ? LED_VD2 : 0U) |
        (leds.VD3 ? LED_VD3 : 0U) | (leds.VD4 ? LED_VD4 : 0U) |
        (leds.VD5 ? LED_VD5 : 0U) | (leds.VD6 ? LED_VD6 : 0U) |
        (leds.VD7 ? LED_VD7 : 0U) | (leds.VD8 ? LED_VD8 : 0U) |
        (leds.VD9 ? LED_VD9 : 0U));
}

static inline void stop_blinking(uint16_t mask)
{
    for (unsigned i = 0; i < LED_BLINKING_COUNT; ++i) {
        if (mask & (1U << i)) {
            blinking[i] = (BlinkState){0};
        }
    }
}

void set_all_leds(Leds leds)
{
    stop_blinking(LED_ALL);
    led_state = leds_to_mask(leds);
}

void set_leds(uint16_t mask, bool enabled)
{
    mask &= LED_ALL;
    stop_blinking(mask);
    if (enabled) {
        led_state |= mask;
    } else {
        led_state &= (uint16_t)~mask;
    }
}

void request_blinking(blinking_led_t led, uint16_t count, uint32_t period_ms)
{
    if ((unsigned)led >= LED_BLINKING_COUNT) {
        return;
    }

    BlinkState *item = &blinking[led];
    *item = (BlinkState){
        .toggles_left = (uint32_t)count * 2U,
        .period_ms = period_ms != 0U ? period_ms : 1U
    };
    if (item->toggles_left != 0U) {
        led_state ^= (uint16_t)(1U << (unsigned)led);
        --item->toggles_left;
    }
}

void blinking_process(uint32_t current_tick)
{
    for (unsigned i = 0; i < LED_BLINKING_COUNT; ++i) {
        BlinkState *item = &blinking[i];
        if (item->toggles_left == 0U) {
            continue;
        }
        if (!item->timer_started) {
            item->last_tick = current_tick;
            item->timer_started = true;
            continue;
        }
        if ((uint32_t)(current_tick - item->last_tick) >= item->period_ms) {
            item->last_tick = current_tick;
            led_state ^= (uint16_t)(1U << i);
            --item->toggles_left;
        }
    }
}

static inline SetLedStateCommand formSetLedStateCommand(void)
{
    SetLedStateCommand command = {{0}};
    command.data[0] = (uint8_t)((frame_number & 0x03U) |
                              ((led_state & LED_VD9) >> 6));
    command.data[1] = (uint8_t)(led_state & 0xFFU);
    command.data[2] = crc(command.data, 2);
    return command;
}

void send_SetLedState(USART_TypeDef *USARTx)
{
    send_command_sync(USARTx, formSetLedStateCommand());
}

ButtonStatus poll_button_events(void)
{
    ButtonStatus status = {0};

    if (exchange_state != LED_EXCHANGE_EVENTS_PENDING)
    {
        return status;
    }

    status.ready = 1U;
    status.VD1 = button_events.VD1;
    status.VD2 = button_events.VD2;
    status.VD3 = button_events.VD3;
    status.VD4 = button_events.VD4;
    status.VD5 = button_events.VD5;
    status.VD6 = button_events.VD6;
    status.VD7 = button_events.VD7;
    status.VD8 = button_events.VD8;

    inc_frame_number();
    exchange_state = LED_EXCHANGE_IDLE;

    return status;
}

void write_events(RawResponce response)
{
    uint32_t data = (uint32_t)response.data[1] |
                    ((uint32_t)response.data[2] << 8);
    button_events.VD1 = (LedEvent)((data >> 0) & 0x03U);
    button_events.VD2 = (LedEvent)((data >> 2) & 0x03U);
    button_events.VD3 = (LedEvent)((data >> 4) & 0x03U);
    button_events.VD4 = (LedEvent)((data >> 6) & 0x03U);
    button_events.VD5 = (LedEvent)((data >> 8) & 0x03U);
    button_events.VD6 = (LedEvent)((data >> 10) & 0x03U);
    button_events.VD7 = (LedEvent)((data >> 12) & 0x03U);
    button_events.VD8 = (LedEvent)((data >> 14) & 0x03U);
}

void LedController_PeriphInit(void)
{
    led_controller_usart_init();
    TIM6_Init();
}

uint8_t can_send_command(void)
{
    if (is_send_command) {
        is_send_command = 0;
        return 1;
    }
    return 0;
}

void tim6_handler(void)
{
    is_send_command = 1U;
    tim6_callback();
}

/* Необязательный пользовательский hook. Мигание больше от него не зависит. */
__attribute__((weak)) void tim6_callback(void)
{
}



void led_controller_process(uint32_t current_tick)
{
    RawResponce response;

    if (exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        return;
    }

    if (exchange_state == LED_EXCHANGE_WAIT_RESPONSE)
    {
        /* Сначала забираем ответ, даже если main долго был занят. */
        if (led_raw_take_response(&response))
        {
            write_events(response);
            exchange_state = LED_EXCHANGE_EVENTS_PENDING;

            return;
        }

        if ((uint32_t)(current_tick - request_tick) <
            LED_RESPONSE_TIMEOUT_MS)
        {
            return;
        }

        /* Ответ не получен. Номер кадра оставляем прежним. */
        led_raw_cancel_response();
        exchange_state = LED_EXCHANGE_IDLE;
    }

    if (!can_send_command())
    {
        return;
    }

    request_tick = current_tick;
    exchange_state = LED_EXCHANGE_WAIT_RESPONSE;

    send_SetLedState(USART2);
}