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

#ifndef __TEGRA_GR3D_SHADER_PROG_H
#define __TEGRA_GR3D_SHADER_PROG_H

struct shader_program {
    const uint32_t *vs_prog_words;
    const unsigned vs_prog_words_nb;
    const uint16_t vs_attrs_in_mask;
    const uint16_t vs_attrs_out_mask;

    const uint32_t *fs_prog_words;
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

#endif
