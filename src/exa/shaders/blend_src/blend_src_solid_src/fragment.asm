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
	[0].l = "src_color.r";
	[0].h = "src_color.g";
	[1].l = "src_color.b";
	[1].h = "src_color.a";

	[6].l = "mask_has_per_component_alpha";
	[6].h = "mask_fmt_alpha";

	[8].l = "dst_fmt_alpha";

.asm

EXEC
	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fp20, t0.fp20, NOP, NOP

	// sample tex1 (mask)
	TEX:	tex r2, r3, tex1, r0, r1, r2

	// tmp = mask_fmt_alpha ? mask.a : 1.0
	ALU:
		ALU0:	CSEL  lp.lh, -u6.h, r3.h, #1

	// mask.r = mask_has_per_component_alpha ? mask.r : tmp
	// mask.g = mask_has_per_component_alpha ? mask.g : tmp
	// mask.b = mask_has_per_component_alpha ? mask.b : tmp
	// tmp.a  = src.a * tmp
	ALU:
		ALU0:	CSEL  lp.lh, -u6.l, r2.l, alu0
		ALU1:	CSEL  lp.lh, -u6.l, r2.h, alu0
		ALU2:	CSEL  lp.lh, -u6.l, r3.l, alu0
		ALU3:	MAD   lp.lh,  alu0,   #1,   #0

	// dst = src.bgra * mask.bgra
	ALU:
		ALU0:	MAD  r0.l, alu0, u0.l, #0
		ALU1:	MAD  r0.h, alu1, u0.h, #0
		ALU2:	MAD  r1.l, alu2, u1.l, #0
		ALU3:	MAD  r1.h, alu3, u1.h, u8.l-1 (sat)

	DW:	store rt1, r0, r1
;
