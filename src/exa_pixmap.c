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

#include "driver.h"

#define TEGRA_MALLOC_TRIM_THRESHOLD     256

static Bool TegraEXAReleasePixmapData(TegraPtr tegra, TegraPixmapPtr priv);

static Bool TegraEXAIsPoolPixmap(PixmapPtr pix)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pix);

    return priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL;
}

static unsigned long TegraEXAPixmapOffset(PixmapPtr pix)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pix);
    unsigned long offset = 0;

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        offset = mem_pool_entry_offset(&priv->pool_entry);

    return offset;
}

static struct drm_tegra_bo * TegraEXAPixmapBO(PixmapPtr pix)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pix);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        TegraPixmapPoolPtr pool = TEGRA_CONTAINER_OF(
                    priv->pool_entry.pool, TegraPixmapPool, pool);
        return pool->bo;
    }

    return priv->bo;
}

static Bool TegraEXAPixmapIsOffscreen(PixmapPtr pPix)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPix);

    return priv && priv->accel && priv->tegra_data;
}

static void TegraEXATrimHeap(TegraEXAPtr exa)
{
    /*
     * Default trimming threshold isn't good for us, that results in
     * a big amounts of wasted memory due to high fragmentation. Hence
     * manually enforce trimming of the heap when it makes sense.
     */
#ifdef __GLIBC__
    if (exa->release_count > TEGRA_MALLOC_TRIM_THRESHOLD) {
        exa->release_count = 0;
        malloc_trim(0);
    }
#endif
}

static void TegraEXADestroyFreelistPixmap(TegraPtr tegra, TegraPixmapPtr priv,
                                          Bool wait_fences)
{
    Bool released_data;

    if (wait_fences)
        TEGRA_PIXMAP_WAIT_ALL_FENCES(priv);

    released_data = TegraEXAReleasePixmapData(tegra, priv);
    assert(released_data);

    DebugMsg("pPix %p priv %p type %u %d:%d:%d stride %d released %d\n",
             priv->pPixmap, priv, priv->type,
             priv->pPixmap->drawable.width,
             priv->pPixmap->drawable.height,
             priv->pPixmap->drawable.bitsPerPixel,
             priv->pPixmap->devKind,
             released_data);

    xorg_list_del(&priv->freelist_entry);
    free(priv);
}

static void TegraEXACleanUpPixmapsFreelist(TegraPtr tegra, Bool force)
{
    TegraEXAPtr exa = tegra->exa;
    TegraPixmapPtr pix, tmp;

    xorg_list_for_each_entry_safe(pix, tmp, &exa->pixmaps_freelist,
                                  freelist_entry) {
        if (force || !TegraEXAPixmapBusy(pix))
            TegraEXADestroyFreelistPixmap(tegra, pix, force);
    }
}

static Bool TegraEXAReleasePixmapData(TegraPtr tegra, TegraPixmapPtr priv)
{
    Bool destroy = !priv->destroyed;
    TegraEXAPtr exa = tegra->exa;

    priv->destroyed = 1;

    if (destroy)
        tegra_exa_cancel_deferred_operations(priv->pPixmap);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_NONE) {
        if (priv->frozen) {
#ifdef HAVE_JPEG
            if (priv->compression_type == TEGRA_EXA_COMPRESSION_JPEG)
                tjFree(priv->compressed_data);
            else
#endif
                free(priv->compressed_data);

            priv->frozen = FALSE;
            exa->release_count++;
        }

        goto out_final;
    } else if (tegra->exa_erase_pixmaps && destroy) {
        /* clear released data for privacy protection */
        TegraEXAClearPixmapData(priv, TRUE);
    }

    if (priv->cold) {
        exa->cooling_size -= TegraPixmapSize(priv);
        xorg_list_del(&priv->fridge_entry);
        priv->cold = FALSE;
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        free(priv->fallback);
        exa->release_count++;
        goto out_final;
    }

    /*
     * Pool allocation data is sprayed with 0x88 if canary-debugging is
     * enabled, see mem_pool_free(). In this case we need to enforce
     * the fence-waiting, otherwise there will be visible glitches if
     * pixmap is released before GPU rendering is finished.
     *
     * One example where problem is visible is a "magnus" application of
     * MATE DE, click on the magnus itself to see the corrupted image of
     * the checkerboard background that app draws.
     *
     * Secondly, we can't check the pool allocation HW usage status at
     * allocation time, so we always need to fence a such allocation.
     *
     * Thirdly, we have to await the fence to avoid BO re-use while job is
     * in progress,  this will be resolved by BO reservation that right now
     * isn't supported by vanilla upstream kernel driver.
     *
     * If hardware is working with pixmap, then we won't free the pixmap,
     * but move it to a freelist where it will sit until hardware is done
     * working with the pixmap, then pixmap will be released.
     */
    if (TegraEXAPixmapBusy(priv)) {
        xorg_list_append(&priv->freelist_entry, &exa->pixmaps_freelist);
        return FALSE;
    }

    TEGRA_PIXMAP_WAIT_ALL_FENCES(priv);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        TegraEXAPoolFree(&priv->pool_entry);
        goto out_final;
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        drm_tegra_bo_unref(priv->bo);
        goto out_final;
    }

