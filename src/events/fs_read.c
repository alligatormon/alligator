#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

typedef struct fs_read_info
{
	void (*callback)(char*, size_t, void*, char *);
	void *data;
	uv_fs_t *open_fd;
	char *filename;
	uv_buf_t buffer;
	uint64_t offset;
} fs_read_info;

void fs_read_close(uv_fs_t* req)
{
	fs_read_info *frinfo  = req->data;

	if (frinfo->callback)
		frinfo->callback(frinfo->buffer.base, strlen(frinfo->buffer.base), frinfo->data, frinfo->filename);

	free(frinfo->buffer.base);
	free(frinfo->open_fd);
	free(req);
	free(frinfo);
}

void fs_read_on_read(uv_fs_t *req)
{
	fs_read_info *frinfo  = req->data;
	uv_fs_req_cleanup(req);

	if ((req->result < 0))
	{
		frinfo->callback = 0;
		fs_read_close(req);
		free(req);
		if (ac->log_level > 2)
			fprintf(stderr, "Read error: %s\n", frinfo->filename);
		return;
	}
	else if (req->result == 0) {
	}
	else {
		size_t str_len = strlen(frinfo->buffer.base);
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
	uv_fs_t *read_req = malloc(sizeof(*read_req));
	read_req->data = frinfo;


	if (req->result != -1)
	{
		if (ac->log_level > 2)
			printf("read_from_file: trying to file read '%s'\n", frinfo->filename);
		uv_fs_read(uv_default_loop(), read_req, req->result, &frinfo->buffer, 1, frinfo->offset, fs_read_on_read);
	}
	else if (ac->log_level > 2)
		fprintf(stdout, "Error opening file: %s\n", frinfo->filename);
}

void read_from_file(char *filename, uint64_t offset, void *callback, void *data)
{
	extern aconf *ac;

	uv_fs_t *open_req = malloc(sizeof(*open_req));
	fs_read_info *frinfo = malloc(sizeof(*frinfo));
	frinfo->callback = callback;
	frinfo->data = data;
	frinfo->offset = offset;
	frinfo->open_fd = open_req;
	frinfo->filename = filename;
	open_req->data = frinfo;

	char *base = malloc(1000000);
	frinfo->buffer = uv_buf_init(base, 1000000);

	if (ac->log_level > 2)
		printf("read_from_file: trying to file open '%s'\n", filename);

	uv_fs_open(uv_default_loop(), open_req, filename, O_RDONLY, 0, fs_read_on_open);
}
