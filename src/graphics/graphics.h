/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/
#pragma once
#include "core.h"
#include "image.h"
#include "types.h"
#include "log.h"

typedef struct tc_allocator_i tc_allocator_i;

/* Constants and macros: */
#ifndef NUM_BUFFER_FRAMES
#define NUM_BUFFER_FRAMES 3
#endif

// Maximum number of shader locations supported
#ifndef MAX_ATTRIBUTES
#define MAX_ATTRIBUTES 16
#endif

#ifndef MAX_BUFFERS
#define MAX_BUFFERS 32
#endif

#ifndef TEXUNIT_MAX
#define TEXUNIT_MAX 8
#endif

#ifndef VIEWPORTS_MAX
#define VIEWPORTS_MAX 16
#endif

#ifndef RENDER_TARGETS_MAX 8
#define RENDER_TARGETS_MAX 8
#endif

// Default shaders glsl code
#ifndef DEFAULT_VERTEXSHADER
#define DEFAULT_VERTEXSHADER \
"#version 330                                       \n"\
"in vec3 vertexPosition;                            \n"\
"in vec2 vertexTexCoords;                           \n"\
"in vec4 vertexColor;                               \n"\
"out vec2 texCoords;                                \n"\
"out vec4 color;                                    \n"\
"layout (std140) uniform matrices {                 \n"\
"   mat4 projection;                                \n"\
"   mat4 view;                                      \n"\
"};                                                 \n"\
"uniform mat4 mvp;                                  \n"\
"void main() {                                      \n"\
"    texCoords = vertexTexCoords;                   \n"\
"    color = vertexColor;                           \n"\
"    gl_Position = mvp*vec4(vertexPosition, 1.0);   \n"\
"}"
#endif
#ifndef DEFAULT_FRAGMENTSHADER
#define DEFAULT_FRAGMENTSHADER \
"#version 330                                       \n"\
"in vec2 texCoords;                                 \n"\
"in vec4 color;                                     \n"\
"out vec4 gl_FragColor;                             \n"\
"uniform sampler2D texture0;                        \n"\
"uniform vec4 diffuse;                              \n"\
"void main() {                                      \n"\
"    vec4 tex = texture(texture0, texCoords);       \n"\
"    gl_FragColor = tex*diffuse*color;              \n"\
"}"
#endif

// Which channels can be writtin to
enum colormask_t {
	COLOR_MASK_NONE = 0,
	COLOR_MASK_RED = 1 << 0,
	COLOR_MASK_GREEN = 1 << 1,
	COLOR_MASK_BLUE = 1 << 2,
	COLOR_MASK_ALPHA = 1 << 3,
	COLOR_MASK_ALL = COLOR_MASK_RED|COLOR_MASK_GREEN|COLOR_MASK_BLUE|COLOR_MASK_ALPHA,
};

// Blend function to describe how to blend colors of the render target with the output of the fragment shader
enum blendfunc_t {
	BLEND_ADD = 0,
	BLEND_SUBTRACT,
	BLEND_REV_SUBTRACT,
	BLEND_MIN,
	BLEND_MAX,
	BLEND_FUNC_MAX
};

// Blend factor to use in blend function per rgba channels
enum blendfactor_t {
	BLEND_ZERO = 0,
	BLEND_ONE,
	BLEND_SRC_COLOR,
	BLEND_INV_SRC_COLOR,
	BLEND_SRC_ALPHA,
	BLEND_INV_SRC_ALPHA,
	BLEND_DST_ALPHA,
	BLEND_INV_DST_ALPHA,
	BLEND_DEST_COLOR,
	BLEND_INV_DEST_COLOR,
	BLEND_SRC_ALPHA_SATURATE,
	BLEND_CONSTANT,
	BLEND_INV_CONSTANT,
	BLEND_SRC1_COLOR,
	BLEND_INV_SRC1_COLOR,
	BLEND_SRC1_ALPHA,
	BLEND_INV_SRC1_ALPHA,
	BLEND_FACTOR_MAX,
};

// Rasterization fill modes
enum fillmode_t {
	FILL_SOLID = 0,
	FILL_WIRE,
	FILL_MAX
};

// Rasterization cull modes
enum cullmode_t {
	CULL_NONE = 0,
	CULL_FRONT,
	CULL_BACK,
	CULL_MAX
};

