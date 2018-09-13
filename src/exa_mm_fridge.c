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

#define TEGRA_EXA_FREEZE_ALLOWANCE_DELTA    1
#define TEGRA_EXA_FREEZE_BOUNCE_DELTA       3
#define TEGRA_EXA_FREEZE_DELTA              2
#define TEGRA_EXA_COOLING_LIMIT_MIN         0x400000
#define TEGRA_EXA_COOLING_LIMIT_MAX         0x1000000
#define TEGRA_EXA_FREEZE_CHUNK              0x1000000
#define TEGRA_EXA_COMPRESS_RATIO_LIMIT      15 / 100
#define TEGRA_EXA_PAGE_SIZE                 0x10000
#define TEGRA_EXA_RESURRECT_DELTA           2

static void * TegraEXAFridgeMapPixmap(TegraPixmapPtr pixmap)
{
    void *data_ptr;
    int err;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return pixmap->fallback;

    if (pixmap->fence) {
        TegraEXAWaitFence(pixmap->fence);

        tegra_stream_put_fence(pixmap->fence);
        pixmap->fence = NULL;
    }

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        return mem_pool_entry_addr(&pixmap->pool_entry);

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        err = drm_tegra_bo_map(pixmap->bo, &data_ptr);
        if (!err)
            return data_ptr;

        return NULL;
    }

    ErrorMsg("FATAL: invalid pixmap type\n");

    return NULL;
}

static void TegraEXAFridgeUnMapPixmap(TegraPixmapPtr pixmap)
{
    int err;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        err = drm_tegra_bo_unmap(pixmap->bo);
        if (err < 0)
            ErrorMsg("FATAL: failed to unmap buffer object: %d\n", err);
    }
}

