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
 * Authors:
 *   Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *   Jakob Bornecrantz <jakob-at-tungstengraphics-dot-com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <sys/time.h>
#include <picturestr.h>
#include <psb_reg.h>
#include "psb_accel.h"
#include "psb_driver.h"

#define PSB_EXA_MIN_COMPOSITE 512      /* Needs tuning */
#define PSB_EXA_MIN_COPY 256	       /* Needs tuning */
#define PSB_EXA_MIN_DOWNLOAD 256       /* Needs tuning */
#define PSB_FMT_HASH_SIZE 256
#define PSB_NUM_COMP_FORMATS 9

#undef PSB_FIX_BUG_W8
#define PSB_FIX_BUG_OVERLAP

#define PSB_FMT_HASH(arg) (((((arg) >> 1) + (arg)) >> 8) & 0xFF)

typedef struct _PsbFormat
{
    unsigned pictFormat;
    Bool dstSupported;
    Bool patSupported;
    Bool srcSupported;
    CARD32 dstFormat;
    CARD32 patFormat;
    CARD32 srcFormat;
} PsbFormatRec, *PsbFormatPointer;

static PsbFormatRec psbCompFormats[PSB_FMT_HASH_SIZE];

static const unsigned psbFormats[PSB_NUM_COMP_FORMATS][7] = {
    {PICT_a8, 0x00, PSB_2D_PAT_8_ALPHA, PSB_2D_SRC_8_ALPHA, 0, 0, 1},
    {PICT_a4, 0x00, PSB_2D_PAT_4_ALPHA, PSB_2D_SRC_4_ALPHA, 0, 0, 1},
    {PICT_r3g3b2, PSB_2D_DST_332RGB, PSB_2D_PAT_332RGB, PSB_2D_SRC_332RGB, 1,
     1, 1},
    {PICT_a4r4g4b4, PSB_2D_DST_4444ARGB, PSB_2D_PAT_4444ARGB,
     PSB_2D_SRC_4444ARGB, 1, 1, 1},
    {PICT_x1r5g5b5, PSB_2D_DST_555RGB, PSB_2D_PAT_555RGB, PSB_2D_SRC_555RGB,
     1, 1, 1},
    {PICT_a1r5g5b5, PSB_2D_DST_1555ARGB, PSB_2D_PAT_1555ARGB,
     PSB_2D_SRC_1555ARGB, 1, 1, 1},
    {PICT_r5g6b5, PSB_2D_DST_565RGB, PSB_2D_PAT_565RGB, PSB_2D_SRC_565RGB, 1,
     1, 1},
    {PICT_x8r8g8b8, PSB_2D_DST_0888ARGB, PSB_2D_PAT_0888ARGB,
     PSB_2D_SRC_0888ARGB, 1, 1, 1},
    {PICT_a8r8g8b8, PSB_2D_DST_8888ARGB, PSB_2D_PAT_8888ARGB,
     PSB_2D_SRC_8888ARGB, 1, 1, 1}
};

static const int psbCopyROP[] =
    { 0x00, 0x88, 0x44, 0xCC, 0x22, 0xAA, 0x66, 0xEE, 0x11,
    0x99, 0x55, 0xDD, 0x33, 0xBB, 0x77, 0xFF
};
static const int psbPatternROP[] =
    { 0x00, 0xA0, 0x50, 0xF0, 0x0A, 0xAA, 0x5A, 0xFA, 0x05,
    0xA5, 0x55, 0xF5, 0x0F, 0xAF, 0x5F, 0xFF
};

/*
 * Pattern as planemask.
 */

static inline int
psbCopyROP_PM(int xRop)
{
    return (psbCopyROP[xRop] & PSB_2D_ROP3_PAT) | (PSB_2D_ROP3_DST &
						   ~PSB_2D_ROP3_PAT);
}

/*
 * Source as planemask.
 */

static inline int
psbPatternROP_PM(int xRop)
{
    return (psbCopyROP[xRop] & PSB_2D_ROP3_SRC) | (PSB_2D_ROP3_DST &
						   ~PSB_2D_ROP3_SRC);
}

/*
 * Helper for bitdepth expansion.
 */

static CARD32
psbBitExpandHelper(CARD32 component, CARD32 bits)
{
    CARD32 tmp, mask;

    mask = (1 << (8 - bits)) - 1;
    tmp = component << (8 - bits);
    return ((component & 1) ? tmp | mask : tmp);
}

/*
 * Extract the components from a pixel of format "format" to an
 * argb8888 pixel. This is used to extract data from one-pixel repeat pixmaps.
 * Assumes little endian.
 */

void
psbPixelARGB8888(unsigned format, void *pixelP, CARD32 * argb8888)
{
    CARD32 bits, shift, pixel, bpp;

    if (pixelP == NULL)
	    return;

    bpp = PICT_FORMAT_BPP(format);

    if (bpp <= 8) {
	pixel = *((CARD8 *) pixelP);
    } else if (bpp <= 16) {
	pixel = *((CARD16 *) pixelP);
    } else {
	pixel = *((CARD32 *) pixelP);
    }

    switch (PICT_FORMAT_TYPE(format)) {
    case PICT_TYPE_A:
	bits = PICT_FORMAT_A(format);
	*argb8888 = psbBitExpandHelper(pixel & ((1 << bits) - 1), bits) << 24;
	return;
    case PICT_TYPE_ARGB:
	shift = 0;
	bits = PICT_FORMAT_B(format);
	*argb8888 = psbBitExpandHelper(pixel & ((1 << bits) - 1), bits);
	shift += bits;
	bits = PICT_FORMAT_G(format);
	*argb8888 |=
	    psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1),
			       bits) << 8;
	shift += bits;
	bits = PICT_FORMAT_R(format);
	*argb8888 |=
	    psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1),
			       bits) << 16;
	shift += bits;
	bits = PICT_FORMAT_A(format);
	*argb8888 |= ((bits) ?
		      psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1),
					 bits) : 0xFF) << 24;
	return;
    case PICT_TYPE_ABGR:
	shift = 0;
	bits = PICT_FORMAT_B(format);
	*argb8888 = psbBitExpandHelper(pixel & ((1 << bits) - 1), bits) << 16;
	shift += bits;
	bits = PICT_FORMAT_G(format);
	*argb8888 |=
	    psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1),
			       bits) << 8;
	shift += bits;
	bits = PICT_FORMAT_R(format);
	*argb8888 |=
	    psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1), bits);
	shift += bits;
	bits = PICT_FORMAT_A(format);
	*argb8888 |= ((bits) ?
		      psbBitExpandHelper((pixel >> shift) & ((1 << bits) - 1),
					 bits) : 0xFF) << 24;
	return;
    default:
	break;
    }
    return;
}

