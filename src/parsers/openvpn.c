#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/logs.h"
#include "common/url.h"
#include "main.h"

#define OPENVPN_MAX_FIELDS 32

enum {
	OPENVPN_SEC_NONE = 0,
	OPENVPN_SEC_V1_CLIENTS,
	OPENVPN_SEC_V1_ROUTES,
	OPENVPN_SEC_V1_GLOBAL,
	OPENVPN_SEC_CLIENT_STATS
};

typedef struct {
	int cn;
	int real;
	int virt;
	int bytes_in;
	int bytes_out;
	int since_t;
	int user;
	int have_header;
} openvpn_client_cols;

typedef struct {
	int virt;
	int cn;
	int real;
	int last_ref_t;
	int have_header;
} openvpn_route_cols;

static void openvpn_metric_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "openvpn_connected_clients", METRIC_TYPE_GAUGE,
		"Number of connected OpenVPN clients.");
	namespace_metric_family_set(NULL, carg, "openvpn_status_updated_seconds", METRIC_TYPE_GAUGE,
		"UNIX timestamp when OpenVPN last wrote the status dump.");
	namespace_metric_family_set(NULL, carg, "openvpn_client_received_bytes_total", METRIC_TYPE_COUNTER,
		"Bytes received from an OpenVPN client.");
	namespace_metric_family_set(NULL, carg, "openvpn_client_sent_bytes_total", METRIC_TYPE_COUNTER,
		"Bytes sent to an OpenVPN client.");
	namespace_metric_family_set(NULL, carg, "openvpn_client_connected_since_seconds", METRIC_TYPE_GAUGE,
		"UNIX timestamp when the OpenVPN client connected.");
	namespace_metric_family_set(NULL, carg, "openvpn_route_last_ref_seconds", METRIC_TYPE_GAUGE,
		"UNIX timestamp when an OpenVPN route was last referenced.");
	namespace_metric_family_set(NULL, carg, "openvpn_max_bcast_mcast_queue_length", METRIC_TYPE_GAUGE,
		"OpenVPN max broadcast/multicast queue length.");
	namespace_metric_family_set(NULL, carg, "openvpn_bytes_in_total", METRIC_TYPE_COUNTER,
		"OpenVPN server bytes in from management load-stats.");
	namespace_metric_family_set(NULL, carg, "openvpn_bytes_out_total", METRIC_TYPE_COUNTER,
		"OpenVPN server bytes out from management load-stats.");
	namespace_metric_family_set(NULL, carg, "openvpn_bytes", METRIC_TYPE_COUNTER,
		"OpenVPN client-mode byte counters by kind.");
}

static char openvpn_detect_delim(const char *s)
{
	if (strstr(s, "TITLE\t") || strstr(s, "HEADER\t") || strstr(s, "CLIENT_LIST\t") ||
	    strstr(s, "ROUTING_TABLE\t") || strstr(s, "GLOBAL_STATS\t") || strstr(s, "TIME\t"))
		return '\t';
	return ',';
}

static int openvpn_split_fields(char *line, char delim, char **fields, int max_fields)
{
	int n = 0;
	char *p = line;

	while (n < max_fields) {
		fields[n++] = p;
		char *d = strchr(p, delim);
		if (!d)
			break;
		*d = 0;
		p = d + 1;
	}
	return n;
}

static const char *openvpn_field(char **fields, int nfields, int idx)
{
	if (idx < 0 || idx >= nfields || !fields[idx])
		return "";
	return fields[idx];
}

static uint64_t openvpn_u64_field(char **fields, int nfields, int idx)
{
	const char *s = openvpn_field(fields, nfields, idx);
	if (!s || !*s)
		return 0;
	return strtoull(s, NULL, 10);
}

static int openvpn_col_name_eq(const char *field, const char *name)
{
	return field && !strcmp(field, name);
}

static void openvpn_parse_client_header(char **fields, int nfields, openvpn_client_cols *cols)
{
	int i;

	cols->cn = cols->real = cols->virt = cols->bytes_in = cols->bytes_out = cols->since_t = cols->user = -1;
	for (i = 2; i < nfields; i++) {
		int data_idx = i - 1;
		if (openvpn_col_name_eq(fields[i], "Common Name"))
			cols->cn = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Real Address"))
			cols->real = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Virtual Address"))
			cols->virt = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Bytes Received"))
			cols->bytes_in = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Bytes Sent"))
			cols->bytes_out = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Connected Since (time_t)"))
			cols->since_t = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Username"))
			cols->user = data_idx;
	}
	cols->have_header = 1;
}

