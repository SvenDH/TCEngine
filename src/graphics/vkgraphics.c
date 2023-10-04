/* Imports */
#include "private_types.h"

#include "private/vkgraphics.h"

#include "stb_ds.h"

//#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <tinyimageformat.h>


static _Atomic uint32_t rtids = 1;

inline void vk_utils_caps_builder(renderer_t* r)
{
	r->capbits = (gpucaps_t*)tc_calloc(1, sizeof(gpucaps_t));

	for (uint32_t i = 0; i < TinyImageFormat_Count; ++i) {
		VkFormatProperties formatsupport;
		VkFormat fmt = (VkFormat) TinyImageFormat_ToVkFormat((TinyImageFormat)i);
		if(fmt == VK_FORMAT_UNDEFINED) continue;

		vkGetPhysicalDeviceFormatProperties(r->vk.activegpu, fmt, &formatsupport);
		r->capbits->canshaderreadfrom[i] =
				(formatsupport.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
		r->capbits->canshaderwriteto[i] =
				(formatsupport.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
		r->capbits->canrtwriteto[i] =
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
#define MAX_WRITE_SETS 256
#define MAX_DESCRIPTOR_INFO_BYTES sizeof(VkDescriptorImageInfo) * 1024

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

VkCompareOp compareopmap[MAX_COMPARE_MODES] = {
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

typedef void (*add_buffer_func)(renderer_t* r, const bufferdesc_t* desc, buffer_t** buf);
typedef void (*remove_buffer_func)(renderer_t* r, buffer_t* buf);
typedef void (*map_buffer_func)(renderer_t* r, buffer_t* buf, range_t* range);
typedef void (*unmap_buffer_func)(renderer_t* r, buffer_t* buf);
typedef void (*cmd_updatebuffer_func)(cmd_t* cmd, buffer_t* buf, uint64_t dstoffset, buffer_t* srcbuf, uint64_t srcoffset, uint64_t size);
typedef void (*cmd_updatesubresource_func)(cmd_t* cmd, texture_t* tex, buffer_t* srcbuf, const struct subresourcedatadesc_s* desc);
typedef void (*cmd_copysubresource_func)(cmd_t* cmd, buffer_t* dstbuf, texture_t* tex, const struct subresourcedatadesc_s* desc);
typedef void (*add_texture_func)(renderer_t* r, const texturedesc_t* desc, texture_t** tex);
typedef void (*remove_texture_func)(renderer_t* r, texture_t* tex);
typedef void (*add_virtualtexture_func)(cmd_t* cmd, const texturedesc_t* desc, texture_t** tex, void* imagedata);
typedef void (*remove_virtualtexture_func)(renderer_t* r, virtualtexture_t* tex);

// Descriptor Pool Functions
static void add_descriptor_pool(
	renderer_t* r, uint32_t numsets, VkDescriptorPoolCreateFlags flags,
	const VkDescriptorPoolSize* sizes, uint32_t numsizes, VkDescriptorPool* pool)
{
	VkDescriptorPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	info.pNext = NULL;
	info.poolSizeCount = numsizes;
	info.pPoolSizes = sizes;
	info.flags = flags;
	info.maxSets = numsets;
	CHECK_VKRESULT(vkCreateDescriptorPool(r->vk.device, &info, &alloccbs, pool));
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

static void add_renderpass(renderer_t* r, const renderpassdesc_t* desc, renderpass_t* renderpass)
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
	CHECK_VKRESULT(vkCreateRenderPass(r->vk.device, &info, &alloccbs, &(renderpass->renderpass)));
}

static void remove_render_pass(renderer_t* r, renderpass_t* renderpass)
{
	vkDestroyRenderPass(r->vk.device, renderpass->renderpass, &alloccbs);
}

static void add_framebuffer(renderer_t* r, const framebufferdesc_t* desc, framebuffer_t* framebuffer)
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
			*iterviews = desc->rendertargets[i]->vk.descriptor;
			++iterviews;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (is_storeop_resolve(desc->rtresolveop[i])) {
				*iterviews = desc->rendertargets[i]->resolveattachment->vk.descriptor;
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
			*iterviews = desc->rendertargets[i]->vk.slicedescriptors[handle];
			++iterviews;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (is_storeop_resolve(desc->rtresolveop[i])) {
				*iterviews = desc->rendertargets[i]->resolveattachment->vk.slicedescriptors[handle];
				++iterviews;
			}
#endif
		}
	}
	if (desc->depthstencil) {
		if (UINT32_MAX == desc->depthmipslice && UINT32_MAX == desc->deptharrayslice) {
			*iterviews = desc->depthstencil->vk.descriptor;
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
			*iterviews = desc->depthstencil->vk.slicedescriptors[handle];
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
	CHECK_VKRESULT(vkCreateFramebuffer(r->vk.device, &info, &alloccbs, &(framebuffer->framebuffer)));
}

static void remove_framebuffer(renderer_t* r, framebuffer_t* framebuffer)
{
	TC_ASSERT(r && framebuffer);
	vkDestroyFramebuffer(r->vk.device, framebuffer->framebuffer, &alloccbs);
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
	ds.depthCompareOp = compareopmap[desc->depthfunc];
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
	ds.back.compareOp = compareopmap[desc->stencilbackfunc];
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

static void _initial_transition(renderer_t* r, texture_t* tex, resourcestate_t state)
{
	TC_LOCK(&r->nulldescriptors->initlck);
	{
		cmd_t* cmd = r->nulldescriptors->initcmd;
		reset_cmdpool(r, r->nulldescriptors->initcmdpool);
		cmd_begin(cmd);
		cmd_resourcebarrier(cmd, 0, NULL, 1, &(texbarrier_t){ tex, RESOURCE_STATE_UNDEFINED, state }, 0, NULL);
		cmd_end(cmd);
		queuesubmitdesc_t desc = { 0 };
		desc.cmdcount = 1;
		desc.cmds = &cmd;
		desc.signalfence = r->nulldescriptors->initfence;
		queue_submit(r->nulldescriptors->initqueue, &desc);
		wait_for_fences(r, 1, &r->nulldescriptors->initfence);
	}
	TC_UNLOCK(&r->nulldescriptors->initlck);
}

static void add_default_resources(renderer_t* r)
{
	spin_lock_init(&r->nulldescriptors->submitlck);
	spin_lock_init(&r->nulldescriptors->initlck);
	for (uint32_t i = 0; i < r->linkednodecount; ++i) {
		uint32_t idx = r->gpumode == GPU_MODE_UNLINKED ? r->unlinkedrendererindex : i;
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
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_1D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexUAV[i][TEXTURE_1D]);

		// 1D texture array
		desc.arraysize = 2;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_1D_ARRAY]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexUAV[i][TEXTURE_1D_ARRAY]);

		// 2D texture
		desc.width = 2;
		desc.height = 2;
		desc.arraysize = 1;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_2D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexUAV[i][TEXTURE_2D]);

		// 2D MS texture
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		desc.samplecount = SAMPLE_COUNT_4;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_2DMS]);
		desc.samplecount = SAMPLE_COUNT_1;

		// 2D texture array
		desc.arraysize = 2;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_2D_ARRAY]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexUAV[i][TEXTURE_2D_ARRAY]);

		// 2D MS texture array
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE;
		desc.samplecount = SAMPLE_COUNT_4;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_2DMS_ARRAY]);
		desc.samplecount = SAMPLE_COUNT_1;

		// 3D texture
		desc.depth = 2;
		desc.arraysize = 1;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_3D]);
		desc.descriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexUAV[i][TEXTURE_3D]);

		// Cube texture
		desc.depth = 1;
		desc.arraysize = 6;
		desc.descriptors = DESCRIPTOR_TYPE_TEXTURE_CUBE;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_CUBE]);
		desc.arraysize = 6 * 2;
		add_texture(r, &desc, &r->nulldescriptors->defaulttexSRV[i][TEXTURE_CUBE_ARRAY]);

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
		add_buffer(r, &bufdesc, &r->nulldescriptors->defaultbufSRV[i]);
		bufdesc.descriptors = DESCRIPTOR_TYPE_RW_BUFFER;
		add_buffer(r, &bufdesc, &r->nulldescriptors->defaultbufUAV[i]);
	}

	samplerdesc_t samplerdesc = { 0 };
	samplerdesc.u = WRAP_BORDER;
	samplerdesc.v = WRAP_BORDER;
	samplerdesc.w = WRAP_BORDER;
	add_sampler(r, &samplerdesc, &r->nulldescriptors->defaultsampler);

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
	add_queue(r, &queuedesc, &gfxqueue);

	cmdpooldesc_t pooldesc = { 0 };
	pooldesc.queue = gfxqueue;
	pooldesc.transient = true;
	add_cmdpool(r, &pooldesc, &pool);

	cmddesc_t cmddesc = { 0 };
	cmddesc.pool = pool;
	add_cmds(r, &cmddesc, 1, &cmd);
	add_fence(r, &fence);
	r->nulldescriptors->initqueue = gfxqueue;
	r->nulldescriptors->initcmdpool = pool;
	r->nulldescriptors->initcmd = cmd;
	r->nulldescriptors->initfence = fence;

	// Transition resources
	for (uint32_t i = 0; i < r->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			if (r->nulldescriptors->defaulttexSRV[i][dim])
				_initial_transition(r, r->nulldescriptors->defaulttexSRV[i][dim], RESOURCE_STATE_SHADER_RESOURCE);
			if (r->nulldescriptors->defaulttexUAV[i][dim])
				_initial_transition(r, r->nulldescriptors->defaulttexUAV[i][dim], RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}
}

static void remove_default_resources(renderer_t* r)
{
	for (uint32_t i = 0; i < r->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			if (r->nulldescriptors->defaulttexSRV[i][dim])
				remove_texture(r, r->nulldescriptors->defaulttexSRV[i][dim]);
			if (r->nulldescriptors->defaulttexUAV[i][dim])
				remove_texture(r, r->nulldescriptors->defaulttexUAV[i][dim]);
		}
		remove_buffer(r, r->nulldescriptors->defaultbufSRV[i]);
		remove_buffer(r, r->nulldescriptors->defaultbufUAV[i]);
	}
	remove_sampler(r, r->nulldescriptors->defaultsampler);
	remove_fence(r, r->nulldescriptors->initfence);
	remove_cmds(r, 1, r->nulldescriptors->initcmd);
	remove_cmdpool(r, r->nulldescriptors->initcmdpool);
	remove_queue(r, r->nulldescriptors->initqueue);
}

