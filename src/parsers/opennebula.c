#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/selector.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/url.h"
#include "main.h"

#define OPENNEBULA_VAL_SIZE 256
#define OPENNEBULA_TAG_SIZE 64

static void opennebula_host_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_host_status", METRIC_TYPE_GAUGE,
		"OpenNebula host state (0=INIT, 1=MONITORING_MONITORED, 2=MONITORED, 3=ERROR, 4=DISABLED).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_cpu_total", METRIC_TYPE_GAUGE,
		"OpenNebula host total CPU (100 per core).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_cpu_max", METRIC_TYPE_GAUGE,
		"OpenNebula host maximum allocatable CPU (100 per core).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_cpu_used", METRIC_TYPE_GAUGE,
		"OpenNebula host allocated CPU (100 per core).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_host_free_cpu", METRIC_TYPE_GAUGE,
		"OpenNebula host free CPU (100 per core).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_memory_total", METRIC_TYPE_GAUGE,
		"OpenNebula host total memory.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_memory_allocated", METRIC_TYPE_GAUGE,
		"OpenNebula host allocated memory.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_memory_max", METRIC_TYPE_GAUGE,
		"OpenNebula host maximum allocatable memory.");
}

static void opennebula_vm_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_vm_status", METRIC_TYPE_GAUGE,
		"OpenNebula VM state (0=INIT, 1=PENDING, 2=HOLD, 3=ACTIVE, 4=STOPPED, 5=SUSPENDED, 6=DONE, 8=POWEROFF, 9=UNDEPLOYED, 10=CLONING, 11=CLONING_FAILURE).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_vm_cpu", METRIC_TYPE_GAUGE,
		"OpenNebula VM allocated CPU from the VM template.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_vm_memory", METRIC_TYPE_GAUGE,
		"OpenNebula VM allocated memory from the VM template.");
}

static void opennebula_vnet_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_vnet_total", METRIC_TYPE_GAUGE,
		"Total IP addresses in OpenNebula virtual network address ranges.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_vnet_leases", METRIC_TYPE_GAUGE,
		"Leased IP addresses in an OpenNebula virtual network.");
}

static void opennebula_ds_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_datastore_status", METRIC_TYPE_GAUGE,
		"OpenNebula datastore state (0=ON, 1=OFF).");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_datastore_total", METRIC_TYPE_GAUGE,
		"OpenNebula datastore total capacity in MB.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_datastore_free", METRIC_TYPE_GAUGE,
		"OpenNebula datastore free space in MB.");
	namespace_metric_family_set(NULL, carg, "opennebula_monitoring_datastore_used", METRIC_TYPE_GAUGE,
		"OpenNebula datastore used space in MB.");
}

static const char *opennebula_find(const char *hay, const char *hay_end, const char *needle)
{
	size_t nlen;
	size_t hlen;
	size_t i;

	if (!hay || !needle || hay >= hay_end)
		return NULL;
	nlen = strlen(needle);
	if (!nlen)
		return NULL;
	hlen = (size_t)(hay_end - hay);
	if (hlen < nlen)
		return NULL;
	for (i = 0; i <= hlen - nlen; i++) {
		if (hay[i] == needle[0] && !memcmp(hay + i, needle, nlen))
			return hay + i;
	}
	return NULL;
}

static int opennebula_tag_follows(const char *after, const char *end)
{
	if (!after || after >= end)
		return 0;
	return *after == '>' || *after == ' ' || *after == '\t' || *after == '\n' || *after == '\r';
}

static const char *opennebula_elem(const char *p, const char *end, const char *tag, const char **close_at)
{
	char open[OPENNEBULA_TAG_SIZE];
	char close[OPENNEBULA_TAG_SIZE];
	size_t tlen = strlen(tag);
	const char *hit;
	const char *gt;
	const char *c;

	if (tlen + 4 >= sizeof(open))
		return NULL;
	open[0] = '<';
	memcpy(open + 1, tag, tlen);
	open[1 + tlen] = 0;
	snprintf(close, sizeof(close), "</%s>", tag);

	hit = p;
	while ((hit = opennebula_find(hit, end, open))) {
		const char *after = hit + 1 + tlen;
		if (!opennebula_tag_follows(after, end)) {
			hit = after;
			continue;
		}
		gt = opennebula_find(after, end, ">");
		if (!gt)
			return NULL;
		c = opennebula_find(gt + 1, end, close);
		if (!c)
			return NULL;
		if (close_at)
			*close_at = c + strlen(close);
		return gt + 1;
	}
	return NULL;
}

