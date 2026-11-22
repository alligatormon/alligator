#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "common/selector.h"
#include "common/validator.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#include "metric/metric_types.h"
#include "common/logs.h"
#define HAPROXY_NAME_SIZE 1000

typedef struct haproxy_stat_col {
	char *csv;
	char *metric;
	uint8_t type; /* METRIC_TYPE_* */
	uint8_t kind; /* 0=int gauge/counter, 1=status info, 2=check_status info, 3=http code, 4=ms→seconds */
	char *code; /* for kind==3 */
} haproxy_stat_col;

static const haproxy_stat_col haproxy_stat_cols[] = {
	{ "qcur", "haproxy_current_queue", METRIC_TYPE_GAUGE, 0, NULL },
	{ "qmax", "haproxy_max_queue", METRIC_TYPE_GAUGE, 0, NULL },
	{ "scur", "haproxy_current_sessions", METRIC_TYPE_GAUGE, 0, NULL },
	{ "smax", "haproxy_max_sessions", METRIC_TYPE_GAUGE, 0, NULL },
	{ "slim", "haproxy_limit_sessions", METRIC_TYPE_GAUGE, 0, NULL },
	{ "stot", "haproxy_sessions_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "bin", "haproxy_bytes_in_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "bout", "haproxy_bytes_out_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "dreq", "haproxy_denied_requests_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "dresp", "haproxy_denied_responses_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "ereq", "haproxy_request_errors_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "econ", "haproxy_connection_errors_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "eresp", "haproxy_response_errors_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "wretr", "haproxy_retries_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "wredis", "haproxy_redispatches_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "status", "haproxy_server_status", METRIC_TYPE_GAUGE, 1, NULL },
	{ "weight", "haproxy_weight", METRIC_TYPE_GAUGE, 0, NULL },
	{ "act", "haproxy_active", METRIC_TYPE_GAUGE, 0, NULL },
	{ "bck", "haproxy_backup", METRIC_TYPE_GAUGE, 0, NULL },
	{ "chkfail", "haproxy_check_failures_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "chkdown", "haproxy_check_downs_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "lastchg", "haproxy_last_change_seconds", METRIC_TYPE_GAUGE, 0, NULL },
	{ "downtime", "haproxy_downtime_seconds_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "qlimit", "haproxy_limit_queue", METRIC_TYPE_GAUGE, 0, NULL },
	{ "pid", "haproxy_pid", METRIC_TYPE_GAUGE, 0, NULL },
	{ "iid", "haproxy_iid", METRIC_TYPE_GAUGE, 0, NULL },
	{ "sid", "haproxy_sid", METRIC_TYPE_GAUGE, 0, NULL },
	{ "throttle", "haproxy_throttle", METRIC_TYPE_GAUGE, 0, NULL },
	{ "lbtot", "haproxy_load_balancing_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "tracked", "haproxy_tracked", METRIC_TYPE_GAUGE, 0, NULL },
	{ "type", "haproxy_type", METRIC_TYPE_GAUGE, 0, NULL },
	{ "rate", "haproxy_session_rate", METRIC_TYPE_GAUGE, 0, NULL },
	{ "rate_lim", "haproxy_session_rate_limit", METRIC_TYPE_GAUGE, 0, NULL },
	{ "rate_max", "haproxy_session_rate_max", METRIC_TYPE_GAUGE, 0, NULL },
	{ "check_status", "haproxy_check_status", METRIC_TYPE_GAUGE, 2, NULL },
	{ "check_code", "haproxy_check_code", METRIC_TYPE_GAUGE, 0, NULL },
	{ "check_duration", "haproxy_check_duration_seconds", METRIC_TYPE_GAUGE, 4, NULL },
	{ "hrsp_1xx", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "1xx" },
	{ "hrsp_2xx", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "2xx" },
	{ "hrsp_3xx", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "3xx" },
	{ "hrsp_4xx", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "4xx" },
	{ "hrsp_5xx", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "5xx" },
	{ "hrsp_other", "haproxy_http_responses_total", METRIC_TYPE_COUNTER, 3, "other" },
	{ "hanafail", "haproxy_health_check_failures_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "req_rate", "haproxy_http_request_rate", METRIC_TYPE_GAUGE, 0, NULL },
	{ "req_rate_max", "haproxy_http_request_rate_max", METRIC_TYPE_GAUGE, 0, NULL },
	{ "req_tot", "haproxy_http_requests_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "cli_abrt", "haproxy_client_aborts_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "srv_abrt", "haproxy_server_aborts_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "comp_in", "haproxy_compressor_bytes_in_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "comp_out", "haproxy_compressor_bytes_out_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "comp_byp", "haproxy_compressor_bytes_bypassed_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "comp_rsp", "haproxy_compressor_responses_total", METRIC_TYPE_COUNTER, 0, NULL },
	{ "lastsess", "haproxy_last_session_seconds", METRIC_TYPE_GAUGE, 0, NULL },
	{ "last_chk", NULL, METRIC_TYPE_GAUGE, 0, NULL }, /* string field, skip numeric */
	{ "last_agt", NULL, METRIC_TYPE_GAUGE, 0, NULL },
	{ "qtime", "haproxy_queue_time_average_seconds", METRIC_TYPE_GAUGE, 4, NULL },
	{ "ctime", "haproxy_connect_time_average_seconds", METRIC_TYPE_GAUGE, 4, NULL },
	{ "rtime", "haproxy_response_time_average_seconds", METRIC_TYPE_GAUGE, 4, NULL },
	{ "ttime", "haproxy_total_time_average_seconds", METRIC_TYPE_GAUGE, 4, NULL },
};

void haproxy_info_handler(char *metrics, size_t size, context_arg *carg)
{
	/* show info → haproxy_<lowercase_key> */
	if (!metrics)
		return;

	char *tmp = metrics;
	uint64_t next;
	char metric_name[255];
	char *prev = NULL;
	uint64_t i = 1000;

	for (; (tmp - metrics) < (int64_t)size && i--; tmp += next)
	{
		if (prev == tmp)
			break;

		tmp += strspn(tmp, "\n");
		next = strcspn(tmp, ":");
		if (!next)
			break;

		size_t nlen = next < sizeof(metric_name) - 9 ? next : sizeof(metric_name) - 9;
		memcpy(metric_name, "haproxy_", 8);
		memcpy(metric_name + 8, tmp, nlen);
		metric_name[8 + nlen] = 0;
		for (size_t k = 0; k < nlen; k++)
			metric_name[8 + k] = (char)tolower((unsigned char)metric_name[8 + k]);
		metric_name_normalizer(metric_name + 8, nlen);

		tmp += next;
		tmp += strspn(tmp, ": ");
		next = strcspn(tmp, "\n");

		int rc = metric_value_validator(tmp, next);
		if (rc == DATATYPE_INT)
		{
			int64_t val = strtoll(tmp, NULL, 10);
			namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "HAProxy show info field.");
			metric_add_auto(metric_name, &val, rc, carg);
		}
		else if (rc == DATATYPE_UINT)
		{
			uint64_t val = strtoull(tmp, NULL, 10);
			namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "HAProxy show info field.");
			metric_add_auto(metric_name, &val, rc, carg);
		}
		else if (rc == DATATYPE_DOUBLE)
		{
			double val = strtod(tmp, NULL);
			namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "HAProxy show info field.");
			metric_add_auto(metric_name, &val, rc, carg);
		}
		prev = tmp;
	}
	carg->parser_status = 1;
}