// Primitive rasterizer type
enum primitivetype_t {
	PRIMITIVE_POINTS,
	PRIMITIVE_LINES,
	PRIMITIVE_TRIANGLE,
	PRIMITIVE_TRIANGLESTRIP,
	PRIMITIVE_TRIANGLEFAN,
	PRIMITIVE_MAX
};

// Function applied on stencil buffer when rendering to it
enum stencilfunc_t {
	STENCIL_KEEP = 0,
	STENCIL_ZERO,
	STENCIL_REPLACE,
	STENCIL_INCR,
	STENDIL_DECR,
	STENCIL_INCR_WRAP,
	STENCIL_DECR_WRAP,
	STENCIL_INVERT,
	STENCIL_FUNC_MAX
};

// Texture sampling filter mode:
enum filtermode_t {
	FILTER_NEAREST = 0,
	FILTER_LINEAR,
	FILTER_ANISOTROPIC,
	FILTER_MAX
};

enum wrapmode_t {
	WRAP_CLAMP = 0,
	WRAP_REPEAT,
	WRAP_MIRROR,
	WRAP_BORDER,
	WRAP_MODE_MAX,
};

// Buffer or texture view types:
enum bindflags_t {
	BIND_NONE = 0,
	BIND_VERTEX_BUFFER = 1 << 0,		// Buffer can be bound as vertex buffer
	BIND_INDEX_BUFFER = 1 << 1,			// Buffer can be bound as index buffer
	BIND_UNIFORM_BUFFER = 1 << 2,		// Buffer can be bound as uniform buffer
	BIND_INDIRECT_DRAW = 1 << 3,		// Buffer can be bound for indirect draw commands
	BIND_SHADER_VARIABLE = 1 << 4,		// Buffer or texture can be bound as shader variable
	BIND_STREAM_OUTPUT = 1 << 5,		// Buffer can be bound as target for stream output
	BIND_RENDER_TARGET = 1 << 6,		// Texture can be bound as render target
	BIND_DEPTH_STENCIL = 1 << 7,		// Texture can be bound as depth-stencil target
	BIND_UNORDERED_ACCESS = 1 << 8,		// Buffer or texture can be bound as unordered acces view
	BIND_INPUT_ATTACHMENT = 1 << 9,		// Texture can be used as render pass input attachment
	BIND_RAY_TRACING = 1 << 10,			// Buffer can be used as scratch buffer for acceleration structures
};

enum usagehint_t {
	USAGE_STATIC = 0,
	USAGE_DYNAMIC,
	USAGE_STREAM,
	USAGE_STAGING,
};

// Data types in shader attributes
enum datavartype_t {
	DATA_UNDEFINED = 0,
	DATA_UINT8,
	DATA_UINT16,
	DATA_UINT32,
	DATA_INT8,
	DATA_INT16,
	DATA_INT32,
	DATA_FLOAT16,
	DATA_FLOAT32,
	DATA_MAX
};

enum shadervartype_t {
	UNIFORM_CONST_BUFFER = 0,			// Constant uniform buffer
	UNIFORM_BUFFER_SRV,					// Read-only storage image
	UNIFORM_BUFFER_UAV,					// Storage buffer
	UNIFORM_TEXTURE_SRV,				// Sampled image
	UNIFORM_TEXTURE_UAV,				// Storage image
	UNIFORM_SAMPLER,					// Separate sampler
	UNIFORM_RESOURCE_MAX,
};

enum varusage_t {
	UNIFORM_STATIC = 0,
	UNIFORM_MUTABLE,
	UNIFORM_DYNAMIC,
	UNIFORM_VARIABLE_MAX,
};

// Types of shader stages
enum shadertype_t {
	SHADER_NONE = 0,
	SHADER_VERTEX = 1 << 0,
	SHADER_PIXEL = 1 << 1,
	SHADER_GEOMETRY = 1 << 2,
	SHADER_DOMAIN = 1 << 3,
	SHADER_HULL = 1 << 4,
	SHADER_COMPUTE = 1 << 5,
	SHADER_MAX = 6,
};

