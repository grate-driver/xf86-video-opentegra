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

#include "driver.h"

#define HANDLE_INVALID  0

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__, __LINE__, \
               __func__, ##args)

static int format_planar(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return 1;
    default:
        break;
    }

    return 0;
}

static int fb_bpp(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return 8;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return 32;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return 16;
    default:
        break;
    }

    return 0;
}

static int fb_bpp_c(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return 8;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_height_aligned(uint32_t format, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return TEGRA_ROUND_UP(height, 16);
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return TEGRA_ROUND_UP(height, 4);
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return TEGRA_ROUND_UP(height, 8);
    default:
        break;
    }

    return 0;
}

static uint32_t fb_height_c_aligned(uint32_t format, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
        return TEGRA_ROUND_UP(height, 32) / 2;
    case DRM_FORMAT_YUV422:
        return TEGRA_ROUND_UP(height, 32);
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return TEGRA_ROUND_UP(height, 4);
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return TEGRA_ROUND_UP(height, 8);
    default:
        break;
    }

    return 0;
}

static uint32_t fb_pitch(uint32_t format, uint32_t width)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return width;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return width * 4;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return width * 2;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_pitch_c(uint32_t format, uint32_t width)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return width / 2;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_width_c(uint32_t format, uint32_t width)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return width / 2;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_height_c(uint32_t format, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
        return height / 2;
    case DRM_FORMAT_YUV422:
        return height;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_pitch_aligned(uint32_t format, uint32_t width)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return TEGRA_ROUND_UP(width, 16);
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return TEGRA_ROUND_UP(width * 4, 16);
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return TEGRA_ROUND_UP(width * 2, 16);
    default:
        break;
    }

    return 0;
}

static uint32_t fb_pitch_c_aligned(uint32_t format, uint32_t width)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return TEGRA_ROUND_UP(width, 32) / 2;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_size(uint32_t format, uint32_t width, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return fb_pitch(format, width) * height;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return fb_pitch(format, width) * height;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return fb_pitch(format, width) * height;
    default:
        break;
    }

    return 0;
}

static uint32_t fb_size_c(uint32_t format, uint32_t width, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
        return fb_pitch_c(format, width) * height / 2;
    case DRM_FORMAT_YUV422:
        return fb_pitch_c(format, width) * height;
    default:
        break;
    }

    return 0;
}

static uint32_t
fb_size_aligned(uint32_t format, uint32_t width, uint32_t height)
{
    return fb_pitch_aligned(format, width) *
           fb_height_aligned(format, height);
}

static uint32_t
fb_size_c_aligned(uint32_t format, uint32_t width, uint32_t height)
{
    return fb_pitch_c_aligned(format, width) *
           fb_height_c_aligned(format, height);
}

