#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/rtime.h"
#include "common/logs.h"
#include "main.h"

/* FRR VTY framing: write enable\0cmd\0; reply is enable_out\0cmd_out\0. */

static int frr_env_flag(context_arg *carg, const char *key, int default_on)
{
	if (!carg || !carg->env || !key)
		return default_on;
	env_struct *es = alligator_ht_search(carg->env, env_struct_compare, (char *)key, tommy_strhash_u32(0, key));
	if (!es || !es->v || !es->v[0])
		return default_on;
	const char *v = es->v;
	if ((v[0] == '0' && v[1] == 0) || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off"))
		return 0;
	if ((v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
		return 1;
	return default_on;
}

static void frr_tolower_inplace(char *s)
{
	if (!s)
		return;
	for (; *s; ++s)
		*s = (char)tolower((unsigned char)*s);
}

static void frr_socket_dir(const char *host, char *dir, size_t dirsz)
{
	const char *slash;

	if (!host || !host[0] || !dir || !dirsz) {
		if (dir && dirsz)
			strlcpy(dir, "/var/run/frr", dirsz);
		return;
	}
	slash = strrchr(host, '/');
	if (!slash || slash == host) {
		strlcpy(dir, "/var/run/frr", dirsz);
		return;
	}
	{
		size_t n = (size_t)(slash - host);
		if (n >= dirsz)
			n = dirsz - 1;
		memcpy(dir, host, n);
		dir[n] = 0;
	}
}

/*
 * FRR VTY: each reply ends with NUL. We write enable\0cmd\0, so the scrape is
 * complete only when the buffer ends with NUL and contains at least two NULs
 * (enable reply + command reply). Requiring a trailing NUL matches frr_exporter's
 * read loop (stop when a read chunk ends with NUL) and avoids treating a large
 * command body as done if an early NUL somehow appears before the final frame.
 */
int8_t frr_validator(context_arg *carg, char *data, size_t size)
{
	size_t i;
	int nuls = 0;

	(void)carg;
	if (!data || size < 2)
		return 0;
	if (data[size - 1] != 0)
		return 0;
	for (i = 0; i < size; i++) {
		if (data[i] == 0)
			nuls++;
	}
	return nuls >= 2 ? 1 : 0;
}

/*
 * Skip enable reply (bytes before the first NUL). Command payload is everything
 * after that until the final trailing NUL. Intermediate NULs (split VTY frames)
 * are left in place for the caller that copies via frr_command_body_dup, which
 * strips them while copying into a contiguous buffer for JSON/text parsers.
 */
int frr_extract_command_body(const char *metrics, size_t size, const char **out, size_t *out_len)
{
	const char *p;
	const char *end;
	const char *last;

	if (!metrics || !out || !out_len || size < 2)
		return -1;
	if (metrics[size - 1] != 0)
		return -1;
	end = metrics + size;
	p = memchr(metrics, 0, size);
	if (!p || p + 1 >= end)
		return -1;
	p++;
	/* Final byte is NUL; payload ends just before it. */
	last = end - 1;
	if (p > last)
		return -1;
	*out = p;
	*out_len = (size_t)(last - p);
	return 0;
}

/* Copy command body, dropping any intermediate NULs between enable and final frame. */
char *frr_command_body_dup(const char *metrics, size_t size, size_t *out_len)
{
	const char *payload;
	size_t payload_len;
	char *dst;
	size_t i;
	size_t n = 0;

	if (out_len)
		*out_len = 0;
	if (frr_extract_command_body(metrics, size, &payload, &payload_len) || !payload)
		return NULL;
	dst = malloc(payload_len + 1);
	if (!dst)
		return NULL;
	for (i = 0; i < payload_len; i++) {
		if (payload[i] != 0)
			dst[n++] = payload[i];
	}
	dst[n] = 0;
	if (out_len)
		*out_len = n;
	return dst;
}

string *frr_build_cmd(const char *cmd)
{
	size_t cmdlen;
	size_t len;
	char *buf;

	if (!cmd || !cmd[0])
		return NULL;
	cmdlen = strlen(cmd);
	len = 6 + 1 + cmdlen + 1; /* enable\0cmd\0 */
	buf = calloc(1, len + 1);
	if (!buf)
		return NULL;
	memcpy(buf, "enable", 6);
	buf[6] = 0;
	memcpy(buf + 7, cmd, cmdlen);
	buf[7 + cmdlen] = 0;
	return string_init_add(buf, len, len);
}

string *frr_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	(void)env;
	(void)proxy_settings;
	if (!hi || hi->transport != APROTO_UNIX)
		return NULL;
	return frr_build_cmd("show version");
}

static void frr_metric_help(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "frr_status_up", METRIC_TYPE_GAUGE,
		"FRR status (1 = up and responding, 0 = down or unreachable).");
	namespace_metric_family_set(NULL, carg, "frr_collector_up", METRIC_TYPE_GAUGE,
		"Whether the collector's last scrape was successful (1 = successful, 0 = unsuccessful).");
	namespace_metric_family_set(NULL, carg, "frr_scrape_duration_seconds", METRIC_TYPE_GAUGE,
		"Time it took for a collector's scrape to complete.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_rib_count_total", METRIC_TYPE_GAUGE,
		"Number of routes in the RIB.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_rib_memory_bytes", METRIC_TYPE_GAUGE,
		"Memory consumed by the RIB.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peers_count_total", METRIC_TYPE_GAUGE,
		"Number peers configured.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peers_memory_bytes", METRIC_TYPE_GAUGE,
		"Memory consumed by peers.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_groups_count_total", METRIC_TYPE_GAUGE,
		"Number of peer groups configured.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_groups_memory_bytes", METRIC_TYPE_GAUGE,
		"Memory consumed by peer groups.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_message_received_total", METRIC_TYPE_COUNTER,
		"Number of received messages.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_message_sent_total", METRIC_TYPE_COUNTER,
		"Number of sent messages.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_prefixes_received_count_total", METRIC_TYPE_GAUGE,
		"Number of prefixes received.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_prefixes_advertised_count_total", METRIC_TYPE_GAUGE,
		"Number of prefixes advertised.");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_state", METRIC_TYPE_GAUGE,
		"State of the peer (2 = Administratively Down, 1 = Established, 0 = Down).");
	namespace_metric_family_set(NULL, carg, "frr_bgp_peer_uptime_seconds", METRIC_TYPE_GAUGE,
		"How long has the peer been up.");
	namespace_metric_family_set(NULL, carg, "frr_bfd_peer_count", METRIC_TYPE_GAUGE,
		"Number of peers detected.");
	namespace_metric_family_set(NULL, carg, "frr_bfd_peer_uptime", METRIC_TYPE_GAUGE,
		"Uptime of bfd peer in seconds.");
	namespace_metric_family_set(NULL, carg, "frr_bfd_peer_state", METRIC_TYPE_GAUGE,
		"State of the bfd peer (1 = Up, 0 = Down).");
	namespace_metric_family_set(NULL, carg, "frr_ospf_neighbors", METRIC_TYPE_GAUGE,
		"Number of neighbors detected.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_neighbor_adjacencies", METRIC_TYPE_GAUGE,
		"Number of neighbor adjacencies formed.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_lsa_external_counter", METRIC_TYPE_GAUGE,
		"Number of external LSAs.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_lsa_as_opaque_counter", METRIC_TYPE_GAUGE,
		"Number of AS Opaque LSAs.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_area_lsa_number", METRIC_TYPE_GAUGE,
		"Number of LSAs in the area.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_area_lsa_network_number", METRIC_TYPE_GAUGE,
		"Number of network LSAs in the area.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_area_lsa_summary_number", METRIC_TYPE_GAUGE,
		"Number of summary LSAs in the area.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_area_lsa_asbr_number", METRIC_TYPE_GAUGE,
		"Number of ASBR LSAs in the area.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_area_lsa_nssa_number", METRIC_TYPE_GAUGE,
		"Number of NSSA LSAs in the area.");
	namespace_metric_family_set(NULL, carg, "frr_ospf_neighbor_state", METRIC_TYPE_GAUGE,
		"OSPF neighbor state (1=Down … 7=Full).");
	namespace_metric_family_set(NULL, carg, "frr_ospf_data_ls_max_age", METRIC_TYPE_GAUGE,
		"Amount of link state max age entries.");
	namespace_metric_family_set(NULL, carg, "frr_route_total", METRIC_TYPE_GAUGE,
		"Total number of routes.");
	namespace_metric_family_set(NULL, carg, "frr_route_total_fib", METRIC_TYPE_GAUGE,
		"Total number of routes in FIB.");
}

