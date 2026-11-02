#include <stddef.h>

#include "led_controller.h"
#include "stdio.h"
#include "customcrc8/new_crc8.h"
#include "led_raw.h"

#define LED_RESPONSE_TIMEOUT_MS 20U
#define LED_MAX_FAILED_REQUESTS  20U
#define LED_REMOTE_COUNT         2U

typedef struct
{
    uint32_t toggles_left;
    uint32_t period_ms;
    uint32_t last_tick;
    bool timer_started;
} BlinkState;

typedef enum
{
    LED_EXCHANGE_IDLE,
    LED_EXCHANGE_WAIT_RESPONSE,
    LED_EXCHANGE_EVENTS_PENDING
} led_exchange_state_t;

typedef enum
{
    LED_LINK_NORMAL,
    LED_LINK_RECOVERY
} led_link_state_t;

#define LED_RECOVERY_LOG_PERIOD 100U
#define LED_RECOVERY_SHIFT_PERIOD 4U

typedef struct
{
    led_exchange_state_t exchange_state;
    led_link_state_t link_state;

    uint32_t request_tick;
    uint32_t recovery_attempts;
    ButtonEvents button_events;

    uint8_t frame_number;
    uint8_t failed_requests;
    uint32_t crc_errors;
    uint32_t frame_errors;
} LedControllerContext;
/* Отдельные состояния обмена: USART2 и UART4. */
static LedControllerContext controllers[LED_REMOTE_COUNT];

/* Источник последней принятой команды. По умолчанию первый пульт. */
static USART_TypeDef *active_remote = USART2;



/* Состояние всех девяти светодиодов и мигания общее для двух пультов. */
static uint16_t led_state;
static BlinkState blinking[LED_BLINKING_COUNT];
static volatile uint8_t is_send_command;

static inline LedControllerContext *get_controller(USART_TypeDef *USARTx)
{
    if (USARTx == USART2)
    {
        return &controllers[0];
    }

    if (USARTx == UART4)
    {
        return &controllers[1];
    }

    return NULL;
}

static inline uint16_t leds_to_mask(Leds leds)
{
    return (uint16_t)(
        (leds.VD1 ? LED_VD1 : 0U) | (leds.VD2 ? LED_VD2 : 0U) |
        (leds.VD3 ? LED_VD3 : 0U) | (leds.VD4 ? LED_VD4 : 0U) |
        (leds.VD5 ? LED_VD5 : 0U) | (leds.VD6 ? LED_VD6 : 0U) |
        (leds.VD7 ? LED_VD7 : 0U) | (leds.VD8 ? LED_VD8 : 0U) |
        (leds.VD9 ? LED_VD9 : 0U)) | (leds.VD10 ? LED_VD10 : 0U)
        | (leds.VD11 ? LED_VD11 : 0U);
}

static inline void stop_blinking(uint16_t mask)
{
    for (unsigned i = 0; i < LED_BLINKING_COUNT; ++i)
    {
        if (mask & (1U << i))
        {
            blinking[i] = (BlinkState){0};
        }
    }
}

void set_all_leds(uint16_t mask)
{
    stop_blinking(LED_ALL);
    led_state = mask & LED_ALL;
}

void set_leds(uint16_t mask, bool enabled)
{
    mask &= LED_ALL;
    stop_blinking(mask);

    if (enabled)
    {
        led_state |= mask;
    }
    else
    {
        led_state &= (uint16_t)~mask;
    }
}
uint16_t get_leds(uint16_t mask)
{
    return led_state & mask & LED_ALL;
}

void request_blinking(blinking_led_t led, uint16_t count, uint32_t period_ms)
{
    if ((unsigned)led >= LED_BLINKING_COUNT)
    {
        return;
    }

    BlinkState *item = &blinking[led];
    *item = (BlinkState){
        .toggles_left = (uint32_t)count * 2U,
        .period_ms = period_ms != 0U ? period_ms : 1U
    };

    if (item->toggles_left != 0U)
    {
        led_state ^= (uint16_t)(1U << (unsigned)led);
        --item->toggles_left;
    }
}

