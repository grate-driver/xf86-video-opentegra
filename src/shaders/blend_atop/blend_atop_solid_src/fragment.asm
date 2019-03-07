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

pseq_to_dw_exec_nb = 6	// the number of 'EXEC' block where DW happens
alu_buffer_size = 2	// number of .rgba regs carried through pipeline

.uniforms
	[0].l = "src_color.r";
	[0].h = "src_color.g";
	[1].l = "src_color.b";
	[1].h = "src_color.a";

	[6].l = "mask_has_per_component_alpha";
	[6].h = "mask_fmt_alpha";

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

	// sample tex1 (mask)
	TEX:	tex r0, r1, tex1, r0, r1, r2

	// tmp = mask_fmt_alpha ? mask.a : 1.0
	ALU:
		ALU0:	CSEL lp.lh, -u6.h, r1.h, #1
		ALU1:	CSEL  r3.h, -u8.l, r3.h, #1

	// mask.r = mask_has_per_component_alpha ? mask.r : tmp
	// mask.g = mask_has_per_component_alpha ? mask.g : tmp
	// mask.b = mask_has_per_component_alpha ? mask.b : tmp
	// mask.a = dst_fmt_alpha ? tmp : 0.0
	ALU:
		ALU0:	CSEL r0.l, -u6.l, r0.l, alu0
		ALU1:	CSEL r0.h, -u6.l, r0.h, alu0
		ALU2:	CSEL r1.l, -u6.l, r1.l, alu0
		ALU3:	CSEL r1.h, -u8.l, alu0, #0
;

// Second batch
EXEC
;

EXEC
	// tmp = -src.aaaa * mask.bgra + 1
	ALU:
		ALU0:	MAD  r4.l, -u1.h, r0.l, #1
		ALU1:	MAD  r4.h, -u1.h, r0.h, #1
		ALU2:	MAD  r5.l, -u1.h, r1.l, #1
		ALU3:	MAD  r5.h, -u1.h, r1.h, u8.l (sat)
;

EXEC
;

// Third batch
EXEC
	// r0,r1 = (1 - src.aaaa * mask.bgra) * dst.bgra
	ALU:
		ALU0:	MAD  r4.l, r2.l, r4.l, #0
		ALU1:	MAD  r4.h, r2.h, r4.h, #0
		ALU2:	MAD  r5.l, r3.l, r5.l, #0
		ALU3:	MAD  r5.h, r3.h, r5.h, #0

	// tmp =  src.bgra * mask.bgra
	ALU:
		ALU0:	MAD  lp.lh, u0.l, r0.l, #0
		ALU1:	MAD  lp.lh, u0.h, r0.h, #0
		ALU2:	MAD  lp.lh, u1.l, r1.l, #0
		ALU3:	MAD  lp.lh, u1.h, r1.h, u8.l-1 (sat)

	// r0,r1 = (1 - src.aaaa * mask.bgra) * dst.bgra + src.bgra * mask.bgra * dst.aaaa
	ALU:
		ALU0:	MAD  r0.l, r3.h, alu0, r4.l (sat)
		ALU1:	MAD  r0.h, r3.h, alu1, r4.h (sat)
		ALU2:	MAD  r1.l, r3.h, alu2, r5.l (sat)
		ALU3:	MAD  r1.h, r3.h, alu3, r5.h (sat)

	DW:	store rt1, r0, r1
;

EXEC
;
