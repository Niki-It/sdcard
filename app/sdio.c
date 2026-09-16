#include "min_sdio_driver.h"
#include "stm32f4xx_ll_utils.h"
#include "core_cm4.h" // Для доступа к DWT
#include "SEGGER_RTT.h"

void SDIO_Periph_Init(void)
{
    //Базовая настройка периферии SDIO
    MIN_SDIO_InitTypeDef sdio_init = {0};
    sdio_init.ClockEdge           = MIN_SDIO_CLOCK_EDGE_RISING;
    sdio_init.ClockBypass         = MIN_SDIO_CLOCK_BYPASS_DISABLE;
    sdio_init.ClockPowerSave      = MIN_SDIO_CLOCK_POWER_SAVE_DISABLE;
    sdio_init.BusWide             = MIN_SDIO_BUS_WIDE_1B;             // Начинаем ВСЕГДА с 1-битного режима
    sdio_init.HardwareFlowControl = MIN_SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    sdio_init.ClockDiv            = 118;                              // 48 МГц / (118 + 2) = 400 кГц
    
    MIN_SDIO_Init(SDIO, &sdio_init);

    // 3. Включаем питание и тактирование
    MIN_SDIO_SetPowerState(SDIO, MIN_SDIO_POWER_ON);
    MIN_SDIO_EnableClock(SDIO);
}

// Коды возврата для отладки
#define SD_OK           0
#define SD_ERR_NO_CARD  1
#define SD_ERR_CMD8     2
#define SD_ERR_ACMD41   3
#define SD_ERR_CMD2     4
#define SD_ERR_CMD3     5

uint32_t SDIO_TestCard(void)
{
    uint32_t response = 0;
    uint32_t timeout = 5000;
    
    SEGGER_RTT_printf(0, "\r\n[SDIO] Starting SD card initialization...\r\n");

    // ---------------------------------------------------------
    // 1. CMD0: GO_IDLE_STATE
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD0: GO_IDLE_STATE\r\n");
    if (MIN_SDIO_SendCmd(SDIO, 0, 0x00000000, MIN_SDIO_CMD_NO_RESPONSE) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD0 failed\r\n");
        return SD_ERR_NO_CARD;
    }
    
    // Задержка минимум 74 такта CLK (при 400 кГц это ~185 мкс)
    for(volatile uint32_t i = 0; i < 1000000; i++);
    SEGGER_RTT_printf(0, "[SDIO] CMD0 OK\r\n");

    // ---------------------------------------------------------
    // 2. CMD8: SEND_IF_COND (проверка SD v2.0+)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD8: SEND_IF_COND (arg=0x000001AA)\r\n");
    if (MIN_SDIO_SendCmd(SDIO, 8, 0x000001AA, MIN_SDIO_CMD_SHORT_RESPONSE) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD8 timeout - no card?\r\n");
        return SD_ERR_NO_CARD;
    }
    
    response = SDIO->RESP1;
    SEGGER_RTT_printf(0, "[SDIO] CMD8 Response: 0x%08X\r\n", response);
    
    if ((response & 0x00000FFF) != 0x000001AA) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD8 check pattern mismatch\r\n");
        return SD_ERR_CMD8;
    }
    SEGGER_RTT_printf(0, "[SDIO] CMD8 OK: SD v2.0+ card detected\r\n");

    // ---------------------------------------------------------
    // 3. ACMD41: SD_SEND_OP_COND (инициализация)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] ACMD41: SD_SEND_OP_COND (HCS=1, VDD=3.3V)\r\n");
    uint32_t acmd41_attempts = 0;
    
    // Правильный аргумент: HCS=1 (бит 30) + поддерживаемое напряжение (биты 23:0)
    // 0x40FF8000 = HCS=1 + VDD=2.7-3.6V (из ответа CMD8)
    uint32_t acmd41_arg = 0x40FF8000;
    
    while (timeout-- > 0) {
        acmd41_attempts++;
        
        // 3.1. CMD55: APP_CMD
        uint32_t cmd55_result = MIN_SDIO_SendCmd(SDIO, 55, 0x00000000, MIN_SDIO_CMD_SHORT_RESPONSE);
        if (cmd55_result != 0) {
            if (acmd41_attempts <= 3) {
                SEGGER_RTT_printf(0, "[SDIO] Attempt %u: CMD55 FAILED (error=%u)\r\n", acmd41_attempts, cmd55_result);
            }
            for(volatile uint32_t i = 0; i < 100000; i++);
            continue;
        }
        
        // Проверяем ответ CMD55 (должен быть R1 с битом APP_CMD = 1)
        uint32_t cmd55_response = SDIO->RESP1;
        if (!(cmd55_response & (1U << 5))) {
            if (acmd41_attempts <= 3) {
                SEGGER_RTT_printf(0, "[SDIO] Attempt %u: CMD55 response=0x%08X (APP_CMD bit not set!)\r\n", 
                                acmd41_attempts, cmd55_response);
            }
        }
        
        // 3.2. ACMD41: Инициализация с полным OCR
        uint32_t acmd41_result = MIN_SDIO_SendCmd(SDIO, 41, acmd41_arg, MIN_SDIO_CMD_SHORT_RESPONSE);
        if (acmd41_result != 0) {
            if (acmd41_attempts <= 3) {
                SEGGER_RTT_printf(0, "[SDIO] Attempt %u: ACMD41 FAILED (error=%u)\r\n", acmd41_attempts, acmd41_result);
            }
            for(volatile uint32_t i = 0; i < 100000; i++);
            continue;
        }
        
        MIN_SDIO_GetResponse(SDIO, &response);
        
        // Логируем первые попытки и каждую 100-ю
        if (acmd41_attempts <= 5 || acmd41_attempts % 100 == 0) {
            SEGGER_RTT_printf(0, "[SDIO] Attempt %u: OCR=0x%08X\r\n", acmd41_attempts, response);
        }
        
        // Проверяем бит 31 (Card Power Up Status)
        if (response & (1U << 31)) {
            SEGGER_RTT_printf(0, "[SDIO] ACMD41 OK after %u attempts\r\n", acmd41_attempts);
            
            if (response & (1U << 30)) {
                SEGGER_RTT_printf(0, "[SDIO] Card type: SDHC/SDXC (>2GB)\r\n");
            } else {
                SEGGER_RTT_printf(0, "[SDIO] Card type: SDSC (<=2GB)\r\n");
            }
            break;
        }
        
        // Задержка ~10 мс
        for(volatile uint32_t i = 0; i < 1000000; i++);
    }
    
    if (timeout == 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: ACMD41 timeout after %u attempts\r\n", acmd41_attempts);
        return SD_ERR_ACMD41;
    }

    // ---------------------------------------------------------
    // 4. CMD2: ALL_SEND_CID (чтение идентификации)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD2: ALL_SEND_CID\r\n");
    if (MIN_SDIO_SendCmd(SDIO, 2, 0x00000000, MIN_SDIO_CMD_LONG_RESPONSE) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD2 failed\r\n");
        return SD_ERR_CMD2;
    }
    SEGGER_RTT_printf(0, "[SDIO] CMD2 OK: CID received\r\n");

    // ---------------------------------------------------------
    // 5. CMD3: SEND_RELATIVE_ADDR (получение RCA)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD3: SEND_RELATIVE_ADDR\r\n");
    if (MIN_SDIO_SendCmd(SDIO, 3, 0x00000000, MIN_SDIO_CMD_SHORT_RESPONSE) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD3 failed\r\n");
        return SD_ERR_CMD3;
    }
    
    MIN_SDIO_GetResponse(SDIO, &response);
    uint16_t rca = (response >> 16) & 0xFFFF;
    SEGGER_RTT_printf(0, "[SDIO] CMD3 OK: RCA = 0x%04X\r\n", rca);
    
    SEGGER_RTT_printf(0, "[SDIO] === CARD INITIALIZED SUCCESSFULLY ===\r\n");

    return SD_OK;
}

