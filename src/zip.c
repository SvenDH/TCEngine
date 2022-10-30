/*==========================================================*/
/*						ZIP FILES							*/
/*==========================================================*/

#include "zip.h"
#include "log.h"
#include "os.h"
#include "fiber.h"
#include "memory.h"
#include "stream.h"
#include "future.h"

#define MZ_CUSTOM_ALLOC
#define MZ_ALLOC mem_malloc

#define MZ_CUSTOM_FREE
#define MZ_FREE mem_free

#include <mz.h>
#include <mz_os.h>
#include <mz_zip.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_split.h>
#include <mz_strm_os.h>
#include <mz_zip_rw.h>

#define DEFAULT_COMPRESS_METHOD MZ_COMPRESS_METHOD_DEFLATE

/*
 * Minizip stream implementation using tc_os file operations
 */

typedef struct tc_zip_s {
	tc_stream_i;
	void* handle;
	const char* path;
	const char entry[260];
	uint8_t* buf;
	uint64_t size;
	tc_allocator_i* base;
} tc_zip_t;

typedef struct fs_stream_async_s {
	mz_stream stream;
	fd_t file;
	int32_t mode;
	int64_t position;
	int64_t size;
	int64_t error;
} fs_stream_async;

struct zip_proxy {	
	// Approximates minizip's reader and writer structs for hacky stuff
	void* zip_handle;
	void* file_stream;
	void* buffered_stream;
	void* split_stream;
};

static void* fs_stream_async_create(void** stream);

tc_fut_t* zip_close(tc_stream_i* stream);
tc_fut_t* zip_read(tc_stream_i* stream, uint8_t* buf, uint64_t size);
tc_fut_t* zip_write(tc_stream_i* stream, uint8_t* buf, uint64_t size);


tc_stream_i* zip_create(const char* path, int flags, tc_allocator_i* base)
{
	tc_zip_t* zip = tc_malloc(base, sizeof(tc_zip_t));
	zip->instance = zip;
	zip->read = zip_read;
	zip->write = zip_write;
	zip->close = zip_close;
	zip->base = base;
	// Mimics mz_zip_reader_open_file() with custom file stream
	mz_zip_reader_create(&zip->handle);

	struct zip_proxy* p = (struct zip_proxy*)zip->handle;
	p->file_stream = tc_malloc(base, sizeof(fs_stream_async));
	fs_stream_async_create(&p->file_stream);
	mz_stream_buffered_create(&p->buffered_stream);
	mz_stream_split_create(&p->split_stream);
	mz_stream_set_base(p->buffered_stream, p->file_stream);
	mz_stream_set_base(p->split_stream, p->buffered_stream);
	
	if (mz_stream_open(p->split_stream, path, MZ_OPEN_MODE_READ) != MZ_OK) {
		mz_zip_reader_delete(&zip->handle);
		TRACE(LOG_ERROR, "[Zip]: Could not open stream for %s", path);
		tc_free(base, zip, sizeof(tc_zip_t));
		return NULL;
	}
	if (mz_zip_reader_open(zip->handle, p->split_stream) != MZ_OK) {
		mz_zip_reader_delete(&zip->handle);
		TRACE(LOG_ERROR, "[Zip]: Could not open zip for %s", path);
		tc_free(base, zip, sizeof(tc_zip_t));
		return NULL;
	}
	zip->path = path;
	return zip;
}

bool zip_start_iter(tc_stream_i* stream)
{
	tc_zip_t* zip = stream->instance;
	int32_t err = mz_zip_reader_goto_first_entry(zip->handle);
	return err == MZ_OK;
}

bool zip_next_entry(tc_stream_i* stream, entry_info_t* entry)
{
	tc_zip_t* zip = stream->instance;
	mz_zip_file* file_info = NULL;
	if (mz_zip_reader_entry_get_info(zip->handle, &file_info) != MZ_OK) {
		TRACE(LOG_ERROR, "[Zip]: Failed to get zip entry info"); 
		return false;
	}
	strcpy(entry->path, file_info->filename);
	entry->size = file_info->uncompressed_size;
	return mz_zip_reader_goto_next_entry(zip->handle) == MZ_OK;
}

static int64_t zip_open_job(void* args)
{
	tc_zip_t* zip = (tc_zip_t*)args;
	if (mz_zip_reader_locate_entry(zip->handle, zip->entry, 0) == MZ_OK)
		if (mz_zip_reader_entry_open(zip->handle) == MZ_OK)
			return zip;
	TRACE(LOG_ERROR, "[Zip]: Could not open entry %s in zip %s", zip->entry, zip->path);
	return -1;
}

static int64_t zip_close_job(void* args)
{
	tc_zip_t* zip = (tc_zip_t*)args;
	int32_t err = mz_zip_reader_close(zip->handle);
	struct zip_proxy* p = (struct zip_proxy*)zip->handle;
	tc_free(zip->base, p->file_stream, sizeof(fs_stream_async));
	tc_free(zip->base, zip, sizeof(tc_zip_t));
	return err;
}