VkFilter _to_vk_filter(filtermode_t filter)
{
	switch (filter) {
		case FILTER_NEAREST: return VK_FILTER_NEAREST;
		case FILTER_LINEAR: return VK_FILTER_LINEAR;
		default: return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode _to_vk_mip_map_mode(mipmapmode_t mode)
{
	switch (mode) {
		case MIPMAP_MODE_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case MIPMAP_MODE_LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		default: TC_ASSERT(false && "Invalid Mip Map Mode"); return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
	}
}

VkSamplerAddressMode _to_vk_address_mode(addressmode_t mode)
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

uint32_t _get_memory_type(uint32_t typebits, const VkPhysicalDeviceMemoryProperties* memprops, const VkMemoryPropertyFlags props)
{
	for (uint32_t i = 0; i < memprops->memoryTypeCount; i++) {
		if ((typebits & 1) == 1)
			if ((memprops->memoryTypes[i].propertyFlags & props) == props)
				return i;
		typebits >>= 1;
	}
	TRACE(LOG_ERROR, "Could not find a matching memory type");
	TC_ASSERT(0);
	return 0;
}

// Determines pipeline stages involved for given accesses
VkPipelineStageFlags util_determine_pipeline_stage_flags(renderer_t* r, VkAccessFlags accessflags, queuetype_t queuetype)
{
	VkPipelineStageFlags flags = 0;
	switch (queuetype) {
		case QUEUE_TYPE_GRAPHICS:
			if ((accessflags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			if ((accessflags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0) {
				flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
				flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				if (r->activegpusettings->geometryshadersupported)
					flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
				if (r->activegpusettings->tessellationsupported) {
					flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
					flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
				}
				flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef VK_RAYTRACING_AVAILABLE
				if (r->vk.raytracingsupported) flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
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
	const renderer_t* r, uint32_t nodeidx, queuetype_t queuetype, VkQueueFamilyProperties* outprops, uint8_t* outfamidx,
	uint8_t* outqueueidx)
{
	if (r->gpumode != GPU_MODE_LINKED) nodeidx = 0;

	uint32_t queuefamidx = UINT32_MAX;
	uint32_t queueidx = UINT32_MAX;
	VkQueueFlags requiredFlags = util_to_vk_queue_flags(queuetype);
	bool found = false;

	// Get queue family properties
	uint32_t queuefampropscount = 0;
	VkQueueFamilyProperties* queuefamprops = NULL;
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefampropscount, NULL);
	queuefamprops = (VkQueueFamilyProperties*)alloca(queuefampropscount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefampropscount, queuefamprops);

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
			r->vk.usedqueues[nodeidx][queueflags] < r->vk.availablequeues[nodeidx][queueflags]) {
			found = true;
			queuefamidx = i;
			queueidx = r->vk.usedqueues[nodeidx][queueflags];
			break;
		}
		if (flagand && ((queueflags - flagand) < minflag) && !gfxqueue &&
			r->vk.usedqueues[nodeidx][queueflags] < r->vk.availablequeues[nodeidx][queueflags]) {
			found = true;
			minflag = (queueflags - flagand);
			queuefamidx = i;
			queueidx = r->vk.usedqueues[nodeidx][queueflags];
			break;
		}
	}
	// If hardware doesn't provide a dedicated queue try to find a non-dedicated one
	if (!found) {
		for (uint32_t i = 0; i < queuefampropscount; ++i) {
			VkQueueFlags queueFlags = queuefamprops[i].queueFlags;
			if ((queueFlags & requiredFlags) &&
				r->vk.usedqueues[nodeidx][queueFlags] < r->vk.availablequeues[nodeidx][queueFlags]) {
				found = true;
				queuefamidx = i;
				queueidx = r->vk.usedqueues[nodeidx][queueFlags];
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

void util_calculate_device_indices(renderer_t* r, uint32_t nodeidx, uint32_t* sharednodeindices, uint32_t sharednodeidxcount, uint32_t* idxs)
{
	for (uint32_t i = 0; i < r->linkednodecount; ++i) idxs[i] = i;
	idxs[nodeidx] = nodeidx;
	for (uint32_t i = 0; i < sharednodeidxcount; ++i) idxs[sharednodeindices[i]] = nodeidx;
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

static bool init_common(const char* app_name, const rendererdesc_t* desc, renderer_t* r)
{
	const char** layers = (const char**)alloca((2 + desc->vk.instancelayercount) * sizeof(char*));
	uint32_t num_layers = 0;
#if defined(ENABLE_GRAPHICS_DEBUG)
		layers[num_layers++] = "VK_LAYER_KHRONOS_validation";
#endif
#ifdef ENABLE_RENDER_DOC
		layers[num_layers++] = "VK_LAYER_RENDERDOC_Capture";
#endif
	for (uint32_t i = 0; i < (uint32_t)desc->vk.instancelayercount; ++i)
		layers[num_layers++] = desc->vk.instancelayers[i];

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
	const uint32_t requestedcount = (uint32_t)desc->vk.instanceextensioncount;
	const char** wantedext = NULL;
	arrsetlen(wantedext, initialcount + num_layers);
	for (uint32_t i = 0; i < initialcount; ++i)
		wantedext[i] = wantedinstanceextensions[i];
	for (uint32_t i = 0; i < requestedcount; ++i)
		wantedext[initialcount + i] = desc->vk.instanceextensions[i];
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

	CHECK_VKRESULT(vkCreateInstance(&instance_info, &alloccbs, &(r->vk.instance)));
	arrfree(temp);
	arrfree(wantedext);

	volkLoadInstance(r->vk.instance);
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
		VkResult res = vkCreateDebugUtilsMessengerEXT(r->vk.instance, &info, &alloccbs, &(r->vk.debugutilsmessenger));
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
		VkResult res = vkCreateDebugReportCallbackEXT(r->vk.instance, &info, &alloccbs, &(r->vk.debugreport));
		if (VK_SUCCESS != res) {
			TRACE(LOG_ERROR, "%s ( %s )", "vkCreateDebugReportCallbackEXT failed - disabling Vulkan debug callbacks", "internal_vk_init_instance");
		}
	}
#endif
	r->unlinkedrendererindex = 0;
	r->vk.owninstance = true;
	return true;
}

static void exit_common(renderer_t* r)
{
	TC_ASSERT(VK_NULL_HANDLE != r->vk.instance);
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
	if (r->vk.debugutilsmessenger) {
		vkDestroyDebugUtilsMessengerEXT(r->vk.instance, r->vk.debugutilsmessenger, &alloccbs);
		r->vk.debugutilsmessenger = NULL;
	}
#else
	if (r->vk.debugreport) {
		vkDestroyDebugReportCallbackEXT(r->vk.instance, r->vk.debugreport, &alloccbs);
		r->vk.debugreport = NULL;
	}
#endif
	vkDestroyInstance(r->vk.instance, &alloccbs);
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

static bool _select_best_gpu(renderer_t* r)
{
	TC_ASSERT(VK_NULL_HANDLE != r->vk.instance);
	uint32_t count = 0;
	CHECK_VKRESULT(vkEnumeratePhysicalDevices(r->vk.instance, &count, NULL));
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
	CHECK_VKRESULT(vkEnumeratePhysicalDevices(r->vk.instance, &count, gpus));

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
	r->vk.activegpu = gpus[gpuidx];
	r->vk.activegpuprops = (VkPhysicalDeviceProperties2*)tc_malloc(sizeof(VkPhysicalDeviceProperties2));
	r->activegpusettings = (gpusettings_t*)tc_malloc(sizeof(gpusettings_t));
	*r->vk.activegpuprops = gpuprops[gpuidx];
	r->vk.activegpuprops->pNext = NULL;
	*r->activegpusettings = gpusettings[gpuidx];
	TC_ASSERT(VK_NULL_HANDLE != r->vk.activegpu);

	TRACE(LOG_INFO, "GPU[%d] is selected as default GPU", gpuidx);
	TRACE(LOG_INFO, "Name of selected gpu: %s", r->activegpusettings->gpuvendorpreset.gpuname);
	TRACE(LOG_INFO, "Vendor id of selected gpu: %s", r->activegpusettings->gpuvendorpreset.vendorid);
	TRACE(LOG_INFO, "Model id of selected gpu: %s", r->activegpusettings->gpuvendorpreset.modelid);
	TRACE(LOG_INFO, "Preset of selected gpu: %s", presettostr(r->activegpusettings->gpuvendorpreset.presetlevel));

	for (uint32_t i = 0; i < count; ++i) tc_free(queuefamprops[i]);
	return true;
}

static bool add_device(const rendererdesc_t* desc, renderer_t* r)
{
	TC_ASSERT(VK_NULL_HANDLE != r->vk.instance);
	const char* cache[MAX_DEVICE_EXTENSIONS] = { 0 };
#if VK_KHR_device_group_creation
	VkDeviceGroupDeviceCreateInfoKHR info = { VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO_KHR };
	VkPhysicalDeviceGroupPropertiesKHR props[MAX_LINKED_GPUS] = { 0 };
	r->linkednodecount = 1;
	if (r->gpumode == GPU_MODE_LINKED && devicegroupcreationext) {
		uint32_t count = 0;
		vkEnumeratePhysicalDeviceGroupsKHR(r->vk.instance, &count, NULL);	// Query the number of device groups
		for (uint32_t i = 0; i < count; i++) {	// Allocate and initialize structures to query the device groups
			props[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHR;
			props[i].pNext = NULL;
		}
		CHECK_VKRESULT(vkEnumeratePhysicalDeviceGroupsKHR(r->vk.instance, &count, props));
		// If the first device group has more than one physical device create a logical device using all of the physical devices.
		for (uint32_t i = 0; i < count; i++) {
			if (props[i].physicalDeviceCount > 1) {
				info.physicalDeviceCount = props[i].physicalDeviceCount;
				info.pPhysicalDevices = props[i].physicalDevices;
				r->linkednodecount = info.physicalDeviceCount;
				break;
			}
		}
	}
#endif
	if (r->linkednodecount < 2 && r->gpumode == GPU_MODE_LINKED)
		r->gpumode = GPU_MODE_SINGLE;
	if (!desc->context)
		if (!_select_best_gpu(r)) return false;
	else {
		TC_ASSERT(desc->gpuindex < desc->context->gpucount);
		r->vk.activegpu = desc->context->gpus[desc->gpuindex].vk.gpu;
		r->vk.activegpuprops = (VkPhysicalDeviceProperties2*)tc_malloc(sizeof(VkPhysicalDeviceProperties2));
		r->activegpusettings = (gpusettings_t*)tc_malloc(sizeof(gpusettings_t));
		*r->vk.activegpuprops = desc->context->gpus[desc->gpuindex].vk.gpuprops;
		r->vk.activegpuprops->pNext = NULL;
		*r->activegpusettings = desc->context->gpus[desc->gpuindex].settings;
	}
	uint32_t layercount = 0;
	uint32_t extcount = 0;
	vkEnumerateDeviceLayerProperties(r->vk.activegpu, &layercount, NULL);
	vkEnumerateDeviceExtensionProperties(r->vk.activegpu, NULL, &extcount, NULL);
	VkLayerProperties* layers = (VkLayerProperties*)alloca(sizeof(VkLayerProperties) * layercount);
	vkEnumerateDeviceLayerProperties(r->vk.activegpu, &layercount, layers);
	VkExtensionProperties* exts = (VkExtensionProperties*)alloca(sizeof(VkExtensionProperties) * extcount);
	vkEnumerateDeviceExtensionProperties(r->vk.activegpu, NULL, &extcount, exts);
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
	const uint32_t userRequestedCount = (uint32_t)desc->vk.deviceextensioncount;
	const char** wanted = NULL;
	arrsetlen(wanted, initialcount + userRequestedCount);
	for (uint32_t i = 0; i < initialcount; ++i)
		wanted[i] = wanteddeviceextensions[i];
	for (uint32_t i = 0; i < userRequestedCount; ++i)
		wanted[initialcount + i] = desc->vk.deviceextensions[i];
	const uint32_t wanted_extcount = (uint32_t)arrlen(wanted);
	uint32_t       count = 0;
	vkEnumerateDeviceExtensionProperties(r->vk.activegpu, layer_name, &count, NULL);
	if (count > 0) {
		VkExtensionProperties* properties = (VkExtensionProperties*)tc_calloc(count, sizeof(*properties));
		TC_ASSERT(properties != NULL);
		vkEnumerateDeviceExtensionProperties(r->vk.activegpu, layer_name, &count, properties);
		for (uint32_t j = 0; j < count; ++j) {
			for (uint32_t k = 0; k < wanted_extcount; ++k) {
				if (strcmp(wanted[k], properties[j].extensionName) == 0) {
					cache[extcount++] = wanted[k];

#ifndef ENABLE_DEBUG_UTILS_EXTENSION
					if (strcmp(wanted[k], VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0)
						r->vk.debugmarkersupport = true;
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
						r->vk.drawindirectcountextension = true;
#endif
					if (strcmp(wanted[k], VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0)
						r->vk.AMDdrawindirectcountextension = true;
					if (strcmp(wanted[k], VK_AMD_GCN_SHADER_EXTENSION_NAME) == 0)
						r->vk.AMDGCNshaderextension = true;
#if VK_EXT_descriptor_indexing
					if (strcmp(wanted[k], VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
						r->vk.descriptorindexingextension = true;
#endif
#ifdef VK_RAYTRACING_AVAILABLE
					uint32_t khrrt = 1; // KHRONOS VULKAN RAY TRACING
					if (strcmp(wanted[k], VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) == 0)
						r->vk.shaderfloatcontrolsextension = 1;
					khrrt &= r->vk.shaderfloatcontrolsextension;
					if (strcmp(wanted[k], VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
						r->vk.bufferdeviceaddressextension = 1;
					khrrt &= r->vk.bufferdeviceaddressextension;
					if (strcmp(wanted[k], VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
						r->vk.deferredhostoperationsextension = 1;
					khrrt &= r->vk.deferredhostoperationsextension;
					if (strcmp(wanted[k], VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
						r->vk.KHRaccelerationstructureextension = 1;
					khrrt &= r->vk.KHRaccelerationstructureextension;
					if (strcmp(wanted[k], VK_KHR_SPIRV_1_4_EXTENSION_NAME) == 0)
						r->vk.KHRspirv14extension = 1;
					khrrt &= r->vk.KHRspirv14extension;
					if (strcmp(wanted[k], VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
						r->vk.KHRRaytracingpipelineextension = 1;
					khrrt &= r->vk.KHRRaytracingpipelineextension;
					if (khrrt) r->vk.raytracingsupported = 1;
					if (strcmp(wanted[k], VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
						r->vk.KHRrayqueryextension = 1;
#endif
#if VK_KHR_sampler_ycbcr_conversion
					if (strcmp(wanted[k], VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) == 0)
						r->vk.YCbCrextension = true;
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
	ADD_TO_NEXT_CHAIN(r->vk.descriptorindexingextension, descriptorIndexingFeatures);
#endif
#if VK_KHR_sampler_ycbcr_conversion
	VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES };
	ADD_TO_NEXT_CHAIN(r->vk.YCbCrextension, ycbcrFeatures);
#endif
#if VK_KHR_buffer_device_address
	VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddressFeatures = { 0 };
	enabledBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	enabledBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
	ADD_TO_NEXT_CHAIN(r->vk.bufferdeviceaddressextension, enabledBufferDeviceAddressFeatures);
#endif
#if VK_KHR_ray_tracing_pipeline
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures = { 0 };
	enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	ADD_TO_NEXT_CHAIN(r->vk.KHRRaytracingpipelineextension, enabledRayTracingPipelineFeatures); 
#endif
#if VK_KHR_acceleration_structure
	VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures = { 0 };
	enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
	ADD_TO_NEXT_CHAIN(r->vk.KHRaccelerationstructureextension, enabledAccelerationStructureFeatures);
#endif
#if VK_KHR_ray_query
	VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures = { 0 };
	enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	enabledRayQueryFeatures.rayQuery = VK_TRUE; 
	ADD_TO_NEXT_CHAIN(r->vk.KHRrayqueryextension, enabledRayQueryFeatures); 
#endif
	vkGetPhysicalDeviceFeatures2KHR(r->vk.activegpu, &gpufeats2);

	uint32_t queuefamcount = 0;		// Get queue family properties
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefamcount, NULL);
	VkQueueFamilyProperties* queuefamiliesprops = (VkQueueFamilyProperties*)alloca(queuefamcount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefamcount, queuefamiliesprops);
	
	float priorities[MAX_QUEUE_FAMILIES][MAX_QUEUE_COUNT] = { 0 };  // max queue families * max queue count: need a queue_priority for each queue in the queue family we create
	uint32_t queue_infos_count = 0;
	VkDeviceQueueCreateInfo* queue_infos = (VkDeviceQueueCreateInfo*)alloca(queuefamcount * sizeof(VkDeviceQueueCreateInfo));

	const uint32_t maxQueueFlag = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT | VK_QUEUE_PROTECTED_BIT;
	r->vk.availablequeues = (uint32_t**)tc_malloc(r->linkednodecount * sizeof(uint32_t*));
	r->vk.usedqueues = (uint32_t**)tc_malloc(r->linkednodecount * sizeof(uint32_t*));
	for (uint32_t i = 0; i < r->linkednodecount; i++) {
		r->vk.availablequeues[i] = (uint32_t*)tc_calloc(maxQueueFlag, sizeof(uint32_t));
		r->vk.usedqueues[i] = (uint32_t*)tc_calloc(maxQueueFlag, sizeof(uint32_t));
	}
	for (uint32_t i = 0; i < queuefamcount; i++) {
		uint32_t num_queues = queuefamiliesprops[i].queueCount;
		if (num_queues > 0) {	// Request only one queue of each type if mRequestAllAvailableQueues is not set to true
			if (num_queues > 1 && !desc->vk.requestallavailablequeues) num_queues = 1;
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
			for (uint32_t n = 0; n < r->linkednodecount; ++n)
				r->vk.availablequeues[n][queuefamiliesprops[i].queueFlags] = num_queues;
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
	ADD_TO_NEXT_CHAIN(r->gpumode == GPU_MODE_LINKED, info2);
#endif
	CHECK_VKRESULT(vkCreateDevice(r->vk.activegpu, &info2, &alloccbs, &r->vk.device));
	if (r->gpumode != GPU_MODE_UNLINKED)	// Load Vulkan device functions to bypass loader
		volkLoadDevice(r->vk.device);

	r->vk.dedicatedallocationextension = dedicatedalloc && memreqext;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	r->vk.externalmemoryextension = externalMemoryExtension && externalMemoryWin32Extension;
#endif
	if (r->vk.dedicatedallocationextension)
		TRACE(LOG_INFO, "Successfully loaded Dedicated Allocation extension");
	if (r->vk.externalmemoryextension)
		TRACE(LOG_INFO, "Successfully loaded External Memory extension");
#if VK_KHR_draw_indirect_count
	if (r->vk.drawindirectcountextension) {
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountKHR;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountKHR;
		TRACE(LOG_INFO, "Successfully loaded Draw Indirect extension");
	}
	else if (r->vk.AMDdrawindirectcountextension)
#endif
	{
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountAMD;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountAMD;
		TRACE(LOG_INFO, "Successfully loaded AMD Draw Indirect extension");
	}
	if (r->vk.AMDGCNshaderextension)
		TRACE(LOG_INFO, "Successfully loaded AMD GCN Shader extension");
	if (r->vk.descriptorindexingextension)
		TRACE(LOG_INFO, "Successfully loaded Descriptor Indexing extension");
	if (r->vk.raytracingsupported)
		TRACE(LOG_INFO, "Successfully loaded Khronos Ray Tracing extensions");
#ifdef _ENABLE_DEBUG_UTILS_EXTENSION
	r->vk.debugmarkersupport = vkCmdBeginDebugUtilsLabelEXT && vkCmdEndDebugUtilsLabelEXT && vkCmdInsertDebugUtilsLabelEXT && vkSetDebugUtilsObjectNameEXT;
#endif
	vk_utils_caps_builder(r);
	return true;
}

static void remove_device(renderer_t* r)
{
	vkDestroyDescriptorSetLayout(r->vk.device, r->vk.emptydescriptorsetlayout, &alloccbs);
	vkDestroyDescriptorPool(r->vk.device, r->vk.emptydescriptorpool, &alloccbs);
	vkDestroyDevice(r->vk.device, &alloccbs);
	tc_free(r->activegpusettings);
	tc_free(r->vk.activegpuprops);
}

VkDeviceMemory get_vk_device_memory(renderer_t* r, buffer_t* buffer)
{
	VmaAllocationInfo info = { 0 };
	vmaGetAllocationInfo(r->vk.vmaAllocator, buffer->vk.alloc, &info);
	return info.deviceMemory;
}

uint64_t get_vk_device_memory_offset(renderer_t* r, buffer_t* buffer)
{
	VmaAllocationInfo info = { 0 };
	vmaGetAllocationInfo(r->vk.vmaAllocator, buffer->vk.alloc, &info);
	return (uint64_t)info.offset;
}

static uint32_t r_count = 0;

void vk_init_renderer(const char* app_name, const rendererdesc_t* desc, renderer_t** rptr)
{
	TC_ASSERT(app_name && desc && rptr);
	uint8_t* mem = (uint8_t*)tc_memalign(alignof(renderer_t), sizeof(renderer_t) + sizeof(nulldescriptors_t));
	TC_ASSERT(mem);
	memset(mem, 0, sizeof(renderer_t) + sizeof(nulldescriptors_t));

	renderer_t* r = (renderer_t*)mem;
	r->gpumode = desc->gpumode;
	r->shadertarget = desc->shadertarget;
	r->enablegpubasedvalidation = desc->enablegpubasedvalidation;
	r->nulldescriptors = (nulldescriptors_t*)(mem + sizeof(renderer_t));
	r->name = (char*)tc_calloc(strlen(app_name) + 1, sizeof(char));
	strcpy(r->name, app_name);
	TC_ASSERT(desc->gpumode != GPU_MODE_UNLINKED || desc->context); // context required in unlinked mode
	if (desc->context) {
		TC_ASSERT(desc->gpuindex < desc->context->gpucount);
		r->vk.instance = desc->context->vk.instance;
		r->vk.owninstance = false;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		r->vk.debugutilsmessenger = desc->context->vk.debugutilsmessenger;
#else
		r->vk.pVkDebugReport = desc->context->vk.pVkDebugReport;
#endif
		r->unlinkedrendererindex = r_count;
	}
	else if (!init_common(app_name, desc, r)) {
		tc_free(r->name);
		tc_freealign(r);
		return;
	}
	if (!add_device(desc, r)) {
		if (r->vk.owninstance) exit_common(r);
		tc_free(r->name);
		tc_freealign(r);
		return;
	}
	VmaAllocatorCreateInfo info = { 0 };
	info.device = r->vk.device;
	info.physicalDevice = r->vk.activegpu;
	info.instance = r->vk.instance;
	if (r->vk.dedicatedallocationextension)
		info.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
	if (r->vk.bufferdeviceaddressextension)
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
	vmaCreateAllocator(&info, &r->vk.vmaAllocator);

	// Empty descriptor set for filling in gaps when example: set 1 is used but set 0 is not used in the shader.
	// We still need to bind empty descriptor set here to keep some drivers happy
	VkDescriptorPoolSize descriptorPoolSizes[1] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1 } };
	add_descriptor_pool(r, 1, 0, descriptorPoolSizes, 1, &r->vk.emptydescriptorpool);
	VkDescriptorSetLayoutCreateInfo info3 = { 0 };
	VkDescriptorSet* emptySets[] = { &r->vk.emptydescriptorset };
	info3.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	CHECK_VKRESULT(vkCreateDescriptorSetLayout(r->vk.device, &info3, &alloccbs, &r->vk.emptydescriptorsetlayout));
	consume_descriptor_sets(r->vk.device, r->vk.emptydescriptorpool, &r->vk.emptydescriptorsetlayout, 1, emptySets);

	VkPhysicalDeviceFeatures2KHR gpufeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };
	vkGetPhysicalDeviceFeatures2KHR(r->vk.activegpu, &gpufeats);
	r->vk.shadersampledimagearraydynamicindexingsupported = (uint32_t)(gpufeats.features.shaderSampledImageArrayDynamicIndexing);
	if (r->vk.shadersampledimagearraydynamicindexingsupported)
		TRACE(LOG_INFO, "GPU supports texture array dynamic indexing");

	util_find_queue_family_index(r, 0, QUEUE_TYPE_GRAPHICS, NULL, &r->vk.graphicsqueuefamilyidx, NULL);
	util_find_queue_family_index(r, 0, QUEUE_TYPE_COMPUTE, NULL, &r->vk.computequeuefamilyidx, NULL);
	util_find_queue_family_index(r, 0, QUEUE_TYPE_TRANSFER, NULL, &r->vk.transferqueuefamilyidx, NULL);
	add_default_resources(r);
	++r_count;
	TC_ASSERT(r_count <= MAX_UNLINKED_GPUS);
	*rptr = r;
}

void vk_exit_renderer(renderer_t* r)
{
	TC_ASSERT(r);
	--r_count;
	remove_default_resources(r);
	vmaDestroyAllocator(r->vk.vmaAllocator);
	remove_device(r);
	if (r->vk.owninstance)
		exit_common(r);

	for (uint32_t i = 0; i < r->linkednodecount; i++) {
		tc_free(r->vk.availablequeues[i]);
		tc_free(r->vk.usedqueues[i]);
	}
	tc_free(r->vk.availablequeues);
	tc_free(r->vk.usedqueues);
	tc_free(r->capbits);
	tc_free(r->name);
	tc_free(r);
}

void vk_add_fence(renderer_t* r, fence_t* fence)
{
	TC_ASSERT(r && fence);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	memset(fence, 0, sizeof(fence_t));
	VkFenceCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	CHECK_VKRESULT(vkCreateFence(r->vk.device, &info, &alloccbs, &fence->vk.fence));
	fence->vk.submitted = false;
}

void vk_remove_fence(renderer_t* r, fence_t* fence)
{
	TC_ASSERT(r && fence);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != fence->vk.fence);
	vkDestroyFence(r->vk.device, fence->vk.fence, &alloccbs);
}

void vk_add_semaphore(renderer_t* r, semaphore_t* semaphore)
{
	TC_ASSERT(r && semaphore);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	VkSemaphoreCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	CHECK_VKRESULT(vkCreateSemaphore(r->vk.device, &info, &alloccbs, &(semaphore->vk.semaphore)));
	semaphore->vk.signaled = false;	// Set signal initial state.
}

void vk_remove_semaphore(renderer_t* r, semaphore_t* semaphore)
{
	TC_ASSERT(r && semaphore);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != semaphore->vk.semaphore);
	vkDestroySemaphore(r->vk.device, semaphore->vk.semaphore, &alloccbs);
}

void vk_add_queue(renderer_t* r, queuedesc_t* desc, queue_t* queue)
{
	TC_ASSERT(r && desc && queue);
	const uint32_t nodeidx = (r->gpumode == GPU_MODE_LINKED) ? desc->nodeidx : 0;
	VkQueueFamilyProperties props = { 0 };
	uint8_t famidx = UINT8_MAX;
	uint8_t queueidx = UINT8_MAX;
	util_find_queue_family_index(r, nodeidx, desc->type, &props, &famidx, &queueidx);
	++r->vk.usedqueues[nodeidx][props.queueFlags];
	memset(queue, 0, sizeof(queue_t));
	queue->vk.queuefamilyindex = famidx;
	queue->nodeidx = desc->nodeidx;
	queue->type = desc->type;
	queue->vk.queueindex = queueidx;
	queue->vk.gpumode = r->gpumode;
	queue->vk.timestampperiod = r->vk.activegpuprops->properties.limits.timestampPeriod;
	queue->vk.flags = props.queueFlags;
	queue->vk.submitlck = &r->nulldescriptors->submitlck;
	if (r->gpumode == GPU_MODE_UNLINKED)
		queue->nodeidx = r->unlinkedrendererindex;
	
	vkGetDeviceQueue(r->vk.device, queue->vk.queuefamilyindex, queue->vk.queuefamilyindex, &queue->vk.queue);
	TC_ASSERT(VK_NULL_HANDLE != queue->vk.queue);
}

void vk_remove_queue(renderer_t* r, queue_t* queue)
{
	TC_ASSERT(r && queue);
	const uint32_t nodeidx = r->gpumode == GPU_MODE_LINKED ? queue->nodeidx : 0;
	const VkQueueFlags flags = queue->vk.flags;
	--r->vk.usedqueues[nodeidx][flags];
}

void vk_add_cmdpool(renderer_t* r, const cmdpooldesc_t* desc, cmdpool_t* pool)
{
	TC_ASSERT(r);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(pool);
	pool->queue = desc->queue;
	VkCommandPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.queueFamilyIndex = desc->queue->vk.queuefamilyindex;
	if (desc->transient) info.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	CHECK_VKRESULT(vkCreateCommandPool(r->vk.device, &info, &alloccbs, &(pool->cmdpool)));
}

void vk_remove_cmdpool(renderer_t* r, cmdpool_t* pool)
{
	TC_ASSERT(r);
	TC_ASSERT(pool);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != pool->cmdpool);
	vkDestroyCommandPool(r->vk.device, pool->cmdpool, &alloccbs);
}

void vk_add_cmds(renderer_t* r, const cmddesc_t* desc, uint32_t count, cmd_t* cmds)
{
	TC_ASSERT(r && desc);
	TC_ASSERT(count);
	TC_ASSERT(cmds);
	VkCommandBufferAllocateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = NULL;
	info.commandBufferCount = 1;
	for (uint32_t i = 0; i < count; ++i) {
		cmd_t* cmd = &cmds[i];
		cmd->renderer = r;
		cmd->queue = desc->pool->queue;
		cmd->vk.cmdpool = desc->pool;
		cmd->vk.type = desc->pool->queue->type;
		cmd->vk.nodeidx = desc->pool->queue->nodeidx;
		info.commandPool = desc->pool->cmdpool;
		info.level = desc->secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		CHECK_VKRESULT(vkAllocateCommandBuffers(r->vk.device, &info, &(cmd->vk.cmdbuf)));
	}
}

void vk_remove_cmds(renderer_t* r, uint32_t count, cmd_t* cmds)
{
	TC_ASSERT(r && cmds);
	for (uint32_t i = 0; i < count; ++i) {
		TC_ASSERT(VK_NULL_HANDLE != cmds[i].renderer->vk.device);
		TC_ASSERT(VK_NULL_HANDLE != cmds[i].vk.cmdbuf);
		vkFreeCommandBuffers(r->vk.device, cmds[i].vk.cmdpool->cmdpool, 1, &(cmds[i].vk.cmdbuf));
	}
}

void vk_toggle_vsync(renderer_t* r, swapchain_t* swapchain)
{
	queue_t queue = { 0 };
	queue.vk.queuefamilyindex = swapchain->vk.presentqueuefamilyindex;
	queue_t* queues[] = { &queue };
	swapchaindesc_t desc = *swapchain->vk.desc;
	desc.vsync = !desc.vsync;	//toggle vsync on or off
	desc.presentqueuecount = 1;
	desc.presentqueues = queues;
	remove_swapchain(r, swapchain);
	add_swapchain(r, &desc, swapchain);
}

void vk_add_swapchain(renderer_t* r, const swapchaindesc_t* desc, swapchain_t* swapchain)
{
	TC_ASSERT(r && desc && swapchain);
	TC_ASSERT(desc->imagecount <= MAX_SWAPCHAIN_IMAGES);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.instance);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.activegpu);
	VkSurfaceKHR surface;
	VkResult err = glfwCreateWindowSurface(r->vk.instance, desc->window, &alloccbs, &surface);
	if (err) {
		TRACE(LOG_ERROR, "Failed to create a window surface");
		TC_ASSERT(0);
	}
	if (0 == desc->imagecount) ((swapchaindesc_t*)desc)->imagecount = 2;
	VkSurfaceCapabilitiesKHR caps = { 0 };
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->vk.activegpu, surface, &caps));
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
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(r->vk.activegpu, surface, &surfacefmtcount, NULL));

	// Allocate and get surface formats
	formats = (VkSurfaceFormatKHR*)tc_calloc(surfacefmtcount, sizeof(*formats));
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(r->vk.activegpu, surface, &surfacefmtcount, formats));
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
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(r->vk.activegpu, surface, &swapChainImageCount, NULL));
	modes = (VkPresentModeKHR*)alloca(swapChainImageCount * sizeof(*modes));		// Allocate and get present modes
	CHECK_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(r->vk.activegpu, surface, &swapChainImageCount, modes));
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
	uint32_t famindices[2] = { desc->presentqueues[0]->vk.queuefamilyindex, 0 };
	uint32_t famidx = -1;
	uint32_t queuefampropscount = 0;				
	VkQueueFamilyProperties* queuefamprops = NULL;
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefampropscount, NULL);		// Get queue family properties
	queuefamprops = (VkQueueFamilyProperties*)alloca(queuefampropscount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(r->vk.activegpu, &queuefampropscount, queuefamprops);

	if (queuefampropscount) {			// Check if hardware provides dedicated present queue
		for (uint32_t i = 0; i < queuefampropscount; i++) {
			VkBool32 supports_present = VK_FALSE;
			VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(r->vk.activegpu, i, surface, &supports_present);
			if ((VK_SUCCESS == res) && (VK_TRUE == supports_present) && desc->presentqueues[0]->vk.queuefamilyindex != i) {
				famidx = i;
				break;
			}
		}
		if (famidx == UINT32_MAX) {		// If there is no dedicated present queue, just find the first available queue which supports present
			for (uint32_t i = 0; i < queuefampropscount; i++) {
				VkBool32 supports_present = VK_FALSE;
				VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(r->vk.activegpu, i, surface, &supports_present);
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
		vkGetDeviceQueue(r->vk.device, famindices[0], 0, &presentQueue);
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
	CHECK_VKRESULT(vkCreateSwapchainKHR(r->vk.device, &info, &alloccbs, &vkSwapchain));

	((swapchaindesc_t*)desc)->colorformat = TinyImageFormat_FromVkFormat((TinyImageFormat_VkFormat)surface_format.format);
	uint32_t numimages = 0;		// Create rendertargets from swapchain
	CHECK_VKRESULT(vkGetSwapchainImagesKHR(r->vk.device, vkSwapchain, &numimages, NULL));
	TC_ASSERT(numimages >= desc->imagecount);
	VkImage* images = (VkImage*)alloca(numimages * sizeof(VkImage));
	CHECK_VKRESULT(vkGetSwapchainImagesKHR(r->vk.device, vkSwapchain, &numimages, images));
	uint8_t* mem = (uint8_t*)tc_calloc(1, numimages * sizeof(rendertarget_t*) + sizeof(swapchaindesc_t));
	TC_ASSERT(mem);
	swapchain->rts = (rendertarget_t**)mem;
	swapchain->vk.desc = (swapchaindesc_t*)(swapchain->rts + numimages);

	rendertargetdesc_t rtdesc = { 0 };
	rtdesc.width = desc->width;
	rtdesc.height = desc->height;
	rtdesc.depth = 1;
	rtdesc.arraysize = 1;
	rtdesc.format = desc->colorformat;
	rtdesc.clearval = desc->colorclearval;
	rtdesc.samplecount = SAMPLE_COUNT_1;
	rtdesc.samplequality = 0;
	rtdesc.state = RESOURCE_STATE_PRESENT;
	rtdesc.nodeidx = r->unlinkedrendererindex;

	char buffer[32] = { 0 };
	for (uint32_t i = 0; i < numimages; ++i) {	// Populate the vk_image field and add the Vulkan texture objects
		sprintf(buffer, "Swapchain RT[%u]", i);
		rtdesc.name = buffer;
		rtdesc.nativehandle = (void*)images[i];
		add_rendertarget(r, &rtdesc, &swapchain->rts[i]);
	}
	*swapchain->vk.desc = *desc;
	swapchain->vsync = desc->vsync;
	swapchain->imagecount = numimages;
	swapchain->vk.surface = surface;
	swapchain->vk.presentqueuefamilyindex = finalPresentQueueFamilyIndex;
	swapchain->vk.presentqueue = presentQueue;
	swapchain->vk.swapchain = vkSwapchain;
}

void vk_remove_swapchain(renderer_t* r, swapchain_t* swapchain)
{
	TC_ASSERT(r && swapchain);
	for (uint32_t i = 0; i < swapchain->imagecount; i++)
		remove_rendertarget(r, swapchain->rts[i]);

	vkDestroySwapchainKHR(r->vk.device, swapchain->vk.swapchain, &alloccbs);
	vkDestroySurfaceKHR(r->vk.instance, swapchain->vk.surface, &alloccbs);
	tc_free(swapchain->rts);
}

void vk_add_buffer(renderer_t* r, const bufferdesc_t* desc, buffer_t* buffer)
{
	TC_ASSERT(r && desc);
	TC_ASSERT(desc->size > 0);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(r->gpumode != GPU_MODE_UNLINKED || desc->nodeidx == r->unlinkedrendererindex);
	memset(buffer, 0, sizeof(buffer_t));
	uint64_t size = desc->size;
	if (desc->descriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) {	// Align the buffer size to multiples of the dynamic uniform buffer minimum size
		uint64_t minAlignment = r->activegpusettings->uniformbufalignment;
		size = round_up_64(size, minAlignment);
	}
	VkBufferCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = util_to_vk_buffer_usage(desc->descriptors, desc->format != TinyImageFormat_UNDEFINED);
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (desc->memusage == RESOURCE_MEMORY_USAGE_GPU_ONLY || desc->memusage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU)
		info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;			// Buffer can be used as dest in a transfer command (Uploading data to a storage buffer, Readback query data)

	const bool linkedMultiGpu = (r->gpumode == GPU_MODE_LINKED && (desc->sharednodeindices || desc->nodeidx));

	VmaAllocationCreateInfo vma_mem_reqs = { 0 };
	vma_mem_reqs.usage = (VmaMemoryUsage)desc->memusage;
	if (desc->flags & BUFFER_CREATION_FLAG_OWN_MEMORY_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	if (desc->flags & BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	if (linkedMultiGpu)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
	if (desc->flags & BUFFER_CREATION_FLAG_HOST_VISIBLE)
		vma_mem_reqs.requiredFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	if (desc->flags & BUFFER_CREATION_FLAG_HOST_COHERENT)
		vma_mem_reqs.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

#if defined(ANDROID)
	if (vma_mem_reqs.usage != VMA_MEMORY_USAGE_GPU_TO_CPU) {
		vma_mem_reqs.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	}
#endif
	VmaAllocationInfo info2 = { 0 };
	CHECK_VKRESULT(vmaCreateBuffer(r->vk.vmaAllocator, &info, &vma_mem_reqs, &buffer->vk.buffer, &buffer->vk.alloc, &info2));
	buffer->data = info2.pMappedData;
	if (linkedMultiGpu)	{	// Buffer to be used on multiple GPUs
		VmaAllocationInfo info3 = { 0 };
		vmaGetAllocationInfo(r->vk.vmaAllocator, buffer->vk.alloc, &info3);
		// Set all the device indices to the index of the device where we will create the buffer
		uint32_t* idxs = (uint32_t*)alloca(r->linkednodecount * sizeof(uint32_t));
		util_calculate_device_indices(r, desc->nodeidx, desc->sharednodeindices, desc->sharednodeidxcount, idxs);
		VkBindBufferMemoryInfoKHR bind_info = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR };
		VkBindBufferMemoryDeviceGroupInfoKHR group = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO_KHR };
		group.deviceIndexCount = r->linkednodecount;
		group.pDeviceIndices = idxs;
		bind_info.buffer = buffer->vk.buffer;
		bind_info.memory = info3.deviceMemory;
		bind_info.memoryOffset = info3.offset;
		bind_info.pNext = &group;
		CHECK_VKRESULT(vkBindBufferMemory2KHR(r->vk.device, 1, &bind_info));
	}
	if ((desc->descriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) || (desc->descriptors & DESCRIPTOR_TYPE_BUFFER) || (desc->descriptors & DESCRIPTOR_TYPE_RW_BUFFER))
		if ((desc->descriptors & DESCRIPTOR_TYPE_BUFFER) || (desc->descriptors & DESCRIPTOR_TYPE_RW_BUFFER))
			buffer->vk.offset = desc->stride * desc->first;

	if (info.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
		VkBufferViewCreateInfo view_info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		view_info.buffer = buffer->vk.buffer;
		view_info.flags = 0;
		view_info.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
		view_info.offset = desc->first * desc->stride;
		view_info.range = desc->count * desc->stride;
		VkFormatProperties formatProps = { 0 };
		vkGetPhysicalDeviceFormatProperties(r->vk.activegpu, view_info.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
			TRACE(LOG_WARNING, "Failed to create uniform texel buffer view for format %u", (uint32_t)desc->format);
		else CHECK_VKRESULT(vkCreateBufferView(r->vk.device, &view_info, &alloccbs, &buffer->vk.uniformtexelview));
	}
	if (info.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
		VkBufferViewCreateInfo view_info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		view_info.buffer = buffer->vk.buffer;
		view_info.flags = 0;
		view_info.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
		view_info.offset = desc->first * desc->stride;
		view_info.range = desc->count * desc->stride;
		VkFormatProperties formatProps = { 0 };
		vkGetPhysicalDeviceFormatProperties(r->vk.activegpu, view_info.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
			TRACE(LOG_WARNING, "Failed to create storage texel buffer view for format %u", (uint32_t)desc->format);
		else CHECK_VKRESULT(vkCreateBufferView(r->vk.device, &view_info, &alloccbs, &buffer->vk.storagetexelview));
	}
#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->pName) set_buffername(r, buffer, desc->pName);
#endif
	buffer->size = (uint32_t)desc->size;
	buffer->memusage = desc->memusage;
	buffer->nodeidx = desc->nodeidx;
	buffer->descriptors = desc->descriptors;
}

void vk_remove_buffer(renderer_t* r, buffer_t* buffer)
{
	TC_ASSERT(r && buffer);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != buffer->vk.buffer);
	if (buffer->vk.uniformtexelview) {
		vkDestroyBufferView(r->vk.device, buffer->vk.uniformtexelview, &alloccbs);
		buffer->vk.uniformtexelview = VK_NULL_HANDLE;
	}
	if (buffer->vk.storagetexelview) {
		vkDestroyBufferView(r->vk.device, buffer->vk.storagetexelview, &alloccbs);
		buffer->vk.storagetexelview = VK_NULL_HANDLE;
	}
	vmaDestroyBuffer(r->vk.vmaAllocator, buffer->vk.buffer, buffer->vk.alloc);
}


struct handleinfo_s {
	void* handle;
	VkExternalMemoryHandleTypeFlagBitsKHR handletype;
};

void vk_add_texture(renderer_t* r, const texturedesc_t* desc, texture_t* texture)
{
	TC_ASSERT(r && desc->width && desc->height && (desc->depth || desc->arraysize));
	TC_ASSERT(r->gpumode != GPU_MODE_UNLINKED || desc->nodeidx == r->unlinkedrendererindex);
	if (desc->samplecount > SAMPLE_COUNT_1 && desc->miplevels > 1) {
		TRACE(LOG_ERROR, "Multi-Sampled textures cannot have mip maps");
		TC_ASSERT(false);
		return;
	}
	size_t descsize = (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE ? (desc->miplevels * sizeof(VkImageView)) : 0);
	uint8_t* mem = (uint8_t*)tc_calloc(1, descsize);
	TC_ASSERT(mem);
	if (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
		texture->vk.UAVdescriptors = (VkImageView*)mem;
	if (desc->nativehandle && !(desc->flags & TEXTURE_CREATION_FLAG_IMPORT_BIT)) {
		texture->ownsimage = false;
		texture->vk.image = (VkImage)desc->nativehandle;
	}
	else texture->ownsimage = true;

	VkImageUsageFlags added_flags = 0;
	if (desc->state & RESOURCE_STATE_RENDER_TARGET) added_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	else if (desc->state & RESOURCE_STATE_DEPTH_WRITE) added_flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    uint32_t size = desc->arraysize;
	VkImageType image_type = VK_IMAGE_TYPE_MAX_ENUM;
	if (desc->flags & TEXTURE_CREATION_FLAG_FORCE_2D) {
		TC_ASSERT(desc->depth == 1);
		image_type = VK_IMAGE_TYPE_2D;
	}
	else if (desc->flags & TEXTURE_CREATION_FLAG_FORCE_3D)
		image_type = VK_IMAGE_TYPE_3D;
	else {
		if (desc->depth > 1) image_type = VK_IMAGE_TYPE_3D;
		else if (desc->height > 1) image_type = VK_IMAGE_TYPE_2D;
		else image_type = VK_IMAGE_TYPE_1D;
	}
	descriptortype_t descriptors = desc->descriptors;
	bool cubemap = (DESCRIPTOR_TYPE_TEXTURE_CUBE == (descriptors & DESCRIPTOR_TYPE_TEXTURE_CUBE));
	bool array = false;
	const bool planar = TinyImageFormat_IsPlanar(desc->format);
	const uint32_t numOfPlanes = TinyImageFormat_NumOfPlanes(desc->format);
	const bool single_plane = TinyImageFormat_IsSinglePlane(desc->format);
	TC_ASSERT(
		((single_plane && numOfPlanes == 1) || (!single_plane && numOfPlanes > 1 && numOfPlanes <= MAX_PLANE_COUNT)) &&
		"Number of planes for multi-planar formats must be 2 or 3 and for single-planar formats it must be 1.");

	if (image_type == VK_IMAGE_TYPE_3D) array = true;
	if (VK_NULL_HANDLE == texture->vk.image) {
		VkImageCreateInfo info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.imageType = image_type;
		info.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
		info.extent.width = desc->width;
		info.extent.height = desc->height;
		info.extent.depth = desc->depth;
		info.mipLevels = desc->miplevels;
		info.arrayLayers = size;
		info.samples = util_to_vk_sample_count(desc->samplecount);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = util_to_vk_image_usage(descriptors);
		info.usage |= added_flags;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (cubemap) info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		if (array) info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;

		VkFormatProperties props = { 0 };
		vkGetPhysicalDeviceFormatProperties(r->vk.activegpu, info.format, &props);
		if (planar) {   // multi-planar formats must have each plane separately bound to memory, rather than having a single memory binding for the whole image
			TC_ASSERT(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DISJOINT_BIT);
			info.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
		}
		if ((VK_IMAGE_USAGE_SAMPLED_BIT & info.usage) || (VK_IMAGE_USAGE_STORAGE_BIT & info.usage))	// Make it easy to copy to and from textures
			info.usage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		TC_ASSERT(r->capbits->canshaderreadfrom[desc->format] && "GPU shader can't' read from this format");

		VkFormatFeatureFlags format_features = util_vk_image_usage_to_format_features(info.usage);	// Verify that GPU supports this format
		VkFormatFeatureFlags flags = props.optimalTilingFeatures & format_features;
		TC_ASSERT((0 != flags) && "Format is not supported for GPU local images (i.e. not host visible images)");
		const bool linked = (r->gpumode == GPU_MODE_LINKED) && (desc->sharednodeindices || desc->nodeidx);
		VmaAllocationCreateInfo mem_reqs = { 0 };
		if (desc->flags & TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT)
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		if (linked) mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
		mem_reqs.usage = (VmaMemoryUsage)VMA_MEMORY_USAGE_GPU_ONLY;

		VkExternalMemoryImageCreateInfoKHR externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR, NULL };
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VkImportMemoryWin32HandleInfoKHR importInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, NULL };
#endif
		VkExportMemoryAllocateInfoKHR exportMemoryInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR, NULL };
		if (r->vk.externalmemoryextension && desc->flags & TEXTURE_CREATION_FLAG_IMPORT_BIT) {
			info.pNext = &externalInfo;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
			struct handleinfo_s* hinfo = (struct handleinfo_s*)desc->nativehandle;
			importInfo.handle = hinfo->handle;
			importInfo.handleType = hinfo->handletype;
			externalInfo.handleTypes = hinfo->handletype;
			mem_reqs.pUserData = &importInfo;
			// Allocate external (importable / exportable) memory as dedicated memory to avoid adding unnecessary complexity to the Vulkan Memory Allocator
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
#endif
		}
		else if (r->vk.externalmemoryextension && desc->flags & TEXTURE_CREATION_FLAG_EXPORT_BIT) {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
			exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#endif
			mem_reqs.pUserData = &exportMemoryInfo;
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}
		if (desc->flags & TEXTURE_CREATION_FLAG_ON_TILE) {	// If lazy allocation is requested, check that the hardware supports it
			uint32_t idx = 0;
			VmaAllocationCreateInfo lazyMemReqs = mem_reqs;
			lazyMemReqs.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			VkResult result = vmaFindMemoryTypeIndex(r->vk.vmaAllocator, UINT32_MAX, &lazyMemReqs, &idx);
			if (VK_SUCCESS == result) {
				mem_reqs = lazyMemReqs;
				info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
				// The Vulkan spec states: If usage includes VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
				// then bits other than VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				// and VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT must not be set
				info.usage &= (VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
				texture->lazilyallocated = true;
			}
		}
		VmaAllocationInfo info2 = { 0 };
		if (single_plane) {
			CHECK_VKRESULT(vmaCreateImage(r->vk.vmaAllocator, &info, &mem_reqs, &texture->vk.image, &texture->vk.alloc, &info2));
		}
		else {   // Multi-planar formats
			// Create info requires the mutable format flag set for multi planar images
			// Also pass the format list for mutable formats as per recommendation from the spec
			// Might help to keep DCC enabled if we ever use this as a output format
			// DCC gets disabled when we pass mutable format bit to the create info. Passing the format list helps the driver to enable it
			VkFormat planar = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
			VkImageFormatListCreateInfoKHR formatList = { 0 };
			formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
			formatList.pNext = NULL;
			formatList.pViewFormats = &planar;
			formatList.viewFormatCount = 1;
			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			info.pNext = &formatList;    //-V506

			// Create Image
			CHECK_VKRESULT(vkCreateImage(r->vk.device, &info, &alloccbs, &texture->vk.image));

			VkMemoryRequirements vkmemreq = { 0 };
			uint64_t             planesOffsets[MAX_PLANE_COUNT] = { 0 };
			util_get_planar_vk_image_memory_requirement(
				r->vk.device, texture->vk.image, numOfPlanes, &vkmemreq, planesOffsets);

			// Allocate image memory
			VkMemoryAllocateInfo mem_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			mem_info.allocationSize = vkmemreq.size;
			VkPhysicalDeviceMemoryProperties props = { 0 };
			vkGetPhysicalDeviceMemoryProperties(r->vk.activegpu, &props);
			mem_info.memoryTypeIndex = _get_memory_type(vkmemreq.memoryTypeBits, &props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			CHECK_VKRESULT(vkAllocateMemory(r->vk.device, &mem_info, &alloccbs, &texture->vk.devicemem));

			// Bind planes to their memories
			VkBindImageMemoryInfo bindImagesMemoryInfo[MAX_PLANE_COUNT];
			VkBindImagePlaneMemoryInfo bindImagePlanesMemoryInfo[MAX_PLANE_COUNT];
			for (uint32_t i = 0; i < numOfPlanes; i++) {
				VkBindImagePlaneMemoryInfo* bind_info = &bindImagePlanesMemoryInfo[i];
				bind_info->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
				bind_info->pNext = NULL;
				bind_info->planeAspect = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);

				VkBindImageMemoryInfo* bind_info2 = &bindImagesMemoryInfo[i];
				bind_info2->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
				bind_info2->pNext = &bind_info;
				bind_info2->image = texture->vk.image;
				bind_info2->memory = texture->vk.devicemem;
				bind_info2->memoryOffset = planesOffsets[i];
			}
			CHECK_VKRESULT(vkBindImageMemory2(r->vk.device, numOfPlanes, bindImagesMemoryInfo));
		}
		// Texture to be used on multiple GPUs
		if (linked) {
			VmaAllocationInfo info3 = { 0 };
			vmaGetAllocationInfo(r->vk.vmaAllocator, texture->vk.alloc, &info3);
			// Set all the device indices to the index of the device where we will create the texture
			uint32_t* pIndices = (uint32_t*)alloca(r->linkednodecount * sizeof(uint32_t));
			util_calculate_device_indices(r, desc->nodeidx, desc->sharednodeindices, desc->sharednodeidxcount, pIndices);
			VkBindImageMemoryInfoKHR            bind_info = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR };
			VkBindImageMemoryDeviceGroupInfoKHR bindDeviceGroup = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO_KHR };
			bindDeviceGroup.deviceIndexCount = r->linkednodecount;
			bindDeviceGroup.pDeviceIndices = pIndices;
			bind_info.image = texture->vk.image;
			bind_info.memory = info3.deviceMemory;
			bind_info.memoryOffset = info3.offset;
			bind_info.pNext = &bindDeviceGroup;
			CHECK_VKRESULT(vkBindImageMemory2KHR(r->vk.device, 1, &bind_info));
		}
	}
	// Create image view
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch (image_type) {
		case VK_IMAGE_TYPE_1D: view_type = size > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D; break;
		case VK_IMAGE_TYPE_2D:
			if (cubemap) view_type = (size > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
			else view_type = size > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			break;
		case VK_IMAGE_TYPE_3D:
			if (size > 1) {
				TRACE(LOG_ERROR, "Cannot support 3D Texture Array in Vulkan");
				TC_ASSERT(false);
			}
			view_type = VK_IMAGE_VIEW_TYPE_3D;
			break;
		default: TC_ASSERT(false && "Image Format not supported!"); break;
	}
	TC_ASSERT(view_type != VK_IMAGE_VIEW_TYPE_MAX_ENUM && "Invalid Image View");

	VkImageViewCreateInfo srv_desc = { 0 };
	srv_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;	
	srv_desc.pNext = NULL;
	srv_desc.flags = 0;
	srv_desc.image = texture->vk.image;
	srv_desc.viewType = view_type;
	srv_desc.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
	srv_desc.components.r = VK_COMPONENT_SWIZZLE_R;
	srv_desc.components.g = VK_COMPONENT_SWIZZLE_G;
	srv_desc.components.b = VK_COMPONENT_SWIZZLE_B;
	srv_desc.components.a = VK_COMPONENT_SWIZZLE_A;
	srv_desc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(srv_desc.format, false);
	srv_desc.subresourceRange.baseMipLevel = 0;
	srv_desc.subresourceRange.levelCount = desc->miplevels;
	srv_desc.subresourceRange.baseArrayLayer = 0;
	srv_desc.subresourceRange.layerCount = size;
	texture->aspectmask = util_vk_determine_aspect_mask(srv_desc.format, true);
	if (desc->samplerycbcrconversioninfo)
		srv_desc.pNext = desc->samplerycbcrconversioninfo;

	if (descriptors & DESCRIPTOR_TYPE_TEXTURE)
		CHECK_VKRESULT(vkCreateImageView(r->vk.device, &srv_desc, &alloccbs, &texture->vk.SRVdescriptor));
	// SRV stencil
	if ((TinyImageFormat_HasStencil(desc->format)) && (descriptors & DESCRIPTOR_TYPE_TEXTURE)) {
		srv_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		CHECK_VKRESULT(vkCreateImageView(r->vk.device, &srv_desc, &alloccbs, &texture->vk.SRVstencildescriptor));
	}
	// UAV
	if (descriptors & DESCRIPTOR_TYPE_RW_TEXTURE) {
		VkImageViewCreateInfo uav_desc = srv_desc;
		// All cubemaps will be used as image2DArray for Image Load / Store ops
		if (uav_desc.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || uav_desc.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
			uav_desc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		uav_desc.subresourceRange.levelCount = 1;
		for (uint32_t i = 0; i < desc->miplevels; ++i) {
			uav_desc.subresourceRange.baseMipLevel = i;
			CHECK_VKRESULT(vkCreateImageView(r->vk.device, &uav_desc, &alloccbs, &texture->vk.UAVdescriptors[i]));
		}
	}
	texture->nodeidx = desc->nodeidx;
	texture->width = desc->width;
	texture->height = desc->height;
	texture->depth = desc->depth;
	texture->miplevels = desc->miplevels;
	texture->uav = desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE;
	texture->extraarraysize = size - 1;
	texture->format = desc->format;
	texture->samplecount = desc->samplecount;
#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->name) set_texturename(r, pTexture, desc->name);
#endif
}

void vk_remove_texture(renderer_t* r, texture_t* texture)
{
	TC_ASSERT(r && texture);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != texture->vk.image);
	if (texture->ownsimage) {
		const TinyImageFormat fmt = (TinyImageFormat)texture->format;
		const bool single_plane = TinyImageFormat_IsSinglePlane(fmt);
		if (single_plane)
			vmaDestroyImage(r->vk.vmaAllocator, texture->vk.image, texture->vk.alloc);
		else {
			vkDestroyImage(r->vk.device, texture->vk.image, &alloccbs);
			vkFreeMemory(r->vk.device, texture->vk.devicemem, &alloccbs);
		}
	}
	if (VK_NULL_HANDLE != texture->vk.SRVdescriptor)
		vkDestroyImageView(r->vk.device, texture->vk.SRVdescriptor, &alloccbs);
	if (VK_NULL_HANDLE != texture->vk.SRVstencildescriptor)
		vkDestroyImageView(r->vk.device, texture->vk.SRVstencildescriptor, &alloccbs);
	if (texture->vk.UAVdescriptors)
		for (uint32_t i = 0; i < texture->miplevels; ++i)
			vkDestroyImageView(r->vk.device, texture->vk.UAVdescriptors[i], &alloccbs);

	if (texture->vt) remove_virtualtexture(r, texture->vt);
	tc_free(texture->vk.UAVdescriptors);
}

void vk_add_rendertarget(renderer_t* r, const rendertargetdesc_t* desc, rendertarget_t* rendertarget)
{
	TC_ASSERT(r && desc && rendertarget);
	TC_ASSERT(r->gpumode != GPU_MODE_UNLINKED || desc->nodeidx == r->unlinkedrendererindex);
	bool const isdepth = TinyImageFormat_IsDepthOnly(desc->format) || TinyImageFormat_IsDepthAndStencil(desc->format);
	TC_ASSERT(!((isdepth) && (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)) && "Cannot use depth stencil as UAV");
	((rendertargetdesc_t*)desc)->miplevels = max(1U, desc->miplevels);

    uint32_t size = desc->arraysize;
	uint32_t depthorarraysize = size * desc->depth;
	uint32_t numrtvs = desc->miplevels;
	if ((desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		numrtvs *= depthorarraysize;
	uint8_t* mem = (uint8_t*)tc_calloc(numrtvs, sizeof(VkImageView));
	TC_ASSERT(mem);
	rendertarget->vk.slicedescriptors = (VkImageView*)mem;

	rendertarget->vk.id = atomic_fetch_add_explicit(&rtids, 1, memory_order_relaxed);

	texturedesc_t tex_desc = { 0 };
	tex_desc.arraysize = size;
	tex_desc.clearval = desc->clearval;
	tex_desc.depth = desc->depth;
	tex_desc.flags = desc->flags;
	tex_desc.format = desc->format;
	tex_desc.height = desc->height;
	tex_desc.miplevels = desc->miplevels;
	tex_desc.samplecount = desc->samplecount;
	tex_desc.samplequality = desc->samplequality;
	tex_desc.width = desc->width;
	tex_desc.nativehandle = desc->nativehandle;
	tex_desc.nodeidx = desc->nodeidx;
	tex_desc.sharednodeindices = desc->sharednodeindices;
	tex_desc.sharednodeidxcount = desc->sharednodeidxcount;
	if (!isdepth) tex_desc.state |= RESOURCE_STATE_RENDER_TARGET;
	else tex_desc.state |= RESOURCE_STATE_DEPTH_WRITE;
	tex_desc.descriptors = desc->descriptors;			// Set this by default to be able to sample the rendertarget in shader
	
	if (!(desc->flags & TEXTURE_CREATION_FLAG_ON_TILE))	// Create SRV by default for a render target unless this is on tile texture where SRV is not supported
		tex_desc.descriptors |= DESCRIPTOR_TYPE_TEXTURE;
	else {
		if ((tex_desc.descriptors & DESCRIPTOR_TYPE_TEXTURE) || (tex_desc.descriptors & DESCRIPTOR_TYPE_RW_TEXTURE))
			TRACE(LOG_WARNING, "On tile textures do not support DESCRIPTOR_TYPE_TEXTURE or DESCRIPTOR_TYPE_RW_TEXTURE");
		// On tile textures do not support SRV/UAV as there is no backing memory
		// You can only read these textures as input attachments inside same render pass
		tex_desc.descriptors &= (descriptortype_t)(~(DESCRIPTOR_TYPE_TEXTURE | DESCRIPTOR_TYPE_RW_TEXTURE));
	}
	if (isdepth) {										// Make sure depth/stencil format is supported - fall back to VK_FORMAT_D16_UNORM if not
		VkFormat vk_depth_stencil_format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
		if (VK_FORMAT_UNDEFINED != vk_depth_stencil_format) {
			VkImageFormatProperties props = { 0 };
			VkResult vk_res = vkGetPhysicalDeviceImageFormatProperties(
				r->vk.activegpu, vk_depth_stencil_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &props);
			
			if (VK_SUCCESS != vk_res) {					// Fall back to something that's guaranteed to work
				tex_desc.format = TinyImageFormat_D16_UNORM;
				TRACE(LOG_WARNING, "Depth stencil format (%u) not supported. Falling back to D16 format", desc->format);
			}
		}
	}
	tex_desc.name = desc->name;
	add_texture(r, &desc, &rendertarget->tex);

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	if (desc->height > 1)
		viewType = depthorarraysize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	else
		viewType = depthorarraysize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;

	VkImageViewCreateInfo rtvDesc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL };
	rtvDesc.flags = 0;
	rtvDesc.image = rendertarget->tex->vk.image;
	rtvDesc.viewType = viewType;
	rtvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(tex_desc.format);
	rtvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	rtvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	rtvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	rtvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	rtvDesc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(rtvDesc.format, true);
	rtvDesc.subresourceRange.baseMipLevel = 0;
	rtvDesc.subresourceRange.levelCount = 1;
	rtvDesc.subresourceRange.baseArrayLayer = 0;
	rtvDesc.subresourceRange.layerCount = depthorarraysize;
	CHECK_VKRESULT(vkCreateImageView(r->vk.device, &rtvDesc, &alloccbs, &rendertarget->vk.descriptor));

	for (uint32_t i = 0; i < desc->miplevels; ++i) {
		rtvDesc.subresourceRange.baseMipLevel = i;
		if ((desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
			(desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES)) {
			for (uint32_t j = 0; j < depthorarraysize; ++j) {
				rtvDesc.subresourceRange.layerCount = 1;
				rtvDesc.subresourceRange.baseArrayLayer = j;
				CHECK_VKRESULT(vkCreateImageView(r->vk.device, &rtvDesc, &alloccbs, &rendertarget->vk.slicedescriptors[i * depthorarraysize + j]));
			}
		}
		else CHECK_VKRESULT(vkCreateImageView(r->vk.device, &rtvDesc, &alloccbs, &rendertarget->vk.slicedescriptors[i]));
	}
	rendertarget->width = desc->width;
	rendertarget->height = desc->height;
	rendertarget->arraysize = size;
	rendertarget->depth = desc->depth;
	rendertarget->miplevels = desc->miplevels;
	rendertarget->samplecount = desc->samplecount;
	rendertarget->samplequality = desc->samplequality;
	rendertarget->format = tex_desc.format;
	rendertarget->clearval = desc->clearval;
    rendertarget->vr = (desc->flags & TEXTURE_CREATION_FLAG_VR_MULTIVIEW) != 0;
    rendertarget->vrfoveatedrendering = (desc->flags & TEXTURE_CREATION_FLAG_VR_FOVEATED_RENDERING) != 0;

	_initial_transition(r, rendertarget->tex, desc->state);

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	if (desc->flags & TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT) {
		RenderTargetDesc resolveRTDesc = *desc;
		resolveRTDesc.mFlags &= ~(TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT | TEXTURE_CREATION_FLAG_ON_TILE);
		resolveRTDesc.mSampleCount = SAMPLE_COUNT_1;
		addRenderTarget(r, &resolveRTDesc, &rendertarget->pResolveAttachment);
	}
#endif
}

void vk_remove_rendertarget(renderer_t* r, rendertarget_t* rendertarget)
{
	remove_texture(r, rendertarget->tex);
	vkDestroyImageView(r->vk.device, rendertarget->vk.descriptor, &alloccbs);
	const uint32_t depthorarraysize = rendertarget->arraysize * rendertarget->depth;
	if ((rendertarget->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(rendertarget->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		for (uint32_t i = 0; i < rendertarget->miplevels; ++i)
			for (uint32_t j = 0; j < depthorarraysize; ++j)
				vkDestroyImageView(r->vk.device, rendertarget->vk.slicedescriptors[i * depthorarraysize + j], &alloccbs);
	else
		for (uint32_t i = 0; i < rendertarget->miplevels; ++i)
			vkDestroyImageView(r->vk.device, rendertarget->vk.slicedescriptors[i], &alloccbs);
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	if (rendertarget->pResolveAttachment)
		remove_rendertarget(r, rendertarget->resolveattachment);
#endif
	tc_free(rendertarget->vk.slicedescriptors);
}

void vk_add_sampler(renderer_t* r, const samplerdesc_t* desc, sampler_t* sampler)
{
	TC_ASSERT(r && desc && sampler);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(desc->compareop < MAX_COMPARE_MODES);
	//default sampler lod values. Used if not overriden by setLODrange or not Linear mipmaps
	float minSamplerLod = 0;
	float maxSamplerLod = desc->mipmapmode == MIPMAP_MODE_LINEAR ? VK_LOD_CLAMP_NONE : 0;
	//user provided lods
	if(desc->setLODrange) {
		minSamplerLod = desc->minLOD;
		maxSamplerLod = desc->maxLOD;
	}
	VkSamplerCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = _to_vk_filter(desc->magfilter);
	info.minFilter = _to_vk_filter(desc->minfilter);
	info.mipmapMode = _to_vk_mip_map_mode(desc->mipmapmode);
	info.addressModeU = _to_vk_address_mode(desc->u);
	info.addressModeV = _to_vk_address_mode(desc->v);
	info.addressModeW = _to_vk_address_mode(desc->w);
	info.mipLodBias = desc->mipLODbias;
	info.anisotropyEnable = r->activegpusettings->sampleranisotropysupported && (desc->maxanisotropy > 0.0f) ? VK_TRUE : VK_FALSE;
	info.maxAnisotropy = desc->maxanisotropy;
	info.compareEnable = (compareopmap[desc->compareop] != VK_COMPARE_OP_NEVER) ? VK_TRUE : VK_FALSE;
	info.compareOp = compareopmap[desc->compareop];
	info.minLod = minSamplerLod;
	info.maxLod = maxSamplerLod;
	info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	info.unnormalizedCoordinates = VK_FALSE;

	if (TinyImageFormat_IsPlanar(desc->vk.format)) {
		VkFormat format = (VkFormat)TinyImageFormat_ToVkFormat(desc->vk.format);
		TC_ASSERT((uint32_t)r->vk.YCbCrextension);

		VkFormatProperties props = { 0 };
		vkGetPhysicalDeviceFormatProperties(r->vk.activegpu, format, &props);
		if (desc->vk.chromaoffsetX == SAMPLE_LOCATION_MIDPOINT) {
			TC_ASSERT(props.optimalTilingFeatures & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT);
		}
		else if (desc->vk.chromaoffsetX == SAMPLE_LOCATION_COSITED) {
			TC_ASSERT(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT);
		}
		VkSamplerYcbcrConversionCreateInfo conversion_info = { 0 };
		conversion_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
		conversion_info.format = format;
		conversion_info.ycbcrModel = (VkSamplerYcbcrModelConversion)desc->vk.model;
		conversion_info.ycbcrRange = (VkSamplerYcbcrRange)desc->vk.range;
		conversion_info.components = (VkComponentMapping){ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		conversion_info.xChromaOffset = (VkChromaLocation)desc->vk.chromaoffsetX;
		conversion_info.yChromaOffset = (VkChromaLocation)desc->vk.chromaoffsetY;
		conversion_info.chromaFilter = _to_vk_filter(desc->vk.chromafilter);
		conversion_info.forceExplicitReconstruction = desc->vk.forceexplicitreconstruction ? VK_TRUE : VK_FALSE;
		CHECK_VKRESULT(vkCreateSamplerYcbcrConversion(r->vk.device, &conversion_info, &alloccbs, &sampler->vk.ycbcrconversion));

		memset(&sampler->vk.ycbcrconversioninfo, 0, sizeof(VkSamplerYcbcrConversionInfo));
		sampler->vk.ycbcrconversioninfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
		sampler->vk.ycbcrconversioninfo.pNext = NULL;
		sampler->vk.ycbcrconversioninfo.conversion = sampler->vk.ycbcrconversion;
		info.pNext = &sampler->vk.ycbcrconversioninfo;
	}
	CHECK_VKRESULT(vkCreateSampler(r->vk.device, &info, &alloccbs, &(sampler->vk.sampler)));
}

void vk_remove_sampler(renderer_t* r, sampler_t* sampler)
{
	TC_ASSERT(r && sampler);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != sampler->vk.sampler);
	vkDestroySampler(r->vk.device, sampler->vk.sampler, &alloccbs);
	if (sampler->vk.ycbcrconversion != NULL)
		vkDestroySamplerYcbcrConversion(r->vk.device, sampler->vk.ycbcrconversion, &alloccbs);
}

void vk_mapbuffer(renderer_t* r, buffer_t* buffer, range_t* range)
{
	TC_ASSERT(buffer->memusage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to map non-cpu accessible resource");
	CHECK_VKRESULT(vmaMapMemory(r->vk.vmaAllocator, buffer->vk.alloc, &buffer->data));
	if (range) buffer->data = ((uint8_t*)buffer->data + range->offset);
}

void vk_unmapbuffer(renderer_t* r, buffer_t* buffer)
{
	TC_ASSERT(buffer->memusage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to unmap non-cpu accessible resource");
	vmaUnmapMemory(r->vk.vmaAllocator, buffer->vk.alloc);
	buffer->data = NULL;
}

void vk_add_descriptorset(renderer_t* r, const descriptorsetdesc_t* desc, descset_t* descset)
{
	TC_ASSERT(r && desc && descset);
	const rootsignature_t* rootsignature = desc->rootsignature;
	const descriptorupdatefreq_t freq = desc->updatefreq;
	const uint32_t nodeidx = r->gpumode == GPU_MODE_LINKED ? desc->nodeidx : 0;
	const uint32_t dynamicoffsets = rootsignature->vk.dynamicdescriptorcounts[freq];
	uint32_t total = desc->maxsets * dynamicoffsets * sizeof(dynamicuniformdata_t);
	if (VK_NULL_HANDLE != rootsignature->vk.descriptorsetlayouts[freq])
		total += desc->maxsets * sizeof(VkDescriptorSet);

	descset->vk.rootsignature = rootsignature;
	descset->vk.updatefreq = freq;
	descset->vk.dynamicoffsetcount = dynamicoffsets;
	descset->vk.nodeidx = nodeidx;
	descset->vk.maxsets = desc->maxsets;

	uint8_t* mem = (uint8_t*)tc_calloc(1, total);
	descset->vk.handles = (VkDescriptorSet*)mem;
	mem += desc->maxsets * sizeof(VkDescriptorSet);
	if (VK_NULL_HANDLE != rootsignature->vk.descriptorsetlayouts[freq]) {
		VkDescriptorSetLayout* layouts = (VkDescriptorSetLayout*)alloca(desc->maxsets * sizeof(VkDescriptorSetLayout));
		VkDescriptorSet** handles = (VkDescriptorSet**)alloca(desc->maxsets * sizeof(VkDescriptorSet*));
		for (uint32_t i = 0; i < desc->maxsets; i++) {
			layouts[i] = rootsignature->vk.descriptorsetlayouts[freq];
			handles[i] = &descset->vk.handles[i];
		}
		VkDescriptorPoolSize poolSizes[MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT] = { 0 };
		for (uint32_t i = 0; i < rootsignature->vk.poolsizecount[freq]; i++) {
			poolSizes[i] = rootsignature->vk.poolsizes[freq][i];
			poolSizes[i].descriptorCount *= desc->maxsets;
		}
		add_descriptor_pool(r, desc->maxsets, 0, poolSizes, rootsignature->vk.poolsizecount[freq], &descset->vk.descriptorpool);
		consume_descriptor_sets(r->vk.device, descset->vk.descriptorpool, layouts, desc->maxsets, handles);
		for (uint32_t descIndex = 0; descIndex < rootsignature->descriptors; ++descIndex) {
			const descriptorinfo_t* desc_info = &rootsignature->descriptors[descIndex];
			if (desc_info->updatefreq != freq || desc_info->rootdescriptor || desc_info->staticsampler)
				continue;

			descriptortype_t type = (descriptortype_t)desc_info->type;
			VkWriteDescriptorSet writeset = { 0 };
			writeset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeset.descriptorCount = 1;
			writeset.descriptorType = (VkDescriptorType)desc_info->vk.type;
			writeset.dstBinding = desc_info->vk.reg;
			for (uint32_t i = 0; i < desc->maxsets; i++) {
				writeset.dstSet = descset->vk.handles[i];
				switch (type) {
				case DESCRIPTOR_TYPE_SAMPLER: {
					VkDescriptorImageInfo update_data = { r->nulldescriptors->defaultsampler->vk.sampler, VK_NULL_HANDLE };
					writeset.pImageInfo = &update_data;
					for (uint32_t arr = 0; arr < desc_info->size; ++arr)
					{
						writeset.dstArrayElement = arr;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pImageInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXTURE:
				case DESCRIPTOR_TYPE_RW_TEXTURE: {
					VkImageView srcView = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ?
						r->nulldescriptors->defaulttexUAV[nodeidx][desc_info->dim]->vk.UAVdescriptors[0] :
						r->nulldescriptors->defaulttexSRV[nodeidx][desc_info->dim]->vk.SRVdescriptor;
					VkImageLayout layout = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					VkDescriptorImageInfo update_data = { VK_NULL_HANDLE, srcView, layout };
					writeset.pImageInfo = &update_data;
					for (uint32_t j = 0; j < desc_info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pImageInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_BUFFER:
				case DESCRIPTOR_TYPE_BUFFER_RAW:
				case DESCRIPTOR_TYPE_RW_BUFFER:
				case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
				case DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
					VkDescriptorBufferInfo update_data = { r->nulldescriptors->defaultbufSRV[nodeidx]->vk.buffer, 0, VK_WHOLE_SIZE };
					writeset.pBufferInfo = &update_data;
					for (uint32_t j = 0; j < desc_info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pBufferInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXEL_BUFFER:
				case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER: {
					VkBufferView update_data = (type == DESCRIPTOR_TYPE_RW_TEXEL_BUFFER) ?
						r->nulldescriptors->defaultbufUAV[nodeidx]->vk.storagetexelview :
						r->nulldescriptors->defaultbufSRV[nodeidx]->vk.uniformtexelview;
					writeset.pTexelBufferView = &update_data;
					for (uint32_t j = 0; j < desc_info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pTexelBufferView = NULL;
					break;
				}
				default:
					break;
				}
			}
		}
	}
	else {
		TRACE(LOG_ERROR, "NULL Descriptor Set Layout for update frequency %u. Cannot allocate descriptor set", (uint32_t)freq);
		TC_ASSERT(false && "NULL Descriptor Set Layout for update frequency. Cannot allocate descriptor set");
	}
	if (descset->vk.dynamicoffsetcount) {
		descset->vk.dynamicuniformdata = (dynamicuniformdata_t*)mem;
		mem += descset->vk.maxsets * descset->vk.dynamicoffsetcount * sizeof(dynamicuniformdata_t);
	}
}

void vk_removeDescriptorSet(renderer_t* r, descset_t* descset)
{
	TC_ASSERT(r && descset);
	vkDestroyDescriptorPool(r->vk.device, descset->vk.descriptorpool, &alloccbs);
	tc_free(descset->vk.handles);
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

void vk_update_descriptorset(renderer_t* r, uint32_t index, descset_t* descset, uint32_t count, const descdata_t* params)
{
	TC_ASSERT(r && descset);
	TC_ASSERT(descset->vk.handles);
	TC_ASSERT(index < descset->vk.maxsets);
	const rootsignature_t* rootsignature = descset->vk.rootsignature;
	VkWriteDescriptorSet writesetArray[MAX_WRITE_SETS] = { 0 };
	uint8_t descriptorUpdateDataStart[MAX_DESCRIPTOR_INFO_BYTES] = { 0 };
	const uint8_t* descriptorUpdateDataEnd = &descriptorUpdateDataStart[MAX_DESCRIPTOR_INFO_BYTES - 1];
	uint32_t writesetCount = 0;
	uint8_t* descriptorUpdateData = descriptorUpdateDataStart;
#define FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(type, pInfo, count)                                         \
	if (descriptorUpdateData + sizeof(type) >= descriptorUpdateDataEnd) {                             \
		writeset->descriptorCount = arr - lastArrayIndexStart;                                        \
		vkUpdateDescriptorSets(r->vk.device, writesetCount, writesetArray, 0, NULL);                  \
		/* All previous write sets flushed. Start from zero */                                        \
		writesetCount = 1;                                                                            \
		writesetArray[0] = *writeset;                                                                 \
		writeset = &writesetArray[0];                                                                 \
		lastArrayIndexStart = arr;                                                                    \
		writeset->dstArrayElement += writeset->descriptorCount;                                       \
		/* Set descriptor count to the remaining count */                                             \
		writeset->descriptorCount = count - writeset->dstArrayElement;                                \
		descriptorUpdateData = descriptorUpdateDataStart;                                             \
		writeset->pInfo = (type*)descriptorUpdateData;                                                \
	}                                                                                                 \
	type* currUpdateData = (type*)descriptorUpdateData;                                               \
	descriptorUpdateData += sizeof(type);

	for (uint32_t i = 0; i < count; i++) {
		const DescriptorData* pParam = pParams + i;
		uint32_t              paramIndex = pParam->mBindByIndex ? pParam->mIndex : UINT32_MAX;

		VALIDATE_DESCRIPTOR(pParam->pName || (paramIndex != UINT32_MAX), "DescriptorData has NULL name and invalid index");

		const DescriptorInfo* desc =
			(paramIndex != UINT32_MAX) ? (rootsignature->descriptors + paramIndex) : get_descriptor(rootsignature, pParam->pName);
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
		if (writesetCount >= MAX_WRITE_SETS)
		{
			vkUpdateDescriptorSets(r->vk.device, writesetCount, writesetArray, 0, NULL);
			writesetCount = 0;
			descriptorUpdateData = descriptorUpdateDataStart;
		}

		VkWriteDescriptorSet* writeset = &writesetArray[writesetCount++];
		writeset->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeset->pNext = NULL;
		writeset->descriptorCount = arrayCount;
		writeset->descriptorType = (VkDescriptorType)desc->vk.type;
		writeset->dstArrayElement = arrayStart;
		writeset->dstBinding = desc->vk.mReg;
		writeset->dstSet = descset->vk.handles[index];

		VALIDATE_DESCRIPTOR(
			desc->mUpdateFrequency == descset->vk.mUpdateFrequency, "Descriptor (%s) - Mismatching update frequency and set index", desc->pName);

		uint32_t lastArrayIndexStart = 0;

		switch (type) {
			case DESCRIPTOR_TYPE_SAMPLER:
			{
				// Index is invalid when descriptor is a static sampler
				VALIDATE_DESCRIPTOR(
					!desc->staticsampler,
					"Trying to update a static sampler (%s). All static samplers must be set in addrootsignature_t and cannot be updated "
					"later",
					desc->pName);

				VALIDATE_DESCRIPTOR(pParam->ppSamplers, "NULL Sampler (%s)", desc->pName);

				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppSamplers[arr], "NULL Sampler (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount) //-V1032
					*currUpdateData = { pParam->ppSamplers[arr]->vk.pVkSampler, VK_NULL_HANDLE };
				}
				break;
			}
			case DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", desc->pName);

#if defined(ENABLE_GRAPHICS_DEBUG)
				DescriptorIndexMap* pNode = shgetp_null(rootsignature->descriptorNameToIndexMap, desc->pName);
				if (!pNode)
				{
					TRACE(LOG_ERROR, "No Static Sampler called (%s)", desc->pName);
					TC_ASSERT(false);
				}
#endif

				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
					*currUpdateData =
					{
						NULL,                                                 // Sampler
						pParam->ppTextures[arr]->vk.SRVdescriptor,    // Image View
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL              // Image Layout
					};
				}
				break;
			}
			case DESCRIPTOR_TYPE_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", desc->pName);

				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				if (!pParam->mBindStencilResource)
				{
					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", desc->pName, arr);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, arrayCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                       // Sampler
							pParam->ppTextures[arr]->vk.SRVdescriptor,    // Image View
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
							pParam->ppTextures[arr]->vk.SRVstencildescriptor,    // Image View
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL                     // Image Layout
						};
					}
				}
				break;
			}
			case DESCRIPTOR_TYPE_RW_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL RW Texture (%s)", desc->pName);

				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;

				if (pParam->mBindMipChain)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[0], "NULL RW Texture (%s)", desc->pName);
					VALIDATE_DESCRIPTOR((!arrayStart), "Descriptor (%s) - mBindMipChain supports only updating the whole mip-chain. No partial updates supported", pParam->pName);
					const uint32_t mipCount = pParam->ppTextures[0]->mMipLevels;
					writeset->descriptorCount = mipCount;

					for (uint32_t arr = 0; arr < mipCount; ++arr)
					{
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, mipCount)
						*currUpdateData =
						{
							VK_NULL_HANDLE,                                           // Sampler
							pParam->ppTextures[0]->vk.UAVdescriptors[arr],    // Image View
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
							pParam->ppTextures[arr]->vk.UAVdescriptors[mipSlice],    // Image View
							VK_IMAGE_LAYOUT_GENERAL                                          // Image Layout
						};
					}
				}
				break;
			}
			case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			{
				if (desc->rootsignature)
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
					VALIDATE_DESCRIPTOR(pParam->pbuffers, "NULL Buffer (%s)", desc->pName);

					writeset->bufferInfo = (VkDescriptorBufferInfo*)descriptorUpdateData;

					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->pbuffers[arr], "NULL Buffer (%s [%u] )", desc->pName, arr);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorBufferInfo, bufferInfo, arrayCount)
						*currUpdateData =
						{
							pParam->pbuffers[arr]->vk.buffer,
							pParam->pbuffers[arr]->vk.mOffset,
							VK_WHOLE_SIZE
						};

						if (pParam->pRanges)
						{
							DescriptorDataRange range = pParam->pRanges[arr];
#if defined(ENABLE_GRAPHICS_DEBUG)
							uint32_t maxRange = DESCRIPTOR_TYPE_UNIFORM_BUFFER == type ?
								r->vk.activegpuprops->properties.limits.maxUniformBufferRange :
								r->vk.activegpuprops->properties.limits.maxStorageBufferRange;
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
				VALIDATE_DESCRIPTOR(pParam->pbuffers, "NULL Texel Buffer (%s)", desc->pName);

				writeset->pTexelBufferView = (VkBufferView*)descriptorUpdateData;

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->pbuffers[arr], "NULL Texel Buffer (%s [%u] )", desc->pName, arr);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkBufferView, pTexelBufferView, arrayCount)
					*currUpdateData = DESCRIPTOR_TYPE_TEXEL_BUFFER == type ?
						pParam->pbuffers[arr]->vk.uniformtexelview :
						pParam->pbuffers[arr]->vk.storagetexelview;
				}

				break;
			}
#ifdef VK_RAYTRACING_AVAILABLE
			case DESCRIPTOR_TYPE_RAY_TRACING:
			{
				VALIDATE_DESCRIPTOR(pParam->ppAccelerationStructures, "NULL Acceleration Structure (%s)", desc->pName);

				VkWriteDescriptorSetAccelerationStructureKHR writesetKHR = { 0 };
				VkAccelerationStructureKHR currUpdateData = { 0 };
				writeset->pNext = &writesetKHR;
				writeset->descriptorCount = 1;
				writesetKHR.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
				writesetKHR.pNext = NULL;
				writesetKHR.accelerationStructureCount = 1;
				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					vk_FillRaytracingDescriptorData(pParam->ppAccelerationStructures[arr], &currUpdateData);
					writesetKHR.pAccelerationStructures = &currUpdateData;
					vkUpdateDescriptorSets(r->vk.device, 1, writeset, 0, NULL);
					++writeset->dstArrayElement;
				}

				// Update done - Dont need this write set anymore. Return it to the array
				writeset->pNext = NULL;
				--writesetCount;

				break;
			}
#endif
			default: break;
		}
	}

	vkUpdateDescriptorSets(r->vk.device, writesetCount, writesetArray, 0, NULL);
}

static const uint32_t VK_MAX_ROOT_DESCRIPTORS = 32;

void vk_cmdBindDescriptorSet(cmd_t* pCmd, uint32_t index, DescriptorSet* descset)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(descset);
	TC_ASSERT(descset->vk.handles);
	TC_ASSERT(index < descset->vk.maxsets);

	const rootsignature_t* rootsignature = descset->vk.rootsignature;

	if (pCmd->vk.pBoundPipelineLayout != rootsignature->vk.pipelineLayout)
	{
		pCmd->vk.pBoundPipelineLayout = rootsignature->vk.pipelineLayout;

		// Vulkan requires to bind all descriptor sets upto the highest set number even if they are empty
		// Example: If shader uses only set 2, we still have to bind empty sets for set=0 and set=1
		for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
		{
			if (rootsignature->vk.pEmptyDescriptorSet[setIndex] != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					pCmd->vk.pVkCmdBuf, gPipelineBindPoint[rootsignature->mPipelineType], rootsignature->vk.pipelineLayout,
					setIndex, 1, &rootsignature->vk.pEmptyDescriptorSet[setIndex], 0, NULL);
			}
		}
	}

	static uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };

	vkCmdBindDescriptorSets(
		pCmd->vk.pVkCmdBuf, gPipelineBindPoint[rootsignature->mPipelineType], rootsignature->vk.pipelineLayout,
		descset->vk.mUpdateFrequency, 1, &descset->vk.handles[index],
		descset->vk.mDynamicOffsetCount, offsets);
}

void vk_cmdBindPushConstants(cmd_t* pCmd, rootsignature_t* rootsignature, uint32_t paramIndex, const void* pConstants)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pConstants);
	TC_ASSERT(rootsignature);
	TC_ASSERT(paramIndex >= 0 && paramIndex < rootsignature->descriptors);

	const DescriptorInfo* desc = rootsignature->descriptors + paramIndex;
	TC_ASSERT(desc);
	TC_ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == desc->mType);

	vkCmdPushConstants(
		pCmd->vk.pVkCmdBuf, rootsignature->vk.pipelineLayout, desc->vk.mVkStages, 0, desc->mSize, pConstants);
}

void vk_cmdBindDescriptorSetWithRootCbvs(cmd_t* pCmd, uint32_t index, DescriptorSet* descset, uint32_t count, const DescriptorData* pParams)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(descset);
	TC_ASSERT(pParams);

	const rootsignature_t* rootsignature = descset->vk.rootsignature;
	uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };

	for (uint32_t i = 0; i < count; ++i)
	{
		const DescriptorData* pParam = pParams + i;
		uint32_t              paramIndex = pParam->mBindByIndex ? pParam->mIndex : UINT32_MAX;

		const DescriptorInfo* desc =
			(paramIndex != UINT32_MAX) ? (rootsignature->descriptors + paramIndex) : get_descriptor(rootsignature, pParam->pName);
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
			pCmd->r->vk.activegpuprops->properties.limits.maxUniformBufferRange :
			pCmd->r->vk.activegpuprops->properties.limits.maxStorageBufferRange;
#endif

		VALIDATE_DESCRIPTOR(desc->rootsignature, "Descriptor (%s) - must be a root cbv", desc->pName);
		VALIDATE_DESCRIPTOR(pParam->mCount <= 1, "Descriptor (%s) - cmdBindDescriptorSetWithRootCbvs does not support arrays", desc->pName);
		VALIDATE_DESCRIPTOR(pParam->pRanges, "Descriptor (%s) - pRanges must be provided for cmdBindDescriptorSetWithRootCbvs", desc->pName);

		DescriptorDataRange range = pParam->pRanges[0];
		VALIDATE_DESCRIPTOR(range.mSize, "Descriptor (%s) - pRanges->mSize is zero", desc->pName);
		VALIDATE_DESCRIPTOR(
			range.mSize <= maxRange,
			"Descriptor (%s) - pRanges->mSize is %ull which exceeds max size %u", desc->pName, range.mSize,
			maxRange);

		offsets[desc->mHandleIndex] = range.mOffset; //-V522
		DynamicUniformData* pData = &descset->vk.pDynamicUniformData[index * descset->vk.mDynamicOffsetCount + desc->mHandleIndex];
		if (pData->buffer != pParam->pbuffers[0]->vk.buffer || range.mSize != pData->mSize)	{
			*pData = { pParam->pbuffers[0]->vk.buffer, 0, range.mSize };

			VkDescriptorBufferInfo bufferInfo = { pData->buffer, 0, (VkDeviceSize)pData->mSize };
			VkWriteDescriptorSet writeset = { 0 };
			writeset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeset.pNext = NULL;
			writeset.descriptorCount = 1;
			writeset.descriptorType = (VkDescriptorType)desc->vk.type;
			writeset.dstArrayElement = 0;
			writeset.dstBinding = desc->vk.mReg;
			writeset.dstSet = descset->vk.handles[index];
			writeset.bufferInfo = &bufferInfo;
			vkUpdateDescriptorSets(pCmd->r->vk.device, 1, &writeset, 0, NULL);
		}
	}

	vkCmdBindDescriptorSets(
		pCmd->vk.pVkCmdBuf, gPipelineBindPoint[rootsignature->mPipelineType], rootsignature->vk.pipelineLayout,
		descset->vk.mUpdateFrequency, 1, &descset->vk.handles[index],
		descset->vk.mDynamicOffsetCount, offsets);
}
/************************************************************************/
// Shader Functions
/************************************************************************/
void vk_addShaderBinary(renderer_t* r, const BinaryShaderDesc* desc, Shader** ppShaderProgram)
{
	TC_ASSERT(r);
	TC_ASSERT(desc);
	TC_ASSERT(ppShaderProgram);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);

	uint32_t counter = 0;

	size_t total = sizeof(Shader);
	total += sizeof(PipelineReflection);

	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		ShaderStage stage_mask = (ShaderStage)(1 << i);
		if (stage_mask == (desc->mStages & stage_mask))
		{
			switch (stage_mask)
			{
				case SHADER_STAGE_VERT: total += (strlen(desc->mVert.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_TESC: total += (strlen(desc->mHull.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_TESE: total += (strlen(desc->mDomain.pEntryPoint) + 1) * sizeof(char); break;    //-V814
				case SHADER_STAGE_GEOM: total += (strlen(desc->mGeom.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_FRAG: total += (strlen(desc->mFrag.pEntryPoint) + 1) * sizeof(char); break;      //-V814
				case SHADER_STAGE_RAYTRACING:
				case SHADER_STAGE_COMP: total += (strlen(desc->mComp.pEntryPoint) + 1) * sizeof(char); break;    //-V814
				default: break;
			}
			++counter;
		}
	}

	if (desc->mConstantCount)
	{
		total += sizeof(VkSpecializationInfo);
		total += sizeof(VkSpecializationMapEntry) * desc->mConstantCount;
		for (uint32_t i = 0; i < desc->mConstantCount; ++i)
		{
			const ShaderConstant* constant = &desc->pConstants[i];
			total += (constant->mSize == sizeof(bool)) ? sizeof(VkBool32) : constant->mSize;
		}
	}

	total += counter * sizeof(VkShaderModule);
	total += counter * sizeof(char*);
	Shader* pShaderProgram = (Shader*)tc_calloc(1, total);
	pShaderProgram->mStages = desc->mStages;
	pShaderProgram->pReflection = (PipelineReflection*)(pShaderProgram + 1);    //-V1027
	pShaderProgram->vk.pShaderModules = (VkShaderModule*)(pShaderProgram->pReflection + 1);
	pShaderProgram->vk.pEntryNames = (char**)(pShaderProgram->vk.pShaderModules + counter);
	pShaderProgram->vk.pSpecializationInfo = NULL;

	uint8_t* mem = (uint8_t*)(pShaderProgram->vk.pEntryNames + counter);
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
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
						r->vk.device, &info, &alloccbs,
						&(pShaderProgram->vk.pShaderModules[counter])));
				}
				break;
				default: TC_ASSERT(false && "Shader Stage not supported!"); break;
			}

			pShaderProgram->vk.pEntryNames[counter] = (char*)mem;
			mem += (strlen(pStageDesc->pEntryPoint) + 1) * sizeof(char);    //-V522
			strcpy(pShaderProgram->vk.pEntryNames[counter], pStageDesc->pEntryPoint);
			++counter;
		}
	}

	// Fill specialization constant entries
	if (desc->mConstantCount)
	{
		pShaderProgram->vk.pSpecializationInfo = (VkSpecializationInfo*)mem;
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

		VkSpecializationInfo* specializationInfo = pShaderProgram->vk.pSpecializationInfo;
		specializationInfo->dataSize = offset;
		specializationInfo->mapEntryCount = desc->mConstantCount;
		specializationInfo->pData = data;
		specializationInfo->pMapEntries = mapEntries;
	}

	createPipelineReflection(stageReflections, counter, pShaderProgram->pReflection);

	addShaderDependencies(pShaderProgram, desc);

	*ppShaderProgram = pShaderProgram;
}

void vk_removeShader(renderer_t* r, Shader* pShaderProgram)
{
	removeShaderDependencies(pShaderProgram);

	TC_ASSERT(r);

	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);

	if (pShaderProgram->mStages & SHADER_STAGE_VERT)
	{
		vkDestroyShaderModule(
			r->vk.device, pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mVertexStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESC)
	{
		vkDestroyShaderModule(
			r->vk.device, pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mHullStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESE)
	{
		vkDestroyShaderModule(
			r->vk.device, pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mDomainStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_GEOM)
	{
		vkDestroyShaderModule(
			r->vk.device, pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mGeometryStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_FRAG)
	{
		vkDestroyShaderModule(
			r->vk.device, pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mPixelStageIndex],
			&alloccbs);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_COMP)
	{
		vkDestroyShaderModule(r->vk.device, pShaderProgram->vk.pShaderModules[0], &alloccbs);
	}
#ifdef VK_RAYTRACING_AVAILABLE
	if (pShaderProgram->mStages & SHADER_STAGE_RAYTRACING)
	{
		vkDestroyShaderModule(r->vk.device, pShaderProgram->vk.pShaderModules[0], &alloccbs);
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
	return (*pLhs)->vk.mReg < (*pRhs)->vk.mReg;
}

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding, simpleSortVkDescriptorSetLayoutBinding)
DEFINE_PARTITION_IMPL_FUNCTION(static, partitionImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding)
DEFINE_QUICK_SORT_IMPL_FUNCTION(static, quickSortImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, compareDescriptorSetLayoutBinding, stableSortVkDescriptorSetLayoutBinding, partitionImplVkDescriptorSetLayoutBinding)
DEFINE_QUICK_SORT_FUNCTION(static, sortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, quickSortImplVkDescriptorSetLayoutBinding)

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortdescriptorInfo, descriptorInfo, comparedescriptorInfo)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortdescriptorInfo, descriptorInfo, comparedescriptorInfo, simpleSortdescriptorInfo)

void vk_addrootsignature_t(renderer_t* r, const rootsignature_tDesc* rootsignatureDesc, rootsignature_t** prootsignature)
{
	TC_ASSERT(r);
	TC_ASSERT(rootsignatureDesc);
	TC_ASSERT(prootsignature);

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

	for (uint32_t i = 0; i < rootsignatureDesc->staticsamplerCount; ++i)
	{
		TC_ASSERT(rootsignatureDesc->ppStaticSamplers[i]);
		shput(staticSamplerMap, rootsignatureDesc->ppStaticSamplerNames[i], rootsignatureDesc->ppStaticSamplers[i]);
	}

	PipelineType		pipelineType = PIPELINE_TYPE_UNDEFINED;
	DescriptorIndexMap*	indexMap = NULL;
	sh_new_arena(indexMap);

	// Collect all unique shader resources in the given shaders
	// Resources are parsed by name (two resources named "XYZ" in two shaders will be considered the same resource)
	for (uint32_t sh = 0; sh < rootsignatureDesc->mShaderCount; ++sh)
	{
		PipelineReflection const* pReflection = rootsignatureDesc->ppShaders[sh]->pReflection;

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
							"sharing the same register and space addrootsignature_t "
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
						"shared by multiple shaders specified in addrootsignature_t "
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
						"shared by multiple shaders specified in addrootsignature_t "
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

	size_t total = sizeof(rootsignature_t);
	total += arrlenu(shaderResources) * sizeof(DescriptorInfo);
	rootsignature_t* rootsignature = (rootsignature_t*)tc_calloc_memalign(1, alignof(rootsignature_t), total);
	TC_ASSERT(rootsignature);

	rootsignature->descriptors = (DescriptorInfo*)(rootsignature + 1);                                                        //-V1027
	rootsignature->descriptorNameToIndexMap = indexMap;

	if (arrlen(shaderResources))
	{
		rootsignature->descriptors = (uint32_t)arrlen(shaderResources);
	}

	rootsignature->mPipelineType = pipelineType;

	// Fill the descriptor array to be stored in the root signature
	for (ptrdiff_t i = 0; i < arrlen(shaderResources); ++i)
	{
		DescriptorInfo*           desc = &rootsignature->descriptors[i];
		ShaderResource const*     pRes = &shaderResources[i];
		uint32_t                  setIndex = pRes->set;
		DescriptorUpdateFrequency freq = (DescriptorUpdateFrequency)setIndex;

		// Copy the binding information generated from the shader reflection into the descriptor
		desc->vk.mReg = pRes->reg;
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
			desc->vk.type = binding.descriptorType;
			desc->vk.mVkStages = binding.stageFlags;
			desc->mUpdateFrequency = freq;

			// Find if the given descriptor is a static sampler
			StaticSamplerNode* it = shgetp_null(staticSamplerMap, desc->pName);
			if (it)
			{
				TRACE(LOG_INFO, "Descriptor (%s) : User specified Static Sampler", desc->pName);
				binding.pImmutableSamplers = &it->value->vk.pVkSampler;
			}

			// Set the index to an invalid value so we can use this later for error checking if user tries to update a static sampler
			// In case of Combined Image Samplers, skip invalidating the index
			// because we do not to introduce new ways to update the descriptor in the Interface
			if (it && desc->mType != DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				desc->staticsampler = true;
			}
			else
			{
				arrpush(layouts[setIndex].descriptors, desc);
			}

			if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				arrpush(layouts[setIndex].mDynamicDescriptors, desc);
				desc->rootsignature = true;
			}

			arrpush(layouts[setIndex].mBindings, binding);

			// Update descriptor pool size for this descriptor type
			VkDescriptorPoolSize* poolSize = NULL;
			for (uint32_t i = 0; i < MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT; ++i)
			{
				if (binding.descriptorType == rootsignature->vk.poolsizes[setIndex][i].type && rootsignature->vk.poolsizes[setIndex][i].descriptorCount)
				{
					poolSize = &rootsignature->vk.poolsizes[setIndex][i];
					break;
				}
			}
			if (!poolSize)
			{
				poolSize = &rootsignature->vk.poolsizes[setIndex][rootsignature->vk.poolsizecount[setIndex]++];
				poolSize->type = binding.descriptorType;
			}

			poolSize->descriptorCount += binding.descriptorCount;
		}
		// If descriptor is a root constant, add it to the root constant array
		else
		{
			TRACE(LOG_INFO, "Descriptor (%s) : User specified Push Constant", desc->pName);

			desc->rootsignature = true;
			desc->vk.mVkStages = util_to_vk_shader_stage_flags(pRes->used_stages);
			setIndex = 0;
			desc->mHandleIndex = pushConstantCount++;

			pushConstants[desc->mHandleIndex] = { 0 };
			pushConstants[desc->mHandleIndex].offset = 0;
			pushConstants[desc->mHandleIndex].size = desc->mSize;
			pushConstants[desc->mHandleIndex].stageFlags = desc->vk.mVkStages;
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
			createLayout = rootsignature->vk.mVkDescriptorSetLayouts[layoutIndex + 1] != VK_NULL_HANDLE;
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
					r->vk.device, &layoutInfo, &alloccbs, &rootsignature->vk.mVkDescriptorSetLayouts[layoutIndex]));
			}
			else
			{
				rootsignature->vk.mVkDescriptorSetLayouts[layoutIndex] = r->vk.pEmptyDescriptorSetLayout;
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
			if (!desc->rootsignature)
			{
				desc->mHandleIndex = cumulativeDescriptorCount;
				cumulativeDescriptorCount += desc->mSize;
			}
		}

		if (arrlen(layout.mDynamicDescriptors))
		{
			// vkCmdBindDescriptorSets - pDynamicOffsets - entries are ordered by the binding numbers in the descriptor set layouts
			stableSortdescriptorInfo(layout.mDynamicDescriptors, arrlenu(layout.mDynamicDescriptors));

			rootsignature->vk.mVkDynamicDescriptorCounts[layoutIndex] = (uint32_t)arrlen(layout.mDynamicDescriptors);
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
	VkDescriptorSetLayout descsetLayouts[kMaxLayoutCount] = { 0 };
	uint32_t              descsetLayoutCount = 0;
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		if (rootsignature->vk.mVkDescriptorSetLayouts[i])
		{
			descsetLayouts[descsetLayoutCount++] = rootsignature->vk.mVkDescriptorSetLayouts[i];
		}
	}

	DECLARE_ZERO(VkPipelineLayoutCreateInfo, info);
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	info.setLayoutCount = descsetLayoutCount;
	info.pSetLayouts = descsetLayouts;
	info.pushConstantRangeCount = pushConstantCount;
	info.pPushConstantRanges = pushConstants;
	CHECK_VKRESULT(vkCreatePipelineLayout(
		r->vk.device, &info, &alloccbs, &(rootsignature->vk.pipelineLayout)));
	/************************************************************************/
	// Update templates
	/************************************************************************/
	for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
	{
		const UpdateFrequencyLayoutInfo& layout = layouts[setIndex];

		if (!arrlen(layout.descriptors) && rootsignature->vk.mVkDescriptorSetLayouts[setIndex] != VK_NULL_HANDLE)
		{
			rootsignature->vk.pEmptyDescriptorSet[setIndex] = r->vk.pEmptyDescriptorSet;
			if (rootsignature->vk.mVkDescriptorSetLayouts[setIndex] != r->vk.pEmptyDescriptorSetLayout)
			{
				add_descriptor_pool(r, 1, 0,
					rootsignature->vk.poolsizes[setIndex], rootsignature->vk.poolsizecount[setIndex],
					&rootsignature->vk.pEmptyDescriptorPool[setIndex]);
				VkDescriptorSet* emptySet[] = { &rootsignature->vk.pEmptyDescriptorSet[setIndex] };
				consume_descriptor_sets(r->vk.device, rootsignature->vk.pEmptyDescriptorPool[setIndex],
					&rootsignature->vk.mVkDescriptorSetLayouts[setIndex],
					1, emptySet);
			}
		}
	}

	addrootsignature_tDependencies(rootsignature, rootsignatureDesc);

	*prootsignature = rootsignature;
	for (uint32_t i = 0; i < kMaxLayoutCount; ++i)
	{
		arrfree(layouts[i].mBindings);
		arrfree(layouts[i].descriptors);
		arrfree(layouts[i].mDynamicDescriptors);
	}
	arrfree(shaderResources);
	shfree(staticSamplerMap);
}

void vk_removerootsignature_t(renderer_t* r, rootsignature_t* rootsignature)
{
	removerootsignature_tDependencies(rootsignature);

	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		if (rootsignature->vk.mVkDescriptorSetLayouts[i] != r->vk.pEmptyDescriptorSetLayout)
		{
			vkDestroyDescriptorSetLayout(
				r->vk.device, rootsignature->vk.mVkDescriptorSetLayouts[i], &alloccbs);
		}
		if (VK_NULL_HANDLE != rootsignature->vk.pEmptyDescriptorPool[i])
		{
			vkDestroyDescriptorPool(
				r->vk.device, rootsignature->vk.pEmptyDescriptorPool[i], &alloccbs);
		}
	}

	shfree(rootsignature->descriptorNameToIndexMap);

	vkDestroyPipelineLayout(r->vk.device, rootsignature->vk.pipelineLayout, &alloccbs);

	tc_free(rootsignature);
}
/************************************************************************/
// Pipeline State Functions
/************************************************************************/
static void addGraphicsPipeline(renderer_t* r, const PipelineDesc* pMainDesc, pipeline_t** pipeline)
{
	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(pMainDesc);

	const GraphicsPipelineDesc* desc = &pMainDesc->mGraphicsDesc;
	VkPipelineCache             psoCache = pMainDesc->pCache ? pMainDesc->pCache->vk.pCache : VK_NULL_HANDLE;

	TC_ASSERT(desc->pShaderProgram);
	TC_ASSERT(desc->rootsignature);

	pipeline_t* pipeline = (pipeline_t*)tc_calloc_memalign(1, alignof(Pipeline), sizeof(Pipeline));
	TC_ASSERT(pipeline);

	const Shader*       pShaderProgram = desc->pShaderProgram;
	const VertexLayout* pVertexLayout = desc->pVertexLayout;

	pipeline->vk.mType = PIPELINE_TYPE_GRAPHICS;

	// Create tempporary renderpass for pipeline creation
	RenderPassDesc renderPassDesc = { 0 };
	RenderPass     renderPass = { 0 };
	renderPassDesc.rtcount = desc->rtcount;
	renderPassDesc.pColorFormats = desc->pColorFormats;
	renderPassDesc.mSampleCount = desc->samplecount;
	renderPassDesc.mDepthStencilFormat = desc->depthStencilFormat;
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
	add_render_pass(r, &renderPassDesc, &renderPass);

	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	for (uint32_t i = 0; i < pShaderProgram->pReflection->mStageReflectionCount; ++i)
		TC_ASSERT(VK_NULL_HANDLE != pShaderProgram->vk.pShaderModules[i]);

	const VkSpecializationInfo* specializationInfo = pShaderProgram->vk.pSpecializationInfo;

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
						stages[stage_count].module = pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mVertexStageIndex];
					}
					break;
					case SHADER_STAGE_TESC:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mHullStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
						stages[stage_count].module = pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mHullStageIndex];
					}
					break;
					case SHADER_STAGE_TESE:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mDomainStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
						stages[stage_count].module = pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mDomainStageIndex];
					}
					break;
					case SHADER_STAGE_GEOM:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mGeometryStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
						stages[stage_count].module =
							pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mGeometryStageIndex];
					}
					break;
					case SHADER_STAGE_FRAG:
					{
						stages[stage_count].pName =
							pShaderProgram->pReflection->mStageReflections[pShaderProgram->pReflection->mPixelStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						stages[stage_count].module = pShaderProgram->vk.pShaderModules[pShaderProgram->pReflection->mPixelStageIndex];
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
				input_bindings[input_binding_count - 1].stride += TinyImageFormat_BitSizeOfBlock(attrib->format) / 8;

				input_attributes[input_attribute_count].location = attrib->mLocation;
				input_attributes[input_attribute_count].binding = attrib->mBinding;
				input_attributes[input_attribute_count].format = (VkFormat)TinyImageFormat_ToVkFormat(attrib->format);
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
		ms.rasterizationSamples = util_to_vk_sample_count(desc->samplecount);
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
		info.layout = desc->rootsignature->vk.pipelineLayout;
		info.renderPass = renderPass.pRenderPass;
		info.subpass = 0;
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.basePipelineIndex = -1;
		CHECK_VKRESULT(vkCreateGraphicsPipelines(
			r->vk.device, psoCache, 1, &info, &alloccbs, &(pipeline->vk.pVkPipeline)));

		remove_render_pass(r, &renderPass);
	}

	*pipeline = pipeline;
}

