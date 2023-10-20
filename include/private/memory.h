/*==========================================================*/
/*							MEMORY							*/
/*==========================================================*/
#pragma once
#include "core.h"
#include "math.h"

#define TC_ALLOCATION_MODULE_NAME "tc_allocation_module"


/* Memory related constants: */
enum {
	SLAB_MIN_SIZE = ((size_t)USHRT_MAX) + 1,// Smallest possible slab size (64k)
	SLAB_UNLIMITED = SIZE_MAX / 2 + 1,		// The largest allowed amount of memory of a single arena
	CHUNK_SIZE = 4096,						// 4KB page size
	GLOBAL_BUFFER_SIZE = 536870912,			// 512MB global buffer size
	GLOBAL_SLAB_SIZE = 4 * 1024 * 1024,		// 4MB 
};

typedef struct tc_registry_i tc_registry_i;
typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_allocator_i {
	/* 
	 * Allocator instance
	 */
	void* instance;

	/* Generic allocation function of an allocator
	 * allocator is the allocator context
	 * ptr a pointer to resize or free or a NULL for new allocations
	 * prev_size is the size of the memory block ptr is pointing at
	 * new_size is the requested size of the block after the call
	 * file is the file where the allocation occurs
	 * line is the line number in the file where the allocation occurs
	 */
	void* (*alloc)(tc_allocator_i* a, void* ptr, size_t prev_size, size_t new_size, const char* file, uint32_t line);
} tc_allocator_i;

#define TC_ALLOC(_a, _s) (_a)->alloc(_a, NULL, 0, _s, __FILE__, __LINE__)
#define TC_CALLOC(_a, _n, _s) memset(TC_ALLOC((_a), (_s * _n)), 0, (_s * _n))
#define TC_ALLOCAT(_a, _s, file, line) (_a)->alloc(_a, NULL, 0, _s, file, line)
#define TC_FREE(_a, _p, _s) (_a)->alloc(_a, _p, _s, 0, __FILE__, __LINE__)
#define TC_REALLOC(_a, _p, _prev, _new) (_a)->alloc(_a, _p, _prev, _new, __FILE__, __LINE__)

#define tc_malloc(_s) TC_ALLOC(tc_mem->sys, _s)
#define tc_calloc(_n, _s) memset(tc_malloc((_s * _n)), 0, (_s * _n))
#define tc_free(_p) TC_FREE(tc_mem->sys, _p, 0)
#define tc_realloc(_p, _s) TC_REALLOC(tc_mem->sys, _p, 0, _s)

// TODO: track this memory also
#if defined(_WIN32)
#define tc_memalign(_a, _s) _aligned_malloc((_s), (_a))
#define tc_freealign(_p) _aligned_free((_p))
#else
#define tc_memalign(_a, _s) aligned_alloc((_a), (_s))
#define tc_freealign(_p) free((_p))
#endif
#define alignof _Alignof

typedef struct tc_memory_i {

	tc_allocator_i* sys;

	tc_allocator_i* vm;

	tc_allocator_i (*create_child)(const tc_allocator_i* parent, const char* name);

	void (*destroy_child)(const tc_allocator_i* parent);

} tc_memory_i;


extern tc_memory_i* tc_mem;


/*==========================================================*/
/*					RESOURCE ALLOCATOR						*/
/*==========================================================*/

typedef struct tc_rslab_i {
	void* instance;
	
	tc_rid_t (*alloc)(struct tc_rslab_i* res);

	void* (*get)(struct tc_rslab_i* res, tc_rid_t id);

	void (*free)(struct tc_rslab_i* res, tc_rid_t id);
} tc_rslab_i;

typedef struct tc_resources_i {

	tc_rslab_i* (*create)(size_t obj_size, tc_allocator_i* base);

	void (*destroy)(tc_rslab_i* res);
} tc_resources_i;

extern tc_resources_i* tc_res;


/*==========================================================*/
/*					TEMPORARY ALLOCATOR		    			*/
/*==========================================================*/

#define TC_TEMP_INIT(a) tc_temp_t a; tc_temp_init((&a), NULL)

#define TC_TEMP_CLOSE(a) tc_temp_free((&a))


typedef struct tc_temp_s {
    tc_allocator_i;

    char buffer[1024];

    tc_allocator_i* parent;

    void* next;
} tc_temp_t;

