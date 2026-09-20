#include "stdlib.h"
#include "string.h"
#include <strings.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "probe/probe.h"
#include "metric/labels.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "common/http.h"
#include "common/json_query.h"
#include "common/logs.h"
#include "common/url.h"
#include "common/units.h"
#include "common/rtime.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include "metric/metrictree.h"
#include "main.h"

static const double probe_hist_le[PROBE_HIST_BUCKETS] = {
	0.00005, 0.0001, 0.0002, 0.0005,
	0.001, 0.002, 0.005, 0.01, 0.02, 0.05,
	0.1, 0.2, 0.5, 1, 2, 5, 10, 30
};

const double *probe_histogram_bounds(uint8_t *n)
{
	if (n)
		*n = PROBE_HIST_BUCKETS;
	return probe_hist_le;
}

static pcre *probe_compile_re(const char *pat)
{
	const char *err;
	int erroffset;

	if (!pat || !*pat)
		return NULL;
	return pcre_compile(pat, 0, &err, &erroffset, NULL);
}

static char **probe_json_str_list(json_t *j, uint64_t *out_size)
{
	uint64_t n;
	uint64_t k = 0;
	char **out;

	if (!j || !out_size)
		return NULL;

	if (json_is_string(j)) {
		const char *s = json_string_value(j);
		if (!s)
			return NULL;
		out = calloc(1, sizeof(*out));
		if (!out)
			return NULL;
		out[0] = strdup(s);
		*out_size = 1;
		return out;
	}

	if (!json_is_array(j))
		return NULL;

	n = json_array_size(j);
	if (!n)
		return NULL;

	out = calloc(n, sizeof(*out));
	if (!out)
		return NULL;

	for (uint64_t i = 0; i < n; i++) {
		const char *s = json_string_value(json_array_get(j, i));
		if (s)
			out[k++] = strdup(s);
	}

	if (!k) {
		free(out);
		return NULL;
	}

	*out_size = k;
	return out;
}

static void probe_free_str_array(char **arr, uint64_t n)
{
	if (!arr)
		return;
	for (uint64_t i = 0; i < n; i++)
		free(arr[i]);
	free(arr);
}

static void probe_free_pcre_array(pcre **arr, uint64_t n)
{
	if (!arr)
		return;
	for (uint64_t i = 0; i < n; i++) {
		if (arr[i])
			pcre_free(arr[i]);
	}
	free(arr);
}

static pcre **probe_compile_re_array(char **pats, uint64_t n)
{
	pcre **out;
	uint64_t i;

	if (!pats || !n)
		return NULL;
	out = calloc(n, sizeof(*out));
	if (!out)
		return NULL;
	for (i = 0; i < n; i++)
		out[i] = probe_compile_re(pats[i]);
	return out;
}

static uint64_t probe_timeout_from_json(json_t *probe)
{
	json_t *jt = json_object_get(probe, "timeout");
	if (!jt)
		return 5000;

	if (json_typeof(jt) == JSON_STRING) {
		const char *s = json_string_value(jt);
		if (s && strpbrk(s, "smhdwSMHDW"))
			return (uint64_t)get_ms_from_human_range(s, json_string_length(jt));
		return s ? strtoull(s, NULL, 10) : 5000;
	}
	if (json_typeof(jt) == JSON_REAL)
		return (uint64_t)json_real_value(jt);
	return (uint64_t)json_integer_value(jt);
}

static uint64_t probe_ms_field(json_t *probe, const char *key)
{
	json_t *jt = json_object_get(probe, key);
	if (!jt)
		return 0;
	if (json_typeof(jt) == JSON_STRING) {
		const char *s = json_string_value(jt);
		if (!s)
			return 0;
		if (strpbrk(s, "smhdwSMHDW"))
			return (uint64_t)get_ms_from_human_range(s, json_string_length(jt));
		return strtoull(s, NULL, 10);
	}
	if (json_typeof(jt) == JSON_REAL)
		return (uint64_t)json_real_value(jt);
	return (uint64_t)json_integer_value(jt);
}

static uint8_t probe_ip_version_from_json(json_t *probe)
{
	json_t *jip = json_object_get(probe, "probe_ip_version");
	if (!jip)
		jip = json_object_get(probe, "preferred_ip_protocol");
	if (!jip)
		jip = json_object_get(probe, "network");
	if (!jip)
		return 0;

	if (json_is_integer(jip)) {
		int64_t v = json_integer_value(jip);
		if (v == 4 || v == 6)
			return (uint8_t)v;
		return 0;
	}

	if (json_is_string(jip)) {
		const char *s = json_string_value(jip);
		if (!s)
			return 0;
		if (!strcasecmp(s, "ip6") || !strcasecmp(s, "ipv6") || !strcmp(s, "6"))
			return 6;
		if (!strcasecmp(s, "ip4") || !strcasecmp(s, "ipv4") || !strcmp(s, "4"))
			return 4;
	}
	return 0;
}

static int probe_json_is_on(json_t *j)
{
	const char *s;

	if (!j)
		return 0;
	if (json_is_true(j))
		return 1;
	if (json_is_integer(j))
		return json_integer_value(j) != 0;
	s = json_string_value(j);
	return s && (!strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "1"));
}

static uint8_t probe_method_from_str(const char *method)
{
	if (!method)
		return HTTP_GET;
	if (!strcasecmp(method, "POST"))
		return HTTP_POST;
	if (!strcasecmp(method, "HEAD"))
		return HTTP_HEAD;
	if (!strcasecmp(method, "PUT"))
		return HTTP_PUT;
	if (!strcasecmp(method, "DELETE"))
		return HTTP_DELETE;
	if (!strcasecmp(method, "PATCH"))
		return HTTP_PATCH;
	return HTTP_GET;
}

static void probe_qr_step_clear(probe_qr_step *st)
{
	if (!st)
		return;
	free(st->expect);
	if (st->expect_re)
		pcre_free(st->expect_re);
	free(st->send);
	free(st->expect_bytes);
}

static void probe_header_match_clear(probe_header_match *hm)
{
	if (!hm)
		return;
	free(hm->header);
	free(hm->regexp);
	if (hm->re)
		pcre_free(hm->re);
}

static void probe_json_check_clear(probe_json_check *jc)
{
	if (!jc)
		return;
	free(jc->path);
	free(jc->regexp);
	if (jc->re)
		pcre_free(jc->re);
}

static void probe_qr_step_from_obj(probe_qr_step *st, json_t *obj)
{
	const char *s;

	if (!st || !obj || !json_is_object(obj))
		return;
	s = json_string_value(json_object_get(obj, "expect"));
	if (s) {
		st->expect = strdup(s);
		st->expect_re = probe_compile_re(s);
	}
	s = json_string_value(json_object_get(obj, "send"));
	if (s)
		st->send = strdup(s);
	s = json_string_value(json_object_get(obj, "expect_bytes"));
	if (s) {
		st->expect_bytes = strdup(s);
		st->expect_bytes_len = strlen(s);
	}
	st->starttls = probe_json_is_on(json_object_get(obj, "starttls")) ? 1 : 0;
}

