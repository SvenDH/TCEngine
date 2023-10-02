/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/
#include "private_types.h"
//#include "vkgraphics.h"

// API functions
add_fence_func add_fence;
remove_fence_func remove_fence;
add_semaphore_func add_semaphore;
remove_semaphore_func remove_semaphore;
add_queue_func add_queue;
remove_queue_func remove_queue;
add_swapchain_func add_swapchain;
remove_swapchain_func remove_swapchain;
acquire_nextimage_func acquire_nextimage;
recommendedswapchainfmt_func recommendedswapchainfmt;
queue_submit_func queue_submit;
queue_present_func queue_present;
queue_waitidle_func queue_waitidle;
get_fencestatus_func get_fencestatus;
wait_for_fences_func wait_for_fences;
toggle_vsync_func toggle_vsync;
add_rendertarget_func add_rendertarget;
remove_rendertarget_func remove_rendertarget;
add_sampler_func add_sampler;
remove_sampler_func remove_sampler;
add_shaderbinary_func add_shaderbinary;
remove_shader_func remove_shader;
add_rootsignature_func add_rootsignature;
remove_rootsignature_func remove_rootsignature;
add_pipeline_func add_pipeline;
remove_pipeline_func remove_pipeline;
add_pipelinecache_func add_pipelinecache;
get_pipelinecachedata_func get_pipelinecachedata;
remove_pipelinecache_func remove_pipelinecache;
add_descriptorset_func add_descriptorset;
remove_descriptorset_func remove_descriptorset;
update_descriptorset_func update_descriptorset;
add_cmdpool_func add_cmdpool;
remove_cmdpool_func remove_cmdpool;
add_cmds_func add_cmds;
remove_cmds_func remove_cmds;
reset_cmdpool_func reset_cmdpool;
cmd_begin_func cmd_begin;
cmd_end_func cmd_end;
cmd_bindrts_func cmd_bindrts;
cmd_setshadingrate_func cmd_setshadingrate;
cmd_setviewport_func cmd_setviewport;
cmd_setscissor_func cmd_setscissor;
cmd_setstencilreferenceval_func cmd_setstencilreferenceval;
cmd_bindpipeline_func cmd_bindpipeline;
cmd_binddescset_func cmd_binddescset;
cmd_binddescsetwithrootbbvs_func cmd_binddescsetwithrootbbvs;
cmd_bindpushconstants_func cmd_bindpushconstants;
cmd_bindindexbuffer_func cmd_bindindexbuffer;
cmd_bindvertexbuffer_func cmd_bindvertexbuffer;
cmd_draw_func cmd_draw;
cmd_drawinstanced_func cmd_drawinstanced;
cmd_drawindexed_func cmd_drawindexed;
cmd_drawindexedinstanced_func cmd_drawindexedinstanced;
cmd_dispatch_func cmd_dispatch;
cmd_resourcebarrier_func cmd_resourcebarrier;
cmd_updatevirtualtexture_func cmd_updatevirtualtexture;
add_indirectcmdsignature_func add_indirectcmdsignature;
remove_indirectcmdsignature_func remove_indirectcmdsignature;
cmd_execindirect_func cmd_execindirect;

// GPU Query Interface
get_timestampfreq_func get_timestampfreq;
add_querypool_func add_querypool;
remove_querypool_func remove_querypool;
cmd_resetquerypool_func cmd_resetquerypool;
cmd_beginquery_func cmd_beginquery;
cmd_endquery_func cmd_endquery;
cmd_resolvequery_func cmd_resolvequery;

// Stats Info Interface
get_memstats_func get_memstats;
get_memuse_func get_memuse;
free_memstats_func free_memstats;

// Debug Marker Interface
cmd_begindebugmark_func cmd_begindebugmark;
cmd_enddebugmark_func cmd_enddebugmark;
cmd_adddebugmark_func cmd_adddebugmark;
cmd_writemark_func cmd_writemark;

// Resource Debug Naming Interface
set_buffername_func set_buffername;
set_texturename_func set_texturename;
set_rendertargetname_func set_rendertargetname;
set_pipelinename_func set_pipelinename;

// Internal Resource Load Functions
typedef void (*add_buffer)(renderer_t* renderer, const bufferdesc_t* desc, buffer_t** buf);
typedef void (*remove_buffer)(renderer_t* renderer, buffer_t* buf);
typedef void (*map_buffer)(renderer_t* renderer, buffer_t* buf, range_t* range);
typedef void (*unmap_buffer)(renderer_t* renderer, buffer_t* buf);
typedef void (*cmd_updatebuffer)(cmd_t* cmd, buffer_t* buf, uint64_t dstoffset, buffer_t* srcbuf, uint64_t srcoffset, uint64_t size);
typedef void (*cmd_updatesubresource)(cmd_t* cmd, texture_t* tex, buffer_t* srcbuf, const struct subresourcedatadesc_s* desc);
typedef void (*cmd_copysubresource)(cmd_t* cmd, buffer_t* dstbuf, texture_t* tex, const struct subresourcedatadesc_s* desc);
typedef void (*add_texture)(renderer_t* renderer, const texturedesc_t* desc, texture_t** tex);
typedef void (*remove_texture)(renderer_t* renderer, texture_t* tex);
typedef void (*add_virtualtexture)(cmd_t* cmd, const texturedesc_t* desc, texture_t** tex, void* imagedata);
typedef void (*remove_virtualtexture)(renderer_t* renderer, virtualtexture_t* tex);


static renderertype_t selected_api;

#if defined(VULKAN)
extern void init_vulkanrenderer(const char* appname, const rendererdesc_t* desc, renderer_t** renderer);
extern void init_vulkanraytracingfuncs();
extern void exit_vulkanrenderer(renderer_t* renderer);
#endif

static void init_rendererapi(const char* appname, const rendererdesc_t* desc, renderer_t** renderer, const renderertype_t api)
{
	switch (api) {
#if defined(VULKAN)
	case RENDERER_VULKAN:
		init_vulkanraytracingfuncs();
		init_vulkanrenderer(appname, desc, renderer);
		break;
#endif
	default:
		TRACE(LOG_ERROR, "No Renderer API defined!");
		break;
	}
}

static void exit_rendererapi(renderer_t* renderer, const renderertype_t api)
{
	switch (api) {
#if defined(VULKAN)
	case RENDERER_VULKAN:
		exit_vulkanrenderer(renderer);
		break;
#endif
	default:
		TRACE(LOG_ERROR, "No Renderer API defined!");
		break;
	}
}

void init_renderer(const char* appname, const rendererdesc_t* desc, renderer_t** renderer)
{
	TC_ASSERT(renderer && *renderer == NULL);
	TC_ASSERT(desc);

	init_rendererapi(appname, desc, renderer, selected_api);
	//if (desc->extendedsettings && *renderer)
	//	setextendedsettings(desc->extendedsettings, (*renderer)->activegpusettings);
}

void exit_renderer(renderer_t* renderer)
{
	TC_ASSERT(renderer);
	exit_rendererapi(renderer, selected_api);
}

uint32_t descindexfromname(const rootsignature_t* signature, const char* name)
{
	for (uint32_t i = 0; i < signature->descriptorcount; ++i) {
		if (!strcmp(name, signature->descriptors[i].name)) {
			return i;
		}
	}
	return UINT32_MAX;
}