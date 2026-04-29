#include <stdlib.h>
#include <string.h>

#include "parsers/bson.h"

static void bson_append_raw(bson_buf_t *b, const void *data, size_t len)
{
	while (b->size + len > b->capacity) {
		b->capacity *= 2;
		b->data = realloc(b->data, b->capacity);
	}
	memcpy(b->data + b->size, data, len);
	b->size += len;
}

void bson_init(bson_buf_t *b)
{
	b->capacity = 128;
	b->data = malloc(b->capacity);
	b->size = 4;
}

void bson_append_int32(bson_buf_t *b, const char *key, int32_t val)
{
	uint8_t type = 0x10;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &val, 4);
}

void bson_append_int64(bson_buf_t *b, const char *key, int64_t val)
{
	uint8_t type = 0x12;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &val, 8);
}

void bson_append_double(bson_buf_t *b, const char *key, double val)
{
	uint8_t type = 0x01;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &val, 8);
}

void bson_append_bool(bson_buf_t *b, const char *key, int val)
{
	uint8_t type = 0x08;
	uint8_t bval = val ? 1 : 0;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &bval, 1);
}

void bson_append_null(bson_buf_t *b, const char *key)
{
	uint8_t type = 0x0A;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
}

void bson_append_utf8(bson_buf_t *b, const char *key, const char *str)
{
	uint8_t type = 0x02;
	uint32_t len = (uint32_t)strlen(str) + 1;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &len, 4);
	bson_append_raw(b, str, len);
}

void bson_append_binary(bson_buf_t *b, const char *key, const uint8_t *data, uint32_t len)
{
	uint8_t type = 0x05;
	uint8_t subtype = 0x00;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &len, 4);
	bson_append_raw(b, &subtype, 1);
	bson_append_raw(b, data, len);
}

void bson_append_document_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len)
{
	uint8_t type = 0x03;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, doc, len);
}

void bson_append_array_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len)
{
	uint8_t type = 0x04;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, doc, len);
}

void bson_finish(bson_buf_t *b)
{
	uint8_t term = 0;
	uint32_t total;
	bson_append_raw(b, &term, 1);
	total = (uint32_t)b->size;
	memcpy(b->data, &total, 4);
}
