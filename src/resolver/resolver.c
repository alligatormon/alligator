#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#include "resolver/resolver.h"
#include "resolver/getaddrinfo.h"
#include "common/url.h"
#include "common/logs.h"
#include "common/stop.h"
#include "events/metrics.h"
#include "metric/percentile_heap.h"
#include "metric/labels.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

int resolver_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((dns_resource_records*)obj)->key;
	return strcmp(s1, s2);
}

void dns_record_rule_push(char *dname, uint16_t rtype, void *data, uint64_t datalen, char *IP, uint64_t SIZE, uint64_t ttl)
{
	char key[DNS_NAME_MAXLEN];
	snprintf(key, DNS_NAME_MAXLEN - 1, "%s:%hu", dname, rtype);
	uint32_t key_hash = tommy_strhash_u32(0, key);
	dns_resource_records *dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
	if (!dns_rr)
	{
		dns_rr = calloc(1, sizeof(*dns_rr));
		dns_rr->rr = calloc(1, sizeof(dns_record) * DNS_MAX_RR_PER_DOMAIN);
		dns_rr->type = rtype;
		dns_rr->key = strdup(key);
		alligator_ht_insert(ac->resolver, &(dns_rr->node), dns_rr, key_hash);
	}

	uint32_t DATA_hash = tommy_strhash_u32(0, IP);
	r_time time = setrtime();
	uint8_t nf = 1;
	for (uint64_t i = 0; i < dns_rr->size; ++i)
	{
		if (DATA_hash == dns_rr->rr[i].data_hash)
		{
			dns_rr->rr[i].ttl = time.sec + ttl;
			nf = 0;
		}
		else
		{
			if (dns_rr->rr[i].ttl <= time.sec)
			{
				string_free(dns_rr->rr[i].data);
				for (uint64_t j = i + 1; j < dns_rr->size; ++j)
				{
					dns_rr->rr[j - 1].data = dns_rr->rr[j].data;
					dns_rr->rr[j - 1].ttl = dns_rr->rr[j].ttl;
					dns_rr->rr[j - 1].data_hash = dns_rr->rr[j].data_hash;
				}
				--dns_rr->size;
			}
		}
	}

	if (nf)
	{
		if (dns_rr->size == DNS_MAX_RR_PER_DOMAIN)
		{
			string_null(dns_rr->rr[dns_rr->index].data);
			string_cat(dns_rr->rr[dns_rr->index].data, IP, SIZE);
			dns_rr->rr[dns_rr->index].data_hash = DATA_hash;
			dns_rr->rr[dns_rr->index].ttl = time.sec + ttl;

			if (dns_rr->index + 1 == dns_rr->size)
				dns_rr->index = 0;
			else
				++dns_rr->index;
		}
		else
		{
			dns_rr->rr[dns_rr->size].data = string_init_dupn(IP, SIZE);
			dns_rr->rr[dns_rr->size].data_hash = DATA_hash;
			dns_rr->rr[dns_rr->size].ttl = time.sec + ttl;
			++dns_rr->size;
		}
	}
}

uint16_t get_rrtype_by_str(char *rrtype)
{
	if (!strncasecmp(rrtype, "any", 3))
		return DNS_TYPE_ANY;
	else if (!strncasecmp(rrtype, "aaaa", 4))
		return DNS_TYPE_AAAA;
	else if (*rrtype == 'a' || *rrtype == 'A')
		return DNS_TYPE_A;
	else if (!strncasecmp(rrtype, "txt", 3))
		return DNS_TYPE_TXT;
	else if (!strncasecmp(rrtype, "ns", 2))
		return DNS_TYPE_NS;
	else if (!strncasecmp(rrtype, "cname", 4))
		return DNS_TYPE_CNAME;
	else if (!strncasecmp(rrtype, "ptr", 3))
		return DNS_TYPE_PTR;
	else if (!strncasecmp(rrtype, "mx", 2))
		return DNS_TYPE_MX;
	else if (!strncasecmp(rrtype, "soa", 3))
		return DNS_TYPE_SOA;
	else if (!strncasecmp(rrtype, "srv", 3))
		return DNS_TYPE_SRV;

	return 0;
}

char* get_str_by_rrtype(uint16_t type)
{
	if (type == DNS_TYPE_AAAA)
		return "AAAA";
	else if (type == DNS_TYPE_A)
		return "A";
	else if (type == DNS_TYPE_CNAME)
		return "CNAME";
	else if (type == DNS_TYPE_MX)
		return "MX";
	else if (type == DNS_TYPE_NS)
		return "NS";
	else if (type == DNS_TYPE_TXT)
		return "TXT";
	else if (type == DNS_TYPE_SRV)
		return "SRV";
	else if (type == DNS_TYPE_PTR)
		return "PTR";

	return NULL;
}

