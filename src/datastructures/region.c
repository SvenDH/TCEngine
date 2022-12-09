/*==========================================================*/
/*					REGION ALLOCATOR						*/
/*==========================================================*/
#include "private_types.h"


typedef struct region_s {
	tc_allocator_i base;
	lf_lifo_t slabs;
	tc_allocator_i* parent;
} region_t;

/* Header struct for each region block */
struct rslab {
	lf_lifo_t next;
	size_t size;
	uintptr_t head;
};

static inline uintptr_t _align_ptr(uintptr_t* ptr, uintptr_t align);

static void* region_alloc(tc_allocator_i* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line);

/* Region allocator functions: */

tc_allocator_i* region_create(tc_allocator_i* base) {
	region_t* region = tc_malloc(base, sizeof(region_t));
	region->parent = base;
	region->slabs.next = 0;
	region->base.instance = region;
	region->base.alloc = region_alloc;
	return &region->base;
}

void* region_aligned_alloc(region_t* region, size_t size, size_t align) {
	struct rslab* r = lf_lifo(region->slabs.next);
	if (lf_lifo_is_empty(&region->slabs) || (_align_ptr(r->head, align) + size - (size_t)r < r->size)) {
		// If no region exists or region is too full allocate new slab
		size_t slab_size = max(next_power_of_2(size + sizeof(struct rslab)), SLAB_MIN_SIZE);
		r = tc_malloc(region->parent, slab_size);
		r->size = slab_size;
		r->head = (size_t)r + sizeof(struct rslab);
		lf_lifo_push(&region->slabs, r);
	}
	size_t ptr = _align_ptr(r->head, align);
	r->head = ptr + size;
	return ptr;
}

static
void* region_alloc(tc_allocator_i* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line) {
	region_t* region = a->instance;
	if (!ptr && new_size > 0) {
		return region_aligned_alloc(region, new_size, 4);
	}
	else {
		TC_ASSERT(old_size == 0, "[Memory]: Freeing from region allocator is not implemented");
	}
	return NULL;
}

void region_clear(tc_allocator_i* a) {
	region_t* region = a->instance;
	for (;;) {
		struct rslab* r = lf_lifo_pop(&region->slabs);
		if (r) tc_free(region->parent, r, r->size);
		else return;
	}
}

// Align upwards, align should be power of 2
static inline uintptr_t _align_ptr(uintptr_t* ptr, uintptr_t align) {
	if (align == 0) return ptr;
	TC_ASSERT(align > 0 && (align & (align - 1)) == 0);	// Power of 2
	TC_ASSERT(ptr != NULL);
	uintptr_t addr = (uintptr_t)ptr;
	addr = (addr + (align - 1)) & -align;   // Round up to align-byte boundary
	TC_ASSERT(addr >= (uintptr_t)ptr);
	return addr;
}

tc_region_i* tc_region = &(tc_region_i) {
	.create = region_create,
	.destroy = region_clear
};
