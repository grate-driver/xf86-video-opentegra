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

#define TEGRA_MALLOC_TRIM_THRESHOLD     256

static bool tegra_exa_pixmap_release_data(TegraPtr tegra,
                                          struct tegra_pixmap *priv);

static bool tegra_exa_pixmap_is_from_pool(PixmapPtr pix)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pix);

    return priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL;
}

static unsigned long tegra_exa_pixmap_offset(PixmapPtr pix)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pix);
    unsigned long offset = 0;

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        offset = mem_pool_entry_offset(&priv->pool_entry);

    return offset;
}

static struct drm_tegra_bo *tegra_exa_pixmap_bo(PixmapPtr pix)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pix);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        return to_tegra_pool(priv->pool_entry.pool)->bo;

    return priv->bo;
}

static bool tegra_exa_pixmap_is_offscreen(PixmapPtr pixmap)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);

    return priv && priv->accel && priv->tegra_data;
}

static void tegra_exa_trim_heap(struct tegra_exa *exa)
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

static void tegra_exa_destroy_freelist_pixmap(TegraPtr tegra,
                                              struct tegra_pixmap *priv,
                                              bool wait_fences)
{
    bool released_data;

    if (wait_fences)
        TEGRA_PIXMAP_WAIT_ALL_FENCES(priv);

    released_data = tegra_exa_pixmap_release_data(tegra, priv);
    assert(released_data);

    DEBUG_MSG("priv %p type %u released %d\n",
              priv, priv->type, released_data);

    xorg_list_del(&priv->freelist_entry);
    free(priv);
}

static void tegra_exa_clean_up_pixmaps_freelist(TegraPtr tegra, bool force)
{
    struct tegra_pixmap *pix, *tmp;
    struct tegra_exa *exa = tegra->exa;

    xorg_list_for_each_entry_safe(pix, tmp, &exa->pixmaps_freelist,
                                  freelist_entry) {
        if (force || !tegra_exa_pixmap_is_busy(pix))
            tegra_exa_destroy_freelist_pixmap(tegra, pix, force);
    }
}

static bool
tegra_exa_pixmap_release_data(TegraPtr tegra, struct tegra_pixmap *priv)
{
    bool destroy = !priv->destroyed;
    struct tegra_exa *exa = tegra->exa;

    priv->destroyed = 1;

    if (destroy)
        tegra_exa_cancel_deferred_operations(priv->base);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_NONE) {
        if (priv->frozen) {
#ifdef HAVE_JPEG
            if (priv->compression_type == TEGRA_EXA_COMPRESSION_JPEG)
                tjFree(priv->compressed_data);
            else
#endif
                free(priv->compressed_data);

            priv->frozen = false;
            exa->release_count++;
        }

        goto out_final;
    } else if (tegra->exa_erase_pixmaps && destroy) {
        /* clear released data for privacy protection */
        tegra_exa_clear_pixmap_data(priv, true);
    }

    if (priv->cold) {
        exa->cooling_size -= tegra_exa_pixmap_size(priv);
        xorg_list_del(&priv->fridge_entry);
        priv->cold = false;
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
    if (tegra_exa_pixmap_is_busy(priv)) {
        xorg_list_append(&priv->freelist_entry, &exa->pixmaps_freelist);
        return false;
    }

    TEGRA_PIXMAP_WAIT_ALL_FENCES(priv);

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        tegra_exa_pixmap_pool_free_entry(&priv->pool_entry);
        goto out_final;
    }

    if (priv->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        drm_tegra_bo_unref(priv->bo);
        goto out_final;
    }

out_final:
    priv->type = TEGRA_EXA_PIXMAP_TYPE_NONE;
    tegra_exa_trim_heap(exa);

    return true;
}

static unsigned tegra_exa_pixmap_size_aligned(unsigned pitch, unsigned height,
                                              unsigned bpp)
{
    unsigned int size;

    size = pitch * tegra_height_hw_aligned(height, bpp);

    return TEGRA_ALIGN(size, TEGRA_EXA_OFFSET_ALIGN);
}

static unsigned tegra_exa_pixmap_size(struct tegra_pixmap *pix)
{
    PixmapPtr pixmap = pix->base;

    if (pix->offscreen)
        return tegra_exa_pixmap_size_aligned(pixmap->devKind,
                                             pixmap->drawable.height,
                                             pixmap->drawable.bitsPerPixel);

    return pixmap->devKind * pixmap->drawable.height;
}

static bool tegra_exa_bpp_accel(unsigned bpp)
{
    return bpp == 8 || bpp == 16 || bpp == 32;
}

