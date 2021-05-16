#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include <query/query.h>
#include "main.h"
#define d64	PRId64
#define DRUID_NAME_SIZE 255
#define DRUID_OTHER 0
#define DRUID_FLOAT 1

typedef struct druid_columns_types {
	char *colname;
	uint8_t type;
} druid_columns_types;

void druid_columns_types_free(druid_columns_types *column_types, uint64_t size)
{
	for (uint64_t i = 0; i < size; i++)
		free(column_types[i].colname);

	free(column_types);
}

void druid_sql_execute_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t cur = 0;
	char druid_field[DRUID_NAME_SIZE];

	uint8_t format_names = 1;
	uint8_t format_type = 0;
	query_node *qn = carg->data;
	//char *colname = NULL;
	uint64_t columns_count = selector_count_field(metrics, "\t\n", strcspn(metrics, "\n"));
	druid_columns_types *column_types = calloc(columns_count, sizeof(*column_types));

	while (cur < size) {
		tommy_hashdyn *hash = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		uint64_t end = strcspn(metrics + cur, "\n") + cur;
		for (uint64_t i = 0; cur < end; i++) {
			uint64_t word_end = strcspn(metrics + cur, "\n\t");
			strlcpy(druid_field, metrics + cur, word_end + 1);

			cur += word_end + 1;

			//printf("ch: %s\n", ch_field);
			//printf("2 %d < %d\n", cur, end);

			if (format_names)
				column_types[i].colname = strdup(druid_field);
			else if (format_type)
			{
				if (!strncasecmp(druid_field, "Float", 5))
					column_types[i].type = DRUID_FLOAT;
			}
			else if (column_types[i].type == DRUID_FLOAT) // Float32 Float64
			{
				query_field *qf = query_field_get(qn->qf_hash, column_types[i].colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", druid_field);
					qf->d = strtod(druid_field, NULL);
					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", column_types[i].colname, druid_field);
					labels_hash_insert_nocache(hash, column_types[i].colname, druid_field);
				}
			}
			else
			{
				query_field *qf = query_field_get(qn->qf_hash, column_types[i].colname);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", druid_field);
					qf->i = strtoll(druid_field, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", column_types[i].colname, druid_field);
					labels_hash_insert_nocache(hash, column_types[i].colname, druid_field);
				}
			}
		}

		//printf("1 %d < %d: %d:%d\n", cur, size, format_names, format_type);

		if (format_names)
		{
			format_names = 0;
			format_type = 1;
		}
		else if (format_type)
		{
			format_type = 0;
		}
		else
		{
			qn->labels = hash;
			qn->carg = carg;
			query_set_values(qn);
			labels_hash_free(hash);
		}
	}
	druid_columns_types_free(column_types, columns_count);
}

void druid_queries_foreach(void *funcarg, void* arg)
{
	context_arg *carg = (context_arg*)funcarg;
	query_node *qn = arg;
	if (carg->log_level > 1)
	{
		puts("+-+-+-+-+-+-+-+");
		printf("run datasource '%s', make '%s': '%s'\n", qn->datasource, qn->make, qn->expr);
	}
	char *key = malloc(255);
	snprintf(key, 255, "(tcp://%s:%u)/custom", carg->host, htons(carg->dest->sin_port));

	string *query = string_init(1024);
	string_cat(query, qn->expr, strlen(qn->expr));
	string_cat(query, " FORMAT TabSeparatedWithNamesAndTypes", 37);

	char *query_encoded = malloc(query->l + 1024);
	urlencode(query_encoded, query->s, query->l);
	string_free(query);
	env_struct_push_alloc(carg->env, "Content-Length", "0");
	char *generated_query = gen_http_query(HTTP_POST, "/?query=", query_encoded, carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL);

	try_again(carg, generated_query, strlen(generated_query), druid_sql_execute_handler, "druid_custom", NULL, key, qn);
	//printf("try again %s/%"u64"\n", generated_query, strlen(generated_query));
}

void druid_status_health_handler(char *metrics, size_t size, context_arg *carg)
{
	if (carg->name)
	{
		query_ds *qds = query_get(carg->name);
		if (carg->log_level > 1)
			printf("found queries for datasource: %s: %p\n", carg->name, qds);
		if (qds)
		{
			tommy_hashdyn_foreach_arg(qds->hash, druid_queries_foreach, carg);
		}
	}

	uint64_t health = 0;
	if (strstr(metrics, "true"))
	{
		health = 1;
	}
	metric_add_auto("druid_status_health", &health, DATATYPE_UINT, carg);
}

