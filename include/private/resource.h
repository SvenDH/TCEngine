/*==========================================================*/
/*					RESOURCE ALLOCATOR						*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_rslab_i {
	void* instance;
	
	tc_rid_t (*alloc)(struct tc_rslab_i* res);

	void* (*get)(struct tc_rslab_i* res, tc_rid_t id);

	void (*free)(struct tc_rslab_i* res, tc_rid_t id);
} tc_rslab_i;

typedef struct tc_resources_i {

	tc_rslab_i* (*create)(size_t obj_size, tc_allocator_i* base);

	void (*destroy)(tc_rslab_i* res);
} tc_resources_i;

extern tc_resources_i* tc_res;
