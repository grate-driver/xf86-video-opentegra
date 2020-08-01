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

static bool tegra_exa_allocate_attributes_buffer(struct tegra_3d_state *state,
                                                 struct tegra_exa *exa)
{
    struct tegra_exa_scratch *scratch = state->scratch;
    unsigned long flags;
    int drm_ver;
    int err;

    if (scratch->attribs.bo)
        return true;

    drm_ver = drm_tegra_version(scratch->drm);
    flags = exa->default_drm_bo_flags;

    if (drm_ver >= GRATE_KERNEL_DRM_VERSION && exa->has_iommu)
        flags |= DRM_TEGRA_GEM_CREATE_SPARSE;

    err = drm_tegra_bo_new(&scratch->attribs.bo, scratch->drm, flags,
                           TEGRA_ATTRIB_BUFFER_SIZE);
    if (err) {
        scratch->attribs.bo = NULL;
        return false;
    }

    err = drm_tegra_bo_map(scratch->attribs.bo, (void**)&scratch->attribs.map);
    if (err) {
        drm_tegra_bo_unref(scratch->attribs.bo);
        scratch->attribs.map = NULL;
        scratch->attribs.bo = NULL;
        return false;
    }

    return true;
}

static void tegra_exa_release_attributes_buffer(struct tegra_3d_state *state)
{
    struct tegra_exa_scratch *scratch = state->scratch;

    drm_tegra_bo_unref(scratch->attribs.bo);
    scratch->attribs.map = NULL;
    scratch->attribs.bo = NULL;
    scratch->attrib_offset = 0;
    scratch->attrib_itr = 0;
    scratch->vtx_cnt = 0;
}

static void tegra_exa_3d_state_reset(struct tegra_3d_state *state)
{
    struct tegra_exa *exa = state->exa;

    tegra_exa_enter_optimization_3d_state(exa);

    if (state->cmds)
        tegra_stream_cleanup(state->cmds);

    if (state->scratch)
        tegra_exa_release_attributes_buffer(state);

    tegra_exa_release_optimized_3d_state(state);

    memset(state, 0, sizeof(*state));
    state->clean = true;

    tegra_exa_exit_optimization_3d_state(exa);
}

static const struct shader_program *
tegra_exa_reselect_program(struct tegra_3d_state *state)
{
    if (state->new.op >= TEGRA_ARRAY_SIZE(composite_cfgs))
        return NULL;

    tegra_exa_optimize_texture_sampler(&state->new.src);
    tegra_exa_optimize_texture_sampler(&state->new.mask);

    return tegra_exa_select_optimized_gr3d_program(state, true);
}

