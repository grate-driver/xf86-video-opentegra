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

#define BLUE(c)     (((c) & 0xff)         / 255.0f)
#define GREEN(c)    ((((c) >> 8)  & 0xff) / 255.0f)
#define RED(c)      ((((c) >> 16) & 0xff) / 255.0f)
#define ALPHA(c)    ((((c) >> 24) & 0xff) / 255.0f)

#define TegraPushVtxAttr(x, y, push)                                        \
    if (push) {                                                             \
        tegra->scratch.attribs->map[tegra->scratch.attrib_itr++] = x;       \
        tegra->scratch.attribs->map[tegra->scratch.attrib_itr++] = y;       \
    }

#define TegraSwapRedBlue(v)                                                 \
    ((v & 0xff00ff00) | (v & 0x00ff0000) >> 16 | (v & 0x000000ff) << 16)

#define TEGRA_ATTRIB_BUFFER_SIZE        0x1000

struct tegra_composit_config {
    /* prog[MASK_TEX_USED][SRC_TEX_USED] */
    struct shader_program *prog[2][2];
};

static const struct tegra_composit_config composit_cfgs[] = {
    [PictOpOver] = {
        .prog[1][1] = &prog_blend_over,
        .prog[0][1] = &prog_blend_over_solid_src,
        .prog[1][0] = &prog_blend_over_solid_mask,
        .prog[0][0] = &prog_blend_over_solid_mask_src,
    },

    [PictOpOverReverse] = {
        .prog[1][1] = &prog_blend_over_reverse,
        .prog[0][1] = &prog_blend_over_reverse_solid_src,
        .prog[1][0] = &prog_blend_over_reverse_solid_mask,
        .prog[0][0] = &prog_blend_over_reverse_solid_mask_src,
    },

    [PictOpAdd] = {
        .prog[1][1] = &prog_blend_add,
        .prog[0][1] = &prog_blend_add_solid_src,
        .prog[1][0] = &prog_blend_add_solid_mask,
        .prog[0][0] = &prog_blend_add_solid_mask_src,
    },

    [PictOpSrc] = {
        .prog[1][1] = &prog_blend_src,
        .prog[0][1] = &prog_blend_src_solid_src,
        .prog[1][0] = &prog_blend_src_solid_mask,
        .prog[0][0] = &prog_blend_src_solid_mask_src,
    },

    [PictOpClear] = {
        .prog[1][1] = &prog_blend_src_solid_mask_src,
        .prog[0][1] = &prog_blend_src_solid_mask_src,
        .prog[1][0] = &prog_blend_src_solid_mask_src,
        .prog[0][0] = &prog_blend_src_solid_mask_src,
    },

    [PictOpIn] = {
        .prog[1][1] = &prog_blend_in,
        .prog[0][1] = &prog_blend_in_solid_src,
        .prog[1][0] = &prog_blend_in_solid_mask,
        .prog[0][0] = &prog_blend_in_solid_mask_src,
    },

    [PictOpInReverse] = {
        .prog[1][1] = &prog_blend_in_reverse,
        .prog[0][1] = &prog_blend_in_reverse_solid_src,
        .prog[1][0] = &prog_blend_in_reverse_solid_mask,
        .prog[0][0] = &prog_blend_in_reverse_solid_mask_src,
    },

    [PictOpOut] = {
        .prog[1][1] = &prog_blend_out,
        .prog[0][1] = &prog_blend_out_solid_src,
        .prog[1][0] = &prog_blend_out_solid_mask,
        .prog[0][0] = &prog_blend_out_solid_mask_src,
    },

    [PictOpOutReverse] = {
        .prog[1][1] = &prog_blend_out_reverse,
        .prog[0][1] = &prog_blend_out_reverse_solid_src,
        .prog[1][0] = &prog_blend_out_reverse_solid_mask,
        .prog[0][0] = &prog_blend_out_reverse_solid_mask_src,
    },

    [PictOpDst] = {
        .prog[1][0] = &prog_blend_dst,
        .prog[0][0] = &prog_blend_dst_solid_mask,
    },

    [PictOpAtop] = {
        .prog[1][1] = &prog_blend_atop,
        .prog[0][1] = &prog_blend_atop_solid_src,
        .prog[1][0] = &prog_blend_atop_solid_mask,
        .prog[0][0] = &prog_blend_atop_solid_mask_src,
    },

    [PictOpAtopReverse] = {
        .prog[1][1] = &prog_blend_atop_reverse,
        .prog[0][1] = &prog_blend_atop_reverse_solid_src,
        .prog[1][0] = &prog_blend_atop_reverse_solid_mask,
        .prog[0][0] = &prog_blend_atop_reverse_solid_mask_src,
    },

    [PictOpXor] = {
        .prog[1][1] = &prog_blend_xor,
        .prog[0][1] = &prog_blend_xor_solid_src,
        .prog[1][0] = &prog_blend_xor_solid_mask,
        .prog[0][0] = &prog_blend_xor_solid_mask_src,
    },

    [PictOpSaturate] = {
        .prog[1][1] = &prog_blend_saturate,
        .prog[0][1] = &prog_blend_saturate_solid_src,
        .prog[1][0] = &prog_blend_saturate_solid_mask,
    },
};