static void TegraEXAResurrectAccelPixmap(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    void *pixmap_data_orig;
    struct timespec time;
    void *pixmap_data;
    TegraEXAPtr exa;
    Bool ret;

    if (!pixmap->accel || pixmap->type != TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return;

    exa = tegra->exa;
    clock_gettime(CLOCK_MONOTONIC, &time);

    /* don't retry too often */
    if (time.tv_sec - exa->last_resurrect_time < TEGRA_EXA_RESURRECT_DELTA)
        return;

    pixmap_data_orig = pixmap->fallback;

    ret = (TegraEXAAllocateDRMFromPool(tegra, pixmap, pixmap->data_size) ||
           TegraEXAAllocateDRM(tegra, pixmap, pixmap->data_size));

    if (ret == TRUE) {
        pixmap->fence = NULL;
        pixmap_data = TegraEXAFridgeMapPixmap(pixmap);
    } else {
        exa->last_resurrect_time = time.tv_sec;
        pixmap_data = NULL;
    }

    if (pixmap_data) {
        memcpy(pixmap_data, pixmap_data_orig, pixmap->data_size);
        TegraEXAFridgeUnMapPixmap(pixmap);
        free(pixmap_data_orig);
    } else {
        pixmap->fallback = pixmap_data_orig;
    }
}

static void TegraEXAThawPixmapData(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    unsigned int data_size;
    void *compressed_data;
    void *pixmap_data;
    int bytes;
    Bool ret;

    compressed_data = pixmap->compressed_data;
    data_size = pixmap->data_size;

    pixmap->fence = NULL;

retry:
    ret = (TegraEXAAllocateDRMFromPool(tegra, pixmap, data_size) ||
           TegraEXAAllocateDRM(tegra, pixmap, data_size) ||
           TegraEXAAllocateMem(pixmap, data_size));

    if (ret == FALSE) {
        if (!pixmap->no_compress) {
            usleep(100000);
            goto retry;
        }

        pixmap->fallback = compressed_data;
        pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;
        return;
    }

    pixmap_data = TegraEXAFridgeMapPixmap(pixmap);
    if (!pixmap_data) {
        ErrorMsg("FATAL: can't restore pixmap data\n");
        goto out;
    }

    if (pixmap->no_compress) {
        memcpy(pixmap_data, compressed_data, data_size);
        bytes = data_size;
    } else {
        bytes = LZ4_decompress_fast(compressed_data, pixmap_data, data_size);
    }

    TegraEXAFridgeUnMapPixmap(pixmap);

    if (bytes < 0) {
        ErrorMsg("FATAL: failed to decompress pixmap data\n");
        goto out;
    }

out:
    free(compressed_data);
}

static void TegraEXAFridgeReleaseUncompressedData(TegraPixmapPtr pixmap)
{
    switch (pixmap->type) {
    case TEGRA_EXA_PIXMAP_TYPE_FALLBACK:
        free(pixmap->fallback);
        break;

    case TEGRA_EXA_PIXMAP_TYPE_POOL:
        TegraEXAPoolFree(&pixmap->pool_entry);
        break;

    case TEGRA_EXA_PIXMAP_TYPE_BO:
        drm_tegra_bo_unref(pixmap->bo);
        break;
    }
}

static void TegraEXAFreezePixmap(TegraEXAPtr exa, TegraPixmapPtr pixmap)
{
    unsigned int compressed_size;
    unsigned int compressed_max;
    void *compressed_data_tmp;
    void *compressed_data;
    void *pixmap_data;

    exa->cooling_size -= pixmap->data_size;
    xorg_list_del(&pixmap->fridge_entry);
    pixmap->cold = FALSE;

    if (pixmap->no_compress &&
        pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return;

    pixmap_data = TegraEXAFridgeMapPixmap(pixmap);
    if (!pixmap_data) {
        ErrorMsg("failed to map pixmap data\n");
        return;
    }

    if (pixmap->no_compress)
        goto no_compress;

    if (pixmap->data_size > TEGRA_EXA_PAGE_SIZE)
        compressed_max = pixmap->data_size - TEGRA_EXA_PAGE_SIZE / 8;
    else
        compressed_max = pixmap->data_size -
                         pixmap->data_size * TEGRA_EXA_COMPRESS_RATIO_LIMIT;

    compressed_data_tmp = malloc(compressed_max);

    if (!compressed_data_tmp) {
        ErrorMsg("failed to allocate buffer for compressed data of size %u",
                 compressed_max);
        goto fail_unmap;
    }

    compressed_size = LZ4_compress_default(pixmap_data,
                                           compressed_data_tmp,
                                           pixmap->data_size,
                                           compressed_max);
    if (!compressed_size) {
        free(compressed_data_tmp);
        pixmap->no_compress = TRUE;

        if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
            goto fail_unmap;

no_compress:
        /* just swap out poorly compressed pixmap from CMA */
        compressed_data = malloc(pixmap->data_size);
        compressed_size = pixmap->data_size;

        if (compressed_data) {
            memcpy(compressed_data, pixmap_data, pixmap->data_size);
            goto out_success;
        }

        goto fail_unmap;
    }

    /* don't bother with reallocation if estimated size was okay */
    if (compressed_max - compressed_size < 512) {
        compressed_data = compressed_data_tmp;
        goto out_success;
    }

    compressed_data = realloc(compressed_data_tmp, compressed_size);

    if (!compressed_data)
        compressed_data = compressed_data_tmp;

out_success:
    TegraEXAFridgeReleaseUncompressedData(pixmap);

    pixmap->compressed_data = compressed_data;
    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_NONE;
    pixmap->frozen = TRUE;
    return;

fail_unmap:
    TegraEXAFridgeUnMapPixmap(pixmap);
}

static void TegraEXAFreezePixmaps(TegraEXAPtr exa, time_t time)
{
    TegraPixmapPtr pix, tmp;
    unsigned long cooling_size;
    unsigned long frost_size = 1;
    Bool emergence = FALSE;

    /* don't bother with freezing until limit is hit */
    if (exa->cooling_size < TEGRA_EXA_COOLING_LIMIT_MIN)
        return;

    /*
     * Enforce freezing if there are more than several megabytes of pixmaps
     * pending to be frozen.
     */
    if (exa->cooling_size > TEGRA_EXA_COOLING_LIMIT_MAX) {
        emergence = TRUE;
        goto freeze;
    }

    /*
     * If last freezing was long time ago, then bounce the allowed freeze
     * time. This avoids immediate freeze-thawing after a period of idling
     * for the pixmaps that has been queued for freeze'ing and gonna be taken
     * out from refrigerator shortly.
     */
    if (time - exa->last_freezing_time > TEGRA_EXA_FREEZE_BOUNCE_DELTA)
        goto out;

    /* allow freezing only once per couple seconds */
    if (time - exa->last_freezing_time < TEGRA_EXA_FREEZE_ALLOWANCE_DELTA)
        return;

freeze:
    cooling_size = exa->cooling_size;
    frost_size = 0;

    xorg_list_for_each_entry_safe(pix, tmp, &exa->cool_pixmaps, fridge_entry) {
        if (!emergence && (time / 8 - pix->last_use < TEGRA_EXA_FREEZE_DELTA))
            break;

        TegraEXAFreezePixmap(exa, pix);

        frost_size = cooling_size - exa->cooling_size;

        /*
         * Freeze in 64K chunks to reduce long stalls due to compressing
         * lots of data.
         */
        if (!emergence && frost_size > TEGRA_EXA_FREEZE_CHUNK)
            break;

        /* stop when enough of data is frozen on emergence */
        if (emergence && exa->cooling_size < TEGRA_EXA_COOLING_LIMIT_MAX)
            break;
    }

out:
    if (frost_size)
        exa->last_freezing_time = time;
}

void TegraEXACoolTegraPixmap(TegraEXAPtr exa, TegraPixmapPtr pix)
{
    unsigned int current_sec8;
    struct timespec time;

    if (pix->frozen || pix->cold || pix->scanout || pix->dri || !pix->accel)
        return;

    clock_gettime(CLOCK_MONOTONIC, &time);
    current_sec8 = time.tv_sec / 8;

    TegraEXAFreezePixmaps(exa, time.tv_sec);

    xorg_list_append(&pix->fridge_entry, &exa->cool_pixmaps);
    pix->last_use = current_sec8;
    pix->cold = TRUE;

    exa->cooling_size += pix->data_size;
}

void TegraEXACoolPixmap(PixmapPtr pPixmap, Bool write)
{
    ScrnInfoPtr pScrn;
    TegraPixmapPtr priv;
    TegraEXAPtr exa;

    if (pPixmap) {
        pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
        priv  = exaGetPixmapDriverPrivate(pPixmap);
        exa   = TegraPTR(pScrn)->exa;

        TegraEXACoolTegraPixmap(exa, priv);

        if (write)
            priv->no_compress = FALSE;
    }
}

void TegraEXAThawPixmap(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn;
    TegraPixmapPtr priv;
    TegraEXAPtr exa;
    TegraPtr tegra;

    if (pPixmap) {
        pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
        priv  = exaGetPixmapDriverPrivate(pPixmap);
        tegra = TegraPTR(pScrn);
        exa   = tegra->exa;

        if (priv->frozen) {
            TegraEXAThawPixmapData(tegra, priv);
            priv->frozen = FALSE;
            return;
        }

        if (priv->cold) {
            exa->cooling_size -= priv->data_size;
            xorg_list_del(&priv->fridge_entry);
            priv->cold = FALSE;
        }

        TegraEXAResurrectAccelPixmap(tegra, priv);
    }
}
