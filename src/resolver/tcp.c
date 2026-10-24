#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "common/entrypoint.h"
#include "resolver/resolver.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "events/client.h"
#include "common/logs.h"
#include "main.h"
#include "common/stop.h"
#include "metric/percentile_heap.h"
#include "common/rtime.h"

static void resolver_closed_tcp(uv_handle_t *handle);

int resolver_tcp_halt(context_arg *carg)
{
	if (!carg)
		return 0;

	if (carg->tt_timer) {
		uv_timer_stop(carg->tt_timer);
		carg->tt_timer->data = NULL;
		alligator_timer_cache_push(carg->tt_timer);
		carg->tt_timer = NULL;
	}

	/* Return 1 when close_cb will run (issued now or already closing) so
	 * ephemeral release can wait instead of sync-freeing into a pending cb. */
	if (carg->client.loop && carg->client.type != UV_UNKNOWN_HANDLE) {
		carg->client.data = carg;
		if (!uv_is_closing((uv_handle_t *)&carg->client)) {
			carg->close_time = setrtime();
			uv_close((uv_handle_t *)&carg->client, resolver_closed_tcp);
		}
		return 1;
	}

	carg->lock = 0;
	return 0;
}

/* Ephemeral probes free the carg; permanent dns aggregates only close the
 * attempt's TCP handle so the repeating resolver_timer can retry. */
static void resolver_tcp_fail(context_arg *carg)
{
	if (!carg)
		return;
	if (carg->resolver_ephemeral)
		resolver_ephemeral_fail(carg);
	else {
		if (!carg->dns_status_reported)
			alligator_dns_resolution_status_set(carg,
				(carg->data && ((char *)carg->data)[0]) ? (char *)carg->data : carg->host,
				carg->rrtype ? carg->rrtype : "A", "IN", "error", 0);
		resolver_tcp_halt(carg);
	}
}

static void resolver_closed_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle ? handle->data : NULL;

	if (!carg)
		return;

	carglog(carg, L_DEBUG, "tcp-resolver: closed key=%s host=%s port=%s tls=%d\n", carg->key, carg->host, carg->port, carg->tls);
	(carg->close_counter)++;
	carg->close_time_finish = setrtime();

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	alligator_parser_ok_set(carg, carg->parsed, "tcp", carg->host);

	carg->lock = 0;
	string_null(carg->full_body);
	/* Length-prefix write owns this buffer; writed_tcp usually frees it, but
	 * close can race (or skip the write cb) — match tcp_client_closed. */
	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}
	/* Match resolver_udp_closed: inert handle so a later halt does not
	 * uv_close an already-closed client (libuv abort) or return 1 waiting
	 * for a close_cb that will never run (ephemeral stuck in ht). */
	memset(&carg->client, 0, sizeof(carg->client));
	if (carg->resolver_ephemeral_freeing)
		resolver_ephemeral_note_closed(carg);
}

void resolver_close_tcp(uv_handle_t *handle)
{
	context_arg* carg = handle->data;
	carglog(carg, L_DEBUG, "tcp-resolver: closing key=%s host=%s port=%s tls=%d\n", carg->key, carg->host, carg->port, carg->tls);

	/* Fully release the timeout timer: only nulling data left carg->tt_timer
	 * pointing at a handle that then fires and recycles, so teardown could
	 * stop/close/recycle it a second time. */
	if (carg->tt_timer) {
		uv_timer_t *timer = carg->tt_timer;
		carg->tt_timer = NULL;
		uv_timer_stop(timer);
		timer->data = NULL;
		alligator_timer_cache_push(timer);
	}

	carg->close_time = setrtime();
	if (!uv_is_closing((uv_handle_t*)&carg->client))
	{
		uv_close((uv_handle_t*)&carg->client, resolver_closed_tcp);
	}
}

void resolver_shutdown_tcp(uv_shutdown_t* req, int status)
{
	context_arg* carg = req->data;
	carglog(carg, L_DEBUG, "tcp-resolver: shut down key=%s host=%s port=%s tls=%d status=%d\n", carg->key, carg->host, carg->port, carg->tls, status);
	(carg->shutdown_counter)++;
	carg->shutdown_time_finish = setrtime();

	resolver_close_tcp((uv_handle_t *)&carg->client);
	free(req);
}

void resolver_timeout_tcp(uv_timer_t *timer)
{
	uv_timer_stop(timer);

	context_arg *carg = timer->data;

	/* Clear the owning carg's reference before recycling this handle into
	 * the shared timer cache, so later teardown cannot stop/close/recycle
	 * the same timer a second time and corrupt the loop timer heap. */
	if (carg && carg->tt_timer == timer)
		carg->tt_timer = NULL;
	timer->data = NULL;

	alligator_timer_cache_push(timer);

	if (!carg)
	{
		return;
	}

	carglog(carg, L_WARN, "tcp-resolver: timeout key=%s host=%s port=%s tls=%d timeout_ms=%"u64"\n", carg->key, carg->host, carg->port, carg->tls, carg->timeout);
	(carg->timeout_counter)++;

	if (carg->resolver_ephemeral)
		resolver_ephemeral_fail(carg);
	else
		resolver_tcp_halt(carg);
}

