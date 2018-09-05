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

#ifndef __TEGRA_EXA_H
#define __TEGRA_EXA_H

#include "pool_alloc.h"

#define TEGRA_DRI_USAGE_HINT ('D' << 16 | 'R' << 8 | 'I')

#define TEGRA_EXA_OFFSET_ALIGN          256

typedef struct tegra_attrib_bo {
    struct tegra_attrib_bo *next;
    struct drm_tegra_bo *bo;
    __fp16 *map;
} TegraEXAAttribBo;

typedef struct tegra_exa_scratch {
    struct tegra_fence *marker;
    TegraEXAAttribBo *attribs;
    Bool attribs_alloc_err;
    struct drm_tegra *drm;
    unsigned attrib_itr;
    unsigned vtx_cnt;
    PixmapPtr pMask;
    PixmapPtr pSrc;
    Bool solid2D;
    unsigned ops;
} TegraEXAScratch, *TegraEXAScratchPtr;

typedef struct {
    struct xorg_list entry;
    struct drm_tegra_bo *bo;
    unsigned int alloc_cnt;
    struct mem_pool pool;
    void *ptr;
} TegraPixmapPool, *TegraPixmapPoolPtr;

typedef struct _TegraEXARec{
    struct drm_tegra_channel *gr2d;
    struct drm_tegra_channel *gr3d;
    struct tegra_stream cmds;
    TegraEXAScratch scratch;
    struct xorg_list mem_pools;
    time_t pool_compact_time;

    ExaDriverPtr driver;
} *TegraEXAPtr;

typedef struct {
    struct tegra_fence *fence;
    struct drm_tegra_bo *bo;
    void *fallback;
    Bool dri;

    struct mem_pool_entry pool_entry;
} TegraPixmapRec, *TegraPixmapPtr;

unsigned int TegraEXAPitch(unsigned int width, unsigned int height,
                           unsigned int bpp);

#endif

/* vim: set et sts=4 sw=4 ts=4: */