static probe_qr_step *probe_parse_query_response(json_t *j, uint64_t *out_n)
{
	probe_qr_step *out;
	uint64_t n;
	uint64_t i;

	if (!j || !out_n)
		return NULL;

	if (json_is_object(j)) {
		out = calloc(1, sizeof(*out));
		if (!out)
			return NULL;
		probe_qr_step_from_obj(out, j);
		*out_n = 1;
		return out;
	}
	if (!json_is_array(j))
		return NULL;
	n = json_array_size(j);
	if (!n)
		return NULL;
	out = calloc(n, sizeof(*out));
	if (!out)
		return NULL;
	for (i = 0; i < n; i++)
		probe_qr_step_from_obj(&out[i], json_array_get(j, i));
	*out_n = n;
	return out;
}

static void probe_header_match_from_obj(probe_header_match *hm, json_t *obj)
{
	const char *s;

	if (!hm || !obj)
		return;
	if (json_is_string(obj)) {
		s = json_string_value(obj);
		if (!s)
			return;
		hm->header = strdup(s);
		return;
	}
	if (!json_is_object(obj))
		return;
	s = json_string_value(json_object_get(obj, "header"));
	if (s)
		hm->header = strdup(s);
	s = json_string_value(json_object_get(obj, "regexp"));
	if (!s)
		s = json_string_value(json_object_get(obj, "regex"));
	if (s) {
		hm->regexp = strdup(s);
		hm->re = probe_compile_re(s);
	}
	hm->allow_missing = probe_json_is_on(json_object_get(obj, "allow_missing")) ? 1 : 0;
}

static probe_header_match *probe_parse_header_matches(json_t *j, uint64_t *out_n)
{
	probe_header_match *out;
	uint64_t n;
	uint64_t i;

	if (!j || !out_n)
		return NULL;
	if (json_is_object(j) || json_is_string(j)) {
		out = calloc(1, sizeof(*out));
		if (!out)
			return NULL;
		probe_header_match_from_obj(out, j);
		*out_n = 1;
		return out;
	}
	if (!json_is_array(j))
		return NULL;
	n = json_array_size(j);
	if (!n)
		return NULL;
	out = calloc(n, sizeof(*out));
	if (!out)
		return NULL;
	for (i = 0; i < n; i++)
		probe_header_match_from_obj(&out[i], json_array_get(j, i));
	*out_n = n;
	return out;
}

static void probe_json_check_from_obj(probe_json_check *jc, json_t *obj, uint8_t must_exist)
{
	const char *s;

	if (!jc || !obj)
		return;
	jc->must_exist = must_exist;
	if (json_is_string(obj)) {
		s = json_string_value(obj);
		if (s)
			jc->path = strdup(s);
		return;
	}
	if (!json_is_object(obj))
		return;
	s = json_string_value(json_object_get(obj, "path"));
	if (!s)
		s = json_string_value(json_object_get(obj, "jpath"));
	if (s)
		jc->path = strdup(s);
	s = json_string_value(json_object_get(obj, "regexp"));
	if (!s)
		s = json_string_value(json_object_get(obj, "regex"));
	if (s) {
		jc->regexp = strdup(s);
		jc->re = probe_compile_re(s);
	}
}

static probe_json_check *probe_parse_json_checks(json_t *exists, json_t *matches, uint64_t *out_n)
{
	uint64_t n = 0;
	uint64_t i;
	uint64_t k = 0;
	probe_json_check *out;

	if (json_is_array(exists))
		n += json_array_size(exists);
	else if (exists)
		n += 1;
	if (json_is_array(matches))
		n += json_array_size(matches);
	else if (matches)
		n += 1;
	if (!n || !out_n)
		return NULL;
	out = calloc(n, sizeof(*out));
	if (!out)
		return NULL;
	if (json_is_array(exists)) {
		for (i = 0; i < json_array_size(exists); i++)
			probe_json_check_from_obj(&out[k++], json_array_get(exists, i), 1);
	} else if (exists) {
		probe_json_check_from_obj(&out[k++], exists, 1);
	}
	if (json_is_array(matches)) {
		for (i = 0; i < json_array_size(matches); i++)
			probe_json_check_from_obj(&out[k++], json_array_get(matches, i), 0);
	} else if (matches) {
		probe_json_check_from_obj(&out[k++], matches, 0);
	}
	*out_n = k;
	return out;
}

static void probe_node_free(probe_node *pn)
{
	uint64_t i;

	if (!pn)
		return;
	free(pn->name);
	free(pn->scheme);
	free(pn->url);
	free(pn->query_type);
	free(pn->query_name);
	free(pn->body);
	free(pn->body_file);
	probe_free_str_array(pn->valid_status_codes, pn->valid_status_codes_size);
	probe_free_str_array(pn->fail_if_body_matches_regexp, pn->fail_if_body_matches_regexp_size);
	probe_free_pcre_array(pn->fail_if_body_matches_re, pn->fail_if_body_matches_regexp_size);
	probe_free_str_array(pn->fail_if_body_not_matches_regexp, pn->fail_if_body_not_matches_regexp_size);
	probe_free_pcre_array(pn->fail_if_body_not_matches_re, pn->fail_if_body_not_matches_regexp_size);
	probe_free_str_array(pn->valid_rcodes, pn->valid_rcodes_size);
	probe_free_str_array(pn->fail_if_answer_matches_regexp, pn->fail_if_answer_matches_regexp_size);
	probe_free_pcre_array(pn->fail_if_answer_matches_re, pn->fail_if_answer_matches_regexp_size);
	probe_free_str_array(pn->fail_if_answer_not_matches_regexp, pn->fail_if_answer_not_matches_regexp_size);
	probe_free_pcre_array(pn->fail_if_answer_not_matches_re, pn->fail_if_answer_not_matches_regexp_size);
	free(pn->payload);
	if (pn->query_response) {
		for (i = 0; i < pn->query_response_size; i++)
			probe_qr_step_clear(&pn->query_response[i]);
		free(pn->query_response);
	}
	if (pn->fail_if_header_matches) {
		for (i = 0; i < pn->fail_if_header_matches_size; i++)
			probe_header_match_clear(&pn->fail_if_header_matches[i]);
		free(pn->fail_if_header_matches);
	}
	if (pn->fail_if_header_not_matches) {
		for (i = 0; i < pn->fail_if_header_not_matches_size; i++)
			probe_header_match_clear(&pn->fail_if_header_not_matches[i]);
		free(pn->fail_if_header_not_matches);
	}
	if (pn->json_checks) {
		for (i = 0; i < pn->json_checks_size; i++)
			probe_json_check_clear(&pn->json_checks[i]);
		free(pn->json_checks);
	}
	free(pn->source_ip_address);
	free(pn->ca_file);
	free(pn->cert_file);
	free(pn->key_file);
	free(pn->server_name);
	free(pn->http_proxy_url);
	if (pn->labels)
		labels_hash_free(pn->labels);
	if (pn->metricstransform)
		json_decref(pn->metricstransform);
	free(pn->metric_name_transform_pattern);
	free(pn->metric_name_transform_replacement);
	if (pn->metric_name_transform_compiled)
		pcre_free(pn->metric_name_transform_compiled);
	if (pn->env)
		env_free(pn->env);
	free(pn);
}

