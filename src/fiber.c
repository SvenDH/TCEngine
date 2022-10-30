/*==========================================================*/
/*							ASYNC							*/
/*==========================================================*/
#include "fiber.h"
#include "memory.h"
#include "lfqueue.h"
#include "lflifo.h"
#include "temp.h"
#include "lock.h"
#include "future.h"
#include "tcatomic.h"
#include "os.h"

#include <fcontext/fcontext.h>
#include <uv.h>

enum {
	FIBER_STACK_SIZE = SLAB_MIN_SIZE,			// Default fiber stack size in bytes
	FIBER_NUM_JOBS = 8192,						// Maximum number of jobs in the queue
	FIBER_MAIN_ID = -1,
};

typedef struct jobrequest_s {
	tc_waitable_i;
	size_t num_jobs;
	int64_t* result_ptr;
} jobrequest_t;

typedef struct ALIGNED(job_s, 64) {
	job_func func;
	void* data;
	tc_fut_t* future;
	uint32_t id;
	jobrequest_t* req;
} job_t;

typedef struct fiber_s {
	// Intrusive list node for recyclable fibers or waiting fibers
	lf_lifo_t state;							
	// Coroutine context
	fcontext_t fctx;							
	// Fiber id
	int id;										
	// Job to be ran when starting this fiber
	job_t* job;									
	// Start of the fiber's stack
	char* stack_ptr;							
	// Size of fiber stack
	uint32_t stack_size;						
	// Fiber scratch allocator that gets cleared when fiber is finished
	tc_temp_t temp;
	// Name of fiber for debug purposes
	char name[FIBER_NAME_LEN];
} fiber_t;

typedef struct worker_s {
	fiber_t sched;
	// Fiber that is currently running in this worker
	fiber_t* curr_fiber;
	// Id of the thread this worker is running in
	tc_thread_t tid;
	// Optional fiber context lock which is unlocked 
	lock_t* fiblk;
	// Event loop that handles io between fiber executions
	uv_loop_t loop;
	// Id of this worker
	int id;
	// Name of worker thread for debug purposes
	char name[FIBER_NAME_LEN];
} worker_t;

typedef struct fiber_context_s {
	// Initialization and destruction synchronization
	uv_sem_t sem;								
	// Array with worker threads
	worker_t** workers;
	// Number of worker threads
	size_t num_cords;
	// Pointer to the main thread
	worker_t* main;
	// These fibers are done waiting
	ALIGNED(lf_lifo_t, 64) ready;				
	// Lock free singly linked list of free fibers
	ALIGNED(lf_lifo_t, 64) free_list;			
	// Cords take jobs from this queue
	ALIGNED(lf_queue_t*, 64) job_queue;		
	// Array of allocated fiber stacks for fiber allocation
	fiber_t** fibers;						
	// Number of fiber stacks
	size_t num_fibers;
	// Base allocator for fibers
	tc_allocator_i* allocator;
} fiber_context_t;

static fiber_context_t* context = NULL;

ALIGNED(THREAD_LOCAL worker_t*, 64) local_cord;

static jobdecl_t* job_next();
static void job_finish(job_t* job, int64_t result);


/*==========================================================*/
/*						WORKER THREADS						*/
/*==========================================================*/

/* Definition of cord methods: */

static worker_t* worker() { return local_cord; }

void* worker_eventloop() { return &worker()->loop; }

static void worker_loop(worker_t* c) 
{
	jobdecl_t* job = NULL;
	fiber_t* f;
	for (;;) {
		f = lf_lifo_pop(&context->ready);
		if (f) { // Dont finish sched fiber in non sched owned thread
			if (f == &c->sched)
				return;
			else if (f->id == 0 || (f->id == FIBER_MAIN_ID && c->id != 1))
				lf_lifo_push(&context->ready, f);
			else {
				lf_lifo_init(&f->state);
				tc_fiber->resume(f);
			}
		}
		job = job_next();
		if (job) {
			f = tc_fiber->create("worker");
			TC_ASSERT(f);
			tc_fiber->start(f, job);
			// If fiber is done we can put it on the free stack
			if (f->job == NULL)
				tc_fiber->destroy(f);
		}
		
		uv_run(&c->loop, UV_RUN_NOWAIT);
	}
}

struct worker_args {
	worker_t* worker;
	const char* name;
	int id;
};

static void worker_init(struct worker_args* args) 
{
	worker_t* c = args->worker;
	local_cord = c;
	c->id = args->id;
	sprintf(&c->name, args->name, args->id);
	// Initialize thread id and assign thread to cpu
	c->tid = tc_os->current_thread();

	tc_os->set_thread_affinity(c->tid, args->id);
	// Initialize scheduling fiber
	tc_fiber->create(&c->sched, 0, NULL);
	strcpy(&c->sched.name, "sched");
	c->curr_fiber = &c->sched;

	uv_loop_init(&c->loop);
}

