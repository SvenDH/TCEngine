/*==========================================================*/
/*						LOCK-FREE QUEUE						*/
/*==========================================================*/
#pragma once
#include "tc.h"


/* This is a fixed-size (FIFO) ring buffer that allows for multiple concurrent reads and writes */

typedef struct cell_s cell_t;

typedef struct lf_queue_s {
	tc_allocator_i* base;
	size_t mask;
	ALIGNED(cell_t*, 64) buffer;
	ALIGNED(atomic_t, 64) write;
	ALIGNED(atomic_t, 64) read;
} lf_queue_t;


typedef struct cell_s {
	atomic_t sequence;
	void* data;
} cell_t;

static inline lf_queue_t* lf_queue_init(uint32_t elements, tc_allocator_i* allocator) {
	lf_queue_t* queue = (lf_queue_t*)TC_ALLOC(allocator, sizeof(lf_queue_t) + elements * sizeof(cell_t));
	queue->base = allocator;
	queue->buffer = (cell_t*)(queue + 1);
	queue->mask = elements - 1;
	TC_ASSERT((elements >= 2) && ((elements & (elements - 1)) == 0));
	for (size_t i = 0; i != elements; i++) {
		atomic_store_explicit(&queue->buffer[i].sequence, i, memory_order_relaxed);
	}
	atomic_store_explicit(&queue->write, 0, memory_order_relaxed);
	atomic_store_explicit(&queue->read, 0, memory_order_relaxed);
	return queue;
}

static inline bool lf_queue_put(lf_queue_t* queue, void* data) {
	cell_t* cell;
	size_t pos = (size_t)atomic_load_explicit(&queue->write, memory_order_relaxed);
	for (;;) {
		cell = &queue->buffer[pos & queue->mask];
		size_t seq = (size_t)atomic_load(&cell->sequence);
		intptr_t dif = (intptr_t)seq - (intptr_t)pos;
		if (dif == 0) {
			if (CAS(&queue->write, &pos, pos + 1)) {
				break;
			}
		}
		else if (dif < 0) {
			return false;
		}
		else {
			pos = (size_t)atomic_load_explicit(&queue->write, memory_order_relaxed);
		}
	}
	cell->data = data;
	atomic_store_explicit(&cell->sequence, pos + 1, memory_order_release);
	return true;
}

static inline bool lf_queue_get(lf_queue_t* queue, void** data) {
	cell_t* cell;
	size_t pos = atomic_load_explicit(&queue->read, memory_order_relaxed);
	for (;;) {
		cell = &queue->buffer[pos & queue->mask];
		size_t seq = (size_t)atomic_load(&cell->sequence);
		intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
		if (dif == 0) {
			if (CAS(&queue->read, &pos, pos + 1)) {
				break;
			}
		}
		else if (dif < 0) {
			return false;
		}
		else {
			pos = (size_t)atomic_load_explicit(&queue->read, memory_order_relaxed);
		}
	}
	*data = cell->data;
	atomic_store_explicit(&cell->sequence, pos + queue->mask + 1, memory_order_release);
	return true;
}

static inline void lf_queue_destroy(lf_queue_t* queue) {
	TC_FREE(queue->base, queue, sizeof(lf_queue_t) + ((queue->mask + 1) * sizeof(cell_t)));
}