/* Imports */
#include "private_types.h"

#include "private/vkgraphics.h"

#include "stb_ds.h"

#include <vk_mem_alloc.h>

#include <tinyimageformat_base.h>
#include <tinyimageformat_query.h>

//#define VK_DEBUG_LOG_EXTENSIONS 1
//#define _ENABLE_DEBUG_UTILS_EXTENSION

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

#ifdef VK_RAYTRACING_AVAILABLE
//TODO: implement
void vk_add_raytracingripeline(const pipelinedesc_t* desc, pipeline_t* pipeline){}
void vk_fill_raytracingdescriptordata(accelstruct_t* desc, VkAccelerationStructureKHR* info){}
#endif

void vk_create_shaderreflection(const uint8_t* code, uint32_t codesize, shaderstage_t stage, shaderreflection_t* out);

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
#define VK_MAX_ROOT_DESCRIPTORS 32

typedef enum {
	GPU_VENDOR_NVIDIA,
	GPU_VENDOR_AMD,
	GPU_VENDOR_INTEL,
	GPU_VENDOR_UNKNOWN,
	GPU_VENDOR_COUNT,
} gpuvendor_t;

static gpuvendor_t _to_internal_gpu_vendor(uint32_t vendorid)
{
	switch(vendorid) {
    case VENDOR_ID_NVIDIA: return GPU_VENDOR_NVIDIA;
    case VENDOR_ID_AMD:
    case VENDOR_ID_AMD_1:
		return GPU_VENDOR_AMD;
	case VENDOR_ID_INTEL:
    case VENDOR_ID_INTEL_1:
    case VENDOR_ID_INTEL_2:
		return GPU_VENDOR_INTEL;
	default: return GPU_VENDOR_UNKNOWN;
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
#if VK_KHR_maintenance3 								// descriptor indexing depends on this
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

VkSampleCountFlagBits _to_vk_sample_count(samplecount_t count);

typedef void (*add_buffer_func)(renderer_t* r, const bufferdesc_t* desc, buffer_t* buf);
typedef void (*remove_buffer_func)(renderer_t* r, buffer_t* buf);
typedef void (*map_buffer_func)(renderer_t* r, buffer_t* buf, range_t* range);
typedef void (*unmap_buffer_func)(renderer_t* r, buffer_t* buf);
typedef void (*cmd_updatebuffer_func)(cmd_t* cmd, buffer_t* buf, uint64_t dstoffset, buffer_t* srcbuf, uint64_t srcoffset, uint64_t size);
typedef void (*cmd_updatesubresource_func)(cmd_t* cmd, texture_t* tex, buffer_t* srcbuf, const struct subresourcedatadesc_s* desc);
typedef void (*cmd_copysubresource_func)(cmd_t* cmd, buffer_t* dstbuf, texture_t* tex, const struct subresourcedatadesc_s* desc);
typedef void (*add_texture_func)(renderer_t* r, const texturedesc_t* desc, texture_t* tex);
typedef void (*remove_texture_func)(renderer_t* r, texture_t* tex);
typedef void (*add_virtualtexture_func)(cmd_t* cmd, const texturedesc_t* desc, texture_t* tex, void* imagedata);
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

static const descinfo_t* get_descriptor(rootsignature_t* signature, const char* name)
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
static inline bool _is_storeop_resolve(storeop_t action) { return STORE_ACTION_RESOLVE_DONTCARE == action || STORE_ACTION_RESOLVE_STORE == action; }
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
} framebufferdesc_t;

typedef struct {
	VkFramebuffer framebuffer;
	uint32_t width;
	uint32_t height;
	uint32_t arraysize;
} framebuffer_t;

// Per Thread Render Pass synchronization logic
typedef struct { uint64_t key; renderpass_t value; } renderpassnode_t;
typedef struct { uint64_t key; framebuffer_t value; } framebuffernode_t;
typedef struct { tc_thread_t key; renderpassnode_t** value; } threadrpnode_t;
typedef struct { tc_thread_t key; framebuffernode_t** value; } threadfbnode_t;

// renderpass and framebuffer map per thread (this will make lookups lock free and we only need a lock when inserting a RenderPass Map for the first time)
static threadrpnode_t* renderpasses[MAX_UNLINKED_GPUS] = { 0 };
static threadfbnode_t* framebuffers[MAX_UNLINKED_GPUS] = { 0 };
static lock_t renderpasslck[MAX_UNLINKED_GPUS] = { 0 };

static renderpassnode_t** get_render_pass_map(uint32_t rid)
{
	spin_lock(&renderpasslck[rid]);
	threadrpnode_t* map = renderpasses[rid];
	tc_thread_t tid = tc_os->current_thread();
	threadrpnode_t* node = hmgetp_null(map, tid);
	if (!node) {	// We need pointer to map, so that thread map can be reallocated without causing data races
		renderpassnode_t** result = (renderpassnode_t**)tc_calloc(1, sizeof(renderpassnode_t*));
		renderpassnode_t** r = hmput(renderpasses[rid], tid, result);
		spin_unlock(&renderpasslck[rid]);
		return r;
	}
	spin_unlock(&renderpasslck[rid]);
	return node->value;
}

static framebuffernode_t** get_frame_buffer_map(uint32_t rid)
{
	spin_lock(&renderpasslck[rid]);
	threadfbnode_t* map = framebuffers[rid];
	tc_thread_t tid = tc_os->current_thread();
	threadfbnode_t* node = hmgetp_null(map, tid);
	if (!node) {
		framebuffernode_t** result = (framebuffernode_t**)tc_calloc(1, sizeof(framebuffernode_t*));
		framebuffernode_t** r = hmput(framebuffers[rid], tid, result);
		spin_unlock(&renderpasslck[rid]);
		return r;
	}
	spin_unlock(&renderpasslck[rid]);
	return node->value;
}

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
	VkSampleCountFlagBits sample_count = _to_vk_sample_count(desc->samplecount);
    for (uint32_t i = 0; i < desc->rtcount; i++, attachment_count++) {
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
        if (_is_storeop_resolve(desc->storeopcolor[i])) {
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
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = desc->rtcount;
	subpass.pColorAttachments = desc->rtcount ? colorattachment_refs : NULL;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	subpass.pResolveAttachments = desc->rtcount ? resolve_attachment_refs : NULL;
#endif
	subpass.pDepthStencilAttachment= has_depthattachment_count ? &dsattachment_ref : NULL;

	VkRenderPassCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = attachment_count;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	CHECK_VKRESULT(vkCreateRenderPass(r->vk.device, &info, &alloccbs, &(renderpass->renderpass)));
}

static void remove_renderpass(renderer_t* r, renderpass_t* renderpass)
{
	vkDestroyRenderPass(r->vk.device, renderpass->renderpass, &alloccbs);
}

static void add_framebuffer(renderer_t* r, const framebufferdesc_t* desc, framebuffer_t* framebuffer)
{
	TC_ASSERT(framebuffer);
	memset(framebuffer, 0, sizeof(framebuffer_t));
	uint32_t color_attachments = desc->rtcount;
	uint32_t depth_attachments = (desc->depthstencil) ? 1 : 0;
	if (color_attachments) {
		framebuffer->width = desc->rendertargets[0]->width;
		framebuffer->height = desc->rendertargets[0]->height;
		if (desc->colorarrayslices)
			framebuffer->arraysize = 1;
		else
			framebuffer->arraysize = desc->rendertargets[0]->vr ? 1 : desc->rendertargets[0]->arraysize;
	}
	else if (depth_attachments) {
		framebuffer->width = desc->depthstencil->width;
		framebuffer->height = desc->depthstencil->height;
		if (desc->deptharrayslice != UINT32_MAX)
			framebuffer->arraysize = 1;
		else
			framebuffer->arraysize = desc->depthstencil->vr ? 1 : desc->depthstencil->arraysize;
	}
	else TC_ASSERT(0 && "No color or depth attachments");

	if (color_attachments && desc->rendertargets[0]->depth > 1)
		framebuffer->arraysize = desc->rendertargets[0]->depth;
	VkImageView imageviews[VK_MAX_ATTACHMENT_ARRAY_COUNT] = { 0 };
	VkImageView* iterviews = imageviews;
	for (uint32_t i = 0; i < desc->rtcount; ++i) {
		if (!desc->colormipslices && !desc->colorarrayslices) {
			*iterviews = desc->rendertargets[i]->vk.descriptor;
			iterviews++;

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (_is_storeop_resolve(desc->rtresolveop[i])) {
				*iterviews = desc->rendertargets[i]->resolveattachment->vk.descriptor;
				iterviews++;
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
			iterviews++;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			if (_is_storeop_resolve(desc->rtresolveop[i])) {
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

static inline VkPipelineColorBlendStateCreateInfo _to_blend_desc(const blendstatedesc_t* desc, VkPipelineColorBlendAttachmentState* attachments)
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
	cb.logicOpEnable = VK_FALSE;
	cb.logicOp = VK_LOGIC_OP_CLEAR;
	cb.pAttachments = attachments;
	return cb;
}

static inline VkPipelineDepthStencilStateCreateInfo _to_depth_desc(const depthstatedesc_t* desc)
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

static inline VkPipelineRasterizationStateCreateInfo _to_rasterizer_desc(const rasterizerstatedesc_t* desc)
{
	TC_ASSERT(desc->fillmode < MAX_FILL_MODES);
	TC_ASSERT(desc->cullmode < MAX_CULL_MODES);
	TC_ASSERT(desc->frontface == FRONT_FACE_CCW || desc->frontface == FRONT_FACE_CW);

	VkPipelineRasterizationStateCreateInfo rs = { 0 };
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.depthClampEnable = desc->depthclampenable ? VK_TRUE : VK_FALSE;
	rs.rasterizerDiscardEnable = VK_FALSE;
	rs.polygonMode = fillmodemap[desc->fillmode];
	rs.cullMode = cullmodemap[desc->cullmode];
	rs.frontFace = frontfacemap[desc->frontface];
	rs.depthBiasEnable = (desc->depthbias != 0) ? VK_TRUE : VK_FALSE;
	rs.depthBiasConstantFactor = (float)desc->depthbias;
	rs.depthBiasSlopeFactor = desc->slopescaleddepthbias;
	rs.lineWidth = 1;
	return rs;
}

static VkPipelineRasterizationStateCreateInfo defaultrasterizerdesc = { 0 };
static VkPipelineDepthStencilStateCreateInfo defaultdepthdesc = { 0 };
static VkPipelineColorBlendStateCreateInfo defaultblenddesc = { 0 };
static VkPipelineColorBlendAttachmentState defaultblendattachments[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };

typedef struct nulldescriptors_s {
	texture_t defaulttexSRV[MAX_LINKED_GPUS][TEXTURE_DIM_COUNT];
	texture_t defaulttexUAV[MAX_LINKED_GPUS][TEXTURE_DIM_COUNT];
	buffer_t defaultbufSRV[MAX_LINKED_GPUS];
	buffer_t defaultbufUAV[MAX_LINKED_GPUS];
	sampler_t defaultsampler;
	lock_t submitlck;
	lock_t initlck;
	queue_t initqueue;
	cmdpool_t initcmdpool;
	cmd_t initcmd;
	fence_t initfence;
} nulldescriptors_t;

static void _initial_transition(renderer_t* r, texture_t* tex, resourcestate_t state)
{
	TC_LOCK(&r->nulldescriptors->initlck);
	{
		cmd_t* cmd = &r->nulldescriptors->initcmd;
		reset_cmdpool(r, &r->nulldescriptors->initcmdpool);
		cmd_begin(cmd);
		cmd_resourcebarrier(cmd, 0, NULL, 1, &(texbarrier_t){ tex, RESOURCE_STATE_UNDEFINED, state }, 0, NULL);
		cmd_end(cmd);
		
		queuesubmitdesc_t desc = { 0 };
		desc.cmdcount = 1;
		desc.cmds = &cmd;
		desc.signalfence = &r->nulldescriptors->initfence;
		queue_submit(&r->nulldescriptors->initqueue, &desc);
		wait_for_fences(r, 1, (fence_t*[]){ &r->nulldescriptors->initfence});
	}
	TC_UNLOCK(&r->nulldescriptors->initlck);
}

static _dim_has_uav_tex(uint32_t dim) { return dim != TEXTURE_2DMS && dim != TEXTURE_2DMS_ARRAY && dim != TEXTURE_CUBE && dim != TEXTURE_CUBE_ARRAY;}

static void add_default_resources(renderer_t* r)
{
	spin_lock_init(&r->nulldescriptors->submitlck);
	spin_lock_init(&r->nulldescriptors->initlck);
	for (uint32_t i = 0; i < r->linkednodecount; i++) {
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
	defaultblenddesc = _to_blend_desc(&bsdesc, defaultblendattachments);

	depthstatedesc_t dsdesc = { 0 };
	dsdesc.depthfunc = CMP_LEQ;
	dsdesc.depthtest = false;
	dsdesc.depthwrite = false;
	dsdesc.stencilbackfunc = CMP_ALWAYS;
	dsdesc.stencilfrontfunc = CMP_ALWAYS;
	dsdesc.stencilreadmask = 0xFF;
	dsdesc.stencilwritemask = 0xFF;
	defaultdepthdesc = _to_depth_desc(&dsdesc);

	rasterizerstatedesc_t rsdesc = { 0 };
	rsdesc.cullmode = CULL_MODE_BACK;
	defaultrasterizerdesc = _to_rasterizer_desc(&rsdesc);

	// Create command buffer to transition resources to the correct state
	queue_t gfxqueue = { 0 };
	queuedesc_t queuedesc = { 0 };
	queuedesc.type = QUEUE_TYPE_GRAPHICS;
	add_queue(r, &queuedesc, &gfxqueue);
	r->nulldescriptors->initqueue = gfxqueue;

	cmdpool_t pool = { 0 };
	cmdpooldesc_t pooldesc = { 0 };
	pooldesc.queue = &r->nulldescriptors->initqueue;
	pooldesc.transient = true;
	add_cmdpool(r, &pooldesc, &pool);
	r->nulldescriptors->initcmdpool = pool;

	cmd_t cmd = { 0 };
	cmddesc_t cmddesc = { 0 };
	cmddesc.pool = &r->nulldescriptors->initcmdpool;
	add_cmds(r, &cmddesc, 1, &cmd);
	r->nulldescriptors->initcmd = cmd;

	fence_t fence = { 0 };
	add_fence(r, &fence);
	r->nulldescriptors->initfence = fence;
	// Transition resources
	for (uint32_t i = 0; i < r->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			_initial_transition(r, &r->nulldescriptors->defaulttexSRV[i][dim], RESOURCE_STATE_SHADER_RESOURCE);
			if (_dim_has_uav_tex(dim))
				_initial_transition(r, &r->nulldescriptors->defaulttexUAV[i][dim], RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}
}

static void remove_default_resources(renderer_t* r)
{
	for (uint32_t i = 0; i < r->linkednodecount; ++i) {
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim) {
			remove_texture(r, &r->nulldescriptors->defaulttexSRV[i][dim]);
			if (_dim_has_uav_tex(dim))
				remove_texture(r, &r->nulldescriptors->defaulttexUAV[i][dim]);
		}
		remove_buffer(r, &r->nulldescriptors->defaultbufSRV[i]);
		remove_buffer(r, &r->nulldescriptors->defaultbufUAV[i]);
	}
	remove_sampler(r, &r->nulldescriptors->defaultsampler);
	remove_fence(r, &r->nulldescriptors->initfence);
	remove_cmds(r, 1, &r->nulldescriptors->initcmd);
	remove_cmdpool(r, &r->nulldescriptors->initcmdpool);
	remove_queue(r, &r->nulldescriptors->initqueue);
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

VkShaderStageFlags _to_vk_stages(shaderstage_t stages)
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

VkSampleCountFlagBits _to_vk_sample_count(samplecount_t count)
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

VkImageUsageFlags _to_vk_image_usage(descriptortype_t usage)
{
	VkImageUsageFlags result = 0;
	if (DESCRIPTOR_TYPE_TEXTURE == (usage & DESCRIPTOR_TYPE_TEXTURE)) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (DESCRIPTOR_TYPE_RW_TEXTURE == (usage & DESCRIPTOR_TYPE_RW_TEXTURE)) result |= VK_IMAGE_USAGE_STORAGE_BIT;
	return result;
}

VkAccessFlags _to_vk_access_flags(resourcestate_t state)
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

VkImageLayout _to_vk_image_layout(resourcestate_t usage)
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

void _get_planar_vk_image_memory_requirement(
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
		outmemreq->size += round_up(req2.memoryRequirements.size, (uint32_t)req2.memoryRequirements.alignment);
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
VkPipelineStageFlags _determine_pipeline_stage_flags(renderer_t* r, VkAccessFlags accessflags, queuetype_t queuetype)
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

VkImageAspectFlags _vk_determine_aspect_mask(VkFormat format, bool includeStencilBit)
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

VkFormatFeatureFlags _vk_image_usage_to_format_features(VkImageUsageFlags usage)
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

VkQueueFlags _to_vk_queue_flags(queuetype_t type)
{
	switch (type) {
		case QUEUE_TYPE_GRAPHICS: return VK_QUEUE_GRAPHICS_BIT;
		case QUEUE_TYPE_TRANSFER: return VK_QUEUE_TRANSFER_BIT;
		case QUEUE_TYPE_COMPUTE: return VK_QUEUE_COMPUTE_BIT;
		default: TC_ASSERT(false && "Invalid Queue Type"); return VK_QUEUE_FLAG_BITS_MAX_ENUM;
	}
}

VkDescriptorType _to_vk_descriptor_type(descriptortype_t type)
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

VkShaderStageFlags _to_vk_shader_stage_flags(shaderstage_t stages)
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

void _find_queue_family_index(
	const renderer_t* r, uint32_t nodeidx, queuetype_t queuetype, VkQueueFamilyProperties* outprops, uint8_t* outfamidx,
	uint8_t* outqueueidx)
{
	if (r->gpumode != GPU_MODE_LINKED) nodeidx = 0;

	uint32_t queuefamidx = UINT32_MAX;
	uint32_t queueidx = UINT32_MAX;
	VkQueueFlags requiredFlags = _to_vk_queue_flags(queuetype);
	bool found = false;
	uint32_t queuefampropscount = 0;				// Get queue family properties
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

static VkPipelineCacheCreateFlags _to_pipeline_cache_flags(pipelinecacheflags_t flags)
{
	VkPipelineCacheCreateFlags ret = 0;
#if VK_EXT_pipeline_creation_cache_control
	if (flags & PIPELINE_CACHE_FLAG_EXTERNALLY_SYNCHRONIZED)
		ret |= VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT;
#endif
	return ret;
}

void _get_device_indices(renderer_t* r, uint32_t nodeidx, uint32_t* sharednodeindices, uint32_t sharednodeidxcount, uint32_t* idxs)
{
	for (uint32_t i = 0; i < r->linkednodecount; ++i) idxs[i] = i;
	idxs[nodeidx] = nodeidx;
	for (uint32_t i = 0; i < sharednodeidxcount; ++i) idxs[sharednodeindices[i]] = nodeidx;
}

void _query_gpu_settings(VkPhysicalDevice gpu, VkPhysicalDeviceProperties2* gpuprops, VkPhysicalDeviceMemoryProperties* gpumemprops,
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
	vkGetPhysicalDeviceFeatures2KHR(gpu, gpufeats);
	VkPhysicalDeviceSubgroupProperties p = { 0 };
	p.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
	gpuprops->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
	p.pNext = gpuprops->pNext;
	gpuprops->pNext = &p;
	vkGetPhysicalDeviceProperties2KHR(gpu, gpuprops);
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
	switch (_to_internal_gpu_vendor(gpuprops->properties.vendorID)) {
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
		//TC_ASSERT(VK_MAX_DESCRIPTION_SIZE == TC_COUNT(outversionstr));
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
	for (uint32_t i = 0; i < vklayercount; i++)
		TRACE(LOG_INFO, "%s ( %s )", vklayers[i].layerName, "vkinstance-layer");
	for (uint32_t i = 0; i < vkextcount; i++)
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
	for (uint32_t i = 0; i < initialcount; i++)
		wantedext[i] = wantedinstanceextensions[i];
	for (uint32_t i = 0; i < requestedcount; i++)
		wantedext[initialcount + i] = desc->vk.instanceextensions[i];
	const uint32_t wantedext_count = (uint32_t)arrlen(wantedext);
	for (ptrdiff_t i = 0; i < arrlen(temp); i++) {
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
		for (uint32_t j = 0; j < count; j++) {
			for (uint32_t k = 0; k < wantedext_count; k++) {
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

	CHECK_VKRESULT(vkCreateInstance(&instance_info, &alloccbs, &r->vk.instance));
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
		info.pfnCallback = internal_debug_report_callback;
		info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
#if defined(__ANDROID__)
			VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
#endif
			VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
		VkResult res = vkCreateDebugReportCallbackEXT(r->vk.instance, &info, &alloccbs, &(r->vk.debugreport));
		if (VK_SUCCESS != res)
			TRACE(LOG_ERROR, "%s ( %s )", "vkCreateDebugReportCallbackEXT failed - disabling Vulkan debug callbacks", "internal_vk_init_instance");
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
		_query_gpu_settings(gpus[i], &gpuprops[i], &gpumemprops[i], &gpufeats[i], &queuefamprops[i], &queuefampropscount[i], &gpusettings[i]);
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
	TC_ASSERT(r->vk.instance != VK_NULL_HANDLE);
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
	TRACE(LOG_INFO, "Creating device");

	if (r->linkednodecount < 2 && r->gpumode == GPU_MODE_LINKED)
		r->gpumode = GPU_MODE_SINGLE;
	if (desc->context == NULL) {
		if (!_select_best_gpu(r)){
			return false;
		}
	} else {
		TC_ASSERT(desc->gpuindex < desc->context->gpucount);
		r->vk.activegpu = desc->context->gpus[desc->gpuindex].vk.gpu;
		r->vk.activegpuprops = (VkPhysicalDeviceProperties2*)tc_calloc(1, sizeof(VkPhysicalDeviceProperties2));
		r->activegpusettings = (gpusettings_t*)tc_calloc(1, sizeof(gpusettings_t));
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
		tc_free(properties);
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

	const uint32_t maxQueueFlag = 383; // All VkQueueFlagBits
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
			queue_infos[queue_infos_count].queueFamilyIndex = i;
			queue_infos[queue_infos_count].queueCount = num_queues;
			queue_infos[queue_infos_count].pQueuePriorities = priorities[i];
			queue_infos_count++;
			for (uint32_t j = 0; j < r->linkednodecount; j++) {
				r->vk.availablequeues[j][queuefamiliesprops[i].queueFlags] = num_queues;
			}
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

void vk_init_renderer(const char* app_name, const rendererdesc_t* desc, renderer_t* r)
{
	TC_ASSERT(app_name && desc && r);
	memset(r, 0, sizeof(renderer_t));
	r->gpumode = desc->gpumode;
	r->shadertarget = desc->shadertarget;
	r->enablegpubasedvalidation = desc->enablegpubasedvalidation;
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
		r->vk.pVkDebugReport = desc->context->vk.debugreport;
#endif
		r->unlinkedrendererindex = r_count;
	}
	else if (!init_common(app_name, desc, r)) {
		tc_free(r->name);
		return;
	}
	if (!add_device(desc, r)) {
		if (r->vk.owninstance) exit_common(r);
		tc_free(r->name);
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
	funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR;
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
	VkDescriptorPoolSize sizes[1] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1 } };
	add_descriptor_pool(r, 1, 0, sizes, 1, &r->vk.emptydescriptorpool);
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

	_find_queue_family_index(r, 0, QUEUE_TYPE_GRAPHICS, NULL, &r->vk.graphicsqueuefamilyidx, NULL);
	_find_queue_family_index(r, 0, QUEUE_TYPE_COMPUTE, NULL, &r->vk.computequeuefamilyidx, NULL);
	_find_queue_family_index(r, 0, QUEUE_TYPE_TRANSFER, NULL, &r->vk.transferqueuefamilyidx, NULL);
	
	r->nulldescriptors = (nulldescriptors_t*)tc_calloc(1, sizeof(nulldescriptors_t));
	TC_ASSERT(r->nulldescriptors);

	add_default_resources(r);
	r_count++;
	TC_ASSERT(r_count <= MAX_UNLINKED_GPUS);
}

void vk_exit_renderer(renderer_t* r)
{
	TC_ASSERT(r);
	r_count--;
	remove_default_resources(r);

	// Remove the renderpasses
	for (ptrdiff_t i = 0; i < hmlen(renderpasses[r->unlinkedrendererindex]); i++) {
		renderpassnode_t** pmap = renderpasses[r->unlinkedrendererindex][i].value;
		renderpassnode_t* map = *pmap;
		for (ptrdiff_t j = 0; j < hmlen(map); ++j)
			remove_renderpass(r, &map[j].value);
		hmfree(map);
		tc_free(pmap);
	}
	hmfree(renderpasses[r->unlinkedrendererindex]);
	for (ptrdiff_t i = 0; i < hmlen(framebuffers[r->unlinkedrendererindex]); i++) {
		framebuffernode_t** pmap = framebuffers[r->unlinkedrendererindex][i].value;
		framebuffernode_t* map = *pmap;
		for (ptrdiff_t j = 0; j < hmlen(map); ++j)
			remove_framebuffer(r, &map[j].value);
		hmfree(map);
		tc_free(pmap);
	}
	hmfree(framebuffers[r->unlinkedrendererindex]);

	vmaDestroyAllocator(r->vk.vmaAllocator);
	remove_device(r);
	if (r->vk.owninstance) exit_common(r);

	for (uint32_t i = 0; i < r->linkednodecount; i++) {
		tc_free(r->vk.availablequeues[i]);
		tc_free(r->vk.usedqueues[i]);
	}
	tc_free(r->vk.availablequeues);
	tc_free(r->vk.usedqueues);
	tc_free(r->capbits);
	tc_free(r->name);
	tc_free(r->nulldescriptors);
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
	_find_queue_family_index(r, nodeidx, desc->type, &props, &famidx, &queueidx);
	r->vk.usedqueues[nodeidx][props.queueFlags]++;
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
	r->vk.usedqueues[nodeidx][queue->vk.flags]--;
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
	if (queuefampropscount) {									// Check if hardware provides dedicated present queue
		for (uint32_t i = 0; i < queuefampropscount; i++) {
			VkBool32 supports_present = VK_FALSE;
			VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(r->vk.activegpu, i, surface, &supports_present);
			if ((VK_SUCCESS == res) && (VK_TRUE == supports_present) && desc->presentqueues[0]->vk.queuefamilyindex != i) {
				famidx = i;
				break;
			}
		}
		if (famidx == UINT32_MAX) {								// If there is no dedicated present queue, just find the first available queue which supports present
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
	if (desc->descriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER)		// Align the buffer size to multiples of the dynamic uniform buffer minimum size
		size = round_up(size, r->activegpusettings->uniformbufalignment);
	
	VkBufferCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = util_to_vk_buffer_usage(desc->descriptors, desc->format != TinyImageFormat_UNDEFINED);
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (desc->memusage == RESOURCE_MEMORY_USAGE_GPU_ONLY || desc->memusage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU)
		info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;			// Buffer can be used as dest in a transfer command (Uploading data to a storage buffer, Readback query data)

	const bool linkedmultigpu = (r->gpumode == GPU_MODE_LINKED && (desc->sharednodeindices || desc->nodeidx));
	VmaAllocationCreateInfo vma_mem_reqs = { 0 };
	vma_mem_reqs.usage = (VmaMemoryUsage)desc->memusage;
	if (desc->flags & BUFFER_CREATION_FLAG_OWN_MEMORY_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	if (desc->flags & BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	if (linkedmultigpu)
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
	if (linkedmultigpu)	{										// Buffer to be used on multiple GPUs
		VmaAllocationInfo info3 = { 0 };
		vmaGetAllocationInfo(r->vk.vmaAllocator, buffer->vk.alloc, &info3);
		// Set all the device indices to the index of the device where we will create the buffer
		uint32_t* idxs = (uint32_t*)alloca(r->linkednodecount * sizeof(uint32_t));
		_get_device_indices(r, desc->nodeidx, desc->sharednodeindices, desc->sharednodeidxcount, idxs);
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
	if (desc->name) set_buffername(r, buffer, desc->name);
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
	memset(texture, 0, sizeof(texture_t));
	if (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
		texture->vk.UAVdescriptors = (VkImageView*)tc_calloc(desc->miplevels, sizeof(VkImageView));
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
	const uint32_t planes = TinyImageFormat_NumOfPlanes(desc->format);
	const bool single_plane = TinyImageFormat_IsSinglePlane(desc->format);
	TC_ASSERT(
		((single_plane && planes == 1) || (!single_plane && planes > 1 && planes <= MAX_PLANE_COUNT)) &&
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
		info.samples = _to_vk_sample_count(desc->samplecount);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = _to_vk_image_usage(descriptors);
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

		VkFormatFeatureFlags format_features = _vk_image_usage_to_format_features(info.usage);	// Verify that GPU supports this format
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
			formatList.pViewFormats = &planar;
			formatList.viewFormatCount = 1;
			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			info.pNext = &formatList;    //-V506

			// Create Image
			CHECK_VKRESULT(vkCreateImage(r->vk.device, &info, &alloccbs, &texture->vk.image));

			VkMemoryRequirements vkmemreq = { 0 };
			uint64_t planesoffsets[MAX_PLANE_COUNT] = { 0 };
			_get_planar_vk_image_memory_requirement(r->vk.device, texture->vk.image, planes, &vkmemreq, planesoffsets);

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
			for (uint32_t i = 0; i < planes; i++) {
				VkBindImagePlaneMemoryInfo* bind_info = &bindImagePlanesMemoryInfo[i];
				bind_info->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
				bind_info->pNext = NULL;
				bind_info->planeAspect = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);

				VkBindImageMemoryInfo* bind_info2 = &bindImagesMemoryInfo[i];
				bind_info2->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
				bind_info2->pNext = &bind_info;
				bind_info2->image = texture->vk.image;
				bind_info2->memory = texture->vk.devicemem;
				bind_info2->memoryOffset = planesoffsets[i];
			}
			CHECK_VKRESULT(vkBindImageMemory2(r->vk.device, planes, bindImagesMemoryInfo));
		}
		// Texture to be used on multiple GPUs
		if (linked) {
			VmaAllocationInfo info3 = { 0 };
			vmaGetAllocationInfo(r->vk.vmaAllocator, texture->vk.alloc, &info3);
			// Set all the device indices to the index of the device where we will create the texture
			uint32_t* pIndices = (uint32_t*)alloca(r->linkednodecount * sizeof(uint32_t));
			_get_device_indices(r, desc->nodeidx, desc->sharednodeindices, desc->sharednodeidxcount, pIndices);
			VkBindImageMemoryInfoKHR bind_info = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR };
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
	srv_desc.image = texture->vk.image;
	srv_desc.viewType = view_type;
	srv_desc.format = (VkFormat)TinyImageFormat_ToVkFormat(desc->format);
	srv_desc.components.r = VK_COMPONENT_SWIZZLE_R;
	srv_desc.components.g = VK_COMPONENT_SWIZZLE_G;
	srv_desc.components.b = VK_COMPONENT_SWIZZLE_B;
	srv_desc.components.a = VK_COMPONENT_SWIZZLE_A;
	srv_desc.subresourceRange.aspectMask = _vk_determine_aspect_mask(srv_desc.format, false);
	srv_desc.subresourceRange.baseMipLevel = 0;
	srv_desc.subresourceRange.levelCount = desc->miplevels;
	srv_desc.subresourceRange.baseArrayLayer = 0;
	srv_desc.subresourceRange.layerCount = size;
	texture->aspectmask = _vk_determine_aspect_mask(srv_desc.format, true);
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
		for (uint32_t i = 0; i < desc->miplevels; i++) {
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
	if (texture->vk.UAVdescriptors) tc_free(texture->vk.UAVdescriptors);
}

void vk_add_rendertarget(renderer_t* r, const rendertargetdesc_t* desc, rendertarget_t* rendertarget)
{
	TC_ASSERT(r && desc && rendertarget);
	TC_ASSERT(r->gpumode != GPU_MODE_UNLINKED || desc->nodeidx == r->unlinkedrendererindex);
	bool const isdepth = TinyImageFormat_IsDepthOnly(desc->format) || TinyImageFormat_IsDepthAndStencil(desc->format);
	TC_ASSERT(!((isdepth) && (desc->descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)) && "Cannot use depth stencil as UAV");
	((rendertargetdesc_t*)desc)->miplevels = max(1U, desc->miplevels);
	memset(rendertarget, 0, sizeof(rendertarget_t));

    uint32_t size = desc->arraysize;
	uint32_t depthorarraysize = size * desc->depth;
	uint32_t numrtvs = desc->miplevels;
	if ((desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(desc->descriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		numrtvs *= depthorarraysize;
	rendertarget->vk.slicedescriptors = (VkImageView*)tc_calloc(numrtvs, sizeof(VkImageView));
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
			VkResult res = vkGetPhysicalDeviceImageFormatProperties(
				r->vk.activegpu, vk_depth_stencil_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &props);
			
			if (VK_SUCCESS != res) {					// Fall back to something that's guaranteed to work
				tex_desc.format = TinyImageFormat_D16_UNORM;
				TRACE(LOG_WARNING, "Depth stencil format (%u) not supported. Falling back to D16 format", desc->format);
			}
		}
	}
	tex_desc.name = desc->name;
	add_texture(r, &tex_desc, &rendertarget->tex);

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	if (desc->height > 1)
		viewType = depthorarraysize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	else
		viewType = depthorarraysize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;

	VkImageViewCreateInfo rtvDesc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL };
	rtvDesc.flags = 0;
	rtvDesc.image = rendertarget->tex.vk.image;
	rtvDesc.viewType = viewType;
	rtvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(tex_desc.format);
	rtvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	rtvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	rtvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	rtvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	rtvDesc.subresourceRange.aspectMask = _vk_determine_aspect_mask(rtvDesc.format, true);
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
	_initial_transition(r, &rendertarget->tex, desc->state);

#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	if (desc->flags & TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT) {
		RenderTargetDesc resolveRTDesc = *desc;
		resolveRTDesc.mFlags &= ~(TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT | TEXTURE_CREATION_FLAG_ON_TILE);
		resolveRTDesc.mSampleCount = SAMPLE_COUNT_1;
		addRenderTarget(r, &resolveRTDesc, &rendertarget->resolveAttachment);
	}
#endif
}

void vk_remove_rendertarget(renderer_t* r, rendertarget_t* rendertarget)
{
	remove_texture(r, &rendertarget->tex);
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
	if (rendertarget->resolveAttachment)
		remove_rendertarget(r, rendertarget->resolveattachment);
#endif
	tc_free(rendertarget->vk.slicedescriptors);
}

void vk_add_sampler(renderer_t* r, const samplerdesc_t* desc, sampler_t* sampler)
{
	TC_ASSERT(r && desc && sampler);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(desc->compareop < MAX_COMPARE_MODES);
	memset(sampler, 0, sizeof(sampler_t));
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

void vk_map_buffer(renderer_t* r, buffer_t* buffer, range_t* range)
{
	TC_ASSERT(buffer->memusage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to map non-cpu accessible resource");
	CHECK_VKRESULT(vmaMapMemory(r->vk.vmaAllocator, buffer->vk.alloc, &buffer->data));
	if (range) buffer->data = ((uint8_t*)buffer->data + range->offset);
}

void vk_unmap_buffer(renderer_t* r, buffer_t* buffer)
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
		for (uint32_t idx = 0; idx < rootsignature->descriptorcount; idx++) {
			const descinfo_t* info = &rootsignature->descriptors[idx];
			if (info->updatefreq != freq || info->rootdescriptor || info->staticsampler)
				continue;

			descriptortype_t type = (descriptortype_t)info->type;
			VkWriteDescriptorSet writeset = { 0 };
			writeset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeset.descriptorCount = 1;
			writeset.descriptorType = (VkDescriptorType)info->vk.type;
			writeset.dstBinding = info->vk.reg;
			for (uint32_t i = 0; i < desc->maxsets; i++) {
				writeset.dstSet = descset->vk.handles[i];
				switch (type) {
				case DESCRIPTOR_TYPE_SAMPLER: {
					VkDescriptorImageInfo update_data = { r->nulldescriptors->defaultsampler.vk.sampler, VK_NULL_HANDLE };
					writeset.pImageInfo = &update_data;
					for (uint32_t j = 0; j < info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pImageInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXTURE:
				case DESCRIPTOR_TYPE_RW_TEXTURE: {
					VkImageView srcView = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ?
						r->nulldescriptors->defaulttexUAV[nodeidx][info->dim].vk.UAVdescriptors[0] :
						r->nulldescriptors->defaulttexSRV[nodeidx][info->dim].vk.SRVdescriptor;
					VkImageLayout layout = (type == DESCRIPTOR_TYPE_RW_TEXTURE) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					VkDescriptorImageInfo update_data = { VK_NULL_HANDLE, srcView, layout };
					writeset.pImageInfo = &update_data;
					for (uint32_t j = 0; j < info->size; j++) {
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
					VkDescriptorBufferInfo update_data = { r->nulldescriptors->defaultbufSRV[nodeidx].vk.buffer, 0, VK_WHOLE_SIZE };
					writeset.pBufferInfo = &update_data;
					for (uint32_t j = 0; j < info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pBufferInfo = NULL;
					break;
				}
				case DESCRIPTOR_TYPE_TEXEL_BUFFER:
				case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER: {
					VkBufferView update_data = (type == DESCRIPTOR_TYPE_RW_TEXEL_BUFFER) ?
						r->nulldescriptors->defaultbufUAV[nodeidx].vk.storagetexelview :
						r->nulldescriptors->defaultbufSRV[nodeidx].vk.uniformtexelview;
					writeset.pTexelBufferView = &update_data;
					for (uint32_t j = 0; j < info->size; j++) {
						writeset.dstArrayElement = j;
						vkUpdateDescriptorSets(r->vk.device, 1, &writeset, 0, NULL);
					}
					writeset.pTexelBufferView = NULL;
					break;
				}
				default: break;
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

void vk_remove_descriptorset(renderer_t* r, descset_t* descset)
{
	TC_ASSERT(r && descset);
	vkDestroyDescriptorPool(r->vk.device, descset->vk.descriptorpool, &alloccbs);
	tc_free(descset->vk.handles);
}

#if defined(ENABLE_GRAPHICS_DEBUG)
#define VALIDATE_DESCRIPTOR(descriptor, ...)                       \
	if (!(descriptor)) {                                           \
		char messageBuf[256];                                      \
		sprintf(messageBuf, __VA_ARGS__);                          \
		TRACE(LOG_ERROR, "%s", messageBuf);                  	   \
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
	VkWriteDescriptorSet writesetarr[MAX_WRITE_SETS] = { 0 };
	uint8_t descriptorUpdateDataStart[MAX_DESCRIPTOR_INFO_BYTES] = { 0 };
	const uint8_t* descriptorUpdateDataEnd = &descriptorUpdateDataStart[MAX_DESCRIPTOR_INFO_BYTES - 1];
	uint32_t num_writesets = 0;
	uint8_t* descriptorUpdateData = descriptorUpdateDataStart;
#define FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(type, pInfo, count)                                         \
	if (descriptorUpdateData + sizeof(type) >= descriptorUpdateDataEnd) {                             \
		writeset->descriptorCount = i - lastArrayIndexStart;                                          \
		vkUpdateDescriptorSets(r->vk.device, num_writesets, writesetarr, 0, NULL);                    	  \
		num_writesets = 1;                                                                            \
		writesetarr[0] = *writeset;                                                                   \
		writeset = &writesetarr[0];                                                                   \
		lastArrayIndexStart = i;                                                                      \
		writeset->dstArrayElement += writeset->descriptorCount;                                       \
		writeset->descriptorCount = count - writeset->dstArrayElement;                                \
		descriptorUpdateData = descriptorUpdateDataStart;                                             \
		writeset->pInfo = (type*)descriptorUpdateData;                                                \
	}                                                                                                 \
	type* currUpdateData = (type*)descriptorUpdateData;                                               \
	descriptorUpdateData += sizeof(type)

	for (uint32_t i = 0; i < count; i++) {
		const descdata_t* param = params + i;
		uint32_t paramidx = param->bindbyidx ? param->index : UINT32_MAX;
		VALIDATE_DESCRIPTOR(param->name || (paramidx != UINT32_MAX), "DescriptorData has NULL name and invalid index");
		const descinfo_t* desc = (paramidx != UINT32_MAX) ? (rootsignature->descriptors + paramidx) : get_descriptor((rootsignature_t*)rootsignature, param->name);
		if (paramidx != UINT32_MAX) {
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param index (%u)", paramidx);
		}
		else {
			VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param name (%s)", param->name);
		}
		const descriptortype_t type = (descriptortype_t)desc->type;
		const uint32_t arrayStart = param->arrayoffset;
		const uint32_t array_count = max(1U, param->count);
		if (num_writesets >= MAX_WRITE_SETS) {	// Flush the update if we go above the max write set limit
			vkUpdateDescriptorSets(r->vk.device, num_writesets, writesetarr, 0, NULL);
			num_writesets = 0;
			descriptorUpdateData = descriptorUpdateDataStart;
		}

		VkWriteDescriptorSet* writeset = &writesetarr[num_writesets++];
		writeset->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeset->pNext = NULL;
		writeset->descriptorCount = array_count;
		writeset->descriptorType = (VkDescriptorType)desc->vk.type;
		writeset->dstArrayElement = arrayStart;
		writeset->dstBinding = desc->vk.reg;
		writeset->dstSet = descset->vk.handles[index];
		VALIDATE_DESCRIPTOR(desc->updatefreq == descset->vk.updatefreq, "Descriptor (%s) - Mismatching update frequency and set index", desc->name);

		uint32_t lastArrayIndexStart = 0;
		switch (type) {
			case DESCRIPTOR_TYPE_SAMPLER:
				VALIDATE_DESCRIPTOR(	// Index is invalid when descriptor is a static sampler
					!desc->staticsampler,
					"Trying to update a static sampler (%s). All static samplers must be set in addrootsignature_t and cannot be updated "
					"later", desc->name);
				VALIDATE_DESCRIPTOR(param->ppSamplers, "NULL Sampler (%s)", desc->name);
				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;
				for (uint32_t i = 0; i < array_count; i++) {
					VALIDATE_DESCRIPTOR(param->samplers[i], "NULL Sampler (%s [%u] )", desc->name, i);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, array_count);
					*currUpdateData = (VkDescriptorImageInfo){ param->samplers[i]->vk.sampler, VK_NULL_HANDLE };
				}
				break;
			case DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				VALIDATE_DESCRIPTOR(param->textures, "NULL Texture (%s)", desc->name);
#if defined(ENABLE_GRAPHICS_DEBUG)
				descidxmap_t* node = shgetp_null(rootsignature->descnametoidxmap, desc->name);
				if (!node) {
					TRACE(LOG_ERROR, "No Static Sampler called (%s)", desc->name);
					TC_ASSERT(false);
				}
#endif
				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;
				for (uint32_t i = 0; i < array_count; i++) {
					VALIDATE_DESCRIPTOR(param->ppTextures[i], "NULL Texture (%s [%u] )", desc->name, i);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, array_count);
					*currUpdateData = (VkDescriptorImageInfo){ VK_NULL_HANDLE, param->textures[i]->vk.SRVdescriptor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
				}
				break;
			case DESCRIPTOR_TYPE_TEXTURE:
				VALIDATE_DESCRIPTOR(param->textures, "NULL Texture (%s)", desc->name);
				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;
				if (!param->bindstencilresource) {
					for (uint32_t i = 0; i < array_count; i++) {
						VALIDATE_DESCRIPTOR(param->ppTextures[i], "NULL Texture (%s [%u] )", desc->name, i);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, array_count);
						*currUpdateData = (VkDescriptorImageInfo){ VK_NULL_HANDLE, param->textures[i]->vk.SRVdescriptor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
					}
				}
				else {
					for (uint32_t i = 0; i < array_count; i++) {
						VALIDATE_DESCRIPTOR(param->textures[i], "NULL Texture (%s [%u] )", desc->name, i);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, array_count);
						*currUpdateData = (VkDescriptorImageInfo){ VK_NULL_HANDLE, param->textures[i]->vk.SRVstencildescriptor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
					}
				}
				break;
			case DESCRIPTOR_TYPE_RW_TEXTURE:
				VALIDATE_DESCRIPTOR(param->textures, "NULL RW Texture (%s)", desc->name);
				writeset->pImageInfo = (VkDescriptorImageInfo*)descriptorUpdateData;
				if (param->bindmipchain) {
					VALIDATE_DESCRIPTOR(param->textures[0], "NULL RW Texture (%s)", desc->name);
					VALIDATE_DESCRIPTOR((!arrayStart), "Descriptor (%s) - bindmipchain supports only updating the whole mip-chain. No partial updates supported", param->pName);
					const uint32_t mipCount = param->textures[0]->miplevels;
					writeset->descriptorCount = mipCount;
					for (uint32_t i = 0; i < mipCount; i++) {
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, mipCount);
						*currUpdateData = (VkDescriptorImageInfo){ VK_NULL_HANDLE, param->textures[0]->vk.UAVdescriptors[i], VK_IMAGE_LAYOUT_GENERAL };
					}
				}
				else {
					const uint32_t slice = param->UAVmipslice;
					for (uint32_t i = 0; i < array_count; i++) {
						VALIDATE_DESCRIPTOR(param->textures[i], "NULL RW Texture (%s [%u] )", desc->name, i);
						VALIDATE_DESCRIPTOR(
							slice < param->ppTextures[i]->miplevels,
							"Descriptor : (%s [%u] ) Mip Slice (%u) exceeds mip levels (%u)", desc->name, i, slice,
							param->ppTextures[i]->miplevels);
						FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorImageInfo, pImageInfo, array_count);
						*currUpdateData = (VkDescriptorImageInfo) { VK_NULL_HANDLE, param->textures[i]->vk.UAVdescriptors[slice], VK_IMAGE_LAYOUT_GENERAL };
					}
				}
				break;
			case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				if (desc->rootdescriptor) {
					VALIDATE_DESCRIPTOR(
						false,
						"Descriptor (%s) - Trying to update a root cbv through updateDescriptorSet. All root cbvs must be updated through cmdBindDescriptorSetWithRootCbvs",
						desc->name);

					break;
				}
			case DESCRIPTOR_TYPE_BUFFER:
			case DESCRIPTOR_TYPE_BUFFER_RAW:
			case DESCRIPTOR_TYPE_RW_BUFFER:
			case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
				VALIDATE_DESCRIPTOR(param->pbuffers, "NULL Buffer (%s)", desc->name);
				writeset->pBufferInfo = (VkDescriptorBufferInfo*)descriptorUpdateData;
				for (uint32_t i = 0; i < array_count; i++) {
					VALIDATE_DESCRIPTOR(param->pbuffers[i], "NULL Buffer (%s [%u] )", desc->name, i);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkDescriptorBufferInfo, pBufferInfo, array_count);
					*currUpdateData = (VkDescriptorBufferInfo){ param->buffers[i]->vk.buffer, param->buffers[i]->vk.offset, VK_WHOLE_SIZE };
					if (param->ranges) {
						range32_t range = param->ranges[i];
#if defined(ENABLE_GRAPHICS_DEBUG)
						uint32_t max_range = DESCRIPTOR_TYPE_UNIFORM_BUFFER == type ?
							r->vk.activegpuprops->properties.limits.maxUniformBufferRange :
							r->vk.activegpuprops->properties.limits.maxStorageBufferRange;
#endif
						VALIDATE_DESCRIPTOR(range.mSize, "Descriptor (%s) - pRanges[%u].mSize is zero", desc->name, i);
						VALIDATE_DESCRIPTOR(
							range.mSize <= max_range,
							"Descriptor (%s) - pRanges[%u].mSize is %ull which exceeds max size %u", desc->name, i, range.size,
							max_range);
						currUpdateData->offset = range.offset;
						currUpdateData->range = range.size;
					}
				}
				break;
			case DESCRIPTOR_TYPE_TEXEL_BUFFER:
			case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER:
				VALIDATE_DESCRIPTOR(param->pbuffers, "NULL Texel Buffer (%s)", desc->name);
				writeset->pTexelBufferView = (VkBufferView*)descriptorUpdateData;
				for (uint32_t i = 0; i < array_count; i++) {
					VALIDATE_DESCRIPTOR(param->pbuffers[i], "NULL Texel Buffer (%s [%u] )", desc->name, i);
					FLUSH_OVERFLOW_DESCRIPTOR_UPDATES(VkBufferView, pTexelBufferView, array_count);
					*currUpdateData = DESCRIPTOR_TYPE_TEXEL_BUFFER == type ?
						param->buffers[i]->vk.uniformtexelview :
						param->buffers[i]->vk.storagetexelview;
				}
				break;
#ifdef VK_RAYTRACING_AVAILABLE
			case DESCRIPTOR_TYPE_RAY_TRACING:
				VALIDATE_DESCRIPTOR(param->ppAccelerationStructures, "NULL Acceleration Structure (%s)", desc->name);
				VkWriteDescriptorSetAccelerationStructureKHR writesetKHR = { 0 };
				VkAccelerationStructureKHR currUpdateData = { 0 };
				writeset->pNext = &writesetKHR;
				writeset->descriptorCount = 1;
				writesetKHR.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
				writesetKHR.pNext = NULL;
				writesetKHR.accelerationStructureCount = 1;
				for (uint32_t i = 0; i < array_count; i++) {
					vk_fill_raytracingdescriptordata(param->accelstructs[i], &currUpdateData);
					writesetKHR.pAccelerationStructures = &currUpdateData;
					vkUpdateDescriptorSets(r->vk.device, 1, writeset, 0, NULL);
					++writeset->dstArrayElement;
				}
				writeset->pNext = NULL;
				--num_writesets;
				break;
#endif
			default: break;
		}
	}
	vkUpdateDescriptorSets(r->vk.device, num_writesets, writesetarr, 0, NULL);
}

void vk_cmd_binddescset(cmd_t* cmd, uint32_t index, descset_t* descset)
{
	TC_ASSERT(cmd && descset);
	TC_ASSERT(descset->vk.handles);
	TC_ASSERT(index < descset->vk.maxsets);
	const rootsignature_t* rootsignature = descset->vk.rootsignature;
	if (cmd->vk.boundpipelinelayout != rootsignature->vk.pipelinelayout) {
		cmd->vk.boundpipelinelayout = rootsignature->vk.pipelinelayout;
		// Vulkan requires to bind all descriptor sets upto the highest set number even if they are empty
		// Example: If shader uses only set 2, we still have to bind empty sets for set=0 and set=1
		for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
			if (rootsignature->vk.emptydescriptorset[i] != VK_NULL_HANDLE)
				vkCmdBindDescriptorSets(
					cmd->vk.cmdbuf, pipelinebindpoint[rootsignature->pipelinetype], rootsignature->vk.pipelinelayout,
					i, 1, &rootsignature->vk.emptydescriptorset[i], 0, NULL);
	}
	static uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };
	vkCmdBindDescriptorSets(
		cmd->vk.cmdbuf, pipelinebindpoint[rootsignature->pipelinetype], rootsignature->vk.pipelinelayout,
		descset->vk.updatefreq, 1, &descset->vk.handles[index], descset->vk.dynamicoffsetcount, offsets);
}

void vk_cmd_bindpushconstants(cmd_t* cmd, rootsignature_t* rootsignature, uint32_t paramidx, const void* consts)
{
	TC_ASSERT(cmd && rootsignature && consts);
	TC_ASSERT(paramidx >= 0 && paramidx < rootsignature->descriptorcount);
	const descinfo_t* desc = rootsignature->descriptors + paramidx;
	TC_ASSERT(desc);
	TC_ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == desc->type);
	vkCmdPushConstants(cmd->vk.cmdbuf, rootsignature->vk.pipelinelayout, desc->vk.stages, 0, desc->size, consts);
}

void vk_cmd_binddescsetwithrootcbvs(cmd_t* cmd, uint32_t index, descset_t* descset, uint32_t count, const descdata_t* params)
{
	TC_ASSERT(cmd && descset && params);
	const rootsignature_t* rootsignature = descset->vk.rootsignature;
	uint32_t offsets[VK_MAX_ROOT_DESCRIPTORS] = { 0 };
	for (uint32_t i = 0; i < count; i++) {
		const descdata_t* param = params + i;
		uint32_t paramidx = param->bindbyidx ? param->index : UINT32_MAX;
		const descinfo_t* desc = (paramidx != UINT32_MAX) ? (rootsignature->descriptors + paramidx) : get_descriptor((rootsignature_t*)rootsignature, param->name);
		if (paramidx != UINT32_MAX) VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param index (%u)", paramidx);
		else VALIDATE_DESCRIPTOR(desc, "Invalid descriptor with param name (%s)", param->name);
#if defined(ENABLE_GRAPHICS_DEBUG)
		const uint32_t max_range = DESCRIPTOR_TYPE_UNIFORM_BUFFER == desc->type ?
			cmd->renderer->vk.activegpuprops->properties.limits.maxUniformBufferRange :
			cmd->renderer->vk.activegpuprops->properties.limits.maxStorageBufferRange;
#endif
		VALIDATE_DESCRIPTOR(desc->rootsignature, "Descriptor (%s) - must be a root cbv", desc->name);
		VALIDATE_DESCRIPTOR(param->count <= 1, "Descriptor (%s) - cmdBindDescriptorSetWithRootCbvs does not support arrays", desc->name);
		VALIDATE_DESCRIPTOR(param->ranges, "Descriptor (%s) - pRanges must be provided for cmdBindDescriptorSetWithRootCbvs", desc->name);
		range32_t range = param->ranges[0];
		VALIDATE_DESCRIPTOR(range.mSize, "Descriptor (%s) - pRanges->size is zero", desc->name);
		VALIDATE_DESCRIPTOR(
			range.mSize <= max_range,
			"Descriptor (%s) - pRanges->size is %ull which exceeds max size %u", desc->name, range.size,
			max_range);

		offsets[desc->handleindex] = range.offset;
		dynamicuniformdata_t* data = &descset->vk.dynamicuniformdata[index * descset->vk.dynamicoffsetcount + desc->handleindex];
		if (data->buffer != param->buffers[0]->vk.buffer || range.size != data->size)	{
			*data = (dynamicuniformdata_t){ param->buffers[0]->vk.buffer, 0, range.size };
			VkDescriptorBufferInfo info = { data->buffer, 0, (VkDeviceSize)data->size };
			VkWriteDescriptorSet writeset = { 0 };
			writeset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeset.pNext = NULL;
			writeset.descriptorCount = 1;
			writeset.descriptorType = (VkDescriptorType)desc->vk.type;
			writeset.dstArrayElement = 0;
			writeset.dstBinding = desc->vk.reg;
			writeset.dstSet = descset->vk.handles[index];
			writeset.pBufferInfo = &info;
			vkUpdateDescriptorSets(cmd->renderer->vk.device, 1, &writeset, 0, NULL);
		}
	}
	vkCmdBindDescriptorSets(
		cmd->vk.cmdbuf, pipelinebindpoint[rootsignature->pipelinetype], rootsignature->vk.pipelinelayout,
		descset->vk.updatefreq, 1, &descset->vk.handles[index],
		descset->vk.dynamicoffsetcount, offsets);
}

void vk_add_shaderbinary(renderer_t* r, const binaryshaderdesc_t* desc, shader_t* shader)
{
	TC_ASSERT(r && desc && shader);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	uint32_t counter = 0;
	size_t total = sizeof(pipelinereflection_t);
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		shaderstage_t mask = (shaderstage_t)(1 << i);
		if (mask == (desc->stages & mask)) {
			switch (mask) {
				case SHADER_STAGE_VERT: total += (strlen(desc->vert.entrypoint) + 1) * sizeof(char); break;
				case SHADER_STAGE_TESC: total += (strlen(desc->hull.entrypoint) + 1) * sizeof(char); break;
				case SHADER_STAGE_TESE: total += (strlen(desc->domain.entrypoint) + 1) * sizeof(char); break;
				case SHADER_STAGE_GEOM: total += (strlen(desc->geom.entrypoint) + 1) * sizeof(char); break;      
				case SHADER_STAGE_FRAG: total += (strlen(desc->frag.entrypoint) + 1) * sizeof(char); break;      
				case SHADER_STAGE_RAYTRACING:
				case SHADER_STAGE_COMP: total += (strlen(desc->comp.entrypoint) + 1) * sizeof(char); break;    
				default: break;
			}
			counter++;
		}
	}
	if (desc->constantcount) {
		total += sizeof(VkSpecializationInfo);
		total += sizeof(VkSpecializationMapEntry) * desc->constantcount;
		for (uint32_t i = 0; i < desc->constantcount; i++) {
			const shaderconstant_t* constant = &desc->constants[i];
			total += (constant->size == sizeof(bool)) ? sizeof(VkBool32) : constant->size;
		}
	}
	total += counter * sizeof(VkShaderModule);
	total += counter * sizeof(char*);
	uint8_t* mem = (uint8_t*)tc_calloc(1, total);
	shader->stages = desc->stages;
	shader->reflection = (pipelinereflection_t*)mem;
	shader->vk.shadermodules = (VkShaderModule*)(shader->reflection + 1);
	shader->vk.entrynames = (char**)(shader->vk.shadermodules + counter);
	shader->vk.specializationinfo = NULL;
	mem = (uint8_t*)(shader->vk.entrynames + counter);
	counter = 0;
	shaderreflection_t reflections[SHADER_STAGE_COUNT] = { 0 };
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		shaderstage_t mask = (shaderstage_t)(1 << i);
		if (mask == (shader->stages & mask)) {
			VkShaderModuleCreateInfo info = { 0 };
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			const binaryshaderstagedesc_t* stage = NULL;
			switch (mask) {
				case SHADER_STAGE_VERT:
					vk_create_shaderreflection((const uint8_t*)desc->vert.bytecode, (uint32_t)desc->vert.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->vert.bytecodesize;
					info.pCode = (const uint32_t*)desc->vert.bytecode;
					stage = &desc->vert;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs, &(shader->vk.shadermodules[counter])));
					break;
				case SHADER_STAGE_TESC:
					vk_create_shaderreflection((const uint8_t*)desc->hull.bytecode, (uint32_t)desc->hull.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->hull.bytecodesize;
					info.pCode = (const uint32_t*)desc->hull.bytecode;
					stage = &desc->hull;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs, &(shader->vk.shadermodules[counter])));
					break;
				case SHADER_STAGE_TESE:
					vk_create_shaderreflection((const uint8_t*)desc->domain.bytecode, (uint32_t)desc->domain.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->domain.bytecodesize;
					info.pCode = (const uint32_t*)desc->domain.bytecode;
					stage = &desc->domain;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs, &(shader->vk.shadermodules[counter])));
					break;
				case SHADER_STAGE_GEOM:
					vk_create_shaderreflection((const uint8_t*)desc->geom.bytecode, (uint32_t)desc->geom.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->geom.bytecodesize;
					info.pCode = (const uint32_t*)desc->geom.bytecode;
					stage = &desc->geom;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs,	&(shader->vk.shadermodules[counter])));
					break;
				case SHADER_STAGE_FRAG:
					vk_create_shaderreflection((const uint8_t*)desc->frag.bytecode, (uint32_t)desc->frag.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->frag.bytecodesize;
					info.pCode = (const uint32_t*)desc->frag.bytecode;
					stage = &desc->frag;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs, &(shader->vk.shadermodules[counter])));
					break;
				case SHADER_STAGE_COMP:
#ifdef VK_RAYTRACING_AVAILABLE
				case SHADER_STAGE_RAYTRACING:
#endif
					vk_create_shaderreflection((const uint8_t*)desc->comp.bytecode, (uint32_t)desc->comp.bytecodesize, mask, &reflections[counter]);
					info.codeSize = desc->comp.bytecodesize;
					info.pCode = (const uint32_t*)desc->comp.bytecode;
					stage = &desc->comp;
					CHECK_VKRESULT(vkCreateShaderModule(r->vk.device, &info, &alloccbs, &(shader->vk.shadermodules[counter])));
					break;
				default: TC_ASSERT(false && "Shader Stage not supported!"); break;
			}
			shader->vk.entrynames[counter] = (char*)mem;
			mem += (strlen(stage->entrypoint) + 1) * sizeof(char);
			strcpy(shader->vk.entrynames[counter], stage->entrypoint);
			counter++;
		}
	}
	if (desc->constantcount) {										// Fill specialization constant entries
		shader->vk.specializationinfo = (VkSpecializationInfo*)mem;
		mem += sizeof(VkSpecializationInfo);
		VkSpecializationMapEntry* map_entries = (VkSpecializationMapEntry*)mem;
		mem += desc->constantcount * sizeof(VkSpecializationMapEntry);
		uint8_t* data = mem;
		uint32_t offset = 0;
		for (uint32_t i = 0; i < desc->constantcount; i++) {
			const shaderconstant_t* constant = &desc->constants[i];
			const bool is_bool = constant->size == sizeof(bool);
			const uint32_t size = is_bool ? sizeof(VkBool32) : constant->size;
			VkSpecializationMapEntry* entry = &map_entries[i];
			entry->constantID = constant->index;
			entry->offset = offset;
			entry->size = size;
			if (is_bool) *(VkBool32*)(data + offset) = *(const bool*)constant->value;
			else memcpy(data + offset, constant->value, constant->size);
			offset += size;
		}
		VkSpecializationInfo* specializationInfo = shader->vk.specializationinfo;
		specializationInfo->dataSize = offset;
		specializationInfo->mapEntryCount = desc->constantcount;
		specializationInfo->pData = data;
		specializationInfo->pMapEntries = map_entries;
	}
	create_pipelinereflection(reflections, counter, shader->reflection);
	//add_shaderdependencies(shader, desc);
}

void vk_remove_shader(renderer_t* r, shader_t* shader)
{
	TC_ASSERT(r && shader);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	//remove_shaderdependencies(shader);
	if (shader->stages & SHADER_STAGE_VERT)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[shader->reflection->vertexidx], &alloccbs);
	if (shader->stages & SHADER_STAGE_TESC)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[shader->reflection->hullidx], &alloccbs);
	if (shader->stages & SHADER_STAGE_TESE)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[shader->reflection->domainidx], &alloccbs);
	if (shader->stages & SHADER_STAGE_GEOM)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[shader->reflection->geometryidx], &alloccbs);
	if (shader->stages & SHADER_STAGE_FRAG)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[shader->reflection->pixelidx], &alloccbs);
	if (shader->stages & SHADER_STAGE_COMP)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[0], &alloccbs);
#ifdef VK_RAYTRACING_AVAILABLE
	if (shader->stages & SHADER_STAGE_RAYTRACING)
		vkDestroyShaderModule(r->vk.device, shader->vk.shadermodules[0], &alloccbs);
#endif
	destroy_pipelinereflection(shader->reflection);
	tc_free(shader->reflection);
}

typedef struct {
	VkDescriptorSetLayoutBinding* bindings;	// Array of all bindings in the descriptor set
	descinfo_t** descriptors;				// Array of all descriptors in this descriptor set
	descinfo_t** dynamicdescriptors;		// Array of all descriptors marked as dynamic in this descriptor set (applicable to DESCRIPTOR_TYPE_UNIFORM_BUFFER)
} updatefreqinfo_t;

static bool comp_binding(const VkDescriptorSetLayoutBinding* a, const VkDescriptorSetLayoutBinding* b)
{
	if (b->descriptorType < a->descriptorType)
		return true;
	else if (b->descriptorType == a->descriptorType && b->binding < a->binding)
		return true;
	return false;
}

static bool comp_info(const descinfo_t** a, const descinfo_t** b) { return (*a)->vk.reg < (*b)->vk.reg; }

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, comp_binding)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, comp_binding, simpleSortVkDescriptorSetLayoutBinding)
DEFINE_PARTITION_IMPL_FUNCTION(static, partitionImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, comp_binding)
DEFINE_QUICK_SORT_IMPL_FUNCTION(static, quickSortImplVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, comp_binding, stableSortVkDescriptorSetLayoutBinding, partitionImplVkDescriptorSetLayoutBinding)
DEFINE_QUICK_SORT_FUNCTION(static, sortVkDescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding, quickSortImplVkDescriptorSetLayoutBinding)

DEFINE_SIMPLE_SORT_FUNCTION(static, simpleSortdescriptorInfo, descinfo_t*, comp_info)
DEFINE_INSERTION_SORT_FUNCTION(static, stableSortdescriptorInfo, descinfo_t*, comp_info, simpleSortdescriptorInfo)

typedef struct { char* key; sampler_t* value; } staticsamplernode_t;

void vk_add_rootsignature(renderer_t* r, const rootsignaturedesc_t* desc, rootsignature_t* rootsignature)
{
	TC_ASSERT(r && desc && rootsignature);
	updatefreqinfo_t layouts[DESCRIPTOR_UPDATE_FREQ_COUNT] = { 0 };
	VkPushConstantRange	pushconsts[SHADER_STAGE_COUNT] = { 0 };
	uint32_t numpushconsts = 0;
	shaderresource_t* shaderresources = NULL;
	staticsamplernode_t* staticsamplermap = NULL;
	sh_new_arena(staticsamplermap);
	for (uint32_t i = 0; i < desc->staticsamplercount; i++) {
		TC_ASSERT(desc->staticsamplers[i]);
		shput(staticsamplermap, desc->staticsamplernames[i], desc->staticsamplers[i]);
	}
	pipelinetype_t pipelinetype = PIPELINE_TYPE_UNDEFINED;
	descidxmap_t* map = NULL;
	sh_new_arena(map);
	// Collect all unique shader resources in the given shaders
	// Resources are parsed by name (two resources named "XYZ" in two shaders will be considered the same resource)
	for (uint32_t i = 0; i < desc->shadercount; i++) {
		pipelinereflection_t const* reflection = desc->shaders[i]->reflection;
		if (reflection->shaderstages & SHADER_STAGE_COMP)
			pipelinetype = PIPELINE_TYPE_COMPUTE;
#ifdef VK_RAYTRACING_AVAILABLE
		else if (reflection->shaderstages & SHADER_STAGE_RAYTRACING)
			pipelinetype = PIPELINE_TYPE_RAYTRACING;
#endif
		else pipelinetype = PIPELINE_TYPE_GRAPHICS;
		for (uint32_t j = 0; j < reflection->resourcecount; j++) {
			shaderresource_t* res = &reflection->resources[j];
			descidxmap_t* node = shgetp_null(map, res->name);
			if (!node) {
				shaderresource_t* resource = NULL;
				for (ptrdiff_t k = 0; k < arrlen(shaderresources); k++) {
					shaderresource_t* curr = &shaderresources[k];
					if (curr->type == res->type &&
						(curr->used_stages == res->used_stages) &&
						(((curr->reg ^ res->reg) | (curr->set ^ res->set)) == 0)) {
						resource = curr;
						break;
					}
				}
				if (!resource) {
					shput(map, res->name, (uint32_t)arrlen(shaderresources));
					arrpush(shaderresources, *res);
				}
				else {
					TC_ASSERT(res->type == resource->type);
					if (res->type != resource->type) {
						TRACE(LOG_ERROR,
							"\nFailed to create root signature\n"
							"Shared shader resources %s and %s have mismatching types (%u) and (%u). All shader resources "
							"sharing the same register and space addrootsignature_t "
							"must have the same type",
							res->name, resource->name, (uint32_t)res->type, (uint32_t)resource->type);
						return;
					}
					uint32_t value = shget(map, resource->name);
					shput(map, res->name, value);
					resource->used_stages |= res->used_stages;
				}
			}
			else {
				if (shaderresources[node->value].reg != res->reg) {
					TRACE(LOG_ERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching binding. All shader resources "
						"shared by multiple shaders specified in addrootsignature_t "
						"must have the same binding and set",
						res->name);
					return;
				}
				if (shaderresources[node->value].set != res->set) {
					TRACE(LOG_ERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching set. All shader resources "
						"shared by multiple shaders specified in addrootsignature_t "
						"must have the same binding and set",
						res->name);
					return;
				}
				for (ptrdiff_t j = 0; j < arrlen(shaderresources); j++) {
					if (strcmp(shaderresources[j].name, node->key) == 0) {
						shaderresources[j].used_stages |= res->used_stages;
						break;
					}
				}
			}
		}
	}
	memset(rootsignature, 0, sizeof(rootsignature_t));
	rootsignature->descriptors = (descinfo_t*)tc_calloc(arrlenu(shaderresources), sizeof(descinfo_t));
	TC_ASSERT(rootsignature->descriptors);
	rootsignature->descnametoidxmap = map;
	if (arrlen(shaderresources))
		rootsignature->descriptorcount = (uint32_t)arrlen(shaderresources);
	rootsignature->pipelinetype = pipelinetype;

	// Fill the descriptor array to be stored in the root signature
	for (ptrdiff_t i = 0; i < arrlen(shaderresources); i++) {
		descinfo_t* descinfo = &rootsignature->descriptors[i];
		shaderresource_t* res = &shaderresources[i];
		uint32_t i = res->set;
		descriptorupdatefreq_t freq = (descriptorupdatefreq_t)i;
		descinfo->vk.reg = res->reg;	// Copy the binding information generated from the shader reflection into the descriptor
		descinfo->size = res->size;
		descinfo->type = res->type;
		descinfo->name = res->name;
		descinfo->dim = res->dim;

		// If descriptor is not a root constant create a new layout binding for this descriptor and add it to the binding array
		if (descinfo->type != DESCRIPTOR_TYPE_ROOT_CONSTANT) {
			VkDescriptorSetLayoutBinding binding = { 0 };
			binding.binding = res->reg;
			binding.descriptorCount = descinfo->size;
			binding.descriptorType = _to_vk_descriptor_type((descriptortype_t)descinfo->type);
			// If a user specified a uniform buffer to be used as a dynamic uniform buffer change its type to VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			// Also log a message for debugging purpose
			if (is_rootcbv(res->name)) {
				if (descinfo->size == 1) {
					TRACE(LOG_INFO, "Descriptor (%s) : User specified VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC", descinfo->name);
					binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				}
				else {
					TRACE(
						LOG_WARNING, "Descriptor (%s) : Cannot use VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC for arrays",
						descinfo->name);
				}
			}
			binding.stageFlags = _to_vk_shader_stage_flags(res->used_stages);
			// Store the vulkan related info in the descriptor to avoid constantly calling the util_to_vk mapping functions
			descinfo->vk.type = binding.descriptorType;
			descinfo->vk.stages = binding.stageFlags;
			descinfo->updatefreq = freq;
			staticsamplernode_t* it = shgetp_null(staticsamplermap, descinfo->name);	// Find if the given descriptor is a static sampler
			if (it) {
				TRACE(LOG_INFO, "Descriptor (%s) : User specified Static Sampler", descinfo->name);
				binding.pImmutableSamplers = &it->value->vk.sampler;
			}
			// Set the index to an invalid value so we can use this later for error checking if user tries to update a static sampler
			// In case of Combined Image Samplers, skip invalidating the index
			// because we do not to introduce new ways to update the descriptor in the Interface
			if (it && descinfo->type != DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				descinfo->staticsampler = true;
			else arrpush(layouts[i].descriptors, descinfo);
			if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
				arrpush(layouts[i].dynamicdescriptors, descinfo);
				descinfo->rootdescriptor = true;
			}
			arrpush(layouts[i].bindings, binding);
			VkDescriptorPoolSize* poolsize = NULL;	// Update descriptor pool size for this descriptor type
			for (uint32_t j = 0; j < MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT; j++) {
				if (binding.descriptorType == rootsignature->vk.poolsizes[i][j].type && rootsignature->vk.poolsizes[i][j].descriptorCount) {
					poolsize = &rootsignature->vk.poolsizes[i][j];
					break;
				}
			}
			if (!poolsize) {
				poolsize = &rootsignature->vk.poolsizes[i][rootsignature->vk.poolsizecount[i]++];
				poolsize->type = binding.descriptorType;
			}
			poolsize->descriptorCount += binding.descriptorCount;
		}
		else {										// If descriptor is a root constant, add it to the root constant array
			TRACE(LOG_INFO, "Descriptor (%s) : User specified Push Constant", descinfo->name);
			descinfo->rootdescriptor = true;
			descinfo->vk.stages = _to_vk_shader_stage_flags(res->used_stages);
			i = 0;
			descinfo->handleindex = numpushconsts++;
			pushconsts[descinfo->handleindex].offset = 0;
			pushconsts[descinfo->handleindex].size = descinfo->size;
			pushconsts[descinfo->handleindex].stageFlags = descinfo->vk.stages;
		}
	}
	// Create descriptor layouts
	// Put least frequently changed params first
	for (uint32_t i = DESCRIPTOR_UPDATE_FREQ_COUNT; i >= 0; i--) {
		updatefreqinfo_t* layout = &layouts[i];
		if (arrlen(layouts[i].bindings))			// sort table by type (CBV/SRV/UAV) by register
			sortVkDescriptorSetLayoutBinding(layout->bindings, arrlenu(layout->bindings));

		bool createlayout = arrlen(layout->bindings) != 0;
		// Check if we need to create an empty layout in case there is an empty set between two used sets
		// Example: set = 0 is used, set = 2 is used. In this case, set = 1 needs to exist even if it is empty
		if (!createlayout && i < DESCRIPTOR_UPDATE_FREQ_COUNT - 1)
			createlayout = rootsignature->vk.descriptorsetlayouts[i + 1] != VK_NULL_HANDLE;
		if (createlayout) {
			if (arrlen(layout->bindings)) {
				VkDescriptorSetLayoutCreateInfo info = { 0 };
				info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = (uint32_t)arrlen(layout->bindings);
				info.pBindings = layout->bindings;
				CHECK_VKRESULT(vkCreateDescriptorSetLayout(r->vk.device, &info, &alloccbs, &rootsignature->vk.descriptorsetlayouts[i]));
			}
			else rootsignature->vk.descriptorsetlayouts[i] = r->vk.emptydescriptorsetlayout;
		}
		if (!arrlen(layout->bindings)) continue;
		uint32_t cumdescriptorcount = 0;
		for (ptrdiff_t j = 0; j < arrlen(layout->descriptors); j++) {
			descinfo_t* descinfo = layout->descriptors[j];
			if (!descinfo->rootdescriptor) {
				descinfo->handleindex = cumdescriptorcount;
				cumdescriptorcount += descinfo->size;
			}
		}
		if (arrlen(layout->dynamicdescriptors)) {
			// vkCmdBindDescriptorSets - pDynamicOffsets - entries are ordered by the binding numbers in the descriptor set layouts
			stableSortdescriptorInfo(layout->dynamicdescriptors, arrlenu(layout->dynamicdescriptors));
			rootsignature->vk.dynamicdescriptorcounts[i] = (uint32_t)arrlen(layout->dynamicdescriptors);
			for (uint32_t j = 0; j < (uint32_t)arrlen(layout->dynamicdescriptors); j++)
				layout->dynamicdescriptors[j]->handleindex = j;
		}
	}
	// Pipeline layout
	VkDescriptorSetLayout descsetlayouts[DESCRIPTOR_UPDATE_FREQ_COUNT] = { 0 };
	uint32_t n = 0;
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
		if (rootsignature->vk.descriptorsetlayouts[i])
			descsetlayouts[n++] = rootsignature->vk.descriptorsetlayouts[i];

	VkPipelineLayoutCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.setLayoutCount = n;
	info.pSetLayouts = descsetlayouts;
	info.pushConstantRangeCount = numpushconsts;
	info.pPushConstantRanges = pushconsts;
	CHECK_VKRESULT(vkCreatePipelineLayout(r->vk.device, &info, &alloccbs, &(rootsignature->vk.pipelinelayout)));
	// Update templates
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i) {
		const updatefreqinfo_t* layout = &layouts[i];
		if (!arrlen(layout->descriptors) && rootsignature->vk.descriptorsetlayouts[i] != VK_NULL_HANDLE) {
			rootsignature->vk.emptydescriptorset[i] = r->vk.emptydescriptorset;
			if (rootsignature->vk.descriptorsetlayouts[i] != r->vk.emptydescriptorsetlayout) {
				add_descriptor_pool(r, 1, 0,
					rootsignature->vk.poolsizes[i], rootsignature->vk.poolsizecount[i],
					&rootsignature->vk.emptydescriptorpool[i]);
				VkDescriptorSet* emptySet[] = { &rootsignature->vk.emptydescriptorset[i] };
				consume_descriptor_sets(r->vk.device, rootsignature->vk.emptydescriptorpool[i],
					&rootsignature->vk.descriptorsetlayouts[i], 1, emptySet);
			}
		}
	}
	//add_rootsignaturedependencies(rootsignature, desc);
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; i++) {
		arrfree(layouts[i].bindings);
		arrfree(layouts[i].descriptors);
		arrfree(layouts[i].dynamicdescriptors);
	}
	arrfree(shaderresources);
	shfree(staticsamplermap);
}

void vk_remove_rootsignature(renderer_t* r, rootsignature_t* rootsignature)
{
	//remove_rootsignaturedependencies(rootsignature);
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; i++) {
		if (rootsignature->vk.descriptorsetlayouts[i] != r->vk.emptydescriptorsetlayout)
			vkDestroyDescriptorSetLayout(r->vk.device, rootsignature->vk.descriptorsetlayouts[i], &alloccbs);
		if (VK_NULL_HANDLE != rootsignature->vk.emptydescriptorpool[i])
			vkDestroyDescriptorPool(r->vk.device, rootsignature->vk.emptydescriptorpool[i], &alloccbs);
	}
	shfree(rootsignature->descnametoidxmap);
	vkDestroyPipelineLayout(r->vk.device, rootsignature->vk.pipelinelayout, &alloccbs);
	tc_free(rootsignature->descriptors);
}

static void add_graphicspipeline(renderer_t* r, const pipelinedesc_t* desc, pipeline_t* pipeline)
{
	TC_ASSERT(r && pipeline && desc);
	const graphicspipelinedesc_t* gfxdesc = &desc->graphicsdesc;
	VkPipelineCache cache = desc->cache ? desc->cache->vk.cache : VK_NULL_HANDLE;
	TC_ASSERT(gfxdesc->shader);
	TC_ASSERT(gfxdesc->rootsignature);
	memset(pipeline, 0, sizeof(pipeline_t));
	const shader_t* shader = gfxdesc->shader;
	const vertexlayout_t* vertlayout = gfxdesc->vertexlayout;
	pipeline->vk.type = PIPELINE_TYPE_GRAPHICS;

	// Create tempporary renderpass for pipeline creation
	renderpassdesc_t rpdesc = { 0 };
	renderpass_t renderPass = { 0 };
	rpdesc.rtcount = gfxdesc->rendertargetcount;
	rpdesc.colorfmts = gfxdesc->colorformats;
	rpdesc.samplecount = gfxdesc->samplecount;
	rpdesc.depthstencilfmt = gfxdesc->depthstencilformat;
    rpdesc.vrmultiview = gfxdesc->shader->isVR;
	rpdesc.loadopcolor = defaultloadops;
	rpdesc.loadopdepth = defaultloadops[0];
	rpdesc.loadopstencil = defaultloadops[1];
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	rpdesc.storeopcolor = gfxdesc->pColorResolveActions ? desc->pColorResolveActions : gDefaultStoreActions;
#else
	rpdesc.storeopcolor = defaultstoreops;
#endif
	rpdesc.storeopdepth = defaultstoreops[0];
	rpdesc.storeopstencil = defaultstoreops[1];
	add_renderpass(r, &rpdesc, &renderPass);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	for (uint32_t i = 0; i < shader->reflection->reflectioncount; ++i)
		TC_ASSERT(VK_NULL_HANDLE != shader->vk.shadermodules[i]);

	const VkSpecializationInfo* specializationInfo = shader->vk.specializationinfo;
	uint32_t stage_count = 0;
	VkPipelineShaderStageCreateInfo stages[5] = { 0 };
	for (uint32_t i = 0; i < 5; i++) {
		shaderstage_t mask = (shaderstage_t)(1 << i);
		if (mask == (shader->stages & mask)) {
			stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[stage_count].pSpecializationInfo = specializationInfo;
			switch (mask) {
				case SHADER_STAGE_VERT:
					stages[stage_count].pName = shader->reflection->reflections[shader->reflection->vertexidx].entrypoint;
					stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
					stages[stage_count].module = shader->vk.shadermodules[shader->reflection->vertexidx];
					break;
				case SHADER_STAGE_TESC:
					stages[stage_count].pName = shader->reflection->reflections[shader->reflection->hullidx].entrypoint;
					stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					stages[stage_count].module = shader->vk.shadermodules[shader->reflection->hullidx];
					break;
				case SHADER_STAGE_TESE:
					stages[stage_count].pName = shader->reflection->reflections[shader->reflection->domainidx].entrypoint;
					stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					stages[stage_count].module = shader->vk.shadermodules[shader->reflection->domainidx];
					break;
				case SHADER_STAGE_GEOM:
					stages[stage_count].pName = shader->reflection->reflections[shader->reflection->geometryidx].entrypoint;
					stages[stage_count].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
					stages[stage_count].module = shader->vk.shadermodules[shader->reflection->geometryidx];
					break;
				case SHADER_STAGE_FRAG:
					stages[stage_count].pName = shader->reflection->reflections[shader->reflection->pixelidx].entrypoint;
					stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
					stages[stage_count].module = shader->vk.shadermodules[shader->reflection->pixelidx];
					break;
				default: TC_ASSERT(false && "Shader Stage not supported!"); break;
			}
			stage_count++;
		}
		TC_ASSERT(0 != stage_count);
		uint32_t input_binding_count = 0;
		VkVertexInputBindingDescription input_bindings[MAX_VERTEX_BINDINGS] = { { 0 } };
		uint32_t input_attribute_count = 0;
		VkVertexInputAttributeDescription input_attributes[MAX_VERTEX_ATTRIBS] = { { 0 } };
		if (vertlayout != NULL) {							// Make sure there's attributes. Ignore everything that's beyond max_vertex_attribs
			uint32_t attrib_count = vertlayout->attribcount > MAX_VERTEX_ATTRIBS ? MAX_VERTEX_ATTRIBS : vertlayout->attribcount;
			uint32_t binding_value = UINT32_MAX;
			for (uint32_t i = 0; i < attrib_count; i++) {
				const vertexattrib_t* attrib = &(vertlayout->attribs[i]);
				if (binding_value != attrib->binding) {
					binding_value = attrib->binding;
					input_binding_count++;
				}
				input_bindings[input_binding_count - 1].binding = binding_value;
				if (attrib->rate == VERTEX_ATTRIB_RATE_INSTANCE)
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
				else
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				input_bindings[input_binding_count - 1].stride += TinyImageFormat_BitSizeOfBlock(attrib->format) / 8;
				input_attributes[input_attribute_count].location = attrib->location;
				input_attributes[input_attribute_count].binding = attrib->binding;
				input_attributes[input_attribute_count].format = (VkFormat)TinyImageFormat_ToVkFormat(attrib->format);
				input_attributes[input_attribute_count].offset = attrib->offset;
				++input_attribute_count;
			}
		}
		VkPipelineVertexInputStateCreateInfo vi = { 0 };
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = input_binding_count;
		vi.pVertexBindingDescriptions = input_bindings;
		vi.vertexAttributeDescriptionCount = input_attribute_count;
		vi.pVertexAttributeDescriptions = input_attributes;
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		switch (gfxdesc->primitivetopo) {
			case PRIMITIVE_TOPO_POINT_LIST: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
			case PRIMITIVE_TOPO_LINE_LIST: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
			case PRIMITIVE_TOPO_LINE_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
			case PRIMITIVE_TOPO_TRI_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
			case PRIMITIVE_TOPO_PATCH_LIST: topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST; break;
			case PRIMITIVE_TOPO_TRI_LIST: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
			default: TC_ASSERT(false && "Primitive Topo not supported!"); break;
		}
		VkPipelineInputAssemblyStateCreateInfo ia = { 0 };
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = topology;
		VkPipelineTessellationStateCreateInfo ts = { 0 };
		if ((shader->stages & SHADER_STAGE_TESC) && (shader->stages & SHADER_STAGE_TESE)) {
			ts.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
			ts.patchControlPoints = shader->reflection->reflections[shader->reflection->hullidx].numcontrolpoint;
		}
		VkPipelineViewportStateCreateInfo vs = { 0 };
		vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vs.viewportCount = 1;			// we are using dynamic viewports but we must set the count to 1
		vs.scissorCount = 1;
		
		VkPipelineMultisampleStateCreateInfo ms = { 0 };
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = _to_vk_sample_count(gfxdesc->samplecount);

		VkPipelineRasterizationStateCreateInfo rs = { 0 };
		rs = gfxdesc->rasterizerstate ? _to_rasterizer_desc(gfxdesc->rasterizerstate) : defaultrasterizerdesc;

		VkPipelineDepthStencilStateCreateInfo ds = { 0 };
		ds = gfxdesc->depthstate ? _to_depth_desc(gfxdesc->depthstate) : defaultdepthdesc;

		VkPipelineColorBlendStateCreateInfo cb = { 0 };
		VkPipelineColorBlendAttachmentState cbAtt[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
		cb = gfxdesc->blendstate ? _to_blend_desc(gfxdesc->blendstate, cbAtt) : defaultblenddesc;
		cb.attachmentCount = gfxdesc->rendertargetcount;

		VkDynamicState dyn_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_BLEND_CONSTANTS,
			VK_DYNAMIC_STATE_DEPTH_BOUNDS,
			VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		};
		VkPipelineDynamicStateCreateInfo dy = { 0 };
		dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dy.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]);
		dy.pDynamicStates = dyn_states;

		VkGraphicsPipelineCreateInfo info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = stage_count;
		info.pStages = stages;
		info.pVertexInputState = &vi;
		info.pInputAssemblyState = &ia;
		if ((shader->stages & SHADER_STAGE_TESC) && (shader->stages & SHADER_STAGE_TESE))
			info.pTessellationState = &ts;
		else
			info.pTessellationState = NULL;
		info.pViewportState = &vs;
		info.pRasterizationState = &rs;
		info.pMultisampleState = &ms;
		info.pDepthStencilState = &ds;
		info.pColorBlendState = &cb;
		info.pDynamicState = &dy;
		info.layout = gfxdesc->rootsignature->vk.pipelinelayout;
		info.renderPass = renderPass.renderpass;
		info.subpass = 0;
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.basePipelineIndex = -1;
		CHECK_VKRESULT(vkCreateGraphicsPipelines(r->vk.device, cache, 1, &info, &alloccbs, &(pipeline->vk.pipeline)));
		remove_renderpass(r, &renderPass);
	}
}

static void add_computepipeline(renderer_t* r, const pipelinedesc_t* desc, pipeline_t* pipeline)
{
	TC_ASSERT(r && pipeline && desc);
	const computepipelinedesc_t* cdesc = &desc->computedesc;
	VkPipelineCache cache = desc->cache ? desc->cache->vk.cache : VK_NULL_HANDLE;
	TC_ASSERT(cdesc->shader);
	TC_ASSERT(cdesc->rootsignature);
	TC_ASSERT(r->vk.device != VK_NULL_HANDLE);
	TC_ASSERT(cdesc->shader->vk.shadermodules[0] != VK_NULL_HANDLE);
	pipeline->vk.type = PIPELINE_TYPE_COMPUTE;

	VkPipelineShaderStageCreateInfo stage = { 0 };
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cdesc->shader->vk.shadermodules[0];
	stage.pName = cdesc->shader->reflection->reflections[0].entrypoint;
	stage.pSpecializationInfo = cdesc->shader->vk.specializationinfo;

	VkComputePipelineCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.stage = stage;
	info.layout = cdesc->rootsignature->vk.pipelinelayout;
	CHECK_VKRESULT(vkCreateComputePipelines(r->vk.device, cache, 1, &info, &alloccbs, &(pipeline->vk.pipeline)));
}

void vk_add_pipeline(renderer_t* r, const pipelinedesc_t* desc, pipeline_t* pipeline)
{
	switch (desc->type) {
		case (PIPELINE_TYPE_COMPUTE):
			add_computepipeline(r, desc, pipeline);
			break;
		case (PIPELINE_TYPE_GRAPHICS):
			add_graphicspipeline(r, desc, pipeline);
			break;
#ifdef VK_RAYTRACING_AVAILABLE
		case (PIPELINE_TYPE_RAYTRACING):
			//vk_add_raytracingpipeline(desc, pipeline);
			break;
#endif
		default:
			TC_ASSERT(false);
			break;
	}
	//add_pipelinedependencies(pipeline, desc);
	if (pipeline && desc->name) set_pipelinename(r, pipeline, desc->name);
}

void vk_remove_pipeline(renderer_t* r, pipeline_t* pipeline)
{
	//remove_pipelinedependencies(pipeline);
	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(VK_NULL_HANDLE != r->vk.device);
	TC_ASSERT(VK_NULL_HANDLE != pipeline->vk.pipeline);
#ifdef VK_RAYTRACING_AVAILABLE
	tc_free(pipeline->vk.stagenames);
#endif
	vkDestroyPipeline(r->vk.device, pipeline->vk.pipeline, &alloccbs);
}

void vk_add_pipelinecache(renderer_t* r, const pipelinecachedesc_t* desc, pipelinecache_t* cache)
{
	TC_ASSERT(r && desc && cache);
	memset(cache, 0, sizeof(pipelinecache_t));
	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	info.initialDataSize = desc->size;
	info.pInitialData = desc->data;
	info.flags = _to_pipeline_cache_flags(desc->flags);
	CHECK_VKRESULT(vkCreatePipelineCache(r->vk.device, &info, &alloccbs, &cache->vk.cache));
}

void vk_remove_pipelinecache(renderer_t* r, pipelinecache_t* cache)
{
	TC_ASSERT(r && cache);
	if (cache->vk.cache)
		vkDestroyPipelineCache(r->vk.device, cache->vk.cache, &alloccbs);
}

void vk_get_pipelinecachedata(renderer_t* r, pipelinecache_t* cache, size_t* size, void* data)
{
	TC_ASSERT(r && cache && size);
	if (cache->vk.cache)
		CHECK_VKRESULT(vkGetPipelineCacheData(r->vk.device, cache->vk.cache, size, data));
}

void vk_reset_cmdpool(renderer_t* r, cmdpool_t* pool)
{
	TC_ASSERT(r && pool);
	CHECK_VKRESULT(vkResetCommandPool(r->vk.device, pool->cmdpool, 0));
}

void vk_cmd_begin(cmd_t* cmd)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	VkCommandBufferBeginInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkDeviceGroupCommandBufferBeginInfoKHR deviceGroupBeginInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO_KHR };
	deviceGroupBeginInfo.pNext = NULL;
	if (cmd->renderer->gpumode == GPU_MODE_LINKED) {
		deviceGroupBeginInfo.deviceMask = (1 << cmd->vk.nodeidx);
		info.pNext = &deviceGroupBeginInfo;
	}
	CHECK_VKRESULT(vkBeginCommandBuffer(cmd->vk.cmdbuf, &info));
	cmd->vk.boundpipelinelayout = NULL;		// Reset CPU side data
}

