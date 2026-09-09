#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <time.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/url.h"
#include "parsers/http_proto.h"
#include "main.h"

#define OS_STR 256
#define OS_JOIN 2048
#define OS_URL 1024
#define MEGABYTE (1024LL * 1024LL)
#define GIGABYTE (1024LL * 1024LL * 1024LL)

typedef struct openstack_settings {
	char project[OS_STR];
	char user_domain[OS_STR];
	char project_domain[OS_STR];
	char region[OS_STR];
	char endpoint_type[32];
	char username[OS_STR];
	char password[AUTH_SIZE];
} openstack_settings;

static void os_family(context_arg *carg, const char *name)
{
	namespace_metric_family_set(NULL, carg, name, METRIC_TYPE_GAUGE, "OpenStack exporter-compatible metric.");
}

static void os_up(context_arg *carg, const char *name)
{
	int64_t one = 1;

	os_family(carg, name);
	metric_add_auto((char *)name, &one, DATATYPE_INT, carg);
}

static void os_count(context_arg *carg, const char *name, int64_t v)
{
	os_family(carg, name);
	metric_add_auto((char *)name, &v, DATATYPE_INT, carg);
}

static const char *os_cstr(json_t *j)
{
	const char *s = json_string_value(j);

	return s ? s : "";
}

static const char *os_obj_str(json_t *obj, const char *key)
{
	return os_cstr(json_object_get(obj, key));
}

static void os_jstr(json_t *j, char *buf, size_t n)
{
	buf[0] = 0;
	if (!j || json_is_null(j))
		return;
	if (json_is_string(j)) {
		strlcpy(buf, json_string_value(j), n);
		return;
	}
	if (json_is_integer(j)) {
		snprintf(buf, n, "%" PRId64, (int64_t)json_integer_value(j));
		return;
	}
	if (json_is_real(j)) {
		snprintf(buf, n, "%g", json_real_value(j));
		return;
	}
	if (json_is_true(j)) {
		strlcpy(buf, "true", n);
		return;
	}
	if (json_is_false(j)) {
		strlcpy(buf, "false", n);
		return;
	}
}

static void os_obj_jstr(json_t *obj, const char *key, char *buf, size_t n)
{
	os_jstr(json_object_get(obj, key), buf, n);
}

static int64_t os_obj_i64(json_t *obj, const char *key)
{
	json_t *j = json_object_get(obj, key);

	if (!j || json_is_null(j))
		return 0;
	if (json_is_integer(j))
		return (int64_t)json_integer_value(j);
	if (json_is_real(j))
		return (int64_t)json_real_value(j);
	if (json_is_string(j))
		return atoll(json_string_value(j));
	if (json_is_true(j))
		return 1;
	return 0;
}

static double os_obj_f64(json_t *obj, const char *key)
{
	json_t *j = json_object_get(obj, key);

	if (!j || json_is_null(j))
		return 0;
	if (json_is_real(j))
		return json_real_value(j);
	if (json_is_integer(j))
		return (double)json_integer_value(j);
	if (json_is_string(j))
		return atof(json_string_value(j));
	return 0;
}

static const char *os_bool_str(json_t *j)
{
	if (!j || json_is_null(j))
		return "false";
	if (json_is_string(j))
		return json_string_value(j);
	return json_is_true(j) ? "true" : "false";
}

static void os_join_str_array(json_t *arr, char *buf, size_t n)
{
	size_t i;
	size_t off = 0;
	size_t sz;

	buf[0] = 0;
	if (!json_is_array(arr))
		return;
	sz = json_array_size(arr);
	for (i = 0; i < sz && off + 1 < n; i++) {
		const char *s = json_string_value(json_array_get(arr, i));
		size_t sl;

		if (!s)
			continue;
		if (off) {
			if (off + 1 >= n)
				break;
			buf[off++] = ',';
		}
		sl = strlen(s);
		if (off + sl >= n)
			sl = n - off - 1;
		memcpy(buf + off, s, sl);
		off += sl;
		buf[off] = 0;
	}
}

static json_t *os_load(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root;

	if (!metrics || !size)
		return NULL;
	root = json_loadb(metrics, size, 0, &error);
	if (!root) {
		carglog(carg, L_ERROR, "openstack json error on line %d: %s\n", error.line, error.text);
		return NULL;
	}
	return root;
}

static json_t *os_array(json_t *root, const char *key)
{
	json_t *arr = json_object_get(root, key);

	return json_is_array(arr) ? arr : NULL;
}

static int os_map_named(const char *const *names, size_t n, const char *cur)
{
	size_t i;

	if (!cur || !*cur)
		return -1;
	for (i = 0; i < n; i++) {
		if (!strcasecmp(names[i], cur))
			return (int)i;
	}
	return -1;
}

static int os_server_status(const char *s)
{
	static const char *const names[] = {
		"ACTIVE", "BUILD", "BUILD(spawning)", "DELETED", "ERROR", "HARD_REBOOT", "PASSWORD",
		"REBOOT", "REBUILD", "RESCUE", "RESIZE", "SHUTOFF", "SUSPENDED", "UNKNOWN",
		"VERIFY_RESIZE", "MIGRATING", "PAUSED", "REVERT_RESIZE", "SHELVED",
		"SHELVED_OFFLOADED", "SOFT_DELETED"
	};

	return os_map_named(names, sizeof(names) / sizeof(names[0]), s);
}

static int os_network_status(const char *s)
{
	static const char *const names[] = { "ACTIVE", "BUILD", "DOWN", "ERROR" };

	return os_map_named(names, sizeof(names) / sizeof(names[0]), s);
}

static int os_volume_status(const char *s)
{
	static const char *const names[] = {
		"creating", "available", "reserved", "attaching", "detaching", "in-use",
		"maintenance", "deleting", "awaiting-transfer", "error", "error_deleting",
		"backing-up", "restoring-backup", "error_backing-up", "error_restoring",
		"error_extending", "downloading", "uploading", "retyping", "extending"
	};

	return os_map_named(names, sizeof(names) / sizeof(names[0]), s);
}

static int64_t os_parse_time(const char *s)
{
	struct tm tm;
	const char *p;

	if (!s || !*s)
		return 0;
	memset(&tm, 0, sizeof(tm));
	p = strptime(s, "%Y-%m-%dT%H:%M:%S", &tm);
	if (!p)
		return 0;
	return (int64_t)timegm(&tm);
}

