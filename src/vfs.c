/*==========================================================*/
/*							FILE							*/
/*==========================================================*/
#include "private_types.h"


/*==========================================================*/
/*						PRIVATE STRUCTS						*/
/*==========================================================*/

enum mounttype_t {
	MOUNT_DIRECTORY,
	MOUNT_ARCHIVE,
};

struct internal_dir_s {
	int16_t index;
};

struct internal_file_s {
	size_t size;
	int16_t index;
};

typedef struct tc_entry_s {
	sid_t path;
	int16_t parent;
	uint8_t ref;
	bool is_file;
	tc_rid_t rid;
} tc_entry_t;

typedef struct tc_mount_s {
	sid_t path;
	sid_t mounted_as;
	uint32_t mount_len;
	int ref;
	enum mounttype_t type;
	struct internal_file_s* files; /* buff */
	struct internal_dir_s* dirs;   /* buff */
} tc_mount_t;


static enum mounttype_t _tc_vfs_get_path(tc_vfs_t* fs, tc_rid_t id, char** path, char** mount);

static uint32_t _tc_vfs_copy_path(tc_vfs_t* fs, const char* dest, sid_t path, uint32_t offset);

static sid_t _tc_vfs_dir_handle(tc_vfs_t* fs, const char* path, uint32_t* len);

static void _tc_vfs_make_path(tc_vfs_t* fs, const char* dest, tc_mount_t entry, int16_t index);

static int16_t _tc_vfs_register(tc_vfs_t* fs, const char* path, uint32_t len, bool is_file);

void tc_vfs_init(tc_vfs_t* fs, tc_allocator_i* a) {
	memset(fs, 0, sizeof(tc_vfs_t));
	fs->a = a;
	tc_str_init(&fs->strings, a);
}

void tc_vfs_free(tc_vfs_t* fs) {
	buff_free(fs->entries, fs->a);
	if (fs->mounts) {
		for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
			buff_free(m->dirs, fs->a);
			buff_free(m->files, fs->a);
		}
		buff_free(fs->mounts, fs->a);
	}
	tc_str_free(&fs->strings);
}

static
uint32_t _tc_vfs_copy_path(tc_vfs_t* fs, const char* dest, sid_t path, uint32_t offset) {
	uint32_t len = 0;
	const char* p = tc_str_get(&fs->strings, path, &len);
	strncpy(dest, p + offset, (size_t)len + 1 - offset);
	return dest + len - offset;
}

static
sid_t _tc_vfs_dir_handle(tc_vfs_t* fs, const char* path, uint32_t* len) {
	char* last_slash = strrchr(path, '/');
	if (len) {
		*len = last_slash ? last_slash - path - 1 : 0;
	}
	if (last_slash) {
		return tc_hash_str_len(path, last_slash - path);
	}
	return tc_hash_str_len('\0', 0);
}

static
void _tc_vfs_make_path(tc_vfs_t* fs, const char* dest, tc_mount_t entry, int16_t index) {
	sid_t path_sid = fs->entries[index].path;
	strcpy(dest, tc_str_get(&fs->strings, entry.path, NULL));
	strcat(dest, *dest == '\0' ? "" : "/");
	strcat(dest, tc_str_get(&fs->strings, path_sid, NULL) + entry.mount_len + 1);
}

static
int16_t _tc_vfs_register(tc_vfs_t* fs, const char* path, uint32_t len, bool is_file) {
	sid_t path_id = tc_str_intern(&fs->strings, path, len);
	int16_t free = -1;
	uint32_t count = buff_count(fs->entries);
	// Search for already referenced path
	for (uint32_t i = 0; i < count; ++i) {
		if (fs->entries[i].ref > 0 &&
			fs->entries[i].path == path_id) {
			TC_ASSERT(is_file == fs->entries[i].is_file);
			fs->entries[i].ref++;
			tc_str_release(&fs->strings, path_id);
			return (int16_t)i;
		}
		if (fs->entries[i].ref == 0)
			free = i;
	}
	// Could not find path, add new path
	if (free < 0) {
		free = (int)count;
		buff_add(fs->entries, 1, fs->a);
	}
	fs->entries[free] = (tc_entry_t){ path_id, -1, 1, is_file };
	return free;
}

