/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2011 Dave Airlie
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
 * Original Author: Alan Hourihane <alanh@tungstengraphics.com>
 * Rewrite: Dave Airlie <airlied@redhat.com>
 *
 */

#include "driver.h"

struct xf86_platform_device;

static SymTabRec Chipsets[] = {
    { 0, "kms" },
    { -1, NULL }
};

typedef enum
{
    OPTION_SW_CURSOR,
    OPTION_DEVICE_PATH,
    OPTION_EXA_DISABLED,
    OPTION_EXA_COMPOSITING,
    OPTION_EXA_POOL_ALLOC,
    OPTION_EXA_REFRIGERATOR,
    OPTION_EXA_COMPRESSION_LZ4,
    OPTION_EXA_COMPRESSION_JPEG,
    OPTION_EXA_COMPRESSION_JPEG_QUALITY,
    OPTION_EXA_COMPRESSION_PNG,
    OPTION_EXA_ERASE_PIXMAPS,
} TegraOptions;

static const OptionInfoRec Options[] = {
    { OPTION_SW_CURSOR, "SWcursor", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_DEVICE_PATH, "device", OPTV_STRING, { 0 }, FALSE },
    { OPTION_EXA_DISABLED, "NoAccel", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_COMPOSITING, "AccelCompositing", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_POOL_ALLOC, "DisablePoolAllocator", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_REFRIGERATOR, "DisablePixmapRefrigerator", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_COMPRESSION_LZ4, "DisableCompressionLZ4", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_COMPRESSION_JPEG, "DisableCompressionJPEG", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_COMPRESSION_JPEG_QUALITY, "JPEGCompressionQuality", OPTV_INTEGER, { 0 }, FALSE },
    { OPTION_EXA_COMPRESSION_PNG, "DisableCompressionPNG", OPTV_BOOLEAN, { 0 }, FALSE },
    { OPTION_EXA_ERASE_PIXMAPS, "SecureErasePixmaps", OPTV_BOOLEAN, { 0 }, FALSE },
    { -1, NULL, OPTV_NONE, { 0 }, FALSE }
};

int tegraEntityIndex = -1;

static Bool
GetRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate)
        return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(TegraRec), 1);

    return TRUE;
}

static void
FreeRec(ScrnInfoPtr pScrn)
{
    TegraPtr tegra;

    if (!pScrn)
        return;

    tegra = TegraPTR(pScrn);
    if (!tegra)
        return;
    pScrn->driverPrivate = NULL;

    if (tegra->fd >= 0) {
        drm_tegra_close(tegra->drm);
        close(tegra->fd);
    }

    free(tegra->Options);
    free(tegra);
}

static void
TegraIdentify(int flags)
{
    xf86PrintChipsets("opentegra", "Open Source Driver for NVIDIA Tegra",
                      Chipsets);
}

static int
TegraCheckHardware(int fd)
{
    enum drm_tegra_soc_id soc_id = DRM_TEGRA_INVALID_SOC;
    struct drm_tegra_channel *channel = NULL;
    struct drm_tegra *drm = NULL;
    static Bool verbose = TRUE;
    int err;

    err = drm_tegra_new(&drm, fd);
    if (!err) {
        /* 2D channel presents only on Tegra SoCs prior to TK1 */
        err = drm_tegra_channel_open(&channel, drm, DRM_TEGRA_GR2D);
        if (err) {
            if (verbose) {
                xf86DrvMsg(-1, X_ERROR, "%s: failed to open 2D channel: %d\n",
                           __func__, err);

                xf86DrvMsg(-1, X_ERROR,
                           "%s: make sure that CONFIG_DRM_TEGRA_STAGING is enabled in the kernel config\n",
                           __func__);
            }
        } else {
            drm_tegra_channel_close(channel);
        }

        soc_id = drm_tegra_get_soc_id(drm);
        drm_tegra_close(drm);
    }

    if (verbose) {
        xf86DrvMsg(-1, X_INFO, "%s: Tegra20/30/114 DRM support %s\n",
                   __func__, err ? "undetected" : "detected");

        xf86DrvMsg(-1, X_INFO, "%s: SoC ID: %s\n",
                   __func__, drm_tegra_soc_names[soc_id]);

        /* print messages only once */
        verbose = FALSE;
    }

    return err;
}

