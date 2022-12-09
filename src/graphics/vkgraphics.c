/* Imports */
#include "vkgraphics.h"
#include "slab.h"
#include "temp.h"
#include "lock.h"
#include "sbuf.h"
#include "heap.h"
#include "hash.h"
#include "strings.h"
#include "list.h"


struct extension_features {
    VkPhysicalDeviceMeshShaderFeaturesNV meshshader;
    VkPhysicalDevice16BitStorageFeaturesKHR storage16;
    VkPhysicalDevice8BitStorageFeaturesKHR storage8;
    VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shader_float16int8;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelstruct;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracing_pipeline;
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address;
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptor_indexing;
    bool spirv14;
    bool spirv15;
};

struct extension_properties {
    VkPhysicalDeviceMeshShaderPropertiesNV meshshader;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelstruct;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracing_pipeline;
    VkPhysicalDeviceDescriptorIndexingPropertiesEXT descriptor_indexing;
};

typedef struct vkphysicaldevice_s {
    VkPhysicalDevice handle;
    vkrenderer_t* renderer;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memory_properties;
    struct extension_features ext_features;
    struct extension_properties ext_properties;
    VkQueueFamilyProperties* queue_family_properties;
    VkExtensionProperties* supported_extensions;
} vkphysicaldevice_t;

typedef struct vkinstance_s {
    VkInstance handle;
    vkrenderer_t* renderer;
    VkLayerProperties* layers;
    VkExtensionProperties* extensions;
    const char** enabled_extensions;
    VkPhysicalDevice* devices;
    VkAllocationCallbacks* allocator;
    uint32_t version;
    bool debug_utils;
} vkinstance_t;

typedef struct vklogicaldevice_s {
    VkDevice handle;
    VkAllocationCallbacks* allocator;
    VkPipelineStageFlags stages;
    VkPhysicalDeviceFeatures feats;
    struct extension_features ext_feats;
} vklogicaldevice_t;

typedef struct vkfbcache_s {
    vkrenderer_t* renderer;
    lock_t lock;
    hash_t map;
    hash_t view_keys;
    hash_t pass_keys;
} vkfbcache_t;

typedef struct vkrenderpass_s {
    slab_obj_t;
    vkrenderer_t* renderer;
    VkRenderPass handle;
} vkrenderpass_t;

typedef struct vkpasscache_s {
    vkrenderer_t* renderer;
    lock_t lock;
    hash_t map;
    vkrenderpass_t* allocator;
} vkpasscache_t;

typedef struct vkfence_s {
    slab_obj_t;
    vkrenderer_t* renderer;
    VkFence* fences;
    list_t pending;
    size_t last_val;
    uint32_t ref;
} vkfence_t;

typedef struct vkcmdqueue_s {
    vklogicaldevice_t* device;
    VkQueue handle;
    uint32_t queue_family_idx;
    vkfence_t* fence;
    atomic_t next_fence_val;
    lock_t lock;
} vkcmdqueue_t;

struct vkdescpoolnode_t;
typedef struct vkdescset_s vkdescset_t;

typedef struct vkdescmgr_s {
    vkrenderer_t* renderer;
    VkDescriptorPool handle;
    VkDescriptorPoolSize* sizes_buf;
    list_t pools;
    lock_t lock;
    uint32_t num_pools;
    uint32_t num_sets;
    sid_t name;
    uint32_t max_sets;
    bool can_free;
    struct vkdescpoolnode_t* allocator;
    vkdescset_t* released_sets;
} vkdescmgr_t;

typedef struct vkdescset_s {
    slab_obj_t;
    VkDescriptorSet set;
    VkDescriptorPool pool;
    vkdescmgr_t* source;
} vkdescset_t;

typedef struct vksetallocator_s {
    vkdescmgr_t* global_pool;
    VkDescriptorPool* pools;
    uint32_t peak_count;
    sid_t name;
} vksetallocator_t;

struct vkcmdpoolnode_t;

typedef struct vkcmdpool_s {
    vkrenderer_t* renderer;
    lock_t lock;
    list_t pools;
    VkCommandPoolCreateFlags flags;
    uint32_t num_pools;
    sid_t name;
    uint32_t queue_family_idx;
    struct vkcmdpoolnode_t* allocator;
} vkcmdpool_t;

struct vkpageidx_t;

typedef struct vkmemmgr_s {
    vkrenderer_t* renderer;
    void* allocator;
    lock_t lock;
    hash_t pages;
    struct vkpageidx_t* pool;
    atomic_t used_size[2];
    VkDeviceSize peak_used[2];
    VkDeviceSize allocated[2];
    VkDeviceSize peak_allocated[2];
    VkDeviceSize device_local_page;
    VkDeviceSize host_visible_page;
    VkDeviceSize device_local_reserve;
    VkDeviceSize host_visible_reserve;
    sid_t name;
} vkmemmgr_t;

typedef struct vkmempage_s {
    vkmemmgr_t* mgr;
    lock_t lock;
    offsetheap_t heap;
    VkDeviceMemory handle;
    void* data;
} vkmempage_t;

typedef struct vkallocation_s {
    vkmempage_t* page;
    VkDeviceSize offset;
    VkDeviceSize size;
} vkallocation_t;

struct staleallocation_t {
    allocation_t;
    slab_obj_t;
};

typedef struct vkheapmgr_s {
    vkrenderer_t* renderer;
    offsetheap_t allocator;
    struct staleallocation_t* stale_allocations;
    lock_t lock;
    VkBuffer buff;
    VkDeviceMemory mem;
    VkDeviceSize alignment;
    uint8_t* data;
    size_t peak_size;
} vkheapmgr_t;

typedef struct vkheap_s {
    vkheapmgr_t* mgr;
    allocation_t* v_allocs;
    size_t offset;
    uint32_t pagesize;
    uint32_t free;
    uint32_t aligned;
    uint32_t used;
    uint32_t peak_aligned;
    uint32_t peak_used;
    uint32_t allocated;
    uint32_t peak_allocated;
    sid_t name;
} vkheap_t;

typedef struct vkheapallocation_s {
    vkheapmgr_t* mgr;
    VkDeviceSize offset;
    VkDeviceSize size;
} vkheapallocation_t;

struct vkpendingfence_t;

typedef struct vkrenderer_s {
    vk_params params;
    tc_allocator_i* allocator;
    graphicscaps_t caps;
    stringpool_t strings;
    vkinstance_t instance;
    vkphysicaldevice_t physical_device;
    vklogicaldevice_t device;
    vkfbcache_t fbcache;
    vkpasscache_t rpcache;
    vkdescmgr_t static_pool;
    vkdescmgr_t dynamic_pool;
    vkcmdpool_t cmdpool_mgr;
    vkcmdqueue_t cmdqueue;
    vkmemmgr_t memory_mgr;
    vkheapmgr_t heap_mgr;
    struct {
        uint32_t shader_group_handle_size;
        uint32_t shader_group_base_alignment;
        uint32_t max_shader_record_stride;
        uint32_t max_draw_mesh_tasks;
        uint32_t max_ray_tracing_recusions;
        uint32_t max_ray_treads;
    } properties;
    atomic_t next_cmd_val;
    lock_t release_lock;
    list_t release_queue;
    lock_t stale_lock;
    list_t stale_queue;
    struct vkstale_t* stale_allocations;
    struct vkpendingfence_t* pending_fences;
    vkfence_t* fences;
} vkrenderer_t;

void vk_instance_init(vkinstance_t* instance, vkrenderer_t* renderer, uint32_t api_version, bool enable_validation, const char* const* extension_names, uint32_t extension_count, VkAllocationCallbacks* allocator);
void vk_instance_delete(vkinstance_t* instance);


static PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT = NULL;
static PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT = NULL;
static PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectNameEXT = NULL;
static PFN_vkSetDebugUtilsObjectTagEXT setDebugUtilsObjectTagEXT = NULL;
static PFN_vkQueueBeginDebugUtilsLabelEXT queueBeginDebugUtilsLabelEXT = NULL;
static PFN_vkQueueEndDebugUtilsLabelEXT queueEndDebugUtilsLabelEXT = NULL;
static PFN_vkQueueInsertDebugUtilsLabelEXT queueInsertDebugUtilsLabelEXT = NULL;
static PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginDebugUtilsLabelEXT = NULL;
static PFN_vkCmdEndDebugUtilsLabelEXT cmdEndDebugUtilsLabelEXT = NULL;
static PFN_vkCmdInsertDebugUtilsLabelEXT cmdInsertDebugUtilsLabelEXT = NULL;

static VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

static const char* device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const char* validation_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

static const VkFormat pixelformat_lut[] = {
    VK_FORMAT_BC1_RGB_UNORM_BLOCK,      //BC1
    VK_FORMAT_BC2_UNORM_BLOCK,          //BC2
    VK_FORMAT_BC3_UNORM_BLOCK,          //BC3
    VK_FORMAT_BC4_UNORM_BLOCK,          //BC4
    VK_FORMAT_BC5_UNORM_BLOCK,          //BC5
    VK_FORMAT_BC6H_SFLOAT_BLOCK,        //BC6H
    VK_FORMAT_BC7_UNORM_BLOCK,          //BC7
    VK_FORMAT_UNDEFINED,                //Unknown
    VK_FORMAT_UNDEFINED,                //R1
    VK_FORMAT_UNDEFINED,                //A8
    VK_FORMAT_R8_UNORM,                 //R8
    VK_FORMAT_R8_SINT,                  //R8I
    VK_FORMAT_R8_UINT,                  //R8U
    VK_FORMAT_R8_SNORM,                 //R8S
    VK_FORMAT_R16_UNORM,                //R16
    VK_FORMAT_R16_SINT,                 //R16I
    VK_FORMAT_R8_UINT,                  //R16U
    VK_FORMAT_R16_SFLOAT,               //R16F
    VK_FORMAT_R16_SNORM,                //R16S
    VK_FORMAT_R32_SINT,                 //R32I
    VK_FORMAT_R32_UINT,                 //R32U
    VK_FORMAT_R32_SFLOAT,               //R32F
    VK_FORMAT_R8G8_UNORM,               //RG8
    VK_FORMAT_R8G8_SINT,                //RG8I
    VK_FORMAT_R8G8_UINT,                //RG8U
    VK_FORMAT_R8G8_SNORM,               //RG8S
    VK_FORMAT_R16G16_UNORM,             //RG16
    VK_FORMAT_R16G16_SINT,              //RG16I
    VK_FORMAT_R16G16_UINT,              //RG16U
    VK_FORMAT_R16G16_SFLOAT,            //RG16F
    VK_FORMAT_R16G16_SNORM,             //RG16S
    VK_FORMAT_R32G32_SINT,              //RG32I
    VK_FORMAT_R32G32_UINT,              //RG32U
    VK_FORMAT_R32G32_SFLOAT,            //RG32F
    VK_FORMAT_R8G8B8_UNORM,             //RGB8
    VK_FORMAT_R8G8B8_SINT,              //RGB8I
    VK_FORMAT_R8G8B8_UINT,              //RGB8U
    VK_FORMAT_R8G8B8_SNORM,             //RGB8S
    VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,   //RGB9E5F
    VK_FORMAT_B8G8R8A8_UNORM,           //BGRA8
    VK_FORMAT_R8G8B8A8_UNORM,           //RGBA8
    VK_FORMAT_R8G8B8A8_SINT,            //RGBA8I
    VK_FORMAT_R8G8B8A8_UINT,            //RGBA8U
    VK_FORMAT_R8G8B8A8_SNORM,           //RGBA8S
    VK_FORMAT_R16G16B16A16_UNORM,       //RGBA16
    VK_FORMAT_R16G16B16A16_SINT,        //RGBA16I
    VK_FORMAT_R16G16B16A16_UINT,        //RGBA16U
    VK_FORMAT_R16G16B16A16_SFLOAT,      //RGBA16F
    VK_FORMAT_R16G16B16A16_SNORM,       //RGBA16S
    VK_FORMAT_R32G32B32A32_SINT,        //RGBA32I
    VK_FORMAT_R32G32B32A32_UINT,        //RGBA32U
    VK_FORMAT_R32G32B32A32_SFLOAT,      //RGBA32F
    VK_FORMAT_B5G6R5_UNORM_PACK16,      //R5G6B5
    VK_FORMAT_B4G4R4A4_UNORM_PACK16,    //RGBA4
    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    //RGB5A1
    VK_FORMAT_A2R10G10B10_UNORM_PACK32, //RGB10A2
    VK_FORMAT_B10G11R11_UFLOAT_PACK32,  //RG11B10F
    VK_FORMAT_UNDEFINED,                //UnknownDepth
    VK_FORMAT_D16_UNORM,                //D16
    VK_FORMAT_D24_UNORM_S8_UINT,        //D24
    VK_FORMAT_D24_UNORM_S8_UINT,        //D24S8
    VK_FORMAT_D24_UNORM_S8_UINT,        //D32
    VK_FORMAT_D32_SFLOAT,               //D16F
    VK_FORMAT_D32_SFLOAT,               //D24F
    VK_FORMAT_D32_SFLOAT,               //D32F
    VK_FORMAT_D24_UNORM_S8_UINT,        //D0S8
};