/*
 * Check if the above function will work.
 */

Bool
psbExpandablePixel(int format)
{
    int formatType = PICT_FORMAT_TYPE(format);

    return (formatType == PICT_TYPE_A ||
	    formatType == PICT_TYPE_ABGR || formatType == PICT_TYPE_ARGB);
}

static void
psbAccelSetMode(PsbTwodContextPtr tdc, int sdepth, int ddepth, Pixel pix)
{
    switch (sdepth) {
    case 8:
	tdc->sMode = PSB_2D_SRC_332RGB;
	break;
    case 15:
	tdc->sMode = PSB_2D_SRC_555RGB;
	break;
    case 16:
	tdc->sMode = PSB_2D_SRC_565RGB;
	break;
    case 24:
	tdc->sMode = PSB_2D_SRC_0888ARGB;
	break;
    default:
	tdc->sMode = PSB_2D_SRC_8888ARGB;
	break;
    }
    switch (ddepth) {
    case 8:
	tdc->dMode = PSB_2D_DST_332RGB;
	psbPixelARGB8888(PICT_r3g3b2, &pix, &tdc->fixPat);
	break;
    case 15:
	tdc->dMode = PSB_2D_DST_555RGB;
	psbPixelARGB8888(PICT_x1r5g5b5, &pix, &tdc->fixPat);
	break;
    case 16:
	tdc->dMode = PSB_2D_DST_565RGB;
	psbPixelARGB8888(PICT_r5g6b5, &pix, &tdc->fixPat);
	break;
    case 24:
	tdc->dMode = PSB_2D_DST_0888ARGB;
	psbPixelARGB8888(PICT_x8r8g8b8, &pix, &tdc->fixPat);
	break;
    default:
	tdc->dMode = PSB_2D_DST_8888ARGB;
	psbPixelARGB8888(PICT_a8r8g8b8, &pix, &tdc->fixPat);
	break;
    }
}

#ifdef PSB_FIX_BUG_W8
static void
psbAccelCompositeBugDelta0(unsigned rotation, int *xDelta, int *yDelta)
{
    switch (rotation) {
    case PSB_2D_ROT_270DEGS:
	*xDelta = 0;
	*yDelta = 4;
	break;
    case PSB_2D_ROT_180DEGS:
	*xDelta = -4;
	*yDelta = 0;
	break;
    case PSB_2D_ROT_90DEGS:
	*xDelta = 0;
	*yDelta = -4;
	break;
    default:
	*xDelta = 4;
	*yDelta = 0;
	break;
    }
}

static void
psbAccelCompositeBugDelta1(unsigned rotation, int *xDelta, int *yDelta)
{
    switch (rotation) {
    case PSB_2D_ROT_270DEGS:
	*xDelta = -4;
	*yDelta = 0;
	break;
    case PSB_2D_ROT_90DEGS:
	*xDelta = 4;
	*yDelta = 0;
	break;
    default:			       /* should not arrive here */
	*xDelta = 0;
	*yDelta = 0;
	break;
    }
}
#endif

static Bool
psbExaPixmapIsOffscreen(PixmapPtr p)
{
    ScreenPtr pScreen = p->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);

    return ((unsigned long)p->devPrivate.ptr 
		- (unsigned long)mmBufVirtual(pPsb->pPsbExa->exaBuf.buf)) 
			< mmBufSize(pPsb->pPsbExa->exaBuf.buf);
}

void
psbExaClose(PsbExaPtr pPsbExa, ScreenPtr pScreen)
{
    PSB_DEBUG(pScreen->myNum, 3, "psbExaClose\n");

    if (!pPsbExa)
	return;

    if (pPsbExa->exaUp) {
	exaDriverFini(pScreen);
	pPsbExa->exaUp = FALSE;
    }
    if (pPsbExa->pExa) {
	xfree(pPsbExa->pExa);
	pPsbExa->pExa = NULL;
    }
    psbClearBufItem(&pPsbExa->exaBuf);
    psbClearBufItem(&pPsbExa->scratchBuf);
    psbClearBufItem(&pPsbExa->tmpBuf);

    xfree(pPsbExa);
}

static Bool
psbExaAllocBuffers(ScrnInfoPtr pScrn, PsbExaPtr pPsbExa)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

#ifdef XF86DRI
    PsbDRIPtr pPsbDRI;
#endif

    mmInitListHead(&pPsbExa->exaBuf.head);
    mmInitListHead(&pPsbExa->scratchBuf.head);
    mmInitListHead(&pPsbExa->tmpBuf.head);

    psbAddBufItem(&pPsb->buffers, &pPsbExa->exaBuf,
		  pDevice->man->createBuf(pDevice->man, pPsb->exaSize, 0,
					  MM_FLAG_READ |
					  MM_FLAG_WRITE |
					  MM_FLAG_MEM_TT |
 					  MM_FLAG_SHAREABLE, /* for psbTexOffsetStart */
					  MM_HINT_DONT_FENCE));
    if (!pPsbExa->exaBuf.buf)
	return FALSE;

#ifdef XF86DRI
    pPsbDRI = (PsbDRIPtr) pPsb->pDRIInfo->devPrivate;
    if (pPsbDRI) {
        drmBO *bo = mmKernelBuf(pPsbExa->exaBuf.buf);
        if(bo) {
           pPsbDRI->exaBufHandle = bo->handle;
        }
    }
