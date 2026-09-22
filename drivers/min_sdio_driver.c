#include "min_sdio_driver.h"

#define WAIT_TIMEOUT 1000000U
#define SD_BLOCK_SIZE  512U
#define SD_BLOCK_WORDS (SD_BLOCK_SIZE / sizeof(uint32_t))

static uint32_t MIN_SDIO_WaitDataEnd(SDIO_TypeDef *SDIOx)
{
    uint32_t timeout = WAIT_TIMEOUT;

    while (timeout > 0U)
    {
        uint32_t sta = SDIOx->STA;

        if (sta & SDIO_STA_DCRCFAIL)
            return 2U;

        if (sta & SDIO_STA_DTIMEOUT)
            return 3U;

        if (sta & SDIO_STA_RXOVERR)
            return 4U;

        if (sta & SDIO_STA_TXUNDERR)
            return 5U;

        if (sta & SDIO_STA_STBITERR)
            return 6U;

        if (sta & SDIO_STA_DATAEND)
            return 0U;

        timeout--;
    }

    return 7U;   // software timeout
}



uint32_t MIN_SDIO_ReadBlock(
    SDIO_TypeDef *SDIOx,
    uint32_t block_addr,
    uint32_t *buffer)
{
    uint32_t words_left = SD_BLOCK_WORDS;

    /*
     * Очищаем старые data-флаги.
     */
    SDIOx->ICR =
        SDIO_STA_DCRCFAIL |
        SDIO_STA_DTIMEOUT |
        SDIO_STA_RXOVERR |
        SDIO_STA_TXUNDERR |
        SDIO_STA_DATAEND |
        SDIO_STA_STBITERR;

    /*
     * Сколько байт должна принять data-блок схема.
     */
    SDIOx->DLEN = SD_BLOCK_SIZE;

    /*
     * Таймаут data path.
     */
    SDIOx->DTIMER = 0xFFFFFFFFU;

    /*
     * DCTRL:
     *
     * DBLOCKSIZE = 9 -> 2^9 = 512 байт
     * DTDIR      = 1 -> CARD -> MCU
     * DTEN       = 1 -> data path enabled
     */
    SDIOx->DCTRL =
        (9U << 4) |
        SDIO_DCTRL_DTDIR |
        SDIO_DCTRL_DTEN;

    /*
     * CMD17 = READ_SINGLE_BLOCK
     */
    if (MIN_SDIO_SendCmd(
            SDIOx,
            17,
            block_addr,
            MIN_SDIO_RESP_SHORT_CRC) != MIN_SDIO_OK)
    {
        SDIOx->DCTRL = 0;
        return 1U;
    }

    /*
     * Теперь карта должна начать передавать 512 байт.
     */
    while (words_left > 0U)
    {
        uint32_t sta = SDIOx->STA;

        if (sta & SDIO_STA_DCRCFAIL)
        {
            SDIOx->DCTRL = 0;
            return 2U;
        }

        if (sta & SDIO_STA_DTIMEOUT)
        {
            SDIOx->DCTRL = 0;
            return 3U;
        }

        if (sta & SDIO_STA_RXOVERR)
        {
            SDIOx->DCTRL = 0;
            return 4U;
        }

        /*
         * В FIFO есть минимум 8 слов.
         */
        if (sta & SDIO_STA_RXFIFOHF)
        {
            for (uint32_t i = 0; i < 8U && words_left > 0U; i++)
            {
                *buffer++ = SDIOx->FIFO;
                words_left--;
            }
        }
        /*
         * В FIFO есть хотя бы одно слово.
         */
        else if (sta & SDIO_STA_RXDAVL)
        {
            *buffer++ = SDIOx->FIFO;
            words_left--;
        }
    }

    /*
     * Все 512 байт прочитаны из FIFO.
     * Ждём окончательного завершения data path.
     */
    uint32_t result = MIN_SDIO_WaitDataEnd(SDIOx);

    SDIOx->DCTRL = 0;

    return result;
}
uint32_t MIN_SDIO_WriteBlock(
    SDIO_TypeDef *SDIOx,
    uint32_t block_addr,
    const uint32_t *buffer)
{
    uint32_t words_left = SD_BLOCK_WORDS;

    /*
     * Очищаем старые data-флаги.
     */
    SDIOx->ICR =
        SDIO_STA_DCRCFAIL |
        SDIO_STA_DTIMEOUT |
        SDIO_STA_RXOVERR |
        SDIO_STA_TXUNDERR |
        SDIO_STA_DATAEND |
        SDIO_STA_STBITERR;

    /*
     * Передаём ровно 512 байт.
     */
    SDIOx->DLEN = SD_BLOCK_SIZE;

    /*
     * Таймаут data path.
     */
    SDIOx->DTIMER = 0xFFFFFFFFU;

    /*
     * DCTRL:
     *
     * DBLOCKSIZE = 9 -> 512 байт
     * DTDIR      = 0 -> MCU -> CARD
     * DTEN       = 1
     */
    SDIOx->DCTRL =
        (9U << 4) |
        SDIO_DCTRL_DTEN;

    /*
     * CMD24 = WRITE_SINGLE_BLOCK
     */
    if (MIN_SDIO_SendCmd(
            SDIOx,
            24,
            block_addr,
            MIN_SDIO_RESP_SHORT_CRC) != MIN_SDIO_OK)
    {
        SDIOx->DCTRL = 0;
        return 1U;
    }

    /*
     * Заполняем TX FIFO.
     */
    while (words_left > 0U)
    {
        uint32_t sta = SDIOx->STA;

        if (sta & SDIO_STA_DCRCFAIL)
        {
            SDIOx->DCTRL = 0;
            return 2U;
        }

        if (sta & SDIO_STA_DTIMEOUT)
        {
            SDIOx->DCTRL = 0;
            return 3U;
        }

        if (sta & SDIO_STA_TXUNDERR)
        {
            SDIOx->DCTRL = 0;
            return 4U;
        }

        /*
         * В FIFO достаточно места для 8 слов.
         */
        if (sta & SDIO_STA_TXFIFOHE)
        {
            for (uint32_t i = 0; i < 8U && words_left > 0U; i++)
            {
                SDIOx->FIFO = *buffer++;
                words_left--;
            }
        }
    }

    /*
     * Ждём полного окончания передачи.
     */
    uint32_t result = MIN_SDIO_WaitDataEnd(SDIOx);

    SDIOx->DCTRL = 0;

    return result;
}

