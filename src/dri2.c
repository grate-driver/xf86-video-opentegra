#include "driver.h"

enum tegra_dri2_frame_event_type {
    MS_DRI2_QUEUE_SWAP,
    MS_DRI2_WAIT_MSC,
};

typedef struct tegra_dri2_frame_event {
    ScreenPtr screen;

    DrawablePtr drawable;
    ClientPtr client;
    enum tegra_dri2_frame_event_type type;
    int frame;
    xf86CrtcPtr crtc;

    struct xorg_list drawable_resource, client_resource;

    /* for swaps & flips only */
    DRI2SwapEventPtr event_complete;
    void *event_data;
    DRI2BufferPtr front;
    DRI2BufferPtr back;
} tegra_dri2_frame_event_rec, *tegra_dri2_frame_event_ptr;

typedef struct {
    int refcnt;
    PixmapPtr pixmap;
} tegra_dri2_buffer_private_rec, *tegra_dri2_buffer_private_ptr;

static DevPrivateKeyRec tegra_dri2_client_key;
static RESTYPE frame_event_client_type, frame_event_drawable_type;
static int tegra_dri2_server_generation;

struct tegra_dri2_resource {
    XID id;
    RESTYPE type;
    struct xorg_list list;
};

static uint32_t
drmmode_crtc_vblank_pipe(int crtc_id)
{
    if (crtc_id > 1)
        return crtc_id << DRM_VBLANK_HIGH_CRTC_SHIFT;
    else if (crtc_id > 0)
        return DRM_VBLANK_SECONDARY;
    else
        return 0;
}

static struct tegra_dri2_resource *
tegra_get_resource(XID id, RESTYPE type)
{
    struct tegra_dri2_resource *resource;
    void *ptr;

    ptr = NULL;
    dixLookupResourceByType(&ptr, id, type, NULL, DixWriteAccess);
    if (ptr)
        return ptr;

    resource = malloc(sizeof(*resource));
    if (resource == NULL)
        return NULL;

    if (!AddResource(id, type, resource))
        return NULL;

    resource->id = id;
    resource->type = type;
    xorg_list_init(&resource->list);
    return resource;
}

static inline PixmapPtr
get_drawable_pixmap(DrawablePtr drawable)
{
    ScreenPtr screen = drawable->pScreen;

    if (drawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr) drawable;
    else
        return screen->GetWindowPixmap((WindowPtr) drawable);
}

static DRI2Buffer2Ptr
tegra_dri2_create_buffer(DrawablePtr drawable, unsigned int attachment,
                         unsigned int format)
{
    ScreenPtr screen = drawable->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    DRI2Buffer2Ptr buffer;
    PixmapPtr pixmap;
    tegra_dri2_buffer_private_ptr private;
    struct tegra_pixmap *tegra;
    int err;

    if (!TegraPTR(scrn)->exa)
        return NULL;

    buffer = calloc(1, sizeof *buffer);
    if (buffer == NULL)
        return NULL;

    private = calloc(1, sizeof(*private));
    if (private == NULL) {
        free(buffer);
        return NULL;
    }

    pixmap = NULL;
    if (attachment == DRI2BufferFrontLeft) {
        pixmap = get_drawable_pixmap(drawable);
        if (pixmap && pixmap->drawable.pScreen != screen)
            pixmap = NULL;
        if (pixmap)
            pixmap->refcnt++;
    }

    if (pixmap == NULL) {
        int pixmap_width = drawable->width;
        int pixmap_height = drawable->height;
        int pixmap_cpp = (format != 0) ? format : drawable->depth;

        /* Assume that non-color-buffers require special
         * device-specific handling.  Mesa currently makes no requests
         * for non-color aux buffers.
         */
        switch (attachment) {
        case DRI2BufferAccum:
        case DRI2BufferBackLeft:
        case DRI2BufferBackRight:
        case DRI2BufferFakeFrontLeft:
        case DRI2BufferFakeFrontRight:
        case DRI2BufferFrontLeft:
        case DRI2BufferFrontRight:
            break;

        case DRI2BufferStencil:
        case DRI2BufferDepth:
        case DRI2BufferDepthStencil:
        case DRI2BufferHiz:
        default:
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "Request for DRI2 buffer attachment %d unsupported\n",
                       attachment);
            free(private);
            free(buffer);
            return NULL;
        }

        pixmap = screen->CreatePixmap(screen,
                                      pixmap_width,
                                      pixmap_height,
                                      pixmap_cpp,
                                      TEGRA_DRI_USAGE_HINT);
        if (pixmap == NULL) {
            free(private);
            free(buffer);
            return NULL;
        }
    }

    buffer->attachment = attachment;
    buffer->cpp = pixmap->drawable.bitsPerPixel / 8;
    buffer->format = format;
    /* The buffer's flags field is unused by the client drivers in
     * Mesa currently.
     */
    buffer->flags = 0;
    buffer->name = -1;

    tegra = exaGetPixmapDriverPrivate(pixmap);

    if (tegra->dri) {
        err = drm_tegra_bo_get_name(tegra->bo, &buffer->name);

        if (buffer->name == -1) {
            xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                    "Failed to get DRI2 name for pixmap: %d\n", err);
            screen->DestroyPixmap(pixmap);
            free(private);
            free(buffer);
            return NULL;
        }

        drm_tegra_bo_forbid_caching(tegra->bo);
    }

    buffer->pitch = pixmap->devKind;
    buffer->driverPrivate = private;
    private->refcnt = 1;
    private->pixmap = pixmap;

    return buffer;
}

