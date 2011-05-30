/**************************************************************************
 * Copyright (c) Intel Corp. 2007.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * psb_ioctl.h - Poulsbo device-specific IOCTL interface.
 */

#ifndef _PSB_IOCTL_H_
#define _PSB_IOCTL_H_

typedef struct _drmBOList
{
    unsigned numTarget;
    unsigned numCurrent;
    unsigned numOnList;
    drmMMListHead list;
    drmMMListHead free;
} drmBOList;

struct _Psb2DBuffer;

typedef void (PsbVolatileStateFunc) (struct _Psb2DBuffer *, void *);

/*
 * 2D command buffer. A drm buffer object that lives in system memory only.
 */

typedef struct _Psb2DBuffer
{
    int fd;
    drmBO buffer;
    drmBOList bufferList;

    unsigned *startCmd;
    unsigned *curCmd;
    int myValidateIndex;

    struct drm_psb_reloc *startReloc;
    struct drm_psb_reloc *curReloc;
    unsigned maxRelocs;
    drm_clip_rect_t *clipRects;
    PsbVolatileStateFunc *emitVolatileState;
    void *volatileStateArg;
} Psb2DBufferRec, *Psb2DBufferPtr;

#define PSB_SUPER_2D_VARS(_cb)		        \
    Psb2DBufferPtr cb = (_cb);			\
    int __ret2D = 0
#define PSB_SUPER_2D_OUT(_data)			\
    {						\
	*(cb)->curCmd++ = (_data);		\
    }
#define PSB_SUPER_2D_RELOC_OFFSET(_pre_add, _buf, _flags, _mask)		\
  {									\
	if ((__ret2D = psbRelocOffset2D(cb, _pre_add, _buf, _flags, _mask))) \
	    goto __out2D;						\
    }

#define PSB_SUPER_2D_DONE(_ret) \
  __out2D:{		  \
    (_ret) = __ret2D;	  \
  }

#define PSB_SUPER_2D_SIZE(_dwords, _offset, _flags, _mask) do{		\
	if (((unsigned long) (cb->curCmd + (_dwords)) >=		\
	     (unsigned long) cb->startReloc) ||				\
	    (cb->curReloc - cb->startReloc + (_offset) >= cb->maxRelocs)) { \
	    if ((__ret2D = psbFlush2D(cb, DRM_FENCE_FLAG_NO_USER, NULL))) \
		goto __out2D;						\
	    cb->emitVolatileState(cb, cb->volatileStateArg);		\
	}								\
    } while (0);

extern int psbFlush2D(Psb2DBufferPtr buf, unsigned fence_flags,
		      unsigned *fence_handle);
extern int psbRelocOffset2D(Psb2DBufferPtr buf, unsigned delta,
			    drmBO * buffer, uint64_t flags, uint64_t mask);
extern Bool psbInit2DBuffer(int fd, Psb2DBufferPtr buf);
extern void psbTakedown2DBuffer(int fd, Psb2DBufferPtr buf);
extern void psbSetStateCallback(Psb2DBufferPtr buf, PsbVolatileStateFunc *func,
				void *arg);

#endif
