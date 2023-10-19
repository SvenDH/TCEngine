/*==========================================================*/
/*							FILE SYSTEM						*/
/*==========================================================*/
#pragma once
#include "os.h"

#define FS_MAX_PATH 512
#if defined(_WINDOWS)
#define FS_SEP '\\'
#else
#define '/'
#endif

typedef enum {
	M_CONTENT,
	M_DEBUG,
	M_DOCUMENTS,
	M_SYSTEM,
	M_SAVE_0,
	M_EMPTY,
	M_COUNT,
} mount_t;

typedef enum {
	R_SHADER_BINARIES,
	R_SHADER_SOURCES,
	R_PIPELINE_CACHE,
	R_TEXTURES,
	R_MESHES,
	R_FONTS,
	R_ANIMATIONS,
	R_AUDIO,
	R_GPU_CONFIG,
	R_LOG,
	R_SCRIPTS,
	R_SCREENSHOTS,
	R_SYSTEM,
	R_OTHER_FILES,
    
	____rd_lib_counter_begin = R_OTHER_FILES + 1,
	R_MIDDLEWARE_0 = ____rd_lib_counter_begin,
	R_MIDDLEWARE_1,
	R_MIDDLEWARE_2,
	R_MIDDLEWARE_3,
	R_MIDDLEWARE_4,
	R_MIDDLEWARE_5,
	R_MIDDLEWARE_6,
	R_MIDDLEWARE_7,
	R_MIDDLEWARE_8,
	R_MIDDLEWARE_9,
	R_MIDDLEWARE_10,
	R_MIDDLEWARE_11,
	R_MIDDLEWARE_12,
	R_MIDDLEWARE_13,
	R_MIDDLEWARE_14,
	R_MIDDLEWARE_15,

	____rd_lib_counter_end = ____rd_lib_counter_begin + 99 * 2,
	RES_COUNT
} resourcedir_t;

typedef struct vfs_s vfs_t;

typedef struct mstream_s {
	uint8_t* buffer;
	size_t cursor;
	size_t capacity;
	bool owner;
} mstream_t;

typedef struct fstream_s {
	vfs_t* fs;
	struct fstream_s* base; // for chaining streams
	union {
		fd_t* fd;
		mstream_t mem;
		void* user;
	};
	intptr_t size;
	file_flags_t flags;
	mount_t mount;
} fstream_t;

typedef struct vfs_s {
	bool (*open)(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out);
	/* Closes and invalidates the file stream */
	bool (*close)(fstream_t* f);
	/* Reads at most `len` bytes from buf and writes them into the file. Returns the number of bytes written. */
	size_t (*read)(fstream_t* f, void* buf, size_t len);
	/* Reads at most `len` bytes from buf and writes them into the file. Returns the number of bytes written. */
	size_t (*write)(fstream_t* f, const void* buf, size_t len);
	/* Gets the current size of the file. Returns -1 if the size is unknown or unavailable */
	intptr_t (*size)(const fstream_t* f);
	/* Flushes all writes to the file stream to the underlying subsystem */
	bool (*flush)(fstream_t* f);

	void* user;

} vfs_t;

extern vfs_t memfs;
extern vfs_t systemfs;

typedef struct tagbstring * bstring;

void fs_parent_path(const char* path, char* output);

void fs_path_ext(const char* path, char* output);

void fs_path_filename(const char* path, char* output);

void fs_path_join(const char* base, const char* component, char* output);

const char* fs_get_resource_dir(resourcedir_t resourcedir);

void fs_set_resource_dir(vfs_t* fs, mount_t mount, resourcedir_t resourcedir, const char* path);

bool fs_open_mstream(const void* buf, size_t bufsize, file_flags_t flags, bool owner, fstream_t* out);

bool fs_open_fstream(const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out);

bool fs_close_stream(fstream_t* stream);

size_t fs_read_stream(fstream_t* stream, void* buf, size_t len);

size_t fs_read_bstr_stream(fstream_t* stream, bstring str, size_t len);

size_t fs_write_stream(fstream_t* stream, const void* buf, size_t len);

bool fs_copy_stream(fstream_t* dst, fstream_t* src, size_t len);

intptr_t fs_stream_size(const fstream_t* stream);

bool fs_flush_stream(fstream_t* stream);

bool fs_is_fstream(fstream_t* stream);

bool fs_is_mstream(fstream_t* stream);
