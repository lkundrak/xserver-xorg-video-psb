/**************************************************************************
 * Copyright (c) Intel Corp. 2007.
 * Copyright (c) Tungsten Graphics Inc., Cedar Park TX. USA.
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
 * psb_ioctl.c - Poulsbo device-specific IOCTL interface.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <xf86drm.h>
#include <psb_drm.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "psb_driver.h"
#include "psb_ioctl.h"

#define PSB_2D_RELOC_BUFFER_SIZE (4096*16)
#define PSB_2D_RELOC_OFFS (4096*4)

typedef struct _drmBONode
{
    drmMMListHead head;
    drmBO *buf;
    struct drm_bo_op_arg bo_arg;
    uint64_t arg0;
    uint64_t arg1;
} drmBONode;

static int
psbAddValidateItem(drmBOList * list, drmBO * buf, uint64_t flags,
		   uint64_t mask, int *itemLoc, struct _drmBONode **pNode);


static unsigned
psbTimeDiff(struct timeval *now, struct timeval *then)
{
    long long val;

    val = now->tv_sec - then->tv_sec;
    val *= 1000000LL;
    val += now->tv_usec;
    val -= then->tv_usec;
    if (val < 1LL)
	val = 1LL;

    return (unsigned)val;
}

static int
drmAdjustListNodes(drmBOList * list)
{
    drmBONode *node;
    drmMMListHead *l;
    int ret = 0;

    while (list->numCurrent < list->numTarget) {
	node = (drmBONode *) malloc(sizeof(*node));
	if (!node) {
	    ret = -ENOMEM;
	    break;
	}
	list->numCurrent++;
	DRMLISTADD(&node->head, &list->free);
    }

    while (list->numCurrent > list->numTarget) {
	l = list->free.next;
	if (l == &list->free)
	    break;
	DRMLISTDEL(l);
	node = DRMLISTENTRY(drmBONode, l, head);
	free(node);
	list->numCurrent--;
    }
    return ret;
}

static int
drmBOCreateList(int numTarget, drmBOList * list)
{
    DRMINITLISTHEAD(&list->list);
    DRMINITLISTHEAD(&list->free);
    list->numTarget = numTarget;
    list->numCurrent = 0;
    list->numOnList = 0;
    return drmAdjustListNodes(list);
}

static int
drmBOResetList(drmBOList * list)
{
    drmMMListHead *l;
    int ret;

    ret = drmAdjustListNodes(list);
    if (ret)
	return ret;

    l = list->list.next;
    while (l != &list->list) {
	DRMLISTDEL(l);
	DRMLISTADD(l, &list->free);
	list->numOnList--;
	l = list->list.next;
    }
    return drmAdjustListNodes(list);
}

static void
drmBOFreeList(drmBOList * list)
{
    drmBONode *node;
    drmMMListHead *l;

    l = list->list.next;
    while (l != &list->list) {
	DRMLISTDEL(l);
	node = DRMLISTENTRY(drmBONode, l, head);
	free(node);
	l = list->list.next;
	list->numCurrent--;
	list->numOnList--;
    }

    l = list->free.next;
    while (l != &list->free) {
	DRMLISTDEL(l);
	node = DRMLISTENTRY(drmBONode, l, head);
	free(node);
	l = list->free.next;
	list->numCurrent--;
    }
}

static void
psbDRMCopyReply(const struct drm_bo_info_rep *rep, drmBO * buf)
{
    buf->handle = rep->handle;
    buf->flags = rep->flags;
    buf->size = rep->size;
    buf->offset = rep->offset;
    buf->mapHandle = rep->arg_handle;
    buf->mask = rep->mask;
    buf->start = rep->buffer_start;
    buf->fenceFlags = rep->fence_flags;
    buf->replyFlags = rep->rep_flags;
    buf->pageAlignment = rep->page_alignment;
}

/*
 * This is the user-space do-it-all interface to the drm cmdbuf ioctl.
 * It allows different buffers as command- and reloc buffer. A list of
 * cliprects to apply and whether to copy the clipRect content to all
 * scanout buffers (damage = 1).
 */