static void addComputePipeline(renderer_t* r, const PipelineDesc* pMainDesc, pipeline_t** pipeline)
{
	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(pMainDesc);

	const ComputePipelineDesc* desc = &pMainDesc->mComputeDesc;
	VkPipelineCache            psoCache = pMainDesc->pCache ? pMainDesc->pCache->vk.pCache : VK_NULL_HANDLE;

	TC_ASSERT(desc->pShaderProgram);
	TC_ASSERT(desc->rootsignature);
	TC_ASSERT(r->vk.device != VK_NULL_HANDLE);
	TC_ASSERT(desc->pShaderProgram->vk.pShaderModules[0] != VK_NULL_HANDLE);

	pipeline_t* pipeline = (pipeline_t*)tc_calloc_memalign(1, alignof(Pipeline), sizeof(Pipeline));
	TC_ASSERT(pipeline);
	pipeline->vk.mType = PIPELINE_TYPE_COMPUTE;

	// Pipeline
	{
		DECLARE_ZERO(VkPipelineShaderStageCreateInfo, stage);
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.pNext = NULL;
		stage.flags = 0;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = desc->pShaderProgram->vk.pShaderModules[0];
		stage.pName = desc->pShaderProgram->pReflection->mStageReflections[0].pEntryPoint;
		stage.pSpecializationInfo = desc->pShaderProgram->vk.pSpecializationInfo;

		DECLARE_ZERO(VkComputePipelineCreateInfo, info);
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.stage = stage;
		info.layout = desc->rootsignature->vk.pipelineLayout;
		info.basePipelineHandle = 0;
		info.basePipelineIndex = 0;
		CHECK_VKRESULT(vkCreateComputePipelines(
			r->vk.device, psoCache, 1, &info, &alloccbs, &(pipeline->vk.pVkPipeline)));
	}

	*pipeline = pipeline;
}

