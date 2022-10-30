/*==========================================================*/
/*							CHANNELS						*/
/*==========================================================*/
#include "channel.h"
#include "memory.h"
#include "fiber.h"
#include "lock.h"
#include "list.h"


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

static void queue_notify_one(slist_t* queue)
{
	while (!slist_empty(queue)) {
		fiber_t* f = slist_pop_front(queue);
		if (f) {
			tc_fiber->ready(f);
			return;
		}
	}
}

static void queue_notify_all(slist_t* queue)
{
	while (!slist_empty(queue)) {
		fiber_t* f = slist_pop_front(queue);
		if (f) tc_fiber->ready(f);
	}
}

static bool channel_full(tc_channel_t* c)
{ return c->tail == ((c->head + 1) % c->cap);}

static bool channel_empty(tc_channel_t* c)
{ return c->tail == c->head; }

static int64_t _channel_get(void* arg)
{
	tc_channel_t* c = arg;
	fiber_t* f = tc_fiber->this();
	for (;;) {
		TC_LOCK(&c->lock);
		if (c->closed) {
			TC_UNLOCK(&c->lock);
			return false;
		}
		if (channel_empty(c)) {
			slist_add_tail(&c->consumers, f);
			tc_fiber->yield(&c->lock);
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

tc_fut_t* channel_get(tc_channel_t* channel)
{
	return tc_fiber->run_jobs(&(jobdecl_t) {
		.func = _channel_get, .data = channel
	}, 1, NULL);
}

bool channel_try_get(tc_channel_t* c, void** value)
{
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

static int64_t _channel_put(void* arg)
{
	tc_put_t* put_data = arg;
	tc_channel_t* c = put_data->channel;
	fiber_t* f = tc_fiber->this();
	for (;;) {
		TC_LOCK(&c->lock);
		if (c->closed) {
			TC_UNLOCK(&c->lock);
			return 0;
		}
		if (channel_full(c)) {
			slist_add_tail(&c->producers, f);
			tc_fiber->yield(&c->lock);
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

tc_fut_t* channel_put(tc_put_t* put_data)
{
	return tc_fiber->run_jobs(&(jobdecl_t) {
		.func = _channel_put, .data = put_data
	}, 1, NULL);
}

bool channel_try_push(tc_put_t* put_data)
{
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

tc_channel_t* channel_create(tc_allocator_i* a, uint32_t num_slots)
{
	tc_channel_t* c = tc_malloc(a, sizeof(tc_channel_t) + num_slots * sizeof(void*));
	memset(c, 0, sizeof(tc_channel_t) + num_slots * sizeof(void*));
	c->base = a;
	c->cap = num_slots;
	slist_init(&c->consumers);
	slist_init(&c->producers);
	return c;
}

void channel_close(tc_channel_t* c)
{
	TC_LOCK(&c->lock);
	if (!c->closed) {
		c->closed = true;
		queue_notify_all(&c->producers);
		queue_notify_all(&c->consumers);
	}
	TC_UNLOCK(&c->lock);
}

void channel_destroy(tc_channel_t* c, tc_allocator_i* a)
{
	tc_free(c->base, c, sizeof(tc_channel_t) + c->cap * sizeof(void*));
}

tc_channel_i* tc_channel = &(tc_channel_i) {
	.put = channel_put,
	.get = channel_get,
	.try_put = channel_try_push,
	.try_get = channel_try_get,
	.close = channel_close,
	.create = channel_create,
	.destroy = channel_destroy,
};