void haproxy_pools_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "haproxy_pool_allocated", METRIC_TYPE_GAUGE, "Allocated entries in HAProxy memory pool.");
	namespace_metric_family_set(NULL, carg, "haproxy_pool_allocated_bytes", METRIC_TYPE_GAUGE, "Bytes allocated in HAProxy memory pool.");
	namespace_metric_family_set(NULL, carg, "haproxy_pool_used", METRIC_TYPE_GAUGE, "Used entries in HAProxy memory pool.");
	namespace_metric_family_set(NULL, carg, "haproxy_pool_users", METRIC_TYPE_GAUGE, "Pool users count in HAProxy memory pool.");
	namespace_metric_family_set(NULL, carg, "haproxy_pools_allocated", METRIC_TYPE_GAUGE, "Total allocated entries in HAProxy memory pools.");
	namespace_metric_family_set(NULL, carg, "haproxy_pools_allocated_bytes", METRIC_TYPE_GAUGE, "Total allocated bytes in HAProxy memory pools.");
	namespace_metric_family_set(NULL, carg, "haproxy_pools_used", METRIC_TYPE_GAUGE, "Total used entries in HAProxy memory pools.");

	char name[HAPROXY_NAME_SIZE];
	size_t name_size;
	int64_t i;
	int64_t allocated;
	int64_t bytes;
	int64_t used;
	int64_t users;
	int to;
	size_t sz;
	uint64_t cursor;

	to = strcspn(metrics, "\n");
	char *tmp = metrics + to;

	char* total = strstr(metrics, "\nTotal");
	if (!total) {
		carglog(carg, L_WARN, "haproxy pools parse: missing Total section in payload\n");
		return;
	}

	for(i=0; i<size && tmp < total; ++i)
	{
		cursor = 0;
		tmp+=10;
		to = strcspn(tmp, "(");
		name_size = HAPROXY_NAME_SIZE > (size_t)to ? (size_t)to : (HAPROXY_NAME_SIZE - 1);
		strlcpy(name, tmp, name_size + 1);

		to = strcspn(tmp, ":");
		tmp += to;
		int64_t sz_tmp = tmp - metrics;
		if (sz_tmp > size)
			break;
		sz = size - sz_tmp;

		allocated = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_allocated", &allocated, DATATYPE_INT, carg, "pool", name);

		bytes = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_allocated_bytes", &bytes, DATATYPE_INT, carg, "pool", name);

		used = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_used", &used, DATATYPE_INT, carg, "pool", name);

		users = int_get_next(tmp, sz, ' ', &cursor);
		metric_add_labels("haproxy_pool_users", &users, DATATYPE_INT, carg, "pool", name);

		tmp += cursor;
		to = strcspn(tmp, "\n");
		tmp += to;
	}

	cursor = 0;
	sz = size - (size_t)(total - metrics);

	allocated = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pools_allocated", &allocated, DATATYPE_INT, carg);

	bytes = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pools_allocated_bytes", &bytes, DATATYPE_INT, carg);

	used = int_get_next(total, sz, ' ', &cursor);
	metric_add_auto("haproxy_pools_used", &used, DATATYPE_INT, carg);
	carg->parser_status = 1;
}

