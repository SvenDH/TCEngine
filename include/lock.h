/*==========================================================*/
/*							LOCKS							*/
/*==========================================================*/
#pragma once
#include "tcatomic.h"

typedef struct lock_s { atomic_t value; } lock_t;

#define TC_LOCK(l) spin_lock(l)
#define TC_UNLOCK(l) spin_unlock(l)

//TODO: Make fiber waiting spin lock after x amount of cycles
static inline 
void spin_lock(lock_t* lock)
{
	for (;;) {
		if (!atomic_exchange(&lock->value, true, MEMORY_ACQUIRE))
			break;
		while (atomic_load(&lock->value, MEMORY_RELAXED))
			pause();
	}
}

static inline
void spin_unlock(lock_t* lock)
{
	atomic_store(&lock->value, false, MEMORY_RELEASE);
}