#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.

AM_CFLAGS = $(XORG_CFLAGS) $(DRM_CFLAGS) $(UDEV_CFLAGS) $(LZ4_CFLAGS) \
	$(CWARNFLAGS) $(JPEG_CFLAGS) $(PNG_CFLAGS)

opentegra_drv_la_LTLIBRARIES = opentegra_drv.la
opentegra_drv_la_LDFLAGS = -module -avoid-version
opentegra_drv_la_LIBADD = @UDEV_LIBS@ @DRM_LIBS@ @LZ4_LIBS@ @JPEG_LIBS@ \
			@PNG_LIBS@
opentegra_drv_ladir = @moduledir@/drivers

# enable fp16 for the 3d attributes
opentegra_drv_la_CFLAGS = $(AM_CFLAGS) -mfp16-format=ieee

opentegra_drv_la_SOURCES = \
	compat-api.h \
	common_helpers.c \
	common_helpers.h \
	dri2.c \
	driver.c \
	driver.h \
	drm_plane.c \
	drmmode_display.c \
	drmmode_display.h \
	host1x.h \
	xv.c \
	xv.h \
	exa.c \
	exa_mm.c \
	exa_mm_pool.c \
	exa_mm_fridge.c \
	exa.h \
	vblank.c \
	vblank.h \
	tegra_stream.c \
	tegra_stream.h \
	shaders/prog.h \
	$(shaders_gen) \
	tgr_3d.xml.h \
	shaders.h \
	gr3d.c \
	gr3d.h \
	pool_alloc.c

shaders_dir := $(filter %/, $(wildcard $(srcdir)/shaders/*/))
shaders_gen := $(addsuffix .bin.h, $(shaders_dir:%/=%))

%.bin.h: gen_shader_bin \
			%/vertex.asm \
			%/linker.asm \
			%/fragment.asm
	$(builddir)/gen_shader_bin \
		--vs $*/vertex.asm \
		--lnk $*/linker.asm \
		--fs $*/fragment.asm \
		--name $(*F) \
		--out $@

asm_grammars := $(wildcard $(srcdir)/asm/*.y)
asm_headers  := $(wildcard $(srcdir)/asm/*.h)
asm_lexers   := $(wildcard $(srcdir)/asm/*.l)

asm_gen_c := \
	$(asm_lexers:%.l=%.lex.c) \
	$(asm_grammars:%.y=%.tab.c)

asm_gen_h := \
	$(asm_grammars:%.y=%.tab.h)

%.lex.c: $(asm_lexers)
	$(LEX) -P $(*F) --nounput -o $@ $*.l

%.tab.c %.tab.h: $(asm_grammars)
	$(YACC) -p $(*F) -b $(*F) -d --debug -o $@ $*.y

HOSTCC = gcc

gen_shader_bin: gen_shader_bin.c $(asm_gen_c) $(asm_headers)
	$(HOSTCC) -I $(srcdir)/asm -o $(builddir)/$@ $< $(asm_gen_c)

BUILT_SOURCES = \
	$(asm_gen_c) \
	$(asm_gen_h) \
	$(shaders_gen)

CLEANFILES = \
	$(asm_gen_c) \
	$(asm_gen_h) \
	$(shaders_gen) \
	$(builddir)/gen_shader_bin
