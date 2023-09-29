/*==========================================================*/
/*								OS							*/
/*==========================================================*/
#pragma once
#include "core.h"

#define TC_OS_MODULE_NAME "tc_os_module"


typedef struct tc_allocator_i tc_allocator_i;
typedef struct tc_stream_i tc_stream_i;
typedef struct tc_fut_s tc_fut_t;

typedef uint64_t tc_thread_t;

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


typedef struct tc_os_i {

    void* (*map)(size_t size);

    void (*unmap)(void* p, size_t size);

    void* (*reserve)(size_t size);

    void (*commit)(void* p, size_t size);

    size_t(*page_size)();

    void (*guard_page)(void* ptr, size_t size);

    /* Opens a file and returns the file handle on success or alse TC_INVALID_FILE
     */
    tc_fut_t* (*open)(const char* path, int flags);

    /* Reads a number of bytes at offset into a buffer
     */
    tc_fut_t* (*read)(fd_t file, char* buf, uint64_t size, int64_t offset);

    /* Writes a number of bytes from a buffer at an offset
     */
    tc_fut_t* (*write)(fd_t file, char* buf, uint64_t size, int64_t offset);

    /* Closes the file
     */
    tc_fut_t* (*close)(fd_t file);
    

    tc_fut_t* (*stat)(stat_t* stat, const char* path);

    tc_fut_t* (*scandir)(const char* path, tc_allocator_i* temp);

    tc_fut_t* (*mkdir)(const char* path);

    tc_fut_t* (*rmdir)(const char* path);

    tc_fut_t* (*rename)(const char* path, const char* new_path);

    tc_fut_t* (*link)(const char* path, const char* new_path);

    tc_fut_t* (*unlink)(const char* path);

    tc_fut_t* (*copy)(const char* path, const char* new_path);

    const char* (*tmpdir)(tc_allocator_i* a);

    const char* (*getcwd)(tc_allocator_i* a);

    bool (*chdir)(const char* path);

    uint32_t(*cpu_id)();

    uint32_t(*num_cpus)();

    tc_thread_t(*create_thread)(tc_thread_f entry, void* data, uint32_t stack_size);

    tc_thread_t(*current_thread)();

    void(*set_thread_affinity)(tc_thread_t thread, uint32_t cpu_num);

} tc_os_i;

extern tc_os_i* tc_os;
