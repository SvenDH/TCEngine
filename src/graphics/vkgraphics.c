/* Imports */
#include "private_types.h"

#include "private/vkgraphics.h"

#include "stb_ds.h"

//#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <tinyimageformat.h>


static atomic_t rtids = 1;

inline void vk_utils_caps_builder(renderer_t* renderer)
{
	renderer->capbits = (gpucaps_t*)tc_calloc(1, sizeof(gpucaps_t));

	for (uint32_t i = 0; i < TinyImageFormat_Count; ++i) {
		VkFormatProperties formatsupport;
		VkFormat fmt = (VkFormat) TinyImageFormat_ToVkFormat((TinyImageFormat)i);
		if(fmt == VK_FORMAT_UNDEFINED) continue;

		vkGetPhysicalDeviceFormatProperties(renderer->vulkan.activegpu, fmt, &formatsupport);
		renderer->capbits->canshaderreadfrom[i] =
				(formatsupport.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
		renderer->capbits->canshaderwriteto[i] =
				(formatsupport.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
		renderer->capbits->canrtwriteto[i] =
				(formatsupport.optimalTilingFeatures & 
					(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0;
	}
}

#define VENDOR_ID_NVIDIA 0x10DE
#define VENDOR_ID_AMD 0x1002
#define VENDOR_ID_AMD_1 0x1022
#define VENDOR_ID_INTEL 0x163C
#define VENDOR_ID_INTEL_1 0x8086
#define VENDOR_ID_INTEL_2 0x8087

#define MAX_QUEUE_FAMILIES 16
#define MAX_QUEUE_COUNT 64

typedef enum {
	GPU_VENDOR_NVIDIA,
	GPU_VENDOR_AMD,
	GPU_VENDOR_INTEL,
	GPU_VENDOR_UNKNOWN,
	GPU_VENDOR_COUNT,
} gpuvendor_t;

static gpuvendor_t util_to_internal_gpu_vendor(uint32_t vendorid)
{
	switch(vendorid) {
    case VENDOR_ID_NVIDIA:
        return GPU_VENDOR_NVIDIA;
    case VENDOR_ID_AMD:
    case VENDOR_ID_AMD_1:
		return GPU_VENDOR_AMD;
	case VENDOR_ID_INTEL:
    case VENDOR_ID_INTEL_1:
    case VENDOR_ID_INTEL_2:
		return GPU_VENDOR_INTEL;
	default:
		return GPU_VENDOR_UNKNOWN;
    }
}

VkBlendOp blendopmap[MAX_BLEND_MODES] = {
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX,
};

VkBlendFactor blendconstmap[BLEND_FACTOR_MAX] = {
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
};

VkCompareOp comparefuncmap[MAX_COMPARE_MODES] = {
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
	VK_COMPARE_OP_ALWAYS,
};

VkStencilOp stencilopmap[MAX_STENCIL_OPS] = {
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_WRAP,
	VK_STENCIL_OP_DECREMENT_AND_WRAP,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
};

VkCullModeFlagBits cullmodemap[MAX_CULL_MODES] = {
	VK_CULL_MODE_NONE,
	VK_CULL_MODE_BACK_BIT,
	VK_CULL_MODE_FRONT_BIT
};

VkPolygonMode fillmodemap[MAX_FILL_MODES] = {
	VK_POLYGON_MODE_FILL,
	VK_POLYGON_MODE_LINE
};

VkFrontFace frontfacemap[] = {
	VK_FRONT_FACE_COUNTER_CLOCKWISE,
	VK_FRONT_FACE_CLOCKWISE
};

VkAttachmentLoadOp attachmentloadopmap[MAX_LOAD_ACTION] = {
	VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	VK_ATTACHMENT_LOAD_OP_LOAD,
	VK_ATTACHMENT_LOAD_OP_CLEAR,
};

VkAttachmentStoreOp attachmentstoreopmap[MAX_STORE_ACTION] = {
	VK_ATTACHMENT_STORE_OP_STORE,
	VK_ATTACHMENT_STORE_OP_DONT_CARE,
	VK_ATTACHMENT_STORE_OP_DONT_CARE,       // Dont care is treated as store op none in most drivers
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	VK_ATTACHMENT_STORE_OP_STORE,           // Resolve + Store = Store Resolve attachment + Store MSAA attachment
	VK_ATTACHMENT_STORE_OP_DONT_CARE,       // Resolve + Dont Care = Store Resolve attachment + Dont Care MSAA attachment
#endif
};

const char* wantedinstanceextensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
	VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_GGP)
	VK_GGP_STREAM_DESCRIPTOR_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_VI_NN)
	VK_NN_VI_SURFACE_EXTENSION_NAME,
#endif
#ifdef ENABLE_DEBUG_UTILS_EXTENSION         // Debug utils not supported on all devices yet
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#else
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
	VK_NV_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
	VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,    // To legally use HDR formats
	VK_KHR_DISPLAY_EXTENSION_NAME,          // VR Extensions
	VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
#if VK_KHR_device_group_creation
	VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,    // Multi GPU Extensions
#endif
#ifndef NX64
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, // Property querying extensions
#endif
};

const char* wanteddeviceextensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_MAINTENANCE1_EXTENSION_NAME,
	VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
	VK_EXT_SHADER_SUBGROUP_BALLOT_EXTENSION_NAME,
	VK_EXT_SHADER_SUBGROUP_VOTE_EXTENSION_NAME,
	VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
	VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
#ifdef USE_EXTERNAL_MEMORY_EXTENSIONS
	VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,
#endif
#endif
#ifndef ENABLE_DEBUG_UTILS_EXTENSION
	VK_EXT_DEBUG_MARKER_EXTENSION_NAME,                 // Debug marker extension in case debug utils is not supported
#endif
#if defined(VK_USE_PLATFORM_GGP)
	VK_GGP_FRAME_TOKEN_EXTENSION_NAME,
#endif
#if VK_KHR_draw_indirect_count
	VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
#endif
#if VK_EXT_fragment_shader_interlock
	VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,    // Fragment shader interlock extension to be used for ROV type functionality in Vulkan
#endif
#ifdef USE_NV_EXTENSIONS
	VK_NVX_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME,    // NVIDIA Specific Extensions
#endif
	VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME,          // AMD Specific Extensions
	VK_AMD_SHADER_BALLOT_EXTENSION_NAME,
	VK_AMD_GCN_SHADER_EXTENSION_NAME,
#if VK_KHR_device_group
	VK_KHR_DEVICE_GROUP_EXTENSION_NAME,                 // Multi GPU Extensions
#endif
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,          // Bindless & None Uniform access Extensions
#if VK_KHR_maintenance3 // descriptor indexing depends on this
    VK_KHR_MAINTENANCE3_EXTENSION_NAME,
#endif
#ifdef VK_RAYTRACING_AVAILABLE
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,        // Raytracing
	VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, 
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_SPIRV_1_4_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
#endif
#if VK_KHR_bind_memory2                                 // YCbCr format support
	VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,                // Requirement for VK_KHR_sampler_ycbcr_conversion
#endif
#if VK_KHR_sampler_ycbcr_conversion
	VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
	VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
	VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
#endif
#if VK_KHR_image_format_list
	VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
#endif
};


#ifdef ENABLE_DEBUG_UTILS_EXTENSION
static bool debugext = false;
#endif
static bool renderdoclayer = false;
static bool devicegroupcreationext = false;

static void* VKAPI_PTR vk_alloc(void* userdata, size_t size, size_t align, VkSystemAllocationScope scope)
{
	return tc_malloc(size);
}
static void* VKAPI_PTR vk_realloc(void* userdata, void* ptr, size_t size, size_t align, VkSystemAllocationScope scope)
{
	return tc_realloc(ptr, size);
}
static void VKAPI_PTR vk_free(void* userdata, void* ptr) { tc_free(ptr); }
static void VKAPI_PTR vk_internalalloc(void* userdata, size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope){}
static void VKAPI_PTR vk_internalfree(void* userdata, size_t size, VkInternalAllocationType type, VkSystemAllocationScope scope){}

VkAllocationCallbacks alloccbs = { NULL, vk_alloc, vk_realloc, vk_free, vk_internalalloc, vk_internalfree };

#if VK_KHR_draw_indirect_count
PFN_vkCmdDrawIndirectCountKHR pfnVkCmdDrawIndirectCountKHR = NULL;
PFN_vkCmdDrawIndexedIndirectCountKHR pfnVkCmdDrawIndexedIndirectCountKHR = NULL;
#else
PFN_vkCmdDrawIndirectCountAMD pfnVkCmdDrawIndirectCountKHR = NULL;
PFN_vkCmdDrawIndexedIndirectCountAMD pfnVkCmdDrawIndexedIndirectCountKHR = NULL;
#endif

VkSampleCountFlagBits util_to_vk_sample_count(samplecount_t count);

typedef void (*add_buffer_func)(renderer_t* renderer, const bufferdesc_t* desc, buffer_t** buf);
typedef void (*remove_buffer_func)(renderer_t* renderer, buffer_t* buf);
typedef void (*map_buffer_func)(renderer_t* renderer, buffer_t* buf, range_t* range);
typedef void (*unmap_buffer_func)(renderer_t* renderer, buffer_t* buf);
typedef void (*cmd_updatebuffer_func)(cmd_t* cmd, buffer_t* buf, uint64_t dstoffset, buffer_t* srcbuf, uint64_t srcoffset, uint64_t size);
typedef void (*cmd_updatesubresource_func)(cmd_t* cmd, texture_t* tex, buffer_t* srcbuf, const struct subresourcedatadesc_s* desc);
typedef void (*cmd_copysubresource_func)(cmd_t* cmd, buffer_t* dstbuf, texture_t* tex, const struct subresourcedatadesc_s* desc);
typedef void (*add_texture_func)(renderer_t* renderer, const texturedesc_t* desc, texture_t** tex);
typedef void (*remove_texture_func)(renderer_t* renderer, texture_t* tex);
typedef void (*add_virtualtexture_func)(cmd_t* cmd, const texturedesc_t* desc, texture_t** tex, void* imagedata);
typedef void (*remove_virtualtexture_func)(renderer_t* renderer, virtualtexture_t* tex);

// Descriptor Pool Functions
static void add_descriptor_pool(
	renderer_t* renderer, uint32_t numsets, VkDescriptorPoolCreateFlags flags,
	const VkDescriptorPoolSize* sizes, uint32_t numsizes, VkDescriptorPool* pool)
{
	VkDescriptorPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	info.pNext = NULL;
	info.poolSizeCount = numsizes;
	info.pPoolSizes = sizes;
	info.flags = flags;
	info.maxSets = numsets;
	CHECK_VKRESULT(vkCreateDescriptorPool(renderer->vulkan.device, &info, &alloccbs, pool));
}

static void consume_descriptor_sets(VkDevice device, VkDescriptorPool pool, const VkDescriptorSetLayout* layouts, uint32_t numsets, VkDescriptorSet** sets)
{
	VkDescriptorSetAllocateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	info.pNext = NULL;
	info.descriptorPool = pool;
	info.descriptorSetCount = numsets;
	info.pSetLayouts = layouts;
	CHECK_VKRESULT(vkAllocateDescriptorSets(device, &info, *sets));
}

VkPipelineBindPoint pipelinebindpoint[PIPELINE_TYPE_COUNT] = { VK_PIPELINE_BIND_POINT_MAX_ENUM, VK_PIPELINE_BIND_POINT_COMPUTE, VK_PIPELINE_BIND_POINT_GRAPHICS,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR };

typedef struct dynamicuniformdata_s {
	VkBuffer buffer;
	uint32_t offset;
	uint32_t size;
} dynamicuniformdata_t;

// Descriptor Set Structure
typedef struct descidxmap_s { char* key; uint32_t value; } descidxmap_t;

static const descriptorinfo_t* get_descriptor(rootsignature_t* signature, const char* name)
{
	const descidxmap_t* node = shgetp_null(signature->descnametoidxmap, name);
	if (node) return &signature->descriptors[node->value];
    TRACE(TC_ERROR, "Invalid descriptor param (%s)", name);
    return NULL;
}

// Render Pass Implementation
static const loadop_t defaultloadops[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
static const storeop_t defaultstoreops[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
static inline bool is_storeop_resolve(storeop_t action) { return STORE_ACTION_RESOLVE_DONTCARE == action || STORE_ACTION_RESOLVE_STORE == action; }
#endif

typedef struct {
	TinyImageFormat* colorfmts;
	const loadop_t* loadopcolor;
	const storeop_t* storeopcolor;
	bool* srgbvals;
	uint32_t rtcount;
	samplecount_t samplecount;
	TinyImageFormat depthstencilfmt;
	loadop_t loadopdepth;
	loadop_t loadopstencil;
	storeop_t storeopdepth;
	storeop_t storeopstencil;
    bool vrmultiview;
    bool vrfoveatedrendering;
} renderpassdesc_t;

typedef struct { VkRenderPass renderpass; renderpassdesc_t desc; } renderpass_t;

typedef struct {
	renderpass_t* renderpass;
	rendertarget_t** rendertargets;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	storeop_t* rtresolveop;
#endif
	rendertarget_t* depthstencil;
	uint32_t* colorarrayslices;
	uint32_t* colormipslices;
	uint32_t deptharrayslice;
	uint32_t depthmipslice;
	uint32_t rtcount;
	bool vrfoveatedrendering;
} framebufferdesc_t;

typedef struct {
	VkFramebuffer framebuffer;
	uint32_t width;
	uint32_t height;
	uint32_t arraysize;
} framebuffer_t;

#define VK_MAX_ATTACHMENT_ARRAY_COUNT ((MAX_RENDER_TARGET_ATTACHMENTS + 2) * 2)

static void add_renderpass(renderer_t* renderer, const renderpassdesc_t* desc, renderpass_t* renderpass)
{
	TC_ASSERT(renderpass);
	memset(renderpass, 0, sizeof(renderpass_t));
	renderpass->desc = *desc;
	bool has_depthattachment_count = desc->depthstencilfmt != TinyImageFormat_UNDEFINED;
	VkAttachmentDescription attachments[VK_MAX_ATTACHMENT_ARRAY_COUNT] = { 0 };
	VkAttachmentReference colorattachment_refs[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	VkAttachmentReference resolve_attachment_refs[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
#endif
	VkAttachmentReference dsattachment_ref = { 0 };
	uint32_t attachment_count = 0;
	VkSampleCountFlagBits sample_count = util_to_vk_sample_count(desc->samplecount);
    for (uint32_t i = 0; i < desc->rtcount; ++i, ++attachment_count) {
        VkAttachmentDescription* attachment = &attachments[attachment_count];
        attachment->flags = 0;
        attachment->format = (VkFormat)TinyImageFormat_ToVkFormat(desc->colorfmts[i]);
        attachment->samples = sample_count;
        attachment->loadOp = attachmentloadopmap[desc->loadopcolor[i]];
        attachment->storeOp = attachmentstoreopmap[desc->storeopcolor[i]];
        attachment->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment->initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment->finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference* attachment_ref = &colorattachment_refs[i];
        attachment_ref->attachment = attachment_count;
        attachment_ref->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
        VkAttachmentReference* resolveattachment_ref = &resolve_attachment_refs[i];
        *resolveattachment_ref = *attachment_ref;
        if (is_storeop_resolve(desc->storeopcolor[i])) {
            ++attachment_count;
            VkAttachmentDescription* resolveAttachment = &attachments[attachment_count];
            *resolveAttachment = *attachment;
            resolveAttachment->samples = VK_SAMPLE_COUNT_1_BIT;
            resolveAttachment->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAttachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            resolveattachment_ref->attachment = attachment_count;
        }
        else resolveattachment_ref->attachment = VK_ATTACHMENT_UNUSED;
#endif
	}

	if (has_depthattachment_count) {
		uint32_t idx = attachment_count++;
		attachments[idx].flags = 0;
		attachments[idx].format = (VkFormat)TinyImageFormat_ToVkFormat(desc->depthstencilfmt);
		attachments[idx].samples = sample_count;
		attachments[idx].loadOp = attachmentloadopmap[desc->loadopdepth];
		attachments[idx].storeOp = attachmentstoreopmap[desc->storeopdepth];
		attachments[idx].stencilLoadOp = attachmentloadopmap[desc->loadopstencil];
		attachments[idx].stencilStoreOp = attachmentstoreopmap[desc->storeopstencil];
		attachments[idx].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[idx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		dsattachment_ref.attachment = idx;
		dsattachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VkSubpassDescription subpass = { 0 };
	subpass.flags = 0;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = desc->rtcount;
	subpass.pColorAttachments = desc->rtcount ? colorattachment_refs : NULL;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	subpass.pResolveAttachments = desc->rtcount ? resolve_attachment_refs : NULL;
#else
	subpass.pResolveAttachments = NULL;
#endif
	subpass.pDepthStencilAttachment= has_depthattachment_count ? &dsattachment_ref : NULL;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	VkRenderPassCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.attachmentCount = attachment_count;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 0;
	info.pDependencies = NULL;
	CHECK_VKRESULT(vkCreateRenderPass(renderer->vulkan.device, &info, &alloccbs, &(renderpass->renderpass)));
}

static void remove_render_pass(renderer_t* renderer, renderpass_t* renderpass)
{
	vkDestroyRenderPass(renderer->vulkan.device, renderpass->renderpass, &alloccbs);
}

static void add_framebuffer(renderer_t* renderer, const framebufferdesc_t* desc, framebuffer_t* framebuffer)
{
	TC_ASSERT(framebuffer);
	memset(framebuffer, 0, sizeof(framebuffer_t));

	uint32_t colorAttachmentCount = desc->rtcount;
	uint32_t depthAttachmentCount = (desc->depthstencil) ? 1 : 0;

	if (colorAttachmentCount) {
		framebuffer->width = desc->rendertargets[0]->width;
		framebuffer->height = desc->rendertargets[0]->height;
		if (desc->colorarrayslices)
			framebuffer->arraysize = 1;
		else
			framebuffer->arraysize = desc->rendertargets[0]->vr ? 1 : desc->rendertargets[0]->arraysize;
	}
	else if (depthAttachmentCount) {
		framebuffer->width = desc->depthstencil->width;
		framebuffer->height = desc->depthstencil->height;
		if (desc->deptharrayslice != UINT32_MAX)
			framebuffer->arraysize = 1;
		else
			framebuffer->arraysize = desc->depthstencil->vr ? 1 : desc->depthstencil->arraysize;
	}
	else TC_ASSERT(0 && "No color or depth attachments");

	if (colorAttachmentCount && desc->rendertargets[0]->depth > 1)
		framebuffer->arraysize = desc->rendertargets[0]->depth;
	VkImageView imageviews[VK_MAX_ATTACHMENT_ARRAY_COUNT] = { 0 };
	VkImageView* iterviews = imageviews;

	for (uint32_t i = 0; i < desc->rtcount; ++i) {
		if (!desc->colormipslices && !desc->colorarrayslices) {
			*iterviews = desc->rendertargets[i]->vulkan.descriptor;
			++iterviews;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (is_storeop_resolve(desc->rtresolveop[i])) {
				*iterviews = desc->rendertargets[i]->resolveattachment->vulkan.descriptor;
				++iterviews;
			}
#endif
		}
		else {
			uint32_t handle = 0;
			if (desc->colormipslices) {
				if (desc->colorarrayslices)
					handle = desc->colormipslices[i] * desc->rendertargets[i]->arraysize + desc->colorarrayslices[i];
				else handle = desc->colormipslices[i];
			}
			else if (desc->colorarrayslices) handle = desc->colorarrayslices[i];
			*iterviews = desc->rendertargets[i]->vulkan.slicedescriptors[handle];
			++iterviews;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (is_storeop_resolve(desc->rtresolveop[i])) {
				*iterviews = desc->rendertargets[i]->resolveattachment->vulkan.slicedescriptors[handle];
				++iterviews;
			}
#endif
		}
	}
	if (desc->depthstencil) {
		if (UINT32_MAX == desc->depthmipslice && UINT32_MAX == desc->deptharrayslice) {
			*iterviews = desc->depthstencil->vulkan.descriptor;
			++iterviews;
		}
		else {
			uint32_t handle = 0;
			if (desc->deptharrayslice != UINT32_MAX) {
				if (desc->deptharrayslice != UINT32_MAX)
					handle = desc->depthmipslice * desc->depthstencil->arraysize + desc->deptharrayslice;
				else handle = desc->depthmipslice;
			}
			else if (desc->deptharrayslice != UINT32_MAX) handle = desc->deptharrayslice;
			*iterviews = desc->depthstencil->vulkan.slicedescriptors[handle];
			++iterviews;
		}
	}

	VkFramebufferCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.renderPass = desc->renderpass->renderpass;
	info.attachmentCount = (uint32_t)(iterviews - imageviews);
	info.pAttachments = imageviews;
	info.width = framebuffer->width;
	info.height = framebuffer->height;
	info.layers = framebuffer->arraysize;
	CHECK_VKRESULT(vkCreateFramebuffer(renderer->vulkan.device, &info, &alloccbs, &(framebuffer->framebuffer)));
}

static void remove_framebuffer(renderer_t* renderer, framebuffer_t* framebuffer)
{
	TC_ASSERT(renderer && framebuffer);
	vkDestroyFramebuffer(renderer->vulkan.device, framebuffer->framebuffer, &alloccbs);
}

static void _internal_log(logtype_t level, const char* msg, const char* component)
{
	TRACE(level, "%s ( %s )", component, msg);
}

#ifdef ENABLE_DEBUG_UTILS_EXTENSION
static VkBool32 VKAPI_PTR _internal_debug_report_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* cbdata, void* userdata)
{
	const char* prefix = cbdata->pMessageIdName;
	const char* msg = cbdata->pMessage;
	int32_t code = cbdata->messageIdNumber;
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		TRACE(LOG_INFO, "[%s] : %s (%i)", prefix, msg, code);
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		TRACE(LOG_WARNING, "[%s] : %s (%i)", prefix, msg, code);
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		TRACE(LOG_ERROR, "[%s] : %s (%i)", prefix, msg, code);
		TC_ASSERT(false);
	}
	return VK_FALSE;
}
#else
static VKAPI_ATTR VkBool32 VKAPI_CALL _internal_debug_report_callback(
	VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objecttype, uint64_t object, size_t location, int32_t code,
	const char* prefix, const char* msg, void* userdata)
{
	if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
		TRACE(LOG_INFO, "[%s] : %s (%i)", prefix, msg, code);
	else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
		TRACE(LOG_WARNING, "[%s] : %s (%i)", prefix, msg, code);
	else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
		TRACE(LOG_WARNING, "[%s] : %s (%i)", prefix, msg, code);
	else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
		TRACE(LOG_ERROR, "[%s] : %s (%i)", prefix, msg, code);
	return VK_FALSE;
}
#endif

static inline VkPipelineColorBlendStateCreateInfo util_to_blend_desc(const blendstatedesc_t* desc, VkPipelineColorBlendAttachmentState* attachments)
{
	int idx = 0;
#if defined(ENABLE_GRAPHICS_DEBUG)
	for (uint32_t i = 0; i < MAX_RENDER_TARGET_ATTACHMENTS; ++i) {
		if (desc->mRenderTargetMask & (1 << i)) {
			TC_ASSERT(desc->mSrcFactors[idx] < MAX_BLEND_CONSTANTS);
			TC_ASSERT(desc->mDstFactors[idx] < MAX_BLEND_CONSTANTS);
			TC_ASSERT(desc->mSrcAlphaFactors[idx] < MAX_BLEND_CONSTANTS);
			TC_ASSERT(desc->mDstAlphaFactors[idx] < MAX_BLEND_CONSTANTS);
			TC_ASSERT(desc->mBlendModes[idx] < MAX_BLEND_MODES);
			TC_ASSERT(desc->mBlendAlphaModes[idx] < MAX_BLEND_MODES);
		}
		if (desc->mIndependentBlend) ++idx;
	}
	idx = 0;
#endif
	for (uint32_t i = 0; i < MAX_RENDER_TARGET_ATTACHMENTS; ++i) {
		if (desc->rtmask & (1 << i)) {
			VkBool32 blend =
				(blendconstmap[desc->srcfactors[idx]] != VK_BLEND_FACTOR_ONE ||
				 blendconstmap[desc->dstfactors[idx]] != VK_BLEND_FACTOR_ZERO ||
				 blendconstmap[desc->srcalphafactors[idx]] != VK_BLEND_FACTOR_ONE ||
				 blendconstmap[desc->dstalphafactors[idx]] != VK_BLEND_FACTOR_ZERO);

			attachments[i].blendEnable = blend;
			attachments[i].colorWriteMask = desc->masks[idx];
			attachments[i].srcColorBlendFactor = blendconstmap[desc->srcfactors[idx]];
			attachments[i].dstColorBlendFactor = blendconstmap[desc->dstfactors[idx]];
			attachments[i].colorBlendOp = blendopmap[desc->blendmodes[idx]];
			attachments[i].srcAlphaBlendFactor = blendconstmap[desc->srcalphafactors[idx]];
			attachments[i].dstAlphaBlendFactor = blendconstmap[desc->dstalphafactors[idx]];
			attachments[i].alphaBlendOp = blendopmap[desc->blendalphamodes[idx]];
		}

		if (desc->independentblend)
			++idx;
	}
	VkPipelineColorBlendStateCreateInfo cb = { 0 };
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.pNext = NULL;
	cb.flags = 0;
	cb.logicOpEnable = VK_FALSE;
	cb.logicOp = VK_LOGIC_OP_CLEAR;
	cb.pAttachments = attachments;
	cb.blendConstants[0] = 0.0f;
	cb.blendConstants[1] = 0.0f;
	cb.blendConstants[2] = 0.0f;
	cb.blendConstants[3] = 0.0f;
	return cb;
}

static inline VkPipelineDepthStencilStateCreateInfo util_to_depth_desc(const depthstatedesc_t* desc)
{
	TC_ASSERT(desc->depthfunc < MAX_COMPARE_MODES);
	TC_ASSERT(desc->stencilfrontfunc < MAX_COMPARE_MODES);
	TC_ASSERT(desc->stencilfrontfail < MAX_STENCIL_OPS);
	TC_ASSERT(desc->depthfrontfail < MAX_STENCIL_OPS);
	TC_ASSERT(desc->stencilfrontpass < MAX_STENCIL_OPS);
	TC_ASSERT(desc->stencilbackfunc < MAX_COMPARE_MODES);
	TC_ASSERT(desc->stencilbackfail < MAX_STENCIL_OPS);
	TC_ASSERT(desc->depthbackfail < MAX_STENCIL_OPS);
	TC_ASSERT(desc->stencilbackpass < MAX_STENCIL_OPS);

	VkPipelineDepthStencilStateCreateInfo ds = { 0 };
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.pNext = NULL;
	ds.flags = 0;
	ds.depthTestEnable = desc->depthtest ? VK_TRUE : VK_FALSE;
	ds.depthWriteEnable = desc->depthwrite ? VK_TRUE : VK_FALSE;
	ds.depthCompareOp = comparefuncmap[desc->depthfunc];
	ds.depthBoundsTestEnable = VK_FALSE;
	ds.stencilTestEnable = desc->stenciltest ? VK_TRUE : VK_FALSE;
	ds.front.failOp = stencilopmap[desc->stencilfrontfail];
	ds.front.passOp = stencilopmap[desc->stencilfrontpass];
	ds.front.depthFailOp = stencilopmap[desc->depthfrontfail];
	ds.front.compareOp = desc->stencilfrontfunc;
	ds.front.compareMask = desc->stencilreadmask;
	ds.front.writeMask = desc->stencilwritemask;
	ds.front.reference = 0;
	ds.back.failOp = stencilopmap[desc->stencilbackfail];
	ds.back.passOp = stencilopmap[desc->stencilbackpass];
	ds.back.depthFailOp = stencilopmap[desc->depthbackfail];
	ds.back.compareOp = comparefuncmap[desc->stencilbackfunc];
	ds.back.compareMask = desc->stencilreadmask;
	ds.back.writeMask = desc->stencilwritemask;
	ds.back.reference = 0;
	ds.minDepthBounds = 0;
	ds.maxDepthBounds = 1;
	return ds;
}

static inline VkPipelineRasterizationStateCreateInfo util_to_rasterizer_desc(const rasterizerstatedesc_t* desc)
{
	TC_ASSERT(desc->fillmode < MAX_FILL_MODES);
	TC_ASSERT(desc->cullmode < MAX_CULL_MODES);
	TC_ASSERT(desc->frontface == FRONT_FACE_CCW || desc->frontface == FRONT_FACE_CW);

	VkPipelineRasterizationStateCreateInfo rs = { 0 };
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.pNext = NULL;
	rs.flags = 0;
	rs.depthClampEnable = desc->depthclampenable ? VK_TRUE : VK_FALSE;
	rs.rasterizerDiscardEnable = VK_FALSE;
	rs.polygonMode = fillmodemap[desc->fillmode];
	rs.cullMode = cullmodemap[desc->cullmode];
	rs.frontFace = frontfacemap[desc->frontface];
	rs.depthBiasEnable = (desc->depthbias != 0) ? VK_TRUE : VK_FALSE;
	rs.depthBiasConstantFactor = (float)desc->depthbias;
	rs.depthBiasClamp = 0.0f;
	rs.depthBiasSlopeFactor = desc->slopescaleddepthbias;
	rs.lineWidth = 1;
	return rs;
}

static VkPipelineRasterizationStateCreateInfo defaultrasterizerdesc = { 0 };
static VkPipelineDepthStencilStateCreateInfo defaultdepthdesc = { 0 };
static VkPipelineColorBlendStateCreateInfo defaultblenddesc = { 0 };
static VkPipelineColorBlendAttachmentState defaultblendattachments[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };

typedef struct nulldescriptors_s {
	texture_t* defaulttexSRV[MAX_LINKED_GPUS][TEXTURE_DIM_COUNT];
	texture_t* defaulttexUAV[MAX_LINKED_GPUS][TEXTURE_DIM_COUNT];
	buffer_t* defaultbufSRV[MAX_LINKED_GPUS];
	buffer_t* defaultbufUAV[MAX_LINKED_GPUS];
	sampler_t* defaultsampler;
	lock_t submitlck;
	lock_t initlck;
	queue_t* initqueue;
	cmdpool_t* initcmdpool;
	cmd_t* initcmd;
	fence_t* initfence;
} nulldescriptors_t;

static void util_initial_transition(renderer_t* renderer, texture_t* tex, resourcestate_t state)
{
	TC_LOCK(&renderer->nulldescriptors->initlck);
	{
		cmd_t* cmd = renderer->nulldescriptors->initcmd;
		reset_cmdpool(renderer, renderer->nulldescriptors->initcmdpool);
		cmd_begin(cmd);
		cmd_resourcebarrier(cmd, 0, NULL, 1, &(texbarrier_t){ tex, RESOURCE_STATE_UNDEFINED, state }, 0, NULL);
		cmd_end(cmd);
		queuesubmitdesc_t desc = { 0 };
		desc.cmdcount = 1;
		desc.cmds = &cmd;
		desc.signalfence = renderer->nulldescriptors->initfence;
		queue_submit(renderer->nulldescriptors->initqueue, &desc);
		wait_for_fences(renderer, 1, &renderer->nulldescriptors->initfence);
	}
	TC_UNLOCK(&renderer->nulldescriptors->initlck);
}

static void add_default_resources(renderer_t* renderer)
{
	spin_lock_init(&renderer->nulldescriptors->submitlck);
	spin_lock_init(&renderer->nulldescriptors->initlck);
	for (uint32_t i = 0; i < renderer->linkednodecount; ++i) {
		uint32_t idx = renderer->gpumode == GPU_MODE_UNLINKED ? renderer->unlinkedrendererindex : i;
		// 1D texture
		texturedesc_t desc = { 0 };
		desc.nodeidx = idx;
		desc.arraysize = 1;
		desc.height = 1;
		desc.width = 1;
		desc.depth = 1;
		desc.miplevels = 1;
		desc.format = TinyImageFormat_R8G8B8A8_UNORM;
		desc.samplecount = SAMPLE_COUNT_1;
		desc.state = RESOURCE_STATE_COMMON;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_1D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexUAV[i][TEXTURE_1D]);

		// 1D texture array
		desc.arraysize = 2;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_1D_ARRAY]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexUAV[i][TEXTURE_1D_ARRAY]);

		// 2D texture
		desc.width = 2;
		desc.height = 2;
		desc.arraysize = 1;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_2D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexUAV[i][TEXTURE_2D]);

		// 2D MS texture
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		desc.samplecount = SAMPLE_COUNT_4;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_2DMS]);
		desc.samplecount = SAMPLE_COUNT_1;

		// 2D texture array
		desc.arraysize = 2;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_2D_ARRAY]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexUAV[i][TEXTURE_2D_ARRAY]);

		// 2D MS texture array
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		desc.samplecount = SAMPLE_COUNT_4;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_2DMS_ARRAY]);
		desc.samplecount = SAMPLE_COUNT_1;

		// 3D texture
		desc.depth = 2;
		desc.arraysize = 1;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_3D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexUAV[i][TEXTURE_3D]);

		// Cube texture
		desc.depth = 1;
		desc.arraysize = 6;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE_CUBE;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_CUBE]);
		desc.arraysize = 6 * 2;
		add_texture(renderer, &desc, &renderer->nulldescriptors->defaulttexSRV[i][TEXTURE_CUBE_ARRAY]);

		bufferdesc_t bufdesc = { 0 };
		bufdesc.nodeidx = idx;
		bufdesc.descriptors = DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bufdesc.memusage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		bufdesc.state = RESOURCE_STATE_COMMON;
		bufdesc.size = sizeof(uint32_t);
		bufdesc.first = 0;
		bufdesc.count = 1;
		bufdesc.stride = sizeof(uint32_t);
		bufdesc.format = TinyImageFormat_R32_UINT;
		add_buffer(renderer, &bufdesc, &renderer->nulldescriptors->defaultbufSRV[i]);
		bufdesc.descriptors = DESCRIPTOR_TYPE_RW_BUFFER;
		add_buffer(renderer, &bufdesc, &renderer->nulldescriptors->defaultbufUAV[i]);
	}

	samplerdesc_t samplerdesc = { 0 };
	samplerdesc.u = WRAP_BORDER;
	samplerdesc.v = WRAP_BORDER;
	samplerdesc.w = WRAP_BORDER;
	add_sampler(renderer, &samplerdesc, &renderer->nulldescriptors->defaultsampler);

	blendstatedesc_t bsdesc = { 0 };
	bsdesc.dstalphafactors[0] = BLEND_ZERO;
	bsdesc.dstfactors[0] = BLEND_ZERO;
	bsdesc.srcalphafactors[0] = BLEND_ONE;
	bsdesc.srcfactors[0] = BLEND_ONE;
	bsdesc.masks[0] = COLOR_MASK_ALL;
	bsdesc.rtmask = BLEND_STATE_TARGET_ALL;
	bsdesc.independentblend = false;
	defaultblenddesc = util_to_blend_desc(&bsdesc, defaultblendattachments);

	depthstatedesc_t dsdesc = { 0 };
	dsdesc.depthfunc = CMP_LEQ;
	dsdesc.depthtest = false;
	dsdesc.depthwrite = false;
	dsdesc.stencilbackfunc = CMP_ALWAYS;
	dsdesc.stencilfrontfunc = CMP_ALWAYS;
	dsdesc.stencilreadmask = 0xFF;
	dsdesc.stencilwritemask = 0xFF;
	defaultdepthdesc = util_to_depth_desc(&dsdesc);

	rasterizerstatedesc_t rsdesc = { 0 };
	rsdesc.cullmode = CULL_MODE_BACK;
	defaultrasterizerdesc = util_to_rasterizer_desc(&rsdesc);

	// Create command buffer to transition resources to the correct state
	queue_t* gfxqueue = NULL;
	cmdpool_t* pool = NULL;
	cmd_t* cmd = NULL;
	fence_t* fence = NULL;

	queuedesc_t queuedesc = { 0 };
	queuedesc.type = QUEUE_TYPE_GRAPHICS;
	add_queue(renderer, &queuedesc, &gfxqueue);

	cmdpooldesc_t pooldesc = { 0 };
	pooldesc.queue = gfxqueue;
	pooldesc.transient = true;
	add_cmdpool(renderer, &pooldesc, &pool);

	cmddesc_t cmddesc = { 0 };
	cmddesc.pool = pool;
	add_cmds(renderer, &cmddesc, 1, &cmd);
	add_fence(renderer, &fence);
	renderer->nulldescriptors->initqueue = gfxqueue;
	renderer->nulldescriptors->initcmdpool = pool;
	renderer->nulldescriptors->initcmd = cmd;
	renderer->nulldescriptors->initfence = fence;

	// Transition resources
	for (uint32_t i = 0; i < renderer->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			if (renderer->nulldescriptors->defaulttexSRV[i][dim])
				util_initial_transition(renderer, renderer->nulldescriptors->defaulttexSRV[i][dim], RESOURCE_STATE_SHADER_RESOURCE);
			if (renderer->nulldescriptors->defaulttexUAV[i][dim])
				util_initial_transition(renderer, renderer->nulldescriptors->defaulttexUAV[i][dim], RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}
}

static void remove_default_resources(renderer_t* renderer)
{
	for (uint32_t i = 0; i < renderer->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			if (renderer->nulldescriptors->defaulttexSRV[i][dim])
				remove_texture(renderer, renderer->nulldescriptors->defaulttexSRV[i][dim]);
			if (renderer->nulldescriptors->defaulttexUAV[i][dim])
				remove_texture(renderer, renderer->nulldescriptors->defaulttexUAV[i][dim]);
		}
		remove_buffer(renderer, renderer->nulldescriptors->defaultbufSRV[i]);
		remove_buffer(renderer, renderer->nulldescriptors->defaultbufUAV[i]);
	}
	remove_sampler(renderer, renderer->nulldescriptors->defaultsampler);
	remove_fence(renderer, renderer->nulldescriptors->initfence);
	remove_cmds(renderer, 1, renderer->nulldescriptors->initcmd);
	remove_cmdpool(renderer, renderer->nulldescriptors->initcmdpool);
	remove_queue(renderer, renderer->nulldescriptors->initqueue);
}

