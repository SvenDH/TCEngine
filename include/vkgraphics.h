/*==========================================================*/
/*						Graphics Library					*/
/*==========================================================*/
#pragma once
#include "types.h"
#include "log.h"
#include "graphics.h"

#define MAX_FRAMES 3
#define INVALID_VK_IDX 0xFFFFFFFF

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct tc_allocator_i tc_allocator_i;

typedef struct vkrenderer_s vkrenderer_t;

typedef void (*vk_delete_func)(vkrenderer_t*, uint64_t, void*);

typedef struct vk_poolsize_s {
	uint32_t max_sets;
	uint32_t num_separate_samplers;
	uint32_t num_combined_samplers;
	uint32_t num_sampled_images;
	uint32_t num_storage_images;
	uint32_t num_uniform_buffers;
	uint32_t num_storage_buffers;
	uint32_t num_uniform_texel_buffers;
	uint32_t num_storage_texel_buffers;
	uint32_t num_input_attachments;
	uint32_t num_accel_structs;
} vk_poolsize_t;

typedef struct {
	tc_allocator_i* allocator;
	int32_t api_version;
	gfxfeatures_t features;
	uint32_t adapter_id;
	bool enable_validation;
	const char** extensions;
	uint32_t num_extensions;
	void* vk_allocator;
	uint32_t flush_count;
	vk_poolsize_t main_pool_size;
	vk_poolsize_t dynamic_pool_size;
	uint32_t device_page;
	uint32_t host_page;
	uint32_t device_reserve;
	uint32_t host_reserve;
	uint32_t upload_size;
	uint32_t dynamic_heap_size;
	uint32_t dynamic_page_size;
} vk_params;

vkrenderer_t* vk_init(vk_params* caps);

void vk_release_handle(vkrenderer_t* renderer, vk_delete_func func, uint64_t handle, void* ud);

void vk_discard_handle(vkrenderer_t* renderer, vk_delete_func func, uint64_t handle, void* ud, size_t fenceval);

void vk_discard_stale_resources(vkrenderer_t* renderer, size_t cmdbufval, size_t fenceval);

void vk_purge(vkrenderer_t* renderer, size_t val);

void vk_cleanup(vkrenderer_t* renderer, bool forcerelease);

void vk_idle_cmdqueue(vkrenderer_t* renderer, bool releaseresources);

void vk_idlegpu(vkrenderer_t* renderer);


void swapchain_init();

void swapchain_destroy();