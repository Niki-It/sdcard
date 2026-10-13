#include "i2s2.h"

#include <stdint.h>
#include <stdbool.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_spi.h"

#include "SEGGER_RTT.h"
#include "circular/ring.h"
#include "perf_counter/perf_counter.h"
#include "media/counters/audio_loss.h"

#define I2S_BUFFER_SAMPLES       1024U

#define I2S_RX_BUFFER_BYTES      4096U
#define I2S_RX_BUFFER_SAMPLES    (I2S_RX_BUFFER_BYTES / 2U)
#define I2S_RX_HALF_SAMPLES      (I2S_RX_BUFFER_SAMPLES / 2U)

#define AUDIO_RING_BLOCK_SIZE   (I2S_RX_BUFFER_BYTES / 2U)
#define AUDIO_RING_BLOCK_COUNT  32U

#define RX_TIME_LOG_COUNT       2048U

static uint16_t i2s_tx_buffer[I2S_BUFFER_SAMPLES];
static uint16_t i2s_rx_buffer[I2S_RX_BUFFER_SAMPLES];

static uint8_t audio_ring_storage[
    AUDIO_RING_BLOCK_SIZE * AUDIO_RING_BLOCK_COUNT
];

// uint16_t обеспечивает выравнивание для просмотра PCM.
static uint16_t audio_read_buffer[AUDIO_RING_BLOCK_SIZE / 2U];

ring_t audio_ring;

volatile audio_rx_stats_t audio_rx_stats = {0};

uint32_t audio_loss_current_blocks = 0;
uint32_t audio_loss_current_index = UINT32_MAX;

static volatile uint8_t i2s_tx_done = 0;
static volatile uint8_t i2s_tx_error = 0;
static volatile uint8_t i2s_rx_error = 0;
static volatile uint8_t i2s_transfer_active = 0;

// После остановки запрещает обработчику добавлять новые блоки.
static volatile uint8_t i2s_rx_accepting = 0;

volatile uint32_t rx_time_us[RX_TIME_LOG_COUNT];
volatile uint32_t rx_time_count = 0;

static uint32_t rx_prev_cycles = 0;
static bool rx_prev_valid = false;


static void rx_clear_dma_flags(void)
{
    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_HT3(DMA1);
    LL_DMA_ClearFlag_TE3(DMA1);
    LL_DMA_ClearFlag_DME3(DMA1);
    LL_DMA_ClearFlag_FE3(DMA1);
}


static void audio_loss_reset(void)
{
    audio_rx_stats = (audio_rx_stats_t){0};

    audio_loss_current_blocks = 0;
    audio_loss_current_index = UINT32_MAX;

    rx_time_count = 0;
    rx_prev_cycles = 0;
    rx_prev_valid = false;
}


static void rx_log_interval(void)
{
    uint32_t now = perf_counter_now();

    if (rx_prev_valid)
    {
        uint32_t index = rx_time_count;

        if (index < RX_TIME_LOG_COUNT)
        {
            rx_time_us[index] =
                perf_counter_cycles_to_us(now - rx_prev_cycles);

            rx_time_count = index + 1U;
        }
    }

    rx_prev_cycles = now;
    rx_prev_valid = true;
}


