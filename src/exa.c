/*
 * Copyright © 2014 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "driver.h"

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s:%d/%s(): " fmt, __FILE__, \
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

static inline unsigned int TegraEXAPitch(unsigned int width, unsigned int bpp)
{
    /*
     * Alignment to 16 bytes isn't strictly necessary for all buffers, but
     * there are cases where X's software rendering fallbacks crash when a
     * buffer's pitch is too small (which happens for very small, low-bpp
     * pixmaps).
     */
    return TEGRA_ALIGN((width * bpp + 7) / 8, 64);
}

static int TegraEXAMarkSync(ScreenPtr pScreen)
{
    /* TODO: implement */

    return 0;
}

static void TegraEXAWaitMarker(ScreenPtr pScreen, int marker)
{
    /* TODO: implement */
}

static Bool TegraEXAPrepareAccess(PixmapPtr pPix, int idx)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPix);
    int err;

    err = drm_tegra_bo_map(priv->bo, &pPix->devPrivate.ptr);
    if (err < 0) {
        ErrorMsg("failed to map buffer object: %d\n", err);
        return FALSE;
    }

    return TRUE;
}

static void TegraEXAFinishAccess(PixmapPtr pPix, int idx)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPix);
    int err;

    err = drm_tegra_bo_unmap(priv->bo);
    if (err < 0)
        ErrorMsg("failed to unmap buffer object: %d\n", err);
}

static Bool TegraEXAPixmapIsOffscreen(PixmapPtr pPix)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPix);

    return priv && priv->bo;
}

static void *TegraEXACreatePixmap2(ScreenPtr pScreen, int width, int height,
                                   int depth, int usage_hint, int bitsPerPixel,
                                   int *new_fb_pitch)
{
    TegraPixmapPtr pixmap;

    pixmap = calloc(1, sizeof(*pixmap));
    if (!pixmap)
        return NULL;

    /*
     * Alignment to 16 bytes isn't strictly necessary for all buffers, but
     * there are cases where X's software rendering fallbacks crash when a
     * buffer's pitch is too small (which happens for very small, low-bpp
     * pixmaps).
     */
    *new_fb_pitch = TegraEXAPitch(width, bitsPerPixel);

    if (usage_hint == TEGRA_DRI_USAGE_HINT)
        pixmap->dri = TRUE;

    return pixmap;
}

static void TegraEXADestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
    TegraPixmapPtr priv = driverPriv;

    drm_tegra_bo_unref(priv->bo);
    free(priv->fallback);
    free(priv);
}

static Bool TegraEXAModifyPixmapHeader(PixmapPtr pPixmap, int width,
                                       int height, int depth, int bitsPerPixel,
                                       int devKind, pointer pPixData)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    TegraPtr tegra = TegraPTR(pScrn);
    unsigned int bpp, size;
    Bool ret;
    int err;

    ret = miModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel,
                               devKind, pPixData);
    if (!ret)
        return ret;

    drm_tegra_bo_unref(priv->bo);
    free(priv->fallback);

    priv->bo = NULL;
    priv->fallback = NULL;

    if (pPixData) {
        void *scanout;

        scanout = drmmode_map_front_bo(&tegra->drmmode);

        if (pPixData == scanout) {
            priv->bo = drmmode_get_front_bo(&tegra->drmmode);
            return TRUE;
        }

        /*
         * The pixmap can't be used for hardware acceleration, so dispose of
         * it.
         */
        pPixmap->devPrivate.ptr = pPixData;
        pPixmap->devKind = devKind;

        return FALSE;
    }

    width = pPixmap->drawable.width;
    height = pPixmap->drawable.height;
    depth = pPixmap->drawable.depth;
    bpp = pPixmap->drawable.bitsPerPixel;
    pPixmap->devKind = TegraEXAPitch(width, bpp);
    size = pPixmap->devKind * height;

    if (!size)
        return FALSE;

    if (!priv->bo) {
        err = drm_tegra_bo_new(&priv->bo, tegra->drm, 0, size);
        if (err < 0) {
            if (!priv->dri)
                priv->fallback = malloc(size);

            ErrorMsg("failed to allocate %ux%u (%zu) buffer object: %d, "
                     "fallback allocation %s\n",
                     width, height, size, err,
                     priv->fallback ? "succeed" : "failed");

            if (!priv->fallback)
                return FALSE;
        }
    }

    pPixmap->devPrivate.ptr = priv->fallback;

    return TRUE;
}

