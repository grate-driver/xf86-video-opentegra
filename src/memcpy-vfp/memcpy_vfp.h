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

#ifndef __TEGRA_MEMCPY_H
#define __TEGRA_MEMCPY_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef void (*tegra_vfp_func)(char *dst, const char *src, int size);

void tegra_copy_block_vfp(char *dst, const char *src, int size);
void tegra_copy_block_vfp_2_pass(char *dst, const char *src, int size);
void tegra_copy_block_vfp_arm(char *dst, const char *src, int size);
void tegra_memcpy_vfp_unaligned_2_pass(char *dst, const char *src, int size);

/*
 * Use multi-threaded copying for a large transfers from uncached memory.
 *
 * Don't use threaded copying from a cached memory if unsure, since it
 * could be 2x slower than a single-threaded operation.
 */
void tegra_memcpy_vfp_threaded(char *dst, const char *src, int size,
                               tegra_vfp_func copy_func);

/* use this when src is uncacheable */
static inline void
tegra_memcpy_vfp_unaligned(char *dst, const char *src, int size)
{
    if (size < 192)
        memcpy(dst, src, size);
    else
        tegra_memcpy_vfp_unaligned_2_pass(dst, src, size);
}

static inline bool
tegra_memcpy_vfp_copy_is_safe(char *dst, const char *src, int size)
{
    return (((uintptr_t)src & 127) == 0 &&
            ((uintptr_t)dst & 127) == 0 &&
            (size & 127) == 0 &&
            size >= 128);
}

/* use this when both src and dst are either cacheable or uncacheable */
static inline void
tegra_memcpy_vfp_aligned(char *dst, const char *src, int size)
{
    if (__builtin_expect(dst == src || !size, 0))
        return;

    assert(tegra_memcpy_vfp_copy_is_safe(dst, src, size));

    tegra_copy_block_vfp_2_pass(dst, src, size);
}

/* use this when src is uncacheable and dst is cacheable */
static inline void
tegra_memcpy_vfp_aligned_dst_cached(char *dst, const char *src, int size)
{
    if (__builtin_expect(dst == src || !size, 0))
        return;

    assert(tegra_memcpy_vfp_copy_is_safe(dst, src, size));

    tegra_copy_block_vfp(dst, src, size);
}

/* use this when src is cacheable and dst uncacheable */
static inline void
tegra_memcpy_vfp_aligned_src_cached(char *dst, const char *src, int size)
{
    if (__builtin_expect(dst == src || !size, 0))
        return;

    assert(tegra_memcpy_vfp_copy_is_safe(dst, src, size));

    tegra_copy_block_vfp_arm(dst, src, size);
}

#define tegra_memmove_vfp_aligned       tegra_memcpy_vfp_aligned

#endif
