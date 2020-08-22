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

#ifndef __TEGRA_MEM_POOL_H
#define __TEGRA_MEM_POOL_H

#include <assert.h>
#include <stddef.h>

#ifdef DEBUG
#define POOL_DEBUG
#endif

// #define POOL_DEBUG_VERBOSE
// #define POOL_DEBUG_CANARY

struct mem_pool_entry;

struct __mem_pool_entry {
    char *base;
    unsigned long size;
    struct mem_pool_entry *owner;
};

struct mem_pool_entry {
    struct mem_pool *pool;
    unsigned int id : 16;
};

typedef void (*mem_pool_memcpy)(char *dst, const char *src, int size);
typedef void (*mem_pool_memmove)(char *dst, const char *src, int size);

struct mem_pool {
    char *base;
    char *vbase;
    int fragmented:1;
    int bitmap_full:1;
    int access_refcount;
    unsigned long remain;
    unsigned long pool_size;
    unsigned long bitmap_size;
    unsigned long *bitmap;
    unsigned long base_offset;
    struct __mem_pool_entry *entries;
    mem_pool_memcpy  memcpy;
    mem_pool_memmove memmove;
};

int mem_pool_init(struct mem_pool *pool, unsigned long size,
                  unsigned int bitmap_size,
                  mem_pool_memcpy memcpy,
                  mem_pool_memmove memmove);
void mem_pool_destroy(struct mem_pool *pool);
int mem_pool_transfer_entries(struct mem_pool *pool_to,
                              struct mem_pool *pool_from);
int mem_pool_transfer_entries_fast(struct mem_pool *pool_to,
                                   struct mem_pool *pool_from);
void *mem_pool_alloc(struct mem_pool *pool, unsigned long size,
                     struct mem_pool_entry *ret_entry, int defrag);
void mem_pool_free(struct mem_pool_entry *entry);
void mem_pool_defrag(struct mem_pool *pool);
void mem_pool_debug_dump(struct mem_pool *pool);
void mem_pool_check_canary(struct __mem_pool_entry *entry);
void mem_pool_check_entry(struct mem_pool_entry *entry);

static inline int mem_pool_has_space(struct mem_pool *pool, unsigned long size)
{
    return !(size > pool->remain || pool->bitmap_full);
}

static inline int mem_pool_full(struct mem_pool *pool)
{
    return (!pool->remain || pool->bitmap_full);
}

static inline int mem_pool_empty(struct mem_pool *pool)
{
    return pool->pool_size == pool->remain;
}

static inline unsigned long mem_pool_entry_offset(struct mem_pool_entry *entry)
{
    struct mem_pool *pool = entry->pool;
    unsigned int entry_id = entry->id;

#ifdef POOL_DEBUG
    mem_pool_check_entry(entry);
#endif
#ifdef POOL_DEBUG_CANARY
    mem_pool_check_canary(&pool->entries[entry_id]);
#endif

    return pool->entries[entry_id].base - pool->base;
}

static inline void *mem_pool_entry_addr(struct mem_pool_entry *entry)
{
    struct mem_pool *pool = entry->pool;

    return pool->vbase + mem_pool_entry_offset(entry);
}

int mem_pool_get_next_used_entry(struct mem_pool *pool,
                                 unsigned int start);

#define MEM_POOL_FOR_EACH_ENTRY(POOL, ENTRY, ITR)               \
    for (ITR = mem_pool_get_next_used_entry(POOL, 0),           \
         ENTRY = (POOL)->entries[ITR < 0 ? 0 : ITR].owner;      \
         ITR != -1;                                             \
         ITR = mem_pool_get_next_used_entry(POOL, ITR + 1),     \
         ENTRY = (POOL)->entries[ITR < 0 ? 0 : ITR].owner)

static inline void mem_pool_open_access(struct mem_pool *pool, char *vbase)
{
    if (pool->access_refcount++)
        assert(pool->vbase == vbase);

    pool->vbase = vbase;
}

static inline void mem_pool_close_access(struct mem_pool *pool)
{
    if (pool->access_refcount-- == 1)
        pool->vbase = NULL;

    assert(pool->access_refcount >= 0);
}

#endif
