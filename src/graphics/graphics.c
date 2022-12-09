/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/
#include "private_types.h"
#include "graphics.h"
#include "vkgraphics.h"

/* Constants and macros: */

#ifndef MAX_TRANSFORMSTACK
#define MAX_TRANSFORMSTACK 32
#endif

// Number of buffers inside a stream index for async index editing
#ifndef MAX_VERTICES
#define MAX_VERTICES 65535
#endif

/* Type declaration: */

typedef struct graphicsstate_s {
    mat3* currentmat;                   // Current matrix being transformed
    mat3 modelview;                     // Transformation in model space
    mat3 project;                       // Transformation to view space
    mat3 transform;
    mat3 stack[MAX_TRANSFORMSTACK];     // Transform stack (top is current matirx)
    int stackcount;                     // Top of transform stack

    tc_rid_t defaulttex;                // Default texture object
	tc_rid_t defaultshader;             // Default shader program object
	tc_rid_t currentshader;             // Currently used render program
    ivec2 rendersize;                   // Size of current FBO

    struct {
        int drawcalls;                  // Number of draw calls in this frame
        int shaderswitches;             // Number of shader switches in this frame
    } stats;
	void* backend_renderer;

	tc_allocator_i* allocator;
} graphicsstate_t;


/* Forward function declarations: */

/* Global state: */
static graphicsstate_t state = { 0 };


/* Function definitions */

void gfx_init(tc_allocator_i* a) {
	uint32_t count = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&count);
	gfxfeatures_t features = {
		.seperable_programs = 1,
		.indirect_draw = 1,
		.fill_wireframe = 1,
		.multithreaded_creation = 1,
		.geometry_shaders = 1,
		.tessellation = 1,
		.mesh_shaders = 2,
		.raytracing = 2,
		.compute_shaders = 1,
		.bindless_resources = 1,
		.depth_clamp = 1,
		.depth_bias_clamp = 1,
		.independent_blend = 1,
		.dual_source_blend = 1,
		.multi_viewport = 1,
		.texture_compression_BC = 1,
		.vertex_UAV_writes = 1,
		.pixel_UAV_writes = 1,
		.texture_UAV_formats = 1,
		.shader_float16 = 2,
		.resource_buffer8 = 2,
		.resource_buffer16 = 2,
		.uniform_buffer8 = 2,
		.uniform_buffer16 = 2,
		.shader_int8 = 2,
		.shader_io16 = 2,
		.texture_MS = 1,
		.texture_array_MS = 1,
		.texture_views = 1,
		.cube_array = 1,
		.occlusion_queries = 1,
		.binary_occlusion_queries = 1,
		.timestamp_queries = 1,
		.pipeline_statistics_queries = 1,
	};
	state.backend_renderer = vk_init(&(vk_params) {
		.allocator = a,
		.features = features,
		.extensions = extensions, 
		.num_extensions = count, 
		.adapter_id = 0,
		.enable_validation = true,
		.main_pool_size = { 8192, 1024, 8192, 8192, 1024, 4096, 4096, 1024, 1024, 256, 256 },
		.dynamic_pool_size = { 2048, 256, 2048, 2048, 256, 1024, 1024, 256, 256, 64, 64 },
		.device_page = 16 << 20,
		.host_page = 16 << 20,
		.device_reserve = 256 << 20,
		.host_reserve = 256 << 20,
		.upload_size = 1 << 20,
		.dynamic_heap_size = 8 << 20,
		.dynamic_page_size = 256 << 10,
	});
}

void gfx_close(void) {
    vk_close(state.backend_renderer);
}

void gfx_ortho(double left, double right, double bottom, double top) { 
    mat_ortho(*state.currentmat, left, right, bottom, top); 
}

