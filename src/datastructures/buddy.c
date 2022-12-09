/*==========================================================*/
/*						BUDDY ALLOCATOR						*/
/*==========================================================*/
#include "private_types.h"

/*( Buddy allocator type: */
enum {
	MIN_LEVEL = 4,										// Minimum of 16 bytes
	MIN_BUDDY_SIZE = (1 << MIN_LEVEL),
	MAX_FREE_UNTIL_GC = 4096,
	MAX_BUDDY_THREADS = 64,
};

typedef struct buddy_allocator_s {
	size_t cap;								// Biggest allocation size that takes up the whole buffer
	uint32_t min_size;						// Minimal size of an allocation
	uint32_t nr_levels;						// Total number of levels (number block/allocation sizes)
	uint32_t num_blocks;					// Maximum number of indices for blocks
	/*( Allocated data for tracking */
	uint8_t* data;							// Track used slabs
	list_t* free_lists;					// Free list array per level
	size_t* merge_bits;						// Bit vector for tracking which blocks are allocated
} buddy_allocator_t;

/*( Buddy allocator function definitions: */

static inline uint32_t _size_at_level(buddy_allocator_t* ba, uint32_t level);
static inline int32_t _level_at_size(buddy_allocator_t* ba, uint32_t size);
static inline uint32_t _block_index(buddy_allocator_t* ba, uint32_t offset, uint32_t level);
static inline uint32_t _buddy_offset(buddy_allocator_t* ba, uint32_t offset, uint32_t level);

/*( Buddy allocator functions: */

// Initialize the buddy allocator with a base allocator
static 
buddy_allocator_t* buddy_create(uint8_t* data, uint32_t size, uint32_t min_size) {
	// Fill in struct fields
	buddy_allocator_t ba = { 0 };
	ba.cap = size;
	ba.min_size = max(MIN_BUDDY_SIZE, min_size);
	ba.nr_levels = log2_32(ba.cap / ba.min_size) + 1;
	ba.num_blocks = 1UL << ba.nr_levels;
	ba.data = data;
	// Calculate array sizes
	size_t freelistsize = (ba.nr_levels) * sizeof(list_t);
	size_t bitvecsize = ((ba.num_blocks / 2 / NUM_BITS) + 1) * sizeof(size_t);
	// Check if one slab can contain all arrays
	TC_ASSERT(sizeof(buddy_allocator_t) + freelistsize + bitvecsize < size);
	// Assign arrays
	ba.free_lists = (list_t*)(data + sizeof(buddy_allocator_t));
	ba.merge_bits = (size_t*)(data + sizeof(buddy_allocator_t) + freelistsize);
	// Initialize free list
	for (uint32_t i = 0; i < ba.nr_levels; i++) {
		list_init(&ba.free_lists[i]);
	}
	memset(ba.merge_bits, 0, bitvecsize);

	size_t offset = sizeof(buddy_allocator_t) + freelistsize + bitvecsize;
	size_t level = 1;
	while (level < ba.nr_levels - 1) {
		uint32_t index = _block_index(&ba, offset, level - 1);
		TC_ASSERT(index < ba.num_blocks / 2);
		uint32_t buddy_offset = _buddy_offset(&ba, offset, level);
		if (buddy_offset > offset) {
			TC_ASSERT(buddy_offset > 0 && buddy_offset < ba.cap);
			bit_toggle(ba.merge_bits, index);
			list_add_tail(&ba.free_lists[level], data + buddy_offset);
		}
		level++;
	}
	return (buddy_allocator_t*)memcpy(data, &ba, sizeof(buddy_allocator_t));
}

static inline
uint32_t _size_at_level(buddy_allocator_t* ba, uint32_t level) {
	return ba->cap / (1LL << level);
}

static inline
uint32_t _block_index(buddy_allocator_t* ba, uint32_t offset, uint32_t level) {
	return (1UL << level) + offset / _size_at_level(ba, level) - 1UL;
}

static inline
int32_t _level_at_size(buddy_allocator_t* ba, uint32_t size) {
	if (size < ba->min_size) {
		return ba->nr_levels - 1;
	}
	size = next_power_of_2(size) / ba->min_size;
	return ba->nr_levels - log2_32(size) - 1;
}

static inline
uint32_t _buddy_offset(buddy_allocator_t* ba, uint32_t offset, uint32_t level) {
	uint32_t size = _size_at_level(ba, level);
	if (_block_index(ba, offset, level) & 1) {
		return (offset & ~(size - 1)) + size;				// Offset is odd block
	}
	else {
		return (offset & ~(size - 1)) - size;				// Offset is even block
	}
}

