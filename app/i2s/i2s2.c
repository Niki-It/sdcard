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
// Половинка буффера DMA в словах
#define DMA_HALF_HALFWORDS     (I2S_AUDIO_BLOCK_BYTES / 2U)
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
static volatile i2s_state_t state = I2S_IDLE;

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


typedef struct
{
    volatile uint8_t error;
    volatile uint8_t done;

    // всего блоков заявлено для передачи
    uint32_t total_blocks;
    // всего блоков загружено в ring и dma
    uint32_t loaded_blocks;
    // завершенных блоков - оправленных по DMA
    uint32_t completed_blocks;

    bool half_has_audio[2];

    uint32_t expected_half;
    uint32_t underruns;
} i2s_tx_context_t;

static volatile i2s_tx_context_t tx;

static volatile uint8_t rx_error;
static uint32_t rx_expected_half;

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


// Можно получить либо 0 либо 1 половинку буффера DMA 
static uint16_t* dma_half(uint32_t half)
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


#if AUDIO_LOSS_STATS
/* Сохранена диагностика RX из твоего файла. */
volatile audio_rx_stats_t audio_loss = {0};
// Добавляет в статистику потерь информацию о результате добавления данных в буффер 
static void audio_loss_log_push(ring_push_result_t result)
{
    audio_loss.rx_accepted_blocks++;

    if (result == RING_PUSH_ADDED)
    {
        audio_loss.rx_current_blocks = 0U;
        audio_loss.current_index = UINT32_MAX;
    }

    audio_loss.rx_overruns++;

    if (audio_loss.rx_current_blocks == 0U)
    {
        audio_loss.rx_loss_series_count++;

        uint32_t index = audio_loss.rx_loss_log_count;

        if (index < AUDIO_LOSS_LOG_CAPACITY)
        {
            audio_loss.current_index = index;

            audio_loss.loss_log[index].first_rx_event =
                audio_loss.rx_accepted_blocks;

            audio_loss.loss_log[index].lost_blocks = 0U;
            audio_loss.rx_loss_log_count = index + 1U;
        }
        else
        {
            audio_loss.current_index = UINT32_MAX;
        }
    }
    audio_loss.rx_current_blocks++;

    if (audio_loss.current_index != UINT32_MAX)
    {
        audio_loss.loss_log[
            audio_loss.current_index
        ].lost_blocks = audio_loss.rx_current_blocks;
    }

    if (audio_loss.rx_current_blocks > audio_loss.rx_loss_max_blocks)
    {
        audio_loss.rx_loss_max_blocks = audio_loss.rx_current_blocks;
    }
}
uint32_t i2s_tx_underrun_count(void)
{
    uint32_t key = irq_lock();
    uint32_t result = audio_loss.tx_underruns;
    irq_unlock(key);
    return result;
}
#endif


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

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    LL_SPI_DisableDMAReq_RX(SPI2);
    LL_SPI_DisableDMAReq_TX(I2S2ext);

    LL_I2S_Disable(SPI2);
    LL_I2S_Disable(I2S2ext);

    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_3);
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_4);

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {}

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
        tx.error = 1U;
        return;
    }

    configure_rx_dma();
    configure_tx_dma();

    NVIC_SetPriority(DMA1_Stream3_IRQn, 5);
    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);

    NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    rx_error = 0U;
    tx.error = 0U;
    state = I2S_IDLE;
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

    while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_3) ||
           LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_4))
    {}

    /*
     * Master ещё выдаёт такты.
     * Даём передатчику закончить содержимое DR/shift register.
     */
    if (previous == I2S_TX_RUNNING)
    {
        uint32_t limit = HW_WAIT_LIMIT;

        while (!LL_SPI_IsActiveFlag_TXE(I2S2ext) ||
               LL_SPI_IsActiveFlag_BSY(I2S2ext))
        {
            if (--limit == 0U)
            {
                tx.error = 1U;
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

}


int i2s_rx_init(void)
{
    if (i2s_is_busy())
    {
        return -1;
    }

    if (!ring_clear(&audio_ring))
    {
        return -1;
    }

    #if AUDIO_LOSS_STATS
    audio_loss_reset(&audio_loss);
    #endif

    rx_error = 0U;
    rx_expected_half = 0U;

    configure_rx_dma();

    state = I2S_RX_PREPARED;
    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_3);

    return 0;
}


int i2s_tx_prepare(uint32_t total_blocks)
{
    if (i2s_is_busy() ||
        total_blocks == 0U)
    {
        return -1;
    }

    if (!ring_clear(&audio_ring))
    {
        return -1;
    }


    tx.error = 0U;
    tx.done = 0U;

    #if AUDIO_LOSS_STATS
        audio_loss.tx_underruns = 0U;
    #endif

    tx.total_blocks = total_blocks;
    tx.loaded_blocks = 0U;
    tx.completed_blocks = 0U;

    tx.half_has_audio[0] = false;
    tx.half_has_audio[1] = false;

    tx.expected_half = 0U;

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

    tx.half_has_audio[half] = false;

    if (tx.loaded_blocks < tx.total_blocks)
    {
        if (ring_pop(&audio_ring, destination))
        {
            tx.loaded_blocks++;
            tx.half_has_audio[half] = true;
            return;
        }

        
#if AUDIO_LOSS_STATS
        audio_loss.tx_underruns += 1;
#endif
    }

    memset(destination, 0, I2S_AUDIO_BLOCK_BYTES);
}


int i2s_tx_init(void)
{
    if (state != I2S_TX_PREPARED)
    {
        return -1;
    }

    uint32_t needed = 2U;

    if (ring_count(&audio_ring) < needed)
    {
        return -1;
    }

    // Заполнить DMA буффер полностью до начала передачи
    tx_fill_half(0U);
    tx_fill_half(1U);

    configure_tx_dma();

    state = I2S_TX_ARMED;
    __DMB();

    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_4);
    return 0;
}