static void frr_collector_meta(context_arg *carg, const char *name, int ok, double seconds)
{
	uint64_t up = ok ? 1 : 0;
	metric_add_labels("frr_collector_up", &up, DATATYPE_UINT, carg, "collector", (char *)name);
	metric_add_labels("frr_scrape_duration_seconds", &seconds, DATATYPE_DOUBLE, carg, "collector", (char *)name);
}

int frr_parse_status_version(const char *text, size_t size, char *version, size_t vsz, char *os, size_t osz)
{
	char line[512];
	uint64_t i = 0;
	const char *p;
	const char *on;

	if (!text || !version || !os || !vsz || !osz)
		return -1;
	version[0] = 0;
	os[0] = 0;
	str_get_next(text, line, sizeof(line), "\n", &i);
	p = strstr(line, "FRRouting ");
	if (!p)
		return -1;
	p += 10;
	{
		size_t n = strcspn(p, " \t(");
		if (n >= vsz)
			n = vsz - 1;
		memcpy(version, p, n);
		version[n] = 0;
	}
	on = strstr(line, " on ");
	if (on) {
		on += 4;
		size_t n = strlen(on);
		while (n && (on[n - 1] == '.' || on[n - 1] == '\r' || on[n - 1] == '\n'))
			n--;
		if (n >= osz)
			n = osz - 1;
		memcpy(os, on, n);
		os[n] = 0;
	}
	(void)size;
	return 0;
}

