#include "i2s2.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_spi.h"

#include "SEGGER_RTT.h"
#include "circular/ring.h"
#include "perf_counter/perf_counter.h"
#include "media/counters/audio_loss.h"

#define DMA_BUFFER_BYTES       (2U * I2S_AUDIO_BLOCK_BYTES)
#define DMA_BUFFER_HALFWORDS   (DMA_BUFFER_BYTES / 2U)
#define DMA_HALF_HALFWORDS     (I2S_AUDIO_BLOCK_BYTES / 2U)

#define RX_TIME_LOG_COUNT      2048U

/* Ограничение ожидания аппаратных флагов, не миллисекунды. */
#define HW_WAIT_LIMIT          1000000U

typedef enum
{
    I2S_IDLE = 0,
    I2S_RX_PREPARED,
    I2S_RX_RUNNING,
    I2S_TX_PREPARED,
    I2S_TX_ARMED,
    I2S_TX_RUNNING
} i2s_state_t;

/*
 * Эти массивы должны находиться в обычной SRAM,
 * доступной DMA, не в CCMRAM.
 */
static uint16_t i2s_dma_buffer[DMA_BUFFER_HALFWORDS]
    __attribute__((aligned(4)));

static uint8_t audio_ring_storage[
    I2S_AUDIO_BLOCK_BYTES * I2S_AUDIO_BLOCK_COUNT
] __attribute__((aligned(4)));

ring_t audio_ring;

static bool initialized;
static volatile i2s_state_t state = I2S_IDLE;

static volatile uint8_t rx_error;
static volatile uint8_t tx_error;
static volatile uint8_t tx_done;

static volatile uint32_t tx_underruns;

/* Меняются перед запуском или внутри TX IRQ. */
static uint32_t tx_total_blocks;
static uint32_t tx_loaded_blocks;
static uint32_t tx_completed_blocks;

static bool tx_half_has_audio[2];
static bool tx_draining;

static uint32_t rx_expected_half;
static uint32_t tx_expected_half;

/* Сохранена диагностика RX из твоего файла. */
volatile audio_rx_stats_t audio_rx_stats = {0};

uint32_t audio_loss_current_blocks;
uint32_t audio_loss_current_index = UINT32_MAX;

volatile uint32_t rx_time_us[RX_TIME_LOG_COUNT];
volatile uint32_t rx_time_count;

static uint32_t rx_prev_cycles;
static bool rx_prev_valid;


static uint32_t irq_lock(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return key;
}


static void irq_unlock(uint32_t key)
{
    __DMB();
    __set_PRIMASK(key);
}


static uint16_t *dma_half(uint32_t half)
{
    return &i2s_dma_buffer[half * DMA_HALF_HALFWORDS];
}


static void rx_clear_flags(void)
{
    LL_DMA_ClearFlag_TC3(DMA1);
    LL_DMA_ClearFlag_HT3(DMA1);
    LL_DMA_ClearFlag_TE3(DMA1);
    LL_DMA_ClearFlag_DME3(DMA1);
    LL_DMA_ClearFlag_FE3(DMA1);
}


static void tx_clear_flags(void)
{
    LL_DMA_ClearFlag_TC4(DMA1);
    LL_DMA_ClearFlag_HT4(DMA1);
    LL_DMA_ClearFlag_TE4(DMA1);
    LL_DMA_ClearFlag_DME4(DMA1);
    LL_DMA_ClearFlag_FE4(DMA1);
}


/* Вызывать при выключенном SPI2/I2S. */
static void clear_i2s_status(void)
{
    volatile uint32_t dummy;

    /* Очистка RXNE/OVR: DR, затем SR. */
    dummy = SPI2->DR;
    dummy = SPI2->SR;

    /* Чтение SR очищает UDR перед новым TX. */
    dummy = I2S2ext->SR;
    (void)dummy;
}


