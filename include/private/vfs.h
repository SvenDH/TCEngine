/*==========================================================*/
/*							FILE SYSTEM						*/
/*==========================================================*/
#pragma once
#include "os.h"

#define FS_MAX_PATH 256
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

typedef struct {
	vfs_t* fs;      // NULL
	mount_t mount;  // RM_CONTENT
	char path[FS_MAX_PATH];
	bool bundled;   // false
} resourcedirinfo_t;

static resourcedirinfo_t resdirs[RD_COUNT] = { 0 };

inline static
void fs_parent_path(const char* path, char* output)
{
	size_t pathlen = strlen(path);
	TC_ASSERT(pathlen != 0);
	const char* seploc = strrchr(path, FS_SEP);
	if (seploc == NULL) {
		seploc = strrchr(path, '/');
		if (seploc == NULL) return;
	}
	const size_t outlen = pathlen - strlen(seploc);
	strncpy(output, path, outlen);
	output[outlen] = '\0';
}

inline static
void fs_path_extension(const char* path, char* output)
{
	size_t pathlen = strlen(path);
	TC_ASSERT(pathlen != 0);
	const char* dot = strrchr(path, '.');
	if (dot == NULL) return;
	dot += 1;
	const size_t extlen = strlen(dot);
	if (extlen == 0 || dot[0] == '/' || dot[0] == FS_SEP)
		return;
	strncpy(output, dot, extlen);
	output[extlen] = '\0';
}

inline static
void fs_path_filename(const char* path, char* output)
{
	const size_t pathlen = strlen(path);
	TC_ASSERT(pathlen != 0);
	char parentPath[FS_MAX_PATH] = { 0 };
	fs_parent_path(path, parentPath);
	size_t parentlen = strlen(parentPath);
	if (parentlen < pathlen && (path[parentlen] == FS_SEP || path[parentlen] == '/'))
		parentlen += 1;
	char extension[FS_MAX_PATH] = { 0 };
	fs_path_extension(path, extension);
	const size_t extensionlen = extension[0] != 0 ? strlen(extension) + 1 : 0;
	const size_t outlen = pathlen - parentlen - extensionlen;
	strncpy(output, path + parentlen, outlen);
	output[outlen] = '\0';
}

inline static
void fs_path_join(const char* base, const char* component, char* output)
{
	const size_t complen = strlen(component);
	const size_t baselen = strlen(base);
	strncpy(output, base, baselen);
	size_t newlen = baselen;
	output[baselen] = '\0';
	if (complen == 0) return;
	const char sepstr[2] = { FS_SEP, 0 };
	if (newlen != 0 && output[newlen - 1] != FS_SEP) {
		strncat(output, sepstr, 1);
		newlen += 1;
		output[newlen] = '\0';
	}
	for (size_t i = 0; i < complen; i++) {
        if (newlen >= FS_MAX_PATH)
		    TRACE(LOG_ERROR, "Appended path length '%d' greater than FS_MAX_PATH, base: \"%s\", component: \"%s\"", newlen, base, component);
		TC_ASSERT(newlen < FS_MAX_PATH);
		if ((component[i] == FS_SEP || component[i] == '/') && newlen != 0 && output[newlen - 1] != FS_SEP) {
			strncat(output, sepstr, 1);
			newlen += 1;
			output[newlen] = '\0';
			continue;
		}
		else if (component[i] == '.') {
			size_t j = i + 1;
			if (j < complen) {
				if (component[j] == FS_SEP || component[j] == '/') {
					i = j;
					continue;
				}
				else if (
					component[j] == '.' && ++j < complen &&
					(component[j] == FS_SEP || component[j] == '/')) {
					if (newlen > 1 && output[newlen - 1] == FS_SEP)
						newlen -= 1;
					for (; newlen > 0; newlen -= 1)
						if (output[newlen - 1] == FS_SEP || output[newlen - 1] == '/')
							break;
					i = j;
					continue;
				}
			}
		}
		output[newlen] = component[i];
		newlen += 1;
		output[newlen] = '\0';
	}
	if (output[newlen - 1] == FS_SEP) newlen -= 1;
	output[newlen] = '\0';
}

inline static
const char* fs_resource_dir(resourcedir_t resource_type)
{
	const resourcedirinfo_t* dir = &resdirs[resource_type];
	if (!dir->fs) {
        if (!dir->path[0])  TRACE(LOG_ERROR, "Trying to get an unset resource directory '%d', make sure the resourceDirectory is set on start of the application", resource_type);
		TC_ASSERT(dir->path[0] != 0);
	}
	return dir->path;
}
