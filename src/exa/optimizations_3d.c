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

#define DISABLE_3D_OPTIMIZATIONS    false

static void
tegra_exa_optimize_texture_sampler(struct tegra_texture_state *tex)
{
   /* optimize wrap-mode if possible */
   if (tex->pix && !tex->coords_wrap)
      tex->tex_sel = TEX_PAD;
}

static const struct shader_program *
tegra_exa_select_optimized_gr3d_program(struct tegra_3d_state *state,
                                        bool update_state)
{
   struct tegra_pixmap *dst_priv = exaGetPixmapDriverPrivate(state->new.dst.pix);
   const struct tegra_composite_config *cfg;
   const struct shader_program *prog = NULL;
   unsigned mask_sel = state->new.mask.tex_sel;
   unsigned src_sel  = state->new.src.tex_sel;

   /* pow2 texture can use more optimized shaders */
   if (state->new.src.pow2 && (src_sel == TEX_NORMAL ||
                               src_sel == TEX_MIRROR))
      src_sel = TEX_PAD;

   /* pow2 texture can use more optimized shaders */
   if (state->new.mask.pow2 && (mask_sel == TEX_NORMAL ||
                                mask_sel == TEX_MIRROR))
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
      if (state->new.dst.alpha || dst_priv->state.alpha_0) {
         if (src_sel == TEX_EMPTY ||
             (mask_sel == TEX_SOLID && state->new.mask.solid == 0x0)) {
            state->new.optimized_out = 1;
            ACCEL_MSG("PictOpOver optimized out\n");
            goto optimized_shader;
         }
      } else if (mask_sel == TEX_SOLID && state->new.mask.solid == 0x0) {
         ACCEL_MSG("optimized out src texture fetch\n");
         if (update_state) {
            state->new.src.tex_sel = TEX_EMPTY;
            state->new.src.solid = 0x00000000;
            state->new.src.pix = NULL;
         }
         src_sel = TEX_EMPTY;
      }

      if ((mask_sel == TEX_EMPTY && !state->new.src.pix && (state->new.src.solid & 0xff000000) == 0xff000000) ||
          (dst_priv->state.solid_fill && dst_priv->state.solid_color == 0x0)) {
         ACCEL_MSG("PictOpOver optimized to PictOpSrc\n");
         state->new.op = PictOpSrc;
         goto op_src;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
          !state->new.dst.alpha) {
         prog = &prog_blend_over_solid_src_empty_mask_dst_opaque;

         if ((state->new.src.solid >> 24) == 0xff) {
            prog = &prog_blend_src_solid_mask_src;
            state->new.mask.solid = 0x00fffffff;
         }

         goto optimized_shader;
      }

      if (src_sel == TEX_PAD && !state->new.src.alpha &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_over_opaque_pad_src_solid_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_NORMAL && state->new.src.alpha &&
          mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
         prog = &prog_blend_over_alpha_normal_src_empty_mask_dst_opaque;
         if (update_state)
            state->new.src.tex_sel = TEX_PAD;
         goto optimized_shader;
      }

      if (src_sel == TEX_NORMAL && state->new.src.alpha &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_over_alpha_normal_src_solid_mask;
         if (update_state)
            state->new.src.tex_sel = TEX_PAD;
         goto optimized_shader;
      }

      if (src_sel == TEX_PAD && state->new.src.alpha &&
          mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
         prog = &prog_blend_over_alpha_pad_src_empty_mask_dst_opaque;
         goto optimized_shader;
      }

      if (src_sel == TEX_PAD && state->new.src.alpha &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_over_alpha_pad_src_solid_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_over_alpha_clipped_src_solid_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_over_opaque_clipped_src_solid_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
          state->new.dst.alpha) {
         prog = &prog_blend_over_solid_src_empty_mask_dst_alpha;
         goto optimized_shader;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
          !state->new.dst.alpha) {
         prog = &prog_blend_over_solid_src_empty_mask_dst_opaque;

         if ((state->new.src.solid >> 24) == 0xff) {
            prog = &prog_blend_src_solid_mask_src;
            state->new.mask.solid = 0x00fffffff;
         }

         goto optimized_shader;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
          state->new.mask.component_alpha) {
         if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
            prog = &prog_blend_over_solid_black_src_clipped_mask_dst_opaque;
            goto optimized_shader;
         }

         prog = &prog_blend_over_solid_src_clipped_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_PAD &&
          state->new.mask.component_alpha) {
         if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
            prog = &prog_blend_over_solid_black_src_pad_mask_dst_opaque;
            goto optimized_shader;
         }

         prog = &prog_blend_over_solid_src_pad_mask;
         goto optimized_shader;
      }

      if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
          state->new.src.solid == 0xff000000 && !state->new.dst.alpha &&
          state->new.mask.component_alpha) {
            prog = &prog_blend_over_solid_black_src_clipped_aaaa_mask_dst_opaque;
            goto optimized_shader;
      }
   }

   if (state->new.op == PictOpSrc) {
op_src:
        if (src_sel == TEX_EMPTY ||
            (mask_sel == TEX_SOLID && state->new.mask.solid == 0x0))
        {
            if (dst_priv->state.solid_fill && dst_priv->state.solid_color == 0x0) {
                ACCEL_MSG("PictOpSrc optimized out\n");
                state->new.optimized_out = 1;
                goto optimized_shader;
            }
        }

      if (src_sel == TEX_CLIPPED &&
          (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
         prog = &prog_blend_src_clipped_src_solid_mask;
         goto optimized_shader;
      }

      if (mask_sel == TEX_CLIPPED &&
          (src_sel == TEX_SOLID || src_sel == TEX_EMPTY)) {
         prog = &prog_blend_src_solid_src_clipped_mask;
         goto optimized_shader;
      }
   }

   cfg = &composite_cfgs[state->new.op];
   prog = cfg->prog[PROG_SEL(src_sel, mask_sel)];

   if (prog == &prog_blend_add_solid_mask &&
       state->new.dst.alpha && state->new.src.alpha) {
      prog = &prog_blend_add_solid_mask_alpha_src_dst;
      goto optimized_shader;
   }

   if (!prog) {
      FALLBACK_MSG("no shader for operation %d src_sel %u mask_sel %u\n",
                   state->new.op, src_sel, mask_sel);
      return NULL;
   }

   ACCEL_MSG("got shader for operation %d src_sel %u mask_sel %u %s\n",
             state->new.op, src_sel, mask_sel, prog->name);

   return prog;

optimized_shader:
   if (state->new.optimized_out)
      ACCEL_MSG("optimized out shader for operation %d src_sel %u mask_sel %u\n",
                state->new.op, src_sel, mask_sel);
   else
      ACCEL_MSG("custom shader for operation %d src_sel %u mask_sel %u %s\n",
                state->new.op, src_sel, mask_sel, prog->name);

   return prog;
}

