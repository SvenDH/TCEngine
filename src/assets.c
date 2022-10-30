/*==========================================================*/
/*							FILE							*/
/*==========================================================*/
#include "module.h"
#include "assets.h"
#include "sbuf.h"
#include "os.h"
#include "lock.h"
#include "fiber.h"
#include "stream.h"
#include "zip.h"
#include "temp.h"
#include "log.h"
#include "future.h"
#include "strings.h"


enum mounttype_t {
	MOUNT_DIRECTORY,
	MOUNT_ARCHIVE,
};

typedef struct internal_dir_s {
	int16_t index;
} internal_dir_t;

typedef struct internal_file_s {
	size_t size;
	int16_t index;
} internal_file_t;

typedef struct fsentry_s {
	sid_t path;
	int16_t parent;
	uint8_t ref;
	bool is_file;
	tc_rid_t rid;
} assetentry_t;

typedef struct mount_s {
	sid_t path;
	sid_t mounted_as;
	uint32_t mount_len;
	int ref;
	enum mounttype_t type;
	internal_file_t* files; /* buff */
	internal_dir_t* dirs; /* buff */
} mount_t;

typedef struct {
	tc_allocator_i* allocator;
	ALIGNED(lock_t, 64) stringlock;
	stringpool_t strings;
	ALIGNED(lock_t, 64) mountlock;	//TODO: RW lock
	mount_t* mounts;
	ALIGNED(lock_t, 64) entrylock;	//TODO: RW lock
	assetentry_t* entries;
} assetmanager_t;


static assetmanager_t* fs = NULL;


void assets_init(tc_allocator_i* a)
{
	fs = tc_malloc(a, sizeof(assetmanager_t));
	memset(fs, 0, sizeof(assetmanager_t));
	fs->allocator = a;
	fs->strings = stringpool_init(a);
}

void assets_close(tc_allocator_i* a)
{
	buff_free(fs->entries, a);
	if (fs->mounts) {
		for (mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
			buff_free(m->dirs, a);
			buff_free(m->files, a);
		}
		buff_free(fs->mounts, a);
	}
	stringpool_destroy(&fs->strings);
	tc_free(a, fs, sizeof(assetmanager_t));
}

static
sid_t path_intern(const char* path, uint32_t len)
{
	TC_LOCK(&fs->stringlock);
	sid_t id = string_intern(&fs->strings, path, len);
	TC_UNLOCK(&fs->stringlock);
	return id;
}

static
void path_release(sid_t path)
{
	TC_LOCK(&fs->stringlock);
	string_release(&fs->strings, path);
	TC_UNLOCK(&fs->stringlock);
}

static
void path_retain(sid_t path)
{
	TC_LOCK(&fs->stringlock);
	string_retain(&fs->strings, path);
	TC_UNLOCK(&fs->stringlock);
}

static
const char* path_get(sid_t path, uint32_t* len)
{
	TC_LOCK(&fs->stringlock);
	const char* r = string_get(&fs->strings, path, len);
	TC_UNLOCK(&fs->stringlock);
	return r;
}

static
uint32_t path_copy(const char* dest, sid_t path, uint32_t offset)
{
	TC_LOCK(&fs->stringlock);
	uint32_t len = 0;
	const char* p = string_get(&fs->strings, path, &len);
	strncpy(dest, p + offset, (size_t)len + 1 - offset);
	TC_UNLOCK(&fs->stringlock);
	return dest + len - offset;
}

static
sid_t dir_handle(const char* path, uint32_t* len)
{
	char* last_slash = strrchr(path, '/');
	if (len) *len = last_slash ? last_slash - path - 1 : 0;
	if (last_slash) 
		return tc_hash_string_len(path, last_slash - path);
	else
		return tc_hash_string_len('\0', 0);
}

static
void _fs_get_path(const char* dest, mount_t entry, int16_t index)
{
	TC_LOCK(&fs->entrylock);
	sid_t path_sid = fs->entries[index].path;
	TC_UNLOCK(&fs->entrylock);
	TC_LOCK(&fs->stringlock);
	{
		strcpy(dest, string_get(&fs->strings, entry.path, NULL));
		strcat(dest, *dest == '\0' ? "" : "/");
		strcat(dest, string_get(&fs->strings, path_sid, NULL) +
			entry.mount_len + 1);
	}
	TC_UNLOCK(&fs->stringlock);
}

