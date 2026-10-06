#include "sdio_ll.h"
#include <stm32f405xx.h>
#include "stddef.h"
#include "SEGGER_RTT.h"

uint32_t sdio_ll_get_short_response()
{
    return SDIO->RESP1;
}

void sdio_ll_get_long_response(uint32_t response[4])
{
    response[0] = SDIO->RESP1;
    response[1] = SDIO->RESP2;
    response[2] = SDIO->RESP3;
    response[3] = SDIO->RESP4;
}

static sd_status_t sdio_ll_wait_cmd(
    sdio_resp_type_t resp_type,
    uint32_t timeout)
{
    while (timeout > 0U)
    {
        uint32_t sta = SDIO->STA;

        /*
         * Карта не ответила.
         */
        if (sta & SDIO_STA_CTIMEOUT)
        {
            SDIO->ICR = SDIO_ICR_CTIMEOUTC;
            return SD_ERR_TIMEOUT;
        }

        /*
         * Ошибка CRC ответа.
         *
         * R3 (OCR) не содержит CRC,
         * поэтому CCRCFAIL для него игнорируем.
         */
       if (sta & SDIO_STA_CCRCFAIL)
        {
            SDIO->ICR = SDIO_ICR_CCRCFAILC;

            // !!! Даже если есть ошибка CRC в этом формате ответа мы его не принимаем
            if (resp_type == SD_RESP_SHORT_NOCRC)
                return SD_OK;

            return SD_ERR_CMD_CRC;
        }

        /*
         * Команда без ответа.
         */
        if (resp_type == SD_RESP_NONE)
        {
            if (sta & SDIO_STA_CMDSENT)
            {
                SDIO->ICR = SDIO_ICR_CMDSENTC;
                return SD_OK;
            }
        }
        else
        {
            /*
             * Ответ принят.
             */
            if (sta & SDIO_STA_CMDREND)
            {
                SDIO->ICR = SDIO_ICR_CMDRENDC;
                return SD_OK;
            }
        }

        timeout--;
    }

    return SD_ERR_TIMEOUT;
}

static uint32_t sdio_ll_build_cmd(
    uint8_t cmd_index,
    sdio_resp_type_t resp_type)
{
    uint32_t cmd = cmd_index & 0x3FU;

    switch (resp_type)
    {
        case SD_RESP_NONE:
            break;

        case SD_RESP_SHORT_CRC:
        case SD_RESP_SHORT_NOCRC:
            cmd |= SDIO_CMD_WAITRESP_0;
            break;

        case SD_RESP_LONG_CRC:
            cmd |= SDIO_CMD_WAITRESP_0 |
                   SDIO_CMD_WAITRESP_1;
            break;

        default:
            break;
    }

    cmd |= SDIO_CMD_CPSMEN;

    return cmd;
}

