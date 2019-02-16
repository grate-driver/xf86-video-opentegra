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

Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                            PicturePtr pMaskPicture,
                            PicturePtr pDstPicture)
{
    if (TegraEXACheckComposite2D(op, pSrcPicture, pMaskPicture, pDstPicture))
        return TRUE;

    if (TegraEXACheckComposite3D(op, pSrcPicture, pMaskPicture, pDstPicture))
        return TRUE;

    FallbackMsg("\n");

    return FALSE;
}

Bool TegraEXAPrepareComposite(int op, PicturePtr pSrcPicture,
                              PicturePtr pMaskPicture,
                              PicturePtr pDstPicture,
                              PixmapPtr pSrc,
                              PixmapPtr pMask,
                              PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    /* Use GR2D for simple solid fills as usually it is more optimal. */
    if (TegraEXAPrepareComposite2D(op, pSrcPicture, pMaskPicture,
                                   pDstPicture, pSrc, pMask, pDst))
        return TRUE;

    if (!tegra->exa_compositing)
        return FALSE;

    if (TegraEXAPrepareComposite3D(op, pSrcPicture, pMaskPicture,
                                   pDstPicture, pSrc, pMask, pDst))
        return TRUE;

    FallbackMsg("\n");

    return FALSE;
}

void TegraEXAComposite(PixmapPtr pDst,
                       int srcX, int srcY,
                       int maskX, int maskY,
                       int dstX, int dstY,
                       int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        return TegraEXASolid(pDst, dstX, dstY, dstX + width, dstY + height);

    if (tegra->scratch.op2d == TEGRA2D_COPY)
        return TegraEXACopyExt(pDst, srcX, srcY, dstX, dstY, width, height);

    return TegraEXAComposite3D(pDst, srcX, srcY, maskX, maskY, dstX, dstY,
                               width, height);
}

void TegraEXADoneComposite(PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        return TegraEXADoneSolid(pDst);

    if (tegra->scratch.op2d == TEGRA2D_COPY)
        return TegraEXADoneCopy(pDst);

    return TegraEXADoneComposite3D(pDst);
}

/* vim: set et sts=4 sw=4 ts=4: */