static int
TegraOpenHardware(const char *dev)
{
    int err, fd;

    if (dev)
        fd = open(dev, O_RDWR | O_CLOEXEC, 0);
    else {
        dev = getenv("KMSDEVICE");
        if ((dev == NULL) || ((fd = open(dev, O_RDWR | O_CLOEXEC, 0)) == -1)) {
            fd = drmOpen("tegra", NULL);
        }
    }

    if (fd < 0) {
        xf86DrvMsg(-1, X_ERROR, "%s: open %s: %s\n",
                   dev, strerror(errno), __func__);
    } else {
        err = TegraCheckHardware(fd);
        if (err) {
            close(fd);
            fd = -1;
        }
    }

    return fd;
}

static Bool
TegraProbeHardware(const char *dev, struct xf86_platform_device *platform_dev)
{
    int fd;

#ifdef XSERVER_PLATFORM_BUS
#ifdef XF86_PDEV_SERVER_FD
    if (platform_dev && (platform_dev->flags & XF86_PDEV_SERVER_FD)) {
        fd = xf86_get_platform_device_int_attrib(platform_dev, ODEV_ATTRIB_FD, -1);
        if (fd == -1)
            return FALSE;
        return TRUE;
    }
#endif
#endif

    fd = TegraOpenHardware(dev);
    if (fd >= 0) {
        close(fd);
        return TRUE;
    }

    return FALSE;
}

static const OptionInfoRec *
TegraAvailableOptions(int chipid, int busid)
{
    return Options;
}

static Bool
TegraDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, void *data)
{
    xorgHWFlags *flag;

    switch (op) {
        case GET_REQUIRED_HW_INTERFACES:
            flag = (CARD32 *)data;
            (*flag) = 0;
            return TRUE;
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,15,99,902,0)
        case SUPPORTS_SERVER_FDS:
            return TRUE;
#endif
        default:
            return FALSE;
    }
}

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif

#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

