#include "stddef.h"
#include <string.h>
#include "led_controller.h"
#include "stdbool.h"
#include <stm32f405xx.h>
#include <stm32f4xx_ll_gpio.h>

#include "rxlog/rx_log.h"
#include "events_buffer/events_buffer.h"
#include <stm32f4xx_ll_bus.h>


#define TX_BUF_SIZE 256
uint8_t tx_buffer[TX_BUF_SIZE];
volatile uint8_t echo_skip = 0;

volatile bool is_transmit_active = false;
volatile uint16_t tx_index = 0;   
volatile uint16_t tx_total = 0;  

volatile uint8_t frame_number = 0;
static void inc_frame_number();

RawResponce responce;
LedEvents led_events = {0};
uint8_t responce_idx = 0;

void send_sync(USART_TypeDef *USARTx, const uint8_t* raw, uint8_t len)
{
    if (raw == NULL || len == 0) {
        return;
    }

    responce_idx = 0; 
    // при полудуплексной передаче байты приходят обратно
    echo_skip = len;

    for (uint16_t i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(USARTx)){}

        LL_USART_TransmitData8(USARTx, raw[i]);
    }

    while (!LL_USART_IsActiveFlag_TC(USARTx)){}

    LL_USART_ClearFlag_TC(USARTx);
}

void send_command(USART_TypeDef *USARTx, SetLedStateCommand cmd)
{
    send_sync(USARTx, cmd.data, 3);
}

void process_rx_byte(uint8_t byte)
{
    responce.data[responce_idx] = byte;

    responce_idx++;

    // команда принята полностью
    if(responce_idx == 4)
    {
        uint16_t raw_led_data = responce.data[1] | ((uint16_t)responce.data[2] << 8);

        led_events = parse_events(raw_led_data);
        inc_frame_number();
        responce_idx = 0;


    }
}

void usart2_handler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(USART2))
    {
        uint8_t byte = LL_USART_ReceiveData8(USART2);
        rx_log_write(byte);

        if (echo_skip > 0)
        {
            echo_skip--;
            return; 
        }
        process_rx_byte(byte);
    }

    if (LL_USART_IsActiveFlag_ORE(USART2))
    {
        LL_USART_ClearFlag_ORE(USART2);
    }

}
void uart4_handler()
{
    // TODO: доделать
}

void led_controller_nvic_init()
{
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);

    NVIC_SetPriority(UART4_IRQn, 1);
    NVIC_EnableIRQ(UART4_IRQn);
}

void led_controller_gpio_init()
{
    // Настроить PA2(TX) - USART2 для режима half duplex
    {
        LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
        
        GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
        GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;    
        GPIO_InitStruct.Alternate = LL_GPIO_AF_7;   
        
        LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    // Настраиваем PA0(TX) - UART4 для режима half duplex
    {
        LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
        
        GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
        GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;       
        GPIO_InitStruct.Alternate = LL_GPIO_AF_8;    
        
        LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void led_controller_usart_init(void)
{
    // Настроить USART2 - в режиме half duplex
    {
        LL_USART_InitTypeDef usart_2 = {0};
        usart_2.BaudRate            = 38400;
        usart_2.DataWidth           = LL_USART_DATAWIDTH_8B; 
        usart_2.StopBits            = LL_USART_STOPBITS_1;
        usart_2.Parity              = LL_USART_PARITY_NONE; 
        usart_2.TransferDirection   = LL_USART_DIRECTION_TX_RX; 
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
        usart_4.BaudRate            = 38400;
        usart_4.DataWidth           = LL_USART_DATAWIDTH_8B; 
        usart_4.StopBits            = LL_USART_STOPBITS_1;
        usart_4.Parity              = LL_USART_PARITY_NONE; 
        usart_4.TransferDirection   = LL_USART_DIRECTION_TX_RX;
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

void inc_frame_number()
{
    frame_number++;
    if (frame_number > 3) 
    {
        frame_number = 0;
    }
}