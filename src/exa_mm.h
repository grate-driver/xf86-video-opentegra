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

#ifndef __TEGRA_EXA_MM_H
#define __TEGRA_EXA_MM_H

static void TegraEXADestroyPool(TegraPixmapPoolPtr pool);

static void TegraEXAPoolFree(struct mem_pool_entry *pool_entry);

static Bool TegraEXAAllocateDRMFromPool(TegraPtr tegra,
                                        TegraPixmapPtr pixmap,
                                        unsigned int size);

static Bool TegraEXAAllocateDRM(TegraPtr tegra,
                                TegraPixmapPtr pixmap,
                                unsigned int size);

static Bool TegraEXAAllocateMem(TegraPixmapPtr pixmap, unsigned int size);

static int TegraEXAInitMM(TegraPtr tegra, TegraEXAPtr exa);
static void TegraEXAReleaseMM(TegraPtr tegra, TegraEXAPtr exa);

static void TegraEXACoolTegraPixmap(TegraPtr tegra, TegraPixmapPtr pix);
static void TegraEXACoolPixmap(PixmapPtr pPixmap, Bool write);
static void TegraEXAThawPixmap(PixmapPtr pPixmap, Bool accel);
static void TegraEXAFreezePixmaps(TegraPtr tegra, time_t time_sec);

#endif
