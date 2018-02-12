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

#include "driver.h"

static void TegraGR3D_InitState(struct tegra_stream *cmds)
{
    unsigned  i;

    /* Tegra30 specific stuff */

    tegra_stream_prep(cmds, 17);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x750, 16));
    for (i = 0; i < 16; i++)
        tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 6);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x907, 5));
    for (i = 0; i < 5; i++)
        tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 3);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0xb00, 2));
    tegra_stream_push(cmds, 0x00000003);
    tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 1);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xb04, 0x00000000));

    tegra_stream_prep(cmds, 14);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0xb06, 13));
    for (i = 0; i < 13; i++)
        tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 1);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xb14, 0x00000000));

    /* End of Tegra30 specific stuff */

    tegra_stream_prep(cmds, 10);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x00d, 9));
    for (i = 0; i < 9; i++)
        tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 3);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x124, 0x00000007));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x125, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x126, 0x00000000));

    tegra_stream_prep(cmds, 6);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x200, 5));
    tegra_stream_push(cmds, 0x00000011);
    tegra_stream_push(cmds, 0x0000ffff);
    tegra_stream_push(cmds, 0x00ff0000);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 3);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x209, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x20a, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x20b, 0x00000003));

    tegra_stream_prep(cmds, 5);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x34e, 0x3f800000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x34f, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x35b, 0x00000205));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x363, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x364, 0x00000000));

    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x412, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x413, 0x00000000));

    tegra_stream_prep(cmds, 32);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x521, 31));
    for (i = 1; i < 32; i++)
        tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 4);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xe40, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xe41, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xe25, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0xe26, 0x00000000));

    tegra_stream_prep(cmds, 13);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x406, 12));
    tegra_stream_push(cmds, 0x00000001);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x1fff1fff);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000006);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000008);
    tegra_stream_push(cmds, 0x00000048);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);

    tegra_stream_prep(cmds, 13);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x501, 0x00000007));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x502, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x503, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x542, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x543, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x544, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x545, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x60e, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x702, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x740, 0x00000035));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x741, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x742, 0x00000000));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x902, 0x00000000));

    tegra_stream_prep(cmds, 14);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_FDC_CONTROL, 13));
    tegra_stream_push(cmds, 0x00000e00 | TGR3D_FDC_CONTROL_INVALIDATE);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x000001ff);
    tegra_stream_push(cmds, 0x000001ff);
    tegra_stream_push(cmds, 0x000001ff);
    tegra_stream_push(cmds, 0x00000030);
    tegra_stream_push(cmds, 0x00000020);
    tegra_stream_push(cmds, 0x000001ff);
    tegra_stream_push(cmds, 0x00000100);
    tegra_stream_push(cmds, 0x0f0f0f0f);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);
    tegra_stream_push(cmds, 0x00000000);
}

void TegraGR3D_UploadConstVP(struct tegra_stream *cmds, unsigned index,
                             float x, float y, float z, float w)
{
    tegra_stream_prep(cmds, 6);

    tegra_stream_push(cmds,
                      HOST1X_OPCODE_IMM(TGR3D_VP_UPLOAD_CONST_ID, index * 4));
    tegra_stream_push(cmds,
                      HOST1X_OPCODE_NONINCR(TGR3D_VP_UPLOAD_CONST, 4));

    tegra_stream_pushf(cmds, x);
    tegra_stream_pushf(cmds, y);
    tegra_stream_pushf(cmds, z);
    tegra_stream_pushf(cmds, w);
}

void TegraGR3D_UploadConstFP(struct tegra_stream *cmds, unsigned index,
                             uint32_t constant)
{
    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_FP_CONST(index), 1));
    tegra_stream_push(cmds, constant);
}

void TegraGR3D_SetupScissor(struct tegra_stream *cmds,
                            unsigned scissor_x,
                            unsigned scissor_y,
                            unsigned scissor_width,
                            unsigned scissor_heigth)
{
    uint32_t value;

    tegra_stream_prep(cmds, 3);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_SCISSOR_HORIZ, 2));

    value  = TGR3D_VAL(SCISSOR_HORIZ, MIN, scissor_x);
    value |= TGR3D_VAL(SCISSOR_HORIZ, MAX, scissor_x + scissor_width);

    tegra_stream_push(cmds, value);

    value  = TGR3D_VAL(SCISSOR_VERT, MIN, scissor_y);
    value |= TGR3D_VAL(SCISSOR_VERT, MAX, scissor_y + scissor_heigth);

    tegra_stream_push(cmds, value);
}

