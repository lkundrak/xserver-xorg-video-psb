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

/*
 * Generic simple memory manager implementation. Intended to be used as a base
 * class implementation for more advanced memory managers.
 *
 * Note that the algorithm used is quite simple and there might be substantial
 * performance gains if a smarter free list is implemented. Currently it is just an
 * unordered stack of free regions. This could easily be improved if an RB-tree
 * is used instead. At least if we expect heavy fragmentation.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "mm_defines.h"

unsigned long
mm_tail_space(MMHead * mm)
{
    MMListHead *tail_node;
    MMNode *entry;

    tail_node = mm->ml_entry.prev;
    entry = mmListEntry(tail_node, MMNode, ml_entry);
    if (!entry->free)
	return 0;

    return entry->size;
}

int
mm_remove_space_from_tail(MMHead * mm, unsigned long size)
{
    MMListHead *tail_node;
    MMNode *entry;

    tail_node = mm->ml_entry.prev;
    entry = mmListEntry(tail_node, MMNode, ml_entry);
    if (!entry->free)
	return -ENOMEM;

    if (entry->size <= size)
	return -ENOMEM;

    entry->size -= size;
    return 0;
}

static int
mm_create_tail_node(MMHead * mm, unsigned long start, unsigned long size)
{
    MMNode *child;

    child = (MMNode *)
	malloc(sizeof(*child));
    if (!child)
	return -ENOMEM;

    child->free = 1;
    child->size = size;
    child->start = start;
    child->mm = mm;

    mmListAddTail(&child->ml_entry, &mm->ml_entry);
    mmListAddTail(&child->fl_entry, &mm->fl_entry);

    return 0;
}

int
mm_add_space_to_tail(MMHead * mm, unsigned long size)
{
    MMListHead *tail_node;
    MMNode *entry;

    tail_node = mm->ml_entry.prev;
    entry = mmListEntry(tail_node, MMNode, ml_entry);
    if (!entry->free) {
	return mm_create_tail_node(mm, entry->start + entry->size, size);
    }
    entry->size += size;
    return 0;
}

static MMNode *
mm_split_at_start(MMNode * parent, unsigned long size)
{
    MMNode *child;

    child = (MMNode *)
	malloc(sizeof(*child));
    if (!child)
	return NULL;

    mmInitListHead(&child->fl_entry);

    child->free = 0;
    child->size = size;
    child->start = parent->start;
    child->mm = parent->mm;

    mmListAddTail(&child->ml_entry, &parent->ml_entry);
    mmInitListHead(&child->fl_entry);

    parent->size -= size;
    parent->start += size;
    return child;
}

/*
 * Put a block. Merge with the previous and / or next block if they are free.
 * Otherwise add to the free stack.
 */

void
mm_put_block(MMNode * cur)
{

    MMHead *mm = cur->mm;
    MMListHead *cur_head = &cur->ml_entry;
    MMListHead *root_head = &mm->ml_entry;
    MMNode *prev_node = NULL;
    MMNode *next_node;
    MMNode *merged_node = NULL;
    int kill_block;

    if (cur_head->prev != root_head) {
	prev_node = mmListEntry(cur_head->prev, MMNode, ml_entry);
	if (prev_node->free) {
	    prev_node->size += cur->size;
	    merged_node = prev_node;
	}
    }
    if (cur_head->next != root_head) {
	next_node = mmListEntry(cur_head->next, MMNode, ml_entry);
	if (next_node->free) {
	    if (merged_node) {
		prev_node->size += next_node->size;
		mmListDel(&next_node->ml_entry);
		mmListDel(&next_node->fl_entry);
		free(next_node);
	    } else {
		next_node->size += cur->size;
		next_node->start = cur->start;
		merged_node = next_node;
	    }
	}
    }
    if (!merged_node) {
	cur->free = 1;
	mmListAdd(&cur->fl_entry, &mm->fl_entry);
    } else {
	mmListDel(&cur->ml_entry);
	free(cur);
    }
}

MMNode *
mm_get_block(MMNode * parent, unsigned long size, unsigned alignment)
{

    MMNode *align_splitoff = NULL;
    MMNode *child;
    unsigned tmp = 0;

    if (alignment)
	tmp = parent->start % alignment;

    if (tmp) {
	align_splitoff = mm_split_at_start(parent, alignment - tmp);
	if (!align_splitoff)
	    return NULL;
    }

    if (parent->size == size) {
	mmListDelInit(&parent->fl_entry);
	parent->free = 0;
	return parent;
    } else {
	child = mm_split_at_start(parent, size);
    }

    if (align_splitoff)
	mm_put_block(align_splitoff);

    return child;
}

MMNode *
mm_search_free(const MMHead * mm,
	       unsigned long size, unsigned alignment, int best_match)
{
    MMListHead *list;
    const MMListHead *free_stack = &mm->fl_entry;
    MMNode *entry;
    MMNode *best;
    unsigned long best_size;
    unsigned wasted;

    best = NULL;
    best_size = ~0UL;

    mmListForEach(list, free_stack) {
	entry = mmListEntry(list, MMNode, fl_entry);
	wasted = 0;

	if (entry->size < size)
	    continue;

	if (alignment) {
	    register unsigned tmp = entry->start % alignment;

	    if (tmp)
		wasted += alignment - tmp;
	}

	if (entry->size >= size + wasted) {
	    if (!best_match)
		return entry;
	    if (size < best_size) {
		best = entry;
		best_size = entry->size;
	    }
	}
    }

    return best;
}

int
mm_clean(MMHead * mm)
{
    MMListHead *head = &mm->ml_entry;

    return (head->next->next == head);
}

int
mm_init(MMHead * mm, unsigned long start, unsigned long size)
{
    mmInitListHead(&mm->ml_entry);
    mmInitListHead(&mm->fl_entry);

    return mm_create_tail_node(mm, start, size);
}

void
mm_takedown(MMHead * mm)
{
    MMListHead *bnode = mm->fl_entry.next;
    MMNode *entry;

    entry = mmListEntry(bnode, MMNode, fl_entry);

    if (entry->ml_entry.next != &mm->ml_entry ||
	entry->fl_entry.next != &mm->fl_entry) {
	/*
	 * Error here.
	 */
	return;
    }

    mmListDel(&entry->fl_entry);
    mmListDel(&entry->ml_entry);
    free(entry);
}
