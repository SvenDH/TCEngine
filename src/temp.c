/*==========================================================*/
/*					TEMPORARY ALLOCATOR		    			*/
/*==========================================================*/
#include "memory.h"
#include "temp.h"
#include "tcmath.h"

typedef struct temp_internal_s {
	uint64_t used;
	uint64_t cap;
	uint8_t* end;
} temp_internal_t;

typedef struct temp_node_s {
	uint64_t size;
	void* next;
} temp_node_t;


void temp_realloc(tc_temp_t* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line);

tc_allocator_i* temp_init(tc_temp_t* a, tc_allocator_i* parent)
{
	a->instance = a;
	a->alloc = temp_realloc;
	a->parent = parent ? parent : tc_memory->vm;
	temp_internal_t* temp = (temp_internal_t*)a->buffer;
	temp->used = sizeof(temp_internal_t);
	temp->cap = sizeof(a->buffer);
	temp->end = (temp + 1);
	a->next = NULL;
	return a;
}

void temp_realloc(tc_temp_t* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line)
{
	temp_internal_t* temp = (temp_internal_t*)a->buffer;
	if (new_size > old_size) {
		if (temp->used + new_size > temp->cap) {
			size_t size = min(CHUNK_SIZE, next_power_of_2(new_size + sizeof(temp_node_t)));
			temp_node_t* node = tc_malloc(a->parent, size);
			node->size = size;
			node->next = a->next;
			a->next = node;
			temp->used = sizeof(temp_node_t);
			temp->cap = size;
			temp->end = (node + 1);
		}
		ptr = temp->end;
		temp->used += new_size;
		temp->end += new_size;
	}
	return ptr;
}

void temp_close(tc_temp_t* a)
{
	temp_node_t* node = a->next;
	while (node) {
		temp_node_t* ptr = node;
		node = node->next;
		tc_free(a->parent, ptr, ptr->size);
	}
}

tc_temp_i* tc_temp = &(tc_temp_i) {
	.init = temp_init,
	.close = temp_close
};
