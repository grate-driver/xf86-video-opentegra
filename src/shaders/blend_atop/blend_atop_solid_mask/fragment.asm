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

pseq_to_dw_exec_nb = 4	// the number of 'EXEC' block where DW happens
alu_buffer_size = 2	// number of .rgba regs carried through pipeline

.uniforms
	[2].l = "mask_color.r";
	[2].h = "mask_color.g";
	[3].l = "mask_color.b";
	[3].h = "mask_color.a";

	[5].l = "src_fmt_alpha";

	[8].l = "dst_fmt_alpha";

.asm

// First batch
EXEC
	// fetch dst pixel to r2,r3
	PSEQ:	0x0081000A

	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fp20, t0.fp20, NOP, NOP

	// sample tex0 (src)
	TEX:	tex r0, r1, tex0, r0, r1, r2

	// src.a = src_fmt_alpha ? src.a : 1.0
	ALU:
		ALU0:	CSEL  r3.h, -u8.l, r3.h, #1
		ALU3:	CSEL lp.lh, -u5.l, r1.h, #1

	// tmp = -src.aaaa * mask.bgra + 1
	ALU:
		ALU0:	MAD  lp.lh, -alu3, u2.l, #1
		ALU1:	MAD  lp.lh, -alu3, u2.h, #1
		ALU2:	MAD  lp.lh, -alu3, u3.l, #1
		ALU3:	MAD  lp.lh, -alu3, u3.h, u8.l (sat)

	// r0,r1 = (1 - src.aaaa * mask.bgra) * dst.bgra
	ALU:
		ALU0:	MAD  r0.l, r2.l, alu0, #0
		ALU1:	MAD  r0.h, r2.h, alu1, #0
		ALU2:	MAD  r1.l, r3.l, alu2, #0
		ALU3:	MAD  r1.h, r3.h, alu3, #0
;

EXEC
	// src.a = src_fmt_alpha ? src.a : 1.0
	ALU:
		ALU0:	MAD  r6.h, r0.h, #1, #0
		ALU1:	MAD  r6.l, r0.l, #1, #0
		ALU2:	MAD  r7.l, r1.l, #1, #0
		ALU3:	CSEL r7.h, -u5.l, r1.h, #1
;

// Second batch
EXEC
	// tmp =  src.bgra * mask.bgra
	ALU:
		ALU0:	MAD  lp.lh, r6.l, u2.l, #0
		ALU1:	MAD  lp.lh, r6.h, u2.h, #0
		ALU2:	MAD  lp.lh, r7.l, u3.l, #0
		ALU3:	MAD  lp.lh, r7.h, u3.h, u8.l-1 (sat)

	// r0,r1 = (1 - src.aaaa * mask.bgra) * dst.bgra + src.bgra * mask.bgra * dst.aaaa
	ALU:
		ALU0:	MAD  r0.l, r3.h, alu0, r0.l (sat)
		ALU1:	MAD  r0.h, r3.h, alu1, r0.h (sat)
		ALU2:	MAD  r1.l, r3.h, alu2, r1.l (sat)
		ALU3:	MAD  r1.h, r3.h, alu3, r1.h (sat)

	DW:	store rt1, r0, r1
;

EXEC
;
