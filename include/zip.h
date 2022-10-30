/*==========================================================*/
/*								ZIP							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_fut_s tc_fut_t;
typedef struct tc_stream_i tc_stream_i;
typedef struct tc_allocator_i tc_allocator_i;

typedef struct entry_info_s {
    int64_t size;
    char path[260];
} entry_info_t;

typedef struct tc_zip_i {

    tc_stream_i* (*create)(const char* path, int flags, tc_allocator_i* base);

    bool (*iter)(tc_stream_i* zip);

    bool (*next)(tc_stream_i* zip, entry_info_t* entry);

    tc_fut_t* (*open)(tc_stream_i* zip, const char* entry, int flags);
} tc_zip_i;

extern tc_zip_i* tc_zip;
