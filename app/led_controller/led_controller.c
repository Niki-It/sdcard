
#include <stdio.h>

#include "led_controller.h"
#include "led_raw.h"
#include "customcrc8/new_crc8.h"

#define LED_COUNT                  11U
#define LED_REMOTE_COUNT           2U
#define LED_RESPONSE_TIMEOUT_MS   20U
#define LED_MAX_FAILED_REQUESTS   20U
#define LED_RECOVERY_LOG_PERIOD  100U
#define LED_RECOVERY_SHIFT_PERIOD  4U

typedef struct
{
    uint32_t last_tick;
    uint32_t period_ms;
    uint32_t toggles_left;
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

typedef struct
{
    led_exchange_state_t exchange_state;
    led_link_state_t link_state;

    uint32_t request_tick;
    uint32_t recovery_attempts;

    ButtonStatus buttons;

    uint8_t frame_number;
    uint8_t failed_requests;

    uint32_t crc_errors;
    uint32_t frame_errors;
} LedControllerContext;

// Состояния светодиодов общие для обоих пультов
static uint16_t steady_state;
static uint16_t blink_phase;
static BlinkState blinking[LED_COUNT];

// Независимые контексты обмена
static LedControllerContext controllers[LED_REMOTE_COUNT];

// Флаг TIM6
static volatile uint8_t is_send_command;


// ============================================================================
// Управление светодиодами
// ============================================================================

void LedController_Set(uint16_t mask, bool enabled)
{
    mask &= LED_ALL;

    if (enabled)
    {
        steady_state |= mask;
    }
    else
    {
        steady_state &= (uint16_t)~mask;
    }
}

void LedController_SetAll(uint16_t mask)
{
    steady_state = mask & LED_ALL;
}

uint16_t LedController_Get(uint16_t mask)
{
    return steady_state & mask & LED_ALL;
}

static inline uint16_t get_output_state(void)
{
    return (steady_state ^ blink_phase) & LED_ALL;
}


// ============================================================================
// Мигание светодиодов
// ============================================================================

void LedController_Blink(uint16_t mask, uint16_t count, uint32_t period_ms)
{
    mask &= LED_ALL;

    if (mask == 0U || count == 0U)
    {
        return;
    }

    if (period_ms == 0U)
    {
        period_ms = 1U;
    }

    for (uint8_t i = 0; i < LED_COUNT; i++)
    {
        //перебираем светодиоды (каждый это 1 бит)
        uint16_t bit = (uint16_t)(1U << i);

        // бит (светодиод) есть в маске (т.е задано для него мигание)
        if (!(mask & bit))
        {
            continue;
        }

        BlinkState *item = &blinking[i];

        *item = (BlinkState){0};

        item->period_ms = period_ms;
        item->toggles_left = (uint32_t)count * 2U - 1U;

        // Первое переключение сразу
        blink_phase |= bit;
    }
}

void LedController_StopBlink(uint16_t mask)
{
    mask &= LED_ALL;

    for (uint8_t i = 0; i < LED_COUNT; i++)
    {
        uint16_t bit = (uint16_t)(1U << i);

        if (!(mask & bit))
        {
            continue;
        }

        blinking[i] = (BlinkState){0};
        blink_phase &= (uint16_t)~bit;
    }
}

static void process_blinking(uint32_t current_tick)
{
    for (uint8_t i = 0; i < LED_COUNT; i++)
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

        uint32_t elapsed = current_tick - item->last_tick;
        uint32_t steps = elapsed / item->period_ms;

        if (steps == 0U)
        {
            continue;
        }

        uint16_t bit = (uint16_t)(1U << i);

        // Все переключения выполнены
        if (steps >= item->toggles_left)
        {
            *item = (BlinkState){0};
            blink_phase &= (uint16_t)~bit;
            continue;
        }

        item->toggles_left -= steps;
        // не берем current_tick, а задаем точно когда должно было наступить мигание (самое свежее)
        item->last_tick += steps * item->period_ms;

        // прошло нечетное кол-во интервалов
        if (steps & 1U)
        {
            blink_phase ^= bit;
        }
    }
}


// ============================================================================
// Формирование и отправка команд
// ============================================================================

static inline SetLedStateCommand form_led_command(uint8_t frame_number)
{
    SetLedStateCommand command = {{0}};

    uint16_t output = get_output_state();

    command.data[0] = (uint8_t)(
        (frame_number & 0x03U) |
        ((output >> 4) & 0x70U)
    );

    command.data[1] = (uint8_t)(output & 0xFFU);
    command.data[2] = crc(command.data, 2U);

    return command;
}

static inline void send_led_command
(
    USART_TypeDef *USARTx,
    uint8_t frame_number,
    bool add_padding
)
{
    SetLedStateCommand command = form_led_command(frame_number);

    if (add_padding)
    {
        send_command_sync_padded(USARTx, command);
    }
    else
    {
        send_command_sync(USARTx, command);
    }
}


