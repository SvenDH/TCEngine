#include "private_types.h"

#include "bstrlib.h"
#include <mz.h>
#include <mz_crypt.h>
#include <mz_os.h>
#include <mz_zip.h>
#include <mz_strm.h>

#define MEMORY_STREAM_GROW_SIZE 4096
#define STREAM_COPY_BUFFER_SIZE 4096
#define STREAM_FIND_BUFFER_SIZE 1024


typedef struct {
	vfs_t* fs;
	mount_t mount;
	char path[FS_MAX_PATH];
	bool bundled;
} resourcedirinfo_t;

static bool mtream_close(fstream_t* stream);
static size_t mstream_read(fstream_t* stream, void* buf, size_t len);
static size_t mstream_write(fstream_t* stream, const void* buf, size_t len);
static intptr_t mstream_size(const fstream_t* stream);
static bool mstream_flush(fstream_t* stream);

static bool fstream_open(vfs_t*, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out);
static bool fstream_close(fstream_t* stream);
static size_t fstream_read(fstream_t* stream, void* buf, size_t len);
static size_t fstream_write(fstream_t* stream, const void* buf, size_t len);
static intptr_t fstream_size(const fstream_t* stream);
static bool fstream_flush(fstream_t* stream);

static bool zstream_open(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out);
static bool zstream_close(fstream_t* stream);
static size_t zstream_read(fstream_t* stream, void* buf, size_t len);
static size_t zstream_write(fstream_t* stream, const void* buf, size_t len);
static intptr_t zstream_size(const fstream_t* stream);
static bool zstream_flush(fstream_t* stream);

vfs_t memfs = {
	.open = NULL,
	.close = mtream_close,
	.read = mstream_read,
	.write = mstream_write,
	.size = mstream_size,
	.flush = mstream_flush
};

vfs_t systemfs = {
	.open = fstream_open,
	.close = fstream_close,
	.read = fstream_read,
	.write = fstream_write,
	.size = fstream_size,
	.flush = fstream_flush
};

vfs_t zipfs = {
	.open = zstream_open,
	.close = zstream_close,
	.read = zstream_read,
	.write = zstream_write,
	.size = zstream_size,
	.flush = zstream_flush
};

static resourcedirinfo_t resdirs[RES_COUNT] = { 0 };

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

void fs_path_ext(const char* path, char* output)
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
	fs_path_ext(path, extension);
	const size_t extensionlen = extension[0] != 0 ? strlen(extension) + 1 : 0;
	const size_t outlen = pathlen - parentlen - extensionlen;
	strncpy(output, path + parentlen, outlen);
	output[outlen] = '\0';
}

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

const char* fs_get_resource_dir(resourcedir_t resourcedir)
{
	const resourcedirinfo_t* dir = &resdirs[resourcedir];
	if (!dir->fs) {
        if (!dir->path[0]) TRACE(LOG_ERROR, "Trying to get an unset resource directory '%d', make sure the resource directory is set on start of the application", resourcedir);
		TC_ASSERT(dir->path[0] != 0);
	}
	return dir->path;
}

void fs_set_resource_dir(vfs_t* io, mount_t mount, resourcedir_t resourcedir, const char* path)
{
	TC_ASSERT(io);
	resourcedirinfo_t* dir = &resdirs[resourcedir];
	if (dir->path[0] != 0) {
		TRACE(LOG_WARNING, "Resource directory {%d} already set on:'%s'", resourcedir, dir->path);
		return;
	}
	dir->mount = mount;
	if (M_CONTENT == mount) dir->bundled = true;
	strcpy(dir->path, path, FS_MAX_PATH);
	dir->fs = io;
	if (!dir->bundled && dir->path[0] != 0 && await(os_mkdir(dir->path)))
		TRACE(LOG_ERROR, "Could not create direcotry '%s' in filesystem", dir->path);
}


/************************************************************************/
/* 							Memory Stream								*/
/************************************************************************/

static inline size_t mstream_cap(fstream_t* stream, size_t reqsize)
{
	return min((intptr_t)reqsize, max((intptr_t)stream->size - (intptr_t)stream->mem.cursor, (intptr_t)0));
}

static bool mtream_close(fstream_t* stream)
{
	mstream_t* mem = &stream->mem;
	if (mem->owner) tc_free(mem->buffer);
	return true;
}

