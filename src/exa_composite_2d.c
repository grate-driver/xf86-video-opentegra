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

#define TegraSwapRedBlue(v)                                                 \
    ((v & 0xff00ff00) | (v & 0x00ff0000) >> 16 | (v & 0x000000ff) << 16)

static Bool
TegraEXA2DTransformIsSupported(int32_t dw, int32_t dh,
                               int32_t sw, int32_t sh,
                               unsigned bpp, PictTransformPtr t,
                               enum Tegra2DOrientation *orientation)
{
    unsigned int i, k;
    int16_t e[4];

    /*
     * GR2D performs transformation using 16x16 bytes buffer and its
     * Fast Rotate (FR) hardware unit doesn't clip / re-align written data.
     *
     * TODO: support 3d / 2d transformation mix or check somehow that
     * the actual transformed area (EXA doesn't provide that information)
     * conforms the requirement.
     */
    if (!TEGRA_ALIGNED(dw * bpp / 8, 16) ||
        !TEGRA_ALIGNED(dh * bpp / 8, 16) ||
        !TEGRA_ALIGNED(sw * bpp / 8, 16) ||
        !TEGRA_ALIGNED(sh * bpp / 8, 16))
        return FALSE;

    /* FR limitation */
    if (dw > 4096 || dh > 4096 || sw > 4096 || sh > 4096) {
        FallbackMsg("FR limitation\n");
        return FALSE;
    }

    /* check whether matrix contains only integer values */
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 2; k++) {
            /* s16.16 format */
            if (t->matrix[i][k] & 0xffff) {
                FallbackMsg("non-integer transform\n");
                return FALSE;
            }
        }
    }

    e[0] = pixman_fixed_to_int(t->matrix[0][0]);
    e[1] = pixman_fixed_to_int(t->matrix[0][1]);
    e[2] = pixman_fixed_to_int(t->matrix[1][0]);
    e[3] = pixman_fixed_to_int(t->matrix[1][1]);

    if (e[0] == 0 && e[1] == -1 && e[2] == 1 && e[3] == 0) {
        *orientation = TEGRA2D_ROT_90;
        return TRUE;
    }

    if (e[0] == -1 && e[1] == 0 && e[2] == 0 && e[3] == -1) {
        *orientation = TEGRA2D_ROT_180;
        return TRUE;
    }

    if (e[0] == 0 && e[1] == 1 && e[2] == -1 && e[3] == 0) {
        *orientation = TEGRA2D_ROT_270;
        return TRUE;
    }

    if (e[0] == -1 && e[1] == 0 && e[2] == 0 && e[3] == 1) {
        *orientation = TEGRA2D_FLIP_X;
        return TRUE;
    }

    if (e[0] == 1 && e[1] == 0 && e[2] == 0 && e[3] == -1) {
        *orientation = TEGRA2D_FLIP_Y;
        return TRUE;
    }

    /*
     * Transposing currently unimplemented (no real use-case),
     * More complex transformations (like scaling, skewing) can't be done
     * on GR2D.
     */
    FallbackMsg("complex transform\n");
    return FALSE;
}

static Bool TegraEXACheckComposite2D(int op, PicturePtr pSrcPicture,
                                     PicturePtr pMaskPicture,
                                     PicturePtr pDstPicture)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPicture->pDrawable->pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    enum Tegra2DOrientation orientation;

    if (op != PictOpSrc && op != PictOpClear)
        return FALSE;

    if (pSrcPicture && pSrcPicture->pDrawable) {
        if (op != PictOpSrc)
            return FALSE;

        if (!pSrcPicture->transform)
            return FALSE;

        if (pMaskPicture)
            return FALSE;

        if (pSrcPicture->pDrawable->bitsPerPixel !=
            pDstPicture->pDrawable->bitsPerPixel)
            return FALSE;

        if (!TegraEXA2DTransformIsSupported(pDstPicture->pDrawable->width,
                                            pDstPicture->pDrawable->height,
                                            pSrcPicture->pDrawable->width,
                                            pSrcPicture->pDrawable->height,
                                            pDstPicture->pDrawable->bitsPerPixel,
                                            pSrcPicture->transform, &orientation))
            return FALSE;

        tegra->scratch.orientation = orientation;
    } else {
        if (pSrcPicture &&
            pSrcPicture->pSourcePict->type != SourcePictTypeSolidFill)
            return FALSE;

        if (pSrcPicture && pSrcPicture->transform)
            return FALSE;

        if (pMaskPicture && pMaskPicture->pDrawable)
            return FALSE;

        if (pMaskPicture &&
            pMaskPicture->pSourcePict->type != SourcePictTypeSolidFill)
            return FALSE;

        if (pMaskPicture && pMaskPicture->transform)
            return FALSE;
    }

    return TRUE;
}

