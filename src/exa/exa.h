/*
 * Copyright © 2014 NVIDIA Corporation
 * Copyright (c) GRATE-DRIVER project
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

#include "driver.h"

#include "gpu/tegra_stream.h"
#include "mempool/pool_alloc.h"

#ifndef __maybe_unused
#define __maybe_unused  __attribute__((unused))
#endif

#define TEGRA_DRI_USAGE_HINT    ('D' << 16 | 'R' << 8 | 'I')

/*
 * The maximum alignment required by hardware seems is 64 bytes,
 * but we are also using VFP for copying write-combined data and
 * it requires a 128 bytes alignment.
 */
#define TEGRA_EXA_OFFSET_ALIGN  128

#if 0
#define FALLBACK_MSG(fmt, args...) \
    printf("FALLBACK: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define FALLBACK_MSG(fmt, args...) do {} while(0)
#endif

#if 0
#define ACCEL_MSG(fmt, args...) \
    printf("ACCELERATE: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define ACCEL_MSG(fmt, args...) do {} while(0)
#endif

#if 0
#define DEBUG_MSG(fmt, args...) \
    printf("DEBUG: %s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define DEBUG_MSG(fmt, args...) do {} while(0)
#endif

#define ERROR_MSG(fmt, args...)                                             \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__,                   \
               __LINE__, __func__, ##args)

#define INFO_MSG(scrn, fmt, args...)                                        \
    xf86DrvMsg((scrn)->scrnIndex, X_INFO, fmt, ##args)

#define INFO_MSG2(fmt, args...)                                             \
    xf86DrvMsg(-1, X_INFO, fmt, ##args)

#define TEGRA_EXA_CPU_FILL_MIN_SIZE     (128 * 1024)

#define PROFILE                         0
#define PROFILE_GPU                     0

#define PROFILE_REPORT_MIN_TIME_US      1000
#define PROFILE_REPORT_START            false

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

extern uint64_t tegra_profiler_seqno;

struct tegra_profiler {
    struct timespec start_time;
    const char *name;
    uint64_t seqno;
};

#define PROFILE_DEF(PNAME)                                      \
    struct tegra_profiler profile_##PNAME __maybe_unused = {    \
        .name = "",                                             \
    };

#define PROFILE_SET_NAME(PNAME, PEXT_NAME)                      \
    profile_##PNAME.name = PEXT_NAME;

#define PROFILE_START(PNAME)                                    \
    if (PROFILE) {                                              \
        profile_##PNAME.seqno = tegra_profiler_seqno++;         \
                                                                \
        if (PROFILE_REPORT_START)                               \
            printf("%s:%d: profile(%llu:%s) start\n",           \
                   __func__, __LINE__,                          \
                   profile_##PNAME.seqno,                       \
                   profile_##PNAME.name);                       \
                                                                \
        clock_gettime(CLOCK_MONOTONIC,                          \
                      &profile_##PNAME.start_time);             \
    }

#define PROFILE_STOP(PNAME)                                     \
    if (PROFILE) {                                              \
        unsigned profile_time;                                  \
        static struct timespec profile_end;                     \
                                                                \
        clock_gettime(CLOCK_MONOTONIC, &profile_end);           \
                                                                \
        profile_time = timespec_diff(&profile_##PNAME.start_time,\
                                     &profile_end);             \
                                                                \
        if (profile_time >= PROFILE_REPORT_MIN_TIME_US)         \
            printf("%s:%d: profile(%llu:%s) stop: %u us\n",     \
                   __func__, __LINE__,                          \
                   profile_##PNAME.seqno,                       \
                   profile_##PNAME.name,                        \
                   profile_time);                               \
    }

struct tegra_pixmap;

struct tegra_texture_state {
    PixmapPtr pix;
    Pixel solid;
    unsigned format : 5;
    unsigned tex_sel : 3;
    bool component_alpha : 1;
    bool coords_wrap : 1;
    bool bilinear : 1;
    bool alpha : 1;
    bool pow2 : 1;
    bool transform_coords : 1;
};

struct tegra_3d_draw_state {
    struct tegra_texture_state src;
    struct tegra_texture_state mask;
    struct tegra_texture_state dst;
    bool dst_full_cover : 1;
    bool discards_clip : 1;
    bool optimized_out : 1;
    int op;
};

struct tegra_3d_state {
    struct tegra_exa_scratch *scratch;
    struct tegra_stream *cmds;
    struct tegra_3d_draw_state new;
    struct tegra_3d_draw_state cur;
    bool inited : 1;
    bool clean : 1;
};

struct tegra_attrib_bo {
    struct drm_tegra_bo *bo;
    __fp16 *map;
};

enum tegra_2d_orientation {
    TEGRA2D_FLIP_X,
    TEGRA2D_FLIP_Y,
    TEGRA2D_TRANS_LR,
    TEGRA2D_TRANS_RL,
    TEGRA2D_ROT_90,
    TEGRA2D_ROT_180,
    TEGRA2D_ROT_270,
    TEGRA2D_IDENTITY,
};

enum tegra_2d_composite_op {
    TEGRA2D_NONE,
    TEGRA2D_SOLID,
    TEGRA2D_COPY,
};

struct tegra_exa_scratch {
    enum tegra_2d_orientation orientation;
    enum tegra_2d_composite_op op2d;
    struct tegra_attrib_bo attribs;
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
    bool cpu_access;
    PixmapPtr mask;
    void *cpu_ptr;
    bool optimize;
    PixmapPtr src;
    unsigned ops;
    Pixel color;
    int src_x;
    int src_y;
    int dst_x;
    int dst_y;
};

struct tegra_pixmap_pool {
    struct drm_tegra_bo *bo;
    struct xorg_list entry;
    struct mem_pool pool;
    bool heavy : 1;
    bool light : 1;
    bool persistent : 1;
};

enum {
    TEGRA_OPT_SOLID,
    TEGRA_OPT_COPY,
    TEGRA_OPT_NUM,
};

struct tegra_optimization_state {
    struct tegra_stream *cmds_tmp;
    struct tegra_stream *cmds;
    struct tegra_exa_scratch scratch_tmp;
    struct tegra_exa_scratch scratch;
};

struct tegra_exa {
    struct drm_tegra_channel *gr2d;
    struct drm_tegra_channel *gr3d;
    struct tegra_stream *cmds;
    struct tegra_exa_scratch scratch;

    struct tegra_pixmap_pool *large_pool;
    struct xorg_list mem_pools;
    time_t pool_slow_compact_time;
    time_t pool_fast_compact_time;

    struct xorg_list cool_pixmaps;
    unsigned long cooling_size;
    time_t last_resurrect_time;
    time_t last_freezing_time;
#ifdef HAVE_JPEG
    tjhandle jpegCompressor;
    tjhandle jpegDecompressor;
#endif

    unsigned release_count;
    unsigned long default_drm_bo_flags;

    CreatePictureProcPtr create_picture;
    ScreenBlockHandlerProcPtr block_handler;

    struct xorg_list pixmaps_freelist;

    struct tegra_3d_state gr3d_state;

    bool has_iommu_bug;

    struct tegra_optimization_state opt_state[TEGRA_OPT_NUM];
    bool in_flush;
};

#define TEGRA_EXA_PIXMAP_TYPE_NONE              0
#define TEGRA_EXA_PIXMAP_TYPE_FALLBACK          1
#define TEGRA_EXA_PIXMAP_TYPE_BO                2
#define TEGRA_EXA_PIXMAP_TYPE_POOL              3

#define TEGRA_EXA_COMPRESSION_UNCOMPRESSED      1
#define TEGRA_EXA_COMPRESSION_LZ4               2
#define TEGRA_EXA_COMPRESSION_JPEG              3
#define TEGRA_EXA_COMPRESSION_PNG               4

struct tegra_pixmap_upload_buffer {
    unsigned int refcount;
    void *data;
};

struct tegra_pixmap {
    bool tegra_data : 1;        /* pixmap's data allocated by Opentegra */
    bool scanout_rotated : 1;   /* pixmap backs rotated frontbuffer BO */
    bool no_compress : 1;       /* pixmap's data compress poorly */
    bool accelerated : 1;       /* pixmap was accelerated at least once */
    bool offscreen : 1;         /* pixmap's data resides in Tegra's GEM */
    bool destroyed : 1;         /* pixmap was destroyed by EXA core */
    bool scanout : 1;           /* pixmap backs frontbuffer BO */
    bool frozen : 1;            /* pixmap's data compressed */
    bool accel : 1;             /* pixmap acceleratable */
    bool cold : 1;              /* pixmap scheduled for compression */
    bool dri : 1;               /* pixmap's BO was exported */

    unsigned crtc : 1;          /* pixmap's CRTC ID (for display rotation) */

    unsigned type : 2;

    unsigned freezer_lockcnt;   /* pixmap's data won't be touched by fridge while > 0 */

    struct pixmap_state {
        bool alpha_0 : 1;       /* pixmap's alpha component is 0x00 (RGBX texture) */
        bool solid_fill : 1;    /* whole pixmap is filled with a solid color */

        Pixel solid_color;
    } state;

    union {
        struct {
            union {
                struct {
                    struct tegra_fence *fence_write[TEGRA_ENGINES_NUM];
                    struct tegra_fence *fence_read[TEGRA_ENGINES_NUM];

                    union {
                        struct mem_pool_entry pool_entry;
                        struct drm_tegra_bo *bo;
                    };
                };

                void *fallback;
            };

            time_t last_use; /* in seconds */
            union {
                struct xorg_list fridge_entry;
                struct xorg_list freelist_entry;
            };
        };

        struct {
            void *compressed_data;
            unsigned compressed_size;
            unsigned compression_type;
            unsigned compression_fmt;
        };
    };

    PixmapPtr base;

    unsigned picture_format;
};

#define TEGRA_WAIT_FENCE(F)                                     \
({                                                              \
    PROFILE_DEF(wait_fence);                                    \
    profile_wait_fence.name = "wait_fence";                     \
    PROFILE_START(wait_fence);                                  \
    TEGRA_FENCE_DEBUG_MSG(F, "wait"); tegra_fence_wait(F);      \
    PROFILE_STOP(wait_fence);                                   \
})

#define TEGRA_WAIT_AND_PUT_FENCE(F)                             \
({                                                              \
    if (F) {                                                    \
        TEGRA_WAIT_FENCE(F);                                    \
        TEGRA_FENCE_PUT(F);                                     \
        F = NULL;                                               \
    }                                                           \
})

#define TEGRA_PIXMAP_WAIT_READ_FENCES(P)                        \
({                                                              \
    TEGRA_WAIT_AND_PUT_FENCE((P)->fence_read[TEGRA_2D]);        \
    TEGRA_WAIT_AND_PUT_FENCE((P)->fence_read[TEGRA_3D]);        \
})

#define TEGRA_PIXMAP_WAIT_WRITE_FENCES(P)                       \
({                                                              \
    TEGRA_WAIT_AND_PUT_FENCE((P)->fence_write[TEGRA_2D]);       \
    TEGRA_WAIT_AND_PUT_FENCE((P)->fence_write[TEGRA_3D]);       \
})

#define TEGRA_PIXMAP_WAIT_ALL_FENCES(P)                         \
({                                                              \
    TEGRA_PIXMAP_WAIT_READ_FENCES(P);                           \
    TEGRA_PIXMAP_WAIT_WRITE_FENCES(P);                          \
})

static inline struct tegra_fence *
tegra_exa_stream_submit(struct tegra_exa *tegra, enum host1x_engine engine,
                        struct tegra_fence *explicit_fence)
{
    struct tegra_fence *out_fence = NULL;

    if (PROFILE_GPU || tegra->has_iommu_bug)
        tegra_stream_flush(tegra->cmds, explicit_fence);
    else
        out_fence = tegra_stream_submit(engine, tegra->cmds, explicit_fence);

    return out_fence;
}

enum thaw_accel {
    THAW_NOACCEL,
    THAW_ACCEL,
};

enum thaw_alloc {
    THAW_NOALLOC,
    THAW_ALLOC,
};

#endif

/* vim: set et sts=4 sw=4 ts=4: */
