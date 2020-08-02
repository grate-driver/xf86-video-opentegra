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

static int tegra_exa_init_optimizations(TegraPtr tegra, struct tegra_exa *exa)
{
    unsigned int i, k;
    int err;

    /*
     * Each optimization pass will be executed in own context,
     * where context consists of tegra-scratch parameters and
     * a commands stream. The scratch parameters already initialized
     * to 0.
     */
    for (i = 0; i < TEGRA_OPT_NUM; i++) {
        err = tegra_stream_create(&exa->opt_state[i].cmds, tegra->drm);
        if (err < 0) {
            ERROR_MSG("failed to create command stream: %d\n", err);
            goto fail;
        }

        exa->opt_state[i].scratch.drm = tegra->drm;
        exa->opt_state[i].id = i;

        for (k = 0; k < TEGRA_ENGINES_NUM; k++)
            exa->opt_state[i].cmds->last_fence[k] = poisoned_fence;
    }

    return 0;

fail:
    while (i--)
        tegra_stream_destroy(exa->opt_state[i].cmds);

    return err;
}

static void tegra_exa_deinit_optimizations(struct tegra_exa *tegra)
{
    unsigned int i = TEGRA_OPT_NUM, k;

    while (i--) {
        for (k = 0; k < TEGRA_ENGINES_NUM; k++) {
            if (tegra->opt_state[i].cmds->last_fence[k] == poisoned_fence)
                tegra->opt_state[i].cmds->last_fence[k] = NULL;
        }

        tegra_stream_destroy(tegra->opt_state[i].cmds);
        tegra->opt_state[i].cmds = NULL;
    }
}

static void tegra_exa_transfer_stream_fences(struct tegra_stream *stream_dst,
                                             struct tegra_stream *stream_src)
{
    unsigned int i;

    stream_dst->fence_seqno = stream_src->fence_seqno;

    /* transfer fences from stream A (src) to stream B (dst) */
    for (i = 0; i < TEGRA_ENGINES_NUM; i++) {
        assert(stream_dst->last_fence[i] == poisoned_fence);

        stream_dst->last_fence[i] = stream_src->last_fence[i];
        stream_src->last_fence[i] = poisoned_fence;
    }
}

static void tegra_exa_wrap_state(struct tegra_exa *tegra,
                                 struct tegra_optimization_state *state)
{
    /*
     * This function replaces current drawing context with the optimization
     * context, the current context is stashed.
     */

    DEBUG_MSG("state %p id %u wrapcnt %u\n", state, state->id, state->wrapcnt);

    if (state->wrapcnt++)
        return;

    tegra_exa_transfer_stream_fences(state->cmds, tegra->cmds);

    state->cmds_tmp = tegra->cmds;
    tegra->cmds = state->cmds;

    state->scratch_tmp = tegra->scratch;
    tegra->scratch = state->scratch;
}

static void tegra_exa_unwrap_state(struct tegra_exa *tegra,
                                   struct tegra_optimization_state *state)
{
    /*
     * This function replaces current (optimization) drawing context with
     * the previous context, i.e. the previous context is restored.
     */

    DEBUG_MSG("state %p id %u wrapcnt %u\n", state, state->id, state->wrapcnt);

    if (--state->wrapcnt)
        return;

    state->scratch = tegra->scratch;
    tegra->scratch = state->scratch_tmp;
    tegra->cmds = state->cmds_tmp;

    tegra_exa_transfer_stream_fences(tegra->cmds, state->cmds);
}

static void tegra_exa_flush_deferred_operations(PixmapPtr pixmap,
                                                bool accel,
                                                bool flush_reads,
                                                bool flush_writes)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);

    assert(priv->type > TEGRA_EXA_PIXMAP_TYPE_FALLBACK);
    if (priv->type <= TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
        return;

    tegra_exa_flush_deferred_2d_operations(pixmap, accel, flush_reads, flush_writes);
    tegra_exa_flush_deferred_3d_operations(pixmap, accel, flush_reads, flush_writes);
}

static void tegra_exa_cancel_deferred_operations(PixmapPtr pixmap)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);

    if (priv->state.solid_fill)
        DEBUG_MSG("pixmap %p canceled deferred solid-fill\n", pixmap);

    priv->state.solid_fill = 0;
}

/* vim: set et sts=4 sw=4 ts=4: */
