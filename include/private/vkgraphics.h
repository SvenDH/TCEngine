/*==========================================================*/
/*						Graphics Library					*/
/*==========================================================*/
#pragma once
#include "private_types.h"

#define VULKAN

#include <volk.h>

#define TARGET_VULKAN_API_VERSION VK_API_VERSION_1_1

#if !defined(__ANDROID__)
#define ENABLE_DEBUG_UTILS_EXTENSION
#endif

#if defined(VK_KHR_ray_tracing_pipeline) && defined(VK_KHR_acceleration_structure)
#define VK_RAYTRACING_AVAILABLE
#endif

#define CHECK_VKRESULT(exp)                                                      \
	{                                                                            \
		VkResult vkres = (exp);                                                  \
		if (VK_SUCCESS != vkres)                                                 \
		{                                                                        \
			TRACE(TC_ERROR, "%s: FAILED with VkResult: %d", #exp, vkres); \
			TC_ASSERT(false);                                                       \
		}                                                                        \
	}