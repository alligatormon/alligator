#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/url.h"
#include "common/logs.h"
#include "main.h"

#define FREERADIUS_VENDOR 24757
#define RADIUS_CODE_ACCESS_ACCEPT 2
#define RADIUS_CODE_ACCESS_REJECT 3
#define RADIUS_CODE_STATUS_SERVER 12
#define RADIUS_ATTR_VENDOR_SPECIFIC 26
#define RADIUS_ATTR_MESSAGE_AUTHENTICATOR 80
#define FREERADIUS_ATTR_STATISTICS_TYPE 127
#define FREERADIUS_STATS_ALL 31
#define RADIUS_HDR_LEN 20

typedef struct {
	uint8_t vsa;
	const char *metric;
	uint8_t gauge;
} freeradius_vsa_map;

static const freeradius_vsa_map freeradius_vsas[] = {
	{ 128, "freeradius_access_requests_total", 0 },
	{ 129, "freeradius_access_accepts_total", 0 },
	{ 130, "freeradius_access_rejects_total", 0 },
	{ 131, "freeradius_access_challenges_total", 0 },
	{ 132, "freeradius_auth_responses_total", 0 },
	{ 133, "freeradius_auth_duplicate_requests_total", 0 },
	{ 134, "freeradius_auth_malformed_requests_total", 0 },
	{ 135, "freeradius_auth_invalid_requests_total", 0 },
	{ 136, "freeradius_auth_dropped_requests_total", 0 },
	{ 137, "freeradius_auth_unknown_types_total", 0 },
	{ 138, "freeradius_proxy_access_requests_total", 0 },
	{ 139, "freeradius_proxy_access_accepts_total", 0 },
	{ 140, "freeradius_proxy_access_rejects_total", 0 },
	{ 141, "freeradius_proxy_access_challenges_total", 0 },
	{ 142, "freeradius_proxy_auth_responses_total", 0 },
	{ 143, "freeradius_proxy_auth_duplicate_requests_total", 0 },
	{ 144, "freeradius_proxy_auth_malformed_requests_total", 0 },
	{ 145, "freeradius_proxy_auth_invalid_requests_total", 0 },
	{ 146, "freeradius_proxy_auth_dropped_requests_total", 0 },
	{ 147, "freeradius_proxy_auth_unknown_types_total", 0 },
	{ 148, "freeradius_accounting_requests_total", 0 },
	{ 149, "freeradius_accounting_responses_total", 0 },
	{ 150, "freeradius_acct_duplicate_requests_total", 0 },
	{ 151, "freeradius_acct_malformed_requests_total", 0 },
	{ 152, "freeradius_acct_invalid_requests_total", 0 },
	{ 153, "freeradius_acct_dropped_requests_total", 0 },
	{ 154, "freeradius_acct_unknown_types_total", 0 },
	{ 155, "freeradius_proxy_accounting_requests_total", 0 },
	{ 156, "freeradius_proxy_accounting_responses_total", 0 },
	{ 157, "freeradius_proxy_acct_duplicate_requests_total", 0 },
	{ 158, "freeradius_proxy_acct_malformed_requests_total", 0 },
	{ 159, "freeradius_proxy_acct_invalid_requests_total", 0 },
	{ 160, "freeradius_proxy_acct_dropped_requests_total", 0 },
	{ 161, "freeradius_proxy_acct_unknown_types_total", 0 },
	{ 162, "freeradius_queue_len_internal", 1 },
	{ 163, "freeradius_queue_len_proxy", 1 },
	{ 164, "freeradius_queue_len_auth", 1 },
	{ 165, "freeradius_queue_len_acct", 1 },
	{ 166, "freeradius_queue_len_detail", 1 },
	{ 176, "freeradius_start_time_seconds", 1 },
	{ 177, "freeradius_hup_time_seconds", 1 },
};

typedef struct {
	const char *section;
	const char *key;
	const char *metric;
} freeradius_text_map;

