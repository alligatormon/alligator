#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "common/selector.h"
#include "common/url.h"
#include "common/validator.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/logs.h"
#include "main.h"
#include "parsers/snmp_mib.h"

extern int env_struct_compare(const void *arg, const void *obj);

#define SNMP_PDU_GET_REQUEST     0xa0
#define SNMP_PDU_GETNEXT_REQUEST 0xa1
#define SNMP_PDU_GET_RESPONSE    0xa2

#define SNMP_TAG_COUNTER32  0x41
#define SNMP_TAG_GAUGE32    0x42
#define SNMP_TAG_TIMETICKS  0x43
#define SNMP_TAG_OPAQUE     0x44
#define SNMP_TAG_COUNTER64  0x46
#define SNMP_TAG_IPADDR     0x40

#define SNMP_MAX_PACKET 2048
#define SNMP_RECV_MAX   65536
#define SNMP_OID_LABEL 384
#define SNMP_METRIC_NAME_MAX 512
#ifndef SNMP_WALK_MAX_STEPS
#define SNMP_WALK_MAX_STEPS 100000u
#endif

static void snmp_octets_to_label(const uint8_t *p, size_t n, char *out, size_t outcap);

static int snmp_env_metric_from_oid(context_arg *carg)
{
	if (!carg || !carg->env)
		return 0;
	env_struct *es = alligator_ht_search(carg->env, env_struct_compare, "snmp_metric_from_oid",
					     tommy_strhash_u32(0, "snmp_metric_from_oid"));
	if (!es || !es->v)
		return 0;
	const char *v = es->v;
	return (v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "yes") ||
	       !strcasecmp(v, "on");
}

static const char *snmp_env_lookup(context_arg *carg, const char *key)
{
	if (!carg || !carg->env)
		return NULL;
	env_struct *es = alligator_ht_search(carg->env, env_struct_compare, key, tommy_strhash_u32(0, key));
	return (es && es->v && es->v[0]) ? es->v : NULL;
}

/*
 * Optional env snmp_oid_strip_prefix="1.3.6.1.2.1" → oid label shows suffix (e.g. 1.1.1.0).
 * Use with snmp_mib="SNMPv2-MIB" (literal label; no MIB file parsing).
 */
static void snmp_oid_label_relative(context_arg *carg, const char *full_oid, char *out, size_t outs)
{
	const char *pre = snmp_env_lookup(carg, "snmp_oid_strip_prefix");
	if (!pre || !pre[0]) {
		strlcpy(out, full_oid, outs);
		return;
	}
	size_t pl = strlen(pre);
	if (strncmp(full_oid, pre, pl) != 0) {
		strlcpy(out, full_oid, outs);
		return;
	}
	if (full_oid[pl] == '\0') {
		out[0] = 0;
		return;
	}
	if (full_oid[pl] != '.') {
		strlcpy(out, full_oid, outs);
		return;
	}
	strlcpy(out, full_oid + pl + 1, outs);
}

static void snmp_ml1(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels2(metric, val, ty, carg, "mib", (char *)mib, k1, v1);
	else
		metric_add_labels(metric, val, ty, carg, k1, v1);
}

static void snmp_ml2(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1, char *k2,
		     char *v2)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels3(metric, val, ty, carg, "mib", (char *)mib, k1, v1, k2, v2);
	else
		metric_add_labels2(metric, val, ty, carg, k1, v1, k2, v2);
}

static void snmp_ml3(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1, char *k2,
		     char *v2, char *k3, char *v3)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels4(metric, val, ty, carg, "mib", (char *)mib, k1, v1, k2, v2, k3, v3);
	else
		metric_add_labels3(metric, val, ty, carg, k1, v1, k2, v2, k3, v3);
}

static void snmp_ml4(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1, char *k2,
		     char *v2, char *k3, char *v3, char *k4, char *v4)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels5(metric, val, ty, carg, "mib", (char *)mib, k1, v1, k2, v2, k3, v3, k4, v4);
	else
		metric_add_labels4(metric, val, ty, carg, k1, v1, k2, v2, k3, v3, k4, v4);
}

static void snmp_ml5(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1, char *k2,
		     char *v2, char *k3, char *v3, char *k4, char *v4, char *k5, char *v5)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels6(metric, val, ty, carg, "mib", (char *)mib, k1, v1, k2, v2, k3, v3, k4, v4, k5, v5);
	else
		metric_add_labels5(metric, val, ty, carg, k1, v1, k2, v2, k3, v3, k4, v4, k5, v5);
}

static void snmp_ml6(context_arg *carg, char *metric, void *val, int8_t ty, char *k1, char *v1, char *k2,
		     char *v2, char *k3, char *v3, char *k4, char *v4, char *k5, char *v5, char *k6, char *v6)
{
	const char *mib = snmp_env_lookup(carg, "snmp_mib");
	if (mib)
		metric_add_labels7(metric, val, ty, carg, "mib", (char *)mib, k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6);
	else
		metric_add_labels6(metric, val, ty, carg, k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6);
}

/* Default on; set snmp_oid_symbols=0 to disable well-known MODULE-IDENTITY names. */
static int snmp_env_oid_symbols_enabled(context_arg *carg)
{
	const char *v = snmp_env_lookup(carg, "snmp_oid_symbols");
	if (!v)
		return 1;
	return !((v[0] == '0' && v[1] == 0) || !strcasecmp(v, "false") || !strcasecmp(v, "off") ||
		 !strcasecmp(v, "no"));
}

/*
 * scrape env snmp_debug: unset/0/off = none; 1/on = mib reload + mib_miss→builtin; 2/verbose/all = every symbol.
 * Logs use carglog(..., L_DEBUG, ...) — raise this target's log_level to debug to see them.
 */
