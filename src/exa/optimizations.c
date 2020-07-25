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
    unsigned int i;
    int err;

    /*
     * Each optimization pass will be executed in own context,
     * where context consists of tegra-scratch parameters and
     * a commands stream. The scratch parameters already initialized
     * to 0.
     */
    for (i = 0; i < TEGRA_OPT_NUM; i++) {
        err = tegra_stream_create(&exa->opt_state[i].cmds, tegra);
        if (err < 0) {
            ERROR_MSG("failed to create command stream: %d\n", err);
            goto fail;
        }

        exa->opt_state[i].scratch.drm = tegra->drm;
    }

    return 0;

fail:
    while (i--)
        tegra_stream_destroy(exa->opt_state[i].cmds);

    return err;
}

static void tegra_exa_deinit_optimizations(struct tegra_exa *tegra)
{
    unsigned int i = TEGRA_OPT_NUM;

    while (i--) {
        tegra_stream_destroy(tegra->opt_state[i].cmds);
        tegra->opt_state[i].cmds = NULL;
    }
}

static void tegra_exa_transfer_stream_fences(struct tegra_stream *stream_dst,
                                             struct tegra_stream *stream_src)
{
    struct tegra_fence *fence;
    unsigned int i;

    /* transfer fences from stream A (src) to stream B (dst) */
    for (i = 0; i < TEGRA_ENGINES_NUM; i++) {
        fence = stream_src->last_fence[i];

        if (fence) {
            TEGRA_FENCE_PUT(stream_dst->last_fence[i]);
            stream_src->last_fence[i] = NULL;

            fence->seqno = stream_dst->fence_seqno++;
            stream_dst->last_fence[i] = fence;
        }
    }
}

static void tegra_exa_wrap_state(struct tegra_exa *tegra,
                                 struct tegra_optimization_state *state)
{
    /*
     * This function replaces current drawing context with the optimization
     * context, the current context is stashed.
     */

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

    state->scratch = tegra->scratch;
    tegra->scratch = state->scratch_tmp;
    tegra->cmds = state->cmds_tmp;

    tegra_exa_transfer_stream_fences(tegra->cmds, state->cmds);
}

static void tegra_exa_flush_deferred_operations(PixmapPtr pixmap, bool accel)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct tegra_exa *tegra = TegraPTR(scrn)->exa;

    if (tegra->in_flush)
        return;

    if (priv->state.solid_fill)
        DEBUG_MSG("pixmap %p flushing deferred operations\n", pixmap);

    tegra->in_flush = true;
    tegra_exa_perform_deferred_solid_fill(pixmap, accel);
    tegra->in_flush = false;
}

static void tegra_exa_cancel_deferred_operations(PixmapPtr pixmap)
{
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);

    if (priv->state.solid_fill)
        DEBUG_MSG("pixmap %p canceled deferred solid-fill\n", pixmap);

    priv->state.solid_fill = 0;
}

/* vim: set et sts=4 sw=4 ts=4: */
