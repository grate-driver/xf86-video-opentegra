/*
 * Copyright (c) Dmitry Osipenko 2018
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

pseq_to_dw_exec_nb = 1	// the number of 'EXEC' block where DW happens
alu_buffer_size = 1	// number of .rgba regs carried through pipeline

.uniforms
	[2].l = "mask_color.r";
	[2].h = "mask_color.g";
	[3].l = "mask_color.b";
	[3].h = "mask_color.a";

	[5].l = "src_fmt_alpha";

.asm

EXEC
	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fp20, t0.fp20, NOP, NOP

	// sample tex0 (src)
	TEX:	tex r2, r3, tex0, r0, r1, r2

	ALU:
		ALU0:	CSEL  lp.lh, r0,  -#1,  #0 (this)
		ALU1:	CSEL  lp.lh, r0-1, #0, -#1 (other)
		ALU2:	CSEL  lp.lh, r1,  -#1,  #0 (other)
		ALU3:	CSEL  lp.lh, r1-1, #0, -#1

	// dst = src.bgra * mask.bgra
	ALU:
		ALU0:	MAD  r0.l,  r2.l, u2.l, alu0 (sat)
		ALU1:	MAD  r0.h,  r2.h, u2.h, alu0 (sat)
		ALU2:	MAD  r1.l,  r3.l, u3.l, alu0 (sat)
		ALU3:	MAD  lp.lh, r3.h, u3.h, u5.l (sat)

	ALU:
		ALU3:	MAD  r1.h, alu3, #1, u5.l (sat)

	DW:	store rt1, r0, r1
;
