#include "sdio_ll_dma.h"

void sdio_ll_init_rx(void)
{
    /*
     * RX Stream
     * SDIO FIFO -> RAM
     */
    LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);

    while (LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_RX_STREAM)){}

    /* Clear RX flags */
    LL_DMA_ClearFlag_FE3(DMA2);
    LL_DMA_ClearFlag_DME3(DMA2);
    LL_DMA_ClearFlag_TE3(DMA2);
    LL_DMA_ClearFlag_HT3(DMA2);
    LL_DMA_ClearFlag_TC3(DMA2);

    LL_DMA_SetChannelSelection(
        DMA2,
        SDIO_DMA_RX_STREAM,
        SDIO_DMA_CHANNEL
    );

    LL_DMA_SetDataTransferDirection(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY
    );

    LL_DMA_SetStreamPriorityLevel(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_PRIORITY_VERYHIGH
    );


    /* SDIO FIFO address не изменяется */
    LL_DMA_SetPeriphIncMode(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_PERIPH_NOINCREMENT
    );

    /* RAM buffer увеличивается */
    LL_DMA_SetMemoryIncMode(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_MEMORY_INCREMENT
    );

    /* SDIO FIFO и RAM работают словами */
    LL_DMA_SetPeriphSize(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_PDATAALIGN_WORD
    );

    LL_DMA_SetMemorySize(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_MDATAALIGN_WORD
    );
    LL_DMA_EnableFifoMode(
        DMA2,
        SDIO_DMA_RX_STREAM
    );
    LL_DMA_SetFIFOThreshold(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_FIFOTHRESHOLD_FULL
    );

    LL_DMA_SetMode(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_MODE_PFCTRL
    );
    
    /*
     * Burst = SINGLE.
     * При FIFO OFF burst также не используем.
     */
    LL_DMA_SetPeriphBurstxfer(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_PBURST_INC4
    );

    LL_DMA_SetMemoryBurstxfer(
        DMA2,
        SDIO_DMA_RX_STREAM,
        LL_DMA_MBURST_INC4
    );
}
void sdio_ll_init_tx(void)
{
    /*
     * TX Stream
     * RAM -> SDIO FIFO
     */
    LL_DMA_DisableStream(DMA2, SDIO_DMA_TX_STREAM);

    while (LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_TX_STREAM)){}

    /* Clear TX flags */
    LL_DMA_ClearFlag_FE6(DMA2);
    LL_DMA_ClearFlag_DME6(DMA2);
    LL_DMA_ClearFlag_TE6(DMA2);
    LL_DMA_ClearFlag_HT6(DMA2);
    LL_DMA_ClearFlag_TC6(DMA2);

    LL_DMA_SetChannelSelection(
        DMA2,
        SDIO_DMA_TX_STREAM,
        SDIO_DMA_CHANNEL
    );

    LL_DMA_SetDataTransferDirection(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH
    );

    LL_DMA_SetStreamPriorityLevel(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PRIORITY_VERYHIGH
    );

    /* Один запуск DMA = одна операция записи */
    LL_DMA_SetMode(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_MODE_PFCTRL
    );

    /* SDIO FIFO address не изменяется */
    LL_DMA_SetPeriphIncMode(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PERIPH_NOINCREMENT
    );

    /* RAM buffer увеличивается */
    LL_DMA_SetMemoryIncMode(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_MEMORY_INCREMENT
    );

    /* SDIO FIFO и RAM работают словами */
    LL_DMA_SetPeriphSize(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PDATAALIGN_WORD
    );

    LL_DMA_SetMemorySize(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_MDATAALIGN_WORD
    );

    /* DMA FIFO ON */
    LL_DMA_EnableFifoMode(
        DMA2,
        SDIO_DMA_TX_STREAM
    );

    /* FIFO threshold = FULL для INCR4 */
    LL_DMA_SetFIFOThreshold(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_FIFOTHRESHOLD_FULL
    );

    /* Burst = INCR4 */
    LL_DMA_SetPeriphBurstxfer(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PBURST_INC4
    );

    LL_DMA_SetMemoryBurstxfer(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PBURST_INC4
    );
}
sd_status_t sdio_ll_dma_init(void)
{
    /* DMA2 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

    sdio_ll_init_rx();
    sdio_ll_init_tx();

    return SD_OK;
}

static sd_status_t sdio_ll_check_rx_status(void)
{
    uint32_t sta = SDIO->STA;

    if (sta & SDIO_STA_RXOVERR)
    {
        LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);
        SDIO->ICR = SDIO_ICR_RXOVERRC;
        return SD_ERR_IO;
    }

    if (sta & SDIO_STA_DTIMEOUT)
    {
        LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);
        SDIO->ICR = SDIO_ICR_DTIMEOUTC;
        return SD_ERR_TIMEOUT;
    }

    if (sta & SDIO_STA_DCRCFAIL)
    {
        LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);
        SDIO->ICR = SDIO_ICR_DCRCFAILC;
        return SD_ERR_DATA_CRC;
    }

    return SD_OK;
}
sd_status_t sdio_ll_wait_dma_rx(void)
{
    sd_status_t status;

    while (1)
    {
        /* Проверяем ошибки SDIO */
        status = sdio_ll_check_rx_status();

        if (status != SD_OK)
            return status;


        /* Проверяем ошибки DMA */
        if (LL_DMA_IsActiveFlag_TE3(DMA2) ||
            LL_DMA_IsActiveFlag_DME3(DMA2) ||
            LL_DMA_IsActiveFlag_FE3(DMA2))
        {
            SEGGER_RTT_printf(0, "DMA ERR: TE=%d DME=%d FE=%d NDTR=%lu\r\n",
                LL_DMA_IsActiveFlag_TE3(DMA2),
                LL_DMA_IsActiveFlag_DME3(DMA2),
                LL_DMA_IsActiveFlag_FE3(DMA2),
                DMA2_Stream3->NDTR);
            
                
            LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);

            LL_DMA_ClearFlag_TE3(DMA2);
            LL_DMA_ClearFlag_DME3(DMA2);
            LL_DMA_ClearFlag_FE3(DMA2);

            return SD_ERR_IO;
        }


        /* DMA закончил перенос */
        if ((SDIO->STA & SDIO_STA_DBCKEND) &&
            !LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_RX_STREAM))
        {
            status = sdio_ll_check_rx_status();
            if (status != SD_OK)
                return status;

            SDIO->ICR = SDIO_ICR_DBCKENDC |
                        SDIO_ICR_DATAENDC;

            return SD_OK;
        }
    }
}

