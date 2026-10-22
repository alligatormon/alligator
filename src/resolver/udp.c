#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "common/entrypoint.h"
#include "common/stop.h"
#include "resolver/resolver.h"
#include "dstructures/uv_cache.h"
#include "events/metrics.h"
#include "events/uv_alloc.h"
#include "common/logs.h"
#include "main.h"
#include <arpa/inet.h>
#include "metric/percentile_heap.h"
#include "common/rtime.h"

#define RESOLVER_UDP_BIND_MAGIC 0x55444e53u /* 'UDNS' */

typedef struct resolver_udp_bind resolver_udp_bind;
void resolver_read_udp(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags);
static void resolver_udp_alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf);

typedef struct resolver_udp_pending {
	uint16_t txid;
	context_arg *carg;
	resolver_udp_bind *bind;
	alligator_ht_node node;
} resolver_udp_pending;

struct resolver_udp_bind {
	uint32_t magic;
	uv_udp_t udp;
	uv_loop_t *loop;
	uint16_t bind_port;
	char bind_ip[64];
	char key[96];
	alligator_ht *pending;
	uint8_t closing;
	tommy_node node;
};

static alligator_ht *resolver_udp_binds;
static resolver_udp_bind *resolver_udp_port_binds[65536];
static uint8_t resolver_udp_binds_stopping;

static void resolver_udp_bind_key(char *key, size_t keysz, const char *ip, uint16_t port)
{
	snprintf(key, keysz, "%s:%u", ip ? ip : "0.0.0.0", port);
}

/* Dedicated alloc: bind->udp.data is resolver_udp_bind, not context_arg. */
static void resolver_udp_alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
	(void)handle;
	buf->base = calloc(1, size);
	buf->len = buf->base ? size : 0;
}

static int resolver_udp_recv_start(uv_udp_t *udp)
{
	int recv_ret;

	if (!udp)
		return UV_EINVAL;
	recv_ret = uv_udp_recv_start(udp, resolver_udp_alloc, resolver_read_udp);
	if (recv_ret && recv_ret != UV_EALREADY)
		return recv_ret;
	return 0;
}

static void resolver_udp_stop_timer(context_arg *carg)
{
	uv_timer_t *timer;

	if (!carg || !carg->tt_timer)
		return;

	/* Clear ownership before recycle so timeout/halt/detach cannot push the
	 * same handle into uv_cache_timer twice (valgrind Invalid free on shutdown). */
	timer = carg->tt_timer;
	carg->tt_timer = NULL;
	uv_timer_stop(timer);
	timer->data = NULL;
	alligator_cache_push(ac->uv_cache_timer, timer);
}

static int resolver_udp_txid_compare(const void *arg, const void *obj)
{
	uint16_t s1 = *(const uint16_t *)arg;
	uint16_t s2 = ((const resolver_udp_pending *)obj)->txid;
	return s1 != s2;
}

static int resolver_udp_bind_compare(const void *arg, const void *obj)
{
	const char *s1 = arg;
	const char *s2 = ((const resolver_udp_bind *)obj)->key;
	return strcmp(s1, s2);
}

static int resolver_udp_qname_equal(const char *a, const char *b)
{
	size_t la;
	size_t lb;

	if (!a || !b)
		return 0;

	la = strlen(a);
	lb = strlen(b);
	if (la && a[la - 1] == '.')
		--la;
	if (lb && b[lb - 1] == '.')
		--lb;
	if (la != lb)
		return 0;
	return strncasecmp(a, b, la) == 0;
}

typedef struct resolver_udp_qname_ctx {
	const char *qname;
	context_arg *found;
	int matches;
} resolver_udp_qname_ctx;

static void resolver_udp_qname_foreach(void *funcarg, void *arg)
{
	resolver_udp_qname_ctx *ctx = funcarg;
	resolver_udp_pending *p = arg;
	const char *dname;

	if (!ctx || ctx->matches > 1 || !p || !p->carg)
		return;

	dname = p->carg->data;
	if (!dname || !resolver_udp_qname_equal(ctx->qname, dname))
		return;

	ctx->found = p->carg;
	ctx->matches++;
}

