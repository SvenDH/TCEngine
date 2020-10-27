/*==========================================================*/
/*							DATASTRUCTS						*/
/*==========================================================*/
#pragma once
#include "tccore.h"
#include "tclog.h"
#include "tctypes.h"
#include "tcmath.h"
#include "tcatomic.h"

#include <string.h>

/*==========================================================*/
/*							BITARRAY						*/
/*==========================================================*/

/* Bit array macros */
#define NUM_BITS (8 * sizeof(uint32_t))
#define INDEX_SHIFT 5UL

/* Bit array functions */
static inline void bit_set(uint32_t* array, uint32_t index) { array[index >> INDEX_SHIFT] |= 1UL << (index & (NUM_BITS - 1UL)); }

static inline void bit_clear(uint32_t* array, uint32_t index) { array[index >> INDEX_SHIFT] &= ~(1UL << (index & (NUM_BITS - 1UL))); }

static inline void bit_toggle(uint32_t* array, uint32_t index) { array[index >> INDEX_SHIFT] ^= 1UL << (index & (NUM_BITS - 1UL)); }

static inline bool bit_test(uint32_t* array, uint32_t index) { return (array[index >> INDEX_SHIFT] & (1UL << (index & (NUM_BITS - 1UL)))) != 0; }


/*==========================================================*/
/*							LIST							*/
/*==========================================================*/

// Simple doubly linked list, use thread local

typedef struct listnode_s {
	struct listnode_s* prev;
	struct listnode_s* next;
} listnode_t;

// To loop over all list elements:
#ifndef list_foreach
#define list_foreach(_list, _node) for ((_node) = (_list)->next; (_list) != (_node); (_node) = (_node)->next )
#endif

static inline void list_init(listnode_t* list) {
	list->prev = list;
	list->next = list;
}

static inline int list_empty(listnode_t* list) {
	return list->next == list && list->prev == list;
}

static inline void list_add(listnode_t* list, listnode_t* node) {
	node->prev = list;
	node->next = list->next;
	list->next->prev = node;
	list->next = node;
}

static inline void list_add_tail(listnode_t* list, listnode_t* node) {
	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;
}

static inline void list_remove(listnode_t* node) {
	node->next->prev = node->prev;
	node->prev->next = node->next;
}

static inline listnode_t* list_first(listnode_t* list) {
	return list->next;
}

static inline listnode_t* list_last(listnode_t* list) {
	return list->prev;
}

static inline listnode_t* list_pop(listnode_t* list) {
	listnode_t* first = NULL;
	if (!list_empty(list)) {
		first = list->next;
		list_remove(first);
	}
	return first;
}


/*==========================================================*/
/*					LOCK-FREE PAGE LIFO/STACK				*/
/*==========================================================*/

// Can only be used to store 64k alligned pointers since it uses the lower 16 bits as counter/tag to track reuse in the ABA problem

typedef struct lf_lifo_t {
	void* next;
} lf_lifo_t;

static inline unsigned short aba_value(void* a) { return (intptr_t)a & 0xffff; }

static inline lf_lifo_t* lf_lifo(void* a) { return (lf_lifo_t*) ((intptr_t)a & ~0xffff); }

static inline void lf_lifo_init(lf_lifo_t* head) { head->next = NULL; }

static inline bool lf_lifo_is_empty(lf_lifo_t* head) { return lf_lifo(head->next) == NULL; }

static inline lf_lifo_t* lf_lifo_push(lf_lifo_t* head, void* elem) {
	TC_ASSERT(lf_lifo(elem) == elem); // Should be aligned address
	do {
		void* tail = head->next;
		lf_lifo(elem)->next = tail;
		void* newhead = (char*)elem + aba_value((char*)tail + 1);
		if (CAS(&head->next, tail, newhead, MEMORY_ACQ_REL))
			return head;
	} while (true);
}

static inline void* lf_lifo_pop(lf_lifo_t* head) {
	do {
		void* tail = head->next;
		lf_lifo_t* elem = lf_lifo(tail);
		if (elem == NULL) return NULL;
		void* newhead = ((char*)lf_lifo(elem->next) + aba_value(tail));
		if (CAS(&head->next, tail, newhead, MEMORY_ACQ_REL))
			return elem;
	} while (true);
}


/*==========================================================*/
/*					FIXED-SIZE MPMC QUEUE					*/
/*==========================================================*/

// This is a fixed-size (FIFO) ring buffer that allows for multiple concurrent reads and writes

typedef struct cell_s {
	atomic_t sequence;
	void* data;
} cell_t;

typedef struct lf_queue_s {
	slab_cache_t* cache;
	size_t mask;
	ALIGNED(cell_t*, 64) buffer;
	ALIGNED(atomic_t, 64) write;
	ALIGNED(atomic_t, 64) read;
} lf_queue_t;

void lf_queue_init(lf_queue_t* queue, uint32_t elements, slab_cache_t* cache);

bool lf_queue_put(lf_queue_t* queue, void* data);

