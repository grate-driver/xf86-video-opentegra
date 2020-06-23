/*
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

#include "gr3d.c"

static Bool TegraAllocateAttribBuffer(TegraGR3DStatePtr state,
                                      TegraEXAPtr exa)
{
    struct tegra_exa_scratch *scratch = state->scratch;
    unsigned long flags;
    int err;

    if (scratch->attribs.bo)
        return TRUE;

    flags = exa->default_drm_bo_flags | DRM_TEGRA_GEM_CREATE_SPARSE;
    err = drm_tegra_bo_new(&scratch->attribs.bo, scratch->drm, flags,
                           TEGRA_ATTRIB_BUFFER_SIZE);
    if (err) {
        scratch->attribs.bo = NULL;
        return FALSE;
    }

    err = drm_tegra_bo_map(scratch->attribs.bo,
                           (void**)&scratch->attribs.map);
    if (err) {
        drm_tegra_bo_unref(scratch->attribs.bo);
        scratch->attribs.map = NULL;
        scratch->attribs.bo = NULL;
        return FALSE;
    }

    return TRUE;
}

static void TegraReleaseAttribBuffers(TegraGR3DStatePtr state)
{
    struct tegra_exa_scratch *scratch = state->scratch;

    drm_tegra_bo_unref(scratch->attribs.bo);
    scratch->attribs.map = NULL;
    scratch->attribs.bo = NULL;
    scratch->attrib_itr = 0;
    scratch->vtx_cnt = 0;
}

static void TegraGR3DStateReset(TegraGR3DStatePtr state)
{
    if (state->cmds)
        tegra_stream_cleanup(state->cmds);

    if (state->scratch)
        TegraReleaseAttribBuffers(state);

    memset(state, 0, sizeof(*state));
    state->clean = TRUE;
}

static const struct shader_program *
TegraGR3DStateSelectProgram(TegraGR3DStatePtr state)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[state->new.op];
    const struct shader_program *prog;
    unsigned mask_sel;
    unsigned src_sel;

    if (state->new.op >= TEGRA_ARRAY_SIZE(composite_cfgs))
        return NULL;

    /* optimize wrap-mode if possible */
    if (!state->new.src.coords_wrap && state->new.src.pPix)
        state->new.src.tex_sel = TEX_PAD;

    src_sel = state->new.src.tex_sel;

    /* optimize wrap-mode if possible */
    if (!state->new.mask.coords_wrap && state->new.mask.pPix)
        state->new.mask.tex_sel = TEX_PAD;

    mask_sel = state->new.mask.tex_sel;

    /* pow2 texture can use more optimized shaders */
    if (state->new.src.pow2 &&
            (src_sel == TEX_NORMAL || src_sel == TEX_MIRROR))
        src_sel = TEX_PAD;

    /* pow2 texture can use more optimized shaders */
    if (state->new.mask.pow2 &&
            (mask_sel == TEX_NORMAL || mask_sel == TEX_MIRROR))
        mask_sel = TEX_PAD;

    /*
     * Currently all shaders are handling texture transparency and
     * coordinates warp-modes in the assembly, this adds a lot of
     * instructions to the shaders and in result they are quite slow.
     * Ideally we need a proper compiler to build all variants of the
     * custom shaders, but we don't have that luxury at the moment.
     *
     * As a temporary workaround we prepared custom shaders for a
     * couple of most popular texture-operation combinations.
     */
    if (state->new.op == PictOpOver) {
        if (src_sel == TEX_PAD && !state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_opaque_pad_src_solid_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_NORMAL && state->new.src.alpha &&
            mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
                prog = &prog_blend_over_alpha_normal_src_empty_mask_dst_opaque;
                state->new.src.tex_sel = TEX_PAD;
                goto custom_shader;
        }

        if (src_sel == TEX_NORMAL && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_normal_src_solid_mask;
                state->new.src.tex_sel = TEX_PAD;
                goto custom_shader;
        }

        if (src_sel == TEX_PAD && state->new.src.alpha &&
            mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
                prog = &prog_blend_over_alpha_pad_src_empty_mask_dst_opaque;
                goto custom_shader;
        }

        if (src_sel == TEX_PAD && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_pad_src_solid_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_clipped_src_solid_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_opaque_clipped_src_solid_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
            state->new.dst.alpha) {
                prog = &prog_blend_over_solid_src_empty_mask_dst_alpha;
                goto custom_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
            !state->new.dst.alpha) {
                prog = &prog_blend_over_solid_src_empty_mask_dst_opaque;
                goto custom_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
            state->new.mask.component_alpha) {
                if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
                    prog = &prog_blend_over_solid_black_src_clipped_mask_dst_opaque;
                    goto custom_shader;
                }

                prog = &prog_blend_over_solid_src_clipped_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_PAD &&
            state->new.mask.component_alpha) {
                if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
                    prog = &prog_blend_over_solid_black_src_pad_mask_dst_opaque;
                    goto custom_shader;
                }

                prog = &prog_blend_over_solid_src_pad_mask;
                goto custom_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
            state->new.src.solid == 0xff000000 && !state->new.dst.alpha &&
            state->new.mask.component_alpha) {
                prog = &prog_blend_over_solid_black_src_clipped_aaaa_mask_dst_opaque;
                goto custom_shader;
        }
    }

    if (state->new.op == PictOpSrc) {
        if (src_sel == TEX_CLIPPED &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_src_clipped_src_solid_mask;
                goto custom_shader;
        }

        if (mask_sel == TEX_CLIPPED &&
            (src_sel == TEX_SOLID || src_sel == TEX_EMPTY)) {
                prog = &prog_blend_src_solid_src_clipped_mask;
                goto custom_shader;
        }
    }

    prog = cfg->prog[PROG_SEL(src_sel, mask_sel)];
    if (!prog) {
        FallbackMsg("no shader for operation %d src_sel %u mask_sel %u\n",
                    state->new.op, src_sel, mask_sel);
        return NULL;
    }

    AccelMsg("got shader for operation %d src_sel %u mask_sel %u %s\n",
             state->new.op, src_sel, mask_sel, prog->name);

    return prog;

