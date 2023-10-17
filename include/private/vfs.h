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
	R_COUNT
} resourcedir_t;

typedef enum { SBO_START_OF_FILE, SBO_CURRENT_POSITION, SBO_END_OF_FILE } seek_t;

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
	ssize_t size;
	file_flags_t flags;
	mount_t mount;
} fstream_t;

typedef struct vfs_s {
	bool (*open)(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out);
	bool (*close)(fstream_t* f);
	size_t (*read)(fstream_t* f, void* buf, size_t len);
	size_t (*write)(fstream_t* f, const void* buf, size_t len);
	bool (*seek)(fstream_t* f, seek_t seek, ssize_t offset);
	ssize_t (*position)(const fstream_t* f);
	ssize_t (*size)(const fstream_t* f);
	bool (*flush)(fstream_t* f);
	bool (*eof)(const fstream_t* f);
	const char* (*mount)(mount_t mount);
	bool (*getpropint64)(fstream_t* f, int32_t prop, int64_t *val);
	bool (*setpropint64)(fstream_t* f, int32_t prop, int64_t val);
	void* user;
} vfs_t;

void fs_parent_path(const char* path, char* output);

void fs_path_ext(const char* path, char* output);

void fs_path_filename(const char* path, char* output);

void fs_path_join(const char* base, const char* component, char* output);

const char* tc_get_resource_dir(resourcedir_t resourcedir);

void tc_set_resource_dir(vfs_t* fs, mount_t mount, resourcedir_t resourcedir, const char* path);