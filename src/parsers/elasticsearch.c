#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/json_query.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "parsers/elasticsearch.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "main.h"

int elasticsearch_check_for_error(context_arg *carg, json_t *root) {
	json_t *error_json = json_object_get(root, "error");
	if (!error_json)
		return 0;
	char *dvalue = json_dumps(error_json, JSON_INDENT(2));
	carglog(carg, L_ERROR, "elasticsearch found error in ES response:\n%s\n", dvalue);
	free(dvalue);
	return 1;
}

static inline size_t es_copy_bytes(size_t buf_size, size_t offset, size_t src_len)
{
	if (offset >= buf_size)
		return 0;
	size_t rem = buf_size - offset;
	size_t copy = src_len < (rem - 1) ? src_len : (rem - 1);
	return copy + 1;
}

static inline void es_poke(char *buf, size_t bufsz, size_t idx, char ch)
{
	if (idx < bufsz)
		buf[idx] = ch;
}

static inline void es_cat(char *buf, size_t bufsz, size_t off, const char *src)
{
	if (!src || off >= bufsz)
		return;
	strlcpy(buf + off, src, bufsz - off);
}

/* Collapse runs of 2+ '_' to one '_', and drop a trailing '_'.
 * Never remove a single '_' between alphanumeric tokens
 * (e.g. segments_segment, task_cancellation must stay intact). */
static void es_collapse_underscores(char *s)
{
	if (!s || !*s)
		return;
	char *r = s;
	char *w = s;
	int prev_us = 0;
	while (*r)
	{
		if (*r == '_')
		{
			if (!prev_us)
			{
				*w++ = '_';
				prev_us = 1;
			}
			/* else: skip extra '_' in a run (__ / ____ → single _) */
		}
		else
		{
			*w++ = *r;
			prev_us = 0;
		}
		r++;
	}
	*w = '\0';
	if (w > s && w[-1] == '_')
		w[-1] = '\0';
}

static void es_emit_name(char *dst, size_t dstsz, const char *src)
{
	strlcpy(dst, src, dstsz);
	es_collapse_underscores(dst);
}

static inline void elasticsearch_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "Elasticsearch exported metric value.");
}

static int es_skip_leaf_key(const char *key)
{
	return !key || !strcmp(key, "timestamp") || !strcmp(key, "max_unsafe_auto_id_timestamp");
}

static void es_emit_traffic_light3(context_arg *carg, const char *metric, const char *status,
	char *l1, char *v1, char *l2, char *v2)
{
	uint64_t green = (status && !strcmp(status, "green")) ? 1 : 0;
	uint64_t yellow = (status && !strcmp(status, "yellow")) ? 1 : 0;
	uint64_t red = (status && !strcmp(status, "red")) ? 1 : 0;
	elasticsearch_metric_set(carg, metric);
	metric_add_labels3((char *)metric, &green, DATATYPE_UINT, carg, l1, v1, l2, v2, "status", "green");
	metric_add_labels3((char *)metric, &yellow, DATATYPE_UINT, carg, l1, v1, l2, v2, "status", "yellow");
	metric_add_labels3((char *)metric, &red, DATATYPE_UINT, carg, l1, v1, l2, v2, "status", "red");
}

static void es_emit_traffic_light4(context_arg *carg, const char *metric, const char *status,
	char *l1, char *v1, char *l2, char *v2, char *l3, char *v3)
{
	uint64_t green = (status && !strcmp(status, "green")) ? 1 : 0;
	uint64_t yellow = (status && !strcmp(status, "yellow")) ? 1 : 0;
	uint64_t red = (status && !strcmp(status, "red")) ? 1 : 0;
	elasticsearch_metric_set(carg, metric);
	metric_add_labels4((char *)metric, &green, DATATYPE_UINT, carg, l1, v1, l2, v2, l3, v3, "status", "green");
	metric_add_labels4((char *)metric, &yellow, DATATYPE_UINT, carg, l1, v1, l2, v2, l3, v3, "status", "yellow");
	metric_add_labels4((char *)metric, &red, DATATYPE_UINT, carg, l1, v1, l2, v2, l3, v3, "status", "red");
}

static void es_strip_in_bytes(const char *key, char *key_buf, size_t key_bufsz, int *bytes)
{
	const char *bytes_suffix = strstr(key, "_in_bytes");
	*bytes = bytes_suffix ? 1 : 0;
	size_t key_size = bytes_suffix ? (size_t)(bytes_suffix - key) : strlen(key);
	size_t key_copy = key_size < (key_bufsz - 1) ? key_size : (key_bufsz - 1);
	strlcpy(key_buf, key, key_copy + 1);
}

static void elasticsearch_emit_node_roles(context_arg *carg, char *cluster_name, char *name, json_t *roles)
{
	size_t n = json_array_size(roles);
	int64_t one = 1;
	for (size_t i = 0; i < n; i++)
	{
		json_t *role_json = json_array_get(roles, i);
		const char *role = json_string_value(role_json);
		if (!role)
			continue;
		char role_buf[255];
		strlcpy(role_buf, role, sizeof(role_buf));
		metric_label_value_validator_normalizer(role_buf, strlen(role_buf));
		elasticsearch_metric_set(carg, "elasticsearch_node_role");
		metric_add_labels3("elasticsearch_node_role", &one, DATATYPE_INT, carg,
			"cluster", cluster_name, "name", name, "role", role_buf);
	}
}