static void os_join_url(char *dst, size_t n, const char *base, const char *path)
{
	size_t bl;
	const char *p = path ? path : "";

	if (!base)
		base = "";
	bl = strlen(base);
	while (bl && base[bl - 1] == '/')
		bl--;
	if (*p == '/')
		p++;
	snprintf(dst, n, "%.*s/%s", (int)bl, base, p);
}

static const char *os_norm_type(const char *type)
{
	if (!type)
		return "";
	if (!strcmp(type, "volumev3") || !strcmp(type, "block-storage") || !strcmp(type, "volumev2"))
		return "volume";
	return type;
}

static int os_iface_ok(const char *iface, const char *want)
{
	if (!want || !*want)
		want = "public";
	if (!iface || !*iface)
		return 1;
	return !strcasecmp(iface, want);
}

static int os_region_ok(const char *region, const char *region_id, const char *want)
{
	if (!want || !*want)
		return 1;
	if (region && !strcmp(region, want))
		return 1;
	if (region_id && !strcmp(region_id, want))
		return 1;
	return 0;
}

static void os_oneshot(context_arg *carg, const char *url, const char *token,
	void (*handler)(char *, size_t, context_arg *), const char *parser_name,
	const char *hdr_name, const char *hdr_value)
{
	host_aggregator_info *hi;
	alligator_ht *env;
	char *mesg;
	char *key;

	if (!carg || !carg->loop || !url || !*url || !handler || !token)
		return;
	hi = parse_url((char *)url, strlen(url));
	if (!hi)
		return;
	env = env_struct_duplicate(carg->env);
	if (!env)
		env = alligator_ht_init(NULL);
	env_struct_push_alloc(env, "X-Auth-Token", (char *)token);
	if (hdr_name && hdr_value)
		env_struct_push_alloc(env, (char *)hdr_name, (char *)hdr_value);
	mesg = gen_http_query(HTTP_GET, hi->query, NULL, hi->host ? hi->host : "localhost",
		"alligator", NULL, "1.1", env, NULL, NULL);
	key = malloc(512);
	snprintf(key, 512, "openstack:%s:%s", parser_name, url);
	aggregator_oneshot(carg, (char *)url, strlen(url), mesg, mesg ? strlen(mesg) : 0,
		handler, (char *)parser_name, NULL, key, carg->follow_redirects, carg->data, NULL, 0, NULL, env);
	env_free(env);
	url_free(hi);
}

static const char *os_json_pick(json_t *root, const char *a, const char *b)
{
	const char *s = os_obj_str(root, a);

	if (s && *s)
		return s;
	return os_obj_str(root, b);
}

static const char *os_env_json(json_t *env, const char *k)
{
	if (!json_is_object(env))
		return NULL;
	return json_string_value(json_object_get(env, k));
}

void *openstack_data_func(host_aggregator_info *hi, void *arg, void *data)
{
	json_t *root = data;
	json_t *env;
	openstack_settings *st = calloc(1, sizeof(*st));
	const char *s;

	(void)arg;
	if (!st)
		return NULL;
	if (hi && hi->user)
		strlcpy(st->username, hi->user, sizeof(st->username));
	if (hi && hi->pass)
		strlcpy(st->password, hi->pass, sizeof(st->password));
	if (json_is_object(root)) {
		if ((s = os_json_pick(root, "project", "project_name")) && *s)
			strlcpy(st->project, s, sizeof(st->project));
		if ((s = os_json_pick(root, "user_domain", "user_domain_name")) && *s)
			strlcpy(st->user_domain, s, sizeof(st->user_domain));
		if ((s = os_json_pick(root, "project_domain", "project_domain_name")) && *s)
			strlcpy(st->project_domain, s, sizeof(st->project_domain));
		if ((s = os_json_pick(root, "region", "region_name")) && *s)
			strlcpy(st->region, s, sizeof(st->region));
		if ((s = os_obj_str(root, "endpoint_type")) && *s)
			strlcpy(st->endpoint_type, s, sizeof(st->endpoint_type));
		env = json_object_get(root, "env");
		if ((s = os_env_json(env, "OS_PROJECT_NAME")) && *s && !st->project[0])
			strlcpy(st->project, s, sizeof(st->project));
		if ((s = os_env_json(env, "OS_USER_DOMAIN_NAME")) && *s && !st->user_domain[0])
			strlcpy(st->user_domain, s, sizeof(st->user_domain));
		if ((s = os_env_json(env, "OS_PROJECT_DOMAIN_NAME")) && *s && !st->project_domain[0])
			strlcpy(st->project_domain, s, sizeof(st->project_domain));
		if ((s = os_env_json(env, "OS_REGION_NAME")) && *s && !st->region[0])
			strlcpy(st->region, s, sizeof(st->region));
		if ((s = os_env_json(env, "OS_INTERFACE")) && *s && !st->endpoint_type[0])
			strlcpy(st->endpoint_type, s, sizeof(st->endpoint_type));
		if ((s = os_env_json(env, "OS_ENDPOINT_TYPE")) && *s && !st->endpoint_type[0])
			strlcpy(st->endpoint_type, s, sizeof(st->endpoint_type));
	}
	if (!st->user_domain[0])
		strlcpy(st->user_domain, "Default", sizeof(st->user_domain));
	if (!st->project_domain[0])
		strlcpy(st->project_domain, "Default", sizeof(st->project_domain));
	if (!st->endpoint_type[0])
		strlcpy(st->endpoint_type, "public", sizeof(st->endpoint_type));
	if (!st->project[0] && st->username[0])
		strlcpy(st->project, st->username, sizeof(st->project));
	return st;
}

