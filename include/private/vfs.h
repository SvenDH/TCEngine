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
	RM_CONTENT,
	RM_DEBUG,
	RM_DOCUMENTS,
	RM_SYSTEM,
	RM_SAVE_0,
	RM_EMPTY,
	RM_COUNT,
} mount_t;

typedef enum {
	RD_SHADER_BINARIES,
	RD_SHADER_SOURCES,
	RD_PIPELINE_CACHE,
	RD_TEXTURES,
	RD_MESHES,
	RD_FONTS,
	RD_ANIMATIONS,
	RD_AUDIO,
	RD_GPU_CONFIG,
	RD_LOG,
	RD_SCRIPTS,
	RD_SCREENSHOTS,
	RD_SYSTEM,
	RD_OTHER_FILES,
    
	____rd_lib_counter_begin = RD_OTHER_FILES + 1,
	RD_MIDDLEWARE_0 = ____rd_lib_counter_begin,
	RD_MIDDLEWARE_1,
	RD_MIDDLEWARE_2,
	RD_MIDDLEWARE_3,
	RD_MIDDLEWARE_4,
	RD_MIDDLEWARE_5,
	RD_MIDDLEWARE_6,
	RD_MIDDLEWARE_7,
	RD_MIDDLEWARE_8,
	RD_MIDDLEWARE_9,
	RD_MIDDLEWARE_10,
	RD_MIDDLEWARE_11,
	RD_MIDDLEWARE_12,
	RD_MIDDLEWARE_13,
	RD_MIDDLEWARE_14,
	RD_MIDDLEWARE_15,

	____rd_lib_counter_end = ____rd_lib_counter_begin + 99 * 2,
	RD_COUNT
} resourcedir_t;

typedef struct vfs_s vfs_t;


void fs_parent_path(const char* path, char* output);

void fs_path_ext(const char* path, char* output);

void fs_path_filename(const char* path, char* output);

void fs_path_join(const char* base, const char* component, char* output);

const char* tc_get_resource_dir(resourcedir_t resourcedir);

void tc_set_resource_dir(vfs_t* io, mount_t mount, resourcedir_t resourcedir, const char* path);
