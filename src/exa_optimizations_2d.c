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

#include "driver.h"
#include "exa_mm.h"
#include "exa_optimizations.h"

#define DISABLE_2D_OPTIMIZATIONS    FALSE

static int tegra_exa_init_optimizations(TegraPtr tegra, TegraEXAPtr exa)
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
            ErrorMsg("failed to create command stream: %d\n", err);
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

static void tegra_exa_deinit_optimizations(TegraEXAPtr tegra)
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

static void tegra_exa_wrap_state(TegraEXAPtr tegra,
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

static void tegra_exa_unwrap_state(TegraEXAPtr tegra,
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

static void
tegra_exa_prepare_optimized_solid_fill(PixmapPtr pPixmap, Pixel color)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    Bool cpu_access = TRUE;
    Bool optimize = TRUE;

    if (tegra->in_flush || priv->scanout)
        optimize = FALSE;

    if (TegraEXAPixmapBusy(priv))
        cpu_access = FALSE;

    /*
     * This optimization pass skips the solid-fill drawing operation if
     * whole pixmap is filled with the same color. The operation isn't
     * canceled, but deferred.
     */

    if (DISABLE_2D_OPTIMIZATIONS) {
        cpu_access = FALSE;
        optimize = FALSE;
    }

    tegra->scratch.color = color;
    tegra->scratch.optimize = optimize;
    tegra->scratch.cpu_access = cpu_access;

    priv->freezer_lockcnt++;
}

static Bool tegra_exa_optimize_solid_op(PixmapPtr pPixmap,
                                        int px1, int py1,
                                        int px2, int py2)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    unsigned int cpp = pPixmap->drawable.bitsPerPixel >> 3;
    unsigned int bytes = (px2 - px1) * (py2 - py1) * cpp;
    void *ptr;

    if (tegra->scratch.optimize &&
        px1 == 0 && py1 == 0 &&
        pPixmap->drawable.width == px2 &&
        pPixmap->drawable.height == py2 &&
        bytes >= TEGRA_EXA_CPU_FILL_MIN_SIZE)
    {
        tegra_exa_cancel_deferred_operations(pPixmap);

        DebugMsg("pixmap %p applying deferred solid-fill optimization\n",
                 pPixmap);

        tegra->scratch.cpu_access = FALSE;

        priv->state.solid_color = tegra->scratch.color;
        priv->state.solid_fill = 1;

        return TRUE;
    }

    if (tegra->scratch.optimize &&
        priv->state.solid_fill &&
        priv->state.solid_color == tegra->scratch.color)
    {
        DebugMsg("pixmap %p partial solid-fill optimized out\n", pPixmap);
        return TRUE;
    }

    if (tegra->scratch.optimize && priv->state.solid_fill) {
        tegra_exa_flush_deferred_operations(pPixmap, TRUE);

        if (tegra->scratch.cpu_access && TegraEXAPixmapBusy(priv))
            tegra->scratch.cpu_access = FALSE;
    }

    /*
     * It's much more optimal to perform small write-only operations on CPU
     * if GPU isn't touching pixmap. The job submission overhead is too big
     * + this allows to perform operation in parallel with GPU.
     */
    if (tegra->scratch.cpu_access && bytes < TEGRA_EXA_CPU_FILL_MIN_SIZE &&
        TegraEXAPrepareCPUAccess(pPixmap, EXA_PREPARE_DEST, &ptr))
    {
        DebugMsg("pixmap %p partial solid-fill optimized to a CPU-fill\n",
                 pPixmap);

        pixman_fill(ptr,
                    pPixmap->devKind / 4,
                    pPixmap->drawable.bitsPerPixel,
                    px1, py1, px2 - px1, py2 - py1,
                    tegra->scratch.color);

        TegraEXAFinishCPUAccess(pPixmap, EXA_PREPARE_DEST);

        return TRUE;
    }

    return FALSE;
}

static void tegra_exa_complete_solid_fill_optimization(PixmapPtr pPixmap)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    if (tegra->scratch.ops)
        tegra_exa_flush_deferred_operations(pPixmap, TRUE);

    tegra->scratch.optimize = FALSE;

    priv->freezer_lockcnt--;
}

static void tegra_exa_perform_deferred_solid_fill(PixmapPtr pPixmap, Bool accel)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    if (priv->state.solid_fill) {
        DebugMsg("pixmap %p %d:%d:%d performing deferred solid-fill (%08lx)\n",
                 pPixmap,
                 pPixmap->drawable.width,
                 pPixmap->drawable.height,
                 pPixmap->drawable.bitsPerPixel,
                 priv->state.solid_color);
        priv->state.solid_fill = 0;

        tegra_exa_wrap_state(tegra, &tegra->opt_state[TEGRA_OPT_SOLID]);
        TegraEXAFillPixmapData(priv, accel, priv->state.solid_color);
        tegra_exa_unwrap_state(tegra, &tegra->opt_state[TEGRA_OPT_SOLID]);
    }
}

static void tegra_exa_flush_deferred_operations(PixmapPtr pPixmap, Bool accel)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;

    if (tegra->in_flush)
        return;

    if (priv->state.solid_fill)
        DebugMsg("pixmap %p flushing deferred operations\n", pPixmap);

    tegra->in_flush = TRUE;
    tegra_exa_perform_deferred_solid_fill(pPixmap, accel);
    tegra->in_flush = FALSE;
}