void vk_cmd_end(cmd_t* cmd)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	if (cmd->vk.activerenderpass)
		vkCmdEndRenderPass(cmd->vk.cmdbuf);

	cmd->vk.activerenderpass = VK_NULL_HANDLE;
	CHECK_VKRESULT(vkEndCommandBuffer(cmd->vk.cmdbuf));
}

void vk_cmd_bindrendertargets(
	cmd_t* cmd, uint32_t rtcount, rendertarget_t** rts, rendertarget_t* depthstencil, const loadopsdesc_t* loadops,
	uint32_t* colorarrayslices, uint32_t* colormipslices, uint32_t deptharrayslice, uint32_t depthmipslice)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	if (cmd->vk.activerenderpass) {
		vkCmdEndRenderPass(cmd->vk.cmdbuf);
		cmd->vk.activerenderpass = VK_NULL_HANDLE;
	}
	if (!rtcount && !depthstencil)
		return;

	uint32_t rphash = 0;
	uint32_t fbhash = 0;
	storeop_t colorstoreop[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
	storeop_t depthstoreop = { 0 };
	storeop_t stencilstoreop = { 0 };
	uint32_t fbrtcount = 0;
	// Generate hash for render pass and frame buffer
	// Render pass does not care about underlying VkImageView. It only cares about the format and sample count of the attachments.
	// We hash those two values to generate render pass hash
	// Frame buffer is the actual array of all the VkImageViews
	// We hash the texture id associated with the render target to generate frame buffer hash
	for (uint32_t i = 0; i < rtcount; i++) {
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		bool resolveattachment = loadops && _is_storeop_resolve(loadops->storecolor[i]);
		if (resolveattachment)
			colorstoreop[i] = rts[i]->texture->lazilyallocated ? STORE_ACTION_RESOLVE_DONTCARE : STORE_ACTION_DONTCARE;
		else
#endif
			colorstoreop[i] = rts[i]->tex.lazilyallocated ? STORE_ACTION_DONTCARE : (loadops ? loadops->storecolor[i] : defaultstoreops[i]);

		uint32_t rpvals[] = {
			(uint32_t)rts[i]->format,
			(uint32_t)rts[i]->samplecount,
			loadops ? (uint32_t)loadops->loadcolor[i] : defaultloadops[i],
			(uint32_t)colorstoreop[i]
		};
		uint32_t fbvals[] = {
			rts[i]->vk.id,
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
			resolveattachment ? rts[i]->resolveattachment->vk.id : 0
#endif
		};
		rphash = crc32_b((uint8_t*)rpvals, TC_COUNT(rpvals) * 4);
		fbhash = crc32_b((uint8_t*)fbvals, TC_COUNT(fbvals) * 4);
		fbrtcount++;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		fbrtcount += resolveattachment ? 1 : 0;
#endif
	}
	if (depthstencil) {
		depthstoreop = depthstencil->tex.lazilyallocated ? STORE_ACTION_DONTCARE :
			(loadops ? loadops->storedepth : defaultstoreops[0]);
		stencilstoreop = depthstencil->tex.lazilyallocated ? STORE_ACTION_DONTCARE :
			(loadops ? loadops->storestencil : defaultstoreops[0]);
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)			// Dont support depth stencil auto resolve
		TC_ASSERT(!(_is_storeop_resolve(depthstoreop) || _is_storeop_resolve(stencilstoreop)));
#endif
		uint32_t hashvals[] = {
			(uint32_t)depthstencil->format,
			(uint32_t)depthstencil->samplecount,
			loadops ? (uint32_t)loadops->loaddepth : defaultloadops[0],
			loadops ? (uint32_t)loadops->loadstencil : defaultloadops[0],
			(uint32_t)depthstoreop,
			(uint32_t)stencilstoreop,
			rphash
		};
		rphash = crc32_b((uint8_t*)hashvals, 7 * 4);
		fbhash = crc32_b((uint8_t*)(uint32_t[]){ depthstencil->vk.id, fbhash }, 8);
	}
	if (colorarrayslices)
		fbhash = crc32_b((uint8_t*)((uint32_t[]){ crc32_b((uint8_t*)colorarrayslices, rtcount * 4), fbhash }), 8);
	if (colormipslices)
		fbhash = crc32_b((uint8_t*)((uint32_t[]){ crc32_b((uint8_t*)colormipslices, rtcount * 4), fbhash }), 8);
	if (deptharrayslice != UINT32_MAX)
		fbhash = crc32_b((uint8_t*)((uint32_t[]){ deptharrayslice, fbhash }), 8);
	if (depthmipslice != UINT32_MAX)
		fbhash = crc32_b((uint8_t*)((uint32_t[]){ depthmipslice, fbhash }), 8);
	
	samplecount_t samplecount = SAMPLE_COUNT_1;
	// Need pointer to pointer in order to reassign hash map when it is resized
	renderpassnode_t** rpmap = get_render_pass_map(cmd->renderer->unlinkedrendererindex);
	framebuffernode_t** fbmap = get_frame_buffer_map(cmd->renderer->unlinkedrendererindex);
	renderpassnode_t* rpnode = hmgetp_null(*rpmap, rphash);
	framebuffernode_t* fbnode = hmgetp_null(*fbmap, fbhash);
	if (!rpnode) {					// If a render pass of this combination already exists just use it or create a new one
		TinyImageFormat colorfmts[MAX_RENDER_TARGET_ATTACHMENTS] = { 0 };
		TinyImageFormat depthstencilfmt = TinyImageFormat_UNDEFINED;
		bool vr = false;
		for (uint32_t i = 0; i < rtcount; ++i) {
			colorfmts[i] = rts[i]->format;
			vr |= rts[i]->vr;
		}
		if (depthstencil) {
			depthstencilfmt = depthstencil->format;
			samplecount = depthstencil->samplecount;
			vr |= depthstencil->vr;
		}
		else if (rtcount)
			samplecount = rts[0]->samplecount;
		renderpass_t renderpass = { 0 };
		renderpassdesc_t desc = { 0 };
		desc.rtcount = rtcount;
		desc.samplecount = samplecount;
		desc.colorfmts = colorfmts;
		desc.depthstencilfmt = depthstencilfmt;
		desc.loadopcolor = loadops ? loadops->loadcolor : defaultloadops;
		desc.loadopdepth = loadops ? loadops->loaddepth : defaultloadops[0];
		desc.loadopstencil = loadops ? loadops->loadstencil : defaultloadops[0];
		desc.storeopcolor = colorstoreop;
		desc.storeopdepth = depthstoreop;
		desc.storeopstencil = stencilstoreop;
		desc.vrmultiview = vr;
		add_renderpass(cmd->renderer, &desc, &renderpass);
		hmput(*rpmap, rphash, renderpass);
		rpnode = hmgetp_null(*rpmap, rphash);
	}
	renderpass_t* renderpass = &rpnode->value;
	if (!fbnode) {					// If a frame buffer of this combination already exists just use it or create a new one
		framebuffer_t framebuffer = { 0 };
		framebufferdesc_t desc = { 0 };
		desc.rtcount = rtcount;
		desc.depthstencil = depthstencil;
		desc.rendertargets = rts;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
		desc.rtresolveop = colorstoreop;
#endif
		desc.renderpass = renderpass;
		desc.colorarrayslices = colorarrayslices;
		desc.colormipslices = colormipslices;
		desc.deptharrayslice = deptharrayslice;
		desc.depthmipslice = depthmipslice;
		add_framebuffer(cmd->renderer, &desc, &framebuffer);
		hmput(*fbmap, fbhash, framebuffer);
		fbnode = hmgetp_null(*fbmap, fbhash);
	}
	framebuffer_t* framebuffer = &fbnode->value;
	VkRect2D render_area = { 0 };
	render_area.extent.width = framebuffer->width;
	render_area.extent.height = framebuffer->height;
	uint32_t n = fbrtcount;
	VkClearValue clearvalues[VK_MAX_ATTACHMENT_ARRAY_COUNT] = { 0 };
	if (loadops) {
		for (uint32_t i = 0; i < rtcount; ++i) {
			clearvalue_t c = loadops->clearcolors[i];
			clearvalues[i].color = (VkClearColorValue){ { c.r, c.g, c.b, c.a } };
		}
		if (depthstencil) {
			clearvalues[fbrtcount].depthStencil = (VkClearDepthStencilValue){ loadops->cleardepth.depth, loadops->cleardepth.stencil };
			n++;
		}
	}
	VkRenderPassBeginInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.renderPass = renderpass->renderpass;
	info.framebuffer = framebuffer->framebuffer;
	info.renderArea = render_area;
	info.clearValueCount = n;
	info.pClearValues = clearvalues;
	vkCmdBeginRenderPass(cmd->vk.cmdbuf, &info, VK_SUBPASS_CONTENTS_INLINE);
	cmd->vk.activerenderpass = renderpass->renderpass;
}

