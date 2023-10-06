/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/
#pragma once
#include "core.h"
#define TinyImageFormat_HAVE_UINTXX_T
#include <tinyimageformat.h>

#include "private/vkgraphics.h"

#include <GLFW/glfw3.h>

#if defined(ANDROID) || defined(SWITCH) || defined(TARGET_APPLE_ARM64)
#define USE_MSAA_RESOLVE_ATTACHMENTS
#endif

#ifdef TC_DEBUG
#define ENABLE_DEPENDENCY_TRACKER
#endif

typedef struct tc_allocator_i tc_allocator_i;

enum {
	NUM_BUFFER_FRAMES = 3,
	MAX_ATTRIBUTES = 16,
	MAX_RENDER_TARGET_ATTACHMENTS = 8,
	MAX_SHADER_STAGE_COUNT = 5,
	MAX_VERTEX_ATTRIBS = 15,
	MAX_VERTEX_BINDINGS = 15,
	MAX_SEMANTIC_NAME_LENGTH = 128,
	MAX_GPU_VENDOR_STRING_LENGTH = 256,
	MAX_MULTIPLE_GPUS = 4,
	MAX_PLANE_COUNT = 3,
	MAX_LINKED_GPUS = 4,
	MAX_UNLINKED_GPUS = 4,
	MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1,
	MAX_INSTANCE_EXTENSIONS = 64,
	MAX_DEVICE_EXTENSIONS = 64,
	MAX_SWAPCHAIN_IMAGES = 3,
	MAX_RESOURCE_NAME_LENGTH = 256
};

typedef enum {
	GPU_PRESET_NONE = 0,
	GPU_PRESET_OFFICE,    //This means unsupported
	GPU_PRESET_LOW,
	GPU_PRESET_MEDIUM,
	GPU_PRESET_HIGH,
	GPU_PRESET_ULTRA,
	GPU_PRESET_COUNT
} gpupreset_t;

inline gpupreset_t get_preset_level(const char* vendorid, const char* modelid, const char* revid)
{
	// TODO: implement preset config lookup
	return GPU_PRESET_MEDIUM;
}

inline const char* presettostr(gpupreset_t preset)
{
	switch (preset) {
	case GPU_PRESET_NONE: return "";
	case GPU_PRESET_OFFICE: return "office";
	case GPU_PRESET_LOW: return "low";
	case GPU_PRESET_MEDIUM: return "medium";
	case GPU_PRESET_HIGH: return "high";
	case GPU_PRESET_ULTRA: return "ultra";
	default: return NULL;
	}
}

typedef enum {
	RENDERER_UNKNOWN = 0,
	RENDERER_VULKAN,
} renderertype_t;

typedef enum {
	QUEUE_TYPE_GRAPHICS = 0,
	QUEUE_TYPE_TRANSFER,
	QUEUE_TYPE_COMPUTE,
	MAX_QUEUE_TYPE
} queuetype_t;

typedef enum {
	QUEUE_FLAG_NONE = 0x0,
	QUEUE_FLAG_DISABLE_GPU_TIMEOUT = 0x1,
	QUEUE_FLAG_INIT_MICROPROFILE = 0x2,
	MAX_QUEUE_FLAG = 0xFFFFFFFF
} queueflag_t;

typedef enum {
	QUEUE_PRIORITY_NORMAL,
	QUEUE_PRIORITY_HIGH,
	QUEUE_PRIORITY_GLOBAL_REALTIME,
	MAX_QUEUE_PRIORITY
} queuepriority_t;

typedef enum {
	LOAD_ACTION_DONTCARE,
	LOAD_ACTION_LOAD,
	LOAD_ACTION_CLEAR,
	MAX_LOAD_ACTION
} loadop_t;

typedef enum {
	STORE_ACTION_STORE,				// Store is the most common use case so keep that as default
	STORE_ACTION_DONTCARE,
	STORE_ACTION_NONE,
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	STORE_ACTION_RESOLVE_STORE,		// Resolve into resolveattachment and also store the MSAA attachment
	STORE_ACTION_RESOLVE_DONTCARE,	// Resolve into resolveattachment and discard MSAA attachment
#endif
	MAX_STORE_ACTION
} storeop_t;

typedef enum {
	RESOURCE_STATE_UNDEFINED = 0,
	RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
	RESOURCE_STATE_INDEX_BUFFER = 0x2,
	RESOURCE_STATE_RENDER_TARGET = 0x4,
	RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
	RESOURCE_STATE_DEPTH_WRITE = 0x10,
	RESOURCE_STATE_DEPTH_READ = 0x20,
	RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
	RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
	RESOURCE_STATE_SHADER_RESOURCE = 0x40 | 0x80,
	RESOURCE_STATE_STREAM_OUT = 0x100,
	RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
	RESOURCE_STATE_COPY_DEST = 0x400,
	RESOURCE_STATE_COPY_SOURCE = 0x800,
	RESOURCE_STATE_GENERIC_READ = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
	RESOURCE_STATE_PRESENT = 0x1000,
	RESOURCE_STATE_COMMON = 0x2000,
	RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE = 0x4000,
	RESOURCE_STATE_SHADING_RATE_SOURCE = 0x8000,
} resourcestate_t;

typedef enum {
	RESOURCE_MEMORY_USAGE_UNKNOWN = 0,		// No intended memory usage specified.
	RESOURCE_MEMORY_USAGE_GPU_ONLY = 1,		// Memory will be used on device only, no need to be mapped on host.
	RESOURCE_MEMORY_USAGE_CPU_ONLY = 2,		// Memory will be mapped on host. Could be used for transfer to device.
	RESOURCE_MEMORY_USAGE_CPU_TO_GPU = 3,	// Memory will be used for frequent (dynamic) updates from host and reads on device.
	RESOURCE_MEMORY_USAGE_GPU_TO_CPU = 4,	// Memory will be used for writing on device and readback on host.
	RESOURCE_MEMORY_USAGE_COUNT,
	RESOURCE_MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF
} resourcememoryusage_t;

typedef enum {
	INDIRECT_ARG_INVALID,
	INDIRECT_DRAW,
	INDIRECT_DRAW_INDEX,
	INDIRECT_DISPATCH,
	INDIRECT_VERTEX_BUFFER,
	INDIRECT_INDEX_BUFFER,
	INDIRECT_CONSTANT,
	INDIRECT_CONSTANT_BUFFER_VIEW,     		// only for dx
	INDIRECT_SHADER_RESOURCE_VIEW,     		// only for dx
	INDIRECT_UNORDERED_ACCESS_VIEW,    		// only for dx
	INDIRECT_COMMAND_BUFFER,            	// metal ICB
	INDIRECT_COMMAND_BUFFER_RESET,      	// metal ICB reset
	INDIRECT_COMMAND_BUFFER_OPTIMIZE    	// metal ICB optimization
} indirectargtype_t;

typedef enum {
	DESCRIPTOR_TYPE_UNDEFINED = 0,
	DESCRIPTOR_TYPE_SAMPLER = 0x1,				
	DESCRIPTOR_TYPE_TEXTURE = 0x2,						// SRV Read only texture
	DESCRIPTOR_TYPE_RW_TEXTURE = 0x4,					// UAV Texture
	DESCRIPTOR_TYPE_BUFFER = 0x8,						// SRV Read only buffer
	DESCRIPTOR_TYPE_BUFFER_RAW = (DESCRIPTOR_TYPE_BUFFER | (DESCRIPTOR_TYPE_BUFFER << 1)),
	DESCRIPTOR_TYPE_RW_BUFFER = 0x20,					// UAV Buffer
	DESCRIPTOR_TYPE_RW_BUFFER_RAW = (DESCRIPTOR_TYPE_RW_BUFFER | (DESCRIPTOR_TYPE_RW_BUFFER << 1)),
	DESCRIPTOR_TYPE_UNIFORM_BUFFER = 0x80,				// Uniform buffer
	DESCRIPTOR_TYPE_ROOT_CONSTANT = 0x100,				// Push constant / Root constant
	DESCRIPTOR_TYPE_VERTEX_BUFFER = 0x200,				// IA
	DESCRIPTOR_TYPE_INDEX_BUFFER = 0x400,
	DESCRIPTOR_TYPE_INDIRECT_BUFFER = 0x800,
	DESCRIPTOR_TYPE_TEXTURE_CUBE = (DESCRIPTOR_TYPE_TEXTURE | (DESCRIPTOR_TYPE_INDIRECT_BUFFER << 1)),	// Cubemap SRV
	DESCRIPTOR_TYPE_RENDER_TARGET_MIP_SLICES = 0x2000,	// RTV / DSV per mip slice
	DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES = 0x4000,// RTV / DSV per array slice
	DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES = 0x8000,// RTV / DSV per depth slice
	DESCRIPTOR_TYPE_RAY_TRACING = 0x10000,
	DESCRIPTOR_TYPE_INDIRECT_COMMAND_BUFFER = 0x20000,
	DESCRIPTOR_TYPE_INPUT_ATTACHMENT = 0x40000,			// Subpass input (descriptor type only available in Vulkan)
	DESCRIPTOR_TYPE_TEXEL_BUFFER = 0x80000,
	DESCRIPTOR_TYPE_RW_TEXEL_BUFFER = 0x100000,
	DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 0x200000,
	DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE = 0x400000,	// Khronos extension ray tracing
	DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_BUILD_INPUT = 0x800000,
	DESCRIPTOR_TYPE_SHADER_DEVICE_ADDRESS = 0x1000000,
	DESCRIPTOR_TYPE_SHADER_BINDING_TABLE = 0x2000000,
} descriptortype_t;

