/*==========================================================*/
/*					LOCK-FREE PAGE LIFO/STACK				*/
/*==========================================================*/
#pragma once
#include "log.h"
#include "types.h"

/* Can only be used to store 64k aligned pointers since it uses the lower 16 bits as counter/tag to track reuse in the ABA problem */

typedef struct lf_lifo_t {
	void* next;
} lf_lifo_t;

static inline 
unsigned short aba_value(void* a) {
	return (intptr_t)a & 0xffff;
}

static inline 
lf_lifo_t* lf_lifo(void* a) {
	return (lf_lifo_t*)((intptr_t)a & ~0xffff);
}

static inline 
void lf_lifo_init(lf_lifo_t* head) { 
	head->next = NULL;
}

static inline 
bool lf_lifo_is_empty(lf_lifo_t* head) {
	return lf_lifo(head->next) == NULL;
}

static inline 
lf_lifo_t* lf_lifo_push(lf_lifo_t* head, void* elem) {
	TC_ASSERT(lf_lifo(elem) == elem); // Should be aligned address
	do {
		void* tail = head->next;
		lf_lifo(elem)->next = tail;
		void* newhead = (char*)elem + aba_value((char*)tail + 1);
		if (CAS(&head->next, tail, newhead))
			return head;
	} while (true);
}

static inline 
void* lf_lifo_pop(lf_lifo_t* head) {
	do {
		void* tail = head->next;
		lf_lifo_t* elem = lf_lifo(tail);
		if (elem == NULL) return NULL;
		void* newhead = ((char*)lf_lifo(elem->next) + aba_value(tail));
		if (CAS(&head->next, tail, newhead))
			return elem;
	} while (true);
}
