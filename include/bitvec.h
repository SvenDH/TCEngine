/*==========================================================*/
/*							BITARRAY						*/
/*==========================================================*/
#pragma once
#include "core.h"

/* Bit array macros */
#define NUM_BITS (8 * sizeof(size_t))
#if PTR_SIZE == 8
#define INDEX_SHIFT 6ULL
#elif PTR_SIZE == 4
#define INDEX_SHIFT 5ULL
#endif

/* Bit array functions */
static INLINE void bit_set(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] |= 1ULL << (index & (NUM_BITS - 1ULL));
}

static INLINE void bit_clear(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] &= ~(1ULL << (index & (NUM_BITS - 1ULL)));
}

static INLINE void bit_toggle(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] ^= 1ULL << (index & (NUM_BITS - 1ULL));
}

static INLINE bool bit_test(size_t* array, uint32_t index)
{
	return (array[index >> INDEX_SHIFT] & (1ULL << (index & (NUM_BITS - 1ULL)))) != 0;
}
