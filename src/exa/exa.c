/*
 * Copyright © 2014 NVIDIA Corporation
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

#include "tegra_exa.c"

static int TegraEXAMarkSync(ScreenPtr pScreen)
{
    /*
     * The EXA markers are supposed to be used for fencing CPU accesses,
     * but we have our own fencing for CPU accesses, and thus, the EXA
     * markers aren't really needed.
     */
    return 0;
}

static void TegraEXAWaitMarker(ScreenPtr pScreen, int marker)
{
}

static void *TegraEXACreatePixmap2(ScreenPtr pScreen, int width, int height,
                                   int depth, int usage_hint, int bpp,
                                   int *new_fb_pitch)
{
    return tegra_exa_create_pixmap(pScreen, width, height, depth, usage_hint,
                                   bpp, new_fb_pitch);
}

static void TegraEXADestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
    tegra_exa_destroy_pixmap(driverPriv);
}

static Bool TegraEXAModifyPixmapHeader(PixmapPtr pixmap, int width,
                                       int height, int depth, int bpp,
                                       int devKind, pointer pix_data)
{
    return tegra_exa_modify_pixmap_header(pixmap, width, height, depth,
                                          bpp, devKind, pix_data);
}

static Bool TegraEXAPrepareAccess(PixmapPtr pix, int idx)
{
    return tegra_exa_prepare_cpu_access(pix, idx, &pix->devPrivate.ptr, true);
}

static void TegraEXAFinishAccess(PixmapPtr pix, int idx)
{
    tegra_exa_finish_cpu_access(pix, idx);
}

static Bool TegraEXAPixmapIsOffscreen(PixmapPtr pixmap)
{
    return tegra_exa_pixmap_is_offscreen(pixmap);
}

static Bool TegraEXADownloadFromScreen(PixmapPtr pSrc,
                                       int x, int y, int w, int h,
                                       char *dst, int dst_pitch)
{
    return tegra_exa_load_screen(pSrc, x, y, w, h, dst, dst_pitch, true);
}

static Bool TegraEXAUploadToScreen(PixmapPtr pDst,
                                   int x, int y, int w, int h,
                                   char *src, int src_pitch)
{
    return tegra_exa_load_screen(pDst, x, y, w, h, src, src_pitch, false);
}

static Bool TegraEXAPrepareSolid(PixmapPtr pixmap, int op, Pixel planemask,
                                 Pixel color)
{
    return tegra_exa_prepare_solid_2d(pixmap, op, planemask, color);
}

static void TegraEXASolid(PixmapPtr pixmap, int px1, int py1, int px2, int py2)
{
    tegra_exa_solid_2d(pixmap, px1, py1, px2, py2);
}

static void TegraEXADoneSolid(PixmapPtr pixmap)
{
    tegra_exa_done_solid_2d(pixmap);
}

static Bool TegraEXAPrepareCopy(PixmapPtr src_pixmap, PixmapPtr dst_pixmap,
                                int dx, int dy, int op, Pixel planemask)
{
    return tegra_exa_prepare_copy_2d(src_pixmap, dst_pixmap, dx, dy, op,
                                     planemask);
}

static void TegraEXACopy(PixmapPtr dst_pixmap, int srcX, int srcY,
                        int dstX, int dstY, int width, int height)
{
    tegra_exa_copy_2d(dst_pixmap, srcX, srcY, dstX, dstY, width, height);
}

static void TegraEXADoneCopy(PixmapPtr dst_pixmap)
{
    tegra_exa_done_copy_2d(dst_pixmap);
}

static Bool TegraEXACheckComposite(int op,
                                   PicturePtr src_picture,
                                   PicturePtr mask_picture,
                                   PicturePtr dst_picture)
{
    return tegra_exa_check_composite(op, src_picture, mask_picture, dst_picture);
}

static Bool TegraEXAPrepareComposite(int op,
                                     PicturePtr src_picture,
                                     PicturePtr mask_picture,
                                     PicturePtr dst_picture,
                                     PixmapPtr src,
                                     PixmapPtr mask,
                                     PixmapPtr dst)
{
    return tegra_exa_prepare_composite(op, src_picture, mask_picture, dst_picture,
                                       src, mask, dst);
}