VkFilter util_to_vk_filter(filtermode_t filter)
{
	switch (filter) {
		case FILTER_NEAREST: return VK_FILTER_NEAREST;
		case FILTER_LINEAR: return VK_FILTER_LINEAR;
		default: return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode util_to_vk_mip_map_mode(mipmapmode_t mode)
{
	switch (mode) {
		case MIPMAP_MODE_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case MIPMAP_MODE_LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		default: TC_ASSERT(false && "Invalid Mip Map Mode"); return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
	}
}

VkSamplerAddressMode util_to_vk_address_mode(addressmode_t mode)
{
	switch (mode) {
		case WRAP_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case WRAP_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case WRAP_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case WRAP_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VkShaderStageFlags util_to_vk_stages(shaderstage_t stages)
{
	VkShaderStageFlags result = 0;
	if (SHADER_STAGE_ALL_GRAPHICS == (stages & SHADER_STAGE_ALL_GRAPHICS)) result = VK_SHADER_STAGE_ALL_GRAPHICS;
	else {
		if (SHADER_STAGE_VERT == (stages & SHADER_STAGE_VERT)) result |= VK_SHADER_STAGE_VERTEX_BIT;
		if (SHADER_STAGE_TESC == (stages & SHADER_STAGE_TESC)) result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (SHADER_STAGE_TESE == (stages & SHADER_STAGE_TESE)) result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (SHADER_STAGE_GEOM == (stages & SHADER_STAGE_GEOM)) result |= VK_SHADER_STAGE_GEOMETRY_BIT;
		if (SHADER_STAGE_FRAG == (stages & SHADER_STAGE_FRAG)) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (SHADER_STAGE_COMP == (stages & SHADER_STAGE_COMP)) result |= VK_SHADER_STAGE_COMPUTE_BIT;
	}
	return result;
}

VkSampleCountFlagBits util_to_vk_sample_count(samplecount_t count)
{
	VkSampleCountFlagBits result = VK_SAMPLE_COUNT_1_BIT;
	switch (count) {
		case SAMPLE_COUNT_1: result = VK_SAMPLE_COUNT_1_BIT; break;
		case SAMPLE_COUNT_2: result = VK_SAMPLE_COUNT_2_BIT; break;
		case SAMPLE_COUNT_4: result = VK_SAMPLE_COUNT_4_BIT; break;
		case SAMPLE_COUNT_8: result = VK_SAMPLE_COUNT_8_BIT; break;
		case SAMPLE_COUNT_16: result = VK_SAMPLE_COUNT_16_BIT; break;
		default: TC_ASSERT(false); break;
	}
	return result;
}

VkBufferUsageFlags util_to_vk_buffer_usage(descriptortype_t usage, bool typed)
{
	VkBufferUsageFlags result = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (usage & DESCRIPTOR_TYPE_UNIFORM_BUFFER) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (usage & DESCRIPTOR_TYPE_RW_BUFFER) {
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (typed) result |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_BUFFER) {
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (typed) result |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_INDEX_BUFFER) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (usage & DESCRIPTOR_TYPE_VERTEX_BUFFER) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (usage & DESCRIPTOR_TYPE_INDIRECT_BUFFER) result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#ifdef VK_RAYTRACING_AVAILABLE
	if (usage & DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE) result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	if (usage & DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_BUILD_INPUT) result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	if (usage & DESCRIPTOR_TYPE_SHADER_DEVICE_ADDRESS) result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (usage & DESCRIPTOR_TYPE_SHADER_BINDING_TABLE) result |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
#endif
	return result;
}

VkImageUsageFlags util_to_vk_image_usage(descriptortype_t usage)
{
	VkImageUsageFlags result = 0;
	if (DESCRIPTOR_TYPE_TEXTURE == (usage & DESCRIPTOR_TYPE_TEXTURE)) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (DESCRIPTOR_TYPE_RW_TEXTURE == (usage & DESCRIPTOR_TYPE_RW_TEXTURE)) result |= VK_IMAGE_USAGE_STORAGE_BIT;
	return result;
}

VkAccessFlags util_to_vk_access_flags(resourcestate_t state)
{
	VkAccessFlags ret = 0;
	if (state & RESOURCE_STATE_COPY_SOURCE) ret |= VK_ACCESS_TRANSFER_READ_BIT;
	if (state & RESOURCE_STATE_COPY_DEST) ret |= VK_ACCESS_TRANSFER_WRITE_BIT;
	if (state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) ret |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	if (state & RESOURCE_STATE_INDEX_BUFFER) ret |= VK_ACCESS_INDEX_READ_BIT;
	if (state & RESOURCE_STATE_UNORDERED_ACCESS) ret |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	if (state & RESOURCE_STATE_INDIRECT_ARGUMENT) ret |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	if (state & RESOURCE_STATE_RENDER_TARGET) ret |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	if (state & RESOURCE_STATE_DEPTH_WRITE) ret |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	if (state & RESOURCE_STATE_SHADER_RESOURCE) ret |= VK_ACCESS_SHADER_READ_BIT;
	if (state & RESOURCE_STATE_PRESENT) ret |= VK_ACCESS_MEMORY_READ_BIT;
#ifdef VK_RAYTRACING_AVAILABLE
	if (state & RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
		ret |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
#endif
	return ret;
}

VkImageLayout util_to_vk_image_layout(resourcestate_t usage)
{
	if (usage & RESOURCE_STATE_COPY_SOURCE) return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	if (usage & RESOURCE_STATE_COPY_DEST) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	if (usage & RESOURCE_STATE_RENDER_TARGET) return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	if (usage & RESOURCE_STATE_DEPTH_WRITE) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	if (usage & RESOURCE_STATE_UNORDERED_ACCESS) return VK_IMAGE_LAYOUT_GENERAL;
	if (usage & RESOURCE_STATE_SHADER_RESOURCE)	return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (usage & RESOURCE_STATE_PRESENT) return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	if (usage == RESOURCE_STATE_COMMON) return VK_IMAGE_LAYOUT_GENERAL;
	return VK_IMAGE_LAYOUT_UNDEFINED;
}

void util_get_planar_vk_image_memory_requirement(
	VkDevice device, VkImage image, uint32_t planescount, VkMemoryRequirements* outmemreq, uint64_t* offsets)
{
	outmemreq->size = 0;
	outmemreq->alignment = 0;
	outmemreq->memoryTypeBits = 0;

	VkImagePlaneMemoryRequirementsInfo info = { VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO, NULL };
	VkImageMemoryRequirementsInfo2 info2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	info2.pNext = &info;
	info2.image = image;

	VkMemoryDedicatedRequirements req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, NULL };
	VkMemoryRequirements2 req2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	req2.pNext = &req;
	for (uint32_t i = 0; i < planescount; ++i) {
		info.planeAspect = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);
		vkGetImageMemoryRequirements2(device, &info2, &req2);
		offsets[i] += outmemreq->size;
		outmemreq->alignment = max(req2.memoryRequirements.alignment, outmemreq->alignment);
		outmemreq->size += round_up_64(req2.memoryRequirements.size, req2.memoryRequirements.alignment);
		outmemreq->memoryTypeBits |= req2.memoryRequirements.memoryTypeBits;
	}
}

uint32_t util_get_memory_type(uint32_t typebits, const VkPhysicalDeviceMemoryProperties* memproperties, const VkMemoryPropertyFlags* properties, VkBool32* memtypefound)
{
	for (uint32_t i = 0; i < memproperties->memoryTypeCount; i++) {
		if ((typebits & 1) == 1) {
			if ((memproperties->memoryTypes[i].propertyFlags & *properties) == *properties) {
				if (memtypefound) *memtypefound = true;
				return i;
			}
		}
		typebits >>= 1;
	}
	if (memtypefound) {
		*memtypefound = false;
		return 0;
	}
	TRACE(LOG_ERROR, "Could not find a matching memory type");
	TC_ASSERT(0);
	return 0;
}

// Determines pipeline stages involved for given accesses
VkPipelineStageFlags util_determine_pipeline_stage_flags(renderer_t* renderer, VkAccessFlags accessflags, queuetype_t queuetype)
{
	VkPipelineStageFlags flags = 0;
	switch (queuetype) {
		case QUEUE_TYPE_GRAPHICS:
			if ((accessflags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			if ((accessflags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0) {
				flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
				flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				if (renderer->activegpusettings->geometryshadersupported)
					flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
				if (renderer->activegpusettings->tessellationsupported) {
					flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
					flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
				}
				flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef VK_RAYTRACING_AVAILABLE
				if (renderer->vulkan.raytracingsupported) flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
#endif
			}
			if ((accessflags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
				flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			if ((accessflags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if ((accessflags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;
		case QUEUE_TYPE_COMPUTE:
			if ((accessflags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
				(accessflags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
				(accessflags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
				(accessflags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
				return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			if ((accessflags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;
		case QUEUE_TYPE_TRANSFER: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		default: break;
	}
	// Compatible with both compute and graphics queues
	if ((accessflags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
		flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	if ((accessflags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if ((accessflags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
		flags |= VK_PIPELINE_STAGE_HOST_BIT;
	if (flags == 0) flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	return flags;
}

VkImageAspectFlags util_vk_determine_aspect_mask(VkFormat format, bool includeStencilBit)
{
	VkImageAspectFlags result = 0;
	switch (format) {
		case VK_FORMAT_D16_UNORM:					// Depth
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
			result = VK_IMAGE_ASPECT_DEPTH_BIT;
			break;
		case VK_FORMAT_S8_UINT:						// Stencil
			result = VK_IMAGE_ASPECT_STENCIL_BIT;
			break;
		case VK_FORMAT_D16_UNORM_S8_UINT:			// Depth/stencil
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			result = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (includeStencilBit) result |= VK_IMAGE_ASPECT_STENCIL_BIT;
			break;
		default: result = VK_IMAGE_ASPECT_COLOR_BIT; break;	// Color
	}
	return result;
}

VkFormatFeatureFlags util_vk_image_usage_to_format_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags result = (VkFormatFeatureFlags)0;
	if (VK_IMAGE_USAGE_SAMPLED_BIT == (usage & VK_IMAGE_USAGE_SAMPLED_BIT))
		result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (VK_IMAGE_USAGE_STORAGE_BIT == (usage & VK_IMAGE_USAGE_STORAGE_BIT))
		result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	if (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
		result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
		result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	return result;
}

VkQueueFlags util_to_vk_queue_flags(queuetype_t type)
{
	switch (type) {
		case QUEUE_TYPE_GRAPHICS: return VK_QUEUE_GRAPHICS_BIT;
		case QUEUE_TYPE_TRANSFER: return VK_QUEUE_TRANSFER_BIT;
		case QUEUE_TYPE_COMPUTE: return VK_QUEUE_COMPUTE_BIT;
		default: TC_ASSERT(false && "Invalid Queue Type"); return VK_QUEUE_FLAG_BITS_MAX_ENUM;
	}
}

VkDescriptorType util_to_vk_descriptor_type(descriptortype_t type)
{
	switch (type) {
		case DESCRIPTOR_TYPE_UNDEFINED: TC_ASSERT(false && "Invalid DescriptorInfo Type"); return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		case DESCRIPTOR_TYPE_SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
		case DESCRIPTOR_TYPE_TEXTURE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DESCRIPTOR_TYPE_UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DESCRIPTOR_TYPE_RW_TEXTURE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case DESCRIPTOR_TYPE_BUFFER:
		case DESCRIPTOR_TYPE_RW_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		case DESCRIPTOR_TYPE_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		case DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
#ifdef VK_RAYTRACING_AVAILABLE
		case DESCRIPTOR_TYPE_RAY_TRACING: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
#endif
		default:
			TC_ASSERT(false && "Invalid DescriptorInfo Type");
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
			break;
	}
}

VkShaderStageFlags util_to_vk_shader_stage_flags(shaderstage_t stages)
{
	VkShaderStageFlags res = 0;
	if (stages & SHADER_STAGE_ALL_GRAPHICS)
		return VK_SHADER_STAGE_ALL_GRAPHICS;
	if (stages & SHADER_STAGE_VERT)
		res |= VK_SHADER_STAGE_VERTEX_BIT;
	if (stages & SHADER_STAGE_GEOM)
		res |= VK_SHADER_STAGE_GEOMETRY_BIT;
	if (stages & SHADER_STAGE_TESE)
		res |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	if (stages & SHADER_STAGE_TESC)
		res |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	if (stages & SHADER_STAGE_COMP)
		res |= VK_SHADER_STAGE_COMPUTE_BIT;
#ifdef VK_RAYTRACING_AVAILABLE
	if (stages & SHADER_STAGE_RAYTRACING)
		res |= (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
		 VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR);
#endif
	TC_ASSERT(res != 0);
	return res;
}

void util_find_queue_family_index(
	const renderer_t* renderer, uint32_t nodeidx, queuetype_t queuetype, VkQueueFamilyProperties* outprops, uint8_t* outfamidx,
	uint8_t* outqueueidx)
{
	if (renderer->gpumode != GPU_MODE_LINKED) nodeidx = 0;

	uint32_t queuefamidx = UINT32_MAX;
	uint32_t queueidx = UINT32_MAX;
	VkQueueFlags requiredFlags = util_to_vk_queue_flags(queuetype);
	bool found = false;

	// Get queue family properties
	uint32_t queuefampropscount = 0;
	VkQueueFamilyProperties* queuefamprops = NULL;
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefampropscount, NULL);
	queuefamprops = (VkQueueFamilyProperties*)alloca(queuefampropscount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefampropscount, queuefamprops);

	uint32_t minflag = UINT32_MAX;
	for (uint32_t i = 0; i < queuefampropscount; ++i) {
		VkQueueFlags queueflags = queuefamprops[i].queueFlags;
		bool gfxqueue = (queueflags & VK_QUEUE_GRAPHICS_BIT) ? true : false;
		uint32_t flagand = (queueflags & requiredFlags);
		if (queuetype == QUEUE_TYPE_GRAPHICS && gfxqueue) {
			found = true;
			queuefamidx = i;
			queueidx = 0;
			break;
		}
		if ((queueflags & requiredFlags) && ((queueflags & ~requiredFlags) == 0) &&
			renderer->vulkan.usedqueues[nodeidx][queueflags] < renderer->vulkan.availablequeues[nodeidx][queueflags]) {
			found = true;
			queuefamidx = i;
			queueidx = renderer->vulkan.usedqueues[nodeidx][queueflags];
			break;
		}
		if (flagand && ((queueflags - flagand) < minflag) && !gfxqueue &&
			renderer->vulkan.usedqueues[nodeidx][queueflags] < renderer->vulkan.availablequeues[nodeidx][queueflags]) {
			found = true;
			minflag = (queueflags - flagand);
			queuefamidx = i;
			queueidx = renderer->vulkan.usedqueues[nodeidx][queueflags];
			break;
		}
	}
	// If hardware doesn't provide a dedicated queue try to find a non-dedicated one
	if (!found) {
		for (uint32_t i = 0; i < queuefampropscount; ++i) {
			VkQueueFlags queueFlags = queuefamprops[i].queueFlags;
			if ((queueFlags & requiredFlags) &&
				renderer->vulkan.usedqueues[nodeidx][queueFlags] < renderer->vulkan.availablequeues[nodeidx][queueFlags]) {
				found = true;
				queuefamidx = i;
				queueidx = renderer->vulkan.usedqueues[nodeidx][queueFlags];
				break;
			}
		}
	}
	if (!found) {
		found = true;
		queuefamidx = 0;
		queueidx = 0;
		TRACE(LOG_WARNING, "Could not find queue of type %u. Using default queue", (uint32_t)queuetype);
	}

	if (outprops) *outprops = queuefamprops[queuefamidx];
	if (outfamidx) *outfamidx = (uint8_t)queuefamidx;
	if (outqueueidx) *outqueueidx = (uint8_t)queueidx;
}

static VkPipelineCacheCreateFlags util_to_pipeline_cache_flags(pipelinecacheflags_t flags)
{
	VkPipelineCacheCreateFlags ret = 0;
#if VK_EXT_pipeline_creation_cache_control
	if (flags & PIPELINE_CACHE_FLAG_EXTERNALLY_SYNCHRONIZED) {
		ret |= VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT;
	}
#endif
	return ret;
}

uint32_t util_calculate_shared_device_mask(uint32_t gpucount) { return (1 << gpucount) - 1; }

void util_calculate_device_indices(renderer_t* renderer, uint32_t nodeidx, uint32_t* sharednodeidxs, uint32_t sharednodeidxcount, uint32_t* idxs)
{
	for (uint32_t i = 0; i < renderer->linkednodecount; ++i) idxs[i] = i;
	idxs[nodeidx] = nodeidx;
	for (uint32_t i = 0; i < sharednodeidxcount; ++i) idxs[sharednodeidxs[i]] = nodeidx;
}

void util_query_gpu_settings(VkPhysicalDevice gpu, VkPhysicalDeviceProperties2* gpuprops, VkPhysicalDeviceMemoryProperties* gpumemprops,
	VkPhysicalDeviceFeatures2KHR* gpufeats, VkQueueFamilyProperties** queuefamprops, uint32_t* queuefampropscount, gpusettings_t* gpusettings)
{
	memset(gpuprops, 0, sizeof(VkPhysicalDeviceProperties2));
	memset(gpumemprops, 0, sizeof(VkPhysicalDeviceMemoryProperties));
	memset(gpufeats, 0, sizeof(VkPhysicalDeviceFeatures2KHR));
	memset(gpusettings, 0, sizeof(gpusettings_t));
	*queuefamprops = NULL;
	*queuefampropscount = 0;
	
	vkGetPhysicalDeviceMemoryProperties(gpu, gpumemprops);
	gpufeats->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
#if VK_EXT_fragment_shader_interlock
	VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT f = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
	gpufeats->pNext = &f;
#endif
#ifndef NX64
	vkGetPhysicalDeviceFeatures2KHR(gpu, gpufeats);
#else
	vkGetPhysicalDeviceFeatures2(gpu, gpufeats);
#endif
	VkPhysicalDeviceSubgroupProperties p = { 0 };
	p.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
	p.pNext = NULL;
	gpuprops->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
	p.pNext = gpuprops->pNext;
	gpuprops->pNext = &p;
#if defined(NX64)
	vkGetPhysicalDeviceProperties2(gpu, gpuprops);
#else
	vkGetPhysicalDeviceProperties2KHR(gpu, gpuprops);
#endif
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, queuefampropscount, NULL);
	*queuefamprops = (VkQueueFamilyProperties*)tc_calloc(*queuefampropscount, sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, queuefampropscount, *queuefamprops);

	gpusettings->uniformbufalignment = (uint32_t)gpuprops->properties.limits.minUniformBufferOffsetAlignment;
	gpusettings->uploadbuftexalignment = (uint32_t)gpuprops->properties.limits.optimalBufferCopyOffsetAlignment;
	gpusettings->uploadbuftexrowalignment = (uint32_t)gpuprops->properties.limits.optimalBufferCopyRowPitchAlignment;
	gpusettings->maxvertexnputbindings = gpuprops->properties.limits.maxVertexInputBindings;
	gpusettings->multidrawindirect = gpufeats->features.multiDrawIndirect;
	gpusettings->indirectrootconstant = false;
	gpusettings->builtindrawid = true;

	gpusettings->wavelanecount = p.subgroupSize;
	gpusettings->waveopssupportflags = WAVE_OPS_SUPPORT_FLAG_NONE;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_BASIC_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_VOTE_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_VOTE_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_CLUSTERED_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_QUAD_BIT;
	if (p.supportedOperations & VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV)
		gpusettings->waveopssupportflags |= WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV;

#if VK_EXT_fragment_shader_interlock
	gpusettings->ROVsupported = (bool)f.fragmentShaderPixelInterlock;
#endif
	gpusettings->tessellationsupported = gpufeats->features.tessellationShader;
	gpusettings->geometryshadersupported = gpufeats->features.geometryShader;
	gpusettings->sampleranisotropysupported = gpufeats->features.samplerAnisotropy;
	
	sprintf(gpusettings->gpuvendorpreset.modelid, "%#x", gpuprops->properties.deviceID);	//save vendor and model Id as string
	sprintf(gpusettings->gpuvendorpreset.vendorid, "%#x", gpuprops->properties.vendorID);
	strncpy(gpusettings->gpuvendorpreset.gpuname, gpuprops->properties.deviceName, MAX_GPU_VENDOR_STRING_LENGTH);
	strncpy(gpusettings->gpuvendorpreset.revisionid, "0x00", MAX_GPU_VENDOR_STRING_LENGTH);
	gpusettings->gpuvendorpreset.presetlevel = get_preset_level(
		gpusettings->gpuvendorpreset.vendorid, gpusettings->gpuvendorpreset.modelid,
		gpusettings->gpuvendorpreset.revisionid);

    uint32_t major, minor, secondarybranch, tertiarybranch;
	switch ( util_to_internal_gpu_vendor(gpuprops->properties.vendorID)) {
	case GPU_VENDOR_NVIDIA:
        major = (gpuprops->properties.driverVersion >> 22) & 0x3ff;
        minor = (gpuprops->properties.driverVersion >> 14) & 0x0ff;
        secondarybranch = (gpuprops->properties.driverVersion >> 6) & 0x0ff;
        tertiarybranch = (gpuprops->properties.driverVersion) & 0x003f;
        sprintf(gpusettings->gpuvendorpreset.gpudriverversion, "%u.%u.%u.%u", major, minor, secondarybranch, tertiarybranch);
		break;
	default:
		uint32_t version = gpuprops->properties.driverVersion;
		char* outversionstr = gpusettings->gpuvendorpreset.gpudriverversion;
		TC_ASSERT(VK_MAX_DESCRIPTION_SIZE == TC_COUNT(outversionstr));
		sprintf(outversionstr, "%u.%u.%u", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
		break;
	}
	gpufeats->pNext = NULL;
	gpuprops->pNext = NULL;
}

static bool init_common(const char* app_name, const rendererdesc_t* desc, renderer_t* renderer)
{
	const char** layers = (const char**)alloca((2 + desc->vulkan.instancelayercount) * sizeof(char*));
	uint32_t num_layers = 0;
#if defined(ENABLE_GRAPHICS_DEBUG)
		layers[num_layers++] = "VK_LAYER_KHRONOS_validation";
#endif
#ifdef ENABLE_RENDER_DOC
		layers[num_layers++] = "VK_LAYER_RENDERDOC_Capture";
#endif
	for (uint32_t i = 0; i < (uint32_t)desc->vulkan.instancelayercount; ++i)
		layers[num_layers++] = desc->vulkan.instancelayers[i];

	VkResult vkRes = volkInitialize();
	if (vkRes != VK_SUCCESS) {
		TRACE(LOG_ERROR, "Failed to initialize Vulkan");
		return false;
	}

	const char* cache[MAX_INSTANCE_EXTENSIONS] = { 0 };	// These are the extensions that we have loaded
	uint32_t vklayercount = 0;
	uint32_t vkextcount = 0;
	vkEnumerateInstanceLayerProperties(&vklayercount, NULL);
	vkEnumerateInstanceExtensionProperties(NULL, &vkextcount, NULL);

	VkLayerProperties* vklayers = (VkLayerProperties*)alloca(sizeof(VkLayerProperties) * vklayercount);
	vkEnumerateInstanceLayerProperties(&vklayercount, vklayers);
	
	VkExtensionProperties* vkexts = (VkExtensionProperties*)alloca(sizeof(VkExtensionProperties) * vkextcount);
	vkEnumerateInstanceExtensionProperties(NULL, &vkextcount, vkexts);
#if VK_DEBUG_LOG_EXTENSIONS
	for (uint32_t i = 0; i < vklayercount; ++i)
		TRACE(LOG_INFO, "%s ( %s )", vklayers[i].layerName, "vkinstance-layer");
	for (uint32_t i = 0; i < vkextcount; ++i)
		TRACE(LOG_INFO, "%s ( %s )", vkexts[i].extensionName, "vkinstance-ext");
#endif
	VkApplicationInfo app_info = { 0 };
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = app_name;
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "TCEngine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = TARGET_VULKAN_API_VERSION;

	const char** temp = NULL;
	arrsetcap(temp, num_layers);
	for (uint32_t i = 0; i < num_layers; ++i) {
		bool found = false;
		for (uint32_t j = 0; j < vklayercount; ++j) {
			if (strcmp(layers[i], vklayers[j].layerName) == 0) {
				found = true;
				arrpush(temp, layers[i]);
				break;
			}
		}
		if (found == false) TRACE(LOG_WARNING, "%s ( %s )", layers[i], "vkinstance-layer-missing");
	}
	uint32_t extcount = 0;
	const uint32_t initialcount = sizeof(wantedinstanceextensions) / sizeof(wantedinstanceextensions[0]);
	const uint32_t requestedcount = (uint32_t)desc->vulkan.instanceextensioncount;
	const char** wantedext = NULL;
	arrsetlen(wantedext, initialcount + num_layers);
	for (uint32_t i = 0; i < initialcount; ++i)
		wantedext[i] = wantedinstanceextensions[i];
	for (uint32_t i = 0; i < requestedcount; ++i)
		wantedext[initialcount + i] = desc->vulkan.instanceextensions[i];
	const uint32_t wantedext_count = (uint32_t)arrlen(wantedext);
	for (ptrdiff_t i = 0; i < arrlen(temp); ++i) {
		const char* layer_name = temp[i];
		uint32_t count = 0;
		vkEnumerateInstanceExtensionProperties(layer_name, &count, NULL);
		VkExtensionProperties* properties = count ? (VkExtensionProperties*)tc_calloc(count, sizeof(*properties)) : NULL;
		TC_ASSERT(properties != NULL || count == 0);
		vkEnumerateInstanceExtensionProperties(layer_name, &count, properties);
		for (uint32_t j = 0; j < count; ++j) {
			for (uint32_t k = 0; k < wantedext_count; ++k) {
				if (strcmp(wantedext[k], properties[j].extensionName) == 0) {
					if (strcmp(wantedext[k], VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME) == 0)
						devicegroupcreationext = true;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
					if (strcmp(wantedext[k], VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) debugext = true;
#endif
					cache[extcount++] = wantedext[k];
					wantedext[k] = "";	// clear wanted extension so we dont load it more then once
					break;
				}
			}
		}
		tc_free(properties);
	}
	const char* layer_name = NULL;
	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(layer_name, &count, NULL);
	if (count > 0) {
		VkExtensionProperties* properties = (VkExtensionProperties*)tc_calloc(count, sizeof(*properties));
		TC_ASSERT(properties != NULL);
		vkEnumerateInstanceExtensionProperties(layer_name, &count, properties);
		for (uint32_t j = 0; j < count; ++j) {
			for (uint32_t k = 0; k < wantedext_count; ++k) {
				if (strcmp(wantedext[k], properties[j].extensionName) == 0) {
					cache[extcount++] = wantedext[k];
					if (strcmp(wantedext[k], VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME) == 0)
						devicegroupcreationext = true;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
					if (strcmp(wantedext[k], VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) debugext = true;
#endif
					break;
				}
			}
		}
		tc_free(properties);
	}

#if VK_HEADER_VERSION >= 108
	VkValidationFeaturesEXT validationext = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	VkValidationFeatureEnableEXT enabledfeats[] = { VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT };
	if (desc->enablegpubasedvalidation) {
		validationext.enabledValidationFeatureCount = 1;
		validationext.pEnabledValidationFeatures = enabledfeats;
	}
#endif
	VkInstanceCreateInfo instance_info = { 0 };
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if VK_HEADER_VERSION >= 108
	instance_info.pNext = &validationext;
#endif
	instance_info.pApplicationInfo = &app_info;
	instance_info.enabledLayerCount = (uint32_t)arrlen(temp);
	instance_info.ppEnabledLayerNames = temp;
	instance_info.enabledExtensionCount = extcount;
	instance_info.ppEnabledExtensionNames = cache;

	TRACE(LOG_INFO, "Creating VkInstance with %i enabled instance layers:", (int)arrlen(temp));
	for (int i = 0; i < arrlen(temp); i++) TRACE(LOG_INFO, "\tLayer %i: %s", i, temp[i]);

	CHECK_VKRESULT(vkCreateInstance(&instance_info, &alloccbs, &(renderer->vulkan.instance)));
	arrfree(temp);
	arrfree(wantedext);

	volkLoadInstance(renderer->vulkan.instance);
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
	if (debugext) {
		VkDebugUtilsMessengerCreateInfoEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		info.pfnUserCallback = _internal_debug_report_callback;
		info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		info.flags = 0;
		info.pUserData = NULL;
		VkResult res = vkCreateDebugUtilsMessengerEXT(renderer->vulkan.instance, &info, &alloccbs, &(renderer->vulkan.debugutilsmessenger));
		if (VK_SUCCESS != res) {
			TRACE(LOG_ERROR, "%s ( %s )", "vkCreateDebugUtilsMessengerEXT failed - disabling Vulkan debug callbacks",
				"internal_vk_init_instance");
		}
	}
#else
#if defined(__ANDROID__)
	if (vkCreateDebugReportCallbackEXT)
#endif
	{
		VkDebugReportCallbackCreateInfoEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		info.pNext = NULL;
		info.pfnCallback = internal_debug_report_callback;
		info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
#if defined(__ANDROID__)
			VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
#endif
			VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
		VkResult res = vkCreateDebugReportCallbackEXT(renderer->vulkan.instance, &info, &alloccbs, &(renderer->vulkan.debugreport));
		if (VK_SUCCESS != res) {
			TRACE(LOG_ERROR, "%s ( %s )", "vkCreateDebugReportCallbackEXT failed - disabling Vulkan debug callbacks", "internal_vk_init_instance");
		}
	}
#endif
	renderer->unlinkedrendererindex = 0;
	renderer->vulkan.owninstance = true;
	return true;
}

static void exit_common(renderer_t* renderer)
{
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.instance);
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
	if (renderer->vulkan.debugutilsmessenger) {
		vkDestroyDebugUtilsMessengerEXT(renderer->vulkan.instance, renderer->vulkan.debugutilsmessenger, &alloccbs);
		renderer->vulkan.debugutilsmessenger = NULL;
	}
#else
	if (renderer->vulkan.debugreport) {
		vkDestroyDebugReportCallbackEXT(renderer->vulkan.instance, renderer->vulkan.debugreport, &alloccbs);
		renderer->vulkan.debugreport = NULL;
	}
#endif
	vkDestroyInstance(renderer->vulkan.instance, &alloccbs);
}

static bool _is_gpu_better(uint32_t test, uint32_t ref, const gpusettings_t* gpusettings,
	const VkPhysicalDeviceProperties2* gpuprops, const VkPhysicalDeviceMemoryProperties* gpumemprops)
{
	const gpusettings_t testsettings = gpusettings[test];
	const gpusettings_t refsettings = gpusettings[ref];

	// First test the preset level
	if (testsettings.gpuvendorpreset.presetlevel != refsettings.gpuvendorpreset.presetlevel)
		return testsettings.gpuvendorpreset.presetlevel > refsettings.gpuvendorpreset.presetlevel;

	// Next test discrete vs integrated/software
	const VkPhysicalDeviceProperties testprops = gpuprops[test].properties;
	const VkPhysicalDeviceProperties refprops = gpuprops[ref].properties;
	// If first is a discrete gpu and second is not discrete (integrated, software, ...), always prefer first
	if (testprops.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && refprops.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		return true;

	// If first is not a discrete gpu (integrated, software, ...) and second is a discrete gpu, always prefer second
	if (testprops.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && refprops.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		return false;

	// Compare by VRAM if both gpu's are of same type (integrated vs discrete)
	if (testprops.vendorID == refprops.vendorID && testprops.deviceID == refprops.deviceID) {
		const VkPhysicalDeviceMemoryProperties testmem = gpumemprops[test];
		const VkPhysicalDeviceMemoryProperties refmem = gpumemprops[ref];
		//if presets are the same then sort by vram size
		VkDeviceSize testvram = 0;
		VkDeviceSize refvram = 0;
		for (uint32_t i = 0; i < testmem.memoryHeapCount; ++i)
			if (VK_MEMORY_HEAP_DEVICE_LOCAL_BIT & testmem.memoryHeaps[i].flags)
				testvram += testmem.memoryHeaps[i].size;
		for (uint32_t i = 0; i < refmem.memoryHeapCount; ++i)
			if (VK_MEMORY_HEAP_DEVICE_LOCAL_BIT & refmem.memoryHeaps[i].flags)
				refvram += refmem.memoryHeaps[i].size;
		return testvram >= refvram;
	}
	return false;
};

static bool _select_best_gpu(renderer_t* renderer)
{
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.instance);
	uint32_t count = 0;
	CHECK_VKRESULT(vkEnumeratePhysicalDevices(renderer->vulkan.instance, &count, NULL));
	if (count < 1) {
		TRACE(LOG_ERROR, "Failed to enumerate any physical Vulkan devices");
		TC_ASSERT(count);
		return false;
	}
	VkPhysicalDevice* gpus = (VkPhysicalDevice*)alloca(count * sizeof(VkPhysicalDevice));
	VkPhysicalDeviceProperties2* gpuprops = (VkPhysicalDeviceProperties2*)alloca(count * sizeof(VkPhysicalDeviceProperties2));
	VkPhysicalDeviceMemoryProperties* gpumemprops = (VkPhysicalDeviceMemoryProperties*)alloca(count * sizeof(VkPhysicalDeviceMemoryProperties));
	VkPhysicalDeviceFeatures2KHR* gpufeats = (VkPhysicalDeviceFeatures2KHR*)alloca(count * sizeof(VkPhysicalDeviceFeatures2KHR));
	VkQueueFamilyProperties** queuefamprops = (VkQueueFamilyProperties**)alloca(count * sizeof(VkQueueFamilyProperties*));
	uint32_t* queuefampropscount = (uint32_t*)alloca(count * sizeof(uint32_t));
	CHECK_VKRESULT(vkEnumeratePhysicalDevices(renderer->vulkan.instance, &count, gpus));

	uint32_t gpuidx = UINT32_MAX;
	gpusettings_t* gpusettings = (gpusettings_t*)alloca(count * sizeof(gpusettings_t));
	for (uint32_t i = 0; i < count; ++i) {
		util_query_gpu_settings(gpus[i], &gpuprops[i], &gpumemprops[i], &gpufeats[i], &queuefamprops[i], &queuefampropscount[i], &gpusettings[i]);
		TRACE(LOG_INFO, "GPU[%i] detected. Vendor ID: %s, Model ID: %s, Preset: %s, GPU Name: %s", i,
			gpusettings[i].gpuvendorpreset.vendorid, gpusettings[i].gpuvendorpreset.modelid,
			presettostr(gpusettings[i].gpuvendorpreset.presetlevel), gpusettings[i].gpuvendorpreset.gpuname);

		if (gpuidx == UINT32_MAX || _is_gpu_better(i, gpuidx, gpusettings, gpuprops, gpumemprops)) {
			VkQueueFamilyProperties* props = queuefamprops[i];
			for (uint32_t j = 0; j < queuefampropscount[i]; j++) {		//select if graphics queue is available
				if (props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {		//get graphics queue family
					gpuidx = i;
					break;
				}
			}
		}
	}
	if (VK_PHYSICAL_DEVICE_TYPE_CPU == gpuprops[gpuidx].properties.deviceType) {
		TRACE(LOG_ERROR, "The only available GPU is of type VK_PHYSICAL_DEVICE_TYPE_CPU. Early exiting");
		TC_ASSERT(false);
		return false;
	}

	TC_ASSERT(gpuidx != UINT32_MAX);
	renderer->vulkan.activegpu = gpus[gpuidx];
	renderer->vulkan.activegpuprops = (VkPhysicalDeviceProperties2*)tc_malloc(sizeof(VkPhysicalDeviceProperties2));
	renderer->activegpusettings = (gpusettings_t*)tc_malloc(sizeof(gpusettings_t));
	*renderer->vulkan.activegpuprops = gpuprops[gpuidx];
	renderer->vulkan.activegpuprops->pNext = NULL;
	*renderer->activegpusettings = gpusettings[gpuidx];
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.activegpu);

	TRACE(LOG_INFO, "GPU[%d] is selected as default GPU", gpuidx);
	TRACE(LOG_INFO, "Name of selected gpu: %s", renderer->activegpusettings->gpuvendorpreset.gpuname);
	TRACE(LOG_INFO, "Vendor id of selected gpu: %s", renderer->activegpusettings->gpuvendorpreset.vendorid);
	TRACE(LOG_INFO, "Model id of selected gpu: %s", renderer->activegpusettings->gpuvendorpreset.modelid);
	TRACE(LOG_INFO, "Preset of selected gpu: %s", presettostr(renderer->activegpusettings->gpuvendorpreset.presetlevel));

	for (uint32_t i = 0; i < count; ++i) tc_free(queuefamprops[i]);
	return true;
}

static bool add_device(const rendererdesc_t* desc, renderer_t* renderer)
{
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.instance);
	const char* cache[MAX_DEVICE_EXTENSIONS] = { 0 };
#if VK_KHR_device_group_creation
	VkDeviceGroupDeviceCreateInfoKHR info = { VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO_KHR };
	VkPhysicalDeviceGroupPropertiesKHR props[MAX_LINKED_GPUS] = { 0 };
	renderer->linkednodecount = 1;
	if (renderer->gpumode == GPU_MODE_LINKED && devicegroupcreationext) {
		uint32_t count = 0;
		vkEnumeratePhysicalDeviceGroupsKHR(renderer->vulkan.instance, &count, NULL);	// Query the number of device groups
		for (uint32_t i = 0; i < count; i++) {	// Allocate and initialize structures to query the device groups
			props[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHR;
			props[i].pNext = NULL;
		}
		CHECK_VKRESULT(vkEnumeratePhysicalDeviceGroupsKHR(renderer->vulkan.instance, &count, props));
		// If the first device group has more than one physical device create a logical device using all of the physical devices.
		for (uint32_t i = 0; i < count; i++) {
			if (props[i].physicalDeviceCount > 1) {
				info.physicalDeviceCount = props[i].physicalDeviceCount;
				info.pPhysicalDevices = props[i].physicalDevices;
				renderer->linkednodecount = info.physicalDeviceCount;
				break;
			}
		}
	}
#endif
	if (renderer->linkednodecount < 2 && renderer->gpumode == GPU_MODE_LINKED)
		renderer->gpumode = GPU_MODE_SINGLE;
	if (!desc->context)
		if (!_select_best_gpu(renderer)) return false;
	else {
		TC_ASSERT(desc->gpuindex < desc->context->gpucount);
		renderer->vulkan.activegpu = desc->context->gpus[desc->gpuindex].vulkan.gpu;
		renderer->vulkan.activegpuprops = (VkPhysicalDeviceProperties2*)tc_malloc(sizeof(VkPhysicalDeviceProperties2));
		renderer->activegpusettings = (gpusettings_t*)tc_malloc(sizeof(gpusettings_t));
		*renderer->vulkan.activegpuprops = desc->context->gpus[desc->gpuindex].vulkan.gpuprops;
		renderer->vulkan.activegpuprops->pNext = NULL;
		*renderer->activegpusettings = desc->context->gpus[desc->gpuindex].settings;
	}
	uint32_t layercount = 0;
	uint32_t extcount = 0;
	vkEnumerateDeviceLayerProperties(renderer->vulkan.activegpu, &layercount, NULL);
	vkEnumerateDeviceExtensionProperties(renderer->vulkan.activegpu, NULL, &extcount, NULL);
	VkLayerProperties* layers = (VkLayerProperties*)alloca(sizeof(VkLayerProperties) * layercount);
	vkEnumerateDeviceLayerProperties(renderer->vulkan.activegpu, &layercount, layers);
	VkExtensionProperties* exts = (VkExtensionProperties*)alloca(sizeof(VkExtensionProperties) * extcount);
	vkEnumerateDeviceExtensionProperties(renderer->vulkan.activegpu, NULL, &extcount, exts);
	for (uint32_t i = 0; i < layercount; ++i)
		if (strcmp(layers[i].layerName, "VK_LAYER_RENDERDOC_Capture") == 0)
			renderdoclayer = true;
#if VK_DEBUG_LOG_EXTENSIONS
	for (uint32_t i = 0; i < layercount; ++i) TRACE(LOG_INFO, "%s ( %s )", layers[i].layerName, "vkdevice-layer");
	for (uint32_t i = 0; i < extcount; ++i) TRACE(LOG_INFO, "%s ( %s )", exts[i].extensionName, "vkdevice-ext");
#endif
	extcount = 0;
	bool dedicatedalloc = false;
	bool memreqext = false;
#if VK_EXT_fragment_shader_interlock
	bool fraginterlockext = false;
#endif
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	bool externalMemoryExtension = false;
	bool externalMemoryWin32Extension = false;
#endif
	const char* layer_name = NULL;
	uint32_t initialcount = sizeof(wanteddeviceextensions) / sizeof(wanteddeviceextensions[0]);
	const uint32_t userRequestedCount = (uint32_t)desc->vulkan.deviceextensioncount;
	const char** wanted = NULL;
	arrsetlen(wanted, initialcount + userRequestedCount);
	for (uint32_t i = 0; i < initialcount; ++i)
		wanted[i] = wanteddeviceextensions[i];
	for (uint32_t i = 0; i < userRequestedCount; ++i)
		wanted[initialcount + i] = desc->vulkan.deviceextensions[i];
	const uint32_t wanted_extcount = (uint32_t)arrlen(wanted);
	uint32_t       count = 0;
	vkEnumerateDeviceExtensionProperties(renderer->vulkan.activegpu, layer_name, &count, NULL);
	if (count > 0) {
		VkExtensionProperties* properties = (VkExtensionProperties*)tc_calloc(count, sizeof(*properties));
		TC_ASSERT(properties != NULL);
		vkEnumerateDeviceExtensionProperties(renderer->vulkan.activegpu, layer_name, &count, properties);
		for (uint32_t j = 0; j < count; ++j) {
			for (uint32_t k = 0; k < wanted_extcount; ++k) {
				if (strcmp(wanted[k], properties[j].extensionName) == 0) {
					cache[extcount++] = wanted[k];

#ifndef ENABLE_DEBUG_UTILS_EXTENSION
					if (strcmp(wanted[k], VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0)
						renderer->vulkan.debugmarkersupport = true;
#endif
					if (strcmp(wanted[k], VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
						dedicatedalloc = true;
					if (strcmp(wanted[k], VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
						memreqext = true;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
					if (strcmp(wanted[k], VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0)
						externalMemoryExtension = true;
					if (strcmp(wanted[k], VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0)
						externalMemoryWin32Extension = true;
#endif
#if VK_KHR_draw_indirect_count
					if (strcmp(wanted[k], VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0)
						renderer->vulkan.drawindirectcountextension = true;
#endif
					if (strcmp(wanted[k], VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0)
						renderer->vulkan.AMDdrawindirectcountextension = true;
					if (strcmp(wanted[k], VK_AMD_GCN_SHADER_EXTENSION_NAME) == 0)
						renderer->vulkan.AMDGCNshaderextension = true;
#if VK_EXT_descriptor_indexing
					if (strcmp(wanted[k], VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
						renderer->vulkan.descriptorindexingextension = true;
#endif
#ifdef VK_RAYTRACING_AVAILABLE
					uint32_t khrrt = 1; // KHRONOS VULKAN RAY TRACING
					if (strcmp(wanted[k], VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) == 0)
						renderer->vulkan.shaderfloatcontrolsextension = 1;
					khrrt &= renderer->vulkan.shaderfloatcontrolsextension;
					if (strcmp(wanted[k], VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
						renderer->vulkan.bufferdeviceaddressextension = 1;
					khrrt &= renderer->vulkan.bufferdeviceaddressextension;
					if (strcmp(wanted[k], VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
						renderer->vulkan.deferredhostoperationsextension = 1;
					khrrt &= renderer->vulkan.deferredhostoperationsextension;
					if (strcmp(wanted[k], VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
						renderer->vulkan.KHRaccelerationstructureextension = 1;
					khrrt &= renderer->vulkan.KHRaccelerationstructureextension;
					if (strcmp(wanted[k], VK_KHR_SPIRV_1_4_EXTENSION_NAME) == 0)
						renderer->vulkan.KHRspirv14extension = 1;
					khrrt &= renderer->vulkan.KHRspirv14extension;
					if (strcmp(wanted[k], VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
						renderer->vulkan.KHRRaytracingpipelineextension = 1;
					khrrt &= renderer->vulkan.KHRRaytracingpipelineextension;
					if (khrrt) renderer->vulkan.raytracingsupported = 1;
					if (strcmp(wanted[k], VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
						renderer->vulkan.KHRrayqueryextension = 1;
#endif
#if VK_KHR_sampler_ycbcr_conversion
					if (strcmp(wanted[k], VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) == 0)
						renderer->vulkan.YCbCrextension = true;
#endif
#if VK_EXT_fragment_shader_interlock
					if (strcmp(wanted[k], VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME) == 0)
						fraginterlockext = true;
#endif
					break;
				}
			}
		}
		tc_free((void*)properties);
	}
	arrfree(wanted);
#define ADD_TO_NEXT_CHAIN(condition, next)        \
	if ((condition)) {                            \
		base->pNext = (VkBaseOutStructure*)&next; \
		base = (VkBaseOutStructure*)base->pNext;  \
	}

	VkPhysicalDeviceFeatures2KHR gpufeats2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };
	VkBaseOutStructure* base = (VkBaseOutStructure*)&gpufeats2;
#if VK_EXT_fragment_shader_interlock
	VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlockFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
	ADD_TO_NEXT_CHAIN(fraginterlockext, fragmentShaderInterlockFeatures);
#endif
#if VK_EXT_descriptor_indexing
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };
	ADD_TO_NEXT_CHAIN(renderer->vulkan.descriptorindexingextension, descriptorIndexingFeatures);
#endif
#if VK_KHR_sampler_ycbcr_conversion
	VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES };
	ADD_TO_NEXT_CHAIN(renderer->vulkan.YCbCrextension, ycbcrFeatures);
#endif
#if VK_KHR_buffer_device_address
	VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddressFeatures = { 0 };
	enabledBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	enabledBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
	ADD_TO_NEXT_CHAIN(renderer->vulkan.bufferdeviceaddressextension, enabledBufferDeviceAddressFeatures);
#endif
#if VK_KHR_ray_tracing_pipeline
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures = { 0 };
	enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	ADD_TO_NEXT_CHAIN(renderer->vulkan.KHRRaytracingpipelineextension, enabledRayTracingPipelineFeatures); 
#endif
#if VK_KHR_acceleration_structure
	VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures = { 0 };
	enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
	ADD_TO_NEXT_CHAIN(renderer->vulkan.KHRaccelerationstructureextension, enabledAccelerationStructureFeatures);
#endif
#if VK_KHR_ray_query
	VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures = { 0 };
	enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	enabledRayQueryFeatures.rayQuery = VK_TRUE; 
	ADD_TO_NEXT_CHAIN(renderer->vulkan.KHRrayqueryextension, enabledRayQueryFeatures); 
#endif
	vkGetPhysicalDeviceFeatures2KHR(renderer->vulkan.activegpu, &gpufeats2);

	uint32_t queuefamcount = 0;		// Get queue family properties
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefamcount, NULL);
	VkQueueFamilyProperties* queuefamiliesprops = (VkQueueFamilyProperties*)alloca(queuefamcount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefamcount, queuefamiliesprops);
	
	float priorities[MAX_QUEUE_FAMILIES][MAX_QUEUE_COUNT] = { 0 };  // max queue families * max queue count: need a queue_priority for each queue in the queue family we create
	uint32_t queue_infos_count = 0;
	VkDeviceQueueCreateInfo* queue_infos = (VkDeviceQueueCreateInfo*)alloca(queuefamcount * sizeof(VkDeviceQueueCreateInfo));

	const uint32_t maxQueueFlag = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT | VK_QUEUE_PROTECTED_BIT;
	renderer->vulkan.availablequeues = (uint32_t**)tc_malloc(renderer->linkednodecount * sizeof(uint32_t*));
	renderer->vulkan.usedqueues = (uint32_t**)tc_malloc(renderer->linkednodecount * sizeof(uint32_t*));
	for (uint32_t i = 0; i < renderer->linkednodecount; i++) {
		renderer->vulkan.availablequeues[i] = (uint32_t*)tc_calloc(maxQueueFlag, sizeof(uint32_t));
		renderer->vulkan.usedqueues[i] = (uint32_t*)tc_calloc(maxQueueFlag, sizeof(uint32_t));
	}
	for (uint32_t i = 0; i < queuefamcount; i++) {
		uint32_t num_queues = queuefamiliesprops[i].queueCount;
		if (num_queues > 0) {	// Request only one queue of each type if mRequestAllAvailableQueues is not set to true
			if (num_queues > 1 && !desc->vulkan.requestallavailablequeues) num_queues = 1;
			TC_ASSERT(num_queues <= MAX_QUEUE_COUNT);
			num_queues = min(num_queues, MAX_QUEUE_COUNT);
			memset(&queue_infos[queue_infos_count], 0, sizeof(VkDeviceQueueCreateInfo));
			queue_infos[queue_infos_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_infos[queue_infos_count].pNext = NULL;
			queue_infos[queue_infos_count].flags = 0;
			queue_infos[queue_infos_count].queueFamilyIndex = i;
			queue_infos[queue_infos_count].queueCount = num_queues;
			queue_infos[queue_infos_count].pQueuePriorities = priorities[i];
			queue_infos_count++;
			for (uint32_t n = 0; n < renderer->linkednodecount; ++n)
				renderer->vulkan.availablequeues[n][queuefamiliesprops[i].queueFlags] = num_queues;
		}
	}
	VkDeviceCreateInfo info2 = { 0 };
	info2.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info2.pNext = &gpufeats2;
	info2.queueCreateInfoCount = queue_infos_count;
	info2.pQueueCreateInfos = queue_infos;
	info2.enabledExtensionCount = extcount;
	info2.ppEnabledExtensionNames = cache;
#if VK_KHR_device_group_creation
	ADD_TO_NEXT_CHAIN(renderer->gpumode == GPU_MODE_LINKED, info2);
#endif
	CHECK_VKRESULT(vkCreateDevice(renderer->vulkan.activegpu, &info2, &alloccbs, &renderer->vulkan.device));
	if (renderer->gpumode != GPU_MODE_UNLINKED)	// Load Vulkan device functions to bypass loader
		volkLoadDevice(renderer->vulkan.device);

	renderer->vulkan.dedicatedallocationextension = dedicatedalloc && memreqext;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	renderer->vulkan.externalmemoryextension = externalMemoryExtension && externalMemoryWin32Extension;
#endif
	if (renderer->vulkan.dedicatedallocationextension)
		TRACE(LOG_INFO, "Successfully loaded Dedicated Allocation extension");
	if (renderer->vulkan.externalmemoryextension)
		TRACE(LOG_INFO, "Successfully loaded External Memory extension");
#if VK_KHR_draw_indirect_count
	if (renderer->vulkan.drawindirectcountextension) {
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountKHR;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountKHR;
		TRACE(LOG_INFO, "Successfully loaded Draw Indirect extension");
	}
	else if (renderer->vulkan.AMDdrawindirectcountextension)
#endif
	{
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountAMD;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountAMD;
		TRACE(LOG_INFO, "Successfully loaded AMD Draw Indirect extension");
	}
	if (renderer->vulkan.AMDGCNshaderextension)
		TRACE(LOG_INFO, "Successfully loaded AMD GCN Shader extension");
	if (renderer->vulkan.descriptorindexingextension)
		TRACE(LOG_INFO, "Successfully loaded Descriptor Indexing extension");
	if (renderer->vulkan.raytracingsupported)
		TRACE(LOG_INFO, "Successfully loaded Khronos Ray Tracing extensions");
#ifdef _ENABLE_DEBUG_UTILS_EXTENSION
	renderer->vulkan.debugmarkersupport = vkCmdBeginDebugUtilsLabelEXT && vkCmdEndDebugUtilsLabelEXT && vkCmdInsertDebugUtilsLabelEXT && vkSetDebugUtilsObjectNameEXT;
#endif
	vk_utils_caps_builder(renderer);
	return true;
}

static void remove_device(renderer_t* renderer)
{
	vkDestroyDescriptorSetLayout(renderer->vulkan.device, renderer->vulkan.emptydescriptorsetlayout, &alloccbs);
	vkDestroyDescriptorPool(renderer->vulkan.device, renderer->vulkan.emptydescriptorpool, &alloccbs);
	vkDestroyDevice(renderer->vulkan.device, &alloccbs);
	tc_free(renderer->activegpusettings);
	tc_free(renderer->vulkan.activegpuprops);
}

VkDeviceMemory get_vk_device_memory(renderer_t* renderer, buffer_t* pBuffer)
{
	VmaAllocationInfo info = { 0 };
	vmaGetAllocationInfo(renderer->vulkan.vmaAllocator, pBuffer->vulkan.allocation, &info);
	return info.deviceMemory;
}

uint64_t get_vk_device_memory_offset(renderer_t* renderer, buffer_t* pBuffer)
{
	VmaAllocationInfo info = { 0 };
	vmaGetAllocationInfo(renderer->vulkan.vmaAllocator, pBuffer->vulkan.allocation, &info);
	return (uint64_t)info.offset;
}

static uint32_t renderer_count = 0;

void vk_init_renderer(const char* app_name, const rendererdesc_t* desc, renderer_t** rendererptr)
{
	TC_ASSERT(app_name && desc && rendererptr);
	uint8_t* mem = (uint8_t*)tc_memalign(alignof(renderer_t), sizeof(renderer_t) + sizeof(nulldescriptors_t));
	TC_ASSERT(mem);
	memset(mem, 0, sizeof(renderer_t) + sizeof(nulldescriptors_t));

	renderer_t* renderer = (renderer_t*)mem;
	renderer->gpumode = desc->gpumode;
	renderer->shadertarget = desc->shadertarget;
	renderer->enablegpubasedvalidation = desc->enablegpubasedvalidation;
	renderer->nulldescriptors = (nulldescriptors_t*)(mem + sizeof(renderer_t));
	renderer->name = (char*)tc_calloc(strlen(app_name) + 1, sizeof(char));
	strcpy(renderer->name, app_name);
	TC_ASSERT(desc->gpumode != GPU_MODE_UNLINKED || desc->context); // context required in unlinked mode
	if (desc->context) {
		TC_ASSERT(desc->gpuindex < desc->context->gpucount);
		renderer->vulkan.instance = desc->context->vulkan.instance;
		renderer->vulkan.owninstance = false;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		renderer->vulkan.debugutilsmessenger = desc->context->vulkan.debugutilsmessenger;
#else
		renderer->vulkan.pVkDebugReport = desc->context->vulkan.pVkDebugReport;
#endif
		renderer->unlinkedrendererindex = renderer_count;
	}
	else if (!init_common(app_name, desc, renderer)) {
		tc_free(renderer->name);
		tc_freealign(renderer);
		return;
	}
	if (!add_device(desc, renderer)) {
		if (renderer->vulkan.owninstance) exit_common(renderer);
		tc_free(renderer->name);
		tc_freealign(renderer);
		return;
	}
	VmaAllocatorCreateInfo info = { 0 };
	info.device = renderer->vulkan.device;
	info.physicalDevice = renderer->vulkan.activegpu;
	info.instance = renderer->vulkan.instance;
	if (renderer->vulkan.dedicatedallocationextension)
		info.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
	if (renderer->vulkan.bufferdeviceaddressextension)
		info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	VmaVulkanFunctions funcs = { 0 };
	funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	funcs.vkAllocateMemory = vkAllocateMemory;
	funcs.vkBindBufferMemory = vkBindBufferMemory;
	funcs.vkBindImageMemory = vkBindImageMemory;
	funcs.vkCreateBuffer = vkCreateBuffer;
	funcs.vkCreateImage = vkCreateImage;
	funcs.vkDestroyBuffer = vkDestroyBuffer;
	funcs.vkDestroyImage = vkDestroyImage;
	funcs.vkFreeMemory = vkFreeMemory;
	funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
	funcs.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
	funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
	funcs.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
	funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
	funcs.vkMapMemory = vkMapMemory;
	funcs.vkUnmapMemory = vkUnmapMemory;
	funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
	funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;
#if VMA_BIND_MEMORY2 || VMA_VULKAN_VERSION >= 1001000
	funcs.vkBindBufferMemory2KHR = vkBindBufferMemory2KHR;
	
	funcs.vkBindImageMemory2KHR = vkBindImageMemory2KHR;
#endif
#if VMA_MEMORY_BUDGET || VMA_VULKAN_VERSION >= 1001000
#ifdef NX64
	funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
#else
	funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR;
#endif
#endif
#if VMA_VULKAN_VERSION >= 1003000
	funcs.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
	funcs.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
#endif
	info.pVulkanFunctions = &funcs;
	info.pAllocationCallbacks = &alloccbs;
	vmaCreateAllocator(&info, &renderer->vulkan.vmaAllocator);

	// Empty descriptor set for filling in gaps when example: set 1 is used but set 0 is not used in the shader.
	// We still need to bind empty descriptor set here to keep some drivers happy
	VkDescriptorPoolSize descriptorPoolSizes[1] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1 } };
	add_descriptor_pool(renderer, 1, 0, descriptorPoolSizes, 1, &renderer->vulkan.emptydescriptorpool);
	VkDescriptorSetLayoutCreateInfo info3 = { 0 };
	VkDescriptorSet* emptySets[] = { &renderer->vulkan.emptydescriptorset };
	info3.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	CHECK_VKRESULT(vkCreateDescriptorSetLayout(renderer->vulkan.device, &info3, &alloccbs, &renderer->vulkan.emptydescriptorsetlayout));
	consume_descriptor_sets(renderer->vulkan.device, renderer->vulkan.emptydescriptorpool, &renderer->vulkan.emptydescriptorsetlayout, 1, emptySets);

	VkPhysicalDeviceFeatures2KHR gpufeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };
	vkGetPhysicalDeviceFeatures2KHR(renderer->vulkan.activegpu, &gpufeats);
	renderer->vulkan.shadersampledimagearraydynamicindexingsupported = (uint32_t)(gpufeats.features.shaderSampledImageArrayDynamicIndexing);
	if (renderer->vulkan.shadersampledimagearraydynamicindexingsupported)
		TRACE(LOG_INFO, "GPU supports texture array dynamic indexing");

	util_find_queue_family_index(renderer, 0, QUEUE_TYPE_GRAPHICS, NULL, &renderer->vulkan.graphicsqueuefamilyidx, NULL);
	util_find_queue_family_index(renderer, 0, QUEUE_TYPE_COMPUTE, NULL, &renderer->vulkan.computequeuefamilyidx, NULL);
	util_find_queue_family_index(renderer, 0, QUEUE_TYPE_TRANSFER, NULL, &renderer->vulkan.transferqueuefamilyidx, NULL);
	add_default_resources(renderer);
	++renderer_count;
	TC_ASSERT(renderer_count <= MAX_UNLINKED_GPUS);
	*rendererptr = renderer;
}

void vk_exit_renderer(renderer_t* renderer)
{
	TC_ASSERT(renderer);
	--renderer_count;
	remove_default_resources(renderer);
	vmaDestroyAllocator(renderer->vulkan.vmaAllocator);
	remove_device(renderer);
	if (renderer->vulkan.owninstance)
		exit_common(renderer);

	for (uint32_t i = 0; i < renderer->linkednodecount; i++) {
		tc_free(renderer->vulkan.availablequeues[i]);
		tc_free(renderer->vulkan.usedqueues[i]);
	}
	tc_free(renderer->vulkan.availablequeues);
	tc_free(renderer->vulkan.usedqueues);
	tc_free(renderer->capbits);
	tc_free(renderer->name);
	tc_free(renderer);
}

void vk_add_fence(renderer_t* renderer, fence_t* fence)
{
	TC_ASSERT(renderer && fence);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	memset(fence, 0, sizeof(fence_t));
	VkFenceCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	CHECK_VKRESULT(vkCreateFence(renderer->vulkan.device, &info, &alloccbs, &fence->vulkan.fence));
	fence->vulkan.submitted = false;
}

void vk_remove_fence(renderer_t* renderer, fence_t* fence)
{
	TC_ASSERT(renderer && fence);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != fence->vulkan.fence);
	vkDestroyFence(renderer->vulkan.device, fence->vulkan.fence, &alloccbs);
}

void vk_add_semaphore(renderer_t* renderer, semaphore_t* semaphore)
{
	TC_ASSERT(renderer && semaphore);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	VkSemaphoreCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	CHECK_VKRESULT(vkCreateSemaphore(renderer->vulkan.device, &info, &alloccbs, &(semaphore->vulkan.semaphore)));
	semaphore->vulkan.signaled = false;	// Set signal initial state.
}

void vk_remove_semaphore(renderer_t* renderer, semaphore_t* semaphore)
{
	TC_ASSERT(renderer && semaphore);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != semaphore->vulkan.semaphore);
	vkDestroySemaphore(renderer->vulkan.device, semaphore->vulkan.semaphore, &alloccbs);
}

void vk_add_queue(renderer_t* renderer, queuedesc_t* desc, queue_t* queue)
{
	TC_ASSERT(renderer && desc && queue);
	const uint32_t nodeidx = (renderer->gpumode == GPU_MODE_LINKED) ? desc->nodeidx : 0;
	VkQueueFamilyProperties props = { 0 };
	uint8_t famidx = UINT8_MAX;
	uint8_t queueidx = UINT8_MAX;
	util_find_queue_family_index(renderer, nodeidx, desc->type, &props, &famidx, &queueidx);
	++renderer->vulkan.usedqueues[nodeidx][props.queueFlags];
	memset(queue, 0, sizeof(queue_t));
	queue->vulkan.queuefamilyindex = famidx;
	queue->nodeidx = desc->nodeidx;
	queue->type = desc->type;
	queue->vulkan.queueindex = queueidx;
	queue->vulkan.gpumode = renderer->gpumode;
	queue->vulkan.timestampperiod = renderer->vulkan.activegpuprops->properties.limits.timestampPeriod;
	queue->vulkan.flags = props.queueFlags;
	queue->vulkan.submitlck = &renderer->nulldescriptors->submitlck;
	if (renderer->gpumode == GPU_MODE_UNLINKED)
		queue->nodeidx = renderer->unlinkedrendererindex;
	
	vkGetDeviceQueue(renderer->vulkan.device, queue->vulkan.queuefamilyindex, queue->vulkan.queuefamilyindex, &queue->vulkan.queue);
	TC_ASSERT(VK_NULL_HANDLE != queue->vulkan.queue);
}

void vk_remove_queue(renderer_t* renderer, queue_t* queue)
{
	TC_ASSERT(renderer && queue);
	const uint32_t nodeidx = renderer->gpumode == GPU_MODE_LINKED ? queue->nodeidx : 0;
	const VkQueueFlags flags = queue->vulkan.flags;
	--renderer->vulkan.usedqueues[nodeidx][flags];
}

void vk_add_cmdpool(renderer_t* renderer, const cmdpooldesc_t* desc, cmdpool_t* pool)
{
	TC_ASSERT(renderer);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(pool);
	pool->queue = desc->queue;
	VkCommandPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.queueFamilyIndex = desc->queue->vulkan.queuefamilyindex;
	if (desc->transient) info.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	CHECK_VKRESULT(vkCreateCommandPool(renderer->vulkan.device, &info, &alloccbs, &(pool->cmdpool)));
}

void vk_remove_cmdpool(renderer_t* renderer, cmdpool_t* pool)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pool);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != pool->cmdpool);
	vkDestroyCommandPool(renderer->vulkan.device, pool->cmdpool, &alloccbs);
}

void vk_add_cmds(renderer_t* renderer, const cmddesc_t* desc, uint32_t count, cmd_t* cmds)
{
	TC_ASSERT(renderer && desc);
	TC_ASSERT(count);
	TC_ASSERT(cmds);
	VkCommandBufferAllocateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = NULL;
	info.commandBufferCount = 1;
	for (uint32_t i = 0; i < count; ++i) {
		cmd_t* cmd = &cmds[i];
		cmd->renderer = renderer;
		cmd->queue = desc->pool->queue;
		cmd->vulkan.cmdpool = desc->pool;
		cmd->vulkan.type = desc->pool->queue->type;
		cmd->vulkan.nodeidx = desc->pool->queue->nodeidx;
		info.commandPool = desc->pool->cmdpool;
		info.level = desc->secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		CHECK_VKRESULT(vkAllocateCommandBuffers(renderer->vulkan.device, &info, &(cmd->vulkan.cmdbuf)));
	}
}

void vk_remove_cmds(renderer_t* renderer, uint32_t count, cmd_t* cmds)
{
	TC_ASSERT(renderer && cmds);
	for (uint32_t i = 0; i < count; ++i) {
		TC_ASSERT(VK_NULL_HANDLE != cmds[i].renderer->vulkan.device);
		TC_ASSERT(VK_NULL_HANDLE != cmds[i].vulkan.cmdbuf);
		vkFreeCommandBuffers(renderer->vulkan.device, cmds[i].vulkan.cmdpool->cmdpool, 1, &(cmds[i].vulkan.cmdbuf));
	}
}

void vk_toggle_vsync(renderer_t* renderer, swapchain_t* swapchain)
{
	queue_t queue = { 0 };
	queue.vulkan.queuefamilyindex = swapchain->vulkan.presentqueuefamilyindex;
	queue_t* queues[] = { &queue };

	swapchaindesc_t desc = *swapchain->vulkan.desc;
	desc.vsync = !desc.vsync;	//toggle vsync on or off
	desc.presentqueuecount = 1;
	desc.presentqueues = queues;
	remove_swapchain(renderer, swapchain);
	add_swapchain(renderer, &desc, swapchain);
}

void vk_add_swapchain(renderer_t* renderer, const swapchaindesc_t* desc, swapchain_t* swapchain)
{
	TC_ASSERT(renderer && desc && swapchain);
	TC_ASSERT(desc->imagecount <= MAX_SWAPCHAIN_IMAGES);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.instance);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.activegpu);
	VkSurfaceKHR surface;
	VkResult err = glfwCreateWindowSurface(renderer->vulkan.instance, desc->window, &alloccbs, &surface);
	if (err) {
		TRACE(LOG_ERROR, "Failed to create a window surface");
		TC_ASSERT(0);
	}
	if (0 == desc->imagecount) ((swapchaindesc_t*)desc)->imagecount = 2;
	VkSurfaceCapabilitiesKHR caps = { 0 };
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer->vulkan.activegpu, surface, &caps));
	if ((caps.maxImageCount > 0) && (desc->imagecount > caps.maxImageCount)) {
		TRACE(LOG_WARNING, "Changed requested SwapChain images {%u} to maximum allowed SwapChain images {%u}", desc->imagecount, caps.maxImageCount);
		((swapchaindesc_t*)desc)->imagecount = caps.maxImageCount;
	}
	if (desc->imagecount < caps.minImageCount) {
		TRACE(LOG_WARNING, "Changed requested SwapChain images {%u} to minimum required SwapChain images {%u}", desc->imagecount, caps.minImageCount);
		((swapchaindesc_t*)desc)->imagecount = caps.minImageCount;
	}
	// Surface format. Select a surface format, depending on whether HDR is available.
	VkSurfaceFormatKHR surface_format = { 0 };
	surface_format.format = VK_FORMAT_UNDEFINED;
	uint32_t surfacefmtcount = 0;
	VkSurfaceFormatKHR* formats = NULL;

	// Get surface formats count
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->vulkan.activegpu, surface, &surfacefmtcount, NULL));

	// Allocate and get surface formats
	formats = (VkSurfaceFormatKHR*)tc_calloc(surfacefmtcount, sizeof(*formats));
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->vulkan.activegpu, surface, &surfacefmtcount, formats));
	if ((1 == surfacefmtcount) && (VK_FORMAT_UNDEFINED == formats[0].format)) {
		surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
		surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}
	else {
		VkSurfaceFormatKHR hdrfmt = { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };
		VkFormat requested_format = (VkFormat)TinyImageFormat_ToVkFormat(desc->colorformat);
		VkColorSpaceKHR requested_color_space = requested_format == hdrfmt.format ? hdrfmt.colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		for (uint32_t i = 0; i < surfacefmtcount; i++) {
			if ((requested_format == formats[i].format) && (requested_color_space == formats[i].colorSpace)) {
				surface_format.format = requested_format;
				surface_format.colorSpace = requested_color_space;
				break;
			}
		}
		if (VK_FORMAT_UNDEFINED == surface_format.format) {		// Default to VK_FORMAT_B8G8R8A8_UNORM if requested format isn't found
			surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
			surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		}
	}
	tc_free(formats);

	// The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
	// This mode waits for the vertical blank ("v-sync")
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t swapChainImageCount = 0;
	VkPresentModeKHR* modes = NULL;
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->vulkan.activegpu, surface, &swapChainImageCount, NULL));
	modes = (VkPresentModeKHR*)alloca(swapChainImageCount * sizeof(*modes));		// Allocate and get present modes
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(renderer->vulkan.activegpu, surface, &swapChainImageCount, modes));
	const uint32_t preferredmodecount = 4;
	VkPresentModeKHR preferredModes[] = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_FIFO_KHR };
	uint32_t start = desc->vsync ? 2 : 0;
	for (uint32_t j = start; j < preferredmodecount; j++) {
		VkPresentModeKHR mode = preferredModes[j];
		uint32_t i = 0;
		for (; i < swapChainImageCount; ++i)
			if (modes[i] == mode)
				break;
		if (i < swapChainImageCount) {
			present_mode = mode;
			break;
		}
	}
	VkExtent2D extent = { 0 };
	extent.width = clamp(desc->width, caps.minImageExtent.width, caps.maxImageExtent.width);
	extent.height = clamp(desc->height, caps.minImageExtent.height, caps.maxImageExtent.height);

	VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
	uint32_t queue_family_index_count = 0;
	uint32_t famindices[2] = { desc->presentqueues[0]->vulkan.queuefamilyindex, 0 };
	uint32_t famidx = -1;
	uint32_t queuefampropscount = 0;				
	VkQueueFamilyProperties* queuefamprops = NULL;
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefampropscount, NULL);		// Get queue family properties
	queuefamprops = (VkQueueFamilyProperties*)alloca(queuefampropscount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->vulkan.activegpu, &queuefampropscount, queuefamprops);

	if (queuefampropscount) {			// Check if hardware provides dedicated present queue
		for (uint32_t i = 0; i < queuefampropscount; i++) {
			VkBool32 supports_present = VK_FALSE;
			VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(renderer->vulkan.activegpu, i, surface, &supports_present);
			if ((VK_SUCCESS == res) && (VK_TRUE == supports_present) && desc->presentqueues[0]->vulkan.queuefamilyindex != i) {
				famidx = i;
				break;
			}
		}
		if (famidx == UINT32_MAX) {		// If there is no dedicated present queue, just find the first available queue which supports present
			for (uint32_t i = 0; i < queuefampropscount; i++) {
				VkBool32 supports_present = VK_FALSE;
				VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(renderer->vulkan.activegpu, i, surface, &supports_present);
				if ((VK_SUCCESS == res) && (VK_TRUE == supports_present)) {
					famidx = i;
					break;
				}
				else TC_ASSERT(0 && "No present queue family available. Something goes wrong.");
			}
		}
	}
	VkQueue presentQueue;
	uint32_t finalPresentQueueFamilyIndex;
	if (famidx != UINT32_MAX && famindices[0] != famidx) {		// Find if gpu has a dedicated present queue
		famindices[0] = famidx;
		vkGetDeviceQueue(renderer->vulkan.device, famindices[0], 0, &presentQueue);
		queue_family_index_count = 1;
		finalPresentQueueFamilyIndex = famidx;
	}
	else {
		finalPresentQueueFamilyIndex = famindices[0];
		presentQueue = VK_NULL_HANDLE;
	}
	VkSurfaceTransformFlagBitsKHR pre_transform;
	if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else
		pre_transform = caps.currentTransform;

	VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] = {
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
	};
	VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR;
	for (int i = 0; i < ARRAYSIZE(compositeAlphaFlags); i++) {
		VkCompositeAlphaFlagBitsKHR flag = compositeAlphaFlags[i];
		if (caps.supportedCompositeAlpha & flag) {
			composite_alpha = flag;
			break;
		}
	}
	TC_ASSERT(composite_alpha != VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR);

	VkSwapchainKHR vkSwapchain;
	VkSwapchainCreateInfoKHR info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface;
	info.minImageCount = desc->imagecount;
	info.imageFormat = surface_format.format;
	info.imageColorSpace = surface_format.colorSpace;
	info.imageExtent = extent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.imageSharingMode = sharing_mode;
	info.queueFamilyIndexCount = queue_family_index_count;
	info.pQueueFamilyIndices = famindices;
	info.preTransform = pre_transform;
	info.compositeAlpha = composite_alpha;
	info.presentMode = present_mode;
	info.clipped = VK_TRUE;
	CHECK_VKRESULT(vkCreateSwapchainKHR(renderer->vulkan.device, &info, &alloccbs, &vkSwapchain));

	((swapchaindesc_t*)desc)->colorformat = TinyImageFormat_FromVkFormat((TinyImageFormat_VkFormat)surface_format.format);

	
	uint32_t numimages = 0;		// Create rendertargets from swapchain
	CHECK_VKRESULT(vkGetSwapchainImagesKHR(renderer->vulkan.device, vkSwapchain, &numimages, NULL));
	TC_ASSERT(numimages >= desc->imagecount);
	VkImage* images = (VkImage*)alloca(numimages * sizeof(VkImage));
	CHECK_VKRESULT(vkGetSwapchainImagesKHR(renderer->vulkan.device, vkSwapchain, &numimages, images));
	uint8_t* mem = (uint8_t*)tc_calloc(1, numimages * sizeof(rendertarget_t*) + sizeof(swapchaindesc_t));
	TC_ASSERT(mem);
	swapchain->rts = (rendertarget_t**)mem;
	swapchain->vulkan.desc = (swapchaindesc_t*)(swapchain->rts + numimages);

	rendertargetdesc_t rtdesc = { 0 };
	rtdesc.width = desc->width;
	rtdesc.height = desc->height;
	rtdesc.depth = 1;
	rtdesc.arraysize = 1;
	rtdesc.format = desc->colorformat;
	rtdesc.clearvalue = desc->colorclearvalue;
	rtdesc.samplecount = SAMPLE_COUNT_1;
	rtdesc.samplequality = 0;
	rtdesc.state = RESOURCE_STATE_PRESENT;
	rtdesc.nodeidx = renderer->unlinkedrendererindex;

	char buffer[32] = { 0 };
	for (uint32_t i = 0; i < numimages; ++i) {	// Populate the vk_image field and add the Vulkan texture objects
		sprintf(buffer, "Swapchain RT[%u]", i);
		rtdesc.name = buffer;
		rtdesc.nativehandle = (void*)images[i];
		add_rendertarget(renderer, &rtdesc, &swapchain->rts[i]);
	}
	*swapchain->vulkan.desc = *desc;
	swapchain->vsync = desc->vsync;
	swapchain->imagecount = numimages;
	swapchain->vulkan.surface = surface;
	swapchain->vulkan.presentqueuefamilyindex = finalPresentQueueFamilyIndex;
	swapchain->vulkan.presentqueue = presentQueue;
	swapchain->vulkan.swapchain = vkSwapchain;
}

void vk_removeSwapChain(renderer_t* renderer, swapchain_t* swapchain)
{
	TC_ASSERT(renderer && swapchain);
	for (uint32_t i = 0; i < swapchain->imagecount; i++)
		removeRenderTarget(renderer, swapchain->rts[i]);

	vkDestroySwapchainKHR(renderer->vulkan.device, swapchain->vulkan.swapchain, &alloccbs);
	vkDestroySurfaceKHR(renderer->vulkan.instance, swapchain->vulkan.surface, &alloccbs);
	tc_free(swapchain->rts);
}

void vk_add_buffer(renderer_t* renderer, const bufferdesc_t* desc, buffer_t* buffer)
{
	TC_ASSERT(renderer && desc);
	TC_ASSERT(desc->size > 0);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(renderer->gpumode != GPU_MODE_UNLINKED || desc->nodeindex == renderer->unlinkedrendererindex);
	memset(buffer, 0, sizeof(buffer_t));
	uint64_t size = desc->size;
	// Align the buffer size to multiples of the dynamic uniform buffer minimum size
	if (desc->descriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
		uint64_t minAlignment = renderer->activegpusettings->uniformbufalignment;
		allocationSize = round_up_64(allocationSize, minAlignment);
	}

	DECLARE_ZERO(VkBufferCreateInfo, info);
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.size = allocationSize;
	info.usage = util_to_vk_buffer_usage(desc->descriptors, desc->mFormat != TinyImageFormat_UNDEFINED);
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.famidxCount = 0;
	info.pQueueFamilyIndices = NULL;

	// Buffer can be used as dest in a transfer command (Uploading data to a storage buffer, Readback query data)
	if (desc->mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_ONLY || desc->mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU)
		info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	const bool linkedMultiGpu = (renderer->gpumode == GPU_MODE_LINKED && (desc->sharednodeidxs || desc->mNodeIndex));

	VmaAllocationCreateInfo vma_mem_reqs = { 0 };
	vma_mem_reqs.usage = (VmaMemoryUsage)desc->mMemoryUsage;
	if (desc->mFlags & BUFFER_CREATION_FLAG_OWN_MEMORY_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	if (desc->mFlags & BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	if (linkedMultiGpu)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
	if (desc->mFlags & BUFFER_CREATION_FLAG_HOST_VISIBLE)
		vma_mem_reqs.requiredFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	if (desc->mFlags & BUFFER_CREATION_FLAG_HOST_COHERENT)
		vma_mem_reqs.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

#if defined(ANDROID) || defined(NX64)
	// UMA for Android and NX64 devices
	if (vma_mem_reqs.usage != VMA_MEMORY_USAGE_GPU_TO_CPU)
	{
		vma_mem_reqs.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	}
#endif

	VmaAllocationInfo info = { 0 };
	CHECK_VKRESULT(vmaCreateBuffer(
		renderer->vulkan.pVmaAllocator, &info, &vma_mem_reqs, &pBuffer->vulkan.pVkBuffer, &pBuffer->vulkan.pVkAllocation,
		&info));

	pBuffer->pCpuMappedAddress = info.pMappedData;
	/************************************************************************/
	// Buffer to be used on multiple GPUs
	/************************************************************************/
	if (linkedMultiGpu)
	{
		VmaAllocationInfo allocInfo = { 0 };
		vmaGetAllocationInfo(renderer->vulkan.pVmaAllocator, pBuffer->vulkan.pVkAllocation, &allocInfo);
		/************************************************************************/
		// Set all the device indices to the index of the device where we will create the buffer
		/************************************************************************/
		uint32_t* pIndices = (uint32_t*)alloca(renderer->linkednodecount * sizeof(uint32_t));
		util_calculate_device_indices(renderer, desc->mNodeIndex, desc->sharednodeidxs, desc->msharednodeidxcount, pIndices);
		/************************************************************************/
		// #TODO : Move this to the Vulkan memory allocator
		/************************************************************************/
		VkBindBufferMemoryInfoKHR            bindInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR };
		VkBindBufferMemoryDeviceGroupInfoKHR bindDeviceGroup = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO_KHR };
		bindDeviceGroup.deviceIndexCount = renderer->linkednodecount;
		bindDeviceGroup.pDeviceIndices = pIndices;
		bindInfo.buffer = pBuffer->vulkan.pVkBuffer;
		bindInfo.memory = allocInfo.deviceMemory;
		bindInfo.memoryOffset = allocInfo.offset;
		bindInfo.pNext = &bindDeviceGroup;
		CHECK_VKRESULT(vkBindBufferMemory2KHR(renderer->vulkan.device, 1, &bindInfo));
		/************************************************************************/
		/************************************************************************/
	}
	/************************************************************************/
	// Set descriptor data
	/************************************************************************/
	if ((desc->descriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) || (desc->descriptors & DESCRIPTOR_TYPE_BUFFER) ||
		(desc->descriptors & DESCRIPTOR_TYPE_RW_BUFFER))
	{
		if ((desc->descriptors & DESCRIPTOR_TYPE_BUFFER) || (desc->descriptors & DESCRIPTOR_TYPE_RW_BUFFER))
		{
			pBuffer->vulkan.mOffset = desc->mStructStride * desc->mFirstElement;
		}
	}

	if (info.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
	{
		VkBufferViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		viewInfo.buffer = pBuffer->vulkan.pVkBuffer;
		viewInfo.flags = 0;
		viewInfo.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
		viewInfo.offset = desc->mFirstElement * desc->mStructStride;
		viewInfo.range = desc->mElementCount * desc->mStructStride;
		VkFormatProperties formatProps = { 0 };
		vkGetPhysicalDeviceFormatProperties(renderer->vulkan.activegpu, viewInfo.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
		{
			TRACE(LOG_WARNING, "Failed to create uniform texel buffer view for format %u", (uint32_t)desc->mFormat);
		}
		else
		{
			CHECK_VKRESULT(vkCreateBufferView(
				renderer->vulkan.device, &viewInfo, &alloccbs, &pBuffer->vulkan.pVkUniformTexelView));
		}
	}
	if (info.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
	{
		VkBufferViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		viewInfo.buffer = pBuffer->vulkan.pVkBuffer;
		viewInfo.flags = 0;
		viewInfo.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
		viewInfo.offset = desc->mFirstElement * desc->mStructStride;
		viewInfo.range = desc->mElementCount * desc->mStructStride;
		VkFormatProperties formatProps = { 0 };
		vkGetPhysicalDeviceFormatProperties(renderer->vulkan.activegpu, viewInfo.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
		{
			TRACE(LOG_WARNING, "Failed to create storage texel buffer view for format %u", (uint32_t)desc->mFormat);
		}
		else
		{
			CHECK_VKRESULT(vkCreateBufferView(
				renderer->vulkan.device, &viewInfo, &alloccbs, &pBuffer->vulkan.pVkStorageTexelView));
		}
	}

#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->pName)
	{
		setBufferName(renderer, pBuffer, desc->pName);
	}
#endif

	/************************************************************************/
	/************************************************************************/
	pBuffer->mSize = (uint32_t)desc->mSize;
	pBuffer->mMemoryUsage = desc->mMemoryUsage;
	pBuffer->mNodeIndex = desc->mNodeIndex;
	pBuffer->descriptors = desc->descriptors;

	*ppBuffer = pBuffer;
}

void vk_remove_buffer(renderer_t* renderer, buffer_t* pBuffer)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pBuffer);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != pBuffer->vulkan.pVkBuffer);

	if (pBuffer->vulkan.pVkUniformTexelView)
	{
		vkDestroyBufferView(renderer->vulkan.device, pBuffer->vulkan.pVkUniformTexelView, &alloccbs);
		pBuffer->vulkan.pVkUniformTexelView = VK_NULL_HANDLE;
	}
	if (pBuffer->vulkan.pVkStorageTexelView)
	{
		vkDestroyBufferView(renderer->vulkan.device, pBuffer->vulkan.pVkStorageTexelView, &alloccbs);
		pBuffer->vulkan.pVkStorageTexelView = VK_NULL_HANDLE;
	}

	vmaDestroyBuffer(renderer->vulkan.pVmaAllocator, pBuffer->vulkan.pVkBuffer, pBuffer->vulkan.pVkAllocation);

	tc_free(pBuffer);
}

void vk_add_texture(renderer_t* renderer, const texturedesc_t* desc, texture_t** ppTexture)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc && desc->width && desc->height && (desc->mDepth || desc->arraysize));
	TC_ASSERT(renderer->gpumode != GPU_MODE_UNLINKED || desc->mNodeIndex == renderer->mUnlinkedRendererIndex);
	if (desc->mSampleCount > SAMPLE_COUNT_1 && desc->mMipLevels > 1)
	{
		TRACE(LOG_ERROR, "Multi-Sampled textures cannot have mip maps");
		TC_ASSERT(false);
		return;
	}

	size_t totalSize = sizeof(Texture);
	totalSize += (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE ? (desc->mMipLevels * sizeof(VkImageView)) : 0);
	texture_t* pTexture = (texture_t*)tc_calloc_memalign(1, alignof(Texture), totalSize);
	TC_ASSERT(pTexture);

	if (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
		pTexture->vulkan.pVkUAVDescriptors = (VkImageView*)(pTexture + 1);

	if (desc->pNativeHandle && !(desc->mFlags & TEXTURE_CREATION_FLAG_IMPORT_BIT))
	{
		pTexture->mOwnsImage = false;
		pTexture->vulkan.pVkImage = (VkImage)desc->pNativeHandle;
	}
	else
	{
		pTexture->mOwnsImage = true;
	}

	VkImageUsageFlags additionalFlags = 0;
	if (desc->mStartState & RESOURCE_STATE_RENDER_TARGET)
		additionalFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	else if (desc->mStartState & RESOURCE_STATE_DEPTH_WRITE)
		additionalFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    uint arraySize = desc->arraysize;
#if defined(QUEST_VR)
    if (additionalFlags == 0 && // If not a render target
        !!(desc->mFlags & TEXTURE_CREATION_FLAG_VR_MULTIVIEW))
    {
        // Double the array size
        arraySize *= 2;
    }
#endif

	VkImageType image_type = VK_IMAGE_TYPE_MAX_ENUM;
	if (desc->mFlags & TEXTURE_CREATION_FLAG_FORCE_2D)
	{
		TC_ASSERT(desc->mDepth == 1);
		image_type = VK_IMAGE_TYPE_2D;
	}
	else if (desc->mFlags & TEXTURE_CREATION_FLAG_FORCE_3D)
	{
		image_type = VK_IMAGE_TYPE_3D;
	}
	else
	{
		if (desc->mDepth > 1)
			image_type = VK_IMAGE_TYPE_3D;
		else if (desc->height > 1)
			image_type = VK_IMAGE_TYPE_2D;
		else
			image_type = VK_IMAGE_TYPE_1D;
	}

	DescriptorType descriptors = desc->descriptors;
	bool           cubemapRequired = (DESCRIPTOR_TYPE_TEXTURE_CUBE == (descriptors & DESCRIPTOR_TYPE_TEXTURE_CUBE));
	bool           arrayRequired = false;

	const bool     isPlanarFormat = TinyImageFormat_IsPlanar(desc->mFormat);
	const uint32_t numOfPlanes = TinyImageFormat_NumOfPlanes(desc->mFormat);
	const bool     isSinglePlane = TinyImageFormat_IsSinglePlane(desc->mFormat);
	TC_ASSERT(
		((isSinglePlane && numOfPlanes == 1) || (!isSinglePlane && numOfPlanes > 1 && numOfPlanes <= MAX_PLANE_COUNT)) &&
		"Number of planes for multi-planar formats must be 2 or 3 and for single-planar formats it must be 1.");

	if (image_type == VK_IMAGE_TYPE_3D)
		arrayRequired = true;

	if (VK_NULL_HANDLE == pTexture->vulkan.pVkImage)
	{
		DECLARE_ZERO(VkImageCreateInfo, info);
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.imageType = image_type;
		info.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
		info.extent.width = desc->width;
		info.extent.height = desc->height;
		info.extent.depth = desc->mDepth;
		info.mipLevels = desc->mMipLevels;
		info.arrayLayers = arraySize;
		info.samples = util_to_vk_sample_count(desc->mSampleCount);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = util_to_vk_image_usage(descriptors);
		info.usage |= additionalFlags;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.famidxCount = 0;
		info.pQueueFamilyIndices = NULL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (cubemapRequired)
			info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		if (arrayRequired)
			info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;

		DECLARE_ZERO(VkFormatProperties, format_props);
		vkGetPhysicalDeviceFormatProperties(renderer->vulkan.activegpu, info.format, &format_props);
		if (isPlanarFormat)    // multi-planar formats must have each plane separately bound to memory, rather than having a single memory binding for the whole image
		{
			TC_ASSERT(format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_DISJOINT_BIT);
			info.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
		}

		if ((VK_IMAGE_USAGE_SAMPLED_BIT & info.usage) || (VK_IMAGE_USAGE_STORAGE_BIT & info.usage))
		{
			// Make it easy to copy to and from textures
			info.usage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		}

		TC_ASSERT(renderer->pCapBits->canShaderReadFrom[desc->mFormat] && "GPU shader can't' read from this format");

		// Verify that GPU supports this format
		VkFormatFeatureFlags format_features = util_vk_image_usage_to_format_features(info.usage);

		VkFormatFeatureFlags flags = format_props.optimalTilingFeatures & format_features;
		TC_ASSERT((0 != flags) && "Format is not supported for GPU local images (i.e. not host visible images)");

		const bool linkedMultiGpu = (renderer->gpumode == GPU_MODE_LINKED) && (desc->sharednodeidxs || desc->mNodeIndex);

		VmaAllocationCreateInfo mem_reqs = { 0 };
		if (desc->mFlags & TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT)
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		if (linkedMultiGpu)
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
		mem_reqs.usage = (VmaMemoryUsage)VMA_MEMORY_USAGE_GPU_ONLY;

		VkExternalMemoryImageCreateInfoKHR externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR, NULL };

#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VkImportMemoryWin32HandleInfoKHR importInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, NULL };
#endif
		VkExportMemoryAllocateInfoKHR exportMemoryInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR, NULL };

		if (renderer->vulkan.externalmemoryextension && desc->mFlags & TEXTURE_CREATION_FLAG_IMPORT_BIT)
		{
			info.pNext = &externalInfo;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
			struct ImportHandleInfo
			{
				void*                                 pHandle;
				VkExternalMemoryHandleTypeFlagBitsKHR mHandleType;
			};

			ImportHandleInfo* pHandleInfo = (ImportHandleInfo*)desc->pNativeHandle;
			importInfo.handle = pHandleInfo->pHandle;
			importInfo.handleType = pHandleInfo->mHandleType;

			externalInfo.handleTypes = pHandleInfo->mHandleType;

			mem_reqs.pUserData = &importInfo;
			// Allocate external (importable / exportable) memory as dedicated memory to avoid adding unnecessary complexity to the Vulkan Memory Allocator
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
#endif
		}
		else if (renderer->vulkan.externalmemoryextension && desc->mFlags & TEXTURE_CREATION_FLAG_EXPORT_BIT)
		{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
			exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#endif

			mem_reqs.pUserData = &exportMemoryInfo;
			// Allocate external (importable / exportable) memory as dedicated memory to avoid adding unnecessary complexity to the Vulkan Memory Allocator
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}


		// If lazy allocation is requested, check that the hardware supports it
		bool lazyAllocation = desc->mFlags & TEXTURE_CREATION_FLAG_ON_TILE;
		if (lazyAllocation)
		{
			uint32_t memoryTypeIndex = 0;
			VmaAllocationCreateInfo lazyMemReqs = mem_reqs;
			lazyMemReqs.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			VkResult result = vmaFindMemoryTypeIndex(renderer->vulkan.pVmaAllocator, UINT32_MAX, &lazyMemReqs, &memoryTypeIndex);
			if (VK_SUCCESS == result)
			{
				mem_reqs = lazyMemReqs;
				info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
				// The Vulkan spec states: If usage includes VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
				// then bits other than VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				// and VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT must not be set
				info.usage &= (VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
				pTexture->mLazilyAllocated = true;
			}
		}

		VmaAllocationInfo info = { 0 };
		if (isSinglePlane)
		{
			CHECK_VKRESULT(vmaCreateImage(
				renderer->vulkan.pVmaAllocator, &info, &mem_reqs, &pTexture->vulkan.pVkImage, &pTexture->vulkan.pVkAllocation,
				&info));
		}
		else    // Multi-planar formats
		{
			// Create info requires the mutable format flag set for multi planar images
			// Also pass the format list for mutable formats as per recommendation from the spec
			// Might help to keep DCC enabled if we ever use this as a output format
			// DCC gets disabled when we pass mutable format bit to the create info. Passing the format list helps the driver to enable it
			VkFormat                       planarFormat = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
			VkImageFormatListCreateInfoKHR formatList = { 0 };
			formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
			formatList.pNext = NULL;
			formatList.pViewFormats = &planarFormat;
			formatList.viewFormatCount = 1;

			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			info.pNext = &formatList;    //-V506

			// Create Image
			CHECK_VKRESULT(vkCreateImage(renderer->vulkan.device, &info, &alloccbs, &pTexture->vulkan.pVkImage));

			VkMemoryRequirements vkMemReq = { 0 };
			uint64_t             planesOffsets[MAX_PLANE_COUNT] = { 0 };
			util_get_planar_vk_image_memory_requirement(
				renderer->vulkan.device, pTexture->vulkan.pVkImage, numOfPlanes, &vkMemReq, planesOffsets);

			// Allocate image memory
			VkMemoryAllocateInfo mem_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			mem_info.allocationSize = vkMemReq.size;
			VkPhysicalDeviceMemoryProperties memProps = { 0 };
			vkGetPhysicalDeviceMemoryProperties(renderer->vulkan.activegpu, &memProps);
			mem_info.memoryTypeIndex = util_get_memory_type(vkMemReq.memoryTypeBits, memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			CHECK_VKRESULT(vkAllocateMemory(
				renderer->vulkan.device, &mem_info, &alloccbs, &pTexture->vulkan.deviceMemory));

			// Bind planes to their memories
			VkBindImageMemoryInfo      bindImagesMemoryInfo[MAX_PLANE_COUNT];
			VkBindImagePlaneMemoryInfo bindImagePlanesMemoryInfo[MAX_PLANE_COUNT];
			for (uint32_t i = 0; i < numOfPlanes; ++i)
			{
				VkBindImagePlaneMemoryInfo& bindImagePlaneMemoryInfo = bindImagePlanesMemoryInfo[i];
				bindImagePlaneMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
				bindImagePlaneMemoryInfo.pNext = NULL;
				bindImagePlaneMemoryInfo.planeAspect = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);

				VkBindImageMemoryInfo& bindImageMemoryInfo = bindImagesMemoryInfo[i];
				bindImageMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
				bindImageMemoryInfo.pNext = &bindImagePlaneMemoryInfo;
				bindImageMemoryInfo.image = pTexture->vulkan.pVkImage;
				bindImageMemoryInfo.memory = pTexture->vulkan.deviceMemory;
				bindImageMemoryInfo.memoryOffset = planesOffsets[i];
			}

			CHECK_VKRESULT(vkBindImageMemory2(renderer->vulkan.device, numOfPlanes, bindImagesMemoryInfo));
		}

		/************************************************************************/
		// Texture to be used on multiple GPUs
		/************************************************************************/
		if (linkedMultiGpu)
		{
			VmaAllocationInfo allocInfo = { 0 };
			vmaGetAllocationInfo(renderer->vulkan.pVmaAllocator, pTexture->vulkan.pVkAllocation, &allocInfo);
			/************************************************************************/
			// Set all the device indices to the index of the device where we will create the texture
			/************************************************************************/
			uint32_t* pIndices = (uint32_t*)alloca(renderer->linkednodecount * sizeof(uint32_t));
			util_calculate_device_indices(renderer, desc->mNodeIndex, desc->sharednodeidxs, desc->msharednodeidxcount, pIndices);
			/************************************************************************/
			// #TODO : Move this to the Vulkan memory allocator
			/************************************************************************/
			VkBindImageMemoryInfoKHR            bindInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR };
			VkBindImageMemoryDeviceGroupInfoKHR bindDeviceGroup = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO_KHR };
			bindDeviceGroup.deviceIndexCount = renderer->linkednodecount;
			bindDeviceGroup.pDeviceIndices = pIndices;
			bindInfo.image = pTexture->vulkan.pVkImage;
			bindInfo.memory = allocInfo.deviceMemory;
			bindInfo.memoryOffset = allocInfo.offset;
			bindInfo.pNext = &bindDeviceGroup;
			CHECK_VKRESULT(vkBindImageMemory2KHR(renderer->vulkan.device, 1, &bindInfo));
			/************************************************************************/
			/************************************************************************/
		}
	}
	/************************************************************************/
	// Create image view
	/************************************************************************/
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch (image_type)
	{
		case VK_IMAGE_TYPE_1D: view_type = arraySize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D; break;
		case VK_IMAGE_TYPE_2D:
			if (cubemapRequired)
				view_type = (arraySize > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
			else
				view_type = arraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			break;
		case VK_IMAGE_TYPE_3D:
			if (arraySize > 1)
			{
				TRACE(LOG_ERROR, "Cannot support 3D Texture Array in Vulkan");
				TC_ASSERT(false);
			}
			view_type = VK_IMAGE_VIEW_TYPE_3D;
			break;
		default: TC_ASSERT(false && "Image Format not supported!"); break;
	}

	TC_ASSERT(view_type != VK_IMAGE_VIEW_TYPE_MAX_ENUM && "Invalid Image View");

	VkImageViewCreateInfo srvDesc = { 0 };
	// SRV
	srvDesc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	srvDesc.pNext = NULL;
	srvDesc.flags = 0;
	srvDesc.image = pTexture->vulkan.pVkImage;
	srvDesc.viewType = view_type;
	srvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
	srvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	srvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	srvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	srvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	srvDesc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(srvDesc.format, false);
	srvDesc.subresourceRange.baseMipLevel = 0;
	srvDesc.subresourceRange.levelCount = desc->mMipLevels;
	srvDesc.subresourceRange.baseArrayLayer = 0;
	srvDesc.subresourceRange.layercount = arraySize;
	pTexture->mAspectMask = util_vk_determine_aspect_mask(srvDesc.format, true);

	if (desc->pVkSamplerYcbcrConversionInfo)
	{
		srvDesc.pNext = desc->pVkSamplerYcbcrConversionInfo;
	}

	if (descriptors & DESCRIPTOR_TYPE_TEXTURE)
	{
		CHECK_VKRESULT(
			vkCreateImageView(renderer->vulkan.device, &srvDesc, &alloccbs, &pTexture->vulkan.pVkSRVDescriptor));
	}

	// SRV stencil
	if ((TinyImageFormat_HasStencil(desc->mFormat)) && (descriptors & DESCRIPTOR_TYPE_TEXTURE))
	{
		srvDesc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		CHECK_VKRESULT(
			vkCreateImageView(renderer->vulkan.device, &srvDesc, &alloccbs, &pTexture->vulkan.pVkSRVStencilDescriptor));
	}

	// UAV
	if (descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
	{
		VkImageViewCreateInfo uavDesc = srvDesc;
		// #NOTE : We dont support imageCube, imageCubeArray for consistency with other APIs
		// All cubemaps will be used as image2DArray for Image Load / Store ops
		if (uavDesc.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || uavDesc.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
			uavDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		uavDesc.subresourceRange.levelCount = 1;
		for (uint32_t i = 0; i < desc->mMipLevels; ++i)
		{
			uavDesc.subresourceRange.baseMipLevel = i;
			CHECK_VKRESULT(vkCreateImageView(
				renderer->vulkan.device, &uavDesc, &alloccbs, &pTexture->vulkan.pVkUAVDescriptors[i]));
		}
	}
	/************************************************************************/
	/************************************************************************/
	pTexture->mNodeIndex = desc->mNodeIndex;
	pTexture->width = desc->width;
	pTexture->height = desc->height;
	pTexture->mDepth = desc->mDepth;
	pTexture->mMipLevels = desc->mMipLevels;
	pTexture->mUav = desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE;
	pTexture->arraysizeMinusOne = arraySize - 1;
	pTexture->mFormat = desc->mFormat;
	pTexture->mSampleCount = desc->mSampleCount;

#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->pName)
	{
		setTextureName(renderer, pTexture, desc->pName);
	}
#endif

	*ppTexture = pTexture;
}

void vk_remove_texture(renderer_t* renderer, texture_t* pTexture)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pTexture);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != pTexture->vulkan.pVkImage);

	if (pTexture->mOwnsImage)
	{
		const TinyImageFormat fmt = (TinyImageFormat)pTexture->mFormat;
		const bool            isSinglePlane = TinyImageFormat_IsSinglePlane(fmt);
		if (isSinglePlane)
		{
			vmaDestroyImage(renderer->vulkan.pVmaAllocator, pTexture->vulkan.pVkImage, pTexture->vulkan.pVkAllocation);
		}
		else
		{
			vkDestroyImage(renderer->vulkan.device, pTexture->vulkan.pVkImage, &alloccbs);
			vkFreeMemory(renderer->vulkan.device, pTexture->vulkan.deviceMemory, &alloccbs);
		}
	}

	if (VK_NULL_HANDLE != pTexture->vulkan.pVkSRVDescriptor)
		vkDestroyImageView(renderer->vulkan.device, pTexture->vulkan.pVkSRVDescriptor, &alloccbs);

	if (VK_NULL_HANDLE != pTexture->vulkan.pVkSRVStencilDescriptor)
		vkDestroyImageView(renderer->vulkan.device, pTexture->vulkan.pVkSRVStencilDescriptor, &alloccbs);

	if (pTexture->vulkan.pVkUAVDescriptors)
	{
		for (uint32_t i = 0; i < pTexture->mMipLevels; ++i)
		{
			vkDestroyImageView(renderer->vulkan.device, pTexture->vulkan.pVkUAVDescriptors[i], &alloccbs);
		}
	}

	if (pTexture->pSvt)
	{
		removeVirtualTexture(renderer, pTexture->pSvt);
	}

	tc_free(pTexture);
}

void vk_addRenderTarget(renderer_t* renderer, const RenderTargetDesc* desc, RenderTarget** ppRenderTarget)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(ppRenderTarget);
	TC_ASSERT(renderer->gpumode != GPU_MODE_UNLINKED || desc->mNodeIndex == renderer->mUnlinkedRendererIndex);

	bool const isDepth = TinyImageFormat_IsDepthOnly(desc->mFormat) || TinyImageFormat_IsDepthAndStencil(desc->mFormat);

	TC_ASSERT(!((isDepth) && (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)) && "Cannot use depth stencil as UAV");

	((RenderTargetDesc*)desc)->mMipLevels = max(1U, desc->mMipLevels);

    uint arraySize = desc->arraysize;
#if defined(QUEST_VR)
    if (desc->mFlags & TEXTURE_CREATION_FLAG_VR_MULTIVIEW)
    {
        TC_ASSERT(arraySize == 1 && desc->mDepth == 1);
        arraySize = 2; // TODO: Support non multiview rendering
    }
#endif

	uint32_t depthOrArraySize = arraySize * desc->mDepth;
	uint32_t numRTVs = desc->mMipLevels;
	if ((desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		numRTVs *= depthOrArraySize;
	size_t totalSize = sizeof(RenderTarget);
	totalSize += numRTVs * sizeof(VkImageView);
	RenderTarget* pRenderTarget = (RenderTarget*)tc_calloc_memalign(1, alignof(RenderTarget), totalSize);
	TC_ASSERT(pRenderTarget);

	pRenderTarget->vulkan.pVkSliceDescriptors = (VkImageView*)(pRenderTarget + 1);

	// Monotonically increasing thread safe id generation
	pRenderTarget->vulkan.mId = tfrg_atomic32_add_relaxed(&gRenderTargetIds, 1);

	texturedesc_t desc = { 0 };
	desc.arraysize = arraySize;
	desc.mClearValue = desc->mClearValue;
	desc.mDepth = desc->mDepth;
	desc.mFlags = desc->mFlags;
	desc.mFormat = desc->mFormat;
	desc.height = desc->height;
	desc.mMipLevels = desc->mMipLevels;
	desc.mSampleCount = desc->mSampleCount;
	desc.mSampleQuality = desc->mSampleQuality;
	desc.width = desc->width;
	desc.pNativeHandle = desc->pNativeHandle;
	desc.mNodeIndex = desc->mNodeIndex;
	desc.sharednodeidxs = desc->sharednodeidxs;
	desc.msharednodeidxcount = desc->msharednodeidxcount;

	if (!isDepth)
		desc.mStartState |= RESOURCE_STATE_RENDER_TARGET;
	else
		desc.mStartState |= RESOURCE_STATE_DEPTH_WRITE;

	// Set this by default to be able to sample the rendertarget in shader
	desc.descriptors = desc->descriptors;
	// Create SRV by default for a render target unless this is on tile texture where SRV is not supported
	if (!(desc->mFlags & TEXTURE_CREATION_FLAG_ON_TILE))
	{
		desc.descriptors |= DESCRIPTOR_TYPE_TEXTURE;
	}
	else
	{
		if ((desc.descriptors & DESCRIPTOR_TYPE_TEXTURE) || (desc.descriptors & DESCRIPTOR_TYPE_RW_TEXTURE))
		{
			TRACE(LOG_WARNING, "On tile textures do not support DESCRIPTOR_TYPE_TEXTURE or DESCRIPTOR_TYPE_RW_TEXTURE");
		}
		// On tile textures do not support SRV/UAV as there is no backing memory
		// You can only read these textures as input attachments inside same render pass
		desc.descriptors &= (DescriptorType)(~(DESCRIPTOR_TYPE_TEXTURE | DESCRIPTOR_TYPE_RW_TEXTURE));
	}

	if (isDepth)
	{
		// Make sure depth/stencil format is supported - fall back to VK_FORMAT_D16_UNORM if not
		VkFormat vk_depth_stencil_format = (VkFormat)TinyImageFormat_ToVkFormat(desc->mFormat);
		if (VK_FORMAT_UNDEFINED != vk_depth_stencil_format)
		{
			DECLARE_ZERO(VkImageFormatProperties, properties);
			VkResult vk_res = vkGetPhysicalDeviceImageFormatProperties(
				renderer->vulkan.activegpu, vk_depth_stencil_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &properties);
			// Fall back to something that's guaranteed to work
			if (VK_SUCCESS != vk_res)
			{
				desc.mFormat = TinyImageFormat_D16_UNORM;
				TRACE(LOG_WARNING, "Depth stencil format (%u) not supported. Falling back to D16 format", desc->mFormat);
			}
		}
	}

	desc.pName = desc->pName;

	add_texture(renderer, &desc, &pRenderTarget->pTexture);

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	if (desc->height > 1)
		viewType = depthOrArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	else
		viewType = depthOrArraySize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;

	VkImageViewCreateInfo rtvDesc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL };
	rtvDesc.flags = 0;
	rtvDesc.image = pRenderTarget->pTexture->vulkan.pVkImage;
	rtvDesc.viewType = viewType;
	rtvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(desc.mFormat);
	rtvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	rtvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	rtvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	rtvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	rtvDesc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(rtvDesc.format, true);
	rtvDesc.subresourceRange.baseMipLevel = 0;
	rtvDesc.subresourceRange.levelCount = 1;
	rtvDesc.subresourceRange.baseArrayLayer = 0;
	rtvDesc.subresourceRange.layercount = depthOrArraySize;

	CHECK_VKRESULT(vkCreateImageView(renderer->vulkan.device, &rtvDesc, &alloccbs, &pRenderTarget->vulkan.pVkDescriptor));

	for (uint32_t i = 0; i < desc->mMipLevels; ++i) {
		rtvDesc.subresourceRange.baseMipLevel = i;
		if ((desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
			(desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES)) {
			for (uint32_t j = 0; j < depthOrArraySize; ++j) {
				rtvDesc.subresourceRange.layercount = 1;
				rtvDesc.subresourceRange.baseArrayLayer = j;
				CHECK_VKRESULT(vkCreateImageView(
					renderer->vulkan.device, &rtvDesc, &alloccbs,
					&pRenderTarget->vulkan.pVkSliceDescriptors[i * depthOrArraySize + j]));
			}
		}
		else
		{
			CHECK_VKRESULT(vkCreateImageView(
				renderer->vulkan.device, &rtvDesc, &alloccbs, &pRenderTarget->vulkan.pVkSliceDescriptors[i]));
		}
	}
	pRenderTarget->width = desc->width;
	pRenderTarget->height = desc->height;
	pRenderTarget->arraysize = arraySize;
	pRenderTarget->mDepth = desc->mDepth;
	pRenderTarget->mMipLevels = desc->mMipLevels;
	pRenderTarget->mSampleCount = desc->mSampleCount;
	pRenderTarget->mSampleQuality = desc->mSampleQuality;
	pRenderTarget->mFormat = desc.mFormat;
	pRenderTarget->mClearValue = desc->mClearValue;
    pRenderTarget->mVRMultiview = (desc->mFlags & TEXTURE_CREATION_FLAG_VR_MULTIVIEW) != 0;
    pRenderTarget->mVRFoveatedRendering = (desc->mFlags & TEXTURE_CREATION_FLAG_VR_FOVEATED_RENDERING) != 0;

	util_initial_transition(renderer, pRenderTarget->pTexture, desc->mStartState);

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	if (desc->mFlags & TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT)
	{
		RenderTargetDesc resolveRTDesc = *desc;
		resolveRTDesc.mFlags &= ~(TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT | TEXTURE_CREATION_FLAG_ON_TILE);
		resolveRTDesc.mSampleCount = SAMPLE_COUNT_1;
		addRenderTarget(renderer, &resolveRTDesc, &pRenderTarget->pResolveAttachment);
	}
#endif

	*ppRenderTarget = pRenderTarget;
}

void vk_removeRenderTarget(renderer_t* renderer, RenderTarget* pRenderTarget)
{
	::remove_texture(renderer, pRenderTarget->pTexture);

	vkDestroyImageView(renderer->vulkan.device, pRenderTarget->vulkan.pVkDescriptor, &alloccbs);

	const uint32_t depthOrArraySize = pRenderTarget->arraysize * pRenderTarget->mDepth;
	if ((pRenderTarget->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(pRenderTarget->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
	{
		for (uint32_t i = 0; i < pRenderTarget->mMipLevels; ++i)
			for (uint32_t j = 0; j < depthOrArraySize; ++j)
				vkDestroyImageView(
					renderer->vulkan.device, pRenderTarget->vulkan.pVkSliceDescriptors[i * depthOrArraySize + j],
					&alloccbs);
	}
	else
	{
		for (uint32_t i = 0; i < pRenderTarget->mMipLevels; ++i)
			vkDestroyImageView(renderer->vulkan.device, pRenderTarget->vulkan.pVkSliceDescriptors[i], &alloccbs);
	}

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	if (pRenderTarget->pResolveAttachment)
	{
		removeRenderTarget(renderer, pRenderTarget->pResolveAttachment);
	}
#endif

	tc_free(pRenderTarget);
}

void vk_add_sampler(renderer_t* renderer, const samplerdesc_t* desc, sampler_t** ppSampler)
{
	TC_ASSERT(renderer);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(desc->mCompareFunc < MAX_COMPARE_MODES);
	TC_ASSERT(ppSampler);

	sampler_t* pSampler = (sampler_t*)tc_calloc_memalign(1, alignof(Sampler), sizeof(Sampler));
	TC_ASSERT(pSampler);

	//default sampler lod values
	//used if not overriden by mSetLodRange or not Linear mipmaps
	float minSamplerLod = 0;
	float maxSamplerLod = desc->mMipMapMode == MIPMAP_MODE_LINEAR ? VK_LOD_CLAMP_NONE : 0;
	//user provided lods
	if(desc->mSetLodRange)
	{
		minSamplerLod = desc->mMinLod;
		maxSamplerLod = desc->mMaxLod;
	}

	DECLARE_ZERO(VkSamplerCreateInfo, info);
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.magFilter = util_to_vk_filter(desc->mMagFilter);
	info.minFilter = util_to_vk_filter(desc->mMinFilter);
	info.mipmapMode = util_to_vk_mip_map_mode(desc->mMipMapMode);
	info.addressModeU = util_to_vk_address_mode(desc->mAddressU);
	info.addressModeV = util_to_vk_address_mode(desc->mAddressV);
	info.addressModeW = util_to_vk_address_mode(desc->mAddressW);
	info.mipLodBias = desc->mMipLodBias;
	info.anisotropyEnable = renderer->activegpusettings->mSamplerAnisotropySupported && (desc->mMaxAnisotropy > 0.0f) ? VK_TRUE : VK_FALSE;
	info.maxAnisotropy = desc->mMaxAnisotropy;
	info.compareEnable = (comparefuncmap[desc->mCompareFunc] != VK_COMPARE_OP_NEVER) ? VK_TRUE : VK_FALSE;
	info.compareOp = comparefuncmap[desc->mCompareFunc];
	info.minLod = minSamplerLod;
	info.maxLod = maxSamplerLod;
	info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	info.unnormalizedCoordinates = VK_FALSE;

	if (TinyImageFormat_IsPlanar(desc->mSamplerConversionDesc.mFormat))
	{
		auto&    conversionDesc = desc->mSamplerConversionDesc;
		VkFormat format = (VkFormat)TinyImageFormat_ToVkFormat(conversionDesc.mFormat);

		// Check format props
		{
			TC_ASSERT((uint32_t)renderer->vulkan.YCbCrextension);

			DECLARE_ZERO(VkFormatProperties, format_props);
			vkGetPhysicalDeviceFormatProperties(renderer->vulkan.activegpu, format, &format_props);
			if (conversionDesc.mChromaOffsetX == SAMPLE_LOCATION_MIDPOINT)
			{
				TC_ASSERT(format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT);
			}
			else if (conversionDesc.mChromaOffsetX == SAMPLE_LOCATION_COSITED)
			{
				TC_ASSERT(format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT);
			}
		}

		DECLARE_ZERO(VkSamplerYcbcrConversionCreateInfo, conversion_info);
		conversion_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
		conversion_info.pNext = NULL;
		conversion_info.format = format;
		conversion_info.ycbcrModel = (VkSamplerYcbcrModelConversion)conversionDesc.mModel;
		conversion_info.ycbcrRange = (VkSamplerYcbcrRange)conversionDesc.mRange;
		conversion_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
									   VK_COMPONENT_SWIZZLE_IDENTITY };
		conversion_info.xChromaOffset = (VkChromaLocation)conversionDesc.mChromaOffsetX;
		conversion_info.yChromaOffset = (VkChromaLocation)conversionDesc.mChromaOffsetY;
		conversion_info.chromaFilter = util_to_vk_filter(conversionDesc.mChromaFilter);
		conversion_info.forceExplicitReconstruction = conversionDesc.mForceExplicitReconstruction ? VK_TRUE : VK_FALSE;
		CHECK_VKRESULT(vkCreateSamplerYcbcrConversion(
			renderer->vulkan.device, &conversion_info, &alloccbs, &pSampler->vulkan.pVkSamplerYcbcrConversion));

		pSampler->vulkan.mVkSamplerYcbcrConversionInfo = { 0 };
		pSampler->vulkan.mVkSamplerYcbcrConversionInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
		pSampler->vulkan.mVkSamplerYcbcrConversionInfo.pNext = NULL;
		pSampler->vulkan.mVkSamplerYcbcrConversionInfo.conversion = pSampler->vulkan.pVkSamplerYcbcrConversion;
		info.pNext = &pSampler->vulkan.mVkSamplerYcbcrConversionInfo;
	}

	CHECK_VKRESULT(vkCreateSampler(renderer->vulkan.device, &info, &alloccbs, &(pSampler->vulkan.pVkSampler)));

	*ppSampler = pSampler;
}

void vk_remove_sampler(renderer_t* renderer, sampler_t* pSampler)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pSampler);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != pSampler->vulkan.pVkSampler);

	vkDestroySampler(renderer->vulkan.device, pSampler->vulkan.pVkSampler, &alloccbs);

	if (NULL != pSampler->vulkan.pVkSamplerYcbcrConversion)
	{
		vkDestroySamplerYcbcrConversion(renderer->vulkan.device, pSampler->vulkan.pVkSamplerYcbcrConversion, &alloccbs);
	}

	tc_free(pSampler);
}

/************************************************************************/
// Buffer Functions
/************************************************************************/
void vk_mapBuffer(renderer_t* renderer, buffer_t* pBuffer, ReadRange* pRange)
{
	TC_ASSERT(pBuffer->mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to map non-cpu accessible resource");

	CHECK_VKRESULT(vmaMapMemory(renderer->vulkan.pVmaAllocator, pBuffer->vulkan.pVkAllocation, &pBuffer->pCpuMappedAddress));

	if (pRange)
	{
		pBuffer->pCpuMappedAddress = ((uint8_t*)pBuffer->pCpuMappedAddress + pRange->mOffset);
	}
}

void vk_unmapBuffer(renderer_t* renderer, buffer_t* pBuffer)
{
	TC_ASSERT(pBuffer->mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to unmap non-cpu accessible resource");

	vmaUnmapMemory(renderer->vulkan.pVmaAllocator, pBuffer->vulkan.pVkAllocation);
	pBuffer->pCpuMappedAddress = NULL;
}
/************************************************************************/
// Descriptor Set Functions
/************************************************************************/
void vk_addDescriptorSet(renderer_t* renderer, const DescriptorSetDesc* desc, DescriptorSet** pdescriptorSet)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(pdescriptorSet);

	const RootSignature*            pRootSignature = desc->pRootSignature;
	const DescriptorUpdateFrequency updateFreq = desc->mUpdateFrequency;
	const uint32_t                  nodeidx = renderer->gpumode == GPU_MODE_LINKED ? desc->mNodeIndex : 0;
	const uint32_t                  dynamicOffsetCount = pRootSignature->vulkan.mVkDynamicDescriptorCounts[updateFreq];

	uint32_t totalSize = sizeof(DescriptorSet);

	if (VK_NULL_HANDLE != pRootSignature->vulkan.mVkDescriptorSetLayouts[updateFreq])
	{
		totalSize += desc->mMaxSets * sizeof(VkDescriptorSet);
	}

	totalSize += desc->mMaxSets * dynamicOffsetCount * sizeof(DynamicUniformData);

	DescriptorSet* descriptorSet = (DescriptorSet*)tc_calloc_memalign(1, alignof(DescriptorSet), totalSize);

	descriptorSet->vulkan.pRootSignature = pRootSignature;
	descriptorSet->vulkan.mUpdateFrequency = updateFreq;
	descriptorSet->vulkan.mDynamicOffsetCount = dynamicOffsetCount;
	descriptorSet->vulkan.mNodeIndex = nodeidx;
	descriptorSet->vulkan.mMaxSets = desc->mMaxSets;

	uint8_t* pMem = (uint8_t*)(descriptorSet + 1);
	descriptorSet->vulkan.pHandles = (VkDescriptorSet*)pMem;
	pMem += desc->mMaxSets * sizeof(VkDescriptorSet);

	if (VK_NULL_HANDLE != pRootSignature->vulkan.mVkDescriptorSetLayouts[updateFreq])
	{
		VkDescriptorSetLayout* pLayouts = (VkDescriptorSetLayout*)alloca(desc->mMaxSets * sizeof(VkDescriptorSetLayout));
		VkDescriptorSet**      pHandles = (VkDescriptorSet**)alloca(desc->mMaxSets * sizeof(VkDescriptorSet*));

		for (uint32_t i = 0; i < desc->mMaxSets; ++i)
		{
			pLayouts[i] = pRootSignature->vulkan.mVkDescriptorSetLayouts[updateFreq];
			pHandles[i] = &descriptorSet->vulkan.pHandles[i];
		}

		VkDescriptorPoolSize poolSizes[MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT] = { 0 };
		for (uint32_t i = 0; i < pRootSignature->vulkan.mPoolSizeCount[updateFreq]; ++i)
		{
			poolSizes[i] = pRootSignature->vulkan.mPoolSizes[updateFreq][i];
			poolSizes[i].descriptorCount *= desc->mMaxSets;
		}
		add_descriptor_pool(renderer, desc->mMaxSets, 0,
			poolSizes, pRootSignature->vulkan.mPoolSizeCount[updateFreq],
			&descriptorSet->vulkan.descriptorPool);
		consume_descriptor_sets(renderer->vulkan.device, descriptorSet->vulkan.descriptorPool, pLayouts, desc->mMaxSets, pHandles);

		for (uint32_t descIndex = 0; descIndex < pRootSignature->mDescriptorCount; ++descIndex)
		{
			const DescriptorInfo* descInfo = &pRootSignature->descriptors[descIndex];
			if (descInfo->mUpdateFrequency != updateFreq || descInfo->mRootDescriptor || descInfo->mStaticSampler)
			{
				continue;
			}

			DescriptorType type = (DescriptorType)descInfo->mType;

			VkWriteDescriptorSet writeSet = { 0 };
			writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSet.pNext = NULL;
			writeSet.descriptorCount = 1;
			writeSet.descriptorType = (VkDescriptorType)descInfo->vulkan.mVkType;
			writeSet.dstArrayElement = 0;
			writeSet.dstBinding = descInfo->vulkan.mReg;

			for (uint32_t index = 0; index < desc->mMaxSets; ++index)
			{
				writeSet.dstSet = descriptorSet->vulkan.pHandles[index];

				switch (type)
				{
				case DESCRIPTOR_TYPE_SAMPLER:
				{
					VkDescriptorImageInfo updateData = { renderer->nulldescriptors->defaultsampler->vulkan.pVkSampler, VK_NULL_HANDLE };
					writeSet.pImageInfo = &updateData;
					for (uint32_t arr = 0; arr < descInfo->mSize; ++arr)
					{
						writeSet.dstArrayElement = arr;
						vkUpdateDescriptorSets(renderer->vulkan.device, 1, &writeSet, 0, NULL);
					}
					writeSet.pImageInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXTURE:
				case DESCRIPTOR_TYPE_RW_TEXTURE:
				{
					VkImageView srcView = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ?
						renderer->nulldescriptors->defaulttexUAV[nodeidx][descInfo->mDim]->vulkan.pVkUAVDescriptors[0] :
						renderer->nulldescriptors->defaulttexSRV[nodeidx][descInfo->mDim]->vulkan.pVkSRVDescriptor;
					VkImageLayout layout = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					VkDescriptorImageInfo updateData = { VK_NULL_HANDLE, srcView, layout };
					writeSet.pImageInfo = &updateData;
					for (uint32_t arr = 0; arr < descInfo->mSize; ++arr)
					{
						writeSet.dstArrayElement = arr;
						vkUpdateDescriptorSets(renderer->vulkan.device, 1, &writeSet, 0, NULL);
					}
					writeSet.pImageInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_BUFFER:
				case DESCRIPTOR_TYPE_BUFFER_RAW:
				case DESCRIPTOR_TYPE_RW_BUFFER:
				case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
				case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				{
					VkDescriptorBufferInfo updateData = { renderer->nulldescriptors->defaultbufSRV[nodeidx]->vulkan.pVkBuffer, 0, VK_WHOLE_SIZE };
					writeSet.pBufferInfo = &updateData;
					for (uint32_t arr = 0; arr < descInfo->mSize; ++arr)
					{
						writeSet.dstArrayElement = arr;
						vkUpdateDescriptorSets(renderer->vulkan.device, 1, &writeSet, 0, NULL);
					}
					writeSet.pBufferInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXEL_BUFFER:
				case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER:
				{
					VkBufferView updateData = (type == DESCRIPTOR_TYPE_RW_TEXEL_BUFFER) ?
						renderer->nulldescriptors->defaultbufUAV[nodeidx]->vulkan.pVkStorageTexelView :
						renderer->nulldescriptors->defaultbufSRV[nodeidx]->vulkan.pVkUniformTexelView;
					writeSet.pTexelBufferView = &updateData;
					for (uint32_t arr = 0; arr < descInfo->mSize; ++arr)
					{
						writeSet.dstArrayElement = arr;
						vkUpdateDescriptorSets(renderer->vulkan.device, 1, &writeSet, 0, NULL);
					}
					writeSet.pTexelBufferView = NULL;
					break;
				}
				default:
					break;
				}
			}
		}
	}
	else
	{
		TRACE(LOG_ERROR, "NULL Descriptor Set Layout for update frequency %u. Cannot allocate descriptor set", (uint32_t)updateFreq);
		TC_ASSERT(false && "NULL Descriptor Set Layout for update frequency. Cannot allocate descriptor set");
	}

	if (descriptorSet->vulkan.mDynamicOffsetCount)
	{
		descriptorSet->vulkan.pDynamicUniformData = (DynamicUniformData*)pMem;
		pMem += descriptorSet->vulkan.mMaxSets * descriptorSet->vulkan.mDynamicOffsetCount * sizeof(DynamicUniformData);
	}

	*pdescriptorSet = descriptorSet;
}

void vk_removeDescriptorSet(renderer_t* renderer, DescriptorSet* descriptorSet)
{
	TC_ASSERT(renderer);
	TC_ASSERT(descriptorSet);

	vkDestroyDescriptorPool(renderer->vulkan.device, descriptorSet->vulkan.descriptorPool, &alloccbs);
	tc_free(descriptorSet);
}

#if defined(ENABLE_GRAPHICS_DEBUG)
#define VALIDATE_DESCRIPTOR(descriptor, ...)                       \
	if (!(descriptor))                                             \
	{                                                              \
		char messageBuf[256];                                      \
		sprintf(messageBuf, __VA_ARGS__);                          \
		TRACE(LOG_ERROR, "%s", messageBuf);                  \
		_FailedAssert(__FILE__, __LINE__, __FUNCTION__);           \
		continue;                                                  \
	}
#else
#define VALIDATE_DESCRIPTOR(descriptor, ...)
#endif

void vk_updateDescriptorSet(
	renderer_t* renderer, uint32_t index, DescriptorSet* descriptorSet, uint32_t count, const DescriptorData* pParams)
{
	TC_ASSERT(renderer);
	TC_ASSERT(descriptorSet);
	TC_ASSERT(descriptorSet->vulkan.pHandles);
	TC_ASSERT(index < descriptorSet->vulkan.mMaxSets);

	const uint32_t maxWriteSets = 256;
	// #NOTE - Should be good enough to avoid splitting most of the update calls other than edge cases having huge update sizes
	const uint32_t maxDescriptorInfoByteSize = sizeof(VkDescriptorImageInfo) * 1024;
	const RootSignature*      pRootSignature = descriptorSet->vulkan.pRootSignature;
	VkWriteDescriptorSet      writeSetArray[maxWriteSets] = { 0 };
	uint8_t                   descriptorUpdateDataStart[maxDescriptorInfoByteSize] = { 0 };
	const uint8_t*            descriptorUpdateDataEnd = &descriptorUpdateDataStart[maxDescriptorInfoByteSize - 1];
	uint32_t                  writeSetCount = 0;

	uint8_t* descriptorUpdateData = descriptorUpdateDataStart;

#define FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(type, pInfo, count)                                         \
	if (descriptorUpdateData + sizeof(type) >= descriptorUpdateDataEnd)                               \
	{                                                                                                 \
		writeSet->descriptorCount = arr - lastArrayIndexStart;                                        \
		vkUpdateDescriptorSets(renderer->vulkan.device, writeSetCount, writeSetArray, 0, NULL);  \
		/* All previous write sets flushed. Start from zero */                                        \
		writeSetCount = 1;                                                                            \
		writeSetArray[0] = *writeSet;                                                                 \
		writeSet = &writeSetArray[0];                                                                 \
		lastArrayIndexStart = arr;                                                                    \
		writeSet->dstArrayElement += writeSet->descriptorCount;                                       \
		/* Set descriptor count to the remaining count */                                             \
		writeSet->descriptorCount = count - writeSet->dstArrayElement;                                \
		descriptorUpdateData = descriptorUpdateDataStart;                                             \
		writeSet->pInfo = (type*)descriptorUpdateData;                                                \
	}                                                                                                 \
	type* currUpdateData = (type*)descriptorUpdateData;                                               \
	descriptorUpdateData += sizeof(type);

	for (uint32_t i = 0; i < count; ++i)
	{
		const DescriptorData* pParam = pParams + i;
		uint32_t              paramIndex = pParam->mBindByIndex ? pParam->mIndex : UINT32_MAX;

		VALIDATE_DESCRIPTOR(pParam->pName || (paramIndex != UINT32_MAX), "DescriptorData has NULL name and invalid index");

		const DescriptorInfo* desc =
			(paramIndex != UINT32_MAX) ? (pRootSignature->descriptors + paramIndex) : get_descriptor(pRootSignature, pParam->pName);
		if (paramIndex != UINT32_MAX)
		{
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param index (%u)", paramIndex);
		}
		else
		{
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param name (%s)", pParam->pName);
		}

		const DescriptorType type = (DescriptorType)desc->mType;    //-V522
		const uint32_t       arrayStart = pParam->mArrayOffset;
		const uint32_t       arrayCount = max(1U, pParam->mCount);

		// #NOTE - Flush the update if we go above the max write set limit
		if (writeSetCount >= maxWriteSets)
		{
			vkUpdateDescriptorSets(renderer->vulkan.device, writeSetCount, writeSetArray, 0, NULL);
			writeSetCount = 0;
			descriptorUpdateData = descriptorUpdateDataStart;
		}

		VkWriteDescriptorSet* writeSet = &writeSetArray[writeSetCount++];
		writeSet->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeSet->pNext = NULL;
		writeSet->descriptorCount = arrayCount;
		writeSet->descriptorType = (VkDescriptorType)desc->vulkan.mVkType;
		writeSet->dstArrayElement = arrayStart;
		writeSet->dstBinding = desc->vulkan.mReg;
		writeSet->dstSet = descriptorSet->vulkan.pHandles[index];

		VALIDATE_DESCRIPTOR(
			desc->mUpdateFrequency == descriptorSet->vulkan.mUpdateFrequency, "Descriptor (%s) - Mismatching update frequency and set index", desc->pName);

		uint32_t lastArrayIndexStart = 0;

		switch (type)
		{
			case DESCRIPTOR_TYPE_SAMPLER:
			{
				// Index is invalid when descriptor is a static sampler
				VALIDATE_DESCRIPTOR(
					!desc->mStaticSampler,
					"Trying to update a static sampler (%s). All static samplers must be set in addRootSignature and cannot be updated "
					"later",
					desc->pName);

				VALIDATE_DESCRIPTOR(pParam->ppSamplers, "NULL Sampler (%s)", desc->pName);

				writeSet->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppSamplers[arr], "NULL Sampler (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount) //-V1032
					*currUpdateData = { pParam->ppSamplers[arr]->vulkan.pVkSampler, VK_NULL_HANDLE };
				}
				break;
			}
			case DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", desc->pName);

#if defined(ENABLE_GRAPHICS_DEBUG)
				DescriptorIndexMap* pNode = shgetp_null(pRootSignature->descriptorNameToIndexMap, desc->pName);
				if (!pNode)
				{
					TRACE(LOG_ERROR, "No Static Sampler called (%s)", desc->pName);
					TC_ASSERT(false);
				}
#endif

				writeSet->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
					*currUpdateData =
					{
						NULL,                                                 // Sampler
						pParam->ppTextures[arr]->vulkan.pVkSRVDescriptor,    // Image View
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL              // Image Layout
					};
				}
				break;
			}
			case DESCRIPTOR_TYPE_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", desc->pName);

				writeSet->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				if (!pParam->mBindStencilResource)
				{
					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", desc->pName, arr);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                       // Sampler
							pParam->ppTextures[arr]->vulkan.pVkSRVDescriptor,    // Image View
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL              // Image Layout
						};
					}
				}
				else
				{
					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", desc->pName, arr);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                              // Sampler
							pParam->ppTextures[arr]->vulkan.pVkSRVStencilDescriptor,    // Image View
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL                     // Image Layout
						};
					}
				}
				break;
			}
			case DESCRIPTOR_TYPE_RW_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL RW Texture (%s)", desc->pName);

				writeSet->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				if (pParam->mBindMipChain)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[0], "NULL RW Texture (%s)", desc->pName);
					VALIDATE_DESCRIPTOR((!arrayStart), "Descriptor (%s) - mBindMipChain supports only updating the whole mip-chain. No partial updates supported", pParam->pName);
					const uint32_t mipCount = pParam->ppTextures[0]->mMipLevels;
					writeSet->descriptorCount = mipCount;

					for (uint32_t arr = 0; arr < mipCount; ++arr)
					{
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, mipCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                           // Sampler
							pParam->ppTextures[0]->vulkan.pVkUAVDescriptors[arr],    // Image View
							VK_IMAGE_LAYOUT_GENERAL                                   // Image Layout
						};
					}
				}
				else
				{
					const uint32_t mipSlice = pParam->mUAVMipSlice;

					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL RW Texture (%s [%u] )", desc->pName, arr);
						VALIDATE_DESCRIPTOR(
							mipSlice < pParam->ppTextures[arr]->mMipLevels,
							"Descriptor : (%s [%u] ) Mip Slice (%u) exceeds mip levels (%u)", desc->pName, arr, mipSlice,
							pParam->ppTextures[arr]->mMipLevels);

						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                                  // Sampler
							pParam->ppTextures[arr]->vulkan.pVkUAVDescriptors[mipSlice],    // Image View
							VK_IMAGE_LAYOUT_GENERAL                                          // Image Layout
						};
					}
				}
				break;
			}
			case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			{
				if (desc->mRootDescriptor)
				{
					VALIDATE_DESCRIPTOR(
						false,
						"Descriptor (%s) - Trying to update a root cbv through updateDescriptorSet. All root cbvs must be updated through cmdBindDescriptorSetWithRootCbvs",
						desc->pName);

					break;
				}
				case DESCRIPTOR_TYPE_BUFFER:
				case DESCRIPTOR_TYPE_BUFFER_RAW:
				case DESCRIPTOR_TYPE_RW_BUFFER:
				case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
				{
					VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Buffer (%s)", desc->pName);

					writeSet->pBufferInfo = (VkDescriptorBufferInfo*)descriptorUpdateData;

					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Buffer (%s [%u] )", desc->pName, arr);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorBufferInfo, pBufferInfo, arrayCount)
						*currUpdateData =
						{
							pParam->ppBuffers[arr]->vulkan.pVkBuffer,
							pParam->ppBuffers[arr]->vulkan.mOffset,
							VK_WHOLE_SIZE
						};

						if (pParam->pRanges)
						{
							DescriptorDataRange range = pParam->pRanges[arr];
#if defined(ENABLE_GRAPHICS_DEBUG)
							uint32_t maxRange = DESCRIPTOR_TYPE_UNIFORM_BUFFER == type ?
								renderer->vulkan.activegpuprops->properties.limits.maxUniformBufferRange :
								renderer->vulkan.activegpuprops->properties.limits.maxStorageBufferRange;
#endif

							VALIDATE_DESCRIPTOR(range.mSize, "Descriptor (%s) - pRanges[%u].mSize is zero", desc->pName, arr);
							VALIDATE_DESCRIPTOR(
								range.mSize <= maxRange,
								"Descriptor (%s) - pRanges[%u].mSize is %ull which exceeds max size %u", desc->pName, arr, range.mSize,
								maxRange);

							currUpdateData->offset = range.mOffset;
							currUpdateData->range = range.mSize;
						}
					}
				}
				break;
			}
			case DESCRIPTOR_TYPE_TEXEL_BUFFER:
			case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER:
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Texel Buffer (%s)", desc->pName);

				writeSet->pTexelBufferView = (VkBufferView*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Texel Buffer (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkBufferView, pTexelBufferView, arrayCount)
					*currUpdateData = DESCRIPTOR_TYPE_TEXEL_BUFFER == type ?
						pParam->ppBuffers[arr]->vulkan.pVkUniformTexelView :
						pParam->ppBuffers[arr]->vulkan.pVkStorageTexelView;
				}

				break;
			}
#ifdef VK_RAYTRACING_AVAILABLE
			case DESCRIPTOR_TYPE_RAY_TRACING:
			{
				VALIDATE_DESCRIPTOR(pParam->ppAccelerationStructures, "NULL Acceleration Structure (%s)", desc->pName);

				VkWriteDescriptorSetAccelerationStructureKHR writeSetKHR = { 0 };
				VkAccelerationStructureKHR currUpdateData = { 0 };
				writeSet->pNext = &writeSetKHR;
				writeSet->descriptorCount = 1;
				writeSetKHR.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
				writeSetKHR.pNext = NULL;
				writeSetKHR.accelerationStructureCount = 1;
				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					vk_FillRaytracingDescriptorData(pParam->ppAccelerationStructures[arr], &currUpdateData);
					writeSetKHR.pAccelerationStructures = &currUpdateData;
					vkUpdateDescriptorSets(renderer->vulkan.device, 1, writeSet, 0, NULL);
					++writeSet->dstArrayElement;
				}

				// Update done - Dont need this write set anymore. Return it to the array
				writeSet->pNext = NULL;
				--writeSetCount;

				break;
			}
#endif
			default: break;
		}
	}

	vkUpdateDescriptorSets(renderer->vulkan.device, writeSetCount, writeSetArray, 0, NULL);
}

static const uint32_t VK_MAX_ROOT_DESCRIPTORS = 32;

void vk_cmdBindDescriptorSet(cmd_t* pCmd, uint32_t index, DescriptorSet* descriptorSet)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(descriptorSet);
	TC_ASSERT(descriptorSet->vulkan.pHandles);
	TC_ASSERT(index < descriptorSet->vulkan.mMaxSets);

	const RootSignature* pRootSignature = descriptorSet->vulkan.pRootSignature;

	if (pCmd->vulkan.pBoundPipelineLayout != pRootSignature->vulkan.pipelineLayout)
	{
		pCmd->vulkan.pBoundPipelineLayout = pRootSignature->vulkan.pipelineLayout;

		// Vulkan requires to bind all descriptor sets upto the highest set number even if they are empty
		// Example: If shader uses only set 2, we still have to bind empty sets for set=0 and set=1
		for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
		{
			if (pRootSignature->vulkan.pEmptyDescriptorSet[setIndex] != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					pCmd->vulkan.pVkCmdBuf, gPipelineBindPoint[pRootSignature->mPipelineType], pRootSignature->vulkan.pipelineLayout,
					setIndex, 1, &pRootSignature->vulkan.pEmptyDescriptorSet[setIndex], 0, NULL);
			}
		}
	}

	static uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };

	vkCmdBindDescriptorSets(
		pCmd->vulkan.pVkCmdBuf, gPipelineBindPoint[pRootSignature->mPipelineType], pRootSignature->vulkan.pipelineLayout,
		descriptorSet->vulkan.mUpdateFrequency, 1, &descriptorSet->vulkan.pHandles[index],
		descriptorSet->vulkan.mDynamicOffsetCount, offsets);
}

void vk_cmdBindPushConstants(cmd_t* pCmd, RootSignature* pRootSignature, uint32_t paramIndex, const void* pConstants)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pConstants);
	TC_ASSERT(pRootSignature);
	TC_ASSERT(paramIndex >= 0 && paramIndex < pRootSignature->mDescriptorCount);

	const DescriptorInfo* desc = pRootSignature->descriptors + paramIndex;
	TC_ASSERT(desc);
	TC_ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == desc->mType);

	vkCmdPushConstants(
		pCmd->vulkan.pVkCmdBuf, pRootSignature->vulkan.pipelineLayout, desc->vulkan.mVkStages, 0, desc->mSize, pConstants);
}