int resolver_udp_pending_add(alligator_ht *pending, context_arg *carg)
{
	resolver_udp_pending *p;

	if (!pending || !carg)
		return 0;

	resolver_udp_pending_del(pending, carg);

	p = calloc(1, sizeof(*p));
	if (!p)
		return 0;

	p->txid = carg->packets_id;
	p->carg = carg;
	alligator_ht_insert(pending, &p->node, p, tommy_inthash_u32(p->txid));
	carg->resolver_udp_pending = p;
	return 1;
}

void resolver_udp_pending_del(alligator_ht *pending, context_arg *carg)
{
	resolver_udp_pending *p;
	alligator_ht *ht = pending;

	if (!carg || !carg->resolver_udp_pending)
		return;

	p = carg->resolver_udp_pending;
	if (!ht && p->bind)
		ht = p->bind->pending;
	if (ht)
		alligator_ht_remove_existing(ht, &p->node);
	carg->resolver_udp_pending = NULL;
	free(p);
}

context_arg *resolver_udp_match_pending(alligator_ht *pending, const char *buf, size_t nread)
{
	uint16_t txid;
	resolver_udp_pending *p;
	dns_t resp;
	resolver_udp_qname_ctx ctx;

	if (!pending || !buf || nread < sizeof(dnshdr_t))
		return NULL;

	txid = ntohs(*(const uint16_t *)buf);
	p = alligator_ht_search(pending, resolver_udp_txid_compare, &txid, tommy_inthash_u32(txid));
	if (p && p->carg)
		return p->carg;

	memset(&resp, 0, sizeof(resp));
	if (dns_unpack((char *)buf, (int)nread, &resp) < 0)
		return NULL;

	ctx.qname = NULL;
	ctx.found = NULL;
	ctx.matches = 0;
	if (resp.hdr.nquestion && resp.questions && resp.questions[0].name[0]) {
		ctx.qname = resp.questions[0].name;
		alligator_ht_foreach_arg(pending, resolver_udp_qname_foreach, &ctx);
	}
	dns_free(&resp);

	if (ctx.matches == 1)
		return ctx.found;
	return NULL;
}

static void resolver_udp_pending_free_cb(void *funcarg, void *arg)
{
	resolver_udp_pending *p = arg;

	(void)funcarg;
	if (!p)
		return;
	/* Drop the carg back-reference so a later pending_del() does not chase
	 * p->bind (freed here) or remove from the destroyed table. */
	if (p->carg && p->carg->resolver_udp_pending == p)
		p->carg->resolver_udp_pending = NULL;
	free(p);
}

static void resolver_udp_bind_closed(uv_handle_t *handle)
{
	resolver_udp_bind *bind = handle->data;

	if (!bind)
		return;

	if (bind->pending) {
		alligator_ht_foreach_arg(bind->pending, resolver_udp_pending_free_cb, NULL);
		alligator_ht_done(bind->pending);
		free(bind->pending);
		bind->pending = NULL;
	}
	if (bind->bind_port && resolver_udp_port_binds[bind->bind_port] == bind)
		resolver_udp_port_binds[bind->bind_port] = NULL;
	free(bind);
}

static void resolver_udp_bind_close(resolver_udp_bind *bind)
{
	if (!bind || bind->closing)
		return;

	bind->closing = 1;
	if (!resolver_udp_binds_stopping && resolver_udp_binds)
		alligator_ht_remove_existing(resolver_udp_binds, &bind->node);

	if (!bind->udp.loop) {
		/* Never initialised: no close_cb will run. */
		resolver_udp_bind_closed((uv_handle_t *)&bind->udp);
	} else if (!uv_is_closing((uv_handle_t *)&bind->udp)) {
		uv_udp_recv_stop(&bind->udp);
		bind->udp.data = bind;
		uv_close((uv_handle_t *)&bind->udp, resolver_udp_bind_closed);
	}
	/* else: a close is already scheduled and its callback owns the bind. */
}

