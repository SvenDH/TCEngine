#include "private_types.h"

#include "bstrlib.h"

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

vfs_t memfs = {
	.open = NULL,
	.close = mtream_close,
	.read = mstream_read,
	.write = mstream_write,
	.size = mstream_size,
	.flush = mstream_flush,
	.user = NULL
};

vfs_t systemfs = {
	.open = fstream_open,
	.close = fstream_close,
	.read = fstream_read,
	.write = fstream_write,
	.size = fstream_size,
	.flush = fstream_flush,
	.user = NULL
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
	if (!dir->bundled && dir->path[0] != 0 && await(tc_os->mkdir(dir->path)))
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


/************************************************************************/
/* 							File Stream									*/
/************************************************************************/
static bool fstream_open(vfs_t* fs, const resourcedir_t dir, const char* filename, file_flags_t flags, const char* pwd, fstream_t* out)
{
	if (pwd) TRACE(LOG_WARNING, "System file streams do not support encrypted files");
	char path[FS_MAX_PATH] = { 0 };
	const char* dir_path = fs_get_resource_dir(dir);
	fs_path_join(dir_path, filename, path);
	fd_t fd = (fd_t)await(tc_os->open(path, flags));
	if (fd == TC_INVALID_FILE) return false;
	out->fd = fd;
	out->flags = flags;
	out->fs = &systemfs;
	out->mount = dir;
	out->size = -1;
	stat_t stat;
	if (await(tc_os->stat(&stat, path)) == 0)
		out->size = stat.size;
	return true;
}

static bool fstream_close(fstream_t* stream)
{
	if (await(tc_os->close(stream->fd))) {
		TRACE(LOG_ERROR, "Error closing system file stream: %s", strerror(errno));
		return false;
	}
	return true;
}

static size_t fstream_read(fstream_t* stream, void* buf, size_t len)
{
	intptr_t bytes = await(tc_os->read(stream->fd, buf, len, -1));
	if (bytes == EOF) TRACE(LOG_WARNING, "Error reading from system file stream: %s", strerror(errno));
	return (size_t)bytes;
}

static size_t fstream_write(fstream_t* stream, const void* buf, size_t len)
{
	if ((stream->flags & (FILE_WRITE | FILE_APPEND)) == 0) {
		TRACE(LOG_WARNING, "Writing to file stream with mode %u", stream->flags);
		return 0;
	}
	intptr_t bytes = await(tc_os->write(stream->fd, buf, len, -1));
	if (bytes == EOF) TRACE(LOG_WARNING, "Error writing to system file stream: %s", strerror(errno));
	return bytes;
}

static intptr_t fstream_size(const fstream_t* stream) { return stream->size; }

static bool fstream_flush(fstream_t* stream)
{
	if (await(tc_os->sync(stream->fd)) == EOF) {
		TRACE(LOG_WARNING, "Error flushing system file stream: %s", strerror(errno));
		return false;
	}
	return true;
}

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
