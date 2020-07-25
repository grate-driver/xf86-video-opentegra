/*
 * Copyright © 2014 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __TEGRA_EXA_CORE_H
#define __TEGRA_EXA_CORE_H

#include "exa.h"
#include "gpu/gr3d.h"
#include "memcpy-vfp/memcpy_vfp.h"

static unsigned tegra_exa_pixmap_size(struct tegra_pixmap *pixmap);
static unsigned long tegra_exa_pixmap_offset(PixmapPtr pix);
static struct drm_tegra_bo *tegra_exa_pixmap_bo(PixmapPtr pix);
static bool tegra_exa_pixmap_is_from_pool(PixmapPtr pix);
static bool tegra_exa_pixmap_is_busy(struct tegra_pixmap *pixmap);
static void tegra_exa_clean_up_pixmaps_freelist(TegraPtr tegra, bool force);

static bool tegra_exa_prepare_cpu_access(PixmapPtr pix, int idx, void **ptr,
                                         bool cancel_optimizations);
static void tegra_exa_finish_cpu_access(PixmapPtr pix, int idx);

static bool
tegra_exa_pixmap_allocate_from_pool(TegraPtr tegra,
                                    struct tegra_pixmap *pixmap,
                                    unsigned int size);

static bool tegra_exa_pixmap_allocate_from_bo(TegraPtr tegra,
                                              struct tegra_pixmap *pixmap,
                                              unsigned int size);

static bool tegra_exa_pixmap_allocate_from_sysmem(TegraPtr tegra,
                                                  struct tegra_pixmap *pixmap,
                                                  unsigned int size);

static int tegra_exa_init_mm(TegraPtr tegra, struct tegra_exa *exa);
static void tegra_exa_release_mm(TegraPtr tegra, struct tegra_exa *exa);

static void tegra_exa_cool_tegra_pixmap(TegraPtr tegra, struct tegra_pixmap *pix);
static void tegra_exa_cool_pixmap(PixmapPtr pixmap, bool write);
static void tegra_exa_thaw_pixmap(PixmapPtr pixmap, bool accel);
static void tegra_exa_thaw_pixmap2(PixmapPtr pixmap, enum thaw_accel accel,
                                   enum thaw_alloc allocate);
static void tegra_exa_freeze_pixmaps(TegraPtr tegra, time_t time_sec);
static void tegra_exa_fill_pixmap_data(struct tegra_pixmap *pixmap,
                                       bool accel, Pixel color);

static void tegra_exa_flush_deferred_operations(PixmapPtr pixmap, bool accel);
static void tegra_exa_cancel_deferred_operations(PixmapPtr pixmap);

static void
tegra_exa_prepare_optimized_solid_fill(PixmapPtr pixmap, Pixel color);
static bool tegra_exa_optimize_solid_op(PixmapPtr pixmap,
                                        int px1, int py1,
                                        int px2, int py2);
static void tegra_exa_complete_solid_fill_optimization(PixmapPtr pixmap);

static void tegra_exa_prepare_optimized_copy(PixmapPtr pSrcPixmap,
                                             PixmapPtr pDstPixmap,
                                             int op, Pixel planemask);
static bool tegra_exa_optimize_copy_op(PixmapPtr pDstPixmap,
                                       int dst_x, int dst_y,
                                       int width, int height);
static void tegra_exa_complete_copy_optimization(PixmapPtr pDstPixmap);

static void
tegra_exa_optimize_texture_sampler(struct tegra_texture_state *tex);
static const struct shader_program *
tegra_exa_select_optimized_gr3d_program(struct tegra_3d_state *state);
static void tegra_exa_optimize_alpha_component(struct tegra_3d_draw_state *state);

static void tegra_exa_perform_deferred_solid_fill(PixmapPtr pixmap, bool accel);

#endif