static void tegra_exa_optimize_alpha_component(struct tegra_3d_draw_state *state)
{
   struct tegra_pixmap *dst_priv = exaGetPixmapDriverPrivate(state->dst.pix);
   struct tegra_pixmap *src_priv, *mask_priv;

   if (DISABLE_2D_OPTIMIZATIONS)
       return;

   if (state->dst_full_cover && !state->dst.alpha) {
        dst_priv->state.alpha_0 = 1;

        DEBUG_MSG("state->dst_full_cover && !state->dst.alpha\n");
   } else if (state->dst.alpha) {
        bool mask_alpha_0 = false;
        bool dst_alpha_0 = false;
        bool src_alpha_0 = false;

        DEBUG_MSG("state->src.pix %p state->mask.pix %p\n",
                  state->src.pix, state->mask.pix);

        if (state->mask.pix) {
            mask_priv = exaGetPixmapDriverPrivate(state->mask.pix);

            if (state->mask.alpha && mask_priv->state.alpha_0)
                mask_alpha_0 = true;
        } else {
            if ((state->mask.solid & 0xff000000) == 0x00000000)
                mask_alpha_0 = true;
        }

        if (state->src.pix) {
            src_priv = exaGetPixmapDriverPrivate(state->src.pix);

            if (state->src.alpha && src_priv->state.alpha_0)
                src_alpha_0 = true;
        } else {
            if ((state->src.solid & 0xff000000) == 0x00000000)
                src_alpha_0 = true;
        }

        /* check whether dst.alpha channel is changed by drawing operation */
        switch (state->op) {
        case PictOpSrc:
        case PictOpOver:
            if (src_alpha_0 || mask_alpha_0)
                dst_alpha_0 = true;
            break;

        default:
            break;
        }

        if (!dst_alpha_0 && dst_priv->state.alpha_0)
            DEBUG_MSG("state->dst.pix %p canceled alpha_0\n", state->dst.pix);

        if (!dst_alpha_0)
            dst_priv->state.alpha_0 = 0;

        DEBUG_MSG("src_alpha_0 %d mask_alpha_0 %d dst_alpha_0 %d state.alpha_0 %d\n",
                  src_alpha_0, mask_alpha_0, dst_alpha_0, dst_priv->state.alpha_0);
   }
}

