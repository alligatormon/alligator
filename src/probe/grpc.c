#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>

#include "probe/grpc.h"
#include "probe/probe.h"
#include "common/http2_grpc.h"
#include "common/protobuf_wire.h"
#include "common/logs.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"

#define PROBE_GRPC_HEALTH_PATH "/grpc.health.v1.Health/Check"
#define PROBE_GRPC_UNAVAILABLE 14

static const char *const probe_grpc_serving_name[] = {
	"UNKNOWN",
	"SERVING",
	"NOT_SERVING",
	"SERVICE_UNKNOWN"
};

static int probe_grpc_copy_port(const char *p, char *port, size_t port_sz)
{
	size_t i;
	unsigned long v = 0;

	if (!p || !*p)
		return 0;
	for (i = 0; p[i]; i++) {
		if (p[i] < '0' || p[i] > '9')
			return 0;
		if (i >= 5)
			return 0;
		v = v * 10u + (unsigned)(p[i] - '0');
	}
	if (!i || v == 0 || v > 65535)
		return 0;
	if (i >= port_sz)
		return 0;
	memcpy(port, p, i);
	port[i] = 0;
	return 1;
}

void probe_grpc_authority(const char *host, const char *port, char *dst, size_t dst_len)
{
	const char *h = host ? host : "";
	const char *p = (port && port[0]) ? port : "";

	if (strchr(h, ':'))
		snprintf(dst, dst_len, "[%s]:%s", h, p);
	else
		snprintf(dst, dst_len, "%s:%s", h, p);
}

int probe_grpc_format_target(int tls, const char *target, char *url, size_t url_len, char *authority, size_t authority_len)
{
	const char *p;
	char host[256];
	char port[8];
	const char *defport;
	int n;

	if (!target || !url || !url_len || !authority || !authority_len)
		return -1;
	p = target;
	if (!strncasecmp(p, "https://", 8))
		p += 8;
	else if (!strncasecmp(p, "http://", 7))
		p += 7;
	if (!*p)
		return -1;

	host[0] = 0;
	port[0] = 0;
	if (*p == '[') {
		const char *rb = strchr(p, ']');
		size_t hlen;

		if (!rb || rb == p + 1)
			return -1;
		hlen = (size_t)(rb - (p + 1));
		if (hlen >= sizeof(host))
			return -1;
		memcpy(host, p + 1, hlen);
		host[hlen] = 0;
		p = rb + 1;
		if (*p == ':') {
			if (!probe_grpc_copy_port(p + 1, port, sizeof(port)))
				return -1;
		} else if (*p) {
			return -1;
		}
	} else {
		const char *slash = strchr(p, '/');
		const char *colon;
		const char *colon2;

		if (slash)
			return -1;
		colon = strchr(p, ':');
		if (colon) {
			colon2 = strchr(colon + 1, ':');
			if (colon2 || colon == p)
				return -1;
			if ((size_t)(colon - p) >= sizeof(host))
				return -1;
			memcpy(host, p, (size_t)(colon - p));
			host[colon - p] = 0;
			if (!probe_grpc_copy_port(colon + 1, port, sizeof(port)))
				return -1;
		} else {
			if (strlen(p) >= sizeof(host))
				return -1;
			memcpy(host, p, strlen(p) + 1);
		}
	}
	if (!host[0])
		return -1;

	defport = tls ? "443" : "80";
	if (!port[0])
		snprintf(port, sizeof(port), "%s", defport);

	probe_grpc_authority(host, port, authority, authority_len);
	if (strchr(host, ':'))
		n = snprintf(url, url_len, "%s://[%s]:%s", tls ? "tls" : "tcp", host, port);
	else
		n = snprintf(url, url_len, "%s://%s:%s", tls ? "tls" : "tcp", host, port);
	if (n < 0 || (size_t)n >= url_len)
		return -1;
	if (!authority[0])
		return -1;
	return 0;
}

char *probe_grpc_build_request(const char *authority, const char *service, int scheme_https, alligator_ht *env, size_t *out_len)
{
	string *pb = NULL;
	string *framed = NULL;
	char *req = NULL;

	if (out_len)
		*out_len = 0;
	if (service && service[0]) {
		pb = string_init(strlen(service) + 16);
		if (!pb)
			return NULL;
		pbwire_write_tag(pb, 1, 2);
		pbwire_write_len(pb, (char *)service, strlen(service));
		framed = otlp_grpc_frame_wrap(pb->s, pb->l);
	} else {
		framed = otlp_grpc_frame_wrap(NULL, 0);
	}
	if (!framed)
		goto out;
	req = otlp_grpc_http2_request_build(authority, PROBE_GRPC_HEALTH_PATH, scheme_https,
		framed->s, framed->l, env, out_len);
out:
	if (pb)
		string_free(pb);
	if (framed)
		string_free(framed);
	return req;
}

void probe_grpc_arm_carg(context_arg *carg, host_aggregator_info *hi, const char *service, alligator_ht *env)
{
	char authority[1024];
	char *req;
	size_t req_len = 0;

	if (!carg || !hi)
		return;
	carg->tls = hi->tls ? 1 : 0;
	if (carg->tls)
		carg->tls_alpn_h2 = 1;
	carg->parser_handler = grpc_health_probe_catch;
	carg->parser_name = "grpc_health_probe";
	carg->expect_function = otlp_grpc_expect_complete;
	probe_grpc_authority(hi->host, hi->port, authority, sizeof(authority));
	req = probe_grpc_build_request(authority, service, carg->tls, env, &req_len);
	if (req)
		aconf_mesg_set(carg, req, req_len);
}