static void opennebula_trim_copy(const char *s, const char *e, char *out, size_t outsz)
{
	size_t n;

	if (!out || !outsz)
		return;
	out[0] = 0;
	if (!s || !e || s >= e)
		return;
	while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		e--;
	if ((size_t)(e - s) >= 9 && !memcmp(s, "<![CDATA[", 9)) {
		s += 9;
		if ((size_t)(e - s) >= 3 && !memcmp(e - 3, "]]>", 3))
			e -= 3;
		while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
			s++;
		while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
			e--;
	}
	n = (size_t)(e - s);
	if (n >= outsz)
		n = outsz - 1;
	memcpy(out, s, n);
	out[n] = 0;
}

static int opennebula_tag_text(const char *start, const char *end, const char *tag, char *out, size_t outsz)
{
	const char *close_at = NULL;
	const char *content;
	char close[OPENNEBULA_TAG_SIZE];

	content = opennebula_elem(start, end, tag, &close_at);
	if (!content || !close_at)
		return 0;
	snprintf(close, sizeof(close), "</%s>", tag);
	opennebula_trim_copy(content, close_at - strlen(close), out, outsz);
	return out[0] ? 1 : 0;
}

static int64_t opennebula_tag_i64(const char *start, const char *end, const char *tag, int *found)
{
	char buf[OPENNEBULA_VAL_SIZE];

	if (found)
		*found = 0;
	if (!opennebula_tag_text(start, end, tag, buf, sizeof(buf)))
		return 0;
	if (found)
		*found = 1;
	return atoll(buf);
}

static double opennebula_tag_f64(const char *start, const char *end, const char *tag, int *found)
{
	char buf[OPENNEBULA_VAL_SIZE];

	if (found)
		*found = 0;
	if (!opennebula_tag_text(start, end, tag, buf, sizeof(buf)))
		return 0;
	if (found)
		*found = 1;
	return atof(buf);
}

static void opennebula_unescape(char *s)
{
	char *r = s;
	char *w = s;

	while (*r) {
		if (r[0] == '&') {
			if (!strncmp(r, "&lt;", 4)) {
				*w++ = '<';
				r += 4;
			} else if (!strncmp(r, "&gt;", 4)) {
				*w++ = '>';
				r += 4;
			} else if (!strncmp(r, "&amp;", 5)) {
				*w++ = '&';
				r += 5;
			} else if (!strncmp(r, "&quot;", 6)) {
				*w++ = '"';
				r += 6;
			} else if (!strncmp(r, "&apos;", 6)) {
				*w++ = '\'';
				r += 6;
			} else {
				*w++ = *r++;
			}
		} else {
			*w++ = *r++;
		}
	}
	*w = 0;
}

static int opennebula_rpc_failed(const char *xml)
{
	if (!xml)
		return 1;
	if (strstr(xml, "<fault>"))
		return 1;
	if (strstr(xml, "<boolean>0</boolean>"))
		return 1;
	return 0;
}

static const char *opennebula_pool(const char *xml, const char *end, const char *pool_tag, const char **pool_end)
{
	const char *close_at = NULL;
	const char *content = opennebula_elem(xml, end, pool_tag, &close_at);

	if (!content)
		return NULL;
	if (pool_end)
		*pool_end = close_at;
	return content;
}

static char *opennebula_prepare(char *metrics, size_t size, size_t *out_len)
{
	char *copy;

	if (!metrics || !size)
		return NULL;
	copy = strndup(metrics, size);
	if (!copy)
		return NULL;
	opennebula_unescape(copy);
	if (out_len)
		*out_len = strlen(copy);
	return copy;
}

static void opennebula_xml_escape_cat(string *s, const char *in)
{
	for (; in && *in; in++) {
		switch (*in) {
		case '&':
			string_cat(s, "&amp;", 5);
			break;
		case '<':
			string_cat(s, "&lt;", 4);
			break;
		case '>':
			string_cat(s, "&gt;", 4);
			break;
		case '"':
			string_cat(s, "&quot;", 6);
			break;
		default:
			string_cat(s, (char *)in, 1);
			break;
		}
	}
}