// Type of texture
enum texturetype_t {
	TEXTURE_UNKNOWN = 0,
	TEXTURE_1D,
	TEXTURE_1D_ARRAY,
	TEXTURE_2D,
	TEXTURE_2D_ARRAY,
	TEXTURE_3D,
	TEXTURE_CUBE,
	TEXTURE_CUBE_ARRAY,
	TEXTURE_BUFFER,
	TEXTURE_MAX
};

// Type of buffer
enum buffermode_t {
	BUFFER_RAW = 0,
	BUFFER_FORMATTED,
	BUFFER_STRUCTURED,
	BUFFER_MODE_MAX
};

// Framebuffer read or write type
enum framebuffertype_t {
	FRAMEBUFFER_READ = 1 << 0,
	FRAMEBUFFER_WRITE = 1 << 1,
	FRAMEBUFFER_ALL = FRAMEBUFFER_READ | FRAMEBUFFER_WRITE,
};

enum cpuaccess_t {
	CPU_NO_ACCESS = 0,
	CPU_READ = 1 << 0,
	CPU_WRITE = 1 << 1,
};

enum maptype_t {
	MAP_READ = 1 << 0,
	MAP_WRITE = 1 << 1,
	MAP_READ_WRITE = MAP_READ |	MAP_WRITE,
};

enum mapflags_t {
	MAP_FLAG_NO_WAIT = 1 << 0,
	MAP_FLAG_DISCARD = 1 << 1,
	MAP_FLAG_NO_OVERWRITE = 1 << 2,
};

enum uavflags_t {
	UAV_NONE = 0,
	UAV_READ = 1 << 0,
	UAV_WRITE = 1 << 1,
	UAV_READ_WRITE = UAV_READ | UAV_WRITE,
};

enum textureviewtype_t {
	TEXTURE_VIEW_SHADER_RESOURCE = 0,
	TEXTURE_VIEW_RENDER_TARGET,
	TEXTURE_VIEW_DEPTH_STENCIL,
	TEXTURE_VIEW_UNORDERED_ACCESS,
	TEXTURE_VIEW_MAX,
};

enum bufferviewtype_t {
	BUFFER_VIEW_SHADER_RESOURCE = 0,
	BUFFER_VIEW_UNORDERED_ACCESS,
	BUFFER_VIEW_MAX,
};

enum resourcestate_t {
	STATE_UNKNOWN = 0,
	STATE_VERTEX_BUFFER = 1 << 0,
	STATE_INDEX_BUFFER = 1 << 1,
	STATE_UNIFORM_BUFFER = 1 << 2,
	STATE_INDIRECT_DRAW = 1 << 3,
	STATE_RENDER_TARGET = 1 << 4,
	STATE_UNORDERED_ACCESS = 1 << 5,
	STATE_DEPTH_WRITE = 1 << 6,
	STATE_DEPTH_READ = 1 << 7,
	STATE_SHADER_RESOURCE = 1 << 8,
	STATE_STREAM_OUT = 1 << 9,
	STATE_COPY_DST = 1 << 10,
	STATE_COPY_SRC = 1 << 11,
	STATE_RESOLVE_DST = 1 << 12,
	STATE_RESOLVE_SRC = 1 << 13,
	STATE_INPUT_ATTACHMENT = 1 << 14,
	STATE_PRESENT = 1 << 15,
	STATE_BUILD_ACCEL_READ = 1 << 16,
	STATE_BUILD_ACCEL_WRITE = 1 << 17,
	STATE_RAY_TRACING = 1 << 18,
	STATE_GENERIC_READ = STATE_VERTEX_BUFFER|STATE_INDEX_BUFFER|STATE_UNIFORM_BUFFER|STATE_INDIRECT_DRAW|STATE_SHADER_RESOURCE|STATE_COPY_SRC,
};

enum loadop_t {
	LOAD_OP_LOAD = 0,
	LOAD_OP_CLEAR,
	LOAD_OP_DISCARD
};

enum storeop_t {
	STORE_OP_STORE = 0,
	STORE_OP_DISCARD
};

enum gpuvendor_t {
	VENDOR_UNKNOWN = 0,
	VENDOR_INTEL,
	VENDOR_AMD,
	VENDOR_NVIDIA,
	VENDOR_MESA_SOFT,
	VENDOR_IMGTEC,
	VENDOR_ARM,
	VENDOR_QUALCOMM,
	VENDOR_MAX
};

