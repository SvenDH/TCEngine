/*==========================================================*/
/*								OS							*/
/*==========================================================*/
#pragma once
#include "core.h"

#define TC_OS_MODULE_NAME "tc_os_module"


typedef struct tc_allocator_i tc_allocator_i;
typedef struct tc_stream_i tc_stream_i;
typedef struct tc_fut_s fut_t;
typedef uint64_t tc_thread_t;
typedef void* tc_window_t;

typedef void(*tc_thread_f)(void*);

#define TC_INVALID_FILE 0xcfffffff


typedef enum {
    FILE_READ = 0,
    FILE_WRITE = 1 << 0,
    FILE_READWRITE = 1 << 1,
    FILE_APPEND = 1 << 3,
    FILE_CREATE = 1 << 8,
} file_flags_t;


typedef struct stat_s {
    /* 
     * File size
     */
    uint64_t size;
    /* 
     * Unix timestamp of last altered time
     */
    uint64_t last_altered;
    /* 
     * Whether the file or directory exists
     */
    bool exists;
    /* 
     * Whether the path is a directory
     */
    bool is_dir;
} stat_t;


void* os_map(size_t size);

void os_unmap(void* p, size_t size);

void* os_reserve(size_t size);

void os_commit(void* p, size_t size);

size_t os_page_size();

void os_guard_page(void* ptr, size_t size);

/* Opens a file and returns the file handle on success or alse TC_INVALID_FILE */
fut_t* os_open(const char* path, file_flags_t flags);

/* Closes the file */
fut_t* os_close(fd_t file);

/* Reads a number of bytes at offset into a buffer */
fut_t* os_read(fd_t file, char* buf, uint64_t size, int64_t offset);

/* Writes a number of bytes from a buffer at an offset */
fut_t* os_write(fd_t file, char* buf, uint64_t size, int64_t offset);

/* Syncs the file to disk */
fut_t* os_sync(fd_t file);

fut_t* os_stat(stat_t* stat, const char* path);

fut_t* os_scandir(const char* path, tc_allocator_i* temp);

fut_t* os_mkdir(const char* path);

fut_t* os_rmdir(const char* path);

fut_t* os_rename(const char* path, const char* new_path);

fut_t* os_link(const char* path, const char* new_path);

fut_t* os_unlink(const char* path);

fut_t* os_copy(const char* path, const char* new_path);

const char* os_tmpdir(tc_allocator_i* a);

const char* os_getcwd(tc_allocator_i* a);

bool os_chdir(const char* path);

uint32_t os_cpu_id();

uint32_t os_num_cpus();

tc_thread_t os_create_thread(tc_thread_f entry, void* data, uint32_t stack_size);

tc_thread_t os_current_thread();

void os_set_thread_affinity(tc_thread_t thread, uint32_t cpu_num);

tc_window_t os_create_window(int width, int height, const char* title);

void os_destroy_window(tc_window_t window);

fut_t* os_system_run(const char* cmd, const char** args, size_t numargs, const char* stdoutpath);

const char* os_get_env(const char* name, tc_allocator_i* temp);