static void resolver_udp_binds_halt_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	resolver_udp_bind_close(arg);
}

void resolver_udp_binds_halt(void)
{
	memset(resolver_udp_port_binds, 0, sizeof(resolver_udp_port_binds));

	if (!resolver_udp_binds)
		return;

	resolver_udp_binds_stopping = 1;
	alligator_ht_foreach_arg(resolver_udp_binds, resolver_udp_binds_halt_foreach, NULL);
	alligator_ht_done(resolver_udp_binds);
	free(resolver_udp_binds);
	resolver_udp_binds = NULL;
	resolver_udp_binds_stopping = 0;
}

static resolver_udp_bind *resolver_udp_bind_lookup(const char *ip, uint16_t port, uv_loop_t *loop)
{
	char key[96];
	uint32_t key_hash;
	resolver_udp_bind *bind;

	if (!port)
		return NULL;

	bind = resolver_udp_port_binds[port];
	if (bind && !bind->closing && bind->loop == loop && !strcmp(bind->bind_ip, ip))
		return bind;

	if (!resolver_udp_binds)
		return NULL;

	resolver_udp_bind_key(key, sizeof(key), ip, port);
	key_hash = tommy_strhash_u32(0, key);
	bind = alligator_ht_search(resolver_udp_binds, resolver_udp_bind_compare, key, key_hash);
	if (bind && !bind->closing && bind->loop == loop)
		return bind;
	return NULL;
}

static void resolver_udp_bind_discard(resolver_udp_bind *bind)
{
	if (!bind)
		return;
	bind->closing = 1;
	if (!bind->udp.loop) {
		resolver_udp_bind_closed((uv_handle_t *)&bind->udp);
	} else if (!uv_is_closing((uv_handle_t *)&bind->udp)) {
		bind->udp.data = bind;
		uv_close((uv_handle_t *)&bind->udp, resolver_udp_bind_closed);
	}
	/* else: close already scheduled; the callback frees it. */
}

static resolver_udp_bind *resolver_udp_bind_get(context_arg *carg)
{
	const char *ip;
	char key[96];
	uint32_t key_hash;
	resolver_udp_bind *bind;
	resolver_udp_bind *existing;
	struct sockaddr_in *local = NULL;
	int bind_ret;
	int recv_ret;

	if (!carg || !carg->loop || !carg->bind_port)
		return NULL;

	ip = carg->bind_address ? carg->bind_address : "0.0.0.0";
	bind = resolver_udp_bind_lookup(ip, carg->bind_port, carg->loop);
	if (bind)
		return bind;

	if (!resolver_udp_binds)
		resolver_udp_binds = alligator_ht_init(NULL);

	resolver_udp_bind_key(key, sizeof(key), ip, carg->bind_port);
	key_hash = tommy_strhash_u32(0, key);

	bind = calloc(1, sizeof(*bind));
	if (!bind)
		return NULL;

	bind->magic = RESOLVER_UDP_BIND_MAGIC;
	strlcpy(bind->key, key, sizeof(bind->key));
	strlcpy(bind->bind_ip, ip, sizeof(bind->bind_ip));
	bind->bind_port = carg->bind_port;
	bind->loop = carg->loop;
	bind->pending = alligator_ht_init(NULL);
	uv_udp_init(carg->loop, &bind->udp);
	bind->udp.data = bind;

	if (!carg_set_socket_addr(&local, carg->bind_address, carg->bind_port)) {
		resolver_udp_bind_discard(bind);
		return NULL;
	}

	bind_ret = uv_udp_bind(&bind->udp, (const struct sockaddr *)local, 0);
	free(local);
	if (bind_ret) {
		existing = resolver_udp_bind_lookup(ip, carg->bind_port, carg->loop);
		resolver_udp_bind_discard(bind);
		if (existing) {
			carglog(carg, L_DEBUG, "udp-resolver: reuse shared bind %s:%u for key=%s\n", ip, carg->bind_port, carg->key);
			return existing;
		}
		carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", ip, carg->bind_port, uv_strerror(bind_ret));
		return NULL;
	}

	recv_ret = resolver_udp_recv_start(&bind->udp);
	if (recv_ret) {
		carglog(carg, L_ERROR, "udp-resolver: recv_start %s:%d error %s\n", ip, carg->bind_port, uv_strerror(recv_ret));
		resolver_udp_bind_discard(bind);
		return NULL;
	}

	alligator_ht_insert(resolver_udp_binds, &bind->node, bind, key_hash);
	resolver_udp_port_binds[carg->bind_port] = bind;
	carglog(carg, L_INFO, "udp-resolver: shared bind %s:%u for key=%s\n", ip, carg->bind_port, carg->key);
	return bind;
}

