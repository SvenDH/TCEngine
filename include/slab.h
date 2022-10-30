/*==========================================================*/
/*						SLAB ALLOCATOR						*/
/*==========================================================*/
#pragma once
#include "memory.h"
#include "log.h"

typedef struct tc_slab_s {

	tc_allocator_i* allocator;

	uint64_t slab_size;

	void* free;

	void* end;

	uint64_t next_id;
} tc_slab_t;

typedef struct slab_obj_s {
    struct slab_obj_s* next;
} slab_obj_t;

#define slab_create(slab, a, slab_size) *slab = _slab_create((a), (slab_size),  sizeof(*((*slab))), (uint8_t*)&((*slab))->next - (uint8_t*)((*slab)))

#define slab_destroy(slab) _slab_destroy((slab), sizeof(*(slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_alloc(slab) _slab_alloc((slab), sizeof((*slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_free(slab, obj) _slab_free((slab), (obj), (void**)&(obj)->next)

#define slab_clear(slab) _slab_clear((slab), sizeof(*(slab)), (uint8_t*)&(slab)->next - (uint8_t*)(slab))

#define slab_next(obj) (SLAB_NEXT(obj->next) ? SLAB_NOTAG(obj->next) : (obj + 1))

#define slab_end(slab) (SLAB_HEADER(slab))

#define slab_id(obj) ((SLAB_TAG(item) == 0) ? (uint64_t)(obj->next) : 0)

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
    tc_slab_t* sh = tc_malloc(a, slab_size);
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
    tc_free(a, sh, slab_size);

    uint64_t n = slab_size / obj_size;
    while (iter) {
        last = iter + (n - 1) * obj_size;
        uint8_t* next = (uint8_t*)SLAB_NOTAG(*(uint8_t**)(last + offset));
        tc_free(a, iter, slab_size);
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
            uint8_t* new_slab = (uint8_t*)tc_malloc(sh->allocator, sh->slab_size);
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