static void worker_entry(void* arg) 
{
	struct worker_args* args = (struct worker_args*)arg;
	worker_init(args);
	// Signal thread init is done
	worker_t* c = args->worker;
	uv_sem_post(&context->sem);
	// Start looping to run fibers
	tc_fiber->yield(NULL);
	// Signal thread is finished
	uv_sem_post(&context->sem);
}

/*==========================================================*/
/*							FIBERS							*/
/*==========================================================*/

/* Definition of fiber methods: */

fiber_t* fiber() { return worker()->curr_fiber; }

void fiber_ready(fiber_t* f) { lf_lifo_push(&context->ready, &f->state); }

static void fiber_loop(fcontext_transfer_t t) 
{
	fiber_t* f = t.data;
	TC_ASSERT(f);
	TC_ASSERT(t.ctx != NULL);
	t = jump_fcontext(t.ctx, NULL);
	worker()->sched.fctx = t.ctx;
	for (;;) {
		TC_ASSERT(f != NULL && f->job != NULL && f->id != 0);
		tc_temp->init(&f->temp, context->allocator);
		job_t* job = f->job;
		int64_t ret = job->func(job->data);
		job_finish(job, ret);
		// Clear fiber local 
		tc_temp->close(&f->temp);
		f->name[0] = '\0';
		f->job = NULL;
		tc_fiber->yield(NULL);		// Back to the scheduler
	}
}

static int stack_direction(int* prev_stack_ptr)
{
	int dummy = 0; 
	return &dummy < prev_stack_ptr ? -1 : 1;
}

static void fiber_init(fiber_t* fiber, uint32_t id, fiber_func func) 
{
	// Initialize fiber
	TC_ASSERT(lf_lifo(&fiber->state) == &fiber->state);
	fiber->id = id;
	if (func) {
		fiber->stack_size = FIBER_STACK_SIZE - 3 * CHUNK_SIZE;
		fiber->stack_ptr = ((char*)fiber) + 2 * CHUNK_SIZE;
		// Setup guard pages around the stack for protection against stack overflow
		tc_os->guard_page(fiber->stack_ptr - CHUNK_SIZE, CHUNK_SIZE);
		tc_os->guard_page(fiber->stack_ptr + fiber->stack_size, CHUNK_SIZE);
		// Set stack begin to begin or end of buffer depending of OS stack grow direction
		int dummy = 0;
		if (stack_direction(&dummy) < 0)
			fiber->stack_ptr = fiber->stack_ptr + fiber->stack_size;
		(void)dummy;
		fcontext_t ctx = make_fcontext(fiber->stack_ptr, fiber->stack_size, func);
		fiber->fctx = jump_fcontext(ctx, fiber).ctx;
	}
}

void fiber_pool_init(tc_allocator_i* a, uint32_t num_fibers)
{
	TC_ASSERT(sizeof(fiber_t) <= CHUNK_SIZE);
	
	uint32_t num_cords = tc_os->num_cpus();

	context = tc_malloc(a, sizeof(fiber_context_t));
	memset(context, 0, sizeof(fiber_context_t));
	context->allocator = a;

	// Initialize job queue for schedulers
	context->job_queue = lf_queue_init(FIBER_NUM_JOBS, a);  // Allocate job queue

	// Create other reusable fibers
	context->fibers = tc_malloc(a, num_fibers * sizeof(fiber_t*));
	TC_ASSERT(context->fibers);
	context->num_fibers = num_fibers;
	lf_lifo_init(&context->free_list);
	for (int i = 0; i < num_fibers; i++) {
		context->fibers[i] = tc_malloc(a, FIBER_STACK_SIZE);
		TC_ASSERT(((size_t)context->fibers[i] & ~0xffff) == (size_t)context->fibers[i]);
		memset(context->fibers[i], 0, FIBER_STACK_SIZE);
		fiber_init(context->fibers[i], i + 1, fiber_loop);
		// Add fiber to free list
		lf_lifo_push(&context->free_list, &context->fibers[i]->state);
	}

	// Initialize main thread
	worker_t* main_cord = tc_malloc(a, FIBER_STACK_SIZE);
	worker_init(&(struct worker_args) { main_cord, "main_%i", 0 });
	context->main = main_cord;

	context->num_cords = num_cords;
	context->workers = tc_malloc(a, num_cords * sizeof(void*));
	context->workers[0] = main_cord;

	// Initialize non-main threads with arguments per thread
	if (uv_sem_init(&context->sem, 0))
		TRACE(LOG_ERROR, "[Thread]: Could not create semophore.");

	for (int i = 1; i < num_cords; i++) {
		worker_t* c = tc_malloc(a, FIBER_STACK_SIZE);
		memset(c, 0, FIBER_STACK_SIZE);
		context->workers[i] = c;
		struct worker_args args = (struct worker_args){ c, "worker_%i", i };
		tc_os->create_thread(worker_entry, &args, CHUNK_SIZE);

		uv_sem_wait(&context->sem);
	}
	lf_lifo_init(&context->ready);
}