int
psbDRMCmdBuf(int fd, drmBOList * list, unsigned cmdBufHandle,
	     unsigned cmdBufOffset, unsigned cmdBufSize,
	     unsigned taBufHandle, unsigned taBufOffset, unsigned taBufSize,
	     unsigned relocBufHandle, unsigned relocBufOffset,
	     unsigned numRelocs, drm_clip_rect_t * clipRects, int damage,
	     unsigned engine, unsigned fence_flags, unsigned *fence_handle)
{

    drmBONode *node;
    drmMMListHead *l;
    drm_psb_cmdbuf_arg_t ca;
    struct drm_bo_op_arg *arg, *first;
    struct drm_bo_op_req *req;
    struct drm_bo_info_rep *rep;
    uint64_t *prevNext = NULL;
    drmBO *buf;
    int ret;
    struct timeval then, now;
    Bool have_then = FALSE;

    first = NULL;

    for (l = list->list.next; l != &list->list; l = l->next) {
	node = DRMLISTENTRY(drmBONode, l, head);

	arg = &node->bo_arg;
	req = &arg->d.req;

	if (!first)
	    first = arg;

	if (prevNext)
	    *prevNext = (unsigned long)arg;

	prevNext = &arg->next;
	req->bo_req.handle = node->buf->handle;
	req->op = drm_bo_validate;
	req->bo_req.flags = node->arg0;
	req->bo_req.mask = node->arg1;
	req->bo_req.hint |= 0;
    }

    memset(&ca, 0, sizeof(ca));

    ca.buffer_list = (uint64_t) ((unsigned long)first);
    ca.clip_rects = (uint64_t) ((unsigned long)clipRects);
    ca.ta_handle = taBufHandle;
    ca.ta_offset = taBufOffset;
    ca.ta_size = taBufSize;
    ca.cmdbuf_handle = cmdBufHandle;
    ca.cmdbuf_offset = cmdBufOffset;
    ca.cmdbuf_size = cmdBufSize;
    ca.oom_size = 0;
    ca.reloc_handle = relocBufHandle;
    ca.reloc_offset = relocBufOffset;
    ca.num_relocs = numRelocs;
    ca.engine = engine;
    ca.fence_flags = fence_flags;
    ca.damage = damage;

    if (gettimeofday(&then, NULL))
	FatalError("Gettimeofday error.\n");

    /*
     * X server Signals will clobber the kernel time out mechanism.
     * we need a user-space timeout as well.
     */

    do {
	ret = drmCommandWriteRead(fd, DRM_PSB_CMDBUF, &ca, sizeof(ca));
	if (ret == -EAGAIN) {
	    if (!have_then) {
		if (gettimeofday(&then, NULL))
		    FatalError("Gettimeofday error.\n");
		have_then = TRUE;
	    }
	    if (gettimeofday(&now, NULL))
		FatalError("Gettimeofday error.\n");
	}
    } while (ret == -EAGAIN && psbTimeDiff(&now, &then) < PSB_TIMEOUT_USEC);

    if (ret)
	return ret;

    for (l = list->list.next; l != &list->list; l = l->next) {
	node = DRMLISTENTRY(drmBONode, l, head);
	arg = &node->bo_arg;
	rep = &arg->d.rep.bo_info;

	if (!arg->handled) {
	    return -EFAULT;
	}
	if (arg->d.rep.ret) {
	    return arg->d.rep.ret;
	}

	buf = node->buf;
	psbDRMCopyReply(rep, buf);
    }

    return 0;
}

Bool
psbInit2DBuffer(int fd, Psb2DBufferPtr buf)
{
    int ret;
    void *addr;
    struct _drmBONode *node;
    struct drm_bo_info_req *req;

    ret = drmBOCreate(fd, PSB_2D_RELOC_BUFFER_SIZE, 0, NULL,
		      DRM_BO_FLAG_MEM_LOCAL | DRM_BO_FLAG_EXE |
		      DRM_BO_FLAG_READ, DRM_BO_HINT_DONT_FENCE, &buf->buffer);
    if (ret)
	return FALSE;

    ret = drmBOMap(fd, &buf->buffer, DRM_BO_FLAG_WRITE, 0, &addr);
    buf->startCmd = addr;
    drmBOUnmap(fd, &buf->buffer);

    ret = drmBOCreateList(10, &buf->bufferList);

    if (ret)
	return FALSE;

    buf->fd = fd;
    buf->curCmd = buf->startCmd;
    buf->startReloc = (struct drm_psb_reloc *)
	((unsigned long)buf->startCmd + PSB_2D_RELOC_OFFS);
    buf->curReloc = buf->startReloc;
    buf->maxRelocs = (PSB_2D_RELOC_BUFFER_SIZE - PSB_2D_RELOC_OFFS) /
	sizeof(struct drm_psb_reloc);

    ret = psbAddValidateItem(&buf->bufferList, &buf->buffer, 0, 0,
			     &buf->myValidateIndex, &node);
    if (ret)
	return FALSE;

    req = &node->bo_arg.d.req.bo_req;
    req->hint = DRM_BO_HINT_PRESUMED_OFFSET;
    req->presumed_offset = 0; /* Local memory */

    return TRUE;
}

void
psbTakedown2DBuffer(int fd, Psb2DBufferPtr buf)
{
    drmBOFreeList(&buf->bufferList);
    (void)drmBOUnreference(fd, &buf->buffer);
}