static
int16_t register_asset(const char* path, uint32_t len, bool is_file)
{
	sid_t path_id = path_intern(path, len);
	int16_t free = -1;
	TC_LOCK(&fs->entrylock);
	{
		uint32_t count = buff_count(fs->entries);
		// Search for already referenced path
		for (uint32_t i = 0; i < count; ++i) {
			if (fs->entries[i].ref > 0 &&
				fs->entries[i].path == path_id) {
				TC_ASSERT(is_file == fs->entries[i].is_file);
				fs->entries[i].ref++;
				path_release(path_id);
				TC_UNLOCK(&fs->entrylock);
				return (int16_t)i;
			}
			if (fs->entries[i].ref == 0)
				free = i;
		}
		// Could not find path, add new path
		if (free < 0) {
			free = (int)count;
			buff_add(fs->entries, 1, fs->allocator);
		}
		fs->entries[free] = (assetentry_t){ path_id, -1, 1, is_file };
	}
	TC_UNLOCK(&fs->entrylock);
	return free;
}

static
void recurse_dirs(mount_t* mount, const uint32_t index, char* temp)
{
	TC_TEMP_INIT(a);
	TC_ASSERT(mount);
	uint32_t mount_len = 0;
	TC_LOCK(&fs->entrylock);
	sid_t index_path = fs->entries[index].path;
	TC_UNLOCK(&fs->entrylock);

	TC_LOCK(&fs->stringlock);
	string_get(&fs->strings, mount->mounted_as, &mount_len);
	const char* path = string_get(&fs->strings, index_path, NULL);
	TC_UNLOCK(&fs->stringlock);

	path += mount_len;
	if (*path == '/') ++path;
	path_copy(temp, mount->path, 0);
	strcat(temp, (*path == '\0' || *temp == '\0') ? "" : "/");
	strcat(temp, path);
	// Open dir
	char* name = await(tc_os->scandir(temp, &name, temp, &a), 0);
	while (name[0] != '\0') {
		if (!name || *name == '\0' ||
			strcmp(name, ".") == 0 ||
			strcmp(name, "..") == 0)
			continue;
		size_t name_len = strlen(name);
		bool is_dir = name[name_len - 1] == '/';
		path_copy(temp, mount->path, 0);
		strcat(temp, (*path == '\0' || *temp == '\0') ? "" : "/");
		strcat(temp, path);
		strcat(temp, *temp == '\0' ? "" : "/");
		strcat(temp, name);
		stat_t stat;
		await(tc_os->stat(&stat, temp));
		if (stat.exists) {
			path_copy(temp, mount->mounted_as, 0);
			strcat(temp, "/");
			strcat(temp, path);
			strcat(temp, *path == '\0' ? "" : "/");
			strcat(temp, name);
			int16_t i = register_asset(temp, strlen(temp), !is_dir);
			if (is_dir) {
				buff_push(mount->dirs, fs->allocator,
					(internal_dir_t) {i});
				recurse_dirs(
					mount, mount->path, mount->mounted_as,
					buff_last(mount->dirs).index, temp);
			}
			else
				buff_push(mount->files, fs->allocator,
					(internal_file_t) {stat.size, i});
		}
		name += name_len + 1;
	}
	TC_TEMP_CLOSE(a);
}

static
void initialize_zip(mount_t* mount)
{
	char* path = path_get(mount->path, NULL);
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
					(internal_dir_t) {
				register_asset(temp, len - 1, false)
			});
			else
				buff_push(
					mount->files,
					fs->allocator,
					(internal_file_t) {
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
					(internal_dir_t) {
				register_asset(temp, len, false)
			});
		}
	}
	zip->close(zip);
}

