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

#ifndef __TEGRA_GR3D_H
#define __TEGRA_GR3D_H

#include <stdbool.h>
#include <stdint.h>

#include "tegradrm/opentegra_lib.h"

#include "tgr_3d.xml.h"

#define FX10(f)             (((int32_t)((f) * 256.0f + 0.5f)) & 0x3ff)
#define FX10_L(f)           FX10(f)
#define FX10_H(f)           (FX10(f) << 10)
#define FX10x2(low, high)   (FX10_H(high) | FX10_L(low))

#define LOG2_SIZE(v)        (31 - __builtin_clz(v))
#define IS_POW2(v)          (((v) & ((v) - 1)) == 0)

#define TGR3D_VAL(reg_name, field_name, value)                          \
    (((value) << TGR3D_ ## reg_name ## _ ## field_name ## __SHIFT) &    \
                 TGR3D_ ## reg_name ## _ ## field_name ## __MASK)

#define TGR3D_BOOL(reg_name, field_name, boolean)                       \
    ((boolean) ? TGR3D_ ## reg_name ## _ ## field_name : 0)

struct tegra_stream;

struct shader_program {
    const uint32_t *vs_prog_words;
    const unsigned vs_prog_words_nb;
    const uint16_t vs_attrs_in_mask;
    const uint16_t vs_attrs_out_mask;

    const uint32_t *fs_prog_words;
    const uint32_t *fs_prog_words_t114;
    const unsigned fs_prog_words_nb;
    const unsigned fs_alu_buf_size;
    const unsigned fs_pseq_to_dw;
    const unsigned fs_pseq_inst_nb;

    const uint32_t *linker_words;
    const unsigned linker_words_nb;
    const unsigned linker_inst_nb;
    const unsigned used_tram_rows_nb;

    const char *name;
};

void tgr3d_upload_const_vp(struct tegra_stream *cmds, unsigned index,
                           float x, float y, float z, float w);

void tgr3d_upload_const_fp(struct tegra_stream *cmds, unsigned index,
                           uint32_t constant);

void tgr3d_set_scissor(struct tegra_stream *cmds,
                       unsigned scissor_x,
                       unsigned scissor_y,
                       unsigned scissor_width,
                       unsigned scissor_heigth);

void tgr3d_set_viewport_bias_scale(struct tegra_stream *cmds,
                                   float viewport_x_bias,
                                   float viewport_y_bias,
                                   float viewport_z_bias,
                                   float viewport_x_scale,
                                   float viewport_y_scale,
                                   float viewport_z_scale);

void tgr3d_set_vp_attrib_buf(struct tegra_stream *cmds,
                             unsigned index,
                             struct drm_tegra_bo *bo,
                             unsigned offset, unsigned type,
                             unsigned size, unsigned stride,
                             bool explicit_fencing);

void tgr3d_set_vp_attributes_inout_mask(struct tegra_stream *cmds,
                                        uint32_t in_mask,
                                        uint32_t out_mask);

void tgr3d_set_render_target(struct tegra_stream *cmds,
                             unsigned index,
                             struct drm_tegra_bo *bo,
                             unsigned offset,
                             unsigned pixel_format,
                             unsigned pitch,
                             bool explicit_fencing);

void tgr3d_enable_render_targets(struct tegra_stream *cmds, uint32_t mask);

void tgr3d_set_texture_desc(struct tegra_stream *cmds,
                            unsigned index,
                            struct drm_tegra_bo *bo,
                            unsigned offset,
                            unsigned width,
                            unsigned height,
                            unsigned pixel_format,
                            bool min_filter_linear,
                            bool mip_filter_linear,
                            bool mag_filter_linear,
                            bool clamp_to_edge,
                            bool mirrored_repeat,
                            bool explicit_fencing);

void tgr3d_set_draw_params(struct tegra_stream *cmds,
                           unsigned primitive_type,
                           unsigned index_mode,
                           unsigned first_vtx,
                           bool vtx_mem_cache_invalidate,
                           bool vtx_gpu_cache_invalidate);

void tgr3d_draw_primitives(struct tegra_stream *cmds,
                           unsigned first_index, unsigned count);

void tgr3d_upload_program(struct tegra_stream *cmds,
                          const struct shader_program *prog);

void tgr3d_initialize(struct tegra_stream *cmds);

#endif