probe_node* probe_from_carg(context_arg *carg)
{
	if (!carg || !carg->name)
		return NULL;
	return probe_get(carg->name);
}

void probe_apply_negative(context_arg *carg, uint64_t *val)
{
	probe_node *pn;

	if (!val)
		return;
	if (carg && carg->negative_test) {
		*val = *val ? 0 : 1;
		return;
	}
	pn = probe_from_carg(carg);
	if (pn && pn->negative_test)
		*val = *val ? 0 : 1;
}

static void probe_emit_alias_u64(context_arg *carg, const char *name, uint64_t val, probe_node *pn)
{
	char *host = carg->host;
	char *key = carg->key ? carg->key : carg->host;
	char *prober = pn && pn->prober_str ? pn->prober_str : "blackbox";
	char *module = pn && pn->name ? pn->name : "blackbox";

	metric_add_labels5((char *)name, &val, DATATYPE_UINT, carg, "host", host, "key", key, "prober", prober, "module", module, "type", "blackbox");
}

static void probe_emit_alias_dbl(context_arg *carg, const char *name, double val, probe_node *pn)
{
	char *host = carg->host;
	char *key = carg->key ? carg->key : carg->host;
	char *prober = pn && pn->prober_str ? pn->prober_str : "blackbox";
	char *module = pn && pn->name ? pn->name : "blackbox";

	metric_add_labels5((char *)name, &val, DATATYPE_DOUBLE, carg, "host", host, "key", key, "prober", prober, "module", module, "type", "blackbox");
}

static uint64_t probe_resolved_ip_protocol(context_arg *carg)
{
	if (!carg)
		return 4;
	if (carg->ping_addr.ss_family == AF_INET6 || carg->ping_ipv6)
		return 6;
	if (carg->ping_addr.ss_family == AF_INET)
		return 4;
	if (carg->remote_addr.sin_family == AF_INET6)
		return 6;
	if (carg->remote_addr.sin_family == AF_INET)
		return 4;
	if (carg->ip_version == 6)
		return 6;
	if (carg->ip_version == 4)
		return 4;
	return 4;
}

static void probe_emit_native_aux(context_arg *carg, probe_node *pn)
{
	uint64_t resolve_us = getrtime_mcs(carg->resolve_time, carg->resolve_time_finish, 0);
	double timeout_s = carg->timeout ? (carg->timeout / 1000.0) : (pn ? pn->timeout / 1000.0 : 5.0);
	uint64_t ipproto = probe_resolved_ip_protocol(carg);

	namespace_metric_family_set(NULL, carg, "alligator_probe_timeout_seconds", METRIC_TYPE_GAUGE, "Configured probe timeout in seconds.");
	namespace_metric_family_set(NULL, carg, "alligator_probe_ip_protocol", METRIC_TYPE_GAUGE, "Resolved IP protocol (4 or 6) from socket or getaddrinfo family.");
	probe_emit_alias_dbl(carg, "alligator_probe_timeout_seconds", timeout_s, pn);
	probe_emit_alias_u64(carg, "alligator_probe_ip_protocol", ipproto, pn);

	/* ICMP emits alligator_dns_resolve_duration_seconds from icmp_metrics with type=icmp labels. */
	if (resolve_us && !(pn && pn->prober == APROTO_ICMP)) {
		double resolve_s = resolve_us / 1000000.0;
		namespace_metric_family_set(NULL, carg, "alligator_dns_resolve_duration_seconds", METRIC_TYPE_GAUGE, "DNS resolve duration in seconds.");
		probe_emit_alias_dbl(carg, "alligator_dns_resolve_duration_seconds", resolve_s, pn);
	}
}

void probe_metric_success(context_arg *carg, uint64_t val)
{
	probe_node *pn = probe_from_carg(carg);
	char *host;
	char *key;
	char *prober;

	if (!pn)
		return;

	probe_apply_negative(carg, &val);

	host = carg->host;
	key = carg->key ? carg->key : carg->host;
	prober = pn->prober_str ? pn->prober_str : "blackbox";
	metric_add_labels6("probe_success", &val, DATATYPE_UINT, carg, "proto", prober, "type", "blackbox", "host", host, "key", key, "prober", prober, "module", pn->name);

	if (carg->probe_regex_fail) {
		uint64_t one = 1;
		namespace_metric_family_set(NULL, carg, "probe_failed_due_to_regex", METRIC_TYPE_GAUGE, "Set when a TCP/HTTP regex expect or body matcher failed.");
		probe_emit_alias_u64(carg, "probe_failed_due_to_regex", one, pn);
	}

	probe_emit_native_aux(carg, pn);
}

int probe_ip_family(const char *host, uint8_t ip_version)
{
	char buf[INET6_ADDRSTRLEN];
	struct in6_addr a6;
	struct in_addr a4;
	const char *p = host;
	size_t n;

	if (ip_version == 6)
		return AF_INET6;
	if (ip_version == 4)
		return AF_INET;
	if (!host || !host[0])
		return AF_UNSPEC;
	if (host[0] == '[') {
		const char *end = strchr(host, ']');
		if (end && (size_t)(end - host - 1) < sizeof(buf)) {
			n = (size_t)(end - host - 1);
			memcpy(buf, host + 1, n);
			buf[n] = 0;
			p = buf;
		}
	}
	if (inet_pton(AF_INET6, p, &a6) == 1)
		return AF_INET6;
	if (inet_pton(AF_INET, p, &a4) == 1)
		return AF_INET;
	return AF_UNSPEC;
}

