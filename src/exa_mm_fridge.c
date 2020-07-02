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

#define TEST_FREEZER    0

#define TEGRA_EXA_FREEZE_ALLOWANCE_DELTA    3
#define TEGRA_EXA_FREEZE_BOUNCE_DELTA       5
#define TEGRA_EXA_FREEZE_MIN_DELTA          (60 * 1)
#define TEGRA_EXA_FREEZE_MAX_DELTA          (60 * 5)
#define TEGRA_EXA_COOLING_LIMIT_MIN         (24 * 1024 * 1024)
#define TEGRA_EXA_COOLING_LIMIT_MAX         (32 * 1024 * 1024)
#define TEGRA_EXA_FREEZE_CHUNK              (128 * 1024)
#define TEGRA_EXA_COMPRESS_RATIO_LIMIT      85 / 100
#define TEGRA_EXA_COMPRESS_SMALL_SIZE       (64 * 1024)
#define TEGRA_EXA_RESURRECT_DELTA           2

struct compression_arg {
    unsigned int compression_type;
    unsigned long out_size;
    unsigned long in_size;
    void *buf_out;
    void *buf_in;
    signed format;
    unsigned samping;
    unsigned height;
    unsigned width;
    unsigned pitch;
    unsigned keep_fallback;
    unsigned quality;
};

static int TegraEXAToPNGFormat(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    if (!tegra->exa_compress_png)
        return -1;

#ifdef HAVE_PNG
    switch (pixmap->picture_format) {
    case PICT_a8:
        return PNG_FORMAT_GRAY;

    case PICT_c8:
        return PNG_FORMAT_GRAY;

    case PICT_r8g8b8:
        return PNG_FORMAT_BGR;

    case PICT_b8g8r8:
        return PNG_FORMAT_RGB;

    case PICT_a8r8g8b8:
        return PNG_FORMAT_BGRA;

    case PICT_a8b8g8r8:
        return PNG_FORMAT_RGBA;

    case PICT_b8g8r8a8:
        return PNG_FORMAT_ARGB;

    default:
        break;
    }
#endif

    return -1;
}

static int TegraEXAToJpegTurboFormat(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    if (!tegra->exa_compress_jpeg)
        return -1;

#ifdef HAVE_JPEG
    switch (pixmap->picture_format) {
    case PICT_a8:
        return TJPF_GRAY;

    case PICT_c8:
        return TJPF_GRAY;

    case PICT_r8g8b8:
        return TJPF_BGR;

    case PICT_b8g8r8:
        return TJPF_RGB;

    case PICT_x8r8g8b8:
        return TJPF_BGRX;

    case PICT_x8b8g8r8:
        return TJPF_RGBX;

    case PICT_b8g8r8x8:
        return TJPF_XRGB;

    default:
        break;
    }
#endif

    return -1;
}

static int TegraEXAToJpegTurboSampling(TegraPixmapPtr pixmap)
{
#ifdef HAVE_JPEG
    switch (pixmap->pPixmap->drawable.bitsPerPixel) {
    case 8:
        return TJSAMP_GRAY;

    default:
        return TJSAMP_422;
    }
#endif

    return -1;
}