void MIN_SDIO_Init(SDIO_TypeDef *SDIOx, MIN_SDIO_InitTypeDef *SDIO_InitStruct)
{
    uint32_t tmpreg = 0U;

    /* Читаем текущее значение регистра CLKCR, чтобы сохранить зарезервированные биты */
    tmpreg = SDIOx->CLKCR;

    /* Очищаем биты, которые мы будем настраивать (биты 0-14) */
    tmpreg &= ~MIN_SDIO_CLKCR_CLEAR_MASK;

    /* Устанавливаем новые значения из структуры */
    tmpreg |= SDIO_InitStruct->ClockEdge;
    tmpreg |= SDIO_InitStruct->ClockBypass;
    tmpreg |= SDIO_InitStruct->ClockPowerSave;
    tmpreg |= SDIO_InitStruct->BusWide;
    tmpreg |= SDIO_InitStruct->HardwareFlowControl;
    
    /* Делитель частоты занимает младшие 8 бит (0-7) */
    tmpreg |= (SDIO_InitStruct->ClockDiv & SDIO_CLKCR_CLKDIV_Msk);

    /* Записываем обратно в регистр */
    SDIOx->CLKCR = tmpreg;
}

void MIN_SDIO_SetPowerState(SDIO_TypeDef *SDIOx, uint32_t PowerState)
{
    /* Очищаем биты PWRCTRL [1:0] и устанавливаем новое состояние */
    SDIOx->POWER &= ~SDIO_POWER_PWRCTRL;
    SDIOx->POWER |= PowerState;
}

