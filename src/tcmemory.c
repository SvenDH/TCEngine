/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#include "tcmemory.h"
#include "tcdata.h"

/*==========================================================*/
/*							MACROS AND UTILS				*/
/*==========================================================*/

// Align upwards, align should be power of 2
static inline uintptr_t align_ptr(uintptr_t* ptr, uintptr_t align) {
	if (align == 0) return ptr;
	TC_ASSERT(align > 0 && (align & (align - 1)) == 0);	// Power of 2
	TC_ASSERT(ptr != NULL);
	uintptr_t addr = (uintptr_t)ptr;
	addr = (addr + (align - 1)) & -align;   // Round up to align-byte boundary
	TC_ASSERT(addr >= (uintptr_t)ptr);
	return addr;
}

/*==========================================================*/
/*							ALLOCATORS						*/
/*==========================================================*/

/* Thread save slab allocator: */
static inline size_t align_slab(size_t size, size_t alignment) {
	TC_ASSERT((alignment & (alignment - 1)) == 0); // Must be a power of two
	TC_ASSERT(size <= SIZE_MAX - alignment); // Bit arithmetics won't work for a large size
	return (size - 1 + alignment) & ~(alignment - 1);
}

int arena_init(slab_arena_t* arena, size_t total_size, uint32_t slab_size) {
	lf_lifo_init(&arena->free);
	arena->slab_size = next_power_of_2(max(slab_size, SLAB_MIN_SIZE));
	total_size = min(total_size, SIZE_MAX - arena->slab_size);
	arena->cap = total_size;
	arena->used = (atomic_t){ 0 };
	arena->arena = aligned_alloc(arena->slab_size, arena->cap);
	return arena->cap && !arena->arena ? -1 : 0;
}

void* arena_alloc(slab_arena_t* arena) {
	void* ptr;
	if ((ptr = lf_lifo_pop(&arena->free))) {
		return ptr;
	}
	size_t used = atomic_fetch_add(&arena->used, arena->slab_size, MEMORY_ACQ_REL);
	used += arena->slab_size;
	if (used <= arena->cap) {
		ptr = arena->arena + used - arena->slab_size;
		return ptr;
	}
	return NULL;
}

void arena_free(slab_arena_t* arena, void* ptr) {
	if (ptr == NULL) return;
	lf_lifo_push(&arena->free, ptr);
}

void arena_destroy(slab_arena_t* arena) {
	if (arena->arena)
		aligned_free(arena->arena);
}


/* Buddy allocator functions: */

#define MIN_LEVEL 4										// Minimum of 16 bytes
#define MIN_BUDDY_SIZE (1 << MIN_LEVEL)

static void* _buddy_alloc_block(slab_cache_t* ba, uint32_t level);

static inline uint32_t _size_at_level(slab_cache_t* ba, uint32_t level) { return ba->cap / (1 << level); }

static inline int32_t _level_at_size(slab_cache_t* ba, uint32_t size) {
	if (size < ba->min_size)
		return ba->nr_levels - 1;
	size = next_power_of_2(size) / ba->min_size;
	return ba->nr_levels - log2_32(size) - 1;
}

static inline uint32_t _block_index(slab_cache_t* ba, uint32_t offset, uint32_t level) {
	return (1UL << level) + offset / _size_at_level(ba, level) - 1UL;
}

static inline uint32_t _get_buddy_offset(slab_cache_t* ba, uint32_t offset, uint32_t level) {
	uint32_t size = _size_at_level(ba, level);
	if (_block_index(ba, offset, level) & 1)
		return (offset & ~(size - 1)) + size;				// Offset is odd block
	else
		return (offset & ~(size - 1)) - size;				// Offset is even block
}

static inline void* _offset_to_ptr(slab_cache_t* ba, uint32_t offset) {
	uint32_t slab_size = _size_at_level(ba, ba->slab_level);
	uint16_t slab = offset / slab_size;
	if (ba->data[slab])
		return ba->data[slab] + (offset % slab_size);		//TODO: Make this faster with bitshift
	return NULL;
}