static void * TegraEXAFridgeMapPixmap(TegraPixmapPtr pixmap)
{
    void *data_ptr;
    int err;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return pixmap->fallback;

    if (pixmap->fence_write)
        TEGRA_EXA_WAIT_AND_PUT_FENCE(pixmap->fence_write);

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

static void TegraEXAFridgeReleaseUncompressedData(TegraEXAPtr exa,
                                                  TegraPixmapPtr pixmap,
                                                  Bool keep_fallback)
{
    switch (pixmap->type) {
    case TEGRA_EXA_PIXMAP_TYPE_FALLBACK:
        if (!keep_fallback) {
            free(pixmap->fallback);
            exa->release_count++;
        }
        break;

    case TEGRA_EXA_PIXMAP_TYPE_POOL:
        TegraEXAPoolFree(&pixmap->pool_entry);
        break;

    case TEGRA_EXA_PIXMAP_TYPE_BO:
        drm_tegra_bo_unref(pixmap->bo);
        break;
    }
}

static void TegraEXAResurrectAccelPixmap(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    void *pixmap_data_orig;
    unsigned int data_size;
    struct timespec time;
    void *pixmap_data;
    TegraEXAPtr exa;
    Bool ret;

    if (!pixmap->accel || !pixmap->offscreen || !pixmap->tegra_data ||
        pixmap->type != TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return;

    exa = tegra->exa;
    clock_gettime(CLOCK_MONOTONIC, &time);

    /* don't retry too often */
    if (time.tv_sec - exa->last_resurrect_time < TEGRA_EXA_RESURRECT_DELTA)
        return;

    pixmap_data_orig = pixmap->fallback;
    data_size = TegraPixmapSize(pixmap);

    ret = (TegraEXAAllocateDRMFromPool(tegra, pixmap, data_size) ||
           TegraEXAAllocateDRM(tegra, pixmap, data_size));

    if (ret == TRUE) {
        pixmap->fence_write = NULL;
        pixmap->fence_read  = NULL;
        pixmap_data = TegraEXAFridgeMapPixmap(pixmap);

        if (!pixmap_data) {
            TegraEXAFridgeReleaseUncompressedData(exa, pixmap, FALSE);
            pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;
        }
    } else {
        exa->last_resurrect_time = time.tv_sec;
        pixmap_data = NULL;
    }

    if (pixmap_data) {
        tegra_memcpy_vfp_aligned_src_cached(pixmap_data, pixmap_data_orig,
                                            data_size);
        TegraEXAFridgeUnMapPixmap(pixmap);
        free(pixmap_data_orig);
        exa->release_count++;
    } else {
        pixmap->fallback = pixmap_data_orig;
    }
}

static int TegraEXACompressPixmap(TegraEXAPtr exa, TegraPixmapPtr pixmap,
                                  struct compression_arg *c)
{
    unsigned long compressed_bound;
    unsigned long compressed_max;
    void *tmp;
    int err;

    if (c->compression_type == TEGRA_EXA_COMPRESSION_UNCOMPRESSED)
        goto uncompressed;

    DebugMsg("priv %p compressing\n", pixmap);

    if (c->in_size > TEGRA_EXA_COMPRESS_SMALL_SIZE)
        compressed_max = c->in_size - TEGRA_EXA_COMPRESS_SMALL_SIZE / 8;
    else
        compressed_max = c->in_size * TEGRA_EXA_COMPRESS_RATIO_LIMIT;

#ifdef HAVE_LZ4
    if (c->compression_type == TEGRA_EXA_COMPRESSION_LZ4) {
        compressed_bound = LZ4_compressBound(c->in_size) + 4096;

        c->buf_out = malloc(compressed_bound);

        if (!c->buf_out) {
            ErrorMsg("failed to allocate buffer for compression of size %lu\n",
                     compressed_bound);
            return -1;
        }

        c->out_size = LZ4_compress_default(c->buf_in, c->buf_out,
                                           c->in_size, compressed_bound);
        if (!c->out_size || c->out_size > compressed_max) {
            free(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DebugMsg("priv %p poor compression\n", pixmap);
            goto uncompressed;
        }

        tmp = realloc(c->buf_out, c->out_size);
        if (tmp)
            c->buf_out = tmp;

        c->compression_type = TEGRA_EXA_COMPRESSION_LZ4;
    }
#endif

#ifdef HAVE_JPEG
    if (c->compression_type == TEGRA_EXA_COMPRESSION_JPEG) {
        err = tjCompress2(exa->jpegCompressor, c->buf_in,
                          c->width, c->pitch, c->height, c->format,
                          (uint8_t **) &c->buf_out, &c->out_size,
                          c->samping, c->quality, TJFLAG_FASTDCT);
        if (err) {
            ErrorMsg("JPEG compression failed\n");
            tjFree(c->buf_out);
            goto uncompressed;
        }

        if (c->out_size > compressed_max) {
            tjFree(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DebugMsg("priv %p poor compression\n", pixmap);
            goto uncompressed;
        }

        c->compression_type = TEGRA_EXA_COMPRESSION_JPEG;
    }
#endif

#ifdef HAVE_PNG
    if (c->compression_type == TEGRA_EXA_COMPRESSION_PNG) {
        png_alloc_size_t png_size;
        png_image png;

        memset(&png, 0, sizeof(png));

        png.version             = PNG_IMAGE_VERSION;
        png.width               = c->width;
        png.height              = c->height;
        png.format              = c->format;
        png.warning_or_error    = PNG_IMAGE_ERROR;

        png_size = PNG_IMAGE_PNG_SIZE_MAX(png);
        c->buf_out = malloc(png_size);

        if (!c->buf_out) {
            ErrorMsg("failed to allocate buffer for PNG compression of size %u\n",
                     png_size);
            return -1;
        }

        err = png_image_write_to_memory(&png, c->buf_out, &png_size, 0,
                                        c->buf_in, c->pitch, NULL);
        if (err == 0) {
            ErrorMsg("PNG compression failed %s\n", png.message);
            free(c->buf_out);
            goto uncompressed;
        }

        if (png_size > compressed_max) {
            free(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DebugMsg("priv %p poor compression\n", pixmap);
            goto uncompressed;
        }

        tmp = realloc(c->buf_out, png_size);
        if (tmp) {
            c->out_size = png_size;
            c->buf_out = tmp;
        } else {
            DebugMsg("priv %p realloc failure\n", pixmap);
            free(c->buf_out);
            goto uncompressed;
        }

        c->compression_type = TEGRA_EXA_COMPRESSION_PNG;
    }
#endif

    DebugMsg("priv %p compressed\n", pixmap);

    return 0;

uncompressed:
    DebugMsg("priv %p going uncompressed\n", pixmap);

    if (c->keep_fallback) {
        /* this is fallback allocation that failed to be compressed */
        c->compression_type = TEGRA_EXA_COMPRESSION_UNCOMPRESSED;
        c->buf_out = c->buf_in;
        c->out_size = c->in_size;
        return 1;
    }

    err = posix_memalign(&c->buf_out, 128, c->in_size);
    if (!err) {
        c->compression_type = TEGRA_EXA_COMPRESSION_UNCOMPRESSED;
        tegra_memcpy_vfp_aligned_dst_cached(c->buf_out, c->buf_in, c->in_size);

        c->out_size = c->in_size;
    }

    if (!c->buf_out) {
        c->compression_type = 0;
        c->out_size = 0;
        return -1;
    }

    return 1;
}

static void TegraEXADecompressPixmap(TegraEXAPtr exa, TegraPixmapPtr pixmap,
                                     struct compression_arg *c)
{
#ifdef HAVE_PNG
    png_image png = { 0 };
#endif

    DebugMsg("priv %p decompressing\n", pixmap);

    switch (c->compression_type) {
    case TEGRA_EXA_COMPRESSION_UNCOMPRESSED:
        tegra_memcpy_vfp_aligned_src_cached(c->buf_out, c->buf_in, c->out_size);
        DebugMsg("priv %p decompressed: uncompressed\n", pixmap);

        /* clear released data for privacy protection */
        memset(c->buf_in, TEST_FREEZER ? 0xffffffff : 0, c->out_size);
        free(c->buf_in);
        break;

#ifdef HAVE_LZ4
    case TEGRA_EXA_COMPRESSION_LZ4:
        LZ4_decompress_fast(c->buf_in, c->buf_out, c->out_size);
        DebugMsg("priv %p decompressed: lz4\n", pixmap);

        free(c->buf_in);
        break;
#endif

#ifdef HAVE_JPEG
    case TEGRA_EXA_COMPRESSION_JPEG:
        tjDecompress2(exa->jpegDecompressor, c->buf_in, c->in_size,
                      c->buf_out, c->width, c->pitch, c->height,
                      c->format, TJFLAG_FASTDCT);
        DebugMsg("priv %p decompressed: jpeg\n", pixmap);

        tjFree(c->buf_in);
        break;
#endif

#ifdef HAVE_PNG
    case TEGRA_EXA_COMPRESSION_PNG:
        png.opaque = NULL;
        png.version = PNG_IMAGE_VERSION;
        png_image_begin_read_from_memory(&png, c->buf_in, c->in_size);
        if (png.warning_or_error)
            ErrorMsg("png error: %s\n", png.message);
        png.format = c->format;
        png_image_finish_read(&png, NULL, c->buf_out, c->pitch, NULL);
        if (png.warning_or_error)
            ErrorMsg("png error: %s\n", png.message);
        DebugMsg("priv %p decompressed: png\n", pixmap);
        break;
#endif
    }
}

static struct compression_arg TegraEXASelectCompression(TegraPtr tegra,
                                                        TegraPixmapPtr pixmap,
                                                        unsigned int data_size,
                                                        void *pixmap_data)
{
    struct compression_arg carg = { 0 };

    carg.compression_type   = TEGRA_EXA_COMPRESSION_UNCOMPRESSED;
    carg.buf_out            = NULL;
    carg.buf_in             = pixmap_data;
    carg.out_size           = 0;
    carg.in_size            = data_size;
    carg.height             = pixmap->pPixmap->drawable.height;
    carg.width              = pixmap->pPixmap->drawable.width;
    carg.pitch              = pixmap->pPixmap->devKind;
    carg.format             = -1;
    carg.keep_fallback      = 0;

    /* don't reallocate if fallback compression fails, out = in */
    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        carg.keep_fallback = 1;

    /* don't compress if failed previously or if size is too small */
    if (pixmap->no_compress || data_size < TEGRA_EXA_OFFSET_ALIGN) {
        DebugMsg("priv %p selected compression: uncompressed\n", pixmap);
        return carg;
    }

    /* JPEG is the preferred compression */
    if (tegra->exa_compress_jpeg) {
        carg.format     = TegraEXAToJpegTurboFormat(tegra, pixmap);
        carg.samping    = TegraEXAToJpegTurboSampling(pixmap);
        carg.quality    = tegra->exa_compress_jpeg_quality;

        if (carg.format > -1) {
            DebugMsg("priv %p selected compression: jpeg\n", pixmap);
            carg.compression_type = TEGRA_EXA_COMPRESSION_JPEG;
            return carg;
        }
    }

    /* select PNG if pixmap's format is unsuitable for JPEG compression */
    if (tegra->exa_compress_png) {
        carg.format = TegraEXAToPNGFormat(tegra, pixmap);

        if (carg.format > -1) {
            DebugMsg("priv %p selected compression: png\n", pixmap);
            carg.compression_type = TEGRA_EXA_COMPRESSION_PNG;
            return carg;
        }
    }

    /* select LZ4 if pixmap's format is unsuitable for PNG / JPEG compression */
    if (tegra->exa_compress_lz4) {
        DebugMsg("priv %p selected compression: lz4\n", pixmap);
        carg.compression_type = TEGRA_EXA_COMPRESSION_LZ4;
    }

    return carg;
}

static void TegraEXAThawPixmapData(TegraPtr tegra, TegraPixmapPtr pixmap,
                                   Bool accel)
{
    TegraEXAPtr exa = tegra->exa;
    struct compression_arg carg;
    unsigned int retries = 0;
    unsigned int data_size;
    uint8_t *pixmap_data;
    Bool ret = FALSE;
    unsigned int i;

    carg.compression_type   = pixmap->compression_type;
    carg.buf_in             = pixmap->compressed_data;
    carg.in_size            = pixmap->compressed_size;
    carg.format             = pixmap->compression_fmt;

    data_size = TegraPixmapSize(pixmap);
    pixmap->fence_write = NULL;
    pixmap->fence_read  = NULL;

retry:
    if (accel || pixmap->accelerated)
        ret = (TegraEXAAllocateDRMFromPool(tegra, pixmap, data_size) ||
               TegraEXAAllocateDRM(tegra, pixmap, data_size));

    if (ret == FALSE) {
        if (carg.compression_type == TEGRA_EXA_COMPRESSION_UNCOMPRESSED) {
            pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;
            pixmap->fallback = carg.buf_in;
            return;
        }

        ret = TegraEXAAllocateMem(pixmap, data_size);
    }

    if (ret == FALSE) {
        if (retries++ > 100)
            ErrorMsg("stuck! size %u\n", data_size);

        usleep(100000);
        goto retry;
    }

    pixmap_data = TegraEXAFridgeMapPixmap(pixmap);
    if (!pixmap_data) {
        ErrorMsg("FATAL: can't restore pixmap data\n");
        return;
    }

    if (TEST_FREEZER) {
        for (i = 0; i < data_size; i++)
            pixmap_data[i] = i + 0x55;
    }

    carg.buf_out            = pixmap_data;
    carg.out_size           = data_size;
    carg.height             = pixmap->pPixmap->drawable.height;
    carg.width              = pixmap->pPixmap->drawable.width;
    carg.pitch              = pixmap->pPixmap->devKind;

    TegraEXADecompressPixmap(exa, pixmap, &carg);

    if (TEST_FREEZER) {
        unsigned matched = 0;

        for (i = 0; i < data_size; i++) {
            if (pixmap_data[i] == ((i + 0x55) & 0xff))
                matched++;
        }

        if (matched > data_size / 2)
            ErrorMsg("priv %p decompression failure!\n", pixmap);
    }

    TegraEXAFridgeUnMapPixmap(pixmap);
}

static int TegraEXAFreezePixmap(TegraPtr tegra, TegraPixmapPtr pixmap)
{
    TegraEXAPtr exa = tegra->exa;
    struct compression_arg carg;
    unsigned int data_size;
    void *pixmap_data;
    int err;

    data_size = TegraPixmapSize(pixmap);

    exa->cooling_size -= data_size;
    xorg_list_del(&pixmap->fridge_entry);
    pixmap->cold = FALSE;

    pixmap_data = TegraEXAFridgeMapPixmap(pixmap);

    if (!pixmap_data) {
        ErrorMsg("failed to map pixmap data\n");
        return -1;
    }

    if (pixmap->fence_read)
        TEGRA_EXA_WAIT_AND_PUT_FENCE(pixmap->fence_read);

    carg = TegraEXASelectCompression(tegra, pixmap, data_size, pixmap_data);
    err = TegraEXACompressPixmap(exa, pixmap, &carg);

    if (err < 0) {
        ErrorMsg("failed to freeze pixmap\n");
        goto fail_unmap;
    }

    if (!err || !carg.keep_fallback) {
        /* clear released data for privacy protection */
        memset(pixmap_data, TEST_FREEZER ? 0xffffffff : 0, data_size);
    }

    pixmap->no_compress = err;

    TegraEXAFridgeReleaseUncompressedData(exa, pixmap, carg.keep_fallback);

    pixmap->compression_type = carg.compression_type;
    pixmap->compressed_data  = carg.buf_out;
    pixmap->compressed_size  = carg.out_size;
    pixmap->compression_fmt  = carg.format;
    pixmap->type             = TEGRA_EXA_PIXMAP_TYPE_NONE;
    pixmap->frozen           = TRUE;

    return 0;

fail_unmap:
    TegraEXAFridgeUnMapPixmap(pixmap);

    return -1;
}

static void TegraEXAFreezePixmaps(TegraPtr tegra, time_t time_sec)
{
    TegraEXAPtr exa = tegra->exa;
    TegraPixmapPtr pix, tmp;
    unsigned long cooling_size;
    unsigned long frost_size = 1;
    Bool emergence = FALSE;
    int err;

    if (TEST_FREEZER)
        goto freeze;

    /* don't bother with freezing until limit is hit */
    if (exa->cooling_size < TEGRA_EXA_COOLING_LIMIT_MIN)
        return;

    /*
     * If last freezing was long time ago, then bounce the allowed freeze
     * time. This avoids immediate freeze-thawing after a period of idling
     * for the pixmaps that has been queued for freeze'ing and gonna be taken
     * out from refrigerator shortly.
     */
    if (time_sec - exa->last_freezing_time > TEGRA_EXA_FREEZE_BOUNCE_DELTA)
        goto out;

    DebugMsg("time_sec %ld last_freezing_time %ld cooling_size %lu\n",
             time_sec, exa->last_freezing_time, exa->cooling_size);

    /*
     * Enforce freezing if there are more than several megabytes of pixmaps
     * pending to be frozen.
     */
    if (exa->cooling_size > TEGRA_EXA_COOLING_LIMIT_MAX)
        emergence = TRUE;

    /* allow freezing only once per couple seconds */
    if (time_sec - exa->last_freezing_time < TEGRA_EXA_FREEZE_ALLOWANCE_DELTA)
        return;

freeze:
    cooling_size = exa->cooling_size;
    frost_size = 0;

    xorg_list_for_each_entry_safe(pix, tmp, &exa->cool_pixmaps, fridge_entry) {
        if (time_sec - pix->last_use < TEGRA_EXA_FREEZE_MAX_DELTA &&
            !TEST_FREEZER)
            break;

        DebugMsg("priv %p last_use %ld stalled\n", pix, pix->last_use);

        /* enforce freezing of staled pixmaps */
        TegraEXAFreezePixmap(tegra, pix);
    }

    xorg_list_for_each_entry_safe(pix, tmp, &exa->cool_pixmaps, fridge_entry) {
        if (time_sec - pix->last_use < TEGRA_EXA_FREEZE_MIN_DELTA)
            break;

        DebugMsg("priv %p last_use %ld cool\n", pix, pix->last_use);

        err = TegraEXAFreezePixmap(tegra, pix);
        if (err)
            break;

        frost_size = cooling_size - exa->cooling_size;

        /*
         * Freeze in 128K chunks to reduce long stalls due to compressing
         * lots of data.
         */
        if (!emergence && frost_size > TEGRA_EXA_FREEZE_CHUNK)
            break;

        /* stop when enough of data is frozen on emergence */
        if (emergence && exa->cooling_size < TEGRA_EXA_COOLING_LIMIT_MAX)
            break;
    }

out:
    if (frost_size) {
        struct timespec time;

        clock_gettime(CLOCK_MONOTONIC, &time);
        exa->last_freezing_time = time.tv_sec;
    }
}

static void TegraEXACoolTegraPixmap(TegraPtr tegra, TegraPixmapPtr pix)
{
    TegraEXAPtr exa = tegra->exa;
    struct timespec time;

    if (pix->frozen || pix->cold || pix->scanout || pix->dri ||
        !pix->accel || !pix->offscreen || !pix->tegra_data ||
        pix->type == TEGRA_EXA_PIXMAP_TYPE_NONE)
        return;

    if (!tegra->exa_refrigerator)
        return;

    clock_gettime(CLOCK_MONOTONIC, &time);

    xorg_list_append(&pix->fridge_entry, &exa->cool_pixmaps);
    pix->last_use = time.tv_sec;
    pix->cold = TRUE;

    exa->cooling_size += TegraPixmapSize(pix);
}

static void TegraEXACoolPixmap(PixmapPtr pPixmap, Bool write)
{
    ScrnInfoPtr pScrn;
    TegraPixmapPtr priv;
    TegraPtr tegra;

    if (pPixmap) {
        pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
        priv  = exaGetPixmapDriverPrivate(pPixmap);
        tegra = TegraPTR(pScrn);

        if (tegra->exa_refrigerator) {
            TegraEXACoolTegraPixmap(tegra, priv);

            if (write)
                priv->no_compress = FALSE;
        }
    }
}

static void TegraEXAClearPixmapDataCPU(TegraPixmapPtr pixmap)
{
    unsigned int size = TegraPixmapSize(pixmap);
    PixmapPtr pix = pixmap->pPixmap;
    void *ptr;

    if (TegraEXAPrepareCPUAccess(pix, EXA_PREPARE_DEST, &ptr)) {
        if (TEST_FREEZER) {
            memset(ptr, 0xff, size);
        } else {
            /* always zero-fill allocated data for consistency */
            memset(ptr, 0, size);
        }

        TegraEXAFinishCPUAccess(pix, EXA_PREPARE_DEST);
    }
}

static Bool TegraEXAClearPixmapDataGPU(TegraPixmapPtr pixmap)
{
    if (TegraEXAPrepareSolid(pixmap->pPixmap, GXcopy, FB_ALLONES,
                             TEST_FREEZER ? 0xffffffff : 0)) {
        TegraEXASolid(pixmap->pPixmap, 0, 0,
                      pixmap->pPixmap->drawable.width,
                      pixmap->pPixmap->drawable.height);
        TegraEXADoneSolid(pixmap->pPixmap);

        return TRUE;
    }

    return FALSE;
}

static Bool TegraEXAClearPixmapDataOnGPU(TegraPixmapPtr pixmap, Bool accel)
{
    struct drm_tegra_bo *bo = NULL;
    TegraPixmapPoolPtr pool;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return FALSE;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO)
        bo = pixmap->bo;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        pool = TEGRA_CONTAINER_OF(pixmap->pool_entry.pool, TegraPixmapPool,
                                  pool);
        bo = pool->bo;
    }

    /*
     * HW job execution overhead is bigger for small pixmaps than clearing
     * on CPU, so we prefer CPU for a such pixmaps.
     */
    if (pixmap->pPixmap->drawable.width *
        pixmap->pPixmap->drawable.height *
        pixmap->pPixmap->drawable.bitsPerPixel / 8 < 128 * 1024 &&
        (!accel || drm_tegra_bo_mapped(bo)))
        return FALSE;

    return TRUE;
}

static void TegraEXAClearPixmapData(TegraPixmapPtr pixmap, Bool accel)
{
    if (!TegraEXAClearPixmapDataOnGPU(pixmap, accel) ||
        !TegraEXAClearPixmapDataGPU(pixmap))
            TegraEXAClearPixmapDataCPU(pixmap);
}

static void
TegraEXAAllocatePixmapDataNoFail(TegraPtr tegra, TegraPixmapPtr pixmap,
                                 Bool accel)
{
    unsigned int size = TegraPixmapSize(pixmap);
    unsigned int retries = 0;

    while (1) {
        if (!accel) {
            if (TegraEXAAllocateMem(pixmap, size))
                return;
        } else {
            if (TegraEXAAllocateDRMFromPool(tegra, pixmap, size) ||
                TegraEXAAllocateDRM(tegra, pixmap, size) ||
                TegraEXAAllocateMem(pixmap, size))
                return;
        }

        if (retries++ > 100)
            ErrorMsg("stuck! size %u\n", size);

        usleep(100000);
    }
}

static void TegraEXAThawPixmap(PixmapPtr pPixmap, Bool accel)
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

        priv->accelerated |= accel;

        if (!tegra->exa_refrigerator)
            return;

        if (priv->frozen) {
            TegraEXAThawPixmapData(tegra, priv, accel);
            priv->accelerated = accel;
            priv->frozen = FALSE;
            return;
        }

        if (priv->cold) {
            exa->cooling_size -= TegraPixmapSize(priv);
            xorg_list_del(&priv->fridge_entry);
            priv->cold = FALSE;
        }

        if (accel)
            TegraEXAResurrectAccelPixmap(tegra, priv);

        if (priv->type == TEGRA_EXA_PIXMAP_TYPE_NONE && priv->tegra_data)
            TegraEXAAllocatePixmapDataNoFail(tegra, priv, accel);
    }
}
