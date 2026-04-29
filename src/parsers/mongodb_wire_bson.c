#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <jansson.h>
#include "mongodb_wire_bson.h"

typedef struct bson_buf_t {
	uint8_t *data;
	size_t size;
	size_t capacity;
} bson_buf_t;

static void bson_init(bson_buf_t *b)
{
	b->capacity = 128;
	b->data = malloc(b->capacity);
	b->size = 4;
}

static void bson_append_raw(bson_buf_t *b, const void *data, size_t len)
{
	while (b->size + len > b->capacity) {
		b->capacity *= 2;
		b->data = realloc(b->data, b->capacity);
	}
	memcpy(b->data + b->size, data, len);
	b->size += len;
}

static void bson_append_int64(bson_buf_t *b, const char *key, int64_t val)
{
	uint8_t type = 0x12;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &val, 8);
}

static void bson_append_double(bson_buf_t *b, const char *key, double val)
{
	uint8_t type = 0x01;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &val, 8);
}

static void bson_append_bool(bson_buf_t *b, const char *key, int val)
{
	uint8_t type = 0x08;
	uint8_t bval = val ? 1 : 0;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &bval, 1);
}

static void bson_append_null(bson_buf_t *b, const char *key)
{
	uint8_t type = 0x0A;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
}

static void bson_append_utf8(bson_buf_t *b, const char *key, const char *str)
{
	uint8_t type = 0x02;
	uint32_t len = strlen(str) + 1;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, &len, 4);
	bson_append_raw(b, str, len);
}

static void bson_append_document_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len)
{
	uint8_t type = 0x03;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, doc, len);
}

static void bson_append_array_raw(bson_buf_t *b, const char *key, const uint8_t *doc, uint32_t len)
{
	uint8_t type = 0x04;
	bson_append_raw(b, &type, 1);
	bson_append_raw(b, key, strlen(key) + 1);
	bson_append_raw(b, doc, len);
}

static void bson_finish(bson_buf_t *b)
{
	uint8_t end = 0x00;
	uint32_t total;
	bson_append_raw(b, &end, 1);
	total = (uint32_t)b->size;
	memcpy(b->data, &total, 4);
}

static int json_value_to_bson_field(bson_buf_t *b, const char *key, json_t *v);

static int json_to_bson_document_bytes(json_t *obj, uint8_t **out, uint32_t *out_len)
{
	const char *k;
	json_t *v;
	bson_buf_t b;

	if (!json_is_object(obj) || !out || !out_len)
		return 0;

	bson_init(&b);
	json_object_foreach(obj, k, v) {
		if (!json_value_to_bson_field(&b, k, v)) {
			free(b.data);
			return 0;
		}
	}
	bson_finish(&b);
	*out = b.data;
	*out_len = (uint32_t)b.size;
	return 1;
}

static int json_value_to_bson_field(bson_buf_t *b, const char *key, json_t *v)
{
	if (json_is_string(v)) {
		bson_append_utf8(b, key, json_string_value(v));
		return 1;
	}
	if (json_is_integer(v)) {
		bson_append_int64(b, key, (int64_t)json_integer_value(v));
		return 1;
	}
	if (json_is_real(v)) {
		bson_append_double(b, key, json_real_value(v));
		return 1;
	}
	if (json_is_boolean(v)) {
		bson_append_bool(b, key, json_is_true(v));
		return 1;
	}
	if (json_is_null(v)) {
		bson_append_null(b, key);
		return 1;
	}
	if (json_is_object(v)) {
		uint8_t *raw = NULL;
		uint32_t raw_len = 0;
		if (!json_to_bson_document_bytes(v, &raw, &raw_len))
			return 0;
		bson_append_document_raw(b, key, raw, raw_len);
		free(raw);
		return 1;
	}
	if (json_is_array(v)) {
		bson_buf_t arr;
		char idx[32];
		size_t i;
		bson_init(&arr);
		for (i = 0; i < json_array_size(v); i++) {
			snprintf(idx, sizeof(idx), "%zu", i);
			if (!json_value_to_bson_field(&arr, idx, json_array_get(v, i))) {
				free(arr.data);
				return 0;
			}
		}
		bson_finish(&arr);
		bson_append_array_raw(b, key, arr.data, (uint32_t)arr.size);
		free(arr.data);
		return 1;
	}
	return 0;
}

int mongodb_json_to_bson_document(const char *json, uint8_t **raw, uint32_t *raw_len)
{
	json_error_t err;
	json_t *obj = json_loads(json, 0, &err);
	int ok;

	if (!obj || !json_is_object(obj)) {
		if (obj)
			json_decref(obj);
		return 0;
	}
	ok = json_to_bson_document_bytes(obj, raw, raw_len);
	json_decref(obj);
	return ok;
}