typedef enum {
	SAMPLE_COUNT_1 = 1,
	SAMPLE_COUNT_2 = 2,
	SAMPLE_COUNT_4 = 4,
	SAMPLE_COUNT_8 = 8,
	SAMPLE_COUNT_16 = 16,
	SAMPLE_COUNT_COUNT = 5,
} samplecount_t;

typedef enum {
	SHADER_STAGE_NONE = 0,
	SHADER_STAGE_VERT = 0X00000001,
	SHADER_STAGE_TESC = 0X00000002,
	SHADER_STAGE_TESE = 0X00000004,
	SHADER_STAGE_GEOM = 0X00000008,
	SHADER_STAGE_FRAG = 0X00000010,
	SHADER_STAGE_COMP = 0X00000020,
	SHADER_STAGE_RAYTRACING = 0X00000040,
	SHADER_STAGE_ALL_GRAPHICS = SHADER_STAGE_VERT | SHADER_STAGE_TESC | SHADER_STAGE_TESE | SHADER_STAGE_GEOM | SHADER_STAGE_FRAG,
	SHADER_STAGE_HULL = SHADER_STAGE_TESC,
	SHADER_STAGE_DOMN = SHADER_STAGE_TESE,
	SHADER_STAGE_COUNT = 7,
} shaderstage_t;

// Primitive rasterizer type
typedef enum {
	PRIMITIVE_TOPO_POINT_LIST,
	PRIMITIVE_TOPO_LINE_LIST,
	PRIMITIVE_TOPO_LINE_STRIP,
	PRIMITIVE_TOPO_TRI_LIST,
	PRIMITIVE_TOPO_TRI_STRIP,
	PRIMITIVE_TOPO_PATCH_LIST,
	PRIMITIVE_TOPO_COUNT,
} primitivetopology_t;

typedef enum { INDEX_TYPE_UINT32, INDEX_TYPE_UINT16 } indextype_t;

typedef enum {
	SEMANTIC_UNDEFINED = 0,
	SEMANTIC_POSITION,
	SEMANTIC_NORMAL,
	SEMANTIC_COLOR,
	SEMANTIC_TANGENT,
	SEMANTIC_BITANGENT,
	SEMANTIC_JOINTS,
	SEMANTIC_WEIGHTS,
	SEMANTIC_SHADING_RATE,
	SEMANTIC_TEXCOORD0,
	SEMANTIC_TEXCOORD1,
	SEMANTIC_TEXCOORD2,
	SEMANTIC_TEXCOORD3,
	SEMANTIC_TEXCOORD4,
	SEMANTIC_TEXCOORD5,
	SEMANTIC_TEXCOORD6,
	SEMANTIC_TEXCOORD7,
	SEMANTIC_TEXCOORD8,
	SEMANTIC_TEXCOORD9,
} shadersemantic_t;

// Blend factor to use in blend function per rgba channels
typedef enum {
	BLEND_ZERO,
	BLEND_ONE,
	BLEND_SRC_COLOR,
	BLEND_INV_SRC_COLOR,
	BLEND_DST_COLOR,
	BLEND_INV_DST_COLOR,
	BLEND_SRC_ALPHA,
	BLEND_INV_SRC_ALPHA,
	BLEND_DST_ALPHA,
	BLEND_INV_DST_ALPHA,
	BLEND_SRC_ALPHA_SATURATE,
	BLEND_CONSTANT,
	BLEND_INV_CONSTANT,
	BLEND_FACTOR_MAX,
} blendfactor_t;


// Blend function to describe how to blend colors of the render target with the output of the fragment shader
typedef enum { BLEND_ADD, BLEND_SUB, BLEND_REV_SUB, BLEND_MIN, BLEND_MAX, MAX_BLEND_MODES} blendmode_t;

typedef enum { CMP_NEVER, CMP_LT, CMP_EQ, CMP_LEQ, CMP_GT, CMP_NEQ, CMP_GEQ, CMP_ALWAYS, MAX_COMPARE_MODES } comparemode_t;

// Function applied on stencil buffer when rendering to it
typedef enum {
	STENCIL_KEEP,
	STENCIL_ZERO,
	STENCIL_REPLACE,
	STENCIL_INVERT,
	STENCIL_INCR_WRAP,
	STENCIL_DECR_WRAP,
	STENCIL_INCR,
	STENDIL_DECR,
	MAX_STENCIL_OPS
} stencilop_t;

// Which channels can be writtin to
typedef enum {
	COLOR_MASK_NONE = 0,
	COLOR_MASK_RED = 0x1,
	COLOR_MASK_GREEN = 0x2,
	COLOR_MASK_BLUE = 0x4,
	COLOR_MASK_ALPHA = 0x8,
	COLOR_MASK_ALL = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
} colormask_t;

// Blend states are always attached to one of the eight or more render targets that are in a MRT
typedef enum {
	BLEND_STATE_TARGET_0 = 0x1,
	BLEND_STATE_TARGET_1 = 0x2,
	BLEND_STATE_TARGET_2 = 0x4,
	BLEND_STATE_TARGET_3 = 0x8,
	BLEND_STATE_TARGET_4 = 0x10,
	BLEND_STATE_TARGET_5 = 0x20,
	BLEND_STATE_TARGET_6 = 0x40,
	BLEND_STATE_TARGET_7 = 0x80,
	BLEND_STATE_TARGET_ALL = 0xFF,
} blendstatetargets_t;

// Rasterization cull modes
typedef enum { CULL_MODE_NONE, CULL_MODE_BACK, CULL_MODE_FRONT, CULL_MODE_BOTH, MAX_CULL_MODES } cullmode_t;

// Front of triangle based on vertices being clockwise vs counterclockwise
typedef enum { FRONT_FACE_CCW, FRONT_FACE_CW } frontface_t;

// Rasterization fill modes
typedef enum { FILL_SOLID, FILL_WIRE, MAX_FILL_MODES} fillmode_t;

typedef enum { PIPELINE_TYPE_UNDEFINED, PIPELINE_TYPE_COMPUTE, PIPELINE_TYPE_GRAPHICS, PIPELINE_TYPE_RAYTRACING, PIPELINE_TYPE_COUNT } pipelinetype_t;

// Texture sampling filter mode:
typedef enum { FILTER_NEAREST, FILTER_LINEAR, FILTER_MAX } filtermode_t;

typedef enum { WRAP_CLAMP, WRAP_REPEAT, WRAP_MIRROR, WRAP_BORDER, WRAP_MODE_MAX } addressmode_t;

typedef enum { MIPMAP_MODE_NEAREST, MIPMAP_MODE_LINEAR } mipmapmode_t;

typedef union clearvalue_s {
	struct { float r, g, b, a; };
	struct { float depth; uint32_t stencil; };
} clearvalue_t;

typedef enum {
	SHADING_RATE_NOT_SUPPORTED = 0x0,
	SHADING_RATE_FULL = 0x1,
	SHADING_RATE_HALF = 0x2,
	SHADING_RATE_QUARTER = 0x4,
	SHADING_RATE_EIGHTH = 0x8,
	SHADING_RATE_1X2 = 0x10,
	SHADING_RATE_2X1 = 0x20,
	SHADING_RATE_2X4 = 0x40,
	SHADING_RATE_4X2 = 0x80,
} shadingrate_t;

typedef enum {
	SHADING_RATE_COMBINER_PASSTHROUGH = 0,
	SHADING_RATE_COMBINER_OVERRIDE = 1,
	SHADING_RATE_COMBINER_MIN = 2,
	SHADING_RATE_COMBINER_MAX = 3,
	SHADING_RATE_COMBINER_SUM = 4,
} shadingratecombiner_t;

typedef enum {
	SHADING_RATE_CAPS_NOT_SUPPORTED = 0x0,
	SHADING_RATE_CAPS_PER_DRAW = 0x1,
	SHADING_RATE_CAPS_PER_TILE = 0x2,
} shadingratecaps_t;

typedef enum {
	BUFFER_CREATION_FLAG_NONE = 0x1,							// Default flag (Buffer will use aliased memory, buffer will not be cpu accessible until map_buffer is called)
	BUFFER_CREATION_FLAG_OWN_MEMORY_BIT = 0x2,					// Buffer will allocate its own memory (COMMITTED resource)
	BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT = 0x4,				// Buffer will be persistently mapped
	BUFFER_CREATION_FLAG_ESRAM = 0x8,							// Use ESRAM to store this buffer
	BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION = 0x10,	// Flag to specify not to allocate descriptors for the resource
	BUFFER_CREATION_FLAG_HOST_VISIBLE = 0x100,					// Memory Host Flags
	BUFFER_CREATION_FLAG_HOST_COHERENT = 0x200,
} buffercreationflags_t;