static inline int32_t _ptr_to_offset(slab_cache_t* ba, void* ptr) {
	uint32_t slab_size = _size_at_level(ba, ba->slab_level);
	for (int i = 0; i < ba->cap / slab_size; i++)
		if (ba->data[i] && ptr >= ba->data[i] && ptr < ba->data[i] + slab_size) 
			return (i * slab_size) + ((char*)ptr - ba->data[i]);
	TC_ASSERT(0, "[Memory]: Could not find allocated block");
	return -1;
}

static void _cache_meta_alloc(slab_cache_t* ba) {
	// Calculate array sizes
	size_t slabarraysize = (ba->cap / ba->arena->slab_size) * sizeof(char*);
	size_t freelistsize = (ba->nr_levels) * sizeof(listnode_t);
	size_t bitvecsize = ((ba->num_blocks / 2 / NUM_BITS) + 1) * sizeof(uint32_t);
	// Check if one slab can contain all arrays
	TC_ASSERT(slabarraysize + freelistsize + 2 * bitvecsize < ba->arena->slab_size);
	// Assign arrays
	ba->data = (char**)arena_alloc(ba->arena);
	TC_ASSERT(ba->data);
	ba->free_lists = ((char*)ba->data) + slabarraysize;
	ba->merge_bits = ((char*)ba->data) + slabarraysize + freelistsize;
	ba->split_bits = ((char*)ba->data) + slabarraysize + freelistsize + bitvecsize;
	// Initialize data array to NULLs
	for (int i = 0; i < ba->cap / ba->arena->slab_size; i++) ba->data[i] = NULL;
	// Initialize free list
	for (int i = 0; i < ba->nr_levels; i++) list_init(&ba->free_lists[i]);
	// Initialize bit arrays
	memset(ba->merge_bits, 0, bitvecsize);
	memset(ba->split_bits, 0, bitvecsize);
	// Split all blocks that are larger than a slab
	for (int i = 0; i < ba->slab_level; i++)
		for (int j = 0; j < ba->cap; j += _size_at_level(ba, i))
			bit_set(ba->split_bits, _block_index(ba, j, i));

	size_t metablocks = slabarraysize + freelistsize + 2 * bitvecsize;

	ba->data[0] = ba->data;
	uint32_t offset, level, index;
	for (offset = 0; offset < metablocks + ba->min_size; offset += ba->min_size) {
		level = ba->slab_level + 1;
		while (level < ba->nr_levels - 1) {
			index = _block_index(ba, offset, level - 1);
			TC_ASSERT(index < ba->num_blocks / 2);
			bit_set(ba->split_bits, index);
			level++;
		}
	}
	level = ba->slab_level + 1;
	offset = metablocks;
	while (level < ba->nr_levels - 1) {
		index = _block_index(ba, offset, level - 1);
		TC_ASSERT(index < ba->num_blocks / 2);
		uint32_t buddy_offset = _get_buddy_offset(ba, offset, level);
		if (buddy_offset > offset) {
			TC_ASSERT(buddy_offset > 0 && buddy_offset < ba->cap);
			bit_toggle(ba->merge_bits, index);
			list_add_tail(&ba->free_lists[level], ((char*)ba->data) + buddy_offset);
		}
		level++;
	}
	/*
	for (uint32_t offset = 0; offset < metablocks + ba->min_size; offset += ba->min_size) {
		void* ptr = cache_alloc(ba, ba->min_size);
		TC_ASSERT(ptr == ba->data[0] + offset);
	}
	// Add tracking arrays to allocation
	memcpy(ba->data[0], ba->data, metablocks);
	void* ptr = ba->data;
	listnode_t* old_free = ba->free_lists;
	ba->data = ba->data[0];
	ba->free_lists = ((char*)ba->data) + slabarraysize;
	ba->merge_bits = ((char*)ba->data) + slabarraysize + freelistsize;
	ba->split_bits = ((char*)ba->data) + slabarraysize + freelistsize + bitvecsize;
	// Fix pointers
	for (int i = 0; i < ba->nr_levels; i++) {
		if (!list_empty(&old_free[i])) {
			old_free[i].next->prev = &ba->free_lists[i];
			old_free[i].prev->next = &ba->free_lists[i];
		}
		else list_init(&ba->free_lists[i]);
	}
	arena_free(ba->arena, ptr);
	*/
}

