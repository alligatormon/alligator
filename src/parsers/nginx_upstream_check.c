#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "common/validator.h"
#include "common/http.h"
#include "main.h"
#include "metric/metric_types.h"
#define NGINX_UPSTREAM_CHECK_SIZE 1000

static void nginx_upstream_check_flush_group(context_arg *carg, const char *upstream_name,
	int64_t live, int64_t dead)
{
	if (!upstream_name || !*upstream_name)
		return;
	if (live < 0)
		live = 0;
	if (dead < 0)
		dead = 0;
	uint64_t total = (uint64_t)live + (uint64_t)dead;
	if (!total)
		return;

	double percent_live = (100.0 * (double)live) / (double)total;
	double percent_dead = (100.0 * (double)dead) / (double)total;
	uint64_t live_u = (uint64_t)live;
	uint64_t dead_u = (uint64_t)dead;

	metric_add_labels("nginx_upstream_live", &live_u, DATATYPE_UINT, carg, "upstream", (char *)upstream_name);
	metric_add_labels("nginx_upstream_dead", &dead_u, DATATYPE_UINT, carg, "upstream", (char *)upstream_name);
	metric_add_labels("nginx_upstream_live_percent", &percent_live, DATATYPE_DOUBLE, carg, "upstream", (char *)upstream_name);
	metric_add_labels("nginx_upstream_dead_percent", &percent_dead, DATATYPE_DOUBLE, carg, "upstream", (char *)upstream_name);
}

static void nginx_upstream_check_set_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "nginx_upstream_check_up", METRIC_TYPE_GAUGE,
		"1 if the last nginx upstream check CSV scrape succeeded.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_check_status", METRIC_TYPE_GAUGE,
		"Nginx upstream check status by upstream/server/type.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_live", METRIC_TYPE_GAUGE,
		"Count of live servers in an upstream check group.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_dead", METRIC_TYPE_GAUGE,
		"Count of dead servers in an upstream check group.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_live_percent", METRIC_TYPE_GAUGE,
		"Percent of live servers in an upstream check group.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_dead_percent", METRIC_TYPE_GAUGE,
		"Percent of dead servers in an upstream check group.");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_check_rise_count", METRIC_TYPE_GAUGE,
		"Current consecutive successful checks for an upstream peer (resets on failure).");
	namespace_metric_family_set(NULL, carg, "nginx_upstream_check_fall_count", METRIC_TYPE_GAUGE,
		"Current consecutive failed checks for an upstream peer (resets on success).");
}

