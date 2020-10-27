#include "tc.h"


static void* array_crunch(void* arg) {
	int* a = (int*)arg;
	*a = 42;
	//printf("hello fiber sub %p\n", a);
	return 0;
}

static void* fiber_worker(void* arg) {
	int data = *(int*)arg;
	
	int* arr = (int*)cache_alloc(fiber_cache(), sizeof(int) * data);

	job_t* jobs = (job_t*)cache_alloc(fiber_cache(), sizeof(job_t) * data);
	for (int i = 0; i < data; i++) {
		jobs[i].func = array_crunch;
		jobs[i].data = &arr[i];
	}

	counter_id counter = jobs_run(jobs, data);
	counter_wait(counter, 0);
	counter_free(counter);

	cache_free(fiber_cache(), jobs);

	for (int i = 0; i < data; i++) {
		TC_ASSERT(arr[i] == 42);
	}

	cache_free(fiber_cache(), arr);

	printf("hello fiber %i\n", data);
	
	return 0;
}

static void* main_fiber(void* args) {
	(void)args;

	int data[] = { 69, 420, 133, 50, 111, 1200, 128, 1337 };

	job_t jobs[] = {
		{ fiber_worker, &data[0] },
		{ fiber_worker, &data[1] },
		{ fiber_worker, &data[2] },
		{ fiber_worker, &data[3] },
		{ fiber_worker, &data[4] },
		{ fiber_worker, &data[5] },
		{ fiber_worker, &data[6] },
		{ fiber_worker, &data[7] },
	};

	counter_id counter = jobs_run(jobs, 8);
	counter_wait(counter, 0);
	counter_free(counter);
	
	return 0;
}

int main(void) {
	memory_init();
	fiber_init();

	job_t main_job = { main_fiber, NULL };
	counter_id c = jobs_run(&main_job, 1);
	counter_wait(c, 0);

	fiber_close();
	memory_free();

	return 0;
}
