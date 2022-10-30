/*==========================================================*/
/*							COUNTERS						*/
/*==========================================================*/
#pragma once
#include "types.h"

/*
 * A counter handle. Fibers can wait on counters to get to a value.
 * The fiber will yield if the counter did not reach that value yet
 * and it will be rescheduled when it did. This can be extended for
 * arbitraty waitable objects.
*/
typedef struct tc_fut_s tc_fut_t;

typedef struct tc_allocator_i tc_allocator_i;

/*
 * Waitable object interface associated with a counter.
*/
#define TC_NOT_FINISHED 0xdfffffff

typedef struct tc_waitable_i {
	/*
	 * The waitable object that will be freed
	 */
	void* instance;
	/*
	 * Counter will call the destructor (dtor) function when it is freed.
	 */
	void(*dtor)(void* w);
	/*
	 * Result that is returned when waited on
	 */
	int64_t results;

} tc_waitable_i;

/*
 * Waits for a counter to complete and free the counter automatically
 */
#define await(_c) tc_future->wait_and_free((_c), 0)

typedef struct tc_future_i {
	/*
	 * Gets a new atomic counter from the counter pool and assigns a value to it
	 */
	tc_fut_t* (*create)(tc_allocator_i* a, size_t value, tc_waitable_i* w, uint32_t num_slots);
	/*
	 * Increments atomic counter and resumes fibers that wait on the new value
	 */
	size_t (*increment)(tc_fut_t* c);
	/*
	 * Decrements atomic counter and resumes fibers that wait on the new value
	 */
	size_t (*decrement)(tc_fut_t* c);
	/*
	 * Waits for a counter to become a specific value.
	 * Puts the currently executing fiber in a wait list in the background.
	 */
	int64_t (*wait)(tc_fut_t* c, size_t value);
	/*
	 * Frees the counter when it is not used anymore and calls the destructor on the waitable
	 */
	void (*free)(tc_fut_t* c);
	/*
	 * First wait for the counter to reach a number and then free the counter
	 */
	int64_t (*wait_and_free)(tc_fut_t* c, size_t value);

} tc_future_i;


extern tc_future_i* tc_future;
