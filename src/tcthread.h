/*==========================================================*/
/*							THREADS							*/
/*==========================================================*/
#pragma once
#include "tccore.h"
#include "tcatomic.h"
#include "tcdata.h"


#define FIBER_NAME_LEN 64



typedef struct fiber_context_s fiber_context_t;

typedef size_t counter_id;

typedef void* (*job_func)(void*);

typedef struct ALIGNED(job_s, 64) {
	job_func func;											// Job function
	void* data;												// Data pointer to give to job function
	counter_id counter;										// Counter to decrement when the job is finished, is set with jobs_run()
} job_t;

typedef void (*fiber_func)(void*);

typedef struct fiber_s fiber_t;

typedef struct cord_s cord_t;


/* Main api: */

/*==========================================================*/
/*							JOBS							*/
/*==========================================================*/

// Runs a number of jobs concurrently and returns a counter that tracks the number of jobs still in process, counter can be waited on by counter_wait()
counter_id jobs_run(job_t* jobs, size_t num_jobs);

// Waits for a counter to become a specific value, puts the currently executing fiber in a wait list in the background
void counter_wait(counter_id counter, size_t value);

// Frees to counter when it is not used anymore
void counter_free(counter_id id);


void fiber_init();

void fiber_close();


/* Api for specific operations: */

/*==========================================================*/
/*							COUNTERS							*/
/*==========================================================*/

// Gets a new atomic counter from the counter pool and assigns a value to it
counter_id counter_new(size_t value);

// Decrements atomic counter and resumes 
void counter_decrement(counter_id id);

/*==========================================================*/
/*							FIBERS							*/
/*==========================================================*/

// Returns the currently executing fiber
fiber_t* fiber(void);

// Gets a new fiber from the fiber pool
fiber_t* fiber_new(const char* name);

// Starts a job in a fiber and switches to that fiber
void fiber_start(fiber_t* f, job_t* job);

// Yields the current fiber to the scheduler
void fiber_yield();

// Resume executing a fiber (that was suspended)
void fiber_resume(fiber_t* f);

// Destroys a fiber
void fiber_destroy(fiber_t* fiber);

// Get fiber owned cache
slab_cache_t* fiber_cache();

// Returns the current thread
cord_t* cord(void);


/*==========================================================*/
/*							LOCKS							*/
/*==========================================================*/

void spin_lock(atomic_t* lock);

void spin_unlock(atomic_t* lock);