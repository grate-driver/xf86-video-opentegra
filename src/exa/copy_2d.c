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

static const uint8_t rop3[] = {
    0x00, /* GXclear */
    0x88, /* GXand */
    0x44, /* GXandReverse */
    0xcc, /* GXcopy */
    0x22, /* GXandInverted */
    0xaa, /* GXnoop */
    0x66, /* GXxor */
    0xee, /* GXor */
    0x11, /* GXnor */
    0x99, /* GXequiv */
    0x55, /* GXinvert */
    0xdd, /* GXorReverse */
    0x33, /* GXcopyInverted */
    0xbb, /* GXorInverted */
    0x77, /* GXnand */
    0xff, /* GXset */
};

static uint32_t sb_offset(PixmapPtr pix, unsigned xpos, unsigned ypos)
{
    unsigned bytes_per_pixel = pix->drawable.bitsPerPixel >> 3;
    unsigned pitch = exaGetPixmapPitch(pix);
    uint32_t offset;

    offset = ypos * pitch;
    offset += xpos * bytes_per_pixel;

    return tegra_exa_pixmap_offset(pix) + offset;
}

static bool
tegra_exa_prepare_copy_2d_ext(PixmapPtr src_pixmap, PixmapPtr dst_pixmap,
                              int op, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    enum tegra_2d_orientation orientation;
    struct tegra_pixmap *priv;
    unsigned int bpp;
    int fr_mode;
    int err;

    ACCEL_MSG("\n");

    orientation = tegra->scratch.orientation;

    if (orientation == TEGRA2D_IDENTITY)
        fr_mode = 0; /* DISABLE */
    else if (src_pixmap != dst_pixmap)
        fr_mode = 1; /* SRC_DST_COPY */
    else
        fr_mode = 2; /* SQUARE */

    /*
     * It should be possible to support this, but let's bail for now
     */
    if (planemask != FB_ALLONES) {
        FALLBACK_MSG("unsupported planemask 0x%08lx\n", planemask);
        return false;
    }

    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy) {
        FALLBACK_MSG("unsupported operation %d\n", op);
        return false;
    }

    /*
     * Some restrictions apply to the hardware accelerated copying.
     */
    bpp = src_pixmap->drawable.bitsPerPixel;

    if (dst_pixmap->drawable.bitsPerPixel != bpp) {
        FALLBACK_MSG("BPP mismatch %u %u\n",
                     dst_pixmap->drawable.bitsPerPixel, bpp);
        return false;
    }

    tegra_exa_thaw_pixmap2(src_pixmap, THAW_ACCEL, THAW_ALLOC);
    tegra_exa_thaw_pixmap2(dst_pixmap, THAW_ACCEL, THAW_ALLOC);

    priv = exaGetPixmapDriverPrivate(src_pixmap);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FALLBACK_MSG("unaccelerateable src pixmap %d:%d:%d\n",
                     src_pixmap->drawable.width,
                     src_pixmap->drawable.height,
                     src_pixmap->drawable.bitsPerPixel);
        return false;
    }

    ACCEL_MSG("src_pixmap %p priv %p type %u %d:%d:%d stride %d scanout %d\n",
              src_pixmap, priv, priv->type,
              src_pixmap->drawable.width,
              src_pixmap->drawable.height,
              src_pixmap->drawable.bitsPerPixel,
              src_pixmap->devKind,
              priv->scanout);

    priv = exaGetPixmapDriverPrivate(dst_pixmap);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FALLBACK_MSG("unaccelerateable dst pixmap %d:%d:%d\n",
                     dst_pixmap->drawable.width,
                     dst_pixmap->drawable.height,
                     dst_pixmap->drawable.bitsPerPixel);
        return false;
    }

    ACCEL_MSG("dst_pixmap %p priv %p type %u %d:%d:%d stride %d scanout %d\n",
              dst_pixmap, priv, priv->type,
              dst_pixmap->drawable.width,
              dst_pixmap->drawable.height,
              dst_pixmap->drawable.bitsPerPixel,
              dst_pixmap->devKind,
              priv->scanout);

    err = tegra_stream_begin(tegra->cmds, tegra->gr2d);
    if (err < 0)
        return false;

    tegra_stream_prep(tegra->cmds, orientation == TEGRA2D_IDENTITY ? 14 : 12);
    tegra_stream_push_setclass(tegra->cmds, HOST1X_CLASS_GR2D);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(tegra->cmds, orientation == TEGRA2D_IDENTITY ?
                                   0x0000003a : 0x00000037 ); /* trigger */
    tegra_stream_push(tegra->cmds, 0x00000000); /* cmdsel */
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x01e, 0x5));
    tegra_stream_push(tegra->cmds, /* controlsecond */
                    orientation << 26 | fr_mode << 24);
    tegra_stream_push(tegra->cmds, rop3[op]); /* ropfade */
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x046, 1));
    /*
     * [20:20] destination write tile mode (0: linear, 1: tiled)
     * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
     */
    tegra_stream_push(tegra->cmds, 0x00000000); /* tilemode */

    if (orientation == TEGRA2D_IDENTITY) {
        tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x149));

        tegra_stream_push_reloc(tegra->cmds, tegra_exa_pixmap_bo(dst_pixmap),
                                tegra_exa_pixmap_offset(dst_pixmap), true,
                                tegra_exa_pixmap_is_from_pool(dst_pixmap));
        tegra_stream_push(tegra->cmds, exaGetPixmapPitch(dst_pixmap)); /* dstst */

        tegra_stream_push_reloc(tegra->cmds, tegra_exa_pixmap_bo(src_pixmap),
                                tegra_exa_pixmap_offset(src_pixmap), false,
                                tegra_exa_pixmap_is_from_pool(src_pixmap));
        tegra_stream_push(tegra->cmds, exaGetPixmapPitch(src_pixmap)); /* srcst */
    } else {
        tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x108));
        tegra_stream_push(tegra->cmds, exaGetPixmapPitch(dst_pixmap)); /* dstst */
        tegra_stream_push(tegra->cmds, exaGetPixmapPitch(src_pixmap)); /* srcst */
    }

    if (tegra->cmds->status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(tegra->cmds);
        return false;
    }

    tegra->scratch.src = src_pixmap;
    tegra->scratch.src_x = -1;
    tegra->scratch.src_y = -1;
    tegra->scratch.dst_x = -1;
    tegra->scratch.dst_y = -1;
    tegra->scratch.ops = 0;

    return true;
}