static void TegraGR3D_SetupGuardband(struct tegra_stream *cmds)
{
    tegra_stream_prep(cmds, 4);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_GUARDBAND_WIDTH, 3));
    tegra_stream_pushf(cmds, 1.0f);
    tegra_stream_pushf(cmds, 1.0f);
    tegra_stream_pushf(cmds, 1.0f);
}

static void TegraGR3D_SetupLateTest(struct tegra_stream *cmds)
{
    uint32_t value = 0x48;

    tegra_stream_prep(cmds, 1);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(0x40f, value));
}

static void TegraGR3D_SetupDepthRange(struct tegra_stream *cmds)
{
    tegra_stream_prep(cmds, 3);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_DEPTH_RANGE_NEAR, 2));
    tegra_stream_push(cmds, (uint32_t)(0xFFFFF * 0.0f));
    tegra_stream_push(cmds, (uint32_t)(0xFFFFF * 1.0f));
}

static void TegraGR3D_SetupDepthBuffer(struct tegra_stream *cmds)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 2);

    value |= TGR3D_VAL(DEPTH_TEST_PARAMS, FUNC, TGR3D_COMPARE_FUNC_ALWAYS);
    value |= 0x200;

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_DEPTH_TEST_PARAMS, 1));
    tegra_stream_pushf(cmds, value);
}

static void TegraGR3D_SetupPolygonOffset(struct tegra_stream *cmds)
{
    tegra_stream_prep(cmds, 3);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_POLYGON_OFFSET_UNITS, 2));
    tegra_stream_pushf(cmds, 0.0f);
    tegra_stream_pushf(cmds, 0.0f);
}

static void TegraGR3D_SetupPSEQ_DW_cfg(struct tegra_stream *cmds,
                                       unsigned pseq_to_dw_nb)
{
    uint32_t value = TGR3D_VAL(FP_PSEQ_DW_CFG, PSEQ_TO_DW_EXEC_NB,
                               pseq_to_dw_nb);

    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_FP_PSEQ_DW_CFG, 1));
    tegra_stream_push(cmds, value);
}

static void TegraGR3D_SetupALUBufferSize(struct tegra_stream *cmds,
                                         unsigned alu_buf_size)
{
    unsigned unk_pseq_cfg = 0x12C;
    uint32_t value = 0;

    tegra_stream_prep(cmds, 4);

    value |= TGR3D_VAL(ALU_BUFFER_SIZE, SIZE, alu_buf_size - 1);
    value |= TGR3D_VAL(ALU_BUFFER_SIZE, SIZEx4,
                       (unk_pseq_cfg - 1) / (alu_buf_size * 4));

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_ALU_BUFFER_SIZE, 1));
    tegra_stream_push(cmds, value);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(0x501, 1));
    tegra_stream_push(cmds, (0x0032 << 16) | (unk_pseq_cfg << 4) | 0xF);
}

