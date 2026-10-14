#pragma once

#include <stdbool.h>
#include <stdint.h>

#define I2S_AUDIO_BLOCK_BYTES   2048U
#define I2S_AUDIO_BLOCK_COUNT   32U

/*
 * GPIO, PLLI2S и perf_counter_init() должны быть
 * настроены до использования.
 *
 * Инициализация вызывается один раз.
 */
void i2s_dma_init(void);

/*
 * Все управляющие функции вызываются из main.
 * RX и TX одновременно не поддерживаются.
 */
bool i2s_is_busy(void);

/* RX: прежний интерфейс для wav_encoder. */
int i2s_rx_start_block(void);
bool i2s_rx_has_error(void);

/*
 * TX:
 *
 * 1. i2s_tx_prepare(total_blocks)
 * 2. Заполнить audio_ring.
 * 3. i2s_tx_start_block()
 * 4. i2s_start_blocks()
 *
 * total_blocks включает последний блок,
 * дополненный нулями до I2S_AUDIO_BLOCK_BYTES.
 */
int i2s_tx_prepare(uint32_t total_blocks);
int i2s_tx_start_block(void);

bool i2s_tx_has_error(void);
bool i2s_tx_is_done(void);
uint32_t i2s_tx_underrun_count(void);

/* Запускает предварительно подготовленный RX или TX. */
int i2s_start_blocks(void);

/*
 * Останавливает I2S и DMA.
 * Не очищает Ring и диагностические счётчики.
 * Не отменяет резерв Ring.
 */
void i2s_stop(void);

void i2s_test_rx(void);
void i2s_test_tx(void);