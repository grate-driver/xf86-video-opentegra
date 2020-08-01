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

#include "shaders.h"

#define BLUE(c)     (((c) & 0xff)         / 255.0f)
#define GREEN(c)    ((((c) >> 8)  & 0xff) / 255.0f)
#define RED(c)      ((((c) >> 16) & 0xff) / 255.0f)
#define ALPHA(c)    ((((c) >> 24) & 0xff) / 255.0f)

#define TEGRA_PUSH_VTX_ATTR(x, y, push)                                 \
    if (push) {                                                         \
        tegra->scratch.attribs.map[tegra->scratch.attrib_itr++] = x;    \
        tegra->scratch.attribs.map[tegra->scratch.attrib_itr++] = y;    \
    }

#define TEX_EMPTY           0
#define TEX_SOLID           1
#define TEX_CLIPPED         2
#define TEX_PAD             3
#define TEX_NORMAL          4
#define TEX_MIRROR          5

#define PROG_SEL(SRC_SEL, MASK_SEL) ((SRC_SEL) | ((MASK_SEL) << 3))

#define PROG_DEF(OP_NAME) \
.prog[PROG_SEL(TEX_SOLID,   TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask_src, \
.prog[PROG_SEL(TEX_SOLID,   TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask_src, \
.prog[PROG_SEL(TEX_SOLID,   TEX_CLIPPED)]   = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_SOLID,   TEX_PAD)]       = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_SOLID,   TEX_NORMAL)]    = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_SOLID,   TEX_MIRROR)]    = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_CLIPPED)]   = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_PAD)]       = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_NORMAL)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_CLIPPED, TEX_MIRROR)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_NORMAL,  TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_NORMAL,  TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_NORMAL,  TEX_PAD)]       = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_NORMAL,  TEX_NORMAL)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_NORMAL,  TEX_MIRROR)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_PAD,     TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_PAD,     TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_PAD,     TEX_CLIPPED)]   = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_PAD,     TEX_PAD)]       = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_PAD,     TEX_NORMAL)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_PAD,     TEX_MIRROR)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_CLIPPED)]   = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_PAD)]       = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_NORMAL)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_MIRROR,  TEX_MIRROR)]    = &prog_blend_ ## OP_NAME, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_SOLID)]     = &prog_blend_ ## OP_NAME ## _solid_mask_src, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_EMPTY)]     = &prog_blend_ ## OP_NAME ## _solid_mask_src, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_CLIPPED)]   = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_PAD)]       = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_NORMAL)]    = &prog_blend_ ## OP_NAME ## _solid_src, \
.prog[PROG_SEL(TEX_EMPTY,   TEX_MIRROR)]    = &prog_blend_ ## OP_NAME ## _solid_src

struct tegra_composite_config {
    const struct shader_program *prog[64];
    bool discards_clipped_area : 1; /* clipped area is either discarded or
                                       filled with black solid */
};

