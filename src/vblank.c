/*
 * Copyright © 2013 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/** @file vblank.c
 *
 * Support for tracking the DRM's vblank events.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <xf86.h>
#include <xf86Crtc.h>
#include <list.h>
#include <poll.h>
#include "driver.h"
#include "drmmode_display.h"

#include "compat-api.h"
#include "vblank.h"

/**
 * Tracking for outstanding events queued to the kernel.
 *
 * Each list entry is a struct tegra_drm_queue, which has a uint32_t
 * value generated from drm_seq that identifies the event and a
 * reference back to the crtc/screen associated with the event.  It's
 * done this way rather than in the screen because we want to be able
 * to drain the list of event handlers that should be called at server
 * regen time, even though we don't close the drm fd and have no way
 * to actually drain the kernel events.
 */
static struct xorg_list tegra_drm_queue;
static uint32_t tegra_drm_seq;

static void tegra_box_intersect(BoxPtr dest, BoxPtr a, BoxPtr b)
{
    dest->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    dest->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    if (dest->x1 >= dest->x2) {
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
        return;
    }

    dest->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    dest->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    if (dest->y1 >= dest->y2)
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
}

static void tegra_crtc_box(xf86CrtcPtr crtc, BoxPtr crtc_box)
{
    if (crtc->enabled) {
        crtc_box->x1 = crtc->x;
        crtc_box->x2 =
            crtc->x + xf86ModeWidth(&crtc->mode, crtc->rotation);
        crtc_box->y1 = crtc->y;
        crtc_box->y2 =
            crtc->y + xf86ModeHeight(&crtc->mode, crtc->rotation);
    } else
        crtc_box->x1 = crtc_box->x2 = crtc_box->y1 = crtc_box->y2 = 0;
}

static int tegra_box_area(BoxPtr box)
{
    return (int)(box->x2 - box->x1) * (int)(box->y2 - box->y1);
}

static Bool
tegra_crtc_on(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    return crtc->enabled && drmmode_crtc->dpms_mode == DPMSModeOn;
}

/*
 * Return the crtc covering 'box'. If two crtcs cover a portion of
 * 'box', then prefer 'desired'. If 'desired' is NULL, then prefer the crtc
 * with greater coverage
 */

xf86CrtcPtr
tegra_covering_crtc(ScrnInfoPtr scrn,
                    BoxPtr box, xf86CrtcPtr desired, BoxPtr crtc_box_ret)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CrtcPtr crtc, best_crtc;
    int coverage, best_coverage;
    int c;
    BoxRec crtc_box, cover_box;

    best_crtc = NULL;
    best_coverage = 0;
    crtc_box_ret->x1 = 0;
    crtc_box_ret->x2 = 0;
    crtc_box_ret->y1 = 0;
    crtc_box_ret->y2 = 0;
    for (c = 0; c < xf86_config->num_crtc; c++) {
        crtc = xf86_config->crtc[c];

        /* If the CRTC is off, treat it as not covering */
        if (!tegra_crtc_on(crtc))
            continue;

        tegra_crtc_box(crtc, &crtc_box);
        tegra_box_intersect(&cover_box, &crtc_box, box);
        coverage = tegra_box_area(&cover_box);
        if (coverage && crtc == desired) {
            *crtc_box_ret = crtc_box;
            return crtc;
        }
        if (coverage > best_coverage) {
            *crtc_box_ret = crtc_box;
            best_crtc = crtc;
            best_coverage = coverage;
        }
    }
    return best_crtc;
}

xf86CrtcPtr
tegra_dri2_crtc_covering_drawable(DrawablePtr pDraw)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    BoxRec box, crtcbox;

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;

    return tegra_covering_crtc(pScrn, &box, NULL, &crtcbox);
}

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