static
void collate_dirs(mount_t* mount)
{
	char temp[1024];
	for (uint32_t i = 0; i < buff_count(mount->dirs); ++i) {
		TC_LOCK(&fs->entrylock);
		{
			assetentry_t* subdir = &fs->entries[mount->dirs[i].index];
			if (subdir->parent < 0) {
				path_copy(temp, subdir->path, 0);
				sid_t handle = dir_handle(temp, NULL);
				for (uint32_t j = 0; j < buff_count(fs->entries); ++j) {
					if (fs->entries[j].path == handle) {
						subdir->parent = j;
						break;
					}
				}
			}
		}
		TC_UNLOCK(&fs->entrylock);
	}
	for (uint32_t i = 0; i < buff_count(mount->files); ++i) {
		TC_LOCK(&fs->entrylock);
		{
			assetentry_t* file = &fs->entries[mount->files[i].index];
			if (file->parent < 0) {
				path_copy(temp, file->path, 0);
				sid_t handle = dir_handle(temp, NULL);
				for (uint32_t j = 0; j < buff_count(fs->entries); ++j) {
					if (fs->entries[j].path == handle) {
						file->parent = j;
						break;
					}
				}
			}
		}
		TC_UNLOCK(&fs->entrylock);
	}
}

static
void remove_entry(const uint32_t index)
{
	assetentry_t* coll = &fs->entries[index];
	TC_ASSERT(coll->ref > 0);
	--coll->ref;
	if (coll->ref == 0)
		path_release(coll->path);
}

static
int16_t find_entry(const char* path)
{
	sid_t handle = tc_hash_string(path);
	int16_t index = -1;
	for (uint32_t i = 0; i < buff_count(fs->entries); i++)
		if (fs->entries[i].path == handle) { index = i; break; }
	return index;
}

bool fs_mount(const char* path, char const* mount_as)
{
	char temp[240];
	int type = -1;
	uint32_t len = (uint32_t)strlen(path);
	uint32_t mount_len = (uint32_t)strlen(mount_as);
	if (strchr(mount_as, ':') || strchr(mount_as, '\\') || strchr(path, '\\') ||
		(len > 0 && path[0] == '/') ||
		(len > 1 && path[len - 1] == '/') ||
		mount_len == 0 || mount_as[0] != '/' ||
		(mount_len > 1 && mount_as[mount_len - 1] == '/')) {
		TRACE(LOG_WARNING,
			"[Filesystem]: %s -> %s not a valid path.", path, mount_as);
		return false;
	}
	stat_t stat;
	await(tc_os->stat(&stat, path));
	if (stat.exists) {
		if (stat.is_dir) type = MOUNT_DIRECTORY;
		else type = MOUNT_ARCHIVE;
	}
	if (type < 0) {
		TRACE(LOG_WARNING,
			"[Filesystem]: %s is not a valid mount.", path); return false;
	}
	// Add mount to file system
	TC_LOCK(&fs->mountlock);
	{
		buff_add(fs->mounts, 1, fs->allocator);
		mount_t* mount = &buff_last(fs->mounts);
		memset(mount, 0, sizeof(mount_t));
		int16_t dir_index = register_asset(mount_as, mount_len, false);
		buff_push(mount->dirs, fs->allocator, (internal_dir_t) { dir_index });

		mount->path = path_intern(path, len);
		mount->mounted_as = path_intern(mount_as, mount_len);
		mount->mount_len = mount_len;
		mount->type = type;

		if (type == MOUNT_DIRECTORY)
			recurse_dirs(mount, dir_index, temp);
		else if (type == MOUNT_ARCHIVE)
			initialize_zip(mount);
		collate_dirs(mount);
	}
	TC_UNLOCK(&fs->mountlock);
	return true;
}

void fs_dismount(const char* mount_as)
{
	TC_ASSERT(mount_as);
	sid_t handle = dir_handle(mount_as, NULL);
	TC_LOCK(&fs->mountlock);
	{
		for (mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
			if (m->mounted_as == handle) {
				path_release(m->mounted_as);
				path_release(m->path);
				for (uint32_t j = 0; j < buff_count(m->dirs); ++j)
					remove_entry(m->dirs[j].index);
				for (uint32_t j = 0; j < buff_count(m->files); ++j)
					remove_entry(m->files[j].index);

				buff_free(m->dirs, fs->allocator);
				buff_free(m->files, fs->allocator);
				memcpy(m, m + 1,
					sizeof(mount_t) * (&buff_last(fs->mounts) - m));
				buff_pop(fs->mounts);
			}
		}
	}
	TC_UNLOCK(&fs->mountlock);
}

