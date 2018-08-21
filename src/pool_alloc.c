/*
 * Copyright (c) Dmitry Osipenko
 * Copyright (c) Erik Faye-Lund
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// #define DEBUG

#include <string.h>
#include "pool_alloc.h"

#ifdef DEBUG
#include <assert.h>
#include <stdio.h>
static struct {
    unsigned int pools_num;
    unsigned int total_remain;
} stats;
#define PRINTF(fmt, ...) printf(fmt, __VA_ARGS__)
#endif

/*
 * 1) Each allocation is an "entry".
 * 2) The maximum number of entries is limited by the size of bitmap.
 * 3) Bitmap represents the used/unused entries.
 * 4) On allocation, the allocator walks up the bitmap until it finds
 *    an unused entry that has enough space for allocation.
 * 5) The unused entry is selected this way:
 *      1. Find the first unused entry.
 *      2. Find first used entry after the unused of step 1.
 *      3. If unused entry position isn't 0, then the memory-end of used
 *         position that is behind the unused (used_index = unused_index - 1)
 *         is the beginning for the new allocation.
 *         Otherwise the base address of pool is the beginning for the
 *         new allocation.
 *      4. If space between the behind-used entry and the front-used entry
 *         is enough for allocation, allocation succeed.
 * 6) If pool has enough space for allocation, but allocation fails due to
 *    fragmentation, then perform defragmentation and retry the allocation.
 * 7) Defragmentation is performed this way:
 *      1. Move the start of each (used) entry to the end of previous (used)
 *         entry, i.e. squash entries data and entries itself to the beginning
 *         of the pool, hence compress the bitmap.
 *      2. Repeat until unused entry that follows the compressed used-entries
 *         has enough space.
 *                          \   |---compressed entries--|
 *                           --> used-used-used-...-used------unused-----used-..
 *                                                      |--enough space-|
 *
 *      3. Note that each entry has a pointer to the ID of entry that is held
 *         by pool-owner entity (*owner). I.e. ID is the handle for allocation,
 *         that handle is getting updated after entry relocation behind
 *         pool-owners back.
 */

void mem_pool_init(struct mem_pool *pool, void *addr, unsigned long size)
{
    pool->bitmap_full = 0;
    pool->pool_size = size;
    pool->remain = size;
    pool->base = addr;

    memset(pool->bitmap, 0, sizeof(pool->bitmap));

#ifdef DEBUG
    stats.total_remain += size;
    PRINTF("%s: pool %08lx: size=%lu pools_num=%u\n",
           __func__, (unsigned long) pool, size, stats.pools_num++);
#endif
}