static Bool
TegraPreInit(ScrnInfoPtr pScrn, int flags)
{
    TegraPtr tegra;
    rgb defaultWeight = { 0, 0, 0 };
    EntityInfoPtr pEnt;
    EntPtr tegraEnt = NULL;
    uint64_t value = 0;
    int ret;
    int bppflags;
    int defaultdepth, defaultbpp;
    Gamma zeros = { 0.0, 0.0, 0.0 };
    const char *path;

    if (pScrn->numEntities != 1)
        return FALSE;

    pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    /* Allocate driverPrivate */
    if (!GetRec(pScrn))
        return FALSE;

    tegra = TegraPTR(pScrn);
    tegra->pEnt = pEnt;

    pScrn->displayWidth = 640; /* default it */

    /* Allocate an entity private if necessary */
    if (xf86IsEntityShared(pScrn->entityList[0])) {
        tegraEnt = xf86GetEntityPrivate(pScrn->entityList[0],
                                        tegraEntityIndex)->ptr;
        tegra->entityPrivate = tegraEnt;
    } else
        tegra->entityPrivate = NULL;

    if (xf86IsEntityShared(pScrn->entityList[0])) {
        if (xf86IsPrimInitDone(pScrn->entityList[0])) {
            /* do something */
        } else {
            xf86SetPrimInitDone(pScrn->entityList[0]);
        }
    }

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->progClock = TRUE;
    pScrn->rgbBits = 8;

    switch (pEnt->location.type) {
#ifdef XSERVER_PLATFORM_BUS
    case BUS_PLATFORM:
#ifdef XF86_PDEV_SERVER_FD
        if (pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD) {
            tegra->fd = xf86_get_platform_device_int_attrib(
                                    pEnt->location.id.plat, ODEV_ATTRIB_FD, -1);
            tegra->fd = dup(tegra->fd);
        } else
#endif
        {
            path = xf86_get_platform_device_attrib(pEnt->location.id.plat,
                                                   ODEV_ATTRIB_PATH);
            tegra->fd = TegraOpenHardware(path);
        }
        break;
#endif

    default:
        path = xf86GetOptValString(tegra->pEnt->device->options,
                                   OPTION_DEVICE_PATH);
        tegra->fd = TegraOpenHardware(path);
        break;
    }

    if (tegra->fd < 0)
        return FALSE;

    ret = drm_tegra_new(&tegra->drm, tegra->fd);
    if (ret < 0) {
        close(tegra->fd);
        return FALSE;
    }

    tegra->path = drmGetDeviceNameFromFd(tegra->fd);
    tegra->drmmode.fd = tegra->fd;

    drmmode_get_default_bpp(pScrn, &tegra->drmmode, &defaultdepth, &defaultbpp);
    if (defaultdepth == 24 && defaultbpp == 24)
        bppflags = SupportConvert32to24 | Support24bppFb;
    else
        bppflags = PreferConvert24to32 | SupportConvert24to32 | Support32bppFb;

    if (!xf86SetDepthBpp(pScrn, defaultdepth, defaultdepth, defaultbpp,
                         bppflags))
        return FALSE;

    switch (pScrn->depth) {
    case 15:
    case 16:
    case 24:
        break;

    default:
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Given depth (%d) is not supported by the driver\n",
                   pScrn->depth);
        return FALSE;
    }

    xf86PrintDepthBpp(pScrn);

    /* Process the options */
    xf86CollectOptions(pScrn, NULL);

    tegra->Options = malloc(sizeof(Options));
    if (!tegra->Options)
        return FALSE;

    memcpy(tegra->Options, Options, sizeof(Options));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, tegra->Options);

    if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight))
        return FALSE;

    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    if (xf86ReturnOptValBool(tegra->Options, OPTION_SW_CURSOR,
            XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1,15,99,902,0)))
        tegra->drmmode.sw_cursor = TRUE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "HW Cursor: enabled %s\n",
               tegra->drmmode.sw_cursor ? "NO" : "YES");

    tegra->cursor_width = 64;
    tegra->cursor_height = 64;
    ret = drmGetCap(tegra->fd, DRM_CAP_CURSOR_WIDTH, &value);
    if (!ret) {
        tegra->cursor_width = value;
    }
    ret = drmGetCap(tegra->fd, DRM_CAP_CURSOR_HEIGHT, &value);
    if (!ret) {
        tegra->cursor_height = value;
    }

    if (!drmmode_pre_init(pScrn, &tegra->drmmode, pScrn->bitsPerPixel / 8)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "KMS setup failed\n");
        return FALSE;
    }

    /*
     * If the driver can do gamma correction, it should call xf86SetGamma() here.
     */
    if (!xf86SetGamma(pScrn, zeros))
        return FALSE;

    if (pScrn->modes == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
        return FALSE;
    }

    pScrn->currentMode = pScrn->modes;

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);

    tegra->exa_enabled = !xf86ReturnOptValBool(tegra->Options,
                                               OPTION_EXA_DISABLED,
                                               FALSE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "EXA HW Acceleration: enabled %s\n",
               tegra->exa_enabled ? "YES" : "NO");

    if (tegra->exa_enabled) {
        tegra->exa_compositing = xf86ReturnOptValBool(tegra->Options,
                                                      OPTION_EXA_COMPOSITING,
                                                      TRUE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA Compositing: enabled %s\n",
                   tegra->exa_compositing ? "YES" : "NO");

        tegra->exa_pool_alloc = !xf86ReturnOptValBool(tegra->Options,
                                                      OPTION_EXA_POOL_ALLOC,
                                                      FALSE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA pool allocator: enabled %s\n",
                   tegra->exa_pool_alloc ? "YES" : "NO");

        tegra->exa_refrigerator = !xf86ReturnOptValBool(tegra->Options,
                                                        OPTION_EXA_REFRIGERATOR,
                                                        FALSE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA pixmap refrigerator: enabled %s\n",
                   tegra->exa_refrigerator ? "YES" : "NO");

#ifdef HAVE_LZ4
        tegra->exa_compress_lz4 = !xf86ReturnOptValBool(tegra->Options,
                                                    OPTION_EXA_COMPRESSION_LZ4,
                                                    FALSE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA LZ4 compression: enabled %s\n",
                   tegra->exa_compress_lz4 ? "YES" : "NO");
#endif

#ifdef HAVE_JPEG
        tegra->exa_compress_jpeg = !xf86ReturnOptValBool(tegra->Options,
                                                    OPTION_EXA_COMPRESSION_JPEG,
                                                    TRUE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA JPEG compression: enabled %s\n",
                   tegra->exa_compress_jpeg ? "YES" : "NO");

        tegra->exa_compress_jpeg_quality = xf86ReturnOptValBool(tegra->Options,
                                            OPTION_EXA_COMPRESSION_JPEG_QUALITY,
                                            75);

        tegra->exa_compress_jpeg_quality = min(100, tegra->exa_compress_jpeg_quality);
        tegra->exa_compress_jpeg_quality = max(  1, tegra->exa_compress_jpeg_quality);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA JPEG compression quality: %d\n",
                   tegra->exa_compress_jpeg_quality);
