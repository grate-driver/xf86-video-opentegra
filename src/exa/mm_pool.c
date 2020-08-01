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

#define TEGRA_EXA_POOL_SIZE             (64 * 1024)
#define TEGRA_EXA_PAGE_SIZE             4096
#define TEGRA_EXA_PAGE_MASK             (TEGRA_EXA_PAGE_SIZE - 1)
#define TEGRA_EXA_POOL_SIZE_MAX         (TEGRA_EXA_POOL_SIZE * 3 / 2)
#define TEGRA_EXA_POOL_SIZE_MERGED_MAX  (1 * 1024 * 1024)

static inline struct tegra_pixmap *
to_tegra_pixmap(struct mem_pool_entry *pool_entry)
{
    return TEGRA_CONTAINER_OF(pool_entry, struct tegra_pixmap, pool_entry);
}

static inline struct tegra_pixmap_pool *to_tegra_pool(struct mem_pool *pool)
{
    return TEGRA_CONTAINER_OF(pool, struct tegra_pixmap_pool, pool);
}

static bool tegra_exa_pool_is_busy(struct tegra_exa *exa,
                                   struct tegra_pixmap_pool *pool)
{
    struct mem_pool_entry *pool_entry;
    int pool_itr;

    MEM_POOL_FOR_EACH_ENTRY(&pool->pool, pool_entry, pool_itr) {
        struct tegra_pixmap *pix = to_tegra_pixmap(pool_entry);
        if (tegra_exa_pixmap_is_busy(exa, pix))
            return true;
    }

    return false;
}

static void tegra_exa_fence_pool_entries(struct tegra_pixmap_pool *pool)
{
    struct mem_pool_entry *pool_entry;
    int pool_itr;

    MEM_POOL_FOR_EACH_ENTRY(&pool->pool, pool_entry, pool_itr) {
        struct tegra_pixmap *pix = to_tegra_pixmap(pool_entry);
        TEGRA_PIXMAP_WAIT_ALL_FENCES(pix);
    }
}

static void tegra_exa_pixmap_pool_destroy(struct tegra_pixmap_pool *pool)
{
    mem_pool_destroy(&pool->pool);
    drm_tegra_bo_unref(pool->bo);
    xorg_list_del(&pool->entry);
    free(pool);
}

static void tegra_exa_pool_memcpy(char *dst, const char *src, int size)
{
    tegra_memcpy_vfp_threaded(dst, src, size, tegra_memcpy_vfp_aligned);
}

static int tegra_exa_pixmap_pool_create(TegraPtr tegra,
                                        struct tegra_pixmap_pool **ret,
                                        unsigned int bitmap_size,
                                        unsigned long size)
{
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap_pool *pool;
    unsigned long flags;
    int drm_ver;
    int err;

    pool = calloc(1, sizeof(*pool));
    if (!pool) {
        ERROR_MSG("failed to allocate pool\n");
        return -ENOMEM;
    }

    drm_ver = drm_tegra_version(tegra->drm);
    flags = exa->default_drm_bo_flags;

    if (drm_ver >= GRATE_KERNEL_DRM_VERSION &&
            size <= TEGRA_EXA_POOL_SIZE_MERGED_MAX &&
                exa->has_iommu)
        flags |= DRM_TEGRA_GEM_CREATE_SPARSE;

    err = drm_tegra_bo_new(&pool->bo, tegra->drm, flags, size);
    if (err) {
        ERROR_MSG("failed to allocate pools BO: %d\n", err);
        free(pool);
        return err;
    }

    err = mem_pool_init(&pool->pool, size, bitmap_size,
                        tegra_exa_pool_memcpy, tegra_memmove_vfp_aligned);
    if (err) {
        ERROR_MSG("failed to initialize pool: %d\n", err);
        drm_tegra_bo_unref(pool->bo);
        free(pool);
        return err;
    }

    xorg_list_init(&pool->entry);

    *ret = pool;

    return 0;
}

static int tegra_exa_pixmap_pool_map(struct tegra_pixmap_pool *pool)
{
    void *ptr;
    int err;

    err = drm_tegra_bo_map(pool->bo, &ptr);
    if (err < 0) {
        ERROR_MSG("failed to map pool: %d\n", err);
        return err;
    }

    mem_pool_open_access(&pool->pool, ptr);

    return 0;
}

static void tegra_exa_pixmap_pool_unmap(struct tegra_pixmap_pool *pool)
{
    int err = drm_tegra_bo_unmap(pool->bo);
    if (err < 0)
        ERROR_MSG("failed to unmap pool: %d\n", err);

    mem_pool_close_access(&pool->pool);
}

