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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool_alloc.h"

#ifdef POOL_DEBUG
static struct {
    unsigned int pools_num;
    unsigned int total_remain;
} stats;
#ifdef POOL_DEBUG_VERBOSE
	#define PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
	#define PRINTF(fmt, ...)
#endif
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

int mem_pool_init(struct mem_pool *pool, unsigned long size,
                  unsigned int bitmap_size,
                  mem_pool_memcpy memcpy,
                  mem_pool_memmove memmove)
{
    assert(pool);
    assert(size);
    assert(memcpy);
    assert(memmove);
    assert(bitmap_size);

    pool->bitmap_size = bitmap_size;
    pool->fragmented = 0;
    pool->bitmap_full = 0;
    pool->base_offset = 0;
    pool->access_refcount = 0;
    pool->pool_size = size;
    pool->remain = size;
    pool->vbase = NULL;
    pool->base = NULL;
    pool->memcpy = memcpy;
    pool->memmove = memmove;

    /*
     * TODO: Rework address handling, for now the base must be non-NULL,
     *       otherwise allocation won't happen since NULL means failed.
     */
    if (!pool->base) {
        pool->base_offset = 0x10000000;
        pool->base += 0x10000000;
    }

    pool->bitmap = calloc(bitmap_size, sizeof(*pool->bitmap));
    pool->entries = malloc(bitmap_size * 32 * sizeof(*pool->entries));

    if (!pool->bitmap || !pool->entries) {
        free(pool->entries);
        free(pool->bitmap);
        return -ENOMEM;
    }

#ifdef POOL_DEBUG
    memset(pool->entries, 0, bitmap_size * 32 * sizeof(*pool->entries));
    stats.total_remain += size;
    PRINTF("%s: pool %p: size=%lu pools_num=%u\n",
           __func__, pool, size, stats.pools_num++);
#endif

    return 0;
}

