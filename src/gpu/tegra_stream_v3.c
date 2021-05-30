/*
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
    xf86DrvMsg(-1, X_ERROR, "%s:%d/%s(): " fmt " errno=%d (%s)", \
               __FILE__, __LINE__, __func__, ##args, -errno, strerror(errno))

#define InfoMsg(fmt, args...) \
    xf86DrvMsg(-1, X_INFO, "%s:%d/%s(): " fmt, \
               __FILE__, __LINE__, __func__, ##args)

struct tegra_fence_v3 {
    struct tegra_fence base;
    struct drm_tegra_fence *fence;
};

struct tegra_stream_v3 {
    struct tegra_stream base;
    struct drm_tegra_job_v3 *job;
};

static struct tegra_fence *
tegra_stream_create_fence_v3(struct tegra_stream_v3 *stream,
                             struct drm_tegra_fence *fence, bool gr2d);

static inline struct tegra_stream_v3 *to_stream_v3(struct tegra_stream *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_stream_v3, base);
}

static inline struct tegra_fence_v3 *to_fence_v3(struct tegra_fence *base)
{
    return TEGRA_CONTAINER_OF(base, struct tegra_fence_v3, base);
}

static void tegra_stream_destroy_v3(struct tegra_stream *base_stream)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_2D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_2D]);

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_3D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_3D]);

    drm_tegra_job_free_v3(stream->job);
    free(stream);
}

static int tegra_stream_cleanup_v3(struct tegra_stream *base_stream)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);

    drm_tegra_job_reset_v3(stream->job);

    stream->job = NULL;
    stream->base.status = TEGRADRM_STREAM_FREE;

    return 0;
}

static int tegra_stream_flush_v3(struct tegra_stream *base_stream,
                                 struct tegra_fence *explicit_fence)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    struct drm_tegra_fence *fence = NULL;
    int ret;

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_2D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_2D]);
    stream->base.last_fence[TEGRA_2D] = NULL;

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_3D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_3D]);
    stream->base.last_fence[TEGRA_3D] = NULL;

    /* reflushing is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return 0;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY) {
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_job_submit_v3(stream->job, &fence);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v3() failed %d\n", ret);
        goto cleanup;
    } else if (!fence) {
        ErrorMsg("drm_tegra_job_submit_v3() failed to create fence\n");
    }

    ret = drm_tegra_fence_wait_timeout(fence, 1000);
    if (ret)
        ErrorMsg("drm_tegra_fence_wait_timeout() failed %d\n", ret);

    drm_tegra_fence_free(fence);

cleanup:
    tegra_stream_cleanup_v3(base_stream);

    return ret;
}

static struct tegra_fence *
tegra_stream_submit_v3(enum host1x_engine engine,
                       struct tegra_stream *base_stream,
                       struct tegra_fence *explicit_fence)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    struct drm_tegra_fence *fence = NULL;
    struct tegra_fence *f;
    int ret;

    f = stream->base.last_fence[engine];

    /* resubmitting is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return f;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY) {
        ret = -1;
        goto cleanup;
    }

    ret = drm_tegra_job_submit_v3(stream->job, &fence);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v3() failed %d\n", ret);
        TEGRA_FENCE_WAIT(stream->base.last_fence[engine]);
        TEGRA_FENCE_PUT(stream->base.last_fence[engine]);
        stream->base.last_fence[engine] = f = NULL;
        ret = -1;
    } else {
        if (!fence)
            ErrorMsg("drm_tegra_job_submit_v3() failed to create fence\n");

        f = tegra_stream_create_fence_v3(stream, fence, engine == TEGRA_2D);
        if (f) {
            TEGRA_FENCE_PUT(stream->base.last_fence[engine]);
            TEGRA_FENCE_SET_ACTIVE(f);

            stream->base.last_fence[engine] = f;
            f->seqno = base_stream->fence_seqno++;
        } else {
            ret = drm_tegra_fence_wait_timeout(fence, 1000);
            if (ret) {
                ErrorMsg("drm_tegra_fence_wait_timeout() failed %d\n", ret);
            }
            drm_tegra_fence_free(fence);
        }
    }

cleanup:
    drm_tegra_job_free_v3(stream->job);

    stream->job = NULL;
    stream->base.status = TEGRADRM_STREAM_FREE;

    return f;
}

static bool tegra_stream_check_fence_v3(struct tegra_fence *base_fence)
{
    struct tegra_fence_v3 *f = to_fence_v3(base_fence);
    int ret;

    if (f->fence) {
        ret = drm_tegra_fence_is_busy(f->fence);
        if (ret < 0)
            ErrorMsg("drm_tegra_fence_is_busy() failed %d\n", ret);
        if (ret > 0)
            return false;

        drm_tegra_fence_free(f->fence);
        f->fence = NULL;
    }

    return true;
}

static bool tegra_stream_wait_fence_v3(struct tegra_fence *base_fence)
{
    struct tegra_fence_v3 *f = to_fence_v3(base_fence);
    int ret;

    if (f->fence) {
        ret = drm_tegra_fence_wait_timeout(f->fence, 1000);
        if (ret) {
            ErrorMsg("drm_tegra_fence_wait_timeout() failed %d\n", ret);
            return false;
        }

        drm_tegra_fence_free(f->fence);
        f->fence = NULL;

        return true;
    }

    return true;
}

static bool tegra_stream_free_fence_v3(struct tegra_fence *base_fence)
{
    struct tegra_fence_v3 *f = to_fence_v3(base_fence);
    int err = 0;

    /*
     * All job's BOs are kept refcounted after the submission. Once job is
     * released, job's BOs are unreferenced and we always releasing the old
     * fence before new job is submitted, hence BOs may get erroneously
     * re-used by a new job if we won't wait for a previous job to be finished.
     *
     * For example, once new 3d job is submitted, the attribute BOs of a
     * previous job are released and then next 2d job could re-use the released
     * BOs while 3d job isn't completed yet.
     *
     * We're solving this trouble by holding fence until job that is
     * associated with the fence is completed. The job is kept alive
     * while fence is alive.
     */
    if (f->fence) {
        err = drm_tegra_fence_is_busy(f->fence);
        if (err < 0)
            ErrorMsg("drm_tegra_fence_is_busy() failed %d\n", err);
    }