inline enum gpuvendor_t vendor_from_id(uint32_t id) {
	switch (id) {
	case 0x01002: return VENDOR_AMD;
	case 0x010DE: return VENDOR_NVIDIA;
	case 0x08086: return VENDOR_INTEL;
	case 0x013B5: return VENDOR_ARM;
	case 0x05143: return VENDOR_QUALCOMM;
	case 0x01010: return VENDOR_IMGTEC;
	case 0x01414: return VENDOR_MESA_SOFT;
	default: return VENDOR_UNKNOWN;
	}
}


enum adaptertype_t {
	ADAPTER_TYPE_UNKNOWN = 0,
	ADAPTER_TYPE_SOFTWARE,
	ADAPTER_TYPE_HARDWARE,
};

/* Type declaration: */
typedef struct gpubufferview_t;

/* Describes the blending operations in a pipeline */
typedef struct {
	/* Use the alpha channel as a coverage mask for anti-aliasing */
	bool alpha_to_coverage;
	/* Blend independently in multiple render targets */
	bool blend_per_rt;							
	struct {
		/* Is blending enabled for this render target */
		bool blend;
		/* Do we use an blend function for this render target */
		bool blendoperation;					
		/* Blend factor applied on the output of the pixel shader */
		enum blendfactor_t src_blend;
		/* Blend factor applied on RGB in the render target */
		enum blendfactor_t dest_blend;
		/* Blending operation to be used when combining source and destination colors */
		enum blendfunc_t blend_func;			
		/* Blend factor applied on the alpha output of the pixel shader */
		enum blendfactor_t src_alpha_blend;
		/* Blend factor applied on the alpha channel of the render target */
		enum blendfactor_t dest_alpha_blend;
		/* Blending operation to be used when combining source and destination alphas */
		enum blendfunc_t alpha_blend_func;		
		/* Which channels on the render target are written to */
		enum colormask_t write_mask;			
	} rendertargets[RENDER_TARGETS_MAX];
} blend_params_t;

/* Describes rasterization options for a pipeline */
typedef struct {
	/* Type of primitive topology to rasterize */
	enum primitivetype_t primitive;
	/* How triangles will be filled during rasterization */
	enum fillmode_t fill_mode;
	/* Which side of the triangles will be culled */
	enum cullmode_t cull_mode;
	/* If true triangles with vertices defined counter clockwise will be considered front facing else the vertices should be befined clockwise */
	bool facing;
	/* If true pixels are clipped against the near and far planes */
	bool depthclip;
	/* If true scissor-rectangle culls pixels */
	bool scissor;
	/* If true lines are anti-alliased */
	bool aa_lines;
	/* Constant bias to add to depth of pixels */
	int32_t depth_bias;
	/* Scales pixel slope before adding pisel depth */
	float scale_depth_bias;
} rasterizer_params_t;

/* Depth and stencil buffer creation options */
typedef struct {
	/* Whether stencil tests are used (if false it always passes) */
	bool stencil_enabled;
	/* Which bits are read from the stencil buffer */
	uint16_t stencil_read_mask;
	/* Which bits are written to the stencil buffer */
	uint16_t stencil_write_mask;
	/* Stencil operation to use on front [0] and back [1] facing fragments */
	struct {
		/* Comparison function between render target stencil buffer and new stencil value */
		enum comparefunc_t test_func;
		/* Performed when stencil test fails */
		enum stencilfunc_t sfail_func;
		/* Performed when stencil test passes but depth test fails */
		enum stencilfunc_t dpfail_func;
		/* Performed when both tests pass */
		enum stencilfunc_t dppass_func;			
	} stencil_operations[2];
	/* Whether depth tests are enabled (if false it always passes) */
	bool depth_enabled;
	/* Whether depth writes are enabled */
	bool depth_write_enabled;
	/* What camparison function is used to compare render target depth with pixel depth */
	enum comparefunc_t depth_func;
} depthstencil_params_t;

