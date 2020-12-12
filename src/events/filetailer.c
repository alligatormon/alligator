#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "main.h"

void filetailer_on_read(uv_fs_t *req);

void on_open(uv_fs_t *req)
{
	puts("open");
	context_arg *carg = req->data;
	printf("offset %"d64"\n", carg->offset);
	if (req->result != -1) {
		uv_fs_read(uv_default_loop(), carg->read, req->result, carg->buffer, 1, carg->offset, filetailer_on_read);
	}
	else {
		fprintf(stderr, "Error opening file!\n");
	}
}

void filetailer_on_write(uv_fs_t *req)
{
	uv_fs_req_cleanup(req);
	if (req->result < 0) {
		fprintf(stderr, "Write error!\n");
	}
	else {
		//uv_fs_read(uv_default_loop(), read, open.result, buffer, 1, -1, filetailer_on_read);
	}
}

void filetailer_on_read(uv_fs_t *req) {
	context_arg *carg = req->data;
	uv_fs_req_cleanup(req);

	if (req->result < 0) {
		fprintf(stderr, "Read error!\n");
	}
	else if (req->result == 0) {
	}
	else {
		//uv_fs_write(uv_default_loop(), &write_req, 1, carg->buffer, 1, -1, filetailer_on_write);
		printf("(%zu) buf %s\n", strlen(carg->buffer->base), carg->buffer->base);
		size_t str_len = strlen(carg->buffer->base);
		carg->offset += str_len;
		alligator_multiparser(carg->buffer->base, str_len, carg->parser_handler, NULL, carg);
	}
	uv_fs_t close_req;
	uv_fs_close(uv_default_loop(), &close_req, carg->open->result, NULL);
}


int on_openfile(context_arg *carg)
{
	unsigned int len = 10000*sizeof(char);
	char *base = malloc(len);
	carg->buffer = malloc(sizeof(uv_buf_t));
	*(carg->buffer) = uv_buf_init(base, len);

	carg->read = malloc(sizeof(uv_fs_t));
	carg->open = malloc(sizeof(uv_fs_t));

	carg->read->data = carg;
	carg->open->data = carg;

	snprintf(carg->pathname, 1023, "%s/%s", carg->path, carg->filename);
	uv_fs_open(uv_default_loop(), carg->open, carg->pathname, O_RDONLY, 0, on_open);
	//free(carg->buffer->base);
	//free(carg->buffer);
	//free(carg->read);
	//free(carg->open);
	//free(carg);
	return 0;
}

void on_file_change(uv_fs_event_t *handle, const char *filename, int events, int status)
{
	context_arg *carg = handle->data;
	carg->filename = filename;
	printf("changing showing file '%s'\n", carg->filename);
	fprintf(stderr, "Change detected in %s: \n", filename);
	if (events == UV_RENAME)
		fprintf(stderr, "renamed");
	else if (events == UV_CHANGE)
	{
		fprintf(stderr, "changed");
		fprintf(stderr, " %s\n", filename ? filename : "");
		on_openfile(carg);
	}
}

void stat_cb(uv_fs_t *req) {
	//context_arg *carg = req->data;
	printf("dcdc\n");
	//printf("%p\n", req->statfs);
	uv_stat_t st = req->statbuf;
	if (S_ISDIR(st.st_mode))
		puts("DIR!");
	else if (S_ISREG(st.st_mode))
		puts("FILE!");
	uv_fs_req_cleanup(req);
}

char* filetailer_handler(char *path, void *parser_handler)
{
	printf("showing file '%s'\n", path);
	uv_fs_t* req_stat = malloc(sizeof(*req_stat));
	uv_fs_stat(uv_default_loop(), req_stat, path, stat_cb);

	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_fs_event_t *handle = calloc(1, sizeof(*handle));

	context_arg *carg = malloc(sizeof(*carg));
	carg->parser_handler = parser_handler;

	carg->offset = 0;
	carg->path = path;
	handle->data = carg;

	uv_fs_event_init(loop, handle);
	uv_fs_event_start(handle, on_file_change, path, UV_FS_EVENT_WATCH_ENTRY);

	return "file";
}
