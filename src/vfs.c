#include "private_types.h"

#define MEMORY_STREAM_GROW_SIZE 4096
#define STREAM_COPY_BUFFER_SIZE 4096
#define STREAM_FIND_BUFFER_SIZE 1024


typedef struct {
	vfs_t* fs;      // NULL
	mount_t mount;  // M_CONTENT
	char path[FS_MAX_PATH];
	bool bundled;   // false
} resourcedirinfo_t;

static resourcedirinfo_t resdirs[RD_COUNT] = { 0 };

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

const char* tc_get_resource_dir(resourcedir_t resourcedir)
{
	const resourcedirinfo_t* dir = &resdirs[resourcedir];
	if (!dir->fs) {
        if (!dir->path[0]) TRACE(LOG_ERROR, "Trying to get an unset resource directory '%d', make sure the resource directory is set on start of the application", resourcedir);
		TC_ASSERT(dir->path[0] != 0);
	}
	return dir->path;
}

void tc_set_resource_dir(vfs_t* io, mount_t mount, resourcedir_t resourcedir, const char* path)
{
	TC_ASSERT(io);
	resourcedirinfo_t* dir = &resdirs[resourcedir];
	if (dir->mPath[0] != 0) {
		TRACE(LOG_WARNING, "Resource directory {%d} already set on:'%s'", resourcedir, dir->path);
		return;
	}
	dir->mount = mount;
	if (RM_CONTENT == mount) dir->bundled = true;
	char resourcepath[FS_MAX_PATH] = { 0 };
	fs_path_join(io->GetResourceMount ? io->GetResourceMount(mount) : "", path, resourcepath);
	strncpy(dir->mPath, resourcepath, FS_MAX_PATH);
	dir->fs = io;
	if (!dir->bundled && dir->path[0] != 0 && !fsCreateDirectory(resourcedir))
		TRACE(LOG_ERROR, "Could not create direcotry '%s' in filesystem", resourcepath);
}


/************************************************************************/
/* 							Memory Stream								*/
/************************************************************************/
static inline size_t mstream_cap(fstream_t* stream, size_t reqsize)
{
	return min((ssize_t)reqsize, max((ssize_t)stream->size - (ssize_t)stream->mem.cursor, (ssize_t)0));
}

static bool mtream_close(fstream_t* stream)
{
	mstream_t* mem = &stream->mem;
	if (mem->owner) tc_free(mem->buffer);
	return true;
}

static size_t mstream_read(fstream_t* stream, void* buf, size_t len)
{
	if (!(stream->flags & FM_READ)) {
		TRACE(LOG_WARNING, "Attempting to read from stream that doesn't have FM_READ flag.");
		return 0;
	}
	if ((ssize_t)stream->mem.cursor >= stream->size) return 0;
	size_t bytes = mstream_cap(stream, len);
	memcpy(buf, stream->mem.buffer + stream->mem.cursor, bytes);
	stream->mem.cursor += bytes;
	return bytes;
}

static size_t mstream_Write(fstream_t* stream, const void* buf, size_t len)
{
	if (!(stream->flags & (FM_WRITE | FM_APPEND))) {
		TRACE(LOG_WARNING, "Attempting to write to stream that doesn't have FM_WRITE or FM_APPEND flags.");
		return 0;
	}
	if (stream->mem.cursor > (size_t)stream->size)
		TRACE(LOG_WARNING, "Creating discontinuity in initialized memory in memory stream.");
	size_t available = 0;
	if (stream->mMemory.mCapacity >= stream->mem.cursor)
		available = stream->mMemory.mCapacity - stream->mem.cursor;

	if (len > available) {
		size_t newCapacity = stream->mem.cursor + len;
		newCapacity = MEMORY_STREAM_GROW_SIZE *
			(newCapacity / MEMORY_STREAM_GROW_SIZE +
			(newCapacity % MEMORY_STREAM_GROW_SIZE == 0 ? 0 : 1));
		void* newBuffer = tc_realloc(stream->mem.buffer, newCapacity);
		if (!newBuffer) {
			LOGF(eERROR, "Failed to reallocate memory stream buffer with new capacity %lu.", (unsigned long)newCapacity);
			return 0;
		}
		stream->mem.buffer = (uint8_t*)newBuffer;
		stream->mem.capacity = newCapacity;
	}
	memcpy(stream->mem.buffer + stream->mem.cursor, buf, len);
	stream->mem.cursor += len;
	stream->size = max(stream->size, (ssize_t)stream->mem.cursor);
	return len;
}


static bool mstream_Seek(fstream_t* stream, seek_t seek, ssize_t offset)
{
	switch (baseOffset)
	{
	case SBO_START_OF_FILE:
	{
		if (seekOffset < 0 || seekOffset > stream->size)
		{
			return false;
		}
		stream->mem.cursor = seekOffset;
	}
	break;
	case SBO_CURRENT_POSITION:
	{
		ssize_t newPosition = (ssize_t)stream->mem.cursor + seekOffset;
		if (newPosition < 0 || newPosition > stream->size)
		{
			return false;
		}
		stream->mem.cursor = (size_t)newPosition;
	}
	break;

	case SBO_END_OF_FILE:
	{
		ssize_t newPosition = (ssize_t)stream->size + seekOffset;
		if (newPosition < 0 || newPosition > stream->size)
		{
			return false;
		}
		stream->mem.cursor = (size_t)newPosition;
	}
	break;
	}
	return true;
}

static ssize_t mstream_GetSeekPosition(const fstream_t* stream) { return stream->mem.cursor; }

static ssize_t mstream_GetSize(const fstream_t* stream) { return stream->size; }

static bool mstream_Flush(fstream_t*) { return true; }

static bool mstream_IsAtEnd(const fstream_t* stream) { return (ssize_t)stream->mem.cursor == stream->size; }