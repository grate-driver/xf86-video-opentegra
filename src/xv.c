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

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(scrn->scrnIndex, X_ERROR, "%s:%d/%s(): " fmt, __FILE__, \
               __LINE__, __func__, ##args)

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

#define DEFAULT_COLOR_KEY 0xFF4AF6

static Atom xvColorKey;
static Atom xvVdpauInfo;

typedef union TegraXvVdpauInfo {
    struct {
        unsigned int visible : 1;
        unsigned int crtc_pipe : 1;
    };

    uint32_t data;
} TegraXvVdpauInfo;

typedef struct TegraOverlay {
    drm_overlay_fb *fb;

    drm_overlay_fb *old_fb_rotated;
    drm_overlay_fb *fb_rotated;
    Rotation rotation;

    uint32_t primary_plane_id;
    uint32_t plane_id;
    Bool visible;
    int crtc_id;

    int src_x;
    int src_y;
    int src_w;
    int src_h;

    int dst_x;
    int dst_y;
    int dst_w;
    int dst_h;

    uint32_t crtc_id_prop_id;
    uint32_t fb_id_prop_id;
    uint32_t src_x_prop_id;
    uint32_t src_y_prop_id;
    uint32_t src_w_prop_id;
    uint32_t src_h_prop_id;
    uint32_t crtc_x_prop_id;
    uint32_t crtc_y_prop_id;
    uint32_t crtc_w_prop_id;
    uint32_t crtc_h_prop_id;

    uint32_t zpos_prop_id;
    uint32_t ckey_mode_prop_id;

    uint32_t primary_ckey_min_prop_id;
    uint32_t primary_ckey_max_prop_id;
    uint32_t primary_zpos_prop_id;
    uint32_t primary_ckey_mode_prop_id;
    uint32_t primary_ckey_plane_mask_prop_id;
    uint32_t primary_ckey_mask_prop_id;

    uint32_t color_key;
    Bool ckey_enb;
} TegraOverlay, *TegraOverlayPtr;

typedef struct TegraVideo {
    TegraOverlay overlay[2];
    drm_overlay_fb *old_fb;
    drm_overlay_fb *fb;

    struct drm_tegra_channel *gr2d;
    struct tegra_stream cmds;

    uint8_t passthrough_data[PASSTHROUGH_DATA_SIZE_V2];
    int passthrough;

    unsigned int overlays_num;
    unsigned int best_overlay_id;

    uint32_t color_key;
    Bool ckey_probed;
} TegraVideo, *TegraVideoPtr;

typedef struct TegraXvAdaptor {
    XF86VideoAdaptorRec xv;
    DevUnion dev_union;
    TegraVideo private;
} TegraXvAdaptor, *TegraXvAdaptorPtr;

static XF86ImageRec XvImages[] = {
    XVIMAGE_YUY2,
    XVIMAGE_YV12,
    XVIMAGE_I420,
    XVIMAGE_UYVY,
    XVMC_YV12,
    XVMC_RGB565,
    XVMC_XRGB8888,
    XVMC_XBGR8888,
    XVMC_YV12_V2,
    XVMC_RGB565_V2,
    XVMC_XRGB8888_V2,
    XVMC_XBGR8888_V2,
};

static XF86VideoFormatRec XvFormats[] = {
    {
        .depth = 16,
        .class = TrueColor,
    },
    {
        .depth = 24,
        .class = TrueColor,
    },
};

static XF86AttributeRec XvAttributes[] = {
    {
        .flags      = XvSettable | XvGettable,
        .min_value  = 0,
        .max_value  = 0xFFFFFF,
        .name       = (char *)"XV_COLORKEY",
    },
    {
        .flags      = XvGettable,
        .min_value  = 0,
        .max_value  = 0xFFFFFFFF,
        .name       = (char *)"XV_TEGRA_VDPAU_INFO",
    },
};

static XF86VideoEncodingRec XvEncoding[] = {
    {
        .id               = 0,
        .name             = "XV_IMAGE",
        .width            = TEGRA_VIDEO_OVERLAY_MAX_WIDTH,
        .height           = TEGRA_VIDEO_OVERLAY_MAX_HEIGHT,
        .rate.numerator   = 1,
        .rate.denominator = 1,
    },
};

static Bool xv_fourcc_valid(int format_id)
{
    switch (format_id) {
    case FOURCC_YUY2:
    case FOURCC_YV12:
    case FOURCC_I420:
    case FOURCC_UYVY:
        return TRUE;

    case FOURCC_PASSTHROUGH_YV12:
    case FOURCC_PASSTHROUGH_RGB565:
    case FOURCC_PASSTHROUGH_XRGB8888:
    case FOURCC_PASSTHROUGH_XBGR8888:
        return TRUE;

    case FOURCC_PASSTHROUGH_YV12_V2:
    case FOURCC_PASSTHROUGH_RGB565_V2:
    case FOURCC_PASSTHROUGH_XRGB8888_V2:
    case FOURCC_PASSTHROUGH_XBGR8888_V2:
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

static uint32_t xv_fourcc_to_drm(int format_id)
{
    switch (format_id) {
    case FOURCC_YUY2:
        return DRM_FORMAT_YUYV;

    case FOURCC_YV12:
        return DRM_FORMAT_YUV420;

    case FOURCC_I420:
        return DRM_FORMAT_YUV420;

    case FOURCC_UYVY:
        return DRM_FORMAT_UYVY;

    case FOURCC_PASSTHROUGH_YV12:
    case FOURCC_PASSTHROUGH_YV12_V2:
        return DRM_FORMAT_YUV420;

    case FOURCC_PASSTHROUGH_RGB565:
    case FOURCC_PASSTHROUGH_RGB565_V2:
        return DRM_FORMAT_RGB565;

    case FOURCC_PASSTHROUGH_XRGB8888:
    case FOURCC_PASSTHROUGH_XRGB8888_V2:
        return DRM_FORMAT_XRGB8888;

    case FOURCC_PASSTHROUGH_XBGR8888:
    case FOURCC_PASSTHROUGH_XBGR8888_V2:
        return DRM_FORMAT_XBGR8888;

    default:
        FatalError("%s: Shouldn't be here; format_id %d\n",
                   __FUNCTION__, format_id);
        break;
    }

    return 0xFFFFFFFF;
}

static Bool TegraVideoOpenGPU(TegraVideoPtr priv, ScrnInfoPtr scrn)
{
    TegraPtr tegra = TegraPTR(scrn);
    int err;

    if (!priv->gr2d) {
        err = drm_tegra_channel_open(&priv->gr2d, tegra->drm, DRM_TEGRA_GR2D);
        if (err) {
            ErrorMsg("failed to open 2D channel: %d\n", err);
            priv->gr2d = NULL;
            return FALSE;
        }

        err = tegra_stream_create(&priv->cmds);
        if (err) {
            ErrorMsg("failed to create command stream: %d\n", err);
            drm_tegra_channel_close(priv->gr2d);
            priv->gr2d = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

static void TegraVideoCloseGPU(TegraVideoPtr priv)
{
    if (priv->gr2d) {
        tegra_stream_destroy(&priv->cmds);
        drm_tegra_channel_close(priv->gr2d);
        priv->gr2d = NULL;
    }
}

static int TegraDrmModeAtomicCommit(int fd,
                                    drmModeAtomicReqPtr req,
                                    uint32_t flags,
                                    void *user_data)
{
    int retries = 300;
    int err;

    goto commit;

    do {
        usleep(300);
commit:
        err = drmModeAtomicCommit(fd, req, flags, user_data);

    } while (err == -EBUSY && (flags & DRM_MODE_ATOMIC_NONBLOCK) && retries--);

    return err;
}

static Bool TegraVideoOverlayShowAtomic(TegraVideoPtr priv,
                                        ScrnInfoPtr scrn,
                                        drmModeAtomicReqPtr req,
                                        int overlay_id,
                                        int src_x, int src_y,
                                        int src_w, int src_h,
                                        int dst_x, int dst_y,
                                        int dst_w, int dst_h)
{
    TegraOverlayPtr overlay = &priv->overlay[overlay_id];
    drm_overlay_fb *fb;
    int ret = 0;

    if (overlay->fb_rotated)
        fb = overlay->fb_rotated;
    else
        fb = overlay->fb;

    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->crtc_id_prop_id,
                                       overlay->crtc_id);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->fb_id_prop_id,
                                       fb->fb_id);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->src_x_prop_id,
                                       src_x << 16);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->src_y_prop_id,
                                       src_y << 16);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->src_w_prop_id,
                                       src_w << 16);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->src_h_prop_id,
                                       src_h << 16);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->crtc_x_prop_id,
                                       dst_x);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->crtc_y_prop_id,
                                       dst_y);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->crtc_w_prop_id,
                                       dst_w);
    if (ret >= 0)
        ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                       overlay->crtc_h_prop_id,
                                       dst_h);

    if (ret < 0) {
        ErrorMsg("drmModeAtomicAddProperty failed: %d (%s)\n",
                 ret, strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

static Bool TegraVideoOverlaySetPlaneColorKey(TegraVideoPtr priv,
                                              ScrnInfoPtr scrn,
                                              drmModeAtomicReqPtr req,
                                              int overlay_id,
                                              uint64_t color_key,
                                              Bool enable,
                                              Bool init)
{
    TegraOverlayPtr overlay = &priv->overlay[overlay_id];
    uint64_t ckey = 0;
    int ret = 0;

    if (overlay->ckey_enb == enable &&
            overlay->color_key == color_key &&
                !init)
        return TRUE;

    /* swizzle BGR888 to RGB16161616 */
    ckey |= (color_key & 0x000000ff) << 40;
    ckey |= (color_key & 0x0000ff00) << 16;
    ckey |= (color_key & 0x00ff0000) >> 8;

    if (overlay->ckey_enb != enable || init) {
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                           overlay->zpos_prop_id,
                                           enable ? 0 : 1);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->plane_id,
                                           overlay->ckey_mode_prop_id,
                                           0);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_ckey_mode_prop_id,
                                           enable ? 1 : 0);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_zpos_prop_id,
                                           enable ? 1 : 0);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_ckey_plane_mask_prop_id,
                                           1 << overlay->primary_plane_id);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_ckey_mask_prop_id,
                                           0xFFFFFFFFFFFFFFFF);
    }

    if (overlay->color_key != color_key || init) {
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_ckey_min_prop_id,
                                           ckey);
        if (ret >= 0)
            ret = drmModeAtomicAddProperty(req, overlay->primary_plane_id,
                                           overlay->primary_ckey_max_prop_id,
                                           ckey);
    }

    if (ret < 0) {
        ErrorMsg("drmModeAtomicAddProperty failed: %d (%s)\n",
                 ret, strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

static void TegraVideoDestroyFramebuffer(ScrnInfoPtr scrn,
                                         drm_overlay_fb **fb)
{
    TegraPtr tegra = TegraPTR(scrn);

    if (*fb) {
        drm_free_overlay_fb(tegra->fd, *fb);
        *fb = NULL;
    }
}

static void TegraVideoOverlayClose(TegraVideoPtr priv, ScrnInfoPtr scrn,
                                   int id)
{
    TegraOverlayPtr overlay = &priv->overlay[id];
    TegraPtr tegra          = TegraPTR(scrn);
    int ret;

    ret = drmModeSetPlane(tegra->fd, overlay->plane_id, id,
                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (ret < 0)
        ErrorMsg("Failed to close overlay\n");

    overlay->visible = FALSE;
    overlay->fb      = NULL;

    TegraVideoDestroyFramebuffer(scrn, &overlay->fb_rotated);
    TegraVideoDestroyFramebuffer(scrn, &overlay->old_fb_rotated);
}

static Bool TegraCloseBo(ScrnInfoPtr scrn, uint32_t handle)
{
    TegraPtr tegra = TegraPTR(scrn);
    struct drm_gem_close args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.handle = handle;

    ret = drmIoctl(tegra->fd, DRM_IOCTL_GEM_CLOSE, &args);
    if (ret < 0) {
            ErrorMsg("Failed to close GEM %s\n", strerror(-ret));
            return ret;
    }

    return TRUE;
}

static Bool TegraImportBo(ScrnInfoPtr scrn, uint32_t flink, uint32_t *handle)
{
    TegraPtr tegra = TegraPTR(scrn);
    struct drm_gem_open args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.name = flink;

    ret = drmIoctl(tegra->fd, DRM_IOCTL_GEM_OPEN, &args);
    if (ret < 0) {
            ErrorMsg("Failed to open GEM by name %s\n", strerror(-ret));
            return ret;
    }

    *handle = args.handle;

    return TRUE;
}

static Bool TegraVideoOverlayCreateFB(TegraVideoPtr priv, ScrnInfoPtr scrn,
                                      uint32_t drm_format,
                                      uint32_t width, uint32_t height,
                                      int passthrough,
                                      void *passthrough_data)
{
    TegraPtr tegra                   = TegraPTR(scrn);
    TegraXvPassthroughDataV1 *pdata1 = passthrough_data;
    TegraXvPassthroughDataV2 *pdata2 = passthrough_data;
    uint32_t *flinks                 = NULL;
    uint32_t *pitches                = NULL;
    uint32_t *offsets                = NULL;
    unsigned int data_size           = 0;
    uint32_t bo_handles[3];
    drm_overlay_fb *fb;
    int i = 0, k = 0;

    switch (passthrough) {
    case 1:
        data_size = PASSTHROUGH_DATA_SIZE;
        flinks    = pdata1->flinks;
        pitches   = pdata1->pitches;
        offsets   = pdata1->offsets;
        break;

    case 2:
        data_size = PASSTHROUGH_DATA_SIZE_V2;
        flinks    = pdata2->flinks;
        pitches   = pdata2->pitches;
        offsets   = pdata2->offsets;
        break;

    default:
        break;
    }

    if (priv->fb &&
        priv->fb->format  == drm_format &&
        priv->fb->width   == width &&
        priv->fb->height  == height &&
        priv->passthrough == passthrough)
    {
        if (passthrough && priv->passthrough == passthrough &&
            memcmp(passthrough_data, priv->passthrough_data, data_size) == 0)
            return TRUE;
    }

    if (passthrough) {
        switch (drm_format) {
        case DRM_FORMAT_YUV420:
            k = 3;
            break;
        default:
            k = 1;
        }

        for (i = 0; i < k; i++) {
            if (!TegraImportBo(scrn, flinks[i], &bo_handles[i]))
                goto fail;
        }

        fb = drm_create_fb_from_handle(tegra->drm, tegra->fd, drm_format,
                                       width, height, bo_handles,
                                       pitches, offsets);
    } else {
        fb = drm_create_fb(tegra->drm, tegra->fd, drm_format, width, height);
    }

    if (fb == NULL) {
        goto fail;
    }

    if (passthrough) {
        memcpy(priv->passthrough_data, passthrough_data, data_size);
    }

    priv->old_fb      = priv->fb;
    priv->fb          = fb;
    priv->passthrough = passthrough;

    return TRUE;

fail:
    ErrorMsg("Failed to create framebuffer\n");

    for (k = 0; k < i; k++) {
        TegraCloseBo(scrn, bo_handles[k]);
    }

    return FALSE;
}

static void TegraVideoOverlayBestSize(ScrnInfoPtr scrn, Bool motion,
                                      short vid_w, short vid_h,
                                      short dst_w, short dst_h,
                                      unsigned int *actual_w,
                                      unsigned int *actual_h,
                                      void *data)
{
    *actual_w = dst_w;
    *actual_h = dst_h;
}

static Bool TegraVideoOverlaySetColorKey(TegraVideoPtr priv,
                                         ScrnInfoPtr scrn,
                                         uint64_t color_key,
                                         Bool enable,
                                         Bool init)
{
    TegraPtr tegra = TegraPTR(scrn);
    drmModeAtomicReqPtr req;
    int err;
    int id;

    req = drmModeAtomicAlloc();
    if (!req) {
        ErrorMsg("drmModeAtomicAlloc() failed\n");
        return FALSE;
    }

    for (id = 0; id < priv->overlays_num; id++) {
        if (!TegraVideoOverlaySetPlaneColorKey(priv, scrn, req, id,
                                               color_key, enable, init))
        {
            drmModeAtomicFree(req);
            return FALSE;
        }
    }

    err = TegraDrmModeAtomicCommit(tegra->fd, req, DRM_MODE_ATOMIC_NONBLOCK,
                                   NULL);
    drmModeAtomicFree(req);

    if (err < 0) {
        ErrorMsg("TegraDrmModeAtomicCommit failed: %d (%s)\n",
                 err, strerror(-err));
        return FALSE;
    }

    for (id = 0; id < priv->overlays_num; id++) {
        priv->overlay[id].color_key = color_key;
        priv->overlay[id].ckey_enb = enable;
    }

    return TRUE;
}

static int TegraVideoOverlaySetAttribute(ScrnInfoPtr scrn, Atom attribute,
                                         INT32 value, void *data)
{
    TegraVideoPtr priv = data;

    if (attribute == xvColorKey) {
        if (!TegraVideoOverlaySetColorKey(priv, scrn, value, TRUE, FALSE))
            return BadImplementation;

        priv->color_key = value;
        priv->ckey_probed = TRUE;

        return Success;
    }

    return BadMatch;
}

static int TegraVideoOverlayGetAttribute(ScrnInfoPtr scrn, Atom attribute,
                                         INT32 *value, void *data)
{
    TegraVideoPtr priv = data;
    TegraXvVdpauInfo vdpau_info;
    TegraOverlayPtr overlay;
    int id;

    if (attribute == xvColorKey) {
        *value = priv->color_key;

        if (priv->ckey_probed)
            return Success;

        if (!TegraVideoOverlaySetColorKey(priv, scrn, priv->color_key,
                                          TRUE, FALSE))
            return BadImplementation;

        priv->ckey_probed = TRUE;

        return Success;
    }

    if (attribute == xvVdpauInfo) {
        vdpau_info.crtc_pipe = priv->best_overlay_id;
        vdpau_info.visible = 0;

        for (id = 0; id < priv->overlays_num; id++) {
            overlay = &priv->overlay[id];

            vdpau_info.visible |= overlay->visible;
        }

        *value = vdpau_info.data;

        return Success;
    }

    return BadMatch;
}

static void TegraVideoOverlayStop(ScrnInfoPtr scrn, void *data, Bool cleanup)
{
    TegraVideoPtr priv = data;
    int id;

    for (id = 0; id < priv->overlays_num; id++)
        TegraVideoOverlayClose(priv, scrn, id);

    if (cleanup) {
        TegraVideoDestroyFramebuffer(scrn, &priv->fb);

        TegraVideoCloseGPU(priv);

        TegraVideoOverlaySetColorKey(priv, scrn, DEFAULT_COLOR_KEY,
                                     FALSE, TRUE);
    }
}

static Bool TegraVideoOverlayInitialize(TegraVideoPtr priv, ScrnInfoPtr scrn,
                                        int overlay_id)
{
    TegraOverlayPtr overlay = &priv->overlay[overlay_id];
    TegraPtr tegra          = TegraPTR(scrn);
    drmModeResPtr res;
    uint32_t primary_plane_id;
    uint32_t plane_id;
    Bool success = TRUE;
    int err;

    res = drmModeGetResources(tegra->fd);
    if (!res) {
        ErrorMsg("drmModeGetResources failed\n");
        success = FALSE;
        goto end;
    }

    if (overlay_id > res->count_crtcs) {
        ErrorMsg("Invalid overlay_id %u:%u\n", overlay_id, res->count_crtcs);
        success = FALSE;
        goto end;
    }

    err = drm_get_overlay_plane(tegra->fd, overlay_id,
                                DRM_FORMAT_YUV420, &plane_id);
    if (err) {
        success = FALSE;
        goto end;
    }

    err = drm_get_primary_plane(tegra->fd, overlay_id, &primary_plane_id);
    if (err) {
        success = FALSE;
        goto end;
    }

    overlay->crtc_id           = res->crtcs[overlay_id];
    overlay->plane_id          = plane_id;
    overlay->primary_plane_id  = primary_plane_id;

end:
    free(res);

    if (!success) {
        ErrorMsg("failed to initialize overlay %d\n", overlay_id);
    }

    return success;
}

static Bool TegraVideoCopyRotatedPlane(TegraVideoPtr priv,
                                       struct drm_tegra_bo *dst_bo,
                                       struct drm_tegra_bo *src_bo,
                                       unsigned width, unsigned height,
                                       unsigned pitch_dst, unsigned pitch_src,
                                       uint32_t offset_dst, uint32_t offset_src,
                                       unsigned bpp, Rotation rotation)
{
    struct tegra_fence *fence;
    unsigned src_height;
    unsigned src_width;
    unsigned fr_type;
    int err;

    if (!dst_bo && !src_bo)
        return TRUE;

    if (!dst_bo || !src_bo)
        return FALSE;

    switch (rotation) {
    case RR_Rotate_90:
        fr_type = TEGRA2D_ROT_90;
        src_width = height - 1;
        src_height = width - 1;
        break;

    case RR_Rotate_180:
        fr_type = TEGRA2D_ROT_180;
        src_width = width - 1;
        src_height = height - 1;
        break;

    case RR_Rotate_270:
        fr_type = TEGRA2D_ROT_270;
        src_width = height - 1;
        src_height = width - 1;
        break;

    default:
        return FALSE;
    }

    err = tegra_stream_begin(&priv->cmds, priv->gr2d);
    if (err)
        return FALSE;

    tegra_stream_prep(&priv->cmds, 16);

    tegra_stream_push_setclass(&priv->cmds, HOST1X_CLASS_GR2D);

    tegra_stream_push(&priv->cmds, HOST1X_OPCODE_MASK(0x9, 0x9));
    tegra_stream_push(&priv->cmds, 0x00000046); /* trigger 0 */
    tegra_stream_push(&priv->cmds, 0x00000000); /* cmdsel */

    tegra_stream_push(&priv->cmds, HOST1X_OPCODE_MASK(0x01e, 0x3));
    tegra_stream_push(&priv->cmds, /* controlsecond*/
                      (fr_type << 26) | (1 << 24));
    tegra_stream_push(&priv->cmds, /* controlmain */
                      (1 << 29) | (1 << 20) | ((bpp >> 4) << 16));

    tegra_stream_push(&priv->cmds, HOST1X_OPCODE_MASK(0x2b, 0x1149));
    tegra_stream_push_reloc(&priv->cmds, dst_bo, offset_dst);
    tegra_stream_push(&priv->cmds, pitch_dst);
    tegra_stream_push_reloc(&priv->cmds, src_bo, offset_src);
    tegra_stream_push(&priv->cmds, pitch_src);
    tegra_stream_push(&priv->cmds, src_height << 16 | src_width);

    tegra_stream_push(&priv->cmds, HOST1X_OPCODE_NONINCR(0x046, 1));
    tegra_stream_push(&priv->cmds, 0x00000000); /* tilemode */

    tegra_stream_sync(&priv->cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE);

    tegra_stream_end(&priv->cmds);

    fence = tegra_stream_submit(&priv->cmds, true);
    if (!fence)
        return FALSE;

    return TRUE;
}

static Bool TegraVideoOverlayUpdateRotatedFb(TegraVideoPtr priv,
                                             ScrnInfoPtr scrn,
                                             TegraOverlayPtr sibling,
                                             TegraOverlayPtr overlay,
                                             Rotation rotation)
{
    TegraPtr tegra = TegraPTR(scrn);
    drm_overlay_fb *fb_rotated;
    uint32_t offsets[4];
    int width, height;

    switch (rotation) {
    case RR_Rotate_0:
        fb_rotated = NULL;
        goto done;

    case RR_Rotate_90:
        width  = priv->fb->height;
        height = priv->fb->width;

        offsets[0] = priv->fb->width_pad;
        offsets[1] = priv->fb->width_c_pad;
        offsets[2] = priv->fb->width_c_pad;
        offsets[3] = 0;
        break;

    case RR_Rotate_180:
        width  = priv->fb->width;
        height = priv->fb->height;

        offsets[0] = priv->fb->height_pad;
        offsets[1] = priv->fb->height_c_pad;
        offsets[2] = priv->fb->height_c_pad;
        offsets[3] = 0;
        break;

    case RR_Rotate_270:
        width  = priv->fb->height;
        height = priv->fb->width;

        offsets[0] = priv->fb->height_offset;
        offsets[1] = priv->fb->height_c_offset;
        offsets[2] = priv->fb->height_c_offset;
        offsets[3] = 0;
        break;

    default:
        return FALSE;
    }

    if (sibling->fb_rotated &&
        sibling->fb == priv->fb &&
        sibling->rotation == rotation)
    {
        fb_rotated = drm_clone_fb(tegra->fd, sibling->fb_rotated);
        if (!fb_rotated)
            return FALSE;

        goto done;
    }

    if (!TegraVideoOpenGPU(priv, scrn))
        return FALSE;

    fb_rotated = drm_create_fb2(tegra->drm, tegra->fd, priv->fb->format,
                                width, height, offsets, FALSE);
    if (!fb_rotated)
        return FALSE;

    if (!TegraVideoCopyRotatedPlane(priv,
                                    fb_rotated->bo_y,
                                    priv->fb->bo_y,
                                    fb_rotated->width,
                                    fb_rotated->height,
                                    fb_rotated->pitch_y,
                                    priv->fb->pitch_y,
                                    0, priv->fb->offset_y,
                                    priv->fb->bpp_y,
                                    rotation))
        goto err_destroy_fb;

    if (!TegraVideoCopyRotatedPlane(priv,
                                    fb_rotated->bo_cb,
                                    priv->fb->bo_cb,
                                    fb_rotated->width_c,
                                    fb_rotated->height_c,
                                    fb_rotated->pitch_cb,
                                    priv->fb->pitch_cb,
                                    0, priv->fb->offset_cb,
                                    priv->fb->bpp_c,
                                    rotation))
        goto err_destroy_fb;

    if (!TegraVideoCopyRotatedPlane(priv,
                                    fb_rotated->bo_cr,
                                    priv->fb->bo_cr,
                                    fb_rotated->width_c,
                                    fb_rotated->height_c,
                                    fb_rotated->pitch_cr,
                                    priv->fb->pitch_cr,
                                    0, priv->fb->offset_cr,
                                    priv->fb->bpp_c,
                                    rotation))
        goto err_destroy_fb;

    tegra_stream_flush(&priv->cmds);

done:
    TegraVideoDestroyFramebuffer(scrn, &overlay->old_fb_rotated);

    overlay->old_fb_rotated = overlay->fb_rotated;
    overlay->fb_rotated     = fb_rotated;

    return TRUE;

err_destroy_fb:
    tegra_stream_flush(&priv->cmds);

    TegraVideoDestroyFramebuffer(scrn, &fb_rotated);

    return FALSE;
}

static Bool TegraVideoOverlayPutImageOnOverlay(TegraVideoPtr priv,
                                               ScrnInfoPtr scrn,
                                               int overlay_id,
                                               drmModeAtomicReqPtr req,
                                               short src_x, short src_y,
                                               short dst_x, short dst_y,
                                               short src_w, short src_h,
                                               short dst_w, short dst_h,
                                               DrawablePtr draw)
{
    TegraOverlayPtr sibling       = &priv->overlay[!overlay_id];
    TegraOverlayPtr overlay       = &priv->overlay[overlay_id];
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CrtcPtr crtc              = xf86_config->crtc[overlay_id];
    drm_overlay_fb *fb            = priv->fb;
    Rotation rotation             = crtc->rotation;
    int dx = dst_x - crtc->x;
    int dy = dst_y - crtc->y;
    int dw = dst_w, dh = dst_h;
    int sx = src_x, sy = src_y;
    int sw = src_w, sh = src_h;

    if (!overlay->visible)
        return TRUE;

    switch (rotation) {
    case RR_Rotate_0:
        dst_x = dx;
        dst_y = dy;
        break;

    case RR_Rotate_90:
        dst_x = dy;
        dst_y = crtc->mode.VDisplay - dw - dx;
        dst_w = dh;
        dst_h = dw;

        src_x = sy;
        src_y = fb->width - sw - sx;
        src_w = sh;
        src_h = sw;
        break;

    case RR_Rotate_180:
        dst_x = crtc->mode.HDisplay - dw - dx;
        dst_y = crtc->mode.VDisplay - dh - dy;
        dst_w = dw;
        dst_h = dh;

        src_x = fb->width - sw - sx;
        src_y = fb->height - sh - sy;
        src_w = sw;
        src_h = sh;
        break;

    case RR_Rotate_270:
        dst_x = crtc->mode.HDisplay - dh - dy;
        dst_y = dx;
        dst_w = dh;
        dst_h = dw;

        src_x = fb->height - sh - sy;
        src_y = sx;
        src_w = sh;
        src_h = sw;
        break;

    default:
        return FALSE;
    }

    if (overlay->fb != fb || overlay->rotation != rotation) {
        if (!TegraVideoOverlayUpdateRotatedFb(priv, scrn, sibling, overlay,
                                              rotation)) {
            ErrorMsg("Failed to rotate framebuffer\n");
            return FALSE;
        }
    }

    if (overlay->rotation == rotation &&
        overlay->src_x    == src_x &&
        overlay->src_y    == src_y &&
        overlay->src_w    == src_w &&
        overlay->src_h    == src_h &&
        overlay->dst_x    == dst_x &&
        overlay->dst_y    == dst_y &&
        overlay->dst_w    == dst_w &&
        overlay->dst_h    == dst_h &&
        overlay->fb       == fb)
            return TRUE;

    overlay->rotation = rotation;
    overlay->src_x    = src_x;
    overlay->src_y    = src_y;
    overlay->src_w    = src_w;
    overlay->src_h    = src_h;
    overlay->dst_x    = dst_x;
    overlay->dst_y    = dst_y;
    overlay->dst_w    = dst_w;
    overlay->dst_h    = dst_h;
    overlay->fb       = fb;

    return TegraVideoOverlayShowAtomic(priv, scrn, req,
                                       overlay_id,
                                       src_x, src_y,
                                       src_w, src_h,
                                       dst_x, dst_y,
                                       dst_w, dst_h);
}

static Bool TegraVideoOverlayPutImageOnOverlays(TegraVideoPtr priv,
                                                ScrnInfoPtr scrn,
                                                short src_x, short src_y,
                                                short dst_x, short dst_y,
                                                short src_w, short src_h,
                                                short dst_w, short dst_h,
                                                DrawablePtr draw)
{
    TegraPtr tegra = TegraPTR(scrn);
    drmModeAtomicReqPtr req;
    uint32_t flags = 0;
    int err;
    int id;

    req = drmModeAtomicAlloc();
    if (!req) {
        ErrorMsg("drmModeAtomicAlloc() failed\n");
        return FALSE;
    }

    for (id = 0; id < priv->overlays_num; id++) {
        if (!TegraVideoOverlayPutImageOnOverlay(priv, scrn, id, req,
                                                src_x, src_y,
                                                dst_x, dst_y,
                                                src_w, src_h,
                                                dst_w, dst_h,
                                                draw))
        {
            drmModeAtomicFree(req);
            return FALSE;
        }
    }

    if (priv->passthrough != 1)
        flags |= DRM_MODE_ATOMIC_NONBLOCK;

    err = TegraDrmModeAtomicCommit(tegra->fd, req, flags, NULL);
    drmModeAtomicFree(req);

    if (err < 0) {
        ErrorMsg("TegraDrmModeAtomicCommit failed: %d (%s)\n",
                 err, strerror(-err));
        return FALSE;
    }

    return TRUE;
}

static int TegraVideoUpdateOverlayCoverage(ScrnInfoPtr scrn,
                                           TegraVideoPtr priv,
                                           DrawablePtr draw)
{
    Bool visible        = FALSE;
    int best_overlay_id = 0;
    int best_coverage   = 0;
    int coverage;
    int id;

    for (id = 0; id < priv->overlays_num; id++) {
        coverage = tegra_crtc_coverage(draw, id);
        priv->overlay[id].visible = !!coverage;

        if (coverage > best_coverage) {
            best_coverage = coverage;
            best_overlay_id = id;
        }

        if (!coverage)
            TegraVideoOverlayClose(priv, scrn, id);
        else
            visible = TRUE;
    }

    priv->best_overlay_id = best_overlay_id;

    return visible;
}

static int TegraVideoOverlayPutImage(ScrnInfoPtr scrn,
                                     short src_x, short src_y,
                                     short dst_x, short dst_y,
                                     short src_w, short src_h,
                                     short dst_w, short dst_h,
                                     int format,
                                     unsigned char *buf,
                                     short width,
                                     short height,
                                     Bool vblankSync,
                                     RegionPtr clipBoxes,
                                     void *data, DrawablePtr draw)
{
    TegraVideoPtr priv  = data;
    int passthrough     = 0;
    int ret             = Success;
    int id;
    Bool visible;

    if (!xv_fourcc_valid(format))
        return BadImplementation;

    switch (format) {
    case FOURCC_PASSTHROUGH_YV12:
    case FOURCC_PASSTHROUGH_RGB565:
    case FOURCC_PASSTHROUGH_XRGB8888:
    case FOURCC_PASSTHROUGH_XBGR8888:
        passthrough = 1;
        break;

    case FOURCC_PASSTHROUGH_YV12_V2:
    case FOURCC_PASSTHROUGH_RGB565_V2:
    case FOURCC_PASSTHROUGH_XRGB8888_V2:
    case FOURCC_PASSTHROUGH_XBGR8888_V2:
        passthrough = 2;
        break;
    }

    if (!TegraVideoOverlayCreateFB(priv, scrn, xv_fourcc_to_drm(format),
                                   width, height, passthrough, buf) != Success)
        return BadImplementation;

    visible = TegraVideoUpdateOverlayCoverage(scrn, priv, draw);
    if (!visible)
        goto clean_up_old_fb;

    if (!passthrough)
        drm_copy_data_to_fb(priv->fb, buf, format == FOURCC_I420);

    if (!TegraVideoOverlayPutImageOnOverlays(priv, scrn,
                                             src_x, src_y,
                                             dst_x, dst_y,
                                             src_w, src_h,
                                             dst_w, dst_h,
                                             draw))
        ret = BadImplementation;

clean_up_old_fb:
    TegraVideoDestroyFramebuffer(scrn, &priv->old_fb);

    for (id = 0; id < priv->overlays_num; id++) {
        TegraOverlayPtr overlay = &priv->overlay[id];
        TegraVideoDestroyFramebuffer(scrn, &overlay->old_fb_rotated);
    }

    return ret;
}

static int TegraVideoOverlayReputImage(ScrnInfoPtr scrn,
                                     short src_x, short src_y,
                                     short dst_x, short dst_y,
                                     short src_w, short src_h,
                                     short dst_w, short dst_h,
                                     RegionPtr clipBoxes,
                                     void *data, DrawablePtr draw)
{
    TegraVideoPtr priv = data;
    Bool visible;

    visible = TegraVideoUpdateOverlayCoverage(scrn, priv, draw);
    if (!visible)
        return Success;

    if (!TegraVideoOverlayPutImageOnOverlays(priv, scrn,
                                             src_x, src_y,
                                             dst_x, dst_y,
                                             src_w, src_h,
                                             dst_w, dst_h,
                                             draw))
        return BadImplementation;

    return Success;
}

static int TegraVideoOverlayQuery(ScrnInfoPtr scrn,
                                  int id,
                                  unsigned short *w, unsigned short *h,
                                  int *pitches, int *offsets)
{
    int size;

    switch (id) {
    case FOURCC_YUY2:
        if (pitches)
            pitches[0] = (*w) * 2;

        if (offsets)
            offsets[0] = 0;

        size = (*w) * (*h) * 2;
        break;
    case FOURCC_YV12:
    case FOURCC_I420:
        if (pitches) {
            pitches[0] = (*w);
            pitches[1] = (*w) / 2;
            pitches[2] = (*w) / 2;
        }

        if (offsets) {
            offsets[0] = 0;
            offsets[1] = (*w) * (*h);
            offsets[2] = (*w) * (*h) * 5 / 4;
        }

        size = (*w) * (*h) * 3 / 2;
        break;
    case FOURCC_UYVY:
        if (pitches)
            pitches[0] = (*w) * 2;

        if (offsets)
            offsets[0] = 0;

        size = (*w) * (*h) * 2;
        break;
    case FOURCC_PASSTHROUGH_YV12:
    case FOURCC_PASSTHROUGH_RGB565:
    case FOURCC_PASSTHROUGH_XRGB8888:
    case FOURCC_PASSTHROUGH_XBGR8888:
        size = PASSTHROUGH_DATA_SIZE;
        break;
    case FOURCC_PASSTHROUGH_YV12_V2:
    case FOURCC_PASSTHROUGH_RGB565_V2:
    case FOURCC_PASSTHROUGH_XRGB8888_V2:
    case FOURCC_PASSTHROUGH_XBGR8888_V2:
        size = PASSTHROUGH_DATA_SIZE_V2;
        break;
    default:
        return BadValue;
    }

    return size;
}

static Bool
TegraXvGetDrmPlaneProperty(ScrnInfoPtr scrn,
                           TegraVideoPtr priv,
                           drmModeObjectPropertiesPtr properties,
                           const char *prop_name,
                           uint32_t *prop_id)
{
    TegraPtr tegra = TegraPTR(scrn);
    drmModePropertyPtr property;
    unsigned int i;

    for (i = 0; i < properties->count_props; i++) {
        property = drmModeGetProperty(tegra->fd, properties->props[i]);
        if (!property)
            continue;

        if (!strcmp(property->name, prop_name)) {
            *prop_id = property->prop_id;
            free(property);
            return TRUE;
        }

        free(property);
    }

    ErrorMsg("Failed to get \"%s\" property\n", prop_name);

    *prop_id = 0;

    return FALSE;
}

static Bool TegraXvGetDrmProps(ScrnInfoPtr scrn, TegraVideoPtr priv)
{
    TegraPtr tegra = TegraPTR(scrn);
    TegraOverlayPtr overlay;
    drmModeObjectPropertiesPtr properties;
    int id;

    for (id = 0; id < priv->overlays_num; id++) {
        overlay = &priv->overlay[id];

        properties = drmModeObjectGetProperties(tegra->fd, overlay->plane_id,
                                                DRM_MODE_OBJECT_PLANE);
        if (!properties) {
            ErrorMsg("drmModeObjectGetProperties() failed\n");
            return FALSE;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "CRTC_ID",
                                        &overlay->crtc_id_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "FB_ID",
                                        &overlay->fb_id_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "SRC_X",
                                        &overlay->src_x_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "SRC_Y",
                                        &overlay->src_y_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "SRC_W",
                                        &overlay->src_w_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "SRC_H",
                                        &overlay->src_h_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "CRTC_X",
                                        &overlay->crtc_x_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "CRTC_Y",
                                        &overlay->crtc_y_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "CRTC_W",
                                        &overlay->crtc_w_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "CRTC_H",
                                        &overlay->crtc_h_prop_id)) {
            goto err_free_props;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "zpos",
                                        &overlay->zpos_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.mode",
                                        &overlay->ckey_mode_prop_id)) {
            /* colorkey optional */
        }

        free(properties);

        properties = drmModeObjectGetProperties(tegra->fd,
                                                overlay->primary_plane_id,
                                                DRM_MODE_OBJECT_PLANE);
        if (!properties) {
            ErrorMsg("drmModeObjectGetProperties() failed\n");
            return FALSE;
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "zpos",
                                        &overlay->primary_zpos_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.mode",
                                        &overlay->primary_ckey_mode_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.min",
                                        &overlay->primary_ckey_min_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.max",
                                        &overlay->primary_ckey_max_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.plane_mask",
                                        &overlay->primary_ckey_plane_mask_prop_id)) {
            /* colorkey optional */
        }

        if (!TegraXvGetDrmPlaneProperty(scrn, priv, properties, "colorkey.mask",
                                        &overlay->primary_ckey_mask_prop_id)) {
            /* colorkey optional */
        }

        free(properties);
    }

    return TRUE;

err_free_props:
    free(properties);

    return FALSE;
}

