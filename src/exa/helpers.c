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

struct tegra_box {
    int x0; int y0;
    int x1; int y1;
};

static bool tegra_exa_simple_transform(PictTransformPtr t)
{
    double e[4];

    if (!t)
        return true;

    e[0] = pixman_fixed_to_double(t->matrix[0][0]);
    e[1] = pixman_fixed_to_double(t->matrix[0][1]);
    e[2] = pixman_fixed_to_double(t->matrix[1][0]);
    e[3] = pixman_fixed_to_double(t->matrix[1][1]);

    if ((e[0] > 0.0f && e[1] == 0   && e[2] == 0   && e[3] > 0.0f) ||
        (e[0] == 0   && e[1] < 0.0f && e[2] > 0.0f && e[3] == 0) ||
        (e[0] < 0.0f && e[1] == 0   && e[2] == 0   && e[3] < 0.0f) ||
        (e[0] == 0   && e[1] > 0.0f && e[2] < 0.0f && e[3] == 0))
        return true;

    return false;
}

static bool tegra_exa_simple_transform_scale(PictTransformPtr t)
{
    if (!t)
        return true;

    /*
     * This is very unlikely to happen in reality, hence ignore this
     * odd case of texture coordinates scaling to simplify vertex program.
     */
    if (t->matrix[2][0] || t->matrix[2][1])
        return false;

    return true;
}

static void tegra_exa_apply_transform(PictTransformPtr t,
                                      struct tegra_box *in,
                                      struct tegra_box *out_transformed)
{
    PictVector v;

    if (t) {
        ACCEL_MSG("orig: %d:%d  %d:%d\n", in->x0, in->y0, in->x1, in->y1);

        v.vector[0] = pixman_int_to_fixed(in->x0);
        v.vector[1] = pixman_int_to_fixed(in->y0);
        v.vector[2] = pixman_int_to_fixed(1);

        PictureTransformPoint3d(t, &v);

        out_transformed->x0 = pixman_fixed_to_int(v.vector[0]);
        out_transformed->y0 = pixman_fixed_to_int(v.vector[1]);

        v.vector[0] = pixman_int_to_fixed(in->x1);
        v.vector[1] = pixman_int_to_fixed(in->y1);
        v.vector[2] = pixman_int_to_fixed(1);

        PictureTransformPoint3d(t, &v);

        out_transformed->x1 = pixman_fixed_to_int(v.vector[0]);
        out_transformed->y1 = pixman_fixed_to_int(v.vector[1]);

        ACCEL_MSG("transformed: %d:%d  %d:%d\n",
                  out_transformed->x0,
                  out_transformed->y0,
                  out_transformed->x1,
                  out_transformed->y1);
    } else {
        out_transformed->x0 = in->x0;
        out_transformed->y0 = in->y0;
        out_transformed->x1 = in->x1;
        out_transformed->y1 = in->y1;
    }
}

static void tegra_exa_clip_to_pixmap_area(PixmapPtr pix,
                                          struct tegra_box *in,
                                          struct tegra_box *out_clipped)
{
    ACCEL_MSG("in: %d:%d  %d:%d\n", in->x0, in->y0, in->x1, in->y1);
    ACCEL_MSG("pix: %d:%d\n", pix->drawable.width, pix->drawable.height);

    if (in->x0 < in->x1) {
        out_clipped->x0 = max(in->x0, 0);
        out_clipped->x1 = min(in->x1, pix->drawable.width);
    } else {
        out_clipped->x0 = max(in->x1, 0);
        out_clipped->x1 = min(in->x0, pix->drawable.width);
    }

    if (in->y0 < in->y1) {
        out_clipped->y0 = max(in->y0, 0);
        out_clipped->y1 = min(in->y1, pix->drawable.height);
    } else {
        out_clipped->y0 = max(in->y1, 0);
        out_clipped->y1 = min(in->y0, pix->drawable.height);
    }

    ACCEL_MSG("out_clipped: %d:%d  %d:%d\n",
              out_clipped->x0, out_clipped->y0,
              out_clipped->x1, out_clipped->y1);
}