void vk_cmdBindDescriptorSetWithRootCbvs(cmd_t* pCmd, uint32_t index, DescriptorSet* descriptorSet, uint32_t count, const DescriptorData* pParams)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(descriptorSet);
	TC_ASSERT(pParams);

	const RootSignature* pRootSignature = descriptorSet->vulkan.pRootSignature;
	uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };

	for (uint32_t i = 0; i < count; ++i)
	{
		const DescriptorData* pParam = pParams + i;
		uint32_t              paramIndex = pParam->mBindByIndex ? pParam->mIndex : UINT32_MAX;

		const DescriptorInfo* desc =
			(paramIndex != UINT32_MAX) ? (pRootSignature->descriptors + paramIndex) : get_descriptor(pRootSignature, pParam->pName);
		if (paramIndex != UINT32_MAX)
		{
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param index (%u)", paramIndex);
		}
		else
		{
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param name (%s)", pParam->pName);
		}

#if defined(ENABLE_GRAPHICS_DEBUG)
		const uint32_t maxRange = DESCRIPTOR_TYPE_UNIFORM_BUFFER == desc->mType ?    //-V522
			pCmd->renderer->vulkan.activegpuprops->properties.limits.maxUniformBufferRange :
			pCmd->renderer->vulkan.activegpuprops->properties.limits.maxStorageBufferRange;
#endif

		VALIDATE_DESCRIPTOR(desc->mRootDescriptor, "Descriptor (%s) - must be a root cbv", desc->pName);
		VALIDATE_DESCRIPTOR(pParam->mCount <= 1, "Descriptor (%s) - cmdBindDescriptorSetWithRootCbvs does not support arrays", desc->pName);
		VALIDATE_DESCRIPTOR(pParam->pRanges, "Descriptor (%s) - pRanges must be provided for cmdBindDescriptorSetWithRootCbvs", desc->pName);

		DescriptorDataRange range = pParam->pRanges[0];
		VALIDATE_DESCRIPTOR(range.mSize, "Descriptor (%s) - pRanges->mSize is zero", desc->pName);
		VALIDATE_DESCRIPTOR(
			range.mSize <= maxRange,
			"Descriptor (%s) - pRanges->mSize is %ull which exceeds max size %u", desc->pName, range.mSize,
			maxRange);

		offsets[desc->mHandleIndex] = range.mOffset; //-V522
		DynamicUniformData* pData = &descriptorSet->vulkan.pDynamicUniformData[index * descriptorSet->vulkan.mDynamicOffsetCount + desc->mHandleIndex];
		if (pData->pBuffer != pParam->ppBuffers[0]->vulkan.pVkBuffer || range.mSize != pData->mSize)
		{
			*pData = { pParam->ppBuffers[0]->vulkan.pVkBuffer, 0, range.mSize };

			VkDescriptorBufferInfo bufferInfo = { pData->pBuffer, 0, (VkDeviceSize)pData->mSize };
			VkWriteDescriptorSet writeSet = { 0 };
			writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSet.pNext = NULL;
			writeSet.descriptorCount = 1;
			writeSet.descriptorType = (VkDescriptorType)desc->vulkan.mVkType;
			writeSet.dstArrayElement = 0;
			writeSet.dstBinding = desc->vulkan.mReg;
			writeSet.dstSet = descriptorSet->vulkan.pHandles[index];
			writeSet.pBufferInfo = &bufferInfo;
			vkUpdateDescriptorSets(pCmd->renderer->vulkan.device, 1, &writeSet, 0, NULL);
		}
	}

	vkCmdBindDescriptorSets(
		pCmd->vulkan.pVkCmdBuf, gPipelineBindPoint[pRootSignature->mPipelineType], pRootSignature->vulkan.pipelineLayout,
		descriptorSet->vulkan.mUpdateFrequency, 1, &descriptorSet->vulkan.pHandles[index],
		descriptorSet->vulkan.mDynamicOffsetCount, offsets);
}
/************************************************************************/
// Shader Functions
/************************************************************************/
void vk_addShaderBinary(renderer_t* renderer, const BinaryShaderDesc* desc, Shader** ppShaderProgram)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(ppShaderProgram);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);

	uint32_t counter = 0;

	size_t totalSize = sizeof(Shader);
	totalSize += sizeof(PipelineReflection);

	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		ShaderStage stage_mask = (ShaderStage)(1 << i);
		if (stage_mask == (desc->mStages & stage_mask))
		{
			switch (stage_mask)
			{
				case SHADER_STAGE_VERT: totalSize += (strlen(desc->mVert.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_TESC: totalSize += (strlen(desc->mHull.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_TESE: totalSize += (strlen(desc->mDomain.pEntryPoint) + 1) * sizeof(char); break;    //-V814
				case SHADER_STAGE_GEOM: totalSize += (strlen(desc->mGeom.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_FRAG: totalSize += (strlen(desc->mFrag.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_RAYTRACING:
				case SHADER_STAGE_COMP: totalSize += (strlen(desc->mComp.pEntryPoint) + 1) * sizeof(char); break;    //-V814
				default: break;
			}
			++counter;
		}
	}

	if (desc->mConstantCount)
	{
		totalSize += sizeof(VkSpecializationInfo);
		totalSize += sizeof(VkSpecializationMapEntry) * desc->mConstantCount;
		for (uint32_t i = 0; i < desc->mConstantCount; ++i)
		{
			const ShaderConstant* constant = &desc->pConstants[i];
			totalSize += (constant->mSize == sizeof(bool)) ? sizeof(VkBool32) : constant->mSize;
		}
	}

	totalSize += counter * sizeof(VkShaderModule);
	totalSize += counter * sizeof(char*);
	Shader* pShaderProgram = (Shader*)tc_calloc(1, totalSize);
	pShaderProgram->mStages = desc->mStages;
	pShaderProgram->pReflection = (PipelineReflection*)(pShaderProgram + 1);    //-V1027
	pShaderProgram->vulkan.pShaderModules = (VkShaderModule*)(pShaderProgram->pReflection + 1);
	pShaderProgram->vulkan.pEntryNames = (char**)(pShaderProgram->vulkan.pShaderModules + counter);
	pShaderProgram->vulkan.pSpecializationInfo = NULL;

	uint8_t* mem = (uint8_t*)(pShaderProgram->vulkan.pEntryNames + counter);
	counter = 0;
	ShaderReflection stageReflections[SHADER_STAGE_COUNT] = { 0 };

	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		ShaderStage stage_mask = (ShaderStage)(1 << i);
		if (stage_mask == (pShaderProgram->mStages & stage_mask))
		{
			DECLARE_ZERO(VkShaderModuleCreateInfo, info);
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.pNext = NULL;
			info.flags = 0;

			const BinaryShaderStageDesc* pStageDesc = nullptr;
			switch (stage_mask)
			{
				case SHADER_STAGE_VERT:
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mVert.pByteCode, (uint32_t)desc->mVert.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mVert.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mVert.pByteCode;
					pStageDesc = &desc->mVert;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				case SHADER_STAGE_TESC:
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mHull.pByteCode, (uint32_t)desc->mHull.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mHull.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mHull.pByteCode;
					pStageDesc = &desc->mHull;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				case SHADER_STAGE_TESE:
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mDomain.pByteCode, (uint32_t)desc->mDomain.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mDomain.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mDomain.pByteCode;
					pStageDesc = &desc->mDomain;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				case SHADER_STAGE_GEOM:
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mGeom.pByteCode, (uint32_t)desc->mGeom.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mGeom.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mGeom.pByteCode;
					pStageDesc = &desc->mGeom;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				case SHADER_STAGE_FRAG:
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mFrag.pByteCode, (uint32_t)desc->mFrag.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mFrag.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mFrag.pByteCode;
					pStageDesc = &desc->mFrag;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				case SHADER_STAGE_COMP:
#ifdef VK_RAYTRACING_AVAILABLE
				case SHADER_STAGE_RAYTRACING:
#endif
				{
					vk_createShaderReflection(
						(const uint8_t*)desc->mComp.pByteCode, (uint32_t)desc->mComp.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					info.codeSize = desc->mComp.mByteCodeSize;
					info.pCode = (const uint32_t*)desc->mComp.pByteCode;
					pStageDesc = &desc->mComp;
					CHECK_VKRESULT(vkCreateShaderModule(
						renderer->vulkan.device, &info, &alloccbs,
						&(pShaderProgram->vulkan.pShaderModules[counter])));
				}
				break;
				default: TC_ASSERT(false && "Shader Stage not supported!"); break;
			}

			pShaderProgram->vulkan.pEntryNames[counter] = (char*)mem;
			mem += (strlen(pStageDesc->pEntryPoint) + 1) * sizeof(char);    //-V522
			strcpy(pShaderProgram->vulkan.pEntryNames[counter], pStageDesc->pEntryPoint);
			++counter;
		}
	}

	// Fill specialization constant entries
	if (desc->mConstantCount)
	{
		pShaderProgram->vulkan.pSpecializationInfo = (VkSpecializationInfo*)mem;
		mem += sizeof(VkSpecializationInfo);

		VkSpecializationMapEntry* mapEntries = (VkSpecializationMapEntry*)mem;
		mem += desc->mConstantCount * sizeof(VkSpecializationMapEntry);

		uint8_t* data = mem;
		uint32_t offset = 0;
		for (uint32_t i = 0; i < desc->mConstantCount; ++i)
		{
			const ShaderConstant* constant = &desc->pConstants[i];
			const bool boolType = constant->mSize == sizeof(bool);
			const uint32_t size = boolType ? sizeof(VkBool32) : constant->mSize;

			VkSpecializationMapEntry* entry = &mapEntries[i];
			entry->constantID = constant->mIndex;
			entry->offset = offset;
			entry->size = size;

			if (boolType)
			{
				*(VkBool32*)(data + offset) = *(const bool*)constant->pValue;
			}
			else
			{
				memcpy(data + offset, constant->pValue, constant->mSize);
			}
			offset += size;
		}

		VkSpecializationInfo* specializationInfo = pShaderProgram->vulkan.pSpecializationInfo;
		specializationInfo->dataSize = offset;
		specializationInfo->mapEntryCount = desc->mConstantCount;
		specializationInfo->pData = data;
		specializationInfo->pMapEntries = mapEntries;
	}

	createPipelineReflection(stageReflections, counter, pShaderProgram->pReflection);

	addShaderDependencies(pShaderProgram, desc);

	*ppShaderProgram = pShaderProgram;
}

void vk_removeShader(renderer_t* renderer, Shader* pShaderProgram)
{
	removeShaderDependencies(pShaderProgram);

	TC_ASSERT(renderer);

	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);

	if (pShaderProgram->mStages & SHADER_STAGE_VERT)
	{
		vkDestroyShaderModule(
			renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mVertexStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESC)
	{
		vkDestroyShaderModule(
			renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mHullStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESE)
	{
		vkDestroyShaderModule(
			renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mDomainStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_GEOM)
	{
		vkDestroyShaderModule(
			renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mGeometryStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_FRAG)
	{
		vkDestroyShaderModule(
			renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mPixelStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_COMP)
	{
		vkDestroyShaderModule(renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[0], &alloccbs);
	}
#ifdef VK_RAYTRACING_AVAILABLE
	if (pShaderProgram->mStages & SHADER_STAGE_RAYTRACING)
	{
		vkDestroyShaderModule(renderer->vulkan.device, pShaderProgram->vulkan.pShaderModules[0], &alloccbs);
	}
#endif

	destroyPipelineReflection(pShaderProgram->pReflection);
	tc_free(pShaderProgram);
}
/************************************************************************/
// Root Signature Functions
/************************************************************************/
typedef struct vk_UpdateFrequencyLayoutInfo
{
	/// Array of all bindings in the descriptor set
	VkDescriptorSetLayoutBinding* mBindings = NULL;
	/// Array of all descriptors in this descriptor set
	DescriptorInfo** descriptors = NULL;
	/// Array of all descriptors marked as dynamic in this descriptor set (applicable to DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	DescriptorInfo** mDynamicDescriptors = NULL;
} UpdateFrequencyLayoutInfo;

static bool compareDescriptorSetLayoutBinding(const VkDescriptorSetLayoutBinding* pLhs, const VkDescriptorSetLayoutBinding* pRhs)
{
	if (pRhs->descriptorType < pLhs->descriptorType)
		return true;
	else if (pRhs->descriptorType == pLhs->descriptorType && pRhs->binding < pLhs->binding)
		return true;

	return false;
}

typedef DescriptorInfo* descriptorInfo;
static bool comparedescriptorInfo(const descriptorInfo* pLhs, const descriptorInfo* pRhs)
{
	return (*pLhs)->vulkan.mReg < (*pRhs)->vulkan.mReg;
}

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding, simpleSortVkDescriptorSetLayoutBinding)
DEFINE_PARTITION_IMPL_FUNCTION(static, partitionImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding)
DEFINE_QUICK_SORT_IMPL_FUNCTION(static, quickSortImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding, stableSortVkDescriptorSetLayoutBinding, partitionImplVkDescriptorSetLayoutBinding)
DEFINE_QUICK_SORT_FUNCTION(static, sortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, quickSortImplVkDescriptorSetLayoutBinding)

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortdescriptorInfo, descriptorInfo, comparedescriptorInfo)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortdescriptorInfo, descriptorInfo, comparedescriptorInfo, simpleSortdescriptorInfo)

void vk_addRootSignature(renderer_t* renderer, const RootSignatureDesc* pRootSignatureDesc, RootSignature** ppRootSignature)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pRootSignatureDesc);
	TC_ASSERT(ppRootSignature);

	typedef struct StaticSamplerNode
	{
		char*		key;
		sampler_t*	value;
	}StaticSamplerNode;

	static constexpr uint32_t	kMaxLayoutCount = DESCRIPTOR_UPDATE_FREQ_COUNT;
	UpdateFrequencyLayoutInfo	layouts[kMaxLayoutCount] = { 0 };
	VkPushConstantRange			pushConstants[SHADER_STAGE_COUNT] = { 0 };
	uint32_t					pushConstantCount = 0;
	ShaderResource*				shaderResources = NULL;
	StaticSamplerNode*			staticSamplerMap = NULL;
	sh_new_arena(staticSamplerMap);

	for (uint32_t i = 0; i < pRootSignatureDesc->mStaticSamplerCount; ++i)
	{
		TC_ASSERT(pRootSignatureDesc->ppStaticSamplers[i]);
		shput(staticSamplerMap, pRootSignatureDesc->ppStaticSamplerNames[i], pRootSignatureDesc->ppStaticSamplers[i]);
	}

	PipelineType		pipelineType = PIPELINE_TYPE_UNDEFINED;
	DescriptorIndexMap*	indexMap = NULL;
	sh_new_arena(indexMap);

	// Collect all unique shader resources in the given shaders
	// Resources are parsed by name (two resources named "XYZ" in two shaders will be considered the same resource)
	for (uint32_t sh = 0; sh < pRootSignatureDesc->mShaderCount; ++sh)
	{
		PipelineReflection const* pReflection = pRootSignatureDesc->ppShaders[sh]->pReflection;

		if (pReflection->mShaderStages & SHADER_STAGE_COMP)
			pipelineType = PIPELINE_TYPE_COMPUTE;
#ifdef VK_RAYTRACING_AVAILABLE
		else if (pReflection->mShaderStages & SHADER_STAGE_RAYTRACING)
			pipelineType = PIPELINE_TYPE_RAYTRACING;
#endif
		else
			pipelineType = PIPELINE_TYPE_GRAPHICS;

		for (uint32_t i = 0; i < pReflection->mShaderResourceCount; ++i)
		{
			ShaderResource const* pRes = &pReflection->pShaderResources[i];
			// uint32_t              setIndex = pRes->set;

			// if (pRes->type == DESCRIPTOR_TYPE_ROOT_CONSTANT)
			// 	setIndex = 0;

			DescriptorIndexMap* pNode = shgetp_null(indexMap, pRes->name);
			if (!pNode)
			{
				ShaderResource* pResource = NULL;
				for (ptrdiff_t i = 0; i < arrlen(shaderResources); ++i)
				{
					ShaderResource* pCurrent = &shaderResources[i];
					if (pCurrent->type == pRes->type &&
						(pCurrent->used_stages == pRes->used_stages) &&
						(((pCurrent->reg ^ pRes->reg) | (pCurrent->set ^ pRes->set)) == 0))
					{
						pResource = pCurrent;
						break;
					}

				}
				if (!pResource)
				{
					shput(indexMap, pRes->name, (uint32_t)arrlen(shaderResources));
					arrpush(shaderResources, *pRes);
				}
				else
				{
					TC_ASSERT(pRes->type == pResource->type);
					if (pRes->type != pResource->type)
					{
						LOGF(
							LogLevel::eERROR,
							"\nFailed to create root signature\n"
							"Shared shader resources %s and %s have mismatching types (%u) and (%u). All shader resources "
							"sharing the same register and space addRootSignature "
							"must have the same type",
							pRes->name, pResource->name, (uint32_t)pRes->type, (uint32_t)pResource->type);
						return;
					}

					uint32_t value = shget(indexMap, pResource->name);
					shput(indexMap, pRes->name, value);
					pResource->used_stages |= pRes->used_stages;
				}
			}
			else
			{
				if (shaderResources[pNode->value].reg != pRes->reg) //-V::522, 595
				{
					LOGF(
						LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching binding. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"must have the same binding and set",
						pRes->name);
					return;
				}
				if (shaderResources[pNode->value].set != pRes->set) //-V::522, 595
				{
					LOGF(
						LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching set. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"must have the same binding and set",
						pRes->name);
					return;
				}

				for (ptrdiff_t i = 0; i < arrlen(shaderResources); ++i)
				{
					if (strcmp(shaderResources[i].name, pNode->key) == 0)
					{
						shaderResources[i].used_stages |= pRes->used_stages;
						break;
					}
				}
			}
		}
	}

	size_t totalSize = sizeof(RootSignature);
	totalSize += arrlenu(shaderResources) * sizeof(DescriptorInfo);
	RootSignature* pRootSignature = (RootSignature*)tc_calloc_memalign(1, alignof(RootSignature), totalSize);
	TC_ASSERT(pRootSignature);

	pRootSignature->descriptors = (DescriptorInfo*)(pRootSignature + 1);                                                        //-V1027
	pRootSignature->descriptorNameToIndexMap = indexMap;

	if (arrlen(shaderResources))
	{
		pRootSignature->mDescriptorCount = (uint32_t)arrlen(shaderResources);
	}

	pRootSignature->mPipelineType = pipelineType;

	// Fill the descriptor array to be stored in the root signature
	for (ptrdiff_t i = 0; i < arrlen(shaderResources); ++i)
	{
		DescriptorInfo*           desc = &pRootSignature->descriptors[i];
		ShaderResource const*     pRes = &shaderResources[i];
		uint32_t                  setIndex = pRes->set;
		DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)setIndex;

		// Copy the binding information generated from the shader reflection into the descriptor
		desc->vulkan.mReg = pRes->reg;
		desc->mSize = pRes->size;
		desc->mType = pRes->type;
		desc->pName = pRes->name;
		desc->mDim = pRes->dim;

		// If descriptor is not a root constant create a new layout binding for this descriptor and add it to the binding array
		if (desc->mType != DESCRIPTOR_TYPE_ROOT_CONSTANT)
		{
			VkDescriptorSetLayoutBinding binding = { 0 };
			binding.binding = pRes->reg;
			binding.descriptorCount = desc->mSize;
			binding.descriptorType = util_to_vk_descriptor_type((DescriptorType)desc->mType);

			// If a user specified a uniform buffer to be used as a dynamic uniform buffer change its type to VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			// Also log a message for debugging purpose
			if (isDescriptorRootCbv(pRes->name))
			{
				if (desc->mSize == 1)
				{
					TRACE(LOG_INFO, "Descriptor (%s) : User specified VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC", desc->pName);
					binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				}
				else
				{
					LOGF(
						LogLevel::eWARNING, "Descriptor (%s) : Cannot use VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC for arrays",
						desc->pName);
				}
			}

			binding.stageFlags = util_to_vk_shader_stage_flags(pRes->used_stages);

			// Store the vulkan related info in the descriptor to avoid constantly calling the util_to_vk mapping functions
			desc->vulkan.mVkType = binding.descriptorType;
			desc->vulkan.mVkStages = binding.stageFlags;
			desc->mUpdateFrequency = updateFreq;

			// Find if the given descriptor is a static sampler
			StaticSamplerNode* it = shgetp_null(staticSamplerMap, desc->pName);
			if (it)
			{
				TRACE(LOG_INFO, "Descriptor (%s) : User specified Static Sampler", desc->pName);
				binding.pImmutableSamplers = &it->value->vulkan.pVkSampler;
			}

			// Set the index to an invalid value so we can use this later for error checking if user tries to update a static sampler
			// In case of Combined Image Samplers, skip invalidating the index
			// because we do not to introduce new ways to update the descriptor in the Interface
			if (it && desc->mType != DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				desc->mStaticSampler = true;
			}
			else
			{
				arrpush(layouts[setIndex].descriptors, desc);
			}

			if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				arrpush(layouts[setIndex].mDynamicDescriptors, desc);
				desc->mRootDescriptor = true;
			}

			arrpush(layouts[setIndex].mBindings, binding);

			// Update descriptor pool size for this descriptor type
			VkDescriptorPoolSize* poolSize = NULL;
			for (uint32_t i = 0; i < MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT; ++i)
			{
				if (binding.descriptorType == pRootSignature->vulkan.mPoolSizes[setIndex][i].type && pRootSignature->vulkan.mPoolSizes[setIndex][i].descriptorCount)
				{
					poolSize = &pRootSignature->vulkan.mPoolSizes[setIndex][i];
					break;
				}
			}
			if (!poolSize)
			{
				poolSize = &pRootSignature->vulkan.mPoolSizes[setIndex][pRootSignature->vulkan.mPoolSizeCount[setIndex]++];
				poolSize->type = binding.descriptorType;
			}

			poolSize->descriptorCount += binding.descriptorCount;
		}
		// If descriptor is a root constant, add it to the root constant array
		else
		{
			TRACE(LOG_INFO, "Descriptor (%s) : User specified Push Constant", desc->pName);

			desc->mRootDescriptor = true;
			desc->vulkan.mVkStages = util_to_vk_shader_stage_flags(pRes->used_stages);
			setIndex = 0;
			desc->mHandleIndex = pushConstantCount++;

			pushConstants[desc->mHandleIndex] = { 0 };
			pushConstants[desc->mHandleIndex].offset = 0;
			pushConstants[desc->mHandleIndex].size = desc->mSize;
			pushConstants[desc->mHandleIndex].stageFlags = desc->vulkan.mVkStages;
		}
	}

	// Create descriptor layouts
	// Put least frequently changed params first
	for (uint32_t layoutIndex = kMaxLayoutCount; layoutIndex-- > 0U;)
	{
		UpdateFrequencyLayoutInfo& layout = layouts[layoutIndex];

		if (arrlen(layouts[layoutIndex].mBindings))
		{
			// sort table by type (CBV/SRV/UAV) by register
			sortVkDescriptorSetLayoutBinding(layout.mBindings, arrlenu(layout.mBindings));
		}

		bool createLayout = arrlen(layout.mBindings) != 0;
		// Check if we need to create an empty layout in case there is an empty set between two used sets
		// Example: set = 0 is used, set = 2 is used. In this case, set = 1 needs to exist even if it is empty
		if (!createLayout && layoutIndex < kMaxLayoutCount - 1)
		{
			createLayout = pRootSignature->vulkan.mVkDescriptorSetLayouts[layoutIndex + 1] != VK_NULL_HANDLE;
		}

		if (createLayout)
		{
			if (arrlen(layout.mBindings))
			{
				VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
				layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				layoutInfo.pNext = NULL;
				layoutInfo.bindingCount = (uint32_t)arrlen(layout.mBindings);
				layoutInfo.pBindings = layout.mBindings;
				layoutInfo.flags = 0;

				CHECK_VKRESULT(vkCreateDescriptorSetLayout(
					renderer->vulkan.device, &layoutInfo, &alloccbs, &pRootSignature->vulkan.mVkDescriptorSetLayouts[layoutIndex]));
			}
			else
			{
				pRootSignature->vulkan.mVkDescriptorSetLayouts[layoutIndex] = renderer->vulkan.pEmptyDescriptorSetLayout;
			}
		}

		if (!arrlen(layout.mBindings))
		{
			continue;
		}

		uint32_t cumulativeDescriptorCount = 0;

		for (ptrdiff_t descIndex = 0; descIndex < arrlen(layout.descriptors); ++descIndex)
		{
			DescriptorInfo* desc = layout.descriptors[descIndex];
			if (!desc->mRootDescriptor)
			{
				desc->mHandleIndex = cumulativeDescriptorCount;
				cumulativeDescriptorCount += desc->mSize;
			}
		}

		if (arrlen(layout.mDynamicDescriptors))
		{
			// vkCmdBindDescriptorSets - pDynamicOffsets - entries are ordered by the binding numbers in the descriptor set layouts
			stableSortdescriptorInfo(layout.mDynamicDescriptors, arrlenu(layout.mDynamicDescriptors));

			pRootSignature->vulkan.mVkDynamicDescriptorCounts[layoutIndex] = (uint32_t)arrlen(layout.mDynamicDescriptors);
			for (uint32_t descIndex = 0; descIndex < (uint32_t)arrlen(layout.mDynamicDescriptors); ++descIndex)
			{
				DescriptorInfo* desc = layout.mDynamicDescriptors[descIndex];
				desc->mHandleIndex = descIndex;
			}
		}
	}
	/************************************************************************/
	// Pipeline layout
	/************************************************************************/
	VkDescriptorSetLayout descriptorSetLayouts[kMaxLayoutCount] = { 0 };
	uint32_t              descriptorSetLayoutCount = 0;
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		if (pRootSignature->vulkan.mVkDescriptorSetLayouts[i])
		{
			descriptorSetLayouts[descriptorSetLayoutCount++] = pRootSignature->vulkan.mVkDescriptorSetLayouts[i];
		}
	}

	DECLARE_ZERO(VkPipelineLayoutCreateInfo, info);
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.setLayoutCount = descriptorSetLayoutCount;
	info.pSetLayouts = descriptorSetLayouts;
	info.pushConstantRangeCount = pushConstantCount;
	info.pPushConstantRanges = pushConstants;
	CHECK_VKRESULT(vkCreatePipelineLayout(
		renderer->vulkan.device, &info, &alloccbs, &(pRootSignature->vulkan.pipelineLayout)));
	/************************************************************************/
	// Update templates
	/************************************************************************/
	for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
	{
		const UpdateFrequencyLayoutInfo& layout = layouts[setIndex];

		if (!arrlen(layout.descriptors) && pRootSignature->vulkan.mVkDescriptorSetLayouts[setIndex] != VK_NULL_HANDLE)
		{
			pRootSignature->vulkan.pEmptyDescriptorSet[setIndex] = renderer->vulkan.pEmptyDescriptorSet;
			if (pRootSignature->vulkan.mVkDescriptorSetLayouts[setIndex] != renderer->vulkan.pEmptyDescriptorSetLayout)
			{
				add_descriptor_pool(renderer, 1, 0,
					pRootSignature->vulkan.mPoolSizes[setIndex], pRootSignature->vulkan.mPoolSizeCount[setIndex],
					&pRootSignature->vulkan.pEmptyDescriptorPool[setIndex]);
				VkDescriptorSet* emptySet[] = { &pRootSignature->vulkan.pEmptyDescriptorSet[setIndex] };
				consume_descriptor_sets(renderer->vulkan.device, pRootSignature->vulkan.pEmptyDescriptorPool[setIndex],
					&pRootSignature->vulkan.mVkDescriptorSetLayouts[setIndex],
					1, emptySet);
			}
		}
	}

	addRootSignatureDependencies(pRootSignature, pRootSignatureDesc);

	*ppRootSignature = pRootSignature;
	for (uint32_t i = 0; i < kMaxLayoutCount; ++i)
	{
		arrfree(layouts[i].mBindings);
		arrfree(layouts[i].descriptors);
		arrfree(layouts[i].mDynamicDescriptors);
	}
	arrfree(shaderResources);
	shfree(staticSamplerMap);
}

void vk_removeRootSignature(renderer_t* renderer, RootSignature* pRootSignature)
{
	removeRootSignatureDependencies(pRootSignature);

	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		if (pRootSignature->vulkan.mVkDescriptorSetLayouts[i] != renderer->vulkan.pEmptyDescriptorSetLayout)
		{
			vkDestroyDescriptorSetLayout(
				renderer->vulkan.device, pRootSignature->vulkan.mVkDescriptorSetLayouts[i], &alloccbs);
		}
		if (VK_NULL_HANDLE != pRootSignature->vulkan.pEmptyDescriptorPool[i])
		{
			vkDestroyDescriptorPool(
				renderer->vulkan.device, pRootSignature->vulkan.pEmptyDescriptorPool[i], &alloccbs);
		}
	}

	shfree(pRootSignature->descriptorNameToIndexMap);

	vkDestroyPipelineLayout(renderer->vulkan.device, pRootSignature->vulkan.pipelineLayout, &alloccbs);

	tc_free(pRootSignature);
}
/************************************************************************/
// Pipeline State Functions
/************************************************************************/
static void addGraphicsPipeline(renderer_t* renderer, const PipelineDesc* pMainDesc, pipeline_t** pipeline)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pipeline);
	TC_ASSERT(pMainDesc);

	const GraphicsPipelineDesc* desc = &pMainDesc->mGraphicsDesc;
	VkPipelineCache             psoCache = pMainDesc->pCache ? pMainDesc->pCache->vulkan.pCache : VK_NULL_HANDLE;

	TC_ASSERT(desc->pShaderProgram);
	TC_ASSERT(desc->pRootSignature);

	pipeline_t* pipeline = (pipeline_t*)tc_calloc_memalign(1, alignof(Pipeline), sizeof(Pipeline));
	TC_ASSERT(pipeline);

	const Shader*       pShaderProgram = desc->pShaderProgram;
	const VertexLayout* pVertexLayout = desc->pVertexLayout;

	pipeline->vulkan.mType = PIPELINE_TYPE_GRAPHICS;

	// Create tempporary renderpass for pipeline creation
	RenderPassDesc renderPassDesc = { 0 };
	RenderPass     renderPass = { 0 };
	renderPassDesc.rtcount = desc->rtcount;
	renderPassDesc.pColorFormats = desc->pColorFormats;
	renderPassDesc.mSampleCount = desc->mSampleCount;
	renderPassDesc.mDepthStencilFormat = desc->mDepthStencilFormat;
    renderPassDesc.mVRMultiview = desc->pShaderProgram->mIsMultiviewVR;
    renderPassDesc.mVRFoveatedRendering = desc->mVRFoveatedRendering;
	renderPassDesc.pLoadActionsColor = gDefaultLoadActions;
	renderPassDesc.mLoadActionDepth = gDefaultLoadActions[0];
	renderPassDesc.mLoadActionStencil = gDefaultLoadActions[1];
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	renderPassDesc.pStoreActionsColor = desc->pColorResolveActions ? desc->pColorResolveActions : gDefaultStoreActions;
#else
	renderPassDesc.pStoreActionsColor = gDefaultStoreActions;
