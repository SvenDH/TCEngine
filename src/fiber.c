/*==========================================================*/
/*							ASYNC							*/
/*==========================================================*/
#include "private_types.h"

#include <fcontext/fcontext.h>
#include <uv.h>

enum {
	FIBER_STACK_SIZE = SLAB_MIN_SIZE,			// Default fiber stack size in bytes
	FIBER_NUM_JOBS = 8192,						// Maximum number of jobs in the queue
	FIBER_MAIN_ID = -1,
};

#define FIBER_NAME_LEN 64

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

typedef struct timer_s {
	tc_waitable_i;
	slab_obj_t;
	uv_timer_t handle;
	tc_fut_t* future;
	uint64_t timeout;
	uint64_t repeats;
} timer_t;

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
	tc_allocator_i* a;
	
	timer_t* timer_pool;

	lock_t timer_pool_lock;

} fiber_context_t;

typedef void (*fiber_func)(void*);

static fiber_context_t* context = NULL;

ALIGNED(THREAD_LOCAL worker_t*, 64) local_cord;

static jobdecl_t* job_next();

static void job_destroy(jobrequest_t* req);

static void job_finish(job_t* job, int64_t result);

/** Gets a new fiber from the fiber pool */
static fiber_t* fiber_create(const char* name);

static void fiber_init(fiber_t* fiber, uint32_t id, fiber_func func);

/** Starts a job in a fiber and switches to that fiber */
static void fiber_start(fiber_t* f, jobdecl_t* job);

/** Destroys a fiber and places it back into the pool */
static void fiber_destroy(fiber_t* f);


/*==========================================================*/
/*						WORKER THREADS						*/
/*==========================================================*/

/* Definition of cord methods: */

static worker_t* worker() { 
	return local_cord;
}

void* tc_eventloop() { 
	return &worker()->loop;
}

static void worker_loop(worker_t* c) {
	jobdecl_t* job = NULL;
	fiber_t* f;
	for (;;) {
		f = lf_lifo_pop(&context->ready);
		if (f) { // Dont finish sched fiber in non sched owned thread
			if (f == &c->sched) {
				return;
			}
			else if (f->id == 0 || (f->id == FIBER_MAIN_ID && c->id != 1)) {
				lf_lifo_push(&context->ready, f);
			}
			else {
				lf_lifo_init(&f->state);
				tc_fiber_resume(f);
			}
		}
		job = job_next();
		if (job) {
			f = fiber_create("worker");
			TC_ASSERT(f);
			fiber_start(f, job);
			// If fiber is done we can put it on the free stack
			if (f->job == NULL) {
				fiber_destroy(f);
			}
		}
		uv_run(&c->loop, UV_RUN_NOWAIT);
	}
}

struct worker_args {
	worker_t* worker;
	const char* name;
	int id;
};

static void worker_init(struct worker_args* args) {
	worker_t* c = args->worker;
	local_cord = c;
	c->id = args->id;
	sprintf(&c->name, args->name, args->id);
	// Initialize thread id and assign thread to cpu
	c->tid = tc_os->current_thread();

	tc_os->set_thread_affinity(c->tid, args->id);
	// Initialize scheduling fiber
	fiber_create(&c->sched, 0, NULL);
	strcpy(&c->sched.name, "sched");
	c->curr_fiber = &c->sched;

	uv_loop_init(&c->loop);
}

static void worker_entry(void* arg) {
	struct worker_args* args = (struct worker_args*)arg;
	worker_init(args);
	// Signal thread init is done
	worker_t* c = args->worker;
	uv_sem_post(&context->sem);
	// Start looping to run fibers
	tc_fiber_yield(NULL);
	// Signal thread is finished
	uv_sem_post(&context->sem);
}

/*==========================================================*/
/*							FIBERS							*/
/*==========================================================*/

/* Definition of fiber methods: */

fiber_t* tc_fiber() { 
	return worker()->curr_fiber;
}

void* tc_scratch_alloc(size_t size) {
	return tc_malloc(&tc_fiber()->temp, size);
}

void tc_fiber_ready(fiber_t* f) { 
	lf_lifo_push(&context->ready, &f->state); 
}

static void fiber_loop(fcontext_transfer_t t) {
	fiber_t* f = t.data;
	TC_ASSERT(f);
	TC_ASSERT(t.ctx != NULL);
	t = jump_fcontext(t.ctx, NULL);
	worker()->sched.fctx = t.ctx;
	for (;;) {
		TC_ASSERT(f != NULL && f->job != NULL && f->id != 0);
		tc_temp_init(&f->temp, context->a);
		job_t* job = f->job;
		int64_t ret = job->func(job->data);
		job_finish(job, ret);
		// Clear fiber local 
		tc_temp_free(&f->temp);
		f->name[0] = '\0';
		f->job = NULL;
		tc_fiber_yield(NULL);		// Back to the scheduler
	}
}