void vk_cmd_setshadingrate(cmd_t* cmd, shadingrate_t rate, texture_t* tex, shadingratecombiner_t post, shadingratecombiner_t final) {}

void vk_cmd_setviewport(cmd_t* cmd, float x, float y, float width, float height, float mindepth, float maxdepth)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	VkViewport viewport = { 0 };
	viewport.x = x;
    viewport.y = y + height;
	viewport.width = width;
    viewport.height = -height;
	viewport.minDepth = mindepth;
	viewport.maxDepth = maxdepth;
	vkCmdSetViewport(cmd->vk.cmdbuf, 0, 1, &viewport);
}

void vk_cmd_setscissor(cmd_t* cmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	VkRect2D rect = { 0 };
	rect.offset.x = x;
	rect.offset.y = y;
	rect.extent.width = width;
	rect.extent.height = height;
	vkCmdSetScissor(cmd->vk.cmdbuf, 0, 1, &rect);
}

void vk_cmd_setstencilreferenceval(cmd_t* cmd, uint32_t val)
{
	TC_ASSERT(cmd);
	vkCmdSetStencilReference(cmd->vk.cmdbuf, VK_STENCIL_FRONT_AND_BACK, val);
}

void vk_cmd_bindpipeline(cmd_t* cmd, pipeline_t* pipeline)
{
	TC_ASSERT(cmd && pipeline);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	VkPipelineBindPoint bindpoint = pipelinebindpoint[pipeline->vk.type];
	vkCmdBindPipeline(cmd->vk.cmdbuf, bindpoint, pipeline->vk.pipeline);
}

