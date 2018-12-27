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

#define MUNMAP_VERBOSE(PTR, SIZE)                       \
    if (PTR && munmap(PTR, page_align(SIZE)) != 0)      \
        FatalError("%s: " #PTR " munmap failed: %s\n",  \
                   __FUNCTION__, strerror(errno));

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__, __LINE__, \
               __func__, ##args)

static size_t page_align(size_t size)
{
    int pagesize = getpagesize();
    return TEGRA_ALIGN(size, pagesize);
}

static void mmap_gem(int drm_fd, int gem_handle, void **map, size_t size)
{
    struct drm_tegra_gem_mmap gem_mmap = {
        .handle = gem_handle,
        .pad = 0,
        .offset = 0,
    };
    int ret;

    ret = ioctl(drm_fd, DRM_IOCTL_TEGRA_GEM_MMAP, &gem_mmap);
    if (ret) {
        ErrorMsg("Failed to get GEM mmap FD: %s\n", strerror(-ret));
        *map = MAP_FAILED;
        return;
    }

    size = page_align(size);

    *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                drm_fd, gem_mmap.offset);
    if (*map == MAP_FAILED)
        ErrorMsg("GEM mmap'ing failed: %s\n", strerror(errno));
}

static int fb_size(uint32_t format, uint32_t width, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV422:
        return width * height;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB8888:
        return width * height * 4;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
        return width * height * 2;
    default:
        break;
    }

    return 0;
}

static int fb_size_c(uint32_t format, uint32_t width, uint32_t height)
{
    switch (format) {
    case DRM_FORMAT_YUV420:
        return width * height / 4;
    case DRM_FORMAT_YUV422:
        return width * height / 2;
    default:
        break;
    }

    return 0;
}

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

static int fb_pitch(uint32_t format, uint32_t width)
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

static int fb_pitch_c(uint32_t format, uint32_t width)
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

static uint32_t create_gem(int drm_fd, uint32_t size)
{
    struct drm_tegra_gem_create gem = {
        .flags = 0,
    };
    int ret;

    gem.size = page_align(size);

    ret = ioctl(drm_fd, DRM_IOCTL_TEGRA_GEM_CREATE, &gem);

    if (ret) {
        ErrorMsg("Failed to create GEM[0]: %s\n", strerror(-ret));
        return HANDLE_INVALID;
    }

    return gem.handle;
}

static void close_gem(int drm_fd, uint32_t handle)
{
    struct drm_gem_close gem = {
        .handle = handle,
    };
    int ret;

    ret = ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem);
    if (ret)
        ErrorMsg("Failed to close GEM: %s\n", strerror(-ret));
}

static drm_overlay_fb * drm_create_fb_internal(int drm_fd, uint32_t drm_format,
                                               uint32_t width, uint32_t height,
                                               uint32_t *bo_handles,
                                               uint32_t *pitches,
                                               uint32_t *offsets)
{
    drm_overlay_fb *fb = NULL;
    uint32_t fb_id = HANDLE_INVALID;
    Bool from_handle;
    int ret;

    if (width == 0 || height == 0)
        return NULL;

    from_handle = !!(bo_handles);

    if (from_handle)
        goto create_framebuffer;

    bo_handles = alloca(sizeof(uint32_t) * 4);
    if (!bo_handles)
        return NULL;

    pitches = alloca(sizeof(uint32_t) * 4);
    if (!pitches)
        return NULL;

    offsets = alloca(sizeof(uint32_t) * 4);
    if (!offsets)
        return NULL;

    pitches[0] = fb_pitch(drm_format, width);
    pitches[1] = fb_pitch_c(drm_format, width);
    pitches[2] = fb_pitch_c(drm_format, width);
    pitches[3] = 0;

    offsets[0] = 0;
    offsets[1] = 0;
    offsets[2] = 0;
    offsets[3] = 0;

    bo_handles[1] = HANDLE_INVALID;
    bo_handles[2] = HANDLE_INVALID;
    bo_handles[3] = HANDLE_INVALID;

    /* Allocate PLANE[0] */
    bo_handles[0] = create_gem(drm_fd, fb_size(drm_format, width, height));

    if (bo_handles[0] == HANDLE_INVALID)
        goto error_cleanup;

    if (!format_planar(drm_format))
        goto create_framebuffer;

    /* Allocate PLANE[1] */
    bo_handles[1] = create_gem(drm_fd, fb_size_c(drm_format, width, height));

    if (bo_handles[1] == HANDLE_INVALID)
        goto error_cleanup;

    /* Allocate PLANE[2] */
    bo_handles[2] = create_gem(drm_fd, fb_size_c(drm_format, width, height));

    if (bo_handles[2] == HANDLE_INVALID)
        goto error_cleanup;

create_framebuffer:
    ret = drmModeAddFB2(drm_fd, width, height, drm_format,
                        bo_handles, pitches, offsets, &fb_id, 0);
    if (ret) {
        ErrorMsg("Failed to create DRM framebuffer: %s\n", strerror(-ret));
        goto error_cleanup;
    }

    fb = xnfcalloc(1, sizeof(*fb));

    fb->fb_id    = fb_id;
    fb->format   = drm_format;
    fb->width    = width;
    fb->height   = height;
    fb->bo_y_id  = bo_handles[0];
    fb->bo_cb_id = bo_handles[1];
    fb->bo_cr_id = bo_handles[2];

    if (from_handle)
        return fb;

    if (!format_planar(drm_format))
        goto non_planar;

    mmap_gem(drm_fd, fb->bo_y_id, &fb->bo_y_mmap,
             fb_size(drm_format, width, height));

    if (fb->bo_y_mmap == MAP_FAILED)
        goto error_cleanup;

    mmap_gem(drm_fd, fb->bo_cb_id, &fb->bo_cb_mmap,
             fb_size_c(drm_format, width, height));

    if (fb->bo_cb_mmap == MAP_FAILED)
        goto error_cleanup;

    mmap_gem(drm_fd, fb->bo_cr_id, &fb->bo_cr_mmap,
             fb_size_c(drm_format, width, height));

    if (fb->bo_cr_mmap == MAP_FAILED)
        goto error_cleanup;

    return fb;

non_planar:
    mmap_gem(drm_fd, fb->bo_id, &fb->bo_mmap,
             fb_size(drm_format, width, height));

    if (fb->bo_mmap == MAP_FAILED)
        goto error_cleanup;

    return fb;

error_cleanup:
    if (from_handle)
        return NULL;

    if (fb != NULL) {
        if (format_planar(drm_format)) {
            if (fb->bo_cr_mmap && fb->bo_cr_mmap != MAP_FAILED)
                MUNMAP_VERBOSE(fb->bo_cr_mmap,
                               fb_size_c(drm_format, width, height));

            if (fb->bo_cb_mmap && fb->bo_cb_mmap != MAP_FAILED)
                MUNMAP_VERBOSE(fb->bo_cb_mmap,
                               fb_size_c(drm_format, width, height));

            if (fb->bo_y_mmap && fb->bo_y_mmap != MAP_FAILED)
                MUNMAP_VERBOSE(fb->bo_y_mmap,
                               fb_size(drm_format, width, height));
        } else {
            if (fb->bo_mmap && fb->bo_mmap != MAP_FAILED)
                MUNMAP_VERBOSE(fb->bo_mmap,
                               fb_size(drm_format, width, height));
        }

        free(fb);
    }

    if (fb_id != HANDLE_INVALID)
        drmModeRmFB(drm_fd, fb_id);

    if (bo_handles[2] != HANDLE_INVALID)
        close_gem(drm_fd, bo_handles[2]);

    if (bo_handles[1] != HANDLE_INVALID)
        close_gem(drm_fd, bo_handles[1]);

    if (bo_handles[0] != HANDLE_INVALID)
        close_gem(drm_fd, bo_handles[0]);

    return NULL;
}

