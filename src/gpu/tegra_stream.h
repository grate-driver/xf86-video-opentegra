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

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tegradrm/opentegra_lib.h"

#include "host1x.h"
#include "tegra_fence.h"

#define TEGRA_STREAM_ERR_MSG(fmt, args...)                              \
({                                                                      \
    fprintf(stderr, "%s:%d/%s(): " fmt,                                 \
            __FILE__, __LINE__, __func__, ##args);                      \
    assert(0);                                                          \
})

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
    bool write;
    bool explicit_fencing;
};

struct tegra_stream {
    enum tegra_stream_status status;
    struct tegra_fence *last_fence[TEGRA_ENGINES_NUM];
    bool op_done_synced;
    uint64_t fence_seqno;
    uint32_t **buf_ptr;
    uint32_t class_id;
    uint32_t num_pushed_words;

    void (*destroy)(struct tegra_stream *stream);
    int (*begin)(struct tegra_stream *stream,
                 struct drm_tegra_channel *channel);
    int (*end)(struct tegra_stream *stream);
    int (*cleanup)(struct tegra_stream *stream);
    int (*flush)(struct tegra_stream *stream,
                 struct tegra_fence *explicit_fence);
    struct tegra_fence * (*submit)(enum host1x_engine engine,
                                   struct tegra_stream *stream,
                                   struct tegra_fence *explicit_fence);
    int (*push_reloc)(struct tegra_stream *stream,
                      struct drm_tegra_bo *bo,
                      unsigned offset,
                      bool write,
                      bool explicit_fencing);
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
        TEGRA_STREAM_ERR_MSG("Stream status isn't FREE\n");
        return -1;
    }

    return stream->begin(stream, channel);
}

static inline int tegra_stream_end(struct tegra_stream *stream)
{
    int ret;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TEGRA_STREAM_ERR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    ret = stream->end(stream);
    stream->num_pushed_words = 0;
    stream->buf_ptr = NULL;

    return ret;
}

static inline int tegra_stream_cleanup(struct tegra_stream *stream)
{
    int ret;

    if (!stream)
        return -1;

    ret = stream->cleanup(stream);
    stream->num_pushed_words = 0;
    stream->buf_ptr = NULL;

    return ret;
}

static inline int tegra_stream_flush(struct tegra_stream *stream,
                                     struct tegra_fence *explicit_fence)
{
    if (!stream)
        return -1;

    return stream->flush(stream, explicit_fence);
}

static inline struct tegra_fence *
tegra_stream_get_last_fence(struct tegra_stream *stream,
                            enum host1x_engine engine)
{
    struct tegra_fence *last_fence = stream->last_fence[engine];

    TEGRA_FENCE_DEBUG_MSG(last_fence, "get_last");

    if (last_fence)
        return TEGRA_FENCE_GET(last_fence, last_fence->opaque);

    return NULL;
}
#define TEGRA_STREAM_GET_LAST_FENCE(STREAM, ENGINE)         \
({                                                          \
    tegra_stream_get_last_fence(STREAM, ENGINE);            \
})

static inline struct tegra_fence *
tegra_stream_submit(enum host1x_engine engine,
                    struct tegra_stream *stream,
                    struct tegra_fence *explicit_fence)
{
    struct tegra_fence *f;

    if (!stream)
        return NULL;

    tegra_fence_validate(explicit_fence);

    f = stream->submit(engine, stream, explicit_fence);
    if (f) {
        TEGRA_FENCE_DEBUG_MSG(f, "submit");
        f->seqno = stream->fence_seqno++;
        tegra_fence_validate(f);
    }

    return f;
}

static inline int
tegra_stream_push_reloc(struct tegra_stream *stream,
                        struct drm_tegra_bo *bo,
                        unsigned offset,
                        bool write,
                        bool explicit_fencing)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TEGRA_STREAM_ERR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    stream->num_pushed_words++;

    return stream->push_reloc(stream, bo, offset, write, explicit_fencing);
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
        TEGRA_STREAM_ERR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    va_start(args, num_relocs);
    ret = stream->push_words(stream, addr, words, num_relocs, args);
    va_end(args);

    stream->num_pushed_words += words;

    return ret;
}

static inline int
tegra_stream_prep(struct tegra_stream *stream, uint32_t words)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TEGRA_STREAM_ERR_MSG("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    return stream->prep(stream, words);
}

static inline int tegra_stream_sync(struct tegra_stream *stream,
                                    enum drm_tegra_syncpt_cond cond,
                                    bool keep_class)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        TEGRA_STREAM_ERR_MSG("Stream status isn't CONSTRUCT\n");
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
    stream->num_pushed_words++;

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

static inline unsigned int
tegra_stream_pushbuf_size(struct tegra_stream *stream)
{
    return stream->num_pushed_words * 4;
}

#endif
