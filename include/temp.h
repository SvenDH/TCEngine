/*==========================================================*/
/*					TEMPORARY ALLOCATOR		    			*/
/*==========================================================*/
#pragma once

#define TC_TEMP_MODULE_NAME "tc_temp_module"

typedef struct tc_allocator_i tc_allocator_i;

#define TC_TEMP_INIT(a) tc_temp_t a; tc_temp->init((&a), NULL)

#define TC_TEMP_CLOSE(a) tc_temp->close((&a))


typedef struct tc_temp_s {

    tc_allocator_i;

    char buffer[1024];

    tc_allocator_i* parent;

    void* next;
} tc_temp_t;

typedef struct tc_temp_i {

    tc_allocator_i* (*init)(tc_temp_t* a, tc_allocator_i* parent);

    void (*close)(tc_temp_t* a);

} tc_temp_i;


extern tc_temp_i* tc_temp;
