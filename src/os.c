/*==========================================================*/
/*								OS							*/
/*==========================================================*/
#include "private_types.h"

#include <uv.h>
#include <GLFW/glfw3.h>
#include <stb_ds.h>

void* os_map(size_t size) {
#ifdef _WIN32
	return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
#ifdef MAP_ANON
	return mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
	return mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
#endif
}

void* os_reserve(size_t size) {
#ifdef _WIN32
	return VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
#else
#if defined(MAP_ANON)
	return mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
	return mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
#endif
}

void os_commit(void* ptr, size_t size) {
#ifdef _WIN32
	VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
#endif
}

void os_unmap(void* ptr, size_t size) {
#ifdef _WIN32
	(void)size;
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	munmap(ptr, size);
#endif
}

size_t os_page_size() {
#ifdef _WIN32
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwPageSize;
#else
	return getpagesize();
#endif
}

void os_guard_page(void* ptr, size_t size) {
#ifdef _WIN32
	DWORD old_options;
	if (VirtualProtect(ptr, size, PAGE_READWRITE | PAGE_GUARD, &old_options) == 0)
		abort();
#else
	mprotect(ptr, size, PROT_NONE)
#endif
}

#ifdef _WIN32
#include <io.h>
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#endif


/*==========================================================*/
/*					SYSTEM ALLOCATOR						*/
/*==========================================================*/

enum {
	OS_MIN_ALLOC_SIZE = 128,
	OS_ALLOCATOR_SIZE = 32768
};

typedef struct os_request_s {
	tc_waitable_i;
	slab_obj_t;
	uv_fs_t req;
	fut_t* future;
	uv_buf_t buf;
	tc_allocator_i* temp;
} os_request_t;

struct context_t {
	tc_allocator_i* allocator;
	os_request_t*  pool;
	lock_t lock;
};

struct context_t* context;

uv_once_t init = UV_ONCE_INIT;


static void* mem_malloc(size_t size) {
	if (size) {
		uint32_t* s = TC_ALLOC(context->allocator, size + sizeof(uint32_t));
		*s = (uint32_t)size;
		return s + 1;
	}
	return NULL;
}

static void* mem_calloc(size_t count, size_t size) {
	return memset(mem_malloc(size), 0, size);
}

static void mem_free(void* ptr) {
	if (ptr) {
		uint32_t* s = ((uint32_t*)ptr) - 1;
		TC_FREE(context->allocator, s, (*s) + sizeof(uint32_t));
	}
}

static void* mem_realloc(void* ptr, size_t size) {
	if (!ptr) {
		return mem_malloc(size);
	}
	if (size == 0) {
		mem_free(ptr);
		return NULL;
	}
	uint32_t* s = ((uint32_t*)ptr) - 1;
	ptr = memcpy(mem_malloc(size), ptr, *s);
	TC_FREE(context->allocator, s, (*s) + sizeof(uint32_t));
	return ptr;
}

void init_context(void) {
	context = tc_malloc(sizeof(struct context_t));
	memset(context, 0, sizeof(struct context_t));
	context->allocator = tc_mem->sys;
	slab_create(&context->pool, context->allocator, CHUNK_SIZE);
	
	//uv_replace_allocator(mem_malloc, mem_realloc, mem_calloc, mem_free);

	if (!glfwInit()) TRACE(LOG_ERROR, "Failed to initialie GLFW");
}

static 
void os_cb(uv_fs_t* req) {
	os_request_t* handle = (os_request_t*)req->data;
	int64_t res = uv_fs_get_result(req);
	handle->results = res;
	if (req->fs_type == UV_FS_SCANDIR) {
		if (handle->buf.base) {
			uv_dirent_t ent;
			char* paths = NULL;
			size_t i = 0;
			while (uv_fs_scandir_next(req, &ent) != UV_EOF) {
				if (ent.name) {
					size_t len = strlen(ent.name) + 1;
					arraddnptr(paths, len);
					memcpy(paths + i, ent.name, len);
					i += len;
				}
			}
			handle->results = (int64_t)paths;
		}
	}
	else if (req->fs_type == UV_FS_STAT) {
		if (handle->buf.base) {
			uv_stat_t* stats = uv_fs_get_statbuf(req);
			stat_t* nstat = (stat_t*)handle->buf.base;
			nstat->exists = (res == 0);
			nstat->is_dir = (stats->st_mode & S_IFDIR);
			nstat->size = stats->st_size;
			nstat->last_altered = stats->st_mtim.tv_sec;
		}
	}
	else if (req->fs_type == UV_FS_OPEN) {
		if (res < 0)
			handle->results = TC_INVALID_FILE;
	}
	uv_fs_req_cleanup(&handle->req);
	tc_fut_decr(handle->future);
}