void MIN_SDIO_EnableClock(SDIO_TypeDef *SDIOx)
{
    /* Устанавливаем бит CLKEN (бит 8) */
    SDIOx->CLKCR |= MIN_SDIO_CLOCK_ENABLE;
}




static uint32_t MIN_SDIO_BuildCmd(
    uint8_t cmd_index,
    MIN_SDIO_ResponseType response_type)
{
    uint32_t cmd = cmd_index & 0x3FU;   // bits 5:0 — номер команды

    switch (response_type)
    {
        case MIN_SDIO_RESP_NONE:
            /*
             * WAITRESP = 00
             */
            break;

        case MIN_SDIO_RESP_SHORT_CRC:
        case MIN_SDIO_RESP_SHORT_NOCRC:
            /*
             * WAITRESP = 01
             */
            cmd |= SDIO_CMD_WAITRESP_0;
            break;

        case MIN_SDIO_RESP_LONG_CRC:
            /*
             * WAITRESP = 11
             */
            cmd |= SDIO_CMD_WAITRESP_0 |
                   SDIO_CMD_WAITRESP_1;
            break;
    }

    /*
     * bit 10 — Command path state machine enable.
     *
     * После записи CMD с этим битом SDIO начинает
     * передавать команду на карту.
     */
    cmd |= SDIO_CMD_CPSMEN;

    return cmd;
}
static MIN_SDIO_Status MIN_SDIO_WaitCmd(
    SDIO_TypeDef *SDIOx,
    MIN_SDIO_ResponseType response_type,
    uint32_t timeout)
{
    while (timeout > 0U)
    {
        uint32_t sta = SDIOx->STA;

        /* Карта вообще не ответила */
        if (sta & SDIO_STA_CTIMEOUT)
        {
            SDIOx->ICR = SDIO_ICR_CTIMEOUTC;
            return MIN_SDIO_TIMEOUT;
        }

        /*
         * Ответ пришёл, но CRC неправильный.
         * Это проверяем независимо от CMDREND.
         */
        if (sta & SDIO_STA_CCRCFAIL)
        {
            SDIOx->ICR = SDIO_ICR_CCRCFAILC;

            if (response_type == MIN_SDIO_RESP_SHORT_NOCRC)
                return MIN_SDIO_OK;

            return MIN_SDIO_CRC_ERROR;
        }

        /* Команда без ответа */
        if (response_type == MIN_SDIO_RESP_NONE)
        {
            if (sta & SDIO_STA_CMDSENT)
            {
                SDIOx->ICR = SDIO_ICR_CMDSENTC;
                return MIN_SDIO_OK;
            }
        }
        else
        {
            /* Ответ принят нормально */
            if (sta & SDIO_STA_CMDREND)
            {
                SDIOx->ICR = SDIO_ICR_CMDRENDC;
                return MIN_SDIO_OK;
            }
        }

        timeout--;
    }

    return MIN_SDIO_TIMEOUT;
}
// маска очистки флагов 0-10 бит
#define SDIO_ICR_ALL_FLAGS_CLEAR (~((1U << 11) - 1U))


uint32_t MIN_SDIO_SendCmd(SDIO_TypeDef *SDIOx, uint8_t cmd_index, uint32_t arg, uint32_t resp_type)
{

    // 1. Очищаем флаги прерываний статуса
    SDIOx->ICR = SDIO_ICR_ALL_FLAGS_CLEAR; 

    // 2. Записываем аргумент команды
    SDIOx->ARG = arg;

    // 3. Формируем регистр команды
    uint32_t cmd = MIN_SDIO_BuildCmd(
        cmd_index,
        resp_type
    );
    SDIOx->CMD = cmd;

    return MIN_SDIO_WaitCmd(
        SDIOx,
        resp_type,
        100000U
    );

}

uint32_t MIN_SDIO_GetResponse(SDIO_TypeDef *SDIOx, uint32_t *response)
{
    // Для коротких ответов (R1, R3, R7) читаем регистр RESP1
    *response = SDIOx->RESP1;
    return 0;
}