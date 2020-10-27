/*==========================================================*/
/*							FIBERS							*/
/*==========================================================*/

#include "tcthread.h"
#include "tcmemory.h"
#include "tcos.h"

#include <coro.h>
#include <uv.h>
//#include <fcontext/fcontext.h>


typedef struct ALIGNED(counter_s, 64) {
	volatile atomic_t value;
	volatile atomic_t lock;
	lf_lifo_t wait_list;
} counter_t;

typedef struct fiber_s {
	lf_lifo_t state;							// Intrusive list node for recyclable fibers or waiting fibers
	uint32_t flags;
	uint32_t counter_value;						// When running wait_on_counter() the fiber sleeps until the counter is this value

	cord_t* cord;
	coro_context ctx;
	//fcontext_t ctx;							// Coroutine context
	int id;										// Fiber id

	job_t* job;									// Job to be ran when starting this fiber

	char* stack_ptr;							// Start of the fiber's stack
	uint32_t stack_size;						// Size of fiber stack
	uint64_t* watermark;						// Watermark location in the stack

	slab_cache_t cache;

	char name[FIBER_NAME_LEN];
} fiber_t;

typedef struct cord_s {
	fiber_t sched;
	fiber_t* curr_fiber;

	uv_thread_t tid;
	uv_loop_t loop;
	int id;

	fiber_context_t* ctx;

	char name[FIBER_NAME_LEN];
} cord_t;

typedef struct fiber_context_s {
	uv_sem_t sem;								// Initialization and destruction synchronization
	cord_t** cords;								// Array with worker cords
	size_t num_cords;
	cord_t* main;
	ALIGNED(lf_queue_t, 64) job_queue;			// Cords take jobs from this queue
	ALIGNED(lf_lifo_t, 64) ready;				// These fibers are done waiting

	ALIGNED(lf_lifo_t, 64) free_list;			// Lock free singly linked list of free fibers
	fiber_t** fibers;							// Array of allocated fiber stacks for fiber allocation
	size_t num_fibers;							// Number of fiber stacks

	ALIGNED(lf_queue_t, 64) free_counters;
	counter_t* counters;
	size_t num_counters;

	ALIGNED(atomic_t, 64) allocator_lock;
	slab_cache_t allocator;
} fiber_context_t;


enum {
	FIBER_STACK_SIZE = SLAB_MIN_SIZE,			// Default fiber stack size in bytes
	FIBER_NUM_DEFAULT_CORDS = 4,				// Number of worker threads for fibers
	FIBER_NUM_STACKS = 128,						// Maximum number of fiber in the fiber pool
	FIBER_NUM_JOBS = 8192,						// Maximum number of jobs in the queue
	FIBER_NUM_COUNTERS = 1024,					// Maximum number of counters
	FIBER_CACHE_SIZE = 16777216,				// Maximum allocated memory in a fiber
	FIBER_MAIN_ID = -1,
	FIBER_READY	= 1 << 0,
	FIBER_DEAD = 1 << 1,
};

static const uint64_t poison_pool[] = {
	0x74f31d37285c4c37, 0xb10269a05bf10c29,
	0x0994d845bd284e0f, 0x9ffd4f7129c184df,
	0x357151e6711c4415, 0x8c5e5f41aafe6f28,
	0x6917dd79e78049d5, 0xba61957c65ca2465,
};
#define POISON_SIZE	(sizeof(poison_pool) / sizeof(poison_pool[0]))
#define POISON_OFF	(128 / sizeof(poison_pool[0]))

static fiber_context_t* context;

ALIGNED(uv_key_t, 64) local_cord;

/* Atomic counter methods, used for waiting */
static counter_t* counter_get(counter_id id) {
	TC_ASSERT(id > 0 && id <= FIBER_NUM_COUNTERS);
	cord_t* c = cord();
	fiber_context_t* ctx = c->ctx;
	return &ctx->counters[id - 1];
}

counter_id counter_new(size_t value) {
	counter_id id = 0;
	lf_queue_get(&cord()->ctx->free_counters, &id);
	if (id) {
		counter_t* counter = counter_get(id);
		atomic_store(&counter->value, value, MEMORY_RELAXED);
		lf_lifo_init(&counter->wait_list);
	}
	return id;
}

void counter_free(counter_id id) {
	counter_t* counter = counter_get(id);
	TC_ASSERT(lf_lifo_is_empty(&counter->wait_list));
	lf_queue_put(&cord()->ctx->free_counters, (void*)id);
}

