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

    return TegraEXAPixmapOffset(pix) + offset;
}

Bool TegraEXAPrepareSolid(PixmapPtr pPixmap, int op, Pixel planemask,
                          Pixel color)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    unsigned int bpp = pPixmap->drawable.bitsPerPixel;
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    int err;

    /*
     * It should be possible to support this, but let's bail for now
     */
    if (planemask != FB_ALLONES) {
        FallbackMsg("unsupported planemask 0x%08lx\n", planemask);
        return FALSE;
    }

    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy) {
        FallbackMsg("unsupported operation %d\n", op);
        return FALSE;
    }

    TegraEXAThawPixmap(pPixmap, TRUE);

    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FallbackMsg("unaccelerateable pixmap %d:%d\n",
                    pPixmap->drawable.width,
                    pPixmap->drawable.height);
        return FALSE;
    }

    err = tegra_stream_begin(&tegra->cmds, tegra->gr2d);
    if (err < 0)
            return FALSE;

    tegra_stream_prep(&tegra->cmds, 15);
    tegra_stream_push_setclass(&tegra->cmds, HOST1X_CLASS_GR2D);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(&tegra->cmds, 0x0000003a); /* trigger */
    tegra_stream_push(&tegra->cmds, 0x00000000); /* cmdsel */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x35, 1));
    tegra_stream_push(&tegra->cmds, color);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x1e, 0x7));
    tegra_stream_push(&tegra->cmds, 0x00000000); /* controlsecond */
    tegra_stream_push(&tegra->cmds, /* controlmain */
                      ((bpp >> 4) << 16) | /* bytes per pixel */
                      (1 << 6) |           /* fill mode */
                      (1 << 2)             /* turbo-fill */);
    tegra_stream_push(&tegra->cmds, rop3[op]); /* ropfade */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x9));
    tegra_stream_push_reloc(&tegra->cmds, TegraEXAPixmapBO(pPixmap),
                            TegraEXAPixmapOffset(pPixmap));
    tegra_stream_push(&tegra->cmds, exaGetPixmapPitch(pPixmap));
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x46, 1));
    tegra_stream_push(&tegra->cmds, 0); /* non-tiled */

    if (tegra->cmds.status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(&tegra->cmds);
        return FALSE;
    }

    tegra->scratch.ops = 0;

    AccelMsg("pixmap %p %d:%d color %08lx\n",
             pPixmap,
             pPixmap->drawable.width,
             pPixmap->drawable.height,
             color);

    return TRUE;
}

void TegraEXASolid(PixmapPtr pPixmap, int px1, int py1, int px2, int py2)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    AccelMsg("%dx%d w:h %d:%d\n", px1, py1, px2 - px1, py2 - py1);

    tegra_stream_prep(&tegra->cmds, 3);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x38, 0x5));
    tegra_stream_push(&tegra->cmds, (py2 - py1) << 16 | (px2 - px1));
    tegra_stream_push(&tegra->cmds, py1 << 16 | px1);
    tegra_stream_sync(&tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE);

    tegra->scratch.ops++;
}

void TegraEXADoneSolid(PixmapPtr pPixmap)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *fence;

    if (tegra->scratch.ops && tegra->cmds.status == TEGRADRM_STREAM_CONSTRUCT) {
        if (priv->fence_write && !priv->fence_write->gr2d)
            TegraEXAWaitFence(priv->fence_write);

        if (priv->fence_read && !priv->fence_read->gr2d)
            TegraEXAWaitFence(priv->fence_read);

        tegra_stream_end(&tegra->cmds);
        fence = tegra_stream_submit(&tegra->cmds, true);

        if (priv->fence_write != fence) {
            tegra_stream_put_fence(priv->fence_write);
            priv->fence_write = tegra_stream_ref_fence(fence, &tegra->scratch);
        }
    } else {
        tegra_stream_cleanup(&tegra->cmds);
    }

    TegraEXACoolPixmap(pPixmap, TRUE);

    AccelMsg("\n");
}