/* Sampler creation options */
typedef struct {
	/* Wrap mode per UVW texture coordinates */
	enum wrapmode_t wrap_modes[3];
	/* Minification filter */
	enum filtermode_t min_filter;
	/* Magnification filter */
	enum filtermode_t mag_filter;
	/* Mip filter (between mip levels)*/
	enum filtermode_t mip_filter;
	/* Minimum LOD value to clamp mip to */
	float mip_min;
	/* Maximum LOD value to clamp mip to */
	float mip_max;
	/* Mip offset to add to calculated mip level */
	float mip_bias;
	/* Maximum anisotropy level for the anisotropic filter */
	uint32_t max_anisotropy;
	/* Border color to set when WRAP_BORDER is used */
	vec4 border_color;
	/* If compare filter is used this function will be used to compare between old en new samples */
	enum comparefunc_t compare_filter_func;
} sampler_params_t;

/* Texture creation options */
typedef struct {
	enum usagehint_t usage;
	enum bindflags_t bindflags;
	enum cpuaccess_t cpu_access;
	enum pixeltype_t format;
	enum texturetype_t type;
	bool generate_mips;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t miplevels;
	uint32_t samples;
} texture_params_t;

typedef struct {
	enum uavflags_t uav_access;
	enum pixeltype_t format;
	enum textureviewtype_t viewtype;
	enum texturetype_t texturedim;
	bool generate_mips;
	uint32_t maxmiplevel;
	uint32_t start;
	uint32_t depth;
	uint32_t levels;
} textureview_params_t;

/* Shader type: */
typedef struct {
	enum shadertype_t type;
	const char* code;
} shader_params_t;

typedef struct {
	enum usagehint_t usage;
	enum bindflags_t bindflags;
	enum cpuaccess_t cpu_access;
	enum buffermode_t mode;
	uint32_t size;
	uint32_t stride;
} buffer_params_t;

typedef struct {
	enum bufferviewtype_t type;
	enum datavartype_t datatype;
	uint8_t components;
	bool normalized;
	uint32_t offset;
	uint32_t size;
} bufferview_params_t;

typedef struct texturedata_s {
	/* Pointer to the GPU buffer that contains subresource data */
	tc_rid_t buffer;
	/* Pointer to the subresource data in CPU memory */
	const void* data;
	/* When updating data from the buffer (src_buffer is not null), offset from the beginning of the buffer to the data start */
	uint32_t offset;
	/* For 2D and 3D textures, row stride in bytes */
	uint32_t stride;
	/* For 3D textures, depth slice stride in bytes */
	uint32_t depthstride;
} texturedata_t;

typedef struct attribute_s {
	/* Relative offset from start of index */
	uint32_t offset;
	/* Stride between elements in the vertex buffer */
	uint32_t stride;
	/* How many instances to draw per element */
	uint32_t instance_rate;
	/* Buffer slot index */
	uint8_t buffer_index;
	/* Data type for this attribute */
	enum datavartype_t type : 3;
	/* Number of elements in the attribute (for vectors and colors) */
	uint8_t components : 3;
	/* Whether the data type is normalized to 0,1 for float types or -1,1 for integer types */
	bool normalized : 1;
	/* Whether the attribute is instanced */
	bool instanced : 1;
} attribute_t;

typedef struct {
	attribute_t* attributes;
	uint32_t num_attributes;
} attribute_params_t_t;

typedef struct shadervar_s {
	sid_t name;
	enum shadertype_t stages;
	enum shadervartype_t type;
} shadervar_t;

typedef struct staticsampler_s {
	sampler_params_t caps;
	sid_t name;
	enum shadertype_t stages;
} staticsampler_t;

typedef struct {
	shadervar_t* variables;
	uint32_t num_variables;
	staticsampler_t* samplers;
	uint32_t num_samplers;
} uniform_params_t;

typedef struct shader_s {
	shader_params_t caps;

} shader_t;

