#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "led_raw.h"

#include <stm32f405xx.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_usart.h>
#include <stm32f4xx_ll_tim.h>

#define RESPONSE_LENGTH      4U
#define REMOTE_COUNT          2U
#define LED_REMOTE_BAUDRATE  38400U
#define RECOVERY_PADDING_BYTE 0xFFU

typedef struct
{
    RawResponce response;
    volatile uint8_t response_ready;
    volatile uint8_t rx_enabled;
    volatile uint8_t response_idx;
    volatile uint32_t rx_counter;

    volatile uint32_t rx_bytes;
    volatile uint32_t packets;
    volatile uint32_t ore_errors;
    volatile uint32_t fe_errors;
    volatile uint32_t ne_errors;
} LedRawContext;

static LedRawContext raw_contexts[REMOTE_COUNT];

static inline LedRawContext *get_raw_context(USART_TypeDef *USARTx)
{
    if (USARTx == USART2)
    {
        return &raw_contexts[0];
    }

    if (USARTx == UART4)
    {
        return &raw_contexts[1];
    }

    return NULL;
}

static void send_sync(USART_TypeDef *USARTx, const uint8_t *raw, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(USARTx)) {}

        LL_USART_TransmitData8(USARTx, raw[i]);
    }

    while (!LL_USART_IsActiveFlag_TC(USARTx)) {}

    LL_USART_ClearFlag_TC(USARTx);
}

static void send_command_internal(USART_TypeDef *USARTx, SetLedStateCommand cmd, bool add_padding)
{
    LedRawContext *context = get_raw_context(USARTx);

    if (context == NULL || context->rx_enabled || context->response_ready)
    {
        return;
    }

    uint8_t tx_buffer[4];
    uint8_t tx_length = 3U;

    const uint8_t *tx_data = cmd.data;

    if (add_padding)
    {
        tx_buffer[0] = RECOVERY_PADDING_BYTE;
        tx_buffer[1] = cmd.data[0];
        tx_buffer[2] = cmd.data[1];
        tx_buffer[3] = cmd.data[2];

        tx_data = tx_buffer;
        tx_length = 4U;
    }

    context->rx_enabled = 0U;
    context->response_idx = 0U;

    LL_USART_SetTransferDirection(USARTx, LL_USART_DIRECTION_TX);

    send_sync(USARTx, tx_data, tx_length);

    context->rx_enabled = 1U;
    LL_USART_SetTransferDirection(USARTx, LL_USART_DIRECTION_RX);
}

void send_command_sync(USART_TypeDef *USARTx, SetLedStateCommand cmd)
{
    send_command_internal(USARTx, cmd, false);
}

void send_command_sync_padded(USART_TypeDef *USARTx, SetLedStateCommand cmd)
{
    send_command_internal(USARTx, cmd, true);
}

static inline void process_rx_byte(LedRawContext *context, uint8_t byte)
{
    if (!context->rx_enabled || context->response_ready)
    {
        return;
    }

    context->response.data[context->response_idx] = byte;
    context->response_idx++;
    context->rx_counter++;

    if (context->response_idx == RESPONSE_LENGTH)
    {
        context->packets++;
        context->rx_enabled = 0U;
        context->response_ready = 1U;
    }
}

static inline void process_usart_irq(USART_TypeDef *USARTx, LedRawContext *context)
{
    uint8_t ore = LL_USART_IsActiveFlag_ORE(USARTx);
    uint8_t fe = LL_USART_IsActiveFlag_FE(USARTx);
    uint8_t ne = LL_USART_IsActiveFlag_NE(USARTx);

    if (ore)
    {
        context->ore_errors++;
    }

    if (fe)
    {
        context->fe_errors++;
    }

    if (ne)
    {
        context->ne_errors++;
    }

    if (LL_USART_IsActiveFlag_RXNE(USARTx))
    {
        uint8_t byte = LL_USART_ReceiveData8(USARTx);

        context->rx_bytes++;

        process_rx_byte(context, byte);
    }
    else if (ore || fe || ne)
    {
        LL_USART_ClearFlag_ORE(USARTx);
    }
}

void usart2_handler(void)
{
    process_usart_irq(USART2, &raw_contexts[0]);
}

void uart4_handler(void)
{
    process_usart_irq(UART4, &raw_contexts[1]);
}

