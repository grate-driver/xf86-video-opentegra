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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>

#include <X11/extensions/Xv.h>
#include <drm_fourcc.h>

#include "xorg-server.h"
#include "xf86Crtc.h"
#include "fourcc.h"

#include "compat-api.h"
#include "driver.h"
#include "xv.h"

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(scrn->scrnIndex, X_ERROR, "%s:%d/%s(): " fmt, __FILE__, \
               __LINE__, __func__, ##args)

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvColorKey;

typedef struct TegraOverlay {
    drm_overlay_fb *fb;
    uint32_t plane_id;
    Bool visible;

    int src_x;
    int src_y;
    int src_w;
    int src_h;

    int dst_x;
    int dst_y;
    int dst_w;
    int dst_h;
} TegraOverlay, *TegraOverlayPtr;

typedef struct TegraVideo {
    drmmode_crtc_private_ptr drmmode_crtc_priv;
    uint32_t primary_plane_id;
    TegraOverlay overlay;
} TegraVideo, *TegraVideoPtr;

static XvImageRec XvImages[] = {
    XVIMAGE_YUY2,
    XVIMAGE_YV12,
    XVIMAGE_I420,
    XVIMAGE_UYVY,
};

static XF86VideoFormatRec XvFormats[] = {
    {
        .depth = 24,
        .class = TrueColor,
    },
};

static XvAttributeRec XvAttributes[] = {
    {
        .flags      = XvSettable | XvGettable,
        .min_value  = 0,
        .max_value  = 0xFFFFFF,
        .name       = (char *)"XV_COLORKEY",
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
    default:
        FatalError("%s: Shouldn't be here; format_id %d\n",
                   __FUNCTION__, format_id);
        break;
    }

    return 0xFFFFFFFF;
}

static Bool TegraVideoOverlayVisible(int display_width, int display_height,
                                     int x0, int y0, int w, int h)
{
    if (x0 >= display_width)
        return FALSE;

    if (y0 >= display_height)
        return FALSE;

    if (x0 + w <= 0)
        return FALSE;

    if (y0 + h <= 0)
        return FALSE;

    return TRUE;
}

static void TegraVideoVSync(TegraVideoPtr priv, ScrnInfoPtr scrn)
{
    uint32_t crtc_pipe = priv->drmmode_crtc_priv->crtc_pipe;
    int drm_fd         = priv->drmmode_crtc_priv->drmmode->fd;
    int ret;

    drmVBlank vblank = {
        .request = {
            .type = DRM_VBLANK_RELATIVE,
            .sequence = 1,
        },
    };

    vblank.request.type |= crtc_pipe << DRM_VBLANK_HIGH_CRTC_SHIFT,

    ret = drmWaitVBlank(drm_fd, &vblank);
    if (ret < 0)
        ErrorMsg("DRM VBlank failed: %s\n", strerror(-ret));
}

static void TegraVideoOverlayShow(TegraVideoPtr priv,
                                  ScrnInfoPtr scrn,
                                  int src_x, int src_y,
                                  int src_w, int src_h,
                                  int dst_x, int dst_y,
                                  int dst_w, int dst_h)
{
    TegraOverlayPtr overlay = &priv->overlay;
    drm_overlay_fb *fb      = overlay->fb;
    uint32_t crtc_id        = priv->drmmode_crtc_priv->mode_crtc->crtc_id;
    int drm_fd              = priv->drmmode_crtc_priv->drmmode->fd;
    int ret;

    if (!overlay->visible) {
        goto update_plane;
    }

    if (overlay->src_x != src_x)
        goto update_plane;

    if (overlay->src_y != src_y)
        goto update_plane;

    if (overlay->src_w != src_w)
        goto update_plane;

    if (overlay->src_h != src_h)
        goto update_plane;

    if (overlay->dst_x != dst_x)
        goto update_plane;

    if (overlay->dst_y != dst_y)
        goto update_plane;

    if (overlay->dst_w != dst_w)
        goto update_plane;

    if (overlay->dst_h != dst_h)
        goto update_plane;

    return;

update_plane:
    overlay->src_x = src_x;
    overlay->src_y = src_y;
    overlay->src_w = src_w;
    overlay->src_h = src_h;
    overlay->dst_x = dst_x;
    overlay->dst_y = dst_y;
    overlay->dst_w = dst_w;
    overlay->dst_h = dst_h;

    ret = drmModeSetPlane(drm_fd,
                          overlay->plane_id,
                          crtc_id,
                          fb->fb_id, 0,
                          dst_x, dst_y,
                          dst_w, dst_h,
                          src_x, src_y,
                          src_w << 16, src_h << 16);
    if (ret < 0)
        ErrorMsg("DRM set plane failed: %s\n", strerror(-ret));
    else
        overlay->visible = TRUE;
}

static void TegraVideoOverlayClose(TegraVideoPtr priv, ScrnInfoPtr scrn)
{
    TegraOverlayPtr overlay = &priv->overlay;
    uint32_t crtc_id        = priv->drmmode_crtc_priv->mode_crtc->crtc_id;
    int drm_fd              = priv->drmmode_crtc_priv->drmmode->fd;
    int ret;

    if (!overlay->visible)
        return;

    ret = drmModeSetPlane(drm_fd, overlay->plane_id, crtc_id,
                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (ret < 0)
        ErrorMsg("Failed to close overlay\n");

    overlay->visible = FALSE;
}

static int TegraVideoOverlayCreateFB(TegraVideoPtr priv, ScrnInfoPtr scrn,
                                     uint32_t drm_format,
                                     uint32_t width, uint32_t height)
{
    TegraOverlayPtr overlay = &priv->overlay;
    drm_overlay_fb *fb      = overlay->fb;
    int drm_fd              = priv->drmmode_crtc_priv->drmmode->fd;

    if (!fb)
        goto create_fb;

    if (fb->format != drm_format)
        goto recreate_fb;

    if (fb->width != width)
        goto recreate_fb;

    if (fb->height != height)
        goto recreate_fb;

    return Success;

recreate_fb:
    drm_free_overlay_fb(drm_fd, fb);

create_fb:
    fb = drm_create_fb(drm_fd, drm_format, width, height);

    if (fb == NULL) {
        ErrorMsg("Failed to create overlay framebuffer\n");
        return BadImplementation;
    }

    overlay->fb = fb;

    return Success;
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

static int TegraVideoOverlaySetAttribute(ScrnInfoPtr scrn, Atom attribute,
                                         INT32 value, void *data)
{
    if (attribute != xvColorKey)
        return BadMatch;

    return Success;
}

static int TegraVideoOverlayGetAttribute(ScrnInfoPtr scrn, Atom attribute,
                                         INT32 *value, void *data)
{
    if (attribute != xvColorKey)
        return BadMatch;

    *value = 0x000000;

    return Success;
}

static void TegraVideoOverlayStop(ScrnInfoPtr scrn, void *data, Bool cleanup)
{
    TegraVideoPtr priv      = data;
    TegraOverlayPtr overlay = &priv->overlay;
    int drm_fd              = priv->drmmode_crtc_priv->drmmode->fd;

    if (!cleanup)
        return;

    TegraVideoOverlayClose(priv, scrn);
    drm_free_overlay_fb(drm_fd, overlay->fb);
    overlay->fb = NULL;
}

static int TegraVideoOverlayPutImage(ScrnInfoPtr scrn,
                                     short src_x, short src_y,
                                     short dst_x, short dst_y,
                                     short src_w, short src_h,
                                     short dst_w, short dst_h,
                                     int id,
                                     unsigned char *buf,
                                     short width,
                                     short height,
                                     Bool sync,
                                     RegionPtr clipBoxes,
                                     void *data, DrawablePtr draw)
{
    TegraVideoPtr priv      = data;
    TegraOverlayPtr overlay = &priv->overlay;
    DisplayModePtr mode     = scrn->currentMode;

    if (!TegraVideoOverlayVisible(mode->CrtcHDisplay, mode->CrtcVDisplay,
                                  dst_x, dst_y,
                                  dst_w, dst_h)) {
        TegraVideoOverlayClose(priv, scrn);
        return Success;
    }

    if (!xv_fourcc_valid(id)) {
        ErrorMsg("Unsupported FOURCC 0x%08X\n", id);
        return BadValue;
    }

    if (TegraVideoOverlayCreateFB(priv, scrn, xv_fourcc_to_drm(id),
                                  width, height) != Success)
        return BadImplementation;

    if (sync)
        TegraVideoVSync(priv, scrn);

    drm_copy_data_to_fb(overlay->fb, buf, id == FOURCC_I420);

    TegraVideoOverlayShow(priv, scrn,
                          src_x, src_y, src_w, src_h,
                          dst_x, dst_y, dst_w, dst_h);

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
    default:
        return BadValue;
    }

    return size;
}

XF86VideoAdaptorPtr TegraXvInit(ScreenPtr pScreen)
{
    ScrnInfoPtr scrn              = xf86ScreenToScrn(pScreen);
    xf86CrtcConfigPtr config      = XF86_CRTC_CONFIG_PTR(scrn);
    xf86OutputPtr output          = config->output[config->compat_output];
    xf86CrtcPtr crtc              = output->crtc;
    drmmode_crtc_private_ptr priv = crtc->driver_private;
    drmmode_ptr drmmode           = priv->drmmode;
    XF86VideoAdaptorPtr adaptor;
    TegraVideoPtr port_priv;
    uint32_t primary_plane_id;
    uint32_t overlay_plane_id;

    if (noXvExtension)
        return NULL;

    if (drmSetClientCap(drmmode->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
        ErrorMsg("Failed to set universal planes CAP\n");
        return NULL;
    }

    if (drm_get_primary_plane(drmmode->fd, priv->crtc_pipe,
                              &primary_plane_id) < 0)
        return NULL;

    if (drm_get_overlay_plane(drmmode->fd, priv->crtc_pipe,
                              DRM_FORMAT_YUV420, &overlay_plane_id) < 0)
        return NULL;

    adaptor   = xnfcalloc(1, sizeof(*adaptor) +
                                sizeof(DevUnion) +
                                    sizeof(*port_priv));

    adaptor->type                   = XvWindowMask | XvInputMask | XvImageMask;
    adaptor->name                   = "Opentegra Video Overlay";
    adaptor->nEncodings             = 1;
    adaptor->pEncodings             = XvEncoding;
    adaptor->pFormats               = XvFormats;
    adaptor->nFormats               = TEGRA_ARRAY_SIZE(XvFormats);
    adaptor->pAttributes            = XvAttributes;
    adaptor->nAttributes            = TEGRA_ARRAY_SIZE(XvAttributes);
    adaptor->pImages                = XvImages;
    adaptor->nImages                = TEGRA_ARRAY_SIZE(XvImages);
    adaptor->StopVideo              = TegraVideoOverlayStop;
    adaptor->SetPortAttribute       = TegraVideoOverlaySetAttribute;
    adaptor->GetPortAttribute       = TegraVideoOverlayGetAttribute;
    adaptor->QueryBestSize          = TegraVideoOverlayBestSize;
    adaptor->PutImage               = TegraVideoOverlayPutImage;
    adaptor->QueryImageAttributes   = TegraVideoOverlayQuery;
    adaptor->nPorts                 = 1;
    adaptor->pPortPrivates          = (DevUnion *) (&adaptor[1]);
    adaptor->flags                  = VIDEO_OVERLAID_IMAGES;

    port_priv = (TegraVideoPtr) (&adaptor->pPortPrivates[1]);

    port_priv->overlay.plane_id  = overlay_plane_id;
    port_priv->primary_plane_id  = primary_plane_id;
    port_priv->drmmode_crtc_priv = priv;

    adaptor->pPortPrivates[0].ptr = port_priv;

    xvColorKey = MAKE_ATOM("XV_COLORKEY");

    xf86DrvMsg(scrn->scrnIndex, X_INFO, "XV adaptor initialized\n");

    return adaptor;
}
