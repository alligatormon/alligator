/*
 * Alligator host builtins for avrl: dns_lookup() and reverse_dns().
 *
 * These bridge VRL scripts to alligator's shared DNS cache (ac->resolver,
 * populated via dns_record_rule_push / read via resolver_cache_lookup).
 *
 * Async model (see vrl/type.h and vrl/vrl.c):
 *   - On a cache HIT the value is returned immediately.
 *   - On a first-sight MISS the builtin starts a one-shot async resolution and
 *     marks the stream suspended (returns null). vrl_run_record() then discards
 *     the un-exported record and a poll timer replays it once the answer lands
 *     in the cache (or, after a timeout, logs L_ERROR and replays with null).
 *
 * Kickoff uses uv_getaddrinfo/uv_getnameinfo (one-shot, TTL-bounded) rather
 * than aggregator_push_addr(): dns_lookup targets arbitrary log-derived names,
 * and permanent repeating resolver probes per name would grow unbounded. The
 * result is stored in the same cache the configured resolver uses.
 */

#define _GNU_SOURCE
#include "vrl/type.h"
#include "resolver/resolver.h"
#include "resolver/dns.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "main.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern aconf *ac;

/* ---------------- resolution metrics ----------------
 *
 * Emitted against ac->system_carg (stable namespace; the originating carg may
 * be freed before an async resolution completes):
 *   vrl_dns_resolutions_total{type=A|AAAA|PTR, result=success|failure|timeout}
 *   vrl_dns_resolution_duration_seconds_sum{type=..., result=...}
 * Average latency = duration_sum / resolutions_total for a given result.
 * "success"/"failure" are recorded when the async resolver call completes;
 * "timeout" is recorded when a paused stream gives up waiting (see vrl.c).
 */
void vrl_dns_metric(const char *type, const char *result,
		    uint64_t start_ms, uint64_t now_ms)
{
	if (!ac || !ac->system_carg)
		return;
	if (!type)
		type = "unknown";

	/* metric_update_* accumulates (+=); metric_add_* would overwrite, keeping
	 * the counter pinned at 1 and losing duration accumulation. */
	uint64_t one = 1;
	metric_update_labels2("vrl_dns_resolutions_total", &one, DATATYPE_UINT,
			      ac->system_carg, "type", (char *)type, "result", (char *)result);

	double dur = (now_ms >= start_ms) ? (double)(now_ms - start_ms) / 1000.0 : 0.0;
	metric_update_labels2("vrl_dns_resolution_duration_seconds_sum", &dur, DATATYPE_DOUBLE,
			      ac->system_carg, "type", (char *)type, "result", (char *)result);
}

/* ---------------- negative cache ----------------
 *
 * Names that failed to resolve (getaddrinfo/getnameinfo error) or timed out are
 * remembered here for dns_negative_ttl_ms. While an entry is live, dns_lookup /
 * reverse_dns return null immediately instead of re-suspending the stream and
 * re-issuing a resolution for every occurrence of a bad name. When the entry
 * expires the name is re-resolved (the "reresolve faster" knob = short TTL).
 *
 * Guarded by a dedicated mutex: entries are looked up and evicted (freed) here,
 * and this runs both on the loop thread (callbacks/timer) and during vrl record
 * processing (possibly other threads), so alligator_ht's internal rwlock alone
 * is not enough to make the search-then-evict/refresh sequence safe.
 */
typedef struct vrl_dns_neg {
	alligator_ht_node node;
	char *key;             /* "<name>:<rrtype>" (owned) */
	uint64_t expire_ms;    /* uv_now() when this negative entry lapses */
} vrl_dns_neg;

static alligator_ht *vrl_dns_negcache;
static uv_mutex_t vrl_dns_neg_lock;
static int vrl_dns_neg_ready;

static int vrl_dns_neg_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const vrl_dns_neg *)obj)->key);
}

static void vrl_dns_neg_init(void)
{
	if (vrl_dns_neg_ready)
		return;
	vrl_dns_negcache = alligator_ht_init(NULL);
	uv_mutex_init(&vrl_dns_neg_lock);
	vrl_dns_neg_ready = 1;
}

