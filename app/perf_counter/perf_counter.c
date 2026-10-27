#include "perf_counter.h"

#define PERF_LOG_CAPACITY  256U
static perf_sample_t perf_samples[PERF_LOG_CAPACITY];
static perf_log_policy_t perf_policy = PERF_LOG_FIRST;

static uint32_t perf_count = 0U;
static uint32_t perf_next = 0U;
static uint32_t perf_sequence = 0U;
static uint32_t perf_started_at = 0U;
static bool perf_running = false;

static bool perf_log_is_better(uint32_t value, uint32_t reference)
{
    if (perf_policy == PERF_LOG_MAX)
    {
        return value > reference;
    }
    return value < reference;
}

static void perf_log_insert_sorted(perf_sample_t sample)
{
    /*
     * Если массив заполнен и новое значение не лучше
     * последнего, сохранять его не нужно.
     */
    if (perf_count == PERF_LOG_CAPACITY)
    {
        if (!perf_log_is_better(
                sample.duration_us,
                perf_samples[perf_count - 1U].duration_us))
        {
            return;
        }
    }

    uint32_t position;

    if (perf_count < PERF_LOG_CAPACITY)
    {
        position = perf_count;
        perf_count++;
    }
    else
    {
        position = perf_count - 1U;
    }

    /*
     * Освобождаем место для нового значения.
     * При заполненном массиве последнее значение вытесняется.
     */
    while (position > 0U)
    {
        if (!perf_log_is_better(
                sample.duration_us,
                perf_samples[position - 1U].duration_us))
        {
            break;
        }

        perf_samples[position] = perf_samples[position - 1U];
        position--;
    }

    perf_samples[position] = sample;
}

void perf_log_init(perf_log_policy_t policy)
{
    switch (policy)
    {
        case PERF_LOG_FIRST:
        case PERF_LOG_LAST:
        case PERF_LOG_MAX:
        case PERF_LOG_MIN:
            perf_policy = policy;
            break;

        default:
            perf_policy = PERF_LOG_FIRST;
            break;
    }

    memset(perf_samples, 0, sizeof(perf_samples));

    perf_count = 0U;
    perf_next = 0U;
    perf_sequence = 0U;
    perf_started_at = 0U;
    perf_running = false;
}

void perf_log_start(void)
{
    if (perf_running)
    {
        return;
    }

    perf_running = true;
    perf_started_at = perf_counter_now();
}

void perf_log_finish(void)
{
    if (!perf_running)
    {
        return;
    }

    uint32_t elapsed = perf_counter_elapsed(perf_started_at);

    perf_running = false;
    perf_sequence++;

    perf_sample_t sample =
    {
        .sequence = perf_sequence,
        .duration_us = perf_counter_cycles_to_us(elapsed)
    };

    switch (perf_policy)
    {
        case PERF_LOG_FIRST:
            if (perf_count < PERF_LOG_CAPACITY)
            {
                perf_samples[perf_count] = sample;
                perf_count++;
            }
            break;

        case PERF_LOG_LAST:
            perf_samples[perf_next] = sample;

            perf_next++;

            if (perf_next == PERF_LOG_CAPACITY)
            {
                perf_next = 0U;
            }

            if (perf_count < PERF_LOG_CAPACITY)
            {
                perf_count++;
            }
            break;

        case PERF_LOG_MAX:
        case PERF_LOG_MIN:
            perf_log_insert_sorted(sample);
            break;

        default:
            break;
    }
}

void perf_log_print(void)
{
    printf(
        "PERF: measured=%u stored=%u policy=%u\r\n",
        (unsigned)perf_sequence,
        (unsigned)perf_count,
        (unsigned)perf_policy
    );

    uint32_t index = 0U;

    /*
     * Полный кольцевой буфер выводим начиная
     * с самой старой сохранённой записи.
     */
    if (perf_policy == PERF_LOG_LAST &&
        perf_count == PERF_LOG_CAPACITY)
    {
        index = perf_next;
    }

    for (uint32_t i = 0U; i < perf_count; i++)
    {
        const perf_sample_t *sample = &perf_samples[index];

        printf(
            "#%u: %u cycles\r\n",
            (unsigned)sample->sequence,
            (unsigned)sample->duration_us
        );

        index++;

        if (index == PERF_LOG_CAPACITY)
        {
            index = 0U;
        }
    }
}

void perf_counter_init(void)
{
    // Обновляем значение частоты ядра после настройки clock.
    SystemCoreClockUpdate();

    // Разрешаем DWT.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Сбрасываем счётчик один раз при инициализации.
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __DSB();
    __ISB();
}

uint32_t perf_counter_now(void)
{
    return DWT->CYCCNT;
}

uint32_t perf_counter_elapsed(uint32_t start)
{
    return (uint32_t)(DWT->CYCCNT - start);
}

uint32_t perf_counter_cycles_to_us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / SystemCoreClock);
}