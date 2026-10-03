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

#define SD_R1_APP_CMD_BIT           (1U << 5)     // Флаг: следующая команда будет ACMD

sd_status_t sd_cmd3(uint32_t *rca);
bool sd_parse_csd(sd_card_info_t *info, const uint32_t response[4]);
sd_status_t sd_init(sd_card_info_t *info)
{
    uint32_t rca;
    uint32_t ocr;
    sd_status_t status;

    if (info == NULL)
        return SD_ERR_IO;
    
    sdio_ll_reginit();
    sdio_ll_power_on();
    // 400kHZ 1 bit
    sdio_ll_set_clock(SD_CLK_INIT);
    sdio_ll_clock_enable();
    sdio_ll_set_bus_width(SD_BUS_1BIT);

    // CMD0
    status = sdio_ll_cmd(
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
        SD_RESP_SHORT_CRC
    );
    if(status != SD_OK)
        return status;

    if((sdio_ll_get_short_response() & SD_CMD8_RESP_MASK) != SD_CMD8_ARG) 
        return SD_ERR_UNSUPPORTED;


    uint32_t acmd41_attempts = 0;
    bool card_ready = false;

    for (; acmd41_attempts < SD_MAX_ACMD41_ATTEMPTS; acmd41_attempts++)
    {
        /* CMD55 */
        status = sdio_ll_cmd(
            SD_CMD55,
            0,
            SD_RESP_SHORT_CRC
        );

        if (status != SD_OK)
        {
            LL_mDelay(10);
            continue;
        }

        /* CMD55 должен вернуть APP_CMD */
        uint32_t r1 = sdio_ll_get_short_response();

        if ((r1 & SD_R1_APP_CMD_BIT) == 0)
        {
            LL_mDelay(10);
            continue;
        }

        /* ACMD41 */
        status = sdio_ll_cmd(
            SD_ACMD41,
            SD_ACMD41_ARG,
            SD_RESP_SHORT_NOCRC
        );

        if (status == SD_ERR_TIMEOUT)
        {
            LL_mDelay(10);
            continue;
        }

        if (status != SD_OK)
            return status;

        ocr = sdio_ll_get_short_response();

        if ((ocr & SD_OCR_BUSY_BIT) != 0)
        {
            card_ready = true;
            break;
        }

        LL_mDelay(10);
    }

    if (!card_ready)
        return SD_ERR_TIMEOUT;

    if ((ocr & SD_OCR_CCS_BIT) == 0)
        return SD_ERR_UNSUPPORTED;

    // CMD2
    status = sdio_ll_cmd(SD_CMD2, 0, SD_RESP_LONG_CRC);
    if (status != SD_OK)
        return status;
    
    if((status = sd_cmd3(&rca)) != SD_OK)
        return status;
    info->rca = rca;

    // CMD9 - чтение информации о карте
    uint32_t response[4];
    status = sdio_ll_cmd(SD_CMD9, rca << 16, SD_RESP_LONG_CRC);

    if (status != SD_OK)
        return status;

    sdio_ll_get_long_response(response);
    if(!sd_parse_csd(info,response))
    {
        return SD_ERR_UNSUPPORTED;
    }

    // Переходим на 24MHZ
    sdio_ll_set_clock(SD_CLK_WORK);
    // CDM7 - проверка готовности карты
    status = sdio_ll_cmd(SD_CMD7, rca << 16, SD_RESP_SHORT_CRC);
    if (status != SD_OK)
        return status;

    // CMD55
    status = sdio_ll_cmd(SD_CMD55, rca << 16, SD_RESP_SHORT_CRC);
    if (status != SD_OK)
        return status;

    // ACMD6 - переключаем карту в 4-bit
    status = sdio_ll_cmd(SD_ACMD6, 2, SD_RESP_SHORT_CRC);
    if (status != SD_OK)
        return status;

    // Переключаем SDIO STM32 в 4-bit
    sdio_ll_set_bus_width(SD_BUS_4BIT);

    if (sdio_ll_dma_init() != SD_OK)
        return SD_ERR_DMA;

    return SD_OK;
}