uint64_t dns_init_type(char *domain, char *buf, uint16_t rrtype, uint16_t *transaction_id)
{
	dns_t *query = calloc(1, sizeof(*query));

	query->hdr.transaction_id = *transaction_id = getpid() + ac->ping_id++;
	query->hdr.qr = DNS_QUERY;
	query->hdr.rd = 1;
	query->hdr.nquestion = 1;

	dns_rr_t *question = calloc(1, sizeof(*question));
	strlcpy(question->name, domain, DNS_NAME_MAXLEN);

	question->rtype = rrtype;
	question->rclass = DNS_CLASS_IN;
	query->questions = question;

	uint64_t buflen = dns_pack(query, buf, 1024);
	free(query->questions);
	free(query);
	return buflen;
}

uint64_t dns_init_strtype(char *domain, char *buf, char *rrtype, uint16_t *transaction_id)
{
	uint16_t type = get_rrtype_by_str(rrtype);
	if (!type)
		type = DNS_TYPE_A;

	return dns_init_type(domain, buf, type, transaction_id);
}

int dns_name_decode_ext(const char* buf, char *msg, char* domain)
{
	return dns_name_decode_msg(buf, msg, domain);
}

void dns_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dns");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = (void*)dns_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "dns_stats", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void resolver_start(uv_timer_t *timer)
{
	context_arg *carg = timer->data;

	if (!carg)
		return;
	if (alligator_stop_requested()) {
		if (carg->resolver_ephemeral)
			resolver_ephemeral_release(carg);
		return;
	}

	resolver_query_refresh(carg);

	if (carg->transport == APROTO_UDP)
		resolver_connect_udp(carg);
	else if (carg->transport == APROTO_TCP || carg->transport == APROTO_TLS)
		resolver_connect_tcp(carg);
	else if (carg->resolver_ephemeral) {
		/* Unsupported transport: nothing will ever answer this probe, so free
		 * it (and abandon waiters) instead of leaving it stuck in the ht. */
		resolver_ephemeral_fail(carg);
	}
}

void resolver_query_refresh(context_arg *carg)
{
	if (!carg || !carg->data)
		return;

	uint16_t rrtype = get_rrtype_by_str(carg->rrtype);
	if (!rrtype)
		rrtype = DNS_TYPE_A;

	char *buf = calloc(1, 1024);
	uint16_t transaction_id;
	uint64_t buflen = dns_init_type(carg->data, buf, rrtype, &transaction_id);
	carg->packets_id = transaction_id;

	/* aconf_mesg_set frees the previous mesg and buffer. */
	aconf_mesg_set(carg, buf, buflen);
}

void resolver_carg_set_transport(context_arg *carg)
{
	srand(time(NULL));
	uint8_t r = rand() % ac->resolver_size;

	char *proto = ac->srv_resolver[r]->hi->proto == APROTO_TCP ? "tcp" : "udp";
	carg->transport_string = proto;
	carg->transport = carg->proto = ac->srv_resolver[r]->hi->proto;
	strlcpy(carg->host, ac->srv_resolver[r]->hi->host, HOSTHEADER_SIZE);
}

static alligator_ht *resolver_rd_labels_alloc(host_aggregator_info *hi, const char *dname)
{
	alligator_ht *labels = alligator_ht_init(NULL);

	if (!labels)
		return NULL;
	if (hi)
		labels_hash_insert_nocache(labels, "host", hi->url);
	if (dname && dname[0])
		labels_hash_insert_nocache(labels, "name", (char *)dname);
	return labels;
}

static resolver_data *resolver_rd_probe_alloc(alligator_ht *labels)
{
	resolver_data *rd = calloc(1, sizeof(*rd));

	if (!rd)
		return NULL;

	rd->response_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	rd->read_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	rd->write_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	rd->labels = labels;
	return rd;
}

static void resolver_rd_probe_free(context_arg *carg)
{
	resolver_data *rd;

	if (!carg || !(rd = carg->rd))
		return;

	/* Shared srv_resolver entries are never owned by the probe. Detect via
	 * ownership flag (not by scanning srv_resolver): after resolver_stop sets
	 * resolver_size=0 a late free_carg would otherwise free already-freed rd. */
	if (!carg->resolver_rd_owned) {
		carg->rd = NULL;
		return;
	}

	free_percentile_buffer(rd->response_time);
	free_percentile_buffer(rd->read_time);
	free_percentile_buffer(rd->write_time);
	if (rd->labels)
		labels_hash_free(rd->labels);
	free(rd);
	carg->rd = NULL;
	carg->resolver_rd_owned = 0;
}

/* Ephemeral probes are reachable only through this table. libuv holds an
 * interior pointer at the embedded resolver_timer, which valgrind reports as
 * possibly lost and which never reaches carg_free. */
typedef struct resolver_ephemeral_slot {
	char *key;
	context_arg *carg;
	tommy_node node;
} resolver_ephemeral_slot;

static alligator_ht *resolver_ephemeral_ht;

static int resolver_ephemeral_compare(const void *arg, const void *obj)
{
	const char *key = arg;
	const resolver_ephemeral_slot *slot = obj;

	if (!key || !slot || !slot->key)
		return 1;
	return strcmp(key, slot->key);
}

/* Ephemeral dedup key is dname:rrtype only (matches dns cache keys). rclass is
 * always IN on the get_addr path; nameserver is picked at probe create time.
 * One in-flight probe per name/type is intentional for crawl leak control. */