static int snmp_env_debug_level(context_arg *carg)
{
	const char *v = snmp_env_lookup(carg, "snmp_debug");
	if (!v || !v[0])
		return 0;
	if ((v[0] == '0' && v[1] == 0) || !strcasecmp(v, "false") || !strcasecmp(v, "off") || !strcasecmp(v, "no"))
		return 0;
	if ((v[0] == '2' && v[1] == 0) || !strcasecmp(v, "verbose") || !strcasecmp(v, "all") ||
	    !strcasecmp(v, "symbols"))
		return 2;
	if ((v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcasecmp(v, "yes"))
		return 1;
	{
		int n = atoi(v);
		if (n >= 2)
			return 2;
		if (n == 1)
			return 1;
	}
	return 1;
}

/* Opt-in: decode well-known TCP connection table indices / enums into labels. */
static int snmp_env_decode_tcp_enabled(context_arg *carg)
{
	const char *v = snmp_env_lookup(carg, "snmp_decode_tcp");
	if (!v)
		return 0;
	return (v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static int snmp_env_decode_tcp_metric_names_enabled(context_arg *carg)
{
	const char *v = snmp_env_lookup(carg, "snmp_decode_tcp_metric_names");
	if (!v)
		return 1;
	return !((v[0] == '0' && v[1] == 0) || !strcasecmp(v, "false") || !strcasecmp(v, "off") || !strcasecmp(v, "no"));
}

/* Optional generic OID->metric renaming for common SNMP tables. */
static int snmp_env_friendly_names_enabled(context_arg *carg)
{
	const char *v = snmp_env_lookup(carg, "snmp_friendly_names");
	if (!v)
		return 0;
	return (v[0] == '1' && v[1] == 0) || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static int snmp_str_is_dotted_oid(const char *s)
{
	if (!s || !s[0] || !strchr(s, '.'))
		return 0;
	if (!isdigit((unsigned char)s[0]))
		return 0;
	for (const char *p = s; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (isdigit(c) || c == '.')
			continue;
		return 0;
	}
	return 1;
}

static int snmp_oid_to_parts(const char *dotted, uint32_t *out, size_t cap, size_t *nparts)
{
	*nparts = 0;
	if (!dotted || !dotted[0])
		return -1;
	const char *p = dotted;
	while (*p) {
		if (*nparts >= cap)
			return -1;
		char *end = NULL;
		unsigned long x = strtoul(p, &end, 10);
		if (end == p)
			return -1;
		out[(*nparts)++] = (uint32_t)x;
		if (*end == '.')
			p = end + 1;
		else if (*end == '\0')
			break;
		else
			return -1;
	}
	return 0;
}

typedef struct snmp_oid_name_ent {
	alligator_ht_node node;
	const char *prefix;
	const char *name;
} snmp_oid_name_ent;

static alligator_ht *g_snmp_oid_name_ht;
static int g_snmp_oid_name_ht_inited;

static int snmp_oid_name_compare(const void *arg, const void *obj)
{
	const char *key = arg;
	const snmp_oid_name_ent *e = obj;
	return strcmp(key, e->prefix);
}

static void snmp_oid_name_map_init(void)
{
	if (g_snmp_oid_name_ht_inited)
		return;
	g_snmp_oid_name_ht_inited = 1;
	g_snmp_oid_name_ht = alligator_ht_init(NULL);
	if (!g_snmp_oid_name_ht)
		return;

	static snmp_oid_name_ent tab[] = {
		/* TCP */
		{ .prefix = "1.3.6.1.2.1.6.13.1.1", .name = "snmp_tcp_conn_state" },
		{ .prefix = "1.3.6.1.2.1.6.13.1.2", .name = "snmp_tcp_conn_local_address_info" },
		{ .prefix = "1.3.6.1.2.1.6.13.1.3", .name = "snmp_tcp_conn_local_port" },
		{ .prefix = "1.3.6.1.2.1.6.13.1.4", .name = "snmp_tcp_conn_remote_address_info" },
		{ .prefix = "1.3.6.1.2.1.6.13.1.5", .name = "snmp_tcp_conn_remote_port" },
		{ .prefix = "1.3.6.1.2.1.6.19.1.7.1.4", .name = "snmp_tcp_connection_state" },
		{ .prefix = "1.3.6.1.2.1.6.19.1.8.1.4", .name = "snmp_tcp_connection_process" },
		{ .prefix = "1.3.6.1.2.1.6.20.1.4.1.4", .name = "snmp_tcp_listener_process" },
		/* UDP */
		{ .prefix = "1.3.6.1.2.1.7.5.1.1", .name = "snmp_udp_endpoint_local_address_info" },
		{ .prefix = "1.3.6.1.2.1.7.5.1.2", .name = "snmp_udp_endpoint_local_port" },
		{ .prefix = "1.3.6.1.2.1.7.5.1.3", .name = "snmp_udp_endpoint_remote_address_info" },
		{ .prefix = "1.3.6.1.2.1.7.5.1.4", .name = "snmp_udp_endpoint_remote_port" },
		{ .prefix = "1.3.6.1.2.1.7.5.1.8", .name = "snmp_udp_endpoint_process" },
		/* IP route */
		{ .prefix = "1.3.6.1.2.1.4.21.1.1", .name = "snmp_ip_route_dest_info" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.2", .name = "snmp_ip_route_ifindex" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.7", .name = "snmp_ip_route_next_hop_info" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.8", .name = "snmp_ip_route_type" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.3", .name = "snmp_ip_route_metric1" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.9", .name = "snmp_ip_route_proto" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.11", .name = "snmp_ip_route_mask_info" },
		{ .prefix = "1.3.6.1.2.1.4.21.1.13", .name = "snmp_ip_route_info" },
		/* CIDR route */
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.1", .name = "snmp_ip_cidr_route_dest_info" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.2", .name = "snmp_ip_cidr_route_mask_info" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.3", .name = "snmp_ip_cidr_route_tos" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.4", .name = "snmp_ip_cidr_route_next_hop_info" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.5", .name = "snmp_ip_cidr_route_type" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.6", .name = "snmp_ip_cidr_route_proto" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.7", .name = "snmp_ip_cidr_route_age" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.9", .name = "snmp_ip_cidr_route_info" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.10", .name = "snmp_ip_cidr_route_next_hop_as" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.11", .name = "snmp_ip_cidr_route_metric1" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.12", .name = "snmp_ip_cidr_route_metric2" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.13", .name = "snmp_ip_cidr_route_metric3" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.14", .name = "snmp_ip_cidr_route_metric4" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.15", .name = "snmp_ip_cidr_route_metric5" },
		{ .prefix = "1.3.6.1.2.1.4.24.4.1.16", .name = "snmp_ip_cidr_route_status" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.7", .name = "snmp_ip_default_router_address_info" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.8", .name = "snmp_ip_default_router_ifindex" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.9", .name = "snmp_ip_default_router_lifetime" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.10", .name = "snmp_ip_default_router_preference" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.11", .name = "snmp_ip_default_router_inbound_hoplimit" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.12", .name = "snmp_ip_default_router_reachable_time" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.13", .name = "snmp_ip_default_router_retransmit_time" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.14", .name = "snmp_ip_default_router_row_status" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.15", .name = "snmp_ip_default_router_prefix_length" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.16", .name = "snmp_ip_default_router_origin" },
		{ .prefix = "1.3.6.1.2.1.4.24.7.1.17", .name = "snmp_ip_default_router_onlink_flag" },
		/* HOST-RESOURCES */
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.2", .name = "snmp_hr_storage_type_info" },
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.1", .name = "snmp_hr_storage_index" },
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.3", .name = "snmp_hr_storage_descr" },
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.4", .name = "snmp_hr_storage_allocation_units" },
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.5", .name = "snmp_hr_storage_size" },
		{ .prefix = "1.3.6.1.2.1.25.2.3.1.6", .name = "snmp_hr_storage_used" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.2", .name = "snmp_hr_device_type_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.3", .name = "snmp_hr_device_descr" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.4", .name = "snmp_hr_device_id_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.1", .name = "snmp_hr_device_index" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.5", .name = "snmp_hr_device_status" },
		{ .prefix = "1.3.6.1.2.1.25.3.2.1.6", .name = "snmp_hr_device_errors" },
		{ .prefix = "1.3.6.1.2.1.25.3.3.1.1", .name = "snmp_hr_processor_firmware_id_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.3.1.2", .name = "snmp_hr_processor_load" },
		{ .prefix = "1.3.6.1.2.1.25.3.4.1.1", .name = "snmp_hr_processor_firmware_id" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.2", .name = "snmp_hr_swrun_name" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.3", .name = "snmp_hr_swrun_path" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.4", .name = "snmp_hr_swrun_parameters" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.5", .name = "snmp_hr_swrun_type" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.7", .name = "snmp_hr_swrun_status" },
		{ .prefix = "1.3.6.1.2.1.25.5.1.1.1", .name = "snmp_hr_swrunperf_cpu" },
		{ .prefix = "1.3.6.1.2.1.25.5.1.1.2", .name = "snmp_hr_swrunperf_mem" },
		{ .prefix = "1.3.6.1.2.1.25.6.3.1.2", .name = "snmp_hr_swinstalled_type_info" },
		{ .prefix = "1.3.6.1.2.1.25.6.3.1.3", .name = "snmp_hr_swinstalled_name" },
		{ .prefix = "1.3.6.1.2.1.25.6.3.1.1", .name = "snmp_hr_swinstalled_index" },
		{ .prefix = "1.3.6.1.2.1.25.6.3.1.4", .name = "snmp_hr_swinstalled_id_info" },
		{ .prefix = "1.3.6.1.2.1.25.6.3.1.5", .name = "snmp_hr_swinstalled_date_info" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.1", .name = "snmp_hr_swrun_index" },
		{ .prefix = "1.3.6.1.2.1.25.4.2.1.6", .name = "snmp_hr_swrun_id_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.8", .name = "snmp_hr_partition_label" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.9", .name = "snmp_hr_partition_id_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.1", .name = "snmp_hr_partition_index" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.2", .name = "snmp_hr_partition_label_text" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.3", .name = "snmp_hr_partition_mount_point" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.4", .name = "snmp_hr_partition_fs_type_info" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.5", .name = "snmp_hr_partition_size" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.6", .name = "snmp_hr_partition_used" },
		{ .prefix = "1.3.6.1.2.1.25.3.8.1.7", .name = "snmp_hr_partition_allocation_failures" },
		{ .prefix = "1.3.6.1.2.1.3.1.1.2", .name = "snmp_at_phys_address_info" },
		{ .prefix = "1.3.6.1.2.1.4.22.1.2", .name = "snmp_ip_net_to_media_phys_address_info" },
		{ .prefix = "1.3.6.1.2.1.4.35.1.4", .name = "snmp_ip_net_to_physical_phys_address_info" },
		{ .prefix = "1.3.6.1.2.1.88.1.3.1.1.4", .name = "snmp_notification_log_event_count" },
		{ .prefix = "1.3.6.1.2.1.88.1.3.1.1.5", .name = "snmp_notification_log_event_interval" },
		{ .prefix = "1.3.6.1.2.1.88.1.3.1.1.3", .name = "snmp_notification_log_event_notification_id_info" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.2.1.4", .name = "snmp_notification_log_variable_counter32" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.2.1.5", .name = "snmp_notification_log_variable_unsigned32" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.2.1.2", .name = "snmp_notification_log_variable_oid_info" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.2.1.3", .name = "snmp_notification_log_variable_value" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.3.1.1", .name = "snmp_notification_log_varbinds_notification_id_info" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.3.1.2", .name = "snmp_notification_log_varbinds_sysuptime" },
		{ .prefix = "1.3.6.1.2.1.88.1.4.3.1.3", .name = "snmp_notification_log_varbinds_timestamp" },
		{ .prefix = "1.3.6.1.4.1.8072.1.2.1.1.6", .name = "snmp_net_snmp_notification_value" },
		{ .prefix = "1.3.6.1.4.1.8072.1.2.1.1.4", .name = "snmp_net_snmp_notification_oid_info" },
		{ .prefix = "1.3.6.1.4.1.8072.1.5.3.1.2", .name = "snmp_net_snmp_agentx_session_open_errors" },
		{ .prefix = "1.3.6.1.4.1.8072.1.5.3.1.3", .name = "snmp_net_snmp_agentx_session_register_errors" },
		{ .prefix = "1.3.6.1.4.1.8072.1.9.1.1.2", .name = "snmp_net_snmp_vacm_auth_type" },
		{ .prefix = "1.3.6.1.4.1.8072.1.9.1.1.3", .name = "snmp_net_snmp_vacm_group_name" },
		{ .prefix = "1.3.6.1.4.1.8072.1.9.1.1.4", .name = "snmp_net_snmp_vacm_context_prefix" },
		{ .prefix = "1.3.6.1.4.1.8072.1.9.1.1.5", .name = "snmp_net_snmp_vacm_view_name" },
		{ .prefix = "1.3.6.1.4.1.2021.13.15.1.1", .name = "snmp_ucd_disk_io_index" },
		{ .prefix = "1.3.6.1.4.1.2021.13.16.2.1", .name = "snmp_ucd_diskio_metric" },
		{ .prefix = "1.3.6.1.4.1.2021.10.1.3", .name = "snmp_ucd_load_average_string" },
		{ .prefix = "1.3.6.1.2.1.7.7.1.8", .name = "snmp_udp_endpoint_process" },
		{ .prefix = "1.3.6.1.2.1.28.2.1.31.1", .name = "snmp_mib2_28_2_1_31_1_value" },
		{ .prefix = "1.3.6.1.2.1.3.1.1.1", .name = "snmp_at_ifindex" },
		{ .prefix = "1.3.6.1.2.1.3.1.1.3", .name = "snmp_at_net_address_info" },
		{ .prefix = "1.3.6.1.2.1.4.22.1.1", .name = "snmp_ip_net_to_media_ifindex" },
		{ .prefix = "1.3.6.1.2.1.4.22.1.3", .name = "snmp_ip_net_to_media_net_address_info" },
		{ .prefix = "1.3.6.1.2.1.4.22.1.4", .name = "snmp_ip_net_to_media_type" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.3", .name = "snmp_ip_net_to_physical_type" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.4", .name = "snmp_ip_net_to_physical_state" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.5", .name = "snmp_ip_net_to_physical_last_updated" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.6", .name = "snmp_ip_net_to_physical_validity" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.7", .name = "snmp_ip_net_to_physical_row_status" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.8", .name = "snmp_ip_net_to_physical_storage_type" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.9", .name = "snmp_ip_net_to_physical_vlan_info" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.10", .name = "snmp_ip_net_to_physical_origin" },
		{ .prefix = "1.3.6.1.2.1.4.34.1.11", .name = "snmp_ip_net_to_physical_status" },
		{ .prefix = "1.3.6.1.2.1.4.35.1.5", .name = "snmp_ip_net_to_physical_type_legacy" },
		{ .prefix = "1.3.6.1.2.1.4.35.1.6", .name = "snmp_ip_net_to_physical_state_legacy" },
		{ .prefix = "1.3.6.1.2.1.4.35.1.7", .name = "snmp_ip_net_to_physical_last_updated_legacy" },
		{ .prefix = "1.3.6.1.2.1.4.35.1.8", .name = "snmp_ip_net_to_physical_validity_legacy" },
		{ .prefix = "1.3.6.1.2.1.4.20.1.2", .name = "snmp_ip_address_ifindex" },
		{ .prefix = "1.3.6.1.2.1.4.20.1.4", .name = "snmp_ip_address_reasm_max_size" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.1", .name = "snmp_ip_if_stats_ip_version" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.2", .name = "snmp_ip_if_stats_ifindex" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.3", .name = "snmp_ip_if_stats_in_receives" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.4", .name = "snmp_ip_if_stats_in_octets" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.5", .name = "snmp_ip_if_stats_in_hdr_errors" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.6", .name = "snmp_ip_if_stats_in_no_routes" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.7", .name = "snmp_ip_if_stats_in_addr_errors" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.8", .name = "snmp_ip_if_stats_in_unknown_protos" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.9", .name = "snmp_ip_if_stats_in_truncated_pkts" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.10", .name = "snmp_ip_if_stats_in_forw_datagrams" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.11", .name = "snmp_ip_if_stats_reasm_reqds" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.12", .name = "snmp_ip_if_stats_reasm_oks" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.13", .name = "snmp_ip_if_stats_reasm_fails" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.14", .name = "snmp_ip_if_stats_in_discards" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.15", .name = "snmp_ip_if_stats_in_delivers" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.16", .name = "snmp_ip_if_stats_out_requests" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.17", .name = "snmp_ip_if_stats_out_forw_datagrams" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.18", .name = "snmp_ip_if_stats_out_discards" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.19", .name = "snmp_ip_if_stats_out_frag_reqds" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.20", .name = "snmp_ip_if_stats_out_frag_oks" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.21", .name = "snmp_ip_if_stats_out_frag_fails" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.22", .name = "snmp_ip_if_stats_out_frag_creates" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.23", .name = "snmp_ip_if_stats_discontinuity_time" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.24", .name = "snmp_ip_if_stats_refresh_rate" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.25", .name = "snmp_ip_if_stats_hc_in_receives" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.26", .name = "snmp_ip_if_stats_hc_in_octets" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.27", .name = "snmp_ip_if_stats_hc_in_forw_datagrams" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.28", .name = "snmp_ip_if_stats_hc_in_delivers" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.29", .name = "snmp_ip_if_stats_hc_out_requests" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.30", .name = "snmp_ip_if_stats_hc_out_forw_datagrams" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.31", .name = "snmp_ip_if_stats_hc_out_frag_reqds" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.32", .name = "snmp_ip_if_stats_hc_out_frag_oks" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.33", .name = "snmp_ip_if_stats_hc_out_frag_fails" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.34", .name = "snmp_ip_if_stats_hc_out_frag_creates" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.35", .name = "snmp_ip_if_stats_col_35" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.36", .name = "snmp_ip_if_stats_col_36" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.37", .name = "snmp_ip_if_stats_col_37" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.38", .name = "snmp_ip_if_stats_col_38" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.39", .name = "snmp_ip_if_stats_col_39" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.40", .name = "snmp_ip_if_stats_col_40" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.41", .name = "snmp_ip_if_stats_col_41" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.42", .name = "snmp_ip_if_stats_col_42" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.43", .name = "snmp_ip_if_stats_col_43" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.44", .name = "snmp_ip_if_stats_col_44" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.45", .name = "snmp_ip_if_stats_col_45" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.46", .name = "snmp_ip_if_stats_col_46" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1.47", .name = "snmp_ip_if_stats_col_47" },
		{ .prefix = "1.3.6.1.2.1.4.31.1.1", .name = "snmp_ip_if_stats" },
		{ .prefix = "1.3.6.1.2.1.4.31.3.1", .name = "snmp_ip_system_stats" },
		{ .prefix = "1.3.6.1.2.1.5.30.1.4", .name = "snmp_icmp_msg_stats_out_type" },
		{ .prefix = "1.3.6.1.2.1.5.30.1.3", .name = "snmp_icmp_msg_stats_in_type" },
		{ .prefix = "1.3.6.1.2.1.6.20.1.4.2", .name = "snmp_tcp_listener_process_ipv6" },
		{ .prefix = "1.3.6.1.2.1.6.19.1.7.2", .name = "snmp_tcp_connection_state_ipv6" },
		{ .prefix = "1.3.6.1.2.1.6.19.1.8.2", .name = "snmp_tcp_connection_process_ipv6" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.1", .name = "snmp_if_name" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.2", .name = "snmp_if_in_multicast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.3", .name = "snmp_if_in_broadcast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.4", .name = "snmp_if_out_multicast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.5", .name = "snmp_if_out_broadcast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.6", .name = "snmp_if_hc_in_octets" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.7", .name = "snmp_if_hc_in_ucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.8", .name = "snmp_if_hc_in_multicast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.9", .name = "snmp_if_hc_in_broadcast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.10", .name = "snmp_if_hc_out_octets" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.11", .name = "snmp_if_hc_out_ucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.12", .name = "snmp_if_hc_out_multicast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.13", .name = "snmp_if_hc_out_broadcast_pkts" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.14", .name = "snmp_if_link_up_down_trap_enable" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.15", .name = "snmp_if_high_speed" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.16", .name = "snmp_if_promiscuous_mode" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.17", .name = "snmp_if_connector_present" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.18", .name = "snmp_if_alias" },
		{ .prefix = "1.3.6.1.2.1.31.1.1.1.19", .name = "snmp_if_counter_discontinuity_time" },
		{ .prefix = "1.3.6.1.4.1.8072.1.7.2.1", .name = "snmp_net_snmp_extend_result" },
		{ .prefix = "1.3.6.1.6.3.16.1.5.2.1", .name = "snmp_vacm_access_context_prefix_info" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.4", .name = "snmp_vacm_access_read_view_name" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.5", .name = "snmp_vacm_access_write_view_name" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.6", .name = "snmp_vacm_access_notify_view_name" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.7", .name = "snmp_vacm_access_storage_type_legacy" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.8", .name = "snmp_vacm_access_context_match" },
		{ .prefix = "1.3.6.1.6.3.16.1.4.1.9", .name = "snmp_vacm_access_storage_type" },
		{ .prefix = "1.3.6.1.6.3.16.1.1.1.1", .name = "snmp_vacm_context_name" },
		{ .prefix = "1.3.6.1.6.3.16.1.2.1.4", .name = "snmp_vacm_security_to_group_name" },
		{ .prefix = "1.3.6.1.6.3.16.1.2.1.3", .name = "snmp_vacm_security_to_group_name_text" },
		{ .prefix = "1.3.6.1.6.3.16.1.2.1.5", .name = "snmp_vacm_security_to_group_storage_type" },
		{ .prefix = "1.3.6.1.2.1.1.9.1.4", .name = "snmp_sysor_up_time" },
		{ .prefix = "1.3.6.1.2.1.1.9.1.2", .name = "snmp_sysor_id_info" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.1", .name = "snmp_dot3_stats_index" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.2", .name = "snmp_dot3_alignment_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.3", .name = "snmp_dot3_fcs_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.4", .name = "snmp_dot3_single_collision_frames" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.5", .name = "snmp_dot3_multiple_collision_frames" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.6", .name = "snmp_dot3_sqe_test_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.7", .name = "snmp_dot3_deferred_transmissions" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.8", .name = "snmp_dot3_late_collisions" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.9", .name = "snmp_dot3_excessive_collisions" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.10", .name = "snmp_dot3_internal_mac_transmit_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.11", .name = "snmp_dot3_carrier_sense_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.12", .name = "snmp_dot3_frame_too_longs" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.13", .name = "snmp_dot3_internal_mac_receive_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.14", .name = "snmp_dot3_ether_chip_set" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.15", .name = "snmp_dot3_frame_too_shorts" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.16", .name = "snmp_dot3_symbol_errors" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.17", .name = "snmp_dot3_duplex_status" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.18", .name = "snmp_dot3_rate_control_ability" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1.19", .name = "snmp_dot3_rate_control_status" },
		{ .prefix = "1.3.6.1.2.1.10.7.2.1", .name = "snmp_dot3_stats" },
		{ .prefix = "1.3.6.1.2.1.4.20.1.1", .name = "snmp_ip_ad_ent_addr_info" },
		{ .prefix = "1.3.6.1.2.1.4.20.1.3", .name = "snmp_ip_ad_ent_netmask_info" },
		{ .prefix = "1.3.6.1.2.1.104.1.1", .name = "snmp_frame_relay_lmi_stats" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.1", .name = "snmp_frame_relay_lmi_full_enquiry_sent" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.2", .name = "snmp_frame_relay_lmi_full_enquiry_rcvd" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.3", .name = "snmp_frame_relay_lmi_error_rcvd" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.4", .name = "snmp_frame_relay_lmi_timeout_rcvd" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.5", .name = "snmp_frame_relay_lmi_status_enquiry_rcvd" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.6", .name = "snmp_frame_relay_lmi_update_status_rcvd" },
		{ .prefix = "1.3.6.1.2.1.104.1.2.7", .name = "snmp_frame_relay_lmi_invalid_msg_rcvd" },
		{ .prefix = "1.3.6.1.2.1.16.1.1.1.1", .name = "snmp_bridge_base_port_ifindex" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.1", .name = "snmp_if_index" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.2", .name = "snmp_if_descr" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.3", .name = "snmp_if_type" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.4", .name = "snmp_if_mtu" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.5", .name = "snmp_if_speed" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.6", .name = "snmp_if_phys_address_info" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.7", .name = "snmp_if_admin_status" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.8", .name = "snmp_if_oper_status" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.9", .name = "snmp_if_last_change" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.10", .name = "snmp_if_in_octets" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.11", .name = "snmp_if_in_ucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.12", .name = "snmp_if_in_nucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.13", .name = "snmp_if_in_discards" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.14", .name = "snmp_if_in_errors" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.15", .name = "snmp_if_in_unknown_protos" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.16", .name = "snmp_if_out_octets" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.17", .name = "snmp_if_out_ucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.18", .name = "snmp_if_out_nucast_pkts" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.19", .name = "snmp_if_out_discards" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.20", .name = "snmp_if_out_errors" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.21", .name = "snmp_if_out_qlen_legacy" },
		{ .prefix = "1.3.6.1.2.1.2.2.1.22", .name = "snmp_if_specific_info" },
		{ .prefix = "1.3.6.1.2.1.4.32.1.5", .name = "snmp_ip_address_prefix_advert_onlink_flag" },
		{ .prefix = "1.3.6.1.2.1.4.32.1.6", .name = "snmp_ip_address_prefix_advert_autonomous_flag" },
		{ .prefix = "1.3.6.1.2.1.4.32.1.7", .name = "snmp_ip_address_prefix_advert_preferred_lifetime" },
		{ .prefix = "1.3.6.1.2.1.4.32.1.8", .name = "snmp_ip_address_prefix_advert_valid_lifetime" },
		{ .prefix = "1.3.6.1.2.1.4.32.1.9", .name = "snmp_ip_address_prefix_advert_row_status" },
		{ .prefix = "1.3.6.1.2.1.4.37.1.4", .name = "snmp_ip_default_router_advert_prefix_len" },
		{ .prefix = "1.3.6.1.2.1.4.37.1.5", .name = "snmp_ip_default_router_advert_preference" },
		{ .prefix = "1.3.6.1.4.1.8072.1.2.1.1.5", .name = "snmp_net_snmp_notification_value_info" },
		{ .prefix = "1.3.6.1.4.1.2021.10.1.4", .name = "snmp_ucd_load_average_real" },
		{ .prefix = "1.3.6.1.4.1.2021.10.1.101", .name = "snmp_ucd_load_average_error" },
		{ .prefix = "1.3.6.1.4.1.2021.10.1.6", .name = "snmp_ucd_la_load_float" }, /* UCD-SNMP-MIB::laLoadFloat */
		{ .prefix = "1.3.6.1.2.1.1.3.0", .name = "snmp_sys_up_time" }, /* DISMAN-EVENT-MIB::sysUpTimeInstance */
		{ .prefix = "1.3.6.1.2.1.1.8.0", .name = "snmp_mib2_sys_or_last_change" }, /* SNMPv2-MIB::sysORLastChange.0 */
		{ .prefix = "1.3.6.1.2.1.11.1.0", .name = "snmp_mib2_in_pkts" }, /* SNMPv2-MIB::snmpInPkts.0 */
		{ .prefix = "1.3.6.1.2.1.11.10.0", .name = "snmp_mib2_in_bad_values" }, /* SNMPv2-MIB::snmpInBadValues.0 */
		{ .prefix = "1.3.6.1.2.1.11.11.0", .name = "snmp_mib2_in_read_onlys" }, /* SNMPv2-MIB::snmpInReadOnlys.0 */
		{ .prefix = "1.3.6.1.2.1.11.12.0", .name = "snmp_mib2_in_gen_errs" }, /* SNMPv2-MIB::snmpInGenErrs.0 */
		{ .prefix = "1.3.6.1.2.1.11.13.0", .name = "snmp_mib2_in_total_req_vars" }, /* SNMPv2-MIB::snmpInTotalReqVars.0 */
		{ .prefix = "1.3.6.1.2.1.11.14.0", .name = "snmp_mib2_in_total_set_vars" }, /* SNMPv2-MIB::snmpInTotalSetVars.0 */
		{ .prefix = "1.3.6.1.2.1.11.15.0", .name = "snmp_mib2_in_get_requests" }, /* SNMPv2-MIB::snmpInGetRequests.0 */
		{ .prefix = "1.3.6.1.2.1.11.16.0", .name = "snmp_mib2_in_get_nexts" }, /* SNMPv2-MIB::snmpInGetNexts.0 */
		{ .prefix = "1.3.6.1.2.1.11.17.0", .name = "snmp_mib2_in_set_requests" }, /* SNMPv2-MIB::snmpInSetRequests.0 */
		{ .prefix = "1.3.6.1.2.1.11.18.0", .name = "snmp_mib2_in_get_responses" }, /* SNMPv2-MIB::snmpInGetResponses.0 */
		{ .prefix = "1.3.6.1.2.1.11.19.0", .name = "snmp_mib2_in_traps" }, /* SNMPv2-MIB::snmpInTraps.0 */
		{ .prefix = "1.3.6.1.2.1.11.2.0", .name = "snmp_mib2_out_pkts" }, /* SNMPv2-MIB::snmpOutPkts.0 */
		{ .prefix = "1.3.6.1.2.1.11.20.0", .name = "snmp_mib2_out_too_bigs" }, /* SNMPv2-MIB::snmpOutTooBigs.0 */
		{ .prefix = "1.3.6.1.2.1.11.21.0", .name = "snmp_mib2_out_no_such_names" }, /* SNMPv2-MIB::snmpOutNoSuchNames.0 */
		{ .prefix = "1.3.6.1.2.1.11.22.0", .name = "snmp_mib2_out_bad_values" }, /* SNMPv2-MIB::snmpOutBadValues.0 */
		{ .prefix = "1.3.6.1.2.1.11.24.0", .name = "snmp_mib2_out_gen_errs" }, /* SNMPv2-MIB::snmpOutGenErrs.0 */
		{ .prefix = "1.3.6.1.2.1.11.25.0", .name = "snmp_mib2_out_get_requests" }, /* SNMPv2-MIB::snmpOutGetRequests.0 */
		{ .prefix = "1.3.6.1.2.1.11.26.0", .name = "snmp_mib2_out_get_nexts" }, /* SNMPv2-MIB::snmpOutGetNexts.0 */
		{ .prefix = "1.3.6.1.2.1.11.27.0", .name = "snmp_mib2_out_set_requests" }, /* SNMPv2-MIB::snmpOutSetRequests.0 */
		{ .prefix = "1.3.6.1.2.1.11.28.0", .name = "snmp_mib2_out_get_responses" }, /* SNMPv2-MIB::snmpOutGetResponses.0 */
		{ .prefix = "1.3.6.1.2.1.11.29.0", .name = "snmp_mib2_out_traps" }, /* SNMPv2-MIB::snmpOutTraps.0 */
		{ .prefix = "1.3.6.1.2.1.11.3.0", .name = "snmp_mib2_in_bad_versions" }, /* SNMPv2-MIB::snmpInBadVersions.0 */
		{ .prefix = "1.3.6.1.2.1.11.30.0", .name = "snmp_mib2_enable_authen_traps" }, /* SNMPv2-MIB::snmpEnableAuthenTraps.0 */
		{ .prefix = "1.3.6.1.2.1.11.31.0", .name = "snmp_mib2_silent_drops" }, /* SNMPv2-MIB::snmpSilentDrops.0 */
		{ .prefix = "1.3.6.1.2.1.11.32.0", .name = "snmp_mib2_proxy_drops" }, /* SNMPv2-MIB::snmpProxyDrops.0 */
		{ .prefix = "1.3.6.1.2.1.11.4.0", .name = "snmp_mib2_in_bad_community_names" }, /* SNMPv2-MIB::snmpInBadCommunityNames.0 */
		{ .prefix = "1.3.6.1.2.1.11.5.0", .name = "snmp_mib2_in_bad_community_uses" }, /* SNMPv2-MIB::snmpInBadCommunityUses.0 */
		{ .prefix = "1.3.6.1.2.1.11.6.0", .name = "snmp_mib2_in_asn_parse_errs" }, /* SNMPv2-MIB::snmpInASNParseErrs.0 */
		{ .prefix = "1.3.6.1.2.1.11.8.0", .name = "snmp_mib2_in_too_bigs" }, /* SNMPv2-MIB::snmpInTooBigs.0 */
		{ .prefix = "1.3.6.1.2.1.11.9.0", .name = "snmp_mib2_in_no_such_names" }, /* SNMPv2-MIB::snmpInNoSuchNames.0 */
		{ .prefix = "1.3.6.1.2.1.2.1.0", .name = "snmp_if_number" }, /* IF-MIB::ifNumber.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.1.0", .name = "snmp_hr_system_uptime" }, /* HOST-RESOURCES-MIB::hrSystemUptime.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.2.0", .name = "snmp_hr_system_date" }, /* HOST-RESOURCES-MIB::hrSystemDate.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.3.0", .name = "snmp_hr_system_initial_load_device" }, /* HOST-RESOURCES-MIB::hrSystemInitialLoadDevice.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.5.0", .name = "snmp_hr_system_num_users" }, /* HOST-RESOURCES-MIB::hrSystemNumUsers.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.6.0", .name = "snmp_hr_system_processes" }, /* HOST-RESOURCES-MIB::hrSystemProcesses.0 */
		{ .prefix = "1.3.6.1.2.1.25.1.7.0", .name = "snmp_hr_system_max_processes" }, /* HOST-RESOURCES-MIB::hrSystemMaxProcesses.0 */
		{ .prefix = "1.3.6.1.2.1.25.2.2.0", .name = "snmp_hr_memory_size" }, /* HOST-RESOURCES-MIB::hrMemorySize.0 */
		{ .prefix = "1.3.6.1.2.1.28.1.1.2.1", .name = "snmp_mib2_28_1_1_2_1" }, /* SNMPv2-SMI::mib-2.28.1.1.2.1 */
		{ .prefix = "1.3.6.1.2.1.28.1.1.5.1", .name = "snmp_mib2_28_1_1_5_1" }, /* SNMPv2-SMI::mib-2.28.1.1.5.1 */
		{ .prefix = "1.3.6.1.2.1.28.2.1.4.1.26", .name = "snmp_mib2_28_2_1_4_1_26" }, /* SNMPv2-SMI::mib-2.28.2.1.4.1.26 */
		{ .prefix = "1.3.6.1.2.1.28.2.1.7.1.26", .name = "snmp_mib2_28_2_1_7_1_26" }, /* SNMPv2-SMI::mib-2.28.2.1.7.1.26 */
		{ .prefix = "1.3.6.1.2.1.31.1.5.0", .name = "snmp_if_table_last_change" }, /* IF-MIB::ifTableLastChange.0 */
		{ .prefix = "1.3.6.1.2.1.4.1.0", .name = "snmp_ip_forwarding" }, /* IP-MIB::ipForwarding.0 */
		{ .prefix = "1.3.6.1.2.1.4.10.0", .name = "snmp_ip_out_requests" }, /* IP-MIB::ipOutRequests.0 */
		{ .prefix = "1.3.6.1.2.1.4.11.0", .name = "snmp_ip_out_discards" }, /* IP-MIB::ipOutDiscards.0 */
		{ .prefix = "1.3.6.1.2.1.4.12.0", .name = "snmp_ip_out_no_routes" }, /* IP-MIB::ipOutNoRoutes.0 */
		{ .prefix = "1.3.6.1.2.1.4.13.0", .name = "snmp_ip_reasm_timeout" }, /* IP-MIB::ipReasmTimeout.0 */
		{ .prefix = "1.3.6.1.2.1.4.14.0", .name = "snmp_ip_reasm_reqds" }, /* IP-MIB::ipReasmReqds.0 */
		{ .prefix = "1.3.6.1.2.1.4.15.0", .name = "snmp_ip_reasm_oks" }, /* IP-MIB::ipReasmOKs.0 */
		{ .prefix = "1.3.6.1.2.1.4.16.0", .name = "snmp_ip_reasm_fails" }, /* IP-MIB::ipReasmFails.0 */
		{ .prefix = "1.3.6.1.2.1.4.17.0", .name = "snmp_ip_frag_oks" }, /* IP-MIB::ipFragOKs.0 */
		{ .prefix = "1.3.6.1.2.1.4.18.0", .name = "snmp_ip_frag_fails" }, /* IP-MIB::ipFragFails.0 */
		{ .prefix = "1.3.6.1.2.1.4.19.0", .name = "snmp_ip_frag_creates" }, /* IP-MIB::ipFragCreates.0 */
		{ .prefix = "1.3.6.1.2.1.4.2.0", .name = "snmp_ip_default_ttl" }, /* IP-MIB::ipDefaultTTL.0 */
		{ .prefix = "1.3.6.1.2.1.4.23.0", .name = "snmp_ip_routing_discards" }, /* IP-MIB::ipRoutingDiscards.0 */
		{ .prefix = "1.3.6.1.2.1.4.24.6.0", .name = "snmp_ip_inet_cidr_route_number" }, /* IP-MIB::ip.24.6.0 */
		{ .prefix = "1.3.6.1.2.1.4.25.0", .name = "snmp_ip_ipv6_forwarding" }, /* IP-MIB::ipv6IpForwarding.0 */
		{ .prefix = "1.3.6.1.2.1.4.26.0", .name = "snmp_ip_ipv6_default_hop_limit" }, /* IP-MIB::ipv6IpDefaultHopLimit.0 */
		{ .prefix = "1.3.6.1.2.1.4.27.0", .name = "snmp_ip_ipv4_interface_table_last_change" }, /* IP-MIB::ipv4InterfaceTableLastChange.0 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.2.1", .name = "snmp_ip_ipv4_interface_reasm_max_size" }, /* IP-MIB::ipv4InterfaceReasmMaxSize.1 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.2.163", .name = "snmp_ip_ipv4_interface_reasm_max_size" }, /* IP-MIB::ipv4InterfaceReasmMaxSize.163 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.3.1", .name = "snmp_ip_ipv4_interface_enable_status" }, /* IP-MIB::ipv4InterfaceEnableStatus.1 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.3.163", .name = "snmp_ip_ipv4_interface_enable_status" }, /* IP-MIB::ipv4InterfaceEnableStatus.163 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.4.1", .name = "snmp_ip_ipv4_interface_retransmit_time" }, /* IP-MIB::ipv4InterfaceRetransmitTime.1 */
		{ .prefix = "1.3.6.1.2.1.4.28.1.4.163", .name = "snmp_ip_ipv4_interface_retransmit_time" }, /* IP-MIB::ipv4InterfaceRetransmitTime.163 */
		{ .prefix = "1.3.6.1.2.1.4.29.0", .name = "snmp_ip_ipv6_interface_table_last_change" }, /* IP-MIB::ipv6InterfaceTableLastChange.0 */
		{ .prefix = "1.3.6.1.2.1.4.3.0", .name = "snmp_ip_in_receives" }, /* IP-MIB::ipInReceives.0 */
		{ .prefix = "1.3.6.1.2.1.4.31.2.0", .name = "snmp_ip_if_stats_table_last_change" }, /* IP-MIB::ipIfStatsTableLastChange.0 */
		{ .prefix = "1.3.6.1.2.1.4.33.0", .name = "snmp_ip_address_spin_lock" }, /* IP-MIB::ipAddressSpinLock.0 */
		{ .prefix = "1.3.6.1.2.1.4.4.0", .name = "snmp_ip_in_hdr_errors" }, /* IP-MIB::ipInHdrErrors.0 */
		{ .prefix = "1.3.6.1.2.1.4.5.0", .name = "snmp_ip_in_addr_errors" }, /* IP-MIB::ipInAddrErrors.0 */
		{ .prefix = "1.3.6.1.2.1.4.6.0", .name = "snmp_ip_forw_datagrams" }, /* IP-MIB::ipForwDatagrams.0 */
		{ .prefix = "1.3.6.1.2.1.4.7.0", .name = "snmp_ip_in_unknown_protos" }, /* IP-MIB::ipInUnknownProtos.0 */
		{ .prefix = "1.3.6.1.2.1.4.8.0", .name = "snmp_ip_in_discards" }, /* IP-MIB::ipInDiscards.0 */
		{ .prefix = "1.3.6.1.2.1.4.9.0", .name = "snmp_ip_in_delivers" }, /* IP-MIB::ipInDelivers.0 */
		{ .prefix = "1.3.6.1.2.1.5.1.0", .name = "snmp_ip_icmp_in_msgs" }, /* IP-MIB::icmpInMsgs.0 */
		{ .prefix = "1.3.6.1.2.1.5.10.0", .name = "snmp_ip_icmp_in_timestamps" }, /* IP-MIB::icmpInTimestamps.0 */
		{ .prefix = "1.3.6.1.2.1.5.11.0", .name = "snmp_ip_icmp_in_timestamp_reps" }, /* IP-MIB::icmpInTimestampReps.0 */
		{ .prefix = "1.3.6.1.2.1.5.12.0", .name = "snmp_ip_icmp_in_addr_masks" }, /* IP-MIB::icmpInAddrMasks.0 */
		{ .prefix = "1.3.6.1.2.1.5.13.0", .name = "snmp_ip_icmp_in_addr_mask_reps" }, /* IP-MIB::icmpInAddrMaskReps.0 */
		{ .prefix = "1.3.6.1.2.1.5.14.0", .name = "snmp_ip_icmp_out_msgs" }, /* IP-MIB::icmpOutMsgs.0 */
		{ .prefix = "1.3.6.1.2.1.5.15.0", .name = "snmp_ip_icmp_out_errors" }, /* IP-MIB::icmpOutErrors.0 */
		{ .prefix = "1.3.6.1.2.1.5.16.0", .name = "snmp_ip_icmp_out_dest_unreachs" }, /* IP-MIB::icmpOutDestUnreachs.0 */
		{ .prefix = "1.3.6.1.2.1.5.17.0", .name = "snmp_ip_icmp_out_time_excds" }, /* IP-MIB::icmpOutTimeExcds.0 */
		{ .prefix = "1.3.6.1.2.1.5.18.0", .name = "snmp_ip_icmp_out_parm_probs" }, /* IP-MIB::icmpOutParmProbs.0 */
		{ .prefix = "1.3.6.1.2.1.5.19.0", .name = "snmp_ip_icmp_out_src_quenchs" }, /* IP-MIB::icmpOutSrcQuenchs.0 */
		{ .prefix = "1.3.6.1.2.1.5.2.0", .name = "snmp_ip_icmp_in_errors" }, /* IP-MIB::icmpInErrors.0 */
		{ .prefix = "1.3.6.1.2.1.5.20.0", .name = "snmp_ip_icmp_out_redirects" }, /* IP-MIB::icmpOutRedirects.0 */
		{ .prefix = "1.3.6.1.2.1.5.21.0", .name = "snmp_ip_icmp_out_echos" }, /* IP-MIB::icmpOutEchos.0 */
		{ .prefix = "1.3.6.1.2.1.5.22.0", .name = "snmp_ip_icmp_out_echo_reps" }, /* IP-MIB::icmpOutEchoReps.0 */
		{ .prefix = "1.3.6.1.2.1.5.23.0", .name = "snmp_ip_icmp_out_timestamps" }, /* IP-MIB::icmpOutTimestamps.0 */
		{ .prefix = "1.3.6.1.2.1.5.24.0", .name = "snmp_ip_icmp_out_timestamp_reps" }, /* IP-MIB::icmpOutTimestampReps.0 */
		{ .prefix = "1.3.6.1.2.1.5.25.0", .name = "snmp_ip_icmp_out_addr_masks" }, /* IP-MIB::icmpOutAddrMasks.0 */
		{ .prefix = "1.3.6.1.2.1.5.26.0", .name = "snmp_ip_icmp_out_addr_mask_reps" }, /* IP-MIB::icmpOutAddrMaskReps.0 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.2.1", .name = "snmp_ip_icmp_stats_in_msgs_ipv4" }, /* IP-MIB::icmpStatsInMsgs.ipv4 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.2.2", .name = "snmp_ip_icmp_stats_in_msgs_ipv6" }, /* IP-MIB::icmpStatsInMsgs.ipv6 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.3.1", .name = "snmp_ip_icmp_stats_in_errors_ipv4" }, /* IP-MIB::icmpStatsInErrors.ipv4 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.3.2", .name = "snmp_ip_icmp_stats_in_errors_ipv6" }, /* IP-MIB::icmpStatsInErrors.ipv6 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.4.1", .name = "snmp_ip_icmp_stats_out_msgs_ipv4" }, /* IP-MIB::icmpStatsOutMsgs.ipv4 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.4.2", .name = "snmp_ip_icmp_stats_out_msgs_ipv6" }, /* IP-MIB::icmpStatsOutMsgs.ipv6 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.5.1", .name = "snmp_ip_icmp_stats_out_errors_ipv4" }, /* IP-MIB::icmpStatsOutErrors.ipv4 */
		{ .prefix = "1.3.6.1.2.1.5.29.1.5.2", .name = "snmp_ip_icmp_stats_out_errors_ipv6" }, /* IP-MIB::icmpStatsOutErrors.ipv6 */
		{ .prefix = "1.3.6.1.2.1.5.3.0", .name = "snmp_ip_icmp_in_dest_unreachs" }, /* IP-MIB::icmpInDestUnreachs.0 */
		{ .prefix = "1.3.6.1.2.1.5.4.0", .name = "snmp_ip_icmp_in_time_excds" }, /* IP-MIB::icmpInTimeExcds.0 */
		{ .prefix = "1.3.6.1.2.1.5.5.0", .name = "snmp_ip_icmp_in_parm_probs" }, /* IP-MIB::icmpInParmProbs.0 */
		{ .prefix = "1.3.6.1.2.1.5.6.0", .name = "snmp_ip_icmp_in_src_quenchs" }, /* IP-MIB::icmpInSrcQuenchs.0 */
		{ .prefix = "1.3.6.1.2.1.5.7.0", .name = "snmp_ip_icmp_in_redirects" }, /* IP-MIB::icmpInRedirects.0 */
		{ .prefix = "1.3.6.1.2.1.5.8.0", .name = "snmp_ip_icmp_in_echos" }, /* IP-MIB::icmpInEchos.0 */
		{ .prefix = "1.3.6.1.2.1.5.9.0", .name = "snmp_ip_icmp_in_echo_reps" }, /* IP-MIB::icmpInEchoReps.0 */
		{ .prefix = "1.3.6.1.2.1.55.1.1.0", .name = "snmp_mib2_55_1_1_0" }, /* SNMPv2-SMI::mib-2.55.1.1.0 */
		{ .prefix = "1.3.6.1.2.1.55.1.2.0", .name = "snmp_mib2_55_1_2_0" }, /* SNMPv2-SMI::mib-2.55.1.2.0 */
		{ .prefix = "1.3.6.1.2.1.6.1.0", .name = "snmp_tcp_rto_algorithm" }, /* TCP-MIB::tcpRtoAlgorithm.0 */
		{ .prefix = "1.3.6.1.2.1.6.10.0", .name = "snmp_tcp_in_segs" }, /* TCP-MIB::tcpInSegs.0 */
		{ .prefix = "1.3.6.1.2.1.6.11.0", .name = "snmp_tcp_out_segs" }, /* TCP-MIB::tcpOutSegs.0 */
		{ .prefix = "1.3.6.1.2.1.6.12.0", .name = "snmp_tcp_retrans_segs" }, /* TCP-MIB::tcpRetransSegs.0 */
		{ .prefix = "1.3.6.1.2.1.6.14.0", .name = "snmp_tcp_in_errs" }, /* TCP-MIB::tcpInErrs.0 */
		{ .prefix = "1.3.6.1.2.1.6.15.0", .name = "snmp_tcp_out_rsts" }, /* TCP-MIB::tcpOutRsts.0 */
		{ .prefix = "1.3.6.1.2.1.6.2.0", .name = "snmp_tcp_rto_min" }, /* TCP-MIB::tcpRtoMin.0 */
		{ .prefix = "1.3.6.1.2.1.6.3.0", .name = "snmp_tcp_rto_max" }, /* TCP-MIB::tcpRtoMax.0 */
		{ .prefix = "1.3.6.1.2.1.6.4.0", .name = "snmp_tcp_max_conn" }, /* TCP-MIB::tcpMaxConn.0 */
		{ .prefix = "1.3.6.1.2.1.6.5.0", .name = "snmp_tcp_active_opens" }, /* TCP-MIB::tcpActiveOpens.0 */
		{ .prefix = "1.3.6.1.2.1.6.6.0", .name = "snmp_tcp_passive_opens" }, /* TCP-MIB::tcpPassiveOpens.0 */
		{ .prefix = "1.3.6.1.2.1.6.7.0", .name = "snmp_tcp_attempt_fails" }, /* TCP-MIB::tcpAttemptFails.0 */
		{ .prefix = "1.3.6.1.2.1.6.8.0", .name = "snmp_tcp_estab_resets" }, /* TCP-MIB::tcpEstabResets.0 */
		{ .prefix = "1.3.6.1.2.1.6.9.0", .name = "snmp_tcp_curr_estab" }, /* TCP-MIB::tcpCurrEstab.0 */
		{ .prefix = "1.3.6.1.2.1.7.1.0", .name = "snmp_udp_in_datagrams" }, /* UDP-MIB::udpInDatagrams.0 */
		{ .prefix = "1.3.6.1.2.1.7.2.0", .name = "snmp_udp_no_ports" }, /* UDP-MIB::udpNoPorts.0 */
		{ .prefix = "1.3.6.1.2.1.7.3.0", .name = "snmp_udp_in_errors" }, /* UDP-MIB::udpInErrors.0 */
		{ .prefix = "1.3.6.1.2.1.7.4.0", .name = "snmp_udp_out_datagrams" }, /* UDP-MIB::udpOutDatagrams.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.1.1.0", .name = "snmp_disman_mte_resource_sample_minimum" }, /* DISMAN-EVENT-MIB::mteResourceSampleMinimum.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.1.2.0", .name = "snmp_disman_mte_resource_sample_instance_maximum" }, /* DISMAN-EVENT-MIB::mteResourceSampleInstanceMaximum.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.1.3.0", .name = "snmp_disman_mte_resource_sample_instances" }, /* DISMAN-EVENT-MIB::mteResourceSampleInstances.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.1.4.0", .name = "snmp_disman_mte_resource_sample_instances_high" }, /* DISMAN-EVENT-MIB::mteResourceSampleInstancesHigh.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.1.5.0", .name = "snmp_disman_mte_resource_sample_instance_lacks" }, /* DISMAN-EVENT-MIB::mteResourceSampleInstanceLacks.0 */
		{ .prefix = "1.3.6.1.2.1.88.1.2.1.0", .name = "snmp_disman_mte_trigger_failures" }, /* DISMAN-EVENT-MIB::mteTriggerFailures.0 */
		{ .prefix = "1.3.6.1.2.1.92.1.1.1.0", .name = "snmp_nlm_nlm_config_global_entry_limit" }, /* NOTIFICATION-LOG-MIB::nlmConfigGlobalEntryLimit.0 */
		{ .prefix = "1.3.6.1.2.1.92.1.1.2.0", .name = "snmp_nlm_nlm_config_global_age_out" }, /* NOTIFICATION-LOG-MIB::nlmConfigGlobalAgeOut.0 */
		{ .prefix = "1.3.6.1.2.1.92.1.2.1.0", .name = "snmp_nlm_nlm_stats_global_notifications_logged" }, /* NOTIFICATION-LOG-MIB::nlmStatsGlobalNotificationsLogged.0 */
		{ .prefix = "1.3.6.1.2.1.92.1.2.2.0", .name = "snmp_nlm_nlm_stats_global_notifications_bumped" }, /* NOTIFICATION-LOG-MIB::nlmStatsGlobalNotificationsBumped.0 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.1.1", .name = "snmp_ucd_la_index" }, /* UCD-SNMP-MIB::laIndex.1 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.1.2", .name = "snmp_ucd_la_index" }, /* UCD-SNMP-MIB::laIndex.2 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.1.3", .name = "snmp_ucd_la_index" }, /* UCD-SNMP-MIB::laIndex.3 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.100.1", .name = "snmp_ucd_la_error_flag" }, /* UCD-SNMP-MIB::laErrorFlag.1 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.100.2", .name = "snmp_ucd_la_error_flag" }, /* UCD-SNMP-MIB::laErrorFlag.2 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.100.3", .name = "snmp_ucd_la_error_flag" }, /* UCD-SNMP-MIB::laErrorFlag.3 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.5.1", .name = "snmp_ucd_la_load_int" }, /* UCD-SNMP-MIB::laLoadInt.1 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.5.2", .name = "snmp_ucd_la_load_int" }, /* UCD-SNMP-MIB::laLoadInt.2 */
		{ .prefix = "1.3.6.1.4.1.2021.10.1.5.3", .name = "snmp_ucd_la_load_int" }, /* UCD-SNMP-MIB::laLoadInt.3 */
		{ .prefix = "1.3.6.1.4.1.2021.100.1.0", .name = "snmp_ucd_version_index" }, /* UCD-SNMP-MIB::versionIndex.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.10.0", .name = "snmp_ucd_version_clear_cache" }, /* UCD-SNMP-MIB::versionClearCache.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.11.0", .name = "snmp_ucd_version_update_config" }, /* UCD-SNMP-MIB::versionUpdateConfig.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.12.0", .name = "snmp_ucd_version_restart_agent" }, /* UCD-SNMP-MIB::versionRestartAgent.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.13.0", .name = "snmp_ucd_version_save_persistent_data" }, /* UCD-SNMP-MIB::versionSavePersistentData.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.20.0", .name = "snmp_ucd_version_do_debugging" }, /* UCD-SNMP-MIB::versionDoDebugging.0 */
		{ .prefix = "1.3.6.1.4.1.2021.100.6.0", .name = "snmp_ucd_version_configure_options" }, /* UCD-SNMP-MIB::versionConfigureOptions.0 */
		{ .prefix = "1.3.6.1.4.1.2021.101.1.0", .name = "snmp_ucd_snmperr_index" }, /* UCD-SNMP-MIB::snmperrIndex.0 */
		{ .prefix = "1.3.6.1.4.1.2021.101.2.0", .name = "snmp_ucd_snmperr_names" }, /* UCD-SNMP-MIB::snmperrNames.0 */
		{ .prefix = "1.3.6.1.4.1.2021.101.100.0", .name = "snmp_ucd_snmperr_error_flag" }, /* UCD-SNMP-MIB::snmperrErrorFlag.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.1.0", .name = "snmp_ucd_ss_index" }, /* UCD-SNMP-MIB::ssIndex.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.2.0", .name = "snmp_ucd_ss_error_name" }, /* UCD-SNMP-MIB::ssErrorName.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.10.0", .name = "snmp_ucd_ss_cpu_system" }, /* UCD-SNMP-MIB::ssCpuSystem.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.11.0", .name = "snmp_ucd_ss_cpu_idle" }, /* UCD-SNMP-MIB::ssCpuIdle.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.3.0", .name = "snmp_ucd_ss_swap_in" }, /* UCD-SNMP-MIB::ssSwapIn.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.4.0", .name = "snmp_ucd_ss_swap_out" }, /* UCD-SNMP-MIB::ssSwapOut.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.5.0", .name = "snmp_ucd_ss_io_sent" }, /* UCD-SNMP-MIB::ssIOSent.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.50.0", .name = "snmp_ucd_ss_cpu_raw_user" }, /* UCD-SNMP-MIB::ssCpuRawUser.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.51.0", .name = "snmp_ucd_ss_cpu_raw_nice" }, /* UCD-SNMP-MIB::ssCpuRawNice.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.52.0", .name = "snmp_ucd_ss_cpu_raw_system" }, /* UCD-SNMP-MIB::ssCpuRawSystem.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.53.0", .name = "snmp_ucd_ss_cpu_raw_idle" }, /* UCD-SNMP-MIB::ssCpuRawIdle.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.54.0", .name = "snmp_ucd_ss_cpu_raw_wait" }, /* UCD-SNMP-MIB::ssCpuRawWait.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.55.0", .name = "snmp_ucd_ss_cpu_raw_kernel" }, /* UCD-SNMP-MIB::ssCpuRawKernel.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.56.0", .name = "snmp_ucd_ss_cpu_raw_interrupt" }, /* UCD-SNMP-MIB::ssCpuRawInterrupt.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.57.0", .name = "snmp_ucd_ss_io_raw_sent" }, /* UCD-SNMP-MIB::ssIORawSent.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.58.0", .name = "snmp_ucd_ss_io_raw_received" }, /* UCD-SNMP-MIB::ssIORawReceived.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.59.0", .name = "snmp_ucd_ss_raw_interrupts" }, /* UCD-SNMP-MIB::ssRawInterrupts.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.6.0", .name = "snmp_ucd_ss_io_receive" }, /* UCD-SNMP-MIB::ssIOReceive.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.60.0", .name = "snmp_ucd_ss_raw_contexts" }, /* UCD-SNMP-MIB::ssRawContexts.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.61.0", .name = "snmp_ucd_ss_cpu_raw_soft_irq" }, /* UCD-SNMP-MIB::ssCpuRawSoftIRQ.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.62.0", .name = "snmp_ucd_ss_raw_swap_in" }, /* UCD-SNMP-MIB::ssRawSwapIn.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.63.0", .name = "snmp_ucd_ss_raw_swap_out" }, /* UCD-SNMP-MIB::ssRawSwapOut.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.64.0", .name = "snmp_ucd_system_stats_64" }, /* UCD-SNMP-MIB::systemStats.64.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.65.0", .name = "snmp_ucd_system_stats_65" }, /* UCD-SNMP-MIB::systemStats.65.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.66.0", .name = "snmp_ucd_system_stats_66" }, /* UCD-SNMP-MIB::systemStats.66.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.7.0", .name = "snmp_ucd_ss_sys_interrupts" }, /* UCD-SNMP-MIB::ssSysInterrupts.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.8.0", .name = "snmp_ucd_ss_sys_context" }, /* UCD-SNMP-MIB::ssSysContext.0 */
		{ .prefix = "1.3.6.1.4.1.2021.11.9.0", .name = "snmp_ucd_ss_cpu_user" }, /* UCD-SNMP-MIB::ssCpuUser.0 */
		{ .prefix = "1.3.6.1.4.1.2021.13.14.1.0", .name = "snmp_ucd_dlmod_next_index" }, /* UCD-DLMOD-MIB::dlmodNextIndex.0 */
		{ .prefix = "1.3.6.1.4.1.2021.16.1.0", .name = "snmp_ucd_log_match_max_entries" }, /* UCD-SNMP-MIB::logMatchMaxEntries.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.1.0", .name = "snmp_ucd_mem_index" }, /* UCD-SNMP-MIB::memIndex.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.2.0", .name = "snmp_ucd_mem_error_name" }, /* UCD-SNMP-MIB::memErrorName.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.100.0", .name = "snmp_ucd_mem_swap_error" }, /* UCD-SNMP-MIB::memSwapError.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.11.0", .name = "snmp_ucd_mem_total_free" }, /* UCD-SNMP-MIB::memTotalFree.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.12.0", .name = "snmp_ucd_mem_minimum_swap" }, /* UCD-SNMP-MIB::memMinimumSwap.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.13.0", .name = "snmp_ucd_mem_shared" }, /* UCD-SNMP-MIB::memShared.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.14.0", .name = "snmp_ucd_mem_buffer" }, /* UCD-SNMP-MIB::memBuffer.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.15.0", .name = "snmp_ucd_mem_cached" }, /* UCD-SNMP-MIB::memCached.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.18.0", .name = "snmp_ucd_memory_18" }, /* UCD-SNMP-MIB::memory.18.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.19.0", .name = "snmp_ucd_memory_19" }, /* UCD-SNMP-MIB::memory.19.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.20.0", .name = "snmp_ucd_memory_20" }, /* UCD-SNMP-MIB::memory.20.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.21.0", .name = "snmp_ucd_memory_21" }, /* UCD-SNMP-MIB::memory.21.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.22.0", .name = "snmp_ucd_memory_22" }, /* UCD-SNMP-MIB::memory.22.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.23.0", .name = "snmp_ucd_memory_23" }, /* UCD-SNMP-MIB::memory.23.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.24.0", .name = "snmp_ucd_memory_24" }, /* UCD-SNMP-MIB::memory.24.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.25.0", .name = "snmp_ucd_memory_25" }, /* UCD-SNMP-MIB::memory.25.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.26.0", .name = "snmp_ucd_memory_26" }, /* UCD-SNMP-MIB::memory.26.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.3.0", .name = "snmp_ucd_mem_total_swap" }, /* UCD-SNMP-MIB::memTotalSwap.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.4.0", .name = "snmp_ucd_mem_avail_swap" }, /* UCD-SNMP-MIB::memAvailSwap.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.5.0", .name = "snmp_ucd_mem_total_real" }, /* UCD-SNMP-MIB::memTotalReal.0 */
		{ .prefix = "1.3.6.1.4.1.2021.4.6.0", .name = "snmp_ucd_mem_avail_real" }, /* UCD-SNMP-MIB::memAvailReal.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.3.2.1.0", .name = "snmp_net_snmp_ns_extend_num_entries" }, /* NET-SNMP-EXTEND-MIB::nsExtendNumEntries.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.5.1.0", .name = "snmp_net_snmp_ns_cache_default_timeout" }, /* NET-SNMP-AGENT-MIB::nsCacheDefaultTimeout.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.5.2.0", .name = "snmp_net_snmp_ns_cache_enabled" }, /* NET-SNMP-AGENT-MIB::nsCacheEnabled.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.7.1.1.0", .name = "snmp_net_snmp_ns_debug_enabled" }, /* NET-SNMP-AGENT-MIB::nsDebugEnabled.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.7.1.2.0", .name = "snmp_net_snmp_ns_debug_output_all" }, /* NET-SNMP-AGENT-MIB::nsDebugOutputAll.0 */
		{ .prefix = "1.3.6.1.4.1.8072.1.7.1.3.0", .name = "snmp_net_snmp_ns_debug_dump_pdu" }, /* NET-SNMP-AGENT-MIB::nsDebugDumpPdu.0 */
		{ .prefix = "1.3.6.1.6.3.1.1.6.1.0", .name = "snmp_mib2_set_serial_no" }, /* SNMPv2-MIB::snmpSetSerialNo.0 */
		{ .prefix = "1.3.6.1.6.3.10.2.1.2.0", .name = "snmp_framework_engine_boots" }, /* SNMP-FRAMEWORK-MIB::snmpEngineBoots.0 */
		{ .prefix = "1.3.6.1.6.3.10.2.1.3.0", .name = "snmp_framework_engine_time" }, /* SNMP-FRAMEWORK-MIB::snmpEngineTime.0 */
		{ .prefix = "1.3.6.1.6.3.10.2.1.4.0", .name = "snmp_framework_engine_max_message_size" }, /* SNMP-FRAMEWORK-MIB::snmpEngineMaxMessageSize.0 */
		{ .prefix = "1.3.6.1.6.3.11.2.1.1.0", .name = "snmp_mpd_unknown_security_models" }, /* SNMP-MPD-MIB::snmpUnknownSecurityModels.0 */
		{ .prefix = "1.3.6.1.6.3.11.2.1.2.0", .name = "snmp_mpd_invalid_msgs" }, /* SNMP-MPD-MIB::snmpInvalidMsgs.0 */
		{ .prefix = "1.3.6.1.6.3.11.2.1.3.0", .name = "snmp_mpd_unknown_pdu_handlers" }, /* SNMP-MPD-MIB::snmpUnknownPDUHandlers.0 */
		{ .prefix = "1.3.6.1.6.3.12.1.1.0", .name = "snmp_target_spin_lock" }, /* SNMP-TARGET-MIB::snmpTargetSpinLock.0 */
		{ .prefix = "1.3.6.1.6.3.12.1.4.0", .name = "snmp_target_unavailable_contexts" }, /* SNMP-TARGET-MIB::snmpUnavailableContexts.0 */
		{ .prefix = "1.3.6.1.6.3.12.1.5.0", .name = "snmp_target_unknown_contexts" }, /* SNMP-TARGET-MIB::snmpUnknownContexts.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.1.0", .name = "snmp_usm_stats_unsupported_sec_levels" }, /* SNMP-USER-BASED-SM-MIB::usmStatsUnsupportedSecLevels.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.2.0", .name = "snmp_usm_stats_not_in_time_windows" }, /* SNMP-USER-BASED-SM-MIB::usmStatsNotInTimeWindows.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.3.0", .name = "snmp_usm_stats_unknown_user_names" }, /* SNMP-USER-BASED-SM-MIB::usmStatsUnknownUserNames.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.4.0", .name = "snmp_usm_stats_unknown_engine_i_ds" }, /* SNMP-USER-BASED-SM-MIB::usmStatsUnknownEngineIDs.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.5.0", .name = "snmp_usm_stats_wrong_digests" }, /* SNMP-USER-BASED-SM-MIB::usmStatsWrongDigests.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.1.6.0", .name = "snmp_usm_stats_decryption_errors" }, /* SNMP-USER-BASED-SM-MIB::usmStatsDecryptionErrors.0 */
		{ .prefix = "1.3.6.1.6.3.15.1.2.1.0", .name = "snmp_usm_user_spin_lock" }, /* SNMP-USER-BASED-SM-MIB::usmUserSpinLock.0 */
		{ .prefix = "1.3.6.1.6.3.16.1.5.1.0", .name = "snmp_vacm_view_spin_lock" }, /* SNMP-VIEW-BASED-ACM-MIB::vacmViewSpinLock.0 */
		{ .prefix = "1.3.6.1.6.3.10.2.1.1.0", .name = "snmp_engine_id_info" },
	};

	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
		alligator_ht_insert(g_snmp_oid_name_ht, &tab[i].node, &tab[i],
				    tommy_strhash_u32(0, tab[i].prefix));
}

static const char *snmp_oid_name_lookup(const char *oid_dotted)
{
	if (!oid_dotted || !oid_dotted[0])
		return NULL;
	snmp_oid_name_map_init();
	if (!g_snmp_oid_name_ht)
		return NULL;

	char key[SNMP_OID_LABEL];
	strlcpy(key, oid_dotted, sizeof key);
	for (;;) {
		snmp_oid_name_ent *e = alligator_ht_search(g_snmp_oid_name_ht, snmp_oid_name_compare, key,
							   tommy_strhash_u32(0, key));
		if (e)
			return e->name;
		char *dot = strrchr(key, '.');
		if (!dot)
			break;
		*dot = 0;
		if (!key[0])
			break;
	}
	return NULL;
}

static const char *snmp_tcp_metric_name_by_oid(const char *oid_dotted)
{
	const char *n = snmp_oid_name_lookup(oid_dotted);
	if (!n)
		return NULL;
	if (!strncmp(n, "snmp_tcp_", 9))
		return n;
	return NULL;
}

static const char *snmp_friendly_metric_name_by_oid(const char *oid_dotted)
{
	return snmp_oid_name_lookup(oid_dotted);
}

static int snmp_hr_table_index_from_oid(const char *oid_dotted, char *out, size_t outcap)
{
	if (!oid_dotted || !out || !outcap)
		return 0;
	if (strncmp(oid_dotted, "1.3.6.1.2.1.25.", 15) != 0)
		return 0;
	const char *dot = strrchr(oid_dotted, '.');
	if (!dot || !dot[1])
		return 0;
	for (const char *p = dot + 1; *p; p++)
		if (*p < '0' || *p > '9')
			return 0;
	strlcpy(out, dot + 1, outcap);
	return out[0] != 0;
}

static int snmp_special_index_label_from_oid(const char *oid_dotted, char *out, size_t outcap)
{
	uint32_t p[64];
	size_t n = 0;
	static const char *notif_pref4 = "1.3.6.1.4.1.8072.1.2.1.1.4.";
	static const char *notif_pref5 = "1.3.6.1.4.1.8072.1.2.1.1.5.";
	static const char *notif_pref6 = "1.3.6.1.4.1.8072.1.2.1.1.6.";
	if (!oid_dotted || !out || !outcap)
		return 0;

	/* Keep a stable join key for net-snmp notification triplet (.4/.5/.6). */
	if (!strncmp(oid_dotted, notif_pref4, strlen(notif_pref4))) {
		strlcpy(out, oid_dotted + strlen(notif_pref4), outcap);
		return out[0] != 0;
	}
	if (!strncmp(oid_dotted, notif_pref5, strlen(notif_pref5))) {
		strlcpy(out, oid_dotted + strlen(notif_pref5), outcap);
		return out[0] != 0;
	}
	if (!strncmp(oid_dotted, notif_pref6, strlen(notif_pref6))) {
		strlcpy(out, oid_dotted + strlen(notif_pref6), outcap);
		return out[0] != 0;
	}

	if (snmp_oid_to_parts(oid_dotted, p, sizeof p / sizeof p[0], &n))
		return 0;

	if (!strncmp(oid_dotted, "1.3.6.1.2.1.88.1.4.2.1.3.", 25)) {
		const size_t base = 12;
		if (n > base) {
			size_t ln = p[base];
			if (ln > 0 && n >= base + 1 + ln && ln < 256) {
				uint8_t tmp[256];
				for (size_t i = 0; i < ln; i++)
					tmp[i] = (uint8_t)(p[base + 1 + i] & 0xff);
				snmp_octets_to_label(tmp, ln, out, outcap);
				return out[0] != 0;
			}
		}
	}

	if (!strncmp(oid_dotted, "1.3.6.1.4.1.8072.1.2.1.1.5.", 27)) {
		const size_t base = 12;
		if (n > base + 1 && p[base] == 0) {
			size_t ln = p[base + 1];
			if (ln > 0 && n >= base + 2 + ln) {
				size_t w = 0;
				out[0] = 0;
				for (size_t i = 0; i < ln; i++) {
					w += (size_t)snprintf(out + w, outcap > w ? outcap - w : 0, "%s%u",
							      i ? "." : "", (unsigned)p[base + 2 + i]);
					if (w >= outcap)
						break;
				}
				return out[0] != 0;
			}
		}
	}

	if (!strncmp(oid_dotted, "1.3.6.1.4.1.8072.1.2.1.1.6.", 27)) {
		const size_t base = 12;
		if (n > base + 1 && p[base] == 0) {
			size_t ln = p[base + 1];
			if (ln > 0 && n >= base + 2 + ln) {
				size_t w = 0;
				out[0] = 0;
				for (size_t i = 0; i < ln; i++) {
					w += (size_t)snprintf(out + w, outcap > w ? outcap - w : 0, "%s%u",
							      i ? "." : "", (unsigned)p[base + 2 + i]);
					if (w >= outcap)
						break;
				}
				return out[0] != 0;
			}
		}
	}
	return 0;
}

static int snmp_last_numeric_index_from_oid(const char *oid_dotted, char *out, size_t outcap)
{
	if (!oid_dotted || !out || !outcap)
		return 0;
	const char *dot = strrchr(oid_dotted, '.');
	if (!dot || !dot[1])
		return 0;
	for (const char *p = dot + 1; *p; p++)
		if (*p < '0' || *p > '9')
			return 0;
	strlcpy(out, dot + 1, outcap);
	return out[0] != 0;
}

static int snmp_decode_at_phys_ipv4(const char *oid_dotted, char *if_index, size_t if_cap, char *ip, size_t ip_cap)
{
	/* atPhysAddress: 1.3.6.1.2.1.3.1.1.2.<ifIndex>.<addrType=1>.<a>.<b>.<c>.<d> */
	static const uint32_t pre[] = { 1,3,6,1,2,1,3,1,1,2 };
	uint32_t parts[32];
	size_t n = 0;
	if (snmp_oid_to_parts(oid_dotted, parts, sizeof parts / sizeof parts[0], &n))
		return 0;
	if (n < (sizeof pre / sizeof pre[0]) + 6)
		return 0;
	for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++)
		if (parts[i] != pre[i])
			return 0;
	size_t off = sizeof pre / sizeof pre[0];
	if (parts[off + 1] != 1) /* network address type: ip(1) */
		return 0;
	snprintf(if_index, if_cap, "%u", (unsigned)parts[off + 0]);
	snprintf(ip, ip_cap, "%u.%u.%u.%u",
		 (unsigned)parts[off + 2], (unsigned)parts[off + 3],
		 (unsigned)parts[off + 4], (unsigned)parts[off + 5]);
	return 1;
}

static const char *snmp_tcp_state_name(uint64_t v)
{
	switch (v) {
	case 1: return "closed";
	case 2: return "listen";
	case 3: return "synSent";
	case 4: return "synReceived";
	case 5: return "established";
	case 6: return "finWait1";
	case 7: return "finWait2";
	case 8: return "closeWait";
	case 9: return "lastAck";
	case 10: return "closing";
	case 11: return "timeWait";
	case 12: return "deleteTCB";
	default: return NULL;
	}
}

typedef struct snmp_tcp_decoded {
	int ok;
	char local_addr[32];
	char rem_addr[32];
	char local_port[16];
	char rem_port[16];
} snmp_tcp_decoded;

static int snmp_decode_tcpconn_ipv4(const char *oid_dotted, snmp_tcp_decoded *d)
{
	/* tcpConnState/local/rem: 1.3.6.1.2.1.6.13.1.{1..5}.<la4>.<lp>.<ra4>.<rp> */
	static const uint32_t pre[] = { 1,3,6,1,2,1,6,13,1 };
	uint32_t parts[64];
	size_t n = 0;
	if (snmp_oid_to_parts(oid_dotted, parts, sizeof parts / sizeof parts[0], &n))
		return 0;
	if (n < (sizeof pre / sizeof pre[0]) + 1 + 10)
		return 0;
	for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++)
		if (parts[i] != pre[i])
			return 0;
	uint32_t col = parts[sizeof pre / sizeof pre[0]];
	if (col < 1 || col > 5)
		return 0;
	size_t off = (sizeof pre / sizeof pre[0]) + 1;
	snprintf(d->local_addr, sizeof d->local_addr, "%u.%u.%u.%u", parts[off+0], parts[off+1], parts[off+2], parts[off+3]);
	snprintf(d->local_port, sizeof d->local_port, "%u", parts[off+4]);
	snprintf(d->rem_addr, sizeof d->rem_addr, "%u.%u.%u.%u", parts[off+5], parts[off+6], parts[off+7], parts[off+8]);
	snprintf(d->rem_port, sizeof d->rem_port, "%u", parts[off+9]);
	d->ok = 1;
	return 1;
}

static int snmp_decode_tcplistener_ipv4(const char *oid_dotted, snmp_tcp_decoded *d)
{
	/* tcpListenerProcess:
	 * 1.3.6.1.2.1.6.20.1.4.1.4.<la4>.<lp>
	 */
	static const uint32_t pre[] = { 1,3,6,1,2,1,6,20,1,4,1,4 };
	uint32_t parts[64];
	size_t n = 0;
	if (snmp_oid_to_parts(oid_dotted, parts, sizeof parts / sizeof parts[0], &n))
		return 0;
	if (n < (sizeof pre / sizeof pre[0]) + 5)
		return 0;
	for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++)
		if (parts[i] != pre[i])
			return 0;
	size_t off = sizeof pre / sizeof pre[0];
	snprintf(d->local_addr, sizeof d->local_addr, "%u.%u.%u.%u", parts[off+0], parts[off+1], parts[off+2], parts[off+3]);
	snprintf(d->local_port, sizeof d->local_port, "%u", parts[off+4]);
	d->rem_addr[0] = 0;
	d->rem_port[0] = 0;
	d->ok = 1;
	return 1;
}

static int snmp_decode_tcpconnection_ipv4(const char *oid_dotted, snmp_tcp_decoded *d)
{
	/* tcpConnectionState / tcpConnectionProcess (common net-snmp indexing for tcpConnectionTable):
	 * 1.3.6.1.2.1.6.19.1.{7|8}.1.4.<la4>.<lp>.1.4.<ra4>.<rp>
	 * (i.e. addressType=1 (ipv4), addressLength=4, then 4 bytes of address, then port; repeated for remote)
	 */
	static const uint32_t pre7[] = { 1,3,6,1,2,1,6,19,1,7,1,4 };
	static const uint32_t pre8[] = { 1,3,6,1,2,1,6,19,1,8,1,4 };
	uint32_t parts[64];
	size_t n = 0;
	if (snmp_oid_to_parts(oid_dotted, parts, sizeof parts / sizeof parts[0], &n))
		return 0;
	/* prefix(12) + local(4+1) + remote marker(2) + remote(4+1) = 12+5+2+5 = 24 */
	if (n < 24)
		return 0;
	int which = 0;
	for (size_t i = 0; i < sizeof pre7 / sizeof pre7[0]; i++)
		if (parts[i] != pre7[i]) { which = 1; break; }
	if (which == 0) {
		/* ok */
	} else {
		for (size_t i = 0; i < sizeof pre8 / sizeof pre8[0]; i++)
			if (parts[i] != pre8[i])
				return 0;
	}
	size_t off = 12;
	snprintf(d->local_addr, sizeof d->local_addr, "%u.%u.%u.%u", parts[off+0], parts[off+1], parts[off+2], parts[off+3]);
	snprintf(d->local_port, sizeof d->local_port, "%u", parts[off+4]);
	if (parts[off+5] != 1 || parts[off+6] != 4)
		return 0;
	snprintf(d->rem_addr, sizeof d->rem_addr, "%u.%u.%u.%u", parts[off+7], parts[off+8], parts[off+9], parts[off+10]);
	snprintf(d->rem_port, sizeof d->rem_port, "%u", parts[off+11]);
	d->ok = 1;
	return 1;
}

/* Longest-prefix: loaded MIB dirs (snmp_mib_dirs), then built-in table. */
static void snmp_wellknown_symbol(context_arg *carg, const char *dotted, char *sym, size_t symcap)
{
	sym[0] = 0;
	if (!dotted || !dotted[0])
		return;
	int dbg = snmp_env_debug_level(carg);
	int mib_tried = 0;
	if (snmp_env_oid_symbols_enabled(carg)) {
		const char *md = snmp_env_lookup(carg, "snmp_mib_dirs");
		if (md && md[0]) {
			int reloaded = snmp_mib_ensure_loaded(md);
			if (dbg >= 1 && reloaded)
				carglog(carg, L_DEBUG, "snmp mib: loaded dirs \"%s\" MODULE-IDENTITY oid entries %zu\n", md,
					 snmp_mib_oid_map_size());
			mib_tried = 1;
			snmp_mib_lookup_symbol(dotted, sym, symcap);
			if (sym[0]) {
				if (dbg >= 2)
					carglog(carg, L_DEBUG, "snmp symbol: oid=%s source=mib sym=%s\n", dotted, sym);
				return;
			}
		}
	}
	/*
	 * Fallback when snmp_mib_dirs unset or lookup misses: snmpModules / mib-2 roots only
	 * (longest-prefix must match sub-OIDs like sysORID values).
	 */
	static const char *const tab[][2] = {
		{ "1.3.6.1.6.3.18", "snmpCommunityMIB" },
		{ "1.3.6.1.6.3.16", "snmpVacmMIB" },
		{ "1.3.6.1.6.3.15", "snmpUsmMIB" },
		{ "1.3.6.1.6.3.13", "snmpNotificationMIB" },
		{ "1.3.6.1.6.3.12", "snmpTargetMIB" },
		{ "1.3.6.1.6.3.11", "snmpMPDMIB" },
		{ "1.3.6.1.6.3.10", "snmpFrameworkMIB" },
		{ "1.3.6.1.6.3.1", "snmpMIB" },
		{ "1.3.6.1.4.1.8072.3.2.10", "netSnmp" },
		{ "1.3.6.1.2.1.92", "notificationLogMIB" },
		{ "1.3.6.1.2.1.50", "udpMIB" },
		{ "1.3.6.1.2.1.49", "tcpMIB" },
		{ "1.3.6.1.2.1.7", "udpMIB" },
		{ "1.3.6.1.2.1.6", "tcpMIB" },
		{ "1.3.6.1.2.1.4", "ipMIB" },
		{ NULL, NULL },
	};
	size_t best = 0;
	for (size_t i = 0; tab[i][0]; i++) {
		const char *p = tab[i][0];
		size_t pl = strlen(p);
		if (strncmp(dotted, p, pl) != 0)
			continue;
		if (dotted[pl] != '\0' && dotted[pl] != '.')
			continue;
		if (pl >= best) {
			best = pl;
			strlcpy(sym, tab[i][1], symcap);
		}
	}
	if (sym[0]) {
		if (dbg >= 2)
			carglog(carg, L_DEBUG, "snmp symbol: oid=%s source=builtin sym=%s\n", dotted, sym);
		else if (dbg >= 1 && mib_tried)
			carglog(carg, L_DEBUG, "snmp symbol: oid=%s mib_miss source=builtin sym=%s\n", dotted, sym);
	} else if (dbg >= 1 && mib_tried) {
		carglog(carg, L_DEBUG, "snmp symbol: oid=%s mib_miss no_builtin\n", dotted);
	}
}

/* snmp_<dotted_oid_with_underscores>[_suffix], then prometheus_metric_name_normalizer */
static void snmp_metric_name_from_oid(char *out, size_t outs, const char *oid, const char *suffix)
{
	if (outs < 8) {
		if (outs)
			out[0] = 0;
		return;
	}
	size_t w = strlcpy(out, "snmp_", outs);
	for (size_t i = 0; oid[i] && w + 3 < outs; i++) {
		unsigned char c = (unsigned char)oid[i];
		if (c == '.')
			out[w++] = '_';
		else if (isalnum(c) || c == '_')
			out[w++] = (char)c;
		else
			out[w++] = '_';
	}
	if (suffix && suffix[0] && w + 2 < outs) {
		out[w++] = '_';
		w += strlcpy(out + w, suffix, outs - w);
	}
	out[w >= outs ? outs - 1 : w] = 0;
	prometheus_metric_name_normalizer(out, strlen(out));
}

static int ber_write_len(uint8_t *out, size_t L)
{
	if (L < 128) {
		out[0] = (uint8_t)L;
		return 1;
	}
	if (L < 256) {
		out[0] = 0x81;
		out[1] = (uint8_t)L;
		return 2;
	}
	out[0] = 0x82;
	out[1] = (uint8_t)((L >> 8) & 0xff);
	out[2] = (uint8_t)(L & 0xff);
	return 3;
}

static size_t ber_tlv(uint8_t *out, uint8_t tag, const uint8_t *val, size_t vlen)
{
	size_t n = 0;
	out[n++] = tag;
	n += ber_write_len(out + n, vlen);
	if (vlen && val)
		memcpy(out + n, val, vlen);
	return n + vlen;
}

/* BER INTEGER is signed, big-endian (MSB first). Strip leading zero octets per X.690. */
static size_t ber_integer_u32(uint8_t *out, uint32_t v)
{
	uint8_t raw[5];
	raw[0] = (uint8_t)((v >> 24) & 0xff);
	raw[1] = (uint8_t)((v >> 16) & 0xff);
	raw[2] = (uint8_t)((v >> 8) & 0xff);
	raw[3] = (uint8_t)(v & 0xff);
	int start = 0;
	while (start < 3 && raw[start] == 0 && (raw[start + 1] & 0x80) == 0)
		start++;
	return ber_tlv(out, 0x02, raw + start, (size_t)(4 - start));
}

static int snmp_oid_dotted_to_ber(const char *dotted, uint8_t *out, size_t outcap, size_t *olen)
{
	if (!dotted || !dotted[0]) {
		(void)outcap;
		*olen = 0;
		return 0;
	}

	uint32_t parts[64];
	int n = 0;
	const char *p = dotted;

	while (*p && n < 64) {
		char *end = NULL;
		unsigned long x = strtoul(p, &end, 10);
		if (end == p)
			return -1;
		parts[n++] = (uint32_t)x;
		if (*end == '.')
			p = end + 1;
		else if (*end == '\0')
			break;
		else
			return -1;
	}
	if (n < 2)
		return -1;

	size_t i = 0;
	out[i++] = (uint8_t)(40u * parts[0] + parts[1]);
	for (int k = 2; k < n; k++) {
		uint32_t v = parts[k];
		uint8_t chunks[8];
		int nc = 0;
		do {
			if (nc >= (int)sizeof chunks)
				return -1;
			chunks[nc++] = (uint8_t)(v & 0x7f);
			v >>= 7;
		} while (v);
		for (int j = nc - 1; j >= 0; j--) {
			uint8_t b = chunks[j];
			if (j > 0)
				b |= 0x80;
			if (i >= outcap)
				return -1;
			out[i++] = b;
		}
	}
	*olen = i;
	return 0;
}

static uint32_t snmp_request_id(void)
{
	/* INTEGER32 range: keep MSB clear so BER is one positive 4-octet form, not negative. */
	uint32_t r = (uint32_t)time(NULL) ^ ((uint32_t)getpid() * 1103515245u);
	return r & 0x7fffffffu;
}

static int snmp_build_v2c_get(const char *community, const char *oid_dotted, int getnext,
			      uint8_t *pkt, size_t cap, size_t *pktlen)
{
	const char *comm = community && community[0] ? community : "public";
	size_t commlen = strlen(comm);
	if (commlen > 255)
		return -1;

	uint8_t oidber[128];
	size_t oidlen;

	if (snmp_oid_dotted_to_ber(oid_dotted ? oid_dotted : "", oidber, sizeof oidber, &oidlen) < 0)
		return -1;
	if (oidlen == 0 && !getnext)
		return -1;

	uint8_t vb_inner[160];
	size_t z = 0;
	z += ber_tlv(vb_inner + z, 0x06, oidber, oidlen);
	z += ber_tlv(vb_inner + z, 0x05, NULL, 0);

	uint8_t varbind[180];
	size_t v = 0;
	varbind[v++] = 0x30;
	v += ber_write_len(varbind + v, z);
	memcpy(varbind + v, vb_inner, z);
	v += z;

	uint8_t vblist[200];
	size_t w = 0;
	vblist[w++] = 0x30;
	w += ber_write_len(vblist + w, v);
	memcpy(vblist + w, varbind, v);
	w += v;

	uint8_t pdu_body[280];
	size_t p = 0;
	p += ber_integer_u32(pdu_body + p, snmp_request_id());
	p += ber_integer_u32(pdu_body + p, 0);
	p += ber_integer_u32(pdu_body + p, 0);
	if (p + w > sizeof pdu_body)
		return -1;
	memcpy(pdu_body + p, vblist, w);
	p += w;

	uint8_t pdu_tag = getnext ? SNMP_PDU_GETNEXT_REQUEST : SNMP_PDU_GET_REQUEST;
	uint8_t pdu_wrap[320];
	size_t pw = 0;
	pdu_wrap[pw++] = pdu_tag;
	pw += ber_write_len(pdu_wrap + pw, p);
	memcpy(pdu_wrap + pw, pdu_body, p);
	pw += p;

	uint8_t outer[400];
	size_t o = 0;
	o += ber_integer_u32(outer + o, 1);
	o += ber_tlv(outer + o, 0x04, (const uint8_t *)comm, commlen);
	if (o + pw > sizeof outer)
		return -1;
	memcpy(outer + o, pdu_wrap, pw);
	o += pw;

	if (1 + ber_write_len(pkt + 1, o) + o > cap)
		return -1;
	size_t f = 0;
	pkt[f++] = 0x30;
	f += ber_write_len(pkt + f, o);
	memcpy(pkt + f, outer, o);
	f += o;
	*pktlen = f;
	return 0;
}

static int ber_read_len(const uint8_t *b, size_t n, size_t *i, size_t *L)
{
	if (*i >= n)
		return -1;
	uint8_t l = b[(*i)++];
	if (l < 128) {
		*L = l;
		return 0;
	}
	int nl = l & 0x7f;
	if (nl == 0 || nl > 4 || *i + (size_t)nl > n)
		return -1;
	size_t v = 0;
	while (nl--) {
		v = (v << 8) | b[(*i)++];
	}
	*L = v;
	return 0;
}

static int ber_next_tlv(const uint8_t *b, size_t n, size_t *i, uint8_t *tag, const uint8_t **c, size_t *clen)
{
	if (*i >= n)
		return -1;
	*tag = b[(*i)++];
	if (ber_read_len(b, n, i, clen))
		return -1;
	if (*i + *clen > n)
		return -1;
	*c = b + *i;
	*i += *clen;
	return 0;
}

static void snmp_oid_ber_to_dotted(const uint8_t *o, size_t ol, char *out, size_t outs)
{
	if (!ol || !outs) {
		if (outs)
			out[0] = 0;
		return;
	}
	uint8_t first = o[0];
	uint32_t a = first / 40u;
	uint32_t b = first % 40u;
	size_t w = (size_t)snprintf(out, outs, "%u.%u", a, b);
	size_t i = 1;
	while (i < ol && w + 1 < outs) {
		uint32_t sub = 0;
		uint8_t by;
		do {
			if (i >= ol)
				return;
			by = o[i++];
			sub = (sub << 7) | (by & 0x7f);
		} while (by & 0x80);
		w += (size_t)snprintf(out + w, outs - w, ".%" PRIu32, sub);
	}
}

static int ber_read_integer(const uint8_t *p, size_t len, int64_t *out)
{
	if (!len)
		return -1;
	int64_t v = (int8_t)p[0];
	for (size_t i = 1; i < len; i++)
		v = (v << 8) | p[i];
	*out = v;
	return 0;
}

static int ber_read_uinteger(const uint8_t *p, size_t len, uint64_t *out)
{
	if (!len || len > 8)
		return -1;
	uint64_t v = 0;
	for (size_t i = 0; i < len; i++)
		v = (v << 8) | p[i];
	*out = v;
	return 0;
}

/* net-snmp Opaque Float/Double (BER APPLICATION 4) — common encodings seen with UCD laLoadFloat, etc. */
static int snmp_opaque_try_double(const uint8_t *p, size_t n, double *out)
{
	if (!p || !n || !out)
		return 0;
	/* Opaque Float: 9f 04 04 + 4-byte IEEE754 BE */
	for (size_t i = 0; i + 7 <= n; i++) {
		if (p[i] == 0x9f && p[i + 1] == 0x04 && p[i + 2] == 0x04) {
			uint32_t be = ((uint32_t)p[i + 3] << 24) | ((uint32_t)p[i + 4] << 16) |
				      ((uint32_t)p[i + 5] << 8) | (uint32_t)p[i + 6];
			union {
				float f;
				uint32_t u;
			} u;
			u.u = be;
			*out = (double)u.f;
			return 1;
		}
	}
	/* Opaque Double: 9f 78 08 + 8-byte IEEE754 BE */
	for (size_t i = 0; i + 11 <= n; i++) {
		if (p[i] == 0x9f && p[i + 1] == 0x78 && p[i + 2] == 0x08) {
			uint64_t be = 0;
			for (int j = 0; j < 8; j++)
				be = (be << 8) | (uint64_t)p[i + 3 + j];
			union {
				double d;
				uint64_t u;
			} u;
			u.u = be;
			*out = u.d;
			return 1;
		}
	}
	/* Raw 4-byte float BE (some agents) */
	if (n == 4) {
		uint32_t be = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
		union {
			float f;
			uint32_t u;
		} u;
		u.u = be;
		*out = (double)u.f;
		return 1;
	}
	return 0;
}

typedef struct snmp_vb {
	int parse_ok;
	int64_t errstat;
	char oidstr[SNMP_OID_LABEL];
	uint8_t valtag;
	const uint8_t *valbytes;
	size_t vallen;
	uint8_t end_of_mib;
} snmp_vb;

static int snmp_parse_first_varbind(const uint8_t *buf, size_t size, snmp_vb *vb)
{
	memset(vb, 0, sizeof(*vb));
	size_t pos = 0;
	uint8_t tag;
	const uint8_t *c;
	size_t clen;

	if (ber_next_tlv(buf, size, &pos, &tag, &c, &clen) || tag != 0x30)
		return -1;

	size_t in = 0;
	const uint8_t *inner = c;
	size_t inner_len = clen;

	if (ber_next_tlv(inner, inner_len, &in, &tag, &c, &clen) || tag != 0x02)
		return -1;
	if (ber_next_tlv(inner, inner_len, &in, &tag, &c, &clen) || tag != 0x04)
		return -1;

	if (ber_next_tlv(inner, inner_len, &in, &tag, &c, &clen))
		return -1;
	if (tag != SNMP_PDU_GET_RESPONSE)
		return -1;

	size_t pd = 0;
	const uint8_t *pdu = c;
	size_t pdu_len = clen;

	int64_t reqid = 0;
	if (ber_next_tlv(pdu, pdu_len, &pd, &tag, &c, &clen) || tag != 0x02)
		return -1;
	if (ber_read_integer(c, clen, &reqid))
		return -1;

	if (ber_next_tlv(pdu, pdu_len, &pd, &tag, &c, &clen) || tag != 0x02)
		return -1;
	if (ber_read_integer(c, clen, &vb->errstat))
		return -1;

	if (ber_next_tlv(pdu, pdu_len, &pd, &tag, &c, &clen) || tag != 0x02)
		return -1;
	int64_t erridx = 0;
	if (ber_read_integer(c, clen, &erridx))
		return -1;
	(void)reqid;
	(void)erridx;

	if (ber_next_tlv(pdu, pdu_len, &pd, &tag, &c, &clen) || tag != 0x30)
		return -1;

	size_t vbi = 0;
	const uint8_t *vbl = c;
	size_t vbl_len = clen;

	if (ber_next_tlv(vbl, vbl_len, &vbi, &tag, &c, &clen) || tag != 0x30)
		return -1;

	size_t one = 0;
	const uint8_t *pair = c;
	size_t pair_len = clen;

	const uint8_t *oidbytes = NULL;
	size_t oidlen = 0;

	if (ber_next_tlv(pair, pair_len, &one, &tag, &c, &clen) || tag != 0x06)
		return -1;
	oidbytes = c;
	oidlen = clen;
	if (ber_next_tlv(pair, pair_len, &one, &tag, &c, &clen))
		return -1;
	vb->valtag = tag;
	vb->valbytes = c;
	vb->vallen = clen;
	if (vb->valtag == 0x82)
		vb->end_of_mib = 1;

	snmp_oid_ber_to_dotted(oidbytes, oidlen, vb->oidstr, sizeof vb->oidstr);
	vb->parse_ok = 1;
	return 0;
}

/* Prometheus label values must be safe text; binary OCTET STRINGs are not valid UTF-8 / break quoting. */
static int snmp_octets_printable_label(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c = p[i];
		if (c < 0x20 || c > 0x7e || c == '"' || c == '\\')
			return 0;
	}
	return 1;
}

static void snmp_octets_to_label(const uint8_t *p, size_t n, char *out, size_t outcap)
{
	if (!outcap) {
		return;
	}
	if (n == 0) {
		out[0] = 0;
		return;
	}
	if (n + 1 < outcap && snmp_octets_printable_label(p, n)) {
		memcpy(out, p, n);
		out[n] = 0;
		return;
	}
	size_t w = strlcpy(out, "hex:", outcap);
	if (w >= outcap)
		return;
	size_t room = outcap - 1 > w ? outcap - 1 - w : 0;
	size_t maxb = room / 2;
	if (n > maxb)
		n = maxb;
	for (size_t i = 0; i < n && w + 3 <= outcap; i++)
		w += (size_t)snprintf(out + w, outcap - w, "%02x", (unsigned)p[i]);
}

static int snmp_date_and_time_try_label(const uint8_t *p, size_t n, char *out, size_t outcap)
{
	/* RFC 2579 DateAndTime: OCTET STRING (SIZE (8 | 11)) */
	if (!out || outcap < 2 || !p)
		return 0;
	if (n != 8 && n != 10 && n != 11)
		return 0;
	unsigned y = ((unsigned)p[0] << 8) | (unsigned)p[1];
	unsigned mo = p[2], d = p[3], h = p[4], mi = p[5], s = p[6], ds = p[7];
	if (y < 1970 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60 || ds > 9)
		return 0;
	if (n == 11 || n == 10) {
		char dir = (char)p[8];
		if (dir != '+' && dir != '-')
			return 0;
		unsigned uh = p[9];
		unsigned um = (n == 11) ? p[10] : 0;
		if (uh > 11 || um > 59)
			return 0;
		(void)snprintf(out, outcap, "%04u-%02u-%02uT%02u:%02u:%02u.%u%c%02u:%02u", y, mo, d, h, mi, s, ds, dir, uh,
				 um);
	} else {
		(void)snprintf(out, outcap, "%04u-%02u-%02uT%02u:%02u:%02u.%uZ", y, mo, d, h, mi, s, ds);
	}
	return 1;
}

/* When OCTET STRING is printable ASCII except " and \\, escape for Prometheus label value. */
static int snmp_octets_to_label_escaped(const uint8_t *p, size_t n, char *out, size_t outcap)
{
	if (!outcap || !out)
		return 0;
	size_t w = 0;
	out[0] = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = p[i];
		if (c == '\\') {
			if (w + 2 >= outcap)
				return 0;
			out[w++] = '\\';
			out[w++] = '\\';
		} else if (c == '"') {
			if (w + 2 >= outcap)
				return 0;
			out[w++] = '\\';
			out[w++] = '"';
		} else if (c >= 0x20 && c <= 0x7e) {
			if (w + 1 >= outcap)
				return 0;
			out[w++] = (char)c;
		} else if (c == '\n' || c == '\r' || c == '\t') {
			if (w + 1 >= outcap)
				return 0;
			out[w++] = ' ';
		} else
			return 0;
	}
	if (w >= outcap)
		return 0;
	out[w] = 0;
	return 1;
}

static void snmp_octets_to_label_for_oid(const char *oid_dotted, const uint8_t *p, size_t n, char *out, size_t outcap)
{
	if (!out || !outcap)
		return;
	if (oid_dotted && !strcmp(oid_dotted, "1.3.6.1.2.1.25.1.2.0") && snmp_date_and_time_try_label(p, n, out, outcap))
		return;
	if (n > 0 && snmp_octets_printable_label(p, n)) {
		if (n + 1 < outcap) {
			memcpy(out, p, n);
			out[n] = 0;
			return;
		}
	}
	if (n > 0 && snmp_octets_to_label_escaped(p, n, out, outcap))
		return;
	snmp_octets_to_label(p, n, out, outcap);
}

static void snmp_emit_varbind(context_arg *carg, snmp_vb *vb)
{
	char mname[SNMP_METRIC_NAME_MAX];
	int oid_as_name = snmp_env_metric_from_oid(carg);
	int decode_tcp = snmp_env_decode_tcp_enabled(carg);
	int decode_tcp_names = snmp_env_decode_tcp_metric_names_enabled(carg);
	int friendly_names = snmp_env_friendly_names_enabled(carg);
	snmp_tcp_decoded td = {0};
	int td_ok = 0;
	int td_listener = 0;
	const char *tcp_mname = NULL;
	const char *friendly_mname = NULL;
	char hr_index[32];
	int hr_index_ok = 0;
	char special_index[SNMP_OID_LABEL];
	int special_index_ok = 0;
	char generic_index[32];
	int generic_index_ok = 0;
	char at_ifindex[16];
	char at_ip[32];
	int at_phys_ok = 0;
	if (decode_tcp) {
		if (snmp_decode_tcpconn_ipv4(vb->oidstr, &td) || snmp_decode_tcpconnection_ipv4(vb->oidstr, &td)) {
			td_ok = 1;
			td_listener = 0;
		} else if (snmp_decode_tcplistener_ipv4(vb->oidstr, &td)) {
			td_ok = 1;
			td_listener = 1;
		}
		if (decode_tcp_names)
			tcp_mname = snmp_tcp_metric_name_by_oid(vb->oidstr);
	}
	if (friendly_names)
		friendly_mname = snmp_friendly_metric_name_by_oid(vb->oidstr);
	if (friendly_mname && !strncmp(friendly_mname, "snmp_hr_", 8))
		hr_index_ok = snmp_hr_table_index_from_oid(vb->oidstr, hr_index, sizeof hr_index);
	if (friendly_mname && !hr_index_ok)
		special_index_ok = snmp_special_index_label_from_oid(vb->oidstr, special_index, sizeof special_index);
	if (friendly_mname && !hr_index_ok && !special_index_ok)
		generic_index_ok = snmp_last_numeric_index_from_oid(vb->oidstr, generic_index, sizeof generic_index);
	if (friendly_mname && !strcmp(friendly_mname, "snmp_at_phys_address_info"))
		at_phys_ok = snmp_decode_at_phys_ipv4(vb->oidstr, at_ifindex, sizeof at_ifindex, at_ip, sizeof at_ip);

	if (!vb->parse_ok)
		return;
	if (vb->errstat != 0) {
		double es = (double)vb->errstat;
		metric_add_labels2("snmp_error", &es, DATATYPE_DOUBLE, carg, "status", "error_status", "index",
				   "0");
		return;
	}
	if (vb->end_of_mib)
		return;

	char oid_lab[SNMP_OID_LABEL];
	snmp_oid_label_relative(carg, vb->oidstr, oid_lab, sizeof oid_lab);

	if (vb->valtag == 0x80 || vb->valtag == 0x81) {
		double z = 0.0;
		char *reason = (char *)(vb->valtag == 0x80 ? "noSuchObject" : "noSuchInstance");
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "missing");
			snmp_ml2(carg, mname, &z, DATATYPE_DOUBLE, "oid", oid_lab, "reason", reason);
		} else
			snmp_ml2(carg, "snmp_scrape_missing", &z, DATATYPE_DOUBLE, "oid", oid_lab, "reason",
				 reason);
		return;
	}

	if (vb->valtag == 0x02) {
		int64_t iv;
		if (!ber_read_integer(vb->valbytes, vb->vallen, &iv)) {
			double dv = (double)iv;
			char port_local[16], port_rem[16];
			const char *state = NULL;
			if (decode_tcp && td_ok) {
				strlcpy(port_local, td.local_port, sizeof port_local);
				strlcpy(port_rem, td.rem_port, sizeof port_rem);
				state = snmp_tcp_state_name((uint64_t)iv);
			}
			if (oid_as_name) {
				snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, NULL);
				char *met = (char *)(tcp_mname ? tcp_mname : mname);
				if (decode_tcp && td_ok && td_listener)
					snmp_ml3(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local);
				else if (decode_tcp && td_ok && state)
					snmp_ml6(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem,
						 "state", (char *)state);
				else if (decode_tcp && td_ok)
					snmp_ml5(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem);
				else
					if (hr_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
					else if (special_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
					else if (generic_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
					else
						snmp_ml1(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab);
			} else
				if (decode_tcp && td_ok && td_listener)
					snmp_ml3(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
						 td.local_addr, "local_port", port_local);
				else if (decode_tcp && td_ok && state)
					snmp_ml6(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
						 td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr, "rem_port",
						 port_rem, "state", (char *)state);
				else if (decode_tcp && td_ok)
					snmp_ml5(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
						 td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr, "rem_port",
						 port_rem);
				else
					if (hr_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
					else if (special_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
					else if (generic_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
					else
						snmp_ml1(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab);
		}
	} else if (vb->valtag == SNMP_TAG_COUNTER32 || vb->valtag == SNMP_TAG_GAUGE32 ||
		   vb->valtag == SNMP_TAG_TIMETICKS) {
		uint64_t uv;
		if (!ber_read_uinteger(vb->valbytes, vb->vallen, &uv)) {
			double dv = (double)uv;
			char port_local[16], port_rem[16];
			if (decode_tcp && td_ok) {
				strlcpy(port_local, td.local_port, sizeof port_local);
				strlcpy(port_rem, td.rem_port, sizeof port_rem);
			}
			if (oid_as_name) {
				snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, NULL);
				char *met = (char *)(tcp_mname ? tcp_mname : mname);
				if (decode_tcp && td_ok && td_listener)
					snmp_ml3(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local);
				else if (decode_tcp && td_ok)
					snmp_ml5(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem);
				else
					if (hr_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
					else if (special_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
					else if (generic_index_ok)
						snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
					else
						snmp_ml1(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab);
			} else
				if (decode_tcp && td_ok && td_listener)
					snmp_ml3(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
						 td.local_addr, "local_port", port_local);
				else if (decode_tcp && td_ok)
					snmp_ml5(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
						 td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr, "rem_port",
						 port_rem);
				else
					if (hr_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
					else if (special_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
					else if (generic_index_ok)
						snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
					else
						snmp_ml1(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab);
		}
	} else if (vb->valtag == SNMP_TAG_COUNTER64) {
		uint64_t uv;
		if (!ber_read_uinteger(vb->valbytes, vb->vallen, &uv)) {
			double dv = (double)uv;
			if (oid_as_name) {
				snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, NULL);
				if (hr_index_ok)
					snmp_ml2(carg, mname, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
				else if (special_index_ok)
					snmp_ml2(carg, mname, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
				else if (generic_index_ok)
					snmp_ml2(carg, mname, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
				else
					snmp_ml1(carg, mname, &dv, DATATYPE_DOUBLE, "oid", oid_lab);
			} else
				if (hr_index_ok)
					snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
				else if (special_index_ok)
					snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
				else if (generic_index_ok)
					snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
				else
					snmp_ml1(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab);
		}
	} else if (vb->valtag == SNMP_TAG_OPAQUE) {
		double od;
		if (snmp_opaque_try_double(vb->valbytes, vb->vallen, &od)) {
			double dv = od;
			char port_local[16], port_rem[16];
			if (decode_tcp && td_ok) {
				strlcpy(port_local, td.local_port, sizeof port_local);
				strlcpy(port_rem, td.rem_port, sizeof port_rem);
			}
			if (oid_as_name) {
				snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, NULL);
				char *met = (char *)(tcp_mname ? tcp_mname : mname);
				if (decode_tcp && td_ok && td_listener)
					snmp_ml3(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local);
				else if (decode_tcp && td_ok)
					snmp_ml5(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr", td.local_addr,
						 "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem);
				else if (hr_index_ok)
					snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
				else if (special_index_ok)
					snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
				else if (generic_index_ok)
					snmp_ml2(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
				else
					snmp_ml1(carg, met, &dv, DATATYPE_DOUBLE, "oid", oid_lab);
			} else if (decode_tcp && td_ok && td_listener)
				snmp_ml3(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
					 td.local_addr, "local_port", port_local);
			else if (decode_tcp && td_ok)
				snmp_ml5(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_value"), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "local_addr",
					 td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem);
			else if (hr_index_ok)
				snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "hr_index", hr_index);
			else if (special_index_ok)
				snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", special_index);
			else if (generic_index_ok)
				snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab, "index", generic_index);
			else
				snmp_ml1(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_value")), &dv, DATATYPE_DOUBLE, "oid", oid_lab);
		}
	} else if (vb->valtag == 0x04) {
		char scratch[SNMP_OID_LABEL];
		snmp_octets_to_label_for_oid(vb->oidstr, vb->valbytes, vb->vallen, scratch, sizeof scratch);
		double onev = 1.0;
		char sym[128];
		sym[0] = 0;
		if (snmp_env_oid_symbols_enabled(carg) && snmp_str_is_dotted_oid(scratch))
			snmp_wellknown_symbol(carg, scratch, sym, sizeof sym);
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "string");
			if (sym[0])
				if (at_phys_ok)
					snmp_ml5(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "if_index", at_ifindex, "ip", at_ip, "symbol", sym);
				else if (hr_index_ok)
					snmp_ml4(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "symbol", sym, "hr_index", hr_index);
				else if (special_index_ok)
					snmp_ml4(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "symbol", sym, "index", special_index);
				else
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "symbol", sym);
			else
				if (at_phys_ok)
					snmp_ml4(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "if_index", at_ifindex, "ip", at_ip);
				else if (hr_index_ok)
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "hr_index", hr_index);
				else if (special_index_ok)
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "index", special_index);
				else if (generic_index_ok)
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch,
						 "index", generic_index);
				else
					snmp_ml2(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", scratch);
		} else if (sym[0])
			if (at_phys_ok)
				snmp_ml5(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "if_index", at_ifindex, "ip", at_ip, "symbol", sym);
			else if (hr_index_ok)
				snmp_ml4(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "symbol", sym, "hr_index", hr_index);
			else if (special_index_ok)
				snmp_ml4(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "symbol", sym, "index", special_index);
			else
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "symbol", sym);
		else
			if (at_phys_ok)
				snmp_ml4(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "if_index", at_ifindex, "ip", at_ip);
			else if (hr_index_ok)
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "hr_index", hr_index);
			else if (special_index_ok)
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "index", special_index);
			else if (generic_index_ok)
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch, "index", generic_index);
			else
				snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 scratch);
	} else if (vb->valtag == SNMP_TAG_IPADDR && vb->vallen == 4) {
		char ip[32];
		snprintf(ip, sizeof ip, "%u.%u.%u.%u", vb->valbytes[0], vb->valbytes[1], vb->valbytes[2],
			 vb->valbytes[3]);
		double onev = 1.0;
		char port_local[16], port_rem[16];
		if (decode_tcp && td_ok) {
			strlcpy(port_local, td.local_port, sizeof port_local);
			strlcpy(port_rem, td.rem_port, sizeof port_rem);
		}
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "string");
			char *met = (char *)(tcp_mname ? tcp_mname : mname);
			if (decode_tcp && td_ok)
				snmp_ml6(carg, met, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip, "local_addr",
					 td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr, "rem_port", port_rem);
			else
				if (hr_index_ok)
					snmp_ml3(carg, met, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip, "hr_index", hr_index);
				else
					snmp_ml2(carg, met, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip);
		} else
			if (decode_tcp && td_ok)
				snmp_ml6(carg, (char *)(tcp_mname ? tcp_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip,
					 "local_addr", td.local_addr, "local_port", port_local, "rem_addr", td.rem_addr,
					 "rem_port", port_rem);
			else
				if (hr_index_ok)
					snmp_ml3(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_string")), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip, "hr_index", hr_index);
				else
					snmp_ml2(carg, (char *)(tcp_mname ? tcp_mname : (friendly_mname ? friendly_mname : "snmp_scrape_string")), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", ip);
	} else if (vb->valtag == 0x06) {
		/* OBJECT IDENTIFIER (e.g. sysObjectID 1.3.6.1.2.1.1.2.0) */
		char val_oid[SNMP_OID_LABEL];
		char val_lab[SNMP_OID_LABEL];
		char sym[128];
		snmp_oid_ber_to_dotted(vb->valbytes, vb->vallen, val_oid, sizeof val_oid);
		snmp_oid_label_relative(carg, val_oid, val_lab, sizeof val_lab);
		sym[0] = 0;
		if (snmp_env_oid_symbols_enabled(carg))
			snmp_wellknown_symbol(carg, val_oid, sym, sizeof sym);
		double onev = 1.0;
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "object_id");
			if (sym[0])
				if (hr_index_ok)
					snmp_ml4(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", val_lab,
						 "symbol", sym, "hr_index", hr_index);
				else
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", val_lab,
						 "symbol", sym);
			else
				if (hr_index_ok)
					snmp_ml3(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", val_lab,
						 "hr_index", hr_index);
				else
					snmp_ml2(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", val_lab);
		} else if (sym[0])
			if (hr_index_ok)
				snmp_ml4(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 val_lab, "symbol", sym, "hr_index", hr_index);
			else
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 val_lab, "symbol", sym);
		else
			if (hr_index_ok)
				snmp_ml3(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 val_lab, "hr_index", hr_index);
			else
				snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
					 val_lab);
	} else if (vb->valtag == 0x03) {
		/* BIT STRING: leading octet = unused bits in last data octet */
		char bits_hex[SNMP_OID_LABEL];
		bits_hex[0] = 0;
		if (vb->vallen > 1) {
			size_t hi = 0;
			for (size_t i = 1; i < vb->vallen && hi + 3 < sizeof bits_hex; i++)
				hi += (size_t)snprintf(bits_hex + hi, sizeof bits_hex - hi, "%02x",
						       vb->valbytes[i]);
		}
		double onev = 1.0;
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "bits");
			snmp_ml2(carg, mname, &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value", bits_hex);
		} else
			snmp_ml2(carg, (char *)(friendly_mname ? friendly_mname : "snmp_scrape_string"), &onev, DATATYPE_DOUBLE, "oid", oid_lab, "value",
				 bits_hex);
	} else {
		double dv = (double)vb->valtag;
		if (oid_as_name) {
			snmp_metric_name_from_oid(mname, sizeof mname, vb->oidstr, "unhandled");
			snmp_ml1(carg, mname, &dv, DATATYPE_DOUBLE, "oid", oid_lab);
		} else
			snmp_ml1(carg, "snmp_scrape_unhandled_type", &dv, DATATYPE_DOUBLE, "oid", oid_lab);
	}
}

