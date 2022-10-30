#pragma once
#include "types.h"

#define TC_REGISTRY_MODULE_NAME "tc_registry_module"

typedef struct tc_registry_i {

	void (*set)(const char* name, void* api, uint32_t size);

	void* (*get)(const char* name);

	void (*remove)(const char* name);

	void (*add_implementation)(const char* name, void* data);

	void (*remove_implementation)(const char* name, void* data);
} tc_registry_i;

extern tc_registry_i* tc_registry;

typedef void tc_load(tc_registry_i* reg, bool load);

typedef struct tc_allocator_i tc_allocator_i;

void tc_init_registry(tc_allocator_i* a);

void tc_close_registry(tc_allocator_i* a);

void load_core(tc_registry_i* registery, bool load);