static void tegra_exa_get_untransformed(PictTransformPtr t_inv,
                                        struct tegra_box *in,
                                        struct tegra_box *out_untransformed)
{
    PictVector v;

    if (t_inv) {
        v.vector[0] = pixman_int_to_fixed(in->x0);
        v.vector[1] = pixman_int_to_fixed(in->y0);
        v.vector[2] = pixman_int_to_fixed(1);

        PictureTransformPoint3d(t_inv, &v);

        out_untransformed->x0 = pixman_fixed_to_int(v.vector[0]);
        out_untransformed->y0 = pixman_fixed_to_int(v.vector[1]);

        v.vector[0] = pixman_int_to_fixed(in->x1);
        v.vector[1] = pixman_int_to_fixed(in->y1);
        v.vector[2] = pixman_int_to_fixed(1);

        PictureTransformPoint3d(t_inv, &v);

        out_untransformed->x1 = pixman_fixed_to_int(v.vector[0]);
        out_untransformed->y1 = pixman_fixed_to_int(v.vector[1]);

        ACCEL_MSG("untransformed: %d:%d  %d:%d\n",
                  out_untransformed->x0,
                  out_untransformed->y0,
                  out_untransformed->x1,
                  out_untransformed->y1);
    } else {
        out_untransformed->x0 = in->x0;
        out_untransformed->y0 = in->y0;
        out_untransformed->x1 = in->x1;
        out_untransformed->y1 = in->y1;
    }
}

static void tegra_exa_apply_clip(struct tegra_box *in_out,
                                 struct tegra_box *clip,
                                 int offset_x, int offset_y)
{
    ACCEL_MSG("in: %d:%d  %d:%d\n", in_out->x0, in_out->y0, in_out->x1, in_out->y1);
    ACCEL_MSG("clip: %d:%d  %d:%d\n", clip->x0, clip->y0, clip->x1, clip->y1);
    ACCEL_MSG("offset_x: %d offset_y: %d\n", offset_x, offset_y);

    in_out->x0 = max(in_out->x0, clip->x0 + offset_x);
    in_out->y0 = max(in_out->y0, clip->y0 + offset_y);
    in_out->x1 = min(in_out->x1, clip->x1 + offset_x);
    in_out->y1 = min(in_out->y1, clip->y1 + offset_y);

    ACCEL_MSG("out: %d:%d  %d:%d\n",
              in_out->x0, in_out->y0,
              in_out->x1, in_out->y1);
}

static inline bool tegra_exa_is_degenerate(struct tegra_box *b)
{
    return (b->x0 >= b->x1 || b->y0 >= b->y1);
}

static void
tegra_exa_wait_pixmaps(enum host1x_engine engine, PixmapPtr dst_pix,
                       int num_src_pixmaps, ...)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pix->drawable.pScreen);
    struct tegra_pixmap *priv;
    PixmapPtr pix_arg;
    int drm_ver;
    va_list ap;

    /* GRATE-kernel supports fencing, hence no need to fence here */
    drm_ver = drm_tegra_version(TegraPTR(pScrn)->drm);
    if (drm_ver >= GRATE_KERNEL_DRM_VERSION + 2)
        return;

    va_start(ap, num_src_pixmaps);
    for (; num_src_pixmaps; num_src_pixmaps--) {
        pix_arg = va_arg(ap, PixmapPtr);
        if (!pix_arg)
            continue;

        priv = exaGetPixmapDriverPrivate(pix_arg);
        TEGRA_WAIT_AND_PUT_FENCE(priv->fence_write[engine]);
    }
    va_end(ap);

    priv = exaGetPixmapDriverPrivate(dst_pix);
    TEGRA_WAIT_AND_PUT_FENCE(priv->fence_write[engine]);
    TEGRA_WAIT_AND_PUT_FENCE(priv->fence_read[engine]);
}