void openstack_identity_domains_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "domains");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_identity_domains", (int64_t)n);
	os_family(carg, "openstack_identity_domain_info");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		alligator_ht *lbl = alligator_ht_init(NULL);

		labels_hash_insert_nocache(lbl, "description", (char *)os_obj_str(it, "description"));
		labels_hash_insert_nocache(lbl, "enabled", (char *)os_bool_str(json_object_get(it, "enabled")));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		metric_add("openstack_identity_domain_info", lbl, &one, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_identity_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_identity_users_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "users");
	os_count(carg, "openstack_identity_users", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_identity_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_identity_groups_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "groups");
	os_count(carg, "openstack_identity_groups", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_identity_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_identity_projects_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "projects");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_identity_projects", (int64_t)n);
	os_family(carg, "openstack_identity_project_info");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		alligator_ht *lbl = alligator_ht_init(NULL);
		char tags[OS_JOIN];

		os_join_str_array(json_object_get(it, "tags"), tags, sizeof(tags));
		labels_hash_insert_nocache(lbl, "is_domain", (char *)os_bool_str(json_object_get(it, "is_domain")));
		labels_hash_insert_nocache(lbl, "description", (char *)os_obj_str(it, "description"));
		labels_hash_insert_nocache(lbl, "domain_id", (char *)os_obj_str(it, "domain_id"));
		labels_hash_insert_nocache(lbl, "enabled", (char *)os_bool_str(json_object_get(it, "enabled")));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "parent_id", (char *)os_obj_str(it, "parent_id"));
		labels_hash_insert_nocache(lbl, "tags", tags);
		metric_add("openstack_identity_project_info", lbl, &one, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_identity_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_identity_regions_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "regions");
	os_count(carg, "openstack_identity_regions", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_identity_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_flavors_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "flavors");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_nova_flavors", (int64_t)n);
	os_family(carg, "openstack_nova_flavor");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		char vcpus[32], ram[32], disk[32];
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_obj_jstr(it, "vcpus", vcpus, sizeof(vcpus));
		os_obj_jstr(it, "ram", ram, sizeof(ram));
		os_obj_jstr(it, "disk", disk, sizeof(disk));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "vcpus", vcpus);
		labels_hash_insert_nocache(lbl, "ram", ram);
		labels_hash_insert_nocache(lbl, "disk", disk);
		json_t *pub = json_object_get(it, "os-flavor-access:is_public");
		if (!pub)
			pub = json_object_get(it, "is_public");
		labels_hash_insert_nocache(lbl, "is_public", (char *)os_bool_str(pub));
		metric_add("openstack_nova_flavor", lbl, &one, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_az_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "availabilityZoneInfo");
	os_count(carg, "openstack_nova_availability_zones", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_secgroups_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "security_groups");
	os_count(carg, "openstack_nova_security_groups", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_services_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "services");
	n = arr ? json_array_size(arr) : 0;
	os_family(carg, "openstack_nova_agent_state");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		int64_t state = strcasecmp(os_obj_str(it, "state"), "up") ? 0 : 1;
		char id[OS_STR];
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_obj_jstr(it, "id", id, sizeof(id));
		labels_hash_insert_nocache(lbl, "id", id);
		labels_hash_insert_nocache(lbl, "hostname", (char *)os_obj_str(it, "host"));
		labels_hash_insert_nocache(lbl, "service", (char *)os_obj_str(it, "binary"));
		labels_hash_insert_nocache(lbl, "adminState", (char *)os_obj_str(it, "status"));
		labels_hash_insert_nocache(lbl, "zone", (char *)os_obj_str(it, "zone"));
		labels_hash_insert_nocache(lbl, "disabledReason", (char *)os_obj_str(it, "disabled_reason"));
		metric_add("openstack_nova_agent_state", lbl, &state, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_hypervisors_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "hypervisors");
	n = arr ? json_array_size(arr) : 0;
	os_family(carg, "openstack_nova_current_workload");
	os_family(carg, "openstack_nova_vcpus_available");
	os_family(carg, "openstack_nova_vcpus_used");
	os_family(carg, "openstack_nova_memory_available_bytes");
	os_family(carg, "openstack_nova_memory_used_bytes");
	os_family(carg, "openstack_nova_local_storage_available_bytes");
	os_family(carg, "openstack_nova_local_storage_used_bytes");
	os_family(carg, "openstack_nova_free_disk_bytes");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *cpu = json_object_get(it, "cpu_info");
		json_t *topo = json_is_object(cpu) ? json_object_get(cpu, "topology") : NULL;
		char *host = (char *)os_obj_str(it, "hypervisor_hostname");
		int64_t cells, sockets, cores, threads, vcpus;
		int64_t workload = os_obj_i64(it, "current_workload");
		int64_t mem = os_obj_i64(it, "memory_mb") * MEGABYTE;
		int64_t mem_used = os_obj_i64(it, "memory_mb_used") * MEGABYTE;
		int64_t disk = os_obj_i64(it, "local_gb") * GIGABYTE;
		int64_t disk_used = os_obj_i64(it, "local_gb_used") * GIGABYTE;
		int64_t free_disk = os_obj_i64(it, "free_disk_gb") * GIGABYTE;
		int64_t used = os_obj_i64(it, "vcpus_used");

		if (json_is_object(topo)) {
			cells = os_obj_i64(topo, "cells");
			if (cells < 1)
				cells = 1;
			sockets = os_obj_i64(topo, "sockets");
			cores = os_obj_i64(topo, "cores");
			threads = os_obj_i64(topo, "threads");
			vcpus = cells * sockets * cores * threads;
		} else {
			vcpus = os_obj_i64(it, "vcpus");
		}
		metric_add_labels3("openstack_nova_current_workload", &workload, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_vcpus_available", &vcpus, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_vcpus_used", &used, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_memory_available_bytes", &mem, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_memory_used_bytes", &mem_used, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_local_storage_available_bytes", &disk, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_local_storage_used_bytes", &disk_used, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
		metric_add_labels3("openstack_nova_free_disk_bytes", &free_disk, DATATYPE_INT, carg, "hostname", host, "availability_zone", "", "aggregates", "");
	}
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_nova_servers_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t running = 0;

	if (!root)
		return;
	arr = os_array(root, "servers");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_nova_total_vms", (int64_t)n);
	os_family(carg, "openstack_nova_server_status");
	os_family(carg, "openstack_nova_running_vms");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *flavor = json_object_get(it, "flavor");
		const char *status = os_obj_str(it, "status");
		char flavor_id[OS_STR];
		int64_t code = os_server_status(status);
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_jstr(json_object_get(flavor, "id"), flavor_id, sizeof(flavor_id));
		if (!flavor_id[0])
			os_obj_jstr(flavor, "original_name", flavor_id, sizeof(flavor_id));
		if (!strcasecmp(status, "ACTIVE")) {
			int64_t one = 1;
			char *hv = (char *)os_obj_str(it, "OS-EXT-SRV-ATTR:hypervisor_hostname");
			char *az = (char *)os_obj_str(it, "OS-EXT-AZ:availability_zone");
			char *tenant = (char *)os_obj_str(it, "tenant_id");

			metric_add_labels4("openstack_nova_running_vms", &one, DATATYPE_INT, carg,
				"hostname", hv, "availability_zone", az, "aggregates", "", "tenant_id", tenant);
			running++;
		}
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "status", (char *)status);
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "tenant_id", (char *)os_obj_str(it, "tenant_id"));
		labels_hash_insert_nocache(lbl, "user_id", (char *)os_obj_str(it, "user_id"));
		labels_hash_insert_nocache(lbl, "address_ipv4", (char *)os_obj_str(it, "accessIPv4"));
		labels_hash_insert_nocache(lbl, "address_ipv6", (char *)os_obj_str(it, "accessIPv6"));
		labels_hash_insert_nocache(lbl, "host_id", (char *)os_obj_str(it, "hostId"));
		labels_hash_insert_nocache(lbl, "hypervisor_hostname", (char *)os_obj_str(it, "OS-EXT-SRV-ATTR:hypervisor_hostname"));
		labels_hash_insert_nocache(lbl, "uuid", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "availability_zone", (char *)os_obj_str(it, "OS-EXT-AZ:availability_zone"));
		labels_hash_insert_nocache(lbl, "flavor_id", flavor_id);
		labels_hash_insert_nocache(lbl, "instance_libvirt", (char *)os_obj_str(it, "OS-EXT-SRV-ATTR:instance_name"));
		metric_add("openstack_nova_server_status", lbl, &code, DATATYPE_INT, carg);
	}
	(void)running;
	os_up(carg, "openstack_nova_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_networks_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "networks");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_neutron_networks", (int64_t)n);
	os_family(carg, "openstack_neutron_network");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		char subnets[OS_JOIN], tags[OS_JOIN], mtu[32], seg[32];
		int64_t code = os_network_status(os_obj_str(it, "status"));
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_join_str_array(json_object_get(it, "subnets"), subnets, sizeof(subnets));
		os_join_str_array(json_object_get(it, "tags"), tags, sizeof(tags));
		os_obj_jstr(it, "mtu", mtu, sizeof(mtu));
		os_obj_jstr(it, "provider:segmentation_id", seg, sizeof(seg));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "tenant_id", (char *)os_obj_str(it, "tenant_id"));
		labels_hash_insert_nocache(lbl, "status", (char *)os_obj_str(it, "status"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "is_shared", (char *)os_bool_str(json_object_get(it, "shared")));
		labels_hash_insert_nocache(lbl, "is_external", (char *)os_bool_str(json_object_get(it, "router:external")));
		labels_hash_insert_nocache(lbl, "provider_network_type", (char *)os_obj_str(it, "provider:network_type"));
		labels_hash_insert_nocache(lbl, "provider_physical_network", (char *)os_obj_str(it, "provider:physical_network"));
		labels_hash_insert_nocache(lbl, "provider_segmentation_id", seg);
		labels_hash_insert_nocache(lbl, "subnets", subnets);
		labels_hash_insert_nocache(lbl, "tags", tags);
		labels_hash_insert_nocache(lbl, "mtu", mtu);
		metric_add("openstack_neutron_network", lbl, &code, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_subnets_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "subnets");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_neutron_subnets", (int64_t)n);
	os_family(carg, "openstack_neutron_subnet");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		char dns[OS_JOIN], tags[OS_JOIN];
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_join_str_array(json_object_get(it, "dns_nameservers"), dns, sizeof(dns));
		os_join_str_array(json_object_get(it, "tags"), tags, sizeof(tags));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "tenant_id", (char *)os_obj_str(it, "tenant_id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "network_id", (char *)os_obj_str(it, "network_id"));
		labels_hash_insert_nocache(lbl, "cidr", (char *)os_obj_str(it, "cidr"));
		labels_hash_insert_nocache(lbl, "gateway_ip", (char *)os_obj_str(it, "gateway_ip"));
		labels_hash_insert_nocache(lbl, "enable_dhcp", (char *)os_bool_str(json_object_get(it, "enable_dhcp")));
		labels_hash_insert_nocache(lbl, "dns_nameservers", dns);
		labels_hash_insert_nocache(lbl, "tags", tags);
		metric_add("openstack_neutron_subnet", lbl, &one, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_ports_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t no_ips = 0;
	int64_t lb_down = 0;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "ports");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_neutron_ports", (int64_t)n);
	os_family(carg, "openstack_neutron_port");
	os_family(carg, "openstack_neutron_ports_no_ips");
	os_family(carg, "openstack_neutron_ports_lb_not_active");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *fips = json_object_get(it, "fixed_ips");
		size_t fsz = json_is_array(fips) ? json_array_size(fips) : 0;
		const char *status = os_obj_str(it, "status");
		const char *owner = os_obj_str(it, "device_owner");
		char ips[OS_JOIN];
		alligator_ht *lbl = alligator_ht_init(NULL);
		size_t j;

		ips[0] = 0;
		for (j = 0; j < fsz; j++) {
			const char *ip = os_obj_str(json_array_get(fips, j), "ip_address");
			size_t off = strlen(ips);

			if (!ip || !*ip)
				continue;
			if (off) {
				strlcat(ips, ",", sizeof(ips));
				off = strlen(ips);
			}
			strlcpy(ips + off, ip, sizeof(ips) - off);
		}
		if (!strcasecmp(status, "ACTIVE") && fsz == 0)
			no_ips++;
		if (!strcmp(owner, "neutron:LOADBALANCERV2") && strcasecmp(status, "ACTIVE"))
			lb_down++;
		labels_hash_insert_nocache(lbl, "uuid", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "network_id", (char *)os_obj_str(it, "network_id"));
		labels_hash_insert_nocache(lbl, "mac_address", (char *)os_obj_str(it, "mac_address"));
		labels_hash_insert_nocache(lbl, "device_owner", (char *)owner);
		labels_hash_insert_nocache(lbl, "device_id", (char *)os_obj_str(it, "device_id"));
		labels_hash_insert_nocache(lbl, "status", (char *)status);
		labels_hash_insert_nocache(lbl, "binding_vif_type", (char *)os_obj_str(it, "binding:vif_type"));
		labels_hash_insert_nocache(lbl, "admin_state_up", (char *)os_bool_str(json_object_get(it, "admin_state_up")));
		labels_hash_insert_nocache(lbl, "fixed_ips", ips);
		metric_add("openstack_neutron_port", lbl, &one, DATATYPE_INT, carg);
	}
	metric_add_auto("openstack_neutron_ports_no_ips", &no_ips, DATATYPE_INT, carg);
	metric_add_auto("openstack_neutron_ports_lb_not_active", &lb_down, DATATYPE_INT, carg);
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_floatingips_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t failed = 0;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "floatingips");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_neutron_floating_ips", (int64_t)n);
	os_family(carg, "openstack_neutron_floating_ip");
	os_family(carg, "openstack_neutron_floating_ips_associated_not_active");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		const char *fixed = os_obj_str(it, "fixed_ip_address");
		const char *status = os_obj_str(it, "status");
		alligator_ht *lbl = alligator_ht_init(NULL);

		if (fixed && *fixed && strcasecmp(status, "ACTIVE"))
			failed++;
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "floating_network_id", (char *)os_obj_str(it, "floating_network_id"));
		labels_hash_insert_nocache(lbl, "router_id", (char *)os_obj_str(it, "router_id"));
		labels_hash_insert_nocache(lbl, "status", (char *)status);
		labels_hash_insert_nocache(lbl, "project_id", (char *)os_obj_str(it, "project_id"));
		labels_hash_insert_nocache(lbl, "floating_ip_address", (char *)os_obj_str(it, "floating_ip_address"));
		metric_add("openstack_neutron_floating_ip", lbl, &one, DATATYPE_INT, carg);
	}
	metric_add_auto("openstack_neutron_floating_ips_associated_not_active", &failed, DATATYPE_INT, carg);
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_routers_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t failed = 0;
	int64_t one = 1;

	if (!root)
		return;
	arr = os_array(root, "routers");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_neutron_routers", (int64_t)n);
	os_family(carg, "openstack_neutron_router");
	os_family(carg, "openstack_neutron_routers_not_active");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *gw = json_object_get(it, "external_gateway_info");
		const char *status = os_obj_str(it, "status");
		alligator_ht *lbl = alligator_ht_init(NULL);

		if (strcasecmp(status, "ACTIVE"))
			failed++;
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "project_id", (char *)os_obj_str(it, "project_id"));
		labels_hash_insert_nocache(lbl, "admin_state_up", (char *)os_bool_str(json_object_get(it, "admin_state_up")));
		labels_hash_insert_nocache(lbl, "status", (char *)status);
		labels_hash_insert_nocache(lbl, "external_network_id", (char *)os_obj_str(gw, "network_id"));
		metric_add("openstack_neutron_router", lbl, &one, DATATYPE_INT, carg);
	}
	metric_add_auto("openstack_neutron_routers_not_active", &failed, DATATYPE_INT, carg);
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_secgroups_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;

	if (!root)
		return;
	arr = os_array(root, "security_groups");
	os_count(carg, "openstack_neutron_security_groups", (int64_t)(arr ? json_array_size(arr) : 0));
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_neutron_agents_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "agents");
	n = arr ? json_array_size(arr) : 0;
	os_family(carg, "openstack_neutron_agent_state");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		int64_t state = json_is_true(json_object_get(it, "alive")) ? 1 : 0;
		const char *admin = json_is_true(json_object_get(it, "admin_state_up")) ? "up" : "down";
		alligator_ht *lbl = alligator_ht_init(NULL);

		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "hostname", (char *)os_obj_str(it, "host"));
		labels_hash_insert_nocache(lbl, "service", (char *)os_obj_str(it, "binary"));
		labels_hash_insert_nocache(lbl, "adminState", (char *)admin);
		labels_hash_insert_nocache(lbl, "availability_zone", (char *)os_obj_str(it, "availability_zone"));
		metric_add("openstack_neutron_agent_state", lbl, &state, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_neutron_up");
	json_decref(root);
	carg->parser_status = 1;
}

