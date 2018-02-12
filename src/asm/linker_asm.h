/*
 * Copyright (c) Dmitry Osipenko
 * Copyright (c) Erik Faye-Lund
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LINKER_ASM_H
#define LINKER_ASM_H

#include <stdint.h>

#define LINK_SWIZZLE_X		0
#define LINK_SWIZZLE_Y		1
#define LINK_SWIZZLE_Z		2
#define LINK_SWIZZLE_W		3

#define TRAM_DST_NONE		0
#define TRAM_DST_FX10_LOW	1
#define TRAM_DST_FX10_HIGH	2
#define TRAM_DST_FP20		3

typedef union shader_linking_instruction {
	struct __attribute__((packed)) {
		unsigned vec4_select:1;
		unsigned __pad1:2;
		unsigned vertex_export_index:4;
		unsigned __pad2:2;
		unsigned tram_row_index:6;
		unsigned __pad3:1;
		unsigned const_x_across_width:1;
		unsigned const_x_across_length:1;
		unsigned x_across_point:2;
		unsigned const_y_across_width:1;
		unsigned const_y_across_length:1;
		unsigned y_across_point:2;
		unsigned const_z_across_width:1;
		unsigned const_z_across_length:1;
		unsigned z_across_point:2;
		unsigned const_w_across_width:1;
		unsigned const_w_across_length:1;
		unsigned w_across_point:2;

		unsigned tram_dst_swizzle_x:2;
		unsigned tram_dst_type_x:2;
		unsigned tram_dst_swizzle_y:2;
		unsigned tram_dst_type_y:2;
		unsigned tram_dst_swizzle_z:2;
		unsigned tram_dst_type_z:2;
		unsigned tram_dst_swizzle_w:2;
		unsigned tram_dst_type_w:2;
		unsigned interpolation_disable_x:1;
		unsigned interpolation_disable_y:1;
		unsigned interpolation_disable_z:1;
		unsigned interpolation_disable_w:1;
	};

	struct __attribute__((packed)) {
		uint32_t first;
		uint32_t latter;
	};

	uint64_t data;
} link_instr;

#endif // LINKER_ASM_H