static void opennebula_session_cat(string *s, host_aggregator_info *hi)
{
	const char *user = (hi && hi->user) ? hi->user : "";
	const char *pass = (hi && hi->pass) ? hi->pass : "";

	opennebula_xml_escape_cat(s, user);
	string_cat(s, ":", 1);
	opennebula_xml_escape_cat(s, pass);
}

static string *opennebula_rpc_body(host_aggregator_info *hi, const char *method, const int *extra, int nextra)
{
	string *s = string_init(512);
	int i;

	string_sprintf(s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><methodCall><methodName>%s</methodName><params><param><value><string>", method);
	opennebula_session_cat(s, hi);
	string_cat(s, "</string></value></param>", 25);
	for (i = 0; i < nextra; i++)
		string_sprintf(s, "<param><value><i4>%d</i4></value></param>", extra[i]);
	string_cat(s, "</params></methodCall>", 22);
	return s;
}

static string *opennebula_rpc_mesg(host_aggregator_info *hi, void *env, void *proxy_settings,
	const char *method, const int *extra, int nextra)
{
	string *body;
	string *req;
	alligator_ht *hdrs;
	char cl[32];
	const char *append = "/RPC2";
	char *generated;

	if (!hi)
		return NULL;
	if (hi->proto == APROTO_FILE || hi->proto == APROTO_PROCESS)
		return NULL;

	if (hi->query && hi->query[0] && strcmp(hi->query, "/"))
		append = NULL;

	body = opennebula_rpc_body(hi, method, extra, nextra);
	hdrs = env_struct_duplicate(env);
	if (!hdrs)
		hdrs = alligator_ht_init(NULL);
	env_struct_push_alloc(hdrs, "Content-Type", "text/xml");
	snprintf(cl, sizeof(cl), "%zu", body->l);
	env_struct_push_alloc(hdrs, "Content-Length", cl);
	generated = gen_http_query(HTTP_POST, hi->query, (char *)append, hi->host, "alligator",
		hi->auth, "1.0", hdrs, proxy_settings, body);
	string_free(body);
	env_free(hdrs);
	if (!generated)
		return NULL;
	req = string_init_add_auto(generated);
	return req;
}

void opennebula_hosts_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	size_t copylen = 0;
	const char *end;
	const char *pool_end = NULL;
	const char *pool;
	const char *host;
	const char *host_end;
	int found = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	opennebula_host_families(carg);
	copy = opennebula_prepare(metrics, size, &copylen);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	end = copy + copylen;
	pool = opennebula_pool(copy, end, "HOST_POOL", &pool_end);
	if (!pool) {
		carglog(carg, L_ERROR, "opennebula hosts: no HOST_POOL in response (rpc_failed=%d)\n",
			opennebula_rpc_failed(copy));
		free(copy);
		carg->parser_status = 0;
		return;
	}

	host = pool;
	while ((host = opennebula_elem(host, pool_end, "HOST", &host_end))) {
		char name[OPENNEBULA_VAL_SIZE];
		char cluster[OPENNEBULA_VAL_SIZE];
		const char *share;
		const char *share_end = NULL;
		int64_t state, total_cpu, max_cpu, cpu_used, free_cpu;
		int64_t total_mem, mem_alloc, max_mem;

		if (!opennebula_tag_text(host, host_end, "NAME", name, sizeof(name))) {
			host = host_end;
			continue;
		}
		cluster[0] = 0;
		opennebula_tag_text(host, host_end, "CLUSTER", cluster, sizeof(cluster));
		if (!cluster[0])
			opennebula_tag_text(host, host_end, "CLUSTER_ID", cluster, sizeof(cluster));

		state = opennebula_tag_i64(host, host_end, "STATE", NULL);
		share = opennebula_elem(host, host_end, "HOST_SHARE", &share_end);
		if (!share)
			share = host, share_end = host_end;

		total_cpu = opennebula_tag_i64(share, share_end, "TOTAL_CPU", NULL);
		max_cpu = opennebula_tag_i64(share, share_end, "MAX_CPU", NULL);
		cpu_used = opennebula_tag_i64(share, share_end, "CPU_USAGE", NULL);
		free_cpu = opennebula_tag_i64(share, share_end, "FREE_CPU", NULL);
		total_mem = opennebula_tag_i64(share, share_end, "TOTAL_MEM", NULL);
		mem_alloc = opennebula_tag_i64(share, share_end, "MEM_USAGE", NULL);
		max_mem = opennebula_tag_i64(share, share_end, "MAX_MEM", NULL);

		metric_add_labels2("opennebula_monitoring_host_status", &state, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_cpu_total", &total_cpu, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_cpu_max", &max_cpu, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_cpu_used", &cpu_used, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_host_free_cpu", &free_cpu, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_memory_total", &total_mem, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_memory_allocated", &mem_alloc, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		metric_add_labels2("opennebula_monitoring_memory_max", &max_mem, DATATYPE_INT, carg,
			"hostname", name, "cluster", cluster);
		found = 1;
		host = host_end;
	}

	free(copy);
	carg->parser_status = 1;
	carglog(carg, L_DEBUG, "opennebula_hosts_handler: status=%d hosts=%d\n", carg->parser_status, found);
}

void opennebula_vms_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	size_t copylen = 0;
	const char *end;
	const char *pool_end = NULL;
	const char *pool;
	const char *vm;
	const char *vm_end;
	int found = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	opennebula_vm_families(carg);
	copy = opennebula_prepare(metrics, size, &copylen);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	end = copy + copylen;
	pool = opennebula_pool(copy, end, "VM_POOL", &pool_end);
	if (!pool) {
		carglog(carg, L_ERROR, "opennebula vms: no VM_POOL in response (rpc_failed=%d)\n",
			opennebula_rpc_failed(copy));
		free(copy);
		carg->parser_status = 0;
		return;
	}

	vm = pool;
	while ((vm = opennebula_elem(vm, pool_end, "VM", &vm_end))) {
		char name[OPENNEBULA_VAL_SIZE];
		const char *tmpl;
		const char *tmpl_end = NULL;
		int64_t state, memory;
		double cpu;

		if (!opennebula_tag_text(vm, vm_end, "NAME", name, sizeof(name))) {
			vm = vm_end;
			continue;
		}
		state = opennebula_tag_i64(vm, vm_end, "STATE", NULL);
		tmpl = opennebula_elem(vm, vm_end, "TEMPLATE", &tmpl_end);
		if (!tmpl)
			tmpl = vm, tmpl_end = vm_end;
		cpu = opennebula_tag_f64(tmpl, tmpl_end, "CPU", NULL);
		memory = opennebula_tag_i64(tmpl, tmpl_end, "MEMORY", NULL);

		metric_add_labels("opennebula_monitoring_vm_status", &state, DATATYPE_INT, carg, "hostname", name);
		metric_add_labels("opennebula_monitoring_vm_cpu", &cpu, DATATYPE_DOUBLE, carg, "hostname", name);
		metric_add_labels("opennebula_monitoring_vm_memory", &memory, DATATYPE_INT, carg, "hostname", name);
		found = 1;
		vm = vm_end;
	}

	free(copy);
	carg->parser_status = 1;
	carglog(carg, L_DEBUG, "opennebula_vms_handler: status=%d vms=%d\n", carg->parser_status, found);
}