tc_rid_t find_asset(const char* path, bool create)
{
	char temp[240];
	tc_rid_t rid;
	uint32_t len = (uint32_t)strlen(path);
	sid_t handle = tc_hash_string_len(path, len);
	mount_t* mount = { 0 };
	TC_LOCK(&fs->mountlock);
	for (mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
		path_copy(temp, m->mounted_as, 0);
		if (strncmp(path, temp, m->mount_len) == 0) {
			mount = m;
			internal_file_t* files = m->files;
			for (uint32_t i = 0; i < buff_count(files); i++) {
				TC_LOCK(&fs->entrylock);
				sid_t other = fs->entries[files[i].index].path;
				rid = fs->entries[files[i].index].rid;
				TC_UNLOCK(&fs->entrylock);
				if (handle == other) {
					TC_UNLOCK(&fs->mountlock);
					return rid;
				}
			}
		}
	}
	// Create the file if it was not found and should be created
	rid = (tc_rid_t){ 0 };
	if (create && mount) {
		sid_t handle = dir_handle(path, NULL);
		int16_t j = -1;
		TC_LOCK(&fs->entrylock);
		{
			for (uint32_t i = 0; i < buff_count(fs->entries); i++) {
				if (fs->entries[i].path == handle) {
					j = i;
					break;
				}
			}
		}
		TC_UNLOCK(&fs->entrylock);
		if (j < 0) {
			TRACE(LOG_WARNING,
				"[File]: Error, could not find directory for %s", path);
			TC_UNLOCK(&fs->mountlock);
			return rid;
		}
		int16_t c = register_asset(path, len, true);
		TC_LOCK(&fs->entrylock);
		{
			fs->entries[c].parent = j;
			rid = fs->entries[c].rid;
		}
		TC_UNLOCK(&fs->entrylock);
		uint32_t i = 0;
		for (i = 0; i < buff_count(mount->files); i++)
			if (mount->files[i].index == -1)
				break;
		if (i != buff_count(mount->files))
			mount->files[i].index = c;
		else buff_push(mount->files, fs->allocator,
			(internal_file_t) {0, c});
	}
	TC_UNLOCK(&fs->mountlock);
	return rid;
}