static bool
tegra_exa_prepare_copy_2d(PixmapPtr src_pixmap, PixmapPtr dst_pixmap,
                          int dx, int dy, int op, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;

    tegra->scratch.orientation = TEGRA2D_IDENTITY;

    if (tegra_exa_prepare_copy_2d_ext(src_pixmap, dst_pixmap, op, planemask)) {
        tegra_exa_prepare_optimized_copy(src_pixmap, dst_pixmap, op, planemask);
        return true;
    }

    return false;
}

static void tegra_exa_copy_2d_ext(PixmapPtr dst_pixmap, int src_x, int src_y,
                                  int dst_x, int dst_y, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    int tsrc_x, tsrc_y, tdst_x, tdst_y, twidth, theight;
    struct drm_tegra_bo * src_bo;
    struct drm_tegra_bo * dst_bo;
    PixmapPtr src_pixmap;
    uint32_t controlmain;
    unsigned cell_size;
    unsigned bpp;
    PictVector v;
    BoxRec grid;

    ACCEL_MSG("src %dx%d dst %dx%d w:h %d:%d\n",
              src_x, src_y, dst_x, dst_y, width, height);

    src_pixmap = tegra->scratch.src;
    src_bo     = tegra_exa_pixmap_bo(src_pixmap);
    dst_bo     = tegra_exa_pixmap_bo(dst_pixmap);
    bpp        = dst_pixmap->drawable.bitsPerPixel;
    cell_size  = 16 / (bpp >> 3);

    /*
     * From TRM 29.2.3 comment to FR_MODE:
     *
     * Source and destination base address must be 128-bit word aligned
     * engine works on FR_BLOCK granularity: transformed surface width in
     * multiples of 16-bytes, transformed surface height in multiples of
     * 16/8/4 lines for bpp8/bpp16/bpp32.
     */

    grid.x1 = TEGRA_ROUND_DOWN(dst_x, cell_size);
    grid.y1 = TEGRA_ROUND_DOWN(dst_y, cell_size);
    grid.x2 = TEGRA_ROUND_UP(dst_x + width,  cell_size);
    grid.y2 = TEGRA_ROUND_UP(dst_y + height, cell_size);

    twidth  = grid.x2 - grid.x1;
    theight = grid.y2 - grid.y1;

    tdst_x = grid.x1;
    tdst_y = grid.y1;

    v.vector[0] = tdst_x << 16;
    v.vector[1] = tdst_y << 16;
    v.vector[2] = 1 << 16;

    PictureTransformPoint3d(&tegra->scratch.transform, &v);

    tsrc_x = v.vector[0] >> 16;
    tsrc_y = v.vector[1] >> 16;

    v.vector[0] = twidth  << 16;
    v.vector[1] = theight << 16;
    v.vector[2] = 0;

    PictureTransformPoint3d(&tegra->scratch.transform, &v);

    twidth  = v.vector[0] >> 16;
    theight = v.vector[1] >> 16;

    if (twidth < 0) {
        tsrc_x += twidth;
        twidth = -twidth;
    }

    if (theight < 0) {
        tsrc_y  += theight;
        theight = -theight;
    }

    /*
     * The copying region may happen to become unaligned after
     * transformation if acceleration-checking missed some case.
     */
    if (!TEGRA_ALIGNED(tsrc_x, cell_size) ||
        !TEGRA_ALIGNED(tsrc_y, cell_size) ||
        !TEGRA_ALIGNED(twidth, cell_size) ||
        !TEGRA_ALIGNED(theight, cell_size)) {

        ERROR_MSG("shouldn't happen %d:%d %d:%d\n",
                  tsrc_x, tsrc_y, twidth, theight);
        return;
    }

    src_x   = tsrc_x;
    src_y   = tsrc_y;
    dst_x   = tdst_x;
    dst_y   = tdst_y;
    width   = twidth  - 1;
    height  = theight - 1;

    tegra_stream_prep(tegra->cmds, 11);

    if (tegra->scratch.dst_x != dst_x || tegra->scratch.dst_y != dst_y) {
        tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x2b, 1));

        tegra_stream_push_reloc(tegra->cmds, dst_bo,
                                sb_offset(dst_pixmap, dst_x, dst_y), true,
                                tegra_exa_pixmap_is_from_pool(dst_pixmap));

        tegra->scratch.dst_x = dst_x;
        tegra->scratch.dst_y = dst_y;
    }

    if (tegra->scratch.src_x != src_x || tegra->scratch.src_y != src_y) {
        tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x31, 1));

        tegra_stream_push_reloc(tegra->cmds, src_bo,
                                sb_offset(src_pixmap, src_x, src_y), false,
                                tegra_exa_pixmap_is_from_pool(src_pixmap));

        tegra->scratch.src_x = src_x;
        tegra->scratch.src_y = src_y;
    }

    /*
     * [29:29] fast rotate wait for read (0: disable, 1: enable)
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     */
    controlmain = (1 << 29) | (1 << 20) | ((bpp >> 4) << 16);

    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x01f, 1));
    tegra_stream_push(tegra->cmds, controlmain);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x37, 0x1));
    tegra_stream_push(tegra->cmds, height << 16 | width); /* srcsize */
    tegra_stream_sync(tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE, true);

    tegra->scratch.ops++;
}