void frr_parse_status(const char *body, size_t size, context_arg *carg)
{
	char version[128];
	char os[256];
	uint64_t up = 0;

	if (frr_parse_status_version(body, size, version, sizeof(version), os, sizeof(os)) == 0)
		up = 1;
	else {
		strlcpy(version, "unknown", sizeof(version));
		strlcpy(os, "unknown", sizeof(os));
	}
	metric_add_labels2("frr_status_up", &up, DATATYPE_UINT, carg, "version", version, "os", os);
}

static uint64_t frr_json_u64(json_t *o, const char *key)
{
	json_t *v = json_object_get(o, key);
	if (!v)
		return 0;
	if (json_is_integer(v))
		return (uint64_t)json_integer_value(v);
	if (json_is_real(v))
		return (uint64_t)json_real_value(v);
	if (json_is_string(v))
		return strtoull(json_string_value(v), NULL, 10);
	return 0;
}

static const char *frr_json_str(json_t *o, const char *key, const char *def)
{
	json_t *v = json_object_get(o, key);
	const char *s;
	if (!v || !json_is_string(v))
		return def;
	s = json_string_value(v);
	return s ? s : def;
}

int frr_parse_bgp_summary(const char *body, size_t size, context_arg *carg, const char *afi)
{
	json_error_t err;
	json_t *root;
	const char *vrf_name;
	json_t *vrf_obj;

	if (!body || !size || !carg || !afi)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		return -1;
	}

	json_object_foreach(root, vrf_name, vrf_obj) {
		const char *safi_key;
		json_t *safi_obj;
		char vrf_l[128];

		if (!json_is_object(vrf_obj))
			continue;
		strlcpy(vrf_l, vrf_name, sizeof(vrf_l));
		frr_tolower_inplace(vrf_l);

		json_object_foreach(vrf_obj, safi_key, safi_obj) {
			char safi_l[64];
			char asbuf[32];
			char peer_asbuf[32];
			uint64_t peer_count;
			const char *peer_ip;
			json_t *peer_obj;
			alligator_ht *plabels;

			if (!json_is_object(safi_obj))
				continue;
			/* ipv4Unicast / ipv6Unicast → unicast */
			if (strlen(safi_key) > 4)
				strlcpy(safi_l, safi_key + 4, sizeof(safi_l));
			else
				strlcpy(safi_l, safi_key, sizeof(safi_l));
			frr_tolower_inplace(safi_l);

			peer_count = frr_json_u64(safi_obj, "peerCount");
			if (!peer_count)
				continue;

			snprintf(asbuf, sizeof(asbuf), "%" PRIu64, frr_json_u64(safi_obj, "as"));

			plabels = alligator_ht_init(NULL);
			labels_hash_insert_nocache(plabels, "vrf", vrf_l);
			labels_hash_insert_nocache(plabels, "afi", (char *)afi);
			labels_hash_insert_nocache(plabels, "safi", safi_l);
			labels_hash_insert_nocache(plabels, "local_as", asbuf);
			{
				uint64_t rib = frr_json_u64(safi_obj, "ribCount");
				uint64_t ribm = frr_json_u64(safi_obj, "ribMemory");
				uint64_t pc = peer_count;
				uint64_t pm = frr_json_u64(safi_obj, "peerMemory");
				uint64_t pgc = frr_json_u64(safi_obj, "peerGroupCount");
				uint64_t pgm = frr_json_u64(safi_obj, "peerGroupMemory");
				/* metric_add consumes the HT; dup for every emit except the last. */
				metric_add("frr_bgp_rib_count_total", labels_dup(plabels), &rib, DATATYPE_UINT, carg);
				metric_add("frr_bgp_rib_memory_bytes", labels_dup(plabels), &ribm, DATATYPE_UINT, carg);
				metric_add("frr_bgp_peers_count_total", labels_dup(plabels), &pc, DATATYPE_UINT, carg);
				metric_add("frr_bgp_peers_memory_bytes", labels_dup(plabels), &pm, DATATYPE_UINT, carg);
				metric_add("frr_bgp_peer_groups_count_total", labels_dup(plabels), &pgc, DATATYPE_UINT, carg);
				metric_add("frr_bgp_peer_groups_memory_bytes", plabels, &pgm, DATATYPE_UINT, carg);
			}

			{
				json_t *peers = json_object_get(safi_obj, "peers");
				if (!peers || !json_is_object(peers))
					continue;
				json_object_foreach(peers, peer_ip, peer_obj) {
					char state_l[64];
					double peer_state = 0;
					uint64_t msg_r;
					uint64_t msg_s;
					uint64_t pfx;
					double uptime;
					alligator_ht *labels;

					if (!json_is_object(peer_obj))
						continue;
					snprintf(peer_asbuf, sizeof(peer_asbuf), "%" PRIu64, frr_json_u64(peer_obj, "remoteAs"));
					strlcpy(state_l, frr_json_str(peer_obj, "state", ""), sizeof(state_l));
					frr_tolower_inplace(state_l);
					if (!strcmp(state_l, "established"))
						peer_state = 1;
					else if (!strcmp(state_l, "idle (admin)"))
						peer_state = 2;

					msg_r = frr_json_u64(peer_obj, "msgRcvd");
					msg_s = frr_json_u64(peer_obj, "msgSent");
					pfx = frr_json_u64(peer_obj, "prefixReceivedCount");
					if (!pfx)
						pfx = frr_json_u64(peer_obj, "pfxRcd");
					uptime = (double)frr_json_u64(peer_obj, "peerUptimeMsec") * 0.001;

					labels = alligator_ht_init(NULL);
					labels_hash_insert_nocache(labels, "vrf", vrf_l);
					labels_hash_insert_nocache(labels, "afi", (char *)afi);
					labels_hash_insert_nocache(labels, "safi", safi_l);
					labels_hash_insert_nocache(labels, "local_as", asbuf);
					labels_hash_insert_nocache(labels, "peer", (char *)peer_ip);
					labels_hash_insert_nocache(labels, "peer_as", peer_asbuf);

					metric_add("frr_bgp_peer_message_received_total", labels_dup(labels), &msg_r, DATATYPE_UINT, carg);
					metric_add("frr_bgp_peer_message_sent_total", labels_dup(labels), &msg_s, DATATYPE_UINT, carg);
					metric_add("frr_bgp_peer_prefixes_received_count_total", labels_dup(labels), &pfx, DATATYPE_UINT, carg);
					metric_add("frr_bgp_peer_uptime_seconds", labels_dup(labels), &uptime, DATATYPE_DOUBLE, carg);
					if (json_object_get(peer_obj, "pfxSnt")) {
						uint64_t snt = frr_json_u64(peer_obj, "pfxSnt");
						metric_add("frr_bgp_peer_prefixes_advertised_count_total", labels_dup(labels), &snt, DATATYPE_UINT, carg);
					}
					metric_add("frr_bgp_peer_state", labels, &peer_state, DATATYPE_DOUBLE, carg);
				}
			}
		}
	}
	json_decref(root);
	return 0;
}