static void *
tegra_exa_pixmap_pool_map_entry(struct mem_pool_entry *pool_entry)
{
    struct tegra_pixmap_pool *pool = to_tegra_pool(pool_entry->pool);
    int err;

    err = tegra_exa_pixmap_pool_map(pool);
    if (err)
        return NULL;

    return mem_pool_entry_addr(pool_entry);
}

static void
tegra_exa_pixmap_pool_unmap_entry(struct mem_pool_entry *pool_entry)
{
    struct tegra_pixmap_pool *pool = to_tegra_pool(pool_entry->pool);
    tegra_exa_pixmap_pool_unmap(pool);
}

static void *tegra_exa_pixmap_pool_alloc(struct tegra_exa *exa,
                                         struct tegra_pixmap_pool *pool,
                                         size_t size,
                                         struct mem_pool_entry *pool_entry,
                                         bool fast)
{
    void *data = mem_pool_alloc(&pool->pool, size, pool_entry, false);

    if (!data && !fast && mem_pool_has_space(&pool->pool, size)) {
        tegra_exa_pixmap_pool_map(pool);
        tegra_exa_fence_pool_entries(pool);

        data = mem_pool_alloc(&pool->pool, size, pool_entry, true);

        tegra_exa_pixmap_pool_unmap(pool);
    }

    if (data) {
        /*
         * Move succeeded pool to the head of the pools list since it just
         * was compacted, and thus, it makes sense to try to allocate from this
         * pool at first for next allocations.
         */
        xorg_list_del(&pool->entry);
        xorg_list_add(&pool->entry, &exa->mem_pools);
    }

    return data;
}

static struct tegra_pixmap_pool *
tegra_exa_compact_pools_fast(struct tegra_exa *exa, size_t size)
{
    struct tegra_pixmap_pool *pool_to, *pool_from = NULL;
    unsigned int transferred;
    unsigned int pass = 3;
    struct timespec time;

    PROFILE_DEF(fast_compaction);
    PROFILE_START(fast_compaction);

again:
    xorg_list_for_each_entry(pool_to, &exa->mem_pools, entry) {
        if (pool_to->pool.remain >= size) {
            PROFILE_STOP(fast_compaction);
            return pool_to;
        }

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
        pool_from->light = true;

        xorg_list_for_each_entry(pool_to, &exa->mem_pools, entry) {
            if (pool_from->light)
                continue;

            if (mem_pool_full(&pool_to->pool))
                continue;

            tegra_exa_pixmap_pool_map(pool_to);
            tegra_exa_pixmap_pool_map(pool_from);

            tegra_exa_fence_pool_entries(pool_from);

            transferred = mem_pool_transfer_entries_fast(&pool_to->pool,
                                                         &pool_from->pool);
            exa->stats.num_pool_fast_compaction_tx_bytes += transferred;

            tegra_exa_pixmap_pool_unmap(pool_from);
            tegra_exa_pixmap_pool_unmap(pool_to);

            if (!transferred)
                continue;

            if (mem_pool_has_space(&pool_from->pool, size)) {
                PROFILE_STOP(fast_compaction);
                return pool_from;
            }
        }
    }

    if (--pass > 0)
        goto again;

    PROFILE_STOP(fast_compaction);

    exa->stats.num_pool_fast_compactions++;

    clock_gettime(CLOCK_MONOTONIC, &time);
    exa->pool_fast_compact_time = time.tv_sec;

    return NULL;
}

static int tegra_exa_shrink_pool(TegraPtr tegra,
                                 struct tegra_pixmap_pool *shrink_pool,
                                 struct xorg_list *new_pools)
{
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap_pool *new_pool;
    unsigned long size;
    int err;

    if (shrink_pool->pool.remain < TEGRA_EXA_PAGE_SIZE)
        return 0;

    size = shrink_pool->pool.pool_size - shrink_pool->pool.remain;
    size = TEGRA_ALIGN(size, TEGRA_EXA_PAGE_SIZE);

    err = tegra_exa_pixmap_pool_create(tegra, &new_pool,
                                       shrink_pool->pool.bitmap_size, size);
    if (err)
        return err;

    tegra_exa_pixmap_pool_map(new_pool);
    tegra_exa_pixmap_pool_map(shrink_pool);

    tegra_exa_fence_pool_entries(shrink_pool);

    size = mem_pool_transfer_entries_fast(&new_pool->pool, &shrink_pool->pool);
    exa->stats.num_pool_slow_compaction_tx_bytes += size;

