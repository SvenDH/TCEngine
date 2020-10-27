/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#pragma once
#include "tccore.h"
#include "tctypes.h"
#include "tcatomic.h"
#include "tcthread.h"

//#include <limits.h>

/*==========================================================*/
/*							SLAB ARENA						*/
/*==========================================================*/

// Slab arena for preallocating a lot 

enum {
	SLAB_MIN_SIZE = ((size_t)USHRT_MAX) + 1,		// Smallest possible slab size (64k)
	SLAB_UNLIMITED = SIZE_MAX / 2 + 1				// The largest allowed amount of memory of a single arena
};

typedef struct slab_arena_s {
	atomic_t free;									// Lock-free free list of slabs that are free
	char* arena;									// A preallocated arena of size = prealloc
	size_t cap;										// How much memory is available
	atomic_t used;									// How much memory in the arena has been used
	uint32_t slab_size;								// Size of each allocation
} slab_arena_t;

int arena_init(slab_arena_t* arena, size_t total_size, uint32_t slab_size);

void* arena_alloc(slab_arena_t* arena);

void arena_free(slab_arena_t* arena, void* ptr);

void arena_destroy(slab_arena_t* arena);

/*==========================================================*/
/*						BUDDY ALLOCATOR						*/
/*==========================================================*/

/* Buddy allocator type: */

typedef struct slab_cache_s {
	size_t cap;								// Biggest allocation size that takes up the whole buffer
	uint32_t min_size;						// Minimal size of an allocation
	uint32_t nr_levels;						// Total number of levels (number block/allocation sizes)
	uint32_t num_blocks;					// Maximum number of indices for blocks
	uint32_t slab_level;					// Slab size of arena (maximum size of allocation)
	char** data;							// Track used slabs
	listnode_t* free_lists;					// Free list array per level
	uint32_t* merge_bits;					// Bit vector for tracking which blocks are allocated
	uint32_t* split_bits;					// Bit vector for tracking which blocks are split
	slab_arena_t* arena;					// Base allocator from which to allocate slabs
	fiber_t* fiber;
} slab_cache_t;

/* Buddy allocator function definitions: */

typedef struct cache_params {
	size_t size;
	fiber_t* fiber;
	slab_arena_t* arena;
} cache_params;

// Initialize the buddy allocator with a slab aeran as base allocator
slab_cache_t cache_create(cache_params params);

// Allocate number of bytes with power of 2 and minimum size of 64 bytes
void* cache_alloc(slab_cache_t* ba, size_t size);

// If we know the amount of bytes we want to free use this function
void cache_release(slab_cache_t* ba, void* ptr, size_t size);

// If we do not know the amount to free we use this function
void cache_free(slab_cache_t* ba, void* ptr);

// Used to resize a piece of memory
void* cache_realloc(slab_cache_t* ba, void* ptr, size_t size);

// Free all resources used by the cache
void cache_destroy(slab_cache_t* ba);


/*==========================================================*/
/*					RESOURCE ALLOCATOR						*/
/*==========================================================*/

#define CHUNK_SIZE 4096

typedef struct resourceallocator_t {
	slab_cache_t* allocator;
	byte** chunks;
	uint32_t* freelist;
	uint32_t* entries;
	uint32_t element_size;
	uint32_t elements_per_chunk;
	uint32_t cap;
	uint32_t used;
} resourceallocator_t;

/* Resource allocator functions */
void res_init(resourceallocator_t* res, size_t element_size, slab_cache_t* base);

rid_t res_alloc(resourceallocator_t* res);

void* res_get(resourceallocator_t* res, rid_t id);

void res_free(resourceallocator_t* res, rid_t id);

bool res_owns(resourceallocator_t* res, rid_t id);


/*==========================================================*/
/*						GLOBAL MEMORY						*/
/*==========================================================*/

#define GLOBAL_BUFFER_SIZE 536870912 //512MB
#define GLOBAL_SLAB_SIZE 4 * 1024 * 1024 //4MB

extern slab_arena_t global_arena;
extern size_t pagesize;
extern int stack_dir;

void memory_init(void);

void memory_free(void);