int frr_parse_bfd_peers(const char *body, size_t size, context_arg *carg)
{
	json_error_t err;
	json_t *root;
	size_t idx;
	json_t *peer;
	uint64_t count = 0;

	if (!body || !size || !carg)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_array(root)) {
		if (root)
			json_decref(root);
		return -1;
	}
	count = json_array_size(root);
	metric_add_auto("frr_bfd_peer_count", &count, DATATYPE_UINT, carg);

	json_array_foreach(root, idx, peer) {
		const char *local = frr_json_str(peer, "local", "");
		const char *peer_a = frr_json_str(peer, "peer", "");
		const char *iface = frr_json_str(peer, "interface", "");
		const char *vrf = frr_json_str(peer, "vrf", "");
		const char *status = frr_json_str(peer, "status", "");
		uint64_t uptime = frr_json_u64(peer, "uptime");
		uint64_t state = (status && !strcasecmp(status, "up")) ? 1 : 0;
		alligator_ht *labels = alligator_ht_init(NULL);

		labels_hash_insert_nocache(labels, "local", (char *)local);
		labels_hash_insert_nocache(labels, "peer", (char *)peer_a);
		labels_hash_insert_nocache(labels, "iface", (char *)iface);
		labels_hash_insert_nocache(labels, "vrf", (char *)vrf);
		metric_add("frr_bfd_peer_uptime", labels_dup(labels), &uptime, DATATYPE_UINT, carg);
		metric_add("frr_bfd_peer_state", labels, &state, DATATYPE_UINT, carg);
	}
	json_decref(root);
	return 0;
}

