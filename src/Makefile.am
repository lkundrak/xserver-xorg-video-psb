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
AM_CFLAGS = @XORG_CFLAGS@ @DRI_CFLAGS@ -Wall -I$(top_srcdir)
psb_drv_la_LTLIBRARIES = psb_drv.la
psb_drv_la_LDFLAGS = -module -avoid-version
psb_drv_la_LIBADD = ../libmm/libmm.la
psb_drv_ladir = @moduledir@/drivers


psb_drv_la_SOURCES = \
        psb_accel.c \
        psb_accel.h \
	psb_buffers.c \
	psb_buffers.h \
	psb_dri.h \
	psb_driver.c \
	psb_driver.h \
	psb_lvds.c \
	psb_lvds.h \
	psb_sdvo.c \
	psb_overlay.c \
	psb_overlay.h \
	psb_shadow.c \
	psb_outputs.c \
	psb_crtc.c \
	psb_cursor.c \
	psb_dga.c \
	i810_reg.h \
	i830.h \
	i830_i2c.c \
	i830_bios.c \
	i830_bios.h \
	i830_sdvo_regs.h

if DRI
psb_drv_la_SOURCES += \
         psb_dri.c \
         psb_dri.h \
	 psb_ioctl.c \
	 psb_ioctl.h \
	 psb_video.c \
         psb_composite.c \
	 Xpsb.h
endif
