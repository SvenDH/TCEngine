/*==========================================================*/
/*							FILES							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_allocator_i tc_allocator_i;


typedef struct tc_assets_i {
    /*
     * Mount a directory or archive to a virtual drive.
     */
    tc_rid_t(*mount)(const char* path, char const* mount_as);
    /*
     * Dismounts the mounted drive.
     */
    void (*dismount)(tc_rid_t mount);
    /*
     * Get a asset handle
     */
    tc_rid_t(*find)(const char* path, bool create);
    /*
     * Returns the file size
     */
    size_t(*size)(tc_rid_t asset);
    /*
     * Delete an asset
     */
    bool (*delete)(tc_rid_t asset);
    /*
     * Get the files in a (virtual) directory at root.
     * Allocates an sbuf buffer with all child assets
     */
    tc_rid_t* (*iter)(tc_rid_t root, tc_allocator_i* temp);
    /*
     * Get internal name of the asset
     */
    const char* (*get_name)(tc_rid_t asset);
    /*
     * Returns whether the asset is a directory (containing more assets)
     */
    bool (*is_directory)(tc_rid_t asset);

} tc_assets_i;


extern tc_assets_i* tc_assets;

/*
 * Creates a new asset manager
 */
void assets_init(tc_allocator_i* a);

/*
 * Destroy resources used by the asset manager.
 */
void assets_close(tc_allocator_i* a);