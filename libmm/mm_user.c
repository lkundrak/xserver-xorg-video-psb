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

#include "mm_defines.h"
#include "mm_interface.h"

#define MM_NUM_MEMTYPES 8
#define MM_NUM_FENCE_CLASSES 2

typedef struct _UserFence
{
    struct _MMFence mf;
    MMListHead ring;
    unsigned sequence;
    unsigned nativeType;
    unsigned signalPrevious;
    unsigned type;
    unsigned submittedFlush;
    unsigned flushMask;
    unsigned class;
    unsigned signaled;
    unsigned hwError;
} UserFence;

typedef struct _UserFenceClassMan
{
    MMListHead ring;
    unsigned pendingFlush;
    int pendingExeFlush;
    unsigned lastExeSequence;
    unsigned exeFlushSequence;
} UserFenceClassMan;

typedef struct _UserMan
{
    MMHead head;
    MMListHead lru;
    MMListHead pinned;
} UserMan;

typedef struct _UserManager
{
    MMManager mm;
    MMDriver *driver;
    MMListHead unfenced;
    UserFenceClassMan fences[MM_NUM_FENCE_CLASSES];
    UserMan managers[MM_NUM_MEMTYPES];
} UserManager;

typedef struct _UserSignal
{
    MMSignal signal;
    UserManager *man;
} UserSignal;

void
mmFenceSignal(struct _MMSignal *signal, unsigned class,
	      unsigned type, unsigned sequence, unsigned error,
	      unsigned errorType)
{
    UserSignal *uSig = containerOf(signal, UserSignal, signal);
    UserManager *man = uSig->man;
    MMListHead *head;
    UserFenceClassMan *fc = &man->fences[class];
    MMListHead *list, *next;
    MMDriver *driver = man->driver;
    UserFence *fence;
    unsigned relevant;
    unsigned diff;
    int isExe = type & MM_FENCE_TYPE_EXE;
    int geLastExe;
    int found;

    diff = (sequence - fc->exeFlushSequence) & driver->sequenceMask;
    if (fc->pendingExeFlush && isExe && diff < driver->wrapDiff)
	fc->pendingExeFlush = 0;

    diff = (sequence - fc->lastExeSequence) & driver->sequenceMask;
    geLastExe = diff < driver->wrapDiff;

    if (geLastExe)
	fc->pendingFlush &= ~type;

    if (isExe && geLastExe) {
	fc->lastExeSequence = sequence;
    }

    if (mmListEmpty(&fc->ring))
	return;

    mmListForEach(list, &fc->ring) {
	fence = mmListEntry(list, UserFence, ring);
	diff = (sequence - fence->sequence) & driver->sequenceMask;
	if (diff > driver->wrapDiff) {
	    found = 1;
	    break;
	}
    }

    head = (found) ? &fence->ring : &fc->ring;
    for (list = head->prev, next = list->prev;
	 list != head && list != &fc->ring; list = next, next = list->prev) {

	fence = mmListEntry(list, UserFence, ring);
	type |= fence->nativeType;
	relevant = type & fence->type;

	if ((fence->signaled | relevant) != fence->signaled) {
	    fence->signaled |= relevant;
	    fence->submittedFlush |= relevant;
	}

	relevant = fence->flushMask &
	    ~(fence->signaled | fence->submittedFlush);

	if (relevant) {
	    fc->pendingFlush |= relevant;
	    fence->submittedFlush = fence->flushMask;
	}

	if (!(fence->type & ~fence->signaled)) {
	    mmListDelInit(&fence->ring);
	}
	type |= fence->signalPrevious;
    }
}

