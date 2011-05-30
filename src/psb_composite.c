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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>
#include <picturestr.h>
#include <psb_reg.h>
#include "psb_accel.h"
#include "psb_driver.h"

static Bool psbSupportDstA0[] =
    { TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE,
    FALSE, FALSE, FALSE, TRUE, FALSE
};

static Bool
psbIsPot(unsigned int val)
{
    unsigned int test = 0x1;

    if (!val)
	return FALSE;

    while ((val & test) == 0)
	test <<= 1;

    return val == test;
}

static Bool
psbExaSrfInfo(ScrnInfoPtr pScrn, XpsbSurfacePtr srf,
	      PixmapPtr pPix, PicturePtr pPict)
{
    DrawablePtr pDraw;
    struct _MMBuffer *mmBuffer;
    unsigned long offset;
    unsigned int needed_pitch;

    pDraw = (pPict->repeat) ? pPict->pDrawable : &pPix->drawable;
    srf->w = pDraw->width;
    srf->h = pDraw->height;
    srf->stride = exaGetPixmapPitch(pPix);
    if (!psbExaGetSuperOffset(pPix, &offset, &mmBuffer))
	return FALSE;

    if (!pPict->repeat) {

	needed_pitch =
	    ALIGN_TO(pDraw->width, 32) * (pDraw->bitsPerPixel >> 3);

	if (srf->stride != needed_pitch) {
	    unsigned int tmp = srf->stride / (pDraw->bitsPerPixel >> 3);

	    if ((tmp & 31) == 0)
		srf->w = tmp;
	}
	srf->offset = offset;

    } else {

	/*
	 * Poulsbo can only do repeat on power-of-two textures.
	 */

	needed_pitch =
	    ALIGN_TO(pDraw->width, 32) * (pDraw->bitsPerPixel >> 3);
	if (srf->stride != needed_pitch || !psbIsPot(pDraw->width)
	    || !psbIsPot(pDraw->height))
	    return FALSE;

	/*
	 * FIXME: Need to check whether we should add the drawable start offset from
	 * the pixmap base here...
	 */

	srf->offset = offset;

    }
    srf->buffer = mmKernelBuf(mmBuffer);
    srf->x = 0;
    srf->y = 0;
    srf->pictFormat = pPict->format;
    srf->minFilter = Xpsb_nearest;
    srf->magFilter = Xpsb_nearest;
    srf->uMode = (pPict->repeat) ? Xpsb_repeat : Xpsb_clamp;
    srf->vMode = srf->uMode;

    return TRUE;
}

static inline Bool
psbIsScalar(PicturePtr pPict)
{
    return (pPict->repeat && pPict->pDrawable->width == 1 &&
	    pPict->pDrawable->height == 1 &&
	    psbExpandablePixel(pPict->format));
}

Bool
psbExaPrepareComposite3D(int op, PicturePtr pSrcPicture,
			 PicturePtr pMaskPicture, PicturePtr pDstPicture,
			 PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
    ScreenPtr pScreen = pDst->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbExaPtr pPsbExa = pPsb->pPsbExa;
    XpsbSurfacePtr opTex[2];
    CARD32 scalarPixel;
    unsigned int numTex = 0;

    if (PICT_FORMAT_A(pDstPicture->format) == 0 && !psbSupportDstA0[op])
	return FALSE;

    pPsbExa->scalarSrc = psbIsScalar(pSrcPicture);
    pPsbExa->scalarMask = (pMaskPicture == NULL) ||
	PICT_FORMAT_A(pMaskPicture->format) == 0 || psbIsScalar(pMaskPicture);

    if (pPsbExa->scalarSrc && pPsbExa->scalarMask) {

	/*
	 * Ouch. EXA should've optimized this away.
	 */

	pPsbExa->scalarSrc = FALSE;
    }

    if ((pPsbExa->scalarSrc || pPsbExa->scalarMask) &&
	!(pPsbExa->scalarSrc && pPsbExa->scalarMask)) {

	/*
	 * Get the scalar pixel value.
	 */

	if (pPsbExa->scalarSrc)
	    psbPixelARGB8888(pSrcPicture->format, pSrc->devPrivate.ptr,
			     &scalarPixel);
	else if (pMaskPicture != NULL &&
		 PICT_FORMAT_A(pMaskPicture->format) > 0)
	    psbPixelARGB8888(pMaskPicture->format, pMask->devPrivate.ptr,
			     &scalarPixel);
	else
	    scalarPixel = 0xFF000000;
    }

    if (!pPsbExa->scalarSrc) {
	if (!psbExaSrfInfo(pScrn, &pPsbExa->src, pSrc, pSrcPicture))
	    return FALSE;

	opTex[numTex] = &pPsbExa->src;
	pPsbExa->src.texCoordIndex = numTex++;
    }
    if (!pPsbExa->scalarMask) {
	if (!psbExaSrfInfo(pScrn, &pPsbExa->mask, pMask, pMaskPicture))
	    return FALSE;

	opTex[numTex] = &pPsbExa->mask;
	pPsbExa->mask.texCoordIndex = numTex++;
    }
    if (!psbExaSrfInfo(pScrn, &pPsbExa->dst, pDst, pDstPicture)) {
	return FALSE;
    }

    pPsbExa->dst.texCoordIndex = 0;

    if (!psb3DPrepareComposite(pScrn, &pPsbExa->dst,
			       opTex, numTex, op,
			       (unsigned int)scalarPixel,
			       pPsbExa->scalarSrc, pPsbExa->scalarMask))
	return FALSE;

    return TRUE;
}