unsigned refed;

static void
tegra_dri2_reference_buffer(DRI2Buffer2Ptr buffer)
{
    if (buffer) {
        tegra_dri2_buffer_private_ptr private = buffer->driverPrivate;
        private->refcnt++;
    }
}

static void tegra_dri2_destroy_buffer(DrawablePtr drawable,
                                      DRI2Buffer2Ptr buffer)
{
    if (!buffer)
        return;

    if (buffer->driverPrivate) {
        tegra_dri2_buffer_private_ptr private = buffer->driverPrivate;
        if (--private->refcnt == 0) {
            ScreenPtr screen = private->pixmap->drawable.pScreen;
            screen->DestroyPixmap(private->pixmap);
            free(private);
            free(buffer);
        }
    } else {
        free(buffer);
    }
}

static void
tegra_dri2_copy_region(DrawablePtr drawable, RegionPtr pRegion,
                       DRI2BufferPtr destBuffer, DRI2BufferPtr sourceBuffer)
{
    tegra_dri2_buffer_private_ptr src_priv = sourceBuffer->driverPrivate;
    tegra_dri2_buffer_private_ptr dst_priv = destBuffer->driverPrivate;
    PixmapPtr src_pixmap = src_priv->pixmap;
    PixmapPtr dst_pixmap = dst_priv->pixmap;
    ScreenPtr screen = drawable->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(pScrn);
    DrawablePtr src = (sourceBuffer->attachment == DRI2BufferFrontLeft)
        ? drawable : &src_pixmap->drawable;
    DrawablePtr dst = (destBuffer->attachment == DRI2BufferFrontLeft)
        ? drawable : &dst_pixmap->drawable;
    struct tegra_pixmap *tegra_pixmap;
    RegionPtr pCopyClip;
    int drm_ver;
    GCPtr gc;

    gc = GetScratchGC(dst->depth, screen);
    if (!gc)
        return;

    pCopyClip = REGION_CREATE(screen, NULL, 0);
    REGION_COPY(screen, pCopyClip, pRegion);
    (*gc->funcs->ChangeClip) (gc, CT_REGION, pCopyClip, 0);
    ValidateGC(dst, gc);

    /* It's important that this copy gets submitted before the direct
     * rendering client submits rendering for the next frame, but we
     * don't actually need to submit right now.  The client will wait
     * for the DRI2CopyRegion reply or the swap buffer event before
     * rendering, and we'll hit the flush callback chain before those
     * messages are sent.  We submit our batch buffers from the flush
     * callback chain so we know that will happen before the client
     * tries to render again.
     */
    gc->ops->CopyArea(src, dst, gc,
                      0, 0,
                      drawable->width, drawable->height,
                      0, 0);

    FreeScratchGC(gc);

    drm_ver = drm_tegra_version(tegra->drm);

    /*
     * We need to wait for the copying completion here because UAPI v1
     * doesn't support BO reservation, and thus, implicit fencing won't
     * be available, resulting in asynchronous usage of the DRI surface
     * (very visible flickering). Previously we didn't support the
     * asynchronous rendering in our EXA driver, so it wasn't a problem
     * back then.
     */
    if (drm_ver < GRATE_KERNEL_DRM_VERSION) {
        tegra_pixmap = exaGetPixmapDriverPrivate(src_pixmap);
        if (tegra_pixmap && tegra_pixmap->dri)
            TEGRA_PIXMAP_WAIT_READ_FENCES(tegra_pixmap);

        tegra_pixmap = exaGetPixmapDriverPrivate(dst_pixmap);
        if (tegra_pixmap && tegra_pixmap->dri)
            TEGRA_PIXMAP_WAIT_WRITE_FENCES(tegra_pixmap);
    }
}