int frr_parse_route_summary(const char *body, size_t size, context_arg *carg, const char *afi)
{
	json_error_t err;
	json_t *root;
	const char *vrf;
	json_t *vrf_obj;

	if (!body || !size || !carg || !afi)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root) {
		return -1;
	}
	if (json_is_object(root) && json_object_get(root, "routesTotal")) {
		/* single VRF (older FRR) */
		uint64_t total = frr_json_u64(root, "routesTotal");
		uint64_t fib = frr_json_u64(root, "routesTotalFib");
		metric_add_labels2("frr_route_total", &total, DATATYPE_UINT, carg, "afi", (char *)afi, "vrf", "default");
		metric_add_labels2("frr_route_total_fib", &fib, DATATYPE_UINT, carg, "afi", (char *)afi, "vrf", "default");
		json_decref(root);
		return 0;
	}
	if (!json_is_object(root)) {
		json_decref(root);
		return -1;
	}
	json_object_foreach(root, vrf, vrf_obj) {
		uint64_t total;
		uint64_t fib;
		if (!json_is_object(vrf_obj))
			continue;
		total = frr_json_u64(vrf_obj, "routesTotal");
		fib = frr_json_u64(vrf_obj, "routesTotalFib");
		metric_add_labels2("frr_route_total", &total, DATATYPE_UINT, carg, "afi", (char *)afi, "vrf", (char *)vrf);
		metric_add_labels2("frr_route_total_fib", &fib, DATATYPE_UINT, carg, "afi", (char *)afi, "vrf", (char *)vrf);
	}
	json_decref(root);
	return 0;
}

int frr_parse_ospf(const char *body, size_t size, context_arg *carg)
{
	json_error_t err;
	json_t *root;
	const char *vrf;
	json_t *vrf_obj;

	if (!body || !size || !carg)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		return -1;
	}
	json_object_foreach(root, vrf, vrf_obj) {
		char vrf_l[128];
		json_t *areas;
		const char *area_name;
		json_t *area;
		uint64_t ext;
		uint64_t opaque;

		if (!json_is_object(vrf_obj))
			continue;
		strlcpy(vrf_l, vrf, sizeof(vrf_l));
		frr_tolower_inplace(vrf_l);
		ext = frr_json_u64(vrf_obj, "lsaExternalCounter");
		opaque = frr_json_u64(vrf_obj, "lsaAsopaqueCounter");
		metric_add_labels("frr_ospf_lsa_external_counter", &ext, DATATYPE_UINT, carg, "vrf", vrf_l);
		metric_add_labels("frr_ospf_lsa_as_opaque_counter", &opaque, DATATYPE_UINT, carg, "vrf", vrf_l);

		areas = json_object_get(vrf_obj, "areas");
		if (!areas || !json_is_object(areas))
			continue;
		json_object_foreach(areas, area_name, area) {
			uint64_t n = frr_json_u64(area, "lsaNumber");
			uint64_t net = frr_json_u64(area, "lsaNetworkNumber");
			uint64_t sum = frr_json_u64(area, "lsaSummaryNumber");
			uint64_t asbr = frr_json_u64(area, "lsaAsbrNumber");
			uint64_t nssa = frr_json_u64(area, "lsaNssaNumber");
			metric_add_labels2("frr_ospf_area_lsa_number", &n, DATATYPE_UINT, carg, "vrf", vrf_l, "area", (char *)area_name);
			metric_add_labels2("frr_ospf_area_lsa_network_number", &net, DATATYPE_UINT, carg, "vrf", vrf_l, "area", (char *)area_name);
			metric_add_labels2("frr_ospf_area_lsa_summary_number", &sum, DATATYPE_UINT, carg, "vrf", vrf_l, "area", (char *)area_name);
			metric_add_labels2("frr_ospf_area_lsa_asbr_number", &asbr, DATATYPE_UINT, carg, "vrf", vrf_l, "area", (char *)area_name);
			metric_add_labels2("frr_ospf_area_lsa_nssa_number", &nssa, DATATYPE_UINT, carg, "vrf", vrf_l, "area", (char *)area_name);
		}
	}
	json_decref(root);
	return 0;
}

