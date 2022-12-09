/*==========================================================*/
/*							LISTS							*/
/*==========================================================*/
#pragma once
#include "log.h"
#include "core.h"

// To loop over all list elements:
#ifndef list_foreach
#define list_foreach(_list, _node) \
for ((_node) = (_list)->next; (list_t*)(_list) != (list_t*)(_node); (_node) = (_node)->next )
#endif

/* Simple signly linked list */

typedef struct lifo_s {
	struct lifo_s* next;
} lifo_t;

/* Signly linked list with cached pointer to tail node */

typedef struct slist_s {
	lifo_t;
	lifo_t* tail;
} slist_t;

/* Simple doubly linked list */

typedef struct listnode_s {
	struct listnode_s* next;
	struct listnode_s* prev;
} list_t;

static inline void lifo_init(lifo_t* list) { list->next = NULL; }

static inline bool lifo_empty(lifo_t* list) { return list->next == NULL; }

static inline void lifo_push(lifo_t* list, lifo_t* node)
{
	node->next = list->next;
	list->next = node;
}

static inline lifo_t* lifo_pop(lifo_t* list)
{
	lifo_t* first = list->next;
	if (first)
		list->next = first->next;
	return first;
}

static inline void slist_init(slist_t* list)
{ list->next = NULL; list->tail = list; }

static inline bool slist_empty(slist_t* list)
{ return list->next == NULL; }

static inline void slist_push_front(slist_t* list, lifo_t* node)
{
	if (lifo_empty(list))
		list->tail = node;

	lifo_push(list, node);
}

static inline lifo_t* slist_pop_front(slist_t* list)
{
	lifo_t* node = lifo_pop(list);
	if (lifo_empty(list))
		list->tail = list;
	return node;
}

static inline void slist_add_tail(slist_t* list, lifo_t* node)
{
	TC_ASSERT(node->next == NULL);
	list->tail->next = node;
	list->tail = node;
}

static inline void list_init(list_t* list) 
{ list->prev = list; list->next = list; }

static inline bool list_empty(list_t* list) 
{ return list->next == list && list->prev == list; }

static inline void list_add(list_t* list, list_t* node) 
{
	node->prev = list;
	node->next = list->next;
	list->next->prev = node;
	list->next = node;
}

static inline void list_add_tail(list_t* list, list_t* node) 
{
	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;
}

static inline void list_remove(list_t* node) 
{
	node->next->prev = node->prev;
	node->prev->next = node->next;
}

static inline list_t* list_first(list_t* list) { return list->next; }

static inline list_t* list_last(list_t* list) { return list->prev; }

static inline list_t* list_pop(list_t* list) 
{
	list_t* first = NULL;
	if (!list_empty(list)) {
		first = list->next;
		list_remove(first);
	}
	return first;
}