void probe_copy_opts_to_carg(context_arg *carg, probe_node *pn)
{
	if (!carg || !pn)
		return;
	if (pn->icmp_payload_size)
		carg->icmp_payload_size = pn->icmp_payload_size;
	if (pn->icmp_ttl)
		carg->icmp_ttl = pn->icmp_ttl;
	if (pn->icmp_tos)
		carg->icmp_tos = pn->icmp_tos;
	if (pn->icmp_interval) {
		carg->icmp_interval_ms = pn->icmp_interval;
		carg->icmp_continuous = 1;
	}
	carg->negative_test = pn->negative_test;
	if (pn->loop)
		carg->pingloop = pn->loop;
	if (pn->loop)
		carg->pingpercent_success = pn->percent_success;
	if (pn->payload) {
		free(carg->probe_payload);
		carg->probe_payload = strdup(pn->payload);
		carg->probe_payload_len = strlen(pn->payload);
		if ((carg->transport == APROTO_UDP || pn->prober == APROTO_UDP) && !carg->mesg_len) {
			char *p = strdup(pn->payload);
			size_t n = carg->probe_payload_len;
			carg->mesg = p;
			carg->mesg_len = n;
			carg->request_buffer = uv_buf_init(p, n);
			if (carg->buffer) {
				carg->buffer->base = p;
				carg->buffer->len = n;
			}
			carg->write = 1;
		}
	}
}

int probe_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((probe_node*)obj)->name;
	return strcmp(s1, s2);
}

probe_node* probe_get(char *name)
{
	probe_node *pn;

	if (!ac || !ac->probe || !name)
		return NULL;

	pn = alligator_ht_search(ac->probe, probe_compare, name, tommy_strhash_u32(0, name));
	if (pn)
		return pn;
	else
		return NULL;
}

int probe_qr_active(context_arg *carg)
{
	probe_node *pn = probe_from_carg(carg);
	return pn && pn->query_response_size && !carg->probe_qr_done;
}

static int probe_bytes_find(const char *h, size_t hn, const char *n, size_t nn)
{
	size_t i;

	if (!n || !nn)
		return 1;
	if (!h || hn < nn)
		return 0;
	for (i = 0; i + nn <= hn; i++) {
		if (!memcmp(h + i, n, nn))
			return 1;
	}
	return 0;
}

static int probe_qr_step_matched(probe_qr_step *st, const char *buf, size_t n)
{
	int ovector[30];

	if (!st)
		return 1;
	if (st->expect_bytes && st->expect_bytes_len)
		return probe_bytes_find(buf, n, st->expect_bytes, st->expect_bytes_len);
	if (st->expect_re)
		return pcre_exec(st->expect_re, NULL, buf, (int)n, 0, 0, ovector, 30) >= 0;
	if (st->expect)
		return 0;
	return 1;
}

static int probe_qr_step_needs_read(probe_qr_step *st)
{
	return st && (st->expect || (st->expect_bytes && st->expect_bytes_len));
}

static void probe_qr_finish_step_actions(context_arg *carg, probe_qr_step *st, probe_qr_write_cb wr, probe_qr_starttls_cb stls)
{
	if (st->starttls && stls && !carg->tls_handshake_done)
		stls(carg);
	if (st->send && wr && (!st->starttls || carg->tls_handshake_done))
		wr(carg, st->send, strlen(st->send));
}

static int probe_qr_pump_sends(context_arg *carg, probe_node *pn, probe_qr_write_cb wr, probe_qr_starttls_cb stls)
{
	if (carg->tls && !carg->tls_handshake_done)
		return 0;
	while (carg->probe_qr_idx < pn->query_response_size) {
		probe_qr_step *st = &pn->query_response[carg->probe_qr_idx];
		if (probe_qr_step_needs_read(st))
			return 0;
		probe_qr_finish_step_actions(carg, st, wr, stls);
		if (st->starttls && carg->tls && !carg->tls_handshake_done)
			return 0;
		carg->probe_qr_idx++;
	}
	probe_metric_success(carg, 1);
	carg->probe_qr_done = 1;
	return 1;
}

int probe_qr_on_connect(context_arg *carg, probe_qr_write_cb wr, probe_qr_starttls_cb stls)
{
	probe_node *pn = probe_from_carg(carg);

	if (!pn || !pn->query_response_size)
		return 0;
	carg->probe_qr_idx = 0;
	carg->probe_qr_done = 0;
	probe_qr_pump_sends(carg, pn, wr, stls);
	return 1;
}

int probe_qr_on_tls_ready(context_arg *carg, probe_qr_write_cb wr, probe_qr_starttls_cb stls)
{
	probe_node *pn = probe_from_carg(carg);

	if (!pn || !pn->query_response_size)
		return 0;
	return probe_qr_pump_sends(carg, pn, wr, stls);
}

int probe_qr_on_read(context_arg *carg, const char *data, size_t n, probe_qr_write_cb wr, probe_qr_starttls_cb stls)
{
	probe_node *pn = probe_from_carg(carg);
	probe_qr_step *st;
	const char *buf;
	size_t buflen;

	if (!pn || !pn->query_response_size)
		return 0;
	if (data && n && carg->full_body)
		string_cat(carg->full_body, (char *)data, n);
	if (carg->probe_qr_idx >= pn->query_response_size) {
		probe_metric_success(carg, 1);
		carg->probe_qr_done = 1;
		return 1;
	}
	st = &pn->query_response[carg->probe_qr_idx];
	buf = carg->full_body ? carg->full_body->s : data;
	buflen = carg->full_body ? carg->full_body->l : n;
	if (probe_qr_step_needs_read(st) && !probe_qr_step_matched(st, buf, buflen))
		return 0;

	if (st->expect) {
		uint64_t one = 1;
		namespace_metric_family_set(NULL, carg, "probe_expect_info", METRIC_TYPE_GAUGE, "TCP query_response expect step matched.");
		metric_add_labels6("probe_expect_info", &one, DATATYPE_UINT, carg,
			"regexp", st->expect, "host", carg->host, "key", carg->key ? carg->key : carg->host,
			"prober", pn->prober_str ? pn->prober_str : "tcp", "module", pn->name, "type", "blackbox");
	}

	probe_qr_finish_step_actions(carg, st, wr, stls);
	carg->probe_qr_idx++;
	if (st->starttls && carg->tls && !carg->tls_handshake_done)
		return 0;
	if (probe_qr_pump_sends(carg, pn, wr, stls))
		return 1;
	return 0;
}

string *probe_http_request_body(probe_node *pn)
{
	if (!pn)
		return NULL;
	if (pn->body_file && pn->body_file[0])
		return get_file_content(pn->body_file, 1);
	if (pn->body)
		return string_init_dup(pn->body);
	return NULL;
}