static void tegra_exa_copy_2d(PixmapPtr dst_pixmap, int src_x, int src_y,
                              int dst_x, int dst_y, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    uint32_t controlmain;

    ACCEL_MSG("src %dx%d dst %dx%d w:h %d:%d\n",
              src_x, src_y, dst_x, dst_y, width, height);

    if (tegra_exa_optimize_copy_op(dst_pixmap, dst_x, dst_y, width, height))
        return;

    /*
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     * [10:10] y-direction (0: increment, 1: decrement)
     * [9:9] x-direction (0: increment, 1: decrement)
     */
    controlmain = (1 << 20) | ((dst_pixmap->drawable.bitsPerPixel >> 4) << 16);

    if (dst_x > src_x) {
        controlmain |= 1 << 9;
        src_x += width - 1;
        dst_x += width - 1;
    }

    if (dst_y > src_y) {
        controlmain |= 1 << 10;
        src_y += height - 1;
        dst_y += height - 1;
    }

    tegra_stream_prep(tegra->cmds, 7);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_INCR(0x01f, 1));
    tegra_stream_push(tegra->cmds, controlmain);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_INCR(0x37, 0x4));
    tegra_stream_push(tegra->cmds, height << 16 | width); /* srcsize */
    tegra_stream_push(tegra->cmds, height << 16 | width); /* dstsize */
    tegra_stream_push(tegra->cmds, src_y << 16 | src_x); /* srcps */
    tegra_stream_push(tegra->cmds, dst_y << 16 | dst_x); /* dstps */
    tegra_stream_sync(tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE, true);

    tegra->scratch.ops++;
}

static void tegra_exa_done_copy_2d(PixmapPtr dst_pixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pixmap->drawable.pScreen);
    struct tegra_pixmap *dst_priv = exaGetPixmapDriverPrivate(dst_pixmap);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *explicit_fence;
    struct tegra_fence *fence;

    PROFILE_DEF(copy);

    tegra_exa_complete_copy_optimization(dst_pixmap);

    if (tegra->scratch.ops && tegra->cmds->status == TEGRADRM_STREAM_CONSTRUCT) {
        tegra->stats.num_2d_copy_jobs_bytes += tegra_stream_pushbuf_size(tegra->cmds);
        tegra_stream_end(tegra->cmds);

        tegra_exa_wait_pixmaps(TEGRA_3D, dst_pixmap, 1, tegra->scratch.src);

        explicit_fence = tegra_exa_get_explicit_fence(TEGRA_3D, dst_pixmap,
                                                      1, tegra->scratch.src);

        PROFILE_START(copy);
        fence = tegra_exa_stream_submit(tegra, TEGRA_2D, explicit_fence);
        PROFILE_STOP(copy);

        TEGRA_FENCE_PUT(explicit_fence);

        tegra_exa_replace_pixmaps_fence(TEGRA_2D, fence, &tegra->scratch,
                                        dst_pixmap, 1, tegra->scratch.src);

        if (dst_priv->scanout)
            tegra->stats.num_2d_copy_jobs_to_scanout++;

        tegra->stats.num_2d_copy_jobs++;
    } else {
        tegra_stream_cleanup(tegra->cmds);
    }

    tegra_exa_cool_pixmap(tegra->scratch.src, false);
    tegra_exa_cool_pixmap(dst_pixmap, true);

    ACCEL_MSG("\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