static const struct tegra_composite_config composite_cfgs[] = {
    [PictOpOver] = {
        PROG_DEF(over),

        .discards_clipped_area = true,
    },

    [PictOpOverReverse] = {
        PROG_DEF(over_reverse),

        .discards_clipped_area = true,
    },

    [PictOpAdd] = {
        PROG_DEF(add),

        .discards_clipped_area = true,
    },

    [PictOpSrc] = {
        PROG_DEF(src),

        .discards_clipped_area = false,
    },

    [PictOpIn] = {
        PROG_DEF(in),

        .discards_clipped_area = false,
    },

    [PictOpInReverse] = {
        PROG_DEF(in_reverse),

        .discards_clipped_area = false,
    },

    [PictOpOut] = {
        PROG_DEF(out),

        .discards_clipped_area = false,
    },

    [PictOpOutReverse] = {
        PROG_DEF(out_reverse),

        .discards_clipped_area = true,
    },

    [PictOpDst] = {
        .prog[PROG_SEL(TEX_PAD,     TEX_SOLID)]     = &prog_blend_dst,
        .prog[PROG_SEL(TEX_CLIPPED, TEX_SOLID)]     = &prog_blend_dst,

        .prog[PROG_SEL(TEX_SOLID,   TEX_SOLID)]     = &prog_blend_dst_solid_mask,
        .prog[PROG_SEL(TEX_SOLID,   TEX_SOLID)]     = &prog_blend_dst_solid_mask,

        .prog[PROG_SEL(TEX_PAD,     TEX_EMPTY)]     = &prog_blend_dst,
        .prog[PROG_SEL(TEX_CLIPPED, TEX_EMPTY)]     = &prog_blend_dst,

        .prog[PROG_SEL(TEX_EMPTY,   TEX_EMPTY)]     = &prog_blend_dst_solid_mask,
        .prog[PROG_SEL(TEX_EMPTY,   TEX_EMPTY)]     = &prog_blend_dst_solid_mask,

        .prog[PROG_SEL(TEX_SOLID,   TEX_EMPTY)]     = &prog_blend_dst_solid_mask,
        .prog[PROG_SEL(TEX_EMPTY,   TEX_SOLID)]     = &prog_blend_dst_solid_mask,

        .discards_clipped_area = false,
    },

    [PictOpAtop] = {
        PROG_DEF(atop),

        .discards_clipped_area = true,
    },

    [PictOpAtopReverse] = {
        PROG_DEF(atop_reverse),

        .discards_clipped_area = false,
    },

    [PictOpXor] = {
        PROG_DEF(xor),

        .discards_clipped_area = true,
    },

    [PictOpSaturate] = {
        .prog[PROG_SEL(TEX_PAD,     TEX_CLIPPED)]   = &prog_blend_saturate,
        .prog[PROG_SEL(TEX_PAD,     TEX_PAD)]       = &prog_blend_saturate,

        .prog[PROG_SEL(TEX_CLIPPED, TEX_CLIPPED)]   = &prog_blend_saturate,
        .prog[PROG_SEL(TEX_CLIPPED, TEX_PAD)]       = &prog_blend_saturate,

        .prog[PROG_SEL(TEX_CLIPPED, TEX_SOLID)]     = &prog_blend_saturate_solid_mask,
        .prog[PROG_SEL(TEX_PAD,     TEX_SOLID)]     = &prog_blend_saturate_solid_mask,

        .prog[PROG_SEL(TEX_SOLID,   TEX_CLIPPED)]   = &prog_blend_saturate_solid_src,
        .prog[PROG_SEL(TEX_SOLID,   TEX_PAD)]       = &prog_blend_saturate_solid_src,

        .prog[PROG_SEL(TEX_CLIPPED, TEX_EMPTY)]     = &prog_blend_saturate_solid_mask,
        .prog[PROG_SEL(TEX_PAD,     TEX_EMPTY)]     = &prog_blend_saturate_solid_mask,

        .prog[PROG_SEL(TEX_EMPTY,   TEX_CLIPPED)]   = &prog_blend_saturate_solid_src,
        .prog[PROG_SEL(TEX_EMPTY,   TEX_PAD)]       = &prog_blend_saturate_solid_src,

        .discards_clipped_area = true,
    },
};

#include "composite_3d_state_tracker.c"

static bool
tegra_exa_texture_optimized_out(PicturePtr picture, PixmapPtr pixmap,
                                const struct tegra_composite_config *cfg)
{
    struct tegra_pixmap *priv;

    /*
     * GR3D performance is quite slow for a non-trivial shaders,
     * hence we want to reduce the number of 3D instructions as
     * much as possible.
     */
    if (picture && picture->pDrawable && picture->repeat) {
        if (picture->pDrawable->width == 1 &&
            picture->pDrawable->height == 1)
                return true;
    }

    if (pixmap) {
        priv = exaGetPixmapDriverPrivate(pixmap);

        if (priv->state.solid_fill) {
            if (picture->repeat)
                return true;

            if (priv->state.solid_color == 0x0)
                return true;

            if (!cfg || cfg->discards_clipped_area)
                return true;
        }
    }

    return false;
}

static bool tegra_exa_is_pow2_texture(PixmapPtr pix)
{
    if (pix) {
        if (IS_POW2(pix->drawable.width) &&
            IS_POW2(pix->drawable.height))
            return true;
    }

    return false;
}

static bool tegra_exa_picture_format_has_alpha(unsigned format)
{
    switch (format) {
    case PICT_a8:
    case PICT_a8r8g8b8:
        return true;

    default:
        return false;
    }
}

