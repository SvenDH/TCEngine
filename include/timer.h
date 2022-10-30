/*==========================================================*/
/*							TIMERS							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;
typedef struct tc_fut_s tc_fut_t;

/*
 * Starts a timer that decreases a counter after timeout miliseconds.
 * Repeats the counter tick repeat number of times if it is nonzero
 */
tc_fut_t* timer_start(uint64_t timeout, uint64_t repeats);


void timer_pool_create(tc_allocator_i* a);

void timer_pool_destroy();
