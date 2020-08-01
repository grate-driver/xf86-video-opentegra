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

static void
tegra_exa_select_copy_func(char *dst, const char *src, int line_len,
                           bool download, bool src_cached, bool dst_cached,
                           tegra_vfp_func *pvfp_func, bool *pvfp_threaded)
{
    tegra_vfp_func vfp_func;
    bool vfp_threaded;
    bool vfp_safe;

    vfp_safe = tegra_memcpy_vfp_copy_is_safe(dst, src, line_len);

    if (vfp_safe && download && !src_cached) {
        vfp_threaded    = true;
        vfp_func        = tegra_memcpy_vfp_aligned;

    } else if (vfp_safe && !src_cached && dst_cached) {
        vfp_threaded    = true;
        vfp_func        = tegra_memcpy_vfp_aligned_dst_cached;

    } else if (vfp_safe && src_cached && !dst_cached) {
        vfp_threaded    = false;
        vfp_func        = tegra_memcpy_vfp_aligned_src_cached;

    } else if (download && !src_cached) {
        vfp_threaded    = true;
        vfp_func        = tegra_memcpy_vfp_unaligned;

    } else {
        vfp_threaded    = false;
        vfp_func        = NULL;
    }

    *pvfp_threaded      = vfp_threaded;
    *pvfp_func          = vfp_func;
}

static bool
tegra_exa_copy_screen(const char *src, int src_pitch, int height,
                      bool download, bool src_cached, bool dst_cached,
                      char *dst, int dst_pitch, int line_len)
{
    tegra_vfp_func vfp_func;
    bool vfp_threaded;
    char pname[128];

    PROFILE_DEF(screen_load);

    if (src_pitch == line_len && src_pitch == dst_pitch) {
        line_len *= height;
        height = 1;
    }

    if (PROFILE)
        sprintf(pname, "%s:%d", download ? "download" : "upload",
                line_len * height);
    PROFILE_SET_NAME(screen_load, pname);
    PROFILE_START(screen_load);

    while (height--) {
        tegra_exa_select_copy_func(dst, src, line_len,
                                   download, src_cached, dst_cached,
                                   &vfp_func, &vfp_threaded);

        if (vfp_func) {
            if (vfp_threaded)
                tegra_memcpy_vfp_threaded(dst, src, line_len, vfp_func);
            else
                vfp_func(dst, src, line_len);
        } else {
            memcpy(dst, src, line_len);
        }

        src += src_pitch;
        dst += dst_pitch;
    }

    PROFILE_STOP(screen_load);

    return true;
}

static bool tegra_exa_load_screen(PixmapPtr pix, int x, int y, int w, int h,
                                  char *usr, int usr_pitch, bool download)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pix->drawable.pScreen);
    struct tegra_pixmap *priv = exaGetPixmapDriverPrivate(pix);
    TegraPtr tegra = TegraPTR(pScrn);
    struct tegra_exa *exa = tegra->exa;
    int offset, pitch, line_len, cpp;
    bool src_cached, dst_cached;
    int access_hint;
    char *pmap;
    bool ret;

    cpp         = pix->drawable.bitsPerPixel >> 3;
    pitch       = exaGetPixmapPitch(pix);
    offset      = (y * pitch) + (x * cpp);
    line_len    = w * cpp;

    if (!line_len || !priv->tegra_data) {
        FALLBACK_MSG("unaccelerateable pixmap %d:%d, %dx%d %d:%d\n",
                     pix->drawable.width, pix->drawable.height,
                     x, y, w, h);
        return false;
    }

    access_hint = download ? EXA_PREPARE_SRC : EXA_PREPARE_DEST;
    ret = tegra_exa_prepare_cpu_access(pix, access_hint, (void**)&pmap, true);
    if (!ret)
        return false;

    if (download) {
        if (priv->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
            src_cached = true;
        else
            src_cached = false;

        dst_cached = true;

        exa->stats.num_screen_uploads++;
        exa->stats.num_screen_uploaded_bytes += line_len * h;
    } else {
        if (priv->type == TEGRA_EXA_PIXMAP_TYPE_FALLBACK)
            dst_cached = true;
        else
            dst_cached = false;

        src_cached = true;

        exa->stats.num_screen_downloads++;
        exa->stats.num_screen_downloaded_bytes += line_len * h;
    }

    ACCEL_MSG("%s pixmap %p %d:%d, %dx%d %d:%d\n",
              download ? "download" : "upload",
              pix, pix->drawable.width, pix->drawable.height, x, y, w, h);

    if (download)
        ret = tegra_exa_copy_screen(pmap + offset, pitch, h,
                                    download, src_cached, dst_cached,
                                    usr, usr_pitch, line_len);
    else
        ret = tegra_exa_copy_screen(usr, usr_pitch, h,
                                    download, src_cached, dst_cached,
                                    pmap + offset, pitch, line_len);

    tegra_exa_finish_cpu_access(pix, access_hint);

    return ret;
}

/* vim: set et sts=4 sw=4 ts=4: */