static
void* buddy_alloc_block(buddy_allocator_t* ba, uint32_t level) {
	list_t* block = NULL;
	// Find free block that is large enough
	if (level > 0) {
		uint32_t index;
		if (!list_empty(&ba->free_lists[level])) {
			block = list_pop(&ba->free_lists[level]);
			index = _block_index(ba, (uint8_t*)block - ba->data, level - 1);
		}
		else {
			// Split a block
			block = buddy_alloc_block(ba, level - 1);
			// Not enough room, failed to allocate a block
			if (!block) return NULL;
			uint32_t offset = (uint8_t*)block - ba->data;
			TC_ASSERT(offset < ba->cap);
			//TRACE(LOG_DEBUG, "[Memory]: Splitting block at %i, level %i", offset, level - 1);
			index = _block_index(ba, offset, level - 1);
			// And buddy it to free list
			list_add_tail(&ba->free_lists[level], ((size_t)block) + _size_at_level(ba, level));
		}
		TC_ASSERT(index < ba->num_blocks / 2);
		bit_toggle(ba->merge_bits, index);
	}
	return block;
}

static
void buddy_free_block(buddy_allocator_t* ba, uint32_t offset, uint32_t level) {
	TC_ASSERT(offset > 0 && offset < ba->cap);
	if (level > 0) {
		// Find buddy of block that is being freed
		uint32_t index = _block_index(ba, offset, level - 1);
		TC_ASSERT(index < ba->num_blocks / 2);
		// Iterative merging towards bigger blocks
		if (bit_test(ba->merge_bits, index)) {
			uint32_t buddy_offset = _buddy_offset(ba, offset, level);
			TC_ASSERT(buddy_offset > 0 && buddy_offset < ba->cap);
			//TRACE(LOG_DEBUG, "[Memory]: Merging block at %i, level %i", min(offset, buddy_offset), level - 1);
			list_remove(ba->data + buddy_offset);
			// Free parent block
			buddy_free_block(ba, min(offset, buddy_offset), level - 1);
		}
		else {
			list_add_tail(&ba->free_lists[level], (list_t*)(ba->data + offset));
		}
		bit_toggle(ba->merge_bits, index);
	}
}

typedef ALIGNED(struct thread_cache_s, 64) {
	/* Base allocator is a buddy allocator for pow of 2 allocations */
	buddy_allocator_t* cache;
	/* Freeds blocks per size from other threads waiting to be merged with gc or reallocated */
	list_t* free_lists;
	/* Buffers that the buddy allocators use */
	uint8_t* data;
	/* Total freed size of blocks in free_lists */
	size_t free;
	/* Lock used for when garbase is collected */
	lock_t lock;
} thread_cache_t;

typedef struct slab_cache_s {
	tc_allocator_i base;
	tc_allocator_i* parent;
	thread_cache_t* thread_caches;			// Each thread has its own buddy allocator 
	uint32_t num_threads;					// Number of threads to which this allocator belongs
	uint32_t nr_levels;
} slab_cache_t;

/* Slab cache allocator functions: */

static void* cache_realloc(tc_allocator_i* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line);

tc_allocator_i* tc_buddy_new(tc_allocator_i* a, uint32_t size, uint32_t min_size) {
	uint32_t id = tc_os->cpu_id();
	// We first create a buddy allocator to allocate our free lists and other buddy allocators from
	// Make sure min size is cache alligned against false sharing
	uint8_t* data = tc_malloc(a, size);
	buddy_allocator_t* first_buddy = buddy_create(data, size, max(min_size, MIN_BUDDY_SIZE));
	uint32_t level = _level_at_size(first_buddy, sizeof(slab_cache_t));
	slab_cache_t* cache = buddy_alloc_block(first_buddy, level);
	// Create extra cache for worker thread
	cache->parent = a;
	cache->num_threads = tc_os->num_cpus();
	TC_ASSERT(cache->num_threads <= MAX_BUDDY_THREADS);
	cache->nr_levels = first_buddy->nr_levels;
	uint32_t cache_size = sizeof(thread_cache_t) * cache->num_threads;
	uint32_t free_size = sizeof(list_t) * cache->nr_levels;
	// We allocator the free lists and per thread allocators from the first allocator
	level = _level_at_size(first_buddy, cache_size);
	char* buffer = buddy_alloc_block(first_buddy, level);
	memset(buffer, 0, cache_size);
	cache->thread_caches = (thread_cache_t*)buffer;
	cache->thread_caches[id].cache = first_buddy;
	cache->thread_caches[id].data = data;

	// Create other buddy allocators
	for (uint32_t i = 0; i < cache->num_threads; i++) {
		if (i != id) {
			data = tc_malloc(a, size);
			cache->thread_caches[i].cache = buddy_create(data, size, max(min_size, MIN_BUDDY_SIZE));
			cache->thread_caches[i].data = data;
		}
		level = _level_at_size(cache->thread_caches[i].cache, free_size);
		cache->thread_caches[i].free_lists = buddy_alloc_block(cache->thread_caches[i].cache, level);

		for (uint32_t j = 0; j < cache->nr_levels; j++) {
			list_init(&cache->thread_caches[i].free_lists[j]);
		}
		TC_ASSERT(cache->thread_caches[i].data);
	}
	cache->base.instance = cache;
	cache->base.alloc = cache_realloc;
	return &cache->base;
}