drm_overlay_fb * drm_create_fb(int drm_fd, uint32_t drm_format,
                               uint32_t width, uint32_t height)
{
    return drm_create_fb_internal(drm_fd, drm_format, width, height,
                                  NULL, NULL, NULL);
}

drm_overlay_fb * drm_create_fb_from_handle(int drm_fd, uint32_t drm_format,
                                           uint32_t width, uint32_t height,
                                           uint32_t *bo_handles,
                                           uint32_t *pitches,
                                           uint32_t *offsets)
{
    return drm_create_fb_internal(drm_fd, drm_format, width, height,
                                  bo_handles, pitches, offsets);
}

void drm_free_overlay_fb(int drm_fd, drm_overlay_fb *fb)
{
    int ret;

    if (fb == NULL)
        return;

    if (format_planar(fb->format)) {
        MUNMAP_VERBOSE(fb->bo_y_mmap,
                       fb_size(fb->format, fb->width, fb->height));
        MUNMAP_VERBOSE(fb->bo_cb_mmap,
                       fb_size_c(fb->format, fb->width, fb->height));
        MUNMAP_VERBOSE(fb->bo_cr_mmap,
                       fb_size_c(fb->format, fb->width, fb->height));

        close_gem(drm_fd, fb->bo_y_id);
        close_gem(drm_fd, fb->bo_cb_id);
        close_gem(drm_fd, fb->bo_cr_id);
    } else {
        MUNMAP_VERBOSE(fb->bo_mmap, fb_size(fb->format, fb->width, fb->height));

        close_gem(drm_fd, fb->bo_id);
    }

    ret = drmModeRmFB(drm_fd, fb->fb_id);
    if (ret < 0)
        ErrorMsg("Failed to remove framebuffer %s\n", strerror(-ret));

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
            if (drm_plane_type(drm_fd, p->plane_id) == DRM_PLANE_TYPE_PRIMARY) {
                id = p->plane_id;
                break;
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
    ErrorMsg("Failed to get primary plane\n");

    return -EFAULT;
}

void drm_copy_data_to_fb(drm_overlay_fb *fb, uint8_t *data, int swap)
{
    if (!format_planar(fb->format)) {
        memcpy(fb->bo_mmap, data, fb_size(fb->format, fb->width, fb->height));
        return;
    }

    memcpy(fb->bo_y_mmap, data, fb_size(fb->format, fb->width, fb->height));
    data += fb_size(fb->format, fb->width, fb->height);

    memcpy(swap ? fb->bo_cb_mmap : fb->bo_cr_mmap, data,
           fb_size_c(fb->format, fb->width, fb->height));
    data += fb_size_c(fb->format, fb->width, fb->height);

    memcpy(swap ? fb->bo_cr_mmap : fb->bo_cb_mmap, data,
           fb_size_c(fb->format, fb->width, fb->height));
}