bool sd_parse_csd(sd_card_info_t *info, const uint32_t response[4])
{
    uint32_t csd_structure;
    uint32_t c_size;

    if (info == NULL || response == NULL)
        return false;


    //CSD_STRUCTURE = CSD[127:126]
    csd_structure = (response[0] >> 30) & 0x03;

    if (csd_structure != 1)
        return false;

    /*
     * C_SIZE = CSD[69:48]
     *
     * CSD[69:64] -> RESP2[5:0]
     * CSD[63:48] -> RESP3[31:16]
     */
    c_size =
        ((response[1] & 0x0000003F) << 16) |
        ((response[2] >> 16) & 0x0000FFFF);

    //Capacity = (C_SIZE + 1) * 512 KiB
    info->capacity_bytes =
        ((uint64_t)c_size + 1ULL) * 512ULL * 1024ULL;

    info->block_size = 512;

    info->block_count =
        info->capacity_bytes / info->block_size;

    // Неважно SDHC или SDXC
    info->high_capacity = true;

    return true;
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

static sd_status_t sd_wait_ready(sd_card_info_t *info);

sd_status_t sd_write_blocks(
    sd_card_info_t *info,
    uint32_t lba,
    const uint8_t *buffer,
    uint32_t count
)
{
    sd_status_t status;
    sd_status_t stop_status;
    uint32_t length;

    if (buffer == NULL || count == 0)
        return SD_ERR_IO;

    if (lba >= info->block_count ||
        count > (info->block_count - lba))
        return SD_ERR_IO;

    length = count * info->block_size;

    /* CMD25 - WRITE_MULTIPLE_BLOCK */
    status = sdio_ll_cmd(
        SD_CMD25,
        lba,
        SD_RESP_SHORT_CRC
    );

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD WRITE: CMD25 failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    //status = sdio_ll_data_write_dma(buffer, length);

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD WRITE: DMA failed, LBA=%lu count=%lu length=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            (unsigned long)length,
            status);

        /*
         * После ошибки передачи пробуем остановить CMD25.
         * Первичную ошибку DMA не затираем результатом CMD12.
         */
        stop_status = sdio_ll_cmd(
            SD_CMD12,
            0,
            SD_RESP_SHORT_CRC
        );

        if (stop_status != SD_OK) {
            SEGGER_RTT_printf(0,
                "SD WRITE: CMD12 after DMA error failed, status=%d\r\n",
                stop_status);
        }

        return status;
    }

    /* CMD12 - завершение передачи */
    status = sdio_ll_cmd(
        SD_CMD12,
        0,
        SD_RESP_SHORT_CRC
    );

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD WRITE: CMD12 failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    /* Ждем завершения внутреннего программирования */
    status = sd_wait_ready(info);

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD WRITE: wait_ready failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    return SD_OK;
}
sd_status_t sd_read_blocks(
    sd_card_info_t *info,
    uint32_t lba,
    uint8_t *buffer,
    uint32_t count
)
{
    sd_status_t status;
    sd_status_t stop_status;
    uint32_t length;

    if (buffer == NULL || count == 0)
        return SD_ERR_IO;

    if (lba >= info->block_count ||
        count > (info->block_count - lba))
        return SD_ERR_IO;

    length = count * info->block_size;

    /* CMD18 - READ_MULTIPLE_BLOCK */
    status = sdio_ll_cmd(
        SD_CMD18,
        lba,
        SD_RESP_SHORT_CRC
    );

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD READ: CMD18 failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    /*
     * Чтение SDIO FIFO -> RAM через DMA.
     * length задаётся в байтах.
     */
    //status = sdio_ll_data_read_dma(buffer, length);

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD READ: DMA failed, LBA=%lu count=%lu length=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            (unsigned long)length,
            status);

        /*
         * При ошибке передачи пытаемся остановить CMD18.
         * Возвращаем исходную ошибку DMA.
         */
        stop_status = sdio_ll_cmd(
            SD_CMD12,
            0,
            SD_RESP_SHORT_CRC
        );

        if (stop_status != SD_OK) {
            SEGGER_RTT_printf(0,
                "SD READ: CMD12 after DMA error failed, status=%d\r\n",
                stop_status);
        }

        return status;
    }

    /* Завершаем multi-block read */
    status = sdio_ll_cmd(
        SD_CMD12,
        0,
        SD_RESP_SHORT_CRC
    );

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD READ: CMD12 failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    /* Проверяем готовность карты */
    status = sd_wait_ready(info);

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD READ: wait_ready failed, LBA=%lu count=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            status);
        return status;
    }

    return SD_OK;
}
#define SD_R1_READY_FOR_DATA    (1U << 8)
#define SD_R1_CURRENT_STATE_MASK (0xFU << 9)
#define SD_R1_CURRENT_STATE_TRAN (4U << 9)

static sd_status_t sd_wait_ready(sd_card_info_t *info)
{
    sd_status_t status;
    uint32_t response;

    while (1)
    {
        status = sdio_ll_cmd(
            SD_CMD13,
            info->rca << 16,
            SD_RESP_SHORT_CRC
        );

        if (status != SD_OK)
            return status;

        response = sdio_ll_get_short_response();

        if ((response & SD_R1_READY_FOR_DATA) &&
            ((response & SD_R1_CURRENT_STATE_MASK) ==
             SD_R1_CURRENT_STATE_TRAN))
        {
            return SD_OK;
        }
    }
}