Bool TegraEXAPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                         int dx, int dy, int op, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    tegra->scratch.orientation = TEGRA2D_IDENTITY;

    return TegraEXAPrepareCopyExt(pSrcPixmap, pDstPixmap, op, planemask);
}

Bool TegraEXAPrepareCopyExt(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                            int op, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    enum Tegra2DOrientation orientation;
    TegraPixmapPtr priv;
    unsigned int bpp;
    int fr_mode;
    int err;

    AccelMsg("\n");

    orientation = tegra->scratch.orientation;

    if (orientation == TEGRA2D_IDENTITY)
        fr_mode = 0; /* DISABLE */
    else if (pSrcPixmap != pDstPixmap)
        fr_mode = 1; /* SRC_DST_COPY */
    else
        fr_mode = 2; /* SQUARE */

    /*
     * It should be possible to support this, but let's bail for now
     */
    if (planemask != FB_ALLONES) {
        FallbackMsg("unsupported planemask 0x%08lx\n", planemask);
        return FALSE;
    }

    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy) {
        FallbackMsg("unsupported operation %d\n", op);
        return FALSE;
    }

    /*
     * Some restrictions apply to the hardware accelerated copying.
     */
    bpp = pSrcPixmap->drawable.bitsPerPixel;

    if (pDstPixmap->drawable.bitsPerPixel != bpp) {
        FallbackMsg("BPP mismatch %u %u\n",
                    pDstPixmap->drawable.bitsPerPixel, bpp);
        return FALSE;
    }

    TegraEXAThawPixmap(pSrcPixmap, TRUE);
    TegraEXAThawPixmap(pDstPixmap, TRUE);

    priv = exaGetPixmapDriverPrivate(pSrcPixmap);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FallbackMsg("unaccelerateable src pixmap %d:%d\n",
                    pSrcPixmap->drawable.width,
                    pSrcPixmap->drawable.height);
        return FALSE;
    }

    priv = exaGetPixmapDriverPrivate(pDstPixmap);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FallbackMsg("unaccelerateable dst pixmap %d:%d\n",
                    pDstPixmap->drawable.width,
                    pDstPixmap->drawable.height);
        return FALSE;
    }

    err = tegra_stream_begin(&tegra->cmds, tegra->gr2d);
    if (err < 0)
        return FALSE;

    tegra_stream_prep(&tegra->cmds, orientation == TEGRA2D_IDENTITY ? 14 : 12);
    tegra_stream_push_setclass(&tegra->cmds, HOST1X_CLASS_GR2D);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(&tegra->cmds, orientation == TEGRA2D_IDENTITY ?
                                    0x0000003a : 0x00000037 ); /* trigger */
    tegra_stream_push(&tegra->cmds, 0x00000000); /* cmdsel */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x01e, 0x5));
    tegra_stream_push(&tegra->cmds, /* controlsecond */
                      orientation << 26 | fr_mode << 24);
    tegra_stream_push(&tegra->cmds, rop3[op]); /* ropfade */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x046, 1));
    /*
     * [20:20] destination write tile mode (0: linear, 1: tiled)
     * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
     */
    tegra_stream_push(&tegra->cmds, 0x00000000); /* tilemode */

    if (orientation == TEGRA2D_IDENTITY) {
        tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x149));

        tegra_stream_push_reloc(&tegra->cmds, TegraEXAPixmapBO(pDstPixmap),
                                TegraEXAPixmapOffset(pDstPixmap));
        tegra_stream_push(&tegra->cmds,
                          exaGetPixmapPitch(pDstPixmap)); /* dstst */

        tegra_stream_push_reloc(&tegra->cmds, TegraEXAPixmapBO(pSrcPixmap),
                                TegraEXAPixmapOffset(pSrcPixmap));
        tegra_stream_push(&tegra->cmds,
                          exaGetPixmapPitch(pSrcPixmap)); /* srcst */
    } else {
        tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x108));
        tegra_stream_push(&tegra->cmds, exaGetPixmapPitch(pDstPixmap)); /* dstst */
        tegra_stream_push(&tegra->cmds, exaGetPixmapPitch(pSrcPixmap)); /* srcst */
    }

    if (tegra->cmds.status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(&tegra->cmds);
        return FALSE;
    }

    tegra->scratch.pSrc = pSrcPixmap;
    tegra->scratch.srcX = -1;
    tegra->scratch.srcY = -1;
    tegra->scratch.dstX = -1;
    tegra->scratch.dstY = -1;
    tegra->scratch.ops = 0;

    return TRUE;
}