#endif

#ifdef HAVE_PNG
        tegra->exa_compress_png = !xf86ReturnOptValBool(tegra->Options,
                                                    OPTION_EXA_COMPRESSION_PNG,
                                                    FALSE);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                  "EXA PNG compression: enabled %s\n",
                   tegra->exa_compress_png ? "YES" : "NO");
#endif
    }

    tegra->exa_erase_pixmaps = xf86ReturnOptValBool(tegra->Options,
                                                    OPTION_EXA_ERASE_PIXMAPS,
                                                    FALSE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "EXA secure erase pixmaps: enabled %s\n",
                tegra->exa_erase_pixmaps ? "YES" : "NO");

    /* Load the required sub modules */
    if (!xf86LoadSubModule(pScrn, "dri2") ||
        !xf86LoadSubModule(pScrn, "fb"))
        return FALSE;

    if (tegra->exa_enabled && !xf86LoadSubModule(pScrn, "exa"))
        return FALSE;

    return TRUE;
}

static Bool
SetMaster(ScrnInfoPtr pScrn)
{
    TegraPtr tegra = TegraPTR(pScrn);
    int ret = 0, err, err2 = 0;

#ifdef XF86_PDEV_SERVER_FD
    if (tegra->pEnt->location.type == BUS_PLATFORM &&
            (tegra->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
        goto get_atomic_caps;
#endif

    ret = drmSetMaster(tegra->fd);
    if (ret)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "drmSetMaster failed: %s\n",
                   strerror(errno));

get_atomic_caps:
#ifdef HAVE_DRM_MODE_ATOMIC
    err = drmSetClientCap(tegra->fd, DRM_CLIENT_CAP_ATOMIC, 2);
    if (err < 0)
        err2 = drmSetClientCap(tegra->fd, DRM_CLIENT_CAP_ATOMIC, 1);
#endif
    if (err < 0 && err2 < 0)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "drmSetClientCap failed: %s %s\n",
                   strerror(err), strerror(err2));

    return ret == 0;
}

/*
 * This gets called when gaining control of the VT, and from ScreenInit().
 */
static Bool
TegraEnterVT(VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    TegraPtr tegra = TegraPTR(pScrn);

    pScrn->vtSema = TRUE;

    SetMaster(pScrn);

    if (!drmmode_set_desired_modes(pScrn, &tegra->drmmode))
        return FALSE;

    return TRUE;
}