static void
tegra_exa_debug_dump_deferred_3d_state_pixmaps(struct tegra_3d_state *state)
{
    unsigned int i;

    DEBUG_MSG("deferred pixmaps:\n");
    for (i = 0; i < state->num_pixmaps; i++)
        DEBUG_MSG("\t%u: %p base %p pixmap.refcnt %u state.refcnt %u read %d write %d cache_dirty %d\n",
                  i,
                  state->pixmaps[i].pixmap,
                  state->pixmaps[i].pixmap->base,
                  state->pixmaps[i].pixmap->refcnt,
                  state->pixmaps[i].refcnt,
                  state->pixmaps[i].read,
                  state->pixmaps[i].write,
                  state->pixmaps[i].cache_dirty);
}

static void
tegra_exa_optimized_3d_state_unref_all_pixmaps(struct tegra_3d_state *state)
{
    struct tegra_pixmap *pixmap;
    unsigned i;

    for (i = 0; i < state->num_pixmaps; i++) {
        pixmap = state->pixmaps[i].pixmap;

        assert(state->pixmaps[i].refcnt);

        while (state->pixmaps[i].refcnt--) {
            pixmap->freezer_lockcnt--;

            if (!state->pixmaps[i].refcnt && !pixmap->destroyed)
                tegra_exa_cool_pixmap(pixmap->base, false);

            /*
             * tegra_exa_unref_pixmap() may destroy pixmap and
             * tegra_exa_pixmap_release_data() checks whether pixmap is in
             * deferred 3d state, hence remove it from the list if pixmap isn't
             * referenced anymore.
             *
             */
            if (!state->pixmaps[i].refcnt)
                state->pixmaps[i].pixmap = NULL;

            tegra_exa_unref_pixmap(pixmap);
        }

        assert(state->pixmaps[i].pixmap == NULL);
        state->pixmaps[i].refcnt = 0;
    }

    state->num_pixmaps = 0;
}

static void tegra_exa_release_optimized_3d_state(struct tegra_3d_state *state)
{
    DEBUG_MSG("inited %d clean %d num_pixmaps %u num_jobs %u\n",
              state->inited, state->clean,
              state->num_pixmaps,
              state->num_jobs);

    if (state->flushtimer) {
        TimerFree(state->flushtimer);

        state->exa->pool_compaction_blockcnt--;
    }

    TEGRA_FENCE_PUT(state->explicit_fence);

    tegra_exa_optimized_3d_state_unref_all_pixmaps(state);
}

static struct tegra_fence *
tegra_exa_submit_deferred_3d_jobs(struct tegra_3d_state *state)
{
    struct tegra_exa *exa = state->exa;
    struct tegra_fence *fence;

    PROFILE_DEF(deferred_gr3d);

    DEBUG_MSG("inited %d clean %d num_pixmaps %u num_jobs %u\n",
              state->inited, state->clean,
              state->num_pixmaps,
              state->num_jobs);

    tegra_exa_debug_dump_deferred_3d_state_pixmaps(state);

    assert(state->num_jobs);
    if (!state->num_jobs)
        return NULL;

    exa->stats.num_3d_jobs_bytes += tegra_stream_pushbuf_size(state->cmds);

    tegra_stream_end(state->cmds);

    DEBUG_MSG("flushed 3d state num_jobs %u\n", state->num_jobs);

    PROFILE_START(deferred_gr3d);
    fence = tegra_exa_stream_submit(exa, TEGRA_3D, state->explicit_fence);
    PROFILE_STOP(deferred_gr3d);

    tegra_exa_3d_state_reset(state);

    exa->stats.num_3d_jobs++;

    return fence;
}

static void tegra_exa_flush_deferred_3d_state(struct tegra_3d_state *state)
{
    struct tegra_exa *exa = state->exa;

    if (!state->num_jobs)
        return;

    /*
     * Situation of a nested 3d job -> 2d flush -> 3d flush shall not
     * happen because there is only one global exa->gr3d_state for the
     * whole driver.
     */
    assert(!exa->opt_state[TEGRA_OPT_3D].wrapcnt);

    if (exa->opt_state[TEGRA_OPT_3D].wrapcnt)
        return;

    DEBUG_MSG("flushing 3d state\n");

    tegra_exa_enter_optimization_3d_state(exa);
    tegra_exa_submit_deferred_3d_jobs(state);
    tegra_exa_exit_optimization_3d_state(exa);
}

