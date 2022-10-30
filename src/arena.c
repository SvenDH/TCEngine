/*==========================================================*/
/*						ARENA ALLOCATOR						*/
/*==========================================================*/
#include "memory.h"
#include "tcatomic.h"
#include "tcmath.h"
#include "lflifo.h"

/* Thread save slab allocator: */

typedef struct slab_arena_s {
	atomic_t free;							// Lock-free free list of slabs that are free
	char* arena;							// A preallocated arena of size = prealloc
	size_t cap;								// How much memory is available
	atomic_t used;							// How much memory in the arena has been used
	uint32_t slab_size;						// Size of each allocation
} slab_arena_t;


static inline size_t _align_slab(size_t size, size_t align);

void* arena_alloc(
	tc_allocator_i* a, 
	void* ptr, 
	size_t prev_size, 
	size_t new_size, 
	const char* file, 
	uint32_t line)
{
	slab_arena_t* arena = a->instance;
	if (ptr == NULL && new_size > 0) {
		TC_ASSERT(new_size == arena->slab_size);

		if ((ptr = lf_lifo_pop(&arena->free))) 
			return ptr;
		
		size_t used = atomic_fetch_add(&arena->used, arena->slab_size, MEMORY_ACQ_REL);
		used += arena->slab_size;
		if (used <= arena->cap) {
			ptr = arena->arena + used - arena->slab_size;
			return ptr;
		}
	}
	else {
		if (ptr == NULL) 
			return NULL;
		TC_ASSERT(prev_size == arena->slab_size);

		lf_lifo_push(&arena->free, ptr);
	}
	return NULL;
}

tc_allocator_i arena_create(uint32_t total_size, uint32_t slab_size)
{
	slab_arena_t* arena = malloc(sizeof(slab_arena_t));
	lf_lifo_init(&arena->free);
	arena->slab_size = next_power_of_2(max(slab_size, SLAB_MIN_SIZE));
	total_size = min(total_size, SIZE_MAX - arena->slab_size);
	arena->cap = total_size;
	arena->used = (atomic_t){ 0 };
	arena->arena = mem_map(arena->cap);
	TC_ASSERT(((size_t)arena->arena % SLAB_MIN_SIZE) == 0);
	return (tc_allocator_i) { .instance = arena, .alloc = arena_alloc };
}

void arena_destroy(tc_allocator_i* a)
{
	slab_arena_t* arena = a->instance;
	if (arena->arena)
		mem_unmap(arena->arena, arena->cap);
}

static inline
size_t _align_slab(size_t size, size_t align)
{
	// Must be a power of two
	TC_ASSERT((align & (align - 1)) == 0);
	// Bit arithmetics won't work for a large size
	TC_ASSERT(size <= SIZE_MAX - align);
	return (size + align - 1) & ~(align - 1);
}