static void audio_loss_reset(void)
{
    audio_rx_stats = (audio_rx_stats_t){0};

    audio_loss_current_blocks = 0U;
    audio_loss_current_index = UINT32_MAX;

    rx_time_count = 0U;
    rx_prev_cycles = 0U;
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
        return false;
    }

    audio_rx_stats.rx_blocks++;

    if (result == RING_PUSH_ADDED)
    {
        audio_loss_current_blocks = 0U;
        audio_loss_current_index = UINT32_MAX;
        return true;
    }

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

            audio_rx_stats.loss_log[index].lost_blocks = 0U;
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


/*
 * Вызывается только при остановленном потоке.
 * RX: DMA1 Stream3 Channel0.
 */
static void configure_rx_dma(void)
{
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
        (uint32_t)i2s_dma_buffer,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY
    );

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_3, DMA_BUFFER_HALFWORDS
    );

    rx_clear_flags();

    LL_DMA_EnableIT_HT(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_3);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_3);

    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);
}


/*
 * TX: DMA1 Stream4 Channel2.
 * Теперь круговой режим с HT и TC.
 */
static void configure_tx_dma(void)
{
    LL_DMA_SetChannelSelection(
        DMA1, LL_DMA_STREAM_4, LL_DMA_CHANNEL_2
    );

    LL_DMA_ConfigTransfer(
        DMA1,
        LL_DMA_STREAM_4,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
        LL_DMA_MODE_CIRCULAR |
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
        (uint32_t)i2s_dma_buffer,
        LL_SPI_DMA_GetRegAddr(I2S2ext),
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH
    );

    LL_DMA_SetDataLength(
        DMA1, LL_DMA_STREAM_4, DMA_BUFFER_HALFWORDS
    );

    tx_clear_flags();

    LL_DMA_EnableIT_HT(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_4);

    NVIC_ClearPendingIRQ(DMA1_Stream4_IRQn);
}


void i2s_dma_init(void)
{
    if (initialized)
    {
        return;
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    uint32_t limit = HW_WAIT_LIMIT;

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
        if (--limit == 0U)
        {
            rx_error = 1U;
            tx_error = 1U;
            return;
        }
    }

    LL_I2S_InitTypeDef cfg = {0};

    cfg.Mode          = LL_I2S_MODE_MASTER_RX;
    cfg.Standard      = LL_I2S_STANDARD_PHILIPS;
    cfg.DataFormat    = LL_I2S_DATAFORMAT_16B;
    cfg.MCLKOutput    = LL_I2S_MCLK_OUTPUT_ENABLE;
    cfg.AudioFreq     = LL_I2S_AUDIOFREQ_48K;
    cfg.ClockPolarity = LL_I2S_POLARITY_LOW;

    LL_I2S_Init(SPI2, &cfg);

    /* Сохранена твоя настройка PLLI2S/делителя. */
    LL_I2S_ConfigPrescaler(
        SPI2, 3, LL_I2S_PRESCALER_PARITY_ODD
    );

    LL_I2S_InitFullDuplex(I2S2ext, &cfg);

    LL_I2S_DisableIT_RXNE(SPI2);
    LL_SPI_DisableIT_TXE(I2S2ext);

    if (!ring_init(
            &audio_ring,
            audio_ring_storage,
            I2S_AUDIO_BLOCK_BYTES,
            I2S_AUDIO_BLOCK_COUNT))
    {
        rx_error = 1U;
        tx_error = 1U;
        return;
    }

    configure_rx_dma();
    configure_tx_dma();

    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);

    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    clear_i2s_status();

    rx_error = 0U;
    tx_error = 0U;
    state = I2S_IDLE;
    initialized = true;
}


bool i2s_is_busy(void)
{
    return state != I2S_IDLE;
}


bool i2s_rx_has_error(void)
{
    return rx_error != 0U;
}


bool i2s_tx_has_error(void)
{
    return tx_error != 0U;
}


bool i2s_tx_is_done(void)
{
    return tx_done != 0U;
}


uint32_t i2s_tx_underrun_count(void)
{
    uint32_t key = irq_lock();
    uint32_t result = tx_underruns;
    irq_unlock(key);
    return result;
}


