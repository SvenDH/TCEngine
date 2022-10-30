/*==========================================================*/
/*							CHANNELS						*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;
typedef struct tc_fut_s tc_fut_t;
typedef struct tc_channel_s tc_channel_t;

typedef struct tc_put_s {
	tc_channel_t* channel;
	void* value;
} tc_put_t;

typedef struct tc_channel_i {
	
	tc_fut_t* (*get)(tc_channel_t* channel);

	bool (*try_get)(tc_channel_t* channel, void** value);

	tc_fut_t* (*put)(tc_put_t* put_data);

	bool (*try_put)(tc_put_t* put_data);

	void (*close)(tc_channel_t* channel);

	tc_channel_t* (*create)(tc_allocator_i* a, uint32_t size);

	void (*destroy)(tc_channel_t* channel);
} tc_channel_i;

extern tc_channel_i* tc_channel;