static drm_overlay_fb * drm_create_fb_internal(struct drm_tegra *drm,
                                               int drm_fd,
                                               uint32_t drm_format,
                                               uint32_t width, uint32_t height,
                                               uint32_t *bo_handles,
                                               uint32_t *pitches,
                                               uint32_t *offsets,
                                               Bool dont_map)
{
    struct drm_tegra_bo *bo    = NULL;
    struct drm_tegra_bo *bo_cb = NULL;
    struct drm_tegra_bo *bo_cr = NULL;
    drm_overlay_fb *fb         = NULL;
    uint32_t fb_id             = HANDLE_INVALID;
    Bool from_handle;
    int err;

    if (width == 0 || height == 0)
        return NULL;

    if (!pitches) {
        pitches = alloca(sizeof(uint32_t) * 4);

        pitches[0] = fb_pitch_aligned(drm_format, width);
        pitches[1] = fb_pitch_c_aligned(drm_format, width);
        pitches[2] = fb_pitch_c_aligned(drm_format, width);
        pitches[3] = 0;
    }

    if (!offsets) {
        offsets = alloca(sizeof(uint32_t) * 4);

        offsets[0] = 0;
        offsets[1] = 0;
        offsets[2] = 0;
        offsets[3] = 0;
    }

    from_handle = !!(bo_handles);

    if (from_handle) {
        err = drm_tegra_bo_wrap(&bo, drm, bo_handles[0], 0,
                                pitches[0] * height);
        if (err)
            goto error_cleanup;

        drm_tegra_bo_forbid_caching(bo);

        if (format_planar(drm_format)) {
            err = drm_tegra_bo_wrap(&bo_cb, drm, bo_handles[1], 0,
                                    pitches[1] * height / 2);
            if (err)
                goto error_cleanup;

            drm_tegra_bo_forbid_caching(bo_cb);

            err = drm_tegra_bo_wrap(&bo_cr, drm, bo_handles[2], 0,
                                    pitches[2] * height / 2);
            if (err)
                goto error_cleanup;

            drm_tegra_bo_forbid_caching(bo_cr);
        }

        goto create_framebuffer;
    }

    bo_handles = alloca(sizeof(uint32_t) * 4);

    bo_handles[1] = HANDLE_INVALID;
    bo_handles[2] = HANDLE_INVALID;
    bo_handles[3] = HANDLE_INVALID;

    /* Allocate PLANE[0] */
    err = drm_tegra_bo_new(&bo, drm, 0, offsets[0] +
                           fb_size_aligned(drm_format, width, height));
    if (err)
        goto error_cleanup;

    drm_tegra_bo_get_handle(bo, &bo_handles[0]);

    if (!format_planar(drm_format))
        goto create_framebuffer;

    /* Allocate PLANE[1] */
    err = drm_tegra_bo_new(&bo_cb, drm, 0, offsets[1] +
                           fb_size_c_aligned(drm_format, width, height));
    if (err)
        goto error_cleanup;

    drm_tegra_bo_get_handle(bo_cb, &bo_handles[1]);

    /* Allocate PLANE[2] */
    err = drm_tegra_bo_new(&bo_cr, drm, 0, offsets[2] +
                           fb_size_c_aligned(drm_format, width, height));
    if (err)
        goto error_cleanup;

    drm_tegra_bo_get_handle(bo_cr, &bo_handles[2]);

create_framebuffer:
    err = drmModeAddFB2(drm_fd, width, height, drm_format,
                        bo_handles, pitches, offsets, &fb_id, 0);
    if (err) {
        ErrorMsg("Failed to create DRM framebuffer: %s\n", strerror(-err));
        goto error_cleanup;
    }

    fb = calloc(1, sizeof(*fb));
    if (!fb)
        goto error_cleanup;

    fb->fb_id           = fb_id;
    fb->format          = drm_format;
    fb->width           = width;
    fb->height          = height;
    fb->width_c         = fb_width_c(drm_format, width);
    fb->height_c        = fb_height_c(drm_format, height);
    fb->bpp             = fb_bpp(drm_format);
    fb->bpp_c           = fb_bpp_c(drm_format);
    fb->bo_y_id         = bo_handles[0];
    fb->bo_cb_id        = bo_handles[1];
    fb->bo_cr_id        = bo_handles[2];
    fb->bo_y            = bo;
    fb->bo_cb           = bo_cb;
    fb->bo_cr           = bo_cr;
    fb->pitch_y         = pitches[0];
    fb->pitch_cb        = pitches[1];
    fb->pitch_cr        = pitches[2];
    fb->offset_y        = offsets[0];
    fb->offset_cb       = offsets[1];
    fb->offset_cr       = offsets[2];

    fb->width_pad       = (fb_pitch_aligned(drm_format, width) -
                            fb_pitch(drm_format, width)) *
                                fb_height_aligned(drm_format, height);

    fb->height_pad      = (fb_height_aligned(drm_format, height) - height) *
                            fb_pitch_aligned(drm_format, width);

    fb->height_offset   = (fb_height_aligned(drm_format, height) - height) *
                            fb->bpp / 8;

    fb->width_c_pad     = (fb_pitch_c_aligned(drm_format, width) -
                            fb_pitch_c(drm_format, width)) *
                                fb_height_c_aligned(drm_format, height);

    fb->height_c_pad    = (fb_height_c_aligned(drm_format, height) - fb->height_c) *
                            fb_pitch_c_aligned(drm_format, width);

    fb->height_c_offset = (fb_height_c_aligned(drm_format, height) - fb->height_c) *
                            fb->bpp_c / 8;

    if (dont_map)
        return fb;

    err = drm_tegra_bo_map(fb->bo_y, (void **)&fb->bo_y_mmap);
    if (err)
        goto error_cleanup;

    fb->bo_y_mmap += fb->offset_y;

    if (!format_planar(drm_format))
        return fb;

    err = drm_tegra_bo_map(fb->bo_cb, (void **)&fb->bo_cb_mmap);
    if (err)
        goto error_cleanup;

    err = drm_tegra_bo_map(fb->bo_cr, (void **)&fb->bo_cr_mmap);
    if (err)
        goto error_cleanup;

    fb->bo_cb_mmap += fb->offset_cb;
    fb->bo_cr_mmap += fb->offset_cr;

    return fb;

error_cleanup:
    if (from_handle)
        return NULL;

    if (fb_id != HANDLE_INVALID)
        drmModeRmFB(drm_fd, fb_id);

    drm_tegra_bo_unref(bo);
    drm_tegra_bo_unref(bo_cb);
    drm_tegra_bo_unref(bo_cr);

    free(fb);

    return NULL;
}