sd_status_t sdio_ll_cmd(
    uint8_t cmd_index,
    uint32_t arg,
    sdio_resp_type_t resp_type)
{
    uint32_t cmd;

    cmd = sdio_ll_build_cmd(
        cmd_index,
        resp_type
    );

    /*
     * Очищаем флаги перед отправкой команды.
     */
    SDIO->ICR =
        SDIO_ICR_CCRCFAILC |
        SDIO_ICR_CTIMEOUTC |
        SDIO_ICR_CMDRENDC |
        SDIO_ICR_CMDSENTC;

    /*
     * Аргумент команды.
     */
    SDIO->ARG = arg;

    /*
     * Отправляем команду.
     */
    SDIO->CMD = cmd;

    /*
     * Ждём завершения команды.
     */
    return sdio_ll_wait_cmd(
        resp_type,
        100000U
    );
}
sd_status_t sdio_ll_reginit(void)
{
    uint32_t tmpreg;

    tmpreg = SDIO->CLKCR;

    tmpreg &= ~(
        SDIO_CLKCR_CLKDIV_Msk |
        SDIO_CLKCR_WIDBUS_Msk |
        SDIO_CLKCR_CLKEN_Msk
    );

    /* 1-bit, CLKDIV = 0, CLKEN = 0 */
    SDIO->CLKCR = tmpreg;

    return SD_OK;
}
sd_status_t sdio_ll_clock_enable(void)
{
    uint32_t tmpreg;

    tmpreg = SDIO->CLKCR;

    tmpreg |= SDIO_CLKCR_CLKEN;

    SDIO->CLKCR = tmpreg;

    return SD_OK;
}
sd_status_t sdio_ll_power_on(void)
{
    uint32_t tmpreg;

    tmpreg = SDIO->POWER;

    tmpreg &= ~SDIO_POWER_PWRCTRL_Msk;
    tmpreg |= SDIO_POWER_ON;

    SDIO->POWER = tmpreg;

    return SD_OK;
}
sd_status_t sdio_ll_set_clock(sdio_clock_t clock)
{
    uint32_t tmpreg;
    uint32_t clkdiv;

    switch (clock)
    {
        case SD_CLK_INIT:
            /* 48 MHz / (118 + 2) = 400 kHz */
            clkdiv = 118U;
            break;

        case SD_CLK_WORK:
            /* 48 MHz / (0 + 2) = 24 MHz */
            clkdiv = 6U; 
            break;

        default:
            return SD_ERR_IO;
    }

    tmpreg = SDIO->CLKCR;

    tmpreg &= ~SDIO_CLKCR_CLKDIV_Msk;
    tmpreg |= clkdiv & SDIO_CLKCR_CLKDIV_Msk;

    SDIO->CLKCR = tmpreg;

    return SD_OK;
}
sd_status_t sdio_ll_set_bus_width(sdio_bus_width_t width)
{
    uint32_t tmpreg;

    tmpreg = SDIO->CLKCR;

    tmpreg &= ~SDIO_CLKCR_WIDBUS_Msk;

    switch (width)
    {
        case SD_BUS_1BIT:
            /* WIDBUS = 00 */
            break;

        case SD_BUS_4BIT:
            /* WIDBUS = 01 */
            tmpreg |= (1U << 11);
            break;

        default:
            return SD_ERR_IO;
    }

    SDIO->CLKCR = tmpreg;

    return SD_OK;
}

static uint32_t fifo_counter = 0;
sd_status_t sdio_ll_fifo_write_words(const uint32_t *data, uint32_t words)
{
    uint32_t status;

    if (data == NULL || words == 0U || words > 8U) {
        return SD_ERR_IO;
    }

    while (!(SDIO->STA & SDIO_STA_TXFIFOHE)) {
        status = SDIO->STA;

        if (status & SDIO_STA_TXUNDERR) {
            SEGGER_RTT_printf(0,
                "TXUNDERR before FIFO write: STA=0x%08lX\r\n",
                (unsigned long)status);

            SDIO->ICR = SDIO_ICR_TXUNDERRC;
            return SD_ERR_IO;
        }

        if (status & SDIO_STA_DTIMEOUT) {
            SDIO->ICR = SDIO_ICR_DTIMEOUTC;
            return SD_ERR_TIMEOUT;
        }

        if (status & SDIO_STA_DCRCFAIL) {
            SDIO->ICR = SDIO_ICR_DCRCFAILC;
            return SD_ERR_DATA_CRC;
        }

    }

    for (uint32_t i = 0; i < words; i++) {
        SDIO->FIFO = data[i];
        fifo_counter++;
    }

    return SD_OK;
}