void vk_cmd_bindindexbuffer(cmd_t* cmd, buffer_t* buffer, uint32_t idxtype, uint64_t offset)
{
	TC_ASSERT(cmd && buffer);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdBindIndexBuffer(cmd->vk.cmdbuf, buffer->vk.buffer, offset, (INDEX_TYPE_UINT16 == idxtype) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void vk_cmd_bindvertexbuffer(cmd_t* cmd, uint32_t count, buffer_t* bufs, const uint32_t* strides, const uint64_t* offsets)
{
	TC_ASSERT(cmd);
	TC_ASSERT(count != 0);
	TC_ASSERT(bufs);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	TC_ASSERT(strides);
	const uint32_t max_buffers = cmd->renderer->vk.activegpuprops->properties.limits.maxVertexInputBindings;
	uint32_t capped_buffer_count = count > max_buffers ? max_buffers : count;
	// No upper bound for this, so use 64 for now
	TC_ASSERT(capped_buffer_count < 64);
	VkBuffer vbuffers[64] = { 0 };
	VkDeviceSize voffsets[64] = { 0 };
	for (uint32_t i = 0; i < capped_buffer_count; i++) {
		vbuffers[i] = bufs[i].vk.buffer;
		voffsets[i] = (offsets ? offsets[i] : 0);;
	}
	vkCmdBindVertexBuffers(cmd->vk.cmdbuf, 0, capped_buffer_count, vbuffers, voffsets);
}

void vk_cmd_draw(cmd_t* cmd, uint32_t vertex_count, uint32_t first_vertex)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdDraw(cmd->vk.cmdbuf, vertex_count, 1, first_vertex, 0);
}

void vk_cmd_drawinstanced(cmd_t* cmd, uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdDraw(cmd->vk.cmdbuf, vertex_count, instance_count, first_vertex, first_instance);
}

void vk_cmd_drawindexed(cmd_t* cmd, uint32_t index_count, uint32_t first_index, uint32_t first_vertex)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdDrawIndexed(cmd->vk.cmdbuf, index_count, 1, first_index, first_vertex, 0);
}