#endif

    psbAddBufItem(&pPsb->buffers, &pPsbExa->scratchBuf,
		  pDevice->man->createBuf(pDevice->man, pPsb->exaScratchSize,
					  0,
					  MM_FLAG_READ | MM_FLAG_WRITE |
					  MM_FLAG_MEM_TT,
					  MM_HINT_DONT_FENCE));

    if (!pPsbExa->scratchBuf.buf)
	return FALSE;

    pPsbExa->tmpBuf.buf = NULL;

    return TRUE;
}

static CARD32
psbAccelCopyDirection(int xdir, int ydir)
{
    if (xdir < 0)
	return ((ydir < 0) ? PSB_2D_COPYORDER_BR2TL : PSB_2D_COPYORDER_TR2BL);
    else
	return ((ydir < 0) ? PSB_2D_COPYORDER_BL2TR : PSB_2D_COPYORDER_TL2BR);
}

/*
 * Psb Exa syncs using the buffer manager map() method.
 * on a per-buffer basis.
 * This relies on EXA always calling.
 * prepareAccess and finishAccess around CPU accesses.
 * to graphics-mapped memory.
 *
 * Note that the buffer manager map() method may sleep, to
 * save CPU, and slow rescheduling may make the X server
 * sluggish if the CPU is under heavy load.
 *
 * If this is a serious issue,
 * A polling map() can be implemented using the NO_BLOCK map flag.
 */

static void
psbExaWaitMarker(ScreenPtr pScreen, int marker)
{
}

static int
psbExaMarkSync(ScreenPtr pScreen)
{
    /*
     * See psbExaWaitMarker.
     */

    return 1;
}

static int
psbExaCheckTransform(PictTransformPtr tr)
{
    if (tr == NULL)
	return 0;

    if (tr->matrix[0][0] == IntToxFixed(1) &&
	tr->matrix[0][1] == IntToxFixed(0) &&
	tr->matrix[1][0] == IntToxFixed(0) &&
	tr->matrix[1][1] == IntToxFixed(1))
	return PSB_2D_ROT_NONE;

    if (tr->matrix[0][0] == IntToxFixed(0) &&
	tr->matrix[0][1] == IntToxFixed(-1) &&
	tr->matrix[1][0] == IntToxFixed(1) &&
	tr->matrix[1][1] == IntToxFixed(0))
	return PSB_2D_ROT_270DEGS;

    if (tr->matrix[0][0] == IntToxFixed(-1) &&
	tr->matrix[0][1] == IntToxFixed(0) &&
	tr->matrix[1][0] == IntToxFixed(0) &&
	tr->matrix[1][1] == IntToxFixed(-1))
	return PSB_2D_ROT_180DEGS;

    if (tr->matrix[0][0] == IntToxFixed(0) &&
	tr->matrix[0][1] == IntToxFixed(1) &&
	tr->matrix[1][0] == IntToxFixed(-1) &&
	tr->matrix[1][1] == IntToxFixed(0))
	return PSB_2D_ROT_90DEGS;

    /*
     * We don't support scaling etc. at this point.
     */

    return -1;
}

static Bool
psbDstSupported(unsigned format)
{
    PsbFormatPointer fm = &psbCompFormats[PSB_FMT_HASH(format)];

    if (fm->pictFormat != format)
	return FALSE;

    return fm->dstSupported;
}

static Bool
psbSrcSupported(unsigned format, Bool pat)
{
    PsbFormatPointer fm = &psbCompFormats[PSB_FMT_HASH(format)];

    if (fm->pictFormat != format)
	return FALSE;

    return ((pat) ? fm->patSupported : fm->srcSupported);
}

static PsbFormatPointer
psbCompFormat(unsigned format)
{
    return &psbCompFormats[PSB_FMT_HASH(format)];
}

static Bool
psbExaCheckComposite(int op,
		     PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		     PicturePtr pDstPicture)
{
    DrawablePtr pDraw = pSrcPicture->pDrawable;
    int w = pDraw->width;
    int h = pDraw->height;

    if (op > PictOpAdd)
	return FALSE;

    if (!pSrcPicture->repeat && w * h < PSB_EXA_MIN_COMPOSITE)
	return FALSE;

    if (pMaskPicture == NULL)
	return TRUE;

    if (!pMaskPicture->repeat && w * h < PSB_EXA_MIN_COMPOSITE)
	return FALSE;

    if (pMaskPicture->componentAlpha)
	return FALSE;

    if (pMaskPicture->transform || pDstPicture->transform)
	return FALSE;

    return TRUE;
}

static void
psbExaBoundingLine(Bool sub, int coord, int *add, int limit, int *cDelta)
{
    int origCoord = coord;

    if (!sub) {
	if (coord < 0) {
	    *add += coord;
	    coord = 0;
	    if (*add < 0)
		*add = 0;
	}
	if (coord >= limit)
	    *add = 0;
	if (coord + *add > limit)
	    *add = limit - coord;
    } else {
	if (coord > limit) {
	    *add -= coord - limit;
	    coord = limit;
	    if (*add < 0)
		*add = 0;
	}
	if (coord < 1)
	    *add = 0;
	if (coord < *add)
	    *add = coord;
    }
    *cDelta = coord - origCoord;
}

/*
 * Apply a source picture bounding box to source coordinates and composite
 * dimensions after transform. Adjust source offset to blitter requirements.
 */

