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
    SD_CMD2,
    SD_CMD3,
    SD_CMD7,
    SD_CMD8,
    SD_CMD9,
    SD_CMD55,
    SD_ACMD41,
    SD_ACMD52,
    AD_ACMD6
    
} sdio_command_idx;

sd_status_t sdio_ll_cmd(
    uint8_t cmd_index,
    uint32_t arg,
    uint32_t resp_type
);
sd_status_t sdio_ll_reginit();
sd_status_t sdio_ll_set_clock(sdio_clock_t);
sd_status_t sdio_ll_set_bus_width(sdio_bus_width_t);

uint32_t sdio_ll_get_short_response();
void sdio_ll_get_long_response(uint32_t response[4]);