static sd_status_t sdio_ll_check_tx_status(void)
{
    uint32_t sta = SDIO->STA;

    if (sta & SDIO_STA_TXUNDERR)
    {
        SEGGER_RTT_printf(0, "SDIO TXUNDERR: "
           "STA=0x%08lX DCTRL=0x%08lX FCR=0x%08lX "
           "DMA_CR=0x%08lX DMA_FCR=0x%08lX "
           "NDTR=%lu FIFOCNT=%lu DCOUNT=%lu\r\n",
           (unsigned long)SDIO->STA,
           (unsigned long)SDIO->DCTRL,
           (unsigned long)SDIO->FIFOCNT,
           (unsigned long)DMA2_Stream6->CR,
           (unsigned long)DMA2_Stream6->FCR,
           (unsigned long)DMA2_Stream6->NDTR,
           (unsigned long)((DMA2_Stream6->FCR >> 5) & 0x7),
           (unsigned long)SDIO->DCOUNT);
        LL_DMA_DisableStream(DMA2, SDIO_DMA_TX_STREAM);
        SDIO->ICR = SDIO_ICR_TXUNDERRC;
        
        return SD_ERR_IO;
    }

    if (sta & SDIO_STA_DTIMEOUT)
    {
        LL_DMA_DisableStream(DMA2, SDIO_DMA_TX_STREAM);
        SDIO->ICR = SDIO_ICR_DTIMEOUTC;
        return SD_ERR_TIMEOUT;
    }

    if (sta & SDIO_STA_DCRCFAIL)
    {
        LL_DMA_DisableStream(DMA2, SDIO_DMA_TX_STREAM);
        SDIO->ICR = SDIO_ICR_DCRCFAILC;
        return SD_ERR_DATA_CRC;
    }

    return SD_OK;
}

static uint32_t fe_counter;
sd_status_t sdio_ll_wait_dma_tx(void)
{
    sd_status_t status;
    fe_counter = 0;

    // Этап 1: DMA TC
    while (!LL_DMA_IsActiveFlag_TC6(DMA2))
    {
        status = sdio_ll_check_tx_status();
        if (status != SD_OK)
            return status;

        if(LL_DMA_IsActiveFlag_FE6(DMA2))
        {
            LL_DMA_ClearFlag_FE6(DMA2);
            fe_counter++;
            continue;
        }

        if (LL_DMA_IsActiveFlag_TE6(DMA2) || LL_DMA_IsActiveFlag_DME6(DMA2))
        {
            return SD_ERR_IO;
        }
    }

    // Этап 2: SDIO DATAEND
    while (!(SDIO->STA & SDIO_STA_DATAEND))
    {
        status = sdio_ll_check_tx_status();
        if (status != SD_OK)
            return status;

        if (LL_DMA_IsActiveFlag_FE6(DMA2) ||
            LL_DMA_IsActiveFlag_TE6(DMA2) ||
            LL_DMA_IsActiveFlag_DME6(DMA2))
        {
            return SD_ERR_IO;
        }
    }
    status = sdio_ll_check_tx_status();
    if (status != SD_OK)
        return status;


    // Этап 3: завершение SDIO
    SDIO->ICR = SDIO_ICR_DATAENDC;
    CLEAR_BIT(SDIO->DCTRL, SDIO_DCTRL_DTEN);
    SEGGER_RTT_printf(0,"fe counter %u\r\n", fe_counter);

    return SD_OK;
}


