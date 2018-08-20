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
    printf("%s: pool %08lx: size=%lu pools_num=%u\n",
           __func__, (unsigned long) pool, size, stats.pools_num++);
#endif
}

static int get_next_unused_entry(struct mem_pool * restrict pool,
                                 unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap = pool->bitmap[bits_array];
    unsigned long mask = (1 << (start % 32)) - 1;
    int bit;

    bitmap |= mask;

    do {
        if (~bitmap) {
            bit = __builtin_ffsl(~bitmap);
#ifdef DEBUG
            printf("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < BITMAP_SIZE)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < BITMAP_SIZE);
#ifdef DEBUG
    printf("%s start=%u ret=-1\n", __func__, start);
#endif
    return -1;
}

static int get_next_used_entry(struct mem_pool * restrict pool,
                               unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap = pool->bitmap[bits_array];
    unsigned long mask = (1 << (start % 32)) - 1;
    int bit;

    bitmap &= ~mask;

    do {
        if (bitmap) {
            bit = __builtin_ffsl(bitmap);
#ifdef DEBUG
            printf("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < BITMAP_SIZE)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < BITMAP_SIZE);
#ifdef DEBUG
    printf("%s: start=%u ret=-1\n", __func__, start);
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

static void move_entry(struct mem_pool * restrict pool,
                       unsigned int from, unsigned int to)
{
#ifdef DEBUG
    printf("%s: from %u to %u\n", __func__, from, to);
#endif
    clear_bit(pool, from);
    set_bit(pool, to);

    memcpy(&pool->entries[to], &pool->entries[from],
           sizeof(struct __mem_pool_entry));

    pool->entries[to].owner->id = to;
}

static void migrate_entry(struct mem_pool * restrict pool,
                          unsigned int from, unsigned int to,
                          void *new_base)
{
#ifdef DEBUG
    char *base = pool->entries[from].base;
    printf("%s: from %u to %u\n", __func__, from, to);
#endif
    move_entry(pool, from, to);
    memmove(new_base, pool->entries[to].base, pool->entries[to].size);
    pool->entries[to].base = new_base;
#ifdef DEBUG
    printf("%s: migrated from %08lx to %08lx\n",
           __func__,
           (unsigned long) base,
           (unsigned long) new_base);
#endif
}

static int defrag_pool(struct mem_pool * restrict pool,
                       unsigned long needed_size)
{
    struct __mem_pool_entry *busy;
    int b = 0, b_next = 0; // b for "busy/used entry"
    char *new_base, *end;

#ifdef DEBUG
    printf("%s+\n", __func__);
#endif

    if (!(pool->bitmap[0] & 1)) {
        b = get_next_used_entry(pool, 1);
        migrate_entry(pool, b, 0, pool->base);
    }

    b = get_next_used_entry(pool, 0);
    b_next = get_next_used_entry(pool, b + 1);

    while (1) {
        if (b_next != -1) {
            busy = &pool->entries[b];
            new_base = busy->base + busy->size;
            end = pool->entries[b_next].base;
            b = b + 1;

            if (end != new_base) {
                migrate_entry(pool, b_next, b, new_base);
            } else if (b != b_next) {
                move_entry(pool, b_next, b);
            }

            b_next = get_next_used_entry(pool, b + 1);
        }

        if (b_next == b + 1)
            continue;

        if (b_next == -1)
            end = pool->base + pool->pool_size;
        else
            end = pool->entries[b_next].base;

        busy = &pool->entries[b];
        new_base = busy->base + busy->size;

        if (end - new_base >= needed_size)
            break;
#ifdef DEBUG
        assert(b_next != -1);
#endif
    }

#ifdef DEBUG
    printf("%s-\n", __func__);
#endif

    return b;
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
    printf("%s+: pool %08lx (full %d): size=%lu pool.remain=%lu\n",
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

#ifdef DEBUG
    printf("%s: pool %08lx (full %d): e=%d size=%lu ret=%08lx pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
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
    printf("%s: pool %08lx: e=%u size=%lu\n",
           __func__, (unsigned long) pool,
           entry_id, pool->entries[entry_id].size);
#endif
    pool->bitmap_full = 0;
    pool->remain += pool->entries[entry_id].size;
    pool->entries[entry_id].owner = NULL;
    clear_bit(pool, entry_id);
#ifdef DEBUG
    stats.total_remain += pool->entries[entry_id].size;
    printf("%s: pool %08lx: addr=%08lx pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
            __func__,
           (unsigned long) pool,
           (unsigned long) pool->entries[entry_id].base,
           pool->remain, stats.pools_num, stats.total_remain);
#endif
}

void mem_pool_destroy(struct mem_pool *pool)
{
#ifdef DEBUG
    assert(pool->remain == pool->pool_size);
    printf("%s: pool %08lx: size=%lu pools_num=%u\n",
           __func__, (unsigned long) pool, pool->pool_size, stats.pools_num--);
    stats.total_remain -= pool->pool_size;
#endif
}
