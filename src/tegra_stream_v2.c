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
 * THE SOFTWARE IS PROVIDED "AS IS\n", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "driver.h"
#include "tegra_stream.h"

#define ErrorMsg(fmt, args...) \
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt, \
               __FILE__, __LINE__, __func__, ##args)

#define InfoMsg(fmt, args...) \
    xf86DrvMsg(-1, X_INFO, "%s:%d/%s(): " fmt, \
               __FILE__, __LINE__, __func__, ##args)

struct tegra_fence_v2 {
    struct tegra_fence base;
    uint32_t syncobj_handle;
    int drm_fd;
};

struct tegra_stream_v2 {
    struct tegra_stream base;
    int drm_fd;
    struct drm_tegra *drm;
    struct drm_tegra_job_v2 *job;
};

static struct tegra_fence *
tegra_stream_create_fence_v2(struct tegra_stream_v2 *stream, bool gr2d);

static inline struct tegra_stream_v2 *to_stream_v2(struct tegra_stream *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_stream_v2, base);
}

static inline struct tegra_fence_v2 *to_fence_v2(struct tegra_fence *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_fence_v2, base);
}

static void tegra_stream_destroy_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    TEGRA_STREAM_WAIT_FENCE(stream->base.last_fence);
    TEGRA_STREAM_PUT_FENCE(stream->base.last_fence);
    drm_tegra_job_free_v2(stream->job);
    free(stream);
}

static int tegra_stream_cleanup_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    drm_tegra_job_reset_v2(stream->job);
    stream->base.status = TEGRADRM_STREAM_FREE;

    return 0;
}