int vrl_dns_neg_active(const char *key, uint64_t now_ms)
{
	if (!vrl_dns_neg_ready || !key)
		return 0;

	uint32_t h = tommy_strhash_u32(0, (char *)key);
	int active = 0;

	uv_mutex_lock(&vrl_dns_neg_lock);
	vrl_dns_neg *e = alligator_ht_search(vrl_dns_negcache, vrl_dns_neg_compare, key, h);
	if (e) {
		if (e->expire_ms > now_ms) {
			active = 1;
		} else {
			/* lapsed: drop so the name gets re-resolved */
			alligator_ht_remove_existing(vrl_dns_negcache, &e->node);
			free(e->key);
			free(e);
		}
	}
	uv_mutex_unlock(&vrl_dns_neg_lock);

	return active;
}

void vrl_dns_neg_add(const char *key, uint64_t now_ms, uint64_t ttl_ms, uint64_t cap)
{
	if (!vrl_dns_neg_ready || !key || !ttl_ms)
		return;
	if (!cap)
		cap = VRL_DNS_DEFAULT_NEGATIVE_CACHE_MAX;

	uint32_t h = tommy_strhash_u32(0, (char *)key);

	uv_mutex_lock(&vrl_dns_neg_lock);
	vrl_dns_neg *e = alligator_ht_search(vrl_dns_negcache, vrl_dns_neg_compare, key, h);
	if (e) {
		e->expire_ms = now_ms + ttl_ms; /* refresh */
	} else if (alligator_ht_count(vrl_dns_negcache) < cap) {
		e = calloc(1, sizeof(*e));
		if (e) {
			e->key = strdup(key);
			e->expire_ms = now_ms + ttl_ms;
			alligator_ht_insert(vrl_dns_negcache, &e->node, e, h);
		}
	}
	/* else: cap reached -> skip; that name simply re-suspends until entries lapse */
	uv_mutex_unlock(&vrl_dns_neg_lock);
}

/* ---------------- reverse-name construction ---------------- */

int vrl_dns_reverse_name(const char *ip, char *out, size_t outlen)
{
	if (!ip || !out || !outlen)
		return 0;

	struct in_addr a4;
	struct in6_addr a6;

	if (inet_pton(AF_INET, ip, &a4) == 1) {
		const unsigned char *b = (const unsigned char *)&a4.s_addr;
		int n = snprintf(out, outlen, "%u.%u.%u.%u.in-addr.arpa",
				 b[3], b[2], b[1], b[0]);
		return (n > 0 && (size_t)n < outlen) ? 1 : 0;
	}

	if (inet_pton(AF_INET6, ip, &a6) == 1) {
		char *p = out;
		size_t rem = outlen;
		for (int i = 15; i >= 0; --i) {
			unsigned char byte = a6.s6_addr[i];
			int n = snprintf(p, rem, "%x.%x.", byte & 0x0f, (byte >> 4) & 0x0f);
			if (n < 0 || (size_t)n >= rem)
				return 0;
			p += n;
			rem -= (size_t)n;
		}
		int n = snprintf(p, rem, "ip6.arpa");
		return (n > 0 && (size_t)n < rem) ? 1 : 0;
	}

	return 0;
}

/* ---------------- one-shot async resolution (populates cache) ---------------- */

typedef struct vrl_dns_ai_req {
	uv_getaddrinfo_t req;   /* must be first: cb casts back to this */
	char *store_name;       /* cache dname to store the answer under */
	uint16_t rrtype;
	uint64_t start_ms;      /* uv_now() at kickoff, for duration metric */
	uint64_t neg_ttl_ms;    /* negative-cache TTL to apply on failure (0 = off) */
	uint64_t neg_cap;       /* negative-cache size cap */
} vrl_dns_ai_req;

typedef struct vrl_dns_ni_req {
	uv_getnameinfo_t req;   /* must be first: cb casts back to this */
	char *store_name;       /* cache dname (the *.arpa key) for the PTR */
	uint64_t start_ms;      /* uv_now() at kickoff, for duration metric */
	uint64_t neg_ttl_ms;    /* negative-cache TTL to apply on failure (0 = off) */
	uint64_t neg_cap;       /* negative-cache size cap */
} vrl_dns_ni_req;

/* Record a negative entry for "<store_name>:<rrtype>" if enabled. */
static void vrl_dns_neg_on_fail(const char *store_name, uint16_t rrtype,
				uint64_t now_ms, uint64_t ttl_ms, uint64_t cap)
{
	if (!ttl_ms || !store_name)
		return;
	char key[VRL_DNS_KEY_MAX];
	snprintf(key, sizeof(key), "%s:%hu", store_name, rrtype);
	vrl_dns_neg_add(key, now_ms, ttl_ms, cap);
}