static int get_next_unused_entry(struct mem_pool * restrict pool,
                                 unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap;
    unsigned long mask;
    int bit;

    if (bits_array >= BITMAP_SIZE)
        goto out;

    bitmap = pool->bitmap[bits_array];
    mask = (1 << (start % 32)) - 1;
    bitmap |= mask;

    do {
        if (~bitmap) {
            bit = __builtin_ffsl(~bitmap);
#ifdef DEBUG
            PRINTF("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < BITMAP_SIZE)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < BITMAP_SIZE);
out:
#ifdef DEBUG
    PRINTF("%s start=%u ret=-1\n", __func__, start);
#endif
    return -1;
}

static int get_next_used_entry(struct mem_pool * restrict pool,
                               unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap ;
    unsigned long mask;
    int bit;

    if (bits_array >= BITMAP_SIZE)
        goto out;

    bitmap = pool->bitmap[bits_array];
    mask = (1 << (start % 32)) - 1;
    bitmap &= ~mask;

    do {
        if (bitmap) {
            bit = __builtin_ffsl(bitmap);
#ifdef DEBUG
            PRINTF("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < BITMAP_SIZE)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < BITMAP_SIZE);
out:
#ifdef DEBUG
    PRINTF("%s: start=%u ret=-1\n", __func__, start);
#endif
    return -1;
}

static void set_bit(struct mem_pool * restrict pool, unsigned int bit)
{
    unsigned int bits_array = bit / 32;
    unsigned long mask = 1 << (bit % 32);
#ifdef DEBUG
    unsigned long bitmap = pool->bitmap[bits_array];
    assert(!(bitmap & mask));
#endif
    pool->bitmap[bits_array] |= mask;
}

static void clear_bit(struct mem_pool * restrict pool, unsigned int bit)
{
    unsigned int bits_array = bit / 32;
    unsigned long mask = 1 << (bit % 32);
#ifdef DEBUG
    unsigned long bitmap = pool->bitmap[bits_array];
    assert(bitmap & mask);
#endif
    pool->bitmap[bits_array] &= ~mask;
}

static void validate_pool(struct mem_pool * restrict pool)
{
#ifdef DEBUG
    struct __mem_pool_entry *busy;
    struct __mem_pool_entry *prev;
    int b = -1, b_prev = -1;

    do {
        b = get_next_used_entry(pool, b + 1);

        if (b == 0) {
            busy = &pool->entries[b];
            PRINTF("%s: pool %08lx entry[0].base=%08lx pool.base=%08lx\n",
                   __func__,
                   (unsigned long) pool,
                   (unsigned long) busy->base,
                   (unsigned long) pool->base);

            assert(busy->base == pool->base);
            assert(busy->base + busy->size <= pool->base + pool->pool_size);
            assert(busy->owner != NULL);
        } else if (b > 0) {
            busy = &pool->entries[b];
            assert(busy->base > pool->base);

            if (b_prev != -1) {
                prev = &pool->entries[b_prev];
                PRINTF("%s: pool %08lx entry[%d].base=%08lx entry[%d].base=%08lx pool.base=%08lx\n",
                       __func__,
                       (unsigned long) pool,
                       b_prev, (unsigned long) prev->base,
                       b, (unsigned long) busy->base,
                       (unsigned long) pool->base);

                assert(prev->base < busy->base);
                assert(prev->base >= pool->base);
                assert(prev->base + prev->size <= pool->base + pool->pool_size);
                assert(prev->size <= pool->pool_size);
                assert(prev->owner != NULL);
                assert(busy->base >= pool->base);
                assert(busy->base + busy->size <= pool->base + pool->pool_size);
                assert(busy->size <= pool->pool_size);
                assert(busy->owner != NULL);
            }
        }

        b_prev = b;
    } while (b >= 0);
#endif
}

static void move_entry(struct mem_pool * restrict pool_from,
                       struct mem_pool * restrict pool_to,
                       unsigned int from, unsigned int to)
{
#ifdef DEBUG
    PRINTF("%s: from %u to %u\n", __func__, from, to);
#endif
    if (pool_from == pool_to && from == to)
        return;

    clear_bit(pool_from, from);
    set_bit(pool_to, to);

    memcpy(&pool_to->entries[to], &pool_from->entries[from],
           sizeof(struct __mem_pool_entry));

    pool_to->entries[to].owner->pool = pool_to;
    pool_to->entries[to].owner->id = to;

#ifdef DEBUG
    pool_from->entries[from].owner = NULL;
#endif
}

static void migrate_entry(struct mem_pool * restrict pool_from,
                          struct mem_pool * restrict pool_to,
                          unsigned int from, unsigned int to,
                          void *new_base)
{
#ifdef DEBUG
    char *base = pool_from->entries[from].base;
    PRINTF("%s: from %u (%08lx) to %u (%08lx)\n",
           __func__,
           from, (unsigned long) pool_from,
           to, (unsigned long) pool_to);
#endif
    move_entry(pool_from, pool_to, from, to);
    if (new_base != pool_to->entries[to].base) {
        memmove(new_base,
                pool_to->entries[to].base,
                pool_to->entries[to].size);
        pool_to->entries[to].base = new_base;
    }
#ifdef DEBUG
    PRINTF("%s: migrated from %08lx to %08lx\n",
           __func__,
           (unsigned long) base,
           (unsigned long) new_base);
#endif
}

static int defrag_pool(struct mem_pool * restrict pool,
                       unsigned long needed_size)
{
    struct __mem_pool_entry *busy;
    struct __mem_pool_entry *prev;
    int b = 0, p = 0, e;
    char *end;

#ifdef DEBUG
    PRINTF("%s+\n", __func__);
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"

    if (!(pool->bitmap[0] & 1)) {
        b = get_next_used_entry(pool, 1);
        migrate_entry(pool, pool, b, 0, pool->base);
    }

    /* compress bitmap / data */
    while (1) {
        b = get_next_used_entry(pool, p + 1);

        if (b == -1)
            break;

        busy = &pool->entries[b];
        prev = &pool->entries[p];
        end = prev->base + prev->size;

        if (busy->base - end >= needed_size) {
            e = get_next_unused_entry(pool, p);

            if (e < b)
                break;
        }

        migrate_entry(pool, pool, b, ++p, prev->base + prev->size);
    }

    validate_pool(pool);

#ifdef DEBUG
    PRINTF("%s-\n", __func__);
#endif

#pragma GCC diagnostic pop
    return p;
}

void *mem_pool_alloc(struct mem_pool * restrict pool, unsigned long size,
                     struct mem_pool_entry *ret_entry)
{
    struct __mem_pool_entry *empty;
    struct __mem_pool_entry *busy;
    char *start = NULL, *end;
    int e, b = -1; // b for "busy/used entry", e for "unused/empty"

#ifdef DEBUG
    int defragged = 0;
    PRINTF("%s+: pool %08lx (full %d): size=%lu pool.remain=%lu\n",
           __func__,
           (unsigned long) pool,
           pool->bitmap_full, size, pool->remain);
#endif

    if (size > pool->remain || pool->bitmap_full)
        return NULL;
retry:
    do {
        e = get_next_unused_entry(pool, b + 1);

        if (e < 0) {
            pool->bitmap_full = 1;
            break;
        }

        if (e == 0) {
            empty = &pool->entries[0];
            start = pool->base;
        } else {
            busy = &pool->entries[e - 1];
            empty = &pool->entries[e];
            start = busy->base + busy->size;
        }

        b = get_next_used_entry(pool, e + 1);

        if (b < 0) {
            end = pool->base + pool->pool_size;
        } else {
            busy = &pool->entries[b];
            end = busy->base;
        }

        if (end - start >= size) {
            empty->owner = ret_entry;
            empty->base = start;
            empty->size = size;
            set_bit(pool, e);
            break;
        }

        start = NULL;
    } while (b > 0);

    if (start) {
        pool->remain -= size;
        ret_entry->pool = pool;
        ret_entry->id = e;
#ifdef DEBUG
        stats.total_remain -= size;
#endif
    } else if (!pool->bitmap_full) {
#ifdef DEBUG
        assert(!defragged);
#endif
        b = defrag_pool(pool, size);
#ifdef DEBUG
        defragged = 1;
#endif
        goto retry;
    }

    validate_pool(pool);

#ifdef DEBUG
    PRINTF("%s: pool %08lx (full %d): e=%d size=%lu ret=%08lx pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
           __func__,
           (unsigned long) pool,
           pool->bitmap_full, e, size,
           (unsigned long) start,
           pool->remain,
           stats.pools_num, stats.total_remain);
#endif

    return start;
}

void mem_pool_free(struct mem_pool_entry *entry)
{
    struct mem_pool *pool = entry->pool;
    unsigned int entry_id = entry->id;

#ifdef DEBUG
    char *base = mem_pool_entry_addr(entry);
    PRINTF("%s: pool %08lx: e=%u size=%lu addr=%08lx\n",
           __func__, (unsigned long) pool,
           entry_id, pool->entries[entry_id].size,
           (unsigned long) base);
#endif
    validate_pool(pool);

    pool->bitmap_full = 0;
    pool->remain += pool->entries[entry_id].size;
    clear_bit(pool, entry_id);
#ifdef DEBUG
    stats.total_remain += pool->entries[entry_id].size;
    PRINTF("%s: pool %08lx: addr=%08lx pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
            __func__,
           (unsigned long) pool,
           (unsigned long) pool->entries[entry_id].base,
           pool->remain, stats.pools_num, stats.total_remain);
    pool->entries[entry_id].owner = NULL;
    pool->entries[entry_id].base = (void *) 0x66600000;
#endif
}

void mem_pool_destroy(struct mem_pool *pool)
{
#ifdef DEBUG
    assert(pool->remain == pool->pool_size);
    PRINTF("%s: pool %08lx: size=%lu pools_num=%u\n",
           __func__, (unsigned long) pool, pool->pool_size, stats.pools_num--);
    stats.total_remain -= pool->pool_size;
#endif
}

static int mem_pool_empty(struct mem_pool *pool)
{
    return pool->remain == pool->pool_size;
}

int mem_pool_transfer_entries(struct mem_pool *pool_to,
                              struct mem_pool *pool_from)
{
    struct __mem_pool_entry *busy_from;
    struct __mem_pool_entry *busy_to;
    int b_from = -1, b_to, e_to;
    int transferred_entries = 0;
    unsigned long size;
    char *new_base;

#ifdef DEBUG
    PRINTF("%s: pool to=%08lx (full=%d remain=%lu) from=%08lx (full=%d remain=%lu)\n",
           __func__,
           (unsigned long) pool_to,
           pool_to->bitmap_full,
           pool_to->remain,
           (unsigned long) pool_from,
           pool_from->bitmap_full,
           pool_from->remain);
#endif

    if (pool_to->bitmap_full || pool_to->remain == 0)
        return 0;

    validate_pool(pool_to);
    validate_pool(pool_from);

    if (mem_pool_empty(pool_to)) {
        b_to = -1;
        new_base = pool_to->base;
    } else {
        b_to = defrag_pool(pool_to, ~0ul);
        busy_to = &pool_to->entries[b_to];
        new_base = busy_to->base + busy_to->size;
    }

    while (1) {
        e_to = get_next_unused_entry(pool_to, b_to + 1);

        if (e_to == -1)
            break;

        b_from = get_next_used_entry(pool_from, b_from + 1);

        if (b_from == -1)
            break;

        busy_from = &pool_from->entries[b_from];
        size = busy_from->size;

        if (size <= pool_to->remain) {
            migrate_entry(pool_from, pool_to, b_from, e_to, new_base);
            pool_from->remain += size;
            pool_to->remain -= size;
            new_base += size;
            transferred_entries++;
        }

        b_to = e_to;
    }

    validate_pool(pool_to);
    validate_pool(pool_from);

#ifdef DEBUG
    PRINTF("%s: pool to=%08lx from=%08lx transferred_entries=%d\n",
           __func__,
           (unsigned long) pool_to,
           (unsigned long) pool_from,
           transferred_entries);
#endif

    return transferred_entries;
}

void mem_pool_defrag(struct mem_pool *pool)
{
    if (!mem_pool_empty(pool))
        defrag_pool(pool, ~0ul);
}
