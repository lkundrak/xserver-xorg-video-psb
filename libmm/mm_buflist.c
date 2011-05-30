#include "mm_defines.h"
#include "mm_interface.h"

typedef struct _MMCoreList
{
    unsigned numTarget;
    unsigned numCurrent;
    unsigned numOnList;
    MMListHead list;
    MMListHead free;
} MMCoreList;

typedef struct _MMCoreNode
{
    MMListHead head;
    void *item;
    unsigned long arg0;
    unsigned long arg1;
} MMCoreNode;

static int
mmAdjustListNodes(MMCoreList * list)
{
    MMCoreNode *node;
    MMListHead *l;
    int ret = 0;

    while (list->numCurrent < list->numTarget) {
	node = (MMCoreNode *) malloc(sizeof(*node));
	if (!node) {
	    ret = -ENOMEM;
	    break;
	}
	list->numCurrent++;
	mmListAdd(&node->head, &list->free);
    }

    while (list->numCurrent > list->numTarget) {
	l = list->free.next;
	if (l == &list->free)
	    break;
	mmListDel(l);
	node = mmListEntry(l, MMCoreNode, head);
	free(node);
	list->numCurrent--;
    }
    return ret;
}

void
mmFreeList(MMCoreList * list)
{
    MMCoreNode *node;
    MMListHead *l;

    l = list->list.next;
    while (l != &list->list) {
	mmListDel(l);
	node = mmListEntry(l, MMCoreNode, head);
	free(node);
	l = list->free.next;
	list->numCurrent--;
	list->numOnList--;
    }

    l = list->free.next;
    while (l != &list->free) {
	mmListDel(l);
	node = mmListEntry(l, MMCoreNode, head);
	free(node);
	l = list->free.next;
	list->numCurrent--;
    }
    free(list);
}

int
mmResetList(MMCoreList * list)
{

    MMListHead *l;
    int ret;

    ret = mmAdjustListNodes(list);
    if (ret)
	return ret;

    l = list->list.next;
    while (l != &list->list) {
	mmListDel(l);
	mmListAdd(l, &list->free);
	list->numOnList--;
	l = list->list.next;
    }
    return mmAdjustListNodes(list);
}

static MMCoreNode *
mmAddListItem(MMCoreList * list, void *item,
	      unsigned long arg0, unsigned long arg1)
{
    MMCoreNode *node;
    MMListHead *l;

    l = list->free.next;
    if (l == &list->free) {
	node = (MMCoreNode *) malloc(sizeof(*node));
	if (!node) {
	    return NULL;
	}
	list->numCurrent++;
    } else {
	mmListDel(l);
	node = mmListEntry(l, MMCoreNode, head);
    }
    node->item = item;
    node->arg0 = arg0;
    node->arg1 = arg1;
    mmListAdd(&node->head, &list->list);
    list->numOnList++;
    return node;
}

void *
mmListIterator(MMCoreList * list)
{
    void *ret = list->list.next;

    if (ret == &list->list)
	return NULL;
    return ret;
}

void *
mmListNext(MMCoreList * list, void *iterator)
{
    void *ret;

    MMListHead *l = (MMListHead *) iterator;

    ret = l->next;
    if (ret == &list->list)
	return NULL;
    return ret;
}

void *
mmListBuf(void *iterator)
{
    MMCoreNode *node;
    MMListHead *l = (MMListHead *) iterator;

    node = mmListEntry(l, MMCoreNode, head);

    return node->item;
}

MMCoreList *
mmCreateList(int numTarget)
{
    MMCoreList *list;
    int ret;

    list = (MMCoreList *) malloc(sizeof(*list));
    if (!list)
	return NULL;

    mmInitListHead(&list->list);
    mmInitListHead(&list->free);
    list->numTarget = numTarget;
    list->numCurrent = 0;
    list->numOnList = 0;
    ret = mmAdjustListNodes(list);
    if (ret) {
	mmFreeList(list);
	return NULL;
    }
    return list;
}

void *
mmAddValidateItem(MMCoreList * list, void *item, unsigned flags,
		  unsigned mask, int *newItem)
{
    MMCoreNode *node, *cur;
    MMListHead *l;

    *newItem = 0;
    cur = NULL;

    for (l = list->list.next; l != &list->list; l = l->next) {
	node = mmListEntry(l, MMCoreNode, head);
	if (node->item == item) {
	    cur = node;
	    break;
	}
    }
    if (!cur) {
	cur = mmAddListItem(list, item, flags, mask);
	if (!cur) {
	    return NULL;
	}
	*newItem = 1;
	cur->arg0 = flags;
	cur->arg1 = mask;
    } else {
	unsigned memMask = (cur->arg1 | mask) & MM_MASK_MEM;
	unsigned memFlags = cur->arg0 & flags & memMask;

	if (!memFlags) {
	    return NULL;
	}
	if (mask & cur->arg1 & ~MM_MASK_MEM & (cur->arg0 ^ flags)) {
	    return NULL;
	}
	cur->arg1 |= mask;
	cur->arg0 =
	    memFlags | ((cur->arg0 | flags) & cur->arg1 & ~MM_MASK_MEM);
    }
    return (void *)cur;
}