    tegra_exa_pixmap_pool_unmap(shrink_pool);
    tegra_exa_pixmap_pool_unmap(new_pool);

    tegra_exa_pixmap_pool_destroy(shrink_pool);

    xorg_list_append(&new_pool->entry, new_pools);

    return 0;
}

static int tegra_exa_merge_pools(TegraPtr tegra)
{
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap_pool *pool, *tmp;
    struct tegra_pixmap_pool *new_pool;
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
            size      += pool->pool.pool_size - pool->pool.remain;
            bitmap    += pool->pool.bitmap_size;
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
    err = tegra_exa_pixmap_pool_create(tegra, &new_pool, bitmap, size);
    if (err)
            return err;

    xorg_list_for_each_entry_safe(pool, tmp, &exa->mem_pools, entry) {
        if (pool->pool.pool_size > TEGRA_EXA_POOL_SIZE_MAX)
            continue;

        if (pool->pool.remain & TEGRA_EXA_PAGE_MASK) {
            tegra_exa_pixmap_pool_map(new_pool);
            tegra_exa_pixmap_pool_map(pool);

            tegra_exa_fence_pool_entries(pool);

            size = mem_pool_transfer_entries_fast(&new_pool->pool, &pool->pool);
            exa->stats.num_pool_slow_compaction_tx_bytes += size;

            tegra_exa_pixmap_pool_unmap(pool);
            tegra_exa_pixmap_pool_unmap(new_pool);

            if (mem_pool_empty(&pool->pool))
                tegra_exa_pixmap_pool_destroy(pool);

            if (mem_pool_full(&new_pool->pool))
                break;
        }
    }

    xorg_list_append(&new_pool->entry, &exa->mem_pools);

    return 0;
}

static void tegra_exa_compact_pools_slow(TegraPtr tegra)
{
    struct tegra_pixmap_pool *tmp1, *tmp2, *pool, *pool_to, *pool_from;
    struct tegra_pixmap_pool *light_pools[5];
    struct tegra_pixmap_pool *heavy_pools[16];
    struct tegra_exa *exa = tegra->exa;
    struct xorg_list new_pools;
    unsigned long lightest_size;
    unsigned long heaviest_size;
    unsigned int transferred;
    unsigned int i, l, h;
    struct timespec time;
    bool list_full;
    int err;

    PROFILE_DEF(slow_compaction);
    PROFILE_START(slow_compaction);

    /* merge as much as possible pools into a larger pools */
    tegra_exa_merge_pools(tegra);

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
    list_full = false;

    /* build list of light pools, lightest pool is the first entry */
    xorg_list_for_each_entry(pool_from, &exa->mem_pools, entry) {
        pool_from->heavy = false;
        pool_from->light = false;

        if (pool_from->pool.remain < pool_from->pool.pool_size / 2)
            continue;

        if (list_full) {
            if (pool_from->pool.remain <= heaviest_size)
                continue;
        }

        for (i = 0; i < TEGRA_ARRAY_SIZE(light_pools); i++) {
            if (!light_pools[i]) {
                light_pools[i] = pool_from;
                light_pools[i]->light = true;
                break;
            }

            if (light_pools[i]->pool.remain < pool_from->pool.remain) {
                tmp1 = light_pools[i];

                light_pools[i] = pool_from;
                light_pools[i]->light = true;

                for (++i; i < TEGRA_ARRAY_SIZE(light_pools) && tmp1; i++) {
                    tmp2 = light_pools[i];
                    light_pools[i] = tmp1;
                    tmp1 = tmp2;
                }

                if (tmp1)
                    tmp1->light = false;

                if (pool_from->pool.remain < heaviest_size)
                    heaviest_size = pool_from->pool.remain;

                break;
            }
        }

        if (i == TEGRA_ARRAY_SIZE(light_pools))
            list_full = true;
    }

heavy_again:
    memset(heavy_pools, 0, sizeof(heavy_pools));
    lightest_size = 0;
    list_full = false;

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
                heavy_pools[i]->heavy = true;
                break;
            }

            if (heavy_pools[i]->pool.remain > pool_to->pool.remain) {
                tmp1 = heavy_pools[i];

                heavy_pools[i] = pool_to;
                heavy_pools[i]->heavy = true;

                for (++i; i < TEGRA_ARRAY_SIZE(heavy_pools) && tmp1; i++) {
                    tmp2 = heavy_pools[i];
                    heavy_pools[i] = tmp1;
                    tmp1 = tmp2;
                }

                if (tmp1)
                    tmp1->heavy = false;

                if (pool_to->pool.remain > lightest_size)
                    lightest_size = pool_to->pool.remain;

                break;
            }
        }

        if (i == TEGRA_ARRAY_SIZE(heavy_pools))
            list_full = true;
    }

    transferred = 0;

    /* move entries from light pools to the heavy */
    for (l = 0; l < TEGRA_ARRAY_SIZE(light_pools); l++) {
        pool_from = light_pools[l];

        if (!pool_from)
            continue;

        for (h = 0; h < TEGRA_ARRAY_SIZE(heavy_pools) && heavy_pools[h]; h++) {
            pool_to = heavy_pools[h];

            if (mem_pool_full(&pool_to->pool))
                continue;

            tegra_exa_pixmap_pool_map(pool_to);
            tegra_exa_pixmap_pool_map(pool_from);

            tegra_exa_fence_pool_entries(pool_to);
            tegra_exa_fence_pool_entries(pool_from);

            transferred += mem_pool_transfer_entries(&pool_to->pool,
                                                     &pool_from->pool);
            exa->stats.num_pool_slow_compaction_tx_bytes += transferred;

            tegra_exa_pixmap_pool_unmap(pool_from);
            tegra_exa_pixmap_pool_unmap(pool_to);
        }

        /* destroy emptied pool */
        if (mem_pool_empty(&pool_from->pool)) {
            tegra_exa_pixmap_pool_destroy(pool_from);
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
                err = tegra_exa_shrink_pool(tegra, pool, &new_pools);
                if (err)
                    break;
            } else {
                pool->light = false;
            }
        }

        xorg_list_for_each_entry_safe(pool, tmp1, &new_pools, entry) {
            xorg_list_del(&pool->entry);
            xorg_list_add(&pool->entry, &exa->mem_pools);
        }
    }

    PROFILE_STOP(slow_compaction);

