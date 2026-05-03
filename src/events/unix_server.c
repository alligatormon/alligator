#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "common/entrypoint.h"
#include "parsers/multiparser.h"
#include "common/logs.h"
#include "events/uv_alloc.h"
#include "events/metrics.h"
#include "common/rtime.h"
#include "common/selector.h"
#include <main.h>
extern aconf* ac;

typedef struct unix_srv_data
{
	uv_stream_t* client;
	char *answ;
	context_arg *carg;
} unix_srv_data;

static void unix_only_free_context(uv_handle_t *handle)
{
	context_arg *cc = handle->data;
	free(cc);
}

static void unix_client_closed(uv_handle_t *handle)
{
	context_arg *cc = handle->data;
	context_arg *srv = cc->srv_carg;

	cc->close_time_finish = setrtime();

	if (srv)
	{
		(srv->close_counter)++;
		if (cc->parser_handler && !cc->parser_status)
			cc->parser_status = 1;
		aggregator_events_metric_add(srv, cc, NULL, "unix", "entrypoint", srv->key);
	}
	if (cc->full_body)
	{
		string_free(cc->full_body);
		cc->full_body = NULL;
	}
	free(cc);
}

void unix_write(uv_write_t* req, int status) {
	unix_srv_data *usdata = (unix_srv_data*) req->data;
	context_arg *cc = usdata->carg;

	if (status)
		carglog(cc, L_INFO, "async unix socket write %s %s\n", uv_err_name(status), uv_strerror(status));

	free(usdata->answ);
	free(usdata);
	free(req);

	cc->close_time = setrtime();
	cc->pclient.data = cc;
	uv_close((uv_handle_t*)&cc->pclient, unix_client_closed);
}

void unix_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
	context_arg *cc = client->data;
	context_arg *srv = cc->srv_carg;

	if (nread < 0) {
		if (buf && buf->base)
			free(buf->base);
		carglog(cc, L_ERROR, "read error: [%s: %s]\n", uv_err_name((int)nread), uv_strerror((int)nread));
		cc->close_time = setrtime();
		cc->pclient.data = cc;
		uv_close((uv_handle_t*)client, unix_client_closed);
		return;
	}
	if (nread == 0) {
		if (buf && buf->base)
			free(buf->base);
		return;
	}

	(srv->read_counter)++;
	srv->read_bytes_counter += (uint64_t)nread;
	if (!cc->no_metric)
		entrypoint_read_metrics_throttled_push(srv, cc, "unix", 1, srv->key);

	unix_srv_data *usdata = malloc(sizeof(*usdata));
	uv_write_t *req = (uv_write_t*) malloc(sizeof(uv_write_t));

	string *str = string_init(6553500);
	alligator_multiparser(buf->base, nread, NULL, str, cc);
	if (buf->base)
		free(buf->base);

	usdata->answ = str->s;
	size_t out_len = str->l;
	str->s = NULL;
	string_free(str);
	if (!usdata->answ)
	{
		usdata->answ = strdup("");
		out_len = 0;
	}

	req->data = usdata;
	usdata->client = client;
	usdata->carg = cc;

	uv_buf_t writebuf;
	writebuf.base = usdata->answ;
	writebuf.len = out_len;

	uv_write(req, client, &writebuf, 1, unix_write);
}

void unix_connect(uv_stream_t *server, int status) {
	int r;
	uv_loop_t *loop = ac->loop;
	context_arg *srv_carg = server->data;
	if (status) {
		carglog(srv_carg, L_ERROR, "connection error: [%s: %s]\n", uv_err_name((status)), uv_strerror((status)));
		return;
	}

	context_arg *cc = malloc(sizeof(*cc));
	memcpy(cc, srv_carg, sizeof(*cc));
	cc->srv_carg = srv_carg;
	cc->pipe = NULL;
	cc->full_body = NULL;
	cc->body = NULL;

	r = uv_pipe_init(loop, &cc->pclient, 0);
	if (r) {
		carglog(srv_carg, L_INFO, "initializing client pipe %s %s\n", uv_err_name(r), uv_strerror(r));
		free(cc);
		return;
	}
	cc->pclient.data = cc;

	r = uv_accept(server, (uv_stream_t*)&cc->pclient);
	if (r != 0) {
		cc->pclient.data = cc;
		uv_close((uv_handle_t*)&cc->pclient, unix_only_free_context);
		return;
	}

	(srv_carg->conn_counter)++;
	cc->connect_time = setrtime();

	if (uv_read_start((uv_stream_t*)&cc->pclient, alloc_buffer, unix_read))
	{
		(srv_carg->conn_counter)--;
		cc->pclient.data = cc;
		uv_close((uv_handle_t*)&cc->pclient, unix_only_free_context);
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
		carg_free(carg);
	}
}
