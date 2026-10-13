#include "ring.h"
#include <string.h>

#include "ring.h"

#include <string.h>

bool ring_init(ring_t *ring, uint8_t *storage, uint32_t block_size, uint32_t block_count)
{
    if (ring == NULL || storage == NULL ||
        block_size == 0 || block_count == 0 ||
        (size_t)block_size > SIZE_MAX / (size_t)block_count) {
        return false;
    }

    *ring = (ring_t){
        .storage = storage,
        .block_size = block_size,
        .block_count = block_count,
    };

    return true;
}

bool ring_push(ring_t *ring, uint8_t *block)
{
    if (ring == NULL || block == NULL || !ring_is_free(ring)) {
        return false;
    }

    memcpy(ring->storage +
           (size_t)ring->write_index * ring->block_size,
           block, ring->block_size);

    ring->write_index++;

    if (ring->write_index == ring->block_count) {
        ring->write_index = 0;
    }

    ring->used_count++;
    return true;
}
ring_push_result_t ring_push_latest(ring_t *ring, uint8_t *block)
{
    if (ring == NULL ||
        block == NULL ||
        ring->storage == NULL ||
        ring->block_size == 0U ||
        ring->block_count == 0U ||
        ring->used_count > ring->block_count ||
        ring->write_index >= ring->block_count ||
        ring->read_index >= ring->block_count)
    {
        return RING_PUSH_ERROR;
    }

    if (ring->used_count < ring->block_count)
    {
        return ring_push(ring, block)
                   ? RING_PUSH_ADDED
                   : RING_PUSH_ERROR;
    }

    // Очередь полна.
    // write_index указывает на следующее место добавления.
    // Самый новый блок находится непосредственно перед ним.
    uint32_t last_index =
        (ring->write_index == 0U)
            ? ring->block_count - 1U
            : ring->write_index - 1U;

    memcpy(ring->storage +
               (size_t)last_index * ring->block_size,
           block,
           ring->block_size);

    // Количество блоков и индексы остаются прежними.
    return RING_PUSH_REPLACED;
}

bool ring_pop(ring_t *ring, uint8_t *block)
{
    if (ring == NULL || block == NULL || ring_is_empty(ring)) {
        return false;
    }

    memcpy(block,
           ring->storage +
           (size_t)ring->read_index * ring->block_size,
           ring->block_size);

    ring->read_index++;

    if (ring->read_index == ring->block_count) {
        ring->read_index = 0;
    }

    ring->used_count--;
    return true;
}

bool ring_is_free(ring_t *ring)
{
    return ring != NULL &&
           ring->storage != NULL &&
           ring->block_count != 0 &&
           ring->block_size != 0 &&
           ring->used_count < ring->block_count;
}

bool ring_is_empty(ring_t *ring)
{
    return ring == NULL ||
           ring->storage == NULL ||
           ring->used_count == 0;
}

size_t ring_count(ring_t *ring)
{
    return ring == NULL || ring->storage == NULL
           ? 0 : ring->used_count;
}

void ring_clear(ring_t *ring)
{
    if (ring == NULL) {
        return;
    }

    ring->read_index = 0;
    ring->write_index = 0;
    ring->used_count = 0;
}