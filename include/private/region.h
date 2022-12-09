/*==========================================================*/
/*					SCRATCH/REGION ALLOCATOR				*/
/*==========================================================*/

/* An allocator that can do fast allocations but get's freed at once */

#define TC_REGION_MODULE_NAME "tc_region_module"

typedef struct tc_allocator_i tc_allocator_i;

typedef struct tc_region_i {

	tc_allocator_i* (*create)(tc_allocator_i* a);

	void (*destroy)(tc_allocator_i* a);

} tc_region_i;

extern tc_region_i* tc_region;