static int probe_grpc_decode_health(const uint8_t *buf, size_t len, int *status)
{
	const uint8_t *p = buf;
	const uint8_t *end = buf + len;
	int found = 0;

	if (status)
		*status = 0;
	if (!buf || !len)
		return 0;
	while (p < end) {
		uint64_t tag = 0;
		uint64_t v = 0;
		uint32_t field;
		uint8_t wire;

		if (!pbwire_read_varint(&p, end, &tag))
			return -1;
		field = (uint32_t)(tag >> 3);
		wire = (uint8_t)(tag & 7);
		if (field == 1 && wire == 0) {
			if (!pbwire_read_varint(&p, end, &v))
				return -1;
			if (status)
				*status = (v <= 3) ? (int)v : -1;
			found = 1;
		} else if (!pbwire_skip_field(&p, end, wire)) {
			return -1;
		}
	}
	return found ? 1 : 0;
}

int probe_grpc_interpret(const char *buf, size_t len, int *grpc_status, int *health_status)
{
	char *protobuf = NULL;
	size_t protobuf_len = 0;
	int gst = -1;
	int health = -1;
	int dec;
	int rc;

	if (grpc_status)
		*grpc_status = -1;
	if (health_status)
		*health_status = -1;
	if (!buf || !len)
		return 0;

	rc = otlp_grpc_http2_response_parse(buf, len, &gst, NULL, 0, &protobuf, &protobuf_len);
	if (grpc_status)
		*grpc_status = gst;
	if (!rc) {
		free(protobuf);
		return 0;
	}
	if (gst != 0) {
		free(protobuf);
		return 1;
	}
	if (!protobuf || !protobuf_len) {
		health = 0;
	} else {
		dec = probe_grpc_decode_health((const uint8_t *)protobuf, protobuf_len, &health);
		if (dec < 0)
			health = -1;
		else if (dec == 0)
			health = 0;
	}
	free(protobuf);
	if (health_status)
		*health_status = health;
	return 1;
}

int probe_grpc_is(context_arg *carg)
{
	return carg && carg->parser_name && !strcmp(carg->parser_name, "grpc_health_probe");
}

static void probe_grpc_emit(context_arg *carg, int grpc_status, int health_status, uint64_t success)
{
	probe_node *pn;
	char authority[512];
	char *host;
	char *key;
	char *prober;
	char *module;
	int64_t code;
	uint64_t ssl;
	int i;

	if (!carg)
		return;
	carg->parser_status = 1;
	carg->parsed = 1;

	pn = probe_from_carg(carg);
	probe_grpc_authority(carg->host, carg->port, authority, sizeof(authority));
	host = authority;
	key = (carg->key && carg->key[0]) ? carg->key : host;
	prober = (pn && pn->prober_str) ? pn->prober_str : "grpc";
	module = (pn && pn->name) ? pn->name : "blackbox";

	namespace_metric_family_set(NULL, carg, "probe_grpc_status_code", METRIC_TYPE_GAUGE, "gRPC status code from the health check. 14 means the probe ended before a grpc-status.");
	namespace_metric_family_set(NULL, carg, "probe_grpc_healthcheck_response", METRIC_TYPE_GAUGE, "HealthCheckResponse serving status. The matching series is 1 and the other three are 0.");
	namespace_metric_family_set(NULL, carg, "probe_grpc_ssl", METRIC_TYPE_GAUGE, "1 when the TLS handshake completed.");

	code = grpc_status;
	metric_add_labels5("probe_grpc_status_code", &code, DATATYPE_INT, carg,
		"host", host, "key", key, "prober", prober, "module", module, "type", "blackbox");

	for (i = 0; i < 4; i++) {
		uint64_t bit = (health_status == i) ? 1 : 0;
		metric_add_labels6("probe_grpc_healthcheck_response", &bit, DATATYPE_UINT, carg,
			"host", host, "key", key, "prober", prober, "module", module,
			"type", "blackbox", "serving_status", (char *)probe_grpc_serving_name[i]);
	}

	ssl = (carg->tls && carg->tls_handshake_done) ? 1 : 0;
	metric_add_labels5("probe_grpc_ssl", &ssl, DATATYPE_UINT, carg,
		"host", host, "key", key, "prober", prober, "module", module, "type", "blackbox");

	probe_metric_success(carg, success);
	carglog(carg, L_DEBUG, "grpc health probe host=%s grpc_status=%d health=%d success=%" PRIu64 " ssl=%" PRIu64 "\n",
		host, grpc_status, health_status, success, ssl);
}

void probe_grpc_fail_unavailable(context_arg *carg)
{
	probe_grpc_emit(carg, PROBE_GRPC_UNAVAILABLE, -1, 0);
}

void grpc_health_probe_catch(char *metrics, size_t size, context_arg *carg)
{
	int grpc_status = -1;
	int health = -1;
	int understood;
	uint64_t success = 0;

	if (!carg)
		return;
	if (!metrics || !size) {
		probe_grpc_fail_unavailable(carg);
		return;
	}
	understood = probe_grpc_interpret(metrics, size, &grpc_status, &health);
	if (!understood) {
		probe_grpc_emit(carg, grpc_status, -1, 0);
		return;
	}
	if (grpc_status == 0 && health == 1)
		success = 1;
	if (grpc_status != 0)
		health = -1;
	probe_grpc_emit(carg, grpc_status, health, success);
}