void druid_selfDiscovered_status_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	uint64_t selfDiscovered = 0;
	json_t *selfDiscovered_json = json_object_get(root, "selfDiscovered");
	if (json_typeof(selfDiscovered_json) == JSON_TRUE)
		selfDiscovered = 1;

	metric_add_auto("druid_selfDiscovered_status", &selfDiscovered, DATATYPE_UINT, carg);

	json_decref(root);
}

void druid_coordinator_isleader_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	uint64_t leader = 0;
	json_t *leader_json = json_object_get(root, "leader");
	if (json_typeof(leader_json) == JSON_TRUE)
		leader = 1;

	metric_add_auto("druid_coordinator_isLeader", &leader, DATATYPE_UINT, carg);

	json_decref(root);
}

void druid_coordinator_leader_handler(char *metrics, size_t size, context_arg *carg)
{
	char_strip_end(metrics, size);
	uint64_t leader = 1;
	metric_add_labels("druid_coordinator_leader", &leader, DATATYPE_UINT, carg, "leader", metrics);
}

void druid_coordinator_loadstatus_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *loadstatus_key;
	json_t *loadstatus_opt;
	json_object_foreach(root, loadstatus_key, loadstatus_opt)
	{
		double metric_value = json_real_value(loadstatus_opt);
		metric_add_labels("druid_coordinator_loadstatus", &metric_value, DATATYPE_DOUBLE, carg, "loadstatus", (char*)loadstatus_key);
	}

	json_decref(root);
}

void druid_coordinator_servers_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t servers_num = json_array_size(root);
	for (uint64_t i = 0; i < servers_num; i++)
	{
		json_t *srv = json_array_get(root, i);

		json_t *host_json = json_object_get(srv, "host");
		char *host = (char*)json_string_value(host_json);
		if (!host)
			continue;

		json_t *tier_json = json_object_get(srv, "tier");
		char *tier = (char*)json_string_value(tier_json);
		if (!tier)
			continue;

		json_t *type_json = json_object_get(srv, "type");
		char *type = (char*)json_string_value(type_json);
		if (!type)
			continue;

		json_t *priority_json = json_object_get(srv, "priority");
		int64_t priority = json_integer_value(priority_json);
		metric_add_labels3("druid_coordinator_servers_priority", &priority, DATATYPE_INT, carg, "host", host, "tier", tier, "type", type);

		json_t *currSize_json = json_object_get(srv, "currSize");
		int64_t currSize = json_integer_value(currSize_json);
		metric_add_labels3("druid_coordinator_servers_currSize", &currSize, DATATYPE_INT, carg, "host", host, "tier", tier, "type", type);

		json_t *maxSize_json = json_object_get(srv, "maxSize");
		int64_t maxSize = json_integer_value(maxSize_json);
		metric_add_labels3("druid_coordinator_servers_maxSize", &maxSize, DATATYPE_INT, carg, "host", host, "tier", tier, "type", type);
	}

	json_decref(root);
}

void druid_coordinator_compaction_status_handler(char *metrics, size_t size, context_arg *carg)
{
	puts("druid_coordinator_compaction_status_handler");
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *latestStatus = json_object_get(root, "latestStatus");
	char metric_name[DRUID_NAME_SIZE];
	strlcpy(metric_name, "druid_coordinator_compaction_latest_status_", DRUID_NAME_SIZE);

	size_t status_num = json_array_size(latestStatus);
	for (uint64_t i = 0; i < status_num; i++)
	{
		json_t *status = json_array_get(latestStatus, i);

		json_t *dataSource_json = json_object_get(status, "dataSource");
		char *dataSource = (char*)json_string_value(dataSource_json);
		if (!dataSource)
			continue;

		json_t *scheduleStatus_json = json_object_get(status, "scheduleStatus");
		char *scheduleStatus = (char*)json_string_value(scheduleStatus_json);
		if (!scheduleStatus)
			continue;

		const char *status_key;
		json_t *status_opt;
		json_object_foreach(root, status_key, status_opt)
		{
			int type = json_typeof(status_opt);
			if (type == JSON_INTEGER)
			{
				strlcpy(metric_name+43, status_key, DRUID_NAME_SIZE - 43);
				int64_t metric_value = json_real_value(status_opt);
				metric_add_labels2(metric_name, &metric_value, DATATYPE_INT, carg, "dataSource", dataSource, "scheduleStatus", scheduleStatus);
			}
			else if (type == JSON_REAL)
			{
				strlcpy(metric_name+43, status_key, DRUID_NAME_SIZE - 43);
				double metric_value = json_real_value(status_opt);
				metric_add_labels2(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "dataSource", dataSource, "scheduleStatus", scheduleStatus);
			}
		}
	}
}

