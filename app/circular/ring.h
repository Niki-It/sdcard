#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *storage;
    uint32_t block_size;
    uint32_t block_count;

    uint32_t read_index;
    uint32_t write_index;

    uint32_t ready_count;
    uint32_t reserved_count;
} ring_t;

typedef enum
{
    RING_PUSH_ERROR = 0,
    RING_PUSH_ADDED,
    RING_PUSH_REPLACED
} ring_push_result_t;

/*
 * Инициализация до начала совместного использования.
 *
 * storage должен содержать минимум:
 * block_size * block_count байт.
 *
 * Повторная инициализация разрешена только после остановки
 * всех пользователей очереди и завершения записи в резерв.
 */
bool ring_init(
    ring_t *ring,
    uint8_t *storage,
    uint32_t block_size,
    uint32_t block_count
);

/*
 * Копирует один блок в очередь.
 *
 * false: неверные аргументы, очередь полна
 * или существует активный резерв.
 */
bool ring_push(ring_t *ring, const void *block);

/*
 * При наличии места добавляет один блок.
 * В полной очереди заменяет самый новый готовый блок.
 *
 * Активный резерв запрещает эту операцию.
 */
ring_push_result_t ring_push_latest(
    ring_t *ring,
    const void *block
);

/*
 * Копирует и извлекает один готовый блок.
 * Активный резерв не мешает извлечению.
 *
 * false: неверные аргументы или нет готовых блоков.
 */
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
 *
 * Только один активный резерв.
 * Резерв не пересекает конец storage.
 * write_index и ready_count не изменяются.
 *
 * При успехе *data получает указатель на резерв.
 * При отказе *data = NULL, если data != NULL.
 *
 * Заполнение резерва выполняется вне блокировки.
 * Только владелец резерва вызывает commit/cancel.
 */
bool ring_reserve(
    ring_t *ring,
    uint32_t blocks,
    uint8_t **data
);

/*
 * Публикует первые ready_blocks активного резерва.
 * Остаток резерва освобождается.
 *
 * ready_blocks должен быть <= reserved_count.
 * commit(0) эквивалентен отмене резерва.
 *
 * До вызова необходимо завершить запись в резерв,
 * включая DMA, если он использовался для заполнения.
 * После успеха указатель резерва больше не принадлежит
 * вызывающему коду.
 */
bool ring_commit(ring_t *ring, uint32_t ready_blocks);

/*
 * Отменяет резерв без публикации.
 * false: некорректная очередь или нет резерва.
 *
 * Перед вызовом необходимо прекратить запись в резерв.
 */
bool ring_cancel_reserve(ring_t *ring);

/*
 * Сбрасывает индексы и счётчики без очистки памяти.
 * При активном резерве возвращает false.
 */
bool ring_clear(ring_t *ring);

/*
 * Правила использования:
 *
 * 1. Не изменять поля структуры напрямую после init().
 * 2. Резервом управляет один владелец.
 * 3. Буферы push/pop не должны пересекаться с storage
 *    или самой структурой ring_t.
 * 4. Все операции, кроме init(), защищены от обычных
 *    маскируемых IRQ на одном ядре Cortex-M.
 * 5. Не вызывать API из NMI/HardFault.
 * 6. Каждый запрос count/free возвращает отдельный снимок.
 */