/*
 * Copyright (c) GRATE-DRIVER project 2019
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

	[8].l = "dst_fmt_alpha";

.asm

EXEC
	// fetch dst pixel to r2,r3
	PSEQ:	0x0081000A

	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fp20, t0.fp20, NOP, NOP

	// sample tex1 (mask)
	TEX:	tex r0, r1, tex1, r0, r1, r2

	// tmp = -src.aaaa * mask.bgra + 1
	ALU:
		ALU0:	MAD  lp.lh, -r0.l, u1.h, #1
		ALU1:	MAD  lp.lh, -r0.h, u1.h, #1
		ALU2:	MAD  lp.lh, -r1.l, u1.h, #1
		ALU3:	MAD  lp.lh, -r1.h, u1.h, u8.l

	// tmp = tmp * dst.bgra
	ALU:
		ALU0:	MAD  lp.lh, alu0, r2.l, #0
		ALU1:	MAD  lp.lh, alu1, r2.h, #0
		ALU2:	MAD  lp.lh, alu2, r3.l, #0
		ALU3:	MAD  lp.lh, alu3, r3.h, u8.l-1

	// r0,r1 = src.bgra * mask.bgra + tmp
	ALU:
		ALU0:	MAD  r0.l, u0.l, r0.l, alu0 (sat)
		ALU1:	MAD  r0.h, u0.h, r0.h, alu1 (sat)
		ALU2:	MAD  r1.l, u1.l, r1.l, alu2 (sat)
		ALU3:	MAD  r1.h, u1.h, r1.h, alu3 (sat)

	DW:	store rt1, r0, r1
;