static void vrl_dns_getaddrinfo_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	vrl_dns_ai_req *r = (vrl_dns_ai_req *)req;
	char addr[INET6_ADDRSTRLEN] = {0};
	uint64_t now = uv_now(req->loop);
	const char *type = get_str_by_rrtype(r->rrtype);

	if (status >= 0 && res) {
		for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
			if (r->rrtype == DNS_TYPE_AAAA && ai->ai_family == AF_INET6) {
				uv_ip6_name((struct sockaddr_in6 *)ai->ai_addr, addr, sizeof(addr));
				break;
			}
			if (r->rrtype == DNS_TYPE_A && ai->ai_family == AF_INET) {
				uv_ip4_name((struct sockaddr_in *)ai->ai_addr, addr, sizeof(addr));
				break;
			}
		}
		if (addr[0]) {
			dns_record_rule_push(r->store_name, r->rrtype, NULL, 0,
					     addr, strlen(addr), 300);
			vrl_dns_metric(type, "success", r->start_ms, now);
		} else {
			vrl_dns_metric(type, "failure", r->start_ms, now);
			vrl_dns_neg_on_fail(r->store_name, r->rrtype, now, r->neg_ttl_ms, r->neg_cap);
		}
	} else {
		vrl_dns_metric(type, "failure", r->start_ms, now);
		vrl_dns_neg_on_fail(r->store_name, r->rrtype, now, r->neg_ttl_ms, r->neg_cap);
		glog(L_INFO, "vrl: dns_lookup getaddrinfo '%s' failed: %s\n",
		     r->store_name ? r->store_name : "(null)", uv_strerror(status));
	}

	if (res)
		uv_freeaddrinfo(res);
	free(r->store_name);
	free(r);
}

static void vrl_dns_getnameinfo_cb(uv_getnameinfo_t *req, int status,
				   const char *hostname, const char *service)
{
	(void)service;
	vrl_dns_ni_req *r = (vrl_dns_ni_req *)req;
	uint64_t now = uv_now(req->loop);

	if (status >= 0 && hostname && hostname[0]) {
		dns_record_rule_push(r->store_name, DNS_TYPE_PTR, NULL, 0,
				     (char *)hostname, strlen(hostname), 300);
		vrl_dns_metric("PTR", "success", r->start_ms, now);
	} else {
		vrl_dns_metric("PTR", "failure", r->start_ms, now);
		vrl_dns_neg_on_fail(r->store_name, DNS_TYPE_PTR, now, r->neg_ttl_ms, r->neg_cap);
		glog(L_INFO, "vrl: reverse_dns getnameinfo '%s' failed: %s\n",
		     r->store_name ? r->store_name : "(null)",
		     status < 0 ? uv_strerror(status) : "(empty)");
	}

	free(r->store_name);
	free(r);
}

static void vrl_dns_kickoff_forward(context_arg *carg, const char *name, uint16_t rrtype,
				    uint64_t neg_ttl_ms, uint64_t neg_cap)
{
	vrl_dns_ai_req *r = calloc(1, sizeof(*r));
	if (!r)
		return;
	r->store_name = strdup(name);
	r->rrtype = rrtype;
	r->start_ms = uv_now(carg->loop);
	r->neg_ttl_ms = neg_ttl_ms;
	r->neg_cap = neg_cap;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = (rrtype == DNS_TYPE_AAAA) ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int rc = uv_getaddrinfo(carg->loop, &r->req, vrl_dns_getaddrinfo_cb,
				name, NULL, &hints);
	if (rc) {
		carglog(carg, L_ERROR, "vrl: dns_lookup getaddrinfo start failed for '%s': %s\n",
			name, uv_strerror(rc));
		free(r->store_name);
		free(r);
	}
}

static void vrl_dns_kickoff_reverse(context_arg *carg, const char *ip, const char *arpa,
				    uint64_t neg_ttl_ms, uint64_t neg_cap)
{
	struct sockaddr_storage ss;
	memset(&ss, 0, sizeof(ss));
	struct sockaddr *sa = NULL;

	struct in_addr a4;
	struct in6_addr a6;
	if (inet_pton(AF_INET, ip, &a4) == 1) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
		sin->sin_family = AF_INET;
		sin->sin_addr = a4;
		sa = (struct sockaddr *)sin;
	} else if (inet_pton(AF_INET6, ip, &a6) == 1) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = a6;
		sa = (struct sockaddr *)sin6;
	} else {
		return;
	}

	vrl_dns_ni_req *r = calloc(1, sizeof(*r));
	if (!r)
		return;
	r->store_name = strdup(arpa);
	r->start_ms = uv_now(carg->loop);
	r->neg_ttl_ms = neg_ttl_ms;
	r->neg_cap = neg_cap;

	int rc = uv_getnameinfo(carg->loop, &r->req, vrl_dns_getnameinfo_cb, sa, 0);
	if (rc) {
		carglog(carg, L_ERROR, "vrl: reverse_dns getnameinfo start failed for '%s': %s\n",
			ip, uv_strerror(rc));
		free(r->store_name);
		free(r);
	}
}

