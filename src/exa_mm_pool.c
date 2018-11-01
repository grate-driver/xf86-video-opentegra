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

#include "driver.h"
#include "exa_mm.h"

#define ErrorMsg(fmt, args...)                                              \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__,                   \
               __LINE__, __func__, ##args)

#define TEGRA_EXA_POOL_SIZE             0x10000
#define TEGRA_EXA_PAGE_SIZE             0x1000
#define TEGRA_EXA_PAGE_MASK             (TEGRA_EXA_PAGE_SIZE - 1)
#define TEGRA_EXA_POOL_SIZE_MAX         (TEGRA_EXA_POOL_SIZE * 3 / 2)
#define TEGRA_EXA_POOL_SIZE_MERGED_MAX  0x100000

void TegraEXADestroyPool(TegraPixmapPoolPtr pool)
{
    mem_pool_destroy(&pool->pool);
    drm_tegra_bo_unref(pool->bo);
    xorg_list_del(&pool->entry);
    free(pool);
}

static int TegraEXACreatePool(TegraPtr tegra, TegraPixmapPoolPtr *ret,
                              unsigned int bitmap_size, unsigned long size)
{
    TegraPixmapPoolPtr pool;
    int err;

    pool = calloc(1, sizeof(*pool));
    if (!pool) {
        ErrorMsg("failed to allocate pool\n");
        return -ENOMEM;
    }

    err = drm_tegra_bo_new(&pool->bo, tegra->drm, 0, size);
    if (err) {
        ErrorMsg("failed to allocate pools BO: %d\n", err);
        free(pool);
        return err;
    }

    err = drm_tegra_bo_map(pool->bo, &pool->ptr);
    if (err < 0) {
        ErrorMsg("failed to map pool: %d\n", err);
        drm_tegra_bo_unref(pool->bo);
        free(pool);
        return err;
    }

    err = mem_pool_init(&pool->pool, pool->ptr, size, bitmap_size);
    if (err) {
        ErrorMsg("failed to initialize pools BO: %d\n", err);
        drm_tegra_bo_unref(pool->bo);
        free(pool);
        return err;
    }

    *ret = pool;

    return 0;
}

static void *TegraEXAPoolAlloc(TegraEXAPtr exa, TegraPixmapPoolPtr pool,
                               size_t size, struct mem_pool_entry *pool_entry,
                               Bool fast)
{
    void *data = mem_pool_alloc(&pool->pool, size, pool_entry, !fast);

    if (data) {
        /* move successive pool to the head of the list */
        xorg_list_del(&pool->entry);
        xorg_list_add(&pool->entry, &exa->mem_pools);
    }

    return data;
}

static TegraPixmapPoolPtr TegraEXACompactPoolsFast(TegraEXAPtr exa, size_t size)
{
    TegraPixmapPoolPtr pool_to, pool_from = NULL;
    unsigned int transferred;
    unsigned int pass = 3;

again:
    xorg_list_for_each_entry(pool_to, &exa->mem_pools, entry) {
        if (pool_to->pool.remain >= size)
            return pool_to;

        if (pool_to->pool.remain < TEGRA_EXA_OFFSET_ALIGN)
            continue;

        if (pool_to->light)
            continue;

        if (!pool_from) {
            pool_from = pool_to;
            continue;
        }

        if (pool_from->pool.remain < pool_to->pool.remain)
            pool_from = pool_to;
    }

    if (pool_from) {
        pool_from->light = TRUE;

        xorg_list_for_each_entry(pool_to, &exa->mem_pools, entry) {
            if (pool_from->light)
                continue;

            transferred = mem_pool_transfer_entries_fast(&pool_to->pool,
                                                         &pool_from->pool);
            if (!transferred)
                continue;

            if (mem_pool_has_space(&pool_from->pool, size))
                return pool_from;
        }
    }

    if (--pass > 0)
        goto again;

    return NULL;
}

static int TegraEXAShrinkPool(TegraPtr tegra, TegraPixmapPoolPtr shrink_pool,
                              struct xorg_list *new_pools)
{
    TegraPixmapPoolPtr new_pool;
    unsigned long size;
    int err;

    if (shrink_pool->pool.remain < TEGRA_EXA_PAGE_SIZE)
        return 0;

    size = shrink_pool->pool.pool_size - shrink_pool->pool.remain;
    size = TEGRA_ALIGN(size, TEGRA_EXA_PAGE_SIZE);