static void tegra_exa_replace_pixmaps_fence(enum host1x_engine engine,
                                            struct tegra_fence *fence,
                                            void *opaque, PixmapPtr dst_pix,
                                            int num_src_pixmaps, ...)
{
    struct tegra_pixmap *priv;
    PixmapPtr pix_arg;
    va_list ap;

    priv = exaGetPixmapDriverPrivate(dst_pix);

    if (priv->fence_write[engine] != fence) {
        TEGRA_FENCE_PUT(priv->fence_write[engine]);
        priv->fence_write[engine] = TEGRA_FENCE_GET(fence, opaque);
    }

    va_start(ap, num_src_pixmaps);
    for (; num_src_pixmaps; num_src_pixmaps--) {
        pix_arg = va_arg(ap, PixmapPtr);
        if (!pix_arg)
            continue;

        priv = exaGetPixmapDriverPrivate(pix_arg);

        if (priv->fence_read[engine] != fence) {
            TEGRA_FENCE_PUT(priv->fence_read[engine]);
            priv->fence_read[engine] = TEGRA_FENCE_GET(fence, opaque);
        }
    }
    va_end(ap);
}

#define SWAP_EXPLICIT_FENCE(EXPLICIT_FENCE, FENCE, LAST_SEQNO)      \
{                                                                   \
    if (FENCE && FENCE->seqno >= LAST_SEQNO) {                      \
        TEGRA_FENCE_PUT(EXPLICIT_FENCE);                            \
        EXPLICIT_FENCE = TEGRA_FENCE_GET(FENCE, NULL);              \
        LAST_SEQNO = EXPLICIT_FENCE->seqno;                         \
    }                                                               \
}

/*
 * This function returns the last fence (selected among pixmaps by highest
 * sequential number of pixmap's fence) that needs to be waited before job
 * could start execution on hardware. In particular this is needed in order
 * to avoid unnecessary stalls on pool-allocated pixmaps that share the same
 * BO.
 *
 * The returned explicit fence shall be put once done with it.
 */
static struct tegra_fence *
tegra_exa_get_explicit_fence(enum host1x_engine engine,
                             PixmapPtr dst_pix,
                             int num_src_pixmaps, ...)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(dst_pix->drawable.pScreen);
    struct tegra_fence *explicit_fence = NULL;
    uint64_t last_seqno = 0;
    struct tegra_pixmap *priv;
    PixmapPtr pix_arg;
    int drm_ver;
    va_list ap;

    /*
     * Vanilla upstream kernel and older GRATE-kernel don't support
     * explicit fencing.
     *
     * Explicit fencing is also bugged on older GRATE-kernel versions.
     */
    drm_ver = drm_tegra_version(TegraPTR(pScrn)->drm);
    if (drm_ver < GRATE_KERNEL_DRM_VERSION + 5)
        return NULL;

    va_start(ap, num_src_pixmaps);
    for (; num_src_pixmaps; num_src_pixmaps--) {
        pix_arg = va_arg(ap, PixmapPtr);
        if (!pix_arg)
            continue;

        priv = exaGetPixmapDriverPrivate(pix_arg);

        SWAP_EXPLICIT_FENCE(explicit_fence, priv->fence_write[engine],
                            last_seqno);
    }
    va_end(ap);

    priv = exaGetPixmapDriverPrivate(dst_pix);

    SWAP_EXPLICIT_FENCE(explicit_fence, priv->fence_write[engine], last_seqno);
    SWAP_EXPLICIT_FENCE(explicit_fence, priv->fence_read[engine],  last_seqno);

    return explicit_fence;
}

/*
 * Returns fence with the biggest seqno, puts older fences.
 */
static struct tegra_fence *tegra_exa_select_latest_fence(int num_fences, ...)
{
    struct tegra_fence *latest_fence = NULL;
    struct tegra_fence *fence_arg;
    uint64_t last_seqno = 0;
    va_list ap;

    va_start(ap, num_fences);
    for (; num_fences; num_fences--) {
        fence_arg = va_arg(ap, struct tegra_fence *);

        if (fence_arg && fence_arg->seqno >= last_seqno) {
            if (latest_fence != fence_arg)
                TEGRA_FENCE_PUT(latest_fence);

            latest_fence = fence_arg;
            last_seqno = latest_fence->seqno;
        } else {
             TEGRA_FENCE_PUT(fence_arg);
        }
    }
    va_end(ap);

    return latest_fence;
}

/* vim: set et sts=4 sw=4 ts=4: */
