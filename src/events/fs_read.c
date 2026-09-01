#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "dstructures/uv_cache.h"
#include "common/logs.h"
#include "main.h"
#define MAX_FILE_SIZE 1000000
#define FS_READ_WHOLE_CHUNK (256*1024)
extern aconf *ac;

typedef struct fs_read_info
{
	void (*callback)(char*, size_t, void*, char *);
	void *data;
	uv_fs_t *open_fd;
	char *filename;
	uv_buf_t buffer;
	char *base;
	uint64_t capacity;
	uint8_t whole;
	uint64_t size;
	uint64_t offset;
} fs_read_info;

void fs_read_on_read(uv_fs_t *req);

void fs_read_close(uv_fs_t* req)
{
	fs_read_info *frinfo  = req->data;

	if (frinfo->callback)
		frinfo->callback(frinfo->base, frinfo->size, frinfo->data, frinfo->filename);

	free(frinfo->base);
	alligator_cache_push(ac->uv_cache_fs, frinfo->open_fd);
	free(req);
	free(frinfo->filename);
	free(frinfo);
}

static void fs_read_finish(fs_read_info *frinfo)
{
	uv_fs_t *close_req = calloc(1, sizeof(*close_req));
	close_req->data = frinfo;
	uv_fs_close(uv_default_loop(), close_req, frinfo->open_fd->result, fs_read_close);
}

// Submits the next read. For whole-file mode the buffer grows on demand, so
// files larger than the initial chunk are read completely instead of being
// silently truncated.
static int8_t fs_read_submit(fs_read_info *frinfo)
{
	if (frinfo->whole && ((frinfo->size + 1) >= frinfo->capacity))
	{
		uint64_t newcapacity = frinfo->capacity * 2;
		char *newbase = realloc(frinfo->base, newcapacity);
		if (!newbase)
		{
			glog(L_ERROR, "fs_read: cannot grow buffer up to %"u64" bytes for '%s'\n", newcapacity, frinfo->filename);
			return 0;
		}

		frinfo->base = newbase;
		frinfo->capacity = newcapacity;
	}

	if ((frinfo->size + 1) >= frinfo->capacity)
		return 0;

	frinfo->buffer = uv_buf_init(frinfo->base + frinfo->size, (size_t)(frinfo->capacity - frinfo->size - 1));

	uv_fs_t *read_req = calloc(1, sizeof(*read_req));
	read_req->data = frinfo;
	uv_fs_read(uv_default_loop(), read_req, frinfo->open_fd->result, &frinfo->buffer, 1, frinfo->offset, fs_read_on_read);
	return 1;
}

void fs_read_on_read(uv_fs_t *req)
{
	fs_read_info *frinfo  = req->data;
	ssize_t result = req->result;
	uv_fs_req_cleanup(req);
	free(req);

	if (result < 0)
	{
		frinfo->callback = 0;
		glog(L_ERROR, "Read error: %s\n", frinfo->filename);
	}
	else if (result > 0)
	{
		size_t str_len = (size_t)result;
		if (str_len > frinfo->buffer.len)
			str_len = frinfo->buffer.len;

		frinfo->size += str_len;
		frinfo->offset += str_len;
		frinfo->base[frinfo->size] = 0;

		if (str_len == frinfo->buffer.len)
		{
			if (frinfo->whole)
			{
				if (fs_read_submit(frinfo))
					return;
			}
			else
				glog(L_ERROR, "fs_read: '%s' is bigger than %d bytes, content is truncated\n", frinfo->filename, MAX_FILE_SIZE);
		}
	}

	fs_read_finish(frinfo);
}

void fs_read_on_open(uv_fs_t *req)
{
	fs_read_info *frinfo  = req->data;

	if (req->result > -1)
	{
		glog(L_DEBUG, "fs_read: reading '%s' fd=%zd\n", frinfo->filename, req->result);
		uv_fs_req_cleanup(req);

		if (!fs_read_submit(frinfo))
			fs_read_finish(frinfo);

		return;
	}

	glog(L_ERROR, "Error opening file: %s\n", frinfo->filename);

	// cleanup before fs_read_close(): it returns open_fd (this very request)
	// back to the uv_fs cache
	uv_fs_req_cleanup(req);

	uv_fs_t *close_req = calloc(1, sizeof(*close_req));
	close_req->data = frinfo;
	fs_read_close(close_req);
}

static void fs_read_start(char *fname, uint64_t offset, void *callback, void *data, uint8_t whole)
{
	char *filename = strdup(fname);
	glog(L_DEBUG, "read_from_file: opening '%s'\n", filename);
	uv_fs_t *open_req = alligator_cache_get(ac->uv_cache_fs, sizeof(*open_req));
	fs_read_info *frinfo = calloc(1, sizeof(*frinfo));
	frinfo->callback = callback;
	frinfo->data = data;
	frinfo->offset = offset;
	frinfo->open_fd = open_req;
	frinfo->filename = filename;
	frinfo->whole = whole;
	frinfo->capacity = whole ? FS_READ_WHOLE_CHUNK : MAX_FILE_SIZE;
	open_req->data = frinfo;

	char *base = calloc(1, frinfo->capacity);
	if (!base) {
		free(filename);
		free(frinfo);
		alligator_cache_push(ac->uv_cache_fs, open_req);
		free(fname);
		return;
	}
	frinfo->base = base;
	frinfo->buffer = uv_buf_init(base, frinfo->capacity - 1);

	uv_fs_open(uv_default_loop(), open_req, filename, O_RDONLY, 0, fs_read_on_open);
	free(fname);
}

void read_from_file(char *fname, uint64_t offset, void *callback, void *data)
{
	fs_read_start(fname, offset, callback, data, 0);
}

// Reads the file up to EOF, growing the buffer as needed.
void read_whole_file(char *fname, void *callback, void *data)
{
	fs_read_start(fname, 0, callback, data, 1);
}