static void resolver_ephemeral_make_key(char *dst, size_t dstsz, const char *dname, uint16_t rrtype)
{
	snprintf(dst, dstsz, "%s:%hu", dname ? dname : "", rrtype);
}

/* 0 = inert, 1 = open (caller may uv_close), 2 = already closing (wait for cb). */
static int resolver_handle_live(uv_handle_t *h)
{
	if (!h || !h->loop || h->type == UV_UNKNOWN_HANDLE)
		return 0;
	if (uv_is_closing(h))
		return 2;
	return 1;
}

static void resolver_ephemeral_unlink(context_arg *carg);

static int resolver_carg_in_aggregators(context_arg *carg)
{
	context_arg *found;

	if (!carg || !carg->key || !ac || !ac->aggregators)
		return 0;
	found = alligator_ht_search(ac->aggregators, aggregator_compare, carg->key, tommy_strhash_u32(0, carg->key));
	return found == carg;
}

static void resolver_ephemeral_free_carg(context_arg *carg)
{
	if (!carg || resolver_carg_in_aggregators(carg))
		return;

	/* Unlink only when freeing so lookup still blocks retries during teardown. */
	resolver_ephemeral_unlink(carg);
	free(carg->data);
	carg->data = NULL;
	resolver_rd_probe_free(carg);
	carg_free(carg);
}

void resolver_timer_handle_closed(uv_handle_t *handle)
{
	context_arg *carg = handle ? handle->data : NULL;

	/* Drop loop so carg_free → carg_uv_detach_timers does not uv_timer_stop a
	 * fully closed embedded handle (libuv abort). */
	if (handle)
		memset(handle, 0, sizeof(uv_timer_t));

	if (carg && carg->resolver_ephemeral_freeing)
		resolver_ephemeral_note_closed(carg);
}

typedef struct resolver_ephemeral_find {
	context_arg *carg;
	resolver_ephemeral_slot *slot;
} resolver_ephemeral_find;

static void resolver_ephemeral_find_cb(void *funcarg, void *arg)
{
	resolver_ephemeral_find *ctx = funcarg;
	resolver_ephemeral_slot *slot = arg;

	if (!ctx || ctx->slot || !slot)
		return;
	if (slot->carg == ctx->carg)
		ctx->slot = slot;
}

static void resolver_ephemeral_unlink(context_arg *carg)
{
	char key[DNS_NAME_MAXLEN + 16];
	resolver_ephemeral_slot *slot = NULL;
	resolver_ephemeral_find ctx;
	uint16_t rrtype = DNS_TYPE_A;
	uint32_t hash;

	if (!resolver_ephemeral_ht || !carg)
		return;

	if (carg->rrtype) {
		rrtype = get_rrtype_by_str(carg->rrtype);
		if (!rrtype)
			rrtype = DNS_TYPE_A;
	}
	resolver_ephemeral_make_key(key, sizeof(key), carg->data, rrtype);
	hash = tommy_strhash_u32(0, key);
	slot = alligator_ht_search(resolver_ephemeral_ht, resolver_ephemeral_compare, key, hash);
	if (!slot || slot->carg != carg) {
		ctx.carg = carg;
		ctx.slot = NULL;
		alligator_ht_foreach_arg(resolver_ephemeral_ht, resolver_ephemeral_find_cb, &ctx);
		slot = ctx.slot;
	}
	if (!slot)
		return;
	alligator_ht_remove_existing(resolver_ephemeral_ht, &slot->node);
	free(slot->key);
	free(slot);
}

/* Lazy ht creation/destruction can race when multiple threaded client loops
 * probe at once. Serialise creation with destroy; the ht itself has an rwlock. */
static pthread_mutex_t resolver_ephemeral_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int resolver_ephemeral_shutting_down;

/* 1 = tracked (inserted or already this carg), 0 = key owned by another probe
 * (that probe will serve waiters), -1 = OOM / init failure. */
static int resolver_ephemeral_add(context_arg *carg, const char *dname, uint16_t rrtype)
{
	char key[DNS_NAME_MAXLEN + 16];
	resolver_ephemeral_slot *slot;
	uint32_t hash;

	if (!carg)
		return -1;

	pthread_mutex_lock(&resolver_ephemeral_init_lock);
	if (resolver_ephemeral_shutting_down) {
		pthread_mutex_unlock(&resolver_ephemeral_init_lock);
		return -1;
	}
	if (!resolver_ephemeral_ht)
		resolver_ephemeral_ht = alligator_ht_init(NULL);
	if (!resolver_ephemeral_ht) {
		pthread_mutex_unlock(&resolver_ephemeral_init_lock);
		return -1;
	}

	resolver_ephemeral_make_key(key, sizeof(key), dname, rrtype);
	hash = tommy_strhash_u32(0, key);
	slot = alligator_ht_search(resolver_ephemeral_ht, resolver_ephemeral_compare, key, hash);
	if (slot) {
		/* Same carg, or a probe still tearing down (its slot is kept until
		 * free_carg to block overlapping probes): do not allocate. */
		pthread_mutex_unlock(&resolver_ephemeral_init_lock);
		return slot->carg == carg ? 1 : 0;
	}

	slot = calloc(1, sizeof(*slot));
	if (!slot) {
		pthread_mutex_unlock(&resolver_ephemeral_init_lock);
		return -1;
	}
	slot->key = strdup(key);
	if (!slot->key) {
		free(slot);
		pthread_mutex_unlock(&resolver_ephemeral_init_lock);
		return -1;
	}
	slot->carg = carg;
	alligator_ht_insert(resolver_ephemeral_ht, &slot->node, slot, hash);
	pthread_mutex_unlock(&resolver_ephemeral_init_lock);
	return 1;
}

