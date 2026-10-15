#include "ring.h"

#include <string.h>
#include "stm32f4xx.h"


static uint32_t ring_irq_lock(void)
{
    uint32_t key = __get_PRIMASK();

    __disable_irq();
    __DMB();

    return key;
}


static void ring_irq_unlock(uint32_t key)
{
    __DMB();
    __set_PRIMASK(key);
}


/*
 * Вызывается только под блокировкой.
 * Проверяет также согласованность индексов и счётчиков.
 */
static bool ring_valid_locked(const ring_t *ring)
{
    if (ring == NULL ||
        ring->storage == NULL ||
        ring->block_size == 0U ||
        ring->block_count == 0U)
    {
        return false;
    }

    if ((size_t)ring->block_size >
        SIZE_MAX / (size_t)ring->block_count)
    {
        return false;
    }

    if (ring->read_index >= ring->block_count ||
        ring->write_index >= ring->block_count ||
        ring->ready_count > ring->block_count)
    {
        return false;
    }

    if (ring->reserved_count >
        ring->block_count - ring->ready_count)
    {
        return false;
    }

    /* Резерв должен быть физически непрерывным. */
    if (ring->reserved_count >
        ring->block_count - ring->write_index)
    {
        return false;
    }

    /*
     * write_index должен находиться сразу после
     * ready_count блоков, начиная с read_index.
     *
     * Вычисление без переполнения сложения.
     */
    uint32_t to_end =
        ring->block_count - ring->read_index;

    uint32_t expected_write =
        (ring->ready_count >= to_end)
            ? ring->ready_count - to_end
            : ring->read_index + ring->ready_count;

    return ring->write_index == expected_write;
}


/// @brief Продвигает индекс определенное ко-во блоков и если он заходит за буффер просто продолжает считать с начала
/// @return 
static uint32_t ring_advance(
    const ring_t *ring,
    uint32_t index,
    uint32_t blocks
)
{
    uint32_t to_end = ring->block_count - index;

    return (blocks >= to_end)
               ? blocks - to_end
               : index + blocks;
}

/// @brief Получаем адрес блока внутри кольцевого буффера
/// @return 
static uint8_t *ring_block_address(
    const ring_t *ring,
    uint32_t index
)
{
    return ring->storage +
           (size_t)index * ring->block_size;
}


/*
 * Вызывается под блокировкой после проверки:
 * резерв отсутствует, свободное место есть.
 */
static void ring_push_locked(ring_t *ring, const void *block)
{
    memcpy(
        ring_block_address(ring, ring->write_index),
        block,
        ring->block_size
    );

    ring->write_index =
        ring_advance(ring, ring->write_index, 1U);

    ring->ready_count++;
}


bool ring_init(
    ring_t *ring,
    uint8_t *storage,
    uint32_t block_size,
    uint32_t block_count
)
{
    if (ring == NULL ||
        storage == NULL ||
        block_size == 0U ||
        block_count == 0U ||
        (size_t)block_size > SIZE_MAX / (size_t)block_count)
    {
        return false;
    }

    /*
     * init не предназначен для конкурентного вызова.
     * Все пользователи очереди должны быть остановлены.
     */
    *ring = (ring_t)
    {
        .storage = storage,
        .block_size = block_size,
        .block_count = block_count,
        .read_index = 0U,
        .write_index = 0U,
        .ready_count = 0U,
        .reserved_count = 0U
    };

    return true;
}


bool ring_push(ring_t *ring, const void *block)
{
    uint32_t key = ring_irq_lock();
    bool result = false;

    if (block != NULL &&
        ring_valid_locked(ring) &&
        ring->reserved_count == 0U &&
        ring->ready_count < ring->block_count)
    {
        ring_push_locked(ring, block);
        result = true;
    }

    ring_irq_unlock(key);
    return result;
}


