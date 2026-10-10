#include "min_sdio_driver.h"
#include "stm32f4xx_ll_utils.h"
#include "core_cm4.h" // Для доступа к DWT
#include "SEGGER_RTT.h"
#include "stdbool.h"
#include "sdio.h"
#include "string.h"
#include "sdio_ll_dma.h"

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
    sdio_ll_dma_init();

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

    if (status != SD_OK)
        return status;

    status = sdio_ll_prepare_dma_tx(buffer, length);
    if (status != SD_OK) {
        return status;
    }


    if (status == SD_OK)
        status = sdio_ll_wait_dma_tx();

    if (status != SD_OK) {
        SEGGER_RTT_printf(0,
            "SD WRITE: failed, LBA=%lu count=%lu length=%lu status=%d\r\n",
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
    status = sdio_ll_prepare_dma_rx(buffer, length);

    if (status == SD_OK)
        status = sdio_ll_wait_dma_rx();

    if (status != SD_OK) {

        SEGGER_RTT_printf(0,
            "SD READ: DMA failed, LBA=%lu count=%lu length=%lu status=%d\r\n",
            (unsigned long)lba,
            (unsigned long)count,
            (unsigned long)length,
            status);

        /*
         * При ошибке пытаемся остановить CMD18.
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


#define SD_TEST_TOTAL_BLOCKS       300000U
#define SD_TEST_BLOCKS_PER_BUFFER  10U
#define SD_TEST_BLOCK_SIZE         512U
#define SD_TEST_LOG_INTERVAL       1000U

#if SD_TEST_BLOCKS_PER_BUFFER == 0
#error "SD_TEST_BLOCKS_PER_BUFFER must be greater than zero"
#endif

/*
 * Новый тестовый паттерн.
 * Используем LBA + смещение внутри сектора.
 * Каждый 32-битный элемент проходит перемешивание.
 */
static uint8_t SDIO_TestPattern(uint32_t lba, uint32_t offset)
{
    uint32_t word_index = offset / 4U;
    uint32_t byte_index = offset & 3U;

    uint32_t x = 0x9E3779B9U;

    x ^= lba * 0x85EBCA6BU;
    x ^= word_index * 0xC2B2AE35U;

    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;

    return (uint8_t)(x >> (byte_index * 8U));
}


/*
 * Запись тестовых блоков.
 */
uint32_t SDIO_TestCard(void)
{
    sd_card_info_t info;
    sd_status_t status;

    static uint8_t test_buffer[
        SD_TEST_BLOCK_SIZE * SD_TEST_BLOCKS_PER_BUFFER
    ] __attribute__((aligned(16)));

    SEGGER_RTT_printf(0,
        "SD TEST: write start, blocks=%lu batch=%lu\r\n",
        (unsigned long)SD_TEST_TOTAL_BLOCKS,
        (unsigned long)SD_TEST_BLOCKS_PER_BUFFER
    );

    status = sd_init(&info);

    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: init failed, status=%d\r\n",
            status);
        return (uint32_t)status;
    }

    if (info.block_size != SD_TEST_BLOCK_SIZE)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: unexpected block size=%lu\r\n",
            (unsigned long)info.block_size);
        return (uint32_t)SD_ERR_IO;
    }

    if (info.block_count < SD_TEST_TOTAL_BLOCKS)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: card too small, blocks=%lu\r\n",
            (unsigned long)info.block_count);
        return (uint32_t)SD_ERR_IO;
    }

    for (uint32_t lba = 0;
         lba < SD_TEST_TOTAL_BLOCKS;)
    {
        uint32_t count = SD_TEST_BLOCKS_PER_BUFFER;

        if (count > (SD_TEST_TOTAL_BLOCKS - lba))
            count = SD_TEST_TOTAL_BLOCKS - lba;

        /*
         * Формируем разные данные для каждого
         * сектора внутри DMA-буфера.
         */
        for (uint32_t block = 0; block < count; block++)
        {
            for (uint32_t i = 0; i < SD_TEST_BLOCK_SIZE; i++)
            {
                uint32_t index =
                    block * SD_TEST_BLOCK_SIZE + i;

                test_buffer[index] =
                    SDIO_TestPattern(lba + block, i);
            }
        }

        status = sd_write_blocks(
            &info,
            lba,
            test_buffer,
            count
        );

        if (status != SD_OK)
        {
            SEGGER_RTT_printf(0,
                "SD TEST: WRITE FAILED LBA=%lu count=%lu status=%d\r\n",
                (unsigned long)lba,
                (unsigned long)count,
                status);

            return (uint32_t)status;
        }

        lba += count;

        if ((lba % SD_TEST_LOG_INTERVAL) < count ||
            lba == SD_TEST_TOTAL_BLOCKS)
        {
            SEGGER_RTT_printf(0,
                "SD TEST: written %lu/%lu\r\n",
                (unsigned long)lba,
                (unsigned long)SD_TEST_TOTAL_BLOCKS);
        }
    }

    SEGGER_RTT_printf(0,
        "SD TEST: all %lu blocks written OK\r\n",
        (unsigned long)SD_TEST_TOTAL_BLOCKS);

    return (uint32_t)SD_OK;
}