void vk_cmd_drawindexedinstanced(cmd_t* cmd, uint32_t index_count, uint32_t first_index, uint32_t instance_count, uint32_t first_instance, uint32_t first_vertex)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdDrawIndexed(cmd->vk.cmdbuf, index_count, instance_count, first_index, first_vertex, first_instance);
}

void vk_cmd_dispatch(cmd_t* cmd, uint32_t group_count_X, uint32_t group_count_Y, uint32_t group_count_Z)
{
	TC_ASSERT(cmd);
	TC_ASSERT(cmd->vk.cmdbuf != VK_NULL_HANDLE);
	vkCmdDispatch(cmd->vk.cmdbuf, group_count_X, group_count_Y, group_count_Z);
}

void vk_cmd_resourcebarrier(cmd_t* cmd, uint32_t numbufbarriers, bufbarrier_t* bufbarriers, uint32_t numtexbarriers, texbarrier_t* texbarriers, uint32_t numrtbarriers, rtbarrier_t* rtbarriers)
{
	VkImageMemoryBarrier* imagebarriers = (numtexbarriers + numrtbarriers) ? (VkImageMemoryBarrier*)alloca((numtexbarriers + numrtbarriers) * sizeof(VkImageMemoryBarrier)) : NULL;
	uint32_t imagebarriercount = 0;
	VkBufferMemoryBarrier* bufferbarriers = numbufbarriers ? (VkBufferMemoryBarrier*)alloca(numbufbarriers * sizeof(VkBufferMemoryBarrier)) : NULL;
	uint32_t bufferbarriercount = 0;
	VkAccessFlags srcaccessflags = 0;
	VkAccessFlags dstaccessflags = 0;
	for (uint32_t i = 0; i < numbufbarriers; i++) {
		bufbarrier_t* trans = &bufbarriers[i];
		buffer_t* buffer = trans->buf;
		VkBufferMemoryBarrier* barrier = NULL;
		if (RESOURCE_STATE_UNORDERED_ACCESS == trans->currentstate && RESOURCE_STATE_UNORDERED_ACCESS == trans->newstate) {
			barrier = &bufferbarriers[bufferbarriercount++];
			barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			barrier->pNext = NULL;
			barrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
		else {
			barrier = &bufferbarriers[bufferbarriercount++];
			barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			barrier->pNext = NULL;
			barrier->srcAccessMask = _to_vk_access_flags(trans->currentstate);
			barrier->dstAccessMask = _to_vk_access_flags(trans->newstate);
		}
		if (barrier) {
			barrier->buffer = buffer->vk.buffer;
			barrier->size = VK_WHOLE_SIZE;
			barrier->offset = 0;
			if (trans->acquire) {
				barrier->srcQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
				barrier->dstQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
			}
			else if (trans->release) {
				barrier->srcQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
				barrier->dstQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
			}
			else {
				barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}
			srcaccessflags |= barrier->srcAccessMask;
			dstaccessflags |= barrier->dstAccessMask;
		}
	}
	for (uint32_t i = 0; i < numtexbarriers; i++) {
		texbarrier_t* trans = &texbarriers[i];
		texture_t* tex = trans->tex;
		VkImageMemoryBarrier* barrier = NULL;
		if (RESOURCE_STATE_UNORDERED_ACCESS == trans->currentstate && RESOURCE_STATE_UNORDERED_ACCESS == trans->newstate) {
			barrier = &imagebarriers[imagebarriercount++];              //-V522
			barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;    //-V522
			barrier->pNext = NULL;
			barrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			barrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else {
			barrier = &imagebarriers[imagebarriercount++];
			barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier->pNext = NULL;
			barrier->srcAccessMask = _to_vk_access_flags(trans->currentstate);
			barrier->dstAccessMask = _to_vk_access_flags(trans->newstate);
			barrier->oldLayout = _to_vk_image_layout(trans->currentstate);
			barrier->newLayout = _to_vk_image_layout(trans->newstate);
		}
		if (barrier) {
			barrier->image = tex->vk.image;
			barrier->subresourceRange.aspectMask = (VkImageAspectFlags)tex->aspectmask;
			barrier->subresourceRange.baseMipLevel = trans->subresourcebarrier ? trans->miplevel : 0;
			barrier->subresourceRange.levelCount = trans->subresourcebarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			barrier->subresourceRange.baseArrayLayer = trans->subresourcebarrier ? trans->arraylayer : 0;
			barrier->subresourceRange.layerCount = trans->subresourcebarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;
			if (trans->acquire && trans->currentstate != RESOURCE_STATE_UNDEFINED) {
				barrier->srcQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
				barrier->dstQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
			}
			else if (trans->release && trans->currentstate != RESOURCE_STATE_UNDEFINED) {
				barrier->srcQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
				barrier->dstQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
			}
			else {
				barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}
			srcaccessflags |= barrier->srcAccessMask;
			dstaccessflags |= barrier->dstAccessMask;
		}
	}
	for (uint32_t i = 0; i < numrtbarriers; i++) {
		rtbarrier_t* trans = &rtbarriers[i];
		texture_t* tex = &trans->rt->tex;
		VkImageMemoryBarrier* barrier = NULL;
		if (trans->currentstate == RESOURCE_STATE_UNORDERED_ACCESS && trans->newstate == RESOURCE_STATE_UNORDERED_ACCESS) {
			barrier = &imagebarriers[imagebarriercount++];
			barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier->pNext = NULL;
			barrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			barrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else {
			barrier = &imagebarriers[imagebarriercount++];
			barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier->pNext = NULL;
			barrier->srcAccessMask = _to_vk_access_flags(trans->currentstate);
			barrier->dstAccessMask = _to_vk_access_flags(trans->newstate);
			barrier->oldLayout = _to_vk_image_layout(trans->currentstate);
			barrier->newLayout = _to_vk_image_layout(trans->newstate);
		}
		if (barrier) {
			barrier->image = tex->vk.image;
			barrier->subresourceRange.aspectMask = (VkImageAspectFlags)tex->aspectmask;
			barrier->subresourceRange.baseMipLevel = trans->subresourcebarrier ? trans->miplevel : 0;
			barrier->subresourceRange.levelCount = trans->subresourcebarrier ? 1 : VK_REMAINING_MIP_LEVELS;
			barrier->subresourceRange.baseArrayLayer = trans->subresourcebarrier ? trans->arraylayer : 0;
			barrier->subresourceRange.layerCount = trans->subresourcebarrier ? 1 : VK_REMAINING_ARRAY_LAYERS;
			if (trans->acquire && trans->currentstate != RESOURCE_STATE_UNDEFINED) {
				barrier->srcQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
				barrier->dstQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
			}
			else if (trans->release && trans->currentstate != RESOURCE_STATE_UNDEFINED) {
				barrier->srcQueueFamilyIndex = cmd->queue->vk.queuefamilyindex;
				barrier->dstQueueFamilyIndex = cmd->renderer->vk.queuefamilyindices[trans->queuetype];
			}
			else {
				barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}
			srcaccessflags |= barrier->srcAccessMask;
			dstaccessflags |= barrier->dstAccessMask;
		}
	}
	VkPipelineStageFlags srcStageMask =
		_determine_pipeline_stage_flags(cmd->renderer, srcaccessflags, (queuetype_t)cmd->vk.type);
	VkPipelineStageFlags dstStageMask =
		_determine_pipeline_stage_flags(cmd->renderer, dstaccessflags, (queuetype_t)cmd->vk.type);
	if (bufferbarriercount || imagebarriercount)
		vkCmdPipelineBarrier(cmd->vk.cmdbuf, srcStageMask, dstStageMask, 0, 0, NULL, bufferbarriercount, bufferbarriers, imagebarriercount, imagebarriers);
}

void vk_cmd_updatebuffer(cmd_t* cmd, buffer_t* buf, uint64_t dstoffset, buffer_t* srcbuf, uint64_t srcoffset, uint64_t size)
{
	TC_ASSERT(cmd);
	TC_ASSERT(srcbuf && buf);
	TC_ASSERT(srcbuf->vk.buffer);
	TC_ASSERT(buf->vk.buffer);
	TC_ASSERT(srcoffset + size <= srcbuf->size);
	TC_ASSERT(dstoffset + size <= buf->size);
	VkBufferCopy region = { 0 };
	region.srcOffset = srcoffset;
	region.dstOffset = dstoffset;
	region.size = (VkDeviceSize)size;
	vkCmdCopyBuffer(cmd->vk.cmdbuf, srcbuf->vk.buffer, buf->vk.buffer, 1, &region);
}

typedef struct subresourcedatadesc_s {
	uint64_t srcoffset;
	uint32_t miplevel;
	uint32_t arraylayer;
	uint32_t rowpitch;
	uint32_t slicepitch;
} subresourcedatadesc_t;

void vk_cmd_updatesubresource(cmd_t* cmd, texture_t* tex, buffer_t* srcbuf, const subresourcedatadesc_t* desc)
{
	const TinyImageFormat fmt = (TinyImageFormat)tex->format;
	if (TinyImageFormat_IsSinglePlane(fmt)) {
		const uint32_t width = max(1, tex->width >> desc->miplevel);
		const uint32_t height = max(1, tex->height >> desc->miplevel);
		const uint32_t depth = max(1, tex->depth >> desc->miplevel);
		const uint32_t blockw = desc->rowpitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t blockh = (desc->slicepitch / desc->rowpitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = desc->srcoffset;
		copy.bufferRowLength = blockw * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = blockh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)tex->aspectmask;
		copy.imageSubresource.mipLevel = desc->miplevel;
		copy.imageSubresource.baseArrayLayer = desc->arraylayer;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = depth;
		vkCmdCopyBufferToImage(cmd->vk.cmdbuf, srcbuf->vk.buffer, tex->vk.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}
	else {
		const uint32_t width = tex->width;
		const uint32_t height = tex->height;
		const uint32_t depth = tex->depth;
		const uint32_t numplanes = TinyImageFormat_NumOfPlanes(fmt);
		uint64_t offset = desc->srcoffset;
		VkBufferImageCopy buffercopies[MAX_PLANE_COUNT] = { 0 };
		for (uint32_t i = 0; i < numplanes; i++) {
			VkBufferImageCopy* copy = &buffercopies[i];
			copy->bufferOffset = offset;
			copy->imageSubresource.aspectMask = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);
			copy->imageSubresource.mipLevel = desc->miplevel;
			copy->imageSubresource.baseArrayLayer = desc->arraylayer;
			copy->imageSubresource.layerCount = 1;
			copy->imageExtent.width = TinyImageFormat_PlaneWidth(fmt, i, width);
			copy->imageExtent.height = TinyImageFormat_PlaneHeight(fmt, i, height);
			copy->imageExtent.depth = depth;
			offset += copy->imageExtent.width * copy->imageExtent.height * TinyImageFormat_PlaneSizeOfBlock(fmt, i);
		}
		vkCmdCopyBufferToImage(cmd->vk.cmdbuf, srcbuf->vk.buffer, tex->vk.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numplanes, buffercopies);
	}
}

void vk_cmd_copysubresource(cmd_t* cmd, buffer_t* dstbuf, texture_t* tex, const subresourcedatadesc_t* desc)
{
	const TinyImageFormat fmt = (TinyImageFormat)tex->format;
	if (TinyImageFormat_IsSinglePlane(fmt)) {
		const uint32_t width = max(1, tex->width >> desc->miplevel);
		const uint32_t height = max(1, tex->height >> desc->miplevel);
		const uint32_t depth = max(1, tex->depth >> desc->miplevel);
		const uint32_t blockw = desc->rowpitch / (TinyImageFormat_BitSizeOfBlock(fmt) >> 3);
		const uint32_t blockh = (desc->slicepitch / desc->rowpitch);

		VkBufferImageCopy copy = { 0 };
		copy.bufferOffset = desc->srcoffset;
		copy.bufferRowLength = blockw * TinyImageFormat_WidthOfBlock(fmt);
		copy.bufferImageHeight = blockh * TinyImageFormat_HeightOfBlock(fmt);
		copy.imageSubresource.aspectMask = (VkImageAspectFlags)tex->aspectmask;
		copy.imageSubresource.mipLevel = desc->miplevel;
		copy.imageSubresource.baseArrayLayer = desc->arraylayer;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = depth;
		vkCmdCopyImageToBuffer(cmd->vk.cmdbuf, tex->vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstbuf->vk.buffer, 1, &copy);
	}
	else {
		const uint32_t width = tex->width;
		const uint32_t height = tex->height;
		const uint32_t depth = tex->depth;
		const uint32_t planes = TinyImageFormat_NumOfPlanes(fmt);
		uint64_t offset = desc->srcoffset;
		VkBufferImageCopy buffercopies[MAX_PLANE_COUNT];
		for (uint32_t i = 0; i < planes; i++) {
			VkBufferImageCopy* copy = &buffercopies[i];
			copy->bufferOffset = offset;
			copy->bufferRowLength = 0;
			copy->bufferImageHeight = 0;
			copy->imageSubresource.aspectMask = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << i);
			copy->imageSubresource.mipLevel = desc->miplevel;
			copy->imageSubresource.baseArrayLayer = desc->arraylayer;
			copy->imageSubresource.layerCount = 1;
			copy->imageOffset.x = 0;
			copy->imageOffset.y = 0;
			copy->imageOffset.z = 0;
			copy->imageExtent.width = TinyImageFormat_PlaneWidth(fmt, i, width);
			copy->imageExtent.height = TinyImageFormat_PlaneHeight(fmt, i, height);
			copy->imageExtent.depth = depth;
			offset += copy->imageExtent.width * copy->imageExtent.height * TinyImageFormat_PlaneSizeOfBlock(fmt, i);
		}
		vkCmdCopyImageToBuffer(cmd->vk.cmdbuf, tex->vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstbuf->vk.buffer, planes, buffercopies);
	}
}

void vk_acquire_next_image(renderer_t* r, swapchain_t* swapchain, semaphore_t* signalsemaphore, fence_t* fence, uint32_t* imageidx)
{
	TC_ASSERT(r);
	TC_ASSERT(r->vk.device != VK_NULL_HANDLE);
	TC_ASSERT(signalsemaphore || fence);
    TC_ASSERT(VK_NULL_HANDLE != swapchain->vk.swapchain);
	VkResult res = { 0 };
	if (fence != NULL) {
		res = vkAcquireNextImageKHR(r->vk.device, swapchain->vk.swapchain, UINT64_MAX, VK_NULL_HANDLE, fence->vk.fence, imageidx);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {			// If swapchain is out of date, let caller know by setting image index to -1
			*imageidx = -1;
			vkResetFences(r->vk.device, 1, &fence->vk.fence);
			fence->vk.submitted = false;
			return;
		}
		fence->vk.submitted = true;
	}
	else {
		res = vkAcquireNextImageKHR(r->vk.device, swapchain->vk.swapchain, UINT64_MAX, signalsemaphore->vk.semaphore, VK_NULL_HANDLE, imageidx);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {			// If swapchain is out of date, let caller know by setting image index to -1
			*imageidx = -1;
			signalsemaphore->vk.signaled = false;
			return;
		}
		// Commonly returned immediately following swapchain resize. 
		// Vulkan spec states that this return value constitutes a successful call to vkAcquireNextImageKHR
		// https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAcquireNextImageKHR.html
		if (res == VK_SUBOPTIMAL_KHR) {
			TRACE(LOG_INFO, "vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR. If window was just resized, ignore this message."); 
			signalsemaphore->vk.signaled = true;
			return; 
		}
		CHECK_VKRESULT(res);
		signalsemaphore->vk.signaled = true;
	}
}

void vk_queue_submit(queue_t* queue, const queuesubmitdesc_t* desc)
{
	TC_ASSERT(queue && desc);
	uint32_t cmdcount = desc->cmdcount;
	cmd_t** cmds = desc->cmds;
	fence_t* fence = desc->signalfence;
	uint32_t waitsemaphorecount = desc->waitsemaphorecount;
	semaphore_t** waitsemaphores = desc->waitsemaphores;
	uint32_t signalsemaphorecount = desc->signalsemaphorecount;
	semaphore_t** signalsemaphores = desc->signalsemaphores;
	TC_ASSERT(cmdcount > 0);
	TC_ASSERT(cmds);
	if (waitsemaphorecount > 0) TC_ASSERT(waitsemaphores);
	if (signalsemaphorecount > 0) TC_ASSERT(signalsemaphores);
	TC_ASSERT(queue->vk.queue != VK_NULL_HANDLE);
	
	VkCommandBuffer* vcmds = (VkCommandBuffer*)alloca(cmdcount * sizeof(VkCommandBuffer));
	for (uint32_t i = 0; i < cmdcount; i++) {
		vcmds[i] = cmds[i]->vk.cmdbuf;
	}
	VkSemaphore* wait_semaphores = waitsemaphorecount ? (VkSemaphore*)alloca(waitsemaphorecount * sizeof(VkSemaphore)) : NULL;
	VkPipelineStageFlags* wait_masks = (VkPipelineStageFlags*)alloca(waitsemaphorecount * sizeof(VkPipelineStageFlags));
	uint32_t waitcount = 0;
	for (uint32_t i = 0; i < waitsemaphorecount; ++i) {
		if (waitsemaphores[i]->vk.signaled) {
			wait_semaphores[waitcount] = waitsemaphores[i]->vk.semaphore;
			wait_masks[waitcount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			waitcount++;
			waitsemaphores[i]->vk.signaled = false;
		}
	}
	VkSemaphore* signal_semaphores = signalsemaphorecount ? (VkSemaphore*)alloca(signalsemaphorecount * sizeof(VkSemaphore)) : NULL;
	uint32_t     signalcount = 0;
	for (uint32_t i = 0; i < signalsemaphorecount; i++) {
		if (!signalsemaphores[i]->vk.signaled) {
			signal_semaphores[signalcount] = signalsemaphores[i]->vk.semaphore;
			signalsemaphores[i]->vk.currentnodeidx = queue->nodeidx;
			signalsemaphores[i]->vk.signaled = true;
			signalcount++;
		}
	}
	VkSubmitInfo submit_info = { 0 };
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = waitcount;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_masks;
	submit_info.commandBufferCount = cmdcount;
	submit_info.pCommandBuffers = vcmds;
	submit_info.signalSemaphoreCount = signalcount;
	submit_info.pSignalSemaphores = signal_semaphores;

	VkDeviceGroupSubmitInfo deviceGroupSubmitInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO_KHR };
	if (queue->vk.gpumode == GPU_MODE_LINKED) {
		uint32_t* devicemasks = NULL;
		uint32_t* signalindices = NULL;
		uint32_t* waitindices = NULL;
		deviceGroupSubmitInfo.pNext = NULL;
		deviceGroupSubmitInfo.commandBufferCount = submit_info.commandBufferCount;
		deviceGroupSubmitInfo.signalSemaphoreCount = submit_info.signalSemaphoreCount;
		deviceGroupSubmitInfo.waitSemaphoreCount = submit_info.waitSemaphoreCount;
		devicemasks = (uint32_t*)alloca(deviceGroupSubmitInfo.commandBufferCount * sizeof(uint32_t));
		signalindices = (uint32_t*)alloca(deviceGroupSubmitInfo.signalSemaphoreCount * sizeof(uint32_t));
		waitindices = (uint32_t*)alloca(deviceGroupSubmitInfo.waitSemaphoreCount * sizeof(uint32_t));
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.commandBufferCount; ++i)
			devicemasks[i] = (1 << cmds[i]->vk.nodeidx);
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.signalSemaphoreCount; ++i)
			signalindices[i] = queue->nodeidx;
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.waitSemaphoreCount; ++i)
			waitindices[i] = waitsemaphores[i]->vk.currentnodeidx;

		deviceGroupSubmitInfo.pCommandBufferDeviceMasks = devicemasks;
		deviceGroupSubmitInfo.pSignalSemaphoreDeviceIndices= signalindices;
		deviceGroupSubmitInfo.pWaitSemaphoreDeviceIndices = waitindices;
		submit_info.pNext = &deviceGroupSubmitInfo;
	}
	// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
	// Many setups have just one queue family and one queue. In this case, async compute, async transfer doesn't exist and we end up using
	// the same queue for all three operations
	spin_lock(queue->vk.submitlck);
	{
		CHECK_VKRESULT(vkQueueSubmit(queue->vk.queue, 1, &submit_info, fence ? fence->vk.fence : VK_NULL_HANDLE));
		if (fence) fence->vk.submitted = true;
	}
	spin_unlock(queue->vk.submitlck);
}

