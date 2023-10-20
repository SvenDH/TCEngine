#include "tc.h"
#include <rmem.h>
#include <tinyimageformat_query.h>

tc_allocator_i* a;

static void* producer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int wid = os_cpu_id();
	tc_put_t data = { c, wid };
	TRACE(LOG_INFO, "%i, produced", os_cpu_id());
	await(tc_chan_put(&data));
	return 0;
}

static void* consumer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int data = await(tc_chan_get(c));
	TRACE(LOG_INFO, "%i, %i consumed", os_cpu_id(), data);
	return 0;
}

static void* test2(void* args) {
	int64_t c = (int64_t)args;
	TRACE(LOG_INFO, "%i consumed %i", os_cpu_id(), c);
	return 0;
}

static void* test(void* args) {
	int64_t* b = (int64_t*)args;
	jobdecl_t jobs[64];
	for (int i = 0; i < 64; i++) {
		jobs[i].func = test2;
		jobs[i].data = *b + i;
	}
	fut_t* c = tc_run_jobs(jobs, 64, NULL);
	tc_fut_wait_and_free(c, 0);
	return 0;
}

tc_window_t window;
swapchain_t swapchain;
renderer_t renderer;
queue_t present_queue;

fence_t transtionfence;
semaphore_t imageaquiresemaphore;
fence_t rendercompletefences[3];
semaphore_t rendercompletesemaphores[3];

cmdpool_t cmdpools[3];
cmd_t cmds[3];
	
int width = 640;
int height = 480;

static void* main_fiber(void* args) {
	add_queue(&renderer, &(queuedesc_t) {
		.type = QUEUE_TYPE_GRAPHICS,
		.flag = QUEUE_FLAG_INIT_MICROPROFILE
	}, &present_queue);
	
	window = os_create_window(width, height, "TC");

	add_swapchain(&renderer, &(swapchaindesc_t){ 
		.window = window,
		.presentqueuecount = 1,
		.presentqueues = &(queue_t*[]){ &present_queue },
		.width = width,
		.height = height,
		.imagecount = 3,
		.colorformat = recommended_swapchain_fmt(true, true),
		.colorclearval = { {1, 1, 1, 1} },
		.vsync = true,
	}, &swapchain);

	for (uint32_t i = 0; i < 3; i++) {
		add_cmdpool(&renderer, &(cmdpooldesc_t){ .queue = &present_queue }, &cmdpools[i]);
		add_cmds(&renderer, &(cmddesc_t){ .pool = &cmdpools[i] }, 1, &cmds[i]);
	}

	add_fence(&renderer, &transtionfence);
	add_semaphore(&renderer, &imageaquiresemaphore);
	for (uint32_t i = 0; i < 3; i++) {
		add_fence(&renderer, &rendercompletefences[i]);
		add_semaphore(&renderer, &rendercompletesemaphores[i]);
	}
	sampler_t sampler;
	samplerdesc_t samplerdesc = { 
		FILTER_LINEAR,
		FILTER_LINEAR,
		MIPMAP_MODE_NEAREST,
		WRAP_CLAMP,
		WRAP_CLAMP,
		WRAP_CLAMP
	};
	add_sampler(&renderer, &samplerdesc, &sampler);

	shader_t shader;
	rootsignature_t rootsignature;

	shaderloaddesc_t desc = { 0 };
	desc.stages[0] = (shaderstageloaddesc_t){ "base.vert", NULL, 0 };
	desc.stages[1] = (shaderstageloaddesc_t){ "base.frag", NULL, 0 };
	load_shader(&renderer, &desc, &shader);

	const char* staticsamplernames[] = { "uSampler" };
	rootsignaturedesc_t rootdesc = { &(shader_t*){&shader}, 1 };
	rootdesc.staticsamplercount = 1;
	rootdesc.staticsamplernames = staticsamplernames;
	rootdesc.staticsamplers = &(sampler_t*){&sampler};
	add_rootsignature(&renderer, &rootdesc, &rootsignature);

	vertexlayout_t vertex = {
		.attribcount = 2,
		.attribs[0].semantic = SEMANTIC_POSITION,
		.attribs[0].format = TinyImageFormat_R32G32_SFLOAT,
		.attribs[1].semantic = SEMANTIC_TEXCOORD0,
		.attribs[1].format = TinyImageFormat_R32G32_SFLOAT,
		.attribs[1].location = 1,
		.attribs[1].offset = TinyImageFormat_BitSizeOfBlock(TinyImageFormat_R32G32_SFLOAT) / 8
	};
	blendstatedesc_t blendstate = {
		.srcfactors[0] = BLEND_SRC_ALPHA,
		.dstfactors[0] = BLEND_INV_SRC_ALPHA,
		.srcalphafactors[0] = BLEND_SRC_ALPHA,
		.dstalphafactors[0] = BLEND_INV_SRC_ALPHA,
		.masks[0] = COLOR_MASK_ALL
	};
	depthstatedesc_t depthstate = { 0 };
	rasterizerstatedesc_t rasterstate = {
		.cullmode = CULL_MODE_NONE,
		.scissor = true
	};

	pipeline_t pipeline;
	graphicspipelinedesc_t gfxdesc = {
		.shader = &shader,
		.rootsignature = &rootsignature,
		.primitivetopo = PRIMITIVE_TOPO_TRI_STRIP,
		.depthstencilformat = TinyImageFormat_UNDEFINED,
		.rendertargetcount = 1,
		.samplecount = SAMPLE_COUNT_1,
		.blendstate = &blendstate,
		.depthstate = &depthstate,
		.rasterizerstate = &rasterstate,
		.vertexlayout = &vertex,
		.colorformats = &swapchain.rts[0].format,
	};
	pipelinedesc_t pipelinedesc = {
		.graphicsdesc = gfxdesc,
		.type = PIPELINE_TYPE_GRAPHICS
	};
	add_pipeline(&renderer, &pipelinedesc, &pipeline);
	
	remove_swapchain(&renderer, &swapchain);
	os_destroy_window(window);

	return 0;
}

int main(void) {

#ifdef ENABLE_MTUNER
	rmemInit(0);
#endif

	a = tc_buddy_new(tc_mem->vm, GLOBAL_BUFFER_SIZE, 64);

	registry_init();
	fiber_pool_init(a, 256);

	fs_set_resource_dir(&systemfs, M_CONTENT, R_SHADER_SOURCES, "..\\..\\shaders");
	fs_set_resource_dir(&systemfs, M_CONTENT, R_SHADER_BINARIES, "..\\..\\compiledshaders");

	renderer_init("TCEngine", &(rendererdesc_t){
		0
	}, &renderer);

	main_fiber(a);
	
	//jobdecl_t main_job = { main_fiber, a };
	//tc_fut_t* c = tc_run_jobs(&main_job, 1, NULL);
	//tc_fut_wait_and_free(c, 0);

	renderer_exit(&renderer);
	registry_close();
	fiber_pool_destroy(a);

	return 0;
}