static bool tegra_exa_texture_has_per_component_alpha(PicturePtr pic)
{
    return pic->componentAlpha;
}

static bool
tegra_exa_pre_check_3d_program(struct tegra_3d_draw_state *draw_state)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[draw_state->op];
    const struct shader_program *prog;

    if (draw_state->op >= TEGRA_ARRAY_SIZE(composite_cfgs))
        return false;

    prog = cfg->prog[PROG_SEL(draw_state->src.tex_sel,
                              draw_state->mask.tex_sel)];
    if (!prog) {
        FALLBACK_MSG("no shader for operation %d src_sel %u mask_sel %u\n",
                     draw_state->op,
                     draw_state->src.tex_sel,
                     draw_state->mask.tex_sel);
        return false;
    }

    /*
     * About half of shaders discard pixels outside of clipping area,
     * doing the clipping within shader is sub-optimal because every
     * ALU quadruple and pixel's re-circulation cycle kills more performance.
     * Hence we will simply clip in software when possible
     */
    if (cfg->discards_clipped_area)
        draw_state->discards_clip = cfg->discards_clipped_area;

    ACCEL_MSG("got shader for operation %d src_sel %u mask_sel %u discards_clip %u %s\n",
              draw_state->op, draw_state->src.tex_sel, draw_state->mask.tex_sel,
              draw_state->discards_clip, prog->name);

    /* we have special shaders for this case */
    if (draw_state->op == PictOpOver &&
        draw_state->src.tex_sel == TEX_NORMAL && draw_state->src.alpha &&
        (draw_state->mask.tex_sel == TEX_SOLID ||
         draw_state->mask.tex_sel == TEX_EMPTY))
        return true;

    /*
     * Only padding and clipping are currently supported by the
     * "generic" shaders for non-pow2 textures.
     */
    if (draw_state->mask.pix &&
        draw_state->mask.tex_sel != TEX_PAD &&
        draw_state->mask.tex_sel != TEX_CLIPPED &&
        !tegra_exa_is_pow2_texture(draw_state->mask.pix)) {
        FALLBACK_MSG("unsupported repeat type mask_sel %u\n",
                     draw_state->mask.tex_sel);
        return false;
    }

    if (draw_state->src.pix &&
        draw_state->src.tex_sel != TEX_PAD &&
        draw_state->src.tex_sel != TEX_CLIPPED &&
        !tegra_exa_is_pow2_texture(draw_state->src.pix)) {
        FALLBACK_MSG("unsupported repeat type src_sel %u\n",
                     draw_state->src.tex_sel);
        return false;
    }

    return true;
}

static unsigned tegra_exa_picture_format_to_tgr3d(PictFormatShort format)
{
    switch (format) {
    case PICT_a8:
        return TGR3D_PIXEL_FORMAT_A8;

    case PICT_x8r8g8b8:
    case PICT_a8r8g8b8:
        return TGR3D_PIXEL_FORMAT_RGBA8888;

    default:
        return 0;
    }
}

static bool tegra_exa_check_texture(int op, PicturePtr pic, PixmapPtr pixmap)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[op];
    unsigned width, height;

    if (pic && pic->pDrawable && !tegra_exa_texture_optimized_out(pic, pixmap, cfg)) {
        width = pic->pDrawable->width;
        height = pic->pDrawable->height;

        if (width > 2048 || height > 2048) {
            FALLBACK_MSG("too large texture %ux%u\n", width, height);
            return false;
        }

        if ((cfg->discards_clipped_area &&
                                !tegra_exa_simple_transform(pic->transform)) ||
            !tegra_exa_simple_transform_scale(pic->transform)) {
                FALLBACK_MSG("unsupported transform\n");
                return false;
        }

        if (pic->filter >= PictFilterConvolution) {
            FALLBACK_MSG("unsupported filtering %u\n", pic->filter);
            return false;
        }

        if (!IS_POW2(width) || !IS_POW2(height)) {
            if (pic->filter == PictFilterBilinear) {
                FALLBACK_MSG("bilinear filtering for non-pow2 texture\n");
                return false;
            }
        }
    }

    return true;
}