static void TegraEXAComposite(PixmapPtr dst,
                              int src_x, int src_y,
                              int mask_x, int mask_y,
                              int dst_x, int dst_y,
                              int width, int height)
{
    tegra_exa_composite(dst, src_x, src_y, mask_x, mask_y, dst_x, dst_y,
                        width, height);
}

static void TegraEXADoneComposite(PixmapPtr pDst)
{
    tegra_exa_done_composite(pDst);
}

Bool TegraEXAScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    int err;

    assert(!tegra->exa_driver);

    tegra->exa_driver = exaDriverAlloc();
    if (!tegra->exa_driver) {
        ERROR_MSG("EXA allocation failed\n");
        return FALSE;
    }

    tegra->exa_driver->exa_major            = EXA_VERSION_MAJOR;
    tegra->exa_driver->exa_minor            = EXA_VERSION_MINOR;

    tegra->exa_driver->pixmapOffsetAlign    = TEGRA_EXA_OFFSET_ALIGN;
    tegra->exa_driver->pixmapPitchAlign     = tegra_hw_pitch(1, 1, 32);

    tegra->exa_driver->flags                = EXA_SUPPORTS_PREPARE_AUX |
                                              EXA_OFFSCREEN_PIXMAPS |
                                              EXA_HANDLES_PIXMAPS;

    tegra->exa_driver->maxX                 = 8192;
    tegra->exa_driver->maxY                 = 8192;

    tegra->exa_driver->MarkSync             = TegraEXAMarkSync;
    tegra->exa_driver->WaitMarker           = TegraEXAWaitMarker;

    tegra->exa_driver->PrepareAccess        = TegraEXAPrepareAccess;
    tegra->exa_driver->FinishAccess         = TegraEXAFinishAccess;
    tegra->exa_driver->PixmapIsOffscreen    = TegraEXAPixmapIsOffscreen;

    tegra->exa_driver->CreatePixmap2        = TegraEXACreatePixmap2;
    tegra->exa_driver->DestroyPixmap        = TegraEXADestroyPixmap;
    tegra->exa_driver->ModifyPixmapHeader   = TegraEXAModifyPixmapHeader;

    tegra->exa_driver->PrepareSolid         = TegraEXAPrepareSolid;
    tegra->exa_driver->Solid                = TegraEXASolid;
    tegra->exa_driver->DoneSolid            = TegraEXADoneSolid;

    tegra->exa_driver->PrepareCopy          = TegraEXAPrepareCopy;
    tegra->exa_driver->Copy                 = TegraEXACopy;
    tegra->exa_driver->DoneCopy             = TegraEXADoneCopy;

    tegra->exa_driver->CheckComposite       = TegraEXACheckComposite;
    tegra->exa_driver->PrepareComposite     = TegraEXAPrepareComposite;
    tegra->exa_driver->Composite            = TegraEXAComposite;
    tegra->exa_driver->DoneComposite        = TegraEXADoneComposite;

    tegra->exa_driver->DownloadFromScreen   = TegraEXADownloadFromScreen;
    tegra->exa_driver->UploadToScreen       = TegraEXAUploadToScreen;

    err = tegra_exa_preinit(pScreen);
    if (err)
        goto err_free;

    if (!exaDriverInit(pScreen, tegra->exa_driver)) {
        ERROR_MSG("EXA initialization failed\n");
        goto err_deinit;
    }

    tegra_exa_finalize_init(pScreen);

    INFO_MSG(pScrn, "EXA initialized\n");

    return TRUE;

err_deinit:
    tegra_exa_deinit(pScreen);
err_free:
    free(tegra->exa_driver);

    tegra->exa_driver = NULL;

    return FALSE;
}

void TegraEXAScreenExit(ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(scrn);

    if (tegra->exa_driver) {
        exaDriverFini(pScreen);
        tegra_exa_deinit(pScreen);

        free(tegra->exa_driver);
        tegra->exa_driver = NULL;
        tegra->exa = NULL;
    }
}

/* vim: set et sts=4 sw=4 ts=4: */
