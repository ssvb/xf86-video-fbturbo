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
AM_CFLAGS = @XORG_CFLAGS@
fbturbo_drv_la_LTLIBRARIES = fbturbo_drv.la
fbturbo_drv_la_LDFLAGS = -module -avoid-version
fbturbo_drv_ladir = @moduledir@/drivers

fbturbo_drv_la_SOURCES = \
         compat-api.h \
         uthash.h \
         arm_asm.S \
         cpuinfo.c \
         cpuinfo.h \
         cpu_backend.c \
         cpu_backend.h \
         backing_store_tuner.c \
         backing_store_tuner.h \
         interfaces.h \
         fbdev.c \
         fbdev_priv.h \
         sunxi_disp.c \
         sunxi_disp.h \
         sunxi_x_g2d.c \
         sunxi_x_g2d.h \
         sunxi_disp_hwcursor.c \
         sunxi_disp_hwcursor.h \
         sunxi_video.c \
         sunxi_video.h

if HAVE_LIBUMP
fbturbo_drv_la_SOURCES += \
         sunxi_mali_ump_dri2.c \
         sunxi_mali_ump_dri2.h
endif