/* Pipeline type: */
typedef struct {
	shader_t* vertex_shader;
	shader_t* pixel_shader;
	shader_t* geometry_shader;
	shader_t* domain_shader;
	shader_t* hull_shader;
	shader_t* compute_shader;
	/* Rasterization options that determine is and how traingles are rendered to the raster */
	rasterizer_params_t raster;
	/* Blending operations applied on the render targets */
	blend_params_t blending;
	/* Depth adn stencil buffer options */
	depthstencil_params_t depthstencil;
	/* Attribute layout for vertex buffer */
	attribute_params_t_t attributes;
	/* Uniform layout for shader variables and samplers */
	uniform_params_t uniforms;
	/* Number of viewports to render from */
	uint8_t num_viewports;
	/* Number of render targets to render to */
	uint8_t num_rendertargets;
	// Multisampling:
	/* Which samples are being writtin to */
	uint32_t sample_mask;
	/* Number of samples per pixel to take */
	uint8_t sample_count;
	/* Quality of the multisampler */
	uint8_t sample_quality;
	/* Sampler options to be used with multisampling */
	sampler_params_t multisampler;
	/* Pixel format to use for collor buffers */
	enum pixeltype_t color_format[RENDER_TARGETS_MAX];
	/* Pixel format to use for depth and stencil buffers */
	enum pixeltype_t depth_stencil_format;
	/* Whether the individual shaders should be seperated */
	bool seperable;
	/* Whether this pipeline is a compute shader pipeline */
	bool is_compute;
} pipeline_params_t;

typedef struct pipeline_s {
	pipeline_params_t caps;
	/* All shaders that are part of the pipeline (0 if not used) */
	shader_t* shaders[SHADER_MAX];
	/* Number of shaders this pipeline uses */
	uint8_t num_shaders;
	/* Hash to check pipeline uniquenes */
	uint32_t hash;
	/* Number of vertex buffers we are using */
	uint32_t num_buffer_slots;
	/* Strides per element for each vertex buffer */
	uint32_t strides[MAX_BUFFERS];
} pipeline_t;

/* Description of a renderpass attachment */
typedef struct {
	/* Format of the texture view that will be used for the attachment. */
	enum pixeltype_t format;
	/* Number of samples in the texture */
	uint8_t num_samples;
	/* Load operation for the content of color and depth components at the start of the pass */
	enum loadop_t load_op;
	/* Store operation for content of color and depth components at the end of the pass */
	enum storeop_t store_op;
	/* Load operation for the content of stencil component at the start of the pass */
	enum loadop_t stencil_load_op;
	/* Store operation for content of stencil component at the end of the pass */
	enum storeop_t stencil_store_op;
	/* State the attachment texture subresource will be in when a render pass instance begins */
	enum resourcestate_t initial_state;
	/* State the attachment texture subresource will be transitioned to when a render pass instance ends */
	enum resourcestate_t final_state;
} attachment_params_t;

typedef struct {
	uint32_t index;
	enum resourcestate_t state;
} attachment_ref;

/* Subpass of a renderpass */
typedef struct {
	uint32_t num_input_attachments;
	attachment_ref* input_attachements;
	uint32_t num_rendertarget_attachments;
	attachment_ref* rendertarget_attachments;
	attachment_ref* resolve_attachments;
	attachment_ref* depthstencil_attachments;
	uint32_t num_preserve_attachments;
	uint32_t* preserve_attachments;
} subpass_params_t;

typedef struct {
	uint32_t src_pass;
	uint32_t dst_pass;
	uint32_t src_stage_mask;
	uint32_t dst_stage_mask;
	uint32_t src_access_mask;
	uint32_t dst_access_mask;
} passdependency_params_t;

typedef struct {
	uint32_t num_attachments;
	attachment_params_t* attachments;
	uint32_t num_subpasses;
	subpass_params_t* subpasses;
	uint32_t num_dependencies;
	passdependency_params_t* dependencies;
} renderpass_params_t;

inline size_t datatype_size(enum datavartype_t type) {
	switch (type) {
	case DATA_UINT8:    return sizeof(uint8_t);
	case DATA_UINT16:   return sizeof(uint16_t);
	case DATA_UINT32:   return sizeof(uint32_t);
	case DATA_INT8:		return sizeof(int8_t);
	case DATA_INT16:	return sizeof(int16_t);
	case DATA_INT32:	return sizeof(int32_t);
	case DATA_FLOAT16:	return sizeof(float) / 2;
	case DATA_FLOAT32:  return sizeof(float);
	default: TRACE(LOG_WARNING, "[Attributes]: Data format not supported (%i)", type);
		return 0;
	}
}

inline enum shadervartype_t shader_variable_type(uint8_t stages, sid_t name, const uniform_params_t* layout) {
	for (uint32_t i = 0; i < layout->num_variables; i++) {
		shadervar_t* var = &layout->variables[i];
		if ((var->stages & stages) && (var->name == name))
			return var->type;
	}
	return UNIFORM_STATIC;
}

