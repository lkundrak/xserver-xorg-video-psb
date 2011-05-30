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
/*
 * Copyright or license is not claimed on the generic linked list
 * implementation of this file.
 */

#ifndef _MM_DEFINES_H
#define _MM_DEFINES_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/errno.h>

typedef struct _MMListHead
{
    struct _MMListHead *prev;
    struct _MMListHead *next;
} MMListHead;

static inline void
mmInitListHead(MMListHead * item)
{
    item->prev = item;
    item->next = item;
}

static inline void
mmListAdd(MMListHead * item, MMListHead * list)
{
    item->prev = list;
    item->next = list->next;
    list->next->prev = item;
    list->next = item;
}

static inline void
mmListAddTail(MMListHead * item, MMListHead * list)
{
    item->next = list;
    item->prev = list->prev;
    list->prev->next = item;
    list->prev = item;
}

static inline void
mmListDel(MMListHead * item)
{
    item->prev->next = item->next;
    item->next->prev = item->prev;
}

static inline void
mmListDelInit(MMListHead * item)
{
    mmListDel(item);
    mmInitListHead(item);
}

#define containerOf(__item, __type, __field)				\
    ((__type *)(((char *) (__item)) - offsetof(__type, __field)))

#define mmListEntry(__item, __type, __field)  \
    containerOf(__item, __type, __field)

#define mmListForEach(__item, __list) \
    for((__item) = (__list)->next; (__item) != (__list); (__item) = (__item)->next)

#define mmListForEachPrev(__item, __list) \
    for((__item) = (__list)->prev; (__item) != (__list); (__item) = (__item)->prev)

#define mmListForEachSafe(__item, __next, __list)		\
        for((__item) = (__list)->next, (__next) = (__item)->next;	\
	(__item) != (__list);					\
	(__item) = (__next), (__next) = (__item)->next)

#define mmListForEachPrevSafe(__item, __prev, __list)		\
    for((__item) = (__list)->prev, (__prev) = (__item->prev);	\
	(__item) != (__list);					\
	(__item) = (__prev), (__prev) = (__item)->prev)

typedef struct _MMNode
{
    MMListHead fl_entry;
    MMListHead ml_entry;
    int free;
    unsigned long start;
    unsigned long size;
    struct _MMHead *mm;
    void *private;
} MMNode;

typedef struct _MMHead
{
    MMListHead fl_entry;
    MMListHead ml_entry;
    int initialized;
    unsigned long start;
    unsigned long size;
} MMHead;

#endif /* _MM_DEFINES_H_ */