void resolver_ephemeral_note_closed(context_arg *carg)
{
	if (!carg || !carg->resolver_ephemeral_freeing)
		return;
	/* Spurious callbacks must not free early or double-free. */
	if (!carg->resolver_ephemeral_closes)
		return;
	carg->resolver_ephemeral_closes--;
	if (carg->resolver_ephemeral_closes)
		return;
	carg->resolver_ephemeral_freeing = 0;
	carg->resolver_ephemeral_wait_send = 0;
	resolver_ephemeral_free_carg(carg);
}

/* Finish a stuck wait_send only when libuv no longer owns carg->udp_send.
 * Freeing while resolver_udp_send_active UAFs the embedded uv_udp_send_t
 * (nulling .data is not enough — libuv still writes the request object). */
static void resolver_ephemeral_cancel_wait_send(context_arg *carg)
{
	if (!carg || !carg->resolver_ephemeral_wait_send)
		return;
	if (carg->resolver_udp_send_active)
		return;
	carg->resolver_ephemeral_wait_send = 0;
	carg->udp_send.data = NULL;
	resolver_ephemeral_note_closed(carg);
}

void resolver_ephemeral_release(context_arg *carg)
{
	int timer_live;

	if (!carg || !carg->resolver_ephemeral || carg->resolver_ephemeral_freeing)
		return;
	/* Permanent dns aggregates stay in ac->aggregators and keep retrying. */
	if (resolver_carg_in_aggregators(carg))
		return;

	carg->resolver_ephemeral_freeing = 1;
	/* Keep ht slot until free_carg so get_addr cannot overlap this teardown.
	 * Hold one close until setup finishes so a nested uv_run (shutdown drain)
	 * cannot note_closed while closes is still 0 and then leave a stuck debt. */
	carg->resolver_ephemeral_closes = 1;

	if (carg->transport == APROTO_UDP || carg->proto == APROTO_UDP) {
		if (carg->resolver_udp_shared && carg->resolver_udp_send_active) {
			/* Shared bind is not closed per-query; wait for send_cb. */
			carg->resolver_ephemeral_wait_send = 1;
			carg->resolver_ephemeral_closes++;
		}
		carg->resolver_ephemeral_closes = (uint8_t)(carg->resolver_ephemeral_closes + resolver_udp_halt(carg));
	} else if (carg->transport == APROTO_TCP || carg->proto == APROTO_TCP ||
		   carg->transport == APROTO_TLS || carg->proto == APROTO_TLS) {
		carg->resolver_ephemeral_closes = (uint8_t)(carg->resolver_ephemeral_closes + resolver_tcp_halt(carg));
	}

	timer_live = resolver_handle_live((uv_handle_t *)&carg->resolver_timer);
	if (timer_live == 1) {
		uv_timer_stop(&carg->resolver_timer);
		carg->resolver_timer.data = carg;
		carg->resolver_ephemeral_closes++;
		uv_close((uv_handle_t *)&carg->resolver_timer, resolver_timer_handle_closed);
	} else if (timer_live == 2) {
		/* Already closing: must wait (embedded handle still owned by the
		 * loop). All resolver_timer closes use resolver_timer_handle_closed
		 * so note_closed drains this debt; a NULL close_cb would stick the
		 * probe — carg_close_embedded_uv_handles and release both use it. */
		carg->resolver_timer.data = carg;
		carg->resolver_ephemeral_closes++;
	}

	/* Drop the setup hold. Frees immediately when nothing else was armed. */
	resolver_ephemeral_note_closed(carg);
}

/* A probe attempt that ended without a usable answer. Oneshot clients waiting
 * on this hostname would otherwise stay with dns_awaiting=1 forever (the
 * probe is one-shot and the successful dns_handler retry never fires). Drop
 * them like the getaddrinfo path does, then free the probe. */
void resolver_ephemeral_fail(context_arg *carg)
{
	if (!carg)
		return;
	/* The response parser already reported this attempt (malformed/empty/
	 * NXDOMAIN). Only transport-level failures reach here unreported. */
	if (!carg->dns_status_reported)
		alligator_dns_resolution_status_set(carg,
			(carg->data && ((char *)carg->data)[0]) ? (char *)carg->data : carg->host,
			carg->rrtype ? carg->rrtype : "A", "IN", "error", 0);
	if (carg->resolver_ephemeral && carg->data)
		aggregator_oneshot_abandon_host(carg->data);
	resolver_ephemeral_release(carg);
}

