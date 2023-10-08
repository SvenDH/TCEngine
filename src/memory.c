/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#include "private_types.h"

#include <rmem.h>

#define ENABLE_MTUNER

#ifdef ENABLE_MTUNER
#define MTUNER_ALLOC(_handle, _ptr, _size, _overhead) rmemAlloc((_handle), (_ptr), (uint32_t)(_size), (uint32_t)(_overhead))
#define MTUNER_ALIGNED_ALLOC(_handle, _ptr, _size, _overhead, _align) \
	rmemAllocAligned((_handle), (_ptr), (uint32_t)(_size), (uint32_t)(_overhead), (uint32_t)(_align))
#define MTUNER_REALLOC(_handle, _ptr, _size, _overhead, _prevPtr) \
	rmemRealloc((_handle), (_ptr), (uint32_t)(_size), (uint32_t)(_overhead), (_prevPtr))
#define MTUNER_FREE(_handle, _ptr) rmemFree((_handle), (_ptr))
#else
#define MTUNER_ALLOC(_handle, _ptr, _size, _overhead)
#define MTUNER_ALIGNED_ALLOC(_handle, _ptr, _size, _overhead, _align)
#define MTUNER_REALLOC(_handle, _ptr, _size, _overhead, _prevPtr)
#define MTUNER_FREE(_handle, _ptr)
#endif




static
tc_allocator_i allocator_create_child(const tc_allocator_i* parent, const char* name)
{
	tc_allocator_i a = { 0 };

	return a;
}

static
void allocator_destroy_child(const tc_allocator_i* a)
{

}

static 
void* vm_alloc(tc_allocator_i* a, void* ptr, size_t prev_size, size_t new_size, const char* file, uint32_t line)
{
	void* new_ptr = NULL;
	if (new_size) {
		new_ptr = tc_os->map(new_size);
		if (prev_size) memcpy(new_ptr, ptr, prev_size);
	}
	if (prev_size) tc_os->unmap(ptr, prev_size);
	return new_ptr; 
}


static
void* system_alloc(tc_allocator_i* a, void* ptr, size_t prev_size, size_t new_size, const char* file, uint32_t line)
{
	if (ptr != NULL && new_size == 0) {
		MTUNER_FREE(0, ptr);
		free(ptr);
		return NULL;
	}
	void* new_ptr = realloc(ptr, new_size);
	MTUNER_REALLOC(0, new_ptr, new_size, 0, ptr);
	return new_ptr;
}

static tc_allocator_i system_allocator = {
	.instance = NULL,
	.alloc = system_alloc,
};

static tc_allocator_i vm_allocator = {
	.instance = NULL,
	.alloc = vm_alloc,
};

tc_memory_i* tc_mem = &(tc_memory_i) {
	.sys = &system_allocator,
	.vm = &vm_allocator,
	.create_child = allocator_create_child,
	.destroy_child = allocator_destroy_child,
};
