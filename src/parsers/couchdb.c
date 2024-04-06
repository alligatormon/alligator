#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_parser.h"
#include "common/http.h"
#include "main.h"
#define COUCHDB_LEN 1000

void couchdb_stats_handler(char *metrics, size_t size, context_arg *carg)
{
	//json_parser_entry(metrics, 0, NULL, "couchdb", carg);
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *db_key;
	json_t *db_value;
	char metric_name[COUCHDB_LEN];
	strlcpy(metric_name, "couchdb_", 9);

	json_object_foreach(root, db_key, db_value)
	{
		if (carg->log_level > 0)
			printf("db_key %s\n", db_key);

		const char *metric_key;
		json_t *metric_opts;
		json_object_foreach(db_value, metric_key, metric_opts)
		{
			strlcpy(metric_name+8, metric_key, COUCHDB_LEN - 8);

			if (carg->log_level > 0)
			{
				printf("\tmetric_key %s\n", metric_key);
				printf("\tmetric_name %s\n", metric_name);
			}

			const char *tag_key;
			json_t *tag_value;
			json_object_foreach(metric_opts, tag_key, tag_value)
			{
				if (carg->log_level > 0)
					printf("\t\ttag %s\n", tag_key);

				int type = json_typeof(tag_value);
				if (type == JSON_REAL)
				{
					double metric_value = json_real_value(tag_value);
					metric_add_labels2(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "type", (char*)tag_key, "db", (char*)db_key);
				}
				else if (type == JSON_INTEGER)
				{
					int64_t metric_value = json_integer_value(tag_value);
					metric_add_labels2(metric_name, &metric_value, DATATYPE_INT, carg, "type", (char*)tag_key, "db", (char*)db_key);
				}
				else if (type == JSON_TRUE)
				{
					int64_t metric_value = 1;
					metric_add_labels2(metric_name, &metric_value, DATATYPE_INT, carg, "type", (char*)tag_key, "db", (char*)db_key);
				}
				else if (type == JSON_FALSE)
				{
					int64_t metric_value = 0;
					metric_add_labels2(metric_name, &metric_value, DATATYPE_INT, carg, "type", (char*)tag_key, "db", (char*)db_key);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void couchdb_config_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *context_key;
	json_t *context_value;
	char metric_name[COUCHDB_LEN];
	strlcpy(metric_name, "couchdb_config_", 16);

	json_object_foreach(root, context_key, context_value)
	{
		if (carg->log_level > 0)
			printf("context_key %s\n", context_key);

		const char *config_key;
		json_t *config_opts;
		json_object_foreach(context_value, config_key, config_opts)
		{
			strlcpy(metric_name+15, config_key, COUCHDB_LEN - 8);

			if (carg->log_level > 0)
			{
				printf("\tconfig_key %s\n", config_key);
				printf("\tmetric_name %s\n", metric_name);
			}

			int type = json_typeof(config_opts);
			if (type == JSON_STRING)
			{
				char* metric_value_string = (char*)json_string_value(config_opts);
				if (isdigit(*metric_value_string))
				{
					int64_t metric_value = strtoll(metric_value_string, NULL, 10);
					metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, "context", (char*)context_key);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void couchdb_active_tasks_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metric_name[COUCHDB_LEN];
	strlcpy(metric_name, "couchdb_active_task_", 21);

	size_t tasks_num = json_array_size(root);
	for (uint64_t i = 0; i < tasks_num; i++)
	{
		json_t *task = json_array_get(root, i);

		json_t *replication_id_json = json_object_get(task, "replication_id");
		char *replication_id = (char*)json_string_value(replication_id_json);

		json_t *database_json = json_object_get(task, "database");
		char *database = (char*)json_string_value(database_json);

		json_t *type_json = json_object_get(task, "type");
		char *type = (char*)json_string_value(type_json);

		json_t *target_json = json_object_get(task, "target");
		char *target = (char*)json_string_value(target_json);

		json_t *pid_json = json_object_get(task, "pid");
		char *pid = (char*)json_string_value(pid_json);

		json_t *design_document_json = json_object_get(task, "design_document");
		char *design_document = (char*)json_string_value(design_document_json);

		const char *context_key;
		json_t *context_value;
		json_object_foreach(task, context_key, context_value)
		{
			strlcpy(metric_name + 20, context_key, COUCHDB_LEN - 20);

			alligator_ht *hash = alligator_ht_init(NULL);

			if (replication_id)
				labels_hash_insert_nocache(hash, "replication_id", replication_id);
			if (database)
				labels_hash_insert_nocache(hash, "database", database);
			if (type)
				labels_hash_insert_nocache(hash, "type", type);
			if (target)
				labels_hash_insert_nocache(hash, "target", target);
			if (pid)
				labels_hash_insert_nocache(hash, "pid", pid);
			if (design_document)
				labels_hash_insert_nocache(hash, "design_document", design_document);

			int type = json_typeof(context_value);
			if (type == JSON_REAL)
			{
				double metric_value = json_real_value(context_value);
				metric_add(metric_name, hash, &metric_value, DATATYPE_DOUBLE, carg);
			}
			else if (type == JSON_INTEGER)
			{
				int64_t metric_value = json_integer_value(context_value);
				metric_add(metric_name, hash, &metric_value, DATATYPE_INT, carg);
			}
			else if (type == JSON_TRUE)
			{
				int64_t metric_value = 1;
				metric_add(metric_name, hash, &metric_value, DATATYPE_INT, carg);
			}
			else if (type == JSON_FALSE)
			{
				int64_t metric_value = 0;
				metric_add(metric_name, hash, &metric_value, DATATYPE_INT, carg);
			}
		}

		replication_id = NULL;
		database = NULL;
		type = NULL;
		target = NULL;
		pid = NULL;
		design_document = NULL;
	}

	json_decref(root);
	carg->parser_status = 1;
}

void couchdb_db_stats(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metric_name[COUCHDB_LEN];
	const char *metric_key;
	json_t *metric_opts;
	strlcpy(metric_name, "couchdb_db_", 12);

	json_t *db_name = json_object_get(root, "db_name");
	char *db_name_string = (char*)json_string_value(db_name);

	json_object_foreach(root, metric_key, metric_opts)
	{
		strlcpy(metric_name+11, metric_key, COUCHDB_LEN - 11);

		if (carg->log_level > 0)
		{
			printf("\tmetric_key %s\n", metric_key);
			printf("\tmetric_name %s\n", metric_name);
		}

		int type = json_typeof(metric_opts);
		if (type == JSON_REAL)
		{
			double metric_value = json_real_value(metric_opts);
			metric_add_labels(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "db", (char*)db_name_string);
		}
		else if (type == JSON_INTEGER)
		{
			int64_t metric_value = json_integer_value(metric_opts);
			metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, "db", (char*)db_name_string);
		}
		else if (type == JSON_TRUE)
		{
			int64_t metric_value = 1;
			metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, "db", (char*)db_name_string);
		}
		else if (type == JSON_FALSE)
		{
			int64_t metric_value = 0;
			metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, "db", (char*)db_name_string);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void couchdb_all_dbs_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	size_t db_num = json_array_size(root);
	for (uint64_t i = 0; i < db_num; i++)
	{
		json_t *db = json_array_get(root, i);
		char *db_name = (char*)json_string_value(db);

		char *key = malloc(255);
		snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), db_name);

		char *generated_query = gen_http_query(HTTP_GET, "/", db_name, carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL, NULL);
		try_again(carg, generated_query, strlen(generated_query), couchdb_db_stats, "couchdb_db_stats", NULL, key, NULL);
	}

	json_decref(root);
	carg->parser_status = 1;
}

string *couchdb_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
}

string* couchdb_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchdb_gen_url(hi, "/_stats", env, proxy_settings); }
string* couchdb_config_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchdb_gen_url(hi, "/_config", env, proxy_settings); }
string* couchdb_active_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchdb_gen_url(hi, "/_active_tasks", env, proxy_settings); }
string* couchdb_all_dbs_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchdb_gen_url(hi, "/_all_dbs", env, proxy_settings); }

void couchdb_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("couchdb");
	actx->handlers = 4;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = couchdb_stats_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = couchdb_stats_mesg;
	strlcpy(actx->handler[0].key,"couchdb_stats", 255);

	actx->handler[1].name = couchdb_config_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = couchdb_config_mesg;
	strlcpy(actx->handler[1].key,"couchdb_config", 255);

	actx->handler[2].name = couchdb_active_tasks_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = couchdb_active_tasks_mesg;
	strlcpy(actx->handler[2].key,"couchdb_active_tasks", 255);

	actx->handler[3].name = couchdb_all_dbs_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = couchdb_all_dbs_mesg;
	strlcpy(actx->handler[3].key,"couchdb_all_dbs", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

