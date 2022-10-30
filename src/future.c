/*==========================================================*/
/*					SYNCHRONIZATION	COUNTER					*/
/*==========================================================*/
#include "fiber.h"
#include "future.h"
#include "tcatomic.h"
#include "memory.h"


typedef struct {
	/* When running counter_wait() the fiber sleeps
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

static void counter_wakeup(tc_fut_t* c, size_t value)
{
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		fiber_t* f = atomic_load(&slots[i].fiber, MEMORY_ACQUIRE);
		if (f == NULL)
			continue;

		if (atomic_load(&slots[i].inuse, MEMORY_ACQUIRE))
			continue;

		if (slots[i].value == value) {
			size_t expected = 0;
			if (!CAS(&slots[i].inuse, expected, 1))
				continue;

			tc_fiber->ready(f);

			atomic_store(&slots[i].fiber, 0, MEMORY_RELEASE);
		}
	}
}

static bool counter_add_to_waiting(tc_fut_t* c, size_t value)
{
	fiber_t* f = tc_fiber->this();
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		size_t expected = 0;
		if (!CAS(&slots[i].fiber, expected, f))
			continue;

		slots[i].value = value;
		// Signal slot is ready
		atomic_store(&slots[i].inuse, 0, MEMORY_ACQ_REL);

		size_t probed = atomic_load(&c->value, MEMORY_RELAXED);
		if (atomic_load(&slots[i].inuse, MEMORY_ACQUIRE))
			return false;

		if (slots[i].value == probed) {
			expected = 0;
			if (!CAS(&slots[i].inuse, expected, 1))
				return false;

			//Slot is now free, in use slays true
			atomic_store(&slots[i].fiber, 0, MEMORY_RELEASE);
			return true;
		}
		return false;
	}
	return false;
}

tc_fut_t* counter_create(tc_allocator_i* a, size_t value, tc_waitable_i* waitable, uint32_t num_slots)
{
	tc_fut_t* c = tc_malloc(a, sizeof(tc_fut_t) + num_slots * sizeof(fiber_entry));
	memset(c, 0, sizeof(tc_fut_t) + num_slots * sizeof(fiber_entry));
	c->a = a;
	c->num_slots = num_slots;
	c->waitable = waitable;
	atomic_store(&c->value, value, MEMORY_RELAXED);
	fiber_entry* slots = (fiber_entry*)(c + 1);
	for (uint32_t i = 0; i < c->num_slots; i++) {
		atomic_store(&slots[i].inuse, 1, MEMORY_RELEASE);
	}
	return c;
}

void counter_free(tc_fut_t* c)
{
	if (c->waitable && c->waitable->dtor && c->waitable->instance)
		c->waitable->dtor(c->waitable->instance);
	tc_free(c->a, c, sizeof(tc_fut_t) + c->num_slots * sizeof(fiber_entry));
}

void counter_set(tc_fut_t* c, size_t value)
{
	atomic_store(&c->value, value, MEMORY_RELAXED);
}

size_t counter_increment(tc_fut_t* c)
{
	size_t val = atomic_fetch_add(&c->value, 1, MEMORY_RELAXED) + 1;
	counter_wakeup(c, val);
	return val;
}

size_t counter_decrement(tc_fut_t* c)
{
	size_t val = atomic_fetch_add(&c->value, -1, MEMORY_RELAXED) - 1;
	counter_wakeup(c, val);
	return val;
}

int64_t counter_wait(tc_fut_t* c, size_t value)
{
	bool done = counter_add_to_waiting(c, value);
	if (!done)
		tc_fiber->yield(NULL);
	return c->waitable->results;
}

int64_t counter_wait_and_free(tc_fut_t* c, size_t value)
{
	int64_t result = counter_wait(c, value);
	counter_free(c);
	return result;
}

tc_future_i* tc_future = &(tc_future_i) {
	.create = counter_create,
	.wait = counter_wait,
	.free = counter_free,
	.wait_and_free = counter_wait_and_free,
	.decrement = counter_decrement,
	.increment = counter_increment
};
