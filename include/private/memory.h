/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#pragma once
#include "types.h"

#define TC_ALLOCATION_MODULE_NAME "tc_allocation_module"


/* Memory related constants: */
enum {
	SLAB_MIN_SIZE = ((size_t)USHRT_MAX) + 1,// Smallest possible slab size (64k)
	SLAB_UNLIMITED = SIZE_MAX / 2 + 1,		// The largest allowed amount of memory of a single arena
	CHUNK_SIZE = 4096,						// 4KB page size
	GLOBAL_BUFFER_SIZE = 536870912,			// 512MB global buffer size
	GLOBAL_SLAB_SIZE = 4 * 1024 * 1024,		// 4MB 
};

typedef struct tc_registry_i tc_registry_i;
typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_allocator_i {
	/* 
	 * Allocator instance
	 */
	void* instance;

	/* Generic allocation function of an allocator
	 * allocator is the allocator context
	 * ptr a pointer to resize or free or a NULL for new allocations
	 * prev_size is the size of the memory block ptr is pointing at
	 * new_size is the requested size of the block after the call
	 * file is the file where the allocation occurs
	 * line is the line number in the file where the allocation occurs
	 */
	void* (*alloc)(tc_allocator_i* a, void* ptr, size_t prev_size, size_t new_size, const char* file, uint32_t line);
} tc_allocator_i;

#define tc_malloc(_a, _s) (_a)->alloc(_a, NULL, 0, _s, __FILE__, __LINE__)
#define tc_malloc_at(_a, _s, file, line) (_a)->alloc(_a, NULL, 0, _s, file, line)
#define tc_free(_a, _p, _s) (_a)->alloc(_a, _p, _s, 0, __FILE__, __LINE__)
#define tc_realloc(_a, _p, _prev, _new) (_a)->alloc(_a, _p, _prev, _new, __FILE__, __LINE__)


typedef struct tc_memory_i {

	tc_allocator_i* system;

	tc_allocator_i* vm;

	tc_allocator_i (*create_child)(const tc_allocator_i* parent, const char* name);

	void (*destroy_child)(const tc_allocator_i* parent);

} tc_memory_i;


extern tc_memory_i* tc_memory;