static Bool TegraEXAPrepareSolid2D(int op,
                                   PicturePtr pSrcPicture,
                                   PicturePtr pMaskPicture,
                                   PicturePtr pDstPicture,
                                   PixmapPtr pDst)
{
    Pixel solid;
    Bool alpha;

    if (pSrcPicture && pSrcPicture->pDrawable)
        return FALSE;

    if (pMaskPicture)
        return FALSE;

    if (op == PictOpSrc) {
        solid = pSrcPicture ? pSrcPicture->pSourcePict->solidFill.color :
                              0x00000000;

        if (pSrcPicture) {
            if (pSrcPicture->format != pDstPicture->format)
                return FALSE;
        }

        alpha = TegraCompositeFormatHasAlpha(pSrcPicture->format);
        if (!alpha)
            solid |= 0xff000000;

        alpha = TegraCompositeFormatHasAlpha(pDstPicture->format);
        if (!alpha)
            solid &= 0x00ffffff;

        if (!TegraEXAPrepareSolid(pDst, GXcopy, FB_ALLONES, solid))
            return FALSE;

        return TRUE;
    }

    if (op == PictOpClear) {
        if (!TegraEXAPrepareSolid(pDst, GXcopy, FB_ALLONES, 0x00000000))
            return FALSE;

        return TRUE;
    }

    return FALSE;
}

static Bool TegraEXAPrepareCopy2DRotate(int op,
                                        PicturePtr pSrcPicture,
                                        PicturePtr pMaskPicture,
                                        PicturePtr pDstPicture,
                                        PixmapPtr pSrc,
                                        PixmapPtr pDst)
{
    ScrnInfoPtr pScrn             = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra             = TegraPTR(pScrn)->exa;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    TegraPixmapPtr priv_src, priv_dst;
    xf86CrtcPtr crtc;

    if (pMaskPicture)
        return FALSE;

    if (op != PictOpSrc)
        return FALSE;

    if (!pSrcPicture ||
            !pSrcPicture->pDrawable ||
                !pSrcPicture->transform)
        return FALSE;

    priv_src = exaGetPixmapDriverPrivate(pSrc);
    priv_dst = exaGetPixmapDriverPrivate(pDst);

    /*
     * Only predictable transformations are supported via GR2D due to its
     * memory addressing limitations.
     */
    if (!priv_src->scanout || !priv_dst->scanout_rotated)
        return FALSE;

    /* destination is rotated in terms of Xorg display rotation */
    crtc = xf86_config->crtc[priv_dst->crtc];

    /* coordinates may become unaligned due to display's panning */
    if (!TEGRA_ALIGNED(crtc->x + pDst->drawable.width, 4) ||
        !TEGRA_ALIGNED(crtc->y + pDst->drawable.height, 4))
        return FALSE;

    tegra->scratch.transform = *pSrcPicture->transform;

    return TegraEXAPrepareCopyExt(pSrc, pDst, GXcopy, FB_ALLONES);
}

static Bool TegraEXAPrepareComposite2D(int op,
                                       PicturePtr pSrcPicture,
                                       PicturePtr pMaskPicture,
                                       PicturePtr pDstPicture,
                                       PixmapPtr pSrc,
                                       PixmapPtr pMask,
                                       PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    tegra->scratch.op2d = TEGRA2D_NONE;

    if (TegraEXAPrepareSolid2D(op, pSrcPicture, pMaskPicture, pDstPicture,
                               pDst)) {
        tegra->scratch.op2d = TEGRA2D_SOLID;
        return TRUE;
    }

    if (TegraEXAPrepareCopy2DRotate(op, pSrcPicture, pMaskPicture, pDstPicture,
                                    pSrc, pDst)) {
        tegra->scratch.op2d = TEGRA2D_COPY;
        return TRUE;
    }

    return FALSE;
}

/* vim: set et sts=4 sw=4 ts=4: */