static void resolver_udp_query_release(context_arg *carg)
{
	if (!carg)
		return;

	resolver_udp_stop_timer(carg);
	resolver_udp_pending_del(NULL, carg);
	carg->lock = 0;
	/* Keep resolver_udp_shared while uv_udp_send is in flight so send_cb
	 * does not treat the shared bind as dedicated udp_client (would set
	 * handle->data = carg and may abort the shared socket). Cleared in
	 * resolver_send_udp once send_active drops. */
	if (!carg->resolver_udp_send_active)
		carg->resolver_udp_shared = 0;
}

static void resolver_udp_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;

	if (carg) {
		resolver_udp_stop_timer(carg);
		carg->lock = 0;
		carg->resolver_udp_send_active = 0;
		memset(&carg->udp_client, 0, sizeof(carg->udp_client));
		if (carg->resolver_ephemeral_freeing)
			resolver_ephemeral_note_closed(carg);
	}
}

/* Returns 1 if close_cb will run (uv_close issued now or already closing). */
static int resolver_udp_abort(context_arg *carg, uv_udp_t *udp)
{
	resolver_udp_stop_timer(carg);

	if (udp && udp->loop && udp->type != UV_UNKNOWN_HANDLE) {
		udp->data = carg;
		if (!uv_is_closing((uv_handle_t *)udp)) {
			uv_udp_recv_stop(udp);
			uv_close((uv_handle_t *)udp, resolver_udp_closed);
		}
		return 1;
	}
	if (carg)
		carg->lock = 0;
	return 0;
}

/* End of one DNS attempt. Ephemeral probes (hostname resolution for blackbox
 * and other clients) are freed; permanent dns aggregates keep their carg.
 * `shared` must be the snapshot taken when the attempt started: re-reading
 * resolver_udp_shared can have been cleared by a later attempt, which would
 * then abort the shared bind socket as if it were the dedicated udp_client. */
static void resolver_udp_done(context_arg *carg, uv_udp_t *udp, int shared)
{
	if (carg && carg->resolver_ephemeral) {
		resolver_ephemeral_fail(carg);
		return;
	}
	if (carg && !carg->dns_status_reported)
		alligator_dns_resolution_status_set(carg,
			(carg->data && ((char *)carg->data)[0]) ? (char *)carg->data : carg->host,
			carg->rrtype ? carg->rrtype : "A", "IN", "error", 0);
	if (carg && shared)
		resolver_udp_query_release(carg);
	else
		resolver_udp_abort(carg, udp);
}

int resolver_udp_halt(context_arg *carg)
{
	if (!carg)
		return 0;

	if (carg->resolver_udp_shared) {
		resolver_udp_query_release(carg);
		return 0;
	}

	return resolver_udp_abort(carg, &carg->udp_client);
}

