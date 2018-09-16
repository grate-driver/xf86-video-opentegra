/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
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
 *
 *
 * Author: Alan Hourihane <alanh@tungstengraphics.com>
 *
 */

#ifndef OPENTEGRA_DRIVER_H
#define OPENTEGRA_DRIVER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UDEV
#include <libudev.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include <lz4.h>
#include <png.h>
#include <turbojpeg.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libdrm/drm.h>
#include <libdrm/drm_fourcc.h>
#include <libdrm/tegra_drm.h>

#include <X11/extensions/Xv.h>
#include <X11/extensions/randr.h>
#include <X11/Xatom.h>

/* DPMS */
#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include <xorg/compiler.h>
#include <xorg/cursorstr.h>
#include <xorg/damage.h>
#include <xorg/dri2.h>
#include <xorg/exa.h>
#include <xorg/fb.h>
#include <xorg/fourcc.h>
#include <xorg/list.h>
#include <xorg/mipointer.h>
#include <xorg/micmap.h>
#include <xorg/shadow.h>
#include <xorg/xorgVersion.h>
#include <xorg/xorg-server.h>
#include <xorg/xf86.h>
#include <xorg/xf86cmap.h>
#include <xorg/xf86Crtc.h>
#include <xorg/xf86DDC.h>
#include <xorg/xf86_OSproc.h>
#include <xorg/xf86str.h>
#include <xorg/xf86xv.h>

#ifdef XSERVER_PLATFORM_BUS
#include <xorg/xf86platformBus.h>
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "common_helpers.h"
#include "compat-api.h"
#include "drmmode_display.h"
#include "drm_plane.h"
#include "tegra_stream.h"
#include "exa.h"
#include "host1x.h"
#include "vblank.h"
#include "xv.h"
#include "tgr_3d.xml.h"
#include "shaders/prog.h"
#include "gr3d.h"

#ifdef LONG64
#  define FMT_CARD32 "x"
#else
#  define FMT_CARD32 "lx"
#endif

#define TEGRA_ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

#define __TEGRA_ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define TEGRA_ALIGN(x, a)           __TEGRA_ALIGN_MASK(x, (typeof(x))(a) - 1)

#define TEGRA_PITCH_ALIGN(width, bpp, align)    \
    TEGRA_ALIGN(width * ((bpp + 7) / 8), align)

#define TEGRA_CONTAINER_OFFSETOF(TYPE, MEMBER) \
    ((size_t) &((TYPE *)0)->MEMBER)

#define TEGRA_CONTAINER_OF(ptr, type, member) ({                            \
        const typeof(((type *)0)->member) *__mptr = (ptr);                  \
        (type *)((char *)__mptr - TEGRA_CONTAINER_OFFSETOF(type, member));  \
    })

typedef struct
{
    int lastInstance;
    int refCount;
    ScrnInfoPtr pScrn_1;
    ScrnInfoPtr pScrn_2;
} EntRec, *EntPtr;

typedef struct _TegraRec
{
    char *path;
    int fd;

    EntPtr entityPrivate;
    EntityInfoPtr pEnt;

    CloseScreenProcPtr CloseScreen;

    /* Broken-out options. */
    OptionInfoPtr Options;

    CreateScreenResourcesProcPtr createScreenResources;
    ScreenBlockHandlerProcPtr BlockHandler;
    void *driver;

    drmmode_rec drmmode;

    drmEventContext event_context;

    DamagePtr damage;
    Bool dirty_enabled;

    uint32_t cursor_width, cursor_height;

    TegraEXAPtr exa;

    Bool dri2_enabled;

    struct drm_tegra *drm;

    Bool exa_compress_png;
    int exa_compress_jpeg_quality;
    Bool exa_compress_jpeg;
    Bool exa_compress_lz4;
    Bool exa_refrigerator;
    Bool exa_pool_alloc;
    Bool exa_compositing;
    Bool exa_enabled;
} TegraRec, *TegraPtr;

#define TegraPTR(p) ((TegraPtr)((p)->driverPrivate))

void TegraXvScreenInit(ScreenPtr pScreen);

void TegraEXAScreenInit(ScreenPtr pScreen);
void TegraEXAScreenExit(ScreenPtr pScreen);

void TegraDRI2ScreenInit(ScreenPtr pScreen);
void TegraDRI2ScreenExit(ScreenPtr pScreen);

Bool TegraVBlankScreenInit(ScreenPtr screen);
void TegraVBlankScreenExit(ScreenPtr screen);

#endif

/* vim: set et sts=4 sw=4 ts=4: */