    err = TegraEXACreatePool(tegra, &new_pool, shrink_pool->pool.bitmap_size,
                             size);
    if (err)
        return err;

    mem_pool_transfer_entries_fast(&new_pool->pool, &shrink_pool->pool);
    TegraEXADestroyPool(shrink_pool);

    xorg_list_append(&new_pool->entry, new_pools);

    return 0;
}

static int TegraEXAMergePools(TegraPtr tegra)
{
    TegraEXAPtr exa = tegra->exa;
    TegraPixmapPoolPtr pool, tmp;
    TegraPixmapPoolPtr new_pool;
    unsigned long unaligned = 0;
    unsigned long bitmap = 0;
    unsigned long size = 0;
    unsigned int pools = 0;
    int err;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry) {
        if (pool->pool.pool_size > TEGRA_EXA_POOL_SIZE_MAX)
            continue;

        if (pool->pool.remain & TEGRA_EXA_PAGE_MASK) {
            unaligned += pool->pool.remain & TEGRA_EXA_PAGE_MASK;
            size += pool->pool.pool_size - pool->pool.remain;
            bitmap += pool->pool.bitmap_size;
            pools++;
        }

        if (size >= TEGRA_EXA_POOL_SIZE_MERGED_MAX)
            break;
    }

    if (unaligned < TEGRA_EXA_POOL_SIZE)
        return 0;

    size = TEGRA_ALIGN(size, TEGRA_EXA_PAGE_SIZE);
    size = (size > TEGRA_EXA_POOL_SIZE_MERGED_MAX) ?
                   TEGRA_EXA_POOL_SIZE_MERGED_MAX : size;
    err = TegraEXACreatePool(tegra, &new_pool, bitmap, size);
    if (err)
            return err;

    xorg_list_for_each_entry_safe(pool, tmp, &exa->mem_pools, entry) {
        if (pool->pool.pool_size > TEGRA_EXA_POOL_SIZE_MAX)
            continue;

        if (pool->pool.remain & TEGRA_EXA_PAGE_MASK) {
            mem_pool_transfer_entries_fast(&new_pool->pool, &pool->pool);

            if (mem_pool_empty(&pool->pool))
                TegraEXADestroyPool(pool);

            if (mem_pool_full(&new_pool->pool))
                break;
        }
    }

    xorg_list_append(&new_pool->entry, &exa->mem_pools);

    return 0;
}

