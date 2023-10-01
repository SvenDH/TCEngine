/*==========================================================*/
/*							RESOURCES						*/
/*==========================================================*/
#include "private_types.h"

typedef struct {
	tc_rslab_i;
	tc_allocator_i* base;
	uint8_t** chunks;
	uint32_t* free_list;
	uint32_t* entries;
	uint32_t type;
	uint32_t obj_size;
	uint32_t cap;
	uint32_t used;
} resources_t;

static atomic_t res_id = { 1 };

tc_rid_t res_alloc(tc_rslab_i* r);
void* res_get(tc_rslab_i* r, tc_rid_t id);
void res_free(tc_rslab_i* r, tc_rid_t id);

tc_rslab_i* res_create(size_t obj_size, tc_allocator_i* base) {
	resources_t* res = tc_malloc(base, sizeof(resources_t));
	res->base = base;
	res->chunks = NULL;
	res->free_list = NULL;
	res->entries = NULL;
	res->obj_size = obj_size;
	res->cap = 0;
	res->used = 0;
	res->type = atomic_fetch_add_explicit(&res_id, 1, memory_order_acquire);
	res->instance = res;
	res->alloc = res_alloc;
	res->free = res_free;
	res->get = res_get;
	return res;
}

void res_destroy(tc_rslab_i* r) {
	resources_t* res = r->instance;
	uint32_t elements = CHUNK_SIZE / res->obj_size;
	uint32_t chunks = res->used == 0 ? 0 : (res->cap / elements);
	for (uint32_t i = 0; i < chunks; i++) {
		tc_free(res->base, res->chunks[i], CHUNK_SIZE);
	}
	tc_free(res->base, res->chunks, sizeof(void*) * chunks);
	tc_free(res->base, res->entries, sizeof(uint32_t) * chunks);
	tc_free(res->base, res->free_list, sizeof(uint32_t) * chunks);
	tc_free(res->base, res, sizeof(resources_t));
}

tc_rid_t res_alloc(tc_rslab_i* r) {
	resources_t* res = r->instance;
	uint32_t elements = CHUNK_SIZE / res->obj_size;
	if (res->used == res->cap) {
		uint32_t chunks = res->used == 0 ? 0 : (res->cap / elements);
		res->chunks = tc_realloc(res->base, res->chunks,
			sizeof(void*) * chunks, sizeof(void*) * (chunks + 1));
		res->chunks[chunks] = tc_malloc(res->base, CHUNK_SIZE);
		res->entries = tc_realloc(
			res->base, res->entries,
			sizeof(uint32_t) * chunks * elements,
			sizeof(uint32_t) * (chunks + 1) * elements
		);
		res->free_list = tc_realloc(
			res->base, res->free,
			sizeof(uint32_t) * chunks * elements,
			sizeof(uint32_t) * (chunks + 1) * elements
		);
		for (uint32_t i = 0; i < elements; i++) {
			res->entries[chunks + i] = 1;
			res->free_list[chunks + i] = res->used + i;
		}
		res->cap += elements;
	}
	uint32_t idx = res->free_list[res->used++];
	return (tc_rid_t) { .index = idx, .gen = res->entries[idx], .type = res->type };
}

void* res_get(tc_rslab_i* r, tc_rid_t id) {
	resources_t* res = r->instance;
	uint32_t idx = id.index;
	if (idx >= res->cap) {
		return NULL;
	}
	uint32_t elements = CHUNK_SIZE / res->obj_size;
	uint32_t chunk = idx / elements;
	uint32_t element = idx % elements;
	if (res->entries[idx] != id.gen) {
		return NULL;
	}
	return &res->chunks[chunk][element];
}

void res_free(tc_rslab_i* r, tc_rid_t id) {
	resources_t* res = r->instance;
	uint32_t idx = id.index;
	if (idx >= res->cap) {
		TRACE(LOG_ERROR, "[Memory]: Invalid resource id %i", idx);
		return;
	}
	res->entries[idx]++;
	res->free_list[--res->used] = idx;
}

tc_resources_i* tc_res = &(tc_resources_i) {
	.create = res_create,
	.destroy = res_destroy
};
