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
#include "shaders.h"

#define ErrorMsg(fmt, args...)                                              \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__,                   \
               __LINE__, __func__, ##args)

#define BLUE(c)     (((c) & 0xff)         / 255.0f)
#define GREEN(c)    ((((c) >> 8)  & 0xff) / 255.0f)
#define RED(c)      ((((c) >> 16) & 0xff) / 255.0f)
#define ALPHA(c)    ((((c) >> 24) & 0xff) / 255.0f)

#define TegraPushVtxAttr(x, y, push)                                        \
    if (push) {                                                             \
        tegra->scratch.attribs.map[tegra->scratch.attrib_itr++] = x;        \
        tegra->scratch.attribs.map[tegra->scratch.attrib_itr++] = y;        \
    }

#define TegraSwapRedBlue(v)                                                 \
    ((v & 0xff00ff00) | (v & 0x00ff0000) >> 16 | (v & 0x000000ff) << 16)

#define TEGRA_ATTRIB_BUFFER_SIZE        0x4000

#define TEX_SOLID           0
#define TEX_CLIPPED         1
#define TEX_PAD             2
#define TEX_NORMAL          3
#define TEX_MIRROR          4
#define TEX_EMPTY           5

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
};

static const struct tegra_composite_config composite_cfgs[] = {
    [PictOpOver] = {
        PROG_DEF(over),
    },

    [PictOpOverReverse] = {
        PROG_DEF(over_reverse),
    },

    [PictOpAdd] = {
        PROG_DEF(add),
    },

    [PictOpSrc] = {
        PROG_DEF(src),
    },

    [PictOpIn] = {
        PROG_DEF(in),
    },

    [PictOpInReverse] = {
        PROG_DEF(in_reverse),
    },

    [PictOpOut] = {
        PROG_DEF(out),
    },

    [PictOpOutReverse] = {
        PROG_DEF(out_reverse),
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
    },

    [PictOpAtop] = {
        PROG_DEF(atop),
    },

    [PictOpAtopReverse] = {
        PROG_DEF(atop_reverse),
    },

    [PictOpXor] = {
        PROG_DEF(xor),
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
    },
};

#include "exa_composite_3d_state_tracker.c"

static Bool TegraCompositeReducedTexture(PicturePtr pPicture)
{
    /*
     * GR3D performance is quite slow for a non-trivial shaders,
     * hence we want to reduce the number of 3D instructions as
     * much as possible.
     */
    if (pPicture && pPicture->pDrawable && pPicture->repeat) {
        if (pPicture->pDrawable->width == 1 &&
            pPicture->pDrawable->height == 1)
                return TRUE;
    }

    return FALSE;
}

static Bool TegraCompositePow2Texture(PixmapPtr pix)
{
    if (pix) {
        if (IS_POW2(pix->drawable.width) &&
            IS_POW2(pix->drawable.height))
            return TRUE;
    }

    return FALSE;
}

static Bool TegraCompositeFormatHasAlpha(unsigned format)
{
    switch (format) {
    case PICT_a8:
    case PICT_a8r8g8b8:
        return TRUE;

    default:
        return FALSE;
    }
}

static Bool TegraCompositeTexturePerComponentAlpha(PicturePtr pic)
{
    return pic->componentAlpha;
}

static Bool TegraCompositeProgram3DPreCheck(TegraGR3DDrawStatePtr draw_state)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[draw_state->op];
    const struct shader_program *prog;

    if (draw_state->op > TEGRA_ARRAY_SIZE(composite_cfgs))
        return FALSE;

    prog = cfg->prog[PROG_SEL(draw_state->src.tex_sel,
                              draw_state->mask.tex_sel)];
    if (!prog) {
        FallbackMsg("no shader for operation %d src_sel %u mask_sel %u\n",
                    draw_state->op,
                    draw_state->src.tex_sel,
                    draw_state->mask.tex_sel);
        return FALSE;
    }

    AccelMsg("got shader for operation %d src_sel %u mask_sel %u %s\n",
             draw_state->op, draw_state->src.tex_sel, draw_state->mask.tex_sel,
             prog->name);

    /* we have special shaders for this case */
    if (draw_state->op == PictOpOver &&
        draw_state->src.tex_sel == TEX_NORMAL && draw_state->src.alpha &&
            (draw_state->mask.tex_sel == TEX_SOLID ||
             draw_state->mask.tex_sel == TEX_EMPTY))
        return TRUE;

    /*
     * Only padding and clipping are currently supported by the
     * "generic" shaders for non-pow2 textures.
     */
    if (draw_state->mask.pPix &&
        draw_state->mask.tex_sel != TEX_PAD &&
        draw_state->mask.tex_sel != TEX_CLIPPED &&
            !TegraCompositePow2Texture(draw_state->mask.pPix)) {
        FallbackMsg("unsupported repeat type mask_sel %u\n",
                    draw_state->mask.tex_sel);
        return FALSE;
    }

    if (draw_state->src.pPix &&
        draw_state->src.tex_sel != TEX_PAD &&
        draw_state->src.tex_sel != TEX_CLIPPED &&
            !TegraCompositePow2Texture(draw_state->src.pPix)) {
        FallbackMsg("unsupported repeat type src_sel %u\n",
                    draw_state->src.tex_sel);
        return FALSE;
    }

    return TRUE;
}

