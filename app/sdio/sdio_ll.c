#include "sdio_ll.h"
#include <stm32f405xx.h>

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