#ifdef POOL_DEBUG
    xorg_list_for_each_entry(pool, &exa->mem_pools, entry)
    mem_pool_debug_dump(&pool->pool);
#endif

    exa->stats.num_pool_slow_compactions++;

    clock_gettime(CLOCK_MONOTONIC, &time);
    exa->pool_slow_compact_time = time.tv_sec;
}

static unsigned long
tegra_exa_pools_total_available_space(struct tegra_exa *exa)
{
    struct tegra_pixmap_pool *pool;
    unsigned long spare = 0;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry)
        spare += pool->pool.remain;

    return spare;
}

static bool
tegra_exa_slow_compaction_allowed(struct tegra_exa *exa, size_t size_limit)
{
    struct timespec time;
    bool expired = true;

    clock_gettime(CLOCK_MONOTONIC, &time);

    if (time.tv_sec - exa->pool_slow_compact_time < 15)
        expired = false;

    if (size_limit) {
        if (tegra_exa_pools_total_available_space(exa) < size_limit)
            expired = false;
    }

    if (expired)
        exa->pool_slow_compact_time = time.tv_sec;

    return expired;
}

static bool
tegra_exa_fast_compaction_allowed(struct tegra_exa *exa, size_t size_limit)
{
    struct timespec time;
    bool expired = true;

    clock_gettime(CLOCK_MONOTONIC, &time);

    if (time.tv_sec - exa->pool_fast_compact_time < 3)
        expired = false;

    if (size_limit && tegra_exa_pools_total_available_space(exa) < size_limit)
        expired = false;

    return expired;
}

static struct tegra_pixmap_pool *
tegra_exa_compact_pools(TegraPtr tegra, size_t size)
{
    struct tegra_exa *exa = tegra->exa;
    bool slow_compact;
    size_t limit;

    if (xorg_list_is_empty(&exa->mem_pools))
        return NULL;

    limit = TEGRA_EXA_POOL_SIZE * 10;

    slow_compact = tegra_exa_slow_compaction_allowed(exa, limit * 3 / 2);
    if (slow_compact)
        tegra_exa_compact_pools_slow(tegra);

    if (tegra_exa_fast_compaction_allowed(exa, size))
        return tegra_exa_compact_pools_fast(exa, size);

    if (slow_compact)
        return tegra_exa_compact_pools_fast(exa, limit);

    return NULL;
}

static void tegra_exa_pixmap_pool_free_entry(struct mem_pool_entry *pool_entry)
{
    struct tegra_pixmap_pool *pool = to_tegra_pool(pool_entry->pool);

    mem_pool_free(pool_entry);

    if (!pool->persistent && mem_pool_empty(&pool->pool))
        tegra_exa_pixmap_pool_destroy(pool);

    pool_entry->pool = NULL;
    pool_entry->id = -1;
}

