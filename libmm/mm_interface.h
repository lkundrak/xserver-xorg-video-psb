/**************************************************************************
 *
 * Copyright 2007 Tungsten Graphics, Inc., Cedar Park, TX., USA.
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

#ifndef _MM_INTERFACE_
#define _MM_INTERFACE_

/*
 * This is the common interface for xorg drm-type memory managers.
 */

#define HAVE_XF86MM_H
#ifdef HAVE_XF86MM_H
#include <xf86mm.h>

#define MM_FENCE_TYPE_EXE        DRM_FENCE_TYPE_EXE

#define MM_FENCE_FLAG_EMIT       DRM_FENCE_FLAG_EMIT
#define MM_FENCE_FLAG_WAIT_LAZY  DRM_FENCE_FLAG_WAIT_LAZY

#define MM_MEM_VRAM              DRM_BO_MEM_VRAM
#define MM_MEM_TT                DRM_BO_MEM_TT
#define MM_MEM_PRIV0             DRM_BO_MEM_PRIV0

#define MM_MASK_MEM              DRM_BO_MASK_MEM
#define MM_FLAG_MEM_VRAM         DRM_BO_FLAG_MEM_VRAM
#define MM_FLAG_MEM_TT           DRM_BO_FLAG_MEM_TT
#define MM_FLAG_MEM_PRIV0        DRM_BO_FLAG_MEM_PRIV0
#define MM_FLAG_MEM_LOCAL        DRM_BO_FLAG_MEM_LOCAL

#define MM_FLAG_SHAREABLE        DRM_BO_FLAG_SHAREABLE
#define MM_FLAG_MAPPABLE         DRM_BO_FLAG_MAPPABLE
#define MM_FLAG_READ             DRM_BO_FLAG_READ
#define MM_FLAG_WRITE            DRM_BO_FLAG_WRITE
#define MM_FLAG_NO_EVICT         DRM_BO_FLAG_NO_EVICT
#define MM_FLAG_CACHED           DRM_BO_FLAG_CACHED
#define MM_FLAG_FORCE_CACHING    DRM_BO_FLAG_FORCE_CACHING

#define MM_HINT_DONT_FENCE       DRM_BO_HINT_DONT_FENCE
#else

#define MM_MASK_MEM              0xFF000000

#define MM_FENCE_TYPE_EXE        0x00000001

#define MM_FENCE_FLAG_EMIT       0x00000001
#define MM_FENCE_FLAG_WAIT_LAZY  0x00000004

#endif

#define MM_FENCE_FLAG_WAIT_SCHED 0x00800000

struct _MMBuffer
{
    struct _MMManager *man;
};
struct _MMFence
{
    int refCount;
    struct _MMManager *man;
};
struct _MMBufList
{
    struct _MMManager *man;
};

/*
 * The manager class.
 */

typedef struct _MMManager
{
    int (*initMemType) (struct _MMManager * man, unsigned long pOffset,
			unsigned long pSize, unsigned memType);
    int (*takeDownMemType) (struct _MMManager * man, int memType);
    int (*lock) (struct _MMManager * man, int memType, int lockBM,
		 int ignoreEvict);
    int (*unLock) (struct _MMManager * man, int memType, int unlockBM);
    struct _MMBuffer *(*createBuf) (struct _MMManager * man,
				    unsigned long size,
				    unsigned pageAlignment, uint64_t flags,
				    unsigned hint);
    struct _MMBuffer *(*createUserBuf) (struct _MMManager * man,
					void *start,
					unsigned long size,
					uint64_t flags,
					unsigned hint);
    void (*destroyBuf) (struct _MMBuffer * buf);
    int (*mapBuf) (struct _MMBuffer * buf, unsigned mapFlags,
		   unsigned mapHints);
    int (*unMapBuf) (struct _MMBuffer * buf);
    void (*destroy) (struct _MMManager * man);

    /*
     * Buffer methods.
     */

    unsigned long (*bufOffset) (struct _MMBuffer * buf);
    unsigned long (*bufFlags) (struct _MMBuffer * buf);
    unsigned long (*bufMask) (struct _MMBuffer * buf);
    void *(*bufVirtual) (struct _MMBuffer * buf);
    unsigned long (*bufSize) (struct _MMBuffer * buf);
    unsigned (*bufHandle) (struct _MMBuffer * buf);
    void *(*kernelBuffer) (struct _MMBuffer * buf);
    int (*validateBuffer) (struct _MMBuffer * buf, uint64_t flags,
			   uint64_t mask, unsigned hint);

    /*
     * Fence methods.
     */

    int (*fenceEmit) (struct _MMFence * mf, unsigned fence_class,
		      unsigned type, unsigned flags);
    struct _MMFence *(*createFence) (struct _MMManager * mm, unsigned class,
				     unsigned type, unsigned flags);
    void (*fenceFlush) (struct _MMFence * mf, unsigned flushMask);
    int (*fenceSignaled) (struct _MMFence * mf, unsigned flushMask);
    int (*fenceWait) (struct _MMFence * mf, unsigned flushMask,
		      unsigned flags);
    unsigned (*fenceError) (struct _MMFence * mf);
    /* void (*fenceDestroy) (struct _MMFence * mf); */
} MMManager;

