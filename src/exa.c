#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "xf86.h"

#include "driver.h"

static int TegraEXAMarkSync(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    int ret = 0;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__,
               pScreen);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void TegraEXAWaitMarker(ScreenPtr pScreen, int marker)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p, marker=%d)\n",
               __func__, pScreen, marker);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static Bool TegraEXAPrepareAccess(PixmapPtr pPix, int index)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPix=%p, index=%d)\n",
               __func__, pPix, index);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void TegraEXAFinishAccess(PixmapPtr pPix, int index)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPix=%p, index=%d)\n",
               __func__, pPix, index);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static Bool TegraEXAPixmapIsOffscreen(PixmapPtr pPix)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPix=%p)\n", __func__, pPix);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void *TegraEXACreatePixmap2(ScreenPtr pScreen, int width, int height,
                                   int depth, int usage_hint, int bitsPerPixel,
                                   int *new_fb_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    void *ret = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p, width=%d, height=%d, depth=%d, usage_hint=%d, bitsPerPixel=%d, new_fb_pitch=%p)\n",
               __func__, pScreen, width, height, depth, usage_hint,
               bitsPerPixel, new_fb_pitch);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %p\n", __func__, ret);
    return ret;
}

static void TegraEXADestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p, driverPriv=%p)\n",
               __func__, pScreen, driverPriv);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static Bool TegraEXAModifyPixmapHeader(PixmapPtr pPixmap, int width,
                                       int height, int depth, int bitsPerPixel,
                                       int devKind, pointer pPixData)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPixmap=%p, width=%d, height=%d, depth=%d, bitsPerPixel=%d, devKind=%d, pPixData=%p)\n",
               __func__, pPixmap, width, height, depth, bitsPerPixel, devKind,
               pPixData);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static Bool TegraEXAPrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask,
                                 Pixel fg)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPixmap=%p, alu=%x, planemask=%" FMT_CARD32 ", color=%" FMT_CARD32 ")\n",
               __func__, pPixmap, alu, planemask, fg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void TegraEXASolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPixmap=%p, x1=%d, y1=%d, x2=%d, y2=%d)\n",
               __func__, pPixmap, x1, y1, x2, y2);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static void TegraEXADoneSolid(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pPixmap=%p)\n", __func__,
               pPixmap);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static Bool TegraEXAPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                                int dx, int dy, int alu, Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pSrcPixmap=%p, pDstPixmap=%p, dx=%d, dy=%d, alu=%x, planemask=%" FMT_CARD32 ")\n",
               __func__, pSrcPixmap, pDstPixmap, dx, dy, alu, planemask);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void TegraEXACopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                         int dstY, int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pDstPixmap=%p, srcX=%d, srcY=%d, dstX=%d, dstY=%d, width=%d, height=%d)\n",
               __func__, pDstPixmap, srcX, srcY, dstX, dstY, width, height);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static void TegraEXADoneCopy(PixmapPtr pDstPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pDstPixmap=%p)\n", __func__,
               pDstPixmap);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                                   PicturePtr pMaskPicture,
                                   PicturePtr pDstPicture)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPicture->pDrawable->pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(op=%d, pSrcPicture=%p, pMaskPicture=%p, pDstPicture=%p)\n",
               __func__, op, pSrcPicture, pMaskPicture, pDstPicture);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static Bool TegraEXAPrepareComposite(int op, PicturePtr pSrcPicture,
                                     PicturePtr pMaskPicture,
                                     PicturePtr pDstPicture, PixmapPtr pSrc,
                                     PixmapPtr pMask, PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    Bool ret = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(op=%d, pSrcPicture=%p, pMaskPicture=%p, pDstPicture=%p, pSrc=%p, pMask=%p, pDst=%p)\n",
               __func__, op, pSrcPicture, pMaskPicture, pDstPicture, pSrc,
               pMask, pDst);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static void TegraEXAComposite(PixmapPtr pDst, int srcX, int srcY, int maskX,
                              int maskY, int dstX, int dstY, int width,
                              int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pDst=%p, srcX=%d, srcY=%d, maskX=%d, maskY=%d, dstX=%d, dstY=%d, width=%d, height=%d)\n",
               __func__, pDst, srcX, srcY, maskX, maskY, dstX, dstY, width,
               height);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static void TegraEXADoneComposite(PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pDst=%p)\n", __func__, pDst);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

void TegraEXAScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    ExaDriverPtr exa;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__,
               pScreen);

    exa = exaDriverAlloc();
    if (!exa) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EXA allocation failed\n");
        return;
    }

    exa->exa_major = EXA_VERSION_MAJOR;
    exa->exa_minor = EXA_VERSION_MINOR;
    exa->pixmapOffsetAlign = 256;
    exa->pixmapPitchAlign = 64;
    exa->flags = EXA_OFFSCREEN_PIXMAPS |
                 EXA_HANDLES_PIXMAPS |
                 EXA_MIXED_PIXMAPS;

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

    if (!exaDriverInit(pScreen, exa)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EXA initialization failed\n");
        return;
    }

    tegra->exa = exa;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

void TegraEXAScreenExit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__,
               pScreen);

    if (tegra->exa) {
        exaDriverFini(pScreen);
        free(tegra->exa);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}
