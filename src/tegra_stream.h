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
#include <stdio.h>

#include "host1x.h"
#include "opentegra_lib.h"

#define TGR_STRM_ERROR_MSG(fmt, args...) \
    fprintf(stderr, "%s:%d/%s(): " fmt, __FILE__, __LINE__, __func__, ##args)

struct _TegraRec;

enum tegra_stream_status {
    TEGRADRM_STREAM_FREE,
    TEGRADRM_STREAM_CONSTRUCT,
    TEGRADRM_STREAM_CONSTRUCTION_FAILED,
    TEGRADRM_STREAM_READY,
};

struct tegra_reloc {
    const void *addr;
    struct drm_tegra_bo *bo;
    uint32_t offset;
    unsigned var_offset;
};

struct tegra_fence {
    void *opaque;
    int refcnt;
    bool gr2d;

    bool (*wait_fence)(struct tegra_fence *f);
    void (*free_fence)(struct tegra_fence *f);
};

struct tegra_stream {
    enum tegra_stream_status status;
    struct tegra_fence *last_fence;
    bool op_done_synced;
    uint32_t **buf_ptr;
    uint32_t class_id;

    void (*destroy)(struct tegra_stream *stream);
    int (*begin)(struct tegra_stream *stream,
                 struct drm_tegra_channel *channel);
    int (*end)(struct tegra_stream *stream);
    int (*cleanup)(struct tegra_stream *stream);
    int (*flush)(struct tegra_stream *stream);
    struct tegra_fence * (*submit)(struct tegra_stream *stream, bool gr2d);
    int (*push_reloc)(struct tegra_stream *stream,
                      struct drm_tegra_bo *bo,
                      unsigned offset);
    int (*push_words)(struct tegra_stream *stream, const void *addr,
                      unsigned words, int num_relocs, ...);
    int (*prep)(struct tegra_stream *stream, uint32_t words);
    int (*sync)(struct tegra_stream *stream,
                enum drm_tegra_syncpt_cond cond,
                bool keep_class);
};

/* Stream operations */
int tegra_stream_create_v1(struct tegra_stream **stream,
                           struct _TegraRec *tegra);

int grate_stream_create_v2(struct tegra_stream **stream,
                           struct _TegraRec *tegra);

static inline int tegra_stream_create(struct tegra_stream **stream,
                                      struct _TegraRec *tegra)
{
    int ret;

    ret = grate_stream_create_v2(stream, tegra);
    if (ret)
        ret = tegra_stream_create_v1(stream, tegra);

    return ret;
}

static inline void tegra_stream_destroy(struct tegra_stream *stream)
{
    if (!stream)
        return;

    return stream->destroy(stream);
}

static inline int tegra_stream_begin(struct tegra_stream *stream,
                                     struct drm_tegra_channel *channel)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_FREE)) {
        TGR_STRM_ERROR_MSG("Stream status isn't FREE\n");
        return -1;
    }

    return stream->begin(stream, channel);
}

static inline int tegra_stream_end(struct tegra_stream *stream)
{
    int ret;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TGR_STRM_ERROR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    ret = stream->end(stream);
    stream->buf_ptr = NULL;

    return ret;
}

static inline int tegra_stream_cleanup(struct tegra_stream *stream)
{
    int ret;

    if (!stream)
        return -1;

    ret = stream->cleanup(stream);
    stream->buf_ptr = NULL;

    return ret;
}

static inline int tegra_stream_flush(struct tegra_stream *stream)
{
    if (!stream)
        return -1;

    return stream->flush(stream);
}

static inline struct tegra_fence *
tegra_stream_submit(struct tegra_stream *stream, bool gr2d)
{
    if (!stream)
        return NULL;

    return stream->submit(stream, gr2d);
}

static inline struct tegra_fence *
tegra_stream_ref_fence(struct tegra_fence *f, void *opaque)
{
    if (f) {
        f->opaque = opaque;
        f->refcnt++;
    }

    return f;
}

static inline struct tegra_fence *
tegra_stream_get_last_fence(struct tegra_stream *stream)
{
    if (stream->last_fence)
        return tegra_stream_ref_fence(stream->last_fence,
                                      stream->last_fence->opaque);

    return NULL;
}

static inline bool tegra_stream_wait_fence(struct tegra_fence *f)
{
    if (f)
        return f->wait_fence(f);

    return false;
}

static inline void tegra_stream_put_fence(struct tegra_fence *f)
{
    if (f) {
        if (f->refcnt < 0) {
            TGR_STRM_ERROR_MSG("BUG: fence refcount underflow\n");
            return;
        }

        if (f->refcnt > 10) {
            TGR_STRM_ERROR_MSG("BUG: fence refcount overflow\n");
            return;
        }

        if (--f->refcnt == -1)
            f->free_fence(f);
    }
}

static inline int
tegra_stream_push_reloc(struct tegra_stream *stream,
                        struct drm_tegra_bo *bo,
                        unsigned offset)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TGR_STRM_ERROR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    return stream->push_reloc(stream, bo, offset);
}

static inline int tegra_stream_push_words(struct tegra_stream *stream,
                                          const void *addr,
                                          unsigned words,
                                          int num_relocs,
                                          ...)
{
    va_list args;
    int ret;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TGR_STRM_ERROR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    va_start(args, num_relocs);
    ret = stream->push_words(stream, addr, words, num_relocs, args);
    va_end(args);

    return ret;
}

static inline int
tegra_stream_prep(struct tegra_stream *stream, uint32_t words)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TGR_STRM_ERROR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    return stream->prep(stream, words);
}

static inline int tegra_stream_sync(struct tegra_stream *stream,
                                    enum drm_tegra_syncpt_cond cond,
                                    bool keep_class)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TGR_STRM_ERROR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    return stream->sync(stream, cond, keep_class);
}

static inline int
tegra_stream_push(struct tegra_stream *stream, uint32_t word)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT))
        return -1;

    *(*stream->buf_ptr)++ = word;
    stream->op_done_synced = false;

    return 0;
}

static inline int
tegra_stream_pushf(struct tegra_stream *stream, float f)
{
    union {
        uint32_t u;
        float f;
    } value;

    value.f = f;

    return tegra_stream_push(stream, value.u);
}

static inline int
tegra_stream_push_setclass(struct tegra_stream *stream, unsigned class_id)
{
    int result;

    if (stream->class_id == class_id)
        return 0;

    result = tegra_stream_push(stream, HOST1X_OPCODE_SETCL(0, class_id, 0));

    if (result == 0)
        stream->class_id = class_id;

    return result;
}

static inline struct tegra_reloc
tegra_reloc(const void *var_ptr, struct drm_tegra_bo *bo,
            uint32_t offset, uint32_t var_offset)
{
    struct tegra_reloc reloc = {var_ptr, bo, offset, var_offset};
    return reloc;
}

#endif