static int
tegra_exa_pixmap_allocate_from_small_pool(TegraPtr tegra, size_t size,
                                          struct mem_pool_entry *pool_entry)
{
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap_pool *pool;
    bool retried = false;
    size_t pool_size;
    void *data;
    int err;

    if (!tegra->exa_pool_alloc)
        return -EINVAL;

    size = TEGRA_ALIGN(size, TEGRA_EXA_OFFSET_ALIGN);

    if (size > TEGRA_EXA_POOL_SIZE_MAX)
        return -ENOMEM;

    xorg_list_for_each_entry(pool, &exa->mem_pools, entry) {
        data = tegra_exa_pixmap_pool_alloc(exa, pool, size, pool_entry, true);
        if (data)
            goto success;
    }

    if (tegra_exa_pools_total_available_space(exa) >= size)
        pool = tegra_exa_compact_pools(tegra, size);
    else
        pool = NULL;

    if (pool) {
        data = tegra_exa_pixmap_pool_alloc(exa, pool, size, pool_entry, false);
        if (data)
            goto success;
    }

again:
    pool_size = TEGRA_ALIGN(size, TEGRA_EXA_POOL_SIZE);
    err = tegra_exa_pixmap_pool_create(tegra, &pool, 1, pool_size);
    if (err) {
        if (err == -ENOMEM) {
            if (!retried && tegra_exa_slow_compaction_allowed(exa, 0)) {
                tegra_exa_compact_pools_slow(tegra);
                retried = true;
                goto again;
            }
        }

        return err;
    }

    xorg_list_add(&pool->entry, &exa->mem_pools);

    data = tegra_exa_pixmap_pool_alloc(exa, pool, size, pool_entry, true);
    if (!data) {
        ERROR_MSG("FATAL: Failed to allocate from a new pool\n");
        return -ENOMEM;
    }

success:
    return 0;
}

static bool
tegra_exa_pixmap_allocate_from_large_pool_slow(TegraPtr tegra,
                                               struct tegra_pixmap *pixmap,
                                               unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;
    struct mem_pool *pool = &exa->large_pool->pool;
    unsigned int usecs = 30 * 1000 * 1000;
    struct timespec now;
    bool defrag = true;
    bool ret;

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (timespec_diff(&exa->large_pool_last_defrag_time, &now) < usecs ||
        tegra_exa_pool_is_busy(exa, exa->large_pool))
            return false;

    exa->large_pool_last_defrag_time = now;

    tegra_exa_pixmap_pool_map(exa->large_pool);
    ret = !!mem_pool_alloc(pool, size, &pixmap->pool_entry, defrag);
    tegra_exa_pixmap_pool_unmap(exa->large_pool);

    return ret;
}

static bool
tegra_exa_pixmap_allocate_from_large_pool_fast(TegraPtr tegra,
                                               struct tegra_pixmap *pixmap,
                                               unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;

    if (!mem_pool_alloc(&exa->large_pool->pool, size,
                        &pixmap->pool_entry, false))
        return false;

    return true;
}

static int
tegra_exa_pixmap_allocate_from_large_pool(TegraPtr tegra,
                                          struct tegra_pixmap *pixmap,
                                          unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;

    if (!exa->large_pool || size <= TEGRA_EXA_POOL_SIZE_MAX)
        return -EINVAL;

    if (!mem_pool_has_space(&exa->large_pool->pool, size))
        return -ENOMEM;

    if (!tegra_exa_pixmap_allocate_from_large_pool_fast(tegra, pixmap, size) &&
        !tegra_exa_pixmap_allocate_from_large_pool_slow(tegra, pixmap, size))
        return -ENOMEM;

    return 0;
}

static bool
tegra_exa_pixmap_allocate_from_pool(TegraPtr tegra,
                                    struct tegra_pixmap *pixmap,
                                    unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;
    int err;

    if (!pixmap->accel || pixmap->dri)
        return false;

    err = tegra_exa_pixmap_allocate_from_large_pool(tegra, pixmap, size);
    if (!err)
        goto success;

    err = tegra_exa_pixmap_allocate_from_small_pool(tegra, size,
                                                    &pixmap->pool_entry);
    if (err)
        return false;

success:
    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_POOL;

    exa->stats.num_pixmaps_allocations++;
    exa->stats.num_pixmaps_allocations_pool++;
    exa->stats.num_pixmaps_allocations_pool_bytes += size;

    return true;
}

/* vim: set et sts=4 sw=4 ts=4: */
