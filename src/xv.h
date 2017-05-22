/*
 * Copyright 2016-2017 Dmitry Osipenko <digetx@gmail.com>
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

#ifndef TEGRA_XV_H
#define TEGRA_XV_H

#include "xf86xv.h"

typedef struct drm_overlay_fb {
    uint32_t fb_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    union {
        uint32_t bo_y_id;
        uint32_t bo_id;
    };
    union {
        void *bo_y_mmap;
        void *bo_mmap;
    };
    uint32_t bo_cb_id;
    uint32_t bo_cr_id;
    void *bo_cb_mmap;
    void *bo_cr_mmap;
} drm_overlay_fb;

drm_overlay_fb * drm_create_fb(int drm_fd, uint32_t drm_format,
                               uint32_t width, uint32_t height);

void drm_free_overlay_fb(int drm_fd, drm_overlay_fb *fb);

int drm_get_overlay_plane(int drm_fd, int crtc_pipe, uint32_t format,
                          uint32_t *plane_id);

int drm_get_primary_plane(int drm_fd, int crtc_pipe, uint32_t *plane_id);

void drm_copy_data_to_fb(drm_overlay_fb *fb, uint8_t *data, int swap);

#define TEGRA_VIDEO_OVERLAY_MAX_WIDTH   4096
#define TEGRA_VIDEO_OVERLAY_MAX_HEIGHT  4096

#define FOURCC_PASSTHROUGH_YV12   (('1' << 24) + ('2' << 16) + ('V' << 8) + 'Y')
#define FOURCC_PASSTHROUGH_RGB565 (('1' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_RGB888 (('X' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_BGR888 (('X' << 24) + ('R' << 16) + ('G' << 8) + 'B')

#define XVMC_YV12                                   \
{                                                   \
    .id                 = FOURCC_PASSTHROUGH_YV12,  \
    .type               = XvYUV,                    \
    .byte_order         = LSBFirst,                 \
    .guid               = {'P', 'A', 'S', 'S', 'T', 'H', 'R', 'O', 'U', 'G', 'H', '_', 'Y', 'V', '1', '2'}, \
    .bits_per_pixel     = 12,                       \
    .format             = XvPlanar,                 \
    .num_planes         = 3,                        \
    /* for RGB formats only */                      \
    .depth              = 0,                        \
    .red_mask           = 0,                        \
    .green_mask         = 0,                        \
    .blue_mask          = 0,                        \
    /* for YUV formats only */                      \
    .y_sample_bits      = 8,                        \
    .u_sample_bits      = 8,                        \
    .v_sample_bits      = 8,                        \
    .horz_y_period      = 1,                        \
    .horz_u_period      = 2,                        \
    .horz_v_period      = 2,                        \
    .vert_y_period      = 1,                        \
    .vert_u_period      = 1,                        \
    .vert_v_period      = 2,                        \
    .component_order    = {'Y', 'V', 'U', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
    .scanline_order     = XvTopToBottom,            \
}

#define XVMC_RGB565                                 \
{                                                   \
    .id                 = FOURCC_PASSTHROUGH_RGB565,\
    .type               = XvRGB,                    \
    .byte_order         = LSBFirst,                 \
    .guid               = {'P', 'A', 'S', 'S', 'T', 'H', 'R', 'O', 'U', 'G', 'H', 'R', 'G', 'B', '1', '6'}, \
    .bits_per_pixel     = 16,                       \
    .format             = XvPacked,                 \
    .num_planes         = 1,                        \
    /* for RGB formats only */                      \
    .depth              = 16,                       \
    .red_mask           = 0x1f << 11,               \
    .green_mask         = 0x3f <<  5,               \
    .blue_mask          = 0x1f <<  0,               \
    /* for YUV formats only */                      \
    .y_sample_bits      = 0,                        \
    .u_sample_bits      = 0,                        \
    .v_sample_bits      = 0,                        \
    .horz_y_period      = 0,                        \
    .horz_u_period      = 0,                        \
    .horz_v_period      = 0,                        \
    .vert_y_period      = 0,                        \
    .vert_u_period      = 0,                        \
    .vert_v_period      = 0,                        \
    .component_order    = {'B', 'G', 'R', 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
    .scanline_order     = XvTopToBottom,            \
}

#define XVMC_RGB888                                 \
{                                                   \
    .id                 = FOURCC_PASSTHROUGH_RGB888,\
    .type               = XvRGB,                    \
    .byte_order         = LSBFirst,                 \
    .guid               = {'P', 'A', 'S', 'S', 'T', 'H', 'R', 'O', 'U', 'G', 'H', 'R', 'G', 'B', '2', '4'}, \
    .bits_per_pixel     = 32,                       \
    .format             = XvPacked,                 \
    .num_planes         = 1,                        \
    /* for RGB formats only */                      \
    .depth              = 24,                       \
    .red_mask           = 0xff << 16,               \
    .green_mask         = 0xff <<  8,               \
    .blue_mask          = 0xff <<  0,               \
    /* for YUV formats only */                      \
    .y_sample_bits      = 0,                        \
    .u_sample_bits      = 0,                        \
    .v_sample_bits      = 0,                        \
    .horz_y_period      = 0,                        \
    .horz_u_period      = 0,                        \
    .horz_v_period      = 0,                        \
    .vert_y_period      = 0,                        \
    .vert_u_period      = 0,                        \
    .vert_v_period      = 0,                        \
    .component_order    = {'B', 'G', 'R', 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
    .scanline_order     = XvTopToBottom,            \
}

#define XVMC_BGR888                                 \
{                                                   \
    .id                 = FOURCC_PASSTHROUGH_BGR888,\
    .type               = XvRGB,                    \
    .byte_order         = LSBFirst,                 \
    .guid               = {'P', 'A', 'S', 'S', 'T', 'H', 'R', 'O', 'U', 'G', 'H', 'B', 'G', 'R', '2', '4'}, \
    .bits_per_pixel     = 32,                       \
    .format             = XvPacked,                 \
    .num_planes         = 1,                        \
    /* for RGB formats only */                      \
    .depth              = 24,                       \
    .red_mask           = 0xff <<  0,               \
    .green_mask         = 0xff <<  8,               \
    .blue_mask          = 0xff << 16,               \
    /* for YUV formats only */                      \
    .y_sample_bits      = 0,                        \
    .u_sample_bits      = 0,                        \
    .v_sample_bits      = 0,                        \
    .horz_y_period      = 0,                        \
    .horz_u_period      = 0,                        \
    .horz_v_period      = 0,                        \
    .vert_y_period      = 0,                        \
    .vert_u_period      = 0,                        \
    .vert_v_period      = 0,                        \
    .component_order    = {'R', 'G', 'B', 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
    .scanline_order     = XvTopToBottom,            \
}

#endif /* TEGRA_XV_H */
