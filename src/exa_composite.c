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

PROFILE_DEF

static __maybe_unused char const * op_name(int op)
{
    switch (op) {
        case PictOpAtopReverse: return "PictOpAtopReverse";
        case PictOpOverReverse: return "PictOpOverReverse";
        case PictOpOutReverse: return "PictOpOutReverse";
        case PictOpInReverse: return "PictOpInReverse";
        case PictOpSaturate: return "PictOpSaturate";
        case PictOpClear: return "PictOpClear";
        case PictOpOver: return "PictOpOver";
        case PictOpAtop: return "PictOpAtop";
        case PictOpAdd: return "PictOpAdd";
        case PictOpSrc: return "PictOpSrc";
        case PictOpOut: return "PictOpOut";
        case PictOpDst: return "PictOpDst";
        case PictOpXor: return "PictOpXor";
        case PictOpIn: return "PictOpIn";

        default: return "???";
    }
}

static __maybe_unused char const * pict_format(int fmt)
{
    switch (fmt) {
        case PICT_x8r8g8b8: return "PICT_x8r8g8b8";
        case PICT_a8r8g8b8: return "PICT_a8r8g8b8";
        case PICT_x8b8g8r8: return "PICT_x8b8g8r8";
        case PICT_a8b8g8r8: return "PICT_a8b8g8r8";
        case PICT_r5g6b5: return "PICT_r5g6b5";
        case PICT_b5g6r5: return "PICT_b5g6r5";
        case PICT_a8: return "PICT_a8";

        default: return "???";
    }
}

static __maybe_unused char const * pict_repeat(int type)
{
    switch (type) {
        case RepeatPad: return "Pad";
        case RepeatNone: return "None";
        case RepeatNormal: return "Normal";
        case RepeatReflect: return "Reflect";

        default: return "???";
    }
}

static __maybe_unused char const * pict_type(PicturePtr pict)
{
    if (pict->pDrawable)
        return "Drawable";

    if (pict->pSourcePict->type == SourcePictTypeSolidFill)
        return "Solid";

    if (pict->pSourcePict->type == SourcePictTypeLinear)
        return "Linear";

    if (pict->pSourcePict->type == SourcePictTypeRadial)
        return "Radial";

    if (pict->pSourcePict->type == SourcePictTypeConical)
        return "Conical";

    return "???";
}

static __maybe_unused int pict_width(PicturePtr pict)
{
    if (pict->pDrawable)
        return pict->pDrawable->width;

    return 0;
}

static __maybe_unused int pict_height(PicturePtr pict)
{
    if (pict->pDrawable)
        return pict->pDrawable->height;

    return 0;
}

static __maybe_unused char const * pict_transform(PicturePtr pict)
{
    static char buf[256];

    if (!pict->pDrawable || !pict->transform)
        return "No";

    sprintf(buf, "Yes (%f:%f:%f  %f:%f:%f  %f:%f:%f)",
            pixman_fixed_to_double(pict->transform->matrix[0][0]),
            pixman_fixed_to_double(pict->transform->matrix[0][1]),
            pixman_fixed_to_double(pict->transform->matrix[0][2]),
            pixman_fixed_to_double(pict->transform->matrix[1][0]),
            pixman_fixed_to_double(pict->transform->matrix[1][1]),
            pixman_fixed_to_double(pict->transform->matrix[1][2]),
            pixman_fixed_to_double(pict->transform->matrix[2][0]),
            pixman_fixed_to_double(pict->transform->matrix[2][1]),
            pixman_fixed_to_double(pict->transform->matrix[2][2]));

    return buf;
}

static __maybe_unused char const * pict_alphamap(PicturePtr pict)
{
    if (!pict->pDrawable || !pict->alphaMap)
        return "No";

    return "Yes";
}

static __maybe_unused char const * pict_componentalpha(PicturePtr pict)
{
    if (!pict->componentAlpha)
        return "No";

    return "Yes";
}

static __maybe_unused char const * pict_filter(PicturePtr pict)
{
    if (!pict->pDrawable)
        return "None";

    if (pict->filter == PictFilterNearest)
        return "PictFilterNearest";

    if (pict->filter == PictFilterBilinear)
        return "PictFilterBilinear";

    if (pict->filter == PictFilterFast)
        return "PictFilterFast";

    if (pict->filter == PictFilterGood)
        return "PictFilterGood";

    if (pict->filter == PictFilterBest)
        return "PictFilterBest";

    if (pict->filter == PictFilterConvolution)
        return "PictFilterConvolution";

    return "???";
}