int frr_parse_ospf_interface(const char *body, size_t size, context_arg *carg)
{
	json_error_t err;
	json_t *root;
	const char *vrf_name;
	json_t *vrf_data;

	if (!body || !size || !carg)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		return -1;
	}

	json_object_foreach(root, vrf_name, vrf_data) {
		char vrf_l[128];
		const char *ikey;
		json_t *ival;
		json_t *ifaces;

		if (!strcmp(vrf_name, "ospfInstance") || !json_is_object(vrf_data))
			continue;
		strlcpy(vrf_l, vrf_name, sizeof(vrf_l));
		frr_tolower_inplace(vrf_l);

		ifaces = json_object_get(vrf_data, "interfaces");
		if (ifaces && json_is_object(ifaces)) {
			json_object_foreach(ifaces, ikey, ival) {
				json_t *passive = json_object_get(ival, "timerPassiveIface");
				if (passive && json_is_true(passive))
					continue;
				{
					uint64_t nbr = frr_json_u64(ival, "nbrCount");
					uint64_t adj = frr_json_u64(ival, "nbrAdjacentCount");
					const char *area = frr_json_str(ival, "area", "");
					metric_add_labels3("frr_ospf_neighbors", &nbr, DATATYPE_UINT, carg,
						"vrf", vrf_l, "iface", (char *)ikey, "area", (char *)area);
					metric_add_labels3("frr_ospf_neighbor_adjacencies", &adj, DATATYPE_UINT, carg,
						"vrf", vrf_l, "iface", (char *)ikey, "area", (char *)area);
				}
			}
			continue;
		}

		/* Older layout: interface keys sit beside vrfName/vrfId. */
		json_object_foreach(vrf_data, ikey, ival) {
			json_t *passive;
			if (!strcmp(ikey, "vrfName") || !strcmp(ikey, "vrfId") || !strcmp(ikey, "interfaces"))
				continue;
			if (!json_is_object(ival))
				continue;
			passive = json_object_get(ival, "timerPassiveIface");
			if (passive && json_is_true(passive))
				continue;
			if (!json_object_get(ival, "nbrCount") && !json_object_get(ival, "area"))
				continue;
			{
				uint64_t nbr = frr_json_u64(ival, "nbrCount");
				uint64_t adj = frr_json_u64(ival, "nbrAdjacentCount");
				const char *area = frr_json_str(ival, "area", "");
				metric_add_labels3("frr_ospf_neighbors", &nbr, DATATYPE_UINT, carg,
					"vrf", vrf_l, "iface", (char *)ikey, "area", (char *)area);
				metric_add_labels3("frr_ospf_neighbor_adjacencies", &adj, DATATYPE_UINT, carg,
					"vrf", vrf_l, "iface", (char *)ikey, "area", (char *)area);
			}
		}
	}
	json_decref(root);
	return 0;
}

static double frr_ospf_state_num(const char *state)
{
	if (!state)
		return 0;
	if (!strcasecmp(state, "Down"))
		return 1;
	if (!strcasecmp(state, "Init"))
		return 2;
	if (!strcasecmp(state, "2-Way"))
		return 3;
	if (!strcasecmp(state, "ExStart"))
		return 4;
	if (!strcasecmp(state, "Exchange"))
		return 5;
	if (!strcasecmp(state, "Loading"))
		return 6;
	if (!strcasecmp(state, "Full"))
		return 7;
	return 0;
}

int frr_parse_ospf_neighbor(const char *body, size_t size, context_arg *carg)
{
	json_error_t err;
	json_t *root;
	const char *vrf;
	json_t *vrf_obj;

	if (!body || !size || !carg)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		return -1;
	}
	json_object_foreach(root, vrf, vrf_obj) {
		char vrf_l[128];
		json_t *neighbors;
		const char *nbr_name;
		json_t *nbr_list;

		if (!json_is_object(vrf_obj))
			continue;
		strlcpy(vrf_l, vrf, sizeof(vrf_l));
		frr_tolower_inplace(vrf_l);
		neighbors = json_object_get(vrf_obj, "neighbors");
		if (!neighbors || !json_is_object(neighbors))
			continue;
		json_object_foreach(neighbors, nbr_name, nbr_list) {
			size_t i;
			json_t *nbr;
			if (!json_is_array(nbr_list))
				continue;
			json_array_foreach(nbr_list, i, nbr) {
				const char *raw_state = frr_json_str(nbr, "nbrState", NULL);
				const char *iface_raw;
				char iface[128];
				char local_addr[128];
				char state_tok[64];
				char *slash;
				char *colon;
				double st;
				alligator_ht *labels;

				if (!raw_state || !raw_state[0])
					raw_state = frr_json_str(nbr, "state", "");
				strlcpy(state_tok, raw_state, sizeof(state_tok));
				slash = strchr(state_tok, '/');
				if (slash)
					*slash = 0;
				st = frr_ospf_state_num(state_tok);
				if (st == 0)
					continue;

				iface_raw = frr_json_str(nbr, "ifaceName", "");
				strlcpy(iface, iface_raw, sizeof(iface));
				local_addr[0] = 0;
				colon = strchr(iface, ':');
				if (colon) {
					strlcpy(local_addr, colon + 1, sizeof(local_addr));
					*colon = 0;
				}
				labels = alligator_ht_init(NULL);
				labels_hash_insert_nocache(labels, "vrf", vrf_l);
				labels_hash_insert_nocache(labels, "neighbor", (char *)nbr_name);
				labels_hash_insert_nocache(labels, "iface", iface);
				labels_hash_insert_nocache(labels, "local_address", local_addr);
				labels_hash_insert_nocache(labels, "remote_address", (char *)frr_json_str(nbr, "address", ""));
				metric_add("frr_ospf_neighbor_state", labels, &st, DATATYPE_DOUBLE, carg);
			}
		}
	}
	json_decref(root);
	return 0;
}