void i2s_stop(void)
{
    /*
     * Сначала запрещаем IRQ работать с Ring/буфером.
     * Долгие аппаратные ожидания выполняем уже без
     * глобального запрета прерываний.
     */
    uint32_t key = irq_lock();

    i2s_state_t previous = state;
    state = I2S_IDLE;

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    irq_unlock(key);

    uint32_t limit = HW_WAIT_LIMIT;

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {
        if (--limit == 0U)
        {
            rx_error = 1U;
            tx_error = 1U;
            initialized = false;
            break;
        }
    }

    /*
     * Master ещё выдаёт такты.
     * Даём передатчику закончить содержимое DR/shift register.
     */
    if (previous == I2S_TX_RUNNING)
    {
        limit = HW_WAIT_LIMIT;

        while (!LL_SPI_IsActiveFlag_TXE(I2S2ext) ||
               LL_SPI_IsActiveFlag_BSY(I2S2ext))
        {
            if (--limit == 0U)
            {
                tx_error = 1U;
                break;
            }
        }
    }

    LL_I2S_Disable(I2S2ext);
    LL_I2S_Disable(SPI2);

    rx_clear_flags();
    tx_clear_flags();

    NVIC_ClearPendingIRQ(DMA1_Stream3_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Stream4_IRQn);

    clear_i2s_status();
}


int i2s_rx_start_block(void)
{
    if (!initialized || state != I2S_IDLE)
    {
        return -1;
    }

    if (!ring_clear(&audio_ring))
    {
        return -1;
    }

    clear_i2s_status();
    audio_loss_reset();

    rx_error = 0U;
    rx_expected_half = 0U;

    configure_rx_dma();

    state = I2S_RX_PREPARED;
    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_3);

    return 0;
}


int i2s_tx_prepare(uint32_t total_blocks)
{
    if (!initialized ||
        state != I2S_IDLE ||
        total_blocks == 0U)
    {
        return -1;
    }

    if (!ring_clear(&audio_ring))
    {
        return -1;
    }

    clear_i2s_status();

    tx_error = 0U;
    tx_done = 0U;
    tx_underruns = 0U;

    tx_total_blocks = total_blocks;
    tx_loaded_blocks = 0U;
    tx_completed_blocks = 0U;

    tx_half_has_audio[0] = false;
    tx_half_has_audio[1] = false;

    tx_draining = false;
    tx_expected_half = 0U;

    memset(i2s_dma_buffer, 0, sizeof(i2s_dma_buffer));

    state = I2S_TX_PREPARED;
    return 0;
}


/*
 * Загружает следующий блок либо тишину.
 * tx_loaded_blocks увеличивается только для реального блока.
 */
static void tx_fill_half(uint32_t half)
{
    uint16_t *destination = dma_half(half);

    tx_half_has_audio[half] = false;

    if (tx_loaded_blocks < tx_total_blocks)
    {
        if (ring_pop(&audio_ring, destination))
        {
            tx_loaded_blocks++;
            tx_half_has_audio[half] = true;
            return;
        }

        tx_underruns++;
    }

    memset(destination, 0, I2S_AUDIO_BLOCK_BYTES);
}


int i2s_tx_start_block(void)
{
    if (state != I2S_TX_PREPARED)
    {
        return -1;
    }

    uint32_t needed = tx_total_blocks >= 2U ? 2U : 1U;

    if (ring_count(&audio_ring) < needed)
    {
        return -1;
    }

    tx_fill_half(0U);
    tx_fill_half(1U);

    configure_tx_dma();

    state = I2S_TX_ARMED;
    __DMB();

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_4);
    return 0;
}


