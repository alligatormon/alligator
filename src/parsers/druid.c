#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include <query/query.h>
#include "common/selector.h"
#include "main.h"
#define d64	PRId64
#define DRUID_NAME_SIZE 255
#define DRUID_OTHER 0
#define DRUID_FLOAT 1

void druid_sql_execute_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t arr_size = json_array_size(root);
	json_t *header = json_array_get(root, 0);
	if (!header)
	{
		if (carg->log_level > 0)
			fprintf(stderr, "empty array\n");
		return;
	}

	query_node *qn = carg->data;
	size_t column_number = json_array_size(header);
	for (uint64_t i = 1; i < arr_size; i++)
	{
		json_t *row = json_array_get(root, i);

		alligator_ht *hash = alligator_ht_init(NULL);

		if (carg->ns)
			labels_hash_insert_nocache(hash, "dbname", carg->ns);

		for (uint64_t j = 0; j < column_number; j++)
		{
			json_t *colval_json = json_array_get(row, j);

			json_t *colname_json = json_array_get(header, j);
			char *colname = (char*)json_string_value(colname_json);
			int type = json_typeof(colval_json);

			//printf("type is %d/%d/%d/%d/\n", type, JSON_REAL, JSON_STRING, JSON_INTEGER);
			if (type == JSON_REAL)
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				double val = json_real_value(colval_json);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%lf'\n", val);
					qf->d = val;
					qf->type = DATATYPE_DOUBLE;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%lf'\n", colname, val);
					char field_str_num[20];
					snprintf(field_str_num, 19, "%lf", val);
					labels_hash_insert_nocache(hash, colname, field_str_num);
				}
			}
			else if (type == JSON_STRING)
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				char* sval = (char*)json_string_value(colval_json);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%s'\n", sval);
					qf->i = strtoll(sval, NULL, 10);
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%s'\n", colname, sval);
					labels_hash_insert_nocache(hash, colname, sval);
				}
			}
			else if (json_is_boolean(colval_json))
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				int64_t val = json_boolean_value(colval_json);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%"d64"'\n", val);
					qf->i = val;
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%"d64"'\n", colname, val);
					char *sval = val ? "true" : "false";
					labels_hash_insert_nocache(hash, colname, sval);
				}
			}
			else if (type == JSON_INTEGER)
			{
				query_field *qf = query_field_get(qn->qf_hash, colname);
				int64_t val = json_real_value(colval_json);
				if (qf)
				{
					if (carg->log_level > 2)
						printf("\tvalue '%"d64"'\n", val);
					qf->i = val;
					qf->type = DATATYPE_INT;
				}
				else
				{
					if (carg->log_level > 2)
						printf("\tfield '%s': '%"d64"'\n", colname, val);
					char field_str_num[20];
					snprintf(field_str_num, 19, "%"d64"", val);
					labels_hash_insert_nocache(hash, colname, field_str_num);
				}
			}
		}

		qn->labels = hash;
		qn->carg = carg;
		query_set_values(qn);
		labels_hash_free(hash);
	}

	json_decref(root);
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
	snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest->sin_port), qn->make);

	env_struct_push_alloc(carg->env, "Content-Length", "0");
	env_struct_push_alloc(carg->env, "Content-Type", "application/json");

//{"query":"SELECT *\nFROM \"wikiticker-2015-09-12-sampled\"","resultFormat":"array","header":true}
	json_t *post_data = json_object();
	json_t *post_query = json_string(strdup(qn->expr));
	json_t *resultFormat = json_string("array");
	json_t *header = json_true();

	json_array_object_insert(post_data, "query", post_query);
	json_array_object_insert(post_data, "resultFormat", resultFormat);
	json_array_object_insert(post_data, "header", header);

	char *post_query_s = json_dumps(post_data, JSON_INDENT(2));
	string *post_query_string = string_init_add_auto(post_query_s);
	json_decref(post_data);

	char *generated_query = gen_http_query(HTTP_POST, carg->query_url, "/druid/v2/sql", carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL, post_query_string);
	string_free(post_query_string);

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
			alligator_ht_foreach_arg(qds->hash, druid_queries_foreach, carg);
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
	trim(metrics);
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
		uint64_t schedstat = 0;
		if (!strcmp(scheduleStatus, "RUNNING"))
			schedstat = 1;

		metric_add_labels("druid_coordinator_compaction_latest_status_running", &schedstat, DATATYPE_UINT, carg, "dataSource", dataSource);

		const char *status_key;
		json_t *status_opt;
		json_object_foreach(status, status_key, status_opt)
		{
			int type = json_typeof(status_opt);
			if (type == JSON_INTEGER)
			{
				strlcpy(metric_name+43, status_key, DRUID_NAME_SIZE - 43);
				int64_t metric_value = json_real_value(status_opt);
				metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, "dataSource", dataSource);
			}
			else if (type == JSON_REAL)
			{
				strlcpy(metric_name+43, status_key, DRUID_NAME_SIZE - 43);
				double metric_value = json_real_value(status_opt);
				metric_add_labels(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "dataSource", dataSource);
			}
		}
	}
	json_decref(root);
}