#endif
	renderPassDesc.mStoreActionDepth = gDefaultStoreActions[0];
	renderPassDesc.mStoreActionStencil = gDefaultStoreActions[1];
	add_render_pass(renderer, &renderPassDesc, &renderPass);

	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	for (uint32_t i = 0; i < pShaderProgram->pReflection->mStageReflectionCount; ++i)
		TC_ASSERT(VK_NULL_HANDLE != pShaderProgram->vulkan.pShaderModules[i]);

	const VkSpecializationInfo* specializationInfo = pShaderProgram->vulkan.pSpecializationInfo;

	// Pipeline
	{
		uint32_t stage_count = 0;
		DECLARE_ZERO(VkPipelineShaderStageCreateInfo, stages[5]);
		for (uint32_t i = 0; i < 5; ++i)
		{
			ShaderStage stage_mask = (ShaderStage)(1 << i);
			if (stage_mask == (pShaderProgram->mStages & stage_mask))
			{
				stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				stages[stage_count].pNext = NULL;
				stages[stage_count].flags = 0;
				stages[stage_count].pSpecializationInfo = specializationInfo;
				switch (stage_mask)
				{
					case SHADER_STAGE_VERT:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mVertexStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
						stages[stage_count].module = pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mVertexStageIndex];
					}
					break;
					case SHADER_STAGE_TESC:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mHullStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
						stages[stage_count].module = pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mHullStageIndex];
					}
					break;
					case SHADER_STAGE_TESE:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mDomainStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
						stages[stage_count].module = pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mDomainStageIndex];
					}
					break;
					case SHADER_STAGE_GEOM:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mGeometryStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
						stages[stage_count].module =
							pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mGeometryStageIndex];
					}
					break;
					case SHADER_STAGE_FRAG:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mPixelStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						stages[stage_count].module = pShaderProgram->vulkan.pShaderModules[pShaderProgram->pReflection->mPixelStageIndex];
					}
					break;
					default: TC_ASSERT(false && "Shader Stage not supported!"); break;
				}
				++stage_count;
			}
		}

		// Make sure there's a shader
		TC_ASSERT(0 != stage_count);

		uint32_t                          input_binding_count = 0;
		VkVertexInputBindingDescription   input_bindings[MAX_VERTEX_BINDINGS] = { { 0 } };
		uint32_t                          input_attribute_count = 0;
		VkVertexInputAttributeDescription input_attributes[MAX_VERTEX_ATTRIBS] = { { 0 } };

		// Make sure there's attributes
		if (pVertexLayout != NULL)
		{
			// Ignore everything that's beyond max_vertex_attribs
			uint32_t attrib_count = pVertexLayout->mAttribCount > MAX_VERTEX_ATTRIBS ? MAX_VERTEX_ATTRIBS : pVertexLayout->mAttribCount;
			uint32_t binding_value = UINT32_MAX;

			// Initial values
			for (uint32_t i = 0; i < attrib_count; ++i)
			{
				const VertexAttrib* attrib = &(pVertexLayout->mAttribs[i]);

				if (binding_value != attrib->mBinding)
				{
					binding_value = attrib->mBinding;
					++input_binding_count;
				}

				input_bindings[input_binding_count - 1].binding = binding_value;
				if (attrib->mRate == VERTEX_ATTRIB_RATE_INSTANCE)
				{
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
				}
				else
				{
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				}
				input_bindings[input_binding_count - 1].stride += TinyImageFormat_BitSizeOfBlock(attrib->mFormat) / 8;

				input_attributes[input_attribute_count].location = attrib->mLocation;
				input_attributes[input_attribute_count].binding = attrib->mBinding;
				input_attributes[input_attribute_count].format = (VkFormat)TinyImageFormat_ToVkFormat(attrib->mFormat);
				input_attributes[input_attribute_count].offset = attrib->mOffset;
				++input_attribute_count;
			}
		}

		DECLARE_ZERO(VkPipelineVertexInputStateCreateInfo, vi);
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.pNext = NULL;
		vi.flags = 0;
		vi.vertexBindingDescriptionCount = input_binding_count;
		vi.pVertexBindingDescriptions = input_bindings;
		vi.vertexAttributeDescriptionCount = input_attribute_count;
		vi.pVertexAttributeDescriptions = input_attributes;

		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		switch (desc->mPrimitiveTopo)
		{
			case PRIMITIVE_TOPO_POINT_LIST: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
			case PRIMITIVE_TOPO_LINE_LIST: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
			case PRIMITIVE_TOPO_LINE_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
			case PRIMITIVE_TOPO_TRI_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
			case PRIMITIVE_TOPO_PATCH_LIST: topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST; break;
			case PRIMITIVE_TOPO_TRI_LIST: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
			default: TC_ASSERT(false && "Primitive Topo not supported!"); break;
		}
		DECLARE_ZERO(VkPipelineInputAssemblyStateCreateInfo, ia);
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.pNext = NULL;
		ia.flags = 0;
		ia.topology = topology;
		ia.primitiveRestartEnable = VK_FALSE;

		DECLARE_ZERO(VkPipelineTessellationStateCreateInfo, ts);
		if ((pShaderProgram->mStages & SHADER_STAGE_TESC) && (pShaderProgram->mStages & SHADER_STAGE_TESE))
		{
			ts.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
			ts.pNext = NULL;
			ts.flags = 0;
			ts.patchControlPoints =
				pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mHullStageIndex].mNumControlPoint;
		}

		DECLARE_ZERO(VkPipelineViewportStateCreateInfo, vs);
		vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vs.pNext = NULL;
		vs.flags = 0;
		// we are using dynamic viewports but we must set the count to 1
		vs.viewportCount = 1;
		vs.pViewports = NULL;
		vs.scissorCount = 1;
		vs.pScissors = NULL;

		DECLARE_ZERO(VkPipelineMultisampleStateCreateInfo, ms);
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.pNext = NULL;
		ms.flags = 0;
		ms.rasterizationSamples = util_to_vk_sample_count(desc->mSampleCount);
		ms.sampleShadingEnable = VK_FALSE;
		ms.minSampleShading = 0.0f;
		ms.pSampleMask = 0;
		ms.alphaToCoverageEnable = VK_FALSE;
		ms.alphaToOneEnable = VK_FALSE;

		DECLARE_ZERO(VkPipelineRasterizationStateCreateInfo, rs);
		rs = desc->pRasterizerState ? util_to_rasterizer_desc(desc->pRasterizerState) : gDefaultRasterizerDesc;

		/// TODO: Dont create depth state if no depth stencil bound
		DECLARE_ZERO(VkPipelineDepthStencilStateCreateInfo, ds);
		ds = desc->pDepthState ? util_to_depth_desc(desc->pDepthState) : gDefaultDepthDesc;

		DECLARE_ZERO(VkPipelineColorBlendStateCreateInfo, cb);
		DECLARE_ZERO(VkPipelineColorBlendAttachmentState, cbAtt[MAX_RENDER_TARGET_ATTACHMENTS]);
		cb = desc->pBlendState ? util_to_blend_desc(desc->pBlendState, cbAtt) : gDefaultBlendDesc;
		cb.attachmentCount = desc->rtcount;

		VkDynamicState dyn_states[] =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_BLEND_CONSTANTS,
			VK_DYNAMIC_STATE_DEPTH_BOUNDS,
			VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		};
		DECLARE_ZERO(VkPipelineDynamicStateCreateInfo, dy);
		dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dy.pNext = NULL;
		dy.flags = 0;
		dy.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]);
		dy.pDynamicStates = dyn_states;

		DECLARE_ZERO(VkGraphicsPipelineCreateInfo, info);
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.stageCount = stage_count;
		info.pStages = stages;
		info.pVertexInputState = &vi;
		info.pInputAssemblyState = &ia;

		if ((pShaderProgram->mStages & SHADER_STAGE_TESC) && (pShaderProgram->mStages & SHADER_STAGE_TESE))
			info.pTessellationState = &ts;
		else
			info.pTessellationState = NULL;    // set tessellation state to null if we have no tessellation

		info.pViewportState = &vs;
		info.pRasterizationState = &rs;
		info.pMultisampleState = &ms;
		info.depthstencilState = &ds;
		info.pColorBlendState = &cb;
		info.pDynamicState = &dy;
		info.layout = desc->pRootSignature->vulkan.pipelineLayout;
		info.renderPass = renderPass.pRenderPass;
		info.subpass = 0;
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.basePipelineIndex = -1;
		CHECK_VKRESULT(vkCreateGraphicsPipelines(
			renderer->vulkan.device, psoCache, 1, &info, &alloccbs, &(pipeline->vulkan.pVkPipeline)));

		remove_render_pass(renderer, &renderPass);
	}

	*pipeline = pipeline;
}