static unsigned TegraCompositeFormatToGR3D(unsigned format)
{
    switch (format) {
    case PICT_a8:
        return TGR3D_PIXEL_FORMAT_A8;

    case PICT_r5g6b5:
        return TGR3D_PIXEL_FORMAT_RGB565;

    case PICT_x8r8g8b8:
    case PICT_a8r8g8b8:
        return TGR3D_PIXEL_FORMAT_RGBA8888;

    default:
        return 0;
    }
}

static Bool TegraCompositeCheckTexture(PicturePtr pic)
{
    unsigned width, height;

    if (pic && pic->pDrawable && !TegraCompositeReducedTexture(pic)) {
        width = pic->pDrawable->width;
        height = pic->pDrawable->height;

        if (width > 2048 || height > 2048) {
            FallbackMsg("too large texture %ux%u\n", width, height);
            return FALSE;
        }

        if (pic->transform) {
            FallbackMsg("unsupported transform\n");
            return FALSE;
        }

        if (pic->filter >= PictFilterConvolution) {
            FallbackMsg("unsupported filtering %u\n", pic->filter);
            return FALSE;
        }

        if (!IS_POW2(width) || !IS_POW2(height)) {
            if (pic->filter == PictFilterBilinear) {
                FallbackMsg("bilinear filtering for non-pow2 texture\n");
                return FALSE;
            }
        }
    }

    return TRUE;
}

static Bool TegraCompositeAttribBufferIsFull(TegraEXAScratchPtr scratch)
{
    unsigned attrs_num = 1 + !!scratch->pSrc + !!scratch->pMask;

    return (scratch->attrib_itr * 2 + attrs_num * 24 > TEGRA_ATTRIB_BUFFER_SIZE);
}

Bool TegraEXACheckComposite3D(int op, PicturePtr pSrcPicture,
                              PicturePtr pMaskPicture,
                              PicturePtr pDstPicture)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPicture->pDrawable->pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    if (!tegra->exa_compositing)
        return FALSE;

    if (op > PictOpSaturate) {
        FallbackMsg("unsupported operation %d\n", op);
        return FALSE;
    }

    if (pDstPicture->format != PICT_x8r8g8b8 &&
        pDstPicture->format != PICT_a8r8g8b8 &&
        pDstPicture->format != PICT_r5g6b5 &&
        pDstPicture->format != PICT_a8) {
        FallbackMsg("unsupported format %u\n", pDstPicture->format);
        return FALSE;
    }

    if (pSrcPicture) {
        if (pSrcPicture->format != PICT_x8r8g8b8 &&
            pSrcPicture->format != PICT_a8r8g8b8 &&
            pSrcPicture->format != PICT_r5g6b5 &&
            pSrcPicture->format != PICT_a8) {
            FallbackMsg("unsupported format %u\n", pSrcPicture->format);
            return FALSE;
        }

        if (pSrcPicture->pDrawable) {
            if (!TegraCompositeCheckTexture(pSrcPicture))
                return FALSE;
        } else {
            if (pSrcPicture->pSourcePict->type != SourcePictTypeSolidFill) {
                FallbackMsg("unsupported fill type %u\n",
                            pSrcPicture->pSourcePict->type);
                return FALSE;
            }
        }
    }

    if (pMaskPicture) {
        if (pMaskPicture->format != PICT_x8r8g8b8 &&
            pMaskPicture->format != PICT_a8r8g8b8 &&
            pMaskPicture->format != PICT_r5g6b5 &&
            pMaskPicture->format != PICT_a8) {
            FallbackMsg("unsupported format %u\n", pMaskPicture->format);
            return FALSE;
        }

        if (pMaskPicture->pDrawable) {
            if (!TegraCompositeCheckTexture(pMaskPicture))
                return FALSE;
        } else {
            if (pMaskPicture->pSourcePict->type != SourcePictTypeSolidFill) {
                FallbackMsg("unsupported fill type %u\n",
                            pMaskPicture->pSourcePict->type);
                return FALSE;
            }
        }
    }

    return TRUE;
}