void druid_coordinator_metadata_datasources_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t ds_num = json_array_size(root);
	for (uint64_t i = 0; i < ds_num; i++)
	{
		json_t *ds = json_array_get(root, i);
		json_t *name_json = json_object_get(ds, "name");
		char *name = (char*)json_string_value(name_json);
		if (!name)
			continue;

		json_t *segments = json_object_get(ds, "segments");
		size_t segments_num = json_array_size(segments);
		for (uint64_t j = 0; j < segments_num; j++)
		{
			json_t *segment = json_array_get(segments, j);

			json_t *identifier_json = json_object_get(segment, "identifier");
			char *identifier = (char*)json_string_value(identifier_json);
			if (!identifier)
				continue;

			json_t *dimensions_json = json_object_get(segment, "dimensions");
			char *dimensions = (char*)json_string_value(dimensions_json);
			int64_t val = 1;
			if (dimensions)
			{
				metric_add_labels3("druid_coordinator_metadata_datasources_dimensions", &val, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier, "dimensions", dimensions);
			}

			json_t *loadSpec = json_object_get(segment, "loadSpec");
			json_t *loadSpec_type_json = json_object_get(loadSpec, "type");
			char *loadSpec_type = (char*)json_string_value(loadSpec_type_json);
			if (loadSpec_type)
				metric_add_labels3("druid_coordinator_metadata_datasources_loadSpec_type", &val, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier, "type", loadSpec_type);

			json_t *shardSpec = json_object_get(segment, "shardSpec");
			json_t *shardSpec_type_json = json_object_get(shardSpec, "type");
			char *shardSpec_type = (char*)json_string_value(shardSpec_type_json);
			if (shardSpec_type)
				metric_add_labels3("druid_coordinator_metadata_datasources_shardSpec_type", &val, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier, "type", shardSpec_type);

			json_t *shardSpec_partitionNum = json_object_get(shardSpec, "partitionNum");
			int64_t partitionNum = json_integer_value(shardSpec_partitionNum);
			metric_add_labels2("druid_coordinator_metadata_datasources_partitionNum", &partitionNum, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier);

			json_t *shardSpec_partitions = json_object_get(shardSpec, "partitions");
			uint64_t partitions = json_integer_value(shardSpec_partitions);
			metric_add_labels2("druid_coordinator_metadata_datasources_partitions", &partitions, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier);

			json_t *size_json = json_object_get(segment, "size");
			uint64_t size = json_integer_value(size_json);
			metric_add_labels2("druid_coordinator_metadata_datasources_size", &size, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier);

			json_t *binaryVersion_json = json_object_get(segment, "binaryVersion");
			uint64_t binaryVersion = json_integer_value(binaryVersion_json);
			metric_add_labels2("druid_coordinator_metadata_datasources_binaryVersion", &binaryVersion, DATATYPE_INT, carg, "dataSource", name, "identifier", identifier);
		}
	}

	json_decref(root);
}