void opennebula_vnets_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	size_t copylen = 0;
	const char *end;
	const char *pool_end = NULL;
	const char *pool;
	const char *vnet;
	const char *vnet_end;
	int found = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	opennebula_vnet_families(carg);
	copy = opennebula_prepare(metrics, size, &copylen);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	end = copy + copylen;
	pool = opennebula_pool(copy, end, "VNET_POOL", &pool_end);
	if (!pool) {
		carglog(carg, L_ERROR, "opennebula vnets: no VNET_POOL in response (rpc_failed=%d)\n",
			opennebula_rpc_failed(copy));
		free(copy);
		carg->parser_status = 0;
		return;
	}

	vnet = pool;
	while ((vnet = opennebula_elem(vnet, pool_end, "VNET", &vnet_end))) {
		char name[OPENNEBULA_VAL_SIZE];
		const char *ar;
		const char *ar_end;
		int64_t leases, total = 0;

		if (!opennebula_tag_text(vnet, vnet_end, "NAME", name, sizeof(name))) {
			vnet = vnet_end;
			continue;
		}
		leases = opennebula_tag_i64(vnet, vnet_end, "USED_LEASES", NULL);
		ar = vnet;
		while ((ar = opennebula_elem(ar, vnet_end, "AR", &ar_end))) {
			total += opennebula_tag_i64(ar, ar_end, "SIZE", NULL);
			ar = ar_end;
		}
		metric_add_labels("opennebula_monitoring_vnet_total", &total, DATATYPE_INT, carg, "vnet", name);
		metric_add_labels("opennebula_monitoring_vnet_leases", &leases, DATATYPE_INT, carg, "vnet", name);
		found = 1;
		vnet = vnet_end;
	}

	free(copy);
	carg->parser_status = 1;
	carglog(carg, L_DEBUG, "opennebula_vnets_handler: status=%d vnets=%d\n", carg->parser_status, found);
}