static int snmp_oid_under_subtree(const char *oid, const char *base)
{
	if (!base || !base[0])
		return 1;
	size_t bl = strlen(base);
	if (strncmp(oid, base, bl) != 0)
		return 0;
	return oid[bl] == '.' || oid[bl] == '\0';
}

/* Lexicographic compare of dotted OIDs (numeric components). */
static int snmp_oid_cmp(const char *a, const char *b)
{
	if (!a || !a[0]) {
		if (!b || !b[0])
			return 0;
		return -1;
	}
	if (!b || !b[0])
		return 1;
	for (;;) {
		unsigned long na = 0, nb = 0;
		while (*a >= '0' && *a <= '9')
			na = na * 10ul + (unsigned long)(*a++ - '0');
		while (*b >= '0' && *b <= '9')
			nb = nb * 10ul + (unsigned long)(*b++ - '0');
		if (na != nb)
			return na < nb ? -1 : 1;
		if (*a == '.' && *b == '.') {
			a++;
			b++;
			continue;
		}
		if (*a == '.' && *b == '\0')
			return 1;
		if (*a == '\0' && *b == '.')
			return -1;
		return 0;
	}
}

static void snmp_socket_recv_timeout(int fd, int64_t timeout_ms)
{
	struct timeval tv;
	if (timeout_ms < 1)
		timeout_ms = 5000;
	tv.tv_sec = (time_t)(timeout_ms / 1000);
	tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

static void snmp_walk_continue(context_arg *carg, const char *subtree, char *next_oid, uint64_t max_more)
{
	const char *comm = carg->user[0] ? carg->user : "public";
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		carglog(carg, L_ERROR, "snmp walk: socket: %s\n", strerror(errno));
		return;
	}
	snmp_socket_recv_timeout(fd, carg->timeout);

	uint8_t *rbuf = malloc(SNMP_RECV_MAX);
	if (!rbuf) {
		close(fd);
		return;
	}

	uint8_t pkt[SNMP_MAX_PACKET];
	for (uint64_t step = 0; step < max_more; step++) {
		char req_oid[SNMP_OID_LABEL];
		strlcpy(req_oid, next_oid, sizeof req_oid);

		size_t pktlen = 0;
		if (snmp_build_v2c_get(comm, next_oid, 1, pkt, sizeof pkt, &pktlen) < 0)
			break;
		if (sendto(fd, pkt, pktlen, 0, (struct sockaddr *)&carg->dest, sizeof carg->dest) < 0) {
			carglog(carg, L_ERROR, "snmp walk: sendto: %s\n", strerror(errno));
			break;
		}
		ssize_t nr = recvfrom(fd, rbuf, SNMP_RECV_MAX, 0, NULL, NULL);
		if (nr <= 0) {
			if (nr < 0)
				carglog(carg, L_ERROR, "snmp walk: recvfrom: %s\n", strerror(errno));
			break;
		}

		snmp_vb vb;
		if (snmp_parse_first_varbind(rbuf, (size_t)nr, &vb) < 0 || !vb.parse_ok)
			break;
		if (vb.errstat != 0)
			break;
		if (vb.end_of_mib || vb.valtag == 0x82)
			break;
		if (!snmp_oid_under_subtree(vb.oidstr, subtree))
			break;

		/*
		 * Valid GET-NEXT advances lexicographically. Many agents return the *same* OID as the
		 * request with endOfMibView (0x82); if that were mis-parsed, we would spin forever.
		 */
		if (snmp_oid_cmp(vb.oidstr, req_oid) <= 0) {
			snmp_emit_varbind(carg, &vb);
			break;
		}

		snmp_emit_varbind(carg, &vb);
		if (vb.valtag == 0x80 || vb.valtag == 0x81)
			break;

		strlcpy(next_oid, vb.oidstr, SNMP_OID_LABEL);
	}

	free(rbuf);
	close(fd);
}