static int TegraCompositeAllocateAttribBuffer(struct drm_tegra *drm,
                                              TegraEXAPtr tegra)
{
    struct tegra_attrib_bo *old = tegra->scratch.attribs;
    struct tegra_attrib_bo *new;
    int err;

    tegra->scratch.attribs_alloc_err = TRUE;

    new = calloc(1, sizeof(*new));
    if (!new)
        return -1;

    err = drm_tegra_bo_new(&new->bo, drm, 0, TEGRA_ATTRIB_BUFFER_SIZE);
    if (err) {
        free(new);
        return err;
    }

    err = drm_tegra_bo_map(new->bo, (void**)&new->map);
    if (err) {
        drm_tegra_bo_unref(new->bo);
        free(new);
        return err;
    }

    tegra->scratch.attribs_alloc_err = FALSE;
    tegra->scratch.attrib_itr = 0;
    tegra->scratch.attribs = new;
    new->next = old;

    return 0;
}

void TegraCompositeReleaseAttribBuffers(TegraEXAScratchPtr scratch)
{
    if (scratch->attribs) {
        struct tegra_attrib_bo *attribs_bo = scratch->attribs;
        struct tegra_attrib_bo *next;

        while (attribs_bo) {
            next = attribs_bo->next;
            drm_tegra_bo_unref(attribs_bo->bo);
            free(attribs_bo);
            attribs_bo = next;
        }

        scratch->attribs = NULL;
        scratch->attrib_itr = 0;
    }
}

static const struct shader_program * TegraCompositeProgram3D(
                int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture)
{
    const struct tegra_composit_config *cfg = &composit_cfgs[op];
    Bool mask_tex = (pMaskPicture && pMaskPicture->pDrawable);
    Bool src_tex = (pSrcPicture && pSrcPicture->pDrawable);

    if (op > PictOpSaturate) {
        FallbackMsg("unsupported operation %d\n", op);
        return NULL;
    }

    return cfg->prog[src_tex][mask_tex];
}

static unsigned TegraCompositeFormatToGR3D(unsigned format)
{
    switch (format) {
    case PICT_a8:
        return TGR3D_PIXEL_FORMAT_A8;

    case PICT_r5g6b5:
    case PICT_b5g6r5:
        return TGR3D_PIXEL_FORMAT_RGB565;

    case PICT_x8b8g8r8:
    case PICT_a8b8g8r8:
    case PICT_x8r8g8b8:
    case PICT_a8r8g8b8:
        return TGR3D_PIXEL_FORMAT_RGBA8888;

    default:
        return 0;
    }
}

static Bool TegraCompositeFormatHasAlpha(unsigned format)
{
    switch (format) {
    case PICT_a8:
    case PICT_a8b8g8r8:
    case PICT_a8r8g8b8:
        return TRUE;

    default:
        return FALSE;
    }
}

static Bool TegraCompositeFormatSwapRedBlue3D(unsigned format)
{
    switch (format) {
    case PICT_x8b8g8r8:
    case PICT_a8b8g8r8:
    case PICT_r5g6b5:
        return TRUE;

    default:
        return FALSE;
    }
}

