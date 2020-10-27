/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/

#pragma once
#include "tccore.h"
#include "tcimage.h"
#include "tctypes.h"
#include "tcmath.h"
#include "tcdata.h"
#include "tclog.h"

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
TC_ENUM(ColorMask, uint8_t) {
	COLOR_MASK_NONE = 0,
	COLOR_MASK_RED = 1 << 0,
	COLOR_MASK_GREEN = 1 << 1,
	COLOR_MASK_BLUE = 1 << 2,
	COLOR_MASK_ALPHA = 1 << 3,
	COLOR_MASK_ALL = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
};

// Blend function to describe how to blend colors of the render target with the output of the fragment shader
TC_ENUM(BlendFunc, uint8_t) {
	BLEND_ADD = 0,
	BLEND_SUBTRACT,
	BLEND_REV_SUBTRACT,
	BLEND_MIN,
	BLEND_MAX,
	BLEND_FUNC_MAX
};

// Blend factor to use in blend function per rgba channels
TC_ENUM(BlendFactor, uint8_t) {
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
TC_ENUM(FillMode, uint8_t) {
	FILL_SOLID = 0,
	FILL_WIRE,
	FILL_MAX
};

// Rasterization cull modes
TC_ENUM(CullMode, uint8_t) {
	CULL_NONE = 0,
	CULL_FRONT,
	CULL_BACK,
	CULL_MAX
};

// Primitive rasterizer type
TC_ENUM(PrimitiveType, uint8_t) {
	PRIMITIVE_POINTS,
	PRIMITIVE_LINES,
	PRIMITIVE_TRIANGLE,
	PRIMITIVE_TRIANGLESTRIP,
	PRIMITIVE_TRIANGLEFAN,
	PRIMITIVE_MAX
};

// Function applied on stencil buffer when rendering to it
TC_ENUM(StencilFunc, uint8_t) {
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
TC_ENUM(FilterMode, uint8_t) {
	FILTER_NEAREST = 0,
	FILTER_LINEAR,
	FILTER_ANISOTROPIC,
	FILTER_MAX
};

TC_ENUM(WrapMode, uint8_t) {
	WRAP_CLAMP = 0,
	WRAP_REPEAT,
	WRAP_MIRROR,
	WRAP_BORDER,
	WRAP_MODE_MAX,
};

// Buffer or texture view types:
TC_ENUM(BindFlags, uint8_t) {
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
};

TC_ENUM(UsageHint, uint8_t) {
	USAGE_STATIC = 0,
	USAGE_DYNAMIC,
	USAGE_STREAM,
	USAGE_STAGING,
};

// Data types in shader attributes
TC_ENUM(DataType, uint8_t) {
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

TC_ENUM(ShaderVariableType, uint8_t) {
	UNIFORM_CONST_BUFFER = 0,			// Constant uniform buffer
	UNIFORM_BUFFER_SRV,					// Read-only storage image
	UNIFORM_BUFFER_UAV,					// Storage buffer
	UNIFORM_TEXTURE_SRV,				// Sampled image
	UNIFORM_TEXTURE_UAV,				// Storage image
	UNIFORM_SAMPLER,					// Separate sampler
	UNIFORM_RESOURCE_MAX,
};

TC_ENUM(VariableUsageHint, uint8_t) {
	UNIFORM_STATIC = 0,
	UNIFORM_MUTABLE,
	UNIFORM_DYNAMIC,
	UNIFORM_VARIABLE_MAX,
};

// Types of shader stages
TC_ENUM(ShaderType, uint8_t) {
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
TC_ENUM(TextureType, uint8_t) {
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
TC_ENUM(BufferMode, uint8_t) {
	BUFFER_RAW = 0,
	BUFFER_FORMATTED,
	BUFFER_STRUCTURED,
	BUFFER_MODE_MAX
};

// Framebuffer read or write type
TC_ENUM(FramebufferType, uint8_t) {
	FRAMEBUFFER_READ = 1 << 0,
	FRAMEBUFFER_WRITE = 1 << 1,
	FRAMEBUFFER_ALL = FRAMEBUFFER_READ | FRAMEBUFFER_WRITE,
};

TC_ENUM(CpuAccessHint, uint8_t) {
	CPU_NO_ACCESS = 0,
	CPU_READ = 1 << 0,
	CPU_WRITE = 1 << 1,
};

TC_ENUM(MapType, uint8_t) {
	MAP_READ = 1 << 0,
	MAP_WRITE = 1 << 1,
	MAP_READ_WRITE = MAP_READ |	MAP_WRITE,
};

TC_ENUM(MapFlags, uint8_t) {
	MAP_FLAG_NO_WAIT = 1 << 0,
	MAP_FLAG_DISCARD = 1 << 1,
	MAP_FLAG_NO_OVERWRITE = 1 << 2,
};

TC_ENUM(UAVFlags, uint8_t) {
	UAV_NONE = 0,
	UAV_READ = 1 << 0,
	UAV_WRITE = 1 << 1,
	UAV_READ_WRITE = UAV_READ | UAV_WRITE,
};

TC_ENUM(TextureViewType, uint8_t) {
	TEXTURE_VIEW_SHADER_RESOURCE = 0,
	TEXTURE_VIEW_RENDER_TARGET,
	TEXTURE_VIEW_DEPTH_STENCIL,
	TEXTURE_VIEW_UNORDERED_ACCESS,
	TEXTURE_VIEW_MAX,
};

TC_ENUM(BufferViewType, uint8_t) {
	BUFFER_VIEW_SHADER_RESOURCE = 0,
	BUFFER_VIEW_UNORDERED_ACCESS,
	BUFFER_VIEW_MAX,
};

TC_ENUM(ResourceState, uint32_t) {
	STATE_NONE = 0,
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
	STATE_PRESENT = 1 << 14,
	STATE_GENERIC_READ = STATE_VERTEX_BUFFER | STATE_INDEX_BUFFER | STATE_UNIFORM_BUFFER | STATE_INDIRECT_DRAW | STATE_SHADER_RESOURCE | STATE_COPY_SRC,
};

TC_ENUM(GPUVendor, uint8_t) {
	VENDOR_UNKNOWN,
	VENDOR_INTEL,
	VENDOR_AMD,
	VENDOR_NVIDIA,
	VENDOR_MESA_SOFT,
	VENDOR_APPLE,
	VENDOR_MICROSOFT,
	VENDOR_IMGTEC,
	VENDOR_ARM,
	VENDOR_QUALCOMM,
	VENDOR_BROADCOM,
	VENDOR_VIVANTE,
	VENDOR_MAX
};

/* Type declaration: */
typedef struct GPUBufferView;

// Describes the blending operations in a pipeline
typedef struct BlendOptions {
	bool alpha_to_coverage;				// Use the alpha channel as a coverage mask for anti-aliasing
	bool blend_per_rt;					// Blend independently in multiple render targets
	struct {
		bool blend;						// Is blending enabled for this render target
		bool blendoperation;			// Do we use an blend function for this render target

		BlendFactor src_blend;			// Blend factor applied on the output of the pixel shader
		BlendFactor dest_blend;			// Blend factor applied on RGB in the render target
		BlendFunc blend_func;			// Blending operation to be used when combining source and destination colors

		BlendFactor src_alpha_blend;	// Blend factor applied on the alpha output of the pixel shader
		BlendFactor dest_alpha_blend;	// Blend factor applied on the alpha channel of the render target
		BlendFunc alpha_blend_func;		// Blending operation to be used when combining source and destination alphas

		ColorMask write_mask;			// Which channels on the render target are written to
	} rendertargets[RENDER_TARGETS_MAX];
} BlendOptions;

// Describes rasterization options for a pipeline
typedef struct RasterizationOptions {
	PrimitiveType primitive;			// Type of primitive topology to rasterize
	FillMode fill_mode;					// How triangles will be filled during rasterization
	CullMode cull_mode;					// Which side of the triangles will be culled
	bool facing;						// If true triangles with vertices defined counter clockwise will be considered front facing else the vertices should be befined clockwise
	bool depthclip;						// If true pixels are clipped against the near and far planes
	bool scissor;						// If true scissor-rectangle culls pixels
	bool aa_lines;						// If true lines are anti-alliased
	int32_t depth_bias;					// Constant bias to add to depth of pixels
	float scale_depth_bias;				// Scales pixel slope before adding pisel depth
} RasterizationOptions;

// Depth and stencil buffer options
typedef struct DepthStencilOptions {
	bool stencil_enabled;				// Whether stencil tests are used (if false it always passes)
	uint16_t stencil_read_mask;			// Which bits are read from the stencil buffer
	uint16_t stencil_write_mask;		// Which bits are written to the stencil buffer
	struct {
		CompareFunc test_func;			// Comparison function between render target stencil buffer and new stencil value
		StencilFunc sfail_func;			// Performed when stencil test fails
		StencilFunc dpfail_func;		// Performed when stencil test passes but depth test fails
		StencilFunc dppass_func;		// Performed when both tests pass
	} stencil_operations[2];			// Stencil operation to use on front [0] and back [1] facing fragments
	bool depth_enabled;					// Whether depth tests are enabled (if false it always passes)
	bool depth_write_enabled;			// Whether depth writes are enabled
	CompareFunc depth_func;				// What camparison function is used to compare render target depth with pixel depth
} DepthStencilOptions;

// Sampler options
typedef struct SamplerOptions {
	WrapMode wrap_modes[3];				// Wrap mode per UVW texture coordinates
	FilterMode min_filter;				// Minification filter
	FilterMode mag_filter;				// Magnification filter
	FilterMode mip_filter;				// Mip filter
	float mip_min;						// Minimum LOD value to clamp mip to
	float mip_max;						// Maximum LOD value to clamp mip to
	float mip_bias;						// Mip offset to add to calculated mip level
	uint32_t max_anisotropy;			// Maximum anisotropy level for the anisotropic filter
	vec4 border_color;					// Border color to set when WRAP_BORDER is used
	CompareFunc compare_filter_func;	// If compare filter is used this function will be used to compare between old en new samples
} SamplerOptions;

typedef struct TextureOptions {
	UsageHint usage;
	BindFlags bindflags;
	CpuAccessHint cpu_access;
	PixelType format;
	TextureType type;
	bool generate_mips;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t miplevels;
	uint32_t samples;
} TextureOptions;

typedef struct TextureViewOptions {
	UAVFlags uav_access;
	PixelType format;
	TextureViewType viewtype;
	TextureType texturedim;
	bool generate_mips;
	uint32_t maxmiplevel;
	uint32_t start;
	uint32_t depth;
	uint32_t levels;
} TextureViewOptions;

/* Shader type: */
typedef struct ShaderOptions {
	StringID code;
	ShaderType type;
} ShaderOptions;

typedef struct BufferOptions {
	UsageHint usage;
	BindFlags bindflags;
	CpuAccessHint cpu_access;
	BufferMode mode;
	uint32_t size;
	uint32_t stride;
} BufferOptions;

typedef struct BufferViewOptions {
	BufferViewType type;
	DataType datatype;
	uint8_t components;
	bool normalized;
	uint32_t offset;
	uint32_t size;
} BufferViewOptions;

typedef struct TextureData {
	rid_t buffer;					// Pointer to the GPU buffer that contains subresource data
	const void* data;				// Pointer to the subresource data in CPU memory
	uint32_t offset;				// When updating data from the buffer (src_buffer is not null), offset from the beginning of the buffer to the data start
	uint32_t stride;				// For 2D and 3D textures, row stride in bytes
	uint32_t depthstride;			// For 3D textures, depth slice stride in bytes
} TextureData;

typedef struct Attribute {
	uint32_t offset;                // Relative offset from start of index
	uint32_t stride;				// Stride between elements in the vertex buffer
	uint32_t instance_rate;			// How many instances to draw per element
	uint8_t buffer_index;           // Buffer slot index
	DataType type : 3;              // Data type for this attribute
	uint8_t components : 3;         // Number of elements in the attribute (for vectors and colors)
	bool normalized : 1;			// Whether the data type is normalized to 0,1 for float types or -1,1 for integer types
	bool instanced : 1;				// Whether the attribute is instanced
} Attribute;

typedef struct AttributeLayout {
	Attribute* attributes;
	uint32_t num_attributes;
} AttributeLayout;

typedef struct ShaderVariable {
	StringID name;
	ShaderType stages;
	ShaderVariableType type;
} ShaderVariable;

typedef struct StaticSampler {
	StringID name;
	ShaderType stages;
	SamplerOptions settings;
} StaticSampler;

typedef struct UniforLayout {
	ShaderVariable* variables;
	StaticSampler* samplers;
	uint32_t num_variables;
	uint32_t num_samplers;
} UniformLayout;


/* Pipeline type: */

typedef struct PipelineOptions {
	rid_t vertex_shader;
	rid_t pixel_shader;
	rid_t geometry_shader;
	rid_t domain_shader;
	rid_t hull_shader;
	rid_t compute_shader;

	RasterizationOptions raster;		// Rasterization options that determine is and how traingles are rendered to the raster
	BlendOptions blending;				// Blending operations applied on the render targets
	DepthStencilOptions depthstencil;	// Depth adn stencil buffer options
	AttributeLayout attributes;			// Attribute layout for vertex buffer
	UniformLayout uniforms;				// Uniform layout for shader variables and samplers

	uint8_t num_viewports;				// Number of viewports to render from
	uint8_t num_rendertargets;			// Number of render targets to render to

	// Multisampling:
	uint32_t sample_mask;				// Which samples are being writtin to
	uint8_t sample_count;				// Number of samples per pixel to take
	uint8_t sample_quality;				// Quality of the multisampler
	SamplerOptions multisample_options;	// Sampler options to be used with multisampling

	PixelType color_format[RENDER_TARGETS_MAX];	// Pixel format to use for collor buffers
	PixelType depth_stencil_format;		// Pixel format to use for depth and stencil buffers

	bool seperable;						// Whether the individual shaders should be seperated
	bool is_compute;					// Whether this pipeline is a compute shader pipeline
} PipelineOptions;

typedef struct Pipeline {
	PipelineOptions options;
	rid_t shaders[SHADER_MAX];			// All shaders that are part of the pipeline (0 if not used)
	uint8_t num_shaders;				// Number of shaders this pipeline uses
	uint32_t hash;						// Hash to check pipeline uniqueness
	uint32_t num_buffer_slots;			// Number of vertex buffers we are using
	uint32_t strides[MAX_BUFFERS];		// Strides per element for each vertex buffer
} Pipeline;

inline void pipeline_init(Pipeline* pipe, PipelineOptions options) {
	pipe->options = options;

	BUFFER_OF(ShaderVariable) variable_b = BUFFER_INIT;
	BUFFER_OF(StaticSampler) sampler_b = BUFFER_INIT;
	BUFFER_OF(Attribute) attribute_b = BUFFER_INIT;

	// Check and initialize uniforms, textures samplers
	UniformLayout uniforms = options.uniforms;
	if (uniforms.variables != NULL) {
		for (int i = 0; i < uniforms.num_variables; i++) {
			TC_ASSERT(uniforms.variables[i].name != 0, "[Pipeline]: Variable name cannot be null");
			BUFFER_PUSH(&variable_b, uniforms.variables[i]);
		}
	}
	if (uniforms.samplers != NULL) {
		for (int i = 0; i < uniforms.num_samplers; i++) {
			TC_ASSERT(uniforms.samplers[i].name != 0, "[Pipeline]: Sampler or texture name cannot be null");
			BUFFER_PUSH(&sampler_b, uniforms.samplers[i]);
		}
	}

	// Check and initialize shaders and vertices
	if (options.is_compute) {
		TC_ASSERT(options.compute_shader != 0, "[Pipeline]: Compute pipeline should have compute shader");
		TC_ASSERT(options.attributes.num_attributes == 0, "[Pipeline]: Compute pipeline cannot have attributes");
		pipe->shaders[pipe->num_shaders++] = options.compute_shader;
	}
	else {
		if (options.vertex_shader)	pipe->shaders[pipe->num_shaders++] = options.vertex_shader;
		if (options.pixel_shader)	pipe->shaders[pipe->num_shaders++] = options.pixel_shader;
		if (options.geometry_shader)pipe->shaders[pipe->num_shaders++] = options.geometry_shader;
		if (options.domain_shader)	pipe->shaders[pipe->num_shaders++] = options.domain_shader;
		if (options.hull_shader)	pipe->shaders[pipe->num_shaders++] = options.hull_shader;
		if (pipe->num_shaders == 0)
			TRACE(LOG_ERROR, "[Pipeline]: There must be at least 1 shader in the pipeline");

		uint32_t autostrides[MAX_BUFFERS] = { 0 };

		for (int i = 0; i < MAX_BUFFERS; i++) pipe->strides[i] = 0xFFFFFFFF;

		AttributeLayout attributes = options.attributes;
		for (int i = 0; i < attributes.num_attributes; i++) {
			Attribute attr = attributes.attributes[i];
			if (attr.type == DATA_FLOAT32 && attr.normalized == true) {
				TRACE(LOG_WARNING, "[Pipeline]: Float data type cannot be normalized");
				attr.normalized = false;
			}
			uint32_t buffslot = attr.buffer_index;
			if (buffslot >= MAX_BUFFERS) {
				TRACE(LOG_WARNING, "[Pipeline]: Buffer slot %i exceeds maximum buffer slot %i", buffslot, MAX_BUFFERS);
				continue;
			}
			pipe->num_buffer_slots = max(pipe->num_buffer_slots, buffslot + 1);
			if (attr.offset == 0xFFFFFFFF)
				attr.offset = autostrides[buffslot];

			if (attr.stride != 0xFFFFFFFF) {
				if (pipe->strides[buffslot] != 0xFFFFFFFF && pipe->strides[buffslot] != attr.stride) {
					TRACE(LOG_WARNING, "[Pipeline]: Buffer stride could be consistent for each attribute in the same buffer slot");
					attr.stride = 0xFFFFFFFF;
				}
				pipe->strides[buffslot] = attr.stride;
			}
			autostrides[buffslot] = max(autostrides[buffslot], attr.offset + attr.components * gl_datasize(attr.type));
		}

		for (int i = 0; i < attributes.num_attributes; i++) {
			Attribute attr = attributes.attributes[i];
			uint32_t buffslot = attr.buffer_index;
			if (pipe->strides[buffslot] == 0xFFFFFFFF)
				pipe->strides[buffslot] = autostrides[buffslot];
			else if (pipe->strides[buffslot] < autostrides[buffslot])
				TRACE(LOG_ERROR, "[Pipeline]: Specified stride %i for buffer %i is too small for all elements", pipe->strides[buffslot], buffslot);

			if (attr.stride == 0xFFFFFFFF)
				attr.stride = pipe->strides[buffslot];

			BUFFER_PUSH(&attribute_b, attr);
		}

		for (int i = 0; i < pipe->num_buffer_slots; i++)
			if (pipe->strides[i] == 0xFFFFFFFF)
				pipe->strides[i] = 0;
	}
	pipe->options.attributes.attributes = attribute_b.data;
	pipe->options.uniforms.variables = variable_b.data;
	pipe->options.uniforms.samplers = sampler_b.data;
}

inline size_t data_type_size(DataType type) {
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

inline void correct_buffer_view_options(BufferViewOptions* options, BufferOptions* buffer_options) {
	if (options->size == 0) {
		TC_ASSERT(buffer_options->size > options->offset, "[GPUBuffer]: Buffer offset exceeds buffer size");
		options->size = buffer_options->size - options->offset;
	}
	if (options->offset + options->size > buffer_options->size)
		TC_ERROR("[GPUBuffer]: Buffer view range [%i, %i) is out of the buffer boundaries [0, %i).", options->offset, options->offset + options->size, buffer_options->size);
	if ((buffer_options->bindflags & BIND_UNORDERED_ACCESS) || (buffer_options->bindflags & BIND_SHADER_VARIABLE)) {
		if (buffer_options->mode == BUFFER_STRUCTURED || buffer_options->mode == BUFFER_FORMATTED) {
			TC_ASSERT(buffer_options->stride != 0, "[GPUBuffer]: Element byte stride is zero");
			if ((options->offset % buffer_options->stride) != 0)
				TC_ERROR("[GPUBuffer]: Buffer view byte offset (%i) is not multiple of element byte stride (%i).", options->offset, buffer_options->stride);
			if ((options->size % buffer_options->stride) != 0)
				TC_ERROR("[GPUBuffer]: Buffer view byte width (%i) is not multiple of element byte stride (%i).", options->size, buffer_options->stride);
		}
		if (buffer_options->mode == BUFFER_FORMATTED && options->datatype == DATA_UNDEFINED)
			TC_ERROR("[GPUBuffer]: Format must be specified when creating a view of a formatted buffer");
		if (buffer_options->mode == BUFFER_FORMATTED || (buffer_options->mode == BUFFER_RAW && options->datatype != DATA_UNDEFINED)) {
			if (options->components <= 0 || options->components > 4)
				TC_ERROR("[GPUBuffer]: Incorrect number of components (%i). 1, 2, 3, or 4 are allowed values", options->components);
			if (options->datatype == DATA_FLOAT32 || options->datatype == DATA_FLOAT16)
				options->normalized = true;
			size_t stride = data_type_size(options->datatype) * options->components;
			if (buffer_options->mode == BUFFER_RAW && buffer_options->stride == 0)
				TC_ERROR("[GPUBuffer]: To enable formatted views of a raw buffer, element byte must be specified during buffer initialization");
			if (stride != buffer_options->stride)
				TC_ERROR("[GPUBuffer]: Buffer element byte stride (%i) is not consistent with the size (%i) defined by the format of the view", buffer_options->stride, stride);
		}
		if (buffer_options->mode == BUFFER_RAW && options->datatype == DATA_UNDEFINED && (options->offset % 16) != 0)
			TC_ERROR("[GPUBuffer]: When creating a RAW view, the offset of the first element from the start of the buffer (%i) must be a multiple of 16 bytes", options->offset);
	}
}

inline void correct_texture_options(TextureOptions* options) {
	if (options->type == TEXTURE_UNKNOWN)
		TC_ERROR("[Texture]: Texture type is undefined");
	if (!(options->type >= TEXTURE_1D && options->type <= TEXTURE_CUBE_ARRAY))
		TC_ERROR("[Texture]: Unexpected texture type");
	if (options->width == 0)
		TC_ERROR("[Texture]: Texture width cannot be zero");
	if (options->type == TEXTURE_1D || options->type == TEXTURE_1D_ARRAY) {
		if (options->height != 1)
			TC_ERROR("[Texture]: Height (%i) of texture 1D/1D array must be 1", options->height);
	}
	else if (options->height == 0) {
		TC_ERROR("[Texture]: Texture height cannot be zero");
	}
	if (options->type == TEXTURE_3D && options->depth == 0)
		TC_ERROR("[Texture]: 3D texture depth cannot be zero");
	if ((options->type == TEXTURE_1D || options->type == TEXTURE_2D) && options->depth != 1)
		TC_ERROR("[Texture]: Texture 1D/2D must have one array slice (%i provided). Use Texture 1D/2D array if you need more than one slice.", options->depth);
	if (options->type == TEXTURE_CUBE || options->type == TEXTURE_CUBE_ARRAY) {
		if (options->width != options->height)
			TC_ERROR("[Texture]: For cube map textures, texture width (%i provided) must match texture height (%i provided)", options->width, options->height);
		if (options->depth < 6)
			TC_ERROR("[Texture]: Texture cube/cube array must have at least 6 slices (%i provided).", options->depth);
	}
	uint32_t maxdim = 0;
	if (options->type == TEXTURE_1D || options->type == TEXTURE_1D_ARRAY)
		maxdim = options->width;
	else if (options->type == TEXTURE_2D || options->type == TEXTURE_2D_ARRAY || options->type == TEXTURE_CUBE)
		maxdim = max(options->width, options->height);
	else if (options->type == TEXTURE_3D)
		maxdim = max(max(options->width, options->height), options->depth);
	TC_ASSERT(maxdim >= (1U << (options->miplevels - 1)), "[Texture]: Incorrect number of mip levels (%i)", options->miplevels);
	if (options->samples > 1) {
		if (!(options->type == TEXTURE_2D || options->type == TEXTURE_2D_ARRAY))
			TC_ERROR("[Texture]: Only Texture 2D/Texture 2D Array can be multisampled");
		if (options->miplevels != 1)
			TC_ERROR("[Texture]: Multisampled textures must have one mip level (%i levels specified)", options->miplevels);
		if (options->bindflags & BIND_UNORDERED_ACCESS)
			TC_ERROR("[Texture]: UAVs are not allowed for multisampled resources");
	}
	if (options->usage == USAGE_STAGING) {
		if (options->bindflags != 0)
			TC_ERROR("[Texture]: Staging textures cannot be bound to any GPU pipeline stage");
		if (options->generate_mips)
			TC_ERROR("[Texture]: Mipmaps cannot be autogenerated for staging textures");
		if (options->cpu_access == 0)
			TC_ERROR("[Texture]: Staging textures must specify CPU access flags");
		if ((options->cpu_access & (CPU_READ | CPU_WRITE)) == (CPU_READ | CPU_WRITE))
			TC_ERROR("[Texture]: Staging textures must use exactly one of CPU_READ or CPU_WRITE flags");
	}
}

inline ShaderVariableType shader_variable_type(ShaderType stages, StringID name, const UniformLayout* layout) {
	for (int i = 0; i < layout->num_variables; i++) {
		ShaderVariable* var = &layout->variables[i];
		if ((var->stages & stages) && (var->name == name))
			return var->type;
	}
	return UNIFORM_STATIC;
}

inline int32_t find_static_sampler_index(StaticSampler* samplers, uint32_t num_samplers, ShaderType stages, StringID name) {
	for (uint32_t i = 0; i < num_samplers; i++) {
		StaticSampler* sampler = &samplers[i];
		if ((sampler->stages & stages) && sampler->name == name)
			return i;
	}
	return -1;
}

inline uint32_t compute_mip_levels(TextureOptions* options) {
	uint32_t length = 0;
	if (options->type == TEXTURE_1D || options->type == TEXTURE_1D_ARRAY)
		length = options->width;
	else if (options->type == TEXTURE_2D || options->type == TEXTURE_2D_ARRAY ||
		options->type == TEXTURE_CUBE || options->type == TEXTURE_CUBE_ARRAY)
		length = max(options->width, options->height);
	else if (options->type == TEXTURE_3D)
		length = max(max(options->width, options->height), options->depth);
	else
		TC_ERROR("[Texture]: Unkown texture type");
	uint32_t miplevels = 0;
	if (length == 0) return 0;
	while ((length >> miplevels) > 0)
		miplevels++;
	TC_ASSERT(length >= (1U << (miplevels - 1)) && length < (1U << miplevels), "[Texture]: Incorrect number of mip levels");
	return miplevels;
}

typedef struct MipLevel {
	uint32_t width_pow2;
	uint32_t height_pow2;
	uint32_t storage_width;
	uint32_t storage_height;
	uint32_t depth;
	uint32_t row_size;
	uint32_t depth_size;
	uint32_t mipsize;
} MipLevel;

inline MipLevel get_mip_level(TextureOptions* options, uint32_t level) {
	MipLevel miplevel = { 0 };
	PixelBlockInfo fmt = pixel_block_format(options->format);
	miplevel.width_pow2 = max(options->width >> level, 1u);
	miplevel.height_pow2 = max(options->height >> level, 1u);
	miplevel.depth = (options->type == TEXTURE_3D) ? max(options->depth >> level, 1u) : 1u;
	if (is_compressed(options->format)) {
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

typedef struct Caps {
	bool seperable_programs;		// Seperable programs are supported
	bool indirect_draw;				// Indirect draw commands are supported
	bool fill_wire;					// Wireframe filling rasterization is supported
	bool multithreaded_creation;	// Multithreaded resource creation is supported

	bool geometry_shaders;			// Geometry shaders are supported
	bool tesselation;				// Tesselation is supported
	bool compute_shaders;			// Compute shaders are supported
	bool bindless_resources;		// Bindless resources are supported

	bool depth_clamp;				// Depth clamping is supported
	bool depth_bias_clamp;			// Depth bias clamping is supported
	bool independent_blend;			// Blend per render target is supported
	bool dual_source_blend;			// Dual-source blending is supported
	bool multiple_viewports;		// Multiple viewports are supported

	bool pixel_type_suported[PIXEL_FMT_MAX];// Which pixel formats are supported
	bool vertex_UAV_writes;			// UAVs writes to vertex, tesselation and geometry shaders are supported
	bool pixel_UAV_writes;			// UAVs writes to pixel shader are supported
	bool texture_UAV_formats;		// UAV texture formats are available in shaders

	bool border_sampling;			// Border sampling is supported
	bool anisotropic_filtering;		// Anisotropic filtering is supported
	bool LOD_bias;					// Mip load bias is supported

	bool texture_MS;				// Multisampling for 2d textures is supported
	bool texture_array_MS;			// Multisampling for 2d texture arrays is supported
	bool texture_views;				// Texture views are supported
	bool cube_array;				// Cubemap arrays are supported

	bool occlusion_queries;			// Occlusion queries are supported
	bool binary_occlusion_queries;	// Binary occlusion queries are supported
	bool timestamp_queries;			// Timestamp queries are supported
	bool pipelinestatiscics_queries;// Pipeline queries are supported

	uint32_t max_1d_dims;			// Maximum width for 1d textures
	uint32_t max_2d_dims;			// Maximum width or height for 2d textures
	uint32_t max_3d_dims;			// Maximum width height or depth for 3d textures
	uint32_t max_cube_dims;			// Maximum width or height for cubemaps

	uint32_t max_1d_array_slices;	// Maximum array elements for 1d texture arrays or 0 if not supported
	uint32_t max_2d_array_slices;	// Maximum array elements for 2d texture arrays or 0 if not supported
	uint32_t max_cube_array_slices;	// Maximum array elements for cubemap arrays or 0 if not supported

	int32_t major_version;
	int32_t minor_version;
} Caps;

void gfx_init(int width, int height);
void gfx_close();