void vk_queue_present(queue_t* queue, const queuepresentdesc_t* desc)
{
	TC_ASSERT(queue && desc);
	uint32_t waitsemaphorecount = desc->waitsemaphorecount;
	semaphore_t** waitsemaphores = desc->waitsemaphores;
	if (desc->swapchain) {
		swapchain_t* swapchain = desc->swapchain;
		if (waitsemaphorecount > 0) TC_ASSERT(waitsemaphores);
		TC_ASSERT(queue->vk.queue != VK_NULL_HANDLE);

		VkSemaphore* wait_semaphores = waitsemaphorecount ? (VkSemaphore*)alloca(waitsemaphorecount * sizeof(VkSemaphore)) : NULL;
		uint32_t waitcount = 0;
		for (uint32_t i = 0; i < waitsemaphorecount; ++i) {
			if (waitsemaphores[i]->vk.signaled) {
				wait_semaphores[waitcount] = waitsemaphores[i]->vk.semaphore;
				waitsemaphores[i]->vk.signaled = false;
				waitcount++;
			}
		}
		uint32_t presentidx = desc->index;
		VkPresentInfoKHR info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = waitcount;
		info.pWaitSemaphores = wait_semaphores;
		info.swapchainCount = 1;
		info.pSwapchains = &(swapchain->vk.swapchain);
		info.pImageIndices = &(presentidx);

		// Lightweight lock to make sure multiple threads dont use the same queue simultaneously
		spin_lock(queue->vk.submitlck);
		VkResult  res = vkQueuePresentKHR(swapchain->vk.presentqueue ? swapchain->vk.presentqueue : queue->vk.queue, &info);
		spin_unlock(queue->vk.submitlck);
		if (res == VK_ERROR_DEVICE_LOST) {	// Will crash normally on Android.
#if defined(_WINDOWS)
			// Wait for a few seconds to allow the driver to come back online before doing a reset.
#endif
		}
		else if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			// Fix bug where we get this error if window is closed before able to present queue.
		}
		else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) TC_ASSERT(0);
	}
}

void vk_wait_for_fences(renderer_t* r, uint32_t fencecount, fence_t** fences)
{
	TC_ASSERT(r && fences);
	VkFence* vfences = (VkFence*)alloca(fencecount * sizeof(VkFence));
	uint32_t numvalidfences = 0;
	for (uint32_t i = 0; i < fencecount; i++)
		if (fences[i]->vk.submitted)
			vfences[numvalidfences++] = fences[i]->vk.fence;
	if (numvalidfences) {
		CHECK_VKRESULT(vkWaitForFences(r->vk.device, numvalidfences, vfences, VK_TRUE, UINT64_MAX));
		CHECK_VKRESULT(vkResetFences(r->vk.device, numvalidfences, vfences));
	}
	for (uint32_t i = 0; i < fencecount; ++i) fences[i]->vk.submitted = false;
}

void vk_queue_wait_idle(queue_t* queue) { vkQueueWaitIdle(queue->vk.queue); }

void vk_get_fence_status(renderer_t* r, fence_t* fence, fencestatus_t* status)
{
	*status = FENCE_COMPLETE;
	if (fence->vk.submitted) {
		VkResult res = vkGetFenceStatus(r->vk.device, fence->vk.fence);
		if (res == VK_SUCCESS) {
			vkResetFences(r->vk.device, 1, &fence->vk.fence);
			fence->vk.submitted = false;
		}
		*status = res == VK_SUCCESS ? FENCE_COMPLETE : FENCE_INCOMPLETE;
	}
	else *status = FENCE_NOTSUBMITTED;
}

TinyImageFormat vk_recommended_swapchainfmt(bool hintHDR, bool hintSRGB)
{
#if !defined(VK_USE_PLATFORM_ANDROID_KHR) && !defined(VK_USE_PLATFORM_VI_NN)
	if (hintSRGB) return TinyImageFormat_B8G8R8A8_SRGB;
	else return TinyImageFormat_B8G8R8A8_UNORM;
#else
	if (hintSRGB) return TinyImageFormat_R8G8B8A8_SRGB;
	else return TinyImageFormat_R8G8B8A8_UNORM;
#endif
}

void vk_add_indirectcmdsignature(renderer_t* r, const cmdsignaturedesc_t* desc, cmdsignature_t** psignature)
{
	TC_ASSERT(r && desc && psignature);
	TC_ASSERT(desc->indirectargcount == 1);
	cmdsignature_t* signature = (cmdsignature_t*)tc_calloc(1, sizeof(cmdsignature_t) + sizeof(indirectarg_t) * desc->indirectargcount);
	TC_ASSERT(signature);
	signature->drawtype = desc->argdescs[0].type;
	switch (desc->argdescs[0].type) {
		case INDIRECT_DRAW:
			signature->stride += sizeof(indirectdrawargs_t);
			break;
		case INDIRECT_DRAW_INDEX:
			signature->stride += sizeof(indirectdrawindexargs_t);
			break;
		case INDIRECT_DISPATCH:
			signature->stride += sizeof(indirectdispatchargs_t);
			break;
		default: TC_ASSERT(false); break;
	}
	if (!desc->packed) signature->stride = round_up(signature->stride, 16);
	*psignature = signature;
}

void vk_remove_indirectcmdsignature(renderer_t* r, cmdsignature_t* signature)
{
	TC_ASSERT(r);
	tc_free(signature);
}

void vk_cmd_execindirect(cmd_t* cmd, cmdsignature_t* signature, uint32_t maxcmdcount, buffer_t* indirectbuf, uint64_t bufoffset, buffer_t* counterbuf, uint64_t counterbufoffset)
{
	PFN_vkCmdDrawIndirect draw_indirect_fn = (signature->drawtype == INDIRECT_DRAW) ? vkCmdDrawIndirect : vkCmdDrawIndexedIndirect;
	PFN_vkCmdDrawIndirectCountKHR draw_indirectcount_fn = (signature->drawtype == INDIRECT_DRAW) ? pfnVkCmdDrawIndirectCountKHR : pfnVkCmdDrawIndexedIndirectCountKHR;

	if (signature->drawtype == INDIRECT_DRAW || signature->drawtype == INDIRECT_DRAW_INDEX) {
		if (cmd->renderer->activegpusettings->multidrawindirect) {
			if (counterbuf && draw_indirectcount_fn)
				draw_indirectcount_fn(cmd->vk.cmdbuf, indirectbuf->vk.buffer, bufoffset, counterbuf->vk.buffer, counterbufoffset, maxcmdcount, signature->stride);
			else
				draw_indirect_fn(cmd->vk.cmdbuf, indirectbuf->vk.buffer, bufoffset, maxcmdcount, signature->stride);
		}
		else {
			// Cannot use counter buffer when MDI is not supported. We will blindly loop through maxCommandCount
			TC_ASSERT(!counterbuf);
			for (uint32_t i = 0; i < maxcmdcount; i++)
				draw_indirect_fn(cmd->vk.cmdbuf, indirectbuf->vk.buffer, bufoffset + i * signature->stride, 1, signature->stride);
		}
	}
	else if (signature->drawtype == INDIRECT_DISPATCH)
		for (uint32_t i = 0; i < maxcmdcount; i++)
			vkCmdDispatchIndirect(cmd->vk.cmdbuf, indirectbuf->vk.buffer, bufoffset + i * signature->stride);
}

VkQueryType _to_vk_query_type(querytype_t type)
{
	switch (type) {
		case QUERY_TYPE_TIMESTAMP: return VK_QUERY_TYPE_TIMESTAMP;
		case QUERY_TYPE_PIPELINE_STATS: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
		case QUERY_TYPE_OCCLUSION: return VK_QUERY_TYPE_OCCLUSION;
		default: TC_ASSERT(false && "Invalid query heap type"); return VK_QUERY_TYPE_MAX_ENUM;
	}
}

void vk_get_timestampfreq(queue_t* queue, double* freq)
{
	TC_ASSERT(queue);
	TC_ASSERT(freq);
	// The framework is using ticks per sec as frequency. Vulkan is nano sec per tick.
	// ns/tick number of nanoseconds required for a timestamp query to be incremented by 1
	// convert to ticks/sec (DX12 standard)
	*freq = 1.0f / ((double)queue->vk.timestampperiod * 1e-9);
}

void vk_add_querypool(renderer_t* r, const querypooldesc_t* desc, querypool_t* pool)
{
	TC_ASSERT(r && desc && pool);
	memset(pool, 0, sizeof(querypool_t));
	pool->vk.type = _to_vk_query_type(desc->type);
	pool->count = desc->querycount;

	VkQueryPoolCreateInfo info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryCount = desc->querycount;
	info.queryType = _to_vk_query_type(desc->type);
	CHECK_VKRESULT(vkCreateQueryPool(r->vk.device, &info, &alloccbs, &pool->vk.querypool));
}

void vk_remove_querypool(renderer_t* r, querypool_t* pool)
{
	TC_ASSERT(r && pool);
	vkDestroyQueryPool(r->vk.device, pool->vk.querypool, &alloccbs);
}

void vk_cmd_resetquerypool(cmd_t* cmd, querypool_t* pool, uint32_t startquery, uint32_t querycount)
{
	vkCmdResetQueryPool(cmd->vk.cmdbuf, pool->vk.querypool, startquery, querycount);
}

void vk_cmd_beginquery(cmd_t* cmd, querypool_t* pool, querydesc_t* query)
{
	switch (pool->vk.type) {
		case VK_QUERY_TYPE_TIMESTAMP:
			vkCmdWriteTimestamp(cmd->vk.cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool->vk.querypool, query->index);
			break;
		case VK_QUERY_TYPE_PIPELINE_STATISTICS: break;
		case VK_QUERY_TYPE_OCCLUSION: break;
		default: break;
	}
}

void vk_cmd_endquery(cmd_t* cmd, querypool_t* pool, querydesc_t* query)
{
	switch (pool->vk.type) {
	case VK_QUERY_TYPE_TIMESTAMP:
		vkCmdWriteTimestamp(cmd->vk.cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool->vk.querypool, query->index);
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS: break;
	case VK_QUERY_TYPE_OCCLUSION: break;
	default: break;
	}
}