inline tc_allocator_i* tc_temp_new(tc_temp_t* a, tc_allocator_i* parent);

inline void tc_temp_free(tc_temp_t* a);


/*==========================================================*/
/*						BUDDY ALLOCATOR						*/
/*==========================================================*/

/** Allocates number of bytes with power of 2 and minimum size of 64 bytes */
tc_allocator_i* tc_buddy_new(tc_allocator_i* a, uint32_t size, uint32_t min_size);

/** Free all resources used by the cache */
void tc_buddy_free(tc_allocator_i* a);


/*==========================================================*/
/*					SCRATCH/REGION ALLOCATOR				*/
/*==========================================================*/

/* An allocator that can do fast allocations but get's freed at once */

typedef struct tc_region_i {

	tc_allocator_i* (*create)(tc_allocator_i* a);

	void (*destroy)(tc_allocator_i* a);

} tc_region_i;

extern tc_region_i* tc_region;


/*==========================================================*/
/*						SLAB ALLOCATOR						*/
/*==========================================================*/

typedef struct tc_slab_s {
	tc_allocator_i* allocator;

	uint64_t slab_size;

	void* free;

	void* end;

	uint64_t next_id;
	
} tc_slab_t;

#define slab_create(slab, a, slab_size) *slab = _slab_create((a), (slab_size),  sizeof(*((*slab))), (uint8_t*)&((*slab))->next - (uint8_t*)((*slab)))

#define slab_destroy(slab) _slab_destroy((slab), sizeof(*(slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_alloc(slab) _slab_alloc((slab), sizeof((*slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_free(slab, obj) _slab_free((slab), (obj), (void**)&(obj)->next)

#define slab_clear(slab) _slab_clear((slab), sizeof(*(slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_next(obj) (SLAB_NEXT(obj->next) ? SLAB_NOTAG(obj->next) : (obj + 1))

#define slab_end(slab) (SLAB_HEADER(slab))

#define slab_id(obj) ((SLAB_TAG(item) == 0) ? (uint64_t)(obj->next) : 0)


/*==========================================================*/
/*							PRIVATE 		    			*/
/*==========================================================*/


/*==========================================================*/
/*					TEMPORARY ALLOCATOR		    			*/
/*==========================================================*/

typedef struct temp_internal_s {
	uint64_t used;
	uint64_t cap;
	uint8_t* end;
} temp_internal_t;

typedef struct temp_node_s {
	uint64_t size;
	void* next;
} temp_node_t;


static void* temp_realloc(tc_temp_t* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line);

inline static
tc_allocator_i* tc_temp_init(tc_temp_t* a, tc_allocator_i* parent) {
	a->instance = a;
	a->alloc = temp_realloc;
	a->parent = parent ? parent : tc_mem->vm;
	temp_internal_t* temp = (temp_internal_t*)a->buffer;
	temp->used = sizeof(temp_internal_t);
	temp->cap = sizeof(a->buffer);
	temp->end = (temp + 1);
	a->next = NULL;
	return (tc_allocator_i*)a;
}

