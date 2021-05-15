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
string* druid_coordinator_compaction_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return druid_gen_url(hi, "/druid/coordinator/v1/compaction/status", env, proxy_settings); }
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
	actx->handlers = 2;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = druid_status_health_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = druid_status_health_mesg;
	strlcpy(actx->handler[0].key,"druid_status_health", 255);

	actx->handler[1].name = druid_selfDiscovered_status_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = druid_selfDiscovered_status_mesg;
	strlcpy(actx->handler[1].key,"druid_selfDiscovered_status", 255);

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