static void
psbExaAdjustForTransform(unsigned rot, int srcWidth, int srcHeight,
			 int *srcX, int *srcY, int *maskX, int *maskY,
			 int *dstX, int *dstY, int *width, int *height)
{
    int xDelta, yDelta;

    switch (rot) {
    case PSB_2D_ROT_270DEGS:
	psbExaBoundingLine(TRUE, *srcX, height, srcWidth, &xDelta);
	psbExaBoundingLine(FALSE, *srcY, width, srcHeight, &yDelta);
	*srcX += xDelta - 1;
	*srcY += yDelta;
	*maskX += xDelta - 1;
	*maskY += yDelta;
	*dstX += yDelta;
	*dstY -= xDelta;
	break;
    case PSB_2D_ROT_90DEGS:
	psbExaBoundingLine(FALSE, *srcX, height, srcWidth, &xDelta);
	psbExaBoundingLine(TRUE, *srcY, width, srcHeight, &yDelta);
	*srcX += xDelta;
	*srcY += yDelta - 1;
	*maskX += xDelta;
	*maskY += yDelta - 1;
	*dstX -= yDelta;
	*dstY += xDelta;
	break;
    case PSB_2D_ROT_180DEGS:
	psbExaBoundingLine(TRUE, *srcX, width, srcWidth, &xDelta);
	psbExaBoundingLine(TRUE, *srcY, height, srcHeight, &yDelta);
	*srcX += xDelta - 1;
	*srcY += yDelta - 1;
	*maskX += xDelta - 1;
	*maskY += yDelta - 1;
	*dstX -= xDelta;
	*dstY -= yDelta;
	break;
    default:
	psbExaBoundingLine(FALSE, *srcX, width, srcWidth, &xDelta);
	psbExaBoundingLine(FALSE, *srcY, height, srcHeight, &yDelta);
	*srcX += xDelta;
	*srcY += yDelta;
	*maskX += xDelta;
	*maskY += yDelta;
	*dstX -= xDelta;
	*dstY -= yDelta;
	break;
    }
}

static void
psbInitComposite(void)
{
    int i;
    unsigned tmp;
    unsigned hash;
    PsbFormatPointer format;

    for (i = 0; i < PSB_FMT_HASH_SIZE; ++i) {
	psbCompFormats[i].pictFormat = 0;
    }

    for (i = 0; i < PSB_NUM_COMP_FORMATS; ++i) {
	tmp = psbFormats[i][0];
	hash = PSB_FMT_HASH(tmp);
	format = &psbCompFormats[hash];

	if (format->pictFormat)
	    FatalError("Bad composite format hash function.\n");

	format->pictFormat = tmp;
	format->dstSupported = (psbFormats[i][4] != 0);
	format->patSupported = (psbFormats[i][5] != 0);
	format->srcSupported = (psbFormats[i][6] != 0);
	format->dstFormat = psbFormats[i][1];
	format->patFormat = psbFormats[i][2];
	format->srcFormat = psbFormats[i][3];
    }
}

Bool
psbExaPrepareAccess(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    void *ptr;
    PsbBufListPtr b;
    unsigned flags;

    ptr = (void *)(exaGetPixmapOffset(pPix) +
		   (unsigned long)mmBufVirtual(pPsb->pPsbExa->exaBuf.buf));
    b = psbInBuffer(&pPsb->buffers, ptr);
    if (b) {
	flags = (index == EXA_PREPARE_DEST) ?
	    DRM_BO_FLAG_WRITE : DRM_BO_FLAG_READ;

	/*
	 * We already have a virtual address of the pixmap.
	 * Use mapBuf as a syncing operation only.
	 * This makes sure the hardware has finished rendering to this
	 * buffer.
	 */

	if (b->buf->man->mapBuf(b->buf, flags, 0))
	    return FALSE;
    }
    return TRUE;
}

void
psbExaFinishAccess(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbBufListPtr b;
    void *ptr;

    ptr = (void *)(exaGetPixmapOffset(pPix) +
		   (unsigned long)mmBufVirtual(pPsb->pPsbExa->exaBuf.buf));
    b = psbInBuffer(&pPsb->buffers, ptr);
    if (b)
	(void)b->buf->man->unMapBuf(b->buf);
}

Bool
psbExaGetSuperOffset(PixmapPtr p, unsigned long *offset,
		     struct _MMBuffer **buffer)
{
    ScreenPtr pScreen = p->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbBufListPtr b;
    void *ptr;

    ptr = (void *)(exaGetPixmapOffset(p) +
		   (unsigned long)mmBufVirtual(pPsb->pPsbExa->exaBuf.buf));

    b = psbInBuffer(&pPsb->buffers, ptr);

    if (!b) {
	return FALSE;
    }

    *offset = (unsigned long)ptr - (unsigned long)mmBufVirtual(b->buf);
    *buffer = b->buf;

    return TRUE;
}

static void
psbExaDoneSuper(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    Psb2DBufferPtr cb = &pPsb->superC;

    psbFlush2D(cb, DRM_FENCE_FLAG_NO_USER, NULL);
    psbDRIUnlock(pScrn);
}

static void
psbExaDoneComposite(PixmapPtr pPixmap)
{
    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    Psb2DBufferPtr cb = &pPsb->superC;
    PsbTwodContextPtr tdc = &pPsb->td;

    if (!tdc->comp2D)
	psbExaDoneComposite3D(pPixmap);
    else
	psbFlush2D(cb, DRM_FENCE_FLAG_NO_USER, NULL);

    psbDRIUnlock(pScrn);
}

static void
psbAccelVolatileStateCallback(Psb2DBufferPtr ptrCb, void *arg)
{
    PsbTwodContextPtr tdc = (PsbTwodContextPtr) arg;
    int ret;

    PSB_SUPER_2D_VARS(ptrCb);
    if (tdc->srcState || tdc->dstState) {
        PSB_SUPER_2D_OUT(PSB_2D_FENCE_BH);
	tdc->bhFence = FALSE;
    }

    if (tdc->srcState) {
	PSB_SUPER_2D_OUT(PSB_2D_SRC_SURF_BH |
			 (tdc->sMode & PSB_2D_SRC_FORMAT_MASK) |
			 ((tdc->sStride << PSB_2D_SRC_STRIDE_SHIFT) &
			  PSB_2D_SRC_STRIDE_MASK));
	PSB_SUPER_2D_RELOC_OFFSET(tdc->sOffset, mmKernelBuf(tdc->sBuffer), 
				  0, 0);
    }

    if (tdc->dstState) {
	PSB_SUPER_2D_OUT(PSB_2D_DST_SURF_BH |
			 (tdc->dMode & PSB_2D_DST_FORMAT_MASK) |
			 ((tdc->dStride << PSB_2D_DST_STRIDE_SHIFT) &
			  PSB_2D_DST_STRIDE_MASK));
	PSB_SUPER_2D_RELOC_OFFSET(tdc->dOffset, mmKernelBuf(tdc->dBuffer), 
				  0, 0);
    }
    PSB_SUPER_2D_DONE(ret);
}
	
