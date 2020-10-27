/*==========================================================*/
/*							DATASTRUCTS						*/
/*==========================================================*/
#include "tcdata.h"
#include "tcmemory.h"

/*==========================================================*/
/*						LOCK-FREE QUEUE						*/
/*==========================================================*/

void lf_queue_init(lf_queue_t* queue, uint32_t elements, slab_cache_t* cache) {
	queue->cache = cache;
	size_t buffer_size = elements * sizeof(cell_t);
	queue->buffer = cache_alloc(cache, buffer_size);
	TC_ASSERT(queue->buffer);
	queue->mask = elements - 1;
	TC_ASSERT((elements >= 2) && ((elements & (elements - 1)) == 0));
	for (size_t i = 0; i != elements; i++)
		atomic_store(&queue->buffer[i].sequence, i, MEMORY_RELAXED);
	atomic_store(&queue->write, 0, MEMORY_RELAXED);
	atomic_store(&queue->read, 0, MEMORY_RELAXED);
}

bool lf_queue_put(lf_queue_t* queue, void* data) {
	cell_t* cell;
	size_t pos = (size_t)atomic_load(&queue->write, MEMORY_RELAXED);
	for (;;) {
		cell = &queue->buffer[pos&queue->mask];
		size_t seq = (size_t)atomic_load(&cell->sequence, MEMORY_ACQUIRE);
		intptr_t dif = (intptr_t)seq - (intptr_t)pos;
		if (dif == 0) {
			if (atomic_compare_exchange_weak(&queue->write, &pos, pos + 1, MEMORY_RELAXED, MEMORY_RELAXED))
				break;
		}
		else if (dif < 0) return false;
		else pos = (size_t)atomic_load(&queue->write, MEMORY_RELAXED);
	}
	cell->data = data;
	atomic_store(&cell->sequence, pos + 1, MEMORY_RELEASE);
	return true;
}

bool lf_queue_get(lf_queue_t* queue, void** data) {
	cell_t* cell;
	size_t pos = atomic_load(&queue->read, MEMORY_RELAXED);
	for (;;) {
		cell = &queue->buffer[pos&queue->mask];
		size_t seq = (size_t)atomic_load(&cell->sequence, MEMORY_ACQUIRE);
		intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
		if (dif == 0) {
			if (atomic_compare_exchange_weak(&queue->read, &pos, pos + 1, MEMORY_RELAXED, MEMORY_RELAXED))
				break;
		}
		else if (dif < 0) return false;
		else pos = (size_t)atomic_load(&queue->read, MEMORY_RELAXED);
	}
	*data = cell->data;
	atomic_store(&cell->sequence, pos + queue->mask + 1, MEMORY_RELEASE);
	return true;
}

void lf_queue_destroy(lf_queue_t* queue) {
	cache_free(queue->cache, queue->buffer);
}

/*==========================================================*/
/*							BUFFER							*/
/*==========================================================*/

/* Vector / Growable buffer functions: */

buffer_t buffer_new(buffer_params params) {
	buffer_t buff = { 0 };
	if (params.allocator)
		buff.allocator = params.allocator;
	else 
		buff.allocator = fiber_cache();
	buff.data = cache_alloc(buff.allocator, params.size);
	buff.cap = params.size;
	buff.len = 0;
	return buff;
}

// Grow or shrink buffer
void buffer_resize(buffer_t* buf, size_t new_size) {
	buf->data = cache_realloc(buf->allocator, buf->data, new_size);
	buf->cap = new_size;
}

