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
    struct drm_tegra *drm;
    struct drm_tegra_job_v2 *job;

    /*
     * job_fence is created by tegra_stream_get_current_fence_v2() that
     * can be invoked during of cmdstream construction in order to get
     * an intermediate job fence, it becomes the job's.
     */
    struct tegra_fence *job_fence;
};

static __maybe_unused uint64_t gettime_ns(void)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    return (uint64_t)current.tv_sec * 1000000000ull + current.tv_nsec;
}

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

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_2D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_2D]);

    TEGRA_FENCE_WAIT(stream->base.last_fence[TEGRA_3D]);
    TEGRA_FENCE_PUT(stream->base.last_fence[TEGRA_3D]);

    drm_tegra_job_free_v2(stream->job);
    free(stream);
}

static int tegra_stream_cleanup_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);

    drm_tegra_job_reset_v2(stream->job);
    stream->base.status = TEGRADRM_STREAM_FREE;
    TEGRA_FENCE_PUT(stream->job_fence);
    stream->job_fence = NULL;

    return 0;
}

static int tegra_stream_flush_v2(struct tegra_stream *base_stream,
                                 struct tegra_fence *explicit_fence)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    uint32_t syncobj_handle_in;
    struct tegra_fence *f;
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

    if (stream->job_fence) {
        f = stream->job_fence;
        stream->job_fence = NULL;
    } else {
        f = tegra_stream_create_fence_v2(stream, false);
        if (!f) {
            ret = -1;
            goto cleanup;
        }
    }

    assert(!f->active);

    if (explicit_fence)
        syncobj_handle_in = to_fence_v2(explicit_fence)->syncobj_handle;
    else
        syncobj_handle_in = 0;

    if (explicit_fence)
        assert(explicit_fence->active);

    ret = drm_tegra_job_submit_v2(stream->job,
                                  syncobj_handle_in,
                                  to_fence_v2(f)->syncobj_handle,
                                  ~0ull);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v2() failed %d (%s)\n",
                 ret, strerror(ret));
        ret = -1;
    } else {
        TEGRA_FENCE_SET_ACTIVE(f);
        TEGRA_FENCE_WAIT(f);
    }

    TEGRA_FENCE_PUT(f);

cleanup:
    tegra_stream_cleanup_v2(base_stream);

    return ret;
}

static struct tegra_fence *
tegra_stream_submit_v2(enum host1x_engine engine,
                       struct tegra_stream *base_stream,
                       struct tegra_fence *explicit_fence)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    uint32_t syncobj_handle_in;
    struct tegra_fence *f;
    int drm_ver;
    int ret;

    f = stream->base.last_fence[engine];

    /* resubmitting is fine */
    if (stream->base.status == TEGRADRM_STREAM_FREE)
        return f;

    /* return error if stream is constructed badly */
    if (stream->base.status != TEGRADRM_STREAM_READY)
        goto cleanup;

    if (stream->job_fence) {
        f = stream->job_fence;
        stream->job_fence = NULL;
    } else {
        f = tegra_stream_create_fence_v2(stream, engine == TEGRA_2D);
        if (!f)
            goto cleanup;

        f->seqno = base_stream->fence_seqno++;
    }

    assert(!f->active);

    if (explicit_fence)
        syncobj_handle_in = to_fence_v2(explicit_fence)->syncobj_handle;
    else
        syncobj_handle_in = 0;

    if (explicit_fence)
        assert(explicit_fence->active);

    drm_ver = drm_tegra_version(stream->drm);

    /*
     * Since GRATE-kernel v6, the fence is attached to job's syncobject
     * at submission time and not at the job's execution-start time.
     */
    if (syncobj_handle_in && drm_ver < GRATE_KERNEL_DRM_VERSION + 6) {
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
        ret = drmSyncobjWait(drm_tegra_fd(stream->drm), &syncobj_handle_in, 1,
                             gettime_ns() + 1000000000,
                             DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                             NULL);
        if (ret)
            ErrorMsg("drmSyncobjWait(WAIT_FOR_SUBMIT) failed %d\n", ret);
#endif
    }

    ret = drm_tegra_job_submit_v2(stream->job,
                                  syncobj_handle_in,
                                  to_fence_v2(f)->syncobj_handle,
                                  ~0ull);
    if (ret) {
        ErrorMsg("drm_tegra_job_submit_v2() failed %d\n", ret);
        TEGRA_FENCE_MARK_COMPLETED(f);
        TEGRA_FENCE_PUT(f);
        TEGRA_FENCE_WAIT(stream->base.last_fence[engine]);
        TEGRA_FENCE_PUT(stream->base.last_fence[engine]);
        stream->base.last_fence[engine] = f = NULL;
    } else {
        TEGRA_FENCE_PUT(stream->base.last_fence[engine]);
        stream->base.last_fence[engine] = f;
        TEGRA_FENCE_SET_ACTIVE(f);
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

    err = drmSyncobjCreate(drm_tegra_fd(stream->drm), 0, syncobj_handle);
    if (err < 0) {
        ErrorMsg("drmSyncobjCreate() failed %d\n", err);
        return err;
    }
#endif

    return 0;
}