void resolver_read_tcp(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	context_arg* carg = (context_arg*)stream->data;

	carglog(carg, L_DEBUG, "tcp-resolver: read key=%s host=%s port=%s nread=%zd\n", carg->key, carg->host, carg->port, nread);
	
	(carg->read_counter)++;
	carg->read_time_finish = setrtime();

	if (nread > 0)
	{
		if (!carg->full_body->l)
		{
			dns_handler(buf->base+2, nread-2, carg);

			resolver_data *rd = carg->rd;
			if (rd)
			{
				double read_s = getrtime_mcs_seconds(carg->read_time, carg->read_time_finish);
				double write_s = getrtime_mcs_seconds(carg->write_time, carg->write_time_finish);
				double response_s = getrtime_mcs_seconds(carg->write_time, carg->read_time_finish);
				heap_insert(rd->read_time, read_s);
				heap_insert(rd->write_time, write_s);
				heap_insert(rd->response_time, response_s);
				calc_percentiles(carg, rd->read_time, NULL, "alligator_dns_read_duration_seconds", rd->labels);
				calc_percentiles(carg, rd->write_time, NULL, "alligator_dns_write_duration_seconds", rd->labels);
				calc_percentiles(carg, rd->response_time, NULL, "alligator_dns_response_duration_seconds", rd->labels);
			}

			/* A usable answer (>=1 A/AAAA RR) already restarted waiters;
			 * only an unusable one needs abandon, because a one-shot probe
			 * will never retry. Permanent must halt (close client + stop
			 * tt_timer); clearing only lock left a live handle that the next
			 * period memset/uv_tcp_init'd over (libuv abort). */
			if (carg->resolver_ephemeral) {
				if (carg->dns_usable)
					resolver_ephemeral_release(carg);
				else
					resolver_ephemeral_fail(carg);
			}
			else
				resolver_tcp_halt(carg);
		}
		else
			string_cat(carg->full_body, buf->base, nread);

	}
	else if (nread == 0)
		return;

	else if(nread == UV_EOF)
	{
		if (carg->resolver_ephemeral) {
			resolver_ephemeral_fail(carg);
			return;
		}
		uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
		if (!req) {
			resolver_close_tcp((uv_handle_t *)&carg->client);
			return;
		}
		req->data = carg;
		carg->shutdown_time = setrtime();
		if (uv_shutdown(req, (uv_stream_t*)&carg->client, resolver_shutdown_tcp))
			free(req);
		resolver_close_tcp((uv_handle_t *)&carg->client);
	}
	else if (nread == UV_ECONNRESET || nread == UV_ECONNABORTED) {
		resolver_tcp_fail(carg);
	}
	else if (nread == UV_ENOBUFS) {
		resolver_tcp_fail(carg);
	}
	else if (nread < 0) {
		carglog(carg, L_ERROR, "tcp-resolver: read error key=%s host=%s nread=%zd %s\n",
			carg->key, carg->host, nread, uv_strerror((int)nread));
		resolver_tcp_fail(carg);
	}
}

void resolver_writed_tcp(uv_write_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;

	if (!carg)
		return;

	/* Teardown owns close_cb; do not start a second uv_write on a closing handle
	 * (timeout/release can race with a length-write completion). Permanent
	 * aggregates use halt without resolver_ephemeral_freeing. */
	if (carg->resolver_ephemeral_freeing ||
	    (carg->client.loop && carg->client.type != UV_UNKNOWN_HANDLE &&
	     uv_is_closing((uv_handle_t *)&carg->client))) {
		if (carg->write_buffer.base) {
			free(carg->write_buffer.base);
			carg->write_buffer.base = NULL;
			carg->write_buffer.len = 0;
		}
		return;
	}

	if (status != 0) {
		carglog(carg, L_ERROR, "resolver length write error: %s\n", uv_strerror(status));
		if (carg->write_buffer.base) {
			free(carg->write_buffer.base);
			carg->write_buffer.base = NULL;
			carg->write_buffer.len = 0;
		}
		resolver_tcp_fail(carg);
		return;
	}

	if (carg->write_buffer.base) {
		free(carg->write_buffer.base);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
	}

	int write_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->request_buffer, 1, NULL);
	if (write_status) {
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(write_status));
		resolver_tcp_fail(carg);
		return;
	}

	carg->write_time_finish = setrtime();
	carg->read_time = setrtime();
}

