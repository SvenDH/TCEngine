#include "private_types.h"

#include <malloc.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

tc_registry_i* tc_registry;

enum {
	MAX_MODULE_SIZE = 8,	// pointers
	MAX_MODULE_NAME = 32,	// bytes
	MAX_MODULES = 32,		// Must be power of 2
	MODULE_HASH_SEED = 3644795231,
};

struct internal_module {
	size_t size;
	char name[MAX_MODULE_NAME];
	void* data[MAX_MODULE_SIZE];
};

struct internal_registry {
	lock_t lock;
	uint64_t hashes[MAX_MODULES];
	struct internal_module modules[MAX_MODULES];
};

static struct internal_registry* state = NULL;

static
struct internal_module* _get_module(const char* name) {
	void* ptr = NULL;
	size_t len = strnlen(name, MAX_MODULE_NAME);
	uint64_t hash = stbds_hash_bytes(name, len, MODULE_HASH_SEED);
	uint32_t idx = hash & (MAX_MODULES - 1);
	for (uint32_t i = 0; i < MAX_MODULES; i++) {
		struct internal_module* mod = &state->modules[idx];
		uint32_t h = state->hashes[idx];
		if (h == hash) {
			uint32_t namelen = strnlen(mod->name, MAX_MODULE_NAME);
			if (namelen == len && strncmp(mod->name, name, len) == 0)
				return &mod;
		}
		else if (h == 0) {
			TC_ASSERT(mod->name[0] == '\0');
			// We did not find the module so we create a new one and 
			// return a pointer to the reserved space (which is empty
			// until you set the module with the set function
			state->hashes[idx] = hash;
			strncpy(mod->name, name, len);
			return &mod;
		}
		idx = (idx + 1) & (MAX_MODULES - 1);
	}
	TC_ASSERT(0, "[Module]: No more room for modules, try to increase MAX_MODULES");
	return NULL;
}

static
void set_module(const char* name, void* data, size_t size) {
	TC_ASSERT(size <= MAX_MODULE_SIZE * sizeof(void*),
		"[Module]: Module too large, try less functions or increasing MAX_MODULE_SIZE");
	TC_LOCK(&state->lock);
	{
		struct internal_module* mod = _get_module(name);
		mod->size = size;
		memcpy(mod->data, data, size);
	}
	TC_UNLOCK(&state->lock);
}

static 
void* get_module(const char* name) {
	void* ptr = NULL;
	TC_LOCK(&state->lock);
	{
		struct internal_module* mod = _get_module(name);
		ptr = mod->data;
	}
	TC_UNLOCK(&state->lock);
	return ptr;
}

static
void remove_module(const char* name) {
	TC_LOCK(&state->lock);
	{
		struct internal_module* mod = _get_module(name);
		memset(mod->data, 0, MAX_MODULE_SIZE * sizeof(void*));
	}
	TC_UNLOCK(&state->lock);
}

static
void add_implementation(const char* name, void* data) {
	TC_LOCK(&state->lock);
	{
		struct internal_module* mod = _get_module(name);
		TC_ASSERT(mod->size < MAX_MODULE_SIZE, "[Module]: Not enough room for more ");
		mod->data[mod->size] = data;
		mod->size++;
	}
	TC_UNLOCK(&state->lock);
}

static
void remove_implementation(const char* name, void* data) {
	TC_LOCK(&state->lock);
	{
		struct internal_module* mod = _get_module(name);
		for (uint32_t i = 0; i < mod->size; i++) {
			if (mod->data[i] == data) {
				mod->data[i] = mod->data[--mod->size];
				TC_UNLOCK(&state->lock);
				return;
			}
		}
	}
	TC_UNLOCK(&state->lock);
	TC_ASSERT(0, "[Module]: Could not find implementation for %s", name);
}

void registry_init() {
	state = malloc(sizeof(struct internal_registry));
	memset(state, 0, sizeof(struct internal_registry));
}

void registry_close() {
	free(state, sizeof(struct internal_registry));
}

tc_registry_i* tc_registry = &(tc_registry_i) {
	.set = set_module,
	.get = get_module,
	.remove = remove_module,
	.add_implementation = add_implementation,
	.remove_implementation = remove_implementation,
};

void load_core(tc_registry_i* registery, bool load) {
	if (load) {
		tc_registry->set(TC_REGISTRY_MODULE_NAME, tc_registry, sizeof(tc_registry_i));

		tc_registry->set(TC_ALLOCATION_MODULE_NAME, tc_mem, sizeof(tc_memory_i));
	}
}