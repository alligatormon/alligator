#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include <string.h>
#include "common/selector.h"
#include "main.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/aggregator.h"
#include "common/json_query.h"
#include "common/validator.h"

static void php_fpm_gauge(context_arg *carg, char *name, const char *help, int64_t value, const char *pool)
{
	namespace_metric_family_set(NULL, carg, name, METRIC_TYPE_GAUGE, help);
	if (pool && *pool)
		metric_add_labels(name, &value, DATATYPE_INT, carg, "pool", (char *)pool);
	else
		metric_add_auto(name, &value, DATATYPE_INT, carg);
}

static void php_fpm_counter(context_arg *carg, char *name, const char *help, int64_t value, const char *pool)
{
	namespace_metric_family_set(NULL, carg, name, METRIC_TYPE_COUNTER, help);
	if (pool && *pool)
		metric_add_labels(name, &value, DATATYPE_INT, carg, "pool", (char *)pool);
	else
		metric_add_auto(name, &value, DATATYPE_INT, carg);
}

static int64_t php_fpm_json_int(json_t *obj, const char *key)
{
	json_t *v = json_object_get(obj, key);
	if (!v)
		return 0;
	if (json_is_integer(v))
		return json_integer_value(v);
	if (json_is_real(v))
		return (int64_t)json_real_value(v);
	if (json_is_string(v))
		return atoll(json_string_value(v));
	return 0;
}

/* Known FPM worker states; unknown folds into "other". Always emit all so zeros overwrite stale series. */
static void php_fpm_emit_process_states(context_arg *carg, const char *pool, json_t *processes)
{
	struct { const char *name; int64_t count; } states[] = {
		{"Idle", 0}, {"Running", 0}, {"Finishing", 0}, {"Reading headers", 0},
		{"Waiting", 0}, {"other", 0}
	};
	const size_t nstates = sizeof(states) / sizeof(states[0]);

	if (processes && json_is_array(processes))
	{
		size_t n = json_array_size(processes);
		for (size_t i = 0; i < n; i++)
		{
			json_t *proc = json_array_get(processes, i);
			json_t *state_j = json_object_get(proc, "state");
			const char *state = (state_j && json_is_string(state_j)) ? json_string_value(state_j) : "other";
			size_t matched = nstates - 1;
			for (size_t s = 0; s + 1 < nstates; s++)
			{
				if (!strcmp(state, states[s].name))
				{
					matched = s;
					break;
				}
			}
			states[matched].count++;
		}
	}

	namespace_metric_family_set(NULL, carg, "php_fpm_process_state", METRIC_TYPE_GAUGE,
		"Count of PHP-FPM workers by state (requires status ?full; zeroed when ?full is absent).");
	for (size_t s = 0; s < nstates; s++)
	{
		metric_add_labels2("php_fpm_process_state", &states[s].count, DATATYPE_INT, carg,
			"pool", (char *)pool, "state", (char *)states[s].name);
	}
}

void php_fpm_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "php-fpm json error on line %d: %s\n", error.line, error.text);
		uint64_t up = 0;
		namespace_metric_family_set(NULL, carg, "php_fpm_up", METRIC_TYPE_GAUGE,
			"1 if the last PHP-FPM status scrape succeeded.");
		metric_add_auto("php_fpm_up", &up, DATATYPE_UINT, carg);
		carg->parser_status = 0;
		return;
	}

	const char *pool = "";
	json_t *pool_j = json_object_get(root, "pool");
	if (pool_j && json_is_string(pool_j))
		pool = json_string_value(pool_j);

	const char *pm = "";
	json_t *pm_j = json_object_get(root, "process manager");
	if (pm_j && json_is_string(pm_j))
		pm = json_string_value(pm_j);

	uint64_t info = 1;
	namespace_metric_family_set(NULL, carg, "php_fpm_info", METRIC_TYPE_GAUGE,
		"PHP-FPM pool identity (value always 1).");
	metric_add_labels2("php_fpm_info", &info, DATATYPE_UINT, carg,
		"pool", (char *)pool, "process_manager", (char *)(pm && *pm ? pm : "unknown"));

	php_fpm_gauge(carg, "php_fpm_start_since_seconds",
		"Seconds since the PHP-FPM pool was last started.",
		php_fpm_json_int(root, "start since"), pool);
	php_fpm_counter(carg, "php_fpm_accepted_connections_total",
		"Total connections accepted by the PHP-FPM pool.",
		php_fpm_json_int(root, "accepted conn"), pool);
	php_fpm_gauge(carg, "php_fpm_listen_queue",
		"Requests currently waiting in the listen queue.",
		php_fpm_json_int(root, "listen queue"), pool);
	php_fpm_gauge(carg, "php_fpm_max_listen_queue",
		"Maximum listen queue length observed since start.",
		php_fpm_json_int(root, "max listen queue"), pool);
	php_fpm_gauge(carg, "php_fpm_listen_queue_length",
		"Configured listen queue length (socket backlog).",
		php_fpm_json_int(root, "listen queue len"), pool);
	php_fpm_gauge(carg, "php_fpm_idle_processes",
		"Idle PHP-FPM worker processes.",
		php_fpm_json_int(root, "idle processes"), pool);
	php_fpm_gauge(carg, "php_fpm_active_processes",
		"Active PHP-FPM worker processes.",
		php_fpm_json_int(root, "active processes"), pool);
	php_fpm_gauge(carg, "php_fpm_total_processes",
		"Total PHP-FPM worker processes (idle + active).",
		php_fpm_json_int(root, "total processes"), pool);
	php_fpm_gauge(carg, "php_fpm_max_active_processes",
		"Peak concurrent active processes since start.",
		php_fpm_json_int(root, "max active processes"), pool);
	php_fpm_counter(carg, "php_fpm_max_children_reached_total",
		"Times the pool hit pm.max_children since start.",
		php_fpm_json_int(root, "max children reached"), pool);
	php_fpm_counter(carg, "php_fpm_slow_requests_total",
		"Requests that exceeded request_slowlog_timeout.",
		php_fpm_json_int(root, "slow requests"), pool);
	php_fpm_gauge(carg, "php_fpm_memory_peak_bytes",
		"Peak request memory reported by the status page (not pool RSS).",
		php_fpm_json_int(root, "memory peak"), pool);

	/* Optional ?full: count workers by state (avoid URI/script cardinality).
	 * Always emit known states (incl. 0) when pool is known so brief ?json clears prior ?full series. */
	json_t *processes = json_object_get(root, "processes");
	if (processes && json_is_array(processes))
		php_fpm_emit_process_states(carg, pool && *pool ? pool : "unknown", processes);
	else if (pool && *pool)
		php_fpm_emit_process_states(carg, pool, NULL);

	uint64_t up = 1;
	namespace_metric_family_set(NULL, carg, "php_fpm_up", METRIC_TYPE_GAUGE,
		"1 if the last PHP-FPM status scrape succeeded.");
	metric_add_auto("php_fpm_up", &up, DATATYPE_UINT, carg);

	json_decref(root);
	carg->parser_status = 1;
}

string* php_fpm_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, "1.1", env, proxy_settings, NULL));
	return NULL;
}

void php_fpm_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("php-fpm");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = php_fpm_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = php_fpm_mesg;
	strlcpy(actx->handler[0].key, "php-fpm", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