context_arg *resolver_ephemeral_lookup(const char *dname, uint16_t rrtype)
{
	char key[DNS_NAME_MAXLEN + 16];
	resolver_ephemeral_slot *slot;
	uint32_t hash;

	if (!resolver_ephemeral_ht || !dname)
		return NULL;
	resolver_ephemeral_make_key(key, sizeof(key), dname, rrtype);
	hash = tommy_strhash_u32(0, key);
	slot = alligator_ht_search(resolver_ephemeral_ht, resolver_ephemeral_compare, key, hash);
	return slot ? slot->carg : NULL;
}

size_t resolver_ephemeral_count(void)
{
	return alligator_ht_count(resolver_ephemeral_ht);
}

typedef struct resolver_ephemeral_list {
	context_arg **list;
	size_t n;
	size_t cap;
	int oom;
} resolver_ephemeral_list;

static void resolver_ephemeral_collect_cb(void *funcarg, void *arg)
{
	resolver_ephemeral_list *ctx = funcarg;
	resolver_ephemeral_slot *slot = arg;
	context_arg **nl;
	size_t ncap;

	if (!ctx || ctx->oom || !slot || !slot->carg)
		return;
	if (ctx->n >= ctx->cap) {
		/* Count raced or was understated — grow; on OOM stop adding (caller
		 * still releases the partial snapshot; remaining stay tracked). */
		ncap = ctx->cap ? ctx->cap * 2 : 8;
		nl = realloc(ctx->list, ncap * sizeof(*nl));
		if (!nl) {
			ctx->oom = 1;
			return;
		}
		ctx->list = nl;
		ctx->cap = ncap;
	}
	ctx->list[ctx->n++] = slot->carg;
}

/* Snapshot cargs so release/unlink is safe outside foreach.
 * Returns 1 if ctx is usable (including empty). 0 only when the initial
 * calloc fails (no snapshot at all). Mid-walk overflow sets ctx->oom but
 * still returns 1 with a partial list so callers can release what was seen. */
static int resolver_ephemeral_collect(resolver_ephemeral_list *ctx)
{
	size_t n;

	if (!ctx || !resolver_ephemeral_ht)
		return 0;
	memset(ctx, 0, sizeof(*ctx));
	n = alligator_ht_count(resolver_ephemeral_ht);
	if (!n)
		return 1;
	ctx->list = calloc(n, sizeof(*ctx->list));
	if (!ctx->list) {
		ctx->oom = 1;
		return 0;
	}
	ctx->cap = n;
	alligator_ht_foreach_arg(resolver_ephemeral_ht, resolver_ephemeral_collect_cb, ctx);
	return 1;
}

static void resolver_ephemeral_release_all(void)
{
	resolver_ephemeral_list ctx;
	size_t i;

	if (!resolver_ephemeral_ht)
		return;
	if (!resolver_ephemeral_collect(&ctx)) {
		/* Prefer leaving probes tracked over dropping slots (ht_destroy leak)
		 * or unlinking during foreach. Retry: shutdown loop calls release_all
		 * again; ht_destroy does a final pass before forfree. */
		glog(L_ERROR, "resolver: ephemeral collect OOM, deferring release_all\n");
		return;
	}
	if (ctx.oom)
		glog(L_ERROR, "resolver: ephemeral collect truncated, releasing partial snapshot\n");
	for (i = 0; i < ctx.n; ++i)
		resolver_ephemeral_release(ctx.list[i]);
	free(ctx.list);
}

static void resolver_ephemeral_drain_loop(void)
{
	unsigned i;

	if (!ac || !ac->loop)
		return;
	for (i = 0; i < 64; ++i) {
		if (!uv_run(ac->loop, UV_RUN_NOWAIT))
			break;
	}
}

/* After shared binds are closed and the loop drained, finish wait_sends whose
 * send_cb already ran (or never armed send_active). Never frees while send is
 * still active — that UAFs embedded udp_send. */
static void resolver_ephemeral_cancel_stuck_wait_sends(void)
{
	resolver_ephemeral_list ctx;
	size_t i;

	if (!resolver_ephemeral_ht)
		return;
	if (!resolver_ephemeral_collect(&ctx))
		return;
	for (i = 0; i < ctx.n; ++i)
		resolver_ephemeral_cancel_wait_send(ctx.list[i]);
	free(ctx.list);
}

