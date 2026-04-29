#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct bson_buf_t {
	uint8_t *data;
	size_t size;
	size_t capacity;
} bson_buf_t;

void bson_init(bson_buf_t *b);
void bson_append_int32(bson_buf_t *b, const char *key, int32_t val);
void bson_append_int64(bson_buf_t *b, const char *key, int64_t val);
void bson_append_double(bson_buf_t *b, const char *key, double val);
void bson_append_bool(bson_buf_t *b, const char *key, int val);
void bson_append_null(bson_buf_t *b, const char *key);
void bson_finish(bson_buf_t *b);
void bson_append_utf8(bson_buf_t *b, const char *key, const char *str);
void bson_append_binary(bson_buf_t *b, const char *key, const uint8_t *data, uint32_t len);
void bson_append_document_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len);
void bson_append_array_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len);