typedef enum {
	TEXTURE_CREATION_FLAG_NONE = 0,								// Default flag (Texture will use default allocation strategy decided by the api specific allocator)
	TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT = 0x01,				// Texture will allocate its own memory (COMMITTED resource)
	TEXTURE_CREATION_FLAG_EXPORT_BIT = 0x02,					// Texture will be allocated in memory which can be shared among multiple processes
	TEXTURE_CREATION_FLAG_EXPORT_ADAPTER_BIT = 0x04,			// Texture will be allocated in memory which can be shared among multiple gpus
	TEXTURE_CREATION_FLAG_IMPORT_BIT = 0x08,					// Texture will be imported from a handle created in another process
	TEXTURE_CREATION_FLAG_ESRAM = 0x10,							// Use ESRAM to store this texture
	TEXTURE_CREATION_FLAG_ON_TILE = 0x20,						// Use on-tile memory to store this texture
	TEXTURE_CREATION_FLAG_NO_COMPRESSION = 0x40,				// Prevent compression meta data from generating
	TEXTURE_CREATION_FLAG_FORCE_2D = 0x80,						// Force 2D instead of automatically determining dimension based on width, height, depth
	TEXTURE_CREATION_FLAG_FORCE_3D = 0x100,						// Force 3D instead of automatically determining dimension based on width, height, depth
	TEXTURE_CREATION_FLAG_ALLOW_DISPLAY_TARGET = 0x200,			// Display target
	TEXTURE_CREATION_FLAG_SRGB = 0x400,							// Create an sRGB texture.
	TEXTURE_CREATION_FLAG_NORMAL_MAP = 0x800,					// Create a normal map texture
	TEXTURE_CREATION_FLAG_FAST_CLEAR = 0x1000,					// Fast clear
	TEXTURE_CREATION_FLAG_FRAG_MASK = 0x2000,					// Fragment mask
    TEXTURE_CREATION_FLAG_VR_MULTIVIEW = 0x4000,				// Doubles the amount of array layers of the texture when rendering VR. Also forces the texture to be a 2D Array texture.
    TEXTURE_CREATION_FLAG_VR_FOVEATED_RENDERING = 0x8000,		// Binds the FFR fragment density if this texture is used as a render target.
	TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT = 0x10000,	// Creates resolve attachment for auto resolve (MSAA on tiled architecture - Resolve can be done on tile through render pass)
} texturecreationflags_t;

typedef enum {
	TEXTURE_1D,
	TEXTURE_2D,
	TEXTURE_2DMS,
	TEXTURE_3D,
	TEXTURE_CUBE,
	TEXTURE_1D_ARRAY,
	TEXTURE_2D_ARRAY,
	TEXTURE_2DMS_ARRAY,
	TEXTURE_CUBE_ARRAY,
	TEXTURE_DIM_COUNT,
	TEXTURE_UNDEFINED,
} texturetype_t;

typedef enum { QUERY_TYPE_TIMESTAMP, QUERY_TYPE_PIPELINE_STATS, QUERY_TYPE_OCCLUSION, QUERY_TYPE_COUNT } querytype_t;

typedef enum { SAMPLER_RANGE_FULL = 0, SAMPLER_RANGE_NARROW = 1 } samplerrange_t;

typedef enum { SAMPLE_LOCATION_COSITED, SAMPLE_LOCATION_MIDPOINT } samplelocation_t;

typedef enum {
	SAMPLER_MODEL_CONVERSION_RGB_IDENTITY,
	SAMPLER_MODEL_CONVERSION_YCBCR_IDENTITY,
	SAMPLER_MODEL_CONVERSION_YCBCR_709,
	SAMPLER_MODEL_CONVERSION_YCBCR_601,
	SAMPLER_MODEL_CONVERSION_YCBCR_2020,
} samplermodelconversion_t;

typedef enum {
	DESCRIPTOR_UPDATE_FREQ_NONE,
	DESCRIPTOR_UPDATE_FREQ_PER_FRAME,
	DESCRIPTOR_UPDATE_FREQ_PER_BATCH,
	DESCRIPTOR_UPDATE_FREQ_PER_DRAW,
	DESCRIPTOR_UPDATE_FREQ_COUNT,
} descriptorupdatefreq_t;

typedef enum {
	shader_target_5_1,
	shader_target_6_0,
	shader_target_6_1,
	shader_target_6_2,
	shader_target_6_3,    //required for Raytracing
	shader_target_6_4,    //required for VRS
} shadertarget_t;

typedef enum {
	WAVE_OPS_SUPPORT_FLAG_NONE = 0x0,
	WAVE_OPS_SUPPORT_FLAG_BASIC_BIT = 0x00000001,
	WAVE_OPS_SUPPORT_FLAG_VOTE_BIT = 0x00000002,
	WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT = 0x00000004,
	WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT = 0x00000008,
	WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT = 0x00000010,
	WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT = 0x00000020,
	WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT = 0x00000040,
	WAVE_OPS_SUPPORT_FLAG_QUAD_BIT = 0x00000080,
	WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV = 0x00000100,
	WAVE_OPS_SUPPORT_FLAG_ALL = 0x7FFFFFFF
} waveopssupportflags_t;

typedef enum { GPU_MODE_SINGLE, GPU_MODE_LINKED, GPU_MODE_UNLINKED } gpumode_t;

typedef enum {
	ROOT_SIGNATURE_FLAG_NONE = 0,			// Default flag
	ROOT_SIGNATURE_FLAG_LOCAL_BIT = 0x1,	// Local root signature used mainly in raytracing shaders
} rootsignatureflags_t;

typedef enum {
	MARKER_TYPE_DEFAULT = 0x0,
	MARKER_TYPE_IN = 0x1,
	MARKER_TYPE_OUT = 0x2,
	MARKER_TYPE_IN_OUT = 0x3,
} markertype_t;

typedef enum { FENCE_COMPLETE, FENCE_INCOMPLETE, FENCE_NOTSUBMITTED } fencestatus_t;

typedef enum { VERTEX_ATTRIB_RATE_VERTEX, VERTEX_ATTRIB_RATE_INSTANCE, VERTEX_ATTRIB_RATE_COUNT } vertexattribrate_t;


typedef struct buffer_s buffer_t;
typedef struct texture_s texture_t;
typedef struct rendertarget_s rendertarget_t;
typedef struct shader_s shader_t;
typedef struct descidxmap_s descidxmap_t;
typedef struct accelstruct_s accelstruct_t;
typedef struct queue_s queue_t;
typedef struct raytracing_s raytracing_t;
typedef struct renderercontext_s renderercontext_t;
typedef struct renderer_s renderer_t;

/* Resource bariers */
struct barrier_s {
	resourcestate_t currentstate;
	resourcestate_t newstate;
	uint8_t beginonly : 1;
	uint8_t endonly : 1;
	uint8_t acquire : 1;
	uint8_t release : 1;
	uint8_t queuetype : 5;
};

struct subresourcebarrier_s {
	uint8_t subresourcebarrier : 1;	// Specifiy whether following barrier targets particular subresource
	uint8_t miplevel : 7;			// Ignored if subresourcebarrier is false
	uint16_t arraylayer;			// Ignored if subresourcebarrier is false
};

typedef struct { buffer_t* buf; struct barrier_s; } bufbarrier_t;
typedef struct { texture_t* tex; struct barrier_s; struct subresourcebarrier_s; } texbarrier_t;
typedef struct { rendertarget_t* rt; struct barrier_s; struct subresourcebarrier_s; } rtbarrier_t;

typedef struct { uint64_t offset; uint64_t size; } range_t;
typedef struct { uint32_t offset; uint32_t size; } range32_t;

typedef struct {
	querytype_t type;
	uint32_t  querycount;
	uint32_t  nodeIndex;
} querypooldesc_t;

typedef struct { uint32_t index; } querydesc_t;

typedef struct {
#if defined(VULKAN)
	struct {
		VkQueryPool querypool;
		VkQueryType type;
	} vk;
#endif
	uint32_t count;
} querypool_t;

// Data structure holding necessary info to create a Buffer
typedef struct {
	uint64_t size;								// Size of the buffer (in bytes)
	struct buffer_s* counterbuffer;				// Set this to specify a counter buffer for this buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
	uint64_t first;								// Index of the first element accessible by the SRV/UAV (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
	uint64_t count;								// Number of elements in the buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
	uint64_t stride;							// Size of each element (in bytes) in the buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
	const char* name;							// Debug name used in gpu profile
	uint32_t* sharednodeindices;
	uint32_t alignment;							// Alignment
	resourcememoryusage_t memusage;				// Decides which memory heap buffer will be used (default, upload, readback)
	buffercreationflags_t flags;				// Creation flags of the buffer
	queuetype_t queuetype;						// What type of queue the buffer is owned by
	resourcestate_t state;						// What state will the buffer get created in
	indirectargtype_t ICBdrawtype;				// ICB draw type
	uint32_t ICBmaxcommands;					// ICB max vertex buffers slots count
	TinyImageFormat format;						// Format of the buffer (applicable to typed storage buffers (Buffer<T>)
	descriptortype_t descriptors;				// Flags specifying the suitable usage of this buffer (Uniform buffer, Vertex Buffer, Index Buffer,...)
	uint32_t nodeidx;							// The index of the GPU in SLI/Cross-Fire that owns this buffer, or the Renderer index in unlinked mode.
	uint32_t sharednodeidxcount;
} bufferdesc_t;