custom_shader:
    AccelMsg("custom shader for operation %d src_sel %u mask_sel %u %s\n",
             state->new.op, src_sel, mask_sel, prog->name);

    return prog;
}

static void TegraGR3DStateFinalize(TegraGR3DStatePtr state)
{
    struct tegra_exa_scratch *scratch = state->scratch;
    struct tegra_stream *cmds = state->cmds;
    unsigned attrs_num, attribs_offset, attrs_id;
    unsigned const_id;
    const struct shader_program *prog;
    Bool wrap_mirrored_repeat = FALSE;
    Bool wrap_clamp_to_edge = TRUE;
    TegraGR3DStateTexPtr tex;
    uint32_t attrs_out = 0;
    uint32_t attrs_in = 0;

    if (state->clean)
        return;

    prog = TegraGR3DStateSelectProgram(state);
    if (!prog) {
        ErrorMsg("BUG: no shader selected for op %u\n", state->new.op);
        return;
    }

    const_id = 0;

    if (!state->inited) {
        tegra_stream_prep(cmds, 1);
        tegra_stream_push_setclass(cmds, HOST1X_CLASS_GR3D);

        TegraGR3D_Initialize(cmds);

        TegraGR3D_UploadConstVP(cmds, const_id++, 0.0f, 0.0f, 0.0f, 1.0f);

        TegraGR3D_SetupDrawParams(cmds, TGR3D_PRIMITIVE_TYPE_TRIANGLES,
                                  TGR3D_INDEX_MODE_NONE, 0);

        state->inited = TRUE;
    }

    attrs_id = 0;
    attrs_in |= 1 << attrs_id;
    attrs_out |= 1 << attrs_id;

    if (scratch->pSrc) {
        attrs_id += 1;
        attrs_in |= 1 << attrs_id;
        attrs_out |= 1 << 1;
    }

    if (scratch->pMask) {
        attrs_id += 1;
        attrs_in |= 1 << attrs_id;
        attrs_out |= 1 << 1;
    }

    /*
     * Set up actual in/out attributes masks since we are using common
     * vertex and linker programs and the common definition enables all
     * 3 attributes, while only 2 may be actually active (if mask or src
     * textures are absent). Note that that we're setting up the masks
     * before the descriptors because Tegra's HW like to start data-fetching
     * on writing to address registers, so better to set up the masks now
     * to be on a safe side.
     */
    TegraGR3D_SetupVpAttributesInOutMask(cmds, attrs_in, attrs_out);

    attrs_num = 1 + !!scratch->pSrc + !!scratch->pMask;
    attribs_offset = 0;
    attrs_id = 0;

    TegraGR3D_SetupAttribute(cmds, attrs_id, scratch->attribs.bo,
                             attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                             2, 4 * attrs_num);

    if (scratch->pSrc) {
        attribs_offset += 4;
        attrs_id += 1;

        TegraGR3D_SetupAttribute(cmds, attrs_id, scratch->attribs.bo,
                                 attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                 2, 4 * attrs_num);
    }

    if (scratch->pMask) {
        attribs_offset += 4;
        attrs_id += 1;

        TegraGR3D_SetupAttribute(cmds, attrs_id, scratch->attribs.bo,
                                 attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                 2, 4 * attrs_num);
    }

    tex = &state->new.src;

    if (tex->pPix) {
        if (tex->pPix != state->cur.src.pPix ||
            tex->format != state->cur.src.format ||
            tex->tex_sel != state->cur.src.tex_sel ||
            tex->bilinear != state->cur.src.bilinear)
        {
            switch (tex->tex_sel) {
            case TEX_CLIPPED:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = TRUE;
                break;
            case TEX_PAD:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = TRUE;
                break;
            case TEX_NORMAL:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = FALSE;
                break;
            case TEX_MIRROR:
                wrap_mirrored_repeat = TRUE;
                wrap_clamp_to_edge = FALSE;
                break;
            default:
                ErrorMsg("BUG: tex->tex_sel %u\n", tex->tex_sel);
                break;
            }

            TegraGR3D_SetupTextureDesc(cmds, 0,
                                       TegraEXAPixmapBO(tex->pPix),
                                       TegraEXAPixmapOffset(tex->pPix),
                                       tex->pPix->drawable.width,
                                       tex->pPix->drawable.height,
                                       tex->format,
                                       tex->bilinear, false, tex->bilinear,
                                       wrap_clamp_to_edge,
                                       wrap_mirrored_repeat);
        }

        /*
         * A special case of blend_src to optimize shader a tad, maybe will
         * apply similar thing to other shaders as well later on.
         */
        if (prog == &prog_blend_src_clipped_src_solid_mask ||
                prog == &prog_blend_src_solid_mask)
        {
            if (state->new.dst.alpha && tex->alpha)
                TegraGR3D_UploadConstFP(cmds, 5, FX10x2(0, 0));

            if (state->new.dst.alpha && !tex->alpha) {
                TegraGR3D_UploadConstFP(cmds, 5,
                                        FX10x2((state->new.mask.solid >> 24) / 255.0f,
                                               0));
                state->new.mask.solid &= 0x00fffffff;
            }

            if (!state->new.dst.alpha)
                TegraGR3D_UploadConstFP(cmds, 5, FX10x2(-1, 0));
        } else {
            TegraGR3D_UploadConstFP(cmds, 5, FX10x2(tex->alpha, 0));
        }

        if (tex->transform_coords) {
            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    pixman_fixed_to_double(scratch->transform_src.matrix[0][0]),
                                    pixman_fixed_to_double(scratch->transform_src.matrix[0][1]),
                                    pixman_fixed_to_double(scratch->transform_src.matrix[0][2]),
                                    tex->pPix->drawable.width * pixman_fixed_to_double(scratch->transform_src.matrix[2][2]));

            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    pixman_fixed_to_double(scratch->transform_src.matrix[1][0]),
                                    pixman_fixed_to_double(scratch->transform_src.matrix[1][1]),
                                    pixman_fixed_to_double(scratch->transform_src.matrix[1][2]),
                                    tex->pPix->drawable.height * pixman_fixed_to_double(scratch->transform_src.matrix[2][2]));
        } else {
            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    1.0f, 0.0f, 0.0f, tex->pPix->drawable.width);

            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    0.0f, 1.0f, 0.0f, tex->pPix->drawable.height);
        }
    } else {
        TegraGR3D_UploadConstFP(cmds, 0, FX10x2(BLUE(tex->solid), GREEN(tex->solid)));
        TegraGR3D_UploadConstFP(cmds, 1, FX10x2(RED(tex->solid), ALPHA(tex->solid)));
    }

    tex = &state->new.mask;

    if (tex->pPix) {
        if (tex->pPix != state->cur.mask.pPix ||
            tex->format != state->cur.mask.format ||
            tex->tex_sel != state->cur.mask.tex_sel ||
            tex->bilinear != state->cur.mask.bilinear)
        {
            switch (tex->tex_sel) {
            case TEX_CLIPPED:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = TRUE;
                break;
            case TEX_PAD:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = TRUE;
                break;
            case TEX_NORMAL:
                wrap_mirrored_repeat = FALSE;
                wrap_clamp_to_edge = FALSE;
                break;
            case TEX_MIRROR:
                wrap_mirrored_repeat = TRUE;
                wrap_clamp_to_edge = FALSE;
                break;
            default:
                ErrorMsg("BUG: tex->tex_sel %u\n", tex->tex_sel);
                break;
            }

            TegraGR3D_SetupTextureDesc(cmds, 1,
                                       TegraEXAPixmapBO(tex->pPix),
                                       TegraEXAPixmapOffset(tex->pPix),
                                       tex->pPix->drawable.width,
                                       tex->pPix->drawable.height,
                                       tex->format,
                                       tex->bilinear, false, tex->bilinear,
                                       wrap_clamp_to_edge,
                                       wrap_mirrored_repeat);
        }

        TegraGR3D_UploadConstFP(cmds, 6, FX10x2(tex->component_alpha,
                                                tex->alpha));
        TegraGR3D_UploadConstFP(cmds, 7, FX10x2(0, tex->tex_sel == TEX_CLIPPED));

        if (tex->transform_coords) {
            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[0][0]),
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[0][1]),
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[0][2]),
                                    tex->pPix->drawable.width * pixman_fixed_to_double(scratch->transform_mask.matrix[2][2]));

            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[1][0]),
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[1][1]),
                                    pixman_fixed_to_double(scratch->transform_mask.matrix[1][2]),
                                    tex->pPix->drawable.height * pixman_fixed_to_double(scratch->transform_mask.matrix[2][2]));
        } else {
            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    1.0f, 0.0f, 0.0f, tex->pPix->drawable.width);

            TegraGR3D_UploadConstVP(cmds, const_id++,
                                    0.0f, 1.0f, 0.0f, tex->pPix->drawable.height);
        }
    } else {
        TegraGR3D_UploadConstFP(cmds, 2, FX10x2(BLUE(tex->solid),
                                                GREEN(tex->solid)));
        TegraGR3D_UploadConstFP(cmds, 3, FX10x2(RED(tex->solid),
                                                ALPHA(tex->solid)));
    }

    tex = &state->new.dst;

    TegraGR3D_UploadConstFP(cmds, 8,
                            FX10x2(tex->alpha,
                                   state->new.src.tex_sel == TEX_CLIPPED));

    TegraGR3D_SetupScissor(cmds, 0, 0,
                           tex->pPix->drawable.width,
                           tex->pPix->drawable.height);

    TegraGR3D_SetupViewportBiasScale(cmds, 0.0f, 0.0f, 0.5f,
                                     tex->pPix->drawable.width,
                                     tex->pPix->drawable.height,
                                     0.5f);

    TegraGR3D_SetupRenderTarget(cmds, 1,
                                TegraEXAPixmapBO(tex->pPix),
                                TegraEXAPixmapOffset(tex->pPix),
                                tex->format, exaGetPixmapPitch(tex->pPix));

    TegraGR3D_EnableRenderTargets(cmds, 1 << 1);

    TegraGR3D_UploadProgram(cmds, prog);

    TegraGR3D_DrawPrimitives(cmds, 0, scratch->vtx_cnt);

    state->cur = state->new;
}

