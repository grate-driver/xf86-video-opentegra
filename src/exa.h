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

typedef struct tegra_attrib_bo {
    struct tegra_attrib_bo *next;
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
    TegraEXAAttribBo *attribs;
    PictTransform transform;
    Bool attribs_alloc_err;
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
    CreatePictureProcPtr CreatePicture;
    DestroyPictureProcPtr DestroyPicture;
    ScreenBlockHandlerProcPtr BlockHandler;
#ifdef HAVE_JPEG
    tjhandle jpegCompressor;
    tjhandle jpegDecompressor;
#endif

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

typedef struct {
    Bool scanout_rotated : 1;   /* pixmap backs rotated frontbuffer BO */
    Bool no_compress : 1;       /* pixmap's data compress poorly */
    Bool accelerated : 1;       /* pixmap was accelerated at least once */
    Bool scanout : 1;           /* pixmap backs frontbuffer BO */
    Bool frozen : 1;            /* pixmap's data compressed */
    Bool accel : 1;             /* pixmap acceleratable */
    Bool cold : 1;              /* pixmap scheduled for compression */
    Bool dri : 1;               /* pixmap's BO was exported */

    unsigned crtc : 2;          /* pixmap's CRTC ID (for display rotation) */

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

            time_t last_use : 16; /* 8 seconds per unit */
            struct xorg_list fridge_entry;
        };

        struct {
            void *compressed_data;
            unsigned compressed_size;
            unsigned compression_type;
            unsigned picture_format;
        };
    };

    PixmapPtr pPixmap;
    PicturePtr pPicture;
} TegraPixmapRec, *TegraPixmapPtr;

unsigned int TegraEXAPitch(unsigned int width, unsigned int height,
                           unsigned int bpp);

void TegraEXAWaitFence(struct tegra_fence *fence);

unsigned TegraPixmapSize(TegraPixmapPtr pixmap);

unsigned TegraEXAHeightHwAligned(unsigned int height, unsigned int bpp);

unsigned long TegraEXAPixmapOffset(PixmapPtr pix);

struct drm_tegra_bo * TegraEXAPixmapBO(PixmapPtr pix);

Bool TegraEXAPrepareSolid(PixmapPtr pPixmap, int op, Pixel planemask,
                          Pixel color);

void TegraEXASolid(PixmapPtr pPixmap, int px1, int py1, int px2, int py2);

void TegraEXADoneSolid(PixmapPtr pPixmap);

Bool TegraEXAPrepareCopyExt(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                            int op, Pixel planemask);

Bool TegraEXAPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                         int dx, int dy, int op, Pixel planemask);

void TegraEXACopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                  int dstY, int width, int height);

void TegraEXACopyExt(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX,
                     int dstY, int width, int height);

void TegraEXADoneCopy(PixmapPtr pDstPixmap);

void TegraCompositeReleaseAttribBuffers(TegraEXAScratchPtr scratch);

Bool TegraEXACheckComposite(int op, PicturePtr pSrcPicture,
                            PicturePtr pMaskPicture,
                            PicturePtr pDstPicture);

Bool TegraEXAPrepareComposite(int op, PicturePtr pSrcPicture,
                              PicturePtr pMaskPicture,
                              PicturePtr pDstPicture,
                              PixmapPtr pSrc,
                              PixmapPtr pMask,
                              PixmapPtr pDst);

void TegraEXAComposite(PixmapPtr pDst,
                       int srcX, int srcY,
                       int maskX, int maskY,
                       int dstX, int dstY,
                       int width, int height);

void TegraEXADoneComposite(PixmapPtr pDst);

Bool TegraEXACheckComposite2D(int op, PicturePtr pSrcPicture,
                              PicturePtr pMaskPicture,
                              PicturePtr pDstPicture);

Bool TegraEXACheckComposite3D(int op, PicturePtr pSrcPicture,
                              PicturePtr pMaskPicture,
                              PicturePtr pDstPicture);

Bool TegraEXAPrepareComposite2D(int op,
                                PicturePtr pSrcPicture,
                                PicturePtr pMaskPicture,
                                PicturePtr pDstPicture,
                                PixmapPtr pSrc,
                                PixmapPtr pMask,
                                PixmapPtr pDst);

Bool TegraEXAPrepareComposite3D(int op,
                                PicturePtr pSrcPicture,
                                PicturePtr pMaskPicture,
                                PicturePtr pDstPicture,
                                PixmapPtr pSrc,
                                PixmapPtr pMask,
                                PixmapPtr pDst);

void TegraEXAComposite3D(PixmapPtr pDst,
                         int srcX, int srcY,
                         int maskX, int maskY,
                         int dstX, int dstY,
                         int width, int height);

void TegraEXADoneComposite3D(PixmapPtr pDst);

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

#endif

/* vim: set et sts=4 sw=4 ts=4: */