void pipeline_init(pipeline_t* pipe, pipeline_params caps) {
	pipe->caps = caps;

	shadervar_t* variable_b = NULL;
	staticsampler_t* sampler_b = NULL;
	attribute_t* attribute_b = NULL;
	// Check and initialize uniforms, textures samplers
	uniform_params uniforms = caps.uniforms;
	if (uniforms.variables != NULL) {
		for (uint32_t i = 0; i < uniforms.num_variables; i++) {
			TC_ASSERT(uniforms.variables[i].name != 0, "[Pipeline]: Variable name cannot be null");
			buff_push(variable_b, state.allocator, uniforms.variables[i]);
		}
	}
	if (uniforms.samplers != NULL) {
		for (uint32_t i = 0; i < uniforms.num_samplers; i++) {
			TC_ASSERT(uniforms.samplers[i].name != 0, "[Pipeline]: Sampler or texture name cannot be null");
			buff_push(sampler_b, state.allocator, uniforms.samplers[i]);
		}
	}
	// Check and initialize shaders and vertices
	if (caps.is_compute) {
		TC_ASSERT(caps.compute_shader != 0, "[Pipeline]: Compute pipeline should have compute shader");
		TC_ASSERT(caps.attributes.num_attributes == 0, "[Pipeline]: Compute pipeline cannot have attributes");
		pipe->shaders[pipe->num_shaders++] = caps.compute_shader;
	}
	else {
		if (caps.vertex_shader) pipe->shaders[pipe->num_shaders++] = caps.vertex_shader;
		if (caps.pixel_shader) pipe->shaders[pipe->num_shaders++] = caps.pixel_shader;
		if (caps.geometry_shader) pipe->shaders[pipe->num_shaders++] = caps.geometry_shader;
		if (caps.domain_shader) pipe->shaders[pipe->num_shaders++] = caps.domain_shader;
		if (caps.hull_shader) pipe->shaders[pipe->num_shaders++] = caps.hull_shader;
		if (pipe->num_shaders == 0) TRACE(LOG_ERROR, "[Pipeline]: There must be at least 1 shader in the pipeline");

		uint32_t autostrides[MAX_BUFFERS] = { 0 };
		for (uint32_t i = 0; i < MAX_BUFFERS; i++) pipe->strides[i] = 0xFFFFFFFF;
		attribute_params attributes = caps.attributes;
		for (uint32_t i = 0; i < attributes.num_attributes; i++) {
			attribute_t attr = attributes.attributes[i];
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
			autostrides[buffslot] = max(autostrides[buffslot], attr.offset + attr.components * datatype_size(attr.type));
		}
		for (uint32_t i = 0; i < attributes.num_attributes; i++) {
			attribute_t attr = attributes.attributes[i];
			uint32_t buffslot = attr.buffer_index;
			if (pipe->strides[buffslot] == 0xFFFFFFFF)
				pipe->strides[buffslot] = autostrides[buffslot];
			else if (pipe->strides[buffslot] < autostrides[buffslot])
				TRACE(LOG_ERROR, "[Pipeline]: Specified stride %i for buffer %i is too small for all elements", pipe->strides[buffslot], buffslot);

			if (attr.stride == 0xFFFFFFFF) attr.stride = pipe->strides[buffslot];
			buff_push(attribute_b, state.allocator, attr);
		}
		for (uint32_t i = 0; i < pipe->num_buffer_slots; i++)
			if (pipe->strides[i] == 0xFFFFFFFF)
				pipe->strides[i] = 0;
	}
	pipe->caps.attributes.attributes = attribute_b;
	pipe->caps.uniforms.variables = variable_b;
	pipe->caps.uniforms.samplers = sampler_b;
}

static void correct_buffer_view_params(bufferview_params* caps, buffer_params* buffer_options) {
	if (caps->size == 0) {
		TC_ASSERT(buffer_options->size > caps->offset, "[GPUBuffer]: Buffer offset exceeds buffer size");
		caps->size = buffer_options->size - caps->offset;
	}
	if (caps->offset + caps->size > buffer_options->size)
		TC_ERROR("[GPUBuffer]: Buffer view range [%i, %i) is out of the buffer boundaries [0, %i).", caps->offset, caps->offset + caps->size, buffer_options->size);
	if ((buffer_options->bindflags & BIND_UNORDERED_ACCESS) || (buffer_options->bindflags & BIND_SHADER_VARIABLE)) {
		if (buffer_options->mode == BUFFER_STRUCTURED || buffer_options->mode == BUFFER_FORMATTED) {
			TC_ASSERT(buffer_options->stride != 0, "[GPUBuffer]: Element byte stride is zero");
			if ((caps->offset % buffer_options->stride) != 0)
				TC_ERROR("[GPUBuffer]: Buffer view byte offset (%i) is not multiple of element byte stride (%i).", caps->offset, buffer_options->stride);
			if ((caps->size % buffer_options->stride) != 0)
				TC_ERROR("[GPUBuffer]: Buffer view byte width (%i) is not multiple of element byte stride (%i).", caps->size, buffer_options->stride);
		}
		if (buffer_options->mode == BUFFER_FORMATTED && caps->datatype == DATA_UNDEFINED)
			TC_ERROR("[GPUBuffer]: Format must be specified when creating a view of a formatted buffer");
		if (buffer_options->mode == BUFFER_FORMATTED || (buffer_options->mode == BUFFER_RAW && caps->datatype != DATA_UNDEFINED)) {
			if (caps->components <= 0 || caps->components > 4)
				TC_ERROR("[GPUBuffer]: Incorrect number of components (%i). 1, 2, 3, or 4 are allowed values", caps->components);
			if (caps->datatype == DATA_FLOAT32 || caps->datatype == DATA_FLOAT16)
				caps->normalized = true;
			size_t stride = data_type_size(caps->datatype) * caps->components;
			if (buffer_options->mode == BUFFER_RAW && buffer_options->stride == 0)
				TC_ERROR("[GPUBuffer]: To enable formatted views of a raw buffer, element byte must be specified during buffer initialization");
			if (stride != buffer_options->stride)
				TC_ERROR("[GPUBuffer]: Buffer element byte stride (%i) is not consistent with the size (%i) defined by the format of the view", buffer_options->stride, stride);
		}
		if (buffer_options->mode == BUFFER_RAW && caps->datatype == DATA_UNDEFINED && (caps->offset % 16) != 0)
			TC_ERROR("[GPUBuffer]: When creating a RAW view, the offset of the first element from the start of the buffer (%i) must be a multiple of 16 bytes", caps->offset);
	}
}

