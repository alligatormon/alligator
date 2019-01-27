#include <uv.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
 
typedef struct fs_write_info
{
	uv_fs_t *open_req;
	uv_fs_t *exit_req;
	uv_fs_t *write_req;
	uv_fs_t *close_req;
	uv_buf_t buffer;
	char *write_data;
} fs_write_info;
 
void exit_cb(uv_fs_t* req) {
	fs_write_info *fs_info = req->data;
	printf("exit");
	free(fs_info->buffer.base);
	uv_fs_req_cleanup(fs_info->write_req);
	free(fs_info->open_req);
	free(fs_info->exit_req);
	free(fs_info->write_req);
	free(fs_info->close_req);
	//free(fs_info->write_data);
	free(fs_info);
}
 
void write_cb(uv_fs_t* req) {
	uv_loop_t* loop = uv_default_loop();
	fs_write_info *fs_info = req->data;
	int result = req->result; 
 
	if (result < 0) {
		printf("Error at writing file: %s\n", uv_strerror(result));
	}
	uv_fs_close(loop, fs_info->close_req, req->file, exit_cb);
}
 
void open_cb(uv_fs_t* req) {
	uv_loop_t* loop = uv_default_loop();
	fs_write_info *fs_info = req->data;
	int result = req->result;
 
	if (result < 0) {
		printf("Error at opening file: %s\n", uv_strerror((int)req->result));
	}
	printf("Successfully opened file.\n"); 
	uv_fs_req_cleanup(req);
	uv_fs_write(loop, fs_info->write_req, result, &fs_info->buffer, 1, 0, write_cb);
}

void write_to_file(char *filename, char *data, size_t len)
{
	//char *filename = "textfile.txt";
	//char *data = strdup("fevrc");
	//size_t len = 5;
	uv_loop_t* loop = uv_default_loop();
	int r;
 
	fs_write_info *fs_info = malloc(sizeof(*fs_info));
	fs_info->buffer = uv_buf_init(data, len);
	fs_info->buffer.len = len;
	fs_info->write_data = data;
	fs_info->open_req = malloc(sizeof(uv_fs_t));
	fs_info->exit_req = malloc(sizeof(uv_fs_t));
	fs_info->write_req = malloc(sizeof(uv_fs_t));
	fs_info->close_req = malloc(sizeof(uv_fs_t));
	fs_info->open_req->data = fs_info;
	fs_info->exit_req->data = fs_info;
	fs_info->write_req->data = fs_info;
	fs_info->close_req->data = fs_info;
 
	r = uv_fs_open(loop, fs_info->open_req, filename, O_RDWR | O_CREAT, 0777, open_cb);
	if (r < 0) {
		printf("Error at opening file: %s\n", uv_strerror(r));
	}
}