void tc_fiber_pool_init(tc_allocator_i* a, uint32_t num_fibers) {
	TC_ASSERT(sizeof(fiber_t) <= CHUNK_SIZE);
	
	uint32_t num_cords = tc_os->num_cpus();

	context = tc_malloc(a, sizeof(fiber_context_t));
	memset(context, 0, sizeof(fiber_context_t));
	context->a = a;

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
	
	slab_create(&context->timer_pool, a, CHUNK_SIZE);
}

void tc_fiber_pool_destroy(tc_allocator_i* a) {
	slab_destroy(context->timer_pool);

	worker_t* c = worker();
	TC_ASSERT(c == context->main);
	TC_ASSERT(lf_lifo_is_empty(&context->ready));

	// Destroy fibers
	for (int i = 0; i < context->num_fibers; i++) {
		fiber_destroy(context->fibers[i]);
	}
	// Destroy cords
	for (int i = 1; i < context->num_cords; i++) {
		worker_t* worker = context->workers[i];
		lf_lifo_push(&context->ready, &worker->sched.state);
		uv_sem_wait(&context->sem);
		uv_loop_close(&worker->loop);
	}
	uv_sem_destroy(&context->sem);

	lf_queue_destroy(context->job_queue);

	tc_free(a, context->workers, context->num_cords * sizeof(void*));
	tc_free(a, context, sizeof(fiber_context_t));
}

void tc_fiber_yield(lock_t* lk) {
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

void tc_fiber_resume(fiber_t* f) {
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

static void fiber_destroy(fiber_t* f) {
	lf_lifo_push(&context->free_list, &f->state);
}

static int stack_direction(int* prev_stack_ptr) {
	int dummy = 0;
	return &dummy < prev_stack_ptr ? -1 : 1;
}

static void fiber_init(fiber_t* fiber, uint32_t id, fiber_func func) {
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
		if (stack_direction(&dummy) < 0) {
			fiber->stack_ptr = fiber->stack_ptr + fiber->stack_size;
		}
		(void)dummy;
		fcontext_t ctx = make_fcontext(fiber->stack_ptr, fiber->stack_size, func);
		fiber->fctx = jump_fcontext(ctx, fiber).ctx;
	}
}

static fiber_t* fiber_create(const char* name) {
	fiber_t* fiber = lf_lifo_pop(&context->free_list);
	if (fiber) {
		lf_lifo_init(&fiber->state);
		strncpy(fiber->name, name, FIBER_NAME_LEN);
		return fiber;
	}
	else return NULL;
}

static void fiber_start(fiber_t* f, jobdecl_t* job) {
	TC_ASSERT(f->id != 0);
	TC_ASSERT(job);
	TC_ASSERT(f != tc_fiber());
	TC_ASSERT(lf_lifo_is_empty(&f->state));
	f->job = job;
	tc_fiber_resume(f);
}


/*==========================================================*/
/*					SYNCHRONIZATION	COUNTER					*/
/*==========================================================*/

typedef struct {
	/**
	 * When running counter_wait() the fiber sleeps
	 * until the counter is this value
	 */
	atomic_t fiber;
	atomic_t inuse;
	size_t value;
} fiber_entry;

typedef struct tc_fut_s {
	tc_allocator_i* a;
	atomic_t value;
	uint32_t num_slots;
	tc_waitable_i* waitable;
} tc_fut_t;


/* Atomic counter methods, used for waiting */
static void counter_wakeup(tc_fut_t* c, size_t value) {
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		fiber_t* f = atomic_load_explicit(&slots[i].fiber, memory_order_acquire);
		if (f == NULL) {
			continue;
		}
		if (atomic_load_explicit(&slots[i].inuse, memory_order_acquire)) {
			continue;
		}
		if (slots[i].value == value) {
			size_t expected = 0;
			if (!CAS(&slots[i].inuse, expected, 1)) {
				continue;
			}
			tc_fiber_ready(f);

			atomic_store_explicit(&slots[i].fiber, 0, memory_order_release);
		}
	}
}

static bool counter_add_to_waiting(tc_fut_t* c, size_t value) {
	fiber_t* f = tc_fiber();
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		size_t expected = 0;
		if (!CAS(&slots[i].fiber, expected, f)) {
			continue;
		}
		slots[i].value = value;
		// Signal slot is ready
		atomic_store(&slots[i].inuse, 0);

		size_t probed = atomic_load_explicit(&c->value, memory_order_relaxed);
		if (atomic_load_explicit(&slots[i].inuse, memory_order_acquire)) {
			return false;
		}
		if (slots[i].value == probed) {
			expected = 0;
			if (!CAS(&slots[i].inuse, expected, 1)) {
				return false;
			}
			//Slot is now free, in use slays true
			atomic_store_explicit(&slots[i].fiber, 0, memory_order_release);
			return true;
		}
		return false;
	}
	return false;
}

