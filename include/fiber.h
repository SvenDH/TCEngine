/*==========================================================*/
/*							THREADS							*/
/*==========================================================*/
#pragma once
#include "types.h"

#define FIBER_NAME_LEN 64

typedef void (*fiber_func)(void*);

typedef struct fiber_s fiber_t;
typedef struct lock_s lock_t;
typedef struct tc_fut_s tc_fut_t;
typedef struct tc_allocator_i tc_allocator_i;

/*
 * Jobs definition. 
*/
typedef int64_t (*job_func)(void*);

typedef struct jobdecl_s {
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

typedef struct tc_fiber_i {

	tc_fut_t* (*run_jobs)(jobdecl_t* jobs, uint32_t num_jobs, int64_t* results);
	/*
	 * Returns the currently executing fiber
	 */
	fiber_t* (*this)();
	/*
	 * Get the cord local event loop
	 */
	void* (*eventloop)();
	/*
	 * Gets a new fiber from the fiber pool
	 */
	fiber_t* (*create)(const char* name);
	/*
	 * Starts a job in a fiber and switches to that fiber
	 */
	void (*start)(fiber_t* f, jobdecl_t* job);
	/*
	 * Put fiber in ready queue to be scheduled
	 */
	void (*ready)(fiber_t* f);
	/*
	 * Yields the current fiber to the scheduler
	 * Optional lock is unlocked when yield was successful to release resources on context switch
	 */
	void (*yield)(lock_t* lk);
	/*
	 * Resume executing a fiber (that was suspended)
	 */
	void (*resume)(fiber_t* f);
	/*
	 * Destroys a fiber and places it back into the pool
	 */
	void (*destroy)(fiber_t* fiber);

} tc_fiber_i;


/*
 * Allocate from fiber local scratch buffer which gets dumped at the end of fiber lifetime
 */
void* scratch_alloc(size_t size);

/*
 * Initializes the fiber pool with `num_fibers` fibers
 */
void fiber_pool_init(tc_allocator_i* a, uint32_t num_fibers);
/*
 * Destroys the fiber pool
 */
void fiber_pool_close(tc_allocator_i* a);


extern tc_fiber_i* tc_fiber;