static void* _buddy_alloc_block(slab_cache_t* ba, uint32_t level) {
	if (ba->data == NULL)
		_cache_meta_alloc(ba);
	listnode_t* block = NULL;
	// Find free block that is large enough
	if (level > ba->slab_level) {
		uint32_t index;
		if (!list_empty(&ba->free_lists[level])) {
			block = list_pop(&ba->free_lists[level]);
			index = _block_index(ba, _ptr_to_offset(ba, block), level - 1);
		}
		else {
			// Split a block
			block = _buddy_alloc_block(ba, level - 1);
			// Not enough room, failed to allocate a block
			if (!block) return NULL;
			uint32_t offset = _ptr_to_offset(ba, block);
			TC_ASSERT(offset < ba->cap);
			//TRACE(LOG_DEBUG, "[Memory]: Splitting block at %i, level %i", offset, level - 1);
			index = _block_index(ba, offset, level - 1);
			// Split block at level - 1
			bit_set(ba->split_bits, index);
			// And buddy it to free list
			list_add_tail(&ba->free_lists[level], ((char*)block) + _size_at_level(ba, level));
			TC_ASSERT(_ptr_to_offset(ba, ((char*)block) + _size_at_level(ba, level)) - offset == _size_at_level(ba, level) || offset - _ptr_to_offset(ba, ((char*)block) + _size_at_level(ba, level)) == _size_at_level(ba, level));
		}
		TC_ASSERT(index < ba->num_blocks / 2);
		bit_toggle(ba->merge_bits, index);
	}
	if (!block) {
		TC_ASSERT(level == ba->slab_level);
		uint16_t nslabs = ba->cap / _size_at_level(ba, ba->slab_level);
		for (int i = 0; i < nslabs; i++) {
			if (!ba->data[i]) {
				block = arena_alloc(ba->arena);
				TC_ASSERT(block);
				//TRACE(LOG_DEBUG, "[Memory]: Allocating new slab at index %i, %p", i, block);
				ba->data[i] = block;
				break;
			}
		}
	}
	return block;
}

static void _buddy_free_block(slab_cache_t* ba, uint32_t offset, uint32_t level) {
	TC_ASSERT(offset > 0 && offset < ba->cap);
	// Return empty block to arena if it is a slab
	if (level == ba->slab_level) {
		void* block = _offset_to_ptr(ba, offset);
		ba->data[offset / ba->arena->slab_size] = NULL;
		arena_free(ba->arena, block);
	}
	else {
		// Find buddy of block that is being freed
		uint32_t index = _block_index(ba, offset, level - 1);
		TC_ASSERT(bit_test(ba->split_bits, index));
		TC_ASSERT(index < ba->num_blocks / 2);
		// Iterative merging towards bigger blocks
		if (bit_test(ba->merge_bits, index)) {
			uint32_t buddy_offset = _get_buddy_offset(ba, offset, level);
			TC_ASSERT(buddy_offset - offset == _size_at_level(ba, level) || offset - buddy_offset == _size_at_level(ba, level));
			TC_ASSERT(buddy_offset > 0 && buddy_offset < ba->cap);
			//TRACE(LOG_DEBUG, "[Memory]: Merging block at %i, level %i", min(offset, buddy_offset), level - 1);
			list_remove(_offset_to_ptr(ba, buddy_offset));
			// Mark a block merge
			bit_clear(ba->split_bits, index);
			// Free parent block
			_buddy_free_block(ba, min(offset, buddy_offset), level - 1);
		}
		else {
			list_add_tail(&ba->free_lists[level], _offset_to_ptr(ba, offset));
		}
		bit_toggle(ba->merge_bits, index);
	}
}