sd_status_t sdio_ll_data_write(
    const uint8_t *buffer,
    uint32_t length
)
{
    uint32_t words;
    sd_status_t status;

    if (buffer == NULL || length == 0)
        return SD_ERR_IO;

    if ((length & 0x3U) != 0)
        return SD_ERR_IO;

    SDIO->ICR =
        SDIO_ICR_DBCKENDC  |
        SDIO_ICR_DATAENDC  |
        SDIO_ICR_TXUNDERRC |
        SDIO_ICR_DTIMEOUTC |
        SDIO_ICR_DCRCFAILC;

    SDIO->DTIMER = 0xFFFFFFFFU;
    SDIO->DLEN = length;
    SDIO->DCTRL =
        (9U << 4) |
        SDIO_DCTRL_DTEN;

    words = length / 4U;


    uint32_t data[8];
    uint32_t count;
    for (uint32_t i = 0; i < words;)
    {
        count = words - i;
        if (count > 8U) {
            count = 8U;
        }

        for (uint32_t j = 0; j < count; j++) {
            data[j] =
                ((uint32_t)buffer[0])       |
                ((uint32_t)buffer[1] << 8)  |
                ((uint32_t)buffer[2] << 16) |
                ((uint32_t)buffer[3] << 24);

            buffer += 4;
        }

        status = sdio_ll_fifo_write_words(data, count);

        if (status != SD_OK)
        {
            SEGGER_RTT_printf(0,
                "FIFO write failed: word=%lu/%lu STA=0x%08lX\r\n",
                (unsigned long)i,
                (unsigned long)words,
                (unsigned long)SDIO->STA);
            return status;
        }

        i += count;
    }

    while (1)
    {
        uint32_t sta = SDIO->STA;

        if (sta & SDIO_STA_TXUNDERR)
        {
            SDIO->ICR = SDIO_ICR_TXUNDERRC;
            return SD_ERR_IO;
        }

        if (sta & SDIO_STA_DTIMEOUT)
        {
            SDIO->ICR = SDIO_ICR_DTIMEOUTC;
            return SD_ERR_TIMEOUT;
        }

        if (sta & SDIO_STA_DCRCFAIL)
        {
            SDIO->ICR = SDIO_ICR_DCRCFAILC;
            return SD_ERR_DATA_CRC;
        }

        if (sta & SDIO_STA_DATAEND)
        {
            SDIO->ICR = SDIO_ICR_DATAENDC;
            return SD_OK;
        }
    }
}
static sd_status_t sdio_ll_fifo_read_word(uint32_t *data)
{
    uint32_t sta;

    while (1)
    {
        sta = SDIO->STA;

        if (sta & SDIO_STA_RXOVERR)
        {
            SDIO->ICR = SDIO_ICR_RXOVERRC;
            return SD_ERR_IO;
        }

        if (sta & SDIO_STA_DTIMEOUT)
        {
            SDIO->ICR = SDIO_ICR_DTIMEOUTC;
            return SD_ERR_TIMEOUT;
        }

        if (sta & SDIO_STA_DCRCFAIL)
        {
            SDIO->ICR = SDIO_ICR_DCRCFAILC;
            return SD_ERR_DATA_CRC;
        }

        if (sta & SDIO_STA_RXDAVL)
        {
            *data = SDIO->FIFO;
            return SD_OK;
        }
    }
}

sd_status_t sdio_ll_data_read(
    uint8_t *buffer,
    uint32_t length
)
{
    uint32_t words;
    uint32_t i;
    uint32_t data;
    uint32_t sta;
    sd_status_t status;

    if (buffer == NULL || length == 0)
        return SD_ERR_IO;

    if ((length & 0x3U) != 0)
        return SD_ERR_IO;

    SDIO->ICR =
        SDIO_ICR_DBCKENDC  |
        SDIO_ICR_DATAENDC  |
        SDIO_ICR_RXOVERRC  |
        SDIO_ICR_DTIMEOUTC |
        SDIO_ICR_DCRCFAILC;

    SDIO->DTIMER = 0xFFFFFFFFU;
    SDIO->DLEN = length;

    SDIO->DCTRL =
        (9U << 4) |
        SDIO_DCTRL_DTEN   |
        SDIO_DCTRL_DTDIR;
    

    words = length / 4U;
    
    for (i = 0; i < words; i++)
    {
        status = sdio_ll_fifo_read_word(&data);

        if (status != SD_OK)
        {
            return status;
        }

        buffer[0] = (uint8_t)(data);
        buffer[1] = (uint8_t)(data >> 8);
        buffer[2] = (uint8_t)(data >> 16);
        buffer[3] = (uint8_t)(data >> 24);

        buffer += 4;
    }

    while (1)
    {
        sta = SDIO->STA;

        if (sta & SDIO_STA_RXOVERR)
        {
            SDIO->ICR = SDIO_ICR_RXOVERRC;
            return SD_ERR_IO;
        }

        if (sta & SDIO_STA_DTIMEOUT)
        {
            SDIO->ICR = SDIO_ICR_DTIMEOUTC;
            return SD_ERR_TIMEOUT;
        }

        if (sta & SDIO_STA_DCRCFAIL)
        {
            SDIO->ICR = SDIO_ICR_DCRCFAILC;
            return SD_ERR_DATA_CRC;
        }

        if (sta & SDIO_STA_DBCKEND)
        {
            SDIO->ICR = SDIO_ICR_DBCKENDC;
            return SD_OK;
        }
    }
}