static const freeradius_text_map freeradius_text_metrics[] = {
	{ "Authentication", "requests", "freeradius_access_requests_total" },
	{ "Authentication", "responses", "freeradius_auth_responses_total" },
	{ "Authentication", "accepts", "freeradius_access_accepts_total" },
	{ "Authentication", "rejects", "freeradius_access_rejects_total" },
	{ "Authentication", "challenges", "freeradius_access_challenges_total" },
	{ "Authentication", "dup", "freeradius_auth_duplicate_requests_total" },
	{ "Authentication", "invalid", "freeradius_auth_invalid_requests_total" },
	{ "Authentication", "malformed", "freeradius_auth_malformed_requests_total" },
	{ "Authentication", "dropped", "freeradius_auth_dropped_requests_total" },
	{ "Authentication", "unknown-types", "freeradius_auth_unknown_types_total" },
	{ "Accounting", "requests", "freeradius_accounting_requests_total" },
	{ "Accounting", "responses", "freeradius_accounting_responses_total" },
	{ "Accounting", "dup", "freeradius_acct_duplicate_requests_total" },
	{ "Accounting", "invalid", "freeradius_acct_invalid_requests_total" },
	{ "Accounting", "malformed", "freeradius_acct_malformed_requests_total" },
	{ "Accounting", "dropped", "freeradius_acct_dropped_requests_total" },
	{ "Accounting", "unknown-types", "freeradius_acct_unknown_types_total" },
	{ "Proxy-Authentication", "requests", "freeradius_proxy_access_requests_total" },
	{ "Proxy-Authentication", "responses", "freeradius_proxy_auth_responses_total" },
	{ "Proxy-Authentication", "accepts", "freeradius_proxy_access_accepts_total" },
	{ "Proxy-Authentication", "rejects", "freeradius_proxy_access_rejects_total" },
	{ "Proxy-Authentication", "challenges", "freeradius_proxy_access_challenges_total" },
	{ "Proxy-Authentication", "dup", "freeradius_proxy_auth_duplicate_requests_total" },
	{ "Proxy-Authentication", "invalid", "freeradius_proxy_auth_invalid_requests_total" },
	{ "Proxy-Authentication", "malformed", "freeradius_proxy_auth_malformed_requests_total" },
	{ "Proxy-Authentication", "dropped", "freeradius_proxy_auth_dropped_requests_total" },
	{ "Proxy-Authentication", "unknown-types", "freeradius_proxy_auth_unknown_types_total" },
	{ "Proxy-Accounting", "requests", "freeradius_proxy_accounting_requests_total" },
	{ "Proxy-Accounting", "responses", "freeradius_proxy_accounting_responses_total" },
	{ "Proxy-Accounting", "dup", "freeradius_proxy_acct_duplicate_requests_total" },
	{ "Proxy-Accounting", "invalid", "freeradius_proxy_acct_invalid_requests_total" },
	{ "Proxy-Accounting", "malformed", "freeradius_proxy_acct_malformed_requests_total" },
	{ "Proxy-Accounting", "dropped", "freeradius_proxy_acct_dropped_requests_total" },
	{ "Proxy-Accounting", "unknown-types", "freeradius_proxy_acct_unknown_types_total" },
};

static void freeradius_help_all(context_arg *carg)
{
	size_t i;
	for (i = 0; i < sizeof(freeradius_vsas) / sizeof(freeradius_vsas[0]); ++i) {
		uint8_t t = freeradius_vsas[i].gauge ? METRIC_TYPE_GAUGE : METRIC_TYPE_COUNTER;
		namespace_metric_family_set(NULL, carg, freeradius_vsas[i].metric, t, "FreeRADIUS exported statistic.");
	}
}

static const char *freeradius_metric_for_vsa(uint8_t vsa)
{
	size_t i;
	for (i = 0; i < sizeof(freeradius_vsas) / sizeof(freeradius_vsas[0]); ++i) {
		if (freeradius_vsas[i].vsa == vsa)
			return freeradius_vsas[i].metric;
	}
	return NULL;
}

