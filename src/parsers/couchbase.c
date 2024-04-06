#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "main.h"
#define COUCHBASE_LEN 1000

void couchbase_bucket_metric_get(context_arg *carg, char *metric_name, const char *bucketType, const char *name, const char *uuid, json_t *value)
{
	if (carg->log_level > 1)
		printf("metric name is %s\n", metric_name);

	int type = json_typeof(value);
	if (type == JSON_REAL)
	{
		double metric_value = json_real_value(value);
		metric_add_labels3(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid);
	}
	else if (type == JSON_INTEGER)
	{
		int64_t metric_value = json_integer_value(value);
		metric_add_labels3(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid);
	}
	else if (type == JSON_TRUE)
	{
		int64_t metric_value = 1;
		metric_add_labels3(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid);
	}
	else if (type == JSON_FALSE)
	{
		int64_t metric_value = 0;
		metric_add_labels3(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid);
	}
}

void couchbase_bucket_node_metric_get(context_arg *carg, char *metric_name, const char *bucketType, const char *name, const char *uuid, const char *nodeUUID, const char *hostname, json_t *value)
{
	if (carg->log_level > 1)
		printf("metric name is %s\n", metric_name);

	int type = json_typeof(value);
	if (type == JSON_REAL)
	{
		double metric_value = json_real_value(value);
		metric_add_labels5(metric_name, &metric_value, DATATYPE_DOUBLE, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid, "nodeUUID", (char*)nodeUUID, "hostname", (char*)hostname);
	}
	else if (type == JSON_INTEGER)
	{
		int64_t metric_value = json_integer_value(value);
		metric_add_labels5(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid, "nodeUUID", (char*)nodeUUID, "hostname", (char*)hostname);
	}
	else if (type == JSON_TRUE)
	{
		int64_t metric_value = 1;
		metric_add_labels5(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid, "nodeUUID", (char*)nodeUUID, "hostname", (char*)hostname);
	}
	else if (type == JSON_FALSE)
	{
		int64_t metric_value = 0;
		metric_add_labels5(metric_name, &metric_value, DATATYPE_INT, carg, "name", (char*)name, "bucketType", (char*)bucketType, "uuid", (char*)uuid, "nodeUUID", (char*)nodeUUID, "hostname", (char*)hostname);
	}
}

