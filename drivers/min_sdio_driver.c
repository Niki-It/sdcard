#include "min_sdio_driver.h"

uint32_t MIN_SDIO_ReadBlock(SDIO_TypeDef *SDIOx, uint32_t block_addr, uint32_t *buffer)
{
    uint32_t timeout = 1000000;
    uint32_t words_to_read = 128; // 512 байт / 4 = 128 слов по 32 бита

    // 1. Очищаем флаги статуса
    SDIOx->ICR = 0x000005FFU;

    // 2. Настраиваем регистр управления данными (DCTRL)
    // Block size = 9 (2^9 = 512 байт), направление: от карты к CPU (0), включаем передачу
    SDIOx->DCTRL = (9U << 4) | (1U << 0);

    // 3. Отправляем команду чтения (CMD17)
    if (MIN_SDIO_SendCmd(SDIOx, 17, block_addr, MIN_SDIO_CMD_SHORT_RESPONSE) != 0) {
        SDIOx->DCTRL = 0;
        return 1; // Ошибка команды
    }

    // 4. Читаем данные из FIFO
    while (words_to_read > 0 && timeout--) {
        if (SDIOx->STA & SDIO_STA_RXDAVL) { // Данные доступны в FIFO
            *buffer++ = SDIOx->FIFO;
            words_to_read--;
        }
        if (SDIOx->STA & (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT)) {
            SDIOx->DCTRL = 0;
            return 2; // Ошибка данных
        }
    }

    // 5. Ждем завершения передачи данных (флаг DATAEND)
    timeout = 1000000;
    while (!(SDIOx->STA & SDIO_STA_DATAEND) && timeout--);
    
    SDIOx->DCTRL = 0; // Отключаем передачу данных
    return (timeout == 0) ? 3 : 0;
}

uint32_t MIN_SDIO_WriteBlock(SDIO_TypeDef *SDIOx, uint32_t block_addr, const uint32_t *buffer)
{
    uint32_t timeout = 1000000;
    uint32_t words_to_write = 128; // 512 байт / 4 = 128 слов по 32 бита
    const uint32_t *src = buffer;

    // 1. Очищаем флаги статуса
    SDIOx->ICR = 0x000005FFU;

    // 2. Настраиваем DCTRL: Block size = 9 (512 байт), направление: от CPU к карте (бит 1 = 1), включаем передачу
    SDIOx->DCTRL = (9U << 4) | (1U << 1) | (1U << 0);

    // 3. Отправляем команду записи (CMD24)
    if (MIN_SDIO_SendCmd(SDIOx, 24, block_addr, MIN_SDIO_CMD_SHORT_RESPONSE) != 0) {
        SDIOx->DCTRL = 0;
        return 1; // Ошибка команды
    }

    // 4. Записываем данные в FIFO
    while (words_to_write > 0 && timeout--) {
        // Ждем, пока FIFO станет наполовину пустым (гарантированно есть место для записи)
        // Исправленный макрос: SDIO_STA_TXFIFOHE (Half Empty)
        if (SDIOx->STA & SDIO_STA_TXFIFOHE) {
            SDIOx->FIFO = *src++;
            words_to_write--;
        }
        
        // Проверка на ошибки передачи данных
        if (SDIOx->STA & (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT)) {
            SDIOx->DCTRL = 0;
            return 2; // Ошибка данных (CRC или таймаут)
        }
    }

    // 5. Ждем завершения передачи данных (флаг DATAEND)
    timeout = 1000000;
    while (!(SDIOx->STA & SDIO_STA_DATAEND) && timeout--);
    
    SDIOx->DCTRL = 0; // Отключаем передачу данных
    
    return (timeout == 0) ? 3 : 0; // 3 = таймаут ожидания DATAEND, 0 = успех
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
    tmpreg |= (SDIO_InitStruct->ClockDiv & 0x000000FFU);

    /* Записываем обратно в регистр */
    SDIOx->CLKCR = tmpreg;
}

void MIN_SDIO_SetPowerState(SDIO_TypeDef *SDIOx, uint32_t PowerState)
{
    /* Очищаем биты PWRCTRL [1:0] и устанавливаем новое состояние */
    SDIOx->POWER &= ~0x00000003U;
    SDIOx->POWER |= PowerState;
}

void MIN_SDIO_EnableClock(SDIO_TypeDef *SDIOx)
{
    /* Устанавливаем бит CLKEN (бит 8) */
    SDIOx->CLKCR |= MIN_SDIO_CLOCK_ENABLE;
}

uint32_t MIN_SDIO_SendCmd(SDIO_TypeDef *SDIOx, uint8_t cmd_index, uint32_t arg, uint32_t resp_type)
{
    uint32_t timeout = 100000;

    // 1. Очищаем флаги прерываний статуса
    SDIOx->ICR = 0x000005FFU; 

    // 2. Записываем аргумент команды
    SDIOx->ARG = arg;

    // 3. Формируем регистр команды
    uint32_t cmd_reg = (uint32_t)cmd_index | resp_type | (1U << 10); // CPSMEN
    SDIOx->CMD = cmd_reg;

    // 4. Ждем завершения в зависимости от типа ответа
    if (resp_type == MIN_SDIO_CMD_NO_RESPONSE) {
        // Для команд без ответа ждем флаг CMDSENT (бит 6)
        while ((!(SDIOx->STA & (1U << 6))) && (timeout-- > 0));
    } else {
        // Для команд с ответом ждем флаг CMDREND (бит 8) - ответ получен, CRC пройден
        while ((!(SDIOx->STA & (1U << 8))) && (timeout-- > 0));
        
        // Проверяем, не было ли ошибок
        if (SDIOx->STA & (1U << 2)) { // CRCFAIL (бит 2)
            SDIOx->ICR = (1U << 2);   // Сбрасываем флаг
            return 2; // Ошибка CRC
        }
        if (SDIOx->STA & (1U << 3)) { // CTIMEOUT (бит 3)
            SDIOx->ICR = (1U << 3);   // Сбрасываем флаг
            return 1; // Таймаут
        }
    }

    if (timeout == 0) return 1; // Общий таймаут

    return 0; // Успех
}

uint32_t MIN_SDIO_GetResponse(SDIO_TypeDef *SDIOx, uint32_t *response)
{
    // Для коротких ответов (R1, R3, R7) читаем регистр RESP1
    *response = SDIOx->RESP1;
    return 0;
}