void counter_decrement(counter_id id) {
	counter_t* counter = counter_get(id);
	TC_ASSERT(counter);
	spin_lock(&counter->lock);
	//TODO: Add atomic decrement
	size_t value = (size_t)atomic_fetch_add(&counter->value, (ptrdiff_t)-1, MEMORY_RELAXED) - 1;
	cord_t* c = cord();
	for (lf_lifo_t* l = &counter->wait_list; l; l = lf_lifo(atomic_load(&l->next, MEMORY_RELAXED))) {
		//TODO: check if this loop is correct
		fiber_t* f = (fiber_t*)lf_lifo(l->next);
		if (f && f->counter_value == value) {
			f->flags |= FIBER_READY;
			lf_lifo_pop(l);
			lf_lifo_push(&c->ctx->ready, &f->state);
		}
	}
	spin_unlock(&counter->lock);
}

void counter_wait(counter_id id, size_t value) {
	counter_t* counter = counter_get(id);
	size_t new_value = atomic_load(&counter->value, MEMORY_RELAXED);
	if (new_value != value) {
		fiber_t* f = fiber();
		f->counter_value = value;
		lf_lifo_push(&counter->wait_list, f);
		fiber_yield();
	}
}

counter_id jobs_run(job_t* jobs, size_t num_jobs) {
	counter_id counter = counter_new(num_jobs);
	for (int i = 0; i < num_jobs; i++) {
		jobs[i].counter = counter;
		lf_queue_put(&cord()->ctx->job_queue, &jobs[i]);
	}
	return counter;
}


static bool _has_watermark(void* addr) {
	const uint64_t* src = poison_pool;
	const uint64_t* dst = addr;
	for (size_t i = 0; i < POISON_SIZE; i++) {
		if (*dst != src[i])
			return false;
		dst += POISON_OFF;
	}
	return true;
}

static void _put_watermark(void* addr) {
	const uint64_t* src = poison_pool;
	uint64_t* dst = addr;
	for (size_t i = 0; i < POISON_SIZE; i++) {
		*dst = src[i];
		dst += POISON_OFF;
	}
}

static void fiber_recycle(fiber_t* f) {
	TC_ASSERT(_has_watermark(f->watermark));
	f->flags |= FIBER_DEAD;
	f->name[0] = '\0';
	f->job = NULL;
}

static void fiber_loop() {
	fiber_t* f = fiber();
	for (;;) {
		TC_ASSERT(f->cord == cord());
		TC_ASSERT(f != NULL && f->job != NULL && f->id != 0);
		void* ret = (f->job->func)(f->job->data);
		TC_ASSERT(ret == 0);
		// TODO: do something with return
		if (f->job->counter)
			counter_decrement(f->job->counter);
		fiber_recycle(f);
		fiber_yield();		// Back to the scheduler
	}
}

static void fiber_create(fiber_t* fiber, uint32_t id, fiber_func func) {
	// Initialize fiber
	TC_ASSERT(lf_lifo(&fiber->state) == &fiber->state);
	fiber->id = id;
	fiber->flags = FIBER_DEAD;
	// Create a fiber owned slab cache for fiber local memory allocation 
	fiber->cache = cache_create((cache_params) { FIBER_CACHE_SIZE, fiber });
	// Set stack begin to begin or end of buffer depending of OS stack grow direction
	size_t offset = rand() % POISON_OFF * sizeof(poison_pool[0]);
	fiber->stack_size = FIBER_STACK_SIZE - 2 * pagesize;
	if (stack_dir < 0) {
		fiber->stack_ptr = ((char*)fiber) + pagesize;
		fiber->watermark = fiber->stack_ptr + fiber->stack_size;
		fiber->watermark += offset;
	}
	else {
		fiber->stack_ptr = ((char*)fiber) + FIBER_STACK_SIZE - pagesize;
		fiber->watermark = fiber->stack_ptr - fiber->stack_size;
		fiber->watermark -= offset;
	}
	_put_watermark(fiber->watermark);
	coro_create(&fiber->ctx, func, fiber, fiber->stack_ptr, fiber->stack_size);
	//fiber->ctx = make_fcontext(fiber->stack_ptr, fiber->stack_size, func);
}

struct cord_args {
	fiber_context_t* ctx;
	cord_t* cord;
	const char* name;
	int id;
	int cpu;
};

void cord_loop(void* arg) {
	cord_t* c = cord();
	TC_ASSERT(c != NULL);
	c->sched.flags |= FIBER_READY;
	job_t* job = NULL;
	fiber_t* f;
	for (;;) {
		f = lf_lifo_pop(&c->ctx->ready);
		if (f) { // Dont finish sched fiber in non sched ownded thread
			if (f->id == c->sched.id && f->cord->id == c->id)
				return;
			else if (f->id == 0 || (f->id == FIBER_MAIN_ID && c->id != 1))
				lf_lifo_push(&c->ctx->ready, f);
			else
				fiber_resume(f);
		}
		if (lf_queue_get(&c->ctx->job_queue, &job)) {
			f = fiber_new("worker");
			TC_ASSERT(f);
			fiber_start(f, job);
			if (f->flags & FIBER_DEAD)
				lf_lifo_push(&c->ctx->free_list, &f->state);
		}
		uv_run(&c->loop, UV_RUN_NOWAIT);
	}
}

