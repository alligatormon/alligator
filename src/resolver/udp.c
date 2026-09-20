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

static void resolver_udp_bind_closed(uv_handle_t *handle)
{
	resolver_udp_bind *bind = handle->data;

	if (!bind)
		return;

	if (bind->pending) {
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

	if (bind->udp.loop && !uv_is_closing((uv_handle_t *)&bind->udp)) {
		uv_udp_recv_stop(&bind->udp);
		bind->udp.data = bind;
		uv_close((uv_handle_t *)&bind->udp, resolver_udp_bind_closed);
	} else {
		resolver_udp_bind_closed((uv_handle_t *)&bind->udp);
	}
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

static resolver_udp_bind *resolver_udp_bind_lookup(const char *ip, uint16_t port)
{
	char key[96];
	uint32_t key_hash;
	resolver_udp_bind *bind;

	if (!port)
		return NULL;

	bind = resolver_udp_port_binds[port];
	if (bind && !bind->closing && !strcmp(bind->bind_ip, ip))
		return bind;

	if (!resolver_udp_binds)
		return NULL;

	resolver_udp_bind_key(key, sizeof(key), ip, port);
	key_hash = tommy_strhash_u32(0, key);
	bind = alligator_ht_search(resolver_udp_binds, resolver_udp_bind_compare, key, key_hash);
	if (bind && !bind->closing)
		return bind;
	return NULL;
}

static void resolver_udp_bind_discard(resolver_udp_bind *bind)
{
	if (!bind)
		return;
	bind->closing = 1;
	if (bind->udp.loop && !uv_is_closing((uv_handle_t *)&bind->udp)) {
		bind->udp.data = bind;
		uv_close((uv_handle_t *)&bind->udp, resolver_udp_bind_closed);
	} else {
		resolver_udp_bind_closed((uv_handle_t *)&bind->udp);
	}
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
	bind = resolver_udp_bind_lookup(ip, carg->bind_port);
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
		existing = resolver_udp_bind_lookup(ip, carg->bind_port);
		resolver_udp_bind_discard(bind);
		if (existing) {
			carglog(carg, L_DEBUG, "udp-resolver: reuse shared bind %s:%u for key=%s\n", ip, carg->bind_port, carg->key);
			return existing;
		}
		carglog(carg, L_FATAL, "Bind udp socket '%s:%d' error %s\n", ip, carg->bind_port, uv_strerror(bind_ret));
		return NULL;
	}

	recv_ret = uv_udp_recv_start(&bind->udp, alloc_buffer, resolver_read_udp);
	if (recv_ret && recv_ret != UV_EALREADY) {
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
	carg->resolver_udp_shared = 0;
	carg->lock = 0;
}

static void resolver_udp_closed(uv_handle_t *handle)
{
	context_arg *carg = handle->data;

	if (carg) {
		resolver_udp_stop_timer(carg);
		carg->lock = 0;
		memset(&carg->udp_client, 0, sizeof(carg->udp_client));
	}
}

static void resolver_udp_abort(context_arg *carg, uv_udp_t *udp)
{
	resolver_udp_stop_timer(carg);

	if (udp && udp->loop && !uv_is_closing((uv_handle_t*)udp)) {
		uv_udp_recv_stop(udp);
		udp->data = carg;
		uv_close((uv_handle_t*)udp, resolver_udp_closed);
	} else if (carg) {
		carg->lock = 0;
	}
}

void resolver_udp_halt(context_arg *carg)
{
	if (!carg)
		return;

	if (carg->resolver_udp_shared) {
		resolver_udp_query_release(carg);
		return;
	}

	resolver_udp_abort(carg, &carg->udp_client);
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
	if (carg->resolver_udp_shared)
		resolver_udp_query_release(carg);
	else
		resolver_udp_abort(carg, &carg->udp_client);
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
	metric_add_labels5("alligator_parser_ok", &carg->parsed, DATATYPE_UINT, carg, "proto", "tcp", "type", "aggregator", "host", carg->host, "key", carg->key, "parser", carg->parser_name);
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
			resolver_udp_abort(carg, req);
		return;
	}
	if (nread == 0)
	{
		if (buf && buf->base)
			free(buf->base);
		if (!shared && carg)
			resolver_udp_abort(carg, req);
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

	if (shared) {
		resolver_udp_query_release(carg);
		free(carg->local_addr);
		carg->local_addr = NULL;
	} else if (carg->lock) {
		resolver_udp_abort(carg, req);
		free(carg->local_addr);
		carg->local_addr = NULL;
	}

	free(buf->base);
}

void resolver_send_udp(uv_udp_send_t* req, int status) {
	context_arg *carg = req->data;
	carglog(carg, L_DEBUG, "udp-resolver: sent key=%s host=%s tls=%d\n", carg->key, carg->host, carg->tls);
	carg->write_time_finish = setrtime();

	if (status != 0) {
		carglog(carg, L_ERROR, "send_cb error: %s\n", uv_strerror(status));
		if (carg->resolver_udp_shared)
			resolver_udp_query_release(carg);
		else
			resolver_udp_abort(carg, req->handle);
		return;
	}
	if (!carg->resolver_udp_shared) {
		req->handle->data = req->data;
		uv_udp_recv_start(req->handle, alloc_buffer, resolver_read_udp);
	}
	carg->read_time = setrtime();
}

static int resolver_udp_send_query(context_arg *carg, uv_udp_t *udp)
{
	int status;

	carg->udp_send.data = carg;
	status = uv_udp_send(&carg->udp_send, udp, &carg->request_buffer, 1, (struct sockaddr *)&carg->remote_addr, resolver_send_udp);
	if (status) {
		carglog(carg, L_ERROR, "uv_udp_send error: %s\n", uv_strerror(status));
		return status;
	}

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

	if (!carg || !carg->loop || alligator_stop_requested())
		return;

	carg->count = 0;
	carglog(carg, L_DEBUG, "udp-resolver: connecting key=%s host=%s tls=%d timeout_ms=%"u64"\n", carg->key, carg->host, carg->tls, carg->timeout);

	if (carg->lock) {
		if (carg->tt_timer)
			return;
		carg->lock = 0;
	}

	carg->lock = 1;
	carg->parsed = 0;
	carg->resolver_udp_shared = 0;

	char *addr = resolver_carg_get_addr(carg);
	if (!addr) {
		carg->lock = 0;
		return;
	}

	resolver_udp_stop_timer(carg);
	resolver_init_client_timer(carg, resolver_timeout_udp);

	uv_ip4_addr(addr, carg->numport, &carg->remote_addr);

	if (carg->bind_port) {
		resolver_udp_bind *bind = resolver_udp_bind_get(carg);
		resolver_udp_pending *p;

		if (!bind) {
			resolver_udp_query_release(carg);
			return;
		}

		if (!resolver_udp_pending_add(bind->pending, carg)) {
			resolver_udp_query_release(carg);
			return;
		}

		p = carg->resolver_udp_pending;
		if (p)
			p->bind = bind;
		carg->resolver_udp_shared = 1;

		if (resolver_udp_send_query(carg, &bind->udp)) {
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
			resolver_udp_abort(carg, &carg->udp_client);
			return;
		}
	}

	if (resolver_udp_send_query(carg, &carg->udp_client)) {
		resolver_udp_abort(carg, &carg->udp_client);
		return;
	}
}
