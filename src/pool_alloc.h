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

struct mem_pool {
    char *base;
    int fragmented:1;
    int bitmap_full:1;
    unsigned long remain;
    unsigned long pool_size;
    unsigned long bitmap_size;
    unsigned long *bitmap;
    struct __mem_pool_entry *entries;
};

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

static inline void *mem_pool_entry_addr(struct mem_pool_entry *entry)
{
    struct mem_pool *pool = entry->pool;
    unsigned int entry_id = entry->id;

#ifdef POOL_DEBUG
    mem_pool_check_entry(entry);
#endif
#ifdef POOL_DEBUG_CANARY
    mem_pool_check_canary(&pool->entries[entry_id]);
#endif

    return pool->entries[entry_id].base;
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

#endif