static bool audio_rx_push(uint8_t *block)
{
    ring_push_result_t result =
        ring_push_latest(&audio_ring, block);

    if (result == RING_PUSH_ERROR)
    {
        i2s_rx_error = 1;
        i2s_rx_accepting = 0;

        LL_SPI_DisableDMAReq_RX(SPI2);
        return false;
    }

    audio_rx_stats.rx_blocks++;

    if (result == RING_PUSH_ADDED)
    {
        // Обычное добавление завершает серию потерь.
        audio_loss_current_blocks = 0;
        audio_loss_current_index = UINT32_MAX;
        return true;
    }

    // Новый блок заменил самый новый блок полной очереди.
    audio_rx_stats.overruns++;

    if (audio_loss_current_blocks == 0U)
    {
        audio_rx_stats.loss_series_count++;

        uint32_t index = audio_rx_stats.loss_log_count;

        if (index < AUDIO_LOSS_LOG_CAPACITY)
        {
            audio_loss_current_index = index;

            audio_rx_stats.loss_log[index].first_rx_event =
                audio_rx_stats.rx_blocks;

            audio_rx_stats.loss_log[index].lost_blocks = 0;

            audio_rx_stats.loss_log_count = index + 1U;
        }
        else
        {
            audio_loss_current_index = UINT32_MAX;
            audio_rx_stats.loss_unlogged_series++;
        }
    }

    audio_loss_current_blocks++;

    if (audio_loss_current_index != UINT32_MAX)
    {
        audio_rx_stats.loss_log[
            audio_loss_current_index
        ].lost_blocks = audio_loss_current_blocks;
    }

    if (audio_loss_current_blocks > audio_rx_stats.loss_max_blocks)
    {
        audio_rx_stats.loss_max_blocks = audio_loss_current_blocks;
    }

    return true;
}


// DMA1 Stream 3 / Channel 0: SPI2_RX.
void i2s_dma_init_rx(void)
{
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3))
    {
    }

    LL_DMA_SetChannelSelection(
        DMA1, LL_DMA_STREAM_3, LL_DMA_CHANNEL_0
    );

    LL_DMA_ConfigTransfer(
        DMA1,
        LL_DMA_STREAM_3,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
        LL_DMA_MODE_CIRCULAR |
        LL_DMA_PERIPH_NOINCREMENT |
        LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PDATAALIGN_HALFWORD |
        LL_DMA_MDATAALIGN_HALFWORD |
        LL_DMA_PRIORITY_HIGH
    );

    LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_3);

    LL_DMA_ConfigAddresses(
        DMA1,
        LL_DMA_STREAM_3,
        LL_SPI_DMA_GetRegAddr(SPI2),
        (uint32_t)i2s_rx_buffer,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY
    );

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_3, I2S_RX_BUFFER_SAMPLES
    );

    rx_clear_dma_flags();

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_3);

    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);
    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}


// DMA1 Stream 4 / Channel 2: I2S2ext_TX.
void i2s_dma_init_tx(void)
{
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
    }

    LL_DMA_SetChannelSelection(
        DMA1, LL_DMA_STREAM_4, LL_DMA_CHANNEL_2
    );

    LL_DMA_ConfigTransfer(
        DMA1,
        LL_DMA_STREAM_4,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
        LL_DMA_MODE_NORMAL |
        LL_DMA_PERIPH_NOINCREMENT |
        LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PDATAALIGN_HALFWORD |
        LL_DMA_MDATAALIGN_HALFWORD |
        LL_DMA_PRIORITY_HIGH
    );

    LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_4);

    LL_DMA_ConfigAddresses(
        DMA1,
        LL_DMA_STREAM_4,
        (uint32_t)i2s_tx_buffer,
        LL_SPI_DMA_GetRegAddr(I2S2ext),
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH
    );

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_4, I2S_BUFFER_SAMPLES
    );

    LL_DMA_ClearFlag_TC4(DMA1);
    LL_DMA_ClearFlag_HT4(DMA1);
    LL_DMA_ClearFlag_TE4(DMA1);
    LL_DMA_ClearFlag_DME4(DMA1);
    LL_DMA_ClearFlag_FE4(DMA1);

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_4);

    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}