static size_t mstream_read(fstream_t* stream, void* buf, size_t len)
{
	if (!(stream->flags & FILE_READ)) {
		TRACE(LOG_WARNING, "Attempting to read from stream that doesn't have FM_READ flag.");
		return 0;
	}
	if ((intptr_t)stream->mem.cursor >= stream->size) return 0;
	size_t bytes = mstream_cap(stream, len);
	memcpy(buf, stream->mem.buffer + stream->mem.cursor, bytes);
	stream->mem.cursor += bytes;
	return bytes;
}

static size_t mstream_write(fstream_t* stream, const void* buf, size_t len)
{
	if (!(stream->flags & (FILE_WRITE | FILE_APPEND))) {
		TRACE(LOG_WARNING, "Attempting to write to stream that doesn't have FM_WRITE or FM_APPEND flags.");
		return 0;
	}
	if (stream->mem.cursor > (size_t)stream->size)
		TRACE(LOG_WARNING, "Creating discontinuity in initialized memory in memory stream.");
	size_t available = 0;
	if (stream->mem.capacity >= stream->mem.cursor)
		available = stream->mem.capacity - stream->mem.cursor;

	if (len > available) {
		size_t newcap = stream->mem.cursor + len;
		newcap = MEMORY_STREAM_GROW_SIZE *
			(newcap / MEMORY_STREAM_GROW_SIZE +
			(newcap % MEMORY_STREAM_GROW_SIZE == 0 ? 0 : 1));
		void* newbuf = tc_realloc(stream->mem.buffer, newcap);
		if (!newbuf) {
			TRACE(LOG_ERROR, "Failed to reallocate memory stream buffer with new capacity %lu.", (unsigned long)newcap);
			return 0;
		}
		stream->mem.buffer = (uint8_t*)newbuf;
		stream->mem.capacity = newcap;
	}
	memcpy(stream->mem.buffer + stream->mem.cursor, buf, len);
	stream->mem.cursor += len;
	stream->size = max(stream->size, (intptr_t)stream->mem.cursor);
	return len;
}

static intptr_t mstream_size(const fstream_t* stream) { return stream->size; }

static bool mstream_flush(fstream_t* stream) { return true; }

bool fs_open_mstream(const void* buffer, size_t bufsize, file_flags_t flags, bool owner, fstream_t* out)
{
	size_t size = buffer ? bufsize : 0;
	size_t capacity = bufsize;
	// Move cursor to the end for appending buffer
	size_t cursor = (flags & FILE_APPEND) ? size : 0;
	// For write and append streams we have to own the memory as we might need to resize it
	if ((flags & (FILE_WRITE | FILE_APPEND)) && (!owner || !buffer)) {
		// make capacity multiple of MEMORY_STREAM_GROW_SIZE
		capacity = MEMORY_STREAM_GROW_SIZE *
			(capacity / MEMORY_STREAM_GROW_SIZE +
			(capacity % MEMORY_STREAM_GROW_SIZE == 0 ? 0 : 1));
		void* newbuf = tc_malloc(capacity);
		TC_ASSERT(newbuf);
		if (buffer) memcpy(newbuf, buffer, size);
		buffer = newbuf;
		owner = true;
	}
	out->fs = &memfs;
	out->mem.buffer = (uint8_t*)buffer;
	out->mem.cursor = cursor;
	out->mem.capacity = capacity;
	out->mem.owner = owner;
	out->size = size;
	out->flags = flags;
	return true;
}


/************************************************************************/
/* 							File Stream									*/
/************************************************************************/

static bool fstream_open(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out)
{
	if (pwd) TRACE(LOG_WARNING, "System file streams do not support encrypted files");
	char path[FS_MAX_PATH] = { 0 };
	const char* dir_path = fs_get_resource_dir(dir);
	fs_path_join(dir_path, filename, path);
	fd_t fd = (fd_t)await(os_open(path, flags));
	if (fd == TC_INVALID_FILE) return false;
	out->fd = fd;
	out->flags = flags;
	out->fs = &systemfs;
	out->mount = dir;
	out->size = -1;
	stat_t stat;
	if (await(os_stat(&stat, path)) == 0)
		out->size = stat.size;
	return true;
}