static int
psbAccelSuperEmitState(Psb2DBufferPtr ptrCb, PsbTwodContextPtr tdc)
{
    int ret;

    PSB_SUPER_2D_VARS(ptrCb);
    PSB_SUPER_2D_SIZE(5, 2, 0, 0);
    psbAccelVolatileStateCallback(ptrCb, tdc);
    PSB_SUPER_2D_DONE(ret);
    
    return TRUE;
}


static void
psbAccelSuperSolidHelper(Psb2DBufferPtr ptrCb, PsbTwodContextPtr tdc,
			 int x, int y, int w, int h,
			 unsigned mode, CARD32 fg, unsigned cmd)
{
    int ret;

    PSB_SUPER_2D_VARS(ptrCb);
    PSB_SUPER_2D_SIZE(5, 0, 0, 0);

    if (tdc->bhFence)
	PSB_SUPER_2D_OUT(PSB_2D_FENCE_BH);
    PSB_SUPER_2D_OUT(cmd);
    PSB_SUPER_2D_OUT(fg);
    PSB_SUPER_2D_OUT(((x << PSB_2D_DST_XSTART_SHIFT) & PSB_2D_DST_XSTART_MASK)
		     | ((y << PSB_2D_DST_YSTART_SHIFT) &
			PSB_2D_DST_YSTART_MASK));
    PSB_SUPER_2D_OUT(((w << PSB_2D_DST_XSIZE_SHIFT) & PSB_2D_DST_XSIZE_MASK) |
		     ((h << PSB_2D_DST_YSIZE_SHIFT) & PSB_2D_DST_YSIZE_MASK));

    PSB_SUPER_2D_DONE(ret);
    tdc->bhFence = TRUE;

    if (ret) {
	PSB_DEBUG(0, 3, "Error = %i\n", ret);
    }
}

static Bool
psbExaPrepareSuperSolid(PixmapPtr pPixmap, int alu, Pixel planeMask, Pixel fg)
{
    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;
    int rop = psbPatternROP[alu];

    if (!EXA_PM_IS_SOLID(&pPixmap->drawable, planeMask))
	return FALSE;

    /* can't do depth 4 */
    if (pPixmap->drawable.depth == 4)
	return FALSE;

    /*
     * Do solid fills in software. Much faster;
     */
    if (alu == GXcopy)
	return FALSE;

    psbDRILock(pScrn, 0);
    psbAccelSetMode(tdc, pPixmap->drawable.depth, pPixmap->drawable.depth,
		    fg);
    tdc->cmd =
	PSB_2D_BLIT_BH | PSB_2D_ROT_NONE | PSB_2D_COPYORDER_TL2BR |
	PSB_2D_DSTCK_DISABLE | PSB_2D_SRCCK_DISABLE | PSB_2D_USE_FILL | ((rop
									  <<
									  PSB_2D_ROP3B_SHIFT)
									 &
									 PSB_2D_ROP3B_MASK)
	| ((rop << PSB_2D_ROP3A_SHIFT) & PSB_2D_ROP3A_MASK);

    if (!psbExaGetSuperOffset(pPixmap, &tdc->dOffset, &tdc->dBuffer))
	goto out_err;

    tdc->dStride = exaGetPixmapPitch(pPixmap);
    tdc->srcState = FALSE;
    tdc->dstState = TRUE;
    psbAccelSuperEmitState(&pPsb->superC, tdc);
    return TRUE;
  out_err:
    psbDRIUnlock(pScrn);
    return FALSE;
}

static void
psbExaSuperSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;
    Psb2DBufferPtr cb2 = &pPsb->superC;

    int w = x2 - x1;
    int h = y2 - y1;

#ifdef PSB_FIX_BUG_W8
    if (w == 8) {
	w = 4;

	psbAccelSuperSolidHelper(cb2, tdc, x1, y1, w, h,
				 tdc->dMode, tdc->fixPat, tdc->cmd);

	x1 += 4;
    }
#endif
    psbAccelSuperSolidHelper(cb2, tdc, x1, y1, w, h,
			     tdc->dMode, tdc->fixPat, tdc->cmd);

}

static void
psbAccelSuperCopyHelper(Psb2DBufferPtr ptrCb, PsbTwodContextPtr tdc,
			int xs, int ys, int xd, int yd, int w, int h,
			unsigned srcMode, unsigned dstMode,
			unsigned fg, unsigned cmd)
{
    int ret;

    PSB_SUPER_2D_VARS(ptrCb);
    PSB_SUPER_2D_SIZE(8, 0, 0, 0);
    
    if (tdc->bhFence)
	PSB_SUPER_2D_OUT(PSB_2D_FENCE_BH);
    PSB_SUPER_2D_OUT(PSB_2D_SRC_OFF_BH |
		     ((xs << PSB_2D_SRCOFF_XSTART_SHIFT) &
		      PSB_2D_SRCOFF_XSTART_MASK) | ((ys <<
						     PSB_2D_SRCOFF_YSTART_SHIFT)
						    &
						    PSB_2D_SRCOFF_YSTART_MASK));
    PSB_SUPER_2D_OUT(cmd);
    PSB_SUPER_2D_OUT(fg);
    PSB_SUPER_2D_OUT(((xd << PSB_2D_DST_XSTART_SHIFT) &
		      PSB_2D_DST_XSTART_MASK) | ((yd <<
						  PSB_2D_DST_YSTART_SHIFT) &
						 PSB_2D_DST_YSTART_MASK));
    PSB_SUPER_2D_OUT(((w << PSB_2D_DST_XSIZE_SHIFT) & PSB_2D_DST_XSIZE_MASK) |
		     ((h << PSB_2D_DST_YSIZE_SHIFT) & PSB_2D_DST_YSIZE_MASK));

    PSB_SUPER_2D_DONE(ret);
    tdc->bhFence = TRUE;

    if (ret) {
	PSB_DEBUG(0, 3, "Error = %i\n", ret);
    }
}