void couchbase_bucket_nodes_stats(char *metrics, size_t size, context_arg *carg)
{
	char *bucket = carg->data;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		if (bucket)
			free(bucket);
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *op = json_object_get(root, "op");
	if (!op)
	{
		json_decref(root);
		if (bucket)
			free(bucket);
		return;
	}

	json_t *samples = json_object_get(op, "samples");
	if (!samples)
	{
		json_decref(root);
		if (bucket)
			free(bucket);
		return;
	}

	json_t *hostname_json = json_object_get(root, "hostname");
	char *hostname = (char*)json_string_value(hostname_json);

	char metric_name[COUCHBASE_LEN];
	uint64_t metric_size;

	char *ctx = NULL;

	// cases:
	// hostname && bucket ! @index|@query|@fts == bucket node stats
	// hostname && bucket ~ @index|@query|@fts == current context node stats
	// !hostname && bucket ! @index|@query|@fts == bucket stats
	// !hostname && bucket ~ @index|@query|@fts == current context stats
	if (hostname && *bucket != '@')
	{
		ctx = "bucket";
		strlcpy(metric_name, "couchbase_bucket_node_stat_", COUCHBASE_LEN);
		metric_size = 27;
	}
	else if (hostname && *bucket == '@')
	{
		ctx = "context";
		strlcpy(metric_name, "couchbase_node_stat_", COUCHBASE_LEN);
		metric_size = 20;
	}
	else if (bucket && *bucket != '@')
	{
		ctx = "bucket";
		strlcpy(metric_name, "couchbase_bucket_stat_", COUCHBASE_LEN);
		metric_size = 22;
	}
	else if (!bucket && !hostname)
	{
		strlcpy(metric_name, "couchbase_stat_", COUCHBASE_LEN);
		metric_size = 15;
	}
	else
	{
		if (carg->log_level > 0)
			printf("couchbase_bucket_nodes_stats error: unknown case\n");

		if (bucket)
			free(bucket);

		json_decref(root);

		return;
	}

	const char *sample_key;
	json_t *sample_value;
	json_object_foreach(samples, sample_key, sample_value)
	{
		json_t *sample = json_array_get(sample_value, 0);
		// case if no stats
		if (!sample)
			continue;

		strlcpy(metric_name + metric_size, sample_key, COUCHBASE_LEN - metric_size);
		
		int type = json_typeof(sample);
		if (type == JSON_REAL)
		{
			double metric_value = json_real_value(sample);
			if (hostname)
				metric_add_labels2(metric_name, &metric_value, DATATYPE_DOUBLE, carg, ctx, bucket, "hostname", (char*)hostname);
			else if (bucket)
				metric_add_labels(metric_name, &metric_value, DATATYPE_DOUBLE, carg, ctx, bucket);
			else
				metric_add_auto(metric_name, &metric_value, DATATYPE_DOUBLE, carg);
		}
		else if (type == JSON_INTEGER)
		{
			int64_t metric_value = json_integer_value(sample);
			if (hostname)
				metric_add_labels2(metric_name, &metric_value, DATATYPE_INT, carg, ctx, bucket, "hostname", (char*)hostname);
			else if (bucket)
				metric_add_labels(metric_name, &metric_value, DATATYPE_INT, carg, ctx, bucket);
			else
				metric_add_auto(metric_name, &metric_value, DATATYPE_INT, carg);
		}
	}

	if (bucket)
		free(bucket);
	json_decref(root);
	carg->parser_status = 1;
}