out_final:
    priv->type = TEGRA_EXA_PIXMAP_TYPE_NONE;
    TegraEXATrimHeap(exa);

    return TRUE;
}

static unsigned TegraEXAPixmapSizeAligned(unsigned pitch, unsigned height,
                                          unsigned bpp)
{
    unsigned int size;

    size = pitch * TegraEXAHeightHwAligned(height, bpp);

    return TEGRA_ALIGN(size, TEGRA_EXA_OFFSET_ALIGN);
}

static unsigned TegraPixmapSize(TegraPixmapPtr pixmap)
{
    PixmapPtr pPixmap = pixmap->pPixmap;

    if (pixmap->offscreen)
        return TegraEXAPixmapSizeAligned(pPixmap->devKind,
                                         pPixmap->drawable.height,
                                         pPixmap->drawable.bitsPerPixel);

    return pPixmap->devKind * pPixmap->drawable.height;
}

static Bool TegraEXAAccelerated(unsigned bpp)
{
    return bpp == 8 || bpp == 16 || bpp == 32;
}

static Bool TegraEXAAllocatePixmapData(TegraPtr tegra,
                                       TegraPixmapPtr pixmap,
                                       unsigned int width,
                                       unsigned int height,
                                       unsigned int bpp,
                                       int usage_hint)
{
    unsigned int pitch = TegraEXAPitch(width, height, bpp);
    unsigned int size = pitch * height;

    pixmap->tegra_data = TRUE;
    pixmap->accel = TegraEXAAccelerated(bpp);

    if (usage_hint == TEGRA_DRI_USAGE_HINT)
        pixmap->dri = TRUE;

    /* DRI allocation must be accelerateable, otherwise what's the point? */
    if (pixmap->dri && !pixmap->accel)
        return FALSE;

    /*
     * Optimize allocation for 1x1 drawable as we will simply always
     * avoid sampling from a such textures.
     */
    if ((!pixmap->dri && width == 1 && height == 1) || !pixmap->accel)
        return TegraEXAAllocateMem(pixmap, size);

    if (pixmap->accel) {
        pixmap->offscreen = 1;
        size = TegraEXAPixmapSizeAligned(pitch, height, bpp);
    }

    /*
     * Allocation is deferred to TegraEXAThawPixmap() invocation
     * because there is no point to allocate BO if pixmap won't
     * be ever used for accelerated drawing. A set usage_hint
     * usually means that we really want to allocate data right
     * now, this will also bypass data-zeroing that is performed
     * for deferred allocations and shouldn't be needed for the
     * internal use.
     */
    if (!usage_hint && tegra->exa_refrigerator)
        return TRUE;

    return (TegraEXAAllocateDRMFromPool(tegra, pixmap, size) ||
            TegraEXAAllocateDRM(tegra, pixmap, size) ||
            TegraEXAAllocateMem(pixmap, size));
}

static Bool TegraEXAPixmapBusy(TegraPixmapPtr pixmap)
{
    unsigned int i;

    for (i = 0; i < TEGRA_ENGINES_NUM; i++) {
        if (!TEGRA_FENCE_COMPLETED(pixmap->fence_write[i]) ||
            !TEGRA_FENCE_COMPLETED(pixmap->fence_read[i]))
                return TRUE;
    }

    return FALSE;
}