static void cord_init(struct cord_args* args) {
	cord_t* c = args->cord;
	uv_key_set(&local_cord, c);
	c->id = args->id;
	c->ctx = args->ctx;
	sprintf(&c->name, args->name, args->id);
	// Initialize thread id and assign thread to cpu
	c->tid = uv_thread_self();
	if (!c->tid) c->tid = get_current_thread();
	set_thread_affinity(c->tid, args->cpu);
	// Initialize scheduling fiber
	fiber_create(&c->sched, 0, NULL);
	strcpy(&c->sched.name, "sched");
	c->sched.flags = FIBER_READY;
	c->sched.cord = c;
	c->curr_fiber = &c->sched;
	// Initialize cord local memory cache
	uv_loop_init(&c->loop);
}

static void cord_worker(void* arg) {
	struct cord_args* args = (struct cord_args*)arg;
	cord_init(args);
	// Signal thread init is done
	cord_t* c = args->cord;
	uv_sem_post(&c->ctx->sem);
	// Start looping to run fibers
	cord_loop(c);
	// Signal thread is finished
	uv_sem_post(&c->ctx->sem);
}

static void* fiber_global_malloc(size_t size) {
	spin_lock(&context->allocator_lock);
	void* ptr = cache_alloc(&context->allocator, size);
	spin_unlock(&context->allocator_lock);
	return ptr;
}

static void* fiber_global_calloc(size_t count, size_t size) {
	spin_lock(&context->allocator_lock);
	void* ptr = cache_alloc(&context->allocator, count * size);
	spin_unlock(&context->allocator_lock);
	return memset(ptr, 0, size);
}

static void fiber_global_free(void* ptr) {
	spin_lock(&context->allocator_lock);
	cache_free(&context->allocator, ptr);
	spin_unlock(&context->allocator_lock);
}

static void* fiber_global_realloc(void* ptr, size_t size) {
	spin_lock(&context->allocator_lock);
	void* new_ptr = cache_realloc(&context->allocator, ptr, size);
	spin_unlock(&context->allocator_lock);
	return new_ptr;
}

void counter_pool_init(fiber_context_t* context) {
	context->counters = fiber_global_malloc(FIBER_NUM_COUNTERS * sizeof(counter_t));
	TC_ASSERT(context->counters);
	memset(context->counters, 0, FIBER_NUM_COUNTERS * sizeof(counter_t));
	context->num_counters = FIBER_NUM_COUNTERS;
	lf_queue_init(&context->free_counters, context->num_counters, &context->allocator);
	for (size_t i = 0; i < FIBER_NUM_COUNTERS; i++)
		lf_queue_put(&context->free_counters, (void*)(i + 1));
}

void fiber_pool_init(fiber_context_t* context) {
	// Create other reusable fibers
	context->fibers = (fiber_t**)fiber_global_malloc(FIBER_NUM_STACKS * sizeof(fiber_t*));
	TC_ASSERT(context->fibers);
	context->num_fibers = FIBER_NUM_STACKS;
	lf_lifo_init(&context->free_list);
	for (int i = 0; i < FIBER_NUM_STACKS; i++) {
		context->fibers[i] = (fiber_t*)fiber_global_malloc(FIBER_STACK_SIZE);
		memset(context->fibers[i], 0, FIBER_STACK_SIZE);
		fiber_create(context->fibers[i], i + 1, fiber_loop);
		// Add fiber to free list
		lf_lifo_push(&context->free_list, &context->fibers[i]->state);
	}
}

static void cord_pool_init(fiber_context_t* context) {
	// Allocate job queue
	lf_queue_init(&context->job_queue, FIBER_NUM_JOBS, &context->allocator);

	// Initialize main thread
	cord_t* main_cord = (cord_t*)cache_alloc(&context->allocator, FIBER_STACK_SIZE);
	cord_init(&(struct cord_args) { context, main_cord, "main_%i", 1, 0 });
	context->main = main_cord;
	
	// Check amount of cpus available
	uv_cpu_info_t* info = NULL;
	int num_cords = 0;
	if (uv_cpu_info(&info, &num_cords))
		num_cords = FIBER_NUM_DEFAULT_CORDS;
	uv_free_cpu_info(info, num_cords);

	context->num_cords = num_cords;
	context->cords = (cord_t**)cache_alloc(&context->allocator, num_cords * sizeof(void*));
	context->cords[0] = main_cord;
	
	// Initialize non-main threads with arguments per thread
	if (uv_sem_init(&context->sem, 0)) 
		abort();
	for (int i = 1; i < num_cords; i++) {
		cord_t* c = (cord_t*)cache_alloc(&context->allocator, FIBER_STACK_SIZE);
		memset(c, 0, FIBER_STACK_SIZE);
		context->cords[i] = c;

		struct cord_args args = (struct cord_args){ context, c, "worker_%i", i + 1, i };
		if (uv_thread_create(&context->cords[i]->tid, cord_worker, (void*)&args)) 
			abort();
		uv_sem_wait(&context->sem);
	}
	lf_lifo_init(&context->ready);
}

