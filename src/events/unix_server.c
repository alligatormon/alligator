#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <uv.h>
#include <main.h>

typedef struct unix_srv_data
{
	uv_stream_t* client;
	char *answ;
} unix_srv_data;

void unix_write(uv_write_t* req, int status) {
	if (status)
		printf("async write %s %s\n", uv_err_name(status), uv_strerror(status));

	unix_srv_data *usdata = (unix_srv_data*) req->data;
	uv_close((uv_handle_t*)usdata->client, NULL);

	free(usdata->answ);
	free(usdata);
	free(req);
}

void unix_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
	if (nread < 0) {
		fprintf(stderr, "read error: [%s: %s]\n", uv_err_name((nread)), uv_strerror((nread)));
		uv_close((uv_handle_t*) client, NULL);
		return;
	}
	unix_srv_data *usdata = malloc(sizeof(*usdata));

	uv_write_t *req = (uv_write_t*) malloc(sizeof(uv_write_t));
	char *answ = malloc(65535);
	alligator_multiparser(buf->base, buf->len, NULL, answ, NULL);
	req->data = usdata;
	usdata->answ = answ;
	usdata->client = client;
	req->data = usdata;
	

	uv_buf_t writebuf;// = calloc(1, sizeof(uv_buf_t));
	writebuf.base = answ;
	writebuf.len = strlen(answ);

	uv_write(req, client, &writebuf, 1, unix_write);
}

void unix_connect(uv_stream_t *server, int status) {
	int r;
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	if (status) {
		fprintf(stderr, "connection error: [%s: %s]\n", uv_err_name((status)), uv_strerror((status)));
		return;
	}

	uv_pipe_t *client = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	r = uv_pipe_init(loop, client, 0);;
	if (r)
		printf("initializing client pipe %s %s\n", uv_err_name(r), uv_strerror(r));

	r = uv_accept(server, (uv_stream_t*) client);
	if (r == 0) {
		uv_read_start((uv_stream_t*) client, alloc_buffer, unix_read);
	} else {
		uv_close((uv_handle_t*) client, NULL);
	}
}

void unix_server_handler(char *file, void* handler)
{
	int r;

	extern aconf* ac;
	uv_loop_t *loop = ac->loop;

	uv_pipe_t *server = calloc(1, sizeof(*server));
	uv_pipe_init(loop, server, 0);

	unlink(file);
	r = uv_pipe_bind(server, file);
	if (r)
		printf("unix %s bind %s %s\n", file, uv_err_name(r), uv_strerror(r));

	r = uv_listen((uv_stream_t*)server, 128, unix_connect);
	if (r)
		printf("unix %s listen %s %s\n", file, uv_err_name(r), uv_strerror(r));

}