static Bool TegraCompositeFormatSwapRedBlue2D(unsigned format)
{
    switch (format) {
    case PICT_x8b8g8r8:
    case PICT_a8b8g8r8:
    case PICT_b5g6r5:
        return TRUE;

    default:
        return FALSE;
    }
}

static Bool TegraCompositeCheckTexture(PicturePtr pic, PixmapPtr pix)
{
    unsigned width, height;

    if (pic) {
        width = pic->pDrawable->width;
        height = pic->pDrawable->height;

        if (width > 2048 || height > 2048) {
            FallbackMsg("too large texture %ux%u\n", width, height);
            return FALSE;
        }

        if (!IS_POW2(width) || !IS_POW2(height)) {
            if (pic->filter == PictFilterBilinear) {
                FallbackMsg("bilinear filtering for non-pow2 texture\n");
                return FALSE;
            }

            if (pic->repeat && (pic->repeatType == RepeatReflect ||
                                pic->repeatType == RepeatNormal)) {
                FallbackMsg("unsupported repeat type %u\n", pic->repeatType);
                return FALSE;
            }
        }
    } else if (pix) {
        width = pix->drawable.width;
        height = pix->drawable.height;

        if (width > 2048 || height > 2048) {
            FallbackMsg("too large texture %ux%u\n", width, height);
            return FALSE;
        }
    }

    return TRUE;
}

static void TegraCompositeSetupTexture(struct tegra_stream *cmds,
                                       unsigned index,
                                       PicturePtr pic,
                                       PixmapPtr pix)
{
    Bool wrap_mirrored_repeat = FALSE;
    Bool wrap_clamp_to_edge = TRUE;
    Bool bilinear = FALSE;

    if (pic->repeat) {
        wrap_mirrored_repeat = (pic->repeatType == RepeatReflect);
        wrap_clamp_to_edge = (pic->repeatType == RepeatPad);
    }

    if (pic->filter == PictFilterBilinear)
        bilinear = TRUE;

    TegraGR3D_SetupTextureDesc(cmds, index,
                               TegraEXAPixmapBO(pix),
                               TegraEXAPixmapOffset(pix),
                               pix->drawable.width,
                               pix->drawable.height,
                               TegraCompositeFormatToGR3D(pic->format),
                               bilinear, false, bilinear,
                               wrap_clamp_to_edge,
                               wrap_mirrored_repeat);
}

static void TegraCompositeSetupAttributes(TegraEXAPtr tegra)
{
    struct tegra_exa_scratch *scratch = &tegra->scratch;
    struct tegra_stream *cmds = &tegra->cmds;
    unsigned attrs_num = 1 + !!scratch->pSrc + !!scratch->pMask;
    unsigned attribs_offset = scratch->attrib_itr * 2;

    TegraGR3D_SetupAttribute(cmds, 0, scratch->attribs->bo,
                             attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                             2, 4 * attrs_num);

    if (scratch->pSrc) {
        attribs_offset += 4;

        TegraGR3D_SetupAttribute(cmds, 1, scratch->attribs->bo,
                                 attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                 2, 4 * attrs_num);
    }

    if (scratch->pMask) {
        attribs_offset += 4;

        TegraGR3D_SetupAttribute(cmds, 2, scratch->attribs->bo,
                                 attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                 2, 4 * attrs_num);
    }
}

static Bool TegraCompositeAttribBufferIsFull(TegraEXAScratchPtr scratch)
{
    unsigned attrs_num = 1 + !!scratch->pSrc + !!scratch->pMask;

    return (scratch->attrib_itr * 2 + attrs_num * 24 > TEGRA_ATTRIB_BUFFER_SIZE);
}

static void TegraEXACompositeDraw(TegraEXAPtr tegra)
{
    struct tegra_exa_scratch *scratch = &tegra->scratch;
    struct tegra_stream *cmds = &tegra->cmds;

    if (scratch->vtx_cnt) {
        TegraGR3D_SetupDrawParams(cmds, TGR3D_PRIMITIVE_TYPE_TRIANGLES,
                                  TGR3D_INDEX_MODE_NONE, 0);
        TegraGR3D_DrawPrimitives(cmds, 0, scratch->vtx_cnt);

        scratch->vtx_cnt = 0;
        scratch->ops++;
    }
}