static void tegra_exa_finalize_3d_state(struct tegra_3d_state *state)
{
    struct tegra_exa_scratch *scratch = state->scratch;
    struct tegra_stream *cmds = state->cmds;
    const struct shader_program *prog;
    struct tegra_texture_state *tex;
    unsigned attrs_num, attribs_offset, attrs_id;
    bool wrap_mirrored_repeat = false;
    bool wrap_clamp_to_edge = true;
    bool vtx_mem_cache_invalidate;
    bool vtx_gpu_cache_invalidate;
    uint32_t attrs_out = 0;
    uint32_t attrs_in = 0;
    unsigned const_id;

    if (state->clean)
        return;

    prog = tegra_exa_reselect_program(state);

    if (!prog) {
        if (!state->new.optimized_out)
            ERROR_MSG("BUG: no shader selected for op %u\n", state->new.op);

        /*
         * attrib_offset is updated below, hence now attrib_offset points at
         * position where previous job ended in the attributes buffer and we
         * can use it in order to restore the iterator position if this drawing
         * operation is skipped entirely.
         */
        scratch->attrib_itr = scratch->attrib_offset / 2;
        scratch->vtx_cnt = 0;
        return;
    }

    state->new.prog = prog;

    const_id = 0;

    if (!state->inited) {
        tegra_stream_prep(cmds, 1);
        tegra_stream_push_setclass(cmds, HOST1X_CLASS_GR3D);

        tgr3d_initialize(cmds);
        tgr3d_upload_const_vp(cmds, const_id++, 0.0f, 0.0f, 0.0f, 1.0f);
        tgr3d_enable_render_targets(cmds, 1 << 1);

        state->inited = true;

        assert(state->num_jobs == 0);

        vtx_mem_cache_invalidate = true;
        vtx_gpu_cache_invalidate = true;
    } else {
        const_id++;

        assert(state->num_jobs > 0);

        /*
         * Apparently GR3D has two caches for vertices: one for fetched memory,
         * and other (smaller cache) for pre-processed vertices that hides
         * latency of memory cache while it's pre-fetching new data.
         */

        /* fetched vertex data is unmodified in memory */
        vtx_mem_cache_invalidate = false;

        /*
         * But attributes data is changed since we don't push mask attributes
         * into the buffer if mask texture is unused, thus the GPU cache
         * containing pre-processed vertex data needs to be invalidated.
         *
         * TODO: skip this invalidation whenever possible.
         */
        vtx_gpu_cache_invalidate = true;
    }

    tgr3d_set_draw_params(cmds, TGR3D_PRIMITIVE_TYPE_TRIANGLES,
                          TGR3D_INDEX_MODE_NONE, 0,
                          vtx_mem_cache_invalidate,
                          vtx_gpu_cache_invalidate);

    attrs_id = 0;
    attrs_in |= 1 << attrs_id;
    attrs_out |= 1 << attrs_id;

    if (scratch->src) {
        attrs_id += 1;
        attrs_in |= 1 << attrs_id;
        attrs_out |= 1 << 1;
    }

    if (scratch->mask) {
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
    tgr3d_set_vp_attributes_inout_mask(cmds, attrs_in, attrs_out);

    attrs_num = 1 + !!scratch->src + !!scratch->mask;
    attribs_offset = scratch->attrib_offset;
    attrs_id = 0;

    DEBUG_MSG("attribs_offset %u\n", attribs_offset);

    tgr3d_set_vp_attrib_buf(cmds, attrs_id, scratch->attribs.bo,
                            attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                            2, 4 * attrs_num, false);

    if (scratch->src) {
        attribs_offset += 4;
        attrs_id += 1;

        tgr3d_set_vp_attrib_buf(cmds, attrs_id, scratch->attribs.bo,
                                attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                2, 4 * attrs_num, false);
    }

    if (scratch->mask) {
        attribs_offset += 4;
        attrs_id += 1;

        tgr3d_set_vp_attrib_buf(cmds, attrs_id, scratch->attribs.bo,
                                attribs_offset, TGR3D_ATTRIB_TYPE_FLOAT16,
                                2, 4 * attrs_num, false);
    }

    scratch->attrib_offset = scratch->attrib_itr * 2;

    tex = &state->new.src;

    if (tex->pix) {
        tegra_exa_flush_deferred_2d_operations(tex->pix, true, false, true);

        if (tex->pix != state->cur.src.pix ||
            tex->format != state->cur.src.format ||
            tex->tex_sel != state->cur.src.tex_sel ||
            tex->bilinear != state->cur.src.bilinear ||
            tegra_exa_3d_state_tex_cache_needs_flush(state, tex->pix))
        {
            switch (tex->tex_sel) {
            case TEX_CLIPPED:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = true;
                break;
            case TEX_PAD:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = true;
                break;
            case TEX_NORMAL:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = false;
                break;
            case TEX_MIRROR:
                wrap_mirrored_repeat = true;
                wrap_clamp_to_edge = false;
                break;
            default:
                ERROR_MSG("BUG: tex->tex_sel %u\n", tex->tex_sel);
                break;
            }

            tgr3d_set_texture_desc(cmds, 0,
                                   tegra_exa_pixmap_bo(tex->pix),
                                   tegra_exa_pixmap_offset(tex->pix),
                                   tex->pix->drawable.width,
                                   tex->pix->drawable.height,
                                   tex->format,
                                   tex->bilinear, false, tex->bilinear,
                                   wrap_clamp_to_edge,
                                   wrap_mirrored_repeat,
                                   tegra_exa_pixmap_is_from_pool(tex->pix));

            tegra_exa_pixmap_3d_state_tex_cache_flushed(state);
        }

        /*
         * A special case of blend_src to optimize shader a tad, maybe will
         * apply similar thing to other shaders as well later on.
         */
        if (prog == &prog_blend_src_clipped_src_solid_mask ||
            prog == &prog_blend_src_solid_mask)
        {
            if (state->new.dst.alpha && tex->alpha)
                tgr3d_upload_const_fp(cmds, 5, FX10x2(0, 0));

            if (state->new.dst.alpha && !tex->alpha) {
                tgr3d_upload_const_fp(cmds, 5, FX10x2((state->new.mask.solid >> 24) / 255.0f, 0));
                state->new.mask.solid &= 0x00fffffff;
            }

            if (!state->new.dst.alpha)
                tgr3d_upload_const_fp(cmds, 5, FX10x2(-1, 0));
        } else {
            tgr3d_upload_const_fp(cmds, 5, FX10x2(tex->alpha, 0));
        }

        if (tex->transform_coords) {
            tgr3d_upload_const_vp(cmds, const_id++,
                                  pixman_fixed_to_double(scratch->transform_src.matrix[0][0]),
                                  pixman_fixed_to_double(scratch->transform_src.matrix[0][1]),
                                  pixman_fixed_to_double(scratch->transform_src.matrix[0][2]),
                                  tex->pix->drawable.width * pixman_fixed_to_double(scratch->transform_src.matrix[2][2]));

            tgr3d_upload_const_vp(cmds, const_id++,
                                  pixman_fixed_to_double(scratch->transform_src.matrix[1][0]),
                                  pixman_fixed_to_double(scratch->transform_src.matrix[1][1]),
                                  pixman_fixed_to_double(scratch->transform_src.matrix[1][2]),
                                  tex->pix->drawable.height * pixman_fixed_to_double(scratch->transform_src.matrix[2][2]));
        } else {
            tgr3d_upload_const_vp(cmds, const_id++, 1.0f, 0.0f, 0.0f, tex->pix->drawable.width);
            tgr3d_upload_const_vp(cmds, const_id++, 0.0f, 1.0f, 0.0f, tex->pix->drawable.height);
        }
    } else {
        tgr3d_upload_const_fp(cmds, 0, FX10x2(BLUE(tex->solid), GREEN(tex->solid)));
        tgr3d_upload_const_fp(cmds, 1, FX10x2(RED(tex->solid), ALPHA(tex->solid)));
    }

    tex = &state->new.mask;

    if (tex->pix) {
        tegra_exa_flush_deferred_2d_operations(tex->pix, true, false, true);

        if (tex->pix != state->cur.mask.pix ||
            tex->format != state->cur.mask.format ||
            tex->tex_sel != state->cur.mask.tex_sel ||
            tex->bilinear != state->cur.mask.bilinear||
            tegra_exa_3d_state_tex_cache_needs_flush(state, tex->pix))
        {
            switch (tex->tex_sel) {
            case TEX_CLIPPED:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = true;
                break;
            case TEX_PAD:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = true;
                break;
            case TEX_NORMAL:
                wrap_mirrored_repeat = false;
                wrap_clamp_to_edge = false;
                break;
            case TEX_MIRROR:
                wrap_mirrored_repeat = true;
                wrap_clamp_to_edge = false;
                break;
            default:
                ERROR_MSG("BUG: tex->tex_sel %u\n", tex->tex_sel);
                break;
            }

            tgr3d_set_texture_desc(cmds, 1,
                                   tegra_exa_pixmap_bo(tex->pix),
                                   tegra_exa_pixmap_offset(tex->pix),
                                   tex->pix->drawable.width,
                                   tex->pix->drawable.height,
                                   tex->format,
                                   tex->bilinear, false, tex->bilinear,
                                   wrap_clamp_to_edge,
                                   wrap_mirrored_repeat,
                                   tegra_exa_pixmap_is_from_pool(tex->pix));

            tegra_exa_pixmap_3d_state_tex_cache_flushed(state);
        }

        tgr3d_upload_const_fp(cmds, 6, FX10x2(tex->component_alpha, tex->alpha));
        tgr3d_upload_const_fp(cmds, 7, FX10x2(0, tex->tex_sel == TEX_CLIPPED));

        if (tex->transform_coords) {
            tgr3d_upload_const_vp(cmds, const_id++,
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[0][0]),
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[0][1]),
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[0][2]),
                                  tex->pix->drawable.width * pixman_fixed_to_double(scratch->transform_mask.matrix[2][2]));

            tgr3d_upload_const_vp(cmds, const_id++,
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[1][0]),
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[1][1]),
                                  pixman_fixed_to_double(scratch->transform_mask.matrix[1][2]),
                                  tex->pix->drawable.height * pixman_fixed_to_double(scratch->transform_mask.matrix[2][2]));
        } else {
            tgr3d_upload_const_vp(cmds, const_id++, 1.0f, 0.0f, 0.0f, tex->pix->drawable.width);
            tgr3d_upload_const_vp(cmds, const_id++, 0.0f, 1.0f, 0.0f, tex->pix->drawable.height);
        }
    } else {
        tgr3d_upload_const_fp(cmds, 2, FX10x2(BLUE(tex->solid), GREEN(tex->solid)));
        tgr3d_upload_const_fp(cmds, 3, FX10x2(RED(tex->solid), ALPHA(tex->solid)));
    }

    tex = &state->new.dst;

    tgr3d_upload_const_fp(cmds, 8, FX10x2(tex->alpha, state->new.src.tex_sel == TEX_CLIPPED));

    if (tex->pix != state->cur.dst.pix) {
        tgr3d_set_scissor(cmds, 0, 0,
                          tex->pix->drawable.width,
                          tex->pix->drawable.height);

        tgr3d_set_viewport_bias_scale(cmds, 0.0f, 0.0f, 0.5f,
                                      tex->pix->drawable.width,
                                      tex->pix->drawable.height,
                                      0.5f);

        tgr3d_set_render_target(cmds, 1,
                                tegra_exa_pixmap_bo(tex->pix),
                                tegra_exa_pixmap_offset(tex->pix),
                                tex->format, exaGetPixmapPitch(tex->pix),
                                tegra_exa_pixmap_is_from_pool(tex->pix));

        tegra_exa_flush_deferred_2d_operations(tex->pix, true, true, true);
    }

    if (state->new.prog != state->cur.prog)
        tgr3d_upload_program(cmds, state->new.prog);

    tgr3d_draw_primitives(cmds, 0, scratch->vtx_cnt);

    scratch->vtx_cnt = 0;
    state->cur = state->new;

    tegra_exa_optimize_alpha_component(&state->new);
}