void opennebula_datastores_handler(char *metrics, size_t size, context_arg *carg)
{
	char *copy;
	size_t copylen = 0;
	const char *end;
	const char *pool_end = NULL;
	const char *pool;
	const char *ds;
	const char *ds_end;
	int found = 0;

	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	opennebula_ds_families(carg);
	copy = opennebula_prepare(metrics, size, &copylen);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	end = copy + copylen;
	pool = opennebula_pool(copy, end, "DATASTORE_POOL", &pool_end);
	if (!pool) {
		carglog(carg, L_ERROR, "opennebula datastores: no DATASTORE_POOL in response (rpc_failed=%d)\n",
			opennebula_rpc_failed(copy));
		free(copy);
		carg->parser_status = 0;
		return;
	}

	ds = pool;
	while ((ds = opennebula_elem(ds, pool_end, "DATASTORE", &ds_end))) {
		char name[OPENNEBULA_VAL_SIZE];
		int64_t state, total, free_mb, used;

		if (!opennebula_tag_text(ds, ds_end, "NAME", name, sizeof(name))) {
			ds = ds_end;
			continue;
		}
		state = opennebula_tag_i64(ds, ds_end, "STATE", NULL);
		total = opennebula_tag_i64(ds, ds_end, "TOTAL_MB", NULL);
		free_mb = opennebula_tag_i64(ds, ds_end, "FREE_MB", NULL);
		used = opennebula_tag_i64(ds, ds_end, "USED_MB", NULL);
		metric_add_labels("opennebula_monitoring_datastore_status", &state, DATATYPE_INT, carg, "hostname", name);
		metric_add_labels("opennebula_monitoring_datastore_total", &total, DATATYPE_INT, carg, "hostname", name);
		metric_add_labels("opennebula_monitoring_datastore_free", &free_mb, DATATYPE_INT, carg, "hostname", name);
		metric_add_labels("opennebula_monitoring_datastore_used", &used, DATATYPE_INT, carg, "hostname", name);
		found = 1;
		ds = ds_end;
	}

	free(copy);
	carg->parser_status = 1;
	carglog(carg, L_DEBUG, "opennebula_datastores_handler: status=%d datastores=%d\n", carg->parser_status, found);
}

string *opennebula_hosts_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	return opennebula_rpc_mesg(hi, env, proxy_settings, "one.hostpool.info", NULL, 0);
}

string *opennebula_vms_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	static const int extra[] = { -2, -1, -1, -1 };

	(void)arg;
	return opennebula_rpc_mesg(hi, env, proxy_settings, "one.vmpool.info", extra, 4);
}

string *opennebula_vnets_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	static const int extra[] = { -2, -1, -1 };

	(void)arg;
	return opennebula_rpc_mesg(hi, env, proxy_settings, "one.vnpool.info", extra, 3);
}

string *opennebula_datastores_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	return opennebula_rpc_mesg(hi, env, proxy_settings, "one.datastorepool.info", NULL, 0);
}

void opennebula_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("opennebula");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = opennebula_hosts_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = opennebula_hosts_mesg;
	strlcpy(actx->handler[0].key, "opennebula_hosts", 255);

	actx->handler[1].name = opennebula_vms_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = opennebula_vms_mesg;
	strlcpy(actx->handler[1].key, "opennebula_vms", 255);

	actx->handler[2].name = opennebula_vnets_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = opennebula_vnets_mesg;
	strlcpy(actx->handler[2].key, "opennebula_vnets", 255);

	actx->handler[3].name = opennebula_datastores_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = opennebula_datastores_mesg;
	strlcpy(actx->handler[3].key, "opennebula_datastores", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