int probe_http_eval_body(probe_node *pn, const char *body, size_t body_size, uint64_t *val)
{
	int ovector[30];
	uint64_t i;

	if (!pn || !val)
		return 0;
	if (!body)
		body = "";
	for (i = 0; i < pn->fail_if_body_matches_regexp_size; i++) {
		pcre *re = pn->fail_if_body_matches_re ? pn->fail_if_body_matches_re[i] : NULL;
		if (!re)
			continue;
		if (pcre_exec(re, NULL, body, (int)body_size, 0, 0, ovector, 30) >= 0) {
			*val = 0;
			return 1;
		}
	}
	for (i = 0; *val && i < pn->fail_if_body_not_matches_regexp_size; i++) {
		pcre *re = pn->fail_if_body_not_matches_re ? pn->fail_if_body_not_matches_re[i] : NULL;
		if (!re)
			continue;
		if (pcre_exec(re, NULL, body, (int)body_size, 0, 0, ovector, 30) < 0) {
			*val = 0;
			return 1;
		}
	}
	return 0;
}

int probe_dns_eval_answers(probe_node *pn, const char *answers, size_t answers_size, uint64_t *val)
{
	int ovector[30];
	uint64_t i;

	if (!pn || !val)
		return 0;
	if (!answers)
		answers = "";
	if (!answers_size)
		answers_size = strlen(answers);
	for (i = 0; i < pn->fail_if_answer_matches_regexp_size; i++) {
		pcre *re = pn->fail_if_answer_matches_re ? pn->fail_if_answer_matches_re[i] : NULL;
		if (!re)
			continue;
		if (pcre_exec(re, NULL, answers, (int)answers_size, 0, 0, ovector, 30) >= 0) {
			*val = 0;
			return 1;
		}
	}
	for (i = 0; *val && i < pn->fail_if_answer_not_matches_regexp_size; i++) {
		pcre *re = pn->fail_if_answer_not_matches_re ? pn->fail_if_answer_not_matches_re[i] : NULL;
		if (!re)
			continue;
		if (pcre_exec(re, NULL, answers, (int)answers_size, 0, 0, ovector, 30) < 0) {
			*val = 0;
			return 1;
		}
	}
	return 0;
}

int probe_udp_payload_ok(const char *reply, size_t reply_len, const char *payload, size_t payload_len)
{
	size_t i;

	if (!payload || !payload_len)
		return 1;
	if (!reply || reply_len < payload_len)
		return 0;
	if (reply_len == payload_len)
		return memcmp(reply, payload, payload_len) == 0;
	for (i = 0; i + payload_len <= reply_len; i++) {
		if (memcmp(reply + i, payload, payload_len) == 0)
			return 1;
	}
	return 0;
}

static void probe_target_host(const char *target, char *out, size_t outsz)
{
	const char *p;
	const char *scheme;
	const char *slash;
	const char *at;
	const char *end;
	const char *q;
	size_t colons;
	size_t n;

	if (!out || !outsz)
		return;
	out[0] = 0;
	if (!target || !*target)
		return;
	p = target;
	scheme = strstr(p, "://");
	if (scheme)
		p = scheme + 3;
	if (*p == '[') {
		p++;
		end = strchr(p, ']');
		if (!end)
			end = p + strlen(p);
		n = (size_t)(end - p);
		if (n >= outsz)
			n = outsz - 1;
		memcpy(out, p, n);
		out[n] = 0;
		return;
	}
	slash = strchr(p, '/');
	at = strchr(p, '@');
	if (at && (!slash || at < slash))
		p = at + 1;
	colons = 0;
	end = slash ? slash : p + strlen(p);
	for (q = p; q < end; q++) {
		if (*q == ':')
			colons++;
	}
	if (colons <= 1)
		end = p + strcspn(p, "/:");
	n = (size_t)(end - p);
	if (n >= outsz)
		n = outsz - 1;
	memcpy(out, p, n);
	out[n] = 0;
}

char *probe_subst_target(const char *tmpl, const char *target)
{
	char host[HOSTHEADER_SIZE];
	string *s;
	const char *p;
	char *ret;

	if (!tmpl)
		return NULL;
	if (!target)
		target = "";
	if (!strstr(tmpl, "@target"))
		return strdup(tmpl);

	probe_target_host(target, host, sizeof(host));
	s = string_init(strlen(tmpl) + strlen(target) + strlen(host) + 8);
	if (!s)
		return NULL;
	p = tmpl;
	while (*p) {
		if (!strncmp(p, "@target.host@", 13)) {
			string_cat(s, host, strlen(host));
			p += 13;
		} else if (!strncmp(p, "@target@", 8)) {
			string_cat(s, (char *)target, strlen(target));
			p += 8;
		} else {
			string_cat(s, (char *)p, 1);
			p++;
		}
	}
	ret = strdup(s->s ? s->s : "");
	string_free(s);
	return ret;
}

static void probe_label_subst_cb(void *funcarg, void *arg)
{
	const char *target = funcarg;
	labels_container *lc = arg;
	char *repl;

	if (!lc || !lc->key || !target)
		return;
	if (!strstr(lc->key, "@target"))
		return;
	repl = probe_subst_target(lc->key, target);
	if (!repl)
		return;
	if (lc->allocatedkey)
		free(lc->key);
	lc->key = repl;
	lc->allocatedkey = 1;
}

void probe_labels_subst_target(alligator_ht *labels, const char *target)
{
	if (!labels || !target)
		return;
	alligator_ht_foreach_arg(labels, probe_label_subst_cb, (void *)target);
}

static int probe_header_lookup(const char *headers, const char *name, char *out, size_t outsz)
{
	const char *p;
	size_t nlen;

	if (!headers || !name || !out || !outsz)
		return 0;
	nlen = strlen(name);
	p = headers;
	while (*p) {
		while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
			p++;
		if (!strncasecmp(p, name, nlen) && (p[nlen] == ':' || p[nlen] == ' ' || p[nlen] == '\t')) {
			p += nlen;
			while (*p == ' ' || *p == '\t' || *p == ':')
				p++;
			size_t i = 0;
			while (p[i] && p[i] != '\r' && p[i] != '\n' && i + 1 < outsz) {
				out[i] = p[i];
				i++;
			}
			out[i] = 0;
			return 1;
		}
		while (*p && *p != '\n')
			p++;
	}
	return 0;
}

int probe_http_eval_headers(probe_node *pn, const char *headers, uint64_t *val)
{
	char value[1024];
	int ovector[30];
	uint64_t i;

	if (!pn || !val)
		return 0;

	for (i = 0; i < pn->fail_if_header_matches_size; i++) {
		probe_header_match *hm = &pn->fail_if_header_matches[i];
		int found = probe_header_lookup(headers, hm->header ? hm->header : "", value, sizeof(value));
		if (!found) {
			if (!hm->allow_missing) {
				*val = 0;
				return 1;
			}
			continue;
		}
		if (hm->re && pcre_exec(hm->re, NULL, value, (int)strlen(value), 0, 0, ovector, 30) >= 0) {
			*val = 0;
			return 1;
		}
	}
	for (i = 0; i < pn->fail_if_header_not_matches_size; i++) {
		probe_header_match *hm = &pn->fail_if_header_not_matches[i];
		int found = probe_header_lookup(headers, hm->header ? hm->header : "", value, sizeof(value));
		if (!found) {
			if (!hm->allow_missing) {
				*val = 0;
				return 1;
			}
			continue;
		}
		if (hm->re && pcre_exec(hm->re, NULL, value, (int)strlen(value), 0, 0, ovector, 30) < 0) {
			*val = 0;
			return 1;
		}
	}
	return 0;
}

