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

#ifndef OPENTEGRA_DRM_PLANE_H
#define OPENTEGRA_DRM_PLANE_H

typedef struct drm_overlay_fb {
    uint32_t fb_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t width_c;
    uint32_t height_c;
    unsigned width_pad;
    unsigned height_pad;
    unsigned width_c_pad;
    unsigned height_c_pad;
    unsigned height_offset;
    unsigned height_c_offset;
    union {
        unsigned bpp_y;
        unsigned bpp;
    };
    unsigned bpp_c;
    union {
        uint32_t bo_y_id;
        uint32_t bo_id;
    };
    union {
        uint8_t *bo_y_mmap;
        uint8_t *bo_mmap;
    };
    uint32_t bo_cb_id;
    uint32_t bo_cr_id;
    uint8_t *bo_cb_mmap;
    uint8_t *bo_cr_mmap;
    union {
        uint32_t pitch_y;
        uint32_t pitch;
    };
    uint32_t pitch_cb;
    uint32_t pitch_cr;
    union {
        struct drm_tegra_bo *bo_y;
        struct drm_tegra_bo *bo;
    };
    struct drm_tegra_bo *bo_cb;
    struct drm_tegra_bo *bo_cr;
    union {
        uint32_t offset;
        uint32_t offset_y;
    };
    uint32_t offset_cb;
    uint32_t offset_cr;
} drm_overlay_fb;

drm_overlay_fb * drm_create_fb(struct drm_tegra *drm, int drm_fd,
                               uint32_t drm_format, uint32_t width,
                               uint32_t height);

drm_overlay_fb * drm_create_fb2(struct drm_tegra *drm, int drm_fd,
                                uint32_t drm_format, uint32_t width,
                                uint32_t height, uint32_t *offsets,
                                Bool dont_map);

drm_overlay_fb * drm_create_fb_from_handle(struct drm_tegra *drm,
                                           int drm_fd,
                                           uint32_t drm_format,
                                           uint32_t width, uint32_t height,
                                           uint32_t *bo_handles,
                                           uint32_t *pitches,
                                           uint32_t *offsets);

drm_overlay_fb * drm_clone_fb(int drm_fd, drm_overlay_fb *fb);

void drm_free_overlay_fb(int drm_fd, drm_overlay_fb *fb);

int drm_get_overlay_plane(int drm_fd, int crtc_pipe, uint32_t format,
                          uint32_t *plane_id);

int drm_get_primary_plane(int drm_fd, int crtc_pipe, uint32_t *plane_id);

void drm_copy_data_to_fb(drm_overlay_fb *fb, uint8_t *data, int swap);

int drm_set_planes_rotation(int drm_fd, uint32_t crtc_mask, uint32_t mode);

#endif /* OPENTEGRA_DRM_PLANE_H */