static Bool
psbExaPrepareSuperCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap, int xdir,
		       int ydir, int alu, Pixel planeMask)
{
    int rop;
    ScrnInfoPtr pScrn = xf86Screens[pDstPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;

    /* can't do depth 4 */
    if (pSrcPixmap->drawable.depth == 4 || pDstPixmap->drawable.depth == 4)
	return FALSE;

    if (pSrcPixmap->drawable.width * pSrcPixmap->drawable.height <
	PSB_EXA_MIN_COPY
	|| pDstPixmap->drawable.width * pDstPixmap->drawable.height <
	PSB_EXA_MIN_COPY)
	return FALSE;

    psbDRILock(pScrn, 0);
    tdc->direction = psbAccelCopyDirection(xdir, ydir);

    rop = (EXA_PM_IS_SOLID(&pDstPixmap->drawable, planeMask)) ?
	psbCopyROP[alu] : psbCopyROP_PM(alu);

    psbAccelSetMode(tdc, pSrcPixmap->drawable.depth,
		    pDstPixmap->drawable.depth, planeMask);
    tdc->cmd = PSB_2D_BLIT_BH | PSB_2D_ROT_NONE |
	tdc->direction |
	PSB_2D_DSTCK_DISABLE |
	PSB_2D_SRCCK_DISABLE |
	PSB_2D_USE_FILL |
	((rop << PSB_2D_ROP3B_SHIFT) & PSB_2D_ROP3B_MASK) |
	((rop << PSB_2D_ROP3A_SHIFT) & PSB_2D_ROP3A_MASK);

    if (!psbExaGetSuperOffset(pSrcPixmap, &tdc->sOffset, &tdc->sBuffer))
	goto out_err;

    if (!psbExaGetSuperOffset(pDstPixmap, &tdc->dOffset, &tdc->dBuffer))
	goto out_err;

    tdc->sStride = exaGetPixmapPitch(pSrcPixmap);
    tdc->dStride = exaGetPixmapPitch(pDstPixmap);

    tdc->sBPP = pSrcPixmap->drawable.bitsPerPixel >> 3;
    tdc->srcState = TRUE;
    tdc->dstState = TRUE;
    psbAccelSuperEmitState(&pPsb->superC, tdc);

    return TRUE;
  out_err:
    psbDRIUnlock(pScrn);
    return FALSE;
}

static void
psbExaSuperCopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY,
		int width, int height)
{
    ScrnInfoPtr pScrn = xf86Screens[pDstPixmap->drawable.pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;
    Psb2DBufferPtr cb2 = &pPsb->superC;

#ifdef PSB_FIX_BUG_OVERLAP
    tdc->cmd &= PSB_2D_COPYORDER_CLRMASK;
    tdc->direction = (tdc->sOffset != tdc->dOffset) ?
	PSB_2D_COPYORDER_TL2BR :
	psbAccelCopyDirection(srcX - dstX, srcY - dstY);
    tdc->cmd |= tdc->direction;
#endif

    if (tdc->direction == PSB_2D_COPYORDER_BR2TL ||
	tdc->direction == PSB_2D_COPYORDER_TR2BL) {
	srcX += width - 1;
	dstX += width - 1;
    }
    if (tdc->direction == PSB_2D_COPYORDER_BR2TL ||
	tdc->direction == PSB_2D_COPYORDER_BL2TR) {
	srcY += height - 1;
	dstY += height - 1;
    }
#ifdef PSB_FIX_BUG_W8
    if (width == 8) {
	width = 4;

	psbAccelSuperCopyHelper(cb2, tdc,
				srcX, srcY, dstX, dstY, width, height,
				tdc->sMode, tdc->dMode, tdc->fixPat,
				tdc->cmd);

	srcX += 4;
	dstX += 4;

	psbAccelSuperCopyHelper(cb2, tdc,
				srcX, srcY, dstX, dstY, width, height,
				tdc->sMode, tdc->dMode, tdc->fixPat,
				tdc->cmd);
    } else
#endif
	psbAccelSuperCopyHelper(cb2, tdc,
				srcX, srcY, dstX, dstY, width, height,
				tdc->sMode, tdc->dMode, tdc->fixPat,
				tdc->cmd);

}

static void
psbAccelSuperCompositeHelper(Psb2DBufferPtr ptrCb, PsbTwodContextPtr tdc,
			     int xs, int ys, unsigned xp, unsigned yp,
			     int xd, int yd, int wp, int hp, int w, int h,
			     unsigned srcMode, unsigned patMode,
			     unsigned dstMode, unsigned fg, unsigned cmd,
			     unsigned alpha1, unsigned alpha2, Bool usePat)
{
    int ret;

    PSB_SUPER_2D_VARS(ptrCb);
    PSB_SUPER_2D_SIZE(12, 0, 0, 0);

    if (tdc->bhFence)
	PSB_SUPER_2D_OUT(PSB_2D_FENCE_BH);
    PSB_SUPER_2D_OUT(PSB_2D_SRC_OFF_BH |
		     ((xs << PSB_2D_SRCOFF_XSTART_SHIFT) &
		      PSB_2D_SRCOFF_XSTART_MASK) | ((ys <<
						     PSB_2D_SRCOFF_YSTART_SHIFT)
						    &
						    PSB_2D_SRCOFF_YSTART_MASK));
    if (cmd & PSB_2D_ALPHA_ENABLE) {
	PSB_SUPER_2D_OUT(PSB_2D_CTRL_BH | PSB_2D_ALPHA_CTRL);
	PSB_SUPER_2D_OUT(alpha1);
	PSB_SUPER_2D_OUT(alpha2);
    }

    PSB_SUPER_2D_OUT(cmd);

    if (!(cmd & PSB_2D_USE_PAT))
	PSB_SUPER_2D_OUT(fg);

    PSB_SUPER_2D_OUT(((xd << PSB_2D_DST_XSTART_SHIFT) &
		      PSB_2D_DST_XSTART_MASK) | ((yd <<
						  PSB_2D_DST_YSTART_SHIFT) &
						 PSB_2D_DST_YSTART_MASK));
    PSB_SUPER_2D_OUT(((w << PSB_2D_DST_XSIZE_SHIFT) & PSB_2D_DST_XSIZE_MASK) |
		     ((h << PSB_2D_DST_YSIZE_SHIFT) & PSB_2D_DST_YSIZE_MASK));

    PSB_SUPER_2D_DONE(ret);

    tdc->bhFence = TRUE;

    if (ret) {
	PSB_DEBUG(0, 3, "Error = %i\n", ret);
    }

}

static Bool
psbExaPrepareSuperComposite(int op, PicturePtr pSrcPicture,
			    PicturePtr pMaskPicture, PicturePtr pDstPicture,
			    PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
    ScreenPtr pScreen = pDst->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;
    PsbFormatPointer format;

    psbDRILock(pScrn, 0);

    tdc->cmd = 0;
    if (op == PictOpSrc && pMaskPicture == NULL &&
	pSrcPicture->format == pDstPicture->format && !pSrcPicture->repeat) {

	/*
	 * Try 2D compositing. Pure blits and rotation.
	 */

	if (!psbDstSupported(pDstPicture->format))
	    goto composite3D;

	if (!psbSrcSupported(pSrcPicture->format, pSrcPicture->repeat))
	    goto composite3D;

	tdc->srcTransform = pSrcPicture->transform;
	tdc->srcRot = psbExaCheckTransform(pSrcPicture->transform);

	format = psbCompFormat(pSrcPicture->format);
	tdc->sMode =
	    (pSrcPicture->repeat) ? format->patFormat : format->srcFormat;
	format = psbCompFormat(pDstPicture->format);
	tdc->dMode = format->dstFormat;

	if (tdc->srcRot == -1)
	    goto composite3D;

	tdc->cmd = PSB_2D_BLIT_BH |
	    tdc->srcRot |
	    PSB_2D_COPYORDER_TL2BR |
	    PSB_2D_DSTCK_DISABLE |
	    PSB_2D_SRCCK_DISABLE | PSB_2D_USE_FILL | PSB_2D_ROP3_SRCCOPY;

	if (!psbExaGetSuperOffset(pSrc, &tdc->sOffset, &tdc->sBuffer))
	    goto out_err;

	tdc->sStride = exaGetPixmapPitch(pSrc);
	tdc->sBPP = pSrc->drawable.bitsPerPixel >> 3;
	tdc->srcWidth = pSrc->drawable.width;
	tdc->srcHeight = pSrc->drawable.height;

	if (!psbExaGetSuperOffset(pDst, &tdc->dOffset, &tdc->dBuffer))
	    goto out_err;

	tdc->dStride = exaGetPixmapPitch(pDst);
	tdc->comp2D = TRUE;
	tdc->srcState = TRUE;
	tdc->dstState = TRUE;
	psbAccelSuperEmitState(&pPsb->superC, tdc);

	return TRUE;
    }
  composite3D:
    if (!pPsb->hasXpsb)
	goto out_err;

    if (pSrcPicture->transform)
	goto out_err;

    if (psbExaPrepareComposite3D(op, pSrcPicture, pMaskPicture,
				 pDstPicture, pSrc, pMask, pDst)) {
	tdc->comp2D = FALSE;
	return TRUE;
    }
  out_err:
    psbDRIUnlock(pScrn);
    return FALSE;
}

/*
 * In the absence of copy-on-write user buffers, this is currently
 * the fastest way to do UploadToScreen on Poulsbo, provided that the
 * destination buffer is write-combined. We should see a throughput
 * in excess of 600MiB / s.
 */

static Bool
psbExaUploadToScreen(PixmapPtr pDst, int x, int y, int w, int h, char *src,
		     int src_pitch)
{
    ScreenPtr pScreen = pDst->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    CARD8 *ptr;
    PsbBufListPtr b;
    unsigned dstPitch = exaGetPixmapPitch(pDst);
    unsigned int bitsPerPixel = pDst->drawable.bitsPerPixel;
    unsigned wBytes = (w * bitsPerPixel) >> 3;

    if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32)
	return FALSE;

    ptr = (CARD8 *) (exaGetPixmapOffset(pDst) +
		     (unsigned long)mmBufVirtual(pPsb->pPsbExa->exaBuf.buf));
    b = psbInBuffer(&pPsb->buffers, ptr);
    if (!b)
	return FALSE;

    ptr += y * dstPitch + ((x * bitsPerPixel) >> 3);

    if (b->buf->man->mapBuf(b->buf, MM_FLAG_WRITE, 0))
	return FALSE;

    while (h--) {
	memcpy(ptr, src, wBytes);
	ptr += dstPitch;
	src += src_pitch;
    }

    b->buf->man->unMapBuf(b->buf);
    return TRUE;
}

