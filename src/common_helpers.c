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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <drm.h>
#include <xorg-server.h>
#include <xf86.h>
#include <xf86drm.h>
#include <xf86Crtc.h>

#include "compat-api.h"
#include "common_helpers.h"
#include "drmmode_display.h"

void tegra_box_intersect(BoxPtr dest, BoxPtr a, BoxPtr b)
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

void tegra_crtc_box(xf86CrtcPtr crtc, BoxPtr crtc_box)
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

int tegra_box_area(BoxPtr box)
{
    return (int)(box->x2 - box->x1) * (int)(box->y2 - box->y1);
}

Bool
tegra_crtc_on(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    return crtc->enabled && drmmode_crtc->dpms_mode == DPMSModeOn;
}

int tegra_crtc_coverage(DrawablePtr pDraw, int crtc_id)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86CrtcPtr crtc;
    BoxRec box, crtc_box, cover_box;

    if (crtc_id >= xf86_config->num_crtc)
        return 0;

    crtc = xf86_config->crtc[crtc_id];

    /* If the CRTC is off, treat it as not covering */
    if (!tegra_crtc_on(crtc))
        return 0;

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;

    tegra_crtc_box(crtc, &crtc_box);
    tegra_box_intersect(&cover_box, &crtc_box, &box);

    return tegra_box_area(&cover_box);
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
tegra_crtc_covering_drawable(DrawablePtr pDraw)
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