void vk_cmd_resolvequery(cmd_t* cmd, querypool_t* pool, buffer_t* readbackbuf, uint32_t startquery, uint32_t querycount)
{
	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
	vkCmdCopyQueryPoolResults(cmd->vk.cmdbuf, pool->vk.querypool, startquery, querycount, readbackbuf->vk.buffer, 0, sizeof(uint64_t), flags);
}

void vk_get_memstats(renderer_t* r, char** stats)
{
	vmaBuildStatsString(r->vk.vmaAllocator, stats, VK_TRUE);
}

void vk_free_memstats(renderer_t* r, char* stats)
{
	vmaFreeStatsString(r->vk.vmaAllocator, stats);
}

void vk_get_memuse(renderer_t* r, uint64_t* usedBytes, uint64_t* totalAllocatedBytes)
{
	VmaTotalStatistics stats = { 0 };
	vmaCalculateStatistics(r->vk.vmaAllocator, &stats);
	*usedBytes = stats.total.statistics.allocationBytes;
	*totalAllocatedBytes = stats.total.statistics.blockBytes;
}

void vk_cmd_begindebugmark(cmd_t* cmd, float r, float g, float b, const char* name)
{
	if (cmd->renderer->vk.debugmarkersupport) {
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		info.color[0] = r;
		info.color[1] = g;
		info.color[2] = b;
		info.color[3] = 1.0f;
		info.pLabelName = name;
		vkCmdBeginDebugUtilsLabelEXT(cmd->vk.cmdbuf, &info);
#elif !defined(ENABLE_RENDER_DOC)
		VkDebugMarkerMarkerInfoEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		info.color[0] = r;
		info.color[1] = g;
		info.color[2] = b;
		info.color[3] = 1.0f;
		info.pMarkerName = name;
		vkCmdDebugMarkerBeginEXT(cmd->vk.cmdbuf, &info);
#endif
	}
}

void vk_cmd_enddebugmark(cmd_t* cmd)
{
	if (cmd->renderer->vk.debugmarkersupport) {
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		vkCmdEndDebugUtilsLabelEXT(cmd->vk.cmdbuf);
#elif !defined(ENABLE_RENDER_DOC)
		vkCmdDebugMarkerEndEXT(cmd->vk.cmdbuf);
#endif
	}
}

void vk_cmd_adddebugmark(cmd_t* cmd, float r, float g, float b, const char* name)
{
	if (cmd->renderer->vk.debugmarkersupport) {
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		info.color[0] = r;
		info.color[1] = g;
		info.color[2] = b;
		info.color[3] = 1.0f;
		info.pLabelName = name;
		vkCmdInsertDebugUtilsLabelEXT(cmd->vk.cmdbuf, &info);
#else
		VkDebugMarkerMarkerInfoEXT info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		info.color[0] = r;
		info.color[1] = g;
		info.color[2] = b;
		info.color[3] = 1.0f;
		info.pMarkerName = name;
		vkCmdDebugMarkerInsertEXT(cmd->vk.cmdbuf, &info);
#endif
	}
}

uint32_t vk_cmd_writemark(cmd_t* cmd, markertype_t type, uint32_t markval, buffer_t* buffer, size_t offset, bool useautoflags) { return 0; }

#ifdef ENABLE_DEBUG_UTILS_EXTENSION
void util_set_object_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name)
{
#if defined(ENABLE_GRAPHICS_DEBUG)
	VkDebugUtilsObjectNameInfoEXT info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objecttype = type;
	info.objectHandle = handle;
	info.pObjectName = name;
	vkSetDebugUtilsObjectNameEXT(device, &info);
#endif
}
#else
void util_set_object_name(VkDevice device, uint64_t handle, VkDebugReportObjectTypeEXT type, const char* name)
{
#if defined(ENABLE_GRAPHICS_DEBUG)
	VkDebugMarkerObjectNameInfoEXT info = { 0 };
	info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
	info.objecttype = type;
	info.object = (uint64_t)handle;
	info.pObjectName = name;
	vkDebugMarkerSetObjectNameEXT(device, &info);
#endif
}
#endif

void vk_set_buffername(renderer_t* r, buffer_t* buffer, const char* name)
{
	TC_ASSERT(r);
	TC_ASSERT(buffer);
	TC_ASSERT(name);
	if (r->vk.debugmarkersupport)
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)buffer->vk.buffer, VK_OBJECT_TYPE_BUFFER, name);
#else
		util_set_object_name(r->vk.device, (uint64_t)buffer->vk.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, name);
#endif
}

void vk_set_texturename(renderer_t* r, texture_t* tex, const char* name)
{
	TC_ASSERT(r);
	TC_ASSERT(tex);
	TC_ASSERT(name);
	if (r->vk.debugmarkersupport)
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)tex->vk.image, VK_OBJECT_TYPE_IMAGE, name);
#else
		util_set_object_name(r->vk.device, (uint64_t)tex->vk.image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, name);
#endif
}

void vk_set_rendertargetname(renderer_t* r, rendertarget_t* rt, const char* name)
{
	set_texturename(r, &rt->tex, name);
}

void vk_set_pipelinename(renderer_t* r, pipeline_t* pipeline, const char* name)
{
	TC_ASSERT(r);
	TC_ASSERT(pipeline);
	TC_ASSERT(name);
	if (r->vk.debugmarkersupport)
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
		util_set_object_name(r->vk.device, (uint64_t)pipeline->vk.pipeline, VK_OBJECT_TYPE_PIPELINE, name);
#else
		util_set_object_name(r->vk.device, (uint64_t)pipeline->vk.pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, name);
#endif
}

// Allocate Vulkan memory for the virtual page
static bool allocate_virtualpage(renderer_t* r, texture_t* tex, vtpage_t* page, buffer_t** intermediatebuf)
{
	if (page->vk.imagemembind.memory != VK_NULL_HANDLE)
		return false;

	bufferdesc_t desc = { 0 };
	desc.descriptors = DESCRIPTOR_TYPE_RW_BUFFER;
	desc.memusage = RESOURCE_MEMORY_USAGE_CPU_ONLY;
	desc.count = tex->vt->sparsevtpagewidth * tex->vt->sparsevtpageheight;
	desc.stride = sizeof(uint32_t);
	desc.size = desc.count * desc.stride;
#if defined(ENABLE_GRAPHICS_DEBUG)
	char debugNameBuffer[MAX_DEBUG_NAME_LENGTH]{ 0 };
	snprintf(debugNameBuffer, MAX_DEBUG_NAME_LENGTH, "(tex %p) VT page #%u intermediate buffer", pTexture, virtualPage.index);
	desc.pName = debugNameBuffer;
#endif
	*intermediatebuf = (buffer_t*)tc_calloc(1, sizeof(buffer_t));
	add_buffer(r, &desc, *intermediatebuf);

	VkMemoryRequirements reqs = { 0 };
	reqs.size = page->vk.size;
	reqs.memoryTypeBits = tex->vt->vk.sparsememtypebits;
	reqs.alignment = reqs.size;

	VmaAllocationCreateInfo info = { 0 };
	info.pool = (VmaPool)tex->vt->vk.pool;
	VmaAllocation allocation;
	VmaAllocationInfo alloc_info;
	CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &reqs, &info, &allocation, &alloc_info));
	page->vk.alloc = allocation;
	page->vk.imagemembind.memory = alloc_info.deviceMemory;
	page->vk.imagemembind.memoryOffset = alloc_info.offset;

	// Sparse image memory binding
	page->vk.imagemembind.subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	page->vk.imagemembind.subresource.mipLevel = page->miplevel;
	page->vk.imagemembind.subresource.arrayLayer = page->layer;
	tex->vt->virtualpagealive++;
	return true;
}

vtpage_t* add_page(renderer_t* r, texture_t* tex, const VkOffset3D* offset, const VkExtent3D* extent, const VkDeviceSize size, const uint32_t miplevel, uint32_t layer, uint32_t pageidx)
{
	vtpage_t* page = &tex->vt->pages[pageidx];
	page->vk.imagemembind.offset = *offset;
	page->vk.imagemembind.extent = *extent;
	page->vk.size = size;
	page->miplevel = miplevel;
	page->layer = layer;
	page->index = pageidx;
	return page;
}

static void aligned_division(const VkExtent3D* extent, const VkExtent3D* granularity, VkExtent3D* out)
{
	out->width = (extent->width / granularity->width + ((extent->width % granularity->width) ? 1u : 0u));
	out->height = (extent->height / granularity->height + ((extent->height % granularity->height) ? 1u : 0u));
	out->depth = (extent->depth / granularity->depth + ((extent->depth % granularity->depth) ? 1u : 0u));
}

typedef struct {
	VmaAllocation* allocations;
	uint32_t* allocationcount;
	buffer_t** intermediatebufs;
	uint32_t* intermediatebufcount;
} pendingpagedeletion_t;

typedef struct {
	uint32_t* alivepagecount;
	uint32_t* removepagecount;
	uint32_t* alivepages;
	uint32_t* removepages;
	uint32_t totalsize;
} readbackbufoffsets_t;

static readbackbufoffsets_t vt_get_readbackbufoffsets(uint32_t* buf, uint32_t bufsize, uint32_t pagecount, uint32_t currentimage)
{
	readbackbufoffsets_t offsets;
	offsets.alivepagecount = buf + ((bufsize / sizeof(uint32_t)) * currentimage);
	offsets.removepagecount = offsets.alivepagecount + 1;
	offsets.alivepages = offsets.removepagecount + 1;
	offsets.removepages = offsets.alivepages + pagecount;
	offsets.totalsize = (uint32_t)((offsets.removepages - offsets.alivepages) + pagecount) * sizeof(uint32_t);
	return offsets;
}
static uint32_t vt_get_readbackbufsize(uint32_t pagecount, uint32_t numimages)
{
	return vt_get_readbackbufoffsets(NULL, 0, pagecount, 0).totalsize;
}
static pendingpagedeletion_t vt_get_pendingpagedeletion(virtualtexture_t* vt, uint32_t currentimage)
{
	if (vt->pendingdeletions <= currentimage) {								// Grow arrays
		const uint32_t old = vt->pendingdeletions;
		vt->pendingdeletions = currentimage + 1;
		vt->vk.pendingdeletedallocs = (void**)tc_realloc(vt->vk.pendingdeletedallocs, vt->pendingdeletions * vt->virtualpages * sizeof(vt->vk.pendingdeletedallocs[0]));
		vt->pendingdeletedalloccount = (uint32_t*)tc_realloc(vt->pendingdeletedalloccount, vt->pendingdeletions * sizeof(vt->pendingdeletedalloccount[0]));
		vt->pendingdeletedbufs = (buffer_t**)tc_realloc(vt->pendingdeletedbufs, vt->pendingdeletions * vt->virtualpages * sizeof(vt->pendingdeletedbufs[0]));
		vt->pendingdeletedbufcount = (uint32_t*)tc_realloc(vt->pendingdeletedbufcount, vt->pendingdeletions * sizeof(vt->pendingdeletedbufcount[0]));
		for (uint32_t i = old; i < vt->pendingdeletions; i++) {	// Zero the new counts
			vt->pendingdeletedalloccount[i] = 0;
			vt->pendingdeletedbufcount[i] = 0;
		}
	}
	pendingpagedeletion_t pending_deletion;
	pending_deletion.allocations = (VmaAllocation*)&vt->vk.pendingdeletedallocs[currentimage * vt->virtualpages];
	pending_deletion.allocationcount = &vt->pendingdeletedalloccount[currentimage];
	pending_deletion.intermediatebufs = &vt->pendingdeletedbufs[currentimage * vt->virtualpages];
	pending_deletion.intermediatebufcount = &vt->pendingdeletedbufcount[currentimage];
	return pending_deletion;
}