static void TegraCompositeFlush(struct drm_tegra *drm, TegraEXAPtr tegra)
{
    TegraEXACompositeDraw(tegra);
    TegraCompositeAllocateAttribBuffer(drm, tegra);
    TegraCompositeSetupAttributes(tegra);
}

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

static Bool TegraEXACheckComposite2D(TegraEXAPtr tegra,
                                     int op, PicturePtr pSrcPicture,
                                     PicturePtr pMaskPicture,
                                     PicturePtr pDstPicture)
{
    enum Tegra2DOrientation orientation;

    if (op != PictOpSrc && op != PictOpClear)
        return FALSE;

    if (pMaskPicture && pMaskPicture->pDrawable)
        return FALSE;

    if (pSrcPicture && pSrcPicture->pDrawable) {
        if (op != PictOpSrc)
            return FALSE;

        if (pMaskPicture)
            return FALSE;

        if (!pSrcPicture->transform)
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
    }

    return TRUE;
}

Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                            PicturePtr pMaskPicture,
                            PicturePtr pDstPicture)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPicture->pDrawable->pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    if (TegraEXACheckComposite2D(TegraPTR(pScrn)->exa,
                                 op, pSrcPicture, pMaskPicture, pDstPicture))
        return TRUE;

    if (!TegraCompositeProgram3D(op, pSrcPicture, pMaskPicture))
        return FALSE;

    if (pDstPicture->format != PICT_x8r8g8b8 &&
        pDstPicture->format != PICT_a8r8g8b8 &&
        pDstPicture->format != PICT_x8b8g8r8 &&
        pDstPicture->format != PICT_a8b8g8r8 &&
        pDstPicture->format != PICT_r5g6b5 &&
        pDstPicture->format != PICT_b5g6r5 &&
        pDstPicture->format != PICT_a8) {
        FallbackMsg("unsupported format %u\n", pDstPicture->format);
        return FALSE;
    }

    if (pSrcPicture) {
        if (pSrcPicture->format != PICT_x8r8g8b8 &&
            pSrcPicture->format != PICT_a8r8g8b8 &&
            pSrcPicture->format != PICT_x8b8g8r8 &&
            pSrcPicture->format != PICT_a8b8g8r8 &&
            pSrcPicture->format != PICT_r5g6b5 &&
            pSrcPicture->format != PICT_b5g6r5 &&
            pSrcPicture->format != PICT_a8) {
            FallbackMsg("unsupported format %u\n", pSrcPicture->format);
            return FALSE;
        }

        if (pSrcPicture->pDrawable) {
            if (pSrcPicture->filter >= PictFilterConvolution) {
                FallbackMsg("unsupported filtering %u\n", pSrcPicture->filter);
                return FALSE;
            }

            if (!TegraCompositeCheckTexture(pSrcPicture, NULL))
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
            pMaskPicture->format != PICT_x8b8g8r8 &&
            pMaskPicture->format != PICT_a8b8g8r8 &&
            pMaskPicture->format != PICT_r5g6b5 &&
            pMaskPicture->format != PICT_b5g6r5 &&
            pMaskPicture->format != PICT_a8) {
            FallbackMsg("unsupported format %u\n", pMaskPicture->format);
            return FALSE;
        }

        if (pMaskPicture->pDrawable) {
            if (pMaskPicture->transform) {
                FallbackMsg("unsupported transform\n");
                return FALSE;
            }

            if (pMaskPicture->filter >= PictFilterConvolution) {
                FallbackMsg("unsupported filtering %u\n", pMaskPicture->filter);
                return FALSE;
            }

            if (!TegraCompositeCheckTexture(pMaskPicture, NULL))
                return FALSE;
        } else {
            if (pMaskPicture->pSourcePict->type != SourcePictTypeSolidFill) {
                FallbackMsg("unsupported fill type %u\n",
                            pMaskPicture->pSourcePict->type);
                return FALSE;
            }
        }
    }

    if (!tegra->exa_compositing)
        return FALSE;

    /* TODO: support GR3D transforms */
    if (pSrcPicture && pSrcPicture->pDrawable && pSrcPicture->transform) {
        FallbackMsg("unsupported transform\n");
        return FALSE;
    }

    return TRUE;
}