static void elasticsearch_emit_fs_paths(context_arg *carg, char *cluster_name, char *name, json_t *data)
{
	if (!json_is_array(data))
		return;
	size_t n = json_array_size(data);
	for (size_t i = 0; i < n; i++)
	{
		json_t *entry = json_array_get(data, i);
		if (!json_is_object(entry))
			continue;
		const char *path = json_string_value(json_object_get(entry, "path"));
		const char *mount = json_string_value(json_object_get(entry, "mount"));
		const char *type = json_string_value(json_object_get(entry, "type"));
		if (!path)
			path = "";
		if (!mount)
			mount = "";
		if (!type)
			type = "";

		int64_t total = json_integer_value(json_object_get(entry, "total_in_bytes"));
		int64_t free = json_integer_value(json_object_get(entry, "free_in_bytes"));
		int64_t available = json_integer_value(json_object_get(entry, "available_in_bytes"));

		elasticsearch_metric_set(carg, "elasticsearch_fs_path_total_bytes");
		metric_add_labels5("elasticsearch_fs_path_total_bytes", &total, DATATYPE_INT, carg,
			"cluster", cluster_name, "name", name, "path", (char *)path, "mount", (char *)mount, "type", (char *)type);
		elasticsearch_metric_set(carg, "elasticsearch_fs_path_free_bytes");
		metric_add_labels5("elasticsearch_fs_path_free_bytes", &free, DATATYPE_INT, carg,
			"cluster", cluster_name, "name", name, "path", (char *)path, "mount", (char *)mount, "type", (char *)type);
		elasticsearch_metric_set(carg, "elasticsearch_fs_path_available_bytes");
		metric_add_labels5("elasticsearch_fs_path_available_bytes", &available, DATATYPE_INT, carg,
			"cluster", cluster_name, "name", name, "path", (char *)path, "mount", (char *)mount, "type", (char *)type);
	}
}

static void elasticsearch_emit_jvm_gc(context_arg *carg, char *cluster_name, char *name, json_t *collectors)
{
	if (!json_is_object(collectors))
		return;
	const char *gc_name;
	json_t *gc_obj;
	json_object_foreach(collectors, gc_name, gc_obj)
	{
		if (!json_is_object(gc_obj))
			continue;
		char gc_label[255];
		strlcpy(gc_label, gc_name, sizeof(gc_label));
		metric_label_value_validator_normalizer(gc_label, strlen(gc_label));

		const char *field;
		json_t *field_val;
		json_object_foreach(gc_obj, field, field_val)
		{
			if (json_typeof(field_val) != JSON_INTEGER)
				continue;
			int64_t vl = json_integer_value(field_val);
			elasticsearch_metric_set(carg, "elasticsearch_jvm_gc_collectors");
			metric_add_labels4("elasticsearch_jvm_gc_collectors", &vl, DATATYPE_INT, carg,
				"cluster", cluster_name, "name", name, "gc", gc_label, "key", (char *)field);
		}
	}
}

static void elasticsearch_emit_jvm_pools(context_arg *carg, char *cluster_name, char *name, json_t *pools)
{
	if (!json_is_object(pools))
		return;
	const char *pool_name;
	json_t *pool_obj;
	json_object_foreach(pools, pool_name, pool_obj)
	{
		if (!json_is_object(pool_obj))
			continue;
		char pool_label[255];
		strlcpy(pool_label, pool_name, sizeof(pool_label));
		metric_label_value_validator_normalizer(pool_label, strlen(pool_label));

		const char *field;
		json_t *field_val;
		json_object_foreach(pool_obj, field, field_val)
		{
			if (json_typeof(field_val) != JSON_INTEGER)
				continue;
			char key_buf[255];
			int bytes = 0;
			es_strip_in_bytes(field, key_buf, sizeof(key_buf), &bytes);
			(void)bytes;
			int64_t vl = json_integer_value(field_val);
			elasticsearch_metric_set(carg, "elasticsearch_jvm_mem_pools_bytes");
			metric_add_labels4("elasticsearch_jvm_mem_pools_bytes", &vl, DATATYPE_INT, carg,
				"cluster", cluster_name, "name", name, "pool", pool_label, "key", key_buf);
		}
	}
}

static void es_emit_labeled_number(context_arg *carg, char *metric_src, void *val, int8_t dtype,
	char *cluster_name, char *name_or_index, const char *name_label, char *key_buf)
{
	char metric[255];
	es_emit_name(metric, sizeof(metric), metric_src);
	elasticsearch_metric_set(carg, metric);
	metric_add_labels3(metric, val, dtype, carg, "cluster", cluster_name, (char *)name_label, name_or_index, "key", key_buf);
}

static void es_json_delete_strings_and_underscore_keys(json_t *obj)
{
	if (!json_is_object(obj))
		return;

	const char *key;
	json_t *val;
	void *iter = json_object_iter(obj);
	while (iter)
	{
		key = json_object_iter_key(iter);
		val = json_object_iter_value(iter);
		void *next = json_object_iter_next(obj, iter);

		if (key && key[0] == '_')
		{
			json_object_del(obj, key);
		}
		else if (json_is_string(val))
		{
			json_object_del(obj, key);
		}
		else if (json_is_object(val))
		{
			es_json_delete_strings_and_underscore_keys(val);
		}
		else if (json_is_array(val))
		{
			size_t n = json_array_size(val);
			for (size_t i = 0; i < n; i++)
			{
				json_t *el = json_array_get(val, i);
				if (json_is_object(el))
					es_json_delete_strings_and_underscore_keys(el);
			}
		}

		iter = next;
	}
}