void haproxy_stat_handler(char *metrics, size_t size, context_arg *carg)
{
	char proxy[HAPROXY_NAME_SIZE];
	size_t proxy_size;

	char server[HAPROXY_NAME_SIZE];
	size_t server_size;

	int64_t i;
	int64_t j;
	int64_t to;
	int64_t tmpval;
	size_t ncols = sizeof(haproxy_stat_cols) / sizeof(haproxy_stat_cols[0]);

	for (i = strcspn(metrics, "\n") + 1; i < size; ++i)
	{
		to = strcspn(metrics + i, ",") + 1;
		proxy_size = HAPROXY_NAME_SIZE > (size_t)to ? (size_t)to : (HAPROXY_NAME_SIZE - 1);
		strlcpy(proxy, metrics + i, proxy_size + 1);

		i += to;
		to = strcspn(metrics + i, ",") + 1;
		server_size = HAPROXY_NAME_SIZE > (size_t)to ? (size_t)to : (HAPROXY_NAME_SIZE - 1);
		strlcpy(server, metrics + i, server_size + 1);

		for (j = 0; j < (int64_t)ncols; ++j)
		{
			i += to;
			to = strcspn(metrics + i, ",");
			if (to && haproxy_stat_cols[j].metric)
			{
				const haproxy_stat_col *col = &haproxy_stat_cols[j];
				namespace_metric_family_set(NULL, carg, col->metric, col->type, "HAProxy show stat column.");

				if (col->kind == 1)
				{
					char status[100];
					strlcpy(status, metrics + i, 100 > (to + 1) ? (to + 1) : 100);
					tmpval = 1;
					metric_add_labels3("haproxy_server_status", &tmpval, DATATYPE_INT, carg,
						"proxy", proxy, "server", server, "state", status);
					int64_t up = (!strncasecmp(status, "UP", 2)) ? 1 : 0;
					namespace_metric_family_set(NULL, carg, "haproxy_server_up", METRIC_TYPE_GAUGE,
						"1 if HAProxy server/frontend/backend status is UP.");
					metric_add_labels2("haproxy_server_up", &up, DATATYPE_INT, carg,
						"proxy", proxy, "server", server);
				}
				else if (col->kind == 2)
				{
					char status[100];
					strlcpy(status, metrics + i, 100 > (to + 1) ? (to + 1) : 100);
					tmpval = 1;
					metric_add_labels3("haproxy_check_status", &tmpval, DATATYPE_INT, carg,
						"proxy", proxy, "server", server, "state", status);
				}
				else if (col->kind == 3)
				{
					tmpval = atoll(metrics + i);
					metric_add_labels3(col->metric, &tmpval, DATATYPE_INT, carg,
						"proxy", proxy, "server", server, "code", (char *)col->code);
				}
				else if (col->kind == 4)
				{
					double seconds = atoll(metrics + i) / 1000.0;
					metric_add_labels2(col->metric, &seconds, DATATYPE_DOUBLE, carg,
						"proxy", proxy, "server", server);
				}
				else
				{
					tmpval = atoll(metrics + i);
					metric_add_labels2(col->metric, &tmpval, DATATYPE_INT, carg,
						"proxy", proxy, "server", server);
				}
			}
			++i;
		}
		to = strcspn(metrics + i, "\n");
		i += to;
	}
	carg->parser_status = 1;
}