void
psbExaComposite3D(PixmapPtr pDst, int srcX, int srcY, int maskX, int maskY,
		  int dstX, int dstY, int width, int height)
{
    ScreenPtr pScreen = pDst->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbExaPtr pPsbExa = pPsb->pPsbExa;
    float vertices[4 * XPSB_VOFFSET_COUNT];
    XpsbSurface *src = &pPsbExa->src;
    XpsbSurface *mask = &pPsbExa->mask;
    Bool scalar = pPsbExa->scalarSrc || pPsbExa->scalarMask;
    unsigned int used_coords = ((scalar) ? 4 : 6);

    int coord;

    float *vertex0 = &vertices[0 * used_coords];
    float *vertex3 = &vertices[3 * used_coords];
    float *vertex;

    /*
     * Top left corner.
     */

    coord = XPSB_VOFFSET_UT0;
    vertex0[XPSB_VOFFSET_X] = dstX;
    vertex0[XPSB_VOFFSET_Y] = dstY;
    if (!pPsbExa->scalarSrc) {
	vertex0[coord++] = (float)srcX / (float)src->w;
	vertex0[coord++] = (float)srcY / (float)src->h;
    }
    if (!pPsbExa->scalarMask) {
	vertex0[coord++] = (float)maskX / (float)mask->w;
	vertex0[coord++] = (float)maskY / (float)mask->h;
    }

    /*
     * Bottom right.
     */

    coord = XPSB_VOFFSET_UT0;
    vertex3[XPSB_VOFFSET_X] = dstX + width;
    vertex3[XPSB_VOFFSET_Y] = dstY + height;
    if (!pPsbExa->scalarSrc) {
	vertex3[coord++] = (float)(srcX + width) / (float)src->w;
	vertex3[coord++] = (float)(srcY + height) / (float)src->h;
    }
    if (!pPsbExa->scalarMask) {
	vertex3[coord++] = (float)(maskX + width) / (float)mask->w;
	vertex3[coord++] = (float)(maskY + height) / (float)mask->h;
    }

    /*
     * Top right.
     */

    vertex = &vertices[1 * used_coords];
    vertex[XPSB_VOFFSET_X] = vertex3[XPSB_VOFFSET_X];
    vertex[XPSB_VOFFSET_Y] = vertex0[XPSB_VOFFSET_Y];
    vertex[XPSB_VOFFSET_UT0] = vertex3[XPSB_VOFFSET_UT0];
    vertex[XPSB_VOFFSET_VT0] = vertex0[XPSB_VOFFSET_VT0];
    if (!scalar) {
	vertex[XPSB_VOFFSET_UT1] = vertex3[XPSB_VOFFSET_UT1];
	vertex[XPSB_VOFFSET_VT1] = vertex0[XPSB_VOFFSET_VT1];
    }

    /*
     * Bottom left.
     */

    vertex = &vertices[2 * used_coords];
    vertex[XPSB_VOFFSET_X] = vertex0[XPSB_VOFFSET_X];
    vertex[XPSB_VOFFSET_Y] = vertex3[XPSB_VOFFSET_Y];
    vertex[XPSB_VOFFSET_UT0] = vertex0[XPSB_VOFFSET_UT0];
    vertex[XPSB_VOFFSET_VT0] = vertex3[XPSB_VOFFSET_VT0];
    if (!scalar) {
	vertex[XPSB_VOFFSET_UT1] = vertex0[XPSB_VOFFSET_UT1];
	vertex[XPSB_VOFFSET_VT1] = vertex3[XPSB_VOFFSET_VT1];
    }

    psb3DCompositeQuad(pScrn, vertices);
}

void
psbExaDoneComposite3D(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];

    psb3DCompositeFinish(pScrn);
}