static void elasticsearch_cluster_stats_filter(json_t *root)
{
	json_t *nodes = json_object_get(root, "nodes");
	if (json_is_object(nodes))
		json_object_del(nodes, "network_types");

	json_t *indices = json_object_get(root, "indices");
	if (json_is_object(indices))
	{
		json_t *segments = json_object_get(indices, "segments");
		if (json_is_object(segments))
			json_object_del(segments, "max_unsafe_auto_id_timestamp");
	}

	es_json_delete_strings_and_underscore_keys(root);
}

static int es_parse_watermark_value(const char *s, double *percent, int64_t *bytes, int *is_percent)
{
	if (!s || !*s)
		return 0;

	while (*s && isspace((unsigned char)*s))
		s++;

	char *end = NULL;
	errno = 0;
	double v = strtod(s, &end);
	if (end == s || errno == ERANGE)
		return 0;

	while (*end && isspace((unsigned char)*end))
		end++;

	if (*end == '%')
	{
		*percent = v;
		*is_percent = 1;
		return 1;
	}

	if (*end == '\0')
	{
		if (v > 0.0 && v <= 1.0)
		{
			*percent = v * 100.0;
			*is_percent = 1;
			return 1;
		}
		if (v > 1.0 && v <= 100.0)
		{
			*percent = v;
			*is_percent = 1;
			return 1;
		}
		return 0;
	}

	double mult = 1.0;
	if (!strncasecmp(end, "pb", 2))
		mult = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
	else if (!strncasecmp(end, "tb", 2))
		mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
	else if (!strncasecmp(end, "gb", 2))
		mult = 1024.0 * 1024.0 * 1024.0;
	else if (!strncasecmp(end, "mb", 2))
		mult = 1024.0 * 1024.0;
	else if (!strncasecmp(end, "kb", 2))
		mult = 1024.0;
	else if (!strncasecmp(end, "b", 1))
		mult = 1.0;
	else
		return 0;

	*bytes = (int64_t)(v * mult);
	*is_percent = 0;
	return 1;
}

static const char *es_settings_pick(json_t *transient, json_t *persistent, json_t *defaults, const char *key)
{
	json_t *layers[3] = { transient, persistent, defaults };
	for (int i = 0; i < 3; i++)
	{
		if (!json_is_object(layers[i]))
			continue;
		json_t *v = json_object_get(layers[i], key);
		const char *s = json_string_value(v);
		if (s && *s)
			return s;
	}
	return NULL;
}

