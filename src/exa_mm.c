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

static Bool TegraEXAAllocateDRM(TegraPtr tegra,
                                TegraPixmapPtr pixmap,
                                unsigned int size)
{
    TegraEXAPtr exa = tegra->exa;
    unsigned long flags;
    int drm_ver;
    int err;

    if (!pixmap->accel && !pixmap->dri)
        return FALSE;

    flags = exa->default_drm_bo_flags;

    drm_ver = drm_tegra_version(tegra->drm);
    if (drm_ver >= GRATE_KERNEL_DRM_VERSION)
        flags |= DRM_TEGRA_GEM_CREATE_SPARSE;

    err = drm_tegra_bo_new(&pixmap->bo, tegra->drm, flags, size);
    if (err)
        return FALSE;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_BO;

    return TRUE;
}

static Bool TegraEXAAllocateMem(TegraPixmapPtr pixmap, unsigned int size)
{
    int err;

    if (pixmap->dri)
        return FALSE;

    err = posix_memalign(&pixmap->fallback, 128, size);
    if (err)
        return FALSE;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;

    return TRUE;
}

static int TegraEXAInitMM(TegraPtr tegra, TegraEXAPtr exa)
{
    Bool has_iommu = FALSE;
    int drm_ver;

    drm_ver = drm_tegra_version(tegra->drm);

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

        xf86DrvMsg(-1, X_INFO, "Tegra DRM uses IOMMU: %s\n",
                   has_iommu ? "YES" : "NO");
    }

    /*
     * CMA doesn't guarantee contiguous allocations. We should do our best
     * in order to avoid fragmentation because even if CMA area is quite
     * large, the accidental pinned memory pages may ruin the day (or Xorg
     * session at least).
     */
    if (tegra->exa_pool_alloc && !has_iommu) {
        unsigned int size;
        int err = -ENOMEM;

        if (err) {
            size = 24 * 1024 * 1024;
            err = TegraEXACreatePool(tegra, &exa->large_pool, 4, size);
            if (err)
                ErrorMsg("failed to preallocate %uMB for a larger pool\n",
                        size / (1024 * 1024));
        }

        if (err) {
            size = 16 * 1024 * 1024;
            err = TegraEXACreatePool(tegra, &exa->large_pool, 4, size);
            if (err)
                ErrorMsg("failed to preallocate %uMB for a larger pool\n",
                        size / (1024 * 1024));
        }

        if (err) {
            size = 8 * 1024 * 1024;
            err = TegraEXACreatePool(tegra, &exa->large_pool, 4, size);
            if (err)
                ErrorMsg("failed to preallocate %uMB for a larger pool\n",
                        size / (1024 * 1024));
        }

        if (!err) {
            xf86DrvMsg(-1, X_INFO,
                       "EXA %uMB pool preallocated\n", size / (1024 * 1024));
            exa->large_pool->persitent = TRUE;
        }
    }

    return 0;
}

static void TegraEXAReleaseMM(TegraPtr tegra, TegraEXAPtr exa)
{
    if (exa->large_pool) {
        exa->large_pool->persitent = FALSE;

        if (mem_pool_empty(&exa->large_pool->pool)) {
            TegraEXADestroyPool(exa->large_pool);
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
        ErrorMsg("FATAL: Memory leak! Unreleased memory pools\n");

    if (!xorg_list_is_empty(&exa->cool_pixmaps))
        ErrorMsg("FATAL: Memory leak! Cooled pixmaps\n");
}
