#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *storage;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t used_count;
} ring_t;

bool ring_init(ring_t *ring, uint8_t *storage,
               uint32_t block_size, uint32_t block_count);

bool ring_push(ring_t *ring, uint8_t *block);
bool ring_pop(ring_t *ring, uint8_t *block);

bool ring_is_free(ring_t *ring);
bool ring_is_empty(ring_t *ring);

size_t ring_count(ring_t *ring);
void ring_clear(ring_t *ring);