void elasticsearch_nodes_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_nodes_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
		return;
	}

	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	char* cluster_name = (char*)json_string_value(cluster_name_json);
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		es_data->cluster_name = strdup(cluster_name);

	int64_t _nodes_attr;
	json_t *_nodes = json_object_get(root, "_nodes");
	_nodes_attr = json_integer_value(json_object_get(_nodes, "total"));
	elasticsearch_metric_set(carg, "elasticsearch_nodes_total");
	metric_add_labels("elasticsearch_nodes_total", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "successful"));
	elasticsearch_metric_set(carg, "elasticsearch_nodes_successful");
	metric_add_labels("elasticsearch_nodes_successful", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	_nodes_attr = json_integer_value(json_object_get(_nodes, "failed"));
	elasticsearch_metric_set(carg, "elasticsearch_nodes_failed");
	metric_add_labels("elasticsearch_nodes_failed", &_nodes_attr, DATATYPE_INT, carg, "cluster", cluster_name);

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
			if (!strcmp(node_key, "adaptive_selection") || !strcmp(node_key, "resource_usage_stats"))
				continue;

			if (!strcmp(node_key, "roles") && json_is_array(node_value))
			{
				elasticsearch_emit_node_roles(carg, cluster_name, name, node_value);
				continue;
			}

			stringlen2 = strlen(node_key);

			char copy_key[255];
			strlcpy(copy_key, node_key, 255);
			metric_name_normalizer(copy_key, strlen(copy_key));

			size_t copy2 = es_copy_bytes(sizeof(string2), 14, stringlen2);
			size_t copy3 = es_copy_bytes(sizeof(string3), 14, stringlen2);
			size_t copy4 = es_copy_bytes(sizeof(string4), 14, stringlen2);
			size_t copy5 = es_copy_bytes(sizeof(string5), 14, stringlen2);
			if (copy2) strlcpy(string2+14, copy_key, copy2);
			if (copy3) strlcpy(string3+14, copy_key, copy3);
			if (copy4) strlcpy(string4+14, copy_key, copy4);
			if (copy5) strlcpy(string5+14, copy_key, copy5);

			int type = json_typeof(node_value);
			if (type == JSON_OBJECT)
			{
				if (!strcmp(node_key, "fs"))
					elasticsearch_emit_fs_paths(carg, cluster_name, name, json_object_get(node_value, "data"));

				json_t *node_value1;
				const char *node_key1;
				json_object_foreach(node_value, node_key1, node_value1)
				{
					stringlen3 = strlen(node_key1);

					char copy_key1[255];
					strlcpy(copy_key1, node_key1, 255);
					metric_name_normalizer(copy_key1, strlen(copy_key1));

					es_poke(string3, sizeof(string3), stringlen2+14, '_');
					es_poke(string4, sizeof(string4), stringlen2+14, '_');
					es_poke(string5, sizeof(string5), stringlen2+14, '_');
					size_t off = stringlen2 + 1 + 14;
					size_t c3 = es_copy_bytes(sizeof(string3), off, stringlen3);
					size_t c4 = es_copy_bytes(sizeof(string4), off, stringlen3);
					size_t c5 = es_copy_bytes(sizeof(string5), off, stringlen3);
					if (c3) strlcpy(string3 + off, copy_key1, c3);
					if (c4) strlcpy(string4 + off, copy_key1, c4);
					if (c5) strlcpy(string5 + off, copy_key1, c5);

					if (!strcmp(node_key, "jvm") && !strcmp(node_key1, "gc"))
						elasticsearch_emit_jvm_gc(carg, cluster_name, name, json_object_get(node_value1, "collectors"));
					if (!strcmp(node_key, "jvm") && !strcmp(node_key1, "mem"))
						elasticsearch_emit_jvm_pools(carg, cluster_name, name, json_object_get(node_value1, "pools"));

					int type1 = json_typeof(node_value1);
					if ((type1 == JSON_INTEGER || type1 == JSON_TRUE || type1 == JSON_FALSE) && !es_skip_leaf_key(node_key1))
					{
						if (type1 == JSON_INTEGER)
							vl = json_integer_value(node_value1);
						else
							vl = (type1 == JSON_TRUE) ? 1 : 0;
						int bytes = 0;
						char key_buf[255];
						es_strip_in_bytes(node_key1, key_buf, sizeof(key_buf), &bytes);
						if (bytes)
							es_cat(string2, sizeof(string2), stringlen2+14, "_bytes");
						es_emit_labeled_number(carg, string2, &vl, DATATYPE_INT, cluster_name, name, "name", key_buf);
						if (bytes)
							es_poke(string2, sizeof(string2), stringlen2+14, 0);
					}
					else if (type1 == JSON_REAL && !es_skip_leaf_key(node_key1))
					{
						dl = json_real_value(node_value1);
						es_emit_labeled_number(carg, string2, &dl, DATATYPE_DOUBLE, cluster_name, name, "name", (char*)node_key1);
					}
					else if (type1 == JSON_OBJECT)
					{
						json_t *node_value2;
						const char *node_key2;
						json_object_foreach(node_value1, node_key2, node_value2)
						{
							int type2 = json_typeof(node_value2);
							if ((type2 == JSON_INTEGER || type2 == JSON_TRUE || type2 == JSON_FALSE) && !es_skip_leaf_key(node_key2))
							{
								if (type2 == JSON_INTEGER)
									vl = json_integer_value(node_value2);
								else
									vl = (type2 == JSON_TRUE) ? 1 : 0;
								int bytes = 0;
								char key_buf[255];
								es_strip_in_bytes(node_key2, key_buf, sizeof(key_buf), &bytes);
								if (bytes)
									es_cat(string3, sizeof(string3), stringlen3+stringlen2+1+14, "_bytes");
								es_emit_labeled_number(carg, string3, &vl, DATATYPE_INT, cluster_name, name, "name", key_buf);
								if (bytes)
									es_poke(string3, sizeof(string3), stringlen3+stringlen2+1+14, 0);
							}
							else if (type2 == JSON_REAL && !es_skip_leaf_key(node_key2))
							{
								dl = json_real_value(node_value2);
								es_emit_labeled_number(carg, string3, &dl, DATATYPE_DOUBLE, cluster_name, name, "name", (char*)node_key2);
							}
							else if (type2 == JSON_OBJECT)
							{
								stringlen4 = strlen(node_key2);

								char copy_key2[255];
								strlcpy(copy_key2, node_key2, 255);
								metric_name_normalizer(copy_key2, strlen(copy_key2));

								/* third-level: '_' after key2+'_'+key3 (idx +15), third key starts at +16 */
								es_poke(string4, sizeof(string4), stringlen2+stringlen3+15, '_');
								es_poke(string5, sizeof(string5), stringlen2+stringlen3+15, '_');
								size_t off3 = stringlen3 + stringlen2 + 1 + 15;
								size_t c4 = es_copy_bytes(sizeof(string4), off3, stringlen4);
								size_t c5 = es_copy_bytes(sizeof(string5), off3, stringlen4);
								if (c4) strlcpy(string4 + off3, copy_key2, c4);
								if (c5) strlcpy(string5 + off3, copy_key2, c5);

								json_t *node_value3;
								const char *node_key3;
								json_object_foreach(node_value2, node_key3, node_value3)
								{
									int type3 = json_typeof(node_value3);
									if ((type3 == JSON_INTEGER || type3 == JSON_TRUE || type3 == JSON_FALSE) && !es_skip_leaf_key(node_key3))
									{
										if (type3 == JSON_INTEGER)
											vl = json_integer_value(node_value3);
										else
											vl = (type3 == JSON_TRUE) ? 1 : 0;
										int bytes = 0;
										char key_buf[255];
										es_strip_in_bytes(node_key3, key_buf, sizeof(key_buf), &bytes);
										if (bytes)
											es_cat(string4, sizeof(string4), stringlen4+stringlen3+stringlen2+1+15, "_bytes");
										es_emit_labeled_number(carg, string4, &vl, DATATYPE_INT, cluster_name, name, "name", key_buf);
										if (bytes)
											es_poke(string4, sizeof(string4), stringlen4+stringlen3+stringlen2+1+15, 0);
									}
									else if (type3 == JSON_REAL && !es_skip_leaf_key(node_key3))
									{
										dl = json_real_value(node_value3);
										es_emit_labeled_number(carg, string4, &dl, DATATYPE_DOUBLE, cluster_name, name, "name", (char*)node_key3);
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
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_cluster_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
		return;
	}

	elasticsearch_cluster_stats_filter(root);
	carg->parser_status = json_query(NULL, root, "elasticsearch_cluster", carg, carg->pquery, carg->pquery_size);
	json_decref(root);
}

void elasticsearch_health_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_health_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
		return;
	}

	json_t *cluster_name_json = json_object_get(root, "cluster_name");
	char* cluster_name = (char*)json_string_value(cluster_name_json);
	if (!cluster_name)
	{
		carglog(carg, L_ERROR, "elasticsearch_health_handler: no field 'cluster_name':\n'%s'\n", metrics);
		json_decref(root);
		return;
	}
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		es_data->cluster_name = strdup(cluster_name);

	json_t *value, *value2, *value3, *value4;
	const char *key, *key2, *key3, *key4;
	int64_t vl = 1;
	double dl;

	uint64_t green = 0;
	uint64_t red = 0;
	uint64_t yellow = 0;
	json_t *cluster_status_json = json_object_get(root, "status");
	char* cluster_status = (char*)json_string_value(cluster_status_json);
	if (cluster_status && !strcmp(cluster_status, "green"))
		green = 1;
	else if (cluster_status && !strcmp(cluster_status, "yellow"))
		yellow = 1;
	else if (cluster_status && !strcmp(cluster_status, "red"))
		red = 1;

	elasticsearch_metric_set(carg, "elasticsearch_cluster_status");
	metric_add_labels2("elasticsearch_cluster_status", &green, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "green");
	metric_add_labels2("elasticsearch_cluster_status", &yellow, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "yellow");
	metric_add_labels2("elasticsearch_cluster_status", &red, DATATYPE_UINT, carg, "cluster", cluster_name, "status", "red");

	json_t *timed_out_json = json_object_get(root, "timed_out");
	int64_t timed_out = (json_typeof(timed_out_json) == JSON_TRUE) ? 1 : 0;
	elasticsearch_metric_set(carg, "elasticsearch_timed_out");
	metric_add_labels("elasticsearch_timed_out", &timed_out, DATATYPE_INT, carg, "cluster", cluster_name);

	json_t *indices = json_object_get(root, "indices");
	char string[1000];
	size_t stringlen;
	char string2[1000];
	size_t stringlen2;
	strlcpy(string, "elasticsearch_", 15);
	strlcpy(string2, "elasticsearch_indice_", 22);

	json_object_foreach(root, key, value)
	{
		size_t keylen = strlen(key);
		size_t copy = es_copy_bytes(sizeof(string), 14, keylen);
		if (copy)
			strlcpy(string+14, key, copy);
		int type = json_typeof(value);
		if (type == JSON_INTEGER)
		{
			vl = json_integer_value(value);
			elasticsearch_metric_set(carg, string);
			metric_add_labels(string, &vl, DATATYPE_INT, carg, "cluster", cluster_name);
		}
		else if (type == JSON_REAL)
		{
			dl = json_real_value(value);
			elasticsearch_metric_set(carg, string);
			metric_add_labels(string, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name);
		}
		else if (type == JSON_TRUE || type == JSON_FALSE)
		{
			if (!strcmp(key, "timed_out"))
				continue;
			vl = (type == JSON_TRUE) ? 1 : 0;
			elasticsearch_metric_set(carg, string);
			metric_add_labels(string, &vl, DATATYPE_INT, carg, "cluster", cluster_name);
		}
	}

	strlcpy(string, "elasticsearch_indice_shard_", 28);

	json_object_foreach(indices, key, value)
	{
		json_t *indice_status_json = json_object_get(value, "status");
		char* indice_status = (char*)json_string_value(indice_status_json);
		es_emit_traffic_light3(carg, "elasticsearch_indice_status", indice_status,
			"cluster", cluster_name, "indice", (char*)key);

		json_object_foreach(value, key2, value2)
		{
			stringlen2 = strlen(key2);
			size_t copy = es_copy_bytes(sizeof(string2), 21, stringlen2);
			if (copy)
				strlcpy(string2+21, key2, copy);
			int type = json_typeof(value2);
			if (type == JSON_INTEGER)
			{
				vl = json_integer_value(value2);
				elasticsearch_metric_set(carg, string2);
				metric_add_labels2(string2, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "indice", (char*)key);
			}
			else if (type == JSON_REAL)
			{
				dl = json_real_value(value2);
				elasticsearch_metric_set(carg, string2);
				metric_add_labels2(string2, &dl, DATATYPE_DOUBLE, carg, "cluster", cluster_name, "indice", (char*)key);
			}
			json_object_foreach(value2, key3, value3)
			{
				uint64_t clfls = 0;
				json_t *shard_status_json = json_object_get(value3, "status");
				char* shard_status = (char*)json_string_value(shard_status_json);
				if (shard_status)
				{
					es_emit_traffic_light4(carg, "elasticsearch_indice_shard_status", shard_status,
						"cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
				}

				json_t *shard_active_json = json_object_get(value3, "primary_active");
				if (json_typeof(shard_active_json) == JSON_TRUE)
				{
					uint64_t clst = 1;
					elasticsearch_metric_set(carg, "elasticsearch_indice_shard_active");
					metric_add_labels3("elasticsearch_indice_shard_active", &clst, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
				}
				else if (json_typeof(shard_active_json) == JSON_FALSE)
				{
					elasticsearch_metric_set(carg, "elasticsearch_indice_shard_active");
					metric_add_labels3("elasticsearch_indice_shard_active", &clfls, DATATYPE_UINT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
				}

				json_object_foreach(value3, key4, value4)
				{
					stringlen = strlen(key4);
					size_t copy4 = es_copy_bytes(sizeof(string), 27, stringlen);
					if (copy4)
						strlcpy(string+27, key4, copy4);

					int type4 = json_typeof(value4);
					if (type4 == JSON_INTEGER)
					{
						vl = json_integer_value(value4);
						elasticsearch_metric_set(carg, string);
						metric_add_labels3(string, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "indice", (char*)key, "shard", (char*)key3);
					}
					else if (type4 == JSON_REAL)
					{
						dl = json_real_value(value4);
						elasticsearch_metric_set(carg, string);
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
	(void)size;
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		return;

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_index_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
		return;
	}

	char* cluster_name = es_data->cluster_name;

	int64_t shards_attr;
	json_t *_shards = json_object_get(root, "_shards");
	shards_attr = json_integer_value(json_object_get(_shards, "total"));
	elasticsearch_metric_set(carg, "elasticsearch_shards_total");
	metric_add_labels("elasticsearch_shards_total", &shards_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	shards_attr = json_integer_value(json_object_get(_shards, "successful"));
	elasticsearch_metric_set(carg, "elasticsearch_shards_successful");
	metric_add_labels("elasticsearch_shards_successful", &shards_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	shards_attr = json_integer_value(json_object_get(_shards, "failed"));
	elasticsearch_metric_set(carg, "elasticsearch_shards_failed");
	metric_add_labels("elasticsearch_shards_failed", &shards_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	json_t *skipped_json = json_object_get(_shards, "skipped");
	if (skipped_json)
	{
		shards_attr = json_integer_value(skipped_json);
		elasticsearch_metric_set(carg, "elasticsearch_shards_skipped");
		metric_add_labels("elasticsearch_shards_skipped", &shards_attr, DATATYPE_INT, carg, "cluster", cluster_name);
	}

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
			size_t c2 = es_copy_bytes(sizeof(string2), 14, stringlen2);
			size_t c3 = es_copy_bytes(sizeof(string3), 14, stringlen2);
			size_t c4 = es_copy_bytes(sizeof(string4), 14, stringlen2);
			size_t c5 = es_copy_bytes(sizeof(string5), 14, stringlen2);
			if (c2) strlcpy(string2+14, node_key, c2);
			if (c3) strlcpy(string3+14, node_key, c3);
			if (c4) strlcpy(string4+14, node_key, c4);
			if (c5) strlcpy(string5+14, node_key, c5);

			int type = json_typeof(node_value);
			if (type == JSON_OBJECT)
			{
				json_t *node_value1;
				const char *node_key1;
				json_object_foreach(node_value, node_key1, node_value1)
				{
					stringlen3 = strlen(node_key1);
					es_poke(string3, sizeof(string3), stringlen2+14, '_');
					es_poke(string4, sizeof(string4), stringlen2+14, '_');
					es_poke(string5, sizeof(string5), stringlen2+14, '_');
					size_t off = stringlen2 + 1 + 14;
					size_t c3b = es_copy_bytes(sizeof(string3), off, stringlen3);
					size_t c4b = es_copy_bytes(sizeof(string4), off, stringlen3);
					size_t c5b = es_copy_bytes(sizeof(string5), off, stringlen3);
					if (c3b) strlcpy(string3 + off, node_key1, c3b);
					if (c4b) strlcpy(string4 + off, node_key1, c4b);
					if (c5b) strlcpy(string5 + off, node_key1, c5b);

					int type1 = json_typeof(node_value1);
					if ((type1 == JSON_INTEGER || type1 == JSON_TRUE || type1 == JSON_FALSE) && !es_skip_leaf_key(node_key1))
					{
						if (type1 == JSON_INTEGER)
							vl = json_integer_value(node_value1);
						else
							vl = (type1 == JSON_TRUE) ? 1 : 0;
						int bytes = 0;
						char key_buf[255];
						es_strip_in_bytes(node_key1, key_buf, sizeof(key_buf), &bytes);
						if (bytes)
							es_cat(string2, sizeof(string2), stringlen2+14, "_bytes");
						es_emit_labeled_number(carg, string2, &vl, DATATYPE_INT, cluster_name, (char*)key, "index", key_buf);
						if (bytes)
							es_poke(string2, sizeof(string2), stringlen2+14, 0);
					}
					else if (type1 == JSON_REAL && !es_skip_leaf_key(node_key1))
					{
						dl = json_real_value(node_value1);
						es_emit_labeled_number(carg, string2, &dl, DATATYPE_DOUBLE, cluster_name, (char*)key, "index", (char*)node_key1);
					}
					else if (type1 == JSON_OBJECT)
					{
						json_t *node_value2;
						const char *node_key2;
						json_object_foreach(node_value1, node_key2, node_value2)
						{
							int type2 = json_typeof(node_value2);
							if ((type2 == JSON_INTEGER || type2 == JSON_TRUE || type2 == JSON_FALSE) && !es_skip_leaf_key(node_key2))
							{
								if (type2 == JSON_INTEGER)
									vl = json_integer_value(node_value2);
								else
									vl = (type2 == JSON_TRUE) ? 1 : 0;
								int bytes = 0;
								char key_buf[255];
								es_strip_in_bytes(node_key2, key_buf, sizeof(key_buf), &bytes);
								if (bytes)
									es_cat(string3, sizeof(string3), stringlen3+stringlen2+1+14, "_bytes");
								es_emit_labeled_number(carg, string3, &vl, DATATYPE_INT, cluster_name, (char*)key, "index", key_buf);
								if (bytes)
									es_poke(string3, sizeof(string3), stringlen3+stringlen2+1+14, 0);
							}
							else if (type2 == JSON_REAL && !es_skip_leaf_key(node_key2))
							{
								dl = json_real_value(node_value2);
								es_emit_labeled_number(carg, string3, &dl, DATATYPE_DOUBLE, cluster_name, (char*)key, "index", (char*)node_key2);
							}
							else if (type2 == JSON_OBJECT)
							{
								stringlen4 = strlen(node_key2);
								/* third-level: '_' after key2+'_'+key3 (idx +15), third key starts at +16 */
								es_poke(string4, sizeof(string4), stringlen2+stringlen3+15, '_');
								es_poke(string5, sizeof(string5), stringlen2+stringlen3+15, '_');
								size_t off3 = stringlen3 + stringlen2 + 1 + 15;
								size_t c4c = es_copy_bytes(sizeof(string4), off3, stringlen4);
								size_t c5c = es_copy_bytes(sizeof(string5), off3, stringlen4);
								if (c4c) strlcpy(string4 + off3, node_key2, c4c);
								if (c5c) strlcpy(string5 + off3, node_key2, c5c);

								json_t *node_value3;
								const char *node_key3;
								json_object_foreach(node_value2, node_key3, node_value3)
								{
									int type3 = json_typeof(node_value3);
									if ((type3 == JSON_INTEGER || type3 == JSON_TRUE || type3 == JSON_FALSE) && !es_skip_leaf_key(node_key3))
									{
										if (type3 == JSON_INTEGER)
											vl = json_integer_value(node_value3);
										else
											vl = (type3 == JSON_TRUE) ? 1 : 0;
										int bytes = 0;
										char key_buf[255];
										es_strip_in_bytes(node_key3, key_buf, sizeof(key_buf), &bytes);
										if (bytes)
											es_cat(string4, sizeof(string4), stringlen4+stringlen3+stringlen2+1+15, "_bytes");
										es_emit_labeled_number(carg, string4, &vl, DATATYPE_INT, cluster_name, (char*)key, "index", key_buf);
										if (bytes)
											es_poke(string4, sizeof(string4), stringlen4+stringlen3+stringlen2+1+15, 0);
									}
									else if (type3 == JSON_REAL && !es_skip_leaf_key(node_key3))
									{
										dl = json_real_value(node_value3);
										es_emit_labeled_number(carg, string4, &dl, DATATYPE_DOUBLE, cluster_name, (char*)key, "index", (char*)node_key3);
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
	(void)size;
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		return;

	char* cluster_name = es_data->cluster_name;

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_settings_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
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
			size_t c2 = es_copy_bytes(sizeof(string2), 23, stringlen2);
			size_t c3 = es_copy_bytes(sizeof(string3), 23, stringlen2);
			size_t c4 = es_copy_bytes(sizeof(string4), 23, stringlen2);
			if (c2) strlcpy(string2+23, node_key, c2);
			if (c3) strlcpy(string3+23, node_key, c3);
			if (c4) strlcpy(string4+23, node_key, c4);

			json_t *node_value2;
			const char *node_key2;
			json_object_foreach(node_value, node_key2, node_value2)
			{
				stringlen3 = strlen(node_key2);
				es_poke(string3, sizeof(string3), stringlen2+23, '_');
				es_poke(string4, sizeof(string4), stringlen2+23, '_');
				size_t off = stringlen2 + 1 + 23;
				size_t c3b = es_copy_bytes(sizeof(string3), off, stringlen3);
				size_t c4b = es_copy_bytes(sizeof(string4), off, stringlen3);
				if (c3b) strlcpy(string3 + off, node_key2, c3b);
				if (c4b) strlcpy(string4 + off, node_key2, c4b);

				json_t *node_value3;
				const char *node_key3;
				json_object_foreach(node_value2, node_key3, node_value3)
				{
					stringlen4 = strlen(node_key3);
					es_poke(string4, sizeof(string4), stringlen2+stringlen3+24, '_');
					size_t off3 = stringlen2 + stringlen3 + 1 + 24;
					size_t c4c = es_copy_bytes(sizeof(string4), off3, stringlen4);
					if (c4c) strlcpy(string4 + off3, node_key3, c4c);

					char* val = (char*)json_string_value(node_value3);
					if (val)
					{
						int64_t vl = 0;
						int emit = 0;

						if (!strncmp(val, "true", 4))
						{
							vl = 1;
							emit = 1;
						}
						else if (!strncmp(val, "false", 5))
						{
							vl = 0;
							emit = 1;
						}
						else
						{
							char *end = NULL;
							errno = 0;
							long long ll = strtoll(val, &end, 10);
							if (end != val && *end == '\0' && errno != ERANGE)
							{
								vl = (int64_t)ll;
								emit = 1;
							}
						}

						if (emit)
						{
							char metric[255];
							es_emit_name(metric, sizeof(metric), string4);
							elasticsearch_metric_set(carg, metric);
							metric_add_labels2(metric, &vl, DATATYPE_INT, carg, "cluster", cluster_name, "index", (char*)key);
						}
					}
				}
			}
		}
	}
	json_decref(root);
	carg->parser_status = 1;
}

void elasticsearch_cluster_settings_handler(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	elastic_settings *es_data = carg->data;
	if (!es_data->cluster_name)
		return;

	char *cluster_name = es_data->cluster_name;

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_cluster_settings_handler: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
		return;
	}

	json_t *transient = json_object_get(root, "transient");
	json_t *persistent = json_object_get(root, "persistent");
	json_t *defaults = json_object_get(root, "defaults");

	const char *enabled = es_settings_pick(transient, persistent, defaults,
		"cluster.routing.allocation.disk.threshold_enabled");
	if (enabled)
	{
		int64_t vl = (!strcmp(enabled, "true")) ? 1 : 0;
		elasticsearch_metric_set(carg, "elasticsearch_disk_threshold_enabled");
		metric_add_labels("elasticsearch_disk_threshold_enabled", &vl, DATATYPE_INT, carg, "cluster", cluster_name);
	}

	struct {
		const char *setting;
		const char *percent_metric;
		const char *bytes_metric;
	} levels[] = {
		{ "cluster.routing.allocation.disk.watermark.low", "elasticsearch_disk_watermark_low_percent", "elasticsearch_disk_watermark_low_bytes" },
		{ "cluster.routing.allocation.disk.watermark.high", "elasticsearch_disk_watermark_high_percent", "elasticsearch_disk_watermark_high_bytes" },
		{ "cluster.routing.allocation.disk.watermark.flood_stage", "elasticsearch_disk_watermark_flood_stage_percent", "elasticsearch_disk_watermark_flood_stage_bytes" },
	};

	for (size_t i = 0; i < sizeof(levels)/sizeof(levels[0]); i++)
	{
		const char *raw = es_settings_pick(transient, persistent, defaults, levels[i].setting);
		if (!raw)
			continue;
		double percent = 0;
		int64_t bytes = 0;
		int is_percent = 0;
		if (!es_parse_watermark_value(raw, &percent, &bytes, &is_percent))
			continue;
		if (is_percent)
		{
			elasticsearch_metric_set(carg, levels[i].percent_metric);
			metric_add_labels((char *)levels[i].percent_metric, &percent, DATATYPE_DOUBLE, carg, "cluster", cluster_name);
		}
		else
		{
			elasticsearch_metric_set(carg, levels[i].bytes_metric);
			metric_add_labels((char *)levels[i].bytes_metric, &bytes, DATATYPE_INT, carg, "cluster", cluster_name);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void elasticsearch_response_catch(char *metrics, size_t size, context_arg *carg)
{
	(void)size;
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "elasticsearch_response_catch: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	int err = elasticsearch_check_for_error(carg, root);
	if (err) {
		json_decref(root);
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

	carglog(carg, L_DEBUG, "elasticsearch_response_catch: took=%"PRId64" errors=%d items=%"PRIu64"\n", took, errors, items_size);

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

		carglog(carg, L_TRACE, "\tid: %s, index: %s, result: %s, seq_no: %"PRId64", shards:[total: %"PRId64", successful: %"PRId64", failed: %"PRId64"]\n", id, _index, result, seq_no, total, successful, failed);
	}

	if (!errors)
		carg->parser_status = 1;

	json_decref(root);
}

string *elastic_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

string* elasticsearch_nodes_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_nodes/stats", env, proxy_settings); }
string* elasticsearch_cluster_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_cluster/stats", env, proxy_settings); }
string* elasticsearch_health_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_cluster/health?level=shards", env, proxy_settings); }
string* elasticsearch_index_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_stats", env, proxy_settings); }
string* elasticsearch_settings_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_settings", env, proxy_settings); }
string* elasticsearch_cluster_settings_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { (void)arg; return elastic_gen_url(hi, "/_cluster/settings?include_defaults=true&flat_settings=true", env, proxy_settings); }

void elasticsearch_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	elastic_settings *data = calloc(1, sizeof(*data));

	actx->key = strdup("elasticsearch");
	actx->handlers = 6;
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

	actx->handler[5].name = elasticsearch_cluster_settings_handler;
	actx->handler[5].validator = NULL;
	actx->handler[5].mesg_func = elasticsearch_cluster_settings_mesg;
	strlcpy(actx->handler[5].key,"elasticsearch_cluster_settings", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