int frr_parse_ospf_max_age(const char *body, size_t size, context_arg *carg)
{
	json_error_t err;
	json_t *root;
	const char *vrf;
	json_t *vrf_obj;

	if (!body || !size || !carg)
		return -1;
	root = json_loadb(body, size, 0, &err);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		return -1;
	}
	json_object_foreach(root, vrf, vrf_obj) {
		char vrf_l[128];
		json_t *maxage;
		uint64_t n = 0;

		if (!json_is_object(vrf_obj))
			continue;
		strlcpy(vrf_l, vrf, sizeof(vrf_l));
		frr_tolower_inplace(vrf_l);
		maxage = json_object_get(vrf_obj, "maxAgeLinkStates");
		if (maxage && json_is_object(maxage))
			n = json_object_size(maxage);
		metric_add_labels("frr_ospf_data_ls_max_age", &n, DATATYPE_UINT, carg, "vrf", vrf_l);
	}
	json_decref(root);
	return 0;
}

/* Run one VTY command against daemon socket; fill *body_out (caller frees). */
static int frr_exec_cmd(context_arg *carg, const char *sock_dir, const char *daemon_sock, const char *cmd, char **body_out, size_t *body_len)
{
	char url[HOSTHEADER_SIZE + 16];
	string *mesg;
	aggregator_await_res_t res;
	context_arg hint;
	char *key;
	size_t payload_len;
	int rc;

	if (body_out)
		*body_out = NULL;
	if (body_len)
		*body_len = 0;

	snprintf(url, sizeof(url), "unix://%s/%s", sock_dir, daemon_sock);
	mesg = frr_build_cmd(cmd);
	if (!mesg)
		return -1;

	key = malloc(320);
	if (!key) {
		string_free(mesg);
		return -1;
	}
	snprintf(key, 320, "frr:%s:%s", daemon_sock, cmd);

	memset(&hint, 0, sizeof(hint));
	hint.timeout = carg->timeout ? carg->timeout : 20000;
	hint.log_level = carg->log_level;
	hint.log_ch = carg->log_ch;
	hint.log_ch_raw = carg->log_ch_raw;
	hint.log_ch_raw_n = carg->log_ch_raw_n;
	hint.log_ch_out = carg->log_ch_out;
	hint.log_ch_out_n = carg->log_ch_out_n;
	hint.labels = carg->labels;
	memset(&res, 0, sizeof(res));

	{
		char *m = mesg->s;
		size_t ml = mesg->l;
		mesg->s = NULL;
		string_free(mesg);
		rc = aggregator_oneshot_await(&hint, url, strlen(url), m, ml,
			NULL, "frr", frr_validator, key, 0, NULL, NULL, 0, NULL, NULL, &res);
	}

	if (rc != 0 || !res.body || !res.body_len) {
		aggregator_await_res_free(&res);
		return -1;
	}
	{
		char *dup = frr_command_body_dup(res.body, res.body_len, &payload_len);
		aggregator_await_res_free(&res);
		if (!dup || !payload_len) {
			free(dup);
			return -1;
		}
		if (body_out) {
			*body_out = dup;
			if (body_len)
				*body_len = payload_len;
		} else {
			free(dup);
		}
		return 0;
	}
}

static void frr_run_collector(context_arg *carg, const char *sock_dir, const char *name,
	int (*fn)(context_arg *carg, const char *sock_dir))
{
	r_time t0 = setrtime();
	int ok = fn(carg, sock_dir) == 0;
	r_time t1 = setrtime();
	frr_collector_meta(carg, name, ok, getrtime_sec_float(t1, t0));
}

static int frr_collect_bgp(context_arg *carg, const char *sock_dir)
{
	char *body = NULL;
	size_t blen = 0;
	int rc;

	if (frr_exec_cmd(carg, sock_dir, "bgpd.vty", "show bgp vrf all ipv4 unicast summary json", &body, &blen))
		return -1;
	rc = frr_parse_bgp_summary(body, blen, carg, "ipv4");
	free(body);
	return rc;
}

static int frr_collect_bgp6(context_arg *carg, const char *sock_dir)
{
	char *body = NULL;
	size_t blen = 0;
	int rc;

	if (frr_exec_cmd(carg, sock_dir, "bgpd.vty", "show bgp vrf all ipv6 unicast summary json", &body, &blen))
		return -1;
	rc = frr_parse_bgp_summary(body, blen, carg, "ipv6");
	free(body);
	return rc;
}

static int frr_collect_bfd(context_arg *carg, const char *sock_dir)
{
	char *body = NULL;
	size_t blen = 0;
	int rc;

	if (frr_exec_cmd(carg, sock_dir, "bfdd.vty", "show bfd peers json", &body, &blen))
		return -1;
	rc = frr_parse_bfd_peers(body, blen, carg);
	free(body);
	return rc;
}

