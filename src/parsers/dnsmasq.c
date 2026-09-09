#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/url.h"
#include "common/logs.h"
#include "resolver/dns.h"
#include "resolver/resolver.h"
#include "main.h"

static int dnsmasq_qname_is(const char *qname, const char *want)
{
	size_t wl;

	if (!qname || !want)
		return 0;
	wl = strlen(want);
	if (strncasecmp(qname, want, wl))
		return 0;
	return qname[wl] == '\0' || qname[wl] == '.';
}

static size_t dnsmasq_txt_decode(const char *data, uint16_t datalen, char *out, size_t outsz)
{
	size_t o = 0;
	size_t i = 0;

	if (!data || !out || outsz == 0)
		return 0;
	while (i < datalen && o + 1 < outsz) {
		uint8_t n = (uint8_t)data[i++];
		if ((size_t)i + n > datalen)
			break;
		if (o + n >= outsz)
			n = (uint8_t)(outsz - 1 - o);
		memcpy(out + o, data + i, n);
		o += n;
		i += n;
	}
	out[o] = 0;
	return o;
}

static string *dnsmasq_chaos_query(const char *qname)
{
	dns_t query;
	dns_rr_t question;
	char buf[512];
	int packed;
	static uint16_t txid;

	memset(&query, 0, sizeof(query));
	memset(&question, 0, sizeof(question));
	query.hdr.transaction_id = ++txid ? txid : ++txid;
	query.hdr.qr = DNS_QUERY;
	query.hdr.rd = 1;
	query.hdr.nquestion = 1;
	strlcpy(question.name, qname, DNS_NAME_MAXLEN);
	question.rtype = DNS_TYPE_TXT;
	question.rclass = DNS_CLASS_CHAOS;
	query.questions = &question;

	packed = dns_pack(&query, buf, (int)sizeof(buf));
	if (packed <= (int)sizeof(dnshdr_t))
		return NULL;
	return string_init_alloc(buf, (size_t)packed);
}

#define DNSMASQ_MESG(fn, qname) \
string *fn(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) \
{ \
	(void)arg; \
	(void)env; \
	(void)proxy_settings; \
	if (!hi || (hi->proto != APROTO_UDP && hi->transport != APROTO_UDP)) \
		return NULL; \
	return dnsmasq_chaos_query(qname); \
}

DNSMASQ_MESG(dnsmasq_mesg, "cachesize.bind")
DNSMASQ_MESG(dnsmasq_mesg_insertions, "insertions.bind")
DNSMASQ_MESG(dnsmasq_mesg_evictions, "evictions.bind")
DNSMASQ_MESG(dnsmasq_mesg_misses, "misses.bind")
DNSMASQ_MESG(dnsmasq_mesg_hits, "hits.bind")
DNSMASQ_MESG(dnsmasq_mesg_auth, "auth.bind")
DNSMASQ_MESG(dnsmasq_mesg_servers, "servers.bind")

static void dnsmasq_metric_help(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "dnsmasq_cachesize", METRIC_TYPE_GAUGE, "dnsmasq DNS cache size.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_insertions", METRIC_TYPE_COUNTER, "dnsmasq cache insertions.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_evictions", METRIC_TYPE_COUNTER, "dnsmasq cache evictions.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_misses", METRIC_TYPE_COUNTER, "dnsmasq cache misses.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_hits", METRIC_TYPE_COUNTER, "dnsmasq cache hits.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_auth", METRIC_TYPE_COUNTER, "dnsmasq authoritative queries.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_servers_queries", METRIC_TYPE_COUNTER, "dnsmasq queries forwarded to an upstream server.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_servers_queries_failed", METRIC_TYPE_COUNTER, "dnsmasq failed queries to an upstream server.");
}

static void dnsmasq_emit_txt(context_arg *carg, const char *qname, const char *txt)
{
	uint64_t val;
	char server[128];
	unsigned long long queries = 0, failed = 0;

	if (!txt || !txt[0])
		return;

	if (dnsmasq_qname_is(qname, "servers.bind")) {
		if (sscanf(txt, "%127s %llu %llu", server, &queries, &failed) >= 2) {
			val = queries;
			metric_add_labels("dnsmasq_servers_queries", &val, DATATYPE_UINT, carg, "server", server);
			val = failed;
			metric_add_labels("dnsmasq_servers_queries_failed", &val, DATATYPE_UINT, carg, "server", server);
		}
		return;
	}

	val = strtoull(txt, NULL, 10);
	if (dnsmasq_qname_is(qname, "cachesize.bind"))
		metric_add_auto("dnsmasq_cachesize", &val, DATATYPE_UINT, carg);
	else if (dnsmasq_qname_is(qname, "insertions.bind"))
		metric_add_auto("dnsmasq_insertions", &val, DATATYPE_UINT, carg);
	else if (dnsmasq_qname_is(qname, "evictions.bind"))
		metric_add_auto("dnsmasq_evictions", &val, DATATYPE_UINT, carg);
	else if (dnsmasq_qname_is(qname, "misses.bind"))
		metric_add_auto("dnsmasq_misses", &val, DATATYPE_UINT, carg);
	else if (dnsmasq_qname_is(qname, "hits.bind"))
		metric_add_auto("dnsmasq_hits", &val, DATATYPE_UINT, carg);
	else if (dnsmasq_qname_is(qname, "auth.bind"))
		metric_add_auto("dnsmasq_auth", &val, DATATYPE_UINT, carg);
}

