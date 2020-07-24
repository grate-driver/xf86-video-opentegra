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

static PROFILE_DEF(composite);

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

static void
dump_pict(const char *prefix, PixmapPtr pixmap, PicturePtr pict, bool accel)
{
    __maybe_unused struct tegra_pixmap *priv;

    if (pixmap)
        priv = exaGetPixmapDriverPrivate(pixmap);
    else
        priv = NULL;

    if (!pict)
        return;

    if (accel)
        ACCEL_MSG("%s: pixmap %p type %s %dx%d format %s repeat %s transform %s alphamap %s componentalpha %s filter %s scanout %d\n",
                  prefix,
                  pixmap,
                  pict_type(pict),
                  pict_width(pict),
                  pict_height(pict),
                  pict_format(pict->format),
                  pict_repeat(pict->repeatType),
                  pict_transform(pict),
                  pict_alphamap(pict),
                  pict_componentalpha(pict),
                  pict_filter(pict),
                  priv ? priv->scanout : 0);
    else
        FALLBACK_MSG("%s: %p type %s %dx%d format %s repeat %s transform %s alphamap %s componentalpha %s filter %s scanout %d\n",
                     prefix,
                     pixmap,
                     pict_type(pict),
                     pict_width(pict),
                     pict_height(pict),
                     pict_format(pict->format),
                     pict_repeat(pict->repeatType),
                     pict_transform(pict),
                     pict_alphamap(pict),
                     pict_componentalpha(pict),
                     pict_filter(pict),
                     priv ? priv->scanout : 0);
}

static bool tegra_exa_check_composite(int op,
                                      PicturePtr src_picture,
                                      PicturePtr mask_picture,
                                      PicturePtr dst_picture)
{
    PROFILE_START(composite)

    if (tegra_exa_check_composite_2d(op, src_picture, mask_picture, dst_picture))
        return true;

    if (tegra_exa_check_composite_3d(op, src_picture, mask_picture, dst_picture))
        return true;

    FALLBACK_MSG("op: %s\n", op_name(op));
    dump_pict("src",  NULL, src_picture,  false);
    dump_pict("mask", NULL, mask_picture, false);
    dump_pict("dst",  NULL, dst_picture,  false);

    return false;
}

static bool tegra_exa_prepare_composite(int op,
                                        PicturePtr src_picture,
                                        PicturePtr mask_picture,
                                        PicturePtr dst_picture,
                                        PixmapPtr src,
                                        PixmapPtr mask,
                                        PixmapPtr dst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst->drawable.pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    struct tegra_pixmap * priv;

    PROFILE_STOP(composite)
    PROFILE_START(composite)

    ACCEL_MSG("\n");

    /* Use GR2D for simple solid fills as usually it is more optimal. */
    if (tegra_exa_prepare_composite_2d(op, src_picture, mask_picture, dst_picture,
                                       src, mask, dst)) {
        ACCEL_MSG("GR2D: op: %s\n", op_name(op));
        dump_pict("GR2D: src", src, src_picture, true);
        dump_pict("GR2D: dst", dst, dst_picture, true);

        PROFILE_STOP(composite)
        PROFILE_START(composite)

        return true;
    }

    if (!tegra->exa_compositing)
        goto fallback;

    if (tegra_exa_prepare_composite_3d(op, src_picture, mask_picture, dst_picture,
                                       src, mask, dst)) {
        ACCEL_MSG("GR3D: op: %s\n", op_name(op));
        dump_pict("GR3D: src",  src,  src_picture,  true);
        dump_pict("GR3D: mask", mask, mask_picture, true);
        dump_pict("GR3D: dst",  dst,  dst_picture,  true);

        PROFILE_STOP(composite)
        PROFILE_START(composite)

        return true;
    }

    if (src_picture && src_picture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(src);
        priv->picture_format = src_picture->format;
    }

    if (mask_picture && mask_picture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(mask);
        priv->picture_format = mask_picture->format;
    }

    if (dst_picture && dst_picture->pDrawable) {
        priv = exaGetPixmapDriverPrivate(dst);
        priv->picture_format = dst_picture->format;
    }

fallback:
    FALLBACK_MSG("op: %s\n", op_name(op));
    dump_pict("src",  src,  src_picture,  false);
    dump_pict("mask", mask, mask_picture, false);
    dump_pict("dst",  dst,  dst_picture,  false);

    return false;
}

static void tegra_exa_composite(PixmapPtr dst,
                                int src_x, int src_y,
                                int mask_x, int mask_y,
                                int dst_x, int dst_y,
                                int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;

    ACCEL_MSG("src %dx%d mask %dx%d dst %dx%d w:h %d:%d\n",
              src_x, src_y, mask_x, mask_y, dst_x, dst_y, width, height);

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        return tegra_exa_solid_2d(dst, dst_x, dst_y,
                                  dst_x + width, dst_y + height);

    if (tegra->scratch.op2d == TEGRA2D_COPY)
        return tegra_exa_copy_2d_ext(dst, src_x, src_y,
                                     dst_x, dst_y, width, height);

    return tegra_exa_composite_3d(dst, src_x, src_y, mask_x, mask_y,
                                  dst_x, dst_y, width, height);
}

static void tegra_exa_done_composite(PixmapPtr dst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;

    if (tegra->scratch.op2d == TEGRA2D_SOLID)
        tegra_exa_done_solid_2d(dst);
    else if (tegra->scratch.op2d == TEGRA2D_COPY)
        tegra_exa_done_copy_2d(dst);
    else
        tegra_exa_done_composite_3d(dst);

    PROFILE_STOP(composite)
}

/* vim: set et sts=4 sw=4 ts=4: */