static void TegraXvInit(TegraVideoPtr priv, ScrnInfoPtr scrn)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    priv->overlays_num = xf86_config->num_crtc;
    priv->color_key    = DEFAULT_COLOR_KEY;
}

Bool TegraXvScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    XF86VideoAdaptorPtr xvAdaptor;
    TegraXvAdaptorPtr adaptor;
    TegraVideoPtr priv;
    int id;

    if (noXvExtension)
        return TRUE;

    adaptor = xnfcalloc(1, sizeof(*adaptor));

    adaptor->xv.type                 = XvWindowMask | XvInputMask | XvImageMask;
    adaptor->xv.name                 = (char *)"Opentegra Video Overlay";
    adaptor->xv.nEncodings           = 1;
    adaptor->xv.pEncodings           = XvEncoding;
    adaptor->xv.pFormats             = XvFormats;
    adaptor->xv.nFormats             = TEGRA_ARRAY_SIZE(XvFormats);
    adaptor->xv.pAttributes          = XvAttributes;
    adaptor->xv.nAttributes          = TEGRA_ARRAY_SIZE(XvAttributes);
    adaptor->xv.pImages              = XvImages;
    adaptor->xv.nImages              = TEGRA_ARRAY_SIZE(XvImages);
    adaptor->xv.StopVideo            = TegraVideoOverlayStop;
    adaptor->xv.SetPortAttribute     = TegraVideoOverlaySetAttribute;
    adaptor->xv.GetPortAttribute     = TegraVideoOverlayGetAttribute;
    adaptor->xv.QueryBestSize        = TegraVideoOverlayBestSize;
    adaptor->xv.PutImage             = TegraVideoOverlayPutImage;
    adaptor->xv.ReputImage           = TegraVideoOverlayReputImage;
    adaptor->xv.QueryImageAttributes = TegraVideoOverlayQuery;
    adaptor->xv.nPorts               = 1;
    adaptor->xv.pPortPrivates        = &adaptor->dev_union;
    adaptor->xv.pPortPrivates[0].ptr = &adaptor->private;
    adaptor->xv.flags                = VIDEO_OVERLAID_IMAGES;

    xvColorKey = MAKE_ATOM("XV_COLORKEY");
    xvVdpauInfo = MAKE_ATOM("XV_TEGRA_VDPAU_INFO");
    xvAdaptor = &adaptor->xv;
    priv = &adaptor->private;

    TegraXvInit(priv, scrn);

    for (id = 0; id < priv->overlays_num; id++) {
        if (!TegraVideoOverlayInitialize(priv, scrn, id))
            goto err_free_adaptor;
    }

    if (!TegraXvGetDrmProps(scrn, priv))
        goto err_free_adaptor;

    if (!xf86XVScreenInit(pScreen, &xvAdaptor, 1)) {
        ErrorMsg("xf86XVScreenInit failed\n");
        goto err_free_adaptor;
    }

    TegraVideoOverlaySetColorKey(priv, scrn, DEFAULT_COLOR_KEY, FALSE, TRUE);

    xf86DrvMsg(scrn->scrnIndex, X_INFO, "XV adaptor initialized\n");

    return TRUE;

err_free_adaptor:
    free(adaptor);

    ErrorMsg("XV initialization failed\n");

    return FALSE;
}