void i2s_dma_init(void)
{
    // GPIO и PLLI2S должны быть настроены заранее.
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    LL_I2S_InitTypeDef i2s_init = {0};

    i2s_init.Mode          = LL_I2S_MODE_MASTER_RX;
    i2s_init.Standard      = LL_I2S_STANDARD_PHILIPS;
    i2s_init.DataFormat    = LL_I2S_DATAFORMAT_16B;
    i2s_init.MCLKOutput    = LL_I2S_MCLK_OUTPUT_ENABLE;
    i2s_init.AudioFreq     = LL_I2S_AUDIOFREQ_48K;
    i2s_init.ClockPolarity = LL_I2S_POLARITY_LOW;

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_I2S_Init(SPI2, &i2s_init);

    // Сохранена твоя настройка делителя.
    LL_I2S_ConfigPrescaler(
        SPI2, 3, LL_I2S_PRESCALER_PARITY_ODD
    );

    LL_I2S_InitFullDuplex(I2S2ext, &i2s_init);

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_DisableIT_RXNE(SPI2);
    LL_SPI_DisableIT_TXE(I2S2ext);

    if (!ring_init(
            &audio_ring,
            audio_ring_storage,
            AUDIO_RING_BLOCK_SIZE,
            AUDIO_RING_BLOCK_COUNT))
    {
        i2s_rx_error = 1;
        return;
    }

    i2s_dma_init_rx();
    i2s_dma_init_tx();
}


void i2s_stop(void)
{
    // Отложенные RX IRQ больше не должны наполнять Ring.
    i2s_rx_accepting = 0;

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
    }

    rx_clear_dma_flags();
    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);

    i2s_transfer_active = 0;
}


// Аппаратная ошибка или некорректная очередь.
// Обычные потери при замене блока сюда не относятся.
bool i2s_rx_has_error(void)
{
    return i2s_rx_error != 0U;
}


void DMA1_Stream3_IRQHandler(void)
{
    if (!i2s_rx_accepting)
    {
        rx_clear_dma_flags();
        return;
    }

    if (LL_DMA_IsActiveFlag_TE3(DMA1) ||
        LL_DMA_IsActiveFlag_DME3(DMA1) ||
        LL_DMA_IsActiveFlag_FE3(DMA1))
    {
        rx_clear_dma_flags();

        i2s_rx_error = 1;
        i2s_rx_accepting = 0;

        LL_SPI_DisableDMAReq_RX(SPI2);
        return;
    }

    if (LL_DMA_IsActiveFlag_HT3(DMA1))
    {
        LL_DMA_ClearFlag_HT3(DMA1);

        rx_log_interval();

        if (!audio_rx_push((uint8_t *)&i2s_rx_buffer[0]))
        {
            return;
        }
    }

    if (LL_DMA_IsActiveFlag_TC3(DMA1))
    {
        LL_DMA_ClearFlag_TC3(DMA1);

        rx_log_interval();

        if (!audio_rx_push(
                (uint8_t *)&i2s_rx_buffer[I2S_RX_HALF_SAMPLES]))
        {
            return;
        }
    }
}


void DMA1_Stream4_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TE4(DMA1) ||
        LL_DMA_IsActiveFlag_DME4(DMA1))
    {
        LL_DMA_ClearFlag_TE4(DMA1);
        LL_DMA_ClearFlag_DME4(DMA1);

        i2s_tx_error = 1;
    }

    if (LL_DMA_IsActiveFlag_TC4(DMA1))
    {
        LL_DMA_ClearFlag_TC4(DMA1);

        i2s_tx_done = 1;
    }
}


int i2s_rx_start_block(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    if (audio_ring.storage == NULL ||
        audio_ring.block_size != AUDIO_RING_BLOCK_SIZE ||
        audio_ring.block_count != AUDIO_RING_BLOCK_COUNT)
    {
        return -1;
    }

    i2s_rx_accepting = 0;

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3))
    {
    }

    rx_clear_dma_flags();
    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_3, I2S_RX_BUFFER_SAMPLES
    );

    ring_clear(&audio_ring);
    audio_loss_reset();

    i2s_rx_error = 0;

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_3);

    return 0;
}


int i2s_tx_start_block(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
    }

    LL_DMA_ClearFlag_TC4(DMA1);
    LL_DMA_ClearFlag_HT4(DMA1);
    LL_DMA_ClearFlag_TE4(DMA1);
    LL_DMA_ClearFlag_DME4(DMA1);
    LL_DMA_ClearFlag_FE4(DMA1);

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_4, I2S_BUFFER_SAMPLES
    );

    i2s_tx_done = 0;
    i2s_tx_error = 0;

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_4);

    return 0;
}