inline int32_t find_static_sampler_index(staticsampler_t* samplers, uint32_t num_samplers, uint8_t stages, sid_t name) {
	for (uint32_t i = 0; i < num_samplers; i++) {
		staticsampler_t* sampler = &samplers[i];
		if ((sampler->stages & stages) && sampler->name == name)
			return i;
	}
	return -1;
}

inline uint32_t compute_mip_levels(texture_params_t* caps) {
	uint32_t length = 0;
	if (caps->type == TEXTURE_1D || caps->type == TEXTURE_1D_ARRAY)
		length = caps->width;
	else if (caps->type == TEXTURE_2D || caps->type == TEXTURE_2D_ARRAY ||
		caps->type == TEXTURE_CUBE || caps->type == TEXTURE_CUBE_ARRAY)
		length = max(caps->width, caps->height);
	else if (caps->type == TEXTURE_3D)
		length = max(max(caps->width, caps->height), caps->depth);
	else TC_ERROR("[Texture]: Unkown texture type");
	uint32_t miplevels = 0;
	if (length == 0) return 0;
	while ((length >> miplevels) > 0) miplevels++;
	TC_ASSERT(length >= (1U << (miplevels - 1)) && length < (1U << miplevels), "[Texture]: Incorrect number of mip levels");
	return miplevels;
}

typedef struct miplevel_s {
	uint32_t width_pow2;
	uint32_t height_pow2;
	uint32_t storage_width;
	uint32_t storage_height;
	uint32_t depth;
	uint32_t row_size;
	uint32_t depth_size;
	uint32_t mipsize;
} miplevel_t;

inline miplevel_t get_mip_level(texture_params_t* caps, uint32_t level) {
	miplevel_t miplevel = { 0 };
	pixelblockinfo_t fmt = pixel_block_format(caps->format);
	miplevel.width_pow2 = max(caps->width >> level, 1u);
	miplevel.height_pow2 = max(caps->height >> level, 1u);
	miplevel.depth = (caps->type == TEXTURE_3D) ? max(caps->depth >> level, 1u) : 1u;
	if (is_compressed(caps->format)) {
		TC_ASSERT(fmt.block_width > 1 && fmt.block_height > 1);
		TC_ASSERT((fmt.block_width & (fmt.block_width - 1)) == 0, "[Texture]: Compressed block width is expected to be power of 2");
		TC_ASSERT((fmt.block_height & (fmt.block_height - 1)) == 0, "[Texture]: Compressed block height is expected to be power of 2");
		miplevel.storage_width = align(miplevel.width_pow2, fmt.block_width);
		miplevel.storage_height = align(miplevel.height_pow2, fmt.block_height);
		miplevel.row_size = miplevel.storage_width / fmt.block_width * fmt.block_size;
	}
	//TODO: fill in other info
	return miplevel;
}

// Device feature caps
enum device_feature_state_t {
	DEVICE_FEATURE_STATE_DISABLED = 0,
	DEVICE_FEATURE_STATE_ENABLED,
	DEVICE_FEATURE_STATE_OPTIONAL,
};

enum renderer_type_t {
	RENDERER_UNKNOWN = 0,
	RENDERER_VULKAN,
};