void resolver_connected_tcp(uv_connect_t* req, int status)
{
	context_arg* carg = (context_arg*)req->data;

	/* Teardown owns close_cb; do not uv_read_start / uv_write on a closing handle
	 * (timeout/release can race with connect completion). Permanent aggregates
	 * use halt without resolver_ephemeral_freeing. */
	if (!carg || carg->resolver_ephemeral_freeing ||
	    (carg->client.loop && carg->client.type != UV_UNKNOWN_HANDLE &&
	     uv_is_closing((uv_handle_t *)&carg->client))) {
		if (carg && status < 0)
			alligator_session_connect_ok_set(carg, 0);
		return;
	}

	carglog(carg, L_DEBUG, "tcp-resolver: connected key=%s parser=%s host=%s port=%s tls=%d status=%d\n", carg->key, carg->parser_name, carg->host, carg->port, carg->tls, status);
	(carg->conn_counter)++;

	uint64_t ok = 1;
	if (status < 0)
	{
		ok = 0;
		alligator_session_connect_ok_set(carg, ok);
		/* Keep lock until close_cb; clearing it here allowed the repeating
		 * resolver_timer to uv_tcp_init over a still-open handle. */
		resolver_tcp_fail(carg);
		return;
	}

	alligator_session_connect_ok_set(carg, ok);

	carg->write_time = setrtime();

	memset(&carg->write_req, 0, sizeof(carg->write_req));

	carg->write_req.data = carg;

	int read_status = uv_read_start((uv_stream_t*)&carg->client, tcp_alloc, resolver_read_tcp);
	if (read_status) {
		carglog(carg, L_ERROR, "resolver_read_tcp error: %s\n", uv_strerror(read_status));
		resolver_tcp_fail(carg);
		return;
	}

	char *write_size_data = malloc(2);
	if (!write_size_data) {
		carglog(carg, L_ERROR, "tcp-resolver: OOM writing length prefix key=%s\n", carg->key);
		resolver_tcp_fail(carg);
		return;
	}
	write_size_data[0] = 0;
	write_size_data[1] = carg->request_buffer.len;
	carg->write_buffer = uv_buf_init(write_size_data, 2);
	int write_size_status = uv_write(&carg->write_req, (uv_stream_t*)&carg->client, &carg->write_buffer, 1, resolver_writed_tcp);
	if (write_size_status) {
		carglog(carg, L_ERROR, "uv_write error: %s\n", uv_strerror(write_size_status));
		free(write_size_data);
		carg->write_buffer.base = NULL;
		carg->write_buffer.len = 0;
		resolver_tcp_fail(carg);
		return;
	}

	carg->write_bytes_counter += carg->request_buffer.len;
	(carg->write_counter)++;
}

void resolver_connect_tcp(void *arg)
{
	context_arg *carg = arg;

	if (!carg || alligator_stop_requested()) {
		if (carg && carg->resolver_ephemeral)
			resolver_ephemeral_release(carg);
		return;
	}

	carg->count = 0;
	carglog(carg, L_DEBUG, "tcp-resolver: connecting key=%s host=%s port=%s tls=%d timeout_ms=%"u64"\n", carg->key, carg->host, carg->port, carg->tls, carg->timeout);

	/* In-progress attempt: wait for timeout/close_cb. */
	if (carg->lock && carg->tt_timer)
		return;

	/* Prior attempt still owns the TCP handle: never memset/re-init over it,
	 * even if lock was cleared early (permanent success used to do that). */
	if (carg->client.loop && carg->client.type != UV_UNKNOWN_HANDLE) {
		/* Ephemeral one-shot probes must fail (not bare halt) or they stay in
		 * resolver_ephemeral_ht with a timer that will not fire and strand
		 * oneshot clients waiting on the name. */
		if (carg->resolver_ephemeral)
			resolver_ephemeral_fail(carg);
		else if (!uv_is_closing((uv_handle_t *)&carg->client))
			resolver_tcp_halt(carg);
		return;
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->dns_status_reported = 0;

	resolver_init_client_timer(carg, resolver_timeout_tcp);

	memset(&carg->connect, 0, sizeof(carg->connect));
	carg->connect.data = carg;

	memset(&carg->client, 0, sizeof(carg->client));
	carg->client.data = carg;
	uv_tcp_init(carg->loop, &carg->client);

	char *addr = resolver_carg_get_addr(carg);
	if (!addr) {
		resolver_tcp_fail(carg);
		return;
	}

	uv_ip4_addr(addr, carg->numport, &carg->remote_addr);

	carg->connect_time = setrtime();
	int status = uv_tcp_connect(&carg->connect, &carg->client, (struct sockaddr *)&carg->remote_addr, resolver_connected_tcp);
	if (status) {
		carglog(carg, L_ERROR, "resolver_connect_tcp error: %s\n", uv_strerror(status));
		resolver_tcp_fail(carg);
	}
}
