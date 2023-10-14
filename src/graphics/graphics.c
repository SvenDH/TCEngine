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
acquire_next_image_func acquire_next_image;
queue_submit_func queue_submit;
queue_present_func queue_present;
queue_wait_idle_func queue_wait_idle;
get_fence_status_func get_fence_status;
wait_for_fences_func wait_for_fences;
toggle_vsync_func toggle_vsync;
recommended_swapchain_fmt_func recommended_swapchain_fmt;

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
reset_cmdpool_func reset_cmdpool;
add_cmds_func add_cmds;
remove_cmds_func remove_cmds;

cmd_begin_func cmd_begin;
cmd_end_func cmd_end;
cmd_bindrendertargets_func cmd_bindrendertargets;
cmd_setshadingrate_func cmd_setshadingrate;
cmd_setviewport_func cmd_setviewport;
cmd_setscissor_func cmd_setscissor;
cmd_setstencilreferenceval_func cmd_setstencilreferenceval;
cmd_bindpipeline_func cmd_bindpipeline;
cmd_binddescset_func cmd_binddescset;
cmd_binddescsetwithrootcbvs_func cmd_binddescsetwithrootcbvs;
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
add_buffer_func add_buffer;
remove_buffer_func remove_buffer;
map_buffer_func map_buffer;
unmap_buffer_func unmap_buffer;
cmd_updatebuffer_func cmd_updatebuffer;
cmd_updatesubresource_func cmd_updatesubresource;
cmd_copysubresource_func cmd_copysubresource;
add_texture_func add_texture;
remove_texture_func remove_texture;
add_virtualtexture_func add_virtualtexture;
remove_virtualtexture_func remove_virtualtexture;

renderertype_t selected_api = RENDERER_VULKAN;

#if defined(VULKAN)
extern void init_vulkanrenderer(const char* appname, const rendererdesc_t* desc, renderer_t* renderer);
//extern void init_vulkanraytracingfuncs();
extern void exit_vulkanrenderer(renderer_t* renderer);
#endif