void dnsmasq_handler(char *metrics, size_t size, context_arg *carg)
{
	dns_t resp;
	const char *qname;
	int nparse;
	int i;
	int emitted = 0;
	char txt[256];

	if (!metrics || size < sizeof(dnshdr_t)) {
		carg->parser_status = 0;
		return;
	}

	memset(&resp, 0, sizeof(resp));
	nparse = dns_unpack(metrics, (int)size, &resp);
	if (nparse < 0 || resp.hdr.qr != DNS_RESPONSE || !resp.hdr.nquestion || !resp.questions) {
		dns_free(&resp);
		carg->parser_status = 0;
		return;
	}

	qname = resp.questions[0].name;
	dnsmasq_metric_help(carg);
	for (i = 0; i < resp.hdr.nanswer; ++i) {
		if (resp.answers[i].rtype != DNS_TYPE_TXT)
			continue;
		if (!dnsmasq_txt_decode(resp.answers[i].data, resp.answers[i].datalen, txt, sizeof(txt)))
			continue;
		carglog(carg, L_DEBUG, "dnsmasq CHAOS TXT %s = '%s'\n", qname, txt);
		dnsmasq_emit_txt(carg, qname, txt);
		emitted = 1;
	}
	dns_free(&resp);
	carg->parser_status = emitted ? 1 : 0;
}

void dnsmasq_dhcp_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	char *save = NULL;
	uint64_t total = 0;
	uint64_t active = 0;
	time_t now = time(NULL);
	int found = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	namespace_metric_family_set(NULL, carg, "dnsmasq_dhcp_leases", METRIC_TYPE_GAUGE, "dnsmasq DHCP lease file entries.");
	namespace_metric_family_set(NULL, carg, "dnsmasq_dhcp_leases_active", METRIC_TYPE_GAUGE, "dnsmasq DHCP leases that have not expired.");

	copy = strndup(metrics, size);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}

	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		unsigned long expiry;
		char *p = line;
		while (*p == ' ' || *p == '\t')
			++p;
		if (!*p || *p == '#')
			continue;
		if (!strncmp(p, "duid", 4)) {
			found = 1;
			continue;
		}
		if (!isdigit((unsigned char)*p))
			continue;
		found = 1;
		expiry = strtoul(p, NULL, 10);
		++total;
		if (expiry > (unsigned long)now)
			++active;
	}
	free(copy);

	if (!found) {
		carg->parser_status = 0;
		return;
	}

	metric_add_auto("dnsmasq_dhcp_leases", &total, DATATYPE_UINT, carg);
	metric_add_auto("dnsmasq_dhcp_leases_active", &active, DATATYPE_UINT, carg);
	carg->parser_status = 1;
}

string *dnsmasq_dhcp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)hi;
	(void)arg;
	(void)env;
	(void)proxy_settings;
	return NULL;
}

void dnsmasq_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dnsmasq");
	actx->handlers = 7;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = dnsmasq_handler;
	actx->handler[0].mesg_func = dnsmasq_mesg;
	strlcpy(actx->handler[0].key, "dnsmasq_cachesize", 255);

	actx->handler[1].name = dnsmasq_handler;
	actx->handler[1].mesg_func = dnsmasq_mesg_insertions;
	strlcpy(actx->handler[1].key, "dnsmasq_insertions", 255);

	actx->handler[2].name = dnsmasq_handler;
	actx->handler[2].mesg_func = dnsmasq_mesg_evictions;
	strlcpy(actx->handler[2].key, "dnsmasq_evictions", 255);

	actx->handler[3].name = dnsmasq_handler;
	actx->handler[3].mesg_func = dnsmasq_mesg_misses;
	strlcpy(actx->handler[3].key, "dnsmasq_misses", 255);

	actx->handler[4].name = dnsmasq_handler;
	actx->handler[4].mesg_func = dnsmasq_mesg_hits;
	strlcpy(actx->handler[4].key, "dnsmasq_hits", 255);

	actx->handler[5].name = dnsmasq_handler;
	actx->handler[5].mesg_func = dnsmasq_mesg_auth;
	strlcpy(actx->handler[5].key, "dnsmasq_auth", 255);

	actx->handler[6].name = dnsmasq_handler;
	actx->handler[6].mesg_func = dnsmasq_mesg_servers;
	strlcpy(actx->handler[6].key, "dnsmasq_servers", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void dnsmasq_dhcp_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("dnsmasq_dhcp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = dnsmasq_dhcp_handler;
	actx->handler[0].mesg_func = dnsmasq_dhcp_mesg;
	strlcpy(actx->handler[0].key, "dnsmasq_dhcp", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
