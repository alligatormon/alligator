#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "dstructures/uv_cache.h"
#include "common/logs.h"
#include "main.h"
#define MAX_FILE_SIZE 1000000
extern aconf *ac;

typedef struct fs_read_info
{
	void (*callback)(char*, size_t, void*, char *);
	void *data;
	uv_fs_t *open_fd;
	char *filename;
	uv_buf_t buffer;
	uint64_t size;
	uint64_t offset;
} fs_read_info;

void fs_read_close(uv_fs_t* req)
{
	fs_read_info *frinfo  = req->data;

	if (frinfo->callback)
		frinfo->callback(frinfo->buffer.base, frinfo->size, frinfo->data, frinfo->filename);

	free(frinfo->buffer.base);
	alligator_cache_push(ac->uv_cache_fs, frinfo->open_fd);
	free(req);
	free(frinfo->filename);
	free(frinfo);
}

void fs_read_on_read(uv_fs_t *req)
{
	fs_read_info *frinfo  = req->data;
	uv_fs_req_cleanup(req);

	if ((req->result < 0))
	{
		frinfo->callback = 0;
		glog(L_ERROR, "Read error: %s\n", frinfo->filename);
	}
	else if (req->result == 0) {
	}
	else {
		size_t str_len = req->result;
		frinfo->buffer.base[str_len] = 0;
		frinfo->size = str_len-1;
		frinfo->offset += str_len;
	}
	uv_fs_t *close_req = malloc(sizeof(*close_req));
	close_req->data = frinfo;
	uv_fs_close(uv_default_loop(), close_req, frinfo->open_fd->result, fs_read_close);
	free(req);
}
void fs_read_on_open(uv_fs_t *req)
{
	fs_read_info *frinfo  = req->data;

	if (req->result > -1)
	{
		uv_fs_t *read_req = malloc(sizeof(*read_req));
		read_req->data = frinfo;

		glog(L_INFO, "fs_read_on_open: trying to file read '%s', result: %zd\n", frinfo->filename, req->result);
		uv_fs_read(uv_default_loop(), read_req, req->result, &frinfo->buffer, 1, frinfo->offset, fs_read_on_read);
	}
	else
	{
		glog(L_ERROR, "Error opening file: %s\n", frinfo->filename);
		uv_fs_t *close_req = malloc(sizeof(*close_req));
		close_req->data = frinfo;
		fs_read_close(close_req);
	}
	uv_fs_req_cleanup(req);
}

void read_from_file(char *fname, uint64_t offset, void *callback, void *data)
{
	char *filename = strdup(fname);
	glog(L_INFO, "read_from_file: trying to file open '%s' %p\n", filename, filename);
	uv_fs_t *open_req = alligator_cache_get(ac->uv_cache_fs, sizeof(*open_req));
	fs_read_info *frinfo = calloc(1, sizeof(*frinfo));
	frinfo->callback = callback;
	frinfo->data = data;
	frinfo->offset = offset;
	frinfo->open_fd = open_req;
	frinfo->filename = filename;
	open_req->data = frinfo;

	char *base = malloc(MAX_FILE_SIZE);
	*base = 0;
	frinfo->buffer = uv_buf_init(base, MAX_FILE_SIZE-1);

	uv_fs_open(uv_default_loop(), open_req, filename, O_RDONLY, 0, fs_read_on_open);
	free(fname);
}