static Pixel TegraCompositeGetReducedTextureColor(PixmapPtr pix)
{
    Pixel color = 0x00000000;
    void *ptr;

    if (TegraEXAPrepareCPUAccess(pix, EXA_PREPARE_SRC, &ptr)) {
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

        TegraEXAFinishCPUAccess(pix, EXA_PREPARE_SRC);
    }

    AccelMsg("color 0x%08lx\n", color);

    return color;
}

Bool TegraEXAPrepareComposite3D(int op,
                                PicturePtr pSrcPicture,
                                PicturePtr pMaskPicture,
                                PicturePtr pDstPicture,
                                PixmapPtr pSrc,
                                PixmapPtr pMask,
                                PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    Bool mask_tex = (pMaskPicture && pMaskPicture->pDrawable);
    Bool src_tex = (pSrcPicture && pSrcPicture->pDrawable);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    TegraGR3DDrawState draw_state = { 0 };
    Bool mask_tex_reduced = TRUE;
    Bool src_tex_reduced = TRUE;
    unsigned mask_sel;
    unsigned src_sel;
    Pixel solid;
    Bool alpha;

    if (!TegraCompositeCheckTexture(pMaskPicture))
            return FALSE;

    if (!TegraCompositeCheckTexture(pSrcPicture))
            return FALSE;

    if (src_tex && TegraCompositeReducedTexture(pSrcPicture))
        src_tex = FALSE;
    else
        src_tex_reduced = FALSE;

    if (src_tex_reduced)
        AccelMsg("src texture reduced\n");

    if (mask_tex && TegraCompositeReducedTexture(pMaskPicture))
        mask_tex = FALSE;
    else
        mask_tex_reduced = FALSE;

    if (mask_tex_reduced)
        AccelMsg("mask texture reduced\n");

    tegra->scratch.pMask = (op != PictOpClear && mask_tex) ? pMask : NULL;
    tegra->scratch.pSrc = (op != PictOpClear && src_tex) ? pSrc : NULL;
    tegra->scratch.ops = 0;

    if (pSrcPicture) {
        if (tegra->scratch.pSrc) {
            switch (pSrcPicture->repeatType) {
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
                FallbackMsg("unsupported repeat type %u\n",
                            pSrcPicture->repeatType);
                return FALSE;
            }

            draw_state.src.pow2             = TegraCompositePow2Texture(pSrc);
            draw_state.src.format           = TegraCompositeFormatToGR3D(pSrcPicture->format);
            draw_state.src.alpha            = TegraCompositeFormatHasAlpha(pSrcPicture->format);
            draw_state.src.bilinear         = (pSrcPicture->filter == PictFilterBilinear);
            draw_state.src.tex_sel          = src_sel;
            draw_state.src.pPix             = pSrc;
        } else {
            if (op != PictOpClear && pSrcPicture) {
                if (src_tex_reduced)
                    solid = TegraCompositeGetReducedTextureColor(pSrc);
                else
                    solid = pSrcPicture->pSourcePict->solidFill.color;

                if (pSrcPicture->format == PICT_r5g6b5)
                    solid = TegraPixelRGB565to888(solid);

                alpha = TegraCompositeFormatHasAlpha(pSrcPicture->format);
                if (!alpha)
                    solid |= 0xff000000;

                src_sel = TEX_SOLID;
            } else {
                solid = 0x00000000;
            }

            if (solid == 0x00000000)
                src_sel = TEX_EMPTY;

            AccelMsg("src solid 0x%08lx\n", solid);

            draw_state.src.tex_sel = src_sel;
            draw_state.src.solid = solid;
        }
    } else {
        draw_state.src.tex_sel = TEX_EMPTY;
        draw_state.src.solid = 0x00000000;
    }

    if (pMaskPicture) {
        if (tegra->scratch.pMask) {
            switch (pMaskPicture->repeatType) {
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
                FallbackMsg("unsupported repeat type %u\n",
                            pMaskPicture->repeatType);
                return FALSE;
            }

            draw_state.mask.pow2            = TegraCompositePow2Texture(pMask);
            draw_state.mask.format          = TegraCompositeFormatToGR3D(pMaskPicture->format);
            draw_state.mask.alpha           = TegraCompositeFormatHasAlpha(pMaskPicture->format);
            draw_state.mask.component_alpha = TegraCompositeTexturePerComponentAlpha(pMaskPicture);
            draw_state.mask.bilinear        = (pMaskPicture->filter == PictFilterBilinear);
            draw_state.mask.tex_sel         = mask_sel;
            draw_state.mask.pPix            = pMask;
        } else {
            if (op != PictOpClear && pMaskPicture) {
                if (mask_tex_reduced)
                    solid = TegraCompositeGetReducedTextureColor(pMask);
                else
                    solid = pMaskPicture->pSourcePict->solidFill.color;

                if (pMaskPicture->format == PICT_r5g6b5)
                    solid = TegraPixelRGB565to888(solid);

                if (!pMaskPicture->componentAlpha)
                    solid |= solid >> 24 | solid >> 16 | solid >> 8;

                alpha = TegraCompositeFormatHasAlpha(pMaskPicture->format);
                if (!alpha)
                    solid |= 0xff000000;

                mask_sel = TEX_SOLID;
            } else {
                solid = 0xffffffff;
            }

            if (solid == 0xffffffff)
                mask_sel = TEX_EMPTY;

            AccelMsg("mask solid 0x%08lx\n", solid);

            draw_state.mask.tex_sel = mask_sel;
            draw_state.mask.solid = solid;
        }
    } else {
        draw_state.mask.tex_sel = TEX_EMPTY;
        draw_state.mask.solid = 0xffffffff;
    }

    draw_state.dst.format = TegraCompositeFormatToGR3D(pDstPicture->format);
    draw_state.dst.alpha  = TegraCompositeFormatHasAlpha(pDstPicture->format);
    draw_state.dst.pPix   = pDst;

    draw_state.op = op;

    if (!TegraCompositeProgram3DPreCheck(&draw_state))
        return FALSE;

    return TegraGR3DStateAppend(&tegra->gr3d_state, tegra, &draw_state);
}