static void dump_pict(const char *prefix, PicturePtr pict, Bool accel)
{
    if (!pict)
        return;

    if (accel)
        AccelMsg("%s: %p type %s %dx%d format %s repeat %s transform %s alphamap %s componentalpha %s filter %s\n",
                 prefix,
                 pict,
                 pict_type(pict),
                 pict_width(pict),
                 pict_height(pict),
                 pict_format(pict->format),
                 pict_repeat(pict->repeatType),
                 pict_transform(pict),
                 pict_alphamap(pict),
                 pict_componentalpha(pict),
                 pict_filter(pict));
    else
        FallbackMsg("%s: %p type %s %dx%d format %s repeat %s transform %s alphamap %s componentalpha %s filter %s\n",
                    prefix,
                    pict,
                    pict_type(pict),
                    pict_width(pict),
                    pict_height(pict),
                    pict_format(pict->format),
                    pict_repeat(pict->repeatType),
                    pict_transform(pict),
                    pict_alphamap(pict),
                    pict_componentalpha(pict),
                    pict_filter(pict));
}

static Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                                   PicturePtr pMaskPicture,
                                   PicturePtr pDstPicture)
{
    PROFILE_START

    if (TegraEXACheckComposite2D(op, pSrcPicture, pMaskPicture, pDstPicture))
        return TRUE;

    if (TegraEXACheckComposite3D(op, pSrcPicture, pMaskPicture, pDstPicture))
        return TRUE;

    FallbackMsg("op: %s\n", op_name(op));
    dump_pict("src", pSrcPicture, FALSE);
    dump_pict("mask", pMaskPicture, FALSE);
    dump_pict("dst", pDstPicture, FALSE);

    return FALSE;
}

static Bool TegraEXAPrepareComposite(int op, PicturePtr pSrcPicture,
                                     PicturePtr pMaskPicture,
                                     PicturePtr pDstPicture,
                                     PixmapPtr pSrc,
                                     PixmapPtr pMask,
                                     PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    TegraPixmapPtr priv;

    PROFILE_STOP
    PROFILE_START

    AccelMsg("\n");

    /* Use GR2D for simple solid fills as usually it is more optimal. */
    if (TegraEXAPrepareComposite2D(op, pSrcPicture, pMaskPicture,
                                   pDstPicture, pSrc, pMask, pDst)) {
        AccelMsg("GR2D: op: %s\n", op_name(op));
        dump_pict("GR2D: src", pSrcPicture, TRUE);
        dump_pict("GR2D: dst", pDstPicture, TRUE);

        PROFILE_STOP
        PROFILE_START

        return TRUE;
    }

    if (!tegra->exa_compositing)
        goto fallback;

    if (TegraEXAPrepareComposite3D(op, pSrcPicture, pMaskPicture,
                                   pDstPicture, pSrc, pMask, pDst)) {
        AccelMsg("GR3D: op: %s\n", op_name(op));
        dump_pict("GR3D: src", pSrcPicture, TRUE);
        dump_pict("GR3D: mask", pMaskPicture, TRUE);
        dump_pict("GR3D: dst", pDstPicture, TRUE);

        PROFILE_STOP
        PROFILE_START

        return TRUE;
    }

    if (pSrcPicture && pSrcPicture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(pSrc);
        priv->picture_format = pSrcPicture->format;
    }

    if (pMaskPicture && pMaskPicture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(pMask);
        priv->picture_format = pMaskPicture->format;
    }

    if (pDstPicture && pDstPicture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(pDst);
        priv->picture_format = pDstPicture->format;
    }

fallback:
    FallbackMsg("op: %s\n", op_name(op));
    dump_pict("src", pSrcPicture, FALSE);
    dump_pict("mask", pMaskPicture, FALSE);
    dump_pict("dst", pDstPicture, FALSE);

    return FALSE;
}

static void TegraEXAComposite(PixmapPtr pDst,
                              int srcX, int srcY,
                              int maskX, int maskY,
                              int dstX, int dstY,
                              int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    AccelMsg("src %dx%d mask %dx%d dst %dx%d w:h %d:%d\n",
             srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        return TegraEXASolid(pDst, dstX, dstY, dstX + width, dstY + height);

    if (tegra->scratch.op2d == TEGRA2D_COPY)
        return TegraEXACopyExt(pDst, srcX, srcY, dstX, dstY, width, height);

    return TegraEXAComposite3D(pDst, srcX, srcY, maskX, maskY, dstX, dstY,
                               width, height);
}

static void TegraEXADoneComposite(PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    PROFILE_STOP
    PROFILE_START

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        TegraEXADoneSolid(pDst);
    else if (tegra->scratch.op2d == TEGRA2D_COPY)
        TegraEXADoneCopy(pDst);
    else
        TegraEXADoneComposite3D(pDst);

    PROFILE_STOP
}

/* vim: set et sts=4 sw=4 ts=4: */