void nginx_upstream_check_handler(char *metrics, size_t size, context_arg *carg)
{
	nginx_upstream_check_set_families(carg);

	if (!metrics || !size) {
		uint64_t up = 0;
		metric_add_auto("nginx_upstream_check_up", &up, DATATYPE_UINT, carg);
		carg->parser_status = 0;
		return;
	}

	int64_t cur;
	int64_t i = 0;
	size_t name_size;
	char upstream[NGINX_UPSTREAM_CHECK_SIZE];
	char server[NGINX_UPSTREAM_CHECK_SIZE];
	char status[NGINX_UPSTREAM_CHECK_SIZE];
	char type[NGINX_UPSTREAM_CHECK_SIZE];

	char upstream_counter[NGINX_UPSTREAM_CHECK_SIZE];
	int64_t upstream_counter_live = 0;
	int64_t upstream_counter_dead = 0;
	*upstream_counter = 0;
	int got_row = 0;

	uint64_t rise;
	uint64_t fall;

	while (i < size)
	{
		if (carg->log_level >= L_DEBUG)
		{
			char str[255];
			size_t n = strcspn(metrics+i, "\n");
			if (n >= sizeof(str))
				n = sizeof(str) - 1;
			strlcpy(str, metrics+i, n + 1);
			carglog(carg, L_DEBUG, "nginx processing string: %"d64" < %zu: '%s'\n", i, size, str);
		}
		if (!isdigit((unsigned char)metrics[i]))
		{
			carglog(carg, L_WARN, "nginx scrape metrics: field 'id' not a number into stats:\n'%s'\n", metrics+i);
			break;
		}
		cur = strcspn(metrics+i, ",");
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(upstream, metrics+i, name_size);
		size_t upstream_len = name_size-1;
		if (!*upstream)
		{
			carglog(carg, L_WARN, "nginx scrape metrics: empty field 'upstream' into stats:\n'%s'\n", metrics+i);
			break;
		}
		if (!metric_label_validator(upstream, cur))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (*upstream_counter == 0)
			strlcpy(upstream_counter, upstream, upstream_len+1);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(server, metrics+i, name_size);
		size_t server_len = name_size-1;
		if (!*server)
		{
			carglog(carg, L_WARN, "nginx scrape metrics: empty field 'server' into stats:\n'%s'\n", metrics+i);
			break;
		}
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(status, metrics+i, name_size);
		size_t status_len = name_size-1;
		cur++;
		i+=cur;

		if (!isdigit((unsigned char)metrics[i]))
		{
			carglog(carg, L_WARN, "nginx scrape metrics: field 'rise' not a number into stats:\n'%s'\n", metrics+i);
			break;
		}
		cur = strcspn(metrics+i, ",");
		rise = atoll(metrics+i);
		cur++;
		i+=cur;

		if (!isdigit((unsigned char)metrics[i]))
		{
			carglog(carg, L_WARN, "nginx scrape metrics: field 'fall' not a number into stats:\n'%s'\n", metrics+i);
			break;
		}
		cur = strcspn(metrics+i, ",");
		fall = atoll(metrics+i);
		cur++;
		i+=cur;

		cur = strcspn(metrics+i, ",");
		name_size = NGINX_UPSTREAM_CHECK_SIZE > cur+1 ? cur+1 : NGINX_UPSTREAM_CHECK_SIZE;
		strlcpy(type, metrics+i, name_size);
		size_t type_len = name_size-1;
		cur++;
		i+=cur;

		carglog(carg, L_TRACE, "nginx_upstream_check: server='%s' upstream='%s' type='%s' status='%s'\n",
			server, upstream, type, status);
		if (!*server)
			continue;
		if (!*upstream)
			continue;

		if (!metric_label_validator(type, type_len))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (!metric_label_validator(status, status_len))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (!metric_label_validator(server, server_len))
		{
			carglog(carg, L_WARN, "nginx_upstream_check: server label invalid, skip line\n");
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}
		if (!metric_label_validator(upstream, upstream_len))
		{
			i += strcspn(metrics+i, "\n")+1;
			continue;
		}

		uint64_t val = 1;
		uint64_t nval = 0;
		if (!strncmp(status, "up", 2))
		{
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "status", "down", "type", type);
			if (!strcmp(upstream_counter, upstream))
			{
				++upstream_counter_live;
			}
			else
			{
				nginx_upstream_check_flush_group(carg, upstream_counter, upstream_counter_live, upstream_counter_dead);
				upstream_counter_live = 1;
				upstream_counter_dead = 0;
				strlcpy(upstream_counter, upstream, upstream_len+1);
			}
			got_row = 1;
		}
		else if (!strncmp(status, "down", 4))
		{
			metric_add_labels4("nginx_upstream_check_status", &nval, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "status", "up", "type", type);
			metric_add_labels4("nginx_upstream_check_status", &val, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "status", "down", "type", type);
			if (!strcmp(upstream_counter, upstream))
			{
				++upstream_counter_dead;
			}
			else
			{
				nginx_upstream_check_flush_group(carg, upstream_counter, upstream_counter_live, upstream_counter_dead);
				upstream_counter_live = 0;
				upstream_counter_dead = 1;
				strlcpy(upstream_counter, upstream, upstream_len+1);
			}
			got_row = 1;
		}
		metric_add_labels3("nginx_upstream_check_rise_count", &rise, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "type", type);
		metric_add_labels3("nginx_upstream_check_fall_count", &fall, DATATYPE_UINT, carg, "upstream", upstream, "server", server, "type", type);

		i += strcspn(metrics+i, "\n");
		i += strspn(metrics+i, "\n");
	}

	nginx_upstream_check_flush_group(carg, upstream_counter, upstream_counter_live, upstream_counter_dead);

	uint64_t up = got_row ? 1 : 0;
	metric_add_auto("nginx_upstream_check_up", &up, DATATYPE_UINT, carg);
	carg->parser_status = got_row ? 1 : 0;
}

string* nginx_upstream_check_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, "?format=csv", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

void nginx_upstream_check_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("nginx_upstream_check");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = nginx_upstream_check_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = nginx_upstream_check_mesg;
	strlcpy(actx->handler[0].key,"nginx_upstream_check", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