static Bool
tegra_get_kernel_ust_msc(xf86CrtcPtr crtc,
                         uint32_t *msc, uint64_t *ust)
{
    ScreenPtr screen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmVBlank vbl;
    int ret;

    /* Get current count */
    vbl.request.type = DRM_VBLANK_RELATIVE | drmmode_crtc_vblank_pipe(drmmode_crtc->crtc_pipe);
    vbl.request.sequence = 0;
    vbl.request.signal = 0;
    ret = drmWaitVBlank(tegra->fd, &vbl);
    if (ret) {
        *msc = 0;
        *ust = 0;
        return FALSE;
    } else {
        *msc = vbl.reply.sequence;
        *ust = (CARD64) vbl.reply.tval_sec * 1000000 + vbl.reply.tval_usec;
        return TRUE;
    }
}

/**
 * Convert a 32-bit kernel MSC sequence number to a 64-bit local sequence
 * number, adding in the vblank_offset and high 32 bits, and dealing
 * with 64-bit wrapping
 */
uint64_t
tegra_kernel_msc_to_crtc_msc(xf86CrtcPtr crtc, uint32_t sequence)
{
    drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
    sequence += drmmode_crtc->vblank_offset;

    if ((int32_t) (sequence - drmmode_crtc->msc_prev) < -0x40000000)
        drmmode_crtc->msc_high += 0x100000000L;
    drmmode_crtc->msc_prev = sequence;
    return drmmode_crtc->msc_high + sequence;
}

int
tegra_get_crtc_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
    uint32_t kernel_msc;

    if (!tegra_get_kernel_ust_msc(crtc, &kernel_msc, ust))
        return BadMatch;
    *msc = tegra_kernel_msc_to_crtc_msc(crtc, kernel_msc);

    return Success;
}

#define MAX_VBLANK_OFFSET       1000

/**
 * Convert a 64-bit adjusted MSC value into a 32-bit kernel sequence number,
 * removing the high 32 bits and subtracting out the vblank_offset term.
 *
 * This also updates the vblank_offset when it notices that the value should
 * change.
 */
uint32_t
tegra_crtc_msc_to_kernel_msc(xf86CrtcPtr crtc, uint64_t expect)
{
    drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
    uint64_t msc;
    uint64_t ust;
    int64_t diff;

    if (tegra_get_crtc_ust_msc(crtc, &ust, &msc) == Success) {
        diff = expect - msc;

        /* We're way off here, assume that the kernel has lost its mind
         * and smack the vblank back to something sensible
         */
        if (diff < -MAX_VBLANK_OFFSET || MAX_VBLANK_OFFSET < diff) {
            drmmode_crtc->vblank_offset += (int32_t) diff;
            if (drmmode_crtc->vblank_offset > -MAX_VBLANK_OFFSET &&
                drmmode_crtc->vblank_offset < MAX_VBLANK_OFFSET)
                drmmode_crtc->vblank_offset = 0;
        }
    }
    return (uint32_t) (expect - drmmode_crtc->vblank_offset);
}

/**
 * Check for pending DRM events and process them.
 */
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,19,0,0,0)
static void
tegra_drm_socket_handler(int fd, int ready, void *data)
{
    ScreenPtr screen = data;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);

    if (data == NULL)
        return;

    drmHandleEvent(tegra->fd, &tegra->event_context);
}
#else
static void
tegra_drm_wakeup_handler(void *data, int err, void *mask)
{
    ScreenPtr screen = data;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    fd_set *read_mask = mask;

    if (data == NULL || err < 0)
        return;

    if (FD_ISSET(tegra->fd, read_mask))
        drmHandleEvent(tegra->fd, &tegra->event_context);
}
#endif

/*
 * Enqueue a potential drm response; when the associated response
 * appears, we've got data to pass to the handler from here
 */