void resolver_timeout_udp(uv_timer_t *timer)
{
	context_arg *carg = timer->data;

	uv_timer_stop(timer);

	/* Same ownership rule as resolver_timeout_tcp / tcp_timeout_timer: drop
	 * carg->tt_timer before cache push so abort/teardown cannot recycle twice. */
	if (carg && carg->tt_timer == timer)
		carg->tt_timer = NULL;
	timer->data = NULL;
	alligator_cache_push(ac->uv_cache_timer, timer);

	if (!carg)
		return;

	carglog(carg, L_WARN, "udp-resolver: timeout key=%s host=%s timeout_ms=%"u64"\n", carg->key, carg->host, carg->timeout);
	(carg->timeout_counter)++;
	resolver_udp_done(carg, &carg->udp_client, carg->resolver_udp_shared);
}

static void resolver_udp_apply_reply(context_arg *carg, ssize_t nread, const uv_buf_t *buf)
{
	resolver_data *rd = carg->rd;
	double read_time;
	double write_time;
	double response_time;

	carglog(carg, L_DEBUG, "udp-resolver: read key=%s host=%s tls=%d nread=%zd\n", carg->key, carg->host, carg->tls, nread);

	carg->read_time_finish = setrtime();
	read_time = getrtime_mcs_seconds(carg->read_time, carg->read_time_finish);
	write_time = getrtime_mcs_seconds(carg->write_time, carg->write_time_finish);
	response_time = getrtime_mcs_seconds(carg->write_time, carg->read_time_finish);
	carg->close_time = setrtime();
	carg->close_time_finish = setrtime();

	if (rd)
	{
		heap_insert(rd->read_time, read_time);
		heap_insert(rd->write_time, write_time);
		heap_insert(rd->response_time, response_time);
		calc_percentiles(carg, rd->read_time, NULL, "alligator_dns_read_duration_seconds", rd->labels);
		calc_percentiles(carg, rd->write_time, NULL, "alligator_dns_write_duration_seconds", rd->labels);
		calc_percentiles(carg, rd->response_time, NULL, "alligator_dns_response_duration_seconds", rd->labels);
	}

	(carg->conn_counter)++;
	(carg->read_counter)++;
	carg->read_bytes_counter += nread;

	metric_add_labels("udp_entrypoint_read_total", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	dns_handler(buf->base, nread, carg);

	aggregator_events_metric_add(carg, carg, NULL, "tcp", "aggregator", carg->host);
	alligator_parser_ok_set(carg, carg->parsed, "tcp", carg->host);
}

void resolver_read_udp(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	context_arg *carg = NULL;
	resolver_udp_bind *bind = NULL;
	int shared = 0;

	(void)addr;
	(void)flags;

	if (req && req->data) {
		resolver_udp_bind *maybe = req->data;
		if (maybe->magic == RESOLVER_UDP_BIND_MAGIC && req == &maybe->udp) {
			bind = maybe;
			shared = 1;
		} else {
			carg = req->data;
		}
	}

	if (nread < 0)
	{
		if (carg)
			carglog(carg, L_ERROR, "Read error %s\n", uv_err_name(nread));
		else
			glog(L_ERROR, "udp-resolver: shared read error %s\n", uv_err_name(nread));
		if (buf && buf->base)
			free(buf->base);
		if (!shared && carg)
			resolver_udp_done(carg, req, 0);
		return;
	}
	if (nread == 0)
	{
		/* libuv reports EAGAIN as nread=0, addr=NULL. Do not tear down
		 * the socket; recv is started before send so this is expected. */
		if (buf && buf->base)
			free(buf->base);
		return;
	}

	if (shared) {
		carg = resolver_udp_match_pending(bind->pending, buf->base, (size_t)nread);
		if (!carg) {
			glog(L_WARN, "udp-resolver: shared bind %s:%u unmatched reply nread=%zd\n",
				bind->bind_ip, bind->bind_port, nread);
			free(buf->base);
			return;
		}
	}

	if (!carg) {
		if (buf && buf->base)
			free(buf->base);
		return;
	}

	resolver_udp_apply_reply(carg, nread, buf);

	if ((shared || carg->lock || carg->resolver_ephemeral) && carg->local_addr) {
		free(carg->local_addr);
		carg->local_addr = NULL;
	}
	/* A usable reply (>=1 A/AAAA RR) already restarted waiters; only an
	 * unusable one (NXDOMAIN / SERVFAIL / empty / malformed) needs the
	 * abandon path, because a one-shot probe will never retry. */
	if (carg->resolver_ephemeral) {
		if (carg->dns_usable)
			resolver_ephemeral_release(carg);
		else
			resolver_ephemeral_fail(carg);
	}
	else if (shared)
		resolver_udp_query_release(carg);
	else if (carg->lock)
		resolver_udp_abort(carg, req);

	free(buf->base);
}

void resolver_send_udp(uv_udp_send_t* req, int status) {
	context_arg *carg = req->data;
	uint8_t shared;

	if (!carg)
		return;

	shared = carg->resolver_udp_shared;
	carg->resolver_udp_send_active = 0;

	/* Ephemeral release on a shared bind waits for this cb (no per-carg udp close). */
	if (carg->resolver_ephemeral_wait_send) {
		carg->resolver_ephemeral_wait_send = 0;
		if (shared && !carg->lock)
			carg->resolver_udp_shared = 0;
		resolver_ephemeral_note_closed(carg);
		return;
	}
	/* Dedicated udp_client is being closed; close_cb owns note_closed. */
	if (carg->resolver_ephemeral_freeing)
		return;

	carglog(carg, L_DEBUG, "udp-resolver: sent key=%s host=%s tls=%d\n", carg->key, carg->host, carg->tls);
	carg->write_time_finish = setrtime();

	if (status != 0) {
		carglog(carg, L_ERROR, "send_cb error: %s\n", uv_strerror(status));
		resolver_udp_done(carg, req->handle, shared);
		return;
	}
	/* Read window starts once the send completed, including the deferred-clear
	 * path (a stale read_time would inflate the next response metric). */
	carg->read_time = setrtime();

	/* Snapshot shared at send completion: query_release may have cleared lock
	 * while send was active but left shared set until here. Never recv_start /
	 * abort the shared bind as if it were carg->udp_client. */
	if (!shared) {
		req->handle->data = req->data;
		if (resolver_udp_recv_start(req->handle)) {
			resolver_udp_done(carg, req->handle, 0);
			return;
		}
	} else if (!carg->lock) {
		/* Deferred clear from query_release while send was in flight. */
		carg->resolver_udp_shared = 0;
		return;
	}
}

static int resolver_udp_send_query(context_arg *carg, uv_udp_t *udp)
{
	int status;

	carg->udp_send.data = carg;
	status = uv_udp_send(&carg->udp_send, udp, &carg->request_buffer, 1, (struct sockaddr *)&carg->remote_addr, resolver_send_udp);
	if (status) {
		carglog(carg, L_ERROR, "uv_udp_send error: %s\n", uv_strerror(status));
		carg->resolver_udp_send_active = 0;
		return status;
	}

	carg->resolver_udp_send_active = 1;
	carg->write_bytes_counter += carg->request_buffer.len;
	(carg->write_counter)++;
	carg->write_time = setrtime();
	carg->connect_time = setrtime();
	carg->connect_time_finish = setrtime();
	return 0;
}

void resolver_connect_udp(void *arg)
{
	context_arg *carg = arg;

	if (!carg)
		return;
	if (!carg->loop || alligator_stop_requested()) {
		if (carg->resolver_ephemeral)
			resolver_ephemeral_release(carg);
		return;
	}

	/* A uv_udp_send on the embedded carg->udp_send is still owned by libuv.
	 * Re-initialising the handle or clearing resolver_udp_shared here would
	 * make the pending send_cb treat a shared bind as the dedicated
	 * udp_client. Retry on the next period; an ephemeral probe has no next
	 * period, so fail it explicitly instead of orphaning it. */
	if (carg->resolver_udp_send_active) {
		if (carg->resolver_ephemeral)
			resolver_ephemeral_fail(carg);
		return;
	}

	carg->count = 0;
	carglog(carg, L_DEBUG, "udp-resolver: connecting key=%s host=%s tls=%d timeout_ms=%"u64"\n", carg->key, carg->host, carg->tls, carg->timeout);

	if (carg->lock) {
		if (carg->tt_timer)
			return;
		/* Prior attempt still owns the UDP handle: never memset over it. */
		if (carg->udp_client.loop && carg->udp_client.type != UV_UNKNOWN_HANDLE) {
			/* Ephemeral one-shot probes must release (not bare halt) or they
			 * stay in resolver_ephemeral_ht with a timer that will not fire. */
			if (carg->resolver_ephemeral)
				resolver_ephemeral_fail(carg);
			else if (!uv_is_closing((uv_handle_t *)&carg->udp_client))
				resolver_udp_halt(carg);
			return;
		}
		carg->lock = 0;
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->dns_status_reported = 0;
	carg->resolver_udp_shared = 0;

	char *addr = resolver_carg_get_addr(carg);
	if (!addr) {
		carg->lock = 0;
		if (carg->resolver_ephemeral)
			resolver_ephemeral_fail(carg);
		return;
	}

	resolver_udp_stop_timer(carg);
	resolver_init_client_timer(carg, resolver_timeout_udp);

	uv_ip4_addr(addr, carg->numport, &carg->remote_addr);

	if (carg->bind_port) {
		resolver_udp_bind *bind = resolver_udp_bind_get(carg);
		resolver_udp_pending *p;

		/* Ephemeral release owns shared/pending cleanup via resolver_udp_halt;
		 * do not query_release first (clears resolver_udp_shared and confuses halt). */
		if (!bind) {
			if (carg->resolver_ephemeral)
				resolver_ephemeral_fail(carg);
			else
				resolver_udp_query_release(carg);
			return;
		}

		if (!resolver_udp_pending_add(bind->pending, carg)) {
			if (carg->resolver_ephemeral)
				resolver_ephemeral_fail(carg);
			else
				resolver_udp_query_release(carg);
			return;
		}

		p = carg->resolver_udp_pending;
		if (p)
			p->bind = bind;
		carg->resolver_udp_shared = 1;

		if (resolver_udp_send_query(carg, &bind->udp)) {
			if (carg->resolver_ephemeral)
				resolver_ephemeral_fail(carg);
			else
				resolver_udp_query_release(carg);
			return;
		}
		return;
	}

	carg->udp_send.data = carg;
	memset(&carg->udp_client, 0, sizeof(carg->udp_client));
	carg->udp_client.data = carg;
	uv_udp_init(carg->loop, &carg->udp_client);

	int addr_ret = carg_set_socket_addr(&carg->local_addr, carg->bind_address, carg->bind_port);
	if (addr_ret) {
		int bind_ret = uv_udp_bind(&carg->udp_client, (const struct sockaddr *)carg->local_addr, 0);
		if (bind_ret) {
			carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", carg->bind_address ? carg->bind_address : "0.0.0.0", carg->bind_port, uv_strerror(bind_ret));
			resolver_udp_done(carg, &carg->udp_client, 0);
			return;
		}
	}

	/* Recv must be armed before send: a fast reply (or POLLIN with send
	 * completion) hits uv__udp_recvmsg while recv_cb is still NULL. */
	if (resolver_udp_recv_start(&carg->udp_client)) {
		resolver_udp_done(carg, &carg->udp_client, 0);
		return;
	}

	if (resolver_udp_send_query(carg, &carg->udp_client)) {
		resolver_udp_done(carg, &carg->udp_client, 0);
		return;
	}
}
