#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "metric/namespace.h"
#include "common/logs.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "dstructures/ht.h"
#include "metric/labels.h"
#include "main.h"
extern aconf* ac;
void json_parse_level(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl);

typedef struct jq_node {
	string_tokens *tokens;

	string *key;
	alligator_ht_node node;
} jq_node;

int json_query_node_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((jq_node*)obj)->key->s;
        return strcmp(s1, s2);
}

void json_parse_object(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	const char *key;
	json_t *value;

	json_object_foreach(element, key, value) {
		carglog(carg, L_DEBUG, "json '%s' parsed object prefix %s with key '%s'\n", carg->key, prefix->s, key);
		string *new_key = string_new();

		string_string_cat(new_key, prefix);
		string_cat(new_key, "_", 1);
		string_cat(new_key, (char*)key, strlen(key));
		json_parse_level(carg, value, new_key, lbl);

		string_free(new_key);
	}
}

void json_parse_array(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	uint64_t size = json_array_size(element);

	for (uint64_t i = 0; i < size; i++) {
		jq_node *jqn = alligator_ht_search(carg->data, json_query_node_compare, prefix->s, tommy_strhash_u32(0, prefix->s));
		json_t *value = json_array_get(element, i);

		string *new_key = string_new();
		alligator_ht *new_lbl;

		string_string_cat(new_key, prefix);
		if (lbl)
			new_lbl = labels_dup(lbl);
		else {
			new_lbl = alligator_ht_init(NULL);
		}

		for (uint64_t tkidx = 0; jqn && (tkidx < jqn->tokens->l); ++tkidx) {
			char *metric_key = jqn->tokens->str[tkidx]->s;
			json_t *metric_obj = json_object_get(value, metric_key);
			if (metric_obj) {
				int metric_type = json_typeof(metric_obj);
				if (metric_type == JSON_STRING) {
					char *metric_value = (char*)json_string_value(metric_obj);
					labels_hash_insert_nocache(new_lbl, metric_key, metric_value);
				}
				else if (metric_type == JSON_INTEGER) {
					int64_t metric_ivalue = json_integer_value(metric_obj);
					char metric_value[21];
					snprintf(metric_value, 20, "%"PRId64, metric_ivalue);
					labels_hash_insert_nocache(new_lbl, metric_key, metric_value);
				}
				else if (metric_type == JSON_REAL) {
					double metric_dvalue = json_real_value(metric_obj);
					char metric_value[21];
					snprintf(metric_value, 20, "%lf", metric_dvalue);
					labels_hash_insert_nocache(new_lbl, metric_key, metric_value);
				}
				else if (metric_type == JSON_TRUE) {
					labels_hash_insert_nocache(new_lbl, metric_key, "true");
				}
				else if (metric_type == JSON_FALSE) {
					labels_hash_insert_nocache(new_lbl, metric_key, "false");
				}
				else {
					carglog(carg, L_DEBUG, "json '%s' error during parsing the metric key '%s': unknown type, this label will be skipped\n", carg->key, metric_key);
				}
			}
		}

		json_parse_level(carg, value, new_key, new_lbl);

		string_free(new_key);
		labels_hash_free(new_lbl);
	}
}

void json_parse_string(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	char *key = (char*)json_string_value(element);
	if (key)
	{
		uint64_t okval = 1;
		carglog(carg, L_DEBUG, "json '%s' parsed string prefix %s with key '%s'\n", carg->key, prefix->s, key);
		prometheus_metric_name_normalizer(prefix->s, prefix->l);
		metric_label_value_validator_normalizer(key, strlen(key));
		alligator_ht *pass_lbl = labels_dup(lbl);
		metric_add(prefix->s, pass_lbl, &okval, DATATYPE_UINT, carg);
	}
}

