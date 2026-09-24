#include "min_sdio_driver.h"
#include "stm32f4xx_ll_utils.h"
#include "core_cm4.h" // Для доступа к DWT
#include "SEGGER_RTT.h"
#include "stdbool.h"
#include "sdio.h"

// ------------------------ Новый API ----------------------------
#define SD_OCR_BUSY_BIT         (1U << 31)    // Card Power Up Status (1 = готова)
#define SD_OCR_CCS_BIT              (1U << 30)    // Card Capacity Status (1 = SDHC/SDXC)
#define SD_MAX_ACMD41_ATTEMPTS 300

#define SD_CMD8_VHS_27_36V      (1U << 8)
#define SD_CMD8_CHECK_PATTERN   0xAA 
#define SD_CMD8_ARG             (SD_CMD8_VHS_27_36V | SD_CMD8_CHECK_PATTERN)
#define SD_CMD8_RESP_MASK       ((1U << 12) - 1U)

#define SD_ACMD41_HCS_BIT           (1U << 30)    // Host Capacity Support (поддержка SDHC/SDXC)
#define SD_ACMD41_VOLTAGE_WINDOW    (0x1FFU << 15)
#define SD_ACMD41_ARG               (SD_ACMD41_HCS_BIT | SD_ACMD41_VOLTAGE_WINDOW)

sd_status_t sd_init(sd_card_info_t *info)
{
    uint32_t rca;
    uint32_t ocr;
    uint8_t  scr[8];
    sd_status_t status;

    if (info == NULL)
        return SD_ERR_IO;
    
    sdio_ll_reginit();
    sdio_ll_set_clock(SD_CLK_INIT);
    sdio_ll_set_bus_width(SD_BUS_1BIT);

    // CMD0
    status =  sdio_ll_cmd(
        SD_CMD0,
        0, 
        SD_RESP_NONE
    );
    if(status != SD_OK)
        return status;


    // CMD8
    status = sdio_ll_cmd(
        SD_CMD8, 
        SD_CMD8_ARG, 
        SD_RESP_NONE
    );
    if(status != SD_OK)
        return status;

    if(sdio_ll_get_short_response() & SD_CMD8_RESP_MASK != SD_CMD8_ARG) 
        return SD_ERR_UNSUPPORTED;

    uint32_t acmd41_attempts = 0;

    do
    {
        /* CMD55 */
        status = sdio_ll_cmd(
            SD_CMD55,
            0,
            SD_RESP_SHORT_CRC
        );

        if (status != SD_OK)
            return status;

        /* ACMD41 */
        status = sdio_ll_cmd(
            SD_ACMD41,
            SD_ACMD41_ARG,
            SD_RESP_SHORT_NOCRC
        );

        if (status != SD_OK)
            return status;

        ocr = sdio_ll_get_short_response();
        acmd41_attempts++;

        /* Карта ещё не готова */
         if ((ocr & SD_OCR_BUSY_BIT) != 0)
        {
            /* SDSC не поддерживается */
            if ((ocr & SD_OCR_CCS_BIT) == 0)
                return SD_ERR_UNSUPPORTED;

            break;
        }

    } while (acmd41_attempts < SD_MAX_ACMD41_ATTEMPTS);

    if (acmd41_attempts >= SD_MAX_ACMD41_ATTEMPTS)
        return SD_ERR_TIMEOUT;

    status = sdio_ll_cmd(SD_CMD2, 0, SD_RESP_LONG_CRC);
    if (status != SD_OK)
        return status;
    
    if((status = sd_cmd3(&rca)) != SD_OK)
        return status;
}

sd_status_t sd_cmd3(uint32_t *rca)
{
    uint32_t response;
    sd_status_t status = sdio_ll_cmd(SD_CMD3, 0, SD_RESP_SHORT_CRC);

    if(status != SD_OK)
    {
        return status;
    }

    response = sdio_ll_get_short_response();

    *rca = (response >> 16) & 0xFFFF;

    return SD_OK;
}


// ------------------ Легаси API ------------------------------

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

/*---------- CMD8 ---------*/
// Напряжение 2.7V - 3.6V



/*---------- CMD55 ---------*/
#define SD_R1_APP_CMD_BIT           (1U << 5)     // Флаг: следующая команда будет ACMD

/*---------- ACMD41 ---------*/


/* Биты ответа ACMD41 (OCR Register) */