static int tegra_stream_flush_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    struct tegra_fence *f;
    int ret;

    TEGRA_STREAM_WAIT_FENCE(stream->base.last_fence);
    TEGRA_STREAM_PUT_FENCE(stream->base.last_fence);
    stream->base.last_fence = NULL;

    /* reflushing is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return 0;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY) {
        ret = -1;
        goto cleanup;
    }

    f = tegra_stream_create_fence_v2(stream, false);
    if (!f) {
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_job_submit_v2(stream->job,
                                     to_fence_v2(f)->syncobj_handle, ~0ull);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v2() failed %d (%s)\n",
                 ret, strerror(ret));
        ret = -1;
    } else {
        TEGRA_STREAM_WAIT_FENCE(f);
    }

    TEGRA_STREAM_PUT_FENCE(f);

cleanup:
    tegra_stream_cleanup_v2(base_stream);

    return ret;
}

static struct tegra_fence *
tegra_stream_submit_v2(struct tegra_stream *base_stream, bool gr2d)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    struct tegra_fence *f;
    int ret;

    f = stream->base.last_fence;

    /* resubmitting is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return f;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY)
        goto cleanup;

    f = tegra_stream_create_fence_v2(stream, gr2d);
    if (!f)
        goto cleanup;

    ret = drm_tegra_job_submit_v2(stream->job,
                                     to_fence_v2(f)->syncobj_handle, ~0ull);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v2() failed %d\n", ret);
        TEGRA_STREAM_PUT_FENCE(f);
        TEGRA_STREAM_WAIT_FENCE(stream->base.last_fence);
        TEGRA_STREAM_PUT_FENCE(stream->base.last_fence);
        stream->base.last_fence = f = NULL;
    } else {
        TEGRA_STREAM_PUT_FENCE(stream->base.last_fence);
        stream->base.last_fence = f;
    }

cleanup:
    drm_tegra_job_reset_v2(stream->job);
    stream->base.status = TEGRADRM_STREAM_FREE;

    return f;
}

static int tegra_stream_create_syncobj_v2(struct tegra_stream_v2 *stream,
                                          uint32_t *syncobj_handle)
{
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    int err;

    err = drmSyncobjCreate(stream->drm_fd, 0, syncobj_handle);
    if (err < 0) {
        ErrorMsg("drmSyncobjCreate() failed %d\n", err);
        return err;
    }
#endif

    return 0;
}

static uint64_t gettime_ns(void)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    return (uint64_t)current.tv_sec * 1000000000ull + current.tv_nsec;
}

static bool tegra_stream_wait_fence_v2(struct tegra_fence *base_fence)
{
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    struct tegra_fence_v2 *f = to_fence_v2(base_fence);
    int ret;

    if (!f->syncobj_handle)
        return true;

    ret = drmSyncobjWait(f->drm_fd, &f->syncobj_handle, 1,
                            gettime_ns() + 1000000000,
                            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
                            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                            NULL);
    if (ret) {
        ErrorMsg("drmSyncobjWait() failed %d\n", ret);
        return false;
    }

    drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
    f->syncobj_handle = 0;
#endif

    return true;
}

static bool tegra_stream_free_fence_v2(struct tegra_fence *base_fence)
{
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    struct tegra_fence_v2 *f = to_fence_v2(base_fence);

    if (f->syncobj_handle)
        drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
    free(f);
#endif
    return true;
}

static struct tegra_fence *
tegra_stream_create_fence_v2(struct tegra_stream_v2 *stream, bool gr2d)
{
    struct tegra_fence_v2 *f = calloc(1, sizeof(*f));
    int err;

    if (!f)
        return NULL;
    err = tegra_stream_create_syncobj_v2(stream, &f->syncobj_handle);
    if (err) {
        free(f);
        return NULL;
    }

    f->drm_fd = stream->drm_fd;
    f->base.wait_fence = tegra_stream_wait_fence_v2;
    f->base.free_fence = tegra_stream_free_fence_v2;
    f->base.gr2d = gr2d;

#ifdef FENCE_DEBUG
    f->base.bug0 = false;
    f->base.bug1 = true;
#endif

    return &f->base;
}

static int tegra_stream_begin_v2(struct tegra_stream *base_stream,
                                 struct drm_tegra_channel *channel)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    stream->base.class_id = 0;
    stream->base.status = TEGRADRM_STREAM_CONSTRUCT;
    stream->base.op_done_synced = false;
    stream->base.buf_ptr = &stream->job->ptr;

    return 0;
}

static int tegra_stream_push_reloc_v2(struct tegra_stream *base_stream,
                                      struct drm_tegra_bo *bo,
                                      unsigned offset)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    int ret;

    ret = drm_tegra_job_push_reloc_v2(stream->job, bo, offset,
                                      DRM_TEGRA_BO_TABLE_WRITE);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_job_push_reloc_v2() failed %d\n", ret);
        return -1;
    }

    return 0;
}

static int tegra_stream_end_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    if (stream->base.op_done_synced)
        goto ready;

    tegra_stream_push(base_stream,
                      HOST1X_OPCODE_IMM(0, DRM_TEGRA_SYNCPT_COND_OP_DONE << 8));

ready:
    stream->base.status = TEGRADRM_STREAM_READY;
    stream->base.op_done_synced = false;

    return 0;
}

static int tegra_stream_prep_v2(struct tegra_stream *base_stream, uint32_t words)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    int ret;

    if (stream->job->ptr + words >
        stream->job->start + stream->job->num_words) {
        if (words < 1024)
            words = 1024;

        ret = drm_tegra_job_resize_v2(stream->job,
                                      stream->job->num_words + words,
                                      stream->job->num_bos,
                                      true);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_job_resize_words_v2() failed %d\n", ret);
            return -1;
        }

        stream->base.buf_ptr = &stream->job->ptr;
    }

    return 0;
}

static int
tegra_stream_push_words_v2(struct tegra_stream *base_stream, const void *addr,
                           unsigned words, int num_relocs, ...)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    struct tegra_reloc reloc_arg;
    uint32_t *pushbuf_ptr;
    va_list ap;
    int ret;

    ret = tegra_stream_prep_v2(base_stream, words);
    if (ret)
        return ret;

    /* class id should be set explicitly, for simplicity. */
    if (stream->base.class_id == 0) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("HOST1X class not specified\n");
        return -1;
    }

    /* copy the contents */
    pushbuf_ptr = stream->job->ptr;
    memcpy(pushbuf_ptr, addr, words * sizeof(uint32_t));

    /* copy relocs */
    va_start(ap, num_relocs);
    for (; num_relocs; num_relocs--) {
        reloc_arg = va_arg(ap, struct tegra_reloc);

        stream->job->ptr  = pushbuf_ptr;
        stream->job->ptr += reloc_arg.var_offset / sizeof(uint32_t);

        ret = drm_tegra_job_push_reloc_v2(stream->job,
                                          reloc_arg.bo,
                                          reloc_arg.offset,
                                          DRM_TEGRA_BO_TABLE_WRITE);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_job_push_reloc_v2() failed %d\n", ret);
            break;
        }
    }
    va_end(ap);

    stream->job->ptr = pushbuf_ptr + words;

    return ret ? -1 : 0;
}

