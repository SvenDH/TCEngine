/*==========================================================*/
/*						BUFFER SET							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_buffers_i {

	void* instance;

	void* (*alloc)(struct tc_buffers_i* b, uint32_t size, void* data);

	uint32_t(*add)(struct tc_buffers_i* b, void* ptr, uint32_t size, uint64_t hash);

	void (*retain)(struct tc_buffers_i* b, uint32_t id);

	void (*release)(struct tc_buffers_i* b, uint32_t id);

	const void* (*get)(struct tc_buffers_i* b, uint32_t id, uint32_t* size);

} tc_buffers_i;

typedef struct tc_buffermanager_i {

	tc_buffers_i* (*create)(tc_allocator_i* a);

	void (*destroy)(tc_buffers_i* a);
} tc_buffermanager_i;

extern tc_buffermanager_i* tc_buffers;