slab_cache_t cache_create(cache_params params) {
	TC_ASSERT(params.size);
	// Fill in struct fields
	slab_cache_t ba = { 0 };
	if (params.arena)
		ba.arena = params.arena;
	else
		ba.arena = &global_arena;
	ba.cap = max(params.size, ba.arena->slab_size);
	ba.min_size = MIN_BUDDY_SIZE;
	ba.nr_levels = log2_32(ba.cap) - MIN_LEVEL + 1;
	ba.num_blocks = 1UL << ba.nr_levels;
	ba.slab_level = max(_level_at_size(&ba, ba.arena->slab_size), 0);
	ba.data = NULL;
	ba.fiber = params.fiber;
	return ba;
}

void cache_destroy(slab_cache_t* ba) {
	// Give used slabs back to arena
	if (ba->data) {
		uint32_t slabs = ba->cap / ba->arena->slab_size;
		for (int i = 0; i < slabs; i++)
			if (ba->data[i])
				arena_free(ba->arena, ba->data[i]);
		ba->data = NULL;
	}
}

void* cache_alloc(slab_cache_t* ba, size_t size) {
	TC_ASSERT(ba->fiber && ba->fiber == fiber() || !ba->fiber);
	//TODO: allow larger than slab size allocations
	if (size == 0 || size > _size_at_level(ba, ba->slab_level))
		return NULL;
	int level = _level_at_size(ba, size);
	void* ptr = _buddy_alloc_block(ba, level);
	//TRACE(LOG_DEBUG, "[Memory]: Allocating %i bytes at %i", size, (char*)ptr - ba->data);
	return ptr;
}

void cache_release(slab_cache_t* ba, void* ptr, size_t size) {
	if (!ptr) return;
	// Find slab to which pointer belongs
	uint32_t offset = _ptr_to_offset(ba, ptr);
	//TRACE(LOG_DEBUG, "[Memory]: Freeing %i bytes at %i", size, (char*)ptr - ba->data);
	_buddy_free_block(ba, offset, _level_at_size(ba, size));
}

void cache_free(slab_cache_t* ba, void* ptr) {
	TC_ASSERT(ba->fiber && ba->fiber == fiber() || !ba->fiber);
	if (!ptr) return;
	// Find slab to which pointer belongs
	uint32_t offset = _ptr_to_offset(ba, ptr);
	TC_ASSERT(offset < ba->cap);
	// Find the correct level
	for (uint32_t level = ba->nr_levels - 1; level > ba->slab_level; level--) {
		if (bit_test(ba->split_bits, _block_index(ba, offset, level - 1))) {
			//TRACE(LOG_DEBUG, "[Memory]: Freeing %i bytes at %i", _size_at_level(ba, level), offset);
			_buddy_free_block(ba, offset, level);
			return;
		}
	}
	// Allocation at the root
	_buddy_free_block(ba, offset, ba->slab_level);
}

void* cache_realloc(slab_cache_t* ba, void* ptr, size_t size) {
	if (!ptr) return cache_alloc(ba, size);
	//TODO: allow larger than slab size allocations
	if (size > ba->arena->slab_size) return NULL;
	if (size == 0) {
		cache_free(ba, ptr);
		return NULL;
	}
	uint32_t offset = _ptr_to_offset(ba, ptr);
	// Find the correct level
	for (uint32_t level = ba->nr_levels - 1; level > ba->slab_level; level--) {
		if (bit_test(ba->split_bits, _block_index(ba, offset, level - 1))) {
			uint32_t new_level = _level_at_size(ba, size);
			if (new_level != level) {
				void* newptr = memcpy(_buddy_alloc_block(ba, new_level), ptr, _size_at_level(ba, level));
				_buddy_free_block(ba, offset, level);
				ptr = newptr;
			}
			return ptr;
		}
	}
	TRACE(LOG_WARNING, "[Memory]: Could not find buddy of %p", ptr);
	return NULL;
}


/*==========================================================*/
/*							RESOURCE IDS					*/
/*==========================================================*/

/* Resource allocator functions (chunk */