int probe_http_eval_ssl(probe_node *pn, int used_tls, uint64_t *val)
{
	if (!pn || !val)
		return 0;
	if (pn->fail_if_ssl && used_tls) {
		*val = 0;
		return 1;
	}
	if (pn->fail_if_not_ssl && !used_tls) {
		*val = 0;
		return 1;
	}
	return 0;
}

static json_t *probe_json_by_path(json_t *root, const char *path)
{
	char buf[256];
	char *save;
	char *tok;
	json_t *cur = root;

	if (!root || !path)
		return NULL;
	strlcpy(buf, path, sizeof(buf));
	tok = strtok_r(buf, ".", &save);
	while (tok && cur) {
		if (json_is_array(cur))
			cur = json_array_get(cur, strtoull(tok, NULL, 10));
		else
			cur = json_object_get(cur, tok);
		tok = strtok_r(NULL, ".", &save);
	}
	return cur;
}

int probe_json_validate(probe_node *pn, const char *body, size_t body_size, uint64_t *val)
{
	json_error_t err;
	json_t *root;
	uint64_t i;
	int ovector[30];

	if (!pn || !pn->json_checks_size || !val)
		return 0;
	root = json_loadb(body ? body : "", body_size, 0, &err);
	if (!root) {
		*val = 0;
		return 1;
	}
	for (i = 0; i < pn->json_checks_size; i++) {
		probe_json_check *jc = &pn->json_checks[i];
		json_t *node = probe_json_by_path(root, jc->path);
		if (!node) {
			if (jc->must_exist || jc->re) {
				*val = 0;
				json_decref(root);
				return 1;
			}
			continue;
		}
		if (jc->re) {
			const char *s = json_string_value(node);
			char *dump = NULL;
			if (!s) {
				dump = json_dumps(node, JSON_COMPACT);
				s = dump;
			}
			if (!s || pcre_exec(jc->re, NULL, s, (int)strlen(s), 0, 0, ovector, 30) >= 0) {
				free(dump);
				*val = 0;
				json_decref(root);
				return 1;
			}
			free(dump);
		}
	}
	json_decref(root);
	return 0;
}

static int probe_rcode_parse(const char *s)
{
	if (!s)
		return -1;
	if (isdigit((unsigned char)*s))
		return atoi(s);
	if (!strcasecmp(s, "NOERROR") || !strcasecmp(s, "noerror"))
		return 0;
	if (!strcasecmp(s, "FORMERR"))
		return 1;
	if (!strcasecmp(s, "SERVFAIL"))
		return 2;
	if (!strcasecmp(s, "NXDOMAIN"))
		return 3;
	if (!strcasecmp(s, "NOTIMP"))
		return 4;
	if (!strcasecmp(s, "REFUSED"))
		return 5;
	return -1;
}

int probe_rcode_is_valid(probe_node *pn, unsigned rcode)
{
	uint64_t i;

	if (!pn || !pn->valid_rcodes_size)
		return rcode == 0;
	for (i = 0; i < pn->valid_rcodes_size; i++) {
		int want = probe_rcode_parse(pn->valid_rcodes[i]);
		if (want >= 0 && (unsigned)want == rcode)
			return 1;
	}
	return 0;
}

static void probe_histogram_emit_one(context_arg *carg, const char *name)
{
	char metric[256];
	char lebuf[32];
	uint8_t i;
	alligator_ht *lbl;
	char *host = carg->host;
	char *key = carg->key ? carg->key : carg->host;

	if (!carg->hist_buckets)
		return;

	namespace_metric_family_set_prom_type(carg, name, METRIC_TYPE_HISTOGRAM);
	for (i = 0; i < PROBE_HIST_BUCKETS; i++) {
		snprintf(lebuf, sizeof(lebuf), "%g", probe_hist_le[i]);
		lbl = alligator_ht_init(NULL);
		labels_hash_insert_nocache(lbl, "le", lebuf);
		labels_hash_insert_nocache(lbl, "host", host);
		labels_hash_insert_nocache(lbl, "key", key);
		snprintf(metric, sizeof(metric), "%s_bucket", name);
		metric_add(metric, lbl, &carg->hist_buckets[i], DATATYPE_UINT, carg);
	}
	lbl = alligator_ht_init(NULL);
	labels_hash_insert_nocache(lbl, "le", "+Inf");
	labels_hash_insert_nocache(lbl, "host", host);
	labels_hash_insert_nocache(lbl, "key", key);
	snprintf(metric, sizeof(metric), "%s_bucket", name);
	metric_add(metric, lbl, &carg->hist_buckets[PROBE_HIST_BUCKETS], DATATYPE_UINT, carg);

	lbl = alligator_ht_init(NULL);
	labels_hash_insert_nocache(lbl, "host", host);
	labels_hash_insert_nocache(lbl, "key", key);
	snprintf(metric, sizeof(metric), "%s_sum", name);
	metric_add(metric, lbl, &carg->hist_sum, DATATYPE_DOUBLE, carg);

	lbl = alligator_ht_init(NULL);
	labels_hash_insert_nocache(lbl, "host", host);
	labels_hash_insert_nocache(lbl, "key", key);
	snprintf(metric, sizeof(metric), "%s_count", name);
	metric_add(metric, lbl, &carg->hist_count, DATATYPE_UINT, carg);
}

void probe_histogram_observe(context_arg *carg, const char *native_name, const char *alias_name, double seconds)
{
	uint8_t i;

	if (!carg || seconds < 0)
		return;
	if (!carg->hist_buckets)
		carg->hist_buckets = calloc(PROBE_HIST_BUCKETS + 1, sizeof(uint64_t));
	if (!carg->hist_buckets)
		return;
	for (i = 0; i < PROBE_HIST_BUCKETS; i++) {
		if (seconds <= probe_hist_le[i])
			carg->hist_buckets[i]++;
	}
	carg->hist_buckets[PROBE_HIST_BUCKETS]++;
	carg->hist_sum += seconds;
	carg->hist_count++;
	if (native_name)
		probe_histogram_emit_one(carg, native_name);
	if (alias_name && (!native_name || strcmp(native_name, alias_name)))
		probe_histogram_emit_one(carg, alias_name);
}