static
void _tc_vfs_recurse_dirs(tc_vfs_t* fs, tc_mount_t* mount, const uint32_t index, char* temp) {
	TC_TEMP_INIT(a);
	TC_ASSERT(mount);
	uint32_t mount_len = 0;
	sid_t index_path = fs->entries[index].path;

	tc_str_get(&fs->strings, mount->mounted_as, &mount_len);
	const char* path = tc_str_get(&fs->strings, index_path, NULL);
	path += mount_len;
	if (*path == '/') {
		++path;
	}
	_tc_vfs_copy_path(fs, temp, mount->path, 0);
	strcat(temp, (*path == '\0' || *temp == '\0') ? "" : "/");
	strcat(temp, path);
	// Open dir
	char* name = await(tc_os->scandir(temp, &name, temp, &a), 0);
	while (name[0] != '\0') {
		if (!name || *name == '\0' ||
			strcmp(name, ".") == 0 ||
			strcmp(name, "..") == 0) {
			continue;
		}
		size_t name_len = strlen(name);
		bool is_dir = name[name_len - 1] == '/';
		_tc_vfs_copy_path(fs, temp, mount->path, 0);
		strcat(temp, (*path == '\0' || *temp == '\0') ? "" : "/");
		strcat(temp, path);
		strcat(temp, *temp == '\0' ? "" : "/");
		strcat(temp, name);
		stat_t stat;
		await(tc_os->stat(&stat, temp));
		if (stat.exists) {
			_tc_vfs_copy_path(fs, temp, mount->mounted_as, 0);
			strcat(temp, "/");
			strcat(temp, path);
			strcat(temp, *path == '\0' ? "" : "/");
			strcat(temp, name);
			int16_t i = _tc_vfs_register(fs, temp, strlen(temp), !is_dir);
			if (is_dir) {
				buff_push(mount->dirs, fs->a, (struct internal_dir_s) {i});
				_tc_vfs_recurse_dirs(
					mount, mount->path, mount->mounted_as,
					buff_last(mount->dirs).index, temp);
			}
			else {
				buff_push(mount->files, fs->a, (struct internal_file_s) { stat.size, i });
			}
		}
		name += name_len + 1;
	}
	TC_TEMP_CLOSE(a);
}

static
void _tc_vfs_initialize_zip(tc_vfs_t* fs, tc_mount_t* mount) {
	/*
	char* path = tc_str_get(&fs->strings, mount->path, NULL);
	tc_stream_i* zip = tc_zip->create(path, 0, fs->allocator);
	if (zip) {
		char temp[1024];
		bool err = tc_zip->iter(zip);
		while (err) {
			entry_info_t file_info = { 0 };
			err = tc_zip->next(zip, &file_info);
			if (!err) {
				TRACE(LOG_ERROR, 
					"[File]: Failed to get entry info for zip %s", path);
				break;
			}
			path_copy(temp, mount->mounted_as, 0);
			strcat(temp, "/");
			strcat(temp, file_info.path);
			uint32_t len = strlen(temp);
			if (temp[len - 1] == '\\' || temp[len - 1] == '/')
				buff_push(
					mount->dirs,
					fs->allocator,
					(tc_internal_dir_t) {
				register_asset(temp, len - 1, false)
			});
			else
				buff_push(
					mount->files,
					fs->allocator,
					(tc_internal_file_t) {
				file_info.size, register_asset(temp, len, true)
			});

			sid_t handle = dir_handle(temp, &len);
			uint32_t num_dirs = buff_count(mount->dirs);
			bool found = false;
			TC_LOCK(&fs->entrylock);
			{
				for (uint32_t j = 0; j < num_dirs; ++j) {
					if (handle == fs->entries[mount->dirs[j].index].path) {
						found = true; break;
					}
				}
			}
			TC_UNLOCK(&fs->entrylock);
			if (!found)
				buff_push(
					mount->dirs,
					fs->allocator,
					(tc_internal_dir_t) {
				register_asset(temp, len, false)
			});
		}
	}
	zip->close(zip);
	*/
}

static
void _tc_vfs_collate_dirs(tc_vfs_t* fs, tc_mount_t* mount) {
	char temp[1024];
	for (uint32_t i = 0; i < buff_count(mount->dirs); ++i) {
		tc_entry_t* subdir = &fs->entries[mount->dirs[i].index];
		if (subdir->parent < 0) {
			_tc_vfs_copy_path(fs, temp, subdir->path, 0);
			sid_t h = _tc_vfs_dir_handle(fs, temp, NULL);
			for (uint32_t j = 0; j < buff_count(fs->entries); ++j) {
				if (fs->entries[j].path == h) {
					subdir->parent = j;
					break;
				}
			}
		}
	}
	for (uint32_t i = 0; i < buff_count(mount->files); ++i) {
		tc_entry_t* file = &fs->entries[mount->files[i].index];
		if (file->parent < 0) {
			_tc_vfs_copy_path(fs, temp, file->path, 0);
			sid_t handle = _tc_vfs_dir_handle(fs, temp, NULL);
			for (uint32_t j = 0; j < buff_count(fs->entries); ++j) {
				if (fs->entries[j].path == handle) {
					file->parent = j;
					break;
				}
			}
		}
	}
}

