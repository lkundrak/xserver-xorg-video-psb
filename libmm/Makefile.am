AM_CFLAGS = @XORG_CFLAGS@ @DRI_CFLAGS@
libmm_la_LTLIBRARIES = libmm.la
libmm_la_LDFLAGS = -avoid-version
libmm_ladir = @moduledir@/drivers

libmm_la_SOURCES = \
	mm_core.c \
	mm_user.c \
	mm_buflist.c \
	mm_defines.h \
	mm_interface.h 

if DRI
libmm_la_SOURCES += \
	mm_drm.c 
endif
