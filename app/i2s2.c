#include "i2s2.h"
#include "stm32f4xx_ll_spi.h"

void I2S2_Init() {
    // Параметры i2s
    LL_I2S_InitTypeDef I2S_InitStruct = {0};
    I2S_InitStruct.Mode           = LL_I2S_MODE_MASTER_TX;       // Master Transmitter
    I2S_InitStruct.Standard       = LL_I2S_STANDARD_PHILIPS;     // Стандарт I2S
    I2S_InitStruct.DataFormat     = LL_I2S_DATAFORMAT_16B;       // 16 бит
    I2S_InitStruct.MCLKOutput     = LL_I2S_MCLK_OUTPUT_ENABLE;   // Включаем выход MCLK на PC6
    I2S_InitStruct.AudioFreq      = LL_I2S_AUDIOFREQ_48K;        // Запрос 48 кГц
    I2S_InitStruct.ClockPolarity  = LL_I2S_POLARITY_LOW;         // Низкая полярность
    LL_I2S_Init(SPI2, &I2S_InitStruct);

    // Точная подстройка предделителя (Табличные значения из Reference Manual)
    LL_I2S_ConfigPrescaler(SPI2, 3, LL_I2S_PRESCALER_PARITY_ODD);

    // Запуск
    LL_I2S_Enable(SPI2);
}

#define TX_BUFFER_SIZE  32
static uint16_t txBuffer[TX_BUFFER_SIZE];

void I2S2_StartTransmitIT(void) {
    for (int i = 0; i < TX_BUFFER_SIZE; i++)
        txBuffer[i] = (i * 1000) & 0xFFFF;

    // Включаем прерывание по завершению передачи (TXE)
    LL_SPI_EnableIT_TXE(SPI2);
    // Отправляем первый байт, чтобы запустить процесс
    LL_SPI_TransmitData16(SPI2, txBuffer[0]);
    // Остальные байты будут отправляться в обработчике прерывания
}

void I2S2_Callback() {
    static uint32_t idx = 1;
    if (LL_SPI_IsActiveFlag_TXE(SPI2)) {
        if (idx < TX_BUFFER_SIZE) {
            LL_SPI_TransmitData16(SPI2, txBuffer[idx++]);
        } else {
            idx = 0; // перезапуск
        }
    }
}