// ============================================================================
// Обработка ответов пультов
// ============================================================================

static inline bool is_valid_response(const RawResponce *response, uint8_t frame_number)
{
    if ((response->data[0] & 0x03U) != frame_number)
    {
        return false;
    }

    return response->data[3] == crc(response->data, 3U);
}

static inline void write_events(LedControllerContext *controller,
                                RawResponce response)
{
    uint32_t data = (uint32_t)response.data[2] |
                    ((uint32_t)response.data[1] << 8) |
                    ((uint32_t)((response.data[0] >> 3) & 0x0FU) << 16);

    ButtonStatus *buttons = &controller->buttons;

    buttons->S1  = (ButtonEvent)((data >> 0) & 0x03U);
    buttons->S2  = (ButtonEvent)((data >> 2) & 0x03U);
    buttons->S3  = (ButtonEvent)((data >> 4) & 0x03U);
    buttons->S4  = (ButtonEvent)((data >> 6) & 0x03U);
    buttons->S5  = (ButtonEvent)((data >> 8) & 0x03U);
    buttons->S6  = (ButtonEvent)((data >> 10) & 0x03U);
    buttons->S7  = (ButtonEvent)((data >> 12) & 0x03U);
    buttons->S8  = (ButtonEvent)((data >> 14) & 0x03U);
    buttons->S9  = (ButtonEvent)((data >> 16) & 0x03U);
    buttons->S10 = (ButtonEvent)((data >> 18) & 0x03U);
}

static inline ButtonStatus take_button_events(uint8_t index)
{
    LedControllerContext *controller = &controllers[index];

    ButtonStatus status = controller->buttons;

    status.ready = 1U;
    status.source = index == 0U ? REMOTE_1 : REMOTE_2;

    controller->frame_number =
        (uint8_t)((controller->frame_number + 1U) & 0x03U);

    controller->exchange_state = LED_EXCHANGE_IDLE;

    return status;
}

ButtonStatus LedController_GetButtons(void)
{
    if (controllers[0].exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        return take_button_events(0);
    }

    if (controllers[1].exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        return take_button_events(1);
    }

    ButtonStatus status = {0};
    status.source = OUTSOURCE;

    return status;
}


// ============================================================================
// Восстановление связи
// ============================================================================

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

    controller->frame_number =
        (uint8_t)((controller->frame_number + 1U) & 0x03U);
}

static void print_recovery_diagnostics(USART_TypeDef *USARTx,
                                       LedControllerContext *controller)
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


// ============================================================================
// Обслуживание одного пульта
// ============================================================================

static void process_controller(
    USART_TypeDef *USARTx,
    LedControllerContext *controller,
    uint32_t current_tick,
    uint8_t send_tick
)
{
    RawResponce response;

    // Ответ ожидает обработки приложением
    if (controller->exchange_state == LED_EXCHANGE_EVENTS_PENDING)
    {
        return;
    }

    // Ожидание ответа на ранее отправленный запрос
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
        else if ((uint32_t)(current_tick - controller->request_tick) <
                 LED_RESPONSE_TIMEOUT_MS)
        {
            return;
        }

        // Ответ отсутствует или некорректен
        led_raw_cancel_response(USARTx);
        register_failed_request(controller);

        controller->exchange_state = LED_EXCHANGE_IDLE;
    }

    // Отправка разрешена только по флагу TIM6
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
            (controller->recovery_attempts - 1U) %
            LED_RECOVERY_SHIFT_PERIOD == 0U)
        {
            add_padding = true;
        }

        if (controller->recovery_attempts % LED_RECOVERY_LOG_PERIOD == 0U)
        {
            print_recovery_diagnostics(USARTx, controller);
        }
    }

    send_led_command(USARTx, controller->frame_number, add_padding);
}


// ============================================================================
// Таймер TIM6
// ============================================================================

static inline uint8_t take_send_tick(void)
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


// ============================================================================
// Инициализация и общее обслуживание
// ============================================================================

void LedController_Init(void)
{
    steady_state = 0U;
    blink_phase = 0U;
    is_send_command = 0U;

    for (uint8_t i = 0; i < LED_COUNT; i++)
    {
        blinking[i] = (BlinkState){0};
    }

    for (uint8_t i = 0; i < LED_REMOTE_COUNT; i++)
    {
        controllers[i] = (LedControllerContext){0};
    }

    led_controller_usart_init();
    TIM6_Init();
}

void LedController_Update(uint32_t current_tick)
{
    process_blinking(current_tick);

    uint8_t send_tick = take_send_tick();

    process_controller(USART2, &controllers[0], current_tick, send_tick);
    process_controller(UART4, &controllers[1], current_tick, send_tick);
}
