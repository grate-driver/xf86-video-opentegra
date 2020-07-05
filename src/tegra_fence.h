/*
 * Copyright (c) GRATE-DRIVER project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef TEGRA_FENCE_H_
#define TEGRA_FENCE_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef DEBUG
#define FENCE_DEBUG
#endif

#define FENCE_DEBUG_VERBOSE     0

#define TEGRA_FENCE_DEBUG_MSG(F, FDSC)                                  \
({                                                                      \
    if (FENCE_DEBUG_VERBOSE && F)                                       \
        printf("%s:%d: fdbg: %s: f=%p 2d=%d cnt=%d\n",                  \
               __func__, __LINE__, FDSC, F, (F)->gr2d, (F)->refcnt);    \
})

#define TEGRA_FENCE_ERR_MSG(fmt, args...) \
    fprintf(stderr, "%s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)

struct tegra_fence {
    uint64_t seqno;
    void *opaque;
    int refcnt;
    bool gr2d;

    bool (*wait_fence)(struct tegra_fence *f);
    bool (*free_fence)(struct tegra_fence *f);

#ifdef FENCE_DEBUG
    bool bug0;
    bool bug1;
    bool released;
#endif
};

static inline void tegra_fence_validate(struct tegra_fence *f)
{
#ifdef FENCE_DEBUG
    if (f) {
        if (f->released)
            assert(f->refcnt == -1);
        else
            assert(f->refcnt >= 0);
        assert(!f->bug0);
        assert(f->bug1);
    }
#endif
}

static inline struct tegra_fence *
tegra_fence_get(struct tegra_fence *f, void *opaque)
{
    if (f) {
        tegra_fence_validate(f);
        if (opaque)
            f->opaque = opaque;
        f->refcnt++;
    }

    return f;
}
#define TEGRA_FENCE_GET(F, OPAQUE) \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "ref"); tegra_fence_get(F, OPAQUE); })

static inline bool tegra_fence_wait(struct tegra_fence *f)
{
    if (f) {
        tegra_fence_validate(f);
        return f->wait_fence(f);
    }

    return false;
}
#define TEGRA_FENCE_WAIT(F) \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "wait"); tegra_fence_wait(F); })

static inline void tegra_fence_free(struct tegra_fence *f)
{
    if (f) {
        tegra_fence_validate(f);
#ifdef FENCE_DEBUG
        f->refcnt = -100;
#endif
        if (!f->free_fence(f)) {
#ifdef FENCE_DEBUG
            f->refcnt = -1;
#endif
        }
    }
}
#define TEGRA_FENCE_FREE(F) \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "free"); tegra_fence_free(F); })

static inline void tegra_fence_finish(struct tegra_fence *f)
{
    if (f) {
#ifdef FENCE_DEBUG
        if (f->refcnt == -1)
            f->released = true;
#endif
        tegra_fence_validate(f);

        if (f->refcnt == -1)
            TEGRA_FENCE_FREE(f);
    }
}
#define TEGRA_FENCE_FINISH(F) \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "finish"); tegra_fence_finish(F); })

static inline void tegra_fence_put(struct tegra_fence *f)
{
    if (f) {
        tegra_fence_validate(f);

#ifdef FENCE_DEBUG
        if (f->refcnt < 0) {
            TEGRA_FENCE_ERR_MSG("BUG: fence refcount underflow\n");
            assert(0);
            return;
        }
#endif

#ifdef FENCE_DEBUG
        if (f->refcnt > 10) {
            TEGRA_FENCE_ERR_MSG("BUG: fence refcount overflow\n");
            assert(0);
            return;
        }
#endif

        if (--f->refcnt == -1)
            TEGRA_FENCE_FINISH(f);
    }
}
#define TEGRA_FENCE_PUT(F) \
    ({ TEGRA_FENCE_DEBUG_MSG(F, "put"); tegra_fence_put(F); })

#endif