static Pixel TegraPixelRGB565to888(Pixel pixel)
{
    Pixel p = 0;

    p |= 0xff000000;
    p |=  ((pixel >> 11)   * 255 + 15) / 31;
    p |=  (((pixel >> 5) & 0x3f) * 255 + 31) / 63;
    p |=  ((pixel & 0x3f)  * 255 + 15) / 31;

    return p;
}

static Pixel TegraPixelRGB888to565(Pixel pixel)
{
    unsigned red, green, blue;
    Pixel p = 0;

    red   = (pixel & 0x00ff0000) >> 16;
    green = (pixel & 0x0000ff00) >> 8;
    blue  = (pixel & 0x000000ff) >> 0;

    p |= ((red >> 3) & 0x1f) << 11;
    p |= ((green >> 2) & 0x3f) << 5;
    p |= (blue >> 3) & 0x1f;

    return p;
}

static Bool TegraEXAPrepareSolid2D(int op,
                                   PicturePtr pSrcPicture,
                                   PicturePtr pMaskPicture,
                                   PicturePtr pDstPicture,
                                   PixmapPtr pDst)
{
    Pixel solid, mask;

    if (pSrcPicture && pSrcPicture->pDrawable)
        return FALSE;

    if (pMaskPicture && pMaskPicture->pDrawable)
        return FALSE;

    if (op == PictOpSrc) {
        solid = pSrcPicture ? pSrcPicture->pSourcePict->solidFill.color :
                              0x00000000;

        mask = pMaskPicture ? pMaskPicture->pSourcePict->solidFill.color :
                              0xffffffff;

        if (pSrcPicture) {
            if (pSrcPicture->format == PICT_r5g6b5 ||
                pSrcPicture->format == PICT_b5g6r5)
                solid = TegraPixelRGB565to888(solid);

            if (TegraCompositeFormatSwapRedBlue2D(pDstPicture->format) !=
                TegraCompositeFormatSwapRedBlue2D(pSrcPicture->format))
                solid = TegraSwapRedBlue(solid);
        }

        if (pMaskPicture) {
            if (pMaskPicture->format == PICT_r5g6b5 ||
                pMaskPicture->format == PICT_b5g6r5)
                mask = TegraPixelRGB565to888(mask);

            if (TegraCompositeFormatSwapRedBlue2D(pDstPicture->format) !=
                TegraCompositeFormatSwapRedBlue2D(pMaskPicture->format))
                mask = TegraSwapRedBlue(mask);
        }

        solid &= mask;

        if (pDstPicture->format == PICT_r5g6b5 ||
            pDstPicture->format == PICT_b5g6r5)
            solid = TegraPixelRGB888to565(solid);

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

static Bool TegraEXAPrepareCopy2D(int op,
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

    if (TegraEXAPrepareCopy2D(op, pSrcPicture, pMaskPicture, pDstPicture,
                              pSrc, pDst)) {
        tegra->scratch.op2d = TEGRA2D_COPY;
        return TRUE;
    }

    return FALSE;
}

static Bool TegraEXAPrepareComposite3D(int op,
                                       PicturePtr pSrcPicture,
                                       PicturePtr pMaskPicture,
                                       PicturePtr pDstPicture,
                                       PixmapPtr pSrc,
                                       PixmapPtr pMask,
                                       PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    struct tegra_stream *cmds = &tegra->cmds;
    const struct shader_program *prog;
    TegraPixmapPtr priv;
    Bool mask_tex = (pMaskPicture && pMaskPicture->pDrawable);
    Bool src_tex = (pSrcPicture && pSrcPicture->pDrawable);
    Bool clamp_src = FALSE;
    Bool clamp_mask = FALSE;
    Bool swap_red_blue;
    Bool dst_alpha;
    Bool alpha;
    Pixel solid;
    int err;

    if (src_tex && pSrcPicture->transform)
        return FALSE;

    prog = TegraCompositeProgram3D(op, pSrcPicture, pMaskPicture);
    if (!prog)
        return FALSE;

    tegra->scratch.pMask = (op != PictOpClear && mask_tex) ? pMask : NULL;
    tegra->scratch.pSrc = (op != PictOpClear && src_tex) ? pSrc : NULL;
    tegra->scratch.ops = 0;

    err = TegraCompositeAllocateAttribBuffer(TegraPTR(pScrn)->drm, tegra);
    if (err)
        return FALSE;

    TegraEXAThawPixmap(pSrc, TRUE);
    TegraEXAThawPixmap(pMask, TRUE);
    TegraEXAThawPixmap(pDst, TRUE);

    if (pSrc) {
        priv = exaGetPixmapDriverPrivate(pSrc);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FallbackMsg("unaccelerateable pixmap\n");
            return FALSE;
        }
    }

    if (pMask) {
        priv = exaGetPixmapDriverPrivate(pMask);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FallbackMsg("unaccelerateable pixmap\n");
            return FALSE;
        }
    }

    priv = exaGetPixmapDriverPrivate(pDst);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FallbackMsg("unaccelerateable pixmap\n");
        return FALSE;
    }

    err = tegra_stream_begin(cmds, tegra->gr3d);
    if (err)
        return FALSE;

    tegra_stream_prep(&tegra->cmds, 1);
    tegra_stream_push_setclass(cmds, HOST1X_CLASS_GR3D);

    TegraGR3D_Initialize(cmds, prog);

    TegraGR3D_SetupScissor(cmds, 0, 0,
                           pDst->drawable.width,
                           pDst->drawable.height);
    TegraGR3D_SetupViewportBiasScale(cmds, 0.0f, 0.0f, 0.5f,
                                     pDst->drawable.width,
                                     pDst->drawable.height, 0.5f);
    TegraGR3D_SetupRenderTarget(cmds, 1,
                                TegraEXAPixmapBO(pDst),
                                TegraEXAPixmapOffset(pDst),
                                TegraCompositeFormatToGR3D(pDstPicture->format),
                                exaGetPixmapPitch(pDst));
    TegraGR3D_EnableRenderTargets(cmds, 1 << 1);

    TegraCompositeSetupAttributes(tegra);

    dst_alpha = TegraCompositeFormatHasAlpha(pDstPicture->format);

    if (tegra->scratch.pSrc) {
        clamp_src = !pSrcPicture->repeat;

        TegraCompositeSetupTexture(cmds, 0, pSrcPicture, pSrc);

        swap_red_blue = TegraCompositeFormatSwapRedBlue3D(pDstPicture->format) !=
                        TegraCompositeFormatSwapRedBlue3D(pSrcPicture->format);

        alpha = TegraCompositeFormatHasAlpha(pSrcPicture->format);
        TegraGR3D_UploadConstFP(cmds, 5, FX10x2(alpha, swap_red_blue));
    } else {
        if (op != PictOpClear && pSrcPicture) {
            solid = pSrcPicture->pSourcePict->solidFill.color;

            if (pSrcPicture->format == PICT_r5g6b5 ||
                pSrcPicture->format == PICT_b5g6r5)
                solid = TegraPixelRGB565to888(solid);
        } else {
            solid = 0x00000000;
        }

        if (TegraCompositeFormatSwapRedBlue3D(pDstPicture->format) !=
            TegraCompositeFormatSwapRedBlue3D(pSrcPicture->format))
            solid = TegraSwapRedBlue(solid);

        TegraGR3D_UploadConstFP(cmds, 0, FX10x2(BLUE(solid), GREEN(solid)));
        TegraGR3D_UploadConstFP(cmds, 1, FX10x2(RED(solid), ALPHA(solid)));
    }

    if (tegra->scratch.pMask) {
        clamp_mask = !pMaskPicture->repeat;

        TegraCompositeSetupTexture(cmds, 1, pMaskPicture, pMask);

        swap_red_blue = TegraCompositeFormatSwapRedBlue3D(pDstPicture->format) !=
                        TegraCompositeFormatSwapRedBlue3D(pMaskPicture->format);

        alpha = TegraCompositeFormatHasAlpha(pMaskPicture->format);
        TegraGR3D_UploadConstFP(cmds, 6, FX10x2(pMaskPicture->componentAlpha, alpha));
        TegraGR3D_UploadConstFP(cmds, 7, FX10x2(swap_red_blue, clamp_mask));
    } else {
        if (op != PictOpClear && pMaskPicture) {
            solid = pMaskPicture->pSourcePict->solidFill.color;

            if (pMaskPicture->format == PICT_r5g6b5 ||
                pMaskPicture->format == PICT_b5g6r5)
                solid = TegraPixelRGB565to888(solid);

            if (!pMaskPicture->componentAlpha)
                solid |= solid >> 24 | solid >> 16 | solid >> 8;

            if (TegraCompositeFormatSwapRedBlue3D(pDstPicture->format) !=
                TegraCompositeFormatSwapRedBlue3D(pMaskPicture->format))
                solid = TegraSwapRedBlue(solid);
        } else {
            solid = 0xffffffff;
        }

        TegraGR3D_UploadConstFP(cmds, 2, FX10x2(BLUE(solid), GREEN(solid)));
        TegraGR3D_UploadConstFP(cmds, 3, FX10x2(RED(solid), ALPHA(solid)));
    }

    TegraGR3D_UploadConstFP(cmds, 8, FX10x2(dst_alpha, clamp_src));
    TegraGR3D_UploadConstVP(cmds, 0, 0.0f, 0.0f, 0.0f, 1.0f);

    if (cmds->status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_stream_cleanup(cmds);
        return FALSE;
    }

    return TRUE;
}

