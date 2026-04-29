#pragma once
#include <stddef.h>
#include <stdint.h>
#include <jansson.h>

typedef struct mongodb_query_expr_t {
	char collection[256];
	char filter_json[4096];
	int has_collection;
} mongodb_query_expr_t;

int mongodb_json_to_bson_document(const char *json, uint8_t **raw, uint32_t *raw_len);
int mongodb_parse_find_expr(const char *expr, mongodb_query_expr_t *out);
int bson_to_json_document(const uint8_t *data, size_t len, json_t **out, int as_array);
