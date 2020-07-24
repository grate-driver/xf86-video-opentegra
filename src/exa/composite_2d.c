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
tegra_exa_transform_is_supported(int32_t dw, int32_t dh,
                                 int32_t sw, int32_t sh,
                                 unsigned bpp, PictTransformPtr t,
                                 enum tegra_2d_orientation *orientation)
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
        return false;

    /* Fast Rotate hardware limitation */
    if (dw > 4096 || dh > 4096 || sw > 4096 || sh > 4096) {
        FALLBACK_MSG("FR limitation\n");
        return false;
    }

    /* check whether matrix contains only integer values */
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 2; k++) {
            /* s16.16 format */
            if (t->matrix[i][k] & 0xffff) {
                FALLBACK_MSG("non-integer transform\n");
                return false;
            }
        }
    }

    e[0] = pixman_fixed_to_int(t->matrix[0][0]);
    e[1] = pixman_fixed_to_int(t->matrix[0][1]);
    e[2] = pixman_fixed_to_int(t->matrix[1][0]);
    e[3] = pixman_fixed_to_int(t->matrix[1][1]);

    if (e[0] == 0 && e[1] == -1 && e[2] == 1 && e[3] == 0) {
        *orientation = TEGRA2D_ROT_90;
        return true;
    }

    if (e[0] == -1 && e[1] == 0 && e[2] == 0 && e[3] == -1) {
        *orientation = TEGRA2D_ROT_180;
        return true;
    }

    if (e[0] == 0 && e[1] == 1 && e[2] == -1 && e[3] == 0) {
        *orientation = TEGRA2D_ROT_270;
        return true;
    }

    if (e[0] == -1 && e[1] == 0 && e[2] == 0 && e[3] == 1) {
        *orientation = TEGRA2D_FLIP_X;
        return true;
    }

    if (e[0] == 1 && e[1] == 0 && e[2] == 0 && e[3] == -1) {
        *orientation = TEGRA2D_FLIP_Y;
        return true;
    }

    /*
     * Transposing currently unimplemented (no real use-case),
     * More complex transformations (like scaling, skewing) can't be done
     * on GR2D.
     */
    FALLBACK_MSG("complex transform\n");
    return false;
}

static bool tegra_exa_check_composite_2d(int op,
                                         PicturePtr src_picture,
                                         PicturePtr mask_picture,
                                         PicturePtr dst_picture)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(dst_picture->pDrawable->pScreen);
    struct tegra_exa *tegra = TegraPTR(scrn)->exa;
    enum tegra_2d_orientation orientation;

    if (op != PictOpSrc && op != PictOpClear)
        return false;

    if (src_picture && src_picture->pDrawable) {
        if (op != PictOpSrc)
            return false;

        if (!src_picture->transform)
            return false;

        if (mask_picture)
            return false;

        if (src_picture->pDrawable->bitsPerPixel !=
            dst_picture->pDrawable->bitsPerPixel)
            return false;

        if (!tegra_exa_transform_is_supported(dst_picture->pDrawable->width,
                                              dst_picture->pDrawable->height,
                                              src_picture->pDrawable->width,
                                              src_picture->pDrawable->height,
                                              dst_picture->pDrawable->bitsPerPixel,
                                              src_picture->transform, &orientation))
            return false;

        tegra->scratch.orientation = orientation;
    } else {
        if (src_picture &&
            src_picture->pSourcePict->type != SourcePictTypeSolidFill)
            return false;

        if (src_picture && src_picture->transform)
            return false;

        if (mask_picture && mask_picture->pDrawable)
            return false;

        if (mask_picture &&
            mask_picture->pSourcePict->type != SourcePictTypeSolidFill)
            return false;

        if (mask_picture && mask_picture->transform)
            return false;
    }

    return true;
}

static bool tegra_exa_composite_format_has_alpha(PictFormatShort format)
{
    switch (format) {
    case PICT_a8:
    case PICT_a8r8g8b8:
        return true;

    default:
        break;
    }

    return false;
}

static bool tegra_exa_prepare_composite_solid_2d(int op,
                                                 PicturePtr src_picture,
                                                 PicturePtr mask_picture,
                                                 PicturePtr dst_picture,
                                                 PixmapPtr dst)
{
    Pixel solid;
    bool alpha;

    if (src_picture && src_picture->pDrawable)
        return false;

    if (mask_picture)
        return false;

    if (op == PictOpSrc) {
        if (src_picture)
            solid = src_picture->pSourcePict->solidFill.color;
        else
            solid = 0x00000000;

        if (src_picture && src_picture->format != dst_picture->format)
            return false;

        alpha = tegra_exa_composite_format_has_alpha(src_picture->format);
        if (!alpha)
            solid |= 0xff000000;

        alpha = tegra_exa_composite_format_has_alpha(dst_picture->format);
        if (!alpha)
            solid &= 0x00ffffff;

        if (!tegra_exa_prepare_solid_2d(dst, GXcopy, FB_ALLONES, solid))
            return false;

        return true;
    }

    if (op == PictOpClear) {
        if (!tegra_exa_prepare_solid_2d(dst, GXcopy, FB_ALLONES, 0x00000000))
            return false;

        return true;
    }

    return false;
}

static bool tegra_exa_prepare_composite_copy_2d_rotate(int op,
                                                       PicturePtr src_picture,
                                                       PicturePtr mask_picture,
                                                       PicturePtr dst_picture,
                                                       PixmapPtr src,
                                                       PixmapPtr dst)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(dst->drawable.pScreen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    struct tegra_exa *tegra = TegraPTR(scrn)->exa;
    struct tegra_pixmap *priv_src, *priv_dst;
    xf86CrtcPtr crtc;

    if (mask_picture)
        return false;

    if (op != PictOpSrc)
        return false;

    if (!src_picture || !src_picture->pDrawable || !src_picture->transform)
        return false;

    priv_src = exaGetPixmapDriverPrivate(src);
    priv_dst = exaGetPixmapDriverPrivate(dst);

    /*
     * Only predictable transformations are supported via GR2D due to its
     * memory addressing limitations.
     */
    if (!priv_src->scanout || !priv_dst->scanout_rotated)
        return false;

    /* destination is rotated in terms of Xorg display rotation */
    crtc = xf86_config->crtc[priv_dst->crtc];

    /* coordinates may become unaligned due to display's panning */
    if (!TEGRA_ALIGNED(crtc->x + dst->drawable.width, 4) ||
        !TEGRA_ALIGNED(crtc->y + dst->drawable.height, 4))
        return false;

    tegra->scratch.transform = *src_picture->transform;

    return tegra_exa_prepare_copy_2d_ext(src, dst, GXcopy, FB_ALLONES);
}

static bool tegra_exa_prepare_composite_2d(int op,
                                           PicturePtr src_picture,
                                           PicturePtr mask_picture,
                                           PicturePtr dst_picture,
                                           PixmapPtr src,
                                           PixmapPtr mask,
                                           PixmapPtr dst)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(dst->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(scrn)->exa;

    tegra->scratch.op2d = TEGRA2D_NONE;

    if (tegra_exa_prepare_composite_solid_2d(op, src_picture, mask_picture,
                                             dst_picture, dst)) {
        tegra->scratch.op2d = TEGRA2D_SOLID;
        return true;
    }

    if (tegra_exa_prepare_composite_copy_2d_rotate(op, src_picture, mask_picture,
                                                   dst_picture, src, dst)) {
        tegra->scratch.op2d = TEGRA2D_COPY;
        return true;
    }

    return false;
}

/* vim: set et sts=4 sw=4 ts=4: */
