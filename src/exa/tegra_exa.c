/*
 * Copyright © 2014 NVIDIA Corporation
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

#include "tegra_exa.h"

#include "helpers.c"
#include "copy_2d.c"
#include "solid_2d.c"
#include "mm_pool.c"
#include "composite_2d.c"
#include "composite_3d.c"
#include "composite.c"
#include "cpu_access.c"
#include "load_screen.c"
#include "mm.c"
#include "mm_fridge.c"
#include "optimizations_2d.c"
#include "optimizations_3d.c"
#include "pixmap.c"

uint64_t tegra_profiler_seqno;

static PixmapPtr tegra_exa_get_drawable_pixmap(DrawablePtr drawable)
{
    if (drawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr) drawable;

    return NULL;
}

static int tegra_exa_create_picture(PicturePtr picture)
{
    PixmapPtr pixmap = tegra_exa_get_drawable_pixmap(picture->pDrawable);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(picture->pDrawable->pScreen);
    struct tegra_exa *exa = TegraPTR(pScrn)->exa;

    if (pixmap) {
        struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pixmap);
        priv->picture_format = picture->format;
    }

    if (exa->create_picture)
        return exa->create_picture(picture);

    return Success;
}

static void tegra_exa_block_handler(BLOCKHANDLER_ARGS_DECL)
{
    SCREEN_PTR(arg);
    TegraPtr tegra = TegraPTR(xf86ScreenToScrn(pScreen));
    struct tegra_exa *exa = tegra->exa;
    struct timespec time;

    pScreen->BlockHandler = exa->block_handler;
    pScreen->BlockHandler(BLOCKHANDLER_ARGS);
    pScreen->BlockHandler = tegra_exa_block_handler;

    clock_gettime(CLOCK_MONOTONIC, &time);
    tegra_exa_freeze_pixmaps(tegra, time.tv_sec);

    drm_tegra_bo_cache_cleanup(tegra->drm, time.tv_sec);
    tegra_exa_clean_up_pixmaps_freelist(tegra, false);
}

static void tegra_exa_wrap_proc(ScreenPtr pScreen)
{
    PictureScreenPtr ps = GetPictureScreenIfSet(pScreen);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct tegra_exa *exa = TegraPTR(pScrn)->exa;

    if (ps) {
        exa->create_picture = ps->CreatePicture;
        ps->CreatePicture = tegra_exa_create_picture;
    }

    exa->block_handler = pScreen->BlockHandler;
    pScreen->BlockHandler = tegra_exa_block_handler;
}

static void tegra_exa_unwrap_proc(ScreenPtr pScreen)
{
    PictureScreenPtr ps = GetPictureScreenIfSet(pScreen);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct tegra_exa *exa = TegraPTR(pScrn)->exa;

    if (ps)
        ps->CreatePicture = exa->create_picture;

    pScreen->BlockHandler = exa->block_handler;
}

static bool host1x_firewall_is_present(struct tegra_exa *tegra)
{
    tegra_stream_begin(tegra->cmds, tegra->gr2d);
    tegra_stream_prep(tegra->cmds, 1);
    tegra_stream_push_setclass(tegra->cmds, HOST1X_CLASS_HOST1X);
    tegra_stream_end(tegra->cmds);

    if (tegra_stream_flush(tegra->cmds, NULL) == -EINVAL)
        return true;

    return false;
}

static int tegra_exa_init_gpu(TegraPtr tegra, struct tegra_exa *exa)
{
    int err;

    err = drm_tegra_channel_open(&exa->gr2d, tegra->drm, DRM_TEGRA_GR2D);
    if (err) {
        ERROR_MSG("failed to open 2D channel: %d\n", err);
        return err;
    }

    err = drm_tegra_channel_open(&exa->gr3d, tegra->drm, DRM_TEGRA_GR3D);
    if (err) {
        ERROR_MSG("failed to open 3D channel: %d\n", err);
        goto close_gr2d;
    }

    err = tegra_stream_create(&exa->cmds, tegra);
    if (err) {
        ERROR_MSG("failed to create command stream: %d\n", err);
        goto close_gr3d;
    }

    tegra_exa_3d_state_reset(&exa->gr3d_state);

    return 0;

close_gr3d:
    drm_tegra_channel_close(exa->gr3d);
close_gr2d:
    drm_tegra_channel_close(exa->gr2d);

    return err;
}

static void tegra_exa_deinit_gpu(struct tegra_exa *exa)
{
    tegra_exa_3d_state_reset(&exa->gr3d_state);
    tegra_stream_destroy(exa->cmds);
    drm_tegra_channel_close(exa->gr2d);
    drm_tegra_channel_close(exa->gr3d);
}

static void tegra_exa_init_features(ScrnInfoPtr scrn, struct tegra_exa *exa,
                                    int drm_ver)
{
    if (drm_ver >= GRATE_KERNEL_DRM_VERSION) {
        exa->default_drm_bo_flags = DRM_TEGRA_GEM_CREATE_DONT_KMAP;
        INFO_MSG(scrn, "EXA using GEM DONT_KMAP\n");
    }

    if (drm_ver >= GRATE_KERNEL_DRM_VERSION + 1) {
        /*
         * Just print message without using sparse allocation for the large
         * pool because it will hog most of GART aperture.
         */
        INFO_MSG(scrn, "EXA using GEM CREATE_SPARSE\n");
    }

    /*
     * Upstream kernel has an unfixed race-condition bug in the SMMU driver
     * that results in a kernel panic if host1x firewall is disabled in the
     * kernel's configuration.
     *
     * We may try to workaround the bug by fencing all jobs, which should
     * reduce number of race condition in the kernel driver. This doesn't
     * prevent the problem if userspace uses GPU in parallel to Opentegra,
     * nothing we could do about it here, kernel fix is required.
     */
    if (drm_ver == 0) {
        bool firewalled = host1x_firewall_is_present(exa);

        INFO_MSG(scrn, "Host1x firewall %s\n",
                 firewalled ? "detected, ignore previous error" : "undetected");

        exa->has_iommu_bug = !firewalled;
    }
}