typedef struct ALIGNED(buffer_s, 64) {
	void* data;		// CPU address of the mapped buffer (applicable to buffers created in CPU accessible heaps (CPU, CPU_TO_GPU, GPU_TO_CPU)
#if defined(VULKAN)
		struct {
			VkBuffer buffer;					// Native handle of the underlying resource
			VkBufferView storagetexelview;		// Buffer view
			VkBufferView uniformtexelview;
			struct VmaAllocation_T* alloc;		// Contains resource allocation info such as parent heap, offset in heap
			uint64_t offset;
		} vk;
#endif
	uint64_t size : 32;
	uint64_t descriptors : 20;
	uint64_t memusage : 3;
	uint64_t nodeidx : 4;
} buffer_t;
TC_COMPILE_ASSERT(sizeof(buffer_t) == 8 * sizeof(uint64_t));	// One cache line

// Data structure holding necessary info to create a Texture
typedef struct {
	clearvalue_t clearval;			// Optimized clear value (recommended to use this same value when clearing the rendertarget)
	const void* nativehandle;		// Pointer to native texture handle if the texture does not own underlying resource
	const char* name;				// Debug name used in gpu profile
	uint32_t* sharednodeindices;	// GPU indices to share this texture
#if defined(VULKAN)
	VkSamplerYcbcrConversionInfo* samplerycbcrconversioninfo;
#endif
	texturecreationflags_t flags;	// Texture creation flags (decides memory allocation strategy, sharing access,...)
	uint32_t width;
	uint32_t height;
	uint32_t depth;					// Depth (Should be 1 if not a mType is not TEXTURE_TYPE_3D)
	uint32_t arraysize;				// Texture array size (Should be 1 if texture is not a texture array or cubemap)
	uint32_t miplevels;				// Number of mip levels
	samplecount_t samplecount;		// Number of multisamples per pixel (currently Textures created with mUsage TEXTURE_USAGE_SAMPLED_IMAGE only support SAMPLE_COUNT_1)
	uint32_t samplequality;			// The image quality level. The higher the quality, the lower the performance. The valid range is between zero and the value appropriate for samplecount
	TinyImageFormat format;			// Image format
	resourcestate_t state;			// What state will the texture get created in
	descriptortype_t descriptors;	// Descriptor creation
	uint32_t sharednodeidxcount;	// Number of GPUs to share this texture
	uint32_t nodeidx;				// GPU which will own this texture
} texturedesc_t;

// Virtual texture page as a part of the partially resident texture
typedef struct {
	uint32_t miplevel;										// Miplevel for this page
	uint32_t layer;											// Array layer for this page
	uint32_t index;											// Index for this page
#if defined(VULKAN)
		struct {
			void* alloc;									// Allocation and resource for this tile
			VkSparseImageMemoryBind imagemembind;			// Sparse image memory bind for this page
			VkDeviceSize size;								// Byte size for this page
		} vk;
#endif
} virtualtexpage_t;

typedef struct {
#if defined(VULKAN)
		struct {
			void* pool;										// GPU memory pool for tiles
			VkSparseImageMemoryBind* sparseimagemembinds;	// Sparse image memory bindings of all memory-backed virtual tables
			VkSparseMemoryBind* opaquemembinds;				// Sparse opaque memory bindings for the mip tail (if present)
			void** opaquemembindalloc;						// GPU allocations for opaque memory binds (mip tail)
			void** pendingdeletedallocs;					// Pending allocation deletions
			uint32_t sparsememtypebits;						// Memory type bits for sparse texture's memory
			uint32_t opaquemembindscount;					// Number of opaque memory binds
		} vk;
#endif
	virtualtexpage_t* pages;								// Virtual Texture members
	buffer_t* pendingdeletedbufs;							// Pending intermediate buffer deletions
	uint32_t* pendingdeletedbufcount;						// Pending intermediate buffer deletions count
	uint32_t* pendingdeletedalloccount;						// Pending allocation deletions count
	buffer_t* readbackbuf;									// Readback buffer, must be filled by app. Size = readbackbufsize * imagecount
	void* data;												// Original Pixel image data
	uint32_t virtualpages;									// Total pages count
	uint32_t virtualpagealive;								// Alive pages count
	uint32_t readbackbufsize;								// Size of the readback buffer per image
	uint32_t pagevisibilitybufsize;							// Size of the readback buffer per image
	uint16_t sparsevtpagewidth;								// Sparse Virtual Texture Width
	uint16_t sparsevtpageheight;							// Sparse Virtual Texture Height
	uint8_t tiledmiplevels;									// Number of mip levels that are tiled
	uint8_t pendingdeletions;								// Size of the pending deletion arrays in image count (highest currentimage + 1)
} virtualtexture_t;

typedef struct ALIGNED(texture_s, 64) {
	union {
#if defined(VULKAN)
		struct {
			VkImageView SRVdescriptor;						// Opaque handle used by shaders for doing read/write operations on the texture
			VkImageView* UAVdescriptors;					// Opaque handle used by shaders for doing read/write operations on the texture
			VkImageView SRVstencildescriptor;				// Opaque handle used by shaders for doing read/write operations on the texture
			VkImage image;									// Native handle of the underlying resource
			union {
				struct VmaAllocation_T* alloc;				// Contains resource allocation info such as parent heap, offset in heap
				VkDeviceMemory devicemem;
			};
		} vk;
#endif
	};
	virtualtexture_t* vt;
	uint32_t width : 16;			// Current state of the buffer
	uint32_t height : 16;
	uint32_t depth : 16;
	uint32_t miplevels : 5;
	uint32_t extraarraysize : 11;
	uint32_t format : 8;
	uint32_t aspectmask : 4;		// Flags specifying which aspects (COLOR,DEPTH,STENCIL) are included in the imageView
	uint32_t nodeidx : 4;
	uint32_t samplecount : 5;
	uint32_t uav : 1;
	uint32_t ownsimage : 1;			// This value will be false if the underlying resource is not owned by the texture (swapchain textures,...)
	uint32_t lazilyallocated : 1;
} texture_t;
TC_COMPILE_ASSERT(sizeof(texture_t) == 8 * sizeof(uint64_t));	// One cache line

typedef struct {
	texturecreationflags_t flags;	// Texture creation flags (decides memory allocation strategy, sharing access,...)
	uint32_t width;
	uint32_t height;
	uint32_t depth;					// Depth (Should be 1 if not a mType is not TEXTURE_TYPE_3D)
	uint32_t arraysize;				// Texture array size (Should be 1 if texture is not a texture array or cubemap)
	uint32_t miplevels;				// Number of mip levels
	samplecount_t samplecount;		// MSAA
	TinyImageFormat format;			// Internal image format
	resourcestate_t state;			// What state will the texture get created in
	clearvalue_t clearval;			// Optimized clear value (recommended to use this same value when clearing the rendertarget)
	uint32_t samplequality;			// The image quality level. The higher the quality, the lower the performance. The valid range is between zero and the value appropriate for samplecount
	descriptortype_t descriptors;	// Descriptor creation
	const void* nativehandle;
	const char* name;				// Debug name used in gpu profile
	uint32_t* sharednodeindices;	// GPU indices to share this texture
	uint32_t sharednodeidxcount;	// Number of GPUs to share this texture
	uint32_t nodeidx;				// GPU which will own this texture
} rendertargetdesc_t;

typedef struct ALIGNED(rendertarget_s, 64) {
	texture_t* tex;
	union {
#if defined(VULKAN)
		struct {
			VkImageView   descriptor;
			VkImageView*  slicedescriptors;
			uint32_t      id;
		} vk;
#endif
	};
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	rendertarget_s* resolveattachment;
#endif
	clearvalue_t clearval;
	uint32_t arraysize : 16;
	uint32_t depth : 16;
	uint32_t width : 16;
	uint32_t height : 16;
	uint32_t descriptors : 20;
	uint32_t miplevels : 10;
	uint32_t samplequality : 5;
	TinyImageFormat format;
	samplecount_t samplecount;
    bool vr;
    bool vrfoveatedrendering;
} rendertarget_t;
TC_COMPILE_ASSERT(sizeof(rendertarget_t) <= 32 * sizeof(uint64_t));

typedef struct {
	loadop_t loadcolor[MAX_RENDER_TARGET_ATTACHMENTS];
	loadop_t loaddepth;
	loadop_t loadstencil;
	clearvalue_t clearcolors[MAX_RENDER_TARGET_ATTACHMENTS];
	clearvalue_t cleardepth;
	storeop_t storecolor[MAX_RENDER_TARGET_ATTACHMENTS];
	storeop_t storedepth;
	storeop_t storestencil;
} loadopsdesc_t;

