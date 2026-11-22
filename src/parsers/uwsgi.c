#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "common/http.h"
#include "common/json_query.h"
#include "common/validator.h"
#include "main.h"
#define UWSGI_METRIC_SIZE 1000

static inline void uwsgi_set_gauge(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, help);
}

static inline void uwsgi_set_counter(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_COUNTER, help);
}

static int64_t uwsgi_json_int(json_t *obj, const char *key)
{
	json_t *v = json_object_get(obj, key);
	if (!v)
		return 0;
	if (json_is_integer(v))
		return json_integer_value(v);
	if (json_is_real(v))
		return (int64_t)json_real_value(v);
	return 0;
}

void uwsgi_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_t *root;
	json_error_t error;
	int64_t tvalue;
	char metricname[UWSGI_METRIC_SIZE];

	root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "json error on line %d: %s\n", error.line, error.text);
		uint64_t up = 0;
		uwsgi_set_gauge(carg, "uwsgi_up", "1 if the last uWSGI stats scrape succeeded.");
		metric_add_auto("uwsgi_up", &up, DATATYPE_UINT, carg);
		carg->parser_status = 0;
		return;
	}

	tvalue = uwsgi_json_int(root, "listen_queue");
	uwsgi_set_gauge(carg, "uwsgi_listen_queue", "uWSGI listen queue depth.");
	metric_add_auto("uwsgi_listen_queue", &tvalue, DATATYPE_INT, carg);

	tvalue = uwsgi_json_int(root, "listen_queue_errors");
	uwsgi_set_counter(carg, "uwsgi_listen_queue_errors_total", "uWSGI listen queue overflow errors.");
	metric_add_auto("uwsgi_listen_queue_errors_total", &tvalue, DATATYPE_INT, carg);

	tvalue = uwsgi_json_int(root, "signal_queue");
	uwsgi_set_gauge(carg, "uwsgi_signal_queue", "uWSGI signal queue depth.");
	metric_add_auto("uwsgi_signal_queue", &tvalue, DATATYPE_INT, carg);

	tvalue = uwsgi_json_int(root, "load");
	uwsgi_set_gauge(carg, "uwsgi_load", "uWSGI reported load.");
	metric_add_auto("uwsgi_load", &tvalue, DATATYPE_INT, carg);

	json_t *locks = json_object_get(root, "locks");
	if (locks && json_is_array(locks))
	{
		size_t locks_size = json_array_size(locks);
		for (size_t i = 0; i < locks_size; i++)
		{
			json_t *locks_obj = json_array_get(locks, i);
			const char *key;
			json_t *value;

			json_object_foreach(locks_obj, key, value)
			{
				snprintf(metricname, UWSGI_METRIC_SIZE, "uwsgi_locks_%s", key);
				metric_name_normalizer(metricname, strlen(metricname));
				tvalue = json_integer_value(value);
				uwsgi_set_gauge(carg, metricname, "uWSGI lock counter from stats.");
				metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
			}
		}
	}

	json_t *sockets = json_object_get(root, "sockets");
	if (sockets && json_is_array(sockets))
	{
		size_t sockets_size = json_array_size(sockets);
		for (size_t i = 0; i < sockets_size; i++)
		{
			json_t *sockets_obj = json_array_get(sockets, i);
			json_t *socket_name = json_object_get(sockets_obj, "name");
			json_t *socket_proto = json_object_get(sockets_obj, "proto");
			if (!socket_name || !socket_proto)
				continue;

			const char *key;
			json_t *value;
			json_object_foreach(sockets_obj, key, value)
			{
				if (json_typeof(value) != JSON_INTEGER)
					continue;
				snprintf(metricname, UWSGI_METRIC_SIZE, "uwsgi_sockets_%s", key);
				metric_name_normalizer(metricname, strlen(metricname));
				tvalue = json_integer_value(value);
				uwsgi_set_gauge(carg, metricname, "uWSGI socket field from stats.");
				metric_add_labels2(metricname, &tvalue, DATATYPE_INT, carg,
					"name", (char *)json_string_value(socket_name),
					"proto", (char *)json_string_value(socket_proto));
			}
		}
	}

	json_t *workers = json_object_get(root, "workers");
	int64_t worker_accepting_cnt = 0;
	int64_t worker_requests_cnt = 0;
	int64_t worker_delta_requests_cnt = 0;
	int64_t worker_running_time_us_cnt = 0;
	int64_t worker_exceptions_cnt = 0;
	int64_t worker_harakiri_count_cnt = 0;
	int64_t worker_signals_cnt = 0;
	int64_t worker_signal_queue_cnt = 0;
	int64_t worker_respawn_count_cnt = 0;
	int64_t worker_tx_cnt = 0;
	int64_t worker_avg_rt_us_sum = 0;
	int64_t worker_avg_rt_samples = 0;
	int64_t worker_idle_cnt = 0;
	int64_t worker_busy_cnt = 0;
	int64_t worker_count = 0;
	int64_t worker_static_requests_cnt = 0;
	int64_t worker_routed_requests_cnt = 0;
	int64_t worker_offloaded_requests_cnt = 0;
	int64_t worker_write_errors_cnt = 0;
	int64_t worker_read_errors_cnt = 0;
	int64_t worker_in_request_cnt = 0;

	if (workers && json_is_array(workers))
	{
		size_t workers_size = json_array_size(workers);
		worker_count = (int64_t)workers_size;
		for (size_t i = 0; i < workers_size; i++)
		{
			json_t *workers_obj = json_array_get(workers, i);

			worker_accepting_cnt += uwsgi_json_int(workers_obj, "accepting");
			worker_requests_cnt += uwsgi_json_int(workers_obj, "requests");
			worker_delta_requests_cnt += uwsgi_json_int(workers_obj, "delta_requests");
			worker_exceptions_cnt += uwsgi_json_int(workers_obj, "exceptions");
			worker_harakiri_count_cnt += uwsgi_json_int(workers_obj, "harakiri_count");
			worker_signals_cnt += uwsgi_json_int(workers_obj, "signals");
			worker_signal_queue_cnt += uwsgi_json_int(workers_obj, "signal_queue");
			worker_respawn_count_cnt += uwsgi_json_int(workers_obj, "respawn_count");
			worker_tx_cnt += uwsgi_json_int(workers_obj, "tx");
			worker_running_time_us_cnt += uwsgi_json_int(workers_obj, "running_time");

			int64_t avg_rt = uwsgi_json_int(workers_obj, "avg_rt");
			if (json_object_get(workers_obj, "avg_rt"))
			{
				worker_avg_rt_us_sum += avg_rt;
				worker_avg_rt_samples++;
			}

			json_t *status = json_object_get(workers_obj, "status");
			if (status && json_is_string(status))
			{
				const char *st = json_string_value(status);
				if (!strncmp(st, "idle", 4))
					worker_idle_cnt++;
				else if (!strncmp(st, "busy", 4))
					worker_busy_cnt++;
			}

			json_t *cores = json_object_get(workers_obj, "cores");
			if (!cores || !json_is_array(cores))
				continue;
			size_t cores_size = json_array_size(cores);
			for (size_t j = 0; j < cores_size; j++)
			{
				json_t *cores_obj = json_array_get(cores, j);
				worker_static_requests_cnt += uwsgi_json_int(cores_obj, "static_requests");
				worker_routed_requests_cnt += uwsgi_json_int(cores_obj, "routed_requests");
				worker_offloaded_requests_cnt += uwsgi_json_int(cores_obj, "offloaded_requests");
				worker_write_errors_cnt += uwsgi_json_int(cores_obj, "write_errors");
				worker_read_errors_cnt += uwsgi_json_int(cores_obj, "read_errors");
				worker_in_request_cnt += uwsgi_json_int(cores_obj, "in_request");
			}
		}
	}

	uwsgi_set_gauge(carg, "uwsgi_workers", "Number of uWSGI workers.");
	metric_add_auto("uwsgi_workers", &worker_count, DATATYPE_INT, carg);
	uwsgi_set_gauge(carg, "uwsgi_worker_accepting", "Sum of workers currently accepting connections.");
	metric_add_auto("uwsgi_worker_accepting", &worker_accepting_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_requests_total", "Sum of worker request counters.");
	metric_add_auto("uwsgi_worker_requests_total", &worker_requests_cnt, DATATYPE_INT, carg);
	uwsgi_set_gauge(carg, "uwsgi_worker_delta_requests", "Sum of worker delta_requests.");
	metric_add_auto("uwsgi_worker_delta_requests", &worker_delta_requests_cnt, DATATYPE_INT, carg);

	double running_time_seconds = (double)worker_running_time_us_cnt / 1000000.0;
	uwsgi_set_counter(carg, "uwsgi_worker_running_time_seconds_total",
		"Sum of worker running_time converted from microseconds to seconds.");
	metric_add_auto("uwsgi_worker_running_time_seconds_total", &running_time_seconds, DATATYPE_DOUBLE, carg);

	uwsgi_set_counter(carg, "uwsgi_worker_exceptions_total", "Sum of worker exceptions.");
	metric_add_auto("uwsgi_worker_exceptions_total", &worker_exceptions_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_harakiri_total", "Sum of worker harakiri_count.");
	metric_add_auto("uwsgi_worker_harakiri_total", &worker_harakiri_count_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_signals_total", "Sum of worker signals handled.");
	metric_add_auto("uwsgi_worker_signals_total", &worker_signals_cnt, DATATYPE_INT, carg);
	uwsgi_set_gauge(carg, "uwsgi_worker_signal_queue", "Sum of worker signal queues.");
	metric_add_auto("uwsgi_worker_signal_queue", &worker_signal_queue_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_respawns_total", "Sum of worker respawn_count.");
	metric_add_auto("uwsgi_worker_respawns_total", &worker_respawn_count_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_transmit_bytes_total", "Sum of worker transmitted bytes (tx).");
	metric_add_auto("uwsgi_worker_transmit_bytes_total", &worker_tx_cnt, DATATYPE_INT, carg);

	double avg_rt_seconds = 0;
	if (worker_avg_rt_samples > 0)
		avg_rt_seconds = ((double)worker_avg_rt_us_sum / (double)worker_avg_rt_samples) / 1000000.0;
	uwsgi_set_gauge(carg, "uwsgi_worker_avg_response_time_seconds",
		"Mean of workers avg_rt (microseconds) converted to seconds.");
	metric_add_auto("uwsgi_worker_avg_response_time_seconds", &avg_rt_seconds, DATATYPE_DOUBLE, carg);

	uwsgi_set_gauge(carg, "uwsgi_worker_idle", "Workers with status idle.");
	metric_add_auto("uwsgi_worker_idle", &worker_idle_cnt, DATATYPE_INT, carg);
	uwsgi_set_gauge(carg, "uwsgi_worker_busy", "Workers with status busy.");
	metric_add_auto("uwsgi_worker_busy", &worker_busy_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_static_requests_total", "Sum of core static_requests.");
	metric_add_auto("uwsgi_worker_static_requests_total", &worker_static_requests_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_routed_requests_total", "Sum of core routed_requests.");
	metric_add_auto("uwsgi_worker_routed_requests_total", &worker_routed_requests_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_offloaded_requests_total", "Sum of core offloaded_requests.");
	metric_add_auto("uwsgi_worker_offloaded_requests_total", &worker_offloaded_requests_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_write_errors_total", "Sum of core write_errors.");
	metric_add_auto("uwsgi_worker_write_errors_total", &worker_write_errors_cnt, DATATYPE_INT, carg);
	uwsgi_set_counter(carg, "uwsgi_worker_read_errors_total", "Sum of core read_errors.");
	metric_add_auto("uwsgi_worker_read_errors_total", &worker_read_errors_cnt, DATATYPE_INT, carg);
	uwsgi_set_gauge(carg, "uwsgi_worker_in_request", "Sum of cores currently in_request.");
	metric_add_auto("uwsgi_worker_in_request", &worker_in_request_cnt, DATATYPE_INT, carg);

	uint64_t up = 1;
	uwsgi_set_gauge(carg, "uwsgi_up", "1 if the last uWSGI stats scrape succeeded.");
	metric_add_auto("uwsgi_up", &up, DATATYPE_UINT, carg);

	json_decref(root);
	carg->parser_status = 1;
}

string* uwsgi_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else
		return NULL;
}

void uwsgi_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("uwsgi");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = uwsgi_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = uwsgi_mesg;
	strlcpy(actx->handler[0].key,"uwsgi", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