static int tegra_exa_preinit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    struct tegra_exa *exa;
    int drm_ver;
    int err;

    drm_ver = drm_tegra_version(tegra->drm);
    INFO_MSG(scrn, "Tegra DRM kernel version %d\n", drm_ver);

    if (!tegra->exa_enabled)
        return -1;

    exa = calloc(1, sizeof(*exa));
    if (!exa) {
        ERROR_MSG("Tegra EXA allocation failed\n");
        return -1;
    }

    exa->scratch.drm = tegra->drm;

    /* tegra->exa is used by MM initialization, so set it early */
    tegra->exa = exa;

    err = tegra_exa_init_gpu(tegra, exa);
    if (err) {
        ERROR_MSG("failed to initialize GPU: %d\n", err);
        goto free_priv;
    }

    err = tegra_exa_init_optimizations(tegra, exa);
    if (err) {
        ERROR_MSG("failed to initialize memory management: %d\n", err);
        goto deinit_gpu;
    }

    err = tegra_exa_init_mm(tegra, exa);
    if (err) {
        ERROR_MSG("failed to initialize drawing optimizations: %d\n", err);
        goto deinit_optimization;
    }

    tegra_exa_init_features(scrn, exa, drm_ver);

    return 0;

deinit_optimization:
    tegra_exa_deinit_optimizations(exa);
deinit_gpu:
    tegra_exa_deinit_gpu(exa);
free_priv:
    free(exa);

    tegra->exa = NULL;
    return err;
}

static void tegra_exa_finalize_init(ScreenPtr screen)
{
    tegra_exa_wrap_proc(screen);
}

static void tegra_exa_deinit(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TegraPtr tegra = TegraPTR(scrn);
    struct tegra_exa *exa = tegra->exa;

    tegra_exa_unwrap_proc(screen);
    tegra_exa_release_mm(tegra, exa);
    tegra_exa_deinit_optimizations(exa);
    tegra_exa_deinit_gpu(exa);
    free(exa);

    tegra->exa = NULL;
}

/* vim: set et sts=4 sw=4 ts=4: */