static
void _tc_vfs_remove_entry(tc_vfs_t* fs, const uint32_t index) {
	tc_entry_t* coll = &fs->entries[index];
	TC_ASSERT(coll->ref > 0);
	--coll->ref;
	if (coll->ref == 0) {
		tc_str_release(&fs->strings, coll->path);
	}
}

static
int16_t _tc_vfs_find_entry(tc_vfs_t* fs, const char* path) {
	sid_t handle = tc_hash_str(path);
	int16_t index = -1;
	for (uint32_t i = 0; i < buff_count(fs->entries); i++) {
		if (fs->entries[i].path == handle) {
			index = i;
			break;
		}
	}
	return index;
}

bool tc_vfs_mount(tc_vfs_t* fs, const char* path, char const* mount_as) {
	char temp[240];
	int type = -1;
	uint32_t len = (uint32_t)strlen(path);
	uint32_t mount_len = (uint32_t)strlen(mount_as);
	if (strchr(mount_as, ':') || strchr(mount_as, '\\') || strchr(path, '\\') ||
		(len > 0 && path[0] == '/') ||
		(len > 1 && path[len - 1] == '/') ||
		mount_len == 0 || mount_as[0] != '/' ||
		(mount_len > 1 && mount_as[mount_len - 1] == '/')) {
		TRACE(LOG_WARNING, "[Filesystem]: %s -> %s not a valid path.", path, mount_as);
		return false;
	}
	stat_t stat;
	await(tc_os->stat(&stat, path));
	if (stat.exists) {
		if (stat.is_dir) {
			type = MOUNT_DIRECTORY;
		}
		else {
			type = MOUNT_ARCHIVE;
		}
	}
	if (type < 0) {
		TRACE(LOG_WARNING, "[Filesystem]: %s is not a valid mount.", path);
		return false;
	}
	// Add mount to file system
	buff_add(fs->mounts, 1, fs->a);
	tc_mount_t* mount = &buff_last(fs->mounts);
	memset(mount, 0, sizeof(tc_mount_t));

	int16_t dir_index = _tc_vfs_register(fs, mount_as, mount_len, false);
	buff_push(mount->dirs, fs->a, (struct internal_dir_s) { dir_index });

	mount->path = tc_str_intern(&fs->strings, path, len);
	mount->mounted_as = tc_str_intern(&fs->strings, mount_as, mount_len);
	mount->mount_len = mount_len;
	mount->type = type;

	if (type == MOUNT_DIRECTORY) {
		_tc_vfs_recurse_dirs(fs, mount, dir_index, temp);
	}
	else if (type == MOUNT_ARCHIVE) {
		_tc_vfs_initialize_zip(fs, mount);
	}
	_tc_vfs_collate_dirs(fs, mount);
	return true;
}

void tc_vfs_dismount(tc_vfs_t* fs, const char* mount_as) {
	TC_ASSERT(mount_as);
	sid_t handle = _tc_vfs_dir_handle(fs, mount_as, NULL);
	for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		if (m->mounted_as == handle) {
			tc_str_release(&fs->strings, m->mounted_as);
			tc_str_release(&fs->strings, m->path);
			for (uint32_t j = 0; j < buff_count(m->dirs); ++j) {
				_tc_vfs_remove_entry(fs, m->dirs[j].index);
			}
			for (uint32_t j = 0; j < buff_count(m->files); ++j) {
				_tc_vfs_remove_entry(fs, m->files[j].index);
			}
			buff_free(m->dirs, fs->a);
			buff_free(m->files, fs->a);
			memcpy(m, m + 1, sizeof(tc_mount_t) * (&buff_last(fs->mounts) - m));
			buff_pop(fs->mounts);
		}
	}
}

