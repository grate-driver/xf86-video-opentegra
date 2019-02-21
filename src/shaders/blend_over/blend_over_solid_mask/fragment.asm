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

pseq_to_dw_exec_nb = 5	// the number of 'EXEC' block where DW happens
alu_buffer_size = 1	// number of .rgba regs carried through pipeline

.uniforms
	[2].l = "mask_color.r";
	[2].h = "mask_color.g";
	[3].l = "mask_color.b";
	[3].h = "mask_color.a";

	[5].l = "src_fmt_alpha";
	[5].h = "src_swap_bgr";

	[8].l = "dst_fmt_alpha";
	[8].h = "src_clamp_to_border";

.asm

EXEC
	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fp20, t0.fp20, NOP, NOP

	// sample tex0 (src)
	TEX:	tex r2, r3, tex0, r0, r1, r2

	ALU:
		// src = src_fmt_alpha ? src.a : 1.0
		ALU0:	CSEL r3.h, -u5.l, r3.h, #1

		// swap src ABGR to ARGB if needed
		ALU1:	CSEL r2.l, -u5.h, r3.l, r2.l
		ALU2:	CSEL r3.l, -u5.h, r2.l, r3.l
;

EXEC
	// Emulate clamp-to-border for src
	ALU:
		ALU0:	MAD  lp.lh, r0, #1, -#1
		ALU1:	MAD  lp.lh, r1, #1, -#1

	ALU:
		ALU0:	CSEL lp.lh,   r0, u8.h, #0 (this)
		ALU1:	CSEL lp.lh, alu0, #0, u8.h (other)
		ALU2:	CSEL lp.lh,   r1, u8.h, #0 (other)
		ALU3:	CSEL lp.lh, alu1, #0, u8.h

	ALU:
		ALU0:	MAD  r0.l, r2.l, #1, -alu0 (sat)
		ALU1:	MAD  r0.h, r2.h, #1, -alu0 (sat)
		ALU2:	MAD  r1.l, r3.l, #1, -alu0 (sat)
		ALU3:	MAD  r1.h, r3.h, #1, -alu0 (sat)
;

EXEC
	ALU:
		ALU0:	MAD  lp.lh, r0.l, u2.l, #0
		ALU1:	MAD  lp.lh, r0.h, u2.h, #0
		ALU2:	MAD  lp.lh, r1.l, u3.l, #0
		ALU3:	MAD  lp.lh, r1.h, u3.h, r1.h

	ALU:
		ALU0:	MAD  lp.lh, alu0, #1, #0 (this)
		ALU1:	MAD  lp.lh, alu1, #1, #0 (other)
		ALU2:	MAD  lp.lh, alu2, #1, #0 (other)
		ALU3:	MAD  lp.lh, alu3, #1, #0

	// kill the pixel if src.bgra * mask.bgra == 0 && src.a == 0
	ALU:
		ALU0:	CSEL kill, -alu0, #0, #1
;

EXEC
	// fetch dst pixel to r2,r3
	PSEQ:	0x0081000A

	// tmp = -src.aaaa * mask.bgra + 1
	ALU:
		ALU0:	MAD  lp.lh, -u2.l, r1.h, #1
		ALU1:	MAD  lp.lh, -u2.h, r1.h, #1
		ALU2:	MAD  lp.lh, -u3.l, r1.h, #1
		ALU3:	MAD  lp.lh, -u3.h, r1.h, #1

	// tmp = tmp * dst.bgra
	ALU:
		ALU0:	MAD  lp.lh, alu0, r2.l, #0
		ALU1:	MAD  lp.lh, alu1, r2.h, #0
		ALU2:	MAD  lp.lh, alu2, r3.l, #0
		ALU3:	MAD  lp.lh, alu3, r3.h, #0

	// r0,r1 = src.bgra * mask.bgra + tmp
	ALU:
		ALU0:	MAD  r0.l, u2.l, r0.l, alu0 (sat)
		ALU1:	MAD  r0.h, u2.h, r0.h, alu1 (sat)
		ALU2:	MAD  r1.l, u3.l, r1.l, alu2 (sat)
		ALU3:	MAD  r1.h, u3.h, r1.h, alu3 (sat)
;

EXEC
	ALU:
		ALU0:	MAD  lp.lh, r0.l, #1, -r2.l
		ALU1:	MAD  lp.lh, r0.h, #1, -r2.h
		ALU2:	MAD  lp.lh, r1.l, #1, -r3.l
		ALU3:	MAD  lp.lh, r1.h, #1, -r3.h

	ALU:
		ALU0:	MAD  lp.lh, abs(alu0), #1, #0 (this)
		ALU1:	MAD  lp.lh, abs(alu1), #1, #0 (other)
		ALU2:	MAD  lp.lh, abs(alu2), #1, #0 (other)
		ALU3:	MAD  lp.lh, abs(alu3), u8.l, #0

	// kill the pixel if dst is unchanged
	ALU:
		ALU0:	CSEL kill, -alu0, #0, #1
		ALU1:	CSEL r1.h, -u8.l, r1.h, #0

	DW:	store rt1, r0, r1
;