sd_status_t sdio_ll_prepare_dma_rx(
    uint8_t *buffer,
    uint32_t length
)
{
    uint32_t words;

    if (buffer == NULL || length == 0)
        return SD_ERR_IO;

    if ((length & 0x3U) != 0)
        return SD_ERR_IO;

    words = length / 4U;

    /* На всякий случай поток должен быть выключен */
    LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);

    while (LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_RX_STREAM)){}

    /* Очистить старые флаги */
    LL_DMA_ClearFlag_FE3(DMA2);
    LL_DMA_ClearFlag_DME3(DMA2);
    LL_DMA_ClearFlag_TE3(DMA2);
    LL_DMA_ClearFlag_HT3(DMA2);
    LL_DMA_ClearFlag_TC3(DMA2);

    /*
     * SDIO FIFO -> RAM
     */
    LL_DMA_SetPeriphAddress(
        DMA2,
        SDIO_DMA_RX_STREAM,
        (uint32_t)&SDIO->FIFO
    );

    LL_DMA_SetMemoryAddress(
        DMA2,
        SDIO_DMA_RX_STREAM,
        (uint32_t)buffer
    );

    LL_DMA_SetDataLength(
        DMA2,
        SDIO_DMA_RX_STREAM,
        words
    );

    /*
     * Теперь разрешаем DMA-запросы SDIO.
     */
    LL_DMA_EnableStream(DMA2, SDIO_DMA_RX_STREAM);
    /*
     * SDIO data path
     */
    SDIO->ICR =
        SDIO_ICR_DBCKENDC  |
        SDIO_ICR_DATAENDC  |
        SDIO_ICR_RXOVERRC  |
        SDIO_ICR_DTIMEOUTC |
        SDIO_ICR_DCRCFAILC;

    SDIO->DTIMER = 0xFFFFFFFFU;
    SDIO->DLEN = length;

    SDIO->DCTRL =
        (9U << 4) |             /* DBLOCKSIZE = 512 bytes */
        SDIO_DCTRL_DTDIR   |    /* card -> SDIO FIFO */
        SDIO_DCTRL_DMAEN   |
        SDIO_DCTRL_DTEN;

    return SD_OK;
}
sd_status_t sdio_ll_prepare_dma_tx(
    const uint8_t *buffer,
    uint32_t length
)
{
    uint32_t words;

    if (buffer == NULL || length == 0)
        return SD_ERR_IO;

    if ((length & 0x3U) != 0)
        return SD_ERR_IO;

    words = length / 4U;

    /*
     * DMA Stream 6 должен быть выключен
     * перед настройкой нового transfer.
     */
    LL_DMA_DisableStream(DMA2, SDIO_DMA_TX_STREAM);

    while (LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_TX_STREAM)
    ) {}

    /* Clear old DMA flags */
    LL_DMA_ClearFlag_FE6(DMA2);
    LL_DMA_ClearFlag_DME6(DMA2);
    LL_DMA_ClearFlag_TE6(DMA2);
    LL_DMA_ClearFlag_HT6(DMA2);
    LL_DMA_ClearFlag_TC6(DMA2);

    /*
     * DMA:
     * RAM -> SDIO FIFO
     */
    LL_DMA_SetPeriphAddress(
        DMA2,
        SDIO_DMA_TX_STREAM,
        (uint32_t)&SDIO->FIFO
    );

    LL_DMA_SetMemoryAddress(
        DMA2,
        SDIO_DMA_TX_STREAM,
        (uint32_t)buffer
    );

    LL_DMA_SetDataLength(
        DMA2,
        SDIO_DMA_TX_STREAM,
        words
    );


    /*
     * SDIO data path
     */
    SDIO->ICR =
        SDIO_ICR_DBCKENDC  |
        SDIO_ICR_DATAENDC  |
        SDIO_ICR_TXUNDERRC |
        SDIO_ICR_DTIMEOUTC |
        SDIO_ICR_DCRCFAILC;

    SDIO->DTIMER = 0xFFFFFFFFU;
    SDIO->DLEN = length;

    // Сначала включаем DMA
    LL_DMA_EnableStream(
        DMA2,
        SDIO_DMA_TX_STREAM
    );

    // Затем настраиваем SDIO и запускаем передачу
    SDIO->DCTRL =
        (9U << 4)        |
        SDIO_DCTRL_DMAEN |
        SDIO_DCTRL_DTEN;

    return SD_OK;
}