int i2s_start_blocks(void)
{
    if (i2s_transfer_active)
    {
        return -1;
    }

    uint32_t rx_enabled =
        LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3);

    uint32_t tx_enabled =
        LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4);

    if (!rx_enabled && !tx_enabled)
    {
        return -1;
    }

    i2s_transfer_active = 1;

    if (rx_enabled)
    {
        i2s_rx_accepting = 1;
        LL_SPI_EnableDMAReq_RX(SPI2);
    }

    if (tx_enabled)
    {
        LL_SPI_EnableDMAReq_TX(I2S2ext);
    }

    // Сохранён порядок Slave -> Master.
    LL_I2S_Enable(I2S2ext);
    LL_I2S_Enable(SPI2);

    return 0;
}


#define I2S_TEST_RX_BLOCK_COUNT 4U

void i2s_test_rx(void)
{
    if (i2s_rx_start_block() != 0)
    {
        SEGGER_RTT_printf(0, "I2S RX DMA prepare error\r\n");
        return;
    }

    if (i2s_start_blocks() != 0)
    {
        i2s_stop();
        SEGGER_RTT_printf(0, "I2S RX start error\r\n");
        return;
    }

    while (audio_rx_stats.rx_blocks < I2S_TEST_RX_BLOCK_COUNT &&
           !i2s_rx_error)
    {
    }

    i2s_stop();

    SEGGER_RTT_printf(
        0,
        "RX blocks=%lu, lost=%lu, ring_count=%lu\r\n",
        (unsigned long)audio_rx_stats.rx_blocks,
        (unsigned long)audio_rx_stats.overruns,
        (unsigned long)ring_count(&audio_ring)
    );

    if (i2s_rx_error)
    {
        SEGGER_RTT_printf(0, "I2S RX DMA/ring error\r\n");
        return;
    }

    for (uint32_t block = 0;
         block < I2S_TEST_RX_BLOCK_COUNT;
         block++)
    {
        if (!ring_pop(&audio_ring, (uint8_t *)audio_read_buffer))
        {
            SEGGER_RTT_printf(0, "Ring pop failed\r\n");
            break;
        }

        SEGGER_RTT_printf(
            0, "Block %lu: ", (unsigned long)block
        );

        for (uint32_t i = 0; i < 20U; i++)
        {
            SEGGER_RTT_printf(
                0, "%d ", (int)(int16_t)audio_read_buffer[i]
            );
        }

        SEGGER_RTT_printf(0, "\r\n");
    }
}


#define I2S_TEST_AMPLITUDE     8000
#define I2S_TEST_PERIOD_FRAMES 48U

void i2s_test_tx(void)
{
    for (uint32_t i = 0; i < I2S_BUFFER_SAMPLES; i += 2U)
    {
        uint32_t frame = i / 2U;
        int16_t sample;

        if ((frame % I2S_TEST_PERIOD_FRAMES) <
            (I2S_TEST_PERIOD_FRAMES / 2U))
        {
            sample = I2S_TEST_AMPLITUDE;
        }
        else
        {
            sample = -I2S_TEST_AMPLITUDE;
        }

        i2s_tx_buffer[i]     = (uint16_t)sample;
        i2s_tx_buffer[i + 1] = (uint16_t)sample;
    }

    if (i2s_tx_start_block() != 0)
    {
        SEGGER_RTT_printf(0, "I2S TX DMA start error\r\n");
        return;
    }

    if (i2s_start_blocks() != 0)
    {
        i2s_stop();
        SEGGER_RTT_printf(0, "I2S TX start error\r\n");
        return;
    }

    while (!i2s_tx_done && !i2s_tx_error)
    {
    }

    if (i2s_tx_error)
    {
        i2s_stop();
        SEGGER_RTT_printf(0, "I2S TX DMA error\r\n");
        return;
    }

    while (LL_SPI_IsActiveFlag_BSY(I2S2ext))
    {
    }

    i2s_stop();

    SEGGER_RTT_printf(0, "I2S TX complete\r\n");
}