typedef struct gfxfeatures_s {
	/* Seperable programs are supported */
	enum device_feature_state_t seperable_programs;
	/* Indirect draw commands are supported */
	enum device_feature_state_t indirect_draw;
	/* Wireframe filling rasterization is supported */
	enum device_feature_state_t fill_wireframe;
	/* Multithreaded resource creation is supported */
	enum device_feature_state_t multithreaded_creation;
	/* Geometry shaders are supported */
	enum device_feature_state_t geometry_shaders;
	/* Tesselation is supported */
	enum device_feature_state_t tessellation;
	/* Mesh shaders are supported */
	enum device_feature_state_t mesh_shaders;
	/* Ray tracing shaders are supported */
	enum device_feature_state_t raytracing;
	/* Compute shaders are supported */
	enum device_feature_state_t compute_shaders;
	/* Bindless resources are supported */
	enum device_feature_state_t bindless_resources;
	/* Depth clamping is supported */
	enum device_feature_state_t depth_clamp;
	/* Depth bias clamping is supported */
	enum device_feature_state_t depth_bias_clamp;
	/* Blend per render target is supported */
	enum device_feature_state_t independent_blend;
	/* Dual-source blending is supported */
	enum device_feature_state_t dual_source_blend;
	/* Multiple viewports are supported */
	enum device_feature_state_t multi_viewport;
	/* Which pixel formats are supported */
	enum device_feature_state_t texture_compression_BC;
	/* UAVs writes to vertex, tesselation and geometry shaders are supported */
	enum device_feature_state_t vertex_UAV_writes;
	/* UAVs writes to pixel shader are supported */
	enum device_feature_state_t pixel_UAV_writes;
	/* UAV texture formats are available in shaders */
	enum device_feature_state_t texture_UAV_formats;
	/* Native 16-bit float (half precision) operations are supported */
	enum device_feature_state_t shader_float16;
	/* Native 8-bit integer operations are supported for shader resource or UAV buffers */
	enum device_feature_state_t resource_buffer8;
	/* Native 16-bit integer operations are supported for shader resource or UAV buffers */
	enum device_feature_state_t resource_buffer16;
	/* 8-bit types reading from uniform buffers is supported */
	enum device_feature_state_t uniform_buffer8;
	/* 16-bit types reading from uniform buffers is supported */
	enum device_feature_state_t uniform_buffer16;
	/* Native 8-bit integers operations are supported */
	enum device_feature_state_t shader_int8;
	/* 16-bit floats and ints can be used ad input/output of a shader */
	enum device_feature_state_t shader_io16;
	/* Multisampling for 2d textures is supported */
	enum device_feature_state_t texture_MS;
	/* Multisampling for 2d texture arrays is supported */
	enum device_feature_state_t texture_array_MS;
	/* Texture views are supported */
	enum device_feature_state_t texture_views;
	/* Cubemap arrays are supported */
	enum device_feature_state_t cube_array;
	/* Occlusion queries are supported */
	enum device_feature_state_t occlusion_queries;
	/* Binary occlusion queries are supported */
	enum device_feature_state_t binary_occlusion_queries;
	/* Timestamp queries are supported */
	enum device_feature_state_t timestamp_queries;
	/* Pipeline queries are supported */
	enum device_feature_state_t pipeline_statistics_queries;
} gfxfeatures_t;

typedef struct graphicscaps_s {
	enum renderer_type_t type;
	int32_t major_version;
	int32_t minor_version;
	gfxfeatures_t features;
	struct {
		/* Adapter description */
		char description[128];
		/* Adapter type (software or hardware) */
		enum adaptertype_t type;
		/* Adapter vendor */
		enum gpuvendor_t vendor;
		/* PCI id of hardware vendor */
		uint32_t vendor_id;
		/* PCI id of hardware device */
		uint32_t device_id;
		/* Number of video outputs on adapter */
		uint32_t num_outputs;
		/* Amount of device local video memory that can't be accessed by cpu */
		uint64_t device_local_memory;
		/* Amount of host-visible memory that is visible by the GPU */
		uint64_t host_visible_memory;
		/* Amount of memory that is accesable by both CPU and GPU */
		uint64_t unified_memory;
		/* Supported access types for unified memory */
		enum cpuaccess_t unified_memory_cpu_access;
	} adapter;
	struct {
		/* Maximum width for 1d textures */
		uint32_t max_1d_dims;
		/* Maximum width or height for 2d textures */
		uint32_t max_2d_dims;
		/* Maximum width height or depth for 3d textures */
		uint32_t max_3d_dims;
		/* Maximum width or height for cubemaps */
		uint32_t max_cube_dims;
		/* Maximum array elements for 1d texture arrays or 0 if not supported */
		uint32_t max_1d_array_slices;
		/* Maximum array elements for 2d texture arrays or 0 if not supported */
		uint32_t max_2d_array_slices;
		/* Maximum array elements for cubemap arrays or 0 if not supported */
		uint32_t max_cube_array_slices;
	} texture;
	struct {
		/* Border sampling is supported */
		bool border_sampling;
		/* Anisotropic filtering is supported */
		bool anisotropic_filtering;
		/* Mip load bias is supported */
		bool LOD_bias;
	} sampler;
} graphicscaps_t;

void gfx_init(tc_allocator_i* a);
void gfx_close();

void pipeline_init(pipeline_t* pipe, pipeline_params_t caps);