static const char *freeradius_metric_for_text(const char *section, const char *key)
{
	size_t i;
	for (i = 0; i < sizeof(freeradius_text_metrics) / sizeof(freeradius_text_metrics[0]); ++i) {
		if (!strcasecmp(freeradius_text_metrics[i].section, section) &&
			!strcasecmp(freeradius_text_metrics[i].key, key))
			return freeradius_text_metrics[i].metric;
	}
	return NULL;
}

static const struct {
	const char *dump;
	const char *metric;
} freeradius_dump_names[] = {
	{ "FreeRADIUS-Total-Access-Requests", "freeradius_access_requests_total" },
	{ "FreeRADIUS-Total-Access-Accepts", "freeradius_access_accepts_total" },
	{ "FreeRADIUS-Total-Access-Rejects", "freeradius_access_rejects_total" },
	{ "FreeRADIUS-Total-Access-Challenges", "freeradius_access_challenges_total" },
	{ "FreeRADIUS-Total-Auth-Responses", "freeradius_auth_responses_total" },
	{ "FreeRADIUS-Total-Auth-Duplicate-Requests", "freeradius_auth_duplicate_requests_total" },
	{ "FreeRADIUS-Total-Auth-Malformed-Requests", "freeradius_auth_malformed_requests_total" },
	{ "FreeRADIUS-Total-Auth-Invalid-Requests", "freeradius_auth_invalid_requests_total" },
	{ "FreeRADIUS-Total-Auth-Dropped-Requests", "freeradius_auth_dropped_requests_total" },
	{ "FreeRADIUS-Total-Auth-Unknown-Types", "freeradius_auth_unknown_types_total" },
	{ "FreeRADIUS-Total-Proxy-Access-Requests", "freeradius_proxy_access_requests_total" },
	{ "FreeRADIUS-Total-Proxy-Access-Accepts", "freeradius_proxy_access_accepts_total" },
	{ "FreeRADIUS-Total-Proxy-Access-Rejects", "freeradius_proxy_access_rejects_total" },
	{ "FreeRADIUS-Total-Proxy-Access-Challenges", "freeradius_proxy_access_challenges_total" },
	{ "FreeRADIUS-Total-Proxy-Auth-Responses", "freeradius_proxy_auth_responses_total" },
	{ "FreeRADIUS-Total-Accounting-Requests", "freeradius_accounting_requests_total" },
	{ "FreeRADIUS-Total-Accounting-Responses", "freeradius_accounting_responses_total" },
	{ "FreeRADIUS-Queue-Len-Internal", "freeradius_queue_len_internal" },
	{ "FreeRADIUS-Queue-Len-Proxy", "freeradius_queue_len_proxy" },
	{ "FreeRADIUS-Queue-Len-Auth", "freeradius_queue_len_auth" },
	{ "FreeRADIUS-Queue-Len-Acct", "freeradius_queue_len_acct" },
	{ "FreeRADIUS-Queue-Len-Detail", "freeradius_queue_len_detail" },
	{ "FreeRADIUS-Stats-Start-Time", "freeradius_start_time_seconds" },
	{ "FreeRADIUS-Stats-HUP-Time", "freeradius_hup_time_seconds" },
};

static uint16_t freeradius_read_u16(const unsigned char *p)
{
	uint16_t v;
	memcpy(&v, p, sizeof(v));
	return ntohs(v);
}

static uint32_t freeradius_read_u32(const unsigned char *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return ntohl(v);
}

static void freeradius_write_u16(unsigned char *p, uint16_t v)
{
	uint16_t n = htons(v);
	memcpy(p, &n, sizeof(n));
}

static void freeradius_write_u32(unsigned char *p, uint32_t v)
{
	uint32_t n = htonl(v);
	memcpy(p, &n, sizeof(n));
}

static void freeradius_emit(context_arg *carg, const char *metric, uint64_t val)
{
	if (!metric)
		return;
	metric_add_auto((char *)metric, &val, DATATYPE_UINT, carg);
}