static uint64_t
gettime_us(void)
{
    struct timespec tv;

    if (clock_gettime(CLOCK_MONOTONIC, &tv))
        return 0;

    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

/**
 * Get current frame count and frame count timestamp, based on drawable's
 * crtc.
 */
static int
tegra_dri2_get_msc(DrawablePtr draw, CARD64 *ust, CARD64 *msc)
{
    int ret;
    xf86CrtcPtr crtc = tegra_crtc_covering_drawable(draw);

    /* Drawable not displayed, make up a *monotonic* value */
    if (crtc == NULL) {
        *ust = gettime_us();
        *msc = 0;
        return TRUE;
    }

    ret = tegra_get_crtc_ust_msc(crtc, ust, msc);

    if (ret)
        return FALSE;

    return TRUE;
}

static XID
get_client_id(ClientPtr client)
{
    XID *ptr = dixGetPrivateAddr(&client->devPrivates, &tegra_dri2_client_key);
    if (*ptr == 0)
        *ptr = FakeClientID(client->index);
    return *ptr;
}

/*
 * Hook this frame event into the server resource
 * database so we can clean it up if the drawable or
 * client exits while the swap is pending
 */
static Bool
tegra_dri2_add_frame_event(tegra_dri2_frame_event_ptr info)
{
    struct tegra_dri2_resource *resource;

    resource = tegra_get_resource(get_client_id(info->client),
                                  frame_event_client_type);
    if (resource == NULL)
        return FALSE;

    xorg_list_add(&info->client_resource, &resource->list);

    resource = tegra_get_resource(info->drawable->id, frame_event_drawable_type);
    if (resource == NULL) {
        xorg_list_del(&info->client_resource);
        return FALSE;
    }

    xorg_list_add(&info->drawable_resource, &resource->list);

    return TRUE;
}

static void
tegra_dri2_del_frame_event(tegra_dri2_frame_event_rec *info)
{
    xorg_list_del(&info->client_resource);
    xorg_list_del(&info->drawable_resource);

    if (info->front)
        tegra_dri2_destroy_buffer(NULL, info->front);
    if (info->back)
        tegra_dri2_destroy_buffer(NULL, info->back);

    free(info);
}

static void
tegra_dri2_blit_swap(DrawablePtr drawable,
                     DRI2BufferPtr dst,
                     DRI2BufferPtr src)
{
    BoxRec box;
    RegionRec region;

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = drawable->width;
    box.y2 = drawable->height;
    REGION_INIT(pScreen, &region, &box, 0);

    tegra_dri2_copy_region(drawable, &region, dst, src);
}

static void
tegra_dri2_frame_event_handler(uint64_t msc,
                               uint64_t usec,
                               void *data)
{
    tegra_dri2_frame_event_ptr frame_info = data;
    DrawablePtr drawable = frame_info->drawable;
    ScreenPtr screen = frame_info->screen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    uint32_t tv_sec = usec / 1000000;
    uint32_t tv_usec = usec % 1000000;

    if (!drawable) {
        tegra_dri2_del_frame_event(frame_info);
        return;
    }

    switch (frame_info->type) {
    case MS_DRI2_QUEUE_SWAP:
        tegra_dri2_blit_swap(drawable, frame_info->front, frame_info->back);
        DRI2SwapComplete(frame_info->client, drawable, msc, tv_sec, tv_usec,
                         DRI2_BLIT_COMPLETE,
                         frame_info->client ? frame_info->event_complete : NULL,
                         frame_info->event_data);
        break;

    case MS_DRI2_WAIT_MSC:
        if (frame_info->client)
            DRI2WaitMSCComplete(frame_info->client, drawable,
                                msc, tv_sec, tv_usec);
        break;

    default:
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "%s: unknown vblank event (type %d) received\n", __func__,
                   frame_info->type);
        break;
    }

    tegra_dri2_del_frame_event(frame_info);
}

static void
tegra_dri2_frame_event_abort(void *data)
{
    tegra_dri2_frame_event_ptr frame_info = data;

    tegra_dri2_del_frame_event(frame_info);
}