static CARD32
tegra_exa_flush_3d_state_cb(OsTimerPtr timer, CARD32 time, void *arg)
{
    struct tegra_3d_state *state = arg;

    DEBUG_MSG("flush-timer fired\n");

    tegra_exa_flush_deferred_3d_state(state);

    return 0;
}

static void tegra_exa_3d_state_ref_pixmap(struct tegra_3d_state *state,
                                          PixmapPtr pixmap, bool write)
{
    struct tegra_pixmap *priv;
    struct drm_tegra_bo *bo;
    unsigned int bo_size;
    unsigned int i;
    int err;

    if (!pixmap)
        return;

    priv = exaGetPixmapDriverPrivate(pixmap);
    priv = tegra_exa_ref_pixmap(priv);
    priv->freezer_lockcnt++;

    for (i = 0; i < state->num_pixmaps; i++) {
        if (state->pixmaps[i].pixmap == priv) {
            state->pixmaps[i].read        |= !write;
            state->pixmaps[i].write       |= write;
            state->pixmaps[i].cache_dirty |= write;
            state->pixmaps[i].refcnt++;
            return;
        }
    }

    state->pixmaps[state->num_pixmaps].cache_dirty  = true;
    state->pixmaps[state->num_pixmaps].read         = !write;
    state->pixmaps[state->num_pixmaps].write        = write;
    state->pixmaps[state->num_pixmaps].pixmap       = priv;
    state->pixmaps[state->num_pixmaps].refcnt       = 1;
    state->num_pixmaps++;

    if (priv->sparse) {
        bool duplicated_bo = false;

        bo = tegra_exa_pixmap_bo(pixmap);
        err = drm_tegra_bo_get_size(bo, &bo_size);
        assert(!err);

        for (i = 0; i < state->num_pixmaps - 1; i++) {
            if (tegra_exa_pixmap_bo(state->pixmaps[i].pixmap->base) == bo) {
                duplicated_bo = true;
                break;
            }
        }

        if (!duplicated_bo && !err && bo_size < tegra_exa_max_sparse_size())
            state->pixmaps_mmap_size += bo_size;

        DEBUG_MSG("pixmap %p bo_size %u write %d pixmaps_mmap_size %u duplicated_bo %d\n",
                  pixmap, bo_size, write, state->pixmaps_mmap_size, duplicated_bo);
    }
}

static struct tegra_fence *
tegra_exa_optimize_3d_submission(struct tegra_3d_state *state)
{
    struct tegra_exa_scratch *scratch = state->scratch;
    struct tegra_stream *stream = state->cmds;
    struct tegra_fence *explicit_fence;
    struct tegra_exa *exa = state->exa;
    unsigned int max_mmap_size;
    unsigned int num_pixmaps;
    int timer_flags = 0;
    CARD32 timeout_ms;
    int drm_ver;

    if (DISABLE_3D_OPTIMIZATIONS)
        return NULL;

    /* upstream kernel driver doesn't support cmdstream synchronization */
    drm_ver = drm_tegra_version(scratch->drm);
    if (drm_ver < GRATE_KERNEL_DRM_VERSION) {
        DEBUG_MSG("optimization unsupported\n");
        return NULL;
    }

    DEBUG_MSG("optimizing state: inited %d clean %d num_pixmaps %u num_jobs %u\n",
              state->inited, state->clean,
              state->num_pixmaps,
              state->num_jobs);

    /*
     * 2d job may request flushing of 3d state, but nested
     * "3d flush -> 2d flush -> 3d flush" shall not happen.
     */
    assert(exa->opt_state[TEGRA_OPT_3D].wrapcnt == 1);

    explicit_fence = tegra_exa_get_explicit_fence(TEGRA_2D,
                                                  state->new.dst.pix, 2,
                                                  state->new.src.pix,
                                                  state->new.mask.pix);

    /* tegra_exa_get_explicit_fence() always bumps refcount */
    if (explicit_fence == state->explicit_fence)
        TEGRA_FENCE_PUT(explicit_fence);

    /*
     * GRATE-kernel driver doesn't support emitting intermediate fences
     * for today. Whole 3D job will wait for the latest explicit 2d fence.
     */
    state->explicit_fence = tegra_exa_select_latest_fence(2,
                                                          state->explicit_fence,
                                                          explicit_fence);
    state->num_jobs++;

    num_pixmaps = state->num_pixmaps + 1;

    if (state->new.src.pix)
        num_pixmaps++;

    if (state->new.mask.pix)
        num_pixmaps++;

    max_mmap_size = 20 * 1024 * 1024;

    if (num_pixmaps > TEGRA_ARRAY_SIZE(state->pixmaps) ||
        state->pixmaps_mmap_size > max_mmap_size)
    {
        DEBUG_MSG("job is too big num_pixmaps %u (max %u) state->pixmaps_mmap_size %u (max %u)\n",
                  num_pixmaps, TEGRA_ARRAY_SIZE(state->pixmaps),
                  state->pixmaps_mmap_size, max_mmap_size);

        return tegra_exa_submit_deferred_3d_jobs(state);
    }

    tegra_exa_3d_state_ref_pixmap(state, state->new.dst.pix, true);
    tegra_exa_3d_state_ref_pixmap(state, state->new.src.pix, false);
    tegra_exa_3d_state_ref_pixmap(state, state->new.mask.pix, false);

    if (!state->flushtimer) {
        DEBUG_MSG("started deferred 3d job\n");

        timeout_ms = 1;

        state->flushtimer = TimerSet(NULL, timer_flags, timeout_ms,
                                     tegra_exa_flush_3d_state_cb, state);

        exa->pool_compaction_blockcnt++;
    }

    DEBUG_MSG("added to queue (%u)\n", state->num_jobs);
    tegra_exa_debug_dump_deferred_3d_state_pixmaps(state);

    return tegra_stream_get_current_fence(stream);
}

