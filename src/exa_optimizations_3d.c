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

#include "driver.h"

static void
tegra_exa_optimize_texture_sampler(TegraGR3DStateTex *tex)
{
    /* optimize wrap-mode if possible */
    if (tex->pPix && !tex->coords_wrap)
        tex->tex_sel = TEX_PAD;
}

static const struct shader_program *
tegra_exa_select_optimized_gr3d_program(TegraGR3DStatePtr state)
{
    const struct tegra_composite_config *cfg = &composite_cfgs[state->new.op];
    const struct shader_program *prog = NULL;
    unsigned mask_sel = state->new.mask.tex_sel;
    unsigned src_sel  = state->new.src.tex_sel;

    /* pow2 texture can use more optimized shaders */
    if (state->new.src.pow2 &&
            (src_sel == TEX_NORMAL || src_sel == TEX_MIRROR))
        src_sel = TEX_PAD;

    /* pow2 texture can use more optimized shaders */
    if (state->new.mask.pow2 &&
            (mask_sel == TEX_NORMAL || mask_sel == TEX_MIRROR))
        mask_sel = TEX_PAD;

    /*
     * Currently all shaders are handling texture transparency and
     * coordinates warp-modes in the assembly, this adds a lot of
     * instructions to the shaders and in result they are quite slow.
     * Ideally we need a proper compiler to build all variants of the
     * custom shaders, but we don't have that luxury at the moment.
     *
     * As a temporary workaround we prepared custom shaders for a
     * couple of most popular texture-operation combinations.
     */
    if (state->new.op == PictOpOver) {
        if (src_sel == TEX_PAD && !state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_opaque_pad_src_solid_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_NORMAL && state->new.src.alpha &&
            mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
                prog = &prog_blend_over_alpha_normal_src_empty_mask_dst_opaque;
                state->new.src.tex_sel = TEX_PAD;
                goto optimized_shader;
        }

        if (src_sel == TEX_NORMAL && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_normal_src_solid_mask;
                state->new.src.tex_sel = TEX_PAD;
                goto optimized_shader;
        }

        if (src_sel == TEX_PAD && state->new.src.alpha &&
            mask_sel == TEX_EMPTY && !state->new.dst.alpha) {
                prog = &prog_blend_over_alpha_pad_src_empty_mask_dst_opaque;
                goto optimized_shader;
        }

        if (src_sel == TEX_PAD && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_pad_src_solid_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_alpha_clipped_src_solid_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_CLIPPED && state->new.src.alpha &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_over_opaque_clipped_src_solid_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
            state->new.dst.alpha) {
                prog = &prog_blend_over_solid_src_empty_mask_dst_alpha;
                goto optimized_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_EMPTY &&
            !state->new.dst.alpha) {
                prog = &prog_blend_over_solid_src_empty_mask_dst_opaque;

                if ((state->new.src.solid >> 24) == 0xff) {
                    prog = &prog_blend_src_solid_mask_src;
                    state->new.mask.solid = 0x00fffffff;
                }

                goto optimized_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
            state->new.mask.component_alpha) {
                if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
                    prog = &prog_blend_over_solid_black_src_clipped_mask_dst_opaque;
                    goto optimized_shader;
                }

                prog = &prog_blend_over_solid_src_clipped_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_PAD &&
            state->new.mask.component_alpha) {
                if (state->new.src.solid == 0xff000000 && !state->new.dst.alpha) {
                    prog = &prog_blend_over_solid_black_src_pad_mask_dst_opaque;
                    goto optimized_shader;
                }

                prog = &prog_blend_over_solid_src_pad_mask;
                goto optimized_shader;
        }

        if (src_sel == TEX_SOLID && mask_sel == TEX_CLIPPED &&
            state->new.src.solid == 0xff000000 && !state->new.dst.alpha &&
            state->new.mask.component_alpha) {
                prog = &prog_blend_over_solid_black_src_clipped_aaaa_mask_dst_opaque;
                goto optimized_shader;
        }
    }

    if (state->new.op == PictOpSrc) {
        if (src_sel == TEX_CLIPPED &&
            (mask_sel == TEX_SOLID || mask_sel == TEX_EMPTY)) {
                prog = &prog_blend_src_clipped_src_solid_mask;
                goto optimized_shader;
        }

        if (mask_sel == TEX_CLIPPED &&
            (src_sel == TEX_SOLID || src_sel == TEX_EMPTY)) {
                prog = &prog_blend_src_solid_src_clipped_mask;
                goto optimized_shader;
        }
    }

    prog = cfg->prog[PROG_SEL(src_sel, mask_sel)];

    if (prog == &prog_blend_add_solid_mask &&
            state->new.dst.alpha && state->new.src.alpha) {
        prog = &prog_blend_add_solid_mask_alpha_src_dst;
        goto optimized_shader;
    }

    if (!prog) {
        FallbackMsg("no shader for operation %d src_sel %u mask_sel %u\n",
                    state->new.op, src_sel, mask_sel);
        return NULL;
    }

    AccelMsg("got shader for operation %d src_sel %u mask_sel %u %s\n",
             state->new.op, src_sel, mask_sel, prog->name);

    return prog;

optimized_shader:
    AccelMsg("custom shader for operation %d src_sel %u mask_sel %u %s\n",
             state->new.op, src_sel, mask_sel, prog->name);

    return prog;
}