static VkImageLayout resourcestate_to_vk(enum resourcestate_t flag, bool inside_renderpass) 
{
    TC_ASSERT((flag & (flag - 1)) == 0, "[Vulkan]: Only single resource state flag must be set");
    switch (flag) {
    case STATE_RENDER_TARGET:       return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case STATE_UNORDERED_ACCESS:    return VK_IMAGE_LAYOUT_GENERAL;
    case STATE_DEPTH_WRITE:         return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case STATE_DEPTH_READ:          return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case STATE_SHADER_RESOURCE:     return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case STATE_COPY_DST:            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case STATE_COPY_SRC:            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case STATE_RESOLVE_DST:         return inside_renderpass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case STATE_RESOLVE_SRC:         return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case STATE_INPUT_ATTACHMENT:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case STATE_PRESENT:             return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:                        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
    VkDebugUtilsMessageTypeFlagsEXT messageType, 
    const VkDebugUtilsMessengerCallbackDataEXT* data, 
    void* pUserData) 
{
    TRACE(LOG_DEBUG, "[Vulkan]: Validation layer: %s", data->pMessage);
    return VK_FALSE;
}

static void create_debug_utils_messenger(
    VkInstance handle, 
    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity, 
    VkDebugUtilsMessageTypeFlagsEXT messageType, 
    void* userdata)
{
    createDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(handle, "vkCreateDebugUtilsMessengerEXT");
    destroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(handle, "vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = messageSeverity;
    createInfo.messageType = messageType;
    createInfo.pfnUserCallback = vk_debug_cb;
    createInfo.pUserData = userdata;
    VkResult err = createDebugUtilsMessengerEXT(handle, &createInfo, NULL, &debugMessenger);
    TC_ASSERT(err == VK_SUCCESS, "[Vulkan]: Failed to create debug utils messenger");
    setDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(handle, "vkSetDebugUtilsObjectNameEXT");
    TC_ASSERT(setDebugUtilsObjectNameEXT);
    setDebugUtilsObjectTagEXT = (PFN_vkSetDebugUtilsObjectTagEXT)vkGetInstanceProcAddr(handle, "vkSetDebugUtilsObjectTagEXT");
    TC_ASSERT(setDebugUtilsObjectTagEXT);
    queueBeginDebugUtilsLabelEXT = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkQueueBeginDebugUtilsLabelEXT");
    TC_ASSERT(queueBeginDebugUtilsLabelEXT);
    queueEndDebugUtilsLabelEXT = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkQueueEndDebugUtilsLabelEXT");
    TC_ASSERT(queueEndDebugUtilsLabelEXT);
    queueInsertDebugUtilsLabelEXT = (PFN_vkQueueInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkQueueInsertDebugUtilsLabelEXT");
    TC_ASSERT(queueInsertDebugUtilsLabelEXT);
    cmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkCmdBeginDebugUtilsLabelEXT");
    TC_ASSERT(cmdBeginDebugUtilsLabelEXT);
    cmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkCmdEndDebugUtilsLabelEXT");
    TC_ASSERT(cmdEndDebugUtilsLabelEXT);
    cmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(handle, "vkCmdInsertDebugUtilsLabelEXT");
    TC_ASSERT(cmdInsertDebugUtilsLabelEXT);
}

static void destroy_debug_utils_messenger(VkInstance handle) 
{
    if (debugMessenger != VK_NULL_HANDLE) 
        destroyDebugUtilsMessengerEXT(handle, debugMessenger, NULL);
}

static void vk_set_handle_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name) 
{
    if (setDebugUtilsObjectNameEXT != NULL && name != NULL && *name != '\0') {
        VkDebugUtilsObjectNameInfoEXT ObjectNameInfo = { 0 };
        ObjectNameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        ObjectNameInfo.pNext = NULL;
        ObjectNameInfo.objectType = type;
        ObjectNameInfo.objectHandle = handle;
        ObjectNameInfo.pObjectName = name;
        TC_ASSERT(setDebugUtilsObjectNameEXT(device, &ObjectNameInfo) == VK_SUCCESS);
    }
}

bool vk_instance_layer_available(vkinstance_t* handle, const char* layer, uint32_t* version) 
{
    for (uint32_t i = 0; i < buff_count(handle->layers); i++)
        if (strcmp(handle->layers[i].layerName, layer) == 0) {
            *version = handle->layers[i].specVersion;
            return true;
        }
    return false;
}

bool vk_instance_extension_available(vkinstance_t* handle, const char* extension) 
{
    for (uint32_t i = 0; i < buff_count(handle->extensions); i++)
        if (strcmp(handle->extensions[i].extensionName, extension) == 0)
            return true;
    return false;
}

bool vk_instance_extension_enabled(vkinstance_t* handle, const char* extension) 
{
    for (uint32_t i = 0; i < buff_count(handle->enabled_extensions); i++)
        if (strcmp(handle->enabled_extensions[i], extension) == 0)
            return true;
    return false;
}

static bool is_device_suitable(VkPhysicalDevice device) 
{
    TC_TEMP_INIT(a);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    TC_ASSERT(count > 0);
    VkQueueFamilyProperties* qfprops = NULL;
    buff_add(qfprops, count, &a);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, qfprops);
    TC_ASSERT(count == buff_count(qfprops));
    for (uint32_t i = 0; i < buff_count(qfprops); i++) {
        if ((qfprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (qfprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            TC_TEMP_CLOSE(a);
            return true;
        }
    }
    TC_TEMP_CLOSE(a);
    return false;
}

void vk_instance_init(
    vkinstance_t* instance, 
    vkrenderer_t* renderer,
    uint32_t api_version, 
    bool enable_validation, 
    const char** extension_names,
    uint32_t extension_count, 
    VkAllocationCallbacks* allocator) 
{
    instance->allocator = allocator;
    instance->renderer = renderer;
    uint32_t count = 0;
    // Enumerate available layers
    VkResult res = vkEnumerateInstanceLayerProperties(&count, NULL);
    TC_ASSERT(res == VK_SUCCESS, "[Vulkan]: Failed to query layers");
    buff_add(instance->layers, count, renderer->allocator);
    vkEnumerateInstanceLayerProperties(&count, instance->layers);
    TC_ASSERT(count == buff_count(instance->layers));
    // Enumerate available extensions
    res = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    TC_ASSERT(res == VK_SUCCESS, "[Vulkan]: Failed to query extensions");
    buff_add(instance->extensions, count, renderer->allocator);
    vkEnumerateInstanceExtensionProperties(NULL, &count, instance->extensions);
    TC_ASSERT(count == buff_count(instance->extensions));
    // Enumerate required layers
    if (vk_instance_extension_available(instance, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        buff_push(instance->enabled_extensions, renderer->allocator, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (TC_DEBUG) buff_push(instance->enabled_extensions, renderer->allocator, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (extension_names && extension_count)
        for (uint32_t i = 0; i < extension_count; i++)
            buff_push(instance->enabled_extensions, renderer->allocator, extension_names[i]);
    // Check availability of required extensions
    for (uint32_t i = 0; i < buff_count(instance->enabled_extensions); i++) {
        if (!vk_instance_extension_available(instance, instance->enabled_extensions[i]))
            TRACE(LOG_ERROR, "[Vulkan]: Required extension %s is not available", instance->enabled_extensions[i]);
    }
    if (enable_validation) {
        instance->debug_utils = vk_instance_extension_available(instance, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (instance->debug_utils) buff_push(instance->enabled_extensions, renderer->allocator, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        else TRACE(LOG_WARNING, "[Vulkan]: Extension %s is not available.", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    // Fill in vulkan app info 
    VkApplicationInfo appInfo = { 0 };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = window_get_title();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = TC_ENGINE_NAME;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = api_version;

    VkInstanceCreateInfo createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = buff_count(instance->enabled_extensions);
    createInfo.ppEnabledExtensionNames = instance->enabled_extensions;

    if (enable_validation) {
        bool present = true;
        for (size_t i = 0; i < TC_COUNT(validation_layers); i++) {
            uint32_t version = 0;
            if (!vk_instance_layer_available(instance, validation_layers[i], &version)) {
                present = false;
                TRACE(LOG_WARNING, "[Vulkan]: Failed to find '%s' layer. Validation will be disabled", validation_layers[i]);
            }
            if (version < VK_HEADER_VERSION_COMPLETE)
                TRACE(LOG_WARNING, "[Vulkan]: Layer '%s' version (%i.%i.%i) is less than header version (%i.%i.%i)", validation_layers[i], VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version), VK_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE), VK_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE), VK_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));

        }
        if (present) {
            createInfo.enabledLayerCount = (uint32_t)TC_COUNT(validation_layers);
            createInfo.ppEnabledLayerNames = validation_layers;
        }
    }
    if (vkCreateInstance(&createInfo, allocator, &instance->handle) != VK_SUCCESS)
        TRACE(LOG_ERROR, "[Vulkan]: Failed to create instance!");

    instance->version = api_version;
    if (instance->debug_utils) {
        VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        VkDebugUtilsMessageTypeFlagsEXT messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_debug_utils_messenger(instance->handle, messageSeverity, messageType, NULL);
    }

    res = vkEnumeratePhysicalDevices(instance->handle, &count, NULL);
    TC_ASSERT(res == VK_SUCCESS, "[Vulkan]: Failed to get physical device count");
    if (count == 0) {
        TRACE(LOG_ERROR, "[Vulkan]: Failed to find GPUs with Vulkan support!");
        vk_instance_delete(instance);
        return;
    }
    buff_add(instance->devices, count, renderer->allocator);
    vkEnumeratePhysicalDevices(instance->handle, &count, instance->devices);
    TC_ASSERT(count == buff_count(instance->devices));
}

VkPhysicalDevice vk_instance_pick_device(vkinstance_t* handle, uint32_t adapter_id) 
{
    VkPhysicalDevice selected = VK_NULL_HANDLE;
    if (adapter_id < buff_count(handle->devices) && is_device_suitable(handle->devices[adapter_id]))
        selected = handle->devices[adapter_id];

    if (selected == VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < buff_count(handle->devices); i++) {
            VkPhysicalDeviceProperties deviceProps;
            vkGetPhysicalDeviceProperties(handle->devices[i], &deviceProps);
            if (is_device_suitable(handle->devices[i])) {
                selected = handle->devices[i];
                if (deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
            }
        }
    }
    if (selected != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties selectedDeviceProps;
        vkGetPhysicalDeviceProperties(selected, &selectedDeviceProps);
        TRACE(LOG_INFO, "[Vulkan]: Using physical device %s", selectedDeviceProps.deviceName);
    }
    else TRACE(LOG_ERROR, "[Vulkan]: Failed to find suitable physical device");

    return selected;
}

void vk_instance_delete(vkinstance_t * instance) 
{
    if (instance->debug_utils) destroy_debug_utils_messenger(instance);
    vkDestroyInstance(&instance->handle, instance->allocator);
    buff_free(instance->devices, instance->renderer->allocator);
    buff_free(instance->layers, instance->renderer->allocator);
    buff_free(instance->extensions, instance->renderer->allocator);
    buff_free(instance->enabled_extensions, instance->renderer->allocator);
}

uint32_t vk_physicaldevice_find_queue_family(vkphysicaldevice_t* device, VkQueueFlags flags) 
{
    VkQueueFlags flagopts = flags;
    if (flags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) {
        flags &= ~VK_QUEUE_TRANSFER_BIT;
        flagopts = flags | VK_QUEUE_TRANSFER_BIT;
    }
    uint32_t idx = INVALID_VK_IDX;
    for (uint32_t i = 0; i < buff_count(device->queue_family_properties); i++)
        if (device->queue_family_properties[i].queueFlags == flags || device->queue_family_properties[i].queueFlags == flagopts) {
            idx = i;
            break;
        }
    if (idx == INVALID_VK_IDX)
        for (uint32_t i = 0; i < buff_count(device->queue_family_properties); i++)
            if ((device->queue_family_properties[i].queueFlags & flags) == flags) {
                idx = i;
                break;
            }
    if (idx != INVALID_VK_IDX) {
        if (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
#ifdef TC_DEBUG
            TC_ASSERT(
                device->queue_family_properties[idx].minImageTransferGranularity.width == 1 && 
                device->queue_family_properties[idx].minImageTransferGranularity.height == 1 && 
                device->queue_family_properties[idx].minImageTransferGranularity.depth == 1);
#endif
    }
    else TRACE(LOG_ERROR, "[Vulkan]: Failed to find suitable queue family");
    return idx;
}

bool vk_physicaldevice_extension_supported(vkphysicaldevice_t* device, const char* name) 
{
    for (uint32_t i = 0; i < buff_count(device->supported_extensions); i++)
        if (strcmp(device->supported_extensions[i].extensionName, name) == 0)
            return true;
    return false;
}

bool vk_physicaldevice_check_present_support(vkphysicaldevice_t* device, uint32_t queuefamily, VkSurfaceKHR surface) 
{
    VkBool32 support = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device->handle, queuefamily, surface, &support);
    return support == VK_TRUE;
}

uint32_t vk_physicaldevice_get_memory_type(vkphysicaldevice_t* device, uint32_t memoryTypeBitsRequirement, VkMemoryPropertyFlags required_properties) 
{
    for (uint32_t i = 0; i < device->memory_properties.memoryTypeCount; i++) {
        if ((memoryTypeBitsRequirement & (1 << i)) != 0) {
            if ((device->memory_properties.memoryTypes[i].propertyFlags & required_properties) == required_properties)
            return i;
        }
    }
    return INVALID_VK_IDX;
}

VkFormatProperties vk_physicaldevice_get_format_properties(vkphysicaldevice_t* device, VkFormat format) 
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device->handle, format, &properties);
    return properties;
}

void vk_physicaldevice_init(
    vkphysicaldevice_t* device, 
    vkrenderer_t* renderer,
    VkPhysicalDevice handle) 
{
    TC_ASSERT(handle != VK_NULL_HANDLE);
    device->handle = handle;
    device->renderer = renderer;
    vkGetPhysicalDeviceProperties(handle, &device->properties);
    vkGetPhysicalDeviceFeatures(handle, &device->features);
    vkGetPhysicalDeviceMemoryProperties(handle, &device->memory_properties);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &count, NULL);
    TC_ASSERT(count > 0);
    buff_add(device->queue_family_properties, count, renderer->allocator);
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &count, device->queue_family_properties);
    TC_ASSERT(count == buff_count(device->queue_family_properties));
    // Get list of supported extensions
    vkEnumerateDeviceExtensionProperties(handle, NULL, &count, NULL);
    if (count > 0) {
        buff_add(device->supported_extensions, count, renderer->allocator);
        VkResult res = vkEnumerateDeviceExtensionProperties(handle, NULL, &count, device->supported_extensions);
        TC_ASSERT(res == VK_SUCCESS);
        (void)res;
        TC_ASSERT(count == buff_count(device->supported_extensions));
    }
}

void vk_physicaldevice_destroy(vkphysicaldevice_t* device) 
{
    buff_free(device->queue_family_properties, device->renderer->allocator);
    buff_free(device->supported_extensions, device->renderer->allocator);
}

void vk_logicaldevice_init(
    vklogicaldevice_t* device, 
    vkphysicaldevice_t* physical_device, 
    VkDeviceCreateInfo* caps, 
    struct extension_features* features, 
    VkAllocationCallbacks* allocator)
{
    TC_ASSERT(physical_device != NULL);
    device->allocator = allocator;
    device->feats = physical_device->features;
    device->ext_feats = *features;
    VkResult result = vkCreateDevice(physical_device->handle, caps, allocator, &device->handle);
    TC_ASSERT(result == VK_SUCCESS, "[Vulkan]: Failed to create logical device");
    device->stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (caps->pEnabledFeatures->geometryShader)
        device->stages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (caps->pEnabledFeatures->tessellationShader)
        device->stages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (device->ext_feats.meshshader.meshShader != VK_FALSE && device->ext_feats.meshshader.taskShader != VK_FALSE)
        device->stages |= VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV | VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
    if (device->ext_feats.raytracing_pipeline.rayTracingPipeline != VK_FALSE)
        device->stages |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
}

VkDescriptorSet vk_logicaldevice_createset(vklogicaldevice_t* device, VkDescriptorPool pool, VkDescriptorSetLayout layout, const char* name) 
{
    VkDescriptorSetAllocateInfo info = { 0 };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext = NULL;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet handle = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device->handle, &info, &handle) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    if (name && *name != 0)
        vk_set_handle_name(device->handle, handle, VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
    return handle;
}

VkDescriptorPool vk_logicaldevice_createpool(vklogicaldevice_t* device, vkdescmgr_t* pool, const char* name) 
{
    VkDescriptorPoolCreateInfo info = { 0 };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.pNext = NULL;
    info.flags = pool->can_free ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    info.maxSets = pool->max_sets;
    info.poolSizeCount = buff_count(pool->sizes_buf);
    info.pPoolSizes = pool->sizes_buf;
    VkDescriptorPool handle = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device->handle, &info, device->allocator, &handle) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    if (name && *name != 0)
        vk_set_handle_name(device->handle, handle, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
    return handle;
}

VkBuffer vk_logicaldevice_createbuffer(vklogicaldevice_t* device, const VkBufferCreateInfo* info, const char* name) 
{
    TC_ASSERT(info->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    VkBuffer handle = VK_NULL_HANDLE;
    if (vkCreateBuffer(device->handle, info, device->allocator, &handle) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    if (name && *name != 0)
        vk_set_handle_name(device->handle, handle, VK_OBJECT_TYPE_BUFFER, name);
    return handle;
}

VkDeviceMemory vk_logicaldevice_alloc(vklogicaldevice_t* device, const VkMemoryAllocateInfo* info, const char* name) 
{
    TC_ASSERT(info->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    VkDeviceMemory handle = VK_NULL_HANDLE;
    if (vkAllocateMemory(device->handle, info, device->allocator, &handle) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    if (name && *name != 0)
        vk_set_handle_name(device->handle, handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);
    return handle;
}

void vk_logicaldevice_waitidle(vklogicaldevice_t* device)
{
    TC_ASSERT(vkDeviceWaitIdle(device->handle) == VK_SUCCESS, "[Vulkan]: Failed to idle device");
}

void vk_logicaldevice_destroy(vklogicaldevice_t* device)
{
    vkDestroyDevice(device->handle, device->allocator);
}

vkmempage_t vk_mempage_init(
    vkmemmgr_t* mgr, 
    VkDeviceSize size, 
    uint32_t type, 
    VkMemoryAllocateFlags flags, 
    bool host_visible)
{
    vkmempage_t page = { .mgr = mgr };
    page.heap = heap_init(size, mgr->allocator);
    VkMemoryAllocateInfo info = { 0 };
    VkMemoryAllocateFlagsInfo flaginfo = { 0 };
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = size;
    info.memoryTypeIndex = type;
    if (flags) {
        info.pNext = &flaginfo;
        flaginfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flaginfo.flags = flags;
    }
    char name[128];
    sprintf(name, "Device memory page (size %llu, type %lu)", size, type);
    page.handle = vk_logicaldevice_alloc(&mgr->renderer->device, &info, name);
    if (host_visible) {
        TC_ASSERT(vkMapMemory(mgr->renderer->device.handle, page.handle, 0, size, 0, &page.data) == VK_SUCCESS,
            "[Vulkan]: Failed to map staging memory");
    }
    return page;
}

vkallocation_t vk_mempage_alloc(vkmempage_t* page, VkDeviceSize size, VkDeviceSize alignment) 
{
    spin_lock(&page->lock);
    allocation_t a = heap_alloc(&page->heap, size, alignment);
    spin_unlock(&page->lock);
    if (allocation_is_valid(a)) {
        TC_ASSERT(align_up(a.offset, alignment) - a.offset + size <= a.size);
        return (vkallocation_t) { page, a.offset, a.size };
    }
    return (vkallocation_t) { 0 };
}

void vk_mempage_free(vkmempage_t* page, vkallocation_t* a) 
{
    atomic_fetch_add(&page->mgr->used_size[page->data != NULL], -a->size, MEMORY_RELAXED);
    spin_lock(&page->lock);
    heap_free(&page->heap, &(allocation_t) { a->offset, a->size });
    spin_unlock(&page->lock);
    a->page = NULL; a->offset = 0; a->size = 0;
}

void vk_mempage_destroy(vkmempage_t* page) 
{
    if (page->data != NULL) 
        vkUnmapMemory(page->mgr->renderer->device.handle, page->handle);
    TC_ASSERT(heap_empty(&page->mgr), "[Vulkan]: Destroying a page with not all allocations released");
}

struct vkpageidx_t {
    slab_obj_t;
    uint32_t mem_type;
    VkMemoryAllocateFlags flags;
    bool host_visible;
    struct vkpageidx_t* next_page;
    vkmempage_t page;
};

static uint64_t vk_pageidx_hash(struct vkpageidx_t* idx) 
{
    uint64_t h = tc_hash64(&idx->mem_type, sizeof(uint32_t), 0);
    h = tc_hash64_combine(h, (uint64_t)idx->flags);
    return tc_hash64_combine(h, (uint64_t)idx->host_visible);
}

void vk_memmanager_init(vkmemmgr_t* mgr, vkrenderer_t* renderer, const char* name, void* allocator, VkDeviceSize device_size, VkDeviceSize host_size, VkDeviceSize device_reserve, VkDeviceSize host_reserve)
{
    memset(mgr, 0, sizeof(vkmemmgr_t));
    mgr->renderer = renderer;
    mgr->pages = hash_init(renderer->allocator);
    slab_create(&mgr->pool, renderer->allocator, CHUNK_SIZE);
    mgr->name = string_intern(&renderer->strings, name, strlen(name));
    mgr->allocator = allocator;
    mgr->device_local_page = device_size;
    mgr->host_visible_page = host_size;
    mgr->device_local_reserve = device_reserve;
    mgr->host_visible_reserve = host_reserve;
}

vkallocation_t vk_memmanager_alloc(vkmemmgr_t* mgr, VkDeviceSize size, VkDeviceSize alignment, uint32_t mem_type, bool host_visible, VkMemoryAllocateFlags flags) 
{
    vkallocation_t a = { 0 };
    struct vkpageidx_t idx = { .mem_type = mem_type, .host_visible = host_visible, .flags = flags };
    uint64_t hash = vk_pageidx_hash(&idx);
    spin_lock(&mgr->lock);
    {
        struct vkpageidx_t* pages = hash_get(&mgr->pages, hash);
        struct vkpageidx_t* page = pages;
        while(page) {
            if (page->flags == flags && page->host_visible == host_visible && page->mem_type == mem_type) {
                a = vk_mempage_alloc(&page->page, size, alignment);
                if (a.page != NULL) break;
            }
            page = page->next_page;
        }
        size_t stat_idx = host_visible ? 1 : 0;
        if (a.page == NULL) {
            VkDeviceSize psize = host_visible ? mgr->host_visible_page : mgr->device_local_page;
            while (psize < size) psize *= 2;
            mgr->allocated[stat_idx] += psize;
            mgr->peak_allocated[stat_idx] = max(mgr->peak_allocated[stat_idx], mgr->allocated[stat_idx]);
            // Add new page to the hashmap with pages
            struct vkpageidx_t* newp = slab_alloc(mgr->pool);
            newp->flags = flags;
            newp->host_visible = host_visible;
            newp->mem_type = mem_type;
            newp->next_page = pages;
            newp->page = vk_mempage_init(mgr, psize, mem_type, flags, host_visible);
            hash_put(&mgr->pages, hash, newp);
            TRACE(LOG_INFO, 
                "[Vulkan]: Allocated new %s page from %s (%llu, type %lu). Current allocated size: %llu",
                host_visible ? "host-visible" : "device-local", 
                string_get(&mgr->renderer->strings, mgr->name, NULL), 
                psize, 
                mem_type, 
                mgr->allocated[stat_idx]);
            a = vk_mempage_alloc(&newp->page, size, alignment);
            TC_ASSERT(a.page != NULL, "[Vulkan]: Failed to allocate new memory page");
        }
        if (a.page != NULL) TC_ASSERT(size + align_up(a.offset, alignment) - a.offset <= a.size);
        size_t s = atomic_fetch_add(&mgr->used_size[stat_idx], a.size, MEMORY_RELAXED);
        mgr->peak_used[stat_idx] = max(mgr->peak_used[stat_idx], s + a.size);
    }
    spin_unlock(&mgr->lock);
    return a;
}

void vk_memmanager_cleanup(vkmemmgr_t* mgr) 
{
    uint64_t h;
    struct vkpageidx_t *pages, *ppage, *npage;
    spin_lock(&mgr->lock);
    {
        hash_foreach(&mgr->pages, h, pages) {
            ppage = NULL;
            while (pages) {
                npage = pages->next_page;
                vkmempage_t* page = &pages->page;
                bool host_visible = page->data != NULL;
                size_t reserved = host_visible ? mgr->host_visible_reserve : mgr->device_local_reserve;
                if (heap_empty(&page->heap) && mgr->allocated[host_visible] > reserved) {
                    mgr->allocated[host_visible] -= heap_size(&page->heap);
                    TRACE(LOG_INFO, 
                        "[Vulkan]: Cleared %s page from %s (%zu). Current allocated size: %llu",
                        host_visible ? "host-visible" : "device-local", 
                        string_get(&mgr->renderer->strings, mgr->name, NULL), 
                        heap_size(&page->heap), 
                        mgr->allocated[host_visible]);
                    if (ppage) ppage->next_page = npage;
                    else if (npage) hash_put(&mgr->pages, h, npage);
                    else hash_remove(&mgr->pages, h);
                    slab_free(mgr->pool, pages);
                }
                else ppage = pages;
                pages = npage;
            }
        }
    }
    spin_unlock(&mgr->lock);
}

void vk_memmanager_destroy(vkmemmgr_t* mgr) 
{
    hash_free(&mgr->pages);
    string_release(&mgr->renderer->strings, mgr->name);
    size_t local_pages = mgr->peak_allocated[0] / mgr->device_local_page;
    size_t host_pages = mgr->peak_allocated[1] / mgr->host_visible_page;
    TRACE(LOG_INFO, "[Vulkan]: Memory manager %s stats:\n   >> %llu/%llu peak used/allocated device-local memory (%zu pages)\n  >> %llu/%llu peak used/allocated host-visible memory (%zu pages)",
        string_get(&mgr->renderer->strings, mgr->name, NULL), mgr->peak_used[0], mgr->peak_allocated[0], local_pages,  mgr->peak_used[1], mgr->peak_allocated[1], host_pages);
    uint64_t h;
    struct vkpageidx_t* page;
    hash_foreach(&mgr->pages, h, page) {
        while (page) {
            TC_ASSERT(heap_empty(&page->page.heap), "[Vulkan]: The page contains unfreed allocations");
            page = page->next_page;
        }
    }
    slab_destroy(mgr->pool);
    TC_ASSERT(mgr->used_size[0]._nonatomic == 0 && mgr->used_size[1]._nonatomic == 0);
}

void vk_heapmanager_init(vkheapmgr_t* mgr, vkrenderer_t* renderer, tc_allocator_i* allocator, uint32_t size) 
{
    memset(mgr, 0, sizeof(vkheapmgr_t));
    mgr->renderer = renderer;
    mgr->allocator = heap_init(size, allocator);
    slab_create(&mgr->stale_allocations, allocator, CHUNK_SIZE);
    VkPhysicalDeviceLimits limits = renderer->physical_device.properties.limits;
    mgr->alignment = max(limits.minUniformBufferOffsetAlignment, max(limits.minTexelBufferOffsetAlignment, limits.minStorageBufferOffsetAlignment));
    VkBufferCreateInfo info = { 0 };
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.flags = 0;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices = NULL;
    mgr->buff = vk_logicaldevice_createbuffer(&renderer->device, &info, "dynamic heap buffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(renderer->device.handle, mgr->buff, &req);
    VkMemoryAllocateInfo allocinfo = { 0 };
    allocinfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocinfo.allocationSize = req.size;
    allocinfo.memoryTypeIndex = vk_physicaldevice_get_memory_type(&renderer->physical_device, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    TC_ASSERT(allocinfo.memoryTypeIndex != INVALID_VK_IDX);
    mgr->mem = vk_logicaldevice_alloc(&renderer->device, &allocinfo, "host-visible memory for upload buffer");
    TC_ASSERT(vkMapMemory(renderer->device.handle, mgr->mem, 0, allocinfo.allocationSize, 0, &mgr->data) == VK_SUCCESS, "[Vulkan]: Failed to map memory");
    TC_ASSERT(vkBindBufferMemory(renderer->device.handle, mgr->buff, mgr->mem, 0) == VK_SUCCESS, "[Vulkan]: Failed to bind  bufer memory");
    TRACE(LOG_INFO, "[Vulkan]: GPU dynamic heap created. Total buffer size: %lu", size);
}

static allocation_t vk_heapmgr_safealloc(vkheapmgr_t* mgr, size_t size, size_t alignment) 
{
    spin_lock(&mgr->lock);
    allocation_t a = heap_alloc(&mgr->allocator, size, alignment);
    spin_unlock(&mgr->lock);
    return a;
}

allocation_t vk_heapmanager_alloc(vkheapmgr_t* mgr, size_t size, size_t alignment) 
{
    if (alignment == 0) alignment = 1024;
    if (size > heap_size(&mgr->allocator)) {
        TRACE(LOG_ERROR, "[Vulkan]: Dynamic heap size not large enough for requested size %zu", size);
        return INVALID_ALLOCATION;
    }
    allocation_t a = vk_heapmgr_safealloc(mgr, size, alignment);
    if (!allocation_is_valid(a)) {
        uint32_t sleeps = 0;
        uint32_t max_sleeps = 60;
        uint32_t dur = 1; //Wait 1 milisecond
        while (!allocation_is_valid(a) && sleeps < max_sleeps) {
            vk_cleanup(mgr->renderer, false);
            a = vk_heapmgr_safealloc(mgr, size, alignment);
            if (!allocation_is_valid(a)) {
                await(timer_start(dur, 0));
                sleeps++;
            }
            //TODO: instead of sleeping max 60 times, sleep max 60 ms
        }
        if (!allocation_is_valid(a)) {
            vk_idlegpu(mgr->renderer);
            a = vk_heapmgr_safealloc(mgr, size, alignment);
            if (!allocation_is_valid(a)) TRACE(LOG_ERROR, "[Vulkan]: Dynamic heap is full. Try increasing the size of the heap");
            else TRACE(LOG_WARNING, "[Vulkan]: Dynamic heap is almost full. Allocation forced GPU idling");
        }
        else if (sleeps == 0) TRACE(LOG_WARNING, "[Vulkan]: Dynamic heap is almost full forcing memory clean up during frame");
        else TRACE(LOG_WARNING, "[Vulkan]: Dynamic heap is almost full. Allocation forced waiting for space to get freed");
    }
    if (allocation_is_valid(a)) {
        spin_lock(&mgr->lock);
        mgr->peak_size = max(mgr->peak_size, heap_used(&mgr->allocator));
        spin_unlock(&mgr->lock);
    }
    return a;
}

static void vk_stalepage_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    (void)renderer;
    vkheapmgr_t* mgr = (vkheapmgr_t*)ud;
    struct staleallocation_t* a = (struct staleallocation_t*)handle;
    spin_lock(&mgr->lock);
    heap_free(&mgr->allocator, a);
    slab_free(mgr->stale_allocations, a);
    spin_unlock(&mgr->lock);
}

static void vk_buff_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    (void)ud;
    vklogicaldevice_t* device = &renderer->device;
    vkDestroyBuffer(device->handle, handle, device->allocator);
}

static void vk_mem_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    (void)ud;
    vklogicaldevice_t* device = &renderer->device;
    vkFreeMemory(device->handle, handle, device->allocator);
}

void vk_heapmanager_free(vkheapmgr_t* mgr, allocation_t* blocks, uint32_t count) 
{
    spin_lock(&mgr->lock);
    {
        for (uint32_t i = 0; i < count; i++) {
            struct staleallocation_t* a = slab_alloc(mgr->stale_allocations);
            a->size = blocks[i].size;
            a->offset = blocks[i].offset;
            vk_release_handle(mgr->renderer, vk_stalepage_destroy_cb, a, mgr);
        }
    }
    spin_unlock(&mgr->lock);
}

void vk_heapmanager_destroy(vkheapmgr_t* heap) 
{
    if (heap->buff) {
        vkUnmapMemory(heap->renderer->device.handle, heap->mem);
        vk_release_handle(heap->renderer, vk_buff_destroy_cb, heap->buff, NULL);
        vk_release_handle(heap->renderer, vk_mem_destroy_cb, heap->mem, NULL);
    }
}

vkheap_t vk_heap_init(vkheapmgr_t* mgr, const char* name, uint32_t pagesize) 
{
    return (vkheap_t){ 
        .mgr = mgr, 
        .pagesize = pagesize, 
        .offset = INVALID_OFFSET, 
        .name = string_intern(&mgr->renderer->strings, name, strlen(name)) 
    };
}

vkheapallocation_t vk_heap_alloc(vkheap_t* heap, uint32_t size, uint32_t alignment) 
{
    TC_ASSERT(alignment > 0 && is_power_of_2(alignment), "[Vulkan]: Alignment must be power of 2");
    size_t offset2 = INVALID_OFFSET;
    size_t size2 = 0;
    if (size > heap->pagesize / 2) {
        allocation_t a = vk_heapmanager_alloc(heap->mgr, size, alignment);
        if (allocation_is_valid(a)) {
            offset2 = align_up(a.offset, alignment);
            size2 = a.size;
            TC_ASSERT(a.size >= size + (offset2 - a.offset));
            heap->allocated += (uint32_t)a.size;
            buff_push(heap->v_allocs, heap->mgr->renderer->allocator, a);
        }
    }
    else {
        if (heap->offset == INVALID_OFFSET || size + (align_up(heap->offset, alignment) - heap->offset) > heap->free) {
            allocation_t a = vk_heapmanager_alloc(heap->mgr, heap->pagesize, 0);
            if (allocation_is_valid(a)) {
                heap->offset = a.offset;
                heap->allocated += (uint32_t)a.size;
                heap->free = a.size;
                buff_push(heap->v_allocs, heap->mgr->renderer->allocator, a);
            }
        }
        if (heap->offset != INVALID_OFFSET) {
            offset2 = align_up(heap->offset, alignment);
            size2 = size + (offset2 - heap->offset);
            if (size2 <= heap->free) {
                heap->free -= (uint32_t)size2;
                heap->offset += size2;
            }
            else offset2 = INVALID_OFFSET;
        }
    }
    if (offset2 != INVALID_OFFSET) {
        heap->aligned += (uint32_t)size2;
        heap->used += size;
        heap->peak_aligned = max(heap->peak_aligned, heap->aligned);
        heap->peak_used = max(heap->peak_used, heap->used);
        heap->peak_allocated = max(heap->peak_allocated, heap->allocated);
        TC_ASSERT((offset2 & (alignment - 1)) == 0);
        return (vkheapallocation_t) { .mgr = heap->mgr, offset2, size };
    }
    return (vkheapallocation_t) { 0 };
}

void vk_heap_release(vkheap_t* heap) 
{
    vk_heapmanager_free(heap->mgr, heap->v_allocs, buff_count(heap->v_allocs));
    buff_free(heap->v_allocs, heap->mgr->renderer->allocator);
    heap->v_allocs = NULL;
    heap->offset = INVALID_OFFSET;
    heap->used = 0;
    heap->aligned = 0;
    heap->allocated = 0;
}

void vk_heap_destroy(vkheap_t* heap) 
{
    TC_ASSERT(buff_count(heap->v_allocs) == 0);
    uint32_t peak_pages = heap->peak_allocated / heap->pagesize;
    TRACE(LOG_INFO,
        "[Vulkan]: Dynamic heap %s stats:\n    >> Peak used/aligned/allocated size: %lu / %lu / %lu (%lu). Peak efficiency (used/aligned): %f%. Peak utilization (used/allocated): %f%.",
        heap->peak_used, heap->peak_aligned, heap->peak_allocated, peak_pages, (heap->peak_used/heap->peak_aligned)*100.0, (heap->peak_used/heap->peak_allocated)*100.0);
}

struct vkpendingfence_t {
    slab_obj_t;
    list_t node;
    VkFence fence;
    size_t val;
};

void vk_fence_init(vkfence_t* fence, vkrenderer_t* renderer) 
{
    memset(fence, 0, sizeof(vkfence_t));
    fence->renderer = renderer;
    list_init(&fence->pending);
}

VkFence vk_fence_get(vkfence_t* fence) 
{
    VkFence handle = VK_NULL_HANDLE;
    vklogicaldevice_t* device = &fence->renderer->device;
    if (buff_count(fence->fences) > 0) {
        handle = buff_pop(fence->fences);
        TC_ASSERT(vkResetFences(device->handle, 1, &handle) == VK_SUCCESS,
            "[Vulkan]: Error resetting fence");
    }
    else {
        VkFenceCreateInfo info = { 0 };
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device->handle, &info, device->allocator, &handle);
    }
    return handle;
}

void vk_fence_dispose(vkfence_t* fence, VkFence* handle) 
{
    TC_ASSERT(vkGetFenceStatus(fence->renderer->device.handle, *handle) == VK_SUCCESS,
        "[Vulkan]: Attempting to dispose fence that is not signalled yet");
    buff_push(fence->fences, fence->renderer->allocator, *handle);
    *handle = VK_NULL_HANDLE;
}

void vk_fence_add_pending(vkfence_t* fence, VkFence* handle, size_t val) 
{
    struct vkpendingfence_t* node = slab_alloc(fence->renderer->pending_fences);
    node->fence = *handle;
    node->val = val;
    list_add_tail(&fence->pending, node);
    *handle = VK_NULL_HANDLE;
}

void vk_fence_wait(vkfence_t* fence, size_t val) 
{
    vklogicaldevice_t* device = &fence->renderer->device;
    while (!list_empty(&fence->pending)) {
        struct vkpendingfence_t* node = (struct vkpendingfence_t*)fence->pending.next;
        if (node->val > val) break;
        //TODO: Wait in a worker thread
        VkFence handle = node->fence;
        VkResult res = vkGetFenceStatus(device->handle, handle);
        if (res == VK_NOT_READY)
            res = vkWaitForFences(device->handle, 1, &handle, VK_TRUE, UINT64_MAX);
        
        TC_ASSERT(res == VK_SUCCESS, "[Vulkan]: All pending fences must be complete");
        if (node->val > fence->last_val)
            fence->last_val = node->val;
        vk_fence_dispose(fence, &handle);
        slab_free(
            &fence->renderer->pending_fences, 
            list_pop(&fence->pending));
    }
}

size_t vk_fence_completed(vkfence_t* fence) 
{
    vklogicaldevice_t* device = &fence->renderer->device;
    while (!list_empty(&fence->pending)) {
        struct vkpendingfence_t* node = (struct vkpendingfence_t*)fence->pending.next;
        VkFence handle = node->fence;
        VkResult res = vkGetFenceStatus(device->handle, handle);
        if (res == VK_SUCCESS) {
            if (node->val > fence->last_val)
                fence->last_val = node->val;
            vk_fence_dispose(fence, &handle);
            slab_free(
                &fence->renderer->pending_fences, 
                list_pop(&fence->pending));
        }
        else break;
    }
    return fence->last_val;
}

void vk_fence_reset(vkfence_t* fence, size_t val) 
{
    TC_ASSERT(val >= fence->last_val, "[Vulkan]: Resetting fence to the value (%zu) that is smaller than the last completed value (%zu)", val, fence->last_val);
    if (val > fence->last_val)
        fence->last_val = val;
}

void vk_fence_destroy(vkfence_t* fence) 
{
    if (!list_empty(&fence->pending)) {
        TRACE(LOG_INFO, "[Vulkan]: Waiting for pending fences");
        vk_fence_wait(fence, SIZE_MAX);
    }
#ifdef TC_DEBUG
    for (uint32_t i = 0; i < buff_count(fence->fences); i++)
        TC_ASSERT(vkGetFenceStatus(fence->renderer->device.handle, fence->fences[i]) == VK_SUCCESS,
            "[Vulkan]: Attempting to destroy fence that is not signalled yet");
#endif
    buff_free(fence->fences, fence->renderer->allocator);
}

void vk_cmdqueue_init(vkcmdqueue_t* queue, vklogicaldevice_t* device, uint32_t index) 
{
    queue->device = device;
    queue->queue_family_idx = index;
    queue->next_fence_val._nonatomic = 1;
    vkGetDeviceQueue(device->handle, index, 0, &queue->handle);
    TC_ASSERT(queue->handle != VK_NULL_HANDLE);
}

void vk_cmdqueue_setfence(vkcmdqueue_t* queue, vkfence_t* fence) 
{
    queue->fence = fence;
}

size_t vk_cmdqueue_submit(vkcmdqueue_t* queue, const VkSubmitInfo* info) 
{
    spin_lock(&queue->lock);
    size_t fenceval = atomic_fetch_add(&queue->next_fence_val, 1, MEMORY_ACQ_REL);
    uint32_t count = (info->waitSemaphoreCount != 0 || info->commandBufferCount != 0 || info->signalSemaphoreCount != 0) ? 1 : 0;
    VkFence fence = vk_fence_get(queue->fence);
    TC_ASSERT(vkQueueSubmit(queue->handle, count, &info, fence) == VK_SUCCESS, 
        "[Vulkan]: Failed to submit command to queue");
    vk_fence_add_pending(queue->fence, fence, fenceval);
    spin_unlock(&queue->lock);
    return fenceval;
}

size_t vk_cmdqueue_completed(vkcmdqueue_t* queue) 
{
    spin_lock(&queue->lock);
    size_t fenceval = vk_fence_completed(queue->fence);
    spin_unlock(&queue->lock);
    return fenceval;
}

size_t vk_cmdqueue_wait(vkcmdqueue_t* queue) 
{
    spin_lock(&queue->lock);
    size_t fenceval = atomic_fetch_add(&queue->next_fence_val, 1, MEMORY_ACQ_REL);
    vkQueueWaitIdle(queue->handle);
    vk_fence_wait(queue->fence, SIZE_MAX);
    vk_fence_reset(queue->fence, fenceval);
    spin_unlock(&queue->lock);
    return fenceval;
}

void vk_cmdqueue_signal(vkcmdqueue_t* queue, VkFence handle) 
{
    spin_lock(&queue->lock);
    TC_ASSERT(vkQueueSubmit(queue->handle, 0, NULL, handle) == VK_SUCCESS, 
        "[Vulkan]: Failed to submit command buffer to the command queue");
    spin_unlock(&queue->lock);
}

VkResult vk_cmdqueue_present(vkcmdqueue_t* queue, const VkPresentInfoKHR* info) 
{
    spin_lock(&queue->lock);
    VkResult err = vkQueuePresentKHR(queue->handle, info);
    spin_unlock(&queue->lock);
    return err;
}

struct vkcmdpoolnode_t {
    list_t;
    VkCommandPool handle;
};

void vk_cmdpoolmgr_init(vkcmdpool_t* pool, vkrenderer_t* renderer, const char* name, uint32_t queue_family_index, VkCommandPoolCreateFlags flags)
{
    pool->renderer = renderer;
    pool->queue_family_idx = queue_family_index;
    pool->flags = flags;
    pool->name = string_intern(&renderer->strings, name, strlen(name));
    list_init(&pool->pools);
    slab_create(&pool->allocator, renderer->allocator, CHUNK_SIZE);
}

VkCommandPool vk_cmdpoolmgr_alloc(vkcmdpool_t* pool) 
{
    VkCommandPool handle = VK_NULL_HANDLE;
    spin_lock(&pool->lock);
    vklogicaldevice_t* device = &pool->renderer->device;
    if (!list_empty(&pool->pools)) {
        struct vkcmdpoolnode_t* node = list_pop(&pool->pools);
        handle = node->handle;
        TC_ASSERT(vkResetCommandPool(device->handle, handle, 0) == VK_SUCCESS, 
            "[Vulkan]: Could not reset command pool");
        slab_free(pool->allocator, node);
    }
    if (handle == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo info = { 0 };
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = pool->queue_family_idx;
        info.flags = pool->flags;
        TC_ASSERT(vkCreateCommandPool(device->handle, &info, device->allocator, &handle) == VK_SUCCESS, 
            "[Vulkan]: Failed to create Vulkan command pool");
        vk_set_handle_name(device->handle, handle, VK_OBJECT_TYPE_COMMAND_POOL, "command pool");
    }
    pool->num_pools++;
    spin_unlock(&pool->lock);
    return handle;
}

static void vk_cmdpool_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    vkcmdpool_t* pool = (vkcmdpool_t*)ud;
    spin_lock(&pool->lock);
    pool->num_pools--;
    struct vkcmdpoolnode_t* node = slab_alloc(pool->allocator);
    node->handle = handle;
    list_add_tail(&pool->pools, node);
    spin_unlock(&pool->lock);
}

void vk_cmdpoolmgr_release(vkcmdpool_t* pool, VkCommandPool handle, size_t fenceval) 
{
    vk_discard_handle(pool->renderer, vk_cmdpool_destroy_cb, handle, pool, fenceval);
}

void vk_cmdpoolmgr_destroy(vkcmdpool_t* pool) 
{
    spin_lock(&pool->lock);
    TC_ASSERT(pool->num_pools == 0, list_empty(&pool->pools), "[Vulkan]: %i pools have not been released yet");
    string_release(&pool->renderer->strings, pool->name);
    spin_unlock(&pool->lock);
}

static VkAttachmentReference* vk_convert_attachement_refs(attachment_ref* ref, uint32_t count, VkAttachmentReference* vkref, uint32_t* index) 
{
    VkAttachmentReference* curr_ref = &vkref[*index];
    for (uint32_t i = 0; i < count; i++, (*index)++) {
        vkref[*index].attachment = ref[i].index;
        vkref[*index].layout = resourcestate_to_vk(ref[i].state, true);
    }
    return curr_ref;
}

void vk_renderpass_init(vkrenderpass_t* renderpass, renderpass_params* caps, vkrenderer_t* renderer) 
{
    TC_TEMP_INIT(a);
    renderpass->renderer = renderer;
    VkAttachmentDescription* attachments = NULL;
    buff_add(attachments, caps->num_attachments, renderer->allocator);
    for (uint32_t i = 0; i < caps->num_attachments; i++) {
        attachments[i] = (VkAttachmentDescription) {
            .format = pixelformat_lut[caps->attachments[i].format],
            .samples = caps->attachments[i].num_samples,
            .loadOp = caps->attachments[i].load_op,
            .storeOp = caps->attachments[i].store_op,
            .stencilLoadOp = caps->attachments[i].stencil_load_op,
            .stencilStoreOp = caps->attachments[i].stencil_store_op,
            .initialLayout = resourcestate_to_vk(caps->attachments[i].initial_state, false),
            .finalLayout = resourcestate_to_vk(caps->attachments[i].final_state, true),
        };
    }
    VkRenderPassCreateInfo info = { 0 };
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = caps->num_attachments;
    info.pAttachments = attachments;
    uint32_t num_attachement_refs = 0;
    uint32_t num_preserves = 0;
    for (uint32_t i = 0; i < caps->num_subpasses; i++) {
        num_attachement_refs += caps->subpasses[i].num_input_attachments;
        num_attachement_refs += caps->subpasses[i].num_rendertarget_attachments;
        if (caps->subpasses[i].resolve_attachments != NULL)
            num_attachement_refs += caps->subpasses[i].num_rendertarget_attachments;
        if (caps->subpasses[i].depthstencil_attachments != NULL)
            num_attachement_refs += 1;
        num_preserves += caps->subpasses[i].num_preserve_attachments;
    }
    VkAttachmentReference* refs = NULL; buff_add(refs, num_attachement_refs, &a);
    uint32_t* preserves = NULL; buff_add(preserves, num_preserves, &a);
    VkSubpassDescription* subpasses = NULL; buff_add(subpasses, caps->num_subpasses, &a);
    VkSubpassDependency* deps = NULL; buff_add(deps, caps->num_dependencies, &a);

    uint32_t ref_idx = 0;
    uint32_t preserve_idx = 0;
    for (uint32_t i = 0; i < caps->num_subpasses; i++) {
        subpass_params subpass = caps->subpasses[i];
        subpasses[i] = (VkSubpassDescription) {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = subpass.num_input_attachments,
        };
        if (caps->subpasses[i].num_input_attachments)
            subpasses[i].pInputAttachments = vk_convert_attachement_refs(subpass.input_attachements, subpass.num_input_attachments, refs, &ref_idx);

        subpasses[i].colorAttachmentCount = subpass.num_rendertarget_attachments;
        if (subpass.num_rendertarget_attachments) {
            subpasses[i].pColorAttachments = vk_convert_attachement_refs(subpass.rendertarget_attachments, subpass.num_rendertarget_attachments, refs, &ref_idx);
            if (subpass.resolve_attachments != NULL)
                subpasses[i].pResolveAttachments = vk_convert_attachement_refs(subpass.rendertarget_attachments, subpass.num_rendertarget_attachments, refs, &ref_idx);
        }
        if (subpass.depthstencil_attachments != NULL)
            subpasses[i].pDepthStencilAttachment = vk_convert_attachement_refs(subpass.rendertarget_attachments, subpass.num_rendertarget_attachments, refs, &ref_idx);
        
        subpasses[i].preserveAttachmentCount = subpass.num_preserve_attachments;
        if (subpass.num_preserve_attachments) {
            subpasses[i].pPreserveAttachments = &preserves[preserve_idx];
            for (uint32_t j = 0; j < subpass.num_preserve_attachments; j++, preserve_idx++)
                preserves[preserve_idx] = subpass.preserve_attachments[j];
        }
    }
    TC_ASSERT(ref_idx == buff_count(refs) && preserve_idx == buff_count(preserves));
    info.subpassCount = caps->num_subpasses;
    info.pSubpasses = subpasses;
    for (uint32_t i = 0; i < caps->num_dependencies; i++) {
        deps[i].srcSubpass = caps->dependencies[i].src_pass;
        deps[i].dstSubpass = caps->dependencies[i].dst_pass;
        deps[i].srcStageMask = caps->dependencies[i].src_stage_mask;
        deps[i].dstStageMask = caps->dependencies[i].dst_stage_mask;
        deps[i].srcAccessMask = caps->dependencies[i].src_access_mask;
        deps[i].dstAccessMask = caps->dependencies[i].dst_access_mask;
        deps[i].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    }
    info.dependencyCount = caps->num_dependencies;
    info.pDependencies = deps;
    vkCreateRenderPass(renderer->device.handle, &info, renderer->device.allocator, &renderpass->handle);
    TC_ASSERT(renderpass->handle, "[Vulkan]: Failed to create vulkan render pass");
    TC_TEMP_CLOSE(a);
}

static void vk_rp_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    (void)ud;
    vklogicaldevice_t* device = &renderer->device;
    vkDestroyRenderPass(device->handle, handle, device->allocator);
}

void vk_renderpass_destroy(vkrenderpass_t* renderpass) 
{
    vk_release_handle(renderpass->renderer, vk_rp_destroy_cb, renderpass->handle, NULL);
}

struct vkfbkey_t {
    VkRenderPass pass;
    uint32_t num_rts;
    VkImageView DSV;
    VkImageView RTVs[RENDER_TARGETS_MAX];
    uint64_t cmdqueue_mask;
    uint64_t hash;
};

static uint64_t vkfbkey_hash(struct vkfbkey_t* key) 
{
    if (key->hash == 0) {
        uint64_t h = tc_hash64(&key->pass, sizeof(uint32_t), 0);
        h = tc_hash64_combine(h, key->num_rts);
        h = tc_hash64_combine(h, key->DSV);
        h = tc_hash64_combine(h, key->cmdqueue_mask);
        for (uint32_t i = 0; i < key->num_rts; i++)
            h = tc_hash64_combine(h, key->RTVs[i]);
        key->hash = h;
    }
    return key->hash;
}

static bool vkfbkey_equals(struct vkfbkey_t* key, struct vkfbkey_t* other) 
{
    if (vkfbkey_hash(key) != vkfbkey_hash(other) ||
        key->pass != other->pass ||
        key->num_rts != other->num_rts ||
        key->DSV != other->DSV ||
        key->cmdqueue_mask != other->cmdqueue_mask)
        return false;
    for (uint32_t i = 0; i < key->num_rts; i++)
        if (key->RTVs[i] != other->RTVs[i])
            return false;
    return true;
}

void vk_fbcache_init(vkfbcache_t* cache, vkrenderer_t* renderer) 
{
    cache->renderer = renderer;
    cache->map = hash_init(renderer->allocator);
    cache->view_keys = hash_init(renderer->allocator);
    cache->pass_keys = hash_init(renderer->allocator);
}

VkFramebuffer vk_fbcache_get(vkfbcache_t* cache, struct vkfbkey_t* fbkey, uint32_t width, uint32_t height, uint32_t layers) 
{
    spin_lock(&cache->lock);
    uint64_t key = vkfbkey_hash(fbkey);
    VkFramebuffer fb = hash_get(&cache->map, key);
    if (!fb) {
        VkFramebufferCreateInfo info = { 0 };
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = fbkey->pass;
        info.attachmentCount = (fbkey->DSV != VK_NULL_HANDLE ? 1 : 0) + fbkey->num_rts;
        VkImageView attachments[1 + RENDER_TARGETS_MAX];
        uint32_t attachment = 0;
        if (fbkey->DSV != VK_NULL_HANDLE)
            attachments[attachment++] = fbkey->DSV;
        for (uint32_t i = 0; i < fbkey->num_rts; i++)
            attachments[attachment++] = fbkey->RTVs[i];
        TC_ASSERT(attachment == info.attachmentCount);
        info.pAttachments = attachments;
        info.width = width;
        info.height = height;
        info.layers = layers;
        TC_ASSERT(vkCreateFramebuffer(cache->renderer->device.handle, &info, cache->renderer->device.allocator, &fb) == VK_SUCCESS, "[Vulkan]: Failed to create framebuffer");
        hash_put(&cache->map, key, fb);
        
#define PLACE_MAP(map, k) do { \
            struct vkfbkey_t* keys = hash_get(&cache->map, fbkey->k); \
            buff_push(keys, cache->renderer->allocator, *fbkey); \
            hash_put(&cache->map, fbkey->k, keys); \
        } while (0)

        PLACE_MAP(pass_keys, pass);
        if (fbkey->DSV != VK_NULL_HANDLE)
            PLACE_MAP(view_keys, DSV);
        for (uint32_t i = 0; i < fbkey->num_rts; i++)
            if (fbkey->RTVs[i] != VK_NULL_HANDLE)
                PLACE_MAP(view_keys, RTVs[i]);
#undef PLACE_MAP
    }
    spin_unlock(&cache->lock);
    return fb;
}

static void vk_fb_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    (void)ud;
    vklogicaldevice_t* device = &renderer->device;
    vkDestroyFramebuffer(device->handle, handle, device->allocator);
}

void vk_fbcache_removeview(vkfbcache_t* cache, VkImageView view) 
{
    spin_lock(&cache->lock);
    struct vkfbkey_t* keys = (struct vkfbkey_t*)hash_get(&cache->view_keys, view);
    for (uint32_t i = 0; i < buff_count(keys); i++) {
        uint64_t key = vkfbkey_hash(&keys[i]);
        VkFramebuffer fb = hash_get(&cache->map, key);
        if (fb != VK_NULL_HANDLE) {
            vk_release_handle(cache->renderer, vk_fb_destroy_cb, fb, NULL);
            hash_remove(&cache->map, fb);
        }
    }
    buff_free(keys, cache->renderer->allocator);
    hash_remove(&cache->view_keys, view);
    spin_unlock(&cache->lock);
}

void vk_fbcache_removepass(vkfbcache_t* cache, VkRenderPass pass) 
{
    spin_lock(&cache->lock);
    struct vkfbkey_t* keys = (struct vkfbkey_t*)hash_get(&cache->pass_keys, pass);
    for (uint32_t i = 0; i < buff_count(keys); i++) {
        uint64_t key = vkfbkey_hash(&keys[i]);
        VkFramebuffer fb = hash_get(&cache->map, key);
        if (fb != VK_NULL_HANDLE) {
            vk_release_handle(cache->renderer, vk_fb_destroy_cb, fb, NULL);
            hash_remove(&cache->map, fb);
        }
    }
    buff_free(keys, cache->renderer->allocator);
    hash_remove(&cache->pass_keys, pass);
    spin_unlock(&cache->lock);
}

void vk_fbcache_destroy(vkfbcache_t* cache) 
{
    //TODO: check if caches are empty
    hash_free(&cache->map);
    hash_free(&cache->view_keys);
    hash_free(&cache->pass_keys);
    cache->renderer = NULL;
}

struct vkpasskey_t {
    uint32_t num_rts;
    uint32_t num_samples;
    enum pixeltype_t DSV;
    enum pixeltype_t RTVs[RENDER_TARGETS_MAX];
    uint64_t hash;
};

uint64_t vkpasskey_hash(struct vkpasskey_t* key) 
{
    if (key->hash == 0) {
        uint64_t h = tc_hash64(&key->num_rts, sizeof(uint32_t), 0);
        h = tc_hash64_combine(h, key->num_samples);
        h = tc_hash64_combine(h, key->DSV);
        for (uint32_t i = 0; i < key->num_rts; i++)
            h = tc_hash64_combine(h, key->RTVs[i]);
        key->hash = h;
    }
    return key->hash;
}

void vk_passcache_init(vkpasscache_t* cache, vkrenderer_t* renderer) 
{
    cache->renderer = renderer;
    cache->map = hash_init(renderer->allocator);
    slab_create(&cache->allocator, renderer->allocator, CHUNK_SIZE);
}

vkrenderpass_t* vk_passcache_get(vkpasscache_t* cache, struct vkpasskey_t* passkey) 
{
    spin_lock(&cache->lock);
    uint64_t key = vkpasskey_hash(passkey);
    vkrenderpass_t* rp = hash_get(&cache->map, key);
    if (!rp) {
        attachment_params atms[RENDER_TARGETS_MAX + 1];
        attachment_ref atm_refs[RENDER_TARGETS_MAX + 1];
        renderpass_params caps = { 0 };
        caps.num_attachments = passkey->num_rts;
        uint32_t idx = 0;
        attachment_ref* depth_ref = NULL;
        if (passkey->DSV != PIXEL_FMT_Unknown) {
            caps.num_attachments += 1;
            atms[idx] = (attachment_params) {
                .format = passkey->DSV,
                .num_samples = passkey->num_samples,
                .load_op = LOAD_OP_LOAD,
                .store_op = STORE_OP_STORE,
                .stencil_load_op = LOAD_OP_LOAD,
                .stencil_store_op = STORE_OP_STORE,
                .initial_state = STATE_DEPTH_WRITE,
                .final_state = STATE_DEPTH_WRITE
            };
            depth_ref = &atm_refs[idx];
            depth_ref->index = idx;
            depth_ref->state = STATE_DEPTH_WRITE;
            idx++;
        }
        attachment_ref* color_ref = passkey->num_rts > 0 ? &atm_refs[idx] : NULL;
        for (uint32_t i = 0; i < passkey->num_rts; i++, idx++) {
            atms[idx] = (attachment_params){
                .format = passkey->RTVs[i],
                .num_samples = passkey->num_samples,
                .load_op = LOAD_OP_LOAD,
                .store_op = STORE_OP_STORE,
                .stencil_load_op = LOAD_OP_DISCARD,
                .stencil_store_op = STORE_OP_DISCARD,
                .initial_state = STATE_RENDER_TARGET,
                .final_state = STATE_RENDER_TARGET,
            };
            atm_refs[idx].index = idx;
            atm_refs[idx].state = STATE_RENDER_TARGET;
        }
        caps.attachments = atms;
        subpass_params subpass = {
            .num_rendertarget_attachments = passkey->num_rts,
            .rendertarget_attachments = color_ref,
            .depthstencil_attachments = depth_ref
        };
        caps.num_subpasses = 1;
        caps.subpasses = &subpass;
        caps.num_dependencies = 0;
        caps.dependencies = NULL;

        rp = slab_alloc(cache->allocator);
        vk_renderpass_init(rp, &caps, cache->renderer);
        hash_put(&cache->map, key, rp);
    }
    spin_unlock(&cache->lock);
    return rp;
}

void vk_passcache_destroy(vkpasscache_t* cache) 
{
    uint64_t key = 0;
    vkrenderpass_t* pass = NULL;
    hash_foreach(&cache->map, key, pass) {
        vk_fbcache_removepass(&cache->renderer->fbcache, pass->handle);
        slab_free(cache->allocator, pass);
    }
    hash_free(&cache->map);
    cache->renderer = NULL;
}

struct vkdescpoolnode_t {
    list_t node;
    VkDescriptorPool handle;
    slab_obj_t;
};

void vk_descmgr_init(vkdescmgr_t* pool, vkrenderer_t* renderer, const char* name, VkDescriptorPoolSize* sizes, uint32_t num_sizes, uint32_t max_sets, bool can_free) 
{
    pool->renderer = renderer;
    pool->max_sets = max_sets;
    pool->can_free = can_free;
    pool->name = string_intern(&renderer->strings, name, strlen(name));
    buff_add(pool->sizes_buf, num_sizes, renderer->allocator);
    for (uint32_t i = 0; i < num_sizes; i++)
        pool->sizes_buf[i] = sizes[i];
    slab_create(&pool->allocator, renderer->allocator, CHUNK_SIZE);
    slab_create(&pool->released_sets, renderer->allocator, CHUNK_SIZE);
    return pool;
}

VkDescriptorPool vk_descmgr_alloc_pool(vkdescmgr_t* pool, const char* name) 
{
    spin_lock(&pool->lock);
    VkDescriptorPool handle = VK_NULL_HANDLE;
    pool->num_pools++;
    if (list_empty(&pool->pools))
        handle = vk_logicaldevice_createpool(&pool->renderer->device, pool, name);
    else {
        struct vkdescpoolnode_t* first = list_pop(&pool->pools);
        handle = first->handle;
        vk_set_handle_name(pool->renderer->device.handle, handle, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
        slab_free(pool->allocator, first);
    }
    spin_unlock(&pool->lock);
    return handle;
}

static void vk_dpool_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud) 
{
    vkdescmgr_t* pool = (vkdescmgr_t*)ud;
    spin_lock(&pool->lock);
    vklogicaldevice_t* device = &renderer->device;
    TC_ASSERT(vkResetDescriptorPool(device->handle, handle, 0) == VK_SUCCESS, 
        "[Vulkan]: Error resetting descriptor pool");
    struct vkdescpoolnode_t* node = slab_alloc(pool->allocator);
    node->handle = handle;
    list_add_tail(&pool->pools, node);
    pool->num_pools--;
    spin_unlock(&pool->lock);
}

void vk_descmgr_release_pool(vkdescmgr_t* pool, VkDescriptorPool handle) 
{
    vk_release_handle(pool->renderer, vk_dpool_destroy_cb, handle, pool);
}

vkdescset_t vk_descmgr_alloc_set(vkdescmgr_t* pool, VkDescriptorSetLayout layout, const char* name) 
{
    spin_lock(&pool->lock);
    struct vkdescpoolnode_t* node = NULL;
    struct vkdescpoolnode_t* first = list_first(&pool->pools);
    VkDescriptorSet set = VK_NULL_HANDLE;
    list_foreach(&pool->pools, node) {
        set = vk_logicaldevice_createset(&pool->renderer->device, node->handle, layout, name);
        if (set != VK_NULL_HANDLE) {
            if (first != node) {
                VkDescriptorPool p = node->handle;
                node->handle = first->handle;
                first->handle = p;
            }
            pool->num_sets++;
            return (vkdescset_t) { .set = set, .pool = node->handle, .source = pool};
        }
    }
    node = slab_alloc(pool->allocator);
    node->handle = vk_logicaldevice_createpool(&pool->renderer->device, pool, "descriptor pool");
    list_add(&pool->pools, node);
    set = vk_logicaldevice_createset(&pool->renderer->device, node->handle, layout, name);
    TC_ASSERT(set != VK_NULL_HANDLE, "[Vulkan]: Failed to allocate descriptor set");
    pool->num_sets++;
    spin_unlock(&pool->lock);
    return (vkdescset_t) { .set = set, .pool = node->handle, .source = pool };
}

static void vk_dset_destroy_cb(vkrenderer_t* renderer, uint64_t handle, void* ud)
{
    vkdescset_t* set = (vkdescset_t*)ud;
    vkdescmgr_t* pool = set->source;
    if (pool) {
        spin_lock(&pool->lock);
        vkFreeDescriptorSets(renderer->device.handle, set->pool, 1, &set->set);
        pool->num_sets--;
        spin_unlock(&pool->lock);
        slab_free(pool->released_sets, set);
    }
}

void vk_descmgr_release_set(vkdescset_t* set) 
{
    vkdescset_t* copy = memcpy(
        slab_alloc(set->source->released_sets),
        set, 
        sizeof(vkdescset_t));
    vk_release_handle(set->source->renderer, vk_dset_destroy_cb, VK_NULL_HANDLE, copy);
    set->source = NULL; set->pool = VK_NULL_HANDLE; set->set = VK_NULL_HANDLE;
}

void vk_descmgr_destroy(vkdescmgr_t* pool) 
{
    TC_ASSERT(pool->num_pools == 0, "[Vulkan]: Not all descriptor pools are returned");
    string_release(&pool->renderer->strings, pool->name);
    buff_free(pool->sizes_buf, pool->renderer->allocator);
    slab_destroy(pool->allocator);
    slab_destroy(pool->released_sets);
}

vksetallocator_t vk_setallocator_init(vkdescmgr_t* pool, sid_t name) 
{
    return (vksetallocator_t) { .global_pool = pool, .name = name };
}

VkDescriptorSet vk_setallocator_alloc(vksetallocator_t* allocator, VkDescriptorSetLayout layout, const char* name) 
{
    vklogicaldevice_t* device = &allocator->global_pool->renderer->device;
    VkDescriptorSet handle = VK_NULL_HANDLE;
    if (!allocator->pools == NULL)
        handle = vk_logicaldevice_createset(device, buff_last(allocator->pools), layout, name);
    if (handle == VK_NULL_HANDLE) {
        buff_push(
            allocator->pools, 
            allocator->global_pool->renderer->allocator,
            vk_descmgr_alloc_pool(allocator->global_pool, "dynamic descriptor pool"));
        handle = vk_logicaldevice_createset(device, buff_last(allocator->pools), layout, name);
    }
    return handle;
}

void vk_setallocator_release(vksetallocator_t* allocator) 
{
    uint32_t num_pools = buff_count(allocator->pools);
    for (uint32_t i = 0; i < num_pools; i++) {
        vk_descmgr_release_pool(allocator->global_pool, allocator->pools[i]);
    }
    allocator->peak_count = max(allocator->peak_count, num_pools);
    buff_free(allocator->pools, allocator->global_pool->renderer->allocator);
}

void vk_setallocator_destroy(vksetallocator_t* allocator) 
{
    TC_ASSERT(buff_count(allocator->pools) == 0, "[Vulkan]: There are still pools not returned to the cache");
    TRACE(LOG_INFO, "[Vulkan]: Peak descriptor pool count of %s: %i", string_get(&allocator->global_pool->renderer->strings, allocator->name, NULL), allocator->peak_count);
}

static enum device_feature_state_t get_feature_support(enum device_feature_state_t state, bool supported, const char* name) 
{
    switch (state) {
    case DEVICE_FEATURE_STATE_DISABLED: return DEVICE_FEATURE_STATE_DISABLED;
    case DEVICE_FEATURE_STATE_ENABLED:
        if (supported) return DEVICE_FEATURE_STATE_ENABLED;
        else TRACE(LOG_ERROR, "[Vulkan]: %s not supported by this device", name);
    case DEVICE_FEATURE_STATE_OPTIONAL: return supported ? DEVICE_FEATURE_STATE_ENABLED : DEVICE_FEATURE_STATE_DISABLED;
    default:
        TRACE(LOG_ERROR, "[Vulkan]: Unexpected feature state");
        return DEVICE_FEATURE_STATE_DISABLED;
    }
}

struct vkstale_t {
    list_t;
    uint64_t handle;
    size_t val;
    vk_delete_func delete_func;
    void* ud;
};

VkPhysicalDeviceFeatures vk_query_device_features(vkrenderer_t* renderer)
{
    VkPhysicalDeviceFeatures devicefeatures = { 0 };

    VkPhysicalDeviceFeatures* features = &renderer->physical_device.features;

    devicefeatures.fullDrawIndexUint32 = features->fullDrawIndexUint32;

    vk_params* params = &renderer->params;
#define ENABLE_FEATURE(feature, state, name)                                                  \
    do {                                                                                      \
        state = get_feature_support(state, features->feature != VK_FALSE, name);              \
        devicefeatures.feature = state == DEVICE_FEATURE_STATE_ENABLED ? VK_TRUE : VK_FALSE;  \
    } while (0)
    enum device_feature_state_t cubearray = DEVICE_FEATURE_STATE_OPTIONAL;
    enum device_feature_state_t anisotropy = DEVICE_FEATURE_STATE_OPTIONAL;
    ENABLE_FEATURE(geometryShader, params->features.geometry_shaders, "Geometry shaders are");
    ENABLE_FEATURE(tessellationShader, params->features.tessellation, "Tessellation is");
    ENABLE_FEATURE(pipelineStatisticsQuery, params->features.pipeline_statistics_queries, "Pipeline statistics queries are");
    ENABLE_FEATURE(occlusionQueryPrecise, params->features.occlusion_queries, "Occlusion queries are");
    ENABLE_FEATURE(imageCubeArray, cubearray, "Image cube arrays are");
    ENABLE_FEATURE(fillModeNonSolid, params->features.fill_wireframe, "Wireframe fill is");
    ENABLE_FEATURE(samplerAnisotropy, anisotropy, "Anisotropic texture filtering is");
    ENABLE_FEATURE(depthBiasClamp, params->features.depth_bias_clamp, "Depth bias clamp is");
    ENABLE_FEATURE(depthClamp, params->features.depth_clamp, "Depth clamp is");
    ENABLE_FEATURE(independentBlend, params->features.independent_blend, "Independent blend is");
    ENABLE_FEATURE(dualSrcBlend, params->features.dual_source_blend, "Dual-source blend is");
    ENABLE_FEATURE(multiViewport, params->features.multi_viewport, "Multiviewport is");
    ENABLE_FEATURE(textureCompressionBC, params->features.texture_compression_BC, "BC texture compression is");
    ENABLE_FEATURE(vertexPipelineStoresAndAtomics, params->features.vertex_UAV_writes, "Vertex pipeline UAV writes and atomics are");
    ENABLE_FEATURE(fragmentStoresAndAtomics, params->features.pixel_UAV_writes, "Pixel UAV writes and atomics are");
    ENABLE_FEATURE(shaderStorageImageExtendedFormats, params->features.texture_UAV_formats, "Texture UAV extended formats are");
#undef ENABLE_FEATURE

    return devicefeatures;
}

void vk_renderer_device_init(vkrenderer_t* renderer, uint32_t* queue_idx)
{
    VkDeviceCreateInfo deviceinfo = { 0 };

    VkPhysicalDeviceFeatures features = vk_query_device_features(renderer);

    VkDeviceQueueCreateInfo queueinfo = { 0 };
    queueinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueinfo.queueFamilyIndex = vk_physicaldevice_find_queue_family(&renderer->physical_device, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    queueinfo.queueCount = 1;
    const float queuePriority = 1.0f;
    queueinfo.pQueuePriorities = &queuePriority;
    
    deviceinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceinfo.queueCreateInfoCount = 1;
    deviceinfo.pQueueCreateInfos = &queueinfo;
    deviceinfo.pEnabledFeatures = &features;

    const char** device_ext = NULL;
    buff_push(device_ext, renderer->allocator, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    buff_push(device_ext, renderer->allocator, VK_KHR_MAINTENANCE1_EXTENSION_NAME);

    struct extension_features ext_feat = renderer->physical_device.ext_features;
    struct extension_features enabled_ext_feat = { 0 };
    enabled_ext_feat.spirv15 = ext_feat.spirv15;

    vk_params* params = &renderer->params;
#define ENABLE_FEATURE(supported, feature, name)                                              \
    do {                                                                                      \
        params->features.feature = get_feature_support(params->features.feature, supported, name);      \
    } while (0)
    ENABLE_FEATURE(ext_feat.meshshader.taskShader != VK_FALSE && ext_feat.meshshader.meshShader != VK_FALSE, mesh_shaders, "Mesh shaders are");
    ENABLE_FEATURE(ext_feat.shader_float16int8.shaderFloat16 != VK_FALSE, shader_float16, "16-bit float shader operations are");
    ENABLE_FEATURE(ext_feat.shader_float16int8.shaderInt8 != VK_FALSE, shader_int8, "8-bit int shader operations are");
    ENABLE_FEATURE(ext_feat.storage16.storageBuffer16BitAccess != VK_FALSE, resource_buffer16, "16-bit resoure buffer access is");
    ENABLE_FEATURE(ext_feat.storage16.uniformAndStorageBuffer16BitAccess != VK_FALSE, uniform_buffer16, "16-bit uniform buffer access is");
    ENABLE_FEATURE(ext_feat.storage16.storageInputOutput16 != VK_FALSE, shader_io16, "16-bit shader inputs/outputs are");
    ENABLE_FEATURE(ext_feat.storage8.storageBuffer8BitAccess != VK_FALSE, resource_buffer8, "8-bit resoure buffer access is");
    ENABLE_FEATURE(ext_feat.storage8.uniformAndStorageBuffer8BitAccess != VK_FALSE, uniform_buffer8, "8-bit uniform buffer access is");
    ENABLE_FEATURE(ext_feat.accelstruct.accelerationStructure != VK_FALSE && ext_feat.raytracing_pipeline.rayTracingPipeline != VK_FALSE, raytracing, "Ray tracing is");
#undef ENABLE_FEATURE

    if (vk_instance_extension_enabled(&renderer->instance, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        void** next = &deviceinfo.pNext;
        if (params->features.mesh_shaders != DEVICE_FEATURE_STATE_DISABLED) {
            enabled_ext_feat.meshshader = ext_feat.meshshader;
            TC_ASSERT(enabled_ext_feat.meshshader.taskShader != VK_FALSE && enabled_ext_feat.meshshader.meshShader != VK_FALSE);
            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_NV_MESH_SHADER_EXTENSION_NAME),
                "VK_NV_mesh_shader extension must be supported as it has already been checked by VulkanPhysicalDevice and both taskShader and meshShader features are TRUE");
            buff_push(device_ext, renderer->allocator, VK_NV_MESH_SHADER_EXTENSION_NAME);
            *next = &enabled_ext_feat.meshshader;
            next = &enabled_ext_feat.meshshader.pNext;
        }
        if (params->features.shader_float16 != DEVICE_FEATURE_STATE_DISABLED || params->features.shader_int8 != DEVICE_FEATURE_STATE_DISABLED) {
            enabled_ext_feat.shader_float16int8 = ext_feat.shader_float16int8;
            TC_ASSERT(enabled_ext_feat.shader_float16int8.shaderFloat16 != VK_FALSE || enabled_ext_feat.shader_float16int8.shaderInt8 != VK_FALSE);
            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME),
                "VK_KHR_shader_float16_int8 extension must be supported as it has already been checked by VulkanPhysicalDevice and at least one of shaderFloat16 or shaderInt8 features is TRUE");
            buff_push(device_ext, renderer->allocator, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
            if (params->features.shader_float16 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.shader_float16int8.shaderFloat16 = VK_FALSE;
            if (params->features.shader_int8 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.shader_float16int8.shaderInt8 = VK_FALSE;
            *next = &enabled_ext_feat.shader_float16int8;
            next = &enabled_ext_feat.shader_float16int8.pNext;
        }
        bool ext_required = false;
        if (params->features.resource_buffer16 != DEVICE_FEATURE_STATE_DISABLED ||
            params->features.uniform_buffer16 != DEVICE_FEATURE_STATE_DISABLED ||
            params->features.shader_io16 != DEVICE_FEATURE_STATE_DISABLED) {
            enabled_ext_feat.storage16 = ext_feat.storage16;
            TC_ASSERT(params->features.resource_buffer16 == DEVICE_FEATURE_STATE_DISABLED || enabled_ext_feat.storage16.storageBuffer16BitAccess != VK_FALSE);
            TC_ASSERT(params->features.uniform_buffer16 == DEVICE_FEATURE_STATE_DISABLED || enabled_ext_feat.storage16.uniformAndStorageBuffer16BitAccess != VK_FALSE);
            TC_ASSERT(params->features.shader_io16 == DEVICE_FEATURE_STATE_DISABLED || enabled_ext_feat.storage16.storageInputOutput16 != VK_FALSE);
            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_16BIT_STORAGE_EXTENSION_NAME),
                "VK_KHR_16bit_storage must be supported as it has already been checked by VulkanPhysicalDevice and at least one of storageBuffer16BitAccess, uniformAndStorageBuffer16BitAccess, or storagePushConstant16 features is TRUE");
            buff_push(device_ext, renderer->allocator, VK_KHR_16BIT_STORAGE_EXTENSION_NAME);

            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME),
                "VK_KHR_storage_buffer_storage_class must be supported as it has already been checked by VulkanPhysicalDevice and at least one of storageBuffer8BitAccess or uniformAndStorageBuffer8BitAccess features is TRUE");
            ext_required = true;

            if (params->features.resource_buffer16 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.storage16.storageBuffer16BitAccess = VK_FALSE;
            if (params->features.uniform_buffer16 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.storage16.uniformAndStorageBuffer16BitAccess = VK_FALSE;
            if (params->features.shader_io16 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.storage16.storageInputOutput16 = VK_FALSE;
            *next = &enabled_ext_feat.storage16;
            next = &enabled_ext_feat.storage16.pNext;
        }
        if (params->features.resource_buffer8 != DEVICE_FEATURE_STATE_DISABLED || params->features.uniform_buffer8 != DEVICE_FEATURE_STATE_DISABLED) {
            enabled_ext_feat.storage8 = ext_feat.storage8;
            TC_ASSERT(params->features.resource_buffer8 == DEVICE_FEATURE_STATE_DISABLED || enabled_ext_feat.storage8.storageBuffer8BitAccess != VK_FALSE);
            TC_ASSERT(params->features.uniform_buffer8 == DEVICE_FEATURE_STATE_DISABLED || enabled_ext_feat.storage8.uniformAndStorageBuffer8BitAccess != VK_FALSE);
            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_8BIT_STORAGE_EXTENSION_NAME),
                "VK_KHR_8bit_storage must be supported as it has already been checked by VulkanPhysicalDevice and at least one of storageBuffer8BitAccess or uniformAndStorageBuffer8BitAccess features is TRUE");
            buff_push(device_ext, renderer->allocator, VK_KHR_8BIT_STORAGE_EXTENSION_NAME);

            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME),
                "VK_KHR_storage_buffer_storage_class must be supported as it has already been checked by VulkanPhysicalDevice and at least one of storageBuffer8BitAccess or uniformAndStorageBuffer8BitAccess features is TRUE");
            ext_required = true;
            if (params->features.resource_buffer8 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.storage8.storageBuffer8BitAccess = VK_FALSE;
            if (params->features.uniform_buffer8 == DEVICE_FEATURE_STATE_DISABLED)
                enabled_ext_feat.storage8.uniformAndStorageBuffer8BitAccess = VK_FALSE;
            *next = &enabled_ext_feat.storage8;
            next = &enabled_ext_feat.storage8.pNext;
        }
        if (ext_required) {
            TC_ASSERT(vk_physicaldevice_extension_supported(&renderer->physical_device, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME));
            buff_push(device_ext, renderer->allocator, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
        }
        if (params->features.raytracing != DEVICE_FEATURE_STATE_DISABLED) {
            if (!ext_feat.spirv15)
            {
                buff_push(device_ext, renderer->allocator, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
                buff_push(device_ext, renderer->allocator, VK_KHR_SPIRV_1_4_EXTENSION_NAME);
                enabled_ext_feat.spirv14 = ext_feat.spirv14;
                TC_ASSERT(ext_feat.spirv14);
            }
            buff_push(device_ext, renderer->allocator, VK_KHR_MAINTENANCE3_EXTENSION_NAME);
            buff_push(device_ext, renderer->allocator, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            buff_push(device_ext, renderer->allocator, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            buff_push(device_ext, renderer->allocator, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            buff_push(device_ext, renderer->allocator, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            buff_push(device_ext, renderer->allocator, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

            enabled_ext_feat.accelstruct = ext_feat.accelstruct;
            enabled_ext_feat.raytracing_pipeline = ext_feat.raytracing_pipeline;
            enabled_ext_feat.buffer_device_address = ext_feat.buffer_device_address;
            enabled_ext_feat.descriptor_indexing = ext_feat.descriptor_indexing;
            enabled_ext_feat.accelstruct.accelerationStructureCaptureReplay = false;
            enabled_ext_feat.accelstruct.accelerationStructureIndirectBuild = false;
            enabled_ext_feat.accelstruct.accelerationStructureHostCommands = false;
            enabled_ext_feat.accelstruct.descriptorBindingAccelerationStructureUpdateAfterBind = false;
            enabled_ext_feat.raytracing_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplay = false;
            enabled_ext_feat.raytracing_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = false;
            enabled_ext_feat.raytracing_pipeline.rayTracingPipelineTraceRaysIndirect = false;
            enabled_ext_feat.raytracing_pipeline.rayTraversalPrimitiveCulling = false;

            *next = &enabled_ext_feat.accelstruct;
            next = &enabled_ext_feat.accelstruct.pNext;
            *next = &enabled_ext_feat.raytracing_pipeline;
            next = &enabled_ext_feat.raytracing_pipeline.pNext;
            *next = &enabled_ext_feat.descriptor_indexing;
            next = &enabled_ext_feat.descriptor_indexing.pNext;
            *next = &enabled_ext_feat.buffer_device_address;
            next = &enabled_ext_feat.buffer_device_address.pNext;
        }
        *next = NULL;
    }
    deviceinfo.ppEnabledExtensionNames = device_ext;
    deviceinfo.enabledExtensionCount = buff_count(device_ext);

    *queue_idx = queueinfo.queueFamilyIndex;

    vk_logicaldevice_init(&renderer->device, &renderer->physical_device, &deviceinfo, &enabled_ext_feat, renderer->instance.allocator);
}

vkrenderer_t* vk_init(vk_params* params) 
{
    TC_ASSERT(sizeof(vk_poolsize_t) == sizeof(uint32_t) * 11);

    vkrenderer_t* renderer = tc_malloc(params->allocator, sizeof(vkrenderer_t));
    memset(renderer, 0, sizeof(vkrenderer_t));
    renderer->params = *params;
    renderer->allocator = params->allocator;
    renderer->strings = stringpool_init(params->allocator);
    slab_create(&renderer->stale_allocations, params->allocator, CHUNK_SIZE);
    slab_create(&renderer->fences, params->allocator, CHUNK_SIZE);
    slab_create(&renderer->pending_fences, params->allocator, CHUNK_SIZE);

    list_init(&renderer->release_queue);
    list_init(&renderer->stale_queue);

    params = &renderer->params;
    
    vk_instance_init(
        &renderer->instance,
        renderer,
        VK_API_VERSION_1_0, 
        params->enable_validation, 
        params->extensions, 
        params->num_extensions, 
        NULL);
    
    vk_physicaldevice_init(
        &renderer->physical_device,
        renderer,
        vk_instance_pick_device(&renderer->instance, params->adapter_id));

    uint32_t queue_idx;
    vk_renderer_device_init(renderer, &queue_idx);

    vk_cmdqueue_init(&renderer->cmdqueue, &renderer->device, queue_idx);

    vk_fbcache_init(&renderer->fbcache, renderer);

    vk_passcache_init(&renderer->rpcache, renderer);

    VkDescriptorPoolSize main_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, params->main_pool_size.num_separate_samplers },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, params->main_pool_size.num_combined_samplers },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, params->main_pool_size.num_sampled_images },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, params->main_pool_size.num_storage_images },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, params->main_pool_size.num_uniform_texel_buffers },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, params->main_pool_size.num_storage_texel_buffers },
        //{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, params.main_pool_size.num_uniform_buffers },
        //{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, params.main_pool_size.num_storage_buffers },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, params->main_pool_size.num_uniform_buffers },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, params->main_pool_size.num_storage_buffers },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, params->main_pool_size.num_input_attachments },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, params->main_pool_size.num_accel_structs }
    };
    vk_descmgr_init(
        &renderer->static_pool, 
        renderer, 
        "static descriptor pool", 
        main_sizes, 
        TC_COUNT(main_sizes), 
        params->main_pool_size.max_sets, 
        true);
    
    VkDescriptorPoolSize dynamic_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, params->dynamic_pool_size.num_separate_samplers },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, params->dynamic_pool_size.num_combined_samplers },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, params->dynamic_pool_size.num_sampled_images },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, params->dynamic_pool_size.num_storage_images },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, params->dynamic_pool_size.num_uniform_texel_buffers },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, params->dynamic_pool_size.num_storage_texel_buffers },
        //{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, params.dynamic_pool_size.num_uniform_buffers },
        //{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, params.dynamic_pool_size.num_storage_buffers },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, params->dynamic_pool_size.num_uniform_buffers },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, params->dynamic_pool_size.num_storage_buffers },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, params->dynamic_pool_size.num_input_attachments },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, params->dynamic_pool_size.num_accel_structs }
    };
    vk_descmgr_init(
        &renderer->dynamic_pool, 
        renderer, 
        "dynamic descriptor pool", 
        dynamic_sizes, 
        TC_COUNT(dynamic_sizes), 
        params->dynamic_pool_size.max_sets, 
        false);

    vk_cmdpoolmgr_init(
        &renderer->cmdpool_mgr, 
        renderer, 
        "transient command buffer pool manager", 
        renderer->cmdqueue.queue_family_idx, 
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    
    vk_memmanager_init(
        &renderer->memory_mgr, 
        renderer, 
        "global resource memory manager", 
        params->allocator, 
        params->device_page, 
        params->host_page, 
        params->device_reserve, 
        params->host_reserve);

    vk_heapmanager_init(
        &renderer->heap_mgr, 
        renderer, 
        params->allocator, 
        params->dynamic_heap_size);

    struct extension_properties props = renderer->physical_device.ext_properties;
    renderer->properties.shader_group_handle_size = props.raytracing_pipeline.shaderGroupHandleSize;
    renderer->properties.shader_group_base_alignment = props.raytracing_pipeline.shaderGroupBaseAlignment;
    renderer->properties.max_shader_record_stride = props.raytracing_pipeline.maxShaderGroupStride;
    renderer->properties.max_draw_mesh_tasks = props.meshshader.maxDrawMeshTasksCount;
    renderer->properties.max_ray_tracing_recusions = props.raytracing_pipeline.maxRayRecursionDepth;
    renderer->properties.max_ray_treads = props.raytracing_pipeline.maxRayDispatchInvocationCount;
    
    uint32_t vendorid = renderer->physical_device.properties.vendorID;
    uint32_t deviceid = renderer->physical_device.properties.deviceID;
    // Copy description
    for (size_t i = 0; i < _countof(renderer->caps.adapter.description) - 1 && renderer->physical_device.properties.deviceName[i] != 0; ++i)
        renderer->caps.adapter.description[i] = renderer->physical_device.properties.deviceName[i];
    renderer->caps.adapter.type = ADAPTER_TYPE_HARDWARE;
    renderer->caps.adapter.vendor = vendor_from_id(vendorid);
    renderer->caps.adapter.vendor_id = vendorid;
    renderer->caps.adapter.device_id = deviceid;

    VkPhysicalDeviceMemoryProperties memprops = renderer->physical_device.memory_properties;
    for (uint32_t i = 0; i < memprops.memoryHeapCount; i++) {
        uint32_t size = memprops.memoryHeaps[i].size;
        if (memprops.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            bool is_unified = false;
            for (uint32_t j = 0; j < memprops.memoryTypeCount; j++) {
                if (memprops.memoryTypes[j].heapIndex != i) continue;
                VkMemoryPropertyFlagBits uniflags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                if ((memprops.memoryTypes[j].propertyFlags & uniflags) == uniflags) {
                    is_unified = true;
                    if (memprops.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                        renderer->caps.adapter.unified_memory_cpu_access |= CPU_WRITE;
                    if (memprops.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
                        renderer->caps.adapter.unified_memory_cpu_access |= CPU_READ;
                }
            }
            if (is_unified) renderer->caps.adapter.unified_memory += size;
            else renderer->caps.adapter.device_local_memory += size;
        }
        else renderer->caps.adapter.host_visible_memory += size;
    }

    vkfence_t* queuefence = slab_alloc(renderer->fences);
    vk_fence_init(queuefence, renderer);
    vk_cmdqueue_setfence(&renderer->cmdqueue, queuefence);

    return renderer;
}

void vk_release_handle(vkrenderer_t* renderer, vk_delete_func func, uint64_t handle, void* ud) 
{
    spin_lock(&renderer->stale_lock);
    {
        struct vkstale_t* staleres = slab_alloc(renderer->stale_allocations);
        staleres->handle = handle;
        staleres->val = atomic_load(&renderer->next_cmd_val, MEMORY_ACQUIRE);
        staleres->delete_func = func;
        staleres->ud = ud;
        list_add_tail(&renderer->stale_queue, staleres);
    }
    spin_unlock(&renderer->stale_lock);
}

void vk_discard_handle(vkrenderer_t* renderer, vk_delete_func func, uint64_t handle, void* ud, size_t fenceval) 
{
    spin_lock(&renderer->release_lock);
    {
        struct vkstale_t* staleres = slab_alloc(renderer->stale_allocations);
        staleres->handle = handle;
        staleres->val = fenceval;
        staleres->delete_func = func;
        staleres->ud = ud;
        list_add_tail(&renderer->release_queue, staleres);
    }
    spin_unlock(&renderer->release_lock);
}

void vk_discard_stale_resources(vkrenderer_t* renderer, size_t cmdbufval, size_t fenceval) 
{
    spin_lock(&renderer->stale_lock);
    spin_lock(&renderer->release_lock);
    {
        while (!list_empty(&renderer->stale_queue)) {
            struct vkstale_t* staleres = (struct vkstale_t*)renderer->stale_queue.next;
            if (staleres->val <= cmdbufval) {
                list_pop(&renderer->stale_queue);
                list_add_tail(&renderer->release_queue, staleres);
                staleres->val = fenceval;
            }
            else break;
        }
    }
    spin_unlock(&renderer->release_lock);
    spin_unlock(&renderer->stale_lock);
}

void vk_purge(vkrenderer_t* renderer, size_t val) 
{
    spin_lock(&renderer->release_lock);
    {
        while (!list_empty(&renderer->release_queue)) {
            struct vkstale_t* staleres = (struct vkstale_t*)renderer->release_queue.next;
            if (staleres->val <= val) {
                staleres->delete_func(renderer, staleres->handle, staleres->ud);
                list_pop(&renderer->release_queue);
                slab_free(renderer->stale_allocations, staleres);
            }
            else break;
        }
    }
    spin_unlock(&renderer->release_lock);
}

void vk_cleanup(vkrenderer_t* renderer, bool forcerelease) {
    size_t val = forcerelease ? SIZE_MAX : vk_cmdqueue_completed(&renderer->cmdqueue);
    vk_purge(renderer, val);
}

void vk_idle_cmdqueue(vkrenderer_t* renderer, bool releaseresources) {
    uint64_t cmdval = 0, fenceval = 0;
    if (releaseresources)
         cmdval = atomic_fetch_add(&renderer->next_cmd_val, 1, MEMORY_ACQ_REL);
    fenceval = vk_cmdqueue_wait(&renderer->cmdqueue);
    if (releaseresources) {
        vk_discard_stale_resources(renderer, cmdval, fenceval);
        vk_purge(renderer, vk_cmdqueue_completed(&renderer->cmdqueue));
    }
}

void vk_idlegpu(vkrenderer_t* renderer) {
    vk_idle_cmdqueue(renderer, true);
    vk_logicaldevice_waitidle(&renderer->device);
    vk_memmanager_cleanup(&renderer->memory_mgr);
    vk_cleanup(renderer, false);
}

void vk_close(vkrenderer_t* renderer) {
    vk_heapmanager_destroy(&renderer->heap_mgr);
    vk_passcache_destroy(&renderer->rpcache);
    vk_idlegpu(renderer);
    vk_cleanup(renderer, true);
    //TODO: Check if pools and allocators are empty

    //vk_cmdqueue_destroy(&renderer->cmdqueue);

    tc_free(renderer->allocator, renderer, sizeof(vkrenderer_t));
}