static int64_t zip_read_job(void* args) 
{
	tc_zip_t* zip = (tc_zip_t*)args;
	int32_t len = mz_zip_reader_entry_read(zip->handle, zip->buf, zip->size);
	if (len < 0)
		TRACE(LOG_WARNING, "[Zip]: Error reading zip %s", zip->path);
	return len;
}

static int64_t zip_write_job(void* args)
{
	TRACE(LOG_ERROR, "[Zip]: Writing zip not implemented.");
	return -1;
}

tc_fut_t* zip_open(tc_stream_i* stream, const char* entry)
{
	tc_zip_t* zip = stream->instance;
	strncpy(zip->entry, entry, sizeof(zip->entry));
	return tc_fiber->run_jobs(&(jobdecl_t) { zip_open_job, zip}, 1, NULL);
}

tc_fut_t* zip_close(tc_stream_i* stream)
{
	tc_zip_t* zip = stream->instance;
	return tc_fiber->run_jobs(&(jobdecl_t) { zip_close_job, zip }, 1, NULL);
}

tc_fut_t* zip_read(tc_stream_i* stream, uint8_t* buf, uint64_t size)
{
	tc_zip_t* zip = stream->instance;
	zip->buf = buf;
	zip->size = size;
	return tc_fiber->run_jobs(&(jobdecl_t) { zip_read_job, zip}, 1, NULL);
}

tc_fut_t* zip_write(tc_stream_i* stream, uint8_t* buf, uint64_t size)
{
	tc_zip_t* zip = stream->instance;
	zip->buf = buf;
	zip->size = size;
	return tc_fiber->run_jobs(&(jobdecl_t) { zip_write_job, zip }, 1, NULL);
}


static mz_stream_vtbl zip_stream_async_vtbl;

static int32_t fs_stream_async_open(void* stream, const char* path, int32_t mode) 
{
	if (path == NULL) return MZ_PARAM_ERROR;
	fs_stream_async* async = (fs_stream_async*)stream;
	/* Some use cases require write sharing as well */
	int flags = 0;
	if (mode & MZ_OPEN_MODE_READ) flags |= FILE_READ;
	if (mode & MZ_OPEN_MODE_WRITE) flags |= FILE_WRITE;
	if (mode & MZ_OPEN_MODE_APPEND) flags |= FILE_APPEND;
	if (mode & MZ_OPEN_MODE_CREATE) flags |= FILE_CREATE;

	await(tc_os->open(&async->file, path, flags));
	if (fs_stream_async_is_open(stream) != MZ_OK) {
		async->error = async->file.handle;
		return MZ_OPEN_ERROR;
	}
	async->mode = mode;
	stat_t stat;
	await(tc_os->stat(&stat, path));
	if (!stat.exists) return MZ_OPEN_ERROR;
	async->size = stat.size;
	if (mode & MZ_OPEN_MODE_APPEND) 
		return fs_stream_async_seek(stream, 0, MZ_SEEK_END);
	return MZ_OK;
}

static int32_t fs_stream_async_is_open(void* stream)
{
	fs_stream_async* async = (fs_stream_async*)stream;
	if (async->file.handle < 0) return MZ_OPEN_ERROR;
	return MZ_OK;
}

static int32_t fs_stream_async_read(void* stream, void* buf, int32_t size) 
{
	fs_stream_async* async = (fs_stream_async*)stream;
	int64_t read = await(tc_os->read(async->file, buf, size, async->position));
	if (read < 0) {
		async->error = read;
		return MZ_READ_ERROR;
	}
	async->position += read;
	return read;
}

static int32_t fs_stream_async_write(void* stream, const void* buf, int32_t size) 
{
	fs_stream_async* async = (fs_stream_async*)stream;
	int64_t written = await(tc_os->write(async->file, buf, size, async->position));
	if (written < 0) {
		async->error = written;
		return MZ_WRITE_ERROR;
	}
	async->position += written;
	if (async->position > async->size)
		async->size = async->position;
	return written;
}

static int64_t fs_stream_async_tell(void* stream) 
{
	return ((fs_stream_async*)stream)->position;
}

static int32_t fs_stream_async_seek(void* stream, int64_t offset, int32_t origin) 
{
	if (fs_stream_async_is_open(stream) != MZ_OK) return MZ_OPEN_ERROR;
	fs_stream_async* async = (fs_stream_async*)stream;
	int64_t new_pos = 0;
	int32_t err = MZ_OK;
	switch (origin) {
	case MZ_SEEK_CUR: new_pos = async->position + offset; break;
	case MZ_SEEK_END: new_pos = async->size + offset; break;
	case MZ_SEEK_SET: new_pos = offset; break;
	default: return MZ_SEEK_ERROR;
	}
	if (new_pos > async->size) {
		if ((async->mode & MZ_OPEN_MODE_CREATE) == 0) return MZ_SEEK_ERROR;
		async->size = new_pos;
	}
	else if (new_pos < 0) {
		return MZ_SEEK_ERROR;
	}
	async->position = new_pos;
	return MZ_OK;
}