void json_parse_integer(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	int64_t val = json_integer_value(element);
	carglog(carg, L_DEBUG, "json '%s' parsed int key '%s' %"PRId64"\n", carg->key, prefix->s, val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	alligator_ht *pass_lbl = labels_dup(lbl);
	metric_add(prefix->s, pass_lbl, &val, DATATYPE_INT, carg);
}

void json_parse_real(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	double val = json_real_value(element);
	carglog(carg, L_DEBUG, "json '%s' parsed real key '%s' %lf\n", carg->key, prefix->s, val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	alligator_ht *pass_lbl = labels_dup(lbl);
	metric_add(prefix->s, pass_lbl, &val, DATATYPE_DOUBLE, carg);
}

void json_parse_bool(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl, uint64_t okval) {
	carglog(carg, L_DEBUG, "json '%s' parsed bool key %s %"PRIu64"\n", carg->key, prefix->s, okval);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	alligator_ht *pass_lbl = labels_dup(lbl);
	metric_add(prefix->s, pass_lbl, &okval, DATATYPE_UINT, carg);
}

void json_parse_level(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl)
{
	int jsontype = json_typeof(element);
	if (jsontype==JSON_OBJECT) {
		json_parse_object(carg, element, prefix, lbl);
	}
	else if (jsontype==JSON_ARRAY) {
		json_parse_array(carg, element, prefix, lbl);
	}
	else if (jsontype==JSON_STRING) {
		json_parse_string(carg, element, prefix, lbl);
	}
	else if (jsontype == JSON_INTEGER) {
		json_parse_integer(carg, element, prefix, lbl);
	}
	else if (jsontype == JSON_REAL) {
		json_parse_real(carg, element, prefix, lbl);
	}
	else if (jsontype == JSON_TRUE) {
		json_parse_bool(carg, element, prefix, lbl, 1);
	}
	else if (jsontype == JSON_FALSE) {
		json_parse_bool(carg, element, prefix, lbl, 0);
	}
}

string_tokens *json_query_parser_object(context_arg *carg, char *obj, size_t sz, int *type) {
	string_tokens *tokens = string_tokens_new();
	uint8_t is_array = 0;
	uint8_t is_object = 0;
	uint64_t i = 0;
	char *key = NULL;
	uint64_t skips = 0;
	for (; i < sz; ++i) {
		if (obj[i] == '[') {
			*type = JSON_ARRAY;
			key = obj + i + 1;
			is_array = 1;
		}
		else if (obj[i] == ' ' && is_array) {
			carglog(carg, L_DEBUG, "\t1push token %s(%zu)\n", key, skips);
			string_tokens_push_dupn(tokens, key, skips);
			key = obj + i + 1;
		}
		else if (obj[i] == ']') {
			is_array = 0;
			size_t key_len = obj + i - key;
			carglog(carg, L_DEBUG, "\t2push token %s(%zu)\n", key, key_len);
			string_tokens_push_dupn(tokens, key, key_len);
			i += skips;
			skips = 0;
		}
		else if (obj[i] == '.' ) {
			carglog(carg, L_DEBUG, "\t\tis object: %c type %d is_object %d is array %d\n", obj[i], *type, is_object, is_array);
			*type = JSON_OBJECT;
			is_object = 1;
			key = obj + i + 1;
		}
		else {
			++skips;
			carglog(carg, L_DEBUG, "\t\tprocessing: %c type %d is_object %d is array %d\n", obj[i], *type, is_object, is_array);
		}
	}

	if (is_object) {
		size_t key_len = obj + i - key + 1;
		is_object = 0;
		carglog(carg, L_DEBUG, "\t3push token %s(%zu)\n", key, key_len);
		string_tokens_push_dupn(tokens, key, key_len);
	}

	return tokens;
}

alligator_ht* json_parse_query(context_arg *carg, char **query, uint8_t queries_size) {
	if (!query)
		return NULL;

	alligator_ht *ht = alligator_ht_init(NULL);

	for (uint64_t query_ind = 0; query_ind < queries_size; ++query_ind) {
		char *queries = query[query_ind];
		carglog(carg, L_INFO, "\njson parse query '%s'\n", queries);
		string *object_key = string_new();
		uint8_t is_object = 0;
		string_cat(object_key, "json", 4);
		size_t queries_len = strlen(queries);
		for (uint64_t i = 0; i < queries_len; ++i) {
			char *context = queries + i;
			size_t context_size = strcspn(context, "|");
			carglog(carg, L_DEBUG,"context '%s' size is %zu\n", context, context_size);

			for (uint64_t j = 0; j < context_size; ++j) {
				char *subctx = context + j;
				size_t subctx_size = strcspn(subctx, ",");
				carglog(carg, L_DEBUG,"subctx '%s' size is %zu\n", subctx, subctx_size);

				for (uint64_t k = 0; k < subctx_size; ) {
					char *object = subctx + k;
					size_t object_size = strcspn(object, ".");

					if ((!object_size) && (object[0] == '.') && (object[1] != '[')) {
						is_object = 1;
						object_size = strcspn(object+2, ".") + 1;
					}
					else if (!object_size) {
						++k;
						continue;
					}

					int type;
					string_tokens *tokens = json_query_parser_object(carg, object, object_size, &type);
					carglog(carg, L_DEBUG, "object is '%s'/%zu, results: %p, object: '%c%c'\n", object, object_size, tokens, object[0], object[1]);
					if (!tokens)
						continue;

					if (object[0] == '[') {
						jq_node *new = calloc(1, sizeof(*new));
						new->tokens = tokens;
						new->key = string_new();
						string_string_copy(new->key, object_key);
						carglog(carg, L_TRACE, "insert array '%s' -> '%s' (", new->key->s, object_key->s);
						for (uint64_t id = 0; id < tokens->l; carglog(carg, L_TRACE, "%s'%s'", id ? ", " : "", tokens->str[id]->s), ++id);
						carglog(carg, L_TRACE, ")\n");
						alligator_ht_insert(ht, &(new->node), new, tommy_strhash_u32(0, new->key->s));
					}
					else if (is_object) {
						jq_node *new = calloc(1, sizeof(*new));
						is_object = 0;
						new->key = string_string_init_dup(tokens->str[0]);
						string_cat(object_key, "_", 1);
						string_string_cat(object_key, new->key);
						carglog(carg, L_TRACE, "insert object '%s' -> '%s'\n", new->key->s, object_key->s);
						alligator_ht_insert(ht, &(new->node), new, tommy_strhash_u32(0, new->key->s));
						string_tokens_free(tokens);
					}
					else {
						string_tokens_free(tokens);
					}

					k += object_size;
				}

				j += subctx_size;
			}

			i += context_size;
		}
		string_free(object_key);
	}

	return ht;
}

void jq_node_free_foreach(void *funcarg, void* arg)
{
	jq_node *jqn = arg;
	if (jqn->tokens)
		string_tokens_free(jqn->tokens);
	string_free(jqn->key);
	free(jqn);
}

int json_query(char *data, json_t *root, char *prefix, context_arg *carg, char **queries, uint8_t queries_size) {
	json_error_t error;
	if (!root) {
		root = json_loads(data, 0, &error);
		if (!root) {
			carglog(carg, L_ERROR, "json '%s' error on line %d: %s\n", carg->key, error.line, error.text);
			return 0;
		}
	}

	carg->data = json_parse_query(carg, queries, queries_size);

	string *pass = string_new();
	alligator_ht *lbl = alligator_ht_init(NULL);

	string_cat(pass, prefix, strlen(prefix));
	json_parse_level(carg, root, pass, lbl);

	string_free(pass);
	labels_hash_free(lbl);
	alligator_ht_forfree(carg->data, jq_node_free_foreach);
	free(carg->data);

	return 1;
}

int8_t json_validator(context_arg *carg, char *data, size_t size)
{
	json_error_t error;
	json_t *root = json_loads(data, 0, &error);
	if (!root)
	{
		if (ac->log_level > 2)
			printf("json validator: json error on line %d: %s\n", error.line, error.text);
		return 0;
	}
	else if (ac->log_level > 2)
		puts("json validator OK");
	return 1;
}

void json_array_object_insert(json_t *dst_json, char *key, json_t *src_json)
{
	int json_type = json_typeof(dst_json);
	if (json_type == JSON_ARRAY)
		json_array_append_new(dst_json, src_json);
	else if (json_type == JSON_OBJECT)
		json_object_set_new(dst_json, key, src_json);
}

json_t *json_integer_string_set(char *str)
{
	json_t *value;
	if (isdigit(*str))
	{
		int64_t num = strtoll(str, NULL, 10);
		value = json_integer(num);
	}
	else
		value = json_string(str);

	return value;
}

int64_t config_get_intstr_json(json_t *aggregate, char *key)
{
	int64_t strintval;
	json_t *jstrintval = json_object_get(aggregate, key);
	if (jstrintval && json_typeof(jstrintval) == JSON_STRING)
		strintval = strtoull(json_string_value(jstrintval), NULL, 10);
	else
		strintval = json_integer_value(jstrintval);

	return strintval;
}

double config_get_floatstr_json(json_t *aggregate, char *key)
{
	double strintval;
	json_t *jstrintval = json_object_get(aggregate, key);
	if (jstrintval && json_typeof(jstrintval) == JSON_STRING)
		strintval = strtod(json_string_value(jstrintval), NULL);
	else
		strintval = json_real_value(jstrintval);

	return strintval;
}