static void TegraGR3D_SetupStencilTest(struct tegra_stream *cmds)
{
    uint32_t value;

    tegra_stream_prep(cmds, 7);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_STENCIL_FRONT1, 3));

    value  = TGR3D_VAL(STENCIL_FRONT1, MASK, 0);
    value |= TGR3D_VAL(STENCIL_FRONT1, FUNC, TGR3D_COMPARE_FUNC_NEVER);
    tegra_stream_push(cmds, value);

    value  = TGR3D_VAL(STENCIL_BACK1, MASK, 0);
    value |= TGR3D_VAL(STENCIL_BACK1, FUNC, TGR3D_COMPARE_FUNC_NEVER);
    tegra_stream_push(cmds, value);

    value  = TGR3D_BOOL(STENCIL_PARAMS, STENCIL_TEST, 0);
    value |= TGR3D_BOOL(STENCIL_PARAMS, STENCIL_WRITE_EARLY, 0);
    value |= 0x8;
    tegra_stream_push(cmds, value);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_STENCIL_FRONT2, 2));

    value  = TGR3D_VAL(STENCIL_FRONT2, REF, 0);
    value |= TGR3D_VAL(STENCIL_FRONT2, OP_FAIL, TGR3D_STENCIL_OP_ZERO);
    value |= TGR3D_VAL(STENCIL_FRONT2, OP_ZFAIL, TGR3D_STENCIL_OP_ZERO);
    value |= TGR3D_VAL(STENCIL_FRONT2, OP_ZPASS, TGR3D_STENCIL_OP_ZERO);
    value |= 0x1fe00;
    tegra_stream_push(cmds, value);

    value  = TGR3D_VAL(STENCIL_BACK2, REF, 0);
    value |= TGR3D_VAL(STENCIL_BACK2, OP_FAIL, TGR3D_STENCIL_OP_ZERO);
    value |= TGR3D_VAL(STENCIL_BACK2, OP_ZFAIL, TGR3D_STENCIL_OP_ZERO);
    value |= TGR3D_VAL(STENCIL_BACK2, OP_ZPASS, TGR3D_STENCIL_OP_ZERO);
    value |= 0x1fe00;
    tegra_stream_push(cmds, value);
}

static void TegraGR3D_Startup_PSEQ_Engine(struct tegra_stream *cmds,
                                          unsigned pseq_inst_nb)
{
    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_FP_PSEQ_ENGINE_INST, 1));
    tegra_stream_push(cmds, 0x20006000 | pseq_inst_nb);
}

static void TegraGR3D_SetUsed_TRAM_RowsNum(struct tegra_stream *cmds,
                                           unsigned used_tram_rows_nb)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 2);

    value |= TGR3D_VAL(TRAM_SETUP, USED_TRAM_ROWS_NB, used_tram_rows_nb);
    value |= TGR3D_VAL(TRAM_SETUP, DIV64, 64 / used_tram_rows_nb);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_TRAM_SETUP, 1));
    tegra_stream_push(cmds, value);
}

void TegraGR3D_SetupViewportBiasScale(struct tegra_stream *cmds,
                                      float viewport_x_bias,
                                      float viewport_y_bias,
                                      float viewport_z_bias,
                                      float viewport_x_scale,
                                      float viewport_y_scale,
                                      float viewport_z_scale)
{
    tegra_stream_prep(cmds, 7);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_VIEWPORT_X_BIAS, 6));
    tegra_stream_pushf(cmds, viewport_x_bias * 16.0f + viewport_x_scale * 8.0f);
    tegra_stream_pushf(cmds, viewport_y_bias * 16.0f + viewport_y_scale * 8.0f);
    tegra_stream_pushf(cmds, viewport_z_bias - 4.76837158203125e-07);
    tegra_stream_pushf(cmds, viewport_x_scale * 8.0f);
    tegra_stream_pushf(cmds, viewport_y_scale * 8.0f);
    tegra_stream_pushf(cmds, viewport_z_scale - 4.76837158203125e-07);
}

static void TegraGR3D_SetupCullFaceAndLinkerInstNum(struct tegra_stream *cmds,
                                                  unsigned linker_inst_nb)
{
    uint32_t unk = 0x2E38;
    uint32_t value = 0;

    tegra_stream_prep(cmds, 2);

    value |= TGR3D_VAL(CULL_FACE_LINKER_SETUP, CULL_FACE, TGR3D_CULL_FACE_NONE);

    value |= TGR3D_VAL(CULL_FACE_LINKER_SETUP, LINKER_INST_COUNT,
                       linker_inst_nb - 1);
    value |= TGR3D_VAL(CULL_FACE_LINKER_SETUP, UNK_18_31, unk);

    tegra_stream_push(cmds,
                      HOST1X_OPCODE_INCR(TGR3D_CULL_FACE_LINKER_SETUP, 1));
    tegra_stream_push(cmds, value);
}