static void correct_texture_params(texture_params* caps) {
	if (caps->type == TEXTURE_UNKNOWN)
		TC_ERROR("[Texture]: Texture type is undefined");
	if (!(caps->type >= TEXTURE_1D && caps->type <= TEXTURE_CUBE_ARRAY))
		TC_ERROR("[Texture]: Unexpected texture type");
	if (caps->width == 0)
		TC_ERROR("[Texture]: Texture width cannot be zero");
	if (caps->type == TEXTURE_1D || caps->type == TEXTURE_1D_ARRAY)
		if (caps->height != 1)
			TC_ERROR("[Texture]: Height (%i) of texture 1D/1D array must be 1", caps->height);

		else if (caps->height == 0)
			TC_ERROR("[Texture]: Texture height cannot be zero");
	if (caps->type == TEXTURE_3D && caps->depth == 0)
		TC_ERROR("[Texture]: 3D texture depth cannot be zero");
	if ((caps->type == TEXTURE_1D || caps->type == TEXTURE_2D) && caps->depth != 1)
		TC_ERROR("[Texture]: Texture 1D/2D must have one array slice (%i provided). Use Texture 1D/2D array if you need more than one slice.", caps->depth);
	if (caps->type == TEXTURE_CUBE || caps->type == TEXTURE_CUBE_ARRAY) {
		if (caps->width != caps->height)
			TC_ERROR("[Texture]: For cube map textures, texture width (%i provided) must match texture height (%i provided)", caps->width, caps->height);
		if (caps->depth < 6)
			TC_ERROR("[Texture]: Texture cube/cube array must have at least 6 slices (%i provided).", caps->depth);
	}
	uint32_t maxdim = 0;
	if (caps->type == TEXTURE_1D || caps->type == TEXTURE_1D_ARRAY)
		maxdim = caps->width;
	else if (caps->type == TEXTURE_2D || caps->type == TEXTURE_2D_ARRAY || caps->type == TEXTURE_CUBE)
		maxdim = max(caps->width, caps->height);
	else if (caps->type == TEXTURE_3D)
		maxdim = max(max(caps->width, caps->height), caps->depth);
	TC_ASSERT(maxdim >= (1U << (caps->miplevels - 1)), "[Texture]: Incorrect number of mip levels (%i)", caps->miplevels);
	if (caps->samples > 1) {
		if (!(caps->type == TEXTURE_2D || caps->type == TEXTURE_2D_ARRAY))
			TC_ERROR("[Texture]: Only Texture 2D/Texture 2D Array can be multisampled");
		if (caps->miplevels != 1)
			TC_ERROR("[Texture]: Multisampled textures must have one mip level (%i levels specified)", caps->miplevels);
		if (caps->bindflags & BIND_UNORDERED_ACCESS)
			TC_ERROR("[Texture]: UAVs are not allowed for multisampled resources");
	}
	if (caps->usage == USAGE_STAGING) {
		if (caps->bindflags != 0)
			TC_ERROR("[Texture]: Staging textures cannot be bound to any GPU pipeline stage");
		if (caps->generate_mips)
			TC_ERROR("[Texture]: Mipmaps cannot be autogenerated for staging textures");
		if (caps->cpu_access == 0)
			TC_ERROR("[Texture]: Staging textures must specify CPU access flags");
		if ((caps->cpu_access & (CPU_READ | CPU_WRITE)) == (CPU_READ | CPU_WRITE))
			TC_ERROR("[Texture]: Staging textures must use exactly one of CPU_READ or CPU_WRITE flags");
	}
}