static 
void os_request_destroy(os_request_t* req) {
	TC_LOCK(&context->lock);
	slab_free(context->pool, req);
	TC_UNLOCK(&context->lock);
}

static 
os_request_t* os_request_init_ex(char* buf, uint64_t size, tc_allocator_i* temp) {
	uv_once(&init, init_context);
	TC_LOCK(&context->lock);
	os_request_t* req = slab_alloc(context->pool);
	TC_UNLOCK(&context->lock);
	req->instance = req;
	req->dtor = os_request_destroy;
	req->future = tc_fut_new(context->allocator, 1, req, 4);
	req->buf = uv_buf_init(buf, (unsigned int)size);
	req->temp = temp;
	req->req.data = req;
	return req;
}

static 
os_request_t* os_request_init() {
	return os_request_init_ex(NULL, 0, NULL);
}

fut_t* os_stat(stat_t* stat, const char* path) {
	os_request_t* req = os_request_init_ex(stat, sizeof(stat_t), NULL);
	uv_fs_stat(tc_eventloop(), &req->req, path, os_cb);
	return req->future;
}

fut_t* os_rename(const char* path, const char* new_path) {
	os_request_t* req = os_request_init();
	uv_fs_rename(tc_eventloop(), &req->req, path, new_path, os_cb);
	return req->future;
}

fut_t* os_link(const char* path, const char* new_path) {
	os_request_t* req = os_request_init();
	uv_fs_link(tc_eventloop(), &req->req, path, new_path, os_cb);
	return req->future;
}

fut_t* os_unlink(const char* path) {
	os_request_t* req = os_request_init();
	uv_fs_unlink(tc_eventloop(), &req->req, path, os_cb);
	return req->future;
}

fut_t* os_mkdir(const char* path) {
	os_request_t* req = os_request_init();
	uv_fs_mkdir(tc_eventloop(), &req->req, path, 0, os_cb);
	return req->future;
}

fut_t* os_rmdir(const char* path) {
	os_request_t* req = os_request_init();
	uv_fs_rmdir(tc_eventloop(), &req->req, path, os_cb);
	return req->future;
}

fut_t* os_scandir(const char* path, tc_allocator_i* temp) {
	os_request_t* req = os_request_init_ex(NULL, 0, temp);
	uv_fs_scandir(tc_eventloop(), &req->req, path, 0, os_cb);
	return req->future;
}

fut_t* os_open(const char* path, file_flags_t flags) {
	os_request_t* req = os_request_init();
	uv_fs_open(tc_eventloop(), &req->req, path, (int)flags, S_IRUSR | S_IWUSR, os_cb);
	return req->future;
}

fut_t* os_read(fd_t file, char* buf, uint64_t len, int64_t offset) {
	os_request_t* req = os_request_init_ex(buf, len, NULL);
	uv_fs_read(tc_eventloop(), &req->req, file, &req->buf, 1, offset, os_cb);
	return req->future;
}

fut_t* os_write(fd_t file, char* buf, uint64_t len, int64_t offset) {
	os_request_t* req = os_request_init_ex(buf, len, NULL);
	uv_fs_write(tc_eventloop(), &req->req, file, &req->buf, 1, offset, os_cb);
	return req->future;
}

fut_t* os_close(fd_t file) {
	os_request_t* req = os_request_init();
	uv_fs_close(tc_eventloop(), &req->req, file, os_cb);
	return req->future;
}

fut_t* os_sync(fd_t file) {
	os_request_t* req = os_request_init();
	uv_fs_fsync(tc_eventloop(), &req->req, file, os_cb);
	return req->future;
}

fut_t* os_copyfile(const char* path, const char* new_path) {
	os_request_t* req = os_request_init();
	uv_fs_copyfile(tc_eventloop(), &req->req, path, new_path, 0, os_cb);
	return req->future;
}

const char* os_tmpdir(tc_allocator_i* temp) {
	char tempbuf[1024];
	size_t len = 1024;
	uv_os_tmpdir(tempbuf, &len);
	char* buf = TC_ALLOC(temp, (len + 1));
	memcpy(buf, tempbuf, len);
	buf[len] = '\0';
	return buf;
}

const char* os_getcwd(tc_allocator_i* temp) {
	char tempbuf[1024];
	size_t len = 1024;
	uv_cwd(tempbuf, &len);
	char* buf = TC_ALLOC(temp, (len + 1));
	memcpy(buf, tempbuf, len);
	buf[len] = '\0';
	return buf;
}

bool os_chdir(const char* path) {
	return uv_chdir(path) == 0;
}

