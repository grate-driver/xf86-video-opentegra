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
    struct drm_tegra_bo *bo;
    struct xorg_list entry;
    struct mem_pool pool;
    void *ptr;
    Bool heavy : 1;
    Bool light : 1;
} TegraPixmapPool, *TegraPixmapPoolPtr;

typedef struct _TegraEXARec{
    struct drm_tegra_channel *gr2d;
    struct drm_tegra_channel *gr3d;
    struct tegra_stream cmds;
    TegraEXAScratch scratch;
    struct xorg_list mem_pools;
    time_t pool_slow_compact_time;
    time_t pool_fast_compact_time;
    struct xorg_list cool_pixmaps;
    unsigned long cooling_size;
    time_t last_resurrect_time;
    time_t last_freezing_time;
    unsigned release_count;

    ExaDriverPtr driver;
} *TegraEXAPtr;

#define TEGRA_EXA_PIXMAP_TYPE_NONE      0
#define TEGRA_EXA_PIXMAP_TYPE_FALLBACK  1
#define TEGRA_EXA_PIXMAP_TYPE_BO        2
#define TEGRA_EXA_PIXMAP_TYPE_POOL      3

typedef struct {
    Bool no_compress : 1;   /* pixmap's data compress poorly */
    Bool scanout : 1;       /* pixmap backs frontbuffer BO */
    Bool frozen : 1;        /* pixmap's data compressed */
    Bool accel : 1;         /* pixmap acceleratable */
    Bool cold : 1;          /* pixmap scheduled for compression */
    Bool dri : 1;           /* pixmap's BO was exported */

    unsigned type : 2;

    union {
        struct {
            union {
                struct {
                    struct tegra_fence *fence;

                    union {
                        struct mem_pool_entry pool_entry;
                        struct drm_tegra_bo *bo;
                    };
                };

                void *fallback;
            };

            time_t last_use : 16; /* 8 seconds per unit */
            struct xorg_list fridge_entry;
        };

        struct {
            void *compressed_data;
        };
    };

    unsigned data_size;
} TegraPixmapRec, *TegraPixmapPtr;

unsigned int TegraEXAPitch(unsigned int width, unsigned int height,
                           unsigned int bpp);

void TegraEXAWaitFence(struct tegra_fence *fence);

#endif

/* vim: set et sts=4 sw=4 ts=4: */
