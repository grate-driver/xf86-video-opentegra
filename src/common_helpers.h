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

#ifndef __TEGRA_COMMON_HELPERS_H
#define __TEGRA_COMMON_HELPERS_H

void tegra_box_intersect(BoxPtr dest, BoxPtr a, BoxPtr b);

void tegra_crtc_box(xf86CrtcPtr crtc, BoxPtr crtc_box);

int tegra_box_area(BoxPtr box);

Bool tegra_crtc_on(xf86CrtcPtr crtc);

int tegra_crtc_coverage(DrawablePtr pDraw, int crtc_id);

xf86CrtcPtr tegra_crtc_covering_drawable(DrawablePtr pDraw);

xf86CrtcPtr tegra_covering_crtc(ScrnInfoPtr scrn, BoxPtr box,
                                xf86CrtcPtr desired, BoxPtr crtc_box_ret);

#endif
