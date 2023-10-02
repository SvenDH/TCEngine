/*==========================================================*/
/*						POOL ALLOCATOR						*/
/*==========================================================*/
#include "private_types.h"

/* Memory pool */

typedef struct {
	tc_allocator_i* parent;
	uint8_t** data;
	void* freed;
	uint32_t objsize;
	uint32_t cap;
	uint32_t used;
	uint32_t index;
} mempool_t;

void* mempool_alloc(tc_allocator_i* a, void* ptr, size_t prev_size, size_t new_size, const char* file, uint32_t line);

tc_allocator_i mempool_create(size_t objsize, tc_allocator_i* a)
{
	TC_ASSERT(objsize <= CHUNK_SIZE, 
		"[Memory]: Max object size for mempool is %i but size is %i", CHUNK_SIZE, objsize);

	mempool_t* pool = TC_ALLOC(a, sizeof(mempool_t));
	pool->parent = a;
	pool->data = NULL;
	pool->freed = NULL;
	pool->objsize = max(objsize, sizeof(void*));
	pool->cap = 0;
	pool->used = 0;
	pool->index = -1;

	return (tc_allocator_i) { .instance = pool, .alloc = mempool_alloc };
}

void* mempool_alloc(
	tc_allocator_i* a,
	void* ptr,
	size_t prev_size,
	size_t new_size,
	const char* file,
	uint32_t line)
{
	mempool_t* p = a->instance;
	TC_ASSERT((new_size == 0 && prev_size == p->objsize) || 
		(new_size == p->objsize && prev_size == 0), 
		"[Memory]: pool can only allocate memory of the same size");
	if (ptr && new_size == 0) {
		*(void**)ptr = p->freed;
		p->freed = ptr;
		p->used -= prev_size;
		return NULL;
	}
	if (p->freed) {
		void* r = p->freed;
		p->freed = *(void**)p->freed;
		return r;
	}
	uint8_t** blocks = p->data;
	uint32_t nr_blocks = p->cap / CHUNK_SIZE;
	//If there are more then one block we store a list of blocks at pool->data else we store the block at pool->data
	if (p->used % CHUNK_SIZE == 0) {
		if (++p->index == nr_blocks) {
			uint8_t** old_blocks = blocks;
			size_t new_blocks = (size_t)(nr_blocks ? nr_blocks * 2 : 1) * sizeof(uint8_t*);
			blocks = memset(
				TC_ALLOC(p->parent, new_blocks), 
				0, 
				new_blocks);
			p->data = memcpy(blocks, old_blocks, nr_blocks * sizeof(uint8_t*));
			p->cap = nr_blocks ? (p->cap * 2) : CHUNK_SIZE;
			if (nr_blocks > 1) 
				TC_FREE(p->parent, old_blocks, nr_blocks * sizeof(uint8_t*));
		}
		if (blocks[p->index] == NULL)
			blocks[p->index] = TC_ALLOC(p->parent, CHUNK_SIZE);
	}
	ptr = blocks[p->index] + (p->used % CHUNK_SIZE);
	p->used += new_size;
	return ptr;
}

void mempool_clear(tc_allocator_i* a)
{
	mempool_t* p = a->instance;
	p->used = 0;
	p->index = -1;
	p->freed = NULL;
	if (p->data) {
		uint32_t nr_blocks = p->cap / CHUNK_SIZE;
		for (uint32_t i = 0; i < nr_blocks; i++)
			TC_FREE(p->parent, p->data[i], CHUNK_SIZE);
	}
}

void mempool_destroy(tc_allocator_i* a)
{
	mempool_t* p = a->instance;
	if (p->used != 0) 
		TRACE(LOG_WARNING, 
			"[Memory]: Some blocks were not returned to the memory pool");

	mempool_clear(p);
	if (p->data) {
		size_t nr_blocks = (p->cap / CHUNK_SIZE);
		TC_FREE(p->parent, p->data, nr_blocks * sizeof(uint8_t*));
	}
	TC_FREE(p->parent, p, sizeof(mempool_t));
}