static bool
tegra_exa_attributes_buffer_is_full(struct tegra_exa_scratch * scratch)
{
    unsigned attrs_num = 1 + !!scratch->src + !!scratch->mask;

    if (scratch->attrib_itr * 2 + attrs_num * 24 > TEGRA_ATTRIB_BUFFER_SIZE)
        return true;

    return false;
}

static bool tegra_exa_check_composite_3d(int op,
                                         PicturePtr src_picture,
                                         PicturePtr mask_picture,
                                         PicturePtr dst_picture)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_picture->pDrawable->pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    if (!tegra->exa_compositing)
        return false;

    if (op > TEGRA_ARRAY_SIZE(composite_cfgs)) {
        FALLBACK_MSG("unsupported operation %d\n", op);
        return false;
    }

    if (dst_picture->format != PICT_x8r8g8b8 &&
        dst_picture->format != PICT_a8r8g8b8 &&
        dst_picture->format != PICT_a8) {
        FALLBACK_MSG("unsupported format %u\n", dst_picture->format);
        return false;
    }

    if (src_picture) {
        if (src_picture->format != PICT_x8r8g8b8 &&
            src_picture->format != PICT_a8r8g8b8 &&
            src_picture->format != PICT_a8) {
            FALLBACK_MSG("unsupported format %u\n", src_picture->format);
            return false;
        }

        if (src_picture->pDrawable) {
            if (!tegra_exa_check_texture(op, src_picture, NULL))
                return false;
        } else {
            if (src_picture->pSourcePict->type != SourcePictTypeSolidFill) {
                FALLBACK_MSG("unsupported fill type %u\n",
                             src_picture->pSourcePict->type);
                return false;
            }
        }
    }

    if (mask_picture) {
        if (mask_picture->format != PICT_x8r8g8b8 &&
            mask_picture->format != PICT_a8r8g8b8 &&
            mask_picture->format != PICT_a8) {
            FALLBACK_MSG("unsupported format %u\n",
                     mask_picture->format);
            return false;
        }

        if (mask_picture->pDrawable) {
            if (!tegra_exa_check_texture(op, mask_picture, NULL))
                return false;
        } else {
            if (mask_picture->pSourcePict->type != SourcePictTypeSolidFill) {
                FALLBACK_MSG("unsupported fill type %u\n",
                             mask_picture->pSourcePict->type);
                return false;
            }
        }
    }

    return true;
}

static Pixel tegra_exa_optimized_texture_color(PixmapPtr pix)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pix);
    Pixel color = 0x00000000;
    void *ptr;

    if (priv->state.solid_fill) {
        switch (pix->drawable.bitsPerPixel) {
        case 8:
            color = priv->state.solid_color << 24;
            break;
        case 16:
            color = priv->state.solid_color;
            break;
        case 32:
            color = priv->state.solid_color;
            break;
        }
        goto done;
    }

    if (tegra_exa_prepare_cpu_access(pix, EXA_PREPARE_SRC, &ptr, false)) {
        switch (pix->drawable.bitsPerPixel) {
        case 8:
            color = *((CARD8*) ptr) << 24;
            break;
        case 16:
            color = *((CARD16*) ptr);
            break;
        case 32:
            color = *((CARD32*) ptr);
            break;
        }

        tegra_exa_finish_cpu_access(pix, EXA_PREPARE_SRC);
    }

done:
    ACCEL_MSG("color 0x%08lx\n", color);

    return color;
}

