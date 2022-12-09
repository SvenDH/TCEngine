/*==========================================================*/
/*						HEAP MANAGER						*/
/*==========================================================*/
#pragma once
#include "slab.h"
#include "tree.h"

typedef struct tc_allocator_i tc_allocator_i;

struct offsetblock_t {
	rbnode_t offset;
	rbnode_t size;
	slab_obj_t;
};

typedef struct offsetheap_s {
	size_t cap;
	size_t free;
	size_t alignment;
	rbtree_t offsetmap;
	rbtree_t sizemap;
	struct offsetblock_t* blocks;
} offsetheap_t;

typedef struct allocation_s {
	size_t offset;
	size_t size;
} allocation_t;

#define INVALID_OFFSET ~0
#define INVALID_ALLOCATION (allocation_t) { INVALID_OFFSET, 0 }

inline bool allocation_is_valid(allocation_t a) {
	return !(a.offset == ~0 && a.size == 0); 
}

inline bool heap_empty(offsetheap_t* mgr);

static inline
void heap_newblock(offsetheap_t* h, size_t offset, size_t size) {
	struct offsetblock_t* block = slab_alloc(h->blocks);
	block->offset.data = offset;
	block->size.data = size;
	rb_insert(&h->offsetmap, &block->offset);
	rb_insert(&h->sizemap, &block->size);
}

#ifdef TC_DEBUG
static inline
void heap_verify(offsetheap_t* h) {
	size_t free = 0;
	TC_ASSERT(is_power_of_2(h->alignment));
	rbnode_t* it = rb_begin(&h->offsetmap);
	struct offsetblock_t* prev = NULL;
	while (it != rb_end(&h->offsetmap)) {
		struct offsetblock_t* block = rb_next(&it);
		TC_ASSERT(block != NULL);
		size_t offset = block->offset.data;
		TC_ASSERT(offset >= 0 && offset + block->size.data <= h->cap);
		TC_ASSERT((offset & (h->alignment - 1)) == 0);
		if (offset + block->size.data < h->cap) {
			TC_ASSERT((block->size.data & (h->alignment - 1)) == 0);
		}
		TC_ASSERT(prev == NULL || block->offset.data > prev->offset.data + prev->size.data);
		free += block->size.data;
		prev = block;
	}
	TC_ASSERT(free == h->free);
}
#endif

inline offsetheap_t heap_init(size_t size, tc_allocator_i* allocator) {
	offsetheap_t h = { .cap = size, .free = size };
	h.offsetmap = rb_init();
	h.sizemap = rb_init();
	slab_create(&h.blocks, allocator, CHUNK_SIZE);
	heap_newblock(&h, 0, size);
	for (h.alignment = 1; h.alignment * 2 <= h.cap; h.alignment *= 2);
#ifdef TC_DEBUG
	heap_verify(&h);
#endif
	return h;
}

inline allocation_t heap_alloc(offsetheap_t* h, size_t size, uint32_t alignment) {
	TC_ASSERT(size > 0);
	TC_ASSERT(is_power_of_2(alignment));
	size = align_up(size, alignment);
	if (h->free < size) {
		return INVALID_ALLOCATION;
	}
	size_t extra = (alignment > h->alignment) ? alignment : 0;
	rbnode_t* it = rb_lower_bound(&h->sizemap, size + extra);
	if (it == rb_end(&it)) {
		return INVALID_ALLOCATION;
	}
	struct offsetblock_t* block = container_of(rb_next(&it), struct offsetblock_t, size);
	TC_ASSERT(size + extra <= block->size.data);
	size_t offset = block->offset.data;
	TC_ASSERT(offset % h->alignment == 0);
	size_t size2 = size + align_up(offset, alignment) - offset;
	TC_ASSERT(size2 <= size + extra);
	size_t newoffset = offset + size2;
	size_t newsize = block->size.data - size2;
	rb_remove(&h->sizemap, &block->size);
	rb_remove(&h->offsetmap, &block->offset);
	slab_free(h->blocks, block);
	if (newsize > 0) {
		heap_newblock(h, newoffset, newsize);
	}
	h->free -= size2;
	if ((size & (h->alignment - 1)) != 0) {
		if (is_power_of_2(size)) {
			TC_ASSERT(size >= alignment && size < h->alignment);
			h->alignment = size;
		}
		else {
			h->alignment = min(h->alignment, alignment);
		}
	}
#ifdef TC_DEBUG
	heap_verify(h);
#endif
	return (allocation_t) { offset, size2 };
}

inline void heap_free(offsetheap_t* h, allocation_t* a) {
	size_t offset = a->offset, size = a->size;
	TC_ASSERT(!(offset == ~0 && size == 0) && size <= h->cap);
	rbnode_t* it = rb_upper_bound(&h->offsetmap, offset);
#ifdef TC_DEBUG
	TC_ASSERT(it == rb_lower_bound(&h->offsetmap, offset));
#endif
	TC_ASSERT(it == rb_end(&h->offsetmap) || offset + size <= it->data);
	struct offsetblock_t* prev, * block;
	prev = block = container_of(it, struct offsetblock_t, offset);
	if (prev != (struct offsetblock_t*)rb_min(h->offsetmap.root)) {
		rb_prev(&prev);
		TC_ASSERT(offset >= prev->offset.data + prev->size.data);
	}
	else prev = rb_end(&h->offsetmap);
	size_t newsize = size;
	size_t newoffset = offset;
	if (prev != (struct offsetblock_t*)rb_end(&h->offsetmap) &&
		offset == prev->offset.data + prev->size.data) {
		newsize += prev->size.data;
		newoffset = prev->offset.data;
		if (block != (struct offsetblock_t*)rb_end(&h->offsetmap) && offset + size == block->offset.data) {
			newsize += block->size.data;
			rb_remove(&h->sizemap, &block->size);
			rb_remove(&h->offsetmap, &block->offset);
			slab_free(h->blocks, block);
		}
		rb_remove(&h->sizemap, &prev->size);
		rb_remove(&h->offsetmap, &prev->offset);
		slab_free(h->blocks, prev);
	}
	else if (block != (struct offsetblock_t*)rb_end(&h->offsetmap) && offset + size == block->offset.data) {
		newsize += block->size.data;
		rb_remove(&h->sizemap, &block->size);
		rb_remove(&h->offsetmap, &block->offset);
		slab_free(h->blocks, block);
	}
	heap_newblock(h, newoffset, newsize);
	h->free += size;
	if (heap_empty(h)) {
		TC_ASSERT(rb_size(&h->offsetmap) == 1);
		for (h->alignment = 1; h->alignment * 2 <= h->cap; h->alignment *= 2);
	}
#ifdef TC_DEBUG
	heap_verify(h);
#endif
}

inline bool heap_empty(offsetheap_t* mgr) {
	return mgr->free == mgr->cap;
}

inline bool heap_full(offsetheap_t* mgr) {
	return mgr->free == 0;
}

inline size_t heap_size(offsetheap_t* mgr) {
	return mgr->cap;
}

inline size_t heap_used(offsetheap_t* mgr) {
	return mgr->cap - mgr->free;
}