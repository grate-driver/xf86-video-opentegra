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

pseq_to_dw_exec_nb = 2	// the number of 'EXEC' block where DW happens
alu_buffer_size = 1	// number of .rgba regs carried through pipeline

.uniforms
	[2].l = "mask_color.r";
	[2].h = "mask_color.g";
	[3].l = "mask_color.b";
	[3].h = "mask_color.a";

	[8].l = "dst_fmt_alpha";

.asm

EXEC
	// fetch dst pixel to r2,r3
	PSEQ:	0x0081000A

	// dst = dst.bgra * mask.bgra
	ALU:
		ALU0:	MAD  r0.l, r2.l, u2.l, #0
		ALU1:	MAD  r0.h, r2.h, u2.h, #0
		ALU2:	MAD  r1.l, r3.l, u3.l, #0
		ALU3:	MAD  r1.h, r3.h, u3.h, #0
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