void fiber_pool_close(tc_allocator_i* a)
{
	worker_t* c = worker();
	TC_ASSERT(c == context->main);
	TC_ASSERT(lf_lifo_is_empty(&context->ready));

	// Destroy fibers
	for (int i = 0; i < context->num_fibers; i++)
		tc_fiber->destroy(context->fibers[i]);
	
	// Destroy cords
	for (int i = 1; i < context->num_cords; i++) {
		worker_t* worker = context->workers[i];
		lf_lifo_push(&context->ready, &worker->sched.state);
		uv_sem_wait(&context->sem);
		uv_loop_close(&worker->loop);
	}
	uv_sem_destroy(&context->sem);

	lf_queue_destroy(context->job_queue);

	tc_free(a, context->workers, context->num_cords, sizeof(void*));
	tc_free(a, context, sizeof(fiber_context_t));
}

fiber_t* fiber_create(const char* name)
{
	fiber_t* fiber = lf_lifo_pop(&context->free_list);
	if (fiber) {
		lf_lifo_init(&fiber->state);
		strncpy(fiber->name, name, FIBER_NAME_LEN);
		return fiber;
	}
	else return NULL;
}

void fiber_start(fiber_t* f, jobdecl_t* job) 
{
	TC_ASSERT(f->id != 0);
	TC_ASSERT(job);
	TC_ASSERT(f != fiber());
	TC_ASSERT(lf_lifo_is_empty(&f->state));
	f->job = job;
	tc_fiber->resume(f);
}

void fiber_yield(lock_t* lk)
{
	worker_t* c = worker();
	fiber_t* curr = c->curr_fiber;
	c->curr_fiber = &c->sched;
	c->fiblk = lk;
	if (curr != &c->sched) {
		fcontext_t ctx = c->sched.fctx;
		TC_ASSERT(ctx != NULL);
		fcontext_t fctx = jump_fcontext(ctx, &c->sched).ctx;
		worker()->sched.fctx = fctx;
	}
	else worker_loop(c);
}

void fiber_resume(fiber_t* f) 
{
	worker_t* c = worker();
	fiber_t* curr = c->curr_fiber;
	TC_ASSERT(f != curr);
	TC_ASSERT(curr == &c->sched);
	TC_ASSERT(lf_lifo_is_empty(&f->state));
	c->curr_fiber = f;
	fcontext_t ctx = f->fctx;
	TC_ASSERT(ctx != NULL);
	f->fctx = NULL;
	f->fctx = jump_fcontext(ctx, f).ctx;
	TC_ASSERT(f->fctx != NULL);

	// Unlock the lock that was left behind by the fiber
	if (c->fiblk) {
		TC_UNLOCK(c->fiblk);
		c->fiblk = NULL;
	}
}

void fiber_destroy(fiber_t* f)
{
	lf_lifo_push(&context->free_list, &f->state);
}

void* scratch_alloc(size_t size) 
{
	return tc_malloc(&fiber()->temp, size);
}

/*==========================================================*/
/*							JOBS							*/
/*==========================================================*/

static void job_destroy(jobrequest_t* req)
{
	tc_free(context->allocator, req, sizeof(jobrequest_t) + req->num_jobs * (sizeof(job_t)));
}

static void job_finish(job_t* job, int64_t result)
{
	// If we have an output array place the result in there
	if (job->req->result_ptr)
		job->req->result_ptr[job->id] = result;
	// Also place the result in the future result (overwriting previous results)
	job->req->results = result;
	// Decrement atomic counter to signal job is done
	tc_future->decrement(job->future);
}

tc_fut_t* fiber_run_jobs(jobdecl_t* jobs, uint32_t num_jobs, int64_t* results)
{
	// Allocate space for request struct and additional jobs
	jobrequest_t* req = tc_malloc(
		context->allocator,
		sizeof(jobrequest_t) + num_jobs * (sizeof(job_t)));
	req->instance = req;
	req->dtor = job_destroy;
	req->num_jobs = num_jobs;
	req->results = 0;
	req->result_ptr = results;

	tc_fut_t* future = tc_future->create(context->allocator, num_jobs, req, 4);
	job_t* j = (job_t*)((size_t)req + sizeof(jobrequest_t));
	for (uint32_t i = 0; i < num_jobs; i++) {
		j[i].func = jobs[i].func;
		j[i].data = jobs[i].data;
		j[i].future = future;
		j[i].id = i;
		j[i].req = req;
		lf_queue_put(context->job_queue, &j[i]);
	}
	return future;
}

jobdecl_t* job_next()
{
	jobdecl_t* job = NULL;
	lf_queue_get(context->job_queue, &job);
	return job;
}

tc_fiber_i* tc_fiber = &(tc_fiber_i) {
	.run_jobs = fiber_run_jobs,
	.create = fiber_create,
	.destroy = fiber_destroy,
	.start = fiber_start,
	.ready = fiber_ready,
	.resume = fiber_resume,
	.yield = fiber_yield,
	.this = fiber,
	.eventloop = worker_eventloop,
};