static int
fenceEmit(struct _MMFence *mf, unsigned class, unsigned type, unsigned flags)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);
    UserManager *man = containerOf(mf->man, UserManager, mm);
    UserFenceClassMan *fc = &man->fences[class];
    unsigned sequence;
    unsigned nativeType;
    unsigned signalPrevious;
    int ret;

    ret = man->driver->emit(man->driver, class, type, flags,
			    &sequence, &nativeType, &signalPrevious);
    if (ret)
	return ret;

    mmListDelInit(&uFence->ring);
    uFence->submittedFlush = 0;
    uFence->flushMask = 0;
    uFence->signaled = 0;
    uFence->class = class;
    uFence->sequence = sequence;
    uFence->nativeType = nativeType;
    uFence->signalPrevious = signalPrevious;
    mmListAddTail(&uFence->ring, &fc->ring);
    return 0;
}

struct _MMFence *
fenceCreate(struct _MMManager *mm,
	    unsigned class, unsigned type, unsigned flags)
{
    UserManager *man = containerOf(mm, UserManager, mm);
    UserFence *uFence;
    UserFenceClassMan *fc = &man->fences[class];
    int ret;

    uFence = (UserFence *) malloc(sizeof(*uFence));
    if (!uFence)
	return NULL;

    uFence->mf.man = mm;
    uFence->mf.refCount = 1;
    mmInitListHead(&uFence->ring);
    uFence->submittedFlush = 0;
    uFence->flushMask = 0;
    uFence->signaled = 0;
    uFence->class = class;
    if (flags & MM_FENCE_FLAG_EMIT) {
	ret = man->driver->emit(man->driver, class, type, flags,
				&uFence->sequence, &uFence->nativeType,
				&uFence->signalPrevious);
	mmListAddTail(&uFence->ring, &fc->ring);
	if (ret) {
	    free(uFence);
	    return NULL;
	}
    }
    return 0;
}

static void
fenceDestroy(struct _MMFence *mf)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);

    mmListDelInit(&uFence->ring);
    free(uFence);
}

static void
fenceFlush(struct _MMFence *mf, unsigned flushMask)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);
    UserManager *man = containerOf(mf->man, UserManager, mm);
    UserFenceClassMan *fc = &man->fences[uFence->class];
    MMDriver *driver = man->driver;
    unsigned diff;

    uFence->flushMask |= (flushMask & uFence->type);
    if (uFence->submittedFlush == uFence->signaled) {
	if ((uFence->type & MM_FENCE_TYPE_EXE) &&
	    !(uFence->submittedFlush & MM_FENCE_TYPE_EXE)) {

	    if (!fc->pendingExeFlush) {
		fc->exeFlushSequence = uFence->sequence;
		fc->pendingExeFlush = 1;
	    } else {
		diff =
		    (uFence->sequence -
		     fc->exeFlushSequence) & driver->sequenceMask;
		if (diff < driver->wrapDiff)
		    fc->exeFlushSequence = uFence->sequence;
	    }
	    uFence->submittedFlush |= MM_FENCE_TYPE_EXE;
	} else {
	    fc->pendingFlush |= (uFence->flushMask & ~uFence->submittedFlush);
	    uFence->submittedFlush = uFence->flushMask;
	}
    }
    man->driver->flush(man->driver, uFence->class, fc->pendingFlush);
}

static int
fenceSignaled(struct _MMFence *mf, unsigned flushMask)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);
    UserManager *man = containerOf(mf->man, UserManager, mm);
    UserFenceClassMan *fc = &man->fences[uFence->class];

    flushFence(mf, flushMask);
    return ((uFence->signaled & flushMask) == flushMask);
}

static int
fenceWait(struct _MMFence *mf, unsigned flushMask, unsigned flags)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);
    UserManager *man = containerOf(mf->man, UserManager, mm);
    UserFenceClassMan *fc = &man->fences[uFence->class];

    while (!fenceSignaled(mf, flushMask)) {
	if (flags & MM_FENCE_FLAG_WAIT_LAZY)
	    usleep(1);
	else if (flags & MM_FENCE_FLAG_WAIT_SCHED)
	    sched_yield();
    }
    return 0;
}

static unsigned
fenceError(struct _MMFence *mf)
{
    UserFence *uFence = containerOf(mf, UserFence, mf);

    return uFence->hwError;
}