static void init_rendererapi(const char* app_name, const rendererdesc_t* desc, renderer_t* renderer, const renderertype_t api)
{
	switch (api) {
#if defined(VULKAN)
	case RENDERER_VULKAN:
		//init_vulkanraytracingfuncs();
		init_vulkanrenderer(app_name, desc, renderer);
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

void tc_renderer_init(const char* app_name, const rendererdesc_t* desc, renderer_t* renderer)
{
	TC_ASSERT(renderer && desc);
	init_rendererapi(app_name, desc, renderer, selected_api);
	//if (desc->extendedsettings && *renderer)
	//	setextendedsettings(desc->extendedsettings, (*renderer)->activegpusettings);
}

void tc_renderer_exit(renderer_t* renderer)
{
	TC_ASSERT(renderer);
	exit_rendererapi(renderer, selected_api);
}

uint32_t descindexfromname(const rootsignature_t* signature, const char* name)
{
	for (uint32_t i = 0; i < signature->descriptorcount; i++)
		if (!strcmp(name, signature->descriptors[i].name))
			return i;
	return UINT32_MAX;
}

static bool shaderres_cmp(shaderresource_t* a, shaderresource_t* b)
{
	bool same = true;
	same = same && (a->type == b->type);
	same = same && (a->set == b->set);
	same = same && (a->reg == b->reg);
	same = same && (a->name_len == b->name_len);
	if (same == false) return same;
	same = (strcmp(a->name, b->name) == 0);
	return same;
}

static bool shadervar_cmp(shadervar_t* a, shadervar_t* b)
{
	bool same = true;
	same = same && (a->offset == b->offset);
	same = same && (a->size == b->size);
	same = same && (a->name_len == b->name_len);
	if (same == false) return same;
	same = (strcmp(a->name, b->name) == 0);
	return same;
}

void destroy_shaderreflection(shaderreflection_t* reflection)
{
	if (reflection == NULL) return;
	tc_free(reflection->namepool);
	tc_free(reflection->vertexinputs);
	tc_free(reflection->shaderresources);
	tc_free(reflection->variables);
}

void create_pipelinereflection(shaderreflection_t* reflection, uint32_t numstages, pipelinereflection_t* out)
{
	TC_ASSERT(reflection && numstages && out);
	// Sanity check to make sure we don't have repeated stages.
	shaderstage_t stages = 0;
	for (uint32_t i = 0; i < numstages; i++) {
		if ((stages & reflection[i].stage) != 0) {
			TRACE(LOG_ERROR, "Duplicate shader stage was detected in shader reflection array.");
			return;
		}
		stages = (shaderstage_t)(stages | reflection[i].stage);
	}
	// Combine all shaders
	// this will have a large amount of looping
	// 1. count number of resources
	uint32_t vertstage = ~0u;
	uint32_t hullstage = ~0u;
	uint32_t domainstage = ~0u;
	uint32_t geomstage = ~0u;
	uint32_t pixstage = ~0u;
	shaderresource_t* resources = NULL;
	shadervar_t* vars = NULL;
	uint32_t numresources = 0;
	uint32_t numvars = 0;
	shaderresource_t* unique_resources[512];
	shaderstage_t shader_usage[512];
	shadervar_t* uniqueVariable[512];
	shaderresource_t* uniqueVariableParent[512];
	for (uint32_t i = 0; i < numstages; i++) {
		shaderreflection_t* ref = reflection + i;
		out->reflections[i] = *ref;
		if (ref->stage == SHADER_STAGE_VERT) vertstage = i;
		else if (ref->stage == SHADER_STAGE_HULL) hullstage = i;
		else if (ref->stage == SHADER_STAGE_DOMN) domainstage = i;
		else if (ref->stage == SHADER_STAGE_GEOM) geomstage = i;
		else if (ref->stage == SHADER_STAGE_FRAG) pixstage = i;
		//Loop through all shader resources
		for (uint32_t j = 0; j < ref->resourcecount; j++) {
			bool unique = true;
			//Go through all already added shader resources to see if this shader
			// resource was already added from a different shader stage. If we find a
			// duplicate shader resource, we add the shader stage to the shader stage
			// mask of that resource instead.
			for (uint32_t k = 0; k < numresources; k++) {
				unique = !shaderres_cmp(&ref->shaderresources[j], unique_resources[k]);
				if (unique == false) {
					shader_usage[k] |= ref->shaderresources[j].used_stages;
					break;
				}
			}
			//If it's unique, we add it to the list of shader resourceas
			if (unique == true) {
				shader_usage[numresources] = ref->shaderresources[j].used_stages;
				unique_resources[numresources] = &ref->shaderresources[j];
				numresources++;
			}
		}
		//Loop through all shader variables (constant/uniform buffer members)
		for (uint32_t j = 0; j < ref->varcount; j++) {
			bool unique = true;
			//Go through all already added shader variables to see if this shader
			// variable was already added from a different shader stage. If we find a
			// duplicate shader variables, we don't add it.
			for (uint32_t k = 0; k < numvars; k++) {
				unique = !shadervar_cmp(&ref->variables[j], uniqueVariable[k]);
				if (unique == false) break;
			}
			//If it's unique we add it to the list of shader variables
			if (unique) {
				uniqueVariableParent[numvars] = &ref->shaderresources[ref->variables[j].parent_index];
				uniqueVariable[numvars] = &ref->variables[j];
				numvars++;
			}
		}
	}
	//Copy over the shader resources in a dynamic array of the correct size
	if (numresources) {
		resources = (shaderresource_t*)tc_calloc(numresources, sizeof(shaderresource_t));
		for (uint32_t i = 0; i < numresources; i++) {
			resources[i] = *unique_resources[i];
			resources[i].used_stages = shader_usage[i];
		}
	}
	//Copy over the shader variables in a dynamic array of the correct size
	if (numvars) {
		vars = (shadervar_t*)tc_malloc(sizeof(shadervar_t) * numvars);
		for (uint32_t i = 0; i < numvars; i++) {
			vars[i] = *uniqueVariable[i];
			shaderresource_t* parentResource = uniqueVariableParent[i];
			// look for parent
			for (uint32_t j = 0; j < numresources; j++)
				if (shaderres_cmp(&resources[j], parentResource)) {
					vars[i].parent_index = j;
					break;
				}
		}
	}
	// all refection structs should be built now
	out->shaderstages = stages;
	out->reflectioncount = numstages;
	out->vertexidx = vertstage;
	out->hullidx = hullstage;
	out->domainidx = domainstage;
	out->geometryidx = geomstage;
	out->pixelidx = pixstage;
	out->resources = resources;
	out->resourcecount = numresources;
	out->variables = vars;
	out->varcount = numvars;
}

void destroy_pipelinereflection(pipelinereflection_t* reflection)
{
	if (reflection == NULL) return;
	for (uint32_t i = 0; i < reflection->reflectioncount; i++)
		destroy_shaderreflection(&reflection->reflections[i]);

	tc_free(reflection->resources);
	tc_free(reflection->variables);
}