static 
void cache_gc(slab_cache_t* sc, uint32_t thread_id) {
	// Merge freed blocks with the buddy allocators
	thread_cache_t* tc = &sc->thread_caches[thread_id];
	for (uint32_t i = 0; i < sc->nr_levels; i++) {
		size_t size = _size_at_level(tc->cache, i);
		while (!list_empty(&tc->free_lists[i])) {
			list_t* ptr = list_pop(&tc->free_lists[i]);
			uint32_t id = 0;
			// Find buddy allocator from which the pointer came from
			for (; id < sc->num_threads; id++) {
				if (ptr >= sc->thread_caches[id].data &&
					ptr < sc->thread_caches[id].data + sc->thread_caches[id].cache->cap) {
					break;
				}
			}
			TC_ASSERT(id >= 0 && id < sc->num_threads,
				"[Memory]: Pointer does not originate from this cache");
			thread_cache_t* t = &sc->thread_caches[id];
			TC_LOCK(&t->lock);
			{
				buddy_free_block(t->cache, (uint8_t*)ptr - t->data, i);
			}
			TC_UNLOCK(&t->lock);
			t->free -= size;
		}
	}
}

static
void* cache_malloc(slab_cache_t* sc, size_t size, uint32_t thread_id) {
	thread_cache_t* tc = &sc->thread_caches[thread_id];
	if (tc->free > MAX_FREE_UNTIL_GC)
		cache_gc(sc, thread_id);

	uint32_t level = _level_at_size(tc->cache, size);
	// If there is a free block 
	if (list_empty(&tc->free_lists[level])) {
		TC_LOCK(&tc->lock);
		void* ptr = buddy_alloc_block(tc->cache, level);
		TC_UNLOCK(&tc->lock);
		return ptr;
	}
	else {
		tc->free -= size;
		return list_pop(&tc->free_lists[level]);
	}
}

static
void cache_release(slab_cache_t* sc, void* ptr, size_t size, uint32_t thread_id) {
	if (!ptr) {
		return;
	}
	// Find level of size and put pointer in the list owned by the current thread id
	thread_cache_t* tc = &sc->thread_caches[thread_id];
	uint32_t level = _level_at_size(tc->cache, size);
	list_add_tail(&tc->free_lists[level], (list_t*)ptr);
	tc->free += _size_at_level(tc->cache, level);
}

static
void* cache_realloc(tc_allocator_i* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line) {
	slab_cache_t* sc = a->instance;
	uint32_t id = tc_os->cpu_id();
	if (!ptr) {
		return cache_malloc(sc, new_size, id);
	}
	if (!new_size) {
		cache_release(sc, ptr, old_size, id);
		return NULL;
	}
	// Check if size levels are different, then allocate new block and move the memory
	thread_cache_t* tc = &sc->thread_caches[id];
	if (_level_at_size(tc->cache, old_size) != _level_at_size(tc->cache, new_size)) {
		void* new_ptr = cache_malloc(sc, new_size, id);
		memcpy(new_ptr, ptr, old_size);
		cache_release(sc, ptr, old_size, id);
		return new_ptr;
	}
	return ptr;
}

void tc_buddy_free(tc_allocator_i* a) {
	thread_cache_t threads[MAX_BUDDY_THREADS];
	slab_cache_t* sc = a->instance;
	tc_allocator_i* p = sc->parent;
	uint32_t n = sc->num_threads;
	TC_ASSERT(n <= MAX_BUDDY_THREADS);
	memcpy(threads, sc->thread_caches, sizeof(thread_cache_t) * n);
	for (uint32_t i = 0; i < n; i++) {
		tc_free(p, threads[i].cache, threads[i].cache->cap);
	}
}