int i2s_start(void)
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
        if (tx.error || --limit == 0U)
        {
            tx.error = 1U;
            i2s_stop();
            return -1;
        }
    }

    // Барьер
    __DSB();

    /* SPI2 в MASTER_RX генерирует CK/WS/MCLK для I2S2ext. */
    LL_I2S_Enable(SPI2);

    return 0;
}


/*
 * Проверяем, что DMA сейчас работает с другой половиной.
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

    ring_push_result_t result =
        ring_push_latest(&audio_ring, (uint8_t *)dma_half(half));

#if AUDIO_LOSS_STATS
    audio_loss_log_push(result);
#endif

    if (result == RING_PUSH_ERROR ||
        !half_available(LL_DMA_STREAM_3, half))
    {
        rx_fail();
        return;
    }

    // переключение половины с 0 на 1 и с 1 на 0
    rx_expected_half ^= 1U;
}


static void tx_fail(void)
{
    tx.error = 1U;
    LL_SPI_DisableDMAReq_TX(I2S2ext);
    tx_clear_flags();
}

void DMA1_Stream4_IRQHandler(void)
{
    if (state != I2S_TX_RUNNING || tx.error)
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

    // определяем половину которая завершилась
    uint32_t half = ht ? 0U : 1U;

    if (half != tx.expected_half ||
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


    // Добовляем к завершенным блокам только если это не пусты блоки (заполнители после основного аудио)
    if (tx.half_has_audio[half])
    {
        tx.completed_blocks++;
    }

    tx_fill_half(half);

    if (!half_available(LL_DMA_STREAM_4, half))
    {
        tx_fail();
        return;
    }

    if (tx.completed_blocks == tx.total_blocks)
    {
        tx.done = true;
    }

    // переключение половины с 0 на 1 и с 1 на 0
    tx.expected_half ^= 1U;

    /*
     * После tx_done DMA продолжает передавать нули,
     * пока main не вызовет i2s_stop().
     */
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
    return tx.error != 0U;
}


bool i2s_tx_is_done(void)
{
    return tx.done != 0U;
}