tc_thread_t os_create_thread(tc_thread_f entry, void* data, uint32_t stack_size) {
	uv_thread_t tid;
	uv_thread_options_t opts = {
		.flags = UV_THREAD_HAS_STACK_SIZE,
		.stack_size = stack_size,
	};
	uv_thread_create_ex(&tid, &opts, entry, data);
	return (tc_thread_t)tid;
}

uint32_t os_cpu_id() {
#ifdef _WIN32
	return GetCurrentProcessorNumber();
#else
	unsigned int cpuid;
	getcpu(&cpuid, NULL);
	return cpuid;
#endif
}

uint32_t os_num_cpus() {
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#else
#if IS_APPLE
#include <sys/sysctl.h>
	int mib[4];
	int num;
	size_t len = sizeof(num);
	mib[0] = CTL_HW;
	mib[1] = HW_AVAILCPU;
	sysctl(mib, 2, &num, &len, NULL, 0);
	if (num < 1) {
		mib[1] = HW_NCPU;
		sysctl(mib, 2, &num, &len, NULL, 0);
		if (num < 1)
			num = 1;
	}
	return num;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
#endif
}

void os_set_thread_affinity(tc_thread_t thread, uint32_t cpu_num) {
#ifdef _WIN32
	//DWORD_PTR prev_mask = 
	SetThreadAffinityMask((HANDLE)thread, (DWORD_PTR)1UL << (cpu_num));
#else
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_num, &cpuset);
	int err = pthread_setaffinity_np(thread, sizeof(cpu_set_t), cpuset);
#endif
}

tc_thread_t os_current_thread() {
#ifdef _WIN32
	HANDLE h = NULL;
	if (!DuplicateHandle(
		GetCurrentProcess(), 
		GetCurrentThread(), 
		GetCurrentProcess(), 
		&h, 0, FALSE, DUPLICATE_SAME_ACCESS))
		abort();
	return (tc_thread_t)h;
#else
	return (tc_thread_t)pthread_self();
#endif
}

tc_window_t os_create_window(int width, int height, const char* title)
{
	uv_once(&init, init_context);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
	TC_ASSERT(window);
	return window;
}

void os_destroy_window(tc_window_t window)
{
	glfwDestroyWindow(window);
}

typedef struct {
	tc_waitable_i;
	uv_process_t req;
	uv_stdio_container_t stdio[3];
	uv_process_options_t options;
	fut_t* future;
	int term_signal;
	int log_fd;
} tc_process_t;

static
void os_process_destroy(tc_waitable_i* w)
{
	tc_process_t* req = (tc_process_t*)w;
	//await(os->close(req->log_fd));
	tc_free(req->options.args);
	tc_free(req);
}

static
void os_process_exit(uv_process_t* process, int64_t exit_status, int term_signal)
{
	tc_process_t* proc = (tc_process_t*)process->data;
	fut_t* fut = (fut_t*)proc->future;
	proc->results = exit_status;
	proc->term_signal = term_signal;
	tc_fut_decr(fut);
}

fut_t* os_system_run(const char* cmd, const char** args, size_t numargs, const char* stdoutpath)
{
	tc_process_t* req = tc_calloc(1, sizeof(tc_process_t));
	req->instance = req;
	req->dtor = os_process_destroy;
	req->future = tc_fut_new(context->allocator, 1, req, 4);
	req->req.data = req;

	const char** pargs = (const char**)tc_malloc((numargs + 2) * sizeof(char*));
	pargs[0] = (char*)cmd;
	for (int i = 0; i < numargs; i++) pargs[i+1] = args[i];
	pargs[numargs+1] = NULL;

    req->stdio[0].flags = UV_IGNORE;
	if (stdoutpath) {
		uv_fs_t file_req;
		req->stdio[1].flags = UV_INHERIT_FD;
		req->stdio[1].data.fd = uv_fs_open(tc_eventloop(), &file_req, stdoutpath, O_CREAT | O_RDWR | O_TRUNC, 0644, NULL);
	}
	else req->stdio[1].flags = UV_IGNORE;
    req->stdio[2].flags = UV_INHERIT_FD;
    req->stdio[2].data.fd = 2;
	req->options.exit_cb = os_process_exit;
	req->options.file = cmd;
	req->options.args = pargs;
	req->options.stdio_count = 3;
	req->options.stdio = req->stdio;
	int result = uv_spawn(tc_eventloop(), &req->req, &req->options);
	TC_ASSERT(result == 0);
	return req->future;
}

const char* os_get_env(const char* name, tc_allocator_i* temp)
{
	char tempbuf[1024];
	size_t len = 1024;
	uv_os_getenv(name, tempbuf, &len);
	char* ptr = TC_ALLOC(temp, len + 1);
	memcpy(ptr, tempbuf, len);
	ptr[len] = '\0';
	return (const char*)ptr;
}