drm_overlay_fb * drm_create_fb(struct drm_tegra *drm, int drm_fd,
                               uint32_t drm_format, uint32_t width,
                               uint32_t height)
{
    return drm_create_fb_internal(drm, drm_fd, drm_format, width, height,
                                  NULL, NULL, NULL, FALSE);
}

drm_overlay_fb * drm_create_fb2(struct drm_tegra *drm, int drm_fd,
                                uint32_t drm_format, uint32_t width,
                                uint32_t height, uint32_t *offsets,
                                Bool dont_map)
{
    return drm_create_fb_internal(drm, drm_fd, drm_format, width, height,
                                  NULL, NULL, offsets, dont_map);
}

drm_overlay_fb * drm_create_fb_from_handle(struct drm_tegra *drm,
                                           int drm_fd,
                                           uint32_t drm_format,
                                           uint32_t width, uint32_t height,
                                           uint32_t *bo_handles,
                                           uint32_t *pitches,
                                           uint32_t *offsets)
{
    return drm_create_fb_internal(drm, drm_fd, drm_format, width, height,
                                  bo_handles, pitches, offsets, TRUE);
}

drm_overlay_fb * drm_clone_fb(int drm_fd, drm_overlay_fb *fb)
{
    uint32_t fb_id = HANDLE_INVALID;
    drm_overlay_fb *clone;
    uint32_t bo_handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    int err;

    clone = malloc(sizeof(*clone));
    if (!clone)
        return NULL;

    memcpy(clone, fb, sizeof(*fb));

    bo_handles[0] = clone->bo_y_id;
    bo_handles[1] = clone->bo_cb_id;
    bo_handles[2] = clone->bo_cr_id;
    bo_handles[3] = 0;

    pitches[0] = clone->pitch_y;
    pitches[1] = clone->pitch_cb;
    pitches[2] = clone->pitch_cr;
    pitches[3] = 0;

    offsets[0] = clone->offset_y;
    offsets[1] = clone->offset_cb;
    offsets[2] = clone->offset_cr;
    offsets[3] = 0;

    err = drmModeAddFB2(drm_fd, clone->width, clone->height, clone->format,
                        bo_handles, pitches, offsets, &fb_id, 0);
    if (err) {
        ErrorMsg("Failed to create DRM framebuffer: %s\n", strerror(-err));
        free(clone);
        return NULL;
    }

    clone->fb_id = fb_id;

    drm_tegra_bo_ref(clone->bo);
    drm_tegra_bo_ref(clone->bo_cb);
    drm_tegra_bo_ref(clone->bo_cr);

    return clone;
}

void drm_free_overlay_fb(int drm_fd, drm_overlay_fb *fb)
{
    int ret;

    if (fb == NULL)
        return;

    ret = drmModeRmFB(drm_fd, fb->fb_id);
    if (ret < 0)
        ErrorMsg("Failed to remove framebuffer %s\n", strerror(-ret));

    drm_tegra_bo_unref(fb->bo);
    drm_tegra_bo_unref(fb->bo_cb);
    drm_tegra_bo_unref(fb->bo_cr);

    free(fb);
}