#define MAX_ATTEMPTS 5000
uint32_t SDIO_TestCard(void)
{
    uint32_t response = 0;
    uint32_t command_code = 0;
    
    SEGGER_RTT_printf(0, "\r\n[SDIO] Starting SD card initialization...\r\n");

    // ---------------------------------------------------------
    // 1. CMD0: GO_IDLE_STATE
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD0: GO_IDLE_STATE\r\n");
    if (MIN_SDIO_SendCmd(SDIO, SD_CMD0, 0x00000000, MIN_SDIO_RESP_NONE) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD0 failed\r\n");
        return SD_ERR_NO_CARD;
    }
    
    // Задержка минимум 74 такта CLK (при 400 кГц это ~185 мкс)
    LL_mDelay(1);
    SEGGER_RTT_printf(0, "[SDIO] CMD0 OK\r\n");

    // ---------------------------------------------------------
    // 2. CMD8: SEND_IF_COND (проверка SD v2.0+)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD8: SEND_IF_COND (arg=0x000001AA)\r\n");
    if ((command_code = MIN_SDIO_SendCmd(SDIO, SD_CMD8, 0x000001AA, MIN_SDIO_RESP_SHORT_CRC)) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD8 timeout - no card?\r\n");
        return SD_ERR_NO_CARD;
    }
    
    SEGGER_RTT_printf(0, "[SDIO] CMD8: %u\r\n", command_code);
    response = SDIO->RESP1;
    SEGGER_RTT_printf(0, "[SDIO] CMD8 Response: 0x%08X\r\n", response);
    
    if ((response & SD_CMD8_RESP_MASK) != SD_CMD8_ARG) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD8 check pattern or voltage mismatch\r\n");
        return SD_ERR_CMD8;
    }

    SEGGER_RTT_printf(0, "[SDIO] CMD8 OK: SD v2.0+ card detected\r\n");

    // ---------------------------------------------------------
    // 3. ACMD41: SD_SEND_OP_COND (инициализация)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] ACMD41: SD_SEND_OP_COND (HCS=1, VDD=3.3V)\r\n");
    
    uint32_t acmd41_attempts = 0;
    bool is_acmd41_success = false;
    for (; acmd41_attempts < MAX_ATTEMPTS; acmd41_attempts++)
    {
        if (MIN_SDIO_SendCmd(SDIO, SD_CMD55, 0, MIN_SDIO_RESP_SHORT_CRC) == 1) 
        {
            LL_mDelay(10);
            continue;  
        }

        if (!(SDIO->RESP1 & SD_R1_APP_CMD_BIT)) 
        {
            LL_mDelay(10);
            continue;  
        }
        
        uint32_t result = 0;
        if ((result = MIN_SDIO_SendCmd(SDIO, SD_ACMD41, SD_ACMD41_ARG,  MIN_SDIO_RESP_SHORT_NOCRC)) == 1) 
        {
            LL_mDelay(10);
            continue;
        }

        
        response = SDIO->RESP1;
        SEGGER_RTT_printf(0, "[SDIO] Success ACMD41 %u \r\n", response);

        
        // Проверяем, готова ли карта (бит BUSY должен быть снят)
        if (response & SD_OCR_BUSY_BIT) {
            // Карта готова! Определяем тип
            if (response & SD_OCR_CCS_BIT) 
            {
                SEGGER_RTT_printf(0, "[SDIO] SDHC/SDXC card", acmd41_attempts);
            } 
            else {
                SEGGER_RTT_printf(0, "[SDIO] ERROR: SDSC not supported", acmd41_attempts);
                return SD_ERR_ACMD41;
            }

            is_acmd41_success = true;
            break;
        }
        // Задержка ~10 мс
        LL_mDelay(10);
    }

    
    if (!is_acmd41_success) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: ACMD41 max attempts after %u attempts\r\n", acmd41_attempts);
        return SD_ERR_ACMD41;
    }

    // ---------------------------------------------------------
    // 4. CMD2: ALL_SEND_CID (чтение идентификации)
    // ---------------------------------------------------------
    
    SEGGER_RTT_printf(0, "[SDIO] CMD2: ALL_SEND_CID\r\n");
    if (MIN_SDIO_SendCmd(SDIO, SD_CMD2, 0x00000000,  MIN_SDIO_RESP_LONG_CRC ) != 0) {
        SEGGER_RTT_printf(0, "[SDIO] ERROR: CMD2 failed\r\n");
        return SD_ERR_CMD2;
    }
    SEGGER_RTT_printf(0, "[SDIO] CMD2 OK: CID received\r\n");

    // ---------------------------------------------------------
    // 5. CMD3: SEND_RELATIVE_ADDR (получение RCA)
    // ---------------------------------------------------------
    SEGGER_RTT_printf(0, "[SDIO] CMD3: SEND_RELATIVE_ADDR\r\n");
    if (MIN_SDIO_SendCmd(SDIO, 3, 0x00000000, MIN_SDIO_RESP_SHORT_CRC) != 0) {
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
    MIN_SDIO_SendCmd(SDIO, 7, (uint32_t)rca << 16,MIN_SDIO_RESP_SHORT_CRC);
    MIN_SDIO_SendCmd(SDIO, 55, (uint32_t)rca << 16, MIN_SDIO_RESP_SHORT_CRC);
    MIN_SDIO_SendCmd(SDIO, 6, 0x00000002, MIN_SDIO_RESP_SHORT_CRC);
    
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
    MIN_SDIO_SendCmd(SDIO, 7, (uint32_t)rca << 16, MIN_SDIO_RESP_SHORT_CRC);

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