void blinking_process(uint32_t current_tick)
{
    for (unsigned i = 0; i < LED_BLINKING_COUNT; ++i)
    {
        BlinkState *item = &blinking[i];

        if (item->toggles_left == 0U)
        {
            continue;
        }

        if (!item->timer_started)
        {
            item->last_tick = current_tick;
            item->timer_started = true;
            continue;
        }

        if ((uint32_t)(current_tick - item->last_tick) >= item->period_ms)
        {
            item->last_tick = current_tick;
            led_state ^= (uint16_t)(1U << i);
            --item->toggles_left;
        }
    }
}

static inline SetLedStateCommand formSetLedStateCommand(uint8_t frame_number)
{
    SetLedStateCommand command = {{0}};

    // формирование 1 байта запроса
    command.data[0] = (uint8_t)(
        (frame_number & 0x03U) |
        ((led_state >> 4) & 0x70U)
    );

    command.data[1] = (uint8_t)(led_state & 0xFFU);
    command.data[2] = crc(command.data, 2U);

    return command;
}

void send_SetLedState(USART_TypeDef *USARTx)
{
    LedControllerContext *controller = get_controller(USARTx);

    if (controller == NULL)
    {
        return;
    }

    send_command_sync(USARTx, formSetLedStateCommand(controller->frame_number));
}

static inline void write_events(LedControllerContext *controller, RawResponce response)
{
    uint32_t data = (uint32_t)response.data[2] |
                    ((uint32_t)response.data[1] << 8) |
                    ((uint32_t)((response.data[0] >> 3) & 0x0FU) << 16);

    controller->button_events.S1  = (ButtonEvent)((data >> 0) & 0x03U);
    controller->button_events.S2  = (ButtonEvent)((data >> 2) & 0x03U);
    controller->button_events.S3  = (ButtonEvent)((data >> 4) & 0x03U);
    controller->button_events.S4  = (ButtonEvent)((data >> 6) & 0x03U);
    controller->button_events.S5  = (ButtonEvent)((data >> 8) & 0x03U);
    controller->button_events.S6  = (ButtonEvent)((data >> 10) & 0x03U);
    controller->button_events.S7  = (ButtonEvent)((data >> 12) & 0x03U);
    controller->button_events.S8  = (ButtonEvent)((data >> 14) & 0x03U);
    controller->button_events.S9  = (ButtonEvent)((data >> 16) & 0x03U);
    controller->button_events.S10 = (ButtonEvent)((data >> 18) & 0x03U);
}

static inline ButtonStatus take_button_events(LedControllerContext *controller, USART_TypeDef *USARTx)
{
    ButtonStatus status = {0};

    if (controller->exchange_state != LED_EXCHANGE_EVENTS_PENDING)
    {
        return status;
    }

    status.ready = 1U;
    status.active_remote = USARTx;

    status.S1 = controller->button_events.S1;
    status.S2 = controller->button_events.S2;
    status.S3 = controller->button_events.S3;
    status.S4 = controller->button_events.S4;
    status.S5 = controller->button_events.S5;
    status.S6 = controller->button_events.S6;
    status.S7 = controller->button_events.S7;
    status.S8 = controller->button_events.S8;
    status.S9 = controller->button_events.S9;
    status.S10 = controller->button_events.S10;

    controller->frame_number = (uint8_t)((controller->frame_number + 1U) & 0x03U);
    controller->exchange_state = LED_EXCHANGE_IDLE;

    return status;
}

/*
 * Один общий обработчик комбинаций в main.c. Берём ответы с обоих
 * пультов, но в button_combo передаём только выбранный источник.
 * Одновременная работа обоих пультов на этом этапе не поддерживается.
 */
ButtonStatus poll_button_events(void)
{
    if (controllers[0].exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        active_remote = USART2;
        return take_button_events(&controllers[0], USART2);
    }

    if (controllers[1].exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        active_remote = UART4;
        return take_button_events(&controllers[1], UART4);
    }

    return (ButtonStatus){0};
}

USART_TypeDef *led_controller_active_usart(void)
{
    return active_remote;
}

void LedController_PeriphInit(void)
{
    led_controller_usart_init();
    TIM6_Init();
}

uint8_t can_send_command(void)
{
    if (is_send_command)
    {
        is_send_command = 0U;
        return 1U;
    }

    return 0U;
}

void tim6_handler(void)
{
    is_send_command = 1U;
    tim6_callback();
}

__attribute__((weak)) void tim6_callback(void)
{
}

// Проверка номера кадра и CRC полученного ответа
static inline bool is_valid_response(RawResponce *response, uint8_t frame_number)
{
    if ((response->data[0] & 0x03U) != frame_number)
    {
        return false;
    }

    return response->data[3] == crc(response->data, 3U);
}