static void TegraEXAComposite3D(PixmapPtr pDst,
                                int srcX, int srcY,
                                int maskX, int maskY,
                                int dstX, int dstY,
                                int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    float dst_left, dst_right, dst_top, dst_bottom;
    float src_left, src_right, src_top, src_bottom;
    float mask_left, mask_right, mask_top, mask_bottom;
    bool push_mask = !!tegra->scratch.pMask;
    bool push_src = !!tegra->scratch.pSrc;

    /* do not proceed if previous reallocation failed */
    if (tegra->scratch.attribs_alloc_err)
        return;

    /* if attributes buffer is full, "flush" it and allocate new buffer */
    if (TegraCompositeAttribBufferIsFull(&tegra->scratch))
        TegraCompositeFlush(TegraPTR(pScrn)->drm, tegra);

    /* do not proceed if current reallocation failed */
    if (tegra->scratch.attribs_alloc_err)
        return;

    if (push_src) {
        src_left   = (float) srcX   / tegra->scratch.pSrc->drawable.width;
        src_right  = (float) width  / tegra->scratch.pSrc->drawable.width + src_left;
        src_bottom = (float) srcY   / tegra->scratch.pSrc->drawable.height;
        src_top    = (float) height / tegra->scratch.pSrc->drawable.height + src_bottom;
    }

    if (push_mask) {
        mask_left   = (float) maskX  / tegra->scratch.pMask->drawable.width;
        mask_right  = (float) width  / tegra->scratch.pMask->drawable.width + mask_left;
        mask_bottom = (float) maskY  / tegra->scratch.pMask->drawable.height;
        mask_top    = (float) height / tegra->scratch.pMask->drawable.height + mask_bottom;
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
}

static void TegraEXADoneComposite3D(PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    struct tegra_fence *fence = NULL;
    TegraPixmapPtr priv;

    TegraEXACompositeDraw(tegra);

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

        tegra_stream_end(&tegra->cmds);
        fence = tegra_stream_submit(&tegra->cmds, false);

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
        tegra_stream_cleanup(&tegra->cmds);
    }

    /* buffer reallocation could fail, cleanup it now */
    if (tegra->scratch.attribs_alloc_err) {
        tegra_stream_wait_fence(fence);
        TegraCompositeReleaseAttribBuffers(&tegra->scratch);
        tegra->scratch.attribs_alloc_err = FALSE;
    }

    TegraEXACoolPixmap(tegra->scratch.pSrc, FALSE);
    TegraEXACoolPixmap(tegra->scratch.pMask, FALSE);
    TegraEXACoolPixmap(pDst, TRUE);
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