static void
psbExaSuperComposite(PixmapPtr pDst, int srcX, int srcY, int maskX, int maskY,
		     int dstX, int dstY, int width, int height)
{
    ScreenPtr pScreen = pDst->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbTwodContextPtr tdc = &pPsb->td;
    Psb2DBufferPtr cb2 = &pPsb->superC;

#ifdef PSB_FIX_BUG_W8
    int xDelta, yDelta;
    unsigned rot = tdc->cmd & PSB_2D_ROT_MASK;
#endif
    if (!tdc->comp2D) {
	psbExaComposite3D(pDst, srcX, srcY, maskX, maskY,
			  dstX, dstY, width, height);
	return;
    }

    if (tdc->srcTransform) {
	PictVector d;

	d.vector[0] = IntToxFixed(srcX);
	d.vector[1] = IntToxFixed(srcY);
	d.vector[2] = IntToxFixed(1);

	PictureTransformPoint(tdc->srcTransform, &d);
	srcX = xFixedToInt(d.vector[0]);
	srcY = xFixedToInt(d.vector[1]);

	psbExaAdjustForTransform(tdc->srcRot, tdc->srcWidth, tdc->srcHeight,
				 &srcX, &srcY, &maskX, &maskY, &dstX, &dstY,
				 &width, &height);
    }
#ifdef PSB_FIX_BUG_W8

    if (width == 8 && height > 8) {
	psbAccelCompositeBugDelta0(rot, &xDelta, &yDelta);

	width = 4;
	psbAccelSuperCompositeHelper(cb2, tdc, srcX, srcY, 0, 0, dstX, dstY,
				     1, 1, width, height, tdc->sMode,
				     tdc->mMode, tdc->dMode, tdc->fixPat,
				     tdc->cmd, 0, 0, FALSE);

	srcX += xDelta;
	srcY += yDelta;
	dstX += 4;
	psbAccelSuperCompositeHelper(cb2, tdc, srcX, srcY, 0, 0, dstX, dstY,
				     1, 1, width, height, tdc->sMode,
				     tdc->mMode, tdc->dMode, tdc->fixPat,
				     tdc->cmd, 0, 0, FALSE);

    } else if (height == 8
	       && (rot == PSB_2D_ROT_90DEGS || rot == PSB_2D_ROT_270DEGS)) {
	psbAccelCompositeBugDelta1(rot, &xDelta, &yDelta);
	height = 4;

	psbAccelSuperCompositeHelper(cb2, tdc, srcX, srcY, 0, 0, dstX, dstY,
				     1, 1, width, height, tdc->sMode,
				     tdc->mMode, tdc->dMode, tdc->fixPat,
				     tdc->cmd, 0, 0, FALSE);

	srcX += xDelta;
	dstY += 4;
	psbAccelSuperCompositeHelper(cb2, tdc, srcX, srcY, 0, 0, dstX, dstY,
				     1, 1, width, height, tdc->sMode,
				     tdc->mMode, tdc->dMode, tdc->fixPat,
				     tdc->cmd, 0, 0, FALSE);

    } else
#endif
	psbAccelSuperCompositeHelper(cb2, tdc, srcX, srcY, 0, 0, dstX, dstY,
				     1, 1, width, height, tdc->sMode,
				     tdc->mMode, tdc->dMode, tdc->fixPat,
				     tdc->cmd, 0, 0, FALSE);
}

