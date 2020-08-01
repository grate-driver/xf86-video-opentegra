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

static bool tegra_exa_pixmap_allocate_from_bo(TegraPtr tegra,
                                              struct tegra_pixmap * pixmap,
                                              unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;
    unsigned long flags;
    int drm_ver;
    int err;

    if (!pixmap->accel && !pixmap->dri)
        return false;

    flags = exa->default_drm_bo_flags;

    drm_ver = drm_tegra_version(tegra->drm);
    if (drm_ver >= GRATE_KERNEL_DRM_VERSION)
        flags |= DRM_TEGRA_GEM_CREATE_SPARSE;

    err = drm_tegra_bo_new(&pixmap->bo, tegra->drm, flags, size);
    if (err)
        return false;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_BO;

    exa->stats.num_pixmaps_allocations++;
    exa->stats.num_pixmaps_allocations_bo++;
    exa->stats.num_pixmaps_allocations_bo_bytes += size;

    if (drm_tegra_bo_reused_from_cache(pixmap->bo)) {
        exa->stats.num_pixmaps_allocations_bo_reused++;
        exa->stats.num_pixmaps_allocations_bo_reused_bytes += size;
    }

    return true;
}

static bool tegra_exa_pixmap_allocate_from_sysmem(TegraPtr tegra,
                                                  struct tegra_pixmap * pixmap,
                                                  unsigned int size)
{
    struct tegra_exa *exa = tegra->exa;
    int err;

    if (pixmap->dri)
        return false;

    err = posix_memalign(&pixmap->fallback, 128, size);
    if (err)
        return false;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;

    exa->stats.num_pixmaps_allocations++;
    exa->stats.num_pixmaps_allocations_fallback++;
    exa->stats.num_pixmaps_allocations_fallback_bytes += size;

    return true;
}

static int tegra_exa_init_mm(TegraPtr tegra, struct tegra_exa *exa)
{
    bool has_iommu = false;
    int drm_ver;

    drm_ver = drm_tegra_version(tegra->drm);

    xorg_list_init(&exa->pixmaps_freelist);
    xorg_list_init(&exa->cool_pixmaps);
    xorg_list_init(&exa->mem_pools);

#ifdef HAVE_JPEG
    if (tegra->exa_compress_jpeg) {
        exa->jpegCompressor = tjInitCompress();
        exa->jpegDecompressor = tjInitDecompress();
    }
#endif

    if (drm_ver >= GRATE_KERNEL_DRM_VERSION + 4) {
        has_iommu = drm_tegra_channel_has_iommu(exa->gr2d) &&
                    drm_tegra_channel_has_iommu(exa->gr3d);

        INFO_MSG2("Tegra DRM uses IOMMU: %s\n", has_iommu ? "YES" : "NO");
    }

    /*
     * CMA doesn't guarantee contiguous allocations. We should do our best
     * in order to avoid fragmentation because even if CMA area is quite
     * large, the accidental pinned memory pages may ruin the day (or Xorg
     * session at least).
     */
    if (tegra->exa_pool_alloc) {
        unsigned int size;
        int err = -ENOMEM;

        if (err) {
            size = 24 * 1024 * 1024;
            err = tegra_exa_pixmap_pool_create(tegra, &exa->large_pool, 4, size);
            if (err)
                ERROR_MSG("failed to preallocate %uMB for a larger pool\n",
                          size / (1024 * 1024));
        }

        if (err) {
            size = 16 * 1024 * 1024;
            err = tegra_exa_pixmap_pool_create(tegra, &exa->large_pool, 4, size);
            if (err)
                ERROR_MSG("failed to preallocate %uMB for a larger pool\n",
                          size / (1024 * 1024));
        }

        if (err) {
            size = 8 * 1024 * 1024;
            err = tegra_exa_pixmap_pool_create(tegra, &exa->large_pool, 4, size);
            if (err)
                ERROR_MSG("failed to preallocate %uMB for a larger pool\n",
                          size / (1024 * 1024));
        }

        if (!err) {
            INFO_MSG2("EXA %uMB pool preallocated\n", size / (1024 * 1024));
            exa->large_pool->persistent = true;
        }
    }

    return 0;
}

static void tegra_exa_release_mm(TegraPtr tegra, struct tegra_exa *exa)
{
    tegra_exa_clean_up_pixmaps_freelist(tegra, true);

    if (exa->large_pool) {
        exa->large_pool->persistent = false;

        if (mem_pool_empty(&exa->large_pool->pool)) {
            tegra_exa_pixmap_pool_destroy(exa->large_pool);
            exa->large_pool = NULL;
        }
    }

#ifdef HAVE_JPEG
    if (tegra->exa_compress_jpeg) {
        tjDestroy(exa->jpegDecompressor);
        tjDestroy(exa->jpegCompressor);
    }
#endif

    if (!xorg_list_is_empty(&exa->mem_pools))
        ERROR_MSG("FATAL: Memory leak! Unreleased memory pools\n");

    if (!xorg_list_is_empty(&exa->cool_pixmaps))
        ERROR_MSG("FATAL: Memory leak! Cooled pixmaps\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