int mongodb_parse_find_expr(const char *expr, mongodb_query_expr_t *out)
{
	const char *p, *dot, *lpar, *rpar;
	size_t coll_len, json_len;
	if (!expr || !out)
		return 0;

	memset(out, 0, sizeof(*out));
	p = expr;
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;

	/* db.<collection>.find({...}) */
	if (!strncmp(p, "db.", 3)) {
		p += 3;
		dot = strchr(p, '.');
		lpar = strchr(p, '(');
		rpar = strrchr(p, ')');
		if (!dot || !lpar || !rpar || dot > lpar)
			return 0;

		coll_len = (size_t)(dot - p);
		if (!coll_len || coll_len >= sizeof(out->collection))
			return 0;
		memcpy(out->collection, p, coll_len);
		out->collection[coll_len] = '\0';
		out->has_collection = 1;

		if (strncmp(dot + 1, "find", 4))
			return 0;
		json_len = (size_t)(rpar - lpar - 1);
		if (!json_len || json_len >= sizeof(out->filter_json))
			return 0;
		memcpy(out->filter_json, lpar + 1, json_len);
		out->filter_json[json_len] = '\0';
		return 1;
	}

	/* plain JSON filter */
	if (*p == '{') {
		strlcpy(out->filter_json, p, sizeof(out->filter_json));
		return 1;
	}

	return 0;
}

static int bson_to_json_value(const uint8_t *data, size_t len, size_t *off, uint8_t type, json_t **out)
{
	if (!data || !off || !out || *off > len)
		return 0;

	switch (type) {
	case 0x01: {
		double v = 0;
		if (*off + 8 > len)
			return 0;
		memcpy(&v, data + *off, 8);
		*off += 8;
		*out = json_real(v);
		return 1;
	}
	case 0x02: {
		int32_t slen = 0;
		if (*off + 4 > len)
			return 0;
		memcpy(&slen, data + *off, 4);
		if (slen <= 0 || *off + 4 + (size_t)slen > len)
			return 0;
		*out = json_stringn_nocheck((const char *)(data + *off + 4), (size_t)slen - 1);
		if (!*out)
			*out = json_string("<invalid-utf8>");
		*off += 4 + (size_t)slen;
		return 1;
	}
	case 0x03: {
		int used = bson_to_json_document(data + *off, len - *off, out, 0);
		if (!used)
			return 0;
		*off += (size_t)used;
		return 1;
	}
	case 0x04: {
		int used = bson_to_json_document(data + *off, len - *off, out, 1);
		if (!used)
			return 0;
		*off += (size_t)used;
		return 1;
	}
	case 0x07: {
		char oid[25];
		if (*off + 12 > len)
			return 0;
		for (int i = 0; i < 12; ++i)
			snprintf(oid + i * 2, 3, "%02x", data[*off + i]);
		oid[24] = '\0';
		*off += 12;
		*out = json_string(oid);
		return 1;
	}
	case 0x05: {
		int32_t blen = 0;
		if (*off + 5 > len)
			return 0;
		memcpy(&blen, data + *off, 4);
		if (blen < 0 || *off + 5 + (size_t)blen > len)
			return 0;
		*out = json_stringn_nocheck((const char *)(data + *off + 5), (size_t)blen);
		if (!*out)
			*out = json_string("<binary>");
		*off += 5 + (size_t)blen;
		return 1;
	}
	case 0x08:
		if (*off + 1 > len)
			return 0;
		*out = data[*off] ? json_true() : json_false();
		*off += 1;
		return 1;
	case 0x09: {
		int64_t ms = 0;
		if (*off + 8 > len)
			return 0;
		memcpy(&ms, data + *off, 8);
		*off += 8;
		*out = json_integer(ms);
		return 1;
	}
	case 0x0A:
		*out = json_null();
		return 1;
	case 0x10: {
		int32_t v = 0;
		if (*off + 4 > len)
			return 0;
		memcpy(&v, data + *off, 4);
		*off += 4;
		*out = json_integer(v);
		return 1;
	}
	case 0x12: {
		int64_t v = 0;
		if (*off + 8 > len)
			return 0;
		memcpy(&v, data + *off, 8);
		*off += 8;
		*out = json_integer(v);
		return 1;
	}
	default:
		*out = json_string("<unsupported>");
		return 1;
	}
}

int bson_to_json_document(const uint8_t *data, size_t len, json_t **out, int as_array)
{
	int32_t doc_len = 0;
	size_t pos = 0;
	json_t *root = as_array ? json_array() : json_object();

	if (!len || !data || !out || len < 5 || !root)
		return 0;

	memcpy(&doc_len, data, 4);
	if (doc_len < 5 || (size_t)doc_len > len) {
		json_decref(root);
		return 0;
	}

	pos = 4;
	while (pos < (size_t)doc_len - 1) {
		uint8_t type;
		const char *key;
		size_t key_len;
		json_t *val = NULL;

		type = data[pos++];
		key = (const char *)(data + pos);
		key_len = strnlen(key, (size_t)doc_len - pos);
		if (pos + key_len >= (size_t)doc_len) {
			json_decref(root);
			return 0;
		}
		pos += key_len + 1;
		if (!bson_to_json_value(data, (size_t)doc_len, &pos, type, &val)) {
			json_decref(root);
			return 0;
		}
		if (as_array)
			json_array_append_new(root, val);
		else
			json_object_set_new(root, key, val);
	}

	if (data[doc_len - 1] != 0) {
		json_decref(root);
		return 0;
	}
	*out = root;
	return doc_len;
}