static void tegra_exa_cancel_deferred_operations(PixmapPtr pPixmap)
{
    TegraPixmapPtr priv = exaGetPixmapDriverPrivate(pPixmap);

    if (priv->state.solid_fill)
        DebugMsg("pixmap %p canceled deferred solid-fill\n", pPixmap);

    priv->state.solid_fill = 0;
}

static Bool tegra_exa_optimize_same_color_copy(TegraPixmapPtr src_priv,
                                               TegraPixmapPtr dst_priv)
{
    if (src_priv->state.solid_fill && dst_priv->state.solid_fill &&
        src_priv->state.solid_color == dst_priv->state.solid_color)
        return TRUE;

    return FALSE;
}

static void tegra_exa_prepare_optimized_copy(PixmapPtr pSrcPixmap,
                                             PixmapPtr pDstPixmap,
                                             int op, Pixel planemask)
{
    TegraPixmapPtr src_priv = exaGetPixmapDriverPrivate(pSrcPixmap);
    TegraPixmapPtr dst_priv = exaGetPixmapDriverPrivate(pDstPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    Bool optimize = TRUE;

    /*
     * This optimization pass turns copy operations into a solid color
     * fill, it also cancels deferred solid fill operation of pDstPixmap
     * if pSrcPixmap is copied over the whole pDstPixmap.
     */

    if (!src_priv->state.solid_fill && !dst_priv->state.solid_fill)
        optimize = FALSE;

    if (DISABLE_2D_OPTIMIZATIONS)
        optimize = FALSE;

    if (optimize && src_priv->state.solid_fill) {
        if (tegra_exa_optimize_same_color_copy(src_priv, dst_priv)) {
            DebugMsg("pixmap %p -> %p copy optimized out to a same-color solid-fill\n",
                        pSrcPixmap, pDstPixmap);
        } else {
            DebugMsg("pixmap %p -> %p copy optimized to a partial solid-fill\n",
                     pSrcPixmap, pDstPixmap);

            tegra_exa_wrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);
            optimize = TegraEXAPrepareSolid(pDstPixmap, op, planemask,
                                            src_priv->state.solid_color);
            tegra_exa_unwrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);
        }
    }

    tegra->scratch.optimize = optimize;

    src_priv->freezer_lockcnt++;
    dst_priv->freezer_lockcnt++;
}

static Bool tegra_exa_optimize_copy_op(PixmapPtr pDstPixmap,
                                       int dstX, int dstY,
                                       int width, int height)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    PixmapPtr pSrcPixmap = tegra->scratch.pSrc;

    if (tegra->scratch.optimize) {
        TegraPixmapPtr src_priv = exaGetPixmapDriverPrivate(pSrcPixmap);
        TegraPixmapPtr dst_priv = exaGetPixmapDriverPrivate(pDstPixmap);

        if (src_priv->state.solid_fill) {
            if (tegra_exa_optimize_same_color_copy(src_priv, dst_priv))
                return TRUE;

            tegra_exa_wrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);
            TegraEXASolid(pDstPixmap, dstX, dstY, dstX + width, dstY + height);
            tegra_exa_unwrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);

            return TRUE;
        } else if (dstX == 0 && dstY == 0 &&
                   pDstPixmap->drawable.width == width &&
                   pDstPixmap->drawable.height == height) {
            tegra_exa_cancel_deferred_operations(pDstPixmap);
        }
    }

    return FALSE;
}

static void tegra_exa_complete_solid_fill_copy_optimization(PixmapPtr pDstPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    TegraPixmapPtr src_priv = exaGetPixmapDriverPrivate(tegra->scratch.pSrc);
    TegraPixmapPtr dst_priv = exaGetPixmapDriverPrivate(pDstPixmap);

    if (tegra_exa_optimize_same_color_copy(src_priv, dst_priv))
        return;

    tegra_exa_wrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);
    TegraEXADoneSolid(pDstPixmap);
    tegra_exa_unwrap_state(tegra, &tegra->opt_state[TEGRA_OPT_COPY]);

    tegra->scratch.ops = 0;
}

static void tegra_exa_complete_copy_optimization(PixmapPtr pDstPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
    TegraEXAPtr tegra = TegraPTR(pScrn)->exa;
    PixmapPtr pSrcPixmap = tegra->scratch.pSrc;
    TegraPixmapPtr src_priv = exaGetPixmapDriverPrivate(pSrcPixmap);
    TegraPixmapPtr dst_priv = exaGetPixmapDriverPrivate(pDstPixmap);

    if (tegra->scratch.optimize && src_priv->state.solid_fill) {
        tegra_exa_complete_solid_fill_copy_optimization(pDstPixmap);
    } else if (tegra->scratch.ops) {
        tegra_exa_flush_deferred_operations(pSrcPixmap, TRUE);
        tegra_exa_flush_deferred_operations(pDstPixmap, TRUE);
    }

    tegra->scratch.optimize = FALSE;

    src_priv->freezer_lockcnt--;
    dst_priv->freezer_lockcnt--;
}