void buffer_clear(buffer_t* buf) {
	cache_release(buf->allocator, buf->data, buf->cap);
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

/*==========================================================*/
/*							HASHMAP							*/
/*==========================================================*/

/* Definition of hashmap functions */

#define MAX_CHAIN_LENGTH 8
#define INITIAL_MAP_SIZE 4

hashmap_t hashmap_init(hashmap_params params) {
	hashmap_t map = { 0 };
	if (params.allocator)
		map.allocator = params.allocator;
	else
		map.allocator = fiber_cache();
	map.data = (hashentry*)cache_alloc(map.allocator, INITIAL_MAP_SIZE * sizeof(hashentry));
	memset(map.data, 0, INITIAL_MAP_SIZE * sizeof(hashentry));
	map.cap = INITIAL_MAP_SIZE;
	return map;
}

static int hashmap_hash(hashmap_t* m, uint32_t key) {
	const uint32_t hash = hash_int(key);
	int curr = hash & (m->cap - 1);
	for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
		if (m->data[curr].key == 0 || m->data[curr].key == key) return curr;
		curr = (curr + 1) & (m->cap - 1);
	}
	return -1;
}

static bool hashmap_rehash(hashmap_t* map) {
	size_t new_size = 2 * map->cap;
	hashentry* temp = (hashentry*)cache_alloc(map->allocator, new_size * sizeof(hashentry));
	if (temp == NULL) return false;
	else memset(temp, 0, new_size * sizeof(hashentry));
	size_t old_size = map->cap;
	hashentry* curr = map->data;
	map->data = temp;
	map->cap = new_size;
	for (int i = 0; i < old_size; i++) {
		if (curr[i].key == 0) continue;
		if (!hashmap_put(map, curr[i].key, curr[i].val)) return false;
	}
	cache_release(map->allocator, curr, old_size * sizeof(hashentry));
	return true;
}

bool hashmap_put(hashmap_t* map, uint32_t key, uint32_t value) {
	TC_ASSERT(key != 0);
	int index = hashmap_hash(map, key);
	while (index < 0) {
		if (!hashmap_rehash(map)) return false;
		index = hashmap_hash(map, key);
	}
	map->data[index] = (hashentry){ key, value };
	return true;
}

uint32_t hashmap_get(hashmap_t* map, uint32_t key) {
	TC_ASSERT(key != 0);
	if (map->data == NULL) return NULL;
	int curr = hash_int(key) & (map->cap - 1);
	for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
		if (map->data[curr].key == key)
			return map->data[curr].val;
		curr = (curr + 1) & (map->cap - 1);
	}
	return 0;
}

bool hashmap_remove(hashmap_t* map, uint32_t key) {
	TC_ASSERT(key != 0);
	if (map->data == NULL) return 0;
	int curr = hash_int(key) & (map->cap - 1);
	for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
		if (map->data[curr].key == key) {
			map->data[curr] = (hashentry){ 0, NULL };
			return true;
		}
		curr = (curr + 1) & (map->cap - 1);
	}
	return false;
}

void hashmap_free(hashmap_t* map) { 
	if (map->data) cache_free(map->allocator, map->data);
}


/*==========================================================*/
/*							STRINGS							*/
/*==========================================================*/