static bool tegra_stream_check_fence_v2(struct tegra_fence *base_fence)
{
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    struct tegra_fence_v2 *f = to_fence_v2(base_fence);
    int ret;

    if (!f->syncobj_handle)
        return true;

    ret = drmSyncobjWait(f->drm_fd, &f->syncobj_handle, 1, 0,
                         DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
                         DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                         NULL);
    if (ret)
        return false;

    drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
    f->syncobj_handle = 0;
#endif

    return true;
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

#ifdef FENCE_DEBUG
    DRMLISTDEL(&f->base.dbg_entry);
    tegra_fences_destroyed++;
#endif

    if (f->syncobj_handle)
        drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
    free(f);
#endif
    return true;
}

static bool
tegra_stream_mark_fence_completed_v2(struct tegra_fence *base_fence)
{
#ifdef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    struct tegra_fence_v2 *f = to_fence_v2(base_fence);

    if (f->syncobj_handle) {
        drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
        f->syncobj_handle = 0;
    }

    TEGRA_FENCE_SET_ACTIVE(base_fence);
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

    f->drm_fd = drm_tegra_fd(stream->drm);
    f->base.check_fence = tegra_stream_check_fence_v2;
    f->base.wait_fence = tegra_stream_wait_fence_v2;
    f->base.free_fence = tegra_stream_free_fence_v2;
    f->base.mark_completed = tegra_stream_mark_fence_completed_v2;
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
                                      unsigned offset,
                                      bool write_dir,
                                      bool explicit_fencing)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    uint32_t flags = 0;
    int drm_ver;
    int ret;

    drm_ver = drm_tegra_version(stream->drm);

    if (write_dir)
        flags |= DRM_TEGRA_BO_TABLE_WRITE;

    /* explicit fencing is bugged on older kernel versions */
    if (explicit_fencing && drm_ver >= GRATE_KERNEL_DRM_VERSION + 5)
        flags |= DRM_TEGRA_BO_TABLE_EXPLICIT_FENCE;

    ret = drm_tegra_job_push_reloc_v2(stream->job, bo, offset, flags);
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
                           unsigned words, int num_relocs, va_list ap)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    struct tegra_reloc reloc_arg;
    uint32_t *pushbuf_ptr;
    uint32_t flags;
    int drm_ver;
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

    drm_ver = drm_tegra_version(stream->drm);

    /* copy the contents */
    pushbuf_ptr = stream->job->ptr;
    memcpy(pushbuf_ptr, addr, words * sizeof(uint32_t));

    /* copy relocs */
    for (; num_relocs; num_relocs--) {
        reloc_arg = va_arg(ap, struct tegra_reloc);

        stream->job->ptr  = pushbuf_ptr;
        stream->job->ptr += reloc_arg.var_offset;

        flags = 0;
        if (reloc_arg.write)
            flags |= DRM_TEGRA_BO_TABLE_WRITE;

        /* explicit fencing is bugged on older kernel versions */
        if (reloc_arg.explicit_fencing && drm_ver >= GRATE_KERNEL_DRM_VERSION + 5)
            flags |= DRM_TEGRA_BO_TABLE_EXPLICIT_FENCE;

        ret = drm_tegra_job_push_reloc_v2(stream->job,
                                          reloc_arg.bo,
                                          reloc_arg.offset,
                                          flags);
        if (ret) {
            stream->base.status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_job_push_reloc_v2() failed %d\n", ret);
            break;
        }
    }

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

static struct tegra_fence *
tegra_stream_get_current_fence_v2(struct tegra_stream *base_stream)
{
    struct tegra_stream_v2 *stream = to_stream_v2(base_stream);
    bool gr2d;

    /*
     * WARNING:
     *
     * Stream v2 returns the final job fence, emitting intermediate fence
     * unsupported.
     */
    if (!stream->job_fence) {
        switch (stream->base.class_id) {
        case HOST1X_CLASS_GR2D:
            gr2d = true;
            break;

        case HOST1X_CLASS_GR3D:
            gr2d = false;
            break;

        default:
            assert(0);
            break;
        }

        stream->job_fence = tegra_stream_create_fence_v2(stream, gr2d);
        stream->job_fence->seqno = base_stream->fence_seqno++;
    }

    return stream->job_fence;
}

int grate_stream_create_v2(struct tegra_stream **pstream,
                           struct drm_tegra *drm)
{
    struct tegra_stream_v2 *stream_v2;
    struct tegra_stream *stream;
    int ret;

#ifndef HAVE_LIBDRM_SYNCOBJ_SUPPORT
    InfoMsg("too old libdrm\n");
    return -1;
#endif

    ret = drm_tegra_version(drm);
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
    stream->current_fence = tegra_stream_get_current_fence_v2;

    stream_v2->drm = drm;

    ret = drm_tegra_job_new_v2(&stream_v2->job, drm,
                               DRM_TEGRA_BO_TABLE_MAX_ENTRIES_NUM,
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
