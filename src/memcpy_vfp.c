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

#include <stdbool.h>
#include <pthread.h>
#include <sys/sysinfo.h>

#include "memcpy_vfp.h"

#define BLOCK_SIZE  1024

struct vfpcpy_cfg {
    tegra_vfp_func cpy;
    const char *src;
    char *dst;
    int size;
};

static __thread char bounce_buf[BLOCK_SIZE] __attribute__((aligned (128)));

static inline void vfpcpy(void *dst, const void *src, int size)
{
    asm volatile(
        "   .fpu vfpv3-d16          \n\t"
        "   .arch armv7a            \n\t"
        "0:                         \n\t"
        "   subs  %2, %2, #64       \n\t"
        "   vldm  %1!, {d0-d7}      \n\t"
        "   pld   [%1, #0]          \n\t"
        "   pld   [%1, #32]         \n\t"
        "   vstm  %0!, {d0-d7}      \n\t"
        "   bgt   0b                \n\t"
        : "+r" (dst), "+r" (src), "+r" (size)
        :
        : "cc");
}

void tegra_copy_block_vfp(char *dst, const char *src, int size)
{
    vfpcpy(dst, src, size);
}

void tegra_copy_block_vfp_2_pass(char *dst, const char *src, int size)
{
    int i, dir, block_size = BLOCK_SIZE;
    const char *psrc = src;
    char *pdst = dst;
    bool move = true;

    if ((uintptr_t)dst + size <= (uintptr_t)src ||
        (uintptr_t)src + size <= (uintptr_t)dst)
            move = false;

    do {
        if (size <= block_size) {
            block_size = size;
            move = false;
        }

        dir = (pdst > psrc && move) ? -1 : 1;

        if (dir < 0) {
            psrc += size - block_size;
            pdst += size - block_size;
        }

        for (i = 0; i < size / block_size; i++) {
            vfpcpy(bounce_buf, psrc, block_size);
            memcpy(pdst, bounce_buf, block_size);

            psrc += block_size * dir;
            pdst += block_size * dir;
        }

        size -= block_size * i;

        if (dst > pdst)
            pdst = dst;

        if (src > psrc)
            psrc = src;

    } while (size);
}

void tegra_copy_block_vfp_arm(char *dst, const char *src, int size)
{
    asm volatile(
        "   .fpu vfpv3-d16          \n\t"
        "   .arch armv7a            \n\t"
        "0:                         \n\t"
        "   vldm  %1!, {d0-d15}     \n\t"
        "   subs  %2, %2, #128      \n\t"
        "   beq   1f                \n\t"
        "   pld   [%1, #0]          \n\t"
        "   pld   [%1, #32]         \n\t"
        "   pld   [%1, #64]         \n\t"
        "   pld   [%1, #96]         \n\t"
        "1:                         \n\t"
        "   push  {%1, %2}          \n\t"
        "   vmov  %1, %2, d0        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d1        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d2        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d3        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d4        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d5        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d6        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d7        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d8        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d9        \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d10       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d11       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d12       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d13       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d14       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   vmov  %1, %2, d15       \n\t"
        "   stmia %0!, {%1, %2}     \n\t"
        "   pop  {%1, %2}           \n\t"
        "   bgt   0b                \n\t"
        : "+r" (dst), "+r" (src), "+r" (size)
        :
        : "cc", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15");
}

void tegra_memcpy_vfp_unaligned_2_pass(char *dst, const char *src, int size)
{
    int bytes_align = (uintptr_t)src & 127;
    bool bounce;

    /* having dst bouncing instead of src is always more efficient */
    if (bytes_align) {
        int offset = 128 - bytes_align;

        memcpy(dst, src, offset);

        src += offset;
        dst += offset;
        size -= offset;
    }

    bytes_align = (uintptr_t)dst & 127;
    bounce = bytes_align > 0;

    while (size > 127) {
        int block_size = size & ~127;

        if (bounce) {
            if (block_size > BLOCK_SIZE)
                block_size = BLOCK_SIZE;

            vfpcpy(bounce_buf, src, block_size);
            memcpy(dst, bounce_buf, block_size);
        } else {
            vfpcpy(dst, src, block_size);
        }

        src += block_size;
        dst += block_size;
        size -= block_size;
    }

    if (size)
        memcpy(dst, src, size);
}

static void *tegra_memcpy_vfp_thread(void *arg)
{
    struct vfpcpy_cfg *cfg = arg;

    cfg->cpy(cfg->dst, cfg->src, cfg->size);

    return NULL;
}

#define MIN_SIZE_PER_THREAD     512
#define MIN_THREADS_NUM         2
#define MAX_THREADS_NUM         2

static unsigned int tegra_num_copy_threads(int size)
{
    int size_threads = (size + MIN_SIZE_PER_THREAD) / MIN_SIZE_PER_THREAD;
    int cpu_threads = size_threads > 1 ? get_nprocs() : 1;
    int threads_num = size_threads < cpu_threads ? size_threads : cpu_threads;

    if (threads_num < MIN_THREADS_NUM)
        return 1;

    if (threads_num > MAX_THREADS_NUM)
        return MAX_THREADS_NUM;

    return threads_num;
}

void tegra_memcpy_vfp_threaded(char *dst, const char *src, int size,
                               tegra_vfp_func copy_func)
{
    unsigned int threads_num = tegra_num_copy_threads(size);
    struct vfpcpy_cfg cfgs[MAX_THREADS_NUM];
    pthread_t threads[MAX_THREADS_NUM];
    unsigned int thread_cpy_size;
    void *thread_arg;
    unsigned int i;

    if (threads_num > 1)
        thread_cpy_size = (size / threads_num) & (~127);
    else
        thread_cpy_size = size;

    for (i = 0; i < threads_num; i++) {
        cfgs[i].size = thread_cpy_size;
        cfgs[i].src  = src;
        cfgs[i].dst  = dst;
        cfgs[i].cpy  = copy_func;

        src += thread_cpy_size;
        dst += thread_cpy_size;
    }

    for (i = 1; i < threads_num; i++) {
        thread_arg = (void*) &cfgs[i];
        pthread_create(&threads[i], NULL, tegra_memcpy_vfp_thread, thread_arg);
    }

    copy_func(cfgs[0].dst, cfgs[0].src, cfgs[0].size);

    size -= thread_cpy_size * threads_num;
    if (size)
        memcpy(dst, src, size);

    for (i = 1; i < threads_num; i++)
        pthread_join(threads[i], NULL);
}