void vk_update_memory(cmd_t* cmd, texture_t* tex, uint32_t n)
{
	if (n > 0) {															// Update sparse bind info
		VkBindSparseInfo info = { 0 };
		info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;

		VkSparseImageMemoryBindInfo imageMemoryBindInfo = { 0 };			// Image memory binds
		imageMemoryBindInfo.image = tex->vk.image;
		imageMemoryBindInfo.bindCount = n;
		imageMemoryBindInfo.pBinds = tex->vt->vk.sparseimagemembinds;
		info.imageBindCount = 1;
		info.pImageBinds = &imageMemoryBindInfo;
		
		VkSparseImageOpaqueMemoryBindInfo opaqueMemoryBindInfo = { 0 };		// Opaque image memory binds (mip tail)
		opaqueMemoryBindInfo.image = tex->vk.image;
		opaqueMemoryBindInfo.bindCount = tex->vt->vk.opaquemembindscount;
		opaqueMemoryBindInfo.pBinds = tex->vt->vk.opaquemembinds;
		info.imageOpaqueBindCount = (opaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		info.pImageOpaqueBinds = &opaqueMemoryBindInfo;
		CHECK_VKRESULT(vkQueueBindSparse(cmd->queue->vk.queue, (uint32_t)1, &info, VK_NULL_HANDLE));
	}
}

void vk_release_page(cmd_t* cmd, texture_t* tex, uint32_t currentimage)
{
	renderer_t* r = cmd->renderer;
	vtpage_t* table = tex->vt->pages;
	readbackbufoffsets_t offsets = vt_get_readbackbufoffsets(
		(uint32_t*)tex->vt->readbackbuf->data,
		tex->vt->readbackbufsize,
		tex->vt->virtualpages,
		currentimage
	);
	const uint32_t removepagecount = *offsets.removepagecount;
	const uint32_t* removetable = offsets.removepages;
	const pendingpagedeletion_t pending_deletion = vt_get_pendingpagedeletion(tex->vt, currentimage);

	// Release pending intermediate buffers
	for (size_t i = 0; i < *pending_deletion.intermediatebufcount; i++)
		remove_buffer(r, pending_deletion.intermediatebufs[i]);
	for (size_t i = 0; i < *pending_deletion.allocationcount; i++)
		vmaFreeMemory(r->vk.vmaAllocator, pending_deletion.allocations[i]);
	*pending_deletion.intermediatebufcount = 0;
	*pending_deletion.allocationcount = 0;

	// Schedule release of newly unneeded pages
	uint32_t pageunbindcount = 0;
	for (uint32_t i = 0; i < removepagecount; i++) {
		uint32_t removeidx = removetable[i];
		vtpage_t* page = &table[removeidx];
		// Never remove the lowest mip level
		if ((int)page->miplevel >= (tex->vt->tiledmiplevels - 1))
			continue;

		TC_ASSERT(!!page->vk.alloc == !!page->vk.imagemembind.memory);
		if (page->vk.alloc) {
			pending_deletion.allocations[(*pending_deletion.allocationcount)++] = (VmaAllocation)page->vk.alloc;
			page->vk.alloc = VK_NULL_HANDLE;
			page->vk.imagemembind.memory = VK_NULL_HANDLE;

			VkSparseImageMemoryBind* unbind = &tex->vt->vk.sparseimagemembinds[pageunbindcount++];
			memset(unbind, 0, sizeof(VkSparseImageMemoryBind));
			unbind->offset = page->vk.imagemembind.offset;
			unbind->extent = page->vk.imagemembind.extent;
			unbind->subresource = page->vk.imagemembind.subresource;
			tex->vt->virtualpagealive--;
		}
	}
	vk_update_memory(cmd, tex, pageunbindcount);	// Unmap tiles
}

void vk_upload_page(cmd_t* cmd, texture_t* tex, vtpage_t* page, uint32_t* n, uint32_t currentimage)
{
	buffer_t* intermediatebuf = NULL;
	if (allocate_virtualpage(cmd->renderer, tex, page, &intermediatebuf)) {
		void* data = (void*)((unsigned char*)tex->vt->data + (page->index * page->vk.size));
		const bool intermediatemap = !intermediatebuf->data;
		if (intermediatemap)
			map_buffer(cmd->renderer, intermediatebuf, NULL);
		//CPU to GPU
		memcpy(intermediatebuf->data, data, page->vk.size);
		if (intermediatemap)
			unmap_buffer(cmd->renderer, intermediatebuf);

		//Copy image to VkImage
		VkBufferImageCopy region = { 0 };
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.mipLevel = page->miplevel;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = page->vk.imagemembind.offset;
		region.imageOffset.z = 0;
		region.imageExtent = (VkExtent3D){ (uint32_t)tex->vt->sparsevtpagewidth, (uint32_t)tex->vt->sparsevtpageheight, 1 };
		vkCmdCopyBufferToImage(cmd->vk.cmdbuf, intermediatebuf->vk.buffer, tex->vk.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		// Update list of memory-backed sparse image memory binds
		tex->vt->vk.sparseimagemembinds[(*n)++] = page->vk.imagemembind;
		// Schedule deletion of this intermediate buffer
		const pendingpagedeletion_t pending_deletion = vt_get_pendingpagedeletion(tex->vt, currentimage);
		pending_deletion.intermediatebufs[(*pending_deletion.intermediatebufcount)++] = intermediatebuf;
	}
}

// Fill a complete mip level
// Need to get visibility info first then fill them
void vk_fill_virtualtexture(cmd_t* cmd, texture_t* tex, fence_t* fence, uint32_t currentimage)
{
	uint32_t n = 0;
	readbackbufoffsets_t readback_offsets = vt_get_readbackbufoffsets(
		(uint32_t*)tex->vt->readbackbuf->data, tex->vt->readbackbufsize,
		tex->vt->virtualpages, currentimage);

	const uint32_t alivepagecount = *readback_offsets.alivepagecount;
	uint32_t* visibility = readback_offsets.alivepages;
	for (int i = 0; i < (int)alivepagecount; i++) {
		uint32_t pageidx = visibility[i];
		TC_ASSERT(pageidx < tex->vt->virtualpages);
		vtpage_t* page = &tex->vt->pages[pageidx];
		TC_ASSERT(pageidx == page->index);
		vk_upload_page(cmd, tex, page, &n, currentimage);
	}
	vk_update_memory(cmd, tex, n);
}

// Fill specific mipLevel
void vk_fill_virtualtexturelevel(cmd_t* cmd, texture_t* tex, uint32_t miplevel, uint32_t currentimage)
{
	uint32_t n = 0;		//Bind data
	for (uint32_t i = 0; i < tex->vt->virtualpages; i++) {
		vtpage_t* page = &tex->vt->pages[i];
		TC_ASSERT(page->index == i);
		if (page->miplevel == miplevel)
			vk_upload_page(cmd, tex, page, &n, currentimage);
	}
	vk_update_memory(cmd, tex, n);
}

void vk_add_virtualtexture(cmd_t* cmd, const texturedesc_t* desc, texture_t* tex, void* data)
{
	TC_ASSERT(cmd && tex);
	tex->vt = (virtualtexture_t*)tc_calloc(1, sizeof(virtualtexture_t));
	TC_ASSERT(tex->vt);
	renderer_t* r = cmd->renderer;
	uint32_t imagesize = 0;
	uint32_t mipsize = desc->width * desc->height * desc->depth;
	while (mipsize > 0)	{
		imagesize += mipsize;
		mipsize /= 4;
	}
	tex->vt->data = data;
	tex->format = desc->format;
	VkFormat format = (VkFormat)TinyImageFormat_ToVkFormat((TinyImageFormat)tex->format);
	TC_ASSERT(format != VK_FORMAT_UNDEFINED);
	tex->ownsimage = true;

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
	CHECK_VKRESULT(vkCreateImage(r->vk.device, &info, &alloccbs, &tex->vk.image));

	// Get memory requirements
	VkMemoryRequirements sparseimagereqs;
	vkGetImageMemoryRequirements(r->vk.device, tex->vk.image, &sparseimagereqs);
	// Check requested image size against hardware sparse limit
	if (sparseimagereqs.size > r->vk.activegpuprops->properties.limits.sparseAddressSpaceSize) {
		TRACE(LOG_ERROR, "Requested sparse image size exceeds supported sparse address space size!");
		return;
	}
	// Get sparse memory requirements
	// Count
	uint32_t memreqscount;
	vkGetImageSparseMemoryRequirements(r->vk.device, tex->vk.image, &memreqscount, NULL);  // Get count
	VkSparseImageMemoryRequirements* sparsememreqs = NULL;
	if (memreqscount == 0) {
		TRACE(LOG_ERROR, "No memory requirements for the sparse image!");
		return;
	}
	else {
		sparsememreqs = (VkSparseImageMemoryRequirements*)tc_calloc(memreqscount, sizeof(VkSparseImageMemoryRequirements));
		vkGetImageSparseMemoryRequirements(r->vk.device, tex->vk.image, &memreqscount, sparsememreqs);  // Get reqs
	}
	TC_ASSERT(memreqscount == 1 && "Multiple sparse image memory requirements not currently implemented");
	tex->vt->sparsevtpagewidth = sparsememreqs[0].formatProperties.imageGranularity.width;
	tex->vt->sparsevtpageheight = sparsememreqs[0].formatProperties.imageGranularity.height;
	tex->vt->virtualpages = imagesize / (uint32_t)(tex->vt->sparsevtpagewidth * tex->vt->sparsevtpageheight);
	tex->vt->readbackbufsize = vt_get_readbackbufsize(tex->vt->virtualpages, 1);
	tex->vt->pagevisibilitybufsize = tex->vt->virtualpages * 2 * sizeof(uint32_t);

	uint32_t tiledmiplevel = desc->miplevels - (uint32_t)log2(min((uint32_t)tex->vt->sparsevtpagewidth, (uint32_t)tex->vt->sparsevtpageheight));
	tex->vt->tiledmiplevels = (uint8_t)tiledmiplevel;
	TRACE(LOG_INFO, "Sparse image memory requirements: %d", memreqscount);

	// Get sparse image requirements for the color aspect
	VkSparseImageMemoryRequirements sparseMemoryReq = { 0 };
	bool found = false;
	for (int i = 0; i < (int)memreqscount; i++) {
		VkSparseImageMemoryRequirements reqs = sparsememreqs[i];
		if (reqs.formatProperties.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
			sparseMemoryReq = reqs;
			found = true;
			break;
		}
	}
	tc_free(sparsememreqs);
	sparsememreqs = NULL;
	if (!found) {
		TRACE(LOG_ERROR, "Could not find sparse image memory requirements for color aspect bit!");
		return;
	}
	VkPhysicalDeviceMemoryProperties props = { 0 };
	vkGetPhysicalDeviceMemoryProperties(r->vk.activegpu, &props);

	// Calculate number of required sparse memory bindings by alignment
	TC_ASSERT((sparseimagereqs.size % sparseimagereqs.alignment) == 0);
	tex->vt->vk.sparsememtypebits = sparseimagereqs.memoryTypeBits;

	// Check if the format has a single mip tail for all layers or one mip tail for each layer
	// The mip tail contains all mip levels > sparseMemoryReq.imageMipTailFirstLod
	bool singlemiptail = sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;
	tex->vt->pages = (vtpage_t*)tc_calloc(tex->vt->virtualpages, sizeof(vtpage_t));
	tex->vt->vk.sparseimagemembinds = (VkSparseImageMemoryBind*)tc_calloc(tex->vt->virtualpages, sizeof(VkSparseImageMemoryBind));
	tex->vt->vk.opaquemembindalloc = (void**)tc_calloc(tex->vt->virtualpages, sizeof(VmaAllocation));
	tex->vt->vk.opaquemembinds = (VkSparseMemoryBind*)tc_calloc(tex->vt->virtualpages, sizeof(VkSparseMemoryBind));
	VmaPoolCreateInfo pool_info = { 0 };
	pool_info.memoryTypeIndex = _get_memory_type(sparseimagereqs.memoryTypeBits, &props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CHECK_VKRESULT(vmaCreatePool(r->vk.vmaAllocator, &pool_info, (VmaPool*)&tex->vt->vk.pool));

	uint32_t idx = 0;
	// Sparse bindings for each mip level of all layers outside of the mip tail
	for (uint32_t layer = 0; layer < 1; layer++) {
		// sparseMemoryReq.imageMipTailFirstLod is the first mip level that's stored inside the mip tail
		for (uint32_t mip = 0; mip < tiledmiplevel; mip++) {
			VkExtent3D extent;
			extent.width = max(info.extent.width >> mip, 1u);
			extent.height = max(info.extent.height >> mip, 1u);
			extent.depth = max(info.extent.depth >> mip, 1u);
			VkImageSubresource subresource = { 0 };
			subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresource.mipLevel = mip;
			subresource.arrayLayer = layer;

			// Aligned sizes by image granularity
			VkExtent3D granularity = sparseMemoryReq.formatProperties.imageGranularity;
			VkExtent3D sparseBindCounts = { 0 };
			VkExtent3D lastBlockExtent = { 0 };
			aligned_division(&extent, &granularity, &sparseBindCounts);
			lastBlockExtent.width =
				((extent.width % granularity.width) ? extent.width % granularity.width : granularity.width);
			lastBlockExtent.height =
				((extent.height % granularity.height) ? extent.height % granularity.height : granularity.height);
			lastBlockExtent.depth =
				((extent.depth % granularity.depth) ? extent.depth % granularity.depth : granularity.depth);

			// Allocate memory for some blocks
			for (uint32_t z = 0; z < sparseBindCounts.depth; z++) {
				for (uint32_t y = 0; y < sparseBindCounts.height; y++) {
					for (uint32_t x = 0; x < sparseBindCounts.width; x++) {
						// Offset
						VkOffset3D offset;
						offset.x = x * granularity.width;
						offset.y = y * granularity.height;
						offset.z = z * granularity.depth;
						// Size of the page
						VkExtent3D extent;
						extent.width = (x == sparseBindCounts.width - 1) ? lastBlockExtent.width : granularity.width;
						extent.height = (y == sparseBindCounts.height - 1) ? lastBlockExtent.height : granularity.height;
						extent.depth = (z == sparseBindCounts.depth - 1) ? lastBlockExtent.depth : granularity.depth;
						// Add new virtual page
						vtpage_t* new_page = add_page(r, tex, &offset, &extent, tex->vt->sparsevtpagewidth * tex->vt->sparsevtpageheight * sizeof(uint32_t), mip, layer, idx);
						idx++;
						new_page->vk.imagemembind.subresource = subresource;
					}
				}
			}
		}
		// Check if format has one mip tail per layer
		if ((!singlemiptail) && (sparseMemoryReq.imageMipTailFirstLod < desc->miplevels)) {
			// Allocate memory for the mip tail
			VkMemoryRequirements reqs = { 0 };
			reqs.size = sparseMemoryReq.imageMipTailSize;
			reqs.memoryTypeBits = tex->vt->vk.sparsememtypebits;
			reqs.alignment = reqs.size;
			VmaAllocationCreateInfo allocCreateInfo = { 0 };
			allocCreateInfo.memoryTypeBits = reqs.memoryTypeBits;
			allocCreateInfo.pool = (VmaPool)tex->vt->vk.pool;

			VmaAllocation allocation;
			VmaAllocationInfo alloc_info;
			CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &reqs, &allocCreateInfo, &allocation, &alloc_info));

			VkSparseMemoryBind bind = { 0 };	// (Opaque) sparse memory binding
			bind.resourceOffset = sparseMemoryReq.imageMipTailOffset + layer * sparseMemoryReq.imageMipTailStride;
			bind.size = sparseMemoryReq.imageMipTailSize;
			bind.memory = alloc_info.deviceMemory;
			bind.memoryOffset = alloc_info.offset;
			tex->vt->vk.opaquemembindalloc[tex->vt->vk.opaquemembindscount] = allocation;
			tex->vt->vk.opaquemembinds[tex->vt->vk.opaquemembindscount] = bind;
			tex->vt->vk.opaquemembindscount++;
		}
	}
	TRACE(LOG_INFO, "Virtual Texture info: Dim %d x %d Pages %d", desc->width, desc->height, tex->vt->virtualpages);
	// Check if format has one mip tail for all layers
	if ((sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) &&
		(sparseMemoryReq.imageMipTailFirstLod < desc->miplevels)) {
		// Allocate memory for the mip tail
		VkMemoryRequirements reqs = { 0 };
		reqs.size = sparseMemoryReq.imageMipTailSize;
		reqs.memoryTypeBits = tex->vt->vk.sparsememtypebits;
		reqs.alignment = reqs.size;

		VmaAllocationCreateInfo allocCreateInfo = { 0 };
		allocCreateInfo.memoryTypeBits = reqs.memoryTypeBits;
		allocCreateInfo.pool = (VmaPool)tex->vt->vk.pool;

		VmaAllocation allocation;
		VmaAllocationInfo alloc_info;
		CHECK_VKRESULT(vmaAllocateMemory(r->vk.vmaAllocator, &reqs, &allocCreateInfo, &allocation, &alloc_info));

		// (Opaque) sparse memory binding
		VkSparseMemoryBind bind = { 0 };
		bind.resourceOffset = sparseMemoryReq.imageMipTailOffset;
		bind.size = sparseMemoryReq.imageMipTailSize;
		bind.memory = alloc_info.deviceMemory;
		bind.memoryOffset = alloc_info.offset;

		tex->vt->vk.opaquemembindalloc[tex->vt->vk.opaquemembindscount] = allocation;
		tex->vt->vk.opaquemembinds[tex->vt->vk.opaquemembindscount] = bind;
		tex->vt->vk.opaquemembindscount++;
	}
	VkImageViewCreateInfo view = { 0 };
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.components = (VkComponentMapping){ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = desc->miplevels;
	view.image = tex->vk.image;
	tex->aspectmask = VK_IMAGE_ASPECT_COLOR_BIT;
	CHECK_VKRESULT(vkCreateImageView(r->vk.device, &view, &alloccbs, &tex->vk.SRVdescriptor));

	texbarrier_t barriers[] = { { tex, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST } };
	cmd_resourcebarrier(cmd, 0, NULL, 1, barriers, 0, NULL);
	// Fill smallest (non-tail) mip map level
	vk_fill_virtualtexturelevel(cmd, tex, tiledmiplevel - 1, 0);
	tex->ownsimage = true;
	tex->nodeidx = desc->nodeidx;
	tex->miplevels = desc->miplevels;
	tex->width = desc->width;
	tex->height = desc->height;
	tex->depth = desc->depth;
#if defined(ENABLE_GRAPHICS_DEBUG)
	if (desc->name) set_texturename(r, tex, desc->name);
#endif
}

void vk_remove_virtualtexture(renderer_t* r, virtualtexture_t* vt)
{
	for (int i = 0; i < (int)vt->virtualpages; i++) {
		vtpage_t* page = &vt->pages[i];
		if (page->vk.alloc) vmaFreeMemory(r->vk.vmaAllocator, (VmaAllocation)page->vk.alloc);
	}
	tc_free(vt->pages);
	for (int i = 0; i < (int)vt->vk.opaquemembindscount; i++)
		vmaFreeMemory(r->vk.vmaAllocator, (VmaAllocation)vt->vk.opaquemembindalloc[i]);
	tc_free(vt->vk.opaquemembinds);
	tc_free(vt->vk.opaquemembindalloc);
	tc_free(vt->vk.sparseimagemembinds);

	for (uint32_t i = 0; i < vt->pendingdeletions; i++) {
		const pendingpagedeletion_t pending_deletion = vt_get_pendingpagedeletion(vt, i);
		for (uint32_t j = 0; j < *pending_deletion.allocationcount; j++)
			vmaFreeMemory(r->vk.vmaAllocator, pending_deletion.allocations[j]);
		for (uint32_t j = 0; j < *pending_deletion.intermediatebufcount; j++)
			remove_buffer(r, pending_deletion.intermediatebufs[j]);
	}
	tc_free(vt->vk.pendingdeletedallocs);
	tc_free(vt->pendingdeletedalloccount);
	tc_free(vt->pendingdeletedbufs);
	tc_free(vt->pendingdeletedbufcount);
	tc_free(vt->data);
	vmaDestroyPool(r->vk.vmaAllocator, (VmaPool)vt->vk.pool);
}

void vk_cmd_updatevirtualtexture(cmd_t* cmd, texture_t* tex, uint32_t currentimage)
{
	TC_ASSERT(tex->vt->readbackbuf);
	const bool map = !tex->vt->readbackbuf->data;
	if (map) map_buffer(cmd->renderer, tex->vt->readbackbuf, NULL);
	vk_release_page(cmd, tex, currentimage);
	vk_fill_virtualtexture(cmd, tex, NULL, currentimage);
	if (map) unmap_buffer(cmd->renderer, tex->vt->readbackbuf);
}

void init_vulkanrenderer(const char* app_name, const rendererdesc_t* desc, renderer_t* r)
{
	// API functions
	add_fence = vk_add_fence;
	remove_fence = vk_remove_fence;
	add_semaphore = vk_add_semaphore;
	remove_semaphore = vk_remove_semaphore;
	add_queue = vk_add_queue;
	remove_queue = vk_remove_queue;
	add_swapchain = vk_add_swapchain;
	remove_swapchain = vk_remove_swapchain;

	// Command pool functions
	add_cmdpool = vk_add_cmdpool;
	remove_cmdpool = vk_remove_cmdpool;
	add_cmds = vk_add_cmds;
	remove_cmds = vk_remove_cmds;

	add_rendertarget = vk_add_rendertarget;
	remove_rendertarget = vk_remove_rendertarget;
	add_sampler = vk_add_sampler;
	remove_sampler = vk_remove_sampler;

	// Resource Load functions
	add_buffer = vk_add_buffer;
	remove_buffer = vk_remove_buffer;
	map_buffer = vk_map_buffer;
	unmap_buffer = vk_unmap_buffer;
	cmd_updatebuffer = vk_cmd_updatebuffer;
	cmd_updatesubresource = vk_cmd_updatesubresource;
	cmd_copysubresource = vk_cmd_copysubresource;
	add_texture = vk_add_texture;
	remove_texture = vk_remove_texture;
	add_virtualtexture = vk_add_virtualtexture;
	remove_virtualtexture = vk_remove_virtualtexture;

	// Shader functions
	add_shaderbinary = vk_add_shaderbinary;
	remove_shader = vk_remove_shader;

	add_rootsignature = vk_add_rootsignature;
	remove_rootsignature = vk_remove_rootsignature;

	// Pipeline functions
	add_pipeline = vk_add_pipeline;
	remove_pipeline = vk_remove_pipeline;
	add_pipelinecache = vk_add_pipelinecache;
	get_pipelinecachedata = vk_get_pipelinecachedata;
	remove_pipelinecache = vk_remove_pipelinecache;

	// Descriptor Set functions
	add_descriptorset = vk_add_descriptorset;
	remove_descriptorset = vk_remove_descriptorset;
	update_descriptorset = vk_update_descriptorset;

	// Command buffer functions
	reset_cmdpool = vk_reset_cmdpool;
	cmd_begin = vk_cmd_begin;
	cmd_end = vk_cmd_end;
	cmd_bindrendertargets = vk_cmd_bindrendertargets;
	cmd_setshadingrate = vk_cmd_setshadingrate;
	cmd_setviewport = vk_cmd_setviewport;
	cmd_setscissor = vk_cmd_setscissor;
	cmd_setstencilreferenceval = vk_cmd_setstencilreferenceval;
	cmd_bindpipeline = vk_cmd_bindpipeline;
	cmd_binddescset = vk_cmd_binddescset;
	cmd_bindpushconstants = vk_cmd_bindpushconstants;
	cmd_binddescsetwithrootcbvs = vk_cmd_binddescsetwithrootcbvs;
	cmd_bindindexbuffer = vk_cmd_bindindexbuffer;
	cmd_bindvertexbuffer = vk_cmd_bindvertexbuffer;
	cmd_draw = vk_cmd_draw;
	cmd_drawinstanced = vk_cmd_drawinstanced;
	cmd_drawindexed = vk_cmd_drawindexed;
	cmd_drawindexedinstanced = vk_cmd_drawindexedinstanced;
	cmd_dispatch = vk_cmd_dispatch;

	// Transition Commands
	cmd_resourcebarrier = vk_cmd_resourcebarrier;
	// Virtual Textures
	cmd_updatevirtualtexture = vk_cmd_updatevirtualtexture;

	// queue/fence/swapchain functions
	acquire_next_image = vk_acquire_next_image;
	queue_submit = vk_queue_submit;
	queue_present = vk_queue_present;
	queue_wait_idle = vk_queue_wait_idle;
	get_fence_status = vk_get_fence_status;
	wait_for_fences = vk_wait_for_fences;
	toggle_vsync = vk_toggle_vsync;
	recommendedswapchainfmt = vk_recommended_swapchainfmt;

	//indirect Draw functions
	add_indirectcmdsignature = vk_add_indirectcmdsignature;
	remove_indirectcmdsignature = vk_remove_indirectcmdsignature;
	cmd_execindirect = vk_cmd_execindirect;

	// GPU Query Interface
	get_timestampfreq = vk_get_timestampfreq;
	add_querypool = vk_add_querypool;
	remove_querypool = vk_remove_querypool;
	cmd_resetquerypool = vk_cmd_resetquerypool;
	cmd_beginquery = vk_cmd_beginquery;
	cmd_endquery = vk_cmd_endquery;
	cmd_resolvequery = vk_cmd_resolvequery;

	// Stats Info Interface
	get_memstats = vk_get_memstats;
	get_memuse = vk_get_memuse;
	free_memstats = vk_free_memstats;

	// Debug Marker Interface
	cmd_begindebugmark = vk_cmd_begindebugmark;
	cmd_enddebugmark = vk_cmd_enddebugmark;
	cmd_adddebugmark = vk_cmd_adddebugmark;
	cmd_writemark = vk_cmd_writemark;

	// Resource Debug Naming Interface
	set_buffername = vk_set_buffername;
	set_texturename = vk_set_texturename;
	set_rendertargetname = vk_set_rendertargetname;
	set_pipelinename = vk_set_pipelinename;

	vk_init_renderer(app_name, desc, r);
}

void exit_vulkanrenderer(renderer_t* r)
{
	TC_ASSERT(r);
	vk_exit_renderer(r);
}
/*
bool _filter_resource(SPIRV_Resource* resource, ShaderStage currentStage)
{
	bool filter = false;
	filter = filter || (resource->is_used == false);
	filter = filter || (resource->type == SPIRV_Resource_Type::SPIRV_TYPE_STAGE_OUTPUTS);
	filter = filter || (resource->type == SPIRV_Resource_Type::SPIRV_TYPE_STAGE_INPUTS && currentStage != SHADER_STAGE_VERT);
	return filter;
}
*/

void vk_create_shaderreflection(const uint8_t* code, uint32_t codesize, shaderstage_t stage, shaderreflection_t* out)
{
	if (out == NULL) {
		TRACE(LOG_ERROR, "Create Shader Refection failed. Invalid reflection output!");
		return;
	}
	/*
	CrossCompiler cc;
	CreateCrossCompiler((const uint32_t*)code, codesize / sizeof(uint32_t), &cc);
	ReflectEntryPoint(&cc);
	ReflectShaderResources(&cc);
	ReflectShaderVariables(&cc);
	if (stage == SHADER_STAGE_COMP)
		ReflectComputeShaderWorkGroupSize(
			&cc, &out->mNumThreadsPerGroup[0], &out->mNumThreadsPerGroup[1], &out->mNumThreadsPerGroup[2]);
	else if (stage == SHADER_STAGE_TESC)
		ReflectHullShaderControlPoint(&cc, &out->mNumControlPoint);
	// lets find out the size of the name pool we need
	// also get number of resources while we are at it
	uint32_t namePoolSize = 0;
	uint32_t vertexInputCount = 0;
	uint32_t resourceCount = 0;
	uint32_t variablesCount = 0;
	namePoolSize += cc.EntryPointSize + 1;
	for (uint32_t i = 0; i < cc.ShaderResourceCount; i++) {
		SPIRV_Resource* resource = cc.pShaderResouces + i;
		// filter out what we don't use
		if (!_filter_resource(resource, stage)) {
			namePoolSize += resource->name_size + 1;
			if (resource->type == SPIRV_Resource_Type::SPIRV_TYPE_STAGE_INPUTS && stage == SHADER_STAGE_VERT)
				++vertexInputCount++;
			else
				++resourceCount++;
		}
	}
	for (uint32_t i = 0; i < cc.UniformVariablesCount; i++) {
		SPIRV_Variable* variable = cc.pUniformVariables + i;
		// check if parent buffer was filtered out
		bool parentFiltered = _filter_resource(cc.pShaderResouces + variable->parent_index, stage);
		// filter out what we don't use
		if (variable->is_used && !parentFiltered) {
			namePoolSize += variable->name_size + 1;
			++variablesCount;
		}
	}
	// we now have the size of the memory pool and number of resources
	char* namePool = NULL;
	if (namePoolSize) namePool = (char*)tc_calloc(namePoolSize, 1);
	char* pCurrentName = namePool;

	out->pEntryPoint = pCurrentName;
	ASSERT(pCurrentName);
	memcpy(pCurrentName, cc.pEntryPoint, cc.EntryPointSize);
	pCurrentName += cc.EntryPointSize + 1;

	vertexinput_t* pVertexInputs = NULL;
	// start with the vertex input
	if (stage == SHADER_STAGE_VERT && vertexInputCount > 0) {
		pVertexInputs = (vertexinput_t*)tf_malloc(sizeof(vertexinput_t) * vertexInputCount);
		uint32_t j = 0;
		for (uint32_t i = 0; i < cc.ShaderResourceCount; i++) {
			SPIRV_Resource* resource = cc.pShaderResouces + i;
			// filter out what we don't use
			if (!_filter_resource(resource, stage) && resource->type == SPIRV_Resource_Type::SPIRV_TYPE_STAGE_INPUTS) {
				pVertexInputs[j].size = resource->size;
				pVertexInputs[j].name = pCurrentName;
				pVertexInputs[j].name_size = resource->name_size;
				// we dont own the names memory we need to copy it to the name pool
				memcpy(pCurrentName, resource->name, resource->name_size);
				pCurrentName += resource->name_size + 1;
				j++;
			}
		}
	}
	uint32_t* indexRemap = NULL;
	shaderresource_t* pResources = NULL;
	// continue with resources
	if (resourceCount) {
		indexRemap = (uint32_t*)tf_malloc(sizeof(uint32_t) * cc.ShaderResourceCount);
		pResources = (shaderresource_t*)tf_malloc(sizeof(shaderresource_t) * resourceCount);
		uint32_t j = 0;
		for (uint32_t i = 0; i < cc.ShaderResourceCount; i++) {
			// set index remap
			indexRemap[i] = (uint32_t)-1;
			SPIRV_Resource* resource = cc.pShaderResouces + i;
			// filter out what we don't use
			if (!_filter_resource(resource, stage) && resource->type != SPIRV_Resource_Type::SPIRV_TYPE_STAGE_INPUTS) {
				// set new index
				indexRemap[i] = j;

				pResources[j].type = sSPIRV_TO_DESCRIPTOR[resource->type];
				pResources[j].set = resource->set;
				pResources[j].reg = resource->binding;
				pResources[j].size = resource->size;
				pResources[j].used_stages = stage;

				pResources[j].name = pCurrentName;
				pResources[j].name_size = resource->name_size;
				pResources[j].dim = sSPIRV_TO_RESOURCE_DIM[resource->dim];
				// we dont own the names memory we need to copy it to the name pool
				memcpy(pCurrentName, resource->name, resource->name_size);
				pCurrentName += resource->name_size + 1;
				j++;
			}
		}
	}
	shadervar_t* pVariables = NULL;
	if (variablesCount) {
		pVariables = (shadervar_t*)tf_malloc(sizeof(shadervar_t) * variablesCount);
		uint32_t j = 0;
		for (uint32_t i = 0; i < cc.UniformVariablesCount; i++) {
			SPIRV_Variable* variable = cc.pUniformVariables + i;
			// check if parent buffer was filtered out
			bool parentFiltered = _filter_resource(cc.pShaderResouces + variable->parent_index, stage);
			// filter out what we don't use
			if (variable->is_used && !parentFiltered) {
				pVariables[j].offset = variable->offset;
				pVariables[j].size = variable->size;
				ASSERT(indexRemap);
				pVariables[j].parent_index = indexRemap[variable->parent_index]; //-V522
				pVariables[j].name = pCurrentName;
				pVariables[j].name_size = variable->name_size;
				// we dont own the names memory we need to copy it to the name pool
				memcpy(pCurrentName, variable->name, variable->name_size);
				pCurrentName += variable->name_size + 1;
				j++;
			}
		}
	}
	tc_free(indexRemap);
	DestroyCrossCompiler(&cc);
	// all refection structs should be built now
	out->mShaderStage = stage;
	out->pNamePool = namePool;
	out->mNamePoolSize = namePoolSize;
	out->pVertexInputs = pVertexInputs;
	out->mVertexInputsCount = vertexInputCount;
	out->pShaderResources = pResources;
	out->mShaderResourceCount = resourceCount;
	out->pVariables = pVariables;
	out->mVariableCount = variablesCount;
	*/
}