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
 * THE SOFTWARE IS PROVIDED "AS IS\n", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Arto Merilainen <amerilainen@nvidia.com>
 */

#include "driver.h"
#include "tegra_stream.h"

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, \
               __FILE__, __LINE__, __func__, ##args)

#define InfoMsg(fmt, args...) \
    xf86DrvMsg(-1, X_INFO, "%s:%d/%s(): " fmt, \
               __FILE__, __LINE__, __func__, ##args)

struct tegra_command_buffer_v1 {
    struct drm_tegra_pushbuf *pushbuf;
};

struct tegra_fence_v1 {
    struct tegra_fence base;
    struct drm_tegra_fence *fence;
    struct drm_tegra_job *job;
};

struct tegra_stream_v1 {
    struct tegra_stream base;
    struct drm_tegra_job *job;
    struct tegra_command_buffer_v1 buffer;
};

static struct tegra_fence *
tegra_stream_create_fence_v1(struct drm_tegra_fence *fence, bool gr2d);

static inline struct tegra_stream_v1 *to_stream_v1(struct tegra_stream *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_stream_v1, base);
}

static inline struct tegra_fence_v1 *to_fence_v1(struct tegra_fence *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_fence_v1, base);
}

static void tegra_stream_destroy_v1(struct tegra_stream *base_stream)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);

    tegra_stream_wait_fence(stream->base.last_fence);
    tegra_stream_put_fence(stream->base.last_fence);
    drm_tegra_job_free(stream->job);
    free(stream);
}

static int tegra_stream_cleanup_v1(struct tegra_stream *base_stream)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);

    drm_tegra_job_free(stream->job);

    stream->job = NULL;
    stream->base.status = TEGRADRM_STREAM_FREE;

    return 0;
}

static int tegra_stream_flush_v1(struct tegra_stream *base_stream)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    struct drm_tegra_fence *fence;
    int ret;

    tegra_stream_wait_fence(stream->base.last_fence);
    tegra_stream_put_fence(stream->base.last_fence);
    stream->base.last_fence = NULL;

    /* reflushing is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return 0;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY) {
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_job_submit(stream->job, &fence);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit() failed %d\n", ret);
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_fence_wait_timeout(fence, 1000);
    if (ret) {
        ErrorMsg("drm_tegra_fence_wait_timeout() failed %d\n", ret);
        ret = -1;
    }

    drm_tegra_fence_free(fence);

cleanup:
    tegra_stream_cleanup_v1(base_stream);

    return ret;
}

static struct tegra_fence *
tegra_stream_submit_v1(struct tegra_stream *base_stream, bool gr2d)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    struct drm_tegra_fence *fence;
    struct tegra_fence_v1 *f_v1;
    struct tegra_fence *f;
    int ret;

    f = stream->base.last_fence;

    /* resubmitting is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return f;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY) {
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_job_submit(stream->job, &fence);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit() failed %d\n", ret);
        ret = -1;
    } else {
        f = tegra_stream_create_fence_v1(fence, gr2d);
        if (f) {
            tegra_stream_put_fence(stream->base.last_fence);
            stream->base.last_fence = f;

            f_v1 = to_fence_v1(f);
            f_v1->job = stream->job;

            goto done;
        } else {
            drm_tegra_fence_wait_timeout(fence, 1000);
            drm_tegra_fence_free(fence);
        }
    }

cleanup:
    drm_tegra_job_free(stream->job);

done:
    stream->job = NULL;
    stream->base.status = TEGRADRM_STREAM_FREE;

    return f;
}

static bool tegra_stream_wait_fence_v1(struct tegra_fence *base_fence)
{
    struct tegra_fence_v1 *f = to_fence_v1(base_fence);
    int ret;

    if (f->fence) {
        ret = drm_tegra_fence_wait_timeout(f->fence, 1000);
        if (ret) {
            ErrorMsg("drm_tegra_fence_wait_timeout() failed %d\n", ret);
        }

        drm_tegra_fence_free(f->fence);
        drm_tegra_job_free(f->job);
        f->fence = NULL;
        f->job = NULL;

        return true;
    }

    return false;
}

static void tegra_stream_free_fence_v1(struct tegra_fence *base_fence)
{
    struct tegra_fence_v1 *f = to_fence_v1(base_fence);

    drm_tegra_fence_free(f->fence);
    drm_tegra_job_free(f->job);
    free(f);
}

static struct tegra_fence *
tegra_stream_create_fence_v1(struct drm_tegra_fence *fence, bool gr2d)
{
    struct tegra_fence_v1 *f = calloc(1, sizeof(*f));

    if (!f)
        return NULL;

    f->fence = fence;
    f->base.wait_fence = tegra_stream_wait_fence_v1;
    f->base.free_fence = tegra_stream_free_fence_v1;
    f->base.gr2d = gr2d;

    return &f->base;
}

static int tegra_stream_begin_v1(struct tegra_stream *base_stream,
                                 struct drm_tegra_channel *channel)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    int ret;

    ret = drm_tegra_job_new(&stream->job, channel);
    if (ret) {
        ErrorMsg("drm_tegra_job_new() failed %d\n", ret);
        return -1;
    }

    ret = drm_tegra_pushbuf_new(&stream->buffer.pushbuf, stream->job);
    if (ret) {
        ErrorMsg("drm_tegra_pushbuf_new() failed %d\n", ret);
        drm_tegra_job_free(stream->job);
        return -1;
    }

    stream->base.class_id = 0;
    stream->base.status = TEGRADRM_STREAM_CONSTRUCT;
    stream->base.op_done_synced = false;

    return 0;
}

static int tegra_stream_push_reloc_v1(struct tegra_stream *base_stream,
                                      struct drm_tegra_bo *bo,
                                      unsigned offset)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    int ret;

    ret = drm_tegra_pushbuf_relocate(stream->buffer.pushbuf,
                                     bo, offset, 0);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_relocate() failed %d\n", ret);
        return -1;
    }

    return 0;
}

static int tegra_stream_end_v1(struct tegra_stream *base_stream)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    int ret;

    if (!(stream && stream->base.status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    if (stream->base.op_done_synced)
        goto ready;

    ret = drm_tegra_pushbuf_sync(stream->buffer.pushbuf,
                                 DRM_TEGRA_SYNCPT_COND_OP_DONE);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_sync() failed %d\n", ret);
        return -1;
    }

ready:
    stream->base.status = TEGRADRM_STREAM_READY;
    stream->base.op_done_synced = false;

    return 0;
}

static int
tegra_stream_push_words_v1(struct tegra_stream *base_stream, const void *addr,
                           unsigned words, int num_relocs, ...)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    struct tegra_reloc reloc_arg;
    uint32_t *pushbuf_ptr;
    va_list ap;
    int ret;

    ret = drm_tegra_pushbuf_prepare(stream->buffer.pushbuf, words);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_prepare() failed %d\n", ret);
        return -1;
    }

    stream->base.buf_ptr = &stream->buffer.pushbuf->ptr;

    /* class id should be set explicitly, for simplicity. */
    if (stream->base.class_id == 0) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("HOST1X class not specified\n");
        return -1;
    }

    /* copy the contents */
    pushbuf_ptr = stream->buffer.pushbuf->ptr;
    memcpy(pushbuf_ptr, addr, words * sizeof(uint32_t));

    /* copy relocs */
    va_start(ap, num_relocs);
    for (; num_relocs; num_relocs--) {
        reloc_arg = va_arg(ap, struct tegra_reloc);

        stream->buffer.pushbuf->ptr  = pushbuf_ptr;
        stream->buffer.pushbuf->ptr += reloc_arg.var_offset / sizeof(uint32_t);

        ret = drm_tegra_pushbuf_relocate(stream->buffer.pushbuf, reloc_arg.bo,
                                         reloc_arg.offset, 0);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_pushbuf_relocate() failed %d\n", ret);
            break;
        }
    }
    va_end(ap);

    stream->buffer.pushbuf->ptr = pushbuf_ptr + words;

    return ret ? -1 : 0;
}

