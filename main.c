#include "tc.h"

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

static void* main_fiber(void* args) {
	jobdecl_t* jobs = tc_scratch_alloc(256 * sizeof(jobdecl_t));

	int64_t array[128];
	for (int i = 0; i < 128; i++)
		array[i] = i;

	tc_channel_t* channel = tc_chan_new(a, 4);

	for (int i = 0; i < 64; i++) {
		jobs[i].func = producer;
		jobs[i].data = channel;
	}
	for (int i = 64; i < 128; i++) {
		jobs[i].func = consumer;
		jobs[i].data = channel;
	}
	//for (int i = 128; i < 256; i++) {
	//	jobs[i].func = test;
	//	jobs[i].data = &array[i];
	//}
	tc_fut_t* c = tc_run_jobs(jobs, 128, NULL);

	//tc_channel->destroy(channel);

	//tc_buffers_i* buf = tc_buffers->create(a);

	//tc_stream_i* stream = tc_stream->open_pipe(a, buf, (fd_t) { 0 });

	//uint32_t b = await(stream->read(stream, 100));


	tc_fut_wait_and_free(c, 0);

	TRACE(LOG_INFO, "done");

	return 0;
}

int main(void) {

	a = tc_buddy_new(tc_mem->vm, GLOBAL_BUFFER_SIZE, 64);

	tc_init_registry();
	tc_fiber_pool_init(a, 1024);

	int width = 640;
	int height = 480;
	
	renderer_t renderer;
	queue_t present_queue;
	swapchain_t swapchain;

	rendererdesc_t desc = { 0 };
	renderer_init("TC", &desc, &renderer);

	queuedesc_t qdesc = {
		.type = QUEUE_TYPE_GRAPHICS,
		.flag = QUEUE_FLAG_INIT_MICROPROFILE
	};
	add_queue(&renderer, &qdesc, &present_queue);
	
	tc_window_t window = tc_os->create_window(width, height, "TC");

	swapchaindesc_t scdesc = { 
		.window = window,
		.presentqueuecount = 1,
		.presentqueues = &present_queue,
		.width = width,
		.height = height,
		.imagecount = 3,
		.colorformat = recommended_swapchain_fmt(true, true),
		.colorclearval = { {1, 1, 1, 1} },
		.vsync = true,
	};
	add_swapchain(&renderer, &scdesc, &swapchain);
	
	jobdecl_t main_job = { main_fiber, a };
	tc_fut_t* c = tc_run_jobs(&main_job, 1, NULL);
	tc_fut_wait_and_free(c, 0);

	TRACE(LOG_INFO, "Created");

	remove_swapchain(&renderer, &swapchain);
	
	renderer_exit(&renderer);
	
	tc_close_registry();
	tc_fiber_pool_destroy(a);

	return 0;
}
