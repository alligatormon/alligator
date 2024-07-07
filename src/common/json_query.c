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

int json_query(char *data, json_t *root, char *prefix, context_arg *carg, char *queries, char *describes) {
	json_error_t error;
	if (!root) {
		root = json_loads(data, 0, &error);
		carglog(carg, L_ERROR, "json '%s' error on line %d: %s\n", describes, error.line, error.text);
		if (!root)
			return 0;
	}

	string *pass = string_new();
	string_cat(pass, prefix, strlen(prefix));
	json_parse_level(carg, root, pass);
    string_free(pass);

	return 1;
}