void druid_worker_handler(char *metrics, size_t size, context_arg *carg)
{
	puts(metrics);
}

void druid_historical_handler(char *metrics, size_t size, context_arg *carg)
{
	puts(metrics);
}

void druid_broker_handler(char *metrics, size_t size, context_arg *carg)
{
	puts(metrics);
}

string *druid_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, "1.0", env, proxy_settings), 0, 0);
}

string* druid_status_health_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/status/health", env, proxy_settings); }
string* druid_selfDiscovered_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/status/selfDiscovered/status", env, proxy_settings); }
string* druid_metadata_datasources_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/metadata/datasources?full", env, proxy_settings); }
string* druid_coordinator_isleader_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/isLeader", env, proxy_settings); }
string* druid_coordinator_leader_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/leader", env, proxy_settings); }
string* druid_coordinator_loadqueue_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/loadqueue", env, proxy_settings); }
string* druid_coordinator_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/loadstatus", env, proxy_settings); }
string* druid_coordinator_compaction_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/compaction/status", env, proxy_settings); }
string* druid_coordinator_servers_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/servers?simple", env, proxy_settings); }
string* druid_indexer_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/indexer/v1/tasks", env, proxy_settings); }
string* druid_indexer_supervisor_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/indexer/v1/supervisor?full", env, proxy_settings); }
string* druid_worker_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/worker/v1/enabled", env, proxy_settings); } // port 8091
string* druid_historical_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/historical/v1/loadstatus", env, proxy_settings); } // port 8083
string* druid_broker_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/broker/v1/loadstatus", env, proxy_settings); } // port 8082
string* druid_sql_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/v2/sql", env, proxy_settings); }

void druid_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("druid");
	actx->handlers = 7;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = druid_status_health_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = druid_status_health_mesg;
	strlcpy(actx->handler[0].key,"druid_status_health", 255);

	actx->handler[1].name = druid_selfDiscovered_status_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = druid_selfDiscovered_status_mesg;
	strlcpy(actx->handler[1].key,"druid_selfDiscovered_status", 255);

	actx->handler[2].name = druid_coordinator_isleader_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = druid_coordinator_isleader_mesg;
	strlcpy(actx->handler[2].key,"druid_coordinator_isleader", 255);

	actx->handler[3].name = druid_coordinator_leader_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = druid_coordinator_leader_mesg;
	strlcpy(actx->handler[3].key,"druid_coordinator_leader", 255);

	actx->handler[4].name = druid_coordinator_loadstatus_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = druid_coordinator_loadstatus_mesg;
	strlcpy(actx->handler[4].key,"druid_coordinator_loadstatus", 255);

	actx->handler[5].name = druid_coordinator_servers_handler;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = druid_coordinator_servers_mesg;
	strlcpy(actx->handler[5].key,"druid_coordinator_servers", 255);

	actx->handler[6].name = druid_coordinator_compaction_status_handler;
	actx->handler[6].validator = NULL;
	actx->handler[6].mesg_func = druid_coordinator_compaction_status_mesg;
	strlcpy(actx->handler[6].key,"druid_coordinator_compaction_status", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void druid_worker_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("druid_worker"); // port 8091
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = druid_worker_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = druid_worker_tasks_mesg;
	strlcpy(actx->handler[0].key,"druid_worker_tasks", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void druid_historical_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("druid_historical"); // port 8083
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = druid_historical_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = druid_historical_loadstatus_mesg;
	strlcpy(actx->handler[0].key,"druid_historical_loadstatus", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void druid_broker_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("druid_broker"); // port 8082
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = druid_broker_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = druid_broker_loadstatus_mesg;
	strlcpy(actx->handler[0].key,"druid_broker_loadstatus", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