static void TegraEXACompactPoolsSlow(TegraPtr tegra)
{
    TegraPixmapPoolPtr tmp1, tmp2, pool, pool_to, pool_from;
    TegraPixmapPoolPtr light_pools[5];
    TegraPixmapPoolPtr heavy_pools[16];
    TegraEXAPtr exa = tegra->exa;
    struct xorg_list new_pools;
    unsigned long lightest_size;
    unsigned long heaviest_size;
    unsigned int transferred;
    unsigned int i, l, h;
    Bool list_full;
    int err;

    /* merge as much as possible pools into a larger pools */
    TegraEXAMergePools(tegra);

    /*
     * 1) Build two list:
     *  - first "light" list that contains the most empty pools
     *  - second "heavy" list that contains the most filled pools
     *
     * 2) Move as much as possible from light pools to the heavy pools.
     *
     * 3) Destroy the emptied light pools
     *
     * 4) Repeat until nothing can be moved in 2)
     *
     * 5) Shrink pools
     */

all_again:
    memset(light_pools, 0, sizeof(light_pools));
    heaviest_size = ~0ul;
    list_full = FALSE;

    /* build list of light pools, lightest pool is the first entry */
    xorg_list_for_each_entry(pool_from, &exa->mem_pools, entry) {
        pool_from->heavy = FALSE;
        pool_from->light = FALSE;

        if (pool_from->pool.remain < pool_from->pool.pool_size / 2)
            continue;

        if (list_full) {
            if (pool_from->pool.remain <= heaviest_size)
                continue;
        }

        for (i = 0; i < TEGRA_ARRAY_SIZE(light_pools); i++) {
            if (!light_pools[i]) {
                light_pools[i] = pool_from;
                light_pools[i]->light = TRUE;
                break;
            }

            if (light_pools[i]->pool.remain < pool_from->pool.remain) {
                tmp1 = light_pools[i];

                light_pools[i] = pool_from;
                light_pools[i]->light = TRUE;

                for (++i; i < TEGRA_ARRAY_SIZE(light_pools) && tmp1; i++) {
                    tmp2 = light_pools[i];
                    light_pools[i] = tmp1;
                    tmp1 = tmp2;
                }

                if (tmp1)
                    tmp1->light = FALSE;

                if (pool_from->pool.remain < heaviest_size)
                    heaviest_size = pool_from->pool.remain;

                break;
            }
        }

        if (i == TEGRA_ARRAY_SIZE(light_pools))
            list_full = TRUE;
    }

heavy_again:
    memset(heavy_pools, 0, sizeof(heavy_pools));
    lightest_size = 0;
    list_full = FALSE;

    /* build list of heavy pools, heaviest pool is the first entry */
    xorg_list_for_each_entry(pool_to, &exa->mem_pools, entry) {
        if (pool_to->light || pool_to->heavy)
            continue;

        if (mem_pool_full(&pool_to->pool))
            continue;

        if (pool_to->pool.remain < TEGRA_EXA_OFFSET_ALIGN)
            continue;

        if (list_full) {
            if (pool_to->pool.remain >= lightest_size)
                continue;
        }

        for (i = 0; i < TEGRA_ARRAY_SIZE(heavy_pools); i++) {
            if (!heavy_pools[i]) {
                heavy_pools[i] = pool_to;
                heavy_pools[i]->heavy = TRUE;
                break;
            }

            if (heavy_pools[i]->pool.remain > pool_to->pool.remain) {
                tmp1 = heavy_pools[i];

                heavy_pools[i] = pool_to;
                heavy_pools[i]->heavy = TRUE;

                for (++i; i < TEGRA_ARRAY_SIZE(heavy_pools) && tmp1; i++) {
                    tmp2 = heavy_pools[i];
                    heavy_pools[i] = tmp1;
                    tmp1 = tmp2;
                }

                if (tmp1)
                    tmp1->heavy = FALSE;

                if (pool_to->pool.remain > lightest_size)
                    lightest_size = pool_to->pool.remain;

                break;
            }
        }

        if (i == TEGRA_ARRAY_SIZE(heavy_pools))
            list_full = TRUE;
    }

    transferred = 0;

    /* move entries from light pools to the heavy */
    for (l = 0; l < TEGRA_ARRAY_SIZE(light_pools); l++) {
        pool_from = light_pools[l];

        if (!pool_from)
            continue;

        for (h = 0; h < TEGRA_ARRAY_SIZE(heavy_pools) && heavy_pools[h]; h++) {
            pool_to = heavy_pools[h];

            transferred += mem_pool_transfer_entries(&pool_to->pool,
                                                     &pool_from->pool);
        }

        /* destroy emptied pool */
        if (mem_pool_empty(&pool_from->pool)) {
            TegraEXADestroyPool(pool_from);
            light_pools[l] = NULL;
        }
    }

    for (l = 0, pool = NULL; l < TEGRA_ARRAY_SIZE(light_pools) && !pool; l++)
        pool = light_pools[l];

    /* try hard */
    if (pool && transferred)
        goto heavy_again;

    /* try very hard */
    if (!pool && transferred)
        goto all_again;

    /* cut off unused space from the pools */
    {
        xorg_list_init(&new_pools);

        xorg_list_for_each_entry_safe(pool, tmp1, &exa->mem_pools, entry) {
            if (!pool->light) {
                err = TegraEXAShrinkPool(tegra, pool, &new_pools);
                if (err)
                    break;
            } else {
                pool->light = FALSE;
            }
        }

        xorg_list_for_each_entry_safe(pool, tmp1, &new_pools, entry) {
            xorg_list_del(&pool->entry);
            xorg_list_add(&pool->entry, &exa->mem_pools);
        }
    }

#ifdef POOL_DEBUG
    xorg_list_for_each_entry(pool, &exa->mem_pools, entry)
        mem_pool_debug_dump(&pool->pool);
#endif
}

static unsigned long TegraEXAPoolsAvailableSpaceTotal(TegraEXAPtr exa)
{
    TegraPixmapPoolPtr pool;
    unsigned long spare = 0;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry)
        spare += pool->pool.remain;

    return spare;
}

static Bool TegraEXACompactPoolsSlowAllowed(TegraEXAPtr exa, size_t size_limit)
{
    struct timespec time;
    Bool compact = FALSE;
    Bool expired = TRUE;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);

    if (time.tv_sec - exa->pool_slow_compact_time < 15)
        expired = FALSE;

    if (size_limit) {
        if (TegraEXAPoolsAvailableSpaceTotal(exa) < size_limit)
            expired = FALSE;
        else
            compact = TRUE;
    }

    if (!compact && size_limit)
        exa->pool_slow_compact_time = time.tv_sec;

    return expired;
}

