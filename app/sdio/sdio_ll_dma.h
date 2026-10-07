#pragma once
#include "sdio_ll.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_bus.h"
#include "stdint.h"
#include "stdbool.h"

#define SDIO_DMA_RX_STREAM   LL_DMA_STREAM_3
#define SDIO_DMA_TX_STREAM   LL_DMA_STREAM_6
#define SDIO_DMA_CHANNEL     LL_DMA_CHANNEL_4

sd_status_t sdio_ll_dma_init(void);
sd_status_t sdio_ll_prepare_dma_rx(
    uint8_t *buffer,
    uint32_t length
);
sd_status_t sdio_ll_prepare_dma_tx(
    const uint8_t *buffer,
    uint32_t length
);
sd_status_t sdio_ll_wait_dma_tx(void);
sd_status_t sdio_ll_wait_dma_rx(void);