/**************************************************************************
 *
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
 */

#ifndef _PSB_BUFFERS_H_
#define _PSB_BUFFERS_H_

#include "libmm/mm_defines.h"
#include "libmm/mm_interface.h"

typedef struct _PsbBufList
{
    MMListHead head;
    struct _MMBuffer *buf;
    Bool validated;
} PsbBufListRec, *PsbBufListPtr;

typedef struct _PsbScanoutRec
{
    PsbBufListRec entry;
    unsigned long offset;
    unsigned stride;
    unsigned height;
    unsigned width;
    unsigned cpp;
    unsigned long size;
    unsigned depth;
    int rotation;
    void *virtual;
#ifdef XF86DRI
    ScrnInfoPtr pScrn;
    MMListHead sAreaList;
#endif
} PsbScanoutRec, *PsbScanoutPtr;

/*
 * psb_buffers.c
 */

static inline struct _MMBuffer *
psbScanoutBuf(PsbScanoutPtr psbScanout)
{
    return psbScanout->entry.buf;
}

static inline unsigned
psbScanoutCpp(PsbScanoutPtr psbScanout)
{
    return psbScanout->cpp;
}

static inline unsigned long
psbScanoutOffset(PsbScanoutPtr psbScanout)
{
    return psbScanout->offset;
}
static inline unsigned long
psbScanoutStride(PsbScanoutPtr psbScanout)
{
    return psbScanout->stride;
}
static inline void *
psbScanoutVirtual(PsbScanoutPtr psbScanout)
{
    return psbScanout->virtual;
}

static inline unsigned
psbScanoutWidth(PsbScanoutPtr psbScanout)
{
    return psbScanout->width;
}

static inline unsigned
psbScanoutDepth(PsbScanoutPtr psbScanout)
{
    return psbScanout->depth;
}

static inline unsigned
psbScanoutHeight(PsbScanoutPtr psbScanout)
{
    return psbScanout->height;
}

static inline int
psbScanoutRotation(PsbScanoutPtr psbScanout)
{
    return psbScanout->rotation;
}

static inline void
psbClearBufItem(PsbBufListPtr b)
{
    if (!b)
	return;

    mmListDelInit(&b->head);
    if (b->buf) {
	mmBufDestroy(b->buf);
	b->buf = NULL;
    }
    b->validated = FALSE;
}

static inline void
psbAddBufItem(MMListHead * list, PsbBufListPtr b, struct _MMBuffer *buf)
{

    if (!buf)
	return;
    b->buf = buf;
    b->validated = FALSE;
    buf->man->mapBuf(buf, MM_FLAG_READ | MM_FLAG_WRITE, 0);
    buf->man->unMapBuf(buf);
    mmListAddTail(&b->head, list);
}

extern PsbScanoutPtr
psbScanoutCreate(ScrnInfoPtr pScrn, unsigned cpp, unsigned depth,
		 unsigned width, unsigned height,
		 unsigned flags, Bool front, unsigned rotation);

extern void *psbScanoutMap(PsbScanoutPtr scanout);
void psbScanoutUnMap(PsbScanoutPtr scanout);
extern void psbScanoutDestroy(PsbScanoutPtr scanout);

extern PsbBufListPtr psbInBuffer(MMListHead * head, void *ptr);
#endif
