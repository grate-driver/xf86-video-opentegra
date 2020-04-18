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
#
# Process this file with autoconf to produce a configure script

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xf86-video-opentegra],
	[0.6.0],
	[https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
	[xf86-video-opentegra])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(.)
AC_SYS_LARGEFILE

# Initialize Automake
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-xz])

# Initialize libtool
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Initialize compiler compiler
AC_PROG_LEX
if test "$LEX" = :; then
	AC_MSG_ERROR([flex not found])
fi

AC_CHECK_PROGS(YACC, 'bison -y' byacc yacc, :)
if test "$YACC" = :; then
	AC_MSG_ERROR([bison not found])
fi

# Initialize X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir,
	    AS_HELP_STRING([--with-xorg-module-dir=DIR],
			   [Default xorg module directory [[default=$libdir/xorg/modules]]]),
	    [moduledir="$withval"],
	    [moduledir="$libdir/xorg/modules"])

# check for X.Org server
PKG_CHECK_MODULES(XORG, [xorg-server xproto fontsproto $REQUIRED_MODULES])
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

# check for extensions
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(XF86DRI, xextproto x11)
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

PKG_CHECK_MODULES(XEXT, [xextproto >= 7.0.99.1],
		  HAVE_XEXTPROTO_71="yes"; AC_DEFINE(HAVE_XEXTPROTO_71, 1, [xextproto 7.1 available]),
		  HAVE_XEXTPROTO_71="no")
AM_CONDITIONAL(HAVE_XEXTPROTO_71, [ test "$HAVE_XEXTPROTO_71" = "yes" ])

AC_HEADER_STDC

PKG_CHECK_MODULES(DRM, [libdrm >= 2.4.81])
AM_CONDITIONAL(DRM, test "x$DRM" = xyes)

PKG_CHECK_MODULES(UDEV, [libudev], [udev=yes], [udev=no])
if test x"$udev" = xyes; then
	AC_DEFINE(HAVE_UDEV,1,[Enable udev-based monitor hotplug detection])
fi

PKG_CHECK_MODULES(LZ4, liblz4,
		  HAVE_LZ4="yes"; AC_DEFINE(HAVE_LZ4, 1, [LZ4 available]),
		  HAVE_LZ4="no")
AM_CONDITIONAL(HAVE_LZ4, [ test "$HAVE_LZ4" = "yes" ])

PKG_CHECK_MODULES(JPEG, [libturbojpeg >= 1.5.0],
		  HAVE_JPEG="yes"; AC_DEFINE(HAVE_JPEG, 1, [JPEG available]),
		  HAVE_JPEG="no")
AM_CONDITIONAL(HAVE_JPEG, [ test "$HAVE_JPEG" = "yes" ])

PKG_CHECK_MODULES(PNG, [libpng >= 1.6.0],
		  HAVE_PNG="yes"; AC_DEFINE(HAVE_PNG, 1, [PNG available]),
		  HAVE_PNG="no")
AM_CONDITIONAL(HAVE_PNG, [ test "$HAVE_PNG" = "yes" ])

SAVE_CFLAGS=$CFLAGS
SAVE_LIBS=$LIBS
CFLAGS=$DRM_CFLAGS
LIBS=$DRM_LIBS

AC_CHECK_FUNCS(
	[drmSyncobjCreate],
	[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_LIBDRM_SYNCOBJ_SUPPORT, [], [Does libdrm have drmSyncobjCreate])
	],
	[
		AC_MSG_RESULT([no])
	])

CFLAGS=$SAVE_CFLAGS
LIBS=$SAVE_LIBS

DRIVER_NAME=opentegra
AC_SUBST([DRIVER_NAME])
AC_SUBST([moduledir])

AC_ARG_ENABLE(valgrind,
	      [AS_HELP_STRING([--enable-valgrind],
	      [Build libdrm with  valgrind support (default: auto)])],
	      [VALGRIND=$enableval], [VALGRIND=auto])
if test "x$VALGRIND" != xno; then
	PKG_CHECK_MODULES(VALGRIND, [valgrind], [have_valgrind=yes], [have_valgrind=no])
fi
AC_MSG_CHECKING([whether to enable Valgrind support])
if test "x$VALGRIND" = xauto; then
	VALGRIND="$have_valgrind"
fi

if test "x$VALGRIND" = "xyes"; then
	if ! test "x$have_valgrind" = xyes; then
		AC_MSG_ERROR([Valgrind support required but not present])
	fi
	AC_DEFINE([HAVE_VALGRIND], 1, [Use valgrind intrinsics to suppress false warnings])
else
	AC_DEFINE([HAVE_VALGRIND], 0)
fi

AC_MSG_RESULT([$VALGRIND])

AC_ARG_ENABLE([debug],
	      [AS_HELP_STRING([--enable-debug],
	      [use debug compiler flags and macros @<:@default=disabled@:>@])],
	      [enable_debug="$enableval"],
	      [enable_debug=no]
)

if test "x$enable_debug" = xyes; then
	DEFINES="$DEFINES -DDEBUG"
	if test "x$GCC" = xyes; then
		if ! echo "$CFLAGS" | grep -q -e '-g'; then
			CFLAGS="$CFLAGS -g"
		fi
		if ! echo "$CFLAGS" | grep -q -e '-O'; then
			CFLAGS="$CFLAGS -O0"
		fi
	fi
	if test "x$GXX" = xyes; then
		if ! echo "$CXXFLAGS" | grep -q -e '-g'; then
			CXXFLAGS="$CXXFLAGS -g"
		fi
		if ! echo "$CXXFLAGS" | grep -q -e '-O'; then
			CXXFLAGS="$CXXFLAGS -O0"
		fi
	fi
else
	DEFINES="$DEFINES -DNDEBUG"
fi

AC_SUBST([DEFINES])

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	man/Makefile
])

AC_OUTPUT
