#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "resolver/resolver.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "events/client.h"
#include "main.h"

void resolver_closed_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver client closed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, TTL: %"d64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->context_ttl);
	(carg->close_counter)++;
	carg->close_time_finish = setrtime();

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	metric_add_labels5("alligator_parser_status", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	carg->lock = 0;
	string_null(carg->full_body);
}

void resolver_close_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver client call close %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls);

	carg->tt_timer->data = NULL;

	carg->close_time = setrtime();
	if (!uv_is_closing((uv_handle_t*)&carg->client) && !carg->is_closing)
	{
		carg->is_closing = 1;
		uv_close((uv_handle_t*)&carg->client, resolver_closed_tcp);
	}
}

void resolver_shutdown_tcp(uv_shutdown_t* req, int status)
{
	context_arg* carg = req->data;
	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver client shutdowned %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	resolver_close_tcp((uv_handle_t *)&carg->client);
	free(req);
}

void resolver_timeout_tcp(uv_timer_t *timer)
{
	uv_timer_stop(timer);
	alligator_cache_push(ac->uv_cache_timer, timer);

	context_arg *carg = timer->data;
	if (!carg)
	{
		return;
	}

	if (carg->log_level > 1)
		printf("%"u64": timeout tcp-resolver client %p(%p:%p) with key %s, hostname %s, port: %s tls: %d, timeout: %"u64"\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	tcp_client_close((uv_handle_t *)&carg->client);
}

void resolver_read_tcp(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;

	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver client readed %p(%p:%p) with key %s, hostname %s, port: %s and tls: %d, nread size: %zd\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->host, carg->port, carg->tls, nread);
	
	(carg->read_counter)++;
	carg->read_time_finish = setrtime();

	if (nread > 0)
	{
		if (!carg->full_body->l)
		{
			uint64_t ttl = dns_handler(buf->base+2, nread-2, carg);
			if (carg->lock)
			{
				carg->lock = 0;
			}

			uv_timer_set_repeat(&carg->resolver_timer, ++ttl * 1000);
		}
		else
			string_cat(carg->full_body, buf->base, nread);

	}
	else if (nread == 0)
		return;

	else if(nread == UV_EOF)
	{
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		req->data = carg;
		carg->shutdown_time = setrtime();
		uv_shutdown(req, (uv_stream_t*)&carg->client, resolver_shutdown_tcp);
		resolver_close_tcp((uv_handle_t *)&carg->client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED)
		resolver_close_tcp((uv_handle_t *)&carg->client);
	else if (nread == UV_ENOBUFS)
		resolver_close_tcp((uv_handle_t *)&carg->client);
}

void resolver_writed_tcp(uv_write_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	int write_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->request_buffer, 1, NULL);
	if (write_status)
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(write_status));

}

void resolver_connected_tcp(uv_connect_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;
	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver connected %p(%p:%p) with key %s, parser name %s, hostname %s, port: %s tls: %d, status: %d\n", carg->count++, carg, &carg->connect, &carg->client, carg->key, carg->parser_name, carg->host, carg->port, carg->tls, status);
	(carg->conn_counter)++;

	uint64_t ok = 1;
	if (status < 0)
	{
		ok = 0;
		metric_add_labels5("alligator_connect_ok", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
		return;
	}

	metric_add_labels5("alligator_connect_ok", &ok, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);

	carg->read_time = setrtime();

	memset(&carg->write_req, 0, sizeof(carg->write_req));

	carg->write_req.data = carg;

	int read_status = uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, resolver_read_tcp);
	if (read_status && carg->log_level > 1)
		fprintf(stderr, "resolver_read_tcp error: %s\n", uv_strerror(read_status));

	char *write_size_data = malloc(2);
	write_size_data[0] = 0;
	write_size_data[1] = carg->request_buffer.len;
	carg->write_buffer = uv_buf_init(write_size_data, 2);
	int write_size_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, resolver_writed_tcp);
	if (write_size_status && carg->log_level > 1)
		fprintf(stderr, "uv_write error: %s\n", uv_strerror(write_size_status));

	carg->write_bytes_counter += carg->request_buffer.len;
	(carg->write_counter)++;
}

void resolver_connect_tcp(void *arg)
{
	context_arg *carg = arg;
	carg->count = 0;
	if (carg->log_level > 1)
		printf("%"u64": tcp-resolver connect %p(%p:%p) with key %s, hostname %s, port: %s, tls: %d, lock: %d, timeout: %"u64"\n", carg->count++, carg, &carg->client, &carg->connect, carg->key, carg->host, carg->port, carg->tls, carg->lock, carg->timeout);

	if (carg->lock)
		return;

	carg->lock = 1;
	carg->parsed = 0;
	carg->is_closing = 0;
	carg->curr_ttl = carg->ttl;

	resolver_init_client_timer(carg, resolver_timeout_tcp);

	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);

	char *addr = resolver_carg_get_addr(carg);
	if (!addr)
		return;

	struct sockaddr_in res;
	uv_ip4_addr(addr, carg->numport, &res);
	carg->dest = &res;

	carg->connect_time = setrtime();
	int status = uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)carg->dest, resolver_connected_tcp);
	if (status)
		if (carg->log_level > 1)
			fprintf(stderr, "resolver_connect_tcp error: %s\n", uv_strerror(status));
}
