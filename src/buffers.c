/*==========================================================*/
/*						BUFFER SET							*/
/*==========================================================*/
#include "buffers.h"
#include "hash.h"
#include "sbuf.h"
#include "memory.h"
#include "lock.h"


typedef struct bufentry_s {
	uint32_t len;
	uint32_t ref;
	uint64_t hash;
	uint8_t* data;
} bufentry_t;

typedef struct buffers_s {
	tc_buffers_i;
	tc_allocator_i* base;
	hash_t map;
	bufentry_t* entries;
	lock_t lock;
	uint32_t free;
} buffers_t;

void* buffer_alloc(tc_buffers_i* b, uint32_t size, void* data) 
{
	buffers_t* buffers = b->instance;
	void* ptr = tc_malloc(buffers->base, size);
	if (data)
		memcpy(ptr, data, size);
	return ptr;
}

uint32_t buffer_add(tc_buffers_i* b, void* ptr, uint32_t size, uint64_t hash)
{
	buffers_t* buffers = b->instance;
	if (hash == 0)
		hash = tc_hash_string_len(ptr, size);

	TC_LOCK(&buffers->lock);
	uint32_t id = hash_get(&buffers->map, hash);
	if (id) {
		buffers->entries[id - 1].ref++;
		tc_free(buffers->base, ptr, size);
	}
	else {
		bufentry_t new_entry = { .data = ptr, .hash = hash, .len = size, .ref = 1 };
		if (buffers->free) {
			id = buffers->free;
			bufentry_t* entry = &buffers->entries[id - 1];
			buffers->free = *(uint32_t*)entry;
			*entry = new_entry;
		}
		else {
			// Add 1 so we can check for zero index
			id = buff_count(buffers->entries) + 1;
			buff_push(buffers->entries, buffers->base, new_entry);
		}
		hash_put(&buffers->map, hash, id);
	}
	TC_UNLOCK(&buffers->lock);
	return id;
}

void buffer_retain(tc_buffers_i* b, uint32_t id)
{
	buffers_t* buffers = b->instance;
	spin_lock(&buffers->lock);
	buffers->entries[id - 1].ref++;
	spin_unlock(&buffers->lock);
}

void buffer_release(tc_buffers_i* b, uint32_t id)
{
	buffers_t* buffers = b->instance;
	TC_LOCK(&buffers->lock);
	bufentry_t* entry = &buffers->entries[id - 1];
	if (--buffers->entries[id - 1].ref == 0) {
		uint64_t hash = entry->hash;
		tc_free(buffers->base, entry->data, entry->len);
		*(uint32_t*)entry = buffers->free;
		buffers->free = id;
		hash_remove(&buffers->map, hash);
	}
	TC_UNLOCK(&buffers->lock);
}

const void* buffer_get(tc_buffers_i* b, uint32_t id, uint32_t* size)
{
	buffers_t* buffers = b->instance;
	TC_LOCK(&buffers->lock);
	bufentry_t* entry = &buffers->entries[id - 1];
	if (size) 
		*size = entry->len;
	void* ptr = entry->data;
	TC_UNLOCK(&buffers->lock);
	return ptr;
}

tc_buffers_i* buffers_create(tc_allocator_i* a)
{
	buffers_t* b = tc_malloc(a, sizeof(buffers_t));
	b->instance = b;
	b->alloc = buffer_alloc;
	b->add = buffer_add;
	b->retain = buffer_retain;
	b->release = buffer_release;
	b->get = buffer_get;
	b->base = a;
	b->map = hash_init(a);
	b->lock.value._nonatomic = 0;
	b->free = 0;
	b->entries = NULL;
}

void buffers_destroy(tc_buffers_i* b)
{
	buffers_t* buffers = b->instance;
	TC_LOCK(&buffers->lock);
	{
		for (uint32_t i = 0; i < buff_count(buffers->entries); i++)
			tc_free(buffers->base, buffers->entries[i].data, buffers->entries[i].len);
		
		buff_free(buffers->entries, buffers->base);
		hash_free(&buffers->map);
	}
	TC_UNLOCK(&buffers->lock);
	tc_free(buffers->base, buffers, sizeof(buffers_t));
}

tc_buffermanager_i* tc_buffers = &(tc_buffermanager_i) {
	.create = buffers_create,
	.destroy = buffers_destroy,
};
