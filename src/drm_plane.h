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

#ifndef TEGRA_DRM_PANLE_H
#define TEGRA_DRM_PANLE_H

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

#endif /* TEGRA_DRM_PANLE_H */