static bool tegra_exa_prepare_composite_3d(int op,
                                           PicturePtr src_picture,
                                           PicturePtr mask_picture,
                                           PicturePtr dst_picture,
                                           PixmapPtr psrc,
                                           PixmapPtr pmask,
                                           PixmapPtr pdst)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[op];
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pdst->drawable.pScreen);
    bool mask_tex = (mask_picture && mask_picture->pDrawable);
    bool src_tex = (src_picture && src_picture->pDrawable);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    struct tegra_3d_draw_state draw_state;
    bool mask_tex_reduced = true;
    bool src_tex_reduced = true;
    struct tegra_pixmap *priv;
    unsigned mask_sel;
    unsigned src_sel;
    Pixel solid;
    bool alpha;

    if (!tegra_exa_check_texture(op, mask_picture, pmask))
        return false;

    if (!tegra_exa_check_texture(op, src_picture, psrc))
        return false;

    memset(&draw_state, 0, sizeof(draw_state));

    tegra_exa_enter_optimization_3d_state(tegra);

    if (src_tex && tegra_exa_texture_optimized_out(src_picture, psrc, cfg))
        src_tex = false;
    else
        src_tex_reduced = false;

    if (src_tex_reduced)
        ACCEL_MSG("src texture reduced\n");

    if (mask_tex && tegra_exa_texture_optimized_out(mask_picture, pmask, cfg))
        mask_tex = false;
    else
        mask_tex_reduced = false;

    if (mask_tex_reduced)
        ACCEL_MSG("mask texture reduced\n");

    tegra->scratch.mask = (op != PictOpClear && mask_tex) ? pmask : NULL;
    tegra->scratch.src = (op != PictOpClear && src_tex) ? psrc : NULL;
    tegra->scratch.ops = 0;

    if (src_picture) {
        if (tegra->scratch.src) {
            switch (src_picture->repeatType) {
            case RepeatNone:
                src_sel = TEX_CLIPPED;
                break;
            case RepeatPad:
                src_sel = TEX_PAD;
                break;
            case RepeatNormal:
                src_sel = TEX_NORMAL;
                break;
            case RepeatReflect:
                src_sel = TEX_MIRROR;
                break;
            default:
                FALLBACK_MSG("unsupported repeat type %u\n",
                         src_picture->repeatType);
                goto fail;
            }

            draw_state.src.pow2     = tegra_exa_is_pow2_texture(psrc);
            draw_state.src.format   = tegra_exa_picture_format_to_tgr3d(src_picture->format);
            draw_state.src.alpha    = tegra_exa_picture_format_has_alpha(src_picture->format);
            draw_state.src.bilinear = (src_picture->filter == PictFilterBilinear);
            draw_state.src.tex_sel  = src_sel;
            draw_state.src.pix      = psrc;

            if (src_picture->transform) {
                tegra->scratch.transform_src = *src_picture->transform;

                if (src_sel == TEX_CLIPPED)
                    pixman_transform_invert(&tegra->scratch.transform_src_inv,
                                            &tegra->scratch.transform_src);

                draw_state.src.transform_coords = true;
            }

            if (draw_state.src.alpha &&
                draw_state.src.format == TGR3D_PIXEL_FORMAT_RGBA8888)
            {
                priv = exaGetPixmapDriverPrivate(psrc);
                draw_state.src.alpha = !priv->state.alpha_0;
            }
        } else {
            if (op != PictOpClear && src_picture) {
                if (src_tex_reduced)
                    solid = tegra_exa_optimized_texture_color(psrc);
                else
                    solid = src_picture->pSourcePict->solidFill.color;

                alpha = tegra_exa_picture_format_has_alpha(src_picture->format);
                if (!alpha)
                    solid |= 0xff000000;

                if (op == PictOpAdd  &&
                    !tegra_exa_picture_format_has_alpha(dst_picture->format))
                    solid &= 0x00ffffff;

                src_sel = TEX_SOLID;
            } else {
                solid = 0x00000000;
            }

            if (solid == 0x00000000)
                src_sel = TEX_EMPTY;

            ACCEL_MSG("src solid 0x%08lx\n", solid);

            draw_state.src.tex_sel = src_sel;
            draw_state.src.solid = solid;
        }
    } else {
        draw_state.src.tex_sel = TEX_EMPTY;
        draw_state.src.solid = 0x00000000;
    }

    if (mask_picture) {
        if (tegra->scratch.mask) {
            switch (mask_picture->repeatType) {
            case RepeatNone:
                mask_sel = TEX_CLIPPED;
                break;
            case RepeatPad:
                mask_sel = TEX_PAD;
                break;
            case RepeatNormal:
                mask_sel = TEX_NORMAL;
                break;
            case RepeatReflect:
                mask_sel = TEX_MIRROR;
                break;
            default:
                FALLBACK_MSG("unsupported repeat type %u\n",
                         mask_picture->repeatType);
                goto fail;
            }

            draw_state.mask.pow2            = tegra_exa_is_pow2_texture(pmask);
            draw_state.mask.format          = tegra_exa_picture_format_to_tgr3d(mask_picture->format);
            draw_state.mask.alpha           = tegra_exa_picture_format_has_alpha(mask_picture->format);
            draw_state.mask.component_alpha = tegra_exa_texture_has_per_component_alpha(mask_picture);
            draw_state.mask.bilinear        = (mask_picture->filter == PictFilterBilinear);
            draw_state.mask.tex_sel         = mask_sel;
            draw_state.mask.pix             = pmask;

            if (mask_picture->transform) {
                tegra->scratch.transform_mask = *mask_picture->transform;

                if (mask_sel == TEX_CLIPPED)
                    pixman_transform_invert(&tegra->scratch.transform_mask_inv,
                                            &tegra->scratch.transform_mask);

                draw_state.mask.transform_coords = true;
            }
        } else {
            if (op != PictOpClear && mask_picture) {
                if (mask_tex_reduced)
                    solid = tegra_exa_optimized_texture_color(pmask);
                else
                    solid = mask_picture->pSourcePict->solidFill.color;

                if (!mask_picture->componentAlpha)
                    solid |= solid >> 24 | solid >> 16 | solid >> 8;

                alpha = tegra_exa_picture_format_has_alpha(mask_picture->format);
                if (!alpha)
                    solid |= 0xff000000;

                if (op == PictOpAdd &&
                    !tegra_exa_picture_format_has_alpha(dst_picture->format))
                    solid &= 0x00ffffff;

                mask_sel = TEX_SOLID;
            } else {
                solid = 0xffffffff;
            }

            if (solid == 0xffffffff)
                mask_sel = TEX_EMPTY;

            ACCEL_MSG("mask solid 0x%08lx\n", solid);

            draw_state.mask.tex_sel = mask_sel;
            draw_state.mask.solid = solid;
        }
    } else {
        draw_state.mask.tex_sel = TEX_EMPTY;
        draw_state.mask.solid = 0xffffffff;
    }

    draw_state.dst.format = tegra_exa_picture_format_to_tgr3d(dst_picture->format);
    draw_state.dst.alpha  = tegra_exa_picture_format_has_alpha(dst_picture->format);
    draw_state.dst.pix    = pdst;

    draw_state.op = op;

    if (!tegra_exa_pre_check_3d_program(&draw_state))
        goto fail;

    if (!tegra_exa_3d_state_append(&tegra->gr3d_state, tegra, &draw_state))
        goto fail;

    return true;