void couchbase_buckets_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metric_name[COUCHBASE_LEN];
	strlcpy(metric_name, "couchbase_bucket_", 18);

	size_t bucket_num = json_array_size(root);
	for (uint64_t i = 0; i < bucket_num; i++)
	{
		json_t *bucket = json_array_get(root, i);

		json_t *bucketType_json = json_object_get(bucket, "bucketType");
		char *bucketType = (char*)json_string_value(bucketType_json);

		json_t *name_json = json_object_get(bucket, "name");
		char *name = (char*)json_string_value(name_json);
		size_t name_length = json_string_length(name_json);

		json_t *uuid_json = json_object_get(bucket, "uuid");
		char *uuid = (char*)json_string_value(uuid_json);

		// generate subquery to bucket stats
		char name_encoded[name_length + 256];
		uint64_t name_encoded_length = urlencode(name_encoded, name, name_length);

		string *bucket_nodes_query_uri = string_new();
		string_cat(bucket_nodes_query_uri, "/pools/default/buckets/", strlen("/pools/default/buckets/"));
		string_cat(bucket_nodes_query_uri, name_encoded, name_encoded_length);
		string_cat(bucket_nodes_query_uri, "/stats", strlen("/stats"));

		char *generated_query = gen_http_query(HTTP_GET, carg->query_url, bucket_nodes_query_uri->s, carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL, NULL);

		char *key = malloc(255);
		snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), bucket_nodes_query_uri->s);
		string_free(bucket_nodes_query_uri);

		try_again(carg, generated_query, strlen(generated_query), couchbase_bucket_nodes_stats, "couchbase_bucket_nodes_stats", NULL, key, strdup(name));
		// end

		const char *bucket_key;
		json_t *bucket_value;
		json_object_foreach(bucket, bucket_key, bucket_value)
		{
			if (carg->log_level > 0)
				printf("bucket_key %s\n", bucket_key);

			uint64_t bucket_key_len = strlcpy(metric_name + 17, bucket_key, COUCHBASE_LEN - 17);
			
			couchbase_bucket_metric_get(carg, metric_name, bucketType, name, uuid, bucket_value);

			const char *bucket_context_key;
			json_t *bucket_context_value;
			json_object_foreach(bucket_value, bucket_context_key, bucket_context_value)
			{
				if (carg->log_level > 0)
					printf("bucket_context_key %s\n", bucket_context_key);

				strlcpy(metric_name + 17 + bucket_key_len, "_", COUCHBASE_LEN - 17 - bucket_key_len);
				strlcpy(metric_name + 18 + bucket_key_len, bucket_context_key, COUCHBASE_LEN - 18 - bucket_key_len);
				couchbase_bucket_metric_get(carg, metric_name, bucketType, name, uuid, bucket_context_value);
			}
		}

		json_t *nodes = json_object_get(bucket, "nodes");
		size_t nodes_num = json_array_size(nodes);
		for (uint64_t j = 0; j < nodes_num; j++)
		{
			json_t *node = json_array_get(nodes, j);

			json_t *nodeUUID_json = json_object_get(node, "nodeUUID");
			char *nodeUUID = (char*)json_string_value(nodeUUID_json);

			json_t *hostname_json = json_object_get(node, "hostname");
			char *hostname = (char*)json_string_value(hostname_json);
			size_t hostname_length = json_string_length(hostname_json);

			// generate subquery to bucket nodes
			char name_encoded[name_length + 256];
			uint64_t name_encoded_length = urlencode(name_encoded, name, name_length);

			char hostname_encoded[hostname_length + 256];
			uint64_t hostname_encoded_length = urlencode(hostname_encoded, hostname, hostname_length);

			string *bucket_nodes_query_uri = string_new();
			string_cat(bucket_nodes_query_uri, "/pools/default/buckets/", strlen("/pools/default/buckets/"));
			string_cat(bucket_nodes_query_uri, name_encoded, name_encoded_length);
			string_cat(bucket_nodes_query_uri, "/nodes/", strlen("/nodes/"));
			string_cat(bucket_nodes_query_uri, hostname_encoded, hostname_encoded_length);
			string_cat(bucket_nodes_query_uri, "/stats", strlen("/stats"));

			char *generated_query = gen_http_query(HTTP_GET, carg->query_url, bucket_nodes_query_uri->s, carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL, NULL);

			char *key = malloc(255);
			snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), bucket_nodes_query_uri->s);
			string_free(bucket_nodes_query_uri);

			try_again(carg, generated_query, strlen(generated_query), couchbase_bucket_nodes_stats, "couchbase_bucket_nodes_stats", NULL, key, strdup(name));
			// end

			const char *node_key;
			json_t *node_value;
			json_object_foreach(node, node_key, node_value)
			{
				if (carg->log_level > 0)
					printf("bucket_name %s, node %s, hostname %s, nodeUUID %s\n", name, node_key, hostname, nodeUUID);

				strlcpy(metric_name + 17, "node_", COUCHBASE_LEN - 17);
				uint64_t node_key_len = strlcpy(metric_name + 22, node_key, COUCHBASE_LEN - 22);
				
				couchbase_bucket_node_metric_get(carg, metric_name, bucketType, name, uuid, nodeUUID, hostname, node_value);

				const char *nopts_key;
				json_t *nopts_value;
				json_object_foreach(node_value, nopts_key, nopts_value)
				{
					if (carg->log_level > 0)
						printf("\tbucket_name %s, node %s, node opts key %s, hostname %s, nodeUUID %s\n", name, node_key, nopts_key, hostname, nodeUUID);

					strlcpy(metric_name + 22 + node_key_len, "_", COUCHBASE_LEN - 22 - node_key_len);
					strlcpy(metric_name + 23 + node_key_len, nopts_key, COUCHBASE_LEN - 23 - node_key_len);
					
					couchbase_bucket_node_metric_get(carg, metric_name, bucketType, name, uuid, nodeUUID, hostname, nopts_value);
				}
			}
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void couchbase_tasks_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char metric_name[COUCHBASE_LEN];
	strlcpy(metric_name, "couchbase_task_", 16);

	size_t task_num = json_array_size(root);
	for (uint64_t i = 0; i < task_num; i++)
	{
		json_t *task = json_array_get(root, i);

		json_t *json_statusId = json_object_get(task, "statusId");
		char *statusId = (char*)json_string_value(json_statusId);

		json_t *json_type = json_object_get(task, "type");
		char *type = (char*)json_string_value(json_type);

		json_t *json_subtype = json_object_get(task, "subtype");
		char *subtype = (char*)json_string_value(json_subtype);
		
		json_t *json_status = json_object_get(task, "status");
		char *status = (char*)json_string_value(json_status);

		json_t *json_errorMessage = json_object_get(task, "errorMessage");
		char *errorMessage = (char*)json_string_value(json_errorMessage);

		json_t *json_bucket = json_object_get(task, "bucket");
		char *bucket = (char*)json_string_value(json_bucket);

		json_t *json_designDocument = json_object_get(task, "designDocument");
		char *designDocument = (char*)json_string_value(json_designDocument);

		json_t *json_pid = json_object_get(task, "pid");
		char *pid = (char*)json_string_value(json_pid);

		const char *task_key;
		json_t *task_value;
		json_object_foreach(task, task_key, task_value)
		{
			strlcpy(metric_name + 15, task_key, COUCHBASE_LEN - 15);

			alligator_ht *hash = alligator_ht_init(NULL);

			if (statusId)
				labels_hash_insert_nocache(hash, "statusId", statusId);
			if (type)
				labels_hash_insert_nocache(hash, "type", type);
			if (subtype)
				labels_hash_insert_nocache(hash, "subtype", subtype);
			if (status)
				labels_hash_insert_nocache(hash, "status", status);
			if (bucket)
				labels_hash_insert_nocache(hash, "bucket", bucket);
			if (designDocument)
				labels_hash_insert_nocache(hash, "designDocument", designDocument);
			if (pid)
				labels_hash_insert_nocache(hash, "pid", pid);
			if (errorMessage)
			{
				labels_hash_insert_nocache(hash, "errorMessage", errorMessage);
				int64_t metric_value = 1;
				metric_add("couchbase_task_error", hash, &metric_value, DATATYPE_INT, carg);
				continue;
			}

			int type = json_typeof(task_value);
			if (type == JSON_REAL)
			{
				double metric_value = json_real_value(task_value);
				metric_add(metric_name, hash, &metric_value, DATATYPE_DOUBLE, carg);
			}
			else if (type == JSON_INTEGER)
			{
				int64_t metric_value = json_integer_value(task_value);
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
			else
				labels_hash_free(hash);
		}

		statusId = NULL;
		type = NULL;
		subtype = NULL;
		status = NULL;
		errorMessage = NULL;
		bucket = NULL;
		designDocument = NULL;
		pid = NULL;
	}
	json_decref(root);
	carg->parser_status = 1;
}

