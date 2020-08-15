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

#define TEST_FREEZER                        0

/* note: validation is very slow */
#define VALIDATE_COMPRESSION                0

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

static int tegra_exa_to_png_format(TegraPtr tegra, struct tegra_pixmap *pixmap)
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

static int
tegra_exa_to_jpeg_turbo_format(TegraPtr tegra, struct tegra_pixmap *pixmap)
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

static int tegra_exa_to_jpeg_turbo_sampling(struct tegra_pixmap *pixmap)
{
#ifdef HAVE_JPEG
    switch (pixmap->base->drawable.bitsPerPixel) {
    case 8:
        return TJSAMP_GRAY;

    default:
        return TJSAMP_422;
    }
#endif

    return -1;
}

static void * tegra_exa_mm_fridge_map_pixmap(struct tegra_pixmap *pixmap)
{
    void *data_ptr;
    int err;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return pixmap->fallback;

    tegra_exa_flush_deferred_operations(pixmap->base, false, true, true);

    TEGRA_PIXMAP_WAIT_ALL_FENCES(pixmap);

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        return tegra_exa_pixmap_pool_map_entry(&pixmap->pool_entry);

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        err = drm_tegra_bo_map(pixmap->bo, &data_ptr);
        if (!err)
            return data_ptr;

        return NULL;
    }

    ERROR_MSG("FATAL: invalid pixmap type\n");

    return NULL;
}

static void tegra_exa_mm_fridge_unmap_pixmap(struct tegra_pixmap *pixmap)
{
    int err;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO) {
        err = drm_tegra_bo_unmap(pixmap->bo);
        if (err < 0)
            ERROR_MSG("FATAL: failed to unmap buffer object: %d\n", err);
    }

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_POOL)
        tegra_exa_pixmap_pool_unmap_entry(&pixmap->pool_entry);
}

static void
tegra_exa_mm_fridge_release_uncompressed_data(struct tegra_exa *exa,
                                              struct tegra_pixmap *pixmap,
                                              bool keep_fallback)
{
    switch (pixmap->type) {
    case TEGRA_EXA_PIXMAP_TYPE_FALLBACK:
        if (!keep_fallback) {
            free(pixmap->fallback);
            exa->release_count++;
        }
        break;

    case TEGRA_EXA_PIXMAP_TYPE_POOL:
        tegra_exa_pixmap_pool_unmap_entry(&pixmap->pool_entry);
        tegra_exa_pixmap_pool_free_entry(&pixmap->pool_entry);
        break;

    case TEGRA_EXA_PIXMAP_TYPE_BO:
        drm_tegra_bo_unref(pixmap->bo);
        break;
    }
}

static void tegra_exa_mm_resurrect_accel_pixmap(TegraPtr tegra,
                                                struct tegra_pixmap *pixmap)
{
    void *pixmap_data_orig;
    unsigned int data_size;
    struct timespec time;
    void *pixmap_data;
    struct tegra_exa *exa;
    bool ret;

    PROFILE_DEF(ressurection);

    if (!pixmap->accel || !pixmap->offscreen || !pixmap->tegra_data ||
        pixmap->type != TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return;

    exa = tegra->exa;
    clock_gettime(CLOCK_MONOTONIC, &time);

    /* don't retry too often */
    if (time.tv_sec - exa->last_resurrect_time < TEGRA_EXA_RESURRECT_DELTA)
        return;

    DEBUG_MSG("%s pixmap %p\n", __FILE__, pixmap->base);

    PROFILE_START(ressurection);

    pixmap_data_orig = pixmap->fallback;
    data_size = tegra_exa_pixmap_size(pixmap);

    ret = (tegra_exa_pixmap_allocate_from_pool(tegra, pixmap, data_size) ||
           tegra_exa_pixmap_allocate_from_bo(tegra, pixmap, data_size));

    if (ret == true) {
        pixmap->fence_write[TEGRA_2D] = NULL;
        pixmap->fence_write[TEGRA_3D] = NULL;
        pixmap->fence_read[TEGRA_2D]  = NULL;
        pixmap->fence_read[TEGRA_3D]  = NULL;
        pixmap_data = tegra_exa_mm_fridge_map_pixmap(pixmap);

        if (!pixmap_data) {
            tegra_exa_mm_fridge_release_uncompressed_data(exa, pixmap, false);
            pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;
        }
    } else {
        exa->last_resurrect_time = time.tv_sec;
        pixmap_data = NULL;
    }

    if (pixmap_data) {
        tegra_memcpy_vfp_aligned_src_cached(pixmap_data, pixmap_data_orig,
                                            data_size);
        tegra_exa_mm_fridge_unmap_pixmap(pixmap);
        free(pixmap_data_orig);
        exa->release_count++;
        exa->stats.num_pixmaps_resurrected++;
        exa->stats.num_pixmaps_resurrected_bytes += data_size;
    } else {
        pixmap->fallback = pixmap_data_orig;
    }

    PROFILE_STOP(ressurection);
}

static int tegra_exa_mm_compress_pixmap(struct tegra_exa *exa,
                                        struct tegra_pixmap *pixmap,
                                        struct compression_arg *c)
{
    unsigned long compressed_bound;
    unsigned long compressed_max;
    void *tmp;
    int err;