void snmp_handler(char *metrics, size_t size, context_arg *carg)
{
	const uint8_t *buf = (const uint8_t *)metrics;
	snmp_vb vb;

	if (snmp_parse_first_varbind(buf, size, &vb) < 0) {
		carglog(carg, L_ERROR, "snmp: parse error\n");
		goto done;
	}

	if (vb.errstat != 0) {
		double es = (double)vb.errstat;
		metric_add_labels2("snmp_error", &es, DATATYPE_DOUBLE, carg, "status", "error_status", "index",
				   "0");
		carglog(carg, L_INFO, "snmp: agent error_status=%" PRId64 "\n", vb.errstat);
		goto done;
	}

	const char *qu = carg->query_url ? carg->query_url : "";
	int walk = !strncmp(qu, "walk/", 5);
	char subtree[SNMP_OID_LABEL];
	subtree[0] = 0;
	if (walk) {
		const char *p = qu + 5;
		while (*p == '/')
			p++;
		strlcpy(subtree, p, sizeof subtree);
		size_t sl = strlen(subtree);
		while (sl > 0 && subtree[sl - 1] == '/')
			subtree[--sl] = 0;
	}

	snmp_emit_varbind(carg, &vb);

	if (walk && vb.parse_ok && !vb.end_of_mib && vb.errstat == 0 && vb.valtag != 0x80 &&
	    vb.valtag != 0x81) {
		if (!snmp_oid_under_subtree(vb.oidstr, subtree))
			goto done;

		char next_oid[SNMP_OID_LABEL];
		strlcpy(next_oid, vb.oidstr, sizeof next_oid);
		uint64_t cap = SNMP_WALK_MAX_STEPS > 0 ? SNMP_WALK_MAX_STEPS - 1u : 0;
		snmp_walk_continue(carg, subtree, next_oid, cap);
	}

done:
	carg->parser_status = 1;
}