void vk_addPipeline(renderer_t* r, const PipelineDesc* desc, pipeline_t** pipeline)
{
	switch (desc->mType)
	{
		case (PIPELINE_TYPE_COMPUTE):
		{
			addComputePipeline(r, desc, pipeline);
			break;
		}
		case (PIPELINE_TYPE_GRAPHICS):
		{
			addGraphicsPipeline(r, desc, pipeline);
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
		setPipelineName(r, *pipeline, desc->pName);
	}
}

void vk_removePipeline(renderer_t* r, pipeline_t* pipeline)
{
	removePipelineDependencies(pipeline);

	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != pipeline->vk.pVkPipeline);

#ifdef VK_RAYTRACING_AVAILABLE
	tc_free(pipeline->vk.ppShaderStageNames);
#endif

	vkDestroyPipeline(r->vk.device, pipeline->vk.pVkPipeline, &alloccbs);

	tc_free(pipeline);
}

void vk_addPipelineCache(renderer_t* r, const PipelineCacheDesc* desc, PipelineCache** pipelineCache)
{
	TC_ASSERT(r);
	TC_ASSERT(desc);
	TC_ASSERT(pipelineCache);

	PipelineCache* pipelineCache = (PipelineCache*)tc_calloc(1, sizeof(PipelineCache));
	TC_ASSERT(pipelineCache);

	VkPipelineCacheCreateInfo psoCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	psoCacheCreateInfo.initialDataSize = desc->mSize;
	psoCacheCreateInfo.pInitialData = desc->pData;
	psoCacheCreateInfo.flags = util_to_pipeline_cache_flags(desc->flags);
	CHECK_VKRESULT(
		vkCreatePipelineCache(r->vk.device, &psoCacheCreateInfo, &alloccbs, &pipelineCache->vk.pCache));

	*pipelineCache = pipelineCache;
}