static Bool TegraEXAPrepareSolid(PixmapPtr pPixmap, int op, Pixel planemask,
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
    if (planemask != FB_ALLONES)
        return FALSE;

    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy)
        return FALSE;

    if (bpp != 32 && bpp != 16 && bpp != 8)
        return FALSE;

    err = tegra_stream_begin(&tegra->cmds);
    if (err < 0)
            return FALSE;

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
    tegra_stream_push_reloc(&tegra->cmds, priv->bo,
                            exaGetPixmapOffset(pPixmap));
    tegra_stream_push(&tegra->cmds, exaGetPixmapPitch(pPixmap));
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x46, 1));
    tegra_stream_push(&tegra->cmds, 0); /* non-tiled */

    if (tegra->cmds.status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(&tegra->cmds);
        return FALSE;
    }

    return TRUE;
}

static void TegraEXASolid(PixmapPtr pPixmap,
                          int px1, int py1, int px2, int py2)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    tegra_stream_prep(&tegra->cmds, 3);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x38, 0x5));
    tegra_stream_push(&tegra->cmds, (py2 - py1) << 16 | (px2 - px1));
    tegra_stream_push(&tegra->cmds, py1 << 16 | px1);
}

static void TegraEXADoneSolid(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    tegra_stream_end(&tegra->cmds);
    tegra_stream_flush(&tegra->cmds);
}

static Bool TegraEXAPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                                int dx, int dy, int op, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraPixmapPtr src = exaGetPixmapDriverPrivate(pSrcPixmap);
    TegraPixmapPtr dst = exaGetPixmapDriverPrivate(pDstPixmap);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    unsigned int bpp;
    int err;

    /*
     * It should be possible to support this, but let's bail for now
     */
    if (planemask != FB_ALLONES)
        return FALSE;

    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy)
        return FALSE;

    /*
     * Some restrictions apply to the hardware accelerated copying.
     */
    bpp = pSrcPixmap->drawable.bitsPerPixel;

    if (bpp != 32 && bpp != 16 && bpp != 8)
        return FALSE;

    if (pDstPixmap->drawable.bitsPerPixel != bpp)
        return FALSE;

    err = tegra_stream_begin(&tegra->cmds);
    if (err < 0)
            return FALSE;

    tegra_stream_push_setclass(&tegra->cmds, HOST1X_CLASS_GR2D);
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(&tegra->cmds, 0x0000003a); /* trigger */
    tegra_stream_push(&tegra->cmds, 0x00000000); /* cmdsel */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x01e, 0x5));
    tegra_stream_push(&tegra->cmds, 0x00000000); /* controlsecond */
    tegra_stream_push(&tegra->cmds, rop3[op]); /* ropfade */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_NONINCR(0x046, 1));
    /*
     * [20:20] destination write tile mode (0: linear, 1: tiled)
     * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
     */
    tegra_stream_push(&tegra->cmds, 0x00000000); /* tilemode */
    tegra_stream_push(&tegra->cmds, HOST1X_OPCODE_MASK(0x2b, 0x149));

    tegra_stream_push_reloc(&tegra->cmds, dst->bo,
                            exaGetPixmapOffset(pDstPixmap));
    tegra_stream_push(&tegra->cmds,
                      exaGetPixmapPitch(pDstPixmap)); /* dstst */

    tegra_stream_push_reloc(&tegra->cmds, src->bo,
                            exaGetPixmapOffset(pSrcPixmap));
    tegra_stream_push(&tegra->cmds,
                      exaGetPixmapPitch(pSrcPixmap)); /* srcst */

    if (tegra->cmds.status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(&tegra->cmds);
        return FALSE;
    }

    return TRUE;
}

static void TegraEXACopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                         int dstY, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    uint32_t controlmain;

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
}

static void TegraEXADoneCopy(PixmapPtr pDstPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    tegra_stream_end(&tegra->cmds);
    tegra_stream_flush(&tegra->cmds);
}

static Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                                   PicturePtr pMaskPicture,
                                   PicturePtr pDstPicture)
{
    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy)
        return FALSE;

    /* TODO: implement */
    return FALSE;
}