static int32_t fs_stream_async_close(void* stream) 
{
	fs_stream_async* async = (fs_stream_async*)stream;
	int64_t r = await(tc_os->close(async->file));
	if (r < 0) {
		async->error = r;
		return MZ_CLOSE_ERROR;
	}
	return MZ_OK;
}

static int32_t fs_stream_async_error(void* stream)
{
	return ((fs_stream_async*)stream)->error;
}

static void* fs_stream_async_create(void** stream) 
{
	fs_stream_async* async = *(fs_stream_async**)stream;
	if (async != NULL) {
		memset(async, 0, sizeof(fs_stream_async));
		async->stream.vtbl = &zip_stream_async_vtbl;
		async->file.handle = TC_INVALID_FILE;
	}
	return async;
}

static mz_stream_vtbl zip_stream_async_vtbl = {
	fs_stream_async_open,
	fs_stream_async_is_open,
	fs_stream_async_read,
	fs_stream_async_write,
	fs_stream_async_tell,
	fs_stream_async_seek,
	fs_stream_async_close,
	fs_stream_async_error,
	fs_stream_async_create,
	NULL,
	NULL,
	NULL
};

tc_zip_i* tc_zip = &(tc_zip_i) {
	.create = zip_create,
	.iter = zip_start_iter,
	.next = zip_next_entry,
	.open = zip_open,
};

/*
static uint32_t zip_write_entry(zip_t* mount, entry_info_t* entry, void* buf) {
	// Write changed file into new archive
	mz_zip_file file_info = { 0 };
	file_info.filename = entry->path;
	file_info.modified_date = time(NULL);
	file_info.version_madeby = MZ_VERSION_MADEBY;
	file_info.compression_method = DEFAULT_COMPRESS_METHOD;
	file_info.uncompressed_size = entry->size;
	file_info.flag = MZ_ZIP_FLAG_UTF8;
	int32_t err = mz_zip_writer_entry_open(mount->handle, &file_info);
	if (err != MZ_OK) return err;
	err = mz_zip_writer_entry_write(mount->handle, buf, entry->size);
	if (err < 0) return err;
	err = mz_zip_writer_entry_close(mount->handle);
	if (err != MZ_OK) return err;
	return err;
}

static uint32_t zip_rewrite(zip_t* zip) {
	// Construct temporary zip name
	uint32_t len = strlen(zip->path);
	char tmp_path[256];
	strncpy(tmp_path, zip->path, len);
	tmp_path[len] = '\0';
	strcat(tmp_path, ".tmp");
	// Open temporary archive

	bool r = zip_init(zip_t * z, const char* path, bool writer);
	int32_t err = mz_stream_open(zip->w_splt_strm, tmp_path, MZ_OPEN_MODE_READWRITE | MZ_OPEN_MODE_CREATE);
	if (err != MZ_OK)
		return;
	err = mz_zip_writer_open(mount->writer, mount->w_splt_strm);
	if (err != MZ_OK)
		return;
	err = mz_zip_reader_goto_first_entry(mount->reader);
	if (err != MZ_OK && err != MZ_END_OF_LIST)
		return;
	uint32_t i = 0;
	while (err == MZ_OK) {
		if (i == f->zip_index && f->changed)
			err = zip_write_changes(f, mount, mount_len);
		else
			err = mz_zip_writer_copy_from_reader(mount->writer, mount->reader);
		if (err != MZ_OK) {
			TRACE(LOG_ERROR, "[File]: Error %i copying entry into new zip", err);
			break;
		}
		err = mz_zip_reader_goto_next_entry(mount->reader);
		if (err != MZ_OK && err != MZ_END_OF_LIST)
			TRACE(LOG_ERROR, "[File]: Error %i going to next entry in zip", err);
		i++;
	}
	if (f->zip_index == -1) {
		err = zip_write_changes(mount, mount_len);
		if (err == MZ_OK) f->zip_index = i;
		else TRACE(LOG_ERROR, "[File]: Error %i copying new entry into new zip", err);
	}
	buff_free(f->buf, f->size);

	uint8_t zip_cd = 0;
	mz_zip_reader_get_zip_cd(mount->reader, &zip_cd);
	mz_zip_writer_set_zip_cd(mount->writer, zip_cd);
	err = mz_zip_writer_close(mount->writer);
	if (err == MZ_OK) {
		char bak_path[256];
		// Swap original archive with temporary archive, backup old archive if possible
		strncpy(bak_path, path, len);
		bak_path[len] = 0;
		strcat(bak_path, ".bak");
		uv_stat_t stat;
		if (fs_stat(&stat, bak_path) == 0)
			fs_unlink(bak_path);
		if (fs_rename(path, bak_path) != 0)
			TRACE(LOG_WARNING, "[File]: Error %i backing up archive before replacing %s", err, bak_path);
		if (fs_rename(tmp_path, path) != 0)
			TRACE(LOG_WARNING, "[File]: Error %i replacing archive with temp %s", err, tmp_path);
		err = mz_zip_reader_close(mount->reader);
		if (err == MZ_OK) err = mz_zip_reader_open(mount->reader, mount->r_splt_strm);
		else TRACE(LOG_WARNING, "[File]: Error %i opening new zip %s", err, path);
	}
}
*/