static bool fstream_close(fstream_t* stream)
{
	if (await(os_close(stream->fd))) {
		TRACE(LOG_ERROR, "Error closing system file stream: %s", strerror(errno));
		return false;
	}
	return true;
}

static size_t fstream_read(fstream_t* stream, void* buf, size_t len)
{
	intptr_t bytes = await(os_read(stream->fd, buf, len, -1));
	if (bytes == EOF) TRACE(LOG_WARNING, "Error reading from system file stream: %s", strerror(errno));
	return (size_t)bytes;
}

static size_t fstream_write(fstream_t* stream, const void* buf, size_t len)
{
	if ((stream->flags & (FILE_WRITE | FILE_APPEND)) == 0) {
		TRACE(LOG_WARNING, "Writing to file stream with mode %u", stream->flags);
		return 0;
	}
	intptr_t bytes = await(os_write(stream->fd, buf, len, -1));
	if (bytes == EOF) TRACE(LOG_WARNING, "Error writing to system file stream: %s", strerror(errno));
	return bytes;
}

static intptr_t fstream_size(const fstream_t* stream) { return stream->size; }

static bool fstream_flush(fstream_t* stream)
{
	if (await(os_sync(stream->fd)) == EOF) {
		TRACE(LOG_WARNING, "Error flushing system file stream: %s", strerror(errno));
		return false;
	}
	return true;
}


/************************************************************************/
/* 							Zip Stream									*/
/************************************************************************/

#define DEFAULT_COMPRESSION_METHOD MZ_COMPRESS_METHOD_DEFLATE
#define MAX_PASSWORD_LENGTH 64

typedef struct {
	void* handle;
	fstream_t fstream;
	uint32_t opened;
	resourcedir_t dir;
	file_flags_t flags;
	char filename[FS_MAX_PATH];
	char pwd[MAX_PASSWORD_LENGTH];
} zipfile_t;

typedef struct {
	size_t writecount;
	char path[FS_MAX_PATH];
	char password[MAX_PASSWORD_LENGTH];
} zstream_t;

static bool force_open_zip(zipfile_t* zip, bool first)
{
	resourcedir_t dir = zip->dir;
	const char* filename = zip->filename;
	const char* password = zip->pwd;
	if (password[0] == '\0') password = NULL;
	file_flags_t mode = zip->flags;
	if (!first) mode |= FILE_READ;
	if (mode & FILE_APPEND) mode |= FILE_READWRITE;
	// Need to exclude append as we need the ability to freely move cursor for zip files
	if (!fs_open_fstream(dir, filename, mode & ~FILE_APPEND, password, &zip->fstream)) {
		TRACE(LOG_ERROR, "Failed to open zip file %s.", filename);
		return false;
	}
	if (!mz_zip_open(zip->handle, &zip->fstream, mode)) {
		TRACE(LOG_ERROR, "Failed to open zip handle to file %s.", filename);
		fs_close_stream(&zip->fstream);
		return false;
	}
	return true;
}

static bool force_close_zip(zipfile_t* zip)
{
	bool noerr = true;
	if (!mz_zip_close(zip->handle)) noerr = false;
	if (!fs_close_stream(&zip->fstream)) noerr = false;
	return noerr;
}

static bool cleanup_zip(zipfile_t* zip, bool result)
{
	mz_zip_delete(&zip->handle);
	tc_free(zip);
	return result;
}

bool fs_open_zip(vfs_t* fs)
{
	TC_ASSERT(fs && fs->userdata);
	if (zipfs.open != fs->open) return false;
	zipfile_t* zip = (zipfile_t*)fs->userdata;
	if (zip->opened == 0 && !force_open_zip(zip, false))
		return false;
	++zip->opened;
	return true;
}

bool fs_close_zip(vfs_t* fs)
{
	TC_ASSERT(fs && fs->userdata);
	if (zipfs.close != fs->close) return false;
	zipfile_t* zip = (zipfile_t*)fs->userdata;
	if (zip->opened == 0) {
		TRACE(LOG_ERROR, "Double close of zip file '%s'", zip->filename);
		return true;
	}
	--zip->opened;
	if (zip->opened == 0) return force_close_zip(zip);
	return true;
}

/***************************************************************
	Zip Entry
***************************************************************/