PsbExaPtr
psbExaInit(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbExaPtr pPsbExa;
    ExaDriverPtr pExa;

    pPsbExa = xcalloc(sizeof(*pPsbExa), 1);
    if (!pPsbExa)
	goto out_err;

    pPsbExa->pExa = exaDriverAlloc();
    pExa = pPsbExa->pExa;
    if (!pExa) {
	goto out_err;
    }

    if (!psbExaAllocBuffers(pScrn, pPsbExa)) {
	goto out_err;
    }

    memset(pExa, 0, sizeof(*pExa));
    pExa->exa_major = 2;
    pExa->exa_minor = 2;
    pExa->memoryBase = mmBufVirtual(pPsbExa->exaBuf.buf);
    pExa->offScreenBase = 0;
    pExa->memorySize = mmBufSize(pPsbExa->exaBuf.buf);
    pExa->pixmapOffsetAlign = 8;
    pExa->pixmapPitchAlign = 32 * 4;
    pExa->flags = EXA_OFFSCREEN_PIXMAPS;
    pExa->maxX = 2047;
    pExa->maxY = 2047;
    pExa->WaitMarker = psbExaWaitMarker;
    pExa->MarkSync = psbExaMarkSync;
    pExa->PrepareSolid = psbExaPrepareSuperSolid;
    pExa->Solid = psbExaSuperSolid;
    pExa->DoneSolid = psbExaDoneSuper;
    pExa->PrepareCopy = psbExaPrepareSuperCopy;
    pExa->Copy = psbExaSuperCopy;
    pExa->DoneCopy = psbExaDoneSuper;
    pExa->CheckComposite = psbExaCheckComposite;
    pExa->PrepareComposite = psbExaPrepareSuperComposite;
    pExa->Composite = psbExaSuperComposite;
    pExa->DoneComposite = psbExaDoneComposite;
    pExa->PixmapIsOffscreen = psbExaPixmapIsOffscreen;
    pExa->PrepareAccess = psbExaPrepareAccess;
    pExa->FinishAccess = psbExaFinishAccess;
    pExa->UploadToScreen = psbExaUploadToScreen;

    if (!exaDriverInit(pScrn->pScreen, pExa)) {
	goto out_err;
    }

    if (!pPsb->secondary)
	psbInitComposite();

    pPsb->td.srcState = FALSE;
    pPsb->td.dstState = FALSE;
    pPsb->td.bhFence = TRUE;
    psbSetStateCallback(&pPsb->superC, &psbAccelVolatileStateCallback,
			&pPsb->td);

    return pPsbExa;

  out_err:
    psbExaClose(pPsbExa, pScrn->pScreen);

    return NULL;
}

#ifdef XF86DRI

#ifndef ExaOffscreenMarkUsed
extern void ExaOffscreenMarkUsed(PixmapPtr);
#endif

#ifndef ExaOffscreenMarkUsed
extern Bool exaPixmapIsOffscreen(PixmapPtr);
#endif

unsigned long long
psbTexOffsetStart(PixmapPtr pPix)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    exaMoveInPixmap(pPix);
    ExaOffscreenMarkUsed(pPix);

    if (!exaPixmapIsOffscreen(pPix))
        return ~0ULL;

    return exaGetPixmapOffset(pPix);
}

#endif
