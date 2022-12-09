#include "tc.h"

tc_strpool_t pool;

tc_allocator_i* a;

static void* producer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int wid = tc_os->cpu_id();
	tc_put_t data = { c, wid };
	await(tc_chan_put(&data));
	return 0;
}

static void* consumer(void* args) {
	tc_channel_t* c = (tc_channel_t*)args;
	int data = await(tc_chan_get(c));
	TRACE(LOG_INFO, "%i, %i", tc_os->cpu_id(), data);
	return 0;
}

static void* main_fiber(void* args) {
	jobdecl_t* jobs = tc_scratch_alloc(128 * sizeof(jobdecl_t));

	int array[64];
	for (int i = 0; i < 64; i++)
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
	tc_fut_t* c = tc_run_jobs(jobs, 128, NULL);

	//tc_channel->destroy(channel);

	//tc_buffers_i* buf = tc_buffers->create(a);

	//tc_stream_i* stream = tc_stream->open_pipe(a, buf, (fd_t) { 0 });

	//uint32_t b = await(stream->read(stream, 100));


	tc_fut_wait_and_free(c, 0);

	return 0;
}

int main(void) {
	a = tc_buddy_new(tc_memory->vm, GLOBAL_BUFFER_SIZE, 64);

	tc_init_registry(a);
	tc_fiber_pool_init(a, 256);
	assets_init(a);


	jobdecl_t main_job = { main_fiber, a };
	tc_fut_t* c = tc_run_jobs(&main_job, 1, NULL);
	tc_fut_wait_and_free(c, 0);
	
	assets_close(a);
	tc_close_registry(a);
	tc_fiber_pool_destroy(a);
	return 0;
}