static void openvpn_parse_route_header(char **fields, int nfields, openvpn_route_cols *cols)
{
	int i;

	cols->virt = cols->cn = cols->real = cols->last_ref_t = -1;
	for (i = 2; i < nfields; i++) {
		int data_idx = i - 1;
		if (openvpn_col_name_eq(fields[i], "Virtual Address"))
			cols->virt = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Common Name"))
			cols->cn = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Real Address"))
			cols->real = data_idx;
		else if (openvpn_col_name_eq(fields[i], "Last Ref (time_t)"))
			cols->last_ref_t = data_idx;
	}
	cols->have_header = 1;
}

static void openvpn_default_client_cols(openvpn_client_cols *cols, int nfields)
{
	if (cols->have_header)
		return;
	/* OpenVPN 2.4+ status-version 2/3: Virtual IPv6 sits at index 4. */
	if (nfields >= 10) {
		cols->cn = 1;
		cols->real = 2;
		cols->virt = 3;
		cols->bytes_in = 5;
		cols->bytes_out = 6;
		cols->since_t = 8;
		cols->user = 9;
	} else {
		cols->cn = 1;
		cols->real = 2;
		cols->virt = 3;
		cols->bytes_in = 4;
		cols->bytes_out = 5;
		cols->since_t = 7;
		cols->user = -1;
	}
}

static void openvpn_default_route_cols(openvpn_route_cols *cols)
{
	if (cols->have_header)
		return;
	cols->virt = 1;
	cols->cn = 2;
	cols->real = 3;
	cols->last_ref_t = 5;
}

static char *openvpn_nonempty(const char *s, char *fallback)
{
	if (s && *s)
		return (char *)s;
	return fallback;
}

static void openvpn_emit_client(context_arg *carg, const char *cn, const char *real,
	const char *virt, const char *user, uint64_t recv, uint64_t sent, uint64_t since)
{
	char *cn_l = openvpn_nonempty(cn, "");
	char *real_l = openvpn_nonempty(real, "");
	char *virt_l = openvpn_nonempty(virt, "");
	char *user_l = openvpn_nonempty(user, "UNDEF");

	metric_add_labels4("openvpn_client_received_bytes_total", &recv, DATATYPE_UINT, carg,
		"common_name", cn_l, "real_address", real_l, "virtual_address", virt_l, "username", user_l);
	metric_add_labels4("openvpn_client_sent_bytes_total", &sent, DATATYPE_UINT, carg,
		"common_name", cn_l, "real_address", real_l, "virtual_address", virt_l, "username", user_l);
	if (since)
		metric_add_labels4("openvpn_client_connected_since_seconds", &since, DATATYPE_UINT, carg,
			"common_name", cn_l, "real_address", real_l, "virtual_address", virt_l, "username", user_l);
}

static void openvpn_emit_route(context_arg *carg, const char *cn, const char *real,
	const char *virt, uint64_t last_ref)
{
	char *cn_l = openvpn_nonempty(cn, "");
	char *real_l = openvpn_nonempty(real, "");
	char *virt_l = openvpn_nonempty(virt, "");

	if (!last_ref)
		return;
	metric_add_labels3("openvpn_route_last_ref_seconds", &last_ref, DATATYPE_UINT, carg,
		"common_name", cn_l, "real_address", real_l, "virtual_address", virt_l);
}

static const char *openvpn_client_stat_kind(const char *key)
{
	if (!strcmp(key, "TUN/TAP read bytes"))
		return "tun_tap_read";
	if (!strcmp(key, "TUN/TAP write bytes"))
		return "tun_tap_write";
	if (!strcmp(key, "TCP/UDP read bytes"))
		return "tcp_udp_read";
	if (!strcmp(key, "TCP/UDP write bytes"))
		return "tcp_udp_write";
	if (!strcmp(key, "Auth read bytes"))
		return "auth_read";
	if (!strcmp(key, "pre-compress bytes"))
		return "pre_compress";
	if (!strcmp(key, "post-compress bytes"))
		return "post_compress";
	if (!strcmp(key, "pre-decompress bytes"))
		return "pre_decompress";
	if (!strcmp(key, "post-decompress bytes"))
		return "post_decompress";
	return NULL;
}