static void addComputePipeline(renderer_t* renderer, const PipelineDesc* pMainDesc, pipeline_t** pipeline)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pipeline);
	TC_ASSERT(pMainDesc);

	const ComputePipelineDesc* desc = &pMainDesc->mComputeDesc;
	VkPipelineCache            psoCache = pMainDesc->pCache ? pMainDesc->pCache->vulkan.pCache : VK_NULL_HANDLE;

	TC_ASSERT(desc->pShaderProgram);
	TC_ASSERT(desc->pRootSignature);
	TC_ASSERT(renderer->vulkan.device != VK_NULL_HANDLE);
	TC_ASSERT(desc->pShaderProgram->vulkan.pShaderModules[0] != VK_NULL_HANDLE);

	pipeline_t* pipeline = (pipeline_t*)tc_calloc_memalign(1, alignof(Pipeline), sizeof(Pipeline));
	TC_ASSERT(pipeline);
	pipeline->vulkan.mType = PIPELINE_TYPE_COMPUTE;

	// Pipeline
	{
		DECLARE_ZERO(VkPipelineShaderStageCreateInfo, stage);
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.pNext = NULL;
		stage.flags = 0;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = desc->pShaderProgram->vulkan.pShaderModules[0];
		stage.pName = desc->pShaderProgram->pReflection->mStageReflections[0].pEntryPoint;
		stage.pSpecializationInfo = desc->pShaderProgram->vulkan.pSpecializationInfo;

		DECLARE_ZERO(VkComputePipelineCreateInfo, info);
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.stage = stage;
		info.layout = desc->pRootSignature->vulkan.pipelineLayout;
		info.basePipelineHandle = 0;
		info.basePipelineIndex = 0;
		CHECK_VKRESULT(vkCreateComputePipelines(
			renderer->vulkan.device, psoCache, 1, &info, &alloccbs, &(pipeline->vulkan.pVkPipeline)));
	}

	*pipeline = pipeline;
}