ring_push_result_t ring_push_latest(
    ring_t *ring,
    const void *block
)
{
    uint32_t key = ring_irq_lock();
    ring_push_result_t result = RING_PUSH_ERROR;

    if (block != NULL &&
        ring_valid_locked(ring) &&
        ring->reserved_count == 0U)
    {
        if (ring->ready_count < ring->block_count)
        {
            ring_push_locked(ring, block);
            result = RING_PUSH_ADDED;
        }
        else
        {
            /* Полная очередь: заменяем самый новый блок. */
            uint32_t latest_index =
                (ring->write_index == 0U)
                    ? ring->block_count - 1U
                    : ring->write_index - 1U;

            memcpy(
                ring_block_address(ring, latest_index),
                block,
                ring->block_size
            );

            /* Индексы и счётчики остаются прежними. */
            result = RING_PUSH_REPLACED;
        }
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_pop(ring_t *ring, void *block)
{
    uint32_t key = ring_irq_lock();
    bool result = false;

    if (block != NULL &&
        ring_valid_locked(ring) &&
        ring->ready_count != 0U)
    {
        /*
         * Копирование тоже под блокировкой:
         * push_latest не должен перезаписать этот блок
         * до окончания memcpy.
         */
        memcpy(
            block,
            ring_block_address(ring, ring->read_index),
            ring->block_size
        );

        ring->read_index =
            ring_advance(ring, ring->read_index, 1U);

        ring->ready_count--;
        result = true;
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_is_free(const ring_t *ring)
{
    uint32_t key = ring_irq_lock();

    bool result =
        ring_valid_locked(ring) &&
        ring->reserved_count == 0U &&
        ring->ready_count < ring->block_count;

    ring_irq_unlock(key);
    return result;
}


bool ring_is_empty(const ring_t *ring)
{
    uint32_t key = ring_irq_lock();

    bool result =
        !ring_valid_locked(ring) ||
        ring->ready_count == 0U;

    ring_irq_unlock(key);
    return result;
}


size_t ring_count(const ring_t *ring)
{
    uint32_t key = ring_irq_lock();

    size_t result = ring_valid_locked(ring)
                        ? (size_t)ring->ready_count
                        : 0U;

    ring_irq_unlock(key);
    return result;
}


size_t ring_free_count(const ring_t *ring)
{
    uint32_t key = ring_irq_lock();
    size_t result = 0U;

    if (ring_valid_locked(ring))
    {
        result = (size_t)(
            ring->block_count -
            ring->ready_count -
            ring->reserved_count
        );
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_reserve(
    ring_t *ring,
    uint32_t blocks,
    uint8_t **data
)
{
    if (data == NULL)
    {
        return false;
    }

    *data = NULL;

    uint32_t key = ring_irq_lock();
    bool result = false;

    if (ring_valid_locked(ring) &&
        blocks != 0U &&
        ring->reserved_count == 0U &&
        blocks <= ring->block_count - ring->ready_count &&
        blocks <= ring->block_count - ring->write_index)
    {
        ring->reserved_count = blocks;

        *data = ring_block_address(
            ring,
            ring->write_index
        );

        result = true;
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_commit(ring_t *ring, uint32_t ready_blocks)
{
    uint32_t key = ring_irq_lock();
    bool result = false;

    if (ring_valid_locked(ring) &&
        ring->reserved_count != 0U &&
        ready_blocks <= ring->reserved_count)
    {
        /*
         * Данные резерва должны быть записаны
         * до публикации счётчика.
         */
        __DMB();

        ring->write_index = ring_advance(
            ring,
            ring->write_index,
            ready_blocks
        );

        ring->ready_count += ready_blocks;

        /* Включая неиспользованный остаток резерва. */
        ring->reserved_count = 0U;

        result = true;
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_cancel_reserve(ring_t *ring)
{
    uint32_t key = ring_irq_lock();
    bool result = false;

    if (ring_valid_locked(ring) &&
        ring->reserved_count != 0U)
    {
        ring->reserved_count = 0U;
        result = true;
    }

    ring_irq_unlock(key);
    return result;
}


bool ring_clear(ring_t *ring)
{
    uint32_t key = ring_irq_lock();
    bool result = false;

    if (ring_valid_locked(ring) &&
        ring->reserved_count == 0U)
    {
        ring->read_index = 0U;
        ring->write_index = 0U;
        ring->ready_count = 0U;
        ring->reserved_count = 0U;

        result = true;
    }

    ring_irq_unlock(key);
    return result;
}