#ifdef FENCE_DEBUG
    DRMLISTDEL(&f->base.dbg_entry);
    tegra_fences_destroyed++;
#endif
    drm_tegra_fence_free(f->fence);
    free(f);

    return true;
}

static bool
tegra_stream_mark_fence_completed_v3(struct tegra_fence *base_fence)
{
    struct tegra_fence_v3 *f = to_fence_v3(base_fence);

    if (f->fence) {
        drm_tegra_fence_free(f->fence);
        f->fence = NULL;
    }

    TEGRA_FENCE_SET_ACTIVE(base_fence);

    return true;
}

static struct tegra_fence *
tegra_stream_create_fence_v3(struct tegra_stream_v3 *stream,
                             struct drm_tegra_fence *fence, bool gr2d)
{
    struct tegra_fence_v3 *f = calloc(1, sizeof(*f));

    if (!f)
        return NULL;

    f->fence = fence;
    f->base.check_fence = tegra_stream_check_fence_v3;
    f->base.wait_fence = tegra_stream_wait_fence_v3;
    f->base.free_fence = tegra_stream_free_fence_v3;
    f->base.mark_completed = tegra_stream_mark_fence_completed_v3;
    f->base.gr2d = gr2d;

#ifdef FENCE_DEBUG
    f->base.bug0 = false;
    f->base.bug1 = true;
    f->base.released = false;

    DRMLISTADD(&f->base.dbg_entry, &tegra_live_fences);
    tegra_fences_created++;
#endif

    return &f->base;
}

static int tegra_stream_begin_v3(struct tegra_stream *base_stream,
                                 struct drm_tegra_channel *channel)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    int ret;

    if (!stream->job) {
        ret = drm_tegra_job_new_v3(&stream->job, channel, 0, 0, 0);
        if (ret) {
            ErrorMsg("drm_tegra_job_new_v3() failed %d\n", ret);
            return -1;
        }
    }

    stream->base.class_id = 0;
    stream->base.status = TEGRADRM_STREAM_CONSTRUCT;
    stream->base.op_done_synced = false;
    stream->base.buf_ptr = &stream->job->ptr;

    return 0;
}

static int tegra_stream_push_reloc_v3(struct tegra_stream *base_stream,
                                      struct drm_tegra_bo *bo,
                                      unsigned offset,
                                      bool write_dir,
                                      bool explicit_fencing)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    int ret;

    ret = drm_tegra_job_push_reloc_v3(stream->job, bo, offset, 0);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_job_push_reloc_v3() failed %d\n", ret);
        return -1;
    }

    return 0;
}

static int tegra_stream_end_v3(struct tegra_stream *base_stream)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    int ret;

    if (!(stream && stream->base.status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    if (stream->base.op_done_synced)
        goto ready;

    ret = drm_tegra_job_push_syncpt_incr_v3(stream->job,
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

static int tegra_stream_prep_v3(struct tegra_stream *base_stream,
                                uint32_t words)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);

    int ret;

    if (stream->job->ptr + words >
        stream->job->start + stream->job->num_words) {
        if (words < 1024)
            words = 1024;

        ret = drm_tegra_job_resize_v3(stream->job,
                                      stream->job->num_words + words,
                                      stream->job->num_buffers_max,
                                      stream->job->num_cmds_max,
                                      true);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_job_resize_v3() failed %d\n", ret);
            return -1;
        }

        stream->base.buf_ptr = &stream->job->ptr;
    }

    return 0;
}