bool lf_queue_get(lf_queue_t* queue, void** data);

void lf_queue_destroy(lf_queue_t* queue);


/*==========================================================*/
/*						VECTORS/BUFFERS						*/
/*==========================================================*/

/* Vector / Growable buffer functions: */

// Common buffer fields
#define BUFFER_OF(_type) \
struct { \
	slab_cache_t* allocator; \
	_type* data; \
	uint32_t len; \
	uint32_t cap; \
}

typedef struct buffer_params {
	size_t size;
	slab_cache_t* allocator;
} buffer_params;

//Generic byte-buffer
typedef BUFFER_OF(uint8_t) buffer_t;

// Allocate buffer with initial size
buffer_t buffer_new(buffer_params params);

// Grow or shrink buffer
void buffer_resize(buffer_t* buf, size_t new_size);

// Free buffer memory
void buffer_clear(buffer_t* buf);


#define BUFFER_CHECK_GROW(_buf) do {									\
if ((_buf)->len == (_buf)->cap) {										\
	uint32_t oldcap = (_buf)->cap;										\
	buffer_resize((buffer_t*)(_buf), oldcap * sizeof(*(_buf)->data) * 2); \
	(_buf)->cap = 2 * oldcap;											\
} } while(0)

#define BUFFER_PUSH(_buf, ...) do {										\
	BUFFER_CHECK_GROW(_buf);											\
	(_buf)->data[(_buf)->len++] = __VA_ARGS__;							\
} while (0)

#define BUFFER_RESIZE(_buf, _n) do {									\
	buffer_resize((buffer_t*)(_buf), _n * sizeof(*(_buf)->data));		\
	(_buf)->cap = _n;													\
} while (0)

#define BUFFER_PUSHN(_buf, _n, ...) do {								\
	void* _ptr = (void*)&((_buf)->data[(_buf)->len];					\
	for (int _i = 0; _i < _n; _i++) {									\
		BUFFER_CHECK_GROW(_buf);										\
		(_buf)->len++;													\
	}																	\
	memcpy(_ptr, __VA_ARGS__, _n * sizeof(*(_buf)->data));				\
} while (0)

// Get element in buffer at index
#define BUFFER_AT(_buf, _idx) (_buf)->data[_idx]

// Get first element in buffer
#define BUFFER_FRONT(_buf) (_buf)->data[0]

// Get last element in buffer
#define BUFFER_BACK(_buf) (_buf)->data[(_buf)->len - 1]

// Get last element and remove it
#define BUFFER_POP(_buf) (_buf)->data[--_buf->len]

// Remove all elements and keep buffer capacity
#define BUFFER_CLEAR(_buf) (_buf)->len = 0

// Clear buffer
#define BUFFER_FREE(_buf) buffer_clear((buffer_t*)(_buf))


/* Hashing utils */
inline uint32_t hash_string(const unsigned char* str, unsigned int len) {
	uint32_t key = crc32(str, len);

	/* Robert Jenkins' 32 bit Mix Function */
	key += (key << 12);
	key ^= (key >> 22);
	key += (key << 4);
	key ^= (key >> 9);
	key += (key << 10);
	key ^= (key >> 2);
	key += (key << 7);
	key ^= (key >> 12);

	/* Knuth's Multiplicative Method */
	key = (key >> 3) * 2654435761;

	return (uint32_t)key;
}

inline uint32_t hash_int(uint32_t x) {
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
}

inline uint32_t hash_combine(uint32_t seed, uint32_t val) {
	return seed ^ (hash_int(val) + 0x9E3779B9 + (seed << 6) * (seed >> 2));
}

/* Hash map functions */
typedef struct {
	uint32_t key;							// Integer key
	uint32_t val;							// Small data integer, (store indices or offsets instead of 
} hashentry;

typedef struct {
	hashentry* data;						// Pointer to buckets
	size_t cap;								// Maximum number of buckets, can grow
	slab_cache_t* allocator;				// Allocator from which to allocate when growing
} hashmap_t;

typedef struct hashmap_params {
	size_t size;
	slab_cache_t* allocator;
} hashmap_params;

/* Definition of hashmap functions */
hashmap_t hashmap_init(hashmap_params params);

// Put value in the hash map at key
bool hashmap_put(hashmap_t* m, uint32_t key, uint32_t value);

// Returns value at key or returns 0
uint32_t hashmap_get(hashmap_t* m, uint32_t key);

// Remove value at key
bool hashmap_remove(hashmap_t* m, uint32_t key);

// Free hashmap
void hashmap_free(hashmap_t* m);


/*==========================================================*/
/*							STRINGS							*/
/*==========================================================*/

/*
typedef struct {
	buffer_t storage;
	hashmap_t lookup;
} ImmutablePool;

// Looks up a string and returns the existing handle or returns a new handle and stores the string internally
StringID string_intern(const char* str, size_t len);

// Returns a pointer to internally stored string and optionally its length (not save: do not use the pointer after interning another string)
const char* string_get(StringID sid, uint32_t* len);
*/