static int drm_plane_type(int drm_fd, int plane_id)
{
    drmModeObjectPropertiesPtr props;
    drmModePropertyPtr prop;
    int plane_type = -EINVAL;
    int i;

    props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props)
        return -ENODEV;

    for (i = 0; i < props->count_props && plane_type == -EINVAL; i++) {
        prop = drmModeGetProperty(drm_fd, props->props[i]);
        if (prop) {
            if (strcmp(prop->name, "type") == 0)
                plane_type = props->prop_values[i];

            drmModeFreeProperty(prop);
        }
    }

    drmModeFreeObjectProperties(props);

    return plane_type;
}

int drm_get_overlay_plane(int drm_fd, int crtc_pipe, uint32_t format,
                          uint32_t *plane_id)
{
    drmModePlaneRes *res;
    drmModePlane *p;
    uint32_t id = 0;
    int i, j;

    if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
        ErrorMsg("Failed to set universal planes CAP\n");
        goto err;
    }

    res = drmModeGetPlaneResources(drm_fd);
    if (!res)
        goto err;

    for (i = 0; i < res->count_planes && !id; i++) {
        p = drmModeGetPlane(drm_fd, res->planes[i]);
        if (!p)
            continue;

        if (!p->crtc_id && (p->possible_crtcs & (1 << crtc_pipe))) {
            if (drm_plane_type(drm_fd, p->plane_id) == DRM_PLANE_TYPE_OVERLAY) {
                for (j = 0; j < p->count_formats; j++) {
                    if (p->formats[j] == format) {
                        id = p->plane_id;
                        break;
                    }
                }
            }
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(res);

    if (!id)
        goto err;

    *plane_id = id;

    return 0;

err:
    ErrorMsg("Failed to get overlay plane\n");

    return -EFAULT;
}

int drm_get_primary_plane(int drm_fd, int crtc_pipe, uint32_t *plane_id)
{
    drmModePlaneRes *res;
    drmModePlane *p;
    uint32_t id = 0;
    int i;

    if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
        ErrorMsg("Failed to set universal planes CAP\n");
        goto err;
    }

    res = drmModeGetPlaneResources(drm_fd);
    if (!res)
        goto err;

    for (i = 0; i < res->count_planes && !id; i++) {
        p = drmModeGetPlane(drm_fd, res->planes[i]);
        if (!p)
            continue;

        if (p->possible_crtcs & (1 << crtc_pipe)) {
            if (drm_plane_type(drm_fd, p->plane_id) == DRM_PLANE_TYPE_PRIMARY)
                id = p->plane_id;
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(res);

    if (!id)
        goto err;

    *plane_id = id;

    return 0;

err:
    ErrorMsg("Failed to get primary plane\n");

    return -EFAULT;
}

static void copy_plane_data(uint8_t *dst, uint8_t *src,
                            unsigned width, unsigned height,
                            unsigned pitch_dst,
                            unsigned pitch_src)
{
    unsigned i;

    if (pitch_dst == pitch_src)
        memcpy(dst, src, height * pitch_dst);
    else
        for (i = 0; i < height; i++, dst += pitch_dst, src += pitch_src)
            memcpy(dst, src, pitch_src);
}

void drm_copy_data_to_fb(drm_overlay_fb *fb, uint8_t *data, int swap)
{
    if (!format_planar(fb->format)) {
        copy_plane_data(fb->bo_mmap, data,
                        fb->width, fb->height,
                        fb->pitch, fb_pitch(fb->format, fb->width));
        return;
    }

    copy_plane_data(fb->bo_y_mmap, data,
                    fb->width, fb->height,
                    fb->pitch_y, fb_pitch(fb->format, fb->width));

    data += fb_size(fb->format, fb->width, fb->height);

    copy_plane_data(swap ? fb->bo_cb_mmap : fb->bo_cr_mmap, data,
                    fb_width_c(fb->format, fb->width),
                    fb_height_c(fb->format, fb->height),
                    swap ? fb->pitch_cb : fb->pitch_cr,
                    fb_pitch_c(fb->format, fb->width));

    data += fb_size_c(fb->format, fb->width, fb->height);

    copy_plane_data(swap ? fb->bo_cr_mmap : fb->bo_cb_mmap, data,
                    fb_width_c(fb->format, fb->width),
                    fb_height_c(fb->format, fb->height),
                    swap ? fb->pitch_cr : fb->pitch_cb,
                    fb_pitch_c(fb->format, fb->width));
}