static Bool TegraEXACompactPoolsFastAllowed(TegraEXAPtr exa, size_t size_limit)
{
    struct timespec time;
    Bool compact = FALSE;
    Bool expired = TRUE;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);

    if (time.tv_sec - exa->pool_fast_compact_time < 3)
        expired = FALSE;

    if (size_limit) {
        if (TegraEXAPoolsAvailableSpaceTotal(exa) < size_limit)
            expired = FALSE;
        else
            compact = TRUE;
    }

    if (!compact && size_limit)
        exa->pool_fast_compact_time = time.tv_sec;

    return expired;
}

static TegraPixmapPoolPtr TegraEXACompactPools(TegraPtr tegra, size_t size)
{
    TegraEXAPtr exa = tegra->exa;
    Bool slow_compact;
    size_t limit;

    if (xorg_list_is_empty(&exa->mem_pools))
        return NULL;

    limit = TEGRA_EXA_POOL_SIZE * 10;

    slow_compact = TegraEXACompactPoolsSlowAllowed(exa, limit * 3 / 2);
    if (slow_compact)
        TegraEXACompactPoolsSlow(tegra);

    if (TegraEXACompactPoolsFastAllowed(exa, size))
        return TegraEXACompactPoolsFast(exa, size);

    if (slow_compact)
        return TegraEXACompactPoolsFast(exa, limit);

    return NULL;
}

void TegraEXAPoolFree(struct mem_pool_entry *pool_entry)
{
    TegraPixmapPoolPtr pool = TEGRA_CONTAINER_OF(pool_entry->pool,
                                                 TegraPixmapPool, pool);

    mem_pool_free(pool_entry);

    if (mem_pool_empty(&pool->pool))
        TegraEXADestroyPool(pool);

    pool_entry->pool = NULL;
    pool_entry->id = -1;
}

static int TegraEXAAllocateFromPool(TegraPtr tegra, size_t size,
                                    struct mem_pool_entry *pool_entry)
{
    TegraEXAPtr exa = tegra->exa;
    TegraPixmapPoolPtr pool;
    size_t pool_size;
    void *data;
    int err;

    if (!tegra->exa_pool_alloc)
        return -EINVAL;

    size = TEGRA_ALIGN(size, TEGRA_EXA_OFFSET_ALIGN);

    if (size > TEGRA_EXA_POOL_SIZE_MAX)
        return -ENOMEM;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry) {
        data = TegraEXAPoolAlloc(exa, pool, size, pool_entry, TRUE);
        if (data)
            goto success;
    }

    if (TegraEXAPoolsAvailableSpaceTotal(exa) >= size)
        pool = TegraEXACompactPools(tegra, size);
    else
        pool = NULL;

    if (pool) {
        data = TegraEXAPoolAlloc(exa, pool, size, pool_entry, FALSE);
        if (data)
            goto success;
    }

again:
    pool_size = TEGRA_ALIGN(size, TEGRA_EXA_POOL_SIZE);
    err = TegraEXACreatePool(tegra, &pool, 1, pool_size);
    if (err) {
        if (err == -ENOMEM) {
            if (TegraEXACompactPoolsSlowAllowed(exa, 0)) {
                TegraEXACompactPoolsSlow(tegra);
                goto again;
            }
        }

        return err;
    }

    xorg_list_add(&pool->entry, &exa->mem_pools);

    data = TegraEXAPoolAlloc(exa, pool, size, pool_entry, TRUE);
    if (!data) {
        ErrorMsg("FATAL: Failed to allocate from a new pool\n");
        return -ENOMEM;
    }

success:
    return 0;
}

Bool TegraEXAAllocateDRMFromPool(TegraPtr tegra,
                                 TegraPixmapPtr pixmap,
                                 unsigned int size)
{
    unsigned int size_masked = size & TEGRA_EXA_PAGE_MASK;
    int err;

    if (!pixmap->accel || pixmap->dri)
        return FALSE;

    if (size_masked == 0)
        return FALSE;

    if (size > TEGRA_EXA_POOL_SIZE / 6) {
        if (TEGRA_EXA_PAGE_SIZE - size_masked <= TEGRA_EXA_OFFSET_ALIGN)
            return FALSE;
    }

    err = TegraEXAAllocateFromPool(tegra, size, &pixmap->pool_entry);
    if (err)
        return FALSE;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_POOL;

    return TRUE;
}