typedef struct {
	filtermode_t minfilter;
	filtermode_t magfilter;
	mipmapmode_t mipmapmode;
	addressmode_t u;
	addressmode_t v;
	addressmode_t w;
	float mipLODbias;
	bool setLODrange;
	float minLOD;
	float maxLOD;
	float maxanisotropy;
	comparemode_t compareop;
#if defined(VULKAN)
	struct {
		TinyImageFormat format;
		samplermodelconversion_t model;
		samplerrange_t range;
		samplelocation_t chromaoffsetX;
		samplelocation_t chromaoffsetY;
		filtermode_t chromafilter;
		bool forceexplicitreconstruction;
	} vk;
#endif
} samplerdesc_t;

typedef struct ALIGNED(sampler_s, 16) {
	union {
#if defined(VULKAN)
		struct {
			VkSampler sampler;
			VkSamplerYcbcrConversion ycbcrconversion;
			VkSamplerYcbcrConversionInfo ycbcrconversioninfo;
		} vk;
#endif
	};
} sampler_t;
TC_COMPILE_ASSERT(sizeof(sampler_t) <= 8 * sizeof(uint64_t));

// Data structure holding the layout for a descriptor
typedef struct ALIGNED(descinfo_s, 16) {
	const char* name;
	uint32_t type;
	uint32_t dim : 4;
	uint32_t rootdescriptor : 1;
	uint32_t staticsampler : 1;
	uint32_t updatefreq : 3;
	uint32_t size;
	uint32_t handleindex;
	union {
#if defined(VULKAN)
		struct {
			uint32_t type;
			uint32_t reg : 20;
			uint32_t stages : 8;
		} vk;
#endif
	};
} descinfo_t;
TC_COMPILE_ASSERT(sizeof(descinfo_t) == 4 * sizeof(uint64_t));

typedef struct {
	shader_t** shaders;
	uint32_t shadercount;
	uint32_t maxbindlesstextures;
	const char** staticsamplernames;
	sampler_t** staticsamplers;
	uint32_t staticsamplercount;
	rootsignatureflags_t flags;
} rootsignaturedesc_t;

typedef struct ALIGNED(rootsignature_s, 64) {
	uint32_t descriptorcount;						// Number of descriptors declared in the root signature layout
	pipelinetype_t pipelinetype;					// Graphics or Compute
	descinfo_t* descriptors;					// Array of all descriptors declared in the root signature layout
	descidxmap_t* descnametoidxmap;					// Translates hash of descriptor name to descriptor index in pDescriptors array
	union {
#if defined(VULKAN)
		struct {
			VkPipelineLayout pipelinelayout;
			VkDescriptorSetLayout descriptorsetlayouts[DESCRIPTOR_UPDATE_FREQ_COUNT];
			uint8_t dynamicdescriptorcounts[DESCRIPTOR_UPDATE_FREQ_COUNT];
			VkDescriptorPoolSize poolsizes[DESCRIPTOR_UPDATE_FREQ_COUNT][MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT];
			uint8_t poolsizecount[DESCRIPTOR_UPDATE_FREQ_COUNT];
			VkDescriptorPool emptydescriptorpool[DESCRIPTOR_UPDATE_FREQ_COUNT];
			VkDescriptorSet emptydescriptorset[DESCRIPTOR_UPDATE_FREQ_COUNT];
		} vk;
#endif
	};
} rootsignature_t;
TC_COMPILE_ASSERT(sizeof(rootsignature_t) <= 72 * sizeof(uint64_t));

typedef struct {
	const char* name;								// User can either set name of descriptor or index (index in rootSignature->descriptors array)
	uint32_t count : 31;							// Number of array entries to update (array size of ppTextures/ppBuffers/...)
	uint32_t arrayoffset : 20;						// Dst offset into the array descriptor (useful for updating few entries in a large array). Example: to update 6th entry in a bindless texture descriptor, arrayoffset will be 6 and count will be 1)
	uint32_t index : 10;							// Index in rootSignature->descriptors array - Cache index using getdescriptorindexfromname to avoid using string checks at runtime
	uint32_t bindbyidx : 1;
	range32_t* ranges;								// Range to bind (buffer offset, size)
	bool bindstencilresource : 1;					// Binds stencil only descriptor instead of color/depth
	union {
		struct {
			
			uint16_t UAVmipslice;					// When binding UAV, control the mip slice to to bind for UAV (example - generating mipmaps in a compute shader)
			bool bindmipchain;						// Binds entire mip chain as array of UAV
		};
		struct {
			const char* ICBname;					// Bind MTLIndirectCommandBuffer along with the MTLBuffer
			uint32_t ICBindex;
			bool bindICB;
		};
	};
	// Array of resources containing descriptor handles or constant to be used in ring buffer memory - DescriptorRange can hold only one resource type array
	union {
		texture_t** textures;						// Array of texture descriptors (srv and uav textures)
		sampler_t** samplers;						// Array of sampler descriptors
		buffer_t** buffers;							// Array of buffer descriptors (srv, uav and cbv buffers)
		accelstruct_t** accelstructs;				// Custom binding (raytracing acceleration structure ...)
	};
} descdata_t;

typedef struct ALIGNED(descriptorset_s, 64) {
	union {
#if defined(VULKAN)
		struct
		{
			VkDescriptorSet* handles;
			const rootsignature_t* rootsignature;
			struct dynamicuniformdata_s* dynamicuniformdata;
			VkDescriptorPool descriptorpool;
			uint32_t maxsets;
			uint8_t dynamicoffsetcount;
			uint8_t updatefreq;
			uint8_t nodeidx;
			uint8_t padA;
		} vk;
#endif
	};
} descset_t;

typedef struct { queue_t* queue; bool transient; } cmdpooldesc_t;

typedef struct {
#if defined(VULKAN)
		VkCommandPool cmdpool;
#endif
	queue_t* queue;
} cmdpool_t;

typedef struct { cmdpool_t* pool; bool secondary; } cmddesc_t;

typedef struct ALIGNED(cmd_s, 64) {
	union {
#if defined(VULKAN)
		struct {
			VkCommandBuffer cmdbuf;
			VkRenderPass activerenderpass;
			VkPipelineLayout boundpipelinelayout;
			cmdpool_t* cmdpool;
			uint32_t nodeidx : 4;
			uint32_t type : 3;
		} vk;
#endif
	};
	renderer_t* renderer;
	queue_t* queue;
} cmd_t;
TC_COMPILE_ASSERT(sizeof(cmd_t) <= 64 * sizeof(uint64_t));

typedef struct {
	union {
#if defined(VULKAN)
		struct
		{
			VkFence fence;
			uint32_t submitted : 1;
			uint32_t padA;
			uint64_t padB;
			uint64_t padC;
		} vk;
#endif
	};
} fence_t;

typedef struct {
	union {
#if defined(VULKAN)
		struct
		{
			VkSemaphore semaphore;
			uint32_t currentnodeidx : 5;
			uint32_t signaled : 1;
			uint32_t padA;
			uint64_t padB;
			uint64_t padC;
		} vk;
#endif
	};
} semaphore_t;

typedef struct {
	queuetype_t type;
	queueflag_t flag;
	queuepriority_t priority;
	uint32_t nodeidx;
} queuedesc_t;

typedef struct queue_s {
	union
	{
#if defined(VULKAN)
		struct
		{
			VkQueue queue;
			lock_t* submitlck;
			uint32_t flags;
			float timestampperiod;
			uint32_t queuefamilyindex : 5;
			uint32_t queueindex : 5;
			uint32_t gpumode : 3;
		} vk;
#endif
	};
	uint32_t type : 3;
	uint32_t nodeidx : 4;
} queue_t;

/* Shader reflection */
typedef struct {
	const char* name;	// resource name
	uint32_t size;		// The size of the attribute
	uint32_t name_size;	// name size
} vertexinput_t;

typedef struct {
	descriptortype_t type;		// resource Type
	uint32_t set;				// The resource set for binding frequency
	uint32_t reg;				// The resource binding location
	uint32_t size;				// The size of the resource. This will be the DescriptorInfo array size for textures
	shaderstage_t used_stages;	// what stages use this resource
	const char* name;			// resource name
	uint32_t name_len;			// name size
	texturetype_t dim;			// 1D / 2D / Array / MSAA / ...
} shaderresource_t;

typedef struct {
	const char* name;			// Variable name
	uint32_t parent_index;		// parents resource index
	uint32_t offset;			// The offset of the Variable.
	uint32_t size;				// The size of the Variable.
	uint32_t name_size;			// name size
} shadervar_t;

typedef struct {
	
	char* namepool;				// single large allocation for names to reduce number of allocations
	vertexinput_t* vertexinputs;
	shaderresource_t* shaderresources;
	shadervar_t* variables;
	char* entrypoint;
	shaderstage_t stage;
	uint32_t namepoolsize;
	uint32_t vertexinputscount;
	uint32_t resourcecount;
	uint32_t varcount;
	uint32_t threadspergroup[3];// Thread group size for compute shader
	uint32_t mNumControlPoint;	// number of tessellation control point
} shaderreflection_t;

typedef struct {
	shaderstage_t shaderstages;
	shaderreflection_t reflections[MAX_SHADER_STAGE_COUNT];	// the individual stages reflection data.
	uint32_t reflectioncount;
	uint32_t vertexidx;
	uint32_t hullidx;
	uint32_t domainidx;
	uint32_t geometryidx;
	uint32_t pixelidx;
	shaderresource_t* resources;
	uint32_t resourcecount;
	shadervar_t* variables;
	uint32_t varcount;
} pipelinereflection_t;

