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

#define FX10(f)	            (((int32_t)((f) * 256.0f + 0.5f)) & 0x3ff)
#define FX10_L(f)           FX10(f)
#define FX10_H(f)           (FX10(f) << 10)
#define FX10x2(low, high)   (FX10_H(high) | FX10_L(low))

#define TGR3D_VAL(reg_name, field_name, value)                              \
    (((value) << TGR3D_ ## reg_name ## _ ## field_name ## __SHIFT) &        \
                 TGR3D_ ## reg_name ## _ ## field_name ## __MASK)

#define TGR3D_BOOL(reg_name, field_name, boolean)                           \
    ((boolean) ? TGR3D_ ## reg_name ## _ ## field_name : 0)

#define LOG2_SIZE(v)    (31 - __builtin_clz(v))
#define IS_POW2(v)      (((v) & ((v) - 1)) == 0)

void TegraGR3D_UploadConstVP(struct tegra_stream *cmds, unsigned index,
                             float x, float y, float z, float w);

void TegraGR3D_UploadConstFP(struct tegra_stream *cmds, unsigned index,
                             uint32_t constant);

void TegraGR3D_SetupScissor(struct tegra_stream *cmds,
                            unsigned scissor_x,
                            unsigned scissor_y,
                            unsigned scissor_width,
                            unsigned scissor_heigth);

void TegraGR3D_SetupViewportBiasScale(struct tegra_stream *cmds,
                                      float viewport_x_bias,
                                      float viewport_y_bias,
                                      float viewport_z_bias,
                                      float viewport_x_scale,
                                      float viewport_y_scale,
                                      float viewport_z_scale);

void TegraGR3D_SetupAttribute(struct tegra_stream *cmds,
                              unsigned index,
                              struct drm_tegra_bo *bo,
                              unsigned offset, unsigned type,
                              unsigned size, unsigned stride);

void TegraGR3D_SetupRenderTarget(struct tegra_stream *cmds,
                                 unsigned index,
                                 struct drm_tegra_bo *bo,
                                 unsigned offset,
                                 unsigned pixel_format,
                                 unsigned pitch);

void TegraGR3D_EnableRenderTargets(struct tegra_stream *cmds, uint32_t mask);

void TegraGR3D_SetupTextureDesc(struct tegra_stream *cmds,
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
                                bool mirrored_repeat);

void TegraGR3D_SetupDrawParams(struct tegra_stream *cmds,
                               unsigned primitive_type,
                               unsigned index_mode,
                               unsigned first_vtx);

void TegraGR3D_DrawPrimitives(struct tegra_stream *cmds,
                              unsigned first_index, unsigned count);

void TegraGR3D_UploadProgram(struct tegra_stream *cmds,
                             const struct shader_program *prog);

void TegraGR3D_Initialize(struct tegra_stream *cmds);

#endif