static
void* temp_realloc(tc_temp_t* a, void* ptr, size_t old_size, size_t new_size, const char* file, uint32_t line) {
	temp_internal_t* temp = (temp_internal_t*)a->buffer;
	if (new_size > old_size) {
		if (temp->used + new_size > temp->cap) {
			size_t size = min(CHUNK_SIZE, next_power_of_2((uint32_t)(new_size + sizeof(temp_node_t))));
			temp_node_t* node = TC_ALLOC(a->parent, size);
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

inline static
void tc_temp_free(tc_temp_t* a) {
	temp_node_t* node = a->next;
	while (node) {
		temp_node_t* ptr = node;
		node = node->next;
		TC_FREE(a->parent, ptr, ptr->size);
	}
}

/*==========================================================*/
/*						SLAB ALLOCATOR						*/
/*==========================================================*/

typedef struct slab_obj_s {
    struct slab_obj_s* next;
} slab_obj_t;

/* Last 2 bits of pointers are used for tag bits
 * 0 is a valid object
 * 1 is a free object
 * 2 is a pointer to next slab
 */
#define SLAB_HEADER(p)  ((tc_slab_t*)p - 1)
#define SLAB_TAG(p)     ((uint64_t)(p) & 3)
#define SLAB_FREE(p)    ((void *)((uint64_t)(p) | 1))
#define SLAB_NEXT(p)    ((void *)((uint64_t)(p) | 2))
#define SLAB_NOTAG(p)   ((void *)((uint64_t)(p) & ~3))
#define SLAB_IS_NEXT(p) (SLAB_TAG(p) == 2)

static inline void* _slab_create(tc_allocator_i* a, uint64_t slab_size, uint64_t obj_size, uint64_t offset)
{
    tc_slab_t* sh = TC_ALLOC(a, slab_size);
    memset(sh, 0, slab_size);
    sh->slab_size = slab_size;
    sh->allocator = a;
    sh->end = sh + 1;
    sh->next_id = 4;
    sh->free = SLAB_FREE(0);
    // The slab pointer points to the first item.
    uint8_t* slab = sh->end;
    uint64_t first = (slab_size - sizeof(*sh)) / obj_size;
    uint8_t* last = (uint8_t*)slab + (first - 1) * obj_size;
    void** last_next = (void**)(last + offset);
    *last_next = SLAB_NEXT(0);

    return slab;
}

static inline void _slab_destroy(void* slab, uint64_t obj_size, uint64_t offset)
{
    tc_slab_t* sh = SLAB_HEADER(slab);
    tc_allocator_i* a = sh->allocator;
    uint64_t slab_size = sh->slab_size;

    // Follow the slab pointers in the last items to delete all linked slabs.
    uint64_t first = (slab_size - sizeof(*sh)) / obj_size;
    uint8_t* last = (uint8_t*)slab + (first - 1) * obj_size;
    uint8_t* iter = (uint8_t*)SLAB_NOTAG(*(uint8_t**)(last + offset));
    TC_FREE(a, sh, slab_size);

    uint64_t n = slab_size / obj_size;
    while (iter) {
        last = iter + (n - 1) * obj_size;
        uint8_t* next = (uint8_t*)SLAB_NOTAG(*(uint8_t**)(last + offset));
        TC_FREE(a, iter, slab_size);
        iter = next;
    }
}

static inline void* _slab_alloc(void* slab, uint64_t obj_size, uint64_t offset)
{
    tc_slab_t* sh = SLAB_HEADER(slab);
    uint8_t* obj;
    void** next;
    // Take from free list.
    if (SLAB_NOTAG(sh->free)) {
        obj = (uint8_t*)SLAB_NOTAG(sh->free);
        next = (void**)(obj + offset);
        sh->free = *next;
    }
    else {
        obj = (uint8_t*)sh->end;
        sh->end = (uint8_t*)sh->end + obj_size;
        next = (void**)(obj + offset);
        // Allocate a new slab if we are at the end
        if (SLAB_IS_NEXT(*next)) {
            uint8_t* new_slab = (uint8_t*)TC_ALLOC(sh->allocator, sh->slab_size);
            *next = SLAB_NEXT(new_slab);
            memset(new_slab, 0, sh->slab_size);
            obj = new_slab;
            next = (void**)(obj + offset);
            sh->end = obj + obj_size;
            // Mark the last item in the new slab.
            uint64_t n = sh->slab_size / obj_size;
            uint8_t* last = new_slab + (n - 1) * obj_size;
            void** last_next = (void**)(last + offset);
            *last_next = SLAB_NEXT(0);
        }
    }
    memset(obj, 0, obj_size);
    *next = (void*)sh->next_id;
    sh->next_id += 4;
    return obj;
}

static inline void _slab_free(void* slab, void* obj, void** next)
{
    if (!(SLAB_TAG(*next) == 0)) return;
    tc_slab_t* sh = SLAB_HEADER(slab);
    *next = sh->free;
    sh->free = SLAB_FREE(obj);
}

static inline void _slab_clear(void* slab, uint64_t obj_size, uint64_t offset)
{
    tc_slab_t* sh = SLAB_HEADER(slab);
    uint8_t* obj = slab;
    while (obj != sh->end) {
        void** next = (void**)(obj + offset);
        if (SLAB_TAG(*next)) {
            *next = sh->free;
            sh->free = SLAB_FREE(obj);
        }
        obj += obj_size;
    }
}
