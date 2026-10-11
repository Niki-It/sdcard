#include "i2s2.h"
#include "stm32f4xx_ll_spi.h"

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