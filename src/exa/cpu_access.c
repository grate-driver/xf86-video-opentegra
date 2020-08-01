/*
 * Copyright (c) GRATE-DRIVER project
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

static PROFILE_DEF(cpu_access);

static bool tegra_exa_prepare_cpu_access(PixmapPtr pixmap, int idx, void **ptr,
                                         bool cancel_optimizations)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    TegraPtr tegra = TegraPTR(pScrn);
    struct tegra_exa *exa = tegra->exa;
    bool write = false;
    bool accel = false;
    int err;

    FALLBACK_MSG("pixmap %p idx %d type %u %d:%d:%d %p\n",
                 pixmap, idx, priv->type,
                 pixmap->drawable.width,
                 pixmap->drawable.height,
                 pixmap->drawable.bitsPerPixel,
                 pixmap->devPrivate.ptr);

    switch (idx) {
    default:
    case EXA_PREPARE_DEST:
    case EXA_PREPARE_AUX_DEST:
        write = true;
        accel = true;
        break;

        /* fall through */
    case EXA_PREPARE_SRC:
    case EXA_PREPARE_MASK:
    case EXA_PREPARE_AUX_SRC:
    case EXA_PREPARE_AUX_MASK:
    case EXA_NUM_PREPARE_INDICES:
        break;
    }

    tegra_exa_thaw_pixmap(pixmap, accel);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        PROFILE_START(cpu_access);
        *ptr = priv->fallback;
        return true;
    }

    /*
     * EXA doesn't sync for Upload/DownloadFromScreen, assuming that HW
     * will take care of the fencing.
     *
     * Wait for the HW operations to be completed.
     */
    switch (idx) {
    default:
    case EXA_PREPARE_DEST:
    case EXA_PREPARE_AUX_DEST:
        TEGRA_PIXMAP_WAIT_READ_FENCES(priv);

        if (cancel_optimizations) {
            if (priv->state.alpha_0)
                DEBUG_MSG("pixmap %p %s canceled alpha_0\n", pixmap, __func__);

            /*
             * We don't know what fallback will do with pixmap,
             * so assume the worst.
             */
            priv->state.alpha_0 = 0;

            exa->stats.num_cpu_write_accesses++;
        }

        /* fall through */
    case EXA_PREPARE_SRC:
    case EXA_PREPARE_MASK:
    case EXA_PREPARE_AUX_SRC:
    case EXA_PREPARE_AUX_MASK:
    case EXA_NUM_PREPARE_INDICES:
        TEGRA_PIXMAP_WAIT_WRITE_FENCES(priv);

        if (!cancel_optimizations && !write)
            exa->stats.num_cpu_read_accesses++;
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        *ptr = tegra_exa_pixmap_pool_map_entry(&priv->pool_entry);
        PROFILE_START(cpu_access);
        return true;
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        PROFILE_DEF(mmap);
        PROFILE_START(mmap);

        err = drm_tegra_bo_map(priv->bo, ptr);
        if (err < 0) {
            ERROR_MSG("failed to map buffer object: %d\n", err);
            return false;
        }

        PROFILE_STOP(mmap);

        PROFILE_START(cpu_access);
        return true;
    }

    return false;
}

static void tegra_exa_finish_cpu_access(PixmapPtr pixmap, int idx)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    int err;

    PROFILE_STOP(cpu_access);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        err = drm_tegra_bo_unmap(priv->bo);
        if (err < 0)
            ERROR_MSG("failed to unmap buffer object: %d\n", err);
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        tegra_exa_pixmap_pool_unmap_entry(&priv->pool_entry);

    tegra_exa_cool_pixmap(pixmap, true);

    FALLBACK_MSG("pixmap %p idx %d\n", pixmap, idx);
}

/* vim: set et sts=4 sw=4 ts=4: */
