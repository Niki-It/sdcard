#include "i2s2.h"
#include "stm32f4xx_ll_spi.h"

#include <stdint.h>
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx.h"
#include "SEGGER_RTT.h"

#define I2S_BUFFER_SAMPLES  1024U

static uint16_t i2s_tx_buffer[I2S_BUFFER_SAMPLES];
static uint16_t i2s_rx_buffer[I2S_BUFFER_SAMPLES];

static volatile uint8_t i2s_tx_done = 0;
static volatile uint8_t i2s_rx_done = 0;
static volatile uint8_t i2s_tx_error = 0;
static volatile uint8_t i2s_rx_error = 0;
static volatile uint8_t i2s_transfer_active = 0;

// DMA1 Stream 3 / Channel 0: SPI2_RX
void i2s_dma_init_rx(void)
{
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3))
    {}

    LL_DMA_SetChannelSelection(DMA1,
                               LL_DMA_STREAM_3,
                               LL_DMA_CHANNEL_0);

    LL_DMA_ConfigTransfer(
        DMA1,
        LL_DMA_STREAM_3,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
        LL_DMA_MODE_NORMAL |
        LL_DMA_PERIPH_NOINCREMENT |
        LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PDATAALIGN_HALFWORD |
        LL_DMA_MDATAALIGN_HALFWORD |
        LL_DMA_PRIORITY_HIGH
    );

    LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_3);

    LL_DMA_ConfigAddresses(
        DMA1,
        LL_DMA_STREAM_3,
        LL_SPI_DMA_GetRegAddr(SPI2),
        (uint32_t)i2s_rx_buffer,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY
    );

    LL_DMA_SetDataLength(DMA1,
                         LL_DMA_STREAM_3,
                         I2S_BUFFER_SAMPLES);

    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_HT3(DMA1);
    LL_DMA_ClearFlag_TE3(DMA1);
    LL_DMA_ClearFlag_DME3(DMA1);
    LL_DMA_ClearFlag_FE3(DMA1);

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_3);

    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}
// DMA1 Stream 4 / Channel 2: I2S2ext_TX
void i2s_dma_init_tx(void)
{
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {}

    LL_DMA_SetChannelSelection(DMA1,
                               LL_DMA_STREAM_4,
                               LL_DMA_CHANNEL_2);

    LL_DMA_ConfigTransfer(
        DMA1,
        LL_DMA_STREAM_4,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
        LL_DMA_MODE_NORMAL |
        LL_DMA_PERIPH_NOINCREMENT |
        LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PDATAALIGN_HALFWORD |
        LL_DMA_MDATAALIGN_HALFWORD |
        LL_DMA_PRIORITY_HIGH
    );

    LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_4);

    LL_DMA_ConfigAddresses(
        DMA1,
        LL_DMA_STREAM_4,
        (uint32_t)i2s_tx_buffer,
        LL_SPI_DMA_GetRegAddr(I2S2ext),
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH
    );

    LL_DMA_SetDataLength(DMA1,
                         LL_DMA_STREAM_4,
                         I2S_BUFFER_SAMPLES);

    LL_DMA_ClearFlag_TC4(DMA1);
    LL_DMA_ClearFlag_HT4(DMA1);
    LL_DMA_ClearFlag_TE4(DMA1);
    LL_DMA_ClearFlag_DME4(DMA1);
    LL_DMA_ClearFlag_FE4(DMA1);

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_4);

    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}


// Общая инициализация I2S2 + I2S2ext + DMA.
void i2s_dma_init(void)
{
    // GPIO и PLLI2S должны быть настроены до этого вызова.

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    LL_I2S_InitTypeDef i2s_init = {0};

    i2s_init.Mode          = LL_I2S_MODE_MASTER_RX;
    i2s_init.Standard      = LL_I2S_STANDARD_PHILIPS;
    i2s_init.DataFormat    = LL_I2S_DATAFORMAT_16B;
    i2s_init.MCLKOutput    = LL_I2S_MCLK_OUTPUT_ENABLE;
    i2s_init.AudioFreq     = LL_I2S_AUDIOFREQ_48K;
    i2s_init.ClockPolarity = LL_I2S_POLARITY_LOW;

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_I2S_Init(SPI2, &i2s_init);

    LL_I2S_ConfigPrescaler(SPI2,
                           3,
                           LL_I2S_PRESCALER_PARITY_ODD);

    // Автоматически настраивает расширенный блок как Slave TX.
    LL_I2S_InitFullDuplex(I2S2ext, &i2s_init);

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_DisableIT_RXNE(SPI2);
    LL_SPI_DisableIT_TXE(I2S2ext);

    i2s_dma_init_rx();
    i2s_dma_init_tx();
}

