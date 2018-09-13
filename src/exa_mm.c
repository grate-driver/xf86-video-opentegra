/*
 * Copyright (c) Dmitry Osipenko
 * Copyright (c) Erik Faye-Lund
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "driver.h"
#include "exa_mm.h"

#define ErrorMsg(fmt, args...)                                              \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, __FILE__,                   \
               __LINE__, __func__, ##args)

Bool TegraEXAAllocateDRM(TegraPtr tegra,
                         TegraPixmapPtr pixmap,
                         unsigned int size)
{
    int err;

    if (!pixmap->accel && !pixmap->dri)
        return FALSE;

    err = drm_tegra_bo_new(&pixmap->bo, tegra->drm, 0, size);
    if (err)
        return FALSE;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_BO;

    return TRUE;
}

Bool TegraEXAAllocateMem(TegraPixmapPtr pixmap, unsigned int size)
{
    if (pixmap->dri)
        return FALSE;

    pixmap->fallback = malloc(size);

    if (!pixmap->fallback)
        return FALSE;

    pixmap->type = TEGRA_EXA_PIXMAP_TYPE_FALLBACK;

    return TRUE;
}

int TegraEXAInitMM(TegraPtr tegra, TegraEXAPtr exa)
{
    xorg_list_init(&exa->cool_pixmaps);
    xorg_list_init(&exa->mem_pools);

    return 0;
}

void TegraEXAReleaseMM(TegraEXAPtr exa)
{
    if (!xorg_list_is_empty(&exa->mem_pools))
        ErrorMsg("FATAL: Memory leak! Unreleased memory pools\n");

    if (!xorg_list_is_empty(&exa->cool_pixmaps))
        ErrorMsg("FATAL: Memory leak! Cooled pixmaps\n");
}