tc_fut_t* tc_fut_new(tc_allocator_i* a, size_t value, tc_waitable_i* waitable, uint32_t num_slots) {
	tc_fut_t* c = tc_malloc(a, sizeof(tc_fut_t) + num_slots * sizeof(fiber_entry));
	memset(c, 0, sizeof(tc_fut_t) + num_slots * sizeof(fiber_entry));
	c->a = a;
	c->num_slots = num_slots;
	c->waitable = waitable;
	atomic_store_explicit(&c->value, value, memory_order_relaxed);
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		atomic_store_explicit(&slots[i].inuse, 1, memory_order_release);
	}
	return c;
}

void tc_fut_free(tc_fut_t* c) {
	if (c->waitable && c->waitable->dtor && c->waitable->instance) {
		c->waitable->dtor(c->waitable->instance);
	}
	tc_free(c->a, c, sizeof(tc_fut_t) + c->num_slots * sizeof(fiber_entry));
}

size_t tc_fut_incr(tc_fut_t* c) {
	size_t val = atomic_fetch_add_explicit(&c->value, 1, memory_order_relaxed) + 1;
	counter_wakeup(c, val);
	return val;
}

size_t tc_fut_decr(tc_fut_t* c) {
	size_t val = atomic_fetch_add_explicit(&c->value, -1, memory_order_relaxed) - 1;
	counter_wakeup(c, val);
	return val;
}

int64_t tc_fut_wait(tc_fut_t* c, size_t value) {
	bool done = counter_add_to_waiting(c, value);
	if (!done) {
		tc_fiber_yield(NULL);
	}
	return c->waitable->results;
}

int64_t tc_fut_wait_and_free(tc_fut_t* c, size_t value) {
	int64_t result = tc_fut_wait(c, value);
	tc_fut_free(c);
	return result;
}


/*==========================================================*/
/*							JOBS							*/
/*==========================================================*/