static int freeradius_parse_vsa_body(context_arg *carg, const unsigned char *p, size_t len)
{
	size_t off = 0;
	int n = 0;

	while (off + 2 <= len) {
		uint8_t vtype = p[off];
		uint8_t vlen = p[off + 1];
		const char *metric;
		if (vlen < 2 || off + vlen > len)
			break;
		metric = freeradius_metric_for_vsa(vtype);
		if (metric && vlen >= 6) {
			uint64_t val = freeradius_read_u32(p + off + 2);
			freeradius_emit(carg, metric, val);
			++n;
		}
		off += vlen;
	}
	return n;
}

static int freeradius_parse_radius(const unsigned char *pkt, size_t size, context_arg *carg)
{
	uint16_t rlen;
	size_t off;
	int n = 0;

	if (size < RADIUS_HDR_LEN)
		return 0;
	if (pkt[0] != RADIUS_CODE_ACCESS_ACCEPT && pkt[0] != RADIUS_CODE_ACCESS_REJECT &&
		pkt[0] != RADIUS_CODE_STATUS_SERVER)
		return 0;
	rlen = freeradius_read_u16(pkt + 2);
	if (rlen < RADIUS_HDR_LEN || rlen > size)
		return 0;

	freeradius_help_all(carg);
	off = RADIUS_HDR_LEN;
	while (off + 2 <= rlen) {
		uint8_t type = pkt[off];
		uint8_t alen = pkt[off + 1];
		if (alen < 2 || off + alen > rlen)
			break;
		if (type == RADIUS_ATTR_VENDOR_SPECIFIC && alen >= 8) {
			uint32_t vendor = freeradius_read_u32(pkt + off + 2);
			if (vendor == FREERADIUS_VENDOR)
				n += freeradius_parse_vsa_body(carg, pkt + off + 6, (size_t)alen - 6);
		}
		off += alen;
	}
	return n > 0;
}

static const char *freeradius_section_from_line(const char *p)
{
	if (strstr(p, "Proxy-Authentication") || strstr(p, "Proxy-Auth"))
		return "Proxy-Authentication";
	if (strstr(p, "Proxy-Accounting") || strstr(p, "Proxy-Acct"))
		return "Proxy-Accounting";
	if (strstr(p, "Authentication"))
		return "Authentication";
	if (strstr(p, "Accounting"))
		return "Accounting";
	return NULL;
}

static int freeradius_parse_attr_line(context_arg *carg, const char *p)
{
	const char *eq;
	char name[128];
	size_t nlen;
	uint64_t val;
	size_t i;

	eq = strchr(p, '=');
	if (!eq)
		return 0;
	while (*p == ' ' || *p == '\t' || *p == '|')
		++p;
	nlen = (size_t)(eq - p);
	while (nlen && (p[nlen - 1] == ' ' || p[nlen - 1] == '\t'))
		--nlen;
	if (nlen >= sizeof(name) || nlen == 0)
		return 0;
	memcpy(name, p, nlen);
	name[nlen] = 0;
	val = strtoull(eq + 1, NULL, 10);

	for (i = 0; i < sizeof(freeradius_dump_names) / sizeof(freeradius_dump_names[0]); ++i) {
		if (!strcasecmp(name, freeradius_dump_names[i].dump)) {
			freeradius_emit(carg, freeradius_dump_names[i].metric, val);
			return 1;
		}
	}
	return 0;
}