#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_bus.h"

#define SDIO_DMA_RX_STREAM   LL_DMA_STREAM_3
#define SDIO_DMA_TX_STREAM   LL_DMA_STREAM_6
#define SDIO_DMA_CHANNEL     LL_DMA_CHANNEL_4

sd_status_t sdio_ll_dma_init(void)
{
    /* DMA2 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

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
        LL_DMA_MODE_NORMAL
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

    /* DMA FIFO OFF */
    LL_DMA_DisableFifoMode(
        DMA2,
        SDIO_DMA_TX_STREAM
    );

    /* Burst = SINGLE */
    LL_DMA_SetPeriphBurstxfer(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_PBURST_SINGLE
    );

    LL_DMA_SetMemoryBurstxfer(
        DMA2,
        SDIO_DMA_TX_STREAM,
        LL_DMA_MBURST_SINGLE
    );


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
uint32_t debug_counter = 0;
sd_status_t sdio_ll_wait_dma_rx(void)
{
    sd_status_t status;
    uint32_t sta;

    while (1)
    {
        if (++debug_counter >= 1000000U)
        {
            SEGGER_RTT_printf(0,
                "DMA HANG: STA=%08lX NDTR=%lu EN=%lu TC=%lu RXOVERR=%lu DTIMEOUT=%lu DCRCFAIL=%lu\r\n",
                (unsigned long)SDIO->STA,
                (unsigned long)DMA2_Stream3->NDTR,
                (unsigned long)LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_RX_STREAM),
                (unsigned long)LL_DMA_IsActiveFlag_TC3(DMA2),
                (unsigned long)((SDIO->STA & SDIO_STA_RXOVERR) != 0),
                (unsigned long)((SDIO->STA & SDIO_STA_DTIMEOUT) != 0),
                (unsigned long)((SDIO->STA & SDIO_STA_DCRCFAIL) != 0));

            debug_counter = 0;
        }
        /* Проверяем ошибки SDIO */
        status = sdio_ll_check_rx_status();

        if (status != SD_OK)
            return status;


        /* Проверяем ошибки DMA */
        if (LL_DMA_IsActiveFlag_TE3(DMA2) ||
            LL_DMA_IsActiveFlag_DME3(DMA2) ||
            LL_DMA_IsActiveFlag_FE3(DMA2))
        {
            LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);

            LL_DMA_ClearFlag_TE3(DMA2);
            LL_DMA_ClearFlag_DME3(DMA2);
            LL_DMA_ClearFlag_FE3(DMA2);

            return SD_ERR_IO;
        }


        /* DMA закончил перенос */
        if (LL_DMA_IsActiveFlag_TC3(DMA2))
        {
            LL_DMA_DisableStream(DMA2, SDIO_DMA_RX_STREAM);

            while (LL_DMA_IsEnabledStream(DMA2, SDIO_DMA_RX_STREAM))
            {
            }

            LL_DMA_ClearFlag_TC3(DMA2);

            /*
             * DMA закончил, теперь проверяем
             * финальное состояние SDIO.
             */
            status = sdio_ll_check_rx_status();

            if (status != SD_OK)
                return status;

            sta = SDIO->STA;

            if (sta & SDIO_STA_DBCKEND)
            {
                SDIO->ICR = SDIO_ICR_DBCKENDC;
                return SD_OK;
            }
        }
    }
}