/*
 * Short buffer method versions.
 */

static inline unsigned long
mmBufOffset(struct _MMBuffer *buf)
{
    return buf->man->bufOffset(buf);
}

static inline unsigned long
mmBufFlags(struct _MMBuffer *buf)
{
    return buf->man->bufFlags(buf);
}

static inline unsigned long
mmBufMask(struct _MMBuffer *buf)
{
    return buf->man->bufMask(buf);
}

static inline void *
mmBufVirtual(struct _MMBuffer *buf)
{
    return buf->man->bufVirtual(buf);
}

static inline unsigned long
mmBufSize(struct _MMBuffer *buf)
{
    return buf->man->bufSize(buf);
}

static inline void
mmBufDestroy(struct _MMBuffer *buf)
{
    return buf->man->destroyBuf(buf);
}

static inline void *
mmKernelBuf(struct _MMBuffer *buf)
{
    if (buf->man->kernelBuffer)
	return buf->man->kernelBuffer(buf);
    else
	return NULL;
}

/*
 * Short fence method versions.
 */

static inline int
mmFenceEmit(struct _MMFence *mf, unsigned class, unsigned type,
	    unsigned flags)
{
    return mf->man->fenceEmit(mf, class, type, flags);
}

/*
static inline void
mmFenceUnReference(struct _MMFence **mfP)
{
    struct _MMFence *mf = *mfP;

    if (--mf->refCount == 0) {
	mf->man->fenceDestroy(mf);
    }
    *mfP = NULL;
}
*/

static inline struct _MMFence *
mmFenceReference(struct _MMFence *mf)
{
    mf->refCount++;
    return mf;
}

static inline void
mmFenceFlush(struct _MMFence *mf, unsigned flushMask)
{
    mf->man->fenceFlush(mf, flushMask);
}

static inline int
mmFenceSignaled(struct _MMFence *mf, unsigned flushMask)
{
    return mf->man->fenceSignaled(mf, flushMask);
}

static inline int
mmFenceWait(struct _MMFence *mf, unsigned flushMask, unsigned flags)
{
    return mf->man->fenceWait(mf, flushMask, flags);
}

static inline unsigned
mmFenceError(struct _MMFence *mf)
{
    return mf->man->fenceError(mf);
}

/*
 * The currently available managers:
 */

/*
 * For the drm manager, no kickout function needs not be implemented, since
 * DRM will handle this automatically.
 */

extern MMManager *mmCreateDRM(int drmFD);

/*
 * Note that, while the manager is device context, the Xorg
 * AGP interface is screen context and thus needs a screen to
 * work with. In a multihead environment, use primary for all
 * memory manager manipulations.
 */

extern MMManager *mmCreateXorg(int screen);

/*
 * Intended as a base class. The function should read back the buffer
 * content of a previously kicked out buffer in whatever way is 
 * suitable by the driver. 
 *
 * The readback function should return 0 on success and may destroy
 * the saved buffer contents after it's first call.
 *
 * Typically private data can be placed in a struct containing the
 * MMKickOut struct, and is accessed using containerOf() from the
 * readback function. Think derived class.
 */
typedef struct _MMKickOut
{
    int (*readBack) (struct _MMKickOut * kickOut, struct _MMBuffer * buf);
} MMKickOut;

typedef struct _MMSignal
{
    void (*signal) (struct _MMSignal * signal, unsigned class, unsigned type,
		    unsigned sequence);
} MMSignal;

typedef struct _MMDriver
{
    unsigned sequenceMask;
    unsigned wrapDiff;
    unsigned oldDiff;

    /*
     * This function should copy the relevant buffer content to a safe 
     * location. It should then return a pointer to a valid MMKickOut struct or
     * NULL on failure. 
     */

    struct _MMKickOut *(*kickOutFunc) (struct _MMDriver * driver,
				       struct _MMBuffer * buf);

    /*
     * Fencing;
     */

    int (*emit) (struct _MMDriver * driver, unsigned class, unsigned type,
		 unsigned flags,
		 unsigned *sequence, unsigned *nativeType,
		 unsigned *signalPrevious);
    void (*flush) (struct _MMDriver * driver, unsigned class,
		   unsigned pendingFlush);
} MMDriver;

#endif