static bool
tegra_exa_3d_state_tex_cache_needs_flush(struct tegra_3d_state *state,
                                         PixmapPtr pixmap)
{
    unsigned int i;

    if (DISABLE_3D_OPTIMIZATIONS)
        return false;

    for (i = 0; i < state->num_pixmaps; i++) {
        if (state->pixmaps[i].pixmap->base == pixmap)
            return state->pixmaps[i].cache_dirty;
    }

    return false;
}

static void
tegra_exa_pixmap_3d_state_tex_cache_flushed(struct tegra_3d_state *state)
{
    unsigned int i;

    if (DISABLE_3D_OPTIMIZATIONS)
        return;

    for (i = 0; i < state->num_pixmaps; i++)
        state->pixmaps[i].cache_dirty = false;
}

static bool
tegra_exa_pixmap_is_in_deferred_3d_state(struct tegra_3d_state *state,
                                         struct tegra_pixmap *pixmap)
{
    unsigned int i;

    if (DISABLE_3D_OPTIMIZATIONS)
        return false;

    for (i = 0; i < state->num_pixmaps; i++) {
        if (state->pixmaps[i].pixmap == pixmap)
            return true;
    }

    return false;
}

static void tegra_exa_flush_deferred_3d_operations(PixmapPtr pixmap,
                                                   bool accel,
                                                   bool flush_reads,
                                                   bool flush_writes)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    struct tegra_exa *exa = TegraPTR(scrn)->exa;
    struct tegra_3d_state *state = &exa->gr3d_state;
    bool flush = false;
    unsigned int i;

    if (DISABLE_3D_OPTIMIZATIONS)
        return;

    for (i = 0; i < state->num_pixmaps; i++) {
        if (state->pixmaps[i].pixmap != priv)
            continue;

        if (flush_reads && state->pixmaps[i].read)
            flush = true;

        if (flush_writes && state->pixmaps[i].write)
            flush = true;

        break;
    }

    if (flush) {
        DEBUG_MSG("pixmap %p flush_reads %d flush_writes %d\n",
                  pixmap, flush_reads, flush_writes);

        tegra_exa_flush_deferred_3d_state(state);
    }
}

static void
tegra_exa_enter_optimization_3d_state(struct tegra_exa *exa)
{
    if (DISABLE_3D_OPTIMIZATIONS || !exa)
        return;

    tegra_exa_wrap_state(exa, &exa->opt_state[TEGRA_OPT_3D]);
}

static void
tegra_exa_exit_optimization_3d_state(struct tegra_exa *exa)
{
    if (DISABLE_3D_OPTIMIZATIONS || !exa)
        return;

    tegra_exa_unwrap_state(exa, &exa->opt_state[TEGRA_OPT_3D]);
}

static bool
tegra_exa_3d_state_deferred(struct tegra_3d_state *state)
{
    if (DISABLE_3D_OPTIMIZATIONS)
        return false;

    return state->num_jobs > 0;
}

/* vim: set et sts=4 sw=4 ts=4: */