typedef struct {
	const char* definition;
	const char* value;
} shadermacro_t;

typedef struct {
	const void* value;
	uint32_t index;
	uint32_t size;
} shaderconstant_t;

typedef struct {
	void* bytecode; 			// Byte code array
	uint32_t bytecodesize;
	const char* entrypoint;
} binaryshaderstagedesc_t;

typedef struct {
	shaderstage_t stages;
	uint32_t ownbytecode : 1;	// Specify whether shader will own byte code memory
	binaryshaderstagedesc_t vert;
	binaryshaderstagedesc_t frag;
	binaryshaderstagedesc_t geom;
	binaryshaderstagedesc_t hull;
	binaryshaderstagedesc_t domain;
	binaryshaderstagedesc_t comp;
	const shaderconstant_t* constants;
	uint32_t constantcount;
} binaryshaderdesc_t;

typedef struct shader_s {
	shaderstage_t stages : 31;
    bool isVR : 1;
	uint32_t threadspergroup[3];
	union {
#if defined(VULKAN)
		struct {
			VkShaderModule* shadermodules;
			char** entrynames;
			VkSpecializationInfo* specializationinfo;
		} vk;
#endif
	};
	pipelinereflection_t* reflection;
} shader_t;

typedef struct {
	blendfactor_t srcfactors[MAX_RENDER_TARGET_ATTACHMENTS];		// Source blend factor per render target.
	blendfactor_t dstfactors[MAX_RENDER_TARGET_ATTACHMENTS];		// Destination blend factor per render target.
	blendfactor_t srcalphafactors[MAX_RENDER_TARGET_ATTACHMENTS];	// Source alpha blend factor per render target.
	blendfactor_t dstalphafactors[MAX_RENDER_TARGET_ATTACHMENTS];	// Destination alpha blend factor per render target.
	blendmode_t blendmodes[MAX_RENDER_TARGET_ATTACHMENTS];			// Blend mode per render target.
	blendmode_t blendalphamodes[MAX_RENDER_TARGET_ATTACHMENTS];		// Alpha blend mode per render target.
	int32_t masks[MAX_RENDER_TARGET_ATTACHMENTS];					// Write mask per render target.
	blendstatetargets_t rtmask;										// Mask that identifies the render targets affected by the blend state.
	bool alphatocoverage;											// Set whether alpha to coverage should be enabled.
	bool independentblend;											// Set whether each render target has an unique blend function. When false the blend function in slot 0 will be used for all render targets.
} blendstatedesc_t;

typedef struct {
	bool depthtest;
	bool depthwrite;
	comparemode_t depthfunc;
	bool stenciltest;
	uint8_t stencilreadmask;
	uint8_t stencilwritemask;
	comparemode_t stencilfrontfunc;
	stencilop_t stencilfrontfail;
	stencilop_t depthfrontfail;
	stencilop_t stencilfrontpass;
	comparemode_t stencilbackfunc;
	stencilop_t stencilbackfail;
	stencilop_t depthbackfail;
	stencilop_t stencilbackpass;
} depthstatedesc_t;

typedef struct {
	cullmode_t cullmode;
	int32_t depthbias;
	float slopescaleddepthbias;
	fillmode_t fillmode;
	frontface_t frontface;
	bool multisample;
	bool scissor;
	bool depthclampenable;
} rasterizerstatedesc_t;

typedef struct {
	shadersemantic_t semantic;
	uint32_t semanticnamelen;
	char semanticname[MAX_SEMANTIC_NAME_LENGTH];
	TinyImageFormat format;
	uint32_t binding;
	uint32_t location;
	uint32_t offset;
	vertexattribrate_t rate;
} vertexattrib_t;

typedef struct {
	uint32_t attribcount;
	vertexattrib_t attribs[MAX_VERTEX_ATTRIBS];
	uint32_t strides[MAX_VERTEX_BINDINGS];
} vertexlayout_t;

typedef struct {
	rootsignature_t* rootsignature;
	shader_t* intersection;
	shader_t* anyhit;
	shader_t* closesthit;
	const char* hitgroupname;
} raytracinghitgroup_t;

typedef struct {
	raytracing_t* raytracing;
	rootsignature_t* globalrootsignature;
	shader_t* raygenshader;
	rootsignature_t* raygenrootsignature;
	shader_t** missshaders;
	rootsignature_t** missrootsignatures;
	raytracinghitgroup_t* hitgroups;
	rootsignature_t* emptyrootsignature;
	unsigned missshadercount;
	unsigned hitgroupcount;
	unsigned payloadsize;
	unsigned attributesize;
	unsigned maxtracerecursiondepth;
	unsigned maxrayscount;
} raytracingpipelinedesc_t;

typedef struct {
	shader_t* shader;
	rootsignature_t* rootsignature;
	vertexlayout_t* vertexlayout;
	blendstatedesc_t* blendstate;
	depthstatedesc_t* depthstate;
	rasterizerstatedesc_t* rasterizerstate;
	TinyImageFormat* colorformats;
#if defined(USE_MSAA_RESOLVE_ATTACHMENTS)
	storeop_t* colorresolveactions;	// Used to specify resolve attachment for render pass
#endif
	uint32_t rendertargetcount;
	samplecount_t samplecount;
	uint32_t samplequality;
	TinyImageFormat depthstencilformat;
	primitivetopology_t primitivetopo;
	bool supportindirectcommandbuffer;
    bool VRfoveatedrendering;
} graphicspipelinedesc_t;

typedef struct { shader_t* shader; rootsignature_t* rootsignature; } computepipelinedesc_t;

typedef enum { PIPELINE_CACHE_FLAG_NONE, PIPELINE_CACHE_FLAG_EXTERNALLY_SYNCHRONIZED } pipelinecacheflags_t;

typedef struct {
	void* data;		// Initial pipeline cache data (can be NULL which means empty pipeline cache)
	size_t size;	// Initial pipeline cache size
	pipelinecacheflags_t flags;
} pipelinecachedesc_t;

typedef struct {
#if defined(VULKAN)
	struct {
		VkPipelineCache cache;
	} vk;
#endif
} pipelinecache_t;

typedef struct {
	union {
		computepipelinedesc_t computedesc;
		graphicspipelinedesc_t graphicsdesc;
		raytracingpipelinedesc_t raytracingdesc;
	};
	pipelinecache_t* cache;
	void* pipelineextensions;
	const char* name;
	pipelinetype_t type;
	uint32_t extensioncount;
} pipelinedesc_t;

typedef struct ALIGNED(pipeline_s, 64) {
	union {
#if defined(VULKAN)
		struct {
			VkPipeline pipeline;
			pipelinetype_t type;
			uint32_t stagecount;
			const char** stagenames;
		} vk;
#endif
	};
} pipeline_t;
TC_COMPILE_ASSERT(sizeof(pipeline_t) == 8 * sizeof(uint64_t));

typedef enum { SWAP_CHAIN_CREATION_FLAG_NONE, SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR } swapchaincreationflags_t;

typedef struct {
	GLFWwindow* window;				// Window handle
	queue_t** presentqueues;		// Queues which should be allowed to present
	uint32_t presentqueuecount;		// Number of present queues
	uint32_t imagecount;			// Number of backbuffers in this swapchain
	uint32_t width;					// Width of the swapchain
	uint32_t height;				// Height of the swapchain
	TinyImageFormat colorformat;	// Color format of the swapchain
	clearvalue_t colorclearval;		// Clear value
    swapchaincreationflags_t flags;	// Swapchain creation flags
	bool vsync;						// Set whether swap chain will be presented using vsync
	bool useflipswapeffect;			// We can toggle to using FLIP model if app desires.
} swapchaindesc_t;

typedef struct {
	rendertarget_t** rts;			// Render targets created from the swapchain back buffers
#if defined(VULKAN)
		struct {
			VkQueue presentqueue;	// Present queue if one exists (queuePresent will use this queue if the hardware has a dedicated present queue)
			VkSwapchainKHR swapchain;
			VkSurfaceKHR surface;
			swapchaindesc_t* desc;
			uint32_t presentqueuefamilyindex : 5;
			uint32_t padA;
		} vk;
#endif
	uint32_t imagecount : 3;
	uint32_t vsync : 1;
} swapchain_t;

typedef struct {
	uint32_t numsettings;
	uint32_t* settings;
	const char* settingnames;
} extendedsettings_t;

typedef struct {
#if defined(VULKAN)
		struct
		{
			const char** instancelayers;
			const char** instanceextensions;
			const char** deviceextensions;
			uint32_t instancelayercount;
			uint32_t instanceextensioncount;
			uint32_t deviceextensioncount;
			bool requestallavailablequeues;	// Flag to specify whether to request all queues from the gpu or just one of each type. This will affect memory usage - Around 200 MB more used if all queues are requested
		} vk;
#endif
	shadertarget_t shadertarget;
	gpumode_t gpumode;
	extendedsettings_t* extendedsettings;	// Apps may want to query additional state for their applications. That information is transferred through here.
	renderercontext_t* context;				// Required when creating unlinked multiple renderers. Optional otherwise, can be used for explicit GPU selection.
	uint32_t gpuindex;
	bool enablegpubasedvalidation;			// This results in new validation not possible during API calls on the CPU, by creating patched shaders that have validation added directly to the shader. However, it can slow things down a lot, especially for applications with numerous PSOs. Time to see the first render frame may take several minutes
} rendererdesc_t;