void i2s_stop(void)
{
    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
    }

    i2s_transfer_active = 0;
}
void DMA1_Stream3_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TE3(DMA1) ||
        LL_DMA_IsActiveFlag_DME3(DMA1))
    {
        LL_DMA_ClearFlag_TE3(DMA1);
        LL_DMA_ClearFlag_DME3(DMA1);

        i2s_rx_error = 1;
    }

    if (LL_DMA_IsActiveFlag_TC3(DMA1))
    {
        LL_DMA_ClearFlag_TC3(DMA1);

        i2s_rx_done = 1;
    }
}


void DMA1_Stream4_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TE4(DMA1) ||
        LL_DMA_IsActiveFlag_DME4(DMA1))
    {
        LL_DMA_ClearFlag_TE4(DMA1);
        LL_DMA_ClearFlag_DME4(DMA1);

        i2s_tx_error = 1;
    }

    if (LL_DMA_IsActiveFlag_TC4(DMA1))
    {
        LL_DMA_ClearFlag_TC4(DMA1);

        i2s_tx_done = 1;
    }
}
// Подготавливаем RX DMA, но пока не запускаем I2S.
int i2s_rx_start_block(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3))
    {
    }

    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_HT3(DMA1);
    LL_DMA_ClearFlag_TE3(DMA1);
    LL_DMA_ClearFlag_DME3(DMA1);
    LL_DMA_ClearFlag_FE3(DMA1);

    LL_DMA_SetDataLength(DMA1,
                         LL_DMA_STREAM_3,
                         I2S_BUFFER_SAMPLES);

    i2s_rx_done = 0;
    i2s_rx_error = 0;

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_3);

    return 0;
}


// Подготавливаем TX DMA, но пока не запускаем I2S.
int i2s_tx_start_block(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
    }

    LL_DMA_ClearFlag_TC4(DMA1);
    LL_DMA_ClearFlag_HT4(DMA1);
    LL_DMA_ClearFlag_TE4(DMA1);
    LL_DMA_ClearFlag_DME4(DMA1);
    LL_DMA_ClearFlag_FE4(DMA1);

    LL_DMA_SetDataLength(DMA1,
                         LL_DMA_STREAM_4,
                         I2S_BUFFER_SAMPLES);

    i2s_tx_done = 0;
    i2s_tx_error = 0;

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_4);

    return 0;
}


// Запускает подготовленные RX и TX одновременно.
int i2s_start_blocks(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    uint32_t rx_enabled = LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3);
    uint32_t tx_enabled = LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4);

    // Хотя бы один DMA должен быть включён.
    if (!rx_enabled && !tx_enabled)
    {
        return -1;
    }

    i2s_transfer_active = 1;

    if (rx_enabled)
    {
        LL_SPI_EnableDMAReq_RX(SPI2);
    }

    if (tx_enabled)
    {
        LL_SPI_EnableDMAReq_TX(I2S2ext);
    }

    // Сначала Slave, затем Master.
    LL_I2S_Enable(I2S2ext);
    LL_I2S_Enable(SPI2);

    return 0;
}


void i2s_test_rx(void)
{
    // Заполняем маркером для проверки работы DMA.
    for (uint32_t i = 0; i < I2S_BUFFER_SAMPLES; i++)
    {
        i2s_rx_buffer[i] = 0xA55A;
    }

    if (i2s_rx_start_block() != 0)
    {
        SEGGER_RTT_printf(0, "I2S RX DMA start error\r\n");
        return;
    }

    if (i2s_start_blocks() != 0)
    {
        SEGGER_RTT_printf(0, "I2S RX start error\r\n");
        i2s_stop();
        return;
    }

    // Ждём завершения DMA.
    while (!i2s_rx_done && !i2s_rx_error)
    {
    }

    i2s_stop();

    if (i2s_rx_error)
    {
        SEGGER_RTT_printf(0, "I2S RX DMA error\r\n");
        return;
    }

    SEGGER_RTT_printf(0, "I2S RX complete\r\n");

    // Выводим 1024 слова, по 20 в строке.
    for (uint32_t i = 0; i < I2S_BUFFER_SAMPLES; i++)
    {
        SEGGER_RTT_printf(0,
                          "%d ",
                          (int16_t)i2s_rx_buffer[i]);

        if ((i + 1U) % 20U == 0U)
        {
            SEGGER_RTT_printf(0, "\r\n");
        }
    }

    if (I2S_BUFFER_SAMPLES % 20U != 0U)
    {
        SEGGER_RTT_printf(0, "\r\n");
    }
}

#define I2S_TEST_AMPLITUDE        8000
#define I2S_TEST_PERIOD_FRAMES     48U