static bool tegra_exa_pixmap_allocate_data(TegraPtr tegra,
                                           struct tegra_pixmap *pixmap,
                                           unsigned int width,
                                           unsigned int height,
                                           unsigned int bpp,
                                           int usage_hint)
{
    unsigned int pitch = tegra_hw_pitch(width, height, bpp);
    unsigned int size = pitch * height;

    pixmap->tegra_data = true;
    pixmap->accel = tegra_exa_bpp_accel(bpp);

    if (usage_hint == TEGRA_DRI_USAGE_HINT)
        pixmap->dri = true;

    /* DRI allocation must be accelerateable, otherwise what's the point? */
    if (pixmap->dri && !pixmap->accel)
        return false;

    /*
     * Optimize allocation for 1x1 drawable as we will simply always
     * avoid sampling from a such textures.
     */
    if ((!pixmap->dri && width == 1 && height == 1) || !pixmap->accel)
        return tegra_exa_pixmap_allocate_from_sysmem(tegra, pixmap, size);

    if (pixmap->accel) {
        pixmap->offscreen = 1;
        size = tegra_exa_pixmap_size_aligned(pitch, height, bpp);
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
        return true;

    return (tegra_exa_pixmap_allocate_from_pool(tegra, pixmap, size) ||
            tegra_exa_pixmap_allocate_from_bo(tegra, pixmap, size) ||
            tegra_exa_pixmap_allocate_from_sysmem(tegra, pixmap, size));
}

static bool tegra_exa_pixmap_is_busy(struct tegra_pixmap *pixmap)
{
    unsigned int i;

    for (i = 0; i < TEGRA_ENGINES_NUM; i++) {
        if (!TEGRA_FENCE_COMPLETED(pixmap->fence_write[i]) ||
            !TEGRA_FENCE_COMPLETED(pixmap->fence_read[i]))
            return true;
    }

    return false;
}

static void *tegra_exa_create_pixmap(ScreenPtr screen, int width, int height,
                                     int depth, int usage_hint, int bitsPerPixel,
                                     int *new_fb_pitch)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap *pixmap;

    pixmap = calloc(1, sizeof(*pixmap));
    if (!pixmap)
        return NULL;

    switch (bitsPerPixel) {
    case 8:
        pixmap->picture_format = PICT_a8;
        break;

    default:
        break;
    }

    if (width > 0 && height > 0 && bitsPerPixel > 0) {
        *new_fb_pitch = tegra_hw_pitch(width, height, bitsPerPixel);

        if (!tegra_exa_pixmap_allocate_data(tegra, pixmap, width, height,
                                            bitsPerPixel, usage_hint)) {
            free(pixmap);
            return NULL;
        }
    } else {
        *new_fb_pitch = 0;
    }

    DEBUG_MSG("priv %p type %u %d:%d:%d stride %d usage_hint 0x%x (%c%c%c%c)\n",
              pixmap, pixmap->type, width, height, bitsPerPixel, *new_fb_pitch,
              usage_hint, usage_hint >> 24, usage_hint >> 16, usage_hint >> 8, usage_hint);

    exa->stats.num_pixmaps_created++;

    return pixmap;
}

static void tegra_exa_destroy_pixmap(void *driverPriv)
{
    struct tegra_pixmap *pixmap = driverPriv;
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->base->drawable.pScreen);
    TegraPtr tegra = TegraPTR(scrn);
    struct tegra_exa *exa = tegra->exa;
    bool released_data;

    released_data = tegra_exa_pixmap_release_data(tegra, pixmap);

    DEBUG_MSG("pixmap %p priv %p type %u %d:%d:%d stride %d released %d\n",
              pixmap->base, pixmap, pixmap->type,
              pixmap->base->drawable.width,
              pixmap->base->drawable.height,
              pixmap->base->drawable.bitsPerPixel,
              pixmap->base->devKind,
              released_data);

    pixmap->base = NULL;

    if (released_data)
        free(pixmap);

    exa->stats.num_pixmaps_destroyed++;
}

static bool tegra_exa_modify_pixmap_header(PixmapPtr pixmap, int width,
                                           int height, int depth, int bpp,
                                           int devKind, pointer pix_data)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    TegraPtr tegra = TegraPTR(pScrn);
    struct drm_tegra_bo *scanout;
    bool ret;

    ret = miModifyPixmapHeader(pixmap, width, height, depth, bpp,
                               devKind, pix_data);
    if (!ret)
        return false;

    if (pix_data) {
        tegra_exa_pixmap_release_data(tegra, priv);

        if (pix_data == drmmode_map_front_bo(&tegra->drmmode)) {
            scanout = drmmode_get_front_bo(&tegra->drmmode);
            priv->type = TEGRA_EXA_PIXMAP_TYPE_BO;
            priv->bo = drm_tegra_bo_ref(scanout);
            priv->tegra_data = true;
            priv->offscreen = true;
            priv->scanout = true;
            priv->accel = true;
            goto success;
        }

        if (pix_data == drmmode_crtc_map_rotate_bo(pScrn, 0)) {
            scanout = drmmode_crtc_get_rotate_bo(pScrn, 0);
            priv->type = TEGRA_EXA_PIXMAP_TYPE_BO;
            priv->bo = drm_tegra_bo_ref(scanout);
            priv->scanout_rotated = true;
            priv->tegra_data = true;
            priv->offscreen = true;
            priv->scanout = true;
            priv->accel = true;
            priv->crtc = 0;
            goto success;
        }

        if (pix_data == drmmode_crtc_map_rotate_bo(pScrn, 1)) {
            scanout = drmmode_crtc_get_rotate_bo(pScrn, 1);
            priv->type = TEGRA_EXA_PIXMAP_TYPE_BO;
            priv->bo = drm_tegra_bo_ref(scanout);
            priv->scanout_rotated = true;
            priv->tegra_data = true;
            priv->offscreen = true;
            priv->scanout = true;
            priv->accel = true;
            priv->crtc = 1;
            goto success;
        }
    } else if (!priv->accel && priv->tegra_data) {
        /* this tells EXA that this pixmap is unacceleratable */
        pixmap->devPrivate.ptr = priv->fallback;
    }

    priv->base = pixmap;
    tegra_exa_cool_tegra_pixmap(tegra, priv);

success:
    DEBUG_MSG("pixmap %p priv %p type %u %d:%d:%d stride %d %d:%d:%d:%p\n",
              pixmap, priv, priv->type,
              pixmap->drawable.width,
              pixmap->drawable.height,
              pixmap->drawable.bitsPerPixel,
              pixmap->devKind,
              width, height, bpp, pix_data);

    return true;
}

/* vim: set et sts=4 sw=4 ts=4: */
