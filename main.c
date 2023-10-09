#include "tc.h"
#include <rmem.h>

tc_allocator_i* a;

static void* producer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int wid = tc_os->cpu_id();
	tc_put_t data = { c, wid };
	TRACE(LOG_INFO, "%i, produced", tc_os->cpu_id());
	await(tc_chan_put(&data));
	return 0;
}

static void* consumer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int data = await(tc_chan_get(c));
	TRACE(LOG_INFO, "%i, %i consumed", tc_os->cpu_id(), data);
	return 0;
}

static void* test2(void* args) {
	int64_t c = (int64_t)args;
	TRACE(LOG_INFO, "%i consumed %i", tc_os->cpu_id(), c);
	return 0;
}

static void* test(void* args) {
	int64_t* b = (int64_t*)args;
	jobdecl_t jobs[64];
	for (int i = 0; i < 64; i++) {
		jobs[i].func = test2;
		jobs[i].data = *b + i;
	}
	tc_fut_t* c = tc_run_jobs(jobs, 64, NULL);
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
	
	window = tc_os->create_window(width, height, "TC");

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
	
	const char* vert_file = "C:\\Users\\denha\\Desktop\\TCEngine\\shaders\\Binary\\base.vert.spv";

	stat_t stat = { 0 };
	//await(tc_os->stat(&stat, vert_file));

	//TRACE(LOG_INFO, "%i", (int)stat.size);

	//int64_t file = await(tc_os->open(vert_file, FILE_READ));

	//char tmp[1024] = { 0 };

	//await(tc_os->read((fd_t){ .handle=file }, tmp, 1024, 0));

	//TRACE(LOG_INFO, "%s", tmp);
	
	tc_os->destroy_window(window);
	//remove_swapchain(&renderer, &swapchain);

	return 0;
}

int main(void) {

#ifdef ENABLE_MTUNER
	rmemInit(0);
#endif

	a = tc_buddy_new(tc_mem->vm, GLOBAL_BUFFER_SIZE, 64);

	tc_init_registry();
	tc_fiber_pool_init(a, 256);
	tc_renderer_init("TCEngine", &(rendererdesc_t){
		0
	}, &renderer);

	main_fiber(a);
	
	//jobdecl_t main_job = { main_fiber, a };
	//tc_fut_t* c = tc_run_jobs(&main_job, 1, NULL);
	//tc_fut_wait_and_free(c, 0);

	tc_renderer_exit(&renderer);
	tc_close_registry();
	tc_fiber_pool_destroy(a);

	return 0;
}