// Функция переключения на высокую скорость (из предыдущего ответа)
void SDIO_SetHighSpeed(uint16_t rca)
{
    MIN_SDIO_SendCmd(SDIO, 7, (uint32_t)rca << 16, MIN_SDIO_CMD_SHORT_RESPONSE);
    MIN_SDIO_SendCmd(SDIO, 55, (uint32_t)rca << 16, MIN_SDIO_CMD_SHORT_RESPONSE);
    MIN_SDIO_SendCmd(SDIO, 6, 0x00000002, MIN_SDIO_CMD_SHORT_RESPONSE);
    
    uint32_t clkcr = SDIO->CLKCR;
    clkcr |= 0x00000800U; // 4-битная шина
    clkcr &= ~(0x000000FFU); // Очищаем делитель
    clkcr |= 0x00000000U;    // CLKDIV = 0 (24 МГц)
    SDIO->CLKCR = clkcr;
}

void SDIO_RunBenchmark(void)
{
    uint32_t rca = 0;
    uint32_t test_buffer[128]; // 512 байт
    uint32_t read_buffer[128];
    uint32_t start_cycle, end_cycle;
    float time_ms;
    uint32_t status;

    // 1. Включаем счетчик циклов DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    SEGGER_RTT_printf(0, "\r\n=== SDIO BENCHMARK START ===\r\n");

    // 2. Инициализация карты (используем вашу проверенную функцию)
    // Предполагаем, что SDIO_TestCard() возвращает SD_OK и мы можем получить RCA
    // Для простоты бенчмарка, давайте продублируем получение RCA здесь или вызовем вашу функцию.
    // (Здесь я использую упрощенный вызов, убедитесь, что карта уже инициализирована)
    
    // ... (Здесь должен быть вызов вашей успешной инициализации, которая вернула RCA)
    // Допустим, мы уже знаем, что RCA = 0x0001 (как в вашем логе), или получим его:
    MIN_SDIO_SendCmd(SDIO, 3, 0x00000000, MIN_SDIO_CMD_SHORT_RESPONSE);
    uint32_t resp;
    MIN_SDIO_GetResponse(SDIO, &resp);
    rca = (resp >> 16) & 0xFFFF;
    
    SEGGER_RTT_printf(0, "Card initialized, RCA = 0x%04X\r\n", rca);

    // Заполняем тестовый буфер паттерном
    for (int i = 0; i < 128; i++) {
        test_buffer[i] = 0xDEADBEEF + i;
    }

    // ==========================================================
    // ТЕСТ 1: Запись и чтение на 400 кГц (1 бит)
    // ==========================================================
    SEGGER_RTT_printf(0, "\r\n--- TEST 1: 400 kHz, 1-bit mode ---\r\n");
    
    // Выбираем карту (обязательно перед чтением/записью!)
    MIN_SDIO_SendCmd(SDIO, 7, (uint32_t)rca << 16, MIN_SDIO_CMD_SHORT_RESPONSE);

    // Запись
    start_cycle = DWT->CYCCNT;
    status = MIN_SDIO_WriteBlock(SDIO, 0, test_buffer); // Блок 0
    end_cycle = DWT->CYCCNT;
    
    if (status == 0) {
        time_ms = (float)(end_cycle - start_cycle) / (SystemCoreClock / 1000.0f);
        SEGGER_RTT_printf(0, "WRITE: SUCCESS. Time: %.2f ms\r\n", time_ms);
    } else {
        SEGGER_RTT_printf(0, "WRITE: FAILED (status=%u)\r\n", status);
    }

    // Чтение
    start_cycle = DWT->CYCCNT;
    status = MIN_SDIO_ReadBlock(SDIO, 0, read_buffer);
    end_cycle = DWT->CYCCNT;

    if (status == 0) {
        time_ms = (float)(end_cycle - start_cycle) / (SystemCoreClock / 1000.0f);
        SEGGER_RTT_printf(0, "READ : SUCCESS. Time: %.2f ms\r\n", time_ms);
        
        // Простая проверка целостности
        if (read_buffer[0] == test_buffer[0] && read_buffer[127] == test_buffer[127]) {
            SEGGER_RTT_printf(0, "DATA VERIFY: OK\r\n");
        } else {
            SEGGER_RTT_printf(0, "DATA VERIFY: FAIL!\r\n");
        }
    } else {
        SEGGER_RTT_printf(0, "READ : FAILED (status=%u)\r\n", status);
    }

    // ==========================================================
    // ПЕРЕКЛЮЧЕНИЕ НА ВЫСОКУЮ СКОРОСТЬ
    // ==========================================================
    SEGGER_RTT_printf(0, "\r\nSwitching to HIGH SPEED (4-bit, 24 MHz)...\r\n");
    SDIO_SetHighSpeed(rca);
    SEGGER_RTT_printf(0, "Switched!\r\n");

    // ==========================================================
    // ТЕСТ 2: Запись и чтение на 24 МГц (4 бита)
    // ==========================================================
    SEGGER_RTT_printf(0, "\r\n--- TEST 2: 24 MHz, 4-bit mode ---\r\n");
    
    // Карта уже выбрана (CMD7 был ранее), но на всякий случай можно повторить, 
    // хотя после смены режима она остается в Transfer state.

    // Запись
    start_cycle = DWT->CYCCNT;
    status = MIN_SDIO_WriteBlock(SDIO, 0, test_buffer);
    end_cycle = DWT->CYCCNT;
    
    if (status == 0) {
        time_ms = (float)(end_cycle - start_cycle) / (SystemCoreClock / 1000.0f);
        SEGGER_RTT_printf(0, "WRITE: SUCCESS. Time: %.2f ms\r\n", time_ms);
    } else {
        SEGGER_RTT_printf(0, "WRITE: FAILED (status=%u)\r\n", status);
    }

    // Чтение
    start_cycle = DWT->CYCCNT;
    status = MIN_SDIO_ReadBlock(SDIO, 0, read_buffer);
    end_cycle = DWT->CYCCNT;

    if (status == 0) {
        time_ms = (float)(end_cycle - start_cycle) / (SystemCoreClock / 1000.0f);
        SEGGER_RTT_printf(0, "READ : SUCCESS. Time: %.2f ms\r\n", time_ms);
        
        if (read_buffer[0] == test_buffer[0] && read_buffer[127] == test_buffer[127]) {
            SEGGER_RTT_printf(0, "DATA VERIFY: OK\r\n");
        } else {
            SEGGER_RTT_printf(0, "DATA VERIFY: FAIL!\r\n");
        }
    } else {
        SEGGER_RTT_printf(0, "READ : FAILED (status=%u)\r\n", status);
    }

    SEGGER_RTT_printf(0, "\r\n=== BENCHMARK COMPLETE ===\r\n");
}