#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/client_info.h"
void elasticsearch_nodes_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);

	// get cluster name
	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	const char* cluster_name = json_string_value(cluster_name_json);

	// get _nodes
	int64_t _nodes_attr;
	json_t *_nodes = json_object_get(root, "_nodes");
	_nodes_attr = json_integer_value(json_object_get(_nodes, "total"));
	metric_add_labels("elasticsearch_nodes_total", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "successful"));
	metric_add_labels("elasticsearch_nodes_successful", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "failed"));
	metric_add_labels("elasticsearch_nodes_failed", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);

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
		const char* name = json_string_value(name_json);
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
						metric_add_labels3(string2, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "name", name, "key", node_key1);

						if (bytes)
							string2[stringlen2+14] = 0;
					}
					else if (type1 == JSON_REAL)
					{
						dl = json_real_value(node_value1);
						metric_add_labels3(string2, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "name", name, "key", node_key1);
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
								metric_name_normalizer(string3, strlen(string3));
								metric_add_labels3(string3, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "name", name, "key", node_key2);

								if (bytes)
									string3[stringlen3+stringlen2+1+14] = 0;
							}
							else if (type2 == JSON_REAL)
							{
								dl = json_real_value(node_value2);
								metric_name_normalizer(string3, strlen(string3));
								metric_add_labels3(string3, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "name", name, "key", node_key2);
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
										metric_add_labels3(string4, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "name", name, "key", node_key3);

										if (bytes)
											string4[stringlen4+stringlen3+stringlen2+1+14] = 0;
									}
									if (type3 == JSON_REAL)
									{
										dl = json_real_value(node_value3);
										metric_add_labels3(string4, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "name", name, "key", node_key3);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	json_decref(root);
}

void elasticsearch_cluster_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_parser_entry(metrics, 0, NULL, "elasticsearch_cluster");
}

void elasticsearch_health_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);

	json_t *value, *value2, *value3, *value4;
	const char *key, *key2, *key3, *key4;
	int64_t vl = 1;
	double dl;

	// get cluster name
	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	const char* cluster_name = json_string_value(cluster_name_json);

	// get cluster status
	json_t *cluster_status_json = json_object_get(root, "status");
	const char* cluster_status = json_string_value(cluster_status_json);
	metric_add_labels2("elasticsearch_cluster_status", &vl, DATATYPE_INT, 0, "cluster", cluster_name, "status", cluster_status);

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
			metric_add_labels(string, &vl, DATATYPE_INT, 0, "cluster", cluster_name);
		}
		else if (type == JSON_REAL)
		{
			dl = json_real_value(value);
			metric_add_labels(string, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name);
		}
		
	}

	strlcpy(string, "elasticsearch_indice_shard_", 28);

	json_object_foreach(indices, key, value)
	{
		uint64_t clst = 1;
		json_t *indice_status_json = json_object_get(value, "status");
		const char* indice_status = json_string_value(indice_status_json);
		metric_add_labels3("elasticsearch_indice_status", &clst, DATATYPE_UINT, 0, "cluster", cluster_name, "indice", key, "status", indice_status);

		json_object_foreach(value, key2, value2)
		{
			// indices status
			stringlen2 = strlen(key2);
			strlcpy(string2+21, key2, stringlen2+1);
			int type = json_typeof(value2);
			if (type == JSON_INTEGER)
			{
				vl = json_integer_value(value2);
				metric_add_labels2(string2, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "indice", key);
			}
			else if (type == JSON_REAL)
			{
				dl = json_real_value(value2);
				metric_add_labels2(string2, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "indice", key);
			}
			json_object_foreach(value2, key3, value3)
			{
				clst = 1;
				uint64_t clfls = 0;
				json_t *shard_status_json = json_object_get(value3, "status");
				const char* shard_status = json_string_value(shard_status_json);
				if (shard_status)
					metric_add_labels4("elasticsearch_indice_shard_status", &clst, DATATYPE_UINT, 0, "cluster", cluster_name, "indice", key, "shard", key3, "status", shard_status);

				json_t *shard_active_json = json_object_get(value3, "primary_active");
				if (json_typeof(shard_active_json) == JSON_TRUE)
					metric_add_labels3("elasticsearch_indice_shard_active", &clst, DATATYPE_UINT, 0, "cluster", cluster_name, "indice", key, "shard", key3);
				else if (json_typeof(shard_active_json) == JSON_FALSE)
					metric_add_labels3("elasticsearch_indice_shard_active", &clfls, DATATYPE_UINT, 0, "cluster", cluster_name, "indice", key, "shard", key3);

				json_object_foreach(value3, key4, value4)
				{
					stringlen = strlen(key4);
					strlcpy(string+27, key4, stringlen+1);

					int type = json_typeof(value4);
					if (type == JSON_INTEGER)
					{
						vl = json_integer_value(value4);
						metric_add_labels3(string, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "indice", key, "shard", key3);
					}
					else if (type == JSON_REAL)
					{
						dl = json_real_value(value4);
						metric_add_labels3(string, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "indice", key, "shard", key3);
					}
				}
			}
		}
	}
	json_decref(root);
}

void elasticsearch_index_handler(char *metrics, size_t size, client_info *cinfo)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);

	// get cluster name
	//json_t *cluster_name_json = json_object_get(root, "cluster_name");
	//const char* cluster_name = json_string_value(cluster_name_json);
	const char* cluster_name = "cluster";
	printf("cluster_name is %s\n", cluster_name);

	// get _nodes
	int64_t _nodes_attr;
	json_t *_nodes = json_object_get(root, "_nodes");
	_nodes_attr = json_integer_value(json_object_get(_nodes, "total"));
	metric_add_labels("elasticsearch_shards_total", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "successful"));
	metric_add_labels("elasticsearch_shards_successful", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "failed"));
	metric_add_labels("elasticsearch_shards_failed", &_nodes_attr, DATATYPE_INT, 0, "cluster", cluster_name);

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
						metric_add_labels3(string2, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "index", key, "key", node_key1);

						if (bytes)
							string2[stringlen2+14] = 0;
					}
					else if (type1 == JSON_REAL)
					{
						dl = json_real_value(node_value1);
						metric_add_labels3(string2, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "index", key, "key", node_key1);
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
								metric_add_labels3(string3, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "index", key, "key", node_key2);

								if (bytes)
									string3[stringlen3+stringlen2+1+14] = 0;
							}
							else if (type2 == JSON_REAL)
							{
								dl = json_real_value(node_value2);
								metric_add_labels3(string3, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "index", key, "key", node_key2);
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
										metric_add_labels3(string4, &vl, DATATYPE_INT, 0, "cluster", cluster_name, "index", key, "key", node_key3);

										if (bytes)
											string4[stringlen4+stringlen3+stringlen2+1+14] = 0;
									}
									if (type3 == JSON_REAL)
									{
										dl = json_real_value(node_value3);
										metric_add_labels3(string4, &dl, DATATYPE_DOUBLE, 0, "cluster", cluster_name, "index", key, "key", node_key3);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	json_decref(root);
}