/*
 * Чтение и проверка тестовых блоков.
 */
uint32_t SDIO_TestCardRead(void)
{
    sd_card_info_t info;
    sd_status_t status;

    static uint8_t test_buffer[
        SD_TEST_BLOCK_SIZE * SD_TEST_BLOCKS_PER_BUFFER
    ] __attribute__((aligned(16)));

    SEGGER_RTT_printf(0,
        "SD TEST: read start, blocks=%lu batch=%lu\r\n",
        (unsigned long)SD_TEST_TOTAL_BLOCKS,
        (unsigned long)SD_TEST_BLOCKS_PER_BUFFER
    );

    status = sd_init(&info);

    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: init failed, status=%d\r\n",
            status);
        return (uint32_t)status;
    }

    if (info.block_size != SD_TEST_BLOCK_SIZE)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: unexpected block size=%lu\r\n",
            (unsigned long)info.block_size);
        return (uint32_t)SD_ERR_IO;
    }

    if (info.block_count < SD_TEST_TOTAL_BLOCKS)
    {
        SEGGER_RTT_printf(0,
            "SD TEST: card too small, blocks=%lu\r\n",
            (unsigned long)info.block_count);
        return (uint32_t)SD_ERR_IO;
    }

    for (uint32_t lba = 0;
         lba < SD_TEST_TOTAL_BLOCKS;)
    {
        uint32_t count = SD_TEST_BLOCKS_PER_BUFFER;

        if (count > (SD_TEST_TOTAL_BLOCKS - lba))
            count = SD_TEST_TOTAL_BLOCKS - lba;

        uint32_t length = count * SD_TEST_BLOCK_SIZE;

        /*
         * Затираем буфер перед чтением.
         */
        memset(test_buffer, 0x00, length);

        status = sd_read_blocks(
            &info,
            lba,
            test_buffer,
            count
        );

        if (status != SD_OK)
        {
            SEGGER_RTT_printf(0,
                "SD TEST: READ FAILED LBA=%lu count=%lu status=%d\r\n",
                (unsigned long)lba,
                (unsigned long)count,
                status);

            return (uint32_t)status;
        }

        /*
         * Проверяем каждый байт каждого сектора.
         */
        for (uint32_t block = 0; block < count; block++)
        {
            for (uint32_t i = 0; i < SD_TEST_BLOCK_SIZE; i++)
            {
                uint32_t index =
                    block * SD_TEST_BLOCK_SIZE + i;

                uint8_t expected =
                    SDIO_TestPattern(lba + block, i);

                if (test_buffer[index] != expected)
                {
                    SEGGER_RTT_printf(0,
                        "SD TEST: VERIFY FAILED "
                        "LBA=%lu offset=%lu "
                        "got=0x%02X expected=0x%02X\r\n",
                        (unsigned long)(lba + block),
                        (unsigned long)i,
                        (unsigned int)test_buffer[index],
                        (unsigned int)expected);

                    return (uint32_t)SD_ERR_IO;
                }
            }
        }

        lba += count;

        if ((lba % SD_TEST_LOG_INTERVAL) < count ||
            lba == SD_TEST_TOTAL_BLOCKS)
        {
            SEGGER_RTT_printf(0,
                "SD TEST: verified %lu/%lu\r\n",
                (unsigned long)lba,
                (unsigned long)SD_TEST_TOTAL_BLOCKS);
        }
    }

    SEGGER_RTT_printf(0,
        "SD TEST: all %lu blocks verified OK\r\n",
        (unsigned long)SD_TEST_TOTAL_BLOCKS);

    return (uint32_t)SD_OK;
}