context_arg* aggregator_push_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass)
{
	if ((!ac->resolver_size) && (carg->transport == APROTO_RESOLVER)) {
		carglog(ac->system_carg, L_ERROR, "resolver: no resolver directive for '%s' key=%s\n", carg->data, carg->key);
		return NULL;
	}

	if ((!ac->resolver_size) && (carg->parser_handler != dns_handler)) {
		resolver_getaddrinfo_get(carg);
		return NULL;
	}

	if (
        (!ac->resolver_size)
        && (rrtype == DNS_TYPE_A)
        && (rclass == DNS_CLASS_IN)
        && (carg->transport != APROTO_RESOLVER)
        && ((carg->parser_handler == dns_handler) && ((carg->transport == APROTO_UDP) || (carg->transport == APROTO_TCP)))
		&& (!carg->resolver)
        )
	{
		resolver_getaddrinfo_get(carg);
		return NULL;
	}

	char *buf = calloc(1, 1024);
	uint16_t transaction_id;
	uint64_t buflen = dns_init_type(dname, buf, rrtype, &transaction_id);


	context_arg* new_carg;
	if (carg->transport == APROTO_RESOLVER)
	{
		srand(time(NULL));
		uint8_t r = rand() % ac->resolver_size;
		new_carg = context_arg_json_fill(NULL, ac->srv_resolver[r]->hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(ac->srv_resolver[r]->labels);
		new_carg->rd = ac->srv_resolver[r];
		new_carg->resolver_rd_owned = 0;
		if (carg->bind_address)
			new_carg->bind_address = strdup(carg->bind_address);
		new_carg->bind_port = carg->bind_port;
	}
	else if (carg->parser_handler == dns_handler)
	{
		host_aggregator_info *hi = parse_url(carg->url, strlen(carg->url));
		new_carg = context_arg_json_fill(NULL, hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(carg->labels);
		labels_hash_insert_nocache(new_carg->labels, "host", hi->url);
		new_carg->rd = resolver_rd_probe_alloc(resolver_rd_labels_alloc(hi, dname));
		new_carg->resolver_rd_owned = new_carg->rd ? 1 : 0;
		url_free(hi);
		if (carg->bind_address)
			new_carg->bind_address = strdup(carg->bind_address);
		new_carg->bind_port = carg->bind_port;
	}
	else
	{
		srand(time(NULL));
		uint8_t r = rand() % ac->resolver_size;
		new_carg = context_arg_json_fill(NULL, ac->srv_resolver[r]->hi, dns_handler, "dns_handler", NULL, 0, strdup(dname), NULL, 0, carg->loop, NULL, 1, NULL, 0);
		new_carg->labels = labels_dup(ac->srv_resolver[r]->labels);
		new_carg->rd = ac->srv_resolver[r];
		new_carg->resolver_rd_owned = 0;
	}

	new_carg->follow_redirects = 1;
	new_carg->key = malloc(255);

	snprintf(new_carg->key, 254, "%s://%s:%s/%s:%s:%u", carg->transport_string, new_carg->host, new_carg->port, (char*)new_carg->data, "IN", rrtype);

	new_carg->packets_id = transaction_id;
	new_carg->log_level = carg->log_level;
	new_carg->log_ch = carg->log_ch;
	new_carg->resolver = 1;
	new_carg->rrtype = get_str_by_rrtype(rrtype);
	if (carg->period)
		new_carg->period = carg->period;
	if (carg->timeout)
		new_carg->timeout = carg->timeout;
	if (carg->name)
		new_carg->name = strdup(carg->name);
	if (carg->ip_version)
		new_carg->ip_version = carg->ip_version;

	aconf_mesg_set(new_carg, buf, buflen);

	carglog(carg, L_DEBUG, "aggregator_push_addr %p->%p(%p:%p) with key %s, hostname %s, dname '%s', rrtype: %u, port: %s tls: %d, timeout: %"u64"\n", carg, new_carg, &new_carg->connect, &new_carg->client, new_carg->key, new_carg->host, dname, rrtype, new_carg->port, new_carg->tls, new_carg->timeout);

	new_carg->resolver_timer.data = new_carg;
	uv_timer_init(new_carg->loop, &new_carg->resolver_timer);
	{
		uint64_t repeat = new_carg->period ? new_carg->period : RESOLVER_QUERY_PERIOD_MS;
		uv_timer_start(&new_carg->resolver_timer, resolver_start, 0, repeat);
	}

	return new_carg;
}

string* dns_cache_pick(dns_resource_records *dns_rr)
{
	if (!dns_rr || !dns_rr->size)
		return NULL;

	r_time now = setrtime();
	uint64_t valid = 0;
	for (uint64_t i = 0; i < dns_rr->size; ++i) {
		if (dns_rr->rr[i].ttl > now.sec)
			++valid;
	}
	if (!valid)
		return NULL;

	srand(time(NULL));
	for (uint8_t attempt = 0; attempt < 8; ++attempt) {
		uint8_t r = rand() % dns_rr->size;
		if (dns_rr->rr[r].ttl > now.sec)
			return dns_rr->rr[r].data;
	}

	for (uint64_t i = 0; i < dns_rr->size; ++i) {
		if (dns_rr->rr[i].ttl > now.sec)
			return dns_rr->rr[i].data;
	}

	return NULL;
}

string* aggregator_get_addr(context_arg *carg, char *dname, uint16_t rrtype, uint32_t rclass)
{
	char key[DNS_NAME_MAXLEN];
	snprintf(key, DNS_NAME_MAXLEN - 1, "%s:%hu", dname, rrtype);
	uint32_t key_hash = tommy_strhash_u32(0, key);

	carglog(carg, L_DEBUG, "aggregator_get_addr '%s' %p(%p:%p) with key %s, hostname %s, dname '%s', port: %s tls: %d, timeout: %"u64"\n", key, carg, &carg->connect, &carg->client, carg->key, carg->host, dname, carg->port, carg->tls, carg->timeout);

	dns_resource_records *dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
	string *data = dns_cache_pick(dns_rr);
	if (data)
		return data;

	/* IPv4 literals are already resolved — seed the cache so oneshots and
	 * crawl alike connect immediately instead of waiting on DNS. */
	if (rrtype == DNS_TYPE_A && dname && is_ip_addr(dname)) {
		size_t n = strlen(dname);
		dns_record_rule_push(dname, DNS_TYPE_A, NULL, 0, dname, n, 3600);
		dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
		data = dns_cache_pick(dns_rr);
		if (data)
			return data;
	}

	/* After probes_halt, tcp/udp crawl may still call get_addr before the
	 * loop fully stops. A new probe would miss release_all and be orphaned
	 * when resolver_stop destroys the ephemeral ht (slots only, not cargs). */
	if (alligator_stop_requested())
		return NULL;

	/* The ephemeral probe table and its teardown (signal handler,
	 * resolver_stop) are main-loop owned. A client on a threaded worker loop
	 * must not create a probe whose uv handles would be closed from another
	 * thread; use libuv's per-loop getaddrinfo instead. */
	if (carg && carg->loop && ac->loop && carg->loop != ac->loop) {
		/* Resolve the name the caller asked for (may be a proxy host, not
		 * carg->host) so proxied threaded clients still connect. */
		resolver_getaddrinfo_get_host(carg, dname);
		return NULL;
	}

	/* A cache miss used to allocate a new probe on every crawl. The probe's
	 * repeating timer never reached carg_free, so an unreachable resolver
	 * (1.2.3.4:53) leaked one context_arg per host per attempt. Keep a single
	 * in-flight probe; free it when that attempt finishes so the next crawl
	 * can retry without orphaning the previous carg. */
	{
		context_arg *inflight = resolver_ephemeral_lookup(dname, rrtype);
		context_arg *probe;
		int add_result;

		if (inflight)
			return NULL;
		probe = aggregator_push_addr(carg, dname, rrtype, rclass);
		if (!probe)
			return NULL;
		probe->resolver_ephemeral = 1;
		/* One-shot: push_addr starts a repeating timer; restart with repeat 0. */
		uv_timer_stop(&probe->resolver_timer);
		if (uv_timer_start(&probe->resolver_timer, resolver_start, 0, 0)) {
			/* Inert timer would stick in the ht and block retries. */
			resolver_ephemeral_fail(probe);
			return NULL;
		}
		add_result = resolver_ephemeral_add(probe, dname, rrtype);
		if (add_result < 0) {
			/* Tracking failed; abandon so waiters are not stranded. */
			resolver_ephemeral_fail(probe);
		}
		else if (!add_result) {
			/* Another loop's probe already owns this name and will serve the
			 * waiters — release ours without abandoning them. */
			resolver_ephemeral_release(probe);
		}
	}
	return NULL;
}

string* aggregator_get_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass)
{
	uint16_t rrtype = get_rrtype_by_str(strtype);

	return aggregator_get_addr(carg, dname, rrtype, rclass);
}

string* resolver_cache_lookup(char *dname, uint16_t rrtype)
{
	if (!ac || !ac->resolver || !dname)
		return NULL;

	char key[DNS_NAME_MAXLEN];
	snprintf(key, DNS_NAME_MAXLEN - 1, "%s:%hu", dname, rrtype);
	uint32_t key_hash = tommy_strhash_u32(0, key);

	dns_resource_records *dns_rr = alligator_ht_search(ac->resolver, resolver_compare, key, key_hash);
	return dns_cache_pick(dns_rr);
}

context_arg* aggregator_push_addr_strtype(context_arg *carg, char *dname, char* strtype, uint32_t rclass)
{
	uint16_t rrtype = get_rrtype_by_str(strtype);

	carglog(carg, L_DEBUG, "aggregator_push_addr_strtype %p(%p:%p) with key %s, hostname %s, dname '%s', port: %s tls: %d, timeout: %"u64"\n", carg, &carg->connect, &carg->client, carg->key, carg->host, dname, carg->port, carg->tls, carg->timeout);

	return aggregator_push_addr(carg, dname, rrtype, rclass);
}

void resolver_push_json(json_t *resolver)
{
	if (ac->resolver_size >= 255)
		return;

	char *host = (char*)json_string_value(resolver);
	size_t size = json_string_length(resolver);
	//ac->hi_resolver[ac->resolver_size] = parse_url(host, size);

	ac->srv_resolver[ac->resolver_size] = calloc(1, sizeof(resolver_data));
	ac->srv_resolver[ac->resolver_size]->response_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->read_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->write_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	ac->srv_resolver[ac->resolver_size]->hi = parse_url(host, size);
	ac->srv_resolver[ac->resolver_size]->labels = resolver_rd_labels_alloc(ac->srv_resolver[ac->resolver_size]->hi, NULL);

	++ac->resolver_size;
}

void resolver_del_json(json_t *resolver)
{
	//char *host = json_string_value(resolver);
	//size_t size = json_string_length(resolver);
	//url_free(ac->hi_resolver[ac->resolver_size])
}

void resolver_del(context_arg *carg)
{
	if (!carg)
		return;

	aggregators_ht_unlink(carg);
	carg_uv_detach_timers(carg);
	resolver_rd_probe_free(carg);
	free(carg->data);
	carg->data = NULL;
	carg->data_allocated = 0;
	carg_free(carg);
}

void resolver_probe_halt(context_arg *carg)
{
	if (!carg || !carg->resolver)
		return;

	if (!uv_is_closing((uv_handle_t *)&carg->resolver_timer))
		uv_timer_stop(&carg->resolver_timer);
	carg->resolver_timer.data = NULL;

	if (carg->transport == APROTO_UDP || carg->proto == APROTO_UDP)
		resolver_udp_halt(carg);
	else if (carg->transport == APROTO_TCP || carg->proto == APROTO_TCP ||
		 carg->transport == APROTO_TLS || carg->proto == APROTO_TLS)
		resolver_tcp_halt(carg);

	carg->lock = 0;
}

static void resolver_probes_halt_foreach(void *funcarg, void *arg)
{
	resolver_probe_halt(arg);
}

void resolver_probes_halt(void)
{
	/* Signal-safe path (alligator_stop from uv signal cb): arm closes only.
	 * Do not uv_run / force-cancel here — nested uv_run is unsafe (see stop.c).
	 * Shared-bind halt + drain + wait_send finish: resolver_ephemeral_shutdown
	 * from alligator_shutdown_after_loop after uv_stop. */
	resolver_ephemeral_release_all();

	if (ac && ac->aggregators)
		alligator_ht_foreach_arg(ac->aggregators, resolver_probes_halt_foreach, NULL);
}

void resolver_ephemeral_shutdown(void)
{
	unsigned i;

	/* Close shared binds so in-flight uv_udp_send cbs run (ECANCELED) while
	 * ephemeral cargs are still alive; release any probes still tracked
	 * (covers collect-OOM / truncated snapshots from probes_halt); drain;
	 * finish wait_sends that are no longer send_active. Repeat briefly for
	 * late close_cbs and deferred release_all retries. */
	resolver_udp_binds_halt();
	for (i = 0; i < 4; ++i) {
		resolver_ephemeral_release_all();
		resolver_ephemeral_drain_loop();
		resolver_ephemeral_cancel_stuck_wait_sends();
		if (!resolver_ephemeral_ht || !alligator_ht_count(resolver_ephemeral_ht))
			break;
	}
}

void resolver_stop_foreach(void *funcarg, void* arg)
{
	dns_resource_records *dns_rr = arg;
	for (uint64_t i = 0; i < dns_rr->size; ++i)
		string_free(dns_rr->rr[i].data);

	free(dns_rr->key);
	free(dns_rr->rr);
	free(dns_rr);
}

static void resolver_ephemeral_slot_free(void *arg)
{
	resolver_ephemeral_slot *slot = arg;

	if (!slot)
		return;
	free(slot->key);
	free(slot);
}

static void resolver_ephemeral_ht_destroy(void)
{
	if (!resolver_ephemeral_ht)
		return;

	/* Block lazy recreation for the duration of teardown. */
	pthread_mutex_lock(&resolver_ephemeral_init_lock);
	resolver_ephemeral_shutting_down = 1;
	pthread_mutex_unlock(&resolver_ephemeral_init_lock);

	/* Defense in depth vs probes allocated after probes_halt: release, then
	 * the same post-loop drain/cancel as resolver_ephemeral_shutdown. Cargs
	 * mid-teardown still reach free_carg from close_cbs after the ht is gone. */
	resolver_ephemeral_release_all();
	resolver_ephemeral_shutdown();

	/* forfree runs the slot free cb then frees the inner tommy_hashdyn.
	 * Any slot whose carg was not freed (OOM collect / still send_active) is
	 * dropped here — prefer that over UAF on embedded udp_send. */
	alligator_ht_forfree(resolver_ephemeral_ht, resolver_ephemeral_slot_free);
	resolver_ephemeral_ht->ht = NULL;
	pthread_rwlock_destroy(&resolver_ephemeral_ht->rwlock);
	free(resolver_ephemeral_ht);

	pthread_mutex_lock(&resolver_ephemeral_init_lock);
	resolver_ephemeral_ht = NULL;
	/* Stay shut: ac->resolver / srv_resolver are freed next and worker
	 * threads may still be alive until main_free() → thread_loop_free(). */
	pthread_mutex_unlock(&resolver_ephemeral_init_lock);
}

void resolver_stop()
{
	/* Drain ephemeral probes before tearing down srv_resolver / DNS cache.
	 * Probes that borrow srv_resolver[*] set resolver_rd_owned=0 so a late
	 * free_carg cannot free shared rd after resolver_size hits 0. */
	resolver_ephemeral_ht_destroy();

	alligator_ht_foreach_arg(ac->resolver, resolver_stop_foreach, NULL);
	alligator_ht_done(ac->resolver);
	free(ac->resolver);

	if (ac->resolver_size)
	{
		while (ac->resolver_size) {
			resolver_data *rd = ac->srv_resolver[--ac->resolver_size];
			if (!rd)
				continue;
			url_free(rd->hi);
			labels_hash_free(rd->labels);
			free_percentile_buffer(rd->response_time);
			free_percentile_buffer(rd->read_time);
			free_percentile_buffer(rd->write_time);
			free(rd);
		}
	}
}
