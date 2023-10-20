/*==========================================================*/
/*							THREADS							*/
/*==========================================================*/
#pragma once
#include "core.h"

/**
 * A counter handle. Fibers can wait on counters to get to a value.
 * The fiber will yield if the counter did not reach that value yet
 * and it will be rescheduled when it did. This can be extended for
 * arbitraty waitable objects.
*/
typedef struct tc_fut_s fut_t;

typedef struct fiber_s fiber_t;
typedef struct lock_s lock_t;
typedef struct tc_allocator_i tc_allocator_i;

/*
 * Jobs definition. 
*/
typedef int64_t (*job_func)(void*);

typedef struct {
	/* 
	 * Job function to be executed by a fiber 
	 */
	job_func func;
	/* 
	 * Context data pointer to give to job function	
	 */
	void* data;

} jobdecl_t;

/*==========================================================*/
/*							FIBERS							*/
/*==========================================================*/

fut_t* tc_run_jobs(jobdecl_t* jobs, uint32_t num_jobs, int64_t* results);

/** Returns the currently executing fiber */
fiber_t* tc_fiber();

/** Get the cord local event loop */
void* tc_eventloop();

/** Put fiber in ready queue to be scheduled */
void tc_fiber_ready(fiber_t* f);

/**
 * Yields the current fiber to the scheduler
 * Optional lock is unlocked when yield was successful to release resources on context switch
 */
void tc_fiber_yield(lock_t* lk);

/** Resume executing a fiber (that was suspended) */
void tc_fiber_resume(fiber_t* f);

/** Allocate from fiber local scratch buffer which gets dumped at the end of fiber lifetime */
void* tc_scratch_alloc(size_t size);

/** Initializes the fiber pool with `num_fibers` fibers */
void fiber_pool_init(tc_allocator_i* a, uint32_t num_fibers);

/** Destroys the fiber pool */
void fiber_pool_destroy(tc_allocator_i* a);


/*==========================================================*/
/*							COUNTERS						*/
/*==========================================================*/

/** Waitable object interface associated with a counter */
#define TC_NOT_FINISHED 0xdfffffff

typedef struct {
	/** The waitable object that will be freed */
	void* instance;
	/** Counter will call the destructor (dtor) function when it is freed. */
	void(*dtor)(void* w);

	/** Result that is returned when waited on */
	int64_t results;

} tc_waitable_i;

/** Waits for a counter to complete and free the counter automatically */
#define await(_c) tc_fut_wait_and_free((_c), 0)

/** Gets a new atomic counter from the counter pool and assigns a value to it */
fut_t* tc_fut_new(tc_allocator_i* a, size_t value, tc_waitable_i* w, uint32_t num_slots);

/** Increments atomic counter and resumes fibers that wait on the new value */
size_t tc_fut_incr(fut_t* c);

/** Decrements atomic counter and resumes fibers that wait on the new value */
size_t tc_fut_decr(fut_t* c);

/** Waits for a counter to become a specific value. Puts the currently executing fiber in a wait list in the background */
int64_t tc_fut_wait(fut_t* c, size_t value);

/** Frees the counter when it is not used anymore and calls the destructor on the waitable */
void tc_fut_free(fut_t* c);

/** First wait for the counter to reach a number and then free the counter */
int64_t tc_fut_wait_and_free(fut_t* c, size_t value);


/*==========================================================*/
/*							TIMERS							*/
/*==========================================================*/

/**
 * Starts a timer that decreases a counter after timeout miliseconds.
 * Repeats the counter tick repeat number of times if it is nonzero
 */
fut_t* tc_timer_start(uint64_t timeout, uint64_t repeats);


/*==========================================================*/
/*							CHANNELS						*/
/*==========================================================*/

typedef struct tc_channel_s tc_channel_t;

typedef struct {
	tc_channel_t* channel;
	void* value;
} tc_put_t;

fut_t* tc_chan_get(tc_channel_t* channel);

bool tc_chan_try_get(tc_channel_t* channel, void** value);

fut_t* tc_chan_put(tc_put_t* put_data);

bool tc_chan_try_put(tc_put_t* put_data);

void tc_chan_close(tc_channel_t* channel);

tc_channel_t* tc_chan_new(tc_allocator_i* a, uint32_t size);

void tc_chan_destroy(tc_channel_t* channel);