static int openvpn_parse_load_stats(context_arg *carg, char *line, uint64_t *nclients)
{
	char *p;
	int got = 0;

	p = strstr(line, "nclients=");
	if (p) {
		*nclients = strtoull(p + 9, NULL, 10);
		got = 1;
	}
	p = strstr(line, "bytesin=");
	if (p) {
		uint64_t v = strtoull(p + 8, NULL, 10);
		metric_add_auto("openvpn_bytes_in_total", &v, DATATYPE_UINT, carg);
		got = 1;
	}
	p = strstr(line, "bytesout=");
	if (p) {
		uint64_t v = strtoull(p + 9, NULL, 10);
		metric_add_auto("openvpn_bytes_out_total", &v, DATATYPE_UINT, carg);
		got = 1;
	}
	return got;
}

void openvpn_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	char delim;
	char *save = NULL;
	int section = OPENVPN_SEC_NONE;
	int found = 0;
	int saw_server = 0;
	uint64_t nclients = 0;
	uint64_t nclients_load = 0;
	int have_load_clients = 0;
	openvpn_client_cols cl = { .cn = 1, .real = 2, .virt = 3, .bytes_in = 5,
		.bytes_out = 6, .since_t = 8, .user = 9, .have_header = 0 };
	openvpn_route_cols rt = { .virt = 1, .cn = 2, .real = 3, .last_ref_t = 5, .have_header = 0 };

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	openvpn_metric_families(carg);

	copy = strndup(metrics, size);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}

	delim = openvpn_detect_delim(copy);

	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *fields[OPENVPN_MAX_FIELDS];
		int nfields;
		size_t len;

		len = strlen(line);
		if (len && line[len - 1] == '\r')
			line[--len] = 0;
		if (!len)
			continue;
		if (line[0] == '>' || !strncmp(line, "ENTER PASSWORD", 14) || !strncmp(line, "ERROR:", 6))
			continue;

		if (!strncmp(line, "SUCCESS:", 8)) {
			if (openvpn_parse_load_stats(carg, line, &nclients_load)) {
				found = 1;
				have_load_clients = 1;
			}
			continue;
		}

		if (!strcmp(line, "END")) {
			found = 1;
			continue;
		}

		if (!strcmp(line, "OpenVPN CLIENT LIST")) {
			section = OPENVPN_SEC_V1_CLIENTS;
			saw_server = 1;
			found = 1;
			continue;
		}
		if (!strcmp(line, "ROUTING TABLE")) {
			section = OPENVPN_SEC_V1_ROUTES;
			found = 1;
			continue;
		}
		if (!strcmp(line, "GLOBAL STATS")) {
			section = OPENVPN_SEC_V1_GLOBAL;
			found = 1;
			continue;
		}
		if (!strcmp(line, "OpenVPN STATISTICS")) {
			section = OPENVPN_SEC_CLIENT_STATS;
			found = 1;
			continue;
		}

		nfields = openvpn_split_fields(line, delim, fields, OPENVPN_MAX_FIELDS);
		if (nfields < 1)
			continue;

		if (!strcmp(fields[0], "TITLE")) {
			found = 1;
			saw_server = 1;
			continue;
		}
		if (!strcmp(fields[0], "TIME") && nfields >= 3) {
			uint64_t ts = strtoull(fields[nfields - 1], NULL, 10);
			if (ts)
				metric_add_auto("openvpn_status_updated_seconds", &ts, DATATYPE_UINT, carg);
			found = 1;
			saw_server = 1;
			continue;
		}
		if (!strcmp(fields[0], "HEADER") && nfields >= 2) {
			found = 1;
			saw_server = 1;
			if (!strcmp(fields[1], "CLIENT_LIST"))
				openvpn_parse_client_header(fields, nfields, &cl);
			else if (!strcmp(fields[1], "ROUTING_TABLE"))
				openvpn_parse_route_header(fields, nfields, &rt);
			continue;
		}
		if (!strcmp(fields[0], "CLIENT_LIST") && nfields >= 4) {
			const char *user;
			uint64_t recv, sent, since;

			saw_server = 1;
			found = 1;
			openvpn_default_client_cols(&cl, nfields);
			user = openvpn_field(fields, nfields, cl.user);
			recv = openvpn_u64_field(fields, nfields, cl.bytes_in);
			sent = openvpn_u64_field(fields, nfields, cl.bytes_out);
			since = openvpn_u64_field(fields, nfields, cl.since_t);
			openvpn_emit_client(carg, openvpn_field(fields, nfields, cl.cn),
				openvpn_field(fields, nfields, cl.real),
				openvpn_field(fields, nfields, cl.virt),
				user, recv, sent, since);
			nclients++;
			continue;
		}
		if (!strcmp(fields[0], "ROUTING_TABLE") && nfields >= 4) {
			found = 1;
			openvpn_default_route_cols(&rt);
			openvpn_emit_route(carg, openvpn_field(fields, nfields, rt.cn),
				openvpn_field(fields, nfields, rt.real),
				openvpn_field(fields, nfields, rt.virt),
				openvpn_u64_field(fields, nfields, rt.last_ref_t));
			continue;
		}
		if (!strcmp(fields[0], "GLOBAL_STATS") && nfields >= 3) {
			uint64_t q = strtoull(fields[nfields - 1], NULL, 10);
			metric_add_auto("openvpn_max_bcast_mcast_queue_length", &q, DATATYPE_UINT, carg);
			found = 1;
			continue;
		}

		if (strstr(fields[0], "Max bcast/mcast queue length") && nfields >= 2) {
			uint64_t q = strtoull(fields[nfields - 1], NULL, 10);
			metric_add_auto("openvpn_max_bcast_mcast_queue_length", &q, DATATYPE_UINT, carg);
			found = 1;
			continue;
		}

		if (section == OPENVPN_SEC_V1_CLIENTS) {
			if (!strcmp(fields[0], "Updated") || !strcmp(fields[0], "Common Name"))
				continue;
			if (nfields >= 4) {
				uint64_t recv = strtoull(fields[2], NULL, 10);
				uint64_t sent = strtoull(fields[3], NULL, 10);
				openvpn_emit_client(carg, fields[0], fields[1], "", "", recv, sent, 0);
				nclients++;
				found = 1;
			}
			continue;
		}
		if (section == OPENVPN_SEC_V1_ROUTES) {
			continue;
		}
		if (section == OPENVPN_SEC_CLIENT_STATS) {
			const char *kind;
			uint64_t val;

			if (!strcmp(fields[0], "Updated") || nfields < 2)
				continue;
			kind = openvpn_client_stat_kind(fields[0]);
			if (!kind)
				continue;
			val = strtoull(fields[1], NULL, 10);
			metric_add_labels("openvpn_bytes", &val, DATATYPE_UINT, carg, "kind", (char *)kind);
			found = 1;
			continue;
		}
	}

	if (saw_server)
		metric_add_auto("openvpn_connected_clients", &nclients, DATATYPE_UINT, carg);
	else if (have_load_clients)
		metric_add_auto("openvpn_connected_clients", &nclients_load, DATATYPE_UINT, carg);

	free(copy);
	carg->parser_status = found ? 1 : 0;
	carglog(carg, L_DEBUG, "openvpn_handler: status=%d clients=%"u64"\n",
		carg->parser_status, nclients);
}

