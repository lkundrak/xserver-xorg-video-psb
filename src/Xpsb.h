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
 * Authors:
 *   Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifndef _XPSB_H_
#define _XPSB_H_

#include "xf86drm.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif
#include "compiler.h"

#define XPSB_VOFFSET_X 0
#define XPSB_VOFFSET_Y 1
#define XPSB_VOFFSET_UT0 2
#define XPSB_VOFFSET_VT0 3
#define XPSB_VOFFSET_UT1 4
#define XPSB_VOFFSET_VT1 5
#define XPSB_VOFFSET_UT2 6
#define XPSB_VOFFSET_VT2 7
#define XPSB_VOFFSET_COUNT 8

typedef enum
{ Xpsb_nearest = 0, Xpsb_linear }
XpsbFilterFormats;
typedef enum
{ Xpsb_repeat = 0, Xpsb_clamp, Xpsb_clampGL }
XpsbAddrModes;

typedef struct _XpsbSurface
{
    drmBO *buffer;
    unsigned int offset;
    unsigned int pictFormat;
    unsigned int w;
    unsigned int h;
    unsigned int stride;

    /*
     * For textures only.
     */

    XpsbFilterFormats minFilter;
    XpsbFilterFormats magFilter;
    XpsbAddrModes uMode;
    XpsbAddrModes vMode;
    unsigned int texCoordIndex;
    Bool isYUVPacked;
    unsigned int packedYUVId;

    /*
     *  For dest surf only.
     */

    unsigned int x;
    unsigned int y;
} XpsbSurface, *XpsbSurfacePtr;

extern int psbBlitYUV(ScrnInfoPtr pScrn, XpsbSurfacePtr dst,
		      XpsbSurfacePtr backTextures[], int numBackTextures,
		      Bool isPlanar, unsigned int planarID, float texCoord0[],
		      float texCoord1[], float texCoord2[], int numCoord,
		      float conversion_data[]);
/* FIXME: better to find a unified place to define this macro PSB_DETEAR,
   rather than have 2 in here and psb_drm.h */
#define PSB_DETEAR 		
#ifdef PSB_DETEAR
extern int XpsbCmdCancelBlit(ScrnInfoPtr pScrn);

extern int psbBlitYUVDetear(ScrnInfoPtr pScrn, XpsbSurfacePtr dst,
		      XpsbSurfacePtr backTextures[], int numBackTextures,
		      Bool isPlanar, unsigned int planarID, float texCoord0[],
		      float texCoord1[], float texCoord2[], int numCoord,
		      float conversion_data[]);
#endif	/* PSB_DETEAR */

extern int
psb3DPrepareComposite(ScrnInfoPtr pScrn,
		      XpsbSurfacePtr dst,
		      XpsbSurfacePtr opTextures[], int numOpTextures,
		      int compOp, unsigned int scalar, Bool scalarSrc,
		      Bool scalarMask);
extern void psb3DCompositeQuad(ScrnInfoPtr pScrn, float vertices[]);
extern int psb3DCompositeFinish(ScrnInfoPtr pScrn);
extern void XpsbTakeDown(ScrnInfoPtr pScrn);
extern Bool XpsbInit(ScrnInfoPtr pScrn, CARD8 * map, int drmFD);

#endif