void haproxy_sess_handler(char *metrics, size_t size, context_arg *carg)
{
	/* Distinct from show-stat scur → haproxy_current_sessions. */
	namespace_metric_family_set(NULL, carg, "haproxy_sessions", METRIC_TYPE_GAUGE,
		"Number of sessions listed by show sess.");

	int64_t i;
	int64_t cnt;
	int64_t to;
	for (i = 0, cnt = -1; i < size; ++i, ++cnt)
	{
		to = strcspn(metrics + i, "\n");
		i += to;
	}
	metric_add_auto("haproxy_sessions", &cnt, DATATYPE_INT, carg);
	carg->parser_status = 1;
}

int8_t haproxy_validator(context_arg *carg, char *data, size_t size)
{
	char *ret = strstr(data, "\n\n");
	if (ret)
		return 1;
	else
		return 0;
}


string* haproxy_stat_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, ";csv", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else
		return string_init_add_auto("show stat\n");
}

string* haproxy_info_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add_auto("show info\n");
	else
		return (void*)-1;
}

string* haproxy_pools_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add_auto("show pools\n");
	else
		return (void*)-1;
}

string* haproxy_sess_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto != APROTO_HTTP) && (hi->proto != APROTO_HTTPS))
		return string_init_add_auto("show sess\n");
	else
		return (void*)-1;
}

void haproxy_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("haproxy");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = haproxy_stat_handler;
	actx->handler[0].validator = haproxy_validator;
	actx->handler[0].mesg_func = haproxy_stat_mesg;
	strlcpy(actx->handler[0].key,"haproxy_stat", 255);

	actx->handler[1].name = haproxy_info_handler;
	actx->handler[1].validator = haproxy_validator;
	actx->handler[1].mesg_func = haproxy_info_mesg;
	strlcpy(actx->handler[1].key,"haproxy_info", 255);

	actx->handler[2].name = haproxy_pools_handler;
	actx->handler[2].validator = haproxy_validator;
	actx->handler[2].mesg_func = haproxy_pools_mesg;
	strlcpy(actx->handler[2].key,"haproxy_pools", 255);

	actx->handler[3].name = haproxy_sess_handler;
	actx->handler[3].validator = haproxy_validator;
	actx->handler[3].mesg_func = haproxy_sess_mesg;
	strlcpy(actx->handler[3].key,"haproxy_sess", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