/**
 * Request a DRM event when the requested conditions will be satisfied.
 *
 * We need to handle the event and ask the server to wake up the client when
 * we receive it.
 */
static int
tegra_dri2_schedule_wait_msc(ClientPtr client, DrawablePtr draw,
                             CARD64 target_msc, CARD64 divisor,
                             CARD64 _remainder)
{
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    tegra_dri2_frame_event_ptr wait_info;
    drmVBlank vbl;
    int ret;
    xf86CrtcPtr crtc = tegra_crtc_covering_drawable(draw);
    drmmode_crtc_private_ptr drmmode_crtc;
    CARD64 current_msc, current_ust, request_msc;
    uint32_t seq;

    /* Drawable not visible, return immediately */
    if (!crtc)
        goto out_complete;
    drmmode_crtc = crtc->driver_private;

    wait_info = calloc(1, sizeof(*wait_info));
    if (!wait_info)
        goto out_complete;

    wait_info->screen = screen;
    wait_info->drawable = draw;
    wait_info->client = client;
    wait_info->type = MS_DRI2_WAIT_MSC;

    if (!tegra_dri2_add_frame_event(wait_info)) {
        free(wait_info);
        wait_info = NULL;
        goto out_complete;
    }

    /* Get current count */
    ret = tegra_get_crtc_ust_msc(crtc, &current_ust, &current_msc);

    /*
     * If divisor is zero, or current_msc is smaller than target_msc,
     * we just need to make sure target_msc passes  before waking up the
     * client.
     */
    if (divisor == 0 || current_msc < target_msc) {
        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This keeps the client from continually
         * sending us MSC targets from the past by forcibly updating
         * their count on this call.
         */
        seq = tegra_drm_queue_alloc(crtc, wait_info,
                                    tegra_dri2_frame_event_handler,
                                    tegra_dri2_frame_event_abort);
        if (!seq)
            goto out_free;

        if (current_msc >= target_msc)
            target_msc = current_msc;
        vbl.request.type = (DRM_VBLANK_ABSOLUTE |
                            DRM_VBLANK_EVENT |
                            drmmode_crtc_vblank_pipe(drmmode_crtc->crtc_pipe));
        vbl.request.sequence = tegra_crtc_msc_to_kernel_msc(crtc, target_msc);
        vbl.request.signal = (unsigned long)seq;

        ret = drmWaitVBlank(tegra->fd, &vbl);
        if (ret) {
            static int limit = 5;
            if (limit) {
                xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                           "%s:%d get vblank counter failed: %s\n",
                           __FUNCTION__, __LINE__,
                           strerror(errno));
                limit--;
            }
            goto out_free;
        }

        wait_info->frame = tegra_kernel_msc_to_crtc_msc(crtc, vbl.reply.sequence);
        DRI2BlockClient(client, draw);
        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * so we queue an event that will satisfy the divisor/remainder equation.
     */
    vbl.request.type =
        DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT | drmmode_crtc_vblank_pipe(drmmode_crtc->crtc_pipe);

    request_msc = current_msc - (current_msc % divisor) +
        _remainder;
    /*
     * If calculated remainder is larger than requested remainder,
     * it means we've passed the last point where
     * seq % divisor == remainder, so we need to wait for the next time
     * that will happen.
     */
    if ((current_msc % divisor) >= _remainder)
        request_msc += divisor;

    seq = tegra_drm_queue_alloc(crtc, wait_info,
                                tegra_dri2_frame_event_handler,
                                tegra_dri2_frame_event_abort);
    if (!seq)
        goto out_free;

    vbl.request.sequence = tegra_crtc_msc_to_kernel_msc(crtc, request_msc);
    vbl.request.signal = (unsigned long)seq;

    ret = drmWaitVBlank(tegra->fd, &vbl);
    if (ret) {
        static int limit = 5;
        if (limit) {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "%s:%d get vblank counter failed: %s\n",
                       __FUNCTION__, __LINE__,
                       strerror(errno));
            limit--;
        }
        goto out_free;
    }

    wait_info->frame = tegra_kernel_msc_to_crtc_msc(crtc, vbl.reply.sequence);
    DRI2BlockClient(client, draw);

    return TRUE;

 out_free:
    tegra_dri2_del_frame_event(wait_info);
 out_complete:
    DRI2WaitMSCComplete(client, draw, target_msc, 0, 0);
    return TRUE;
}

/**
 * ScheduleSwap is responsible for requesting a DRM vblank event for
 * the appropriate frame, or executing the swap immediately if it
 * doesn't need to wait.
 *
 * When the swap is complete, the driver should call into the server so it
 * can send any swap complete events that have been requested.
 */
