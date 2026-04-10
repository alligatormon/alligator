#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include <ctype.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "metric/namespace.h"
#include "common/logs.h"
#include "common/validator.h"
#include "events/context_arg.h"
#include "dstructures/ht.h"
#include "metric/labels.h"
#include "main.h"
extern aconf* ac;

static const char *json_carg_key(const context_arg *carg)
{
	return (carg && carg->key) ? carg->key : "";
}

static const char *json_prefix_s(const string *prefix)
{
	return (prefix && prefix->s) ? prefix->s : "";
}

/* Avoid calling carglog when the line would be dropped: a variadic call costs a stack frame;
 * unit tests use a very deep main() and calloc'd cargs at L_OFF — entering carglog can SIGSEGV
 * before carglog's own early return runs. */
#define json_carglog_if(carg_, pri_, ...)					\
	do {									\
		context_arg *_json_c = (carg_);					\
		int _json_lv = _json_c ? _json_c->log_level : ac->log_level;	\
		if (_json_lv >= (pri_))						\
			carglog(_json_c, (pri_), __VA_ARGS__);			\
	} while (0)

void json_parse_level(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl);

typedef struct jq_node {
	string_tokens *label_fields;
	string_tokens *label_names;

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
		json_carglog_if(carg, L_DEBUG, "json '%s' parsed object prefix %s with key '%s'\n", json_carg_key(carg), json_prefix_s(prefix), key);
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

		for (uint64_t tkidx = 0; jqn && jqn->label_fields && jqn->label_names && (tkidx < jqn->label_fields->l) && (tkidx < jqn->label_names->l); ++tkidx) {
			char *metric_key = jqn->label_fields->str[tkidx]->s;
			char *label_name = jqn->label_names->str[tkidx]->s;
			json_t *metric_obj = json_object_get(value, metric_key);
			if (metric_obj) {
				int metric_type = json_typeof(metric_obj);
				if (metric_type == JSON_STRING) {
					char *metric_value = (char*)json_string_value(metric_obj);
					labels_hash_insert_nocache(new_lbl, label_name, metric_value);
				}
				else if (metric_type == JSON_INTEGER) {
					int64_t metric_ivalue = json_integer_value(metric_obj);
					char metric_value[21];
					snprintf(metric_value, 20, "%"PRId64, metric_ivalue);
					labels_hash_insert_nocache(new_lbl, label_name, metric_value);
				}
				else if (metric_type == JSON_REAL) {
					double metric_dvalue = json_real_value(metric_obj);
					char metric_value[21];
					snprintf(metric_value, 20, "%lf", metric_dvalue);
					labels_hash_insert_nocache(new_lbl, label_name, metric_value);
				}
				else if (metric_type == JSON_TRUE) {
					labels_hash_insert_nocache(new_lbl, label_name, "true");
				}
				else if (metric_type == JSON_FALSE) {
					labels_hash_insert_nocache(new_lbl, label_name, "false");
				}
				else {
					json_carglog_if(carg, L_DEBUG, "json '%s' error during parsing the metric key '%s': unknown type, this label will be skipped\n", json_carg_key(carg), metric_key);
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
		json_carglog_if(carg, L_DEBUG, "json '%s' parsed string prefix %s with key '%s'\n", json_carg_key(carg), json_prefix_s(prefix), key);
		prometheus_metric_name_normalizer(prefix->s, prefix->l);
		metric_label_value_validator_normalizer(key, strlen(key));
		alligator_ht *pass_lbl = labels_dup(lbl);
		metric_add(prefix->s, pass_lbl, &okval, DATATYPE_UINT, carg);
	}
}

void json_parse_integer(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	int64_t val = json_integer_value(element);
	json_carglog_if(carg, L_DEBUG, "json '%s' parsed int key '%s' %"PRId64"\n", json_carg_key(carg), json_prefix_s(prefix), val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	alligator_ht *pass_lbl = labels_dup(lbl);
	metric_add(prefix->s, pass_lbl, &val, DATATYPE_INT, carg);
}

void json_parse_real(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl) {
	double val = json_real_value(element);
	json_carglog_if(carg, L_DEBUG, "json '%s' parsed real key '%s' %lf\n", json_carg_key(carg), json_prefix_s(prefix), val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	alligator_ht *pass_lbl = labels_dup(lbl);
	metric_add(prefix->s, pass_lbl, &val, DATATYPE_DOUBLE, carg);
}

void json_parse_bool(context_arg *carg, json_t *element, string *prefix, alligator_ht *lbl, uint64_t okval) {
	json_carglog_if(carg, L_DEBUG, "json '%s' parsed bool key %s %"PRIu64"\n", json_carg_key(carg), json_prefix_s(prefix), okval);
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

static void json_query_trim_range(char *src, size_t *start, size_t *end)
{
	while ((*start) < (*end) && isspace((unsigned char)src[*start]))
		++(*start);
	while ((*end) > (*start) && isspace((unsigned char)src[*end - 1]))
		--(*end);
}

static size_t json_query_find_delim(char *src, size_t start, size_t end, char delim)
{
	size_t bracket_level = 0;
	for (size_t i = start; i < end; ++i) {
		if (src[i] == '[')
			++bracket_level;
		else if (src[i] == ']' && bracket_level)
			--bracket_level;
		else if (src[i] == delim && !bracket_level)
			return i;
	}
	return end;
}

static int json_query_labels_contains(jq_node *node, char *field, size_t field_len, char *name, size_t name_len)
{
	if (!node || !node->label_fields || !node->label_names)
		return 0;

	for (uint64_t i = 0; i < node->label_fields->l && i < node->label_names->l; ++i) {
		if ((node->label_fields->str[i]->l == field_len) &&
		    (node->label_names->str[i]->l == name_len) &&
		    !strncmp(node->label_fields->str[i]->s, field, field_len) &&
		    !strncmp(node->label_names->str[i]->s, name, name_len))
			return 1;
	}

	return 0;
}

static jq_node *json_query_get_or_create_node(alligator_ht *ht, char *prefix)
{
	jq_node *node = alligator_ht_search(ht, json_query_node_compare, prefix, tommy_strhash_u32(0, prefix));
	if (node)
		return node;

	node = calloc(1, sizeof(*node));
	node->key = string_init_dup(prefix);
	node->label_fields = string_tokens_new();
	node->label_names = string_tokens_new();
	alligator_ht_insert(ht, &(node->node), node, tommy_strhash_u32(0, node->key->s));
	return node;
}

static void json_query_add_label_pair(alligator_ht *ht, char *prefix, char *field, size_t field_len, char *name, size_t name_len)
{
	if (!field_len || !name_len)
		return;

	jq_node *node = json_query_get_or_create_node(ht, prefix);
	if (json_query_labels_contains(node, field, field_len, name, name_len))
		return;

	string_tokens_push_dupn(node->label_fields, field, field_len);
	string_tokens_push_dupn(node->label_names, name, name_len);
}

static void json_query_parse_label_block(alligator_ht *ht, char *prefix, char *src, size_t start, size_t end)
{
	size_t i = start;
	while (i < end) {
		while (i < end && (isspace((unsigned char)src[i]) || src[i] == ','))
			++i;
		if (i >= end)
			break;

		size_t tok_start = i;
		while (i < end && !isspace((unsigned char)src[i]) && src[i] != ',')
			++i;
		size_t tok_end = i;

		if (tok_end <= tok_start)
			continue;

		size_t map_pos = tok_start;
		while (map_pos < tok_end && src[map_pos] != ':' && src[map_pos] != '=')
			++map_pos;

		if (map_pos < tok_end) {
			json_query_add_label_pair(ht, prefix, src + tok_start, map_pos - tok_start, src + map_pos + 1, tok_end - map_pos - 1);
		} else {
			json_query_add_label_pair(ht, prefix, src + tok_start, tok_end - tok_start, src + tok_start, tok_end - tok_start);
		}
	}
}

static void json_query_apply_expr(alligator_ht *ht, char *in_prefix, char *expr, size_t expr_start, size_t expr_end, string_tokens *out_prefixes)
{
	string *current = string_init_dup(in_prefix);
	size_t i = expr_start;

	while (i < expr_end) {
		while (i < expr_end && (isspace((unsigned char)expr[i]) || expr[i] == '.'))
			++i;
		if (i >= expr_end)
			break;

		if (expr[i] == '[') {
			size_t block_start = i + 1;
			size_t block_end = block_start;
			while (block_end < expr_end && expr[block_end] != ']')
				++block_end;
			if (block_end <= expr_end)
				json_query_parse_label_block(ht, current->s, expr, block_start, block_end);
			i = block_end < expr_end ? block_end + 1 : expr_end;
			continue;
		}

		size_t key_start = i;
		while (i < expr_end && expr[i] != '.' && expr[i] != '[')
			++i;
		size_t key_end = i;
		json_query_trim_range(expr, &key_start, &key_end);

		if (key_end > key_start) {
			string_cat(current, "_", 1);
			string_cat(current, expr + key_start, key_end - key_start);
		}
	}

	string_tokens_push_dupn(out_prefixes, current->s, current->l);
	string_free(current);
}

alligator_ht* json_parse_query(context_arg *carg, char **query, uint8_t queries_size, char *prefix, uint64_t prefix_len) {
	if (!query)
		return NULL;

	alligator_ht *ht = alligator_ht_init(NULL);

	for (uint64_t query_ind = 0; query_ind < queries_size; ++query_ind) {
		char *queries = query[query_ind];
		if (!queries)
			continue;

		json_carglog_if(carg, L_INFO, "\njson parse query '%s'\n", queries);

		string_tokens *branches = string_tokens_new();
		if (prefix)
			string_tokens_push_dupn(branches, prefix, prefix_len);
		else
			string_tokens_push_dupn(branches, "json", 4);

		size_t queries_len = strlen(queries);
		size_t stage_start = 0;
		while (stage_start < queries_len) {
			size_t stage_end = json_query_find_delim(queries, stage_start, queries_len, '|');
			string_tokens *next_branches = string_tokens_new();

			for (uint64_t b = 0; b < branches->l; ++b) {
				size_t expr_start = stage_start;
				while (expr_start < stage_end) {
					size_t expr_end = json_query_find_delim(queries, expr_start, stage_end, ',');
					size_t tstart = expr_start;
					size_t tend = expr_end;
					json_query_trim_range(queries, &tstart, &tend);
					if (tend > tstart)
						json_query_apply_expr(ht, branches->str[b]->s, queries, tstart, tend, next_branches);

					if (expr_end == stage_end)
						break;
					expr_start = expr_end + 1;
				}
			}

			string_tokens_free(branches);
			branches = next_branches;
			stage_start = stage_end < queries_len ? stage_end + 1 : queries_len;
		}

		string_tokens_free(branches);
	}

	return ht;
}

void jq_node_free_foreach(void *funcarg, void* arg)
{
	jq_node *jqn = arg;
	if (jqn->label_fields)
		string_tokens_free(jqn->label_fields);
	if (jqn->label_names)
		string_tokens_free(jqn->label_names);
	string_free(jqn->key);
	free(jqn);
}

int json_query(char *data, json_t *root, char *prefix, context_arg *carg, char **queries, uint8_t queries_size) {
	int need_free = 1;
	int rc = 0;
	char *jq_key_owned = NULL;

	if (root)
		need_free = 0;

	/* Logger context: handlers often omit carg->key; use metric prefix for all json_query carglog lines. */
	if (carg && prefix && prefix[0] && !carg->key) {
		jq_key_owned = strdup(prefix);
		if (jq_key_owned)
			carg->key = jq_key_owned;
	}

	json_error_t error;
	if (!root) {
		root = json_loads(data, 0, &error);
		if (!root) {
			json_carglog_if(carg, L_ERROR, "json '%s' error on line %d: %s\n", json_carg_key(carg), error.line, error.text);
			if (jq_key_owned) {
				if (carg && carg->key == jq_key_owned)
					carg->key = NULL;
				free(jq_key_owned);
			}
			return 0;
		}
	}

	carg->data = json_parse_query(carg, queries, queries_size, prefix, strlen(prefix));

	string *pass = string_new();
	alligator_ht *lbl = alligator_ht_init(NULL);

	string_cat(pass, prefix, strlen(prefix));
	json_parse_level(carg, root, pass, lbl);

	string_free(pass);
	labels_hash_free(lbl);
	alligator_ht_forfree(carg->data, jq_node_free_foreach);
	free(carg->data);

	if (need_free)
		json_decref(root);

	rc = 1;
	if (jq_key_owned) {
		if (carg && carg->key == jq_key_owned)
			carg->key = NULL;
		free(jq_key_owned);
	}
	return rc;
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