void druid_indexer_tasks_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t tasks_num = json_array_size(root);
	for (uint64_t j = 0; j < tasks_num; j++)
	{
		json_t *task = json_array_get(root, j);

		json_t *id_json = json_object_get(task, "id");
		char *id = (char*)json_string_value(id_json);
		if (!id)
			continue;

		json_t *groupId_json = json_object_get(task, "groupId");
		char *groupId = (char*)json_string_value(groupId_json);
		if (!groupId)
			continue;

		json_t *type_json = json_object_get(task, "type");
		char *type = (char*)json_string_value(type_json);
		if (!type)
			continue;

		json_t *status_json = json_object_get(task, "status");
		char *status = (char*)json_string_value(status_json);
		if (!status)
			continue;

		json_t *dataSource_json = json_object_get(task, "dataSource");
		char *dataSource = (char*)json_string_value(dataSource_json);
		if (!dataSource)
			continue;

		json_t *duration_json = json_object_get(task, "duration");
		double duration = json_real_value(duration_json);

		metric_add_labels5("druid_indexer_tasks_duration", &duration, DATATYPE_DOUBLE, carg, "dataSource", dataSource, "id", id, "groupId", groupId, "type", type, "status", status);
	}

	json_decref(root);
}

void druid_indexer_supervisor_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t supervisors_num = json_array_size(root);
	for (uint64_t j = 0; j < supervisors_num; j++)
	{
		json_t *supervisor = json_array_get(root, j);

		json_t *detailedState_json = json_object_get(supervisor, "detailedState");
		char *detailedState = (char*)json_string_value(detailedState_json);
		if (!detailedState)
			continue;

		json_t *spec_json = json_object_get(supervisor, "spec");
		char *spec = (char*)json_string_value(spec_json);
		if (!spec)
			continue;

		uint64_t healthy = 0;
		json_t *healthy_json = json_object_get(supervisor, "healthy");
		if (json_typeof(healthy_json) == JSON_TRUE)
			healthy = 1;

		metric_add_labels2("druid_indexer_supervisor_healthy", &healthy, DATATYPE_UINT, carg, "spec", spec, "detailedState", detailedState);
	}

	json_decref(root);
}

void druid_worker_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *host;
	json_t *status_opt;
	json_object_foreach(root, host, status_opt)
	{
		int64_t enabled = 0;
		if (json_typeof(status_opt) == JSON_TRUE)
			enabled = 1;

		metric_add_labels("druid_worker_enabled", &enabled, DATATYPE_INT, carg, "host", (char*)host);
	}

	json_decref(root);
}

void druid_historical_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "druid_historical", carg);
}

void druid_broker_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "druid_broker", carg);
}

string *druid_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, "1.0", env, proxy_settings, NULL), 0, 0);
}

string* druid_status_health_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/status/health", env, proxy_settings); }
string* druid_selfDiscovered_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/status/selfDiscovered/status", env, proxy_settings); }
string* druid_coordinator_metadata_datasources_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/metadata/datasources?full", env, proxy_settings); }
string* druid_coordinator_isleader_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/isLeader", env, proxy_settings); }
string* druid_coordinator_leader_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/leader", env, proxy_settings); }
string* druid_coordinator_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/loadstatus", env, proxy_settings); }
string* druid_coordinator_compaction_status_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/compaction/status", env, proxy_settings); }
string* druid_coordinator_servers_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/servers?simple", env, proxy_settings); }
string* druid_coordinator_loadqueue_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/loadqueue", env, proxy_settings); }
string* druid_indexer_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/indexer/v1/tasks", env, proxy_settings); }
string* druid_indexer_supervisor_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/indexer/v1/supervisor?full", env, proxy_settings); }
string* druid_worker_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/worker/v1/enabled", env, proxy_settings); } // port 8091
string* druid_historical_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/historical/v1/loadstatus", env, proxy_settings); } // port 8083
string* druid_broker_loadstatus_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/broker/v1/loadstatus", env, proxy_settings); } // port 8082

void druid_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("druid");
	actx->handlers = 10;
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

	actx->handler[7].name = druid_coordinator_metadata_datasources_handler;
	actx->handler[7].validator = NULL;
	actx->handler[7].mesg_func = druid_coordinator_metadata_datasources_mesg;
	strlcpy(actx->handler[7].key,"druid_coordinator_metadata_datasources", 255);

	actx->handler[8].name = druid_indexer_tasks_handler;
	actx->handler[8].validator = NULL;
	actx->handler[8].mesg_func = druid_indexer_tasks_mesg;
	strlcpy(actx->handler[8].key,"druid_indexer_tasks", 255);

	actx->handler[9].name = druid_indexer_supervisor_handler;
	actx->handler[9].validator = NULL;
	actx->handler[9].mesg_func = druid_indexer_supervisor_mesg;
	strlcpy(actx->handler[9].key,"druid_indexer_supervisor", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
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

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
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

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
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

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