static int
tegra_dri2_schedule_swap(ClientPtr client, DrawablePtr draw,
                         DRI2BufferPtr front, DRI2BufferPtr back,
                         CARD64 *target_msc, CARD64 divisor,
                         CARD64 _remainder, DRI2SwapEventPtr func, void *data)
{
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    drmVBlank vbl;
    int ret;
    xf86CrtcPtr crtc = tegra_crtc_covering_drawable(draw);
    drmmode_crtc_private_ptr drmmode_crtc;
    tegra_dri2_frame_event_ptr frame_info = NULL;
    uint64_t current_msc, current_ust;
    uint64_t request_msc;
    uint32_t seq;

    /* Drawable not displayed... just complete the swap */
    if (!crtc)
        goto blit_fallback;
    drmmode_crtc = crtc->driver_private;

    frame_info = calloc(1, sizeof(*frame_info));
    if (!frame_info)
        goto blit_fallback;

    frame_info->screen = screen;
    frame_info->drawable = draw;
    frame_info->client = client;
    frame_info->event_complete = func;
    frame_info->event_data = data;
    frame_info->front = front;
    frame_info->back = back;
    frame_info->crtc = crtc;
    frame_info->type = MS_DRI2_QUEUE_SWAP;

    if (!tegra_dri2_add_frame_event(frame_info)) {
        free(frame_info);
        frame_info = NULL;
        goto blit_fallback;
    }

    tegra_dri2_reference_buffer(front);
    tegra_dri2_reference_buffer(back);

    ret = tegra_get_crtc_ust_msc(crtc, &current_ust, &current_msc);

    /*
     * If divisor is zero, or current_msc is smaller than target_msc
     * we just need to make sure target_msc passes before initiating
     * the swap.
     */
    if (divisor == 0 || current_msc < *target_msc) {
        /* We need to use DRM_VBLANK_NEXTONMISS to avoid unreliable
         * timestamping later on.
         */
        vbl.request.type = (DRM_VBLANK_ABSOLUTE |
                            DRM_VBLANK_NEXTONMISS |
                            DRM_VBLANK_EVENT |
                            drmmode_crtc_vblank_pipe(drmmode_crtc->crtc_pipe));

        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This makes swap_interval logic more robust.
         */
        if (current_msc >= *target_msc)
            *target_msc = current_msc;

        seq = tegra_drm_queue_alloc(crtc, frame_info,
                                    tegra_dri2_frame_event_handler,
                                    tegra_dri2_frame_event_abort);
        if (!seq)
            goto blit_fallback;

        vbl.request.sequence = tegra_crtc_msc_to_kernel_msc(crtc, *target_msc);
        vbl.request.signal = (unsigned long)seq;

        ret = drmWaitVBlank(tegra->fd, &vbl);
        if (ret) {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "divisor 0 get vblank counter failed: %s\n",
                       strerror(errno));
            tegra_drm_abort_seq(scrn, seq);
            frame_info = NULL;
            goto blit_fallback;
        }

        *target_msc = tegra_kernel_msc_to_crtc_msc(crtc, vbl.reply.sequence);
        frame_info->frame = *target_msc;

        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * and we need to queue an event that will satisfy the divisor/_remainder
     * equation.
     */
    vbl.request.type = (DRM_VBLANK_ABSOLUTE |
                        DRM_VBLANK_NEXTONMISS |
                        DRM_VBLANK_EVENT |
                        drmmode_crtc_vblank_pipe(drmmode_crtc->crtc_pipe));

    request_msc = current_msc - (current_msc % divisor) +
        _remainder;

    /*
     * If the calculated deadline vbl.request.sequence is smaller than
     * or equal to current_msc, it means we've passed the last point
     * when effective onset frame seq could satisfy
     * seq % divisor == remainder, so we need to wait for the next time
     * this will happen.

     * This comparison takes the DRM_VBLANK_NEXTONMISS delay into account.
     */
    if (request_msc <= current_msc)
        request_msc += divisor;


    seq = tegra_drm_queue_alloc(crtc, frame_info,
                                tegra_dri2_frame_event_handler,
                                tegra_dri2_frame_event_abort);
    if (!seq)
        goto blit_fallback;

    vbl.request.sequence = tegra_crtc_msc_to_kernel_msc(crtc, request_msc);
    vbl.request.signal = (unsigned long)seq;

    ret = drmWaitVBlank(tegra->fd, &vbl);
    if (ret) {
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "final get vblank counter failed: %s\n",
                   strerror(errno));
        tegra_drm_abort_seq(scrn, seq);
        frame_info = NULL;
        goto blit_fallback;
    }

    *target_msc = tegra_kernel_msc_to_crtc_msc(crtc, vbl.reply.sequence);
    frame_info->frame = *target_msc;

    return TRUE;

 blit_fallback:
    tegra_dri2_blit_swap(draw, front, back);
    DRI2SwapComplete(client, draw, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
    if (frame_info)
        tegra_dri2_del_frame_event(frame_info);
    *target_msc = 0; /* offscreen, so zero out target vblank count */
    return TRUE;
}

