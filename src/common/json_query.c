#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include "dstructures/tommyds/tommyds/tommy.h"
#include "metric/namespace.h"
#include "common/logs.h"
#include "common/validator.h"
#include "common/json_parser.h"
#include "events/context_arg.h"
#include "dstructures/ht.h"
#include "main.h"
extern aconf* ac;
void json_parse_level(context_arg *carg, json_t *element, string *prefix);

void json_parse_object(context_arg *carg, json_t *element, string *prefix) {
	const char *key;
	json_t *value;

	json_object_foreach(element, key, value) {
		carglog(carg, L_DEBUG, "json '%s' parsed object prefix %s with key '%s'\n", carg->key, prefix->s, key);
		string *new_key = string_new();
		string_string_cat(new_key, prefix);
		string_cat(new_key, "_", 1);
		string_cat(new_key, (char*)key, strlen(key));
		json_parse_level(carg, value, new_key);
		string_free(new_key);
	}
}

void json_parse_array(context_arg *carg, json_t *element, string *prefix) {
	uint64_t size = json_array_size(element);

	for (uint64_t i = 0; i < size; i++) {
		json_t *value = json_array_get(element, i);

		string *new_key = string_new();
		string_string_cat(new_key, prefix);
		json_parse_level(carg, value, new_key);
		string_free(new_key);
	}
}

void json_parse_string(context_arg *carg, json_t *element, string *prefix) {
	char *key = (char*)json_string_value(element);
	if (key)
	{
		uint64_t okval = 1;
		carglog(carg, L_DEBUG, "json '%s' parsed string prefix %s with key '%s'\n", carg->key, prefix->s, key);
		prometheus_metric_name_normalizer(prefix->s, prefix->l);
		metric_label_value_validator_normalizer(key, strlen(key));
		metric_add_labels(prefix->s, &okval, DATATYPE_UINT, carg, "object", key);
	}
}

void json_parse_integer(context_arg *carg, json_t *element, string *prefix) {
	int64_t val = json_integer_value(element);
	carglog(carg, L_DEBUG, "json '%s' parsed int key '%s' %"PRId64"\n", carg->key, prefix->s, val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	metric_add_auto(prefix->s, &val, DATATYPE_INT, carg);
}

void json_parse_real(context_arg *carg, json_t *element, string *prefix) {
	double val = json_real_value(element);
	carglog(carg, L_DEBUG, "json '%s' parsed real key '%s' %lf\n", carg->key, prefix->s, val);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	metric_add_auto(prefix->s, &val, DATATYPE_DOUBLE, carg);
}

void json_parse_bool(context_arg *carg, json_t *element, string *prefix, uint64_t okval) {
	carglog(carg, L_DEBUG, "json '%s' parsed bool key %s %"PRIu64"\n", carg->key, prefix->s, okval);
	prometheus_metric_name_normalizer(prefix->s, prefix->l);
	metric_add_auto(prefix->s, &okval, DATATYPE_UINT, carg);
}

void json_parse_level(context_arg *carg, json_t *element, string *prefix)
{
	int jsontype = json_typeof(element);
	if (jsontype==JSON_OBJECT) {
		json_parse_object(carg, element, prefix);
	}
	else if (jsontype==JSON_ARRAY) {
		json_parse_array(carg, element, prefix);
	}
	else if (jsontype==JSON_STRING) {
		json_parse_string(carg, element, prefix);
	}
	else if (jsontype == JSON_INTEGER) {
		json_parse_integer(carg, element, prefix);
	}
	else if (jsontype == JSON_REAL) {
		json_parse_real(carg, element, prefix);
	}
	else if (jsontype == JSON_TRUE) {
		json_parse_bool(carg, element, prefix, 1);
	}
	else if (jsontype == JSON_FALSE) {
		json_parse_bool(carg, element, prefix, 0);
	}
}

typedef struct jq_node {
	string_tokens *tokens;
	alligator_ht ht;

	string *key;
	alligator_ht_node node;
} jq_node;

string_tokens *json_query_parser_object(char *obj, size_t sz, int *type) {
	string_tokens *tokens = string_tokens_new();
	uint8_t is_array = 0;
	uint8_t is_object = 0;
	uint64_t i = 0;
	char *key = NULL;
	for (; i < sz; ++i) {
		if (obj[i] == '[') {
			*type = JSON_ARRAY;
			key = obj + i + 1;
			is_array = 1;
		}
		else if (obj[i] == ' ' && is_array) {
			size_t key_len = obj + i - key;
			string_tokens_push(tokens, key, key_len);
		}
		else if (obj[i] == ']') {
			is_array = 0;
			size_t key_len = obj + i - key;
			string_tokens_push(tokens, key, key_len);
		}
		else if (obj[i] == '.' ) {
			*type = JSON_OBJECT;
			is_object = 1;
			key = obj + i + 1;
		}
	}

	if (is_object) {
		size_t key_len = obj + i - key;
		is_object = 0;
		string_tokens_push(tokens, key, key_len);
	}

	return tokens;
}

jq_node* json_parse_query(char *queries) {
	if (!queries)
		return NULL;

	jq_node *jqn = calloc(1, sizeof(*jqn));
    alligator_ht_init(&jqn->ht);
	size_t queries_len = strlen(queries);
	for (uint64_t i = 0; i < queries_len; ++i) {
		char *context = queries + i;
		size_t context_size = strcspn(context, "|");

		for (uint64_t j = 0; j < context_size; ++j) {
			char *subctx = context + j;
			size_t subctx_size = strcspn(subctx, ",");

			for (uint64_t k = 0; k < subctx_size; ++k) {
				char *object = subctx + k;
				size_t object_size = strcspn(object, ".");
				if (!object_size)
					continue;

				printf("object is '%s'/%zu\n", object, object_size);
				int type;
				string_tokens *tokens = json_query_parser_object(object, object_size, &type);
				if (!tokens)
					continue;

				if (object[0] == '.') {
				    string_tokens *tokens = json_query_parser_object(object, object_size, &type);
					jq_node *new = calloc(1, sizeof(*jqn));
					new->key = tokens->str[0];
					alligator_ht_insert(&jqn->ht, &(new->node), new, tommy_strhash_u32(0, new->key->s));
				}
				else if (object[1] == '[') {
					jq_node *new = calloc(1, sizeof(*jqn));
					new->tokens = tokens;
					alligator_ht_insert(&jqn->ht, &(new->node), new, tommy_strhash_u32(0, new->key->s));
				}

				k += object_size;
			}

			j += subctx_size;
		}

		i += context_size;
	}

	return jqn;
}

int json_query(char *data, json_t *root, char *prefix, context_arg *carg, char *queries) {
	json_error_t error;
	if (!root) {
		root = json_loads(data, 0, &error);
		carglog(carg, L_ERROR, "json '%s' error on line %d: %s\n", carg->key, error.line, error.text);
		if (!root)
			return 0;
	}

	json_parse_query(queries);

	string *pass = string_new();
	string_cat(pass, prefix, strlen(prefix));
	json_parse_level(carg, root, pass);
	string_free(pass);

	return 1;
}