string *snmp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	(void)env;
	(void)proxy_settings;

	const char *q = hi->query ? hi->query : "";
	int getnext = 0;
	const char *oidpart = q;
	int is_walk = 0;

	if (!strncmp(q, "walk/", 5)) {
		is_walk = 1;
		getnext = 1;
		oidpart = q + 5;
	} else if (!strncmp(q, "next/", 5)) {
		getnext = 1;
		oidpart = q + 5;
	} else if (!strncmp(q, "getnext/", 8)) {
		getnext = 1;
		oidpart = q + 8;
	}

	while (*oidpart == '/' || *oidpart == ' ')
		oidpart++;

	if (!oidpart[0] && !is_walk) {
		if (ac->log_level > 0)
			fprintf(stderr,
				"snmp: empty OID in URL path (e.g. udp://public@host:161/1.3.6.1.2.1.1.3.0 or walk/...)\n");
		return string_init_add_auto(strdup(""));
	}

	uint8_t *pkt = malloc(SNMP_MAX_PACKET);
	if (!pkt)
		return string_init_add_auto(strdup(""));

	size_t pktlen = 0;
	if (snmp_build_v2c_get(hi->user, oidpart, getnext, pkt, SNMP_MAX_PACKET, &pktlen) < 0) {
		free(pkt);
		return string_init_add_auto(strdup(""));
	}

	string *ret = malloc(sizeof(*ret));
	ret->s = (char *)pkt;
	ret->l = pktlen;
	ret->m = pktlen;
	return ret;
}

void snmp_parser_push(void)
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("snmp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = snmp_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = snmp_mesg;
	strlcpy(actx->handler[0].key, "snmp", sizeof actx->handler[0].key);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