static void
TegraLeaveVT(VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    TegraPtr tegra = TegraPTR(pScrn);
    xf86_hide_cursors(pScrn);

    pScrn->vtSema = FALSE;

#ifdef XF86_PDEV_SERVER_FD
    if (tegra->pEnt->location.type == BUS_PLATFORM &&
            (tegra->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
        return;
#endif

    drmDropMaster(tegra->fd);
}

static Bool
TegraCreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    PixmapPtr rootPixmap;
    Bool ret;
    void *pixels;

    pScreen->CreateScreenResources = tegra->createScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = TegraCreateScreenResources;

    if (!drmmode_set_desired_modes(pScrn, &tegra->drmmode))
      return FALSE;

    drmmode_uevent_init(pScrn, &tegra->drmmode);

    if (!tegra->drmmode.sw_cursor)
        drmmode_map_cursor_bos(pScrn, &tegra->drmmode);

    pixels = drmmode_map_front_bo(&tegra->drmmode);
    if (!pixels)
        return FALSE;

    rootPixmap = pScreen->GetScreenPixmap(pScreen);

    if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
        FatalError("Couldn't adjust screen pixmap\n");

    return ret;
}

static Bool
TegraCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    drmmode_uevent_fini(pScrn, &tegra->drmmode);

    xf86_cursors_fini(pScreen);
    TegraDRI2ScreenExit(pScreen);
    TegraVBlankScreenExit(pScreen);
#ifdef HAVE_DRM_MODE_ATOMIC
    TegraXvScreenExit(pScreen);
#endif
    TegraEXAScreenExit(pScreen);

    drmmode_free_bos(pScrn, &tegra->drmmode);

    if (pScrn->vtSema)
        TegraLeaveVT(VT_FUNC_ARGS);

    pScrn->vtSema = FALSE;
    pScreen->CreateScreenResources = tegra->createScreenResources;
    pScreen->CloseScreen = tegra->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool
TegraScreenFbInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VisualPtr visual;

    if (!fbScreenInit(pScreen, NULL, pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
                      pScrn->bitsPerPixel))
        return FALSE;

    if (pScrn->bitsPerPixel > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;

        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    fbPictureInit(pScreen, NULL, 0);

    return TRUE;
}

static Bool
TegraScreenVisualsInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    miClearVisualTypes();

    if (!miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
                          pScrn->rgbBits, pScrn->defaultVisual))
        return FALSE;

    if (!miSetPixmapDepths())
        return FALSE;

    return TRUE;
}

static Bool
TegraScreenXf86Init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* Initialize software cursor.
     * Must precede creation of the default colormap. */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* Need to extend HWcursor support to handle mask interleave */
    if (!tegra->drmmode.sw_cursor)
        xf86_cursors_init(pScreen, tegra->cursor_width, tegra->cursor_height,
                          HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 |
                          HARDWARE_CURSOR_ARGB);

    pScreen->SaveScreen = xf86SaveScreen;

    if (serverGeneration == 1)
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    return TRUE;
}

static Bool
TegraScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);

    pScrn->pScreen = pScreen;

    if (!SetMaster(pScrn))
        return FALSE;

    pScrn->displayWidth = pScrn->virtualX; /* HW dependent - FIXME */
    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    /* Must force it before EnterVT, so we are in control of VT and
     * later memory should be bound when allocating, e.g rotate_mem */
    pScrn->vtSema = TRUE;

    if (!drmmode_create_initial_bos(pScrn, &tegra->drmmode))
        return FALSE;

    TegraScreenVisualsInit(pScreen);
    TegraScreenFbInit(pScreen);

    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

    tegra->createScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = TegraCreateScreenResources;

    tegra->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = TegraCloseScreen;

    if (!xf86CrtcScreenInit(pScreen))
        return FALSE;

    xf86SetBlackWhitePixels(pScreen);

    TegraEXAScreenInit(pScreen);

    if (!TegraScreenXf86Init(pScreen))
        return FALSE;

#ifdef HAVE_DRM_MODE_ATOMIC
    TegraXvScreenInit(pScreen);
#endif
    TegraVBlankScreenInit(pScreen);
    TegraDRI2ScreenInit(pScreen);

    if (!miCreateDefColormap(pScreen))
        return FALSE;

    return TegraEnterVT(VT_FUNC_ARGS);
}

static Bool
TegraSwitchMode(SWITCH_MODE_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);

    return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}

static void
TegraAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    TegraPtr tegra = TegraPTR(pScrn);

    drmmode_adjust_frame(pScrn, &tegra->drmmode, x, y);
}