uint32_t
tegra_drm_queue_alloc(xf86CrtcPtr crtc,
                      void *data,
                      tegra_drm_handler_proc handler,
                      tegra_drm_abort_proc abort)
{
    ScreenPtr screen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct tegra_drm_queue *q;

    q = calloc(1, sizeof(struct tegra_drm_queue));

    if (!q)
        return 0;
    if (!tegra_drm_seq)
        ++tegra_drm_seq;
    q->seq = tegra_drm_seq++;
    q->scrn = scrn;
    q->crtc = crtc;
    q->data = data;
    q->handler = handler;
    q->abort = abort;

    xorg_list_add(&q->list, &tegra_drm_queue);

    return q->seq;
}

/**
 * Abort one queued DRM entry, removing it
 * from the list, calling the abort function and
 * freeing the memory
 */
static void
tegra_drm_abort_one(struct tegra_drm_queue *q)
{
        xorg_list_del(&q->list);
        q->abort(q->data);
        free(q);
}

/**
 * Abort all queued entries on a specific scrn, used
 * when resetting the X server
 */
static void
tegra_drm_abort_scrn(ScrnInfoPtr scrn)
{
    struct tegra_drm_queue *q, *tmp;

    xorg_list_for_each_entry_safe(q, tmp, &tegra_drm_queue, list) {
        if (q->scrn == scrn)
            tegra_drm_abort_one(q);
    }
}

/**
 * Abort by drm queue sequence number.
 */
void
tegra_drm_abort_seq(ScrnInfoPtr scrn, uint32_t seq)
{
    struct tegra_drm_queue *q, *tmp;

    xorg_list_for_each_entry_safe(q, tmp, &tegra_drm_queue, list) {
        if (q->seq == seq) {
            tegra_drm_abort_one(q);
            break;
        }
    }
}

/*
 * Externally usable abort function that uses a callback to match a single
 * queued entry to abort
 */
void
tegra_drm_abort(ScrnInfoPtr scrn, Bool (*match)(void *data, void *match_data),
             void *match_data)
{
    struct tegra_drm_queue *q;

    xorg_list_for_each_entry(q, &tegra_drm_queue, list) {
        if (match(q->data, match_data)) {
            tegra_drm_abort_one(q);
            break;
        }
    }
}

/*
 * General DRM kernel handler. Looks for the matching sequence number in the
 * drm event queue and calls the handler for it.
 */
static void
tegra_drm_handler(int fd, uint32_t frame, uint32_t sec, uint32_t usec,
               void *user_ptr)
{
    struct tegra_drm_queue *q, *tmp;
    uint32_t user_data = (uint32_t) (intptr_t) user_ptr;

    xorg_list_for_each_entry_safe(q, tmp, &tegra_drm_queue, list) {
        if (q->seq == user_data) {
            uint64_t msc;

            msc = tegra_kernel_msc_to_crtc_msc(q->crtc, frame);
            xorg_list_del(&q->list);
            q->handler(msc, (uint64_t) sec * 1000000 + usec, q->data);
            free(q);
            break;
        }
    }
}

Bool
TegraVBlankScreenInit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    xorg_list_init(&tegra_drm_queue);

    tegra->event_context.version = 2;
    tegra->event_context.vblank_handler = tegra_drm_handler;
    tegra->event_context.page_flip_handler = tegra_drm_handler;

    /* We need to re-register the DRM fd for the synchronisation
     * feedback on every server generation, so perform the
     * registration within ScreenInit and not PreInit.
     */
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,19,0,0,0)
    SetNotifyFd(tegra->fd, tegra_drm_socket_handler, X_NOTIFY_READ, screen);
#else
    AddGeneralSocket(tegra->fd);
    RegisterBlockAndWakeupHandlers((BlockHandlerProcPtr)NoopDDA,
                                   tegra_drm_wakeup_handler, screen);
#endif

    return TRUE;
}

void
TegraVBlankScreenExit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);

    tegra_drm_abort_scrn(scrn);

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,19,0,0,0)
    RemoveNotifyFd(tegra->fd);
#else
    RemoveBlockAndWakeupHandlers((BlockHandlerProcPtr)NoopDDA,
                                 tegra_drm_wakeup_handler, screen);
    RemoveGeneralSocket(tegra->fd);
#endif
}
