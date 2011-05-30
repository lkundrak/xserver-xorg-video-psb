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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "psb_driver.h"
#include <unistd.h>

/* return the front buffer's drmBO */
drmBO *
psbScanoutBO(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);

    return mmKernelBuf(pPsb->front->entry.buf);
}

void
psbScanoutDestroy(PsbScanoutPtr scanout)
{
    PSB_DEBUG(-1, 3, "psbScanoutDestroy\n");

    if (!scanout)
	return;

#ifdef XF86DRI
    mmListDel(&scanout->sAreaList);
    psbDRIUpdateScanouts(scanout->pScrn);
#endif

    mmListDel(&scanout->entry.head);
    if (scanout->entry.buf) {

	/*
	 * Wait for buffer idle, remove the NO_EVICT flag and move
	 * buffer to local memory. It may take some time before the DRI
	 * clients release this buffer, so at least it's not in the way
	 * if / when we allocate a new scanout buffer.
	 */

	scanout->entry.buf->man->validateBuffer(scanout->entry.buf,
						MM_FLAG_READ |
						MM_FLAG_MEM_LOCAL,
						MM_MASK_MEM |
						MM_FLAG_NO_EVICT,
						MM_HINT_DONT_FENCE);
	/*
	 * Unreference the buffer. It will be destroyed when all dri clients
	 * release their reference as well.
	 */

	scanout->entry.buf->man->destroyBuf(scanout->entry.buf);
    }
    xfree(scanout);
}

void *
psbScanoutMap(PsbScanoutPtr scanout)
{
    MMManager *man = scanout->entry.buf->man;

    if (man->mapBuf(scanout->entry.buf, MM_FLAG_READ | MM_FLAG_WRITE, 0))
	return NULL;

    scanout->virtual = mmBufVirtual(scanout->entry.buf);
    return scanout->virtual;
}

void
psbScanoutUnMap(PsbScanoutPtr scanout)
{
    MMManager *man = scanout->entry.buf->man;

    man->unMapBuf(scanout->entry.buf);
}

PsbScanoutPtr
psbScanoutCreate(ScrnInfoPtr pScrn, unsigned cpp, unsigned depth,
		 unsigned width, unsigned height,
		 unsigned flags, Bool front, unsigned rotation)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    MMManager *man = pDevice->man;
    PsbScanoutPtr tmp;
    int pageSize = getpagesize();

    tmp = xcalloc(sizeof(*tmp), 1);
    if (!tmp)
	return NULL;

#ifdef XF86DRI
    mmInitListHead(&tmp->sAreaList);
#endif
    mmInitListHead(&tmp->entry.head);
    tmp->rotation = rotation;
    tmp->cpp = cpp;
    tmp->depth = depth;
    tmp->width = width;
    tmp->stride = cpp * width;
    tmp->stride = ALIGN_TO(tmp->stride, 64);
    tmp->height = height;
    tmp->size = height * tmp->stride;
    tmp->size = ALIGN_TO(tmp->size, pageSize);
    tmp->entry.buf = man->createBuf(man, tmp->size, 0,
				    MM_FLAG_READ | MM_FLAG_WRITE |
				    MM_FLAG_MEM_TT | MM_FLAG_MEM_VRAM |
				    MM_FLAG_NO_EVICT |
				    MM_FLAG_SHAREABLE | MM_FLAG_MAPPABLE,
				    MM_HINT_DONT_FENCE);
    if (!tmp->entry.buf) {
	goto out_err;
    }

    /*
     * We map to get the virtual address, and unmap shortly after.
     * It's allowed, (but not recommended for shared buffers) to access
     * a buffer when it is unmapped.
     */

    if (man->mapBuf(tmp->entry.buf, MM_FLAG_READ | MM_FLAG_WRITE, 0))
	goto out_err;

    tmp->virtual = mmBufVirtual(tmp->entry.buf);

    /*
     * VDC wants the offset relative to the aperture start.
     */

    tmp->offset = mmBufOffset(tmp->entry.buf) & 0x0FFFFFFF;
    man->unMapBuf(tmp->entry.buf);
    tmp->entry.validated = FALSE;
    mmListAddTail(&tmp->entry.head, &pPsb->buffers);

#ifdef XF86DRI
    if (front)
	mmListAdd(&tmp->sAreaList, &pPsb->sAreaList);
    else
	mmListAddTail(&tmp->sAreaList, &pPsb->sAreaList);
    tmp->pScrn = pScrn;
    psbDRIUpdateScanouts(pScrn);
#endif

    return tmp;
  out_err:
#ifdef XF86DRI
    tmp->pScrn = pScrn;
#endif
    psbScanoutDestroy(tmp);
    return NULL;
}

PsbBufListPtr
psbInBuffer(MMListHead * head, void *ptr)
{
    PsbBufListPtr entry;
    struct _MMBuffer *buf;
    MMListHead *list;
    unsigned long offset;

    mmListForEach(list, head) {
	entry = mmListEntry(list, PsbBufListRec, head);
	buf = entry->buf;
	offset = (unsigned long)ptr - (unsigned long)mmBufVirtual(buf);
	if (offset < mmBufSize(buf))
	    return entry;
    }
    return NULL;
}