void vk_removePipelineCache(renderer_t* r, PipelineCache* pipelineCache)
{
	TC_ASSERT(r);
	TC_ASSERT(pipelineCache);

	if (pipelineCache->vk.pCache)
	{
		vkDestroyPipelineCache(r->vk.device, pipelineCache->vk.pCache, &alloccbs);
	}

	tc_free(pipelineCache);
}

void vk_getPipelineCacheData(renderer_t* r, PipelineCache* pipelineCache, size_t* pSize, void* pData)
{
	TC_ASSERT(r);
	TC_ASSERT(pipelineCache);
	TC_ASSERT(pSize);

	if (pipelineCache->vk.pCache)
	{
		CHECK_VKRESULT(vkGetPipelineCacheData(r->vk.device, pipelineCache->vk.pCache, pSize, pData));
	}
}
/************************************************************************/
// Command buffer functions
/************************************************************************/
void vk_reset_cmdpool(renderer_t* r, cmdpool_t* pCmdPool)
{
	TC_ASSERT(r);
	TC_ASSERT(pCmdPool);

	CHECK_VKRESULT(vkResetCommandPool(r->vk.device, pCmdPool->pVkCmdPool, 0));
}

void vk_cmd_begin(cmd_t* pCmd)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	DECLARE_ZERO(VkCommandBufferBeginInfo, begin_info);
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VkDeviceGroupCommandBufferBeginInfoKHR deviceGroupBeginInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO_KHR };
	deviceGroupBeginInfo.pNext = NULL;
	if (pCmd->r->gpumode == GPU_MODE_LINKED)
	{
		deviceGroupBeginInfo.deviceMask = (1 << pCmd->vk.nodeidx);
		begin_info.pNext = &deviceGroupBeginInfo;
	}

	CHECK_VKRESULT(vkBeginCommandBuffer(pCmd->vk.pVkCmdBuf, &begin_info));

	// Reset CPU side data
	pCmd->vk.pBoundPipelineLayout = NULL;
}

