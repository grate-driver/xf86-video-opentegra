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

static bool
tegra_exa_prepare_solid_2d(PixmapPtr pixmap, int op, Pixel planemask,
                           Pixel color)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    unsigned int bpp = pixmap->drawable.bitsPerPixel;
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    int err;

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

    tegra_exa_thaw_pixmap2(pixmap, THAW_ACCEL, THAW_ALLOC);

    tegra->scratch.ops = 0;

    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        if (pixmap->drawable.width == 1 &&
            pixmap->drawable.height == 1) {
                void *ptr = priv->fallback;

                switch (pixmap->drawable.bitsPerPixel) {
                case 8:
                    *((CARD8*) ptr) = color;
                    return true;
                case 16:
                    *((CARD16*) ptr) = color;
                    return true;
                case 32:
                    *((CARD32*) ptr) = color;
                    return true;
                }
        }

        FALLBACK_MSG("unaccelerateable pixmap %d:%d:%d\n",
                     pixmap->drawable.width,
                     pixmap->drawable.height,
                     pixmap->drawable.bitsPerPixel);

        return false;
    }

    err = tegra_stream_begin(tegra->cmds, tegra->gr2d);
    if (err < 0)
        return false;

    tegra_stream_prep(tegra->cmds, 15);
    tegra_stream_push_setclass(tegra->cmds, HOST1X_CLASS_GR2D);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(tegra->cmds, 0x0000003a); /* trigger */
    tegra_stream_push(tegra->cmds, 0x00000000); /* cmdsel */
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x35, 1));
    tegra_stream_push(tegra->cmds, color);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x1e, 0x7));
    tegra_stream_push(tegra->cmds, 0x00000000); /* controlsecond */
    tegra_stream_push(tegra->cmds, /* controlmain */
                      ((bpp >> 4) << 16) |  /* bytes per pixel */
                      (1 << 6) |            /* fill mode */
                      (1 << 2)              /* turbo-fill */);
    tegra_stream_push(tegra->cmds, rop3[op]); /* ropfade */
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x9));
    tegra_stream_push_reloc(tegra->cmds, tegra_exa_pixmap_bo(pixmap),
                            tegra_exa_pixmap_offset(pixmap), true,
                            tegra_exa_pixmap_is_from_pool(pixmap));
    tegra_stream_push(tegra->cmds, exaGetPixmapPitch(pixmap));
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_NONINCR(0x46, 1));
    tegra_stream_push(tegra->cmds, 0); /* non-tiled */

    if (tegra->cmds->status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(tegra->cmds);
        return false;
    }

    tegra_exa_prepare_optimized_solid_fill(pixmap, color);

    ACCEL_MSG("pixmap %p %d:%d color %08lx scanout %d\n",
              pixmap,
              pixmap->drawable.width,
              pixmap->drawable.height,
              color, priv->scanout);

    return true;
}

static void
tegra_exa_solid_2d(PixmapPtr pixmap, int px1, int py1, int px2, int py2)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;

    if (pixmap->drawable.width == 1 && pixmap->drawable.height == 1)
        return;

    ACCEL_MSG("%dx%d w:h %d:%d\n", px1, py1, px2 - px1, py2 - py1);

    if (tegra_exa_optimize_solid_op(pixmap, px1, py1, px2, py2))
        return;

    tegra_stream_prep(tegra->cmds, 3);
    tegra_stream_push(tegra->cmds, HOST1X_OPCODE_MASK(0x38, 0x5));
    tegra_stream_push(tegra->cmds, (py2 - py1) << 16 | (px2 - px1));
    tegra_stream_push(tegra->cmds, py1 << 16 | px1);
    tegra_stream_sync(tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE, true);

    tegra->scratch.ops++;
}

static void tegra_exa_done_solid_2d(PixmapPtr pixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *explicit_fence;
    struct tegra_fence *fence;

    PROFILE_DEF(solid);

    tegra_exa_complete_solid_fill_optimization(pixmap);

    if (tegra->scratch.ops && tegra->cmds->status == TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_end(tegra->cmds);

        tegra_exa_wait_pixmaps(TEGRA_3D, pixmap, 0);

        explicit_fence = tegra_exa_get_explicit_fence(TEGRA_3D, pixmap, 0);

        PROFILE_START(solid);
        fence = tegra_exa_stream_submit(tegra, TEGRA_2D, explicit_fence);
        PROFILE_STOP(solid);

        TEGRA_FENCE_PUT(explicit_fence);

        tegra_exa_replace_pixmaps_fence(TEGRA_2D, fence, &tegra->scratch,
                                        pixmap, 0);
    } else {
        tegra_stream_cleanup(tegra->cmds);
    }

    tegra_exa_cool_pixmap(pixmap, true);

    ACCEL_MSG("\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