#define RESOURCE_INDEX(_ra, _i, _chunk, _element) do { \
	uint32_t elements = (_ra)->elements_per_chunk; \
	_chunk = _i / elements; \
	_element = _i % elements; \
} while(0)

static atomic32_t res_id = { 1 };

void res_init(resourceallocator_t* res, size_t element_size, slab_cache_t* base) {
	res->allocator = base;
	res->chunks = NULL;
	res->freelist = NULL;
	res->entries = NULL;
	res->element_size = element_size;
	res->elements_per_chunk = element_size > CHUNK_SIZE ? 1 : (CHUNK_SIZE / element_size);
	res->cap = 0;
	res->used = 0;
}

rid_t res_alloc(resourceallocator_t* allocator) {
	uint32_t idx, chunk, element;
	uint32_t used = allocator->used;
	uint32_t elements = allocator->elements_per_chunk;
	if (allocator->used == allocator->cap) {
		uint32_t chunks = allocator->used == 0 ? 0 : (allocator->cap / elements);
		allocator->chunks = (void**)cache_realloc(allocator->allocator, allocator->chunks, sizeof(void*) * (chunks + 1));
		allocator->chunks[chunks] = cache_alloc(allocator->allocator, CHUNK_SIZE, CHUNK_SIZE);
		allocator->entries = (uint32_t*)cache_realloc(allocator->allocator, allocator->entries, sizeof(uint32_t) * (chunks + 1) * elements);
		allocator->freelist = (uint32_t*)cache_realloc(allocator->allocator, allocator->freelist, sizeof(uint32_t) * (chunks + 1) * elements);
		for (uint32_t i = 0; i < elements; i++) {
			allocator->entries[chunks + i] = 0xFFFFFFFF;
			allocator->freelist[chunks + i] = used + i;
		}
		allocator->cap += elements;
	}
	idx = allocator->freelist[used];

	RESOURCE_INDEX(allocator, idx, chunk, element);
	void* ptr = &allocator->chunks[chunk][element * allocator->element_size];

	uint32_t entry = atomic_fetch_add(&res_id, 1, MEMORY_ACQUIRE);
	allocator->entries[idx] = entry;
	allocator->used++;

	return (((uint64_t)entry << 32) | idx);
}

void* res_get(resourceallocator_t* allocator, rid_t id) {
	uint32_t idx, chunk, element;
	idx = (uint32_t)id & 0xFFFFFFFF;
	if (idx >= allocator->cap) return NULL;
	RESOURCE_INDEX(allocator, idx, chunk, element);
	uint32_t entry = (uint32_t)(id >> 32);
	if (allocator->entries[idx] != entry) return NULL;
	void* ptr = &allocator->chunks[chunk][element];
	return ptr;
}

void res_free(resourceallocator_t* allocator, rid_t id) {
	uint32_t idx, chunk, element;
	idx = (uint32_t)id & 0xFFFFFFFF;
	if (id >= allocator->cap) {
		TRACE(LOG_ERROR, "[Memory]: Invalid resource id %i", idx);
		return;
	}
	allocator->entries[idx] = 0xFFFFFFFF;
	allocator->freelist[--allocator->used] = idx;
}

bool res_owns(resourceallocator_t* allocator, rid_t id) {
	uint32_t idx, chunk, element;
	idx = (uint32_t)id & 0xFFFFFFFF;
	if (idx >= allocator->cap) return false;
	RESOURCE_INDEX(allocator, idx, chunk, element);
	uint32_t entry = (uint32_t)(id >> 32);
	return allocator->entries[idx] == entry;
}


slab_arena_t global_arena = { 0 };
size_t pagesize;
int stack_dir;

static int stack_direction(void* prev_stack_ptr) { int dummy = 0; return &dummy < prev_stack_ptr ? -1 : 1; }

void memory_init(void) {
	int dummy = 0;
	stack_dir = stack_direction(&dummy);
	(void)dummy;
	pagesize = page_size();

	arena_init(&global_arena, GLOBAL_BUFFER_SIZE, GLOBAL_SLAB_SIZE);
}

void memory_free(void) {
	arena_destroy(&global_arena);
}