void vk_cmd_end(cmd_t* pCmd)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	if (pCmd->vk.pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->vk.pVkCmdBuf);
	}

	pCmd->vk.pVkActiveRenderPass = VK_NULL_HANDLE;

	CHECK_VKRESULT(vkEndCommandBuffer(pCmd->vk.pVkCmdBuf));
}

void vk_cmdBindRenderTargets(
	cmd_t* pCmd, uint32_t renderTargetCount, RenderTarget** rendertargets, RenderTarget* depthstencil,
	const LoadActionsDesc* pLoadActions /* = NULL*/, uint32_t* pColorArraySlices, uint32_t* pColorMipSlices, uint32_t depthArraySlice,
	uint32_t depthMipSlice)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	if (pCmd->vk.pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->vk.pVkCmdBuf);
		pCmd->vk.pVkActiveRenderPass = VK_NULL_HANDLE;
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
			colorStoreAction[i] = rendertargets[i]->texture->mLazilyAllocated ? STORE_ACTION_RESOLVE_DONTCARE : STORE_ACTION_DONTCARE;
		}
		else
#endif
		{
			colorStoreAction[i] = rendertargets[i]->texture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
				(pLoadActions ? pLoadActions->mStoreActionsColor[i] : gDefaultStoreActions[i]);
		}

		uint32_t renderPassHashValues[] =
		{
			(uint32_t)rendertargets[i]->format,
			(uint32_t)rendertargets[i]->mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionsColor[i] : gDefaultLoadActions[i],
			(uint32_t)colorStoreAction[i]
		};
		uint32_t frameBufferHashValues[] =
		{
			rendertargets[i]->vk.mId,
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			resolveAttachment ? rendertargets[i]->pResolveAttachment->vk.mId : 0
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
		depthStoreAction = depthstencil->texture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
			(pLoadActions ? pLoadActions->mStoreActionDepth : gDefaultStoreActions[0]);
		stencilStoreAction = depthstencil->texture->mLazilyAllocated ? STORE_ACTION_DONTCARE :
			(pLoadActions ? pLoadActions->mStoreActionStencil : gDefaultStoreActions[0]);

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		// Dont support depth stencil auto resolve
		TC_ASSERT(!(IsStoreActionResolve(depthStoreAction) || IsStoreActionResolve(stencilStoreAction)));
#endif

		uint32_t hashValues[] =
		{
			(uint32_t)depthstencil->format,
			(uint32_t)depthstencil->mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionDepth : gDefaultLoadActions[0],
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionStencil : gDefaultLoadActions[0],
			(uint32_t)depthStoreAction,
			(uint32_t)stencilStoreAction,
		};
		renderPassHash = tf_mem_hash<uint32_t>(hashValues, 6, renderPassHash);
		frameBufferHash = tf_mem_hash<uint32_t>(&depthstencil->vk.mId, 1, frameBufferHash);
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
	RenderPassNode** pRenderPassMap = get_render_pass_map(pCmd->r->mUnlinkedRendererIndex);
	FrameBufferNode** framebufferMap = get_frame_buffer_map(pCmd->r->mUnlinkedRendererIndex);

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
			colorFormats[i] = rendertargets[i]->format;
			vrMultiview |= rendertargets[i]->mVRMultiview;
		}
		if (depthstencil)
		{
			depthStencilFormat = depthstencil->format;
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
		add_render_pass(pCmd->r, &renderPassDesc, &renderPass);

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
		desc.rendertargetResolveActions = colorStoreAction;
#endif
		desc.pRenderPass = pRenderPass;
		desc.pColorArraySlices = pColorArraySlices;
		desc.pColorMipSlices = pColorMipSlices;
		desc.mDepthArraySlice = depthArraySlice;
		desc.mDepthMipSlice = depthMipSlice;
		desc.mVRFoveatedRendering = vrFoveatedRendering;
		add_framebuffer(pCmd->r, &desc, &frameBuffer);

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

	vkCmdBeginRenderPass(pCmd->vk.pVkCmdBuf, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	pCmd->vk.pVkActiveRenderPass = renderpass->renderpass;
}

void vk_cmdSetShadingRate(
	cmd_t* pCmd, ShadingRate shadingRate, texture_t* pTexture, ShadingRateCombiner postRasterizerRate, ShadingRateCombiner finalRate)
{
}

void vk_cmdSetViewport(cmd_t* pCmd, float x, float y, float width, float height, float minDepth, float maxDepth)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	DECLARE_ZERO(VkViewport, viewport);
	viewport.x = x;
    viewport.y = y + height;
	viewport.width = width;
    viewport.height = -height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	vkCmdSetViewport(pCmd->vk.pVkCmdBuf, 0, 1, &viewport);
}

void vk_cmdSetScissor(cmd_t* pCmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	DECLARE_ZERO(VkRect2D, rect);
	rect.offset.x = x;
	rect.offset.y = y;
	rect.extent.width = width;
	rect.extent.height = height;
	vkCmdSetScissor(pCmd->vk.pVkCmdBuf, 0, 1, &rect);
}

void vk_cmdSetStencilReferenceValue(cmd_t* pCmd, uint32_t val)
{
	TC_ASSERT(pCmd);

	vkCmdSetStencilReference(pCmd->vk.pVkCmdBuf, VK_STENCIL_FRONT_AND_BACK, val);
}

void vk_cmdBindPipeline(cmd_t* pCmd, pipeline_t* pipeline)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pipeline);
	TC_ASSERT(pCmd->vk.pVkCmdBuf != VK_NULL_HANDLE);

	VkPipelineBindPoint pipeline_bind_point = gPipelineBindPoint[pipeline->vk.mType];
	vkCmdBindPipeline(pCmd->vk.pVkCmdBuf, pipeline_bind_point, pipeline->vk.pVkPipeline);
}

void vk_cmdBindIndexBuffer(cmd_t* pCmd, buffer_t* buffer, uint32_t indexType, uint64_t offset)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(buffer);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	VkIndexType vk_index_type = (INDEX_TYPE_UINT16 == indexType) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
	vkCmdBindIndexBuffer(pCmd->vk.pVkCmdBuf, buffer->vk.buffer, offset, vk_index_type);
}

void vk_cmdBindVertexBuffer(cmd_t* pCmd, uint32_t bufferCount, buffer_t** pbuffers, const uint32_t* pStrides, const uint64_t* pOffsets)
{
	UNREF_PARAM(pStrides);

	TC_ASSERT(pCmd);
	TC_ASSERT(0 != bufferCount);
	TC_ASSERT(pbuffers);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);
	TC_ASSERT(pStrides);

	const uint32_t max_buffers = pCmd->r->vk.activegpuprops->properties.limits.maxVertexInputBindings;
	uint32_t       capped_buffer_count = bufferCount > max_buffers ? max_buffers : bufferCount;

	// No upper bound for this, so use 64 for now
	TC_ASSERT(capped_buffer_count < 64);

	DECLARE_ZERO(VkBuffer, buffers[64]);
	DECLARE_ZERO(VkDeviceSize, offsets[64]);

	for (uint32_t i = 0; i < capped_buffer_count; ++i)
	{
		buffers[i] = pbuffers[i]->vk.buffer;
		offsets[i] = (pOffsets ? pOffsets[i] : 0);
	}

	vkCmdBindVertexBuffers(pCmd->vk.pVkCmdBuf, 0, capped_buffer_count, buffers, offsets);
}

void vk_cmdDraw(cmd_t* pCmd, uint32_t vertex_count, uint32_t first_vertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	vkCmdDraw(pCmd->vk.pVkCmdBuf, vertex_count, 1, first_vertex, 0);
}

void vk_cmdDrawInstanced(cmd_t* pCmd, uint32_t vertexCount, uint32_t firstVertex, uint32_t instanceCount, uint32_t firstInstance)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	vkCmdDraw(pCmd->vk.pVkCmdBuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void vk_cmdDrawIndexed(cmd_t* pCmd, uint32_t index_count, uint32_t first_index, uint32_t first_vertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->vk.pVkCmdBuf, index_count, 1, first_index, first_vertex, 0);
}

void vk_cmdDrawIndexedInstanced(
	cmd_t* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t instanceCount, uint32_t firstInstance, uint32_t firstVertex)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(VK_NULL_HANDLE != pCmd->vk.pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->vk.pVkCmdBuf, indexCount, instanceCount, firstIndex, firstVertex, firstInstance);
}

void vk_cmdDispatch(cmd_t* pCmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pCmd->vk.pVkCmdBuf != VK_NULL_HANDLE);

	vkCmdDispatch(pCmd->vk.pVkCmdBuf, groupCountX, groupCountY, groupCountZ);
}

