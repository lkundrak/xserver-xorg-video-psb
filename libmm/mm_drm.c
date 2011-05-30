/**************************************************************************
 *
 * Copyright 2006-2007 Tungsten Graphics, Inc., Cedar Park, TX., USA.
 * All Rights Reserved.
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

#include "mm_defines.h"
#include "mm_interface.h"
#include "xf86mm.h"
#include "xf86drm.h"
#include "stdio.h"
#include <assert.h>

/*
 * This is a simple wrapper around libdrm's buffer interface to be used 
 * when the DRM memory manager is activated.
 */

typedef struct _DRMManager
{
    MMManager mm;
    int drmFD;
    int initialized[DRM_BO_MEM_TYPES];
} DRMManager;

typedef struct _DRMBuffer
{
    struct _MMBuffer mb;
    drmBO buf;
} DRMBuffer;

typedef struct _DRMFence
{
    struct _MMFence mf;
    drmFence fence;
} DRMFence;

static int
initMemType(MMManager * man, unsigned long pOffset,
	    unsigned long pSize, unsigned memType)
{
    int ret;
    DRMManager *drmMM = containerOf(man, DRMManager, mm);

    ret = drmMMInit(drmMM->drmFD, pOffset, pSize, memType);
    if (ret)
	return ret;

    drmMM->initialized[memType] = 1;
}

static int
takeDownMemType(MMManager * man, int memType)
{
    int ret;
    DRMManager *drmMM = containerOf(man, DRMManager, mm);

    ret = drmMMTakedown(drmMM->drmFD, memType);
    if (ret)
	return ret;
    drmMM->initialized[memType] = 0;
}

static int
lock(MMManager * man, int memType, int lockBM, int ignoreEvict)
{
    DRMManager *drmMM = containerOf(man, DRMManager, mm);

    return drmMMLock(drmMM->drmFD, memType, lockBM, ignoreEvict);
}

static int
unLock(MMManager * man, int memType, int unlockBM)
{
    DRMManager *drmMM = containerOf(man, DRMManager, mm);

    return drmMMUnlock(drmMM->drmFD, memType, unlockBM);
}

static struct _MMBuffer *
createBuf(MMManager * man, unsigned long size,
	  unsigned pageAlignment, uint64_t mask, unsigned hint)
{
    DRMManager *drmMM = containerOf(man, DRMManager, mm);
    DRMBuffer *buf = (DRMBuffer *) malloc(sizeof(*buf));
    int ret;

    if (!buf)
	return NULL;

    ret = drmBOCreate(drmMM->drmFD, size, pageAlignment, NULL,
		      mask, hint, &buf->buf);
    if (ret) {
	free(buf);
	return NULL;
    }

    buf->mb.man = &drmMM->mm;
    return &buf->mb;
}

static struct _MMBuffer *
createUserBuf(MMManager * man, void *start,
	  unsigned long size, uint64_t mask, unsigned hint)
{
    DRMManager *drmMM = containerOf(man, DRMManager, mm);
    DRMBuffer *buf = (DRMBuffer *) malloc(sizeof(*buf));
    int ret;

    if (!buf)
	return NULL;

    ret = drmBOCreate(drmMM->drmFD, size, 0, start,
		      mask, hint, &buf->buf);
    if (ret) {
	free(buf);
	return NULL;
    }

    buf->mb.man = &drmMM->mm;
    return &buf->mb;
}

static void
destroyBuf(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);
    DRMManager *drmMM = containerOf(mb->man, DRMManager, mm);

    drmBOUnreference(drmMM->drmFD, &buf->buf);
    free(buf);
}

static int
mapBuf(struct _MMBuffer *mb, unsigned mapFlags, unsigned mapHint)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);
    DRMManager *drmMM = containerOf(mb->man, DRMManager, mm);
    void *virtual;
    int fd = drmMM->drmFD;
    int ret;

    ret = drmBOMap(fd, &buf->buf, mapFlags, mapHint, &virtual);
    return ret;
}

static int
unMapBuf(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);
    DRMManager *drmMM = containerOf(mb->man, DRMManager, mm);

    return drmBOUnmap(drmMM->drmFD, &buf->buf);
}

static int
validateBuffer(struct _MMBuffer *mb, uint64_t flags, uint64_t mask,
	       unsigned hint)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);
    DRMManager *drmMM = containerOf(mb->man, DRMManager, mm);

    return drmBOSetStatus(drmMM->drmFD, &buf->buf, flags, mask, hint, 0, 0);
}

static unsigned long
bufOffset(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.offset;
}