void TegraEXACopyExt(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                     int dstY, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    int tsrcX, tsrcY, tdstX, tdstY, twidth, theight;
    struct drm_tegra_bo * src_bo;
    struct drm_tegra_bo * dst_bo;
    PixmapPtr pSrcPixmap;
    uint32_t controlmain;
    unsigned cell_size;
    unsigned bpp;
    PictVector v;
    BoxRec grid;

    AccelMsg("src %dx%d dst %dx%d w:h %d:%d\n",
             srcX, srcY, dstX, dstY, width, height);

    pSrcPixmap = tegra->scratch.pSrc;
    src_bo     = TegraEXAPixmapBO(pSrcPixmap);
    dst_bo     = TegraEXAPixmapBO(pDstPixmap);
    bpp        = pDstPixmap->drawable.bitsPerPixel;
    cell_size  = 16 / (bpp >> 3);

    /*
     * From TRM 29.2.3 comment to FR_MODE:
     *
     * Source and destination base address must be 128-bit word aligned
     * engine works on FR_BLOCK granularity: transformed surface width in
     * multiples of 16-bytes, transformed surface height in multiples of
     * 16/8/4 lines for bpp8/bpp16/bpp32.
     */

    grid.x1 = TEGRA_ROUND_DOWN(dstX, cell_size);
    grid.y1 = TEGRA_ROUND_DOWN(dstY, cell_size);
    grid.x2 = TEGRA_ROUND_UP(dstX + width,  cell_size);
    grid.y2 = TEGRA_ROUND_UP(dstY + height, cell_size);

    twidth  = grid.x2 - grid.x1;
    theight = grid.y2 - grid.y1;

    tdstX = grid.x1;
    tdstY = grid.y1;

    v.vector[0] = tdstX << 16;
    v.vector[1] = tdstY << 16;
    v.vector[2] = 1 << 16;

    PictureTransformPoint3d(&tegra->scratch.transform, &v);

    tsrcX = v.vector[0] >> 16;
    tsrcY = v.vector[1] >> 16;

    v.vector[0] = twidth  << 16;
    v.vector[1] = theight << 16;
    v.vector[2] = 0;

    PictureTransformPoint3d(&tegra->scratch.transform, &v);

    twidth  = v.vector[0] >> 16;
    theight = v.vector[1] >> 16;

    if (twidth < 0) {
        tsrcX += twidth;
        twidth = -twidth;
    }

    if (theight < 0) {
        tsrcY  += theight;
        theight = -theight;
    }

    /*
     * The copying region may happen to become unaligned after
     * transformation if acceleration-checking missed some case.
     */
    if (!TEGRA_ALIGNED(tsrcX, cell_size) ||
        !TEGRA_ALIGNED(tsrcY, cell_size) ||
        !TEGRA_ALIGNED(twidth, cell_size) ||
        !TEGRA_ALIGNED(theight, cell_size)) {

        ErrorMsg("shouldn't happen %d:%d %d:%d\n",
                 tsrcX, tsrcY, twidth, theight);
        return;
    }

    srcX   = tsrcX;
    srcY   = tsrcY;
    dstX   = tdstX;
    dstY   = tdstY;
    width  = twidth  - 1;
    height = theight - 1;

    tegra_stream_prep(&tegra->cmds, 11);

    if (tegra->scratch.dstX != dstX || tegra->scratch.dstY != dstY) {
        tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x2b, 1));

        tegra_stream_push_reloc(&tegra->cmds, dst_bo,
                                sb_offset(pDstPixmap, dstX, dstY));

        tegra->scratch.dstX = dstX;
        tegra->scratch.dstY = dstY;
    }

    if (tegra->scratch.srcX != srcX || tegra->scratch.srcY != srcY) {
        tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x31, 1));

        tegra_stream_push_reloc(&tegra->cmds, src_bo,
                                sb_offset(pSrcPixmap, srcX, srcY));

        tegra->scratch.srcX = srcX;
        tegra->scratch.srcY = srcY;
    }

    /*
     * [29:29] fast rotate wait for read (0: disable, 1: enable)
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     */
    controlmain = (1 << 29) | (1 << 20) | ((bpp >> 4) << 16);

    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x01f, 1));
    tegra_stream_push(&tegra->cmds, controlmain);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x37, 0x1));
    tegra_stream_push(&tegra->cmds, height << 16 | width); /* srcsize */
    tegra_stream_sync(&tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE);

    tegra->scratch.ops++;
}

void TegraEXACopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                  int dstY, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    uint32_t controlmain;

    AccelMsg("src %dx%d dst %dx%d w:h %d:%d\n",
             srcX, srcY, dstX, dstY, width, height);

    /*
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     * [10:10] y-direction (0: increment, 1: decrement)
     * [9:9] x-direction (0: increment, 1: decrement)
     */
    controlmain = (1 << 20) | ((pDstPixmap->drawable.bitsPerPixel >> 4) << 16);

    if (dstX > srcX) {
        controlmain |= 1 << 9;
        srcX += width - 1;
        dstX += width - 1;
    }

    if (dstY > srcY) {
        controlmain |= 1 << 10;
        srcY += height - 1;
        dstY += height - 1;
    }

    tegra_stream_prep(&tegra->cmds, 7);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_INCR(0x01f, 1));
    tegra_stream_push(&tegra->cmds, controlmain);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_INCR(0x37, 0x4));
    tegra_stream_push(&tegra->cmds, height << 16 | width); /* srcsize */
    tegra_stream_push(&tegra->cmds, height << 16 | width); /* dstsize */
    tegra_stream_push(&tegra->cmds, srcY << 16 | srcX); /* srcps */
    tegra_stream_push(&tegra->cmds, dstY << 16 | dstX); /* dstps */
    tegra_stream_sync(&tegra->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE);

    tegra->scratch.ops++;
}

void TegraEXADoneCopy(PixmapPtr pDstPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *fence;
    TegraPixmapPtr priv;

    if (tegra->scratch.ops && tegra->cmds.status == TEGRADRM_STREAM_CONSTRUCT) {
        if (tegra->scratch.pSrc) {
            priv = exaGetPixmapDriverPrivate(tegra->scratch.pSrc);

            if (priv->fence_write && !priv->fence_write->gr2d) {
                TegraEXAWaitFence(priv->fence_write);

                tegra_stream_put_fence(priv->fence_write);
                priv->fence_write = NULL;
            }
        }

        priv = exaGetPixmapDriverPrivate(pDstPixmap);
        if (priv->fence_write && !priv->fence_write->gr2d)
            TegraEXAWaitFence(priv->fence_write);

        if (priv->fence_read && !priv->fence_read->gr2d)
            TegraEXAWaitFence(priv->fence_read);

        tegra_stream_end(&tegra->cmds);
        fence = tegra_stream_submit(&tegra->cmds, true);

        if (priv->fence_write != fence) {
            tegra_stream_put_fence(priv->fence_write);
            priv->fence_write = tegra_stream_ref_fence(fence, &tegra->scratch);
        }

        if (tegra->scratch.pSrc) {
            priv = exaGetPixmapDriverPrivate(tegra->scratch.pSrc);

            if (priv->fence_read != fence) {
                tegra_stream_put_fence(priv->fence_read);
                priv->fence_read = tegra_stream_ref_fence(fence, &tegra->scratch);
            }
        }
    } else {
        tegra_stream_cleanup(&tegra->cmds);
    }

    TegraEXACoolPixmap(tegra->scratch.pSrc, FALSE);
    TegraEXACoolPixmap(pDstPixmap, TRUE);

    AccelMsg("\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