fail:
    tegra_exa_exit_optimization_3d_state(tegra);
    return false;
}

static void tegra_exa_composite_3d(PixmapPtr pdst,
                                   int src_x, int src_y,
                                   int mask_x, int mask_y,
                                   int dst_x, int dst_y,
                                   int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pdst->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    struct tegra_3d_state *state = &tegra->gr3d_state;
    PictTransformPtr mask_t = NULL, mask_t_inv = NULL;
    PictTransformPtr src_t = NULL, src_t_inv = NULL;
    struct tegra_3d_draw_state *draw_state = &state->new;
    float dst_left, dst_right, dst_top, dst_bottom;
    float src_left  = 0, src_right  = 0, src_top  = 0, src_bottom = 0;
    float mask_left = 0, mask_right = 0, mask_top = 0, mask_bottom = 0;
    bool push_mask = !!tegra->scratch.mask;
    bool push_src = !!tegra->scratch.src;
    int swidth = 0, sheight = 0;
    int mwidth = 0, mheight = 0;
    struct tegra_box src_untransformed = {0}, mask_untransformed = {0};
    struct tegra_box src_transformed = {0}, mask_transformed = {0};
    struct tegra_box src = {0}, mask = {0};
    struct tegra_box dst;
    bool clip_mask;
    bool clip_src;

    if (draw_state->optimized_out)
        goto degenerate;

    /* if attributes buffer is full, do nothing for now (TODO better job) */
    if (tegra_exa_attributes_buffer_is_full(&tegra->scratch)) {
        ERROR_MSG("FIXME: attributes buffer is full\n");
        return;
    }

    if (dst_x == 0 && dst_y == 0 &&
        pdst->drawable.width == width &&
        pdst->drawable.height == height)
        draw_state->dst_full_cover = 1;

    dst.x0 = dst_x;
    dst.y0 = dst_y;
    dst.x1 = dst_x + width;
    dst.y1 = dst_y + height;

    clip_src = (draw_state->src.tex_sel == TEX_CLIPPED);
    clip_mask = (draw_state->mask.tex_sel == TEX_CLIPPED);

    if (draw_state->src.transform_coords) {
        src_t = &tegra->scratch.transform_src;
        src_t_inv = &tegra->scratch.transform_src_inv;
    }

    if (draw_state->mask.transform_coords) {
        mask_t = &tegra->scratch.transform_mask;
        mask_t_inv = &tegra->scratch.transform_mask_inv;
    }

    if (push_src) {
        swidth = tegra->scratch.src->drawable.width;
        sheight = tegra->scratch.src->drawable.height;

        src.x0 = src_x;
        src.y0 = src_y;
        src.x1 = src_x + width;
        src.y1 = src_y + height;
    }

    if (push_mask) {
        mwidth = tegra->scratch.mask->drawable.width;
        mheight = tegra->scratch.mask->drawable.height;

        mask.x0 = mask_x;
        mask.y0 = mask_y;
        mask.x1 = mask_x + width;
        mask.y1 = mask_y + height;
    }

    if (push_src) {
        tegra_exa_apply_transform(src_t, &src, &src_transformed);

        /*
         * EXA doesn't clip transparent areas for us, hence we're doing
         * it by ourselves here because it is important to be able to use
         * optimized shaders and to reduce memory bandwidth usage.
         */
        if (draw_state->discards_clip && clip_src) {
            tegra_exa_clip_to_pixmap_area(tegra->scratch.src,
                                          &src_transformed, &src_transformed);

            if (tegra_exa_is_degenerate(&src_transformed))
                goto degenerate;

            tegra_exa_get_untransformed(src_t_inv, &src_transformed, &src_untransformed);
            tegra_exa_apply_clip(&dst, &src_untransformed, dst_x - src_x, dst_y - src_y);
            tegra_exa_apply_clip(&mask, &dst, mask_x - dst_x, mask_y - dst_y);
            tegra_exa_apply_clip(&src, &dst, src_x - dst_x, src_y - dst_y);

            dst_x = dst.x0;
            dst_y = dst.y0;

            src_x = src.x0;
            src_y = src.y0;

            mask_x = mask.x0;
            mask_y = mask.y0;
        }
    }

    if (push_mask) {
        tegra_exa_apply_transform(mask_t, &mask, &mask_transformed);

        if (draw_state->discards_clip && clip_mask) {
            tegra_exa_clip_to_pixmap_area(tegra->scratch.mask,
                                          &mask_transformed, &mask_transformed);

            if (tegra_exa_is_degenerate(&mask_transformed))
                goto degenerate;

            tegra_exa_get_untransformed(mask_t_inv, &mask_transformed, &mask_untransformed);
            tegra_exa_apply_clip(&dst, &mask_untransformed, dst_x - mask_x, dst_y - mask_y);
            tegra_exa_apply_clip(&src, &dst, src_x - dst_x, src_y - dst_y);
            tegra_exa_apply_transform(src_t, &src, &src_transformed);
        }
    }

    if (push_src) {
        if (swidth > 1 && (src_transformed.x0 < 0 || src_transformed.x0 > swidth))
            draw_state->src.coords_wrap = true;

        if (swidth > 1 && (src_transformed.x1 < 0 || src_transformed.x1 > swidth))
            draw_state->src.coords_wrap = true;

        if (sheight > 1 && (src_transformed.y0 < 0 || src_transformed.y0 > sheight))
            draw_state->src.coords_wrap = true;

        if (sheight > 1 && (src_transformed.y1 < 0 || src_transformed.y1 > sheight))
            draw_state->src.coords_wrap = true;

        src_left   = src.x0;
        src_right  = src.x1;
        src_bottom = src.y0;
        src_top    = src.y1;
    }

    if (push_mask) {
        if (mwidth > 1 && (mask_transformed.x0 < 0 || mask_transformed.x0 > mwidth))
            draw_state->mask.coords_wrap = true;

        if (mwidth > 1 && (mask_transformed.x1 < 0 || mask_transformed.x1 > mwidth))
            draw_state->mask.coords_wrap = true;

        if (mheight > 1 && (mask_transformed.y0 < 0 || mask_transformed.y0 > mheight))
            draw_state->mask.coords_wrap = true;

        if (mheight > 1 && (mask_transformed.y1 < 0 || mask_transformed.y1 > mheight))
            draw_state->mask.coords_wrap = true;

        mask_left   = mask.x0;
        mask_right  = mask.x1;
        mask_bottom = mask.y0;
        mask_top    = mask.y1;
    }

    dst_left   = (float) (dst.x0 * 2) / pdst->drawable.width  - 1.0f;
    dst_right  = (float) (dst.x1 * 2) / pdst->drawable.width  - 1.0f;
    dst_bottom = (float) (dst.y0 * 2) / pdst->drawable.height - 1.0f;
    dst_top    = (float) (dst.y1 * 2) / pdst->drawable.height - 1.0f;

    /* push first triangle of the quad to attributes buffer */
    TEGRA_PUSH_VTX_ATTR(dst_left,  dst_bottom,  true);
    TEGRA_PUSH_VTX_ATTR(src_left,  src_bottom,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_left, mask_bottom, push_mask);

    TEGRA_PUSH_VTX_ATTR(dst_left,  dst_top,  true);
    TEGRA_PUSH_VTX_ATTR(src_left,  src_top,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_left, mask_top, push_mask);

    TEGRA_PUSH_VTX_ATTR(dst_right,  dst_top,  true);
    TEGRA_PUSH_VTX_ATTR(src_right,  src_top,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_right, mask_top, push_mask);

    /* push second */
    TEGRA_PUSH_VTX_ATTR(dst_right,  dst_top,  true);
    TEGRA_PUSH_VTX_ATTR(src_right,  src_top,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_right, mask_top, push_mask);

    TEGRA_PUSH_VTX_ATTR(dst_right,  dst_bottom,  true);
    TEGRA_PUSH_VTX_ATTR(src_right,  src_bottom,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_right, mask_bottom, push_mask);

    TEGRA_PUSH_VTX_ATTR(dst_left,  dst_bottom,  true);
    TEGRA_PUSH_VTX_ATTR(src_left,  src_bottom,  push_src);
    TEGRA_PUSH_VTX_ATTR(mask_left, mask_bottom, push_mask);

    tegra->scratch.vtx_cnt += 6;
    tegra->scratch.ops++;

    return;

degenerate:
    ACCEL_MSG("src %dx%d mask %dx%d w:h %d:%d area degenerated, optimized_out %d\n",
              src_x, src_y, mask_x, mask_y, width, height,
              draw_state->optimized_out);
}