static void
TegraFreeScreen(FREE_SCREEN_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    FreeRec(pScrn);
}

static ModeStatus
TegraValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode, Bool verbose, int flags)
{
    return MODE_OK;
}

#ifdef XSERVER_PLATFORM_BUS
static Bool
TegraPlatformProbe(DriverPtr driver, int entity_num, int flags,
                   struct xf86_platform_device *dev, intptr_t match_data)
{
    char *path = xf86_get_platform_device_attrib(dev, ODEV_ATTRIB_PATH);
    ScrnInfoPtr scrn = NULL;

    if (TegraProbeHardware(path, dev)) {
        scrn = xf86AllocateScreen(driver, 0);

        xf86AddEntityToScreen(scrn, entity_num);

        scrn->driverName = (char *)"opentegra";
        scrn->name = (char *)"opentegra";
        scrn->PreInit = TegraPreInit;
        scrn->ScreenInit = TegraScreenInit;
        scrn->SwitchMode = TegraSwitchMode;
        scrn->AdjustFrame = TegraAdjustFrame;
        scrn->EnterVT = TegraEnterVT;
        scrn->LeaveVT = TegraLeaveVT;
        scrn->FreeScreen = TegraFreeScreen;
        scrn->ValidMode = TegraValidMode;
    }

    return scrn != NULL;
}
#endif

static Bool
TegraProbe(DriverPtr drv, int flags)
{
    int i, numDevSections;
    GDevPtr *devSections;
    Bool foundScreen = FALSE;
    const char *dev;
    ScrnInfoPtr scrn = NULL;

    /* For now, just bail out for PROBE_DETECT. */
    if (flags & PROBE_DETECT)
        return FALSE;

    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice("opentegra", &devSections)) <= 0)
        return FALSE;

    for (i = 0; i < numDevSections; i++) {
        dev = xf86FindOptionValue(devSections[i]->options, "device");
        if (TegraProbeHardware(dev, NULL)) {
            int entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
            scrn = xf86ConfigFbEntity(scrn, 0, entity, NULL, NULL, NULL,
                                      NULL);
        }

        if (scrn) {
            foundScreen = TRUE;
            scrn->driverVersion = 1;
            scrn->driverName = (char *)"opentegra";
            scrn->name = (char *)"opentegra";
            scrn->Probe = TegraProbe;
            scrn->PreInit = TegraPreInit;
            scrn->ScreenInit = TegraScreenInit;
            scrn->SwitchMode = TegraSwitchMode;
            scrn->AdjustFrame = TegraAdjustFrame;
            scrn->EnterVT = TegraEnterVT;
            scrn->LeaveVT = TegraLeaveVT;
            scrn->FreeScreen = TegraFreeScreen;
            scrn->ValidMode = TegraValidMode;

            xf86DrvMsg(scrn->scrnIndex, X_INFO, "using %s\n",
                       dev ? dev : "default device");
        }
    }

    free(devSections);

    return foundScreen;
}

_X_EXPORT DriverRec tegra = {
    1,
    (char *)"opentegra",
    TegraIdentify,
    TegraProbe,
    TegraAvailableOptions,
    NULL,
    0,
    TegraDriverFunc,
    NULL,
    NULL,
#ifdef XSERVER_PLATFORM_BUS
    TegraPlatformProbe,
#endif
};

static MODULESETUPPROTO(Setup);

static pointer
Setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = 0;

    /*
     * This module should be loaded only once, but check to be sure.
     */
    if (!setupDone) {
        setupDone = 1;
        xf86AddDriver(&tegra, module, HaveDriverFuncs);

        /*
         * The return value must be non-NULL on success even though there
         * is no TearDownProc.
         */
        return (pointer)1;
    } else {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;

        return NULL;
    }
}

static XF86ModuleVersionInfo VersRec = {
    "opentegra",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    { 0, 0, 0, 0 }
};

_X_EXPORT XF86ModuleData opentegraModuleData = { &VersRec, Setup, NULL };

/* vim: set et sts=4 sw=4 ts=4: */