void TegraEXAComposite3D(PixmapPtr pDst,
                         int srcX, int srcY,
                         int maskX, int maskY,
                         int dstX, int dstY,
                         int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    TegraGR3DStatePtr state = &tegra->gr3d_state;
    TegraGR3DDrawStatePtr draw_state = &state->new;
    float dst_left, dst_right, dst_top, dst_bottom;
    float src_left, src_right, src_top, src_bottom;
    float mask_left, mask_right, mask_top, mask_bottom;
    bool push_mask = !!tegra->scratch.pMask;
    bool push_src = !!tegra->scratch.pSrc;

    /* if attributes buffer is full, do nothing for now (TODO better job) */
    if (TegraCompositeAttribBufferIsFull(&tegra->scratch)) {
        ErrorMsg("FIXME: attributes buffer is full\n");
        return;
    }

    if (push_src) {
        src_left   = (float) srcX   / tegra->scratch.pSrc->drawable.width;
        src_right  = (float) width  / tegra->scratch.pSrc->drawable.width + src_left;
        src_bottom = (float) srcY   / tegra->scratch.pSrc->drawable.height;
        src_top    = (float) height / tegra->scratch.pSrc->drawable.height + src_bottom;

        if ((src_left < 0.0f || src_right > 1.0f) &&
            tegra->scratch.pSrc->drawable.width > 1)
                draw_state->src.coords_wrap = TRUE;

        if ((src_bottom < 0.0f || src_top > 1.0f) &&
            tegra->scratch.pSrc->drawable.height > 1)
                draw_state->src.coords_wrap = TRUE;
    }

    if (push_mask) {
        mask_left   = (float) maskX  / tegra->scratch.pMask->drawable.width;
        mask_right  = (float) width  / tegra->scratch.pMask->drawable.width + mask_left;
        mask_bottom = (float) maskY  / tegra->scratch.pMask->drawable.height;
        mask_top    = (float) height / tegra->scratch.pMask->drawable.height + mask_bottom;

        if ((mask_left < 0.0f || mask_right > 1.0f) &&
            tegra->scratch.pMask->drawable.width > 1)
                draw_state->mask.coords_wrap = TRUE;

        if ((mask_bottom < 0.0f || mask_top > 1.0f) &&
            tegra->scratch.pMask->drawable.height > 1)
                draw_state->mask.coords_wrap = TRUE;
    }

    dst_left   = (float) (dstX   * 2) / pDst->drawable.width  - 1.0f;
    dst_right  = (float) (width  * 2) / pDst->drawable.width  + dst_left;
    dst_bottom = (float) (dstY   * 2) / pDst->drawable.height - 1.0f;
    dst_top    = (float) (height * 2) / pDst->drawable.height + dst_bottom;

    /* push first triangle of the quad to attributes buffer */
    TegraPushVtxAttr(dst_left,  dst_bottom,  true);
    TegraPushVtxAttr(src_left,  src_bottom,  push_src);
    TegraPushVtxAttr(mask_left, mask_bottom, push_mask);

    TegraPushVtxAttr(dst_left,  dst_top,  true);
    TegraPushVtxAttr(src_left,  src_top,  push_src);
    TegraPushVtxAttr(mask_left, mask_top, push_mask);

    TegraPushVtxAttr(dst_right,  dst_top,  true);
    TegraPushVtxAttr(src_right,  src_top,  push_src);
    TegraPushVtxAttr(mask_right, mask_top, push_mask);

    /* push second */
    TegraPushVtxAttr(dst_right,  dst_top,  true);
    TegraPushVtxAttr(src_right,  src_top,  push_src);
    TegraPushVtxAttr(mask_right, mask_top, push_mask);

    TegraPushVtxAttr(dst_right,  dst_bottom,  true);
    TegraPushVtxAttr(src_right,  src_bottom,  push_src);
    TegraPushVtxAttr(mask_right, mask_bottom, push_mask);

    TegraPushVtxAttr(dst_left,  dst_bottom,  true);
    TegraPushVtxAttr(src_left,  src_bottom,  push_src);
    TegraPushVtxAttr(mask_left, mask_bottom, push_mask);

    tegra->scratch.vtx_cnt += 6;
    tegra->scratch.ops++;
}