void i2s_test_tx(void)
{
    // Генерируем тестовый сигнал 1 кГц, стерео.
    for (uint32_t i = 0; i < I2S_BUFFER_SAMPLES; i += 2U)
    {
        uint32_t frame = i / 2U;

        int16_t sample;

        if ((frame % I2S_TEST_PERIOD_FRAMES) <
            (I2S_TEST_PERIOD_FRAMES / 2U))
        {
            sample = I2S_TEST_AMPLITUDE;
        }
        else
        {
            sample = -I2S_TEST_AMPLITUDE;
        }

        i2s_tx_buffer[i]     = (uint16_t)sample;
        i2s_tx_buffer[i + 1] = (uint16_t)sample;
    }

    if (i2s_tx_start_block() != 0)
    {
        SEGGER_RTT_printf(0, "I2S TX DMA start error\r\n");
        return;
    }

    if (i2s_start_blocks() != 0)
    {
        SEGGER_RTT_printf(0, "I2S TX start error\r\n");
        i2s_stop();
        return;
    }

    // Ждём завершения DMA TX.
    while (!i2s_tx_done && !i2s_tx_error)
    {
    }

    if (i2s_tx_error)
    {
        i2s_stop();

        SEGGER_RTT_printf(0, "I2S TX DMA error\r\n");
        return;
    }

    // DMA закончил передачу в DR.
    // Не останавливаем I2S немедленно:
    // последнее слово ещё может передаваться по линии.
    // Здесь для теста используем ожидание окончания SPI/I2S.
    while (LL_SPI_IsActiveFlag_BSY(I2S2ext))
    {}

    i2s_stop();

    SEGGER_RTT_printf(0, "I2S TX complete\r\n");
}




// ----------- Легаси -------------------
#define TX_BUFFER_SIZE 32
#define RX_BUFFER_SIZE 32

static uint16_t txBuffer[TX_BUFFER_SIZE];
static volatile uint16_t rxBuffer[RX_BUFFER_SIZE];

static volatile uint32_t txIndex = 0;
static volatile uint32_t rxIndex = 0;

static volatile uint32_t rxCount = 0;
static volatile uint32_t rxOverrunCount = 0;

void I2S2_Init(void)
{
    LL_I2S_InitTypeDef I2S_InitStruct = {0};

    // Параметры I2S
    I2S_InitStruct.Mode          = LL_I2S_MODE_MASTER_RX;
    I2S_InitStruct.Standard      = LL_I2S_STANDARD_PHILIPS;
    I2S_InitStruct.DataFormat    = LL_I2S_DATAFORMAT_16B;
    I2S_InitStruct.MCLKOutput    = LL_I2S_MCLK_OUTPUT_ENABLE;
    I2S_InitStruct.AudioFreq     = LL_I2S_AUDIOFREQ_48K;
    I2S_InitStruct.ClockPolarity = LL_I2S_POLARITY_LOW;

    // SPI2 - Master RX
    LL_I2S_Init(SPI2, &I2S_InitStruct);

    // Точная подстройка предделителя
    LL_I2S_ConfigPrescaler(SPI2, 3, LL_I2S_PRESCALER_PARITY_ODD);

    // I2S2ext - Slave TX
    LL_I2S_InitFullDuplex(I2S2ext, &I2S_InitStruct);
}


void I2S2_StartTransmitIT(void)
{
    // Заполняем буфер передачи тестовыми данными
    for (uint32_t i = 0; i < TX_BUFFER_SIZE; i++)
    {
        txBuffer[i] = (i * 1000) & 0xFFFF;
    }

    txIndex = 0;
    rxIndex = 0;
    rxCount = 0;
    rxOverrunCount = 0;

    // Загружаем первое слово в передатчик I2S2ext
    LL_SPI_TransmitData16(I2S2ext, txBuffer[txIndex++]);

    // Включаем прерывания
    LL_I2S_EnableIT_RXNE(SPI2);
    LL_SPI_EnableIT_TXE(I2S2ext);

    // Запускаем сначала Slave TX, затем Master RX
    LL_I2S_Enable(I2S2ext);
    LL_I2S_Enable(SPI2);
}


void I2S2_Callback(void)
{
    // Приём данных: SPI2 MASTER_RX
    if (LL_I2S_IsActiveFlag_RXNE(SPI2))
    {
        uint16_t rx_data = LL_SPI_ReceiveData16(SPI2);

        rxBuffer[rxIndex] = rx_data;
        rxIndex++;

        if (rxIndex >= RX_BUFFER_SIZE)
        {
            rxIndex = 0;
        }

        rxCount++;
    }

    // Передача данных: I2S2ext SLAVE_TX
    if (LL_SPI_IsActiveFlag_TXE(I2S2ext))
    {
        LL_SPI_TransmitData16(I2S2ext, txBuffer[txIndex++]);

        if (txIndex >= TX_BUFFER_SIZE)
        {
            txIndex = 0;
        }
    }

    // Переполнение приёмника SPI2
    if (LL_SPI_IsActiveFlag_OVR(SPI2))
    {
        volatile uint16_t data = LL_SPI_ReceiveData16(SPI2);
        volatile uint32_t status = SPI2->SR;

        (void)data;
        (void)status;

        rxOverrunCount++;
    }
}