    if (c->compression_type == TEGRA_EXA_COMPRESSION_UNCOMPRESSED)
        goto uncompressed;

    DEBUG_MSG("priv %p compressing\n", pixmap);

    if (c->in_size > TEGRA_EXA_COMPRESS_SMALL_SIZE)
        compressed_max = c->in_size - TEGRA_EXA_COMPRESS_SMALL_SIZE / 8;
    else
        compressed_max = c->in_size * TEGRA_EXA_COMPRESS_RATIO_LIMIT;

#ifdef HAVE_LZ4
    if (c->compression_type == TEGRA_EXA_COMPRESSION_LZ4) {
        compressed_bound = LZ4_compressBound(c->in_size) + 4096;

        c->buf_out = malloc(compressed_bound);

        if (!c->buf_out) {
            ERROR_MSG("failed to allocate buffer for compression of size %lu\n",
                      compressed_bound);
            return -1;
        }

        c->out_size = LZ4_compress_default(c->buf_in, c->buf_out, c->in_size,
                                           compressed_bound);
        if (!c->out_size || c->out_size > compressed_max) {
            free(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DEBUG_MSG("priv %p poor compression\n", pixmap);
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
            ERROR_MSG("JPEG compression failed\n");
            tjFree(c->buf_out);
            goto uncompressed;
        }

        if (c->out_size > compressed_max) {
            tjFree(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DEBUG_MSG("priv %p poor compression\n", pixmap);
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
            ERROR_MSG("failed to allocate buffer for PNG compression of size %u\n",
                      png_size);
            return -1;
        }

        err = png_image_write_to_memory(&png, c->buf_out, &png_size, 0,
                                        c->buf_in, c->pitch, NULL);
        if (err == 0) {
            ERROR_MSG("PNG compression failed %s\n", png.message);
            free(c->buf_out);
            goto uncompressed;
        }

        if (png_size > compressed_max) {
            free(c->buf_out);
            /* just swap out poorly compressed pixmap from CMA */
            DEBUG_MSG("priv %p poor compression\n", pixmap);
            goto uncompressed;
        }

        tmp = realloc(c->buf_out, png_size);
        if (tmp) {
            c->out_size = png_size;
            c->buf_out = tmp;
        } else {
            DEBUG_MSG("priv %p realloc failure\n", pixmap);
            free(c->buf_out);
            goto uncompressed;
        }

        c->compression_type = TEGRA_EXA_COMPRESSION_PNG;
    }
#endif

    DEBUG_MSG("priv %p compressed\n", pixmap);

    return 0;

uncompressed:
    DEBUG_MSG("priv %p going uncompressed\n", pixmap);

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

static void
tegra_exa_mm_decompress_pixmap(TegraPtr tegra,
                               struct tegra_pixmap *pixmap,
                               struct compression_arg *c)
{
    struct tegra_exa *exa = tegra->exa;
#ifdef HAVE_PNG
    png_image png = { 0 };
#endif

    DEBUG_MSG("priv %p decompressing\n", pixmap);

    switch (c->compression_type) {
    case TEGRA_EXA_COMPRESSION_UNCOMPRESSED:
        tegra_memcpy_vfp_aligned_src_cached(c->buf_out, c->buf_in, c->out_size);
        DEBUG_MSG("priv %p decompressed: uncompressed\n", pixmap);

        /* clear released data for privacy protection */
        if (TEST_FREEZER || tegra->exa_erase_pixmaps)
            memset(c->buf_in, TEST_FREEZER ? 0xffffffff : 0, c->out_size);
        free(c->buf_in);
        break;

#ifdef HAVE_LZ4
    case TEGRA_EXA_COMPRESSION_LZ4:
        LZ4_decompress_fast(c->buf_in, c->buf_out, c->out_size);
        DEBUG_MSG("priv %p decompressed: lz4\n", pixmap);

        free(c->buf_in);
        break;
#endif

#ifdef HAVE_JPEG
    case TEGRA_EXA_COMPRESSION_JPEG:
        tjDecompress2(exa->jpegDecompressor, c->buf_in, c->in_size,
                  c->buf_out, c->width, c->pitch, c->height,
                  c->format, TJFLAG_FASTDCT);
        DEBUG_MSG("priv %p decompressed: jpeg\n", pixmap);

        tjFree(c->buf_in);
        break;
#endif

#ifdef HAVE_PNG
    case TEGRA_EXA_COMPRESSION_PNG:
        png.opaque = NULL;
        png.version = PNG_IMAGE_VERSION;
        png_image_begin_read_from_memory(&png, c->buf_in, c->in_size);
        if (png.warning_or_error)
            ERROR_MSG("png error: %s\n", png.message);
        png.format = c->format;
        png_image_finish_read(&png, NULL, c->buf_out, c->pitch, NULL);
        if (png.warning_or_error)
            ERROR_MSG("png error: %s\n", png.message);
        DEBUG_MSG("priv %p decompressed: png\n", pixmap);
        break;
#endif
    }
}

static struct compression_arg
tegra_exa_select_compression(TegraPtr tegra,
                             struct tegra_pixmap *pixmap,
                             unsigned int data_size,
                             void *pixmap_data)
{
    struct compression_arg carg = { 0 };

    carg.compression_type   = TEGRA_EXA_COMPRESSION_UNCOMPRESSED;
    carg.buf_out            = NULL;
    carg.buf_in             = pixmap_data;
    carg.out_size           = 0;
    carg.in_size            = data_size;
    carg.height             = pixmap->base->drawable.height;
    carg.width              = pixmap->base->drawable.width;
    carg.pitch              = pixmap->base->devKind;
    carg.format             = -1;
    carg.keep_fallback      = 0;

    /* don't reallocate if fallback compression fails, out = in */
    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        carg.keep_fallback = 1;

    /* don't compress if failed previously or if size is too small */
    if (pixmap->no_compress || data_size < TEGRA_EXA_OFFSET_ALIGN) {
        DEBUG_MSG("priv %p selected compression: uncompressed\n", pixmap);
        return carg;
    }

    /* JPEG is the preferred compression */
    if (tegra->exa_compress_jpeg) {
        carg.format     = tegra_exa_to_jpeg_turbo_format(tegra, pixmap);
        carg.samping    = tegra_exa_to_jpeg_turbo_sampling(pixmap);
        carg.quality    = tegra->exa_compress_jpeg_quality;

        if (carg.format > -1) {
            DEBUG_MSG("priv %p selected compression: jpeg\n", pixmap);
            carg.compression_type = TEGRA_EXA_COMPRESSION_JPEG;
            return carg;
        }
    }

    /* select PNG if pixmap's format is unsuitable for JPEG compression */
    if (tegra->exa_compress_png) {
        carg.format = tegra_exa_to_png_format(tegra, pixmap);

        if (carg.format > -1) {
            DEBUG_MSG("priv %p selected compression: png\n", pixmap);
            carg.compression_type = TEGRA_EXA_COMPRESSION_PNG;
            return carg;
        }
    }

    /* select LZ4 if pixmap's format is unsuitable for PNG / JPEG compression */
    if (tegra->exa_compress_lz4) {
        DEBUG_MSG("priv %p selected compression: lz4\n", pixmap);
        carg.compression_type = TEGRA_EXA_COMPRESSION_LZ4;
    }

    return carg;
}

static void
tegra_exa_thaw_pixmap_data(TegraPtr tegra,
                           struct tegra_pixmap *pixmap,
                           bool accel)
{
    struct tegra_exa *exa = tegra->exa;
    struct compression_arg carg;
    unsigned int retries = 0;
    unsigned int data_size;
    uint8_t *pixmap_data;
    bool ret = false;

    PROFILE_DEF(decompression);

    carg.compression_type   = pixmap->compression_type;
    carg.buf_in             = pixmap->compressed_data;
    carg.in_size            = pixmap->compressed_size;
    carg.format             = pixmap->compression_fmt;

    data_size = tegra_exa_pixmap_size(pixmap);
    pixmap->fence_write[TEGRA_2D] = NULL;
    pixmap->fence_write[TEGRA_3D] = NULL;
    pixmap->fence_read[TEGRA_2D]  = NULL;
    pixmap->fence_read[TEGRA_3D]  = NULL;

retry:
    if (accel || pixmap->accelerated)
        ret = (tegra_exa_pixmap_allocate_from_pool(tegra, pixmap, data_size) ||
               tegra_exa_pixmap_allocate_from_bo(tegra, pixmap, data_size));

    if (ret == false) {
        if (carg.compression_type == TEGRA_EXA_COMPRESSION_UNCOMPRESSED) {
            pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;
            pixmap->fallback = carg.buf_in;
            return;
        }

        ret = tegra_exa_pixmap_allocate_from_sysmem(tegra, pixmap, data_size);
    }

    if (ret == false) {
        if (retries++ > 100)
            ERROR_MSG("stuck! size %u\n", data_size);

        usleep(100000);
        goto retry;
    }

    pixmap_data = tegra_exa_mm_fridge_map_pixmap(pixmap);
    if (!pixmap_data) {
        ERROR_MSG("FATAL: can't restore pixmap data\n");
        return;
    }

    if (VALIDATE_COMPRESSION) {
        unsigned int cpp = pixmap->base->drawable.bitsPerPixel / 8;
        unsigned int width_bytes = pixmap->base->drawable.width * cpp;
        unsigned int x, y, match;

        for (y = 0; y < pixmap->base->drawable.height; y++) {
            for (x = 0; x < width_bytes; x++) {
                match = y * width_bytes + x + 0x55;
                pixmap_data[y * width_bytes + x] = match;
            }
        }
    }

    carg.buf_out    = pixmap_data;
    carg.out_size   = data_size;
    carg.height     = pixmap->base->drawable.height;
    carg.width      = pixmap->base->drawable.width;
    carg.pitch      = pixmap->base->devKind;

    PROFILE_START(decompression);
    tegra_exa_mm_decompress_pixmap(tegra, pixmap, &carg);
    PROFILE_STOP(decompression);

    if (VALIDATE_COMPRESSION) {
        unsigned int cpp = pixmap->base->drawable.bitsPerPixel / 8;
        unsigned int width_bytes = pixmap->base->drawable.width * cpp;
        unsigned int x, y, match, matched = 0;

        for (y = 0; y < pixmap->base->drawable.height; y++) {
            for (x = 0; x < width_bytes; x++) {
                match = y * width_bytes + x + 0x55;

                if (pixmap_data[y * width_bytes + x] == (match & 0xff))
                    matched++;
            }
        }

        if (matched > data_size / 2)
            ERROR_MSG("priv %p decompression failure! data_size %u\n",
                      pixmap, data_size);
    }

    tegra_exa_mm_fridge_unmap_pixmap(pixmap);

    exa->stats.num_pixmaps_decompressed++;
    exa->stats.num_pixmaps_decompression_bytes += data_size;
}

static int tegra_exa_freeze_pixmap(TegraPtr tegra, struct tegra_pixmap *pixmap)
{
    struct tegra_exa *exa = tegra->exa;
    struct compression_arg carg;
    unsigned int data_size;
    void *pixmap_data;
    int err;

    PROFILE_DEF(compression);

    data_size = tegra_exa_pixmap_size(pixmap);

    pixmap_data = tegra_exa_mm_fridge_map_pixmap(pixmap);

    if (!pixmap_data) {
        ERROR_MSG("failed to map pixmap data\n");
        return -1;
    }

    /*
     * Note that tegra_exa_mm_fridge_map_pixmap() flushes deferred operations,
     * and thus, pixmap will be thawed and then returned to the cool_pixmaps
     * list after the flushing is finished, hence pixmap shall be removed from
     * the list after the mapping is done.
     */
    if (pixmap->cold) {
        exa->cooling_size -= data_size;
        xorg_list_del(&pixmap->fridge_entry);
        pixmap->cold = false;
    }

    carg = tegra_exa_select_compression(tegra, pixmap, data_size, pixmap_data);

    PROFILE_START(compression);
    err = tegra_exa_mm_compress_pixmap(exa, pixmap, &carg);
    PROFILE_STOP(compression);

    if (err < 0) {
        ERROR_MSG("failed to freeze pixmap\n");
        goto fail_unmap;
    }

    if (!err || !carg.keep_fallback) {
        /* clear released data for privacy protection */
        memset(pixmap_data, TEST_FREEZER ? 0xffffffff : 0, data_size);
    }

    pixmap->no_compress = err;

    tegra_exa_mm_fridge_release_uncompressed_data(exa, pixmap,
                                                  carg.keep_fallback);

    pixmap->compression_type    = carg.compression_type;
    pixmap->compressed_data     = carg.buf_out;
    pixmap->compressed_size     = carg.out_size;
    pixmap->compression_fmt     = carg.format;
    pixmap->type                = TEGRA_EXA_PIXMAP_TYPE_NONE;
    pixmap->frozen              = true;

    exa->stats.num_pixmaps_compressed++;
    exa->stats.num_pixmaps_compression_in_bytes  += data_size;
    exa->stats.num_pixmaps_compression_out_bytes += carg.out_size;

    return 0;

fail_unmap:
    tegra_exa_mm_fridge_unmap_pixmap(pixmap);

    return -1;
}

static void tegra_exa_freeze_pixmaps(TegraPtr tegra, time_t time_sec)
{
    struct tegra_exa *exa = tegra->exa;
    struct tegra_pixmap *pix, *tmp;
    unsigned long frost_size = 1;
    unsigned long cooling_size;
    bool emergence = false;
    int err;

    PROFILE_DEF(freezing);

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

    DEBUG_MSG("time_sec %ld last_freezing_time %ld cooling_size %lu\n",
              time_sec, exa->last_freezing_time, exa->cooling_size);

    /*
     * Enforce freezing if there are more than several megabytes of pixmaps
     * pending to be frozen.
     */
    if (exa->cooling_size > TEGRA_EXA_COOLING_LIMIT_MAX)
        emergence = true;

    /* allow freezing only once per couple seconds */
    if (time_sec - exa->last_freezing_time < TEGRA_EXA_FREEZE_ALLOWANCE_DELTA)
        return;

freeze:
    cooling_size = exa->cooling_size;
    frost_size = 0;

    PROFILE_START(freezing);

    xorg_list_for_each_entry_safe(pix, tmp, &exa->cool_pixmaps, fridge_entry) {
        if (time_sec - pix->last_use < TEGRA_EXA_FREEZE_MAX_DELTA &&
            !TEST_FREEZER)
            break;

        DEBUG_MSG("priv %p last_use %ld stalled\n", pix, pix->last_use);

        /* enforce freezing of staled pixmaps */
        tegra_exa_freeze_pixmap(tegra, pix);
    }

    xorg_list_for_each_entry_safe(pix, tmp, &exa->cool_pixmaps, fridge_entry) {
        if (time_sec - pix->last_use < TEGRA_EXA_FREEZE_MIN_DELTA)
            break;

        DEBUG_MSG("priv %p last_use %ld cool\n", pix, pix->last_use);

        err = tegra_exa_freeze_pixmap(tegra, pix);
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

    PROFILE_STOP(freezing);
}

static void tegra_exa_cool_tegra_pixmap(TegraPtr tegra,
                                        struct tegra_pixmap *pix)
{
    struct tegra_exa *exa = tegra->exa;
    struct timespec time;

    if (pix->frozen || pix->cold || pix->scanout || pix->dri ||
        !pix->accel || !pix->offscreen || !pix->tegra_data ||
        pix->freezer_lockcnt ||
        pix->type == TEGRA_EXA_PIXMAP_TYPE_NONE)
        return;

    if (!tegra->exa_refrigerator)
        return;

    clock_gettime(CLOCK_MONOTONIC, &time);

    xorg_list_append(&pix->fridge_entry, &exa->cool_pixmaps);
    pix->last_use = time.tv_sec;
    pix->cold = true;

    exa->cooling_size += tegra_exa_pixmap_size(pix);
}

static void tegra_exa_cool_pixmap(PixmapPtr pixmap, bool write)
{
    ScrnInfoPtr pScrn;
    struct tegra_pixmap *priv;
    TegraPtr tegra;

    if (pixmap) {
        pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
        priv  = exaGetPixmapDriverPrivate(pixmap);
        tegra = TegraPTR(pScrn);

        assert(!priv->destroyed);

        if (tegra->exa_refrigerator) {
            tegra_exa_cool_tegra_pixmap(tegra, priv);

            if (write)
                priv->no_compress = false;
        }
    }
}

static void
tegra_exa_fill_pixmap_data_sw(struct tegra_pixmap *pixmap, Pixel color)
{
    unsigned int size = tegra_exa_pixmap_size(pixmap);
    PixmapPtr pix = pixmap->base;
    unsigned int x, y;
    void *ptr;

    DEBUG_MSG("%s pixmap %p\n", __FILE__, pixmap->base);

    if (tegra_exa_prepare_cpu_access(pix, EXA_PREPARE_DEST, &ptr, false)) {
        if (color == 0 || pixmap->base->drawable.bitsPerPixel == 8) {
            memset(ptr, color, size);
        } else {
            for (y = 0; y < pixmap->base->drawable.height; y++) {
                for (x = 0; x < pixmap->base->drawable.width; x++) {
                    switch (pixmap->base->drawable.bitsPerPixel) {
                    case 8:
                        *(((CARD8*) ptr) + x) = color;
                        break;
                    case 16:
                        *(((CARD16*) ptr) + x) = color;
                        break;
                    case 32:
                        *(((CARD32*) ptr) + x) = color;
                        break;
                    }
                }

                ptr = (char*)ptr + pixmap->base->devKind;
            }
        }

        tegra_exa_finish_cpu_access(pix, EXA_PREPARE_DEST);
    }
}

static void tegra_exa_clear_pixmap_data_sw(struct tegra_pixmap *pixmap)
{
    /* always zero-fill allocated data for consistency */
    tegra_exa_fill_pixmap_data_sw(pixmap, TEST_FREEZER ? 0xff : 0);
}

static bool
tegra_exa_fill_pixmap_data_hw(struct tegra_pixmap *pixmap, Pixel color)
{
    DEBUG_MSG("%s pixmap %p\n", __FILE__, pixmap->base);

    if (tegra_exa_prepare_solid_2d(pixmap->base, GXcopy, FB_ALLONES, color)) {
        tegra_exa_solid_2d(pixmap->base, 0, 0,
                           pixmap->base->drawable.width,
                           pixmap->base->drawable.height);
        tegra_exa_done_solid_2d(pixmap->base);

        return true;
    }

    return false;
}

static bool tegra_exa_clear_pixmap_data_hw(struct tegra_pixmap *pixmap)
{
    return tegra_exa_fill_pixmap_data_hw(pixmap, TEST_FREEZER ? 0xffffffff : 0);
}

static bool tegra_exa_prefer_hw_fill(struct tegra_pixmap *pixmap, bool accel)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->base->drawable.pScreen);
    TegraPtr tegra = TegraPTR(scrn);
    struct tegra_exa *exa = tegra->exa;
    struct drm_tegra_bo *bo = NULL;
    struct tegra_pixmap_pool *pool;

    if (pixmap->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return false;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_BO)
        bo = pixmap->bo;

    if (pixmap->type == TEGRA_EXA_PIXMAP_TYPE_POOL) {
        pool = to_tegra_pool(pixmap->pool_entry.pool);
        bo = pool->bo;
    }

    if (tegra_exa_pixmap_is_busy(exa, pixmap))
        return true;

    /*
     * HW job execution overhead is bigger for small pixmaps than clearing
     * on CPU, so we prefer CPU for a such pixmaps.
     */
    if (tegra_exa_pixmap_size(pixmap) < TEGRA_EXA_CPU_FILL_MIN_SIZE &&
        (!accel || drm_tegra_bo_mapped(bo)))
        return false;

    return true;
}

static void tegra_exa_clear_pixmap_data(struct tegra_pixmap *pixmap, bool accel)
{
    if (!tegra_exa_prefer_hw_fill(pixmap, accel) ||
        !tegra_exa_clear_pixmap_data_hw(pixmap))
            tegra_exa_clear_pixmap_data_sw(pixmap);
}

static void
tegra_exa_fill_pixmap_data(struct tegra_pixmap *pixmap, bool accel, Pixel color)
{
    if (!tegra_exa_prefer_hw_fill(pixmap, accel) ||
        !tegra_exa_fill_pixmap_data_hw(pixmap, color))
            tegra_exa_fill_pixmap_data_sw(pixmap, color);
}

static void
tegra_exa_allocate_pixmap_data_no_fail(TegraPtr tegra,
                                       struct tegra_pixmap *pixmap,
                                       bool accel)
{
    unsigned int size = tegra_exa_pixmap_size(pixmap);
    unsigned int retries = 0;

    PROFILE_DEF(alloc);
    PROFILE_START(alloc);

    while (1) {
        if (!accel) {
            if (tegra_exa_pixmap_allocate_from_sysmem(tegra, pixmap, size))
                break;
        } else {
            /* take opportunity to re-use allocation from a free-list */
            tegra_exa_clean_up_pixmaps_freelist(tegra, false);

            if (tegra_exa_pixmap_allocate_from_pool(tegra, pixmap, size) ||
                tegra_exa_pixmap_allocate_from_bo(tegra, pixmap, size) ||
                tegra_exa_pixmap_allocate_from_sysmem(tegra, pixmap, size))
                break;
        }

        if (retries++ > 100)
            ERROR_MSG("stuck! size %u\n", size);

        usleep(100000);
    }

    PROFILE_STOP(alloc);
}

static void tegra_exa_thaw_pixmap2(PixmapPtr pixmap,
                                   enum thaw_accel accel,
                                   enum thaw_alloc allocate)
{
    ScrnInfoPtr pScrn;
    struct tegra_pixmap *priv;
    struct tegra_exa *exa;
    TegraPtr tegra;

    PROFILE_DEF(thaw);
    PROFILE_START(thaw);

    if (pixmap) {
        pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
        priv  = exaGetPixmapDriverPrivate(pixmap);
        tegra = TegraPTR(pScrn);
        exa   = tegra->exa;

        assert(!priv->destroyed);

        priv->accelerated |= accel;

        if (!tegra->exa_refrigerator || priv->freezer_lockcnt)
            return;

        if (priv->frozen) {
            tegra_exa_thaw_pixmap_data(tegra, priv, accel);
            priv->accelerated = accel;
            priv->frozen = false;
            return;
        }

        if (priv->cold) {
            exa->cooling_size -= tegra_exa_pixmap_size(priv);
            xorg_list_del(&priv->fridge_entry);
            priv->cold = false;
        }

        if (allocate) {
            if (accel)
                tegra_exa_mm_resurrect_accel_pixmap(tegra, priv);

            if (priv->type == TEGRA_EXA_PIXMAP_TYPE_NONE && priv->tegra_data)
                tegra_exa_allocate_pixmap_data_no_fail(tegra, priv, accel);
        }
    }

    PROFILE_STOP(thaw);
}

static void tegra_exa_thaw_pixmap(PixmapPtr pixmap, bool accel)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);

    if (!priv->freezer_lockcnt)
        tegra_exa_thaw_pixmap2(pixmap, accel ? THAW_ACCEL : THAW_NOACCEL,
                               THAW_ALLOC);
}

/* vim: set et sts=4 sw=4 ts=4: */