void vk_addPipeline(renderer_t* renderer, const PipelineDesc* desc, pipeline_t** pipeline)
{
	switch (desc->mType)
	{
		case (PIPELINE_TYPE_COMPUTE):
		{
			addComputePipeline(renderer, desc, pipeline);
			break;
		}
		case (PIPELINE_TYPE_GRAPHICS):
		{
			addGraphicsPipeline(renderer, desc, pipeline);
			break;
		}
#ifdef VK_RAYTRACING_AVAILABLE
		case (PIPELINE_TYPE_RAYTRACING):
		{
			vk_addRaytracingPipeline(desc, pipeline);
			break;
		}
#endif
		default:
		{
			TC_ASSERT(false);
			*pipeline = { 0 };
			break;
		}
	}

	addPipelineDependencies(*pipeline, desc);

	if (*pipeline && desc->pName)
	{
		setPipelineName(renderer, *pipeline, desc->pName);
	}
}

void vk_removePipeline(renderer_t* renderer, pipeline_t* pipeline)
{
	removePipelineDependencies(pipeline);

	TC_ASSERT(renderer);
	TC_ASSERT(pipeline);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(VK_NULL_HANDLE != pipeline->vulkan.pVkPipeline);

#ifdef VK_RAYTRACING_AVAILABLE
	tc_free(pipeline->vulkan.ppShaderStageNames);
#endif

	vkDestroyPipeline(renderer->vulkan.device, pipeline->vulkan.pVkPipeline, &alloccbs);

	tc_free(pipeline);
}

void vk_addPipelineCache(renderer_t* renderer, const PipelineCacheDesc* desc, PipelineCache** pipelineCache)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(pipelineCache);

	PipelineCache* pipelineCache = (PipelineCache*)tc_calloc(1, sizeof(PipelineCache));
	TC_ASSERT(pipelineCache);

	VkPipelineCacheCreateInfo psoCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	psoCacheCreateInfo.initialDataSize = desc->mSize;
	psoCacheCreateInfo.pInitialData = desc->pData;
	psoCacheCreateInfo.flags = util_to_pipeline_cache_flags(desc->mFlags);
	CHECK_VKRESULT(
		vkCreatePipelineCache(renderer->vulkan.device, &psoCacheCreateInfo, &alloccbs, &pipelineCache->vulkan.pCache));

	*pipelineCache = pipelineCache;
}

void vk_removePipelineCache(renderer_t* renderer, PipelineCache* pipelineCache)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pipelineCache);

	if (pipelineCache->vulkan.pCache)
	{
		vkDestroyPipelineCache(renderer->vulkan.device, pipelineCache->vulkan.pCache, &alloccbs);
	}

	tc_free(pipelineCache);
}

void vk_getPipelineCacheData(renderer_t* renderer, PipelineCache* pipelineCache, size_t* pSize, void* pData)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pipelineCache);
	TC_ASSERT(pSize);

	if (pipelineCache->vulkan.pCache)
	{
		CHECK_VKRESULT(vkGetPipelineCacheData(renderer->vulkan.device, pipelineCache->vulkan.pCache, pSize, pData));
	}
}
/************************************************************************/
// Command buffer functions
/************************************************************************/
void vk_reset_cmdpool(renderer_t* renderer, cmdpool_t* pCmdPool)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pCmdPool);

	CHECK_VKRESULT(vkResetCommandPool(renderer->vulkan.device, pCmdPool->pVkCmdPool, 0));
}

void vk_cmd_begin(cmd_t* pCmd)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	DECLARE_ZERO(VkCommandBufferBeginInfo, begin_info);
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VkDeviceGroupCommandBufferBeginInfoKHR deviceGroupBeginInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO_KHR };
	deviceGroupBeginInfo.pNext = NULL;
	if (pCmd->renderer->gpumode == GPU_MODE_LINKED)
	{
		deviceGroupBeginInfo.deviceMask = (1 << pCmd->vulkan.mNodeIndex);
		begin_info.pNext = &deviceGroupBeginInfo;
	}

	CHECK_VKRESULT(vkBeginCommandBuffer(pCmd->vulkan.pVkCmdBuf, &begin_info));

	// Reset CPU side data
	pCmd->vulkan.pBoundPipelineLayout = NULL;
}

void vk_cmd_end(cmd_t* pCmd)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	if (pCmd->vulkan.pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->vulkan.pVkCmdBuf);
	}

	pCmd->vulkan.pVkActiveRenderPass = VK_NULL_HANDLE;

	CHECK_VKRESULT(vkEndCommandBuffer(pCmd->vulkan.pVkCmdBuf));
}

void vk_cmdBindRenderTargets(
	cmd_t* pCmd, uint32_t renderTargetCount, RenderTarget** rendertargets, RenderTarget* depthstencil,
	const LoadActionsDesc* pLoadActions /* = NULL*/, uint32_t* pColorArraySlices, uint32_t* pColorMipSlices, uint32_t depthArraySlice,
	uint32_t depthMipSlice)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	if (pCmd->vulkan.pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->vulkan.pVkCmdBuf);
		pCmd->vulkan.pVkActiveRenderPass = VK_NULL_HANDLE;
	}

	if (!renderTargetCount && !depthstencil)
		return;

	size_t renderPassHash = 0;
	size_t frameBufferHash = 0;
	bool vrFoveatedRendering = false;
	StoreActionType colorStoreAction[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
	StoreActionType depthStoreAction = { 0 };
	StoreActionType stencilStoreAction = { 0 };
	uint32_t frameBufferRenderTargetCount = 0;

	// Generate hash for render pass and frame buffer
	// NOTE:
	// Render pass does not care about underlying VkImageView. It only cares about the format and sample count of the attachments.
	// We hash those two values to generate render pass hash
	// Frame buffer is the actual array of all the VkImageViews
	// We hash the texture id associated with the render target to generate frame buffer hash
	for (uint32_t i = 0; i < renderTargetCount; ++i)
	{
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		bool resolveAttachment = pLoadActions && IsStoreActionResolve(pLoadActions->mStoreActionsColor[i]);
		if (resolveAttachment)
		{
			colorStoreAction[i] = rendertargets[i]->pTexture->mLazilyAllocated ? STORE_ACTION_RESOLVE_DONTCARE : STORE_ACTION_DONTCARE;
		}
		else
#endif
		{
			colorStoreAction[i] = rendertargets[i]->pTexture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
				(pLoadActions ? pLoadActions->mStoreActionsColor[i] : gDefaultStoreActions[i]);
		}

		uint32_t renderPassHashValues[] =
		{
			(uint32_t)rendertargets[i]->mFormat,
			(uint32_t)rendertargets[i]->mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionsColor[i] : gDefaultLoadActions[i],
			(uint32_t)colorStoreAction[i]
		};
		uint32_t frameBufferHashValues[] =
		{
			rendertargets[i]->vulkan.mId,
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			resolveAttachment ? rendertargets[i]->pResolveAttachment->vulkan.mId : 0
#endif
		};
		renderPassHash = tf_mem_hash<uint32_t>(renderPassHashValues, TF_ARRAY_COUNT(renderPassHashValues), renderPassHash);
		frameBufferHash = tf_mem_hash<uint32_t>(frameBufferHashValues, TF_ARRAY_COUNT(frameBufferHashValues), frameBufferHash);
		vrFoveatedRendering |= rendertargets[i]->mVRFoveatedRendering;

		++frameBufferRenderTargetCount;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		frameBufferRenderTargetCount += resolveAttachment ? 1 : 0;
#endif
	}
	if (depthstencil)
	{
		depthStoreAction = depthstencil->pTexture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
			(pLoadActions ? pLoadActions->mStoreActionDepth : gDefaultStoreActions[0]);
		stencilStoreAction = depthstencil->pTexture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
			(pLoadActions ? pLoadActions->mStoreActionStencil : gDefaultStoreActions[0]);

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		// Dont support depth stencil auto resolve
		TC_ASSERT(!(IsStoreActionResolve(depthStoreAction) || IsStoreActionResolve(stencilStoreAction)));
#endif

		uint32_t hashValues[] =
		{
			(uint32_t)depthstencil->mFormat,
			(uint32_t)depthstencil->mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionDepth : gDefaultLoadActions[0],
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionStencil : gDefaultLoadActions[0],
			(uint32_t)depthStoreAction,
			(uint32_t)stencilStoreAction,
		};
		renderPassHash = tf_mem_hash<uint32_t>(hashValues, 6, renderPassHash);
		frameBufferHash = tf_mem_hash<uint32_t>(&depthstencil->vulkan.mId, 1, frameBufferHash);
		vrFoveatedRendering |= depthstencil->mVRFoveatedRendering;
	}
	if (pColorArraySlices)
		frameBufferHash = tf_mem_hash<uint32_t>(pColorArraySlices, renderTargetCount, frameBufferHash);
	if (pColorMipSlices)
		frameBufferHash = tf_mem_hash<uint32_t>(pColorMipSlices, renderTargetCount, frameBufferHash);
	if (depthArraySlice != UINT32_MAX)
		frameBufferHash = tf_mem_hash<uint32_t>(&depthArraySlice, 1, frameBufferHash);
	if (depthMipSlice != UINT32_MAX)
		frameBufferHash = tf_mem_hash<uint32_t>(&depthMipSlice, 1, frameBufferHash);

	SampleCount sampleCount = SAMPLE_COUNT_1;

	// Need pointer to pointer in order to reassign hash map when it is resized
	RenderPassNode** pRenderPassMap = get_render_pass_map(pCmd->renderer->mUnlinkedRendererIndex);
	FrameBufferNode** framebufferMap = get_frame_buffer_map(pCmd->renderer->mUnlinkedRendererIndex);

	RenderPassNode*	 pNode = hmgetp_null(*pRenderPassMap, renderPassHash);
	FrameBufferNode* framebufferNode = hmgetp_null(*framebufferMap, frameBufferHash);

	// If a render pass of this combination already exists just use it or create a new one
	if (!pNode)
	{
		TinyImageFormat colorFormats[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
		TinyImageFormat depthStencilFormat = TinyImageFormat_UNDEFINED;
		bool vrMultiview = false;
		for (uint32_t i = 0; i < renderTargetCount; ++i)
		{
			colorFormats[i] = rendertargets[i]->mFormat;
			vrMultiview |= rendertargets[i]->mVRMultiview;
		}
		if (depthstencil)
		{
			depthStencilFormat = depthstencil->mFormat;
			sampleCount = depthstencil->mSampleCount;
			vrMultiview |= depthstencil->mVRMultiview;
		}
		else if (renderTargetCount)
		{
			sampleCount = rendertargets[0]->mSampleCount;
		}

		RenderPass renderPass = { 0 };
		RenderPassDesc renderPassDesc = { 0 };
		renderPassDesc.rtcount = renderTargetCount;
		renderPassDesc.mSampleCount = sampleCount;
		renderPassDesc.pColorFormats = colorFormats;
		renderPassDesc.mDepthStencilFormat = depthStencilFormat;
		renderPassDesc.pLoadActionsColor = pLoadActions ? pLoadActions->mLoadActionsColor : gDefaultLoadActions;
		renderPassDesc.mLoadActionDepth = pLoadActions ? pLoadActions->mLoadActionDepth : gDefaultLoadActions[0];
		renderPassDesc.mLoadActionStencil = pLoadActions ? pLoadActions->mLoadActionStencil : gDefaultLoadActions[0];
		renderPassDesc.pStoreActionsColor = colorStoreAction;
		renderPassDesc.mStoreActionDepth = depthStoreAction;
		renderPassDesc.mStoreActionStencil = stencilStoreAction;
		renderPassDesc.mVRMultiview = vrMultiview;
		renderPassDesc.mVRFoveatedRendering = vrFoveatedRendering;
		add_render_pass(pCmd->renderer, &renderPassDesc, &renderPass);

		// No need of a lock here since this map is per thread
		hmput(*pRenderPassMap, renderPassHash, renderPass);

		pNode = hmgetp_null(*pRenderPassMap, renderPassHash);
	}

	RenderPass* pRenderPass = &pNode->value;

	// If a frame buffer of this combination already exists just use it or create a new one
	if (!framebufferNode)
	{
		FrameBuffer frameBuffer = { 0 };
		FrameBufferDesc desc = { 0 };
		desc.rtcount = renderTargetCount;
		desc.depthstencil = depthstencil;
		desc.rendertargets = rendertargets;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		desc.pRenderTargetResolveActions = colorStoreAction;
#endif
		desc.pRenderPass = pRenderPass;
		desc.pColorArraySlices = pColorArraySlices;
		desc.pColorMipSlices = pColorMipSlices;
		desc.mDepthArraySlice = depthArraySlice;
		desc.mDepthMipSlice = depthMipSlice;
		desc.mVRFoveatedRendering = vrFoveatedRendering;
		add_framebuffer(pCmd->renderer, &desc, &frameBuffer);

		// No need of a lock here since this map is per thread
		hmput(*framebufferMap, frameBufferHash, frameBuffer);

		framebufferNode = hmgetp_null(*framebufferMap, frameBufferHash);
	}

	framebuffer_t* framebuffer = &framebufferNode->value;

	DECLARE_ZERO(VkRect2D, render_area);
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = framebuffer->width;
	render_area.extent.height = framebuffer->height;

	uint32_t     clearValueCount = frameBufferRenderTargetCount;
	VkClearValue clearValues[VK_MAX_ATTACHMENT_ARRAY_COUNT] = { 0 };
	if (pLoadActions)
	{
		for (uint32_t i = 0; i < renderTargetCount; ++i)
		{
			ClearValue clearValue = pLoadActions->mClearColorValues[i];
			clearValues[i].color = { { clearValue.r, clearValue.g, clearValue.b, clearValue.a } };
		}
		if (depthstencil)
		{
			clearValues[frameBufferRenderTargetCount].depthStencil = { pLoadActions->mClearDepth.depth, pLoadActions->mClearDepth.stencil };
			++clearValueCount;
		}
	}

	DECLARE_ZERO(VkRenderPassBeginInfo, begin_info);
	begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.renderPass = renderpass->renderpass;
	begin_info.framebuffer = framebuffer->framebuffer;
	begin_info.renderArea = render_area;
	begin_info.clearValueCount = clearValueCount;
	begin_info.pClearValues = clearValues;

	vkCmdBeginRenderPass(pCmd->vulkan.pVkCmdBuf, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	pCmd->vulkan.pVkActiveRenderPass = renderpass->renderpass;
}

void vk_cmdSetShadingRate(
	cmd_t* pCmd, ShadingRate shadingRate, texture_t* pTexture, ShadingRateCombiner postRasterizerRate, ShadingRateCombiner finalRate)
{
}

void vk_cmdSetViewport(cmd_t* pCmd, float x, float y, float width, float height, float minDepth, float maxDepth)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	DECLARE_ZERO(VkViewport, viewport);
	viewport.x = x;
    viewport.y = y + height;
	viewport.width = width;
    viewport.height = -height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	vkCmdSetViewport(pCmd->vulkan.pVkCmdBuf, 0, 1, &viewport);
}

void vk_cmdSetScissor(cmd_t* pCmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	DECLARE_ZERO(VkRect2D, rect);
	rect.offset.x = x;
	rect.offset.y = y;
	rect.extent.width = width;
	rect.extent.height = height;
	vkCmdSetScissor(pCmd->vulkan.pVkCmdBuf, 0, 1, &rect);
}

void vk_cmdSetStencilReferenceValue(cmd_t* pCmd, uint32_t val)
{
	TC_ASSERT(pCmd);

	vkCmdSetStencilReference(pCmd->vulkan.pVkCmdBuf, VK_STENCIL_FRONT_AND_BACK, val);
}

void vk_cmdBindPipeline(cmd_t* pCmd, pipeline_t* pipeline)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pipeline);
	TC_ASSERT(pCmd->vulkan.pVkCmdBuf != VK_NULL_HANDLE);

	VkPipelineBindPoint pipeline_bind_point = gPipelineBindPoint[pipeline->vulkan.mType];
	vkCmdBindPipeline(pCmd->vulkan.pVkCmdBuf, pipeline_bind_point, pipeline->vulkan.pVkPipeline);
}

void vk_cmdBindIndexBuffer(cmd_t* pCmd, buffer_t* pBuffer, uint32_t indexType, uint64_t offset)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pBuffer);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	VkIndexType vk_index_type = (INDEX_TYPE_UINT16 == indexType) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
	vkCmdBindIndexBuffer(pCmd->vulkan.pVkCmdBuf, pBuffer->vulkan.pVkBuffer, offset, vk_index_type);
}

void vk_cmdBindVertexBuffer(cmd_t* pCmd, uint32_t bufferCount, buffer_t** ppBuffers, const uint32_t* pStrides, const uint64_t* pOffsets)
{
	UNREF_PARAM(pStrides);

	TC_ASSERT(pCmd);
	TC_ASSERT(0 != bufferCount);
	TC_ASSERT(ppBuffers);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);
	TC_ASSERT(pStrides);

	const uint32_t max_buffers = pCmd->renderer->vulkan.activegpuprops->properties.limits.maxVertexInputBindings;
	uint32_t       capped_buffer_count = bufferCount > max_buffers ? max_buffers : bufferCount;

	// No upper bound for this, so use 64 for now
	TC_ASSERT(capped_buffer_count < 64);

	DECLARE_ZERO(VkBuffer, buffers[64]);
	DECLARE_ZERO(VkDeviceSize, offsets[64]);

	for (uint32_t i = 0; i < capped_buffer_count; ++i)
	{
		buffers[i] = ppBuffers[i]->vulkan.pVkBuffer;
		offsets[i] = (pOffsets ? pOffsets[i] : 0);
	}

	vkCmdBindVertexBuffers(pCmd->vulkan.pVkCmdBuf, 0, capped_buffer_count, buffers, offsets);
}

void vk_cmdDraw(cmd_t* pCmd, uint32_t vertex_count, uint32_t first_vertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	vkCmdDraw(pCmd->vulkan.pVkCmdBuf, vertex_count, 1, first_vertex, 0);
}

void vk_cmdDrawInstanced(cmd_t* pCmd, uint32_t vertexCount, uint32_t firstVertex, uint32_t instanceCount, uint32_t firstInstance)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	vkCmdDraw(pCmd->vulkan.pVkCmdBuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void vk_cmdDrawIndexed(cmd_t* pCmd, uint32_t index_count, uint32_t first_index, uint32_t first_vertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->vulkan.pVkCmdBuf, index_count, 1, first_index, first_vertex, 0);
}

void vk_cmdDrawIndexedInstanced(
	cmd_t* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t instanceCount, uint32_t firstInstance, uint32_t firstVertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vulkan.pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->vulkan.pVkCmdBuf, indexCount, instanceCount, firstIndex, firstVertex, firstInstance);
}

void vk_cmdDispatch(cmd_t* pCmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pCmd->vulkan.pVkCmdBuf != VK_NULL_HANDLE);

	vkCmdDispatch(pCmd->vulkan.pVkCmdBuf, groupCountX, groupCountY, groupCountZ);
}

