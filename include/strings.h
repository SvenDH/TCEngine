/*==========================================================*/
/*							STRINGS							*/
/*==========================================================*/
#pragma once
#include "types.h"
#include "hash.h"

typedef struct stringentry_s stringentry_t;

typedef struct {
	tc_allocator_i* base;
	stringentry_t* entries; /* buff */
	uint32_t free_entry;
	hash_t lookup;
} stringpool_t;

/* Private structs */

typedef struct stringentry_s {
	char* string;
	uint64_t hash;
	int16_t len;
	int16_t ref;
} stringentry_t;

/* Create a new string pool */
inline stringpool_t stringpool_init(tc_allocator_i* allocator)
{
	stringpool_t pool = { 0 };
	pool.base = allocator;
	pool.lookup = hash_init(pool.base);
	return pool;
}

inline void stringpool_destroy(stringpool_t* pool) 
{
	uint32_t entries = buff_count(pool->entries);
	for (uint32_t i = 0; i < entries; i++) {
		stringentry_t entry = pool->entries[i];
		if (entry.ref) 
			tc_free(
				pool->base, 
				entry.string - sizeof(uint32_t), 
				entry.len + 1 + sizeof(uint32_t));
	}
	buff_free(pool->entries, pool->base);
	hash_free(&pool->lookup);
}

#define PAGEBITS 12
#define PAGESIZE (1<<PAGEBITS)

/*
 * Looks up a string and returns the existing handle or returns a new handle and stores the string internally.
 * Reference count gets increased.
 */
inline sid_t string_intern(stringpool_t* pool, const char* str, uint32_t len) 
{
	if (!len) return 0;
	TC_ASSERT(len < PAGESIZE);
	sid_t hash = tc_hash_string_len(str, len);
	uint32_t index = hash_get(&pool->lookup, hash);
	if (index) {
		stringentry_t* entry = &pool->entries[index - 1];
		TC_ASSERT(entry->hash == hash, 
			"[String]: Corrupted hash in entry");
		TC_ASSERT(entry->len == len && memcmp(entry->string, str, len) == 0, \
			"[String]: Hash collision detected between %s and %s. You should change the string or hash function", entry->string, str);
		// Increase reference count
		entry->ref++;
	}
	else {
		// String not found, put it it in the pool
		size_t data_size = len + 1 + sizeof(uint32_t);
		char* data = tc_malloc(pool->base, data_size);
		stringentry_t new_entry = { 
			.string = data + sizeof(uint32_t), .hash = hash, .len = len, .ref = 1};
		if (pool->free_entry) {
			index = pool->free_entry;
			pool->free_entry = *(uint32_t*)&pool->entries[index - 1];
			pool->entries[index - 1] = new_entry;
		}
		else {
			// Add 1 so we can check for zero index
			index = buff_count(pool->entries) + 1;
			buff_push(pool->entries, pool->base, new_entry);
		}
		hash_put(&pool->lookup, hash, index);
		// Copy string to storage
		*(uint32_t*)data = len;
		data += sizeof(uint32_t);
		memcpy(data, str, len);
		data[len] = '\0';
	}
	return hash;
}

/*
 * Increase reference to a string, make sure to release the reference at cleanup
 */
inline sid_t string_retain(stringpool_t* pool, sid_t sid) 
{
	if (sid) {
		uint32_t index = hash_get(&pool->lookup, sid);
		TC_ASSERT(index);
		stringentry_t* entry = &pool->entries[index - 1];
		++entry->ref;
	}
	return sid;
}

/*
 * Drops reference to a string. When no references exist the string gets garbage collected.
 */
inline void string_release(stringpool_t* pool, sid_t sid) 
{
	if (sid) {
		uint32_t index = hash_get(&pool->lookup, sid);
		TC_ASSERT(index);
		stringentry_t* entry = &pool->entries[index - 1];
		if (--entry->ref == 0) {
			// Free entry and string
			hash_remove(&pool->lookup, entry->hash);
			tc_free(pool->base, 
				entry->string - sizeof(uint32_t), 
				entry->len + 1 + sizeof(uint32_t));
			memset(entry, 0, sizeof(stringentry_t));
			// Add entry to free entries list
			*(uint32_t*)entry = pool->free_entry;
			pool->free_entry = index;
		}
	}
}

/*
 * Returns a pointer to internally stored string and optionally its length .
 * (Do not use the pointer after interning another string)
*/
inline const char* string_get(stringpool_t* pool, sid_t sid, uint32_t* len) 
{
	if (sid) {
		uint32_t index = hash_get(&pool->lookup, sid);
		if (index) {
			stringentry_t entry = pool->entries[index - 1];
			if (len) *len = entry.len;
			return entry.string;
		}
	}
	return NULL;
}
