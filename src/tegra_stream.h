/*
 * Copyright (c) 2016-2017 Dmitry Osipenko <digetx@gmail.com>
 * Copyright (C) 2012-2013 NVIDIA Corporation.
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
 *
 * Authors:
 *	Arto Merilainen <amerilainen@nvidia.com>
 */

#ifndef TEGRA_STREAM_H_
#define TEGRA_STREAM_H_

#include <stdbool.h>
#include <stdint.h>
#include <libdrm/tegra.h>

enum tegra_stream_status {
    TEGRADRM_STREAM_FREE,
    TEGRADRM_STREAM_CONSTRUCT,
    TEGRADRM_STREAM_CONSTRUCTION_FAILED,
    TEGRADRM_STREAM_READY,
};

struct tegra_command_buffer {
    struct drm_tegra_pushbuf *pushbuf;
};

struct tegra_fence {
    struct drm_tegra_fence *fence;
    void *opaque;
    int refcnt;
    bool gr2d;
};

struct tegra_stream {
    enum tegra_stream_status status;

    struct drm_tegra_job *job;

    struct tegra_fence *last_fence;
    struct tegra_command_buffer buffer;
    uint32_t class_id;

    bool op_done_synced;
};

struct tegra_reloc {
    const void *addr;
    struct drm_tegra_bo *bo;
    uint32_t offset;
    unsigned var_offset;
};

/* Stream operations */
int tegra_stream_create(struct tegra_stream *stream);
void tegra_stream_destroy(struct tegra_stream *stream);
int tegra_stream_begin(struct tegra_stream *stream,
                       struct drm_tegra_channel *channel);
int tegra_stream_end(struct tegra_stream *stream);
int tegra_stream_cleanup(struct tegra_stream *stream);
int tegra_stream_flush(struct tegra_stream *stream);
struct tegra_fence * tegra_stream_submit(struct tegra_stream *stream, bool gr2d);
struct tegra_fence * tegra_stream_ref_fence(struct tegra_fence *f, void *opaque);
struct tegra_fence * tegra_stream_get_last_fence(struct tegra_stream *stream);
struct tegra_fence * tegra_stream_create_fence(struct drm_tegra_fence *fence,
                                               bool gr2d);
bool tegra_stream_wait_fence(struct tegra_fence *f);
void tegra_stream_put_fence(struct tegra_fence *f);
int tegra_stream_push(struct tegra_stream *stream, uint32_t word);
int tegra_stream_push_setclass(struct tegra_stream *stream, unsigned class_id);
int tegra_stream_push_reloc(struct tegra_stream *stream,
                            struct drm_tegra_bo *bo, unsigned offset);
struct tegra_reloc tegra_reloc(const void *var_ptr, struct drm_tegra_bo *bo,
                               uint32_t offset, uint32_t var_offset);
int tegra_stream_push_words(struct tegra_stream *stream, const void *addr,
                            unsigned words, int num_relocs, ...);
int tegra_stream_prep(struct tegra_stream *stream, uint32_t words);
int tegra_stream_sync(struct tegra_stream *stream,
                      enum drm_tegra_syncpt_cond cond);
int tegra_stream_pushf(struct tegra_stream *stream, float f);

#endif