static Bool
tegra_dri2_frame_info_match(void *data, void *match_data)
{
    tegra_dri2_frame_event_ptr info = data;
    tegra_dri2_frame_event_ptr match = match_data;

    return (info == match);
}

static int
tegra_dri2_frame_event_client_gone(void *data, XID id)
{
    struct tegra_dri2_resource *resource = data;

    while (!xorg_list_is_empty(&resource->list)) {
        tegra_dri2_frame_event_ptr info =
            xorg_list_first_entry(&resource->list,
                                  tegra_dri2_frame_event_rec,
                                  client_resource);

        tegra_drm_abort(NULL, tegra_dri2_frame_info_match, info);

        xorg_list_del(&info->client_resource);
        info->client = NULL;
    }
    free(resource);

    return Success;
}

static int
tegra_dri2_frame_event_drawable_gone(void *data, XID id)
{
    struct tegra_dri2_resource *resource = data;

    while (!xorg_list_is_empty(&resource->list)) {
        tegra_dri2_frame_event_ptr info =
            xorg_list_first_entry(&resource->list,
                                  tegra_dri2_frame_event_rec,
                                  drawable_resource);

        xorg_list_del(&info->drawable_resource);
        info->drawable = NULL;
    }
    free(resource);

    return Success;
}

static Bool
tegra_dri2_register_frame_event_resource_types(void)
{
    frame_event_client_type =
        CreateNewResourceType(tegra_dri2_frame_event_client_gone,
                              "Frame Event Client");
    if (!frame_event_client_type)
        return FALSE;

    frame_event_drawable_type =
        CreateNewResourceType(tegra_dri2_frame_event_drawable_gone,
                              "Frame Event Drawable");
    if (!frame_event_drawable_type)
        return FALSE;

    return TRUE;
}

Bool TegraDRI2ScreenInit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    DRI2InfoRec info;
    const char *drivernames[2] = {
        "tegra", /* DRI */
        "tegra", /* VDPAU */
    };

    if (tegra->dri2_enabled)
        return TRUE;

    if (!xf86LoaderCheckSymbol("DRI2Version"))
        return FALSE;

    if (!dixRegisterPrivateKey(&tegra_dri2_client_key,
                               PRIVATE_CLIENT, sizeof(XID)))
        return FALSE;

    if (serverGeneration != tegra_dri2_server_generation) {
        tegra_dri2_server_generation = serverGeneration;
        if (!tegra_dri2_register_frame_event_resource_types()) {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "Cannot register DRI2 frame event resources\n");
            return FALSE;
        }
    }

    memset(&info, '\0', sizeof(info));
    info.fd = tegra->fd;
    info.driverName = "tegra"; /* Compat field, unused. */
    info.deviceName = drmGetDeviceNameFromFd(tegra->fd);

    info.version = 4;
    info.CreateBuffer = tegra_dri2_create_buffer;
    info.DestroyBuffer = tegra_dri2_destroy_buffer;
    info.CopyRegion = tegra_dri2_copy_region;
    info.ScheduleSwap = tegra_dri2_schedule_swap;
    info.GetMSC = tegra_dri2_get_msc;
    info.ScheduleWaitMSC = tegra_dri2_schedule_wait_msc;

    /* These two will be filled in by dri2.c */
    info.numDrivers = 2;
    info.driverNames = drivernames;

    DRI2ScreenInit(screen, &info);

    tegra->dri2_enabled = TRUE;

    xf86DrvMsg(scrn->scrnIndex, X_INFO, "DRI2 initialized\n");

    return TRUE;
}

void TegraDRI2ScreenExit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);

    if (!tegra->dri2_enabled)
        return;

    DRI2CloseScreen(screen);

    tegra->dri2_enabled = FALSE;
}
