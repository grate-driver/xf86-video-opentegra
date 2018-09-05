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

void TegraEXADestroyPool(TegraPixmapPoolPtr pool)
{
    mem_pool_destroy(&pool->pool);
    drm_tegra_bo_unref(pool->bo);
    xorg_list_del(&pool->entry);
    free(pool);
}

static int TegraEXACreatePool(TegraPtr tegra, TegraPixmapPoolPtr *ret)
{
    TegraPixmapPoolPtr pool;
    int err;

    pool = calloc(1, sizeof(*pool));
    if (!pool) {
        ErrorMsg("failed to allocate pool\n");
        return -ENOMEM;
    }

    err = drm_tegra_bo_new(&pool->bo, tegra->drm, 0, TEGRA_EXA_POOL_SIZE);
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

    mem_pool_init(&pool->pool, pool->ptr, TEGRA_EXA_POOL_SIZE);

    *ret = pool;

    return 0;
}

static void *TegraEXAPoolAlloc(TegraPixmapPoolPtr pool, size_t size,
                               struct mem_pool_entry *pool_entry)
{
    void *data;

    data = mem_pool_alloc(&pool->pool, size, pool_entry);
    if (data)
        pool->alloc_cnt++;

    return data;
}

static unsigned long TegraEXAPoolsAvailableSpaceTotal(TegraEXAPtr exa)
{
    TegraPixmapPoolPtr pool;
    unsigned long spare = 0;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry)
        spare += pool->pool.remain;

    return spare;
}

void TegraEXACompactPools(TegraEXAPtr exa, Bool force, size_t size)
{
    TegraPixmapPoolPtr pool_to, pool_from;
    struct xorg_list *to, *from;
    int transferred;
    int pass = 3;
    int done;

    if (xorg_list_is_empty(&exa->mem_pools))
        return;

    if (force) {
        struct timespec time;

        clock_gettime(CLOCK_MONOTONIC, &time);

        if (time.tv_sec - exa->pool_compact_time < 15)
            return;

        exa->pool_compact_time = time.tv_sec;
    }

repeate:
    to = exa->mem_pools.next;
    done = 1;

    /*
     * Move as many entries as we can into the first pool from the last pool,
     * then from the last-1 pool and so on.
     *
     * Then move as many entries as we can into the second pool from the last
     * pool, then from the last-1 pool and so on.
     *
     * As a result, the last pool will become empty and orphaned, hence it
     * could be destroyed.
     *
     * This procedure is repeated in several passes and stops if there was
     * no transfers made during the pass.
     *
     * If "force" isn't set, compaction stops once emptied pool has enough
     * space.
     */
    do {
        pool_to = xorg_list_entry(to, TegraPixmapPool, entry);
        from = exa->mem_pools.prev;

        if (to->next == &exa->mem_pools)
            goto out;

        do {
            pool_from = xorg_list_entry(from, TegraPixmapPool, entry);
            from = from->prev;

            transferred = mem_pool_transfer_entries(&pool_to->pool,
                                                    &pool_from->pool);
            if (transferred) {
                pool_to->alloc_cnt += transferred;
                pool_from->alloc_cnt -= transferred;

                if (!force && pool_from->pool.remain >= size)
                    return;

                if (!pool_from->alloc_cnt)
                    TegraEXADestroyPool(pool_from);
                else
                    done = 0;
            }

        } while (from != to);

        to = to->next;
    } while (to != &exa->mem_pools);

out:
    if (force && --pass && !done)
        goto repeate;

    mem_pool_defrag(&pool_to->pool);
}

void TegraEXAPoolFree(TegraPixmapPoolPtr pool,
                      struct mem_pool_entry *pool_entry)
{
    mem_pool_free(pool_entry);

    if (--pool->alloc_cnt == 0)
        TegraEXADestroyPool(pool);

    pool_entry->pool = NULL;
    pool_entry->id = -1;
}

static int TegraEXAAllocateFromPool(TegraPtr tegra, size_t size,
                                    struct mem_pool_entry *pool_entry)
{
    TegraEXAPtr exa = tegra->exa;
    TegraPixmapPoolPtr p;
    Bool retried = FALSE;
    unsigned long spare;
    void *data;
    int err;

    if (!tegra->exa_pool_alloc)
        return -EINVAL;

    size = TEGRA_ALIGN(size, TEGRA_EXA_OFFSET_ALIGN);

retry:
    xorg_list_for_each_entry(p, &exa->mem_pools, entry) {
        data = TegraEXAPoolAlloc(p, size, pool_entry);
        if (data)
            goto success;
    }

    if (!retried) {
        spare = TegraEXAPoolsAvailableSpaceTotal(exa);

        if (spare > size) {
            TegraEXACompactPools(exa, FALSE, size);
            retried = TRUE;
            goto retry;
        }
    }

    err = TegraEXACreatePool(tegra, &p);
    if (err)
        return err;

    xorg_list_append(&p->entry, &exa->mem_pools);

    data = TegraEXAPoolAlloc(p, size, pool_entry);
    if (!data) {
        ErrorMsg("failed to allocate from a new pool\n");
        return -ENOMEM;
    }

success:
    return 0;
}

Bool TegraEXAAllocateDRMFromPool(TegraPtr tegra,
                                 TegraPixmapPtr pixmap,
                                 unsigned int size,
                                 unsigned int bpp)
{
    if (size >= TEGRA_EXA_POOL_SIZE * 2 / 3)
        return FALSE;

    if (0x1000 - (size & 0xFFF) <= TEGRA_EXA_OFFSET_ALIGN)
        return FALSE;

    if (bpp != 8 && bpp != 16 && bpp != 32)
        return FALSE;

    if (pixmap->dri)
        return FALSE;

    return TegraEXAAllocateFromPool(tegra, size,
                                    &pixmap->pool_entry) == 0;
}

Bool TegraEXAAllocateDRM(TegraPtr tegra,
                         TegraPixmapPtr pixmap,
                         unsigned int size,
                         unsigned int bpp)
{
    if (bpp != 8 && bpp != 16 && bpp != 32)
        return FALSE;

    return drm_tegra_bo_new(&pixmap->bo, tegra->drm, 0, size) == 0;
}

Bool TegraEXAAllocateMem(TegraPixmapPtr pixmap, unsigned int size)
{
    pixmap->fallback = malloc(size);

    return pixmap->fallback != NULL;
}