static int freeradius_parse_text(char *copy, context_arg *carg)
{
	char *save = NULL;
	const char *section = "Authentication";
	int n = 0;

	freeradius_help_all(carg);
	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *p = line;
		char *colon;
		char key[64];
		char *eq;
		const char *metric;
		const char *sec;
		uint64_t val;
		size_t klen;

		while (*p == ' ' || *p == '\t' || *p == '|' || *p == '`' || *p == '-')
			++p;
		if (!*p)
			continue;

		sec = freeradius_section_from_line(p);
		colon = strchr(p, ':');
		if (sec && colon && !strchr(p, '=')) {
			section = sec;
			continue;
		}

		if (strstr(p, "FreeRADIUS-")) {
			if (freeradius_parse_attr_line(carg, p))
				++n;
			continue;
		}

		eq = strchr(p, '=');
		if (!eq)
			continue;
		klen = (size_t)(eq - p);
		while (klen && (p[klen - 1] == ' ' || p[klen - 1] == '\t'))
			--klen;
		if (!klen || klen >= sizeof(key))
			continue;
		memcpy(key, p, klen);
		key[klen] = 0;
		if (strpbrk(eq + 1, "ms")) {
			/* elapsed times like "1m 2s" are skipped */
			if (!isdigit((unsigned char)*(eq + 1 + strspn(eq + 1, " \t"))))
				continue;
			if (strchr(eq + 1, 'm') || strchr(eq + 1, 's')) {
				char *end = NULL;
				strtoull(eq + 1, &end, 10);
				if (end && (*end == 'm' || *end == 's'))
					continue;
			}
		}
		val = strtoull(eq + 1, NULL, 10);
		metric = freeradius_metric_for_text(section, key);
		if (metric) {
			freeradius_emit(carg, metric, val);
			++n;
		}
	}
	return n > 0;
}

void freeradius_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	if ((unsigned char)metrics[0] < 32 && size >= RADIUS_HDR_LEN) {
		carg->parser_status = freeradius_parse_radius((const unsigned char *)metrics, size, carg) ? 1 : 0;
		return;
	}

	{
		char *copy = strndup(metrics, size);
		int ok;
		if (!copy) {
			carg->parser_status = 0;
			return;
		}
		ok = freeradius_parse_text(copy, carg);
		free(copy);
		carg->parser_status = ok ? 1 : 0;
	}
}

string *freeradius_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	unsigned char *pkt;
	const char *secret = "";
	unsigned int hmac_len = 16;
	uint16_t plen = 50;

	(void)arg;
	(void)env;
	(void)proxy_settings;

	if (!hi || (hi->proto != APROTO_UDP && hi->transport != APROTO_UDP))
		return NULL;
	if (hi->pass && hi->pass[0])
		secret = hi->pass;
	else if (hi->user && hi->user[0])
		secret = hi->user;

	/* Status-Server + VSA Statistics-Type + Message-Authenticator. +1 for string NUL. */
	pkt = calloc(1, (size_t)plen + 1);
	if (!pkt)
		return NULL;

	pkt[0] = RADIUS_CODE_STATUS_SERVER;
	pkt[1] = 1;
	freeradius_write_u16(pkt + 2, plen);
	if (RAND_bytes(pkt + 4, 16) != 1) {
		size_t i;
		for (i = 0; i < 16; ++i)
			pkt[4 + i] = (unsigned char)(0xA5 ^ (unsigned char)i);
	}

	/* Vendor-Specific FreeRADIUS-Statistics-Type = All (31) */
	pkt[20] = RADIUS_ATTR_VENDOR_SPECIFIC;
	pkt[21] = 12;
	freeradius_write_u32(pkt + 22, FREERADIUS_VENDOR);
	pkt[26] = FREERADIUS_ATTR_STATISTICS_TYPE;
	pkt[27] = 6;
	freeradius_write_u32(pkt + 28, FREERADIUS_STATS_ALL);

	/* Message-Authenticator (zeros, then HMAC-MD5 of the packet) */
	pkt[32] = RADIUS_ATTR_MESSAGE_AUTHENTICATOR;
	pkt[33] = 18;
	HMAC(EVP_md5(), secret, (int)strlen(secret), pkt, plen, pkt + 34, &hmac_len);

	return string_init_add((char *)pkt, plen, plen);
}

void freeradius_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("freeradius");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = freeradius_handler;
	actx->handler[0].mesg_func = freeradius_mesg;
	strlcpy(actx->handler[0].key, "freeradius", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
