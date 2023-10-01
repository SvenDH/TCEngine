/*==========================================================*/
/*							BITARRAY						*/
/*==========================================================*/
#pragma once

/** Bit array macros */
#define NUM_BITS (8 * sizeof(size_t))
#define INDEX_SHIFT 6ULL

/** Bit array functions */
static inline void bit_set(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] |= 1ULL << (index & (NUM_BITS - 1ULL));
}

static inline void bit_clear(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] &= ~(1ULL << (index & (NUM_BITS - 1ULL)));
}

static inline void bit_toggle(size_t* array, uint32_t index)
{
	array[index >> INDEX_SHIFT] ^= 1ULL << (index & (NUM_BITS - 1ULL));
}

static inline bool bit_test(size_t* array, uint32_t index)
{
	return (array[index >> INDEX_SHIFT] & (1ULL << (index & (NUM_BITS - 1ULL)))) != 0;
}
