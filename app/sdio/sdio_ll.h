#pragma once
#include "stdint.h"

typedef enum 
{
    SD_OK = 0,
    SD_ERR_NO_CARD,
    SD_ERR_TIMEOUT,
    SD_ERR_CMD_CRC,
    SD_ERR_DATA_CRC,
    SD_ERR_DMA,
    SD_ERR_IO,
    SD_ERR_UNSUPPORTED,
} sd_status_t;

typedef enum 
{
    SD_BUS_1BIT,
    SD_BUS_4BIT,
} sdio_bus_width_t;

typedef enum 
{
    SD_CLK_INIT,
    SD_CLK_WORK,
} sdio_clock_t;

typedef enum
{
    SD_RESP_NONE = 0,       // Нет ответа: CMD0
    SD_RESP_SHORT_CRC,      // 48-битный ответ с CRC: R1/R6/R7
    SD_RESP_SHORT_NOCRC,    // 48-битный ответ без CRC: R3
    SD_RESP_LONG_CRC        // 136-битный ответ с CRC: R2
} sdio_resp_type_t;

typedef enum
{
    SD_CMD0 = 0,
    SD_CMD2 = 2,
    SD_CMD3 = 3,
    SD_ACMD6 = 6,
    SD_CMD7 = 7,
    SD_CMD8 = 8,
    SD_CMD9 = 9,
    SD_CMD12 = 12,
    SD_CMD13 = 13,
    SD_CMD18 = 18,
    SD_CMD25 = 25,
    SD_ACMD41 = 41,
    SD_ACMD52 = 52,
    SD_CMD55 = 55,
    
} sdio_command_idx;

sd_status_t sdio_ll_cmd(
    uint8_t cmd_index,
    uint32_t arg,
    sdio_resp_type_t resp_type
);

#define SDIO_POWER_ON (0x00000003U)

sd_status_t sdio_ll_reginit();
sd_status_t sdio_ll_power_on(void);
sd_status_t sdio_ll_clock_enable(void);

sd_status_t sdio_ll_set_clock(sdio_clock_t);
sd_status_t sdio_ll_set_bus_width(sdio_bus_width_t);

uint32_t sdio_ll_get_short_response();
void sdio_ll_get_long_response(uint32_t response[4]);

sd_status_t sdio_ll_data_write(
    const uint8_t *buffer,
    uint32_t length
);
sd_status_t sdio_ll_data_read(
    uint8_t *buffer,
    uint32_t length
);