static int
tegra_stream_push_words_v3(struct tegra_stream *base_stream, const void *addr,
                           unsigned words, int num_relocs, va_list ap)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    struct tegra_reloc reloc_arg;
    uint32_t *pushbuf_ptr;
    int ret;

    ret = tegra_stream_prep_v3(base_stream, words);
    if (ret)
        return ret;

    stream->base.buf_ptr = &stream->job->ptr;

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
    for (; num_relocs; num_relocs--) {
        reloc_arg = va_arg(ap, struct tegra_reloc);

        stream->job->ptr  = pushbuf_ptr;
        stream->job->ptr += reloc_arg.var_offset;

        ret = drm_tegra_job_push_reloc_v3(stream->job, reloc_arg.bo,
                                          reloc_arg.offset, 0);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_pushbuf_relocate() failed %d\n", ret);
            break;
        }
    }

    stream->job->ptr = pushbuf_ptr + words;

    return ret ? -1 : 0;
}

static int tegra_stream_sync_v3(struct tegra_stream *base_stream,
                                enum drm_tegra_syncpt_cond cond,
                                bool keep_class)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    int ret;

    ret = tegra_stream_prep_v3(base_stream, 2);
    if (ret)
        return ret;

    ret = drm_tegra_job_push_syncpt_incr_v3(stream->job,
                                            DRM_TEGRA_SYNCPT_COND_OP_DONE);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_pushbuf_sync() failed %d\n", ret);
        return -1;
    }

    ret = drm_tegra_job_push_wait_v3(stream->job, stream->job->sp_incrs);
    if (ret) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_job_push_wait_v3() failed %d\n", ret);
        return -1;
    }

    if (cond == DRM_TEGRA_SYNCPT_COND_OP_DONE)
        stream->base.op_done_synced = true;

    return 0;
}

static struct tegra_fence *
tegra_stream_get_current_fence_v3(struct tegra_stream *base_stream)
{
    struct tegra_stream_v3 *stream = to_stream_v3(base_stream);
    struct drm_tegra_fence *fence;
    struct tegra_fence *f;
    int err;

    if (stream->base.class_id == 0) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("HOST1X class not specified\n");
        return NULL;
    }

    err = drm_tegra_job_create_fence_v3(stream->job, &fence);
    if (err) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_job_create_fence_v3() failed\n");
        return NULL;
    }

    f = tegra_stream_create_fence_v3(stream, fence,
                                     stream->base.class_id != HOST1X_CLASS_GR3D);
    if (!fence) {
        stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("tegra_stream_create_fence_v3() failed\n");
        drm_tegra_fence_free(fence);
        return NULL;
    }

    return f;
}

int tegra_stream_create_v3(struct tegra_stream **pstream,
                           struct drm_tegra *drm)
{
    struct drm_tegra_channel *channel;
    struct tegra_stream_v3 *stream_v3;
    struct tegra_stream *stream;
    int ret;

    if (getenv("OPENTEGRA_FORCE_OLD_UAPI"))
        return -1;

#ifndef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    InfoMsg("too old libdrm\n");
    return -1;
#endif

    ret = drm_tegra_version(drm);
    if (ret < 0) {
        ErrorMsg("drm_tegra_version() failed %d\n", ret);
        return -1;
    }

    if (ret != 1)
        return -1;

    ret = drm_tegra_channel_open_v3(&channel, drm, DRM_TEGRA_GR3D);
    if (ret) {
        ErrorMsg("drm_tegra_channel_open_v3() failed %d\n", ret);
        return -1;
    }

    drm_tegra_channel_close_v3(channel);

    stream_v3 = calloc(1, sizeof(*stream_v3));
    if (!stream_v3)
        return -1;

    stream = &stream_v3->base;
    stream->status = TEGRADRM_STREAM_FREE;
    stream->destroy = tegra_stream_destroy_v3;
    stream->begin = tegra_stream_begin_v3;
    stream->end = tegra_stream_end_v3;
    stream->cleanup = tegra_stream_cleanup_v3;
    stream->flush = tegra_stream_flush_v3;
    stream->submit = tegra_stream_submit_v3;
    stream->push_reloc = tegra_stream_push_reloc_v3;
    stream->push_words = tegra_stream_push_words_v3;
    stream->prep = tegra_stream_prep_v3;
    stream->sync = tegra_stream_sync_v3;
    stream->current_fence = tegra_stream_get_current_fence_v3;

    InfoMsg("success\n");

    *pstream = stream;

    return 0;
}