typedef struct {
	gpupreset_t presetlevel;
	char vendorid[MAX_GPU_VENDOR_STRING_LENGTH];
	char modelid[MAX_GPU_VENDOR_STRING_LENGTH];
	char revisionid[MAX_GPU_VENDOR_STRING_LENGTH];    	// Optional as not all gpu's have that.
	char gpuname[MAX_GPU_VENDOR_STRING_LENGTH];    		//If GPU Name is missing then value will be empty string
	char gpudriverversion[MAX_GPU_VENDOR_STRING_LENGTH];
	char gpudriverdate[MAX_GPU_VENDOR_STRING_LENGTH];
	uint32_t RTcores;
} gpuvendorpreset_t;

typedef struct {
	bool canshaderreadfrom[TinyImageFormat_Count];
	bool canshaderwriteto[TinyImageFormat_Count];
	bool canrtwriteto[TinyImageFormat_Count];
} gpucaps_t;

typedef struct {
	uint64_t VRAM;
	uint32_t uniformbufalignment;
	uint32_t uploadbuftexalignment;
	uint32_t uploadbuftexrowalignment;
	uint32_t maxvertexnputbindings;
	uint32_t maxrootsignatureDWORDS;
	uint32_t wavelanecount;
	waveopssupportflags_t waveopssupportflags;
	gpuvendorpreset_t gpuvendorpreset;
	shadingrate_t shadingrates;					// Variable Rate Shading
	shadingratecaps_t shadingratecaps;
	uint32_t shadingratetexelwidth;
	uint32_t shadingratetexelheight;
	uint32_t multidrawindirect : 1;
	uint32_t indirectrootconstant : 1;
	uint32_t builtindrawid : 1;
	uint32_t indirectcmdbuf : 1;
	uint32_t ROVsupported : 1;
	uint32_t tessellationsupported : 1;
	uint32_t geometryshadersupported : 1;
	uint32_t gpubreadcrumbs : 1;
	uint32_t HDRSupported : 1;
#ifdef VULKAN
	uint32_t sampleranisotropysupported : 1;
#endif
} gpusettings_t;

typedef struct ALIGNED(renderer_s, 64) {
#if defined(VULKAN)
		struct {
			VkInstance instance;
			VkPhysicalDevice activegpu;
			VkPhysicalDeviceProperties2* activegpuprops;
			VkDevice device;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
			VkDebugUtilsMessengerEXT debugutilsmessenger;
#else
			VkDebugReportCallbackEXT debugreport;
#endif
			uint32_t** availablequeues;
			uint32_t** usedqueues;
			VkDescriptorPool emptydescriptorpool;
			VkDescriptorSetLayout emptydescriptorsetlayout;
			VkDescriptorSet emptydescriptorset;
			struct VmaAllocator_T* vmaAllocator;
			uint32_t raytracingsupported : 1;
			uint32_t YCbCrextension : 1;
			uint32_t KHRspirv14extension : 1;
			uint32_t KHRaccelerationstructureextension : 1;
			uint32_t KHRRaytracingpipelineextension : 1;
			uint32_t KHRrayqueryextension : 1;
			uint32_t AMDGCNshaderextension : 1;
			uint32_t AMDdrawindirectcountextension : 1;
			uint32_t descriptorindexingextension : 1;
			uint32_t shadersampledimagearraydynamicindexingsupported : 1;
			uint32_t shaderfloatcontrolsextension : 1;
			uint32_t bufferdeviceaddressextension : 1;
			uint32_t deferredhostoperationsextension : 1;
			uint32_t drawindirectcountextension : 1;
			uint32_t dedicatedallocationextension : 1;
			uint32_t externalmemoryextension : 1;
			uint32_t debugmarkersupport : 1;
			uint32_t owninstance : 1;
			uint32_t multiviewextension : 1;
			union {
				struct {
					uint8_t graphicsqueuefamilyidx;
					uint8_t transferqueuefamilyidx;
					uint8_t computequeuefamilyidx;
				};
				uint8_t queuefamilyindices[3];
			};
		} vk;
#endif
	struct nulldescriptors_s* nulldescriptors;
	char* name;
	gpusettings_t* activegpusettings;
	gpucaps_t* capbits;
	uint32_t linkednodecount : 4;
	uint32_t unlinkedrendererindex : 4;
	uint32_t gpumode : 3;
	uint32_t shadertarget : 4;
	uint32_t enablegpubasedvalidation : 1;
	char* apiname;
} renderer_t;
TC_COMPILE_ASSERT(sizeof(renderer_t) <= 24 * sizeof(uint64_t)); // 3 cache lines

typedef struct {
#if defined(VULKAN)
		struct {
			VkPhysicalDevice gpu;
			VkPhysicalDeviceProperties2 gpuprops;
		} vk;
#endif
	gpusettings_t settings;
} gpuinfo_t;

typedef struct renderercontext_s {
#if defined(VULKAN)
		struct {
			VkInstance instance;
#ifdef ENABLE_DEBUG_UTILS_EXTENSION
			VkDebugUtilsMessengerEXT debugutilsmessenger;
#else
			VkDebugReportCallbackEXT debugreport;
#endif
		} vk;
#endif
	gpuinfo_t gpus[MAX_MULTIPLE_GPUS];
	uint32_t gpucount;
} renderercontext_t;

// Indirect command structure define
typedef struct {
	uint32_t vertex_count;
	uint32_t instance_count;
	uint32_t start_vertex;
	uint32_t start_instance;
} indirectdrawargs_t;

typedef struct {
	uint32_t index_count;
	uint32_t instance_count;
	uint32_t start_index;
	uint32_t vertex_offset;
	uint32_t start_instance;
} indirectdrawindexargs_t;

typedef struct {
	uint32_t groupcountX;
	uint32_t groupcountY;
	uint32_t groupcountZ;
} indirectdispatchargs_t;

typedef struct { indirectargtype_t type; uint32_t offset; } indirectarg_t;

typedef struct {
	indirectargtype_t type;
	uint32_t index;
	uint32_t bytesize;
} indirectargdesc_t;

typedef struct {
	rootsignature_t* rootsignature;
	indirectargdesc_t* argdescs;
	uint32_t indirectargcount;
	bool packed;					// Set to true if indirect argument struct should not be aligned to 16 bytes
} cmdsignaturedesc_t;

typedef struct { indirectargtype_t drawtype; uint32_t stride; } cmdsignature_t;

typedef struct {
	rootsignature_t* rootsignature;
	descriptorupdatefreq_t updatefreq;
	uint32_t maxsets;
	uint32_t nodeidx;
} descriptorsetdesc_t;

typedef struct {
	cmd_t** cmds;
	fence_t* signalfence;
	semaphore_t** waitsemaphores;
	semaphore_t** signalsemaphores;
	uint32_t cmdcount;
	uint32_t waitsemaphorecount;
	uint32_t signalsemaphorecount;
	bool submitdone;
} queuesubmitdesc_t;

typedef struct {
	swapchain_t* swapchain;
	semaphore_t** waitsemaphores;
	uint32_t waitsemaphorecount;
	uint8_t index;
	bool submitdone;
} queuepresentdesc_t;


// Queue/fence/swapchain functions
typedef void (*add_fence_func)(renderer_t* renderer, fence_t* fence);
typedef void (*remove_fence_func)(renderer_t* renderer, fence_t* fence);
typedef void (*add_semaphore_func)(renderer_t* renderer, semaphore_t* semaphore);
typedef void (*remove_semaphore_func)(renderer_t* renderer, semaphore_t* semaphore);
typedef void (*add_queue_func)(renderer_t* renderer, queuedesc_t* desc, queue_t* queue);
typedef void (*remove_queue_func)(renderer_t* renderer, queue_t* queue);

typedef void (*add_swapchain_func)(renderer_t* renderer, const swapchaindesc_t* desc, swapchain_t* swapchain);
typedef void (*remove_swapchain_func)(renderer_t* renderer, swapchain_t* swapchain);
typedef void (*acquire_nextimage_func)(renderer_t* renderer, swapchain_t* swapchain, semaphore_t* signalsemaphore, fence_t* fence, uint32_t* image_idx);
typedef void (*queue_submit_func)(queue_t* queue, const queuesubmitdesc_t* desc);
typedef void (*queue_present_func)(queue_t* queue, const queuepresentdesc_t* desc);
typedef void (*queue_waitidle_func)(queue_t* queue);
typedef void (*get_fencestatus_func)(renderer_t* renderer, fence_t* fence, fencestatus_t* status);
typedef void (*wait_for_fences_func)(renderer_t* renderer, uint32_t count, fence_t* fences);
typedef void (*toggle_vsync_func)(renderer_t* renderer, swapchain_t* swapchain);