void probe_push_json(json_t *probe)
{
	probe_node *pn = calloc(1, sizeof(*pn));
	json_t *jmethod;
	const char *method;
	json_t *jbody;
	json_t *jsrc;
	json_t *jurl;
	json_t *jqtype;
	json_t *jqname;
	json_t *jenv;
	json_t *jca_file;
	json_t *jcert_file;
	json_t *jkey_file;
	json_t *jserver_name;
	json_t *jproxy;
	json_t *jtls;
	json_t *jtls_verify;
	char *tls_verify;
	int tls;

	json_t *jname = json_object_get(probe, "name");
	if (!jname)
	{
		free(pn);
		return;
	}
	char *name = (char*)json_string_value(jname);
	if (!name)
	{
		free(pn);
		return;
	}
	pn->name = strdup(name);

	json_t *jprober = json_object_get(probe, "prober");
	if (!jprober)
	{
		free(pn->name);
		free(pn);
		return;
	}
	char *prober = (char*)json_string_value(jprober);
	if (!prober)
	{
		free(pn->name);
		free(pn);
		return;
	}

	pn->timeout = probe_timeout_from_json(probe);
	pn->probe_ip_version[0] = probe_ip_version_from_json(probe);

	pn->method = HTTP_GET;
	jmethod = json_object_get(probe, "method");
	method = json_string_value(jmethod);
	if (method)
		pn->method = probe_method_from_str(method);

	jbody = json_object_get(probe, "body");
	if (jbody && json_is_string(jbody)) {
		const char *body = json_string_value(jbody);
		if (body)
			pn->body = strdup(body);
	}

	{
		json_t *jbody_file = json_object_get(probe, "body_file");
		if (jbody_file && json_is_string(jbody_file)) {
			const char *body_file = json_string_value(jbody_file);
			if (body_file && *body_file)
				pn->body_file = strdup(body_file);
		}
	}
	if (pn->body && pn->body_file) {
		glog(L_ERROR, "probe '%s': setting body and body_file both is invalid; using body_file\n", pn->name);
		free(pn->body);
		pn->body = NULL;
	}

	jsrc = json_object_get(probe, "source_ip_address");
	if (jsrc && json_is_string(jsrc)) {
		const char *src = json_string_value(jsrc);
		if (src)
			pn->source_ip_address = strdup(src);
	}

	jurl = json_object_get(probe, "url");
	if (jurl && json_is_string(jurl)) {
		const char *url = json_string_value(jurl);
		if (url && *url)
			pn->url = strdup(url);
	}

	jqtype = json_object_get(probe, "type");
	if (jqtype && json_is_string(jqtype)) {
		const char *qtype = json_string_value(jqtype);
		if (qtype && *qtype)
			pn->query_type = strdup(qtype);
	}

	jqname = json_object_get(probe, "query_name");
	if (jqname && json_is_string(jqname)) {
		const char *qname = json_string_value(jqname);
		if (qname && *qname)
			pn->query_name = strdup(qname);
	}

	jenv = json_object_get(probe, "env");
	if (!jenv || json_typeof(jenv) != JSON_OBJECT)
		jenv = json_object_get(probe, "header");
	if (jenv && json_typeof(jenv) == JSON_OBJECT) {
		json_t *wrap = json_object();
		json_object_set(wrap, "env", jenv);
		pn->env = env_struct_parser(wrap);
		json_decref(wrap);
	}

	pn->unixgram = probe_json_is_on(json_object_get(probe, "unixgram"));

	pn->follow_redirects = config_get_intstr_json(probe, "follow_redirects");

	pn->loop = config_get_intstr_json(probe, "loop");
	pn->percent_success = config_get_floatstr_json(probe, "percent");

	jca_file = json_object_get(probe, "ca_file");
	if (jca_file)
	{
		char *ca_file = (char*)json_string_value(jca_file);
		if (ca_file)
			pn->ca_file = strdup(ca_file);
	}

	jcert_file = json_object_get(probe, "cert_file");
	if (jcert_file)
	{
		char *cert_file = (char*)json_string_value(jcert_file);
		if (cert_file)
			pn->cert_file = strdup(cert_file);
	}

	jkey_file = json_object_get(probe, "key_file");
	if (jkey_file)
	{
		char *key_file = (char*)json_string_value(jkey_file);
		if (key_file)
			pn->key_file = strdup(key_file);
	}

	jserver_name = json_object_get(probe, "server_name");
	if (jserver_name)
	{
		char *server_name = (char*)json_string_value(jserver_name);
		if (server_name)
			pn->server_name = strdup(server_name);
	}

	jproxy = json_object_get(probe, "http_proxy_url");
	if (!jproxy)
		jproxy = json_object_get(probe, "proxy");
	if (jproxy && json_typeof(jproxy) == JSON_STRING) {
		const char *proxy_url = json_string_value(jproxy);
		if (proxy_url && *proxy_url)
			pn->http_proxy_url = strdup(proxy_url);
	}

	pn->valid_status_codes = probe_json_str_list(json_object_get(probe, "valid_status_codes"), &pn->valid_status_codes_size);
	pn->fail_if_body_matches_regexp = probe_json_str_list(json_object_get(probe, "fail_if_body_matches_regexp"), &pn->fail_if_body_matches_regexp_size);
	pn->fail_if_body_matches_re = probe_compile_re_array(pn->fail_if_body_matches_regexp, pn->fail_if_body_matches_regexp_size);
	pn->fail_if_body_not_matches_regexp = probe_json_str_list(json_object_get(probe, "fail_if_body_not_matches_regexp"), &pn->fail_if_body_not_matches_regexp_size);
	pn->fail_if_body_not_matches_re = probe_compile_re_array(pn->fail_if_body_not_matches_regexp, pn->fail_if_body_not_matches_regexp_size);
	pn->valid_rcodes = probe_json_str_list(json_object_get(probe, "valid_rcodes"), &pn->valid_rcodes_size);
	pn->fail_if_answer_matches_regexp = probe_json_str_list(json_object_get(probe, "fail_if_answer_matches_regexp"), &pn->fail_if_answer_matches_regexp_size);
	pn->fail_if_answer_matches_re = probe_compile_re_array(pn->fail_if_answer_matches_regexp, pn->fail_if_answer_matches_regexp_size);
	pn->fail_if_answer_not_matches_regexp = probe_json_str_list(json_object_get(probe, "fail_if_answer_not_matches_regexp"), &pn->fail_if_answer_not_matches_regexp_size);
	pn->fail_if_answer_not_matches_re = probe_compile_re_array(pn->fail_if_answer_not_matches_regexp, pn->fail_if_answer_not_matches_regexp_size);
	{
		json_t *jpayload = json_object_get(probe, "payload");
		if (jpayload && json_is_string(jpayload)) {
			const char *payload = json_string_value(jpayload);
			if (payload)
				pn->payload = strdup(payload);
		}
	}
	pn->query_response = probe_parse_query_response(json_object_get(probe, "query_response"), &pn->query_response_size);
	pn->fail_if_header_matches = probe_parse_header_matches(json_object_get(probe, "fail_if_header_matches"), &pn->fail_if_header_matches_size);
	pn->fail_if_header_not_matches = probe_parse_header_matches(json_object_get(probe, "fail_if_header_not_matches"), &pn->fail_if_header_not_matches_size);
	pn->json_checks = probe_parse_json_checks(json_object_get(probe, "fail_if_json_not_exists"), json_object_get(probe, "fail_if_json_matches_regexp"), &pn->json_checks_size);

	pn->fail_if_ssl = probe_json_is_on(json_object_get(probe, "fail_if_ssl")) ? 1 : 0;
	pn->fail_if_not_ssl = probe_json_is_on(json_object_get(probe, "fail_if_not_ssl")) ? 1 : 0;
	pn->negative_test = probe_json_is_on(json_object_get(probe, "negative_test")) ? 1 : 0;

	pn->icmp_payload_size = (uint32_t)config_get_intstr_json(probe, "payload_size");
	if (!pn->icmp_payload_size)
		pn->icmp_payload_size = (uint32_t)config_get_intstr_json(probe, "size");
	pn->icmp_ttl = (int)config_get_intstr_json(probe, "ttl");
	pn->icmp_tos = (int)config_get_intstr_json(probe, "tos");
	pn->icmp_interval = probe_ms_field(probe, "interval");

	jtls = json_object_get(probe, "tls");
	tls = probe_json_is_on(jtls);

	jtls_verify = json_object_get(probe, "tls_verify");
	tls_verify = jtls_verify ? (char*)json_string_value(jtls_verify) : NULL;
	if (tls_verify)
	{
		if (!strcmp(tls_verify, "on"))
			pn->tls_verify = 1;
	} else if (probe_json_is_on(jtls_verify)) {
		pn->tls_verify = 1;
	}

	if (!strcmp(prober, "icmp"))
	{
		pn->prober = APROTO_ICMP;
		pn->prober_str = "icmp";
		pn->scheme = strdup("icmp://");
	}
	else if (!strcmp(prober, "http") && !tls)
	{
		pn->prober = APROTO_HTTP;
		pn->prober_str = "http";
		pn->scheme = strdup("http://");
	}
	else if (!strcmp(prober, "http") && tls)
	{
		pn->prober = APROTO_HTTPS;
		pn->tls = 1;
		pn->prober_str = "http";
		pn->scheme = strdup("https://");
	}
	else if (!strcmp(prober, "tcp") && !tls)
	{
		pn->prober = APROTO_TCP;
		pn->prober_str = "tcp";
		pn->scheme = strdup("tcp://");
	}
	else if (!strcmp(prober, "tcp") && tls)
	{
		pn->prober = APROTO_TLS;
		pn->prober_str = "tcp";
		pn->tls = 1;
		pn->scheme = strdup("tls://");
	}
	else if (!strcmp(prober, "udp") && !tls)
	{
		pn->prober = APROTO_UDP;
		pn->prober_str = "udp";
		pn->scheme = strdup("udp://");
	}
	else if (!strcmp(prober, "dns"))
	{
		pn->prober = APROTO_RESOLVER;
		pn->prober_str = "dns";
		pn->scheme = strdup(pn->url ? pn->url : "resolver://");
	}
	else if (!strcmp(prober, "websocket") || !strcmp(prober, "ws"))
	{
		pn->prober = tls ? APROTO_WSS : APROTO_WS;
		pn->prober_str = "websocket";
		pn->tls = tls ? 1 : 0;
		pn->scheme = strdup(tls ? "wss://" : "ws://");
	}
	else if (!strcmp(prober, "unix") || !strcmp(prober, "unixgram"))
	{
		int gram = pn->unixgram || !strcmp(prober, "unixgram");
		if (pn->url && !strncmp(pn->url, "unixgram://", 11))
			gram = 1;
		if (pn->url && (!strncmp(pn->url, "http://unix:", 12) || !strncmp(pn->url, "https://unix:", 13) ||
		    !strncmp(pn->url, "tcp://unix:", 11) || !strncmp(pn->url, "tls://unix:", 11))) {
			pn->prober = APROTO_UNIX;
			pn->prober_str = "unix";
			pn->tls = (!strncmp(pn->url, "https://unix:", 13) || !strncmp(pn->url, "tls://unix:", 11)) ? 1 : 0;
			pn->scheme = strdup(pn->url);
		} else if (gram) {
			pn->prober = APROTO_UNIXGRAM;
			pn->prober_str = "unix";
			pn->unixgram = 1;
			pn->scheme = strdup("unixgram://");
		} else if (tls) {
			pn->prober = APROTO_UNIX;
			pn->prober_str = "unix";
			pn->tls = 1;
			pn->scheme = strdup("tls://unix:");
		} else {
			pn->prober = APROTO_UNIX;
			pn->prober_str = "unix";
			pn->scheme = strdup("unix://");
		}
	}
	else
	{
		probe_node_free(pn);
		return;
	}

	if ((pn->prober == APROTO_HTTP || pn->prober == APROTO_HTTPS) && !pn->valid_status_codes) {
		pn->valid_status_codes = calloc(1, sizeof(char *));
		pn->valid_status_codes[0] = strdup("2xx");
		pn->valid_status_codes_size = 1;
	}

	glog(L_INFO, "create probe node name '%s' and prober '%s'\n", pn->name, prober);

	{
		context_arg tmp = {0};
		parse_add_label(&tmp, probe);
		parse_metricstransform(&tmp, probe);
		parse_metric_name_transform(&tmp, probe);
		pn->labels = tmp.labels;
		pn->metricstransform = tmp.metricstransform;
		pn->metric_name_transform_pattern = tmp.metric_name_transform_pattern;
		pn->metric_name_transform_replacement = tmp.metric_name_transform_replacement;
		pn->metric_name_transform_compiled = tmp.metric_name_transform_compiled;
	}

	alligator_ht_insert(ac->probe, &(pn->node), pn, tommy_strhash_u32(0, pn->name));
}

void probe_del_json(json_t *probe)
{
	json_t *jname = json_object_get(probe, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);
	if (!name)
		return;

	probe_node *pn = alligator_ht_search(ac->probe, probe_compare, name, tommy_strhash_u32(0, name));
	if (pn)
	{
		alligator_ht_remove_existing(ac->probe, &(pn->node));
		probe_node_free(pn);
	}
}
