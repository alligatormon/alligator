#include <jansson.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_parser.h"
#include "common/http.h"
#include "main.h"
#define UWSGI_METRIC_SIZE 1000
void uwsgi_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;
	int64_t tvalue;
	char metricname[UWSGI_METRIC_SIZE];

	root = json_loads(metrics, 0, &error);

	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}
	json_t *listen_queue = json_object_get(root, "listen_queue");
	tvalue = json_integer_value(listen_queue);
	metric_add_auto("uwsgi_listen_queue", &tvalue, DATATYPE_INT, carg);

	json_t *listen_queue_errors = json_object_get(root, "listen_queue_errors");
	tvalue = json_integer_value(listen_queue_errors);
	metric_add_auto("uwsgi_listen_queue_errors", &tvalue, DATATYPE_INT, carg);

	json_t *signal_queue = json_object_get(root, "signal_queue");
	tvalue = json_integer_value(signal_queue);
	metric_add_auto("uwsgi_signal_queue", &tvalue, DATATYPE_INT, carg);

	json_t *load = json_object_get(root, "load");
	tvalue = json_integer_value(load);
	metric_add_auto("uwsgi_load", &tvalue, DATATYPE_INT, carg);


	json_t *locks = json_object_get(root, "locks");
	size_t i;
	size_t locks_size = json_array_size(locks);
	for (i = 0; i < locks_size; i++)
	{
		json_t *locks_obj = json_array_get(locks, i);
		const char *key;
		json_t *value;

		json_object_foreach(locks_obj, key, value)
		{
			snprintf(metricname, UWSGI_METRIC_SIZE, "uwsgi_locks_%s", key);
			tvalue = json_integer_value(value);
			metric_add_auto(metricname, &tvalue, DATATYPE_INT, carg);
		}
	}

	json_t *sockets = json_object_get(root, "sockets");
	size_t sockets_size = json_array_size(sockets);
	for (i = 0; i < sockets_size; i++)
	{
		json_t *sockets_obj = json_array_get(sockets, i);
		const char *key;
		json_t *value;

		json_t *socket_name = json_object_get(sockets_obj, "name");
		if(!socket_name)
			continue;
		json_t *socket_proto = json_object_get(sockets_obj, "proto");
		if(!socket_proto)
			continue;

		json_object_foreach(sockets_obj, key, value)
		{
			int jsontype = json_typeof(value);
			if (jsontype==JSON_INTEGER)
			{
				snprintf(metricname, UWSGI_METRIC_SIZE, "uwsgi_sockets_%s", key);
				tvalue = json_integer_value(value);
				metric_add_labels2(metricname, &tvalue, DATATYPE_INT, carg, "name", (char*)json_string_value(socket_name), "proto", (char*)json_string_value(socket_proto));
			}
		}
	}

	json_t *workers = json_object_get(root, "workers");
	size_t workers_size = json_array_size(workers);

	int64_t worker_accepting_cnt = 0;
	int64_t worker_requests_cnt = 0;
	int64_t worker_delta_requests_cnt = 0;
	int64_t worker_running_time_cnt = 0;
	int64_t worker_exceptions_cnt = 0;
	int64_t worker_harakiri_count_cnt = 0;
	int64_t worker_signals_cnt = 0;
	int64_t worker_signal_queue_cnt = 0;
	int64_t worker_respawn_count_cnt = 0;
	int64_t worker_tx_cnt = 0;
	int64_t worker_avg_rt_cnt = 0;
	int64_t worker_idle_cnt = 0;
	int64_t worker_static_requests_cnt = 0;
	int64_t worker_routed_requests_cnt = 0;
	int64_t worker_offloaded_requests_cnt = 0;
	int64_t worker_write_errors_cnt = 0;
	int64_t worker_read_errors_cnt = 0;
	int64_t worker_in_request_cnt = 0;
	for (i = 0; i < workers_size; i++)
	{
		json_t *workers_obj = json_array_get(workers, i);

		json_t *worker_accepting = json_object_get(workers_obj, "accepting");
		if(!worker_accepting)
			continue;
		tvalue = json_integer_value(worker_accepting);
		worker_accepting_cnt += tvalue;

		json_t *worker_requests = json_object_get(workers_obj, "requests");
		if(!worker_requests)
			continue;
		tvalue = json_integer_value(worker_requests);
		worker_requests_cnt += tvalue;

		json_t *worker_delta_requests = json_object_get(workers_obj, "delta_requests");
		if(!worker_delta_requests)
			continue;
		tvalue = json_integer_value(worker_delta_requests);
		worker_delta_requests_cnt += tvalue;


		json_t *worker_exceptions = json_object_get(workers_obj, "exceptions");
		if(!worker_exceptions)
			continue;
		tvalue = json_integer_value(worker_exceptions);
		worker_exceptions_cnt += tvalue;


		json_t *worker_harakiri_count = json_object_get(workers_obj, "harakiri_count");
		if(!worker_harakiri_count)
			continue;
		tvalue = json_integer_value(worker_harakiri_count);
		worker_harakiri_count_cnt += tvalue;


		json_t *worker_signals = json_object_get(workers_obj, "signals");
		if(!worker_signals)
			continue;
		tvalue = json_integer_value(worker_signals);
		worker_signals_cnt += tvalue;

		json_t *worker_signal_queue = json_object_get(workers_obj, "signal_queue");
		if(!worker_signal_queue)
			continue;
		tvalue = json_integer_value(worker_signal_queue);
		worker_signal_queue_cnt += tvalue;

		json_t *worker_idle = json_object_get(workers_obj, "status");
		if(!worker_idle)
			continue;
		if(!strncmp((char*)json_string_value(worker_idle), "idle", 4))
			worker_idle_cnt += tvalue;

		json_t *worker_respawn_count = json_object_get(workers_obj, "respawn_count");
		if(!worker_respawn_count)
			continue;
		tvalue = json_integer_value(worker_respawn_count);
		worker_respawn_count_cnt += tvalue;

		json_t *worker_tx = json_object_get(workers_obj, "tx");
		if(!worker_tx)
			continue;
		tvalue = json_integer_value(worker_tx);
		worker_tx_cnt += tvalue;

		json_t *worker_avg_rt = json_object_get(workers_obj, "avg_rt");
		if(!worker_avg_rt)
			continue;
		tvalue = json_integer_value(worker_avg_rt);
		worker_avg_rt_cnt += tvalue;


		json_t *worker_running_time = json_object_get(workers_obj, "running_time");
		if(!worker_running_time)
			continue;
		tvalue = json_integer_value(worker_running_time);
		worker_running_time_cnt += tvalue;


		json_t *cores = json_object_get(workers_obj, "cores");
		if(!cores)
			continue;
		size_t j;
		size_t cores_size = json_array_size(cores);
		for (j = 0; j < cores_size; j++)
		{
			json_t *cores_obj = json_array_get(cores, j);

			json_t *uwsgi_worker_static_requests = json_object_get(cores_obj, "static_requests");
			if(!uwsgi_worker_static_requests)
				continue;
			tvalue = json_integer_value(uwsgi_worker_static_requests);
			worker_static_requests_cnt += tvalue;

			json_t *uwsgi_worker_routed_requests = json_object_get(cores_obj, "routed_requests");
			if(!uwsgi_worker_routed_requests)
				continue;
			tvalue = json_integer_value(uwsgi_worker_routed_requests);
			worker_routed_requests_cnt += tvalue;

			json_t *uwsgi_worker_offloaded_requests = json_object_get(cores_obj, "offloaded_requests");
			if(!uwsgi_worker_offloaded_requests)
				continue;
			tvalue = json_integer_value(uwsgi_worker_offloaded_requests);
			worker_offloaded_requests_cnt += tvalue;

			json_t *uwsgi_worker_write_errors = json_object_get(cores_obj, "write_errors");
			if(!uwsgi_worker_write_errors)
				continue;
			tvalue = json_integer_value(uwsgi_worker_write_errors);
			worker_write_errors_cnt += tvalue;

			json_t *uwsgi_worker_read_errors = json_object_get(cores_obj, "read_errors");
			if(!uwsgi_worker_read_errors)
				continue;
			tvalue = json_integer_value(uwsgi_worker_read_errors);
			worker_read_errors_cnt += tvalue;

			json_t *uwsgi_worker_in_request = json_object_get(cores_obj, "in_request");
			if(!uwsgi_worker_in_request)
				continue;
			tvalue = json_integer_value(uwsgi_worker_in_request);
			worker_in_request_cnt += tvalue;
		}
		
	}

	metric_add_auto("uwsgi_worker_accepting", &worker_accepting_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_requests", &worker_requests_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_delta_requests", &worker_delta_requests_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_running_time", &worker_running_time_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_exceptions", &worker_exceptions_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_harakiri_count", &worker_harakiri_count_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_signals", &worker_signals_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_signal_queue", &worker_signal_queue_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_respawn_count", &worker_respawn_count_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_tx", &worker_tx_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_avg_rt", &worker_avg_rt_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_idle", &worker_idle_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_static_requests", &worker_static_requests_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_routed_requests", &worker_routed_requests_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_offloaded_requests", &worker_offloaded_requests_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_write_errors", &worker_write_errors_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_read_errors", &worker_read_errors_cnt, DATATYPE_INT, carg);
	metric_add_auto("uwsgi_worker_in_request", &worker_in_request_cnt, DATATYPE_INT, carg);



	json_decref(root);
}

string* uwsgi_mesg(host_aggregator_info *hi, void *arg)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 1), 0, 0);
	else
		return NULL;
}

void uwsgi_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("uwsgi");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = uwsgi_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = uwsgi_mesg;
	strlcpy(actx->handler[0].key,"uwsgi", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