static drmBONode *
psbAddListItem(drmBOList * list, drmBO * item, uint64_t arg0, uint64_t arg1)
{
    drmBONode *node;
    drmMMListHead *l;

    l = list->free.next;
    if (l == &list->free) {
	node = (drmBONode *) malloc(sizeof(*node));
	if (!node) {
	    return NULL;
	}
	list->numCurrent++;
    } else {
	DRMLISTDEL(l);
	node = DRMLISTENTRY(drmBONode, l, head);
    }
    memset(&node->bo_arg, 0, sizeof(node->bo_arg));
    node->buf = item;
    node->arg0 = arg0;
    node->arg1 = arg1;
    DRMLISTADDTAIL(&node->head, &list->list);
    list->numOnList++;
    return node;
}

/*
 * Should really go into libdrm. Slightly different semantics than
 * the libdrm counterpart.
 */

static int
psbAddValidateItem(drmBOList * list, drmBO * buf, uint64_t flags,
		   uint64_t mask, int *itemLoc, struct _drmBONode **pNode)
{
    drmBONode *node, *cur;
    drmMMListHead *l;
    int count = 0;

    cur = NULL;

    for (l = list->list.next; l != &list->list; l = l->next) {
	node = DRMLISTENTRY(drmBONode, l, head);
	if (node->buf == buf) {
	    cur = node;
	    break;
	}
	count++;
    }
    if (!cur) {
	cur = psbAddListItem(list, buf, flags, mask);
	if (!cur)
	    return -ENOMEM;

	cur->arg0 = flags;
	cur->arg1 = mask;
    } else {
	uint64_t memMask = (cur->arg1 | mask) & DRM_BO_MASK_MEM;
	uint64_t memFlags = cur->arg0 & flags & memMask;

	if (!memFlags && ((mask & DRM_BO_MASK_MEM) == DRM_BO_MASK_MEM))
	    return -EINVAL;

	if (mask & cur->arg1 & ~DRM_BO_MASK_MEM & (cur->arg0 ^ flags))
	    return -EINVAL;

	cur->arg1 |= mask;
	cur->arg0 = memFlags | ((cur->arg0 | flags) &
				cur->arg1 & ~DRM_BO_MASK_MEM);
    }
    *itemLoc = count;
    *pNode = cur;
    return 0;
}

int
psbRelocOffset2D(Psb2DBufferPtr buf, unsigned delta, drmBO * buffer,
		 uint64_t flags, uint64_t mask)
{
    struct drm_psb_reloc *reloc = buf->curReloc;
    struct _drmBONode *node;
    int ret;
    int itemLoc;
    struct drm_bo_info_req *req;

    ret = psbAddValidateItem(&buf->bufferList, buffer, flags, mask, &itemLoc,
			     &node);
    if (ret) {
	FatalError("Add validate failed %s\n", strerror(-ret));
    }

    req = &node->bo_arg.d.req.bo_req;

    if (!(req->hint &  DRM_BO_HINT_PRESUMED_OFFSET)) {
	req->presumed_offset = buffer->offset;
	req->hint = DRM_BO_HINT_PRESUMED_OFFSET;
    }

    *buf->curCmd = (req->presumed_offset + delta) & 0x0FFFFFFF;
    reloc->reloc_op = PSB_RELOC_OP_2D_OFFSET;
    reloc->where = (buf->curCmd - buf->startCmd);
    reloc->buffer = itemLoc;
    reloc->mask = 0x0FFFFFFF;
    reloc->shift = 0;
    reloc->pre_add = delta;
    reloc->dst_buffer = buf->myValidateIndex;

    buf->curCmd++;
    buf->curReloc++;

    return 0;
}

int
psbFlush2D(Psb2DBufferPtr buf, unsigned fence_flags, unsigned *fence_handle)
{
    struct _drmBONode *node;
    struct drm_bo_info_req *req;
    int ret;

    if (buf->curCmd == buf->startCmd)
	return 0;

    ret = psbDRMCmdBuf(buf->fd, &buf->bufferList, buf->buffer.handle,
		       0, buf->curCmd - buf->startCmd,
		       0, 0, 0,
		       buf->buffer.handle, PSB_2D_RELOC_OFFS,
		       buf->curReloc - buf->startReloc, buf->clipRects, 0,
		       PSB_ENGINE_2D, fence_flags, fence_handle);

    if (ret) {
	ErrorF("Command submission ioctl failed: \"%s\".\n", strerror(-ret));
    }

    drmBOResetList(&buf->bufferList);
    buf->curCmd = buf->startCmd;
    buf->curReloc = buf->startReloc;
    ret = psbAddValidateItem(&buf->bufferList, &buf->buffer, 0, 0,
			     &buf->myValidateIndex, &node);
    if (ret) {
	ErrorF("Failed adding command buffer to validate list:"
	       " \"%s\".\n", strerror(-ret));
	goto out;
    }

    req = &node->bo_arg.d.req.bo_req;
    req->hint = DRM_BO_HINT_PRESUMED_OFFSET;
    req->presumed_offset = 0; /* Local memory */

  out:
    return ret;
}

void
psbSetStateCallback(Psb2DBufferPtr buf, PsbVolatileStateFunc *func,
		    void *arg)
{
    buf->emitVolatileState = func;
    buf->volatileStateArg = arg;
}
