/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#include "private_types.h"


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
	return realloc(ptr, new_size);
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
