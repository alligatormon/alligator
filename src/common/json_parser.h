#pragma once
#include "events/context_arg.h"
#include <jansson.h>
void json_parser_entry(char *line, int argc, char **argv, char *name, context_arg *carg);
uint8_t json_check(const char *text);
int8_t json_validator(context_arg *carg, char *data, size_t size);
void json_array_object_insert(json_t *dst_json, char *key, json_t *src_json);
json_t *json_integer_string_set(char *str);
int64_t config_get_intstr_json(json_t *aggregate, char *key);
double config_get_floatstr_json(json_t *aggregate, char *key);
