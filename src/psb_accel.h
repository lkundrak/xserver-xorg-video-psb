/**************************************************************************
 *
 * Copyright 2006 Thomas Hellstrom.
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

#ifndef _PSB_ACCEL_H_
#define _PSB_ACCEL_H_

#include <xf86.h>
#include "exa.h"
#include "psb_buffers.h"
#include "Xpsb.h"

struct _PsbDevice;

typedef struct _PsbTwodContext
{
    CARD32 sMode;
    CARD32 dMode;
    CARD32 mMode;
    CARD32 fixPat;
    CARD32 cmd;
    struct _MMBuffer *sBuffer;
    CARD32 sStride;
    CARD32 sOffset;
    struct _MMBuffer *dBuffer;
    CARD32 dStride;
    CARD32 dOffset;
    int sBPP;
    CARD32 direction;
    CARD32 srcRot;
    PictTransformPtr srcTransform;
    int srcWidth;
    int srcHeight;
    Bool twoPassComp;
    Bool comp2D;
    Bool srcState;
    Bool dstState;
    Bool bhFence;
} PsbTwodContextRec, *PsbTwodContextPtr;

typedef struct _PsbExa
{
    PsbBufListRec tmpBuf;
    PsbBufListRec scratchBuf;
    PsbBufListRec exaBuf;
    ExaDriverPtr pExa;
    Bool exaUp;

    /*
     * Composite stuff.
     */

    XpsbSurface dst;
    XpsbSurface src;
    XpsbSurface mask;
    Bool scalarSrc;
    Bool scalarMask;
} PsbExaRec, *PsbExaPtr;

#define PSB_FLUSH_CHUNK 0x40
#define PSB_2D_BUFFER_SIZE 1024

#define PSB_2D_SPACE(_space)			     \
    if ((cb->dWords - (cb->cur - cb->buf)) < _space) \
	psbFlushTwodBuffer(cb)

#define PSB_2D_OUT(_arg)			\
    *cb->cur++ = (_arg)
#define PSB_2D_DONE

extern void psbExaClose(PsbExaPtr pPsbExa, ScreenPtr pScreen);
extern PsbExaPtr psbExaInit(ScrnInfoPtr pScrn);
extern void psbPixelARGB8888(unsigned format, void *pixelP,
			     CARD32 * argb8888);
extern Bool psbExpandablePixel(int format);
extern unsigned long long psbTexOffsetStart(PixmapPtr pPix);
#endif
