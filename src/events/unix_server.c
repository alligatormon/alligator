#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <uv.h>
#include "common/entrypoint.h"
#include "parsers/multiparser.h"
#include "common/logs.h"
#include <main.h>
extern aconf* ac;

typedef struct unix_srv_data
{
	uv_stream_t* client;
	char *answ;
	context_arg *carg;
} unix_srv_data;

void unix_write(uv_write_t* req, int status) {
	unix_srv_data *usdata = (unix_srv_data*) req->data;
	uv_close((uv_handle_t*)usdata->client, NULL);

	if (status)
		carglog(usdata->carg, L_INFO, "async unix socket write %s %s\n", uv_err_name(status), uv_strerror(status));

	free(usdata->answ);
	free(usdata);
	free(req);
}

void unix_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
	if (nread < 0) {
		carglog(client->data, L_ERROR, "read error: [%s: %s]\n", uv_err_name((nread)), uv_strerror((nread)));
		uv_close((uv_handle_t*) client, NULL);
		return;
	}
	unix_srv_data *usdata = malloc(sizeof(*usdata));
	context_arg *carg = client->data;

	uv_write_t *req = (uv_write_t*) malloc(sizeof(uv_write_t));

	string *str = string_init(6553500);
	alligator_multiparser(buf->base, nread, NULL, str, carg);
	char *answ = str->s;
	size_t answ_len = str->l;

	req->data = usdata;
	usdata->answ = answ;
	usdata->client = client;
	usdata->carg = client->data;
	req->data = usdata;
	

	uv_buf_t writebuf;// = calloc(1, sizeof(uv_buf_t));
	writebuf.base = answ;
	writebuf.len = answ_len;

	uv_write(req, client, &writebuf, 1, unix_write);
}

void unix_connect(uv_stream_t *server, int status) {
	int r;
	uv_loop_t *loop = ac->loop;
	context_arg *carg = server->data;
	if (status) {
		carglog(carg, L_ERROR, "connection error: [%s: %s]\n", uv_err_name((status)), uv_strerror((status)));
		return;
	}

	uv_pipe_t *client = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	r = uv_pipe_init(loop, client, 0);;
	client->data = carg;
	if (r)
		carglog(carg, L_INFO, "initializing client pipe %s %s\n", uv_err_name(r), uv_strerror(r));

	r = uv_accept(server, (uv_stream_t*) client);
	if (r == 0) {
		uv_read_start((uv_stream_t*) client, alloc_buffer, unix_read);
	} else {
		uv_close((uv_handle_t*) client, NULL);
	}
}

void unix_server_init(uv_loop_t *loop, const char* file, context_arg *carg)
{
	int r;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "unix:%s", file);

	uv_pipe_t *server = carg->pipe = calloc(1, sizeof(*server));
	uv_pipe_init(loop, server, 0);
	server->data = carg;

	unlink(file);
	r = uv_pipe_bind(server, file);
	if (r)
		carglog(carg, L_INFO, "unix %s bind %s %s\n", file, uv_err_name(r), uv_strerror(r));

	r = uv_listen((uv_stream_t*)server, 128, unix_connect);
	if (r)
		carglog(carg, L_INFO, "unix %s listen %s %s\n", file, uv_err_name(r), uv_strerror(r));

	alligator_ht_insert(ac->entrypoints, &(carg->context_node), carg, tommy_strhash_u32(0, carg->key));
}

void unix_server_stop(const char* file)
{
	char key[255];
	snprintf(key, 255, "unix:%s", file);
	context_arg *carg = alligator_ht_search(ac->entrypoints, entrypoint_compare, key, tommy_strhash_u32(0, key));
	if (carg)
	{
		uv_close((uv_handle_t*)carg->pipe, NULL);
		unlink(file);
		alligator_ht_remove_existing(ac->entrypoints, &(carg->context_node));
	}
}