static int tegra_stream_prep_v1(struct tegra_stream *base_stream,
                                uint32_t words)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    int ret;

    ret = drm_tegra_pushbuf_prepare(stream->buffer.pushbuf, words);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_prepare() failed %d\n", ret);
        return -1;
    }

    stream->base.buf_ptr = &stream->buffer.pushbuf->ptr;

    return 0;
}

static int tegra_stream_sync_v1(struct tegra_stream *base_stream,
                                enum drm_tegra_syncpt_cond cond,
                                bool keep_class)
{
    struct tegra_stream_v1 *stream = to_stream_v1(base_stream);
    int ret;

    ret = drm_tegra_pushbuf_sync(stream->buffer.pushbuf, cond);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_sync() failed %d\n", ret);
        return -1;
    }

    if (cond == DRM_TEGRA_SYNCPT_COND_OP_DONE)
        stream->base.op_done_synced = true;

    return 0;
}

int tegra_stream_create_v1(struct tegra_stream **pstream,
                           struct _TegraRec *tegra)
{
    struct tegra_stream_v1 *stream_v1;
    struct tegra_stream *stream;
    int ret;

    ret = drm_tegra_version(tegra->drm);
    if (ret < 0) {
        ErrorMsg("drm_tegra_version() failed %d\n", ret);
        return -1;
    }

    stream_v1 = calloc(1, sizeof(*stream_v1));
    if (!stream_v1)
        return -1;

    stream = &stream_v1->base;
    stream->status = TEGRADRM_STREAM_FREE;
    stream->destroy = tegra_stream_destroy_v1;
    stream->begin = tegra_stream_begin_v1;
    stream->end = tegra_stream_end_v1;
    stream->cleanup = tegra_stream_cleanup_v1;
    stream->flush = tegra_stream_flush_v1;
    stream->submit = tegra_stream_submit_v1;
    stream->push_reloc = tegra_stream_push_reloc_v1;
    stream->push_words = tegra_stream_push_words_v1;
    stream->prep = tegra_stream_prep_v1;
    stream->sync = tegra_stream_sync_v1;

    InfoMsg("success\n");

    *pstream = stream;

    return 0;
}
