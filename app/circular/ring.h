#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *storage;
    uint32_t block_size;
    uint32_t block_count; // число занятых блоков

    uint32_t read_index; 
    uint32_t write_index;

    uint32_t ready_count; // число уже записанных блоков 
    uint32_t reserved_count; // число зарезервированных блоков
} ring_t;

typedef enum
{
    RING_PUSH_ERROR = 0,
    RING_PUSH_ADDED,
    RING_PUSH_REPLACED
} ring_push_result_t;


bool ring_init(
    ring_t *ring,
    uint8_t *storage,
    uint32_t block_size,
    uint32_t block_count
);


bool ring_push(ring_t *ring, const void *block);


ring_push_result_t ring_push_latest(
    ring_t *ring,
    const void *block
);


bool ring_pop(ring_t *ring, void *block);

/* Можно ли сейчас добавить блок через ring_push(). */
bool ring_is_free(const ring_t *ring);

/* Нет готовых блоков. Для некорректной очереди — true. */
bool ring_is_empty(const ring_t *ring);

/* Количество опубликованных блоков. */
size_t ring_count(const ring_t *ring);

/* Количество блоков, не занятых данными или резервом. */
size_t ring_free_count(const ring_t *ring);

/*
 * Резервирует ровно blocks физически непрерывных блоков.
 * Только один активный резерв.
*/
bool ring_reserve(
    ring_t *ring,
    uint32_t blocks,
    uint8_t **data
);

/*
 * Публикует первые ready_blocks активного резерва.
 * Остаток резерва освобождается.
 * ready_blocks должен быть <= reserved_count.
 * commit(0) эквивалентен отмене резерва.
 */
bool ring_commit(ring_t *ring, uint32_t ready_blocks);


bool ring_cancel_reserve(ring_t *ring);


bool ring_clear(ring_t *ring);