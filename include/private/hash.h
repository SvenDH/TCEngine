/*==========================================================*/
/*						HASH FUNCTIONS						*/
/*==========================================================*/
#pragma once
#include "types.h"
#include "memory.h"

// Code based on MurmurHash2, 64-bit versions, by Austin Appleby
//
// All code is released to the public domain. For business purposes, Murmurhash is
// under the MIT license.

static inline uint64_t tc_hash64(const void* key, uint32_t len, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t* data = (const uint64_t*)key;
    const uint64_t* end = data + (len / 8);

    while (data != end) {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char* data2 = (const unsigned char*)data;

    switch (len & 7) {
    case 7:
        h ^= (uint64_t)(data2[6]) << 48;
    case 6:
        h ^= (uint64_t)(data2[5]) << 40;
    case 5:
        h ^= (uint64_t)(data2[4]) << 32;
    case 4:
        h ^= (uint64_t)(data2[3]) << 24;
    case 3:
        h ^= (uint64_t)(data2[2]) << 16;
    case 2:
        h ^= (uint64_t)(data2[1]) << 8;
    case 1:
        h ^= (uint64_t)(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

static inline uint64_t tc__tolower64(const uint64_t* c, uint8_t read_bytes) {
    char bytes[8] = { 0 };
    memcpy(bytes, c, read_bytes < sizeof(bytes) ? read_bytes : sizeof(bytes));
    for (uint32_t i = 0; i < 8; ++i)
        bytes[i] = (bytes[i] >= 'A' && bytes[i] <= 'Z' ? bytes[i] - 'A' + 'a' : bytes[i]);
    uint64_t ret;
    memcpy(&ret, bytes, sizeof(ret));
    return ret;
}

static inline uint64_t tc_hash64_tolower(const void* key, uint32_t len, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t* data = (const uint64_t*)key;
    const uint64_t* end = data + (len / 8);

    while (data != end) {
        uint64_t k = tc__tolower64(data++, sizeof(uint64_t));

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    uint8_t remain = (uint8_t)(len - ((const char*)data - (const char*)key));
    const uint64_t klast = tc__tolower64(data, remain);
    const unsigned char* data2 = (const unsigned char*)&klast;

    switch (len & 7) {
    case 7:
        h ^= (uint64_t)(data2[6]) << 48;
    case 6:
        h ^= (uint64_t)(data2[5]) << 40;
    case 5:
        h ^= (uint64_t)(data2[4]) << 32;
    case 4:
        h ^= (uint64_t)(data2[3]) << 24;
    case 3:
        h ^= (uint64_t)(data2[2]) << 16;
    case 2:
        h ^= (uint64_t)(data2[1]) << 8;
    case 1:
        h ^= (uint64_t)(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

static inline uint64_t tc_hash64_combine(uint64_t a, uint64_t b) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;

    // Using this expression creates an overflow warning from visual studio, 
    // so just use the calculated value instead:
    // uint64_t h = 0 ^ (16 * m);
    uint64_t h = 7659067388010076496ULL;

    uint64_t k = a;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;

    k = b;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

static inline sid_t tc_hash_str(const char* s) {
    return s ? tc_hash64(s, (uint32_t)strlen(s), 0) : 0;
}

static inline sid_t tc_hash_str_len(const char* s, uint32_t len) {
    return s ? tc_hash64(s, len, 0) : 0;
}

static inline sid_t tc_hash_str_tolower(const char* s) {
    return s ? tc_hash64_tolower(s, (uint32_t)strlen(s), 0) : 0;
}

/** Definition of hashmap functions */

// Large value integer hashmap (for pointers and such)
typedef struct {
    uint64_t* keys;
    uint64_t* vals;
    uint32_t cap;
    tc_allocator_i* base;
} hash_t;

#define MAX_CHAIN_LENGTH 8
#define INITIAL_MAP_SIZE 4

/** Private function definitions: */
static inline int32_t hash_hash(hash_t* m, uint64_t key);
static inline bool hash_rehash(hash_t* map);


/* Definition of 64-bit hashmap functions */

/** Initialize a new hashmap with 64 bit keys and values */
inline hash_t hash_init(tc_allocator_i* allocator) {
    hash_t map = { 0 };
    map.cap = INITIAL_MAP_SIZE;
    map.base = allocator;
    size_t s = map.cap * sizeof(uint64_t);
    map.keys = tc_malloc(map.base, s * 2);
    memset(map.keys, 0, s * 2);
    map.vals = (size_t)map.keys + s;
    return map;
}

/** Put 64 bit value value in the hashmap at 64 bit key */
inline bool hash_put(hash_t* map, uint64_t key, uint64_t value) {
    TC_ASSERT(key != 0);
    int32_t i = hash_hash(map, key);
    while (i < 0) {
        if (!hash_rehash(map)) {
            return false;
        }
        i = hash_hash(map, key);
    }
    map->keys[i] = key;
    map->vals[i] = value;
    return true;
}

/** Returns value at key or returns 0 */
inline uint64_t hash_get(hash_t* map, uint64_t key) {
    TC_ASSERT(key != 0);
    if (map->keys == NULL) {
        return 0;
    }
    uint64_t curr = key & (map->cap - 1);
    for (uint32_t i = 0; i < MAX_CHAIN_LENGTH; i++) {
        if (map->keys[curr] == key) {
            return map->vals[curr];
        }
        curr = (curr + 1) & (map->cap - 1);
    }
    return 0;
}

/** Remove value at key from the hashmap */
inline bool hash_remove(hash_t* map, uint64_t key) {
    TC_ASSERT(key != 0);
    if (map->keys == NULL) {
        return false;
    }
    uint64_t curr = key & (map->cap - 1);
    for (uint32_t i = 0; i < MAX_CHAIN_LENGTH; i++) {
        if (map->keys[curr] == key) {
            map->keys[curr] = 0;
            map->vals[curr] = 0;
            return true;
        }
        curr = (curr + 1) & (map->cap - 1);
    }
    return false;
}

/** Free memory allocated by the hashmap */
inline void hash_free(hash_t* map) {
    tc_free(map->base, map->keys, (size_t)map->cap * 2 * sizeof(uint64_t));
}

static inline int32_t hash_hash(hash_t* m, uint64_t key) {
    uint32_t curr = key & (m->cap - 1);
    for (uint32_t i = 0; i < MAX_CHAIN_LENGTH; i++) {
        if (m->keys[curr] == 0 || m->keys[curr] == key) {
            return curr;
        }
        curr = (curr + 1) & (m->cap - 1);
    }
    return -1;
}

static inline bool hash_rehash(hash_t* map) {
    size_t size = map->cap;
    size_t new_size = 2 * size;
    uint64_t* temp_keys = memset(tc_malloc(map->base, new_size * 2 * sizeof(uint64_t)), 0, new_size * 2 * sizeof(uint64_t));
    uint64_t* temp_vals = ((size_t)temp_keys) + new_size * sizeof(uint64_t);
    uint64_t* curr_keys = map->keys; map->keys = temp_keys;
    uint64_t* curr_vals = map->vals; map->vals = temp_vals;
    map->cap = new_size;
    for (size_t i = 0; i < size; i++) {
        if (curr_keys[i] == 0) {
            continue;
        }
        if (!hash_put(map, curr_keys[i], curr_vals[i])) {
            return false;
        }
    }
    tc_free(map->base, curr_keys, size * 2 * sizeof(uint64_t));
    return true;
}