static void tegra_exa_done_composite_3d(PixmapPtr pdst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pdst->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *fence = NULL;

    if (tegra->scratch.ops && tegra->cmds->status == TEGRADRM_STREAM_CONSTRUCT) {
        tegra_exa_wait_pixmaps(TEGRA_2D, pdst, 2, tegra->scratch.src,
                               tegra->scratch.mask);

        fence = tegra_exa_submit_3d_state(&tegra->gr3d_state);

        if (fence) {
            /*
            * XXX: Glitches may occur due to lack of support for waitchecks
            *      by kernel driver, they are required for 3D engine to complete
            *      data prefetching before starting to render. Alternative would
            *      be to flush the job, but that impacts performance very
            *      significantly and just happens to minimize the issue, so we
            *      choose glitches to low performance. Mostly fonts rendering is
            *      affected.
            *
            *      See TegraGR3D_DrawPrimitives() in gr3d.c
            */
            tegra_exa_replace_pixmaps_fence(TEGRA_3D, fence, &tegra->scratch, pdst,
                                            2, tegra->scratch.src, tegra->scratch.mask);
        }

    } else if (!tegra_exa_3d_state_deferred(&tegra->gr3d_state)) {
        tegra_exa_3d_state_reset(&tegra->gr3d_state);
    }

    tegra_exa_cool_pixmap(tegra->scratch.src,  false);
    tegra_exa_cool_pixmap(tegra->scratch.mask, false);
    tegra_exa_cool_pixmap(pdst, true);

    tegra_exa_exit_optimization_3d_state(tegra);

    ACCEL_MSG("\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