void couchbase_nodes_list(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *nodes = json_object_get(root, "nodes");
	if (!nodes)
	{
		json_decref(root);
		if (carg->log_level > 0)
			printf("couchbase_nodes_list error: no 'nodes'\n");
		return;
	}

	size_t nodes_num = json_array_size(nodes);
	for (uint64_t i = 0; i < nodes_num; i++)
	{
		json_t *node = json_array_get(nodes, i);
		json_t *hostname_json = json_object_get(node, "hostname");
		char *hostname = (char*)json_string_value(hostname_json);
		uint64_t hostname_length = json_string_length(hostname_json);
		if (!hostname)
		{
			if (carg->log_level > 0)
			{
				char *dvalue = json_dumps(node, JSON_INDENT(2));
				printf("couchbase_nodes_list warning: no 'hostname' in:\n%s\n", dvalue);
				free(dvalue);
			}

			continue;
		}

		char hostname_encoded[hostname_length + 256];
		uint64_t hostname_encoded_length = urlencode(hostname_encoded, hostname, hostname_length);

		char *subqueries[] = { "@index", "@query", "@fts" };
		uint64_t subqueries_len = 3;
		// generate subquery to query by node stats
		for (uint64_t j = 0; j < subqueries_len; j++)
		{
			string *query_string = string_new();
			string_cat(query_string, "/pools/default/buckets/", strlen("/pools/default/buckets/"));
			string_cat(query_string, subqueries[j], strlen(subqueries[j]));
			string_cat(query_string, "/nodes/", strlen("/nodes/"));
			string_cat(query_string, hostname_encoded, hostname_encoded_length);
			string_cat(query_string, "/stats", strlen("/stats"));

			char *generated_query = gen_http_query(HTTP_GET, carg->query_url, query_string->s, carg->host, "alligator", NULL, 1, "1.0", carg->env, NULL, NULL);

			char *key = malloc(255);
			snprintf(key, 255, "(tcp://%s:%u)/%s", carg->host, htons(carg->dest.sin_port), query_string->s);
			string_null(query_string);

			try_again(carg, generated_query, strlen(generated_query), couchbase_bucket_nodes_stats, "couchbase_bucket_nodes_stats", NULL, key, strdup(subqueries[j]));
			// end
			string_free(query_string);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

string *couchbase_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
}

string* couchbase_buckets_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets", env, proxy_settings); }
string* couchbase_tasks_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/tasks", env, proxy_settings); }
string* couchbase_query_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets/@query/stats", env, proxy_settings); }
string* couchbase_index_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets/@index/stats", env, proxy_settings); }
string* couchbase_fts_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets/@fts/stats", env, proxy_settings); }
string* couchbase_cbas_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets/@cbas/stats", env, proxy_settings); }
string* couchbase_eventing_stats_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/default/buckets/@eventing/stats", env, proxy_settings); }
string* couchbase_nodes_list_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return couchbase_gen_url(hi, "/pools/nodes", env, proxy_settings); }

void couchbase_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("couchbase");
	actx->handlers = 8;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = couchbase_buckets_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = couchbase_buckets_mesg;
	strlcpy(actx->handler[0].key,"couchbase_buckets", 255);

	actx->handler[1].name = couchbase_tasks_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = couchbase_tasks_mesg;
	strlcpy(actx->handler[1].key,"couchbase_tasks", 255);

	actx->handler[2].name = couchbase_bucket_nodes_stats;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = couchbase_query_stats_mesg;
	strlcpy(actx->handler[2].key,"couchbase_query_stats", 255);

	actx->handler[3].name = couchbase_bucket_nodes_stats;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = couchbase_index_stats_mesg;
	strlcpy(actx->handler[3].key,"couchbase_index_stats", 255);

	actx->handler[4].name = couchbase_bucket_nodes_stats;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = couchbase_fts_stats_mesg;
	strlcpy(actx->handler[4].key,"couchbase_fts_stats", 255);

	actx->handler[5].name = couchbase_bucket_nodes_stats;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = couchbase_cbas_stats_mesg;
	strlcpy(actx->handler[5].key,"couchbase_cbas_stats", 255);

	actx->handler[6].name = couchbase_bucket_nodes_stats;
	actx->handler[6].validator = NULL;
	actx->handler[6].mesg_func = couchbase_eventing_stats_mesg;
	strlcpy(actx->handler[6].key,"couchbase_eventing_stats", 255);

	actx->handler[7].name = couchbase_nodes_list;
	actx->handler[7].validator = NULL;
	actx->handler[7].mesg_func = couchbase_nodes_list_mesg;
	strlcpy(actx->handler[7].key,"couchbase_nodes_list", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