enum mounttype_t asset_get_path(tc_rid_t id, char** path, char** mount)
{
	TC_LOCK(&fs->mountlock);
	{
		for (mount_t* m = &buff_last(fs->mounts); m >= fs->mounts; m--) {
			internal_file_t* files = m->files;
			for (uint32_t i = 0; i < buff_count(files); i++) {
				int16_t idx = files[i].index;
				TC_LOCK(&fs->entrylock);
				sid_t sid = fs->entries[idx].path;
				tc_rid_t other = fs->entries[idx].rid;
				TC_UNLOCK(&fs->entrylock);
				if (id.handle == other.handle) {
					enum mounttype_t type = m->type;
					if (mount)
						*mount = path_get(m->path, NULL);
					if (path)
						*path = path_get(sid, NULL) + m->mount_len;
					TC_UNLOCK(&fs->mountlock);
					return type;
				}
			}
		}
	}
	TC_UNLOCK(&fs->mountlock);
	return -1;
}
/*
counter_id file_read(tc_stream_i* params)
{
	char temp[1024];
	tc_rid_t rid = find_asset(path, flags & FILE_CREATE);
	if (rid.handle != 0) {
		char* path;
		char* mount;
		enum mounttype_t type = asset_get_path(rid, &path, &mount);
		switch (type) {
		case MOUNT_DIRECTORY:
			strcpy(temp, mount);
			strcat(temp, "/");
			strcat(temp, path);
			return tc_os->file->open(&stream->handle, temp, flags);
		case MOUNT_ARCHIVE:
			spin_lock(&fs->entrylock);
			sid_t entry_path = fs->entries[index].path;
			spin_unlock(&fs->entrylock);
			//TODO: defer zip init
			if (zip_create(&stream->zip, path_get(entry.path, NULL), 0))
				return zip_open(&stream->zip, path_get(entry_path, NULL) + entry.mount_len + 1);
			return 0;
		}
	}
	return 0;
	switch (find_mount(file->file.mount, file->file.path).type) {
	case MOUNT_ARCHIVE:
		return zip_read(params);
	case MOUNT_DIRECTORY:
		return tc_os->file->read(params->file->handle, params->buf, params->size, -1);
	default: return 0;
	}
}

size_t file_size(file_t file)
{
	char temp[1024];
	mount_t entry = find_mount(file.mount, file.path);
	mountentry_t* mount = entry.mount;
	if (mount) {
		switch (entry.type) {
		case MOUNT_ARCHIVE:
			spin_lock(&mount->lock);
			size_t s = mount->files[file.index].size;
			spin_unlock(&mount->lock);
			return s;
		case MOUNT_DIRECTORY:
			spin_lock(&mount->lock);
			if (mount->files[file.index].counter != file.counter) {
				spin_unlock(&mount->lock);
				return 0;
			}
			_fs_get_path(temp, entry, mount->files[file.index].index);
			spin_unlock(&mount->lock);
			stat_t stat;
			await(tc_os->fs->stat(&stat, temp, NULL));
			spin_lock(&mount->lock);
			mount->files[file.index].size = stat.size;
			spin_unlock(&mount->lock);
			return stat.size;
		}
	}
	return 0;
}

counter_id file_delete(file_t file)
{
	char temp[1024];
	mount_t entry = find_mount(file.mount, file.path);
	mountentry_t* mount = entry.mount;
	if (mount && entry.type == MOUNT_DIRECTORY) {
		spin_lock(&mount->lock);
		if (mount->files[file.index].counter != file.counter) {
			spin_unlock(&mount->lock);
			return 0;
		}
		int16_t i = mount->files[file.index].index;
		mount->files[file.index].size = 0;
		mount->files[file.index].index = -1;
		mount->files[file.index].counter++;
		spin_unlock(&mount->lock);
		_fs_get_path(temp, entry, i);
		spin_lock(&fs->entrylock);
		remove_entry(i);
		spin_unlock(&fs->entrylock);
		return tc_os->fs->unlink(temp);
	}
	return 0;
}

uint32_t fs_file_count(const char* path)
{
	uint32_t count = 0;
	spin_lock(&fs->entrylock);
	{
		int16_t dir = find_entry(path);
		for (uint32_t i = 0; i < buff_count(fs->entries); i++)
			if (fs->entries[i].is_file && fs->entries[i].parent == dir)
				count++;
	}
	spin_unlock(&fs->entrylock);
	return count;
}

const char* fs_file_path(const char* path, uint32_t index)
{
	const char* r = NULL;
	spin_lock(&fs->entrylock);
	{
		int16_t dir = find_entry(path);
		uint32_t count = 0;
		for (uint32_t i = 0; i < buff_count(fs->entries); i++) {
			if (fs->entries[i].is_file && fs->entries[i].parent == dir &&
				count++ == index) {
				r = path_get(fs->entries[i].path, NULL);
				break;
			}
		}
	}
	spin_unlock(&fs->entrylock);
	return r;
}

const char* fs_file_name(const char* path, uint32_t index)
{
	const char* file_path = fs_file_path(path, index);
	if (file_path) {
		const char* name = strrchr(file_path, '/');
		if (!name) return file_path;
		return name + 1;
	}
	return NULL;
}

uint32_t fs_subdir_count(const char* path)
{
	uint32_t count = 0;
	spin_lock(&fs->entrylock);
	{
		int16_t dir = find_entry(path);
		for (uint32_t i = 0; i < buff_count(fs->entries); i++)
			if (!fs->entries[i].is_file && fs->entries[i].parent == dir)
				count++;
	}
	spin_unlock(&fs->entrylock);
	return count;
}

const char* fs_subdir_path(const char* path, uint32_t index)
{
	const char* r = NULL;
	spin_lock(&fs->entrylock);
	{
		int16_t dir = find_entry(path);
		uint32_t count = 0;
		for (uint32_t i = 0; i < buff_count(fs->entries); i++)
			if (!fs->entries[i].is_file && fs->entries[i].parent == dir &&
				count++ == index) {
				r = path_get(fs->entries[i].path, NULL);
				break;
			}
	}
	spin_unlock(&fs->entrylock);
	return r;
}

const char* fs_subdir_name(const char* path, uint32_t index)
{
	const char* subdir_path = fs_subdir_path(path, index);
	if (subdir_path) {
		const char* name = strrchr(subdir_path, '/');
		if (!name) return subdir_path;
		return name + 1;
	}
	return NULL;
}
*/
