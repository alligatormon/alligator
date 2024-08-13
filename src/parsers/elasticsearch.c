#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/json_parser.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "parsers/elasticsearch.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/validator.h"
#include "main.h"

void elasticsearch_nodes_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	// get cluster name
	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	char* cluster_name = (char*)json_string_value(cluster_name_json);
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		es_data->cluster_name = strdup(cluster_name);

	// get _nodes
	int64_t _nodes_attr;
	json_t *_nodes = json_object_get(root, "_nodes");
	_nodes_attr = json_integer_value(json_object_get(_nodes, "total"));
	metric_add_labels("elasticsearch_nodes_total", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "successful"));
	metric_add_labels("elasticsearch_nodes_successful", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "failed"));
	metric_add_labels("elasticsearch_nodes_failed", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);

	// get nodes
	double dl;
	int64_t vl;
	char string2[255];
	char string3[255];
	char string4[255];
	char string5[255];
	strlcpy(string2, "elasticsearch_", 15);
	strlcpy(string3, "elasticsearch_", 15);
	strlcpy(string4, "elasticsearch_", 15);
	strlcpy(string5, "elasticsearch_", 15);
	size_t stringlen2;
	size_t stringlen3;
	size_t stringlen4;
	//size_t stringlen5;
	json_t *nodes = json_object_get(root, "nodes");
	const char *key;
	json_t *value;
	json_object_foreach(nodes, key, value)
	{
		json_t *name_json = json_object_get(value, "name");
		char* name = (char*)json_string_value(name_json);
		json_t *node_value;
		const char *node_key;
		json_object_foreach(value, node_key, node_value)
		{
			if (!strcmp(node_key, "adaptive_selection"))
				continue;
			stringlen2 = strlen(node_key);

			char copy_key[255];
			strlcpy(copy_key, node_key, 255);
			metric_name_normalizer(copy_key, stringlen2);

			strlcpy(string2+14, copy_key, stringlen2+1);
			strlcpy(string3+14, copy_key, stringlen2+1);
			strlcpy(string4+14, copy_key, stringlen2+1);
			strlcpy(string5+14, copy_key, stringlen2+1);

			int type = json_typeof(node_value);
			if (type == JSON_OBJECT)
			{
				json_t *node_value1;
				const char *node_key1;
				json_object_foreach(node_value, node_key1, node_value1)
				{
					stringlen3 = strlen(node_key1);

					char copy_key1[255];
					strlcpy(copy_key1, node_key1, 255);
					metric_name_normalizer(copy_key1, stringlen3);

					string3[stringlen2+14] = '_';
					string4[stringlen2+14] = '_';
					string5[stringlen2+14] = '_';
					strlcpy(string3+stringlen2+1+14, copy_key1, stringlen3+1);
					strlcpy(string4+stringlen2+1+14, copy_key1, stringlen3+1);
					strlcpy(string5+stringlen2+1+14, copy_key1, stringlen3+1);

					int type1 = json_typeof(node_value1);
					if (type1 == JSON_INTEGER)
					{
						vl = json_integer_value(node_value1);
						int8_t bytes = 0;
						char *tmp;
						if ((tmp = strstr(node_key1, "_in_bytes")))
						{
							strlcpy(string2+stringlen2+14, "_bytes", 7);
							bytes = 1;
							*tmp = 0;
						}
						metric_add_labels3(string2, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key1);

						if (bytes)
							string2[stringlen2+14] = 0;
					}
					else if (type1 == JSON_REAL)
					{
						dl = json_real_value(node_value1);
						metric_add_labels3(string2, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key1);
					}
					else if (type1 == JSON_OBJECT)
					{
						json_t *node_value2;
						const char *node_key2;
						json_object_foreach(node_value1, node_key2, node_value2)
						{

							int type2 = json_typeof(node_value2);
							if (type2 == JSON_INTEGER)
							{
								vl = json_integer_value(node_value2);
								int8_t bytes = 0;
								char *tmp;
								if ((tmp = strstr(node_key2, "_in_bytes")))
								{
									strlcpy(string3+stringlen3+stringlen2+1+14, "_bytes", 7);
									bytes = 1;
									*tmp = 0;
								}
								//metric_name_normalizer(string3, strlen(string3));
								metric_add_labels3(string3, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key2);

								if (bytes)
									string3[stringlen3+stringlen2+1+14] = 0;
							}
							else if (type2 == JSON_REAL)
							{
								dl = json_real_value(node_value2);
								//metric_name_normalizer(string3, strlen(string3));
								metric_add_labels3(string3, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key2);
							}
							else if (type2 == JSON_OBJECT)
							{
								stringlen4 = strlen(node_key2);

								char copy_key2[255];
								strlcpy(copy_key2, node_key2, 255);
								metric_name_normalizer(copy_key2, stringlen4);

								string4[stringlen2+stringlen3+14] = '_';
								string5[stringlen2+stringlen3+14] = '_';
								strlcpy(string4+stringlen3+stringlen2+1+14, copy_key2, stringlen4+1);
								strlcpy(string5+stringlen3+stringlen2+1+14, copy_key2, stringlen4+1);

								json_t *node_value3;
								const char *node_key3;
								json_object_foreach(node_value2, node_key3, node_value3)
								{
									int type3 = json_typeof(node_value3);
									if (type3 == JSON_INTEGER)
									{
										vl = json_integer_value(node_value3);
										int8_t bytes = 0;
										char *tmp;
										if ((tmp = strstr(node_key3, "_in_bytes")))
										{
											strlcpy(string4+stringlen4+stringlen3+stringlen2+1+14, "_bytes", 7);
											bytes = 1;
											*tmp = 0;
										}
										metric_add_labels3(string4, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key3);

										if (bytes)
											string4[stringlen4+stringlen3+stringlen2+1+14] = 0;
									}
									if (type3 == JSON_REAL)
									{
										dl = json_real_value(node_value3);
										metric_add_labels3(string4, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "name", name, "key", (char*)node_key3);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	carg->parser_status = 1;
	json_decref(root);
}

void elasticsearch_cluster_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "elasticsearch_cluster", carg);
	carg->parser_status = 1;
}

void elasticsearch_health_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	// get cluster name
	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	char* cluster_name = (char*)json_string_value(cluster_name_json);
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		es_data->cluster_name = strdup(cluster_name);

	json_t *value, *value2, *value3, *value4;
	const char *key, *key2, *key3, *key4;
	int64_t vl = 1;
	double dl;

	// get cluster status
	uint64_t green = 0;
	uint64_t red = 0;
	uint64_t yellow = 0;
	json_t *cluster_status_json = json_object_get(root, "status");
	char* cluster_status = (char*)json_string_value(cluster_status_json);
	if (!strcmp(cluster_status, "green"))
		green = 1;
	else if (!strcmp(cluster_status, "yellow"))
		yellow = 1;
	else if (!strcmp(cluster_status, "red"))
		red = 1;

	metric_add_labels2("elasticsearch_cluster_status", &green, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "green");
	metric_add_labels2("elasticsearch_cluster_status", &yellow, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "yellow");
	metric_add_labels2("elasticsearch_cluster_status", &red, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "red");

	json_t *indices = json_object_get(root, "indices");
	char string[1000];
	size_t stringlen;
	char string2[1000];
	size_t stringlen2;
	strlcpy(string, "elasticsearch_", 15);
	strlcpy(string2, "elasticsearch_indice_", 22);

	json_object_foreach(root, key, value)
	{
		size_t stringlen = strlen(key);
		strlcpy(string+14, key, stringlen+1);
		int type = json_typeof(value);
		if (type == JSON_INTEGER)
		{
			vl = json_integer_value(value);
			metric_add_labels(string, &vl, DATATYPE_INT, carg, "cluster", cluster_name);
		}
		else if (type == JSON_REAL)
		{
			dl = json_real_value(value);
			metric_add_labels(string, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name);
		}
	}

	strlcpy(string, "elasticsearch_indice_shard_", 28);

	json_object_foreach(indices, key, value)
	{
		uint64_t clst = 1;
		json_t *indice_status_json = json_object_get(value, "status");
		char* indice_status = (char*)json_string_value(indice_status_json);
		metric_add_labels3("elasticsearch_indice_status", &clst, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "status", indice_status);

		json_object_foreach(value, key2, value2)
		{
			// indices status
			stringlen2 = strlen(key2);
			strlcpy(string2+21, key2, stringlen2+1);
			int type = json_typeof(value2);
			if (type == JSON_INTEGER)
			{
				vl = json_integer_value(value2);
				metric_add_labels2(string2, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "indice", (char*)key);
			}
			else if (type == JSON_REAL)
			{
				dl = json_real_value(value2);
				metric_add_labels2(string2, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "indice", (char*)key);
			}
			json_object_foreach(value2, key3, value3)
			{
				clst = 1;
				uint64_t clfls = 0;
				json_t *shard_status_json = json_object_get(value3, "status");
				char* shard_status = (char*)json_string_value(shard_status_json);
				if (shard_status)
				{
					//printf("elasticsearch_indice_shard_status cluster:'%s', indice:'%s', shard:'%s', status:'%s'\n", cluster_name, key, key3, shard_status);
					metric_add_labels4("elasticsearch_indice_shard_status", &clst, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3, "status", shard_status);
				}

				json_t *shard_active_json = json_object_get(value3, "primary_active");
				if (json_typeof(shard_active_json) == JSON_TRUE)
					metric_add_labels3("elasticsearch_indice_shard_active", &clst, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
				else if (json_typeof(shard_active_json) == JSON_FALSE)
					metric_add_labels3("elasticsearch_indice_shard_active", &clfls, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);

				json_object_foreach(value3, key4, value4)
				{
					stringlen = strlen(key4);
					strlcpy(string+27, key4, stringlen+1);

					int type = json_typeof(value4);
					if (type == JSON_INTEGER)
					{
						vl = json_integer_value(value4);
						metric_add_labels3(string, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
					}
					else if (type == JSON_REAL)
					{
						dl = json_real_value(value4);
						metric_add_labels3(string, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
					}
				}
			}
		}
	}
	json_decref(root);
	carg->parser_status = 1;
}

void elasticsearch_index_handler(char *metrics, size_t size, context_arg *carg)
{

	// get cluster name
	//json_t *cluster_name_json = json_object_get(root, "cluster_name");
	//const char* cluster_name = json_string_value(cluster_name_json);
	//
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		return;

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char* cluster_name = es_data->cluster_name;

	// get _nodes
	int64_t _nodes_attr;
	json_t *_nodes = json_object_get(root, "_nodes");
	_nodes_attr = json_integer_value(json_object_get(_nodes, "total"));
	metric_add_labels("elasticsearch_shards_total", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "successful"));
	metric_add_labels("elasticsearch_shards_successful", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "failed"));
	metric_add_labels("elasticsearch_shards_failed", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);

	// get nodes
	double dl;
	int64_t vl;
	char string2[255];
	char string3[255];
	char string4[255];
	char string5[255];
	strlcpy(string2, "elasticsearch_", 15);
	strlcpy(string3, "elasticsearch_", 15);
	strlcpy(string4, "elasticsearch_", 15);
	strlcpy(string5, "elasticsearch_", 15);
	size_t stringlen2;
	size_t stringlen3;
	size_t stringlen4;
	json_t *nodes = json_object_get(root, "indices");
	const char *key;
	json_t *value;
	json_object_foreach(nodes, key, value)
	{
		json_t *node_value;
		const char *node_key;
		json_object_foreach(value, node_key, node_value)
		{
			if (!strcmp(node_key, "adaptive_selection"))
				continue;
			stringlen2 = strlen(node_key);
			strlcpy(string2+14, node_key, stringlen2+1);
			strlcpy(string3+14, node_key, stringlen2+1);
			strlcpy(string4+14, node_key, stringlen2+1);
			strlcpy(string5+14, node_key, stringlen2+1);

			int type = json_typeof(node_value);
			if (type == JSON_OBJECT)
			{
				json_t *node_value1;
				const char *node_key1;
				json_object_foreach(node_value, node_key1, node_value1)
				{
					stringlen3 = strlen(node_key1);
					string3[stringlen2+14] = '_';
					string4[stringlen2+14] = '_';
					string5[stringlen2+14] = '_';
					strlcpy(string3+stringlen2+1+14, node_key1, stringlen3+1);
					strlcpy(string4+stringlen2+1+14, node_key1, stringlen3+1);
					strlcpy(string5+stringlen2+1+14, node_key1, stringlen3+1);

					int type1 = json_typeof(node_value1);
					if (type1 == JSON_INTEGER)
					{
						vl = json_integer_value(node_value1);
						int8_t bytes = 0;
						char *tmp;
						if ((tmp = strstr(node_key1, "_in_bytes")))
						{
							strlcpy(string2+stringlen2+14, "_bytes", 7);
							bytes = 1;
							*tmp = 0;
						}
						metric_add_labels3(string2, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key1);

						if (bytes)
							string2[stringlen2+14] = 0;
					}
					else if (type1 == JSON_REAL)
					{
						dl = json_real_value(node_value1);
						metric_add_labels3(string2, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key1);
					}
					else if (type1 == JSON_OBJECT)
					{
						json_t *node_value2;
						const char *node_key2;
						json_object_foreach(node_value1, node_key2, node_value2)
						{

							int type2 = json_typeof(node_value2);
							if (type2 == JSON_INTEGER)
							{
								vl = json_integer_value(node_value2);
								int8_t bytes = 0;
								char *tmp;
								if ((tmp = strstr(node_key2, "_in_bytes")))
								{
									strlcpy(string3+stringlen3+stringlen2+1+14, "_bytes", 7);
									bytes = 1;
									*tmp = 0;
								}
								metric_add_labels3(string3, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key2);

								if (bytes)
									string3[stringlen3+stringlen2+1+14] = 0;
							}
							else if (type2 == JSON_REAL)
							{
								dl = json_real_value(node_value2);
								metric_add_labels3(string3, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key2);
							}
							else if (type2 == JSON_OBJECT)
							{
								stringlen4 = strlen(node_key2);
								string4[stringlen2+stringlen3+14] = '_';
								string5[stringlen2+stringlen3+14] = '_';
								strlcpy(string4+stringlen3+stringlen2+1+14, node_key2, stringlen4+1);
								strlcpy(string5+stringlen3+stringlen2+1+14, node_key2, stringlen4+1);

								json_t *node_value3;
								const char *node_key3;
								json_object_foreach(node_value2, node_key3, node_value3)
								{
									int type3 = json_typeof(node_value3);
									if (type3 == JSON_INTEGER)
									{
										vl = json_integer_value(node_value3);
										int8_t bytes = 0;
										char *tmp;
										if ((tmp = strstr(node_key3, "_in_bytes")))
										{
											strlcpy(string4+stringlen4+stringlen3+stringlen2+1+14, "_bytes", 7);
											bytes = 1;
											*tmp = 0;
										}
										metric_add_labels3(string4, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key3);

										if (bytes)
											string4[stringlen4+stringlen3+stringlen2+1+14] = 0;
									}
									if (type3 == JSON_REAL)
									{
										dl = json_real_value(node_value3);
										metric_add_labels3(string4, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "index", (char*)key, "key", (char*)node_key3);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	carg->parser_status = 1;
	json_decref(root);
}

void elasticsearch_settings_handler(char *metrics, size_t size, context_arg *carg)
{
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		return;

	char* cluster_name = es_data->cluster_name;

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	char string2[255];
	char string3[255];
	char string4[255];
	strlcpy(string2, "elasticsearch_settings_", 24);
	strlcpy(string3, "elasticsearch_settings_", 24);
	strlcpy(string4, "elasticsearch_settings_", 24);
	size_t stringlen2;
	size_t stringlen3;
	size_t stringlen4;

	json_t *value;
	const char *key;
	json_object_foreach(root, key, value)
	{
		json_t *nodes = json_object_get(value, "settings");

		json_t *node_value;
		const char *node_key;
		json_object_foreach(nodes, node_key, node_value)
		{
			stringlen2 = strlen(node_key);
			strlcpy(string2+23, node_key, stringlen2+1);
			strlcpy(string3+23, node_key, stringlen2+1);
			strlcpy(string4+23, node_key, stringlen2+1);
			//printf("\t\t%s:%s\n", node_key, string2);

			json_t *node_value2;
			const char *node_key2;
			json_object_foreach(node_value, node_key2, node_value2)
			{
				stringlen3 = strlen(node_key2);
				string3[stringlen2+23] = '_';
				string4[stringlen2+23] = '_';
				strlcpy(string3+stringlen2+1+23, node_key2, stringlen3+1);
				strlcpy(string4+stringlen2+1+23, node_key2, stringlen3+1);

				//printf("\t\t%s:%s\n", node_key2, string3);
				json_t *node_value3;
				const char *node_key3;
				json_object_foreach(node_value2, node_key3, node_value3)
				{
					stringlen4 = strlen(node_key3);
					string4[stringlen2+stringlen3+24] = '_';
					strlcpy(string4+stringlen2+stringlen3+1+24, node_key3, stringlen4+1);

					//json_t *val_json = json_object_get(node_value3, "cluster_name");
					char* val = (char*)json_string_value(node_value3);
					if (val)
					{
						int64_t vl;
						//printf("\t\t\t%s:%s:%s\n", node_key3, string4, val);
						if (!strncmp(val, "true", 4))
							vl = 1;
						else if (!strncmp(val, "false", 5))
							vl = 0;

						metric_add_labels2(string4, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "index", (char*)key);
					}
				}
			}
		}
	}
	json_decref(root);
	carg->parser_status = 1;
}

void elasticsearch_response_catch(char *metrics, size_t size, context_arg *carg)
{
	if (1)
	{
		puts("elasticsearch_response_catch");
		json_error_t error;
		json_t *root = json_loads(metrics, 0, &error);
		if (!root)
		{
			fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
			return;
		}

		json_t *jtook = json_object_get(root, "took");
		int64_t took = json_integer_value(jtook);

		json_t *jerrors = json_object_get(root, "errors");
		int jsontype = json_typeof(jerrors);
		int errors = 0;
		if (jsontype == JSON_TRUE)
			errors = 1;

		json_t *jitems = json_object_get(root, "items");
		uint64_t items_size = json_array_size(jitems);

		printf("took is %"PRId64"\n", took);
		printf("errors is %d\n", errors);
		printf("items is %"PRIu64"\n", items_size);

		//if (ac->log_level > 2)
		if (2)
		{
			for (uint64_t i = 0; i < items_size; i++)
			{
				json_t *item = json_array_get(jitems, i);
				json_t *index= json_object_get(item, "index");

				json_t *jid = json_object_get(index, "_id");
				const char *id = json_string_value(jid);

				json_t *jseq_no = json_object_get(index, "_seq_no");
				int64_t seq_no = json_integer_value(jseq_no);

				json_t *j_index = json_object_get(index, "_index");
				const char *_index = json_string_value(j_index);

				json_t *jresult = json_object_get(index, "result");
				const char *result = json_string_value(jresult);

				json_t *jshards = json_object_get(index, "_shards");

				json_t *jtotal = json_object_get(jshards, "total");
				int64_t total = json_integer_value(jtotal);

				json_t *jsuccessful = json_object_get(jshards, "successful");
				int64_t successful = json_integer_value(jsuccessful);

				json_t *jfailed = json_object_get(jshards, "failed");
				int64_t failed = json_integer_value(jfailed);

				printf("\tid: %s, index: %s, result: %s, seq_no: %"PRId64", shards:[total: %"PRId64", successful: %"PRId64", failed: %"PRId64"\n", id, _index, result, seq_no, total, successful, failed);
			}
		}

		if (!errors)
			carg->parser_status = 1;

		json_decref(root);
		return;
	}
}

string *elastic_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

string* elasticsearch_nodes_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return elastic_gen_url(hi, "/_nodes/stats", env, proxy_settings); }
string* elasticsearch_cluster_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return elastic_gen_url(hi, "/_cluster/stats", env, proxy_settings); }
string* elasticsearch_health_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return elastic_gen_url(hi, "/_cluster/health?level=shards", env, proxy_settings); }
string* elasticsearch_index_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return elastic_gen_url(hi, "/_stats", env, proxy_settings); }
string* elasticsearch_settings_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return elastic_gen_url(hi, "/_settings", env, proxy_settings); }

void elasticsearch_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	elastic_settings *data = calloc(1, sizeof(*data));

	actx->key = strdup("elasticsearch");
	actx->handlers = 5;
	actx->data = data;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = elasticsearch_nodes_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = elasticsearch_nodes_mesg;
	strlcpy(actx->handler[0].key,"elasticsearch_nodes", 255);

	actx->handler[1].name = elasticsearch_cluster_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = elasticsearch_cluster_mesg;
	strlcpy(actx->handler[1].key,"elasticsearch_cluster", 255);

	actx->handler[2].name = elasticsearch_health_handler;
	actx->handler[2].validator = NULL;
	actx->handler[2].mesg_func = elasticsearch_health_mesg;
	strlcpy(actx->handler[2].key,"elasticsearch_health", 255);

	actx->handler[3].name = elasticsearch_index_handler;
	actx->handler[3].validator = NULL;
	actx->handler[3].mesg_func = elasticsearch_index_mesg;
	strlcpy(actx->handler[3].key,"elasticsearch_index", 255);

	actx->handler[4].name = elasticsearch_settings_handler;
	actx->handler[4].validator = NULL;
	actx->handler[4].mesg_func = elasticsearch_settings_mesg;
	strlcpy(actx->handler[4].key,"elasticsearch_settings", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