static Bool TegraEXAPrepareComposite(int op, PicturePtr pSrcPicture,
                                     PicturePtr pMaskPicture,
                                     PicturePtr pDstPicture, PixmapPtr pSrc,
                                     PixmapPtr pMask, PixmapPtr pDst)
{
    /*
     * It should be possible to support all GX* raster operations given the
     * mapping in the rop3 table, but none other than GXcopy have been
     * validated.
     */
    if (op != GXcopy)
        return FALSE;

    /* TODO: implement */
    return FALSE;
}

static void TegraEXAComposite(PixmapPtr pDst, int srcX, int srcY, int maskX,
                              int maskY, int dstX, int dstY, int width,
                              int height)
{
    /* TODO: implement */
}

static void TegraEXADoneComposite(PixmapPtr pDst)
{
    /* TODO: implement */
}

static Bool
TegraEXADownloadFromScreen(PixmapPtr pSrc, int x, int y, int w, int h,
                           char *dst, int pitch)
{
    return FALSE;
}

void TegraEXAScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    ExaDriverPtr exa;
    TegraEXAPtr priv;
    int err;

    if (tegra->drmmode.shadow_enable) {
        ErrorMsg("using \"Shadow Framebuffer\" - acceleration disabled\n");
        return;
    }

    exa = exaDriverAlloc();
    if (!exa) {
        ErrorMsg("EXA allocation failed\n");
        return;
    }

    priv = calloc(1, sizeof(*priv));
    if (!priv) {
        ErrorMsg("EXA allocation failed\n");
        goto free_exa;
    }

    err = drm_tegra_channel_open(&priv->gr2d, tegra->drm, DRM_TEGRA_GR2D);
    if (err < 0) {
        ErrorMsg("failed to open 2D channel: %d\n", err);
        goto free_priv;
    }

    err = tegra_stream_create(tegra->drm, priv->gr2d, &priv->cmds, 1);
    if (err < 0) {
        ErrorMsg("failed to create command stream: %d\n", err);
        goto close_gr2d;
    }

    exa->exa_major = EXA_VERSION_MAJOR;
    exa->exa_minor = EXA_VERSION_MINOR;
    exa->pixmapOffsetAlign = 256;
    exa->pixmapPitchAlign = 64;
    exa->flags = EXA_SUPPORTS_PREPARE_AUX |
                 EXA_OFFSCREEN_PIXMAPS |
                 EXA_HANDLES_PIXMAPS;

    exa->maxX = 8192;
    exa->maxY = 8192;

    exa->MarkSync = TegraEXAMarkSync;
    exa->WaitMarker = TegraEXAWaitMarker;

    exa->PrepareAccess = TegraEXAPrepareAccess;
    exa->FinishAccess = TegraEXAFinishAccess;
    exa->PixmapIsOffscreen = TegraEXAPixmapIsOffscreen;

    exa->CreatePixmap2 = TegraEXACreatePixmap2;
    exa->DestroyPixmap = TegraEXADestroyPixmap;
    exa->ModifyPixmapHeader = TegraEXAModifyPixmapHeader;

    exa->PrepareSolid = TegraEXAPrepareSolid;
    exa->Solid = TegraEXASolid;
    exa->DoneSolid = TegraEXADoneSolid;

    exa->PrepareCopy = TegraEXAPrepareCopy;
    exa->Copy = TegraEXACopy;
    exa->DoneCopy = TegraEXADoneCopy;

    exa->CheckComposite = TegraEXACheckComposite;
    exa->PrepareComposite = TegraEXAPrepareComposite;
    exa->Composite = TegraEXAComposite;
    exa->DoneComposite = TegraEXADoneComposite;

    exa->DownloadFromScreen = TegraEXADownloadFromScreen;

    if (!exaDriverInit(pScreen, exa)) {
        ErrorMsg("EXA initialization failed\n");
        goto destroy_stream;
    }

    priv->driver = exa;
    tegra->exa = priv;

    return;

destroy_stream:
    tegra_stream_destroy(&priv->cmds);
close_gr2d:
    drm_tegra_channel_close(priv->gr2d);
free_priv:
    free(priv);
free_exa:
    free(exa);
}

void TegraEXAScreenExit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    TegraEXAPtr priv = tegra->exa;

    if (priv) {
        exaDriverFini(pScreen);
        free(priv->driver);

        tegra_stream_destroy(&priv->cmds);
        drm_tegra_channel_close(priv->gr2d);
        free(priv);
    }
}

/* vim: set et sts=4 sw=4 ts=4: */
