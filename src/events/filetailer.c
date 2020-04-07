#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "main.h"

typedef struct file_status
{
	uv_fs_t *open_req; 
	uv_fs_t *read_req;
	uv_buf_t *buffer;
	void *parser_handler;
	const char *filename;
	int64_t offset;
} file_status;

void filetailer_on_read(uv_fs_t *req);

void on_open(uv_fs_t *req)
{
	puts("open");
	file_status *fstatus = req->data;
	printf("offset %"d64"\n", fstatus->offset);
	if (req->result != -1) {
		uv_fs_read(uv_default_loop(), fstatus->read_req, req->result, fstatus->buffer, 1, fstatus->offset, filetailer_on_read);
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
		//uv_fs_read(uv_default_loop(), read_req, open_req.result, buffer, 1, -1, filetailer_on_read);
	}
}

void filetailer_on_read(uv_fs_t *req) {
	file_status *fstatus = req->data;
	uv_fs_req_cleanup(req);

	if (req->result < 0) {
		fprintf(stderr, "Read error!\n");
	}
	else if (req->result == 0) {
	}
	else {
		//uv_fs_write(uv_default_loop(), &write_req, 1, fstatus->buffer, 1, -1, filetailer_on_write);
		//printf("(%zu) buf %s\n", strlen(fstatus->buffer->base), fstatus->buffer->base);
		size_t str_len = strlen(fstatus->buffer->base);
		fstatus->offset += str_len;
		alligator_multiparser(fstatus->buffer->base, str_len, fstatus->parser_handler, NULL, NULL);
	}
	uv_fs_t close_req;
	uv_fs_close(uv_default_loop(), &close_req, fstatus->open_req->result, NULL);
}


int on_openfile(file_status *fstatus)
{
	unsigned int len = 10000*sizeof(char);
	char *base = malloc(len);
	fstatus->buffer = malloc(sizeof(uv_buf_t));
	*(fstatus->buffer) = uv_buf_init(base, len);

	fstatus->read_req = malloc(sizeof(uv_fs_t));
	fstatus->open_req = malloc(sizeof(uv_fs_t));

	fstatus->read_req->data = fstatus;
	fstatus->open_req->data = fstatus;

	uv_fs_open(uv_default_loop(), fstatus->open_req, fstatus->filename, O_RDONLY, 0, on_open);
	//free(fstatus->buffer->base);
	//free(fstatus->buffer);
	//free(fstatus->read_req);
	//free(fstatus->open_req);
	//free(fstatus);
	return 0;
}

void on_file_change(uv_fs_event_t *handle, const char *filename, int events, int status)
{
	file_status *fstatus = handle->data;
	fstatus->filename = filename;
	printf("changing showing file '%s'\n", fstatus->filename);
	fprintf(stderr, "Change detected in %s: \n", filename);
	if (events == UV_RENAME)
		fprintf(stderr, "renamed");
	else if (events == UV_CHANGE)
	{
		fprintf(stderr, "changed");
		fprintf(stderr, " %s\n", filename ? filename : "");
		on_openfile(fstatus);
	}
}

void stat_cb(uv_fs_t *req) {
	//file_status *fstatus = req->data;
	printf("dcdc\n");
	//printf("%p\n", req->statfs);
	uv_stat_t st = req->statbuf;
	if (S_ISDIR(st.st_mode))
		puts("DIR!");
	else if (S_ISREG(st.st_mode))
		puts("FILE!");
	uv_fs_req_cleanup(req);
}

void filetailer_handler(char *file, void *parser_handler)
{
	printf("showing file '%s'\n", file);
	uv_fs_t* req_stat = malloc(sizeof(*req_stat));
	uv_fs_stat(uv_default_loop(), req_stat, file, stat_cb);

	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_fs_event_t *handle = calloc(1, sizeof(*handle));

	file_status *fstatus = malloc(sizeof(*fstatus));
	fstatus->parser_handler = parser_handler;

	fstatus->offset = 0;
	handle->data = fstatus;

	uv_fs_event_init(loop, handle);
	uv_fs_event_start(handle, on_file_change, file, UV_FS_EVENT_WATCH_ENTRY);
}