typedef void (*add_rendertarget_func)(renderer_t* renderer, const rendertargetdesc_t* desc, rendertarget_t* rt);
typedef void (*remove_rendertarget_func)(renderer_t* renderer, rendertarget_t* rt);
typedef void (*add_sampler_func)(renderer_t* renderer, const samplerdesc_t* desc, sampler_t* sampler);
typedef void (*remove_sampler_func)(renderer_t* renderer, sampler_t* sampler);
typedef void (*add_shaderbinary_func)(renderer_t* renderer, const binaryshaderdesc_t* desc, shader_t* shader);
typedef void (*remove_shader_func)(renderer_t* renderer, shader_t* shader);

typedef void (*add_rootsignature_func)(renderer_t* renderer, const rootsignaturedesc_t* desc, rootsignature_t* rootsignature);
typedef void (*remove_rootsignature_func)(renderer_t* renderer, rootsignature_t* rootsignature);

// Pipeline functions
typedef void (*add_pipeline_func)(renderer_t* renderer, const pipelinedesc_t* desc, pipeline_t* pipeline);
typedef void (*remove_pipeline_func)(renderer_t* renderer, pipeline_t* pipeline);
typedef void (*add_pipelinecache_func)(renderer_t* renderer, const pipelinecachedesc_t* desc, pipelinecache_t* cache);
typedef void (*get_pipelinecachedata_func)(renderer_t* renderer, pipelinecache_t* cache, size_t* size, void* data);
typedef void (*remove_pipelinecache_func)(renderer_t* renderer, pipelinecache_t* cache);

// Descriptor Set functions
typedef void (*add_descriptorset_func)(renderer_t* renderer, const descriptorsetdesc_t* desc, descset_t* descset);
typedef void (*remove_descriptorset_func)(renderer_t* renderer, descset_t* descset);
typedef void (*update_descriptorset_func)(renderer_t* renderer, uint32_t index, descset_t* descset, uint32_t count, const descdata_t* data);

// Command buffer functions
typedef void (*add_cmdpool_func)(renderer_t* renderer, const cmdpooldesc_t* desc, cmdpool_t* pool);
typedef void (*remove_cmdpool_func)(renderer_t* renderer, cmdpool_t* pool);
typedef void (*add_cmds_func)(renderer_t* renderer, const cmddesc_t* desc, uint32_t count, cmd_t* cmds);
typedef void (*remove_cmds_func)(renderer_t* renderer, uint32_t count, cmd_t* cmds);
typedef void (*reset_cmdpool_func)(renderer_t* renderer, cmdpool_t* pool);

typedef void (*cmd_begin_func)(cmd_t* cmd);
typedef void (*cmd_end_func)(cmd_t* cmd);
typedef void (*cmd_bindrendertargets_func)(cmd_t* cmd, uint32_t count, rendertarget_t** rts, rendertarget_t* depthstencil, const loadopsdesc_t* loadops, uint32_t* colorarrayslices, uint32_t* colormipslices, uint32_t deptharrayslice, uint32_t depthmipslice);
typedef void (*cmd_setshadingrate_func)(cmd_t* cmd, shadingrate_t shading_rate, texture_t* tex, shadingratecombiner_t post_rasterizer_rate, shadingratecombiner_t final_rate);
typedef void (*cmd_setviewport_func)(cmd_t* cmd, float x, float y, float w, float h, float min_depth, float max_depth);
typedef void (*cmd_setscissor_func)(cmd_t* cmd, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
typedef void (*cmd_setstencilreferenceval_func)(cmd_t* cmd, uint32_t val);
typedef void (*cmd_bindpipeline_func)(cmd_t* cmd, pipeline_t* pipeline);
typedef void (*cmd_binddescset_func)(cmd_t* cmd, uint32_t index, descset_t* descset);
typedef void (*cmd_bindpushconstants_func)(cmd_t* cmd, rootsignature_t* rootsignature, uint32_t paramindex, const void* constants);
typedef void (*cmd_binddescsetwithrootbbvs_func)(cmd_t* cmd, uint32_t index, descset_t* descset, uint32_t count, const descdata_t* params);
typedef void (*cmd_bindindexbuffer_func)(cmd_t* cmd, buffer_t* buf, uint32_t indextype, uint64_t offset);
typedef void (*cmd_bindvertexbuffer_func)(cmd_t* cmd, uint32_t count, buffer_t* bufs, const uint32_t* strides, const uint64_t* poffsets);
typedef void (*cmd_draw_func)(cmd_t* cmd, uint32_t vertex_count, uint32_t first_vertex);
typedef void (*cmd_drawinstanced_func)(cmd_t* cmd, uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance);
typedef void (*cmd_drawindexed_func)(cmd_t* cmd, uint32_t index_count, uint32_t first_index, uint32_t first_vertex);
typedef void (*cmd_drawindexedinstanced_func)(cmd_t* cmd, uint32_t index_count, uint32_t first_index, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
typedef void (*cmd_dispatch_func)(cmd_t* cmd, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

// Transition Commands
typedef void (*cmd_resourcebarrier_func)(cmd_t* cmd, uint32_t bufbarrier_count, bufbarrier_t* bufbarriers, uint32_t texbarrier_count, texbarrier_t* texbarriers, uint32_t rtbarrier_count, rtbarrier_t* rtbarriers);

// Virtual Textures
typedef void (*cmd_updatevirtualtexture_func)(cmd_t* cmd, texture_t* tex, uint32_t currimage);

// Returns the recommended format for the swapchain.
//If true is passed for the hintHDR parameter, it will return an HDR format IF the platform supports it
//If false is passed or the platform does not support HDR a non HDR format is returned.
//If true is passed for the hintSrgb parameter, it will return format that is will do gamma correction automatically
//If false is passed for the hintSrgb parameter the gamma correction should be done as a postprocess step before submitting image to swapchain
typedef TinyImageFormat (*recommendedswapchainfmt_func)(bool hintHDR, bool hintSRGB);

// Indirect Draw functions
typedef void (*add_indirectcmdsignature_func)(renderer_t* renderer, const cmdsignaturedesc_t* desc, cmdsignature_t* cmdsignature);
typedef void (*remove_indirectcmdsignature_func)(renderer_t* renderer, cmdsignature_t* cmdsignature);
typedef void (*cmd_execindirect_func)(cmd_t* cmd, cmdsignature_t* cmdsignature, unsigned int maxcmdcount, buffer_t* indirectbuf, uint64_t bufoffset, buffer_t* counterbuf, uint64_t counterbufoffset);

// GPU Query Interface
typedef void (*get_timestampfreq_func)(queue_t* queue, double* freq);
typedef void (*add_querypool_func)(renderer_t* renderer, const querypooldesc_t* desc, querypool_t* pool);
typedef void (*remove_querypool_func)(renderer_t* renderer, querypool_t* pool);
typedef void (*cmd_resetquerypool_func)(cmd_t* cmd, querypool_t* pool, uint32_t startquery, uint32_t querycount);
typedef void (*cmd_beginquery_func)(cmd_t* cmd, querypool_t* pool, querydesc_t* query);
typedef void (*cmd_endquery_func)(cmd_t* cmd, querypool_t* pool, querydesc_t* query);
typedef void (*cmd_resolvequery_func)(cmd_t* cmd, querypool_t* pool, buffer_t* readbackbuf, uint32_t startquery, uint32_t querycount);

// Stats Info Interface
typedef void (*get_memstats_func)(renderer_t* renderer, char* stats);
typedef void (*get_memuse_func)(renderer_t* renderer, uint64_t* used, uint64_t* totalallocated);
typedef void (*free_memstats_func)(renderer_t* renderer, char* stats);

// Debug Marker Interface
typedef void (*cmd_begindebugmark_func)(cmd_t* cmd, float r, float g, float b, const char* name);
typedef void (*cmd_enddebugmark_func)(cmd_t* cmd);
typedef void (*cmd_adddebugmark_func)(cmd_t* cmd, float r, float g, float b, const char* name);
typedef uint32_t (*cmd_writemark_func)(cmd_t* cmd, markertype_t type, uint32_t val, buffer_t* buf, size_t offset, bool useautoflags);

// Resource Debug Naming Interface
typedef void (*set_buffername_func)(renderer_t* renderer, buffer_t* buf, const char* name);
typedef void (*set_texturename_func)(renderer_t* renderer, texture_t* tex, const char* name);
typedef void (*set_rendertargetname_func)(renderer_t* renderer, rendertarget_t* rt, const char* name);
typedef void (*set_pipelinename_func)(renderer_t* renderer, pipeline_t* pipeline, const char* name);


void renderer_init(const char* app_name, const rendererdesc_t* desc_func, renderer_t* renderer);
void renderer_exit(renderer_t* renderer);

uint32_t descindexfromname(const rootsignature_t* rootsignature, const char* name);

static inline bool is_rootcbv(const char* name) {
	char lower[MAX_RESOURCE_NAME_LENGTH] = { 0 };
	uint32_t len = (uint32_t)strlen(name);
	for (uint32_t i = 0; i < len; ++i)
		lower[i] = tolower(name[i]);
	return strstr(lower, "rootcbv");
}