static const char *os_vol_tenant(json_t *it)
{
	const char *s = os_obj_str(it, "os-vol-tenant-attr:tenant_id");

	if (s && *s)
		return s;
	s = os_obj_str(it, "os-vol-tenant-attr:project_id");
	if (s && *s)
		return s;
	return os_obj_str(it, "os-vol-host-attr:tenant_id");
}

static const char *os_vol_server(json_t *it)
{
	json_t *atts = json_object_get(it, "attachments");
	json_t *first;

	if (!json_is_array(atts) || !json_array_size(atts))
		return "";
	first = json_array_get(atts, 0);
	return os_obj_str(first, "server_id");
}

void openstack_cinder_volumes_handler(char *metrics, size_t size, context_arg *carg)
{
	static const char *const known[] = {
		"creating", "available", "reserved", "attaching", "detaching", "in-use",
		"maintenance", "deleting", "awaiting-transfer", "error", "error_deleting",
		"backing-up", "restoring-backup", "error_backing-up", "error_restoring",
		"error_extending", "downloading", "uploading", "retyping", "extending"
	};
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;
	int64_t counters[sizeof(known) / sizeof(known[0])];
	size_t k;

	memset(counters, 0, sizeof(counters));
	if (!root)
		return;
	arr = os_array(root, "volumes");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_cinder_volumes", (int64_t)n);
	os_family(carg, "openstack_cinder_volume_gb");
	os_family(carg, "openstack_cinder_volume_status");
	os_family(carg, "openstack_cinder_volume_status_counter");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		const char *status = os_obj_str(it, "status");
		const char *server = os_vol_server(it);
		char size_s[32];
		int64_t gb = os_obj_i64(it, "size");
		int64_t code = os_volume_status(status);
		int mapped = os_volume_status(status);
		alligator_ht *lbl = alligator_ht_init(NULL);
		alligator_ht *slbl = alligator_ht_init(NULL);

		if (mapped >= 0)
			counters[mapped]++;
		os_obj_jstr(it, "size", size_s, sizeof(size_s));
		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "status", (char *)status);
		labels_hash_insert_nocache(lbl, "availability_zone", (char *)os_obj_str(it, "availability_zone"));
		labels_hash_insert_nocache(lbl, "bootable", (char *)os_obj_str(it, "bootable"));
		labels_hash_insert_nocache(lbl, "tenant_id", (char *)os_vol_tenant(it));
		labels_hash_insert_nocache(lbl, "user_id", (char *)os_obj_str(it, "user_id"));
		labels_hash_insert_nocache(lbl, "volume_type", (char *)os_obj_str(it, "volume_type"));
		labels_hash_insert_nocache(lbl, "server_id", (char *)server);
		metric_add("openstack_cinder_volume_gb", lbl, &gb, DATATYPE_INT, carg);
		labels_hash_insert_nocache(slbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(slbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(slbl, "status", (char *)status);
		labels_hash_insert_nocache(slbl, "bootable", (char *)os_obj_str(it, "bootable"));
		labels_hash_insert_nocache(slbl, "tenant_id", (char *)os_vol_tenant(it));
		labels_hash_insert_nocache(slbl, "size", size_s);
		labels_hash_insert_nocache(slbl, "volume_type", (char *)os_obj_str(it, "volume_type"));
		labels_hash_insert_nocache(slbl, "server_id", (char *)server);
		metric_add("openstack_cinder_volume_status", slbl, &code, DATATYPE_INT, carg);
	}
	for (k = 0; k < sizeof(known) / sizeof(known[0]); k++)
		metric_add_labels("openstack_cinder_volume_status_counter", &counters[k], DATATYPE_INT, carg, "status", (char *)known[k]);
	os_up(carg, "openstack_cinder_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_cinder_snapshots_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "snapshots");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_cinder_snapshots", (int64_t)n);
	os_family(carg, "openstack_cinder_snapshot_gb");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		int64_t gb = os_obj_i64(it, "size");
		alligator_ht *lbl = alligator_ht_init(NULL);

		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "status", (char *)os_obj_str(it, "status"));
		{
			const char *tenant = os_obj_str(it, "os-extended-snapshot-attributes:project_id");
			if (!tenant || !*tenant)
				tenant = os_obj_str(it, "project_id");
			labels_hash_insert_nocache(lbl, "tenant_id", (char *)tenant);
		}
		labels_hash_insert_nocache(lbl, "volume_id", (char *)os_obj_str(it, "volume_id"));
		metric_add("openstack_cinder_snapshot_gb", lbl, &gb, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_cinder_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_cinder_backups_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "backups");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_cinder_backups", (int64_t)n);
	os_family(carg, "openstack_cinder_backup_gb");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		int64_t gb = os_obj_i64(it, "size");
		alligator_ht *lbl = alligator_ht_init(NULL);

		labels_hash_insert_nocache(lbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(lbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(lbl, "status", (char *)os_obj_str(it, "status"));
		{
			const char *tenant = os_obj_str(it, "os-backup-project-attr:project_id");
			if (!tenant || !*tenant)
				tenant = os_obj_str(it, "project_id");
			labels_hash_insert_nocache(lbl, "tenant_id", (char *)tenant);
		}
		labels_hash_insert_nocache(lbl, "volume_id", (char *)os_obj_str(it, "volume_id"));
		labels_hash_insert_nocache(lbl, "snapshot_id", (char *)os_obj_str(it, "snapshot_id"));
		labels_hash_insert_nocache(lbl, "incremental", (char *)os_bool_str(json_object_get(it, "is_incremental")));
		metric_add("openstack_cinder_backup_gb", lbl, &gb, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_cinder_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_cinder_services_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "services");
	n = arr ? json_array_size(arr) : 0;
	os_family(carg, "openstack_cinder_agent_state");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		int64_t state = strcasecmp(os_obj_str(it, "state"), "up") ? 0 : 1;
		char uuid[OS_STR];
		alligator_ht *lbl = alligator_ht_init(NULL);

		os_obj_jstr(it, "id", uuid, sizeof(uuid));
		if (!uuid[0])
			snprintf(uuid, sizeof(uuid), "%s/%s", os_obj_str(it, "host"), os_obj_str(it, "binary"));
		labels_hash_insert_nocache(lbl, "uuid", uuid);
		labels_hash_insert_nocache(lbl, "hostname", (char *)os_obj_str(it, "host"));
		labels_hash_insert_nocache(lbl, "service", (char *)os_obj_str(it, "binary"));
		labels_hash_insert_nocache(lbl, "adminState", (char *)os_obj_str(it, "status"));
		labels_hash_insert_nocache(lbl, "zone", (char *)os_obj_str(it, "zone"));
		labels_hash_insert_nocache(lbl, "disabledReason", (char *)os_obj_str(it, "disabled_reason"));
		metric_add("openstack_cinder_agent_state", lbl, &state, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_cinder_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_cinder_pools_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "pools");
	n = arr ? json_array_size(arr) : 0;
	os_family(carg, "openstack_cinder_pool_capacity_free_gb");
	os_family(carg, "openstack_cinder_pool_capacity_total_gb");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *cap = json_object_get(it, "capabilities");
		double free_gb = os_obj_f64(cap, "free_capacity_gb");
		double total_gb = os_obj_f64(cap, "total_capacity_gb");
		char *name = (char *)os_obj_str(it, "name");
		char *backend = (char *)os_obj_str(cap, "volume_backend_name");
		char *vendor = (char *)os_obj_str(cap, "vendor_name");

		metric_add_labels3("openstack_cinder_pool_capacity_free_gb", &free_gb, DATATYPE_DOUBLE, carg, "name", name, "volume_backend_name", backend, "vendor_name", vendor);
		metric_add_labels3("openstack_cinder_pool_capacity_total_gb", &total_gb, DATATYPE_DOUBLE, carg, "name", name, "volume_backend_name", backend, "vendor_name", vendor);
	}
	os_up(carg, "openstack_cinder_up");
	json_decref(root);
	carg->parser_status = 1;
}

