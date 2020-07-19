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

#ifndef __TEGRA_EXA_OPTIMIZATIONS_H
#define __TEGRA_EXA_OPTIMIZATIONS_H

static void tegra_exa_flush_deferred_operations(PixmapPtr pPixmap, Bool accel);
static void tegra_exa_cancel_deferred_operations(PixmapPtr pPixmap);

static void
tegra_exa_prepare_optimized_solid_fill(PixmapPtr pPixmap, Pixel color);
static Bool tegra_exa_optimize_solid_op(PixmapPtr pPixmap,
                                        int px1, int py1,
                                        int px2, int py2);
static void tegra_exa_complete_solid_fill_optimization(PixmapPtr pPixmap);

static void tegra_exa_prepare_optimized_copy(PixmapPtr pSrcPixmap,
                                             PixmapPtr pDstPixmap,
                                             int op, Pixel planemask);
static Bool tegra_exa_optimize_copy_op(PixmapPtr pDstPixmap,
                                       int dstX, int dstY,
                                       int width, int height);
static void tegra_exa_complete_copy_optimization(PixmapPtr pDstPixmap);

static void
tegra_exa_optimize_texture_sampler(TegraGR3DStateTex *tex);
static const struct shader_program *
tegra_exa_select_optimized_gr3d_program(TegraGR3DStatePtr state);
static void tegra_exa_optimize_alpha_component(TegraGR3DDrawStatePtr state);

#endif