void vk_cmd_resourcebarrier(
	cmd_t* pCmd, uint32_t numBufferBarriers, BufferBarrier* pBufferBarriers, uint32_t numTextureBarriers, TextureBarrier* pTextureBarriers,
	uint32_t numRtBarriers, RenderTargetBarrier* pRtBarriers)
{
	VkImageMemoryBarrier* imageBarriers =
		(numTextureBarriers + numRtBarriers)
			? (VkImageMemoryBarrier*)alloca((numTextureBarriers + numRtBarriers) * sizeof(VkImageMemoryBarrier))
			: NULL;
	uint32_t imageBarrierCount = 0;

	VkBufferMemoryBarrier* bufferBarriers =
		numBufferBarriers ? (VkBufferMemoryBarrier*)alloca(numBufferBarriers * sizeof(VkBufferMemoryBarrier)) : NULL;
	uint32_t bufferBarrierCount = 0;

	VkAccessFlags srcAccessFlags = 0;
	VkAccessFlags dstAccessFlags = 0;

	for (uint32_t i = 0; i < numBufferBarriers; ++i)
	{
		BufferBarrier*         pTrans = &pBufferBarriers[i];
		buffer_t*                pBuffer = pTrans->pBuffer;
		VkBufferMemoryBarrier* pBufferBarrier = NULL;

		if (RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mCurrentState && RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mNewState)
		{
			pBufferBarrier = &bufferBarriers[bufferBarrierCount++];             //-V522
			pBufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;    //-V522
			pBufferBarrier->pNext = NULL;

			pBufferBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			pBufferBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
		else
		{
			pBufferBarrier = &bufferBarriers[bufferBarrierCount++];
			pBufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			pBufferBarrier->pNext = NULL;

			pBufferBarrier->srcAccessMask = util_to_vk_access_flags(pTrans->mCurrentState);
			pBufferBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);
		}

		if (pBufferBarrier)
		{
			pBufferBarrier->buffer = pBuffer->vulkan.pVkBuffer;
			pBufferBarrier->size = VK_WHOLE_SIZE;
			pBufferBarrier->offset = 0;

			if (pTrans->mAcquire)
			{
				pBufferBarrier->srcQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
				pBufferBarrier->dstQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease)
			{
				pBufferBarrier->srcQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
				pBufferBarrier->dstQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
			}
			else
			{
				pBufferBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				pBufferBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}

			srcAccessFlags |= pBufferBarrier->srcAccessMask;
			dstAccessFlags |= pBufferBarrier->dstAccessMask;
		}
	}

	for (uint32_t i = 0; i < numTextureBarriers; ++i)
	{
		TextureBarrier*       pTrans = &pTextureBarriers[i];
		texture_t*              pTexture = pTrans->pTexture;
		VkImageMemoryBarrier* pImageBarrier = NULL;

		if (RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mCurrentState && RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mNewState)
		{
			pImageBarrier = &imageBarriers[imageBarrierCount++];              //-V522
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;    //-V522
			pImageBarrier->pNext = NULL;

			pImageBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			pImageBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			pImageBarrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			pImageBarrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else
		{
			pImageBarrier = &imageBarriers[imageBarrierCount++];
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pImageBarrier->pNext = NULL;

			pImageBarrier->srcAccessMask = util_to_vk_access_flags(pTrans->mCurrentState);
			pImageBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);
			pImageBarrier->oldLayout = util_to_vk_image_layout(pTrans->mCurrentState);
			pImageBarrier->newLayout = util_to_vk_image_layout(pTrans->mNewState);
		}

		if (pImageBarrier)
		{
			pImageBarrier->image = pTexture->vulkan.pVkImage;
			pImageBarrier->subresourceRange.aspectMask = (VkImageAspectFlags)pTexture->mAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = pTrans->mSubresourceBarrier ? pTrans->mMipLevel : 0;
			pImageBarrier->subresourceRange.levelCount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = pTrans->mSubresourceBarrier ? pTrans->mArrayLayer : 0;
			pImageBarrier->subresourceRange.layercount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;

			if (pTrans->mAcquire && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
				pImageBarrier->dstQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
				pImageBarrier->dstQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
			}
			else
			{
				pImageBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				pImageBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}

			srcAccessFlags |= pImageBarrier->srcAccessMask;
			dstAccessFlags |= pImageBarrier->dstAccessMask;
		}
	}

	for (uint32_t i = 0; i < numRtBarriers; ++i)
	{
		RenderTargetBarrier*  pTrans = &pRtBarriers[i];
		texture_t*              pTexture = pTrans->pRenderTarget->pTexture;
		VkImageMemoryBarrier* pImageBarrier = NULL;

		if (RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mCurrentState && RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mNewState)
		{
			pImageBarrier = &imageBarriers[imageBarrierCount++];
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pImageBarrier->pNext = NULL;

			pImageBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			pImageBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			pImageBarrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			pImageBarrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else
		{
			pImageBarrier = &imageBarriers[imageBarrierCount++];
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pImageBarrier->pNext = NULL;

			pImageBarrier->srcAccessMask = util_to_vk_access_flags(pTrans->mCurrentState);
			pImageBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);
			pImageBarrier->oldLayout = util_to_vk_image_layout(pTrans->mCurrentState);
			pImageBarrier->newLayout = util_to_vk_image_layout(pTrans->mNewState);
		}

		if (pImageBarrier)
		{
			pImageBarrier->image = pTexture->vulkan.pVkImage;
			pImageBarrier->subresourceRange.aspectMask = (VkImageAspectFlags)pTexture->mAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = pTrans->mSubresourceBarrier ? pTrans->mMipLevel : 0;
			pImageBarrier->subresourceRange.levelCount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = pTrans->mSubresourceBarrier ? pTrans->mArrayLayer : 0;
			pImageBarrier->subresourceRange.layercount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;

			if (pTrans->mAcquire && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
				pImageBarrier->dstQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->pQueue->vulkan.mVkQueueFamilyIndex;
				pImageBarrier->dstQueueFamilyIndex = pCmd->renderer->vulkan.mQueueFamilyIndices[pTrans->mQueueType];
			}
			else
			{
				pImageBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				pImageBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}

			srcAccessFlags |= pImageBarrier->srcAccessMask;
			dstAccessFlags |= pImageBarrier->dstAccessMask;
		}
	}

	VkPipelineStageFlags srcStageMask =
		util_determine_pipeline_stage_flags(pCmd->renderer, srcAccessFlags, (QueueType)pCmd->vulkan.mType);
	VkPipelineStageFlags dstStageMask =
		util_determine_pipeline_stage_flags(pCmd->renderer, dstAccessFlags, (QueueType)pCmd->vulkan.mType);

	if (bufferBarrierCount || imageBarrierCount)
	{
		vkCmdPipelineBarrier(
			pCmd->vulkan.pVkCmdBuf, srcStageMask, dstStageMask, 0, 0, NULL, bufferBarrierCount, bufferBarriers, imageBarrierCount,
			imageBarriers);
	}
}

void vk_cmdUpdateBuffer(cmd_t* pCmd, buffer_t* pBuffer, uint64_t dstOffset, buffer_t* pSrcBuffer, uint64_t srcOffset, uint64_t size)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pSrcBuffer);
	TC_ASSERT(pSrcBuffer->vulkan.pVkBuffer);
	TC_ASSERT(pBuffer);
	TC_ASSERT(pBuffer->vulkan.pVkBuffer);
	TC_ASSERT(srcOffset + size <= pSrcBuffer->mSize);
	TC_ASSERT(dstOffset + size <= pBuffer->mSize);

	DECLARE_ZERO(VkBufferCopy, region);
	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size = (VkDeviceSize)size;
	vkCmdCopyBuffer(pCmd->vulkan.pVkCmdBuf, pSrcBuffer->vulkan.pVkBuffer, pBuffer->vulkan.pVkBuffer, 1, &region);
}

typedef struct SubresourceDataDesc
{
	uint64_t mSrcOffset;
	uint32_t mMipLevel;
	uint32_t mArrayLayer;
	uint32_t mRowPitch;
	uint32_t mSlicePitch;
} SubresourceDataDesc;

void vk_cmdUpdateSubresource(cmd_t* pCmd, texture_t* pTexture, buffer_t* pSrcBuffer, const SubresourceDataDesc* pSubresourceDesc)
{
	const TinyImageFormat fmt = (TinyImageFormat)pTexture->mFormat;
	const bool            isSinglePlane = TinyImageFormat_IsSinglePlane(fmt);

	if (isSinglePlane)
	{
		const uint32_t width = max<uint32_t>(1, pTexture->width >> pSubresourceDesc->mMipLevel);
		const uint32_t height = max<uint32_t>(1, pTexture->height >> pSubresourceDesc->mMipLevel);
		const uint32_t depth = max<uint32_t>(1, pTexture->mDepth >> pSubresourceDesc->mMipLevel);
		const uint32_t numBlocksWide = pSubresourceDesc->mRowPitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t numBlocksHigh = (pSubresourceDesc->mSlicePitch / pSubresourceDesc->mRowPitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = pSubresourceDesc->mSrcOffset;
		copy.bufferRowLength = numBlocksWide * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = numBlocksHigh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)pTexture->mAspectMask;
		copy.imageSubresource.mipLevel = pSubresourceDesc->mMipLevel;
		copy.imageSubresource.baseArrayLayer = pSubresourceDesc->mArrayLayer;
		copy.imageSubresource.layercount = 1;
		copy.imageOffset.x = 0;
		copy.imageOffset.y = 0;
		copy.imageOffset.z = 0;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = depth;

		vkCmdCopyBufferToImage(
			pCmd->vulkan.pVkCmdBuf, pSrcBuffer->vulkan.pVkBuffer, pTexture->vulkan.pVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copy);
	}
	else
	{
		const uint32_t width = pTexture->width;
		const uint32_t height = pTexture->height;
		const uint32_t depth = pTexture->mDepth;
		const uint32_t numOfPlanes = TinyImageFormat_NumOfPlanes(fmt);

		uint64_t          offset = pSubresourceDesc->mSrcOffset;
		VkBufferImageCopy bufferImagesCopy[MAX_PLANE_COUNT];

		for (uint32_t i = 0; i < numOfPlanes; ++i)
		{
			VkBufferImageCopy& copy = bufferImagesCopy[i];
			copy.bufferOffset = offset;
			copy.bufferRowLength = 0;
			copy.bufferImageHeight = 0;
			copy.imageSubresource.aspectMask = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);
			copy.imageSubresource.mipLevel = pSubresourceDesc->mMipLevel;
			copy.imageSubresource.baseArrayLayer = pSubresourceDesc->mArrayLayer;
			copy.imageSubresource.layercount = 1;
			copy.imageOffset.x = 0;
			copy.imageOffset.y = 0;
			copy.imageOffset.z = 0;
			copy.imageExtent.width = TinyImageFormat_PlaneWidth(fmt, i, width);
			copy.imageExtent.height = TinyImageFormat_PlaneHeight(fmt, i, height);
			copy.imageExtent.depth = depth;
			offset += copy.imageExtent.width * copy.imageExtent.height * TinyImageFormat_PlaneSizeOfBlock(fmt, i);
		}

		vkCmdCopyBufferToImage(
			pCmd->vulkan.pVkCmdBuf, pSrcBuffer->vulkan.pVkBuffer, pTexture->vulkan.pVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			numOfPlanes, bufferImagesCopy);
	}
}

void vk_cmdCopySubresource(cmd_t* pCmd, buffer_t* pDstBuffer, texture_t* pTexture, const SubresourceDataDesc* pSubresourceDesc)
{
	const TinyImageFormat fmt = (TinyImageFormat)pTexture->mFormat;
	const bool            isSinglePlane = TinyImageFormat_IsSinglePlane(fmt);

	if (isSinglePlane)
	{
		const uint32_t width = max<uint32_t>(1, pTexture->width >> pSubresourceDesc->mMipLevel);
		const uint32_t height = max<uint32_t>(1, pTexture->height >> pSubresourceDesc->mMipLevel);
		const uint32_t depth = max<uint32_t>(1, pTexture->mDepth >> pSubresourceDesc->mMipLevel);
		const uint32_t numBlocksWide = pSubresourceDesc->mRowPitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t numBlocksHigh = (pSubresourceDesc->mSlicePitch / pSubresourceDesc->mRowPitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = pSubresourceDesc->mSrcOffset;
		copy.bufferRowLength = numBlocksWide * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = numBlocksHigh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)pTexture->mAspectMask;
		copy.imageSubresource.mipLevel = pSubresourceDesc->mMipLevel;
		copy.imageSubresource.baseArrayLayer = pSubresourceDesc->mArrayLayer;
		copy.imageSubresource.layercount = 1;
		copy.imageOffset.x = 0;
		copy.imageOffset.y = 0;
		copy.imageOffset.z = 0;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = depth;

		vkCmdCopyImageToBuffer(
			pCmd->vulkan.pVkCmdBuf, pTexture->vulkan.pVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstBuffer->vulkan.pVkBuffer, 1,
			&copy);
	}
	else
	{
		const uint32_t width = pTexture->width;
		const uint32_t height = pTexture->height;
		const uint32_t depth = pTexture->mDepth;
		const uint32_t numOfPlanes = TinyImageFormat_NumOfPlanes(fmt);

		uint64_t          offset = pSubresourceDesc->mSrcOffset;
		VkBufferImageCopy bufferImagesCopy[MAX_PLANE_COUNT];

		for (uint32_t i = 0; i < numOfPlanes; ++i)
		{
			VkBufferImageCopy& copy = bufferImagesCopy[i];
			copy.bufferOffset = offset;
			copy.bufferRowLength = 0;
			copy.bufferImageHeight = 0;
			copy.imageSubresource.aspectMask = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);
			copy.imageSubresource.mipLevel = pSubresourceDesc->mMipLevel;
			copy.imageSubresource.baseArrayLayer = pSubresourceDesc->mArrayLayer;
			copy.imageSubresource.layercount = 1;
			copy.imageOffset.x = 0;
			copy.imageOffset.y = 0;
			copy.imageOffset.z = 0;
			copy.imageExtent.width = TinyImageFormat_PlaneWidth(fmt, i, width);
			copy.imageExtent.height = TinyImageFormat_PlaneHeight(fmt, i, height);
			copy.imageExtent.depth = depth;
			offset += copy.imageExtent.width * copy.imageExtent.height * TinyImageFormat_PlaneSizeOfBlock(fmt, i);
		}

		vkCmdCopyImageToBuffer(
			pCmd->vulkan.pVkCmdBuf, pTexture->vulkan.pVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstBuffer->vulkan.pVkBuffer,
			numOfPlanes, bufferImagesCopy);
	}
}
/************************************************************************/
// Queue Fence Semaphore Functions
/************************************************************************/
void vk_acquireNextImage(renderer_t* renderer, SwapChain* pSwapChain, Semaphore* pSignalSemaphore, fence_t* pFence, uint32_t* pImageIndex)
{
	TC_ASSERT(renderer);
	TC_ASSERT(VK_NULL_HANDLE != renderer->vulkan.device);
	TC_ASSERT(pSignalSemaphore || pFence);

#if defined(QUEST_VR)
    TC_ASSERT(VK_NULL_HANDLE != pSwapChain->mVR.pSwapChain);
    hook_acquire_next_image(pSwapChain, pImageIndex);
    return;
#else
    TC_ASSERT(VK_NULL_HANDLE != pSwapChain->vulkan.pSwapChain);
#endif

	VkResult vk_res = { 0 };

	if (pFence != NULL)
	{
		vk_res = vkAcquireNextImageKHR(
			renderer->vulkan.device, pSwapChain->vulkan.pSwapChain, UINT64_MAX, VK_NULL_HANDLE, pFence->vulkan.pVkFence,
			pImageIndex);

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			vkResetFences(renderer->vulkan.device, 1, &pFence->vulkan.pVkFence);
			pFence->vulkan.mSubmitted = false;
			return;
		}

		pFence->vulkan.mSubmitted = true;
	}
	else
	{
		vk_res = vkAcquireNextImageKHR(
			renderer->vulkan.device, pSwapChain->vulkan.pSwapChain, UINT64_MAX, pSignalSemaphore->vulkan.pVkSemaphore,
			VK_NULL_HANDLE, pImageIndex);    //-V522

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			pSignalSemaphore->vulkan.mSignaled = false;
			return;
		}

		// Commonly returned immediately following swapchain resize. 
		// Vulkan spec states that this return value constitutes a successful call to vkAcquireNextImageKHR
		// https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAcquireNextImageKHR.html
		if (vk_res == VK_SUBOPTIMAL_KHR)
		{
			TRACE(LOG_INFO, "vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR. If window was just resized, ignore this message."); 
			pSignalSemaphore->vulkan.mSignaled = true;
			return; 
		}

		CHECK_VKRESULT(vk_res);
		pSignalSemaphore->vulkan.mSignaled = true;
	}
}

void vk_queue_submit(queue_t* pQueue, const queuesubmitdesc_t* desc)
{
	TC_ASSERT(pQueue);
	TC_ASSERT(desc);

	uint32_t    cmdCount = desc->mCmdCount;
	cmd_t**       ppCmds = desc->ppCmds;
	fence_t*      pFence = desc->pSignalFence;
	uint32_t    waitSemaphoreCount = desc->mWaitSemaphoreCount;
	Semaphore** ppWaitSemaphores = desc->ppWaitSemaphores;
	uint32_t    signalSemaphoreCount = desc->mSignalSemaphoreCount;
	Semaphore** ppSignalSemaphores = desc->ppSignalSemaphores;

	TC_ASSERT(cmdCount > 0);
	TC_ASSERT(ppCmds);
	if (waitSemaphoreCount > 0)
	{
		TC_ASSERT(ppWaitSemaphores);
	}
	if (signalSemaphoreCount > 0)
	{
		TC_ASSERT(ppSignalSemaphores);
	}

	TC_ASSERT(VK_NULL_HANDLE != pQueue->vulkan.pVkQueue);

	VkCommandbuffer_t* cmds = (VkCommandbuffer_t*)alloca(cmdCount * sizeof(VkCommandBuffer));
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		cmds[i] = ppCmds[i]->vulkan.pVkCmdBuf;
	}

	VkSemaphore*          wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	VkPipelineStageFlags* wait_masks = (VkPipelineStageFlags*)alloca(waitSemaphoreCount * sizeof(VkPipelineStageFlags));
	uint32_t              waitCount = 0;
	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
	{
		if (ppWaitSemaphores[i]->vulkan.mSignaled)
		{
			wait_semaphores[waitCount] = ppWaitSemaphores[i]->vulkan.pVkSemaphore;    //-V522
			wait_masks[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			++waitCount;

			ppWaitSemaphores[i]->vulkan.mSignaled = false;
		}
	}

	VkSemaphore* signal_semaphores = signalSemaphoreCount ? (VkSemaphore*)alloca(signalSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	uint32_t     signalCount = 0;
	for (uint32_t i = 0; i < signalSemaphoreCount; ++i)
	{
		if (!ppSignalSemaphores[i]->vulkan.mSignaled)
		{
			signal_semaphores[signalCount] = ppSignalSemaphores[i]->vulkan.pVkSemaphore;    //-V522
			ppSignalSemaphores[i]->vulkan.mCurrentNodeIndex = pQueue->mNodeIndex;
			ppSignalSemaphores[i]->vulkan.mSignaled = true;
			++signalCount;
		}
	}

	DECLARE_ZERO(VkSubmitInfo, submit_info);
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.waitSemaphoreCount = waitCount;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_masks;
	submit_info.commandBufferCount = cmdCount;
	submit_info.pCommandBuffers = cmds;
	submit_info.signalSemaphoreCount = signalCount;
	submit_info.pSignalSemaphores = signal_semaphores;

	VkDeviceGroupSubmitInfo deviceGroupSubmitInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO_KHR };
	if (pQueue->vulkan.gpumode == GPU_MODE_LINKED)
	{
		uint32_t* pVkDeviceMasks = NULL;
		uint32_t* pSignalIndices = NULL;
		uint32_t* pWaitIndices = NULL;
		deviceGroupSubmitInfo.pNext = NULL;
		deviceGroupSubmitInfo.commandBufferCount = submit_info.commandBufferCount;
		deviceGroupSubmitInfo.signalSemaphoreCount = submit_info.signalSemaphoreCount;
		deviceGroupSubmitInfo.waitSemaphoreCount = submit_info.waitSemaphoreCount;

		pVkDeviceMasks = (uint32_t*)alloca(deviceGroupSubmitInfo.commandBufferCount * sizeof(uint32_t));
		pSignalIndices = (uint32_t*)alloca(deviceGroupSubmitInfo.signalSemaphoreCount * sizeof(uint32_t));
		pWaitIndices = (uint32_t*)alloca(deviceGroupSubmitInfo.waitSemaphoreCount * sizeof(uint32_t));

		for (uint32_t i = 0; i < deviceGroupSubmitInfo.commandBufferCount; ++i)
		{
			pVkDeviceMasks[i] = (1 << ppCmds[i]->vulkan.mNodeIndex);
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.signalSemaphoreCount; ++i)
		{
			pSignalIndices[i] = pQueue->mNodeIndex;
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.waitSemaphoreCount; ++i)
		{
			pWaitIndices[i] = ppWaitSemaphores[i]->vulkan.mCurrentNodeIndex;
		}

		deviceGroupSubmitInfo.pCommandBufferDeviceMasks = pVkDeviceMasks;
		deviceGroupSubmitInfo.pSignalSemaphoreDeviceIndices = pSignalIndices;
		deviceGroupSubmitInfo.pWaitSemaphoreDeviceIndices = pWaitIndices;
		submit_info.pNext = &deviceGroupSubmitInfo;
	}

	// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
	// Many setups have just one queue family and one queue. In this case, async compute, async transfer doesn't exist and we end up using
	// the same queue for all three operations
	lock_tLock lock(*pQueue->vulkan.pSubmitlock_t);
	CHECK_VKRESULT(vkQueueSubmit(pQueue->vulkan.pVkQueue, 1, &submit_info, pFence ? pFence->vulkan.pVkFence : VK_NULL_HANDLE));

	if (pFence)
		pFence->vulkan.mSubmitted = true;
}

void vk_queuePresent(queue_t* pQueue, const QueuePresentDesc* desc)
{
	TC_ASSERT(pQueue);
	TC_ASSERT(desc);

#if defined(QUEST_VR)
    hook_queue_present(desc);
    return;
#endif

	uint32_t    waitSemaphoreCount = desc->mWaitSemaphoreCount;
	Semaphore** ppWaitSemaphores = desc->ppWaitSemaphores;
	if (desc->pSwapChain)
	{
		SwapChain* pSwapChain = desc->pSwapChain;

		TC_ASSERT(pQueue);
		if (waitSemaphoreCount > 0)
		{
			TC_ASSERT(ppWaitSemaphores);
		}

		TC_ASSERT(VK_NULL_HANDLE != pQueue->vulkan.pVkQueue);

		VkSemaphore* wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
		uint32_t     waitCount = 0;
		for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		{
			if (ppWaitSemaphores[i]->vulkan.mSignaled)
			{
				wait_semaphores[waitCount] = ppWaitSemaphores[i]->vulkan.pVkSemaphore;    //-V522
				ppWaitSemaphores[i]->vulkan.mSignaled = false;
				++waitCount;
			}
		}

		uint32_t presentIndex = desc->mIndex;

		DECLARE_ZERO(VkPresentInfoKHR, present_info);
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.pNext = NULL;
		present_info.waitSemaphoreCount = waitCount;
		present_info.pWaitSemaphores = wait_semaphores;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &(pSwapChain->vulkan.pSwapChain);
		present_info.pImageIndices = &(presentIndex);
		present_info.pResults = NULL;

		// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
		lock_tLock lock(*pQueue->vulkan.pSubmitlock_t);
		VkResult  vk_res = vkQueuePresentKHR(
            pSwapChain->vulkan.pPresentQueue ? pSwapChain->vulkan.pPresentQueue : pQueue->vulkan.pVkQueue, &present_info);

		if (vk_res == VK_ERROR_DEVICE_LOST)
		{
			// Will crash normally on Android.
#if defined(_WINDOWS)
			threadSleep(5000);    // Wait for a few seconds to allow the driver to come back online before doing a reset.
			ResetDesc resetDesc;
			resetDesc.mType = RESET_TYPE_DEVICE_LOST;
			requestReset(&resetDesc);
#endif
		}
		else if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// TODO : Fix bug where we get this error if window is closed before able to present queue.
		}
		else if (vk_res != VK_SUCCESS && vk_res != VK_SUBOPTIMAL_KHR)
		{
			TC_ASSERT(0);
		}
	}
}

void vk_wait_for_fences(renderer_t* renderer, uint32_t fenceCount, fence_t** ppFences)
{
	TC_ASSERT(renderer);
	TC_ASSERT(fenceCount);
	TC_ASSERT(ppFences);

	Vkfence_t* fences = (Vkfence_t*)alloca(fenceCount * sizeof(VkFence));
	uint32_t numValidFences = 0;
	for (uint32_t i = 0; i < fenceCount; ++i)
	{
		if (ppFences[i]->vulkan.mSubmitted)
			fences[numValidFences++] = ppFences[i]->vulkan.pVkFence;
	}

	if (numValidFences)
	{
		CHECK_VKRESULT(vkWaitForFences(renderer->vulkan.device, numValidFences, fences, VK_TRUE, UINT64_MAX));
		CHECK_VKRESULT(vkResetFences(renderer->vulkan.device, numValidFences, fences));
	}

	for (uint32_t i = 0; i < fenceCount; ++i)
		ppFences[i]->vulkan.mSubmitted = false;
}

void vk_waitQueueIdle(queue_t* pQueue) { vkQueueWaitIdle(pQueue->vulkan.pVkQueue); }

void vk_getFenceStatus(renderer_t* renderer, fence_t* pFence, FenceStatus* pFenceStatus)
{
	*pFenceStatus = FENCE_STATUS_COMPLETE;

	if (pFence->vulkan.mSubmitted)
	{
		VkResult vkRes = vkGetFenceStatus(renderer->vulkan.device, pFence->vulkan.pVkFence);
		if (vkRes == VK_SUCCESS)
		{
			vkResetFences(renderer->vulkan.device, 1, &pFence->vulkan.pVkFence);
			pFence->vulkan.mSubmitted = false;
		}

		*pFenceStatus = vkRes == VK_SUCCESS ? FENCE_STATUS_COMPLETE : FENCE_STATUS_INCOMPLETE;
	}
	else
	{
		*pFenceStatus = FENCE_STATUS_NOTSUBMITTED;
	}
}
/************************************************************************/
// Utility functions
/************************************************************************/
TinyImageFormat vk_getRecommendedSwapchainFormat(bool hintHDR, bool hintSRGB)
{
	//TODO: figure out this properly. BGRA not supported on android
#if !defined(VK_USE_PLATFORM_ANDROID_KHR) && !defined(VK_USE_PLATFORM_VI_NN)
	if (hintSRGB)
		return TinyImageFormat_B8G8R8A8_SRGB;
	else
		return TinyImageFormat_B8G8R8A8_UNORM;
#else
	if (hintSRGB)
		return TinyImageFormat_R8G8B8A8_SRGB;
	else
		return TinyImageFormat_R8G8B8A8_UNORM;
#endif
}

/************************************************************************/
// Indirect draw functions
/************************************************************************/
void vk_addIndirectCommandSignature(renderer_t* renderer, const CommandSignatureDesc* desc, CommandSignature** ppCommandSignature)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(desc->mIndirectArgCount == 1);
	
	CommandSignature* pCommandSignature =
		(CommandSignature*)tc_calloc(1, sizeof(CommandSignature) + sizeof(IndirectArgument) * desc->mIndirectArgCount);
	TC_ASSERT(pCommandSignature);
	
	pCommandSignature->mDrawType = desc->pArgDescs[0].mType;
	switch (desc->pArgDescs[0].mType)
	{
		case INDIRECT_DRAW:
			pCommandSignature->mStride += sizeof(IndirectDrawArguments);
			break;
		case INDIRECT_DRAW_INDEX:
			pCommandSignature->mStride += sizeof(IndirectDrawIndexArguments);
			break;
		case INDIRECT_DISPATCH:
			pCommandSignature->mStride += sizeof(IndirectDispatchArguments);
			break;
		default:
			TC_ASSERT(false);
			break;
	}

	if (!desc->mPacked)
	{
		pCommandSignature->mStride = round_up(pCommandSignature->mStride, 16);
	}

	*ppCommandSignature = pCommandSignature;
}

void vk_removeIndirectCommandSignature(renderer_t* renderer, CommandSignature* pCommandSignature)
{
	TC_ASSERT(renderer);
	tc_free(pCommandSignature);
}

void vk_cmdExecuteIndirect(
	cmd_t* pCmd, CommandSignature* pCommandSignature, uint maxCommandCount, buffer_t* pIndirectBuffer, uint64_t bufferOffset,
	buffer_t* pCounterBuffer, uint64_t counterBufferOffset)
{
	PFN_vkCmdDrawIndirect drawIndirect = (pCommandSignature->mDrawType == INDIRECT_DRAW) ? vkCmdDrawIndirect : vkCmdDrawIndexedIndirect;
#ifndef NX64
	decltype(pfnVkCmdDrawIndirectCountKHR) drawIndirectCount = (pCommandSignature->mDrawType == INDIRECT_DRAW) ? pfnVkCmdDrawIndirectCountKHR : pfnVkCmdDrawIndexedIndirectCountKHR;
#endif

	if (pCommandSignature->mDrawType == INDIRECT_DRAW || pCommandSignature->mDrawType == INDIRECT_DRAW_INDEX)
	{
		if (pCmd->renderer->activegpusettings->mMultiDrawIndirect)
		{
#ifndef NX64
			if (pCounterBuffer && drawIndirectCount)
			{
				drawIndirectCount(
					pCmd->vulkan.pVkCmdBuf, pIndirectBuffer->vulkan.pVkBuffer, bufferOffset, pCounterBuffer->vulkan.pVkBuffer,
					counterBufferOffset, maxCommandCount, pCommandSignature->mStride);
			}
			else
#endif
			{
				drawIndirect(
					pCmd->vulkan.pVkCmdBuf, pIndirectBuffer->vulkan.pVkBuffer, bufferOffset, maxCommandCount, pCommandSignature->mStride);
			}
		}
		else
		{
			// Cannot use counter buffer when MDI is not supported. We will blindly loop through maxCommandCount
			TC_ASSERT(!pCounterBuffer);

			for (uint32_t cmd = 0; cmd < maxCommandCount; ++cmd)
			{
				drawIndirect(
					pCmd->vulkan.pVkCmdBuf, pIndirectBuffer->vulkan.pVkBuffer, bufferOffset + cmd * pCommandSignature->mStride, 1, pCommandSignature->mStride);
			}
		}
	}
	else if (pCommandSignature->mDrawType == INDIRECT_DISPATCH)
	{
		for (uint32_t i = 0; i < maxCommandCount; ++i)
		{
			vkCmdDispatchIndirect(pCmd->vulkan.pVkCmdBuf, pIndirectBuffer->vulkan.pVkBuffer, bufferOffset + i * pCommandSignature->mStride);
		}
	}
}
/************************************************************************/
// Query Heap Implementation
/************************************************************************/
VkQueryType util_to_vk_query_type(QueryType type)
{
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP: return VK_QUERY_TYPE_TIMESTAMP;
		case QUERY_TYPE_PIPELINE_STATISTICS: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
		case QUERY_TYPE_OCCLUSION: return VK_QUERY_TYPE_OCCLUSION;
		default: TC_ASSERT(false && "Invalid query heap type"); return VK_QUERY_TYPE_MAX_ENUM;
	}
}

void vk_getTimestampFrequency(queue_t* pQueue, double* pFrequency)
{
	TC_ASSERT(pQueue);
	TC_ASSERT(pFrequency);

	// The framework is using ticks per sec as frequency. Vulkan is nano sec per tick.
	// Handle the conversion logic here.
	*pFrequency =
		1.0f /
		((double)pQueue->vulkan.mTimestampPeriod /*ns/tick number of nanoseconds required for a timestamp query to be incremented by 1*/
		 * 1e-9);                                 // convert to ticks/sec (DX12 standard)
}

void vk_addQueryPool(renderer_t* renderer, const QueryPoolDesc* desc, QueryPool** ppQueryPool)
{
	TC_ASSERT(renderer);
	TC_ASSERT(desc);
	TC_ASSERT(ppQueryPool);

	QueryPool* pQueryPool = (QueryPool*)tc_calloc(1, sizeof(QueryPool));
	TC_ASSERT(ppQueryPool);

	pQueryPool->vulkan.mType = util_to_vk_query_type(desc->mType);
	pQueryPool->mCount = desc->mQueryCount;

	VkQueryPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.pNext = NULL;
	info.queryCount = desc->mQueryCount;
	info.queryType = util_to_vk_query_type(desc->mType);
	info.flags = 0;
	info.pipelineStatistics = 0;
	CHECK_VKRESULT(
		vkCreateQueryPool(renderer->vulkan.device, &info, &alloccbs, &pQueryPool->vulkan.pVkQueryPool));

	*ppQueryPool = pQueryPool;
}

void vk_removeQueryPool(renderer_t* renderer, QueryPool* pQueryPool)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pQueryPool);
	vkDestroyQueryPool(renderer->vulkan.device, pQueryPool->vulkan.pVkQueryPool, &alloccbs);

	tc_free(pQueryPool);
}

void vk_cmdResetQueryPool(cmd_t* pCmd, QueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount)
{
	vkCmdResetQueryPool(pCmd->vulkan.pVkCmdBuf, pQueryPool->vulkan.pVkQueryPool, startQuery, queryCount);
}

void vk_cmdBeginQuery(cmd_t* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	VkQueryType type = pQueryPool->vulkan.mType;
	switch (type)
	{
		case VK_QUERY_TYPE_TIMESTAMP:
			vkCmdWriteTimestamp(
				pCmd->vulkan.pVkCmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pQueryPool->vulkan.pVkQueryPool, pQuery->mIndex);
			break;
		case VK_QUERY_TYPE_PIPELINE_STATISTICS: break;
		case VK_QUERY_TYPE_OCCLUSION: break;
		default: break;
	}
}

void vk_cmdEndQuery(cmd_t* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	VkQueryType type = pQueryPool->vulkan.mType;
	switch (type)
	{
	case VK_QUERY_TYPE_TIMESTAMP:
		vkCmdWriteTimestamp(
			pCmd->vulkan.pVkCmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pQueryPool->vulkan.pVkQueryPool, pQuery->mIndex);
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS: break;
	case VK_QUERY_TYPE_OCCLUSION: break;
	default: break;
	}
}

void vk_cmdResolveQuery(cmd_t* pCmd, QueryPool* pQueryPool, buffer_t* pReadbackBuffer, uint32_t startQuery, uint32_t queryCount)
{
	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
	vkCmdCopyQueryPoolResults(
		pCmd->vulkan.pVkCmdBuf, pQueryPool->vulkan.pVkQueryPool, startQuery, queryCount, pReadbackBuffer->vulkan.pVkBuffer, 0,
		sizeof(uint64_t), flags);
}
/************************************************************************/
// Memory Stats Implementation
/************************************************************************/
void vk_calculateMemoryStats(renderer_t* renderer, char** stats)
{
	vmaBuildStatsString(renderer->vulkan.pVmaAllocator, stats, VK_TRUE);
}

void vk_freeMemoryStats(renderer_t* renderer, char* stats)
{
	vmaFreeStatsString(renderer->vulkan.pVmaAllocator, stats);
}

void vk_calculateMemoryUse(renderer_t* renderer, uint64_t* usedBytes, uint64_t* totalAllocatedBytes)
{
	VmaTotalStatistics stats = { 0 };
	vmaCalculateStatistics(renderer->vulkan.pVmaAllocator, &stats);
	*usedBytes = stats.total.statistics.allocationBytes;
	*totalAllocatedBytes = stats.total.statistics.blockBytes;
}
/************************************************************************/
// Debug Marker Implementation
/************************************************************************/
void vk_cmdBeginDebugMarker(cmd_t* pCmd, float r, float g, float b, const char* pName)
{
	if (pCmd->renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdBeginDebugUtilsLabelEXT(pCmd->vulkan.pVkCmdBuf, &markerInfo);
#elif !defined(NX64) || !defined(ENABLE_RENDER_DOC)
		VkDebugMarkerMarkerInfoEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerBeginEXT(pCmd->vulkan.pVkCmdBuf, &markerInfo);
#endif
	}
}

void vk_cmdEndDebugMarker(cmd_t* pCmd)
{
	if (pCmd->renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		vkCmdEndDebugUtilsLabelEXT(pCmd->vulkan.pVkCmdBuf);
#elif !defined(NX64) || !defined(ENABLE_RENDER_DOC)
		vkCmdDebugMarkerEndEXT(pCmd->vulkan.pVkCmdBuf);
#endif
	}
}

void vk_cmdAddDebugMarker(cmd_t* pCmd, float r, float g, float b, const char* pName)
{
	if (pCmd->renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdInsertDebugUtilsLabelEXT(pCmd->vulkan.pVkCmdBuf, &markerInfo);
#else
		VkDebugMarkerMarkerInfoEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerInsertEXT(pCmd->vulkan.pVkCmdBuf, &markerInfo);
#endif
	}
}

uint32_t vk_cmdWriteMarker(cmd_t* pCmd, MarkerType markerType, uint32_t markerValue, buffer_t* pBuffer, size_t offset, bool useAutoFlags)
{
	return 0;
}
/************************************************************************/
// Resource Debug Naming Interface
/************************************************************************/
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
void util_set_object_name(VkDevice pDevice, uint64_t handle, VkObjectType type, const char* pName)
{
#if defined(ENABLE_GRAPHICS_DEBUG)
	VkDebugUtilsObjectNameInfoEXT nameInfo = { 0 };
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objecttype = type;
	nameInfo.objectHandle = handle;
	nameInfo.pObjectName = pName;
	vkSetDebugUtilsObjectNameEXT(pDevice, &nameInfo);
#endif
}
#else
void util_set_object_name(VkDevice pDevice, uint64_t handle, VkDebugReportObjectTypeEXT type, const char* pName)
{
#if defined(ENABLE_GRAPHICS_DEBUG)
	VkDebugMarkerObjectNameInfoEXT nameInfo = { 0 };
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
	nameInfo.objecttype = type;
	nameInfo.object = (uint64_t)handle;
	nameInfo.pObjectName = pName;
	vkDebugMarkerSetObjectNameEXT(pDevice, &nameInfo);
#endif
}
#endif

void vk_setBufferName(renderer_t* renderer, buffer_t* pBuffer, const char* pName)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pBuffer);
	TC_ASSERT(pName);

	if (renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(renderer->vulkan.device, (uint64_t)pBuffer->vulkan.pVkBuffer, VK_OBJECT_TYPE_BUFFER, pName);
#else
		util_set_object_name(renderer->vulkan.device, (uint64_t)pBuffer->vulkan.pVkBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, pName);
#endif
	}
}

void vk_setTextureName(renderer_t* renderer, texture_t* pTexture, const char* pName)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pTexture);
	TC_ASSERT(pName);

	if (renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(renderer->vulkan.device, (uint64_t)pTexture->vulkan.pVkImage, VK_OBJECT_TYPE_IMAGE, pName);
#else
		util_set_object_name(renderer->vulkan.device, (uint64_t)pTexture->vulkan.pVkImage, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, pName);
#endif
	}
}