void openstack_glance_images_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root = os_load(metrics, size, carg);
	json_t *arr;
	size_t i;
	size_t n;

	if (!root)
		return;
	arr = os_array(root, "images");
	n = arr ? json_array_size(arr) : 0;
	os_count(carg, "openstack_glance_images", (int64_t)n);
	os_family(carg, "openstack_glance_image_bytes");
	os_family(carg, "openstack_glance_image_created_at");
	for (i = 0; arr && i < n; i++) {
		json_t *it = json_array_get(arr, i);
		json_t *props = json_object_get(it, "properties");
		const char *itype = os_obj_str(props, "image_type");
		int64_t bytes = os_obj_i64(it, "size");
		int64_t created = os_parse_time(os_obj_str(it, "created_at"));
		alligator_ht *blbl = alligator_ht_init(NULL);
		alligator_ht *clbl = alligator_ht_init(NULL);
		char hidden[16];

		if (!itype || !*itype)
			itype = "image";
		os_obj_jstr(it, "os_hidden", hidden, sizeof(hidden));
		if (!hidden[0])
			strlcpy(hidden, os_bool_str(json_object_get(it, "os_hidden")), sizeof(hidden));
		labels_hash_insert_nocache(blbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(blbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(blbl, "tenant_id", (char *)os_obj_str(it, "owner"));
		labels_hash_insert_nocache(blbl, "image_type", (char *)itype);
		metric_add("openstack_glance_image_bytes", blbl, &bytes, DATATYPE_INT, carg);
		labels_hash_insert_nocache(clbl, "id", (char *)os_obj_str(it, "id"));
		labels_hash_insert_nocache(clbl, "name", (char *)os_obj_str(it, "name"));
		labels_hash_insert_nocache(clbl, "tenant_id", (char *)os_obj_str(it, "owner"));
		labels_hash_insert_nocache(clbl, "visibility", (char *)os_obj_str(it, "visibility"));
		labels_hash_insert_nocache(clbl, "hidden", hidden);
		labels_hash_insert_nocache(clbl, "status", (char *)os_obj_str(it, "status"));
		labels_hash_insert_nocache(clbl, "image_type", (char *)itype);
		metric_add("openstack_glance_image_created_at", clbl, &created, DATATYPE_INT, carg);
	}
	os_up(carg, "openstack_glance_up");
	json_decref(root);
	carg->parser_status = 1;
}

typedef struct os_query {
	const char *path;
	void (*handler)(char *, size_t, context_arg *);
	const char *parser_name;
	const char *hdr_name;
	const char *hdr_value;
} os_query;

static const os_query os_identity_q[] = {
	{ "/domains", openstack_identity_domains_handler, "openstack_identity_domains", NULL, NULL },
	{ "/users", openstack_identity_users_handler, "openstack_identity_users", NULL, NULL },
	{ "/groups", openstack_identity_groups_handler, "openstack_identity_groups", NULL, NULL },
	{ "/projects", openstack_identity_projects_handler, "openstack_identity_projects", NULL, NULL },
	{ "/regions", openstack_identity_regions_handler, "openstack_identity_regions", NULL, NULL },
};

static const os_query os_compute_q[] = {
	{ "/os-hypervisors/detail", openstack_nova_hypervisors_handler, "openstack_nova_hypervisors", "X-OpenStack-Nova-API-Version", "2.87" },
	{ "/servers/detail", openstack_nova_servers_handler, "openstack_nova_servers", "X-OpenStack-Nova-API-Version", "2.87" },
	{ "/flavors/detail", openstack_nova_flavors_handler, "openstack_nova_flavors", "X-OpenStack-Nova-API-Version", "2.87" },
	{ "/os-availability-zone", openstack_nova_az_handler, "openstack_nova_az", "X-OpenStack-Nova-API-Version", "2.87" },
	{ "/os-services", openstack_nova_services_handler, "openstack_nova_services", "X-OpenStack-Nova-API-Version", "2.87" },
	{ "/os-security-groups", openstack_nova_secgroups_handler, "openstack_nova_secgroups", "X-OpenStack-Nova-API-Version", "2.87" },
};

static const os_query os_network_q[] = {
	{ "/v2.0/networks", openstack_neutron_networks_handler, "openstack_neutron_networks", NULL, NULL },
	{ "/v2.0/subnets", openstack_neutron_subnets_handler, "openstack_neutron_subnets", NULL, NULL },
	{ "/v2.0/ports", openstack_neutron_ports_handler, "openstack_neutron_ports", NULL, NULL },
	{ "/v2.0/floatingips", openstack_neutron_floatingips_handler, "openstack_neutron_floatingips", NULL, NULL },
	{ "/v2.0/routers", openstack_neutron_routers_handler, "openstack_neutron_routers", NULL, NULL },
	{ "/v2.0/security-groups", openstack_neutron_secgroups_handler, "openstack_neutron_secgroups", NULL, NULL },
	{ "/v2.0/agents", openstack_neutron_agents_handler, "openstack_neutron_agents", NULL, NULL },
};

static const os_query os_volume_q[] = {
	{ "/volumes/detail", openstack_cinder_volumes_handler, "openstack_cinder_volumes", "OpenStack-API-Version", "volume 3.18" },
	{ "/snapshots/detail", openstack_cinder_snapshots_handler, "openstack_cinder_snapshots", "OpenStack-API-Version", "volume 3.18" },
	{ "/backups/detail", openstack_cinder_backups_handler, "openstack_cinder_backups", "OpenStack-API-Version", "volume 3.18" },
	{ "/os-services", openstack_cinder_services_handler, "openstack_cinder_services", "OpenStack-API-Version", "volume 3.18" },
	{ "/scheduler-stats/get_pools?detail=true", openstack_cinder_pools_handler, "openstack_cinder_pools", "OpenStack-API-Version", "volume 3.18" },
};

static const os_query os_image_q[] = {
	{ "/v2/images", openstack_glance_images_handler, "openstack_glance_images", NULL, NULL },
};

static const char *os_rel_path(const char *base, const char *path)
{
	if (strstr(base, "/v2.0") && !strncmp(path, "/v2.0/", 6))
		return path + 5;
	if (strstr(base, "/v2/") && !strncmp(path, "/v2/", 4))
		return path + 3;
	if ((strstr(base, "/v2") && !strstr(base, "/v2.") && !strncmp(path, "/v2/", 4)))
		return path + 3;
	return path;
}

static void os_spawn_queries(context_arg *carg, const char *base, const char *token,
	const os_query *q, size_t nq)
{
	size_t i;
	char url[OS_URL];

	for (i = 0; i < nq; i++) {
		os_join_url(url, sizeof(url), base, os_rel_path(base, q[i].path));
		os_oneshot(carg, url, token, q[i].handler, q[i].parser_name, q[i].hdr_name, q[i].hdr_value);
	}
}

static void os_spawn_service(context_arg *carg, const char *type, const char *base, const char *token)
{
	const char *norm = os_norm_type(type);

	if (!strcmp(norm, "identity"))
		os_spawn_queries(carg, base, token, os_identity_q, sizeof(os_identity_q) / sizeof(os_identity_q[0]));
	else if (!strcmp(norm, "compute"))
		os_spawn_queries(carg, base, token, os_compute_q, sizeof(os_compute_q) / sizeof(os_compute_q[0]));
	else if (!strcmp(norm, "network"))
		os_spawn_queries(carg, base, token, os_network_q, sizeof(os_network_q) / sizeof(os_network_q[0]));
	else if (!strcmp(norm, "volume"))
		os_spawn_queries(carg, base, token, os_volume_q, sizeof(os_volume_q) / sizeof(os_volume_q[0]));
	else if (!strcmp(norm, "image"))
		os_spawn_queries(carg, base, token, os_image_q, sizeof(os_image_q) / sizeof(os_image_q[0]));
}

static char *os_header_value(const char *headers, const char *name)
{
	const char *p;
	size_t nlen;

	if (!headers || !name)
		return NULL;
	nlen = strlen(name);
	for (p = headers; *p; ) {
		const char *eol = strchr(p, '\n');
		size_t linelen = eol ? (size_t)(eol - p) : strlen(p);

		if (linelen >= nlen && !strncasecmp(p, name, nlen) &&
		    (p[nlen] == ':' || p[nlen] == ' ' || p[nlen] == '\t')) {
			p += nlen;
			p += strspn(p, ": \t");
			return strndup(p, strcspn(p, "\r\n"));
		}
		if (!eol)
			break;
		p = eol + 1;
	}
	return NULL;
}

void openstack_auth_handler(char *metrics, size_t size, context_arg *carg)
{
	http_reply_data *hr = NULL;
	char *body = metrics;
	size_t body_len = size;
	char *token = NULL;
	json_t *root;
	json_t *token_obj;
	json_t *catalog;
	openstack_settings *st = carg ? carg->data : NULL;
	const char *want_iface = st && st->endpoint_type[0] ? st->endpoint_type : "public";
	const char *want_region = st && st->region[0] ? st->region : NULL;
	size_t i;
	size_t n;
	int seen_volume = 0;
	int64_t svc = 0;

	if (!metrics || !size)
		return;
	if (size >= 4 && !strncasecmp(metrics, "HTTP", 4)) {
		hr = http_reply_parser(metrics, (ssize_t)size);
		if (hr) {
			body = hr->body;
			body_len = hr->body_size;
			token = os_header_value(hr->headers, "X-Subject-Token");
		}
	}
	root = os_load(body ? body : metrics, body_len, carg);
	if (!root) {
		if (hr)
			http_reply_data_free(hr);
		free(token);
		return;
	}
	token_obj = json_object_get(root, "token");
	catalog = json_object_get(token_obj, "catalog");
	n = json_is_array(catalog) ? json_array_size(catalog) : 0;
	os_count(carg, "openstack_identity_catalog_services", (int64_t)n);
	os_up(carg, "openstack_identity_up");
	for (i = 0; i < n; i++) {
		json_t *svc_obj = json_array_get(catalog, i);
		const char *type = os_obj_str(svc_obj, "type");
		const char *norm = os_norm_type(type);
		json_t *eps = json_object_get(svc_obj, "endpoints");
		size_t e;
		size_t en = json_is_array(eps) ? json_array_size(eps) : 0;
		const char *url = NULL;

		if (!strcmp(norm, "volume")) {
			if (seen_volume && strcmp(type, "volumev3"))
				continue;
		}
		for (e = 0; e < en; e++) {
			json_t *ep = json_array_get(eps, e);
			const char *iface = os_obj_str(ep, "interface");
			const char *region = os_obj_str(ep, "region");
			const char *region_id = os_obj_str(ep, "region_id");

			if (!os_iface_ok(iface, want_iface))
				continue;
			if (!os_region_ok(region, region_id, want_region))
				continue;
			url = os_obj_str(ep, "url");
			if (url && *url)
				break;
		}
		if (url && *url && token) {
			if (!strcmp(norm, "volume"))
				seen_volume = 1;
			os_spawn_service(carg, type, url, token);
			svc++;
		}
	}
	(void)svc;
	json_decref(root);
	if (hr)
		http_reply_data_free(hr);
	free(token);
	carg->parser_status = 1;
}

static void os_auth_path(host_aggregator_info *hi, char *path, size_t n)
{
	const char *q = hi && hi->query ? hi->query : "";

	if (q && strstr(q, "auth/tokens")) {
		strlcpy(path, q, n);
		return;
	}
	if (q && q[0] && strcmp(q, "/")) {
		size_t ql = strlen(q);

		while (ql && q[ql - 1] == '/')
			ql--;
		snprintf(path, n, "%.*s/auth/tokens", (int)ql, q);
		return;
	}
	strlcpy(path, "/v3/auth/tokens", n);
}

string *openstack_auth_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	aggregate_context *actx = arg;
	openstack_settings *st = actx ? actx->data : NULL;
	const char *user = st && st->username[0] ? st->username : (hi && hi->user ? hi->user : "");
	const char *pass = st && st->password[0] ? st->password : (hi && hi->pass ? hi->pass : "");
	const char *project = st && st->project[0] ? st->project : user;
	const char *udom = st && st->user_domain[0] ? st->user_domain : "Default";
	const char *pdom = st && st->project_domain[0] ? st->project_domain : "Default";
	json_t *root = json_object();
	json_t *auth = json_object();
	json_t *identity = json_object();
	json_t *methods = json_array();
	json_t *password = json_object();
	json_t *user_obj = json_object();
	json_t *ud = json_object();
	json_t *scope = json_object();
	json_t *proj = json_object();
	json_t *pd = json_object();
	char *dump;
	string *body;
	alligator_ht *hdr;
	char path[OS_URL];
	char clen[32];
	char *query;

	json_array_append_new(methods, json_string("password"));
	json_object_set_new(ud, "name", json_string(udom));
	json_object_set_new(user_obj, "name", json_string(user));
	json_object_set_new(user_obj, "domain", ud);
	json_object_set_new(user_obj, "password", json_string(pass));
	json_object_set_new(password, "user", user_obj);
	json_object_set_new(identity, "methods", methods);
	json_object_set_new(identity, "password", password);
	json_object_set_new(pd, "name", json_string(pdom));
	json_object_set_new(proj, "name", json_string(project));
	json_object_set_new(proj, "domain", pd);
	json_object_set_new(scope, "project", proj);
	json_object_set_new(auth, "identity", identity);
	json_object_set_new(auth, "scope", scope);
	json_object_set_new(root, "auth", auth);
	dump = json_dumps(root, JSON_COMPACT);
	json_decref(root);
	if (!dump)
		return NULL;
	body = string_init_add_auto(dump);
	os_auth_path(hi, path, sizeof(path));
	hdr = env_struct_duplicate(env);
	if (!hdr)
		hdr = alligator_ht_init(NULL);
	snprintf(clen, sizeof(clen), "%zu", body->l);
	env_struct_push_alloc(hdr, "Content-Type", "application/json");
	env_struct_push_alloc(hdr, "Content-Length", clen);
	query = gen_http_query(HTTP_POST, path, NULL, hi && hi->host ? hi->host : "localhost",
		"alligator", NULL, "1.1", hdr, proxy_settings, body);
	env_free(hdr);
	string_free(body);
	return string_init_add_auto(query);
}

void openstack_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("openstack");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->data_func = openstack_data_func;

	actx->handler[0].name = openstack_auth_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = openstack_auth_mesg;
	actx->handler[0].headers_pass = 1;
	strlcpy(actx->handler[0].key, "openstack", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