tc_mount_t* find_mount(tc_vfs_t* fs, const char* path) {
	char temp[240];
	tc_rid_t rid;
	tc_mount_t* mount = NULL;
	uint32_t len = (uint32_t)strlen(path);
	sid_t handle = tc_hash_str_len(path, len);
	for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		_tc_vfs_copy_path(fs, temp, m->mounted_as, 0);
		if (strncmp(path, temp, m->mount_len) == 0) {
			mount = m;
			struct internal_file_s* files = m->files;
			for (uint32_t i = 0; i < buff_count(files); i++) {
				sid_t other = fs->entries[files[i].index].path;
				rid = fs->entries[files[i].index].rid;
				if (handle == other) {
					return mount;
				}
			}
		}
	}
}

tc_rid_t tc_vfs_find(tc_vfs_t* fs, const char* path, bool create) {
	char temp[240];
	tc_rid_t rid;
	uint32_t len = (uint32_t)strlen(path);
	sid_t handle = tc_hash_str_len(path, len);
	tc_mount_t* mount = NULL;
	for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		_tc_vfs_copy_path(fs, temp, m->mounted_as, 0);
		if (strncmp(path, temp, m->mount_len) == 0) {
			mount = m;
			struct internal_file_s* files = m->files;
			for (uint32_t i = 0; i < buff_count(files); i++) {
				sid_t other = fs->entries[files[i].index].path;
				rid = fs->entries[files[i].index].rid;
				if (handle == other) {
					return rid;
				}
			}
		}
	}
	// Create the file if it was not found and should be created
	rid = (tc_rid_t){ 0 };
	if (create && mount) {
		sid_t handle = _tc_vfs_dir_handle(fs, path, NULL);
		int16_t j = -1;
		for (uint32_t i = 0; i < buff_count(fs->entries); i++) {
			if (fs->entries[i].path == handle) {
				j = i;
				break;
			}
		}
		if (j < 0) {
			TRACE(LOG_WARNING, "[File]: Error, could not find directory for %s", path);
			return rid;
		}
		int16_t c = _tc_vfs_register(fs, path, len, true);
		fs->entries[c].parent = j;
		rid = fs->entries[c].rid;
		uint32_t i = 0;
		for (i = 0; i < buff_count(mount->files); i++) {
			if (mount->files[i].index == -1) {
				break;
			}
		}
		if (i != buff_count(mount->files)) {
			mount->files[i].index = c;
		}
		else {
			buff_push(mount->files, fs->a, (struct internal_file_s) { 0, c });
		}
	}
	return rid;
}

size_t tc_vfs_size(tc_vfs_t* fs, tc_rid_t file) {
	tc_mount_t* mount = NULL;
	for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		if (strncmp(path, temp, m->mount_len) == 0) {
			mount = m;
			struct internal_file_s* files = m->files;
			for (uint32_t i = 0; i < buff_count(files); i++) {
				sid_t other = fs->entries[files[i].index].path;
			}
		}
	}
	char temp[1024];
	tc_mount_t entry = find_mount(file.mount, file.path);
	tc_mount_t* mount = entry.mount;
	if (mount) {
		return mount->files[file.index].size;
	}
	return 0;
}

int tc_vfs_delete(tc_vfs_t* fs, tc_rid_t file) {
	char temp[1024];
	tc_mount_t entry = find_mount(file.mount, file.path);
	tc_mount_t* mount = entry.mount;
	if (mount && entry.type == MOUNT_DIRECTORY) {
		if (mount->files[file.index].counter != file.counter) {
			return 0;
		}
		int16_t i = mount->files[file.index].index;
		mount->files[file.index].size = 0;
		mount->files[file.index].index = -1;
		mount->files[file.index].counter++;
		_tc_vfs_make_path(fs, temp, entry, i);
		_tc_vfs_remove_entry(fs, i);
		return tc_os->unlink(temp);
	}
	return 0;
}

static
enum mounttype_t _tc_vfs_get_path(tc_vfs_t* fs, tc_rid_t id, char** path, char** mount) {
	for (tc_mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		struct internal_file_s* files = m->files;
		for (uint32_t i = 0; i < buff_count(files); i++) {
			int16_t idx = files[i].index;
			sid_t sid = fs->entries[idx].path;
			tc_rid_t other = fs->entries[idx].rid;
			if (id.handle == other.handle) {
				if (mount) {
					*mount = tc_str_get(&fs->strings, m->path, NULL);
				}
				if (path) {
					*path = tc_str_get(&fs->strings, sid, NULL) + m->mount_len;
				}
				return m->type;
			}
		}
	}
	return -1;
}