void vk_cmd_resourcebarrier(
	cmd_t* pCmd, uint32_t numBufferBarriers, BufferBarrier* bufferBarriers, uint32_t numTextureBarriers, TextureBarrier* pTextureBarriers,
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
		BufferBarrier*         pTrans = &bufferBarriers[i];
		buffer_t*                buffer = pTrans->buffer;
		VkBufferMemoryBarrier* bufferBarrier = NULL;

		if (RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mCurrentState && RESOURCE_STATE_UNORDERED_ACCESS == pTrans->mNewState)
		{
			bufferBarrier = &bufferBarriers[bufferBarrierCount++];             //-V522
			bufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;    //-V522
			bufferBarrier->pNext = NULL;

			bufferBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			bufferBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
		else
		{
			bufferBarrier = &bufferBarriers[bufferBarrierCount++];
			bufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			bufferBarrier->pNext = NULL;

			bufferBarrier->srcAccessMask = util_to_vk_access_flags(pTrans->mCurrentState);
			bufferBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);
		}

		if (bufferBarrier)
		{
			bufferBarrier->buffer = buffer->vk.buffer;
			bufferBarrier->size = VK_WHOLE_SIZE;
			bufferBarrier->offset = 0;

			if (pTrans->mAcquire)
			{
				bufferBarrier->srcQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
				bufferBarrier->dstQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease)
			{
				bufferBarrier->srcQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
				bufferBarrier->dstQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
			}
			else
			{
				bufferBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bufferBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}

			srcAccessFlags |= bufferBarrier->srcAccessMask;
			dstAccessFlags |= bufferBarrier->dstAccessMask;
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
			pImageBarrier->image = texture->vk.image;
			pImageBarrier->subresourceRange.aspectMask = (VkImageAspectFlags)texture->mAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = pTrans->mSubresourceBarrier ? pTrans->mMipLevel : 0;
			pImageBarrier->subresourceRange.levelCount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = pTrans->mSubresourceBarrier ? pTrans->mArrayLayer : 0;
			pImageBarrier->subresourceRange.layercount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;

			if (pTrans->mAcquire && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
				pImageBarrier->dstQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
				pImageBarrier->dstQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
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
		texture_t*              pTexture = pTrans->rendertarget->pTexture;
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
			pImageBarrier->image = texture->vk.image;
			pImageBarrier->subresourceRange.aspectMask = (VkImageAspectFlags)texture->mAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = pTrans->mSubresourceBarrier ? pTrans->mMipLevel : 0;
			pImageBarrier->subresourceRange.levelCount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = pTrans->mSubresourceBarrier ? pTrans->mArrayLayer : 0;
			pImageBarrier->subresourceRange.layercount = pTrans->mSubresourceBarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;

			if (pTrans->mAcquire && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
				pImageBarrier->dstQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
			}
			else if (pTrans->mRelease && pTrans->mCurrentState != RESOURCE_STATE_UNDEFINED)
			{
				pImageBarrier->srcQueueFamilyIndex = pCmd->pQueue->vk.mVkQueueFamilyIndex;
				pImageBarrier->dstQueueFamilyIndex = pCmd->r->vk.mQueueFamilyIndices[pTrans->mQueueType];
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
		util_determine_pipeline_stage_flags(pCmd->r, srcAccessFlags, (QueueType)pCmd->vk.mType);
	VkPipelineStageFlags dstStageMask =
		util_determine_pipeline_stage_flags(pCmd->r, dstAccessFlags, (QueueType)pCmd->vk.mType);

	if (bufferBarrierCount || imageBarrierCount)
	{
		vkCmdPipelineBarrier(
			pCmd->vk.pVkCmdBuf, srcStageMask, dstStageMask, 0, 0, NULL, bufferBarrierCount, bufferBarriers, imageBarrierCount,
			imageBarriers);
	}
}

void vk_cmdUpdateBuffer(cmd_t* pCmd, buffer_t* buffer, uint64_t dstOffset, buffer_t* pSrcBuffer, uint64_t srcOffset, uint64_t size)
{
	TC_ASSERT(pCmd);
	TC_ASSERT(pSrcBuffer);
	TC_ASSERT(pSrcBuffer->vk.buffer);
	TC_ASSERT(buffer);
	TC_ASSERT(buffer->vk.buffer);
	TC_ASSERT(srcOffset + size <= pSrcBuffer->mSize);
	TC_ASSERT(dstOffset + size <= buffer->mSize);

	DECLARE_ZERO(VkBufferCopy, region);
	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size = (VkDeviceSize)size;
	vkCmdCopyBuffer(pCmd->vk.pVkCmdBuf, pSrcBuffer->vk.buffer, buffer->vk.buffer, 1, &region);
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
	const TinyImageFormat fmt = (TinyImageFormat)texture->format;
	const bool            single_plane = TinyImageFormat_IsSinglePlane(fmt);

	if (single_plane)
	{
		const uint32_t width = max<uint32_t>(1, texture->width >> pSubresourceDesc->mMipLevel);
		const uint32_t height = max<uint32_t>(1, texture->height >> pSubresourceDesc->mMipLevel);
		const uint32_t depth = max<uint32_t>(1, texture->depth >> pSubresourceDesc->mMipLevel);
		const uint32_t numBlocksWide = pSubresourceDesc->mRowPitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t numBlocksHigh = (pSubresourceDesc->mSlicePitch / pSubresourceDesc->mRowPitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = pSubresourceDesc->mSrcOffset;
		copy.bufferRowLength = numBlocksWide * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = numBlocksHigh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)texture->mAspectMask;
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
			pCmd->vk.pVkCmdBuf, pSrcBuffer->vk.buffer, texture->vk.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copy);
	}
	else
	{
		const uint32_t width = texture->width;
		const uint32_t height = texture->height;
		const uint32_t depth = texture->depth;
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
			pCmd->vk.pVkCmdBuf, pSrcBuffer->vk.buffer, texture->vk.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			numOfPlanes, bufferImagesCopy);
	}
}

void vk_cmdCopySubresource(cmd_t* pCmd, buffer_t* pDstBuffer, texture_t* pTexture, const SubresourceDataDesc* pSubresourceDesc)
{
	const TinyImageFormat fmt = (TinyImageFormat)texture->format;
	const bool            single_plane = TinyImageFormat_IsSinglePlane(fmt);

	if (single_plane)
	{
		const uint32_t width = max<uint32_t>(1, texture->width >> pSubresourceDesc->mMipLevel);
		const uint32_t height = max<uint32_t>(1, texture->height >> pSubresourceDesc->mMipLevel);
		const uint32_t depth = max<uint32_t>(1, texture->depth >> pSubresourceDesc->mMipLevel);
		const uint32_t numBlocksWide = pSubresourceDesc->mRowPitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t numBlocksHigh = (pSubresourceDesc->mSlicePitch / pSubresourceDesc->mRowPitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = pSubresourceDesc->mSrcOffset;
		copy.bufferRowLength = numBlocksWide * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = numBlocksHigh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)texture->mAspectMask;
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
			pCmd->vk.pVkCmdBuf, texture->vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstBuffer->vk.buffer, 1,
			&copy);
	}
	else
	{
		const uint32_t width = texture->width;
		const uint32_t height = texture->height;
		const uint32_t depth = texture->depth;
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
			pCmd->vk.pVkCmdBuf, texture->vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDstBuffer->vk.buffer,
			numOfPlanes, bufferImagesCopy);
	}
}
/************************************************************************/
// Queue Fence Semaphore Functions
/************************************************************************/
void vk_acquireNextImage(renderer_t* r, SwapChain* pSwapChain, Semaphore* pSignalSemaphore, fence_t* pFence, uint32_t* pImageIndex)
{
	TC_ASSERT(r);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(pSignalSemaphore || pFence);

#if defined(QUEST_VR)
    TC_ASSERT(VK_NULL_HANDLE != pSwapChain->mVR.pSwapChain);
    hook_acquire_next_image(pSwapChain, pImageIndex);
    return;
#else
    TC_ASSERT(VK_NULL_HANDLE != pSwapChain->vk.pSwapChain);
#endif

	VkResult vk_res = { 0 };

	if (pFence != NULL)
	{
		vk_res = vkAcquireNextImageKHR(
			r->vk.device, pSwapChain->vk.pSwapChain, UINT64_MAX, VK_NULL_HANDLE, pFence->vk.pVkFence,
			pImageIndex);

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			vkResetFences(r->vk.device, 1, &pFence->vk.pVkFence);
			pFence->vk.mSubmitted = false;
			return;
		}

		pFence->vk.mSubmitted = true;
	}
	else
	{
		vk_res = vkAcquireNextImageKHR(
			r->vk.device, pSwapChain->vk.pSwapChain, UINT64_MAX, pSignalSemaphore->vk.pVkSemaphore,
			VK_NULL_HANDLE, pImageIndex);    //-V522

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			pSignalSemaphore->vk.mSignaled = false;
			return;
		}

		// Commonly returned immediately following swapchain resize. 
		// Vulkan spec states that this return value constitutes a successful call to vkAcquireNextImageKHR
		// https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAcquireNextImageKHR.html
		if (vk_res == VK_SUBOPTIMAL_KHR)
		{
			TRACE(LOG_INFO, "vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR. If window was just resized, ignore this message."); 
			pSignalSemaphore->vk.mSignaled = true;
			return; 
		}

		CHECK_VKRESULT(vk_res);
		pSignalSemaphore->vk.mSignaled = true;
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

	TC_ASSERT(VK_NULL_HANDLE != pQueue->vk.pVkQueue);

	VkCommandbuffer_t* cmds = (VkCommandbuffer_t*)alloca(cmdCount * sizeof(VkCommandBuffer));
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		cmds[i] = ppCmds[i]->vk.pVkCmdBuf;
	}

	VkSemaphore*          wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	VkPipelineStageFlags* wait_masks = (VkPipelineStageFlags*)alloca(waitSemaphoreCount * sizeof(VkPipelineStageFlags));
	uint32_t              waitCount = 0;
	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
	{
		if (ppWaitSemaphores[i]->vk.mSignaled)
		{
			wait_semaphores[waitCount] = ppWaitSemaphores[i]->vk.pVkSemaphore;    //-V522
			wait_masks[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			++waitCount;

			ppWaitSemaphores[i]->vk.mSignaled = false;
		}
	}

	VkSemaphore* signal_semaphores = signalSemaphoreCount ? (VkSemaphore*)alloca(signalSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	uint32_t     signalCount = 0;
	for (uint32_t i = 0; i < signalSemaphoreCount; ++i)
	{
		if (!ppSignalSemaphores[i]->vk.mSignaled)
		{
			signal_semaphores[signalCount] = ppSignalSemaphores[i]->vk.pVkSemaphore;    //-V522
			ppSignalSemaphores[i]->vk.mCurrentNodeIndex = pQueue->nodeidx;
			ppSignalSemaphores[i]->vk.mSignaled = true;
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
	if (pQueue->vk.gpumode == GPU_MODE_LINKED)
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
			pVkDeviceMasks[i] = (1 << ppCmds[i]->vk.nodeidx);
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.signalSemaphoreCount; ++i)
		{
			pSignalIndices[i] = pQueue->nodeidx;
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.waitSemaphoreCount; ++i)
		{
			pWaitIndices[i] = ppWaitSemaphores[i]->vk.mCurrentNodeIndex;
		}

		deviceGroupSubmitInfo.pCommandBufferDeviceMasks = pVkDeviceMasks;
		deviceGroupSubmitInfo.pSignalSemaphoreDeviceIndices = pSignalIndices;
		deviceGroupSubmitInfo.pWaitSemaphoreDeviceIndices = pWaitIndices;
		submit_info.pNext = &deviceGroupSubmitInfo;
	}

	// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
	// Many setups have just one queue family and one queue. In this case, async compute, async transfer doesn't exist and we end up using
	// the same queue for all three operations
	lock_tLock lock(*pQueue->vk.pSubmitlock_t);
	CHECK_VKRESULT(vkQueueSubmit(pQueue->vk.pVkQueue, 1, &submit_info, pFence ? pFence->vk.pVkFence : VK_NULL_HANDLE));

	if (pFence)
		pFence->vk.mSubmitted = true;
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

		TC_ASSERT(VK_NULL_HANDLE != pQueue->vk.pVkQueue);

		VkSemaphore* wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
		uint32_t     waitCount = 0;
		for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		{
			if (ppWaitSemaphores[i]->vk.mSignaled)
			{
				wait_semaphores[waitCount] = ppWaitSemaphores[i]->vk.pVkSemaphore;    //-V522
				ppWaitSemaphores[i]->vk.mSignaled = false;
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
		present_info.pSwapchains = &(pSwapChain->vk.pSwapChain);
		present_info.pImageIndices = &(presentIndex);
		present_info.pResults = NULL;

		// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
		lock_tLock lock(*pQueue->vk.pSubmitlock_t);
		VkResult  vk_res = vkQueuePresentKHR(
            pSwapChain->vk.pPresentQueue ? pSwapChain->vk.pPresentQueue : pQueue->vk.pVkQueue, &present_info);

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

void vk_wait_for_fences(renderer_t* r, uint32_t fenceCount, fence_t** ppFences)
{
	TC_ASSERT(r);
	TC_ASSERT(fenceCount);
	TC_ASSERT(ppFences);

	Vkfence_t* fences = (Vkfence_t*)alloca(fenceCount * sizeof(VkFence));
	uint32_t numValidFences = 0;
	for (uint32_t i = 0; i < fenceCount; ++i)
	{
		if (ppFences[i]->vk.mSubmitted)
			fences[numValidFences++] = ppFences[i]->vk.pVkFence;
	}

	if (numValidFences)
	{
		CHECK_VKRESULT(vkWaitForFences(r->vk.device, numValidFences, fences, VK_TRUE, UINT64_MAX));
		CHECK_VKRESULT(vkResetFences(r->vk.device, numValidFences, fences));
	}

	for (uint32_t i = 0; i < fenceCount; ++i)
		ppFences[i]->vk.mSubmitted = false;
}

void vk_waitQueueIdle(queue_t* pQueue) { vkQueueWaitIdle(pQueue->vk.pVkQueue); }

void vk_getFenceStatus(renderer_t* r, fence_t* pFence, FenceStatus* pFenceStatus)
{
	*pFenceStatus = FENCE_STATUS_COMPLETE;

	if (pFence->vk.mSubmitted)
	{
		VkResult vkRes = vkGetFenceStatus(r->vk.device, pFence->vk.pVkFence);
		if (vkRes == VK_SUCCESS)
		{
			vkResetFences(r->vk.device, 1, &pFence->vk.pVkFence);
			pFence->vk.mSubmitted = false;
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
void vk_addIndirectCommandSignature(renderer_t* r, const CommandSignatureDesc* desc, CommandSignature** ppCommandSignature)
{
	TC_ASSERT(r);
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

void vk_removeIndirectCommandSignature(renderer_t* r, CommandSignature* pCommandSignature)
{
	TC_ASSERT(r);
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
		if (pCmd->r->activegpusettings->mMultiDrawIndirect)
		{
#ifndef NX64
			if (pCounterBuffer && drawIndirectCount)
			{
				drawIndirectCount(
					pCmd->vk.pVkCmdBuf, pIndirectBuffer->vk.buffer, bufferOffset, pCounterBuffer->vk.buffer,
					counterBufferOffset, maxCommandCount, pCommandSignature->mStride);
			}
			else
#endif
			{
				drawIndirect(
					pCmd->vk.pVkCmdBuf, pIndirectBuffer->vk.buffer, bufferOffset, maxCommandCount, pCommandSignature->mStride);
			}
		}
		else
		{
			// Cannot use counter buffer when MDI is not supported. We will blindly loop through maxCommandCount
			TC_ASSERT(!pCounterBuffer);

			for (uint32_t cmd = 0; cmd < maxCommandCount; ++cmd)
			{
				drawIndirect(
					pCmd->vk.pVkCmdBuf, pIndirectBuffer->vk.buffer, bufferOffset + cmd * pCommandSignature->mStride, 1, pCommandSignature->mStride);
			}
		}
	}
	else if (pCommandSignature->mDrawType == INDIRECT_DISPATCH)
	{
		for (uint32_t i = 0; i < maxCommandCount; ++i)
		{
			vkCmdDispatchIndirect(pCmd->vk.pVkCmdBuf, pIndirectBuffer->vk.buffer, bufferOffset + i * pCommandSignature->mStride);
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
		((double)pQueue->vk.mTimestampPeriod /*ns/tick number of nanoseconds required for a timestamp query to be incremented by 1*/
		 * 1e-9);                                 // convert to ticks/sec (DX12 standard)
}

void vk_addQueryPool(renderer_t* r, const QueryPoolDesc* desc, QueryPool** ppQueryPool)
{
	TC_ASSERT(r);
	TC_ASSERT(desc);
	TC_ASSERT(ppQueryPool);

	QueryPool* pQueryPool = (QueryPool*)tc_calloc(1, sizeof(QueryPool));
	TC_ASSERT(ppQueryPool);

	pQueryPool->vk.mType = util_to_vk_query_type(desc->mType);
	pQueryPool->mCount = desc->mQueryCount;

	VkQueryPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.pNext = NULL;
	info.queryCount = desc->mQueryCount;
	info.queryType = util_to_vk_query_type(desc->mType);
	info.flags = 0;
	info.pipelineStatistics = 0;
	CHECK_VKRESULT(
		vkCreateQueryPool(r->vk.device, &info, &alloccbs, &pQueryPool->vk.pVkQueryPool));

	*ppQueryPool = pQueryPool;
}

void vk_removeQueryPool(renderer_t* r, QueryPool* pQueryPool)
{
	TC_ASSERT(r);
	TC_ASSERT(pQueryPool);
	vkDestroyQueryPool(r->vk.device, pQueryPool->vk.pVkQueryPool, &alloccbs);

	tc_free(pQueryPool);
}

void vk_cmdResetQueryPool(cmd_t* pCmd, QueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount)
{
	vkCmdResetQueryPool(pCmd->vk.pVkCmdBuf, pQueryPool->vk.pVkQueryPool, startQuery, queryCount);
}

void vk_cmdBeginQuery(cmd_t* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	VkQueryType type = pQueryPool->vk.mType;
	switch (type)
	{
		case VK_QUERY_TYPE_TIMESTAMP:
			vkCmdWriteTimestamp(
				pCmd->vk.pVkCmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pQueryPool->vk.pVkQueryPool, pQuery->mIndex);
			break;
		case VK_QUERY_TYPE_PIPELINE_STATISTICS: break;
		case VK_QUERY_TYPE_OCCLUSION: break;
		default: break;
	}
}

void vk_cmdEndQuery(cmd_t* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	VkQueryType type = pQueryPool->vk.mType;
	switch (type)
	{
	case VK_QUERY_TYPE_TIMESTAMP:
		vkCmdWriteTimestamp(
			pCmd->vk.pVkCmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pQueryPool->vk.pVkQueryPool, pQuery->mIndex);
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
		pCmd->vk.pVkCmdBuf, pQueryPool->vk.pVkQueryPool, startQuery, queryCount, pReadbackBuffer->vk.buffer, 0,
		sizeof(uint64_t), flags);
}
/************************************************************************/
// Memory Stats Implementation
/************************************************************************/
void vk_calculateMemoryStats(renderer_t* r, char** stats)
{
	vmaBuildStatsString(r->vk.vmaAllocator, stats, VK_TRUE);
}

void vk_freeMemoryStats(renderer_t* r, char* stats)
{
	vmaFreeStatsString(r->vk.vmaAllocator, stats);
}

void vk_calculateMemoryUse(renderer_t* r, uint64_t* usedBytes, uint64_t* totalAllocatedBytes)
{
	VmaTotalStatistics stats = { 0 };
	vmaCalculateStatistics(r->vk.vmaAllocator, &stats);
	*usedBytes = stats.total.statistics.allocationBytes;
	*totalAllocatedBytes = stats.total.statistics.blockBytes;
}
/************************************************************************/
// Debug Marker Implementation
/************************************************************************/
void vk_cmdBeginDebugMarker(cmd_t* pCmd, float r, float g, float b, const char* pName)
{
	if (pCmd->r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdBeginDebugUtilsLabelEXT(pCmd->vk.pVkCmdBuf, &markerInfo);
#elif !defined(NX64) || !defined(ENABLE_RENDER_DOC)
		VkDebugMarkerMarkerInfoEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerBeginEXT(pCmd->vk.pVkCmdBuf, &markerInfo);
#endif
	}
}

void vk_cmdEndDebugMarker(cmd_t* pCmd)
{
	if (pCmd->r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		vkCmdEndDebugUtilsLabelEXT(pCmd->vk.pVkCmdBuf);
#elif !defined(NX64) || !defined(ENABLE_RENDER_DOC)
		vkCmdDebugMarkerEndEXT(pCmd->vk.pVkCmdBuf);
#endif
	}
}

void vk_cmdAddDebugMarker(cmd_t* pCmd, float r, float g, float b, const char* pName)
{
	if (pCmd->r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdInsertDebugUtilsLabelEXT(pCmd->vk.pVkCmdBuf, &markerInfo);
#else
		VkDebugMarkerMarkerInfoEXT markerInfo = { 0 };
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerInsertEXT(pCmd->vk.pVkCmdBuf, &markerInfo);
#endif
	}
}

uint32_t vk_cmdWriteMarker(cmd_t* pCmd, MarkerType markerType, uint32_t markerValue, buffer_t* buffer, size_t offset, bool useAutoFlags)
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

void vk_setBufferName(renderer_t* r, buffer_t* buffer, const char* pName)
{
	TC_ASSERT(r);
	TC_ASSERT(buffer);
	TC_ASSERT(pName);

	if (r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)buffer->vk.buffer, VK_OBJECT_TYPE_BUFFER, pName);
#else
		util_set_object_name(r->vk.device, (uint64_t)buffer->vk.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, pName);
#endif
	}
}

void vk_setTextureName(renderer_t* r, texture_t* pTexture, const char* pName)
{
	TC_ASSERT(r);
	TC_ASSERT(pTexture);
	TC_ASSERT(pName);

	if (r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)texture->vk.image, VK_OBJECT_TYPE_IMAGE, pName);
#else
		util_set_object_name(r->vk.device, (uint64_t)texture->vk.image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, pName);
#endif
	}
}

void vk_setRenderTargetName(renderer_t* r, RenderTarget* rendertarget, const char* pName)
{
	setTextureName(r, rendertarget->pTexture, pName);
}

void vk_setPipelineName(renderer_t* r, pipeline_t* pipeline, const char* pName)
{
	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(pName);

	if (r->vk.mDebugMarkerSupport)
	{
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)pipeline->vk.pVkPipeline, VK_OBJECT_TYPE_PIPELINE, pName);
#else
		util_set_object_name(
			r->vk.device, (uint64_t)pipeline->vk.pVkPipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, pName);
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
static bool allocateVirtualPage(renderer_t* r, texture_t* pTexture, VirtualTexturePage& virtualPage, buffer_t** ppIntermediateBuffer)
{
	if (virtualPage.vk.imageMemoryBind.memory != VK_NULL_HANDLE)
	{
		//already filled
		return false;
	};

	BufferDesc desc = { 0 };
	desc.descriptors = DESCRIPTOR_TYPE_RW_BUFFER;
	desc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_ONLY;

	desc.mFirstElement = 0;
	desc.mElementCount = texture->pSvt->mSparseVirtualTexturePageWidth * texture->pSvt->mSparseVirtualTexturePageHeight;
	desc.stride = sizeof(uint32_t);
	desc.mSize = desc.mElementCount * desc.stride;
#if defined(ENABLE_GRAPHICS_DEBUG)
	char debugNameBuffer[MAX_DEBUG_NAME_LENGTH]{ 0 };
	snprintf(debugNameBuffer, MAX_DEBUG_NAME_LENGTH, "(tex %p) VT page #%u intermediate buffer", pTexture, virtualPage.index);
	desc.pName = debugNameBuffer;
#endif
	add_buffer(r, &desc, ppIntermediateBuffer);

	VkMemoryRequirements memReqs = { 0 };
	memReqs.size = virtualPage.vk.size;
	memReqs.memoryTypeBits = texture->pSvt->vk.mSparseMemoryTypeBits;
	memReqs.alignment = memReqs.size;

	VmaAllocationCreateInfo vmaAllocInfo = { 0 };
	vmaAllocInfo.pool = (VmaPool)texture->pSvt->vk.pPool;

	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &memReqs, &vmaAllocInfo, &allocation, &allocationInfo));
	TC_ASSERT(allocation->GetAlignment() == memReqs.size || allocation->GetAlignment() == 0);

	virtualPage.vk.pAllocation = allocation;
	virtualPage.vk.imageMemoryBind.memory = allocation->GetMemory();
	virtualPage.vk.imageMemoryBind.memoryOffset = allocation->GetOffset();

	// Sparse image memory binding
	virtualPage.vk.imageMemoryBind.subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	virtualPage.vk.imageMemoryBind.subresource.mipLevel = virtualPage.mipLevel;
	virtualPage.vk.imageMemoryBind.subresource.arrayLayer = virtualPage.layer;

	++texture->pSvt->mVirtualPageAliveCount;

	return true;
}

VirtualTexturePage* addPage(renderer_t* r, texture_t* pTexture, const VkOffset3D& offset, const VkExtent3D& extent, const VkDeviceSize size, const uint32_t mipLevel, uint32_t layer, uint32_t pageIndex)
{
	VirtualTexturePage& newPage = texture->pSvt->pPages[pageIndex];

	newPage.vk.imageMemoryBind.offset = offset;
	newPage.vk.imageMemoryBind.extent = extent;
	newPage.vk.size = size;
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
		pSvt->vk.pPendingDeletedAllocations = (void**)tf_realloc(pSvt->vk.pPendingDeletedAllocations,
			pSvt->mPendingDeletionCount * pSvt->mVirtualPageTotalCount * sizeof(pSvt->vk.pPendingDeletedAllocations[0]));

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
	pendingDeletion.pAllocations = (VmaAllocation*)&pSvt->vk.pPendingDeletedAllocations[currentImage * pSvt->mVirtualPageTotalCount];
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
		imageMemoryBindInfo.image = texture->vk.image;
		imageMemoryBindInfo.bindCount = imageMemoryCount;
		imageMemoryBindInfo.pBinds = texture->pSvt->vk.pSparseImageMemoryBinds;
		bindSparseInfo.imageBindCount = 1;
		bindSparseInfo.pImageBinds = &imageMemoryBindInfo;

		// Opaque image memory binds (mip tail)
		VkSparseImageOpaqueMemoryBindInfo opaqueMemoryBindInfo = { 0 };
		opaqueMemoryBindInfo.image = texture->vk.image;
		opaqueMemoryBindInfo.bindCount = texture->pSvt->vk.mOpaqueMemoryBindsCount;
		opaqueMemoryBindInfo.pBinds = texture->pSvt->vk.pOpaqueMemoryBinds;
		bindSparseInfo.imageOpaqueBindCount = (opaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		bindSparseInfo.pImageOpaqueBinds = &opaqueMemoryBindInfo;

		CHECK_VKRESULT(vkQueueBindSparse(pCmd->pQueue->vk.pVkQueue, (uint32_t)1, &bindSparseInfo, VK_NULL_HANDLE));
	}
}

void vk_releasePage(cmd_t* pCmd, texture_t* pTexture, uint32_t currentImage)
{
	r_t* r = pCmd->r;

	VirtualTexturePage* pPageTable = texture->pSvt->pPages;

	VTReadbackBufOffsets offsets = vtGetReadbackBufOffsets(
		(uint32_t*)texture->pSvt->pReadbackBuffer->pCpuMappedAddress,
		texture->pSvt->mReadbackBufferSize,
		texture->pSvt->mVirtualPageTotalCount,
		currentImage);

	const uint removePageCount = *offsets.pRemovePageCount;
	const uint32_t* RemovePageTable = offsets.pRemovePages;

	const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(texture->pSvt, currentImage);

	// Release pending intermediate buffers
	{
		for (size_t i = 0; i < *pendingDeletion.pIntermediateBuffersCount; i++)
			remove_buffer(r, pendingDeletion.pIntermediateBuffers[i]);

		for (size_t i = 0; i < *pendingDeletion.pAllocationsCount; i++)
			vmaFreeMemory(r->vk.vmaAllocator, pendingDeletion.pAllocations[i]);

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
		if ((int)removePage.mipLevel >= (texture->pSvt->mTiledMipLevelCount - 1))
			continue;

		TC_ASSERT(!!removePage.vk.pAllocation == !!removePage.vk.imageMemoryBind.memory);
		if (removePage.vk.pAllocation)
		{
			TC_ASSERT(((VmaAllocation)removePage.vk.pAllocation)->GetMemory() == removePage.vk.imageMemoryBind.memory);
			pendingDeletion.pAllocations[(*pendingDeletion.pAllocationsCount)++] = (VmaAllocation)removePage.vk.pAllocation;
			removePage.vk.pAllocation = VK_NULL_HANDLE;
			removePage.vk.imageMemoryBind.memory = VK_NULL_HANDLE;

			VkSparseImageMemoryBind& unbind = texture->pSvt->vk.pSparseImageMemoryBinds[pageUnbindCount++];
			unbind = { 0 };
			unbind.offset = removePage.vk.imageMemoryBind.offset;
			unbind.extent = removePage.vk.imageMemoryBind.extent;
			unbind.subresource = removePage.vk.imageMemoryBind.subresource;

			--texture->pSvt->mVirtualPageAliveCount;
		}
	}

	// Unmap tiles
	vk_updateVirtualTextureMemory(pCmd, pTexture, pageUnbindCount);
}

void vk_uploadVirtualTexturePage(cmd_t* pCmd, texture_t* pTexture, VirtualTexturePage* pPage, uint32_t* imageMemoryCount, uint32_t currentImage)
{
	buffer_t* pIntermediateBuffer = NULL;
	if (allocateVirtualPage(pCmd->r, pTexture, *pPage, &pIntermediateBuffer))
	{
		void* pData = (void*)((unsigned char*)texture->pSvt->pVirtualImageData + (pPage->index * pPage->vk.size));

		const bool intermediateMap = !pIntermediateBuffer->pCpuMappedAddress;
		if (intermediateMap)
		{
			mabuffer(pCmd->r, pIntermediateBuffer, NULL);
		}

		//CPU to GPU
		memcpy(pIntermediateBuffer->pCpuMappedAddress, pData, pPage->vk.size);

		if (intermediateMap)
		{
			unmabuffer(pCmd->r, pIntermediateBuffer);
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

		region.imageOffset = pPage->vk.imageMemoryBind.offset;
		region.imageOffset.z = 0;
		region.imageExtent = { (uint32_t)texture->pSvt->mSparseVirtualTexturePageWidth, (uint32_t)texture->pSvt->mSparseVirtualTexturePageHeight, 1 };

		vkCmdCopyBufferToImage(
			pCmd->vk.pVkCmdBuf,
			pIntermediateBuffer->vk.buffer,
			texture->vk.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region);

		// Update list of memory-backed sparse image memory binds
		texture->pSvt->vk.pSparseImageMemoryBinds[(*imageMemoryCount)++] = pPage->vk.imageMemoryBind;

		// Schedule deletion of this intermediate buffer
		const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(texture->pSvt, currentImage);
		pendingDeletion.pIntermediateBuffers[(*pendingDeletion.pIntermediateBuffersCount)++] = pIntermediateBuffer;
	}
}

// Fill a complete mip level
// Need to get visibility info first then fill them
void vk_fillVirtualTexture(cmd_t* pCmd, texture_t* pTexture, fence_t* pFence, uint32_t currentImage)
{
	uint32_t imageMemoryCount = 0;

	VTReadbackBufOffsets readbackOffsets = vtGetReadbackBufOffsets(
		(uint*)texture->pSvt->pReadbackBuffer->pCpuMappedAddress,
		texture->pSvt->mReadbackBufferSize,
		texture->pSvt->mVirtualPageTotalCount,
		currentImage);

	const uint alivePageCount = *readbackOffsets.pAlivePageCount;
	uint32_t* VisibilityData = readbackOffsets.pAlivePages;

	for (int i = 0; i < (int)alivePageCount; ++i)
	{
		uint pageIndex = VisibilityData[i];
		TC_ASSERT(pageIndex < texture->pSvt->mVirtualPageTotalCount);
		VirtualTexturePage* pPage = &texture->pSvt->pPages[pageIndex];
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

	for (uint32_t i = 0; i < texture->pSvt->mVirtualPageTotalCount; i++)
	{
		VirtualTexturePage* pPage = &texture->pSvt->pPages[i];
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

	r_t* r = pCmd->r;

	texture->pSvt = (Virtualtexture_t*)(pTexture + 1);

	uint32_t imageSize = 0;
	uint32_t mipSize = desc->width * desc->height * desc->depth;
	while (mipSize > 0)
	{
		imageSize += mipSize;
		mipSize /= 4;
	}

	texture->pSvt->pVirtualImageData = pImageData;
	texture->format = desc->format;
	TC_ASSERT(texture->format == desc->format);

	VkFormat format = (VkFormat)TinyImageFormat_ToVkFormat((TinyImageFormat)texture->format);
	TC_ASSERT(VK_FORMAT_UNDEFINED != format);
	texture->ownsimage = true;

	VkImageCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = desc->width;
	info.extent.height = desc->height;
	info.extent.depth = desc->depth;
	info.mipLevels = desc->miplevels;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	CHECK_VKRESULT(vkCreateImage(r->vk.device, &info, &alloccbs, &texture->vk.image));

	// Get memory requirements
	VkMemoryRequirements sparseImageMemoryReqs;
	// Sparse image memory requirement counts
	vkGetImageMemoryRequirements(r->vk.device, texture->vk.image, &sparseImageMemoryReqs);

	// Check requested image size against hardware sparse limit
	if (sparseImageMemoryReqs.size > r->vk.activegpuprops->properties.limits.sparseAddressSpaceSize)
	{
		TRACE(LOG_ERROR, "Requested sparse image size exceeds supported sparse address space size!");
		return;
	}

	// Get sparse memory requirements
	// Count
	uint32_t sparseMemoryReqsCount;
	vkGetImageSparseMemoryRequirements(r->vk.device, texture->vk.image, &sparseMemoryReqsCount, NULL);  // Get count
	VkSparseImageMemoryRequirements* sparseMemoryReqs = NULL;

	if (sparseMemoryReqsCount == 0)
	{
		TRACE(LOG_ERROR, "No memory requirements for the sparse image!");
		return;
	}
	else
	{
		sparseMemoryReqs = (VkSparseImageMemoryRequirements*)tc_calloc(sparseMemoryReqsCount, sizeof(VkSparseImageMemoryRequirements));
		vkGetImageSparseMemoryRequirements(r->vk.device, texture->vk.image, &sparseMemoryReqsCount, sparseMemoryReqs);  // Get reqs
	}

	TC_ASSERT(sparseMemoryReqsCount == 1 && "Multiple sparse image memory requirements not currently implemented");

	texture->pSvt->mSparseVirtualTexturePageWidth = sparseMemoryReqs[0].formatProperties.imageGranularity.width;
	texture->pSvt->mSparseVirtualTexturePageHeight = sparseMemoryReqs[0].formatProperties.imageGranularity.height;
	texture->pSvt->mVirtualPageTotalCount = imageSize / (uint32_t)(texture->pSvt->mSparseVirtualTexturePageWidth * texture->pSvt->mSparseVirtualTexturePageHeight);
	texture->pSvt->mReadbackBufferSize = vtGetReadbackBufSize(texture->pSvt->mVirtualPageTotalCount, 1);
	texture->pSvt->mPageVisibilityBufferSize = texture->pSvt->mVirtualPageTotalCount * 2 * sizeof(uint);

	uint32_t TiledMiplevel = desc->miplevels - (uint32_t)log2(min((uint32_t)texture->pSvt->mSparseVirtualTexturePageWidth, (uint32_t)texture->pSvt->mSparseVirtualTexturePageHeight));
	texture->pSvt->mTiledMipLevelCount = (uint8_t)TiledMiplevel;

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

	VkPhysicalDeviceMemoryProperties props = { 0 };
	vkGetPhysicalDeviceMemoryProperties(r->vk.activegpu, &props);

	// todo:
	// Calculate number of required sparse memory bindings by alignment
	assert((sparseImageMemoryReqs.size % sparseImageMemoryReqs.alignment) == 0);
	texture->pSvt->vk.mSparseMemoryTypeBits = sparseImageMemoryReqs.memoryTypeBits;

	// Check if the format has a single mip tail for all layers or one mip tail for each layer
	// The mip tail contains all mip levels > sparseMemoryReq.imageMipTailFirstLod
	bool singleMipTail = sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;

	texture->pSvt->pPages = (VirtualTexturePage*)tc_calloc(texture->pSvt->mVirtualPageTotalCount, sizeof(VirtualTexturePage));
	texture->pSvt->vk.pSparseImageMemoryBinds = (VkSparseImageMemoryBind*)tc_calloc(texture->pSvt->mVirtualPageTotalCount, sizeof(VkSparseImageMemoryBind));

	texture->pSvt->vk.pOpaqueMemoryBindAllocations = (void**)tc_calloc(texture->pSvt->mVirtualPageTotalCount, sizeof(VmaAllocation));
	texture->pSvt->vk.pOpaqueMemoryBinds = (VkSparseMemoryBind*)tc_calloc(texture->pSvt->mVirtualPageTotalCount, sizeof(VkSparseMemoryBind));

	VmaPoolCreateInfo poolCreateInfo = { 0 };
	poolCreateInfo.memtypeidx = _get_memory_type(sparseImageMemoryReqs.memoryTypeBits, props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CHECK_VKRESULT(vmaCreatePool(r->vk.vmaAllocator, &poolCreateInfo, (VmaPool*)&texture->pSvt->vk.pPool));

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
						VirtualTexturePage *newPage = addPage(r, pTexture, offset, extent, texture->pSvt->mSparseVirtualTexturePageWidth * texture->pSvt->mSparseVirtualTexturePageHeight * sizeof(uint), mipLevel, layer, currentPageIndex);
						currentPageIndex++;
						newPage->vk.imageMemoryBind.subresource = subResource;

						index++;
					}
				}
			}
		}

		// Check if format has one mip tail per layer
		if ((!singleMipTail) && (sparseMemoryReq.imageMipTailFirstLod < desc->miplevels))
		{
			// Allocate memory for the mip tail
			VkMemoryRequirements memReqs = { 0 };
			memReqs.size = sparseMemoryReq.imageMipTailSize;
			memReqs.memoryTypeBits = texture->pSvt->vk.mSparseMemoryTypeBits;
			memReqs.alignment = memReqs.size;

			VmaAllocationCreateInfo allocCreateInfo = { 0 };
			allocCreateInfo.memoryTypeBits = memReqs.memoryTypeBits;
			allocCreateInfo.pool = (VmaPool)texture->pSvt->vk.pPool;

			VmaAllocation allocation;
			VmaAllocationInfo allocationInfo;
			CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &memReqs, &allocCreateInfo, &allocation, &allocationInfo));

			// (Opaque) sparse memory binding
			VkSparseMemoryBind sparseMemoryBind{ 0 };
			sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset + layer * sparseMemoryReq.imageMipTailStride;
			sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
			sparseMemoryBind.memory = allocation->GetMemory();
			sparseMemoryBind.memoryOffset = allocation->GetOffset();

			texture->pSvt->vk.pOpaqueMemoryBindAllocations[texture->pSvt->vk.mOpaqueMemoryBindsCount] = allocation;
			texture->pSvt->vk.pOpaqueMemoryBinds[texture->pSvt->vk.mOpaqueMemoryBindsCount] = sparseMemoryBind;
			texture->pSvt->vk.mOpaqueMemoryBindsCount++;
		}
	}    // end layers and mips

	TRACE(LOG_INFO, "Virtual Texture info: Dim %d x %d Pages %d", desc->width, desc->height, texture->pSvt->mVirtualPageTotalCount);

	// Check if format has one mip tail for all layers
	if ((sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) &&
		(sparseMemoryReq.imageMipTailFirstLod < desc->miplevels))
	{
		// Allocate memory for the mip tail
		VkMemoryRequirements memReqs = { 0 };
		memReqs.size = sparseMemoryReq.imageMipTailSize;
		memReqs.memoryTypeBits = texture->pSvt->vk.mSparseMemoryTypeBits;
		memReqs.alignment = memReqs.size;

		VmaAllocationCreateInfo allocCreateInfo = { 0 };
		allocCreateInfo.memoryTypeBits = memReqs.memoryTypeBits;
		allocCreateInfo.pool = (VmaPool)texture->pSvt->vk.pPool;

		VmaAllocation allocation;
		VmaAllocationInfo allocationInfo;
		CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &memReqs, &allocCreateInfo, &allocation, &allocationInfo));

		// (Opaque) sparse memory binding
		VkSparseMemoryBind sparseMemoryBind{ 0 };
		sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset;
		sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
		sparseMemoryBind.memory = allocation->GetMemory();
		sparseMemoryBind.memoryOffset = allocation->GetOffset();

		texture->pSvt->vk.pOpaqueMemoryBindAllocations[texture->pSvt->vk.mOpaqueMemoryBindsCount] = allocation;
		texture->pSvt->vk.pOpaqueMemoryBinds[texture->pSvt->vk.mOpaqueMemoryBindsCount] = sparseMemoryBind;
		texture->pSvt->vk.mOpaqueMemoryBindsCount++;
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
	view.subresourceRange.levelCount = desc->miplevels;
	view.image = texture->vk.image;
	texture->mAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	CHECK_VKRESULT(vkCreateImageView(r->vk.device, &view, &alloccbs, &texture->vk.SRVdescriptor));

	TextureBarrier textureBarriers[] = { { pTexture, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST } };
	cmd_resourcebarrier(pCmd, 0, NULL, 1, textureBarriers, 0, NULL);

	// Fill smallest (non-tail) mip map level
	vk_fillVirtualTextureLevel(pCmd, pTexture, TiledMiplevel - 1, 0);

	texture->ownsimage = true;
	texture->nodeidx = desc->nodeidx;
	texture->miplevels = desc->miplevels;
	texture->width = desc->width;
	texture->height = desc->height;
	texture->depth = desc->depth;