tc_fut_t* tc_run_jobs(jobdecl_t* jobs, uint32_t num_jobs, int64_t* results) {
	// Allocate space for request struct and additional jobs
	jobrequest_t* req = tc_malloc(
		context->a,
		sizeof(jobrequest_t) + num_jobs * (sizeof(job_t)));
	req->instance = req;
	req->dtor = job_destroy;
	req->num_jobs = num_jobs;
	req->results = 0;
	req->result_ptr = results;

	tc_fut_t* future = tc_fut_new(context->a, num_jobs, req, 4);
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

static void job_destroy(jobrequest_t* req) {
	tc_free(context->a, req, sizeof(jobrequest_t) + req->num_jobs * (sizeof(job_t)));
}

static void job_finish(job_t* job, int64_t result) {
	// If we have an output array place the result in there
	if (job->req->result_ptr)
		job->req->result_ptr[job->id] = result;
	// Also place the result in the future result (overwriting previous results)
	job->req->results = result;
	// Decrement atomic counter to signal job is done
	tc_fut_decr(job->future);
}

static jobdecl_t* job_next() {
	jobdecl_t* job = NULL;
	lf_queue_get(context->job_queue, &job);
	return job;
}

/*==========================================================*/
/*							TIMERS							*/
/*==========================================================*/

static
void timer_cb(uv_timer_t* handle) {
	timer_t* timer = (timer_t*)handle->data;
	if (--timer->repeats == 0) {
		uv_timer_stop(&timer->handle);
		timer->results = 0;
	}
	tc_fut_decr(timer->future);
}

static
void timer_destroy(timer_t* timer) {
	TC_LOCK(&context->timer_pool_lock);
	slab_free(context->timer_pool, timer);
	TC_UNLOCK(&context->timer_pool_lock);
}

tc_fut_t* tc_timer_start(uint64_t timeout, uint64_t repeats) {
	if (repeats) {
		TC_LOCK(&context->timer_pool_lock);
		timer_t* timer = slab_alloc(context->timer_pool);
		TC_UNLOCK(&context->timer_pool_lock);
		timer->handle.data = timer;
		timer->timeout = timeout;
		timer->repeats = repeats;
		timer->instance = timer;
		timer->dtor = timer_destroy;
		timer->future = tc_fut_new(context->a, repeats, timer, 4);
		uv_timer_init(tc_eventloop(), &timer->handle);
		uv_timer_start(&timer->handle, timer_cb, timeout, timeout);
		return timer->future;
	}
	else return 0;
}


/*==========================================================*/
/*							CHANNELS						*/
/*==========================================================*/

typedef struct tc_channel_s {
	tc_allocator_i* base;
	lock_t lock;
	slist_t producers;
	slist_t consumers;
	uint32_t cap;
	uint32_t head;
	uint32_t tail;
	bool closed;
} tc_channel_t;


static void queue_notify_one(slist_t* queue) {
	while (!slist_empty(queue)) {
		fiber_t* f = slist_pop_front(queue);
		if (f) {
			tc_fiber_ready(f);
			return;
		}
	}
}

static void queue_notify_all(slist_t* queue) {
	while (!slist_empty(queue)) {
		fiber_t* f = slist_pop_front(queue);
		if (f) {
			tc_fiber_ready(f);
		}
	}
}

static bool channel_full(tc_channel_t* c) {
	return c->tail == ((c->head + 1) % c->cap);
}

static bool channel_empty(tc_channel_t* c) {
	return c->tail == c->head;
}

static int64_t _channel_get(void* arg) {
	tc_channel_t* c = arg;
	fiber_t* f = tc_fiber();
	for (;;) {
		TC_LOCK(&c->lock);
		if (c->closed) {
			TC_UNLOCK(&c->lock);
			return false;
		}
		if (channel_empty(c)) {
			slist_add_tail(&c->consumers, f);
			tc_fiber_yield(&c->lock);
		}
		else {
			void** slots = (void**)(c + 1);
			void* data = slots[c->tail];
			c->tail = (c->tail + 1) % c->cap;
			queue_notify_one(&c->producers);
			TC_UNLOCK(&c->lock);
			return data;
		}
		TC_UNLOCK(&c->lock);
	}
}

tc_fut_t* tc_chan_get(tc_channel_t* channel) {
	return tc_run_jobs(&(jobdecl_t) { .func = _channel_get, .data = channel }, 1, NULL);
}

bool tc_chan_try_get(tc_channel_t* c, void** value) {
	TC_LOCK(&c->lock);
	if (c->closed) {
		TC_UNLOCK(&c->lock);
		return false;
	}
	if (channel_empty(c)) {
		TC_UNLOCK(&c->lock);
		return false;
	}
	void** slots = (void**)(c + 1);
	*value = slots[c->tail];
	c->tail = (c->tail + 1) % c->cap;
	queue_notify_one(&c->producers);
	TC_UNLOCK(&c->lock);
	return true;
}

static int64_t _channel_put(void* arg) {
	tc_put_t* put_data = arg;
	tc_channel_t* c = put_data->channel;
	fiber_t* f = tc_fiber();
	for (;;) {
		TC_LOCK(&c->lock);
		if (c->closed) {
			TC_UNLOCK(&c->lock);
			return 0;
		}
		if (channel_full(c)) {
			slist_add_tail(&c->producers, f);
			tc_fiber_yield(&c->lock);
		}
		else {
			void** slots = (void**)(c + 1);
			slots[c->head] = put_data->value;
			c->head = (c->head + 1) % c->cap;
			queue_notify_one(&c->consumers);
			TC_UNLOCK(&c->lock);
			return 1;
		}
		TC_UNLOCK(&c->lock);
	}
}

tc_fut_t* tc_chan_put(tc_put_t* put_data) {
	return tc_run_jobs(&(jobdecl_t) { .func = _channel_put, .data = put_data }, 1, NULL);
}

bool tc_chan_try_put(tc_put_t* put_data) {
	tc_channel_t* c = put_data->channel;
	TC_LOCK(&c->lock);
	if (c->closed) {
		TC_UNLOCK(&c->lock);
		return false;
	}
	if (channel_full(c)) {
		TC_UNLOCK(&c->lock);
		return false;
	}
	void** slots = (void**)(c + 1);
	slots[c->head] = put_data->value;
	c->head = (c->head + 1) % c->cap;
	queue_notify_one(&c->consumers);
	TC_UNLOCK(&c->lock);
	return true;
}

tc_channel_t* tc_chan_new(tc_allocator_i* a, uint32_t num_slots) {
	tc_channel_t* c = tc_malloc(a, sizeof(tc_channel_t) + num_slots * sizeof(void*));
	memset(c, 0, sizeof(tc_channel_t) + num_slots * sizeof(void*));
	c->base = a;
	c->cap = num_slots;
	slist_init(&c->consumers);
	slist_init(&c->producers);
	return c;
}

void tc_chan_close(tc_channel_t* c) {
	TC_LOCK(&c->lock);
	if (!c->closed) {
		c->closed = true;
		queue_notify_all(&c->producers);
		queue_notify_all(&c->consumers);
	}
	TC_UNLOCK(&c->lock);
}

void tc_chan_destroy(tc_channel_t* c) {
	tc_free(c->base, c, sizeof(tc_channel_t) + c->cap * sizeof(void*));
}
