/*==========================================================*/
/*					LOCK-FREE PAGE LIFO/STACK				*/
/*==========================================================*/
#pragma once
#include "tc.h"

/* Can only be used to store 64k aligned pointers since it uses the lower 16 bits as counter/tag to track reuse in the ABA problem */

typedef struct lf_lifo_t {
	atomic_t next;
} lf_lifo_t;

static inline 
unsigned short aba_value(void* a)
{
	return (intptr_t)a & 0xffff;
}

static inline 
lf_lifo_t* lf_lifo(void* a)
{
	return (lf_lifo_t*)((intptr_t)a & ~0xffff);
}

static inline 
void lf_lifo_init(lf_lifo_t* head)
{
	atomic_init(&head->next, 0);
}

static inline 
bool lf_lifo_is_empty(lf_lifo_t* head)
{
	return lf_lifo(head->next) == NULL;
}

static inline 
lf_lifo_t* lf_lifo_push(lf_lifo_t* head, void* elem)
{
	TC_ASSERT(lf_lifo(elem) == elem); // Should be aligned address
	void* next = (void*)atomic_load(&head->next);
	do {
		lf_lifo(elem)->next = lf_lifo(next);
		void* newhead = (char*)lf_lifo(elem) + aba_value((char*)next + 1);
		if (CAS(head, next, newhead))
			return head;
	} while (true);
}

static inline 
void* lf_lifo_pop(lf_lifo_t* head)
{
	void* next = (void*)atomic_load(&head->next);
	do {
		lf_lifo_t* elem = lf_lifo(next);
		if (elem == NULL) return NULL;
		void* newhead = ((char*)lf_lifo(elem->next) + aba_value((char*)next + 1));
		if (CAS(head, next, newhead))
			return elem;
	} while (true);
}
