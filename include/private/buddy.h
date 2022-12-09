/*==========================================================*/
/*						BUDDY ALLOCATOR						*/
/*==========================================================*/
#include "types.h"

/** Buddy allocator function definitions: */
typedef struct tc_allocator_i tc_allocator_i;

/** Allocates number of bytes with power of 2 and minimum size of 64 bytes */
tc_allocator_i* tc_buddy_new(tc_allocator_i* a, uint32_t size, uint32_t min_size);

/** Free all resources used by the cache */
void tc_buddy_free(tc_allocator_i* a);