static bool zstream_open(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t mode, const char* pwd, fstream_t* out)
{
	TC_ASSERT(fs && out);
	if (mode & FILE_APPEND) mode |= FILE_WRITE;
	bool noerr = true;
	zipfile_t* zipfile = (zipfile_t*)fs->userdata;
	void* zip = zipfile->handle;
	if (!fs_open_zip(fs)) {		// make sure that zip file is opened
		TRACE(LOG_ERROR, "Failed to open zip file, while trying to access zip entry.");
		return false;
	}
	fstream_t* strm;
	mz_zip_get_stream(zip, &strm);
	file_flags_t zipmode = strm->flags;
	if ((mode & zipmode) != mode) {
		TRACE(LOG_WARNING, "Trying to open zip entry '%s' in file mode mode '%i', while zip file was opened in '%i' mode.", filename, mode, zipmode);
		fs_close_zip(fs);
		return false;
	}
	void* buffer = NULL;
	size_t len = 0;
	zstream_t* entry = tc_calloc(1, sizeof(zstream_t));
	if (!entry) {
		TRACE(LOG_ERROR, "Failed to allocate memory for file entry %s in zip", filename);
		fs_close_zip(fs);
		return false;
	}
	fs_path_join(fs_get_resource_dir(dir), filename, entry->path);
	if ((mode & FILE_READ) || (mode & FILE_APPEND)) {
		noerr = mz_zip_locate_entry(zip, entry->path, 0);
		if (noerr || !(mode & FILE_WRITE)) {	// Ignore error if write mode is enabled
			if (!noerr) {
				TRACE(LOG_WARNING, "Couldn't find file entry '%s' in zip.", entry->path);
				tc_free(entry);
				fs_close_zip(fs);
				return false;
			}
			noerr = mz_zip_entry_read_open(zip, 0, pwd);
			if (!noerr) {
				TRACE(LOG_ERROR, "Couldn't open file entry '%s' in zip.", entry->path);
				tc_free(entry);
				fs_close_zip(fs);
				return false;
			}
			mz_zip_file* info;
			noerr = mz_zip_entry_get_local_info(zip, &info);
			if (!noerr) {
				TRACE(LOG_ERROR, "Couldn't get info on file entry '%s' in zip.", entry->path);
				mz_zip_entry_close(zip);
				tc_free(entry);
				fs_close_zip(fs);
				return false;
			}
			len = info->uncompressed_size;
			if (len > 0) {
				buffer = tc_malloc(len);
				if (!buffer) {
					TRACE(LOG_ERROR, "Couldn't allocate buffer for reading zip file entry '%s'.", entry->path);
					mz_zip_entry_close(zip);
					tc_free(entry);
					fs_close_zip(fs);
					return false;
				}
				size_t read = mz_zip_entry_read(zip, buffer, (int32_t)len);
				if (read != len) {
					TRACE(LOG_ERROR, "Couldn't read zip file entry '%s'.", entry->path);
					tc_free(buffer);
					mz_zip_entry_close(zip);
					tc_free(entry);
					fs_close_zip(fs);
					return false;
				}
				mz_zip_entry_close(zip);
			}
			mz_zip_entry_close(zip);
		}
	}
	fstream_t* mstream = tc_malloc(sizeof(fstream_t));
	if (!mstream) {
		TRACE(LOG_ERROR, "Couldn't allocate memory for memory stream for zip file entry '%s'.", entry->path);
		tc_free(buffer);
		tc_free(entry);
		fs_close_zip(fs);
		return false;
	}
	if (!fs_open_mstream(buffer, len, mode, true, mstream)) {
		TRACE(LOG_ERROR, "Couldn't open memory stream for zip file entry '%s'.", entry->path);
		tc_free(buffer);
		tc_free(entry);
		fs_close_zip(fs);
		return false;
	}
	if ((mode & FILE_WRITE) && pwd) {				// We need to store password only if we are planning to write
		size_t pwdlen = strlen(pwd);
		if (pwdlen >= MAX_PASSWORD_LENGTH) {
			TRACE(LOG_ERROR, "Provided password for zip entry '%s' is too long.", filename);
			fs_close_zip(fs);
			return false;
		}
		strncpy(entry->password, pwd, pwdlen);
	}
	out->fs = fs;
	out->flags = mode;
	out->base = mstream;
	out->userdata = entry;
	return true;
}