/* ---------------- builtins ---------------- */

static vrl_status vrl_dns_common(vrl_call_args *a, int reverse, vrl_value **out, char **err)
{
	vrl_stream *st = (a && a->ctx) ? (vrl_stream *)a->ctx->host : NULL;
	const char *fname = reverse ? "reverse_dns" : "dns_lookup";

	if (!st || !st->carg) {
		*err = vrl_errf("%s: not available outside an alligator vrl stream", fname);
		return VRL_ERR;
	}

	vrl_value *arg = vrl_arg(a, NULL, 0);
	if (!arg || arg->type != VRL_BYTES || !arg->u.bytes.data) {
		*err = vrl_errf("%s: expects a string argument", fname);
		return VRL_ERR;
	}

	char qname[VRL_DNS_KEY_MAX];
	uint16_t rrtype;
	if (reverse) {
		if (!vrl_dns_reverse_name(arg->u.bytes.data, qname, sizeof(qname))) {
			*err = vrl_errf("reverse_dns: invalid IP literal '%s'", arg->u.bytes.data);
			return VRL_ERR;
		}
		rrtype = DNS_TYPE_PTR;
	} else {
		snprintf(qname, sizeof(qname), "%s", arg->u.bytes.data);
		rrtype = DNS_TYPE_A;
	}

	/* Timeout fallback: this exact name/type already gave up — yield null once
	 * so the replayed record can complete instead of pausing forever. */
	if (st->dns_force_null && rrtype == st->dns_rrtype && !strcmp(qname, st->dns_name)) {
		st->dns_force_null = 0;
		*out = vrl_null();
		return VRL_OK;
	}

	/* Positive cache wins over any lingering negative entry. */
	string *hit = resolver_cache_lookup(qname, rrtype);
	if (hit && hit->s) {
		*out = vrl_bytes(hit->s, hit->l);
		return VRL_OK;
	}

	/* Negative cache: a recent failure/timeout for this name is still in effect,
	 * so yield null immediately without re-suspending or re-resolving. */
	if (st->dns_negative_ttl_ms) {
		uint64_t now = st->carg->loop ? uv_now(st->carg->loop) : 0;
		char nkey[VRL_DNS_KEY_MAX];
		snprintf(nkey, sizeof(nkey), "%s:%hu", qname, rrtype);
		if (vrl_dns_neg_active(nkey, now)) {
			*out = vrl_null();
			return VRL_OK;
		}
	}

	/* First-sight cache miss: start async resolution and pause the stream.
	 * If we already paused on another name during this pass, just defer (the
	 * record is replayed on resume); names are resolved one at a time. */
	if (!st->dns_suspended) {
		uint64_t neg_cap = st->vn ? st->vn->dns_negative_cache_max : 0;
		st->dns_suspended = 1;
		st->dns_rrtype = rrtype;
		snprintf(st->dns_name, sizeof(st->dns_name), "%s", qname);
		if (reverse)
			vrl_dns_kickoff_reverse(st->carg, arg->u.bytes.data, qname,
						st->dns_negative_ttl_ms, neg_cap);
		else
			vrl_dns_kickoff_forward(st->carg, qname, rrtype,
						st->dns_negative_ttl_ms, neg_cap);
		carglog(st->carg, L_INFO, "vrl: %s('%s') miss, resolving async (paused)\n",
			fname, arg->u.bytes.data);
	}

	*out = vrl_null();
	return VRL_OK;
}

static vrl_status fn_dns_lookup(vrl_call_args *a, vrl_value **out, char **err)
{
	return vrl_dns_common(a, 0, out, err);
}

static vrl_status fn_reverse_dns(vrl_call_args *a, vrl_value **out, char **err)
{
	return vrl_dns_common(a, 1, out, err);
}

void vrl_host_builtins_init(void)
{
	vrl_dns_neg_init();
	vrl_register("dns_lookup", fn_dns_lookup);
	vrl_register("reverse_dns", fn_reverse_dns);
}