static Bool TegraGR3DStateChanged(TegraGR3DStatePtr state)
{
    return memcmp(&state->new, &state->cur, sizeof(state->new)) != 0;
}

static Bool TegraGR3DStateAppend(TegraGR3DStatePtr state, TegraEXAPtr tegra,
                                 TegraGR3DDrawState *draw_state)
{
    struct tegra_exa_scratch *scratch = &tegra->scratch;
    struct tegra_stream *cmds = tegra->cmds;
    TegraPixmapPtr priv;
    int err;

    if (state->clean) {
        err = tegra_stream_begin(cmds, tegra->gr3d);
        if (err)
            return FALSE;
    }

    if (cmds->status != TEGRADRM_STREAM_CONSTRUCT) {
        TegraGR3DStateReset(state);
        return FALSE;
    }

    state->new = *draw_state;
    state->scratch = scratch;
    state->clean = FALSE;
    state->cmds = cmds;

    if (state->new.src.pPix) {
        TegraEXAThawPixmap(state->new.src.pPix, TRUE);

        priv = exaGetPixmapDriverPrivate(state->new.src.pPix);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FallbackMsg("unaccelerateable src pixmap %d:%d:%d\n",
                        state->new.src.pPix->drawable.width,
                        state->new.src.pPix->drawable.height,
                        state->new.src.pPix->drawable.bitsPerPixel);
            TegraGR3DStateReset(state);
            return FALSE;
        }
    }

    if (state->new.mask.pPix) {
        TegraEXAThawPixmap(state->new.mask.pPix, TRUE);

        priv = exaGetPixmapDriverPrivate(state->new.mask.pPix);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FallbackMsg("unaccelerateable mask pixmap %d:%d:%d\n",
                        state->new.mask.pPix->drawable.width,
                        state->new.mask.pPix->drawable.height,
                        state->new.mask.pPix->drawable.bitsPerPixel);
            TegraGR3DStateReset(state);
            return FALSE;
        }
    }

    TegraEXAThawPixmap(state->new.dst.pPix, TRUE);

    priv = exaGetPixmapDriverPrivate(state->new.dst.pPix);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FallbackMsg("unaccelerateable dst pixmap %d:%d:%d\n",
                        state->new.dst.pPix->drawable.width,
                        state->new.dst.pPix->drawable.height,
                        state->new.dst.pPix->drawable.bitsPerPixel);
        TegraGR3DStateReset(state);
        return FALSE;
    }

    if (!TegraAllocateAttribBuffer(state, tegra)) {
        TegraGR3DStateReset(state);
        return FALSE;
    }

    return TRUE;
}

static struct tegra_fence * TegraGR3DStateSubmit(TegraGR3DStatePtr state)
{
    struct tegra_fence *fence = NULL;
    PROFILE_DEF(gr3d);

    if (state->clean)
        return NULL;

    if (TegraGR3DStateChanged(state))
        TegraGR3DStateFinalize(state);

    /*
     * TODO: We can't batch up draw calls until host1x driver will
     * expose controls for explicit CDMA synchronization.
     */
    tegra_stream_end(state->cmds);
#if PROFILE
    PROFILE_START(gr3d);
    tegra_stream_flush(state->cmds);
    PROFILE_STOP(gr3d);
#else
    fence = tegra_stream_submit(state->cmds, false);
#endif

    TegraGR3DStateReset(state);

    return fence;
}