void vk_setRenderTargetName(renderer_t* renderer, RenderTarget* pRenderTarget, const char* pName)
{
	setTextureName(renderer, pRenderTarget->pTexture, pName);
}

void vk_setPipelineName(renderer_t* renderer, pipeline_t* pipeline, const char* pName)
{
	TC_ASSERT(renderer);
	TC_ASSERT(pipeline);
	TC_ASSERT(pName);

	if (renderer->vulkan.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(renderer->vulkan.device, (uint64_t)pipeline->vulkan.pVkPipeline, VK_OBJECT_TYPE_PIPELINE, pName);
#else
		util_set_object_name(
			renderer->vulkan.device, (uint64_t)pipeline->vulkan.pVkPipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, pName);
#endif
	}
}
/************************************************************************/
// Virtual Texture
/************************************************************************/
static void alignedDivision(const VkExtent3D& extent, const VkExtent3D& granularity, VkExtent3D* out)
{
	out->width = (extent.width / granularity.width + ((extent.width % granularity.width) ? 1u : 0u));
	out->height = (extent.height / granularity.height + ((extent.height % granularity.height) ? 1u : 0u));
	out->depth = (extent.depth / granularity.depth + ((extent.depth % granularity.depth) ? 1u : 0u));
}

struct VkVTPendingPageDeletion
{
	VmaAllocation* pAllocations;
	uint32_t* pAllocationsCount;

	buffer_t** pIntermediateBuffers;
	uint32_t* pIntermediateBuffersCount;
};

// Allocate Vulkan memory for the virtual page
static bool allocateVirtualPage(renderer_t* renderer, texture_t* pTexture, VirtualTexturePage& virtualPage, buffer_t** ppIntermediateBuffer)
{
	if (virtualPage.vulkan.imageMemoryBind.memory != VK_NULL_HANDLE)
	{
		//already filled
		return false;
	};

	BufferDesc desc = { 0 };
	desc.descriptors = DESCRIPTOR_TYPE_RW_BUFFER;
	desc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_ONLY;

	desc.mFirstElement = 0;
	desc.mElementCount = pTexture->pSvt->mSparseVirtualTexturePageWidth * pTexture->pSvt->mSparseVirtualTexturePageHeight;
	desc.mStructStride = sizeof(uint32_t);
	desc.mSize = desc.mElementCount * desc.mStructStride;
#if defined(ENABLE_GRAPHICS_DEBUG)
	char debugNameBuffer[MAX_DEBUG_NAME_LENGTH]{ 0 };
	snprintf(debugNameBuffer, MAX_DEBUG_NAME_LENGTH, "(tex %p) VT page #%u intermediate buffer", pTexture, virtualPage.index);
	desc.pName = debugNameBuffer;
#endif
	add_buffer(renderer, &desc, ppIntermediateBuffer);

	VkMemoryRequirements memReqs = { 0 };
	memReqs.size = virtualPage.vulkan.size;
	memReqs.memoryTypeBits = pTexture->pSvt->vulkan.mSparseMemoryTypeBits;
	memReqs.alignment = memReqs.size;

	VmaAllocationCreateInfo vmaAllocInfo = { 0 };
	vmaAllocInfo.pool = (VmaPool)pTexture->pSvt->vulkan.pPool;

	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	CHECK_VKRESULT(vmaAllocateMemory(renderer->vulkan.pVmaAllocator, &memReqs, &vmaAllocInfo, &allocation, &allocationInfo));
	TC_ASSERT(allocation->GetAlignment() == memReqs.size || allocation->GetAlignment() == 0);

	virtualPage.vulkan.pAllocation = allocation;
	virtualPage.vulkan.imageMemoryBind.memory = allocation->GetMemory();
	virtualPage.vulkan.imageMemoryBind.memoryOffset = allocation->GetOffset();

	// Sparse image memory binding
	virtualPage.vulkan.imageMemoryBind.subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	virtualPage.vulkan.imageMemoryBind.subresource.mipLevel = virtualPage.mipLevel;
	virtualPage.vulkan.imageMemoryBind.subresource.arrayLayer = virtualPage.layer;

	++pTexture->pSvt->mVirtualPageAliveCount;

	return true;
}

VirtualTexturePage* addPage(renderer_t* renderer, texture_t* pTexture, const VkOffset3D& offset, const VkExtent3D& extent, const VkDeviceSize size, const uint32_t mipLevel, uint32_t layer, uint32_t pageIndex)
{
	VirtualTexturePage& newPage = pTexture->pSvt->pPages[pageIndex];

	newPage.vulkan.imageMemoryBind.offset = offset;
	newPage.vulkan.imageMemoryBind.extent = extent;
	newPage.vulkan.size = size;
	newPage.mipLevel = mipLevel;
	newPage.layer = layer;
	newPage.index = pageIndex;

	return &newPage;
}

struct VTReadbackBufOffsets
{
	uint* pAlivePageCount;
	uint* pRemovePageCount;
	uint* pAlivePages;
	uint* pRemovePages;
	uint mTotalSize;
};
static VTReadbackBufOffsets vtGetReadbackBufOffsets(uint32_t* buffer, uint32_t readbackBufSize, uint32_t pageCount, uint32_t currentImage)
{
	TC_ASSERT(!!buffer == !!readbackBufSize);  // If you already know the readback buf size, why is buffer null?

	VTReadbackBufOffsets offsets;
	offsets.pAlivePageCount = buffer + ((readbackBufSize / sizeof(uint32_t)) * currentImage);
	offsets.pRemovePageCount = offsets.pAlivePageCount + 1;
	offsets.pAlivePages = offsets.pRemovePageCount + 1;
	offsets.pRemovePages = offsets.pAlivePages + pageCount;

	offsets.mTotalSize = (uint)((offsets.pRemovePages - offsets.pAlivePageCount) + pageCount) * sizeof(uint);
	return offsets;
}
static uint32_t vtGetReadbackBufSize(uint32_t pageCount, uint32_t numimages)
{
	VTReadbackBufOffsets offsets = vtGetReadbackBufOffsets(NULL, 0, pageCount, 0);
	return offsets.mTotalSize;
}
static VkVTPendingPageDeletion vtGetPendingPageDeletion(Virtualtexture_t* pSvt, uint32_t currentImage)
{
	if (pSvt->mPendingDeletionCount <= currentImage)
	{
		// Grow arrays
		const uint32_t oldDeletionCount = pSvt->mPendingDeletionCount;
		pSvt->mPendingDeletionCount = currentImage + 1;
		pSvt->vulkan.pPendingDeletedAllocations = (void**)tf_realloc(pSvt->vulkan.pPendingDeletedAllocations,
			pSvt->mPendingDeletionCount * pSvt->mVirtualPageTotalCount * sizeof(pSvt->vulkan.pPendingDeletedAllocations[0]));

		pSvt->pPendingDeletedAllocationsCount = (uint32_t*)tf_realloc(pSvt->pPendingDeletedAllocationsCount,
			pSvt->mPendingDeletionCount * sizeof(pSvt->pPendingDeletedAllocationsCount[0]));

		pSvt->pPendingDeletedBuffers = (buffer_t**)tf_realloc(pSvt->pPendingDeletedBuffers,
			pSvt->mPendingDeletionCount * pSvt->mVirtualPageTotalCount * sizeof(pSvt->pPendingDeletedBuffers[0]));

		pSvt->pPendingDeletedBuffersCount = (uint32_t*)tf_realloc(pSvt->pPendingDeletedBuffersCount,
			pSvt->mPendingDeletionCount * sizeof(pSvt->pPendingDeletedBuffersCount[0]));

		// Zero the new counts
		for (uint32_t i = oldDeletionCount; i < pSvt->mPendingDeletionCount; i++)
		{
			pSvt->pPendingDeletedAllocationsCount[i] = 0;
			pSvt->pPendingDeletedBuffersCount[i] = 0;
		}
	}

	VkVTPendingPageDeletion pendingDeletion;
	pendingDeletion.pAllocations = (VmaAllocation*)&pSvt->vulkan.pPendingDeletedAllocations[currentImage * pSvt->mVirtualPageTotalCount];
	pendingDeletion.pAllocationsCount = &pSvt->pPendingDeletedAllocationsCount[currentImage];
	pendingDeletion.pIntermediateBuffers = &pSvt->pPendingDeletedBuffers[currentImage * pSvt->mVirtualPageTotalCount];
	pendingDeletion.pIntermediateBuffersCount = &pSvt->pPendingDeletedBuffersCount[currentImage];
	return pendingDeletion;
}

void vk_updateVirtualTextureMemory(cmd_t* pCmd, texture_t* pTexture, uint32_t imageMemoryCount)
{
	// Update sparse bind info
	if (imageMemoryCount > 0)
	{
		VkBindSparseInfo bindSparseInfo = { 0 };
		bindSparseInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;

		// Image memory binds
		VkSparseImageMemoryBindInfo imageMemoryBindInfo = { 0 };
		imageMemoryBindInfo.image = pTexture->vulkan.pVkImage;
		imageMemoryBindInfo.bindCount = imageMemoryCount;
		imageMemoryBindInfo.pBinds = pTexture->pSvt->vulkan.pSparseImageMemoryBinds;
		bindSparseInfo.imageBindCount = 1;
		bindSparseInfo.pImageBinds = &imageMemoryBindInfo;

		// Opaque image memory binds (mip tail)
		VkSparseImageOpaqueMemoryBindInfo opaqueMemoryBindInfo = { 0 };
		opaqueMemoryBindInfo.image = pTexture->vulkan.pVkImage;
		opaqueMemoryBindInfo.bindCount = pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount;
		opaqueMemoryBindInfo.pBinds = pTexture->pSvt->vulkan.pOpaqueMemoryBinds;
		bindSparseInfo.imageOpaqueBindCount = (opaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		bindSparseInfo.pImageOpaqueBinds = &opaqueMemoryBindInfo;

		CHECK_VKRESULT(vkQueueBindSparse(pCmd->pQueue->vulkan.pVkQueue, (uint32_t)1, &bindSparseInfo, VK_NULL_HANDLE));
	}
}

void vk_releasePage(cmd_t* pCmd, texture_t* pTexture, uint32_t currentImage)
{
	renderer_t* renderer = pCmd->renderer;

	VirtualTexturePage* pPageTable = pTexture->pSvt->pPages;

	VTReadbackBufOffsets offsets = vtGetReadbackBufOffsets(
		(uint32_t*)pTexture->pSvt->pReadbackBuffer->pCpuMappedAddress,
		pTexture->pSvt->mReadbackBufferSize,
		pTexture->pSvt->mVirtualPageTotalCount,
		currentImage);

	const uint removePageCount = *offsets.pRemovePageCount;
	const uint32_t* RemovePageTable = offsets.pRemovePages;

	const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(pTexture->pSvt, currentImage);

	// Release pending intermediate buffers
	{
		for (size_t i = 0; i < *pendingDeletion.pIntermediateBuffersCount; i++)
			remove_buffer(renderer, pendingDeletion.pIntermediateBuffers[i]);

		for (size_t i = 0; i < *pendingDeletion.pAllocationsCount; i++)
			vmaFreeMemory(renderer->vulkan.pVmaAllocator, pendingDeletion.pAllocations[i]);

		*pendingDeletion.pIntermediateBuffersCount = 0;
		*pendingDeletion.pAllocationsCount = 0;
	}

	// Schedule release of newly unneeded pages
	uint pageUnbindCount = 0;
	for (uint removePageIndex = 0; removePageIndex < removePageCount; ++removePageIndex)
	{
		uint32_t RemoveIndex = RemovePageTable[removePageIndex];
		VirtualTexturePage& removePage = pPageTable[RemoveIndex];

		// Never remove the lowest mip level
		if ((int)removePage.mipLevel >= (pTexture->pSvt->mTiledMipLevelCount - 1))
			continue;

		TC_ASSERT(!!removePage.vulkan.pAllocation == !!removePage.vulkan.imageMemoryBind.memory);
		if (removePage.vulkan.pAllocation)
		{
			TC_ASSERT(((VmaAllocation)removePage.vulkan.pAllocation)->GetMemory() == removePage.vulkan.imageMemoryBind.memory);
			pendingDeletion.pAllocations[(*pendingDeletion.pAllocationsCount)++] = (VmaAllocation)removePage.vulkan.pAllocation;
			removePage.vulkan.pAllocation = VK_NULL_HANDLE;
			removePage.vulkan.imageMemoryBind.memory = VK_NULL_HANDLE;

			VkSparseImageMemoryBind& unbind = pTexture->pSvt->vulkan.pSparseImageMemoryBinds[pageUnbindCount++];
			unbind = { 0 };
			unbind.offset = removePage.vulkan.imageMemoryBind.offset;
			unbind.extent = removePage.vulkan.imageMemoryBind.extent;
			unbind.subresource = removePage.vulkan.imageMemoryBind.subresource;

			--pTexture->pSvt->mVirtualPageAliveCount;
		}
	}

	// Unmap tiles
	vk_updateVirtualTextureMemory(pCmd, pTexture, pageUnbindCount);
}

void vk_uploadVirtualTexturePage(cmd_t* pCmd, texture_t* pTexture, VirtualTexturePage* pPage, uint32_t* imageMemoryCount, uint32_t currentImage)
{
	buffer_t* pIntermediateBuffer = NULL;
	if (allocateVirtualPage(pCmd->renderer, pTexture, *pPage, &pIntermediateBuffer))
	{
		void* pData = (void*)((unsigned char*)pTexture->pSvt->pVirtualImageData + (pPage->index * pPage->vulkan.size));

		const bool intermediateMap = !pIntermediateBuffer->pCpuMappedAddress;
		if (intermediateMap)
		{
			mapBuffer(pCmd->renderer, pIntermediateBuffer, NULL);
		}

		//CPU to GPU
		memcpy(pIntermediateBuffer->pCpuMappedAddress, pData, pPage->vulkan.size);

		if (intermediateMap)
		{
			unmapBuffer(pCmd->renderer, pIntermediateBuffer);
		}

		//Copy image to VkImage
		VkBufferImageCopy region = { 0 };
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.mipLevel = pPage->mipLevel;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layercount = 1;

		region.imageOffset = pPage->vulkan.imageMemoryBind.offset;
		region.imageOffset.z = 0;
		region.imageExtent = { (uint32_t)pTexture->pSvt->mSparseVirtualTexturePageWidth, (uint32_t)pTexture->pSvt->mSparseVirtualTexturePageHeight, 1 };

		vkCmdCopyBufferToImage(
			pCmd->vulkan.pVkCmdBuf,
			pIntermediateBuffer->vulkan.pVkBuffer,
			pTexture->vulkan.pVkImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region);

		// Update list of memory-backed sparse image memory binds
		pTexture->pSvt->vulkan.pSparseImageMemoryBinds[(*imageMemoryCount)++] = pPage->vulkan.imageMemoryBind;

		// Schedule deletion of this intermediate buffer
		const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(pTexture->pSvt, currentImage);
		pendingDeletion.pIntermediateBuffers[(*pendingDeletion.pIntermediateBuffersCount)++] = pIntermediateBuffer;
	}
}

// Fill a complete mip level
// Need to get visibility info first then fill them
void vk_fillVirtualTexture(cmd_t* pCmd, texture_t* pTexture, fence_t* pFence, uint32_t currentImage)
{
	uint32_t imageMemoryCount = 0;

	VTReadbackBufOffsets readbackOffsets = vtGetReadbackBufOffsets(
		(uint*)pTexture->pSvt->pReadbackBuffer->pCpuMappedAddress,
		pTexture->pSvt->mReadbackBufferSize,
		pTexture->pSvt->mVirtualPageTotalCount,
		currentImage);

	const uint alivePageCount = *readbackOffsets.pAlivePageCount;
	uint32_t* VisibilityData = readbackOffsets.pAlivePages;

	for (int i = 0; i < (int)alivePageCount; ++i)
	{
		uint pageIndex = VisibilityData[i];
		TC_ASSERT(pageIndex < pTexture->pSvt->mVirtualPageTotalCount);
		VirtualTexturePage* pPage = &pTexture->pSvt->pPages[pageIndex];
		TC_ASSERT(pageIndex == pPage->index);

		vk_uploadVirtualTexturePage(pCmd, pTexture, pPage, &imageMemoryCount, currentImage);
	}

	vk_updateVirtualTextureMemory(pCmd, pTexture, imageMemoryCount);
}

// Fill specific mipLevel
void vk_fillVirtualTextureLevel(cmd_t* pCmd, texture_t* pTexture, uint32_t mipLevel, uint32_t currentImage)
{
	//Bind data
	uint32_t imageMemoryCount = 0;

	for (uint32_t i = 0; i < pTexture->pSvt->mVirtualPageTotalCount; i++)
	{
		VirtualTexturePage* pPage = &pTexture->pSvt->pPages[i];
		TC_ASSERT(pPage->index == i);

		if (pPage->mipLevel == mipLevel)
		{
			vk_uploadVirtualTexturePage(pCmd, pTexture, pPage, &imageMemoryCount, currentImage);
		}
	}

	vk_updateVirtualTextureMemory(pCmd, pTexture, imageMemoryCount);
}

void vk_addVirtualTexture(cmd_t* pCmd, const texturedesc_t* desc, texture_t** ppTexture, void* pImageData)
{
	TC_ASSERT(pCmd);
	texture_t* pTexture = (texture_t*)tc_calloc_memalign(1, alignof(Texture), sizeof(*pTexture) + sizeof(VirtualTexture));
	TC_ASSERT(pTexture);

	renderer_t* renderer = pCmd->renderer;

	pTexture->pSvt = (Virtualtexture_t*)(pTexture + 1);

	uint32_t imageSize = 0;
	uint32_t mipSize = desc->width * desc->height * desc->mDepth;
	while (mipSize > 0)
	{
		imageSize += mipSize;
		mipSize /= 4;
	}

	pTexture->pSvt->pVirtualImageData = pImageData;
	pTexture->mFormat = desc->mFormat;
	TC_ASSERT(pTexture->mFormat == desc->mFormat);

	VkFormat format = (VkFormat)TinyImageFormat_ToVkFormat((TinyImageFormat)pTexture->mFormat);
	TC_ASSERT(VK_FORMAT_UNDEFINED != format);
	pTexture->mOwnsImage = true;

	VkImageCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = desc->width;
	info.extent.height = desc->height;
	info.extent.depth = desc->mDepth;
	info.mipLevels = desc->mMipLevels;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	CHECK_VKRESULT(vkCreateImage(renderer->vulkan.device, &info, &alloccbs, &pTexture->vulkan.pVkImage));

	// Get memory requirements
	VkMemoryRequirements sparseImageMemoryReqs;
	// Sparse image memory requirement counts
	vkGetImageMemoryRequirements(renderer->vulkan.device, pTexture->vulkan.pVkImage, &sparseImageMemoryReqs);

	// Check requested image size against hardware sparse limit
	if (sparseImageMemoryReqs.size > renderer->vulkan.activegpuprops->properties.limits.sparseAddressSpaceSize)
	{
		TRACE(LOG_ERROR, "Requested sparse image size exceeds supported sparse address space size!");
		return;
	}

	// Get sparse memory requirements
	// Count
	uint32_t sparseMemoryReqsCount;
	vkGetImageSparseMemoryRequirements(renderer->vulkan.device, pTexture->vulkan.pVkImage, &sparseMemoryReqsCount, NULL);  // Get count
	VkSparseImageMemoryRequirements* sparseMemoryReqs = NULL;

	if (sparseMemoryReqsCount == 0)
	{
		TRACE(LOG_ERROR, "No memory requirements for the sparse image!");
		return;
	}
	else
	{
		sparseMemoryReqs = (VkSparseImageMemoryRequirements*)tc_calloc(sparseMemoryReqsCount, sizeof(VkSparseImageMemoryRequirements));
		vkGetImageSparseMemoryRequirements(renderer->vulkan.device, pTexture->vulkan.pVkImage, &sparseMemoryReqsCount, sparseMemoryReqs);  // Get reqs
	}

	TC_ASSERT(sparseMemoryReqsCount == 1 && "Multiple sparse image memory requirements not currently implemented");

	pTexture->pSvt->mSparseVirtualTexturePageWidth = sparseMemoryReqs[0].formatProperties.imageGranularity.width;
	pTexture->pSvt->mSparseVirtualTexturePageHeight = sparseMemoryReqs[0].formatProperties.imageGranularity.height;
	pTexture->pSvt->mVirtualPageTotalCount = imageSize / (uint32_t)(pTexture->pSvt->mSparseVirtualTexturePageWidth * pTexture->pSvt->mSparseVirtualTexturePageHeight);
	pTexture->pSvt->mReadbackBufferSize = vtGetReadbackBufSize(pTexture->pSvt->mVirtualPageTotalCount, 1);
	pTexture->pSvt->mPageVisibilityBufferSize = pTexture->pSvt->mVirtualPageTotalCount * 2 * sizeof(uint);

	uint32_t TiledMiplevel = desc->mMipLevels - (uint32_t)log2(min((uint32_t)pTexture->pSvt->mSparseVirtualTexturePageWidth, (uint32_t)pTexture->pSvt->mSparseVirtualTexturePageHeight));
	pTexture->pSvt->mTiledMipLevelCount = (uint8_t)TiledMiplevel;

	TRACE(LOG_INFO, "Sparse image memory requirements: %d", sparseMemoryReqsCount);

	// Get sparse image requirements for the color aspect
	VkSparseImageMemoryRequirements sparseMemoryReq = { 0 };
	bool colorAspectFound = false;
	for (int i = 0; i < (int)sparseMemoryReqsCount; ++i)
	{
		VkSparseImageMemoryRequirements reqs = sparseMemoryReqs[i];

		if (reqs.formatProperties.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			sparseMemoryReq = reqs;
			colorAspectFound = true;
			break;
		}
	}

	tc_free(sparseMemoryReqs);
	sparseMemoryReqs = NULL;

	if (!colorAspectFound)
	{
		TRACE(LOG_ERROR, "Could not find sparse image memory requirements for color aspect bit!");
		return;
	}

	VkPhysicalDeviceMemoryProperties memProps = { 0 };
	vkGetPhysicalDeviceMemoryProperties(renderer->vulkan.activegpu, &memProps);

	// todo:
	// Calculate number of required sparse memory bindings by alignment
	assert((sparseImageMemoryReqs.size % sparseImageMemoryReqs.alignment) == 0);
	pTexture->pSvt->vulkan.mSparseMemoryTypeBits = sparseImageMemoryReqs.memoryTypeBits;

	// Check if the format has a single mip tail for all layers or one mip tail for each layer
	// The mip tail contains all mip levels > sparseMemoryReq.imageMipTailFirstLod
	bool singleMipTail = sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;

	pTexture->pSvt->pPages = (VirtualTexturePage*)tc_calloc(pTexture->pSvt->mVirtualPageTotalCount, sizeof(VirtualTexturePage));
	pTexture->pSvt->vulkan.pSparseImageMemoryBinds = (VkSparseImageMemoryBind*)tc_calloc(pTexture->pSvt->mVirtualPageTotalCount, sizeof(VkSparseImageMemoryBind));

	pTexture->pSvt->vulkan.pOpaqueMemoryBindAllocations = (void**)tc_calloc(pTexture->pSvt->mVirtualPageTotalCount, sizeof(VmaAllocation));
	pTexture->pSvt->vulkan.pOpaqueMemoryBinds = (VkSparseMemoryBind*)tc_calloc(pTexture->pSvt->mVirtualPageTotalCount, sizeof(VkSparseMemoryBind));

	VmaPoolCreateInfo poolCreateInfo = { 0 };
	poolCreateInfo.memoryTypeIndex = util_get_memory_type(sparseImageMemoryReqs.memoryTypeBits, memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CHECK_VKRESULT(vmaCreatePool(renderer->vulkan.pVmaAllocator, &poolCreateInfo, (VmaPool*)&pTexture->pSvt->vulkan.pPool));

	uint32_t currentPageIndex = 0;

	// Sparse bindings for each mip level of all layers outside of the mip tail
	for (uint32_t layer = 0; layer < 1; layer++)
	{
		// sparseMemoryReq.imageMipTailFirstLod is the first mip level that's stored inside the mip tail
		for (uint32_t mipLevel = 0; mipLevel < TiledMiplevel; mipLevel++)
		{
			VkExtent3D extent;
			extent.width = max(info.extent.width >> mipLevel, 1u);
			extent.height = max(info.extent.height >> mipLevel, 1u);
			extent.depth = max(info.extent.depth >> mipLevel, 1u);

			VkImageSubresource subResource{ 0 };
			subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subResource.mipLevel = mipLevel;
			subResource.arrayLayer = layer;

			// Aligned sizes by image granularity
			VkExtent3D imageGranularity = sparseMemoryReq.formatProperties.imageGranularity;
			VkExtent3D sparseBindCounts = { 0 };
			VkExtent3D lastBlockExtent = { 0 };
			alignedDivision(extent, imageGranularity, &sparseBindCounts);
			lastBlockExtent.width =
				((extent.width % imageGranularity.width) ? extent.width % imageGranularity.width : imageGranularity.width);
			lastBlockExtent.height =
				((extent.height % imageGranularity.height) ? extent.height % imageGranularity.height : imageGranularity.height);
			lastBlockExtent.depth =
				((extent.depth % imageGranularity.depth) ? extent.depth % imageGranularity.depth : imageGranularity.depth);

			// Allocate memory for some blocks
			uint32_t index = 0;
			for (uint32_t z = 0; z < sparseBindCounts.depth; z++)
			{
				for (uint32_t y = 0; y < sparseBindCounts.height; y++)
				{
					for (uint32_t x = 0; x < sparseBindCounts.width; x++)
					{
						// Offset
						VkOffset3D offset;
						offset.x = x * imageGranularity.width;
						offset.y = y * imageGranularity.height;
						offset.z = z * imageGranularity.depth;
						// Size of the page
						VkExtent3D extent;
						extent.width = (x == sparseBindCounts.width - 1) ? lastBlockExtent.width : imageGranularity.width;
						extent.height = (y == sparseBindCounts.height - 1) ? lastBlockExtent.height : imageGranularity.height;
						extent.depth = (z == sparseBindCounts.depth - 1) ? lastBlockExtent.depth : imageGranularity.depth;

						// Add new virtual page
						VirtualTexturePage *newPage = addPage(renderer, pTexture, offset, extent, pTexture->pSvt->mSparseVirtualTexturePageWidth * pTexture->pSvt->mSparseVirtualTexturePageHeight * sizeof(uint), mipLevel, layer, currentPageIndex);
						currentPageIndex++;
						newPage->vulkan.imageMemoryBind.subresource = subResource;

						index++;
					}
				}
			}
		}

		// Check if format has one mip tail per layer
		if ((!singleMipTail) && (sparseMemoryReq.imageMipTailFirstLod < desc->mMipLevels))
		{
			// Allocate memory for the mip tail
			VkMemoryRequirements memReqs = { 0 };
			memReqs.size = sparseMemoryReq.imageMipTailSize;
			memReqs.memoryTypeBits = pTexture->pSvt->vulkan.mSparseMemoryTypeBits;
			memReqs.alignment = memReqs.size;

			VmaAllocationCreateInfo allocCreateInfo = { 0 };
			allocCreateInfo.memoryTypeBits = memReqs.memoryTypeBits;
			allocCreateInfo.pool = (VmaPool)pTexture->pSvt->vulkan.pPool;

			VmaAllocation allocation;
			VmaAllocationInfo allocationInfo;
			CHECK_VKRESULT(vmaAllocateMemory(renderer->vulkan.pVmaAllocator, &memReqs, &allocCreateInfo, &allocation, &allocationInfo));

			// (Opaque) sparse memory binding
			VkSparseMemoryBind sparseMemoryBind{ 0 };
			sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset + layer * sparseMemoryReq.imageMipTailStride;
			sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
			sparseMemoryBind.memory = allocation->GetMemory();
			sparseMemoryBind.memoryOffset = allocation->GetOffset();

			pTexture->pSvt->vulkan.pOpaqueMemoryBindAllocations[pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount] = allocation;
			pTexture->pSvt->vulkan.pOpaqueMemoryBinds[pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount] = sparseMemoryBind;
			pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount++;
		}
	}    // end layers and mips

	TRACE(LOG_INFO, "Virtual Texture info: Dim %d x %d Pages %d", desc->width, desc->height, pTexture->pSvt->mVirtualPageTotalCount);

	// Check if format has one mip tail for all layers
	if ((sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) &&
		(sparseMemoryReq.imageMipTailFirstLod < desc->mMipLevels))
	{
		// Allocate memory for the mip tail
		VkMemoryRequirements memReqs = { 0 };
		memReqs.size = sparseMemoryReq.imageMipTailSize;
		memReqs.memoryTypeBits = pTexture->pSvt->vulkan.mSparseMemoryTypeBits;
		memReqs.alignment = memReqs.size;

		VmaAllocationCreateInfo allocCreateInfo = { 0 };
		allocCreateInfo.memoryTypeBits = memReqs.memoryTypeBits;
		allocCreateInfo.pool = (VmaPool)pTexture->pSvt->vulkan.pPool;

		VmaAllocation allocation;
		VmaAllocationInfo allocationInfo;
		CHECK_VKRESULT(vmaAllocateMemory(renderer->vulkan.pVmaAllocator, &memReqs, &allocCreateInfo, &allocation, &allocationInfo));

		// (Opaque) sparse memory binding
		VkSparseMemoryBind sparseMemoryBind{ 0 };
		sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset;
		sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
		sparseMemoryBind.memory = allocation->GetMemory();
		sparseMemoryBind.memoryOffset = allocation->GetOffset();

		pTexture->pSvt->vulkan.pOpaqueMemoryBindAllocations[pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount] = allocation;
		pTexture->pSvt->vulkan.pOpaqueMemoryBinds[pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount] = sparseMemoryBind;
		pTexture->pSvt->vulkan.mOpaqueMemoryBindsCount++;
	}

	/************************************************************************/
	// Create image view
	/************************************************************************/
	VkImageViewCreateInfo view = { 0 };
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layercount = 1;
	view.subresourceRange.levelCount = desc->mMipLevels;
	view.image = pTexture->vulkan.pVkImage;
	pTexture->mAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	CHECK_VKRESULT(vkCreateImageView(renderer->vulkan.device, &view, &alloccbs, &pTexture->vulkan.pVkSRVDescriptor));

	TextureBarrier textureBarriers[] = { { pTexture, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST } };
	cmd_resourcebarrier(pCmd, 0, NULL, 1, textureBarriers, 0, NULL);

	// Fill smallest (non-tail) mip map level
	vk_fillVirtualTextureLevel(pCmd, pTexture, TiledMiplevel - 1, 0);

	pTexture->mOwnsImage = true;
	pTexture->mNodeIndex = desc->mNodeIndex;
	pTexture->mMipLevels = desc->mMipLevels;
	pTexture->width = desc->width;
	pTexture->height = desc->height;
	pTexture->mDepth = desc->mDepth;

#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->pName)
	{
		setTextureName(renderer, pTexture, desc->pName);
	}
#endif

	*ppTexture = pTexture;
}

void vk_removeVirtualTexture(renderer_t* renderer, Virtualtexture_t* pSvt)
{
	for (int i = 0; i < (int)pSvt->mVirtualPageTotalCount; i++)
	{
		VirtualTexturePage& page = pSvt->pPages[i];
		if (page.vulkan.pAllocation)
			vmaFreeMemory(renderer->vulkan.pVmaAllocator, (VmaAllocation)page.vulkan.pAllocation);
	}
	tc_free(pSvt->pPages);

	for (int i = 0; i < (int)pSvt->vulkan.mOpaqueMemoryBindsCount; i++)
	{
		vmaFreeMemory(renderer->vulkan.pVmaAllocator, (VmaAllocation)pSvt->vulkan.pOpaqueMemoryBindAllocations[i]);
	}
	tc_free(pSvt->vulkan.pOpaqueMemoryBinds);
	tc_free(pSvt->vulkan.pOpaqueMemoryBindAllocations);
	tc_free(pSvt->vulkan.pSparseImageMemoryBinds);

	for (uint32_t deletionIndex = 0; deletionIndex < pSvt->mPendingDeletionCount; deletionIndex++)
	{
		const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(pSvt, deletionIndex);

		for (uint32_t i = 0; i < *pendingDeletion.pAllocationsCount; i++)
			vmaFreeMemory(renderer->vulkan.pVmaAllocator, pendingDeletion.pAllocations[i]);
		for (uint32_t i = 0; i < *pendingDeletion.pIntermediateBuffersCount; i++)
			remove_buffer(renderer, pendingDeletion.pIntermediateBuffers[i]);
	}
	tc_free(pSvt->vulkan.pPendingDeletedAllocations);
	tc_free(pSvt->pPendingDeletedAllocationsCount);
	tc_free(pSvt->pPendingDeletedBuffers);
	tc_free(pSvt->pPendingDeletedBuffersCount);

	tc_free(pSvt->pVirtualImageData);

	vmaDestroyPool(renderer->vulkan.pVmaAllocator, (VmaPool)pSvt->vulkan.pPool);
}

void vk_cmdUpdateVirtualTexture(cmd_t* cmd, texture_t* pTexture, uint32_t currentImage)
{
	TC_ASSERT(pTexture->pSvt->pReadbackBuffer);

	const bool map = !pTexture->pSvt->pReadbackBuffer->pCpuMappedAddress;
	if (map)
	{
		mapBuffer(cmd->renderer, pTexture->pSvt->pReadbackBuffer, NULL);
	}

	vk_releasePage(cmd, pTexture, currentImage);
	vk_fillVirtualTexture(cmd, pTexture, NULL, currentImage);

	if (map)
	{
		unmapBuffer(cmd->renderer, pTexture->pSvt->pReadbackBuffer);
	}
}

#endif

void initVulkanRenderer(const char* appName, const RendererDesc* pSettings, Renderer** prenderer)
{
	// API functions
	add_fence = vk_add_fence;
	remove_fence = vk_remove_fence;
	addSemaphore = vk_addSemaphore;
	removeSemaphore = vk_removeSemaphore;
	addQueue = vk_addQueue;
	remove_queue = vk_remove_queue;
	addSwapChain = vk_addSwapChain;
	removeSwapChain = vk_removeSwapChain;

	// command pool functions
	add_cmdPool = vk_add_cmdPool;
	remove_cmdpool = vk_remove_cmdpool;
	add_cmd = vk_add_cmd;
	remove_cmd = vk_remove_cmd;
	add_cmd_n = vk_add_cmd_n;
	remove_cmd_n = vk_remove_cmd_n;

	addRenderTarget = vk_addRenderTarget;
	removeRenderTarget = vk_removeRenderTarget;
	add_sampler = vk_add_sampler;
	remove_sampler = vk_remove_sampler;

	// Resource Load functions
	add_buffer = vk_add_buffer;
	remove_buffer = vk_remove_buffer;
	mapBuffer = vk_mapBuffer;
	unmapBuffer = vk_unmapBuffer;
	cmdUpdateBuffer = vk_cmdUpdateBuffer;
	cmdUpdateSubresource = vk_cmdUpdateSubresource;
	cmdCopySubresource = vk_cmdCopySubresource;
	add_texture = vk_add_texture;
	remove_texture = vk_remove_texture;
	addVirtualTexture = vk_addVirtualTexture;
	removeVirtualTexture = vk_removeVirtualTexture;

	// shader functions
	addShaderBinary = vk_addShaderBinary;
	removeShader = vk_removeShader;

	addRootSignature = vk_addRootSignature;
	removeRootSignature = vk_removeRootSignature;

	// pipeline functions
	addPipeline = vk_addPipeline;
	removePipeline = vk_removePipeline;
	addPipelineCache = vk_addPipelineCache;
	getPipelineCacheData = vk_getPipelineCacheData;
	removePipelineCache = vk_removePipelineCache;

	// Descriptor Set functions
	addDescriptorSet = vk_addDescriptorSet;
	removeDescriptorSet = vk_removeDescriptorSet;
	updateDescriptorSet = vk_updateDescriptorSet;

	// command buffer functions
	reset_cmdpool = vk_reset_cmdpool;
	cmd_begin = vk_cmd_begin;
	cmd_end = vk_cmd_end;
	cmdBindRenderTargets = vk_cmdBindRenderTargets;
	cmdSetShadingRate = vk_cmdSetShadingRate;
	cmdSetViewport = vk_cmdSetViewport;
	cmdSetScissor = vk_cmdSetScissor;
	cmdSetStencilReferenceValue = vk_cmdSetStencilReferenceValue;
	cmdBindPipeline = vk_cmdBindPipeline;
	cmdBindDescriptorSet = vk_cmdBindDescriptorSet;
	cmdBindPushConstants = vk_cmdBindPushConstants;
	cmdBindDescriptorSetWithRootCbvs = vk_cmdBindDescriptorSetWithRootCbvs;
	cmdBindIndexBuffer = vk_cmdBindIndexBuffer;
	cmdBindVertexBuffer = vk_cmdBindVertexBuffer;
	cmdDraw = vk_cmdDraw;
	cmdDrawInstanced = vk_cmdDrawInstanced;
	cmdDrawIndexed = vk_cmdDrawIndexed;
	cmdDrawIndexedInstanced = vk_cmdDrawIndexedInstanced;
	cmdDispatch = vk_cmdDispatch;

	// Transition Commands
	cmd_resourcebarrier = vk_cmd_resourcebarrier;
	// Virtual Textures
	cmdUpdateVirtualTexture = vk_cmdUpdateVirtualTexture;

	// queue/fence/swapchain functions
	acquireNextImage = vk_acquireNextImage;
	queue_submit = vk_queue_submit;
	queuePresent = vk_queuePresent;
	waitQueueIdle = vk_waitQueueIdle;
	getFenceStatus = vk_getFenceStatus;
	wait_for_fences = vk_wait_for_fences;
	toggleVSync = vk_toggleVSync;

	getRecommendedSwapchainFormat = vk_getRecommendedSwapchainFormat;

	//indirect Draw functions
	addIndirectCommandSignature = vk_addIndirectCommandSignature;
	removeIndirectCommandSignature = vk_removeIndirectCommandSignature;
	cmdExecuteIndirect = vk_cmdExecuteIndirect;

	/************************************************************************/
	// GPU Query Interface
	/************************************************************************/
	getTimestampFrequency = vk_getTimestampFrequency;
	addQueryPool = vk_addQueryPool;
	removeQueryPool = vk_removeQueryPool;
	cmdResetQueryPool = vk_cmdResetQueryPool;
	cmdBeginQuery = vk_cmdBeginQuery;
	cmdEndQuery = vk_cmdEndQuery;
	cmdResolveQuery = vk_cmdResolveQuery;
	/************************************************************************/
	// Stats Info Interface
	/************************************************************************/
	calculateMemoryStats = vk_calculateMemoryStats;
	calculateMemoryUse = vk_calculateMemoryUse;
	freeMemoryStats = vk_freeMemoryStats;
	/************************************************************************/
	// Debug Marker Interface
	/************************************************************************/
	cmdBeginDebugMarker = vk_cmdBeginDebugMarker;
	cmdEndDebugMarker = vk_cmdEndDebugMarker;
	cmdAddDebugMarker = vk_cmdAddDebugMarker;
	cmdWriteMarker = vk_cmdWriteMarker;
	/************************************************************************/
	// Resource Debug Naming Interface
	/************************************************************************/
	setBufferName = vk_setBufferName;
	setTextureName = vk_setTextureName;
	setRenderTargetName = vk_setRenderTargetName;
	setPipelineName = vk_setPipelineName;

	vk_initRenderer(appName, pSettings, prenderer);
}

void exitVulkanRenderer(renderer_t* renderer)
{
	TC_ASSERT(renderer);

	vk_exitRenderer(renderer);
}