bool led_raw_take_response(USART_TypeDef *USARTx, RawResponce *response)
{
    LedRawContext *context = get_raw_context(USARTx);

    if (context == NULL || response == NULL || !context->response_ready)
    {
        return false;
    }

    /* При response_ready == 1 приём этого канала уже остановлен в ISR. */
    *response = context->response;

    context->response_ready = 0U;
    context->response_idx = 0U;

    return true;
}

void led_raw_cancel_response(USART_TypeDef *USARTx)
{
    LedRawContext *context = get_raw_context(USARTx);

    if (context == NULL)
    {
        return;
    }

    context->rx_enabled = 0U;
    context->response_ready = 0U;
    context->response_idx = 0U;
}

// ------------------ Инициализация периферии ---------------------------
void led_controller_nvic_init(void)
{
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);

    NVIC_SetPriority(UART4_IRQn, 1);
    NVIC_EnableIRQ(UART4_IRQn);

    // Приоритет TIM6 должен быть ниже приоритета USART2 и UART4.
    NVIC_SetPriority(TIM6_DAC_IRQn, 2);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

void led_controller_gpio_init(void)
{
    // Настроить PA2(TX) - USART2 для режима half duplex
    {
        LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
        
        GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
        GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
        GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
        GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;   
        GPIO_InitStruct.Alternate = LL_GPIO_AF_7;   
        
        LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    // Настраиваем PA0(TX) - UART4 для режима half duplex
    {
        LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
        
        GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
        GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
        GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;     
        GPIO_InitStruct.Alternate = LL_GPIO_AF_8;    
        
        LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void led_controller_usart_init(void)
{
    // Настроить USART2 - в режиме half duplex
    {
        LL_USART_InitTypeDef usart_2 = {0};
        usart_2.BaudRate            = LED_REMOTE_BAUDRATE;
        usart_2.DataWidth           = LL_USART_DATAWIDTH_8B; 
        usart_2.StopBits            = LL_USART_STOPBITS_1;
        usart_2.Parity              = LL_USART_PARITY_NONE; 
        usart_2.TransferDirection = LL_USART_DIRECTION_RX; 
        usart_2.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
        usart_2.OverSampling        = LL_USART_OVERSAMPLING_16;

        LL_USART_Init(USART2, &usart_2);
    
        LL_USART_EnableHalfDuplex(USART2);

        LL_USART_EnableIT_RXNE(USART2);
        LL_USART_Enable(USART2);
    }
    // Настроить UART4 - в режиме half duplex
    {
        LL_USART_InitTypeDef usart_4 = {0};
        usart_4.BaudRate            = LED_REMOTE_BAUDRATE;
        usart_4.DataWidth           = LL_USART_DATAWIDTH_8B; 
        usart_4.StopBits            = LL_USART_STOPBITS_1;
        usart_4.Parity              = LL_USART_PARITY_NONE; 
        usart_4.TransferDirection   = LL_USART_DIRECTION_RX;
        usart_4.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
        usart_4.OverSampling        = LL_USART_OVERSAMPLING_16;

        LL_USART_Init(UART4, &usart_4);
        
        LL_USART_EnableHalfDuplex(UART4);

        LL_USART_EnableIT_RXNE(UART4);
        LL_USART_Enable(UART4);
    }
}
void led_controller_clock_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_UART4);
}

void TIM6_Init(void)
{
    LL_TIM_InitTypeDef TIM_InitStruct = {0};

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

    TIM_InitStruct.Prescaler = 8399;
    TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
    TIM_InitStruct.Autoreload = 99;
    TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;

    LL_TIM_Init(TIM6, &TIM_InitStruct);

    LL_TIM_EnableARRPreload(TIM6);
    LL_TIM_ClearFlag_UPDATE(TIM6);
    LL_TIM_EnableIT_UPDATE(TIM6);

    LL_TIM_EnableCounter(TIM6);
}
void led_raw_get_diagnostics(USART_TypeDef *USARTx, LedRawDiagnostics *diagnostics)
{
    if (diagnostics == NULL)
    {
        return;
    }

    LedRawContext *context = get_raw_context(USARTx);

    if (context == NULL)
    {
        *diagnostics = (LedRawDiagnostics){0};
        return;
    }

    diagnostics->rx_bytes = context->rx_bytes;
    diagnostics->packets = context->packets;
    diagnostics->ore_errors = context->ore_errors;
    diagnostics->fe_errors = context->fe_errors;
    diagnostics->ne_errors = context->ne_errors;
}