static int frr_collect_route(context_arg *carg, const char *sock_dir)
{
	char *b4 = NULL;
	char *b6 = NULL;
	size_t l4 = 0, l6 = 0;
	int rc = 0;

	if (frr_exec_cmd(carg, sock_dir, "zebra.vty", "show ip route vrf all summary json", &b4, &l4))
		return -1;
	if (frr_parse_route_summary(b4, l4, carg, "ipv4"))
		rc = -1;
	free(b4);

	if (frr_exec_cmd(carg, sock_dir, "zebra.vty", "show ipv6 route vrf all summary json", &b6, &l6))
		return -1;
	if (frr_parse_route_summary(b6, l6, carg, "ipv6"))
		rc = -1;
	free(b6);
	return rc;
}

static int frr_collect_ospf(context_arg *carg, const char *sock_dir)
{
	char *body = NULL;
	size_t blen = 0;
	int rc = 0;

	if (frr_exec_cmd(carg, sock_dir, "ospfd.vty", "show ip ospf vrf all json", &body, &blen))
		return -1;
	if (frr_parse_ospf(body, blen, carg))
		rc = -1;
	free(body);

	if (frr_exec_cmd(carg, sock_dir, "ospfd.vty", "show ip ospf vrf all interface json", &body, &blen))
		return -1;
	if (frr_parse_ospf_interface(body, blen, carg))
		rc = -1;
	free(body);

	if (frr_exec_cmd(carg, sock_dir, "ospfd.vty", "show ip ospf vrf all neighbor json", &body, &blen))
		return -1;
	if (frr_parse_ospf_neighbor(body, blen, carg))
		rc = -1;
	free(body);

	if (frr_exec_cmd(carg, sock_dir, "ospfd.vty", "show ip ospf vrf all database max-age json", &body, &blen))
		return -1;
	if (frr_parse_ospf_max_age(body, blen, carg))
		rc = -1;
	free(body);
	return rc;
}

static int frr_collect_status_body(context_arg *carg, const char *body, size_t size)
{
	frr_parse_status(body, size, carg);
	return 0;
}

void frr_handler(char *metrics, size_t size, context_arg *carg)
{
	char sock_dir[HOSTHEADER_SIZE];
	size_t cmd_len = 0;
	r_time t0;
	r_time t1;
	int status_ok = 0;

	if (!carg)
		return;

	frr_metric_help(carg);
	frr_socket_dir(carg->host, sock_dir, sizeof(sock_dir));

	t0 = setrtime();
	{
		char *dup = frr_command_body_dup(metrics, size, &cmd_len);
		if (dup && cmd_len) {
			frr_collect_status_body(carg, dup, cmd_len);
			status_ok = 1;
		} else {
			char version[] = "unknown";
			char os[] = "unknown";
			uint64_t up = 0;
			metric_add_labels2("frr_status_up", &up, DATATYPE_UINT, carg, "version", version, "os", os);
		}
		free(dup);
	}
	t1 = setrtime();
	frr_collector_meta(carg, "status", status_ok, getrtime_sec_float(t1, t0));

	if (frr_env_flag(carg, "collect.bgp", 1))
		frr_run_collector(carg, sock_dir, "bgp", frr_collect_bgp);
	if (frr_env_flag(carg, "collect.bgp6", 0))
		frr_run_collector(carg, sock_dir, "bgp6", frr_collect_bgp6);
	if (frr_env_flag(carg, "collect.bfd", 1))
		frr_run_collector(carg, sock_dir, "bfd", frr_collect_bfd);
	if (frr_env_flag(carg, "collect.ospf", 1))
		frr_run_collector(carg, sock_dir, "ospf", frr_collect_ospf);
	if (frr_env_flag(carg, "collect.route", 1))
		frr_run_collector(carg, sock_dir, "route", frr_collect_route);

	carg->parser_status = status_ok ? 1 : 0;
}

int frr_aggregator(context_arg *carg)
{
	size_t hlen;

	if (!carg)
		return 0;
	hlen = strlen(carg->host);
	if (hlen < 4 || strcmp(carg->host + hlen - 4, ".vty")) {
		char tmp[HOSTHEADER_SIZE];
		if (hlen && carg->host[hlen - 1] == '/')
			snprintf(tmp, sizeof(tmp), "%szebra.vty", carg->host);
		else
			snprintf(tmp, sizeof(tmp), "%s/zebra.vty", carg->host);
		strlcpy(carg->host, tmp, HOSTHEADER_SIZE);
	}
	return smart_aggregator(carg);
}

void frr_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("frr");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = frr_handler;
	actx->handler[0].validator = frr_validator;
	actx->handler[0].mesg_func = frr_mesg;
	actx->handler[0].smart_aggregator_replace = frr_aggregator;
	strlcpy(actx->handler[0].key, "frr", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