void TegraGR3D_SetupAttribute(struct tegra_stream *cmds,
                              unsigned index,
                              struct drm_tegra_bo *bo,
                              unsigned offset, unsigned type,
                              unsigned size, unsigned stride)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 3);

    value |= TGR3D_VAL(ATTRIB_MODE, TYPE, type);
    value |= TGR3D_VAL(ATTRIB_MODE, SIZE, size);
    value |= TGR3D_VAL(ATTRIB_MODE, STRIDE, stride);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_ATTRIB_PTR(index), 2));
    tegra_stream_push_reloc(cmds, bo, offset);
    tegra_stream_push(cmds, value);
}

static void TegraGR3D_SetupVpAttributesInOutMask(struct tegra_stream *cmds,
                                               uint32_t in_mask,
                                               uint32_t out_mask)
{
    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_VP_ATTRIB_IN_OUT_SELECT, 1));
    tegra_stream_push(cmds, in_mask << 16 | out_mask);
}

void TegraGR3D_SetupRenderTarget(struct tegra_stream *cmds,
                                 unsigned index,
                                 struct drm_tegra_bo *bo,
                                 unsigned offset,
                                 unsigned pixel_format,
                                 unsigned pitch)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 4);

    value |= TGR3D_VAL(RT_PARAMS, FORMAT, pixel_format);
    value |= TGR3D_VAL(RT_PARAMS, PITCH, pitch);
    value |= TGR3D_BOOL(RT_PARAMS, TILED, 0);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_RT_PARAMS(index), 1));
    tegra_stream_push(cmds, value);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_RT_PTR(index), 1));
    tegra_stream_push_reloc(cmds, bo, offset);
}

void TegraGR3D_EnableRenderTargets(struct tegra_stream *cmds, uint32_t mask)
{
    tegra_stream_prep(cmds, 2);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_RT_ENABLE, 1));
    tegra_stream_push(cmds, mask);
}

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
                                bool mirrored_repeat)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 5);
    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_TEXTURE_DESC1(index), 2));

    value  = TGR3D_VAL(TEXTURE_DESC1, FORMAT, pixel_format);
    value |= TGR3D_BOOL(TEXTURE_DESC1, MINFILTER_LINEAR_WITHIN, min_filter_linear);
    value |= TGR3D_BOOL(TEXTURE_DESC1, MINFILTER_LINEAR_BETWEEN, mip_filter_linear);
    value |= TGR3D_BOOL(TEXTURE_DESC1, MAGFILTER_LINEAR, mag_filter_linear);
    value |= TGR3D_BOOL(TEXTURE_DESC1, WRAP_T_CLAMP_TO_EDGE, clamp_to_edge);
    value |= TGR3D_BOOL(TEXTURE_DESC1, WRAP_S_CLAMP_TO_EDGE, clamp_to_edge);
    value |= TGR3D_BOOL(TEXTURE_DESC1, WRAP_T_MIRRORED_REPEAT, mirrored_repeat);
    value |= TGR3D_BOOL(TEXTURE_DESC1, WRAP_S_MIRRORED_REPEAT, mirrored_repeat);

    tegra_stream_push(cmds, value);

    value = TGR3D_BOOL(TEXTURE_DESC2, MIPMAP_DISABLE, 1);

    if (IS_POW2(width) && IS_POW2(height)) {
        value |= TGR3D_VAL(TEXTURE_DESC2, WIDTH_LOG2, LOG2_SIZE(width));
        value |= TGR3D_VAL(TEXTURE_DESC2, HEIGHT_LOG2, LOG2_SIZE(height));
    } else {
        value |= TGR3D_BOOL(TEXTURE_DESC2, NOT_POW2_DIMENSIONS, 1);
        value |= TGR3D_VAL(TEXTURE_DESC2, WIDTH, width);
        value |= TGR3D_VAL(TEXTURE_DESC2, HEIGHT, height);
    }
    tegra_stream_push(cmds, value);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_TEXTURE_POINTER(index), 1));
    tegra_stream_push_reloc(cmds, bo, offset);
}