void fiber_init() {
	TC_ASSERT(sizeof(fiber_t) <= pagesize);
	slab_cache_t cache = cache_create((cache_params) { FIBER_CACHE_SIZE });
	context = cache_alloc(&cache, sizeof(fiber_context_t));
	memset(context, 0, sizeof(fiber_context_t));
	context->allocator = cache;
	(void)cache;

	uv_replace_allocator(fiber_global_malloc, fiber_global_realloc, fiber_global_calloc, fiber_global_free);
	uv_key_create(&local_cord);

	fiber_pool_init(context);
	cord_pool_init(context);
	counter_pool_init(context);
}

void fiber_close() {
	cord_t* c = cord();
	TC_ASSERT(c == context->main);
	TC_ASSERT(lf_lifo_is_empty(&context->ready));
	// Destroy cords
	for (int i = 1; i < context->num_cords; i++) {
		cord_t* cord = context->cords[i];
		lf_lifo_push(&context->ready, &cord->sched.state);
		uv_sem_wait(&context->sem);
	}
	uv_sem_destroy(&context->sem);
	fiber_global_free(context->cords);

	// Destroy fibers
	for (int i = 0; i < context->num_fibers; i++)
		fiber_destroy(context->fibers[i]);

	uv_key_delete(&local_cord);
	lf_queue_destroy(&context->job_queue);

	// Destroy counter pool
	fiber_global_free(context->counters);
	lf_queue_destroy(&context->free_counters);
	cache_destroy(&context->allocator);
}

cord_t* cord() {
	return (cord_t*)uv_key_get(&local_cord);
}

fiber_t* fiber() {
	return cord()->curr_fiber;
}

fiber_t* fiber_new(const char* name) {
	fiber_t* fiber = lf_lifo_pop(&cord()->ctx->free_list);
	if (fiber) {
		TC_ASSERT(fiber->flags & FIBER_DEAD);
		lf_lifo_init(&fiber->state);
		memcpy(fiber->name, name, min(FIBER_NAME_LEN, strlen(name) + 1));
		fiber->flags &= ~FIBER_DEAD;
		return fiber;
	}
	else return NULL;
}

void fiber_start(fiber_t* f, job_t* job) {
	TC_ASSERT(!(f->flags & FIBER_DEAD) && !(f->flags & FIBER_READY));
	TC_ASSERT(f->id != 0);
	TC_ASSERT(job);
	TC_ASSERT(f != fiber());
	TC_ASSERT(lf_lifo_is_empty(&f->state));
	f->job = job;
	f->flags |= FIBER_READY;
	fiber_resume(f);
}

void fiber_yield() {
	cord_t* c = cord();
	fiber_t* curr = c->curr_fiber;
	c->curr_fiber = &c->sched;
	if (curr == &c->sched)
		// A main thread that waits will execute other fibers while waiting
		cord_loop(c);
	else {
		TC_ASSERT(curr->cord == c);
		//f->flags &= ~FIBER_READY;
		coro_transfer(&curr->ctx, &c->sched.ctx);
	}
}

void fiber_resume(fiber_t* f) {
	cord_t* c = cord();
	fiber_t* curr = c->curr_fiber;
	TC_ASSERT(f != curr);
	TC_ASSERT(!(f->flags & FIBER_DEAD));
	TC_ASSERT(f->flags & FIBER_READY);
	//TC_ASSERT(curr == &c->sched);
	//TC_ASSERT(lf_lifo_is_empty(&f->state));
	c->curr_fiber = f;
	f->cord = c;
	f->flags &= ~FIBER_READY;
	coro_transfer(&curr->ctx, &f->ctx);
	//jump_fcontext(f->ctx, NULL);
}

void fiber_destroy(fiber_t* f) {
	cache_destroy(&f->cache);
	coro_destroy(&f->ctx);
	fiber_global_free(f);
}

slab_cache_t* fiber_cache() {
	return &fiber()->cache;
}

void spin_lock(atomic_t* lock) {
	for (;;) {
		if (!atomic_exchange(lock, true, MEMORY_ACQUIRE))
			break;
		while (atomic_load(lock, MEMORY_RELAXED))
			pause();
	}
}

void spin_unlock(atomic_t* lock) { atomic_store(lock, false, MEMORY_RELEASE); }