static int get_next_unused_entry(struct mem_pool * restrict pool,
                                 unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap;
    unsigned long mask;
    int bit;

    if (bits_array >= pool->bitmap_size)
        goto out;

    bitmap = pool->bitmap[bits_array];
    mask = (1 << (start % 32)) - 1;
    bitmap |= mask;

    do {
        if (~bitmap) {
            bit = __builtin_ffsl(~bitmap);
#ifdef POOL_DEBUG
            PRINTF("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < pool->bitmap_size)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < pool->bitmap_size);
out:
#ifdef POOL_DEBUG
    PRINTF("%s start=%u ret=-1\n", __func__, start);
#endif
    return -1;
}

int mem_pool_get_next_used_entry(struct mem_pool * restrict pool,
                                 unsigned int start)
{
    unsigned int bits_array = start / 32;
    unsigned long bitmap ;
    unsigned long mask;
    int bit;

    if (bits_array >= pool->bitmap_size)
        goto out;

    bitmap = pool->bitmap[bits_array];
    mask = (1 << (start % 32)) - 1;
    bitmap &= ~mask;

    do {
        if (bitmap) {
            bit = __builtin_ffsl(bitmap);
#ifdef POOL_DEBUG
            PRINTF("%s start=%u ret=%u\n",
                   __func__, start, bits_array * 32 + bit - 1);
#endif
            return bits_array * 32 + bit - 1;
        }

        if (++bits_array < pool->bitmap_size)
            bitmap = pool->bitmap[bits_array];
    } while (bits_array < pool->bitmap_size);
out:
#ifdef POOL_DEBUG
    PRINTF("%s: start=%u ret=-1\n", __func__, start);
#endif
    return -1;
}

static void set_bit(struct mem_pool * restrict pool, unsigned int bit)
{
    unsigned int bits_array = bit / 32;
    unsigned long mask = 1 << (bit % 32);
#ifdef POOL_DEBUG
    unsigned long bitmap = pool->bitmap[bits_array];
    assert(!(bitmap & mask));
#endif
    pool->bitmap[bits_array] |= mask;
}

static void clear_bit(struct mem_pool * restrict pool, unsigned int bit)
{
    unsigned int bits_array = bit / 32;
    unsigned long mask = 1 << (bit % 32);
#ifdef POOL_DEBUG
    unsigned long bitmap = pool->bitmap[bits_array];
    assert(bitmap & mask);
#endif
    pool->bitmap[bits_array] &= ~mask;
}

static void mem_pool_set_canary(struct __mem_pool_entry *entry)
{
#ifdef POOL_DEBUG_CANARY
    int i;

    for (i = 0; i < 256; i++)
        entry->base[entry->size - 256 + i] = i;
#endif
}

inline void mem_pool_check_canary(struct __mem_pool_entry *entry)
{
#ifdef POOL_DEBUG_CANARY
    int i;

    for (i = 0; i < 256; i++)
        assert(entry->base[entry->size - 256 + i] == i);
#endif
}

static void mem_pool_clear_canary(struct __mem_pool_entry *entry)
{
#ifdef POOL_DEBUG_CANARY
    int i;

    for (i = 0; i < 256; i++)
        entry->base[entry->size - 256 + i] = 0x55;
#endif
}

static void validate_pool(struct mem_pool * restrict pool)
{
#ifdef POOL_DEBUG
    struct __mem_pool_entry *busy;
    struct __mem_pool_entry *prev;
    int b = -1, b_prev = -1;

    do {
        b = mem_pool_get_next_used_entry(pool, b + 1);

        if (b == 0) {
            busy = &pool->entries[b];
            PRINTF("%s: pool %p entry[0].base=%p pool.base=%p\n",
                   __func__, pool, busy->base, pool->base);

            assert(busy->base == pool->base);
            assert(busy->base + busy->size <= pool->base + pool->pool_size);
            assert(busy->owner != NULL);
        } else if (b > 0) {
            busy = &pool->entries[b];
            assert(busy->base > pool->base);

            if (b_prev != -1) {
                prev = &pool->entries[b_prev];
                PRINTF("%s: pool %p entry[%d].base=%p entry[%d].base=%p pool.base=%p\n",
                       __func__, pool, b_prev, prev->base, b, busy->base, pool->base);

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

        if (b > -1)
            mem_pool_check_canary(&pool->entries[b]);

        b_prev = b;
    } while (b >= 0);
#endif
}

static void move_entry(struct mem_pool *pool_from,
                       struct mem_pool *pool_to,
                       unsigned int from, unsigned int to)
{
#ifdef POOL_DEBUG
    PRINTF("%s: from %u to %u\n", __func__, from, to);
#endif
    if (pool_from == pool_to && from == to)
        return;

    clear_bit(pool_from, from);
    set_bit(pool_to, to);

    pool_to->entries[to] = pool_from->entries[from];
    pool_to->entries[to].owner->pool = pool_to;
    pool_to->entries[to].owner->id = to;

#ifdef POOL_DEBUG
    pool_from->entries[from].owner = NULL;
    pool_from->entries[from].base = (void *) 0x66600000;
    pool_from->entries[from].size = 0x10000000;
#endif
}

static void migrate_entry(struct mem_pool *pool_from,
                          struct mem_pool *pool_to,
                          unsigned int from, unsigned int to,
                          void *new_base)
{
    char *from_vbase = pool_from->vbase + (unsigned long)pool_from->entries[from].base;
    char *new_vbase  = pool_to->vbase   + (unsigned long)new_base;

#ifdef POOL_DEBUG_VERBOSE
    char *base = pool_from->entries[from].base;
    PRINTF("%s: from %u (%p) to %u (%p)\n",
           __func__, from, pool_from, to, pool_to);
#endif

    from_vbase -= pool_from->base_offset;
    new_vbase  -= pool_to->base_offset;

    assert(pool_from->access_refcount > 0);
    assert(pool_to->access_refcount > 0);

    /* pool_from data is copied into pool_to by move_entry()! */
    move_entry(pool_from, pool_to, from, to);
    if (new_vbase != from_vbase) {
        int mem_move = 1;

        if (new_vbase >= from_vbase + pool_to->entries[to].size ||
            from_vbase >= new_vbase + pool_to->entries[to].size)
                mem_move = 0;

        assert(pool_from->memmove == pool_to->memmove);
        assert(pool_from->memcpy == pool_to->memcpy);

        if (mem_move)
            pool_to->memmove(new_vbase, from_vbase, pool_to->entries[to].size);
        else
            pool_to->memcpy(new_vbase, from_vbase, pool_to->entries[to].size);

        mem_pool_clear_canary(&pool_to->entries[to]);
        pool_to->entries[to].base = new_base;
    }
#ifdef POOL_DEBUG_VERBOSE
    PRINTF("%s: migrated from %p to %p\n",
           __func__, base, new_base);
#endif
}

static int mem_pool_resize_bitmap(struct mem_pool * restrict pool,
                                  unsigned long new_size)
{
    struct __mem_pool_entry *new_entries;
    unsigned long *new_bitmap;
    unsigned long old_size;
    int shrink;
    int i;

    old_size = pool->bitmap_size;

#ifdef POOL_DEBUG
    PRINTF("%s: pool %p (fragmented %d) bitmap_size %lu new_size %lu\n",
           __func__, pool, pool->fragmented, old_size, new_size);
#endif

    shrink = (new_size < old_size);

    if (pool->fragmented && shrink)
        return 0;

    if (old_size == new_size)
        return 0;

#ifdef POOL_DEBUG
    if (shrink) {
        int e = get_next_unused_entry(pool, 0);
        assert(new_size >= (e / 32 + 1));
        assert(new_size > 0);
    }
#endif

    new_bitmap = realloc(pool->bitmap, new_size * sizeof(*new_bitmap));
    new_entries = realloc(pool->entries, new_size * 32 * sizeof(*new_entries));

    if (new_bitmap && new_entries) {
        pool->entries = new_entries;
        pool->bitmap_size = new_size;
        pool->bitmap = new_bitmap;

        if (!shrink) {
            for (i = old_size; i < new_size; i++)
                pool->bitmap[i] = 0;
        }

        return 1;
    }

    if (new_entries)
        pool->entries = new_entries;

    if (new_bitmap)
        pool->bitmap = new_bitmap;

    return 0;
}

static int defrag_pool(struct mem_pool * restrict pool,
                       unsigned long needed_size,
                       int ret_last_busy)
{
    struct __mem_pool_entry *busy;
    struct __mem_pool_entry *prev;
    int b = 0, p = 0, e; /* p for previous */
    char *end;

#ifdef POOL_DEBUG
    PRINTF("%s+ pool %p\n", __func__, pool);
#endif

    if (!pool->fragmented) {
        if (ret_last_busy) {
            if (mem_pool_full(pool))
                p = -1;
            else
                p = get_next_unused_entry(pool, 0) - 1;
        }

        goto out;
    }

    if (!(pool->bitmap[0] & 1)) {
        b = mem_pool_get_next_used_entry(pool, 1);
        migrate_entry(pool, pool, b, 0, pool->base);
    }

    /* compress bitmap / data */
    while (1) {
        b = mem_pool_get_next_used_entry(pool, p + 1);

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

    e = get_next_unused_entry(pool, p + 1);
    if (e == -1)
        pool->fragmented = 0;
out:
#ifdef POOL_DEBUG
    PRINTF("%s-\n", __func__);
#endif

    /* returns ID of last busy entry (in terms of position in the list) */
    return p;
}

static int mem_pool_grow_bitmap(struct mem_pool * restrict pool)
{
    return mem_pool_resize_bitmap(pool, pool->bitmap_size + 1);
}

void *mem_pool_alloc(struct mem_pool * restrict pool, unsigned long size,
                     struct mem_pool_entry *ret_entry, int defrag)
{
    struct __mem_pool_entry *empty;
    struct __mem_pool_entry *busy;
    char *start = NULL, *end;
    int e, b = -1; // b for "busy/used entry", e for "unused/empty"

#ifdef POOL_DEBUG
    int defragged = 0;
#endif

#ifdef POOL_DEBUG_CANARY
    size += 256;
#endif

#ifdef POOL_DEBUG
    PRINTF("%s+: pool %p (full %d): size=%lu pool.remain=%lu\n",
           __func__, pool, pool->bitmap_full, size, pool->remain);
#endif

    if (size > pool->remain)
        return NULL;

    if (pool->bitmap_full)
        pool->bitmap_full = !mem_pool_grow_bitmap(pool);

    if (pool->bitmap_full)
        return NULL;

retry:
    do {
        e = get_next_unused_entry(pool, b + 1);

        if (e < 0) {
            if (mem_pool_grow_bitmap(pool))
                continue;

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

        b = mem_pool_get_next_used_entry(pool, e + 1);

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

        if (!pool->bitmap_full)
            pool->bitmap_full = (get_next_unused_entry(pool, b + 1) < 0);

        if (pool->bitmap_full)
            pool->bitmap_full = !mem_pool_grow_bitmap(pool);

        mem_pool_set_canary(&pool->entries[e]);

#ifdef POOL_DEBUG
        stats.total_remain -= size;
#endif
    } else if (defrag && !pool->bitmap_full) {
#ifdef POOL_DEBUG
        assert(!defragged);
#endif
        b = defrag_pool(pool, size, 1);
#ifdef POOL_DEBUG
        defragged = 1;
#else
        defrag = 0;
#endif
        goto retry;
    }

    validate_pool(pool);

#ifdef POOL_DEBUG
    PRINTF("%s: pool %p (full %d): e=%d size=%lu ret=%p pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
           __func__, pool, pool->bitmap_full, e, size, start, pool->remain,
           stats.pools_num, stats.total_remain);
#endif

    return start;
}

void mem_pool_free(struct mem_pool_entry *entry)
{
    struct mem_pool *pool = entry->pool;
    unsigned int entry_id = entry->id;
    int b;

#ifdef POOL_DEBUG_VERBOSE
    char *base = mem_pool_entry_addr(entry);
    PRINTF("%s: pool %p: e=%u size=%lu addr=%p\n",
           __func__, pool, entry_id, pool->entries[entry_id].size, base);
#endif
    validate_pool(pool);

    if (!pool->fragmented) {
        b = get_next_unused_entry(pool, 0) - 1;

        if (b != entry_id)
            pool->fragmented = 1;
    }

    pool->bitmap_full = 0;
    pool->remain += pool->entries[entry_id].size;
    clear_bit(pool, entry_id);

    mem_pool_check_canary(&pool->entries[entry_id]);
#ifdef POOL_DEBUG_CANARY
    memset(pool->entries[entry_id].base, 0x88, pool->entries[entry_id].size);
#endif
#ifdef POOL_DEBUG
    stats.total_remain += pool->entries[entry_id].size;
    PRINTF("%s: pool %p: addr=%p pool.remain=%lu stats.pools_num=%u stats.total_remain=%u\n",
            __func__, pool, pool->entries[entry_id].base, pool->remain,
           stats.pools_num, stats.total_remain);
    pool->entries[entry_id].owner = NULL;
    pool->entries[entry_id].base = (void *) 0x66600000;
    pool->entries[entry_id].size = 0x10000000;
#endif
}

void mem_pool_destroy(struct mem_pool *pool)
{
#ifdef POOL_DEBUG
    PRINTF("%s: pool %p: size=%lu remain=%lu pools_num=%u\n",
           __func__, pool, pool->pool_size, pool->remain, stats.pools_num--);
    assert(mem_pool_get_next_used_entry(pool, 0) == -1);
    assert(get_next_unused_entry(pool, 0) == 0);
    assert(pool->remain == pool->pool_size);
    assert(!pool->access_refcount);
    stats.total_remain -= pool->pool_size;
    pool->base = (void *) 0xfff00000;
    pool->pool_size = 0;
#endif

    free(pool->bitmap);
    pool->bitmap = NULL;

    free(pool->entries);
    pool->entries = NULL;
}

/*
 * Transfer as many entries from "pool_from" to "pool_to" as possible.
 *
 * Before the transferring, the "pool_to" is defragmented to maximize
 * the number of transferred entries. We take advantage of the defragmented
 * pool in order to optimize the code a tad.
 *
 * Returns number of transferred entries. The "pool_to" is in defragmented
 * state.
 */
int mem_pool_transfer_entries(struct mem_pool * restrict pool_to,
                              struct mem_pool * restrict pool_from)
{
    struct __mem_pool_entry *busy_from;
    struct __mem_pool_entry *busy_to;
    int b_from = -1, b_to, e_to;
    int transferred_entries = 0;
    int transferred_bytes = 0;
    unsigned long size;
    char *new_base;

#ifdef POOL_DEBUG
    PRINTF("%s: pool to=%p (full=%d remain=%lu) from=%p (full=%d remain=%lu)\n",
           __func__, pool_to, pool_to->bitmap_full, pool_to->remain, pool_from,
           pool_from->bitmap_full, pool_from->remain);
#endif

    assert(pool_from->access_refcount > 0);
    assert(pool_to->access_refcount > 0);

    if (!pool_from->access_refcount || !pool_to->access_refcount)
        return 0;

    if (mem_pool_full(pool_to))
        return 0;

    if (pool_to == pool_from)
        return 0;

    validate_pool(pool_to);
    validate_pool(pool_from);

    if (mem_pool_empty(pool_to)) {
        b_to = -1;
        new_base = pool_to->base;
    } else {
        b_to = defrag_pool(pool_to, ~0ul, 1);
        busy_to = &pool_to->entries[b_to];
        new_base = busy_to->base + busy_to->size;
    }

    while (1) {
        e_to = get_next_unused_entry(pool_to, b_to + 1);

        if (e_to == -1) {
            if (mem_pool_grow_bitmap(pool_to))
                continue;

            pool_to->bitmap_full = 1;
            break;
        }

next_from:
        b_from = mem_pool_get_next_used_entry(pool_from, b_from + 1);

        if (b_from == -1)
            break;

        busy_from = &pool_from->entries[b_from];
        size = busy_from->size;

        if (size <= pool_to->remain) {
            migrate_entry(pool_from, pool_to, b_from, e_to, new_base);
            pool_from->remain += size;
            pool_to->remain -= size;
            new_base += size;
            transferred_bytes += size;
            transferred_entries++;

            b_to = e_to;

            if (pool_to->remain == 0)
                break;
        } else {
            goto next_from;
        }
    }

    if (transferred_entries) {
        pool_from->bitmap_full = 0;
        pool_from->fragmented = !mem_pool_empty(pool_from);
    }

#ifdef POOL_DEBUG
    PRINTF("%s: pool to=%p from=%p transferred_entries=%d transferred_bytes=%d\n",
           __func__, pool_to, pool_from, transferred_entries, transferred_bytes);
#endif

    if (transferred_entries) {
        validate_pool(pool_to);
        validate_pool(pool_from);
    }

    return transferred_bytes;
}

/*
 * Transfer as many entries from "pool_from" to "pool_to" as possible.
 *
 * Returns number of transferred entries.
 */
int mem_pool_transfer_entries_fast(struct mem_pool * restrict pool_to,
                                   struct mem_pool * restrict pool_from)
{
    struct __mem_pool_entry *busy_from;
    struct mem_pool_entry empty_to;
    int transferred_entries = 0;
    int transferred_bytes = 0;
    unsigned long fail_size = ~0ul;
    unsigned long size;
    int b_from = -1, e_to;

#ifdef POOL_DEBUG
    PRINTF("%s: pool to=%p (full=%d remain=%lu) from=%p (full=%d remain=%lu)\n",
           __func__, pool_to, pool_to->bitmap_full, pool_to->remain, pool_from,
           pool_from->bitmap_full, pool_from->remain);
#endif

    assert(pool_from->access_refcount > 0);
    assert(pool_to->access_refcount > 0);

    if (!pool_from->access_refcount || !pool_to->access_refcount)
        return 0;

    if (mem_pool_full(pool_to))
        return 0;

    if (pool_to == pool_from)
        return 0;

    validate_pool(pool_to);
    validate_pool(pool_from);

    while (1) {
        b_from = mem_pool_get_next_used_entry(pool_from, b_from + 1);

        if (b_from == -1)
            break;

        busy_from = &pool_from->entries[b_from];
        size = busy_from->size;
#ifdef POOL_DEBUG_CANARY
        size -= 256;
#endif
        if (size >= fail_size)
            continue;

        if (mem_pool_alloc(pool_to, size, &empty_to, 0) != NULL) {
            e_to = empty_to.id;
#ifdef POOL_DEBUG_CANARY
            size += 256;
#endif
#ifdef POOL_DEBUG
            clear_bit(pool_to, e_to);
            stats.total_remain += size;
#endif
            migrate_entry(pool_from, pool_to, b_from, e_to,
                          pool_to->entries[e_to].base);

            pool_from->remain += size;
            transferred_bytes += size;
            transferred_entries++;

            if (mem_pool_full(pool_to))
                break;
        } else if (size < fail_size) {
            fail_size = size;
        }
    }

    if (transferred_entries) {
        pool_from->bitmap_full = 0;
        pool_from->fragmented = !mem_pool_empty(pool_from);
    }

#ifdef POOL_DEBUG
    PRINTF("%s: pool to=%p from=%p transferred_entries=%d transferred_bytes=%d\n",
           __func__, pool_to, pool_from, transferred_entries, transferred_bytes);
#endif

    if (transferred_entries) {
        validate_pool(pool_to);
        validate_pool(pool_from);
    }

    return transferred_bytes;
}

void mem_pool_defrag(struct mem_pool *pool)
{
    if (!mem_pool_empty(pool))
        defrag_pool(pool, ~0ul, 0);
}

void mem_pool_debug_dump(struct mem_pool *pool)
{
#ifdef POOL_DEBUG_VERBOSE
    struct __mem_pool_entry *busy;
    int b = -1;

    PRINTF("%s: +pool %p (full=%d remain=%lu size=%lu)\n",
           __func__, pool, pool->bitmap_full, pool->remain, pool->pool_size);

    while (1) {
        b = mem_pool_get_next_used_entry(pool, b + 1);

        if (b == -1)
            break;

        busy = &pool->entries[b];

        PRINTF("%s: pool %p: entry[%d]: base=%p size=%lu\n",
               __func__, pool, b, busy->base, busy->size);
    }
#endif
}

void mem_pool_check_entry(struct mem_pool_entry *entry)
{
#ifdef POOL_DEBUG
    struct mem_pool *pool = entry->pool;
    unsigned int entry_id = entry->id;
    unsigned int bits_array = entry_id / 32;
    unsigned long mask = 1 << (entry_id % 32);

    assert(pool->bitmap_size > bits_array);
    assert(pool->bitmap[bits_array] & mask);
    assert(pool->entries[entry_id].base >= pool->base);
    assert(pool->entries[entry_id].base +
           pool->entries[entry_id].size <= pool->base + pool->pool_size);
#endif
}