static int tegra_stream_sync_v2(struct tegra_stream *base_stream,
                                enum drm_tegra_syncpt_cond cond,
                                bool keep_class)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    struct drm_tegra_cmdstream_wait_syncpt wait;
    int ret;

    wait.threshold = DRM_TEGRA_WAIT_FOR_LAST_SYNCPT_INCR;

    ret = tegra_stream_prep_v2(base_stream, 4);
    if (ret)
        return ret;

    tegra_stream_push(base_stream, HOST1X_OPCODE_IMM(0, cond << 8));

    /* switch to host1x class to await the sync point increment */
    tegra_stream_push(base_stream, HOST1X_OPCODE_SETCL(8, HOST1X_CLASS_HOST1X, 1));
    tegra_stream_push(base_stream, wait.u_data);

    /* return to the original class if desired */
    if (keep_class)
        tegra_stream_push(base_stream,
                          HOST1X_OPCODE_SETCL(0, stream->base.class_id , 0));

    if (cond == DRM_TEGRA_SYNCPT_COND_OP_DONE)
        stream->base.op_done_synced = true;

    return 0;
}

int grate_stream_create_v2(struct tegra_stream **pstream,
                           struct _TegraRec *tegra)
{
    struct tegra_stream_v2 *stream_v2;
    struct tegra_stream *stream;
    int ret;

#ifndef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    InfoMsg("too old libdrm\n");
    return -1;
#endif

    ret = drm_tegra_version(tegra->drm);
    if (ret < 0) {
        ErrorMsg("drm_tegra_version() failed %d\n", ret);
        return -1;
    }

    /* this is experimental grate-kernel UAPI version */
    if (ret < GRATE_KERNEL_DRM_VERSION) {
        InfoMsg("GRATE DRM v2 API unsupported by kernel driver\n");
        InfoMsg("https://github.com/grate-driver/linux\n");
        return -1;
    }

    stream_v2 = calloc(1, sizeof(*stream_v2));
    if (!stream_v2)
        return -1;

    stream = &stream_v2->base;
    stream->status = TEGRADRM_STREAM_FREE;
    stream->destroy = tegra_stream_destroy_v2;
    stream->begin = tegra_stream_begin_v2;
    stream->end = tegra_stream_end_v2;
    stream->cleanup = tegra_stream_cleanup_v2;
    stream->flush = tegra_stream_flush_v2;
    stream->submit = tegra_stream_submit_v2;
    stream->push_reloc = tegra_stream_push_reloc_v2;
    stream->push_words = tegra_stream_push_words_v2;
    stream->prep = tegra_stream_prep_v2;
    stream->sync = tegra_stream_sync_v2;

    stream_v2->drm_fd = tegra->fd;
    stream_v2->drm = tegra->drm;

    ret = drm_tegra_job_new_v2(&stream_v2->job, tegra->drm, 16,
                               65536 /* xxx: 64K should be enough (!?) */);
    if (ret) {
        ErrorMsg("drm_tegra_job_new_v2() failed %d\n", ret);
        free(stream_v2);
        return ret;
    }

    InfoMsg("success\n");

    *pstream = stream;

    return 0;
}
