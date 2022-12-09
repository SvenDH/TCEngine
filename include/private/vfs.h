/*==========================================================*/
/*							FILES							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;
typedef struct tc_vfs_s tc_vfs_t;

/** Creates a new asset manager */
void tc_vfs_init(tc_vfs_t* fs, tc_allocator_i* a);

/** Destroy resources used by the asset manager */
void tc_vfs_free(tc_vfs_t* fs);

/** Mount a directory or archive to a virtual drive */
bool tc_vfs_mount(tc_vfs_t* fs, const char* path, char const* mount_as);

/** Dismounts the mounted drive */
void tc_vfs_dismount(tc_vfs_t* fs, const char* mount_as);

/** Get a asset handle */
tc_rid_t tc_vfs_find(tc_vfs_t* fs, const char* path, bool create);

/** Returns the file size */
size_t tc_vfs_size(tc_vfs_t* fs, tc_rid_t file);

/** Delete an asset */
bool tc_vfs_delete(tc_vfs_t* fs, tc_rid_t file);

/**
 * Get the files in a (virtual) directory at root.
 * Allocates an sbuf buffer with all child assets
 */
tc_rid_t* tc_vfs_iter(tc_vfs_t* fs, tc_rid_t root, tc_allocator_i* temp);

/** Get internal name of the asset */
const char* tc_vfs_name(tc_vfs_t* fs, tc_rid_t file);

/** Returns whether the asset is a directory (containing more assets) */
bool tc_vfs_is_dir(tc_vfs_t* fs, tc_rid_t file);


typedef struct tc_mount_s tc_mount_t;
typedef struct tc_entry_s tc_entry_t;

struct tc_vfs_s {
	tc_allocator_i* a;
	tc_strpool_t strings;
	tc_mount_t* mounts;
	tc_entry_t* entries;
};
