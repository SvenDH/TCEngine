/*==========================================================*/
/*						BUDDY ALLOCATOR						*/
/*==========================================================*/
#include "types.h"

#define TC_BUDDY_MODULE_NAME "tc_buddy_module"

/* Buddy allocator function definitions: */

typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_buddy_i {
	/*
	 * Allocates number of bytes with power of 2 and minimum size of 64 bytes
	 */
	tc_allocator_i* (*create)(tc_allocator_i* a, uint32_t size, uint32_t min_size);
	/*
	 * Free all resources used by the cache
	 */
	void (*destroy)(tc_allocator_i* a);

} tc_buddy_i;


extern tc_buddy_i* tc_buddy;
