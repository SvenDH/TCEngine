/*==========================================================*/
/*					STRETCHY BUFFERS						*/
/*==========================================================*/
#pragma once
#include "memory.h"

/* Vector / Growable / "Stretchy" buffer functions: */

inline void* _buff_growf(void* arr, int increment, int elemsize, tc_allocator_i* a, const char* file, uint32_t line);

#define buff_free(a,alloc)      ((a) ? tc_free(alloc, _buff_raw(a), _buff_m(a) * sizeof(*a) + 2 * sizeof(uint32_t)), 0 : 0)
#define buff_push(a,alloc, ...) (_buff_maygrow(a,1,alloc), (a)[_buff_n(a)++] = (##__VA_ARGS__))
#define buff_pop(a)				((a)[--_buff_n(a)])
#define buff_count(a)			((a) ? _buff_n(a) : 0)
#define buff_add(a,n,alloc)     (_buff_maygrow((a),(n),(alloc)), _buff_n(a)+=(n), &(a)[_buff_n(a)-(n)])
#define buff_last(a)			((a)[_buff_n(a)-1])

#define _buff_raw(a) ((uint32_t*)(void*)(a) - 2)
#define _buff_m(a)   _buff_raw(a)[0]
#define _buff_n(a)   _buff_raw(a)[1]

#define _buff_needgrow(a, n) ((a)==0 || _buff_n(a)+(n) >= _buff_m(a))
#define _buff_maygrow(a, n, alloc)	 (_buff_needgrow(a,(n)) ? _buff_grow(a,n,alloc) : 0)
#define _buff_grow(a, n, alloc)     (*((void**)&(a)) = _buff_growf((a),(n),sizeof(*(a)),(alloc),__FILE__,__LINE__))

/* Vector / Growable buffer functions: */
inline void* _buff_growf(void* arr, int increment, int elemsize, tc_allocator_i* a, const char* file, uint32_t line) {
	uint32_t old_size = arr ? _buff_m(arr) : 0;
	uint32_t new_size = old_size * 2;
	uint32_t min_needed = buff_count(arr) + increment;
	uint32_t size = new_size > min_needed ? new_size : min_needed;
	uint32_t* ptr = (uint32_t*)a->alloc(
		a,
		arr ? _buff_raw(arr) : 0,
		(size_t)old_size * elemsize + (arr ? sizeof(uint32_t) * 2 : 0), 
		(size_t)size * elemsize + sizeof(uint32_t) * 2,
		file,
		line
	);
	if (ptr) {
		if (!arr)
			ptr[1] = 0;
		ptr[0] = size;
		return ptr + 2;
	}
	// Try to force a NULL pointer exception later
	else return (void*)(2 * sizeof(uint32_t)); 
}