int i2s_start_blocks(void)
{
    if (state == I2S_RX_PREPARED)
    {
        state = I2S_RX_RUNNING;

        LL_SPI_EnableDMAReq_RX(SPI2);
        LL_I2S_Enable(SPI2);

        return 0;
    }

    if (state != I2S_TX_ARMED)
    {
        return -1;
    }

    state = I2S_TX_RUNNING;

    /*
     * Slave включаем и загружаем его DR до запуска
     * тактов от Master.
     */
    LL_I2S_Enable(I2S2ext);
    LL_SPI_EnableDMAReq_TX(I2S2ext);

    uint32_t limit = HW_WAIT_LIMIT;

    while (LL_DMA_GetDataLength(DMA1, LL_DMA_STREAM_4) ==
           DMA_BUFFER_HALFWORDS)
    {
        if (tx_error || --limit == 0U)
        {
            tx_error = 1U;
            i2s_stop();
            return -1;
        }
    }

    __DSB();

    /* SPI2 в MASTER_RX генерирует CK/WS/MCLK для I2S2ext. */
    LL_I2S_Enable(SPI2);

    return 0;
}


/*
 * Проверяем, что DMA сейчас работает с другой половиной.
 * Это дополнительная защита от сильно задержанного IRQ.
 */
static bool half_available(uint32_t stream, uint32_t half)
{
    uint32_t remaining = LL_DMA_GetDataLength(DMA1, stream);

    if (half == 0U)
    {
        return remaining != 0U &&
               remaining <= DMA_HALF_HALFWORDS;
    }

    return remaining > DMA_HALF_HALFWORDS;
}


static void rx_fail(void)
{
    rx_error = 1U;
    LL_SPI_DisableDMAReq_RX(SPI2);
    rx_clear_flags();
}


static void tx_fail(void)
{
    tx_error = 1U;
    LL_SPI_DisableDMAReq_TX(I2S2ext);
    tx_clear_flags();
}


void DMA1_Stream3_IRQHandler(void)
{
    if (state != I2S_RX_RUNNING || rx_error)
    {
        rx_clear_flags();
        return;
    }

    if (LL_DMA_IsActiveFlag_TE3(DMA1) ||
        LL_DMA_IsActiveFlag_DME3(DMA1) ||
        LL_DMA_IsActiveFlag_FE3(DMA1) ||
        (SPI2->SR & SPI_SR_OVR) != 0U)
    {
        rx_fail();
        return;
    }

    bool ht = LL_DMA_IsActiveFlag_HT3(DMA1) != 0U;
    bool tc = LL_DMA_IsActiveFlag_TC3(DMA1) != 0U;

    if (!ht && !tc)
    {
        return;
    }

    /* Обе половины успели завершиться: IRQ обслужен поздно. */
    if (ht && tc)
    {
        rx_fail();
        return;
    }

    uint32_t half = ht ? 0U : 1U;

    if (half != rx_expected_half ||
        !half_available(LL_DMA_STREAM_3, half))
    {
        rx_fail();
        return;
    }

    if (ht)
    {
        LL_DMA_ClearFlag_HT3(DMA1);
    }
    else
    {
        LL_DMA_ClearFlag_TC3(DMA1);
    }

    rx_log_interval();

    if (!audio_rx_push((uint8_t *)dma_half(half)) ||
        !half_available(LL_DMA_STREAM_3, half))
    {
        rx_fail();
        return;
    }

    rx_expected_half ^= 1U;
}