static unsigned long
bufFlags(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.flags;
}

static unsigned long
bufMask(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.mask;
}

static void *
bufVirtual(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.virtual;
}

static unsigned long
bufSize(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.size;
}

static unsigned
bufHandle(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return buf->buf.handle;
}

static void *
kernelBuffer(struct _MMBuffer *mb)
{
    DRMBuffer *buf = containerOf(mb, DRMBuffer, mb);

    return &buf->buf;
}

static void
destroy(MMManager * mm)
{
    DRMManager *man = containerOf(mm, DRMManager, mm);
    int i;

    for (i = 0; i < DRM_BO_MEM_TYPES; ++i) {
	if (man->initialized[i])
	    takeDownMemType(mm, i);
    }

    free(man);
}

static struct _MMFence *
createFence(struct _MMManager *mm,
	    unsigned class, unsigned type, unsigned flags)
{
    DRMManager *man = containerOf(mm, DRMManager, mm);
    DRMFence *dFence;
    int ret;

    dFence = (DRMFence *) malloc(sizeof(*dFence));
    if (!dFence)
	return NULL;

    ret = drmFenceCreate(man->drmFD, flags, class, type, &dFence->fence);
    if (ret) {
	free(dFence);
	return NULL;
    }

    dFence->mf.refCount = 1;
    dFence->mf.man = mm;

    return &dFence->mf;
}

/*
static void
fenceDestroy(struct _MMFence *mf)
{
    DRMFence *dFence = containerOf(mf, DRMFence, mf);
    DRMManager *man = containerOf(mf->man, DRMManager, mm);
    int ret;

    ret = drmFenceDestroy(man->drmFD, &dFence->fence);
    assert(ret == 0);
    free(dFence);
}
*/

static int
fenceEmit(struct _MMFence *mf, unsigned fence_class,
	  unsigned type, unsigned flags)
{
    DRMFence *dFence = containerOf(mf, DRMFence, mf);
    DRMManager *man = containerOf(mf->man, DRMManager, mm);
    int ret;

    dFence->fence.fence_class = fence_class;
    return drmFenceEmit(man->drmFD, flags, &dFence->fence, type);
}

static void
fenceFlush(struct _MMFence *mf, unsigned flushMask)
{
    DRMFence *dFence = containerOf(mf, DRMFence, mf);
    DRMManager *man = containerOf(mf->man, DRMManager, mm);
    int ret;

    ret = drmFenceFlush(man->drmFD, &dFence->fence, flushMask);
    assert(ret == 0);
}

static int
fenceSignaled(struct _MMFence *mf, unsigned flushMask)
{
    DRMFence *dFence = containerOf(mf, DRMFence, mf);
    DRMManager *man = containerOf(mf->man, DRMManager, mm);
    int signaled;
    int ret;

    ret = drmFenceSignaled(man->drmFD, &dFence->fence, flushMask, &signaled);
    assert(ret == 0);
    return signaled;
}

static int
fenceWait(struct _MMFence *mf, unsigned flushMask, unsigned flags)
{
    DRMFence *dFence = containerOf(mf, DRMFence, mf);
    DRMManager *man = containerOf(mf->man, DRMManager, mm);

    return drmFenceWait(man->drmFD, flags, &dFence->fence, flushMask);
}

MMManager *
mmCreateDRM(int drmFD)
{
    DRMManager *man = (DRMManager *) calloc(sizeof(*man), 1);
    MMManager *mm;

    if (!man)
	return NULL;

    man->drmFD = drmFD;
    mm = &man->mm;
    mm->initMemType = initMemType;
    mm->takeDownMemType = takeDownMemType;
    mm->lock = lock;
    mm->unLock = unLock;
    mm->createBuf = createBuf;
    mm->createUserBuf = createUserBuf;
    mm->destroyBuf = destroyBuf;
    mm->mapBuf = mapBuf;
    mm->unMapBuf = unMapBuf;
    mm->validateBuffer = validateBuffer;

    mm->bufOffset = bufOffset;
    mm->bufFlags = bufFlags;
    mm->bufMask = bufMask;
    mm->bufVirtual = bufVirtual;
    mm->bufSize = bufSize;
    mm->bufHandle = bufHandle;
    mm->kernelBuffer = kernelBuffer;

    mm->fenceEmit = fenceEmit;
    mm->fenceFlush = fenceFlush;
    mm->fenceSignaled = fenceSignaled;
    mm->fenceWait = fenceWait;
    mm->fenceError = NULL;
    /* mm->fenceDestroy = fenceDestroy; */
    mm->destroy = destroy;
    return mm;

}