static bool tegra_exa_3d_state_append(struct tegra_3d_state *state,
                                      struct tegra_exa *tegra,
                                      struct tegra_3d_draw_state *draw_state)
{
    struct tegra_exa_scratch *scratch = &tegra->scratch;
    struct tegra_stream *cmds = tegra->cmds;
    bool begin = state->clean;
    struct tegra_pixmap *priv;
    int err;

    state->new = *draw_state;
    state->scratch = scratch;
    state->clean = false;
    state->cmds = cmds;
    state->exa = tegra;

    if (state->new.src.pix) {
        tegra_exa_thaw_pixmap2(state->new.src.pix, THAW_ACCEL, THAW_ALLOC);

        priv = exaGetPixmapDriverPrivate(state->new.src.pix);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FALLBACK_MSG("unaccelerateable src pixmap %d:%d:%d\n",
                         state->new.src.pix->drawable.width,
                         state->new.src.pix->drawable.height,
                         state->new.src.pix->drawable.bitsPerPixel);

            if (!tegra_exa_3d_state_deferred(state))
                tegra_exa_3d_state_reset(state);

            return false;
        }
    }

    if (state->new.mask.pix) {
        tegra_exa_thaw_pixmap2(state->new.mask.pix, THAW_ACCEL, THAW_ALLOC);

        priv = exaGetPixmapDriverPrivate(state->new.mask.pix);
        if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
            FALLBACK_MSG("unaccelerateable mask pixmap %d:%d:%d\n",
                         state->new.mask.pix->drawable.width,
                         state->new.mask.pix->drawable.height,
                         state->new.mask.pix->drawable.bitsPerPixel);

            if (!tegra_exa_3d_state_deferred(state))
                tegra_exa_3d_state_reset(state);

            return false;
        }
    }

    tegra_exa_thaw_pixmap2(state->new.dst.pix, THAW_ACCEL, THAW_ALLOC);

    priv = exaGetPixmapDriverPrivate(state->new.dst.pix);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK) {
        FALLBACK_MSG("unaccelerateable dst pixmap %d:%d:%d\n",
                     state->new.dst.pix->drawable.width,
                     state->new.dst.pix->drawable.height,
                     state->new.dst.pix->drawable.bitsPerPixel);

        if (!tegra_exa_3d_state_deferred(state))
            tegra_exa_3d_state_reset(state);

        return false;
    }

    if (begin) {
        err = tegra_stream_begin(cmds, tegra->gr3d);
        if (err) {
            tegra_exa_3d_state_reset(state);
            return false;
        }
    }

    if (cmds->status != TEGRADRM_STREAM_CONSTRUCT) {
        tegra_exa_3d_state_reset(state);
        return false;
    }

    if (!tegra_exa_allocate_attributes_buffer(state, tegra)) {
        tegra_exa_3d_state_reset(state);
        return false;
    }

    /*
     * The optimized program is re-selected in append and finalize phases.
     * It is possible to skip tegra_exa_composite_3d() if drawing is already
     * known to be optimized-out.
     *
     * tegra_exa_select_optimized_gr3d_program() sets the
     * state->new.optimized_out flag and tegra_exa_composite_3d() checks it.
     */
    tegra_exa_select_optimized_gr3d_program(state, false);

    return true;
}