void DMA1_Stream4_IRQHandler(void)
{
    if (state != I2S_TX_RUNNING || tx_error)
    {
        tx_clear_flags();
        return;
    }

    uint32_t status = I2S2ext->SR;

    if (LL_DMA_IsActiveFlag_TE4(DMA1) ||
        LL_DMA_IsActiveFlag_DME4(DMA1) ||
        LL_DMA_IsActiveFlag_FE4(DMA1) ||
        (status & SPI_SR_UDR) != 0U)
    {
        tx_fail();
        return;
    }

    bool ht = LL_DMA_IsActiveFlag_HT4(DMA1) != 0U;
    bool tc = LL_DMA_IsActiveFlag_TC4(DMA1) != 0U;

    if (!ht && !tc)
    {
        return;
    }

    if (ht && tc)
    {
        tx_fail();
        return;
    }

    uint32_t half = ht ? 0U : 1U;

    if (half != tx_expected_half ||
        !half_available(LL_DMA_STREAM_4, half))
    {
        tx_fail();
        return;
    }

    if (ht)
    {
        LL_DMA_ClearFlag_HT4(DMA1);
    }
    else
    {
        LL_DMA_ClearFlag_TC4(DMA1);
    }

    /*
     * После последнего аудиоблока ждём ещё одну границу
     * половины DMA. За это время остаток звука выйдет
     * из DR/shift register; DMA уже передаёт тишину.
     */
    if (tx_draining)
    {
        tx_done = 1U;
    }

    if (tx_half_has_audio[half])
    {
        tx_completed_blocks++;
    }

    tx_fill_half(half);

    if (!half_available(LL_DMA_STREAM_4, half))
    {
        tx_fail();
        return;
    }

    if (tx_completed_blocks == tx_total_blocks)
    {
        tx_draining = true;
    }

    tx_expected_half ^= 1U;

    /*
     * После tx_done DMA продолжает передавать нули,
     * пока main не вызовет i2s_stop().
     */
}


void i2s_test_rx(void)
{
    if (i2s_rx_start_block() != 0)
    {
        SEGGER_RTT_printf(0, "RX prepare error\r\n");
        return;
    }

    if (i2s_start_blocks() != 0)
    {
        i2s_stop();
        return;
    }

    uint32_t started = perf_counter_now();

    while (audio_rx_stats.rx_blocks < 4U &&
           !i2s_rx_has_error())
    {
        if (perf_counter_cycles_to_us(
                perf_counter_elapsed(started)) > 1000000U)
        {
            rx_error = 1U;
            break;
        }
    }

    i2s_stop();

    SEGGER_RTT_printf(
        0,
        "RX blocks=%lu lost=%lu error=%u\r\n",
        (unsigned long)audio_rx_stats.rx_blocks,
        (unsigned long)audio_rx_stats.overruns,
        (unsigned)i2s_rx_has_error()
    );

    /*
     * DMA остановлен: общий буфер можно использовать
     * и для просмотра принятых блоков.
     */
    for (uint32_t block = 0U; block < 4U; block++)
    {
        if (!ring_pop(&audio_ring, i2s_dma_buffer))
        {
            break;
        }

        SEGGER_RTT_printf(0, "Block %lu: ",
                          (unsigned long)block);

        for (uint32_t i = 0U; i < 20U; i++)
        {
            SEGGER_RTT_printf(
                0, "%d ", (int)(int16_t)i2s_dma_buffer[i]
            );
        }

        SEGGER_RTT_printf(0, "\r\n");
    }
}


void i2s_test_tx(void)
{
    if (i2s_tx_prepare(1U) != 0)
    {
        SEGGER_RTT_printf(0, "TX prepare error\r\n");
        return;
    }

    /* Короткий тест: stereo, меандр 1 кГц при Fs=48 кГц. */
    for (uint32_t i = 0U; i < DMA_HALF_HALFWORDS; i += 2U)
    {
        uint32_t frame = i / 2U;

        int16_t sample =
            (frame % 48U) < 24U ? 8000 : -8000;

        i2s_dma_buffer[i] = (uint16_t)sample;
        i2s_dma_buffer[i + 1U] = (uint16_t)sample;
    }

    if (!ring_push(&audio_ring, i2s_dma_buffer) ||
        i2s_tx_start_block() != 0 ||
        i2s_start_blocks() != 0)
    {
        i2s_stop();
        SEGGER_RTT_printf(0, "TX start error\r\n");
        return;
    }

    uint32_t started = perf_counter_now();

    while (!i2s_tx_is_done() && !i2s_tx_has_error())
    {
        if (perf_counter_cycles_to_us(
                perf_counter_elapsed(started)) > 1000000U)
        {
            tx_error = 1U;
            break;
        }
    }

    i2s_stop();

    SEGGER_RTT_printf(
        0, "TX error=%u\r\n", (unsigned)i2s_tx_has_error()
    );
}