int8_t openvpn_validator(context_arg *carg, char *data, size_t size)
{
	(void)carg;
	if (!data || size < 3)
		return 0;
	if (strstr(data, "\nEND") || strstr(data, "\r\nEND"))
		return 1;
	if (size >= 3 && !memcmp(data + size - 3, "END", 3))
		return 1;
	if (size >= 4 && !memcmp(data + size - 4, "END\n", 4))
		return 1;
	if (size >= 5 && !memcmp(data + size - 5, "END\r\n", 5))
		return 1;
	return 0;
}

string *openvpn_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	const char *secret;

	(void)arg;
	(void)env;
	(void)proxy_settings;
	if (!hi)
		return NULL;
	if (hi->proto == APROTO_FILE || hi->proto == APROTO_PROCESS)
		return NULL;
	if (hi->proto == APROTO_HTTP || hi->proto == APROTO_HTTPS)
		return NULL;

	secret = NULL;
	if (hi->pass && hi->pass[0])
		secret = hi->pass;
	else if (hi->user && hi->user[0])
		secret = hi->user;

	if (secret) {
		string *s = string_init(64 + strlen(secret));
		string_sprintf(s, "%s\nload-stats\nstatus 2\nquit\n", secret);
		return s;
	}
	return string_init_alloc("load-stats\nstatus 2\nquit\n", 0);
}

void openvpn_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("openvpn");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = openvpn_handler;
	actx->handler[0].validator = openvpn_validator;
	actx->handler[0].mesg_func = openvpn_mesg;
	strlcpy(actx->handler[0].key, "openvpn", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