static struct tegra_fence *
tegra_exa_submit_3d_state(struct tegra_3d_state *state)
{
    struct tegra_fence *explicit_fence;
    struct tegra_fence *fence = NULL;
    struct tegra_exa *tegra;
    ScrnInfoPtr pScrn;
    PROFILE_DEF(gr3d);

    if (state->clean)
        return fence;

    tegra_exa_finalize_3d_state(state);

    if (state->new.optimized_out) {
        if (tegra_exa_3d_state_deferred(state))
            return fence;

        goto reset_state;
    }

    /* optimized submission path combines multiple 3d jobs into a single job */
    fence = tegra_exa_optimize_3d_submission(state);
    if (fence)
        return fence;

    /*
     * tegra_exa_optimize_3d_submission() returns NULL if PROFILE_GPU is set
     * and then job may be flushed, and thus, state was reset and it's clean
     * now.
     */
    if (state->clean)
        return NULL;

    pScrn = xf86ScreenToScrn(state->new.dst.pix->drawable.pScreen);
    tegra = TegraPTR(pScrn)->exa;

    tegra->stats.num_3d_jobs_bytes += tegra_stream_pushbuf_size(state->cmds);

    /*
     * We can't batch up draw calls using upstream host1x driver because it
     * doesn't support explicit CDMA synchronization.
     */
    tegra_stream_end(state->cmds);

    explicit_fence = tegra_exa_get_explicit_fence(TEGRA_2D,
                                                  state->new.dst.pix, 2,
                                                  state->new.src.pix,
                                                  state->new.mask.pix);

    PROFILE_START(gr3d);
    fence = tegra_exa_stream_submit(tegra, TEGRA_3D, explicit_fence);
    PROFILE_STOP(gr3d);

    TEGRA_FENCE_PUT(explicit_fence);

    tegra->stats.num_3d_jobs++;

reset_state:
    tegra_exa_3d_state_reset(state);

    return fence;
}

/* vim: set et sts=4 sw=4 ts=4: */