static bool zstream_flush(fstream_t* stream)
{
	if (!(stream->flags & (FILE_WRITE | FILE_APPEND))) return true;
	bool noerr = true;
	fstream_t* mstream = stream->base;
	TC_ASSERT(mstream);
	zipfile_t* zipfile = (zipfile_t*)stream->fs->userdata;
	void* zip = zipfile->handle;
	zstream_t* entry = (zstream_t*)stream->userdata;
	const char* password = entry->password;
	if (password[0] == '\0') password = NULL;
	fstream_t* fstream;
	mz_zip_get_stream(zip, &fstream);
	mz_zip_file info = (mz_zip_file) { 0 };
	info.version_madeby = MZ_VERSION_MADEBY;
	info.compression_method = DEFAULT_COMPRESSION_METHOD;
	info.filename = entry->path;
	info.filename_size = (uint16_t)strlen(entry->path);
	info.uncompressed_size = fs_stream_size(mstream);
	if (password) info.aes_version = MZ_AES_VERSION;
	mz_zip_file* pinfo = &info;
	if (mz_zip_locate_entry(zip, entry->path, 0))
		mz_zip_entry_get_info(zip, &pinfo);
	
	noerr = mz_zip_entry_write_open(zip, pinfo, MZ_COMPRESS_LEVEL_DEFAULT, 0, password);
	if (!noerr) {
		TRACE(LOG_ERROR, "Failed to open file entry '%s' for write in zip.", entry->path);
		return false;
	}
	const void* buf = mstream->mem.buffer;
	size_t memlen = (size_t)fs_stream_size(mstream);
	if (memlen > 0) {
		noerr = mz_zip_entry_write(zip, buf, (int32_t)memlen) == memlen;
		if (!noerr) TRACE(LOG_WARNING, "Failed to write %ul bytes to zip file entry '%s'.", (unsigned long)memlen, entry->path);
	}
	if (!mz_zip_entry_close(zip)) {
		TRACE(LOG_WARNING, "Failed to close zip entry '%s'.", entry->path);
		noerr = false;
	}
	return noerr;
}

static bool zstream_close(fstream_t* stream)
{	
	TC_ASSERT(stream && stream->fs);
	vfs_t* fs = stream->fs;
	bool noerr = true;
	if (!fs_flush_stream(stream)) noerr = false;
	if (!fs_close_stream(stream->base)) noerr = false;
	tc_free(stream->base);
	tc_free(stream->userdata);
	if (!fs_close_zip(fs)) noerr = false;
	return noerr;
}

static size_t zstream_read(fstream_t* stream, void* buf, size_t len)
{
	return fs_read_stream(stream->base, buf, len);
}

static size_t zstream_write(fstream_t* stream, const void* buf, size_t len)
{
	return fs_write_stream(stream->base, buf, len);
}

static intptr_t zstream_size(const fstream_t* stream)
{
	return fs_stream_size(stream->base);
}

/***************************************************************
	Zip File System
***************************************************************/

bool init_zip_fs(const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, vfs_t* out)
{
	if (pwd && pwd[0] == '\0') pwd = NULL;
	if ((flags & FILE_READWRITE) == FILE_READWRITE || (flags & (FILE_READ | FILE_APPEND)) == (FILE_READ | FILE_APPEND)) {
		TRACE(LOG_WARNING, "Simultaneous read and write for zip files is error prone. Use with cautious.");
	}
	size_t filenamelen = strlen(filename);
	if (filenamelen >= FS_MAX_PATH) {
		TRACE(LOG_ERROR, "filename '%s' is too big.", filename);
		return false;
	}
	size_t pwdlen = 0;
	if (pwd) pwdlen = strlen(pwd);
	if (pwdlen >= MAX_PASSWORD_LENGTH) {
		TRACE(LOG_ERROR, "Password for file '%s' is too big.", filename);
		return false;
	}
	zipfile_t* zip = tc_calloc(1, sizeof(zipfile_t));
	mz_zip_create(&zip->handle);
	zip->opened = 0;
	zip->dir = dir;
	zip->flags = flags;
	memcpy(zip->filename, filename, filenamelen);
	if (pwd) memcpy(zip->pwd, pwd, pwdlen);
	if (!force_open_zip(zip, true)) {
		TRACE(LOG_ERROR, "Failed to open zip file '%s'", filename);
		return cleanup_zip(zip, false);
	}
	if (!force_close_zip(zip)) {				// Close everything and reopen when the entries are opened
		TRACE(LOG_ERROR, "Failed to close zip file '%s'", filename);
		return cleanup_zip(zip, false);
	}
	vfs_t system = zipfs;
	system.userdata = zip;
	*out = system;
	return true;
}