#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->pName)
	{
		setTextureName(r, pTexture, desc->pName);
	}
#endif

	*ppTexture = pTexture;
}

void vk_removeVirtualTexture(renderer_t* r, Virtualtexture_t* pSvt)
{
	for (int i = 0; i < (int)pSvt->mVirtualPageTotalCount; i++)
	{
		VirtualTexturePage& page = pSvt->pPages[i];
		if (page.vk.pAllocation)
			vmaFreeMemory(r->vk.vmaAllocator, (VmaAllocation)page.vk.pAllocation);
	}
	tc_free(pSvt->pPages);

	for (int i = 0; i < (int)pSvt->vk.mOpaqueMemoryBindsCount; i++)
	{
		vmaFreeMemory(r->vk.vmaAllocator, (VmaAllocation)pSvt->vk.pOpaqueMemoryBindAllocations[i]);
	}
	tc_free(pSvt->vk.pOpaqueMemoryBinds);
	tc_free(pSvt->vk.pOpaqueMemoryBindAllocations);
	tc_free(pSvt->vk.pSparseImageMemoryBinds);

	for (uint32_t deletionIndex = 0; deletionIndex < pSvt->mPendingDeletionCount; deletionIndex++)
	{
		const VkVTPendingPageDeletion pendingDeletion = vtGetPendingPageDeletion(pSvt, deletionIndex);

		for (uint32_t i = 0; i < *pendingDeletion.pAllocationsCount; i++)
			vmaFreeMemory(r->vk.vmaAllocator, pendingDeletion.pAllocations[i]);
		for (uint32_t i = 0; i < *pendingDeletion.pIntermediateBuffersCount; i++)
			remove_buffer(r, pendingDeletion.pIntermediateBuffers[i]);
	}
	tc_free(pSvt->vk.pPendingDeletedAllocations);
	tc_free(pSvt->pPendingDeletedAllocationsCount);
	tc_free(pSvt->pPendingDeletedBuffers);
	tc_free(pSvt->pPendingDeletedBuffersCount);

	tc_free(pSvt->pVirtualImageData);

	vmaDestroyPool(r->vk.vmaAllocator, (VmaPool)pSvt->vk.pPool);
}

void vk_cmdUpdateVirtualTexture(cmd_t* cmd, texture_t* pTexture, uint32_t currentImage)
{
	TC_ASSERT(texture->pSvt->pReadbackBuffer);

	const bool map = !texture->pSvt->pReadbackBuffer->pCpuMappedAddress;
	if (map)
	{
		mabuffer(cmd->renderer, texture->pSvt->pReadbackBuffer, NULL);
	}

	vk_releasePage(cmd, pTexture, currentImage);
	vk_fillVirtualTexture(cmd, pTexture, NULL, currentImage);

	if (map)
	{
		unmabuffer(cmd->renderer, texture->pSvt->pReadbackBuffer);
	}
}

#endif

void initVulkanRenderer(const char* appName, const RendererDesc* pSettings, Renderer** pr)
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
	mabuffer = vk_mabuffer;
	unmabuffer = vk_unmabuffer;
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

	addrootsignature_t = vk_addrootsignature_t;
	removerootsignature_t = vk_removerootsignature_t;

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

	vk_initRenderer(appName, pSettings, pr);
}

void exitVulkanRenderer(renderer_t* r)
{
	TC_ASSERT(r);

	vk_exit_renderer(r);
}
