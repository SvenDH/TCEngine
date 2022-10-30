/*==========================================================*/
/*							TIMERS							*/
/*==========================================================*/
#include "timer.h"
#include "fiber.h"
#include "slab.h"
#include "lock.h"
#include "future.h"

#include <uv.h>


typedef struct timer_s {
	tc_waitable_i;
	slab_obj_t;
	uv_timer_t handle;
	tc_fut_t* future;
	uint64_t timeout;
	uint64_t repeats;
} timer_t;

struct context_t {
	tc_allocator_i* a;
	timer_t* pool;
	lock_t lock;
};

struct context_t* context = NULL;

static 
void timer_cb(uv_timer_t* handle) 
{
	timer_t* timer = (timer_t*)handle->data;
	if (--timer->repeats == 0) {
		uv_timer_stop(&timer->handle);
		timer->results = 0;
	}
	tc_future->decrement(timer->future);
}

static 
void timer_destroy(timer_t* timer)
{
	TC_LOCK(&context->lock);
	slab_free(context->pool, timer);
	TC_UNLOCK(&context->lock);
}

tc_fut_t* timer_start(uint64_t timeout, uint64_t repeats)
{
	if (repeats) {
		TC_LOCK(&context->lock);
		timer_t* timer = slab_alloc(context->pool);
		TC_UNLOCK(&context->lock);
		timer->handle.data = timer;
		timer->timeout = timeout;
		timer->repeats = repeats;
		timer->instance = timer;
		timer->dtor = timer_destroy;
		timer->future = tc_future->create(context->a, repeats, timer, 4);
		uv_timer_init(worker_eventloop(), &timer->handle);
		uv_timer_start(&timer->handle, timer_cb, timeout, timeout);
		return timer->future;
	}
	else return 0;
}

void timer_pool_create(tc_allocator_i* a)
{
	context = tc_malloc(a, sizeof(struct context_t));
	context->a = a;
	slab_create(&context->pool, a, CHUNK_SIZE);
}

void timer_pool_destroy()
{
	slab_destroy(context->pool);
	tc_free(context->a, context, sizeof(struct context_t));
}