void TegraGR3D_SetupDrawParams(struct tegra_stream *cmds,
                               unsigned primitive_type,
                               unsigned index_mode,
                               unsigned first_vtx)
{
    uint32_t value = 0;

    tegra_stream_prep(cmds, 2);

    value |= TGR3D_VAL(DRAW_PARAMS, INDEX_MODE, index_mode);
    value |= TGR3D_VAL(DRAW_PARAMS, PROVOKING_VERTEX, 0);
    value |= TGR3D_VAL(DRAW_PARAMS, PRIMITIVE_TYPE, primitive_type);
    value |= TGR3D_VAL(DRAW_PARAMS, FIRST, first_vtx);
    value |= 0xC0000000;

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_DRAW_PARAMS, 1));
    tegra_stream_push(cmds, value);
}

void TegraGR3D_DrawPrimitives(struct tegra_stream *cmds,
                              unsigned first_index, unsigned count)
{
    uint32_t value = 0;

    /*
     * XXX: This requires proper waitcheck barrier, expect graphical
     *      glitches due to not properly prefetched vertex / tex data.
     */
    tegra_stream_sync(cmds, DRM_TEGRA_SYNCPT_COND_RD_DONE);

    tegra_stream_prep(cmds, 2);

    value |= TGR3D_VAL(DRAW_PRIMITIVES, INDEX_COUNT, count - 1);
    value |= TGR3D_VAL(DRAW_PRIMITIVES, OFFSET, first_index);

    tegra_stream_push(cmds, HOST1X_OPCODE_INCR(TGR3D_DRAW_PRIMITIVES, 1));
    tegra_stream_push(cmds, value);

    tegra_stream_sync(cmds, DRM_TEGRA_SYNCPT_COND_OP_DONE);
}

static void TegraGR3D_UploadProgram(struct tegra_stream *cmds,
                                    const struct shader_program *prog)
{
    unsigned i;

    tegra_stream_prep(cmds, 4);
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(TGR3D_VP_UPLOAD_INST_ID, 0));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(TGR3D_FP_UPLOAD_INST_ID_COMMON, 0));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(TGR3D_FP_UPLOAD_MFU_INST_ID, 0));
    tegra_stream_push(cmds, HOST1X_OPCODE_IMM(TGR3D_FP_UPLOAD_ALU_INST_ID, 0));

    tegra_stream_prep(cmds, prog->vs_prog_words_nb);
    for (i = 0; i < prog->vs_prog_words_nb; i++)
        tegra_stream_push(cmds, prog->vs_prog_words[i]);

    tegra_stream_prep(cmds, prog->fs_prog_words_nb);
    for (i = 0; i < prog->fs_prog_words_nb; i++)
        tegra_stream_push(cmds, prog->fs_prog_words[i]);

    tegra_stream_prep(cmds, prog->linker_words_nb);
    for (i = 0; i < prog->linker_words_nb; i++)
        tegra_stream_push(cmds, prog->linker_words[i]);
}

void TegraGR3D_Initialize(struct tegra_stream *cmds,
                          const struct shader_program *prog)
{
    TegraGR3D_InitState(cmds);
    TegraGR3D_SetupGuardband(cmds);
    TegraGR3D_SetupLateTest(cmds);
    TegraGR3D_SetupDepthRange(cmds);
    TegraGR3D_SetupDepthBuffer(cmds);
    TegraGR3D_SetupStencilTest(cmds);
    TegraGR3D_SetupPolygonOffset(cmds);
    TegraGR3D_SetupVpAttributesInOutMask(cmds,
        prog->vs_attrs_in_mask, prog->vs_attrs_out_mask);
    TegraGR3D_SetupPSEQ_DW_cfg(cmds, prog->fs_pseq_to_dw);
    TegraGR3D_SetupALUBufferSize(cmds, prog->fs_alu_buf_size);
    TegraGR3D_Startup_PSEQ_Engine(cmds, prog->fs_pseq_inst_nb);
    TegraGR3D_SetUsed_TRAM_RowsNum(cmds, prog->used_tram_rows_nb);
    TegraGR3D_SetupCullFaceAndLinkerInstNum(cmds, prog->linker_inst_nb);
    TegraGR3D_UploadProgram(cmds, prog);
}
