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
#include "tegra_stream.h"

#define TEGRA_DRI_USAGE_HINT ('D' << 16 | 'R' << 8 | 'I')

/*
 * The maximum alignment required by hardware seems is 64 bytes,
 * but we are also using VFP for copying write-combined data and
 * it requires a 128 bytes alignment.
 */
#define TEGRA_EXA_OFFSET_ALIGN          128

#if 0
#define FallbackMsg(fmt, args...)                                           \
    printf("FALLBACK: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define FallbackMsg(fmt, args...) do {} while(0)
#endif

#if 0
#define AccelMsg(fmt, args...)                                              \
    printf("ACCELERATE: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define AccelMsg(fmt, args...) do {} while(0)
#endif

#if 0
#define DebugMsg(fmt, args...)                                              \
    printf("DEBUG: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define DebugMsg(fmt, args...) do {} while(0)
#endif

#define PROFILE 0

static inline float timespec_diff(const struct timespec *start,
                                  const struct timespec *end)
{
    unsigned long seconds = end->tv_sec - start->tv_sec;
    long ns = end->tv_nsec - start->tv_nsec;

    if (ns < 0) {
        ns += 1000000000;
        seconds--;
    }

    return (seconds * 1000000000.0f + ns) / 1000;
}

#define PROFILE_DEF(PNAME)                                          \
    struct timespec profile_##PNAME __attribute__((unused));

#define PROFILE_START(PNAME)                                        \
    if (PROFILE) {                                                  \
        printf("%s:%d: profile start\n", __func__, __LINE__);       \
        clock_gettime(CLOCK_MONOTONIC, &profile_##PNAME);           \
    }

#define PROFILE_STOP(PNAME)                                         \
    if (PROFILE) {                                                  \
        unsigned profile_time;                                      \
        static struct timespec profile_end;                         \
        clock_gettime(CLOCK_MONOTONIC, &profile_end);               \
        profile_time = timespec_diff(&profile_##PNAME, &profile_end);\
        printf("%s:%d: profile stop: %u us\n",                      \
               __func__, __LINE__, profile_time);                   \
    }

struct tegra_pixmap;

typedef struct gr3d_tex_state {
    PixmapPtr pPix;
    Pixel solid;
    unsigned format : 5;
    unsigned tex_sel : 3;
    bool component_alpha : 1;
    bool coords_wrap : 1;
    bool bilinear : 1;
    bool alpha : 1;
    bool pow2 : 1;
    bool transform_coords : 1;
} TegraGR3DStateTex, *TegraGR3DStateTexPtr;

typedef struct gr3d_draw_state {
    TegraGR3DStateTex src;
    TegraGR3DStateTex mask;
    TegraGR3DStateTex dst;
    bool discards_clip : 1;
    int op;
} TegraGR3DDrawState, *TegraGR3DDrawStatePtr;

typedef struct gr3d_state {
    struct tegra_exa_scratch *scratch;
    struct tegra_stream *cmds;
    TegraGR3DDrawState new;
    TegraGR3DDrawState cur;
    bool inited : 1;
    bool clean : 1;
} TegraGR3DState, *TegraGR3DStatePtr;

typedef struct tegra_attrib_bo {
    struct drm_tegra_bo *bo;
    __fp16 *map;
} TegraEXAAttribBo;

enum Tegra2DOrientation {
    TEGRA2D_FLIP_X,
    TEGRA2D_FLIP_Y,
    TEGRA2D_TRANS_LR,
    TEGRA2D_TRANS_RL,
    TEGRA2D_ROT_90,
    TEGRA2D_ROT_180,
    TEGRA2D_ROT_270,
    TEGRA2D_IDENTITY,
};

enum Tegra2DCompositeOp {
    TEGRA2D_NONE,
    TEGRA2D_SOLID,
    TEGRA2D_COPY,
};

typedef struct tegra_exa_scratch {
    enum Tegra2DOrientation orientation;
    enum Tegra2DCompositeOp op2d;
    struct tegra_fence *marker;
    TegraEXAAttribBo attribs;
    union {
        PictTransform transform;

        struct {
            PictTransform transform_src;
            PictTransform transform_src_inv;
            PictTransform transform_mask;
            PictTransform transform_mask_inv;
        };
    };
    struct drm_tegra *drm;
    unsigned attrib_itr;
    unsigned vtx_cnt;
    PixmapPtr pMask;
    PixmapPtr pSrc;
    unsigned ops;
    int srcX;
    int srcY;
    int dstX;
    int dstY;
} TegraEXAScratch, *TegraEXAScratchPtr;

typedef struct {
    struct drm_tegra_bo *bo;
    struct xorg_list entry;
    struct mem_pool pool;
    void *ptr;
    Bool heavy : 1;
    Bool light : 1;
    Bool persitent : 1;
} TegraPixmapPool, *TegraPixmapPoolPtr;

typedef struct _TegraEXARec{
    struct drm_tegra_channel *gr2d;
    struct drm_tegra_channel *gr3d;
    struct tegra_stream *cmds;
    TegraEXAScratch scratch;
    TegraPixmapPoolPtr large_pool;
    struct xorg_list mem_pools;
    time_t pool_slow_compact_time;
    time_t pool_fast_compact_time;
    struct xorg_list cool_pixmaps;
    unsigned long cooling_size;
    time_t last_resurrect_time;
    time_t last_freezing_time;
    unsigned release_count;
    unsigned long default_drm_bo_flags;
    CreatePictureProcPtr CreatePicture;
    ScreenBlockHandlerProcPtr BlockHandler;
#ifdef HAVE_JPEG
    tjhandle jpegCompressor;
    tjhandle jpegDecompressor;
#endif

    TegraGR3DState gr3d_state;

    ExaDriverPtr driver;
} *TegraEXAPtr;

#define TEGRA_EXA_PIXMAP_TYPE_NONE      0
#define TEGRA_EXA_PIXMAP_TYPE_FALLBACK  1
#define TEGRA_EXA_PIXMAP_TYPE_BO        2
#define TEGRA_EXA_PIXMAP_TYPE_POOL      3

#define TEGRA_EXA_COMPRESSION_UNCOMPRESSED  1
#define TEGRA_EXA_COMPRESSION_LZ4           2
#define TEGRA_EXA_COMPRESSION_JPEG          3
#define TEGRA_EXA_COMPRESSION_PNG           4

typedef struct tegra_pixmap {
    bool tegra_data : 1;        /* pixmap's data allocated by Opentegra */
    bool scanout_rotated : 1;   /* pixmap backs rotated frontbuffer BO */
    bool no_compress : 1;       /* pixmap's data compress poorly */
    bool accelerated : 1;       /* pixmap was accelerated at least once */
    bool offscreen : 1;         /* pixmap's data resides in Tegra's GEM */
    bool scanout : 1;           /* pixmap backs frontbuffer BO */
    bool frozen : 1;            /* pixmap's data compressed */
    bool accel : 1;             /* pixmap acceleratable */
    bool cold : 1;              /* pixmap scheduled for compression */
    bool dri : 1;               /* pixmap's BO was exported */

    unsigned crtc : 1;          /* pixmap's CRTC ID (for display rotation) */

    unsigned type : 2;

    union {
        struct {
            union {
                struct {
                    struct tegra_fence *fence_write;
                    struct tegra_fence *fence_read;

                    union {
                        struct mem_pool_entry pool_entry;
                        struct drm_tegra_bo *bo;
                    };
                };

                void *fallback;
            };

            time_t last_use; /* in seconds */
            struct xorg_list fridge_entry;
        };

        struct {
            void *compressed_data;
            unsigned compressed_size;
            unsigned compression_type;
            unsigned compression_fmt;
        };
    };

    PixmapPtr pPixmap;

    unsigned picture_format;
} TegraPixmapRec, *TegraPixmapPtr;

unsigned int TegraEXAPitch(unsigned int width, unsigned int height,
                           unsigned int bpp);

static inline void tegra_exa_wait_fence(struct tegra_fence *fence)
{
    PROFILE_DEF(wait_fence)
    PROFILE_START(wait_fence)
    tegra_fence_wait(fence);
    PROFILE_STOP(wait_fence)
}
#define TegraEXAWaitFence(F)    \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "wait"); tegra_exa_wait_fence(F); })

unsigned TegraEXAHeightHwAligned(unsigned int height, unsigned int bpp);

static inline Pixel TegraPixelRGB565to888(Pixel pixel)
{
    Pixel p = 0;

    p |= 0xff000000;
    p |=  ((pixel >> 11)   * 255 + 15) / 31;
    p |=  (((pixel >> 5) & 0x3f) * 255 + 31) / 63;
    p |=  ((pixel & 0x3f)  * 255 + 15) / 31;

    return p;
}

static inline Pixel TegraPixelRGB888to565(Pixel pixel)
{
    unsigned red, green, blue;
    Pixel p = 0;

    red   = (pixel & 0x00ff0000) >> 16;
    green = (pixel & 0x0000ff00) >> 8;
    blue  = (pixel & 0x000000ff) >> 0;

    p |= ((red >> 3) & 0x1f) << 11;
    p |= ((green >> 2) & 0x3f) << 5;
    p |= (blue >> 3) & 0x1f;

    return p;
}

static inline Bool TegraCompositeFormatHasAlpha(unsigned format)
{
    switch (format) {
    case PICT_a8:
    case PICT_a8r8g8b8:
        return TRUE;

    default:
        return FALSE;
    }
}

#endif

/* vim: set et sts=4 sw=4 ts=4: */