/*
#define STRINGPOOL_INITIALSIZE 4096

static ImmutablePool stringpool = { 0 };

static bool string_rehash() {
	hashentry* curr = stringpool.lookup.data;
	size_t old_size = stringpool.lookup.cap;
	size_t new_size = old_size;
	hashentry* temp = NULL;
	while (temp == NULL) {
		new_size *= 2;
		temp = (hashentry*)cache_alloc(&default_cache, new_size * sizeof(hashentry));
		if (!temp) return false;
		memset(temp, 0, new_size * sizeof(hashentry));
		for (uint32_t i = 0; i < old_size; i++) {
			if (curr[i].key == 0) continue;
			uint32_t index = curr[i].key & (new_size - 1);
			for (uint32_t j = 0; j < MAX_CHAIN_LENGTH; j++) {
				if (temp[index].key == 0) {
					temp[index] = curr[i];
					break;
				}
				index = (index + 1) & (new_size - 1);
			}
			if (temp[index].key == 0) { // Failed to place need to increase again
				cache_release(&default_cache, temp, new_size * sizeof(hashentry));
				temp = NULL;
				break;
			}
		}
	}
	cache_release(&default_cache, curr, old_size * sizeof(hashentry));
	stringpool.lookup.data = temp;
	stringpool.lookup.cap = new_size;
	//TRACE(LOG_DEBUG, "Rehashing from %i to %i", old_size, new_size);
	return true;
}

static bool string_checkresize(size_t len) {
	size_t newsize = next_power_of_2(stringpool.storage.len + len);
	if (newsize > stringpool.storage.cap) {
		char* newstore = cache_alloc(&default_cache, newsize);
		//TRACE(LOG_DEBUG, "Resizing string storage from %i to %i", stringpool.storage.len, newsize);
		if (newstore) {
			memcpy(newstore, stringpool.storage.data, stringpool.storage.len);
			cache_release(&default_cache, stringpool.storage.data, stringpool.storage.cap);
			stringpool.storage.data = newstore;
			stringpool.storage.cap = newsize;
			return true;
		}
		return false;
	}
	return true;
}

// Looks up a string and returns the existing handle or returns a new handle and stores the string internally
StringID string_intern(const char* str, size_t len) {
	if (stringpool.lookup.data == NULL) {	// Initialize stringpool
		hashmap_init(&stringpool.lookup);
		stringpool.storage.data = memset(cache_alloc(&default_cache, STRINGPOOL_INITIALSIZE), 0, STRINGPOOL_INITIALSIZE);
		stringpool.storage.cap = STRINGPOOL_INITIALSIZE;
	}
	uint32_t hash = hash_string(str, len);
	assert(hash != 0);
	uint32_t curr = hash & (stringpool.lookup.cap - 1);
	int32_t base = -1;
	for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
		if (stringpool.lookup.data[curr].key == 0)
			base = curr;
		else if (stringpool.lookup.data[curr].key == hash) {
			uint32_t index = stringpool.lookup.data[curr].val;
			const char* entry = &stringpool.storage.data[index];
			const uint32_t length = *(uint32_t*)entry;
			const char* string = entry + sizeof(uint32_t);
			if (length == len && memcmp(string, str, len) == 0)
				return hash;
			else {
				TRACE(LOG_WARNING, "[String]: Hash collision detected between %s and %s. You should change the string or hash function", string, str);
				assert(0);
				return 0;
			}
		}
		curr = (curr + 1) & (stringpool.lookup.cap - 1);
	}
	// New string should be stored
	while (base < 0) {
		// Make sure we have room for string and sentinel
		if (!string_rehash()) return 0;
		curr = hash & (stringpool.lookup.cap - 1);
		for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
			if (stringpool.lookup.data[curr].key == 0) {
				base = curr;
				break;
			}
			curr = (curr + 1) & (stringpool.lookup.cap - 1);
		}
	}
	if (!string_checkresize(len + 1)) return 0;
	// Store the string
	uint32_t index = stringpool.storage.len;
	char* data = &stringpool.storage.data[index];
	*(uint32_t*)data = len;
	data += sizeof(uint32_t);
	memcpy(data, str, len);
	data[len] = '\0';
	stringpool.lookup.data[base] = (hashentry){ .key = hash, .val = index };
	// Add in multiples of 4 and size of length field
	stringpool.storage.len += ((len + sizeof(uint32_t)) / sizeof(uint32_t)) * sizeof(uint32_t) + sizeof(uint32_t);
	return hash;
}

// Returns a pointer to internally stored string and optionally its length (not save: do not use the pointer after interning another string)
const char* string_get(StringID sid, uint32_t* len) {
	assert(sid != 0);
	uint32_t curr = sid & (stringpool.lookup.cap - 1);
	for (int i = 0; i < MAX_CHAIN_LENGTH; i++) {
		if (stringpool.lookup.data[curr].key == sid) {
			uint32_t index = stringpool.lookup.data[curr].val;
			const char* entry = &stringpool.storage.data[index];
			if (len != NULL) *len = *(uint32_t*)entry;
			return entry + sizeof(uint32_t);
		}
		curr = (curr + 1) & (stringpool.lookup.cap - 1);
	}
	return NULL;
}
*/