bool exit_zip_fs(vfs_t* fs)
{
	TC_ASSERT(fs && fs->userdata);
	if (fs->close != zipfs.close) return false;
	zipfile_t* zip = (zipfile_t*)fs->userdata;
	bool noerr = true;
	if (zip->opened != 0) {
		TRACE(LOG_WARNING, "Closing zip file '%s' when there are %u opened entries left.", zip->filename, zip->opened);
		zip->opened = 0;
		force_close_zip(zip);
	}
	return cleanup_zip(zip, noerr);
}


bool fs_is_fstream(fstream_t* stream) { return stream->fs == &systemfs; }

bool fs_is_mstream(fstream_t* stream) { return stream->fs == &memfs; }

bool fs_open_fstream(const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out)
{
	vfs_t* fs = resdirs[dir].fs;
	if (!fs) {
		TRACE(LOG_ERROR, "Trying to get an unset resource directory '%d', make sure the resource directory is set on start of the application", dir);
		return false;
	}
	return fs->open(fs, dir, filename, flags, pwd, out);
}

bool fs_close_stream(fstream_t* stream) { return stream->fs->close(stream); }

size_t fs_read_stream(fstream_t* stream, void* buf, size_t len) { return stream->fs->read(stream, buf, len); }

size_t fs_read_bstr_stream(fstream_t* stream, bstring str, size_t len)
{
	int bytes = 0;
	bassignStatic(str, "");
	if (len == SIZE_MAX) {	// Read until the end of the file
		do {				// read one page at a time
			balloc(str, str->slen + 512);
			bytes = (int)fs_read_stream(stream, str->data + str->slen, 512);
			TC_ASSERT(INT_MAX - str->slen > bytes && "Integer overflow");
			str->slen += bytes;
		} while (bytes == 512);
		balloc(str, str->slen + 1);
		str->data[str->slen] = '\0';
		return (size_t)str->slen;
	}
	TC_ASSERT(len < (size_t)INT_MAX);
	balloc(str, (int)len + 1);
	bytes = (int)fs_read_stream(stream, str->data, len);
	str->data[bytes] = '\0';
	str->slen = bytes;
	return bytes;
}

size_t fs_write_stream(fstream_t* stream, const void* buf, size_t len) { return stream->fs->write(stream, buf, len); }

bool fs_copy_stream(fstream_t* dst, fstream_t* src, size_t len)
{
	TC_ASSERT(dst && src);
    fstream_t* memstream = NULL;
    if (fs_is_mstream(dst)) memstream = dst;
    else if (dst->base && fs_is_mstream(dst->base)) memstream = dst->base;
    // Reallocate memory stream buffer to prevent realloc calls for each 4k write.
    if (memstream && memstream->mem.buffer && memstream->mem.capacity < len) {
        size_t cap = MEMORY_STREAM_GROW_SIZE *
            (len / MEMORY_STREAM_GROW_SIZE +
            (len % MEMORY_STREAM_GROW_SIZE == 0 ? 0 : 1));
        void* mem = tc_realloc(memstream->mem.buffer, cap);
        if (!mem) {
            TRACE(LOG_ERROR, "Failed to reallocate memory stream buffer with capacity %lu.", (unsigned long)len);
            return false;
        }
        memstream->mem.buffer = (uint8_t*)mem;
        memstream->mem.capacity = cap;
    }
	uint8_t buffer[STREAM_COPY_BUFFER_SIZE];	// L1 cache is usually from 8k to 64k, so 4k should fit into L1 cache
	size_t copied = 0;
	while (len > 0) {
		copied = min(len, sizeof(buffer));
		size_t read = fs_read_stream(src, buffer, copied);
		if (read <= 0) return false;
		size_t wrote = fs_write_stream(dst, buffer, copied);
		if (wrote != read) return false;
		len -= copied;
	}
	return true;
}

intptr_t fs_stream_size(const fstream_t* stream) { return stream->fs->size(stream); }

bool fs_flush_stream(fstream_t* stream) { return stream->fs->flush(stream); }