void TegraEXADoneComposite3D(PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *fence = NULL;
    TegraPixmapPtr priv;

    if (tegra->scratch.ops && tegra->cmds.status == TEGRADRM_STREAM_CONSTRUCT) {
        if (tegra->scratch.pSrc) {
            priv = exaGetPixmapDriverPrivate(tegra->scratch.pSrc);

            if (priv->fence_write && priv->fence_write->gr2d) {
                TegraEXAWaitFence(priv->fence_write);

                tegra_stream_put_fence(priv->fence_write);
                priv->fence_write = NULL;
            }
        }

        if (tegra->scratch.pMask) {
            priv = exaGetPixmapDriverPrivate(tegra->scratch.pMask);

            if (priv->fence_write && priv->fence_write->gr2d) {
                TegraEXAWaitFence(priv->fence_write);

                tegra_stream_put_fence(priv->fence_write);
                priv->fence_write = NULL;
            }
        }

        priv = exaGetPixmapDriverPrivate(pDst);
        if (priv->fence_write && priv->fence_write->gr2d)
            TegraEXAWaitFence(priv->fence_write);

        if (priv->fence_read && priv->fence_read->gr2d)
            TegraEXAWaitFence(priv->fence_read);

        fence = TegraGR3DStateSubmit(&tegra->gr3d_state);

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

        if (tegra->scratch.pMask) {
            priv = exaGetPixmapDriverPrivate(tegra->scratch.pMask);

            if (priv->fence_read != fence) {
                tegra_stream_put_fence(priv->fence_read);
                priv->fence_read = tegra_stream_ref_fence(fence, &tegra->scratch);
            }
        }
    } else {
        TegraGR3DStateReset(&tegra->gr3d_state);
    }

    TegraEXACoolPixmap(tegra->scratch.pSrc, FALSE);
    TegraEXACoolPixmap(tegra->scratch.pMask, FALSE);
    TegraEXACoolPixmap(pDst, TRUE);

    AccelMsg("\n");
}

/* vim: set et sts=4 sw=4 ts=4: */