// Обработка неудачного запроса
static inline void register_failed_request(LedControllerContext *controller)
{
    if (controller->link_state == LED_LINK_NORMAL)
    {
        controller->failed_requests++;

        if (controller->failed_requests < LED_MAX_FAILED_REQUESTS)
        {
            return;
        }

        controller->link_state = LED_LINK_RECOVERY;
    }

    controller->frame_number = (uint8_t)((controller->frame_number + 1U) & 0x03U);
}

// Обслуживание одного пульта
static inline void process_controller(USART_TypeDef *USARTx, LedControllerContext *controller,
                                      uint32_t current_tick, uint8_t send_tick)
{
    RawResponce response;

    if (controller->exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        return;
    }

    if (controller->exchange_state == LED_EXCHANGE_WAIT_RESPONSE)
    {
        if (led_raw_take_response(USARTx, &response))
        {
            if (is_valid_response(&response, controller->frame_number))
            {
                if (controller->link_state == LED_LINK_RECOVERY)
                {
                    printf("RECOVERY OK: Remote %u, attempt %lu, frame %u\r\n",
                        USARTx == USART2 ? 1U : 2U,
                        (unsigned long)controller->recovery_attempts,
                        (unsigned)controller->frame_number
                    );
                }

                write_events(controller, response);

                controller->failed_requests = 0U;
                controller->recovery_attempts = 0U;
                controller->link_state = LED_LINK_NORMAL;
                controller->exchange_state = LED_EXCHANGE_EVENTS_PENDING;

                return;
            }
            if (response.data[3] != crc(response.data, 3U))
            {
                controller->crc_errors++;
            }

            if ((response.data[0] & 0x03U) != controller->frame_number)
            {
                controller->frame_errors++;
            }
        }
        else if ((uint32_t)(current_tick - controller->request_tick) < LED_RESPONSE_TIMEOUT_MS)
        {
            return;
        }

        // Нет валидного ответа
        led_raw_cancel_response(USARTx);
        register_failed_request(controller);

        controller->exchange_state = LED_EXCHANGE_IDLE;
    }

    if (!send_tick)
    {
        return;
    }

    controller->request_tick = current_tick;
    controller->exchange_state = LED_EXCHANGE_WAIT_RESPONSE;

    bool add_padding = false;

    if (controller->link_state == LED_LINK_RECOVERY)
    {
        controller->recovery_attempts++;

        if (controller->recovery_attempts > 1U &&
            (controller->recovery_attempts - 1U) % LED_RECOVERY_SHIFT_PERIOD == 0U)
        {
            add_padding = true;
        }

        if (controller->recovery_attempts % LED_RECOVERY_LOG_PERIOD == 0U)
        {
            LedRawDiagnostics diagnostics;

            led_raw_get_diagnostics(USARTx, &diagnostics);

            uint32_t phase = ((controller->recovery_attempts - 1U) /
                LED_RECOVERY_SHIFT_PERIOD) % 3U;

            printf("RECOVERY: Remote %u, attempt %lu, frame %u, phase %lu\r\n",
                USARTx == USART2 ? 1U : 2U,
                (unsigned long)controller->recovery_attempts,
                (unsigned)controller->frame_number,
                (unsigned long)phase
            );

            printf("RX=%lu PACKETS=%lu CRC=%lu FRAME=%lu ORE=%lu FE=%lu NE=%lu\r\n",
                (unsigned long)diagnostics.rx_bytes,
                (unsigned long)diagnostics.packets,
                (unsigned long)controller->crc_errors,
                (unsigned long)controller->frame_errors,
                (unsigned long)diagnostics.ore_errors,
                (unsigned long)diagnostics.fe_errors,
                (unsigned long)diagnostics.ne_errors
            );
        }
    }

    if (add_padding)
    {
        SetLedStateCommand command = formSetLedStateCommand(controller->frame_number);
        send_command_sync_padded(USARTx, command);
    }
    else
    {
        send_SetLedState(USARTx);
    }

}

void led_controller_process(uint32_t current_tick)
{
    uint8_t send_tick = can_send_command();

    process_controller(USART2, &controllers[0], current_tick, send_tick);
    process_controller(UART4, &controllers[1], current_tick, send_tick);
}
